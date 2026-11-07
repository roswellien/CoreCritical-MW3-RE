#include "check.h"
#include "content/world.h"
#include "content/world_hierarchy.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zreader/zreader.h"
#include "mech3/content_registry.h"
#include "mech3/content_world.h"
#include "mech3/content_world_hierarchy.h"
#include "mech3/mech_objective.h"
#include "mech3/mission.h"

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
        zcls::classes_reset();
        mech3::clear_content_world();
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
    EXPECT(compiled["chunks"][0]["vertices"][1] == Json({0.0, 0.0, -2.0}));
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
}

TEST(content_world_hierarchy_vehicle_asset_keeps_nodes_and_marker_bindings_under_actor) {
    Fixture fixture;
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
    auto *marker = zcls::node_find_in_tree(actor, "support00");
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
    zcls::Node *instance = nullptr, *root = nullptr, *deck = nullptr, *latch = nullptr;
    EXPECT(mech3::content_hierarchy_root("fixture", &instance) && instance);
    EXPECT(rejects([&] { mech3::content_hierarchy_root("Latch", &root); }));
    EXPECT(mech3::content_hierarchy_child(instance, "Root", &root) && root);
    EXPECT(mech3::content_hierarchy_child(root, "Deck", &deck) && deck);
    EXPECT(mech3::content_hierarchy_child(deck, "Latch", &latch) && latch);
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
