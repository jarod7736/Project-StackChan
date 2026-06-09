> **⛔ SUPERSEDED (2026-06-09) — root cause FOUND & FIXED.** This is the oldest handoff and
> its framing (brown-out / vbat collapse / current-load) is entirely **VOID**. The actual
> root cause is an **I2C-controller collision**: `Servos::begin()` does `Wire1.begin(17,18)` =
> `I2C_NUM_1`, the SAME controller M5Unified uses for the internal AXP2101 bus → a stray write
> disables a core rail → silent off; the AXP holds the bad state → battery-pull recovery.
> Source-confirmed (`M5Unified.cpp:1620-1631`) + empirically proven. Fix shipped in
> `src/hal/Servos.cpp` (`Wire1`/I2C_NUM_1 → `Wire`/I2C_NUM_0). Canonical record: the
> `axp-brownout-state` memory. Kept below for historical context only.

# AXP2101 brown-out / power-off — investigation handoff (2026-06-06)

Branch: `feat/presence-awareness`. Resume here.

## TL;DR / state on pause

- The **full app** reproduces the production power-off; the **bisection could not**.
- Last action: a **staged, UNVERIFIED fix** is committed (`bf4b46b`) but **never flashed/tested** — the board powered off and the user left before it could be flashed.
- Board is **off the USB bus** (latched off, needs physical recovery). User is away.

## What the symptom is

Device powers off on its own. `vbat` collapses 4.15 V → ~0–0.7 V (pct→0) while
`vbus` holds ~5.1 V and heap is healthy. SoC sometimes survives on USB, sometimes
fully drops off the bus. **Recovery requires switching the DinBase battery OFF +
USB power-cycle** — the exact `ecf953b` production signature. Reproduced 2–3×, and
**escalating (faster each time)** — last deaths were within seconds of restart.

## What we proved

1. **Bisection 2a–2e + max-load all CLEARED** on the *bare diagnostic* build
   (`STKCHAN_BARE_*`/`AUDIO_*`/`MAXLOAD`). Power telemetry showed the battery
   **never discharges** in isolation: vbat pinned 4.15 V, `chg=0`, VBUS supplies
   the whole load (sag only to ~4.89 V at max), **AXP fault regs never move**. A
   per-subsystem bisection *cannot* reproduce a battery-discharge fault by
   construction — which is why everything was clean. (Earlier wrong conclusion:
   "engineered out / not reproducible" — that was the bare build only.)
2. **The real app reproduces it.** Once the full firmware ran (LVGL + servos +
   FSM + WiFi + 30 s connectivity probe), the battery-path fault fired.
3. At the fault, the AXP latched **no internal OCP** — only `WARNING_LEVEL1`
   (SOC-drop). So the **DinBase pack's own protection** is cutting, i.e. a load
   spike pulled through the battery FET trips the pack, not the AXP's BATFET-OCP.

## Leading hypothesis (NOT yet confirmed by a clean trigger capture)

A load spike exceeds the AXP **VBUS input-current limit** (reg `0x16` IIN_LIM =
`0x04` = **1500 mA**), so the AXP pulls the deficit through the battery FET and
trips the DinBase pack protection. Prime runtime trigger: the **30 s connectivity
probe's WiFi burst** (WiFi TX is the spikiest CoreS3 current). **Servos are on an
external supply** (ASSEMBLY.md / Servos.cpp) — they do NOT load the CoreS3, so
saccades are not the spike.

**Competing hypothesis (rising):** the rapid escalation (deaths within seconds)
points at a **degrading / loose DinBase battery or connector**, possibly stressed
by hours of soak + crash testing. Firmware can't fix dying battery hardware.

## The staged fix (`bf4b46b`, UNVERIFIED — flash first thing)

Build is clean (`pio run -e cores3_linux`), three changes attacking the spike
from both sides + an identification net:
- `main.cpp` setup(): raise VBUS ilim **1500→2000 mA** (`0x16` IIN_LIM `0b100→0b101`).
- `WifiManager.cpp`: cut WiFi TX **11 → 2 dBm** (`applyTxPowerCap`; rssi ~-51 so range fine).
- `main.cpp` loop(): **`[TRIP]` capture** — full AXP forensics at 100 ms whenever `vbat<3800 mV`.

(reg 0x16 encoding verified: 100=1500 mA, 101=2000 mA = max. `getBatteryCurrent`
AND `getVBUSCurrent` are both hardwired 0 on CoreS3 — infer from vbat/vbus.)

## Resume plan (morning)

1. **Recover board:** DinBase battery **OFF**, USB **in** → it should boot
   *batteryless on USB* (`output_power=false`, commit `fafce55`) and stay up —
   no battery path to trip. Eyeball the DinBase connector for looseness.
2. **Flash** the staged fix: `pio run -e cores3_linux -t upload` (or the
   poll-and-flash loop if it won't stay up: flash the instant `/dev/ttyACM0`
   appears — esptool resets it into the bootloader where the app can't crash).
3. **Verify:** stable on USB **battery-off** = app itself is stable (= "stabilized
   as it stands"). Then **battery ON** to test against the OCP:
   - holds → the load-spike OCP is mitigated (fix works).
   - still drops out battery-on while rock-stable battery-off → **hardware**
     (pack/connector) fault, not firmware. Swap/RESEAT the DinBase battery.
   - `[TRIP]` fires → read it: which IRQ (`bocp`/`ldooc`/`vrem`), boot vs runtime.

## Environment / gotchas

- Device: `/dev/ttyACM0` on the **Linux laptop** via the **`cores3_linux`** env
  (added for Linux; inherits `cores3`, overrides port). Windows side = COM16 via
  `cores3` (usbipd). ModemManager already ignores the device.
- All `STKCHAN_*` diag flags are **0** (committed `e5a25f1`); the diag scaffold
  (bare path, `DiagTlsStreamSource`, telemetry) is dormant but has a **latent
  double-free** in its fetch-failure teardown — fix that before re-enabling diag.
- lobsterboy diag HTTP/HTTPS servers (`:8088`/`:8443`) are torn down.
- Bisection rung commits 2c–2e + telemetry + restore are on `feat/presence-awareness`
  (2c/2d were also on `main` earlier). `git log` tells the full story.

## Methodological note

Six clean isolation rungs were the signal that the *test setup*, not the
subsystems, was wrong — the battery wasn't in the discharge path. Power telemetry
(vbat/vbus + AXP regs) is what cracked it. The real app, run under that telemetry,
is what finally reproduced the fault. Don't trust a "clear" without confirming the
battery is actually in the discharge path and reading how close to the OCP it got.
