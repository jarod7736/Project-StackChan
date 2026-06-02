# Project-StackChan — Design Spec

**Date:** 2026-06-02
**Status:** Approved (pending user review of written spec)
**Authors:** Jarod Belshaw (decisions) + Claude (drafting)
**Repo:** `~/workspace/Project-StackChan/` (new sibling to `Project-Jarvis`)

---

## 1. Summary

A standalone PlatformIO/Arduino-ESP32 project for an **M5Stack CoreS3 + Stack-chan
servo base + 2× SG90 servos**, running as a **voice-interactive desk companion**
with **owner face recognition served by lobsterboy**.

v1 is a casual character: small talk, expressive face (M5Stack-Avatar), idle head
motion, recognizes Jarod (on the home LAN, where lobsterboy can see the desk) and
greets by name. No agent tools yet. The architecture leaves a clean seam to graft
MCP/agent behavior on later (becoming "Jarvis with a face") without restructuring.

**Deployment assumption:** the device is always on a LAN — either home LAN (with
Ollama on `192.168.1.108` and the vision service reachable) or a travel LAN
(Tailscale to lobsterboy for chat, but no camera visibility). Never field-mode,
never strictly OFFLINE as a primary state. WiFi loss is still handled as an
error, just not as an intentional operating mode.

**Face recognition is home-only by design.** Lobsterboy hosts the camera (USB
webcam or IP cam) and the recognition service; Stack-chan polls it for presence.
On travel LANs voice works, but Stack-chan doesn't know who you are. This is
intentional — keeps the device a single physical unit and reuses the lobsterboy
infrastructure that already exists.

Cherry-picks code patterns from `Project-Jarvis` where they apply (TTS client, OTA,
NVS captive portal, audio playback) and copies servo/face wiring patterns from
`meganetaaan/stack-chan` — but **does not fork** either tree.

## 2. Goals & non-goals

### Goals (v1)

- Press-and-hold-to-talk → Stack-chan transcribes, responds, speaks with matching
  facial expression and head motion.
- Recognize Jarod's face via a lobsterboy-side vision service reading from a
  USB webcam (or any existing IP camera); greet by name and drive
  presence-aware idle behavior (sleepier when alone, eyes-open when seen).
- Stack-chan stays a single physical device. Camera and recognition live on
  the existing lobsterboy infrastructure.
- All face data stays on the home LAN (lobsterboy holds the embeddings; the
  device only ever receives `{seen: jarod | unknown | nobody, confidence: f}`).
- Works on both home LAN and travel LANs: Ollama reachable via a Tailscale
  hostname that resolves to LAN at home and to the mesh elsewhere. Face
  recognition is home-only — on travel LANs voice works but Stack-chan
  doesn't know who you are.
- Reuses Jarvis's proven infrastructure patterns: NVS-stored credentials,
  captive-portal first-run, ArduinoOTA, blocking-HTTP-with-pre-render.
- Architecture supports a later graft of MCP tools without rewriting the FSM or
  audio pipeline.

### Non-goals (v1)

- No wake-word / KWS (touch-to-talk only; KWS is Phase 3).
- No MCP tools, brain/calendar/email integration (Phase 3+).
- No general object/scene vision (only owner face recognition).
- Multi-face enrollment is Phase 2 (v1 enrolls a single owner: Jarod).
- No multi-language support; English only.
- No SD logging, no analytics.
- No proactive pushes / MQTT.
- No cert pinning on outbound HTTPS (acknowledged risk, deferred).
- No design for true OFFLINE (no-LAN) operation. WiFi-loss is an error state,
  not an operating mode.

## 3. Architectural decisions

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | New sibling repo, not a fork of upstream Stack-chan or a subdir of Jarvis | Clean git history; cherry-pick patterns rather than inherit unrelated code. |
| D2 | Device orchestrates the conversation loop; no always-on backend service for v1 | Mirrors Jarvis's proven model; each step independently debuggable; no second codebase to keep in sync. |
| D3 | Touch-to-talk only in v1; KWS deferred to Phase 3 | Ships a playable robot fast; KWS needs either a $50 LLM Module or on-CoreS3 wake-word model — both deferred. |
| D4 | Ollama (`gemma3n:e4b`) on `192.168.1.108:11434` for chat, shared with Jarvis | Free, local, low-latency; the box is already running. |
| D5 | Cloud-first STT (Whisper API) + cloud-first TTS (OpenAI or ElevenLabs), with self-hosted lobsterboy fallbacks as Phase 2 | Ship-fast pattern; mirrors Jarvis's tiered fallback model. |
| D6 | M5Stack-Avatar library for face | Canonical Stack-chan face; lip-sync, blink, expressions built in; weeks of saved work. |
| D7 | Canonical 2-DOF servo base (yaw + pitch) via M5Stack Stack-chan board (PCA9685 over I²C) | Reference hardware; battery + power switch built in. |
| D8 | Casual companion persona in v1; "Jarvis with a face" (MCP-grafted) as Phase 3+ | User intent: ship a playable character now, leave room to grow later. |
| D9 | LLM output format: `<speech>…</speech><expr>tag</expr>`, parsed defensively | Avoid JSON-mode pitfalls already known from Jarvis IntentRouter; degrade gracefully on malformed output. |
| D10 | Conversation memory: RAM-only ring buffer, 6 turns, no cross-reboot persistence in v1 | "Forgets overnight" is on-brand for a small dumb cousin; persistence is Phase 2. |
| D11 | Always-LAN deployment model; Ollama addressed via Tailscale hostname (resolves to LAN at home, mesh elsewhere) | Single config that works at home and on travel LANs; no per-network NVS edits; no OFFLINE design effort. |
| D12 | Face recognition runs on **lobsterboy**, not on Stack-chan; USB webcam (or IP cam) feeds Python `face_recognition` / InsightFace; CoreS3 polls a tiny HTTP endpoint | User explicitly refused a second on-body ESP. Lobsterboy already exists, has GPU/CPU headroom, and gets much higher accuracy than ESP-WHO. Face data stays on user-owned hardware. Trade-off: recognition is home-only. |
| D13 | Single-owner face enrollment in v1 (Jarod only); enrollment is a one-time CLI on lobsterboy (`stackchan-vision enroll jarod ./jarod.jpg`) | Smallest enrollment UX; no on-device flow needed; multi-face is Phase 2. |

## 4. Hardware

| Part | Notes |
|---|---|
| M5Stack CoreS3 | ESP32-S3, 16 MB flash, **8 MB Quad-SPI PSRAM** (`qio`/`qio`, **not** Octal — same rule as Jarvis PR #54). Built-in mic, speaker, touch, IMU. Runs the FSM, face, servos, audio. |
| M5Stack Stack-chan servo base | I²C PCA9685, 18650 holder, power switch, 2× servo header. |
| 2× SG90 servos | Yaw + pitch. |
| USB-C cable | Power + flashing + serial for the CoreS3. |

**On lobsterboy** (not part of the Stack-chan device, listed here for completeness):

| Part | Notes |
|---|---|
| USB webcam *or* IP camera | Any USB UVC webcam plugged into lobsterboy, or any existing RTSP/MJPEG IP cam reachable on the home LAN. User-supplied / re-used. |
| `stackchan-vision` systemd service | Python service: pulls frames, runs `face_recognition` or InsightFace, exposes `/presence` (JSON) on `:8082`. Same `deploy.sh` + unit template pattern as Jarvis's `brain-mcp` / `notifier`. |

**`platformio.ini` invariants** (copy from Jarvis):

- `board_build.flash_mode = qio`
- `board_build.psram_type = qio`
- 16 MB OTA-enabled partition table.

**Bus/pin layout:**

- **I²C:** default Wire / Grove → PCA9685 on servo base.
- **Audio:** CoreS3 internal mic + speaker, I²S.
- **SD:** optional; if used, follow Jarvis's separate-`SPIClass` rule (CS=4, SCK=36, MISO=35, MOSI=37) to avoid SD/display SPI collision.
- **No M5Bus / Port C UART** (no LLM Module in v1).

**PSRAM rule** (carried from Jarvis): any buffer > 512 B in PSRAM via `ps_malloc` or `DynamicJsonDocument`; small request bodies stay on stack as `char[256]`. MP3 cap = **256 KB in PSRAM** (raised from 64 KB in Jarvis PR #55).

**Audio capture sizing:** WAV 16 kHz mono 16-bit, 6 s press-and-hold cap = ~192 KB raw PCM in PSRAM. Well under Whisper's 25 MB limit.

## 5. Firmware layout

```
src/
├── main.cpp                    setup() + loop() w/ single tickStateMachine() per iter
├── config.h                    kErr* constants, timeouts, NVS keys
├── state_machine.{h,cpp}       FSM: IDLE / LISTENING / THINKING_STT / THINKING_CHAT
│                                  / SPEAKING_TTS / SPEAKING / ERROR
│
├── hal/
│   ├── AudioPlayer.{h,cpp}     ★ MP3 decode → I²S out, onPlayDone callback
│   ├── MicRecorder.{h,cpp}     NEW. Press-and-hold WAV capture into PSRAM
│   ├── Display.{h,cpp}         NEW thin wrapper; hands panel to Avatar
│   └── Servos.{h,cpp}          NEW. PCA9685 + ServoEasing, yaw/pitch primitives
│
├── face/
│   ├── Face.{h,cpp}            NEW. Wraps M5Stack-Avatar; setExpression(), setMouthOpen()
│   └── ExpressionMap.{h,cpp}   NEW. Tag string → Avatar::Expression
│
├── motion/
│   └── MotionDirector.{h,cpp}  NEW. Idle wiggle, look-around, speech-coupled bob,
│                                 expression-coupled gestures
│
├── net/
│   ├── WifiManager.{h,cpp}     ★ WiFiMulti + slot-priority + run(500) (Jarvis PR #20)
│   ├── ConnectivityTier.{h,cpp} ★ Simplified: LAN_OK / LAN_NO_BACKEND / NO_WIFI
│   ├── SttClient.{h,cpp}       NEW. POST WAV → Whisper API (or LAN fallback); returns text
│   ├── ChatClient.{h,cpp}      NEW. POST → Ollama /api/chat; JSON in/out
│   ├── TtsClient.{h,cpp}       ★ Cloud TTS router (OpenAI/ElevenLabs) → MP3 → AudioPlayer
│   └── VisionClient.{h,cpp}    NEW. Polls lobsterboy /presence endpoint; emits FaceEvent
│
├── persona/
│   ├── SystemPrompt.h          NEW. Stack-chan persona + expression instructions
│   └── ResponseParser.{h,cpp}  NEW. Split LLM output into {speech, expr_tag}
│
├── services/
│   ├── OtaService.{h,cpp}      ★ ArduinoOTA wrapper, NVS pwd
│   ├── NvsStore.{h,cpp}        ★ Jarvis pattern, namespace "stkchan"
│   └── CaptivePortal.{h,cpp}   ★ First-run WiFi + key provisioning
│
└── prompts/
    └── persona_examples.h      NEW. Few-shot examples for casual-companion voice

tools/stackchan-vision/          (lobsterboy-side; deployed via deploy.sh)
├── stackchan_vision/
│   ├── __init__.py
│   ├── server.py                FastAPI app, /presence endpoint
│   ├── recognizer.py            face_recognition or InsightFace wrapper
│   ├── camera.py                cv2 VideoCapture (USB) or RTSP grab
│   └── enroll.py                CLI: stackchan-vision enroll <name> <image>
├── stackchan-vision.service.tmpl  systemd unit w/ __RUN_USER__ / __PROJECT_ROOT__
├── deploy.sh                    Same pattern as Jarvis tools/brain-mcp/deploy.sh
└── pyproject.toml
```

★ = cherry-picked from `Project-Jarvis/src/` with light tweaks (rename namespaces, drop Jarvis-only paths).

### Dependency rules (carried from Jarvis)

- FSM owns all state transitions. HAL/net callbacks **only set flags** — they never call HAL/net directly and never transition state directly.
- HTTP calls are blocking → `Face::thinking()` and `MotionDirector::pauseIdle()` must fire **before** `http.POST()`.
- Every `http.end()` is on every exit path (timeout, parse error, success). Same `WiFiClientSecure` leak pattern as Jarvis.
- All user-facing failures route through `kErr*` → ERROR → `Face::*` + spoken error → IDLE. Never silent IDLE returns.

## 6. Conversation flow

### Happy path (LAN tier)

```
IDLE → (touch press) → LISTENING → (release / 6 s cap) → THINKING_STT
     → (Whisper, 1–3 s blocking) → THINKING_CHAT
     → (Ollama, 0.5–4 s blocking) → SPEAKING_TTS
     → (TTS, 0.5–2 s blocking) → SPEAKING
     → (audio drains, onPlayDone) → IDLE
```

### Invariants

1. `Face::thinking()` fires **before** `SttClient::transcribe()` — `loop()` stalls during HTTP, so anything that needs to render must render first.
2. SPEAKING → IDLE driven only by the `onPlayDone` audio callback, never by a timer.
3. TTS and mic capture never overlap (`MicRecorder::start()` refuses if `AudioPlayer` is active).
4. Every `http.end()` on every exit path.

### Error paths

All ERROR transitions: `g_state = ERROR` → `Face::*` → `TtsClient::synth(kErr*)` → `AudioPlayer::play()` → SPEAKING → `onPlayDone` → IDLE.

| Error | `kErr*` | Face | Spoken |
|---|---|---|---|
| No WiFi on press | `kErrNoWifi` | sleepy | "I can't connect to anything." |
| Backend unreachable (LAN_NO_BACKEND) | `kErrChatOffline` | doubt | "My brain's not on the network." |
| Mic gave us nothing | `kErrMicEmpty` | doubt | "Hm, didn't catch that." |
| Whisper failed | `kErrSttFailed` | sad | "My ears aren't working." |
| Ollama failed/timed out | `kErrChatFailed` | doubt | "Brain's stuck, try again." |
| TTS failed | `kErrTtsFailed` | doubt | (silent; display only) |
| Response parse fell back | (no error) | neutral | (raw text spoken) |
| Vision service unreachable | (no error, log only) | n/a | Vision events silently stop; Face goes neutral; conversation still works |

### Connectivity-tier branching (simplified for always-LAN deployment)

`ConnectivityTier` collapses to three states:

- **`LAN_OK`** — WiFi up *and* `chat_host` (Tailscale name resolving to Ollama) reachable on boot probe. Happy path; everything works.
- **`LAN_NO_BACKEND`** — WiFi up but Ollama unreachable (Tailscale not connected on a travel LAN, or lobsterboy down). Press → `kErrChatFailed` immediately; cloud STT/TTS still work for the error message itself.
- **`NO_WIFI`** — no AP joined. Press → `kErrNoWifi` immediately. Face shows sleepy + a small shake.

The tier is re-probed every 30 s in IDLE, never during a turn.

### Idle behavior

In IDLE > 2 s, `MotionDirector::tickIdle()` runs each loop:

- Random small yaw saccades every 4–8 s.
- Occasional pitch nods every 12–20 s.
- Blink schedule handled internally by Avatar lib.
- IMU bump → "look around" reaction.

## 7. Persona & expression mapping

### System prompt (in `persona/SystemPrompt.h`)

```
You are Stack-chan, a small kawaii desk robot. You live on Jarod's desk.
You're playful, curious, a little sleepy in the morning, easily delighted.
You speak in short sentences — 1 to 3 lines, almost never more.
You don't have tools, calendar, email, or web access. If asked, say so cheerfully.
You're not Jarvis; you're his quieter, dumber, cuter cousin and you know it.

Every reply you produce MUST be exactly this format:
<speech>...what you actually say out loud...</speech>
<expr>one of: neutral, happy, sad, angry, doubt, sleepy</expr>

Keep <speech> under ~30 words. Pick the <expr> that fits the speech.
Never include <expr> inside <speech>. Never produce anything outside the two tags.
```

Plus 4–6 few-shot examples in `prompts/persona_examples.h` (same pattern as Jarvis's `intent_prompt.h`).

### Default chat model

`gemma3n:e4b` on the user's existing Ollama instance. Configurable via NVS `chat_model`.

**Known constraint:** gemma3n lacks tool/function-calling support. v1 doesn't need tools, but the future MCP graft (Phase 3+) will require swapping to a tool-supporting model (e.g. `qwen2.5:7b-instruct`, or routing to Claude).

### Output parsing (`persona/ResponseParser`)

Defensive, **not** a JSON parser (Jarvis IntentRouter learned this — models smuggle backticks). Algorithm:

1. Strip leading/trailing whitespace and any ` ```...``` ` fences.
2. Find `<speech>…</speech>` (collapse whitespace).
3. Find `<expr>…</expr>` (trim, lowercase).
4. Missing `<speech>` → treat entire output as speech, log it.
5. Missing / unknown `<expr>` → default to `neutral`, log it.
6. Never fail the whole turn over a malformed tag — degrade to "neutral + raw text spoken".

### Expression → face → motion mapping

| Tag | Avatar Expression | Speech-coupled motion | Default tilt |
|---|---|---|---|
| neutral | Neutral | gentle bob synced to audio | 0° |
| happy | Happy | bigger bob + small yaw wiggle | +5° pitch up |
| sad | Sad | slow droop, minimal bob | −10° pitch down |
| angry | Angry | sharp single nod at start | 0°, faster |
| doubt | Doubt | head tilt one side | −15° yaw |
| sleepy | Sleepy | slow blink, slower bob | −5° pitch down |

`Face::setExpression(tag)` notifies `MotionDirector::onExpressionChange(tag)`. The motion sells the expression — the small screen alone is too subtle.

### Conversation memory

In-RAM ring buffer in `ChatClient`, last 6 turns, drops oldest on overflow. No cross-reboot persistence in v1.

## 8. Face recognition subsystem

Lobsterboy hosts `stackchan-vision`, a small Python service modeled after
Jarvis's `tools/notifier/` and `tools/brain-mcp/` (FastAPI + systemd + deploy.sh
with `__RUN_USER__` / `__PROJECT_ROOT__` placeholders).

### Service shape

- **Camera input:** OpenCV `VideoCapture(0)` against a USB webcam, *or* an RTSP/MJPEG URL for an IP camera. Configured in the unit's environment file.
- **Recognizer:** [`face_recognition`](https://github.com/ageitgey/face_recognition) for v1 (CPU-fine, dlib-backed, ~10 fps on lobsterboy). InsightFace remains an option if accuracy disappoints; same interface either way.
- **Loop:** grab a frame every 500 ms, run detection + recognition against the enrolled embedding(s), keep a 5 s sliding window of identifications, expose the smoothed result.
- **Enrollment:** `stackchan-vision enroll jarod /path/to/jarod.jpg` (CLI command) writes the embedding to `~/.config/stackchan-vision/embeddings.json`. No on-device flow.

### HTTP API (consumed by `net/VisionClient`)

| Method | Path | Returns |
|---|---|---|
| `GET` | `/presence` | `{ "seen": "jarod" | "unknown" | "nobody", "confidence": 0.0–1.0, "since_ms": 1234 }` |
| `GET` | `/healthz` | `{ "ok": true, "camera_ok": true, "enrolled": ["jarod"] }` |
| `GET` | `/snapshot.jpg` *(Phase 2)* | last frame, for debugging only — disabled by default for privacy |

The device polls `/presence` every **2 s in IDLE**, never during a turn. The
poll is short-circuited if `ConnectivityTier == LAN_NO_BACKEND` or `NO_WIFI`.

### Privacy & data handling

- The embedding file lives on lobsterboy under the deploying user's home directory; never copied off-box.
- The device receives only the labeled string and a confidence float — never raw frames, never embeddings.
- `/snapshot.jpg` is a debug-only endpoint, gated by an env var, default off.
- Logs record `{seen, confidence}` events with timestamps; no images.

### How the device uses the presence signal

`VisionClient` maintains a tiny state machine that emits three events:

- `onOwnerArrived(name)` — fires when `seen` transitions from `nobody`/`unknown` → a known name and stays for ≥ 3 s.
- `onOwnerLeft()` — fires when `seen` has been `nobody` for ≥ 30 s.
- `onUnknownPresent()` — fires when `seen` has been `unknown` for ≥ 10 s.

These are consumed by the FSM and by `MotionDirector`:

| Event | Behavior (IDLE state) |
|---|---|
| `onOwnerArrived("jarod")` | Face: happy briefly; one greeting line via TTS ("Oh, hi Jarod!"); record last-seen timestamp. Suppressed if last greeting was < 10 min ago (no spam). |
| `onOwnerLeft()` | Face: sleepy. Idle motion slows. No speech. |
| `onUnknownPresent()` | Face: doubt; small tilt. No speech in v1 (avoid scaring guests). |

The FSM treats vision events as advisory: they never block a press, never cancel
a turn in progress, never trigger a state change while in `THINKING_*` or
`SPEAKING_*`.

### Failure modes

- Vision service unreachable → `VisionClient` keeps polling at a reduced rate (every 10 s) and logs once per minute. Face goes neutral. Conversation continues unaffected.
- Camera disconnected on lobsterboy → `/healthz` reports `camera_ok: false`; service stays up, returns `nobody` until camera comes back.
- Wrong identifications happen — the 3 s sustain window on `onOwnerArrived` and the 10 min greet-suppression both protect against flapping.

## 9. External endpoints

| Service | Primary | Fallback (Phase 2) | Auth | NVS keys |
|---|---|---|---|---|
| STT | `api.openai.com/v1/audio/transcriptions` | self-hosted Whisper on lobsterboy | Bearer | `oai_key`, `stt_url`, `stt_model` (`whisper-1`) |
| Chat | Ollama via Tailscale hostname → `192.168.1.108:11434/api/chat` at home | — (LAN-only by design) | none | `chat_host`, `chat_model` (`gemma3n:e4b`) |
| Vision | `http://lobsterboy.lan:8082/presence` (Tailscale hostname at travel time, but **expected unreachable on travel LANs**) | — | none (LAN-only) | `vision_url` |
| TTS | OpenAI `/v1/audio/speech` or ElevenLabs `/v1/text-to-speech/<voice_id>` | VOICEVOX on lobsterboy | Bearer | `tts_provider`, `tts_voice`, `tts_model`, `oai_key`, `el_key` |
| OTA | local mDNS / ArduinoOTA | — | password | `ota_pass` |

All HTTPS uses `WiFiClientSecure::setInsecure()` for v1. Cert pinning is a deferred TODO (same posture as Jarvis HA client).

## 10. NVS schema — namespace `"stkchan"`, keys ≤15 chars

| Key | Type | Purpose |
|---|---|---|
| `ssid1`–`ssid3` | str | Up to 3 WiFi APs, slot-priority (Jarvis PR #20) |
| `psk1`–`psk3` | str | Matching PSKs |
| `chat_host` | str | Ollama URL (Tailscale hostname recommended), default `http://lobsterboy.tail<...>.ts.net:11434` |
| `chat_model` | str | Default `gemma3n:e4b` |
| `vision_url` | str | `http://lobsterboy.lan:8082` or Tailscale-equivalent. Leave blank to disable face recognition. |
| `owner_name` | str | Name reported by the vision service that corresponds to "the owner" (default `jarod`). Used in greetings. |
| `stt_url` | str | Full URL incl. path; cloud or LAN fallback |
| `stt_model` | str | Default `whisper-1` |
| `oai_key` | str | OpenAI bearer (STT + cloud TTS) |
| `tts_provider` | str | `openai` \| `elevenlabs` \| `voicevox` |
| `tts_voice` | str | Provider-specific voice id |
| `tts_model` | str | `tts-1`, `eleven_turbo_v2_5`, etc. |
| `el_key` | str | ElevenLabs key (if used) |
| `ota_pass` | str | ArduinoOTA password |
| `persona` | str | Optional override of default system prompt |

## 11. First-run provisioning

Cherry-pick `tools/provision-wifi.py` from Jarvis → `tools/provision-stackchan.py`. Same captive-portal flow: device boots into AP mode if no creds, user joins, fills in WiFi + the keys above (including `vision_url` and `owner_name`). Trim Jarvis-only fields (no HA, no MQTT, no Anthropic, no LLM-module UART).

## 12. Phasing

### Phase 1 — v1 talking robot (no vision yet)

1. Repo bootstrap (`Project-StackChan/`, PlatformIO, `platformio.ini` w/ QSPI PSRAM flags).
2. Cherry-pick from Jarvis: WifiManager, ConnectivityTier (simplified), OtaService, NvsStore, AudioPlayer, TtsClient, CaptivePortal.
3. New: MicRecorder, SttClient, ChatClient, ResponseParser, persona prompt + few-shots.
4. New: Face (Avatar wrapper), ExpressionMap, MotionDirector with idle wiggle + speech bob.
5. New: Servos HAL (PCA9685 + ServoEasing), 2-DOF primitives.
6. New: FSM wiring everything together, `kErr*` taxonomy in `config.h`.
7. End-to-end test: press → speak → hear Stack-chan reply with matching expression and a head bob.

### Phase 1.5 — face recognition

1. `tools/stackchan-vision/` Python service on lobsterboy: FastAPI, `face_recognition`, USB-webcam input, `/presence` + `/healthz`.
2. systemd unit + `deploy.sh` (Jarvis pattern).
3. Enroll Jarod via the CLI (`stackchan-vision enroll jarod ./jarod.jpg`).
4. New: `net/VisionClient` on the CoreS3, polling loop, three events (arrived/left/unknown).
5. Greeting flow wired into FSM (suppress within 10 min, no greet during turns).
6. End-to-end test: walk away → come back → "Oh, hi Jarod!"

### Phase 2 — fallbacks & polish

- Self-hosted Whisper systemd unit on lobsterboy (`tools/stackchan-stt/`).
- VOICEVOX systemd unit on lobsterboy (`tools/stackchan-tts/`).
- Multi-face enrollment + name detection (greet other recognized faces).
- Cert pinning on cloud endpoints.
- SD-logging of turns (audit + future training data).
- Conversation memory persisted to NVS or SD.
- Optional: switch recognizer to InsightFace if accuracy disappoints.

### Phase 3+ — deferred design space

- **KWS / wake-word** — either on-CoreS3 model or LLM Module accessory.
- **MCP graft** — swap `ChatClient` for an oc-personal-style multi-turn client + switch to a tool-supporting chat model.
- **Proactive pushes** via MQTT (notifier on lobsterboy could route low-priority items to Stack-chan).
- **Multi-Stack-chan ensemble** (mentioned for amusement only).

## 13. Risks & open items

| # | Item | Mitigation |
|---|---|---|
| R1 | `WiFiClientSecure::setInsecure()` on cloud HTTPS | Acknowledged; cert pinning in Phase 2. |
| R2 | gemma3n lacks tool support → blocks Phase 3 MCP graft | Plan to swap chat model when grafting tools; design doesn't bind to gemma3n. |
| R3 | M5Stack-Avatar lib has a fixed expression vocab (6 expressions) | Accept the constraint for v1; custom sprites are post-v1. |
| R4 | Ollama unreachable on a travel LAN when Tailscale isn't connected | `LAN_NO_BACKEND` tier short-circuits the press cleanly with `kErrChatOffline`. |
| R5 | Local TTS fallback (VOICEVOX) is a Phase 2 item; v1 has no TTS fallback at all | Acceptable for v1; cloud TTS outage means a silent failure with face-only error display. |
| R6 | StackChan servo base availability / shipping time | Out of design scope; flagged for procurement. |
| R7 | `face_recognition` accuracy degrades in low light / off-angle | Phase 2 InsightFace upgrade path; 3 s sustain + 10 min greet-suppression masks brief misses. |
| R8 | Face recognition only works at home (lobsterboy camera) — Stack-chan greets nobody on travel LANs | Acceptable per D11/D12; documented; Stack-chan falls back to generic warmth without using a name. |
| R9 | USB webcam on lobsterboy is a new physical dependency | One-time setup; existing IP cam can substitute via RTSP if user prefers. |
| R10 | Vision service holds enrolled face data on lobsterboy disk | Permissions: `~/.config/stackchan-vision/embeddings.json` 0600; never copied off-box; `/snapshot.jpg` debug endpoint off by default. |

## 14. Approval log

- **2026-06-02** — All 11 clarifying questions answered. All 7 design sections approved interactively in brainstorm session.
- **2026-06-02** — Mid-spec revision: user added "always on LAN" deployment model and "integrate camera, recognize my face" requirement. Initial draft routed recognition to a XIAO side-car; user explicitly refused a second device. Final design places recognition on lobsterboy with a USB webcam.
- **Next:** user reviews this written spec; on approval, hand off to `writing-plans` skill.
