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
  // Backend-agnostic reachability: GET the host root. Any HTTP status (200,
  // 404, 405 ...) means TCP+HTTP is alive — works for both Ollama (/api/tags
  // is Ollama-only) and any OpenAI-compatible server. We only gate the casual
  // chat host here; the brain host is checked implicitly when a brain query runs.
  while (host.endsWith("/")) host.remove(host.length() - 1);

  HTTPClient http;
  http.setConnectTimeout(1500);
  http.setTimeout(2000);
  http.useHTTP10(true);
  if (!http.begin(host)) { http.end(); return false; }
  int code = http.GET();
  http.end();
  return code > 0;   // got any HTTP response → reachable
}

}  // namespace stkchan
