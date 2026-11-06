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
#include <vulkan/vulkan_win32.h>
#include <windows.h>

#undef DrawState // Win32's DrawState macro conflicts with platform::DrawState

namespace cc_mw3_re::platform {

using NativeWindowHandle = HWND;

inline HWND get_window_hwnd(SDL_Window *window) {
  if (!window)
    return nullptr;
  return static_cast<HWND>(
      SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

inline NativeWindowHandle get_window_native_handle(SDL_Window *window) {
  return get_window_hwnd(window);
}

inline const char *get_platform_surface_extension_name() {
  return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
}

inline VkSurfaceKHR
create_win32_surface(VkInstance instance, HWND hwnd,
                     PFN_vkCreateWin32SurfaceKHR create_fn) {
  if (!create_fn || !hwnd || !instance)
    return VK_NULL_HANDLE;
  VkWin32SurfaceCreateInfoKHR ci{};
  ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  ci.hinstance = ::GetModuleHandleW(nullptr);
  ci.hwnd = hwnd;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (create_fn(instance, &ci, nullptr, &surface) != VK_SUCCESS)
    return VK_NULL_HANDLE;
  return surface;
}

inline HWND make_present_child(HWND parent) {
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
  if (!cls || !parent)
    return nullptr;
  RECT rc{};
  ::GetClientRect(parent, &rc);
  return ::CreateWindowExW(
      WS_EX_NOPARENTNOTIFY, L"MW34VulkanPresent", L"",
      WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS, 0, 0, rc.right,
      rc.bottom, parent, nullptr, ::GetModuleHandleW(nullptr), nullptr);
}

inline void destroy_present_child(HWND child) {
  if (child)
    ::DestroyWindow(child);
}

inline void sync_present_child_size(HWND parent, HWND child) {
  if (!parent || !child)
    return;
  RECT rc{}, cc{};
  ::GetClientRect(parent, &rc);
  ::GetClientRect(child, &cc);
  if (rc.right != cc.right || rc.bottom != cc.bottom) {
    ::SetWindowPos(child, nullptr, 0, 0, rc.right, rc.bottom,
                   SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);
  }
}

inline void get_window_client_rect(HWND hwnd, long *w, long *h) {
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

inline bool is_window_iconic(HWND hwnd) {
  return hwnd ? (::IsIconic(hwnd) != FALSE) : false;
}

inline bool is_window_valid(HWND hwnd) {
  return hwnd ? (::IsWindow(hwnd) != FALSE) : true;
}

inline bool is_window_visible(HWND hwnd) {
  return hwnd ? (::IsWindowVisible(hwnd) != FALSE) : true;
}

inline long get_window_style(HWND hwnd) {
  return hwnd ? ::GetWindowLongW(hwnd, GWL_STYLE) : 0;
}

inline long get_window_exstyle(HWND hwnd) {
  return hwnd ? ::GetWindowLongW(hwnd, GWL_EXSTYLE) : 0;
}

inline void pump_presentation_messages() {
  MSG message{};
  ::PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
}

inline bool is_window_foreground_root(HWND window) {
  if (!window)
    return false;
  HWND foreground = ::GetForegroundWindow();
  if (!foreground)
    return false;
  return ::GetAncestor(foreground, GA_ROOT) == ::GetAncestor(window, GA_ROOT);
}

struct PlatformExclusiveFullscreenChain {
  VkSurfaceFullScreenExclusiveWin32InfoEXT win32_info{};
  VkSurfaceFullScreenExclusiveInfoEXT exclusive_info{};

  void setup(HWND window) {
    win32_info.sType =
        VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
    win32_info.pNext = nullptr;
    win32_info.hmonitor =
        window ? ::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)
               : nullptr;

    exclusive_info.sType =
        VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
    exclusive_info.pNext = &win32_info;
    exclusive_info.fullScreenExclusive =
        VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
  }

  void *pNext() { return &exclusive_info; }
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

inline NativeWindowHandle get_window_native_handle(SDL_Window *w) {
  return static_cast<NativeWindowHandle>(w);
}

inline const char *get_platform_surface_extension_name() { return nullptr; }

inline VkSurfaceKHR create_win32_surface(VkInstance, HWND, void *) {
  return VK_NULL_HANDLE;
}

inline HWND make_present_child(HWND) { return nullptr; }

inline void destroy_present_child(HWND) {}

inline void sync_present_child_size(HWND, HWND) {}

inline void get_window_client_rect(HWND, long *w, long *h) {
  if (w)
    *w = 0;
  if (h)
    *h = 0;
}

inline bool is_window_iconic(HWND) { return false; }

inline bool is_window_valid(HWND) { return true; }

inline bool is_window_visible(HWND) { return true; }

inline long get_window_style(HWND) { return 0; }

inline long get_window_exstyle(HWND) { return 0; }

inline void pump_presentation_messages() {}

inline bool is_window_foreground_root(HWND) { return true; }

struct PlatformExclusiveFullscreenChain {
  VkSurfaceFullScreenExclusiveInfoEXT exclusive_info{};

  void setup(NativeWindowHandle) {
    exclusive_info.sType =
        VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
    exclusive_info.pNext = nullptr;
    exclusive_info.fullScreenExclusive =
        VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
  }

  void *pNext() { return &exclusive_info; }
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
