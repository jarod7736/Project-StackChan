#include "net/ChatClient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <utility>
#include <vector>
#include "net/AdvisorProtocol.h"
#include "services/NvsStore.h"

namespace stkchan {

ChatClient chat;

namespace {

using Header = std::pair<const char*, String>;

// Normalize a host into a full https/http URL with `path` appended. Bare hosts
// default to http:// (LAN is the common case). Trailing slash safe.
String buildUrl(String host, const char* path) {
  while (host.endsWith("/")) host.remove(host.length() - 1);
  if (!host.startsWith("http://") && !host.startsWith("https://")) {
    host = "http://" + host;
  }
  return host + path;
}

// POST `body` to `url` with the given headers. Returns HTTP status; fills
// `resp` with the body on success. Uses a STACK-allocated WiFi client (the
// Jarvis LLMClient lesson: heap-new'ing the secure client caused a
// LoadProhibited crash). HTTP/1.0 for clean connection-close behavior.
int httpPost(const String& url, const String& body,
             const std::vector<Header>& headers, String& resp) {
  HTTPClient http;
  http.setTimeout(kChatTimeoutMs);
  http.useHTTP10(true);

  auto run = [&](void) -> int {
    http.addHeader("Content-Type", "application/json");
    for (const auto& h : headers) {
      if (h.second.length()) http.addHeader(h.first, h.second);
    }
    int code = http.POST(body);
    if (code == 200) resp = http.getString();
    http.end();
    return code;
  };

  if (url.startsWith("https://")) {
    WiFiClientSecure secure;
    secure.setInsecure();  // cert pinning is Phase 2
    if (!http.begin(secure, url)) { http.end(); return -1; }
    return run();
  } else {
    WiFiClient plain;
    if (!http.begin(plain, url)) { http.end(); return -1; }
    return run();
  }
}

}  // namespace

void ChatClient::clearHistory() {
  for (auto& t : ring_) { t.role = ""; t.content = ""; }
  head_ = 0;
  count_ = 0;
}

void ChatClient::push_(const String& role, const String& content) {
  ring_[head_] = Turn{role, content};
  head_ = (head_ + 1) % ring_.size();
  if (count_ < ring_.size()) ++count_;
}

bool ChatClient::send(const String& userMsg, String& out, bool brainMode) {
  out = "";

  // Casual turns route through the Anthropic advisor path when an API key is
  // provisioned (adv_key). Brain turns always go to the oc-personal agent.
  if (!brainMode && nvs.getString(kNvsAdvKey, "").length()) {
    return sendViaAdvisor_(userMsg, out);
  }

  // Pick endpoint + model + auth by mode.
  String host, model, bearer;
  if (brainMode) {
    host   = nvs.getString(kNvsBrainHost, kDefaultBrainHost);
    model  = kOcPersonalModel;
    bearer = nvs.getString(kNvsBrainKey, "");
  } else {
    host   = nvs.getString(kNvsChatHost, "");
    model  = nvs.getString(kNvsChatModel, kDefaultChatModel);
  }
  if (host.isEmpty()) {
    Serial.printf("[ChatClient] no %s host configured\n",
                  brainMode ? "brain" : "chat");
    return false;
  }

  // Build the OpenAI-compatible request body.
  JsonDocument req;
  req["model"]      = model;
  req["stream"]     = false;
  req["max_tokens"] = kChatMaxTokens;
  JsonArray msgs = req["messages"].to<JsonArray>();

  if (brainMode) {
    // Minimal one-shot: the agent owns its own system prompt + context, and
    // the <speech>/<expr> format would fight its tool output.
    JsonObject usr = msgs.add<JsonObject>();
    usr["role"]    = "user";
    usr["content"] = userMsg;
  } else {
    // Casual: persona system prompt + replayed history + user turn.
    if (!system_.isEmpty()) {
      JsonObject sys = msgs.add<JsonObject>();
      sys["role"]    = "system";
      sys["content"] = system_;
    }
    size_t idx = (head_ + ring_.size() - count_) % ring_.size();
    for (size_t i = 0; i < count_; ++i) {
      const Turn& t = ring_[(idx + i) % ring_.size()];
      JsonObject m = msgs.add<JsonObject>();
      m["role"]    = t.role;
      m["content"] = t.content;
    }
    JsonObject usr = msgs.add<JsonObject>();
    usr["role"]    = "user";
    usr["content"] = userMsg;
  }

  String body, resp;
  serializeJson(req, body);
  String url = buildUrl(host, "/v1/chat/completions");
  Serial.printf("[ChatClient] POST %s model=%s%s\n", url.c_str(), model.c_str(),
                brainMode ? " (brain)" : "");

  std::vector<Header> headers;
  if (bearer.length()) headers.push_back({"Authorization", "Bearer " + bearer});
  int code = httpPost(url, body, headers, resp);
  if (code != 200) {
    Serial.printf("[ChatClient] ERR: HTTP %d\n", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    Serial.println("[ChatClient] ERR: JSON parse failed");
    return false;
  }
  // OpenAI-compatible shape (both Ollama /v1 and the oc-personal runner).
  out = doc["choices"][0]["message"]["content"].as<String>();
  if (out.isEmpty()) {
    Serial.println("[ChatClient] ERR: empty choices[0].message.content");
    return false;
  }

  // Only casual mode keeps conversational history; brain queries are one-shot.
  if (!brainMode) {
    push_("user", userMsg);
    push_("assistant", out);
  }
  return true;
}

bool ChatClient::sendViaAdvisor_(const String& userMsg, String& out) {
  String host    = nvs.getString(kNvsAdvHost,  kDefaultAdvisorHost);
  String apiKey  = nvs.getString(kNvsAdvKey,   "");
  String exec    = nvs.getString(kNvsAdvExec,  kDefaultAdvisorExec);
  String advisor = nvs.getString(kNvsAdvModel, kDefaultAdvisorModel);

  // The API rejects an unsupported executor/advisor pair with a 400; catch it
  // here and fail fast (the caller speaks kErrChatFailed) rather than burn a
  // round-trip.
  std::string execS(exec.c_str()), advisorS(advisor.c_str());
  if (!isValidModelPair(execS, advisorS)) {
    Serial.printf("[Advisor] unsupported pair exec=%s advisor=%s\n",
                  exec.c_str(), advisor.c_str());
    return false;
  }

  // System prompt + replayed casual history + the current user turn, in the
  // Anthropic Messages shape (system is a top-level field, not a message).
  std::vector<AdvisorTurn> turns;
  size_t idx = (head_ + ring_.size() - count_) % ring_.size();
  for (size_t i = 0; i < count_; ++i) {
    const Turn& t = ring_[(idx + i) % ring_.size()];
    turns.push_back({std::string(t.role.c_str()), std::string(t.content.c_str())});
  }
  turns.push_back({"user", std::string(userMsg.c_str())});

  AdvisorConfig cfg;
  cfg.advisorModel = advisorS;
  cfg.maxTokens    = kAdvisorMaxTokens;
  std::string body = buildAdvisorRequestBody(
      execS, std::string(system_.c_str()), turns, kChatMaxTokens, cfg);

  String url = buildUrl(host, "/v1/messages");
  Serial.printf("[Advisor] POST %s exec=%s advisor=%s\n", url.c_str(),
                exec.c_str(), advisor.c_str());

  std::vector<Header> headers = {
      {"x-api-key", apiKey},
      {"anthropic-version", String(kAnthropicVersion)},
      {"anthropic-beta", String(kAdvisorBeta)},
  };
  String resp;
  int code = httpPost(url, String(body.c_str()), headers, resp);
  if (code != 200) {
    Serial.printf("[Advisor] ERR: HTTP %d\n", code);
    return false;
  }

  AdvisorOutcome r = parseAdvisorResponse(std::string(resp.c_str()));
  if (!r.ok) {
    Serial.println("[Advisor] ERR: no usable assistant text in response");
    return false;
  }
  switch (r.variant) {
    case AdvisorVariant::kResult:
      Serial.printf("[Advisor] advice (%u chars): %.120s\n",
                    (unsigned)r.adviceText.size(), r.adviceText.c_str());
      break;
    case AdvisorVariant::kRedacted:
      Serial.println("[Advisor] advice returned encrypted");
      break;
    case AdvisorVariant::kError:
      // A failed sub-call doesn't fail the turn — the executor still answered.
      Serial.printf("[Advisor] sub-call error: %s\n", r.errorCode.c_str());
      break;
    case AdvisorVariant::kNone:
      break;
  }

  out = String(r.assistantText.c_str());
  push_("user", userMsg);
  push_("assistant", out);
  return true;
}

}  // namespace stkchan
