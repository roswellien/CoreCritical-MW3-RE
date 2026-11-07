#include "check.h"
#include "content/world.h"
#include "gamez/zclass/camera.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zdeclient/zdec_crater.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zreader/zreader.h"
#include "mech3/content_registry.h"
#include "mech3/content_world.h"
#include "mech3/mech_objective.h"
#include "mech3/mission.h"
#include "mech3/zrdrainet.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace {
using cc::content::Json;
cc::content::Definition authored_world() {
    return {"world",
            "demo:terrain",
            "demo",
            "authored",
            {{"bounds", {-16, 16, 32, 32}},
             {"spawn", {0, 2, 0}},
             {"heightfield",
              {{"origin", {-16, 0, 16}},
               {"spacing", 16},
               {"heights", {{0, 0, 0}, {0, 3, 0}, {0, 0, 0}}}}}},
            9};
}
bool rejects_world(const cc::content::Definition &world,
                   const cc::content::ResolvedContentSet &content) {
    try {
        cc::content::compile_world(world, content);
    } catch (const cc::content::Error &) {
        return true;
    }
    return false;
}
void put32(std::string &out, uint32_t value, bool big = false) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>(value >> (8 * (big ? 3 - i : i))));
}
void png_chunk(std::string &out, const char *name, const std::string &value) {
    put32(out, static_cast<uint32_t>(value.size()), true);
    const size_t start = out.size();
    out.append(name, 4);
    out += value;
    uint32_t crc = 0xffffffffu;
    for (size_t i = start; i < out.size(); ++i) {
        crc ^= static_cast<unsigned char>(out[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    put32(out, crc ^ 0xffffffffu, true);
}
std::string red_png() {
    std::string png("\x89PNG\r\n\x1a\n", 8), header;
    put32(header, 2, true);
    put32(header, 2, true);
    header.append("\x08\x02\x00\x00\x00", 5);
    png_chunk(png, "IHDR", header);
    const std::string pixels("\x00\xff\x00\x00\xff\x00\x00\x00\xff\x00\x00\xff\x00\x00", 14);
    std::string zlib("\x78\x01\x01\x0e\x00\xf1\xff", 7);
    zlib += pixels;
    uint32_t a = 1, b = 0;
    for (unsigned char pixel : pixels) {
        a = (a + pixel) % 65521;
        b = (b + a) % 65521;
    }
    put32(zlib, (b << 16) | a, true);
    png_chunk(png, "IDAT", zlib);
    png_chunk(png, "IEND", {});
    return png;
}
} // namespace

TEST(content_world_compiles_glb_geometry_and_png_albedo_without_game_assets) {
    const auto root = cc::content::fs::path(MW3_TEST_TMP) / "authored-glb";
    cc::content::fs::create_directories(root);
    const float vertices[] = {0, 0, 0, 1, 0, 0, 0, 0, -1};
    std::string binary(reinterpret_cast<const char *>(vertices), sizeof vertices);
    binary.append("\x00\x00\x01\x00\x02\x00\x00\x00", 8);
    const auto image = red_png();
    binary += image;
    Json doc = {
        {"asset", {{"version", "2.0"}}},
        {"buffers", Json::array({{{"byteLength", binary.size()}}})},
        {"bufferViews",
         Json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                      {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 6}},
                      {{"buffer", 0}, {"byteOffset", 44}, {"byteLength", image.size()}}})},
        {"accessors",
         Json::array(
             {{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
              {{"bufferView", 1}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}}})},
        {"meshes", Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION", 0}}},
                                                              {"indices", 1},
                                                              {"material", 0}}})}}})},
        {"nodes", Json::array({{{"mesh", 0}}})},
        {"scenes", Json::array({{{"nodes", {0}}}})},
        {"scene", 0},
        {"images", Json::array({{{"bufferView", 2}, {"mimeType", "image/png"}}})},
        {"textures", Json::array({{{"source", 0}}})},
        {"materials",
         Json::array({{{"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}})}};
    std::string json = doc.dump();
    while (json.size() % 4)
        json += ' ';
    while (binary.size() % 4)
        binary += '\0';
    std::string glb;
    put32(glb, 0x46546c67);
    put32(glb, 2);
    put32(glb, static_cast<uint32_t>(28 + json.size() + binary.size()));
    put32(glb, static_cast<uint32_t>(json.size()));
    put32(glb, 0x4e4f534a);
    glb += json;
    put32(glb, static_cast<uint32_t>(binary.size()));
    put32(glb, 0x004e4942);
    glb += binary;
    const auto file = root / "scene.glb";
    {
        std::ofstream out(file, std::ios::binary);
        out.write(glb.data(), static_cast<std::streamsize>(glb.size()));
    }
    cc::content::ResolvedContentSet content;
    content.packages.push_back(
        {root,
         {{"files",
           {{"scene.glb", {{"bytes", glb.size()}, {"sha256", cc::content::sha256(glb)}}}}}},
         "demo",
         "1.0.0",
         "",
         {}});
    const cc::content::Definition world = {"world",
                                           "demo:source",
                                           "demo",
                                           "authored",
                                           {{"source", "scene.glb"}, {"bounds", {-1, 1, 2, 2}}},
                                           0};
    auto renamed = world;
    renamed.data["source"] = "scene.json";
    EXPECT(rejects_world(renamed, content));
    renamed.data["source"] = "one::two::scene.gltf";
    EXPECT(rejects_world(renamed, content));
    {
        std::ofstream out(root / "scene.GLB", std::ios::binary);
        out.write(glb.data(), static_cast<std::streamsize>(glb.size()));
    }
    content.packages[0].manifest["files"]["scene.GLB"] = {{"bytes", glb.size()},
                                                          {"sha256", cc::content::sha256(glb)}};
    renamed.data["source"] = "scene.GLB";
    EXPECT(cc::content::compile_world(renamed, content)["chunks"].size() == 1);
    content.definitions.push_back(world);
    const cc::content::Definition crosspack = {"world",    "variant:source",     "variant",
                                               "authored", {{"base", world.id}}, 1};
    EXPECT(cc::content::compile_world(crosspack, content)["textures"].size() == 1);
    const auto compiled = cc::content::compile_world(world, content);
    EXPECT(compiled["chunks"].size() == 1);
    EXPECT(compiled["textures"].size() == 1);
    EXPECT(compiled["textures"].begin().value()["pixels"][0] == 63488);
    EXPECT(compiled["textures"].begin().value()["width"] == 2);
    glb[8] = 0;
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(glb.data(), static_cast<std::streamsize>(glb.size()));
    }
    EXPECT(rejects_world(world, content));
}

TEST(content_world_unknown_parameters_fail_before_registry_activation) {
    const auto previous = mech3::active_content();
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content;
        ~Restore() { mech3::activate_content(content); }
    } restore{previous};
    auto valid = std::make_shared<cc::content::ResolvedContentSet>();
    valid->definitions = mech3::stock_content();
    valid->definitions.push_back(authored_world());
    valid->definitions.push_back(
        {"world_asset",
         "demo:rock",
         "demo",
         "static_mesh",
         {{"meshes", Json::array({{{"vertices", {{0, 0, 0}, {1, 0, 0}, {0, 0, -1}}},
                                   {"triangles", {{0, 1, 2}}}}})}},
         0});
    valid->definitions.push_back(
        {"mission",
         "demo:mission",
         "demo",
         "native",
         {{"world", "demo:terrain"},
          {"number", 1},
          {"readers",
           {{"objectives.zrd",
             Json{"m1",
                  Json{"primary", Json{"reach_area", Json{"point", Json{0.0, 0.0, 0.0}, "radius",
                                                          10.0, "desc", "Land"}}}}}}}},
         77});
    mech3::activate_content(valid);
    const auto world_index = valid->definitions.size() - 3;
    const auto asset_index = valid->definitions.size() - 2;
    auto check = [&](const std::shared_ptr<cc::content::ResolvedContentSet> &invalid,
                     const std::string &field) {
        bool rejected = false;
        try {
            mech3::activate_content(invalid);
        } catch (const cc::content::Error &error) {
            rejected = error.diagnostic.field.find(field) != std::string::npos;
        }
        EXPECT(rejected);
        EXPECT(mech3::active_content() == valid);
    };
    for (const char *pointer : {"/gravity", "/heightfield/gravity", "/instances/0/gravity",
                                "/meshes/0/gravity", "/markers/0/gravity"}) {
        auto invalid = std::make_shared<cc::content::ResolvedContentSet>(*valid);
        auto &world = invalid->definitions[world_index];
        world.data[Json::json_pointer(pointer)] = 0;
        check(invalid, "gravity");
        EXPECT(rejects_world(world, *invalid));
        auto variant = authored_world();
        variant.id = "demo:variant";
        variant.data = {{"base", world.id}};
        EXPECT(rejects_world(variant, *invalid));
    }
    for (const char *pointer : {"/gravity", "/meshes/0/gravity"}) {
        auto invalid = std::make_shared<cc::content::ResolvedContentSet>(*valid);
        invalid->definitions[asset_index].data[Json::json_pointer(pointer)] = 0;
        check(invalid, "gravity");
        auto world = authored_world();
        world.data["instances"] = Json::array({{{"id", "rock"}, {"asset", "demo:rock"}}});
        EXPECT(rejects_world(world, *invalid));
    }
    auto invalid = std::make_shared<cc::content::ResolvedContentSet>(*valid);
    invalid->definitions.back().data["persist_wrold"] = true;
    check(invalid, "persist_wrold");
    for (const char *id : {"stock:c1", "stock:c1m1"}) {
        invalid = std::make_shared<cc::content::ResolvedContentSet>(*valid);
        for (auto &definition : invalid->definitions)
            if (definition.id == id)
                definition.data["gravity"] = 0;
        check(invalid, "gravity");
    }
    for (const Json &reader :
         {Json{{"world.zrd", Json{"m1", Json{"jet_cieling", Json{10.0}}}}},
          Json{{"objectives.zrd",
                Json{"m1", Json{"primary", Json{"reach_area", Json{"point", Json{0.0, 0.0, 0.0},
                                                                   "gravity", Json{0}}}}}}},
          Json{
              {"net_01.zrd", Json{"version", Json{106}, "netparams", Json{"gravity", Json{0}}}}}}) {
        invalid = std::make_shared<cc::content::ResolvedContentSet>(*valid);
        invalid->definitions.back().data["readers"].update(reader);
        bool rejected = false;
        try {
            mech3::activate_content(invalid);
        } catch (const cc::content::Error &) {
            rejected = true;
        }
        EXPECT(rejected && mech3::active_content() == valid);
    }
    auto compiled = cc::content::compile_world(authored_world(), *valid);
    compiled["chunks"][0]["gravity"] = 0;
    bool rejected = false;
    try {
        cc::content::validate_compiled_world(compiled);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
}

TEST(content_world_compiler_creates_new_topology_and_reusable_instances) {
    cc::content::ResolvedContentSet content;
    auto world = authored_world();
    auto compiled = cc::content::compile_world(world, content);
    EXPECT(compiled["chunks"].size() == 1);
    EXPECT(compiled["chunks"][0]["triangles"].size() == 8);
    EXPECT(compiled["digest"] == cc::content::compile_world(world, content)["digest"]);
    content.definitions.push_back(
        {"world_asset",
         "demo:prop",
         "demo",
         "static_mesh",
         {{"meshes", Json::array({{{"vertices", {{0, 0, 0}, {1, 0, 0}, {0, 0, -1}}},
                                   {"triangles", {{0, 1, 2}}}}})}},
         0});
    world.data["instances"] =
        Json::array({{{"id", "first"}, {"asset", "demo:prop"}, {"translation", {4, 2, 0}}},
                     {{"id", "second"}, {"asset", "demo:prop"}, {"translation", {-4, 2, 0}}}});
    compiled = cc::content::compile_world(world, content);
    EXPECT(compiled["chunks"].size() == 3);
    EXPECT(compiled["chunks"][1]["vertices"][0][0] == 4.0);
    EXPECT(compiled["chunks"][2]["vertices"][0][0] == -4.0);
    world.data["instances"][1]["scale"] = {-1.0, 1.0, 1.0};
    const auto mirrored = cc::content::compile_world(world, content)["chunks"][2]["vertices"];
    const auto &a = mirrored[0];
    const auto &b = mirrored[1];
    const auto &c = mirrored[2];
    const double normal_y =
        (b[2].get<double>() - a[2].get<double>()) * (c[0].get<double>() - a[0].get<double>()) -
        (b[0].get<double>() - a[0].get<double>()) * (c[2].get<double>() - a[2].get<double>());
    EXPECT(normal_y > 0);
    compiled["schema"] = uint64_t(4294967297ULL);
    bool rejected = false;
    try {
        cc::content::validate_compiled_world(compiled);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
}

TEST(content_world_variants_resolve_instances_before_compilation) {
    cc::content::ResolvedContentSet content;
    auto base = authored_world();
    content.definitions.push_back(base);
    auto variant = base;
    variant.id = "demo:night";
    variant.data = {{"base", base.id}, {"spawn", {1, 2, 3}}};
    const auto compiled = cc::content::compile_world(variant, content);
    EXPECT(compiled["spawn"] == Json({1, 2, 3}));
    EXPECT(compiled["chunks"].size() == 1);
    variant.data["base"] = "demo:missing";
    EXPECT(rejects_world(variant, content));
}

TEST(content_world_rejects_degenerate_indices_and_unbounded_sources) {
    cc::content::ResolvedContentSet content;
    auto world = authored_world();
    world.data["heightfield"]["heights"] = {{0, 0}, {0}};
    EXPECT(rejects_world(world, content));
    world = authored_world();
    world.data.erase("heightfield");
    world.data["meshes"] = Json::array(
        {{{"vertices", {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}}, {"triangles", {{0, 1, 2}}}}});
    EXPECT(rejects_world(world, content));
    world.data["meshes"][0]["triangles"] = {{0, 1, 100}};
    EXPECT(rejects_world(world, content));
    world = authored_world();
    world.data["instances"] = Json::array({{{"id", "missing"}, {"asset", "other:missing"}}});
    EXPECT(rejects_world(world, content));
}

TEST(content_world_native_ground_and_projectile_queries_use_authored_geometry) {
    const auto previous = mech3::active_content();
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    auto terrain = authored_world();
    terrain.data["heightfield"]["color"] = {0.25, 0.5, 0.75};
    content->definitions.push_back(terrain);
    content->definitions.push_back(
        {"mission",
         "demo:digest",
         "demo",
         "native",
         {{"world", terrain.id}, {"readers", {{"objectives.zrd", Json::array()}}}},
         100});
    mech3::activate_content(content);
    zcls::classes_reset();
    zmth::reset_stack();
    zmod::models() = zmod::ModelPool{};
    zmod::reset_pool();
    const auto selected = mech3::content_selected_mission();
    const auto model_count = zmod::models().count;
    const auto preflight_digest = mech3::content_mission_world_digest("demo:digest");
    EXPECT(!preflight_digest.empty() && mech3::content_selected_mission() == selected);
    EXPECT(zcls::list_find_by_name(zcls::kWorlds, "world1") == nullptr);
    EXPECT(zmod::models().count == model_count);
    const std::string folder = "ccw" + cc::content::sha256("demo:terrain").substr(0, 16);
    const int index = mech3::mission_dir_index(folder.c_str());
    EXPECT(index >= 9);
    zmod::fog_set_on(1); // An unconfigured previous/reset render state must not leak in.
    EXPECT(mech3::load_content_world(index));
    EXPECT(mech3::content_world_digest() == preflight_digest);
    zcls::Node *world = zcls::list_find_by_name(zcls::kWorlds, "world1");
    EXPECT(world != nullptr);
    if (world) {
        const auto *parameters = static_cast<const zcls::World *>(world->data);
        EXPECT(parameters->get<uint32_t>(0x10) == 0);
        EXPECT(parameters->get<float>(0x14) == 0.08f && parameters->get<float>(0x24) > 0.0f);
    }
    EXPECT(zmod::fog().on == 0);
    const auto *camera = zcls::list_find_by_name(zcls::kCameras, "camera1");
    EXPECT(camera != nullptr);
    if (camera) {
        const auto *view = static_cast<const zcls::Camera *>(camera->data);
        EXPECT(view->get<float>(0xe8) > 0.0f && view->get<float>(0xec) > 0.0f);
        EXPECT(std::isfinite(view->get<float>(0x1d4)) && std::isfinite(view->get<float>(0x1d8)));
        EXPECT(view->get<float>(0xd0) == 1.0f && view->get<float>(0xd4) == 1.0f);
    }
    zcls::GroundHits ground{};
    zcls::ground_point(world, 0, 100, 0, &ground);
    EXPECT(ground.count > 0);
    if (ground.count)
        EXPECT(std::abs(ground.hit[0].height - 3.0f) < 0.01f);
    zcls::LineHits line{};
    const float from[] = {0, 20, 0}, to[] = {0, -20, 0};
    zcls::segment_nearest(world, from, to, &line);
    EXPECT(line.total > 0 || line.count > 0);
    zcls::Node *object = zcls::list_find_by_name(zcls::kAllNodes, "cc_0");
    EXPECT(zdec::crater_hooks().restore_node != nullptr);
    if (object) {
        EXPECT(object->model && object->model->polygons);
        if (object->model && object->model->polygons) {
            const auto *material = object->model->polygons[0].material;
            EXPECT(material && material->r == 63.75f && material->g == 127.5f &&
                   material->b == 191.25f);
        }
        const auto *before = object->model;
        zdec::craters().touched.insert(object);
        zdec::craters_reset();
        EXPECT(object->model != before);
        zdec::history_clear();
    }
    EXPECT(!mech3::content_world_digest().empty());
    zcls::classes_reset();
    mech3::clear_content_world();
    EXPECT(zdec::crater_hooks().restore_node == nullptr);
    mech3::activate_content(previous);
}

TEST(content_vehicle_asset_constructs_real_models_and_points_without_a_world_template) {
    const auto previous = mech3::active_content();
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    content->definitions.push_back(authored_world());
    Json markers = Json::array();
    for (int i = 0; i < 4; ++i) {
        char name[20];
        std::snprintf(name, sizeof name, "support%02d", i);
        markers.push_back({{"name", name}, {"position", {double(i & 1), 0.0, double(i >> 1)}}});
    }
    for (int i = 0; i < 12; ++i) {
        char name[20];
        std::snprintf(name, sizeof name, "collide%02d", i);
        markers.push_back(
            {{"name", name}, {"position", {double(i % 3), double(i / 6), double((i / 3) & 1)}}});
    }
    content->definitions.push_back(
        {"world_asset",
         "demo:truck_body",
         "demo",
         "static_mesh",
         {{"meshes",
           Json::array({{{"vertices", {{-1.0, 0.0, -1.0}, {1.0, 0.0, -1.0}, {0.0, 2.0, 1.0}}},
                         {"triangles", {{0, 1, 2}}},
                         {"ground", false},
                         {"collision", true}}})},
          {"markers", markers}},
         -1});
    content->definitions.push_back({"vehicle",
                                    "demo:truck",
                                    "demo",
                                    "mw3:ai-vehicle@1",
                                    {{"name", "assettruck"},
                                     {"label", "Authored Truck"},
                                     {"base", "stock:ttruck"},
                                     {"model_name", "ttruck"},
                                     {"asset", "demo:truck_body"}},
                                    5});
    auto invalid = *content;
    invalid.definitions[invalid.definitions.size() - 2].data["markers"].erase(15);
    bool rejected = false;
    try {
        mech3::validate_content_vehicle_assets(invalid);
    } catch (const cc::content::Error &error) {
        rejected = error.diagnostic.code == "vehicle-geometry";
    }
    EXPECT(rejected);
    mech3::activate_content(content);
    zcls::classes_reset();
    zmth::reset_stack();
    zmod::models() = zmod::ModelPool{};
    zmod::reset_pool();
    const std::string folder = "ccw" + cc::content::sha256("demo:terrain").substr(0, 16);
    EXPECT(mech3::load_content_world(mech3::mission_dir_index(folder.c_str())));
    EXPECT(zcls::list_find_by_name(zcls::kAllNodes, "assettruck_01") == nullptr);
    const auto before = zmod::models().count;
    auto *actor = mech3::create_content_vehicle_node("assettruck", "assettruck_01");
    EXPECT(actor != nullptr && zmod::models().count > before);
    auto *point = actor ? zcls::node_find_in_tree(actor, "support03") : nullptr;
    EXPECT(point && zcls::node_find_in_tree(actor, "collide11"));
    if (point) {
        float x = 0, y = 0, z = 0;
        zcls::object3d_get_translation(point, &x, &y, &z);
        EXPECT(x == 1.0f && y == 0.0f && z == 1.0f);
    }
    const auto used = zmod::models().count;
    zmod::models().count = static_cast<int32_t>(zmod::models().slots.size());
    rejected = false;
    try {
        mech3::create_content_vehicle_node("assettruck", "assettruck_02");
    } catch (const cc::content::Error &error) {
        rejected = error.diagnostic.code == "vehicle-model-limit";
    }
    EXPECT(rejected && zcls::list_find_by_name(zcls::kAllNodes, "assettruck_02") == nullptr);
    zmod::models().count = used;
    zcls::classes_reset();
    mech3::clear_content_world();
    mech3::activate_content(previous);
}

TEST(content_world_mission_composition_serves_real_aiv_nav_and_objective_records) {
    const auto previous = mech3::active_content();
    struct Restore {
        std::shared_ptr<const cc::content::ResolvedContentSet> content;
        ~Restore() {
            mech3::clear_content_mission_selection();
            mech3::activate_content(content);
        }
    } restore{previous};
    auto content = std::make_shared<cc::content::ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    content->definitions.push_back(authored_world());
    cc::content::Definition mission = {
        "mission",
        "demo:landing",
        "demo",
        "native",
        {{"world", "demo:terrain"},
         {"number", 1},
         {"nav_points",
          Json::array({{{"id", "Able"}, {"position", {1.0, 2.0, 3.0}}, {"links", Json::array()}}})},
         {"actors", Json::array({{{"id", "01"},
                                  {"vehicle", "stock:ttruck"},
                                  {"position", {4.0, 5.0, 6.0}},
                                  {"net", 1}}})},
         {"readers",
          {{"objectives.zrd",
            Json{"m1", Json{"primary", Json{"reach_area", Json{"point", Json{0.0, 0.0, 0.0},
                                                               "radius", 10.0, "desc", "Land"}}}}},
           {"net_01.zrd", Json{"version", Json{106}, "name", Json{"net_01"}, "node_00",
                               Json{12, Json{4.0, 5.0, 6.0}, Json{0}, "speed", Json{1, 4.0}}}}}}},
        77};
    content->definitions.push_back(mission);
    auto invalid = *content;
    invalid.definitions.back().data["readers"]["valves.zrd"] = Json{"signal", Json{"value", 0}};
    bool rejected = false;
    try {
        mech3::validate_world_content(invalid);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
    invalid = *content;
    invalid.definitions.back().data["readers"]["objectives.zrd"] =
        Json{"m1", Json{"primary", Json{"node_change", Json{"node", "beacon", "state", "active"}}}};
    rejected = false;
    try {
        mech3::validate_world_content(invalid);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
    mech3::activate_content(content);
    EXPECT(mech3::start_content_mission(mission.id));
    EXPECT(mech3::content_mission_actor_required("ttruck_01"));
    EXPECT(!mech3::content_mission_actor_required("unlisted_01"));
    mech3::ai::AivFile aiv;
    aiv.tree = mech3::content_world_reader("aiv.zrd");
    EXPECT(aiv.record_count() == 2);
    const char *name = nullptr;
    auto *actor = aiv.record(1, &name);
    EXPECT(name && std::string(name) == "ttruck_01");
    EXPECT(actor && actor->type == zrdr::List);
    if (actor) {
        EXPECT(actor->list[1].i == 1);
        EXPECT(actor->list[2].list[1].type == zrdr::Float);
        EXPECT(actor->list[2].list[1].f == 4.0f);
    }
    aiv.unload();
    mech3::ai::NetFile nav(10000, "waynet_1");
    nav.tree = mech3::content_world_reader("waynet_1.zrd");
    nav.parse();
    EXPECT(nav.header.version == 107);
    EXPECT(nav.record_count() == 1);
    const auto *point = nav.node_raw(0, &name);
    EXPECT(point && point->pos[0] == 1.0f && point->pos[1] == 2.0f && point->pos[2] == 3.0f);
    nav.unload();
    mech3::ai::NetFile net(1, "net_01");
    net.tree = mech3::content_world_reader("net_01.zrd");
    net.parse();
    EXPECT(net.header.version == 106);
    EXPECT(net.record_count() == 1);
    const auto *route = net.node_raw(0, &name);
    EXPECT(route && route->speed_on == 1 && route->speed == 4.0f);
    net.unload();
    auto *objectives = mech3::content_world_reader("objectives.zrd");
    struct Hooks final : mech3::ObjectiveHooks {
        const char *message(int32_t) override { return "objective"; }
        const char *localize(const char *name) override { return name; }
        int32_t game_type() override { return 0; }
        zgame::Handle sound_find(const char *) override { return 0; }
    } hooks;
    mech3::Objectives parsed;
    EXPECT(mech3::read_objectives(zrdr::find(objectives, "m1"), parsed, hooks));
    EXPECT(parsed.primary && parsed.primary->items.size() == 1);
    zrdr::free_node(objectives);
    auto legacy = std::make_shared<cc::content::ResolvedContentSet>(*content);
    legacy->definitions.back().data["world"] = "stock:c1";
    mech3::activate_content(legacy);
    EXPECT(mech3::start_content_mission(mission.id));
    EXPECT(!mech3::content_world_digest().empty());
    EXPECT(mech3::content_mission_world_digest(mission.id) == mech3::content_world_digest());
    std::FILE *file = std::tmpfile();
    std::fputs("(\"player\" (0 (0.0 0.0 0.0) 0.0 1) \"original_01\" (1 (1.0 0.0 "
               "0.0) 0.0 2))",
               file);
    std::rewind(file);
    mech3::ai::AivFile merged;
    merged.tree = zrdr::parse_text(file);
    std::fclose(file);
    mech3::compose_world_reader("aiv.zrd", merged.tree);
    EXPECT(merged.record_count() == 3);
    merged.record(1, &name);
    EXPECT(name && std::string(name) == "original_01");
    merged.record(2, &name);
    EXPECT(name && std::string(name) == "ttruck_01");
    rejected = false;
    try {
        mech3::compose_world_reader("aiv.zrd", merged.tree);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
    merged.unload();
    rejected = false;
    try {
        auto *missing = mech3::content_world_reader("aiv.zrd");
        zrdr::free_node(missing);
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    EXPECT(rejected);
    mech3::clear_content_mission_selection();
    EXPECT(mech3::content_world_reader("aiv.zrd") == nullptr);
}

TEST(content_world_asset_markers_follow_placement_and_reject_ambiguous_native_points) {
    cc::content::ResolvedContentSet content;
    cc::content::Definition asset = {
        "world_asset",
        "demo:marked",
        "demo",
        "static_mesh",
        {{"meshes", Json::array({{{"vertices", {{0, 0, 0}, {1, 0, 0}, {0, 0, -1}}},
                                  {"triangles", {{0, 1, 2}}}}})},
         {"markers", Json::array({{{"name", "support00"}, {"position", {1, 2, 3}}}})}},
        0};
    content.definitions.push_back(asset);
    auto world = authored_world();
    const double q = std::sqrt(0.5);
    world.data["instances"] = Json::array({{{"id", "truck_a"},
                                            {"asset", asset.id},
                                            {"translation", {10, 20, 30}},
                                            {"scale", {2, 3, 4}},
                                            {"rotation", {0, q, 0, q}}},
                                           {{"id", "truck_b"}, {"asset", asset.id}}});
    auto compiled = cc::content::compile_world(world, content);
    EXPECT(compiled["markers"].size() == 2);
    const auto point = compiled["markers"][0];
    EXPECT(point["instance"] == "truck_a" && point["name"] == "support00");
    EXPECT(std::abs(point["position"][0].get<double>() - 22.0) < 1e-10);
    EXPECT(std::abs(point["position"][1].get<double>() - 26.0) < 1e-10);
    EXPECT(std::abs(point["position"][2].get<double>() - 28.0) < 1e-10);
    EXPECT(compiled["markers"][1]["position"] == Json({1.0, 2.0, 3.0}));
    auto rejects = [&](const Json &value) {
        try {
            cc::content::validate_compiled_world(value);
        } catch (const cc::content::Error &) {
            return true;
        }
        return false;
    };
    auto invalid = compiled;
    invalid["markers"].push_back(invalid["markers"][0]);
    EXPECT(rejects(invalid));
    invalid = compiled;
    invalid["markers"][0]["instance"] = "missing";
    EXPECT(rejects(invalid));
    invalid = compiled;
    invalid["markers"][0]["unexpected"] = true;
    EXPECT(rejects(invalid));
    invalid = compiled;
    invalid["markers"][0]["position"] = {0, 0, 10000001};
    EXPECT(rejects(invalid));
    content.definitions[0].data["markers"].push_back(asset.data["markers"][0]);
    EXPECT(rejects_world(world, content));
    content.definitions[0] = asset;
    content.definitions[0].data["markers"][0]["name"] = "support/00";
    EXPECT(rejects_world(world, content));
}
