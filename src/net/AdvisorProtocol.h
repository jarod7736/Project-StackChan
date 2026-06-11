#pragma once
#include <string>
#include <vector>
#include <ArduinoJson.h>

// Pure, hardware-free helpers for the Anthropic Messages "advisor tool" — a
// server-side feature that lets a fast EXECUTOR model consult a stronger
// ADVISOR model mid-generation (https://docs.anthropic.com advisor-tool docs).
//
// The device speaks the OpenAI-compatible wire format to its casual/agent
// backends; the advisor tool only exists on Anthropic's /v1/messages API, so
// this module owns the request-body construction, the executor/advisor
// compatibility table, and the response parsing for that one path. It has no
// Arduino/M5/HTTP deps -> compiled and unit-tested in [env:native]. The
// hardware glue (TLS POST + NVS config) lives in net/ChatClient.

namespace stkchan {

// Required beta header value and tool `type` for the advisor tool.
inline constexpr const char* kAdvisorBeta     = "advisor-tool-2026-03-01";
inline constexpr const char* kAdvisorToolType = "advisor_20260301";
inline constexpr const char* kAdvisorToolName = "advisor";

// Knobs on the advisor tool definition. Defaults mean "unset" (server default).
struct AdvisorConfig {
  std::string advisorModel;        // e.g. "claude-opus-4-8" (required)
  int  maxTokens = 0;              // 0 = unset; else cap advisor output (>=1024)
  int  maxUses   = 0;              // 0 = unset; else per-request advisor-call cap
  bool caching   = false;          // advisor-side prompt caching across calls
  std::string cacheTtl = "5m";     // "5m" | "1h" — only emitted when caching
};

// One conversation turn in the Anthropic Messages format (plain-text content).
struct AdvisorTurn {
  std::string role;     // "user" | "assistant"
  std::string content;
};

// True if `executor`/`advisor` form a supported pair (advisor at least as
// capable as the executor), per the documented compatibility table.
bool isValidModelPair(const std::string& executor, const std::string& advisor);

// Which advisor result variant the executor's reply carried.
enum class AdvisorVariant {
  kNone,      // the executor never invoked the advisor
  kResult,    // advisor_result          — plaintext advice
  kRedacted,  // advisor_redacted_result — encrypted advice (opaque to us)
  kError,     // advisor_tool_result_error
};

struct AdvisorOutcome {
  bool ok = false;                        // a usable assistant message parsed
  std::string assistantText;              // concatenated executor text blocks
  AdvisorVariant variant = AdvisorVariant::kNone;
  std::string adviceText;                 // advisor_result.text (kResult only)
  std::string stopReason;                 // advisor sub-call stop_reason if set
  std::string errorCode;                  // advisor_tool_result_error.error_code
};

// Serializes an Anthropic /v1/messages request body pairing `executorModel`
// with the advisor tool from `cfg`. `system` may be empty; `turns` is the
// replayed history plus the current user turn. `maxTokens` bounds EXECUTOR
// output only (the advisor cap lives in cfg.maxTokens).
std::string buildAdvisorRequestBody(const std::string& executorModel,
                                    const std::string& system,
                                    const std::vector<AdvisorTurn>& turns,
                                    int maxTokens,
                                    const AdvisorConfig& cfg);

// Parses an Anthropic /v1/messages response body. On success `ok` is true and
// `assistantText` holds the executor's visible reply; advisor fields describe
// the sub-inference (if any). A failed advisor sub-call does NOT fail the
// parse — the executor still answers — so check `variant`/`errorCode`.
AdvisorOutcome parseAdvisorResponse(const std::string& body);

}  // namespace stkchan
