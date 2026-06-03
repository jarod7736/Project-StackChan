#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace stkchan {

enum class State : uint8_t {
  IDLE,
  LISTENING,
  THINKING_STT,
  THINKING_CHAT,
  SPEAKING_TTS,
  SPEAKING,
  ERROR,
};

void initStateMachine();
void tickStateMachine(uint32_t nowMs);

// Called by main loop / hal callbacks to set flags.
void onPressDown();
void onPressUp();
void onPressCancel();  // abort an in-progress press (e.g. swipe-up gesture)
void onAudioDone();   // AudioPlayer onPlayDone callback target

// Speak arbitrary text on demand (web "say this"), reusing the TTS/speak
// path. exprTag drives the face/motion while speaking. Returns false if the
// FSM is busy (not IDLE). Call from the main loop task only.
bool requestExternalSpeak(const String& text, const char* exprTag);

State currentState();

}  // namespace stkchan
