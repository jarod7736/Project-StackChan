# Wake on keyword ("hey Stack-chan") — design

**Date:** 2026-06-11
**Status:** Approved (brainstorm decisions confirmed by owner)
**Origin:** Task 2 of `docs/superpowers/2026-06-10-eod-handoff.md`; roadmap Phase 3
(`docs/superpowers/roadmaps/2026-06-06-next-capabilities-roadmap.md` §Phase 3).

## Problem

Starting a voice turn requires press-and-hold. The robot should also wake on a spoken
"hey Stack-chan" while idle — hands-free, with the words after the wake phrase usable as
the request itself.

## Decisions (confirmed in brainstorm)

1. **Approach: VAD-then-confirm** (roadmap option b). Continuous on-device energy VAD; on
   speech, record a short window, transcribe via the existing cloud STT, fuzzy-match the
   transcript prefix against the wake phrase. Rejected for now: ESP-SR WakeNet (needs the
   arduino-esp32 3.x platform upgrade AND custom wake words are a paid Espressif service);
   on-chip tflite KWS (30–70 KB tensor arena vs ~19 KB observed min internal heap, plus a
   model-training pipeline — revisit later; the VAD trigger is designed to be swappable
   for it); M5 LLM Module (hardware/BOM + Port-C policy change).
2. **Privacy/cost: acceptable.** Speech-like room sound regularly hits the Whisper API as
   short snippets and is discarded on non-match. Revisit when local STT (Halo box) lands.
3. **One-breath UX:** "hey Stack-chan, what's the weather?" → the remainder after the wake
   phrase IS the request (straight to chat). "hey Stack-chan" alone → instant offline ack
   clip ("Yes?") → normal listening window.
4. **Listen gate: whenever idle.** Not presence-gated (presence read "absent" with the
   owner at the desk on 2026-06-11 — camera-gating would make wake unreliable). Always
   paused during voice turns, TTS playback, press-and-hold recording, and OTA.

## 1. WakeListener (`src/app/WakeListener.{h,cpp}`)

A second, always-on capture path — deliberately separate from press-and-hold
`MicRecorder` (spec §6 invariant 3 and the roadmap's 3b "capture coexistence" both stand):

- Own PSRAM capture buffer: 3.5 s × 16 kHz × int16 ≈ 112 KB (PSRAM is plentiful;
  internal heap untouched). Uses `M5.Mic` at the existing `kRecordSampleRate` (16 kHz).
- **Energy VAD** over 30 ms RMS frames against an adaptive noise floor (slow EMA updated
  only on quiet frames). Trip = N consecutive frames above `floor × kWakeVadRatio`.
  After trip: record until ~600 ms of trailing silence or the 3.5 s cap.
- On window close: write a WAV header — extract `MicRecorder`'s 44-byte writer into a
  shared header-only helper `src/hal/WavHeader.h` used by both — then call
  `stt.transcribe()` (blocking, like every HTTP call in this codebase) and evaluate the
  match (§3).
- **Non-match → discard + 2 s cooldown** before the VAD can trip again (caps stray STT
  calls in continuous noise).
- **Pure core for native tests:** the VAD framing/trip/close logic lives in
  `WakeVad.{h,cpp}` (no Arduino deps) — feed synthetic RMS sequences under
  `pio test -e native`, the `McpProtocol` pattern. `WakeListener` is the thin device
  shell (mic, PSRAM, STT call).

## 2. Mic/speaker handoff — centralized pause/resume

Mic and speaker are mutually exclusive (M5Unified; see `MicRecorder::stop()` /
`AudioPlayer::reapplySpeakerConfig()` — wrong handoff = 44.1 kHz static).

- `WakeListener::pause()` — idempotent; if capturing: `M5.Mic.end()` +
  `audio.reapplySpeakerConfig()`. Called at the **three places audio or the mic can
  start**:
  1. `requestExternalSpeak()` (greetings, low-batt, MCP `say`) — before the FSM
     transitions to SPEAKING_TTS;
  2. the LISTENING entry (button press path), before `mic.start()`;
  3. `enterError()`, before `tts.synth()`.
- `WakeListener::tick()` (main loop) resumes capture only when ALL hold:
  `currentState() == IDLE`, `!audio.isPlaying()`, `!mic.isActive()`,
  `!OtaService::isActive()`, `wake_en`, and ≥ ~700 ms since the last track ended
  (anti-self-trigger guard on the robot's own voice tail).

## 3. Wake matching (`src/app/WakeMatch.{h,cpp}`, pure)

- Normalize: lowercase, strip everything but [a-z0-9] → "Hey, Stack Chan! what's up"
  → "heystackchanwhatsup".
- Match: edit distance ≤ 1 between the normalized wake phrase and the transcript's
  normalized prefix of the same length (tightened from ≤2 during implementation: all real Whisper variants normalize to distance 0–1, while ≤2 admitted false-accepts like 'haystack chair') (absorbs Whisper variants: "hey stackchan",
  "hey stack chan", "hey, Stack-chan").
- Returns `{matched, remainder}` where remainder is the original (un-normalized)
  transcript text after the matched prefix, trimmed.
- Config: NVS `wake_word` (default `"hey stack chan"`), NVS `wake_en` (default on).
  No portal UI this round; keys settable via the provisioning script.

## 4. FSM integration

One new entry point, mirroring `onPressDown()`: `onWakeDetected(const String& remainder)`
— valid only in IDLE (ignored otherwise).

- **Remainder non-empty:** set `g_transcript = remainder`, transition directly to
  THINKING_CHAT (skipping LISTENING/THINKING_STT — the transcription already happened).
  Face/motion cues identical to the post-STT path.
- **Remainder empty:** speak `kWakeAck` ("Yes?") — a new fixed phrase in `src/config.h`
  riding the **stock-clips pipeline** (extend `tools/render_speeches.py`'s config.h
  filter to include `kWakeAck`, re-render once → instant offline ack) — then enter
  LISTENING. The existing LISTENING stops on press-release, which a wake-opened window
  doesn't have: the wake path stops recording via **WakeVad end-of-speech detection
  (~800 ms trailing silence) with a 6 s hard cap**, then proceeds to THINKING_STT
  normally. A press during a wake-opened window is ignored.
- Press-and-hold continues to work unchanged.

## 5. Error handling

| Failure | Behavior |
|---|---|
| STT fails / offline | discard window, cooldown, keep listening (NO error speech — wake attempts must be silent failures) |
| Transcript empty / no match | discard + cooldown |
| Mic start fails | log, retry next tick |
| FSM busy when match lands | drop the wake (user can re-say it) |

## 6. Validation

- **Native:** WakeVad trip/close sequences; WakeMatch normalize/fuzzy/remainder vectors
  (incl. Whisper-style punctuation variants).
- **Power:** 30-min battery-only soak with wake listening active; compare PWR serial
  telemetry (vbat slope, heap) against baseline. (Roadmap: power is a validation item,
  not a gate.)
- **Cost/noise:** observe STT call rate over an evening of normal room noise; tune
  `kWakeVadRatio`/cooldown if chatty.
- **Live:** wake+question one-breath; wake-alone → ack clip → question; press-and-hold
  regression; greeting interplay (presence greet must cleanly pause/resume listening);
  robot must not wake on its own TTS.

## Out of scope (recorded)

- On-chip KWS (swap-in path for the VAD trigger later); ESP-SR platform upgrade.
- Portal UI for wake settings; presence-gated listening; barge-in during TTS.
- Echo cancellation; multi-wake-phrase support.
