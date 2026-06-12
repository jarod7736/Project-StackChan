# Wake-word session handoff — 2026-06-11 evening

State after the wake-word build + a long live-debugging session. **The robot is
parked on stable `main` (22cdea9, stock-clips build, no wake) for the night.**
The wake feature lives on `feat/wake-word` / local `worktree-wake-word` (HEAD
`f9e8986`), functionally proven but not yet deployable. Read this before the
next wake session.

## What was PROVEN working (live, on-device)

- The full pipeline fired end-to-end at 19:23: "hey Stack-chan" → VAD trip →
  capture → Whisper → match → **"Yes?" ack clip from flash** → listening
  window opened → recorded the follow-up question (STT then failed on a
  transient socket error — the only gap in an otherwise complete run).
- One-breath remainder extraction, noise rejection (Whisper's "Beadaholique"/
  "Thank you for watching" hallucinations correctly discarded), and the error
  table's silent-failure behavior all observed live.

## Field bugs found & FIXED on the branch (`f9e8986`)

1. **Floor-poisoning death spiral** (the big one): mic warm-up frames read
   near-zero RMS and dragged the adaptive floor to `floorMin` → threshold
   under room noise → perpetual trip → STT call every ~6s → TLS heap churn
   drove internal heap to ~2 KB → **WiFi/HTTP death** (the "robot went
   unresponsive" episodes; OTA impossible, USB esptool was the recovery
   path). Fixed: `WakeVadConfig.warmupFrames=10` skip + floorMin 100 +
   cooldown 5 s.
2. **Buffer-exhaust deadlock**: a VAD trip near the 4 s window end could never
   see Closed; the not-tripped restart guard then blocked forever →
   permanently deaf listener. Fixed: tripped windows submit at exhaustion.
3. **I2S driver corruption**: `M5.Speaker.end()` on an already-down speaker
   (every 4 s restart) logged `i2s_driver_uninstall: port 1 has not installed`
   and corrupted speaker state → the loud-static playback. Fixed:
   `if (M5.Speaker.isEnabled())` guard.
4. **Whisper drift vs tight matching**: real attempts transcribe as "Hey, Stat
   Chan." (dist 2) / "Hey, Stack Jam." (dist 3) — beyond maxEdits=1. Fixed:
   `matchWakeVariants()` with live-collected variants at tight per-variant
   tolerance; all of tonight's real transcripts are pinned in native tests
   (66/66 pass).

## OPEN blockers (why it's parked)

1. **The pitch servo is physically failing** — full-range commands produce
   twitches only (yaw fine, command path proven). It buzzes continuously when
   powered: that buzz sits right at the VAD threshold, trips noise windows
   (Whisper literally transcribed "bzzzzzzzz"), and burns STT calls until the
   floor adapts (~30 s). **Repair/replace the MG90S before the next wake
   session and keep servo V+ unplugged during wake testing.**
2. **chat_host NVS is UNVERIFIED and probably wrong.** The 30 s tier probe
   SYN-times-out ("connection refused" in the log is HTTPClient's misleading
   enum — the preceding line shows a 1500 ms connect timeout). The host side
   is PROVEN fine: owner's phone gets "Ollama is running" at
   `http://192.168.1.108:11434` after tonight's firewall work. A config POST
   `{"chat_host": "...108:11434", "wake_en": "0"}` returned `{"updated":1}`
   and refusals stopped for ~5 min — then returned after the next OTA reboot.
   Until tier == LAN_OK, `submitWindow_`/`onWakeDetected` silently drop
   every wake.
3. **`GET /api/config` returns empty 200s** on the wake build (POST works
   sometimes) — that's why chat_host can't be read back. Untriaged.

## Next session — three small firmware diagnostics END the guesswork

1. `ConnectivityTier::probeBackend_`: log the host string + result once per
   tier CHANGE (not per probe). Instantly answers "what is it probing?"
2. Add `wake_en` (+ `wake_word`) to the CaptivePortal config schema — the
   kill switch exists in WakeListener but can't be set remotely (schema gap).
3. Fix/triage `GET /api/config` empty response.
Then: verify chat_host, quiet room (servo unplugged), full live test, tune
`tripRatio`/cooldown for STT cost, REMOVE the DIAG telemetry, spec §6 power
soak, merge.

## Environment facts learned tonight (also in memory)

- **This PC (JAROD-DESKTOP, 192.168.1.108) IS the Ollama host** ("lobsterboy"
  in older docs ≈ this box now). Ollama 0.24.0 native Windows (PID 9324),
  dual-stack `::11434`; `gemma3n:e4b` (the firmware default) is installed.
  NOTE: `%LOCALAPPDATA%\Ollama\*.log` are stale since May — the live instance
  logs elsewhere; don't trust them as a request sensor.
- WiFi "holdfast 4" was categorized **Public** — reclassified Private tonight
  (`Set-NetConnectionProfile`); inbound-allow rules for 11434 exist (4 of
  them). Phone-from-LAN confirms reachability.
- WSL mirrored-mode tcpdump CANNOT see host↔LAN traffic (calibrated: blind
  even to this box's own robot traffic) — don't try to sniff from WSL.
- USB esptool recovery (COM16, boot_app0 @ 0xe000 + fw @ 0x10000) is the
  reliable path when the device heap-starves its network. espota "say to
  distract the FSM" trick does NOT work once HTTP is dead.
- The robot survived ~6 firmware flashes, 2 heap-death recoveries, and a
  zombie-WiFi state (esptool hard reset fixes that too) without harm.

## Pointers

- Branch: `feat/wake-word` (pushed through f9e8986). Spec + plan:
  `docs/superpowers/specs/2026-06-11-wake-word-design.md`,
  `plans/2026-06-11-wake-word.md`.
- Memory: `wake-word-debug-state` (new), `stock-clips-ops`,
  `audio-crackles-during-hard-charge`, `stackchan-flashing-workflow`.
- Serial capture tooling: `~/.claude/jobs/e742c652/tmp/capture.py` pattern
  (dtr/rts=False, ascii-safe printing); DIAG RMS telemetry already in the
  branch's WakeListener.
