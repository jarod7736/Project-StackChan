#pragma once
#include <Arduino.h>
#include <string>

namespace stkchan {

class Face {
 public:
  void begin();                            // calls Avatar::init()
  void setExpression(const std::string& tag);
  void setMouthOpen(float ratio);          // 0.0 .. 1.0
  void setAutoBlinkEnabled(bool enabled);  // control auto-blink behavior
  std::string currentExpression() const { return currentTag_; }

 private:
  std::string currentTag_ = "neutral";
};

extern Face face;

}  // namespace stkchan
