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

  // Yaw range: -45..+45. Pitch range: -10..+45. +deg = head UP (clockwise);
  // -deg = head down (counterclockwise). After the pivot rebuild (MG90S in the
  // MG90S2 bracket + new servo holder freed the binding), the chin clears the
  // base down to ~-10 (bench-validated 2026-06-16), so expression tilts like
  // "sad" (-10) and idle nods (-3) now land. +45 is the validated max-useful-up
  // (enough to face a user seated above the camera).
  static constexpr int kYawMin    = -45;
  static constexpr int kYawMax    =  45;
  static constexpr int kPitchMin  = -10;
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
