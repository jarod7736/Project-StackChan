// test/test_mcp_protocol/test_mcp_protocol.cpp
#include <unity.h>
#include "services/McpProtocol.h"
using stkchan::McpProtocol; using stkchan::McpTool; using stkchan::McpOutcome;

static McpProtocol makeServer(bool toolOk = true) {
  McpProtocol p;
  p.addTool({"echo", "echoes", R"({"type":"object","properties":{"v":{"type":"string"}},"required":["v"]})",
    [toolOk](JsonVariantConst args, std::string& r) {
      r = std::string("got:") + (args["v"] | ""); return toolOk; }});
  return p;
}
static JsonDocument roundtrip(McpProtocol& p, const char* body, McpOutcome expect) {
  std::string out;
  TEST_ASSERT_EQUAL((int)expect, (int)p.handle(body, strlen(body), out));
  JsonDocument d;
  if (expect == McpOutcome::kJson) TEST_ASSERT_TRUE(deserializeJson(d, out) == DeserializationError::Ok);
  return d;
}

void setUp() {}
void tearDown() {}

void test_initialize_echoes_known_version() {  // 2025-03-26 echoed back
  auto p = makeServer();
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26"}})", McpOutcome::kJson);
  TEST_ASSERT_EQUAL_STRING("2025-03-26", d["result"]["protocolVersion"]);
  TEST_ASSERT_EQUAL_STRING("stackchan", d["result"]["serverInfo"]["name"]);
  TEST_ASSERT_EQUAL(1, (int)d["id"]);
}
void test_initialize_unknown_version_responds_latest() {  // -> 2025-06-18
  auto p = makeServer();
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2099-01-01"}})", McpOutcome::kJson);
  TEST_ASSERT_EQUAL_STRING("2025-06-18", d["result"]["protocolVersion"]);
}
void test_notification_gets_202_even_when_unknown() {
  auto p = makeServer(); std::string out;
  TEST_ASSERT_EQUAL((int)McpOutcome::kNotification,
    (int)p.handle(R"({"jsonrpc":"2.0","method":"notifications/cancelled"})", 58, out));
  TEST_ASSERT_EQUAL(0, (int)out.size());
}
void test_tools_list_contains_schema() {
  auto p = makeServer();
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})", McpOutcome::kJson);
  TEST_ASSERT_EQUAL_STRING("echo", d["result"]["tools"][0]["name"]);
  TEST_ASSERT_EQUAL_STRING("object", d["result"]["tools"][0]["inputSchema"]["type"]);
}
void test_tools_call_dispatches() {
  auto p = makeServer();
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"echo","arguments":{"v":"hi"}}})", McpOutcome::kJson);
  TEST_ASSERT_EQUAL_STRING("got:hi", d["result"]["content"][0]["text"]);
  TEST_ASSERT_FALSE(d["result"]["isError"] | false);
}
void test_tool_failure_is_isError_not_protocol_error() {
  auto p = makeServer(false);
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"echo","arguments":{"v":"x"}}})", McpOutcome::kJson);
  TEST_ASSERT_TRUE(d["result"]["isError"] | false);
}
void test_unknown_tool_is_minus32602() {
  auto p = makeServer();
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"nope"}})", McpOutcome::kJson);
  TEST_ASSERT_EQUAL(-32602, (int)d["error"]["code"]);
}
void test_ping_returns_empty_object() {
  auto p = makeServer();
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":7,"method":"ping"})", McpOutcome::kJson);
  TEST_ASSERT_TRUE(d["result"].is<JsonObjectConst>());
}
void test_unknown_method_with_id_is_minus32601() {
  auto p = makeServer();
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":8,"method":"resources/list"})", McpOutcome::kJson);
  TEST_ASSERT_EQUAL(-32601, (int)d["error"]["code"]);
}
void test_malformed_json_is_minus32700_with_null_id() {
  auto p = makeServer();
  auto d = roundtrip(p, "{nope", McpOutcome::kJson);
  TEST_ASSERT_EQUAL(-32700, (int)d["error"]["code"]);
  TEST_ASSERT_TRUE(d["id"].isNull());
}
void test_batch_array_is_minus32600() {
  auto p = makeServer();
  auto d = roundtrip(p, R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])", McpOutcome::kJson);
  TEST_ASSERT_EQUAL(-32600, (int)d["error"]["code"]);
}
void test_string_id_echoed_as_string() {
  auto p = makeServer();
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":"abc","method":"ping"})", McpOutcome::kJson);
  TEST_ASSERT_TRUE(d["id"].is<const char*>());
  TEST_ASSERT_EQUAL_STRING("abc", d["id"]);
}
void test_invalid_prefix_becomes_minus32602() {
  McpProtocol p;
  p.addTool({"t", "d", R"({"type":"object"})",
    [](JsonVariantConst, std::string& r){ r = "invalid:yaw out of range"; return false; }});
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"t"}})", McpOutcome::kJson);
  TEST_ASSERT_EQUAL(-32602, (int)d["error"]["code"]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_initialize_echoes_known_version);
  RUN_TEST(test_initialize_unknown_version_responds_latest);
  RUN_TEST(test_notification_gets_202_even_when_unknown);
  RUN_TEST(test_tools_list_contains_schema);
  RUN_TEST(test_tools_call_dispatches);
  RUN_TEST(test_tool_failure_is_isError_not_protocol_error);
  RUN_TEST(test_unknown_tool_is_minus32602);
  RUN_TEST(test_ping_returns_empty_object);
  RUN_TEST(test_unknown_method_with_id_is_minus32601);
  RUN_TEST(test_malformed_json_is_minus32700_with_null_id);
  RUN_TEST(test_batch_array_is_minus32600);
  RUN_TEST(test_string_id_echoed_as_string);
  RUN_TEST(test_invalid_prefix_becomes_minus32602);
  return UNITY_END();
}
