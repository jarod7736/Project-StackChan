#pragma once
#include <Arduino.h>
#include "vision/PresenceLogic.h"
#include "config.h"

// On-device face-DETECTION presence sensor. Owns the CoreS3 GC0308 camera and a
// pinned core-0 FreeRTOS task that runs esp-dl human-face detection on each
// captured RGB565 frame. The main loop calls tick() to fold new detections into
// PresenceDebounce (arrived/left edges) and reads the latest bbox for tracking.
//
// All heavy code compiles out when STKCHAN_PRESENCE==0 (see config.h) so the
// power-soak builds stay lean.

namespace stkchan {

class PresenceSensor {
 public:
  bool begin();                 // camera init + spawn core-0 infer task
  void tick(uint32_t nowMs);    // main loop: fold new detection into debounce
  void setScanEnabled(bool e);  // pause capture during turns (CPU/power/camera)

  bool present() const { return debounce_.present(); }
  bool consumeArrived() { return debounce_.consumeArrived(); }
  bool consumeLeft()    { return debounce_.consumeLeft(); }
  // Latest detected face bbox center + size (frame pixel coords). False if absent.
  bool hasFace(int& cx, int& cy, int& w, int& h) const;
  int  frameW() const { return box_.fw; }
  int  frameH() const { return box_.fh; }

 private:
  struct Det {
    bool     detected = false;
    int      cx = 0, cy = 0, w = 0, h = 0;
    int      fw = 320, fh = 240;
    float    score = 0.0f;
    uint32_t stamp = 0;
  };

  PresenceDebounce  debounce_{ PresenceParams{ kPresArriveHits, kPresAbsentMs } };
  Det               latest_{};                    // written by infer task
  Det               box_{};                       // last detected box (hasFace)
  volatile uint32_t seq_            = 0;           // bumped per new capture
  uint32_t          processedSeq_   = 0;
  volatile uint32_t scanIntervalMs_ = kPresIdleScanMs;
  volatile bool     scanEnabled_    = false;

#if STKCHAN_PRESENCE
  void        taskLoop();
  static void taskTrampoline(void* arg);
  void*       task_ = nullptr;                     // TaskHandle_t
#endif
};

extern PresenceSensor presence;

}  // namespace stkchan
