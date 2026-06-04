#include "TtsClient.h"

#include <algorithm>
#include <ArduinoJson.h>
#include <AudioFileSource.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

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

// ── Streaming MP3 source ───────────────────────────────────────────────────
// Owns the HTTP/TLS client and feeds MP3 bytes to the decoder as they arrive,
// so playback starts on the first frame instead of after the whole download.
// Lifetime: AudioPlayer::playStream() takes ownership and deletes this on
// teardown, which closes the connection (~TtsStreamSource → close()).
class TtsStreamSource : public AudioFileSource {
public:
    TtsStreamSource(WiFiClientSecure* tls, HTTPClient* http)
        : tls_(tls), http_(http) {
        stream_ = http_ ? http_->getStreamPtr() : nullptr;
        size_   = http_ ? http_->getSize() : -1;  // -1 = unknown (HTTP/1.0 close)
    }
    ~TtsStreamSource() override { close(); }

    bool     open(const char*) override { return stream_ != nullptr; }
    bool     isOpen() override          { return stream_ != nullptr; }
    uint32_t getSize() override         { return size_ > 0 ? (uint32_t)size_ : 0x7FFFFFFFu; }
    uint32_t getPos() override          { return pos_; }
    bool     seek(int32_t, int) override { return false; }  // no seeking on a live stream

    uint32_t read(void* dst, uint32_t len) override {
        if (!stream_ || len == 0) return 0;
        uint8_t* out = static_cast<uint8_t*>(dst);
        uint32_t got = 0;
        uint32_t lastByte = millis();
        while (got < len) {
            int avail = stream_->available();
            if (avail > 0) {
                int n = stream_->readBytes(
                    out + got, std::min((uint32_t)avail, len - got));
                if (n > 0) { got += n; lastByte = millis(); }
            } else {
                // Clean EOF: HTTP/1.0 server closed and the buffer is drained.
                if (http_ && !http_->connected() && stream_->available() == 0) break;
                if (millis() - lastByte > 3000) break;  // stall guard
                // Don't busy-block the main loop: hand back what we have and
                // let the decoder ask again next tick. Only wait if we have
                // nothing at all yet.
                if (got > 0) break;
                delay(2);
            }
        }
        pos_ += got;
        return got;
    }

    bool close() override {
        if (http_) { http_->end(); delete http_; http_ = nullptr; }
        if (tls_)  { delete tls_;  tls_  = nullptr; }
        stream_ = nullptr;
        return true;
    }

private:
    WiFiClientSecure* tls_    = nullptr;
    HTTPClient*       http_   = nullptr;
    Stream*           stream_ = nullptr;
    int               size_   = -1;
    uint32_t          pos_    = 0;
};

// Open an authed TTS POST and, on HTTP 200, return a streaming source that
// owns the HTTP/TLS client. Returns nullptr on any failure (client freed).
TtsStreamSource* openStream(const String& provider, const String& text,
                            const String& voice, const String& model,
                            const String& apiKey) {
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

    Serial.printf("[TtsClient] POST %s body=%u chars (streaming)\n",
                  url.c_str(), (unsigned)body.length());
    int code = http->POST(body);
    if (code != 200) {
        Serial.printf("[TtsClient] HTTP %d\n", code);
        http->end(); delete http; delete tls;
        return nullptr;
    }
    return new TtsStreamSource(tls, http);
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

    // Open the streaming POST. On 200 we get a source that owns the HTTP/TLS
    // client; the decoder consumes it off the wire (no full download).
    TtsStreamSource* src = openStream(provider, text, voice, model, apiKey);
    if (!src) {
        Serial.println("[TtsClient] synth failed (no stream)");
        onDone(false);
        return;
    }

    // AudioPlayer takes ownership of the source from here (and frees it +
    // closes the connection on teardown / failure).
    if (!audio.playStream(src)) {
        Serial.println("[TtsClient] AudioPlayer::playStream rejected");
        onDone(false);
        return;
    }

    // Playback started — fire success.
    onDone(true);
}

// Singleton definition.
TtsClient tts;

}  // namespace stkchan
