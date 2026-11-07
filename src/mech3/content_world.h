#pragma once

#include "content/world.h"

namespace zrdr {
struct Node;
}
namespace zcls {
struct Node;
}

namespace mech3 {

void append_world_stock_content(std::vector<cc::content::Definition> &definitions);
cc::content::Capabilities world_capabilities();
void validate_world_content(const cc::content::ResolvedContentSet &content);
void register_content_worlds(const cc::content::ResolvedContentSet &content);
void clear_content_world_registry();
bool content_world_is_authored(int32_t world_index);
int32_t content_world_index(const cc::content::Definition &world);
// Called after native class/model reset. Validation is completed before any
// native object is published. False means this is a legacy world.
bool load_content_world(int32_t world_index);
void clear_content_world();
void clear_content_mission_selection();
void refresh_content_mission();
void validate_content_mission_objectives();
const std::string &content_world_digest();
// Validates/compiles immutable inputs without constructing a native scene.
std::string content_mission_world_digest(const std::string &mission);
const std::string &content_selected_mission();
bool content_mission_actor_required(const char *native_name);
void validate_content_vehicle_assets(const cc::content::ResolvedContentSet &content);
zcls::Node *create_content_vehicle_node(const char *vehicle_type, const char *native_name);
zrdr::Node *content_world_reader(const char *entry);
void compose_world_reader(const char *entry, zrdr::Node *root);
bool start_content_mission(const std::string &id);
std::vector<std::pair<std::string, int32_t>> content_multiplayer_entries();
bool select_content_multiplayer(int32_t value, int32_t &world, int32_t &number);
// Crater restoration uses the immutable compiled source for authored models.
bool restore_content_node(zcls::Node *node);

} // namespace mech3
