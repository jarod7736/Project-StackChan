#include "net/ChatClient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "services/NvsStore.h"

namespace stkchan {

ChatClient chat;

namespace {

// Normalize a host into a full https/http URL ending at /v1/chat/completions.
// Bare hosts default to http:// (LAN is the common case). Trailing slash safe.
String buildUrl(String host) {
  while (host.endsWith("/")) host.remove(host.length() - 1);
  if (!host.startsWith("http://") && !host.startsWith("https://")) {
    host = "http://" + host;
  }
  return host + "/v1/chat/completions";
}

// POST `body` to `url` with optional Bearer token. Returns HTTP status; fills
// `resp` with the body on success. Uses a STACK-allocated WiFi client (the
// Jarvis LLMClient lesson: heap-new'ing the secure client caused a
// LoadProhibited crash). HTTP/1.0 for clean connection-close behavior.
int postJson(const String& url, const String& body, const String& bearer,
             String& resp) {
  HTTPClient http;
  http.setTimeout(kChatTimeoutMs);
  http.useHTTP10(true);

  bool ok;
  if (url.startsWith("https://")) {
    WiFiClientSecure secure;
    secure.setInsecure();  // cert pinning is Phase 2
    ok = http.begin(secure, url);
    if (!ok) { http.end(); return -1; }
    http.addHeader("Content-Type", "application/json");
    if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);
    int code = http.POST(body);
    if (code == 200) resp = http.getString();
    http.end();
    return code;
  } else {
    WiFiClient plain;
    ok = http.begin(plain, url);
    if (!ok) { http.end(); return -1; }
    http.addHeader("Content-Type", "application/json");
    if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);
    int code = http.POST(body);
    if (code == 200) resp = http.getString();
    http.end();
    return code;
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
  String url = buildUrl(host);
  Serial.printf("[ChatClient] POST %s model=%s%s\n", url.c_str(), model.c_str(),
                brainMode ? " (brain)" : "");

  int code = postJson(url, body, bearer, resp);
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

}  // namespace stkchan
