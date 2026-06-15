# Handoff — pitch servo "won't tilt up" (2026-06-14)

## TL;DR
Pitch won't drive **up** when the head is attached. After full diagnosis the root
cause is **mechanical binding in the pitch (tilt) pivot — NOT the servo, channel,
wiring, or firmware.** Next step is purely mechanical: free the pivot so the bare
head swings up/down smoothly by hand, then reconnect and recalibrate firmware.

## Goal that started this
Get the head to tilt **up** far enough to face a user whose head sits **above the
camera** (camera can't see the face when the head is level). Began as "widen the
pitch range," turned into a hardware diagnosis.

## Root-cause diagnosis (evidence chain — this is the important part)
It is **mechanical pivot binding**, proven by elimination:
1. **Yaw works perfectly** on the same MG90S servo → not a servo-type problem.
2. **Disconnect the tilt horn from the head** → the bare horn sweeps **up freely**
   and **>180° down** → servo + PCA9685 **channel 1** + wiring are all GOOD.
3. **Reconnect the head** → up = **no movement, no buzz**; down works (gravity helps
   push through the bind).
4. **Hand test:** lifting the head up is **stiff / binding** (not smooth-heavy), and
   the servo **holds** it once it's raised → friction, not lack of holding torque.
5. **Removed the battery** (took the front weight away, now hangs below) → **still
   won't go up** → rules out weight / front-heavy balance / torque.

Conclusion: friction/bind in the tilt pivot. **Prime suspect:** the tilt-horn axis
and the opposite **head-pivot pin are not coaxial** (user noted "the pin gets
squeezed"). Loosening that pin alone did **not** fix it.

Red herring noted: all 4 MG90S spin **>360° freely by hand when unplugged**. That did
NOT block positioning (yaw is fine), so it is not the cause of the bind.

## Hardware state
- Servos: **MG90S** (4 units), in the **MG90S2 bracket** (printed/assembled).
  Supersedes the older "MS18 in bracket_SG90 / MG90S doesn't fit" notes — the
  MG90S2 taller bracket is what made MG90S fit. Old SG90 envelope table
  (`hardware/case_SG90/FITMENT_NOTES.md`) applies only to the v1.0 SG90 case.
- Tilt servo on **PCA9685 channel 1** (`setPWM(1,…)`, sign-inverted for the mirrored
  mount); yaw on **channel 0**.

## NEXT STEPS (when resuming)
1. **Mechanical (do first):** with the servo disconnected, swing the bare head up/down
   by hand on its pivot. Target = **smooth and free**, no stiffness, no notchy spots.
   Feel for: stiff-throughout (pivot pinched too tight, or horn-axis vs pin not
   coaxial = cocked) vs. jams-at-an-angle (shell/wire/boss rubbing at that point).
   Fix the drag.
2. **Re-test:** reconnect, then drive an up-sweep and confirm the head physically
   tilts up:
   `for p in 0 15 30 45; do curl -s -X POST "http://<ip>/api/control/servo?yaw=0&pitch=$p"; sleep 2; done`
3. **Recalibrate firmware** once it moves freely (see below) and re-commit.

## Firmware state — IMPORTANT
- **Branch:** `feat/pitch-range-and-servo-hud` (NOT pushed / no PR).
  - Committed `801d5a3`: pitch range **-2..+45** across `Servos.h` clamp, MCP
    `move_head`, web slider.
  - Committed `a55aba3`: top-right **servo pitch/yaw debug HUD** (`Face.h/.cpp`,
    `main.cpp`) — shows live `P:<pitch>  Y:<yaw>` from `servos.currentPitch()/Yaw()`.
- **Uncommitted exploratory edits (currently FLASHED on the device):** clamps opened
  to **±90** in `Servos.h`, MCP `move_head` ±90 in `McpServer.cpp`, web sliders ±90 in
  `data/web/index.html`. These were "remove all limits" for exploration — **revert /
  replace with the real calibration; do NOT commit ±90.**

### Recalibration TODO (after the pivot is freed)
- `degToPwm()` maps ±90 → PWM 150..600 (0.73–2.93 ms). **2.93 ms exceeds MG90S spec
  (~2.5 ms)** — the deep-down end likely pins the servo. Recalibrate to the MG90S's
  real pulse range.
- Empirically find the commands for **level / max-useful-up / chin-down**, then set
  `kPitchMin/kPitchMax` + add a **trim offset** so logical `0` = physical level
  (keeps expression tilts/idle-nods/tracking valid). HUD shows *commanded* value, so
  read landmarks by eye, not degrees.
- Mirror final limits into MCP `move_head` and the web slider, then commit (replacing
  the ±90 edits).

## Device / ops cheatsheet
- CoreS3, MAC **10:20:BA:26:E8:98**, currently **192.168.1.120** (DHCP floats —
  find via `avahi-resolve -n stackchan.local` or a subnet ping-sweep + ARP for the MAC).
- Drive servo: `curl -X POST "http://<ip>/api/control/servo?yaw=N&pitch=N"`.
  Read state: `GET /api/control/state`. HUD = **commanded** value, not measured.
- Build: `~/.pio-venv/bin/pio run -e cores3` (app), `… -t buildfs` (FS). **Don't run
  app + buildfs concurrently** — they contend on the build dir and leave a stale
  `firmware.bin`; build sequentially and check the `.bin` timestamp before flashing.
- OTA: `~/.pio-venv/bin/python ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py -i <ip> -p 3232 -a gr8ful -f .pio/build/cores3/firmware.bin`
  (password **gr8ful**). FS-OTA: add `-s -f .pio/build/cores3/littlefs.bin`.
  Verify the reboot via the **`infers` counter reset** in `/api/debug/presence`
  (no build tag). OTA can fail with "Error Uploading" (presence-inference vs OTA
  writer collision) — **just retry**.
- Autonomous motion (idle nods/saccades + face tracker) will move/fight the servo;
  with limits removed the tracker can drive it past stops. Keep face out of frame or
  disable tracking during bench work.
