#pragma once
#include "platform/prepared_visuals.h"
#include <cstring>
#include <string>
namespace prepared_fixture {
struct Bytes {
    std::string value;
    void u32(uint32_t n) {
        for (unsigned i = 0; i < 4; ++i)
            value += char(n >> (i * 8));
    }
    void u64(uint64_t n) {
        u32(uint32_t(n));
        u32(uint32_t(n >> 32));
    }
    void f32(float n) {
        uint32_t bits;
        std::memcpy(&bits, &n, 4);
        u32(bits);
    }
    void f64(double n) {
        uint64_t bits;
        std::memcpy(&bits, &n, 8);
        u64(bits);
    }
    void name(const char *text) {
        std::string n(text);
        n.resize(32);
        value += n;
    }
    void matrix() {
        for (int i = 0; i < 16; ++i)
            f64(i % 5 == 0 ? 1 : 0);
    }
};
inline std::string mesh(unsigned version, uint32_t color = 0xffffffff,
                        std::array<float, 3> normal = {0, 0, 1}, bool mirrored_uv = false,
                        bool degenerate_uv = false, bool rank_one_uv = false) {
    Bytes out;
    out.value = std::string("BWMESH") + char('0' + version) + '\0';
    out.u32(1);
    if (version == 4) {
        out.u32(1);
        out.u32(1);
        out.u32(0);
        for (int i = 0; i < 5; ++i)
            out.f64(0);
        out.name("joint");
        out.name("control");
        out.u32(0xffffffff);
        out.matrix();
        out.name("control");
        out.matrix();
    }
    out.name("control");
    if (version != 1)
        out.name("panel");
    const int placements = version == 3 ? 1 : version == 5 ? 2 : version == 6 ? 3 : 0;
    for (int i = 0; i < placements; ++i) {
        out.name("control");
        out.f32(0);
        out.f32(0);
        out.f32(0);
    }
    if (version == 4)
        out.u32(0);
    out.u32(3);
    out.u32(color);
    for (const auto &vertex : {platform::prepared::Vertex{-1, -1, 2, 0, 0, 1, 0, 0},
                               platform::prepared::Vertex{1, -1, 2, 0, 0, 1, 1, 0},
                               platform::prepared::Vertex{0, 1, 2, 0, 0, 1, .5f, 1}})
        for (unsigned i = 0; i < vertex.size(); ++i) {
            float value = vertex[i];
            if (i >= 3 && i < 6)
                value = normal[i - 3];
            if (i == 6 && mirrored_uv)
                value = 1.f - value;
            if (i >= 6 && degenerate_uv)
                value = 0;
            if (i == 7 && rank_one_uv)
                value = 0;
            out.f32(value);
        }
    return out.value;
}
inline std::string textures() {
    Bytes out;
    out.value.assign("BWTEX1\0\0", 8);
    out.u32(1);
    out.u32(1);
    out.u32(0);
    out.name("authored");
    out.u32(2);
    out.u32(1);
    out.u64(0);
    out.value += char(0);
    out.value += char(0xf8);
    out.value += char(0xe0);
    out.value += char(7);
    return out.value;
}
inline std::string normals(uint32_t rgba = 0xffff8080, bool absent = false) {
    Bytes out;
    out.value.assign("BWNRM2\0\0", 8);
    out.u32(1);
    out.u32(absent ? 0 : 1);
    out.u32(absent ? platform::prepared::no_normal : 0);
    if (!absent) {
        out.name("authored");
        out.u32(1);
        out.u32(1);
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash ^= uint8_t(rgba >> shift);
            hash *= 0x100000001b3ULL;
        }
        out.u64(hash);
        out.u32(rgba);
    }
    return out.value;
}
} // namespace prepared_fixture
