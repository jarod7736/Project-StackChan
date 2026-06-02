#pragma once
#include <string>

namespace stkchan {

// Indices match meganetaaan/M5Stack-Avatar Expression enum order.
// Verified against .pio/libdeps/cores3/M5Stack-Avatar/src/Expression.h:
//   enum class Expression { Happy, Angry, Sad, Doubt, Sleepy, Neutral };
// NOTE: The plan listed Neutral=0 first, but the actual Avatar lib starts
// with Happy=0. AvatarExprIdx uses the library's actual order — these
// values are passed to Avatar::setExpression() and MUST match the lib.
enum AvatarExprIdx {
  Happy   = 0,
  Angry   = 1,
  Sad     = 2,
  Doubt   = 3,
  Sleepy  = 4,
  Neutral = 5,
};

struct ExprMapping {
  int   idx;        // Avatar Expression index
  int   yawDeg;     // default yaw offset
  int   pitchDeg;   // default pitch offset
  float bobAmp;     // speech-bob amplitude scale (1.0 = baseline)
};

ExprMapping expressionFor(const std::string& tag);

}  // namespace stkchan
