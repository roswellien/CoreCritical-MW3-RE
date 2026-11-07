#include "content/content.h"
#include "content/legacy.h"
#include "platform/host_path.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <sstream>

namespace cc::content {
namespace {
constexpr size_t kPackageLimit = 256, kDefinitionLimit = 65536;
struct Version {
    uint64_t major = 0, minor = 0, patch = 0;
    std::string prerelease;
};
Version version(const std::string &value) {
    Version result;
    std::string plain = value.substr(0, value.find('+'));
    size_t dash = plain.find('-');
    if (dash != std::string::npos) {
        result.prerelease = plain.substr(dash + 1);
        plain.resize(dash);
    }
    std::istringstream parts(plain);
    uint64_t *output[] = {&result.major, &result.minor, &result.patch};
    for (size_t i = 0; i < 3; ++i) {
        std::string part;
        if (!std::getline(parts, part, '.') || part.empty() || part.size() > 9 ||
            (part.size() > 1 && part[0] == '0') ||
            !std::all_of(part.begin(), part.end(),
                         [](unsigned char c) { return c >= '0' && c <= '9'; }))
            fail("invalid-version", "Expected a semantic version: " + value);
        *output[i] = std::stoull(part);
    }
    std::string extra;
    if (std::getline(parts, extra, '.') || value.empty() ||
        !std::all_of(
            value.begin(), value.end(),
            [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-' || c == '+'; }) ||
        (dash != std::string::npos && result.prerelease.empty()))
        fail("invalid-version", "Invalid semantic version: " + value);
    return result;
}
int compare(const std::string &left, const std::string &right) {
    auto a = version(left), b = version(right);
    for (auto pair : {std::make_pair(a.major, b.major), std::make_pair(a.minor, b.minor),
                      std::make_pair(a.patch, b.patch)})
        if (pair.first != pair.second)
            return pair.first < pair.second ? -1 : 1;
    if (a.prerelease.empty() != b.prerelease.empty())
        return a.prerelease.empty() ? 1 : -1;
    std::istringstream ap(a.prerelease), bp(b.prerelease);
    while (true) {
        std::string x, y;
        bool ax = bool(std::getline(ap, x, '.')), by = bool(std::getline(bp, y, '.'));
        if (!ax || !by)
            return ax ? 1 : by ? -1 : 0;
        if (x == y)
            continue;
        auto numeric = [](const std::string &s) {
            return !s.empty() && std::all_of(s.begin(), s.end(),
                                             [](unsigned char c) { return c >= '0' && c <= '9'; });
        };
        bool nx = numeric(x), ny = numeric(y);
        if (nx && ny) {
            if (x.size() != y.size())
                return x.size() < y.size() ? -1 : 1;
        } else if (nx != ny)
            return nx ? -1 : 1;
        return x < y ? -1 : 1;
    }
}
bool matches(const Package &package, const Json &requirement) {
    if (requirement.contains("path") &&
        package.root != platform::logical_host_path(fs::weakly_canonical(platform::host_path(
                            fs::u8path(requirement.at("path").get<std::string>())))))
        return false;
    if (requirement.contains("version") &&
        package.version != requirement.at("version").get<std::string>())
        return false;
    if (requirement.contains("sha256") &&
        package.digest != requirement.at("sha256").get<std::string>())
        return false;
    if (requirement.contains("min_version") &&
        compare(package.version, requirement.at("min_version")) < 0)
        return false;
    if (requirement.contains("max_version") &&
        compare(package.version, requirement.at("max_version")) >= 0)
        return false;
    return true;
}
bool digest_string(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string key(const Definition &definition) { return definition.kind + "\n" + definition.id; }
std::vector<fs::path> package_roots(const fs::path &store) {
    std::vector<fs::path> roots;
    std::error_code error;
    if (!fs::exists(platform::host_path(store), error))
        return roots;
    fs::recursive_directory_iterator it(platform::host_path(store), fs::directory_options::none,
                                        error),
        end;
    for (; it != end && !error; it.increment(error)) {
        if (it->is_directory() && it->path().filename().string().rfind('.', 0) == 0) {
            it.disable_recursion_pending();
            continue;
        }
        if (it.depth() > 3) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink()) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && it->path().filename() == "pack.json")
            roots.push_back(platform::logical_host_path(it->path()).parent_path());
        if (roots.size() > 4096)
            fail("package-limit", "Installed package inventory exceeds 4096 versions");
    }
    if (error)
        fail("store-read", error.message(), {}, {}, store.generic_u8string());
    std::sort(roots.begin(), roots.end());
    return roots;
}
void flatten(const Json &changes, const std::string &prefix, std::map<std::string, Json> &leaves) {
    if (changes.is_object() && !changes.empty()) {
        for (auto it = changes.begin(); it != changes.end(); ++it) {
            std::string name = it.key();
            size_t at = 0;
            while ((at = name.find('~', at)) != std::string::npos) {
                name.replace(at, 1, "~0");
                at += 2;
            }
            at = 0;
            while ((at = name.find('/', at)) != std::string::npos) {
                name.replace(at, 1, "~1");
                at += 2;
            }
            flatten(it.value(), prefix + "/" + name, leaves);
        }
    } else
        leaves[prefix] = changes;
}
bool cosmetic(const Definition &definition) {
    return definition.kind == "hud" || definition.kind == "skin" || definition.kind == "audio" ||
           definition.kind == "appearance" || definition.kind == "texture" ||
           definition.kind == "legacy_layer";
}
void resource_hashes(const Json &value, const std::string &owner,
                     const std::vector<Package> &packages, Json &hashes, size_t &work) {
    if (++work > 1000000)
        fail("resource-limit", "Gameplay resource reference work exceeds the profile limit");
    if (value.is_string()) {
        auto name = value.get<std::string>();
        std::string selected_owner = owner;
        auto separator = name.find("::");
        if (separator != std::string::npos) {
            selected_owner = name.substr(0, separator);
            name = name.substr(separator + 2);
        }
        std::vector<std::pair<std::string, std::string>> pending{{selected_owner, name}};
        for (size_t at = 0; at < pending.size(); ++at) {
            if (++work > 1000000)
                fail("resource-limit", "Gameplay resource closure exceeds the profile limit");
            const auto reference = pending[at];
            const auto qualified = reference.first + "::" + reference.second;
            if (hashes.contains(qualified))
                continue;
            for (const auto &package : packages)
                if (package.id == reference.first) {
                    const auto &files = package.manifest.at("files");
                    if (!files.contains(reference.second))
                        continue;
                    hashes[qualified] = files.at(reference.second).at("sha256");
                    const auto path = confined_path(package.root, reference.second);
                    auto extension = path.extension().string();
                    std::transform(
                        extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    Json document;
                    if (extension == ".gltf")
                        document = read_json(path);
                    else if (extension == ".glb") {
                        std::ifstream input(platform::host_path(path), std::ios::binary);
                        std::array<uint8_t, 20> header{};
                        if (!input.read(reinterpret_cast<char *>(header.data()), header.size()))
                            fail("gltf-header", "Truncated GLB resource", package.id);
                        auto word = [&](size_t offset) {
                            return uint32_t(header[offset]) | (uint32_t(header[offset + 1]) << 8) |
                                   (uint32_t(header[offset + 2]) << 16) |
                                   (uint32_t(header[offset + 3]) << 24);
                        };
                        uint32_t size = word(12);
                        if (word(0) != 0x46546c67 || word(4) != 2 || word(16) != 0x4e4f534a ||
                            size > 16 * 1024 * 1024)
                            fail("gltf-header", "Invalid GLB JSON chunk", package.id);
                        std::string text(size, '\0');
                        if (!input.read(text.data(), size))
                            fail("gltf-header", "Truncated GLB JSON chunk", package.id);
                        document = parse_json(text, "GLB resource closure");
                    } else
                        continue;
                    for (const char *field : {"buffers", "images"})
                        for (const auto &asset : document.value(field, Json::array()))
                            if (asset.contains("uri")) {
                                const auto uri = asset.at("uri").get<std::string>();
                                const auto dependency =
                                    (fs::path(reference.second).parent_path() / fs::u8path(uri))
                                        .generic_u8string();
                                confined_path(package.root, dependency);
                                if (!files.contains(dependency))
                                    fail("unlisted-resource",
                                         "glTF dependency is absent from package integrity table",
                                         package.id, {}, dependency);
                                pending.emplace_back(package.id, dependency);
                            }
                }
        }
    } else if (value.is_structured()) {
        for (const auto &child : value)
            resource_hashes(child, owner, packages, hashes, work);
    }
}
} // namespace

Package load_package(const fs::path &root, bool verify_files) try {
    if (auto legacy = try_load_legacy(root, verify_files))
        return std::move(*legacy);
    Package package;
    package.root = platform::logical_host_path(fs::weakly_canonical(platform::host_path(root)));
    package.manifest = read_json(confined_path(package.root, "pack.json"));
    const Json &manifest = package.manifest;
    if (!manifest.is_object() || !manifest.contains("schema_version") ||
        !manifest.at("schema_version").is_number_integer() || manifest.at("schema_version") != 3)
        fail("unsupported-schema",
             "This importer requires modular pack schema 3; use the legacy pack adapter for older "
             "packs",
             {}, {}, "schema_version");
    package.id = manifest.at("id").get<std::string>();
    package.version = manifest.at("version").get<std::string>();
    if (!valid_id(package.id) || package.id == "stock")
        fail("invalid-package-id", "Invalid or reserved package identity", package.id);
    version(package.version);
    if (manifest.value("game_id", std::string("mw3")) != "mw3")
        fail("wrong-game", "Package does not target MW3", package.id);
    if (!manifest.contains("files") || !manifest.at("files").is_object())
        fail("file-table", "A package requires a file integrity table", package.id);
    std::set<std::string> names;
    uint64_t total = 0;
    for (auto file = manifest.at("files").begin(); file != manifest.at("files").end(); ++file) {
        auto path = confined_path(package.root, file.key());
        std::string folded = file.key();
        std::transform(folded.begin(), folded.end(), folded.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (folded == "pack.json" || !names.insert(folded).second)
            fail("duplicate-path", "Duplicate or reserved payload path", package.id, {},
                 file.key());
        if (!file.value().is_object() || !file.value().contains("bytes") ||
            !file.value().at("bytes").is_number_unsigned()) {
            // JSON positive integer literals may use the signed representation in authored tests.
            if (!file.value().is_object() || !file.value().contains("bytes") ||
                !file.value().at("bytes").is_number_integer() ||
                file.value().at("bytes").get<int64_t>() < 0)
                fail("file-table", "Invalid payload size", package.id, {}, file.key());
        }
        uint64_t size = file.value().at("bytes").get<uint64_t>();
        if (size > 8ull * 1024 * 1024 * 1024 || total > 32ull * 1024 * 1024 * 1024 - size)
            fail("payload-limit", "Package exceeds expanded size limits", package.id);
        total += size;
        if (!file.value().contains("sha256") || !file.value().at("sha256").is_string() ||
            !digest_string(file.value().at("sha256")))
            fail("file-table", "Invalid payload SHA-256", package.id, {}, file.key());
        if (verify_files) {
            std::error_code error;
            if (!fs::is_regular_file(platform::host_path(path), error) ||
                fs::file_size(platform::host_path(path), error) != size ||
                file_sha256(path) != file.value().at("sha256"))
                fail("payload-integrity", "Missing, changed or corrupt payload", package.id, {},
                     file.key());
        }
    }
    if (names.size() > 65536)
        fail("payload-limit", "Too many payload files", package.id);
    const auto dependencies = manifest.value("dependencies", Json::array());
    if (!dependencies.is_array() || dependencies.size() > 256)
        fail("dependency-limit", "A package supports at most 256 dependency constraints",
             package.id);
    std::set<std::string> dependency_ids;
    for (const auto &dependency : dependencies) {
        const auto id = dependency.at("id").get<std::string>();
        if (!valid_id(id) || !dependency_ids.insert(id).second)
            fail("dependency", "Dependency IDs must be valid and unique", package.id, {}, id);
        for (const char *field : {"version", "min_version", "max_version"})
            if (dependency.contains(field))
                version(dependency.at(field).get<std::string>());
    }
    Json definitions = manifest.value("definitions", Json::array());
    if (!definitions.is_array() || definitions.size() > kDefinitionLimit)
        fail("definitions", "Invalid definition list", package.id);
    for (const auto &value : definitions) {
        Definition definition;
        definition.kind = value.at("kind").get<std::string>();
        definition.id = value.at("id").get<std::string>();
        definition.behavior = value.at("behavior").get<std::string>();
        definition.owner = package.id;
        definition.data = value.at("data");
        if (!valid_id(definition.kind) || !valid_content_id(definition.id) ||
            definition.id.substr(0, definition.id.find(':')) != package.id ||
            !definition.data.is_object() || definition.behavior.empty())
            fail("definition", "Invalid definition identity, behavior or data", package.id,
                 definition.id);
        package.definitions.push_back(std::move(definition));
    }
    package.digest = sha256(manifest.dump());
    return package;
} catch (const Error &) {
    throw;
} catch (const std::exception &error) {
    fail("invalid-package", error.what(), root.filename().string(), {}, "pack.json");
}

Json installed_packages(const fs::path &store) {
    Json result = Json::array();
    for (const auto &root : package_roots(store)) {
        auto package = load_package(root, false);
        result.push_back({{"id", package.id},
                          {"version", package.version},
                          {"sha256", package.digest},
                          {"path", root.generic_u8string()},
                          {"name", package.manifest.value("name", package.id)}});
    }
    return result;
}

ResolvedContentSet resolve(const Json &profile, const fs::path &profile_root, const fs::path &store,
                           const std::vector<Definition> &stock,
                           const Capabilities &capabilities) try {
    if (!profile.is_object() || !profile.contains("schema_version") ||
        !profile.at("schema_version").is_number_integer() || profile.at("schema_version") != 1 ||
        !profile.contains("packages") || !profile.at("packages").is_array())
        fail("profile-schema", "Expected content profile schema 1 with a package list");
    if (profile.at("packages").size() > kPackageLimit)
        fail("package-limit", "Too many enabled packages");
    std::map<std::string, std::vector<Package>> available;
    uint64_t metadata_bytes = 0;
    std::set<std::string> explicit_ids;
    std::vector<Json> requirements;
    for (auto request : profile.at("packages")) {
        std::string id = request.at("id").get<std::string>();
        if (!valid_id(id) || !explicit_ids.insert(id).second)
            fail("profile-package", "Invalid or duplicate profile package", id);
        if (request.contains("path")) {
            fs::path path = fs::u8path(request.at("path").get<std::string>());
            if (path.is_relative())
                path = profile_root / path;
            if (fs::exists(platform::host_path(path / "pack.json")))
                metadata_bytes += fs::file_size(platform::host_path(path / "pack.json"));
            if (metadata_bytes > 128ull * 1024 * 1024)
                fail("inventory-limit",
                     "Profile package metadata exceeds the 128 MiB inventory budget");
            request["path"] =
                platform::logical_host_path(fs::weakly_canonical(platform::host_path(path)))
                    .generic_u8string();
            auto package = load_package(path, false);
            if (package.id != id)
                fail("package-identity", "Profile path has a different package identity", id);
            available[id].push_back(std::move(package));
        }
        requirements.push_back(std::move(request));
    }
    for (const auto &root : package_roots(store)) {
        metadata_bytes += fs::file_size(platform::host_path(root / "pack.json"));
        if (metadata_bytes > 128ull * 1024 * 1024)
            fail("inventory-limit",
                 "Installed package metadata exceeds the 128 MiB inventory budget");
        auto package = load_package(root, false);
        auto &choices = available[package.id];
        if (std::none_of(choices.begin(), choices.end(),
                         [&](const Package &p) { return p.digest == package.digest; }))
            choices.push_back(std::move(package));
    }
    for (auto &entry : available)
        std::sort(entry.second.begin(), entry.second.end(), [](const Package &a, const Package &b) {
            int order = compare(a.version, b.version);
            return order ? order > 0 : a.digest < b.digest;
        });
    std::map<std::string, Package> selected;
    size_t attempts = 0;
    bool cyclic_candidate = false;
    std::string unavailable_capability, unavailable_owner, conflicting_owner, conflicting_peer;
    auto acyclic = [&]() {
        for (const auto &entry : selected) {
            const Json required =
                entry.second.manifest.value("required_capabilities", Json::object());
            if (!required.is_object())
                fail("capabilities", "Expected required capability versions", entry.first);
            for (auto it = required.begin(); it != required.end(); ++it) {
                auto actual = capabilities.find(it.key());
                if (!it.value().is_number_integer())
                    fail("capabilities", "Capability version must be an integer", entry.first, {},
                         it.key());
                if (actual == capabilities.end() || it.value() != actual->second) {
                    unavailable_capability = it.key();
                    unavailable_owner = entry.first;
                    return false;
                }
            }
            for (const auto &conflict : entry.second.manifest.value("conflicts", Json::array()))
                if (selected.count(conflict.get<std::string>())) {
                    conflicting_owner = entry.first;
                    conflicting_peer = conflict.get<std::string>();
                    return false;
                }
        }
        for (const auto &entry : selected)
            for (const auto &dependency :
                 entry.second.manifest.value("dependencies", Json::array())) {
                auto target = selected.find(dependency.at("id").get<std::string>());
                if (target != selected.end() && !matches(target->second, dependency))
                    return false;
            }
        std::map<std::string, int> colors;
        std::function<bool(const std::string &)> walk = [&](const std::string &id) {
            if (colors[id] == 1)
                return false;
            if (colors[id] == 2)
                return true;
            colors[id] = 1;
            for (const auto &dependency :
                 selected.at(id).manifest.value("dependencies", Json::array())) {
                std::string target = dependency.at("id").get<std::string>();
                if (selected.count(target) && !walk(target))
                    return false;
            }
            colors[id] = 2;
            return true;
        };
        for (const auto &entry : selected)
            if (!walk(entry.first)) {
                cyclic_candidate = true;
                return false;
            }
        return true;
    };
    std::function<bool(size_t, std::vector<Json> &)> solve = [&](size_t at,
                                                                 std::vector<Json> &pending) {
        if (++attempts > 100000)
            fail("resolution-limit", "Dependency resolution exceeded its work limit");
        // Repeated constraints do not add call-stack depth. Only choosing another
        // distinct package recurses, bounded by kPackageLimit.
        while (at < pending.size()) {
            auto existing = selected.find(pending[at].at("id").get<std::string>());
            if (existing == selected.end())
                break;
            if (!matches(existing->second, pending[at]))
                return false;
            ++at;
        }
        if (at == pending.size())
            return acyclic();
        Json request = pending[at];
        std::string id = request.at("id").get<std::string>();
        auto candidates = available.find(id);
        if (candidates == available.end())
            return false;
        for (const Package &candidate : candidates->second) {
            if (!matches(candidate, request))
                continue;
            if (selected.size() >= kPackageLimit)
                fail("package-limit", "Too many resolved dependencies");
            selected[id] = candidate;
            size_t previous = pending.size();
            auto dependencies = candidate.manifest.value("dependencies", Json::array());
            if (!dependencies.is_array())
                fail("dependency", "Expected dependency list", id);
            for (const auto &dependency : dependencies) {
                std::string target = dependency.at("id").get<std::string>();
                if (!valid_id(target))
                    fail("dependency", "Invalid dependency identity", id, {}, target);
                if (!dependency.value("optional", false) || explicit_ids.count(target)) {
                    if (pending.size() >= 4096)
                        fail("dependency-limit",
                             "An active profile supports at most 4096 dependency constraints");
                    pending.push_back(dependency);
                }
            }
            if (solve(at + 1, pending))
                return true;
            pending.resize(previous);
            selected.erase(id);
        }
        return false;
    };
    if (!solve(0, requirements)) {
        if (!unavailable_capability.empty())
            fail("unsupported-capability",
                 "No selected package version has compatible required capabilities",
                 unavailable_owner, {}, unavailable_capability);
        if (!conflicting_owner.empty())
            fail("package-conflict", "No compatible package version avoids the declared conflict",
                 conflicting_owner, {}, {}, conflicting_peer);
        fail(cyclic_candidate ? "dependency-cycle" : "dependencies",
             "Required package versions/hashes have no compatible acyclic resolution");
    }
    std::vector<std::string> order;
    std::map<std::string, int> visited;
    std::function<void(const std::string &)> visit = [&](const std::string &id) {
        if (visited[id] == 1)
            fail("dependency-cycle", "Dependency cycle includes " + id, id);
        if (visited[id] == 2)
            return;
        visited[id] = 1;
        for (const auto &dependency :
             selected.at(id).manifest.value("dependencies", Json::array())) {
            std::string target = dependency.at("id").get<std::string>();
            if (selected.count(target))
                visit(target);
        }
        visited[id] = 2;
        order.push_back(id);
    };
    for (const auto &entry : selected)
        visit(entry.first);
    ResolvedContentSet result;
    result.definitions = stock;
    std::map<std::string, size_t> indices;
    for (size_t i = 0; i < stock.size(); ++i) {
        if (!indices.emplace(key(stock[i]), i).second)
            fail("stock-registry", "Duplicate stock definition", {}, stock[i].id);
    }
    for (const auto &id : order) {
        Package package = load_package(selected.at(id).root, true);
        Json required = package.manifest.value("required_capabilities", Json::object());
        if (!required.is_object())
            fail("capabilities", "Expected required capability versions", id);
        for (auto it = required.begin(); it != required.end(); ++it) {
            auto actual = capabilities.find(it.key());
            if (!it.value().is_number_integer() || actual == capabilities.end() ||
                it.value() != actual->second)
                fail("unsupported-capability", "Runtime does not support required capability", id,
                     {}, it.key());
        }
        for (const auto &conflict : package.manifest.value("conflicts", Json::array()))
            if (selected.count(conflict.get<std::string>()))
                fail("package-conflict", "Package declares conflict with enabled package", id, {},
                     {}, conflict);
        for (const auto &definition : package.definitions) {
            if (result.definitions.size() >= kDefinitionLimit)
                fail("definition-limit", "Too many active definitions");
            if (!indices.emplace(key(definition), result.definitions.size()).second)
                fail("duplicate-definition",
                     "Additions cannot replace existing definitions; declare a patch", id,
                     definition.id);
            result.definitions.push_back(definition);
        }
        result.packages.push_back(std::move(package));
    }
    struct Edit {
        std::string owner;
        Json value;
        std::set<std::string> overrides;
    };
    std::map<std::pair<size_t, std::string>, std::vector<Edit>> edits;
    for (const auto &package : result.packages) {
        for (const auto &patch : package.manifest.value("patches", Json::array())) {
            std::string kind = patch.at("kind"), id = patch.at("id");
            auto target = indices.find(kind + "\n" + id);
            if (target == indices.end())
                fail("patch-target", "Patch target does not exist", package.id, id);
            if (!patch.at("changes").is_object() || patch.at("changes").empty())
                fail("patch-fields", "Patch changes must be a nonempty object", package.id, id);
            std::set<std::string> overrides;
            for (const auto &owner : patch.value("overrides", Json::array()))
                overrides.insert(owner.get<std::string>());
            std::map<std::string, Json> leaves;
            flatten(patch.at("changes"), "", leaves);
            for (const auto &leaf : leaves) {
                if (leaf.second.is_string() &&
                    package.manifest.at("files").contains(leaf.second.get<std::string>()) &&
                    result.definitions[target->second].owner != package.id)
                    fail("patch-resource-owner",
                         "Cross-package resource patches must qualify the owner as "
                         "package-id::relative/path",
                         package.id, id, leaf.first);
                edits[{target->second, leaf.first}].push_back({package.id, leaf.second, overrides});
            }
        }
    }
    // A replacement of an entire subtree overlaps every descendant edit. Resolve
    // these at that subtree so a player choice identifies the full winning value.
    std::map<std::pair<size_t, std::string>, std::vector<Edit>> grouped;
    for (const auto &entry : edits) {
        std::string root = entry.first.second;
        for (size_t slash = root.find('/', 1); slash != std::string::npos;
             slash = root.find('/', slash + 1)) {
            auto prefix = entry.first.second.substr(0, slash);
            if (edits.count({entry.first.first, prefix})) {
                root = prefix;
                break;
            }
        }
        auto group = std::make_pair(entry.first.first, root);
        if (root == entry.first.second) {
            auto &proposals = grouped[group];
            proposals.insert(proposals.end(), entry.second.begin(), entry.second.end());
        } else
            for (const auto &proposal : entry.second) {
                auto &proposals = grouped[group];
                auto found = std::find_if(proposals.begin(), proposals.end(),
                                          [&](const Edit &e) { return e.owner == proposal.owner; });
                if (found == proposals.end()) {
                    Json base = Json::object();
                    const auto pointer = Json::json_pointer(root);
                    if (result.definitions[entry.first.first].data.contains(pointer))
                        base = result.definitions[entry.first.first].data.at(pointer);
                    proposals.push_back({proposal.owner, base, proposal.overrides});
                    found = proposals.end() - 1;
                }
                found->value[Json::json_pointer(entry.first.second.substr(root.size()))] =
                    proposal.value;
                found->overrides.insert(proposal.overrides.begin(), proposal.overrides.end());
            }
    }
    for (const auto &entry : grouped) {
        Definition &target = result.definitions[entry.first.first];
        const std::string &field = entry.first.second;
        const auto &proposals = entry.second;
        const Edit *winner = &proposals.front();
        bool same = std::all_of(proposals.begin(), proposals.end(), [&](const Edit &e) {
            return e.value.dump() == winner->value.dump();
        });
        if (!same) {
            winner = nullptr;
            for (const auto &choice : profile.value("conflicts", Json::array())) {
                if (choice.at("kind") == target.kind && choice.at("id") == target.id &&
                    choice.at("field") == field) {
                    for (const auto &proposal : proposals)
                        if (proposal.owner == choice.at("winner"))
                            winner = &proposal;
                    if (!winner)
                        fail("conflict-choice", "Selected conflict winner does not edit this field",
                             {}, target.id, field);
                }
            }
            if (!winner)
                for (const auto &proposal : proposals) {
                    bool covers =
                        std::all_of(proposals.begin(), proposals.end(), [&](const Edit &other) {
                            return other.owner == proposal.owner ||
                                   proposal.overrides.count(other.owner);
                        });
                    if (covers) {
                        if (winner)
                            fail("patch-conflict", "Multiple patches claim precedence",
                                 proposal.owner, target.id, field, winner->owner);
                        winner = &proposal;
                    }
                }
            if (!winner)
                fail("patch-conflict",
                     "Overlapping edits require a compatibility patch or profile choice",
                     proposals.back().owner, target.id, field, proposals.front().owner);
        }
        target.data[Json::json_pointer(field)] = winner->value;
    }
    // Assign per-kind additions canonically, preserving the stock adapter's handles.
    std::sort(result.definitions.begin(), result.definitions.end(),
              [](const Definition &a, const Definition &b) {
                  return std::make_pair(a.kind, a.id) < std::make_pair(b.kind, b.id);
              });
    std::map<std::string, int32_t> next;
    for (const auto &definition : result.definitions)
        if (definition.handle >= 0)
            next[definition.kind] = std::max(next[definition.kind], definition.handle + 1);
    for (auto &definition : result.definitions)
        if (definition.handle < 0)
            definition.handle = next[definition.kind]++;
    if (result.definitions.size() > kDefinitionLimit)
        fail("definition-limit", "Too many active definitions");
    Json locked = Json::array();
    for (const auto &package : result.packages)
        locked.push_back(
            {{"id", package.id}, {"version", package.version}, {"sha256", package.digest}});
    result.lock = {{"schema_version", 1},
                   {"packages", locked},
                   {"conflicts", profile.value("conflicts", Json::array())},
                   {"selections", profile.value("selections", Json::object())}};
    resolve_legacy_claims(result, profile);
    Json gameplay = Json::array(), mapping = Json::array();
    size_t resource_work = 0;
    for (const auto &definition : result.definitions) {
        if (cosmetic(definition))
            continue;
        Json resources = Json::object();
        resource_hashes(definition.data, definition.owner, result.packages, resources,
                        resource_work);
        gameplay.push_back({{"kind", definition.kind},
                            {"id", definition.id},
                            {"behavior", definition.behavior},
                            {"data", definition.data},
                            {"resources", resources}});
        mapping.push_back(
            {{"kind", definition.kind}, {"id", definition.id}, {"handle", definition.handle}});
    }
    result.gameplay_digest = sha256(gameplay.dump());
    Json legacy_gameplay = Json::array();
    for (const auto &claim : result.lock.value("legacy_resources", Json::array()))
        if (claim.at("gameplay").get<bool>())
            legacy_gameplay.push_back({{"path", claim.at("path")}, {"sha256", claim.at("sha256")}});
    if (!legacy_gameplay.empty())
        result.gameplay_digest =
            sha256(Json{{"definitions", gameplay}, {"legacy_resources", legacy_gameplay}}.dump());
    result.session_digest = sha256(mapping.dump());
    return result;
} catch (const Error &) {
    throw;
} catch (const std::exception &error) {
    fail("invalid-profile", error.what(), {}, {}, profile_root.generic_u8string());
}
} // namespace cc::content
