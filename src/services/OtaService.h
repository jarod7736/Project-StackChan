#pragma once

// OtaService — ArduinoOTA wrapper.
//
// LAN-side OTA: ArduinoOTA listens for IDE/PlatformIO uploads at port 3232,
// authenticated with the password stored in NVS as `ota_pass`.
// If `ota_pass` is empty, the service logs a warning but does not refuse to init —
// that would prevent OTA recovery (CLAUDE.md architectural invariant).

#include <Arduino.h>

namespace stkchan {

class OtaService {
public:
    static bool begin();     // wire ArduinoOTA; returns true if enabled
    static void tick();      // pump ArduinoOTA.handle()
    static bool isActive();  // true mid-LAN-flash
};

extern OtaService ota;

}  // namespace stkchan
