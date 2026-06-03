#include "face/Face.h"
#include "face/ExpressionMap.h"
#include "face/LvglDisplay.h"
#include "face/MenuScreen.h"

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
lv_obj_t* g_mouth    = nullptr;   // lv_line
lv_obj_t* g_menu_btn = nullptr;   // hidden until swipe-up reveals
uint32_t  g_menu_btn_show_ms = 0; // millis() at reveal; 0 = not visible
constexpr uint32_t kMenuBtnAutoHideMs = 5000;

lv_anim_t g_blink_anim_l;
lv_anim_t g_blink_anim_r;
lv_timer_t* g_blink_timer = nullptr;
bool g_auto_blink = true;

constexpr int kBgColor   = 0x101820;   // near-black teal
constexpr int kEyeWhite  = 0xFFFFFF;
constexpr int kPupil     = 0x101820;   // matches bg so the pupil "is" a hole
constexpr int kMouthWhite= 0xFFFFFF;
constexpr int kMouthRed  = 0xFF5566;

// Layout for a 320x240 screen.
constexpr int kEyeY    = 105;
constexpr int kEyeW    = 38;
constexpr int kEyeH    = 32;   // smaller + squatter for a chiller resting look
constexpr int kEyeLX   = 100;
constexpr int kEyeRX   = 182;
constexpr int kEyeBlinkH = 3;  // height during blink — "thin white line"
constexpr int kPupilW  = 30;
constexpr int kPupilH  = 26;   // nearly fills eye, leaves thin white rim

// Mouth is an lv_line with 7 control points spanning kMouthW px,
// centered horizontally at kMouthCenterX, baseline at kMouthY.
// Each expression provides per-point Y offsets, then setMouthOpen() adds
// a bow in the middle for lip-sync.
constexpr int kMouthCenterX = 160;
constexpr int kMouthY       = 175;
constexpr int kMouthW       = 80;
constexpr int kMouthPoints  = 7;

// Per-expression Y offsets for the 7 mouth points (left → right).
// Positive Y is downward; corners at the ends, middle at index 3.
struct MouthCurve { const char* tag; int dy[kMouthPoints]; uint32_t color; };
constexpr MouthCurve kCurves[] = {
    // tag      dy[0]  dy[1] dy[2] dy[3] dy[4] dy[5] dy[6]   color
    {"neutral", {  0,    0,    0,    0,    0,    0,    0  }, kMouthWhite},
    {"happy",   { -8,   -4,    3,    8,    3,   -4,   -8  }, kMouthWhite}, // ∪ smile
    {"sad",     {  8,    4,   -3,   -8,   -3,    4,    8  }, kMouthWhite}, // ∩ frown
    {"angry",   {  0,    6,    0,    8,    0,    6,    0  }, kMouthRed  }, // jagged red
    {"doubt",   { -6,   -3,    0,    3,    6,    9,   12  }, kMouthWhite}, // tilted up-left
    {"sleepy",  {  4,    4,    4,    4,    4,    4,    4  }, kMouthWhite}, // flat low
};

// State, recomputed in rebuildMouth() from the active expression + open ratio.
int   g_mouth_expr_dy[kMouthPoints] = {0,0,0,0,0,0,0};
float g_mouth_open_r = 0.0f;
lv_point_precise_t g_mouth_pts[kMouthPoints];

void rebuildMouth() {
    if (!g_mouth) return;
    const int xStart = kMouthCenterX - kMouthW / 2;
    const int step   = kMouthW / (kMouthPoints - 1);
    // Per-point amplitude for the open-mouth bow — peaks at the middle,
    // tapers to 0 at the corners. Multiplied by g_mouth_open_r ∈ [0,1].
    static constexpr int kBow[kMouthPoints] = { 0, 6, 14, 22, 14, 6, 0 };
    for (int i = 0; i < kMouthPoints; ++i) {
        g_mouth_pts[i].x = xStart + i * step;
        g_mouth_pts[i].y = kMouthY + g_mouth_expr_dy[i]
                                   + (int)(kBow[i] * g_mouth_open_r);
    }
    lv_line_set_points(g_mouth, g_mouth_pts, kMouthPoints);
}

// ── Blink animation ─────────────────────────────────────────────────────
// Shrink the eye's height from kEyeH → kEyeBlinkH and back. Y is adjusted
// so the eye contracts toward its center. Pupil is hidden during the
// shrink so the result is a clean "thin white line on black" — the
// pupil's clip rectangle is otherwise still visible inside the line.
void blinkSetH(void* obj, int32_t h) {
    lv_obj_t* eye = static_cast<lv_obj_t*>(obj);
    lv_obj_set_height(eye, h);
    lv_obj_set_y(eye, kEyeY + (kEyeH - (int)h) / 2);
    // Hide pupil while the eye is sufficiently closed.
    bool isLeft  = (eye == g_eye_l);
    lv_obj_t* pupil = isLeft ? g_pupil_l : g_pupil_r;
    if (pupil) {
        if (h < kEyeH * 2 / 3) lv_obj_add_flag(pupil, LV_OBJ_FLAG_HIDDEN);
        else                    lv_obj_clear_flag(pupil, LV_OBJ_FLAG_HIDDEN);
    }
}

void runBlinkOnce() {
    if (!g_eye_l || !g_eye_r) return;
    // Left eye
    lv_anim_init(&g_blink_anim_l);
    lv_anim_set_var(&g_blink_anim_l, g_eye_l);
    lv_anim_set_values(&g_blink_anim_l, kEyeH, kEyeBlinkH);
    lv_anim_set_duration(&g_blink_anim_l, 110);
    lv_anim_set_playback_duration(&g_blink_anim_l, 110);
    lv_anim_set_path_cb(&g_blink_anim_l, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&g_blink_anim_l, blinkSetH);
    lv_anim_start(&g_blink_anim_l);
    // Right eye (clone)
    lv_anim_init(&g_blink_anim_r);
    lv_anim_set_var(&g_blink_anim_r, g_eye_r);
    lv_anim_set_values(&g_blink_anim_r, kEyeH, kEyeBlinkH);
    lv_anim_set_duration(&g_blink_anim_r, 110);
    lv_anim_set_playback_duration(&g_blink_anim_r, 110);
    lv_anim_set_path_cb(&g_blink_anim_r, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&g_blink_anim_r, blinkSetH);
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

    // ── Mouth (lv_line, 7 control points) ──────────────────────────────
    g_mouth = lv_line_create(root);
    lv_obj_set_style_line_width(g_mouth, 6, 0);
    lv_obj_set_style_line_color(g_mouth, lv_color_hex(kMouthWhite), 0);
    lv_obj_set_style_line_rounded(g_mouth, true, 0);
    lv_obj_set_style_line_opa(g_mouth, LV_OPA_COVER, 0);
    // Build the initial neutral curve.
    rebuildMouth();

    // Eyelids are no longer needed — blink is implemented by animating
    // the eye's own height (see runBlinkOnce / blinkSetH above).

    // ── Menu button (hidden until swipe-up) ─────────────────────────────
    // Make it visually loud so it can't be missed when it appears.
    g_menu_btn = lv_btn_create(root);
    lv_obj_set_size(g_menu_btn, 200, 60);
    lv_obj_align(g_menu_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(g_menu_btn, lv_color_hex(0xFF6688), 0);
    lv_obj_set_style_bg_opa(g_menu_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_menu_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(g_menu_btn, 3, 0);
    lv_obj_set_style_radius(g_menu_btn, 12, 0);
    lv_obj_add_flag(g_menu_btn, LV_OBJ_FLAG_HIDDEN);
    // Bring to front in case anything else gets layered on top.
    lv_obj_move_foreground(g_menu_btn);
    lv_obj_add_event_cb(g_menu_btn,
        [](lv_event_t*) {
            Serial.println("[Face] Menu button tapped");
            menu.show();
        },
        LV_EVENT_CLICKED, nullptr);

    lv_obj_t* mlabel = lv_label_create(g_menu_btn);
    lv_label_set_text(mlabel, "MENU");
    lv_obj_set_style_text_color(mlabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(mlabel, &lv_font_montserrat_14, 0);
    lv_obj_center(mlabel);

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
    if (!g_mouth) return;

    // Look up the curve table, fall back to neutral.
    const MouthCurve* found = &kCurves[0];
    for (const auto& c : kCurves) {
        if (tag == c.tag) { found = &c; break; }
    }
    for (int i = 0; i < kMouthPoints; ++i) g_mouth_expr_dy[i] = found->dy[i];
    lv_obj_set_style_line_color(g_mouth, lv_color_hex(found->color), 0);
    rebuildMouth();
}

void Face::setMouthOpen(float ratio) {
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    g_mouth_open_r = ratio;
    rebuildMouth();
}

void Face::setAutoBlinkEnabled(bool enabled) {
    g_auto_blink = enabled;
}

bool Face::isMenuButtonVisible() const {
    return g_menu_btn != nullptr &&
           !lv_obj_has_flag(g_menu_btn, LV_OBJ_FLAG_HIDDEN);
}

void Face::revealMenuButton() {
    if (!g_menu_btn) return;
    Serial.println("[Face] Menu button revealed");
    lv_obj_clear_flag(g_menu_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_menu_btn);
    g_menu_btn_show_ms = millis();
}

void Face::tick(uint32_t nowMs) {
    if (g_menu_btn_show_ms != 0 &&
        nowMs - g_menu_btn_show_ms >= kMenuBtnAutoHideMs) {
        if (g_menu_btn) lv_obj_add_flag(g_menu_btn, LV_OBJ_FLAG_HIDDEN);
        g_menu_btn_show_ms = 0;
        Serial.println("[Face] Menu button auto-hidden");
    }
}

}  // namespace stkchan
