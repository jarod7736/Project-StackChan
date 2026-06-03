#include "AudioPlayer.h"

#include <M5Unified.h>

// ESP8266Audio — runs on ESP32-S3 despite the name. See platformio.ini.
#include <AudioFileSource.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>

#include "../config.h"

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

    bool ConsumeSample(int16_t sample[2]) override {
        if (idx_ < kBufSize) {
            buf_[bank_][idx_]     = sample[0];
            buf_[bank_][idx_ + 1] = sample[1];
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
    m5::Speaker_Class* spk_;
    uint8_t            vch_;
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
// 4.0 lifts the floor noticeably without audible clipping on
// conversational voice. Raised to 4.0 in Jarvis PR #57.
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
    auto* src = static_cast<AudioFileSourceMemory*>(src_);
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

    // Re-apply the speaker config every play. MicRecorder::stop() restores
    // the speaker after the mic owned I2S, but it does a bare M5.Speaker.
    // begin() which loses our sample-rate (kSampleRate) + virtual-channel
    // settings — output sounds like static until config is restored. Doing
    // it here makes play() self-healing regardless of who else touched the
    // speaker.
    auto cfg = M5.Speaker.config();
    cfg.sample_rate = kSampleRate;
    M5.Speaker.config(cfg);
    M5.Speaker.begin();
    auto* out = static_cast<AudioOutputM5Speaker*>(out_);
    if (out) out->SetGain(kDecoderGain);

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
    }
    if (!mp3->isRunning()) {
        Serial.println("[AudioPlayer] track done");
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
    uint8_t m5_vol = static_cast<uint8_t>((pct * 255) / 100);
    M5.Speaker.setVolume(m5_vol);
}

}  // namespace stkchan
