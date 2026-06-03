#include "face/Face.h"
#include "face/ExpressionMap.h"

// v1 NOTE: the M5Stack-Avatar lib spawns its own drawLoop FreeRTOS task
// when init() runs, and that task asserts on a SPI mutex (Bus_SPI::
// endTransaction → xQueueGenericSend) when anything on the main task
// touches Avatar or LCD state concurrently. Stubbed out for the v1 smoke
// test so the voice path is reachable. Proper fix is either:
//   (a) don't auto-start drawLoop — drive g_avatar.draw() manually from
//       our main loop() so there's a single SPI consumer;
//   (b) wrap every g_avatar.setExpression / setMouthOpenRatio call in
//       g_avatar.suspend() / .resume().
// Either way, re-enabling face will need ExpressionMap (T15) unchanged.

namespace stkchan {

Face face;

void Face::begin() {
  // No-op for v1; see file comment above.
  currentTag_ = "neutral";
}

void Face::setExpression(const std::string& tag) {
  currentTag_ = tag;
  (void)expressionFor(tag);  // silence unused-include warning
}

void Face::setMouthOpen(float ratio) {
  (void)ratio;
}

void Face::setAutoBlinkEnabled(bool enabled) {
  (void)enabled;
}

}  // namespace stkchan
