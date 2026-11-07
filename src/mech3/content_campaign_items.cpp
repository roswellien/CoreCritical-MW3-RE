#include "mech3/content_campaign_items.h"

#include <limits>
#include <memory>

#include "mech3/content_registry.h"
#include "mech3/pilot.h"

namespace mech3::campaign_items {
namespace {
using cc::content::Definition;
using cc::content::fail;
using cc::content::Json;

int quantity(const Json &action) {
    const Json count = action.value("count", Json(1));
    // Compare JSON values before narrowing: unsigned inputs must not wrap into
    // an otherwise accepted signed count.
    if (!count.is_number_integer() || count < 1 || count > 1000000)
        fail("campaign-reward", "Reward count must be an integer from 1 through 1000000");
    return count.get<int>();
}

int technology(const Json &action, const Definition &definition) {
    const Json mask = definition.data.value("tech", Json(0));
    if (!mask.is_number_integer() || mask < 1 || mask > 3)
        fail("campaign-reward", "Reward definition has no available native technology", {},
             definition.id);
    const int available = mask.get<int>();
    // Retain Clan as the default where available, and select Inner Sphere for
    // definitions that have no Clan implementation.
    const Json tech = action.value("tech", Json((available & 1) ? 1 : 2));
    if (!tech.is_number_integer() || tech < 1 || tech > 3)
        fail("campaign-reward", "Reward technology must be an integer from 1 through 3", {},
             definition.id);
    const int selected = tech.get<int>();
    if ((available & selected) != selected)
        fail("campaign-reward", "Reward technology is unavailable for this definition", {},
             definition.id);
    return selected;
}

const Definition &definition(const Json &action, const cc::content::ResolvedContentSet &content) {
    const auto kind = action.value("kind", std::string{});
    if (kind != "weapon" && kind != "equipment" && kind != "chassis" && kind != "ammo" &&
        kind != "mission_item")
        fail("campaign-action", "Unsupported inventory reward kind");
    const auto *item =
        content.find(kind == "ammo" ? "weapon" : kind, action.value("id", std::string{}));
    if (!item)
        fail("campaign-reward", "Reward references missing content");
    if (kind == "weapon" || kind == "equipment")
        technology(action, *item);
    if (kind == "ammo" && !item->data.value("uses_ammo", false))
        fail("campaign-reward", "Ammunition reward requires a weapon that uses ammunition", {},
             item->id);
    quantity(action);
    return *item;
}
} // namespace

void validate(const Json &action, const Definition &,
              const cc::content::ResolvedContentSet &content) {
    definition(action, content);
}

void apply(CampaignGame &game, const Json &rewards) {
    const auto content = active_content();
    if (!content)
        fail("campaign-content", "Inventory rewards require an active content set");
    for (const auto &action : rewards) {
        const auto &item = definition(action, *content);
        const auto kind = action.at("kind").get<std::string>();
        const int n = quantity(action) * (action.at("op") == "take" ? -1 : 1);
        if (kind == "mission_item") {
            auto &inventory = game.content_state["mission_items"];
            if (!inventory.is_object())
                inventory = Json::object();
            const int64_t count = int64_t(inventory.value(item.id, 0)) + n;
            if (count < 0 || count > INT32_MAX)
                fail("campaign-reward",
                     "Mission item reward cannot consume unavailable items or overflow");
            inventory[item.id] = count;
            continue;
        }
        std::unique_ptr<ConfigItem> value;
        if (kind == "weapon") {
            auto weapon = std::make_unique<WeaponItem>();
            weapon->weapon = item.handle;
            weapon->tech = technology(action, item);
            value = std::move(weapon);
        } else if (kind == "equipment") {
            auto equipment = std::make_unique<EquipmentItem>();
            equipment->equipment = item.handle;
            equipment->tech = technology(action, item);
            value = std::move(equipment);
        } else if (kind == "ammo") {
            auto ammo = std::make_unique<AmmoItem>();
            ammo->weapon = item.handle;
            value = std::move(ammo);
        } else {
            auto chassis = std::make_unique<BaseItem>();
            chassis->value = item.handle;
            value = std::move(chassis);
        }
        const auto *previous = game.store.find(value.get());
        const int64_t count = (previous ? int64_t(previous->count) : 0) + n;
        if (count < 0 || count > INT32_MAX)
            fail("campaign-reward",
                 "Inventory action cannot consume unavailable items or overflow the native store");
        game.store.add_copy(value.get(), n);
    }
}
} // namespace mech3::campaign_items
