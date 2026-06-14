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

  // Discreet status readout in the top-left corner: WiFi link + battery.
  // batteryPct < 0 means "unknown" (icon only). Called throttled from loop().
  void setStatus(int batteryPct, bool charging, bool wifiConnected);

  // Debug readout in the top-right corner: live servo pitch/yaw (the firmware's
  // current commanded values). Called throttled from loop().
  void setServoDebug(int yaw, int pitch);
  std::string currentExpression() const { return currentTag_; }

  // Reveal the floating "Menu" button at the bottom of the face screen
  // for ~3 s. Tapping it navigates to MenuScreen. Called from main.cpp
  // when a swipe-up gesture is detected.
  void revealMenuButton();

  // True while the floating Menu button is visible. main.cpp uses this
  // to suppress press-to-talk during the reveal window — otherwise
  // tapping the button would also start the mic.
  bool isMenuButtonVisible() const;

  // Called once per loop() to handle auto-hide of the menu button.
  void tick(uint32_t nowMs);

 private:
  std::string currentTag_ = "neutral";
};

extern Face face;

}  // namespace stkchan
