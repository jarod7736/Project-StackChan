# Stock audio clips for fixed phrases — design

**Date:** 2026-06-11
**Status:** Approved (brainstorm decisions confirmed by owner)
**Origin:** Task 1 of `docs/superpowers/2026-06-10-eod-handoff.md`

## Problem

All speech goes through cloud TTS (`src/net/TtsClient.cpp` → OpenAI/ElevenLabs → PSRAM →
`AudioPlayer`). When the network or provider is down the robot is mute — exactly when the
error phrases ("I can't connect to anything.") matter most. The fixed phrases never change
at runtime, so render them once on the host and ship them on LittleFS.

## Decisions (confirmed in brainstorm)

1. **Scope:** all ~12 fixed phrases — the 5 spoken `kErr*` strings (`src/config.h:126-130`;
   `kErrTtsFailed` is empty/display-only and excluded), `kLowBattMsg` (`src/config.h:63`),
   and the 6 greetings (`src/prompts/greetings.h`).
2. **Policy: clip always wins.** If a clip exists for the exact text, play it from LittleFS
   even when online — instant playback, zero API cost, offline by construction. Cloud TTS
   remains for dynamic LLM responses.
3. **Voice: match the live TTS voice.** The render script calls the same provider / voice /
   model the device is configured with, with the same request shape as `TtsClient`, so
   clips are indistinguishable from live speech. Re-render when the device voice changes.
4. **Architecture: intercept in `TtsClient::synth()`** (approach A). Zero call-site
   changes; every fixed phrase everywhere (FSM errors, greetings, low-batt, MCP `say` if
   the text happens to match) benefits automatically.

## 1. Clip identity

- Clip filename = **FNV-1a 32-bit hash of the phrase's exact UTF-8 bytes**, lowercase hex:
  `/clips/<8-hex>.mp3`. The text is the key — no enum to maintain, and the device and the
  render script cannot disagree on naming.
- `data/clips/manifest.json` (committed) maps hash → text plus render metadata
  (`provider`, `voice`, `model`, `rendered_at`) for humans and tooling.
- Trade-off (accepted): editing a phrase in a header without re-rendering silently reverts
  that phrase to cloud TTS (mute when offline). `render_speeches.py --check` exists to
  catch this; run it after touching phrase strings.

## 2. Device side

New module `src/services/ClipStore.{h,cpp}`:

- **Pure core, native-testable** (no Arduino/FS dependencies): `uint32_t fnv1a32(const
  char* s, size_t len)` and path construction (`/clips/%08x.mp3`). Follows the
  `McpProtocol` pattern of keeping logic unity-testable under `pio test -e native`.
- **Thin device layer:** `bool has(const String& text)` — LittleFS existence check
  (LittleFS is already mounted by CaptivePortal; the check is idempotent/cheap), and
  `String pathFor(const String& text)`.

`TtsClient::synth()` gains a short pre-check before the cloud path:

```cpp
if (!ota.isActive() && clips.has(text)) {
    auto* src = new AudioFileSourceLittleFS(clips.pathFor(text).c_str());
    if (audio.playStream(src)) { onDone(true); return; }
    // playStream failed → src already owned/deleted by AudioPlayer; fall through
}
// existing cloud TTS path unchanged
```

- `AudioPlayer::playStream()` already accepts any `AudioFileSource*`, takes ownership, and
  drives `onPlayDone` — **no AudioPlayer changes**.
- The FSM contract is preserved: `onDone(true)` = playback started; SPEAKING → IDLE still
  driven only by `onPlayDone`.
- **OTA guard:** when `ota.isActive()`, skip the clip path (LittleFS reads stall the OTA
  flash writer — same reason presence scanning pauses during OTA).

### Error handling

Every failure degrades to today's behavior — clips can only improve things:

| Failure | Behavior |
|---|---|
| Clip file missing / FS not mounted | `has()` false → cloud TTS |
| Clip corrupt / decoder refuses | `playStream()` false → cloud TTS (log it) |
| Offline and no clip | existing `onDone(false)` error path |

## 3. Host side — `tools/render_speeches.py`

- **Phrase extraction from the headers** (single source of truth): regex over
  `constexpr const char*` definitions — `kErr*` + `kLowBattMsg` in `src/config.h`, the
  `kGreetings` pool in `src/prompts/greetings.h`. Empty strings skipped.
- **Rendering:** same request shape as `TtsClient` (`buildOpenAiBody` /
  `buildElevenBody` equivalents). Provider/voice/model default to the device defaults
  parsed from `config.h`; CLI flags override (`--provider`, `--voice`, `--model`). API
  keys from env (`OPENAI_API_KEY` / `ELEVENLABS_API_KEY`).
- **Output:** `data/clips/<hash>.mp3` + `manifest.json`. Idempotent — existing clips
  skipped unless `--force`.
- **`--check` mode:** exit non-zero listing any extracted phrase without a clip (and any
  orphaned clip). No network calls; safe for CI.

## 4. Deployment

Clips live in `data/clips/`, so the existing `pio run -t buildfs` includes them in the
LittleFS image alongside `data/web/`. Flash the FS image from the Windows side per the
`stackchan-flashing-workflow` memory (or OTA-fs). **Never** upload via HTTP POST to the
device (heap: a 153 KB body knocked it off WiFi).

Size: ~12 clips × 20–60 KB ≈ well under 1 MB; the data partition is ~4 MB
(`default_16MB.csv`, `spiffs` 0x3F0000).

## 5. Testing

- **Native (`pio test -e native`):** FNV-1a known-answer vectors; `pathFor` mapping
  (text → expected `/clips/<hex>.mp3`).
- **Host:** `render_speeches.py --check` passes after rendering; manifest matches headers.
- **On-device:** verify build tag via `GET /api/debug/presence`; WiFi off → button press →
  "I can't connect to anything." plays from flash; greeting plays instantly on face
  detection while online (no cloud round-trip in the serial log).

## Non-goals

- No runtime clip recording/caching (rejected approach C — flash wear, heap pressure).
- No phrase-ID enum at call sites (rejected approach B — churn, misses MCP/web text).
- No local/Halo-box TTS rendering yet — the script's provider flag leaves the door open.
