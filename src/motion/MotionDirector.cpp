#include "motion/MotionDirector.h"
#include "hal/Servos.h"
#include "face/ExpressionMap.h"

namespace stkchan {

MotionDirector motion;

void MotionDirector::begin() {
  nextSaccadeMs_ = millis() + random(4000, 8000);
  nextNodMs_     = millis() + random(12000, 20000);
}

void MotionDirector::pauseIdle()  { idleEnabled_ = false; }
void MotionDirector::resumeIdle() { idleEnabled_ = true; }

void MotionDirector::startSpeechBob(float amp) {
  bobActive_ = true;
  bobAmp_    = amp;
  lastBobMs_ = millis();
}
void MotionDirector::stopSpeechBob() {
  bobActive_ = false;
  servos.easePitchTo(0, 300);
}

void MotionDirector::onExpressionChange(const std::string& tag) {
  auto m = expressionFor(tag);
  servos.easeYawTo  (m.yawDeg,   500);
  servos.easePitchTo(m.pitchDeg, 500);
}

void MotionDirector::onBump() { /* TODO Phase 2: IMU-driven react */ }

void MotionDirector::tick(uint32_t nowMs) {
  servos.tick(nowMs);

  if (bobActive_) {
    if (nowMs - lastBobMs_ > 220) {
      int target = (bobPhase_ ? 0 : (int)(4 * bobAmp_));
      servos.easePitchTo(target, 200);
      bobPhase_ ^= 1;
      lastBobMs_ = nowMs;
    }
    return;  // don't interleave idle motion during speech
  }

  if (!idleEnabled_) return;

  if ((int32_t)(nowMs - nextSaccadeMs_) >= 0) {
    int yaw = (int)random(-8, 9);
    servos.easeYawTo(yaw, 400);
    nextSaccadeMs_ = nowMs + random(4000, 8000);
  }
  if ((int32_t)(nowMs - nextNodMs_) >= 0) {
    int pitch = (int)random(-3, 4);
    servos.easePitchTo(pitch, 600);
    nextNodMs_ = nowMs + random(12000, 20000);
  }
}

}  // namespace stkchan
