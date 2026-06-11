#pragma once

// WakeMatch — fuzzy wake-phrase prefix matching over an STT transcript.
// Pure C++ (no Arduino) so it runs under `pio test -e native`.
// Spec: docs/superpowers/specs/2026-06-11-wake-word-design.md §3.

#include <string>

namespace stkchan {

struct WakeMatchResult {
  bool        matched;
  std::string remainder;  // original-text words after the wake phrase, trimmed
};

// Lowercase; keep only [a-z0-9]. "Hey, Stack-Chan!" → "heystackchan".
std::string normalizeTranscript(const std::string& s);

// True if a prefix of the normalized transcript (within ±maxEdits of the
// normalized wake phrase's length) is within `maxEdits` edit distance of the
// normalized wake phrase. remainder = original transcript after that prefix.
WakeMatchResult matchWake(const std::string& transcript,
                          const std::string& wakePhrase, int maxEdits = 2);

}  // namespace stkchan
