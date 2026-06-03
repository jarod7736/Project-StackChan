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
#include "face/MenuScreen.h"
#include "motion/MotionDirector.h"
#include "state_machine.h"

using namespace stkchan;

static bool    g_pressedLast = false;
static int16_t g_pressStartY = 0;
static bool    g_swipeFired  = false;   // latches per-press so we only fire once

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
  menu.begin();
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

  // Touch → FSM events + swipe-up gesture for menu reveal.
  // When the menu screen is active, LVGL widget handlers own the touch
  // and we skip FSM voice + swipe detection entirely.
  bool pressed = (M5.Touch.getCount() > 0);
  if (menu.isActive()) {
    // LVGL handles the slider / back button. Just reset our local state
    // so the next return-to-face has a clean slate.
    g_pressedLast = pressed;
    g_swipeFired  = false;
    tickStateMachine(now);
    delay(5);
    return;
  }

  // While the floating "Menu" button is showing, let LVGL own touch —
  // tapping anywhere is either "open menu" (button) or "dismiss menu
  // button" (anywhere else). Don't start the mic during that window.
  if (face.isMenuButtonVisible()) {
    g_pressedLast = pressed;
    g_swipeFired  = false;
    tickStateMachine(now);
    delay(5);
    return;
  }

  if (pressed) {
    auto t = M5.Touch.getDetail(0);
    if (!g_pressedLast) {
      // Touch-down: remember start position. Fire the FSM press event so
      // press-to-talk still works.
      g_pressStartY = t.y;
      g_swipeFired  = false;
      onPressDown();
    } else if (!g_swipeFired) {
      // Mid-press: check for swipe-up from the bottom edge.
      int dy = g_pressStartY - t.y;
      if (g_pressStartY >= 180 && dy >= 60) {
        // It's a swipe — abort the press-to-talk and reveal the menu button.
        g_swipeFired = true;
        onPressCancel();
        face.revealMenuButton();
      }
    }
  } else if (g_pressedLast) {
    // Touch-up: fire normal release ONLY if this wasn't a swipe.
    if (!g_swipeFired) onPressUp();
  }
  g_pressedLast = pressed;

  tickStateMachine(now);
  delay(5);
}
