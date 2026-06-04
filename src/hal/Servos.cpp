#include "hal/Servos.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

namespace stkchan {

Servos servos;

// M5Stack Module13.2 Servo2 on CoreS3. Per the official compatibility chart
// the module's I2C is a FIXED connection to the M-Bus:
//   I2C_SDA → G43,  I2C_SCL → G44.
// We drive it on the secondary I2C peripheral (Wire1) so it doesn't collide
// with the CoreS3's internal Wire (display/touch/power on G11/G12). The
// PCA9685 sits at 0x40 (to confirm against the module datasheet on arrival).
constexpr int kServoSDA  = 43;
constexpr int kServoSCL  = 44;
constexpr uint8_t kServoAddr = 0x40;
static Adafruit_PWMServoDriver g_pwm = Adafruit_PWMServoDriver(kServoAddr, Wire1);

// PCA9685 PWM range for SG90: ~150 .. 600 counts at 50 Hz.
static int degToPwm(int deg) {
  // Map -90..+90 onto 150..600. Servo zero ≈ 375.
  long mapped = map(deg, -90, 90, 150, 600);
  return (int)mapped;
}

bool Servos::begin() {
  // Secondary I2C bus on the Servo2 module's fixed M-Bus pins.
  Wire1.begin(kServoSDA, kServoSCL, 400000);
  if (!g_pwm.begin()) {
    Serial.println("ERR: PCA9685 init failed (SERVO2 module not detected on Wire1 G43/G44)");
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
