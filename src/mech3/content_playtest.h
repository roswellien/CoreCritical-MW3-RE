#pragma once

#include "content/content.h"

namespace mech3 {
class GameEngine;
class Pilot;

// Direct native playtests share the production content identity envelope.
enum class ContentPeerCheck { pending, compatible, incompatible };
bool content_mission_is_multiplayer(const std::string &mission);
void validate_content_multiplayer_selection(const cc::content::ResolvedContentSet &content);
bool configure_content_multiplayer_launch(GameEngine &engine, Pilot &pilot,
                                          const std::string &mission);
std::vector<uint32_t> content_playtest_identity(const std::string &mission);
ContentPeerCheck check_content_playtest_identity(const std::vector<uint32_t> &words,
                                                 const std::string &mission,
                                                 std::string *reason = nullptr);
void publish_content_playtest_identity(const std::string &mission);
ContentPeerCheck content_playtest_peers(const std::string &mission, size_t required,
                                        std::string *reason = nullptr);
} // namespace mech3
