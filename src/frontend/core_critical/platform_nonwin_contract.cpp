// Simulated non-Windows platform boundary compilation check
// Exercises shared renderer and platform headers without Win32 declarations
#include <SDL3/SDL.h>
#include <vulkan/vulkan_core.h>

#undef _WIN32
#undef _WIN64

#include "platform/vk_loader.hpp"
#include "platform/wsi_surface.hpp"
#include "platform/wsi_win32_dxgi.hpp"
#include "platform_checks.inc"
#include "renderer/launcher_display.hpp"
#include "renderer/vk_scene.hpp"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "Windows headers detected in non-Windows compilation boundary"
#endif

namespace cc_mw3_re::platform::nonwin_check {

inline void verify_nonwin_boundary_types() {
  static_assert(sizeof(NativeWindowHandle) == sizeof(void *),
                "NativeWindowHandle must be pointer-sized");
  ExclusiveFullscreenHandler handler;
  (void)handler.is_extension("test");
  (void)handler.extension_name();
  (void)handler.is_device_exclusive();
  (void)handler.swapchain_pnext();
  (void)&run_platform_checks;
}

} // namespace cc_mw3_re::platform::nonwin_check
