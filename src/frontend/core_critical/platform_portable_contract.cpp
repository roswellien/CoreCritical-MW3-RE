#include "platform/vk_loader.hpp"
#include "platform/wsi_surface.hpp"
#include "renderer/launcher_display.hpp"

#include <stdexcept>
#include <string_view>
#include <type_traits>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "Transitive windows.h detected in shared platform headers"
#endif

namespace cc_mw3_re::platform {

void check_portable_contract() {
  static_assert(std::is_same_v<decltype(mw34::display::Rect{}.left), int32_t>,
                "Rect coordinates must be portable int32_t");
  static_assert(std::is_same_v<decltype(mw34::display::Rect{}.top), int32_t>,
                "Rect coordinates must be portable int32_t");
  static_assert(std::is_same_v<decltype(mw34::display::Rect{}.right), int32_t>,
                "Rect coordinates must be portable int32_t");
  static_assert(std::is_same_v<decltype(mw34::display::Rect{}.bottom), int32_t>,
                "Rect coordinates must be portable int32_t");

  const auto rect = mw34::display::presentation_rect(640, 480, 1920, 1080);
  if (rect.left < 0 || rect.top < 0 || rect.right <= rect.left ||
      rect.bottom <= rect.top) {
    throw std::runtime_error(
        "Portable contract: invalid presentation rect calculation");
  }

  const auto upload_rect = mw34::display::upload_presentation_rect(
      640, 480, 1920, 1080, 1920, 1080, true);
  if (upload_rect.left != 0 || upload_rect.top != 0 ||
      upload_rect.right != 1920 || upload_rect.bottom != 1080) {
    throw std::runtime_error(
        "Portable contract: invalid upload presentation rect calculation");
  }

  const auto samples = mw34::display::samples("2");
  if (samples.x != 2 || samples.y != 1) {
    throw std::runtime_error(
        "Portable contract: invalid sample grid calculation");
  }
}

} // namespace cc_mw3_re::platform
