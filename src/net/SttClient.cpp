#include "net/SttClient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"
#include "services/NvsStore.h"

namespace stkchan {

SttClient stt;

static constexpr const char* kBoundary = "----StackchanFormBoundary";

bool SttClient::transcribe(const uint8_t* wavData, size_t wavSize, String& out) {
  out = "";
  if (!wavData || wavSize == 0) return false;

  String url    = nvs.getString(kNvsSttUrl,
                                "https://api.openai.com/v1/audio/transcriptions");
  String model  = nvs.getString(kNvsSttModel, kDefaultSttModel);
  String apiKey = nvs.getString(kNvsOaiKey, "");
  if (apiKey.isEmpty()) {
    Serial.println("ERR: stt no oai_key in NVS");
    return false;
  }

  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  if (!http.begin(tls, url)) {
    Serial.println("ERR: stt http.begin failed");
    return false;
  }
  http.setTimeout(kSttTimeoutMs);
  http.addHeader("Authorization", "Bearer " + apiKey);
  String ctype = String("multipart/form-data; boundary=") + kBoundary;
  http.addHeader("Content-Type", ctype);

  // Build the multipart body in PSRAM. Body = preamble + WAV + epilogue.
  String preamble;
  preamble.reserve(256);
  preamble += "--"; preamble += kBoundary; preamble += "\r\n";
  preamble += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  preamble += model;
  preamble += "\r\n--"; preamble += kBoundary; preamble += "\r\n";
  preamble += "Content-Disposition: form-data; name=\"file\"; "
              "filename=\"audio.wav\"\r\n";
  preamble += "Content-Type: audio/wav\r\n\r\n";

  String epilogue = "\r\n--";
  epilogue += kBoundary;
  epilogue += "--\r\n";

  size_t bodyLen = preamble.length() + wavSize + epilogue.length();
  uint8_t* body = static_cast<uint8_t*>(ps_malloc(bodyLen));
  if (!body) {
    http.end();
    Serial.println("ERR: stt ps_malloc body failed");
    return false;
  }
  size_t off = 0;
  memcpy(body + off, preamble.c_str(), preamble.length()); off += preamble.length();
  memcpy(body + off, wavData, wavSize);                    off += wavSize;
  memcpy(body + off, epilogue.c_str(), epilogue.length()); off += epilogue.length();

  int code = http.sendRequest("POST", body, bodyLen);
  free(body);

  if (code != 200) {
    Serial.printf("ERR: stt HTTP %d\n", code);
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();

  // Response is JSON: { "text": "..." }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    Serial.println("ERR: stt JSON parse failed");
    return false;
  }
  out = doc["text"].as<String>();
  return !out.isEmpty();
}

}  // namespace stkchan
