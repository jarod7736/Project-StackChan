#include "McpServer.h"

// McpServer — the thin Arduino transport layer for the on-device MCP server
// (spec docs/superpowers/specs/2026-06-10-mcp-server-design.md §3–§4).
//
// POST /mcp carries one JSON-RPC 2.0 message (Streamable HTTP, stateless JSON
// mode). This file owns the AsyncWebServer route, the 4 KB body cap, the
// per-request accumulation buffer, the Origin guard, and the four firmware
// tool handlers. All protocol logic lives in McpProtocol (pure C++, tested
// in [env:native]); all actuator effects route through the ControlBridge
// queue — handlers here run on the async_tcp task and never touch
// face/servos/audio/FSM directly.

#include <ESPAsyncWebServer.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <cstring>
#include <cstdlib>

#include "McpProtocol.h"
#include "../app/ControlBridge.h"
#include "../hal/AudioPlayer.h"
#include "../hal/Servos.h"
#include "../state_machine.h"
#include "../vision/PresenceSensor.h"

namespace stkchan {

namespace {

// Body cap (spec §3): one JSON-RPC request comfortably fits; anything larger
// is rejected with 413 and the connection is closed.
constexpr size_t kMcpBodyCap = 4096;

// PSRAM allocator for all MCP JsonDocuments (spec §3 Memory) — parse and
// response docs must not eat internal heap, which is razor thin with vision.
struct PsramAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override { return ps_malloc(n); }
    void  deallocate(void* p) override { free(p); }
    void* reallocate(void* p, size_t n) override { return ps_realloc(p, n); }
};
PsramAllocator g_psramAlloc;

// Protocol core; the four firmware tools are registered on first attach.
McpProtocol g_mcp;

// Per-request body accumulator hung on request->_tempObject (NOT a shared
// global — concurrent clients must not interleave).
//
// Lifecycle: AsyncWebServerRequest's destructor calls free(_tempObject), so
// this struct MUST be a single plain-malloc'd block with no owned heap
// pointers left at destruction time. The 4097-byte body buffer is therefore
// allocated SEPARATELY via ps_malloc (PSRAM) and freed by us — in the
// completion handler after dispatch, on the reject paths, and (safety net
// for a mid-upload disconnect, where the completion handler never runs) in
// onDisconnect. Every free nulls `buf`, so the paths cannot double-free.
struct McpBody {
    char*  buf;       // ps_malloc'd, kMcpBodyCap + 1 bytes; freed by us
    size_t len;       // bytes accumulated so far
    bool   rejected;  // 413/500 already sent; completion handler must bail
};

// Frees the PSRAM body buffer (idempotent — see lifecycle note above).
void freeBodyBuf(McpBody* body) {
    if (body && body->buf) {
        free(body->buf);
        body->buf = nullptr;
    }
}

// ── Origin guard (spec §3) ──────────────────────────────────────────────────
// Cheap DNS-rebinding defense: browsers always send Origin; if present, the
// host must be localhost, a .local name, or an RFC1918 address. Non-browser
// clients (curl, Claude Code) send no Origin and are unaffected.

bool originAllowed(const String& origin) {
    // Strip scheme ("http://"), path, and port to isolate the host.
    String host = origin;
    int schemeEnd = host.indexOf("://");
    if (schemeEnd >= 0) host = host.substring(schemeEnd + 3);
    int slash = host.indexOf('/');
    if (slash >= 0) host = host.substring(0, slash);
    int colon = host.lastIndexOf(':');
    if (colon >= 0) host = host.substring(0, colon);
    host.toLowerCase();

    if (host == "localhost" || host == "127.0.0.1") return true;
    if (host.endsWith(".local")) return true;
    // RFC1918: 10.0.0.0/8, 192.168.0.0/16, 172.16.0.0/12.
    if (host.startsWith("10.") || host.startsWith("192.168.")) return true;
    if (host.startsWith("172.")) {
        int dot = host.indexOf('.', 4);
        if (dot < 0) return false;
        int second = host.substring(4, dot).toInt();
        return second >= 16 && second <= 31;
    }
    return false;
}

// ── HTTP handlers ───────────────────────────────────────────────────────────

// Body accumulation (runs per TCP chunk on the async_tcp task). First chunk
// allocates the McpBody struct + PSRAM buffer; the cap is enforced both on
// the declared total AND on every running index+len (covers chunked transfer
// with no Content-Length). On rejection we close the client so the remaining
// inflow doesn't drain through LwIP.
void onMcpBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
               size_t index, size_t total) {
    McpBody* body = static_cast<McpBody*>(request->_tempObject);
    if (!body) {
        // Struct is plain malloc (internal heap, 16 B) because AsyncWebServer
        // frees _tempObject with free() at request destruction.
        body = static_cast<McpBody*>(malloc(sizeof(McpBody)));
        if (!body) {
            request->send(500);
            request->client()->close();
            return;
        }
        body->buf = nullptr;
        body->len = 0;
        body->rejected = false;
        request->_tempObject = body;
        // Safety net: if the client vanishes mid-upload the completion
        // handler never runs; reclaim the PSRAM buffer here. The struct
        // itself is reclaimed by the request destructor.
        request->onDisconnect([request]() {
            freeBodyBuf(static_cast<McpBody*>(request->_tempObject));
        });

        if (total > kMcpBodyCap) {
            body->rejected = true;
            request->send(413);
            request->client()->close();
            return;
        }
        body->buf = static_cast<char*>(ps_malloc(kMcpBodyCap + 1));
        if (!body->buf) {
            body->rejected = true;
            request->send(500);
            request->client()->close();
            return;
        }
    }
    if (body->rejected) return;

    if (index + len > kMcpBodyCap) {
        freeBodyBuf(body);
        body->rejected = true;
        request->send(413);
        request->client()->close();
        return;
    }
    memcpy(body->buf + index, data, len);
    body->len = index + len;
}

// Completion handler — runs once the full body has arrived. Status policy
// (spec §3): every JSON-RPC envelope, results AND protocol errors, ships as
// HTTP 200; non-200 is reserved for 202 (notification), 413 (cap), 405
// (GET), 403 (Origin).
void onMcpRequest(AsyncWebServerRequest* request) {
    McpBody* body = static_cast<McpBody*>(request->_tempObject);

    // body struct malloc failed in onMcpBody (sent 500 already); bail.
    if (!body) return;

    if (body->rejected) return;  // 413/500 already sent

    // Origin check: header absent → OK (non-browser client).
    if (request->hasHeader("Origin") &&
        !originAllowed(request->header("Origin"))) {
        freeBodyBuf(body);
        request->send(403);
        return;
    }

    // Empty/absent body parses as malformed JSON → -32700 envelope (HTTP 200).
    const char* buf = body->buf ? body->buf : "";
    size_t      len = body->len;

    std::string out;
    McpOutcome  outcome = g_mcp.handle(buf, len, out, &g_psramAlloc);
    freeBodyBuf(body);

    if (outcome == McpOutcome::kNotification) {
        request->send(202);
        return;
    }

    // AsyncBasicResponse (used by send(200, …)) copies the body into its own
    // String on construction and frees it when the response completes —
    // provably safe lifetime, no manual buffer needed. The transient copy
    // (≤4 KB internal heap) is freed before the next request; the JsonDocuments
    // — the dominant allocation — are PSRAM-backed, which is the real budget.
    request->send(200, "application/json", out.c_str());
}

// ── Firmware tools (spec §4) ────────────────────────────────────────────────

constexpr const char* kExprTags[] = {"neutral", "happy", "sad",
                                     "angry",   "doubt", "sleepy"};

bool validExprTag(const char* tag) {
    for (const char* t : kExprTags) {
        if (strcmp(tag, t) == 0) return true;
    }
    return false;
}

// Registers the four v1 tools. Handlers run on the async_tcp task: they only
// push into the ControlBridge queue and read snapshot values — never the
// actuators. Validation failures return false with an "invalid:" prefix,
// which McpProtocol renders as JSON-RPC -32602; other failures render as
// MCP isError content.
void registerTools() {
    g_mcp.addTool({"say",
        "Make the robot speak text aloud (TTS) with a facial expression. "
        "Returns queued; speech may be skipped if a voice turn starts first.",
        R"({"type":"object","properties":{
            "text":{"type":"string","description":"<=240 bytes UTF-8"},
            "expression":{"type":"string","enum":["neutral","happy","sad","angry","doubt","sleepy"]}},
            "required":["text"]})",
        [](JsonVariantConst a, std::string& r) {
            const char* t = a["text"] | (const char*)nullptr;
            if (!t)              { r = "invalid:text is required";       return false; }
            // Byte-limit check BEFORE pushing — never silently truncate
            // (UTF-8 must not be split mid-sequence).
            if (strlen(t) > 240) { r = "invalid:text exceeds 240 bytes"; return false; }
            // Validate the optional expression against the 6-tag enum; invalid
            // tags are caught here before reaching pushSay (fix #3).
            const char* expr = a["expression"] | "happy";
            if (!validExprTag(expr)) {
                r = "invalid:unknown expression tag";
                return false;
            }
            if (currentState() != State::IDLE) {
                r = "robot is mid-conversation - try again shortly";
                return false;
            }
            if (!controlBridge.pushSay(t, expr)) {
                r = "queue full";
                return false;
            }
            r = "queued";
            return true;
        }});

    g_mcp.addTool({"set_expression",
        "Set the robot's facial expression.",
        R"({"type":"object","properties":{
            "tag":{"type":"string","enum":["neutral","happy","sad","angry","doubt","sleepy"]}},
            "required":["tag"]})",
        [](JsonVariantConst a, std::string& r) {
            const char* tag = a["tag"] | (const char*)nullptr;
            if (!tag || !validExprTag(tag)) {
                r = "invalid:unknown expression tag";
                return false;
            }
            if (!controlBridge.pushExpression(tag)) {
                r = "queue full";
                return false;
            }
            r = "ok";
            return true;
        }});

    g_mcp.addTool({"move_head",
        "Point the robot's head. yaw: -45..45 deg (positive = left), "
        "pitch: -10..45 deg (positive = up).",
        R"({"type":"object","properties":{
            "yaw":{"type":"integer","minimum":-45,"maximum":45},
            "pitch":{"type":"integer","minimum":-10,"maximum":45}},
            "required":["yaw","pitch"]})",
        [](JsonVariantConst a, std::string& r) {
            if (!a["yaw"].is<int>() ||
                (a["yaw"].as<int>() < -45 || a["yaw"].as<int>() > 45)) {
                r = "invalid:yaw out of range -45..45";
                return false;
            }
            if (!a["pitch"].is<int>() ||
                (a["pitch"].as<int>() < -10 || a["pitch"].as<int>() > 45)) {
                r = "invalid:pitch out of range -10..45";
                return false;
            }
            int yaw = a["yaw"].as<int>();
            int pitch = a["pitch"].as<int>();
            if (!controlBridge.pushServo(yaw, pitch)) {
                r = "queue full";
                return false;
            }
            char buf[40];
            snprintf(buf, sizeof(buf), "ok yaw=%d pitch=%d", yaw, pitch);
            r = buf;
            return true;
        }});

    g_mcp.addTool({"get_status",
        "Robot status snapshot: presence, FSM state, head pose, volume, "
        "battery, free heap, WiFi RSSI.",
        R"({"type":"object","properties":{}})",
        [](JsonVariantConst, std::string& r) {
            // Snapshot reads only (same fields /api/control/state and
            // /api/debug/presence expose); doc allocated from PSRAM.
            JsonDocument doc(&g_psramAlloc);
            doc["present"]  = presence.present();
            // score: same presenceDebugStatus() call used by /api/debug/presence
            // (fix #1 — vision module exposes it identically).
#if STKCHAN_PRESENCE
            {
                uint32_t inferMs, infers; int det, cands; float score, c1top;
                presenceDebugStatus(inferMs, infers, det, score, cands, c1top);
                doc["score"] = score;
            }
#endif
            doc["fsm"]      = (int)currentState();
            doc["yaw"]      = servos.currentYaw();
            doc["pitch"]    = servos.currentPitch();
            doc["volume"]   = audio.getVolume();
            doc["battPct"]  = M5.Power.getBatteryLevel();
            doc["heapFree"] = ESP.getFreeHeap();
            doc["rssi"]     = WiFi.RSSI();
            serializeJson(doc, r);
            return true;
        }});
}

// MCP clients (e.g. Claude Desktop) send "Accept: application/json,
// text/event-stream". ESPAsyncWebServer 3.x classifies any request whose
// Accept contains "text/event-stream" as RCT_EVENT, and
// AsyncCallbackWebHandler::canHandle requires request->isHTTP() — which
// excludes RCT_EVENT — so the stock server.on() POST route 404s for every
// real MCP client.
//
// Fix: subclass AsyncWebHandler directly. canHandle() matches POST /mcp
// regardless of the Accept-derived connection-type classification, bypassing
// the isHTTP() gate. isRequestHandlerTrivial() returns false so the framework
// calls handleBody() for body accumulation (WebRequest.cpp lines ~219, ~366).
//
// Note: a GET /mcp with that Accept header would also be RCT_EVENT and skip
// the 405 route below — it will 404. That is acceptable; clients don't GET.
class McpPostHandler : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        return request->method() == HTTP_POST && request->url() == "/mcp";
    }
    void handleRequest(AsyncWebServerRequest* request) override {
        onMcpRequest(request);
    }
    void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                    size_t index, size_t total) override {
        onMcpBody(request, data, len, index, total);
    }
    bool isRequestHandlerTrivial() const override { return false; }
};

}  // namespace

// ── Public API ──────────────────────────────────────────────────────────────

void mcpAttach(AsyncWebServer& server) {
    // Tool table is process-static; guard against a second attach.
    static bool toolsRegistered = false;
    if (!toolsRegistered) {
        registerTools();
        toolsRegistered = true;
    }

    // One-time alloc: McpPostHandler matches POST /mcp regardless of the
    // Accept-derived RCT classification (see class comment above).
    server.addHandler(new McpPostHandler());

    // Streamable HTTP without SSE: no server-initiated stream to GET.
    // (A GET with Accept: text/event-stream would be RCT_EVENT and skip this
    // route too — it will 404, which is acceptable; clients never GET /mcp.)
    server.on("/mcp", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(405);
    });
}

}  // namespace stkchan
