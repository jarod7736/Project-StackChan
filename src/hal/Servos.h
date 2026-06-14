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

  // Yaw range: -45..+45. Pitch range: -2..+45. +deg = head UP (clockwise);
  // -deg = head down (counterclockwise). The assembled case allows almost no
  // down-travel — the chin sits right at the base — so negative requests
  // (e.g. "sad" = -10, idle nods to -3) clamp at -2.
  static constexpr int kYawMin    = -45;
  static constexpr int kYawMax    =  45;
  static constexpr int kPitchMin  = -2;
  static constexpr int kPitchMax  =  45;

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
