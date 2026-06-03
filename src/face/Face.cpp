#include "face/Face.h"
#include "face/ExpressionMap.h"
#include "face/LvglDisplay.h"

// v1.5: bring up LVGL (single-threaded, driven from main loop()) and use
// it for the face. m5avatar's drawLoop FreeRTOS task tripped a SPI mutex
// assert against the main task; LVGL's lv_timer_handler runs on whichever
// task calls it, so there's only ever one SPI consumer.
//
// Step 1 (this commit) just brings LVGL up with a placeholder rectangle so
// we can verify the flush driver before adding face widgets.

namespace stkchan {

Face face;

void Face::begin() {
  currentTag_ = "neutral";
  if (!lvglDisplay.begin()) {
    Serial.println("[Face] LVGL bring-up failed; face is no-op");
  }
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
