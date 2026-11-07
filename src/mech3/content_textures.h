#pragma once
#include "content/content.h"

namespace zimg {
struct Texture;
}
namespace mech3 {
void register_texture_capabilities(cc::content::Capabilities &capabilities);
void validate_content_textures(const cc::content::ResolvedContentSet &content);
void require_content_texture_quiescence();
bool content_texture_named(const char *name);
zimg::Texture *load_content_texture(const char *name);
void content_texture_released(zimg::Texture *texture);
} // namespace mech3
