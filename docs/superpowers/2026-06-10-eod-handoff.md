# Session handoff — end of 2026-06-10 (bring-up day + MCP server)

Resume doc for the next session. Supersedes the morning's
[`2026-06-10-session-handoff.md`](2026-06-10-session-handoff.md) — everything open there is
now closed. The robot is **fully assembled, on the desk, and working end-to-end**: head
moves, presence detects faces and greets, and the on-device MCP server lets Claude Code
drive it directly.

## Repo / device state

- `main` = **`f535923`** (PRs #8, #9, #10, #12 merged today; remote in sync; no open
  feature branches from this session).
- Unit #1 (CoreS3, MAC `10:20:BA:26:E8:98`, `192.168.1.121` / `stackchan.local`) runs the
  current `main` build. **Booting from `ota_1`** — re-read the partition gotcha in the
  `stackchan-flashing-workflow` memory before any USB flash.
- Claude Code MCP registration: `stackchan: http://192.168.1.121/mcp (HTTP) — ✔ Connected`
  (local config of this project).
- Unrelated remote branch `claude/advisor-tool-implementation-ukye5p` appeared on origin
  today — not from this session; investigate or delete at leisure.

## ✅ DONE today

1. **Head bring-up complete.** Travel matched to the assembled case: yaw ±45°,
   **pitch 0…+25° only** (`kPitchMin = 0` — forward/negative pitch fouls the base). The
   tilt servo is mirror-mounted, so `Servos.cpp` **negates pitch** at the PWM write
   (`degToPwm(-deg)`). ControlBridge SERVO dispatch now calls `motion.pauseIdle()` so web
   slider / MCP moves don't fight the idle animation.
2. **Presence (on-device camera face detection) actually works** — it had been silently
   dead. Two compounding fixes in `src/vision/PresenceSensor.cpp`: convert frames via
   `fmt2rgb888` before inference (the prebuilt esp-dl direct-RGB565 path returns zero
   candidates), and use the CameraWebServer reference params (MSR01 score **0.1** — real
   faces score only 0.10–0.22 at stage 1). Debug endpoint: `GET /api/debug/presence`
   (includes a build tag — use it to verify which build is running, never "did it reboot").
3. **On-device MCP server live** (spec `specs/2026-06-10-mcp-server-design.md` rev 2, plan
   `plans/2026-06-10-mcp-server.md`, all tasks done). Streamable HTTP, stateless JSON at
   `POST /mcp`; four tools: `say`, `set_expression`, `move_head`, `get_status`. 35 native
   unity tests pass (`pio test -e native`). Verified end-to-end: a headless Claude session
   drove all four tools on the live robot.
4. **OTA hardened**: `OtaService` onStart pauses presence scanning (esp-dl flash reads
   stalled the OTA writer); main-loop presence gate includes `!ota.isActive()`.

## ⚠️ Gotchas that will bite again (short list — details in memory + git log)

- **MCP transport:** MCP clients send `Accept: …text/event-stream`, which makes
  ESPAsyncWebServer classify the request RCT_EVENT and 404 stock `server.on()` routes.
  `McpServer.cpp`'s custom `McpPostHandler` (AsyncWebHandler subclass) exists *solely* for
  this. Any new MCP-adjacent endpoint needs the same treatment.
- **Heap:** internal RAM is razor thin with vision on (~70 KB free, observed min 1.8 KB).
  Big buffers and JsonDocuments go in **PSRAM** (`ps_malloc` / PsramAllocator pattern in
  `McpServer.cpp`). **No large HTTP POSTs to the device** (a 153 KB body knocked it off
  WiFi). MCP body cap is 4 KB — keep it.
- **curl testing:** always `-H 'Content-Type: application/json'` — bare `-d` sends
  form-encoding, which AsyncWebServer consumes as POST params and `onBody` never fires.
- **Flashing:** all of it (COM16 Windows esptool, OTA pw, `boot_app0.bin` @ 0xe000,
  `dtr/rts=False` serial) is in the `stackchan-flashing-workflow` memory.
- `CaptivePortal.cpp`'s `g_pending_config` global body buffer has a latent concurrency
  bug — don't copy that pattern; use the per-request `_tempObject` pattern from
  `McpServer.cpp`.

## 📋 PLAN — next session

Each task gets its own brainstorm → spec → plan cycle (superpowers workflow). Suggested
order as listed; Task 1 is self-contained and high-value, Task 2 is the bigger lift.

### Task 1: Stock audio clips for regular phrases + error messages

**Problem:** all speech goes through cloud TTS (`src/net/TtsClient.cpp` → OpenAI /
ElevenLabs MP3 → PSRAM → `AudioPlayer::play()`). When the network/provider is down the
robot is mute — exactly when the `kErr*` phrases ("I can't connect to anything.",
`src/config.h:126-131`) and greetings (`src/prompts/greetings.h`) matter most. They're
fixed strings, so render them once and ship them.

**Shape of the work:**
- Host-side `tools/render_speeches.py`: enumerate the `kErr*` strings + greeting pool
  (+ "queued"-style MCP acks if desired), render each to MP3 via the configured TTS
  provider (or local TTS once the Halo box lands), write to `data/clips/<id>.mp3`.
- Device: clip lookup keyed by phrase ID; on hit, play from LittleFS
  (`AudioFileSourceLittleFS` from ESP8266Audio — same decoder, different source) instead
  of calling TtsClient. Fall back to cloud TTS on miss so new phrases still work.
- FS already mounted (LittleFS, `default_16MB.csv` — multi-MB data partition, clips are
  ~20–60 KB each). Upload clips with the filesystem image (`pio run -t buildfs` + flash
  from Windows side, or OTA-fs) — **not** via HTTP POST to the device (heap gotcha above).
- Decisions for the spec: clip-ID scheme (hash of text vs. explicit enum), whether
  greetings rotate through N pre-rendered variants, voice consistency with the live TTS
  voice.

### Task 2: Wake on keyword ("hey Stack-chan" → start a listening turn)

Roadmap Phase 3 (`roadmaps/2026-06-06-next-capabilities-roadmap.md` §Phase 3) — the
groundwork analysis there still holds. Key constraints discovered since it was written:
**internal heap is nearly exhausted with vision enabled**, so an on-chip KWS model
competes with esp-dl for the same scarce RAM, and the mic must coexist with
press-and-hold `MicRecorder` *and* yield during TTS playback.

**Candidate approaches (spec must pick):**
- **(a) ESP-SR WakeNet on-chip** — proper wake-word, but esp-sr's Arduino support targets
  arduino-esp32 3.x / IDF 5; we're pinned to 2.0.17 (espressif32@6.13.0). Platform
  upgrade is a real project on its own (touches every lib). RAM budget vs. vision TBD.
- **(b) VAD-then-confirm MVP** — continuous lightweight energy/VAD on the mic; on speech,
  record a short window, run normal STT, check the transcript starts with the wake
  phrase; discard otherwise. Works on the current stack today; costs an STT round-trip
  per utterance and only "wakes" after a beat. Cheap to build, easy to throw away.
- **(c) M5 LLM Module offload** — hardware purchase + the spec §4 Port-C/M5Bus policy
  change; park unless (a) and (b) both disappoint.
- New FSM entry point either way: `onWakeDetected()` → IDLE→LISTENING (analogous to
  `onPressDown()`; ControlBridge can't express "start a turn").
- Validate continuous-mic power draw against the (now healthy) power budget.

**Synergy:** Task 1's clips give the wake flow an instant offline "yes?" ack while STT/
chat spin up.

### Backlog (unscheduled, need owner opt-in)

- Lip-sync from audio power; eye-gaze-before-head-turn; face decorators.
- `take_photo` MCP tool (chunked — mind the 4 KB / heap limits).
- Lemonade / OpenAI-compatible ChatClient path when the Ryzen AI Halo box arrives
  (spec `2026-06-10-mcp-server-design.md` §8).
- Dedicated 5 V ≥ 2 A servo supply (hardware; owner's).

## Pointers

- Memory: `stackchan-flashing-workflow`, `ryzen-ai-halo-box`, `mg90s-servos-actual-build`.
- Specs/plans: `specs/2026-06-10-mcp-server-design.md`, `plans/2026-06-10-mcp-server.md`.
- Code hot spots: `src/services/McpServer.cpp` (transport patterns worth copying),
  `src/services/McpProtocol.{h,cpp}` (pure-C++ core, native-testable),
  `src/vision/PresenceSensor.cpp` (camera/esp-dl), `src/hal/Servos.cpp` (travel limits +
  pitch inversion), `src/config.h` (`kErr*` taxonomy, NVS keys).
