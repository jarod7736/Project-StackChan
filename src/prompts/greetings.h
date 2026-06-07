#pragma once
#include <stddef.h>

namespace stkchan {

// Short kawaii arrival greetings. These are spoken via
// requestExternalSpeak(text, "happy") and bypass the LLM/ResponseParser, so they
// carry NO <speech>/<expr> tags — just the literal line. Voice matches
// persona/SystemPrompt.h (playful, easily delighted, 1 short line).
constexpr const char* kGreetings[] = {
    "Oh! Hi there.",
    "There you are!",
    "Hello again.",
    "Hi! You're back.",
    "Oh, hey!",
    "Yay, company!",
};
constexpr size_t kGreetingCount = sizeof(kGreetings) / sizeof(kGreetings[0]);

// Round-robin pick — deterministic, no Arduino RNG dependency.
inline const char* pickGreeting() {
    static size_t i = 0;
    const char* g = kGreetings[i % kGreetingCount];
    ++i;
    return g;
}

}  // namespace stkchan
