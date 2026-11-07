#include "frontend/sdl/content_visuals.h"

#include "mech3/content_hud.h"
#include "mech3/content_registry.h"
#include "platform/content_visuals.h"

namespace sdlfe {
void apply_content_visuals(CoreCriticalAssets &assets) {
    const auto content = mech3::active_content();
    if (!content)
        return;
    assets.hud_artwork = mech3::content_hud_artwork();
    for (const auto &binding : platform::content::selections(content)) {
        assets.models.push_back({binding.chassis, binding.lod, binding.mesh, binding.textures,
                                 binding.content_id, binding.validate_resource});
        if (!binding.normals.empty())
            assets.normals.push_back(
                {binding.chassis, binding.lod, binding.normals, binding.content_id});
        if (!binding.materials.empty())
            assets.materials.push_back(
                {binding.chassis, binding.lod, binding.materials, binding.content_id});
    }
}
} // namespace sdlfe
