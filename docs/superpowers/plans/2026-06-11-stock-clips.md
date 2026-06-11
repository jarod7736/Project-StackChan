# Offline Stock Audio Clips Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pre-render the ~12 fixed phrases (errors, low-batt, greetings) to MP3 clips on LittleFS so the robot speaks them instantly and offline; cloud TTS remains for dynamic responses.

**Architecture:** A pure `ClipId` module maps phrase text → `/clips/<fnv1a32-hex>.mp3`. `TtsClient::synth()` checks LittleFS first (buffered read → PSRAM → existing `audio.play()`), falling through to cloud TTS on any miss/failure. A host-side Python script extracts phrases from the C++ headers, renders them via the same TTS provider/voice the device uses, and writes clips + manifest into `data/clips/` for the normal `buildfs` image. Spec: `docs/superpowers/specs/2026-06-11-stock-clips-design.md`.

**Tech Stack:** C++17 (Arduino/ESP32-S3, PlatformIO), Unity native tests (`pio test -e native`), Python 3 stdlib only (`urllib`), LittleFS, ESP8266Audio (already wrapped by `AudioPlayer`).

---

## File map

| File | Action | Responsibility |
|---|---|---|
| `src/services/ClipId.h` | Create | Pure API: `fnv1a32()`, `clipPathForText()` |
| `src/services/ClipId.cpp` | Create | Implementation (no Arduino deps) |
| `test/test_clip_id/test_clip_id.cpp` | Create | Unity known-answer + path tests |
| `platformio.ini` | Modify (line 91) | Add `ClipId.cpp` to native `build_src_filter` |
| `src/net/TtsClient.cpp` | Modify | `tryPlayClip()` helper + intercept in `synth()` |
| `tools/render_speeches.py` | Create | Extract phrases, render MP3s, manifest, `--check` |
| `data/clips/*.mp3`, `data/clips/manifest.json` | Generate + commit | The clips |

---

### Task 1: ClipId pure module (TDD)

**Files:**
- Create: `test/test_clip_id/test_clip_id.cpp`
- Create: `src/services/ClipId.h`, `src/services/ClipId.cpp`
- Modify: `platformio.ini:91` (build_src_filter)

- [ ] **Step 1: Write the failing test**

Create `test/test_clip_id/test_clip_id.cpp`:

```cpp
// test/test_clip_id/test_clip_id.cpp
// Known-answer tests for the FNV-1a 32-bit hash and the clip path scheme.
// The render script (tools/render_speeches.py) implements the same hash in
// Python; the "foobar" vector below also appears in its --selftest.
#include <unity.h>
#include <string.h>
#include "services/ClipId.h"

using stkchan::fnv1a32;
using stkchan::clipPathForText;

// Standard FNV-1a 32-bit vectors (offset basis 0x811c9dc5, prime 0x01000193).
static void test_fnv1a32_empty() {
  TEST_ASSERT_EQUAL_UINT32(0x811c9dc5u, fnv1a32("", 0));
}
static void test_fnv1a32_a() {
  TEST_ASSERT_EQUAL_UINT32(0xe40c292cu, fnv1a32("a", 1));
}
static void test_fnv1a32_foobar() {
  TEST_ASSERT_EQUAL_UINT32(0xbf9cf968u, fnv1a32("foobar", 6));
}

static void test_path_format() {
  // "foobar" → 0xbf9cf968 → "/clips/bf9cf968.mp3" (lowercase, zero-padded)
  std::string p = clipPathForText("foobar");
  TEST_ASSERT_EQUAL_STRING("/clips/bf9cf968.mp3", p.c_str());
}
static void test_path_zero_padded() {
  // Hash of "" is 0x811c9dc5 (no leading zero) — instead verify padding by
  // construction: every path must be exactly strlen("/clips/") + 8 + 4 long.
  std::string p = clipPathForText("Hm, didn't catch that.");
  TEST_ASSERT_EQUAL_UINT32(7 + 8 + 4, (uint32_t)p.size());
  TEST_ASSERT_TRUE(p.rfind("/clips/", 0) == 0);
  TEST_ASSERT_EQUAL_STRING(".mp3", p.c_str() + p.size() - 4);
}
static void test_path_deterministic() {
  TEST_ASSERT_EQUAL_STRING(clipPathForText("Oh! Hi there.").c_str(),
                           clipPathForText("Oh! Hi there.").c_str());
}
static void test_path_distinct_texts_distinct_paths() {
  TEST_ASSERT_TRUE(clipPathForText("Oh! Hi there.") !=
                   clipPathForText("There you are!"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fnv1a32_empty);
  RUN_TEST(test_fnv1a32_a);
  RUN_TEST(test_fnv1a32_foobar);
  RUN_TEST(test_path_format);
  RUN_TEST(test_path_zero_padded);
  RUN_TEST(test_path_deterministic);
  RUN_TEST(test_path_distinct_texts_distinct_paths);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_clip_id`
Expected: FAIL to build — `services/ClipId.h: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `src/services/ClipId.h`:

```cpp
#pragma once

// ClipId — phrase text → pre-rendered clip path on LittleFS.
//
// A clip's filename is the FNV-1a 32-bit hash of the phrase's exact UTF-8
// bytes, lowercase hex: /clips/<8-hex>.mp3. The text IS the key — the device
// and tools/render_speeches.py (same hash in Python) can never disagree.
// Pure C++ (no Arduino) so it runs under `pio test -e native`.
//
// Spec: docs/superpowers/specs/2026-06-11-stock-clips-design.md

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace stkchan {

// FNV-1a 32-bit over `len` bytes of `data`.
uint32_t fnv1a32(const char* data, size_t len);

// "/clips/<8-hex-lowercase>.mp3" for the exact bytes of NUL-terminated `text`.
std::string clipPathForText(const char* text);

}  // namespace stkchan
```

Create `src/services/ClipId.cpp`:

```cpp
#include "ClipId.h"

#include <cstdio>
#include <cstring>

namespace stkchan {

uint32_t fnv1a32(const char* data, size_t len) {
    uint32_t h = 0x811c9dc5u;            // FNV offset basis
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint8_t>(data[i]);
        h *= 0x01000193u;                // FNV prime
    }
    return h;
}

std::string clipPathForText(const char* text) {
    char buf[24];                        // "/clips/" + 8 hex + ".mp3" + NUL = 20
    std::snprintf(buf, sizeof(buf), "/clips/%08x.mp3",
                  fnv1a32(text, std::strlen(text)));
    return std::string(buf);
}

}  // namespace stkchan
```

Modify `platformio.ini` line 91 — add `+<services/ClipId.cpp>`:

```ini
build_src_filter = -<*> +<persona/ResponseParser.cpp> +<face/ExpressionMap.cpp> +<vision/PresenceLogic.cpp> +<services/McpProtocol.cpp> +<services/ClipId.cpp>
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_clip_id`
Expected: 7 tests, all PASS

- [ ] **Step 5: Run the full native suite (no regressions)**

Run: `pio test -e native`
Expected: all suites pass (35 pre-existing tests + 7 new)

- [ ] **Step 6: Commit**

```bash
git add test/test_clip_id/test_clip_id.cpp src/services/ClipId.h src/services/ClipId.cpp platformio.ini
git commit -m "feat(clips): ClipId pure module — FNV-1a text→clip-path mapping"
```

---

### Task 2: TtsClient clip intercept

**Files:**
- Modify: `src/net/TtsClient.cpp` (includes block at lines 1–10; anonymous namespace before line 150; `synth()` at line ~159)

No native test — this layer is Arduino/LittleFS-bound (repo pattern: device layers are verified by device build + on-device check; pure logic was tested in Task 1).

- [ ] **Step 1: Add includes**

In `src/net/TtsClient.cpp`, extend the include block (after line 10, `#include "../services/NvsStore.h"`):

```cpp
#include <LittleFS.h>

#include "../services/ClipId.h"
#include "../services/OtaService.h"
```

- [ ] **Step 2: Add the `tryPlayClip()` helper**

Inside the existing anonymous namespace (immediately before its closing `}  // namespace`, currently line 150), add:

```cpp
// ── Pre-rendered clip playback ─────────────────────────────────────────────
// If a clip exists for this exact text (see services/ClipId.h), read it from
// LittleFS into a transient PSRAM buffer and hand it to audio.play() — the
// same buffered fetch-fully-then-play shape as the cloud path (AXP invariant:
// never overlap I/O with the audio amp; see fetchMp3ToPsram above).
// Returns true iff playback started. Any failure returns false and the
// caller falls through to cloud TTS — clips can only improve behavior.
bool tryPlayClip(const String& text) {
    if (OtaService::isActive()) return false;  // FS reads stall the OTA writer
    String path(clipPathForText(text.c_str()).c_str());
    File f = LittleFS.open(path, "r");
    if (!f) return false;                       // no clip (or FS unmounted)
    size_t len = f.size();
    if (len == 0 || len > kMp3MaxBytes) { f.close(); return false; }
    auto* buf = static_cast<uint8_t*>(ps_malloc(len));
    if (!buf) { f.close(); return false; }
    size_t got = f.read(buf, len);
    f.close();
    // audio.play() copies into its own PSRAM buffer; free ours right after.
    bool ok = (got == len) && audio.play(buf, got);
    free(buf);
    if (ok) {
        Serial.printf("[TtsClient] clip hit %s (%u B)\n", path.c_str(),
                      (unsigned)len);
    } else {
        Serial.printf("[TtsClient] clip %s unplayable, falling back\n",
                      path.c_str());
    }
    return ok;
}
```

- [ ] **Step 3: Intercept in `synth()`**

In `TtsClient::synth()`, directly after the empty-text guard (currently lines 155–159), insert:

```cpp
    // Pre-rendered clip? Instant, free, offline-safe. Falls through to cloud
    // on any miss. Spec: docs/superpowers/specs/2026-06-11-stock-clips-design.md
    if (tryPlayClip(text)) {
        onDone(true);  // playback started — same contract as the cloud path
        return;
    }
```

- [ ] **Step 4: Device build compiles**

Run: `pio run -e cores3_linux 2>&1 | tail -5` (the Linux-host device build; `cores3` is the Windows-side flash env)
Expected: `SUCCESS`

- [ ] **Step 5: Native suite still passes**

Run: `pio test -e native`
Expected: all pass (TtsClient.cpp is not in the native filter; this catches accidental header damage)

- [ ] **Step 6: Commit**

```bash
git add src/net/TtsClient.cpp
git commit -m "feat(clips): play pre-rendered LittleFS clips in TtsClient before cloud TTS"
```

---

### Task 3: render_speeches.py

**Files:**
- Create: `tools/render_speeches.py`

- [ ] **Step 1: Write the script**

Create `tools/render_speeches.py` (stdlib only — no pip installs):

```python
#!/usr/bin/env python3
"""Render the firmware's fixed phrases to MP3 clips for LittleFS.

Extracts phrases from the C++ headers (single source of truth), renders each
via the SAME provider/voice/model the device uses (request shape mirrors
src/net/TtsClient.cpp), and writes data/clips/<fnv1a32-hex>.mp3 + manifest.

The hash MUST match src/services/ClipId.cpp (FNV-1a 32-bit over UTF-8 bytes).

Usage:
  OPENAI_API_KEY=... python3 tools/render_speeches.py          # render missing
  python3 tools/render_speeches.py --list                      # phrases+hashes
  python3 tools/render_speeches.py --check                     # CI drift gate
  python3 tools/render_speeches.py --force                     # re-render all
  python3 tools/render_speeches.py --selftest                  # hash vectors
  ELEVENLABS_API_KEY=... python3 tools/render_speeches.py \\
      --provider eleven --voice <voice_id> --model eleven_multilingual_v2

Spec: docs/superpowers/specs/2026-06-11-stock-clips-design.md
"""
import argparse
import json
import os
import re
import sys
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_H = ROOT / "src" / "config.h"
GREETINGS_H = ROOT / "src" / "prompts" / "greetings.h"
CLIPS_DIR = ROOT / "data" / "clips"
MANIFEST = CLIPS_DIR / "manifest.json"

CONST_RE = re.compile(
    r'constexpr\s+const\s+char\*\s+(k\w+)\s*=\s*"((?:[^"\\]|\\.)*)"')


def unescape(s: str) -> str:
    return s.encode().decode("unicode_escape")


def fnv1a32(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def clip_name(text: str) -> str:
    return f"{fnv1a32(text.encode('utf-8')):08x}.mp3"


def extract_phrases() -> dict:
    """name → text for every spoken fixed phrase. Empty strings skipped."""
    phrases = {}
    cfg = CONFIG_H.read_text()
    for name, raw in CONST_RE.findall(cfg):
        if name.startswith("kErr") or name == "kLowBattMsg":
            text = unescape(raw)
            if text:
                phrases[name] = text
    greet_src = GREETINGS_H.read_text()
    block = re.search(r"kGreetings\[\]\s*=\s*\{(.*?)\};", greet_src, re.S)
    if not block:
        sys.exit("ERROR: kGreetings[] block not found in greetings.h")
    for i, m in enumerate(re.finditer(r'"((?:[^"\\]|\\.)*)"', block.group(1))):
        phrases[f"kGreetings[{i}]"] = unescape(m.group(1))
    return phrases


def device_defaults() -> dict:
    cfg = CONFIG_H.read_text()
    vals = dict(CONST_RE.findall(cfg))
    return {
        "provider": vals.get("kDefaultTtsProv", "openai"),
        "voice": vals.get("kDefaultTtsVoice", "nova"),
        "model": vals.get("kDefaultTtsModel", "tts-1"),
    }


def render_openai(text: str, voice: str, model: str, key: str) -> bytes:
    # Mirrors buildOpenAiBody() in src/net/TtsClient.cpp.
    body = json.dumps({"model": model, "voice": voice, "input": text,
                       "response_format": "mp3"}).encode()
    req = urllib.request.Request(
        "https://api.openai.com/v1/audio/speech", data=body,
        headers={"Authorization": f"Bearer {key}",
                 "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def render_eleven(text: str, voice: str, model: str, key: str) -> bytes:
    # Mirrors buildElevenBody() in src/net/TtsClient.cpp.
    body = json.dumps({"text": text, "model_id": model,
                       "voice_settings": {"stability": 0.5,
                                          "similarity_boost": 0.75}}).encode()
    req = urllib.request.Request(
        f"https://api.elevenlabs.io/v1/text-to-speech/{voice}", data=body,
        headers={"xi-api-key": key, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def selftest() -> None:
    # Must match test/test_clip_id/test_clip_id.cpp.
    assert fnv1a32(b"") == 0x811C9DC5
    assert fnv1a32(b"a") == 0xE40C292C
    assert fnv1a32(b"foobar") == 0xBF9CF968
    assert clip_name("foobar") == "bf9cf968.mp3"
    print("selftest OK")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--provider", default=None, help="openai|eleven")
    ap.add_argument("--voice", default=None)
    ap.add_argument("--model", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return 0

    phrases = extract_phrases()
    defaults = device_defaults()
    provider = args.provider or defaults["provider"]
    voice = args.voice or defaults["voice"]
    model = args.model or defaults["model"]

    if args.list:
        for name, text in phrases.items():
            print(f"{clip_name(text)}  {name:<16} {text!r}")
        return 0

    if args.check:
        missing = [n for n, t in phrases.items()
                   if not (CLIPS_DIR / clip_name(t)).exists()]
        expected = {clip_name(t) for t in phrases.values()}
        orphans = [p.name for p in CLIPS_DIR.glob("*.mp3")
                   if p.name not in expected]
        for n in missing:
            print(f"MISSING clip for {n}: {phrases[n]!r}")
        for o in orphans:
            print(f"ORPHAN clip {o} (phrase text changed or removed?)")
        if missing or orphans:
            return 1
        print(f"OK: {len(phrases)} phrases all have clips, no orphans")
        return 0

    # Render.
    if provider == "openai":
        key = os.environ.get("OPENAI_API_KEY", "")
        render = render_openai
    elif provider in ("eleven", "elevenlabs"):
        key = os.environ.get("ELEVENLABS_API_KEY", "")
        render = render_eleven
    else:
        sys.exit(f"ERROR: unsupported provider {provider!r}")
    if not key:
        sys.exit("ERROR: API key env var not set")

    CLIPS_DIR.mkdir(parents=True, exist_ok=True)
    manifest = {"provider": provider, "voice": voice, "model": model,
                "rendered_at": datetime.now(timezone.utc).isoformat(),
                "clips": {}}
    for name, text in phrases.items():
        fname = clip_name(text)
        out = CLIPS_DIR / fname
        manifest["clips"][fname] = {"name": name, "text": text}
        if out.exists() and not args.force:
            print(f"skip   {fname}  {name} (exists)")
            continue
        print(f"render {fname}  {name}  {text!r}")
        out.write_bytes(render(text, voice, model, key))
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False)
                        + "\n")
    print(f"wrote {MANIFEST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Selftest (hash parity with C++)**

Run: `python3 tools/render_speeches.py --selftest`
Expected: `selftest OK`

- [ ] **Step 3: Extraction works**

Run: `python3 tools/render_speeches.py --list`
Expected: 12 lines — 5 `kErr*` (NO `kErrTtsFailed` — it's empty), `kLowBattMsg`, `kGreetings[0..5]`, each with an 8-hex `.mp3` name. Verify "I can't connect to anything." appears.

- [ ] **Step 4: --check reports missing clips (clips not rendered yet)**

Run: `python3 tools/render_speeches.py --check; echo "exit=$?"`
Expected: 12 `MISSING` lines, `exit=1`

- [ ] **Step 5: Commit**

```bash
git add tools/render_speeches.py
git commit -m "feat(clips): render_speeches.py — extract phrases from headers, render via TTS API"
```

---

### Task 4: Render the clips (needs `OPENAI_API_KEY`)

**Files:**
- Create (generated): `data/clips/*.mp3` (12 files), `data/clips/manifest.json`

> If `OPENAI_API_KEY` is not in the environment, STOP and ask the owner to run
> Step 1 themselves (the key lives in device NVS, not necessarily on this host).

- [ ] **Step 1: Render**

Run: `OPENAI_API_KEY=<key> python3 tools/render_speeches.py`
Expected: 12 `render …` lines, then `wrote data/clips/manifest.json`

- [ ] **Step 2: Verify**

Run: `python3 tools/render_speeches.py --check && ls -la data/clips/ && du -sh data/clips/`
Expected: `OK: 12 phrases all have clips, no orphans`; 12 MP3s (~20–60 KB each) + manifest; total well under 1 MB

Spot-check audio on the host: `mpv data/clips/$(python3 -c "import sys; sys.path.insert(0,'tools'); from render_speeches import clip_name; print(clip_name(\"I can't connect to anything.\"))")` (or any player; skip if headless — the on-device check in Task 5 covers it).

- [ ] **Step 3: Commit (binaries are intentional — buildfs needs them from a fresh clone)**

```bash
git add data/clips/
git commit -m "feat(clips): pre-rendered MP3 clips for the 12 fixed phrases + manifest"
```

---

### Task 5: FS image + on-device verification (owner hardware steps)

**Files:** none (build + flash + observe)

- [ ] **Step 1: Build the FS image**

Run: `pio run -t buildfs 2>&1 | tail -3`
Expected: SUCCESS; `.pio/build/<env>/littlefs.bin` exists and includes `clips/` (size check: `ls -la .pio/build/*/littlefs.bin`)

- [ ] **Step 2: Flash FS image — OWNER STEP (Windows side)**

Per the `stackchan-flashing-workflow` memory: flash `littlefs.bin` to the `spiffs` partition offset from `default_16MB.csv` via Windows esptool on COM16 (or OTA-fs if available). The app flash is untouched; the firmware from Tasks 1–2 must also be flashed (OTA is fine).

- [ ] **Step 3: On-device verification**

1. Confirm the new build is running (build tag via `GET /api/debug/presence`).
2. Online greeting: walk into view → greeting plays **instantly**; serial shows `[TtsClient] clip hit /clips/….mp3`, no HTTP fetch.
3. Offline error: disable WiFi (or unplug the AP) → press-and-hold → "I can't connect to anything." **plays audibly from flash** — the headline feature.
4. Dynamic speech unchanged: ask a question online → normal cloud TTS response.

Expected: all four observations hold.

- [ ] **Step 4: Commit any doc touch-ups, then merge**

Wrap up via superpowers:finishing-a-development-branch (merge `worktree-stock-clips` → `main`, push, clean up).

---

## Self-review notes

- **Spec coverage:** §1 clip identity → Task 1; §2 device side → Tasks 1–2; §3 host script → Task 3; §4 deployment → Tasks 4–5; §5 testing → steps in every task. OTA guard → Task 2 Step 2. Voice-match decision → Task 3 `device_defaults()` + Task 4.
- **Hash parity** is enforced twice: C++ known-answer tests (Task 1) and Python `--selftest` (Task 3) share the `foobar` vector.
- **`kMp3MaxBytes`** is referenced in `tryPlayClip` and already defined in `src/config.h` (used at `TtsClient.cpp:106`).
- Greeting rotation needs no work: each greeting text is its own clip; `pickGreeting()` round-robin is untouched.
