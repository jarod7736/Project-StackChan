#include "vision/PresenceSensor.h"

#if STKCHAN_PRESENCE
#include <M5CoreS3.h>
#include "esp_camera.h"
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#endif

namespace stkchan {

PresenceSensor presence;

#if STKCHAN_PRESENCE

// Guards the published detection result shared between the infer task (core 0)
// and the main loop (core 1).
static portMUX_TYPE s_resultMux = portMUX_INITIALIZER_UNLOCKED;

// esp-who reference thresholds; tune in the on-device spike.
//   MSR01(score, nms, top_k, resize_scale) — stage-1 candidate proposer.
//   MNP01(score, nms, top_k)               — stage-2 refiner.
static constexpr float kMsrScore = 0.3f, kMsrNms = 0.5f, kMsrResize = 0.2f;
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

    std::vector<int> shape = {(int)fb->height, (int)fb->width, 3};
    std::list<dl::detect::result_t>& cand =
        s1.infer((uint16_t*)fb->buf, shape);
    std::list<dl::detect::result_t>& res =
        s2.infer((uint16_t*)fb->buf, shape, cand);

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

    taskENTER_CRITICAL(&s_resultMux);
    latest_ = d;
    seq_++;
    taskEXIT_CRITICAL(&s_resultMux);
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
    if (d.detected) box_ = d;
  }
  // Faster pursuit while a face is present; lazy poll when the desk is empty.
  scanIntervalMs_ = debounce_.present() ? kPresTrackScanMs : kPresIdleScanMs;
}

bool PresenceSensor::hasFace(int& cx, int& cy, int& w, int& h) const {
  if (!debounce_.present()) return false;
  cx = box_.cx; cy = box_.cy; w = box_.w; h = box_.h;
  return true;
}

#else  // STKCHAN_PRESENCE == 0 — feature compiled out; stubs keep `presence` linkable.

bool PresenceSensor::begin() { return false; }
void PresenceSensor::tick(uint32_t) {}
void PresenceSensor::setScanEnabled(bool) {}
bool PresenceSensor::hasFace(int&, int&, int&, int&) const { return false; }

#endif

}  // namespace stkchan
