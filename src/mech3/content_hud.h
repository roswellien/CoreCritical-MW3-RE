#pragma once

#include "content/content.h"
#include "platform/prepared_hud.h"

namespace mech3 {
void register_hud_capabilities(cc::content::Capabilities &capabilities);
void validate_content_hud(const cc::content::ResolvedContentSet &content);
bool content_hud_selected();
// Only an explicitly selected layout overrides the native reader. Coordinates
// are logical HUD pixels; the existing native widgets retain state and input.
bool content_hud_position(const char *piece, const char *key, int32_t width, int32_t height,
                          int32_t out[2]);
bool content_hud_size(const char *piece, int32_t out[2]);
bool content_hud_width(const char *piece, int32_t *width);
bool content_hud_visible(const char *piece);
bool content_hud_radius(bool zoomed, int32_t *radius);
platform::HudArtworkSources content_hud_artwork();
} // namespace mech3
