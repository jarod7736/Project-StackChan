#pragma once

// WakeListener — always-on wake-word capture (VAD-then-confirm).
//
// Runs ONLY while the robot is idle: captures mic audio continuously using
// MicRecorder's proven single-PSRAM-buffer pattern, feeds 30 ms RMS frames
// to WakeVad, and on a closed speech window transcribes it via the existing
// SttClient. If the transcript fuzzy-matches the wake phrase (WakeMatch),
// fires onWakeDetected(remainder) into the FSM.
//
// HARDWARE INVARIANT (see MicRecorder::start/stop): CoreS3 shares I2S0
// between mic and speaker. While this listener captures, THE SPEAKER IS
// DOWN. pause() must be called before ANY audio playback or mic.start();
// it is idempotent and restores the speaker. tick() re-arms capture only
// after the FSM has been idle and audio-quiet for kWakeResumeGuardMs.
//
// All failures are silent (discard + cooldown) — wake attempts never speak
// errors. Spec: docs/superpowers/specs/2026-06-11-wake-word-design.md.

#include <Arduino.h>

namespace stkchan {

class WakeListener {
 public:
  bool begin();              // PSRAM buffers + NVS config; call once in setup()
  void tick(uint32_t nowMs); // call every loop() iteration
  void pause();              // idempotent: stop mic capture, restore speaker
  bool isCapturing() const { return capturing_; }

 private:
  bool conditionsOk_() const;     // IDLE && !audio && !mic && !OTA && enabled
  void startCapture_();
  void processNewFrames_(uint32_t nowMs);
  void submitWindow_(size_t firstSample, size_t endSample);

  int16_t* pcm_        = nullptr;  // PSRAM: kWakeBufSeconds of mono int16
  uint8_t* wav_        = nullptr;  // PSRAM: header + window copy for STT
  size_t   capSamples_ = 0;
  bool     enabled_    = false;    // NVS wake_en
  String   wakeWord_;              // NVS wake_word
  bool     capturing_  = false;
  uint32_t captureStartMs_ = 0;
  size_t   processedFrames_ = 0;   // frames already fed to the VAD
  size_t   tripFrame_       = 0;   // frame index where the VAD tripped
  uint32_t quietSinceMs_    = 0;   // for the resume guard
  uint32_t cooldownUntilMs_ = 0;   // after a non-match
};

extern WakeListener wakeListener;

}  // namespace stkchan
