#include "vision/PresenceSensor.h"

#if STKCHAN_PRESENCE
#include <M5CoreS3.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#endif

namespace stkchan {

PresenceSensor presence;

#if STKCHAN_PRESENCE

// Guards the published detection result shared between the infer task (core 0)
// and the main loop (core 1).
static portMUX_TYPE s_resultMux = portMUX_INITIALIZER_UNLOCKED;

// Latest inference result, served by /api/debug/presence (lightweight JSON;
// the heavy frame-dump/replay debug endpoints from vision bring-up are gone).
static volatile uint32_t s_dbgInferMs = 0, s_dbgInfers = 0;
static volatile int      s_dbgDet     = 0;
static volatile float    s_dbgScore   = 0.0f;
static volatile int      s_dbgCands   = 0;
static volatile float    s_dbgC1Top   = 0.0f;

void presenceDebugStatus(uint32_t& inferMs, uint32_t& infers, int& det,
                         float& score, int& cands, float& c1top) {
  inferMs = s_dbgInferMs; infers = s_dbgInfers; det = s_dbgDet;
  score = s_dbgScore; cands = s_dbgCands; c1top = s_dbgC1Top;
}

// esp-who reference thresholds; tune in the on-device spike.
//   MSR01(score, nms, top_k, resize_scale) — stage-1 candidate proposer.
//   MNP01(score, nms, top_k)               — stage-2 refiner.
// EXACT mirror of the known-working CameraWebServer reference (app_httpd.cpp
// TWO_STAGE): s1(0.1, 0.5, 10, 0.2) + s2(0.5, 0.3, 5). Stage 2 rescales
// candidate boxes by 1/resize_scale — only ever validated at 0.2.
static constexpr float kMsrScore = 0.1f, kMsrNms = 0.5f, kMsrResize = 0.2f;
static constexpr int   kMsrTopK  = 10;
static constexpr float kMnpScore = 0.5f, kMnpNms = 0.3f;
static constexpr int   kMnpTopK  = 5;

bool PresenceSensor::begin() {
  if (!CoreS3.Camera.begin()) {
    Serial.println("[PRES] camera init FAILED");
    return false;
  }
  Serial.println("[PRES] camera init ok");
  BaseType_t ok = xTaskCreatePinnedToCore(
      &PresenceSensor::taskTrampoline, "presence", 8192, this, 1,
      reinterpret_cast<TaskHandle_t*>(&task_), 0);
  return ok == pdPASS;
}

void PresenceSensor::setScanEnabled(bool e) { scanEnabled_ = e; }

void PresenceSensor::taskTrampoline(void* arg) {
  static_cast<PresenceSensor*>(arg)->taskLoop();
}

void PresenceSensor::taskLoop() {
  // Detectors allocate their models once (PSRAM); reuse across inferences.
  HumanFaceDetectMSR01 s1(kMsrScore, kMsrNms, kMsrTopK, kMsrResize);
  HumanFaceDetectMNP01 s2(kMnpScore, kMnpNms, kMnpTopK);
  uint32_t lastCapMs = 0;

  for (;;) {
    if (!scanEnabled_) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    uint32_t now = millis();
    if (now - lastCapMs < scanIntervalMs_) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    lastCapMs = now;

    if (!CoreS3.Camera.get()) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    camera_fb_t* fb = CoreS3.Camera.fb;

    uint32_t t0 = millis();
    std::vector<int> shape = {(int)fb->height, (int)fb->width, 3};
    // Convert to RGB888 (BGR byte order, matching FB_BGR888 in the reference)
    // before inference. REQUIRED: the prebuilt esp-dl's direct-RGB565 infer
    // path returns zero candidates on verified-good frames; the RGB888 path is
    // the one the CameraWebServer reference validates. (Bring-up 2026-06-10.)
    static uint8_t* s_rgb888 = nullptr;
    if (!s_rgb888) s_rgb888 = (uint8_t*)ps_malloc((size_t)fb->width * fb->height * 3);
    if (!s_rgb888 || !fmt2rgb888(fb->buf, fb->len, fb->format, s_rgb888)) {
      CoreS3.Camera.free();
      continue;
    }
    std::list<dl::detect::result_t>& cand =
        s1.infer(s_rgb888, shape);
    std::list<dl::detect::result_t>& res =
        s2.infer(s_rgb888, shape, cand);
    uint32_t inferMs = millis() - t0;
    s_dbgInferMs = inferMs;
    s_dbgInfers  = s_dbgInfers + 1;
    s_dbgCands   = (int)cand.size();
    s_dbgC1Top   = 0.0f;
    for (auto& c : cand) {
      if (c.score > s_dbgC1Top) s_dbgC1Top = c.score;
    }

    Det d;
    d.stamp = now;
    d.fw    = fb->width;
    d.fh    = fb->height;
    if (!res.empty()) {
      const dl::detect::result_t* best = nullptr;
      for (auto& r : res) {
        if (!best || r.score > best->score) best = &r;
      }
      const int lx = best->box[0], ly = best->box[1];
      const int rx = best->box[2], ry = best->box[3];
      d.detected = true;
      d.cx = (lx + rx) / 2;
      d.cy = (ly + ry) / 2;
      d.w  = rx - lx;
      d.h  = ry - ly;
      d.score = best->score;
    }

    CoreS3.Camera.free();
    s_dbgDet   = (int)d.detected;
    s_dbgScore = d.score;

    taskENTER_CRITICAL(&s_resultMux);
    latest_ = d;
    seq_++;
    taskEXIT_CRITICAL(&s_resultMux);

    // Throttled instrumentation: inference time, detection, stage-1 top score,
    // task stack high-water, free PSRAM.
    static uint32_t s_logCtr = 0;
    if ((++s_logCtr % 16) == 0) {
      Serial.printf("[PRES] infer=%lums det=%d score=%.2f c1top=%.2f stackHW=%u psram=%u\n",
                    (unsigned long)inferMs, (int)d.detected, d.score,
                    (float)s_dbgC1Top,
                    (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                    (unsigned)ESP.getFreePsram());
    }
  }
}

void PresenceSensor::tick(uint32_t nowMs) {
  (void)nowMs;
  Det      d;
  uint32_t seq;
  taskENTER_CRITICAL(&s_resultMux);
  d   = latest_;
  seq = seq_;
  taskEXIT_CRITICAL(&s_resultMux);

  if (seq != processedSeq_) {            // process each capture exactly once
    processedSeq_ = seq;
    debounce_.update(d.detected, d.stamp);
    if (d.detected) { box_ = d; fresh_ = true; }
  }
  // Faster pursuit while a face is present; lazy poll when the desk is empty.
  scanIntervalMs_ = debounce_.present() ? kPresTrackScanMs : kPresIdleScanMs;
}

bool PresenceSensor::hasFace(int& cx, int& cy, int& w, int& h) const {
  if (!debounce_.present()) return false;
  cx = box_.cx; cy = box_.cy; w = box_.w; h = box_.h;
  return true;
}

bool PresenceSensor::takeFreshFace(int& cx, int& cy, int& w, int& h) {
  if (!fresh_) return false;
  fresh_ = false;
  cx = box_.cx; cy = box_.cy; w = box_.w; h = box_.h;
  return true;
}

#else  // STKCHAN_PRESENCE == 0 — feature compiled out; stubs keep `presence` linkable.

bool PresenceSensor::begin() { return false; }
void PresenceSensor::tick(uint32_t) {}
void PresenceSensor::setScanEnabled(bool) {}
bool PresenceSensor::hasFace(int&, int&, int&, int&) const { return false; }
bool PresenceSensor::takeFreshFace(int&, int&, int&, int&) { return false; }

#endif

}  // namespace stkchan
