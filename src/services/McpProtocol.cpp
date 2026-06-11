// McpProtocol.cpp — pure JSON-RPC MCP protocol core (no Arduino headers).
#include "McpProtocol.h"

#include <cstring>

namespace stkchan {

namespace {

// Known protocol versions accepted by this server.
static const char* const kKnownVersions[] = {
    "2024-11-05",
    "2025-03-26",
    "2025-06-18",
};
static const char* const kLatestVersion = "2025-06-18";

bool isKnownVersion(const char* v) {
  if (!v) return false;
  for (auto kv : kKnownVersions) {
    if (strcmp(v, kv) == 0) return true;
  }
  return false;
}

// Serialize an error envelope into `out`.
void renderError(JsonDocument& resp, int code, const char* message) {
  resp["error"]["code"] = code;
  resp["error"]["message"] = message;
}

// Helper: write `serializeJson` to a std::string.
void toStr(JsonDocument& doc, std::string& out) {
  out.clear();
  serializeJson(doc, out);
}

}  // namespace

void McpProtocol::addTool(const McpTool& tool) {
  tools_.push_back(tool);
}

McpOutcome McpProtocol::handle(const char* body, size_t len, std::string& out,
                               ArduinoJson::Allocator* alloc) {
  out.clear();

  // -----------------------------------------------------------------------
  // 1. Parse the request JSON.
  // -----------------------------------------------------------------------
  JsonDocument req;
  // ArduinoJson v7: if alloc is provided, pass it; otherwise use default.
  // Unfortunately JsonDocument's constructor that accepts Allocator* is the
  // same signature — we duplicate the deserialize call to keep it clean.
  DeserializationError err;
  if (alloc) {
    JsonDocument reqAlloc(alloc);
    err = deserializeJson(reqAlloc, body, len);
    if (err) {
      // Parse error — build minimal error doc with the custom allocator.
      JsonDocument resp(alloc);
      resp["jsonrpc"] = "2.0";
      resp["id"] = nullptr;
      renderError(resp, -32700, "parse error");
      toStr(resp, out);
      return McpOutcome::kJson;
    }
    // Move parsed data into req by re-parsing (deep copy via set).
    // Simpler: just fall through to the non-alloc path for the logic — we
    // already have reqAlloc. Use a reference trick.
    //
    // Actually the cleanest approach: always parse into the alloc doc and
    // alias it through a JsonVariant for reading, then build resp with alloc.
    // Restructure: goto a shared lambda won't work cleanly. Instead just use
    // an inner lambda via std::function for the response-building phase.
    //
    // Simplest correct approach: parse into local doc, build resp with alloc
    // if available. Repeat deserialize for req only once.
    req = std::move(reqAlloc);
  } else {
    err = deserializeJson(req, body, len);
    if (err) {
      JsonDocument resp;
      resp["jsonrpc"] = "2.0";
      resp["id"] = nullptr;
      renderError(resp, -32700, "parse error");
      toStr(resp, out);
      return McpOutcome::kJson;
    }
  }

  // -----------------------------------------------------------------------
  // 2. Reject batch (top-level array).
  // -----------------------------------------------------------------------
  if (req.is<JsonArray>()) {
    JsonDocument resp;
    if (alloc) resp = JsonDocument(alloc);
    resp["jsonrpc"] = "2.0";
    resp["id"] = nullptr;
    renderError(resp, -32600, "batch not supported");
    toStr(resp, out);
    return McpOutcome::kJson;
  }

  // -----------------------------------------------------------------------
  // 3. Notification detection: no "id" key present.
  //    Explicit "id":null is also treated as notification (per spec note).
  //    ArduinoJson v7: iterate the object to check key presence without
  //    using deprecated containsKey().
  // -----------------------------------------------------------------------
  bool hasId = false;
  for (JsonPairConst kv : req.as<JsonObjectConst>()) {
    if (strcmp(kv.key().c_str(), "id") == 0 && !kv.value().isNull()) {
      hasId = true;
      break;
    }
  }
  if (!hasId) {
    // Notification — no response.
    return McpOutcome::kNotification;
  }

  // -----------------------------------------------------------------------
  // 4. Build response envelope (copy id verbatim to preserve type).
  // -----------------------------------------------------------------------
  JsonDocument resp;
  if (alloc) resp = JsonDocument(alloc);
  resp["jsonrpc"] = "2.0";
  resp["id"] = req["id"];  // ArduinoJson v7 preserves number/string type.

  // -----------------------------------------------------------------------
  // 5. Dispatch by method.
  // -----------------------------------------------------------------------
  const char* method = req["method"] | "";

  if (strcmp(method, "initialize") == 0) {
    const char* clientVer = req["params"]["protocolVersion"] | "";
    const char* echoed = isKnownVersion(clientVer) ? clientVer : kLatestVersion;
    resp["result"]["protocolVersion"] = echoed;
    resp["result"]["capabilities"]["tools"].to<JsonObject>();
    resp["result"]["serverInfo"]["name"] = "stackchan";
    resp["result"]["serverInfo"]["version"] = "1.0.0";

  } else if (strcmp(method, "tools/list") == 0) {
    JsonArray tools = resp["result"]["tools"].to<JsonArray>();
    for (const auto& t : tools_) {
      JsonObject entry = tools.add<JsonObject>();
      entry["name"] = t.name;
      entry["description"] = t.description;
      // Parse the schema string into a temporary doc, then copy it in.
      JsonDocument schemaTmp;
      if (alloc) schemaTmp = JsonDocument(alloc);
      if (deserializeJson(schemaTmp, t.schemaJson) == DeserializationError::Ok) {
        entry["inputSchema"].set(schemaTmp.as<JsonVariantConst>());
      } else {
        entry["inputSchema"].to<JsonObject>();
      }
    }

  } else if (strcmp(method, "tools/call") == 0) {
    const char* name = req["params"]["name"] | "";
    // Find the tool.
    const McpTool* found = nullptr;
    for (const auto& t : tools_) {
      if (strcmp(t.name, name) == 0) {
        found = &t;
        break;
      }
    }
    if (!found) {
      renderError(resp, -32602, "unknown tool");
    } else {
      JsonVariantConst args = req["params"]["arguments"];
      std::string result;
      bool ok = found->handler(args, result);

      // Check for "invalid:" prefix — render as protocol error, not isError.
      if (!ok && result.size() >= 8 &&
          result.compare(0, 8, "invalid:") == 0) {
        std::string msg = result.substr(8);
        renderError(resp, -32602, msg.c_str());
      } else {
        // Normal success or application-level failure.
        JsonObject content0 = resp["result"]["content"].to<JsonArray>().add<JsonObject>();
        content0["type"] = "text";
        content0["text"] = result;
        resp["result"]["isError"] = !ok;
      }
    }

  } else if (strcmp(method, "ping") == 0) {
    resp["result"].to<JsonObject>();

  } else if (method[0] == '\0') {
    // Missing method key — invalid request.
    renderError(resp, -32600, "invalid request");

  } else {
    // Unknown method with id → -32601 Method Not Found.
    renderError(resp, -32601, "method not found");
  }

  toStr(resp, out);
  return McpOutcome::kJson;
}

}  // namespace stkchan
