# On-device MCP server — design

**Date:** 2026-06-10 (rev 2 — advisor review applied)
**Status:** approved design, advisor-reviewed; awaiting owner spec sign-off

## 1. Summary

Expose StackChan's abilities as Model Context Protocol tools served from the robot
itself, so MCP clients (Claude Code first) can drive it:

```
claude mcp add --transport http stackchan http://192.168.1.121/mcp
```

(IP, not `stackchan.local` — mDNS is unreliable from WSL2, where Claude Code runs
here. mDNS still works for other clients.)

Inspired by upstream `stack-chan/stack-chan` `services/mcp-server` (Moddable/TS);
re-implemented natively for our AsyncWebServer/C++ firmware.

**Deployment context:** the LAN AI host is about to grow — a Ryzen AI Max box
(128 GB RAM, NPU running Lemonade's OpenAI-compatible server) will take over
local model serving from the current Ollama host. Local agents on that box are
expected MCP clients too, not just Claude Code. Nothing in this design assumes a
particular client or cloud; all the robot's AI endpoints stay NVS-configurable
(`chat_host` etc. — see §8).

## 2. Goals & non-goals

**Goals**
- MCP Streamable HTTP endpoint at `POST /mcp`, JSON-RPC 2.0, stateless JSON mode
  (single JSON response per request; no SSE, no sessions, no auth — matches the
  existing open-LAN posture of `/api/control/*`).
- Four v1 tools: `say`, `set_expression`, `move_head`, `get_status`.
- Easy expansion: tools live in one static table (name, description, input
  schema, handler); adding a tool is one entry + one handler.
- Heap-safe on a device whose internal RAM is razor thin with vision enabled.

**Non-goals (v1)**
- `take_photo` (≈200 KB base64 responses starve LwIP; revisit with chunking).
- SSE / server-initiated messages, sessions, resumability, auth.
- MCP resources/prompts capabilities (tools only).
- JSON-RPC batch arrays (legal only pre-2025-06-18; we reject with `-32600`).

## 3. Architecture

New module `src/services/McpServer.{h,cpp}`:

- `McpServer::attach(AsyncWebServer&)` called from `CaptivePortal` when the LAN
  control server starts (LAN mode only, after WiFi join).
- `POST /mcp` body handling (async_tcp task):
  - Per-request accumulation buffer attached via `request->_tempObject` — NOT a
    shared global (the existing `g_pending_config` single-global pattern has a
    latent interleaving bug with concurrent clients; do not copy it).
  - Cap 4096 bytes: on the first chunk, if `total > 4096`, and on every chunk if
    `index + len > 4096` (covers chunked transfer with no Content-Length), send
    `413`, mark the request rejected, and `request->client()->close()` so the
    remaining inflow doesn't drain through LwIP.
- **Dispatch order (normative):**
  1. Parse JSON. Malformed → `-32700` (HTTP 200, `id: null`).
  2. Batch array → `-32600`.
  3. **No `id` member → HTTP `202`, empty body, regardless of method.** This
     covers `notifications/initialized`, `notifications/cancelled`,
     `notifications/roots/list_changed`, and anything else Claude Code emits.
     Notifications NEVER receive a JSON-RPC response or error.
  4. Only `id`-bearing requests reach the method table.

| method | behavior |
|---|---|
| `initialize` | echo the client's `protocolVersion` if it is one of {`2024-11-05`, `2025-03-26`, `2025-06-18`}, else respond `2025-06-18`; capabilities `{tools:{}}`; serverInfo name `stackchan`, version `1.0.0` |
| `tools/list` | tool table with JSON Schemas |
| `tools/call` | dispatch to handler (§4) |
| `ping` | `{}` result |
| anything else (with `id`) | JSON-RPC `-32601` |

- `GET /mcp` → `405`.
- **HTTP status policy:** every JSON-RPC envelope — results AND protocol errors
  (`-32700` … `-32603`) — ships as **HTTP 200** `Content-Type: application/json`.
  Non-200 is reserved for: `202` notifications, `413` body cap, `405` GET, `403`
  Origin rejection. (A 4xx on `initialize` would trip Claude Code's legacy
  HTTP+SSE fallback probe and fail the connection uselessly.)
- **Headers:** `MCP-Protocol-Version` request header is accepted and ignored.
  `Accept` is ignored (curl-friendly). `Mcp-Session-Id` is never issued.
  JSON-RPC `id` is echoed with its original type (number or string).
- **Origin:** if an `Origin` header is present and its host is not a private-LAN
  address, `.local` name, or `localhost`, respond `403` (cheap DNS-rebinding
  guard; non-browser clients send no Origin and are unaffected).

**Memory:** all `JsonDocument`s (parse + response) use an ArduinoJson v7 custom
`Allocator` backed by `ps_malloc` (PSRAM), as do the accumulation and serialized
output buffers. Transient internal-heap cost per request must stay under ~2 KB;
`tools/list` (the largest response, ~2–4 KB serialized) must not allocate it
from internal heap. The protocol core's transient serialization std::string and the HTTP library's response copy (each ≤4 KB internal, freed within the request) are accepted; the JsonDocuments — the dominant allocation — are PSRAM-backed.

**Thread-safety:** handlers run on the async_tcp task. All effects route through
the `ControlBridge` queue (executed on the main task). `get_status` reads only
snapshot values (same fields `/api/control/state` and `/api/debug/presence`
already expose from this task). No handler calls `requestExternalSpeak`,
`face`, `motion`, or `servos` directly.

## 4. Tools

**ControlBridge change (prerequisite):** `ControlCommand` gains
`char expr[12]`; `text` grows 192 → 256 bytes. Queue footprint: ~272 B × depth 8
≈ 2.2 KB static internal (was 1.5 KB) — acceptable. `pushSay(text, expr)` sets
both; the SAY dispatch passes `c.expr` (default `"happy"`) instead of the
current hardcoded `"happy"`.

| tool | params (JSON Schema) | maps to | result |
|---|---|---|---|
| `say` | `text` (string, required, **≤240 bytes UTF-8** — schema documents the byte semantics); `expression` (enum §below, optional, default `happy`) | `controlBridge.pushSay(text, expr)` | see semantics below |
| `set_expression` | `tag` (enum: neutral, happy, sad, angry, doubt, sleepy) | `controlBridge.pushExpression` | ok |
| `move_head` | `yaw` (int −45…45), `pitch` (int 0…25) | `controlBridge.pushServo` (idle-pause + clamps apply) | ok + applied pose |
| `get_status` | none | snapshot: presence (present/score), FSM state, yaw/pitch, volume, battery %, free heap, RSSI | JSON text content |

**`say` semantics (honest about async reality):** the handler returns
`"queued"` when the ControlBridge queue accepts the command. Best-effort busy
detection: if the FSM snapshot (`currentState()`) is not IDLE at push time, the
result is `isError: true` "robot is mid-conversation — try again shortly"
WITHOUT pushing. Queued speech can still be dropped if the FSM becomes busy
before dispatch; v1 accepts this (no result-reporting channel) and the tool
description says so.

Text longer than the byte limit → `-32602` naming the field. **Never silently
truncate** (UTF-8 must not be split mid-sequence).

Param violations → `-32602` naming the offending field. Tool-level failures →
MCP `isError: true` content, not protocol errors.

## 5. Error handling (summary)

- Malformed JSON → `-32700` (id null). Missing `method` on id-bearing message →
  `-32600`. Unknown method (with id) → `-32601`. Bad params → `-32602`. JSON
  doc allocation failure → `-32603`.
- All of the above: HTTP 200 (§3 status policy).
- Notifications: HTTP 202, no body — even when unknown or malformed-but-id-less.

## 6. Testing

- `pio run` + 22 native tests stay green. The ControlBridge buffer change is
  covered by compile + on-device behavior (no native harness exists for it).
- On-device curl pass: initialize (each of the 3 protocol versions + an unknown
  one), notification (expect 202), tools/list, each tool happy-path, bad JSON,
  unknown method, unknown notification (202, NOT -32601), oversized body (413),
  yaw out of range (-32602), say text > 240 bytes (-32602), string `id` echo.
- End-to-end: `claude mcp add --transport http stackchan http://192.168.1.121/mcp`,
  Claude calls each tool live (owner observes robot), including a user-initiated
  cancel mid-`say` (exercises `notifications/cancelled` → 202 path).

## 7. Future expansion (documented intent, not v1)

`take_photo` (chunked/downscaled), `play_tone`, canned-speech triggers
(`speak_clip`), `get_camera_stats`, presence event streaming (needs SSE).
Each is one tool-table entry + handler.

## 8. Related (out of scope here, noted for the Halo-box arrival)

When the Ryzen AI Max box lands, the robot needs no MCP changes — but two
config-side items will matter and deserve their own small efforts:
- `ChatClient` speaks Ollama's `/api/chat`; Lemonade serves OpenAI-compatible
  `/v1/chat/completions`. Either run Lemonade's Ollama-compat layer (if enabled)
  or add an OpenAI-style chat path selected by NVS key.
- `stt_url` already accepts any Whisper-compatible endpoint, and a local TTS
  endpoint on the box could join `tts_provider` — both pure-config or small
  client tweaks, fully local pipeline end-to-end.
