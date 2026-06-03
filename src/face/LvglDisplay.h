#pragma once

// LvglDisplay — minimal LVGL ↔ M5GFX bridge.
//
// LVGL renders into a small in-memory buffer. We register a flush callback
// that hands each dirty rectangle to M5.Display.pushImage(), which pushes
// it out over QSPI to the panel. Everything runs on the main task — no
// background draw loop, no SPI mutex race.
//
// Usage:
//   stkchan::lvglDisplay.begin();
//   ...
//   stkchan::lvglDisplay.tick();   // call every loop()

#include <Arduino.h>

namespace stkchan {

class LvglDisplay {
public:
    bool begin();
    void tick();      // drives lv_timer_handler()
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
    uint32_t lastTickMs_ = 0;
};

extern LvglDisplay lvglDisplay;

}  // namespace stkchan
