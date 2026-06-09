> **⛔ SUPERSEDED (2026-06-09) — root cause FOUND & FIXED.** The silent AXP power-off is an
> **I2C-controller collision**, not a current/load/PCM problem. `Servos::begin()` does
> `Wire1.begin(17,18,400k)` = `I2C_NUM_1`, the SAME controller M5Unified uses for the
> internal AXP2101 bus (0x34, ES7210 0x40, touch 0x38, AW9523B 0x58) → a stray write disables
> a core rail → silent SoC off; the AXP holds the bad register state → battery-pull recovery.
> Source-confirmed (`M5Unified.cpp:1620-1631`) and empirically proven (an empty Port C scan
> returned the internal constellation). **Everything below about a current-spike /
> battery-PCM-OCP trip and "reduce firmware peak current" is VOID.** Fix shipped in
> `src/hal/Servos.cpp` (PCA9685 moved off `Wire1`/I2C_NUM_1 → `Wire`/I2C_NUM_0, same pins).
> Canonical record: the `axp-brownout-state` memory. Kept below for the historical
> elimination trail only.

# AXP power-off — investigation handoff (2026-06-07)

Branch: `diag/axp-silent-poweroff` (off `feat/presence-awareness`). Supersedes the
2026-06-06 handoff. Pair with the `axp-brownout-state` memory and the gated debug
plan in `2026-06-07-axp-poweroff-debug-plan.md`.

## TL;DR

The device powers off on its own. After a long session that **invalidated most prior
timing data** (a laptop-sleep confound) and reframed the bug twice, the current best
understanding:

- There appear to be **two faces of the failure**, both presenting as a **silent SoC
  power-off with vbat HEALTHY (~4.15 V) and zero AXP fault** (not the "vbat collapses
  4.15→0" the 2026-06-06 handoff assumed):
  - **Bug A — idle, battery+USB:** silent off at **~30 min** (clean, sleep-inhibited).
    Unit #2 ran **>1 h idle** without it → leans **unit-#1-specific** (n=1 each).
  - **Bug B — load-triggered, battery-only (the production-relevant one):** crashes
    **during a TTS "say"** (web control or voice). Reproduced on unit #1 across **two
    different batteries (incl. no DinBase)**. Recovery needs a **battery pull**.
- **Root cause NOT yet proven.** Leading hypothesis for Bug B: a **fast (<100 ms)
  current spike** from the streaming-TTS overlap (WiFi-RX download + amp playback) trips
  the **battery pack's own protection (PCM)** — NOT the AXP. Strong circumstantial
  support; not confirmed by a current measurement.

## The two big course-corrections this session (don't re-learn these)

1. **⚠️ LAPTOP-SLEEP CONFOUND (the big one).** The laptop's
   `gsettings ... sleep-inactive-{ac,battery}-type` was `'suspend'` → on idle it
   powered down the USB bus → dropped `/dev/ttyACM*` → my capture recorded that as a
   "device death" AND froze the capture. This **falsified** the "deconfound dies in 15 s"
   result and means **every earlier session timing** (batteryless ~139 s, battery+USB
   6–16 min, SOAK_MINIMAL 7.6 min, "deaths within seconds") is **sleep-contaminated —
   do not trust.** FIX (done, permanent): gsettings set to `'nothing'`. Also always wrap
   soaks in `systemd-inhibit --what=idle:sleep --mode=block`. *Survivals* with continuous
   heartbeats (bare 54–60 min, output_power/network/M5.update eliminations) are still
   valid — a sleep would have dropped the port mid-stream.

2. **Signature is "vbat healthy → silent cut," NOT "vbat collapse."** Measured on both
   battery+USB (Bug A) and battery-only (Bug B). The production "collapse" was likely a
   dying-ADC artifact, or a different mode. The AXP registers (0x00/0x01, IRQ 0x48-0x4A)
   never change at the cut.

## The decisive measurement (Bug B), via SD logging

Battery-only has no live USB serial, so we logged the `[AXPT]` trail to **microSD** with a
flush per sample; on the next boot the firmware dumps the prior run's log over USB. The
captured battery-only crash trail (`2026-06-07-unit1-batteryonly-crash-trail.txt`):

- 369 samples, `up=2882 → 44540 ms`; **vbat 4092–4159 mV the ENTIRE time (never sagged);
  vbus=0 (battery-only); AXP regs constant; then INSTANT cut.**
- → **Not a gradual brownout.** Consistent with a **sub-100 ms current spike** the 100 ms
  sampling can't see (vbat is a voltage proxy; **CoreS3 has no battery-current ADC** —
  getBatteryCurrent/getVBUSCurrent are hardwired 0). A faster vbat sampler was built
  (100 Hz) but **never flashed/tested** (see "what blocked").

## Recovery signature → points at the battery PCM, not the AXP

Battery-only, after a crash: **power button does NOT restart it — only pulling the
battery does.** Reasoning: battery-only → when the PCM cuts the cell, the AXP loses its
only input → unpowered → PWRON dead → reseating the battery resets the PCM. A plain AXP
soft-off (reg 0x10) would be PWRON-recoverable; it isn't. Matches `ecf953b`'s "tripped the
PACK's own protection," now across two batteries. CONSEQUENCE: can't change the battery's
PCM → **the fix must be a firmware PEAK-CURRENT reduction** (and the AXP-latch read is moot
— the battery pull wipes the AXP always-on domain anyway).

## Ruled out (measured this session)

Hardware-as-dead (bare-WiFi ran 54–60 min; both units run), AXP internal OCP/thermal, I2C
corruption (cid 0x03 = 0x4A always), I2C "shark-fin" artifact, firmware-crash (HWCDC
survives resets; full power loss seen), battery-path OPEN as the cause, `output_power`
(bare survived 60 min with it false), the always-on network stack (full app + SOAK_MINIMAL
still died), `M5.update()`/power-button path (bare + M5.update survived 60 min), the
`bf4b46b` `0x16` ilim write (removing it didn't stop deaths). **Gate 0 code read:** the
firmware does **NO power manipulation** — only the (disabled) `0x16` write; zero GPIO
writes; every `M5.Power.*` call is a READ; no powerOff/sleep/timerSleep anywhere. So the
off is **not commanded by our code** → load/electrical or hardware.

## Hardware / unit state

- **Unit #1** (MAC `10:20:BA:26:E8:98`): the abused test unit. Reproduces Bug A (idle
  ~30 min, battery+USB) and Bug B (load crash, battery-only). Its flash got **corrupted**
  by repeated abrupt power-cuts + esptool churn → boot loop (`rst:0x3 RTC_SW_SYS_RST`);
  **recovered** by reflashing the known-good build. Currently runs **known-good `2195962`
  firmware (no SD logger)**, USB-only at last check (no battery → vbat reads floating ~0,
  expected).
- **Unit #2 / "Jarvis"** (MAC `F4:12:FA:BA:0E:04`): 2nd CoreS3, was running other firmware
  ("Jarvis"); we **backed it up** (`/home/jarod7736/jarvis_full_backup.bin`, 16 MB,
  byte-exact restore) and flashed our deconfound build. Ran **>1 h idle** (didn't repro
  Bug A). Its USB serial is unreliable (re-enumerates ~every 75–140 s → HWCDC drops TX) —
  a unit-specific USB quirk. **Restore Jarvis from the backup when done with it.**
- **USB gotcha:** the original laptop port went flaky (re-enumeration storms, esptool I/O
  errors) — a **different port (1-3) is stable**. Node hops `ttyACM0↔ttyACM1`; tools glob
  `/dev/ttyACM*`. The flakiness was the *port*, not (only) the device.

## Git / firmware / artifact state

- **Branch:** `diag/axp-silent-poweroff` (pushed to origin). HEAD `2195962` = the
  known-good `[AXPT]` build (no SD), which is **what's flashed on unit #1**.
- **Stashed:** `stash@{0}` = the **SD crash-trail logger + 100 Hz vbat sampler**
  (`src/main.cpp` only). Restore with `git checkout diag/axp-silent-poweroff &&
  git stash pop`. NOTE: the SD logger WORKS (captured the trail) but **repeated
  crash-testing with it corrupts the device flash** — harden it (or use a fresh card /
  external scope) before heavy reuse.
- **Uncommitted (working tree):** `tools/fast-flash.sh` (now globs ttyACM*),
  `tools/loop-capture.sh` (NEW, survives re-enumeration), `tools/presence-monitor.sh`
  (NEW, death-by-sustained-USB-loss), and two docs (this plan/handoff + the crash-trail).
- **Evidence:** `2026-06-07-batteryless-usb-stable-openpin-signature.log` (batteryless
  open-pin signature), `2026-06-07-unit1-batteryonly-crash-trail.txt` (the Bug B trail).
- **Tools:** `fast-flash.sh` (catch port + direct esptool), `fast-capture.sh` (single-shot),
  `loop-capture.sh` (looping, re-enum-proof), `presence-monitor.sh`. Always run captures
  under `systemd-inhibit --what=idle:sleep --mode=block`.

## What blocked us at the end (so you don't repeat it)

- Trying to flash the 100 Hz build, my **back-to-back esptool retries** (each toggles the
  reset line) + the flaky port drove a re-enumeration storm and left unit #1 stuck/looping;
  combined with crash-corrupted flash → it wouldn't boot. **Lesson: ONE clean flash, on the
  stable port; don't loop esptool.** Recovery = reflash known-good on the stable port.

## Resume plan (gated — see the debug-plan doc for the full tree)

Pick by what you want to confirm, sleep always inhibited:

1. **Confirm Bug B mechanism without stressing the device flash → external multimeter/scope
   on the battery during a "web say."** Directly shows the current spike / sag at the trip.
   This is the *definitive* test and avoids the SD-corruption problem. **Recommended.**
2. **Or** restore the SD logger (stash pop), reformat the card, reflash unit #1, and re-run
   battery-only "web say" with the **100 Hz** vbat sampler — to see if any sag appears in
   the final ms (voltage event) vs flat (current trip). Caveat: a current trip won't sag
   vbat at any rate; and crash-testing re-corrupts flash.
3. **Then the FIX (once mechanism confirmed):** reduce the firmware's peak current on
   battery — de-overlap WiFi-download and amp (buffer-then-play, reverses the streaming
   decision in `audio-streaming-decision`), OR a streaming-compatible cut (lower volume
   and/or throttle WiFi-RX during playback). Test: battery-only "web say" survives.
4. **Bug A (lower priority):** if it matters, re-confirm the idle ~30 min battery+USB death
   on unit #1 vs unit #2, sleep-inhibited, to settle unit-specific-hardware vs firmware.

## Interaction model reminder (for testing)

Voice is **press-and-hold the screen** (touchscreen `g_pressFlag`), speak, release — no
wake word. Needs WiFi + reachable STT/chat/TTS backends (else "sleepy"/"doubt" error face).
The **web control page** (`http://<ip>/`, unit #1 was `192.168.1.121`, SSID `holdfast`) can
send a "say <text>" → TTS playback — the cleanest Bug B trigger. Backends confirmed working
(saw an LLM reply this session).
