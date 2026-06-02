// test/test_response_parser/test_response_parser.cpp
#include <unity.h>
#include <string>
#include "persona/ResponseParser.h"

using stkchan::ParsedReply;
using stkchan::parseReply;

void test_basic_happy_case() {
  auto p = parseReply("<speech>Hello!</speech><expr>happy</expr>");
  TEST_ASSERT_EQUAL_STRING("Hello!", p.speech.c_str());
  TEST_ASSERT_EQUAL_STRING("happy",  p.expr.c_str());
  TEST_ASSERT_TRUE(p.ok);
}

void test_strip_backtick_fence() {
  auto p = parseReply("```\n<speech>Hi</speech><expr>neutral</expr>\n```");
  TEST_ASSERT_EQUAL_STRING("Hi", p.speech.c_str());
  TEST_ASSERT_EQUAL_STRING("neutral", p.expr.c_str());
}

void test_missing_expr_defaults_neutral() {
  auto p = parseReply("<speech>okay</speech>");
  TEST_ASSERT_EQUAL_STRING("okay",    p.speech.c_str());
  TEST_ASSERT_EQUAL_STRING("neutral", p.expr.c_str());
}

void test_missing_speech_uses_raw_text() {
  auto p = parseReply("just some words");
  TEST_ASSERT_EQUAL_STRING("just some words", p.speech.c_str());
  TEST_ASSERT_EQUAL_STRING("neutral",         p.expr.c_str());
}

void test_unknown_expr_falls_back_neutral() {
  auto p = parseReply("<speech>hey</speech><expr>elated</expr>");
  TEST_ASSERT_EQUAL_STRING("neutral", p.expr.c_str());
}

void test_collapses_whitespace_in_speech() {
  auto p = parseReply("<speech>\n  hello\n  world\n</speech>");
  TEST_ASSERT_EQUAL_STRING("hello world", p.speech.c_str());
}

void test_lowercases_expr() {
  auto p = parseReply("<speech>hi</speech><expr>HAPPY</expr>");
  TEST_ASSERT_EQUAL_STRING("happy", p.expr.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_basic_happy_case);
  RUN_TEST(test_strip_backtick_fence);
  RUN_TEST(test_missing_expr_defaults_neutral);
  RUN_TEST(test_missing_speech_uses_raw_text);
  RUN_TEST(test_unknown_expr_falls_back_neutral);
  RUN_TEST(test_collapses_whitespace_in_speech);
  RUN_TEST(test_lowercases_expr);
  return UNITY_END();
}
