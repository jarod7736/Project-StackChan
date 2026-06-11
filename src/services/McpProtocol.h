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
  const char* schemaJson;  // JSON Schema literal written to "inputSchema" in tools/list
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
