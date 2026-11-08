#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zcls { struct Node; }

namespace mech3 {
// Borrowed only while the authored static scene is alive. Actor, camera and
// animation nodes never enter this identity table.
void bind_content_world_activation(const std::string& digest,
                                   std::map<std::string, zcls::Node*> nodes);
void clear_content_world_activation();
void register_content_world_activation();
void unregister_content_world_activation();
bool content_world_activation_registered();
// Checks the complete required record before any native zSave callback runs.
// Capture permits an explicitly marked nonpersistent deformation snapshot;
// restoration refuses it instead of silently discarding changed geometry.
void validate_content_world_snapshot(const std::vector<uint8_t>& bytes, bool restoring);
} // namespace mech3
