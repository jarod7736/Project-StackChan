#pragma once
// Stack-chan v1 — global constants.
// See spec section 6 (errors) and section 10 (NVS schema).

#include <stddef.h>
#include <stdint.h>

// ── Feature: on-device presence awareness ──────────────────────────────────
// 1 = enable camera face-DETECTION presence behaviors (perk up + greet on
// arrival, servo look-toward-you tracking, sleepy when the desk is empty).
// Default 0: the feature ships dark until the camera/I2C spike clears.
// See vision/PresenceSensor + vision/PresenceLogic.
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

// ── Wake-word (VAD-then-confirm) ──────────────────────────────────────────
// Spec: docs/superpowers/specs/2026-06-11-wake-word-design.md
constexpr const char* kWakeAck         = "Yes?";  // offline ack clip (stock-clips pipeline)
constexpr const char* kNvsWakeEn       = "wake_en";      // bool, default on
constexpr const char* kNvsWakeWord     = "wake_word";    // default below
constexpr const char* kDefaultWakeWord = "hey stack chan";
constexpr uint32_t kWakeBufSeconds     = 4;      // continuous capture buffer
constexpr uint32_t kWakeFrameSamples   = 480;    // 30 ms @ 16 kHz
constexpr uint32_t kWakeLeadInFrames   = 10;     // 300 ms kept before the trip
constexpr uint32_t kWakeCooldownMs     = 2000;   // after a non-match
constexpr uint32_t kWakeResumeGuardMs  = 700;    // quiet time before re-arming
constexpr uint32_t kWakeOpenSilenceMs  = 800;    // wake-opened LISTENING end-of-speech
constexpr uint32_t kWakeOpenMaxMs      = 6000;   // wake-opened LISTENING hard cap

}  // namespace stkchan
