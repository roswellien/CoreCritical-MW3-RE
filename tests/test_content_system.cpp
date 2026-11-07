#include "check.h"
#include "content/content.h"
#include "content/hash.h"
#include "content/legacy.h"
#include "content/persistence.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/content_campaign.h"
#include "mech3/content_legacy.h"
#include "mech3/content_network.h"
#include "mech3/content_registry.h"
#include "mech3/content_state.h"
#include "mech3/content_stock_campaign.h"
#include "mech3/mech_config.h"
#include "mech3/mods.h"
#include "mech3/pilot.h"
#include "mech3/source_archive.h"
#include "platform/host_path.h"
#include "platform/reg_file.h"

#include <fstream>
#include <functional>

namespace {
using namespace cc::content;
bool rejected(const std::function<void()> &action, const std::string &code = {}) {
    try {
        action();
        return false;
    } catch (const Error &error) {
        if (!code.empty() && error.diagnostic.code != code)
            std::fprintf(stderr, "Unexpected diagnostic: %s (expected %s)\n",
                         error.diagnostic.code.c_str(), code.c_str());
        return code.empty() || error.diagnostic.code == code;
    }
}
fs::path scratch(const char *name) {
    auto path = fs::path(MW3_TEST_TMP) / name;
    fs::create_directories(path);
    return path;
}
fs::path long_unicode_root(const char *name) {
    auto path = fs::path(MW3_TEST_TMP) / name / fs::u8path(u8"profile-\u6e2c\u8a66-\u00e9");
    while (path.native().size() < 330)
        path /= "long-profile-component-012345678901234567890123456789";
    fs::create_directories(platform::host_path(path));
    return path;
}
Json manifest(const std::string &id) {
    return {{"schema_version", 3}, {"game_id", "mw3"},        {"id", id},
            {"version", "1.0.0"},  {"files", Json::object()}, {"definitions", Json::array()}};
}
Json profile(std::initializer_list<std::pair<std::string, fs::path>> packages) {
    Json result = {{"schema_version", 1}, {"packages", Json::array()}};
    for (const auto &package : packages)
        result["packages"].push_back({{"id", package.first},
                                      {"version", "1.0.0"},
                                      {"path", package.second.generic_string()}});
    return result;
}
} // namespace

TEST(content_long_unicode_paths_preserve_atomic_saves_and_package_roundtrips) {
    const auto root = long_unicode_root("content-long-host-path");
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(platform::host_path(path), error);
        }
    } cleanup{fs::path(MW3_TEST_TMP) / "content-long-host-path"};
    EXPECT(root.native().size() > 260);
#if defined(_WIN32)
    EXPECT(platform::host_path(root).native().rfind(L"\\\\?\\", 0) == 0);
    const auto unc = fs::path(L"\\\\server\\share\\folder");
    EXPECT(platform::host_path(unc).native() == L"\\\\?\\UNC\\server\\share\\folder");
    EXPECT(platform::host_path(fs::path(L"\\\\?\\unc\\server\\share\\folder")) ==
           platform::host_path(unc));
#endif
    ResolvedContentSet content;
    content.lock = {{"packages", Json::array()}};
    content.gameplay_digest = sha256("long-path-gameplay");
    content.session_digest = sha256("long-path-handles");
    const auto save =
        root /
        "campaign-run-0123456789012345678901234567890123456789012345678901234567890123.ccsave";
    const Json state = {{"score", 7}};
    const std::vector<uint8_t> world{1, 2, 3, 4};
    save_bundle(save, state, world, content);
    const auto before = file_sha256(save);
    EXPECT(load_bundle(save, content).world == world);
    bool failed = false;
#if defined(_WIN32)
    auto *held = platform::file_open(save, "rb");
    EXPECT(held != nullptr);
    if (!held)
        return;
    try {
        save_bundle(save, {{"score", 99}}, {9, 9}, content);
    } catch (const Error &error) {
        failed = error.diagnostic.code == "save-activate";
    }
    std::fclose(held);
#else
    try {
        save_bundle(save / "blocked-child", {{"score", 99}}, {9, 9}, content);
    } catch (const std::exception &) {
        failed = true;
    }
#endif
    EXPECT(failed && before == file_sha256(save));
    EXPECT(load_bundle(save, content).state == state);
    const auto source = root / "source";
    fs::create_directories(platform::host_path(source));
    const std::string payload = "project-authored long Unicode path payload";
    {
        std::ofstream output(platform::host_path(source / "payload.bin"), std::ios::binary);
        output << payload;
    }
    auto package = manifest("long-path");
    package["files"]["payload.bin"] = {{"bytes", payload.size()}, {"sha256", sha256(payload)}};
    write_json_atomic(source / "pack.json", package);
    EXPECT(read_json(source / "pack.json") == package);
    const auto archive = root / "package.mw3pack";
    package_directory(source, archive);
    const auto store = root / "store";
    const auto installed = install_package(source, store);
    if (mech3::source::archives_supported())
        EXPECT(install_package(archive, root / "archive-store").filename() == installed.filename());
    const Json selected = {
        {"schema_version", 1},
        {"packages", Json::array({{{"id", "long-path"}, {"version", "1.0.0"}}})}};
    const auto resolved = resolve(selected, root, store, {}, {});
    EXPECT(resolved.packages.size() == 1);
    EXPECT(platform::logical_host_path(resolved.packages.front().root) ==
           resolved.packages.front().root);
    EXPECT(file_sha256(resolved.resource("long-path", "payload.bin")) == sha256(payload));
}

TEST(content_long_unicode_profile_supports_native_vfs_and_managed_pilot_reads) {
    struct Hooks final : mech3::PilotHooks {
        void store_fill(mech3::Store &, int32_t) override {}
        int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
    } hooks;
    struct Restore {
        std::shared_ptr<const ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        ~Restore() {
            mech3::activate_content(content);
            zutl::mounts() = mounts;
            mech3::set_pilot_hooks(hooks);
        }
    } restore;
    const auto root = long_unicode_root("content-long-native-path");
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(platform::host_path(path), error);
        }
    } cleanup{fs::path(MW3_TEST_TMP) / "content-long-native-path"};
    zutl::mounts() = {};
    zutl::set_write_dir(root.generic_u8string());
    zutl::mounts().confined = true;
    auto *output = zutl::open("native/io.bin", "wb");
    EXPECT(output != nullptr);
    if (!output)
        return;
    EXPECT(std::fwrite("native", 1, 6, output) == 6);
    std::fclose(output);
    auto *input = zutl::open("native/io.bin", "rb");
    EXPECT(input != nullptr);
    if (!input)
        return;
    char bytes[6]{};
    EXPECT(std::fread(bytes, 1, 6, input) == 6 && std::string(bytes, 6) == "native");
    std::fclose(input);
    EXPECT(zutl::list("native/*.bin") == std::vector<std::string>{"io.bin"});
    // Keep a stock archive entry available: a failed selected loose read must
    // not look like a successful fallback to this different value.
    std::string binary;
    auto word = [&](uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            binary.push_back(static_cast<char>(value >> (8 * i)));
    };
    word(4);
    word(3);
    word(3);
    word(6);
    binary += "winner";
    word(1);
    word(5);
    zutl_zar::Archive archive;
    zutl_zar::init(archive);
    EXPECT(zutl_zar::create(archive, "zbd/reader.zbd"));
    zutl_zar::add(archive, "overlay.zrd", binary.data(), static_cast<uint32_t>(binary.size()));
    zutl_zar::close(archive);
    zutl_zar::destroy(archive);
    const auto overlay = root / "legacy";
    const std::string loose = "(\"winner\" 77)", strings = "7=Selected Unicode overlay\n";
    fs::create_directories(platform::host_path(overlay / "zbd"));
    {
        std::ofstream output(platform::host_path(overlay / "zbd/overlay.zrd"), std::ios::binary);
        output << loose;
    }
    {
        std::ofstream output(platform::host_path(overlay / "strings.txt"), std::ios::binary);
        output << strings;
    }
    const Json legacy = {
        {"schema_version", 1},
        {"id", "long-overlay"},
        {"version", "1.0.0"},
        {"files",
         {{"zbd/overlay.zrd", {{"bytes", loose.size()}, {"sha256", sha256(loose)}}},
          {"strings.txt", {{"bytes", strings.size()}, {"sha256", sha256(strings)}}}}}};
    write_json_atomic(overlay / "pack.json", legacy);
    mech3::set_pilot_hooks(&hooks);
    auto content = std::make_shared<ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    auto package = load_package(overlay);
    content->packages.push_back(package);
    content->definitions.insert(content->definitions.end(), package.definitions.begin(),
                                package.definitions.end());
    resolve_legacy_claims(*content, Json::object());
    mech3::activate_content(content);
    mech3::mount_content_legacy(*content);
    struct RestoreReader {
        zrdr::State previous = zrdr::state();
        RestoreReader() {
            zrdr::state() = {};
            zrdr::init();
        }
        ~RestoreReader() {
            zrdr::close_archives(true);
            zrdr::state() = previous;
        }
    } reader;
    EXPECT(zrdr::open_archive("zbd/reader.zbd", true));
    auto *tree = zrdr::load("overlay.zrd");
    int32_t winner = 0;
    EXPECT(tree && zrdr::get_int(tree, "winner", &winner) && winner == 77);
    if (tree)
        zrdr::free_node(tree);
    std::map<int32_t, std::string> text;
    mech3::mods_apply_strings(text, {});
    EXPECT(text[7] == "Selected Unicode overlay");
    mech3::Pilot pilot("LongProfile");
    pilot.dirty = 0;
    pilot.training->mission = 3;
    auto &campaign = *static_cast<mech3::CampaignGame *>(pilot.campaigns.front());
    campaign.slot.mech = -1;
    EXPECT(mech3::adopt_content_stock_campaign(campaign));
    campaign.c4 = 73;
    const auto run = campaign.content_run;
    EXPECT(pilot.save());
    campaign.c4 = 74;
    EXPECT(pilot.save()); // Refresh the already accepted semantic bundle.
    auto *loaded = mech3::load_pilot("LongProfile", false);
    EXPECT(loaded && loaded->training->mission == 3);
    bool campaign_loaded = false;
    if (loaded) {
        for (const auto *entry : loaded->campaigns) {
            const auto *game = static_cast<const mech3::CampaignGame *>(entry);
            if (game->content_run == run)
                campaign_loaded = game->c4 == 74;
        }
        mech3::pilots().remove(loaded);
        loaded->dirty = 0;
        delete loaded;
    }
    EXPECT(campaign_loaded);
    const auto settings_path = (root / "settings.reg").generic_u8string();
    const uint32_t expected = 42;
    {
        platform::RegFile settings(settings_path);
        EXPECT(settings.open("Software\\LongPathFixture", true, true));
        EXPECT(settings.write(true, "value", 4, &expected, sizeof expected));
        settings.close(true);
    }
    platform::RegFile settings(settings_path);
    EXPECT(settings.open("Software\\LongPathFixture", true, false));
    uint32_t value = 0, size = sizeof value;
    EXPECT(settings.read(true, "value", &value, &size) && value == expected);
    // A correctly hashed malformed selected file must not reactivate the valid
    // stock archive entry. Re-admit the new authored bytes as a fresh profile.
    const std::string truncated = "(\"winner\" 77";
    {
        std::ofstream output(platform::host_path(overlay / "zbd/overlay.zrd"),
                             std::ios::binary | std::ios::trunc);
        output << truncated;
    }
    auto changed_manifest = legacy;
    changed_manifest["files"]["zbd/overlay.zrd"] = {{"bytes", truncated.size()},
                                                    {"sha256", sha256(truncated)}};
    write_json_atomic(overlay / "pack.json", changed_manifest);
    auto malformed = std::make_shared<ResolvedContentSet>();
    malformed->definitions = mech3::stock_content();
    auto changed = load_package(overlay);
    malformed->packages.push_back(changed);
    malformed->definitions.insert(malformed->definitions.end(), changed.definitions.begin(),
                                  changed.definitions.end());
    resolve_legacy_claims(*malformed, Json::object());
    mech3::activate_content(malformed);
    mech3::mount_content_legacy(*malformed);
    zutl::mounts().strict_reads = true;
    bool failed = false;
    try {
        auto *unexpected = zrdr::load("overlay.zrd");
        if (unexpected)
            zrdr::free_node(unexpected);
    } catch (const std::runtime_error &) {
        failed = true;
    }
    EXPECT(failed);
}
TEST(content_json_hash_and_path_boundaries) {
    EXPECT(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256 hash;
    hash.update("a", 1);
    hash.update("bc", 2);
    EXPECT(hash.finish() == sha256("abc"));
    EXPECT(rejected([] { parse_json("{\"a\":1,\"a\":2}"); }, "duplicate-key"));
    EXPECT(parse_json("[1,1.0]")[0].is_number_integer());
    EXPECT(parse_json("[1,1.0]")[1].is_number_float());
    EXPECT(rejected([] { parse_json(std::string(70, '[') + "0" + std::string(70, ']')); },
                    "json-limit"));
    auto root = scratch("content_paths");
    EXPECT(rejected([&] { confined_path(root, "../escape"); }, "unsafe-path"));
    EXPECT(rejected([&] { confined_path(root, "C:/escape"); }, "unsafe-path"));
    EXPECT(rejected([&] { confined_path(root, "a\\escape"); }, "unsafe-path"));
    for (const char *name :
         {"data.txt.", "data.txt ", ".. /escape", "CON.txt", "a//b", "a?.bin", "caf\xc3\xa9.png"})
        EXPECT(rejected([&] { confined_path(root, name); }, "unsafe-path"));
}
TEST(content_composition_conflicts_and_stable_handles) {
    auto root = scratch("content_composition"), one = root / "one", two = root / "two";
    auto a = manifest("one"), b = manifest("two");
    a["definitions"].push_back({{"kind", "equipment"},
                                {"id", "one:item"},
                                {"behavior", "existing"},
                                {"data", {{"mass", 1.5}, {"slots", 2}}}});
    b["definitions"].push_back({{"kind", "equipment"},
                                {"id", "two:item"},
                                {"behavior", "existing"},
                                {"data", {{"mass", 2.0}, {"slots", 1}}}});
    a["patches"] =
        Json::array({{{"kind", "equipment"}, {"id", "stock:ecm"}, {"changes", {{"mass", 2.0}}}}});
    b["patches"] =
        Json::array({{{"kind", "equipment"}, {"id", "stock:ecm"}, {"changes", {{"slots", 3}}}}});
    write_json_atomic(one / "pack.json", a);
    write_json_atomic(two / "pack.json", b);
    std::vector<Definition> stock{
        {"equipment", "stock:ecm", "stock", "existing", {{"mass", 1.0}, {"slots", 2}}, 8}};
    auto selected = profile({{"one", one}, {"two", two}});
    auto first = resolve(selected, root, root / "store", stock, {});
    EXPECT(first.find("equipment", "stock:ecm")->data["mass"] == 2.0);
    EXPECT(first.find("equipment", "stock:ecm")->data["slots"] == 3);
    std::reverse(selected["packages"].begin(), selected["packages"].end());
    auto reordered = resolve(selected, root, root / "store", stock, {});
    EXPECT(first.gameplay_digest == reordered.gameplay_digest &&
           first.session_digest == reordered.session_digest);
    EXPECT(first.find("equipment", "one:item")->handle == 9 &&
           first.find("equipment", "two:item")->handle == 10);
    b["patches"][0]["changes"] = {{"mass", 3.0}};
    write_json_atomic(two / "pack.json", b);
    EXPECT(rejected([&] { resolve(selected, root, root / "store", stock, {}); }, "patch-conflict"));
    selected["conflicts"] = Json::array(
        {{{"kind", "equipment"}, {"id", "stock:ecm"}, {"field", "/mass"}, {"winner", "two"}}});
    EXPECT(resolve(selected, root, root / "store", stock, {})
               .find("equipment", "stock:ecm")
               ->data["mass"] == 3.0);
    a["required_capabilities"] = {{"unimplemented", 1}};
    write_json_atomic(one / "pack.json", a);
    EXPECT(rejected([&] { resolve(selected, root, root / "store", stock, {}); },
                    "unsupported-capability"));
}
TEST(content_integrity_install_and_dependency_failures) {
    auto root = scratch("content_install"), source = root / "source", store = root / "store";
    fs::create_directories(source);
    { std::ofstream(source / "data.bin", std::ios::binary) << "abc"; }
    auto value = manifest("fixture");
    value["files"]["data.bin"] = {{"bytes", 3}, {"sha256", sha256("abc")}};
    write_json_atomic(source / "pack.json", value);
    auto installed = install_package(source, store);
    EXPECT(fs::exists(installed / "data.bin"));
    EXPECT(install_package(source, store) == installed);
    { std::ofstream(source / "data.bin", std::ios::binary) << "bad"; }
    EXPECT(rejected([&] { install_package(source, store); }, "payload-integrity"));
    EXPECT(file_sha256(installed / "data.bin") == sha256("abc"));
    auto selected = profile({{"fixture", installed}});
    auto result = resolve(selected, root, store, {}, {});
    EXPECT(result.packages.size() == 1);
    value["dependencies"] = Json::array({{{"id", "missing"}, {"min_version", "1.0.0"}}});
    value["files"]["data.bin"]["sha256"] = sha256("bad");
    write_json_atomic(source / "pack.json", value);
    EXPECT(rejected([&] { resolve(profile({{"fixture", source}}), root, root / "empty", {}, {}); },
                    "dependencies"));
    value["dependencies"] = Json::array({{{"id", "fixture"}, {"version", "1.0.0"}}});
    write_json_atomic(source / "pack.json", value);
    EXPECT(rejected([&] { resolve(profile({{"fixture", source}}), root, root / "empty", {}, {}); },
                    "dependency-cycle"));
}
TEST(content_save_preflight_atomicity_and_explicit_migration) {
    auto root = scratch("content_saves");
    ResolvedContentSet content;
    content.lock = {{"schema_version", 1}, {"packages", Json::array()}};
    content.gameplay_digest = sha256("gameplay");
    content.session_digest = sha256("mapping");
    content.definitions.push_back(
        {"campaign",
         "fixture:campaign",
         "fixture",
         "graph",
         {{"nodes", Json::array({{{"id", "start"}, {"mission", "fixture:mission"}}})}},
         0});
    content.definitions.push_back(
        {"mission", "fixture:mission", "fixture", "native", Json::object(), 0});
    Json state = {{"campaign", "fixture:campaign"},
                  {"mission", "fixture:mission"},
                  {"graph",
                   {{"schema", 1},
                    {"campaign", "fixture:campaign"},
                    {"run", sha256("fixture-run")},
                    {"node", "start"},
                    {"attempt", 1},
                    {"terminal", false}}},
                  {"world_digest", sha256("world")},
                  {"inventory", Json::array()}};
    save_bundle(root / "save.ccsave", state, {1, 2, 3}, content);
    auto loaded = load_bundle(root / "save.ccsave", content);
    EXPECT(loaded.state == state && loaded.world == std::vector<uint8_t>({1, 2, 3}));
    auto changed = content;
    changed.lock["packages"].push_back({{"id", "other"}, {"version", "2.0.0"}});
    EXPECT(rejected([&] { load_bundle(root / "save.ccsave", changed); }, "save-content"));
    Json migration = {{"schema_version", 1},
                      {"source_lock_sha256", sha256(content.lock.dump())},
                      {"target_lock_sha256", sha256(changed.lock.dump())}};
    migrate_bundle(root / "save.ccsave", root / "new.ccsave", migration, changed);
    EXPECT(load_bundle(root / "new.ccsave", changed).state == state);
    EXPECT(fs::exists(root / "new.ccsave.source-backup"));
    EXPECT(load_bundle(root / "save.ccsave", content).state == state);
    { std::ofstream(root / "broken.ccsave", std::ios::binary) << "CCSAVE1\n"; }
    EXPECT(rejected([&] { load_bundle(root / "broken.ccsave", content); }, "save-truncated"));
}
TEST(content_session_rejects_missing_and_different_gameplay) {
    struct Restore {
        std::shared_ptr<const ResolvedContentSet> previous = mech3::active_content();
        ~Restore() { mech3::activate_content(previous); }
    } restore;
    auto content = std::make_shared<ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    content->lock = {{"selections", Json::object()}};
    content->gameplay_digest = sha256("one");
    content->session_digest = sha256("map");
    for (auto &definition : content->definitions)
        if (definition.kind == "equipment" && definition.id == "stock:ecm")
            definition.data["weight"] = {1.25, 1.75};
    mech3::activate_content(content);
    EXPECT(mech3::managed_gameplay_session());
    std::vector<uint32_t> player{1, 0, 0, 0, 0, 0x53444f4d, 0, 0, 0};
    auto legacy = player;
    mech3::append_content_session(player);
    EXPECT(mech3::content_sessions_match(player));
    EXPECT(!mech3::content_sessions_match(legacy));
    player.back() ^= 1;
    EXPECT(!mech3::content_sessions_match(player));
    player.pop_back();
    EXPECT(!mech3::content_sessions_match(player));
}

TEST(content_resource_and_schema_reject_ambiguous_or_narrowed_inputs) {
    EXPECT(rejected([] { resource_reference("owner", "one::two::file"); }, "resource-owner"));
    EXPECT(rejected([] { resource_reference("owner", "one::"); }, "resource-owner"));
    auto root = scratch("content_schema_limits");
    auto a = manifest("one");
    a["schema_version"] = uint64_t(0x100000003ull);
    write_json_atomic(root / "pack.json", a);
    EXPECT(rejected([&] { load_package(root); }, "unsupported-schema"));
    a["schema_version"] = 3;
    a["dependencies"] = Json::array({{{"id", "two"}}, {{"id", "two"}}});
    write_json_atomic(root / "pack.json", a);
    EXPECT(rejected([&] { resolve(profile({{"one", root}}), root, root / "store", {}, {}); }));
    a.erase("dependencies");
    a["required_capabilities"] = {{"supported", uint64_t(0x100000001ull)}};
    write_json_atomic(root / "pack.json", a);
    EXPECT(rejected(
        [&] { resolve(profile({{"one", root}}), root, root / "store", {}, {{"supported", 1}}); },
        "unsupported-capability"));
}

TEST(content_solver_backtracks_for_capability_conflict_optional_and_cycle) {
    auto root = scratch("content_solver");
    auto older = manifest("dependency"), newer = older, base = manifest("root");
    newer["version"] = "2.0.0";
    base["dependencies"] = Json::array({{{"id", "dependency"}}});
    write_json_atomic(root / "root" / "pack.json", base);
    write_json_atomic(root / "store" / "old" / "pack.json", older);
    auto check = [&] {
        write_json_atomic(root / "store" / "new" / "pack.json", newer);
        auto result = resolve(profile({{"root", root / "root"}}), root, root / "store", {}, {});
        for (auto &pack : result.packages)
            if (pack.id == "dependency")
                return pack.version == "1.0.0";
        return false;
    };
    newer["required_capabilities"] = {{"unsupported", 1}};
    EXPECT(check());
    newer.erase("required_capabilities");
    newer["conflicts"] = Json::array({"root"});
    EXPECT(check());
    newer.erase("conflicts");
    newer["dependencies"] = Json::array({{{"id", "root"}}});
    EXPECT(check());
    newer.erase("dependencies");
    base["dependencies"].push_back({{"id", "optional"}, {"version", "1.0.0"}});
    auto optional = manifest("optional");
    write_json_atomic(root / "store" / "optional" / "pack.json", optional);
    write_json_atomic(root / "root" / "pack.json", base);
    newer["dependencies"] =
        Json::array({{{"id", "optional"}, {"version", "2.0.0"}, {"optional", true}}});
    EXPECT(check());
}

TEST(content_patch_subtrees_and_numeric_types_require_resolution) {
    auto root = scratch("content_patch_tree");
    auto a = manifest("one"), b = manifest("two");
    std::vector<Definition> stock{{"equipment",
                                   "stock:item",
                                   "stock",
                                   "native",
                                   {{"value", 1}, {"nested", {{"a", 1}, {"b", 2}}}},
                                   0}};
    a["patches"] =
        Json::array({{{"kind", "equipment"}, {"id", "stock:item"}, {"changes", {{"nested", 7}}}}});
    b["patches"] = Json::array(
        {{{"kind", "equipment"}, {"id", "stock:item"}, {"changes", {{"nested", {{"a", 3}}}}}}});
    write_json_atomic(root / "one" / "pack.json", a);
    write_json_atomic(root / "two" / "pack.json", b);
    auto chosen = profile({{"one", root / "one"}, {"two", root / "two"}});
    EXPECT(rejected([&] { resolve(chosen, root, root / "store", stock, {}); }, "patch-conflict"));
    chosen["conflicts"] = Json::array(
        {{{"kind", "equipment"}, {"id", "stock:item"}, {"field", "/nested"}, {"winner", "two"}}});
    auto result = resolve(chosen, root, root / "store", stock, {});
    EXPECT(result.find("equipment", "stock:item")->data.at("nested") == Json({{"a", 3}, {"b", 2}}));
    chosen.erase("conflicts");
    a["patches"][0]["changes"] = {{"value", 1}};
    b["patches"][0]["changes"] = {{"value", 1.0}};
    write_json_atomic(root / "one" / "pack.json", a);
    write_json_atomic(root / "two" / "pack.json", b);
    EXPECT(rejected([&] { resolve(chosen, root, root / "store", stock, {}); }, "patch-conflict"));
}

TEST(content_install_empty_payload_and_corrupted_immutable_target) {
    auto root = scratch("content_empty_payload");
    auto value = manifest("empty");
    fs::create_directories(root / "source");
    std::ofstream(root / "source" / "empty.dat", std::ios::binary);
    value["files"]["empty.dat"] = {{"bytes", 0}, {"sha256", sha256("")}};
    write_json_atomic(root / "source" / "pack.json", value);
    package_directory(root / "source", root / "empty.mw3pack");
    const auto source =
        mech3::source::archives_supported() ? root / "empty.mw3pack" : root / "source";
    if (!mech3::source::archives_supported())
        EXPECT(rejected([&] { install_package(root / "empty.mw3pack", root / "store"); },
                        "archive-unavailable"));
    auto installed = install_package(source, root / "store");
    EXPECT(fs::file_size(installed / "empty.dat") == 0);
    auto corrupted = read_json(installed / "pack.json");
    corrupted["label"] = "tampered";
    write_json_atomic(installed / "pack.json", corrupted);
    EXPECT(rejected([&] { install_package(source, root / "store"); }, "install-integrity"));
    EXPECT(read_json(installed / "pack.json")["label"] == "tampered");
}

TEST(content_migration_changes_typed_ids_and_requires_world_boundary) {
    auto root = scratch("content_typed_migration");
    ResolvedContentSet old, target;
    old.lock = {{"packages", Json::array()}};
    old.gameplay_digest = sha256("old");
    old.session_digest = sha256("map-old");
    target = old;
    target.lock["generation"] = 2;
    target.definitions = {
        {"weapon", "new:weapon", "new", "native", {}, 0},
        {"mission", "new:mission", "new", "native", {}, 0},
        {"mission", "new:other", "new", "native", {}, 1},
        {"campaign",
         "new:campaign",
         "new",
         "graph",
         {{"nodes", Json::array({{{"id", "start"}, {"mission", "new:mission"}},
                                 {{"id", "finish"}, {"mission", "new:mission"}}})}},
         0}};
    Json state = {{"campaign", "old:campaign"},
                  {"mission", "old:mission"},
                  {"graph",
                   {{"schema", 1},
                    {"campaign", "old:campaign"},
                    {"run", sha256("migration-run")},
                    {"node", "start"},
                    {"attempt", 1},
                    {"terminal", false},
                    {"variables", {{"story", "old:weapon"}}}}},
                  {"name", "old:weapon"},
                  {"variables", {{"story", "old:weapon"}}},
                  {"inventory", Json::array({{{"kind", 1}, {"id", "old:weapon"}}})},
                  {"base_digest", sha256("base1")},
                  {"world_digest", sha256("world")}};
    save_bundle(root / "original", state, {1, 2, 3}, old);
    Json migration = {{"schema_version", 1},
                      {"source_lock_sha256", sha256(old.lock.dump())},
                      {"target_lock_sha256", sha256(target.lock.dump())},
                      {"ids",
                       {{"old:campaign", "new:campaign"},
                        {"old:weapon", "new:weapon"},
                        {"old:mission", "new:mission"}}}};
    migrate_bundle(root / "original", root / "typed", migration, target);
    auto result = load_bundle(root / "typed", target).state;
    EXPECT(result["campaign"] == "new:campaign" && result["inventory"][0]["id"] == "new:weapon");
    EXPECT(result["graph"]["run"] == state["graph"]["run"]);
    EXPECT(result["name"] == "old:weapon" && result["variables"]["story"] == "old:weapon");
    auto invalid_node = migration;
    invalid_node["campaign_nodes"] = {{"start", "typo"}};
    EXPECT(rejected(
        [&] { migrate_bundle(root / "original", root / "bad-node", invalid_node, target, true); },
        "campaign-state"));
    EXPECT(!fs::exists(root / "bad-node") && !fs::exists(root / "bad-node.source-backup"));
    invalid_node["campaign_nodes"] = {{"start", 1}};
    EXPECT(rejected(
        [&] { migrate_bundle(root / "original", root / "bad-node", invalid_node, target, true); },
        "migration-nodes"));
    auto invalid_mission = migration;
    invalid_mission["ids"]["old:mission"] = "new:other";
    EXPECT(rejected(
        [&] {
            migrate_bundle(root / "original", root / "bad-mission", invalid_mission, target, true);
        },
        "campaign-state"));
    auto invalid_variables = migration;
    invalid_variables["variables"] = {{"story", 1}};
    EXPECT(rejected(
        [&] {
            migrate_bundle(root / "original", root / "bad-variable", invalid_variables, target,
                           true);
        },
        "migration-variables"));
    auto valid_node = migration;
    valid_node["campaign_nodes"] = {{"start", "finish"}};
    auto node_preview =
        migrate_bundle(root / "original", root / "mapped-node", valid_node, target, true);
    EXPECT(node_preview["preview"] == true && !node_preview["state_changes"].empty());
    auto wrong = migration;
    wrong["ids"]["old:weapon"] = "new:campaign";
    EXPECT(rejected([&] { migrate_bundle(root / "original", root / "wrong", wrong, target); },
                    "migration-type"));
    auto missing = migration;
    missing["ids"].erase("old:weapon");
    EXPECT(rejected([&] { migrate_bundle(root / "original", root / "missing", missing, target); },
                    "migration-type"));
    migration["base_digest"] = sha256("base2");
    EXPECT(rejected([&] { migrate_bundle(root / "original", root / "base", migration, target); },
                    "migration-world"));
    migration["reset_world"] = true;
    migration["mission_boundary"] = true;
    migration["world_digest"] = sha256("new-world");
    auto preview = migrate_bundle(root / "original", root / "reset", migration, target, true);
    EXPECT(preview["preview"] == true && preview["world_bytes_after"] == 0);
    EXPECT(!fs::exists(root / "reset") && !fs::exists(root / "reset.source-backup"));
    migrate_bundle(root / "original", root / "reset", migration, target);
    auto reset = load_bundle(root / "reset", target);
    EXPECT(reset.world.empty() && reset.state["base_digest"] == sha256("base2"));
    EXPECT(load_bundle(root / "original", old).world.size() == 3);
}

TEST(content_migration_resets_opaque_native_snapshots_when_gameplay_or_handle_maps_change) {
    const auto root = scratch("content_native_snapshot_migration");
    ResolvedContentSet source;
    source.lock = {{"packages", Json::array()}};
    source.gameplay_digest = sha256("source-gameplay");
    source.session_digest = sha256("source-handles");
    source.definitions = {{"weapon", "fixture:weapon", "fixture", "native", {{"damage", 1}}, 0}};
    const Json state = {{"world_snapshot", true},
                        {"world_digest", sha256("source-world")},
                        {"inventory", Json::array({{{"kind", 1}, {"id", "fixture:weapon"}}})}};
    const std::vector<uint8_t> world = {1, 2, 3, 4};
    save_bundle(root / "original", state, world, source);
    const auto original_hash = file_sha256(root / "original");
    for (const std::string kind : {"gameplay", "handles"}) {
        auto target = source;
        target.lock["generation"] = kind;
        if (kind == "gameplay") {
            target.gameplay_digest = sha256("changed-gameplay");
            target.definitions[0].data["damage"] = 2;
        } else {
            target.session_digest = sha256("changed-handles");
            target.definitions[0].handle = 1;
        }
        Json migration = {{"schema_version", 1},
                          {"source_lock_sha256", sha256(source.lock.dump())},
                          {"target_lock_sha256", sha256(target.lock.dump())},
                          {"ids", {{"fixture:weapon", "fixture:weapon"}}}};
        const auto output = root / kind;
        for (const bool preview : {true, false}) {
            EXPECT(rejected(
                [&] { migrate_bundle(root / "original", output, migration, target, preview); },
                "migration-world"));
            EXPECT(!fs::exists(output) && !fs::exists(output.string() + ".source-backup"));
            EXPECT(original_hash == file_sha256(root / "original"));
        }
        migration["reset_world"] = true;
        EXPECT(rejected([&] { migrate_bundle(root / "original", output, migration, target, true); },
                        "migration-world"));
        EXPECT(!fs::exists(output) && !fs::exists(output.string() + ".source-backup"));
        migration["mission_boundary"] = true;
        migration["world_digest"] = sha256("new-world");
        const auto report = migrate_bundle(root / "original", output, migration, target, true);
        EXPECT(report["world_bytes_before"] == world.size() && report["world_bytes_after"] == 0);
        EXPECT(!fs::exists(output) && !fs::exists(output.string() + ".source-backup"));
        migrate_bundle(root / "original", output, migration, target);
        const auto reset = load_bundle(output, target);
        EXPECT(reset.world.empty() && reset.state["world_snapshot"] == false);
        EXPECT(reset.state["inventory"] == state["inventory"]);
        EXPECT(file_sha256(output.string() + ".source-backup") == original_hash);
    }
    auto cosmetic = source;
    cosmetic.lock["packages"].push_back({{"id", "fixture-cosmetic"}, {"version", "1.0.0"}});
    const Json cosmetic_migration = {{"schema_version", 1},
                                     {"source_lock_sha256", sha256(source.lock.dump())},
                                     {"target_lock_sha256", sha256(cosmetic.lock.dump())}};
    migrate_bundle(root / "original", root / "cosmetic", cosmetic_migration, cosmetic);
    const auto preserved = load_bundle(root / "cosmetic", cosmetic);
    EXPECT(preserved.world == world && preserved.state == state);
    EXPECT(original_hash == file_sha256(root / "original"));
}

TEST(content_managed_loadout_uses_stable_identity_and_complete_envelope) {
    struct Restore {
        std::shared_ptr<const ResolvedContentSet> previous = mech3::active_content();
        ~Restore() { mech3::activate_content(previous); }
    } restore;
    auto content = std::make_shared<ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    content->lock = {{"selections", Json::object()}};
    content->gameplay_digest = sha256("gameplay");
    content->session_digest = sha256("mapping");
    mech3::activate_content(content);
    mech3::Config config(0, "network");
    config.armor_tech = 3;
    config.sink_tech = 3;
    auto packed = mech3::pack_managed_config(config, "cosmetic:blue");
    std::string skin;
    std::unique_ptr<mech3::Config> copy(
        mech3::unpack_managed_config(packed.data(), packed.size(), &skin));
    EXPECT(copy->mech == config.mech && skin == "cosmetic:blue");
    EXPECT(rejected([&] { mech3::unpack_managed_config(packed.data(), packed.size() - 1); },
                    "network-loadout"));
    zfix::options().bugs = !zfix::options().bugs;
    EXPECT(rejected([&] { mech3::unpack_managed_config(packed.data(), packed.size()); },
                    "network-content"));
    zfix::options().bugs = !zfix::options().bugs;
    auto invalid = mech3::config_state(config);
    invalid["armor"][0] = 1.5;
    EXPECT(rejected([&] { mech3::config_from_state(invalid); }, "save-armor"));
    invalid = mech3::config_state(config);
    invalid["items"] = Json::array({{{"kind", 3},
                                     {"id", "stock:ecm"},
                                     {"count", 1},
                                     {"part", uint64_t(-1)},
                                     {"group", 0},
                                     {"tech", 3}}});
    EXPECT(rejected([&] { mech3::config_from_state(invalid); }, "save-field"));
}

TEST(content_fingerprint_includes_transitive_gltf_resources_and_preserves_cosmetics) {
    auto root = scratch("content_gltf_identity"), source = root / "source";
    fs::create_directories(source);
    auto value = manifest("geometry");
    value["definitions"].push_back({{"kind", "world"},
                                    {"id", "geometry:world"},
                                    {"behavior", "authored"},
                                    {"data", {{"source", "world.GLTF"}}}});
    write_json_atomic(source / "world.GLTF",
                      {{"asset", {{"version", "2.0"}}},
                       {"buffers", Json::array({{{"uri", "geometry.bin"}, {"byteLength", 3}}})}});
    { std::ofstream(source / "geometry.bin", std::ios::binary) << "abc"; }
    { std::ofstream(source / "cosmetic.png", std::ios::binary) << "one"; }
    auto update = [&] {
        for (const char *name : {"world.GLTF", "geometry.bin", "cosmetic.png"})
            value["files"][name] = {{"bytes", fs::file_size(source / name)},
                                    {"sha256", file_sha256(source / name)}};
        write_json_atomic(source / "pack.json", value);
        return resolve(profile({{"geometry", source}}), root, root / "store", {}, {});
    };
    auto before = update();
    { std::ofstream(source / "geometry.bin", std::ios::binary) << "def"; }
    auto changed = update();
    EXPECT(rejected([&] { before.resource("geometry", "geometry.bin"); }, "resource-changed"));
    EXPECT(before.gameplay_digest != changed.gameplay_digest);
    { std::ofstream(source / "cosmetic.png", std::ios::binary) << "two"; }
    auto cosmetic = update();
    EXPECT(changed.gameplay_digest == cosmetic.gameplay_digest &&
           changed.session_digest == cosmetic.session_digest);
}
