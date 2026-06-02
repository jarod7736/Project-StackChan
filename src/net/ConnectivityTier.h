#pragma once
#include <stdint.h>

namespace stkchan {

enum class Tier {
  LAN_OK,           // WiFi up and chat host responding
  LAN_NO_BACKEND,   // WiFi up, chat host unreachable
  NO_WIFI,
};

class ConnectivityTier {
 public:
  void begin();
  void tick(uint32_t nowMs);
  Tier current() const { return tier_; }
  uint32_t lastProbeAgeMs(uint32_t nowMs) const { return nowMs - lastProbeMs_; }

 private:
  Tier     tier_         = Tier::NO_WIFI;
  uint32_t lastProbeMs_  = 0;
  bool probeBackend_();
};

extern ConnectivityTier connectivity;

}  // namespace stkchan
