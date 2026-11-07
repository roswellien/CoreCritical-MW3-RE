#include "mech3/content_checkpoint.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <set>

#include "content/persistence.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/app_engine.h"
#include "mech3/content_campaign.h"
#include "mech3/content_registry.h"
#include "mech3/content_world.h"
#include "mech3/pilot.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace mech3 {
namespace {
using cc::content::fail;
using cc::content::Json;
namespace fs = cc::content::fs;

struct Staging {
    fs::path directory;
    bool keep = false;
    explicit Staging(const fs::path &parent) {
        fs::create_directories(parent);
        static std::atomic<uint64_t> sequence{0};
        for (int attempt = 0; attempt < 16; ++attempt) {
            directory =
                parent /
                (".checkpoint-import-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(++sequence));
            if (fs::create_directory(directory))
                return;
        }
        fail("checkpoint-busy", "Cannot reserve a checkpoint import staging directory");
    }
    ~Staging() {
        if (keep)
            return;
        std::error_code error;
        for (const auto *file : {"index.before", "bundle.new"})
            fs::remove(directory / file, error);
        fs::remove(directory, error);
    }
};

void activate(const fs::path &source, const fs::path &target, bool replace) {
#if defined(_WIN32)
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(source.c_str(), target.c_str(), flags))
        fail("checkpoint-activate", "Cannot atomically activate the checkpoint or pilot index", {},
             {}, target.generic_string());
#else
    if (replace)
        fs::rename(source, target);
    else {
        // Same-filesystem link publication is atomic and refuses an existing name.
        // rename() alone would replace another importer's concurrently created
        // file.
        fs::create_hard_link(source, target);
        std::error_code error;
        fs::remove(source, error);
    }
#endif
}

fs::path writable(const fs::path &root, const std::string &relative) {
    const fs::path path = fs::u8path(zutl::host(relative.c_str()));
    for (const auto &part : path)
        if (part == "..")
            fail("checkpoint-path", "Pilot path contains traversal");
    // Native pilot paths deliberately use doubled separators. Package paths
    // remain strictly normalized; normalize only this internal native spelling.
    return cc::content::confined_path(root, path.lexically_normal().generic_string());
}
} // namespace

bool configure_content_checkpoint(GameEngine &engine, const std::string &path) try {
    const auto content = active_content();
    if (!content || path.empty())
        return false;
    if (zutl::mounts().write.empty())
        fail("checkpoint-profile",
             "A checkpoint import requires the active profile's writable directory");
    const auto write_root = fs::weakly_canonical(fs::u8path(zutl::mounts().write));
    auto saved = cc::content::load_bundle(fs::u8path(path), *content);
    cc::content::validate_saved_campaign_graph(saved.state, *content);
    const auto graph = saved.state.at("graph");
    const auto run = graph.at("run").get<std::string>();
    if (!cc::content::valid_campaign_run_id(run))
        fail("campaign-run", "Checkpoint has no valid campaign run identity");
    if (graph.contains("terminal") &&
        (!graph["terminal"].is_boolean() || graph["terminal"] == true))
        fail("checkpoint-terminal",
             "A completed campaign run cannot be resumed as an active mission");

    const bool stock = saved.state.value("campaign", std::string{}) == "stock:campaign";
    int32_t operation = 0, visit = 1;
    if (stock) {
        operation = graph.at("stock_operation").get<int32_t>();
        visit = graph.at("stock_mission").get<int32_t>();
    } else if (graph.contains("attempt")) {
        if (!graph["attempt"].is_number_integer() || graph["attempt"] < 1 ||
            graph["attempt"] >= INT32_MAX)
            fail("checkpoint-visit", "Checkpoint progression exceeds the native visit index");
        visit = graph["attempt"].get<int32_t>();
    }
    // Validate all saved item/loadout/skin identities and native environment on
    // an unowned game before loading, creating or selecting any pilot.
    auto candidate = std::make_unique<CampaignGame>(nullptr, operation, visit);
    candidate->content_run = run;
    apply_content_campaign_state(*candidate, saved.state);
    const auto *mission = content->find("mission", candidate->content_mission);
    if (mission->data.value("multiplayer", false))
        fail("checkpoint-multiplayer",
             "Multiplayer missions do not support campaign checkpoint import");
    if (saved.state.contains("world_snapshot") && !saved.state["world_snapshot"].is_boolean())
        fail("save-native", "Checkpoint world snapshot marker is malformed");
    if (!saved.world.empty()) {
        if (!saved.state.value("world_snapshot", true))
            fail("save-native", "Semantic-only checkpoint contains unexpected native world bytes");
        const auto source_mission = saved.state.value("snapshot_mission", std::string{});
        if (source_mission.empty() || saved.state.value("world_digest", std::string{}) !=
                                          content_mission_world_digest(source_mission))
            fail("save-world", "Retained native snapshot does not match its declared "
                               "source mission; migrate with the world discarded");
    }
    (void)preflight_content_campaign_launch(*candidate);

    const auto previous_current = current_pilot();
    const auto previous_action = action_setup();
    const auto previous_launch = engine.launch();
    const auto previous_mission = content_selected_mission();
    std::set<Pilot *> previous_pilots(pilots().begin(), pilots().end());
    Pilot *known = previous_current ? previous_current : find_pilot("CoreCritical");
    if (known)
        previous_pilots.insert(known);
    const auto index = writable(write_root, known ? known->file_path() : "pilots/CoreCritical.mw3");
    const bool had_index = fs::exists(index);
    if (had_index && !fs::is_regular_file(index))
        fail("checkpoint-index", "Selected pilot index is not a regular file");
    Staging staging(index.parent_path());
    const auto backup = staging.directory / "index.before";
    if (had_index) {
        const auto digest = cc::content::file_sha256(index);
        fs::copy_file(index, backup);
        if (cc::content::file_sha256(backup) != digest || cc::content::file_sha256(index) != digest)
            fail("checkpoint-index", "Pilot index changed while preparing the "
                                     "import; source was preserved");
    }

    Pilot *pilot = nullptr;
    Game *previous_game = nullptr;
    uint8_t previous_dirty = 0;
    bool appended = false, owns_bundle = false, index_committed = false;
    fs::path canonical;
    try {
        pilot = obtain_content_pilot();
        if (!pilot)
            fail("campaign-pilot", "Cannot obtain the selected checkpoint pilot");
        if (writable(write_root, pilot->file_path()) != index)
            fail("checkpoint-index", "Selected pilot changed during import");
        previous_game = pilot->current;
        previous_dirty = pilot->dirty;
        for (const auto *entry : pilot->campaigns) {
            const auto *game = dynamic_cast<const CampaignGame *>(entry);
            if (game && (game->content_run == run ||
                         game->content_state.value("run", std::string{}) == run))
                fail("checkpoint-run-exists",
                     "This campaign run is already indexed in the selected pilot", {}, run);
        }
        candidate->pilot = pilot;
        if (!stock)
            candidate->campaign =
                content_campaign_operation_alias(*pilot, candidate->content_campaign + ":" + run);
        candidate->mission = visit;
        canonical = writable(write_root, std::string(candidate->path()) + ".ccsave");
        if (fs::exists(canonical))
            fail("checkpoint-exists",
                 "Import refuses to overwrite an existing canonical checkpoint", {}, {},
                 canonical.generic_string());
        fs::create_directories(canonical.parent_path());
        pilot->campaigns.reserve(pilot->campaigns.size() + 1);
        // The source is never moved. save_bundle supplies the ordinary checked,
        // atomic bundle encoding, then no-replace publication claims its new name.
        const auto prepared = staging.directory / "bundle.new";
        cc::content::save_bundle(prepared, saved.state, saved.world, *content);
        activate(prepared, canonical, false);
        owns_bundle = true;
        pilot->campaigns.push_back(candidate.get());
        appended = true;
        if (!pilot->save())
            fail("checkpoint-index", "Cannot atomically save the imported pilot index");
        index_committed = true;
        configure_existing_content_launch(engine, *pilot, *candidate);
        candidate.release();
        return true;
    } catch (...) {
        const auto failure = std::current_exception();
        if (pilot && appended) {
            auto &entries = pilot->campaigns;
            entries.erase(std::remove(entries.begin(), entries.end(), candidate.get()),
                          entries.end());
        }
        if (pilot) {
            pilot->current = previous_game;
            pilot->dirty = previous_dirty;
        }
        current_pilot() = previous_current;
        action_setup() = previous_action;
        // The shared launch has already been preflighted. This rollback also covers
        // an unexpected publication exception without discarding the old index.
        if (index_committed) {
            try {
                if (had_index)
                    activate(backup, index, true);
                else {
                    std::error_code error;
                    fs::remove(index, error);
                }
                engine.set_launch(previous_launch);
                engine.pilot_chosen(previous_current);
                if (!previous_mission.empty())
                    start_content_mission(previous_mission);
                else
                    clear_content_mission_selection();
            } catch (...) {
                staging.keep = true;
                fail("checkpoint-rollback",
                     "Cannot restore the previous pilot index; its backup was retained", {}, {},
                     backup.generic_string());
            }
        }
        if (owns_bundle) {
            std::error_code error;
            fs::remove(canonical, error);
        }
        if (pilot && !previous_pilots.count(pilot)) {
            pilots().remove(pilot);
            pilot->dirty = 0;
            delete pilot;
            if (!had_index) {
                std::error_code error;
                fs::remove(index, error);
            }
        }
        std::rethrow_exception(failure);
    }
} catch (const cc::content::Error &) {
    throw;
} catch (const std::exception &error) {
    fail("checkpoint-import", error.what());
}
} // namespace mech3
