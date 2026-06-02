#include "TtsClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "../config.h"
#include "../hal/AudioPlayer.h"
#include "../services/NvsStore.h"

namespace stkchan {

namespace {

// Provider endpoint constants.
constexpr const char* kOpenAiHost     = "api.openai.com";
constexpr const char* kOpenAiPath     = "/v1/audio/speech";
constexpr const char* kElevenHost     = "api.elevenlabs.io";
constexpr const char* kElevenPathBase = "/v1/text-to-speech/";

// Allocate `n` bytes in PSRAM if available, else regular heap. Any buffer
// >= a couple KB belongs in PSRAM to keep internal SRAM free for stack/DMA.
uint8_t* psramAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (p) return reinterpret_cast<uint8_t*>(p);
    // Fallback: better to allocate from internal heap and squeeze than to
    // silently return nothing.
    return reinterpret_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_8BIT));
}

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

// Download the HTTP response body into a freshly-allocated PSRAM buffer,
// capped at kMp3MaxBytes. Returns {buf, len} or {nullptr, 0} on failure.
// Caller is responsible for calling http.end() — this function does NOT.
struct RawMp3 {
    uint8_t* buf = nullptr;
    size_t   len = 0;
    bool empty() const { return len == 0 || buf == nullptr; }
};

RawMp3 downloadBody(HTTPClient& http, int code) {
    RawMp3 out;
    if (code < 200 || code >= 300) {
        Serial.printf("[TtsClient] HTTP %d\n", code);
        return out;
    }

    int contentLen = http.getSize();  // -1 if chunked / unknown
    size_t cap = kMp3MaxBytes;
    if (contentLen > 0) {
        if ((size_t)contentLen > cap) {
            Serial.printf("[TtsClient] response too large: %d > %u cap\n",
                          contentLen, (unsigned)cap);
            return out;
        }
        cap = (size_t)contentLen;
    }

    uint8_t* buf = psramAlloc(cap);
    if (!buf) {
        Serial.printf("[TtsClient] PSRAM alloc failed (%u bytes)\n",
                      (unsigned)cap);
        Serial.printf("[TtsClient]   free_psram=%u largest_psram_block=%u\n",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        Serial.printf("[TtsClient]   free_heap=%u largest_heap_block=%u\n",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return out;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t got = 0;
    uint32_t lastByte = millis();
    while (got < cap) {
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes(buf + got,
                                      std::min((size_t)avail, cap - got));
            if (n > 0) {
                got += n;
                lastByte = millis();
            }
        } else {
            // No bytes ready: check for clean EOF.
            if (!http.connected() && stream->available() == 0) break;
            if (millis() - lastByte > 3000) {
                Serial.println("[TtsClient] body read stall (>3s)");
                break;
            }
            delay(5);
        }
    }

    if (got == 0) {
        Serial.println("[TtsClient] empty body");
        free(buf);
        return out;
    }
    Serial.printf("[TtsClient] downloaded %u bytes\n", (unsigned)got);
    out.buf = buf;
    out.len = got;
    return out;
}

// Synthesize via OpenAI /v1/audio/speech. Returns RawMp3 (caller frees buf).
RawMp3 synthOpenAi(const String& text, const String& voice,
                   const String& model, const String& apiKey) {
    WiFiClientSecure secure;
    secure.setInsecure();  // cert pinning is Phase 2 (spec risk R1)
    HTTPClient http;
    http.setTimeout(kTtsTimeoutMs);
    http.useHTTP10(true);

    String url = String("https://") + kOpenAiHost + kOpenAiPath;
    if (!http.begin(secure, url)) {
        Serial.println("[TtsClient] http.begin failed (openai)");
        return RawMp3{};
    }
    http.addHeader("Authorization", String("Bearer ") + apiKey);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Accept",        "audio/mpeg");

    String body = buildOpenAiBody(text, voice, model);
    Serial.printf("[TtsClient] POST %s body=%u chars\n", url.c_str(),
                  (unsigned)body.length());
    int code = http.POST(body);
    RawMp3 out = downloadBody(http, code);
    http.end();   // every exit path closes the client
    return out;
}

// Synthesize via ElevenLabs /v1/text-to-speech/<voice_id>.
RawMp3 synthEleven(const String& text, const String& voice,
                   const String& model, const String& apiKey) {
    WiFiClientSecure secure;
    secure.setInsecure();
    HTTPClient http;
    http.setTimeout(kTtsTimeoutMs);
    http.useHTTP10(true);

    String url = String("https://") + kElevenHost + kElevenPathBase + voice;
    if (!http.begin(secure, url)) {
        Serial.println("[TtsClient] http.begin failed (elevenlabs)");
        return RawMp3{};
    }
    http.addHeader("xi-api-key",   apiKey);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept",       "audio/mpeg");

    String body = buildElevenBody(text, model);
    Serial.printf("[TtsClient] POST %s body=%u chars\n", url.c_str(),
                  (unsigned)body.length());
    int code = http.POST(body);
    RawMp3 out = downloadBody(http, code);
    http.end();   // every exit path closes the client
    return out;
}

}  // namespace

// --------------------------------------------------------------------------

void TtsClient::synth(const String& text, std::function<void(bool)> onDone) {
    if (text.length() == 0) {
        Serial.println("[TtsClient] synth: empty text");
        onDone(false);
        return;
    }

    // Read config from NVS at call time so portal changes apply immediately.
    String provider = nvs.getString(kNvsTtsProv, kDefaultTtsProv);
    String voice    = nvs.getString(kNvsTtsVoice, kDefaultTtsVoice);
    String model    = nvs.getString(kNvsTtsModel, kDefaultTtsModel);

    Serial.printf("[TtsClient] synth provider=%s voice=%s model=%s len=%u\n",
                  provider.c_str(), voice.c_str(), model.c_str(),
                  (unsigned)text.length());

    RawMp3 mp3;

    if (provider.equalsIgnoreCase("openai")) {
        String apiKey = nvs.getString(kNvsOaiKey, "");
        if (apiKey.length() == 0) {
            Serial.println("[TtsClient] no oai_key in NVS");
            onDone(false);
            return;
        }
        mp3 = synthOpenAi(text, voice, model, apiKey);

    } else if (provider.equalsIgnoreCase("eleven") ||
               provider.equalsIgnoreCase("elevenlabs")) {
        String apiKey = nvs.getString(kNvsElKey, "");
        if (apiKey.length() == 0) {
            Serial.println("[TtsClient] no el_key in NVS");
            onDone(false);
            return;
        }
        mp3 = synthEleven(text, voice, model, apiKey);

    } else {
        // VOICEVOX and any unknown provider: Phase 2. Log + fail.
        Serial.printf("[TtsClient] unsupported provider \"%s\" (Phase 2)\n",
                      provider.c_str());
        onDone(false);
        return;
    }

    if (mp3.empty()) {
        Serial.println("[TtsClient] synth failed (empty MP3)");
        onDone(false);
        return;
    }

    // Hand the buffer off to AudioPlayer. play() copies into its own PSRAM
    // buffer, so we can free ours once the call returns.
    bool ok = audio.play(mp3.buf, mp3.len);
    free(mp3.buf);

    if (!ok) {
        Serial.println("[TtsClient] AudioPlayer::play rejected buffer");
        onDone(false);
        return;
    }

    // Playback started — fire success.
    onDone(true);
}

// Singleton definition.
TtsClient tts;

}  // namespace stkchan
