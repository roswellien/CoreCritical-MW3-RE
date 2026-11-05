#pragma once

#include "frontend/sdl/sdl_render3d.h"
#include "frontend/sdl/cpu_interval.h"
#include "platform/prepared_visuals.h"

namespace cc_mw3_re {

struct PreparedBatchDraw {
    const platform::prepared::Batch *batch{};
    const platform::prepared::Image *texture{};
    const platform::prepared::NormalImage *normal{};
    platform::prepared::Matrix camera{}, world{};
    // Mixed-bone seams follow the existing CPU visual deformation contract.
    // Their per-frame vertices are submitted as TL data, never cached as immutable geometry.
    std::vector<platform::prepared::Vertex> deformed;
};
struct PreparedModelDraw {
    bool replace{};
    std::vector<PreparedBatchDraw> batches;
};

class PreparedModels {
  public:
    struct CpuStatistics {
        std::uint64_t calls{}, selection_ticks{}, retarget_ticks{}, deform_ticks{};
    };
    const CpuStatistics &cpu_statistics() const { return cpu_statistics_; }
    explicit PreparedModels(const sdlfe::CoreCriticalAssets &options);
    bool empty() const { return models_.empty(); }
    const platform::prepared::Image *named_texture(const char *name) const;
    PreparedModelDraw draw(const platform::ModelDrawContext &context) const;

  private:
    struct Model {
        std::string source_asset;
        platform::prepared::Mesh mesh;
        platform::prepared::Textures textures;
        platform::prepared::Normals normals;
    };
    platform::prepared::Textures named_;
    std::vector<Model> models_;
    const bool cpu_profile_ = sdlfe::cpu_profile_requested();
    mutable CpuStatistics cpu_statistics_;
};

} // namespace cc_mw3_re
