// Stack-chan v1 — entry point.
// See docs/superpowers/specs/2026-06-02-stackchan-design.md
#include <Arduino.h>
#include <M5CoreS3.h>

#include "config.h"
#include "services/NvsStore.h"
#include "services/OtaService.h"
#include "services/CaptivePortal.h"
#include "net/WifiManager.h"
#include "net/ConnectivityTier.h"
#include "net/SttClient.h"     // not strictly needed in main but pulls externs
#include "net/ChatClient.h"
#include "net/TtsClient.h"
#include "hal/AudioPlayer.h"
#include "hal/MicRecorder.h"
#include "hal/Servos.h"
#include "hal/Display.h"
#include "face/Face.h"
#include "face/LvglDisplay.h"
#include "motion/MotionDirector.h"
#include "state_machine.h"

using namespace stkchan;

static bool g_pressedLast = false;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Stack-chan v1 boot ===");

  if (!nvs.begin()) {
    Serial.println("WARN: NVS open failed");
  }

  display.begin();
  face.begin();
  if (!audio.begin()) {
    Serial.println("WARN: AudioPlayer init failed");
  }

  if (!servos.begin()) {
    Serial.println("WARN: servo init failed");
  }
  motion.begin();

  if (!mic.begin()) {
    Serial.println("WARN: mic alloc failed");
  }

  // Provisioning gate: no SSID1 in NVS → captive portal forever, no FSM.
  bool needsProvisioning = nvs.getString(kNvsSsid1, "").isEmpty();
  if (needsProvisioning) {
    Serial.println("No WiFi creds — entering captive portal");
    portal.begin();
    while (true) {
      portal.tick();
      lvglDisplay.tick();   // keep the face alive during provisioning
      if (portal.exitRequested()) {
        Serial.println("[PORTAL] exit requested — rebooting");
        portal.clearExitFlag();
        portal.end();
        delay(200);
        ESP.restart();
      }
      delay(10);
    }
  }

  wifi.begin();
  connectivity.begin();
  initStateMachine();
}

void loop() {
  static bool ota_begun = false;
  uint32_t now = millis();

  M5.update();
  lvglDisplay.tick();
  wifi.tick();

  // T7: OTA — initialize once WiFi connects (ota.tick() is a no-op until begin())
  if (!ota_begun && wifi.isConnected()) {
    ota.begin();
    ota_begun = true;
  }
  ota.tick();

  connectivity.tick(now);
  motion.tick(now);

  // Touch → FSM events
  bool pressed = (M5.Touch.getCount() > 0);
  if (pressed && !g_pressedLast) onPressDown();
  if (!pressed && g_pressedLast) onPressUp();
  g_pressedLast = pressed;

  tickStateMachine(now);
  delay(5);
}
