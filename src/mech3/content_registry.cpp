#include "mech3/content_registry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <set>

#include "crt/msvc_string.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "mech3/content_base.h"
#include "mech3/content_campaign.h"
#include "mech3/content_hud.h"
#include "mech3/content_legacy.h"
#include "mech3/content_playtest.h"
#include "mech3/content_textures.h"
#include "mech3/content_world.h"
#include "mech3/mech_config.h"
#include "mech3/mech_names.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_skin.h"
#include "mech3/weapon.h"
#include "platform/content_visuals.h"

namespace mech3 {
namespace {
using cc::content::Definition;
using cc::content::Json;
using cc::content::ResolvedContentSet;
std::shared_ptr<const ResolvedContentSet> g_content;
std::map<std::string, std::vector<const Definition *>> g_handles;
std::map<std::pair<std::string, std::string>, int32_t> g_names;
std::vector<int32_t> g_equipment_families;
struct LabData {
    WeaponLab lab{};
    int32_t rounds = 0;
};
std::map<int32_t, LabData> g_weapon_labs;
bool g_weapon_labs_ready = false;
const char *const kFamilies[] = {"ams",   "armor", "armorff", "fcs", "bap",
                                 "case",  "c3",    "c3slave", "ecm", "sink",
                                 "dsink", "jjets", "masc",    "tag", "tc"};

std::string lower(std::string value) {
    for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::string stock_id(const char *name) { return "stock:" + lower(name); }
std::string equipment_behavior(int32_t family) {
    return std::string("mw3:equipment/") + kFamilies[family] + "@1";
}
int32_t family_of(const Definition &d) {
    for (int32_t i = 0; i < 15; ++i)
        if (d.behavior == equipment_behavior(i))
            return i;
    return -1;
}
[[noreturn]] void bad(const Definition &d, const std::string &field, const std::string &message) {
    cc::content::fail("native-content", message, d.owner, d.id, field);
}
void fields(const Definition &d, std::initializer_list<const char *> allowed) {
    if (!d.data.is_object())
        bad(d, "data", "Definition data must be an object");
    for (auto it = d.data.begin(); it != d.data.end(); ++it) {
        bool known = false;
        for (const char *key : allowed)
            if (it.key() == key)
                known = true;
        if (!known)
            bad(d, it.key(), "Unsupported native definition field");
    }
}
void number(const Definition &d, const char *key, double minimum, double maximum) {
    if (!d.data.contains(key) || !d.data[key].is_number())
        bad(d, key, "Expected a number");
    const double v = d.data[key].get<double>();
    if (!std::isfinite(v) || v < minimum || v > maximum)
        bad(d, key, "Number outside supported range");
}
void integer(const Definition &d, const char *key, int minimum, int maximum) {
    if (!d.data.contains(key) || !d.data[key].is_number_integer())
        bad(d, key, "Expected an integer");
    number(d, key, minimum, maximum);
}
void name(const Definition &d) {
    if (!d.data.contains("name") || !d.data["name"].is_string())
        bad(d, "name", "Expected a native resource name");
    const std::string n = d.data["name"].get<std::string>();
    if (n.empty() || n.size() > 40 || !std::all_of(n.begin(), n.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_';
        }))
        bad(d, "name", "Native names use 1..40 letters, digits or underscores");
    if (d.data.contains("label") &&
        (!d.data["label"].is_string() || d.data["label"].get<std::string>().size() > 255))
        bad(d, "label", "Label must be a string of at most 255 bytes");
    if (d.id.rfind("stock:", 0) != 0 && !d.data.contains("label"))
        bad(d, "label", "Additional native content needs a display label");
}
void pair(const Definition &d, const char *key, bool integral) {
    const auto at = d.data.find(key);
    if (at == d.data.end() || !at->is_array() || at->size() != 2)
        bad(d, key, "Expected Clan and Inner Sphere values");
    for (const Json &n : *at) {
        if (!n.is_number() || (integral && !n.is_number_integer()) ||
            !std::isfinite(n.get<double>()) || n.get<double>() < 0 ||
            n.get<double>() > (integral ? 78 : 1000))
            bad(d, key, "Invalid per-tech value");
    }
}
void typed_reader(const Definition &d, const Json &value, int depth, size_t &count) {
    if (++count > 100000 || depth > 32)
        bad(d, "reader", "Reader overrides exceed size or nesting limits");
    if (value.is_boolean() || value.is_null() || value.is_binary() || value.is_discarded())
        bad(d, "reader",
            "Reader values must retain integer, float, string, list or keyed-list types");
    if (value.is_number() && (!std::isfinite(value.get<double>()) ||
                              (value.is_number_float() &&
                               std::abs(value.get<double>()) > std::numeric_limits<float>::max()) ||
                              (value.is_number_integer() && (value.get<double>() < INT32_MIN ||
                                                             value.get<double>() > INT32_MAX))))
        bad(d, "reader", "Reader number is outside its native range");
    if (value.is_string() && value.get<std::string>().size() > 4096)
        bad(d, "reader", "Reader string exceeds its limit");
    if (value.is_array() || value.is_object())
        for (const Json &child : value)
            typed_reader(d, child, depth + 1, count);
}
void base(const Definition &d, const ResolvedContentSet &content) {
    std::set<std::string> visited{d.id};
    const Definition *current = &d;
    while (current->data.contains("base")) {
        if (!current->data["base"].is_string())
            bad(d, "base", "Base must be a stable content ID");
        const std::string id = current->data["base"].get<std::string>();
        if (!visited.insert(id).second)
            bad(d, "base", "Cyclic native definition inheritance");
        current = content.find(d.kind, id);
        if (!current)
            bad(d, "base", "Required native base definition is missing");
    }
    if (current->id.rfind("stock:", 0) != 0)
        bad(d, "base", "A native variant must ultimately inherit a stock definition");
}

zrdr::Node clone(const zrdr::Node &in) {
    zrdr::Node out{};
    out.type = in.type;
    if (in.type == zrdr::String) {
        const size_t size = std::strlen(in.s) + 1;
        out.s = static_cast<char *>(std::malloc(size));
        if (!out.s)
            throw std::bad_alloc();
        std::memcpy(out.s, in.s, size);
    } else if (in.type == zrdr::List) {
        out.list = zrdr::alloc_list(in.list[0].i);
        out.list[0].type = zrdr::Int;
        out.list[0].i = in.list[0].i;
        for (int32_t i = 1; i < in.list[0].i; ++i)
            out.list[i] = clone(in.list[i]);
    } else
        out.i = in.i;
    return out;
}
zrdr::Node typed(const Json &value) {
    zrdr::Node out{};
    if (value.is_object() || value.is_array()) {
        const size_t n = value.is_object() ? value.size() * 2 : value.size();
        out.type = zrdr::List;
        out.list = zrdr::alloc_list(static_cast<int32_t>(n + 1));
        out.list[0].type = zrdr::Int;
        out.list[0].i = static_cast<int32_t>(n + 1);
        int32_t at = 1;
        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                out.list[at++] = typed(Json(it.key()));
                out.list[at++] = typed(it.value());
            }
        } else
            for (const Json &child : value)
                out.list[at++] = typed(child);
    } else if (value.is_string()) {
        out.type = zrdr::String;
        const std::string s = value.get<std::string>();
        out.s = static_cast<char *>(std::malloc(s.size() + 1));
        if (!out.s)
            throw std::bad_alloc();
        std::memcpy(out.s, s.c_str(), s.size() + 1);
    } else if (value.is_number_integer())
        out.type = zrdr::Int, out.i = value.get<int32_t>();
    else
        out.type = zrdr::Float, out.f = value.get<float>();
    return out;
}
void put(zrdr::Node &tree, const std::string &key, zrdr::Node value) {
    if (zrdr::Node *found = zrdr::find(&tree, key.c_str())) {
        zrdr::free_contents(found);
        *found = value;
        return;
    }
    const int32_t count = tree.list[0].i;
    zrdr::Node *grown = zrdr::alloc_list(count + 2);
    std::memcpy(grown, tree.list, count * sizeof(zrdr::Node));
    grown[0].i = count + 2;
    grown[count] = typed(Json(key));
    grown[count + 1] = value;
    std::free(tree.list);
    tree.list = grown;
}
bool same_shape(const zrdr::Node &a, const zrdr::Node &b) {
    if (a.type != b.type)
        return false;
    if (a.type != zrdr::List)
        return true;
    if (a.list[0].i != b.list[0].i)
        return false;
    for (int32_t i = 1; i < a.list[0].i; ++i)
        if (!same_shape(a.list[i], b.list[i]))
            return false;
    return true;
}
void overlay(zrdr::Node &tree, const Json &changes, const Definition &d) {
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        zrdr::Node *found = zrdr::find(&tree, it.key().c_str());
        if (!found)
            bad(d, "reader." + it.key(), "Reader override does not name an existing native field");
        if (it.value().is_object()) {
            if (found->type != zrdr::List)
                bad(d, "reader." + it.key(), "Expected a native keyed list");
            overlay(*found, it.value(), d);
        } else {
            zrdr::Node value = typed(it.value());
            if (!same_shape(value, *found)) {
                zrdr::free_contents(&value);
                bad(d, "reader." + it.key(),
                    "Reader override changes a native field's type or arity");
            }
            zrdr::free_contents(found);
            *found = value;
        }
    }
}
zrdr::Node weapon_tree(const Definition &d, zrdr::Node &ballistics) {
    zrdr::Node out{};
    const std::string n = d.data.at("name").get<std::string>();
    zrdr::Node *original = zrdr::find_flat(&ballistics, n.c_str(), 1);
    if (d.data.contains("base")) {
        const Definition *parent = g_content->find("weapon", d.data.at("base").get<std::string>());
        out = weapon_tree(*parent, ballistics);
    } else {
        if (!original) {
            // Some stock BALLISTICS keys differ from their public NAME.
            for (int32_t i = 1; i + 1 < ballistics.list[0].i; i += 2) {
                zrdr::Node *candidate = &ballistics.list[i + 1];
                const char *public_name = zrdr::get_string(candidate, "NAME");
                if (public_name && crt::msvc_stricmp(public_name, n.c_str()) == 0)
                    original = candidate;
            }
        }
        if (!original)
            bad(d, "base", "Native weapon base was not present in weapons.zrd");
        out = clone(*original);
    }
    if (d.data.contains("reader"))
        overlay(out, d.data["reader"], d);
    put(out, "NAME", typed(Json::array({n})));
    return out;
}
} // namespace

std::shared_ptr<const cc::content::ResolvedContentSet> active_content() { return g_content; }
const Definition *content_definition(const char *kind, int32_t handle) {
    const auto entries = g_handles.find(kind);
    return entries != g_handles.end() && handle >= 0 &&
                   static_cast<size_t>(handle) < entries->second.size()
               ? entries->second[handle]
               : nullptr;
}
const char *content_label(const char *kind, int32_t handle) {
    const Definition *d = content_definition(kind, handle);
    if (!d || !d->data.contains("label"))
        return nullptr;
    return d->data["label"].get_ref<const std::string &>().c_str();
}
int32_t content_index(const char *kind, const char *n) {
    if (!g_content || !n)
        return -1;
    const auto entry = g_names.find({kind, lower(n)});
    return entry == g_names.end() ? -1 : entry->second;
}
int32_t chassis_count() {
    return g_content ? static_cast<int32_t>(g_handles["chassis"].size()) : 19;
}
int32_t equipment_count() {
    return g_content ? static_cast<int32_t>(g_handles["equipment"].size()) : 15;
}
int32_t equipment_family(int32_t item) {
    if (g_content)
        return item >= 0 && static_cast<size_t>(item) < g_equipment_families.size()
                   ? g_equipment_families[item]
                   : -1;
    return item >= 0 && item < 15 ? item : -1;
}
bool weapon_is_missile(int32_t item) {
    if (const Definition *d = content_definition("weapon", item))
        return d->data.value("missile", false);
    return (item >= 0x11 && item <= 0x14) || (item >= 0x1a && item <= 0x1f);
}
bool weapon_is_lrm(int32_t item) {
    if (const Definition *d = content_definition("weapon", item))
        return d->data.value("lrm", false);
    return item >= 0x11 && item <= 0x14;
}
const char *chassis_model_name(int32_t mech) {
    if (const Definition *d = content_definition("chassis", mech))
        return d->data["model_name"].get_ref<const std::string &>().c_str();
    return mech_name(mech);
}
const char *content_vehicle_model(const char *native_type) {
    const int32_t index = content_index("vehicle", native_type);
    const Definition *d = content_definition("vehicle", index);
    return d ? d->data.at("model_name").get_ref<const std::string &>().c_str() : native_type;
}
bool content_vehicle_field_base(const char *native_type) {
    const int32_t index = content_index("vehicle", native_type);
    if (const Definition *d = content_definition("vehicle", index))
        return d->behavior == "mw3:field-base@1";
    return native_type && std::strncmp(native_type, "mobilehq", 9) == 0;
}
const char *content_weapon_model_name(const char *native_name) {
    const Definition *d = content_definition("weapon", content_index("weapon", native_name));
    while (d && d->data.contains("base"))
        d = g_content->find("weapon", d->data.at("base").get<std::string>());
    return d ? d->data.at("name").get_ref<const std::string &>().c_str() : native_name;
}

std::vector<Definition> stock_content() {
    // Capture compiled stock values before activation; readers continue to be the
    // authority for proprietary weapon physics and chassis files at load time.
    static const std::vector<Definition> stock = [] {
        std::vector<Definition> out;
        for (int32_t i = 0; i < 19; ++i) {
            Json data = {{"name", mech_name(i)},
                         {"tech", mech_tech(i)},
                         {"mass", max_mass(i)},
                         {"weight_class", weight_class(i)},
                         {"model_name", mech_name(i)}};
            out.push_back({"chassis", stock_id(mech_name(i)), "stock",
                           i == 7 ? "mw3:elemental@1" : "mw3:biped@1", data, i});
        }
        for (int32_t i = 0; i < 15; ++i) {
            int tech = (equip_has_tech(i, 1) ? 1 : 0) | (equip_has_tech(i, 2) ? 2 : 0);
            Json data = {{"name", kFamilies[i]},
                         {"tech", tech},
                         {"weight", {equip_weight(i, 1), equip_weight(i, 2)}},
                         {"slots", {equip_slots(i, 1), equip_slots(i, 2)}}};
            out.push_back(
                {"equipment", stock_id(kFamilies[i]), "stock", equipment_behavior(i), data, i});
        }
        const bool bugfixes = zfix::options().bugs;
        zfix::options().bugs = true;
        for (int32_t i = 0; i < 38; ++i) {
            int tech = (weapon_has_tech(i, 1) ? 1 : 0) | (weapon_has_tech(i, 2) ? 2 : 0);
            Json data = {{"name", weapon_name(i)},
                         {"tech", tech},
                         {"uses_ammo", weapon_uses_ammo(i)},
                         {"missile", weapon_is_missile(i)},
                         {"lrm", weapon_is_lrm(i)}};
            out.push_back({"weapon", stock_id(weapon_name(i)), "stock", "mw3:weapon@1", data, i});
        }
        zfix::options().bugs = bugfixes;
        const char *vehicles[] = {"ttruck", "apc", "bulldog", "striker", "mobilehq"};
        for (int32_t i = 0; i < 5; ++i)
            out.push_back({"vehicle",
                           stock_id(vehicles[i]),
                           "stock",
                           i == 4 ? "mw3:field-base@1" : "mw3:ai-vehicle@1",
                           {{"name", vehicles[i]}, {"model_name", vehicles[i]}},
                           i});
        return out;
    }();
    std::vector<Definition> out = stock;
    append_world_stock_content(out);
    append_campaign_stock_content(out);
    return out;
}
cc::content::Capabilities content_capabilities() {
    cc::content::Capabilities out = {{"content-packages", 3},      {"legacy-file-overlays", 1},
                                     {"equipment-definitions", 1}, {"weapon-definitions", 1},
                                     {"chassis-definitions", 1},   {"skin-definitions", 1}};
    out["vehicle-definitions"] = 1;
    register_hud_capabilities(out);
    register_texture_capabilities(out);
    platform::content::register_capabilities(out);
    for (const auto &capability : world_capabilities())
        out.insert(capability);
    for (const auto &capability : campaign_capabilities())
        out.insert(capability);
    return out;
}
void validate_native_content(const ResolvedContentSet &content) {
    std::set<std::pair<std::string, std::string>> names;
    std::map<std::string, std::set<int32_t>> handles;
    for (const Definition &d : content.definitions) {
        static const std::set<std::string> supported = {
            "equipment", "weapon",      "chassis", "skin",        "vehicle",
            "world",     "world_asset", "mission", "campaign",    "mission_item",
            "hud",       "appearance",  "texture", "legacy_layer"};
        if (!supported.count(d.kind))
            bad(d, "kind", "Content kind is not implemented by this runtime");
        if (d.kind != "equipment" && d.kind != "weapon" && d.kind != "chassis" &&
            d.kind != "skin" && d.kind != "vehicle")
            continue;
        if (d.handle < 0 || !handles[d.kind].insert(d.handle).second)
            bad(d, "handle", "Native session handles must be unique nonnegative values");
        name(d);
        if (d.id.rfind("stock:", 0) == 0 &&
            stock_id(d.data["name"].get_ref<const std::string &>().c_str()) != d.id)
            bad(d, "name", "Stock native names are fixed; change the display label instead");
        if (d.kind != "skin" &&
            !names.insert({d.kind, lower(d.data["name"].get<std::string>())}).second)
            bad(d, "name", "Duplicate native resource name");
        if (d.kind == "equipment") {
            fields(d, {"name", "label", "tech", "weight", "slots"});
            if (family_of(d) < 0)
                bad(d, "behavior", "Equipment behavior is not implemented");
            integer(d, "tech", 0, 3);
            pair(d, "weight", false);
            pair(d, "slots", true);
            const int family = family_of(d);
            if ((family == 1 || family == 2) && d.data["weight"] != Json::array({0.5, 0.5}))
                bad(d, "weight", "Stock armor families count half-ton units");
            if ((family == 1 || family == 2) && d.data["slots"] != Json::array({0, 0}))
                bad(d, "slots", "Stock armor uses the native distributed critical-slot contract");
        } else if (d.kind == "weapon") {
            fields(d, {"name", "label", "base", "tech", "uses_ammo", "missile", "lrm", "reader"});
            if (d.behavior != "mw3:weapon@1")
                bad(d, "behavior", "Weapon behavior is not implemented");
            integer(d, "tech", 0, 3);
            for (const char *key : {"uses_ammo", "missile", "lrm"})
                if (!d.data.contains(key) || !d.data[key].is_boolean())
                    bad(d, key, "Expected a boolean");
            base(d, content);
        } else if (d.kind == "chassis") {
            fields(d, {"name", "label", "base", "tech", "mass", "weight_class", "model_name",
                       "reader", "loadouts"});
            if (d.behavior != "mw3:biped@1" && d.behavior != "mw3:elemental@1")
                bad(d, "behavior", "Chassis body plan is not implemented");
            if (d.behavior == "mw3:elemental@1" && d.id != "stock:elemental")
                bad(d, "behavior",
                    "Additional chassis currently require the stock biped body plan");
            if (d.data.contains("base")) {
                if (!d.data.at("base").is_string())
                    bad(d, "base", "Base must be a stable content ID");
                const Definition *donor =
                    content.find("chassis", d.data.at("base").get<std::string>());
                if (donor && donor->behavior != d.behavior)
                    bad(d, "base", "Chassis donor has a different body plan");
            }
            integer(d, "tech", 1, 3);
            number(d, "mass", 10, 100);
            if (std::fmod(d.data["mass"].get<double>(), 5.0) != 0)
                bad(d, "mass", "Stock structure supports five-ton increments");
            integer(d, "weight_class", 0, 3);
            if (!d.data.contains("model_name") || !d.data["model_name"].is_string())
                bad(d, "model_name", "Expected a model resource name");
            Definition model = d;
            model.data["name"] = d.data["model_name"];
            name(model);
            base(d, content);
            if (d.data.contains("loadouts")) {
                const Json &loadouts = d.data["loadouts"];
                if (!loadouts.is_array() || loadouts.size() > 256)
                    bad(d, "loadouts", "Expected a bounded loadout list");
                std::set<std::string> loadout_names;
                for (const Json &loadout : loadouts) {
                    if (!loadout.is_object() || !loadout.contains("name") ||
                        !loadout["name"].is_string() ||
                        loadout["name"].get<std::string>().empty() ||
                        loadout["name"].get<std::string>().size() > 80 ||
                        !loadout_names.insert(loadout["name"].get<std::string>()).second)
                        bad(d, "loadouts", "Loadout names must be unique nonempty strings");
                    for (auto field = loadout.begin(); field != loadout.end(); ++field)
                        if (field.key() != "name" && field.key() != "base_index" &&
                            field.key() != "weapons" && field.key() != "equipment")
                            bad(d, "loadouts." + field.key(), "Unsupported loadout field");
                    if (!loadout.contains("base_index") ||
                        !loadout["base_index"].is_number_integer() ||
                        loadout["base_index"].get<int64_t>() < 0 ||
                        loadout["base_index"].get<int64_t>() > 255)
                        bad(d, "loadouts.base_index", "Expected a donor loadout index");
                    for (const char *kind : {"weapons", "equipment"}) {
                        if (!loadout.contains(kind))
                            continue;
                        const Json &items = loadout[kind];
                        if (!items.is_array() ||
                            items.size() > (std::string(kind) == "weapons" ? 16 : 78))
                            bad(d, std::string("loadouts.") + kind,
                                "Item list exceeds stock body-plan limits");
                        for (const Json &item : items) {
                            const bool weapon = std::string(kind) == "weapons";
                            if (!item.is_object() || !item.contains("id") ||
                                !item["id"].is_string() ||
                                !content.find(weapon ? "weapon" : "equipment",
                                              item["id"].get<std::string>()))
                                bad(d, "loadouts.id", "A loadout item definition is missing");
                            for (auto field = item.begin(); field != item.end(); ++field) {
                                const std::string key = field.key();
                                if (key == "id")
                                    continue;
                                const bool accepted =
                                    key == "tech" ||
                                    (weapon ? key == "firepoint" || key == "group" ||
                                                  key == "ammo" || key == "ammo_part"
                                            : key == "part" || key == "count");
                                if (!accepted || !field.value().is_number_integer() ||
                                    field.value().get<int64_t>() < (key == "part" ? -1 : 0) ||
                                    field.value().get<int64_t>() > 100000)
                                    bad(d, "loadouts." + key,
                                        "Unsupported or invalid loadout item field");
                            }
                            const int tech = item.value("tech", d.data["tech"].get<int>());
                            if (tech < 1 || tech > 3)
                                bad(d, "loadouts.tech", "Tech must be Clan, Inner Sphere or Any");
                        }
                    }
                }
            }
        } else if (d.kind == "vehicle") {
            fields(d, {"name", "label", "base", "model_name", "reader", "asset"});
            if (d.behavior != "mw3:ai-vehicle@1" && d.behavior != "mw3:field-base@1")
                bad(d, "behavior", "Vehicle controller is not implemented");
            if (d.data.at("name").get<std::string>().find('_') != std::string::npos)
                bad(d, "name",
                    "Vehicle type names cannot contain the native instance-name separator");
            if (!d.data.contains("model_name") || !d.data["model_name"].is_string())
                bad(d, "model_name", "Expected a donor vehicle model name");
            Definition model = d;
            model.data["name"] = d.data["model_name"];
            name(model);
            base(d, content);
            if (d.data.contains("base")) {
                const Definition *donor =
                    content.find("vehicle", d.data.at("base").get<std::string>());
                if (donor && donor->behavior != d.behavior)
                    bad(d, "base", "Vehicle donor uses a different native controller family");
            }
        } else {
            fields(d, {"name", "label", "chassis", "entries", "texture"});
            if (d.behavior != "mw3:skin@1")
                bad(d, "behavior", "Skin behavior is not implemented");
            if (!d.data.contains("chassis") || !d.data["chassis"].is_string() ||
                !content.find("chassis", d.data["chassis"].get<std::string>()))
                bad(d, "chassis", "Skin target chassis is missing");
            if (d.data.contains("entries") == d.data.contains("texture"))
                bad(d, "entries", "Choose explicit slots or one texture for all native slots");
            if (d.data.contains("texture")) {
                if (!d.data["texture"].is_string() ||
                    d.data["texture"].get<std::string>().empty() ||
                    d.data["texture"].get<std::string>().size() > 19)
                    bad(d, "texture", "Expected a native texture name of at most 19 bytes");
            } else {
                if (!d.data["entries"].is_array() || d.data["entries"].empty() ||
                    d.data["entries"].size() > 4096)
                    bad(d, "entries", "Expected a bounded texture-slot list");
                for (const Json &entry : d.data["entries"]) {
                    if (entry.is_string() && !entry.get<std::string>().empty() &&
                        entry.get<std::string>().size() <= 19)
                        continue;
                    if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() ||
                        !entry[1].is_string() || entry[0].get<std::string>().empty() ||
                        entry[0].get<std::string>().size() > 19 ||
                        entry[1].get<std::string>().empty() ||
                        entry[1].get<std::string>().size() > 19)
                        bad(d, "entries",
                            "Texture slots use a native texture name or a pair of names");
                }
            }
            if (d.data["name"] == "damage")
                bad(d, "name", "Additional skins cannot replace the damage-mask contract");
        }
        if (d.data.contains("reader")) {
            if (!d.data["reader"].is_object())
                bad(d, "reader", "Reader overrides must be a keyed object");
            size_t count = 0;
            typed_reader(d, d.data["reader"], 0, count);
        }
    }
    for (const auto &kind : handles) {
        if (!kind.second.empty() &&
            *kind.second.rbegin() != static_cast<int32_t>(kind.second.size() - 1))
            cc::content::fail("native-content", "Native session handles must be contiguous", {}, {},
                              kind.first);
    }
    validate_content_hud(content);
    validate_content_textures(content);
    platform::content::validate(content);
    validate_world_content(content);
    validate_content_vehicle_assets(content);
    validate_campaign_content(content);
    validate_content_legacy(content);
    validate_content_multiplayer_selection(content);
}
void activate_content(std::shared_ptr<const ResolvedContentSet> content) {
    if (content)
        validate_native_content(*content);
    // Cached definitions borrow reader strings and write saved configurations on
    // teardown. Destroy them while the old registry is still alive.
    free_dfns();
    require_content_texture_quiescence();
    unmount_content_legacy();
    clear_content_base_digest_cache();
    g_content = std::move(content);
    g_handles.clear();
    g_names.clear();
    g_equipment_families.clear();
    g_weapon_labs.clear();
    g_weapon_labs_ready = false;
    if (g_content)
        for (const Definition &d : g_content->definitions) {
            if (d.handle < 0)
                continue;
            auto &handles = g_handles[d.kind];
            if (handles.size() <= static_cast<size_t>(d.handle))
                handles.resize(static_cast<size_t>(d.handle) + 1);
            handles[d.handle] = &d;
            g_names[{d.kind, lower(d.id)}] = d.handle;
            if (d.data.contains("name") && d.data.at("name").is_string())
                g_names[{d.kind, lower(d.data.at("name").get<std::string>())}] = d.handle;
        }
    if (g_content) {
        g_equipment_families.resize(g_handles["equipment"].size(), -1);
        for (const auto *d : g_handles["equipment"])
            if (d)
                g_equipment_families[d->handle] = family_of(*d);
    }
    zrdr::set_compose_hook(g_content ? compose_content_reader : nullptr);
    if (g_content)
        register_content_worlds(*g_content);
    else
        clear_content_world_registry();
}

zrdr::Node *compose_content_reader(const char *entry, zrdr::Node *tree) {
    if (!g_content)
        return tree;
    const std::string filename = lower(entry ? entry : "");
    if (!tree)
        tree = content_world_reader(entry);
    if (tree)
        compose_world_reader(entry, tree);
    if (filename == "vehicle.zrd" && tree) {
        zrdr::Node source = clone(*tree);
        std::function<zrdr::Node(const Definition &)> build =
            [&](const Definition &d) -> zrdr::Node {
            zrdr::Node out{};
            if (d.data.contains("base"))
                out = build(*g_content->find("vehicle", d.data.at("base").get<std::string>()));
            else {
                zrdr::Node *original =
                    zrdr::find(&source, d.data.at("name").get_ref<const std::string &>().c_str());
                if (!original)
                    bad(d, "base", "Native vehicle donor is absent from vehicle.zrd");
                out = clone(*original);
            }
            if (d.data.contains("reader"))
                overlay(out, d.data.at("reader"), d);
            return out;
        };
        try {
            for (const Definition *d : g_content->of_kind("vehicle")) {
                if (d->owner == "stock" && !d->data.contains("reader"))
                    continue;
                put(*tree, d->data.at("name").get<std::string>(), build(*d));
            }
        } catch (...) {
            zrdr::free_contents(&source);
            throw;
        }
        zrdr::free_contents(&source);
    }
    if (filename != "weapons.zrd" || !tree)
        return tree;
    zrdr::Node *ballistics = zrdr::find(tree, "BALLISTICS");
    if (!ballistics || ballistics->type != zrdr::List)
        cc::content::fail("native-reader", "weapons.zrd has no BALLISTICS list");
    // Clone every source before writing, so a stock patch cannot accidentally
    // become the implicit base of an unrelated independently added definition.
    zrdr::Node source = clone(*ballistics);
    try {
        for (const Definition *d : g_content->of_kind("weapon")) {
            if (d->owner == "stock" && !d->data.contains("reader"))
                continue;
            zrdr::Node value = weapon_tree(*d, source);
            put(*ballistics, d->data["name"].get<std::string>(), value);
        }
    } catch (...) {
        zrdr::free_contents(&source);
        throw;
    }
    zrdr::free_contents(&source);
    return tree;
}
zrdr::Node *load_content_chassis_reader(int32_t mech) {
    const Definition *d = content_definition("chassis", mech);
    if (!d)
        return nullptr;
    std::vector<const Definition *> lineage{d};
    while (d->data.contains("base")) {
        d = g_content->find("chassis", d->data.at("base").get<std::string>());
        lineage.push_back(d);
    }
    const std::string filename = "dfn_" + d->data.at("name").get<std::string>() + ".zrd";
    zrdr::Node *tree = zrdr::load(filename.c_str());
    if (!tree)
        cc::content::fail("native-reader", "Required base chassis definition is missing", d->owner,
                          d->id, "base");
    try {
        for (auto it = lineage.rbegin(); it != lineage.rend(); ++it)
            if ((*it)->data.contains("reader"))
                overlay(*tree, (*it)->data.at("reader"), **it);
    } catch (...) {
        zrdr::free_node(tree);
        throw;
    }
    return tree;
}
void apply_content_skins(MechDfn &dfn) {
    if (!g_content)
        return;
    const Definition *chassis = content_definition("chassis", dfn.index);
    if (!chassis)
        return;
    for (const Definition *d : g_content->of_kind("skin")) {
        if (d->data.at("chassis").get<std::string>() != chassis->id)
            continue;
        if (!dfn.skins)
            dfn.skins = new SkinSet(0);
        const std::string n = d->data.at("name").get<std::string>();
        if (dfn.skins->named(n.c_str()))
            bad(*d, "name", "Skin name already exists on the chassis");
        Json entries;
        if (d->data.contains("texture")) {
            if (dfn.skins->skins.empty())
                bad(*d, "texture", "Chassis has no base material slots");
            entries = Json::array();
            for (size_t i = 0; i < dfn.skins->skins[0]->entries.size(); ++i)
                entries.push_back(d->data.at("texture"));
        } else
            entries = d->data.at("entries");
        if (!dfn.skins->skins.empty() && entries.size() != dfn.skins->skins[0]->entries.size())
            bad(*d, "entries", "Skin texture slots must match the chassis base skin");
        auto skin = std::make_unique<Skin>(n.c_str(), static_cast<uint32_t>(entries.size()));
        for (const Json &entry : entries) {
            if (entry.is_string())
                skin->add_entry(entry.get_ref<const std::string &>().c_str(), nullptr);
            else
                skin->add_entry(entry[0].get_ref<const std::string &>().c_str(),
                                entry[1].get_ref<const std::string &>().c_str());
        }
        dfn.skins->add(skin.release());
    }
}
void apply_content_loadouts(MechDfn &dfn) {
    const Definition *d = content_definition("chassis", dfn.index);
    if (!d || !d->data.contains("loadouts"))
        return;
    if (!dfn.configs)
        bad(*d, "loadouts", "Donor chassis has no loadouts");
    prepare_content_weapon_labs();
    const size_t source_count = dfn.configs->configs.size();
    for (const Json &loadout : d->data.at("loadouts")) {
        const size_t source = loadout.at("base_index").get<size_t>();
        if (source >= source_count)
            bad(*d, "loadouts.base_index", "Donor loadout does not exist");
        const std::string label = loadout.at("name").get<std::string>();
        if (dfn.configs->find(label.c_str()))
            bad(*d, "loadouts.name", "Loadout name already exists on the donor");
        auto config =
            std::unique_ptr<Config>(Config::copy_of(label.c_str(), *dfn.configs->configs[source]));
        config->mech = dfn.index;
        config->player_made = 0;
        for (auto it = config->items.begin(); it != config->items.end();) {
            const int k = (*it)->kind();
            const int family = k == 3 ? equipment_family((*it)->id()) : -1;
            const bool replace = (loadout.contains("weapons") && (k == 1 || k == 2)) ||
                                 (loadout.contains("equipment") && k == 3 && family != 1 &&
                                  family != 2 && family != 9 && family != 10);
            if (replace) {
                delete *it;
                it = config->items.erase(it);
            } else
                ++it;
        }
        if (loadout.contains("weapons"))
            for (const Json &item : loadout.at("weapons")) {
                const Definition *weapon =
                    g_content->find("weapon", item.at("id").get<std::string>());
                const int32_t tech = item.value("tech", mech_tech(dfn.index));
                if (!weapon_has_tech(weapon->handle, tech))
                    bad(*d, "loadouts.weapons", "Weapon is unavailable for the requested tech");
                const WeaponType *type = weapon_type(weapon_name(weapon->handle));
                if (!type && !content_weapon_lab(weapon->handle))
                    bad(*d, "loadouts.weapons", "Required native weapon lab data is unavailable");
                const int32_t rounds_per_ton =
                    type ? type->rounds_per_ton : content_weapon_rounds(weapon->handle);
                const int32_t fp = item.value("firepoint", 0), group = item.value("group", 0);
                if (!dfn.firepoints || fp < 0 ||
                    static_cast<size_t>(fp) >= dfn.firepoints->size() || group < 0 || group > 4)
                    bad(*d, "loadouts.weapons",
                        "Weapon firepoint or group is outside the donor body plan");
                WeaponItem *mounted = config->add_weapon(weapon->handle, tech);
                mounted->part = fp, mounted->group = group;
                if (item.contains("ammo") && item.at("ammo").get<int32_t>() != 0) {
                    if (!weapon_uses_ammo(weapon->handle))
                        bad(*d, "loadouts.ammo",
                            "Ammunition was assigned to a weapon without ammunition");
                    const int32_t ammo_part = item.value("ammo_part", (*dfn.firepoints)[fp]->part);
                    if (ammo_part < 1 || ammo_part > 8 || rounds_per_ton < 1)
                        bad(*d, "loadouts.ammo",
                            "Ammunition requires a valid body location and native rounds-per-ton "
                            "definition");
                    int32_t left = item.at("ammo").get<int32_t>();
                    while (left > 0) {
                        const int32_t rounds = std::min(left, rounds_per_ton);
                        auto *ammo = new AmmoItem;
                        ammo->weapon = weapon->handle, ammo->count = rounds, ammo->part = ammo_part;
                        config->items.push_back(ammo);
                        left -= rounds;
                    }
                }
            }
        if (loadout.contains("equipment"))
            for (const Json &item : loadout.at("equipment")) {
                const Definition *equipment =
                    g_content->find("equipment", item.at("id").get<std::string>());
                const int32_t tech = item.value("tech", mech_tech(dfn.index));
                if (!equip_has_tech(equipment->handle, tech))
                    bad(*d, "loadouts.equipment",
                        "Equipment is unavailable for the requested tech");
                const int32_t part = item.value("part", -1), count = item.value("count", 1);
                if (part < -1 || part > 8 || count < 1 || count > 78)
                    bad(*d, "loadouts.equipment",
                        "Equipment location or quantity is outside the donor body plan");
                for (int32_t i = 0; i < count; ++i) {
                    EquipmentItem *mounted = config->add_equipment(equipment->handle, tech, 1);
                    if (mounted)
                        mounted->part = part;
                }
            }
        config->recount();
        if (!config->validate(false) || config->weapon_count() > 16 || config->free_slots < 0)
            bad(*d, "loadouts", "Authored loadout violates mass, armor, placement or slot limits");
        for (int32_t part = 0; part < 9; ++part)
            if (config->free_in[part] < 0)
                bad(*d, "loadouts", "Authored loadout exceeds a part's critical slots");
        dfn.configs->configs.push_back(config.release());
        ++dfn.configs->count;
    }
}
void prepare_content_weapon_labs() {
    if (!g_content || g_weapon_labs_ready)
        return;
    std::unique_ptr<zrdr::Node, void (*)(zrdr::Node *)> tree(zrdr::load("weapons.zrd"),
                                                             [](zrdr::Node *node) {
                                                                 if (node)
                                                                     zrdr::free_node(node);
                                                             });
    if (!tree)
        cc::content::fail(
            "native-weapon-lab",
            "Required native weapons.zrd is unavailable before campaign loadout creation");
    auto *ballistics = zrdr::find(tree.get(), "BALLISTICS");
    if (!ballistics || ballistics->type != zrdr::List)
        cc::content::fail("native-weapon-lab", "Required native BALLISTICS table is absent");
    std::map<int32_t, LabData> labs;
    for (int32_t slot = 1; slot + 1 < zrdr::list_count(ballistics); slot += 2) {
        auto &key = ballistics->list[slot];
        auto &value = ballistics->list[slot + 1];
        if (key.type != zrdr::String || value.type != zrdr::List)
            cc::content::fail("native-weapon-lab", "Malformed native weapon entry");
        const auto *native_name = zrdr::get_string(&value, "NAME");
        const int32_t weapon = weapon_index(native_name ? native_name : key.s);
        if (weapon < 0)
            continue;
        auto *mass = zrdr::find(&value, "MASS");
        if (!mass)
            continue;
        LabData data;
        bool present = false;
        const char *tech[2] = {"clan", "sphere"};
        for (int i = 0; i < 2; ++i) {
            float values[2]{};
            if (!zrdr::get_vec2(mass, tech[i], values))
                continue;
            if (!std::isfinite(values[0]) || !std::isfinite(values[1]) || values[0] < 0 ||
                values[0] > 100 || values[1] < 0 || values[1] > 78)
                cc::content::fail("native-weapon-lab",
                                  "Weapon MASS exceeds supported native loadout limits", {}, {},
                                  key.s);
            data.lab.weight[i] = values[0];
            data.lab.slots[i] = static_cast<int32_t>(values[1]);
            present = true;
        }
        zrdr::get_int(mass, "ammo", &data.rounds);
        if (auto *salvo = zrdr::find(&value, "SALVO")) {
            int32_t count = 1, true_count = 1;
            zrdr::get_int(salvo, "count", &count);
            if (!zrdr::get_int(salvo, "truecount", &true_count))
                true_count = count;
            if (count != 0) {
                const int64_t rounds = int64_t(data.rounds) * true_count;
                if (rounds < 0 || rounds > 1000000)
                    cc::content::fail("native-weapon-lab",
                                      "Native ammunition count exceeds supported limits", {}, {},
                                      key.s);
                data.rounds = static_cast<int32_t>(rounds);
            }
        }
        if (data.rounds < 0 || data.rounds > 1000000)
            cc::content::fail("native-weapon-lab", "Invalid native ammunition count", {}, {},
                              key.s);
        data.lab.ammo_weight = data.rounds == 0 ? 0.0f : 1.0f / static_cast<float>(data.rounds);
        if (present)
            labs.emplace(weapon, data);
    }
    g_weapon_labs = std::move(labs);
    g_weapon_labs_ready = true;
}
const WeaponLab *content_weapon_lab(int32_t weapon) {
    if (!g_content || !g_weapon_labs_ready)
        return nullptr;
    const auto lab = g_weapon_labs.find(weapon);
    return lab == g_weapon_labs.end() ? nullptr : &lab->second.lab;
}
int32_t content_weapon_rounds(int32_t weapon) {
    if (!content_weapon_lab(weapon))
        return 0;
    return g_weapon_labs.at(weapon).rounds;
}
std::string content_skin_id(int32_t mech, int32_t index) {
    const MechDfn *dfn = mech_dfn(mech);
    if (!dfn || !dfn->skins || index < 0 || static_cast<size_t>(index) >= dfn->skins->skins.size())
        return {};
    const std::string n = dfn->skins->skins[index]->name;
    const Definition *chassis = content_definition("chassis", mech);
    if (g_content && chassis)
        for (const Definition *d : g_content->of_kind("skin"))
            if (d->data.at("chassis") == chassis->id && d->data.at("name") == n)
                return d->id;
    return "stock:" + lower(chassis_model_name(mech)) + "." + lower(n);
}
int32_t content_skin_index(int32_t mech, const std::string &id) {
    const MechDfn *dfn = mech_dfn(mech);
    if (!dfn || !dfn->skins)
        return -1;
    for (size_t i = 0; i < dfn->skins->skins.size(); ++i)
        if (content_skin_id(mech, static_cast<int32_t>(i)) == id)
            return static_cast<int32_t>(i);
    return -1;
}
const char *content_skin_label(int32_t mech, int32_t index) {
    const std::string id = content_skin_id(mech, index);
    if (!g_content)
        return nullptr;
    const Definition *d = g_content->find("skin", id);
    return d && d->data.contains("label")
               ? d->data.at("label").get_ref<const std::string &>().c_str()
               : nullptr;
}
} // namespace mech3
