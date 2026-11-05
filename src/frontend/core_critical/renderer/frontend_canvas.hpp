#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mw34::frontend {
// Shared fit/inverse for an original logical canvas. A future reflow layout
// can choose different logical bounds without changing presentation or input.
struct CanvasFit {
    int x{}, y{}, width{}, height{};
    unsigned logical_width{}, logical_height{};
    // Fit without cropping, then center the rounded pixel rectangle. Keep the
    // same rectangle for presentation and input inversion after a resize.
    static CanvasFit fit(unsigned logical_width_pixels, unsigned logical_height_pixels,
                         unsigned physical_width_pixels, unsigned physical_height_pixels) {
        if (!logical_width_pixels || !logical_height_pixels || !physical_width_pixels ||
            !physical_height_pixels)
            throw std::runtime_error("Frontend canvas has zero dimensions");
        const double fit_scale = std::min(double(physical_width_pixels) / logical_width_pixels,
                                          double(physical_height_pixels) / logical_height_pixels);
        const int fitted_width = int(std::lround(logical_width_pixels * fit_scale)),
                  fitted_height = int(std::lround(logical_height_pixels * fit_scale));
        if (fitted_width <= 0 || fitted_height <= 0)
            throw std::runtime_error("Frontend canvas cannot fit its output");
        return {(int(physical_width_pixels) - fitted_width) / 2,
                (int(physical_height_pixels) - fitted_height) / 2,
                fitted_width,
                fitted_height,
                logical_width_pixels,
                logical_height_pixels};
    }
    static int logical_axis(int physical_coordinate, int origin, int extent, unsigned logical) {
        // Margin coordinates stay outside the logical canvas so a click in a
        // letterbox bar cannot become a click on an edge control. Only points
        // inside the fitted rectangle clamp to the last logical pixel.
        const double value = double(physical_coordinate - origin) * logical / extent;
        if (physical_coordinate < origin)
            return int(std::floor(value));
        if (physical_coordinate >= origin + extent)
            return int(std::ceil(value));
        return std::clamp(int(std::lround(value)), 0, int(logical) - 1);
    }
    int logical_x(int physical_coordinate) const {
        return logical_axis(physical_coordinate, x, width, logical_width);
    }
    int logical_y(int physical_coordinate) const {
        return logical_axis(physical_coordinate, y, height, logical_height);
    }
    int physical_x(int logical) const {
        return x + int(std::lround(double(logical) * width / logical_width));
    }
    int physical_y(int logical) const {
        return y + int(std::lround(double(logical) * height / logical_height));
    }
};
} // namespace mw34::frontend
