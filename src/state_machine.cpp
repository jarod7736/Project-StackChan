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
#include "services/NvsStore.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace stkchan {

static State    g_state         = State::IDLE;
static bool     g_pressFlag     = false;
static bool     g_releaseFlag   = false;
static bool     g_audioDoneFlag = false;
static uint32_t g_pressStartMs  = 0;
static String   g_transcript;
static String   g_replyRaw;
static ParsedReply g_parsed;

// Background chat task: chat.send() can block 5–30 s (esp. the oc-personal
// agent), so we run it off the main loop to keep the LVGL face animating.
static TaskHandle_t  g_chatTask  = nullptr;
static volatile bool g_chatDone  = false;
static volatile bool g_chatOk    = false;
static bool          g_brainMode = false;

// Case-insensitive substring scan of the transcript for any brain stem.
// Routes brain/email/calendar utterances to the oc-personal agent.
static bool transcriptWantsBrain(const String& transcript) {
  String low = transcript;
  low.toLowerCase();
  // Optional NVS CSV override of the default stem list.
  String csv = nvs.getString(kNvsBrainKw, "");
  if (csv.length()) {
    csv.toLowerCase();
    int start = 0;
    while (start < (int)csv.length()) {
      int comma = csv.indexOf(',', start);
      if (comma < 0) comma = csv.length();
      String stem = csv.substring(start, comma); stem.trim();
      if (stem.length() && low.indexOf(stem) >= 0) return true;
      start = comma + 1;
    }
    return false;
  }
  for (size_t i = 0; i < kBrainStemCount; ++i) {
    if (low.indexOf(kBrainStems[i]) >= 0) return true;
  }
  return false;
}

// One-shot worker: runs the (blocking) chat call, then flags completion.
// g_transcript / g_brainMode are set by the FSM before spawn and not touched
// again until g_chatDone is observed, so no extra locking is needed.
static void chatTaskFn(void* /*arg*/) {
  g_chatOk = chat.send(g_transcript, g_replyRaw, g_brainMode);
  g_chatDone = true;   // publish last
  g_chatTask = nullptr;
  vTaskDelete(nullptr);
}

State currentState() { return g_state; }

void onPressDown()  { g_pressFlag   = true; }
void onPressUp()    { g_releaseFlag = true; }
void onAudioDone()  { g_audioDoneFlag = true; }

bool requestExternalSpeak(const String& text, const char* exprTag) {
  if (g_state != State::IDLE) return false;       // busy with a voice turn
  if (text.isEmpty()) return false;
  std::string expr = (exprTag && *exprTag) ? std::string(exprTag) : "neutral";
  g_parsed.speech = std::string(text.c_str());
  g_parsed.expr   = expr;
  g_parsed.ok     = true;
  // Mirror the THINKING_CHAT → SPEAKING_TTS handoff: set face/motion, then
  // let the SPEAKING_TTS tick run the (blocking) synth + playback.
  face.setExpression(expr);
  motion.onExpressionChange(expr);
  motion.startSpeechBob(expressionFor(expr).bobAmp);
  g_state = State::SPEAKING_TTS;
  return true;
}

void onPressCancel() {
  // Abort whatever the FSM is doing right now (mic recording, etc.) and
  // return cleanly to IDLE without speaking an error. Called by main.cpp
  // when a swipe-up gesture is detected mid-press.
  g_pressFlag   = false;
  g_releaseFlag = false;
  if (mic.isActive()) mic.stop();
  if (g_state == State::LISTENING ||
      g_state == State::THINKING_STT ||
      g_state == State::THINKING_CHAT) {
    face.setExpression(std::string("neutral"));
    display.clearOverlay();
    motion.resumeIdle();
    g_state = State::IDLE;
  }
}

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
      // Route: brain-stem utterances → oc-personal agent, else casual local.
      g_brainMode = transcriptWantsBrain(g_transcript);
      Serial.printf("[FSM] chat route: %s\n", g_brainMode ? "BRAIN (oc-personal)" : "casual");
      if (g_brainMode) {
        face.setExpression("doubt");           // "hmm, let me check…"
        display.showStatusOverlay("checking...", 0x07E0);
      }
      // Run the (blocking, possibly slow) chat call on a background task so
      // the LVGL face + idle motion keep animating during the wait. 16 KB
      // stack covers mbedTLS if brain_host is https. Pin to core 0 (the
      // Arduino loop runs on core 1).
      g_chatDone = false;
      g_chatOk   = false;
      if (xTaskCreatePinnedToCore(chatTaskFn, "chat", 16384, nullptr, 1,
                                  &g_chatTask, 0) != pdPASS) {
        Serial.println("[FSM] chat task spawn failed");
        enterError(kErrChatFailed, "doubt");
        break;
      }
      motion.resumeIdle();   // keep the head alive while we wait
      g_state = State::THINKING_CHAT_WAIT;
      break;
    }

    case State::THINKING_CHAT_WAIT: {
      // Worker is running chat.send() off-core; the loop keeps ticking LVGL +
      // idle motion so the face animates. Just watch for completion.
      if (!g_chatDone) break;
      g_chatDone = false;
      if (!g_chatOk) {
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
      // Render "speaking..." before the blocking synth (render-before-HTTP).
      display.showStatusOverlay("speaking...", 0x07E0);
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
