#include "check.h"
#include "content/hash.h"
#include "content/persistence.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/content_base.h"
#include "mech3/content_campaign.h"
#include "mech3/content_campaign_items.h"
#include "mech3/content_network.h"
#include "mech3/content_registry.h"
#include "mech3/content_state.h"
#include "mech3/content_stock_campaign.h"
#include "mech3/content_world.h"
#include "mech3/mission.h"
#include "mech3/pilot.h"

#include <cstring>
#include <fstream>

namespace {
using cc::content::Json;
cc::content::Definition branching_campaign() {
    return {
        "campaign",
        "demo:campaign",
        "demo",
        "graph",
        {{"start", "recon"},
         {"variables", {{"supplies", 0}}},
         {"nodes",
          Json::array(
              {{{"id", "recon"},
                {"mission", "demo:recon"},
                {"on_success",
                 Json::array(
                     {{{"op", "add"}, {"variable", "supplies"}, {"value", 2}},
                      {{"op", "give"}, {"kind", "weapon"}, {"id", "demo:laser"}, {"count", 1}}})},
                {"routes", Json::array({{{"when",
                                          {{"all", Json::array({{{"outcome", "success"}},
                                                                {{"objective", "intel"},
                                                                 {"equals", true}}})}}},
                                         {"next", "strike"}},
                                        {{"when", {{"outcome", "success"}}}, {"next", nullptr}},
                                        {{"when", true}, {"next", "recon"}}})}},
               {{"id", "strike"},
                {"mission", "demo:strike"},
                {"routes", Json::array({{{"when", true}, {"next", nullptr}}})}}})}},
        0};
}
} // namespace

TEST(content_campaign_branches_on_real_named_results_and_applies_rewards_once) {
    const auto campaign = branching_campaign();
    const auto initial = mech3::begin_content_campaign(campaign);
    const Json result = {{"run", initial["run"]},
                         {"attempt", 1},
                         {"outcome", "success"},
                         {"objectives", {{"intel", true}}}};
    const auto next = mech3::reduce_content_campaign(campaign, initial, result);
    EXPECT(next["node"] == "strike");
    EXPECT(next["variables"]["supplies"] == 2.0);
    EXPECT(next["rewards"].size() == 1);
    EXPECT(next["attempt"] == 2);
    EXPECT(mech3::reduce_content_campaign(campaign, next, result) == next);
    auto unsupported = next;
    unsupported["schema"] = uint64_t(4294967297ULL);
    bool rejected = false;
    try {
        mech3::reduce_content_campaign(
            campaign, unsupported,
            {{"run", initial["run"]}, {"attempt", 2}, {"outcome", "success"}});
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
    const auto end = mech3::reduce_content_campaign(
        campaign, next, {{"run", next["run"]}, {"attempt", 2}, {"outcome", "success"}});
    EXPECT(end["terminal"] == true);
}

TEST(content_campaign_failure_retries_without_success_rewards) {
    const auto campaign = branching_campaign();
    const auto initial = mech3::begin_content_campaign(campaign);
    const auto state = mech3::reduce_content_campaign(
        campaign, initial, {{"run", initial["run"]}, {"attempt", 1}, {"outcome", "failure"}});
    EXPECT(state["node"] == "recon");
    EXPECT(state["terminal"] == false);
    EXPECT(state["rewards"].empty());
    EXPECT(state["variables"]["supplies"] == 0);
    bool rejected = false;
    try {
        mech3::reduce_content_campaign(
            campaign, state, {{"run", state["run"]}, {"attempt", 100}, {"outcome", "success"}});
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
}

TEST(content_campaign_run_identity_survives_saves_and_rejects_another_runs_result) {
    const auto campaign = branching_campaign();
    const auto first = mech3::begin_content_campaign(campaign);
    const auto second = mech3::begin_content_campaign(campaign);
    EXPECT(first["run"] != second["run"]);
    EXPECT(cc::content::valid_campaign_run_id(first["run"].get<std::string>()));
    EXPECT(cc::content::valid_campaign_run_id(second["run"].get<std::string>()));
    const Json first_result = {{"run", first["run"]},
                               {"attempt", 1},
                               {"outcome", "success"},
                               {"objectives", {{"intel", true}}}};
    const Json second_result = {{"run", second["run"]},
                                {"attempt", 1},
                                {"outcome", "success"},
                                {"objectives", {{"intel", true}}}};
    const auto accepted = mech3::reduce_content_campaign(campaign, second, second_result);
    EXPECT(accepted["run"] == second["run"] && accepted["rewards"].size() == 1);
    EXPECT(mech3::reduce_content_campaign(campaign, accepted, second_result) == accepted);
    for (const auto &state : {second, accepted}) {
        bool rejected = false;
        try {
            mech3::reduce_content_campaign(campaign, state, first_result);
        } catch (const cc::content::Error &error) {
            rejected = error.diagnostic.code == "campaign-run";
        }
        EXPECT(rejected); // Run identity is checked before the already-applied attempt ledger.
    }
    cc::content::ResolvedContentSet content;
    content.definitions = {campaign,
                           {"mission", "demo:recon", "demo", "native", Json::object(), 0},
                           {"mission", "demo:strike", "demo", "native", Json::object(), 1}};
    const Json state = {{"campaign", campaign.id}, {"mission", "demo:strike"}, {"graph", accepted}};
    cc::content::validate_saved_campaign_graph(state, content);
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "authored-run-save";
    cc::content::fs::create_directories(root);
    cc::content::save_bundle(root / "run.ccsave", state, {}, content);
    const auto restored = cc::content::load_bundle(root / "run.ccsave", content);
    EXPECT(restored.state["graph"]["run"] == second["run"]);
    for (const Json &run : {Json(), Json(""), Json("invalid"), Json(1)}) {
        auto invalid = state;
        invalid["graph"]["run"] = run;
        bool rejected = false;
        try {
            cc::content::validate_saved_campaign_graph(invalid, content);
        } catch (const cc::content::Error &) {
            rejected = true;
        }
        EXPECT(rejected);
    }
}

TEST(content_campaign_live_result_stays_bound_to_the_launched_run) {
    struct Hooks final : mech3::PilotHooks {
        int snapshots = 0;
        void store_fill(mech3::Store &, int32_t) override {}
        int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
        bool save_world_checked(const char *, int32_t) override {
            ++snapshots;
            return false;
        }
    } hooks;
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        uint8_t ended = mech3::missions().ended;
        ~Restore() {
            mech3::activate_content(content);
            zutl::mounts() = mounts;
            mech3::set_pilot_hooks(hooks);
            mech3::missions().ended = ended;
            mech3::clear_content_mission_selection();
            mech3::clear_content_base_digest_cache();
        }
    } restore;
    zutl::mounts() = {};
    mech3::set_pilot_hooks(&hooks);
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    const cc::content::Definition campaign = {
        "campaign",
        "demo:run",
        "demo",
        "graph",
        {{"start", "first"},
         {"nodes",
          Json::array({{{"id", "first"},
                        {"mission", "stock:c1m1"},
                        {"routes", Json::array({{{"when", true}, {"next", nullptr}}})}}})}},
        1};
    content->definitions.push_back(campaign);
    mech3::activate_content(content);
    mech3::CampaignGame game(nullptr, 1000, 1);
    game.content_campaign = campaign.id;
    game.content_mission = "stock:c1m1";
    game.content_state = mech3::begin_content_campaign(campaign);
    int32_t world = -1, mission = -1, operation = -1;
    EXPECT(mech3::content_campaign_launch(game, world, mission, operation));
    const auto launched_run = game.content_state["run"];
    const auto selected_mission = mech3::content_selected_mission();
    mech3::CampaignGame staged(nullptr, 1000, 2);
    staged.content_campaign = campaign.id;
    staged.content_mission = "stock:c1m1";
    staged.content_state = mech3::begin_content_campaign(campaign);
    EXPECT(!mech3::preflight_content_campaign_launch(staged).empty());
    EXPECT(mech3::content_selected_mission() == selected_mission);
    game.content_state = staged.content_state;
    const auto switched = game.content_state;
    EXPECT(switched["run"] != launched_run && switched["attempt"] == 1);
    mech3::missions().ended = 1;
    EXPECT(mech3::advance_content_campaign(game, true, false) == &game);
    EXPECT(game.content_state == switched && hooks.snapshots == 0);
}

TEST(content_campaign_rejects_missing_missions_and_dangling_graph_edges) {
    const auto campaign = branching_campaign();
    cc::content::ResolvedContentSet content;
    content.definitions.push_back(campaign);
    bool rejected = false;
    try {
        mech3::validate_campaign_content(content);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
    content.definitions.push_back({"mission",
                                   "demo:recon",
                                   "demo",
                                   "native",
                                   {{"objective_ids", {{"intel", {"secondary", 0}}}}},
                                   0});
    content.definitions.push_back({"mission", "demo:strike", "demo", "native", Json::object(), 1});
    content.definitions.push_back({"weapon", "demo:laser", "demo", "beam", {{"tech", 2}}, 0});
    mech3::validate_campaign_content(content);
    content.definitions[0].data["nodes"][0]["routes"][0]["next"] = "missing";
    rejected = false;
    try {
        mech3::validate_campaign_content(content);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
}

TEST(content_campaign_snapshot_rejects_failed_writers_without_reusing_old_world) {
    struct Writer final : mech3::PilotHooks {
        bool success = false;
        bool malformed = false;
        std::string staged;
        bool save_world_checked(const char *path, int32_t kind) override {
            staged = path;
            if (!success)
                return false;
            if (malformed) {
                std::ofstream out(zutl::write_path(path), std::ios::binary);
                out << "partial";
                return true;
            }
            zutl_zar::Archive archive;
            zutl_zar::init(archive);
            if (!zutl_zar::create(archive, path))
                return false;
            const int32_t header[2] = {8, kind};
            const int32_t value = 123;
            zutl_zar::add(archive, "zSaveHeader", header, sizeof header);
            zutl_zar::add(archive, "fixture/state", &value, sizeof value);
            zutl_zar::close(archive);
            zutl_zar::destroy(archive);
            return true;
        }
    } writer;
    auto *previous = &mech3::pilot_hooks();
    const auto mounts = zutl::mounts();
    struct Restore {
        mech3::PilotHooks *hooks;
        zutl::Mounts mounts;
        ~Restore() {
            mech3::set_pilot_hooks(hooks);
            zutl::mounts() = mounts;
        }
    } restore{previous, mounts};
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "snapshot-staging";
    cc::content::fs::create_directories(root);
    zutl::mounts().confined = false;
    zutl::set_write_dir(root.string());
    mech3::set_pilot_hooks(&writer);
    const auto old = root / "checkpoint.mw3";
    {
        std::ofstream out(old, std::ios::binary);
        out << "older native snapshot";
    }
    auto rejected = [&]() {
        try {
            mech3::capture_content_world_snapshot("checkpoint.mw3");
        } catch (const cc::content::Error &) {
            return true;
        }
        return false;
    };
    EXPECT(rejected());
    EXPECT(writer.staged != "checkpoint.mw3");
    EXPECT(!cc::content::fs::path(writer.staged).is_absolute());
    EXPECT(cc::content::fs::u8path(zutl::write_path(writer.staged.c_str())).parent_path() == root);
    EXPECT(!cc::content::fs::exists(zutl::write_path(writer.staged.c_str())));
    writer.success = true;
    writer.malformed = true;
    EXPECT(rejected());
    writer.malformed = false;
    const auto fresh = mech3::capture_content_world_snapshot("checkpoint.mw3");
    EXPECT(fresh.size() > 8);
    EXPECT(!cc::content::fs::exists(zutl::write_path(writer.staged.c_str())));
    std::ifstream in(old, std::ios::binary);
    const std::string original(std::istreambuf_iterator<char>(in), {});
    EXPECT(original == "older native snapshot");
}

TEST(content_campaign_rewards_require_available_technology_ammo_and_bounded_counts) {
    const auto previous = mech3::active_content();
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> previous;
        ~Restore() { mech3::activate_content(previous); }
    } restore{previous};
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    mech3::activate_content(content);
    const auto campaign = branching_campaign();
    Json action = {{"op", "give"}, {"kind", "weapon"}, {"id", "stock:ac5"}, {"count", 2}};
    mech3::campaign_items::validate(action, campaign, *content);
    mech3::CampaignGame game(nullptr, 1000, 1);
    mech3::campaign_items::apply(game, Json::array({action}));
    mech3::WeaponItem probe;
    probe.weapon = content->find("weapon", "stock:ac5")->handle;
    probe.tech = 2;
    EXPECT(game.store.count_like(&probe) == 2); // IS-only default must not become Clan.
    probe.tech = 1;
    EXPECT(game.store.count_like(&probe) == 0);
    auto rejected = [&](const Json &value) {
        try {
            mech3::campaign_items::validate(value, campaign, *content);
        } catch (const cc::content::Error &) {
            return true;
        }
        return false;
    };
    for (const Json &tech : {Json(0), Json(1), Json(3), Json(999), Json(2.0), Json(true),
                             Json(uint64_t(4294967298ULL))}) {
        action["tech"] = tech;
        EXPECT(rejected(action));
    }
    action["tech"] = 2;
    for (const Json &count :
         {Json(0), Json(-1), Json(1.0), Json(uint64_t(18446744073709551615ULL))}) {
        action["count"] = count;
        EXPECT(rejected(action));
    }
    action.erase("tech");
    action["count"] = 1;
    action["kind"] = "ammo";
    action["id"] = "stock:laser_medium";
    EXPECT(rejected(action));
    action["id"] = "stock:ac5";
    EXPECT(!rejected(action));
    action["kind"] = "equipment";
    action["id"] = "stock:bap";
    action["tech"] = 1;
    EXPECT(rejected(action));
    action.erase("tech");
    mech3::campaign_items::apply(game, Json::array({action}));
    mech3::EquipmentItem equipment;
    equipment.equipment = content->find("equipment", "stock:bap")->handle;
    equipment.tech = 2;
    EXPECT(game.store.count_like(&equipment) == 1);
}

TEST(content_campaign_ordinary_save_updates_semantics_without_recapturing_world) {
    struct Hooks final : mech3::PilotHooks {
        int snapshots = 0;
        void store_fill(mech3::Store &, int32_t) override {}
        int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
        bool save_world_checked(const char *, int32_t) override {
            ++snapshots;
            return false;
        }
    } hooks;
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        ~Restore() {
            mech3::activate_content(content);
            zutl::mounts() = mounts;
            mech3::set_pilot_hooks(hooks);
            mech3::clear_content_base_digest_cache();
        }
    } restore;
    zutl::mounts() = {};
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "campaign-semantic-save";
    cc::content::fs::create_directories(root);
    zutl::set_write_dir(root.string());
    mech3::set_pilot_hooks(&hooks);
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    mech3::activate_content(content);
    mech3::Pilot pilot("SemanticSave");
    pilot.dirty = 0;
    pilot.file = std::tmpfile();
    EXPECT(pilot.file != nullptr);
    if (!pilot.file)
        return;
    mech3::CampaignGame game(&pilot, 1000, 1);
    game.content_mission = "stock:c1m1";
    game.slot.mech = -1;
    mech3::bind_content_campaign_run(game);
    const auto path = zutl::write_path((std::string(game.path()) + ".ccsave").c_str());
    EXPECT(!mech3::restore_content_campaign(game)); // New campaign has no checkpoint yet.
    game.content_checkpoint_expected = true;
    bool missing_rejected = false;
    try {
        mech3::restore_content_campaign(game);
    } catch (const cc::content::Error &error) {
        missing_rejected = error.diagnostic.code == "save-bundle";
    }
    EXPECT(missing_rejected);
    game.content_checkpoint_expected = false;
    game.c4 = 7;
    game.save();
    const auto initial = cc::content::load_bundle(path, *content);
    EXPECT(initial.world.empty() && initial.state["world_snapshot"] == false);
    mech3::CampaignGame initial_loaded(&pilot, 1000, 1);
    initial_loaded.content_run = game.content_run;
    mech3::read_content_campaign(initial_loaded);
    EXPECT(initial_loaded.c4 == 7 && initial_loaded.content_checkpoint_expected);
    EXPECT(!mech3::restore_content_campaign(initial_loaded));
    EXPECT(hooks.snapshots == 0);
    const std::vector<uint8_t> world = {1, 2, 3, 4, 5};
    Json state = {{"campaign", ""},
                  {"mission", game.content_mission},
                  {"graph", game.content_state},
                  {"native", Json::object()},
                  {"world_digest", "accepted-world"},
                  {"snapshot_mission", "stock:c1m1"},
                  {"persistence_group", "fixture:group"},
                  {"base_digest", mech3::content_base_digest()},
                  {"simulation_options", mech3::content_simulation_options()}};
    cc::content::save_bundle(path, state, world, *content);
    game.store.add_equipment(8, 2, 3);
    game.c4 = 42;
    game.save();
    EXPECT(hooks.snapshots == 0 && game.content_checkpoint_expected);
    const auto saved = cc::content::load_bundle(path, *content);
    EXPECT(saved.world == world && saved.state["world_digest"] == "accepted-world");
    EXPECT(saved.state["snapshot_mission"] == "stock:c1m1");
    EXPECT(saved.state["persistence_group"] == "fixture:group");
    mech3::CampaignGame loaded(&pilot, 1000, 1);
    loaded.content_run = game.content_run;
    mech3::read_content_campaign(loaded);
    mech3::EquipmentItem probe;
    probe.equipment = 8;
    probe.tech = 2;
    EXPECT(loaded.store.count_like(&probe) == 3 && loaded.c4 == 42);
    state = saved.state;
    state["native"]["player"]["chassis"] = "missing:chassis";
    state["graph"] = {{"run", game.content_run}, {"untrusted", "graph"}};
    cc::content::save_bundle(path, state, world, *content);
    const auto accepted_graph = loaded.content_state;
    bool identity_rejected = false;
    try {
        mech3::read_content_campaign(loaded);
    } catch (const cc::content::Error &) {
        identity_rejected = true;
    }
    EXPECT(identity_rejected && loaded.content_state == accepted_graph);
    EXPECT(loaded.store.count_like(&probe) == 3 && loaded.c4 == 42);
    state = saved.state;
    state["simulation_options"] = mech3::content_simulation_options() ^ 1u;
    cc::content::save_bundle(path, state, world, *content);
    const auto before = cc::content::file_sha256(path);
    bool rejected = false;
    try {
        mech3::refresh_content_campaign_semantics(game);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected && before == cc::content::file_sha256(path));
    EXPECT(hooks.snapshots == 0);
}

TEST(content_campaign_managed_pilot_index_preserves_previous_file_on_writer_failure) {
    struct Hooks final : mech3::PilotHooks {
        void store_fill(mech3::Store &, int32_t) override {}
        int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
    } hooks;
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        ~Restore() {
            mech3::activate_content(content);
            zutl::mounts() = mounts;
            mech3::set_pilot_hooks(hooks);
            mech3::clear_content_base_digest_cache();
        }
    } restore;
    zutl::mounts() = {};
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "pilot-atomic-save";
    cc::content::fs::create_directories(root);
    zutl::set_write_dir(root.string());
    mech3::set_pilot_hooks(&hooks);
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    mech3::activate_content(content);
    mech3::Pilot pilot("AtomicSave");
    pilot.dirty = 0;
    EXPECT(pilot.save());
    const auto path = cc::content::fs::u8path(zutl::write_path(pilot.file_path().c_str()));
    const auto before = cc::content::file_sha256(path);
    pilot.training->slot.config = new mech3::Config(3, "Interrupted");
    pilot.training->slot.config->mech = -1;
    pilot.dirty = 1;
    bool rejected = false;
    try {
        pilot.save();
    } catch (const cc::content::Error &error) {
        rejected = error.diagnostic.code == "save-identity";
    }
    EXPECT(rejected && before == cc::content::file_sha256(path));
    EXPECT(pilot.dirty == 1 && pilot.file == nullptr);
    for (const auto &entry : cc::content::fs::directory_iterator(path.parent_path()))
        EXPECT(entry.path().filename().generic_string().rfind(".pilot-", 0) != 0);
    // Managed reads preserve bad headers, including their failed-load destructor.
    const auto corrupt = path.parent_path() / "Corrupt.mw3";
    {
        std::ofstream out(corrupt, std::ios::binary);
        out << "incompatible pilot";
    }
    const auto corrupt_hash = cc::content::file_sha256(corrupt);
    EXPECT(mech3::load_pilot("Corrupt", false) == nullptr);
    EXPECT(corrupt_hash == cc::content::file_sha256(corrupt));
    const auto truncated = path.parent_path() / "Truncated.mw3";
    {
        std::ofstream out(truncated, std::ios::binary);
        const int32_t header[] = {pilot.magic, pilot.version, pilot.x0c};
        out.write(reinterpret_cast<const char *>(header), sizeof header);
    }
    const auto truncated_hash = cc::content::file_sha256(truncated);
    EXPECT(mech3::load_pilot("Truncated", false) == nullptr);
    EXPECT(truncated_hash == cc::content::file_sha256(truncated));
    pilot.dirty = 0;
}

TEST(content_campaign_managed_lab_load_rejects_complete_unknown_identity_without_publishing) {
    struct Hooks final : mech3::PilotHooks {
        void store_fill(mech3::Store &, int32_t) override {}
        int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
    } hooks;
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        mech3::ActionSetup action = mech3::action_setup();
        ~Restore() {
            mech3::activate_content(content);
            zutl::mounts() = mounts;
            mech3::set_pilot_hooks(hooks);
            mech3::action_setup() = action;
            mech3::clear_content_base_digest_cache();
        }
    } restore;
    zutl::mounts() = {};
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "pilot-lab-identity";
    cc::content::fs::create_directories(root);
    zutl::set_write_dir(root.string());
    mech3::set_pilot_hooks(&hooks);
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    mech3::activate_content(content);
    // A complete recognized loadout must propagate semantic failures to its caller.
    mech3::Config target(3, "Previous loadout");
    auto invalid_config = mech3::config_state(target);
    invalid_config["chassis"] = "missing:chassis";
    const auto config_text = invalid_config.dump();
    const auto config_length = static_cast<uint32_t>(config_text.size());
    auto *config_file = std::tmpfile();
    EXPECT(config_file != nullptr);
    if (!config_file)
        return;
    std::fwrite("CCLOAD1\n", 1, 8, config_file);
    std::fwrite(&config_length, 4, 1, config_file);
    std::fwrite(config_text.data(), 1, config_text.size(), config_file);
    std::rewind(config_file);
    bool config_rejected = false;
    try {
        target.load(config_file);
    } catch (const cc::content::Error &) {
        config_rejected = true;
    }
    std::fclose(config_file);
    EXPECT(config_rejected && target.mech == 3 && std::string(target.name) == "Previous loadout");
    mech3::Pilot fixture("LabIdentity");
    fixture.dirty = 0;
    EXPECT(fixture.save());
    auto *loaded = mech3::load_pilot("LabIdentity", false);
    EXPECT(loaded != nullptr);
    if (!loaded)
        return;
    struct RemoveLoaded {
        mech3::Pilot *pilot;
        mech3::Pilot *previous = mech3::current_pilot();
        ~RemoveLoaded() {
            mech3::current_pilot() = previous;
            mech3::pilots().remove(pilot);
            pilot->dirty = 0;
            delete pilot;
        }
    } cleanup{loaded};
    loaded->lab->slot.mech = 8;
    mech3::current_pilot() = loaded;
    fixture.lab->slot.config = new mech3::Config(3, "Unknown typed ID");
    mech3::action_setup().g874 = 123;
    EXPECT(fixture.save());
    mech3::action_setup().g874 = 77;
    const auto path = cc::content::fs::u8path(zutl::write_path(fixture.file_path().c_str()));
    std::ifstream input(path, std::ios::binary);
    std::string bytes(std::istreambuf_iterator<char>(input), {});
    input.close();
    const auto slot = bytes.rfind("CCSLOTV1");
    EXPECT(slot != std::string::npos);
    if (slot == std::string::npos)
        return;
    uint32_t old_length = 0;
    std::memcpy(&old_length, bytes.data() + slot + 8, 4);
    auto state = cc::content::parse_json(bytes.substr(slot + 12, old_length));
    state["config"]["chassis"] = "missing:chassis";
    const auto replacement = state.dump();
    const auto length = static_cast<uint32_t>(replacement.size());
    bytes.replace(slot + 12, old_length, replacement);
    std::memcpy(bytes.data() + slot + 8, &length, 4);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), bytes.size());
    }
    const auto before = cc::content::file_sha256(path);
    EXPECT(mech3::load_pilot("LabIdentity", true) == nullptr);
    EXPECT(mech3::find_pilot("LabIdentity") == loaded && mech3::current_pilot() == loaded);
    EXPECT(loaded->lab->slot.mech == 8 && loaded->lab->slot.config == nullptr);
    EXPECT(mech3::action_setup().g874 == 77);
    EXPECT(before == cc::content::file_sha256(path));
    mech3::pilots().remove(loaded);
    EXPECT(mech3::load_pilot("LabIdentity", false) == nullptr);
    EXPECT(mech3::find_pilot("LabIdentity") == nullptr && mech3::current_pilot() == loaded);
    EXPECT(before == cc::content::file_sha256(path));
}

TEST(content_campaign_run_storage_keeps_same_mission_runs_and_rejects_retargeted_bundles) {
    struct Hooks final : mech3::PilotHooks {
        void store_fill(mech3::Store &, int32_t) override {}
        int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
    } hooks;
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        ~Restore() {
            mech3::activate_content(content);
            zutl::mounts() = mounts;
            mech3::set_pilot_hooks(hooks);
            mech3::clear_content_base_digest_cache();
        }
    } restore;
    zutl::mounts() = {};
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "campaign-run-storage";
    cc::content::fs::create_directories(root);
    zutl::set_write_dir(root.string());
    mech3::set_pilot_hooks(&hooks);
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    mech3::activate_content(content);
    mech3::Pilot pilot("RunStorage");
    pilot.dirty = 0;
    auto &first = *static_cast<mech3::CampaignGame *>(pilot.campaigns.front());
    first.slot.mech = -1;
    first.c4 = 11;
    EXPECT(mech3::adopt_content_stock_campaign(first));
    auto second = std::make_unique<mech3::CampaignGame>(&pilot, 1, 1);
    second->slot.mech = -1;
    second->c4 = 22;
    EXPECT(mech3::adopt_content_stock_campaign(*second));
    const auto first_run = first.content_run, second_run = second->content_run;
    const std::string first_path = first.path(), second_path = second->path();
    EXPECT(first_run != second_run && first_path != second_path);
    EXPECT(first_path.find(first_run) != std::string::npos);
    EXPECT(second_path.find(second_run) != std::string::npos);
    pilot.campaigns.push_back(second.release());
    EXPECT(pilot.save());
    const auto index = cc::content::fs::u8path(zutl::write_path(pilot.file_path().c_str()));
    std::ifstream input(index, std::ios::binary);
    const std::string index_bytes(std::istreambuf_iterator<char>(input), {});
    input.close();
    const auto marker = index_bytes.find(std::string("CCRUN01\n", 8) + first_run);
    EXPECT(marker != std::string::npos);
    auto *loaded = mech3::load_pilot("RunStorage", false);
    EXPECT(loaded != nullptr);
    if (!loaded)
        return;
    struct RemoveLoaded {
        mech3::Pilot *pilot;
        ~RemoveLoaded() {
            mech3::pilots().remove(pilot);
            pilot->dirty = 0;
            delete pilot;
        }
    } cleanup{loaded};
    int found = 0;
    for (const auto *entry : loaded->campaigns) {
        const auto *game = static_cast<const mech3::CampaignGame *>(entry);
        if (game->content_run == first_run) {
            EXPECT(game->c4 == 11);
            ++found;
        }
        if (game->content_run == second_run) {
            EXPECT(game->c4 == 22);
            ++found;
        }
    }
    EXPECT(found == 2);
    const auto bundle_path =
        cc::content::fs::u8path(zutl::write_path((first_path + ".ccsave").c_str()));
    const auto saved = cc::content::load_bundle(bundle_path, *content);
    auto retargeted = saved.state;
    retargeted["graph"]["run"] = second_run;
    cc::content::save_bundle(bundle_path, retargeted, saved.world, *content);
    const auto index_hash = cc::content::file_sha256(index);
    const auto retargeted_hash = cc::content::file_sha256(bundle_path);
    EXPECT(mech3::load_pilot("RunStorage", true) == nullptr);
    EXPECT(mech3::find_pilot("RunStorage") == loaded);
    EXPECT(index_hash == cc::content::file_sha256(index));
    EXPECT(retargeted_hash == cc::content::file_sha256(bundle_path));
    cc::content::save_bundle(bundle_path, saved.state, saved.world, *content);
    if (marker == std::string::npos)
        return;
    std::vector<std::string> invalid_indices = {index_bytes, index_bytes,
                                                index_bytes.substr(0, marker)};
    invalid_indices[0][marker] = 'X';
    invalid_indices[1][marker + 8] = 'z';
    for (const auto &bytes : invalid_indices) {
        {
            std::ofstream output(index, std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), bytes.size());
        }
        const auto invalid_hash = cc::content::file_sha256(index);
        EXPECT(mech3::load_pilot("RunStorage", true) == nullptr);
        EXPECT(mech3::find_pilot("RunStorage") == loaded);
        EXPECT(invalid_hash == cc::content::file_sha256(index));
    }
    {
        std::ofstream output(index, std::ios::binary | std::ios::trunc);
        output.write(index_bytes.data(), index_bytes.size());
    }
}

TEST(content_campaign_copied_pilot_index_rejects_another_profile_before_state_changes) {
    struct Hooks final : mech3::PilotHooks {
        void store_fill(mech3::Store &, int32_t) override {}
        int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
    } hooks;
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        mech3::ActionSetup action = mech3::action_setup();
        ~Restore() {
            mech3::activate_content(content);
            zutl::mounts() = mounts;
            mech3::set_pilot_hooks(hooks);
            mech3::action_setup() = action;
            mech3::clear_content_base_digest_cache();
        }
    } restore;
    zutl::mounts() = {};
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "pilot-intrinsic-identity";
    cc::content::fs::create_directories(root / "source");
    cc::content::fs::create_directories(root / "target" / "pilots");
    zutl::set_write_dir((root / "source").string());
    mech3::set_pilot_hooks(&hooks);
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    content->lock = {{"generation", "source"}};
    content->gameplay_digest = cc::content::sha256("gameplay");
    content->session_digest = cc::content::sha256("handles");
    mech3::activate_content(content);
    mech3::Pilot fixture("CopiedIndex");
    fixture.dirty = 0;
    for (mech3::Game *game : std::initializer_list<mech3::Game *>{fixture.training, fixture.multi,
                                                                  fixture.action_game, fixture.lab})
        game->slot.mech = -1;
    EXPECT(fixture.save());
    auto *loaded = mech3::load_pilot("CopiedIndex", false);
    EXPECT(loaded != nullptr);
    if (!loaded)
        return;
    struct RemoveLoaded {
        mech3::Pilot *pilot;
        mech3::Pilot *previous = mech3::current_pilot();
        ~RemoveLoaded() {
            mech3::current_pilot() = previous;
            mech3::pilots().remove(pilot);
            pilot->dirty = 0;
            delete pilot;
        }
    } cleanup{loaded};
    const auto copied = root / "target" / "pilots" / "CopiedIndex.mw3";
    cc::content::fs::copy_file(root / "source" / "pilots" / "CopiedIndex.mw3", copied);
    const auto before = cc::content::file_sha256(copied);
    zutl::set_write_dir((root / "target").string());
    auto other = std::make_shared<cc::content::ResolvedContentSet>(*content);
    other->lock["generation"] = "target";
    mech3::activate_content(other);
    mech3::current_pilot() = loaded;
    loaded->lab->slot.mech = 8;
    mech3::action_setup().g874 = 77;
    EXPECT(mech3::load_pilot("CopiedIndex", true) == nullptr);
    EXPECT(mech3::find_pilot("CopiedIndex") == loaded && mech3::current_pilot() == loaded);
    EXPECT(loaded->lab->slot.mech == 8 && mech3::action_setup().g874 == 77);
    EXPECT(before == cc::content::file_sha256(copied));
}

TEST(content_campaign_obtain_preserves_an_existing_failed_default_pilot) {
    struct Hooks final : mech3::PilotHooks {
        void store_fill(mech3::Store &, int32_t) override {}
        int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
    } hooks;
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        mech3::Pilot *current = mech3::current_pilot();
        std::list<mech3::Pilot *> listed;
        Restore() {
            listed.swap(mech3::pilots());
            mech3::current_pilot() = nullptr;
        }
        ~Restore() {
            for (auto *pilot : mech3::pilots()) {
                pilot->dirty = 0;
                delete pilot;
            }
            mech3::pilots().clear();
            listed.swap(mech3::pilots());
            mech3::current_pilot() = current;
            mech3::activate_content(content);
            zutl::mounts() = mounts;
            mech3::set_pilot_hooks(hooks);
            mech3::clear_content_base_digest_cache();
        }
    } restore;
    zutl::mounts() = {};
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "campaign-corrupt-default";
    cc::content::fs::create_directories(root / "pilots");
    zutl::set_write_dir(root.string());
    mech3::set_pilot_hooks(&hooks);
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    mech3::activate_content(content);
    const auto index = root / "pilots" / "CoreCritical.mw3";
    {
        std::ofstream output(index, std::ios::binary);
        output << "incompatible pilot index";
    }
    const auto before = cc::content::file_sha256(index);
    EXPECT(mech3::load_pilot("CoreCritical", false) == nullptr);
    bool rejected = false;
    try {
        mech3::obtain_content_pilot();
    } catch (const cc::content::Error &error) {
        rejected = error.diagnostic.code == "campaign-pilot";
    }
    EXPECT(rejected && mech3::find_pilot("CoreCritical") == nullptr);
    EXPECT(mech3::current_pilot() == nullptr && before == cc::content::file_sha256(index));
}

TEST(content_campaign_stock_progression_keeps_managed_inventory_and_run_identity) {
    struct Hooks final : mech3::PilotHooks {
        int snapshots = 0, restores = 0;
        bool load_world(const char *, int32_t) override {
            ++restores;
            return false;
        }
        void store_fill(mech3::Store &, int32_t) override {}
        int32_t store_add_mech(mech3::Store &, int32_t, const char *) override { return 0; }
        void save_world(const char *, int32_t) override { ++snapshots; }
    } hooks;
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content = mech3::active_content();
        zutl::Mounts mounts = zutl::mounts();
        mech3::PilotHooks *hooks = &mech3::pilot_hooks();
        ~Restore() {
            mech3::activate_content(content);
            zutl::mounts() = mounts;
            mech3::set_pilot_hooks(hooks);
            mech3::clear_content_base_digest_cache();
        }
    } restore;
    zutl::mounts() = {};
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "stock-campaign-save";
    cc::content::fs::create_directories(root);
    zutl::set_write_dir(root.string());
    mech3::set_pilot_hooks(&hooks);
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    content->definitions.push_back({"equipment",
                                    "demo:stock_ecm",
                                    "demo",
                                    "mw3:equipment/ecm@1",
                                    {{"name", "stock_campaign_ecm"},
                                     {"label", "Campaign ECM"},
                                     {"tech", 2},
                                     {"weight", {1.0, 1.0}},
                                     {"slots", {1, 1}}},
                                    15});
    mech3::activate_content(content);
    mech3::Pilot pilot("StockCampaign");
    pilot.dirty = 0;
    auto &first = *static_cast<mech3::CampaignGame *>(pilot.campaigns.front());
    first.slot.mech = -1;
    first.store.add_equipment(15, 2, 2);
    first.store50.add_equipment(15, 2, 1);
    EXPECT(mech3::adopt_content_stock_campaign(first));
    const auto run = first.content_state["run"];
    EXPECT(first.content_mission == "stock:c1m1" && mech3::content_stock_campaign(first));
    EXPECT(pilot.save()); // Includes the launched first stock mission, previously skipped.
    pilot.file = std::tmpfile();
    EXPECT(pilot.file != nullptr);
    if (!pilot.file)
        return;
    first.save();
    std::rewind(pilot.file);
    mech3::CampaignGame reloaded(&pilot); // Native binary load also merges pending stock inventory.
    pilot.close();
    EXPECT(reloaded.content_state["run"] == run);
    mech3::EquipmentItem probe;
    probe.equipment = 15;
    probe.tech = 2;
    EXPECT(reloaded.store.count_like(&probe) == 3 && reloaded.store50.items.empty());
    std::unique_ptr<mech3::CampaignGame> following(reloaded.next_mission(true, false));
    EXPECT(following && following->operation() == 1 && following->mission_number() == 2);
    if (!following)
        return;
    EXPECT(following->content_mission == "stock:c1m2" && following->content_state["run"] == run);
    EXPECT(following->store.count_like(&probe) == 3 && hooks.snapshots == 0);
    pilot.file = std::tmpfile();
    EXPECT(pilot.file != nullptr);
    if (!pilot.file)
        return;
    following->save();
    mech3::CampaignGame restored(&pilot, 1, 2);
    restored.content_run = following->content_run;
    mech3::read_content_campaign(restored);
    EXPECT(restored.store.count_like(&probe) == 3 && restored.content_state["run"] == run);
    EXPECT(content->find("mission", "stock:c1m2")->data["persist_world"] == true);
    EXPECT(content->find("mission", "stock:c1m2")->data["persistence_group"] == "stock:c1");
    EXPECT(mech3::start_content_mission("stock:c1m2"));
    const auto path = zutl::write_path((std::string(restored.path()) + ".ccsave").c_str());
    auto state = cc::content::load_bundle(path, *content).state;
    state["world_snapshot"] = true;
    state["snapshot_mission"] = "stock:c1m2";
    state["world_digest"] = "different-world";
    const std::vector<uint8_t> snapshot = {1, 2, 3, 4, 5, 6, 7, 8};
    cc::content::save_bundle(path, state, snapshot, *content);
    auto rejects_restore = [&]() {
        try {
            mech3::restore_content_campaign(restored);
        } catch (const cc::content::Error &error) {
            return error.diagnostic.code == "save-world";
        }
        return false;
    };
    const auto mismatch_hash = cc::content::file_sha256(path);
    EXPECT(rejects_restore() && hooks.restores == 0);
    EXPECT(mismatch_hash == cc::content::file_sha256(path));
    state["world_digest"] = mech3::content_world_digest();
    cc::content::save_bundle(path, state, snapshot, *content);
    const auto verified_hash = cc::content::file_sha256(path);
    EXPECT(rejects_restore() && hooks.restores == 1);
    EXPECT(verified_hash == cc::content::file_sha256(path));
    EXPECT(!cc::content::fs::exists(
        zutl::write_path((std::string(restored.path()) + ".restore").c_str())));
    restored.content_state["stock_mission"] = 3;
    bool rejected = false;
    try {
        mech3::validate_content_stock_campaign(restored);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
}
