#pragma once

// Shared 44-byte PCM16-mono WAV header writer. Used by MicRecorder
// (press-and-hold capture) and WakeListener (continuous wake capture).
// Pure (no Arduino) so it tests under `pio test -e native`.
// Little-endian layout — true on both Xtensa and x86 test hosts.

#include <stdint.h>
#include <string.h>

namespace stkchan {

constexpr size_t kWavHeaderBytes = 44;

// dst must have at least kWavHeaderBytes of room.
inline void writeWavHeader(uint8_t* dst, uint32_t sampleCount,
                           uint32_t sampleRate) {
  uint32_t dataBytes = sampleCount * 2u;            // mono int16
  uint32_t fileBytes = dataBytes + kWavHeaderBytes - 8u;
  memcpy(dst + 0, "RIFF", 4);
  memcpy(dst + 4, &fileBytes, 4);
  memcpy(dst + 8, "WAVE", 4);
  memcpy(dst + 12, "fmt ", 4);
  uint32_t fmtChunkSize = 16;
  memcpy(dst + 16, &fmtChunkSize, 4);
  uint16_t audioFormat = 1;   // PCM
  uint16_t numChannels = 1;
  uint16_t bitsPerSamp = 16;
  uint32_t byteRate    = sampleRate * 2u;
  uint16_t blockAlign  = 2;
  memcpy(dst + 20, &audioFormat, 2);
  memcpy(dst + 22, &numChannels, 2);
  memcpy(dst + 24, &sampleRate, 4);
  memcpy(dst + 28, &byteRate, 4);
  memcpy(dst + 32, &blockAlign, 2);
  memcpy(dst + 34, &bitsPerSamp, 2);
  memcpy(dst + 36, "data", 4);
  memcpy(dst + 40, &dataBytes, 4);
}

}  // namespace stkchan
