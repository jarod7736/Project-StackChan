#pragma once
// Stack-chan v1 — global constants.
// See spec section 6 (errors) and section 10 (NVS schema).

#include <stddef.h>
#include <stdint.h>

// ── Diagnostic soak toggle ─────────────────────────────────────────────────
// 1 = skip the always-on network services (AsyncTCP web server, mDNS, OTA
// listener, the 30 s connectivity probe) to isolate the AXP2101 power-off.
// WiFi STA still connects (same as the OEM/other stable firmwares) and the
// face + [PWR]/[AXP] diagnostics still run — so this is a clean "WiFi on but
// no extra network stack" idle-soak. Set back to 0 to restore full function.
#define STKCHAN_SOAK_MINIMAL 0

// 1 = bare WiFi isolation test: M5.begin (factory-default power) + WiFi STA + a
// minimal on-screen uptime loop. NO app at all — no audio, LVGL face, servos,
// motion, FSM, or diagnostics. Mirrors the proven-good OpenClaw continuous-WiFi
// load. Trips here -> the unit's WiFi power path; runs indefinitely -> the
// trigger is in our app code (add it back one piece at a time, battery on).
#define STKCHAN_BARE_WIFI 0

// Bisection on top of the bare-WiFi baseline (which ran all night clean). Add ONE
// subsystem at a time; whichever makes it trip is the culprit. Audio first — the
// fastest deaths were during voice, and audio.begin() turns on the AW88298 amp
// rail (ALDO3) and leaves it on. 1 = also init audio (amp rail on, idle, silent).
#define STKCHAN_BARE_AUDIO 0

// Sub-step 2b: only meaningful with STKCHAN_BARE_AUDIO. 0 = amp rail held on but
// silent (tests rail-on-idle). 1 = also play a synthetic "TTS phrase" (a run of
// short speech-band tones with gaps) every 3 min — a faithful proxy for the
// bursty current envelope of real TTS playback, where the fastest deaths
// happened (amp sourcing current + onset transients, no network/decode/LVGL).
// Full volume on purpose — current draw scales with volume, so a quiet tone
// would be a weaker, less faithful test. Flip to 1 only after 2a is cleared.
#define STKCHAN_AUDIO_PLAYBACK 0

// Sub-step 2c: only meaningful with STKCHAN_BARE_AUDIO. Adds the real MP3 DECODE
// path on top of amp-drive (2b). Every 3 min, STREAM the embedded broadband clip
// (src/diag/diag_clip_mp3.h) straight from flash via AudioFileSourcePROGMEM →
// AudioPlayer::playStream() — decoded incrementally, NO full-buffer PSRAM copy.
// Deliberately the STREAMING path (not the buffered play() that ecf953b added to
// de-overlap current peaks): isolates streaming-decode CPU + amp-drive current,
// still free of network/STT/LVGL/FSM. Runs at audible 230/255 (200 was sub-
// audible), so it's also the first FULL-current amp test. Supersedes 2b when 1.
#define STKCHAN_AUDIO_DECODE 0

// Rung 2d (network): only meaningful with STKCHAN_BARE_AUDIO. Supersedes 2c.
// Stream the clip LIVE over plain HTTP from a LAN host and decode off the wire
// DURING playback, so WiFi-RX + decode + amp current OVERLAP — the exact
// concurrency ecf953b's buffer-then-play fix removed. Production tripped the
// AXP2101 *battery-path* OCP this way but over HTTPS/TLS; plain HTTP strips TLS,
// so this isolates whether streaming-overlap ALONE trips it: trips -> overlap is
// enough; clears -> TLS is the needed ingredient (next rung = HTTPS). The
// DinBase battery must be connected — the OCP that fires is on the battery path.
#define STKCHAN_AUDIO_HTTP 0
#define STKCHAN_AUDIO_HTTP_URL "http://192.168.1.178:8088/clip.mp3"

// Rung 2e (TLS): only meaningful with STKCHAN_BARE_AUDIO. Supersedes 2d. Same
// live-stream overlap as 2d but over HTTPS — adds the TLS handshake + per-record
// decrypt current (WiFiClientSecure) on top of WiFi-RX + decode + amp. This is
// the LAST differentiator from the production failing path (which streamed TTS
// over HTTPS/TLS and tripped the AXP2101 battery-path OCP). Uses
// src/diag/DiagTlsStreamSource (mirrors the TtsStreamSource ecf953b removed):
// trips -> TLS streaming-overlap is the culprit (validates buffer-then-play);
// clears 30 min -> trigger is elsewhere in the real path (POST/STT/larger MP3).
#define STKCHAN_AUDIO_HTTPS 0
#define STKCHAN_AUDIO_HTTPS_URL "https://192.168.1.178:8443/clip.mp3"

// Power-path telemetry for the bare soak. 1 = log [PWR] (vbat/vbus/die-temp/
// charging/pct) + [AXP] latched fault regs every tick, and reset-reason + AXP
// fault dump at boot. Reuses the forensics from a900ad7/312dbd0/e2bbaef/583b5d9.
// Needed to INTERPRET a "clear": every rung survived, but with no power readings
// we can't tell if a playback peak neared the battery-discharge OCP — or whether
// the battery is even discharging on USB (getBatteryCurrent is hardwired 0 on
// CoreS3, so we infer from vbat-dip / isCharging / die-temp / latched AXP IRQs).
#define STKCHAN_PWR_TELEMETRY 0

// Max-load mode: drive the heaviest achievable CoreS3-side load to test whether
// ANY load can push past the VBUS input-current limit and force the battery into
// the discharge path (servos don't count — they're on an external supply). 1 =
// back-to-back playback (no 3-min gap) + max display brightness, stacked on the
// active audio rung. Watch [PWR] vbat: a dip off its floating ~4.15V = battery
// now sourcing (OCP reachable); pinned = load still under VBUS limit.
#define STKCHAN_MAXLOAD 0
#define STKCHAN_PLAY_GAP_MS (STKCHAN_MAXLOAD ? 800u : 180000u)

// ── Feature: on-device presence awareness ──────────────────────────────────
// 1 = enable camera face-DETECTION presence behaviors (perk up + greet on
// arrival, servo look-toward-you tracking, sleepy when the desk is empty).
// Default 0: the feature ships dark until the camera/I2C spike clears and the
// AXP brownout work above lands. See vision/PresenceSensor + vision/PresenceLogic.
// #ifndef-guarded so a build flag (-DSTKCHAN_PRESENCE=1) can force it on.
#ifndef STKCHAN_PRESENCE
#define STKCHAN_PRESENCE 0
#endif

namespace stkchan {

// === NVS ===
constexpr const char* kNvsNamespace = "stkchan";  // keys must be <= 15 chars

constexpr const char* kNvsSsid1     = "ssid1";
constexpr const char* kNvsPsk1      = "psk1";
constexpr const char* kNvsSsid2     = "ssid2";
constexpr const char* kNvsPsk2      = "psk2";
constexpr const char* kNvsSsid3     = "ssid3";
constexpr const char* kNvsPsk3      = "psk3";
constexpr const char* kNvsChatHost  = "chat_host";   // casual LLM (direct Ollama)
constexpr const char* kNvsChatModel = "chat_model";
constexpr const char* kNvsBrainHost = "brain_host";  // oc-personal agent runner
constexpr const char* kNvsBrainKey  = "brain_key";   // bearer for the agent (optional)
constexpr const char* kNvsBrainKw   = "brain_kw";    // optional CSV stem override
constexpr const char* kNvsSttUrl    = "stt_url";
constexpr const char* kNvsSttModel  = "stt_model";
constexpr const char* kNvsOaiKey    = "oai_key";
constexpr const char* kNvsTtsProv   = "tts_provider";
constexpr const char* kNvsTtsVoice  = "tts_voice";
constexpr const char* kNvsTtsModel  = "tts_model";
constexpr const char* kNvsElKey     = "el_key";
constexpr const char* kNvsOtaPass   = "ota_pass";
constexpr const char* kNvsPersona   = "persona";
constexpr const char* kNvsSpkVolume = "spk_vol";  // 0-255 M5.Speaker.setVolume
constexpr const char* kNvsPresEnable = "pres_en";   // "1"/"0" presence on/off (NVS override)
constexpr const char* kNvsGreetMin   = "greet_min"; // spoken-greeting cooldown, minutes
constexpr const char* kNvsPresGain    = "pres_gain"; // tracking gain x100 (e.g. "50" = 0.50)

// === Defaults ===
constexpr const char* kDefaultChatModel = "gemma3n:e4b";
constexpr const char* kDefaultSttModel  = "whisper-1";
constexpr const char* kDefaultTtsProv   = "openai";
constexpr const char* kDefaultTtsVoice  = "nova";
constexpr const char* kDefaultTtsModel  = "tts-1";
constexpr int         kDefaultSpkVolume = 230;   // 0-255; 200 was inaudible

// === Battery ===
// The CoreS3 AXP2101 fuel-gauge percentage (getBatteryLevel) is unreliable —
// it can read low even on a full pack. We gate the low-battery cue on battery
// VOLTAGE (a direct ADC read) instead, and derive the displayed % from it.
constexpr int         kLowBattMv      = 3500;  // warn at/below this battery voltage (mV)
constexpr int         kLowBattClearMv = 3700;  // re-arm the cue once recovered above this
constexpr int         kVbusPresentMv  = 4000;  // VBUS above this => on USB/external power
constexpr const char* kLowBattMsg     = "My battery is low. Please plug me in.";

// Rough LiPo voltage(mV) -> percent for a discreet indicator. -1 if unknown.
inline int batteryPctFromMv(int mv) {
    if (mv <= 0)    return -1;
    if (mv >= 4200) return 100;
    if (mv <= 3300) return 0;
    return (int)((long)(mv - 3300) * 100 / (4200 - 3300));
}

// === Brain agent (oc-personal) ===
// The oc-personal runner is an OpenAI-compatible endpoint; model "oc-personal"
// runs the Claude + multi-MCP agent (2ndBrain / Gmail / Calendar).
constexpr const char* kDefaultBrainHost = "http://192.168.1.178:8080";
constexpr const char* kOcPersonalModel  = "oc-personal";
// Utterances containing any of these stems (case-insensitive substring) route
// to the agent instead of the local casual model. NVS "brain_kw" (CSV) overrides.
constexpr const char* kBrainStems[] = {
    "2nd brain", "second brain", "calendar", "schedule", "agenda",
    "my email", "inbox", "my notes",
};
constexpr size_t kBrainStemCount = sizeof(kBrainStems) / sizeof(kBrainStems[0]);

// === NTP / Time ===
// POSIX TZ string (US Central with DST). Override at provisioning time if needed.
constexpr const char* kNtpServer        = "pool.ntp.org";
constexpr const char* kTimezoneDefault  = "CST6CDT,M3.2.0,M11.1.0";

// === WiFi ===
// Per-slot connect budget inside the slot-priority loop (Jarvis PR #20).
constexpr uint32_t kPerSlotTimeoutMs    = 8000;

// === Timeouts (ms) ===
constexpr uint32_t kSttTimeoutMs        = 8000;
constexpr uint32_t kChatTimeoutMs       = 60000;  // cold gemma3n load can be ~20-40s
constexpr uint32_t kTtsTimeoutMs        = 8000;
constexpr uint32_t kTierProbeIntervalMs = 30000;
constexpr uint32_t kMaxRecordMs         = 6000;

// === Audio ===
constexpr uint32_t kRecordSampleRate = 16000;
constexpr size_t   kRecordMaxBytes   = 192 * 1024;  // 6 s @ 16 kHz mono 16-bit
constexpr size_t   kMp3MaxBytes      = 256 * 1024;  // PSRAM cap (PR #55 in Jarvis)

// === Presence (on-device face detection) ===
// Defaults feed vision/PresenceLogic params; NVS keys above can override at runtime (T7).
constexpr uint32_t kPresIdleScanMs  = 1200;    // capture cadence with no face (power save)
constexpr uint32_t kPresTrackScanMs = 250;     // capture cadence while tracking (~4 Hz pursuit)
constexpr int      kPresArriveHits  = 3;       // consecutive detections -> "present"
constexpr uint32_t kPresAbsentMs    = 30000;   // sustained no-detection -> "absent"
constexpr uint32_t kGreetCooldownMs = 600000;  // 10 min between spoken greetings
constexpr float    kTrackDeadband   = 0.12f;   // normalized; below this, hold still
constexpr float    kTrackYawGain    = 0.5f;    // fraction of full-scale per unit error
constexpr float    kTrackPitchGain  = 0.5f;
constexpr int      kTrackYawSlew    = 8;        // max deg yaw change per update
constexpr int      kTrackPitchSlew  = 5;        // max deg pitch change per update

// === Conversation ===
constexpr size_t kHistoryTurns = 6;
constexpr int    kChatMaxTokens = 512;  // cap casual replies (agent is capped server-side)

// === User-facing error strings ===
// Routed via TtsClient → AudioPlayer; never returned silently.
constexpr const char* kErrNoWifi      = "I can't connect to anything.";
constexpr const char* kErrChatOffline = "My brain's not on the network.";
constexpr const char* kErrMicEmpty    = "Hm, didn't catch that.";
constexpr const char* kErrSttFailed   = "My ears aren't working.";
constexpr const char* kErrChatFailed  = "Brain's stuck, try again.";
constexpr const char* kErrTtsFailed   = "";  // display-only, no speech

}  // namespace stkchan
