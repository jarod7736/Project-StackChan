# Project-StackChan — Advisor Tool Design Spec

**Date:** 2026-06-11
**Status:** Implemented (device path dark until `adv_key` is provisioned)
**Authors:** Jarod Belshaw (decisions) + Claude (drafting + build)
**Beta:** Anthropic `advisor-tool-2026-03-01`

---

## 1. Summary

Add an optional **advisor-tool** path to the casual conversation pipeline. The
[advisor tool](https://docs.anthropic.com) is an Anthropic Messages API beta
feature that lets a fast, low-cost **executor** model consult a stronger
**advisor** model mid-generation: the advisor reads the full transcript, returns
a short plan or course correction (~400–700 text tokens), and the executor
finishes the turn informed by it. You get close to advisor-solo quality while
the bulk of generation runs at executor rates.

For Stack-chan this is the documented *"step up in intelligence from Haiku
alone"* configuration — a **Haiku executor paired with an Opus advisor** — giving
the casual character noticeably better answers than a small local model without
paying full Opus rates on every token.

The feature ships **dark**: it only engages when an Anthropic API key is
provisioned into NVS (`adv_key`). With no key, the casual path is unchanged
(local Ollama on `chat_host`).

## 2. Why this fits the existing architecture

`ChatClient::send(brainMode)` already routes a turn to one of two OpenAI-
compatible backends (casual Ollama vs. the `oc-personal` agent). The advisor
tool is a **third casual backend**, not a graft:

- It only exists on Anthropic's `POST /v1/messages` (not the OpenAI
  `/v1/chat/completions` shape the other two speak), so it needs its own
  request/response codec — but it reuses the same TLS POST helper, the same
  persona system prompt, and the same 6-turn casual history ring.
- The agent (`brainMode`) is untouched: it owns its own tools and context
  server-side, and `<speech>/<expr>` formatting would fight the advisor's tool
  output, so brain turns never take this path.

## 3. Design

### 3.1 Pure protocol module — `src/net/AdvisorProtocol.{h,cpp}`

Hardware-free (`std::string` + ArduinoJson, no Arduino/M5/HTTP), so it is
compiled and unit-tested in `[env:native]` alongside `ResponseParser` /
`ExpressionMap` / `PresenceLogic`. It owns the three things that are pure logic:

- **`isValidModelPair(executor, advisor)`** — the documented compatibility
  table (advisor must be ≥ executor capability). An unsupported pair is a `400`
  from the API, so we reject it locally and fail fast.
- **`buildAdvisorRequestBody(...)`** — serializes the `/v1/messages` body:
  top-level `model` (executor), `max_tokens` (executor cap), `system`, the
  `messages` array, and the `advisor_20260301` tool declaration with the
  advisor `model` plus optional knobs (`max_tokens`, `max_uses`, `caching`).
  Unset knobs are omitted so the server applies its defaults.
- **`parseAdvisorResponse(...)`** — walks the `content` blocks: concatenates the
  executor's `text` blocks (advice arrives mid-stream, so there can be several)
  into the spoken reply, and classifies the `advisor_tool_result` variant
  (`advisor_result` / `advisor_redacted_result` / `advisor_tool_result_error`),
  capturing `stop_reason` and `error_code`. A failed advisor sub-call does **not**
  fail the turn — the executor still answered.

### 3.2 Device glue — `src/net/ChatClient.cpp`

- The file-local `postJson(bearer)` helper is generalized to
  `httpPost(url, body, headers, resp)` taking arbitrary headers; the existing
  Bearer path now builds a one-entry header list, and the advisor path supplies
  `x-api-key`, `anthropic-version`, and `anthropic-beta: advisor-tool-2026-03-01`.
- `send()` routes a casual turn to `sendViaAdvisor_()` when `adv_key` is set.
- `sendViaAdvisor_()` validates the pair, replays persona + history + the user
  turn through `AdvisorProtocol`, POSTs, parses, logs the advice to Serial (the
  device speaks only the executor's final reply — advice is for the executor,
  not the user), and updates history exactly like the casual path.

### 3.3 Configuration — `src/config.h` / NVS

| NVS key (≤15 ch) | Meaning | Default |
|---|---|---|
| `adv_key`  | Anthropic `x-api-key` — **gates the feature** | _(empty → dark)_ |
| `adv_host` | API base URL | `https://api.anthropic.com` |
| `adv_exec` | executor model id | `claude-haiku-4-5-20251001` |
| `adv_model`| advisor model id | `claude-opus-4-8` |

Per-call advisor output is capped at `kAdvisorMaxTokens = 2048` — the documented
sweet spot (~7× shorter advice, near-zero truncation, no measured quality loss).
The executor reply stays bounded by the existing `kChatMaxTokens = 512`.

## 4. Decisions & non-goals

- **No FSM changes.** Routing is entirely inside `ChatClient`; the chat task,
  face, and motion are unaware of which casual backend answered.
- **Advice is not surfaced to the user.** It steers the executor; only the final
  reply is spoken. (Serial logging only, for debugging.)
- **Advisor-side prompt caching is wired but off by default.** It only pays off
  at ≥3 advisor calls/conversation; the casual ring is 6 turns and most turns
  won't consult, so the default-off `caching` knob is correct here.
- **Out of scope:** streaming (the advisor sub-inference doesn't stream anyway,
  and the streaming pipeline is its own roadmap item — see Phase 2 of the
  next-capabilities roadmap), the mid-conversation under-call nudge (relevant for
  long agent loops, not a 6-turn casual chat), and routing the `oc-personal`
  agent through the advisor (the agent owns its own model server-side).

## 5. Testing

`test/test_advisor_protocol/` covers model-pair validation (valid + rejected),
request-body construction (tool block shape, optional-knob emission/omission,
system omitted when empty), and response parsing for every variant
(result/redacted/error/none, multi-text concatenation, malformed input).
Run with `pio test -e native`. The device glue (`ChatClient`) is verified on
hardware, matching the repo convention that HTTP/JSON glue is device-tested while
pure logic is host-tested.
