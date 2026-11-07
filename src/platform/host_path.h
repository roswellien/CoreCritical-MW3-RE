#pragma once

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <string>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

// Only host IO boundaries use these paths. Logical VFS/package names and their
// confinement checks, serialized identities and relative paths stay unchanged.
namespace platform {
inline std::filesystem::path logical_host_path(const std::filesystem::path &path) {
#if defined(_WIN32)
    const auto &name = path.native();
    if (name.rfind(L"\\\\?\\", 0) == 0) {
        const auto plain = name.substr(4);
        if (plain.size() >= 4 && (plain[0] == L'U' || plain[0] == L'u') &&
            (plain[1] == L'N' || plain[1] == L'n') && (plain[2] == L'C' || plain[2] == L'c') &&
            plain[3] == L'\\')
            return std::filesystem::path(L"\\\\" + plain.substr(4));
        if (plain.size() >= 3 && plain[1] == L':' && plain[2] == L'\\')
            return std::filesystem::path(plain);
        throw std::filesystem::filesystem_error("Unsupported extended host namespace", path,
                                                std::make_error_code(std::errc::invalid_argument));
    }
#endif
    return path;
}

inline std::filesystem::path host_path(const std::filesystem::path &path) {
#if defined(_WIN32)
    if (path.empty())
        return path;
    auto absolute = std::filesystem::absolute(logical_host_path(path)).lexically_normal();
    absolute.make_preferred();
    const auto &name = absolute.native();
    if (name.rfind(L"\\\\.\\", 0) == 0)
        throw std::filesystem::filesystem_error("Device paths are not host files", path,
                                                std::make_error_code(std::errc::invalid_argument));
    const auto extended =
        name.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + name.substr(2) : L"\\\\?\\" + name;
    if (extended.size() >= 32767)
        throw std::filesystem::filesystem_error("Host path exceeds Windows limits", path,
                                                std::make_error_code(std::errc::filename_too_long));
    return std::filesystem::path(extended);
#else
    return path;
#endif
}

inline std::FILE *file_open(const std::filesystem::path &path, const char *mode) {
#if defined(_WIN32)
    std::wstring wide_mode;
    for (const unsigned char *at = reinterpret_cast<const unsigned char *>(mode); *at; ++at) {
        if (*at > 127 || wide_mode.size() >= 16) {
            errno = EINVAL;
            return nullptr;
        }
        wide_mode.push_back(static_cast<wchar_t>(*at));
    }
    return _wfopen(host_path(path).c_str(), wide_mode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

// Finish staged file contents before publishing its name. The caller retains
// the previous destination when opening or syncing this private stage fails.
inline bool sync_file(const std::filesystem::path &path) {
    auto *file = file_open(path, "r+b");
    if (!file)
        return false;
#if defined(_WIN32)
    const bool synced = ::_commit(::_fileno(file)) == 0;
#else
    const bool synced = ::fsync(::fileno(file)) == 0;
#endif
    const bool closed = std::fclose(file) == 0;
    return synced && closed;
}
} // namespace platform
