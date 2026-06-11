# On-device MCP Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Serve MCP (Streamable HTTP, stateless JSON mode) from the robot at `POST /mcp` with tools `say`, `set_expression`, `move_head`, `get_status`.

**Architecture:** Two layers. `McpProtocol` is a pure C++ module (ArduinoJson + std::string only, no Arduino headers) that parses JSON-RPC, negotiates protocol version, dispatches to a registered tool table, and renders responses — unit-tested in `[env:native]`. `McpServer` is the thin Arduino layer: AsyncWebServer route, 4 KB body cap with connection close, per-request buffers, Origin guard, PSRAM allocation, and the firmware tool handlers that push into ControlBridge.

**Tech Stack:** ArduinoJson v7 (already a dep; works on host), ESPAsyncWebServer (existing), Unity tests (existing native env).

**Spec:** `docs/superpowers/specs/2026-06-10-mcp-server-design.md` (rev 2). The spec is normative; this plan implements all of §3–§5.

---

### Task 1: ControlBridge — expression field + bigger text buffer

**Files:**
- Modify: `src/app/ControlBridge.h` (struct ControlCommand, pushSay decl)
- Modify: `src/app/ControlBridge.cpp` (pushSay, SAY dispatch)

- [ ] **Step 1: Widen the command struct**

In `ControlBridge.h`, change the struct fields:

```cpp
struct ControlCommand {
    CtrlCmd type;
    int     a = 0;
    int     b = 0;
    char    text[256];   // was 192 — MCP say allows 240 UTF-8 bytes + NUL headroom
    char    expr[12];    // expression tag for SAY (e.g. "happy"); empty = happy
};
```

and the declaration: `bool pushSay(const char* text, const char* expr = nullptr);`

- [ ] **Step 2: Update pushSay and the SAY dispatch**

In `ControlBridge.cpp`:

```cpp
bool ControlBridge::pushSay(const char* text, const char* expr) {
    ControlCommand c{};
    c.type = CtrlCmd::SAY;
    strncpy(c.text, text ? text : "", sizeof(c.text) - 1);
    strncpy(c.expr, (expr && expr[0]) ? expr : "happy", sizeof(c.expr) - 1);
    return push(c);
}
```

and in `tick()`'s SAY case replace the hardcoded `"happy"`:

```cpp
            case CtrlCmd::SAY: {
                if (!requestExternalSpeak(String(c.text), c.expr)) {
                    Serial.println("[ControlBridge] say ignored (FSM busy)");
                }
                break;
            }
```

- [ ] **Step 3: Build**

Run: `pio run -e cores3_linux 2>&1 | tail -3` — Expected: SUCCESS. (Existing caller in CaptivePortal passes one arg; default param keeps it compiling.)

- [ ] **Step 4: Commit**

```bash
git add src/app/ControlBridge.h src/app/ControlBridge.cpp
git commit -m "feat(control): SAY carries an expression tag; text buffer 192->256"
```

---

### Task 2: McpProtocol — pure protocol core, TDD in [env:native]

**Files:**
- Create: `src/services/McpProtocol.h`
- Create: `src/services/McpProtocol.cpp`
- Create: `test/test_mcp_protocol/test_mcp_protocol.cpp`
- Modify: `platformio.ini` ([env:native]: add ArduinoJson to lib_deps, add module to build_src_filter)

- [ ] **Step 1: Define the interface (header)**

`src/services/McpProtocol.h`:

```cpp
#pragma once
#include <ArduinoJson.h>
#include <functional>
#include <string>
#include <vector>

namespace stkchan {

// One MCP tool: metadata + JSON Schema (as a literal string) + handler.
// Handler returns true on success; fills `result` (text content) either way —
// on false, `result` is the human-readable error (rendered isError: true).
struct McpTool {
  const char* name;
  const char* description;
  const char* schemaJson;  // JSON Schema object literal for inputParams
  std::function<bool(JsonVariantConst args, std::string& result)> handler;
};

enum class McpOutcome {
  kJson,          // body holds a JSON-RPC envelope -> HTTP 200 application/json
  kNotification,  // no response body -> HTTP 202
};

class McpProtocol {
 public:
  void addTool(const McpTool& tool);
  // Parse one POST body, produce response. `alloc` backs the JsonDocuments
  // (nullptr = ArduinoJson default heap allocator; device passes PSRAM).
  McpOutcome handle(const char* body, size_t len, std::string& out,
                    ArduinoJson::Allocator* alloc = nullptr);

 private:
  std::vector<McpTool> tools_;
};

}  // namespace stkchan
```

- [ ] **Step 2: Write the failing tests**

`test/test_mcp_protocol/test_mcp_protocol.cpp` — Unity, mirroring the existing test style. Cover, with exact assertions (parse `out` with ArduinoJson in the test):

```cpp
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
  return UNITY_END();
}
```

(Match the existing tests' main()/setUp signature style — check `test/test_response_parser/test_response_parser.cpp` first and mirror it.)

- [ ] **Step 3: Wire the native env**

In `platformio.ini` `[env:native]`: append `+<services/McpProtocol.cpp>` to `build_src_filter` and add

```ini
lib_deps = bblanchon/ArduinoJson@^7.2.0
```

- [ ] **Step 4: Run tests — expect FAIL (no implementation)**

Run: `pio test -e native -f test_mcp_protocol 2>&1 | tail -5` — Expected: build error (McpProtocol.cpp missing) or link failure.

- [ ] **Step 5: Implement McpProtocol.cpp**

Implementation contract (full file, ~150 lines):

- `handle()`: create `JsonDocument req(alloc)` (when alloc non-null) / `JsonDocument req` otherwise; `deserializeJson(req, body, len)`.
  - Parse error → render `{"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"parse error"}}` → kJson.
  - `req.is<JsonArray>()` → error −32600 "batch not supported", id null → kJson.
  - No `id` key (`!req["id"].is<JsonVariantConst>()` AND !req.containsKey("id")) → out cleared → kNotification. (Note: `id: null` from a parse-error response is OUR output; an incoming explicit `"id":null` is treated as a notification too — acceptable.)
  - Build `JsonDocument resp(alloc)`; `resp["jsonrpc"]="2.0"`; copy id verbatim: `resp["id"] = req["id"]` (ArduinoJson preserves type).
  - Method switch on `const char* m = req["method"] | ""`:
    - `initialize`: known list {"2024-11-05","2025-03-26","2025-06-18"} → echo, else "2025-06-18". result: protocolVersion, `capabilities.tools` = empty object, `serverInfo.name`="stackchan", `.version`="1.0.0".
    - `tools/list`: for each tool: name, description, `inputSchema` = parsed from `schemaJson` (deserialize into a nested doc and `set()` it).
    - `tools/call`: find tool by `params.name` — missing → −32602 "unknown tool". Call handler with `params.arguments`. Render `result.content[0] = {type:"text", text:<result>}`, `result.isError` = !ok.
    - `ping`: `resp["result"].to<JsonObject>()`.
    - missing method key → −32600; anything else → −32601.
  - `serializeJson(resp, out)` (serialize to std::string works on host and device) → kJson.
- Param validation lives in the firmware handlers (Task 3), EXCEPT unknown tool (−32602 here). Handlers signal validation failures by returning false with a message starting `"invalid:"` — `handle()` detects that prefix and renders a −32602 protocol error (message = rest of string) instead of isError content.

Add one more test for that last behavior before implementing it:

```cpp
void test_invalid_prefix_becomes_minus32602() {
  McpProtocol p;
  p.addTool({"t", "d", R"({"type":"object"})",
    [](JsonVariantConst, std::string& r){ r = "invalid:yaw out of range"; return false; }});
  auto d = roundtrip(p, R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"t"}})", McpOutcome::kJson);
  TEST_ASSERT_EQUAL(-32602, (int)d["error"]["code"]);
}
```

- [ ] **Step 6: Run tests — expect 13 PASS**

Run: `pio test -e native 2>&1 | tail -6` — Expected: all suites pass (13 new + 22 existing).

- [ ] **Step 7: Commit**

```bash
git add src/services/McpProtocol.* test/test_mcp_protocol platformio.ini
git commit -m "feat(mcp): protocol core - JSON-RPC dispatch, version negotiation, tools (TDD)"
```

---

### Task 3: McpServer — transport layer + firmware tools

**Files:**
- Create: `src/services/McpServer.h`
- Create: `src/services/McpServer.cpp`
- Modify: `src/services/CaptivePortal.cpp` (attach call in the LAN-server section, + include)

- [ ] **Step 1: Header**

```cpp
#pragma once
class AsyncWebServer;
namespace stkchan {
// Registers POST /mcp (+405 GET) on the given server. Call once, LAN mode only.
void mcpAttach(AsyncWebServer& server);
}
```

- [ ] **Step 2: Implementation**

`McpServer.cpp` essentials (complete file in implementation; key parts):

```cpp
// PSRAM allocator for all MCP JsonDocuments (spec §3 Memory).
struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return ps_malloc(n); }
  void  deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override { return ps_realloc(p, n); }
};
static PsramAllocator g_psramAlloc;
static McpProtocol g_mcp;          // tools registered in mcpAttach()

// Per-request body buffer hung on request->_tempObject (NOT a global).
struct McpBody { char* buf; size_t len; bool rejected; };
```

Body handler: first chunk allocates `McpBody` + `ps_malloc(min(total,4096)+1)`; if `total > 4096` OR running `index+len > 4096` → `request->send(413)`, `rejected=true`, `request->client()->close()`. Completion handler (the request callback): if rejected → return; Origin check (header absent → OK; present → host must be RFC1918/`localhost`/`*.local` else `403`); run `g_mcp.handle(buf, len, out, &g_psramAlloc)`; kNotification → `request->send(202)`; kJson → `request->send(200, "application/json", out.c_str())`. Free buffer in `request->onDisconnect` or after send (set `_tempObject=nullptr`; AsyncWebServer frees `_tempObject` with free() on destruct — allocate the struct with malloc and the buffer separately, freeing the buffer ourselves after send).

Tool registration in `mcpAttach` (handlers run on async_tcp; ControlBridge push + snapshot reads only):

```cpp
g_mcp.addTool({"say",
  "Make the robot speak text aloud (TTS) with a facial expression. "
  "Returns queued; speech may be skipped if a voice turn starts first.",
  R"({"type":"object","properties":{
      "text":{"type":"string","description":"<=240 bytes UTF-8"},
      "expression":{"type":"string","enum":["neutral","happy","sad","angry","doubt","sleepy"]}},
      "required":["text"]})",
  [](JsonVariantConst a, std::string& r) {
    const char* t = a["text"] | (const char*)nullptr;
    if (!t)                 { r = "invalid:text is required";        return false; }
    if (strlen(t) > 240)    { r = "invalid:text exceeds 240 bytes";  return false; }
    if (currentState() != State::IDLE) { r = "robot is mid-conversation - try again shortly"; return false; }
    if (!controlBridge.pushSay(t, a["expression"] | "happy")) { r = "queue full"; return false; }
    r = "queued"; return true; }});
```

`set_expression`: validate tag against the same 6-enum (strcmp loop) → `invalid:` on bad tag → `pushExpression`. `move_head`: require integer yaw∈[−45,45], pitch∈[0,25] → `invalid:yaw out of range -45..45` etc. → `pushServo`; result `"ok yaw=<y> pitch=<p>"`. `get_status`: build a small JSON string: `present` (presence.present()), `fsm` ((int)currentState()), `yaw/pitch` (servos.currentYaw/Pitch()), `volume` (audio.getVolume()), `battPct` (M5.Power.getBatteryLevel()), `heapFree` (ESP.getFreeHeap()), `rssi` (WiFi.RSSI()).

GET /mcp → `server.on("/mcp", HTTP_GET, [](r){ r->send(405); })`.

- [ ] **Step 3: Attach from CaptivePortal**

In the LAN-mode server-start path (where `/api/control/*` registration happens), add `#include "McpServer.h"` and `mcpAttach(g_server);` — registered BEFORE `serveStatic` (same constraint as the API routes).

- [ ] **Step 4: Build**

Run: `pio run -e cores3_linux 2>&1 | tail -3` — Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/services/McpServer.* src/services/CaptivePortal.cpp
git commit -m "feat(mcp): /mcp Streamable HTTP endpoint + say/set_expression/move_head/get_status tools"
```

---

### Task 4: On-device curl verification

**Files:** none (runtime).

- [ ] **Step 1: Flash** — USB (COM16, esptool via powershell, artifacts at `\\wsl.localhost\...`) or OTA (`espota.py -i 192.168.1.121 -p 3232 -a gr8ful`). Remember the app0/ota_1 selector gotcha if USB.

- [ ] **Step 2: Protocol pass** (expect per spec §6; `B=http://192.168.1.121/mcp`):

```bash
curl -si -X POST $B -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}'   # 200, echoes version
curl -si -X POST $B -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'                                      # 202 empty
curl -si -X POST $B -d '{"jsonrpc":"2.0","method":"notifications/cancelled"}'                                        # 202 (NOT -32601)
curl -si -X POST $B -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'                                              # 200, 4 tools
curl -si -X POST $B -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"say","arguments":{"text":"MCP is alive!","expression":"happy"}}}'  # robot speaks
curl -si -X POST $B -d '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"move_head","arguments":{"yaw":30,"pitch":20}}}'                    # head moves
curl -si -X POST $B -d '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"move_head","arguments":{"yaw":99,"pitch":0}}}'                     # -32602
curl -si -X POST $B -d '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"get_status"}}'               # 200, status JSON
curl -si -X POST $B -d '{nope'                                                                                       # 200, -32700
curl -si -X POST $B -d "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"say\",\"arguments\":{\"text\":\"$(python3 -c 'print("x"*300)')\"}}}"  # -32602
curl -si -X GET  $B                                                                                                  # 405
head -c 8000 /dev/zero | tr '\0' 'a' | curl -si -X POST $B --data-binary @-                                          # 413
curl -si -X POST $B -d '{"jsonrpc":"2.0","id":"abc","method":"ping"}'                                                # id "abc" as string
```

- [ ] **Step 3: Heap check** — `curl http://192.168.1.121/api/debug/presence` before/after the pass; device stays on WiFi, heap stable.

---

### Task 5: Claude Code end-to-end + PR

- [ ] **Step 1:** `claude mcp add --transport http stackchan http://192.168.1.121/mcp` then in a Claude session: list tools, `get_status`, `say` "Hello from Claude", `move_head`, `set_expression` — owner observes the robot. Exercise Esc mid-call once (cancellation → 202 path, no error log).
- [ ] **Step 2:** Push branch `feat/mcp-server`, open PR with summary + curl/E2E results, hand off for merge decision.

## Self-review
- Spec coverage: §3 dispatch order → Task 2 tests (notification/batch/malformed/id-echo); §3 status policy + cap + Origin + headers → Task 3 Step 2 + Task 4 curls; §4 four tools incl. `say` semantics + ControlBridge prerequisite → Tasks 1, 3; §5 → Task 2 tests; §6 → Tasks 4–5. `MCP-Protocol-Version`/`Accept` ignored = no code (documented). ✓
- No placeholders: every step has code or exact commands. ✓
- Type consistency: `McpTool.handler(JsonVariantConst, std::string&)` used identically in Tasks 2/3; `pushSay(text, expr)` matches Task 1. ✓
