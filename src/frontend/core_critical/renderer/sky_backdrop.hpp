#pragma once
#include <cmath>
#include <cstddef>
#include <cstring>

namespace mw34::sky {
// Depth is only a distance test; distant solid terrain shares this band.
inline bool far_backdrop(const unsigned char* vertices, std::size_t bytes) {
    if (!vertices || !bytes || bytes % 32) return false;
    for (std::size_t offset = 0; offset < bytes; offset += 32) {
        float depth;
        std::memcpy(&depth, vertices + offset + 8, sizeof depth);
        if (!std::isfinite(depth) || depth < 0 || depth > .00025f) return false;
    }
    return true;
}
// The lower horizon is untextured even when the original draw writes depth.
// Textured terrain must never become sky merely by crossing this depth band.
inline bool backdrop(const unsigned char* vertices, std::size_t bytes,
                     unsigned role, bool textured, bool /*depth_write*/) {
    return (role == 3 || !textured) && far_backdrop(vertices, bytes);
}
}
