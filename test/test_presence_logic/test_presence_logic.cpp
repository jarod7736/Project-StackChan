#include <unity.h>
#include "vision/PresenceLogic.h"

using namespace stkchan;

// ── PresenceDebounce: arrival hysteresis ───────────────────────────────────

void test_arrives_only_after_K_consecutive_hits() {
  PresenceDebounce d({/*arriveHits=*/3, /*absentMs=*/30000});
  d.update(true, 0);
  TEST_ASSERT_FALSE(d.present());
  d.update(true, 100);
  TEST_ASSERT_FALSE(d.present());
  d.update(true, 200);          // 3rd consecutive -> present
  TEST_ASSERT_TRUE(d.present());
}

void test_arrived_edge_fires_once() {
  PresenceDebounce d({3, 30000});
  d.update(true, 0);
  d.update(true, 100);
  d.update(true, 200);
  TEST_ASSERT_TRUE(d.consumeArrived());
  TEST_ASSERT_FALSE(d.consumeArrived());   // edge, not level
}

void test_single_false_positive_does_not_arrive() {
  PresenceDebounce d({3, 30000});
  d.update(true, 0);
  d.update(false, 100);         // breaks the streak
  d.update(true, 200);
  TEST_ASSERT_FALSE(d.present());
}

void test_brief_gap_does_not_leave() {
  PresenceDebounce d({3, 30000});
  d.update(true, 0); d.update(true, 100); d.update(true, 200);  // present, lastSeen=200
  d.update(false, 5000);        // 4.8 s gap < 30 s
  TEST_ASSERT_TRUE(d.present());
  TEST_ASSERT_FALSE(d.consumeLeft());
}

void test_leaves_after_absent_timeout() {
  PresenceDebounce d({3, 30000});
  d.update(true, 0); d.update(true, 100); d.update(true, 200);  // present, lastSeen=200
  d.update(false, 30200);       // 30 s since lastSeen -> absent
  TEST_ASSERT_FALSE(d.present());
  TEST_ASSERT_TRUE(d.consumeLeft());
}

// ── trackTarget: bbox -> servo control math ────────────────────────────────

static TrackParams params() {
  TrackParams tp;                // defaults: deadband .12, gain .5, slew 8/5,
  return tp;                     // limits ±45/±25, full 45/25
}

void test_centered_face_holds_still() {
  auto t = trackTarget(160, 120, 320, 240, /*curYaw=*/0, /*curPitch=*/0, params());
  TEST_ASSERT_FALSE(t.move);
  TEST_ASSERT_EQUAL_INT(0, t.yaw);
  TEST_ASSERT_EQUAL_INT(0, t.pitch);
}

void test_small_offset_within_deadband_holds() {
  // ex = (165-160)/160 = 0.03 < 0.12 deadband
  auto t = trackTarget(165, 120, 320, 240, 0, 0, params());
  TEST_ASSERT_FALSE(t.move);
}

void test_face_right_steps_yaw_by_slew() {
  // ex = (240-160)/160 = 0.5 ; desired = 0 - round(0.5*0.5*45) = -11 ; slew-capped to -8
  auto t = trackTarget(240, 120, 320, 240, 0, 0, params());
  TEST_ASSERT_TRUE(t.move);
  TEST_ASSERT_EQUAL_INT(-8, t.yaw);
  TEST_ASSERT_EQUAL_INT(0, t.pitch);     // y centered
}

void test_face_left_steps_yaw_opposite() {
  auto t = trackTarget(80, 120, 320, 240, 0, 0, params());   // ex = -0.5
  TEST_ASSERT_EQUAL_INT(8, t.yaw);
}

void test_yaw_clamps_to_mechanical_limit() {
  // curYaw near max, face far left pushes further; clamp at +45
  auto t = trackTarget(0, 120, 320, 240, /*curYaw=*/42, 0, params());  // ex=-1
  TEST_ASSERT_EQUAL_INT(45, t.yaw);
}

void test_pitch_tracks_vertical_offset() {
  // ey = (200-120)/120 = 0.667 ; desired = 0 - round(0.5*0.667*25) = -8 ; slew cap -5
  auto t = trackTarget(160, 200, 320, 240, 0, 0, params());
  TEST_ASSERT_EQUAL_INT(-5, t.pitch);
  TEST_ASSERT_EQUAL_INT(0, t.yaw);       // x centered
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_arrives_only_after_K_consecutive_hits);
  RUN_TEST(test_arrived_edge_fires_once);
  RUN_TEST(test_single_false_positive_does_not_arrive);
  RUN_TEST(test_brief_gap_does_not_leave);
  RUN_TEST(test_leaves_after_absent_timeout);
  RUN_TEST(test_centered_face_holds_still);
  RUN_TEST(test_small_offset_within_deadband_holds);
  RUN_TEST(test_face_right_steps_yaw_by_slew);
  RUN_TEST(test_face_left_steps_yaw_opposite);
  RUN_TEST(test_yaw_clamps_to_mechanical_limit);
  RUN_TEST(test_pitch_tracks_vertical_offset);
  return UNITY_END();
}
