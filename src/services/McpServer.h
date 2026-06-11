#pragma once

class AsyncWebServer;

namespace stkchan {

// Registers POST /mcp (+ 405 GET) on the given server. Call once, LAN mode only.
void mcpAttach(AsyncWebServer& server);

}  // namespace stkchan
