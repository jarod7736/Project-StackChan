#include "vision/PresenceLogic.h"
#include <cmath>

namespace stkchan {

void PresenceDebounce::update(bool detected, uint32_t nowMs) {
  if (detected) {
    lastSeenMs_ = nowMs;
    if (!present_) {
      if (++hits_ >= p_.arriveHits) {
        present_ = true;
        arrived_ = true;
        hits_    = 0;
      }
    }
  } else {
    hits_ = 0;  // a miss breaks the arrival streak
    if (present_ && (nowMs - lastSeenMs_) >= p_.absentMs) {
      present_ = false;
      left_    = true;
    }
  }
}

bool PresenceDebounce::consumeArrived() {
  bool e = arrived_;
  arrived_ = false;
  return e;
}

bool PresenceDebounce::consumeLeft() {
  bool e = left_;
  left_ = false;
  return e;
}

static int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

TrackTarget trackTarget(int cx, int cy, int frameW, int frameH,
                        int curYaw, int curPitch, const TrackParams& tp) {
  TrackTarget t{curYaw, curPitch, false};
  if (frameW <= 0 || frameH <= 0) return t;

  const float ex = (cx - frameW / 2.0f) / (frameW / 2.0f);  // + = right of center
  const float ey = (cy - frameH / 2.0f) / (frameH / 2.0f);  // + = below center

  if (std::fabs(ex) >= tp.deadband) {
    int desired = curYaw - (int)lroundf(tp.yawGain * ex * tp.yawFull);
    int delta   = clampi(desired - curYaw, -tp.yawSlew, tp.yawSlew);
    int yaw     = clampi(curYaw + delta, tp.yawMin, tp.yawMax);
    if (yaw != curYaw) { t.yaw = yaw; t.move = true; }
  }
  if (std::fabs(ey) >= tp.deadband) {
    int desired = curPitch - (int)lroundf(tp.pitchGain * ey * tp.pitchFull);
    int delta   = clampi(desired - curPitch, -tp.pitchSlew, tp.pitchSlew);
    int pitch   = clampi(curPitch + delta, tp.pitchMin, tp.pitchMax);
    if (pitch != curPitch) { t.pitch = pitch; t.move = true; }
  }
  return t;
}

}  // namespace stkchan
