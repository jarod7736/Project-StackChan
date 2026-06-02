#include "CaptivePortal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "../config.h"
#include "NvsStore.h"

namespace stkchan {

// ============================================================================
// Internal schema — maps NVS keys to human labels, categories, types.
// Only the 17 Stack-chan spec keys are present. Adapted from Jarvis's
// ConfigSchema pattern; self-contained here so we don't need a separate
// ConfigSchema.cpp.
// ============================================================================

namespace {

// Text is functionally identical to String at the NVS layer; the UI
// emits a multi-line <textarea> for it. Used for long fields like persona.
enum class FieldType : uint8_t { String, Enum, Text };

struct EnumOption { const char* value; const char* label; };

struct ConfigField {
    const char* key;        // NVS key + JSON key (≤15 chars per spec)
    const char* label;      // Human-facing label
    const char* category;   // UI tab: wifi / chat / stt / tts / ota / persona
    FieldType   type;
    bool        sensitive;  // mask in UI ("(unchanged)" placeholder)
    const char* sdefault;   // default string value
    const EnumOption* options;
    size_t            optionCount;
};

constexpr EnumOption kTtsProvOptions[] = {
    {"openai",   "OpenAI"},
    {"eleven",   "ElevenLabs"},
    // VOICEVOX is the Phase-2 self-hosted fallback per spec §9.
    // Selecting it now persists the value; TtsClient (T9) currently
    // only routes openai/eleven, so VOICEVOX will be inactive until
    // a Phase-2 implementation lands.
    {"voicevox", "VOICEVOX (Phase 2)"},
};

// 17 spec keys — WiFi 3-slot keys are handled separately via /api/wifi/*.
// The non-WiFi keys are managed here via the schema-driven /api/config.
constexpr ConfigField kSchema[] = {
    // ── Chat ────────────────────────────────────────────────────────────
    {"chat_host",  "Chat Host URL",   "chat",    FieldType::String,
     false, "",                         nullptr, 0},
    {"chat_model", "Chat Model",      "chat",    FieldType::String,
     false, kDefaultChatModel,           nullptr, 0},

    // ── STT ─────────────────────────────────────────────────────────────
    {"stt_url",    "STT Endpoint URL","stt",     FieldType::String,
     false, "",                         nullptr, 0},
    {"stt_model",  "STT Model",       "stt",     FieldType::String,
     false, kDefaultSttModel,           nullptr, 0},
    {"oai_key",    "OpenAI API Key",  "stt",     FieldType::String,
     true,  "",                         nullptr, 0},

    // ── TTS ─────────────────────────────────────────────────────────────
    {"tts_provider","TTS Provider",   "tts",     FieldType::Enum,
     false, kDefaultTtsProv,
     kTtsProvOptions, sizeof(kTtsProvOptions) / sizeof(kTtsProvOptions[0])},
    {"tts_voice",  "TTS Voice ID",    "tts",     FieldType::String,
     false, kDefaultTtsVoice,           nullptr, 0},
    {"tts_model",  "TTS Model",       "tts",     FieldType::String,
     false, kDefaultTtsModel,           nullptr, 0},
    {"el_key",     "ElevenLabs Key",  "tts",     FieldType::String,
     true,  "",                         nullptr, 0},

    // ── OTA ─────────────────────────────────────────────────────────────
    {"ota_pass",   "OTA Password",    "ota",     FieldType::String,
     true,  "",                         nullptr, 0},

    // ── Persona ─────────────────────────────────────────────────────────
    {"persona",    "Persona Prompt",  "persona", FieldType::Text,
     false, "",                         nullptr, 0},
};
constexpr size_t kSchemaCount = sizeof(kSchema) / sizeof(kSchema[0]);

// ── WiFi 3-slot constants ────────────────────────────────────────────────────
// Slots are managed via /api/wifi/saved, /api/wifi/add, /api/wifi/remove.
// The schema above handles non-WiFi config; WiFi uses its own flat keys.
struct WifiSlot { const char* ssid; const char* psk; };
constexpr WifiSlot kWifiSlots[] = {
    {kNvsSsid1, kNvsPsk1},
    {kNvsSsid2, kNvsPsk2},
    {kNvsSsid3, kNvsPsk3},
};
constexpr size_t kWifiSlotCount = sizeof(kWifiSlots) / sizeof(kWifiSlots[0]);

// ── Server singletons ────────────────────────────────────────────────────────
AsyncWebServer g_server(80);
DNSServer      g_dns;
bool           g_running       = false;
bool           g_exit_requested = false;

// AsyncWebServer fragments POST bodies; we accumulate manually per route.
struct PendingBody { String data; };
PendingBody g_pending_config;
PendingBody g_pending_wifi;

// ── Config JSON builder ──────────────────────────────────────────────────────

void buildConfigJson(JsonDocument& doc) {
    JsonArray fields = doc["fields"].to<JsonArray>();
    for (size_t i = 0; i < kSchemaCount; ++i) {
        const auto& f = kSchema[i];
        JsonObject o = fields.add<JsonObject>();
        o["key"]       = f.key;
        o["label"]     = f.label;
        o["category"]  = f.category;
        o["sensitive"] = f.sensitive;

        switch (f.type) {
            case FieldType::String:
            case FieldType::Text: {
                o["type"] = (f.type == FieldType::Text) ? "text" : "string";
                String v = stkchan::nvs.getString(f.key, f.sdefault ? f.sdefault : "");
                o["value"] = (f.sensitive && v.length()) ? "********" : v;
                break;
            }
            case FieldType::Enum: {
                o["type"]  = "enum";
                o["value"] = stkchan::nvs.getString(f.key, f.sdefault ? f.sdefault : "");
                JsonArray opts = o["options"].to<JsonArray>();
                for (size_t j = 0; j < f.optionCount; ++j) {
                    JsonObject opt = opts.add<JsonObject>();
                    opt["value"] = f.options[j].value;
                    opt["label"] = f.options[j].label;
                }
                break;
            }
        }
    }
}

int applyConfigJson(const JsonDocument& patch) {
    int updated = 0;
    for (size_t i = 0; i < kSchemaCount; ++i) {
        const auto& f = kSchema[i];
        if (!patch[f.key].is<JsonVariantConst>()) continue;

        switch (f.type) {
            case FieldType::String:
            case FieldType::Text: {
                String v = patch[f.key].as<String>();
                if (f.sensitive && v == "********") continue;
                stkchan::nvs.putString(f.key, v);
                break;
            }
            case FieldType::Enum: {
                String v = patch[f.key].as<String>();
                bool valid = false;
                for (size_t j = 0; j < f.optionCount; ++j) {
                    if (v == f.options[j].value) { valid = true; break; }
                }
                if (!valid) return -1;
                stkchan::nvs.putString(f.key, v);
                break;
            }
        }
        ++updated;
    }
    return updated;
}

// ── Route handlers ───────────────────────────────────────────────────────────

void handleGetConfig(AsyncWebServerRequest* req) {
    JsonDocument doc;
    buildConfigJson(doc);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

void handlePostConfigBody(AsyncWebServerRequest* req,
                          uint8_t* data, size_t len, size_t idx, size_t total) {
    if (idx == 0) g_pending_config.data = "";
    g_pending_config.data.concat(reinterpret_cast<const char*>(data), len);
    if (idx + len < total) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_pending_config.data);
    g_pending_config.data = "";
    if (err) {
        req->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    int n = applyConfigJson(doc);
    if (n < 0) {
        req->send(400, "application/json", "{\"error\":\"validation failed\"}");
        return;
    }
    JsonDocument resp;
    resp["updated"] = n;
    String out; serializeJson(resp, out);
    req->send(200, "application/json", out);
}

void handleWifiScan(AsyncWebServerRequest* req) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        req->send(202, "application/json", "{\"status\":\"scanning\"}");
        return;
    }
    if (n < 0) {
        WiFi.scanNetworks(/*async=*/true);
        req->send(202, "application/json", "{\"status\":\"started\"}");
        return;
    }
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"]   = WiFi.SSID(i);
        o["rssi"]   = WiFi.RSSI(i);
        o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    String out; serializeJson(doc, out);
    WiFi.scanDelete();
    req->send(200, "application/json", out);
}

// Return the 3 WiFi slots that have a non-empty SSID.
void handleWifiSaved(AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < kWifiSlotCount; ++i) {
        String ssid = stkchan::nvs.getString(kWifiSlots[i].ssid, "");
        if (ssid.isEmpty()) continue;
        JsonObject o = arr.add<JsonObject>();
        o["ssid"]     = ssid;
        o["priority"] = static_cast<int>(i);
        o["slot"]     = static_cast<int>(i + 1);
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
}

// Add/update a WiFi credential. Fills the first empty slot; if all slots
// are full, slot 0 is overwritten (most-recently-added priority).
void handlePostWifiAddBody(AsyncWebServerRequest* req,
                           uint8_t* data, size_t len, size_t idx, size_t total) {
    if (idx == 0) g_pending_wifi.data = "";
    g_pending_wifi.data.concat(reinterpret_cast<const char*>(data), len);
    if (idx + len < total) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_pending_wifi.data);
    g_pending_wifi.data = "";
    if (err) {
        req->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    String ssid = doc["ssid"].as<String>();
    String pass = doc["password"].as<String>();
    if (ssid.isEmpty()) {
        req->send(400, "application/json", "{\"error\":\"ssid required\"}");
        return;
    }

    // Check if SSID already exists in a slot — update it in place.
    for (size_t i = 0; i < kWifiSlotCount; ++i) {
        if (stkchan::nvs.getString(kWifiSlots[i].ssid, "") == ssid) {
            stkchan::nvs.putString(kWifiSlots[i].psk, pass);
            req->send(200, "application/json", "{\"saved\":true}");
            return;
        }
    }
    // Not found — fill the first empty slot.
    for (size_t i = 0; i < kWifiSlotCount; ++i) {
        if (stkchan::nvs.getString(kWifiSlots[i].ssid, "").isEmpty()) {
            stkchan::nvs.putString(kWifiSlots[i].ssid, ssid);
            stkchan::nvs.putString(kWifiSlots[i].psk, pass);
            req->send(200, "application/json", "{\"saved\":true}");
            return;
        }
    }
    // All slots full — overwrite slot 0.
    stkchan::nvs.putString(kWifiSlots[0].ssid, ssid);
    stkchan::nvs.putString(kWifiSlots[0].psk, pass);
    req->send(200, "application/json", "{\"saved\":true}");
}

void handleWifiRemove(AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid")) {
        req->send(400, "application/json", "{\"error\":\"ssid required\"}");
        return;
    }
    String target = req->getParam("ssid")->value();
    for (size_t i = 0; i < kWifiSlotCount; ++i) {
        if (stkchan::nvs.getString(kWifiSlots[i].ssid, "") == target) {
            stkchan::nvs.eraseKey(kWifiSlots[i].ssid);
            stkchan::nvs.eraseKey(kWifiSlots[i].psk);
            req->send(200, "application/json", "{\"removed\":true}");
            return;
        }
    }
    req->send(404, "application/json", "{\"error\":\"not found\"}");
}

void handleStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["uptime_ms"]   = millis();
    doc["free_heap"]   = ESP.getFreeHeap();
    doc["mode"]        = "config";
    doc["ap_ssid"]     = WiFi.softAPSSID();
    doc["ap_clients"]  = WiFi.softAPgetStationNum();
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
}

void handleExit(AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"exiting\":true}");
    g_exit_requested = true;
}

void handleNotFound(AsyncWebServerRequest* req) {
    // Captive-portal redirect. iOS hits /hotspot-detect.html,
    // Android /generate_204, Windows /connecttest.txt — redirect all
    // to the AP root to trigger the OS-level "sign in to network" UI.
    req->redirect("http://192.168.4.1/");
}

void registerRoutes() {
    // Static UI from LittleFS.
    g_server.serveStatic("/", LittleFS, "/web/")
            .setDefaultFile("index.html");

    g_server.on("/api/config", HTTP_GET, handleGetConfig);
    g_server.on("/api/config", HTTP_POST,
                [](AsyncWebServerRequest* req) {},
                nullptr,
                handlePostConfigBody);

    g_server.on("/api/wifi/scan",   HTTP_GET,  handleWifiScan);
    g_server.on("/api/wifi/saved",  HTTP_GET,  handleWifiSaved);
    g_server.on("/api/wifi/add",    HTTP_POST,
                [](AsyncWebServerRequest* req) {},
                nullptr,
                handlePostWifiAddBody);
    g_server.on("/api/wifi/remove", HTTP_POST, handleWifiRemove);
    g_server.on("/api/status",      HTTP_GET,  handleStatus);
    g_server.on("/api/exit",        HTTP_POST, handleExit);

    g_server.onNotFound(handleNotFound);
}

}  // namespace

// ============================================================================
// CaptivePortal public API
// ============================================================================

CaptivePortal portal;

void CaptivePortal::begin() {
    if (g_running) return;
    g_exit_requested = false;

    if (!LittleFS.begin(false)) {
        Serial.println("[Portal] WARN: LittleFS mount failed — web UI unavailable");
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);

    // AP SSID: "Stackchan-XXXX" where XXXX = last 4 hex digits of MAC.
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char apSsid[24];
    snprintf(apSsid, sizeof(apSsid), "Stackchan-%02X%02X", mac[4], mac[5]);

    WiFi.softAP(apSsid, nullptr);

    registerRoutes();
    g_server.begin();
    g_dns.start(53, "*", IPAddress(192, 168, 4, 1));
    g_running = true;

    Serial.printf("[Portal] AP \"%s\" up — %s\n", apSsid,
                  WiFi.softAPIP().toString().c_str());
}

void CaptivePortal::end() {
    if (!g_running) return;
    g_dns.stop();
    g_server.end();
    g_running = false;
    Serial.println("[Portal] stopped");
}

void CaptivePortal::tick() {
    if (!g_running) return;
    g_dns.processNextRequest();
}

bool CaptivePortal::running()       { return g_running; }
bool CaptivePortal::exitRequested() { return g_exit_requested; }
void CaptivePortal::clearExitFlag() { g_exit_requested = false; }

}  // namespace stkchan
