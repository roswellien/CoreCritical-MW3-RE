#include "content/content.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/source_archive.h"
#include "platform/host_path.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cc::content {
namespace {
struct Staging {
    fs::path path;
    explicit Staging(const fs::path &parent) {
        fs::create_directories(platform::host_path(parent));
        static std::atomic<uint64_t> sequence{0};
        for (int attempt = 0; attempt < 32; ++attempt) {
            std::string name =
                ".staging-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(++sequence);
            fs::path candidate = confined_path(parent, name);
            if (fs::create_directory(platform::host_path(candidate))) {
                path = candidate;
                return;
            }
        }
        fail("store-busy", "Could not reserve a private staging directory");
    }
    ~Staging() {
        if (!path.empty()) {
            std::error_code error;
            fs::remove_all(platform::host_path(path), error);
        }
    }
};
void copy_package(const Package &package, const fs::path &target) {
    for (auto entry = package.manifest.at("files").begin();
         entry != package.manifest.at("files").end(); ++entry) {
        auto from = confined_path(package.root, entry.key()),
             to = confined_path(target, entry.key());
        fs::create_directories(platform::host_path(to.parent_path()));
        fs::copy_file(platform::host_path(from), platform::host_path(to), fs::copy_options::none);
    }
    write_json_atomic(target / "pack.json", package.manifest);
}
void extract(const fs::path &source, const fs::path &target) {
    if (!mech3::source::archives_supported())
        fail("archive-unavailable", "This build does not include package archive support; install "
                                    "a development folder or an archive-enabled runtime");
    mech3::source::ArchiveReader input;
    if (!input.open_file(source.u8string()))
        fail("archive-read", input.error());
    mech3::source::ArchiveReader::Entry entry;
    std::set<std::string> seen;
    uint64_t total = 0;
    while (input.next(&entry)) {
        std::string name = entry.path;
        if (entry.dir && !name.empty() && name.back() == '/')
            name.pop_back();
        auto destination = confined_path(target, name);
        std::string folded = name;
        std::transform(folded.begin(), folded.end(), folded.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!seen.insert(folded).second || seen.size() > 65537)
            fail("duplicate-path", "Duplicate archive entry or entry-count limit", {}, {}, name);
        if (entry.special)
            fail("unsafe-path", "Archive links and special files are not allowed", {}, {}, name);
        if (entry.dir) {
            fs::create_directories(platform::host_path(destination));
            continue;
        }
        if (entry.size < 0 || entry.size > 8ll * 1024 * 1024 * 1024 ||
            total > 32ull * 1024 * 1024 * 1024 - static_cast<uint64_t>(entry.size))
            fail("archive-limit", "Expanded archive exceeds its size limits", {}, {}, name);
        total += static_cast<uint64_t>(entry.size);
        fs::create_directories(platform::host_path(destination.parent_path()));
        std::ofstream output(platform::host_path(destination), std::ios::binary);
        std::array<uint8_t, 65536> buffer{};
        uint64_t copied = 0;
        while (true) {
            auto count = input.read(buffer.data(), buffer.size());
            if (count < 0)
                fail("archive-read", input.error(), {}, {}, name);
            if (!count)
                break;
            copied += static_cast<uint64_t>(count);
            if (copied > static_cast<uint64_t>(entry.size) ||
                !output.write(reinterpret_cast<char *>(buffer.data()), count))
                fail("archive-write", "Expanded size mismatch or write failure", {}, {}, name);
        }
        if (copied != static_cast<uint64_t>(entry.size) || !output.flush())
            fail("archive-size", "Truncated archive entry", {}, {}, name);
    }
    if (!input.error().empty())
        fail("archive-read", input.error());
    auto package = load_package(target, true);
    for (const auto &name : seen)
        if (name != "pack.json") {
            // Directory entries have no file contents to admit; every file must be listed.
            auto file = confined_path(target, name);
            if (fs::is_regular_file(platform::host_path(file))) {
                bool listed = false;
                for (auto it = package.manifest.at("files").begin();
                     it != package.manifest.at("files").end(); ++it) {
                    std::string candidate = it.key();
                    std::transform(
                        candidate.begin(), candidate.end(), candidate.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (candidate == name)
                        listed = true;
                }
                if (!listed)
                    fail("unlisted-resource", "Archive contains an unlisted payload", package.id,
                         {}, name);
            }
        }
}
uint32_t crc_update(uint32_t crc, const char *bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint8_t>(bytes[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    return crc;
}
void u16(std::ostream &output, uint16_t word) {
    for (unsigned i = 0; i < 2; ++i)
        output.put(static_cast<char>(word >> (i * 8)));
}
void u32(std::ostream &output, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i)
        output.put(static_cast<char>(word >> (i * 8)));
}
// A standard stored ZIP writer avoids another compression dependency. Runtime
// assets remain loose after installation. ZIP64 packaging is rejected explicitly.
struct ZipEntry {
    std::string name;
    uint32_t bytes = 0, crc = 0, offset = 0;
};
} // namespace
fs::path install_package(const fs::path &source, const fs::path &store) {
    Staging staging(store);
    if (fs::is_directory(platform::host_path(source)))
        copy_package(load_package(source, true), staging.path);
    else
        extract(source, staging.path);
    Package package = load_package(staging.path, true);
    fs::path parent = confined_path(store, package.id + "/" + package.version);
    fs::create_directories(platform::host_path(parent));
    fs::path target = confined_path(parent, package.digest);
    if (fs::exists(platform::host_path(target))) {
        if (load_package(target, true).digest != package.digest)
            fail("install-integrity",
                 "Installed manifest does not match its immutable store identity", package.id);
        return target;
    }
    std::error_code error;
    fs::rename(platform::host_path(staging.path), platform::host_path(target), error);
    if (error) {
        if (fs::exists(platform::host_path(target)) &&
            load_package(target, true).digest == package.digest)
            return target;
        fail("install-activate", error.message(), package.id);
    }
    staging.path.clear();
    return target;
}
void package_directory(const fs::path &source, const fs::path &output) {
    Package package = load_package(source, true);
    if (fs::exists(platform::host_path(output)))
        fail("output-exists", "Refusing to overwrite an existing package", package.id, {},
             output.generic_string());
    fs::path parent = output.parent_path().empty() ? fs::current_path() : output.parent_path();
    Staging staging(parent);
    fs::path temporary = staging.path / "payload.mw3pack";
    Staging snapshot(parent);
    copy_package(package, snapshot.path);
    package = load_package(snapshot.path, true);
    std::ofstream zip(platform::host_path(temporary), std::ios::binary);
    std::vector<ZipEntry> entries;
    std::vector<std::string> names{"pack.json"};
    for (auto it = package.manifest.at("files").begin(); it != package.manifest.at("files").end();
         ++it)
        names.push_back(it.key());
    if (names.size() > 65535)
        fail("zip-limit", "Stored ZIP packaging supports at most 65535 files", package.id);
    for (const auto &name : names) {
        fs::path file = confined_path(package.root, name);
        uint64_t size = fs::file_size(platform::host_path(file)),
                 offset = static_cast<uint64_t>(zip.tellp());
        if (size > 0xffffffffull || offset + size + name.size() + 64 > 0xffffffffull ||
            name.size() > 65535)
            fail("zip-limit",
                 "This writer requires a package below 4 GiB; split it into dependent packages",
                 package.id, {}, name);
        std::ifstream input(platform::host_path(file), std::ios::binary);
        std::array<char, 65536> buffer{};
        uint32_t crc = 0xffffffffu;
        while (input) {
            input.read(buffer.data(), buffer.size());
            crc = crc_update(crc, buffer.data(), static_cast<size_t>(input.gcount()));
        }
        if (!input.eof())
            fail("file-read", "Could not checksum payload", package.id, {}, name);
        crc ^= 0xffffffffu;
        ZipEntry entry{name, static_cast<uint32_t>(size), crc, static_cast<uint32_t>(offset)};
        entries.push_back(entry);
        u32(zip, 0x04034b50);
        u16(zip, 20);
        u16(zip, 0x0800);
        u16(zip, 0);
        u16(zip, 0);
        u16(zip, 0x21);
        u32(zip, crc);
        u32(zip, entry.bytes);
        u32(zip, entry.bytes);
        u16(zip, static_cast<uint16_t>(name.size()));
        u16(zip, 0);
        zip << name;
        input.clear();
        input.seekg(0);
        if (size)
            zip << input.rdbuf();
        if (!zip)
            fail("package-write", "Failed to write archive", package.id);
    }
    uint32_t central = static_cast<uint32_t>(zip.tellp());
    for (const auto &entry : entries) {
        u32(zip, 0x02014b50);
        u16(zip, 20);
        u16(zip, 20);
        u16(zip, 0x0800);
        u16(zip, 0);
        u16(zip, 0);
        u16(zip, 0x21);
        u32(zip, entry.crc);
        u32(zip, entry.bytes);
        u32(zip, entry.bytes);
        u16(zip, static_cast<uint16_t>(entry.name.size()));
        u16(zip, 0);
        u16(zip, 0);
        u16(zip, 0);
        u16(zip, 0);
        u32(zip, 0);
        u32(zip, entry.offset);
        zip << entry.name;
    }
    uint64_t end = static_cast<uint64_t>(zip.tellp());
    if (end > 0xffffffffull)
        fail("zip-limit", "Archive directory exceeds ZIP32 limits", package.id);
    u32(zip, 0x06054b50);
    u16(zip, 0);
    u16(zip, 0);
    u16(zip, static_cast<uint16_t>(entries.size()));
    u16(zip, static_cast<uint16_t>(entries.size()));
    u32(zip, static_cast<uint32_t>(end - central));
    u32(zip, central);
    u16(zip, 0);
    if (!zip.flush())
        fail("package-write", "Could not finish package", package.id);
    zip.close();
#if defined(_WIN32)
    if (!MoveFileExW(platform::host_path(temporary).c_str(), platform::host_path(output).c_str(),
                     MOVEFILE_WRITE_THROUGH))
        fail("package-activate", "Cannot activate package without overwriting another file",
             package.id);
#else
    fs::create_hard_link(platform::host_path(temporary), platform::host_path(output));
#endif
}
} // namespace cc::content
