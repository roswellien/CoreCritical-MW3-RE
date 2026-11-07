#include "content/legacy.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace cc::content {
namespace {
std::string lower(std::string value) {
    for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::string trim(const std::string &value) {
    auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos
               ? std::string()
               : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
bool digest(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
           });
}
bool metadata(const std::string &path) {
    const auto name = lower(path);
    return name == "mod.txt" || name == "readme" || name.rfind("readme.", 0) == 0 ||
           name == "license" || name.rfind("license.", 0) == 0 || name.rfind("docs/", 0) == 0;
}
void semantic_version(const std::string &value) {
    static const std::regex pattern("(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8}"
                                    ")(-[A-Za-z0-9.-]+)?(\\+[A-Za-z0-9.-]+)?");
    if (!std::regex_match(value, pattern))
        fail("invalid-version", "Expected a semantic package version: " + value);
}
std::string mod_version(const std::string &value) {
    if (value.empty())
        return "1.0.0";
    if (std::regex_match(value, std::regex("(0|[1-9][0-9]{0,8})(\\.(0|[1-9][0-9]{0,8}))?")))
        return value + (value.find('.') == std::string::npos ? ".0.0" : ".0");
    try {
        semantic_version(value);
        return value;
    } catch (const Error &) {
    }
    return "0.0.0-legacy." + sha256(value).substr(0, 12);
}
std::string folder_id(const fs::path &root) {
    std::string value = lower(root.filename().string());
    for (char &c : value)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
            c = '-';
    if (value.empty())
        value = "mod";
    return "legacy-" + value.substr(0, 112);
}
std::string read_text(const fs::path &path) {
    std::error_code error;
    auto size = fs::file_size(path, error);
    if (error || size > 2 * 1024 * 1024)
        fail("legacy-metadata", "Missing or oversized legacy metadata", {}, {},
             path.generic_string());
    std::ifstream input(path, std::ios::binary);
    std::string result(static_cast<size_t>(size), '\0');
    if (!input.read(result.data(), static_cast<std::streamsize>(result.size())))
        fail("file-read", "Cannot read legacy metadata");
    return result;
}
Json mod_manifest(const fs::path &root) {
    Json result = {{"schema_version", 1},
                   {"id", folder_id(root)},
                   {"name", ""},
                   {"version", "1.0.0"},
                   {"legacy_worlds", Json::array()},
                   {"legacy_ia_worlds", Json::array()}};
    std::istringstream input(read_text(confined_path(root, "mod.txt")));
    std::set<std::string> singleton;
    for (std::string line; std::getline(input, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            fail("legacy-metadata", "Expected key=value in mod.txt");
        const auto key = lower(trim(line.substr(0, eq))), value = trim(line.substr(eq + 1));
        if (key == "world") {
            result["legacy_worlds"].push_back(value);
            continue;
        }
        if (key == "ia_world") {
            const auto space = value.find_first_of(" \t");
            result["legacy_ia_worlds"].push_back(
                {{"directory", value.substr(0, space)},
                 {"name", space == std::string::npos ? value : trim(value.substr(space + 1))}});
            continue;
        }
        if (!std::set<std::string>{"id", "name", "version", "author", "description", "for"}.count(
                key))
            fail("legacy-metadata", "Unsupported mod.txt directive: " + key);
        if (!singleton.insert(key).second)
            fail("legacy-metadata", "Duplicate mod.txt directive: " + key);
        if (key == "for") {
            if (!value.empty() && lower(value).rfind("mw3", 0) != 0)
                fail("wrong-game", "Legacy mod is not for MW3");
        } else if (key == "version") {
            result["source_version"] = value;
            result["version"] = mod_version(value);
        } else
            result[key] = value;
    }
    if (result["name"] == "")
        fail("legacy-metadata", "mod.txt requires name=");
    return result;
}
Json integrity(const fs::path &root, const Json &original, bool verify, bool required) {
    Json files = original.value("files", Json::object());
    if (!files.is_object() || (required && files.empty()))
        fail("file-table", "Legacy native payload requires an integrity table");
    // Very early directory mods did not carry an integrity table. Pin every
    // regular file on import, then use that immutable table in the managed store.
    if (!original.contains("files")) {
        std::error_code error;
        uint64_t expanded = 0;
        fs::recursive_directory_iterator it(root, fs::directory_options::none, error), end;
        for (; it != end && !error; it.increment(error)) {
            if (it.depth() > 32)
                fail("payload-limit", "Legacy directory depth exceeds 32");
            auto relative = it->path().lexically_relative(root).generic_string();
            confined_path(root, relative);
            if (!it->is_directory(error) && !it->is_regular_file(error))
                fail("unsafe-path", "Only regular package files are supported", {}, {}, relative);
            if (!it->is_regular_file(error) || lower(relative) == "pack.json")
                continue;
            const auto size = it->file_size();
            if (size > 8ull * 1024 * 1024 * 1024 || expanded > 32ull * 1024 * 1024 * 1024 - size)
                fail("payload-limit", "Legacy directory exceeds expanded size limits");
            expanded += size;
            files[relative] = {{"bytes", size}, {"sha256", file_sha256(it->path())}};
            if (files.size() > 65536)
                fail("payload-limit", "Legacy package has too many files");
        }
        if (error)
            fail("file-read", error.message());
    }
    if (files.size() > 65536)
        fail("payload-limit", "Legacy package has too many files");
    uint64_t total = 0;
    std::set<std::string> names;
    for (auto file = files.begin(); file != files.end(); ++file) {
        const auto path = confined_path(root, file.key());
        const auto folded = lower(file.key());
        if (folded == "pack.json" || !names.insert(folded).second)
            fail("duplicate-path", "Duplicate or reserved legacy payload path", {}, {}, file.key());
        if (!file.value().is_object() || !file.value().contains("bytes") ||
            !file.value()["bytes"].is_number_integer() ||
            (file.value()["bytes"].is_number_integer() &&
             !file.value()["bytes"].is_number_unsigned() &&
             file.value()["bytes"].get<int64_t>() < 0) ||
            !file.value().contains("sha256") || !file.value()["sha256"].is_string())
            fail("file-table", "Invalid legacy integrity record", {}, {}, file.key());
        const auto size = file.value()["bytes"].get<uint64_t>();
        if (size > 8ull * 1024 * 1024 * 1024 || total > 32ull * 1024 * 1024 * 1024 - size)
            fail("payload-limit", "Legacy package exceeds expanded size limits");
        total += size;
        file.value()["sha256"] = lower(file.value()["sha256"].get<std::string>());
        if (!digest(file.value()["sha256"]))
            fail("file-table", "Invalid legacy SHA-256", {}, {}, file.key());
        if (!metadata(file.key()) && !legacy_file_allowed(file.key()))
            fail("protected-resource",
                 "Legacy package attempts to provide player files or executable code", {}, {},
                 file.key());
        if (verify) {
            std::error_code error;
            if (!fs::is_regular_file(path, error) || fs::file_size(path, error) != size ||
                file_sha256(path) != file.value()["sha256"])
                fail("payload-integrity", "Missing, changed or corrupt legacy payload", {}, {},
                     file.key());
        }
    }
    return files;
}
void add_definition(Package &package, const std::string &kind, const std::string &local,
                    const std::string &behavior, Json data) {
    const auto id = package.id + ":" + local;
    package.manifest["definitions"].push_back(
        {{"kind", kind}, {"id", id}, {"behavior", behavior}, {"data", data}});
    package.definitions.push_back({kind, id, package.id, behavior, std::move(data), -1});
}
void prepared(Package &package, const Json &original, bool verify) {
    const auto payload = original.value("native_payload", Json::object());
    if (!payload.is_object() || payload.value("profile", "") != "prepared-native-v1" ||
        payload.value("path_base", "") != "pack-root")
        fail("unsupported-legacy-profile",
             "Only prepared-native-v1 schema-2 models can be adapted; this old native binding is "
             "unsupported",
             package.id);
    const auto registry_path = payload.value("registry", std::string());
    if (!package.manifest["files"].contains(registry_path))
        fail("file-table", "Prepared registry is not integrity-pinned", package.id);
    const auto registry = read_json(confined_path(package.root, registry_path));
    if (!registry.is_object() || !registry.contains("schema_version") ||
        !registry["schema_version"].is_number_integer() || registry["schema_version"] != 2 ||
        registry.value("payload_profile", "") != "prepared-native-v1" ||
        !registry.contains("models") || !registry["models"].is_array())
        fail("unsupported-legacy-profile", "Unsupported prepared model registry", package.id);
    const std::set<std::string> chassis = {
        "bushwacker", "madcat",   "vulture",     "thor",     "orion",  "daishi",
        "shadowcat",  "puma",     "annihilator", "cauldron", "sunder", "supernova",
        "blackhawk",  "champion", "strider",     "avatar",   "owens",  "firefly"};
    const std::set<std::string> formats = {"BWMESH1", "BWMESH2", "BWMESH3", "BWMESH4", "BWMESH5",
                                           "BWMESH6", "BWTEX1",  "BWNRM2",  "BWSFM1"};
    const auto payload_formats = payload.value("required_formats", Json::array());
    if (!payload_formats.is_array())
        fail("unsupported-legacy-format", "Expected native format requirement array", package.id);
    for (const auto &format : payload_formats)
        if (!format.is_string() || !formats.count(format.get<std::string>()))
            fail("unsupported-legacy-format", "Unsupported prepared payload format", package.id);
    const auto payload_capabilities = payload.value("required_capabilities", Json::object());
    if (!payload_capabilities.is_object())
        fail("unsupported-legacy-capability", "Expected native capability version object",
             package.id);
    for (const auto &cap : payload_capabilities.items())
        if ((cap.key() != "surface-materials" && cap.key() != "prepared-mechs") ||
            !cap.value().is_number_integer() || cap.value() != 1)
            fail("unsupported-legacy-capability", "Unsupported old native capability: " + cap.key(),
                 package.id);
    std::set<std::string> models;
    Json defaults = Json::array();
    for (const auto &model : registry["models"]) {
        const auto id = model.value("id", std::string());
        if (!chassis.count(id) || !models.insert(id).second ||
            model.value("backend", "") != "native" || model.value("status", "") != "runtime-ready")
            fail("unsupported-legacy-model",
                 "Only unique runtime-ready stock-rig models are supported", package.id, id);
        const auto model_formats = model.value("required_formats", Json::array());
        if (!model_formats.is_array())
            fail("unsupported-legacy-format", "Expected model format requirement array", package.id,
                 id);
        for (const auto &format : model_formats)
            if (!format.is_string() || !formats.count(format.get<std::string>()))
                fail("unsupported-legacy-format", "Unsupported model format requirement",
                     package.id, id);
        if (model.contains("skin_ids") &&
            (!model["skin_ids"].is_array() || !model["skin_ids"].empty()))
            fail("unsupported-legacy-binding",
                 "Alternate prepared skins require explicit migration to typed skins", package.id,
                 id);
        const auto bindings = model.value("environment", Json::object());
        if (!bindings.is_object())
            fail("unsupported-legacy-binding", "Expected old model bindings", package.id, id);
        std::string upper_id = id;
        for (char &c : upper_id)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::set<std::string> consumed;
        for (const auto *lod : {"0", "1"}) {
            const std::string prefix = "MW34_" + upper_id + (lod[0] == '1' ? "_LOD1" : "");
            Json data = {{"chassis", "stock:" + id}, {"lod", lod}};
            for (const auto &field : std::vector<std::pair<std::string, std::string>>{
                     {"mesh", "_MESH"},
                     {"textures", "_TEXTURES"},
                     {"normals", "_NORMALS"},
                     {"materials", "_SURFACE_MATERIALS"}}) {
                const auto key = id == "bushwacker" && lod[0] == '0' && field.first == "mesh"
                                     ? "MW34_BUSHWACKER_TEST"
                                     : prefix + field.second;
                if (!bindings.contains(key)) {
                    if (field.first == "materials")
                        continue;
                    fail("unsupported-legacy-binding", "Incomplete prepared model binding: " + key,
                         package.id, id);
                }
                if (!bindings[key].is_string())
                    fail("unsupported-legacy-binding", "Expected package-relative payload binding",
                         package.id, id, key);
                const auto path = bindings[key].get<std::string>();
                if (!package.manifest["files"].contains(path))
                    fail("file-table", "Prepared binding is not integrity-pinned", package.id, id,
                         path);
                const auto format = package.manifest["files"][path].value("format", std::string());
                const std::string expected = field.first == "textures"    ? "BWTEX1"
                                             : field.first == "normals"   ? "BWNRM2"
                                             : field.first == "materials" ? "BWSFM1"
                                                                          : "BWMESH";
                if (!formats.count(format) ||
                    (field.first == "mesh" ? format.rfind(expected, 0) != 0 : format != expected))
                    fail("unsupported-legacy-format", "Prepared binding has an incorrect format",
                         package.id, id, path);
                for (const auto &declared : {model.value("required_formats", Json::array()),
                                             payload.value("required_formats", Json::array())})
                    if (!declared.is_array() ||
                        std::find(declared.begin(), declared.end(), Json(format)) == declared.end())
                        fail("unsupported-legacy-format", "Prepared format was not declared",
                             package.id, id, path);
                if (field.first == "materials" &&
                    payload.value("required_capabilities", Json::object())
                            .value("surface-materials", Json()) != 1)
                    fail("unsupported-legacy-capability",
                         "Materials require surface-materials version 1", package.id, id);
                if (verify) {
                    std::ifstream input(confined_path(package.root, path), std::ios::binary);
                    char magic[8] = {};
                    input.read(magic, sizeof magic);
                    if (input.gcount() < static_cast<std::streamsize>(format.size()) ||
                        std::string(magic, format.size()) != format)
                        fail("payload-format", "Prepared payload magic does not match its format",
                             package.id, id, path);
                }
                data[field.first] = path;
                consumed.insert(key);
            }
            const std::string local = "appearance." + id + ".lod" + lod;
            add_definition(package, "appearance", local, "prepared-mech-v1", std::move(data));
            defaults.push_back(package.id + ":" + local);
        }
        if (consumed.size() != bindings.size())
            fail("unsupported-legacy-binding", "Unsupported old prepared environment binding",
                 package.id, id);
    }
    if (models.empty())
        fail("unsupported-legacy-model", "Prepared registry has no models", package.id);
    std::set<std::string> provided;
    for (const auto &id : original.value("provides", Json::object()).value("mechs", Json::array()))
        provided.insert(id.get<std::string>());
    if (models != provided)
        fail("unsupported-legacy-model", "Prepared manifest and registry chassis differ",
             package.id);
    package.manifest["required_capabilities"] =
        original.value("required_capabilities", Json::object());
    if (!package.manifest["required_capabilities"].is_object())
        fail("unsupported-legacy-capability", "Expected capability version object", package.id);
    if (package.manifest["required_capabilities"].contains("prepared-mech-visuals") &&
        (!package.manifest["required_capabilities"]["prepared-mech-visuals"].is_number_integer() ||
         package.manifest["required_capabilities"]["prepared-mech-visuals"] != 1))
        fail("unsupported-legacy-capability", "Prepared adapter requires reader version 1",
             package.id);
    package.manifest["required_capabilities"]["prepared-mech-visuals"] = 1;
    package.manifest["legacy_default_appearances"] = std::move(defaults);
}
} // namespace

bool legacy_file_allowed(const std::string &path) {
    const auto p = lower(path);
    if (p.empty() || p.front() == '/' || p.find('\\') != std::string::npos ||
        p.find(':') != std::string::npos)
        return false;
    std::istringstream parts(p);
    for (std::string part; std::getline(parts, part, '/');)
        if (part.empty() || part == "." || part == "..")
            return false;
    for (const char *dir :
         {"pilots/", "configs/", "keys/", "saves/", "patch12/", "mods/", "profiles/"})
        if (p.rfind(dir, 0) == 0)
            return false;
    for (const char *file : {"mw3.cfg", "settings.reg", "movie-shapes.txt"})
        if (p == file)
            return false;
    const auto extension = fs::u8path(p).extension().string();
    return !std::set<std::string>{".log", ".exe", ".dll", ".com", ".bat", ".cmd", ".ps1",
                                  ".vbs", ".js",  ".lua", ".py",  ".asi", ".sys", ".scr"}
                .count(extension);
}
bool legacy_file_is_gameplay(const std::string &path) {
    // Legacy consumers choose roles at runtime. A bank named rtexture.zbd can
    // still be passed to the native world reader, so suffixes prove no exemption.
    (void)path;
    return true;
}
std::optional<Package> try_load_legacy(const fs::path &input, bool verify_files) try {
    const auto root = fs::weakly_canonical(input);
    std::error_code error;
    const bool has_manifest = fs::is_regular_file(root / "pack.json", error);
    Json original;
    bool mod = false;
    if (has_manifest) {
        original = read_json(confined_path(root, "pack.json"));
        if (!original.is_object())
            fail("unsupported-schema", "Expected a package manifest object");
        const auto schema = original.value("schema_version", Json(1));
        if (!schema.is_number_integer())
            fail("unsupported-schema", "Legacy schema version must be an integer");
        if (schema == 3)
            return std::nullopt;
        if (schema != 1 && schema != 2)
            fail("unsupported-schema", "Unsupported legacy manifest schema");
    } else {
        if (!fs::is_regular_file(root / "mod.txt", error))
            return std::nullopt;
        original = mod_manifest(root);
        mod = true;
    }
    Package package;
    package.root = root;
    package.id = original.value("id", std::string());
    package.version = original.value("version", std::string("1.0.0"));
    if (!valid_id(package.id) || package.id == "stock")
        fail("invalid-package-id", "Invalid legacy package identity", package.id);
    semantic_version(package.version);
    if (original.value("game_id", "mw3") != "mw3")
        fail("wrong-game", "Legacy package is not for MW3", package.id);
    // These packs predate resource-reader admission. Preserve the old launcher
    // version as provenance while enforcing implemented readers and capabilities.
    const bool native = original.value("schema_version", Json(1)) == 2;
    if (!native && original.contains("native_payload"))
        fail("unsupported-legacy-profile", "Native payload bindings require a schema-2 adapter",
             package.id);
    package.manifest = {{"schema_version", 3},
                        {"game_id", "mw3"},
                        {"id", package.id},
                        {"version", package.version},
                        {"name", original.value("name", package.id)},
                        {"files", integrity(root, original, verify_files, native)},
                        {"definitions", Json::array()},
                        {"dependencies", original.value("dependencies", Json::array())},
                        {"conflicts", original.value("conflicts", Json::array())}};
    for (const char *key : {"author", "description", "source_version"})
        if (original.contains(key))
            package.manifest[key] = original[key];
    if (original.contains("min_engine_version"))
        package.manifest["legacy_min_engine_version"] = original["min_engine_version"];
    package.manifest["legacy_source_format"] = native ? "schema-2-prepared-native-v1"
                                               : mod  ? "mod-txt"
                                                      : "schema-1";
    if (native)
        prepared(package, original, verify_files);
    else {
        Json claims = Json::array();
        for (auto file = package.manifest["files"].begin(); file != package.manifest["files"].end();
             ++file) {
            if (metadata(file.key()))
                continue;
            claims.push_back({{"path", lower(file.key())},
                              {"source", file.key()},
                              {"sha256", file.value().at("sha256")},
                              {"gameplay", legacy_file_is_gameplay(file.key())}});
        }
        Json layer = {{"files", claims},
                      {"source_format", package.manifest["legacy_source_format"]},
                      {"ia_worlds", original.value("legacy_ia_worlds", Json::array())}};
        add_definition(package, "legacy_layer", "files", "file-overlay-v1", std::move(layer));
        std::set<std::string> worlds;
        for (const auto &world : original.value("legacy_worlds", Json::array())) {
            auto directory = lower(world.get<std::string>());
            if (!valid_id(directory) || directory.find('.') != std::string::npos ||
                !worlds.insert(directory).second)
                fail("legacy-world", "Legacy world folders must be unique simple native names",
                     package.id, directory);
            auto archive = "zbd/" + directory + "/gamez.zbd";
            if (std::none_of(claims.begin(), claims.end(),
                             [&](const Json &claim) { return claim.at("path") == archive; }))
                fail("legacy-world", "Declared world has no gamez.zbd payload", package.id,
                     directory);
            add_definition(package, "world", "world." + directory, "legacy-layer",
                           {{"directory", directory}, {"name", directory}, {"layer", package.id}});
        }
        package.manifest["required_capabilities"] =
            original.value("required_capabilities", Json::object());
        if (!package.manifest["required_capabilities"].is_object())
            fail("unsupported-legacy-capability", "Expected capability version object", package.id);
        if (package.manifest["required_capabilities"].contains("legacy-file-overlays") &&
            (!package.manifest["required_capabilities"]["legacy-file-overlays"]
                  .is_number_integer() ||
             package.manifest["required_capabilities"]["legacy-file-overlays"] != 1))
            fail("unsupported-legacy-capability", "Legacy adapter requires file-overlay version 1",
                 package.id);
        package.manifest["required_capabilities"]["legacy-file-overlays"] = 1;
    }
    package.digest = sha256(package.manifest.dump());
    return package;
} catch (const Error &) {
    throw;
} catch (const std::exception &error) {
    fail("legacy-metadata", error.what());
}

void resolve_legacy_claims(ResolvedContentSet &content, const Json &profile) try {
    Json choices = profile.value("resource_overrides", Json::array());
    if (!choices.is_array() || choices.size() > 65536)
        fail("resource-conflict", "Expected a bounded resource_overrides array");
    std::map<std::string, Json> overrides;
    for (const auto &choice : choices) {
        if (!choice.is_object() || !choice.contains("path") || !choice["path"].is_string() ||
            !choice.contains("winner") || !choice["winner"].is_string())
            fail("resource-conflict", "Expected resource path and winner");
        const auto path = choice["path"].get<std::string>();
        if (path != lower(path) || !legacy_file_allowed(path) ||
            !overrides.emplace(path, choice).second)
            fail("resource-conflict", "Invalid or duplicate resource override", {}, {}, path);
        for (auto field = choice.begin(); field != choice.end(); ++field)
            if (field.key() != "path" && field.key() != "winner" &&
                field.key() != "allow_composition")
                fail("resource-conflict", "Unknown resource override field", {}, {}, field.key());
        if (choice.contains("allow_composition") && !choice["allow_composition"].is_boolean())
            fail("resource-conflict", "allow_composition must be boolean");
    }
    std::map<std::string, std::vector<Json>> candidates;
    for (const auto *layer : content.of_kind("legacy_layer")) {
        if (layer->behavior != "file-overlay-v1" || !layer->data.is_object() ||
            !layer->data.contains("files") || !layer->data["files"].is_array())
            fail("legacy-layer", "Unsupported legacy layer contract", layer->owner, layer->id);
        std::set<std::string> paths;
        for (const auto &claim : layer->data["files"]) {
            if (!claim.is_object() || claim.size() != 4 || !claim.contains("path") ||
                !claim["path"].is_string() || !claim.contains("source") ||
                !claim["source"].is_string() || !claim.contains("sha256") ||
                !claim["sha256"].is_string() || !claim.contains("gameplay") ||
                !claim["gameplay"].is_boolean())
                fail("legacy-layer", "Malformed legacy resource claim", layer->owner, layer->id);
            const auto path = claim["path"].get<std::string>(),
                       source = claim["source"].get<std::string>();
            if (path != lower(source) || !legacy_file_allowed(path) || !paths.insert(path).second ||
                claim["gameplay"] != legacy_file_is_gameplay(path))
                fail("legacy-layer", "Invalid or misclassified legacy resource claim", layer->owner,
                     layer->id, path);
            content.resource(layer->owner, source);
            const auto package =
                std::find_if(content.packages.begin(), content.packages.end(),
                             [&](const Package &package) { return package.id == layer->owner; });
            if (package == content.packages.end() ||
                package->manifest.at("files").at(source).at("sha256") != claim["sha256"])
                fail("legacy-layer", "Legacy resource hash differs from package integrity",
                     layer->owner, layer->id, path);
            Json selected = claim;
            selected["owner"] = layer->owner;
            candidates[path].push_back(std::move(selected));
        }
    }
    bool composition = false;
    for (const auto &d : content.definitions)
        if ((d.kind == "weapon" || d.kind == "chassis" || d.kind == "vehicle") &&
            (d.data.contains("base") || d.data.contains("reader")))
            composition = true;
    Json selected = Json::array();
    for (auto &entry : candidates) {
        auto &options = entry.second;
        std::sort(options.begin(), options.end(),
                  [](const Json &a, const Json &b) { return a.at("owner") < b.at("owner"); });
        auto winner = options.begin();
        const auto choice = overrides.find(entry.first);
        if (choice != overrides.end()) {
            winner = std::find_if(options.begin(), options.end(), [&](const Json &claim) {
                return claim["owner"] == choice->second["winner"];
            });
            if (winner == options.end())
                fail("resource-conflict", "Selected resource owner does not provide the file", {},
                     {}, entry.first);
        } else if (std::any_of(options.begin() + 1, options.end(), [&](const Json &claim) {
                       return claim["sha256"] != options.front()["sha256"];
                   }))
            fail("resource-conflict",
                 "Overlapping legacy files require an explicit resource winner",
                 options.front()["owner"], {}, entry.first, options.back()["owner"]);
        const auto filename = fs::u8path(entry.first).filename().string();
        const bool reader_collision =
            composition &&
            (entry.first == "zbd/reader.zbd" || filename == "weapons.zrd" ||
             filename == "vehicle.zrd" ||
             (filename.rfind("dfn", 0) == 0 && fs::u8path(filename).extension() == ".zrd"));
        if (reader_collision &&
            (choice == overrides.end() || !choice->second.value("allow_composition", false)))
            fail("resource-composition-conflict",
                 "Overlay changes a reader used by managed definitions; explicitly choose its "
                 "owner and allow composition",
                 (*winner)["owner"], {}, entry.first);
        selected.push_back(*winner);
    }
    for (const auto &choice : overrides)
        if (!candidates.count(choice.first))
            fail("resource-conflict", "Resource override names no provided file", {}, {},
                 choice.first);
    content.lock["legacy_resources"] = std::move(selected);
    content.lock["resource_overrides"] = Json::array();
    for (const auto &choice : overrides)
        content.lock["resource_overrides"].push_back(choice.second);
    if (!content.lock.contains("selections"))
        content.lock["selections"] = Json::object();
    if (!content.lock["selections"].contains("appearances")) {
        Json defaults = Json::array();
        for (const auto &package : content.packages)
            for (const auto &id :
                 package.manifest.value("legacy_default_appearances", Json::array()))
                defaults.push_back(id);
        std::sort(defaults.begin(), defaults.end());
        if (!defaults.empty())
            content.lock["selections"]["appearances"] = std::move(defaults);
    }
} catch (const Error &) {
    throw;
} catch (const std::exception &error) {
    fail("legacy-layer", error.what());
}
} // namespace cc::content
