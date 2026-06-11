#include "OtaService.h"

#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

#include "../config.h"
#include "../services/NvsStore.h"
#include "../vision/PresenceSensor.h"

namespace stkchan {

namespace {

bool g_began  = false;
bool g_active = false;

// Map ArduinoOTA's error enum to a short tag for the serial log. The lib
// only hands us the int — translate so log greps actually find something.
const char* otaErrorName(ota_error_t err) {
    switch (err) {
        case OTA_AUTH_ERROR:    return "AUTH";
        case OTA_BEGIN_ERROR:   return "BEGIN";
        case OTA_CONNECT_ERROR: return "CONNECT";
        case OTA_RECEIVE_ERROR: return "RECEIVE";
        case OTA_END_ERROR:     return "END";
        default:                return "UNKNOWN";
    }
}

// Throttle progress logging. ArduinoOTA fires onProgress every few KB;
// printing every callback floods the serial console and slows the flash.
// Print at most every 5%.
int g_last_pct_logged = -1;
void resetProgressLog() { g_last_pct_logged = -1; }

void logProgressIfStep(unsigned int cur, unsigned int total) {
    if (total == 0) return;
    int pct = (int)((uint64_t)cur * 100 / total);
    if (pct < g_last_pct_logged + 5 && pct < 100) return;
    g_last_pct_logged = pct;
    Serial.printf("[OTA] %d%% (%u / %u)\n", pct, cur, total);
}

}  // namespace

bool OtaService::begin() {
    String pass = stkchan::nvs.getString(stkchan::kNvsOtaPass, "");
    if (pass.length() == 0) {
        Serial.println("[OTA] warning: no ota_pass in NVS (LAN OTA disabled)");
        // Don't return false — we still initialize ArduinoOTA to maintain
        // the architectural invariant that OTA survives every build.
    }

    // mDNS hostname for the device — matches WifiManager's "stackchan"
    ArduinoOTA.setHostname("stackchan");
    ArduinoOTA.setPort(3232);  // standard ArduinoOTA port

    if (pass.length() > 0) {
        ArduinoOTA.setPassword(pass.c_str());
    }

    ArduinoOTA.onStart([]() {
        const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "flash" : "fs";
        Serial.printf("[OTA] start type=%s\n", type);
        g_active = true;
        // Must happen HERE, not in the main loop: ArduinoOTA.handle() blocks
        // for the whole upload, so loop() never runs again to pause scanning.
        // esp-dl inference reads model weights from flash and stalls against
        // the OTA writer's cache-disabled erase/write bursts -> failed upload.
        presence.setScanEnabled(false);
        delay(100);  // let an in-flight capture/inference drain
        resetProgressLog();
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA] end (rebooting)");
        g_active = false;
    });

    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("[OTA] error=%s (%u)\n", otaErrorName(err), (unsigned)err);
        g_active = false;
    });

    ArduinoOTA.onProgress([](unsigned int cur, unsigned int total) {
        // Feed the loop watchdog — ArduinoOTA's internal recv loop
        // doesn't return to loop() during a flash, so loop()'s
        // watchdog reset can't fire. Without this the watchdog
        // panics the device mid-flash.
        esp_task_wdt_reset();
        logProgressIfStep(cur, total);
    });

    ArduinoOTA.begin();
    g_began = true;
    Serial.println("[OTA] ready hostname=stackchan.local port=3232");
    return true;
}

void OtaService::tick() {
    if (!g_began) return;
    ArduinoOTA.handle();
}

bool OtaService::isActive() {
    return g_active;
}

}  // namespace stkchan

// Global singleton
stkchan::OtaService ota;
