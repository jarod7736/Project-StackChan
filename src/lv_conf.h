// LVGL configuration for Project-StackChan (CoreS3).
// See lvgl/lvgl/lv_conf_template.h for the full set of options.

#pragma once

#define LV_CONF_INCLUDE_SIMPLE 1

// --- Color / resolution ----------------------------------------------------
#define LV_COLOR_DEPTH 16          // CoreS3 LCD is RGB565
#define LV_COLOR_16_SWAP 1         // M5GFX expects byte-swapped RGB565

// --- Memory ----------------------------------------------------------------
// LVGL's internal heap. 48 KB is enough for our simple face (eyes + mouth
// + a few animations) without trying to hit PSRAM from the LVGL allocator
// (LVGL's allocator wants tight locality; PSRAM-via-ps_malloc has higher
// latency).
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE   (48U * 1024U)

// --- Timing ----------------------------------------------------------------
// Pull millis() from Arduino-ESP32; LVGL calls this for animation timing.
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DEF_REFR_PERIOD 30   // ms between display refreshes (~33 fps)

// --- Features we DON'T need ------------------------------------------------
// Trim to keep flash impact small — we only need basic widgets + animations.
#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0

#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0

// --- Widgets we DO need ----------------------------------------------------
// We draw eyes and mouth with primitives (lv_obj + filled circles, an
// arc/line for mouth). Default widget set is fine; we can trim further
// once the face is working.

// --- Fonts -----------------------------------------------------------------
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
