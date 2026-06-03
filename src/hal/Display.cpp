#include "hal/Display.h"
#include <M5CoreS3.h>

namespace stkchan {

Display display;

void Display::begin() {
  // No-op for v1. See showStatusOverlay() below.
}

// v1: no-op. The M5Stack-Avatar library spawns its own drawLoop FreeRTOS
// task that owns the SPI bus to the LCD; calling M5.Display.* from the
// main task here races that task and trips the xQueueGenericSend mutex
// assert (Bus_SPI::endTransaction from drawLoop while we hold the bus).
// Proper fix is either avatar.suspend()/.resume() around writes or routing
// status text through Avatar::setSpeechText. Until that lands, the face
// expression conveys state and the Serial log carries the diagnostics.
void Display::showStatusOverlay(const String& text, uint16_t fgColor) {
  (void)text; (void)fgColor;
}

void Display::clearOverlay() {
  // No-op for v1; see showStatusOverlay().
}

}  // namespace stkchan
