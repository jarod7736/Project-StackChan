#pragma once

// ClipId — phrase text → pre-rendered clip path on LittleFS.
//
// A clip's filename is the FNV-1a 32-bit hash of the phrase's exact UTF-8
// bytes, lowercase hex: /clips/<8-hex>.mp3. The text IS the key — the device
// and tools/render_speeches.py (same hash in Python) can never disagree.
// Pure C++ (no Arduino) so it runs under `pio test -e native`.
//
// Spec: docs/superpowers/specs/2026-06-11-stock-clips-design.md

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace stkchan {

// FNV-1a 32-bit over `len` bytes of `data`.
uint32_t fnv1a32(const char* data, size_t len);

// "/clips/<8-hex-lowercase>.mp3" for the exact bytes of NUL-terminated `text`.
std::string clipPathForText(const char* text);

}  // namespace stkchan
