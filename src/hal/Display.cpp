#include "hal/Display.h"
#include <M5CoreS3.h>

namespace stkchan {

Display display;

void Display::begin() {
  // Avatar owns the main canvas; here we only need the overlay layer.
  M5.Display.setTextSize(2);
}

void Display::showStatusOverlay(const String& text, uint16_t fgColor) {
  int w = M5.Display.width();
  int h = M5.Display.height();
  M5.Display.fillRect(0, h - 28, w, 28, BLACK);
  M5.Display.setCursor(8, h - 24);
  M5.Display.setTextColor(fgColor, BLACK);
  M5.Display.print(text);
}

void Display::clearOverlay() {
  int w = M5.Display.width();
  int h = M5.Display.height();
  M5.Display.fillRect(0, h - 28, w, 28, BLACK);
}

}  // namespace stkchan
