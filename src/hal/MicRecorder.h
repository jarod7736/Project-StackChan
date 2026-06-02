#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace stkchan {

class MicRecorder {
 public:
  bool begin();             // allocates PSRAM buffer
  bool start();             // returns false if already recording
  bool stop();              // returns false if not recording
  bool isActive() const { return active_; }

  // After stop(), these point at a WAV-formatted buffer in PSRAM.
  // Valid until the next start().
  const uint8_t* wavData() const { return wav_; }
  size_t         wavSize() const { return wavSize_; }

 private:
  uint8_t* wav_      = nullptr;   // WAV header + PCM samples
  size_t   wavCap_   = 0;
  size_t   wavSize_  = 0;
  bool     active_   = false;
  uint32_t startMs_  = 0;

  void writeWavHeader_(uint32_t sampleCount);
};

extern MicRecorder mic;

}  // namespace stkchan
