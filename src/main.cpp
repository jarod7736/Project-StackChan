// Stack-chan v1 — entry point.
// See docs/superpowers/specs/2026-06-02-stackchan-design.md
#include <Arduino.h>
#include <M5CoreS3.h>

#include <ArduinoJson.h>

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
#include "app/ControlBridge.h"

using namespace stkchan;

static bool    g_pressedLast = false;
static int16_t g_pressStartY = 0;
static bool    g_swipeFired  = false;   // latches per-press so we only fire once

// ── USB-serial WiFi provisioning ───────────────────────────────────────────
// No captive-portal hotspot. On first boot with no saved WiFi creds, the
// device waits for a single line of JSON on USB serial:
//   {"ssid":"MyNet","psk":"secret"}            (slot 1)
//   {"ssid1":"A","psk1":"a","ssid2":"B","psk2":"b"}   (multi-slot also OK)
// Sent by tools/provision-serial.py (or just typed into a serial monitor).
// On a valid line we persist to NVS and reboot into normal operation.
static bool tryConsumeProvisioningLine(const String& line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return false;

  // Accept either {"ssid","psk"} or explicit slot keys.
  String s1 = doc["ssid1"] | doc["ssid"] | "";
  String p1 = doc["psk1"]  | doc["psk"]  | doc["password"] | "";
  if (s1.isEmpty()) return false;

  nvs.putString(kNvsSsid1, s1);
  nvs.putString(kNvsPsk1,  p1);
  String s2 = doc["ssid2"] | "";
  if (!s2.isEmpty()) { nvs.putString(kNvsSsid2, s2); nvs.putString(kNvsPsk2, doc["psk2"] | ""); }
  String s3 = doc["ssid3"] | "";
  if (!s3.isEmpty()) { nvs.putString(kNvsSsid3, s3); nvs.putString(kNvsPsk3, doc["psk3"] | ""); }

  // Optional: accept other config keys in the same blob so a one-shot
  // provision can set the OpenAI key etc. without the web UI.
  const char* passKeys[][2] = {
    {"chat_host", kNvsChatHost}, {"oai_key", kNvsOaiKey}, {"ota_pass", kNvsOtaPass},
    {"tts_voice", kNvsTtsVoice}, {"tts_provider", kNvsTtsProv},
  };
  for (auto& kv : passKeys) {
    const char* v = doc[kv[0]] | (const char*)nullptr;
    if (v && *v) nvs.putString(kv[1], String(v));
  }

  Serial.printf("[PROV] saved ssid=\"%s\" — rebooting\n", s1.c_str());
  return true;
}

// Non-blocking serial line accumulator. Call frequently. On a complete
// line that parses as a provisioning blob, persists + reboots. Safe to
// call anytime — non-JSON / non-provisioning lines are ignored. This is
// the always-available escape hatch: you can (re)send WiFi creds over USB
// even after a wrong password was saved, since WiFi-not-connected means
// the web UI is unreachable.
static void pollSerialProvisioning() {
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line.length() && tryConsumeProvisioningLine(line)) {
        delay(200);
        ESP.restart();
      }
      line = "";
    } else if (line.length() < 1024) {
      line += c;
    }
  }
}

// Block at boot until provisioned over serial. Keeps the LVGL face alive
// and shows a setup prompt on screen.
static void runSerialProvisioning() {
  Serial.println("[PROV] No WiFi creds. Send JSON over serial, e.g.:");
  Serial.println("[PROV]   {\"ssid\":\"YourNet\",\"psk\":\"password\"}");
  Serial.println("[PROV]   (use tools/provision-serial.py, or type + Enter)");
  display.showStatusOverlay("setup: send WiFi over USB", 0xFFE0);

  for (;;) {
    lvglDisplay.tick();
    pollSerialProvisioning();  // reboots on success
    delay(10);
  }
}

// ── AXP2101 power-fault forensics (read-only) ──────────────────────────────
// The crash signature — "screen black, stays OFF, no serial, no reboot" — is
// the AXP2101 PMIC latching a rail OFF. A CPU panic / watchdog / brownout
// DETECTOR would reboot and print (reset reason 4/5/6/9); we see neither, so
// esp_reset_reason() is blind to it (a manual power-on just reads POWERON=1).
// The AXP's own registers live in its always-on domain and PERSIST across the
// off period as long as VBUS or BAT stays connected — so reading them on the
// next boot reveals WHY it cut power. 0x00/0x01 = live PMU status (input
// current-limit / thermal-throttle / charge state); 0x48-0x4A = latched IRQ
// flags (over/under-temp, low-V warnings, key press) recording what fired.
// Pure I2C reads on the internal bus (AXP @0x34) — nothing here alters power.
// NOTE: to keep these flags valid after a crash, recover with the POWER BUTTON
// (leave USB/battery connected) — yanking all power resets the AXP domain.
static void dumpAxpFault(const char* when) {
  constexpr uint8_t kAxp = 0x34;
  uint8_t s1 = M5.In_I2C.readRegister8(kAxp, 0x00, 400000);
  uint8_t s2 = M5.In_I2C.readRegister8(kAxp, 0x01, 400000);
  uint8_t i0 = M5.In_I2C.readRegister8(kAxp, 0x48, 400000);
  uint8_t i1 = M5.In_I2C.readRegister8(kAxp, 0x49, 400000);
  uint8_t i2 = M5.In_I2C.readRegister8(kAxp, 0x4A, 400000);
  Serial.printf("[AXP %s] status1=0x%02X status2=0x%02X irq=0x%02X/0x%02X/0x%02X\n",
                when, s1, s2, i0, i1, i2);
}

void setup() {
  // CoreS3 power: M5 factory defaults (output_power defaults true) — matching the
  // STOCK demo firmware, which runs stably on this hardware (DinBase + Core are
  // fine). The crashes are in our firmware (or the servo-test wiring), not the
  // board; this clean config is the apples-to-apples baseline vs stock.
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Stack-chan v1 boot ===");
  // Why did we last reset? Prints on the NEXT boot (survives the USB-CDC drop),
  // so a crash that reboots reveals its cause. 1=POWERON 2=EXT 3=SW 4=PANIC
  // 5=INT_WDT 6=TASK_WDT 7=WDT 8=DEEPSLEEP 9=BROWNOUT.
  Serial.printf("[BOOT] reset reason: %d\n", (int)esp_reset_reason());
  // PMIC forensics: shows what the AXP latched before the LAST power-off. If
  // the previous death was a PMIC cut, the IRQ/status bits here say why.
  dumpAxpFault("boot");

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

  // Provisioning gate (v2): no SSID1 in NVS → wait for USB-serial creds.
  // No hotspot. tools/provision-serial.py (or a serial monitor) sends a
  // JSON line; we persist + reboot.
  if (nvs.getString(kNvsSsid1, "").isEmpty()) {
    runSerialProvisioning();  // never returns — reboots on success
  }

  controlBridge.begin();   // queue for web → main-loop control commands

  wifi.begin();
  connectivity.begin();

  // v2: bring up the always-on LAN control server once WiFi is connected.
  // Reachable at http://stackchan.local/ from any browser on the network.
  if (wifi.isConnected()) {
    portal.beginLan();
  }

  initStateMachine();
}

void loop() {
  static bool ota_begun = false;
  uint32_t now = millis();

  M5.update();
  lvglDisplay.tick();
  face.tick(now);
  wifi.tick();

  // Discreet face status (battery + WiFi), refreshed ~every 2 s. Battery via
  // the AXP2101 (M5.Power); WiFi from the live link state.
  static uint32_t s_lastStatusMs  = 0;
  static bool     s_lowBattWarned = false;
  static int      s_lowStreak     = 0;
  if (now - s_lastStatusMs >= 2000) {
    s_lastStatusMs = now;
    // Voltage-based: the AXP2101 fuel-gauge % is unreliable on CoreS3, so we
    // use battery voltage (direct ADC) and treat high VBUS as "on USB power".
    int  vbat = M5.Power.getBatteryVoltage();      // mV
    int  vbus = M5.Power.getVBUSVoltage();         // mV
    int  ibat = M5.Power.getBatteryCurrent();      // mA, signed (<0 = discharging)
    bool ext  = (vbus >= kVbusPresentMv) || ((int)M5.Power.isCharging() == 1);
    int  pct  = batteryPctFromMv(vbat);
    face.setStatus(pct, ext, wifi.isConnected());
    // heap/psram included to catch a per-voice-turn memory leak (the crash
    // happens after a couple interactions). If free heap / min-free-heap trends
    // DOWN each turn, that's a leak; getMinFreeHeap is the all-time low-water.
    // maxblk = largest contiguous free block. If it shrinks toward ~40 KB while
    // heap stays high, that's TLS FRAGMENTATION (each HTTPS handshake — STT+TTS,
    // 2/turn — grabs ~40 KB then frees it fragmented) → next handshake fails.
    Serial.printf("[PWR] vbat=%dmV vbus=%dmV ibat=%dmA chg=%d pct=%d ext=%d | heap=%u min=%u maxblk=%u psram=%u\n",
                  vbat, vbus, ibat, (int)M5.Power.isCharging(), pct, (int)ext,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getFreePsram());

    // One-shot spoken low-battery cue: gated on external power, debounced
    // (~10 s sustained) + 30 s boot grace to avoid AXP settling false alarms.
    bool lowNow = (!ext && vbat > 0 && vbat <= kLowBattMv);
    s_lowStreak = lowNow ? s_lowStreak + 1 : 0;
    if (ext || vbat <= 0 || vbat >= kLowBattClearMv) {
      s_lowBattWarned = false;                     // re-arm once recovered/on USB
    }
    if (now >= 30000 && !s_lowBattWarned && s_lowStreak >= 5) {
      // requestExternalSpeak only fires from IDLE; retries next tick if busy.
      if (requestExternalSpeak(String(kLowBattMsg), "sad")) s_lowBattWarned = true;
    }
  }

  // Always-available USB escape hatch: while WiFi is down (e.g. a wrong
  // password was saved and the web UI is unreachable), accept a fresh
  // provisioning line over serial. Reboots on a valid blob.
  if (!wifi.isConnected()) {
    pollSerialProvisioning();
  }

  // T7: OTA — initialize once WiFi connects (ota.tick() is a no-op until begin())
  if (!ota_begun && wifi.isConnected()) {
    ota.begin();
    ota_begun = true;
  }
  ota.tick();

  // v2: start the LAN control server the first time WiFi comes up (covers
  // the case where WiFi wasn't connected yet at the end of setup()).
  static bool lan_begun = false;
  if (!lan_begun && wifi.isConnected()) {
    portal.beginLan();   // idempotent (no-op if already running)
    lan_begun = true;
  }

  connectivity.tick(now);
  motion.tick(now);
  controlBridge.tick();   // apply queued web-control commands on this task

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
      Serial.printf("[Touch] down @ (%d,%d)\n", (int)t.x, (int)t.y);
      onPressDown();
    } else if (!g_swipeFired) {
      // Mid-press: check for swipe-up from the bottom edge. Loosened
      // thresholds — allow swipes starting from the bottom half of the
      // screen with only 40 px of upward motion.
      int dy = g_pressStartY - t.y;
      if (g_pressStartY >= 120 && dy >= 40) {
        Serial.printf("[Touch] swipe-up detected (startY=%d, dy=%d)\n",
                      g_pressStartY, dy);
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
