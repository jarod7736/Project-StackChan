#include "hal/Servos.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

namespace stkchan {

Servos servos;

// PCA9685 servo driver on CoreS3 via PORT C (G17/G18). The M-Bus is occupied by
// the DinBase (which holds the battery), so the original M-Bus pins (G43/G44)
// aren't reachable — we solder the PCA's I2C to Port C instead. ESP32-S3 routes
// I2C to any GPIO, so we run it on the secondary peripheral (Wire1) to keep off
// the CoreS3's internal Wire (display/touch/power/codecs on G11/G12 — note the
// ES7210 mic codec is ALSO 0x40 there, so the PCA must stay on its own bus).
//   PCA SDA → G17 (Port C),  PCA SCL → G18 (Port C)
//   PCA VCC → 3V3 (NOT the Grove 5V — keeps the bus 3.3V-safe), GND common.
// PCA9685 at default address 0x40.
constexpr int kServoSDA  = 17;   // Port C
constexpr int kServoSCL  = 18;   // Port C
constexpr uint8_t kServoAddr = 0x40;
static Adafruit_PWMServoDriver g_pwm = Adafruit_PWMServoDriver(kServoAddr, Wire1);

// PCA9685 PWM range for SG90: ~150 .. 600 counts at 50 Hz.
static int degToPwm(int deg) {
  // Map -90..+90 onto 150..600. Servo zero ≈ 375.
  long mapped = map(deg, -90, 90, 150, 600);
  return (int)mapped;
}

bool Servos::begin() {
  // Secondary I2C bus on Port C (G17/G18).
  Wire1.begin(kServoSDA, kServoSCL, 400000);
  if (!g_pwm.begin()) {
    Serial.println("ERR: PCA9685 init failed (not detected on Wire1 Port C G17/G18 @0x40)");
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
