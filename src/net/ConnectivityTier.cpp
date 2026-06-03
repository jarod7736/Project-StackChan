#include "net/ConnectivityTier.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"
#include "services/NvsStore.h"

namespace stkchan {

ConnectivityTier connectivity;

void ConnectivityTier::begin() {
  tier_ = Tier::NO_WIFI;
  lastProbeMs_ = 0;
}

void ConnectivityTier::tick(uint32_t nowMs) {
  if (nowMs - lastProbeMs_ < kTierProbeIntervalMs && lastProbeMs_ != 0) return;
  lastProbeMs_ = nowMs;

  if (WiFi.status() != WL_CONNECTED) {
    tier_ = Tier::NO_WIFI;
    return;
  }
  tier_ = probeBackend_() ? Tier::LAN_OK : Tier::LAN_NO_BACKEND;
}

bool ConnectivityTier::probeBackend_() {
  String host = nvs.getString(kNvsChatHost, "");
  if (host.isEmpty()) return false;

  HTTPClient http;
  // Ollama exposes /api/tags for a cheap reachability probe.
  String url = host + "/api/tags";
  http.setConnectTimeout(1500);
  http.setTimeout(2000);
  if (!http.begin(url)) { http.end(); return false; }
  int code = http.GET();
  http.end();
  return code > 0 && code < 500;
}

}  // namespace stkchan
