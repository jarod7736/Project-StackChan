#include "persona/ResponseParser.h"
#include <algorithm>
#include <cctype>

namespace stkchan {

static const std::string kValidExprs[] = {
  "neutral", "happy", "sad", "angry", "doubt", "sleepy",
};

bool isValidExpr(const std::string& tag) {
  for (const auto& v : kValidExprs) if (v == tag) return true;
  return false;
}

static std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

static std::string stripFences(std::string s) {
  // Drop any leading ```lang fence and trailing ``` fence.
  auto npos = std::string::npos;
  auto open = s.find("```");
  if (open != npos) {
    auto eol = s.find('\n', open);
    if (eol != npos) s.erase(open, eol - open + 1);
  }
  auto close = s.rfind("```");
  if (close != npos) s.erase(close, 3);
  return s;
}

static std::string trim(std::string s) {
  auto isSpace = [](unsigned char c) { return std::isspace(c); };
  s.erase(s.begin(),
          std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !isSpace(c); }));
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [&](unsigned char c){ return !isSpace(c); }).base(),
          s.end());
  return s;
}

static std::string collapseWhitespace(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool prevSpace = false;
  for (char c : s) {
    bool sp = std::isspace(static_cast<unsigned char>(c));
    if (sp) {
      if (!prevSpace && !out.empty()) out.push_back(' ');
      prevSpace = true;
    } else {
      out.push_back(c);
      prevSpace = false;
    }
  }
  return trim(out);
}

static bool extractTag(const std::string& s,
                       const std::string& tag,
                       std::string& out) {
  std::string open  = "<"  + tag + ">";
  std::string close = "</" + tag + ">";
  auto a = s.find(open);
  if (a == std::string::npos) return false;
  a += open.size();
  auto b = s.find(close, a);
  if (b == std::string::npos) return false;
  out = s.substr(a, b - a);
  return true;
}

ParsedReply parseReply(const std::string& raw) {
  ParsedReply r;
  std::string s = stripFences(trim(raw));

  std::string speechRaw, exprRaw;
  bool hasSpeech = extractTag(s, "speech", speechRaw);
  bool hasExpr   = extractTag(s, "expr",   exprRaw);

  if (hasSpeech) {
    r.speech = collapseWhitespace(speechRaw);
  } else {
    r.speech = collapseWhitespace(s);
  }
  if (hasExpr) {
    std::string lowered = toLower(trim(exprRaw));
    r.expr = isValidExpr(lowered) ? lowered : "neutral";
  } else {
    r.expr = "neutral";
  }
  r.ok = hasSpeech && hasExpr && isValidExpr(toLower(trim(exprRaw)));
  return r;
}

}  // namespace stkchan
