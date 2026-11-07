#include "mech3/content_world.h"
#include "mech3/content_world_readers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>

#include "gamez/zclass/camera.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/display.h"
#include "gamez/zclass/light.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zclass/window.h"
#include "gamez/zdeclient/zdec_crater.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zvideo/zvid_format.h"
#include "mech3/content_base.h"
#include "mech3/content_registry.h"
#include "mech3/mech_objective.h"
#include "mech3/mission.h"
#include "mech3/pilot.h"

namespace mech3 {
Objectives &mission_objectives();
namespace {
using cc::content::Definition;
using cc::content::fail;
using cc::content::Json;
std::map<std::string, Json> g_compiled;
std::map<int32_t, std::string> g_worlds;
std::map<zcls::Node *, Json> g_sources;
std::string g_digest, g_selected_mission;
Json g_readers = Json::object();
Json g_legacy_actors = Json::array();
std::map<std::string, zimg::TextureEntry *> g_images;
std::map<std::string, Json> g_vehicle_assets;
uint32_t g_vehicle_node_serial = 0;
std::set<std::string> g_registered_folders;

void fill_reader(zrdr::Node &node, const Json &value, int depth) {
    if (depth > 48)
        fail("world-reader-depth", "Authored reader tree is too deep");
    if (value.is_array()) {
        if (value.size() > 100000)
            fail("world-reader-size", "Authored reader list is too large");
        node.type = zrdr::List;
        node.list = zrdr::alloc_list(static_cast<int32_t>(value.size() + 1));
        for (size_t i = 0; i <= value.size(); ++i) {
            node.list[i].type = zrdr::Int;
            node.list[i].i = 0;
        }
        node.list[0].i = static_cast<int32_t>(value.size() + 1);
        for (size_t i = 0; i < value.size(); ++i)
            fill_reader(node.list[i + 1], value[i], depth + 1);
    } else if (value.is_string()) {
        node.type = zrdr::String;
        const auto text = value.get<std::string>();
        node.s = static_cast<char *>(std::malloc(text.size() + 1));
        if (!node.s)
            throw std::bad_alloc();
        std::memcpy(node.s, text.c_str(), text.size() + 1);
    } else if (value.is_number_integer()) {
        const int64_t n = value.get<int64_t>();
        if (n < INT32_MIN || n > INT32_MAX)
            fail("world-reader-number", "Reader integer exceeds the native range");
        node.type = zrdr::Int;
        node.i = static_cast<int32_t>(n);
    } else if (value.is_number_float()) {
        node.type = zrdr::Float;
        node.f = value.get<float>();
    } else
        fail("world-reader-type", "Readers accept only arrays, strings and finite numbers");
}

zmod::Model *make_model(const Json &chunk) {
    zmod::Material material{};
    zmod::material_init(material);
    const auto color = chunk.value("color", Json{1, 1, 1});
    // Authoring uses normalized colors; the native material/lighting interface
    // stores RGB channels in the same 0..255 units as its polygon colors.
    material.r = color[0].get<float>() * 255.0f;
    material.g = color[1].get<float>() * 255.0f;
    material.b = color[2].get<float>() * 255.0f;
    material.alpha = chunk.value("alpha", 255);
    material.rgb = zvid::pack_rgb(&material.r);
    material.soil = chunk.value("soil", 0);
    const auto texture = chunk.value("texture", std::string{});
    const auto image = chunk.value("image", std::string{});
    if (!image.empty()) {
        const auto found = g_images.find(image);
        if (found == g_images.end())
            fail("world-image", "Compiled material image is unavailable");
        material.texture = found->second;
        material.flags |= 1;
    } else if (!texture.empty()) {
        material.texture = zimg::find_texture(texture.c_str());
        if (!material.texture)
            material.texture = zimg::load_named(texture.c_str(), 0, 0);
        if (!material.texture || material.texture->image == &zimg::default_image())
            fail("world-texture", "World references an unavailable texture: " + texture);
        material.flags |= 1;
    }
    zmod::Material *registered = zmod::material_add(material);
    zmod::Model *model = zmod::model_alloc();
    for (const auto &triangle : chunk["triangles"]) {
        float points[9], uv[6];
        for (size_t k = 0; k < 3; ++k) {
            const size_t v = triangle[k].get<size_t>();
            for (size_t c = 0; c < 3; ++c)
                points[k * 3 + c] = chunk["vertices"][v][c].get<float>();
            for (size_t c = 0; c < 2; ++c)
                uv[k * 2 + c] = chunk["uvs"][v][c].get<float>();
        }
        // Native polygon construction requires an explicit four-byte zone set.
        // One wildcard zone makes authored surfaces visible to every native query.
        const uint8_t zone[4] = {1, 0xff, 0, 0};
        zmod::PolygonAdd add;
        add.points = points;
        add.uvs = uv;
        add.material = registered;
        add.zone = zone;
        if (zmod::model_add_polygon(model, 3, add) != 0)
            fail("world-polygon", "Native engine refused an authored triangle");
    }
    return model;
}

void load_images(const Json &world, bool append = false) {
    if (!append)
        g_images.clear();
    for (auto it = world.at("textures").begin(); it != world.at("textures").end(); ++it) {
        if (g_images.count(it.key()))
            continue;
        const auto &source = it.value();
        auto *entry = zimg::free_texture_entry();
        if (!entry)
            fail("world-texture-limit", "Authored world exceeds the native texture table capacity");
        const auto name = "cc_" + it.key().substr(0, 16);
        std::snprintf(entry->name, sizeof entry->name, "%s", name.c_str());
        auto *image = static_cast<zimg::Texture *>(std::calloc(1, sizeof(zimg::Texture)));
        if (!image)
            throw std::bad_alloc();
        image->width = source.at("width").get<int16_t>();
        image->height = source.at("height").get<int16_t>();
        image->pixel_count = image->width * image->height;
        image->flags = zimg::kTwoBytes | zimg::kPixelsOwned | zimg::kNoAlpha;
        image->pixels = std::malloc(static_cast<size_t>(image->pixel_count) * 2);
        if (!image->pixels) {
            std::free(image);
            throw std::bad_alloc();
        }
        auto *pixels = static_cast<uint16_t *>(image->pixels);
        for (int i = 0; i < image->pixel_count; ++i)
            pixels[i] = source.at("pixels")[static_cast<size_t>(i)].get<uint16_t>();
        if (!source.at("alpha").empty()) {
            image->flags = static_cast<uint8_t>((image->flags & ~zimg::kNoAlpha) | zimg::kAlpha |
                                                zimg::kAlphaOwned);
            image->alpha =
                static_cast<uint8_t *>(std::malloc(static_cast<size_t>(image->pixel_count)));
            if (!image->alpha) {
                zimg::free_texture(image);
                throw std::bad_alloc();
            }
            for (int i = 0; i < image->pixel_count; ++i)
                image->alpha[i] = source["alpha"][static_cast<size_t>(i)].get<uint8_t>();
        }
        zimg::software_prepare(*image);
        entry->image = image;
        entry->group = 0;
        entry->state = 1;
        entry->mip = nullptr;
        entry->surface =
            zvid::create_texture(entry->name, *image, image->flags & zimg::kAlpha, 0, 0);
        g_images[it.key()] = entry;
    }
}

Json floats(const Json &point) {
    Json out = Json::array();
    for (const auto &coordinate : point)
        out.push_back(coordinate.get<double>());
    return out;
}
Json semantic_actors(const Definition &mission, const cc::content::ResolvedContentSet &content) {
    Json actors = Json::array();
    for (const auto &actor : mission.data.value("actors", Json::array())) {
        const auto *vehicle = content.find("vehicle", actor.at("vehicle").get<std::string>());
        if (!vehicle)
            fail("mission-actor", "Mission actor references a missing vehicle");
        actors.push_back(vehicle->data.at("name").get<std::string>() + "_" +
                         actor.at("id").get<std::string>());
        actors.push_back(Json{actor.at("net"), floats(actor.at("position")),
                              actor.value("yaw", 0.0), actor.value("team", 2)});
    }
    return actors;
}
void semantic_navigation(const Definition &mission) {
    if (!mission.data.contains("nav_points"))
        return;
    const auto number = mission.data.value("number", 1);
    const bool multiplayer = mission.data.value("multiplayer", false);
    const std::string name =
        std::string(multiplayer ? "waynet_mp" : "waynet_") + std::to_string(number);
    Json document = {"version", Json{107}, "name", Json{name}};
    std::map<std::string, int> points;
    int index = 0;
    for (const auto &nav : mission.data["nav_points"])
        points[nav.at("id").get<std::string>()] = index++;
    index = 0;
    for (const auto &nav : mission.data["nav_points"]) {
        char node[32];
        std::snprintf(node, sizeof node, "node_%02d", index++);
        Json links = Json::array();
        for (const auto &link : nav.value("links", Json::array()))
            links.push_back(points.at(link.get<std::string>()));
        document.push_back(node);
        document.push_back(Json{12, floats(nav.at("position")), links, "name",
                                Json{nav.value("label", nav.at("id").get<std::string>())},
                                "auxname", Json{nav.at("id")}});
    }
    world_readers::validate_document(name + ".zrd", document);
    g_readers[name + ".zrd"] = document;
}

void readers_for(const Json &world) {
    g_readers = world.value("readers", Json::object());
    const auto content = active_content();
    const Definition *mission = content && !g_selected_mission.empty()
                                    ? content->find("mission", g_selected_mission)
                                    : nullptr;
    if (mission) {
        const Json readers = mission->data.value("readers", Json::object());
        for (auto it = readers.begin(); it != readers.end(); ++it)
            g_readers[it.key()] = it.value();
    }
    const int number = mission ? mission->data.value("number", 1) : 1;
    const bool multiplayer = mission && mission->data.value("multiplayer", false);
    const std::string block = std::string(multiplayer ? "mp" : "m") + std::to_string(number);
    if (!g_readers.contains("aiv.zrd"))
        g_readers["aiv.zrd"] =
            Json{"player", Json{0, floats(world.value("spawn", Json{0, 0, 0})), 0.0, 1}};
    if (multiplayer) {
        const Json spawns = mission->data.at("spawn_points");
        for (size_t i = 0; i < spawns.size(); ++i) {
            g_readers["aiv.zrd"].push_back("remote_" + std::to_string(i));
            g_readers["aiv.zrd"].push_back(Json{0, floats(spawns[i]), 0.0, 1});
        }
    }
    if (mission)
        for (const auto &value : semantic_actors(*mission, *content))
            g_readers["aiv.zrd"].push_back(value);
    if (!g_readers.contains("world.zrd"))
        g_readers["world.zrd"] =
            Json{block, Json{"ambient_temperature", 20.0, "jet_ceiling", 150.0}};
    if (multiplayer && !g_readers.contains("mpinfo.zrd")) {
        auto value_of = [&](const Json &root, const std::string &key) -> Json {
            if (root.is_array())
                for (size_t i = 0; i + 1 < root.size(); i += 2)
                    if (root[i] == key)
                        return root[i + 1];
            return Json::array();
        };
        g_readers["mpinfo.zrd"] =
            Json{"db_name",      mission->data.value("name", mission->id),
                 "world_params", value_of(g_readers["world.zrd"], block),
                 "objectives",   value_of(g_readers.value("objectives.zrd", Json::array()), block)};
    }
    // These mission-local records must not fall through into an unrelated stock
    // operation. Empty lists intentionally mean no authored records of this kind.
    if (mission)
        semantic_navigation(*mission);
    for (const char *entry : {"valves.zrd", "aiTurrets.zrd", "landmines.zrd", "inout.zrd",
                              "location.zrd", "targets.zrd", "traps.zrd"})
        if (!g_readers.contains(entry))
            g_readers[entry] = Json::array();
    for (auto reader = g_readers.begin(); reader != g_readers.end(); ++reader)
        world_readers::validate_document(reader.key(), reader.value());
}
} // namespace

void register_content_worlds(const cc::content::ResolvedContentSet &content) {
    clear_content_world_registry();
    g_worlds.clear();
    g_compiled.clear();
    g_selected_mission.clear();
    g_legacy_actors = Json::array();
    for (const auto *world : content.of_kind("world"))
        if (world->behavior == "legacy-layer") {
            const auto folder = world->data.at("directory").get<std::string>();
            if (mission_dir_index(folder.c_str()) < 0) {
                mod_worlds().push_back(folder);
                g_registered_folders.insert(folder);
            }
        }
    for (const auto *world : content.of_kind("world"))
        if (world->behavior == "authored" || world->behavior == "compiled") {
            const auto folder = "ccw" + cc::content::sha256(world->id).substr(0, 16);
            int index = mission_dir_index(folder.c_str());
            if (index >= 0)
                fail("world-alias", "Managed world alias collides with an existing legacy world");
            index = mission_dir_count();
            mod_worlds().push_back(folder);
            g_registered_folders.insert(folder);
            g_worlds[index] = world->id;
        }
}
void clear_content_world_registry() {
    auto &worlds = mod_worlds();
    worlds.erase(std::remove_if(
                     worlds.begin(), worlds.end(),
                     [](const std::string &name) { return g_registered_folders.count(name) != 0; }),
                 worlds.end());
    g_registered_folders.clear();
    g_worlds.clear();
    g_compiled.clear();
    g_selected_mission.clear();
    clear_content_world();
}
bool content_world_is_authored(int32_t index) { return g_worlds.count(index) != 0; }
int32_t content_world_index(const Definition &world) {
    if (world.behavior == "legacy")
        return world.handle;
    if (world.behavior == "legacy-layer")
        return mission_dir_index(world.data.at("directory").get<std::string>().c_str());
    return mission_dir_index(("ccw" + cc::content::sha256(world.id).substr(0, 16)).c_str());
}
namespace {
std::string authored_world_digest(const Json &world) {
    return cc::content::sha256("mw3-authored-world-v1\n" + world.at("digest").get<std::string>() +
                               "\n" + content_base_digest());
}
std::string legacy_world_digest(const Definition &world,
                                const cc::content::ResolvedContentSet &content) {
    return cc::content::sha256("mw3-native-world-v1\n" + world.id + "\n" + content.session_digest +
                               "\n" + content_base_digest());
}
} // namespace
std::string content_mission_world_digest(const std::string &id) {
    const auto content = active_content();
    const auto *mission = content ? content->find("mission", id) : nullptr;
    if (!mission)
        fail("mission-missing", "World identity requires an available mission", {}, id);
    const auto *world = content->find("world", mission->data.at("world").get<std::string>());
    if (!world)
        fail("world-missing", "Mission world is unavailable", {}, id);
    if (world->behavior == "legacy" || world->behavior == "legacy-layer")
        return legacy_world_digest(*world, *content);
    if (!g_compiled.count(world->id))
        g_compiled[world->id] = cc::content::compile_world(*world, *content);
    const auto &compiled = g_compiled.at(world->id);
    cc::content::validate_compiled_world(compiled);
    return authored_world_digest(compiled);
}
bool load_content_world(int32_t index) {
    const auto it = g_worlds.find(index);
    if (it == g_worlds.end())
        return false;
    if (!g_compiled.count(it->second)) {
        const auto content = active_content();
        const auto *definition = content->find("world", it->second);
        g_compiled[it->second] = cc::content::compile_world(*definition, *content);
    }
    const Json &world = g_compiled.at(it->second);
    cc::content::validate_compiled_world(world);
    g_sources.clear();
    zdec::crater_hooks().restore_node = restore_content_node;
    g_digest = authored_world_digest(world);
    readers_for(world);
    load_images(world);
    for (const auto &chunk : world["chunks"]) {
        const auto texture = chunk.value("texture", std::string{});
        if (texture.empty() || !chunk.value("image", std::string{}).empty())
            continue;
        auto *entry = zimg::find_texture(texture.c_str());
        if (!entry)
            entry = zimg::load_named(texture.c_str(), 0, 0);
        if (!entry || entry->image == &zimg::default_image())
            fail("world-texture", "World references an unavailable native texture: " + texture);
    }
    // The pool must never move after any native node holds a Model pointer.
    zmod::models().size =
        static_cast<int32_t>(std::max<size_t>(1750, world["chunks"].size() + 1024));
    zmod::reset_pool();
    zcls::Node *root = zcls::world_create();
    zcls::node_set_name(root, "world1");
    // Original world scripts supply fog distance/color. Authored worlds need
    // explicit clear defaults before optional native mission fog is applied.
    zcls::world_set_fog_type(root, 0);
    zcls::world_set_fog_color(root, 0.08f, 0.12f, 0.18f);
    zcls::world_set_fog_range(root, 4000.0f, 5000.0f);
    const auto bounds = world["bounds"];
    zcls::world_set_origin(root, bounds[0].get<float>(), bounds[1].get<float>());
    zcls::world_set_size(root, bounds[2].get<float>(), -bounds[3].get<float>());
    zcls::world_make_grid(root, 64.0f, -64.0f);
    size_t i = 0;
    std::map<std::string, zcls::Node *> instances;
    for (const auto &chunk : world["chunks"]) {
        const std::string instance = chunk.value("instance", std::string("terrain"));
        zcls::Node *&parent = instances[instance];
        if (!parent) {
            parent = zcls::object3d_create();
            zcls::node_set_name(parent, instance.c_str());
            parent->set_flags(parent->flags() & ~0x1000000u);
            zcls::world_add_child(root, parent);
        }
        zcls::Node *node = zcls::object3d_create();
        const auto name = "cc_" + std::to_string(i++);
        zcls::node_set_name(node, name.c_str());
        node->set_flags((node->flags() & ~(8u | 16u | 0x1000000u)) | 4u |
                        (chunk.value("ground", true) ? 8u : 0u) |
                        (chunk.value("collision", true) ? 16u : 0u));
        zcls::node_set_model(node, make_model(chunk));
        if (!chunk.value("visible", true)) {
            zcls::object3d_set_opacity(node, 0);
            zcls::object3d_use_opacity(node, 1);
        }
        zcls::update_node(node);
        zcls::object3d_add_child(parent, node);
        g_sources.emplace(node, chunk);
    }
    for (const auto &marker : world.value("markers", Json::array())) {
        const auto instance = marker.at("instance").get<std::string>();
        const auto parent = instances.find(instance);
        if (parent == instances.end())
            fail("world-marker", "Native marker has no geometry parent");
        auto *node = zcls::object3d_create();
        zcls::node_set_name(node, marker.at("name").get_ref<const std::string &>().c_str());
        node->set_flags(node->flags() & ~0x1000000u);
        const auto &position = marker.at("position");
        zcls::object3d_set_translation(node, position[0].get<float>(), position[1].get<float>(),
                                       position[2].get<float>());
        zcls::object3d_add_child(parent->second, node);
        zcls::update_node(node);
    }
    auto *window = zcls::window_create();
    zcls::node_set_name(window, "window1");
    zcls::window_set_size(window, 640, 480);
    auto *display = zcls::node_alloc();
    display->put<int32_t>(0x34, 4);
    display->data = new zcls::Display{};
    zcls::node_set_name(display, "display");
    zcls::push_front(zcls::kDisplays, display);
    zcls::display_set_size(display, 640, 480);
    zcls::display_set_clear_color(display, 20.4f, 30.6f, 45.9f);
    auto *camera = zcls::camera_create();
    zcls::node_set_name(camera, "camera1");
    zcls::camera_set_world(camera, root);
    zcls::camera_set_window(camera, window);
    zcls::camera_set_clip(camera, 0.2f, 5000.0f);
    // The native FOV setter changes zoom factors relative to base angles read
    // from an archive. A generated camera must establish those angles first.
    zcls::camera_set_fov_angles(camera, 1.2f, 0.9f);
    zcls::camera_set_lod_scale(camera, 1.0f);
    const auto spawn = world.value("spawn", Json{0, 10, 0});
    zcls::camera_set_position(camera, spawn[0].get<float>(), spawn[1].get<float>() + 10,
                              spawn[2].get<float>());
    auto *sun = zcls::light_create();
    zcls::node_set_name(sun, "sunlight");
    zcls::light_set_directional(sun, 1);
    zcls::light_set_ambient(sun, 0.4f);
    zcls::light_set_diffuse(sun, 0.8f);
    zcls::light_set_orientation(sun, -0.8f, 0.2f, 0);
    zcls::world_add_light(root, sun);
    // World fog changes live in class data, outside the changed-node list.
    // The archive loader performs the same explicit renderer initialization.
    zcls::world_push_fog(root);
    zcls::frame_update();
    return true;
}
namespace {
Json compile_vehicle_geometry(const Definition &vehicle,
                              const cc::content::ResolvedContentSet &content) {
    if (vehicle.behavior != "mw3:ai-vehicle@1")
        fail("vehicle-asset",
             "Reusable static vehicle assets currently support the native AI vehicle body plan",
             vehicle.owner, vehicle.id);
    const auto asset_id = vehicle.data.at("asset").get<std::string>();
    const auto *asset = content.find("world_asset", asset_id);
    if (!asset || asset->behavior != "static_mesh")
        fail("vehicle-asset", "Vehicle requires an installed static_mesh world asset",
             vehicle.owner, vehicle.id, asset_id);
    Definition source{"world",
                      vehicle.id + "/body",
                      vehicle.owner,
                      "authored",
                      {{"instances", Json::array({{{"id", "vehicle_body"}, {"asset", asset_id}}})}},
                      -1};
    auto geometry = cc::content::compile_world(source, content);
    std::set<std::string> points;
    for (const auto &marker : geometry.value("markers", Json::array()))
        points.insert(marker.at("name").get<std::string>());
    for (const auto &group : {std::make_pair("support", 4), std::make_pair("collide", 12)})
        for (int i = 0; i < group.second; ++i) {
            char name[20];
            std::snprintf(name, sizeof name, "%s%02d", group.first, i);
            if (!points.count(name))
                fail("vehicle-geometry",
                     "Vehicle asset requires four support and twelve collision markers",
                     vehicle.owner, vehicle.id, name);
        }
    return geometry;
}
} // namespace
void validate_content_vehicle_assets(const cc::content::ResolvedContentSet &content) {
    for (const auto *vehicle : content.of_kind("vehicle"))
        if (vehicle->data.contains("asset")) {
            if (!vehicle->data["asset"].is_string())
                fail("vehicle-asset", "Expected a stable world_asset identity", vehicle->owner,
                     vehicle->id, "asset");
            (void)compile_vehicle_geometry(*vehicle, content);
        }
}
zcls::Node *create_content_vehicle_node(const char *vehicle_type, const char *native_name) {
    const auto content = active_content();
    const auto *vehicle = content_definition("vehicle", content_index("vehicle", vehicle_type));
    if (!content || !vehicle || !vehicle->data.contains("asset"))
        return nullptr;
    if (!native_name || !*native_name || std::strlen(native_name) > 35)
        fail("vehicle-name", "Vehicle instance name exceeds native limits", vehicle->owner,
             vehicle->id);
    auto *world = zcls::list_find_by_name(zcls::kWorlds, "world1");
    if (!world)
        fail("vehicle-world", "Vehicle assets require an initialized native world", vehicle->owner,
             vehicle->id);
    if (!g_vehicle_assets.count(vehicle->id))
        g_vehicle_assets[vehicle->id] = compile_vehicle_geometry(*vehicle, *content);
    const auto &geometry = g_vehicle_assets.at(vehicle->id);
    auto &pool = zmod::models();
    if (pool.count < 0 || static_cast<size_t>(pool.count) > pool.slots.size() ||
        geometry.at("chunks").size() > pool.slots.size() - static_cast<size_t>(pool.count))
        fail("vehicle-model-limit",
             "Native model pool has insufficient capacity for this vehicle asset", vehicle->owner,
             vehicle->id);
    // All capacity and source geometry checks complete before creating any node;
    // a live native pool must never grow and invalidate existing model pointers.
    load_images(geometry, true);
    auto *root = zcls::object3d_create();
    zcls::node_set_name(root, native_name);
    root->set_flags(root->flags() & ~0x1000000u);
    for (const auto &chunk : geometry.at("chunks")) {
        auto *node = zcls::object3d_create();
        const auto name = "cv_" + std::to_string(g_vehicle_node_serial++);
        zcls::node_set_name(node, name.c_str());
        node->set_flags((node->flags() & ~(8u | 16u | 0x1000000u)) | 4u |
                        (chunk.value("collision", true) ? 16u : 0u));
        zcls::node_set_model(node, make_model(chunk));
        if (!chunk.value("visible", true)) {
            zcls::object3d_set_opacity(node, 0);
            zcls::object3d_use_opacity(node, 1);
        }
        zcls::object3d_add_child(root, node);
        zcls::update_node(node);
        g_sources.emplace(node, chunk);
    }
    for (const auto &marker : geometry.value("markers", Json::array())) {
        auto *node = zcls::object3d_create();
        zcls::node_set_name(node, marker.at("name").get_ref<const std::string &>().c_str());
        node->set_flags(node->flags() & ~0x1000000u);
        const auto &point = marker.at("position");
        zcls::object3d_set_translation(node, point[0].get<float>(), point[1].get<float>(),
                                       point[2].get<float>());
        zcls::object3d_add_child(root, node);
        zcls::update_node(node);
    }
    zcls::world_add_child(world, root);
    zcls::update_node(root);
    return root;
}
void clear_content_world() {
    if (zdec::crater_hooks().restore_node == restore_content_node)
        zdec::crater_hooks().restore_node = nullptr;
    g_sources.clear();
    g_images.clear();
    g_vehicle_assets.clear();
    g_vehicle_node_serial = 0;
    g_readers = Json::object();
    g_legacy_actors = Json::array();
    g_digest.clear();
}
void validate_content_mission_objectives() {
    const auto content = active_content();
    const auto *mission = content ? content->find("mission", g_selected_mission) : nullptr;
    if (!mission)
        return;
    const auto *world = content->find("world", mission->data.at("world").get<std::string>());
    const auto &objectives = mission_objectives();
    if ((world->behavior == "authored" || world->behavior == "compiled") && !objectives.primary)
        fail("mission-objectives",
             "Authored mission did not construct its required native primary objectives");
    const Json exports = mission->data.value("objective_ids", Json::object());
    for (auto exported = exports.begin(); exported != exports.end(); ++exported) {
        const auto &path = exported.value();
        const auto group = path[0].get<std::string>();
        const Objective *current = group == "primary"     ? objectives.primary.get()
                                   : group == "secondary" ? objectives.secondary.get()
                                   : group == "final"     ? objectives.final.get()
                                                          : objectives.fail.get();
        for (size_t i = 1; i < path.size() && current; ++i) {
            const auto *list = dynamic_cast<const ObjectiveGroup *>(current);
            const auto index = path[i].get<size_t>();
            if (!list || index >= list->items.size())
                fail("mission-objective-ids",
                     "Native objective export has no matching child: " + exported.key());
            auto child = list->items.begin();
            std::advance(child, static_cast<std::ptrdiff_t>(index));
            current = child->get();
        }
        if (!current)
            fail("mission-objective-ids",
                 "Native objective export did not construct its declared group: " + exported.key());
    }
}
void clear_content_mission_selection() {
    g_selected_mission.clear();
    clear_content_world();
}
void refresh_content_mission() {
    if (!g_selected_mission.empty())
        start_content_mission(g_selected_mission);
}
const std::string &content_world_digest() { return g_digest; }
const std::string &content_selected_mission() { return g_selected_mission; }
bool content_mission_actor_required(const char *native_name) {
    const auto content = active_content();
    const auto *mission = content ? content->find("mission", g_selected_mission) : nullptr;
    if (!mission || !native_name)
        return false;
    const auto actors = semantic_actors(*mission, *content);
    for (size_t i = 0; i < actors.size(); i += 2)
        if (actors[i] == native_name)
            return true;
    return false;
}
std::string reader_basename(const char *entry) {
    std::string name = entry ? entry : "";
    std::replace(name.begin(), name.end(), '\\', '/');
    return cc::content::fs::path(name).filename().string();
}
zrdr::Node *content_world_reader(const char *entry) {
    if (g_readers.empty() || !entry)
        return nullptr;
    const std::string name = reader_basename(entry);
    const auto found = g_readers.find(name);
    if (found == g_readers.end()) {
        if (name == "aiv.zrd" && !g_legacy_actors.empty())
            fail("mission-aiv", "Added actors require the selected legacy mission AIV source or an "
                                "explicit AIV reader");
        return nullptr;
    }
    size_t nodes = 0;
    world_readers::validate_tree(Json::array({*found}), nodes);
    auto *root = zrdr::new_nodes(zrdr::Int, 1);
    root->i = 0;
    // A reader document is one outer list containing the authored key/value list,
    // matching the native RdrFile record enumeration contract exactly.
    try {
        fill_reader(*root, Json::array({*found}), 0);
    } catch (...) {
        zrdr::free_node(root);
        throw;
    }
    return root;
}
void compose_world_reader(const char *entry, zrdr::Node *root) {
    if (!root)
        return;
    const auto name = reader_basename(entry);
    if (g_readers.contains(name))
        if (auto *replacement = content_world_reader(entry)) {
            zrdr::free_contents(root);
            *root = *replacement;
            std::free(replacement);
        }
    if (name != "aiv.zrd" || g_legacy_actors.empty())
        return;
    if (root->type != zrdr::List || root->list[0].i != 2 || root->list[1].type != zrdr::List)
        fail("mission-aiv", "Existing legacy AIV document has no native record list");
    auto &document = root->list[1];
    for (size_t i = 0; i < g_legacy_actors.size(); i += 2)
        if (zrdr::find_flat(&document, g_legacy_actors[i].get<std::string>().c_str(), 1))
            fail("mission-actor-collision",
                 "Added actor name already exists in the selected native mission");
    zrdr::Node added{};
    added.type = zrdr::Int;
    try {
        fill_reader(added, g_legacy_actors, 0);
        const int count = document.list[0].i, extra = added.list[0].i - 1;
        auto *grown = zrdr::alloc_list(count + extra);
        if (!grown)
            throw std::bad_alloc();
        std::memcpy(grown, document.list, size_t(count) * sizeof(zrdr::Node));
        std::memcpy(grown + count, added.list + 1, size_t(extra) * sizeof(zrdr::Node));
        grown[0].i = count + extra;
        std::free(added.list);
        added.type = zrdr::Int;
        added.i = 0;
        std::free(document.list);
        document.list = grown;
    } catch (...) {
        zrdr::free_contents(&added);
        throw;
    }
}
bool start_content_mission(const std::string &id) {
    const auto content = active_content();
    const Definition *mission = content ? content->find("mission", id) : nullptr;
    if (!mission)
        return false;
    g_selected_mission = id;
    g_legacy_actors = Json::array();
    const auto *world = content->find("world", mission->data.at("world").get<std::string>());
    if (world && (world->behavior == "legacy" || world->behavior == "legacy-layer")) {
        g_readers = mission->data.value("readers", Json::object());
        semantic_navigation(*mission);
        g_legacy_actors = semantic_actors(*mission, *content);
        world_readers::validate_document("aiv.zrd", g_legacy_actors);
        g_digest = legacy_world_digest(*world, *content);
    } else if (world) {
        if (!g_compiled.count(world->id))
            g_compiled[world->id] = cc::content::compile_world(*world, *content);
        readers_for(g_compiled.at(world->id));
    }
    return true;
}
std::vector<std::pair<std::string, int32_t>> content_multiplayer_entries() {
    std::vector<std::pair<std::string, int32_t>> entries;
    const auto content = active_content();
    if (content)
        for (const auto *mission : content->of_kind("mission"))
            if (mission->data.value("multiplayer", false)) {
                if (mission->handle < 0 || mission->handle >= 0x40000000)
                    fail("multiplayer-mission", "Mission handle exceeds managed selection range");
                entries.emplace_back(mission->data.value("name", mission->id),
                                     0x40000000 | mission->handle);
            }
    return entries;
}
bool select_content_multiplayer(int32_t value, int32_t &world, int32_t &number) {
    if (value < 0 || (value & 0x40000000) == 0)
        return false;
    const auto content = active_content();
    const auto *mission = content ? content->by_handle("mission", value & 0x3fffffff) : nullptr;
    if (!mission || !mission->data.value("multiplayer", false))
        fail("multiplayer-mission",
             "Managed multiplayer mission is missing from this content session");
    const auto *definition = content->find("world", mission->data.at("world").get<std::string>());
    world = content_world_index(*definition);
    if (world < 0)
        fail("multiplayer-world", "Managed multiplayer world is unavailable");
    number = mission->data.value("number", 1);
    start_content_mission(mission->id);
    return true;
}
bool restore_content_node(zcls::Node *node) {
    const auto found = g_sources.find(node);
    if (found == g_sources.end())
        return false;
    zmod::Model *previous = node->model;
    zcls::node_set_model(node, make_model(found->second));
    if (previous)
        zmod::model_release(previous);
    zcls::update_node(node);
    return true;
}
} // namespace mech3
