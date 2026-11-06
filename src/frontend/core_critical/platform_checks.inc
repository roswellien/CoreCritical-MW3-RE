#pragma once

#include "platform/vk_loader.hpp"
#include "platform/wsi_surface.hpp"
#include "platform/wsi_win32_dxgi.hpp"

#include <stdexcept>

namespace cc_mw3_re::platform {

inline void run_platform_checks() {
  // Assert portable platform boundary abstractions compile and function
  VulkanLoader loader;
  if (!loader.load()) {
    throw std::runtime_error(
        "Platform check failed: VulkanLoader could not load Vulkan library");
  }
  if (!loader.get_instance_proc_addr()) {
    throw std::runtime_error("Platform check failed: VulkanLoader returned "
                             "null vkGetInstanceProcAddr");
  }
  loader.unload();
}

} // namespace cc_mw3_re::platform
