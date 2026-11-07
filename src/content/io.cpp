#include "content/content.h"
#include "platform/host_path.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <set>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cc::content {
Json Diagnostic::json() const {
    return {{"code", code},       {"message", message}, {"package", package},
            {"content", content}, {"field", field},     {"other_owner", other_owner}};
}
Error::Error(Diagnostic value) : std::runtime_error(value.message), diagnostic(std::move(value)) {}
[[noreturn]] void fail(const std::string &code, const std::string &message,
                       const std::string &package, const std::string &content,
                       const std::string &field, const std::string &other_owner) {
    throw Error({code, message, package, content, field, other_owner});
}
Json parse_json(const std::string &text, const std::string &source) {
    if (text.size() > 16 * 1024 * 1024)
        fail("json-limit", "JSON document exceeds 16 MiB", {}, {}, source);
    std::vector<std::set<std::string>> objects;
    size_t nodes = 0;
    try {
        return Json::parse(text, [&](int depth, Json::parse_event_t event, Json &value) {
            if (depth > 64 || ++nodes > 1000000)
                fail("json-limit", "JSON depth or node limit exceeded", {}, {}, source);
            if (value.is_string() && value.get_ref<const std::string &>().size() > 1024 * 1024)
                fail("json-limit", "JSON string exceeds 1 MiB", {}, {}, source);
            if (event == Json::parse_event_t::object_start)
                objects.emplace_back();
            if (event == Json::parse_event_t::key &&
                !objects.back().insert(value.get<std::string>()).second)
                fail("duplicate-key", "Duplicate JSON key: " + value.get<std::string>(), {}, {},
                     source);
            if (event == Json::parse_event_t::object_end)
                objects.pop_back();
            return true;
        });
    } catch (const Error &) {
        throw;
    } catch (const Json::exception &error) {
        fail("invalid-json", error.what(), {}, {}, source);
    }
}
Json read_json(const fs::path &path) {
    std::error_code error;
    auto size = fs::file_size(platform::host_path(path), error);
    if (error || size > 16 * 1024 * 1024)
        fail("file-read", "Missing or oversized JSON file", {}, {}, path.generic_u8string());
    std::ifstream input(platform::host_path(path), std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        fail("file-read", "Could not read JSON file", {}, {}, path.generic_u8string());
    return parse_json(bytes, path.generic_u8string());
}
void write_json_atomic(const fs::path &path, const Json &value) {
    std::error_code error;
    if (!path.parent_path().empty())
        fs::create_directories(platform::host_path(path.parent_path()), error);
    if (error)
        fail("write-failed", error.message(), {}, {}, path.generic_u8string());
    static std::atomic<unsigned long> sequence{0};
#if defined(_WIN32)
    auto process = GetCurrentProcessId();
#else
    auto process = getpid();
#endif
    fs::path staging = path;
    staging += ".new-" + std::to_string(process) + "-" + std::to_string(++sequence);
    try {
        {
            std::ofstream output(platform::host_path(staging), std::ios::binary | std::ios::trunc);
            auto bytes = value.dump(2) + "\n";
            if (!output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
                !output.flush())
                fail("write-failed", "Could not finish staged file", {}, {},
                     path.generic_u8string());
        }
        if (!platform::sync_file(staging))
            fail("write-failed", "Could not sync staged file", {}, {}, path.generic_u8string());
#if defined(_WIN32)
        if (!MoveFileExW(platform::host_path(staging).c_str(), platform::host_path(path).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            fail("write-failed", "Could not atomically activate staged file", {}, {},
                 path.generic_u8string());
#else
        fs::rename(platform::host_path(staging), platform::host_path(path));
#endif
    } catch (...) {
        fs::remove(platform::host_path(staging), error);
        throw;
    }
}
bool valid_id(const std::string &id) {
    if (id.empty() || id.size() > 128 || id == "." || id.find("..") != std::string::npos)
        return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}
bool valid_content_id(const std::string &id) {
    auto colon = id.find(':');
    if (colon == std::string::npos || id.size() > 256 || !valid_id(id.substr(0, colon)) ||
        colon + 1 == id.size())
        return false;
    auto local = id.substr(colon + 1);
    if (local.find("..") != std::string::npos)
        return false;
    return std::all_of(local.begin(), local.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '.' || c == '/';
    });
}
fs::path confined_path(const fs::path &root, const std::string &relative) {
    if (relative.empty() || relative.find_first_of(":\\\0", 0, 3) != std::string::npos ||
        relative.front() == '/')
        fail("unsafe-path", "Expected a package-relative path", {}, {}, relative);
    fs::path value = fs::u8path(relative);
    // Use one portable spelling before either extraction or ownership lookup.
    // Windows otherwise aliases trailing dots/spaces and device basenames;
    // ASCII payload names also make case-folded identity platform independent.
    for (unsigned char c : relative)
        if (c < 0x20 || c > 0x7e || std::string("<>\"|?*").find(char(c)) != std::string::npos)
            fail("unsafe-path", "Package paths require portable ASCII filenames", {}, {}, relative);
    if (value.lexically_normal().generic_u8string() != relative)
        fail("unsafe-path", "Package paths require one normalized spelling", {}, {}, relative);
    for (const auto &part : value) {
        if (part == ".." || part == ".")
            fail("unsafe-path", "Path traversal refused", {}, {}, relative);
        const auto segment = part.generic_u8string();
        if (segment.empty() || segment.back() == '.' || segment.back() == ' ')
            fail("unsafe-path", "Trailing dots, spaces and empty path segments are not allowed", {},
                 {}, relative);
        auto stem = segment.substr(0, segment.find('.'));
        while (!stem.empty() && stem.back() == ' ')
            stem.pop_back();
        for (char &c : stem)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
             stem[3] >= '0' && stem[3] <= '9'))
            fail("unsafe-path", "Reserved device filenames are not allowed", {}, {}, relative);
    }
    fs::path base = platform::logical_host_path(fs::weakly_canonical(platform::host_path(root))),
             result = platform::logical_host_path(
                 fs::weakly_canonical(platform::host_path(base / value)));
    auto below = result.lexically_relative(base);
    if (below.empty() || below.is_absolute() || *below.begin() == "..")
        fail("unsafe-path", "Path escapes package", {}, {}, relative);
    fs::path current = base;
    for (const auto &part : value) {
        current /= part;
        std::error_code error;
        auto status = fs::symlink_status(platform::host_path(current), error);
        if (!error && fs::is_symlink(status))
            fail("unsafe-path", "Package links are not allowed", {}, {}, relative);
#if defined(_WIN32)
        DWORD attributes = GetFileAttributesW(platform::host_path(current).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fail("unsafe-path", "Package reparse points are not allowed", {}, {}, relative);
#endif
    }
    return result;
}
const Definition *ResolvedContentSet::find(const std::string &kind, const std::string &id) const {
    for (const auto &value : definitions)
        if (value.kind == kind && value.id == id)
            return &value;
    return nullptr;
}
const Definition *ResolvedContentSet::by_handle(const std::string &kind, int32_t handle) const {
    for (const auto &value : definitions)
        if (value.kind == kind && value.handle == handle)
            return &value;
    return nullptr;
}
std::vector<const Definition *> ResolvedContentSet::of_kind(const std::string &kind) const {
    std::vector<const Definition *> result;
    for (const auto &value : definitions)
        if (value.kind == kind)
            result.push_back(&value);
    return result;
}
ResourceReference resource_reference(const std::string &owner, const std::string &value) {
    ResourceReference result{owner, value};
    auto separator = value.find("::");
    if (separator != std::string::npos) {
        if (value.find("::", separator + 2) != std::string::npos)
            fail("resource-owner", "A resource may have exactly one package qualifier", owner, {},
                 value);
        result.owner = value.substr(0, separator);
        result.path = value.substr(separator + 2);
    }
    if (!valid_id(result.owner) || result.path.empty())
        fail("resource-owner", "Invalid resource identity", owner, {}, value);
    return result;
}
fs::path ResolvedContentSet::resource(const std::string &owner, const std::string &relative) const {
    const auto reference = resource_reference(owner, relative);
    for (const auto &package : packages)
        if (package.id == reference.owner) {
            if (!package.manifest.at("files").contains(reference.path))
                fail("unlisted-resource", "Resource is absent from its package integrity table",
                     reference.owner, {}, reference.path);
            auto path = confined_path(package.root, reference.path);
            const auto &expected = package.manifest.at("files").at(reference.path);
            std::error_code error;
            if (!fs::is_regular_file(platform::host_path(path), error) ||
                fs::file_size(platform::host_path(path), error) != expected.at("bytes") ||
                file_sha256(path) != expected.at("sha256"))
                fail("resource-changed",
                     "Resource changed after profile resolution; unload the session "
                     "and resolve "
                     "again",
                     reference.owner, {}, reference.path);
            return path;
        }
    fail("unknown-package", "Resource owner is unavailable", reference.owner, {}, reference.path);
}
Json inspect(const ResolvedContentSet &resolved) {
    Json definitions = Json::array();
    for (const auto &value : resolved.definitions)
        definitions.push_back({{"kind", value.kind},
                               {"id", value.id},
                               {"owner", value.owner},
                               {"behavior", value.behavior},
                               {"handle", value.handle},
                               {"data", value.data}});
    return {{"lock", resolved.lock},
            {"gameplay_digest", resolved.gameplay_digest},
            {"session_digest", resolved.session_digest},
            {"definitions", std::move(definitions)}};
}
} // namespace cc::content
