#pragma once

#include "frontend/sdl/cpu_interval.h"
#include "frontend/sdl/sdl_render3d.h"
#include "platform/prepared_visuals.h"
#include "renderer/surface_materials.hpp"

namespace cc_mw3_re {

struct PreparedBatchDraw {
    const ::platform::prepared::Batch *batch{};
    const ::platform::prepared::Image *texture{};
    const ::platform::prepared::NormalImage *normal{};
    const ::mw34::materials::SurfaceMaterialRecord *surface_material{};
    ::platform::prepared::Matrix camera{}, world{};
    // Mixed-bone seams follow the existing CPU visual deformation contract.
    // Their per-frame vertices are submitted as TL data, never cached as
    // immutable geometry.
    std::vector<::platform::prepared::Vertex> deformed;
};
struct PreparedModelDraw {
    bool replace{};
    std::vector<PreparedBatchDraw> batches;
};

using PreparedMaterialBinding = sdlfe::PreparedMaterialBinding;

class PreparedModels {
  public:
    struct CpuStatistics {
        std::uint64_t calls{}, selection_ticks{}, retarget_ticks{}, deform_ticks{};
    };
    const CpuStatistics &cpu_statistics() const { return cpu_statistics_; }
    explicit PreparedModels(const sdlfe::CoreCriticalAssets &options,
                            const std::vector<PreparedMaterialBinding> &materials = {});
    bool empty() const { return models_.empty(); }
    bool has_binding(const ::platform::ModelDrawContext &context) const;
    const ::platform::prepared::Image *named_texture(const char *name) const;
    PreparedModelDraw draw(const ::platform::ModelDrawContext &context,
                           bool rigid_only = false) const;

  private:
    struct Model {
        std::string source_asset;
        ::platform::prepared::Mesh mesh;
        ::platform::prepared::Textures textures;
        ::platform::prepared::Normals normals;
        std::shared_ptr<::mw34::materials::SurfaceMaterialPack> surface_materials;
    };
    ::platform::prepared::Textures named_;
    std::vector<Model> models_;
    const bool cpu_profile_ = sdlfe::cpu_profile_requested();
    mutable CpuStatistics cpu_statistics_;
};

} // namespace cc_mw3_re
