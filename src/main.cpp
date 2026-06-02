// Stack-chan v1 — entry point.
// See docs/superpowers/specs/2026-06-02-stackchan-design.md
#include <Arduino.h>
#include <M5CoreS3.h>
#include "config.h"
#include "services/NvsStore.h"
#include "services/CaptivePortal.h"
#include "net/WifiManager.h"
#include "net/ConnectivityTier.h"

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Stack-chan v1 boot ===");

  // T3: NVS smoke-test
  if (!stkchan::nvs.begin()) {
    Serial.println("WARN: NVS open failed");
  } else {
    Serial.printf("NVS chat_host = '%s'\n",
                  stkchan::nvs.getString(stkchan::kNvsChatHost, "(unset)").c_str());
  }
  Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());
  Serial.printf("Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());

  // T6: First-run captive portal — if no WiFi creds are saved, bring up AP
  // provisioning at 192.168.4.1 so a phone can configure the device.
  if (stkchan::nvs.getString(stkchan::kNvsSsid1, "").isEmpty()) {
    Serial.println("No WiFi creds — entering captive portal");
    stkchan::portal.begin();
    // portal.tick() runs in loop(); the T4/T5 ticks below are harmless
    // during the portal lifetime (wifi.begin() will find no creds and
    // stand idle, connectivity will stay OFFLINE).
  }

  // T4: WiFi slot-priority connect + NTP kick
  stkchan::wifi.begin();
  // T5: Connectivity tier probe
  stkchan::connectivity.begin();
}

void loop() {
  M5.update();
  stkchan::portal.tick();  // T6: drain DNS catch-all (no-op when not running)

  // T6: honor the web UI's "Exit Config Mode" button — tear down the AP
  // and restart cleanly so the next boot reads the freshly-saved creds.
  if (stkchan::portal.exitRequested()) {
    Serial.println("[PORTAL] exit requested — rebooting");
    stkchan::portal.clearExitFlag();
    stkchan::portal.end();
    delay(200);
    ESP.restart();
  }

  stkchan::wifi.tick();
  stkchan::connectivity.tick(millis());
  delay(10);
}
