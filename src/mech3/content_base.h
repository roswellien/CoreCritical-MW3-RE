#pragma once

#include <string>

namespace mech3 {
// Runtime-only identity of the native gameplay banks and loose native inputs.
// It is independent of host paths and never requires data for CLI authoring.
std::string content_base_digest();
void clear_content_base_digest_cache();
// Loose interpreter inputs must be gameplay data admitted by this snapshot.
void validate_content_script_source(const char *name);
bool content_managed_script_source(const char *name);
void validate_content_world_bank(const char *name);
} // namespace mech3
