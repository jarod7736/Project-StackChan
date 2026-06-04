#include "AudioPlayer.h"

#include <M5Unified.h>

// ESP8266Audio — runs on ESP32-S3 despite the name. See platformio.ini.
#include <AudioFileSource.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>

#include "../config.h"
#include "../face/Face.h"
#include "../services/NvsStore.h"

#include <math.h>

namespace {
// User-controlled speaker volume (0-255). Loaded from NVS on begin(),
// re-applied after every M5.Speaker.begin() (including post-mic via
// reapplySpeakerConfig). Updated by AudioPlayer::setVolume().
int g_user_volume = 0;
}

// Pull in ESP8266Audio concrete types here (not in the header) so
// consumers of AudioPlayer.h don't need ESP8266Audio on their include path.
//
// The anonymous namespace contains:
//   - AudioOutputM5Speaker  — PCM → M5.Speaker bridge
//   - AudioFileSourceMemory — read from a PSRAM heap buffer

namespace stkchan {

namespace {

// ── Custom AudioOutput → M5.Speaker bridge ────────────────────────────────
// Pattern lifted from M5Unified's MP3_with_ESP8266Audio example. Decoded
// PCM samples (stereo int16) are buffered in a triple buffer and pushed
// to M5.Speaker.playRaw() in chunks. Triple buffering smooths audio
// over the decoder's irregular frame timing without growing latency.
class AudioOutputM5Speaker : public AudioOutput {
public:
    explicit AudioOutputM5Speaker(m5::Speaker_Class* spk,
                                  uint8_t virtual_channel = 0)
        : spk_(spk), vch_(virtual_channel) {}

    bool begin() override { return true; }

    // Capture the gain locally — the base class stores it but it's only
    // applied if WE multiply samples by it, which is exactly what was
    // missing before (gain was a silent no-op, so audio was always 1x).
    bool SetGain(float f) override {
        gain_ = f;
        return AudioOutput::SetGain(f);
    }

    bool ConsumeSample(int16_t sample[2]) override {
        if (idx_ < kBufSize) {
            buf_[bank_][idx_]     = amplify(sample[0]);
            buf_[bank_][idx_ + 1] = amplify(sample[1]);
            idx_ += 2;
            return true;
        }
        flush();
        return false;
    }

    void flush() override {
        if (idx_ == 0) return;
        spk_->playRaw(buf_[bank_], idx_, hertz, /*stereo=*/true,
                      /*repeat=*/1, vch_);
        bank_ = (bank_ + 1) % 3;
        idx_  = 0;
    }

    bool stop() override {
        flush();
        spk_->stop(vch_);
        return true;
    }

private:
    // Software amplify with hard clamp. Voice TTS is low-amplitude so a
    // few× boost is well below clipping on most syllables.
    inline int16_t amplify(int16_t s) const {
        int32_t v = (int32_t)(s * gain_);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        return (int16_t)v;
    }

    m5::Speaker_Class* spk_;
    uint8_t            vch_;
    float              gain_ = 1.0f;
    // 1536 int16_t = 3 KB per bank — fits in stack/DRAM, small enough that
    // three banks don't threaten the heap, large enough to amortize
    // playRaw() call overhead.
    static constexpr size_t kBufSize = 1536;
    int16_t            buf_[3][kBufSize] = {};
    size_t             bank_ = 0;
    size_t             idx_  = 0;
};

// ── In-memory AudioFileSource ─────────────────────────────────────────────
// ESP8266Audio's bundled sources are file/HTTP/PROGMEM. We've already
// downloaded the MP3 into a PSRAM heap buffer, so the lightest path is
// a tiny adapter that streams from that buffer. Implements only what
// AudioGeneratorMP3 actually calls: read, seek, getPos, getSize,
// isOpen, close.
class AudioFileSourceMemory : public AudioFileSource {
public:
    AudioFileSourceMemory(const uint8_t* data, size_t len)
        : data_(data), len_(len), pos_(0), open_(true) {}

    bool open(const char* /*filename*/) override { return open_ = true; }
    bool close() override                         { open_ = false; return true; }
    bool isOpen() override                        { return open_; }
    uint32_t getSize() override                   { return (uint32_t)len_; }
    uint32_t getPos() override                    { return (uint32_t)pos_; }

    uint32_t read(void* dst, uint32_t bytes) override {
        if (pos_ >= len_) return 0;
        size_t n = (size_t)bytes;
        if (n > len_ - pos_) n = len_ - pos_;
        memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return (uint32_t)n;
    }

    bool seek(int32_t off, int dir) override {
        size_t newpos;
        if (dir == SEEK_SET)      newpos = (size_t)off;
        else if (dir == SEEK_CUR) newpos = pos_ + (size_t)off;
        else if (dir == SEEK_END) newpos = len_ + (size_t)off;  // off is negative
        else return false;
        if (newpos > len_) return false;
        pos_ = newpos;
        return true;
    }

private:
    const uint8_t* data_;
    size_t         len_;
    size_t         pos_;
    bool           open_;
};

// ── Player constants ───────────────────────────────────────────────────────

constexpr uint8_t  kVirtualChannel = 0;

// Match the OpenAI TTS MP3 output rate (24 kHz mono). The earlier
// 96 kHz value was lifted from an M5Unified example that played higher-
// rate source material; with a 4× mismatch the speaker driver's
// resampler stretched playback and introduced amplitude loss.
constexpr uint32_t kSampleRate     = 24000;

// Gain multiplier applied by the MP3 decoder before samples reach the
// speaker. OpenAI's `onyx` voice is naturally deep/low-amplitude and
// the CoreS3 onboard amp has a low ceiling, so even setVolume(100)
// leaves voice replies barely loud enough for across-the-room hearing.
// NOTE: until now this gain was never actually applied (ConsumeSample
// copied samples raw), so effective gain was 1.0×. Now that amplify()
// applies it for real, 4.0 = a genuine 4× boost over what shipped before.
// 7.0 would hard-clip; raise/lower from 4.0 to taste vs distortion.
constexpr float    kDecoderGain    = 4.0f;

// Max time AudioPlayer::tick() may spend decoding per main-loop tick.
// The main loop has ~30-50 ms of other work per iteration (display
// refresh, MQTT keepalive, etc.); if we decode only one MP3 frame per
// tick the I2S DMA buffer drains faster than we refill it and audio
// becomes jerky / stretched. Draining for up to 25 ms keeps us ahead
// of playback while still leaving time for display animation. This is
// the Jarvis PR #56 decoder starvation fix.
constexpr uint32_t kDecodeBudgetMs = 25;

}  // namespace

// ── Singleton ────────────────────────────────────────────────────────────
AudioPlayer audio;

// ── AudioPlayer implementation ────────────────────────────────────────────

void AudioPlayer::teardown_track() {
    auto* mp3 = static_cast<AudioGeneratorMP3*>(mp3_);
    // Delete through the base pointer (virtual dtor) so this works for both
    // AudioFileSourceMemory and the streaming source (whose dtor closes the
    // HTTP/TLS client). AudioFileSource has a virtual destructor.
    auto* src = static_cast<AudioFileSource*>(src_);
    if (mp3 && mp3->isRunning()) mp3->stop();
    if (src) { src->close(); delete src; src_ = nullptr; }
    delete mp3; mp3_ = nullptr;
    // Release PSRAM buffer
    psram_buf_.reset();
    psram_len_ = 0;
    running_ = false;
}

bool AudioPlayer::begin() {
    if (ok_) return true;

    // Bring up M5.Speaker. M5.begin() in main.cpp initialises power rails
    // but doesn't auto-start the speaker on every CoreS3 firmware revision.
    // Calling begin() again is harmless if it's already up.
    auto cfg = M5.Speaker.config();
    cfg.sample_rate = kSampleRate;
    M5.Speaker.config(cfg);
    if (!M5.Speaker.begin()) {
        Serial.println("[AudioPlayer] M5.Speaker.begin failed");
        return false;
    }
    // Load user-set volume from NVS (falls back to default if unset).
    String vstr = stkchan::nvs.getString(stkchan::kNvsSpkVolume, "");
    g_user_volume = vstr.isEmpty() ? stkchan::kDefaultSpkVolume : vstr.toInt();
    if (g_user_volume < 0)   g_user_volume = 0;
    if (g_user_volume > 255) g_user_volume = 255;
    M5.Speaker.setVolume(g_user_volume);

    auto* out = new AudioOutputM5Speaker(&M5.Speaker, kVirtualChannel);
    if (!out) {
        Serial.println("[AudioPlayer] alloc AudioOutputM5Speaker failed");
        return false;
    }
    // Boost decoder output so low-amplitude voices reach a usable level.
    // SetGain multiplies each sample before output — applied once at init
    // so every track inherits it.
    out->SetGain(kDecoderGain);
    out_ = out;

    Serial.printf("[AudioPlayer] ready (sample_rate=%u, vch=%u, gain=%.1f)\n",
                  (unsigned)kSampleRate, (unsigned)kVirtualChannel,
                  (double)kDecoderGain);
    ok_ = true;
    return true;
}

bool AudioPlayer::ok() const { return ok_; }

void AudioPlayer::reapplySpeakerConfig() {
    auto cfg = M5.Speaker.config();
    cfg.sample_rate = kSampleRate;
    M5.Speaker.config(cfg);
    M5.Speaker.begin();
    // M5.Speaker.begin() may reset volume after an end()/begin() cycle
    // (post-Mic). Restore the user's chosen volume.
    M5.Speaker.setVolume(g_user_volume > 0 ? g_user_volume : stkchan::kDefaultSpkVolume);
    if (auto* spk = static_cast<AudioOutputM5Speaker*>(out_)) {
        spk->SetGain(kDecoderGain);
    }
}

bool AudioPlayer::play(const uint8_t* mp3, size_t len) {
    if (!ok_) {
        Serial.println("[AudioPlayer] play() before begin()");
        return false;
    }
    if (!mp3 || len == 0) {
        Serial.println("[AudioPlayer] play() got empty buffer");
        return false;
    }
    if (len > stkchan::kMp3MaxBytes) {
        Serial.printf("[AudioPlayer] play() buffer too large (%u > %u)\n",
                      (unsigned)len, (unsigned)stkchan::kMp3MaxBytes);
        return false;
    }

    teardown_track();

    // Allocate PSRAM buffer and copy MP3 bytes (spec: any buffer > 512 B
    // goes in PSRAM via ps_malloc).
    uint8_t* buf = static_cast<uint8_t*>(ps_malloc(len));
    if (!buf) {
        Serial.println("[AudioPlayer] ps_malloc failed");
        return false;
    }
    memcpy(buf, mp3, len);
    psram_buf_ = std::unique_ptr<uint8_t[], decltype(&free)>(buf, free);
    psram_len_ = len;

    auto* src = new AudioFileSourceMemory(psram_buf_.get(), psram_len_);
    auto* dec = new AudioGeneratorMP3();
    if (!src || !dec) {
        Serial.println("[AudioPlayer] alloc decoder failed");
        teardown_track();
        return false;
    }
    src_ = src;
    mp3_ = dec;

    auto* out = static_cast<AudioOutputM5Speaker*>(out_);
    if (!dec->begin(src, out)) {
        Serial.println("[AudioPlayer] mp3.begin failed");
        teardown_track();
        return false;
    }
    running_ = true;
    Serial.printf("[AudioPlayer] playing %u-byte MP3\n", (unsigned)len);
    return true;
}

bool AudioPlayer::playStream(void* sourcePtr) {
    // Take ownership immediately: on EVERY false return below we free the
    // source (closing its HTTP/TLS client), so the caller never has to.
    auto* src = static_cast<AudioFileSource*>(sourcePtr);
    if (!ok_) {
        Serial.println("[AudioPlayer] playStream() before begin()");
        if (src) { src->close(); delete src; }
        return false;
    }
    if (!src) return false;

    teardown_track();

    // The streaming source owns the HTTP/TLS client and reads MP3 bytes as
    // they arrive. No PSRAM copy — we decode straight off the wire, so
    // time-to-first-audio is just the first frame, not the whole download.
    auto* dec = new AudioGeneratorMP3();
    if (!dec) {
        src->close();
        delete src;
        return false;
    }
    src_ = src;
    mp3_ = dec;

    auto* out = static_cast<AudioOutputM5Speaker*>(out_);
    if (!dec->begin(src, out)) {
        Serial.println("[AudioPlayer] stream mp3.begin failed");
        teardown_track();
        return false;
    }
    running_ = true;
    Serial.println("[AudioPlayer] streaming playback started");
    return true;
}

void AudioPlayer::stop() {
    if (!running_) return;
    Serial.println("[AudioPlayer] stop()");
    teardown_track();
    // stop() does NOT fire on_done_ — only natural playback completion does.
}

bool AudioPlayer::isPlaying() const {
    auto* mp3 = static_cast<AudioGeneratorMP3*>(mp3_);
    return running_ && mp3 && mp3->isRunning();
}

void AudioPlayer::tick() {
    if (!running_ || !mp3_) return;
    auto* mp3 = static_cast<AudioGeneratorMP3*>(mp3_);
    if (mp3->isRunning()) {
        // Drain multiple MP3 frames per tick up to kDecodeBudgetMs.
        // One frame per main-loop tick is too slow: the loop spends
        // 30-50 ms on display / MQTT / WiFi between AudioPlayer ticks
        // and the I2S DMA buffer drains in less than that, producing
        // jerky / stretched playback (Jarvis PR #56 fix).
        uint32_t deadline = millis() + kDecodeBudgetMs;
        while (mp3->isRunning() && (int32_t)(deadline - millis()) > 0) {
            if (!mp3->loop()) {
                // loop() returns false on natural end-of-stream OR a
                // fatal decoder error. Treat both as "done" — fire
                // callback so FSM can advance out of SPEAKING either way.
                mp3->stop();
                break;
            }
        }
        // Cheap fake lip-sync: oscillate mouth open ratio on a ~6 Hz sine
        // while audio is playing. Not amplitude-tracked but it reads as
        // "the mouth is moving while speaking", which is enough for v1.5.
        float t = (float)millis() * 0.012f;
        float r = 0.35f + 0.35f * (0.5f + 0.5f * sinf(t));
        face.setMouthOpen(r);
    }
    if (!mp3->isRunning()) {
        Serial.println("[AudioPlayer] track done");
        face.setMouthOpen(0.0f);
        teardown_track();
        // Fire onPlayDone AFTER teardown so the callback can immediately
        // call play() for a queued utterance without reentrancy issues.
        if (on_done_) on_done_();
    }
}

void AudioPlayer::setOnPlayDone(OnDoneCb cb) {
    on_done_ = std::move(cb);
}

void AudioPlayer::setVolume(int pct) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    // M5.Speaker.setVolume takes 0–255. Linear map.
    int m5_vol = (pct * 255) / 100;
    g_user_volume = m5_vol;
    M5.Speaker.setVolume((uint8_t)m5_vol);
    // Persist to NVS so it survives reboot.
    stkchan::nvs.putString(stkchan::kNvsSpkVolume, String(m5_vol));
}

int AudioPlayer::getVolume() const {
    // Map current 0-255 back to 0-100%.
    return (g_user_volume * 100) / 255;
}

}  // namespace stkchan
