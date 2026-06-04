#include "face/Face.h"
#include "face/ExpressionMap.h"
#include "face/LvglDisplay.h"
#include "face/MenuScreen.h"

#include <lvgl.h>

// LVGL-based face. Single-threaded — every update happens on whichever task
// calls lv_timer_handler (we call it from main loop()), so no SPI mutex race.
//
// Expression model: each tag drives the mouth curve/color, a resting eye
// height (half-lidded sleepy, narrowed angry), eyebrow tilt, and an optional
// "kiss" pucker that replaces the line mouth.

namespace stkchan {

Face face;

namespace {

lv_obj_t* g_bg       = nullptr;
lv_obj_t* g_eye_l    = nullptr;
lv_obj_t* g_eye_r    = nullptr;
lv_obj_t* g_pupil_l  = nullptr;
lv_obj_t* g_pupil_r  = nullptr;
lv_obj_t* g_brow_l   = nullptr;   // lv_line, 2 pts
lv_obj_t* g_brow_r   = nullptr;
lv_obj_t* g_mouth    = nullptr;   // lv_line, 7 pts
lv_obj_t* g_kiss     = nullptr;   // small filled pucker (shown for "kiss")
lv_obj_t* g_menu_btn = nullptr;
uint32_t  g_menu_btn_show_ms = 0;
constexpr uint32_t kMenuBtnAutoHideMs = 5000;

lv_anim_t g_blink_anim_l;
lv_anim_t g_blink_anim_r;
lv_timer_t* g_blink_timer = nullptr;
bool g_auto_blink = true;

constexpr int kBgColor   = 0x101820;
constexpr int kEyeWhite  = 0xFFFFFF;
constexpr int kPupil     = 0x101820;
constexpr int kMouthWhite= 0xFFFFFF;
constexpr int kMouthRed  = 0xFF5566;
constexpr int kBrowColor = 0xFFFFFF;

// Layout (320x240).
constexpr int kEyeY      = 105;
constexpr int kEyeW      = 38;
constexpr int kEyeH      = 32;          // full (resting) eye height
constexpr int kEyeCenterY= kEyeY + kEyeH / 2;  // eyes stay centered on this line
constexpr int kEyeLX     = 100;
constexpr int kEyeRX     = 182;
constexpr int kEyeBlinkH = 3;
constexpr int kPupilW    = 30;
constexpr int kPupilH    = 26;
constexpr int kBrowBaseY = kEyeY - 16;  // eyebrow baseline (above the eye)

constexpr int kMouthCenterX = 160;
constexpr int kMouthY       = 175;
constexpr int kMouthW       = 80;
constexpr int kMouthPoints  = 7;

// Per-expression style descriptor.
struct ExprStyle {
    const char* tag;
    int      dy[kMouthPoints];  // mouth curve (line points), ignored if kiss
    uint32_t mouthColor;
    int      eyeH;              // resting eye height (lidding/narrowing)
    bool     browShow;
    int      browTiltL;         // inner-end Y offset, + = down (angry), - = up (sad)
    int      browTiltR;
    bool     kiss;              // true → hide line mouth, show pucker
};

constexpr ExprStyle kStyles[] = {
    // tag       dy[0..6]                          color        eyeH brow  tiltL tiltR kiss
    {"neutral", {  0,  0,  0,  0,  0,  0,  0 },    kMouthWhite, kEyeH, false,  0,   0, false},
    {"happy",   { -8, -4,  3,  8,  3, -4, -8 },    kMouthWhite, kEyeH, false,  0,   0, false},
    {"sad",     {  8,  4, -3, -8, -3,  4,  8 },    kMouthWhite, kEyeH, true,  -8,  -8, false},
    {"angry",   {  0,  6,  0,  8,  0,  6,  0 },    kMouthRed,   18,    true,  11,  11, false},
    {"doubt",   { -6, -3,  0,  3,  6,  9, 12 },    kMouthWhite, kEyeH, true, -10,   3, false},
    {"sleepy",  {  4,  4,  4,  4,  4,  4,  4 },    kMouthWhite, 14,    false,  0,   0, false},
    {"kiss",    {  0,  0,  0,  0,  0,  0,  0 },    kMouthWhite, 24,    false,  0,   0, true },
};

int   g_mouth_expr_dy[kMouthPoints] = {0,0,0,0,0,0,0};
float g_mouth_open_r = 0.0f;
int   g_rest_eye_h   = kEyeH;
lv_point_precise_t g_mouth_pts[kMouthPoints];
lv_point_precise_t g_brow_l_pts[2];
lv_point_precise_t g_brow_r_pts[2];

void rebuildMouth() {
    if (!g_mouth) return;
    const int xStart = kMouthCenterX - kMouthW / 2;
    const int step   = kMouthW / (kMouthPoints - 1);
    static constexpr int kBow[kMouthPoints] = { 0, 6, 14, 22, 14, 6, 0 };
    for (int i = 0; i < kMouthPoints; ++i) {
        g_mouth_pts[i].x = xStart + i * step;
        g_mouth_pts[i].y = kMouthY + g_mouth_expr_dy[i]
                                   + (int)(kBow[i] * g_mouth_open_r);
    }
    lv_line_set_points(g_mouth, g_mouth_pts, kMouthPoints);
}

// Resize+recenter one eye (and its pupil) to a given height, centered on the
// fixed eye centre line so lidding contracts toward the middle.
void applyEyeHeight(lv_obj_t* eye, lv_obj_t* pupil, int h) {
    if (!eye) return;
    lv_obj_set_height(eye, h);
    lv_obj_set_y(eye, kEyeCenterY - h / 2);
    if (pupil) {
        int ph = kPupilH;
        if (ph > h - 4) ph = h - 4;
        if (ph < 4) ph = 4;
        lv_obj_set_size(pupil, kPupilW, ph);
        lv_obj_center(pupil);
    }
}

void setBrows(const ExprStyle& s) {
    if (!g_brow_l || !g_brow_r) return;
    if (!s.browShow) {
        lv_obj_add_flag(g_brow_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_brow_r, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // Left brow: outer end at the left edge, inner end (toward nose) tilted.
    g_brow_l_pts[0] = { (lv_value_precise_t)kEyeLX,          (lv_value_precise_t)kBrowBaseY };
    g_brow_l_pts[1] = { (lv_value_precise_t)(kEyeLX+kEyeW),  (lv_value_precise_t)(kBrowBaseY + s.browTiltL) };
    // Right brow: inner end (toward nose) tilted, outer end at the right edge.
    g_brow_r_pts[0] = { (lv_value_precise_t)kEyeRX,          (lv_value_precise_t)(kBrowBaseY + s.browTiltR) };
    g_brow_r_pts[1] = { (lv_value_precise_t)(kEyeRX+kEyeW),  (lv_value_precise_t)kBrowBaseY };
    lv_line_set_points(g_brow_l, g_brow_l_pts, 2);
    lv_line_set_points(g_brow_r, g_brow_r_pts, 2);
    lv_obj_clear_flag(g_brow_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_brow_r, LV_OBJ_FLAG_HIDDEN);
}

// ── Blink ────────────────────────────────────────────────────────────────
// Animate eye height between the current resting height and a thin line,
// keeping the eye centred. Pupil hides while the eye is mostly closed.
void blinkSetH(void* obj, int32_t h) {
    lv_obj_t* eye = static_cast<lv_obj_t*>(obj);
    lv_obj_set_height(eye, h);
    lv_obj_set_y(eye, kEyeCenterY - (int)h / 2);
    lv_obj_t* pupil = (eye == g_eye_l) ? g_pupil_l : g_pupil_r;
    if (pupil) {
        if (h < 12) lv_obj_add_flag(pupil, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_clear_flag(pupil, LV_OBJ_FLAG_HIDDEN);
    }
}

void startBlinkAnim(lv_anim_t* a, lv_obj_t* eye) {
    lv_anim_init(a);
    lv_anim_set_var(a, eye);
    lv_anim_set_values(a, g_rest_eye_h, kEyeBlinkH);
    lv_anim_set_duration(a, 110);
    lv_anim_set_playback_duration(a, 110);
    lv_anim_set_path_cb(a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(a, blinkSetH);
    lv_anim_start(a);
}

void runBlinkOnce() {
    if (!g_eye_l || !g_eye_r) return;
    startBlinkAnim(&g_blink_anim_l, g_eye_l);
    startBlinkAnim(&g_blink_anim_r, g_eye_r);
}

void blinkTimerCb(lv_timer_t* /*t*/) {
    if (g_auto_blink) runBlinkOnce();
}

lv_obj_t* makeEye(lv_obj_t* root, int x) {
    lv_obj_t* e = lv_obj_create(root);
    lv_obj_set_size(e, kEyeW, kEyeH);
    lv_obj_set_pos(e, x, kEyeY);
    lv_obj_set_style_radius(e, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(e, lv_color_hex(kEyeWhite), 0);
    lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(e, 0, 0);
    lv_obj_clear_flag(e, LV_OBJ_FLAG_SCROLLABLE);
    return e;
}

lv_obj_t* makePupil(lv_obj_t* eye) {
    lv_obj_t* p = lv_obj_create(eye);
    lv_obj_set_size(p, kPupilW, kPupilH);
    lv_obj_center(p);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(kPupil), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t* makeBrow(lv_obj_t* root) {
    lv_obj_t* b = lv_line_create(root);
    lv_obj_set_style_line_width(b, 5, 0);
    lv_obj_set_style_line_color(b, lv_color_hex(kBrowColor), 0);
    lv_obj_set_style_line_rounded(b, true, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
}

void buildStage() {
    lv_obj_t* root = lv_screen_active();
    lv_obj_clean(root);

    g_bg = root;
    lv_obj_set_style_bg_color(g_bg, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(g_bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_bg, LV_OBJ_FLAG_SCROLLABLE);

    g_eye_l = makeEye(root, kEyeLX);
    g_eye_r = makeEye(root, kEyeRX);
    g_pupil_l = makePupil(g_eye_l);
    g_pupil_r = makePupil(g_eye_r);

    g_brow_l = makeBrow(root);
    g_brow_r = makeBrow(root);

    // Line mouth (7 control points).
    g_mouth = lv_line_create(root);
    lv_obj_set_style_line_width(g_mouth, 6, 0);
    lv_obj_set_style_line_color(g_mouth, lv_color_hex(kMouthWhite), 0);
    lv_obj_set_style_line_rounded(g_mouth, true, 0);
    lv_obj_set_style_line_opa(g_mouth, LV_OPA_COVER, 0);
    rebuildMouth();

    // Kiss pucker — a small filled oval, hidden unless the "kiss" expression.
    g_kiss = lv_obj_create(root);
    lv_obj_set_size(g_kiss, 26, 20);
    lv_obj_set_pos(g_kiss, kMouthCenterX - 13, kMouthY - 6);
    lv_obj_set_style_radius(g_kiss, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_kiss, lv_color_hex(kMouthRed), 0);
    lv_obj_set_style_bg_opa(g_kiss, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_kiss, 0, 0);
    lv_obj_clear_flag(g_kiss, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_kiss, LV_OBJ_FLAG_HIDDEN);

    // Menu button (hidden until swipe-up reveals it).
    g_menu_btn = lv_btn_create(root);
    lv_obj_set_size(g_menu_btn, 200, 60);
    lv_obj_align(g_menu_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(g_menu_btn, lv_color_hex(0xFF6688), 0);
    lv_obj_set_style_bg_opa(g_menu_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_menu_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(g_menu_btn, 3, 0);
    lv_obj_set_style_radius(g_menu_btn, 12, 0);
    lv_obj_add_flag(g_menu_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_menu_btn);
    lv_obj_add_event_cb(g_menu_btn,
        [](lv_event_t*) { menu.show(); },
        LV_EVENT_CLICKED, nullptr);
    lv_obj_t* mlabel = lv_label_create(g_menu_btn);
    lv_label_set_text(mlabel, "MENU");
    lv_obj_set_style_text_color(mlabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(mlabel, &lv_font_montserrat_14, 0);
    lv_obj_center(mlabel);

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

    const ExprStyle* s = &kStyles[0];
    for (const auto& st : kStyles) {
        if (tag == st.tag) { s = &st; break; }
    }

    // Stop any in-flight blink so it doesn't snap eyes back to the old height.
    lv_anim_delete(g_eye_l, blinkSetH);
    lv_anim_delete(g_eye_r, blinkSetH);

    // Eyes: resting height (lidding/narrowing).
    g_rest_eye_h = s->eyeH;
    applyEyeHeight(g_eye_l, g_pupil_l, s->eyeH);
    applyEyeHeight(g_eye_r, g_pupil_r, s->eyeH);

    // Eyebrows.
    setBrows(*s);

    // Mouth: kiss pucker vs line curve.
    if (s->kiss) {
        lv_obj_add_flag(g_mouth, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_kiss, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(g_mouth, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_kiss, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < kMouthPoints; ++i) g_mouth_expr_dy[i] = s->dy[i];
        lv_obj_set_style_line_color(g_mouth, lv_color_hex(s->mouthColor), 0);
        rebuildMouth();
    }
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
    lv_obj_clear_flag(g_menu_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_menu_btn);
    g_menu_btn_show_ms = millis();
}

void Face::tick(uint32_t nowMs) {
    if (g_menu_btn_show_ms != 0 &&
        nowMs - g_menu_btn_show_ms >= kMenuBtnAutoHideMs) {
        if (g_menu_btn) lv_obj_add_flag(g_menu_btn, LV_OBJ_FLAG_HIDDEN);
        g_menu_btn_show_ms = 0;
    }
}

}  // namespace stkchan
