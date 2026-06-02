#include "face/Face.h"
#include <Avatar.h>
#include "face/ExpressionMap.h"

namespace stkchan {

Face face;
static m5avatar::Avatar g_avatar;

void Face::begin() {
  g_avatar.init();
  g_avatar.setIsAutoBlink(true);
  setExpression("neutral");
}

void Face::setExpression(const std::string& tag) {
  auto m = expressionFor(tag);
  currentTag_ = tag;
  g_avatar.setExpression(static_cast<m5avatar::Expression>(m.idx));
}

void Face::setMouthOpen(float ratio) {
  if (ratio < 0) ratio = 0;
  if (ratio > 1) ratio = 1;
  g_avatar.setMouthOpenRatio(ratio);
}

void Face::setAutoBlinkEnabled(bool enabled) {
  g_avatar.setIsAutoBlink(enabled);
}

}  // namespace stkchan
