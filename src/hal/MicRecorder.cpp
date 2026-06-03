#include "hal/MicRecorder.h"
#include <M5CoreS3.h>
#include "config.h"
#include "hal/AudioPlayer.h"

namespace stkchan {

MicRecorder mic;

static constexpr size_t kHeaderBytes = 44;  // RIFF / WAVE / fmt / data

bool MicRecorder::begin() {
  if (wav_) return true;
  wavCap_ = kRecordMaxBytes + kHeaderBytes;
  wav_ = static_cast<uint8_t*>(ps_malloc(wavCap_));
  if (!wav_) {
    Serial.println("ERR: MicRecorder ps_malloc failed");
    wavCap_ = 0;
    return false;
  }
  return true;
}

bool MicRecorder::start() {
  if (!wav_ || active_) return false;

  // The CoreS3 shares I2S0 between speaker and mic — bringing up the mic
  // while the speaker holds I2S leaves spk_task with stale DMA descriptors
  // and crashes the next audio.play() with a LoadProhibited / stack-canary
  // panic. Tear the speaker down before claiming I2S for capture; stop()
  // brings it back.
  M5.Speaker.end();

  // Queue a full-capacity recording into the PCM region of the PSRAM buffer.
  // M5.Mic.record() is asynchronous — the background I2S task fills the
  // buffer while loop() continues.  stop() will call M5.Mic.end() to halt
  // the task and then trim the sample count using elapsed time.
  size_t   capSamples = kRecordMaxBytes / sizeof(int16_t);
  int16_t* dst        = reinterpret_cast<int16_t*>(wav_ + kHeaderBytes);

  M5.Mic.setSampleRate(kRecordSampleRate);
  if (!M5.Mic.record(dst, capSamples, kRecordSampleRate)) {
    Serial.println("ERR: M5.Mic.record failed");
    // Restore the speaker even on failed mic start — otherwise the next
    // audio.play() would crash spk_task.
    audio.reapplySpeakerConfig();
    return false;
  }

  active_  = true;
  startMs_ = millis();
  wavSize_ = kHeaderBytes;  // reserve header; filled in stop()
  return true;
}

bool MicRecorder::stop() {
  if (!active_) return false;

  uint32_t elapsedMs = millis() - startMs_;

  // Halt the background I2S task.  Mic_Class::end() sets _task_running=false
  // and spin-waits (vTaskDelay) until the task exits, so the buffer is no
  // longer being written after this returns.
  M5.Mic.end();

  // Bring the speaker back online so AudioPlayer can drive I2S for TTS
  // playback. We have to re-apply AudioPlayer's full config (sample rate,
  // gain) — a bare M5.Speaker.begin() leaves the speaker on M5Unified
  // defaults (44.1 kHz) and TTS plays as static.
  audio.reapplySpeakerConfig();

  // Calculate samples actually captured from elapsed wall time, capped at
  // the buffer capacity.
  size_t capSamples   = kRecordMaxBytes / sizeof(int16_t);
  size_t sampleCount  = (size_t)(elapsedMs) * kRecordSampleRate / 1000;
  if (sampleCount > capSamples) sampleCount = capSamples;

  wavSize_ = kHeaderBytes + sampleCount * sizeof(int16_t);
  writeWavHeader_(sampleCount);
  active_ = false;

  Serial.printf("MicRecorder: %u samples (%u ms), %u bytes WAV\n",
                (unsigned)sampleCount,
                (unsigned)(sampleCount * 1000 / kRecordSampleRate),
                (unsigned)wavSize_);
  return true;
}

void MicRecorder::writeWavHeader_(uint32_t sampleCount) {
  uint32_t dataBytes = sampleCount * sizeof(int16_t);
  uint32_t fileBytes = dataBytes + kHeaderBytes - 8;

  // RIFF
  memcpy(wav_ + 0, "RIFF", 4);
  memcpy(wav_ + 4, &fileBytes, 4);
  memcpy(wav_ + 8, "WAVE", 4);
  // fmt
  memcpy(wav_ + 12, "fmt ", 4);
  uint32_t fmtChunkSize = 16;
  memcpy(wav_ + 16, &fmtChunkSize, 4);
  uint16_t audioFormat = 1;        // PCM
  uint16_t numChannels = 1;
  uint32_t sampleRate  = kRecordSampleRate;
  uint16_t bitsPerSamp = 16;
  uint32_t byteRate    = sampleRate * numChannels * (bitsPerSamp / 8);
  uint16_t blockAlign  = numChannels * (bitsPerSamp / 8);
  memcpy(wav_ + 20, &audioFormat, 2);
  memcpy(wav_ + 22, &numChannels, 2);
  memcpy(wav_ + 24, &sampleRate,  4);
  memcpy(wav_ + 28, &byteRate,    4);
  memcpy(wav_ + 32, &blockAlign,  2);
  memcpy(wav_ + 34, &bitsPerSamp, 2);
  // data
  memcpy(wav_ + 36, "data", 4);
  memcpy(wav_ + 40, &dataBytes, 4);
}

}  // namespace stkchan
