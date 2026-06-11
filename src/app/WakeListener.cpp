#include "app/WakeListener.h"

#include <M5CoreS3.h>
#include <math.h>

#include "config.h"
#include "hal/AudioPlayer.h"
#include "hal/MicRecorder.h"
#include "hal/WavHeader.h"
#include "app/WakeMatch.h"
#include "app/WakeVad.h"
#include "net/SttClient.h"
#include "net/ConnectivityTier.h"
#include "services/NvsStore.h"
#include "services/OtaService.h"
#include "state_machine.h"

namespace stkchan {

WakeListener wakeListener;

namespace {
WakeVad g_vad;  // floor persists across capture restarts (same room)

float frameRms(const int16_t* s, size_t n) {
  // Plain mean-square over 480 samples; float accumulator is plenty.
  float acc = 0;
  for (size_t i = 0; i < n; ++i) acc += (float)s[i] * (float)s[i];
  return sqrtf(acc / (float)n);
}
}  // namespace

bool WakeListener::begin() {
  enabled_  = (nvs.getString(kNvsWakeEn, "1") != "0");  // any value but "0" = on
  wakeWord_ = nvs.getString(kNvsWakeWord, kDefaultWakeWord);
  if (!enabled_) {
    Serial.println("[Wake] disabled (wake_en=0)");
    return true;
  }
  capSamples_ = (size_t)kWakeBufSeconds * kRecordSampleRate;
  pcm_ = static_cast<int16_t*>(ps_malloc(capSamples_ * sizeof(int16_t)));
  wav_ = static_cast<uint8_t*>(
      ps_malloc(kWavHeaderBytes + capSamples_ * sizeof(int16_t)));
  if (!pcm_ || !wav_) {
    Serial.println("[Wake] ps_malloc failed — wake disabled");
    enabled_ = false;
    return false;
  }
  Serial.printf("[Wake] ready, phrase=\"%s\"\n", wakeWord_.c_str());
  return true;
}

bool WakeListener::conditionsOk_() const {
  return enabled_ && currentState() == State::IDLE && !audio.isPlaying() &&
         !mic.isActive() && !OtaService::isActive();
}

void WakeListener::pause() {
  if (!capturing_) return;
  M5.Mic.end();
  audio.reapplySpeakerConfig();  // speaker back online at the right rate
  capturing_ = false;
  g_vad.reset();
}

void WakeListener::startCapture_() {
  // Same I2S handoff as MicRecorder::start(): speaker must release I2S0
  // first or spk_task crashes on the next play.
  M5.Speaker.end();
  M5.Mic.setSampleRate(kRecordSampleRate);
  if (!M5.Mic.record(pcm_, capSamples_, kRecordSampleRate)) {
    Serial.println("[Wake] M5.Mic.record failed");
    audio.reapplySpeakerConfig();
    return;  // retry next tick
  }
  capturing_       = true;
  captureStartMs_  = millis();
  processedFrames_ = 0;
  tripFrame_       = 0;
  g_vad.reset();
}

void WakeListener::tick(uint32_t nowMs) {
  if (!enabled_) return;

  if (!conditionsOk_()) {
    pause();
    quietSinceMs_ = nowMs;  // restart the resume guard
    return;
  }

  if (!capturing_) {
    if (nowMs - quietSinceMs_ < kWakeResumeGuardMs) return;  // voice-tail guard
    if (nowMs < cooldownUntilMs_) return;                    // non-match cooldown
    startCapture_();
    return;
  }

  processNewFrames_(nowMs);
}

void WakeListener::processNewFrames_(uint32_t nowMs) {
  // The I2S task fills pcm_ in the background; estimate the fill point from
  // elapsed time (the same trick MicRecorder::stop() uses), one frame behind
  // to never read a partially-written frame.
  uint32_t elapsedMs    = nowMs - captureStartMs_;
  size_t   filledFrames = ((size_t)elapsedMs * kRecordSampleRate / 1000)
                          / kWakeFrameSamples;
  if (filledFrames > 0) filledFrames -= 1;
  size_t totalFrames = capSamples_ / kWakeFrameSamples;
  if (filledFrames > totalFrames) filledFrames = totalFrames;

  while (processedFrames_ < filledFrames) {
    const int16_t* f = pcm_ + processedFrames_ * kWakeFrameSamples;
    WakeVad::Event ev = g_vad.onFrame(frameRms(f, kWakeFrameSamples));
    ++processedFrames_;

    if (ev == WakeVad::Event::Tripped) {
      tripFrame_ = (processedFrames_ > kWakeLeadInFrames)
                       ? processedFrames_ - kWakeLeadInFrames : 0;
    } else if (ev == WakeVad::Event::Closed ||
               ev == WakeVad::Event::Overflow) {
      size_t first = tripFrame_ * kWakeFrameSamples;
      size_t end   = processedFrames_ * kWakeFrameSamples;
      pause();  // mic off + speaker restored BEFORE the blocking HTTP call
      submitWindow_(first, end);
      return;
    }
  }

  // Buffer exhausted without a speech window → seamless restart.
  if (processedFrames_ >= totalFrames - 1 && !g_vad.tripped()) {
    M5.Mic.end();
    capturing_ = false;
    startCapture_();  // floor persists; VAD run state resets inside
  }
}

void WakeListener::submitWindow_(size_t firstSample, size_t endSample) {
  if (connectivity.current() != Tier::LAN_OK) return;  // silent skip offline
  size_t n = endSample - firstSample;
  if (n < kWakeFrameSamples * 5) return;  // <150 ms of audio: noise, skip

  writeWavHeader(wav_, (uint32_t)n, kRecordSampleRate);
  memcpy(wav_ + kWavHeaderBytes, pcm_ + firstSample, n * sizeof(int16_t));

  // Blocking HTTP on the main loop — same invariant as every other call in
  // this codebase. The robot is idle; the face freezes ~1 s worst case.
  String transcript;
  bool ok = stt.transcribe(wav_, kWavHeaderBytes + n * sizeof(int16_t),
                           transcript);
  if (!ok || transcript.isEmpty()) {
    cooldownUntilMs_ = millis() + kWakeCooldownMs;
    return;  // silent failure by design
  }

  auto m = matchWake(std::string(transcript.c_str()),
                     std::string(wakeWord_.c_str()));
  if (!m.matched) {
    Serial.printf("[Wake] no match: \"%s\"\n", transcript.c_str());
    cooldownUntilMs_ = millis() + kWakeCooldownMs;
    return;
  }

  Serial.printf("[Wake] MATCHED, remainder=\"%s\"\n", m.remainder.c_str());
  if (!onWakeDetected(String(m.remainder.c_str()))) {
    cooldownUntilMs_ = millis() + kWakeCooldownMs;  // FSM busy/refused
  }
}

}  // namespace stkchan
