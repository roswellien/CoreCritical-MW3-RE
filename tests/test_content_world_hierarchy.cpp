#include "check.h"
#include "content/world.h"
#include "content/world_hierarchy.h"
#include "content/persistence.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsave/zsave.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "gamez/zdeclient/zdec_crater.h"
#include "mech3/content_base.h"
#include "mech3/content_campaign.h"
#include "mech3/content_network.h"
#include "mech3/content_registry.h"
#include "mech3/content_world.h"
#include "mech3/content_world_hierarchy.h"
#include "mech3/content_world_state.h"
#include "mech3/mech_objective.h"
#include "mech3/mission.h"
#include "mech3/pilot.h"
#include "mech3/veh_controls.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace {
using cc::content::Definition;
using cc::content::Json;
void put32(std::string &out, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>(value >> (8 * i)));
}
Json identity() { return Json{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
struct Fixture {
    std::shared_ptr<cc::content::ResolvedContentSet> content =
        std::make_shared<cc::content::ResolvedContentSet>();
    Json document;
    cc::content::fs::path root = cc::content::fs::path(MW3_TEST_TMP) / "gltf-hierarchy";
    Fixture() {
        const double turn = std::sqrt(0.5);
        document = {
            {"asset", {{"version", "2.0"}}},
            {"buffers", Json::array({{{"byteLength", 44}}})},
            {"bufferViews", Json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                                         {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 6}}})},
            {"accessors",
             Json::array(
                 {{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
                  {{"bufferView", 1}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}}})},
            {"meshes", Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION", 0}}},
                                                                  {"indices", 1}}})}}})},
            {"nodes", Json::array({{{"name", "Root"},
                                    {"translation", {10.0, 2.0, 20.0}},
                                    {"rotation", {0.0, turn, 0.0, turn}},
                                    {"children", {1}}},
                                   {{"name", "Deck"},
                                    {"translation", {3.0, 4.0, 0.0}},
                                    {"mesh", 0},
                                    {"children", {2}}},
                                   {{"name", "Latch"}, {"translation", {0.0, 3.0, 1.0}}}})},
            {"scenes", Json::array({{{"nodes", {0}}}})},
            {"scene", 0}};
        content->definitions = mech3::stock_content();
        Json markers = Json::array();
        for (const auto &group : {std::make_pair("support", 4), std::make_pair("collide", 12)})
            for (int i = 0; i < group.second; ++i) {
                char name[24];
                std::snprintf(name, sizeof name, "%s%02d", group.first, i);
                markers.push_back(
                    {{"name", name}, {"position", {double(i & 1), 0.0, double(i >> 1)}}});
            }
        content->definitions.push_back({"world_asset",
                                        "hierarchy:asset",
                                        "hierarchy",
                                        "static_mesh",
                                        {{"source", "scene.glb"}, {"markers", markers}},
                                        0});
        const Json placement = {{"id", "fixture"},
                                {"asset", "hierarchy:asset"},
                                {"translation", {-30.0, 1.0, 8.0}},
                                {"rotation", {0.0, 1.0, 0.0, 0.0}},
                                {"scale", {2.0, 1.0, 2.0}}};
        const Json world_data = {{"bounds", {-256.0, 256.0, 512.0, 512.0}},
                                 {"spawn", {-51.0, 20.0, -26.0}},
                                 {"instances", Json::array({placement})}};
        content->definitions.push_back(
            {"world", "hierarchy:world", "hierarchy", "authored", world_data, 9});
        const Json condition = {"node",    Json{"fixture", "Root", "Deck", "Latch"},
                                "state",   "active",
                                "silent",  1,
                                "visible", 0};
        const Json objective_document = {"m1", Json{"primary", Json{"node_change", condition}}};
        const Json mission_data = {{"world", "hierarchy:world"},
                                   {"number", 1},
                                   {"readers", {{"objectives.zrd", objective_document}}}};
        content->definitions.push_back(
            {"mission", "hierarchy:mission", "hierarchy", "native", mission_data, 77});
        content->packages.push_back(
            {root, {{"files", Json::object()}}, "hierarchy", "1.0.0", "", {}});
        write();
    }
    void write() {
        cc::content::fs::create_directories(root);
        const float vertices[] = {-2, 0, 2, 2, 0, 2, 0, 0, -2};
        std::string binary(reinterpret_cast<const char *>(vertices), sizeof vertices);
        binary.append("\x00\x00\x01\x00\x02\x00\x00\x00", 8);
        std::string json = document.dump();
        while (json.size() % 4)
            json += ' ';
        std::string bytes;
        put32(bytes, 0x46546c67);
        put32(bytes, 2);
        put32(bytes, static_cast<uint32_t>(28 + json.size() + binary.size()));
        put32(bytes, static_cast<uint32_t>(json.size()));
        put32(bytes, 0x4e4f534a);
        bytes += json;
        put32(bytes, static_cast<uint32_t>(binary.size()));
        put32(bytes, 0x004e4942);
        bytes += binary;
        const auto file = root / "scene.glb";
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        content->packages[0].manifest["files"]["scene.glb"] = {
            {"bytes", bytes.size()}, {"sha256", cc::content::file_sha256(file)}};
    }
    Json compile() const {
        return cc::content::compile_world(*content->find("world", "hierarchy:world"), *content);
    }
};
bool rejects(const std::function<void()> &operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return true;
    }
    return false;
}
struct NativeScene {
    std::shared_ptr<const cc::content::ResolvedContentSet> previous = mech3::active_content();
    explicit NativeScene(Fixture &fixture) {
        mech3::activate_content(fixture.content);
        zcls::classes_reset();
        zmth::reset_stack();
        zmod::models() = zmod::ModelPool{};
        zmod::reset_pool();
        EXPECT(mech3::start_content_mission("hierarchy:mission"));
        const int index = mech3::mission_dir_index(
            ("ccw" + cc::content::sha256("hierarchy:world").substr(0, 16)).c_str());
        EXPECT(mech3::load_content_world(index));
    }
    ~NativeScene() {
        mech3::clear_content_world();
        zcls::classes_reset();
        mech3::activate_content(previous);
    }
};
struct NativeObjectiveHooks final : mech3::ObjectiveHooks {
    std::vector<zcls::Node *> handles{nullptr};
    zgame::Handle handle(zcls::Node *node) {
        if (!node)
            return 0;
        handles.push_back(node);
        return static_cast<zgame::Handle>(handles.size() - 1);
    }
    zgame::Handle node_find(const char *name) override {
        zcls::Node *root = nullptr;
        if (mech3::content_hierarchy_root(name, &root))
            return handle(root);
        return handle(zcls::list_find_by_name(zcls::kAllNodes, name));
    }
    zgame::Handle subnode_find(zgame::Handle parent, const char *name) override {
        zcls::Node *child = nullptr;
        if (mech3::content_hierarchy_child(handles.at(parent), name, &child))
            return handle(child);
        return handle(zcls::node_find_in_tree(handles.at(parent), name));
    }
    uint8_t node_active(zgame::Handle node) override {
        return (handles.at(node)->flags() >> 2) & 1;
    }
    const char *message(int32_t) override { return "Objective"; }
    const char *localize(const char *value) override { return value; }
    zgame::Handle sound_find(const char *) override { return 0; }
    int32_t game_type() override { return 0; }
};
} // namespace

TEST(content_world_hierarchy_managed_snapshot_restores_new_scene_and_preflights_callbacks) {
    struct Hooks final : mech3::PilotHooks {
        int loads = 0;
        void store_fill(mech3::Store&, int32_t) override {}
        int32_t store_add_mech(mech3::Store&, int32_t, const char*) override { return 0; }
        bool load_world(const char* path, int32_t kind) override {
            ++loads;
            return PilotHooks::load_world(path, kind);
        }
    } hooks;
    struct Restore {
        mech3::PilotHooks* hooks = &mech3::pilot_hooks();
        zutl::Mounts mounts = zutl::mounts();
        ~Restore() {
            mech3::clear_content_world();
            zsave::shutdown();
            mech3::set_pilot_hooks(hooks);
            zutl::mounts() = mounts;
            mech3::clear_content_base_digest_cache();
        }
    } restore;
    const auto directory = cc::content::fs::path(MW3_TEST_TMP) / "activation-integration";
    cc::content::fs::create_directories(directory);
    zutl::mounts() = {};
    zutl::set_write_dir(directory.string());
    mech3::set_pilot_hooks(&hooks);
    zsave::init();
    Fixture fixture;
    for (auto& definition : fixture.content->definitions)
        if (definition.id == "hierarchy:mission") definition.data["persist_world"] = true;
    mech3::Pilot pilot("ActivationIntegration");
    pilot.dirty = 0;
    pilot.file = std::tmpfile();
    EXPECT(pilot.file != nullptr);
    if (!pilot.file) return;
    mech3::CampaignGame game(&pilot, 1000, 1);
    game.content_mission = "hierarchy:mission";
    game.slot.mech = -1;
    mech3::bind_content_campaign_run(game);
    cc::content::fs::path bundle_path;
    auto latch = []() {
        return zcls::node_find_in_tree(
            zcls::list_find_by_name(zcls::kAllNodes, "fixture"), "Latch");
    };
    std::vector<uint8_t> snapshot;
    std::string digest;
    uint64_t first_allocation = 0;
    {
        NativeScene scene(fixture);
        bundle_path = cc::content::fs::u8path(
            zutl::write_path((std::string(game.path()) + ".ccsave").c_str()));
        cc::content::fs::create_directories(bundle_path.parent_path());
        auto* node = latch();
        EXPECT(node != nullptr);
        if (!node) return;
        first_allocation = node->allocation_id;
        zcls::node_set_active(node, 0);
        snapshot = mech3::capture_content_world_snapshot(game.path());
        digest = mech3::content_world_digest();
    }
    {
        NativeScene scene(fixture);
        auto* node = latch();
        EXPECT(node && node->allocation_id != first_allocation && (node->flags() & 4));
        if (!node) return;
        const Json state = {{"campaign", game.content_campaign}, {"mission", game.content_mission},
                            {"graph", game.content_state}, {"world_snapshot", true},
                            {"snapshot_mission", game.content_mission}, {"world_digest", digest},
                            {"base_digest", mech3::content_base_digest()},
                            {"simulation_options", mech3::content_simulation_options()}};
        cc::content::save_bundle(bundle_path, state, snapshot, *fixture.content);
        EXPECT(mech3::restore_content_campaign(game));
        EXPECT(hooks.loads == 1 && (node->flags() & 4) == 0);

        // A throwing native callback must close the temporary file and leave
        // the already accepted bundle untouched, then permit another capture.
        const auto accepted_hash = cc::content::file_sha256(bundle_path);
        zdec::craters().history.push_back(zdec::HistoryEntry{});
        EXPECT(rejects([&] { mech3::capture_content_world_snapshot(game.path()); }));
        EXPECT(zsave::manager()->archive.file == nullptr);
        EXPECT(cc::content::file_sha256(bundle_path) == accepted_hash);
        zdec::craters().history.clear();
        EXPECT(!mech3::capture_content_world_snapshot(game.path()).empty());
        for (const auto& file : cc::content::fs::directory_iterator(bundle_path.parent_path()))
            EXPECT(file.path().filename().string().find(".snapshot-") != 0);

        // Put a real sentinel entry first in native archive order. A direct
        // native load proves the control callback runs without managed preflight.
        uint32_t footer[2]{};
        std::memcpy(footer, snapshot.data() + snapshot.size() - 8, 8);
        const size_t table = snapshot.size() - 8 - footer[1] * sizeof(zutl_zar::Entry);
        std::vector<zutl_zar::Entry> entries(footer[1]);
        std::memcpy(entries.data(), snapshot.data() + table, entries.size() * sizeof(entries[0]));
        auto corrupt_activation = snapshot;
        for (const auto& entry : entries)
            if (std::strcmp(entry.name, "ContentWorld/Activation") == 0)
                corrupt_activation[entry.start] = '!';
        cc::content::save_bundle(bundle_path, state, corrupt_activation, *fixture.content);
        zcls::node_set_active(node, 1);
        EXPECT(rejects([&] { mech3::restore_content_campaign(game); }));
        EXPECT(hooks.loads == 1 && (node->flags() & 4));
        std::vector<uint8_t> with_sentinel(snapshot.begin(), snapshot.begin() + table);
        zutl_zar::Entry sentinel{};
        std::snprintf(sentinel.name, sizeof sentinel.name, "%s", "Sentinel/Value");
        sentinel.start = static_cast<uint32_t>(with_sentinel.size());
        sentinel.length = 1;
        with_sentinel.push_back(42);
        entries.insert(entries.begin(), sentinel);
        const auto* entry_bytes = reinterpret_cast<const uint8_t*>(entries.data());
        with_sentinel.insert(with_sentinel.end(), entry_bytes,
                             entry_bytes + entries.size() * sizeof(entries[0]));
        footer[1] = static_cast<uint32_t>(entries.size());
        const auto* tail = reinterpret_cast<const uint8_t*>(footer);
        with_sentinel.insert(with_sentinel.end(), tail, tail + 8);
        int sentinel_calls = 0;
        zsave::add("Sentinel", nullptr,
                   [](zsave::Context&, const char*, const void*, uint32_t, void* user) {
                       ++*static_cast<int*>(user);
                   }, 500, &sentinel_calls);
        const auto control = directory / "sentinel-control.zbd";
        {
            std::ofstream output(control, std::ios::binary);
            output.write(reinterpret_cast<const char*>(with_sentinel.data()), with_sentinel.size());
        }
        EXPECT(zsave::load(control.generic_string().c_str(), 2));
        EXPECT(sentinel_calls == 1);
        sentinel_calls = 0;
        for (const auto& entry : entries)
            if (std::strcmp(entry.name, "ContentWorld/Activation") == 0)
                with_sentinel[entry.start] = '!';
        cc::content::save_bundle(bundle_path, state, with_sentinel, *fixture.content);
        zcls::node_set_active(node, 1);
        EXPECT(rejects([&] { mech3::restore_content_campaign(game); }));
        EXPECT(hooks.loads == 1 && sentinel_calls == 0 && (node->flags() & 4));
    }
}

TEST(content_world_hierarchy_retains_transform_only_nodes_and_local_bindings) {
    Fixture fixture;
    const auto compiled = fixture.compile();
    EXPECT(compiled["nodes"].size() == 3);
    EXPECT(compiled["chunks"].size() == 1);
    EXPECT(compiled["nodes"][0]["id"] == "fixture/gltf/0");
    EXPECT(compiled["nodes"][0]["parent"].is_null());
    EXPECT(compiled["nodes"][1]["parent"] == "fixture/gltf/0");
    EXPECT(compiled["nodes"][2]["name"] == "Latch");
    EXPECT(compiled["chunks"][0]["node"] == "fixture/gltf/1");
    EXPECT(compiled["chunks"][0]["vertices"][0] == Json({-2.0, 0.0, 2.0}));
    EXPECT(compiled == fixture.compile());
    auto second = *fixture.content->find("world", "hierarchy:world");
    second.data["instances"][0]["id"] = "second";
    auto combined = *fixture.content->find("world", "hierarchy:world");
    combined.data["instances"].push_back(second.data["instances"][0]);
    const auto twice = cc::content::compile_world(combined, *fixture.content);
    EXPECT(twice["nodes"].size() == 6);
    EXPECT(twice["nodes"][3]["id"] == "second/gltf/0");
    EXPECT(twice["chunks"][1]["node"] == "second/gltf/1");
}

TEST(content_world_hierarchy_rejects_invalid_graphs_matrices_and_bindings) {
    Fixture fixture;
    const auto good = fixture.compile();
    auto invalid = good;
    invalid["nodes"][0]["parent"] = "missing";
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["nodes"][0]["parent"] = "fixture/gltf/2";
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["nodes"][2]["parent"] = nullptr;
    invalid["nodes"][2]["name"] = "Root";
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["nodes"].push_back(invalid["nodes"][0]);
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["nodes"][0]["local_matrix"][12] = 10000001;
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["nodes"][0]["local_matrix"][0] = 0;
    invalid["nodes"][0]["local_matrix"][1] = 0;
    invalid["nodes"][0]["local_matrix"][2] = 0;
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["nodes"][0]["gravity"] = 1;
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["chunks"][0]["node"] = "unknown";
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["chunks"][0]["instance"] = "other";
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["chunks"][0]["reflection"] = true;
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["chunks"][0]["reflection"] = 1;
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["chunks"][0].erase("node");
    invalid["chunks"][0]["reflection"] = false;
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["nodes"] = Json::array();
    for (int i = 0; i < 25; ++i)
        invalid["nodes"].push_back(
            {{"id", "n" + std::to_string(i)},
             {"instance", "fixture"},
             {"name", "n" + std::to_string(i)},
             {"parent", i ? Json("n" + std::to_string(i - 1)) : Json(nullptr)},
             {"local_matrix", identity()}});
    invalid["chunks"][0]["node"] = "n24";
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid = good;
    invalid["nodes"] = Json::array();
    for (size_t i = 0; i <= cc::content::hierarchy::max_nodes; ++i)
        invalid["nodes"].push_back(nullptr);
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    fixture.document["nodes"][0]["matrix"] = identity();
    fixture.write();
    EXPECT(rejects([&] { fixture.compile(); }));
    fixture.document["nodes"][0].erase("matrix");
    fixture.document["nodes"][0]["children"] = {1.5};
    fixture.write();
    EXPECT(rejects([&] { fixture.compile(); }));
    fixture.document["nodes"][0]["children"] = {1};

    fixture.document["nodes"][2]["children"] = {0};
    fixture.write();
    EXPECT(rejects([&] { fixture.compile(); }));
    fixture.document["nodes"][2].erase("children");
    fixture.document["nodes"][0]["children"] = {1, 2};
    fixture.write();
    EXPECT(rejects([&] { fixture.compile(); }));
}

TEST(content_world_hierarchy_native_objective_and_collision_follow_nested_glb_transforms) {
    Fixture fixture;
    NativeScene scene(fixture);
    auto *world = zcls::list_find_by_name(zcls::kWorlds, "world1");
    auto *instance = zcls::list_find_by_name(zcls::kAllNodes, "fixture");
    zcls::Node *root = nullptr, *deck = nullptr, *latch = nullptr;
    EXPECT(mech3::content_hierarchy_child(instance, "Root", &root) && root);
    EXPECT(mech3::content_hierarchy_child(root, "Deck", &deck) && deck);
    EXPECT(mech3::content_hierarchy_child(deck, "Latch", &latch) && latch);
    float position[3]{};
    if (latch)
        zcls::node_world_position(latch, position);
    EXPECT(std::abs(position[0] + 52.0f) < 0.001f && std::abs(position[1] - 10.0f) < 0.001f &&
           std::abs(position[2] + 26.0f) < 0.001f);
    zcls::GroundHits ground{};
    zcls::ground_point(world, -51.0f, 100.0f, -26.0f, &ground);
    EXPECT(ground.count > 0);
    if (ground.count)
        EXPECT(std::abs(ground.hit[0].height - 7.0f) < 0.001f);
    zcls::LineHits line{};
    const float from[] = {-51, 20, -26}, to[] = {-51, -10, -26};
    zcls::segment_nearest(world, from, to, &line);
    EXPECT(line.total > 0 || line.count > 0);
    if (ground.count) {
        auto *geometry = ground.hit[0].node;
        auto *original = geometry->model;
        EXPECT(mech3::restore_content_node(geometry));
        EXPECT(geometry->model != original);
        zcls::ground_point(world, -51.0f, 100.0f, -26.0f, &ground);
        EXPECT(ground.count > 0 && std::abs(ground.hit[0].height - 7.0f) < 0.001f);
    }
    auto *marker = zcls::node_find_in_tree(instance, "support00");
    if (marker)
        zcls::node_world_position(marker, position);
    EXPECT(marker && std::abs(position[0] + 30.0f) < 0.001f &&
           std::abs(position[1] - 1.0f) < 0.001f && std::abs(position[2] - 8.0f) < 0.001f);
    auto *tree = mech3::content_world_reader("objectives.zrd");
    NativeObjectiveHooks hooks;
    mech3::Objectives objectives;
    EXPECT(mech3::read_objectives(zrdr::find(tree, "m1"), objectives, hooks));
    auto *objective =
        objectives.primary && !objectives.primary->items.empty()
            ? dynamic_cast<mech3::NodeChangeObjective *>(objectives.primary->items.front().get())
            : nullptr;
    EXPECT(objective && hooks.handles.at(objective->node) == latch);
    if (objective)
        EXPECT(objective->check(hooks) == 1);
    zrdr::free_node(tree);
}

TEST(content_world_hierarchy_mirrored_parent_preserves_native_ground_surface) {
    Fixture fixture;
    fixture.document["nodes"][0]["scale"] = {-1.0, 1.0, 1.0};
    fixture.write();
    const auto compiled = fixture.compile();
    EXPECT(compiled["chunks"][0]["reflection"] == true);
    EXPECT(compiled["chunks"][0]["vertices"][0] == Json({2.0, 0.0, 2.0}));
    EXPECT(compiled["chunks"][0]["vertices"][1] == Json({0.0, 0.0, -2.0}));
    auto invalid = compiled;
    invalid["chunks"][0].erase("reflection");
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    invalid["chunks"][0]["reflection"] = false;
    EXPECT(rejects([&] { cc::content::validate_compiled_world(invalid); }));
    NativeScene scene(fixture);
    auto *world = zcls::list_find_by_name(zcls::kWorlds, "world1");
    zcls::GroundHits ground{};
    zcls::ground_point(world, -51.0f, 100.0f, -38.0f, &ground);
    EXPECT(ground.count > 0);
    if (ground.count)
        EXPECT(std::abs(ground.hit[0].height - 7.0f) < 0.001f);
    zcls::LineHits line{};
    const float from[] = {-51, 20, -38}, to[] = {-51, -10, -38};
    zcls::segment_nearest(world, from, to, &line);
    EXPECT(line.total > 0 || line.count > 0);
    line = {};
    zcls::segment_nearest(world, to, from, &line);
    EXPECT(line.total == 0 && line.count == 0);
    if (ground.count) {
        auto *geometry = ground.hit[0].node;
        EXPECT(mech3::restore_content_node(geometry));
        line = {};
        zcls::segment_nearest(world, from, to, &line);
        EXPECT(line.total > 0 || line.count > 0);
        zcls::ground_point(world, -51.0f, 100.0f, -38.0f, &ground);
        EXPECT(ground.count > 0 && std::abs(ground.hit[0].height - 7.0f) < 0.001f);
    }
}

TEST(content_world_hierarchy_vehicle_asset_keeps_nodes_and_marker_bindings_under_actor) {
    Fixture fixture;
    fixture.document["nodes"][1]["children"] = {2, 3, 4};
    fixture.document["nodes"].push_back(
        {{"name", "support00"}, {"translation", {1000.0, 1000.0, 1000.0}}});
    fixture.document["nodes"].push_back(
        {{"name", "collide00"}, {"translation", {1000.0, 1000.0, 1000.0}}});
    fixture.write();
    fixture.content->definitions.push_back({"vehicle",
                                            "hierarchy:truck",
                                            "hierarchy",
                                            "mw3:ai-vehicle@1",
                                            {{"name", "hierarchytruck"},
                                             {"label", "Hierarchy Truck"},
                                             {"base", "stock:ttruck"},
                                             {"model_name", "ttruck"},
                                             {"asset", "hierarchy:asset"}},
                                            5});
    NativeScene scene(fixture);
    auto *actor = mech3::create_content_vehicle_node("hierarchytruck", "hierarchytruck_01");
    EXPECT(actor != nullptr);
    zcls::object3d_set_translation(actor, 100, 0, 0);
    zcls::update_node(actor);
    zcls::frame_update();
    zcls::Node *root = nullptr, *deck = nullptr, *latch = nullptr;
    EXPECT(mech3::content_hierarchy_child(actor, "Root", &root) && root);
    EXPECT(mech3::content_hierarchy_child(root, "Deck", &deck) && deck);
    EXPECT(mech3::content_hierarchy_child(deck, "Latch", &latch) && latch);
    float position[3]{};
    if (latch)
        zcls::node_world_position(latch, position);
    EXPECT(latch && std::abs(position[0] - 111.0f) < 0.001f &&
           std::abs(position[1] - 9.0f) < 0.001f && std::abs(position[2] - 17.0f) < 0.001f);
    zcls::Node *marker = nullptr, *imported_marker = nullptr;
    EXPECT(mech3::content_hierarchy_child(actor, "support00", &marker) && marker);
    EXPECT(mech3::content_hierarchy_child(deck, "support00", &imported_marker) && imported_marker);
    EXPECT(marker != imported_marker);
    mech3::VehControlsHooks controls;
    EXPECT(controls.node_find(actor, "support00") == marker);
    // Even if the root body point is removed, a deep imported alias is not a
    // valid substitute for the explicit vehicle marker contract.
    zcls::node_set_name(marker, "removed_support");
    EXPECT(controls.node_find(actor, "support00") == nullptr);
    zcls::node_set_name(marker, "support00");
    zcls::Node *collision_marker = nullptr;
    EXPECT(mech3::content_hierarchy_child(actor, "collide00", &collision_marker) &&
           collision_marker);
    EXPECT(controls.node_find(actor, "collide00") == collision_marker);
    if (marker)
        zcls::node_world_position(marker, position);
    EXPECT(marker && std::abs(position[0] - 100.0f) < 0.001f && std::abs(position[1]) < 0.001f &&
           std::abs(position[2]) < 0.001f);
}

TEST(content_world_hierarchy_paths_require_instance_root_and_tags_follow_native_lifetime) {
    Fixture fixture;
    fixture.document["nodes"][0]["children"] = {1, 3};
    fixture.document["nodes"].push_back({{"name", "Branch"}, {"children", {4}}});
    fixture.document["nodes"].push_back({{"name", "Latch"}, {"translation", {0.0, 1.0, 0.0}}});
    fixture.write();
    NativeScene scene(fixture);
    zcls::Node *world = nullptr, *instance = nullptr, *root = nullptr, *deck = nullptr,
               *latch = nullptr;
    EXPECT(mech3::content_hierarchy_root("world1", &world) && world && world->cls() == 2);
    EXPECT(mech3::content_hierarchy_child(world, "fixture", &instance) && instance);
    EXPECT(mech3::content_hierarchy_child(world, "Deck", &deck) && !deck);
    EXPECT(mech3::content_hierarchy_root("fixture", &instance) && instance);
    EXPECT(rejects([&] { mech3::content_hierarchy_root("Latch", &root); }));
    EXPECT(mech3::content_hierarchy_child(instance, "Root", &root) && root);
    EXPECT(mech3::content_hierarchy_child(root, "Deck", &deck) && deck);
    EXPECT(mech3::content_hierarchy_child(deck, "Latch", &latch) && latch);
    NativeObjectiveHooks hooks;
    const auto world_handle = hooks.node_find("world1");
    EXPECT(world_handle && hooks.subnode_find(world_handle, "Deck") == 0);
    const auto instance_handle = hooks.subnode_find(world_handle, "fixture");
    const auto root_handle = hooks.subnode_find(instance_handle, "Root");
    const auto deck_handle = hooks.subnode_find(root_handle, "Deck");
    EXPECT(hooks.handles.at(hooks.subnode_find(deck_handle, "Latch")) == latch);
    EXPECT(!mech3::content_hierarchy_child(root, "Latch", &latch) || !latch);
    EXPECT(mech3::content_hierarchy_child(deck, "Latch", &latch) && latch);
    auto *copied = zcls::node_copy(latch);
    EXPECT(copied && copied->content_hierarchy_parent && !copied->content_hierarchy_root);
    const int immediate = zcls::immediate_delete();
    zcls::immediate_delete() = 1;
    EXPECT(zcls::node_destroy(copied) == 0);
    auto *recycled = zcls::object3d_create();
    EXPECT(recycled == copied && !recycled->content_hierarchy_parent &&
           !recycled->content_hierarchy_root);
    zcls::Node *unused = nullptr;
    EXPECT(!mech3::content_hierarchy_child(recycled, "Latch", &unused));
    EXPECT(zcls::node_destroy(recycled) == 0);
    zcls::immediate_delete() = immediate;
}
