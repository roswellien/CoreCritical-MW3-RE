#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mech3 {
bool managed_gameplay_session();
uint32_t content_simulation_options();
void append_content_session(std::vector<uint32_t> &player);
bool content_sessions_match(const std::vector<uint32_t> &player, std::string *reason = nullptr);
} // namespace mech3
