#pragma once

#include <cstdint>
#include <memory>

#include "frontend/sdl/sdl_render3d.h"
#include "renderer/renderer_bridge_config.hpp"

namespace cc_mw3_re {

struct QualityStatistics {
  // mip_levels counts generated levels below each uploaded base image.
  std::uint64_t fxaa_passes{}, anisotropic_samplers{}, mip_levels{};
};
struct AssetStatistics {
  std::uint64_t prepared_batches{}, gpu_model_draws{}, cpu_clipped_triangles{},
      named_textures{};
  std::uint64_t geometry_uploads{};
  std::uint64_t lit_draws{}, normal_draws{};
  std::uint64_t retired_geometry_buffers{};
  std::uint64_t gpu_prepared_vertices{}, early_prepared_models{},
      prepared_cpu_fallback_batches{};
  // Decoded world-page pixels only; excludes authored catalogs and GPU copies.
  std::uint64_t world_normal_decoded_bytes{}, world_normal_peak_decoded_bytes{},
      world_normal_loaded_pages{};
  std::uint64_t binding_rejected_mismatch{}, native_fingerprint_evaluations{};
};

class VulkanFrameRenderer : public sdlfe::FrameRenderer {
public:
  // Successful visible presentations, including SUBOPTIMAL, across scene
  // resets.
  virtual std::uint64_t presentation_count() const = 0;
  virtual QualityStatistics quality_statistics() const = 0;
  virtual AssetStatistics asset_statistics() const = 0;
  virtual cc::geometry::Statistics geometry_statistics() const = 0;
};

RendererBridgeConfig configure_bridge(
    sdlfe::RendererBackend backend = sdlfe::RendererBackend::core_critical_dxgi,
    const sdlfe::CoreCriticalQuality &quality = {},
    const sdlfe::CoreCriticalAssets &assets = {});

std::unique_ptr<VulkanFrameRenderer> make_renderer(
    SDL_Window *window,
    sdlfe::RendererBackend backend = sdlfe::RendererBackend::core_critical_dxgi,
    const sdlfe::CoreCriticalQuality &quality = {},
    const sdlfe::CoreCriticalAssets &assets = {});

} // namespace cc_mw3_re
