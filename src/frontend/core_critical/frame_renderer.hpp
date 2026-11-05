#pragma once

#include <cstdint>
#include <memory>

#include "frontend/sdl/sdl_render3d.h"

namespace cc_mw3_re {

struct QualityStatistics {
    // mip_levels counts generated levels below each uploaded base image.
    std::uint64_t fxaa_passes{}, anisotropic_samplers{}, mip_levels{};
};

class VulkanFrameRenderer : public sdlfe::FrameRenderer {
  public:
    // Successful visible presentations, including SUBOPTIMAL, across scene resets.
    virtual std::uint64_t presentation_count() const = 0;
    virtual QualityStatistics quality_statistics() const = 0;
};

std::unique_ptr<VulkanFrameRenderer>
make_renderer(SDL_Window *window,
              sdlfe::RendererBackend backend = sdlfe::RendererBackend::core_critical_dxgi,
              const sdlfe::CoreCriticalQuality &quality = {});

} // namespace cc_mw3_re
