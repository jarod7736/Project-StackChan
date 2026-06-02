#pragma once
#include <string>

namespace stkchan {

struct ParsedReply {
  std::string speech;   // never empty for a non-empty input
  std::string expr;     // always one of: neutral, happy, sad, angry, doubt, sleepy
  bool        ok;       // true if both tags parsed cleanly
};

ParsedReply parseReply(const std::string& raw);

bool isValidExpr(const std::string& tag);

}  // namespace stkchan
