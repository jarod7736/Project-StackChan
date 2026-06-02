#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace stkchan {

class SttClient {
 public:
  // Returns true and fills `out` on success, false on any failure.
  bool transcribe(const uint8_t* wavData, size_t wavSize, String& out);
};

extern SttClient stt;

}  // namespace stkchan
