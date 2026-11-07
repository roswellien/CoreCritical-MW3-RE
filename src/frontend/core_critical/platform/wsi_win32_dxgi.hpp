#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan_core.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "dxgi_present.hpp"
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan_win32.h>
#include <windows.h>

#undef DrawState // Win32's DrawState macro conflicts with platform::DrawState

namespace cc_mw3_re::platform {

using NativeWindowHandle = void *;

struct WindowHandleCache {
  std::mutex mutex;
  std::unordered_map<const void *, HWND> map;
};

inline WindowHandleCache &get_window_handle_cache() {
  static WindowHandleCache cache;
  return cache;
}

inline void register_window_hwnd(const void *handle, HWND hwnd) {
  if (!handle || !hwnd)
    return;
  auto &cache = get_window_handle_cache();
  std::lock_guard<std::mutex> lock(cache.mutex);
  cache.map[handle] = hwnd;
}

inline void unregister_window_handle(const void *handle) {
  if (!handle)
    return;
  auto &cache = get_window_handle_cache();
  std::lock_guard<std::mutex> lock(cache.mutex);
  cache.map.erase(handle);
}

inline HWND lookup_cached_window_hwnd(const void *handle) {
  if (!handle)
    return nullptr;
  auto &cache = get_window_handle_cache();
  std::lock_guard<std::mutex> lock(cache.mutex);
  auto it = cache.map.find(handle);
  if (it != cache.map.end())
    return it->second;
  return nullptr;
}

static inline void SDLCALL on_window_destroyed_cleanup(void * /*userdata*/,
                                                       void *value) {
  unregister_window_handle(value);
}

inline HWND get_window_hwnd(SDL_Window *window) {
  if (!window)
    return nullptr;
  if (SDL_IsMainThread()) {
    auto props = SDL_GetWindowProperties(window);
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd) {
      register_window_hwnd(window, hwnd);
      if (!SDL_GetPointerProperty(props, "cc_mw3_re.window_cleanup", nullptr)) {
        SDL_SetPointerPropertyWithCleanup(props, "cc_mw3_re.window_cleanup",
                                          window, on_window_destroyed_cleanup,
                                          nullptr);
      }
    }
    return hwnd;
  }
  return lookup_cached_window_hwnd(window);
}

inline NativeWindowHandle get_window_native_handle(SDL_Window *window) {
  if (window) {
    get_window_hwnd(window);
  }
  return static_cast<NativeWindowHandle>(window);
}

inline HWND resolve_window_hwnd(NativeWindowHandle handle) {
  if (!handle)
    return nullptr;
  if (::IsWindow(static_cast<HWND>(handle)))
    return static_cast<HWND>(handle);
  HWND cached = lookup_cached_window_hwnd(handle);
  if (cached)
    return cached;
  if (SDL_IsMainThread())
    return get_window_hwnd(static_cast<SDL_Window *>(handle));
  return nullptr;
}

inline const char *get_platform_surface_extension_name() {
  return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
}

inline VkSurfaceKHR
create_win32_surface(VkInstance instance, NativeWindowHandle hwnd,
                     PFN_vkCreateWin32SurfaceKHR create_fn) {
  HWND h = resolve_window_hwnd(hwnd);
  if (!create_fn || !h || !instance)
    return VK_NULL_HANDLE;
  VkWin32SurfaceCreateInfoKHR ci{};
  ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  ci.hinstance = ::GetModuleHandleW(nullptr);
  ci.hwnd = h;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (create_fn(instance, &ci, nullptr, &surface) != VK_SUCCESS)
    return VK_NULL_HANDLE;
  return surface;
}

inline HWND make_present_child(NativeWindowHandle parent) {
  HWND parent_hwnd = resolve_window_hwnd(parent);
  static const ATOM cls = [] {
    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
      if (m == WM_NCHITTEST)
        return HTTRANSPARENT;
      if (m == WM_ERASEBKGND)
        return 1;
      return ::DefWindowProcW(h, m, w, l);
    };
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MW34VulkanPresent";
    return ::RegisterClassW(&wc);
  }();
  if (!cls || !parent_hwnd)
    return nullptr;
  RECT rc{};
  ::GetClientRect(parent_hwnd, &rc);
  return ::CreateWindowExW(
      WS_EX_NOPARENTNOTIFY, L"MW34VulkanPresent", L"",
      WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS, 0, 0, rc.right,
      rc.bottom, parent_hwnd, nullptr, ::GetModuleHandleW(nullptr), nullptr);
}

inline void destroy_present_child(NativeWindowHandle child) {
  if (child) {
    HWND h = resolve_window_hwnd(child);
    if (h)
      ::DestroyWindow(h);
  }
}

inline void sync_present_child_size(NativeWindowHandle parent,
                                    NativeWindowHandle child) {
  if (!parent || !child)
    return;
  HWND parent_hwnd = resolve_window_hwnd(parent);
  HWND child_hwnd = resolve_window_hwnd(child);
  RECT rc{}, cc{};
  ::GetClientRect(parent_hwnd, &rc);
  ::GetClientRect(child_hwnd, &cc);
  if (rc.right != cc.right || rc.bottom != cc.bottom) {
    ::SetWindowPos(child_hwnd, nullptr, 0, 0, rc.right, rc.bottom,
                   SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);
  }
}

inline void get_window_client_rect(NativeWindowHandle window, long *w,
                                   long *h) {
  if (!window) {
    if (w)
      *w = 0;
    if (h)
      *h = 0;
    return;
  }
  HWND hwnd = resolve_window_hwnd(window);
  if (!hwnd) {
    if (w)
      *w = 0;
    if (h)
      *h = 0;
    return;
  }
  RECT rc{};
  ::GetClientRect(hwnd, &rc);
  if (w)
    *w = rc.right;
  if (h)
    *h = rc.bottom;
}

inline bool is_window_iconic(NativeWindowHandle window) {
  if (!window)
    return false;
  HWND hwnd = resolve_window_hwnd(window);
  return hwnd ? (::IsIconic(hwnd) != FALSE) : false;
}

inline bool is_window_valid(NativeWindowHandle window) {
  if (!window)
    return true;
  HWND hwnd = resolve_window_hwnd(window);
  return hwnd ? (::IsWindow(hwnd) != FALSE) : false;
}

inline bool is_window_visible(NativeWindowHandle window) {
  if (!window)
    return false;
  HWND hwnd = resolve_window_hwnd(window);
  return hwnd ? (::IsWindowVisible(hwnd) != FALSE) : false;
}

inline long get_window_style(NativeWindowHandle window) {
  if (!window)
    return 0;
  HWND hwnd = resolve_window_hwnd(window);
  return hwnd ? ::GetWindowLongW(hwnd, GWL_STYLE) : 0;
}

inline long get_window_exstyle(NativeWindowHandle window) {
  if (!window)
    return 0;
  HWND hwnd = resolve_window_hwnd(window);
  return hwnd ? ::GetWindowLongW(hwnd, GWL_EXSTYLE) : 0;
}

inline void pump_presentation_messages() {
  MSG message{};
  ::PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
}

inline bool is_window_foreground_root(NativeWindowHandle window) {
  if (!window)
    return false;
  HWND hwnd = resolve_window_hwnd(window);
  if (!hwnd || !::IsWindow(hwnd))
    return false;
  HWND foreground = ::GetForegroundWindow();
  if (!foreground)
    return false;
  return ::GetAncestor(foreground, GA_ROOT) == ::GetAncestor(hwnd, GA_ROOT);
}

class ExclusiveFullscreenHandler {
public:
  bool is_extension(const char *name) const {
    return name &&
           !std::strcmp(name, VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME);
  }

  const char *extension_name() const {
    return VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME;
  }

  void setup_device(bool requested, bool supported) {
    requested_ = requested;
    supported_ = supported;
  }

  void set_device_exclusive(bool supported) { supported_ = supported; }

  bool is_device_exclusive() const { return supported_; }

  template <typename F1, typename F2>
  void init_device(VkInstance instance, VkDevice device, F1 get_inst,
                   F2 get_dev) {
    load_device(instance, device, get_inst, get_dev);
  }

  void load_device(VkInstance instance, VkDevice device,
                   PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                   PFN_vkGetDeviceProcAddr get_device_proc_addr) {
    if (!supported_)
      return;
    surfaceCaps2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(
            get_instance_proc_addr(
                instance, "vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
    acquireExclusive =
        reinterpret_cast<PFN_vkAcquireFullScreenExclusiveModeEXT>(
            get_device_proc_addr(device,
                                 "vkAcquireFullScreenExclusiveModeEXT"));
    releaseExclusive =
        reinterpret_cast<PFN_vkReleaseFullScreenExclusiveModeEXT>(
            get_device_proc_addr(device,
                                 "vkReleaseFullScreenExclusiveModeEXT"));
    if (!surfaceCaps2 || !acquireExclusive || !releaseExclusive) {
      throw std::runtime_error(
          "Vulkan exclusive fullscreen entry points are missing");
    }
  }

  void setup_swapchain_chain(NativeWindowHandle window) {
    HWND hwnd = resolve_window_hwnd(window);
    win32_info_ = {};
    win32_info_.sType =
        VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
    win32_info_.pNext = nullptr;
    win32_info_.hmonitor =
        hwnd ? ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;

    exclusive_info_ = {};
    exclusive_info_.sType =
        VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
    exclusive_info_.pNext = &win32_info_;
    exclusive_info_.fullScreenExclusive =
        VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
  }

  void *swapchain_pnext() { return &exclusive_info_; }

  bool validate_swapchain_support(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    if (!supported_)
      return true;
    if (!surfaceCaps2)
      return false;
    VkPhysicalDeviceSurfaceInfo2KHR info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
    info.surface = surface;
    info.pNext = &exclusive_info_;

    VkSurfaceCapabilitiesFullScreenExclusiveEXT caps_ext{};
    caps_ext.sType =
        VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_FULL_SCREEN_EXCLUSIVE_EXT;

    VkSurfaceCapabilities2KHR caps2{};
    caps2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
    caps2.pNext = &caps_ext;

    if (surfaceCaps2(gpu, &info, &caps2) != VK_SUCCESS)
      return false;
    return caps_ext.fullScreenExclusiveSupported != VK_FALSE;
  }

  bool sync(VkDevice device, VkSwapchainKHR swapchain,
            NativeWindowHandle parent_window) {
    if (!supported_)
      return true;
    const bool foreground = is_window_foreground_root(parent_window);
    if (!foreground) {
      if (held_) {
        if (releaseExclusive && device && swapchain) {
          const auto r = releaseExclusive(device, swapchain);
          if (r != VK_SUCCESS)
            throw std::runtime_error("release fullscreen on focus loss failed");
        }
        held_ = false;
      }
      return false;
    }
    if (!held_) {
      if (!acquireExclusive || !device || !swapchain)
        return false;
      const auto r = acquireExclusive(device, swapchain);
      if (r != VK_SUCCESS)
        throw std::runtime_error("acquire Vulkan exclusive fullscreen failed");
      held_ = true;
      std::fprintf(stderr, "Vulkan exclusive fullscreen acquired\n");
    }
    return true;
  }

  void release(VkDevice device, VkSwapchainKHR swapchain) {
    if (held_ && releaseExclusive && device && swapchain) {
      const auto r = releaseExclusive(device, swapchain);
      held_ = false;
      if (r != VK_SUCCESS)
        throw std::runtime_error("release Vulkan exclusive fullscreen failed");
    }
  }

  void reset() {
    held_ = false;
    supported_ = false;
    requested_ = false;
    surfaceCaps2 = nullptr;
    acquireExclusive = nullptr;
    releaseExclusive = nullptr;
  }

private:
  bool requested_{false};
  bool supported_{false};
  bool held_{false};
  PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR surfaceCaps2{nullptr};
  PFN_vkAcquireFullScreenExclusiveModeEXT acquireExclusive{nullptr};
  PFN_vkReleaseFullScreenExclusiveModeEXT releaseExclusive{nullptr};
  VkSurfaceFullScreenExclusiveWin32InfoEXT win32_info_{};
  VkSurfaceFullScreenExclusiveInfoEXT exclusive_info_{};
};

} // namespace cc_mw3_re::platform

namespace mw34::display {
// Own the fullscreen Windows mode without persisting a desktop configuration
// change.
class ExclusiveMode {
  std::wstring device_name_;
  DEVMODEW original_mode_{}, requested_mode_{};
  bool owns_mode_{}, changing_mode_{};
  HWND window_{};

public:
  ~ExclusiveMode() { release(); }
  void release() noexcept {
    if (!owns_mode_ || changing_mode_)
      return;
    changing_mode_ = true;
    const LONG change_result = ::ChangeDisplaySettingsExW(
        device_name_.c_str(), &original_mode_, nullptr, 0, nullptr);
    if (change_result == DISP_CHANGE_SUCCESSFUL)
      owns_mode_ = false;
    changing_mode_ = false;
  }
  void acquire(HWND window_handle, unsigned width, unsigned height) {
    if (changing_mode_)
      return;
    if (owns_mode_ && window_ == window_handle &&
        requested_mode_.dmPelsWidth == width &&
        requested_mode_.dmPelsHeight == height)
      return;
    release();
    MONITORINFOEXW monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!::GetMonitorInfoW(
            ::MonitorFromWindow(window_handle, MONITOR_DEFAULTTONEAREST),
            &monitor_info))
      throw std::runtime_error("Cannot locate fullscreen monitor");
    device_name_ = monitor_info.szDevice;
    original_mode_ = {};
    original_mode_.dmSize = sizeof(original_mode_);
    if (!::EnumDisplaySettingsExW(device_name_.c_str(), ENUM_CURRENT_SETTINGS,
                                  &original_mode_, 0))
      throw std::runtime_error("Cannot read desktop display mode");
    requested_mode_ = original_mode_;
    requested_mode_.dmPelsWidth = width;
    requested_mode_.dmPelsHeight = height;
    requested_mode_.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    changing_mode_ = true;
    LONG change_result = ::ChangeDisplaySettingsExW(
        device_name_.c_str(), &requested_mode_, nullptr, CDS_TEST, nullptr);
    if (change_result == DISP_CHANGE_SUCCESSFUL)
      change_result =
          ::ChangeDisplaySettingsExW(device_name_.c_str(), &requested_mode_,
                                     nullptr, CDS_FULLSCREEN, nullptr);
    changing_mode_ = false;
    if (change_result != DISP_CHANGE_SUCCESSFUL)
      throw std::runtime_error(
          "Exclusive fullscreen mode is not supported by this monitor (Windows "
          "result " +
          std::to_string(change_result) + ")");
    window_ = window_handle;
    owns_mode_ = true;
  }
};

inline ExclusiveMode &exclusive() {
  static ExclusiveMode mode;
  return mode;
}
} // namespace mw34::display

#else

#include <array>

// Non-Win32 portable stub declarations
using NativeWindowHandle = void *;
using HWND = void *;
using HMODULE = void *;
using LRESULT = long long;
using UINT = unsigned int;
using WPARAM = unsigned long long;
using LPARAM = long long;

struct VkWin32KeyedMutexAcquireReleaseInfoKHR {
  VkStructureType sType;
  const void *pNext;
  uint32_t acquireCount;
  const VkDeviceMemory *pAcquireSyncs;
  const uint64_t *pAcquireKeys;
  const uint32_t *pAcquireTimeouts;
  uint32_t releaseCount;
  const VkDeviceMemory *pReleaseSyncs;
  const uint64_t *pReleaseKeys;
};

namespace mw34::vkscene {
class DxgiPresent {
public:
  DxgiPresent() = default;
  ~DxgiPresent() = default;
  static bool requested(bool = false) { return false; }
  bool active() const { return false; }
  VkImage image() const { return VK_NULL_HANDLE; }
  void attach(VkDevice, const VkPhysicalDeviceIDProperties &, unsigned,
              PFN_vkGetDeviceProcAddr, VkExtent3D) {}
  void reset_window() {}
  void detach() noexcept {}
  void prepare(HWND, unsigned, unsigned) {}
  void acquire_image(VkCommandBuffer, PFN_vkCmdPipelineBarrier,
                     PFN_vkCmdClearColorImage) {}
  void release_image(VkCommandBuffer, PFN_vkCmdPipelineBarrier) {}
  VkWin32KeyedMutexAcquireReleaseInfoKHR synchronization() const { return {}; }
  bool present(bool) { return false; }
  void request_pixel(unsigned, unsigned) {}
  std::array<unsigned char, 4> read_pixel() { return {}; }
};
} // namespace mw34::vkscene

namespace cc_mw3_re::platform {

using NativeWindowHandle = void *;

inline void *get_window_hwnd(SDL_Window *) { return nullptr; }

inline HWND resolve_window_hwnd(NativeWindowHandle) { return nullptr; }

inline void unregister_window_handle(const void *) {}

inline NativeWindowHandle get_window_native_handle(SDL_Window *w) {
  return static_cast<NativeWindowHandle>(w);
}

inline const char *get_platform_surface_extension_name() { return nullptr; }

inline VkSurfaceKHR create_win32_surface(VkInstance, NativeWindowHandle,
                                         void *) {
  return VK_NULL_HANDLE;
}

inline HWND make_present_child(NativeWindowHandle) { return nullptr; }

inline void destroy_present_child(NativeWindowHandle) {}

inline void sync_present_child_size(NativeWindowHandle, NativeWindowHandle) {}

inline void get_window_client_rect(NativeWindowHandle, long *w, long *h) {
  if (w)
    *w = 0;
  if (h)
    *h = 0;
}

inline bool is_window_iconic(NativeWindowHandle) { return false; }

inline bool is_window_valid(NativeWindowHandle) { return true; }

inline bool is_window_visible(NativeWindowHandle window) {
  return window != nullptr;
}

inline long get_window_style(NativeWindowHandle) { return 0; }

inline long get_window_exstyle(NativeWindowHandle) { return 0; }

inline void pump_presentation_messages() {}

inline bool is_window_foreground_root(NativeWindowHandle window) {
  if (!window)
    return false;
  return true;
}

class ExclusiveFullscreenHandler {
public:
  bool is_extension(const char *) const { return false; }
  const char *extension_name() const { return nullptr; }
  void setup_device(bool, bool) {}
  void set_device_exclusive(bool) {}
  bool is_device_exclusive() const { return false; }
  template <typename F1, typename F2>
  void init_device(VkInstance, VkDevice, F1, F2) {}
  template <typename F1, typename F2>
  void load_device(VkInstance, VkDevice, F1, F2) {}
  void setup_swapchain_chain(NativeWindowHandle) {}
  void *swapchain_pnext() { return nullptr; }
  bool validate_swapchain_support(VkPhysicalDevice, VkSurfaceKHR) {
    return true;
  }
  bool sync(VkDevice, VkSwapchainKHR, NativeWindowHandle) { return true; }
  void release(VkDevice, VkSwapchainKHR) {}
  void reset() {}
};

} // namespace cc_mw3_re::platform

namespace mw34::display {
class ExclusiveMode {
public:
  void release() noexcept {}
  void acquire(void *, unsigned, unsigned) {}
};

inline ExclusiveMode &exclusive() {
  static ExclusiveMode mode;
  return mode;
}
} // namespace mw34::display

#endif
