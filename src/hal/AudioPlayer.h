#pragma once

// AudioPlayer — MP3 → I²S playback.
//
// Wraps the CoreS3's built-in I2S amplifier (M5.Speaker) with an MP3
// decoder from ESP8266Audio. Plays a buffered MP3 (typically received
// from TtsClient) through M5.Speaker's virtual-channel mixer.
//
// Usage from main.cpp (after T20 FSM wires it up):
//   stkchan::audio.begin();               // once, after M5.Speaker.begin()
//   stkchan::audio.setOnPlayDone([]{ }); // FSM registers SPEAKING → IDLE
//   ...
//   stkchan::audio.play(mp3_bytes, len);  // start playback (non-blocking)
//   ...
//   stkchan::audio.tick();               // every loop() iteration
//
// Single-track: a play() while another is in flight stops the previous
// track first. The FSM never requests two simultaneous TTS playbacks
// (spec invariant: "TTS and ASR cannot run simultaneously").
//
// SPEC INVARIANT: SPEAKING → IDLE is driven ONLY by the onPlayDone
// callback, never by a timer.

#include <Arduino.h>
#include <functional>
#include <memory>

namespace stkchan {

class AudioPlayer {
public:
    using OnDoneCb = std::function<void()>;

    // Initialise M5.Speaker (if not already begun) and configure the
    // virtual channel + sample rate. Idempotent.
    bool begin();

    // True iff begin() succeeded.
    bool ok() const;

    // Hand off an MP3 buffer for playback. Copies the bytes into a PSRAM
    // buffer (up to kMp3MaxBytes). Stops any currently-playing track first.
    // Returns false if not begun, or if the buffer is empty / too large.
    bool play(const uint8_t* mp3, size_t len);

    // Stop and release the current track. Safe to call when nothing is
    // playing. Does NOT fire the OnDone callback — that fires only on
    // a natural end of playback (or decoder error, so FSM can always advance).
    void stop();

    // True while the decoder is still feeding samples to the speaker.
    bool isPlaying() const;

    // Drive the decoder. Call from loop() every iteration. Cheap when
    // nothing is playing.
    void tick();

    // Register a one-shot callback that fires when the current track
    // ends naturally (or on decoder error). Used by the FSM to drive
    // SPEAKING → IDLE.
    void setOnPlayDone(OnDoneCb cb);

    // Set output volume as a 0–100 percentage. Mapped internally to
    // M5.Speaker's 0–255 range. Clamped to [0, 100].
    void setVolume(int pct);

private:
    bool            ok_      = false;
    bool            running_ = false;
    OnDoneCb        on_done_;

    // PSRAM-backed MP3 source buffer. Allocated once in play().
    std::unique_ptr<uint8_t[], decltype(&free)> psram_buf_{nullptr, free};
    size_t psram_len_ = 0;

    // ESP8266Audio objects — allocated per-track, deleted in teardown.
    void* out_ = nullptr;  // AudioOutputM5Speaker* (opaque to header)
    void* src_ = nullptr;  // AudioFileSourceMemory*
    void* mp3_ = nullptr;  // AudioGeneratorMP3*

    void teardown_track();
};

// Singleton instance — defined in AudioPlayer.cpp.
extern AudioPlayer audio;

}  // namespace stkchan
