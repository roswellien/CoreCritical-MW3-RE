#pragma once

#include "content/content.h"

namespace mech3 {
void validate_content_legacy(const cc::content::ResolvedContentSet &content);
void mount_content_legacy(const cc::content::ResolvedContentSet &content);
void unmount_content_legacy();
} // namespace mech3
