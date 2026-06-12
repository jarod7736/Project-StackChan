// test/test_wake_vad/test_wake_vad.cpp
// Energy-VAD state machine: trip on sustained speech, close on trailing
// silence, overflow at the cap, adapt the floor only on quiet frames.
#include <unity.h>
#include "app/WakeVad.h"

using stkchan::WakeVad;
using stkchan::WakeVadConfig;

void setUp() {}
void tearDown() {}

// Config used by most tests: floor starts at 100, speech = >300 RMS.
static WakeVadConfig cfg() {
  WakeVadConfig c;
  c.tripRatio          = 3.0f;
  c.tripFrames         = 3;
  c.closeSilenceFrames = 5;
  c.maxFrames          = 20;
  c.floorAlpha         = 0.5f;   // fast adaptation for testing
  c.floorInit          = 100.0f;
  c.floorMin           = 50.0f;
  c.warmupFrames       = 0;      // most tests exercise steady-state behavior
  return c;
}

static void test_quiet_never_trips() {
  WakeVad v(cfg());
  for (int i = 0; i < 100; ++i)
    TEST_ASSERT_TRUE(v.onFrame(120.0f) == WakeVad::Event::None);
  TEST_ASSERT_FALSE(v.tripped());
}

static void test_trips_after_consecutive_speech_frames() {
  WakeVad v(cfg());
  TEST_ASSERT_TRUE(v.onFrame(500.0f) == WakeVad::Event::None);
  TEST_ASSERT_TRUE(v.onFrame(500.0f) == WakeVad::Event::None);
  TEST_ASSERT_TRUE(v.onFrame(500.0f) == WakeVad::Event::Tripped);
  TEST_ASSERT_TRUE(v.tripped());
}

static void test_speech_blip_does_not_trip() {
  WakeVad v(cfg());
  v.onFrame(500.0f);
  v.onFrame(500.0f);
  v.onFrame(80.0f);   // blip ends the run before tripFrames
  TEST_ASSERT_TRUE(v.onFrame(500.0f) == WakeVad::Event::None);
  TEST_ASSERT_FALSE(v.tripped());
}

static void test_closes_after_trailing_silence() {
  WakeVad v(cfg());
  for (int i = 0; i < 3; ++i) v.onFrame(500.0f);  // trip
  for (int i = 0; i < 4; ++i)
    TEST_ASSERT_TRUE(v.onFrame(80.0f) == WakeVad::Event::None);
  TEST_ASSERT_TRUE(v.onFrame(80.0f) == WakeVad::Event::Closed);  // 5th quiet
}

static void test_speech_resets_silence_run() {
  WakeVad v(cfg());
  for (int i = 0; i < 3; ++i) v.onFrame(500.0f);
  for (int i = 0; i < 4; ++i) v.onFrame(80.0f);
  v.onFrame(500.0f);  // speech resumes — silence run resets
  for (int i = 0; i < 4; ++i)
    TEST_ASSERT_TRUE(v.onFrame(80.0f) == WakeVad::Event::None);
  TEST_ASSERT_TRUE(v.onFrame(80.0f) == WakeVad::Event::Closed);
}

static void test_overflow_at_cap() {
  WakeVad v(cfg());
  for (int i = 0; i < 3; ++i) v.onFrame(500.0f);  // trip
  WakeVad::Event last = WakeVad::Event::None;
  for (int i = 0; i < 20; ++i) last = v.onFrame(500.0f);  // loud forever
  TEST_ASSERT_TRUE(last == WakeVad::Event::Overflow);
}

static void test_floor_adapts_only_on_quiet() {
  WakeVad v(cfg());
  for (int i = 0; i < 50; ++i) v.onFrame(200.0f);  // quiet-ish, floor rises
  TEST_ASSERT_TRUE(v.noiseFloor() > 150.0f);
  float before = v.noiseFloor();
  v.onFrame(5000.0f);  // single loud frame must NOT raise the floor
  TEST_ASSERT_TRUE(v.noiseFloor() <= before + 0.001f);
}

static void test_floor_never_below_min() {
  WakeVad v(cfg());
  for (int i = 0; i < 200; ++i) v.onFrame(0.0f);
  TEST_ASSERT_TRUE(v.noiseFloor() >= 50.0f);
}

static void test_warmup_frames_ignored_entirely() {
  // Mic warm-up: first N frames after (re)start are near-zero. They must
  // neither adapt the floor nor count toward a trip (live bug 2026-06-11:
  // warm-up zeros dragged the floor to floorMin -> threshold below room
  // noise -> perpetual trip/STT loop -> internal heap exhaustion).
  WakeVadConfig c = cfg();
  c.warmupFrames = 10;
  WakeVad v(c);
  for (int i = 0; i < 10; ++i)
    TEST_ASSERT_TRUE(v.onFrame(0.0f) == WakeVad::Event::None);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, v.noiseFloor());  // floor untouched
  // Loud frames during warm-up must not have counted toward tripFrames:
  WakeVad v2(c);
  for (int i = 0; i < 9; ++i) v2.onFrame(500.0f);   // all inside warm-up
  TEST_ASSERT_FALSE(v2.tripped());
}

static void test_warmup_resets_with_reset() {
  WakeVadConfig c = cfg();
  c.warmupFrames = 5;
  WakeVad v(c);
  for (int i = 0; i < 5; ++i) v.onFrame(0.0f);      // consume warm-up
  for (int i = 0; i < 3; ++i) v.onFrame(500.0f);    // trip normally
  TEST_ASSERT_TRUE(v.tripped());
  v.reset();
  // After reset (new capture), warm-up zeros again must not move the floor.
  float before = v.noiseFloor();
  for (int i = 0; i < 5; ++i) v.onFrame(0.0f);
  TEST_ASSERT_EQUAL_FLOAT(before, v.noiseFloor());
}

static void test_reset_rearms() {
  WakeVad v(cfg());
  for (int i = 0; i < 3; ++i) v.onFrame(500.0f);
  TEST_ASSERT_TRUE(v.tripped());
  v.reset();
  TEST_ASSERT_FALSE(v.tripped());
  v.onFrame(500.0f);
  v.onFrame(500.0f);
  TEST_ASSERT_TRUE(v.onFrame(500.0f) == WakeVad::Event::Tripped);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_quiet_never_trips);
  RUN_TEST(test_trips_after_consecutive_speech_frames);
  RUN_TEST(test_speech_blip_does_not_trip);
  RUN_TEST(test_closes_after_trailing_silence);
  RUN_TEST(test_speech_resets_silence_run);
  RUN_TEST(test_overflow_at_cap);
  RUN_TEST(test_floor_adapts_only_on_quiet);
  RUN_TEST(test_floor_never_below_min);
  RUN_TEST(test_warmup_frames_ignored_entirely);
  RUN_TEST(test_warmup_resets_with_reset);
  RUN_TEST(test_reset_rearms);
  return UNITY_END();
}
