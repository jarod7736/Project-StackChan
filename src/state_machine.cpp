#include "state_machine.h"
#include "config.h"
#include "hal/MicRecorder.h"
#include "hal/AudioPlayer.h"
#include "hal/Display.h"
#include "face/Face.h"
#include "motion/MotionDirector.h"
#include "face/ExpressionMap.h"
#include "persona/ResponseParser.h"
#include "persona/SystemPrompt.h"
#include "prompts/persona_examples.h"
#include "net/SttClient.h"
#include "net/ChatClient.h"
#include "net/TtsClient.h"
#include "net/ConnectivityTier.h"

namespace stkchan {

static State    g_state         = State::IDLE;
static bool     g_pressFlag     = false;
static bool     g_releaseFlag   = false;
static bool     g_audioDoneFlag = false;
static uint32_t g_pressStartMs  = 0;
static String   g_transcript;
static String   g_replyRaw;
static ParsedReply g_parsed;

State currentState() { return g_state; }

void onPressDown()  { g_pressFlag   = true; }
void onPressUp()    { g_releaseFlag = true; }
void onAudioDone()  { g_audioDoneFlag = true; }

// enterError: face + display + motion, then either speak (→ SPEAKING) or
// set audioDone + ERROR for display-only errors (kErrTtsFailed is "").
//
// Simplified from the plan's double-assignment pattern: SPEAKING is used
// directly for spoken errors; ERROR is only used when there is no speech
// (e.g. kErrTtsFailed == "").  The ERROR case in tickStateMachine drains
// g_audioDoneFlag back to IDLE.
static void enterError(const char* spoken, const char* exprTag) {
  Serial.printf("ERR state: %s\n", spoken ? spoken : "");
  face.setExpression(std::string(exprTag ? exprTag : "neutral"));
  display.showStatusOverlay(String(spoken ? spoken : ""), 0xF800 /* red */);
  motion.pauseIdle();
  if (spoken && *spoken) {
    // Speak the error; audio-done fires onAudioDone() → SPEAKING → IDLE.
    tts.synth(String(spoken), [](bool /*ok*/) { onAudioDone(); });
    g_state = State::SPEAKING;
  } else {
    // No speech — just wait one tick in ERROR then clear to IDLE.
    g_state         = State::ERROR;
    g_audioDoneFlag = true;
  }
}

void initStateMachine() {
  chat.setSystemPrompt(String(kDefaultPersona) + kPersonaExamples);
  audio.setOnPlayDone([]() { onAudioDone(); });
  face.setExpression(std::string("neutral"));
  g_state = State::IDLE;
}

void tickStateMachine(uint32_t nowMs) {
  switch (g_state) {

    case State::IDLE: {
      motion.resumeIdle();
      if (g_pressFlag) {
        g_pressFlag = false;
        // Refuse if connectivity not sufficient.
        auto t = connectivity.current();
        if (t == Tier::NO_WIFI)        { enterError(kErrNoWifi,      "sleepy"); break; }
        if (t == Tier::LAN_NO_BACKEND) { enterError(kErrChatOffline, "doubt");  break; }
        // Start LISTENING.
        face.setExpression(std::string("neutral"));
        display.showStatusOverlay("listening...", 0x07E0);
        motion.pauseIdle();
        if (!mic.start()) { enterError(kErrMicEmpty, "doubt"); break; }
        g_pressStartMs = nowMs;
        g_state = State::LISTENING;
      }
      break;
    }

    case State::LISTENING: {
      bool tooLong = (nowMs - g_pressStartMs) >= kMaxRecordMs;
      if (g_releaseFlag || tooLong) {
        g_releaseFlag = false;
        mic.stop();
        if (mic.wavSize() < 1024) { enterError(kErrMicEmpty, "doubt"); break; }
        face.setExpression(std::string("neutral"));
        // Show "thinking..." BEFORE the blocking STT HTTP call.
        display.showStatusOverlay("thinking...", 0xFFE0);
        g_state = State::THINKING_STT;
      }
      break;
    }

    case State::THINKING_STT: {
      // Blocking HTTP call. Display already shows "thinking...".
      if (!stt.transcribe(mic.wavData(), mic.wavSize(), g_transcript)) {
        enterError(kErrSttFailed, "sad");
        break;
      }
      if (g_transcript.isEmpty()) { enterError(kErrMicEmpty, "doubt"); break; }
      Serial.printf("USER: %s\n", g_transcript.c_str());
      g_state = State::THINKING_CHAT;
      break;
    }

    case State::THINKING_CHAT: {
      // Display already shows "thinking..." from LISTENING transition.
      if (!chat.send(g_transcript, g_replyRaw)) {
        enterError(kErrChatFailed, "doubt");
        break;
      }
      Serial.printf("LLM RAW: %s\n", g_replyRaw.c_str());
      g_parsed = parseReply(std::string(g_replyRaw.c_str()));
      Serial.printf("PARSED: speech='%s' expr='%s'\n",
                    g_parsed.speech.c_str(), g_parsed.expr.c_str());
      // Set expression & motion before TTS HTTP call.
      face.setExpression(g_parsed.expr);
      motion.onExpressionChange(g_parsed.expr);
      motion.startSpeechBob(expressionFor(g_parsed.expr).bobAmp);
      g_state = State::SPEAKING_TTS;
      break;
    }

    case State::SPEAKING_TTS: {
      // synth() is blocking; onDone fires when AudioPlayer accepts the buffer
      // (playback started) or on failure (ok=false).
      tts.synth(String(g_parsed.speech.c_str()), [](bool ok) {
        if (!ok) {
          // kErrTtsFailed is "" — no spoken error, just let FSM advance.
          onAudioDone();
        }
        // On ok=true, AudioPlayer is playing; onPlayDone fires onAudioDone().
      });
      // Move to SPEAKING to wait for audio-done (either natural end or error).
      g_state = State::SPEAKING;
      break;
    }

    case State::SPEAKING: {
      audio.tick();  // drives the MP3 decoder; fires onPlayDone when done
      if (g_audioDoneFlag) {
        g_audioDoneFlag = false;
        motion.stopSpeechBob();
        face.setExpression(std::string("neutral"));
        display.clearOverlay();
        g_state = State::IDLE;
      }
      break;
    }

    case State::ERROR: {
      // Only reached for display-only (no-speech) errors; audioDoneFlag is
      // already set by enterError in that path.  Clear and return to IDLE.
      if (g_audioDoneFlag) {
        g_audioDoneFlag = false;
        display.clearOverlay();
        face.setExpression(std::string("neutral"));
        g_state = State::IDLE;
      }
      break;
    }
  }
}

}  // namespace stkchan
