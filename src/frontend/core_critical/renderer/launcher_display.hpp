#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace mw34::display {

struct Rect {
  int32_t left{0};
  int32_t top{0};
  int32_t right{0};
  int32_t bottom{0};
};

// Resident and uploaded frames share these destination bounds. A world readback
// has guest-sized storage, but still contains the configured output's wide
// view.
inline Rect presentation_rect(double content_width, double content_height,
                              unsigned window_width, unsigned window_height) {
  if (content_width <= 0 || content_height <= 0)
    throw std::runtime_error(
        "Presentation content dimensions must be positive");
  const double fit =
      std::min(window_width / content_width, window_height / content_height);
  const int32_t width = int32_t(std::lround(content_width * fit));
  const int32_t height = int32_t(std::lround(content_height * fit));
  const int32_t x = (int32_t(window_width) - width) / 2;
  const int32_t y = (int32_t(window_height) - height) / 2;
  return {x, y, x + width, y + height};
}

inline Rect upload_presentation_rect(unsigned guest_width,
                                     unsigned guest_height,
                                     unsigned output_width,
                                     unsigned output_height,
                                     unsigned window_width,
                                     unsigned window_height, bool wide_world) {
  if (wide_world && output_width && output_height)
    return presentation_rect(output_width, output_height, window_width,
                             window_height);
  return presentation_rect(guest_width, guest_height, window_width,
                           window_height);
}

struct Samples {
  unsigned x{1}, y{1};
};

// Return the supersampling grid. FXAA is a separate postprocess, so its grid is
// still 1x1; "2" doubles horizontal samples and "4" requests a 2x2 grid.
inline Samples samples(const char *value) {
  if (!value || !std::strcmp(value, "off") || !std::strcmp(value, "1") ||
      !std::strcmp(value, "fxaa"))
    return {};
  if (!std::strcmp(value, "2"))
    return {2, 1};
  if (!std::strcmp(value, "4"))
    return {2, 2};
  throw std::runtime_error("Unknown Remaster AA setting");
}

} // namespace mw34::display
