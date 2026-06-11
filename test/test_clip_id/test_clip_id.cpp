// test/test_clip_id/test_clip_id.cpp
// Known-answer tests for the FNV-1a 32-bit hash and the clip path scheme.
// The render script (tools/render_speeches.py) implements the same hash in
// Python; the "foobar" vector below also appears in its --selftest.
#include <unity.h>
#include <string.h>
#include "services/ClipId.h"

using stkchan::fnv1a32;
using stkchan::clipPathForText;

// Standard FNV-1a 32-bit vectors (offset basis 0x811c9dc5, prime 0x01000193).
static void test_fnv1a32_empty() {
  TEST_ASSERT_EQUAL_UINT32(0x811c9dc5u, fnv1a32("", 0));
}
static void test_fnv1a32_a() {
  TEST_ASSERT_EQUAL_UINT32(0xe40c292cu, fnv1a32("a", 1));
}
static void test_fnv1a32_foobar() {
  TEST_ASSERT_EQUAL_UINT32(0xbf9cf968u, fnv1a32("foobar", 6));
}

static void test_path_format() {
  // "foobar" → 0xbf9cf968 → "/clips/bf9cf968.mp3" (lowercase, zero-padded)
  std::string p = clipPathForText("foobar");
  TEST_ASSERT_EQUAL_STRING("/clips/bf9cf968.mp3", p.c_str());
}
static void test_path_zero_padded() {
  // Every path must be exactly strlen("/clips/") + 8 + 4 long.
  std::string p = clipPathForText("Hm, didn't catch that.");
  TEST_ASSERT_EQUAL_UINT32(7 + 8 + 4, (uint32_t)p.size());
  TEST_ASSERT_TRUE(p.rfind("/clips/", 0) == 0);
  TEST_ASSERT_EQUAL_STRING(".mp3", p.c_str() + p.size() - 4);
}
static void test_path_deterministic() {
  TEST_ASSERT_EQUAL_STRING(clipPathForText("Oh! Hi there.").c_str(),
                           clipPathForText("Oh! Hi there.").c_str());
}
static void test_path_distinct_texts_distinct_paths() {
  TEST_ASSERT_TRUE(clipPathForText("Oh! Hi there.") !=
                   clipPathForText("There you are!"));
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fnv1a32_empty);
  RUN_TEST(test_fnv1a32_a);
  RUN_TEST(test_fnv1a32_foobar);
  RUN_TEST(test_path_format);
  RUN_TEST(test_path_zero_padded);
  RUN_TEST(test_path_deterministic);
  RUN_TEST(test_path_distinct_texts_distinct_paths);
  return UNITY_END();
}
