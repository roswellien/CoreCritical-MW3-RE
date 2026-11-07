#include "check.h"
#include "content/content.h"
#include "content/legacy.h"
#include "gamez/zclass/cls_zbd.h"
#include "gamez/zinterp/zinterp.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/content_base.h"
#include "mech3/content_legacy.h"
#include "mech3/content_registry.h"

#include <chrono>
#include <cstring>
#include <fstream>

namespace {
namespace fs = std::filesystem;
void put(const fs::path &path, const std::string &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << bytes;
}
struct Mounts {
    zutl::Mounts saved = zutl::mounts();
    Mounts() {
        zutl::mounts() = {};
        mech3::clear_content_base_digest_cache();
    }
    ~Mounts() {
        zutl::mounts() = saved;
        mech3::clear_content_base_digest_cache();
    }
};
} // namespace

TEST(content_base_fingerprint_is_empty_without_runtime_mounts) {
    Mounts mounts;
    EXPECT(mech3::content_base_digest() == cc::content::sha256("[]"));
}

TEST(content_base_fingerprint_tracks_native_bytes_fallbacks_and_mount_order) {
    Mounts mounts;
    auto root = fs::path(MW3_TEST_TMP) / "content_base", install = root / "install",
         patch = root / "patch";
    put(install / "zbd/reader.zbd", "authored reader bank");
    put(install / "zbd/interp.zbd", "authored interpreter bank");
    put(install / "zbd/c1/gamez.zbd", "authored world collision bank");
    put(install / "zbd/c1/readerm2.zbd", "authored mission reader");
    put(install / "c1.gs", "authored world setup commands");
    put(install / "support/c1size.gw", "authored world pool sizing commands");
    put(patch / "zbd/reader.zbd", "authored reader patch");
    put(install / "zbd/rtexture.zbd", "authored opaque texture-named bank");
    put(install / "zbd/sounds.zbd", "authored opaque sound-named bank");
    zutl::mount(patch.generic_string());
    zutl::mount(install.generic_string());
    auto first = mech3::content_base_digest();
    // Logging, persistence and unrelated directories do not enter this native
    // behavior identity. The scanner never opens SQLite or traverses those dirs.
    put(install / "runtime.log", "log changes");
    put(install / "pilots/pilot.zrd", "player persistence");
    put(install / "unrelated/active.sqlite", "unrelated database");
    mech3::clear_content_base_digest_cache();
    EXPECT(first == mech3::content_base_digest());
    // Mission setup adds these aliases; effective resource identities stay equal.
    zutl::mount(patch.generic_string() + "/zbd/c1");
    zutl::mount(install.generic_string() + "/zbd/c1");
    zutl::mount(patch.generic_string() + "/zbd");
    zutl::mount(install.generic_string() + "/zbd");
    EXPECT(first == mech3::content_base_digest());
    zutl::mounts().read = {install.generic_string(), patch.generic_string()};
    EXPECT(first != mech3::content_base_digest());
    zutl::mounts().read = {patch.generic_string(), install.generic_string()};
    put(install / "zbd/reader.zbd", "changed fallback reader bank");
    mech3::clear_content_base_digest_cache();
    auto changed = mech3::content_base_digest();
    EXPECT(first != changed);
    put(install / "zbd/c1/custom.zrd", "authored loose mission behavior");
    mech3::clear_content_base_digest_cache();
    EXPECT(changed != mech3::content_base_digest());
    const auto with_loose = mech3::content_base_digest();
    put(install / "support/c1size.gw", "changed native world pool sizing commands");
    mech3::clear_content_base_digest_cache();
    EXPECT(with_loose != mech3::content_base_digest());
    const auto with_source = mech3::content_base_digest();
    auto written = fs::last_write_time(install / "support/c1size.gw");
    fs::last_write_time(install / "support/c1size.gw", written + std::chrono::seconds(5));
    mech3::clear_content_base_digest_cache();
    EXPECT(with_source != mech3::content_base_digest());
    const auto with_time = mech3::content_base_digest();
    put(install / "zbd/rtexture.zbd", "different opaque bank bytes");
    mech3::clear_content_base_digest_cache();
    EXPECT(with_time != mech3::content_base_digest());
}

TEST(content_base_fingerprint_survives_relocation_and_rejects_missing_required_bank) {
    Mounts mounts;
    auto root = fs::path(MW3_TEST_TMP) / "content_base_relocated", first = root / "first",
         second = root / "second";
    for (const auto &directory : {first, second}) {
        put(directory / "zbd/reader.zbd", "authored fixture");
        put(directory / "zbd/c1/anim.zbd", "authored animation fixture");
    }
    zutl::mount(first.generic_string());
    const auto initial = mech3::content_base_digest();
    zutl::mounts().read = {second.generic_string()};
    EXPECT(initial == mech3::content_base_digest());
    zutl::mounts().read = {(root / "missing").generic_string()};
    bool rejected = false;
    try {
        mech3::content_base_digest();
    } catch (const cc::content::Error &error) {
        rejected = error.diagnostic.code == "base-content";
    }
    EXPECT(rejected);
}

TEST(content_managed_runtime_refuses_implicit_working_directory_inputs) {
    Mounts mounts;
    const auto root = fs::path(MW3_TEST_TMP) / "content_untracked_cwd";
    put(root / "untracked.gw", "authored untracked input");
    struct Directory {
        fs::path original = fs::current_path();
        ~Directory() { fs::current_path(original); }
    } directory;
    fs::current_path(root);
    EXPECT(!zutl::find("untracked.gw").empty());
    zutl::mounts().strict_reads = true;
    EXPECT(zutl::find("untracked.gw").empty());
    EXPECT(zutl::find_all("untracked.gw").empty() && zutl::list("*.gw").empty());
}

TEST(content_managed_native_sources_reject_disguised_cosmetics_and_use_explicit_claim_precedence) {
    using namespace cc::content;
    struct Registry {
        std::shared_ptr<const ResolvedContentSet> previous = mech3::active_content();
        ~Registry() { mech3::activate_content(previous); }
    } registry;
    Mounts mounts;
    auto root = fs::path(MW3_TEST_TMP) / "content_source_admission", install = root / "install",
         package_root = root / "package";
    put(install / "zbd/reader.zbd", "authored reader fixture");
    put(install / "support/physics.gw", "Quit\n");
    put(install / "support/physics.png", "Quit\n");
    put(install / "unknown/physics.gw", "Quit\n");
    zutl::mount(install.generic_string());
    auto content = std::make_shared<ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    mech3::activate_content(content);
    const auto saved_archive = zint::globals().use_archive;
    zint::globals().use_archive = 0;
    zint::Interp interpreter;
    zint::init(interpreter, "interp.zbd", ".;zbd");
    for (const auto *source : {"support/physics.png", "unknown/physics.gw"}) {
        bool rejected = false;
        try {
            zint::source(interpreter, source);
        } catch (const Error &error) {
            rejected = error.diagnostic.code == "content-source";
        }
        EXPECT(rejected);
    }
    for (const auto *world : {"support/physics.png", "unknown/geometry.zbd", "../outside.zbd"}) {
        bool rejected = false;
        try {
            zcls::read_zbd(world);
        } catch (const Error &error) {
            rejected = error.diagnostic.code == "content-world-bank";
        }
        EXPECT(rejected);
    }
    EXPECT(zint::source(interpreter, "support/physics.gw"));
    // The recovered mtime rule still selects the archived entry for an older
    // unclaimed installation source, even with a cosmetic/empty active profile.
    interpreter.archive.f = std::tmpfile();
    EXPECT(interpreter.archive.f != nullptr);
    if (interpreter.archive.f) {
        interpreter.archive_size = 512;
        interpreter.entry_count = 1;
        interpreter.dir.resize(1);
        std::strcpy(interpreter.dir[0].name, "support/physics.gw");
        interpreter.dir[0].timestamp = 0x7fffffff;
        interpreter.dir[0].offset = 140;
        EXPECT(zint::archive_find(interpreter, "support/physics.gw") != nullptr);
        Json manifest = {
            {"schema_version", 1},
            {"id", "source-overlay"},
            {"version", "1.0.0"},
            {"files", {{"support/physics.gw", {{"bytes", 5}, {"sha256", sha256("Quit\n")}}}}}};
        put(package_root / "support/physics.gw", "Quit\n");
        write_json_atomic(package_root / "pack.json", manifest);
        auto package = *try_load_legacy(package_root);
        content = std::make_shared<ResolvedContentSet>();
        content->definitions = mech3::stock_content();
        content->definitions.insert(content->definitions.end(), package.definitions.begin(),
                                    package.definitions.end());
        content->packages.push_back(package);
        resolve_legacy_claims(*content, Json::object());
        mech3::activate_content(content);
        mech3::mount_content_legacy(*content);
        EXPECT(mech3::content_managed_script_source("support/physics.gw"));
        EXPECT(zint::archive_find(interpreter, "support/physics.gw") == nullptr);
        std::fclose(interpreter.archive.f);
        interpreter.archive.f = nullptr;
    }
    zint::destroy(interpreter);
    zint::globals().use_archive = saved_archive;
}
