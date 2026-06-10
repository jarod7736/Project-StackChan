# Session handoff — full current state (2026-06-10)

Resume doc covering everything through 2026-06-10. The whole electronics/firmware stack is
DONE and on `main`; the only open work is **mechanical (head fitment)**. Detailed docs are
linked rather than duplicated.

## Repo/branch state

- `main` == `feat/presence-awareness` == **`25da1f9`** (kept in lockstep; both pushed).
- Other branches retained: `diag/axp-bisect-builds` (AXP bisection history),
  `diag/axp-silent-poweroff`. `stash@{0}` = old SD-logger WIP, parked.
- Device (unit #1, MAC `10:20:BA:26:E8:98`) runs the current build.

## ✅ DONE

1. **AXP silent power-off — root-caused, fixed, shipped, validated.** It was an
   **I2C-controller collision**: `Servos::begin()` ran `Wire1.begin(17,18)` = `I2C_NUM_1` =
   M5Unified's internal AXP bus → clobbered a rail → silent off, battery-pull recovery. Fix =
   drive the PCA on Arduino `Wire` (`I2C_NUM_0`), same Port C pins (`6889e75`). 30-min soaks
   pass on both the fix build and the cleaned ship build. Diagnostic scaffolding removed +
   WiFi TX cap dropped (`44fce8b`). Full detail: `axp-brownout-state` memory + the
   banner-marked `2026-06-0{6,7}-axp-poweroff-*` docs.
2. **2-servo head electronics — physically working.** Both axes sweep; down-tilt clears the
   base. Validated wiring (also in `src/hal/Servos.cpp` + `docs/hardware/cores3-pca9685-wiring.png`):
   PCA VCC ← **M-Bus pin 12** (`VCC_3V3`, AXP DCDC3, always-on, underside DinBase pad);
   SDA ← Port C **`T`/G17**, SCL ← Port C **`R`/G18**; GND common; servo **V+ ← separate
   external 5–6V supply** + common ground + bulk cap; **yaw=ch0, pitch=ch1**.
3. **Firmware additions** (`829b0d3`): boot **self-test sweep** (yaw ±45 / pitch +25,-12) +
   **`kPitchMin` -25 → -12** (at -25 the chin hit the base). Up (+25) and yaw (±45) unchanged.
4. **Docs:** `2026-06-09-servo-bringup-handoff.md` (wiring + the full bring-up troubleshooting
   playbook + CoreS3 USB-CDC capture gotchas), the wiring diagram, and this handoff.

## 🔧 OPEN — mechanical (head fitment), the only active thread

The current head has the **servo mount baked into the mesh**, so the CoreS3 + battery and the
servo mount fight for the same front space. Set up for the fix:
- **Vendored the official stack-chan v1.0 SG90 case** (Apache-2.0) to `hardware/case_SG90/` —
  STEP + STL for shell / **separate servo bracket** / feet. Plan + Fusion how-to in
  `hardware/case_SG90/FITMENT_NOTES.md`.
- **Path B (fastest, no CAD):** move the battery to the rear — frees the front AND counterweights
  the face (CG over the pitch shaft → SG90 doesn't strain / nose-dive).
- **Path A (real fix):** in Fusion, shift the separate bracket back (Move/Copy) or deepen the
  shell (Press Pull) on the imported STEP body. **The CAD edit is the owner's** — this dev env
  has NO CAD tooling, and the Autodesk connector is help-DOCS search, not CAD editing.

## 🔧 OPEN — minor / non-blocking

- **Servo supply is still computer USB** (~500mA) — fine for one servo, may sag when both move
  hard together. Get a dedicated 5V/≥2A source.
- **Web `/api/control/servo` doesn't pause idle** → a streamed slider restarts the 400ms ease and
  the head crawls. One-line fix in `ControlBridge` dispatch (pause idle on servo cmd); not done.
- **Real PCA on Port C** would need ~4.7k pull-ups (or move to Port A G1/G2); the breakout covers
  it for now.

## ⚠️ Gotchas worth re-reading before touching hardware

- **A computer USB port can't power servos** (current-starved → "tiny movements" on big moves).
- **CoreS3 = native USB-Serial/JTAG; pyserial opens assert DTR/RTS = the RESET line** → a
  poll-and-reopen capture resets the chip every reopen (it faked a "12s reboot loop"). Open with
  `dtr=False; rts=False`. Port hops `ACM0↔ACM1` (glob `/dev/ttyACM*`).
- **De-confound USB/host artifacts by running battery-only and watching the hardware** — this rig
  has a long history of host/port artifacts masquerading as device faults.

## Pointers

- Memory: `axp-brownout-state`, `servo-bringup` (+ `MEMORY.md` index).
- Docs: `docs/superpowers/2026-06-09-servo-bringup-handoff.md`, `hardware/case_SG90/FITMENT_NOTES.md`,
  `docs/hardware/cores3-pca9685-wiring.png`.
- Code: `src/hal/Servos.{cpp,h}`, `src/main.cpp` (boot sweep), `src/motion/MotionDirector.cpp`.
