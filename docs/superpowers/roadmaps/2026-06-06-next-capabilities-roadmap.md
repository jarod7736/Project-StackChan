# Stack-chan — Next Capabilities Roadmap

**Date:** 2026-06-06
**Status:** Roadmap (sequencing + decision-points only — each capability gets its own spec → plan → build cycle)
**Supersedes:** the Phase 3+ bullets in [`../specs/2026-06-02-stackchan-design.md`](../specs/2026-06-02-stackchan-design.md) §11

---

## Context

v1 is ~95% built (touch-to-talk → Whisper STT → Ollama/`oc-personal` chat → cloud TTS,
LVGL avatar face, 2-DOF servo motion, WiFi/web-UI/OTA). The owner wants to plan three
new capabilities, in stated priority:

1. **Face recognition / presence**
2. **Agent / MCP tools**
3. **Wake-word / hands-free**

This document sequences them, names what already exists (so we evolve rather than
rebuild), and flags the decisions each future spec must resolve. It deliberately stays
at roadmap altitude — no implementation detail.

### Planning assumption: the brownout is fixed

The device is currently dark for power debugging — every soak flag in `src/config.h` is set
(`STKCHAN_SOAK_MINIMAL`, `STKCHAN_BARE_WIFI`, `STKCHAN_BARE_AUDIO`, `STKCHAN_AUDIO_PLAYBACK`,
`STKCHAN_AUDIO_DECODE` = 1) while the AXP2101 brownout is bisected. **For this roadmap we
assume that work lands first**: the soak flags go back to 0 and the full app (FSM, face,
audio, servos, probe) runs with power margin. That removes power as the sequencing driver,
so the three capabilities order by **value + dependency**, matching the owner's stated
priority.

Power doesn't vanish from the picture, though — it drops from a *gate* to a *validation
item* on the one feature that adds steady-state load: wake-word's continuous mic (see
Track C). Everything else is server-side or a light HTTP poll.

---

## What already exists (don't rebuild)

| Seam | Where | Implication |
|------|-------|-------------|
| **Agent backend** | `src/net/ChatClient.cpp:73-122` — `send(..., brainMode)` routes to a second host (`brain_host`) as a one-shot to model `oc-personal` with `Bearer brain_key`; the agent owns its own tools + context server-side. | Agent/MCP is **evolution, not a graft**. The spec's risk R2 ("gemma3n lacks tool support blocks MCP") is **moot** — tools never run on the casual model. |
| **Event injection into the FSM** | `src/app/ControlBridge.{h,cpp}` — thread-safe queue: `pushExpression / pushServo / pushVolume / pushSay`, drained on the main loop. `SAY` → `requestExternalSpeak()` (IDLE-guarded). | Presence proactive actions ride ControlBridge. **Bug to fix:** `EXPRESSION`/`SERVO` dispatch *unconditionally* (`ControlBridge.cpp:68-80`) — a presence cue mid-turn would stomp the active face. Gate non-speech pushes on `currentState()==IDLE`. |
| **Background work during long waits** | `state_machine.cpp:67` chat task pinned to core 0; idle motion kept alive during the wait; one "checking…"/doubt cue for brain routes (`:195-198`). | Agent latency UX is *partially built* — the gap is *progressive* feedback over 5–30 s, an enhancement not a from-scratch build. |
| **HTTP poll pattern** | `src/net/ConnectivityTier.cpp:17-25` — simple time-gated 30 s probe, IDLE-only. | Replicate this 3-line pattern for the presence poll; **don't** abstract a shared poller (gating differs per consumer — low value). |

**NVS note:** the 15-char limit is per-key, not a key-count cap. `vision_url`, `owner_name`,
`wake_word`, `kws_model`, `wake_thresh` all fit. Note it; it won't shape design.

---

## The roadmap

Three features, ordered by the owner's priority (power no longer reorders them). They are
mutually independent — no feature blocks another — so the sequence is about focus, not hard
dependency. Each large feature splits into a **server-side** piece (startable independently)
and a **device-side** piece.

```
Phase 1  Face recognition / presence
           1a  stackchan-vision service on lobsterboy   (server; the long pole)
           1b  VisionClient + PresenceManager            (device; needs 1a)

Phase 2  Agent / MCP tools
           2a  confirm oc-personal session capability    (server question)
           2b  routing + multi-turn + latency UX         (device; evolves the seam)

Phase 3  Wake-word / hands-free
           3a  hardware-fork decision                    (on-chip KWS vs LLM Module)
           3b  continuous-capture path + onWakeDetected  (device; validate power load)
```

### Phase 1 — Face recognition / presence (owner's #1)

**1a — `stackchan-vision` service (server, the long pole).** Pure work on `lobsterboy`; no
device dependency. This is the bulk of the feature and can start independently of everything
else.
- Build/deploy the Python systemd unit (Appendix A): `GET /presence` →
  `{seen, confidence, since_ms}`, `GET /healthz`. Camera = USB webcam or RTSP/MJPEG URL
  (server-side — no on-device camera, no second device).
- Enrollment flow for the owner's face; privacy posture per Appendix A (labels + confidence
  only off-box; no frames; snapshot off by default — already sound, keep it).
- **Decision-points:** recognizer (`face_recognition`/dlib vs InsightFace if accuracy
  disappoints); camera source (USB vs IP cam).

**1b — VisionClient + PresenceManager (device, needs 1a).**
- `net/VisionClient`: IDLE-only poll of `/presence`, short-circuit when `tier != LAN_OK`
  (copy the `ConnectivityTier` poll pattern).
- `PresenceManager`: debounce/hysteresis (arrived ≥3 s / left ≥30 s / unknown ≥10 s) +
  greeting-suppression (<10 min); proactive cues via ControlBridge **with the IDLE-gating
  fix** (greet on arrival, sleepy on departure, tilt at unknown).
- **Decision — poll interval:** Appendix A's 2 s is 15× the 30 s tier probe. Mostly a
  network-cost choice now that power has margin; 5–10 s (or piggyback on the tier probe) is
  still likely plenty for presence and cheaper.
- **Decision:** where debounce + greeting-suppression state lives (net-new, no home today).

### Phase 2 — Agent / MCP tools

Evolve the existing `brainMode` seam into the real assistant path (it already routes to
`oc-personal` with server-side tools — this is evolution, not a graft).
- **2a — server question:** brain mode is one-shot *by design* (`ChatClient.h:24-26`). True
  multi-turn needs a session id passed to `oc-personal`. **Confirm `oc-personal` supports
  sessions before designing the device side.**
- **2b — Decision, routing:** today `transcriptWantsBrain` (`state_machine.cpp:41`) is
  brittle case-insensitive substring matching. Options: expand keywords / a classifier
  round-trip (adds latency *before* chat) / have the casual model emit a route tag. Flag the
  latency cost of each.
- **2b — Enhancement, latency UX:** progressive feedback over 5–30 s agent/tool waits (build
  on the existing core-0 chat task + idle-motion-during-wait).

### Phase 3 — Wake-word / hands-free

The only feature adding **steady-state device load** (continuous mic). With the brownout
assumed fixed this is no longer *gated* on power, but the continuous-mic draw must be
**validated against the repaired power budget** before committing to the on-chip path.
- **3a — hardware fork (the major branch):** on-CoreS3 KWS (ESP-SR / microWakeWord — RAM/CPU
  budget + continuous-mic current) vs. the ~$50 M5 LLM Module. Note: spec §4 *excludes*
  Port C UART / M5Bus, so the LLM Module is a **BOM + hardware-policy change**, not a
  drop-in accessory.
- **3b — Design problem, capture coexistence:** spec §6 invariant 3 ("TTS and mic never
  overlap") + `MicRecorder` is press-and-hold only. Continuous KWS needs a *separate*
  always-on capture path that coexists with the press-and-hold recorder and yields the I²S
  mic during TTS.
- **3b — New FSM edge:** IDLE→LISTENING via a new `onWakeDetected()` trigger (analogous to
  `onPressDown()`). This is the one genuinely new FSM entry point — ControlBridge can't
  express "start a turn."

---

## Synergies (note, don't scope here)

- **Face recognition × proactive behaviors:** `onOwnerArrived` greeting is the first
  proactive push; a later MQTT/Home-Assistant notifier (spec §11) reuses the same
  ControlBridge `SAY` path.
- **Agent × everything:** once tools work, presence ("who's here?") and proactive
  reminders can be agent-driven rather than hard-coded.

## Corrections to prior framing (captured so they aren't re-litigated)

1. Power is **assumed fixed** for this roadmap, so it no longer reorders the features. The
   one residual power concern is wake-word's continuous mic, demoted from a gate to a
   validation item (Phase 3).
2. "Face recognition has near-zero device power impact" → **overstated even so.** A 2 s poll
   is 15× the existing probe rate; with margin restored it's a network-cost choice, not a
   blocker.
3. "Build a shared event-injection path" → **already exists** (`ControlBridge`); presence
   reuses it, only wake-word needs a small new trigger.
4. Spec §11 / R2 MCP framing is **stale** — the `brainMode`/`oc-personal` seam supersedes it.

## Critical files (for the future per-feature specs)

- `src/config.h` — soak flags (Phase 0 gate), NVS keys, brain seam config, `kErr*` taxonomy
- `src/state_machine.cpp` — FSM, `transcriptWantsBrain` routing, core-0 chat task,
  `requestExternalSpeak`, IDLE-gating point, future `onWakeDetected()`
- `src/app/ControlBridge.{h,cpp}` — shared event queue (presence reuses; EXPRESSION/SERVO
  stomping fix lives here)
- `src/net/ChatClient.cpp` — `brainMode`/`oc-personal` seam (agent evolution + multi-turn)
- `src/net/ConnectivityTier.cpp` — poll pattern to replicate for `VisionClient`

## Next step

Each capability proceeds through its own brainstorm → spec (`docs/superpowers/specs/`) →
plan (`docs/superpowers/plans/`) cycle. Recommended order to *spec next*:
**Phase 1 — Face recognition**, starting with the `stackchan-vision` service (1a), the
long pole and the owner's top priority.
