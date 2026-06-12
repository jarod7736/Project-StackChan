#include "WakeMatch.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace stkchan {

std::string normalizeTranscript(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) out += static_cast<char>(std::tolower(u));
  }
  return out;
}

namespace {

// Classic two-row Levenshtein. Inputs are short (wake phrases ~12 chars).
int editDistance(const std::string& a, const std::string& b) {
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = static_cast<int>(i);
    for (size_t j = 1; j <= b.size(); ++j) {
      int sub = prev[j - 1] + (a[i - 1] != b[j - 1] ? 1 : 0);
      cur[j]  = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
    }
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

}  // namespace

WakeMatchResult matchWake(const std::string& transcript,
                          const std::string& wakePhrase, int maxEdits) {
  const std::string normWake = normalizeTranscript(wakePhrase);
  if (normWake.empty()) return {false, ""};

  // Build the normalized transcript plus a map: normalized char k → original
  // index just AFTER that char (so we can cut the remainder in original text).
  std::string norm;
  std::vector<size_t> after;
  for (size_t i = 0; i < transcript.size(); ++i) {
    unsigned char u = static_cast<unsigned char>(transcript[i]);
    if (std::isalnum(u)) {
      norm += static_cast<char>(std::tolower(u));
      after.push_back(i + 1);
    }
  }
  if (norm.size() + static_cast<size_t>(maxEdits) < normWake.size())
    return {false, ""};

  // Try candidate prefix lengths around the wake phrase's length; keep the
  // best edit distance.
  int    bestD = maxEdits + 1;
  size_t bestL = 0;
  size_t lo = normWake.size() > static_cast<size_t>(maxEdits)
                  ? normWake.size() - maxEdits : 1;
  size_t hi = std::min(norm.size(), normWake.size() + maxEdits);
  for (size_t L = lo; L <= hi; ++L) {
    int d = editDistance(norm.substr(0, L), normWake);
    if (d < bestD) { bestD = d; bestL = L; }
  }
  if (bestD > maxEdits) return {false, ""};

  std::string rem = transcript.substr(after[bestL - 1]);
  size_t p = rem.find_first_not_of(" ,.!?;:-");
  rem = (p == std::string::npos) ? "" : rem.substr(p);
  size_t q = rem.find_last_not_of(' ');
  if (q != std::string::npos) rem = rem.substr(0, q + 1);
  return {true, rem};
}

WakeMatchResult matchWakeVariants(const std::string& transcript,
                                  const std::string& wakePhrase) {
  WakeMatchResult r = matchWake(transcript, wakePhrase);
  if (r.matched) return r;
  // Live-collected Whisper mis-hearings of "hey stack chan". Each is matched
  // at the same tight tolerance as the canonical phrase.
  static const char* kVariants[] = {
      "hey stat chan",   // observed 2026-06-11 ("Hey, Stat Chan.")
      "hey stack jam",   // observed 2026-06-11 ("Hey, Stack Jam.")
      "hey stack chen",
      "hey stack john",
  };
  for (const char* v : kVariants) {
    r = matchWake(transcript, v);
    if (r.matched) return r;
  }
  return {false, ""};
}

}  // namespace stkchan
