#include "face/Face.h"
#include "face/ExpressionMap.h"
#include "face/LvglDisplay.h"

#include <lvgl.h>

// v1.5: LVGL-based face. Single-threaded — every update happens on whichever
// task calls lv_timer_handler (we call it from main loop()), so no SPI mutex
// race against the panel driver the way m5avatar's drawLoop did.

namespace stkchan {

Face face;

namespace {

// Stage objects — created in begin(), mutated in setExpression / setMouthOpen.
lv_obj_t* g_bg       = nullptr;
lv_obj_t* g_eye_l    = nullptr;
lv_obj_t* g_eye_r    = nullptr;
lv_obj_t* g_pupil_l  = nullptr;
lv_obj_t* g_pupil_r  = nullptr;
lv_obj_t* g_mouth    = nullptr;
lv_obj_t* g_lid_l    = nullptr;   // animated downward to close eye
lv_obj_t* g_lid_r    = nullptr;

lv_anim_t g_blink_anim_l;
lv_anim_t g_blink_anim_r;
lv_timer_t* g_blink_timer = nullptr;
bool g_auto_blink = true;

constexpr int kBgColor   = 0x101820;   // near-black teal
constexpr int kEyeWhite  = 0xFFFFFF;
constexpr int kPupil     = 0x101820;   // matches bg so the pupil "is" a hole
constexpr int kMouth     = 0xFFFFFF;

// Layout for a 320x240 screen.
constexpr int kEyeY    = 70;
constexpr int kEyeW    = 80;
constexpr int kEyeH    = 80;
constexpr int kEyeLX   = 60;
constexpr int kEyeRX   = 180;
constexpr int kPupilW  = 28;
constexpr int kPupilH  = 28;
constexpr int kMouthY  = 175;
constexpr int kMouthW  = 60;
constexpr int kMouthHClosed = 6;
constexpr int kMouthHMax    = 38;

// ── Blink animation ─────────────────────────────────────────────────────
// Drives lid Y-position from -kEyeH (fully up, invisible) → 0 (fully covering
// the eye). The animation is a there-and-back via lv_anim_set_playback_*.
void blinkSetY(void* obj, int32_t y) {
    lv_obj_set_y(static_cast<lv_obj_t*>(obj), y);
}

void runBlinkOnce() {
    if (!g_lid_l || !g_lid_r) return;
    // Left lid
    lv_anim_init(&g_blink_anim_l);
    lv_anim_set_var(&g_blink_anim_l, g_lid_l);
    lv_anim_set_values(&g_blink_anim_l, kEyeY - kEyeH, kEyeY);
    lv_anim_set_duration(&g_blink_anim_l, 120);
    lv_anim_set_playback_duration(&g_blink_anim_l, 120);
    lv_anim_set_path_cb(&g_blink_anim_l, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&g_blink_anim_l, blinkSetY);
    lv_anim_start(&g_blink_anim_l);
    // Right lid (clone)
    lv_anim_init(&g_blink_anim_r);
    lv_anim_set_var(&g_blink_anim_r, g_lid_r);
    lv_anim_set_values(&g_blink_anim_r, kEyeY - kEyeH, kEyeY);
    lv_anim_set_duration(&g_blink_anim_r, 120);
    lv_anim_set_playback_duration(&g_blink_anim_r, 120);
    lv_anim_set_path_cb(&g_blink_anim_r, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&g_blink_anim_r, blinkSetY);
    lv_anim_start(&g_blink_anim_r);
}

void blinkTimerCb(lv_timer_t* /*t*/) {
    if (!g_auto_blink) return;
    runBlinkOnce();
}

void buildStage() {
    lv_obj_t* root = lv_screen_active();
    lv_obj_clean(root);

    // Background
    g_bg = root;
    lv_obj_set_style_bg_color(g_bg, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(g_bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_bg, LV_OBJ_FLAG_SCROLLABLE);

    // ── Eyes (white circles) ────────────────────────────────────────────
    g_eye_l = lv_obj_create(root);
    lv_obj_set_size(g_eye_l, kEyeW, kEyeH);
    lv_obj_set_pos(g_eye_l, kEyeLX, kEyeY);
    lv_obj_set_style_radius(g_eye_l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_eye_l, lv_color_hex(kEyeWhite), 0);
    lv_obj_set_style_bg_opa(g_eye_l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_eye_l, 0, 0);
    lv_obj_clear_flag(g_eye_l, LV_OBJ_FLAG_SCROLLABLE);

    g_eye_r = lv_obj_create(root);
    lv_obj_set_size(g_eye_r, kEyeW, kEyeH);
    lv_obj_set_pos(g_eye_r, kEyeRX, kEyeY);
    lv_obj_set_style_radius(g_eye_r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_eye_r, lv_color_hex(kEyeWhite), 0);
    lv_obj_set_style_bg_opa(g_eye_r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_eye_r, 0, 0);
    lv_obj_clear_flag(g_eye_r, LV_OBJ_FLAG_SCROLLABLE);

    // ── Pupils ──────────────────────────────────────────────────────────
    g_pupil_l = lv_obj_create(g_eye_l);
    lv_obj_set_size(g_pupil_l, kPupilW, kPupilH);
    lv_obj_center(g_pupil_l);
    lv_obj_set_style_radius(g_pupil_l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_pupil_l, lv_color_hex(kPupil), 0);
    lv_obj_set_style_bg_opa(g_pupil_l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_pupil_l, 0, 0);
    lv_obj_clear_flag(g_pupil_l, LV_OBJ_FLAG_SCROLLABLE);

    g_pupil_r = lv_obj_create(g_eye_r);
    lv_obj_set_size(g_pupil_r, kPupilW, kPupilH);
    lv_obj_center(g_pupil_r);
    lv_obj_set_style_radius(g_pupil_r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_pupil_r, lv_color_hex(kPupil), 0);
    lv_obj_set_style_bg_opa(g_pupil_r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_pupil_r, 0, 0);
    lv_obj_clear_flag(g_pupil_r, LV_OBJ_FLAG_SCROLLABLE);

    // ── Mouth ───────────────────────────────────────────────────────────
    g_mouth = lv_obj_create(root);
    lv_obj_set_size(g_mouth, kMouthW, kMouthHClosed);
    lv_obj_set_pos(g_mouth, (320 - kMouthW) / 2, kMouthY);
    lv_obj_set_style_radius(g_mouth, 4, 0);
    lv_obj_set_style_bg_color(g_mouth, lv_color_hex(kMouth), 0);
    lv_obj_set_style_bg_opa(g_mouth, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_mouth, 0, 0);
    lv_obj_clear_flag(g_mouth, LV_OBJ_FLAG_SCROLLABLE);

    // ── Eyelids (start parked above the eye) ────────────────────────────
    // Eyelids are colored rectangles matching the background — when their
    // y matches the eye's y they appear to "close" the eye.
    g_lid_l = lv_obj_create(root);
    lv_obj_set_size(g_lid_l, kEyeW, kEyeH);
    lv_obj_set_pos(g_lid_l, kEyeLX, kEyeY - kEyeH);   // parked offscreen-above
    lv_obj_set_style_radius(g_lid_l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_lid_l, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(g_lid_l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_lid_l, 0, 0);
    lv_obj_clear_flag(g_lid_l, LV_OBJ_FLAG_SCROLLABLE);

    g_lid_r = lv_obj_create(root);
    lv_obj_set_size(g_lid_r, kEyeW, kEyeH);
    lv_obj_set_pos(g_lid_r, kEyeRX, kEyeY - kEyeH);
    lv_obj_set_style_radius(g_lid_r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_lid_r, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(g_lid_r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_lid_r, 0, 0);
    lv_obj_clear_flag(g_lid_r, LV_OBJ_FLAG_SCROLLABLE);

    // ── Auto-blink timer ────────────────────────────────────────────────
    // Fire every 5 seconds. runBlinkOnce() plays through the up-then-down
    // animation; LVGL handles the rest.
    g_blink_timer = lv_timer_create(blinkTimerCb, 5000, nullptr);
}

}  // namespace

void Face::begin() {
    currentTag_ = "neutral";
    if (!lvglDisplay.begin()) {
        Serial.println("[Face] LVGL bring-up failed; face is no-op");
        return;
    }
    buildStage();
    Serial.println("[Face] ready (LVGL widgets)");
}

void Face::setExpression(const std::string& tag) {
    currentTag_ = tag;
    if (!g_mouth || !g_eye_l || !g_eye_r) return;
    auto m = expressionFor(tag);
    (void)m;

    // Quick-and-dirty expression mapping: change the mouth shape per tag.
    // Eyes stay round for now; future work can squish them per emotion.
    int mouthW = kMouthW;
    int mouthH = kMouthHClosed;
    uint32_t mouthColor = kEyeWhite;
    if      (tag == "happy")  { mouthW = 80; mouthH = 18; }
    else if (tag == "sad")    { mouthW = 50; mouthH = 4;  }
    else if (tag == "angry")  { mouthW = 70; mouthH = 8;  mouthColor = 0xff5566; }
    else if (tag == "doubt")  { mouthW = 40; mouthH = 6;  }
    else if (tag == "sleepy") { mouthW = 30; mouthH = 4;  }
    lv_obj_set_size(g_mouth, mouthW, mouthH);
    lv_obj_set_pos(g_mouth, (320 - mouthW) / 2, kMouthY);
    lv_obj_set_style_bg_color(g_mouth, lv_color_hex(mouthColor), 0);
}

void Face::setMouthOpen(float ratio) {
    if (!g_mouth) return;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    int h = kMouthHClosed + int((kMouthHMax - kMouthHClosed) * ratio);
    // Keep the mouth centered vertically as it grows.
    int yShift = (h - kMouthHClosed) / 2;
    lv_obj_set_size(g_mouth, lv_obj_get_width(g_mouth), h);
    lv_obj_set_y(g_mouth, kMouthY - yShift);
}

void Face::setAutoBlinkEnabled(bool enabled) {
    g_auto_blink = enabled;
}

}  // namespace stkchan
