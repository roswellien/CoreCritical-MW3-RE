#include "check.h"
#include "content/content.h"
#include "gamez/zreader/zreader.h"
#include "gamez/ztwod/twod_host.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/briefing.h"
#include "mech3/content_base.h"
#include "mech3/content_campaign.h"
#include "mech3/content_registry.h"
#include "mech3/mech_config.h"
#include "mech3/mech_reader.h"
#include "mech3/pilot.h"

#include <memory>

TEST(content_native_lance_exhausts_added_chassis_without_looping) {
    using cc::content::ResolvedContentSet;
    struct Session {
        std::shared_ptr<const ResolvedContentSet> previous = mech3::active_content();
        ~Session() { mech3::activate_content(previous); }
    } session;
    auto content = std::make_shared<ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    auto chassis = *content->find("chassis", "stock:bushwacker");
    chassis.id = "authored:extra", chassis.owner = "authored", chassis.handle = 19;
    chassis.data["name"] = "authored_extra";
    chassis.data["label"] = "Authored Extra";
    chassis.data["base"] = "stock:bushwacker";
    content->definitions.push_back(std::move(chassis));
    mech3::activate_content(content);
    mech3::Store store;
    mech3::GameSlot slot(19);
    mech3::lance_slot(slot, store, false);
    EXPECT(slot.mech == -1 && slot.config == nullptr);
}

namespace {
struct StandaloneBriefingEngine final : mech3::Engine {
    int32_t fast_menu() override { return 1; }
    void set_fast_menu(int32_t) override {}
    const char *message(int32_t) override { return "Fixture"; }
    const char *version_number() override { return "test"; }
    void briefing_map_image(ztwod::Dialog *, zrdr::Node *) override {}
};

struct EmptyInitialStores final : mech3::PilotHooks {
    void store_fill(mech3::Store &, int32_t) override {}
    int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
};
} // namespace

TEST(content_standalone_briefing_keeps_selected_commander_and_unlimited_loadout) {
    using cc::content::Json;
    using cc::content::ResolvedContentSet;
    constexpr int32_t commander = 3; // Changed from a new ActionGame's default chassis zero.
    constexpr int32_t weapon = 11;   // Inner Sphere medium laser.

    ztwod::Host silent_host;
    EmptyInitialStores hooks;
    struct Restore {
        std::shared_ptr<const ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        ztwod::Host *host = &ztwod::host();
        mech3::Pilot *pilot = mech3::current_pilot();
        bool archives_created = zrdr::state().archives_created;
        mech3::MechDfn *fixture_dfn = nullptr;
        mech3::MechDfn *previous_dfn = nullptr;
        ~Restore() {
            // The fixture owns its synthetic loadout; do not save it on DFN teardown.
            if (fixture_dfn) {
                if (mech3::mech_dfn_slot(commander) == fixture_dfn)
                    mech3::mech_dfn_slot(commander) = previous_dfn;
                delete fixture_dfn->configs;
                fixture_dfn->configs = nullptr;
                delete fixture_dfn;
            }
            mech3::current_pilot() = pilot;
            mech3::activate_content(content);
            mech3::set_pilot_hooks(hooks);
            ztwod::set_host(host);
            zrdr::state().archives_created = archives_created;
            zutl::mounts() = mounts;
            mech3::clear_content_base_digest_cache();
        }
    } restore;

    zutl::mounts() = {};
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "standalone-briefing-fit";
    cc::content::fs::create_directories(root);
    zutl::set_write_dir(root.string());
    ztwod::set_host(&silent_host);
    mech3::set_pilot_hooks(&hooks);
    // Do not close or replace any preexisting archives; this fixture needs none.
    zrdr::state().archives_created = 0;

    auto content = std::make_shared<ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    content->definitions.push_back({"mission",
                                    "fixture:standalone",
                                    "fixture",
                                    "native",
                                    {{"world", "stock:c1"}, {"number", 1}},
                                    0});
    mech3::activate_content(content);

    auto *dfn = new mech3::MechDfn(commander, nullptr);
    restore.fixture_dfn = dfn;
    dfn->configs = new mech3::ConfigList(commander, 0);
    auto *canned = new mech3::Config(commander, "three-lasers");
    for (int32_t part = 0; part < 3; ++part) {
        auto *laser = canned->add_weapon(weapon, 2);
        laser->part = part;
    }
    dfn->configs->configs.push_back(canned);
    restore.previous_dfn = mech3::mech_dfn_slot(commander);
    mech3::mech_dfn_slot(commander) = dfn;

    mech3::Pilot pilot("StandaloneFit");
    mech3::current_pilot() = &pilot;
    auto &game = mech3::select_content_standalone(pilot, "fixture:standalone");
    // This is the accepted ActionScreen state. The regression below covers
    // the subsequent real briefing consumer, not the ActionScreen constructor.
    game.store.fill_all();
    game.slot.mech = commander;
    game.slot.set_config(canned);
    mech3::WeaponItem probe;
    probe.weapon = weapon;
    probe.tech = 2;
    const auto inventory_size = game.store.items.size();
    EXPECT(game.store.full && game.store.count_like(&probe) == 1);
    EXPECT(game.slot.config && game.slot.config->weapon_count() == 3);

    StandaloneBriefingEngine engine;
    EXPECT(engine.game_kind() == mech3::kCampaign); // Native mission routing remains unchanged.
    {
        mech3::BriefingScreen briefing(engine, "default", false);
        EXPECT(briefing.build_lab_store() == &game.store);
        briefing.set_loaded(true); // Calls the real lance_loadouts and refresh.
        briefing.lance_loadouts(); // A second visit must not consume unlimited stock.
        EXPECT(game.slot.mech == commander);
        EXPECT(game.slot.config && game.slot.config->mech == commander);
        EXPECT(game.slot.config && game.slot.config->weapon_count() == 3);
        EXPECT(game.store.full && game.store.items.size() == inventory_size);
        EXPECT(game.store.has_mech(commander) && game.store.has_mech(commander)->b10 == 1);
        EXPECT(game.store.count_like(&probe) == 1);
        EXPECT(game.store3c.items.empty());
        EXPECT(briefing.salvage_btn.flags & ztwod::kHidden);
        EXPECT(briefing.replay_btn.flags & ztwod::kHidden);
        EXPECT(briefing.launch_btn.enabled);
    }
    pilot.dirty = 0;
}
