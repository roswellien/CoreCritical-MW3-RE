#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace platform {
// Windows' narrow CRT main uses the machine's ANSI code page. Preserve the
// command line's Unicode paths before passing UTF-8 arguments to shared code.
inline int windows_utf8_main(int count, wchar_t **arguments, int (*run)(int, char **)) {
    std::vector<std::string> values;
    values.reserve(count);
    for (int i = 0; i < count; ++i) {
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, arguments[i], -1,
                                               nullptr, 0, nullptr, nullptr);
        if (length <= 0) {
            std::fputs("Invalid Unicode command-line argument\n", stderr);
            return 2;
        }
        std::string value(static_cast<size_t>(length), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, arguments[i], -1, value.data(),
                                length, nullptr, nullptr) != length) {
            std::fputs("Cannot convert Unicode command-line argument\n", stderr);
            return 2;
        }
        value.resize(static_cast<size_t>(length - 1));
        values.push_back(std::move(value));
    }
    std::vector<char *> pointers;
    pointers.reserve(values.size() + 1);
    for (auto &value : values)
        pointers.push_back(value.data());
    pointers.push_back(nullptr);
    return run(count, pointers.data());
}
} // namespace platform
#endif
