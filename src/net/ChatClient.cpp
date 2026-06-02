#include "net/ChatClient.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "services/NvsStore.h"

namespace stkchan {

ChatClient chat;

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

bool ChatClient::send(const String& userMsg, String& out) {
  out = "";
  String host  = nvs.getString(kNvsChatHost, "");
  String model = nvs.getString(kNvsChatModel, kDefaultChatModel);
  if (host.isEmpty()) return false;

  HTTPClient http;
  String url = host + "/api/chat";
  if (!http.begin(url)) {
    http.end();  // defensive — HTTPClient may have partial state (T9/T11 pattern)
    return false;
  }
  http.setTimeout(kChatTimeoutMs);
  http.addHeader("Content-Type", "application/json");

  JsonDocument req;
  req["model"] = model;
  req["stream"] = false;
  JsonArray msgs = req["messages"].to<JsonArray>();
  if (!system_.isEmpty()) {
    JsonObject sys = msgs.add<JsonObject>();
    sys["role"] = "system";
    sys["content"] = system_;
  }
  // Replay history in chronological order.
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

  String body;
  serializeJson(req, body);
  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[ChatClient] ERR: HTTP %d\n", code);
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    Serial.println("[ChatClient] ERR: JSON parse failed");
    return false;
  }
  out = doc["message"]["content"].as<String>();
  if (out.isEmpty()) return false;

  push_("user", userMsg);
  push_("assistant", out);
  return true;
}

}  // namespace stkchan
