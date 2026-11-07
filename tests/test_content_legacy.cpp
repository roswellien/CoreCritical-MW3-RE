#include "check.h"
#include "content/legacy.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/content_legacy.h"
#include "mech3/mods.h"
#include "mech3/source_archive.h"

#include <fstream>
#include <functional>

namespace {
using namespace cc::content;
fs::path scratch(const char *name) {
    auto root = fs::path(MW3_TEST_TMP) / name;
    fs::create_directories(root);
    return root;
}
void put(const fs::path &path, const std::string &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
bool rejects(const std::function<void()> &action, const char *code) {
    try {
        action();
        return false;
    } catch (const Error &error) {
        return error.diagnostic.code == code;
    }
}
Json old_manifest(const std::string &id) {
    return {{"schema_version", 1},
            {"id", id},
            {"name", id},
            {"version", "1.0.0"},
            {"files", Json::object()}};
}
void payload(Json &manifest, const fs::path &root, const std::string &name,
             const std::string &bytes, const char *format = nullptr) {
    put(root / name, bytes);
    manifest["files"][name] = {{"bytes", bytes.size()}, {"sha256", sha256(bytes)}};
    if (format)
        manifest["files"][name]["format"] = format;
}
ResolvedContentSet set(std::initializer_list<Package> packages, Json profile = Json::object()) {
    ResolvedContentSet content;
    content.lock["selections"] = profile.value("selections", Json::object());
    for (const auto &package : packages) {
        content.packages.push_back(package);
        content.definitions.insert(content.definitions.end(), package.definitions.begin(),
                                   package.definitions.end());
    }
    resolve_legacy_claims(content, profile);
    return content;
}
struct MountSession {
    zutl::Mounts saved = zutl::mounts();
    MountSession() { zutl::mounts() = {}; }
    ~MountSession() {
        mech3::unmount_content_legacy();
        zutl::mounts() = saved;
    }
};
} // namespace

TEST(content_legacy_modtxt_normalizes_without_source_writes) {
    auto root = scratch("legacy_modtxt");
    put(root / "mod.txt",
        "id=authored-legacy\nname=Authored "
        "Legacy\nfor=mw3\nversion=2\nworld=trainingdemo\nia_world=trainingdemo Authored Arena\n");
    put(root / "zbd/trainingdemo/gamez.zbd", "authored fixture, not original game data");
    put(root / "strings.txt", "1=Authored string\n");
    auto imported = try_load_legacy(root);
    EXPECT(imported.has_value() && imported->version == "2.0.0" &&
           imported->id == "authored-legacy");
    EXPECT(imported->manifest["schema_version"] == 3 &&
           imported->manifest["source_version"] == "2");
    EXPECT(imported->manifest["files"].contains("mod.txt") && !fs::exists(root / "pack.json"));
    EXPECT(imported->definitions.size() == 2 &&
           imported->definitions[1].behavior == "legacy-layer");
    auto content = set({*imported});
    mech3::validate_content_legacy(content);
    EXPECT(content.lock["legacy_resources"].size() == 2);
    for (const auto &claim : content.lock["legacy_resources"])
        EXPECT(!fs::u8path(claim["source"].get<std::string>()).is_absolute());
    EXPECT(content.lock["legacy_resources"][0]["gameplay"] == true);
    auto store = fs::path(MW3_TEST_TMP) / "legacy_modtxt_store";
    const auto installed = install_package(root, store);
    EXPECT(load_package(installed).digest == imported->digest);
    const auto archive = fs::path(MW3_TEST_TMP) / "legacy_modtxt.zip";
    package_directory(root, archive);
    if (!mech3::source::archives_supported()) {
        EXPECT(rejects(
            [&] { install_package(archive, fs::path(MW3_TEST_TMP) / "legacy_modtxt_zip_store"); },
            "archive-unavailable"));
        return;
    }
    const auto unpacked =
        install_package(archive, fs::path(MW3_TEST_TMP) / "legacy_modtxt_zip_store");
    EXPECT(load_package(unpacked).digest == imported->digest && !fs::exists(root / "pack.json"));
}

TEST(content_legacy_schema1_integrity_protection_and_explicit_file_winners) {
    auto root = scratch("legacy_winners"), one = root / "one", two = root / "two";
    auto a = old_manifest("legacy-one"), b = old_manifest("legacy-two");
    payload(a, one, "zbd/Reader.zbd", "first archive");
    payload(a, one, "strings.txt", "7=first\n");
    payload(a, one, "zbd/c1/rtexture.zbd", "first world texture bank");
    payload(a, one, "zbd/rtexture.zbd", "first common texture bank");
    payload(b, two, "zbd/reader.zbd", "second archive");
    payload(b, two, "strings.txt", "7=second\n");
    payload(b, two, "zbd/c1/rtexture.zbd", "second world texture bank");
    a["dependencies"] = Json::array({{{"id", "dependency"}, {"min_version", "1.0.0"}}});
    write_json_atomic(one / "pack.json", a);
    write_json_atomic(two / "pack.json", b);
    auto pa = *try_load_legacy(one), pb = *try_load_legacy(two);
    EXPECT(pa.manifest["dependencies"] == a["dependencies"]);
    EXPECT(rejects([&] { set({pa, pb}); }, "resource-conflict"));
    Json choices = {{"resource_overrides",
                     Json::array({{{"path", "zbd/reader.zbd"}, {"winner", "legacy-two"}},
                                  {{"path", "strings.txt"}, {"winner", "legacy-one"}},
                                  {{"path", "zbd/c1/rtexture.zbd"}, {"winner", "legacy-two"}}})}};
    auto selected = set({pa, pb}, choices);
    EXPECT(selected.lock["legacy_resources"][0]["owner"] == "legacy-one");
    EXPECT(selected.lock["legacy_resources"][1]["owner"] == "legacy-two");
    MountSession mounts;
    auto install = root / "install";
    put(install / "zbd/reader.zbd", "stock fallback");
    zutl::mount(install.generic_string());
    mech3::mount_content_legacy(selected);
    EXPECT(zutl::find("ZBD\\READER.ZBD") == confined_path(two, "zbd/reader.zbd").generic_string());
    EXPECT(zutl::find_in_mods("strings.txt") == confined_path(one, "strings.txt").generic_string());
    EXPECT(zutl::find_all("zbd/reader.zbd").size() == 2);
    EXPECT(zutl::list("zbd/*.zbd").size() == 2);
    zutl::set_managed_search({"zbd/c1", "zbd"});
    EXPECT(zutl::find("rtexture.zbd") ==
           confined_path(two, "zbd/c1/rtexture.zbd").generic_string());
    EXPECT(zutl::list("rtexture*.zbd") == std::vector<std::string>{"rtexture.zbd"});
    zutl::set_managed_search({"zbd/c2", "zbd"});
    EXPECT(zutl::find("rtexture.zbd") == confined_path(one, "zbd/rtexture.zbd").generic_string());
    std::map<int32_t, std::string> strings;
    mech3::mods_apply_strings(strings, {});
    EXPECT(strings[7] == "first");
    // A payload added after import cannot leak through the tagged package root.
    put(one / "zbd/not-claimed.zrd", "unclaimed");
    EXPECT(zutl::find_in_mods("zbd/not-claimed.zrd").empty());
    put(two / "zbd/reader.zbd", "tampered");
    EXPECT(rejects([&] { try_load_legacy(two); }, "payload-integrity"));
    EXPECT(rejects(
        [&] {
            auto *file = zutl::open("zbd/reader.zbd", "rb");
            if (file)
                std::fclose(file);
        },
        "payload-integrity"));
    auto protected_pack = old_manifest("protected-pack");
    payload(protected_pack, root / "protected", "saves/pilot.zrd", "fake save");
    write_json_atomic(root / "protected/pack.json", protected_pack);
    EXPECT(rejects([&] { try_load_legacy(root / "protected"); }, "protected-resource"));
    protected_pack = old_manifest("protected-pack");
    protected_pack["schema_version"] = uint64_t(4294967299ull);
    write_json_atomic(root / "protected/pack.json", protected_pack);
    EXPECT(rejects([&] { try_load_legacy(root / "protected"); }, "unsupported-schema"));
}

TEST(content_legacy_reader_composition_requires_explicit_acknowledgement) {
    auto root = scratch("legacy_composition");
    auto manifest = old_manifest("legacy-reader");
    payload(manifest, root, "zbd/reader.zbd", "authored archive fixture");
    write_json_atomic(root / "pack.json", manifest);
    auto package = *try_load_legacy(root);
    auto content = set({package});
    content.definitions.push_back({"weapon",
                                   "authored:weapon",
                                   "authored",
                                   "mw3:weapon@1",
                                   {{"base", "stock:laser_medium"}},
                                   -1});
    EXPECT(rejects([&] { resolve_legacy_claims(content, Json::object()); },
                   "resource-composition-conflict"));
    resolve_legacy_claims(content,
                          {{"resource_overrides", Json::array({{{"path", "zbd/reader.zbd"},
                                                                {"winner", package.id},
                                                                {"allow_composition", true}}})}});
    EXPECT(content.lock["legacy_resources"][0]["owner"] == package.id);
    content.definitions[0].data["files"][0]["gameplay"] = false;
    EXPECT(rejects([&] { resolve_legacy_claims(content, Json::object()); }, "legacy-layer"));
}

TEST(content_legacy_prepared_native_normalizes_typed_appearances_and_rejects_unknown_bindings) {
    auto root = scratch("legacy_prepared");
    auto manifest = old_manifest("legacy-prepared");
    manifest["schema_version"] = 2;
    manifest["provides"] = {{"mechs", {"bushwacker"}}};
    manifest["native_payload"] = {{"profile", "prepared-native-v1"},
                                  {"registry", "models/models.json"},
                                  {"path_base", "pack-root"},
                                  {"required_formats", {"BWMESH1", "BWTEX1", "BWNRM2"}}};
    Json model = {{"id", "bushwacker"},
                  {"backend", "native"},
                  {"status", "runtime-ready"},
                  {"skin_ids", Json::array()},
                  {"required_formats", {"BWMESH1", "BWTEX1", "BWNRM2"}},
                  {"environment", Json::object()}};
    for (const auto &binding : std::vector<std::pair<std::string, const char *>>{
             {"MW34_BUSHWACKER_TEST", "BWMESH1"},
             {"MW34_BUSHWACKER_TEXTURES", "BWTEX1"},
             {"MW34_BUSHWACKER_NORMALS", "BWNRM2"},
             {"MW34_BUSHWACKER_LOD1_MESH", "BWMESH1"},
             {"MW34_BUSHWACKER_LOD1_TEXTURES", "BWTEX1"},
             {"MW34_BUSHWACKER_LOD1_NORMALS", "BWNRM2"}}) {
        const auto file = "models/" + binding.first + ".bin";
        // Normalization checks declared format/magic; actual mesh structure belongs
        // to the existing prepared renderer parser when this selection is applied.
        payload(manifest, root, file, std::string(binding.second) + std::string(20, '\0'),
                binding.second);
        model["environment"][binding.first] = file;
    }
    Json registry = {{"schema_version", 2},
                     {"payload_profile", "prepared-native-v1"},
                     {"models", Json::array({model})}};
    payload(manifest, root, "models/models.json", registry.dump());
    write_json_atomic(root / "pack.json", manifest);
    auto package = *try_load_legacy(root);
    auto content = set({package});
    EXPECT(package.definitions.size() == 2 && package.definitions[0].kind == "appearance");
    EXPECT(package.definitions[0].data["chassis"] == "stock:bushwacker");
    EXPECT(content.lock["selections"]["appearances"].size() == 2 &&
           content.lock["legacy_resources"].empty());
    registry["models"][0]["environment"]["MW34_NOT_A_SUPPORTED_BINDING"] = "models/models.json";
    payload(manifest, root, "models/models.json", registry.dump());
    write_json_atomic(root / "pack.json", manifest);
    EXPECT(rejects([&] { try_load_legacy(root); }, "unsupported-legacy-binding"));
    manifest["native_payload"]["profile"] = "prepared-ui-v1";
    write_json_atomic(root / "pack.json", manifest);
    EXPECT(rejects([&] { try_load_legacy(root); }, "unsupported-legacy-profile"));
    registry["models"][0]["environment"].erase("MW34_NOT_A_SUPPORTED_BINDING");
    payload(manifest, root, "models/models.json", registry.dump());
    manifest["native_payload"]["profile"] = "prepared-native-v1";
    manifest["native_payload"]["required_capabilities"] = {
        {"prepared-mechs", uint64_t(4294967297ull)}};
    write_json_atomic(root / "pack.json", manifest);
    EXPECT(rejects([&] { try_load_legacy(root); }, "unsupported-legacy-capability"));
    manifest["native_payload"].erase("required_capabilities");
    registry["models"][0]["required_formats"].push_back("BWMESH999");
    payload(manifest, root, "models/models.json", registry.dump());
    write_json_atomic(root / "pack.json", manifest);
    EXPECT(rejects([&] { try_load_legacy(root); }, "unsupported-legacy-format"));
}
