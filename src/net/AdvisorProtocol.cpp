#include "net/AdvisorProtocol.h"

namespace stkchan {

namespace {

// The compatibility table, encoded as {executor, advisor} pairs. The advisor
// must be at least as capable as the executor; only these combinations are
// accepted by the API. Keep in sync with the advisor-tool docs.
struct ModelPair { const char* executor; const char* advisor; };
constexpr ModelPair kValidPairs[] = {
    {"claude-haiku-4-5-20251001", "claude-opus-4-8"},
    {"claude-haiku-4-5-20251001", "claude-opus-4-7"},
    {"claude-sonnet-4-6",         "claude-opus-4-8"},
    {"claude-sonnet-4-6",         "claude-opus-4-7"},
    {"claude-opus-4-6",           "claude-opus-4-8"},
    {"claude-opus-4-6",           "claude-opus-4-7"},
    {"claude-opus-4-7",           "claude-opus-4-8"},
    {"claude-opus-4-7",           "claude-opus-4-7"},
    {"claude-opus-4-8",           "claude-opus-4-8"},
    {"claude-fable-5",            "claude-fable-5"},
    {"claude-mythos-5",          "claude-mythos-5"},
};

}  // namespace

bool isValidModelPair(const std::string& executor, const std::string& advisor) {
  for (const auto& p : kValidPairs) {
    if (executor == p.executor && advisor == p.advisor) return true;
  }
  return false;
}

std::string buildAdvisorRequestBody(const std::string& executorModel,
                                    const std::string& system,
                                    const std::vector<AdvisorTurn>& turns,
                                    int maxTokens,
                                    const AdvisorConfig& cfg) {
  JsonDocument req;
  req["model"]      = executorModel;
  req["max_tokens"] = maxTokens;         // executor output cap only
  req["stream"]     = false;
  if (!system.empty()) req["system"] = system;

  // Advisor tool definition. `input` is always supplied empty by the executor;
  // we only declare the tool. Optional knobs are emitted only when set so the
  // server applies its own defaults otherwise.
  JsonArray tools = req["tools"].to<JsonArray>();
  JsonObject tool = tools.add<JsonObject>();
  tool["type"]  = kAdvisorToolType;
  tool["name"]  = kAdvisorToolName;
  tool["model"] = cfg.advisorModel;
  if (cfg.maxTokens > 0) tool["max_tokens"] = cfg.maxTokens;
  if (cfg.maxUses   > 0) tool["max_uses"]   = cfg.maxUses;
  if (cfg.caching) {
    JsonObject caching = tool["caching"].to<JsonObject>();
    caching["type"] = "ephemeral";
    caching["ttl"]  = cfg.cacheTtl;
  }

  JsonArray msgs = req["messages"].to<JsonArray>();
  for (const auto& t : turns) {
    JsonObject m = msgs.add<JsonObject>();
    m["role"]    = t.role;
    m["content"] = t.content;
  }

  std::string body;
  serializeJson(req, body);
  return body;
}

AdvisorOutcome parseAdvisorResponse(const std::string& body) {
  AdvisorOutcome out;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return out;   // ok stays false

  JsonArrayConst content = doc["content"].as<JsonArrayConst>();
  if (content.isNull()) return out;

  for (JsonObjectConst block : content) {
    const char* type = block["type"];
    if (!type) continue;

    if (std::string(type) == "text") {
      // The executor's visible output may arrive as several text blocks
      // (before and after the advisor call); concatenate them in order.
      const char* txt = block["text"];
      if (txt) out.assistantText += txt;

    } else if (std::string(type) == "advisor_tool_result") {
      JsonObjectConst inner = block["content"];
      const char* itype = inner["type"];
      if (!itype) {
        out.variant = AdvisorVariant::kError;
        continue;
      }
      std::string it = itype;
      if (it == "advisor_result") {
        out.variant = AdvisorVariant::kResult;
        if (const char* a = inner["text"]) out.adviceText = a;
        if (const char* s = inner["stop_reason"]) out.stopReason = s;
      } else if (it == "advisor_redacted_result") {
        out.variant = AdvisorVariant::kRedacted;
        if (const char* s = inner["stop_reason"]) out.stopReason = s;
      } else if (it == "advisor_tool_result_error") {
        out.variant = AdvisorVariant::kError;
        if (const char* e = inner["error_code"]) out.errorCode = e;
      } else {
        out.variant = AdvisorVariant::kError;
      }
    }
  }

  out.ok = !out.assistantText.empty();
  return out;
}

}  // namespace stkchan
