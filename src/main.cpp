// Stack-chan v1 — entry point.
// See docs/superpowers/specs/2026-06-02-stackchan-design.md
#include <Arduino.h>
#include <M5CoreS3.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Stack-chan v1 boot ===");
  Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());
  Serial.printf("Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  M5.update();
  delay(10);
}
