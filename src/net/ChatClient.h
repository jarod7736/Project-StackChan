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
  //
  // brainMode=false (casual): POST to chat_host (direct local Ollama) with
  //   model=chat_model, persona system prompt + 6-turn history. Updates history.
  // brainMode=true: POST to brain_host (oc-personal runner) with
  //   model="oc-personal" + Bearer brain_key, a minimal one-shot payload (no
  //   persona, no history — the agent owns its own context). History untouched.
  //
  // When adv_key is provisioned, a casual (brainMode=false) turn instead routes
  // to the Anthropic Messages API with the advisor tool (fast executor + strong
  // advisor); it keeps history exactly like the casual path.
  //
  // Safe to call from a background task (reads NVS, no shared mutable state
  // beyond the casual ring buffer which brain mode doesn't touch).
  bool send(const String& userMsg, String& out, bool brainMode = false);

 private:
  String system_;
  std::array<Turn, kHistoryTurns * 2> ring_{};
  size_t head_  = 0;        // next write index
  size_t count_ = 0;
  void push_(const String& role, const String& content);

  // Casual turn via the Anthropic advisor tool (executor+advisor). Updates
  // history on success. Gated by adv_key in send().
  bool sendViaAdvisor_(const String& userMsg, String& out);
};

extern ChatClient chat;

}  // namespace stkchan
