#include "mech3/content_world_readers.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace mech3::world_readers {
using cc::content::fail;
using cc::content::Json;

void validate_tree(const Json &value, size_t &nodes, int depth) {
    if (++nodes > 400000 || depth > 48)
        fail("world-reader-limit", "Authored reader exceeds its size or nesting budget");
    if (value.is_array()) {
        if (value.size() > 100000)
            fail("world-reader-limit", "Authored reader list is too large");
        for (const auto &child : value)
            validate_tree(child, nodes, depth + 1);
    } else if (value.is_string()) {
        const auto &text = value.get_ref<const std::string &>();
        if (text.size() > 65536 || text.find('\0') != std::string::npos)
            fail("world-reader-string", "Reader string is overlong or contains a null byte");
    } else if (value.is_number_unsigned()) {
        if (value.get<uint64_t>() > INT32_MAX)
            fail("world-reader-number", "Reader integer exceeds the native signed range");
    } else if (value.is_number_integer()) {
        const auto n = value.get<int64_t>();
        if (n < INT32_MIN || n > INT32_MAX)
            fail("world-reader-number", "Reader integer exceeds the native signed range");
    } else if (value.is_number_float()) {
        const auto n = value.get<double>();
        if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max())
            fail("world-reader-number", "Reader float must be a finite native float");
    } else
        fail("world-reader-type", "Raw reader trees require arrays, strings, integers or floats; "
                                  "booleans/null are not native reader types");
}

namespace {
const Json *property(const Json &pairs, const std::string &key) {
    for (size_t i = 0; i + 1 < pairs.size(); i += 2)
        if (pairs[i] == key)
            return &pairs[i + 1];
    return nullptr;
}
void key_values(const Json &value, const std::string &where, bool unique = true) {
    if (!value.is_array() || value.size() % 2)
        fail("mission-reader-shape", where + " requires complete key/value pairs");
    std::set<std::string> keys;
    for (size_t i = 0; i < value.size(); i += 2)
        if (!value[i].is_string() || value[i].get<std::string>().empty() ||
            (unique && !keys.insert(value[i].get<std::string>()).second))
            fail("mission-reader-shape", where + " requires unique nonempty string keys");
}
void allowed_keys(const Json &value, const std::set<std::string> &allowed, const std::string &where,
                  bool unique = true) {
    key_values(value, where, unique);
    for (size_t i = 0; i < value.size(); i += 2)
        if (!allowed.count(value[i].get<std::string>()))
            fail("mission-reader-property",
                 "Unsupported native parameter: " + where + "." + value[i].get<std::string>(), {},
                 {}, where + "." + value[i].get<std::string>());
}
void world_parameters(const Json &block) {
    allowed_keys(block,
                 {"fog", "underwater", "jet_ceiling", "global_wind", "sun", "ambient_temperature",
                  "weather", "fluctuate"},
                 "world");
    const std::map<std::string, std::set<std::string>> children = {
        {"fog", {"range", "hwrange", "color", "altitude", "state", "node"}},
        {"underwater", {"range", "hwrange", "color", "altitude"}},
        {"global_wind", {"max_speed", "random_accel", "random_angvel", "velocity"}},
        {"sun", {"orientation", "diffuse", "ambient"}},
        {"weather", {"particles", "type", "color", "wind", "gravity", "alpha_gradient"}},
        {"fluctuate", {"period", "wavelength", "base", "amplitude", "heading"}}};
    for (const auto &child : children)
        if (const auto *value = property(block, child.first))
            allowed_keys(*value, child.second, "world." + child.first);
    if (const auto *weather = property(block, "weather"))
        if (const auto *wind = property(*weather, "wind"))
            allowed_keys(*wind, {"dir", "vel"}, "world.weather.wind");
}
void tuple(const Json &value, size_t minimum, const std::string &where) {
    if (!value.is_array() || value.size() < minimum)
        fail("mission-reader-shape", where + " requires a native value list with at least " +
                                         std::to_string(minimum) + " values");
}
void native_position(const Json &value, const std::string &where) {
    if (!value.is_array() || value.size() != 3)
        fail("mission-reader-shape", where + " requires three coordinates");
    for (const auto &coordinate : value)
        if (!coordinate.is_number_float() || std::abs(coordinate.get<double>()) > 10000000)
            fail("mission-reader-shape",
                 where + " uses native IEEE float words; write decimal coordinates");
}
void validate_objective_group(const Json &group, int depth = 0) {
    if (depth > 32)
        fail("mission-objective-depth", "Objective groups exceed their nesting limit");
    key_values(group, "Objective group", false);
    const std::set<std::string> kinds = {
        "destroy_entity", "node_change",   "reach_area",          "valve_change", "reach_velocity",
        "aim_at_object",  "target_object", "time_limit",          "view_object",  "face_object",
        "heat_warning",   "User_Command",  "joystick_press",      "key_press",    "mouse_press",
        "weapon_fire",    "weapon_select", "weapon_group_select", "obj_list"};
    for (size_t i = 0; i < group.size(); i += 2) {
        const auto kind = group[i].get<std::string>();
        const auto &args = group[i + 1];
        if (kind == "desc" || kind == "sound" || kind == "visible" || kind == "silent")
            continue;
        if (!kinds.count(kind))
            fail("mission-objective-kind", "Unknown required native objective kind: " + kind);
        if (!args.is_array())
            fail("mission-objective-shape", "Objective arguments require a native list");
        const std::map<std::string, std::set<std::string>> extra = {
            {"destroy_entity", {"nav"}},
            {"node_change", {"node", "state", "nav"}},
            {"reach_area", {"radius", "node", "nav", "point"}},
            {"valve_change", {"valve", "value", "nav"}},
            {"reach_velocity", {"test", "velocity"}},
            {"aim_at_object", {"node"}},
            {"target_object", {"entity", "part"}},
            {"time_limit", {}},
            {"view_object", {"entity"}},
            {"face_object", {"entity"}},
            {"heat_warning", {}},
            {"User_Command", {"cmd"}},
            {"joystick_press", {"button"}},
            {"mouse_press", {"button"}},
            {"key_press", {"key"}},
            {"weapon_fire", {"weapon"}},
            {"weapon_select", {"weapon"}},
            {"weapon_group_select", {"group"}},
            {"obj_list", {"list", "nav", "goal_count", "simultaneous"}}};
        std::set<std::string> allowed = {"desc",    "sound",        "visible", "silent",
                                         "salvage", "cond_salvage", "persist", "set_valve"};
        allowed.insert(extra.at(kind).begin(), extra.at(kind).end());
        if (kind != "destroy_entity" && kind != "time_limit")
            allowed_keys(args, allowed, "objective." + kind);
        else
            for (size_t k = 0; k < args.size(); ++k) {
                if (args[k].is_string() && allowed.count(args[k].get<std::string>())) {
                    if (++k == args.size())
                        fail("mission-objective-shape", "Objective metadata has no value");
                } else if ((kind == "time_limit" && !args[k].is_number()) ||
                           (kind == "destroy_entity" && !args[k].is_string()))
                    fail("mission-objective-shape", "Unsupported mixed objective argument");
            }
        if (const auto *salvage = property(args, "salvage"))
            allowed_keys(*salvage, {"mechs", "equipment", "weapons", "ammo", "engines"},
                         "objective.salvage");
        if (const auto *conditions = property(args, "cond_salvage")) {
            allowed_keys(*conditions, {"node_state"}, "objective.cond_salvage", false);
            for (size_t k = 1; k < conditions->size(); k += 2) {
                allowed_keys((*conditions)[k], {"node", "state", "salvage"},
                             "objective.cond_salvage.node_state");
                if (const auto *salvage = property((*conditions)[k], "salvage"))
                    allowed_keys(*salvage, {"mechs", "equipment", "weapons", "ammo", "engines"},
                                 "objective.cond_salvage.salvage");
            }
        }
        for (const char *key : {"node", "weapon", "cmd", "key", "button", "group"})
            if (const auto *value = property(args, key)) {
                tuple(*value, 1, std::string("Objective ") + key);
                if (std::string(key) == "node")
                    for (const auto &name : *value)
                        if (!name.is_string())
                            fail("mission-objective-node",
                                 "Objective node paths require string names");
            }
        if (const auto *value = property(args, "point")) {
            if (!value->is_array() || value->size() != 3)
                fail("mission-objective-point", "Objective point requires three coordinates");
            for (const auto &coordinate : *value)
                if (!coordinate.is_number())
                    fail("mission-objective-point", "Objective coordinates require numbers");
        }
        if (const auto *value = property(args, "set_valve")) {
            tuple(*value, 2, "Objective set_valve");
            if (!(*value)[0].is_string() || !(*value)[1].is_number_integer())
                fail("mission-objective-valve", "Objective set_valve requires a name and integer");
        }
        if (kind == "obj_list") {
            const auto *list = property(args, "list");
            if (!list)
                fail("mission-objective-list", "Nested objective group needs a list");
            validate_objective_group(*list, depth + 1);
        }
    }
}
} // namespace

void validate_document(const std::string &name, const Json &document) {
    size_t nodes = 0;
    validate_tree(document, nodes);
    key_values(document, name);
    if (document.empty())
        return;
    if (name == "world.zrd") {
        for (size_t i = 1; i < document.size(); i += 2)
            world_parameters(document[i]);
    } else if (name == "objectives.zrd") {
        for (size_t i = 1; i < document.size(); i += 2) {
            allowed_keys(document[i], {"desc", "primary", "secondary", "final", "fail"},
                         "objectives");
            for (const char *key : {"primary", "secondary", "final", "fail"})
                if (const auto *group = property(document[i], key))
                    validate_objective_group(*group);
        }
    } else if (name == "aiv.zrd") {
        for (size_t i = 1; i < document.size(); i += 2) {
            const auto &actor = document[i];
            if (!actor.is_array() || actor.size() != 4 || !actor[0].is_number_integer() ||
                !actor[2].is_number_float() || !actor[3].is_number_integer())
                fail("mission-aiv",
                     "Managed AIV records require net, decimal position, decimal yaw and "
                     "team; use semantic actors for additions");
            native_position(actor[1], "AIV position");
        }
    } else if (name == "valves.zrd") {
        const std::map<std::string, size_t> arity = {
            {"value", 1},           {"sound", 2},       {"sound_once", 2},   {"animation", 2},
            {"animation_once", 2},  {"scan_aim", 3},    {"scan_hit", 3},     {"scan_time", 3},
            {"canleave", 3},        {"shutdown", 3},    {"missionreset", 1}, {"missionabort", 1},
            {"missioncomplete", 1}, {"destroy", 2},     {"setteam", 3},      {"attack_strategy", 3},
            {"update", 1},          {"delayupdate", 2}, {"all_nonzero", 0},  {"all_zero", 0},
            {"any_nonzero", 0},     {"any_zero", 0},    {"namelist", 0},     {"teleport", 4}};
        for (size_t i = 1; i < document.size(); i += 2) {
            key_values(document[i], "Valve definition");
            for (size_t k = 0; k < document[i].size(); k += 2) {
                const auto key = document[i][k].get<std::string>();
                const auto found = arity.find(key);
                if (found == arity.end())
                    fail("mission-valve-property",
                         "Unknown required native valve property: " + key);
                tuple(document[i][k + 1], found->second, "Valve " + key);
            }
        }
    } else if (name.rfind("net_", 0) == 0 || name.rfind("waynet_", 0) == 0) {
        const auto *version = property(document, "version");
        if (!version)
            fail("mission-net-version", "Native AI net requires a declared version");
        tuple(*version, 1, "AI net version");
        if (!(*version)[0].is_number_integer() || ((*version)[0] != 106 && (*version)[0] != 107))
            fail("mission-net-version", "Managed AI nets support compiled route106 or waypoint107");
        if (const auto *title = property(document, "name")) {
            tuple(*title, 1, "AI net name");
            if (!(*title)[0].is_string())
                fail("mission-net-name", "AI net name must be a string");
        }
        const std::map<std::string, size_t> arity = {
            {"speed", 2},    {"canleave", 2},     {"wait_interval", 2}, {"wait_time", 2},
            {"valve", 2},    {"name", 1},         {"auxname", 1},       {"jump", 2},
            {"scan_aim", 2}, {"scan_time", 2},    {"scan_hit", 2},      {"hidden", 1},
            {"fixed", 1},    {"autonext", 1},     {"visible", 1},       {"deploy", 5},
            {"mcbwait", 3},  {"arrangenodes", 1}, {"sound", 1},         {"backup", 0},
            {"gate", 1},     {"shunt", 0},        {"valve_assign", 2}};
        std::set<int> indices;
        std::vector<int> links;
        for (size_t i = 0; i < document.size(); i += 2) {
            const auto key = document[i].get<std::string>();
            const auto &value = document[i + 1];
            if (key == "version" || key == "name")
                continue;
            if (key != "netparams" && key.rfind("node_", 0) != 0)
                fail("mission-net-property", "Unknown required AI net record: " + key);
            if (key == "netparams") {
                allowed_keys(value, {"pos"}, "netparams");
                if (const auto *pos = property(value, "pos"))
                    native_position(*pos, "AI net origin");
                continue;
            }
            const auto suffix = key.substr(5);
            if (suffix.empty() || suffix.find_first_not_of("0123456789") != std::string::npos ||
                suffix.size() > 5)
                fail("mission-net-node", "AI node names require bounded decimal indices");
            indices.insert(std::stoi(suffix));
            tuple(value, 3, "AI net node");
            if (value[0] != 12)
                fail("mission-net-node", "Managed AI route nodes require native kind12");
            native_position(value[1], "AI node position");
            if (!value[2].is_array())
                fail("mission-net-links", "AI node links require an array");
            for (const auto &link : value[2]) {
                if (!link.is_number_integer() || link.get<int>() < 0)
                    fail("mission-net-links", "AI node links require nonnegative node indices");
                links.push_back(link.get<int>());
            }
            Json properties = Json::array();
            for (size_t k = 3; k < value.size(); ++k)
                properties.push_back(value[k]);
            key_values(properties, "AI node properties");
            for (size_t k = 0; k < properties.size(); k += 2) {
                const auto property_name = properties[k].get<std::string>();
                const auto found = arity.find(property_name);
                if (found == arity.end())
                    fail("mission-net-property",
                         "Unsupported required AI node property: " + property_name);
                tuple(properties[k + 1], found->second, "AI node " + property_name);
            }
        }
        if (indices.empty() && name.rfind("waynet_", 0) != 0)
            fail("mission-net-empty", "A declared AI route requires at least one node");
        for (const int link : links)
            if (!indices.count(link))
                fail("mission-net-links", "AI node links reference a missing node");
    } else if (name == "mpinfo.zrd") {
        if (const auto *objectives = property(document, "objectives")) {
            allowed_keys(*objectives, {"desc", "primary", "secondary", "final", "fail"},
                         "objectives");
            for (const char *key : {"primary", "secondary", "final", "fail"})
                if (const auto *group = property(*objectives, key))
                    validate_objective_group(*group);
        }
        if (const auto *world = property(document, "world_params"))
            world_parameters(*world);
    } else
        fail("mission-reader-schema",
             "No managed authoring schema is available for nonempty reader: " + name);
}

} // namespace mech3::world_readers
