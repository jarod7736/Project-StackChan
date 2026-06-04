#include "face/ExpressionMap.h"

namespace stkchan {

ExprMapping expressionFor(const std::string& tag) {
  if (tag == "happy")  return { AvatarExprIdx::Happy,    0,  5, 1.4f };
  if (tag == "sad")    return { AvatarExprIdx::Sad,      0, -10, 0.6f };
  if (tag == "angry")  return { AvatarExprIdx::Angry,    0,  0, 1.2f };
  if (tag == "doubt")  return { AvatarExprIdx::Doubt,  -15,  0, 1.0f };
  if (tag == "sleepy") return { AvatarExprIdx::Sleepy,   0, -5, 0.7f };
  if (tag == "kiss")   return { AvatarExprIdx::Happy,    0,  4, 0.8f };
  // default / unknown
  return { AvatarExprIdx::Neutral, 0, 0, 1.0f };
}

}  // namespace stkchan
