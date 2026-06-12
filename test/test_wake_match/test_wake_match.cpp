// test/test_wake_match/test_wake_match.cpp
// Fuzzy wake-phrase prefix matching over Whisper-style transcripts.
#include <unity.h>
#include "app/WakeMatch.h"

using stkchan::matchWakeVariants;

using stkchan::normalizeTranscript;
using stkchan::matchWake;

void setUp() {}
void tearDown() {}

static const char* kWake = "hey stack chan";

static void test_normalize_strips_punct_and_case() {
  TEST_ASSERT_EQUAL_STRING("heystackchan",
      normalizeTranscript("Hey, Stack-Chan!").c_str());
  TEST_ASSERT_EQUAL_STRING("", normalizeTranscript("  ...!?  ").c_str());
}

static void test_exact_match_no_remainder() {
  auto r = matchWake("Hey Stack Chan", kWake);
  TEST_ASSERT_TRUE(r.matched);
  TEST_ASSERT_EQUAL_STRING("", r.remainder.c_str());
}

static void test_whisper_variants_match() {
  TEST_ASSERT_TRUE(matchWake("Hey, Stackchan.", kWake).matched);
  TEST_ASSERT_TRUE(matchWake("hey stack chan", kWake).matched);
  TEST_ASSERT_TRUE(matchWake("Hey Stack-chan!", kWake).matched);
  // one substitution inside the phrase ("stak")
  TEST_ASSERT_TRUE(matchWake("Hey Stak Chan", kWake).matched);
}

static void test_remainder_is_original_text() {
  auto r = matchWake("Hey Stack-chan, what's the weather like?", kWake);
  TEST_ASSERT_TRUE(r.matched);
  TEST_ASSERT_EQUAL_STRING("what's the weather like?", r.remainder.c_str());
}

static void test_non_wake_speech_rejected() {
  TEST_ASSERT_FALSE(matchWake("What's for dinner tonight?", kWake).matched);
  TEST_ASSERT_FALSE(matchWake("Hey Alexa, play music", kWake).matched);
  TEST_ASSERT_FALSE(matchWake("", kWake).matched);
  // wake word later in the sentence is NOT a wake (prefix only)
  TEST_ASSERT_FALSE(matchWake("I said hey stack chan yesterday", kWake).matched);
}

static void test_too_many_edits_rejected() {
  // "hey snack man" vs "hey stack chan": >2 edits on the normalized prefix
  TEST_ASSERT_FALSE(matchWake("hey snack man", kWake).matched);
}

static void test_haystack_chair_false_accept_regression() {
  // dist("haystackcha(i)", "heystackchan") == 2 — admitted at maxEdits=2,
  // correctly rejected at the default 1. Real-world phrase, real risk.
  TEST_ASSERT_FALSE(matchWake("haystack chair", kWake).matched);
}

static void test_single_edit_mistranscription_still_matches() {
  TEST_ASSERT_TRUE(matchWake("Hey Stack Chen, hello there", kWake).matched);
}

static void test_variants_accept_live_whisper_mishearings() {
  // Real transcripts collected from the robot 2026-06-11:
  auto a = matchWakeVariants("Hey, Stat Chan.", kWake);
  TEST_ASSERT_TRUE(a.matched);
  auto b = matchWakeVariants("Hey, Stack Jam.", kWake);
  TEST_ASSERT_TRUE(b.matched);
  auto c = matchWakeVariants("Hey, Stack Jam. What time is it?", kWake);
  TEST_ASSERT_TRUE(c.matched);
  TEST_ASSERT_EQUAL_STRING("What time is it?", c.remainder.c_str());
}

static void test_variants_still_reject_noise() {
  // Real noise hallucinations collected from the robot 2026-06-11:
  TEST_ASSERT_FALSE(matchWakeVariants(
      "Go to Beadaholique.com for all of your beading supplies needs!", kWake).matched);
  TEST_ASSERT_FALSE(matchWakeVariants("Thank you for watching.", kWake).matched);
  TEST_ASSERT_FALSE(matchWakeVariants("bzzzzzzzzzzzzzzzzzzzzz", kWake).matched);
  TEST_ASSERT_FALSE(matchWakeVariants("haystack chair", kWake).matched);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_variants_accept_live_whisper_mishearings);
  RUN_TEST(test_variants_still_reject_noise);
  RUN_TEST(test_normalize_strips_punct_and_case);
  RUN_TEST(test_exact_match_no_remainder);
  RUN_TEST(test_whisper_variants_match);
  RUN_TEST(test_remainder_is_original_text);
  RUN_TEST(test_non_wake_speech_rejected);
  RUN_TEST(test_too_many_edits_rejected);
  RUN_TEST(test_haystack_chair_false_accept_regression);
  RUN_TEST(test_single_edit_mistranscription_still_matches);
  return UNITY_END();
}
