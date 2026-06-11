#include "state_machine.h"
#include "config.h"
#include "hal/MicRecorder.h"
#include "hal/AudioPlayer.h"
#include "hal/Display.h"
#include "hal/WavHeader.h"
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
#include "app/WakeListener.h"
#include "app/WakeVad.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

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

// Wake-word integration state.
static bool               g_openListenAfterAck = false;  // ack done → open wake window
static bool               g_wakeOpenedListen   = false;  // LISTENING entered via wake
static uint32_t           g_wakeListenStartMs  = 0;
static stkchan::WakeVad   g_openVad;                     // end-of-speech for wake windows

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
  wakeListener.pause();  // speaker needs I2S0 back before any playback
  face.setExpression(expr);
  motion.onExpressionChange(expr);
  motion.startSpeechBob(expressionFor(expr).bobAmp);
  g_state = State::SPEAKING_TTS;
  return true;
}

bool onWakeDetected(const String& remainder) {
  if (g_state != State::IDLE) return false;
  auto t = connectivity.current();
  if (t != Tier::LAN_OK) return false;  // silent refusal — no error speech

  motion.pauseIdle();
  if (remainder.length() > 0) {
    // One-breath UX: the rest of the utterance IS the request.
    g_transcript = remainder;
    Serial.printf("USER (wake): %s\n", g_transcript.c_str());
    face.setExpression(std::string("neutral"));
    display.showStatusOverlay("thinking...", 0xFFE0);
    g_state = State::THINKING_CHAT;
  } else {
    // Bare wake: instant offline ack (stock clip), then open a window.
    // WakeListener already paused itself before calling us, so the speaker
    // is back online for the clip.
    g_openListenAfterAck = true;
    face.setExpression(std::string("happy"));
    tts.synth(String(kWakeAck), [](bool /*ok*/) { onAudioDone(); });
    g_state = State::SPEAKING;
  }
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
  wakeListener.pause();
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
        wakeListener.pause();  // release I2S0 for MicRecorder
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
      bool stopNow = false;
      if (g_wakeOpenedListen) {
        g_releaseFlag = false;  // presses are ignored in a wake window
        // Feed 30 ms frames from MicRecorder's live buffer to the VAD.
        // Fill estimated from elapsed time (same trick as MicRecorder::stop),
        // one frame behind to avoid reading a partially-written frame.
        uint32_t elapsed = nowMs - g_wakeListenStartMs;
        size_t   filled  = ((size_t)elapsed * kRecordSampleRate / 1000)
                           / kWakeFrameSamples;
        if (filled > 0) filled -= 1;
        // Clamp to MicRecorder's PCM capacity (mirror of WakeListener's
        // processNewFrames_ clamp) — a stalled tick must not walk past it.
        size_t maxFrames = (kRecordMaxBytes / sizeof(int16_t)) / kWakeFrameSamples;
        if (filled > maxFrames) filled = maxFrames;
        static size_t s_fed = 0;  // frames fed this window; reset on close
        const int16_t* pcm = reinterpret_cast<const int16_t*>(
            mic.wavData() + kWavHeaderBytes);
        while (s_fed < filled) {
          float rms = 0;
          {
            const int16_t* f = pcm + s_fed * kWakeFrameSamples;
            float acc = 0;
            for (size_t i = 0; i < kWakeFrameSamples; ++i)
              acc += (float)f[i] * (float)f[i];
            rms = sqrtf(acc / (float)kWakeFrameSamples);
          }
          auto ev = g_openVad.onFrame(rms);
          ++s_fed;
          if (ev == stkchan::WakeVad::Event::Closed ||
              ev == stkchan::WakeVad::Event::Overflow) { stopNow = true; break; }
        }
        if (elapsed >= kWakeOpenMaxMs) stopNow = true;
        if (stopNow) s_fed = 0;  // reset for the next window
        if (!stopNow) break;
      } else {
        bool tooLong = (nowMs - g_pressStartMs) >= kMaxRecordMs;
        stopNow = g_releaseFlag || tooLong;
        if (stopNow) g_releaseFlag = false;
      }
      if (!stopNow) break;
      g_wakeOpenedListen = false;
      mic.stop();
      if (mic.wavSize() < 1024) { enterError(kErrMicEmpty, "doubt"); break; }
      face.setExpression(std::string("neutral"));
      display.showStatusOverlay("thinking...", 0xFFE0);
      g_state = State::THINKING_STT;
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
        if (g_openListenAfterAck) {
          g_openListenAfterAck = false;
          // Open a wake-listening window — like a press, but VAD-terminated.
          face.setExpression(std::string("neutral"));
          display.showStatusOverlay("listening...", 0x07E0);
          if (!mic.start()) { enterError(kErrMicEmpty, "doubt"); break; }
          g_wakeOpenedListen  = true;
          g_wakeListenStartMs = nowMs;
          stkchan::WakeVadConfig wcfg;
          wcfg.closeSilenceFrames = (int)(kWakeOpenSilenceMs / 30);  // ~800 ms
          wcfg.maxFrames          = (int)(kWakeOpenMaxMs / 30);
          g_openVad = stkchan::WakeVad(wcfg);
          g_state = State::LISTENING;
          break;
        }
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
