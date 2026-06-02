#pragma once
#include <Arduino.h>
#include <array>
#include "config.h"

namespace stkchan {

struct Turn {
  String role;     // "user" | "assistant"
  String content;
};

class ChatClient {
 public:
  void setSystemPrompt(const String& sp) { system_ = sp; }
  void clearHistory();

  // Sends `userMsg`. On success returns true and fills `out` with the
  // assistant's content. On any failure returns false.
  bool send(const String& userMsg, String& out);

 private:
  String system_;
  std::array<Turn, kHistoryTurns * 2> ring_{};
  size_t head_  = 0;        // next write index
  size_t count_ = 0;
  void push_(const String& role, const String& content);
};

extern ChatClient chat;

}  // namespace stkchan
