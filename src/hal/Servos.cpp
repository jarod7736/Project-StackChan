#include "hal/Servos.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

namespace stkchan {

Servos servos;

// PCA9685 servo driver on CoreS3. I2C SIGNALS on PORT C (G17/G18); LOGIC POWER
// tapped from the M-Bus 3V3 rail. The M-Bus header is occupied by the DinBase
// (which holds the battery), so the original M-Bus servo pins (G43/G44) aren't
// reachable from the top — but every M-Bus pin is exposed as a SOLDER PAD on the
// DinBase underside, so the 3V3 rail is reachable there.
//
// WIRING (schematic-confirmed, Sch_M5_CoreS3_v1.0 / M-Bus "BUS1"; see the diagram
// docs/hardware/cores3-pca9685-wiring.png):
//   PCA SDA  -> G17  (Port C Grove)
//   PCA SCL  -> G18  (Port C Grove)
//   PCA VCC  -> M-Bus pin 12 = VCC_3V3  (underside pad). This is AXP2101 DCDC3,
//              3.3V / 1.5A, and is ALWAYS-ON: it is independent of
//              cfg.output_power (which only gates the separate 5V bus-boost), so
//              the rail is live even though this firmware disables that boost.
//   PCA GND  -> M-Bus GND (pin 1/3/5) OR the Port C Grove GND — same ground plane.
//   Servo V+ -> a SEPARATE external 5-6V supply (NEVER the CoreS3 battery/AXP
//              path), common ground, with a 470-1000uF bulk cap at the PCA V+
//              terminal for inrush/stall.
//   PCA9685 default address 0x40.
//
// Powering VCC at 3.3V keeps the whole I2C bus at 3.3V => NO level shifter, and the
// Adafruit PCA9685 BREAKOUT carries its own SDA/SCL pull-ups (to VCC) => NO added
// pull-up resistors. (A BARE PCA9685 chip would need 2x ~4.7k to 3.3V.)
// ⚠ DON'T tap M-Bus pin 28 (BUS_OUT = 5V boost) or pin 30 (VBAT = raw battery) for
//   VCC — either would overvolt/destroy the 3.3V bus. Verify pin 12 reads ~3.30V to
//   GND with a meter before soldering; watch for bridges to pin 11 (SPI_SCK) / 14
//   (U0TXD). Underside view MIRRORS the columns (left<->right) vs the top.
//
// ⚠ MUST use Arduino `Wire` (I2C_NUM_0), NOT `Wire1` (I2C_NUM_1). On CoreS3,
// M5Unified binds its INTERNAL bus (AXP2101 0x34 + codec/touch/AW9523B) to
// I2C_NUM_1 (M5Unified.cpp:1624) — so `Wire1.begin(17,18)` reconfigured the very
// controller driving the AXP power chip; a stray byte then disabled a rail → the
// long-hunted silent "AXP power-off" (vbat healthy, battery-pull-only recovery).
// Root cause found 2026-06-09; PROVEN by an I2C scan on Wire1/Port C returning the
// whole internal constellation (0x34 AXP, 0x38 touch, 0x40 codec, 0x58 AW9523…).
// I2C_NUM_0 is free (the app uses no Port A / Ex_I2C devices). Port C itself has no
// external pull-ups — fine here because the breakout provides them (above).
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
  // Tilt servo sits mirrored in the assembled bracket, so the pulse sense is
  // inverted: logical +deg = head UP needs the negated angle on the horn.
  g_pwm.setPWM(1, 0, degToPwm(-deg));
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
