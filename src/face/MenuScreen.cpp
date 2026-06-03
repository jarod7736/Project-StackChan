#include "face/MenuScreen.h"
#include "face/LvglDisplay.h"
#include "hal/AudioPlayer.h"

#include <lvgl.h>

namespace stkchan {

MenuScreen menu;

namespace {

lv_obj_t* g_screen        = nullptr;
lv_obj_t* g_face_screen   = nullptr;  // captured at begin() so back-nav works
lv_obj_t* g_volume_slider = nullptr;
lv_obj_t* g_volume_label  = nullptr;

constexpr int kBgColor   = 0x101820;
constexpr int kFgColor   = 0xFFFFFF;
constexpr int kAccentCol = 0xFF6688;

void updateVolumeLabel(int pct) {
    if (!g_volume_label) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "Volume  %d%%", pct);
    lv_label_set_text(g_volume_label, buf);
}

void volumeSliderCb(lv_event_t* e) {
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int v = lv_slider_get_value(slider);
    audio.setVolume(v);            // applies immediately + persists to NVS
    updateVolumeLabel(v);
}

void backBtnCb(lv_event_t* /*e*/) {
    menu.hide();
}

void buildScreen() {
    g_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa  (g_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

    // ── Title ────────────────────────────────────────────────────────────
    lv_obj_t* title = lv_label_create(g_screen);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(kFgColor), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    // ── Volume row ──────────────────────────────────────────────────────
    g_volume_label = lv_label_create(g_screen);
    lv_obj_set_style_text_color(g_volume_label, lv_color_hex(kFgColor), 0);
    lv_obj_align(g_volume_label, LV_ALIGN_TOP_MID, 0, 60);

    g_volume_slider = lv_slider_create(g_screen);
    lv_obj_set_size(g_volume_slider, 240, 20);
    lv_obj_align(g_volume_slider, LV_ALIGN_TOP_MID, 0, 100);
    lv_slider_set_range(g_volume_slider, 0, 100);
    lv_slider_set_value(g_volume_slider, audio.getVolume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_volume_slider, lv_color_hex(0x334050), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_volume_slider, lv_color_hex(kAccentCol), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_volume_slider, lv_color_hex(kFgColor), LV_PART_KNOB);
    lv_obj_add_event_cb(g_volume_slider, volumeSliderCb, LV_EVENT_VALUE_CHANGED, nullptr);

    updateVolumeLabel(audio.getVolume());

    // ── Back button ─────────────────────────────────────────────────────
    lv_obj_t* back = lv_btn_create(g_screen);
    lv_obj_set_size(back, 120, 44);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_bg_color(back, lv_color_hex(kAccentCol), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, backBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* back_label = lv_label_create(back);
    lv_label_set_text(back_label, "Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(kFgColor), 0);
    lv_obj_center(back_label);
}

}  // namespace

bool MenuScreen::begin() {
    if (g_screen) return true;
    if (!lvglDisplay.ok()) return false;
    g_face_screen = lv_screen_active();  // remember to navigate back later
    buildScreen();
    Serial.println("[MenuScreen] ready");
    return true;
}

void MenuScreen::show() {
    if (!g_screen) return;
    // Refresh slider to current volume (in case AudioPlayer was reconfigured).
    if (g_volume_slider) {
        lv_slider_set_value(g_volume_slider, audio.getVolume(), LV_ANIM_OFF);
        updateVolumeLabel(audio.getVolume());
    }
    lv_screen_load_anim(g_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 220, 0, false);
    active_ = true;
}

void MenuScreen::hide() {
    if (!g_screen || !g_face_screen) return;
    lv_screen_load_anim(g_face_screen, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 220, 0, false);
    active_ = false;
}

}  // namespace stkchan
