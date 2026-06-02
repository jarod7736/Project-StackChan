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
void onAudioDone();   // AudioPlayer onPlayDone callback target

State currentState();

}  // namespace stkchan
