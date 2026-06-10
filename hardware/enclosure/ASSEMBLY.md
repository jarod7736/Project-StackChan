# StackChan (MG90S / SG90 · Takao) — Assembly & Fitment Guide

> 📷 Prefer pictures? See the **[Illustrated Assembly guide](ASSEMBLY-ILLUSTRATED.md)**
> — exploded view + a render for every step, generated from the real STLs.

This is the build we're using: the **"stack-chan_takao_base"** shell (the upstream
"SG90 case" set by Takao Akaki, via `mongonta0716/3DPrinter_Models`), driven by
**MG90S** servos on our CoreS3 + **PCA9685** firmware. It is **not** the M5 K151 shell (that one needs DYNAMIXEL
XL330 serial-bus servos — see "Other design" at the bottom).

## Printed parts (SG90 Takao set)

Source: `mongonta0716/3DPrinter_Models/stackchan_sg90_case_takao_version`
(cloned locally to `~/cloned/stackchan-sg90-models/`).

| STL | Role |
|-----|------|
| `stackchan_takao_shell_v2_resin` | Head shell — wraps the **CoreS3**; speaker grille + "stack-chan" emboss. Display faces front. Rides on the tilt servo. |
| `stackchan_takao_bracket_v2.5`   | Servo bracket — holds **both servos** (rectangular pockets): pan (lower) + tilt (upper). |
| `stackchan_takao_feet`           | Base feet (54 × 50 × 8 mm) — two pads + a center hub with the **round-horn screw pattern** (shared by MG90S/SG90). The pan servo horn screws here; the body pans on the feet. Central slot routes the servo cables down. |
| `stackchan_takao_hat_cat_CoreS3` | Optional cat-ear hat (CoreS3 fitment). |
| `…/pedestal-*`                   | Our optional electronics base (PCA9685 + barrel jack) the feet nest on. See `pedestal.scad`. |

Print note: this set is toleranced for **resin**. On FDM/PLA, open the servo
pockets / screw holes slightly or scale those features ~102–103%.

## Servos

- **2 × MG90S** (metal-gear, ~13.4 g micro servos), plain **PWM** (no IDs — unlike XL330).
  This is what the build uses. **SG90 / MS18** (plastic-gear, ~9 g) are
  pin/PWM/horn-compatible drop-ins — fine for this light, low-load head, but the
  MG90S metal gears give better durability and a slightly steadier hold.
- **Fit note:** the MG90S body is **~1–2 mm longer** than SG90, so it's a **snug**
  fit in the Takao pockets (which are cut for SG90). On FDM/PLA open the pockets a
  touch (see the print note above); a plastic-gear SG90/MS18 sits a little looser in
  the same pocket. No firmware or horn change either way.
- **Pan (yaw)** servo → drives rotation on the feet. Wire to **PCA9685 ch0**.
- **Tilt (pitch)** servo → drives head up/down. Wire to **PCA9685 ch1**.
- Firmware already matches: `src/hal/Servos.cpp` `writeYaw_`→ch0, `writePitch_`→ch1,
  limits **yaw ±45°, pitch ±25°**. No firmware change needed for this shell.
- Power the servos from a **dedicated 5 V** rail (barrel jack → PCA9685 V+),
  common ground with the CoreS3. Don't run servo power off the CoreS3 rail.

## Assembly sequence

1. **Center the servos first.** Power each servo and command it to its mid
   position (90°) *before* attaching horns, so the mechanism's range is centered.
2. **Pan servo → feet.** Screw the round horn to the **feet** center hub.
   Seat the pan servo body into the **bracket's lower pocket** so its output
   shaft engages that horn — the assembly now rotates on the feet.
3. **Tilt servo → bracket.** Seat the second servo in the **bracket's upper
   pocket** (output shaft on the head-tilt axis).
4. **Head.** Attach the **shell** to the tilt servo's horn; route both servo
   cables down through the bracket and the feet's center slot.
5. **CoreS3.** Seat the CoreS3 in the shell (display forward).
6. **Wiring.** Pan → PCA9685 ch0, tilt → ch1. PCA9685 I²C + power to the CoreS3
   (Port A Grove `Wire`, or M-Bus `Wire1` G43/G44 per `Servos.cpp`). Servo V+
   from the barrel jack.
7. **Electronics base (optional).** Drop the printed **feet into the pedestal's
   top recess**; the PCA9685 + barrel jack live inside the pedestal, cables pass
   up through the feet slot. See `pedestal.scad`.
8. **Hat** (optional) on top.

## Movement / care
- Keep within the firmware limits (yaw ±45°, pitch ±25°) — gentle desktop motion.
- Don't back-drive a powered servo by hand.

## Other design (NOT used here)
The M5 **K151** shell (`M5_Hardware/Products/K151_StackChan/`) is built for
**DYNAMIXEL XL330-M288-T** serial-bus servos (IDs 1/2, TTL bus, tilt 5–85°). It
is **incompatible** with MG90S/SG90 PWM servos and the PCA9685. We chose the SG90
Takao shell (driven by MG90S) instead so the existing PCA9685 firmware applies unchanged.

## Sources
- SG90 STLs: https://github.com/mongonta0716/3DPrinter_Models
- Takao stack-chan project: https://github.com/meganetaaan/stack-chan
- (K151/XL330 reference) https://docs.m5stack.com/en/arduino/stackchan/servo
