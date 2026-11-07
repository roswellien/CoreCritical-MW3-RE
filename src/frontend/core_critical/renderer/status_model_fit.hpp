#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace mw34::status {
struct Fit {
    double scale{1}, x{}, y{};
    bool changed() const { return scale != 1 || x != 0 || y != 0; }
};
// Bounds and the original clear rectangle are in the same guest pixel space.
// Fit before widescreen adaptation, so depth, viewport and geometry share it.
inline Fit fit(const std::array<double, 4> &bounds, const std::array<double, 4> &box,
               double padding = 3) {
    for (double v : bounds)
        if (!std::isfinite(v))
            throw std::runtime_error("Status models: nonfinite projected bounds");
    const double width = bounds[2] - bounds[0], height = bounds[3] - bounds[1];
    if (width <= 0 || height <= 0)
        return {};
    const double left = box[0] + padding, top = box[1] + padding, right = box[2] - padding,
                 bottom = box[3] - padding;
    if (right <= left || bottom <= top)
        throw std::runtime_error("Status models: invalid original preview rectangle");
    Fit result;
    result.scale = std::min({1., (right - left) / width, (bottom - top) / height});
    const double half_width = width * result.scale * .5, half_height = height * result.scale * .5;
    const double cx = (bounds[0] + bounds[2]) * .5, cy = (bounds[1] + bounds[3]) * .5;
    result.x = std::clamp(cx, left + half_width, right - half_width) - cx * result.scale;
    result.y = std::clamp(cy, top + half_height, bottom - half_height) - cy * result.scale;
    return result;
}
} // namespace mw34::status
