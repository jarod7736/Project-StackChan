#include "face/LvglDisplay.h"

#include <M5CoreS3.h>
#include <lvgl.h>

namespace stkchan {

LvglDisplay lvglDisplay;

// ── Display buffers ──────────────────────────────────────────────────────
// CoreS3 LCD is 320×240 RGB565 (2 B/pixel). We use a partial-buffer setup
// so LVGL renders 1/10 of the screen at a time and we flush each chunk —
// keeps the buffer small (~15 KB) and rendering smooth.
static constexpr uint32_t kHor = 320;
static constexpr uint32_t kVer = 240;
static constexpr uint32_t kBufRows = 30;
static constexpr uint32_t kBufPx = kHor * kBufRows;

static lv_color_t  g_buf1[kBufPx];   // ~19 KB on DRAM — fine for partial flush
static lv_display_t* g_disp = nullptr;

// ── Flush callback ───────────────────────────────────────────────────────
// LVGL hands us a dirty area + a pixel buffer. We push it to M5.Display
// and call lv_display_flush_ready() so LVGL can continue.
static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const uint32_t w = lv_area_get_width(area);
    const uint32_t h = lv_area_get_height(area);
    M5.Display.pushImage(
        area->x1, area->y1,
        (int32_t)w, (int32_t)h,
        reinterpret_cast<uint16_t*>(px_map));
    lv_display_flush_ready(disp);
}

bool LvglDisplay::begin() {
    if (ok_) return true;

    lv_init();

    g_disp = lv_display_create(kHor, kVer);
    if (!g_disp) {
        Serial.println("[LvglDisplay] lv_display_create failed");
        return false;
    }
    lv_display_set_flush_cb(g_disp, flushCb);
    lv_display_set_buffers(
        g_disp,
        g_buf1, nullptr,
        sizeof(g_buf1),
        LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Paint a quick "alive" test rectangle so we can confirm flush works
    // before any face widgets are wired up. Removed in step 2.
    lv_obj_t* root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_make(0x00, 0x18, 0x28), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    lv_obj_t* test = lv_obj_create(root);
    lv_obj_set_size(test, 200, 80);
    lv_obj_center(test);
    lv_obj_set_style_bg_color(test, lv_color_make(0xff, 0x55, 0x88), 0);
    lv_obj_set_style_radius(test, 12, 0);

    lv_obj_t* label = lv_label_create(test);
    lv_label_set_text(label, "Stack-chan LVGL OK");
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);

    lastTickMs_ = millis();
    ok_ = true;
    Serial.println("[LvglDisplay] ready (LVGL v9, 320x240 RGB565)");
    return true;
}

void LvglDisplay::tick() {
    if (!ok_) return;
    uint32_t now = millis();
    uint32_t dt = now - lastTickMs_;
    if (dt < 5) return;            // 5 ms granularity is plenty for 33 fps
    lastTickMs_ = now;
    lv_tick_inc(dt);
    lv_timer_handler();
}

}  // namespace stkchan
