# Servo / PCA9685 head bring-up — handoff (2026-06-09)

Covers the session that (a) shipped the AXP silent-power-off fix and (b) physically
brought up the 2-servo head (yaw + pitch) on the CoreS3 + DinBase + external PCA9685.
Pairs with the `axp-brownout-state` memory and `src/hal/Servos.cpp` (the wiring lives
in-code there) and the diagram `docs/hardware/cores3-pca9685-wiring.png`.

## TL;DR — where things landed

- **Power-off bug: SOLVED + SHIPPED.** Root cause was an **I2C-controller collision**
  (`Servos::begin()` ran `Wire1.begin(17,18)` = `I2C_NUM_1` = M5Unified's internal AXP
  bus → clobbered a rail → silent off, battery-pull recovery). Fix = drive the PCA on
  Arduino `Wire` (`I2C_NUM_0`), same Port C pins. On `main` (commit `6889e75`), validated
  by 30-min soaks. Full detail in `axp-brownout-state` memory + the banner-marked
  `2026-06-0{6,7}-axp-poweroff-*` docs.
- **Head bring-up: WORKING.** Both axes sweep full range; down-tilt clips the base.
- **Firmware added this session** (commit `829b0d3`, branch `feat/servo-selftest`):
  a boot self-test sweep + clipped `kPitchMin` -25 → -12.

## The validated wiring (this is the source of truth)

Schematic-confirmed (Sch_M5_CoreS3_v1.0: M-Bus `BUS1` + AXP2101 PMIC page), then
physically validated. See the diagram PNG and the `Servos.cpp` header comment.

| PCA9685 | ← from |
|---|---|
| **VCC (logic)** | **M-Bus pin 12 = `VCC_3V3`** (underside DinBase solder pad). AXP2101 **DCDC3**, 3.3V/1.5A, **always-on** — independent of `output_power` (which only gates the 5V boost). |
| **SDA** | Port C **`T`** = G17 |
| **SCL** | Port C **`R`** = G18 |
| **GND** | M-Bus GND (pin 1/3/5) or Port C `G` — same ground plane |
| **V+ (servo motor)** | **separate external 5–6V supply**, common ground, 470–1000µF bulk cap |
| **yaw servo** | PCA **channel 0** |
| **pitch servo** | PCA **channel 1** |

- Powering VCC at 3.3V keeps the bus 3.3V → **no level shifter**; the Adafruit breakout's
  own pull-ups → **no added resistors**. Port C itself has no pull-ups (breakout covers it).
- **Why `Wire`/I2C_NUM_0, never `Wire1`:** `Wire1`==I2C_NUM_1==the internal AXP/codec bus.
  That collision is the power-off bug. Drive the PCA only on `Wire` (I2C_NUM_0).
- **DON'T** tap M-Bus pin 28 (`BUS_OUT` 5V) or pin 30 (`VBAT`) for VCC. Meter pin 12 ≈3.30V
  before soldering.

## Firmware changes this session

- **`src/main.cpp`** — boot self-test sweep in `setup()` (after `servos.begin()`, before the
  loop's idle motion): yaw ±45, pitch +25/-12, eases pumped to completion. Runs only if
  `servos.begin()` succeeded. Wake-up flourish + "are the servos alive / full range" check.
- **`src/hal/Servos.h`** — `kPitchMin` -25 → **-12** (at -25 the chin hit the base/feet).
  -12 still clears the deepest expression ("sad" = -10) and idle nods (±3). `kPitchMax` +25
  unchanged (up has no collision). Yaw stays ±45.
- Note: in normal operation yaw only moves ±8 (idle saccades) and expressions barely touch
  yaw (only "doubt" = -15); the full ±45 sweep is reserved for face-tracking (presence, OFF
  by default) and the boot self-test. So "yaw barely moves at idle" is BY DESIGN.

## Bring-up troubleshooting playbook (hard-won — read before re-debugging servos)

Symptom → cause, in the order they bit us:

1. **PCA detected (no `servo init failed`) but nothing moves** → check, in order:
   **(a) servo V+ rail** (meter the red `V+` row → GND, must read 5–6V; the screw terminal
   often isn't clamping the wire). **(b) OE pin** — active-low; floating/high = all outputs
   off; tie to GND. **(c) servos on ch0/ch1** specifically (ch2–15 are not driven).
   **(d) common ground** between the servo supply and the CoreS3.
2. **Servo only makes "tiny movements" on a big command, free shaft** → NOT mechanical.
   Almost always **current-starved servo supply**. **A computer USB port cannot power
   servos** (~500mA, sags under motor load) — small/slow moves work, big/fast moves stutter.
   Use a dedicated 5–6V / ≥2A source.
3. **One axis works, the other only twitches** → swap the two plugs. Fault **follows the
   servo** = bad servo (or its lead/crimp). Fault **stays on the channel** = bad channel
   signal path. (Ours: the original yaw servo was genuinely dead — twitched even bare.)
4. **Commands drive the wrong axis / "both controls move pitch"** → leftover plug-swap from
   diagnostics. Re-establish: head-turns-L/R servo → ch0, head-tilts-U/D servo → ch1, verify
   with isolated `yaw N / pitch 0` and `yaw 0 / pitch N` commands.
5. **Servo moves but the head axis doesn't** → mechanical. Ours: an **over-tight mounting
   screw seized the yaw rotation** — the servo strained against it. Back the screw off until
   the axis turns freely (snug, not preloaded).

## CoreS3 USB-CDC capture gotchas (cost us a wrong conclusion)

- The CoreS3 is **native USB-Serial/JTAG** (303a:1001). Opening the port with **pyserial
  asserts DTR/RTS by default = the reset line** → your capture **resets the chip on every
  (re)open**. We briefly saw a "12-second reboot loop" that was actually our own
  poll-and-reopen script resetting it. **Fix:** open with `dtr=False; rts=False`.
- The port **hops `/dev/ttyACM0 ↔ ACM1`** on re-enumeration — a known quirk. Tools must glob
  `/dev/ttyACM*`; a fixed-port monitor loses it.
- **Decisive way to remove all USB/host/probe artifacts: run battery-only, observe the
  hardware physically.** This rig has a long history of host/port artifacts masquerading as
  device faults ("the flakiness was the port, not the device").

## Open items / notes

- **Servo supply is still computer USB** — works for one servo at a time, but when both move
  hard together (idle + expression + speech bob during a voice turn) the ~500mA rail may sag.
  A dedicated 5V/2A source is the real fix; not urgent.
- Web `/api/control/servo` does NOT pause idle motion, and a streamed slider restarts the
  400ms ease each value → the head crawls. If web control feels unresponsive, pause idle on
  servo commands (one-liner in `ControlBridge` dispatch) — not done.
- `feat/servo-selftest` (sweep + pitch clip) lands via PR to `main` this session.
- Hardware note for a REAL future PCA on Port C: no external pull-ups there (breakout covers
  it); a bare chip needs ~4.7k, or move to Port A G1/G2 (`M5.Ex_I2C`).
- Carryover: `diag/axp-bisect-builds` retained; SD-logger `stash@{0}` parked.
