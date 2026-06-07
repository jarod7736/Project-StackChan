#pragma once
#include <stdint.h>

// Pure, hardware-free logic for the on-device presence feature:
//   - PresenceDebounce: turns a noisy per-frame detected/not-detected stream
//     into stable "present" state + arrived/left edges (hysteresis).
//   - trackTarget(): maps a face bounding-box center into an eased, slew- and
//     limit-bounded yaw/pitch servo target ("look-toward-you").
// No Arduino/M5 deps -> compiled and unit-tested in [env:native]. The hardware
// module vision/PresenceSensor + motion/MotionDirector consume these.

namespace stkchan {

// ── Presence hysteresis ────────────────────────────────────────────────────

struct PresenceParams {
  int      arriveHits = 3;       // consecutive detections to declare "present"
  uint32_t absentMs   = 30000;   // sustained no-detection before "absent"
};

class PresenceDebounce {
 public:
  PresenceDebounce() = default;
  explicit PresenceDebounce(PresenceParams p) : p_(p) {}

  // Feed exactly one observation per scan tick.
  void update(bool detected, uint32_t nowMs);

  bool present() const { return present_; }
  bool consumeArrived();   // true once on transition into present
  bool consumeLeft();      // true once on transition into absent

 private:
  PresenceParams p_{};
  int      hits_       = 0;
  bool     present_    = false;
  bool     arrived_    = false;
  bool     left_       = false;
  uint32_t lastSeenMs_ = 0;
};

// ── bbox -> servo tracking ──────────────────────────────────────────────────

struct TrackParams {
  float deadband  = 0.12f;   // normalized error below which we hold still
  float yawGain   = 0.5f;    // fraction of full-scale per unit normalized error
  float pitchGain = 0.5f;
  int   yawSlew   = 8;       // max deg yaw change per call
  int   pitchSlew = 5;       // max deg pitch change per call
  int   yawMin = -45, yawMax = 45;     // mechanical limits (Servos.h)
  int   pitchMin = -25, pitchMax = 25;
  int   yawFull = 45;        // deg mapped to |error|==1
  int   pitchFull = 25;
};

struct TrackTarget {
  int  yaw;
  int  pitch;
  bool move;   // false if both axes within deadband (no command needed)
};

// cx,cy: face bbox center in frame pixels. frameW/H: frame dimensions.
// curYaw/curPitch: current servo angles. Returns a clamped, slew-limited target.
// Sign convention: +error (face right/below center) decreases the angle; verify
// physical direction on hardware (camera may be mirrored) and flip the gain sign
// if needed.
TrackTarget trackTarget(int cx, int cy, int frameW, int frameH,
                        int curYaw, int curPitch, const TrackParams& tp);

}  // namespace stkchan
