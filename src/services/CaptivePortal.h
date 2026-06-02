#pragma once

// CaptivePortal — bring the Stack-chan-Setup AP up, host the web UI from
// LittleFS, and serve the schema-driven config + WiFi APIs at
// http://192.168.4.1/.
//
// Lifecycle:
//   begin()   — switch WiFi to AP mode (open SSID "Stackchan-XXXX"), bind
//               port 80, start DNS catch-all on port 53. Idempotent.
//   end()     — tear down server + DNS, switch WiFi back to STA. Idempotent.
//   tick()    — must be called every loop() iteration while running().
//               Drains the DNS catch-all.
//   running() — true between begin() and end().
//
// Captive-portal trick: every 404 redirects to http://192.168.4.1/ — iOS,
// Android, and Windows all hit OS-specific probe URLs to detect captive
// networks and pop up the system "sign in" UI when those probes redirect.

#include <Arduino.h>

namespace stkchan {

class CaptivePortal {
public:
    static void begin();
    static void end();
    static void tick();
    static bool running();

    // Set when the web UI POSTs /api/exit. Caller polls this and calls end()
    // when it sees the flag, then clears it.
    static bool exitRequested();
    static void clearExitFlag();
};

extern CaptivePortal portal;

}  // namespace stkchan
