#pragma once

// WakeVad — adaptive-floor energy VAD over fixed-size RMS frames (30 ms at
// the call site). Pure C++ (no Arduino) for `pio test -e native`.
// Spec: docs/superpowers/specs/2026-06-11-wake-word-design.md §1.

#include <stdint.h>

namespace stkchan {

struct WakeVadConfig {
  float tripRatio          = 2.0f;   // speech := rms > floor * ratio
                                     // (was 3.0 — live telemetry 2026-06-11:
                                     // room floor ~140, speech peaks 320-1050;
                                     // much of real speech sat under 3x)
  int   tripFrames         = 3;      // consecutive speech frames to trip (90 ms)
  int   closeSilenceFrames = 20;     // consecutive quiet frames to close (600 ms)
  int   maxFrames          = 110;    // frames after trip before Overflow (3.3 s)
  float floorAlpha         = 0.02f;  // noise-floor EMA rate (quiet frames only)
  float floorInit          = 200.0f;
  float floorMin           = 100.0f; // floor never adapts below this
                                     // (was 50 — see warmupFrames note)
  int   warmupFrames       = 10;     // frames ignored after (re)start: mic
                                     // warm-up reads near-zero and poisoned
                                     // the floor EMA (live bug 2026-06-11 —
                                     // floor pinned at min -> threshold under
                                     // room noise -> perpetual trip/STT loop)
};

class WakeVad {
 public:
  enum class Event : uint8_t { None, Tripped, Closed, Overflow };

  explicit WakeVad(const WakeVadConfig& cfg = {})
      : cfg_(cfg), floor_(cfg.floorInit) {}

  // Feed one frame's RMS. Returns Tripped exactly once per utterance;
  // Closed/Overflow end it (caller must reset() before reuse).
  Event onFrame(float rms);

  bool  tripped() const { return tripped_; }
  float noiseFloor() const { return floor_; }
  void  reset();

 private:
  WakeVadConfig cfg_;
  float floor_;
  bool  tripped_         = false;
  int   speechRun_       = 0;
  int   quietRun_        = 0;
  int   framesSinceTrip_ = 0;
  int   framesSeen_      = 0;  // for warm-up skip; reset() zeroes it
};

}  // namespace stkchan
