#include "mech3/content_base.h"
#include "platform/host_path.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <sys/stat.h>

#include "content/content.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/content_registry.h"
#include "mech3/mission.h"

namespace mech3 {
namespace {
using cc::content::Json;
namespace fs = std::filesystem;
std::string g_key, g_digest;
std::string lower(std::string value) {
    for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool relevant(const std::string &logical) {
    const auto file = lower(fs::u8path(logical).filename().string());
    const auto extension = fs::u8path(file).extension().string();
    if (extension == ".zrd" || extension == ".gs" || extension == ".gw" || extension == ".zan")
        return true;
    return extension == ".zbd";
}
bool recognized(const std::string &logical, const std::set<std::string> &worlds) {
    auto path = fs::u8path(logical);
    const auto parent = lower(path.parent_path().generic_string());
    if (parent.empty() || parent == "zbd" || parent == "support" ||
        parent.rfind("support/", 0) == 0)
        return relevant(logical);
    if (parent.rfind("zbd/", 0) != 0)
        return false;
    auto world = parent.substr(4);
    world.resize(world.find('/') == std::string::npos ? world.size() : world.find('/'));
    return worlds.count(world) && relevant(logical);
}
bool script(const std::string &logical) {
    const auto extension = fs::u8path(logical).extension().string();
    return extension == ".gs" || extension == ".gw" || extension == ".zan";
}
bool claimed_gameplay_source(const std::string &host) {
    const auto content = active_content();
    if (!content || host.empty())
        return false;
    for (const auto &selected : zutl::mounts().managed_files)
        if (selected.second == host)
            for (const auto &claim : content->lock.value("legacy_resources", Json::array()))
                if (claim.at("path") == selected.first && claim.at("gameplay") == true)
                    return true;
    return false;
}
std::vector<std::string> original_roots(const zutl::Mounts &mounts,
                                        const std::set<std::string> &worlds) {
    std::vector<std::string> candidates = mounts.read;
    if (!mounts.write.empty())
        candidates.insert(candidates.begin(), mounts.write);
    std::vector<std::string> roots;
    for (const auto &candidate : candidates) {
        bool derived = false;
        // Native mission setup temporarily adds each root's zbd/world and zbd
        // directories as aliases. They do not add new bytes to the base identity.
        for (const auto &parent : candidates)
            if (candidate != parent) {
                const auto relative =
                    fs::u8path(candidate).lexically_relative(fs::u8path(parent)).generic_string();
                if (relative == "zbd" ||
                    (relative.rfind("zbd/", 0) == 0 && worlds.count(lower(relative.substr(4)))))
                    derived = true;
            }
        if (!derived && std::find(roots.begin(), roots.end(), candidate) == roots.end())
            roots.push_back(candidate);
    }
    return roots;
}
void discover(const std::string &root, const std::string &logical_dir, bool recursive,
              std::set<std::string> &names) {
    const auto host = logical_dir.empty() ? root : zutl::any_case(root, logical_dir);
    std::error_code error;
    if (host.empty() || !fs::is_directory(platform::host_path(fs::u8path(host)), error))
        return;
    auto add = [&](const fs::directory_entry &entry) {
        const auto relative = platform::logical_host_path(entry.path())
                                  .lexically_relative(fs::u8path(host))
                                  .generic_string();
        const auto logical = lower(logical_dir.empty() ? relative : logical_dir + "/" + relative);
        if (!relevant(logical))
            return;
        if (entry.is_symlink(error))
            cc::content::fail("base-content",
                              "Linked native gameplay inputs cannot be fingerprinted", {}, {},
                              logical);
        if (entry.is_regular_file(error))
            names.insert(logical);
        if (names.size() > 65536)
            cc::content::fail("base-content-limit",
                              "Native gameplay input inventory exceeds 65536 files");
    };
    if (recursive) {
        fs::recursive_directory_iterator it(platform::host_path(fs::u8path(host)),
                                            fs::directory_options::none, error),
            end;
        size_t visited = 0;
        for (; it != end && !error; it.increment(error)) {
            if (it.depth() > 8 || ++visited > 131072)
                cc::content::fail("base-content-limit",
                                  "Native world directory inventory exceeds limits", {}, {},
                                  logical_dir);
            if (it->is_directory(error) && it->is_symlink(error))
                cc::content::fail("base-content",
                                  "Linked native world directories cannot be fingerprinted", {}, {},
                                  logical_dir);
            add(*it);
        }
    } else {
        fs::directory_iterator it(platform::host_path(fs::u8path(host)), error), end;
        size_t visited = 0;
        for (; it != end && !error; it.increment(error)) {
            if (++visited > 131072)
                cc::content::fail("base-content-limit", "Native directory inventory exceeds limits",
                                  {}, {}, logical_dir);
            add(*it);
        }
    }
    if (error)
        cc::content::fail("base-content", "Cannot enumerate native gameplay input directory", {},
                          {}, logical_dir);
}
} // namespace

void clear_content_base_digest_cache() {
    g_key.clear();
    g_digest.clear();
}
void validate_content_script_source(const char *name) {
    if (!active_content())
        return;
    const auto host = zutl::find(name);
    if (host.empty())
        return; // preserve optional missing native source behavior
    const auto logical = lower(zutl::host(name));
    if (!script(logical))
        cc::content::fail("content-source",
                          "Managed loose native sources require .gs, .gw or .zan; cosmetic data "
                          "cannot execute as a script",
                          {}, {}, logical);
    std::set<std::string> worlds;
    for (int i = 0; i < mission_dir_count(); ++i)
        worlds.insert(lower(mission_dir(i)));
    if (!recognized(logical, worlds) && !claimed_gameplay_source(host))
        cc::content::fail("content-source",
                          "Loose native source is outside the fingerprinted native directories and "
                          "has no gameplay package claim",
                          {}, {}, logical);
}
bool content_managed_script_source(const char *name) {
    const auto logical = lower(zutl::host(name));
    return script(logical) && claimed_gameplay_source(zutl::find(name));
}
void validate_content_world_bank(const char *name) {
    if (!active_content())
        return;
    const auto logical = lower(zutl::host(name));
    const auto path = fs::u8path(logical);
    bool relative =
        !logical.empty() && !path.is_absolute() && logical.find(':') == std::string::npos;
    for (const auto &part : path)
        if (part == "." || part == "..")
            relative = false;
    std::set<std::string> worlds;
    for (int i = 0; i < mission_dir_count(); ++i)
        worlds.insert(lower(mission_dir(i)));
    if (!relative || path.extension() != ".zbd" ||
        (!recognized(logical, worlds) && !claimed_gameplay_source(zutl::find(name))))
        cc::content::fail("content-world-bank",
                          "Managed native world reads require a relative .zbd in fingerprinted "
                          "directories or an explicit gameplay package claim",
                          {}, {}, logical);
}
std::string content_base_digest() {
    const auto &mounts = zutl::mounts();
    const auto content = active_content();
    std::set<std::string> worlds;
    for (int i = 0; i < mission_dir_count(); ++i)
        worlds.insert(lower(mission_dir(i)));
    Json snapshot = {{"registry", reinterpret_cast<uintptr_t>(content.get())},
                     {"read", mounts.read},
                     {"write", mounts.write},
                     {"mods", mounts.mods},
                     {"managed", mounts.managed_files},
                     {"managed_search", mounts.managed_search},
                     {"worlds", worlds},
                     {"confined", mounts.confined},
                     {"strict_reads", mounts.strict_reads}};
    const auto key = snapshot.dump();
    if (key == g_key && !g_digest.empty())
        return g_digest;
    std::set<std::string> names;
    const auto roots = original_roots(mounts, worlds);
    for (const auto &root : roots) {
        discover(root, "", false, names);
        discover(root, "zbd", false, names);
        discover(root, "support", true, names);
        for (const auto &world : worlds)
            discover(root, "zbd/" + world, true, names);
    }
    for (const auto &resource : mounts.managed_files)
        if (recognized(resource.first, worlds))
            names.insert(resource.first);
    if (!mounts.read.empty() && zutl::find_all("zbd/reader.zbd").empty())
        cc::content::fail("base-content",
                          "Mounted gameplay session has no required zbd/reader.zbd");
    Json inputs = Json::array();
    uint64_t total = 0;
    size_t copies_hashed = 0;
    for (const auto &logical : names) {
        const auto copies = zutl::find_all(logical.c_str());
        for (size_t ordinal = 0; ordinal < copies.size(); ++ordinal) {
            zutl::verify_managed_read(copies[ordinal]);
            if (++copies_hashed > 131072)
                cc::content::fail("base-content-limit",
                                  "Native gameplay fallback inventory exceeds 131072 files");
            std::error_code error;
            const auto size =
                fs::file_size(platform::host_path(fs::u8path(copies[ordinal])), error);
            if (error)
                cc::content::fail("base-content", "Cannot inspect native gameplay input", {}, {},
                                  logical);
            if (size > 8ull * 1024 * 1024 * 1024 || total > 32ull * 1024 * 1024 * 1024 - size)
                cc::content::fail("base-content-limit",
                                  "Native gameplay inputs exceed hashing budget", {}, {}, logical);
            total += size;
            // file_sha256 streams a 64 KiB buffer; no native banks or media are
            // materialized in memory. Descriptors contain no source-machine paths.
            Json input = {{"path", logical},
                          {"mount_ordinal", ordinal},
                          {"sha256", cc::content::file_sha256(fs::u8path(copies[ordinal]))}};
            if (script(logical) && !claimed_gameplay_source(copies[ordinal])) {
                // Original native loose-vs-interp selection depends on this timestamp.
                // Keep recovered selection semantics for unclaimed installation files.
#if defined(_WIN32)
                struct _stat written;
                const auto status =
                    ::_wstat(platform::host_path(fs::u8path(copies[ordinal])).c_str(), &written);
#else
                struct stat written;
                const auto status = ::stat(copies[ordinal].c_str(), &written);
#endif
                if (status != 0)
                    cc::content::fail("base-content", "Cannot inspect native source timestamp", {},
                                      {}, logical);
                input["source_mtime"] = static_cast<int32_t>(written.st_mtime);
            }
            inputs.push_back(std::move(input));
        }
    }
    g_digest = cc::content::sha256(inputs.dump());
    g_key = key;
    return g_digest;
}
} // namespace mech3
