# Stack-chan v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the Phase 1 voice-interactive desk companion described in `docs/superpowers/specs/2026-06-02-stackchan-design.md` — touch-to-talk on an M5Stack CoreS3 + Stack-chan servo base, talking to Whisper API → Ollama → cloud TTS, with an animated face and idle head motion.

**Architecture:** Single-threaded Arduino-ESP32 FSM (Jarvis's pattern). Device orchestrates the whole turn; lobsterboy hosts Ollama only. Modules separated by responsibility: `hal/` (audio, mic, servos, display), `face/` + `motion/` (Avatar + servos), `net/` (HTTP clients + connectivity tier), `persona/` (prompt + parser), `services/` (NVS, OTA, captive portal).

**Tech Stack:** PlatformIO + Arduino-ESP32, M5CoreS3, M5Stack-Avatar, ESP8266Audio (MP3 decode), ESPAsyncWebServer (captive portal), ArduinoJson, ServoEasing + Adafruit PCA9685, Unity (native unit tests). Face recognition is **out of scope** for this plan — its design lives in Appendix A of the spec.

---

## Conventions & invariants (read once, apply everywhere)

These come from the spec and Jarvis's PRs; they apply to every task.

1. **FSM owns transitions.** HAL/net callbacks set flags; they never call HAL/net directly or transition state directly.
2. **Render before HTTP.** Anything that should display (Face, status) renders *before* the blocking `http.POST()`.
3. **`http.end()` on every exit path** of every HTTP client. Timeout, parse error, success — all paths.
4. **PSRAM rule.** Any buffer > 512 B uses `ps_malloc` or `DynamicJsonDocument` against PSRAM. Small request bodies stay on stack as `char[256]`.
5. **NVS namespace is `"stkchan"`, keys ≤ 15 chars.**
6. **Errors route through `kErr*` constants in `src/config.h`.** Pattern: `g_state = ERROR` → `face.setExpression(...)` → `tts.synth(kErr*)` → `audio.play()` → `SPEAKING` → `onPlayDone` → `IDLE`. Never silent IDLE.
7. **All commits go through pre-commit hooks** (if any are installed) — never use `--no-verify`.
8. **Commit after every task** so each step is atomic and reviewable.

If a step contradicts these conventions, **stop and ask** — don't silently deviate.

---

## File map (created or modified across tasks)

```
Project-StackChan/
├── platformio.ini                     T1
├── default_16MB.csv                   T1
├── .gitignore                         T1
├── README.md                          T1
├── partitions/                        (none — use root default_16MB.csv)
├── data/web/                          T6 (captive portal UI)
├── src/
│   ├── main.cpp                       T1 (hello), T21 (wired)
│   ├── config.h                       T2
│   ├── state_machine.h                T20
│   ├── state_machine.cpp              T20
│   ├── services/
│   │   ├── NvsStore.h                 T3
│   │   ├── NvsStore.cpp               T3
│   │   ├── OtaService.h               T7
│   │   ├── OtaService.cpp             T7
│   │   ├── CaptivePortal.h            T6
│   │   └── CaptivePortal.cpp          T6
│   ├── net/
│   │   ├── WifiManager.h              T4
│   │   ├── WifiManager.cpp            T4
│   │   ├── ConnectivityTier.h         T5
│   │   ├── ConnectivityTier.cpp       T5
│   │   ├── SttClient.h                T11
│   │   ├── SttClient.cpp              T11
│   │   ├── ChatClient.h               T13
│   │   ├── ChatClient.cpp             T13
│   │   ├── TtsClient.h                T9
│   │   └── TtsClient.cpp              T9
│   ├── hal/
│   │   ├── AudioPlayer.h              T8
│   │   ├── AudioPlayer.cpp            T8
│   │   ├── MicRecorder.h              T10
│   │   ├── MicRecorder.cpp            T10
│   │   ├── Display.h                  T19
│   │   ├── Display.cpp                T19
│   │   ├── Servos.h                   T17
│   │   └── Servos.cpp                 T17
│   ├── face/
│   │   ├── Face.h                     T16
│   │   ├── Face.cpp                   T16
│   │   ├── ExpressionMap.h            T15
│   │   └── ExpressionMap.cpp          T15
│   ├── motion/
│   │   ├── MotionDirector.h           T18
│   │   └── MotionDirector.cpp         T18
│   ├── persona/
│   │   ├── SystemPrompt.h             T14
│   │   ├── ResponseParser.h           T12
│   │   └── ResponseParser.cpp         T12
│   └── prompts/
│       └── persona_examples.h         T14
├── test/
│   ├── test_response_parser/          T12
│   │   └── test_response_parser.cpp
│   └── test_expression_map/           T15
│       └── test_expression_map.cpp
└── tools/
    └── provision-stackchan.py         T22
```

---

## Task 1: Repo bootstrap

**Files:**
- Create: `platformio.ini`
- Create: `default_16MB.csv`
- Create: `.gitignore`
- Create: `README.md`
- Create: `src/main.cpp`

**Goal:** A repo that builds, boots on a CoreS3, prints a banner over serial, and commits cleanly.

- [ ] **Step 1: Create `.gitignore`**

```gitignore
.pio/
.vscode/
*.bin
*.elf
*.map
*.swp
*.swo
.DS_Store
__pycache__/
*.pyc
.venv/
node_modules/
```

- [ ] **Step 2: Create `platformio.ini`**

Use Jarvis's known-good PSRAM init (`memory_type = qio_qspi`, **not** the older `flash_mode`/`psram_type` pair the spec mentions). Keep it tight — no Phase-2 libs yet.

```ini
; Project Stack-chan — M5Stack CoreS3 + Stack-chan servo base
; Spec: docs/superpowers/specs/2026-06-02-stackchan-design.md

[platformio]
default_envs = cores3

[env:cores3]
platform = espressif32@6.13.0
board = m5stack-cores3
framework = arduino

; CoreS3 has 8 MB Quad-SPI PSRAM (NOT Octal). qio_qspi is the working
; setting that survives the 3.20017 framework toolchain bump; qio_opi
; leaves ESP.getPsramSize() == 0 and breaks cloud-TTS MP3 allocation.
board_build.arduino.memory_type = qio_qspi

build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCORE_DEBUG_LEVEL=3
    -std=gnu++17

build_unflags =
    -std=gnu++11

board_build.filesystem = littlefs
board_build.partitions = default_16MB.csv

monitor_speed = 115200
monitor_filters =
    esp32_exception_decoder
    time

lib_deps =
    m5stack/M5CoreS3@^1.0.1
    bblanchon/ArduinoJson@^7.2.0
    earlephilhower/ESP8266Audio@^1.9.7
    esp32async/ESPAsyncWebServer@^3.6.0
    esp32async/AsyncTCP@^3.2.0
    meganetaaan/M5Stack-Avatar@^0.10.0
    arduino-libraries/Servo@^1.2.2
    adafruit/Adafruit PWM Servo Driver Library@^3.0.2

; Native unit-test environment (host-side parser/mapper tests). Hardware
; tasks verify on the device; pure-logic modules are TDD'd here.
[env:native]
platform = native
build_flags = -std=gnu++17 -DUNIT_TEST
test_framework = unity
```

- [ ] **Step 3: Create `default_16MB.csv` partition table**

Copy verbatim from Jarvis: `/home/jarod7736/workspace/Project-Jarvis/default_16MB.csv`.

```bash
cp /home/jarod7736/workspace/Project-Jarvis/default_16MB.csv \
   /home/jarod7736/workspace/Project-StackChan/default_16MB.csv
```

If that file doesn't exist (it should — `pio run` in Jarvis succeeds with it), use this standard 16 MB OTA layout:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x600000,
app1,     app,  ota_1,   ,        0x600000,
spiffs,   data, spiffs,  ,        0x3F0000,
```

- [ ] **Step 4: Create stub `src/main.cpp`**

```cpp
// Stack-chan v1 — entry point.
// See docs/superpowers/specs/2026-06-02-stackchan-design.md
#include <Arduino.h>
#include <M5CoreS3.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Stack-chan v1 boot ===");
  Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());
  Serial.printf("Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  M5.update();
  delay(10);
}
```

- [ ] **Step 5: Create minimal `README.md`**

```markdown
# Project Stack-chan

Voice-interactive M5Stack CoreS3 desk companion. See
[`docs/superpowers/specs/2026-06-02-stackchan-design.md`](docs/superpowers/specs/2026-06-02-stackchan-design.md)
for the full design and
[`docs/superpowers/plans/2026-06-02-stackchan-v1.md`](docs/superpowers/plans/2026-06-02-stackchan-v1.md)
for the v1 implementation plan.

## Build

```
pio run                 # firmware
pio run -t upload       # flash via USB
pio run -t uploadfs     # captive-portal UI to LittleFS
pio test -e native      # host-side unit tests
```
```

- [ ] **Step 6: Build to verify the toolchain is happy**

```bash
cd /home/jarod7736/workspace/Project-StackChan
pio run
```

Expected: build succeeds. If a library version doesn't resolve, downgrade to the closest tagged release.

- [ ] **Step 7: Commit**

```bash
git add platformio.ini default_16MB.csv .gitignore README.md src/main.cpp
git commit -m "chore: bootstrap PlatformIO project for Stack-chan v1"
```

---

## Task 2: `config.h` — error taxonomy, timeouts, NVS keys

**Files:**
- Create: `src/config.h`

**Goal:** Single place that downstream modules pull constants from. Spec Section 6 error table, Section 10 NVS keys.

- [ ] **Step 1: Write `src/config.h`**

```cpp
#pragma once
// Stack-chan v1 — global constants.
// See spec section 6 (errors) and section 10 (NVS schema).

#include <stdint.h>

namespace stkchan {

// === NVS ===
constexpr const char* kNvsNamespace = "stkchan";  // keys must be <= 15 chars

constexpr const char* kNvsSsid1     = "ssid1";
constexpr const char* kNvsPsk1      = "psk1";
constexpr const char* kNvsSsid2     = "ssid2";
constexpr const char* kNvsPsk2      = "psk2";
constexpr const char* kNvsSsid3     = "ssid3";
constexpr const char* kNvsPsk3      = "psk3";
constexpr const char* kNvsChatHost  = "chat_host";
constexpr const char* kNvsChatModel = "chat_model";
constexpr const char* kNvsSttUrl    = "stt_url";
constexpr const char* kNvsSttModel  = "stt_model";
constexpr const char* kNvsOaiKey    = "oai_key";
constexpr const char* kNvsTtsProv   = "tts_provider";
constexpr const char* kNvsTtsVoice  = "tts_voice";
constexpr const char* kNvsTtsModel  = "tts_model";
constexpr const char* kNvsElKey     = "el_key";
constexpr const char* kNvsOtaPass   = "ota_pass";
constexpr const char* kNvsPersona   = "persona";

// === Defaults ===
constexpr const char* kDefaultChatModel = "gemma3n:e4b";
constexpr const char* kDefaultSttModel  = "whisper-1";
constexpr const char* kDefaultTtsProv   = "openai";
constexpr const char* kDefaultTtsVoice  = "nova";
constexpr const char* kDefaultTtsModel  = "tts-1";

// === Timeouts (ms) ===
constexpr uint32_t kSttTimeoutMs        = 8000;
constexpr uint32_t kChatTimeoutMs       = 30000;
constexpr uint32_t kTtsTimeoutMs        = 8000;
constexpr uint32_t kTierProbeIntervalMs = 30000;
constexpr uint32_t kMaxRecordMs         = 6000;

// === Audio ===
constexpr uint32_t kRecordSampleRate = 16000;
constexpr size_t   kRecordMaxBytes   = 192 * 1024;  // 6 s @ 16 kHz mono 16-bit
constexpr size_t   kMp3MaxBytes      = 256 * 1024;  // PSRAM cap (PR #55 in Jarvis)

// === Conversation ===
constexpr size_t kHistoryTurns = 6;

// === User-facing error strings ===
// Routed via TtsClient → AudioPlayer; never returned silently.
constexpr const char* kErrNoWifi      = "I can't connect to anything.";
constexpr const char* kErrChatOffline = "My brain's not on the network.";
constexpr const char* kErrMicEmpty    = "Hm, didn't catch that.";
constexpr const char* kErrSttFailed   = "My ears aren't working.";
constexpr const char* kErrChatFailed  = "Brain's stuck, try again.";
constexpr const char* kErrTtsFailed   = "";  // display-only, no speech

}  // namespace stkchan
```

- [ ] **Step 2: Verify it compiles**

```bash
pio run
```

Expected: still builds. `main.cpp` doesn't include `config.h` yet — that's fine.

- [ ] **Step 3: Commit**

```bash
git add src/config.h
git commit -m "feat(config): error taxonomy, timeouts, NVS schema"
```

---

## Task 3: `NvsStore` — cherry-pick from Jarvis

**Files:**
- Create: `src/services/NvsStore.h`
- Create: `src/services/NvsStore.cpp`
- Reference: `/home/jarod7736/workspace/Project-Jarvis/src/app/NVSConfig.h` (Jarvis equivalent)

**Goal:** Tiny wrapper over ESP32 `Preferences` for the `stkchan` namespace. Strings only; everything else uses string-encoded values.

- [ ] **Step 1: Write `src/services/NvsStore.h`**

```cpp
#pragma once
#include <Arduino.h>

namespace stkchan {

class NvsStore {
 public:
  bool begin();   // false if NVS can't be opened
  void end();

  String getString(const char* key, const char* fallback = "");
  bool   putString(const char* key, const String& value);
  bool   eraseKey(const char* key);

  bool hasKey(const char* key);
};

extern NvsStore nvs;  // global singleton

}  // namespace stkchan
```

- [ ] **Step 2: Write `src/services/NvsStore.cpp`**

```cpp
#include "services/NvsStore.h"
#include <Preferences.h>
#include "config.h"

namespace stkchan {

NvsStore nvs;
static Preferences prefs;
static bool g_open = false;

bool NvsStore::begin() {
  if (g_open) return true;
  g_open = prefs.begin(kNvsNamespace, /*readOnly=*/false);
  return g_open;
}

void NvsStore::end() {
  if (g_open) {
    prefs.end();
    g_open = false;
  }
}

String NvsStore::getString(const char* key, const char* fallback) {
  if (!g_open) return String(fallback);
  return prefs.getString(key, fallback);
}

bool NvsStore::putString(const char* key, const String& value) {
  if (!g_open) return false;
  return prefs.putString(key, value.c_str()) > 0;
}

bool NvsStore::eraseKey(const char* key) {
  if (!g_open) return false;
  return prefs.remove(key);
}

bool NvsStore::hasKey(const char* key) {
  if (!g_open) return false;
  return prefs.isKey(key);
}

}  // namespace stkchan
```

- [ ] **Step 3: Wire into `main.cpp` for smoke test**

Append to `setup()` in `src/main.cpp`:

```cpp
#include "services/NvsStore.h"

// ... inside setup(), after Serial.println("=== Stack-chan v1 boot ===");
if (!stkchan::nvs.begin()) {
  Serial.println("WARN: NVS open failed");
} else {
  Serial.printf("NVS chat_host = '%s'\n",
                stkchan::nvs.getString(stkchan::kNvsChatHost, "(unset)").c_str());
}
```

- [ ] **Step 4: Build and flash; verify serial output**

```bash
pio run -t upload && pio device monitor
```

Expected serial: `NVS chat_host = '(unset)'` (or whatever value is in NVS).

- [ ] **Step 5: Commit**

```bash
git add src/services/NvsStore.h src/services/NvsStore.cpp src/main.cpp
git commit -m "feat(nvs): wrapper for stkchan-namespace preferences"
```

---

## Task 4: `WifiManager` — cherry-pick from Jarvis

**Files:**
- Create: `src/net/WifiManager.h`
- Create: `src/net/WifiManager.cpp`
- Reference: `/home/jarod7736/workspace/Project-Jarvis/src/net/WiFiManager.h` and `.cpp`

**Goal:** Up-to-3 slots, `WiFiMulti.run(500)` for fast failover, slot-priority (Jarvis PR #20), `configTzTime` after NTP sync. Reads creds from `NvsStore`.

- [ ] **Step 1: Read Jarvis's `WiFiManager.{h,cpp}`** to understand the slot-priority pattern.

```bash
less /home/jarod7736/workspace/Project-Jarvis/src/net/WiFiManager.h
less /home/jarod7736/workspace/Project-Jarvis/src/net/WiFiManager.cpp
```

- [ ] **Step 2: Copy as a starting point and rename**

```bash
cp /home/jarod7736/workspace/Project-Jarvis/src/net/WiFiManager.h  src/net/WifiManager.h
cp /home/jarod7736/workspace/Project-Jarvis/src/net/WiFiManager.cpp src/net/WifiManager.cpp
```

- [ ] **Step 3: Adapt to `stkchan` namespace and NVS keys**

In both files, apply these substitutions:

- `namespace jarvis` → `namespace stkchan`
- Any include of `app/NVSConfig.h` → `services/NvsStore.h`
- Any reference to `kNvsSsid*`, `kNvsPsk*` constants → use `stkchan::kNvsSsid1` etc. from `config.h`
- Drop any Jarvis-specific code paths (HA-related, MQTT-related) — Stack-chan v1 doesn't use them.

Keep the slot-priority + `run(500)` + `configTzTime` behavior intact.

- [ ] **Step 4: Wire into `main.cpp`**

```cpp
#include "net/WifiManager.h"

// in setup(), after nvs.begin():
stkchan::wifi.begin();  // non-blocking start

// in loop(), before delay():
stkchan::wifi.tick();
```

- [ ] **Step 5: Build, flash, verify serial shows WiFi join attempts**

Expected: serial logs slot-by-slot WiFi attempts; if no creds in NVS, logs "no creds, skipping" and returns to loop.

- [ ] **Step 6: Commit**

```bash
git add src/net/WifiManager.h src/net/WifiManager.cpp src/main.cpp
git commit -m "feat(wifi): slot-priority WiFiMulti from Jarvis pattern"
```

---

## Task 5: `ConnectivityTier` — simplified for always-LAN

**Files:**
- Create: `src/net/ConnectivityTier.h`
- Create: `src/net/ConnectivityTier.cpp`

**Goal:** Spec §6 "connectivity-tier branching." Three states: `LAN_OK`, `LAN_NO_BACKEND`, `NO_WIFI`. Re-probed every 30 s in IDLE, never during a turn.

- [ ] **Step 1: Write `src/net/ConnectivityTier.h`**

```cpp
#pragma once
#include <stdint.h>

namespace stkchan {

enum class Tier {
  LAN_OK,           // WiFi up and chat host responding
  LAN_NO_BACKEND,   // WiFi up, chat host unreachable
  NO_WIFI,
};

class ConnectivityTier {
 public:
  void begin();
  void tick(uint32_t nowMs);
  Tier current() const { return tier_; }
  uint32_t lastProbeAgeMs(uint32_t nowMs) const { return nowMs - lastProbeMs_; }

 private:
  Tier     tier_         = Tier::NO_WIFI;
  uint32_t lastProbeMs_  = 0;
  bool probeBackend_();
};

extern ConnectivityTier connectivity;

}  // namespace stkchan
```

- [ ] **Step 2: Write `src/net/ConnectivityTier.cpp`**

```cpp
#include "net/ConnectivityTier.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"
#include "services/NvsStore.h"

namespace stkchan {

ConnectivityTier connectivity;

void ConnectivityTier::begin() {
  tier_ = Tier::NO_WIFI;
  lastProbeMs_ = 0;
}

void ConnectivityTier::tick(uint32_t nowMs) {
  if (nowMs - lastProbeMs_ < kTierProbeIntervalMs && lastProbeMs_ != 0) return;
  lastProbeMs_ = nowMs;

  if (WiFi.status() != WL_CONNECTED) {
    tier_ = Tier::NO_WIFI;
    return;
  }
  tier_ = probeBackend_() ? Tier::LAN_OK : Tier::LAN_NO_BACKEND;
}

bool ConnectivityTier::probeBackend_() {
  String host = nvs.getString(kNvsChatHost, "");
  if (host.isEmpty()) return false;

  HTTPClient http;
  // Ollama exposes /api/tags for a cheap reachability probe.
  String url = host + "/api/tags";
  http.setConnectTimeout(1500);
  http.setTimeout(2000);
  if (!http.begin(url)) return false;
  int code = http.GET();
  http.end();
  return code > 0 && code < 500;
}

}  // namespace stkchan
```

- [ ] **Step 3: Wire into `main.cpp`**

```cpp
#include "net/ConnectivityTier.h"

// in setup():
stkchan::connectivity.begin();

// in loop():
stkchan::connectivity.tick(millis());
```

- [ ] **Step 4: Build, flash, monitor; without WiFi, expect `NO_WIFI`; with WiFi but no Ollama, `LAN_NO_BACKEND`; with both, `LAN_OK`**

Add a transient debug print to verify:

```cpp
static stkchan::Tier lastTier = stkchan::Tier::NO_WIFI;
auto t = stkchan::connectivity.current();
if (t != lastTier) {
  Serial.printf("tier -> %d\n", (int)t);
  lastTier = t;
}
```

Remove the debug print before committing.

- [ ] **Step 5: Commit**

```bash
git add src/net/ConnectivityTier.h src/net/ConnectivityTier.cpp src/main.cpp
git commit -m "feat(net): tiered connectivity probe (LAN_OK / NO_BACKEND / NO_WIFI)"
```

---

## Task 6: `CaptivePortal` — cherry-pick + strip Jarvis-only fields

**Files:**
- Create: `src/services/CaptivePortal.h`
- Create: `src/services/CaptivePortal.cpp`
- Create: `data/web/` (LittleFS UI files trimmed from Jarvis)
- Reference: `/home/jarod7736/workspace/Project-Jarvis/src/net/CaptivePortal.{h,cpp}` and `data/web/`

**Goal:** First-run AP at `192.168.4.1` for WiFi + chat/STT/TTS keys. Trim Jarvis-only fields: no HA, MQTT, Anthropic, LLM-module UART.

- [ ] **Step 1: Read Jarvis's CaptivePortal sources to learn the API surface**

```bash
less /home/jarod7736/workspace/Project-Jarvis/src/net/CaptivePortal.h
less /home/jarod7736/workspace/Project-Jarvis/src/net/CaptivePortal.cpp
ls    /home/jarod7736/workspace/Project-Jarvis/data/web/
```

- [ ] **Step 2: Copy the C++ files**

```bash
cp /home/jarod7736/workspace/Project-Jarvis/src/net/CaptivePortal.h  src/services/CaptivePortal.h
cp /home/jarod7736/workspace/Project-Jarvis/src/net/CaptivePortal.cpp src/services/CaptivePortal.cpp
```

- [ ] **Step 3: Apply the namespace + NVS adaptation**

- `namespace jarvis` → `namespace stkchan`
- Include `services/NvsStore.h` instead of `app/NVSConfig.h`
- Use `stkchan::kNvs*` from `config.h`
- **Delete** any HTML form fields and POST handlers for: `ha_token`, `ha_host`, `mqtt_*`, `anth_key`, `oc_host`, `llm_uart_*`, `tts_provider_push`/`tts_voice_push` (Stack-chan v1 has no per-source routing). Keep only fields that map to `stkchan::kNvs*` keys.

- [ ] **Step 4: Copy and trim the LittleFS UI**

```bash
mkdir -p data/web
cp -r /home/jarod7736/workspace/Project-Jarvis/data/web/* data/web/
```

Edit `data/web/index.html` (or whichever template Jarvis uses) — delete the HA, MQTT, Anthropic, OpenClaw, and LLM-module sections. Keep WiFi (3 slots), `chat_host`, `chat_model`, `stt_url`, `stt_model`, `oai_key`, `tts_provider`, `tts_voice`, `tts_model`, `el_key`, `ota_pass`, `persona`.

- [ ] **Step 5: Wire into `main.cpp` (run only if no WiFi creds saved)**

```cpp
#include "services/CaptivePortal.h"

// in setup(), after nvs.begin():
if (stkchan::nvs.getString(stkchan::kNvsSsid1, "").isEmpty()) {
  Serial.println("No WiFi creds — entering captive portal");
  stkchan::portal.begin();
}

// in loop():
stkchan::portal.tick();
```

- [ ] **Step 6: Build, upload firmware, upload filesystem**

```bash
pio run -t upload
pio run -t uploadfs
pio device monitor
```

Expected: device boots into AP `Stackchan-XXXX`; phone joins; browser loads the trimmed config page; all listed fields submit and write to NVS.

- [ ] **Step 7: Commit**

```bash
git add src/services/CaptivePortal.* data/web/ src/main.cpp
git commit -m "feat(provisioning): captive portal for Stack-chan NVS keys"
```

---

## Task 7: `OtaService` — cherry-pick from Jarvis

**Files:**
- Create: `src/services/OtaService.h`
- Create: `src/services/OtaService.cpp`
- Reference: `/home/jarod7736/workspace/Project-Jarvis/src/net/OtaService.{h,cpp}`

**Goal:** ArduinoOTA wrapper that requires NVS `ota_pass`. Spec calls this a load-bearing dependency.

- [ ] **Step 1: Copy**

```bash
cp /home/jarod7736/workspace/Project-Jarvis/src/net/OtaService.h  src/services/OtaService.h
cp /home/jarod7736/workspace/Project-Jarvis/src/net/OtaService.cpp src/services/OtaService.cpp
```

- [ ] **Step 2: Rename namespace + NVS include** (same edits as Task 4).

- [ ] **Step 3: Wire into `main.cpp` after WiFi connects (not during boot)**

```cpp
#include "services/OtaService.h"

// in loop(), after wifi.tick():
stkchan::ota.tick();   // tick handles "begin once connected" internally
```

- [ ] **Step 4: Build, flash, then verify OTA is discoverable**

```bash
arp -a | grep stackchan   # or check your router for mDNS
ping stackchan.local
```

Expected: `stackchan.local` resolves after WiFi join.

- [ ] **Step 5: Commit**

```bash
git add src/services/OtaService.* src/main.cpp
git commit -m "feat(ota): ArduinoOTA wrapper requiring NVS ota_pass"
```

---

## Task 8: `AudioPlayer` — cherry-pick from Jarvis

**Files:**
- Create: `src/hal/AudioPlayer.h`
- Create: `src/hal/AudioPlayer.cpp`
- Reference: `/home/jarod7736/workspace/Project-Jarvis/src/hal/AudioPlayer.{h,cpp}`

**Goal:** MP3 → I²S out, single 256 KB PSRAM buffer, `onPlayDone` callback. Spec invariant: `SPEAKING → IDLE` driven only by `onPlayDone`.

- [ ] **Step 1: Copy**

```bash
cp /home/jarod7736/workspace/Project-Jarvis/src/hal/AudioPlayer.h  src/hal/AudioPlayer.h
cp /home/jarod7736/workspace/Project-Jarvis/src/hal/AudioPlayer.cpp src/hal/AudioPlayer.cpp
```

- [ ] **Step 2: Rename namespace** (same as before; no NVS dependency here typically).

- [ ] **Step 3: Verify the 256 KB cap is in place**

Open `AudioPlayer.cpp`, find the MP3 buffer alloc. It should be `kMp3MaxBytes` (256 KB) — pull from `stkchan::kMp3MaxBytes` in `config.h`. If Jarvis hardcoded 64 KB or 256 KB, replace with the constant.

- [ ] **Step 4: Smoke test with a hardcoded MP3 URL (skip if pulling cleanly from Jarvis)**

Add a one-shot in `setup()` (delete after verifying):

```cpp
#include "hal/AudioPlayer.h"
// ... after WiFi connects:
stkchan::audio.playUrl("http://example.com/test.mp3", []() {
  Serial.println("playback done");
});
```

Expected: speaker emits MP3 content; `playback done` logs.

- [ ] **Step 5: Remove the smoke-test code; commit**

```bash
git add src/hal/AudioPlayer.*
git commit -m "feat(audio): MP3 → I²S player with onPlayDone callback"
```

---

## Task 9: `TtsClient` — cherry-pick from Jarvis

**Files:**
- Create: `src/net/TtsClient.h`
- Create: `src/net/TtsClient.cpp`
- Reference: `/home/jarod7736/workspace/Project-Jarvis/src/net/TtsClient.{h,cpp}`

**Goal:** Synthesize a text string into MP3 via OpenAI or ElevenLabs (`tts_provider` NVS key), hand off to `AudioPlayer`. `WiFiClientSecure::setInsecure()`. Every exit path closes the HTTP client.

- [ ] **Step 1: Copy**

```bash
cp /home/jarod7736/workspace/Project-Jarvis/src/net/TtsClient.h  src/net/TtsClient.h
cp /home/jarod7736/workspace/Project-Jarvis/src/net/TtsClient.cpp src/net/TtsClient.cpp
```

- [ ] **Step 2: Adapt**

- Rename namespace.
- Replace any Jarvis NVS keys (e.g. `oai_tts_key`) with `stkchan::kNvsOaiKey`, `kNvsElKey`, `kNvsTtsProv`, `kNvsTtsVoice`, `kNvsTtsModel`.
- **Remove per-source routing** (Jarvis PR #43 introduced proactive-vs-reactive provider routing; Stack-chan v1 has one provider). Simplify the API to `synth(const String& text, std::function<void(bool ok)> onDone)`.

- [ ] **Step 3: Confirm `http.end()` is on every exit path**

Open `TtsClient.cpp` and walk every `return` inside the synth function. Each must call `http.end()` first (or be in a scope where it's already called). If you find a leak, fix it.

- [ ] **Step 4: Smoke test from `main.cpp`**

After WiFi connects, in a one-shot, call:

```cpp
stkchan::tts.synth("Hello from Stack-chan", [](bool ok) {
  Serial.printf("TTS done ok=%d\n", ok);
});
```

Expected: speaker says "Hello from Stack-chan" after 1–2 s.

- [ ] **Step 5: Remove smoke-test code; commit**

```bash
git add src/net/TtsClient.* src/main.cpp
git commit -m "feat(tts): cloud TTS router (OpenAI / ElevenLabs)"
```

---

## Task 10: `MicRecorder` — NEW

**Files:**
- Create: `src/hal/MicRecorder.h`
- Create: `src/hal/MicRecorder.cpp`

**Goal:** Press-to-talk WAV capture into PSRAM. 16 kHz mono 16-bit. 6 s cap. WAV header prepended.

- [ ] **Step 1: Write `src/hal/MicRecorder.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace stkchan {

class MicRecorder {
 public:
  bool begin();             // allocates PSRAM buffer
  bool start();             // returns false if already recording
  bool stop();              // returns false if not recording
  bool isActive() const { return active_; }

  // After stop(), these point at a WAV-formatted buffer in PSRAM.
  // Valid until the next start().
  const uint8_t* wavData() const { return wav_; }
  size_t         wavSize() const { return wavSize_; }

 private:
  uint8_t* wav_      = nullptr;   // WAV header + PCM samples
  size_t   wavCap_   = 0;
  size_t   wavSize_  = 0;
  bool     active_   = false;
  uint32_t startMs_  = 0;

  void writeWavHeader_(uint32_t sampleCount);
};

extern MicRecorder mic;

}  // namespace stkchan
```

- [ ] **Step 2: Write `src/hal/MicRecorder.cpp`**

```cpp
#include "hal/MicRecorder.h"
#include <M5CoreS3.h>
#include "config.h"

namespace stkchan {

MicRecorder mic;

static constexpr size_t kHeaderBytes = 44;  // RIFF / WAVE / fmt / data

bool MicRecorder::begin() {
  if (wav_) return true;
  wavCap_ = kRecordMaxBytes + kHeaderBytes;
  wav_ = static_cast<uint8_t*>(ps_malloc(wavCap_));
  if (!wav_) {
    Serial.println("ERR: MicRecorder ps_malloc failed");
    wavCap_ = 0;
    return false;
  }
  return true;
}

bool MicRecorder::start() {
  if (!wav_ || active_) return false;
  if (!M5.Mic.begin()) {
    Serial.println("ERR: M5.Mic.begin failed");
    return false;
  }
  M5.Mic.setSampleRate(kRecordSampleRate);
  active_ = true;
  startMs_ = millis();
  wavSize_ = kHeaderBytes;  // reserve header; filled in stop()
  return true;
}

bool MicRecorder::stop() {
  if (!active_) return false;

  // Drain whatever's already been recorded into PCM bytes after the header.
  // M5.Mic.record() reads into a target buffer; here we pull from its
  // internal ring buffer until empty.
  size_t  pcmRoom = wavCap_ - wavSize_;
  int16_t* dst = reinterpret_cast<int16_t*>(wav_ + wavSize_);
  size_t  capSamples = pcmRoom / sizeof(int16_t);
  size_t  got = M5.Mic.record(dst, capSamples, kRecordSampleRate);
  wavSize_ += got * sizeof(int16_t);

  M5.Mic.end();
  uint32_t pcmBytes = wavSize_ - kHeaderBytes;
  uint32_t sampleCount = pcmBytes / sizeof(int16_t);
  writeWavHeader_(sampleCount);
  active_ = false;
  Serial.printf("MicRecorder: %u samples (%u ms), %u bytes WAV\n",
                (unsigned)sampleCount,
                (unsigned)(sampleCount * 1000 / kRecordSampleRate),
                (unsigned)wavSize_);
  return true;
}

void MicRecorder::writeWavHeader_(uint32_t sampleCount) {
  uint32_t dataBytes = sampleCount * sizeof(int16_t);
  uint32_t fileBytes = dataBytes + kHeaderBytes - 8;

  // RIFF
  memcpy(wav_ + 0, "RIFF", 4);
  memcpy(wav_ + 4, &fileBytes, 4);
  memcpy(wav_ + 8, "WAVE", 4);
  // fmt
  memcpy(wav_ + 12, "fmt ", 4);
  uint32_t fmtChunkSize = 16;
  memcpy(wav_ + 16, &fmtChunkSize, 4);
  uint16_t audioFormat = 1;        // PCM
  uint16_t numChannels = 1;
  uint32_t sampleRate  = kRecordSampleRate;
  uint16_t bitsPerSamp = 16;
  uint32_t byteRate    = sampleRate * numChannels * (bitsPerSamp / 8);
  uint16_t blockAlign  = numChannels * (bitsPerSamp / 8);
  memcpy(wav_ + 20, &audioFormat, 2);
  memcpy(wav_ + 22, &numChannels, 2);
  memcpy(wav_ + 24, &sampleRate,  4);
  memcpy(wav_ + 28, &byteRate,    4);
  memcpy(wav_ + 32, &blockAlign,  2);
  memcpy(wav_ + 34, &bitsPerSamp, 2);
  // data
  memcpy(wav_ + 36, "data", 4);
  memcpy(wav_ + 40, &dataBytes, 4);
}

}  // namespace stkchan
```

- [ ] **Step 3: Smoke test from `main.cpp`**

```cpp
#include "hal/MicRecorder.h"
// in setup(): stkchan::mic.begin();

// in loop(), tap the screen to test:
M5.update();
if (M5.Touch.getCount() && !stkchan::mic.isActive()) {
  stkchan::mic.start();
} else if (stkchan::mic.isActive() && !M5.Touch.getCount()) {
  stkchan::mic.stop();
}
```

Tap and hold the screen for 2 s, release. Expected serial: `MicRecorder: 32000 samples (2000 ms), 64044 bytes WAV`.

- [ ] **Step 4: Remove the smoke-test code; commit**

```bash
git add src/hal/MicRecorder.*
git commit -m "feat(mic): press-to-talk WAV capture into PSRAM"
```

---

## Task 11: `SttClient` — Whisper API

**Files:**
- Create: `src/net/SttClient.h`
- Create: `src/net/SttClient.cpp`

**Goal:** POST a WAV buffer as `multipart/form-data` to `https://api.openai.com/v1/audio/transcriptions`, return the `text` field.

- [ ] **Step 1: Write `src/net/SttClient.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace stkchan {

class SttClient {
 public:
  // Returns true and fills `out` on success, false on any failure.
  bool transcribe(const uint8_t* wavData, size_t wavSize, String& out);
};

extern SttClient stt;

}  // namespace stkchan
```

- [ ] **Step 2: Write `src/net/SttClient.cpp`**

```cpp
#include "net/SttClient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"
#include "services/NvsStore.h"

namespace stkchan {

SttClient stt;

static constexpr const char* kBoundary = "----StackchanFormBoundary";

bool SttClient::transcribe(const uint8_t* wavData, size_t wavSize, String& out) {
  out = "";
  if (!wavData || wavSize == 0) return false;

  String url    = nvs.getString(kNvsSttUrl,
                                "https://api.openai.com/v1/audio/transcriptions");
  String model  = nvs.getString(kNvsSttModel, kDefaultSttModel);
  String apiKey = nvs.getString(kNvsOaiKey, "");
  if (apiKey.isEmpty()) {
    Serial.println("ERR: stt no oai_key in NVS");
    return false;
  }

  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  if (!http.begin(tls, url)) {
    Serial.println("ERR: stt http.begin failed");
    return false;
  }
  http.setTimeout(kSttTimeoutMs);
  http.addHeader("Authorization", "Bearer " + apiKey);
  String ctype = String("multipart/form-data; boundary=") + kBoundary;
  http.addHeader("Content-Type", ctype);

  // Build the multipart body in PSRAM. Body = preamble + WAV + epilogue.
  String preamble;
  preamble.reserve(256);
  preamble += "--"; preamble += kBoundary; preamble += "\r\n";
  preamble += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  preamble += model;
  preamble += "\r\n--"; preamble += kBoundary; preamble += "\r\n";
  preamble += "Content-Disposition: form-data; name=\"file\"; "
              "filename=\"audio.wav\"\r\n";
  preamble += "Content-Type: audio/wav\r\n\r\n";

  String epilogue = "\r\n--";
  epilogue += kBoundary;
  epilogue += "--\r\n";

  size_t bodyLen = preamble.length() + wavSize + epilogue.length();
  uint8_t* body = static_cast<uint8_t*>(ps_malloc(bodyLen));
  if (!body) {
    http.end();
    Serial.println("ERR: stt ps_malloc body failed");
    return false;
  }
  size_t off = 0;
  memcpy(body + off, preamble.c_str(), preamble.length()); off += preamble.length();
  memcpy(body + off, wavData, wavSize);                    off += wavSize;
  memcpy(body + off, epilogue.c_str(), epilogue.length()); off += epilogue.length();

  int code = http.sendRequest("POST", body, bodyLen);
  free(body);

  if (code != 200) {
    Serial.printf("ERR: stt HTTP %d\n", code);
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();

  // Response is JSON: { "text": "..." }
  DynamicJsonDocument doc(8 * 1024);
  if (deserializeJson(doc, resp)) {
    Serial.println("ERR: stt JSON parse failed");
    return false;
  }
  out = doc["text"].as<String>();
  return !out.isEmpty();
}

}  // namespace stkchan
```

- [ ] **Step 3: Smoke test (record → STT → print)**

In `loop()`, on touch-release, drive: `mic.stop()` → `stt.transcribe(...)` → log result.

- [ ] **Step 4: Verify on hardware**

Press screen, say "test one two three," release. Expected serial: `STT: test one two three`.

- [ ] **Step 5: Remove smoke-test code; commit**

```bash
git add src/net/SttClient.*
git commit -m "feat(stt): Whisper API client"
```

---

## Task 12: `ResponseParser` — TDD with native unit tests

**Files:**
- Create: `src/persona/ResponseParser.h`
- Create: `src/persona/ResponseParser.cpp`
- Create: `test/test_response_parser/test_response_parser.cpp`

**Goal:** Defensive parser for `<speech>…</speech><expr>…</expr>` (spec §7). Pure logic — TDD natively.

- [ ] **Step 1: Write the failing tests**

```cpp
// test/test_response_parser/test_response_parser.cpp
#include <unity.h>
#include <string>
#include "persona/ResponseParser.h"

using stkchan::ParsedReply;
using stkchan::parseReply;

void test_basic_happy_case() {
  auto p = parseReply("<speech>Hello!</speech><expr>happy</expr>");
  TEST_ASSERT_EQUAL_STRING("Hello!", p.speech.c_str());
  TEST_ASSERT_EQUAL_STRING("happy",  p.expr.c_str());
  TEST_ASSERT_TRUE(p.ok);
}

void test_strip_backtick_fence() {
  auto p = parseReply("```\n<speech>Hi</speech><expr>neutral</expr>\n```");
  TEST_ASSERT_EQUAL_STRING("Hi", p.speech.c_str());
  TEST_ASSERT_EQUAL_STRING("neutral", p.expr.c_str());
}

void test_missing_expr_defaults_neutral() {
  auto p = parseReply("<speech>okay</speech>");
  TEST_ASSERT_EQUAL_STRING("okay",    p.speech.c_str());
  TEST_ASSERT_EQUAL_STRING("neutral", p.expr.c_str());
}

void test_missing_speech_uses_raw_text() {
  auto p = parseReply("just some words");
  TEST_ASSERT_EQUAL_STRING("just some words", p.speech.c_str());
  TEST_ASSERT_EQUAL_STRING("neutral",         p.expr.c_str());
}

void test_unknown_expr_falls_back_neutral() {
  auto p = parseReply("<speech>hey</speech><expr>elated</expr>");
  TEST_ASSERT_EQUAL_STRING("neutral", p.expr.c_str());
}

void test_collapses_whitespace_in_speech() {
  auto p = parseReply("<speech>\n  hello\n  world\n</speech>");
  TEST_ASSERT_EQUAL_STRING("hello world", p.speech.c_str());
}

void test_lowercases_expr() {
  auto p = parseReply("<speech>hi</speech><expr>HAPPY</expr>");
  TEST_ASSERT_EQUAL_STRING("happy", p.expr.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_basic_happy_case);
  RUN_TEST(test_strip_backtick_fence);
  RUN_TEST(test_missing_expr_defaults_neutral);
  RUN_TEST(test_missing_speech_uses_raw_text);
  RUN_TEST(test_unknown_expr_falls_back_neutral);
  RUN_TEST(test_collapses_whitespace_in_speech);
  RUN_TEST(test_lowercases_expr);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the tests; verify they fail**

```bash
pio test -e native -f test_response_parser
```

Expected: build error (`ResponseParser.h` not found).

- [ ] **Step 3: Write `src/persona/ResponseParser.h`**

```cpp
#pragma once
#include <string>

namespace stkchan {

struct ParsedReply {
  std::string speech;   // never empty for a non-empty input
  std::string expr;     // always one of: neutral, happy, sad, angry, doubt, sleepy
  bool        ok;       // true if both tags parsed cleanly
};

ParsedReply parseReply(const std::string& raw);

bool isValidExpr(const std::string& tag);

}  // namespace stkchan
```

- [ ] **Step 4: Write `src/persona/ResponseParser.cpp`**

```cpp
#include "persona/ResponseParser.h"
#include <algorithm>
#include <cctype>

namespace stkchan {

static const std::string kValidExprs[] = {
  "neutral", "happy", "sad", "angry", "doubt", "sleepy",
};

bool isValidExpr(const std::string& tag) {
  for (const auto& v : kValidExprs) if (v == tag) return true;
  return false;
}

static std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

static std::string stripFences(std::string s) {
  // Drop any leading ```lang fence and trailing ``` fence.
  auto npos = std::string::npos;
  auto open = s.find("```");
  if (open != npos) {
    auto eol = s.find('\n', open);
    if (eol != npos) s.erase(open, eol - open + 1);
  }
  auto close = s.rfind("```");
  if (close != npos) s.erase(close, 3);
  return s;
}

static std::string trim(std::string s) {
  auto isSpace = [](unsigned char c) { return std::isspace(c); };
  s.erase(s.begin(),
          std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !isSpace(c); }));
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [&](unsigned char c){ return !isSpace(c); }).base(),
          s.end());
  return s;
}

static std::string collapseWhitespace(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool prevSpace = false;
  for (char c : s) {
    bool sp = std::isspace(static_cast<unsigned char>(c));
    if (sp) {
      if (!prevSpace && !out.empty()) out.push_back(' ');
      prevSpace = true;
    } else {
      out.push_back(c);
      prevSpace = false;
    }
  }
  return trim(out);
}

static bool extractTag(const std::string& s,
                       const std::string& tag,
                       std::string& out) {
  std::string open  = "<"  + tag + ">";
  std::string close = "</" + tag + ">";
  auto a = s.find(open);
  if (a == std::string::npos) return false;
  a += open.size();
  auto b = s.find(close, a);
  if (b == std::string::npos) return false;
  out = s.substr(a, b - a);
  return true;
}

ParsedReply parseReply(const std::string& raw) {
  ParsedReply r;
  std::string s = stripFences(trim(raw));

  std::string speechRaw, exprRaw;
  bool hasSpeech = extractTag(s, "speech", speechRaw);
  bool hasExpr   = extractTag(s, "expr",   exprRaw);

  if (hasSpeech) {
    r.speech = collapseWhitespace(speechRaw);
  } else {
    r.speech = collapseWhitespace(s);
  }
  if (hasExpr) {
    std::string lowered = toLower(trim(exprRaw));
    r.expr = isValidExpr(lowered) ? lowered : "neutral";
  } else {
    r.expr = "neutral";
  }
  r.ok = hasSpeech && hasExpr && isValidExpr(toLower(trim(exprRaw)));
  return r;
}

}  // namespace stkchan
```

- [ ] **Step 5: Run tests; verify they pass**

```bash
pio test -e native -f test_response_parser
```

Expected: 7/7 PASS.

- [ ] **Step 6: Build the embedded target too (no regression)**

```bash
pio run
```

- [ ] **Step 7: Commit**

```bash
git add src/persona/ResponseParser.* test/test_response_parser/
git commit -m "feat(persona): defensive <speech>/<expr> parser with native tests"
```

---

## Task 13: `ChatClient` — Ollama

**Files:**
- Create: `src/net/ChatClient.h`
- Create: `src/net/ChatClient.cpp`

**Goal:** POST to `<chat_host>/api/chat` with a system prompt + ring-buffered history + the new user turn. Plain HTTP (LAN). Returns the assistant's content string.

- [ ] **Step 1: Write `src/net/ChatClient.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <array>
#include <string>
#include "config.h"

namespace stkchan {

struct Turn {
  String role;     // "user" | "assistant"
  String content;
};

class ChatClient {
 public:
  void setSystemPrompt(const String& sp) { system_ = sp; }
  void clearHistory();

  // Sends `userMsg`. On success returns true and fills `out` with the
  // assistant's content. On any failure returns false.
  bool send(const String& userMsg, String& out);

 private:
  String system_;
  std::array<Turn, kHistoryTurns * 2> ring_{};
  size_t head_  = 0;        // next write index
  size_t count_ = 0;
  void push_(const String& role, const String& content);
};

extern ChatClient chat;

}  // namespace stkchan
```

- [ ] **Step 2: Write `src/net/ChatClient.cpp`**

```cpp
#include "net/ChatClient.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "services/NvsStore.h"

namespace stkchan {

ChatClient chat;

void ChatClient::clearHistory() {
  for (auto& t : ring_) { t.role = ""; t.content = ""; }
  head_ = 0;
  count_ = 0;
}

void ChatClient::push_(const String& role, const String& content) {
  ring_[head_] = Turn{role, content};
  head_ = (head_ + 1) % ring_.size();
  if (count_ < ring_.size()) ++count_;
}

bool ChatClient::send(const String& userMsg, String& out) {
  out = "";
  String host  = nvs.getString(kNvsChatHost, "");
  String model = nvs.getString(kNvsChatModel, kDefaultChatModel);
  if (host.isEmpty()) return false;

  HTTPClient http;
  String url = host + "/api/chat";
  if (!http.begin(url)) return false;
  http.setTimeout(kChatTimeoutMs);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument req(16 * 1024);
  req["model"] = model;
  req["stream"] = false;
  JsonArray msgs = req.createNestedArray("messages");
  if (!system_.isEmpty()) {
    JsonObject sys = msgs.createNestedObject();
    sys["role"] = "system";
    sys["content"] = system_;
  }
  // Replay history in chronological order.
  size_t idx = (head_ + ring_.size() - count_) % ring_.size();
  for (size_t i = 0; i < count_; ++i) {
    const Turn& t = ring_[(idx + i) % ring_.size()];
    JsonObject m = msgs.createNestedObject();
    m["role"]    = t.role;
    m["content"] = t.content;
  }
  JsonObject usr = msgs.createNestedObject();
  usr["role"]    = "user";
  usr["content"] = userMsg;

  String body;
  serializeJson(req, body);
  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("ERR: chat HTTP %d\n", code);
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();

  DynamicJsonDocument doc(16 * 1024);
  if (deserializeJson(doc, resp)) {
    Serial.println("ERR: chat JSON parse failed");
    return false;
  }
  out = doc["message"]["content"].as<String>();
  if (out.isEmpty()) return false;

  push_("user", userMsg);
  push_("assistant", out);
  return true;
}

}  // namespace stkchan
```

- [ ] **Step 3: Wire smoke test — set a temporary system prompt, send a fixed message**

```cpp
// in setup, after WiFi:
stkchan::chat.setSystemPrompt("You are Stack-chan. Reply briefly.");
String reply;
if (stkchan::chat.send("Say hi.", reply)) {
  Serial.printf("CHAT: %s\n", reply.c_str());
}
```

Expected serial: a short reply from gemma3n.

- [ ] **Step 4: Remove smoke-test code; commit**

```bash
git add src/net/ChatClient.*
git commit -m "feat(chat): Ollama /api/chat client with 6-turn ring buffer"
```

---

## Task 14: Persona prompt + few-shots

**Files:**
- Create: `src/persona/SystemPrompt.h`
- Create: `src/prompts/persona_examples.h`

**Goal:** Embed the spec's persona prompt + 4 few-shot examples. Single header each, no .cpp.

- [ ] **Step 1: Write `src/persona/SystemPrompt.h`**

```cpp
#pragma once
namespace stkchan {

constexpr const char* kDefaultPersona =
  "You are Stack-chan, a small kawaii desk robot. You live on Jarod's desk.\n"
  "You're playful, curious, a little sleepy in the morning, easily delighted.\n"
  "You speak in short sentences — 1 to 3 lines, almost never more.\n"
  "You don't have tools, calendar, email, or web access. If asked, say so cheerfully.\n"
  "You're not Jarvis; you're his quieter, dumber, cuter cousin and you know it.\n"
  "\n"
  "Every reply you produce MUST be exactly this format:\n"
  "<speech>...what you actually say out loud...</speech>\n"
  "<expr>one of: neutral, happy, sad, angry, doubt, sleepy</expr>\n"
  "\n"
  "Keep <speech> under ~30 words. Pick the <expr> that fits the speech.\n"
  "Never include <expr> inside <speech>. Never produce anything outside the two tags.";

}  // namespace stkchan
```

- [ ] **Step 2: Write `src/prompts/persona_examples.h`**

```cpp
#pragma once
namespace stkchan {

// Few-shot examples to anchor the format and the voice.
// Appended to the system prompt at boot.
constexpr const char* kPersonaExamples =
  "\n\nExamples:\n"
  "User: Hey little buddy.\n"
  "Assistant: <speech>Oh! Hi.</speech><expr>happy</expr>\n"
  "\n"
  "User: What's the weather like?\n"
  "Assistant: <speech>I don't know — I can't see outside.</speech><expr>doubt</expr>\n"
  "\n"
  "User: Tell me a joke.\n"
  "Assistant: <speech>Why don't robots get tired? We have no muscles to ache.</speech><expr>happy</expr>\n"
  "\n"
  "User: I'm going to bed.\n"
  "Assistant: <speech>Goodnight. I'll be here in the morning.</speech><expr>sleepy</expr>\n";

}  // namespace stkchan
```

- [ ] **Step 3: Build to verify includes resolve**

```bash
pio run
```

- [ ] **Step 4: Commit**

```bash
git add src/persona/SystemPrompt.h src/prompts/persona_examples.h
git commit -m "feat(persona): system prompt + few-shot examples"
```

---

## Task 15: `ExpressionMap` — TDD natively

**Files:**
- Create: `src/face/ExpressionMap.h`
- Create: `src/face/ExpressionMap.cpp`
- Create: `test/test_expression_map/test_expression_map.cpp`

**Goal:** Map a tag string ("happy", "sad", …) to: Avatar expression enum index, default yaw/pitch tilt offsets, idle-bob amplitude. Pure data — TDD natively.

- [ ] **Step 1: Write the failing tests**

```cpp
// test/test_expression_map/test_expression_map.cpp
#include <unity.h>
#include "face/ExpressionMap.h"

using stkchan::expressionFor;

void test_happy_has_positive_pitch_and_bigger_bob() {
  auto e = expressionFor("happy");
  TEST_ASSERT_EQUAL_INT(stkchan::AvatarExprIdx::Happy, e.idx);
  TEST_ASSERT_TRUE(e.pitchDeg > 0);
  TEST_ASSERT_TRUE(e.bobAmp > 1.0f);
}

void test_sad_droops() {
  auto e = expressionFor("sad");
  TEST_ASSERT_EQUAL_INT(stkchan::AvatarExprIdx::Sad, e.idx);
  TEST_ASSERT_TRUE(e.pitchDeg < 0);
}

void test_unknown_falls_back_to_neutral() {
  auto e = expressionFor("majestic");
  TEST_ASSERT_EQUAL_INT(stkchan::AvatarExprIdx::Neutral, e.idx);
  TEST_ASSERT_EQUAL_INT(0, e.pitchDeg);
}

void test_doubt_tilts_yaw() {
  auto e = expressionFor("doubt");
  TEST_ASSERT_EQUAL_INT(stkchan::AvatarExprIdx::Doubt, e.idx);
  TEST_ASSERT_TRUE(e.yawDeg != 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_happy_has_positive_pitch_and_bigger_bob);
  RUN_TEST(test_sad_droops);
  RUN_TEST(test_unknown_falls_back_to_neutral);
  RUN_TEST(test_doubt_tilts_yaw);
  return UNITY_END();
}
```

- [ ] **Step 2: Run; expect failure**

```bash
pio test -e native -f test_expression_map
```

- [ ] **Step 3: Write `src/face/ExpressionMap.h`**

```cpp
#pragma once
#include <string>

namespace stkchan {

// Indices match meganetaaan/M5Stack-Avatar Expression enum order:
//   Neutral=0, Happy=1, Sleepy=2, Doubt=3, Sad=4, Angry=5.
// Keep in sync with the Avatar library; verify by reading
// .pio/libdeps/cores3/M5Stack-Avatar/src/Expression.h if it ever drifts.
enum AvatarExprIdx {
  Neutral = 0,
  Happy   = 1,
  Sleepy  = 2,
  Doubt   = 3,
  Sad     = 4,
  Angry   = 5,
};

struct ExprMapping {
  int   idx;        // Avatar Expression index
  int   yawDeg;     // default yaw offset
  int   pitchDeg;   // default pitch offset
  float bobAmp;     // speech-bob amplitude scale (1.0 = baseline)
};

ExprMapping expressionFor(const std::string& tag);

}  // namespace stkchan
```

- [ ] **Step 4: Write `src/face/ExpressionMap.cpp`**

```cpp
#include "face/ExpressionMap.h"

namespace stkchan {

ExprMapping expressionFor(const std::string& tag) {
  if (tag == "happy")  return { AvatarExprIdx::Happy,    0,  5, 1.4f };
  if (tag == "sad")    return { AvatarExprIdx::Sad,      0, -10, 0.6f };
  if (tag == "angry")  return { AvatarExprIdx::Angry,    0,  0, 1.2f };
  if (tag == "doubt")  return { AvatarExprIdx::Doubt,  -15,  0, 1.0f };
  if (tag == "sleepy") return { AvatarExprIdx::Sleepy,   0, -5, 0.7f };
  // default / unknown
  return { AvatarExprIdx::Neutral, 0, 0, 1.0f };
}

}  // namespace stkchan
```

- [ ] **Step 5: Run tests; verify PASS**

```bash
pio test -e native -f test_expression_map
```

- [ ] **Step 6: Build embedded target**

```bash
pio run
```

- [ ] **Step 7: Commit**

```bash
git add src/face/ExpressionMap.* test/test_expression_map/
git commit -m "feat(face): expression-tag → Avatar idx + tilt + bob mapping"
```

---

## Task 16: `Face` — M5Stack-Avatar wrapper

**Files:**
- Create: `src/face/Face.h`
- Create: `src/face/Face.cpp`

**Goal:** Thin wrapper that owns an `Avatar` instance, lets the rest of the firmware say `face.setExpression("happy")` or `face.setMouthOpen(0.7f)`.

- [ ] **Step 1: Read the Avatar library API briefly**

```bash
ls /home/jarod7736/.platformio/lib/M5Stack-Avatar/src/ 2>/dev/null || \
 ls .pio/libdeps/cores3/M5Stack-Avatar/src/ 2>/dev/null
```

The library exposes `m5avatar::Avatar` with `init()`, `setExpression(Expression)`, `setMouthOpenRatio(float)`. Verify before continuing.

- [ ] **Step 2: Write `src/face/Face.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <string>

namespace stkchan {

class Face {
 public:
  void begin();                            // calls Avatar::init()
  void setExpression(const std::string& tag);
  void setMouthOpen(float ratio);          // 0.0 .. 1.0
  void blinkOnce();                        // optional convenience
  std::string currentExpression() const { return currentTag_; }

 private:
  std::string currentTag_ = "neutral";
};

extern Face face;

}  // namespace stkchan
```

- [ ] **Step 3: Write `src/face/Face.cpp`**

```cpp
#include "face/Face.h"
#include <M5CoreS3.h>
#include <Avatar.h>             // M5Stack-Avatar library
#include "face/ExpressionMap.h"

namespace stkchan {

Face face;
static m5avatar::Avatar g_avatar;

void Face::begin() {
  g_avatar.init();
  setExpression("neutral");
}

void Face::setExpression(const std::string& tag) {
  auto m = expressionFor(tag);
  currentTag_ = tag;
  g_avatar.setExpression(static_cast<m5avatar::Expression>(m.idx));
}

void Face::setMouthOpen(float ratio) {
  if (ratio < 0) ratio = 0;
  if (ratio > 1) ratio = 1;
  g_avatar.setMouthOpenRatio(ratio);
}

void Face::blinkOnce() {
  // Avatar lib handles blinking internally; expose a manual nudge if needed.
  g_avatar.setSpeechText("");  // no-op placeholder
}

}  // namespace stkchan
```

- [ ] **Step 4: Smoke test from `main.cpp`**

```cpp
#include "face/Face.h"
// in setup(): stkchan::face.begin();
// after a 2 s delay, cycle expressions:
stkchan::face.setExpression("happy");  delay(2000);
stkchan::face.setExpression("sad");    delay(2000);
stkchan::face.setExpression("doubt");  delay(2000);
stkchan::face.setExpression("neutral");
```

Expected: face on the LCD changes through all four states.

- [ ] **Step 5: Remove smoke-test code; commit**

```bash
git add src/face/Face.*
git commit -m "feat(face): Avatar wrapper with tag-based expression API"
```

---

## Task 17: `Servos` HAL — PCA9685 + ServoEasing

**Files:**
- Create: `src/hal/Servos.h`
- Create: `src/hal/Servos.cpp`

**Goal:** Two servos driven through the Stack-chan servo base's PCA9685. Yaw on channel 0, pitch on channel 1. Smooth motion via easing.

- [ ] **Step 1: Add ServoEasing to `lib_deps`** (if not already)

In `platformio.ini`:

```ini
lib_deps =
    ...
    arminjo/ServoEasing@^3.4.0
```

- [ ] **Step 2: Write `src/hal/Servos.h`**

```cpp
#pragma once
#include <Arduino.h>

namespace stkchan {

class Servos {
 public:
  bool begin();   // I²C init + PCA9685 init
  void setYaw  (int deg);                // hard-set
  void setPitch(int deg);
  void easeYawTo  (int deg, uint32_t durationMs);
  void easePitchTo(int deg, uint32_t durationMs);
  bool isMoving() const;

  // Call once per loop() to drive easing.
  void tick(uint32_t nowMs);

  int currentYaw()   const { return yawDeg_; }
  int currentPitch() const { return pitchDeg_; }

  // Yaw range: -45..+45, Pitch range: -25..+25 (mechanical limits).
  static constexpr int kYawMin    = -45;
  static constexpr int kYawMax    =  45;
  static constexpr int kPitchMin  = -25;
  static constexpr int kPitchMax  =  25;

 private:
  int yawDeg_   = 0;
  int pitchDeg_ = 0;
  struct Easing {
    bool active = false;
    int  from   = 0;
    int  to     = 0;
    uint32_t startMs = 0;
    uint32_t durMs   = 0;
  } eYaw_, ePitch_;

  static int clamp_(int v, int lo, int hi);
  void writeYaw_(int deg);
  void writePitch_(int deg);
};

extern Servos servos;

}  // namespace stkchan
```

- [ ] **Step 3: Write `src/hal/Servos.cpp`**

```cpp
#include "hal/Servos.h"
#include <Adafruit_PWMServoDriver.h>

namespace stkchan {

Servos servos;

static Adafruit_PWMServoDriver g_pwm = Adafruit_PWMServoDriver(0x40);

// PCA9685 PWM range for SG90: ~150 .. 600 counts at 50 Hz.
static int degToPwm(int deg) {
  // Map -90..+90 onto 150..600. Servo zero ≈ 375.
  long mapped = map(deg, -90, 90, 150, 600);
  return (int)mapped;
}

bool Servos::begin() {
  Wire.begin();
  if (!g_pwm.begin()) {
    Serial.println("ERR: PCA9685 init failed");
    return false;
  }
  g_pwm.setOscillatorFrequency(27000000);
  g_pwm.setPWMFreq(50);
  writeYaw_(0);
  writePitch_(0);
  return true;
}

int Servos::clamp_(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void Servos::writeYaw_(int deg) {
  deg = clamp_(deg, kYawMin, kYawMax);
  g_pwm.setPWM(0, 0, degToPwm(deg));
  yawDeg_ = deg;
}
void Servos::writePitch_(int deg) {
  deg = clamp_(deg, kPitchMin, kPitchMax);
  g_pwm.setPWM(1, 0, degToPwm(deg));
  pitchDeg_ = deg;
}

void Servos::setYaw  (int deg) { eYaw_.active   = false; writeYaw_(deg); }
void Servos::setPitch(int deg) { ePitch_.active = false; writePitch_(deg); }

void Servos::easeYawTo(int deg, uint32_t durMs) {
  eYaw_ = {true, yawDeg_, clamp_(deg, kYawMin, kYawMax), millis(), durMs};
}
void Servos::easePitchTo(int deg, uint32_t durMs) {
  ePitch_ = {true, pitchDeg_, clamp_(deg, kPitchMin, kPitchMax), millis(), durMs};
}

bool Servos::isMoving() const { return eYaw_.active || ePitch_.active; }

static int interp(int from, int to, uint32_t elapsed, uint32_t dur) {
  if (elapsed >= dur) return to;
  float t = (float)elapsed / (float)dur;
  // ease-in-out cubic
  float eased = t < 0.5f ? 4 * t * t * t
                          : 1 - powf(-2 * t + 2, 3) / 2;
  return from + (int)((to - from) * eased);
}

void Servos::tick(uint32_t nowMs) {
  if (eYaw_.active) {
    uint32_t e = nowMs - eYaw_.startMs;
    writeYaw_(interp(eYaw_.from, eYaw_.to, e, eYaw_.durMs));
    if (e >= eYaw_.durMs) eYaw_.active = false;
  }
  if (ePitch_.active) {
    uint32_t e = nowMs - ePitch_.startMs;
    writePitch_(interp(ePitch_.from, ePitch_.to, e, ePitch_.durMs));
    if (e >= ePitch_.durMs) ePitch_.active = false;
  }
}

}  // namespace stkchan
```

- [ ] **Step 4: Smoke test on hardware**

```cpp
#include "hal/Servos.h"
// in setup(): stkchan::servos.begin();
// after 2 s: stkchan::servos.easeYawTo(30, 800);
// later:    stkchan::servos.easePitchTo(-10, 600);
// in loop(): stkchan::servos.tick(millis());
```

Expected: head smoothly rotates right, then nods down. If reversed, swap signs or PCA9685 channels.

- [ ] **Step 5: Remove smoke-test code; commit**

```bash
git add src/hal/Servos.* platformio.ini
git commit -m "feat(servos): PCA9685 yaw/pitch with ease-in-out"
```

---

## Task 18: `MotionDirector` — idle, speech bob, expression coupling

**Files:**
- Create: `src/motion/MotionDirector.h`
- Create: `src/motion/MotionDirector.cpp`

**Goal:** A small actor that drives `Servos` based on FSM events. Idle saccades, speech-coupled bob, expression-coupled tilts.

- [ ] **Step 1: Write `src/motion/MotionDirector.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <string>

namespace stkchan {

class MotionDirector {
 public:
  void begin();
  void tick(uint32_t nowMs);   // call once per loop()

  void pauseIdle();
  void resumeIdle();

  void startSpeechBob(float amp);  // amp from ExpressionMap
  void stopSpeechBob();

  void onExpressionChange(const std::string& tag);  // applies tilt

  // External nudges:
  void onBump();    // IMU-driven "look around" (Phase 2 hook; no-op for now)

 private:
  bool     idleEnabled_ = true;
  bool     bobActive_   = false;
  float    bobAmp_      = 1.0f;
  uint32_t nextSaccadeMs_ = 0;
  uint32_t nextNodMs_     = 0;
  uint32_t lastBobMs_     = 0;
  int      bobPhase_      = 0;  // 0/1 alternating
};

extern MotionDirector motion;

}  // namespace stkchan
```

- [ ] **Step 2: Write `src/motion/MotionDirector.cpp`**

```cpp
#include "motion/MotionDirector.h"
#include "hal/Servos.h"
#include "face/ExpressionMap.h"

namespace stkchan {

MotionDirector motion;

void MotionDirector::begin() {
  nextSaccadeMs_ = millis() + random(4000, 8000);
  nextNodMs_     = millis() + random(12000, 20000);
}

void MotionDirector::pauseIdle()  { idleEnabled_ = false; }
void MotionDirector::resumeIdle() { idleEnabled_ = true; }

void MotionDirector::startSpeechBob(float amp) {
  bobActive_ = true;
  bobAmp_    = amp;
  lastBobMs_ = millis();
}
void MotionDirector::stopSpeechBob() {
  bobActive_ = false;
  servos.easePitchTo(0, 300);
}

void MotionDirector::onExpressionChange(const std::string& tag) {
  auto m = expressionFor(tag);
  servos.easeYawTo  (m.yawDeg,   500);
  servos.easePitchTo(m.pitchDeg, 500);
}

void MotionDirector::onBump() { /* TODO Phase 2: IMU-driven react */ }

void MotionDirector::tick(uint32_t nowMs) {
  servos.tick(nowMs);

  if (bobActive_) {
    if (nowMs - lastBobMs_ > 220) {
      int target = (bobPhase_ ? 0 : (int)(4 * bobAmp_));
      servos.easePitchTo(target, 200);
      bobPhase_ ^= 1;
      lastBobMs_ = nowMs;
    }
    return;  // don't interleave idle motion during speech
  }

  if (!idleEnabled_) return;

  if ((int32_t)(nowMs - nextSaccadeMs_) >= 0) {
    int yaw = (int)random(-8, 9);
    servos.easeYawTo(yaw, 400);
    nextSaccadeMs_ = nowMs + random(4000, 8000);
  }
  if ((int32_t)(nowMs - nextNodMs_) >= 0) {
    int pitch = (int)random(-3, 4);
    servos.easePitchTo(pitch, 600);
    nextNodMs_ = nowMs + random(12000, 20000);
  }
}

}  // namespace stkchan
```

- [ ] **Step 3: Smoke test on hardware**

```cpp
#include "motion/MotionDirector.h"
// in setup(): stkchan::servos.begin(); stkchan::motion.begin();
// in loop():  stkchan::motion.tick(millis());
```

Expected: idle saccades + nods over 30 s. No-op during press if you also call `pauseIdle()`/`resumeIdle()` around it.

- [ ] **Step 4: Commit**

```bash
git add src/motion/MotionDirector.*
git commit -m "feat(motion): idle wiggle + speech bob + expression tilt"
```

---

## Task 19: `Display` — thin status wrapper

**Files:**
- Create: `src/hal/Display.h`
- Create: `src/hal/Display.cpp`

**Goal:** Tiny helper to draw status overlays on top of the Avatar face — used by the FSM to surface ERROR strings while keeping Avatar in charge of the main canvas.

- [ ] **Step 1: Write `src/hal/Display.h`**

```cpp
#pragma once
#include <Arduino.h>

namespace stkchan {

class Display {
 public:
  void begin();
  void showStatusOverlay(const String& text, uint16_t fgColor);  // ephemeral
  void clearOverlay();
};

extern Display display;

}  // namespace stkchan
```

- [ ] **Step 2: Write `src/hal/Display.cpp`**

```cpp
#include "hal/Display.h"
#include <M5CoreS3.h>

namespace stkchan {

Display display;

void Display::begin() {
  // Avatar owns the main canvas; here we only need the overlay layer.
  M5.Display.setTextSize(2);
}

void Display::showStatusOverlay(const String& text, uint16_t fgColor) {
  int w = M5.Display.width();
  int h = M5.Display.height();
  M5.Display.fillRect(0, h - 28, w, 28, BLACK);
  M5.Display.setCursor(8, h - 24);
  M5.Display.setTextColor(fgColor, BLACK);
  M5.Display.print(text);
}

void Display::clearOverlay() {
  int w = M5.Display.width();
  int h = M5.Display.height();
  M5.Display.fillRect(0, h - 28, w, 28, BLACK);
}

}  // namespace stkchan
```

- [ ] **Step 3: Commit**

```bash
git add src/hal/Display.*
git commit -m "feat(display): bottom-strip status overlay over Avatar canvas"
```

---

## Task 20: State machine — `state_machine.{h,cpp}`

**Files:**
- Create: `src/state_machine.h`
- Create: `src/state_machine.cpp`

**Goal:** Spec §6. States: `IDLE / LISTENING / THINKING_STT / THINKING_CHAT / SPEAKING_TTS / SPEAKING / ERROR`. One transition per FSM tick. Callbacks set flags; transitions happen in `tickStateMachine()`.

- [ ] **Step 1: Write `src/state_machine.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace stkchan {

enum class State : uint8_t {
  IDLE,
  LISTENING,
  THINKING_STT,
  THINKING_CHAT,
  SPEAKING_TTS,
  SPEAKING,
  ERROR,
};

void initStateMachine();
void tickStateMachine(uint32_t nowMs);

// Called by main loop / hal callbacks to set flags.
void onPressDown();
void onPressUp();
void onAudioDone();   // AudioPlayer onPlayDone callback target

State currentState();

}  // namespace stkchan
```

- [ ] **Step 2: Write `src/state_machine.cpp`**

```cpp
#include "state_machine.h"
#include "config.h"
#include "hal/MicRecorder.h"
#include "hal/AudioPlayer.h"
#include "hal/Display.h"
#include "face/Face.h"
#include "motion/MotionDirector.h"
#include "face/ExpressionMap.h"
#include "persona/ResponseParser.h"
#include "persona/SystemPrompt.h"
#include "prompts/persona_examples.h"
#include "net/SttClient.h"
#include "net/ChatClient.h"
#include "net/TtsClient.h"
#include "net/ConnectivityTier.h"

namespace stkchan {

static State    g_state       = State::IDLE;
static bool     g_pressFlag   = false;
static bool     g_releaseFlag = false;
static bool     g_audioDoneFlag = false;
static uint32_t g_pressStartMs  = 0;
static String   g_transcript;
static String   g_replyRaw;
static ParsedReply g_parsed;

State currentState() { return g_state; }

void onPressDown()  { g_pressFlag = true; }
void onPressUp()    { g_releaseFlag = true; }
void onAudioDone()  { g_audioDoneFlag = true; }

static void enterError(const char* spoken, const char* exprTag) {
  Serial.printf("ERR state: %s\n", spoken);
  face.setExpression(exprTag);
  display.showStatusOverlay(spoken, 0xF800 /* red */);
  motion.pauseIdle();
  g_state = State::ERROR;
  // Speak the error; on done, go to IDLE.
  if (spoken && *spoken) {
    tts.synth(String(spoken), [](bool /*ok*/) { onAudioDone(); });
    g_state = State::SPEAKING;
  } else {
    g_audioDoneFlag = true;  // skip speech for empty errors
  }
}

void initStateMachine() {
  chat.setSystemPrompt(String(kDefaultPersona) + kPersonaExamples);
  audio.setOnPlayDone([](){ onAudioDone(); });
  face.setExpression("neutral");
  g_state = State::IDLE;
}

void tickStateMachine(uint32_t nowMs) {
  switch (g_state) {

    case State::IDLE: {
      motion.resumeIdle();
      if (g_pressFlag) {
        g_pressFlag = false;
        // Refuse if connectivity not OK.
        auto t = connectivity.current();
        if (t == Tier::NO_WIFI) { enterError(kErrNoWifi, "sleepy"); break; }
        if (t == Tier::LAN_NO_BACKEND) { enterError(kErrChatOffline, "doubt"); break; }
        // Start LISTENING.
        face.setExpression("neutral");
        display.showStatusOverlay("listening...", 0x07E0);
        motion.pauseIdle();
        if (!mic.start()) { enterError(kErrMicEmpty, "doubt"); break; }
        g_pressStartMs = nowMs;
        g_state = State::LISTENING;
      }
      break;
    }

    case State::LISTENING: {
      bool tooLong = (nowMs - g_pressStartMs) >= kMaxRecordMs;
      if (g_releaseFlag || tooLong) {
        g_releaseFlag = false;
        mic.stop();
        if (mic.wavSize() < 1024) { enterError(kErrMicEmpty, "doubt"); break; }
        face.setExpression("neutral");
        display.showStatusOverlay("thinking...", 0xFFE0);
        g_state = State::THINKING_STT;
      }
      break;
    }

    case State::THINKING_STT: {
      // Blocking HTTP call. Render was done above.
      if (!stt.transcribe(mic.wavData(), mic.wavSize(), g_transcript)) {
        enterError(kErrSttFailed, "sad");
        break;
      }
      if (g_transcript.isEmpty()) { enterError(kErrMicEmpty, "doubt"); break; }
      Serial.printf("USER: %s\n", g_transcript.c_str());
      g_state = State::THINKING_CHAT;
      break;
    }

    case State::THINKING_CHAT: {
      if (!chat.send(g_transcript, g_replyRaw)) {
        enterError(kErrChatFailed, "doubt");
        break;
      }
      Serial.printf("LLM RAW: %s\n", g_replyRaw.c_str());
      g_parsed = parseReply(std::string(g_replyRaw.c_str()));
      Serial.printf("PARSED: speech='%s' expr='%s'\n",
                    g_parsed.speech.c_str(), g_parsed.expr.c_str());
      // Set expression & motion now so they're up before TTS HTTP.
      face.setExpression(g_parsed.expr);
      motion.onExpressionChange(g_parsed.expr);
      motion.startSpeechBob(expressionFor(g_parsed.expr).bobAmp);
      g_state = State::SPEAKING_TTS;
      break;
    }

    case State::SPEAKING_TTS: {
      tts.synth(String(g_parsed.speech.c_str()), [](bool ok) {
        if (!ok) {
          // No spoken error (kErrTtsFailed is empty); just transition home.
          onAudioDone();
        }
      });
      // tts.synth dispatches AudioPlayer asynchronously; we wait in SPEAKING.
      g_state = State::SPEAKING;
      break;
    }

    case State::SPEAKING: {
      audio.tick();  // drives mouth open ratio internally if wired
      if (g_audioDoneFlag) {
        g_audioDoneFlag = false;
        motion.stopSpeechBob();
        face.setExpression("neutral");
        display.clearOverlay();
        g_state = State::IDLE;
      }
      break;
    }

    case State::ERROR: {
      // ERROR enters speaking via enterError(); audio-done routes via SPEAKING.
      // If we wound up here without speech (empty error), fall through:
      if (g_audioDoneFlag) {
        g_audioDoneFlag = false;
        display.clearOverlay();
        face.setExpression("neutral");
        g_state = State::IDLE;
      }
      break;
    }
  }
}

}  // namespace stkchan
```

- [ ] **Step 3: Note dependency:** `AudioPlayer` needs a `setOnPlayDone(std::function<void()>)` setter. If Jarvis's wrapper exposes it differently, adapt the FSM call here, not the AudioPlayer module.

- [ ] **Step 4: Note dependency:** `TtsClient::synth` signature must be `void synth(const String& text, std::function<void(bool ok)> onDone)`. Verify Task 9's adaptation matches.

- [ ] **Step 5: Build** (don't run yet — main.cpp not wired):

```bash
pio run
```

Expected: clean compile.

- [ ] **Step 6: Commit**

```bash
git add src/state_machine.*
git commit -m "feat(fsm): wire mic → stt → chat → parse → tts loop"
```

---

## Task 21: Final `main.cpp` wiring

**Files:**
- Modify: `src/main.cpp`

**Goal:** Boot everything in the right order, run `tickStateMachine()` once per `loop()`.

- [ ] **Step 1: Replace `src/main.cpp` with the integrated version**

```cpp
// Stack-chan v1 — entry point.
// See docs/superpowers/specs/2026-06-02-stackchan-design.md
#include <Arduino.h>
#include <M5CoreS3.h>

#include "config.h"
#include "services/NvsStore.h"
#include "services/OtaService.h"
#include "services/CaptivePortal.h"
#include "net/WifiManager.h"
#include "net/ConnectivityTier.h"
#include "net/SttClient.h"     // not strictly needed in main but pulls externs
#include "net/ChatClient.h"
#include "net/TtsClient.h"
#include "hal/AudioPlayer.h"
#include "hal/MicRecorder.h"
#include "hal/Servos.h"
#include "hal/Display.h"
#include "face/Face.h"
#include "motion/MotionDirector.h"
#include "state_machine.h"

using namespace stkchan;

static bool g_pressedLast = false;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Stack-chan v1 boot ===");

  if (!nvs.begin()) {
    Serial.println("WARN: NVS open failed");
  }

  display.begin();
  face.begin();

  if (!servos.begin()) {
    Serial.println("WARN: servo init failed");
  }
  motion.begin();

  if (!mic.begin()) {
    Serial.println("WARN: mic alloc failed");
  }

  // Provisioning gate: no SSID1 in NVS → captive portal forever, no FSM.
  bool needsProvisioning = nvs.getString(kNvsSsid1, "").isEmpty();
  if (needsProvisioning) {
    Serial.println("No WiFi creds — entering captive portal");
    portal.begin();
    while (true) { portal.tick(); delay(10); }
  }

  wifi.begin();
  connectivity.begin();
  initStateMachine();
}

void loop() {
  uint32_t now = millis();
  M5.update();
  wifi.tick();
  ota.tick();
  connectivity.tick(now);
  motion.tick(now);

  // Touch → FSM events
  bool pressed = (M5.Touch.getCount() > 0);
  if (pressed && !g_pressedLast) onPressDown();
  if (!pressed && g_pressedLast) onPressUp();
  g_pressedLast = pressed;

  tickStateMachine(now);
  delay(5);
}
```

- [ ] **Step 2: Build**

```bash
pio run
```

- [ ] **Step 3: Flash and upload filesystem**

```bash
pio run -t upload
pio run -t uploadfs
pio device monitor
```

- [ ] **Step 4: First-boot verification**

Expected serial:
```
=== Stack-chan v1 boot ===
NVS ...
```
If no WiFi creds, expect "No WiFi creds — entering captive portal" and the AP shows up on your phone.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(main): wire all subsystems + touch → FSM event bridge"
```

---

## Task 22: `provision-stackchan.py` — first-run CLI

**Files:**
- Create: `tools/provision-stackchan.py`
- Reference: `/home/jarod7736/workspace/Project-Jarvis/tools/provision-wifi.py`

**Goal:** Headless flow for users who'd rather not use the captive portal: open the AP, push a JSON blob of WiFi + keys, device persists to NVS and reboots.

- [ ] **Step 1: Copy Jarvis's tool**

```bash
cp /home/jarod7736/workspace/Project-Jarvis/tools/provision-wifi.py \
   tools/provision-stackchan.py
```

- [ ] **Step 2: Edit the JSON schema**

Open `tools/provision-stackchan.py`. Trim Jarvis-only keys (HA, MQTT, Anthropic, OpenClaw, LLM-module UART) from the prompt list. Keep:

- `ssid1`, `psk1`, `ssid2`, `psk2`, `ssid3`, `psk3`
- `chat_host`, `chat_model`
- `stt_url`, `stt_model`, `oai_key`
- `tts_provider`, `tts_voice`, `tts_model`, `el_key`
- `ota_pass`
- `persona` (optional)

Point the HTTP POST at the same endpoint the captive portal accepts (whatever `CaptivePortal.cpp` registers).

- [ ] **Step 3: Smoke test**

```bash
python3 tools/provision-stackchan.py --ap-ssid Stackchan-XXXX
```

Expected: prompts for fields, POSTs, prints "OK, rebooting".

- [ ] **Step 4: Commit**

```bash
git add tools/provision-stackchan.py
git commit -m "feat(tools): provisioning CLI for Stack-chan NVS keys"
```

---

## Task 23: End-to-end smoke test (manual)

**Files:** none — runtime verification only.

**Goal:** Verify the spec's happy path on real hardware:
**`IDLE → press → LISTENING → release → THINKING_STT → THINKING_CHAT → SPEAKING_TTS → SPEAKING → IDLE`** with face + servo motion matching the spec.

- [ ] **Step 1: Pre-flight**

- NVS provisioned via captive portal (Task 6) or provisioning CLI (Task 22): WiFi creds, `chat_host`, `oai_key`, `tts_provider=openai`, `tts_voice=nova`, `stt_url=https://api.openai.com/v1/audio/transcriptions`.
- Ollama on `chat_host` responds: `curl $chat_host/api/tags` returns 200.
- OpenAI key valid: `curl -H "Authorization: Bearer $key" https://api.openai.com/v1/models | head` works.

- [ ] **Step 2: Power up, watch serial**

Expected boot lines: NVS open, WiFi join, OTA online, `tier -> 0` (LAN_OK), Avatar visible on LCD, idle wiggle starts within 2 s.

- [ ] **Step 3: Press-and-hold the screen, say "hello, who are you?", release**

Watch the serial for the full sequence:
- `USER: hello, who are you?`
- `LLM RAW: <speech>...</speech><expr>...</expr>`
- `PARSED: speech='...' expr='...'`
- Audio plays through the speaker.
- Face transitions through the parsed expression and back to neutral after audio completes.
- Head bobs while speaking, then returns to centered.

- [ ] **Step 4: Provoke an error**

- Disable WiFi on the AP → wait 30 s for tier to flip → press → expect `kErrNoWifi` spoken, then return to IDLE.
- Disable Ollama on lobsterboy (`systemctl stop ollama`) → wait 30 s → press → expect `kErrChatOffline` spoken.

- [ ] **Step 5: Commit the test log as a note (optional)**

```bash
mkdir -p docs/superpowers/verification
# Paste serial-log snippets demonstrating the happy path + each kErr*.
$EDITOR docs/superpowers/verification/2026-06-02-v1-smoke.md
git add docs/superpowers/verification/2026-06-02-v1-smoke.md
git commit -m "docs(verify): v1 end-to-end smoke test log"
```

- [ ] **Step 6: Tag a release**

```bash
git tag -a v0.1.0 -m "Stack-chan v1 — talking robot, no vision"
```

---

## Self-review

| Spec section | Covered by |
|---|---|
| §1 Summary | Plan goal + architecture statement |
| §2 Goals & non-goals | Task 21 main wiring, captive portal Task 6 strips non-goal fields |
| §3 Decisions D1–D12 | D1 repo bootstrap T1; D2 device-orch FSM T20; D3 touch-to-talk T21; D4 chat_model T2/T14; D5 cloud-first STT T11 & TTS T9; D6 Avatar T16; D7 PCA9685 T17; D8 persona T14; D9 parser T12; D10 6-turn ring T13; D11 Tailscale chat_host T2/T13; D12 face-rec deferred — explicit no-task |
| §4 Hardware | T1 platformio.ini PSRAM; T17 PCA9685 wiring; T10 mic; T8 audio |
| §5 Firmware layout | File map in this plan exactly matches |
| §6 Conversation flow + errors | T20 state_machine.cpp; T2 kErr* taxonomy |
| §7 Persona + expression mapping | T14 prompt + examples; T15 ExpressionMap; T12 ResponseParser |
| §8 Endpoints | T11 STT, T13 Chat, T9 TTS, T7 OTA |
| §9 NVS schema | T2 config.h key constants; T6 captive portal trims to these |
| §10 Provisioning | T6 captive portal; T22 CLI |
| §11 Phasing Phase 1 | All 23 tasks |
| §12 Risks | R3 expression vocab fixed at 6 — enforced in T15; R4 tier short-circuit — T20; R5 no TTS fallback — accepted in T9; R6 servo-base procurement — out of plan scope |

**Placeholder scan:** all "TODO Phase 2" markers in code (e.g. `MotionDirector::onBump()`) are intentional — they're the seams the spec calls out for future work.

**Type consistency:** verified — `ParsedReply` (T12) ↔ `parseReply` callers (T20); `ExprMapping`/`expressionFor` (T15) ↔ both `Face::setExpression` (T16) and `MotionDirector::onExpressionChange`/`startSpeechBob` (T18, T20); `audio.setOnPlayDone(std::function<void()>)` signature consumed in T20 must be matched in T8's `AudioPlayer` adaptation.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-02-stackchan-v1.md`. Two execution options:

**1. Subagent-Driven (recommended)** — Dispatch a fresh subagent per task, review between tasks, fast iteration. Good fit for this plan because most tasks are independent file additions and several (T12, T15) are pure-logic TDD that benefits from clean context.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints. Best if you want to watch each task land in real time.

Which approach?
