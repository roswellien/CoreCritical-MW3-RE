#include "mech3/content_state.h"
#include "platform/host_path.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "gamez/zutil/zutl_path.h"
#include "mech3/content_base.h"
#include "mech3/content_network.h"
#include "mech3/content_registry.h"
#include "mech3/pilot.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace mech3 {
bool write_managed_pilot_identity(std::FILE *output) {
    const auto content = active_content();
    if (!content)
        cc::content::fail("pilot-content", "Managed pilot has no active content set");
    const cc::content::Json identity = {{"schema_version", 1},
                                        {"lock", content->lock},
                                        {"gameplay_digest", content->gameplay_digest},
                                        {"session_digest", content->session_digest},
                                        {"base_digest", content_base_digest()},
                                        {"simulation_options", content_simulation_options()}};
    const auto text = identity.dump();
    if (text.size() > 16 * 1024 * 1024)
        cc::content::fail("pilot-content", "Managed pilot identity exceeds its size limit");
    const auto length = static_cast<uint32_t>(text.size());
    return output && std::fwrite("CCPILOT1", 1, 8, output) == 8 &&
           std::fwrite(&length, 4, 1, output) == 1 &&
           std::fwrite(text.data(), 1, text.size(), output) == text.size();
}
void read_managed_pilot_identity(std::FILE *input) {
    const auto content = active_content();
    if (!content)
        cc::content::fail("pilot-content", "Managed pilot has no active content set");
    char magic[8]{};
    uint32_t length = 0;
    if (!input || std::fread(magic, 1, 8, input) != 8 || std::memcmp(magic, "CCPILOT1", 8) != 0 ||
        std::fread(&length, 4, 1, input) != 1 || length > 16 * 1024 * 1024)
        cc::content::fail("pilot-content", "Managed pilot identity header is missing or malformed");
    std::string text(length, '\0');
    if (std::fread(text.data(), 1, length, input) != length)
        cc::content::fail("pilot-content", "Managed pilot identity is truncated");
    const auto identity = cc::content::parse_json(text, "managed pilot identity");
    if (!identity.contains("schema_version") || !identity["schema_version"].is_number_integer() ||
        identity["schema_version"] != 1 || identity.at("lock") != content->lock ||
        identity.at("gameplay_digest") != content->gameplay_digest ||
        identity.at("session_digest") != content->session_digest ||
        identity.at("base_digest") != content_base_digest() ||
        !identity.at("simulation_options").is_number_integer() ||
        identity.at("simulation_options") != content_simulation_options())
        cc::content::fail("pilot-content",
                          "Managed pilot package, gameplay or environment identity differs");
}
bool save_managed_pilot(Pilot &pilot) {
    namespace fs = cc::content::fs;
    using cc::content::fail;
    const auto target = fs::u8path(zutl::write_path(pilot.file_path().c_str()));
    const auto parent = target.parent_path().empty() ? fs::current_path() : target.parent_path();
    fs::create_directories(platform::host_path(parent));
    const auto stage =
        parent /
        (".pilot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(platform::host_path(stage)))
        fail("pilot-save", "Cannot reserve managed pilot staging directory");
    const auto temporary = stage / "pilot";
    const auto previous_dirty = pilot.dirty;
    pilot.close();
    try {
        pilot.file = platform::file_open(temporary, "w+b");
        if (!pilot.file || !pilot.write_header() || !pilot.write() || std::ferror(pilot.file) ||
            std::fflush(pilot.file) != 0)
            fail("pilot-save", "Managed pilot write failed; the previous pilot was preserved");
#if defined(_WIN32)
        const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(pilot.file)));
        if (handle == INVALID_HANDLE_VALUE || !FlushFileBuffers(handle))
            fail("pilot-save", "Managed pilot flush failed; the previous pilot was preserved");
#else
        if (::fsync(fileno(pilot.file)) != 0)
            fail("pilot-save", "Managed pilot flush failed; the previous pilot was preserved");
#endif
        auto *written = pilot.file;
        pilot.file = nullptr;
        if (std::fclose(written) != 0)
            fail("pilot-save", "Managed pilot close failed; the previous pilot was preserved");
#if defined(_WIN32)
        if (!MoveFileExW(platform::host_path(temporary).c_str(),
                         platform::host_path(target).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            fail("pilot-save", "Cannot atomically activate the managed pilot");
#else
        fs::rename(platform::host_path(temporary), platform::host_path(target));
#endif
        std::error_code cleanup_error;
        fs::remove(platform::host_path(stage), cleanup_error);
        pilot.dirty = 0;
        return true;
    } catch (...) {
        pilot.close();
        pilot.dirty = previous_dirty;
        std::error_code error;
        fs::remove_all(platform::host_path(stage), error);
        throw;
    }
}
} // namespace mech3
