> **⛔ SUPERSEDED (2026-06-09) — root cause FOUND & FIXED.** The silent AXP power-off is an
> **I2C-controller collision**: `Servos::begin()` does `Wire1.begin(17,18)` = `I2C_NUM_1`,
> the SAME controller M5Unified uses for the internal AXP2101 bus → a stray write disables a
> core rail → silent off; the AXP holds the bad state → battery-pull recovery. Source-confirmed
> (`M5Unified.cpp:1620-1631`) + empirically proven. **The current-spike / battery-PCM-OCP /
> "reduce firmware peak current" gates in this plan are VOID** — the answer wasn't a load
> problem. Fix shipped in `src/hal/Servos.cpp` (`Wire1`/I2C_NUM_1 → `Wire`/I2C_NUM_0).
> Canonical record: the `axp-brownout-state` memory. Kept below for historical context only.

# AXP power-off — structured debug plan (2026-06-07)

Branch: `diag/axp-silent-poweroff`. Supersedes the ad-hoc grind; built to escape the
slow/confounded loop. Read with the `axp-brownout-state` memory.

## Honest status (what's actually true)

Split all prior "evidence" into two piles:

- **Trustworthy** — *survivals with continuous heartbeats*: bare-WiFi 54–60 min, and the
  `output_power`, always-on-network-stack, and `M5.update()` eliminations. (A laptop sleep
  would have dropped the port mid-stream, not produced continuous counting, so these hold.)
- **Discard as data** — *every death TIMING*: "deaths within seconds", "stochastic
  30 min–2 h", "6–16 min", "7.6 min", "15 s". All contaminated by laptop auto-suspend
  cutting USB (found 2026-06-07; now permanently disabled + we wrap soaks in
  `systemd-inhibit`).

**One solid new fact:** full app → **silent ~30 min soft-off, clean, n = 1** (vbat+vbus
healthy, zero AXP fault/IRQ change). This is the first trustworthy ground we've had.

**Two failure signatures, not yet reconciled:**
- PRODUCTION (real use, "mixed/random" context per owner): vbat *collapses* 4.15→0.
- LAB (this session, clean): vbat *healthy*, silent off. `powerOff()` on AXP2101 is
  `bitOn(0x10,0x01)` — a soft-off that cuts all rails with NO IRQ — an exact match for the
  lab signature. Whether these are the same bug is an open gate (Gate 4).

Ruled out as cause (measured): hardware (bare ran long), AXP internal OCP/thermal, I2C,
battery-path open, `output_power`, always-on network stack, `M5.update()`/button path,
`bf4b46b` 0x16 ilim write. Nothing in `src/` calls powerOff/timerSleep/deepSleep.

## Method rules (apply to EVERY run from now on)

1. **Sleep inhibited always** — gsettings auto-suspend = `'nothing'` (done) AND wrap each
   capture in `systemd-inhibit --what=idle:sleep --mode=block`.
2. **Recover via POWER BUTTON ONLY** after a death — battery stays on, USB stays in. Do
   NOT toggle the battery switch (it wipes the AXP always-on domain and the latched cause).
3. **Batch unattended** — set a run, walk away, get notified on the port drop. No babysitting.
4. **n ≥ 2 before believing a timing.**

## Gates (decision-tree, ordered by yield — not a fixed ladder)

### Gate 1 — Second-CoreS3 A/B swap  ← LEAD (owner has a 2nd unit)
Flash the SAME current build to unit #2, run sleep-inhibited + unattended.
- Unit #2 **also dies ~30 min** → **FIRMWARE** confirmed (same code, different hardware).
  Proceed to Gate 2/3 on whichever unit.
- Unit #2 **runs > ~1 h clean** → **unit #1 HARDWARE** is the problem (it took days of
  abuse). The bug is unit-specific; production failures were likely this unit degrading.
  Validate by moving the suspect base/cell to #2.
This sidesteps days of firmware soaking. Decisive.

### Gate 0 — Read the bare-vs-full code delta  (free, runs in PARALLEL with every soak)
Structurally diff what the full app inits/runs that the bare loop does not. Hunt for:
timers (grep `1800000`, `180000`, minute math), RTOS tasks (xTaskCreate), any
`M5.Power.*` / library power call, NTP re-sync, web/OTA/keepalive timers. Costs nothing.

### Gate 2 — Repeatable vs stochastic  (cheap, ~2 unattended runs)
Re-run the same build 2–3×. Bisects the whole hypothesis space:
- **Tight cluster near ~30 min** → a deterministic **timer/counter** fires (then chase the
  exact code path from Gate 0's grep — 30 min is suspiciously round).
- **Wide spread** → **load/event-driven** → subtractive subsystem bisection.

### Gate 3 — Read the AXP latch at boot  (one death, high yield)
Add to firmware: read the FULL AXP register bank as the FIRST I2C op at boot, BEFORE
`M5.begin()`, and print it. Combined with power-button-only recovery, a soft-off leaves
the PMIC always-on domain powered → the next boot prints what latched. Also add reg `0x10`
to the live `[AXPT]` trail as backup. One captured death may hand us the cause.

### Gate 4 — Reconcile production (collapse) vs lab (silent)
Decide explicitly if they're the same failure. If Gate 1 says firmware and the lab death
is silent while production collapsed, confirm we're chasing the bug the owner actually hit
(not a lab ghost). Cross-check: does unit running normally (battery only, no laptop) power
off at ~30 min too?

### Then (only if gates don't resolve) — Subtractive bisection
From the full app, `#if`-out one subsystem at a time (display/LVGL → audio → motion),
sleep-inhibited, on Gate 2's branch. Death is fast-ish; a survival clears that subsystem.

### Escalation — hardware scope
If firmware instrumentation can't see the cause: logic-analyzer / scope on the PWRON line
+ the 3V3/5V rails to catch a sub-ms event or a PWRON assertion directly.

## Tools
- `tools/fast-flash.sh` — catch the port + direct esptool (beats the brown-out window).
- `tools/fast-capture.sh` — poll ttyACM*, cat until real drop (timeout-vs-drop aware).
- Capture log: `/tmp/stkchan_boot.log`. AXP IRQ decode: `axp2101_irq_t` in
  `.pio/libdeps/cores3/M5Unified/src/utility/power/AXP2101_Class.hpp`.
