# On-device MCP server — design

**Date:** 2026-06-10
**Status:** approved (owner, this session)

## 1. Summary

Expose StackChan's abilities as Model Context Protocol tools served from the robot
itself, so MCP clients (Claude Code first) can drive it:
`claude mcp add --transport http stackchan http://stackchan.local/mcp`.

Inspired by upstream `stack-chan/stack-chan` `services/mcp-server` (Moddable/TS);
re-implemented natively for our AsyncWebServer/C++ firmware.

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

## 3. Architecture

New module `src/services/McpServer.{h,cpp}`:

- `McpServer::attach(AsyncWebServer&)` called from `CaptivePortal` when the LAN
  control server starts (LAN mode only, after WiFi join).
- `POST /mcp`: body accumulated per-request (cap 4 KB → `413` before buffering),
  parsed with ArduinoJson, dispatched by `method`:

| method | behavior |
|---|---|
| `initialize` | protocol version `2024-11-05`, capabilities `{tools:{}}`, serverInfo name `stackchan`, version `1.0.0` |
| `notifications/initialized` | `202`, empty body |
| `tools/list` | tool table with JSON Schemas |
| `tools/call` | dispatch to handler (below) |
| `ping` | `{}` result |
| anything else | JSON-RPC `-32601` |

- `GET /mcp` → `405`.

**Thread-safety:** handlers run on the async_tcp task. All effects route through
the existing `ControlBridge` queue (executed on the main task), identical to the
web UI path. `get_status` reads only atomic-ish snapshot values (same fields the
existing `/api/control/state` and `/api/debug/presence` endpoints expose).

## 4. Tools

| tool | params (JSON Schema) | maps to | result |
|---|---|---|---|
| `say` | `text` (string, required, ≤500 chars); `expression` (enum, optional, default `happy`) | `requestExternalSpeak(text, expr)` | "queued" / `isError`: "busy — robot is mid-conversation" |
| `set_expression` | `tag` (enum: neutral, happy, sad, angry, doubt, sleepy) | `controlBridge.pushExpression` | ok |
| `move_head` | `yaw` (int −45…45), `pitch` (int 0…25) | `controlBridge.pushServo` (idle-pause + clamps apply) | ok + applied pose |
| `get_status` | none | reads presence, FSM state, servo pose, volume, battery %, free heap, RSSI | JSON text content |

Tool-level failures return MCP `isError: true` content, not protocol errors.
Param violations return `-32602` naming the offending field.

## 5. Error handling

- Malformed JSON → `-32700`. Missing `method` → `-32600`.
- Body > 4 KB → HTTP `413` (no JSON-RPC envelope).
- JSON doc allocation failure → `-32603` internal error.
- Notifications (no `id`) never get JSON-RPC responses (HTTP `202`).

## 6. Testing

- `pio run` + 22 native tests stay green (no shared-logic changes expected).
- On-device curl pass: one request per method incl. error cases (bad JSON, bad
  tool, out-of-range yaw).
- End-to-end: `claude mcp add --transport http stackchan http://192.168.1.121/mcp`,
  then Claude calls each tool live (owner observes robot).

## 7. Future expansion (documented intent, not v1)

`take_photo` (chunked/downscaled), `play_tone`, canned-speech triggers
(`speak_clip`), `get_camera_stats`, presence event streaming (needs SSE).
Each is one tool-table entry + handler.
