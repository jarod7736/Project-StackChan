#pragma once

// MenuScreen — settings UI revealed from the face screen.
//
// A second LVGL screen separate from the face. Currently exposes:
//   - Speaker volume slider (0-100%, persists to NVS)
//   - Back button (returns to face)
//
// Easy to extend: add more rows under volumeRow_ in begin().

#include <Arduino.h>

namespace stkchan {

class MenuScreen {
 public:
    bool begin();      // build the screen objects (idempotent)
    void show();       // slide-in to the menu screen
    void hide();       // slide back to the face screen
    bool isActive() const { return active_; }

 private:
    bool active_ = false;
};

extern MenuScreen menu;

}  // namespace stkchan
