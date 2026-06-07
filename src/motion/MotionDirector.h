#pragma once
#include <Arduino.h>
#include <string>

namespace stkchan {

class MotionDirector {
 public:
  void begin();
  void tick(uint32_t nowMs);   // call once per loop()

  void pauseIdle();
  void resumeIdle();

  void startSpeechBob(float amp);  // amp from ExpressionMap
  void stopSpeechBob();

  void onExpressionChange(const std::string& tag);  // applies tilt

  // External nudges:
  void onBump();    // IMU-driven "look around" (Phase 2 hook; no-op for now)

  // Presence "look-toward-you" tracking (driven from PresenceSensor in IDLE).
  void startTracking();   // pause idle saccades; begin pursuing the face
  void stopTracking();    // resume idle motion
  bool isTracking() const { return tracking_; }
  // Ease the head toward a face bbox center (frame pixel coords). Caller gates
  // this to IDLE + a present face; the eased step is slew/deadband/limit-bounded.
  void lookAt(int cx, int cy, int frameW, int frameH);

 private:
  bool     tracking_    = false;
  bool     idleEnabled_ = true;
  bool     bobActive_   = false;
  float    bobAmp_      = 1.0f;
  uint32_t nextSaccadeMs_ = 0;
  uint32_t nextNodMs_     = 0;
  uint32_t lastBobMs_     = 0;
  int      bobPhase_      = 0;  // 0/1 alternating
};

extern MotionDirector motion;

}  // namespace stkchan
