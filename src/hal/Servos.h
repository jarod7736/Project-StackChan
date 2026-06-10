#pragma once
#include <Arduino.h>

namespace stkchan {

class Servos {
 public:
  bool begin();   // I²C init + PCA9685 init
  void setYaw  (int deg);                // hard-set
  void setPitch(int deg);
  void easeYawTo  (int deg, uint32_t durationMs);
  void easePitchTo(int deg, uint32_t durationMs);
  bool isMoving() const;

  // Call once per loop() to drive easing.
  void tick(uint32_t nowMs);

  int currentYaw()   const { return yawDeg_; }
  int currentPitch() const { return pitchDeg_; }

  // Yaw range: -45..+45. Pitch range: -4..+25 (mechanical limits). Down-travel
  // clipped to -4 for the assembled case: any further forward and the chin
  // fouls the base. Deeper requests (e.g. "sad" = -10) clamp here to a
  // shallower droop; idle nods (-3..+3) still fit.
  static constexpr int kYawMin    = -45;
  static constexpr int kYawMax    =  45;
  static constexpr int kPitchMin  = -4;
  static constexpr int kPitchMax  =  25;

 private:
  int yawDeg_   = 0;
  int pitchDeg_ = 0;
  struct Easing {
    bool active = false;
    int  from   = 0;
    int  to     = 0;
    uint32_t startMs = 0;
    uint32_t durMs   = 0;
  } eYaw_, ePitch_;

  static int clamp_(int v, int lo, int hi);
  void writeYaw_(int deg);
  void writePitch_(int deg);
};

extern Servos servos;

}  // namespace stkchan
