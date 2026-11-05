#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

namespace mw34::effects {
inline constexpr unsigned capacity = 64;
struct alignas(16) Light {
    std::array<float, 4> start{}, end{}, color{};
};
struct alignas(16) Uniform {
    std::array<float, 4> params{};
    std::array<Light, capacity> lights{};
    std::array<std::array<float, 4>, capacity> shadow{};
    // Appended to preserve the original light/shadow prefix. z=0 retains legacy spots.
    std::array<std::array<float, 4>, capacity> cones{};
};
static_assert(sizeof(Light) == 48 && sizeof(Uniform) == 16 + 80 * capacity);
using Point = std::array<float, 3>;

inline Point reconstruct(float x, float y, float depth, const std::array<float, 6> &p,
                         const std::array<float, 12> &w) {
    if (!std::isfinite(depth) || depth <= 0 || !std::isfinite(x) || !std::isfinite(y) ||
        p[1] == 0 || p[2] == 0)
        throw std::runtime_error("Invalid effect source projection");
    const Point c{(x - p[3]) / (p[1] * depth), (y - p[4]) / (p[2] * depth), p[0] / depth};
    Point q{};
    for (unsigned i = 0; i < 3; ++i) {
        q[i] = w[i * 4 + 3];
        for (unsigned j = 0; j < 3; ++j)
            q[i] += w[i * 4 + j] * c[j];
        if (!std::isfinite(q[i]) || std::abs(q[i]) > 1000000)
            throw std::runtime_error("Effect source position outside supported world");
    }
    return q;
}
}
