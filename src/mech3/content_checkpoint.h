#pragma once

#include <string>

namespace mech3 {
class GameEngine;
// Explicitly validates and imports a ModSaveV1 copy into this profile's pilot
// index, then selects its native campaign. Source files are never rewritten.
bool configure_content_checkpoint(GameEngine &engine, const std::string &path);
} // namespace mech3
