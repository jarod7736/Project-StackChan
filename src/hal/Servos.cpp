#include "hal/Servos.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

namespace stkchan {

Servos servos;

// PCA9685 servo driver on CoreS3 via PORT C (G17/G18). The M-Bus is occupied by
// the DinBase (which holds the battery), so the original M-Bus pins (G43/G44)
// aren't reachable — we solder the PCA's I2C to Port C instead.
//   PCA SDA → G17 (Port C),  PCA SCL → G18 (Port C)
//   PCA VCC → 3V3 (NOT the Grove 5V — keeps the bus 3.3V-safe), GND common.
// PCA9685 at default address 0x40.
//
// ⚠ MUST use Arduino `Wire` (I2C_NUM_0), NOT `Wire1` (I2C_NUM_1). On CoreS3,
// M5Unified binds its INTERNAL bus (AXP2101 0x34 + codec/touch/AW9523B) to
// I2C_NUM_1 (M5Unified.cpp:1624) — so `Wire1.begin(17,18)` reconfigured the very
// controller driving the AXP power chip; a stray byte then disabled a rail → the
// long-hunted silent "AXP power-off" (vbat healthy, battery-pull-only recovery).
// Root cause found 2026-06-09; PROVEN by an I2C scan on Wire1/Port C returning the
// whole internal constellation (0x34 AXP, 0x38 touch, 0x40 codec, 0x58 AW9523…).
// I2C_NUM_0 is free (the app uses no Port A / Ex_I2C devices). NOTE: Port C has no
// external pull-ups — for a real PCA add ~4.7k pull-ups or move it to Port A (G1/G2).
constexpr int kServoSDA  = 17;   // Port C
constexpr int kServoSCL  = 18;   // Port C
constexpr uint8_t kServoAddr = 0x40;
static Adafruit_PWMServoDriver g_pwm = Adafruit_PWMServoDriver(kServoAddr, Wire);

// PCA9685 PWM range for SG90: ~150 .. 600 counts at 50 Hz.
static int degToPwm(int deg) {
  // Map -90..+90 onto 150..600. Servo zero ≈ 375.
  long mapped = map(deg, -90, 90, 150, 600);
  return (int)mapped;
}

bool Servos::begin() {
  // Secondary I2C on Port C via Wire (I2C_NUM_0) — see the controller warning above.
  Wire.begin(kServoSDA, kServoSCL, 400000);
  if (!g_pwm.begin()) {
    Serial.println("ERR: PCA9685 init failed (not detected on Wire Port C G17/G18 @0x40)");
    return false;
  }
  g_pwm.setOscillatorFrequency(27000000);
  g_pwm.setPWMFreq(50);
  writeYaw_(0);
  writePitch_(0);
  return true;
}

int Servos::clamp_(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void Servos::writeYaw_(int deg) {
  deg = clamp_(deg, kYawMin, kYawMax);
  g_pwm.setPWM(0, 0, degToPwm(deg));
  yawDeg_ = deg;
}
void Servos::writePitch_(int deg) {
  deg = clamp_(deg, kPitchMin, kPitchMax);
  g_pwm.setPWM(1, 0, degToPwm(deg));
  pitchDeg_ = deg;
}

void Servos::setYaw  (int deg) { eYaw_.active   = false; writeYaw_(deg); }
void Servos::setPitch(int deg) { ePitch_.active = false; writePitch_(deg); }

void Servos::easeYawTo(int deg, uint32_t durMs) {
  eYaw_ = {true, yawDeg_, clamp_(deg, kYawMin, kYawMax), millis(), durMs};
}
void Servos::easePitchTo(int deg, uint32_t durMs) {
  ePitch_ = {true, pitchDeg_, clamp_(deg, kPitchMin, kPitchMax), millis(), durMs};
}

bool Servos::isMoving() const { return eYaw_.active || ePitch_.active; }

static int interp(int from, int to, uint32_t elapsed, uint32_t dur) {
  if (elapsed >= dur) return to;
  float t = (float)elapsed / (float)dur;
  // ease-in-out cubic
  float eased = t < 0.5f ? 4 * t * t * t
                          : 1 - powf(-2 * t + 2, 3) / 2;
  return from + (int)((to - from) * eased);
}

void Servos::tick(uint32_t nowMs) {
  if (eYaw_.active) {
    uint32_t e = nowMs - eYaw_.startMs;
    writeYaw_(interp(eYaw_.from, eYaw_.to, e, eYaw_.durMs));
    if (e >= eYaw_.durMs) eYaw_.active = false;
  }
  if (ePitch_.active) {
    uint32_t e = nowMs - ePitch_.startMs;
    writePitch_(interp(ePitch_.from, ePitch_.to, e, ePitch_.durMs));
    if (e >= ePitch_.durMs) ePitch_.active = false;
  }
}

}  // namespace stkchan
