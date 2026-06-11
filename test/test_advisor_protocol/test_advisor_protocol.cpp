// test/test_advisor_protocol/test_advisor_protocol.cpp
#include <unity.h>
#include <string>
#include <vector>
#include "net/AdvisorProtocol.h"

using stkchan::AdvisorConfig;
using stkchan::AdvisorTurn;
using stkchan::AdvisorVariant;
using stkchan::buildAdvisorRequestBody;
using stkchan::isValidModelPair;
using stkchan::parseAdvisorResponse;

// ── model-pair validation ───────────────────────────────────────────────────

void test_valid_haiku_opus_pair() {
  TEST_ASSERT_TRUE(
      isValidModelPair("claude-haiku-4-5-20251001", "claude-opus-4-8"));
  TEST_ASSERT_TRUE(
      isValidModelPair("claude-sonnet-4-6", "claude-opus-4-7"));
  TEST_ASSERT_TRUE(isValidModelPair("claude-fable-5", "claude-fable-5"));
}

void test_invalid_weaker_advisor_rejected() {
  // Advisor must be at least as capable: Opus-4.8 executor needs Opus-4.8.
  TEST_ASSERT_FALSE(
      isValidModelPair("claude-opus-4-8", "claude-opus-4-7"));
  // Cross-family pairs are not allowed.
  TEST_ASSERT_FALSE(isValidModelPair("claude-fable-5", "claude-opus-4-8"));
  // A bare/unknown id never validates.
  TEST_ASSERT_FALSE(isValidModelPair("gemma3n:e4b", "claude-opus-4-8"));
}

// ── request-body construction ───────────────────────────────────────────────

void test_body_declares_advisor_tool() {
  std::vector<AdvisorTurn> turns = {{"user", "Hi"}};
  AdvisorConfig cfg;
  cfg.advisorModel = "claude-opus-4-8";
  std::string body = buildAdvisorRequestBody(
      "claude-haiku-4-5-20251001", "be brief", turns, 512, cfg);

  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, body));
  TEST_ASSERT_EQUAL_STRING("claude-haiku-4-5-20251001", doc["model"]);
  TEST_ASSERT_EQUAL_INT(512, doc["max_tokens"].as<int>());
  TEST_ASSERT_EQUAL_STRING("be brief", doc["system"]);

  JsonObject tool = doc["tools"][0];
  TEST_ASSERT_EQUAL_STRING("advisor_20260301", tool["type"]);
  TEST_ASSERT_EQUAL_STRING("advisor", tool["name"]);
  TEST_ASSERT_EQUAL_STRING("claude-opus-4-8", tool["model"]);
  // Unset knobs must be absent so the server applies its defaults.
  TEST_ASSERT_FALSE(tool["max_tokens"].is<int>());
  TEST_ASSERT_FALSE(tool["caching"].is<JsonObject>());

  TEST_ASSERT_EQUAL_STRING("user", doc["messages"][0]["role"]);
  TEST_ASSERT_EQUAL_STRING("Hi",   doc["messages"][0]["content"]);
}

void test_body_omits_system_when_empty() {
  std::vector<AdvisorTurn> turns = {{"user", "yo"}};
  AdvisorConfig cfg;
  cfg.advisorModel = "claude-opus-4-8";
  std::string body =
      buildAdvisorRequestBody("claude-sonnet-4-6", "", turns, 256, cfg);
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, body));
  TEST_ASSERT_FALSE(doc["system"].is<const char*>());
}

void test_body_emits_optional_knobs() {
  std::vector<AdvisorTurn> turns = {{"user", "x"}};
  AdvisorConfig cfg;
  cfg.advisorModel = "claude-opus-4-8";
  cfg.maxTokens = 2048;
  cfg.maxUses   = 3;
  cfg.caching   = true;
  cfg.cacheTtl  = "1h";
  std::string body =
      buildAdvisorRequestBody("claude-sonnet-4-6", "", turns, 256, cfg);
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, body));
  JsonObject tool = doc["tools"][0];
  TEST_ASSERT_EQUAL_INT(2048, tool["max_tokens"].as<int>());
  TEST_ASSERT_EQUAL_INT(3,    tool["max_uses"].as<int>());
  TEST_ASSERT_EQUAL_STRING("ephemeral", tool["caching"]["type"]);
  TEST_ASSERT_EQUAL_STRING("1h",        tool["caching"]["ttl"]);
}

// ── response parsing ────────────────────────────────────────────────────────

void test_parse_result_variant_concatenates_text() {
  // server_tool_use is ignored; the two text blocks join in order.
  const char* resp = R"({
    "role":"assistant",
    "content":[
      {"type":"text","text":"Let me consult. "},
      {"type":"server_tool_use","id":"srvtoolu_1","name":"advisor","input":{}},
      {"type":"advisor_tool_result","tool_use_id":"srvtoolu_1",
       "content":{"type":"advisor_result","text":"Use a channel.","stop_reason":"end_turn"}},
      {"type":"text","text":"Here is the answer."}
    ],
    "stop_reason":"end_turn"
  })";
  auto o = parseAdvisorResponse(resp);
  TEST_ASSERT_TRUE(o.ok);
  TEST_ASSERT_EQUAL_STRING("Let me consult. Here is the answer.",
                           o.assistantText.c_str());
  TEST_ASSERT_TRUE(o.variant == AdvisorVariant::kResult);
  TEST_ASSERT_EQUAL_STRING("Use a channel.", o.adviceText.c_str());
  TEST_ASSERT_EQUAL_STRING("end_turn", o.stopReason.c_str());
  TEST_ASSERT_EQUAL_STRING("", o.errorCode.c_str());
}

void test_parse_redacted_variant() {
  const char* resp = R"({
    "content":[
      {"type":"advisor_tool_result","tool_use_id":"s1",
       "content":{"type":"advisor_redacted_result","encrypted_content":"blob","stop_reason":"max_tokens"}},
      {"type":"text","text":"answer"}
    ]})";
  auto o = parseAdvisorResponse(resp);
  TEST_ASSERT_TRUE(o.ok);
  TEST_ASSERT_TRUE(o.variant == AdvisorVariant::kRedacted);
  TEST_ASSERT_EQUAL_STRING("answer", o.assistantText.c_str());
  TEST_ASSERT_EQUAL_STRING("max_tokens", o.stopReason.c_str());
  TEST_ASSERT_EQUAL_STRING("", o.adviceText.c_str());
}

void test_parse_error_variant_does_not_fail_reply() {
  const char* resp = R"({
    "content":[
      {"type":"advisor_tool_result","tool_use_id":"s1",
       "content":{"type":"advisor_tool_result_error","error_code":"overloaded"}},
      {"type":"text","text":"answered anyway"}
    ]})";
  auto o = parseAdvisorResponse(resp);
  TEST_ASSERT_TRUE(o.ok);   // executor still produced a usable reply
  TEST_ASSERT_TRUE(o.variant == AdvisorVariant::kError);
  TEST_ASSERT_EQUAL_STRING("overloaded", o.errorCode.c_str());
  TEST_ASSERT_EQUAL_STRING("answered anyway", o.assistantText.c_str());
}

void test_parse_no_advisor_call() {
  const char* resp = R"({"content":[{"type":"text","text":"just text"}]})";
  auto o = parseAdvisorResponse(resp);
  TEST_ASSERT_TRUE(o.ok);
  TEST_ASSERT_TRUE(o.variant == AdvisorVariant::kNone);
  TEST_ASSERT_EQUAL_STRING("just text", o.assistantText.c_str());
}

void test_parse_empty_or_malformed_is_not_ok() {
  TEST_ASSERT_FALSE(parseAdvisorResponse("not json").ok);
  TEST_ASSERT_FALSE(parseAdvisorResponse("{\"content\":[]}").ok);
  // content present but only an advisor block, no visible text -> not usable.
  const char* resp = R"({"content":[
      {"type":"advisor_tool_result","tool_use_id":"s1",
       "content":{"type":"advisor_result","text":"plan only"}}]})";
  TEST_ASSERT_FALSE(parseAdvisorResponse(resp).ok);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_haiku_opus_pair);
  RUN_TEST(test_invalid_weaker_advisor_rejected);
  RUN_TEST(test_body_declares_advisor_tool);
  RUN_TEST(test_body_omits_system_when_empty);
  RUN_TEST(test_body_emits_optional_knobs);
  RUN_TEST(test_parse_result_variant_concatenates_text);
  RUN_TEST(test_parse_redacted_variant);
  RUN_TEST(test_parse_error_variant_does_not_fail_reply);
  RUN_TEST(test_parse_no_advisor_call);
  RUN_TEST(test_parse_empty_or_malformed_is_not_ok);
  return UNITY_END();
}
