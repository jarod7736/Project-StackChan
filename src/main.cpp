// Stack-chan v1 — entry point.
// See docs/superpowers/specs/2026-06-02-stackchan-design.md
#include <Arduino.h>
#include <M5CoreS3.h>
#include "config.h"
#include "services/NvsStore.h"
#include "net/WifiManager.h"
#include "net/ConnectivityTier.h"

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Stack-chan v1 boot ===");
  if (!stkchan::nvs.begin()) {
    Serial.println("WARN: NVS open failed");
  } else {
    Serial.printf("NVS chat_host = '%s'\n",
                  stkchan::nvs.getString(stkchan::kNvsChatHost, "(unset)").c_str());
  }
  Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());
  Serial.printf("Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());

  stkchan::wifi.begin();  // non-blocking slot-priority connect + NTP kick
  stkchan::connectivity.begin();
}

void loop() {
  M5.update();
  stkchan::wifi.tick();
  stkchan::connectivity.tick(millis());
  delay(10);
}
