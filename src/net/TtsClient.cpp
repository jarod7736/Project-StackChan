#include "TtsClient.h"

#include <algorithm>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "../config.h"
#include "../hal/AudioPlayer.h"
#include "../services/NvsStore.h"

#include <LittleFS.h>

#include "../services/ClipId.h"
#include "../services/OtaService.h"

namespace stkchan {

namespace {

// Provider endpoint constants.
constexpr const char* kOpenAiHost     = "api.openai.com";
constexpr const char* kOpenAiPath     = "/v1/audio/speech";
constexpr const char* kElevenHost     = "api.elevenlabs.io";
constexpr const char* kElevenPathBase = "/v1/text-to-speech/";

// Build the OpenAI /v1/audio/speech request body.
String buildOpenAiBody(const String& text, const String& voice,
                       const String& model) {
    JsonDocument req;
    req["model"]           = model;
    req["voice"]           = voice;
    req["input"]           = text;
    req["response_format"] = "mp3";
    String body;
    serializeJson(req, body);
    return body;
}

// Build the ElevenLabs /v1/text-to-speech/<voice_id> request body.
String buildElevenBody(const String& text, const String& model) {
    JsonDocument req;
    req["text"]     = text;
    req["model_id"] = model;
    auto vs = req["voice_settings"].to<JsonObject>();
    vs["stability"]        = 0.5;
    vs["similarity_boost"] = 0.75;
    String body;
    serializeJson(req, body);
    return body;
}

// ── Buffered MP3 fetch ─────────────────────────────────────────────────────
// Download the ENTIRE MP3 into a PSRAM buffer, then close the connection
// BEFORE playback. This is deliberately buffered, not streamed:
//
//   The streamed path held the HTTPS/TLS connection open and decoded MP3 off
//   the wire during playback — so WiFi-RX + TLS-decrypt ran concurrently with
//   the audio amp + decoder for the whole track. That concurrent current peak
//   tripped the AXP2101 battery-path over-current protection: the device
//   powered off right at "track done" (recoverable only by disconnecting the
//   DinBase battery). Heavy WiFi *without* audio (STT/chat) never crashed it;
//   only WiFi *during* audio did. Downloading first, then playing, separates
//   the two current peaks so neither window hits the trip threshold. It also
//   eliminates on-the-wire decode starvation (the choppy-audio symptom).
//
// Returns a ps_malloc'd buffer (caller frees) + length in *outLen, or nullptr
// on any failure. Honours the http.end()-on-every-exit-path invariant.
uint8_t* fetchMp3ToPsram(const String& provider, const String& text,
                         const String& voice, const String& model,
                         const String& apiKey, size_t* outLen) {
    *outLen = 0;
    auto* tls = new WiFiClientSecure();
    tls->setInsecure();  // cert pinning is Phase 2 (spec risk R1)
    auto* http = new HTTPClient();
    http->setTimeout(kTtsTimeoutMs);
    http->useHTTP10(true);  // HTTP/1.0 → connection-close gives a clean EOF

    const bool eleven = provider.equalsIgnoreCase("eleven") ||
                        provider.equalsIgnoreCase("elevenlabs");
    String url = eleven
        ? String("https://") + kElevenHost + kElevenPathBase + voice
        : String("https://") + kOpenAiHost + kOpenAiPath;

    if (!http->begin(*tls, url)) {
        Serial.println("[TtsClient] http.begin failed");
        delete http; delete tls;
        return nullptr;
    }
    String body;
    if (eleven) {
        http->addHeader("xi-api-key", apiKey);
        body = buildElevenBody(text, model);
    } else {
        http->addHeader("Authorization", String("Bearer ") + apiKey);
        body = buildOpenAiBody(text, voice, model);
    }
    http->addHeader("Content-Type", "application/json");
    http->addHeader("Accept",       "audio/mpeg");

    Serial.printf("[TtsClient] POST %s body=%u chars (buffered)\n",
                  url.c_str(), (unsigned)body.length());
    int code = http->POST(body);
    if (code != 200) {
        Serial.printf("[TtsClient] HTTP %d\n", code);
        http->end(); delete http; delete tls;
        return nullptr;
    }

    // Pull the whole response into PSRAM (cap at kMp3MaxBytes), then close.
    uint8_t* buf = static_cast<uint8_t*>(ps_malloc(stkchan::kMp3MaxBytes));
    if (!buf) {
        Serial.println("[TtsClient] ps_malloc failed");
        http->end(); delete http; delete tls;
        return nullptr;
    }
    Stream*  stream   = http->getStreamPtr();
    size_t   total    = 0;
    uint32_t lastByte = millis();
    for (;;) {
        int avail = stream ? stream->available() : 0;
        if (avail > 0) {
            size_t room = stkchan::kMp3MaxBytes - total;
            if (room == 0) {
                Serial.printf("[TtsClient] response exceeds %u B cap — truncated\n",
                              (unsigned)stkchan::kMp3MaxBytes);
                break;
            }
            int n = stream->readBytes(buf + total,
                                      std::min((size_t)avail, room));
            if (n > 0) { total += n; lastByte = millis(); }
        } else {
            // Clean EOF: HTTP/1.0 server closed and the buffer is drained.
            if (!http->connected() && (!stream || stream->available() == 0)) break;
            if (millis() - lastByte > 3000) {            // stall guard
                Serial.println("[TtsClient] download stalled");
                break;
            }
            delay(2);
        }
    }
    http->end(); delete http; delete tls;

    if (total == 0) {
        Serial.println("[TtsClient] empty MP3 response");
        free(buf);
        return nullptr;
    }
    Serial.printf("[TtsClient] downloaded %u B MP3, connection closed\n",
                  (unsigned)total);
    *outLen = total;
    return buf;
}

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
    if (len == 0 || len > stkchan::kMp3MaxBytes) { f.close(); return false; }
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

}  // namespace

// --------------------------------------------------------------------------

void TtsClient::synth(const String& text, std::function<void(bool)> onDone) {
    if (text.length() == 0) {
        Serial.println("[TtsClient] synth: empty text");
        onDone(false);
        return;
    }

    // Pre-rendered clip? Instant, free, offline-safe. Falls through to cloud
    // on any miss. Spec: docs/superpowers/specs/2026-06-11-stock-clips-design.md
    if (tryPlayClip(text)) {
        onDone(true);  // playback started — same contract as the cloud path
        return;
    }

    // Read config from NVS at call time so portal changes apply immediately.
    String provider = nvs.getString(kNvsTtsProv, kDefaultTtsProv);
    String voice    = nvs.getString(kNvsTtsVoice, kDefaultTtsVoice);
    String model    = nvs.getString(kNvsTtsModel, kDefaultTtsModel);

    Serial.printf("[TtsClient] synth provider=%s voice=%s model=%s len=%u\n",
                  provider.c_str(), voice.c_str(), model.c_str(),
                  (unsigned)text.length());

    // Resolve the API key per provider.
    String apiKey;
    if (provider.equalsIgnoreCase("openai")) {
        apiKey = nvs.getString(kNvsOaiKey, "");
        if (apiKey.length() == 0) {
            Serial.println("[TtsClient] no oai_key in NVS");
            onDone(false);
            return;
        }
    } else if (provider.equalsIgnoreCase("eleven") ||
               provider.equalsIgnoreCase("elevenlabs")) {
        apiKey = nvs.getString(kNvsElKey, "");
        if (apiKey.length() == 0) {
            Serial.println("[TtsClient] no el_key in NVS");
            onDone(false);
            return;
        }
    } else {
        // VOICEVOX and any unknown provider: Phase 2. Log + fail.
        Serial.printf("[TtsClient] unsupported provider \"%s\" (Phase 2)\n",
                      provider.c_str());
        onDone(false);
        return;
    }

    // Download the full MP3 to PSRAM and CLOSE the connection before playing,
    // so WiFi-RX and the audio amp never peak at the same time (see
    // fetchMp3ToPsram). This is the fix for the "powers off at track done"
    // AXP over-current latch — and for the choppy audio.
    size_t   mp3Len = 0;
    uint8_t* mp3    = fetchMp3ToPsram(provider, text, voice, model, apiKey, &mp3Len);
    if (!mp3) {
        Serial.println("[TtsClient] synth failed (no audio)");
        onDone(false);
        return;
    }

    // Connection is now closed. Play from PSRAM — no network during audio.
    // AudioPlayer::play() copies the buffer into its own PSRAM, so we free
    // our download buffer immediately after.
    bool ok = audio.play(mp3, mp3Len);
    free(mp3);
    if (!ok) {
        Serial.println("[TtsClient] AudioPlayer::play rejected");
        onDone(false);
        return;
    }

    // Playback started — fire success.
    onDone(true);
}

// Singleton definition.
TtsClient tts;

}  // namespace stkchan
