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

 private:
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
