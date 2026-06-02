#include "WifiManager.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <time.h>

#include "../config.h"
#include "../services/NvsStore.h"

namespace stkchan {

// ── NTP sync ──────────────────────────────────────────────────────────────
// configTzTime sets TZ atomically with the SNTP config. Using
// setenv()+tzset()+configTime() separately races: configTime can reset TZ
// to UTC internally on some Arduino-ESP32 versions, so getLocalTime()
// returns UTC despite the setenv. Idempotent: safe to call on every
// reconnect.
static void kickNtpSync() {
    configTzTime(stkchan::kTimezoneDefault, stkchan::kNtpServer);
    Serial.printf("[WIFI] NTP sync requested (server=%s tz=%s)\n",
                  stkchan::kNtpServer,
                  stkchan::kTimezoneDefault);
}

// ── Slot-priority connect loop (Jarvis PR #20) ────────────────────────────
// Reads up to 3 SSID/PSK slots from NVS (ssid1/psk1 … ssid3/psk3).
// Each slot gets kPerSlotTimeoutMs before we move on; the total is also
// capped by connectTimeoutMs so a dead list can't burn the whole boot budget.
//
// Replaces WiFiMulti.run() RSSI-based selection: the captive portal (T6)
// labels slot 1 as highest priority, and that label now does what it says.
static bool connectInSlotOrder(uint32_t connectTimeoutMs) {
    // Build slot list from NVS.
    struct Slot { String ssid; String psk; };
    const Slot kSlots[3] = {
        { stkchan::nvs.getString(stkchan::kNvsSsid1),
          stkchan::nvs.getString(stkchan::kNvsPsk1)  },
        { stkchan::nvs.getString(stkchan::kNvsSsid2),
          stkchan::nvs.getString(stkchan::kNvsPsk2)  },
        { stkchan::nvs.getString(stkchan::kNvsSsid3),
          stkchan::nvs.getString(stkchan::kNvsPsk3)  },
    };

    size_t count = 0;
    for (const auto& s : kSlots) {
        if (!s.ssid.isEmpty()) ++count;
    }
    if (count == 0) {
        Serial.println("[WIFI] No saved credentials in NVS.");
        return false;
    }

    WiFi.mode(WIFI_STA);
    const uint32_t t_start = millis();

    for (size_t i = 0; i < 3; ++i) {
        const auto& slot = kSlots[i];
        if (slot.ssid.isEmpty()) continue;

        const uint32_t elapsed = millis() - t_start;
        if (elapsed >= connectTimeoutMs) {
            Serial.printf("[WIFI] total budget %lums exhausted before slot %u\n",
                          (unsigned long)connectTimeoutMs, (unsigned)i);
            return false;
        }
        const uint32_t remaining   = connectTimeoutMs - elapsed;
        const uint32_t slot_budget =
            (stkchan::kPerSlotTimeoutMs < remaining)
                ? stkchan::kPerSlotTimeoutMs
                : remaining;

        Serial.printf("[WIFI] slot %u \"%s\" (budget=%lums)",
                      (unsigned)i + 1,
                      slot.ssid.c_str(),
                      (unsigned long)slot_budget);

        // Drop any partial association from a previous slot before a clean
        // attempt. eraseap=true clears the cached SSID so the driver doesn't
        // resume the previous attempt.
        WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/true);
        delay(100);

        WiFi.begin(slot.ssid.c_str(), slot.psk.c_str());

        const uint32_t t_slot = millis();
        while (WiFi.status() != WL_CONNECTED &&
               millis() - t_slot < slot_budget) {
            delay(200);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WIFI] OK ssid=\"%s\" ip=%s rssi=%d ch=%d slot=%u mac=%s\n",
                          WiFi.SSID().c_str(),
                          WiFi.localIP().toString().c_str(),
                          WiFi.RSSI(),
                          WiFi.channel(),
                          (unsigned)i + 1,
                          WiFi.macAddress().c_str());
            kickNtpSync();

            // Register mDNS hostname so OtaService (T7) and other hosts can
            // reach the device as stackchan.local on the LAN.
            if (MDNS.begin("stackchan")) {
                Serial.println("[WIFI] mDNS: stackchan.local");
            }

            return true;
        }

        Serial.printf("[WIFI] slot %u \"%s\" failed (status=%d), trying next\n",
                      (unsigned)i + 1, slot.ssid.c_str(), (int)WiFi.status());
    }

    Serial.printf("[WIFI] all slots exhausted\n");
    return false;
}

// ── WifiManager public API ────────────────────────────────────────────────

bool WifiManager::begin(uint32_t connectTimeoutMs) {
    Serial.println("[WIFI] begin()");
    // Log which slots are populated so boot logs make provisioning state obvious.
    const char* const kSsidKeys[3] = {
        stkchan::kNvsSsid1, stkchan::kNvsSsid2, stkchan::kNvsSsid3
    };
    for (size_t i = 0; i < 3; ++i) {
        String s = stkchan::nvs.getString(kSsidKeys[i]);
        if (!s.isEmpty()) {
            Serial.printf("[WIFI] saved[%u] \"%s\"\n", (unsigned)i + 1, s.c_str());
        }
    }

    bool ok = connectInSlotOrder(connectTimeoutMs);
    if (!ok) {
        Serial.println("[WIFI] All slots failed — staying offline.");
    }
    return ok;
}

void WifiManager::tick() {
    // Lightweight; the ESP32 driver handles auto-reconnect internally.
    // This hook is here so T5 (ConnectivityProbe) and future reconnect
    // backoff logic can be added without changing main.cpp's call sites.
    static bool s_was_connected = false;
    bool now = (WiFi.status() == WL_CONNECTED);
    if (s_was_connected && !now) {
        Serial.println("[WIFI] Connection lost.");
    } else if (!s_was_connected && now) {
        Serial.printf("[WIFI] Reconnected. ip=%s\n",
                      WiFi.localIP().toString().c_str());
        kickNtpSync();
    }
    s_was_connected = now;
}

bool   WifiManager::isConnected() { return WiFi.status() == WL_CONNECTED; }
String WifiManager::getIP()       { return WiFi.localIP().toString(); }
int    WifiManager::getRSSI()     { return WiFi.RSSI(); }

// Global singleton.
WifiManager wifi;

}  // namespace stkchan
