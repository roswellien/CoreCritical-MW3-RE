#pragma once
#include <algorithm>
#include <array>
#include <vector>

namespace cc::geometry {
// Development CPU reference for the same modern materials and source geometry.
// Attributes are evaluated before clipping, matching vertex-shader interpolation.
struct ReferenceVertex {
    std::array<float, 3> camera{}, normal{};
    float u{}, v{}, visibility{1};
};
inline std::vector<ReferenceVertex> clip_reference(const std::vector<ReferenceVertex> &input,
                                                   float plane, bool near_plane) {
    std::vector<ReferenceVertex> result;
    if (input.empty())
        return result;
    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto &a = input[index];
        const auto &b = input[(index + 1) % input.size()];
        const bool inside_a = near_plane ? a.camera[2] >= plane : a.camera[2] <= plane;
        const bool inside_b = near_plane ? b.camera[2] >= plane : b.camera[2] <= plane;
        if (inside_a)
            result.push_back(a);
        if (inside_a == inside_b)
            continue;
        const float t = (plane - a.camera[2]) / (b.camera[2] - a.camera[2]);
        const auto mix = [t](float x, float y) { return x + t * (y - x); };
        ReferenceVertex vertex;
        for (unsigned axis = 0; axis < 3; ++axis) {
            vertex.camera[axis] = mix(a.camera[axis], b.camera[axis]);
            vertex.normal[axis] = mix(a.normal[axis], b.normal[axis]);
        }
        vertex.camera[2] = plane;
        vertex.u = mix(a.u, b.u);
        vertex.v = mix(a.v, b.v);
        vertex.visibility = mix(a.visibility, b.visibility);
        result.push_back(vertex);
    }
    return result;
}
} // namespace cc::geometry
