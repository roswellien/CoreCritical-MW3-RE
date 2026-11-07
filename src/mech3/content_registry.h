#pragma once

#include <memory>
#include <string>
#include <vector>

#include "content/content.h"

namespace zrdr {
struct Node;
}
namespace mech3 {
struct MechDfn;
struct WeaponLab;

// The active immutable catalogue is installed before any game state is made.
// Old numeric stock identities remain fixed; added identities are session handles.
std::vector<cc::content::Definition> stock_content();
cc::content::Capabilities content_capabilities();
void validate_native_content(const cc::content::ResolvedContentSet &content);
void activate_content(std::shared_ptr<const cc::content::ResolvedContentSet> content);
std::shared_ptr<const cc::content::ResolvedContentSet> active_content();
const cc::content::Definition *content_definition(const char *kind, int32_t handle);
const char *content_label(const char *kind, int32_t handle);
int32_t content_index(const char *kind, const char *name);
int32_t chassis_count();
int32_t equipment_count();
int32_t equipment_family(int32_t item);
bool weapon_is_missile(int32_t weapon);
bool weapon_is_lrm(int32_t weapon);
const char *chassis_model_name(int32_t mech);
const char *content_vehicle_model(const char *native_type);
bool content_vehicle_field_base(const char *native_type);
const char *content_weapon_model_name(const char *native_name);
// Lab/inventory data can be read before a world's weapon effects exist.
void prepare_content_weapon_labs();
const WeaponLab *content_weapon_lab(int32_t weapon);
int32_t content_weapon_rounds(int32_t weapon);

// Reader composition follows legacy patching for archive and loose reads alike.
// It may replace the owned tree; the returned tree belongs to the caller.
zrdr::Node *compose_content_reader(const char *entry, zrdr::Node *tree);
zrdr::Node *load_content_chassis_reader(int32_t mech);
void apply_content_skins(MechDfn &dfn);
void apply_content_loadouts(MechDfn &dfn);
std::string content_skin_id(int32_t mech, int32_t index);
const char *content_skin_label(int32_t mech, int32_t index);
int32_t content_skin_index(int32_t mech, const std::string &id);
} // namespace mech3
