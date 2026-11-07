#include "mech3/content_world.h"
#include "mech3/content_world_readers.h"
#include "mech3/mission.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

namespace mech3 {
using cc::content::Definition;
using cc::content::fail;
using cc::content::Json;
namespace {
void fields(const Definition &definition, const Json &object,
            std::initializer_list<const char *> allowed, const std::string &path) {
    if (!object.is_object())
        fail("mission-schema", "Expected an object", definition.owner, definition.id, path);
    for (const auto &field : object.items())
        if (std::none_of(allowed.begin(), allowed.end(),
                         [&](const char *key) { return field.key() == key; }))
            fail("unsupported-mission-field", "Unsupported native mission parameter",
                 definition.owner, definition.id, path + "." + field.key());
}
} // namespace
void append_world_stock_content(std::vector<Definition> &definitions) {
    const char *worlds[] = {"c1", "c2", "c3", "c4", "c4b", "t1", "lab", "dev1", "dev2"};
    for (int i = 0; i < 9; ++i)
        definitions.push_back({"world",
                               std::string("stock:") + worlds[i],
                               "stock",
                               "legacy",
                               {{"directory", worlds[i]}, {"name", worlds[i]}},
                               i});
    for (int op = 1; op <= 4; ++op)
        for (int n = 1; n <= (op < 3 ? 4 : 6); ++n) {
            const std::string name = "c" + std::to_string(op) + "m" + std::to_string(n);
            const std::string world = op == 4 && n > 3 ? "c4b" : "c" + std::to_string(op);
            definitions.push_back({"mission",
                                   "stock:" + name,
                                   "stock",
                                   "native",
                                   {{"name", name},
                                    {"world", "stock:" + world},
                                    {"number", n},
                                    {"operation", op},
                                    {"persist_world", n != 1 && !(op == 4 && n == 4)},
                                    {"persistence_group", "stock:" + world},
                                    {"legacy", true}},
                                   -1});
        }
}
cc::content::Capabilities world_capabilities() {
    return {{"world.authored", 1},     {"world.compiled", 1},          {"world.legacy", 1},
            {"world.legacy-layer", 1}, {"world_asset.static_mesh", 1}, {"mission.native", 1},
            {"world.markers", 1},      {"world.hierarchy", 1},         {"vehicle-assets", 1}};
}
void validate_world_content(const cc::content::ResolvedContentSet &content) {
    std::set<std::string> legacy_directories;
    for (const auto *asset : content.of_kind("world_asset")) {
        cc::content::validate_world_fields(*asset);
        if (asset->behavior != "static_mesh")
            fail("world-asset-behavior", "Unknown required reusable world asset behavior",
                 asset->owner, asset->id);
        if (!asset->data.contains("source") &&
            (!asset->data.contains("meshes") || !asset->data["meshes"].is_array() ||
             asset->data["meshes"].empty()))
            fail("world-asset",
                 "Reusable world asset requires a source scene or nonempty inline "
                 "meshes",
                 asset->owner, asset->id);
    }
    for (const auto *world : content.of_kind("world")) {
        cc::content::validate_world_fields(*world);
        if (world->behavior == "authored") {
            if (world->data.contains("base") &&
                !content.find("world", world->data["base"].get<std::string>()))
                fail("world-base", "World variant requires an installed base", world->owner,
                     world->id);
            for (const auto &instance : world->data.value("instances", Json::array()))
                if (!instance.value("remove", false) && instance.contains("asset") &&
                    !content.find("world_asset", instance["asset"].get<std::string>()))
                    fail("world-asset", "World instance references a missing asset", world->owner,
                         world->id);
        } else if (world->behavior == "compiled") {
            content.resource(world->owner, world->data.at("resource").get<std::string>());
        } else if (world->behavior == "legacy-layer") {
            const auto directory = world->data.value("directory", std::string{});
            if (directory.empty() || directory.size() > 35 ||
                directory.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN"
                                            "OPQRSTUVWXYZ0123456789_-") != std::string::npos)
                fail("world-directory",
                     "Legacy layer world requires a simple native directory name");
            std::string canonical = directory;
            std::transform(canonical.begin(), canonical.end(), canonical.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!legacy_directories.insert(canonical).second)
                fail("world-directory",
                     "Legacy layers cannot register the same native directory twice");
            for (int i = 0; i < 9; ++i)
                if (canonical == mission_dir(i))
                    fail("world-directory", "Legacy added worlds cannot reuse a stock "
                                            "native directory; use a resource overlay");
            if (world->data.size() != 3 || !world->data.contains("name") ||
                !world->data["name"].is_string() ||
                world->data.value("layer", std::string{}) != world->owner)
                fail("world-layer", "Legacy world metadata requires exactly directory, "
                                    "name and its owning layer");
            bool archive = false;
            for (const auto *layer : content.of_kind("legacy_layer"))
                if (layer->owner == world->owner && layer->behavior == "file-overlay-v1")
                    for (const auto &claim : layer->data.value("files", Json::array()))
                        if (claim.value("path", std::string{}) == "zbd/" + canonical + "/gamez.zbd")
                            archive = true;
            if (!archive)
                fail("world-layer", "Legacy added world must be backed by its owning "
                                    "layer's declared gamez.zbd claim");
        } else if (world->behavior != "legacy" || world->owner != "stock")
            fail("world-behavior", "Unknown required world behavior", world->owner, world->id);
    }
    for (const auto *mission : content.of_kind("mission")) {
        fields(*mission, mission->data,
               {"name", "description", "world", "number", "operation", "selectable", "multiplayer",
                "spawn_points", "readers", "objective_ids", "export_valves", "actors", "nav_points",
                "persist_world", "persistence_group", "legacy"},
               "data");
        const auto world = mission->data.value("world", std::string{});
        if (mission->behavior != "native" || !content.find("world", world))
            fail("mission-world", "Mission references an unavailable world", mission->owner,
                 mission->id);
        if (mission->data.contains("persist_world") && !mission->data["persist_world"].is_boolean())
            fail("mission-persistence", "persist_world must be boolean");
        if (mission->data.contains("persistence_group") &&
            (!mission->data["persistence_group"].is_string() ||
             !cc::content::valid_content_id(mission->data["persistence_group"].get<std::string>())))
            fail("mission-persistence",
                 "persistence_group must be an explicit namespaced group identity");
        if (mission->data.contains("number") && (!mission->data["number"].is_number_integer() ||
                                                 mission->data["number"].get<int64_t>() < 1 ||
                                                 mission->data["number"].get<int64_t>() > 999))
            fail("mission-number", "Mission block number requires an integer from one to999");
        const int number = mission->data.value("number", 1);
        if (number < 1 || number > 999)
            fail("mission-number", "Mission block number must be from one to 999");
        const auto *world_definition = content.find("world", world);
        if (mission->data.value("multiplayer", false) && world_definition->behavior != "legacy" &&
            world_definition->behavior != "legacy-layer") {
            const auto spawns = mission->data.value("spawn_points", Json::array());
            if (!spawns.is_array() || spawns.size() != 16)
                fail("mission-spawns",
                     "Native multiplayer missions require sixteen spawn positions");
            for (const auto &spawn : spawns) {
                if (!spawn.is_array() || spawn.size() != 3)
                    fail("mission-spawns", "Multiplayer spawn position requires three coordinates");
                for (const auto &n : spawn)
                    if (!n.is_number() || !std::isfinite(n.get<double>()) ||
                        std::abs(n.get<double>()) > 10000000)
                        fail("mission-spawns", "Multiplayer spawn coordinates must be finite");
            }
        }
        if (world_definition->behavior != "legacy" &&
            world_definition->behavior != "legacy-layer" &&
            !mission->data.value("readers", Json::object()).contains("objectives.zrd"))
            fail("mission-objectives", "Authored missions must declare native objective data",
                 mission->owner, mission->id);
        const Json readers = mission->data.value("readers", Json::object());
        if (!readers.is_object())
            fail("mission-readers", "Mission readers must be a keyed document set");
        for (auto reader = readers.begin(); reader != readers.end(); ++reader) {
            if (reader.key().find_first_of("/\\:") != std::string::npos ||
                cc::content::fs::path(reader.key()).extension() != ".zrd" ||
                !reader.value().is_array())
                fail("mission-reader-name",
                     "Mission reader needs a native .zrd basename and a key/value list");
            world_readers::validate_document(reader.key(), reader.value());
        }
        const Json exports = mission->data.value("objective_ids", Json::object());
        if (!exports.is_object() || exports.size() > 1024)
            fail("mission-objective-ids", "Objective exports require a bounded identity map");
        for (auto exported = exports.begin(); exported != exports.end(); ++exported) {
            const auto &path = exported.value();
            if (exported.key().empty() || !path.is_array() || path.empty() || path.size() > 16 ||
                !path[0].is_string())
                fail("mission-objective-ids",
                     "Objective export requires a group and optional child indices");
            const auto group = path[0].get<std::string>();
            if (group != "primary" && group != "secondary" && group != "final" && group != "fail")
                fail("mission-objective-ids", "Unknown exported objective group");
            for (size_t i = 1; i < path.size(); ++i)
                if (!path[i].is_number_integer() || path[i].get<int64_t>() < 0 ||
                    path[i].get<int64_t>() > 100000)
                    fail("mission-objective-ids",
                         "Objective export indices must be bounded nonnegative integers");
        }
        const Json exported_valves = mission->data.value("export_valves", Json::array());
        std::set<std::string> valve_ids;
        if (!exported_valves.is_array() || exported_valves.size() > 1024)
            fail("mission-valve-ids", "Valve exports require a bounded list");
        for (const auto &valve : exported_valves)
            if (!valve.is_string() || valve.get<std::string>().empty() ||
                !valve_ids.insert(valve.get<std::string>()).second)
                fail("mission-valve-ids", "Valve exports require unique nonempty names");
        const Json actors = mission->data.value("actors", Json::array());
        if (!actors.is_array() || actors.size() > 1024)
            fail("mission-actors", "Mission actor count exceeds the supported range");
        std::set<std::string> names;
        for (const auto &actor : actors) {
            fields(*mission, actor, {"vehicle", "id", "net", "position", "yaw", "team"},
                   "data.actors");
            const auto *vehicle = content.find("vehicle", actor.value("vehicle", std::string{}));
            const auto id = actor.value("id", std::string{});
            if (!vehicle || id.empty() || id.size() > 16 || !names.insert(id).second)
                fail("mission-actor",
                     "Actor needs a unique bounded identity and an installed vehicle",
                     mission->owner, mission->id);
            if (vehicle && vehicle->data.value("name", std::string{}).size() + 1 + id.size() > 35)
                fail("mission-actor", "Native actor name exceeds 35 bytes");
            if (!actor.contains("position") || !actor["position"].is_array() ||
                actor["position"].size() != 3)
                fail("mission-actor", "Actor needs an explicit position");
            for (const auto &coordinate : actor["position"])
                if (!coordinate.is_number() || !std::isfinite(coordinate.get<double>()) ||
                    std::abs(coordinate.get<double>()) > 10000000)
                    fail("mission-actor", "Actor position must be finite");
            for (const char *key : {"net", "team"})
                if (actor.contains(key) &&
                    (!actor[key].is_number_integer() || actor[key].get<int64_t>() < 0 ||
                     actor[key].get<int64_t>() > INT32_MAX))
                    fail("mission-actor", "Actor net and team require bounded native integers");
            if (actor.contains("yaw") &&
                (!actor["yaw"].is_number() || !std::isfinite(actor["yaw"].get<double>())))
                fail("mission-actor", "Actor yaw must be finite");
            if (actor.value("team", 2) < 0 || actor.value("team", 2) > 31 ||
                actor.value("net", 0) < 1)
                fail("mission-actor", "AI vehicle actors require an implemented "
                                      "positive native AI net and a valid team");
            const int net = actor.at("net").get<int>();
            char reader[32];
            std::snprintf(reader, sizeof reader, "net_%02d.zrd", net);
            if (net > 99 || !mission->data.value("readers", Json::object()).contains(reader))
                fail("mission-actor", "AI vehicle actor requires a declared native net "
                                      "reader from one to 99");
        }
        const Json navs = mission->data.value("nav_points", Json::array());
        std::set<std::string> nav_ids;
        if (!navs.is_array() || navs.size() > 1024)
            fail("mission-nav", "Mission navigation exceeds its supported point count");
        for (const auto &nav : navs) {
            fields(*mission, nav, {"id", "position", "links", "label"}, "data.nav_points");
            if (!nav_ids.insert(nav.at("id").get<std::string>()).second ||
                !nav.at("position").is_array() || nav["position"].size() != 3)
                fail("mission-nav", "Navigation needs unique identities and "
                                    "three-coordinate positions");
            for (const auto &coordinate : nav["position"])
                if (!coordinate.is_number() || !std::isfinite(coordinate.get<double>()) ||
                    std::abs(coordinate.get<double>()) > 10000000)
                    fail("mission-nav", "Navigation positions must be finite");
        }
        for (const auto &nav : navs)
            for (const auto &link : nav.value("links", Json::array()))
                if (!nav_ids.count(link.get<std::string>()))
                    fail("mission-nav", "Navigation link targets an unknown point");
    }
}

} // namespace mech3
