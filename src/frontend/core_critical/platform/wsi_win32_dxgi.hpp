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

namespace cc_mw3_re::platform {

inline HWND get_window_hwnd(SDL_Window *window) {
  if (!window)
    return nullptr;
  return static_cast<HWND>(
      SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

inline HMODULE load_vulkan_library() {
  return ::LoadLibraryExW(L"vulkan-1.dll", nullptr,
                          LOAD_LIBRARY_SEARCH_SYSTEM32);
}

inline void free_vulkan_library(HMODULE mod) {
  if (mod)
    ::FreeLibrary(mod);
}

inline void *get_proc_address(HMODULE mod, const char *name) {
  return mod ? reinterpret_cast<void *>(::GetProcAddress(mod, name)) : nullptr;
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

inline bool is_window_iconic(HWND hwnd) { return hwnd && ::IsIconic(hwnd); }

inline bool is_window_valid(HWND hwnd) { return hwnd && ::IsWindow(hwnd); }

inline bool is_window_visible(HWND hwnd) {
  return hwnd && ::IsWindowVisible(hwnd);
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

} // namespace cc_mw3_re::platform

#else

// Non-Win32 portable stub declarations
using HWND = void *;
using HMODULE = void *;
using LRESULT = long long;
using UINT = unsigned int;
using WPARAM = unsigned long long;
using LPARAM = long long;

namespace mw34::vkscene {
class DxgiPresent {
public:
  bool active() const { return false; }
  void detach() {}
  void reset_window() {}
  void request_pixel(int, int) {}
  uint32_t read_pixel() { return 0; }
};
} // namespace mw34::vkscene

namespace cc_mw3_re::platform {

inline void *get_window_hwnd(SDL_Window *) { return nullptr; }

inline HMODULE load_vulkan_library() { return nullptr; }

inline void free_vulkan_library(HMODULE) {}

inline void *get_proc_address(HMODULE, const char *) { return nullptr; }

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

inline bool is_window_valid(HWND hwnd) { return hwnd != nullptr; }

inline bool is_window_visible(HWND hwnd) { return hwnd != nullptr; }

inline long get_window_style(HWND) { return 0; }

inline long get_window_exstyle(HWND) { return 0; }

inline void pump_presentation_messages() {}

} // namespace cc_mw3_re::platform

#endif
