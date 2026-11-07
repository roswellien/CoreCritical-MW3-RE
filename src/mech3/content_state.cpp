#include "mech3/content_state.h"
#include "mech3/content_base.h"
#include "mech3/content_network.h"
#include "mech3/content_registry.h"
#include "mech3/content_world.h"
#include "mech3/mech_config.h"
#include "mech3/mech_names.h"
#include "mech3/pilot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace mech3 {
using namespace cc::content;
namespace {
const Definition *by_handle(const char *kind, int32_t handle) {
    if (auto content = active_content())
        return content->by_handle(kind, handle);
    static const auto stock = stock_content();
    for (const auto &item : stock)
        if (item.kind == kind && item.handle == handle)
            return &item;
    return nullptr;
}
const Definition *by_id(const char *kind, const std::string &id) {
    if (auto content = active_content())
        return content->find(kind, id);
    static const auto stock = stock_content();
    for (const auto &item : stock)
        if (item.kind == kind && item.id == id)
            return &item;
    return nullptr;
}
std::string identity(const char *kind, int32_t handle) {
    auto *value = by_handle(kind, handle);
    if (!value)
        fail("save-identity", "Cannot save an unknown content handle", {}, {}, kind);
    return value->id;
}
int32_t handle(const char *kind, const Json &value) {
    if (!value.is_string())
        fail("save-identity", "Expected a stable content identity", {}, {}, kind);
    auto id = value.get<std::string>();
    auto *definition = by_id(kind, id);
    if (!definition)
        fail("missing-content", "Saved content is unavailable", {}, id, kind);
    return definition->handle;
}
int integer(const Json &object, const char *name, int minimum, int maximum) {
    if (!object.contains(name) || !object.at(name).is_number_integer())
        fail("save-field", "Expected integer", {}, {}, name);
    const auto &value = object.at(name);
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        if (number > static_cast<uint64_t>(maximum))
            fail("save-field", "Saved integer is outside supported range", {}, {}, name);
        return static_cast<int>(number);
    }
    const auto number = value.get<int64_t>();
    if (number < minimum || number > maximum)
        fail("save-field", "Saved integer is outside supported range", {}, {}, name);
    return static_cast<int>(number);
}
float real(const Json &object, const char *name) {
    double value = object.at(name).get<double>();
    if (!std::isfinite(value) || std::abs(value) > 1e9)
        fail("save-field", "Invalid finite saved number", {}, {}, name);
    return static_cast<float>(value);
}
const char *item_kind(int kind) {
    switch (kind) {
    case 0:
        return "chassis";
    case 1:
    case 2:
        return "weapon";
    case 3:
        return "equipment";
    default:
        return nullptr;
    }
}
Json read_record(std::FILE *input, const char *expected, uint32_t maximum) {
    char magic[8]{};
    uint8_t size[4]{};
    if (!input || std::fread(magic, 1, 8, input) != 8 || std::memcmp(magic, expected, 8) != 0 ||
        std::fread(size, 1, 4, input) != 4)
        fail("save-record", "Missing or unsupported managed semantic record");
    const uint32_t length = uint32_t(size[0]) | (uint32_t(size[1]) << 8) |
                            (uint32_t(size[2]) << 16) | (uint32_t(size[3]) << 24);
    if (length > maximum)
        fail("save-limit", "Managed semantic record exceeds its limit");
    std::string text(length, '\0');
    if (std::fread(text.data(), 1, length, input) != length)
        fail("save-record", "Truncated managed semantic record");
    return parse_json(text, "managed semantic record");
}
uint32_t write_record(std::FILE *output, const char *magic, const Json &state, uint32_t maximum) {
    const auto text = state.dump();
    if (text.size() > maximum)
        fail("save-limit", "Managed semantic record exceeds its limit");
    const auto length = static_cast<uint32_t>(text.size());
    if (output) {
        const uint8_t size[4] = {uint8_t(length), uint8_t(length >> 8), uint8_t(length >> 16),
                                 uint8_t(length >> 24)};
        if (std::fwrite(magic, 1, 8, output) != 8 || std::fwrite(size, 1, 4, output) != 4 ||
            std::fwrite(text.data(), 1, length, output) != length)
            fail("save-write", "Managed semantic record write failed");
    }
    return length + 12;
}
} // namespace
Json item_state(const ConfigItem &item) {
    Json result = {
        {"kind", item.kind()}, {"count", item.count}, {"part", item.part}, {"group", item.group}};
    result["flags"] = item.b10;
    if (const char *kind = item_kind(item.kind()))
        result["id"] = identity(kind, item.id());
    else if (item.kind() == 4)
        result["engine"] = item.id();
    else
        fail("save-item", "Unsupported item kind");
    if (item.kind() == 1)
        result["tech"] = static_cast<const WeaponItem &>(item).tech;
    if (item.kind() == 3)
        result["tech"] = static_cast<const EquipmentItem &>(item).tech;
    if (item.kind() == 4)
        result["xl"] = static_cast<const EngineItem &>(item).xl;
    return result;
}
std::unique_ptr<ConfigItem> item_from_state(const Json &state) {
    int kind = integer(state, "kind", 0, 4);
    std::unique_ptr<ConfigItem> result;
    switch (kind) {
    case 0:
        result = std::make_unique<BaseItem>();
        break;
    case 1: {
        auto item = std::make_unique<WeaponItem>();
        item->tech = integer(state, "tech", 1, 3);
        result = std::move(item);
        break;
    }
    case 2:
        result = std::make_unique<AmmoItem>();
        break;
    case 3: {
        auto item = std::make_unique<EquipmentItem>();
        item->tech = integer(state, "tech", 1, 3);
        result = std::move(item);
        break;
    }
    case 4: {
        auto item = std::make_unique<EngineItem>();
        item->xl = static_cast<uint8_t>(integer(state, "xl", 0, 1));
        result = std::move(item);
        break;
    }
    }
    if (state.contains("flags"))
        result->b10 = static_cast<uint8_t>(integer(state, "flags", 0, 255));
    result->set_id(kind == 4 ? integer(state, "engine", 0, 78)
                             : handle(item_kind(kind), state.at("id")));
    result->count = integer(state, "count", 0, 1000000);
    result->part = integer(state, "part", -1, kind == 1 ? 255 : 8);
    result->group = integer(state, "group", kind == 1 ? -1 : 0, kind == 1 ? 4 : 65535);
    if (kind == 1 && !weapon_has_tech(result->id(), static_cast<const WeaponItem &>(*result).tech))
        fail("save-item", "Weapon technology is unavailable");
    if (kind == 3 &&
        !equip_has_tech(result->id(), static_cast<const EquipmentItem &>(*result).tech))
        fail("save-item", "Equipment technology is unavailable");
    return result;
}
Json config_state(const Config &config) {
    Json items = Json::array();
    for (const auto *item : config.items)
        items.push_back(item_state(*item));
    Json armor = Json::array();
    for (auto value : config.armor)
        armor.push_back(value);
    return {{"schema_version", 1},
            {"chassis", identity("chassis", config.mech)},
            {"name", config.name ? config.name : ""},
            {"items", items},
            {"base_flag", config.base_flag},
            {"player_made", config.player_made},
            {"training", config.training},
            {"modified", config.modified},
            {"endo_steel", config.endo_steel},
            {"engine", config.engine},
            {"xl", config.xl},
            {"armor_tons", config.armor_tons},
            {"armor_tech", config.armor_tech},
            {"ferro", config.ferro},
            {"armor", armor},
            {"sink_tech", config.sink_tech},
            {"sinks", config.sinks},
            {"double_sinks", config.double_sinks},
            {"turn_rate", config.turn_rate},
            {"accel_rate", config.accel_rate},
            {"decel_rate", config.decel_rate},
            {"turn_accel", config.turn_accel},
            {"g70", config.g70},
            {"g78", config.g78},
            {"b1d", config.b1d}};
}
std::unique_ptr<Config> config_from_state(const Json &state) {
    if (!state.contains("schema_version") || !state.at("schema_version").is_number_integer() ||
        state.at("schema_version") != 1)
        fail("save-schema", "Unsupported semantic configuration");
    auto name = state.at("name").get<std::string>();
    if (name.size() > 255 || name.find('\0') != std::string::npos)
        fail("save-name", "Invalid configuration name");
    auto result = std::make_unique<Config>(handle("chassis", state.at("chassis")), name.c_str());
    const auto &items = state.at("items");
    if (!items.is_array() || items.size() > 512)
        fail("save-limit", "Too many saved configuration items");
    for (const auto &item : items)
        result->items.push_back(item_from_state(item).release());
    result->base_flag = static_cast<uint8_t>(integer(state, "base_flag", 0, 1));
    result->player_made = static_cast<uint8_t>(integer(state, "player_made", 0, 1));
    result->training = static_cast<uint8_t>(integer(state, "training", 0, 1));
    result->modified = static_cast<uint8_t>(integer(state, "modified", 0, 1));
    result->endo_steel = static_cast<uint8_t>(integer(state, "endo_steel", 0, 1));
    result->engine = integer(state, "engine", 0, 78);
    result->xl = static_cast<uint8_t>(integer(state, "xl", 0, 1));
    result->armor_tons = real(state, "armor_tons");
    result->armor_tech = integer(state, "armor_tech", 1, 3);
    result->ferro = static_cast<uint8_t>(integer(state, "ferro", 0, 1));
    if (!state.at("armor").is_array() || state.at("armor").size() != 11)
        fail("save-armor", "Expected eleven armor locations");
    for (size_t i = 0; i < 11; ++i) {
        const auto &entry = state.at("armor")[i];
        if (!entry.is_number_integer() || entry < 0 || entry > 10000)
            fail("save-armor", "Invalid armor allocation");
        result->armor[i] = entry.get<int32_t>();
    }
    result->sink_tech = integer(state, "sink_tech", 1, 3);
    result->sinks = integer(state, "sinks", 0, 1000);
    result->double_sinks = static_cast<uint8_t>(integer(state, "double_sinks", 0, 1));
    result->turn_rate = real(state, "turn_rate");
    result->accel_rate = real(state, "accel_rate");
    result->decel_rate = real(state, "decel_rate");
    result->turn_accel = real(state, "turn_accel");
    for (const char *key : {"g70", "g78"})
        if (!state.at(key).is_number_integer() || state.at(key) < 0 || state.at(key) > UINT32_MAX)
            fail("save-config", "Invalid configuration flags", {}, {}, key);
    result->g70 = state.at("g70").get<uint32_t>();
    result->g78 = state.at("g78").get<uint32_t>();
    result->b1d = static_cast<uint8_t>(integer(state, "b1d", 0, 255));
    if (result->weapon_count() > 16)
        fail("save-limit", "Loadout exceeds sixteen supported weapons");
    result->recount();
    return result;
}
void restore_config(Config &target, const Json &state) {
    auto source = config_from_state(state);
    std::swap(target.name, source->name);
    target.items.swap(source->items);
    target.mech = source->mech;
    target.base_flag = source->base_flag;
    target.player_made = source->player_made;
    target.b1d = source->b1d;
    target.training = source->training;
    target.modified = source->modified;
    target.endo_steel = source->endo_steel;
    target.engine = source->engine;
    target.xl = source->xl;
    target.armor_tons = source->armor_tons;
    target.armor_tech = source->armor_tech;
    target.ferro = source->ferro;
    std::copy(std::begin(source->armor), std::end(source->armor), std::begin(target.armor));
    target.sink_tech = source->sink_tech;
    target.sinks = source->sinks;
    target.double_sinks = source->double_sinks;
    target.g70 = source->g70;
    target.g78 = source->g78;
    target.turn_rate = source->turn_rate;
    target.accel_rate = source->accel_rate;
    target.decel_rate = source->decel_rate;
    target.turn_accel = source->turn_accel;
    target.crc = 0;
    target.recount();
}
Json store_state(const Store &store) {
    Json items = Json::array();
    for (auto *item : store.items)
        items.push_back(item_state(*item));
    return {{"full", store.full}, {"flag", store.b04}, {"items", items}};
}
void restore_store(Store &store, const Json &state) {
    const auto &items = state.at("items");
    if (!items.is_array() || items.size() > 65536)
        fail("save-limit", "Too many inventory records");
    Store temporary(static_cast<uint8_t>(integer(state, "flag", 0, 1)));
    for (const auto &value : items)
        temporary.items.push_back(item_from_state(value).release());
    temporary.full = static_cast<uint8_t>(integer(state, "full", 0, 1));
    store.items.swap(temporary.items);
    store.full = temporary.full;
    store.b04 = temporary.b04;
}
Json slot_state(const GameSlot &slot) {
    const auto skin_id =
        slot.mech < 0 || slot.skin == 0 ? std::string{} : content_skin_id(slot.mech, slot.skin);
    if (slot.mech >= 0 && skin_id.empty() && slot.skin != 0)
        fail("save-skin", "Cannot save an unknown selected skin");
    return {{"chassis", slot.mech < 0 ? Json() : Json(identity("chassis", slot.mech))},
            // Null is an explicit request for the chassis's native default.
            // Fresh Lab slots need no decoded native skin metadata yet.
            {"skin", skin_id.empty() ? Json() : Json(skin_id)},
            {"config", slot.config ? config_state(*slot.config) : Json()},
            {"x0c", slot.x0c},
            {"x14", slot.x14}};
}
void restore_slot(GameSlot &slot, const Json &state) {
    int32_t mech = state.at("chassis").is_null() ? -1 : handle("chassis", state.at("chassis"));
    auto config = state.at("config").is_null() ? std::unique_ptr<Config>()
                                               : config_from_state(state.at("config"));
    if (config && config->mech != mech)
        fail("save-slot", "Slot and loadout chassis disagree");
    int skin = mech < 0 || state.at("skin").is_null()
                   ? 0
                   : content_skin_index(mech, state.at("skin").get<std::string>());
    if (skin < 0)
        fail("missing-content", "Saved skin is unavailable", {}, state.at("skin"));
    const auto x0c = integer(state, "x0c", INT32_MIN, INT32_MAX);
    const auto x14 = integer(state, "x14", INT32_MIN, INT32_MAX);
    delete slot.config;
    slot.config = config.release();
    slot.mech = mech;
    slot.skin = skin;
    slot.x0c = x0c;
    slot.x14 = x14;
}
uint32_t save_managed_store(std::FILE *output, const Store &store) {
    return write_record(output, "CCSTRV1\n", store_state(store), 16 * 1024 * 1024);
}
void load_managed_store(std::FILE *input, Store &store) {
    restore_store(store, read_record(input, "CCSTRV1\n", 16 * 1024 * 1024));
}
void save_managed_slot(std::FILE *output, const GameSlot &slot) {
    write_record(output, "CCSLOTV1", slot_state(slot), 2 * 1024 * 1024);
}
void load_managed_slot(std::FILE *input, GameSlot &slot) {
    restore_slot(slot, read_record(input, "CCSLOTV1", 2 * 1024 * 1024));
}
bool load_managed_config(std::FILE *input, Config &target) {
    long start = std::ftell(input);
    char magic[8]{};
    if (std::fread(magic, 1, 8, input) != 8 || std::memcmp(magic, "CCLOAD1\n", 8) != 0) {
        std::fseek(input, start, SEEK_SET);
        return false;
    }
    if (std::fseek(input, start, SEEK_SET) != 0)
        fail("save-loadout", "Cannot read managed loadout");
    restore_config(target, read_record(input, "CCLOAD1\n", 1024 * 1024));
    return true;
}
uint32_t save_managed_config(std::FILE *output, const Config &config) {
    return write_record(output, "CCLOAD1\n", config_state(config), 1024 * 1024);
}
std::vector<uint32_t> pack_managed_config(const Config &config, const std::string &skin) {
    auto content = active_content();
    if (!content)
        fail("network-content", "No active content session");
    std::string text = Json{
        {"schema_version", 1},
        {"gameplay_digest", content->gameplay_digest},
        {"session_digest", content->session_digest},
        {"base_digest", content_base_digest()},
        {"simulation_options", content_simulation_options()},
        {"mission", content_selected_mission()},
        {"config", config_state(config)},
        {"skin", skin}}.dump();
    size_t size = 16 + ((text.size() + 3) / 4) * 4;
    if (size > 65532)
        fail("network-limit", "Managed loadout exceeds network size limit");
    std::vector<uint32_t> result(size / 4, 0);
    result[0] = content_loadout_marker | static_cast<uint32_t>(size);
    result[1] = 0x3143434d;
    result[2] = 1;
    result[3] = static_cast<uint32_t>(text.size());
    std::memcpy(result.data() + 4, text.data(), text.size());
    return result;
}
Config *unpack_managed_config(const uint32_t *words, size_t count, std::string *skin,
                              std::string *mission) {
    if (!words || count < 4 || words[1] != 0x3143434d || words[2] != 1 ||
        words[3] > (count - 4) * 4 || (words[0] & 65535) != count * 4)
        fail("network-loadout", "Invalid managed loadout envelope");
    auto content = active_content();
    if (!content)
        fail("network-content", "Managed loadout requires a content session");
    auto state = parse_json(std::string(reinterpret_cast<const char *>(words + 4), words[3]),
                            "network loadout");
    if (!state.at("schema_version").is_number_integer() || state.at("schema_version") != 1 ||
        state.at("gameplay_digest") != content->gameplay_digest ||
        state.at("session_digest") != content->session_digest ||
        state.at("base_digest") != content_base_digest() ||
        state.at("simulation_options") != content_simulation_options())
        fail("network-content", "Managed gameplay identity does not match");
    auto result = config_from_state(state.at("config"));
    const auto mission_id = state.value("mission", std::string{});
    if (!content_selected_mission().empty() && mission_id != content_selected_mission())
        fail("network-content", "Managed loadout belongs to a different selected mission");
    if (skin)
        *skin = state.value("skin", std::string{});
    if (mission)
        *mission = mission_id;
    return result.release();
}
} // namespace mech3
