#include "ClipId.h"

#include <cstdio>
#include <cstring>

namespace stkchan {

uint32_t fnv1a32(const char* data, size_t len) {
    uint32_t h = 0x811c9dc5u;            // FNV offset basis
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint8_t>(data[i]);
        h *= 0x01000193u;                // FNV prime
    }
    return h;
}

std::string clipPathForText(const char* text) {
    char buf[24];                        // "/clips/" + 8 hex + ".mp3" + NUL = 20
    std::snprintf(buf, sizeof(buf), "/clips/%08x.mp3",
                  fnv1a32(text, std::strlen(text)));
    return std::string(buf);
}

}  // namespace stkchan
