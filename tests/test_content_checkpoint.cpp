#include "check.h"
#include "content/persistence.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/app_engine.h"
#include "mech3/content_campaign.h"
#include "mech3/content_checkpoint.h"
#include "mech3/content_registry.h"
#include "mech3/content_stock_campaign.h"
#include "mech3/content_world.h"
#include "mech3/pilot.h"

#include <fstream>
#include <functional>

namespace {
using namespace cc::content;
bool rejects(const std::function<void()> &action, const std::string &code = {}) {
    try {
        action();
        return false;
    } catch (const Error &error) {
        return code.empty() || error.diagnostic.code == code;
    }
}
struct Hooks : mech3::PilotHooks {
    int snapshots = 0, restores = 0;
    void store_fill(mech3::Store &, int32_t) override {}
    int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
    bool save_world_checked(const char *, int32_t) override {
        ++snapshots;
        return false;
    }
    bool load_world(const char *, int32_t) override {
        ++restores;
        return false;
    }
};
struct Scope {
    std::shared_ptr<const ResolvedContentSet> previous = mech3::active_content();
    zutl::Mounts mounts = zutl::mounts();
    mech3::PilotHooks *old_hooks = &mech3::pilot_hooks();
    mech3::Pilot *old_current = mech3::current_pilot();
    std::list<mech3::Pilot *> old_pilots = mech3::pilots();
    mech3::ActionSetup old_action = mech3::action_setup();
    fs::path root;
    Hooks hooks;
    std::shared_ptr<ResolvedContentSet> content;
    mech3::Platform platform{};
    std::unique_ptr<mech3::GameEngine> engine;
    std::unique_ptr<mech3::Pilot> pilot;
    explicit Scope(const char *name, bool select = true) {
        root = fs::path(MW3_TEST_TMP) / name;
        fs::create_directories(root);
        zutl::mounts() = {};
        zutl::set_write_dir(root.generic_string());
        mech3::pilots().clear();
        mech3::current_pilot() = nullptr;
        engine = std::make_unique<mech3::GameEngine>(platform, "mech3.exe -nofmv");
        engine->quiet = true;
        mech3::set_pilot_hooks(&hooks);
        content = std::make_shared<ResolvedContentSet>();
        content->definitions = mech3::stock_content();
        content->lock = {{"selections", Json::object()}};
        content->gameplay_digest = sha256("checkpoint-fixture");
        content->session_digest = sha256("checkpoint-mapping");
        mech3::activate_content(content);
        pilot = std::make_unique<mech3::Pilot>("Checkpoint");
        pilot->dirty = 0;
        if (select)
            mech3::current_pilot() = pilot.get();
    }
    ~Scope() {
        mech3::current_pilot() = nullptr;
        for (auto *loaded : mech3::pilots()) {
            loaded->dirty = 0;
            delete loaded;
        }
        mech3::pilots().clear();
        pilot->dirty = 0;
        pilot.reset();
        engine.reset();
        (void)mech3::consume_content_initial_launch();
        mech3::activate_content(previous);
        zutl::mounts() = mounts;
        mech3::set_pilot_hooks(old_hooks);
        mech3::pilots() = old_pilots;
        mech3::current_pilot() = old_current;
        mech3::action_setup() = old_action;
    }
    fs::path source(const char *filename = "migrated.ccsave") {
        mech3::CampaignGame game(pilot.get(), 1, 2);
        game.slot.mech = -1;
        game.c4 = 42;
        mech3::adopt_content_stock_campaign(game);
        mech3::refresh_content_campaign_semantics(game);
        const auto canonical =
            fs::u8path(zutl::write_path((std::string(game.path()) + ".ccsave").c_str()));
        const auto path = root / filename;
        fs::rename(canonical, path);
        return path;
    }
    fs::path canonical(const Json &state) {
        mech3::CampaignGame game(pilot.get(), 1, 2);
        game.content_run = state.at("graph").at("run").get<std::string>();
        return fs::u8path(zutl::write_path((std::string(game.path()) + ".ccsave").c_str()));
    }
};
} // namespace

TEST(content_checkpoint_import_indexes_a_validated_copy_and_refuses_existing_runs) {
    Scope scope("checkpoint_import");
    EXPECT(scope.pilot->save());
    const auto index = fs::u8path(zutl::write_path(scope.pilot->file_path().c_str()));
    const auto old_index = file_sha256(index);
    auto *previous_game = scope.pilot->current;
    const auto source = scope.source();
    const auto source_hash = file_sha256(source);
    const auto saved = load_bundle(source, *scope.content);
    const auto canonical = scope.canonical(saved.state);
    {
        std::ofstream collision(canonical);
        collision << "existing canonical file";
    }
    const auto collision_hash = file_sha256(canonical);
    EXPECT(rejects(
        [&] { mech3::configure_content_checkpoint(*scope.engine, source.generic_string()); },
        "checkpoint-exists"));
    EXPECT(scope.pilot->current == previous_game && file_sha256(index) == old_index &&
           file_sha256(canonical) == collision_hash);
    fs::remove(canonical);
    EXPECT(mech3::configure_content_checkpoint(*scope.engine, source.generic_string()));
    auto *imported = dynamic_cast<mech3::CampaignGame *>(scope.pilot->current);
    EXPECT(imported && imported->content_run == saved.state["graph"]["run"] && imported->c4 == 42);
    EXPECT(imported && imported->operation() == 1 && imported->mission_number() == 2);
    EXPECT(scope.engine->launch().keep_pilot && scope.engine->launch().index == 0 &&
           scope.engine->launch().number == 2);
    EXPECT(scope.pilot->campaigns.size() == 2 && fs::exists(canonical) &&
           file_sha256(source) == source_hash);
    const auto imported_index = file_sha256(index), imported_bundle = file_sha256(canonical);
    EXPECT(rejects(
        [&] { mech3::configure_content_checkpoint(*scope.engine, source.generic_string()); },
        "checkpoint-run-exists"));
    EXPECT(scope.pilot->current == imported && file_sha256(index) == imported_index &&
           file_sha256(canonical) == imported_bundle);
    // The ordinary native pilot loader now finds the imported run through its
    // durable index, without a special launch-only or foreign-world callback.
    auto *loaded = mech3::load_pilot(scope.pilot->name, false);
    bool indexed = false;
    if (loaded)
        for (const auto *entry : loaded->campaigns)
            if (const auto *game = dynamic_cast<const mech3::CampaignGame *>(entry))
                indexed =
                    indexed || (game->content_run == saved.state["graph"]["run"] && game->c4 == 42);
    EXPECT(indexed && scope.hooks.snapshots == 0 && scope.hooks.restores == 0);
}

TEST(content_checkpoint_rejects_foreign_semantics_and_world_before_creating_a_pilot) {
    Scope scope("checkpoint_preflight", false);
    const auto source = scope.source();
    auto saved = load_bundle(source, *scope.content);
    const auto core = scope.root / "pilots/CoreCritical.mw3";
    auto invalid = saved.state;
    invalid["native"]["player"]["chassis"] = "missing:chassis";
    save_bundle(scope.root / "missing.ccsave", invalid, {}, *scope.content);
    EXPECT(rejects([&] {
        mech3::configure_content_checkpoint(*scope.engine,
                                            (scope.root / "missing.ccsave").generic_string());
    }));
    invalid = saved.state;
    invalid["base_digest"] = sha256("different base");
    save_bundle(scope.root / "base.ccsave", invalid, {}, *scope.content);
    EXPECT(rejects(
        [&] {
            mech3::configure_content_checkpoint(*scope.engine,
                                                (scope.root / "base.ccsave").generic_string());
        },
        "save-base"));
    invalid = saved.state;
    invalid["world_snapshot"] = true;
    invalid["snapshot_mission"] = "stock:c1m1";
    invalid["world_digest"] = sha256("foreign geometry");
    save_bundle(scope.root / "world.ccsave", invalid, {1, 2, 3}, *scope.content);
    EXPECT(rejects(
        [&] {
            mech3::configure_content_checkpoint(*scope.engine,
                                                (scope.root / "world.ccsave").generic_string());
        },
        "save-world"));
    EXPECT(!fs::exists(core) && mech3::pilots().empty() && mech3::current_pilot() == nullptr);
    EXPECT(scope.hooks.snapshots == 0 && scope.hooks.restores == 0);
}

TEST(content_checkpoint_index_failure_removes_only_its_new_bundle_and_keeps_selection) {
    Scope scope("checkpoint_index_failure");
    EXPECT(scope.pilot->save());
    const auto index = fs::u8path(zutl::write_path(scope.pilot->file_path().c_str()));
    const auto original_index = file_sha256(index);
    const auto source = scope.source();
    const auto original_source = file_sha256(source);
    const auto canonical = scope.canonical(load_bundle(source, *scope.content).state);
    auto *previous_game = scope.pilot->current;
    const auto count = scope.pilot->campaigns.size();
    // Force a real native index serialization failure after the imported bundle
    // has been staged, through an invalid identity in another pilot game.
    scope.pilot->lab->slot.mech = INT32_MAX;
    EXPECT(rejects(
        [&] { mech3::configure_content_checkpoint(*scope.engine, source.generic_string()); }));
    EXPECT(file_sha256(index) == original_index && file_sha256(source) == original_source);
    EXPECT(!fs::exists(canonical) && scope.pilot->current == previous_game &&
           scope.pilot->campaigns.size() == count);
    EXPECT(scope.hooks.snapshots == 0 && scope.hooks.restores == 0);
    scope.pilot->lab->slot.mech = 3;
}
