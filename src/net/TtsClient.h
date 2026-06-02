#pragma once

// TtsClient — cloud TTS HTTP client.
//
// POSTs text to OpenAI /v1/audio/speech or ElevenLabs
// /v1/text-to-speech/<voice_id>, downloads the MP3 response into a
// PSRAM buffer, and hands it to AudioPlayer::play(). Provider is
// chosen at synth time from NVS key kNvsTtsProv ("openai" or
// "elevenlabs"/"eleven"). VOICEVOX is reserved for Phase 2; calling
// synth() with that provider set logs a warning and fires onDone(false).
//
// The call is blocking (HTTP invariant — see CLAUDE.md). The FSM must
// set the display to "Speaking..." before calling synth().
//
// Failure modes: missing api_key, HTTP error, oversized response,
// network unreachable, unsupported provider. All fire onDone(false);
// the FSM falls back to an error TTS or IDLE.

#include <Arduino.h>
#include <functional>

namespace stkchan {

class TtsClient {
public:
    // Synthesize `text` to speech via the configured provider (NVS
    // kNvsTtsProv). Blocks until the MP3 is fetched and handed to
    // AudioPlayer::play(). onDone(true) means AudioPlayer accepted the
    // buffer (playback started); onDone(false) means any failure.
    //
    // NVS keys are read at call time so portal config changes take effect
    // on the next press without a reboot.
    void synth(const String& text, std::function<void(bool)> onDone);
};

// Singleton instance — defined in TtsClient.cpp.
extern TtsClient tts;

}  // namespace stkchan
