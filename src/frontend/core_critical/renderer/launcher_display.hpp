#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <windows.h>

namespace mw34::display {
// Resident and uploaded frames share these destination bounds. A world readback
// has guest-sized storage, but still contains the configured output's wide view.
inline RECT presentation_rect(double content_width, double content_height,
                              unsigned window_width, unsigned window_height) {
    if (content_width <= 0 || content_height <= 0)
        throw std::runtime_error("Presentation content dimensions must be positive");
    const double fit = std::min(window_width / content_width, window_height / content_height);
    const LONG width = LONG(std::lround(content_width * fit));
    const LONG height = LONG(std::lround(content_height * fit));
    const LONG x = (LONG(window_width) - width) / 2;
    const LONG y = (LONG(window_height) - height) / 2;
    return {x, y, x + width, y + height};
}
inline RECT upload_presentation_rect(unsigned guest_width, unsigned guest_height,
                                     unsigned output_width, unsigned output_height,
                                     unsigned window_width, unsigned window_height,
                                     bool wide_world) {
    if (wide_world && output_width && output_height)
        return presentation_rect(output_width, output_height, window_width, window_height);
    return presentation_rect(guest_width, guest_height, window_width, window_height);
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
// Own the fullscreen Windows mode without persisting a desktop configuration change.
class ExclusiveMode {
    // The changing flag guards reentrant calls, not concurrent access. The
    // window/display owner must serialize use of this mode record.
    std::wstring device_name_;
    DEVMODEW original_mode_{}, requested_mode_{};
    bool owns_mode_{}, changing_mode_{};
    HWND window_{};

  public:
    ~ExclusiveMode() { release(); }
    void release() noexcept {
        // Restoration is best effort. Keep ownership after a failed Windows
        // call so another release can retry; no exception escapes destruction.
        if (!owns_mode_ || changing_mode_)
            return;
        changing_mode_ = true;
        const LONG change_result =
            ::ChangeDisplaySettingsExW(device_name_.c_str(), &original_mode_, nullptr, 0, nullptr);
        if (change_result == DISP_CHANGE_SUCCESSFUL)
            owns_mode_ = false;
        changing_mode_ = false;
    }
    void acquire(HWND window_handle, unsigned width, unsigned height) {
        // Repeated requests for the same owned window and dimensions are a no-op.
        // Remember the monitor's current mode before testing a temporary change;
        // neither the test nor CDS_FULLSCREEN persists a registry mode setting.
        if (changing_mode_)
            return;
        if (owns_mode_ && window_ == window_handle && requested_mode_.dmPelsWidth == width &&
            requested_mode_.dmPelsHeight == height)
            return;
        release();
        MONITORINFOEXW monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!::GetMonitorInfoW(::MonitorFromWindow(window_handle, MONITOR_DEFAULTTONEAREST),
                               &monitor_info))
            throw std::runtime_error("Cannot locate fullscreen monitor");
        device_name_ = monitor_info.szDevice;
        original_mode_ = {};
        original_mode_.dmSize = sizeof(original_mode_);
        if (!::EnumDisplaySettingsExW(device_name_.c_str(), ENUM_CURRENT_SETTINGS, &original_mode_,
                                      0))
            throw std::runtime_error("Cannot read desktop display mode");
        requested_mode_ = original_mode_;
        requested_mode_.dmPelsWidth = width;
        requested_mode_.dmPelsHeight = height;
        requested_mode_.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
        changing_mode_ = true;
        LONG change_result = ::ChangeDisplaySettingsExW(device_name_.c_str(), &requested_mode_,
                                                        nullptr, CDS_TEST, nullptr);
        if (change_result == DISP_CHANGE_SUCCESSFUL)
            change_result = ::ChangeDisplaySettingsExW(device_name_.c_str(), &requested_mode_,
                                                       nullptr, CDS_FULLSCREEN, nullptr);
        changing_mode_ = false;
        if (change_result != DISP_CHANGE_SUCCESSFUL)
            throw std::runtime_error(
                "Exclusive fullscreen mode is not supported by this monitor (Windows result " +
                std::to_string(change_result) + ")");
        window_ = window_handle;
        owns_mode_ = true;
    }
};
// One process-lifetime owner backs the native host's exclusive display mode.
inline ExclusiveMode &exclusive() {
    static ExclusiveMode mode;
    return mode;
}
} // namespace mw34::display
