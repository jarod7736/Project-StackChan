#pragma once

// WifiManager — slot-priority WiFi bring-up.
//
// Saved networks are tried in slot order (ssid1/psk1 = highest priority).
// Each slot gets a per-slot connect budget of kPerSlotTimeoutMs; on
// failure we move to the next slot. This is the Jarvis PR #20 pattern:
// true slot-order priority rather than strongest-RSSI selection.
//
// Credentials are read from NVS keys ssid1/psk1, ssid2/psk2, ssid3/psk3
// (written by the CaptivePortal in T6). On success, NTP sync is kicked
// off via configTzTime so the TZ survives across SNTP reinits.
//
// ConnectivityTier lives in T5 (ConnectivityProbe). This class only
// exposes isConnected() for T5 to branch on.

#include <Arduino.h>

namespace stkchan {

class WifiManager {
public:
    // Iterate saved slots in priority order and connect; kick NTP sync on
    // success. Blocks until either a slot connects or connectTimeoutMs is
    // hit — never hangs forever, but is NOT async. Display "Connecting…"
    // before calling. Returns true if connected.
    bool begin(uint32_t connectTimeoutMs = 20000);

    // Called every loop() iteration to handle auto-reconnect logging.
    // Currently lightweight — just returns quickly if connected. Future:
    // reconnect backoff, tier cache invalidation on drop.
    void tick();

    bool   isConnected() const;
    String getIP()       const;
    int    getRSSI()     const;
};

extern WifiManager wifi;  // global singleton

}  // namespace stkchan
