#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace mw34::fixture_shadow {
using Point = std::array<float, 3>;
using Matrix = std::array<float, 12>;
using Clip = std::array<float, 16>;
inline constexpr unsigned columns = 16, rows = 24;
inline constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

// Positive-forward cube faces. The receiver uses these same axis conventions.
inline Clip face(const Point &eye, unsigned side, float near_plane, float far_plane) {
    static constexpr std::array<std::array<Point, 3>, 6> axes{
        {{{{0, 0, -1}, {0, -1, 0}, {1, 0, 0}}},
         {{{0, 0, 1}, {0, -1, 0}, {-1, 0, 0}}},
         {{{1, 0, 0}, {0, 0, 1}, {0, 1, 0}}},
         {{{1, 0, 0}, {0, 0, -1}, {0, -1, 0}}},
         {{{1, 0, 0}, {0, -1, 0}, {0, 0, 1}}},
         {{{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}}}}};
    Clip result{};
    for (unsigned r = 0; r < 3; ++r) {
        for (unsigned c = 0; c < 3; ++c)
            result[r * 4 + c] = axes[side][r][c];
        for (unsigned c = 0; c < 3; ++c)
            result[r * 4 + 3] -= axes[side][r][c] * eye[c];
    }
    for (unsigned c = 0; c < 4; ++c)
        result[12 + c] = result[8 + c];
    const float a = far_plane / (far_plane - near_plane);
    const float b = far_plane * near_plane / (far_plane - near_plane);
    for (unsigned c = 0; c < 4; ++c)
        result[8 + c] *= a;
    result[11] -= b;
    return result;
}

inline Clip compose(const Clip &clip, const Matrix &world) {
    Clip result{};
    for (unsigned r = 0; r < 4; ++r) {
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned k = 0; k < 3; ++k)
                result[r * 4 + c] += clip[r * 4 + k] * world[k * 4 + c];
        result[r * 4 + 3] = clip[r * 4 + 3];
        for (unsigned k = 0; k < 3; ++k)
            result[r * 4 + 3] += clip[r * 4 + k] * world[k * 4 + 3];
    }
    return result;
}

struct Bounds {
    Point low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
    void add(const Point &p) {
        for (unsigned i = 0; i < 3; ++i) {
            low[i] = std::min(low[i], p[i]);
            high[i] = std::max(high[i], p[i]);
        }
    }
    bool overlaps(const Point &p, float radius) const {
        float distance2 = 0;
        for (unsigned i = 0; i < 3; ++i) {
            const float d = p[i] - std::clamp(p[i], low[i], high[i]);
            distance2 += d * d;
        }
        return distance2 < radius * radius;
    }
};
} // namespace mw34::fixture_shadow
