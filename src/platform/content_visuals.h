#pragma once

#include "content/content.h"

#include <functional>
#include <memory>

namespace platform::content {
struct PreparedAppearance {
    std::string content_id, chassis, lod, mesh, textures, normals, materials;
    std::function<void(const std::string &)> validate_resource;
};
void register_capabilities(cc::content::Capabilities &capabilities);
void validate(const cc::content::ResolvedContentSet &content);
std::vector<PreparedAppearance>
selections(std::shared_ptr<const cc::content::ResolvedContentSet> content);
} // namespace platform::content
