#pragma once
#include <Arduino.h>

namespace stkchan {

class Display {
 public:
  void begin();
  void showStatusOverlay(const String& text, uint16_t fgColor);  // ephemeral
  void clearOverlay();
};

extern Display display;

}  // namespace stkchan
