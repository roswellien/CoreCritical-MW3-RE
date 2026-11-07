#include "content/content.h"
#include "content/persistence.h"
#include "content/world.h"
#include "mech3/content_registry.h"
#include "mech3/source_archive.h"

#include <chrono>
#include <iostream>
#include <set>

namespace {
using namespace cc::content;
ResolvedContentSet profile_content(const fs::path &file, const fs::path &store) {
    auto result = resolve(read_json(file), fs::absolute(file).parent_path(), store,
                          mech3::stock_content(), mech3::content_capabilities());
    mech3::validate_native_content(result);
    return result;
}
ResolvedContentSet directory_content(const fs::path &directory, const fs::path &store) {
    auto package = load_package(directory, true);
    Json profile = {
        {"schema_version", 1},
        {"packages", Json::array({{{"id", package.id},
                                   {"version", package.version},
                                   {"sha256", package.digest},
                                   {"path", fs::absolute(directory).generic_string()}}})}};
    auto result =
        resolve(profile, directory, store, mech3::stock_content(), mech3::content_capabilities());
    mech3::validate_native_content(result);
    return result;
}
Json source_manifest(const fs::path &source) {
    Json manifest = read_json(source / "pack.json");
    manifest["files"] = Json::object();
    for (const auto &entry : fs::recursive_directory_iterator(source)) {
        if (entry.is_symlink())
            fail("unsafe-path", "Source packages cannot contain links");
        if (!entry.is_regular_file())
            continue;
        auto name = entry.path().lexically_relative(source).generic_string();
        if (name == "pack.json")
            continue;
        auto file = confined_path(source, name);
        if (manifest["files"].size() >= 65536)
            fail("source-limit", "Too many source files");
        manifest["files"][name] = {{"bytes", fs::file_size(file)}, {"sha256", file_sha256(file)}};
    }
    return manifest;
}
Json compile_directory(const fs::path &source, const fs::path &output, const fs::path &store) {
    auto base = fs::weakly_canonical(source), destination = fs::weakly_canonical(output);
    auto relative = destination.lexically_relative(base);
    if (!relative.empty() && *relative.begin() != "..")
        fail("compile-output", "Build output must be outside the source package directory");
    if (fs::exists(output) && (!fs::is_directory(output) || !fs::is_empty(output)))
        fail("output-exists", "Compile needs a fresh output directory");
    auto manifest = source_manifest(base);
    fs::path stage = output.string() + ".building-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(stage.parent_path());
    if (!fs::create_directory(stage))
        fail("compile-output", "Cannot reserve a build directory");
    try {
        for (auto it = manifest["files"].begin(); it != manifest["files"].end(); ++it) {
            auto target = confined_path(stage, it.key());
            fs::create_directories(target.parent_path());
            fs::copy_file(confined_path(base, it.key()), target);
        }
        write_json_atomic(stage / "pack.json", manifest);
        auto content = directory_content(stage, store);
        std::map<std::string, Json> worlds;
        for (const auto &definition : content.definitions)
            if (definition.owner == manifest.at("id") && definition.kind == "world" &&
                definition.behavior == "authored")
                worlds[definition.id] = compile_world(definition, content);
        for (auto &definition : manifest["definitions"]) {
            auto found = worlds.find(definition.at("id").get<std::string>());
            if (found == worlds.end())
                continue;
            std::string path = "compiled/world-" + sha256(found->first).substr(0, 24) + ".json";
            auto file = confined_path(stage, path);
            write_json_atomic(file, found->second);
            manifest["files"][path] = {{"bytes", fs::file_size(file)},
                                       {"sha256", file_sha256(file)},
                                       {"format", "CoreCriticalWorld-v1"}};
            definition["behavior"] = "compiled";
            definition["data"] = {{"resource", path},
                                  {"name", definition["data"].value("name", found->first)}};
        }
        write_json_atomic(stage / "pack.json", manifest);
        auto verified = directory_content(stage, store);
        if (fs::exists(output))
            fs::remove(output); // Only the empty directory checked above.
        fs::rename(stage, output);
        return {{"output", fs::absolute(output).generic_string()},
                {"id", manifest.at("id")},
                {"version", manifest.at("version")},
                {"sha256", load_package(output).digest},
                {"gameplay_digest", verified.gameplay_digest}};
    } catch (...) {
        std::error_code error;
        fs::remove_all(stage, error);
        throw;
    }
}
} // namespace
int main(int argc, char **argv) {
    using namespace cc::content;
    try {
        if (argc < 2)
            fail("arguments", "Usage: mw3content "
                              "capabilities|list|validate|resolve|inspect|install|compile|package|"
                              "migrate ... [--store DIR]");
        std::string command = argv[1];
        std::vector<std::string> args;
        fs::path store;
        bool preview = false;
        for (int i = 2; i < argc; ++i) {
            std::string value = argv[i];
            if (value == "--store") {
                if (++i >= argc)
                    fail("arguments", "--store needs a directory");
                store = fs::absolute(fs::u8path(argv[i]));
            } else if (value == "--preview" && command == "migrate")
                preview = true;
            else if (value.rfind("--", 0) == 0)
                fail("arguments", "Unknown option: " + value);
            else
                args.push_back(value);
        }
        if (store.empty())
            store = args.empty() ? fs::current_path() / "store"
                                 : fs::absolute(fs::u8path(args[0])).parent_path() / "store";
        auto argument = [&](size_t count) {
            if (args.size() != count)
                fail("arguments", "Wrong number of arguments for " + command);
        };
        Json result = {{"ok", true}, {"schema_version", 1}};
        if (command == "capabilities") {
            argument(0);
            result["capabilities"] = mech3::content_capabilities();
            result["capabilities"]["content-packages"] = 3;
            result["archive_install"] = mech3::source::archives_supported();
        } else if (command == "list") {
            argument(0);
            result["packages"] = installed_packages(store);
        } else if (command == "resolve" || command == "inspect") {
            argument(1);
            result["content"] = inspect(profile_content(fs::u8path(args[0]), store));
        } else if (command == "validate") {
            argument(1);
            result["content"] = inspect(directory_content(fs::u8path(args[0]), store));
        } else if (command == "install") {
            argument(1);
            auto path = install_package(fs::u8path(args[0]), store);
            auto package = load_package(path, true);
            result["path"] = path.generic_string();
            result["id"] = package.id;
            result["version"] = package.version;
            result["sha256"] = package.digest;
        } else if (command == "package") {
            argument(2);
            directory_content(fs::u8path(args[0]), store);
            package_directory(fs::u8path(args[0]), fs::u8path(args[1]));
            result["output"] = fs::absolute(fs::u8path(args[1])).generic_string();
            result["sha256"] = file_sha256(fs::u8path(args[1]));
        } else if (command == "compile") {
            argument(2);
            fs::path source = fs::u8path(args[0]), output = fs::u8path(args[1]);
            if (fs::is_directory(source))
                result.update(compile_directory(source, output, store));
            else {
                auto value = read_json(source);
                Definition definition{value.at("kind"),
                                      value.at("id"),
                                      value.value("owner", std::string("source")),
                                      value.at("behavior"),
                                      value.at("data"),
                                      -1};
                ResolvedContentSet content;
                content.definitions = mech3::stock_content();
                content.definitions.push_back(definition);
                Package package;
                package.id = definition.owner;
                package.root = fs::absolute(source).parent_path();
                package.manifest = fs::exists(package.root / "pack.json")
                                       ? source_manifest(package.root)
                                       : Json{{"files", Json::object()}};
                content.packages.push_back(package);
                auto world = compile_world(definition, content);
                write_json_atomic(output, world);
                result["output"] = fs::absolute(output).generic_string();
            }
        } else if (command == "migrate") {
            argument(4);
            auto content = profile_content(fs::u8path(args[2]), store);
            result["migration"] = migrate_bundle(fs::u8path(args[0]), fs::u8path(args[1]),
                                                 read_json(fs::u8path(args[3])), content, preview);
            result["output"] = fs::absolute(fs::u8path(args[1])).generic_string();
        } else
            fail("arguments", "Unknown command: " + command);
        std::cout << result.dump() << '\n';
        return 0;
    } catch (const Error &error) {
        std::cout
            << Json{{"ok", false}, {"diagnostics", Json::array({error.diagnostic.json()})}}.dump()
            << '\n';
        return 2;
    } catch (const std::exception &error) {
        Diagnostic diagnostic{"invalid-input", error.what(), {}, {}, {}, {}};
        std::cout << Json{{"ok", false}, {"diagnostics", Json::array({diagnostic.json()})}}.dump()
                  << '\n';
        return 2;
    }
}
