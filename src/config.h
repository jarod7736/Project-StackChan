#pragma once
// Stack-chan v1 — global constants.
// See spec section 6 (errors) and section 10 (NVS schema).

#include <stddef.h>
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

// === NTP / Time ===
// POSIX TZ string (US Central with DST). Override at provisioning time if needed.
constexpr const char* kNtpServer        = "pool.ntp.org";
constexpr const char* kTimezoneDefault  = "CST6CDT,M3.2.0,M11.1.0";

// === WiFi ===
// Per-slot connect budget inside the slot-priority loop (Jarvis PR #20).
constexpr uint32_t kPerSlotTimeoutMs    = 8000;

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
