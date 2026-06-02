#include <unity.h>
#include "face/ExpressionMap.h"

using stkchan::expressionFor;

void test_happy_has_positive_pitch_and_bigger_bob() {
  auto e = expressionFor("happy");
  TEST_ASSERT_EQUAL_INT(stkchan::AvatarExprIdx::Happy, e.idx);
  TEST_ASSERT_TRUE(e.pitchDeg > 0);
  TEST_ASSERT_TRUE(e.bobAmp > 1.0f);
}

void test_sad_droops() {
  auto e = expressionFor("sad");
  TEST_ASSERT_EQUAL_INT(stkchan::AvatarExprIdx::Sad, e.idx);
  TEST_ASSERT_TRUE(e.pitchDeg < 0);
}

void test_unknown_falls_back_to_neutral() {
  auto e = expressionFor("majestic");
  TEST_ASSERT_EQUAL_INT(stkchan::AvatarExprIdx::Neutral, e.idx);
  TEST_ASSERT_EQUAL_INT(0, e.pitchDeg);
}

void test_doubt_tilts_yaw() {
  auto e = expressionFor("doubt");
  TEST_ASSERT_EQUAL_INT(stkchan::AvatarExprIdx::Doubt, e.idx);
  TEST_ASSERT_TRUE(e.yawDeg != 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_happy_has_positive_pitch_and_bigger_bob);
  RUN_TEST(test_sad_droops);
  RUN_TEST(test_unknown_falls_back_to_neutral);
  RUN_TEST(test_doubt_tilts_yaw);
  return UNITY_END();
}
