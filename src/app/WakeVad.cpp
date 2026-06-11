#include "WakeVad.h"

namespace stkchan {

WakeVad::Event WakeVad::onFrame(float rms) {
  const bool speech = rms > floor_ * cfg_.tripRatio;

  if (!tripped_) {
    if (speech) {
      if (++speechRun_ >= cfg_.tripFrames) {
        tripped_         = true;
        quietRun_        = 0;
        framesSinceTrip_ = 0;
        return Event::Tripped;
      }
    } else {
      speechRun_ = 0;
      // Adapt the floor only on quiet frames so speech can't raise it.
      floor_ += cfg_.floorAlpha * (rms - floor_);
      if (floor_ < cfg_.floorMin) floor_ = cfg_.floorMin;
    }
    return Event::None;
  }

  ++framesSinceTrip_;
  if (framesSinceTrip_ >= cfg_.maxFrames) return Event::Overflow;
  if (speech) quietRun_ = 0; else ++quietRun_;
  if (quietRun_ >= cfg_.closeSilenceFrames) return Event::Closed;
  return Event::None;
}

void WakeVad::reset() {
  tripped_         = false;
  speechRun_       = 0;
  quietRun_        = 0;
  framesSinceTrip_ = 0;
  // floor_ deliberately persists — the room didn't change.
}

}  // namespace stkchan
