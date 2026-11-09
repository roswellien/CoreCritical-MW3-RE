#include "depth_transfer.hpp"
#include "sky_backdrop.hpp"
// Vulkan scene renderer for the Direct3D 6 provider (adapted from
// tools/vulkan-probe/replay.cpp). The provider records a scene
// (transformed-and-lit vertices, fixed-function state, clears) and the renderer
// draws it into the guest render target: the target's 16-bit pixels and the
// guest z-buffer are uploaded, the scene is rasterized on the GPU, and both are
// read back into guest memory, where the game's own 2D code and Flip continue
// to use them. Colour uses R5G6B5 (the target's format, so blending happens at
// the target's precision) and depth D16 (the game's 16-bit z-buffer). The
// Vulkan loader is opened at run time; no validation layer is required
// (MW34_VK_VALIDATION=1 enables it when installed). The same device presents
// flipped frames: the guest front buffer (R5G6B5) is uploaded and blitted to a
// FIFO swapchain on the game's window (Flip waits for the vertical blank, as
// DDFLIP_WAIT did).
#pragma once
#include "capture_pixels.hpp"
#include "frontend/sdl/cpu_interval.h"
#define VK_NO_PROTOTYPES
#include "buffer_reserve.hpp"
#include "camera_history.hpp"
#include "d3d_scene_frag.h"
#include "d3d_scene_vert.h"
#include "draw_emissions.hpp"
#include "effect_lighting_settings.hpp"

#include "effect_scene_frag.h"
#include "exact_uniform_runs.hpp"
#include "fixture_shadow.hpp"
#include "fixture_shadow_frag.h"
#include "fixture_shadow_vert.h"
#include "frontend_canvas.hpp"
#include "fxaa_frag.h"
#include "fxaa_vert.h"
#include "geometry/resource.hpp"
#include "launcher_display.hpp"
#include "light_cull_tiled_comp.h"
#include "model_scene_vert.h"
#include "native_geometry_vert.h"
#include "platform/vk_loader.hpp"
#include "platform/wsi_surface.hpp"
#include "platform/wsi_win32_dxgi.hpp"
#include "renderer_bridge_config.hpp"
#include "status_model_fit.hpp"
#include "sun_lighting.hpp"
#include "sun_model_vert.h"
#include "sun_shadow_vert.h"
#include "sun_tl_vert.h"
#include "sun_world_vert.h"
#include "surface_detail.hpp"
#include "texture_staging.hpp"
#include "tiled_light_stage.inc"
#include "world_binding.hpp"
#include "world_coverage_frag.h"
#include "world_fast_binding.hpp"
#include "world_scene_vert.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace mw34::vkscene {
// rgba8888: host-composed straight-alpha RGBA (GPU HUD images: RGB565 colour
// with the image's alpha mask or colour key), carried in pixels32 (R, G, B, A
// bytes in memory order).
enum class TexFormat { rgb565, argb1555, argb4444, rgba8888 };
// key identifies a guest surface; hash identifies its current content. A cached
// version may omit pixel payloads; a changed version must supply width * height
// pixels.
struct TextureRef {
  std::uint64_t key, hash;
  unsigned width, height;
  TexFormat format;
  std::vector<std::uint16_t> pixels;
  std::vector<std::uint32_t> pixels32;
};
// Fixed-function state that selects a pipeline: blend enable/source/destination
// (D3DBLEND), depth enable/function (D3DCMP)/write, cull mode (D3DCULL).
struct PipelineKey {
  std::uint8_t blend, src, dst, zenable, zfunc, zwrite, cull,
      points = 0; // points: D3DPT_POINTLIST input assembly
  std::uint64_t packed() const {
    return std::uint64_t(blend) | std::uint64_t(src) << 8 |
           std::uint64_t(dst) << 16 | std::uint64_t(zenable) << 24 |
           std::uint64_t(zfunc) << 32 | std::uint64_t(zwrite) << 40 |
           std::uint64_t(cull) << 48 | std::uint64_t(points) << 56;
  }
};
// Shader ABI: these arrays and the model extension below must match the
// compiled SPIR-V layouts. Keep the size and offset assertions when editing
// either side.
struct Push {
  float target[4], fog[4], factor[4];
  std::uint32_t mode[4];
};
static_assert(sizeof(Push) == 64);
using ModelVertex = cc::geometry::Vertex;
static_assert(sizeof(ModelVertex) == 32);
struct ModelGeometry {
  std::uint64_t key{}, hash{};
  std::vector<ModelVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::weak_ptr<const cc::geometry::Resource> source;
};
struct ModelInstance {
  std::shared_ptr<const ModelGeometry> geometry;
  std::array<float, 12> camera{};
  std::array<float, 6> projection{};
  std::uint32_t color{};
  float diagnostic_fog_factor{1.f};
  bool world{};
  bool gpu_vertices{}; // Projection, depth clipping and fog require no CPU TL
                       // stream.
  std::array<float, 12> sun_normal{}, sun_world{};
  bool sun_eligible{};
  std::uint64_t shadow_source{};
  std::shared_ptr<const cc::geometry::Resource> source;
  std::array<float, 4> geometry_fog0{}, geometry_fog1{};
  float geometry_near{};
};
struct ModelPush {
  Push base;
  std::array<float, 12> camera;
  std::array<float, 4> projection;
};
static_assert(sizeof(ModelPush) == 128 && offsetof(ModelPush, camera) == 64 &&
              offsetof(ModelPush, projection) == 112);
struct Draw {
  unsigned first, count;
  int texture;
  int normal_texture{-1};
  unsigned sampler;
  PipelineKey key;
  Push push;
  VkRect2D scissor;
  std::shared_ptr<const ModelInstance> model{};
  std::shared_ptr<const mw34::sun::Tl> sun_tl{};
  // Camera-space smooth normals for CPU-clipped/deformed TL geometry.
  // Owned until submission; never retained as immutable model cache entries.
  std::shared_ptr<const std::vector<std::array<float, 3>>> lit_normals{};
  unsigned char sky{};
  std::shared_ptr<const mw34::sun::Tl> sky_view{};
  std::shared_ptr<const mw34::temporal::Snapshot> camera_view{};
  unsigned detail_role{};
  bool native_sky{};
  std::uint64_t shadow_source{};
  std::function<bool()> shadow_valid;
  bool condition_schematic{};
  bool depth_only{};
  std::uint32_t emission_index{mw34::submission::DrawEmissions::none};
  // Sun material surface parameters (roughness, reflectivity, metallic, active)
  // and eye (world xyz, valid); owned values, zero preserves the legacy path.
  std::array<float, 4> surface_parameters{};
  std::array<float, 4> surface_eye{};
  bool geometry_debug_normals{};
};
struct Clear {
  unsigned flags;
  float color[4];
  float z;
  std::vector<VkRect2D> rects;
};
// index addresses clears or draws according to clear. Keep this stream in guest
// submission order: grouping all clears before draws would change the frame.
struct Command {
  bool clear;
  unsigned index;
};
struct Scene {
  double detail_time{};
  std::uint64_t world_frame{}, world_generation{}, world_context{};
  std::array<float, 3> world_eye{};
  bool world_eye_valid{};
  mw34::effects::Uniform effect_lights{};
  unsigned width, height;
  std::vector<std::uint16_t> color,
      depth; // in/out, tightly packed width*height
  bool has_depth;
  bool frontend_canvas{};
  bool hud{}; // Host HUD/overlay draws keep their original sampling and follow
              // world AA.
  bool guest_surface_write{}; // DirectDraw surface operation outside the 3D
                              // command stream.
  std::vector<unsigned char> vertices; // 32-byte TL vertices, triangle list
  std::vector<TextureRef> textures;
  std::vector<Draw> draws;
  std::vector<std::shared_ptr<const ModelGeometry>> model_warmup;
  mw34::submission::DrawEmissions emissions;
  std::vector<Clear> clears;
  std::vector<Command> order;
};

// Owns the loader, device, caches, render targets and child presentation
// window. The provider must serialize access; these mutable resources have no
// internal lock. Scene and presentation submissions use separate command pools
// and fences on one queue.
class Renderer {
public:
  struct WindowSurface {
    std::vector<const char *> extensions;
    std::function<VkSurfaceKHR(VkInstance)> create;
    std::function<void(VkInstance, VkSurfaceKHR)> destroy;
    std::function<bool(cc_mw3_re::platform::NativeWindowHandle)> is_visible;
    std::function<void(cc_mw3_re::platform::NativeWindowHandle, long *, long *)>
        get_drawable_size;
  };
  struct Quality {
    unsigned samples_x, samples_y, anisotropy;
    bool fxaa;
  };
  enum class ColorTarget { device_default, rgba8 };
  // An explicit format preference lets format-sensitive callers exercise the
  // RGBA fallback without changing device capabilities or process settings.
  explicit Renderer(ColorTarget color_target = ColorTarget::device_default,
                    WindowSurface source = {},
                    Quality quality = {1, 1, 1, false}, bool models = false,
                    mw34::sun::Settings lighting = {},
                    bool native_geometry = false,
                    cc_mw3_re::RendererBridgeConfig config = {})
      : require_rgba8(color_target == ColorTarget::rgba8),
        surface_source(std::move(source)), quality_settings(quality),
        use_gpu_models(models), lighting_settings(lighting),
        use_native_geometry(native_geometry), bridge_config(std::move(config)),
        effects_settings(mw34::effects::from_config(
            bridge_config, lighting_settings.enabled)) {
    if (surface_source.create) {
      bridge_config.dxgi_present = false;
    }
    if (bool(surface_source.create) != bool(surface_source.destroy))
      throw std::runtime_error(
          "Vulkan surface creation and destruction must have one owner");
    if (surface_source.create && surface_source.extensions.empty())
      throw std::runtime_error("Vulkan surface requires instance extensions");
    if ((quality.samples_x != 1 && quality.samples_x != 2) ||
        (quality.samples_y != 1 && quality.samples_y != 2) ||
        (quality.samples_x == 1 && quality.samples_y != 1) ||
        (quality.fxaa && (quality.samples_x != 1 || quality.samples_y != 1)) ||
        (quality.anisotropy != 1 && quality.anisotropy != 2 &&
         quality.anisotropy != 4 && quality.anisotropy != 8 &&
         quality.anisotropy != 16))
      throw std::runtime_error("Invalid Vulkan quality configuration");
  }
  bool alpha_target() const { return rgba; }
  std::string device_name;
  mw34::temporal::History camera_history;
  void invalidate_camera_history(mw34::temporal::Reset reason) {
    if (bridge_config.temporal_inputs)
      camera_history.invalidate(reason);
  }
  void complete_camera_history(std::uint64_t frame, unsigned target,
                               const char *admission = "none") {
    if (!bridge_config.temporal_inputs)
      return;
    camera_history.complete(frame, target);
    unsigned valid = 0, mode_mask = 0;
    for (const auto &[view, pair] : camera_history.pairs()) {
      valid += pair.valid();
      if (pair.current && pair.current->mode < 32)
        mode_mask |= 1U << pair.current->mode;
    }
    std::fprintf(stderr,
                 "Camera history frame=%llu views=%zu pairs=%u mode_mask=%x "
                 "reset=%s rejected=%s mismatch=%s\n",
                 (unsigned long long)frame, camera_history.pairs().size(),
                 valid, mode_mask,
                 mw34::temporal::name(camera_history.last_reset()), admission,
                 camera_history.disagreement());
  }
  std::uint64_t scenes{}, draws{}, uploads{}, submitted_draws{},
      combined_draws{};
  ~Renderer() { shutdown(); }
  // Presents a 16-bit RGB565 frame (rows of `pitch` bytes) to the window. False
  // when this device cannot present to the window (no surface/swapchain
  // support); the caller then uses its other presenter.
  bool present(cc_mw3_re::platform::NativeWindowHandle hwnd,
               const std::uint8_t *pixels, unsigned w, unsigned h,
               unsigned pitch, bool wide_world = false, bool rgba32 = false) {
    const bool visible = surface_source.is_visible
                             ? surface_source.is_visible(hwnd)
                             : cc_mw3_re::platform::is_window_visible(hwnd);
    if (!visible)
      return true; // hidden: nothing to present
    // SDL owns the native WSI surface. The legacy presenter uses a disabled,
    // hit-test-transparent child covering the game window's client area.
    if (hwnd != presentParent) {
      if (device) {
        dropSwapchain();
        dropSurface();
      }
      if (child)
        cc_mw3_re::platform::destroy_present_child(child);
      child = surface_source.create
                  ? nullptr
                  : cc_mw3_re::platform::make_present_child(hwnd);
      presentParent = hwnd;
      if (!child && bridge_config.dxgi_present)
        throw std::runtime_error(
            "Cannot create the required DXGI presentation window");
      if (!child && !surface_source.create)
        return false;
      // A presenting instance selects the device that can present to this
      // window (on hybrid systems the display may belong to another adapter
      // than the one rendering scenes).
      if (!device)
        init(surface_source.create ? hwnd : child);
      else if (presentable && !surface(surface_source.create ? hwnd : child))
        return false;
    }
    if (!presentable || (!windowSurface && !dxgi.active()))
      return false;
    // The previous Flip's copy/blit is waited for here, before its upload
    // buffer, command pool or source image are reused, instead of right after
    // vkQueuePresentKHR (presentation stays one frame per Flip).
    if (presentInFlight) {
      check(
          f.vkWaitForFences(device, 1, &presentFence, VK_TRUE, 10000000000ULL),
          "wait present");
      presentInFlight = false;
    }
    if (child) {
      cc_mw3_re::platform::sync_present_child_size(hwnd, child);
    }
    if (!swapchainReady())
      return true; // minimized: nothing to present
    if (w != srcWidth || h != srcHeight || rgba32 != srcRgba)
      presentSource(w, h, rgba32);
    const unsigned bpp = rgba32 ? 4 : 2;
    ensure(presentUpload, VkDeviceSize(w) * h * bpp,
           VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    auto *dst = static_cast<std::uint8_t *>(presentUpload.mapped);
    for (unsigned y = 0; y < h; ++y)
      std::memcpy(dst + size_t(y) * w * bpp, pixels + size_t(y) * pitch,
                  size_t(w) * bpp);
    unsigned index = 0;
    VkResult r = VK_SUCCESS;
    if (!dxgi.active()) {
      r = f.vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired,
                                  VK_NULL_HANDLE, &index);
      if ((r == VK_ERROR_OUT_OF_DATE_KHR ||
           r == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)) {
        dropSwapchain();
        if (!swapchainReady())
          return true;
        r = f.vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired,
                                    VK_NULL_HANDLE, &index);
      }
      if (r == VK_ERROR_OUT_OF_DATE_KHR ||
          r == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT) {
        dropSwapchain();
        return true;
      }
      if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        check(r, "acquire swapchain image");
    }
    const bool suboptimal_acquire = r == VK_SUBOPTIMAL_KHR;
    const VkImage swap_target =
        dxgi.active() ? dxgi.image() : swapImages[index];
    check(f.vkResetCommandPool(device, presentPool, 0), "reset present pool");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(presentCmd, &bi), "begin present");
    auto bar = [&](VkImage img, VkImageLayout from, VkImageLayout to,
                   VkAccessFlags sa, VkAccessFlags da) {
      VkImageMemoryBarrier b{};
      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.srcAccessMask = sa;
      b.dstAccessMask = da;
      b.oldLayout = from;
      b.newLayout = to;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = img;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      f.vkCmdPipelineBarrier(presentCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &b);
    };
    bar(srcImage.image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {w, h, 1};
    f.vkCmdCopyBufferToImage(presentCmd, presentUpload.buffer, srcImage.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    bar(srcImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    if (dxgi.active())
      dxgi.acquire_image(presentCmd, f.vkCmdPipelineBarrier,
                         f.vkCmdClearColorImage);
    else
      bar(swap_target, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
          VK_ACCESS_TRANSFER_WRITE_BIT);
    // Scaled to the window's client area with nearest sampling (the GDI
    // presenter's COLORONCOLOR).
    VkImageBlit blit{};
    blit.srcSubresource =
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {int(w), int(h), 1};
    // Original menu/movie rasters retain their proportions in the centre.
    // A world readback must use the resident output's aspect ratio, rather
    // than stretching to the window on frames that need a guest upload.
    const auto destination = mw34::display::upload_presentation_rect(
        w, h, remaster_width(), remaster_height(), swapExtent.width,
        swapExtent.height, wide_world);
    blit.dstOffsets[0] = {destination.left, destination.top, 0};
    blit.dstOffsets[1] = {destination.right, destination.bottom, 1};
    if (destination.left || destination.top ||
        destination.right != int32_t(swapExtent.width) ||
        destination.bottom != int32_t(swapExtent.height)) {
      const VkClearColorValue black{};
      const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      f.vkCmdClearColorImage(presentCmd, swap_target,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                             &all);
      bar(swap_target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT);
    }
    if (dxgi.active() && wide_world && remaster_width()) {
      const auto fit =
          mw34::frontend::CanvasFit::fit(remaster_width(), remaster_height(),
                                         swapExtent.width, swapExtent.height);
      blit.dstOffsets[0] = {fit.x, fit.y, 0};
      blit.dstOffsets[1] = {fit.x + fit.width, fit.y + fit.height, 1};
    }
    f.vkCmdBlitImage(presentCmd, srcImage.image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap_target,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_NEAREST);
    if (dxgi.active())
      dxgi.release_image(presentCmd, f.vkCmdPipelineBarrier);
    else
      bar(swap_target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    check(f.vkEndCommandBuffer(presentCmd), "end present");
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const auto shared_sync = dxgi.synchronization();
    si.pNext = dxgi.active() ? &shared_sync : nullptr;
    si.waitSemaphoreCount = dxgi.active() ? 0 : 1;
    si.pWaitSemaphores = &acquired;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &presentCmd;
    si.signalSemaphoreCount = dxgi.active() ? 0 : 1;
    si.pSignalSemaphores = dxgi.active() ? nullptr : &swapRendered[index];
    check(f.vkResetFences(device, 1, &presentFence), "reset present fence");
    check(f.vkQueueSubmit(queue, 1, &si, presentFence), "submit present");
    if (dxgi.active()) {
      presentInFlight = true;
      if (dxgi.present(bridge_config.vsync))
        ++presents;
      return true;
    }
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &swapRendered[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &index;
    r = f.vkQueuePresentKHR(queue, &pi);
    // SUBOPTIMAL still queues a successful presentation and must signal
    // readiness.
    if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
      ++presents;
    if ((r == VK_ERROR_OUT_OF_DATE_KHR ||
         r == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT) ||
        r == VK_SUBOPTIMAL_KHR || suboptimal_acquire) {
      if (r != VK_SUBOPTIMAL_KHR && r != VK_ERROR_OUT_OF_DATE_KHR &&
          r != VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
        check(r, "present");
      check(
          f.vkWaitForFences(device, 1, &presentFence, VK_TRUE, 10000000000ULL),
          "wait present");
      dropSwapchain();
      return true;
    }
    check(r, "present");
    presentInFlight = true;
    return true;
  }
  // Presents the resident scene target (lazy residency on this device: the 3D
  // scenes and the GPU HUD of the frame) directly: no guest copy, no upload.
  // False when this device cannot present to the window yet (the caller then
  // synchronizes guest memory and uses present()). The target returns to
  // attachment layout for the next scene. Present the existing GPU target
  // without a guest upload. The caller completes any pending world-quality work
  // first; queue order sequences the image accesses. false asks the provider to
  // use its upload presentation path; true also covers hidden/minimized
  // windows, where no image needs to be presented. Call on the window owner,
  // with submissions drained, before asynchronous presentation. SetWindowPos
  // can synchronously message the owning thread.
  void resize_present_window(cc_mw3_re::platform::NativeWindowHandle hwnd) {
    if (child) {
      cc_mw3_re::platform::sync_present_child_size(hwnd, child);
    }
  }
  bool present_resident(cc_mw3_re::platform::NativeWindowHandle hwnd,
                        unsigned w, unsigned h, bool manage_window = true) {
    finish_world();
    const bool visible = surface_source.is_visible
                             ? surface_source.is_visible(hwnd)
                             : cc_mw3_re::platform::is_window_visible(hwnd);
    if (!visible)
      return true;
    if (hwnd != presentParent || (!child && !surface_source.create) ||
        !presentable || (!windowSurface && !dxgi.active()) ||
        !colorImage.image || w != width || h != height)
      return false;
    if (presentInFlight) {
      check(
          f.vkWaitForFences(device, 1, &presentFence, VK_TRUE, 10000000000ULL),
          "wait present");
      presentInFlight = false;
    }
    if (manage_window)
      resize_present_window(hwnd);
    if (!swapchainReady())
      return true;
    unsigned index = 0;
    VkResult r = VK_SUCCESS;
    if (!dxgi.active()) {
      r = f.vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired,
                                  VK_NULL_HANDLE, &index);
      if ((r == VK_ERROR_OUT_OF_DATE_KHR ||
           r == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)) {
        dropSwapchain();
        if (!swapchainReady())
          return true;
        r = f.vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired,
                                    VK_NULL_HANDLE, &index);
      }
      if (r == VK_ERROR_OUT_OF_DATE_KHR ||
          r == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT) {
        dropSwapchain();
        return true;
      }
      if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        check(r, "acquire swapchain image");
    }
    const bool suboptimal_acquire = r == VK_SUBOPTIMAL_KHR;
    const VkImage swap_target =
        dxgi.active() ? dxgi.image() : swapImages[index];
    check(f.vkResetCommandPool(device, presentPool, 0), "reset present pool");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(presentCmd, &bi), "begin resident present");
    auto bar = [&](VkImage img, VkImageLayout from, VkImageLayout to,
                   VkAccessFlags sa, VkAccessFlags da, VkPipelineStageFlags ss,
                   VkPipelineStageFlags ds,
                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
      VkImageMemoryBarrier b{};
      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.srcAccessMask = sa;
      b.dstAccessMask = da;
      b.oldLayout = from;
      b.newLayout = to;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = img;
      b.subresourceRange = {aspect, 0, 1, 0, 1};
      f.vkCmdPipelineBarrier(presentCmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1,
                             &b);
    };
    if (in_attachment_layout)
      bar(colorImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT);
    if (dxgi.active())
      dxgi.acquire_image(presentCmd, f.vkCmdPipelineBarrier,
                         f.vkCmdClearColorImage);
    else
      bar(swap_target, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    // The frame keeps its aspect ratio (side or top bars are cleared to black
    // when the window's differs).
    const auto ss = sample_grid();
    const double ow = double(rw) / ss.x, oh = double(rh) / ss.y;
    const auto destination = mw34::display::presentation_rect(
        ow, oh, swapExtent.width, swapExtent.height);
    const int dw = destination.right - destination.left;
    const int dh = destination.bottom - destination.top;
    if (destination.left || destination.top ||
        destination.right != int32_t(swapExtent.width) ||
        destination.bottom != int32_t(swapExtent.height)) {
      const VkClearColorValue black{};
      const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      f.vkCmdClearColorImage(presentCmd, swap_target,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                             &all);
      bar(swap_target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    VkImageBlit blit{};
    blit.srcSubresource =
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {int(rw), int(rh), 1};
    blit.dstOffsets[0] = {destination.left, destination.top, 0};
    blit.dstOffsets[1] = {destination.right, destination.bottom, 1};
    f.vkCmdBlitImage(presentCmd, colorImage.image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap_target,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     (dw == int(rw) && dh == int(rh)) ? VK_FILTER_NEAREST
                                                      : VK_FILTER_LINEAR);
    if (dxgi.active())
      dxgi.release_image(presentCmd, f.vkCmdPipelineBarrier);
    else
      bar(swap_target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    bar(colorImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (!in_attachment_layout)
      bar(depthImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          VK_ACCESS_TRANSFER_READ_BIT,
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          VK_IMAGE_ASPECT_DEPTH_BIT);
    in_attachment_layout = true;
    check(f.vkEndCommandBuffer(presentCmd), "end resident present");
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const auto shared_sync = dxgi.synchronization();
    si.pNext = dxgi.active() ? &shared_sync : nullptr;
    si.waitSemaphoreCount = dxgi.active() ? 0 : 1;
    si.pWaitSemaphores = &acquired;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &presentCmd;
    si.signalSemaphoreCount = dxgi.active() ? 0 : 1;
    si.pSignalSemaphores = dxgi.active() ? nullptr : &swapRendered[index];
    check(f.vkResetFences(device, 1, &presentFence), "reset present fence");
    check(f.vkQueueSubmit(queue, 1, &si, presentFence),
          "submit resident present");
    if (dxgi.active()) {
      presentInFlight = true;
      if (dxgi.present(bridge_config.vsync)) {
        ++presents;
        ++resident_presents;
      }
      return true;
    }
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &swapRendered[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &index;
    r = f.vkQueuePresentKHR(queue, &pi);
    if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
      ++presents;
      ++resident_presents;
    }
    if ((r == VK_ERROR_OUT_OF_DATE_KHR ||
         r == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT) ||
        r == VK_SUBOPTIMAL_KHR || suboptimal_acquire) {
      if (r != VK_SUBOPTIMAL_KHR && r != VK_ERROR_OUT_OF_DATE_KHR &&
          r != VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
        check(r, "present");
      check(
          f.vkWaitForFences(device, 1, &presentFence, VK_TRUE, 10000000000ULL),
          "wait present");
      dropSwapchain();
      return true;
    }
    check(r, "present");
    presentInFlight = true;
    return true;
  }
  std::uint64_t resident_presents{};
  bool dxgi_presenting() const { return dxgi.active(); }
  void request_presented_bgra_pixel(unsigned x, unsigned y) {
    if (!dxgi.active())
      throw std::runtime_error("Presentation capture requires the DXGI bridge");
    dxgi.request_pixel(x, y);
  }
  std::array<unsigned char, 4> presented_bgra_pixel() {
    if (!dxgi.active())
      throw std::runtime_error(
          "Presented-pixel diagnostics require the DXGI bridge");
    return dxgi.read_pixel();
  }

  // Diagnostic: the resident colour image at the render resolution as a binary
  // PPM.
  void dump_fullres(const char *path) {
    finish_world();
    settle();
    const VkDeviceSize bytes = VkDeviceSize(rw) * rh * (rgba ? 4 : 2);
    ensure(readback,
           std::max<VkDeviceSize>(bytes, VkDeviceSize(width) * height * 4),
           VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    check(f.vkResetCommandPool(device, commandPool, 0), "reset command pool");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(cmd, &bi), "begin dump");
    if (in_attachment_layout) {
      barrier(colorImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
      barrier(depthImage.image,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {rw, rh, 1};
    f.vkCmdCopyImageToBuffer(cmd, colorImage.image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             readback.buffer, 1, &c);
    VkMemoryBarrier hb{};
    hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0,
                           nullptr);
    check(f.vkEndCommandBuffer(cmd), "end dump");
    check(f.vkResetFences(device, 1, &fence), "reset fence");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    check(f.vkQueueSubmit(queue, 1, &si, fence), "submit dump");
    check(f.vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL),
          "wait dump");
    in_attachment_layout = false;
    FILE *out = std::fopen(path, "wb");
    if (!out)
      return;
    std::fprintf(out, "P6\n%u %u\n255\n", rw, rh);
    const auto *src = static_cast<const unsigned char *>(readback.mapped);
    std::vector<unsigned char> row(size_t(rw) * 3);
    for (unsigned y = 0; y < rh; ++y) {
      for (unsigned x = 0; x < rw; ++x) {
        unsigned char *o = &row[size_t(x) * 3];
        if (rgba) {
          const unsigned char *p = src + (size_t(y) * rw + x) * 4;
          o[0] = p[0];
          o[1] = p[1];
          o[2] = p[2];
        } else {
          const unsigned p = src[(size_t(y) * rw + x) * 2] |
                             src[(size_t(y) * rw + x) * 2 + 1] << 8;
          o[0] = ((p >> 11) & 31) * 255 / 31;
          o[1] = ((p >> 5) & 63) * 255 / 63;
          o[2] = (p & 31) * 255 / 31;
        }
      }
      std::fwrite(row.data(), row.size(), 1, out);
    }
    std::fclose(out);
  }
  // Capture at physical render resolution, rather than the guest buffer size.
  // Finish world AA and wait for the transfer fence before reading mapped
  // memory.
  void capture_fullres_rgb(std::vector<std::uint8_t> &out_rgb, unsigned &out_w,
                           unsigned &out_h) {
    finish_world();
    settle();
    out_w = rw;
    out_h = rh;
    if (!rw || !rh || !colorImage.image) {
      out_rgb.clear();
      return;
    }
    const VkDeviceSize bytes = VkDeviceSize(rw) * rh * (rgba ? 4 : 2);
    ensure(readback,
           std::max<VkDeviceSize>(bytes, VkDeviceSize(width) * height * 4),
           VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    check(f.vkResetCommandPool(device, commandPool, 0), "reset command pool");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(cmd, &bi), "begin capture");
    if (in_attachment_layout) {
      barrier(colorImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
      barrier(depthImage.image,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {rw, rh, 1};
    f.vkCmdCopyImageToBuffer(cmd, colorImage.image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             readback.buffer, 1, &c);
    VkMemoryBarrier hb{};
    hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0,
                           nullptr);
    check(f.vkEndCommandBuffer(cmd), "end capture");
    check(f.vkResetFences(device, 1, &fence), "reset fence");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    check(f.vkQueueSubmit(queue, 1, &si, fence), "submit capture");
    check(f.vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL),
          "wait capture");
    in_attachment_layout = false;
    const auto *src = static_cast<const unsigned char *>(readback.mapped);
    out_rgb.resize(size_t(rw) * rh * 3);
    for (unsigned y = 0; y < rh; ++y) {
      for (unsigned x = 0; x < rw; ++x) {
        std::uint8_t *o = &out_rgb[(size_t(y) * rw + x) * 3];
        if (rgba) {
          const unsigned char *p = src + (size_t(y) * rw + x) * 4;
          o[0] = p[0];
          o[1] = p[1];
          o[2] = p[2];
        } else {
          const unsigned p = src[(size_t(y) * rw + x) * 2] |
                             (src[(size_t(y) * rw + x) * 2 + 1] << 8);
          o[0] = ((p >> 11) & 31) * 255 / 31;
          o[1] = ((p >> 5) & 63) * 255 / 63;
          o[2] = (p & 31) * 255 / 31;
        }
      }
    }
  }
  // Return BGRA with the alpha needed to restore a held RGBA frame.
  void capture_fullres_bgra(std::vector<std::uint8_t> &out_rgba,
                            unsigned &out_w, unsigned &out_h) {
    finish_world();
    settle();
    out_w = rw;
    out_h = rh;
    if (!rw || !rh || !colorImage.image) {
      out_rgba.clear();
      return;
    }
    const VkDeviceSize bytes = VkDeviceSize(rw) * rh * (rgba ? 4 : 2);
    ensure(readback,
           std::max<VkDeviceSize>(bytes, VkDeviceSize(width) * height * 4),
           VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    check(f.vkResetCommandPool(device, commandPool, 0), "reset command pool");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(cmd, &bi), "begin capture");
    if (in_attachment_layout) {
      barrier(colorImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
      barrier(depthImage.image,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {rw, rh, 1};
    f.vkCmdCopyImageToBuffer(cmd, colorImage.image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             readback.buffer, 1, &c);
    VkMemoryBarrier hb{};
    hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0,
                           nullptr);
    check(f.vkEndCommandBuffer(cmd), "end capture");
    check(f.vkResetFences(device, 1, &fence), "reset fence");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    check(f.vkQueueSubmit(queue, 1, &si, fence), "submit capture");
    check(f.vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL),
          "wait capture");
    in_attachment_layout = false;
    const auto *src = static_cast<const unsigned char *>(readback.mapped);
    out_rgba.resize(size_t(rw) * rh * 4);
    mw34::capture::to_bgra({src, size_t(bytes)}, out_rgba,
                           rgba ? mw34::capture::ColorFormat::rgba8
                                : mw34::capture::ColorFormat::rgb565);
  }
  struct OutputResolution {
    unsigned width{}, height{};
  };
  OutputResolution remaster_resolution() const {
    const unsigned w = bridge_config.remaster_width,
                   h = bridge_config.remaster_height;
    if ((w || h) && (w < 640 || h < 480 || w > 7680 || h > 4320))
      throw std::runtime_error(
          "Remaster output resolution outside 640x480..7680x4320");
    return OutputResolution{w, h};
  }
  unsigned remaster_width() const { return remaster_resolution().width; }
  unsigned remaster_height() const { return remaster_resolution().height; }
  bool widescreen() const {
    return remaster_width() && std::uint64_t(remaster_width()) * 3 >
                                   std::uint64_t(remaster_height()) * 4;
  }
  bool fxaa_enabled() const { return quality_settings.fxaa; }
  unsigned anisotropy() const { return quality_settings.anisotropy; }
  mw34::display::Samples sample_grid() const {
    return {quality_settings.samples_x, quality_settings.samples_y};
  }
  // Called before the performance overlay/presentation if no HUD followed the
  // world.
  void finish_world() {
    select_world_target(false, false);
    if (!fxaa_enabled() || !quality_world_dirty || !colorImage.image)
      return;
    settle();
    check(f.vkResetCommandPool(device, commandPool, 0), "reset quality pool");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(cmd, &bi), "begin quality");
    record_fxaa();
    check(f.vkEndCommandBuffer(cmd), "end quality");
    check(f.vkResetFences(device, 1, &fence), "reset quality fence");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    check(f.vkQueueSubmit(queue, 1, &si, fence), "submit quality");
    scene_pending = true;
  }
  std::uint64_t fxaa_passes{}, af_samplers{}, mip_levels_uploaded{};
  std::uint64_t emission_draw_records{}, emission_payloads{};
  // Query the actual scene/presenter device, avoiding a throwaway render device
  // when enhanced texture banks need dimensions beyond the original 256 cap.
  unsigned
  max_texture_dimension(cc_mw3_re::platform::NativeWindowHandle window) {
    // The explicit bridge selects by adapter identity and needs no WSI window.
    if (!device && bridge_config.dxgi_present)
      init();
    if (!device) {
      // Capability queries can precede the first Flip. Select against the
      // actual window now, before texture banks bind us to an adapter.
      if (surface_source.create
              ? (window != nullptr)
              : cc_mw3_re::platform::is_window_valid(window)) {
        child = surface_source.create
                    ? nullptr
                    : cc_mw3_re::platform::make_present_child(window);
        if (!child && !surface_source.create)
          throw std::runtime_error(
              "Cannot create Vulkan capability-query window");
        presentParent = window;
        init(surface_source.create ? window : child);
      } else {
        return unbound_texture_dimension();
      }
    }
    if (!device)
      throw std::runtime_error(
          "Vulkan device unavailable for enhanced texture capabilities");
    VkPhysicalDeviceProperties p{};
    f.vkGetPhysicalDeviceProperties(gpu, &p);
    return p.limits.maxImageDimension2D;
  }
  bool gpu_models() const { return use_gpu_models; }
  bool use_native_geometry{};
  std::uint64_t retired_model_buffers{};
  std::uint64_t model_uploads{}, model_uploaded_bytes{}, model_draws{},
      world_gpu_draws{}, effect_light_peak{}, fixture_light_peak{};
  std::uint64_t normal_draws{};
  std::uint64_t presents{};
  bool presentInFlight{};
  // True when the cached texture for this guest surface already holds content
  // version `hash` at this size.
  bool current(std::uint64_t key, std::uint64_t hash, unsigned w,
               unsigned h) const {
    auto it = textures.find(key);
    return it != textures.end() && it->second.valid &&
           it->second.hash == hash && it->second.width == w &&
           it->second.height == h;
  }
  // Lazy residency (provider MW34_LAZY_RT): with upload_in false the target
  // images keep the content of the previous scene (no guest upload); with
  // readback_out false nothing is copied back and the images stay in attachment
  // layout until readback() copies them out on demand.
  bool in_attachment_layout{};
  bool frontend_canvas() const { return target_frontend; }
  float current_scale() const { return scale; }
  void request_scale(float s) { scale_req = s; }
  std::uint64_t readbacks{}, target_uploads{};
  std::uint64_t reduced_world_scenes{}, world_promotions{}, coverage_scenes{},
      coverage_draws{};
  unsigned raster_width() const { return rw; }
  unsigned raster_height() const { return rh; }
  // A scene without copy-back is not waited for; the next scene or copy-back
  // waits before reusing the command pool, upload, vertex and descriptor
  // resources, so the game computes the next scene while the GPU renders this
  // one.
  bool scene_pending{};
  const bool profile_cpu =
      bridge_config.cpu_profile || sdlfe::cpu_profile_requested();
  const bool profile_gpu = bridge_config.gpu_profile;
  VkQueryPool profile_queries{};
  bool profile_queries_pending{};
  unsigned profile_timestamp_bits{};
  float profile_timestamp_period{};
  std::uint64_t profile_gpu_ticks{}, profile_gpu_submissions{},
      profile_wait_ticks{};
  void collect_profile_queries() {
    if (!profile_queries_pending)
      return;
    std::uint64_t values[2]{};
    // The scene fence (or shutdown device-idle) completed before this read.
    check(f.vkGetQueryPoolResults(device, profile_queries, 0, 2, sizeof values,
                                  values, sizeof(std::uint64_t),
                                  VK_QUERY_RESULT_64_BIT),
          "completed scene timestamp results");
    const auto mask = profile_timestamp_bits == 64
                          ? ~std::uint64_t{}
                          : (std::uint64_t{1} << profile_timestamp_bits) - 1;
    profile_gpu_ticks += (values[1] - values[0]) & mask;
    ++profile_gpu_submissions;
    profile_queries_pending = false;
  }
  void settle() {
    if (!scene_pending)
      return;
    sdlfe::CpuInterval interval(profile_cpu ? &profile_wait_ticks : nullptr);
    check(f.vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL),
          "wait previous scene");
    collect_profile_queries();
    scene_pending = false;
  }
  // Record uploads, ordered clears/draws, and optional copy-back as one
  // submission. settle() protects the reusable staging buffers, descriptors and
  // command pool. With copy-back disabled, the GPU may still be working when
  // this call returns.
  void render(Scene &s, bool upload_in = true, bool readback_out = true) {
    settle();
    // The previous GPU submission has completed. Indexed geometry owns no
    // live engine pointers and can retire once its source and packets expire.
    for (auto it = model_buffers.begin(); it != model_buffers.end();) {
      auto &buffer = it->second;
      if (!buffer.index_count || !buffer.source.expired()) {
        ++it;
        continue;
      }
      for (Buffer *allocation : {&buffer.local, &buffer.staging,
                                 &buffer.indices, &buffer.index_staging}) {
        if (allocation->buffer)
          f.vkDestroyBuffer(device, allocation->buffer, nullptr);
        if (allocation->memory)
          f.vkFreeMemory(device, allocation->memory, nullptr);
      }
      sunChecked.erase(it->first);
      ++retired_model_buffers;
      it = model_buffers.erase(it);
    }
    if (!device)
      init();
    if (s.width != width || s.height != height || scale_req != scale ||
        s.frontend_canvas != target_frontend)
      target(s.width, s.height, s.frontend_canvas);
    if (bridge_config.temporal_inputs) {
      if (s.frontend_canvas)
        camera_history.invalidate(mw34::temporal::Reset::transition);
      if (upload_in && !(leading_full_clear(s) & 1))
        camera_history.invalidate(mw34::temporal::Reset::external_write);
      if (s.guest_surface_write) {
        // A complete clear before this frame's first world draw is
        // normal frame initialization. Partial/interleaved fills are
        // unsupported temporal writes and poison this frame.
        bool initial_clear =
            !camera_history.pending() && s.draws.empty() && !s.clears.empty();
        for (const auto &clear : s.clears) {
          initial_clear &=
              clear.flags && !(clear.flags & ~3U) && clear.rects.size() == 1;
          if (clear.rects.size() == 1) {
            const auto &rectangle = clear.rects[0];
            initial_clear &= rectangle.offset.x == 0 &&
                             rectangle.offset.y == 0 &&
                             rectangle.extent.width == s.width &&
                             rectangle.extent.height == s.height;
          }
        }
        if (!initial_clear)
          camera_history.invalidate(mw34::temporal::Reset::external_write);
      }
      // Observe only this original submission. Internal shading/coverage
      // replays never pass through this entry point.
      for (const auto &draw : s.draws) {
        // P2 history belongs to admitted static scenery. Cockpit/effect
        // projections sharing a camera address are not that world view.
        if (!draw.camera_view || !draw.model || !draw.model->world)
          continue;
        if (draw.camera_view->projection != draw.model->projection) {
          camera_history.invalidate(mw34::temporal::Reset::invalid);
          continue;
        }
        auto snapshot =
            std::make_shared<mw34::temporal::Snapshot>(*draw.camera_view);
        const unsigned output_width =
            reduced_world_active ? alternate_width : rw;
        const unsigned output_height =
            reduced_world_active ? alternate_height : rh;
        const unsigned world_width =
            worldColor.image ? (reduced_world_active ? rw : alternate_width)
                             : rw;
        const unsigned world_height =
            worldColor.image ? (reduced_world_active ? rh : alternate_height)
                             : rh;
        snapshot->sizes = {s.width,       s.height,    output_width,
                           output_height, world_width, world_height};
        camera_history.observe(std::move(snapshot));
      }
    }
    if (can_reconstruct_world(s, upload_in, readback_out))
      render_reconstructed_world(s);
    else
      render_scene(s, upload_in, readback_out);
  }

private:
  void render_scene(Scene &s, bool upload_in, bool readback_out) {
    settle();
    if (!device)
      init();
    if (s.width != width || s.height != height || scale_req != scale ||
        s.frontend_canvas != target_frontend)
      target(s.width, s.height, s.frontend_canvas);
    if (worldColor.image &&
        reconstruction_stage == ReconstructionStage::normal) {
      // A depth-only frame may retain full-resolution HUD color. Reduce
      // only when a full color clear replaces every retained pixel.
      const auto cleared = leading_full_clear(s);
      if (!s.hud && !reduced_world_active && !s.draws.empty() &&
          fxaa_enabled() && !retained_color_warning) {
        std::fprintf(
            stderr,
            "World resolution: non-reconstructed submissions retain output "
            "resolution; "
            "world FXAA applies separately to reconstructed opaque batches\n");
        retained_color_warning = true;
      }
      const bool restart = !s.hud && !s.frontend_canvas && (cleared & 1);
      if (restart)
        quality_world_dirty = false;
      // An imported guest image can contain UI or software-rendered
      // pixels. Preserve its full output resolution unless this same
      // submission proves a complete color replacement with a clear.
      if (s.hud || s.guest_surface_write || readback_out || s.frontend_canvas ||
          (upload_in && !restart))
        select_world_target(false, false);
      else if (restart)
        select_world_target(true, upload_in);
      settle();
    }
    if (upload_in)
      quality_world_dirty = false;
    const VkDeviceSize pixels = VkDeviceSize(width) * height;
    // The host-side buffers carry pixels only for an upload or a readback; a
    // GPU-resident scene leaves them empty.
    if ((upload_in || readback_out) &&
        (s.color.size() != pixels || s.depth.size() != pixels))
      throw std::runtime_error("Scene buffers do not match the target");
    const VkDeviceSize colorBytes = pixels * (rgba ? 4 : 2),
                       depthOffset = mw34::depth::offset(size_t(colorBytes)),
                       depthBytes = pixels * sizeof(float);
    ensure(upload, depthOffset + depthBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    ensure(readback, depthOffset + VkDeviceSize(rw) * rh * sizeof(float),
           VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    ensure(vertexBuffer, std::max<VkDeviceSize>(s.vertices.size(), 32),
           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    auto *up = static_cast<unsigned char *>(upload.mapped);
    if (upload_in)
      ++target_uploads;
    if (!upload_in) {
    } else if (rgba) {
      for (VkDeviceSize i = 0; i < pixels; ++i) {
        const unsigned p = s.color[i];
        const std::uint32_t r = ((p >> 11) & 31) * 255 / 31,
                            g = ((p >> 5) & 63) * 255 / 63,
                            b = (p & 31) * 255 / 31;
        const std::uint32_t v = r | g << 8 | b << 16 | 0xff000000u;
        std::memcpy(up + 4 * i, &v, 4);
      }
    } else
      std::memcpy(up, s.color.data(), colorBytes);
    if (upload_in)
      mw34::depth::upload(up + depthOffset, s.depth.data(), size_t(pixels));
    const auto &environment = lighting_settings;
    if (!world_scene_prepared)
      apply_atmosphere(s, environment);
    if (!s.vertices.empty())
      std::memcpy(vertexBuffer.mapped, s.vertices.data(), s.vertices.size());
    if (lighting_settings.enabled) {
      const auto count = s.vertices.size() / 32;
      ensure(normalBuffer, std::max<VkDeviceSize>(count * 12, 12),
             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
      std::memset(normalBuffer.mapped, 0, size_t(normalBuffer.size));
      for (const auto &draw : s.draws) {
        if (!draw.lit_normals)
          continue;
        if (draw.lit_normals->size() != draw.count || draw.first > count ||
            draw.count > count - draw.first)
          throw std::runtime_error("Clipped smooth-normal range mismatch");
        std::memcpy(static_cast<unsigned char *>(normalBuffer.mapped) +
                        size_t(draw.first) * 12,
                    draw.lit_normals->data(), size_t(draw.count) * 12);
      }
    }
    // Collect each immutable geometry upload once even if several instances use
    // it. std::map keeps pointers stable while additional model entries are
    // inserted.
    std::vector<ModelBuffer *> model_pending;
    for (const auto &d : s.draws)
      if (d.model) {
        if (!gpu_models() || !d.model->geometry)
          throw std::runtime_error(
              "GPU model draw without the enabled model path");
        auto &b = model_buffer(*d.model->geometry);
        if (!b.uploaded && std::find(model_pending.begin(), model_pending.end(),
                                     &b) == model_pending.end())
          model_pending.push_back(&b);
      }
    // Warmup shares the original immutable upload and completion path.
    // It adds no draw and retains exact geometry identity checks.
    for (const auto &geometry : s.model_warmup) {
      if (!gpu_models() || !geometry)
        throw std::runtime_error("Invalid immutable model warmup");
      auto &b = model_buffer(*geometry);
      if (!b.uploaded && std::find(model_pending.begin(), model_pending.end(),
                                   &b) == model_pending.end())
        model_pending.push_back(&b);
    }
    // Textures: content-hashed cache keyed by guest surface; changed or new
    // ones are uploaded.
    VkDeviceSize texBytes = 0;
    std::vector<std::pair<Tex *, const TextureRef *>> changed;
    std::vector<Tex *> bound(s.textures.size());
    for (size_t i = 0; i < s.textures.size(); ++i) {
      const auto &t = s.textures[i];
      auto &c = textures[t.key];
      if (c.image && (c.width != t.width || c.height != t.height))
        destroy(c);
      if (!c.image)
        create(c, t.width, t.height);
      if (c.hash != t.hash || !c.valid) {
        if ((t.format == TexFormat::rgba8888
                 ? t.pixels32.size()
                 : t.pixels.size()) != size_t(t.width) * t.height)
          throw std::runtime_error(
              "Vulkan scene renderer: changed texture without pixels");
        changed.push_back({&c, &t});
        texBytes += VkDeviceSize(t.width) * t.height * 4;
      }
      bound[i] = &c;
    }
    ensure(texUpload, std::max<VkDeviceSize>(texBytes, 4),
           VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    {
      auto *tp = static_cast<unsigned char *>(texUpload.mapped);
      for (auto &[c, t] : changed) {
        if (t->format == TexFormat::rgba8888) {
          std::memcpy(tp, t->pixels32.data(), t->pixels32.size() * 4);
          tp += VkDeviceSize(t->width) * t->height * 4;
          continue;
        }
        const bool staged = !bridge_config.texture_staging.empty() &&
                            bridge_config.texture_staging != "0";
        if (staged) {
          const unsigned format = t->format == TexFormat::rgb565     ? 0
                                  : t->format == TexFormat::argb1555 ? 1
                                                                     : 2;
          mw34::texture_staging::copy(tp, t->pixels.data(), t->pixels.size(),
                                      format);
          tp += VkDeviceSize(t->width) * t->height * 4;
          continue;
        }
        for (size_t i = 0; i < t->pixels.size(); ++i) {
          const unsigned p = t->pixels[i];
          std::uint32_t r, g, b, a;
          if (t->format == TexFormat::rgb565) {
            r = ((p >> 11) & 31) * 255 / 31;
            g = ((p >> 5) & 63) * 255 / 63;
            b = (p & 31) * 255 / 31;
            a = 255;
          } else if (t->format == TexFormat::argb1555) {
            r = ((p >> 10) & 31) * 255 / 31;
            g = ((p >> 5) & 31) * 255 / 31;
            b = (p & 31) * 255 / 31;
            a = (p & 0x8000) ? 255 : 0;
          } else {
            r = ((p >> 8) & 15) * 17;
            g = ((p >> 4) & 15) * 17;
            b = (p & 15) * 17;
            a = ((p >> 12) & 15) * 17;
          }
          const std::uint32_t v = r | g << 8 | b << 16 | a << 24;
          std::memcpy(tp + 4 * i, &v, 4);
        }
        tp += VkDeviceSize(t->width) * t->height * 4;
      }
    }
    ShadowFrame localShadowFrame;
    const ShadowFrame *shadowFrame = &localShadowFrame;
    mw34::effects::Uniform localFixtureLights{};
    const mw34::effects::Uniform *fixtureLights = &localFixtureLights;
    if (world_scene_prepared) {
      if (!world_shadow_scene)
        throw std::runtime_error(
            "Reconstructed scene is missing its shadow source");
      if (!world_shadow_inputs_ready) {
        world_shadow_frame = prepare_shadow(*world_shadow_scene, environment);
        // Batching owns the full captured vertex buffer while the source
        // scene retains every draw needed to prepare shared shadow maps.
        world_fixture_lights =
            prepare_fixture_shadows(*world_shadow_scene, bound, s.vertices);
        world_shadow_inputs_ready = true;
      }
      shadowFrame = &world_shadow_frame;
      fixtureLights = &world_fixture_lights;
    } else {
      localShadowFrame = prepare_shadow(s, environment);
      localFixtureLights = prepare_fixture_shadows(s, bound, s.vertices);
    }
    // Upload memory can be write-combined: never read it for CPU comparisons.
    mw34::submission::ExactUniformRuns<mw34::sun::Uniform> uniformRuns;
    std::vector<unsigned> sunUniformIds(s.draws.size());
    std::vector<mw34::tiled::View> tiledFrameViews;
    std::vector<unsigned> tiledDrawViews(s.draws.size(), ~0u);
    bool sunScene = false;
    if (lighting_settings.enabled) {
      if (s.draws.size() > 0xffffffffULL / sunStride)
        throw std::runtime_error(
            "Sun uniform offsets exceed the Vulkan dynamic-offset range");
      ensure(sunBuffer,
             std::max<VkDeviceSize>(sunStride, sunStride * s.draws.size()),
             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
      ensure(effectBuffer, sizeof(mw34::effects::Uniform),
             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
      const mw34::effects::Uniform noEffects{};
      const auto &fx = s.hud || s.frontend_canvas ? noEffects : *fixtureLights;
      std::memcpy(effectBuffer.mapped, &fx, sizeof(fx));
      mw34::tiled::validate_lights(fx);
      if (effects_settings.enabled && fx.params[0] > 0) {
        for (size_t at = 0; at < s.draws.size(); ++at) {
          const auto &d = s.draws[at];
          if (s.hud || s.frontend_canvas || d.sky || !sun_lit(d))
            continue;
          const auto camera =
              d.model ? mw34::tiled::compose_affine(d.model->camera,
                                                    mw34::tiled::inverse_affine(d.model->sun_world))
                      : mw34::tiled::inverse_affine(d.sun_tl->camera_world);
          const auto &projection = d.model ? d.model->projection : d.sun_tl->projection;
          const auto v = mw34::tiled::draw_view(camera, projection, width, height, rw, rh);
          auto found =
              std::find_if(tiledFrameViews.begin(), tiledFrameViews.end(), [&](const auto &prior) {
                return std::memcmp(&prior, &v, sizeof(v)) == 0;
              });
          tiledDrawViews[at] = unsigned(found - tiledFrameViews.begin());
          if (found == tiledFrameViews.end()) {
            if (tiledFrameViews.size() == 64)
              throw std::runtime_error("Too many tiled camera views");
            tiledFrameViews.push_back(v);
          }
        }
      }
      prepare_tiled(tiledFrameViews);
      effect_light_peak =
          std::max(effect_light_peak, std::uint64_t(fx.params[0]));
      fixture_light_peak =
          std::max(fixture_light_peak, std::uint64_t(fx.params[2]));
      effect_light_peak =
          std::max(effect_light_peak, std::uint64_t(fx.params[0]));
      fixture_light_peak =
          std::max(fixture_light_peak, std::uint64_t(fx.params[2]));
      for (size_t at = 0; at < s.draws.size(); ++at)
        if (!s.hud && sun_lit(s.draws[at])) {
          const auto &draw = s.draws[at];
          mw34::sun::Uniform u;
          if (draw.model) {
            const auto &instance = *draw.model;
            const auto &geometry = *instance.geometry;
            auto previous = sunChecked.find(geometry.key);
            if (previous == sunChecked.end() ||
                previous->second != geometry.hash) {
              for (const auto &v : geometry.vertices) {
                const double n = double(v.nx) * v.nx + double(v.ny) * v.ny +
                                 double(v.nz) * v.nz;
                if (!std::isfinite(n) || n < 1e-12)
                  throw std::runtime_error(
                      "Sun prototype has an invalid source normal");
              }
              sunChecked[geometry.key] = geometry.hash;
            }
            u = mw34::sun::uniform(instance.sun_normal, environment);
            u.world = instance.sun_world;
            u.geometry_fog0 = instance.geometry_fog0;
            u.geometry_fog1 = instance.geometry_fog1;
            if (instance.world)
              u.extra[1] = environment.ground_normal_strength;
          } else {
            const auto &light = *draw.sun_tl;
            u = mw34::sun::uniform(light.camera_world, environment);
            u.projection = {light.projection[1], light.projection[2],
                            light.projection[3], light.projection[4]};
            u.extra = {light.projection[0], environment.ground_normal_strength,
                       environment.shadow_debug ? 1.f : 0.f, 1};
          }
          if (draw.geometry_debug_normals)
            u.ambient[3] = 1;
          if (environment.normals && draw.surface_parameters[3] > .5f &&
              draw.surface_eye[3] > .5f && !draw.sky && !draw.key.blend &&
              !draw.condition_schematic && !draw.geometry_debug_normals) {
            u.surface_parameters = draw.surface_parameters;
            u.surface_eye = draw.surface_eye;
          }
          if (skyEyeValid) {
            const auto &e =
                draw.sun_tl && !draw.sky ? draw.sun_tl->camera_world : skyEye;
            u.sky_eye = {e[3], e[7], e[11], 1};
          }
          if (draw.sky)
            u.extra[3] = 1.f + float(draw.sky);
          if (const char *dbg = bridge_config.sky_debug.empty()
                                    ? nullptr
                                    : bridge_config.sky_debug.c_str())
            u.sky_blend[3] =
                *dbg == '3'
                    ? 3.f + float(draw.sky     ? 3
                                  : draw.model ? (draw.model->world ? 1 : 2)
                                               : 0)
                : *dbg == '2' ? 2.f
                              : 1.f + float(at % 13) / 13.f;
          if (shadowFrame->active) {
            const auto &sun = environment;
            for (unsigned i = 0; i < 12; ++i)
              u.shadow[i] = float(shadowFrame->view.rows[i]);
            u.shadow_params = {
                1.f, float(shadowFrame->view.texel),
                float(sun.shadow_bias * shadowFrame->view.depth_scale),
                sun.shadow_darkness};
          }
          if (draw.normal_texture < 0)
            u.extra[1] = 0;
          if (draw.normal_texture >= 0 &&
              ((draw.detail_role == 3 && draw.sky == 1) ||
               (draw.detail_role > 0 && draw.detail_role < 3 && !draw.sky &&
                (draw.sun_tl || (draw.model && draw.model->world)))))
            mw34::detail::uniform(u, draw.detail_role, s.detail_time);
          u.detail_motion[3] =
              draw.key.blend ? -1.f : environment.night_world_gain;
          s.emissions.apply(draw.emission_index, u);
          if (tiledDrawViews[at] != ~0u) {
            const auto id = tiledDrawViews[at];
            u.tiled_lights = {tiledFrameViews[id].extent[2], tiledFrameViews[id].extent[3],
                              tiledOffsets[id], 1};
          }
          sunUniformIds[at] = uniformRuns.identify(u);
          std::memcpy(static_cast<unsigned char *>(sunBuffer.mapped) +
                          at * sunStride,
                      &u, sizeof(u));
          sunScene = true;
        }
    }
    emission_draw_records += s.draws.size();
    emission_payloads += s.emissions.size();
    // Descriptor sets: one per distinct (texture, sampler).
    if (descriptorCapacity < s.draws.size() + fixtureFrameCasters.size() + 1)
      pool(unsigned(s.draws.size() + fixtureFrameCasters.size() + 64));
    check(f.vkResetDescriptorPool(device, descriptorPool, 0),
          "reset descriptor pool");
    std::map<std::pair<Tex *, unsigned>, VkDescriptorSet> sets;
    std::vector<VkDescriptorSet> drawSets(s.draws.size());
    for (size_t i = 0; i < s.draws.size(); ++i) {
      const auto &d = s.draws[i];
      Tex *t = d.texture >= 0 ? bound[size_t(d.texture)] : &white;
      auto it = sets.find({t, d.sampler});
      if (it == sets.end()) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &setLayout;
        VkDescriptorSet set{};
        check(f.vkAllocateDescriptorSets(device, &ai, &set), "descriptor set");
        VkDescriptorImageInfo info{sampler(d.sampler), t->view,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &info;
        f.vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        if (reconstruction_stage == ReconstructionStage::coverage) {
          VkDescriptorImageInfo reconstructed{
              sampler(3U | (3U << 4) | (3U << 8)), reconstructedColor.view,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
          w.dstBinding = 1;
          w.pImageInfo = &reconstructed;
          f.vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }
        it = sets.emplace(std::make_pair(t, d.sampler), set).first;
      }
      drawSets[i] = it->second;
    }
    fixture_shadow_sets(sets);
    std::map<std::pair<Tex *, unsigned>, VkDescriptorSet> normalSets;
    std::vector<VkDescriptorSet> drawNormalSets(s.draws.size());
    if (lighting_settings.enabled) {
      for (size_t i = 0; i < s.draws.size(); ++i) {
        const auto &d = s.draws[i];
        const auto &normal_settings = environment;
        const float normal_strength =
            d.model && !d.model->world ? normal_settings.normal_strength
                                       : normal_settings.ground_normal_strength;
        const bool normal_active =
            (normal_strength > 0 || mw34::detail::enabled(d.detail_role));
        Tex *t = (normal_active && d.normal_texture >= 0 &&
                  size_t(d.normal_texture) < bound.size())
                     ? bound[size_t(d.normal_texture)]
                     : &flatNormal;
        const unsigned normal_sampler =
            d.detail_role > 0 && d.detail_role < 3 ? 0x10113U : d.sampler;
        auto it = normalSets.find({t, normal_sampler});
        if (it == normalSets.end()) {
          VkDescriptorSetAllocateInfo ai{};
          ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
          ai.descriptorPool = descriptorPool;
          ai.descriptorSetCount = 1;
          ai.pSetLayouts = &normalSetLayout;
          VkDescriptorSet set{};
          check(f.vkAllocateDescriptorSets(device, &ai, &set),
                "normal descriptor set");
          VkDescriptorImageInfo info{sampler(normal_sampler), t->view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
          VkWriteDescriptorSet w{};
          w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          w.dstSet = set;
          w.descriptorCount = 1;
          w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          w.pImageInfo = &info;
          f.vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
          it = normalSets.emplace(std::make_pair(t, normal_sampler), set).first;
        }
        drawNormalSets[i] = it->second;
      }
    }
    VkDescriptorSet sunSet{};
    if (sunScene) {
      VkDescriptorSetAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      ai.descriptorPool = descriptorPool;
      ai.descriptorSetCount = 1;
      ai.pSetLayouts = &sunSetLayout;
      check(f.vkAllocateDescriptorSets(device, &ai, &sunSet),
            "sun descriptor set");
      VkDescriptorBufferInfo info{sunBuffer.buffer, 0,
                                  sizeof(mw34::sun::Uniform)};
      VkDescriptorImageInfo shadowInfo{
          shadowSampler, shadowView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      VkDescriptorBufferInfo effectInfo{effectBuffer.buffer, 0,
                                        sizeof(mw34::effects::Uniform)};
      std::array<VkWriteDescriptorSet, 5> w{};
      for (auto &x : w) {
        x.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        x.dstSet = sunSet;
        x.descriptorCount = 1;
      }
      w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      w[0].pBufferInfo = &info;
      w[1].dstBinding = 1;
      w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w[1].pImageInfo = &shadowInfo;
      w[2].dstBinding = 2;
      w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w[2].pBufferInfo = &effectInfo;
      VkDescriptorImageInfo fixtureInfo{
          fixtureShadowSampler, fixtureShadowView,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      w[3].dstBinding = 3;
      w[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w[3].pImageInfo = &fixtureInfo;
      VkDescriptorBufferInfo tileInfo{tiledLists.buffer, 0, tiledLists.size};
      w[4].dstBinding = 4;
      w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[4].pBufferInfo = &tileInfo;
      f.vkUpdateDescriptorSets(device, 5, w.data(), 0, nullptr);
    }
    check(f.vkResetCommandPool(device, commandPool, 0), "reset command pool");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(cmd, &bi), "begin");
    record_tiled(tiledFrameViews);
    if (profile_queries) {
      f.vkCmdResetQueryPool(cmd, profile_queries, 0, 2);
      f.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            profile_queries, 0);
    }
    // Static model geometry reaches device-local memory once. Subsequent
    // frames send only controller matrices and projection constants.
    for (auto *b : model_pending) {
      VkBufferCopy c{0, 0, b->local.size};
      f.vkCmdCopyBuffer(cmd, b->staging.buffer, b->local.buffer, 1, &c);
      if (b->indices.buffer) {
        VkBufferCopy indices{0, 0, b->indices.size};
        f.vkCmdCopyBuffer(cmd, b->index_staging.buffer, b->indices.buffer, 1,
                          &indices);
      }
      b->uploaded = true;
      ++model_uploads;
      model_uploaded_bytes += c.size;
    }
    if (!model_pending.empty()) {
      VkMemoryBarrier mb{};
      mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
      mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      mb.dstAccessMask =
          VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
      f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &mb, 0,
                             nullptr, 0, nullptr);
    }
    {
      VkDeviceSize off = 0;
      for (auto &[c, t] : changed) {
        barrier(c->image,
                c->valid ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                         : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                c->valid ? VK_ACCESS_SHADER_READ_BIT : 0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                c->valid ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                         : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0,
                c->levels);
        copyIn(texUpload.buffer, off, c->image, t->width, t->height,
               VK_IMAGE_ASPECT_COLOR_BIT);
        generate_mips(*c);
        c->hash = t->hash;
        c->valid = true;
        ++uploads;
        off += VkDeviceSize(t->width) * t->height * 4;
      }
    }
    if (!upload_in) {
      // Keep the resident content: from the layout the previous scene left
      // (attachment, or transfer source after a copy-out).
      const bool att = in_attachment_layout;
      barrier(colorImage.image,
              att ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                  : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              att ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                  : VK_ACCESS_TRANSFER_READ_BIT,
              VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              att ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                  : VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT);
      barrier(depthImage.image,
              att ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                  : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              att ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                  : VK_ACCESS_TRANSFER_READ_BIT,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              att ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                  : VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_IMAGE_ASPECT_DEPTH_BIT);
    } else {
      barrier(colorImage.image, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
      barrier(depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
      copy_in_scaled(depthOffset);
      barrier(colorImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT);
      barrier(depthImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    in_attachment_layout = true;
    // Apply world AA before the first HUD pass so text and overlays stay sharp.
    if ((s.hud || (s.guest_surface_write && worldColor.image)) &&
        quality_world_dirty && fxaa_enabled())
      record_fxaa();
    if (!world_scene_prepared || !world_shadow_recorded) {
      if (shadowFrame->active && !shadowFrame->reused)
        record_shadow(*shadowFrame);
      record_fixture_shadows(*fixtureLights);
      if (world_scene_prepared)
        world_shadow_recorded = true;
    }
    VkRenderPassBeginInfo rb{};
    rb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rb.renderPass = renderPass;
    rb.framebuffer = framebuffer;
    rb.renderArea = {{0, 0}, {rw, rh}};
    f.vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, float(rw), float(rh), 0, 1};
    f.vkCmdSetViewport(cmd, 0, 1, &vp);
    VkDeviceSize zero = 0;
    f.vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &zero);
    VkBuffer boundVertices = vertexBuffer.buffer;
    // Coalesce only adjacent complete TL triangle lists. All effective
    // GPU inputs must match bit-for-bit; never cross clears or model draws.
    // The shaders use vertex attributes, not primitive/draw IDs.
    const bool combine = bridge_config.tl_combine;
    auto same_tl = [&](unsigned a, unsigned b) {
      const auto &left = s.draws[a];
      const auto &right = s.draws[b];
      if (left.model || right.model || left.key.points || right.key.points ||
          !left.count || !right.count || left.count % 3 || right.count % 3 ||
          left.key.packed() != right.key.packed() ||
          drawSets[a] != drawSets[b] ||
          std::memcmp(&left.push, &right.push, sizeof(Push)) != 0 ||
          std::memcmp(&left.scissor, &right.scissor, sizeof(VkRect2D)) != 0)
        return false;
      const bool lit = !s.hud && sun_lit(left);
      if (lit != (!s.hud && sun_lit(right)))
        return false;
      if (lit && (drawNormalSets[a] != drawNormalSets[b] ||
                  sunUniformIds[a] != sunUniformIds[b]))
        return false;
      return true;
    };
    // Draw and clear indices refer to separate arrays, but their ordering is
    // shared.
    for (size_t orderAt = 0; orderAt < s.order.size(); ++orderAt) {
      const auto &o = s.order[orderAt];
      if (o.clear) {
        const auto &c = s.clears[o.index];
        std::vector<VkClearAttachment> at;
        if (c.flags & 1) {
          VkClearAttachment a{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
          std::memcpy(a.clearValue.color.float32, c.color, 16);
          at.push_back(a);
        }
        if ((c.flags & 2) && s.has_depth) {
          VkClearAttachment a{VK_IMAGE_ASPECT_DEPTH_BIT, 0, {}};
          a.clearValue.depthStencil.depth = c.z;
          at.push_back(a);
        }
        std::vector<VkClearRect> rects;
        for (const auto &r : c.rects)
          rects.push_back({scaled(r), 0, 1});
        if (!at.empty() && !rects.empty())
          f.vkCmdClearAttachments(cmd, unsigned(at.size()), at.data(),
                                  unsigned(rects.size()), rects.data());
        continue;
      }
      const auto &d = s.draws[o.index];
      if (!d.count || !d.scissor.extent.width || !d.scissor.extent.height)
        continue;
      unsigned vertexCount = d.count, logicalDraws = 1;
      if (combine && !d.model && !d.key.points && d.count % 3 == 0) {
        while (orderAt + 1 < s.order.size()) {
          const auto &next = s.order[orderAt + 1];
          if (next.clear)
            break;
          const auto &candidate = s.draws[next.index];
          if (std::uint64_t(d.first) + vertexCount != candidate.first ||
              std::uint64_t(vertexCount) + candidate.count > 0xffffffffULL ||
              !same_tl(o.index, next.index))
            break;
          vertexCount += candidate.count;
          ++logicalDraws;
          ++orderAt;
        }
      }
      const bool model = bool(d.model), lit = !s.hud && sun_lit(d);
      const VkPipelineLayout layout =
          (model || lit) ? modelLayout : pipelineLayout;
      f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline(d.key, model, d.model && d.model->world, lit,
                                   d.model && d.model->gpu_vertices,
                                   (d.push.mode[3] & 0x80000000U) != 0));
      f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                                1, &drawSets[o.index], 0, nullptr);
      if (lit) {
        if (d.normal_texture >= 0)
          ++normal_draws;
        const std::uint32_t offset = std::uint32_t(o.index * sunStride);
        f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                  1, 1, &sunSet, 1, &offset);
        f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                  2, 1, &drawNormalSets[o.index], 0, nullptr);
        if (!d.model)
          sun_tl_draws += logicalDraws;
        else if (d.model->world)
          ++sun_world_draws;
        else
          ++sun_model_draws;
      }
      {
        const VkRect2D sc = scaled(d.scissor);
        f.vkCmdSetScissor(cmd, 0, 1, &sc);
      }
      if (model) {
        const auto &i = *d.model;
        const auto &b = model_buffers.at(i.geometry->key);
        const bool indexed = !i.geometry->indices.empty();
        if (!b.uploaded ||
            (indexed ? (d.first > b.index_count ||
                        d.count > b.index_count - d.first)
                     : ((!i.world && d.first) || d.count != b.count)))
          throw std::runtime_error("GPU model draw range mismatch");
        if (indexed)
          f.vkCmdBindIndexBuffer(cmd, b.indices.buffer, 0,
                                 VK_INDEX_TYPE_UINT32);
        if (i.world) {
          const VkBuffer buffers[2]{b.local.buffer, vertexBuffer.buffer};
          const VkDeviceSize offsets[2]{0, VkDeviceSize(d.first) * 32};
          f.vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
          boundVertices = b.local.buffer;
        } else if (boundVertices != b.local.buffer) {
          f.vkCmdBindVertexBuffers(cmd, 0, 1, &b.local.buffer, &zero);
          boundVertices = b.local.buffer;
        }
        ModelPush mp{};
        mp.base = d.push;
        mp.camera = i.camera;
        mp.base.target[2] = i.projection[0];
        mp.base.target[3] = i.projection[5];
        mp.projection = {i.projection[1], i.projection[2], i.projection[3],
                         i.projection[4]};
        if (!i.world) {
          mp.base.factor[0] = float((i.color >> 16) & 255) / 255.f;
          mp.base.factor[1] = float((i.color >> 8) & 255) / 255.f;
          mp.base.factor[2] = float(i.color & 255) / 255.f;
          mp.base.factor[3] =
              i.gpu_vertices ? i.geometry_near : i.diagnostic_fog_factor;
        }
        f.vkCmdPushConstants(cmd, layout,
                             VK_SHADER_STAGE_VERTEX_BIT |
                                 VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(ModelPush), &mp);
        ++model_draws;
        if (i.world)
          ++world_gpu_draws;
      } else { // A model draw binds its own VBO; ordinary TL draws must restore
               // theirs.
        if (lit) {
          const VkBuffer buffers[2]{vertexBuffer.buffer, normalBuffer.buffer};
          const VkDeviceSize offsets[2]{0, 0};
          f.vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
          boundVertices = vertexBuffer.buffer;
        } else if (boundVertices != vertexBuffer.buffer) {
          f.vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &zero);
          boundVertices = vertexBuffer.buffer;
        }
        f.vkCmdPushConstants(cmd, layout,
                             VK_SHADER_STAGE_VERTEX_BIT |
                                 VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(Push), &d.push);
      }
      if (d.model && !d.model->geometry->indices.empty())
        f.vkCmdDrawIndexed(cmd, vertexCount, 1, d.first, 0, 0);
      else
        f.vkCmdDraw(cmd, vertexCount, 1,
                    d.model && d.model->world ? 0 : d.first, 0);
      if (reconstruction_stage == ReconstructionStage::coverage)
        coverage_draws += logicalDraws;
      else {
        draws += logicalDraws;
        ++submitted_draws;
        combined_draws += logicalDraws - 1;
      }
    }
    f.vkCmdEndRenderPass(cmd);
    const bool writes_color =
        std::any_of(s.draws.begin(), s.draws.end(),
                    [](const Draw &draw) { return !draw.depth_only; });
    if (!s.hud && writes_color && (!worldColor.image || reduced_world_active))
      quality_world_dirty = true;
    if (reduced_world_active)
      ++reduced_world_scenes;
    if (readback_out) {
      barrier(colorImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
      barrier(depthImage.image,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
      copy_out_scaled(mw34::depth::offset(size_t(colorBytes)));
      VkMemoryBarrier hb{};
      hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
      hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
      f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr,
                             0, nullptr);
    }
    in_attachment_layout = !readback_out;
    if (profile_queries)
      f.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            profile_queries, 1);
    check(f.vkEndCommandBuffer(cmd), "end");
    check(f.vkResetFences(device, 1, &fence), "reset fence");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    check(f.vkQueueSubmit(queue, 1, &si, fence), "submit");
    profile_queries_pending = profile_queries != VK_NULL_HANDLE;
    fixture_atlas_submitted();
    if (reconstruction_stage != ReconstructionStage::coverage)
      ++scenes;
    if (readback_out) {
      check(f.vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL),
            "wait");
      collect_profile_queries();
      copy_back(s);
    } else
      scene_pending = true;
  }

public:
  // The frontend drains its render worker before updating this CPU snapshot.
  // Diagnostic readback uses the same producer and synchronization as rendering.
  std::vector<std::vector<uint32_t>> probe_tiled_lists(const std::vector<mw34::tiled::View> &views,
                                                       const mw34::effects::Uniform &lights) {
    settle();
    if (!device)
      init();
    mw34::tiled::validate_lights(lights);
    ensure(effectBuffer, sizeof(lights), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    std::memcpy(effectBuffer.mapped, &lights, sizeof(lights));
    prepare_tiled(views);
    check(f.vkResetCommandPool(device, commandPool, 0), "reset tiled probe pool");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(cmd, &bi), "begin tiled probe");
    record_tiled(views);
    check(f.vkEndCommandBuffer(cmd), "end tiled probe");
    check(f.vkResetFences(device, 1, &fence), "reset tiled probe fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    check(f.vkQueueSubmit(queue, 1, &submit, fence), "submit tiled probe");
    scene_pending = true;
    settle();
    std::vector<std::vector<uint32_t>> result;
    for (size_t i = 0; i < views.size(); ++i) {
      result.emplace_back(size_t(tiledLengths[i] / 4));
      std::memcpy(result.back().data(),
                  static_cast<char *>(tiledLists.mapped) + VkDeviceSize(tiledOffsets[i]) * 4,
                  size_t(tiledLengths[i]));
    }
    return result;
  }
  // The frontend drains its render worker before updating this CPU snapshot.
  // Pipeline topology is fixed at construction; only mission uniforms change.
  void update_lighting_settings(const mw34::sun::Settings &next) {
    if (next.enabled != lighting_settings.enabled ||
        next.normals != lighting_settings.normals)
      throw std::runtime_error(
          "Mission lighting cannot change pipeline topology");
    lighting_settings = next;
  }
  // Copies the resident target images into s.color/s.depth (s sized to the
  // target).
  void fetch_resident(Scene &s) {
    finish_world();
    settle();
    if (s.width != width || s.height != height)
      throw std::runtime_error(
          "Readback size differs from the resident target");
    check(f.vkResetCommandPool(device, commandPool, 0), "reset command pool");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(cmd, &bi), "begin readback");
    const VkDeviceSize pixels = VkDeviceSize(width) * height,
                       colorBytes = pixels * (rgba ? 4 : 2);
    ensure(readback,
           mw34::depth::offset(size_t(colorBytes)) +
               VkDeviceSize(rw) * rh * sizeof(float),
           VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (in_attachment_layout) {
      barrier(colorImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
      barrier(depthImage.image,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    copy_out_scaled(mw34::depth::offset(size_t(colorBytes)));
    VkMemoryBarrier hb{};
    hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0,
                           nullptr);
    check(f.vkEndCommandBuffer(cmd), "end readback");
    check(f.vkResetFences(device, 1, &fence), "reset fence");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    check(f.vkQueueSubmit(queue, 1, &si, fence), "submit readback");
    check(f.vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL),
          "wait readback");
    in_attachment_layout = false;
    ++readbacks;
    copy_back(s);
  }
  // The caller has waited for GPU writes and sized both output vectors. The
  // readback buffer packs guest color bytes first, then full-raster D32 depth.
  // Depth is sampled and quantized only at the guest boundary.
  // RGB565 quantization deliberately uses integer rounding; preserve this
  // arithmetic.
  void copy_back(Scene &s) {
    const VkDeviceSize pixels = VkDeviceSize(width) * height,
                       colorBytes = pixels * (rgba ? 4 : 2);
    const auto *rd = static_cast<const unsigned char *>(readback.mapped);
    if (rgba) {
      for (VkDeviceSize i = 0; i < pixels; ++i) {
        const std::uint32_t r = rd[4 * i], g = rd[4 * i + 1], b = rd[4 * i + 2];
        s.color[i] =
            std::uint16_t(((r * 31 + 127) / 255) << 11 |
                          ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
      }
    } else
      std::memcpy(s.color.data(), rd, colorBytes);
    mw34::depth::readback_scaled(s.depth.data(),
                                 rd + mw34::depth::offset(size_t(colorBytes)),
                                 width, height, rw, rh);
  }

private:
  // Enumeration may precede window creation. Advertise a limit every graphics
  // adapter supports, without creating a device before presentation can choose.
  static unsigned unbound_texture_dimension() {
    struct Probe {
      cc_mw3_re::platform::VulkanLoader loader;
      VkInstance instance{};
      PFN_vkDestroyInstance destroy{};
      ~Probe() {
        if (instance && destroy)
          destroy(instance, nullptr);
      }
    } probe;
    if (!probe.loader.load())
      throw std::runtime_error("Vulkan capability loader unavailable");
    const auto get = probe.loader.get_instance_proc_addr();
    if (!get)
      throw std::runtime_error("Vulkan capability entry point unavailable");
    const auto create = reinterpret_cast<PFN_vkCreateInstance>(
        get(nullptr, "vkCreateInstance"));
    if (!create)
      throw std::runtime_error(
          "Vulkan capability instance entry point unavailable");
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    check(create(&info, nullptr, &probe.instance),
          "create capability instance");
    probe.destroy = reinterpret_cast<PFN_vkDestroyInstance>(
        get(probe.instance, "vkDestroyInstance"));
    const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        get(probe.instance, "vkEnumeratePhysicalDevices"));
    const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        get(probe.instance, "vkGetPhysicalDeviceProperties"));
    const auto queues =
        reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            get(probe.instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    if (!probe.destroy || !enumerate || !properties || !queues)
      throw std::runtime_error("Vulkan capability functions unavailable");
    unsigned count{};
    check(enumerate(probe.instance, &count, nullptr),
          "enumerate capability adapters");
    std::vector<VkPhysicalDevice> adapters(count);
    check(enumerate(probe.instance, &count, adapters.data()),
          "read capability adapters");
    unsigned limit{};
    for (unsigned index = 0; index < count; ++index) {
      unsigned queue_count{};
      queues(adapters[index], &queue_count, nullptr);
      std::vector<VkQueueFamilyProperties> families(queue_count);
      queues(adapters[index], &queue_count, families.data());
      const bool graphics =
          std::any_of(families.begin(), families.end(), [](const auto &family) {
            return family.queueCount &&
                   (family.queueFlags & VK_QUEUE_GRAPHICS_BIT);
          });
      if (!graphics)
        continue;
      VkPhysicalDeviceProperties device_properties{};
      properties(adapters[index], &device_properties);
      const auto supported = device_properties.limits.maxImageDimension2D;
      limit = limit ? std::min(limit, supported) : supported;
    }
    if (!limit)
      throw std::runtime_error(
          "No graphics adapter supplies Vulkan texture limits");
    return limit;
  }
  const bool require_rgba8;
  const WindowSurface surface_source;
  const Quality quality_settings;
  const bool use_gpu_models;
  mw34::sun::Settings lighting_settings;
  cc_mw3_re::RendererBridgeConfig bridge_config{};
  mw34::effects::Settings effects_settings{};
  DxgiPresent dxgi;
  // Keep the entry-point macro lists together for loader auditing.
  // clang-format off
    struct Fns {
#define MW34_VK_FNS(X) X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceFeatures) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFormatProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkEnumerateDeviceExtensionProperties)
#if defined(_WIN32)
#define MW34_VK_WIN32_WSI_FNS(X) X(vkCreateWin32SurfaceKHR)
#else
#define MW34_VK_WIN32_WSI_FNS(X)
#endif
#define MW34_VK_WSI_FNS(X) MW34_VK_WIN32_WSI_FNS(X) X(vkDestroySurfaceKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR)
#define MW34_VK_SWAP_FNS(X) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCmdBlitImage) X(vkCmdClearColorImage)
#define MW34_VK_DEV_FNS(X) X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
    X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkBindImageMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCreateImage) X(vkDestroyImage) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkResetDescriptorPool) X(vkAllocateDescriptorSets) \
    X(vkUpdateDescriptorSets) X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkCreateComputePipelines) X(vkCmdDispatch) X(vkDestroyPipeline) X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) \
    X(vkCmdCopyBuffer) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) \
    X(vkCmdBindIndexBuffer) X(vkCmdDrawIndexed) X(vkCmdPushConstants) X(vkCmdDraw) X(vkCmdClearAttachments) X(vkCmdBindVertexBuffers) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCreateFence) \
    X(vkDestroyFence) X(vkResetFences) X(vkQueueSubmit) X(vkWaitForFences) \
    X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkGetQueryPoolResults) X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp)
#define MW34_VK_DECL(n) PFN_##n n{};
        MW34_VK_FNS(MW34_VK_DECL) MW34_VK_DEV_FNS(MW34_VK_DECL) MW34_VK_WSI_FNS(MW34_VK_DECL) MW34_VK_SWAP_FNS(MW34_VK_DECL)
    } f;
  // clang-format on
  // Handle records are explicitly destroyed by Renderer; they are not
  // independent RAII owners and must not be copied out and freed separately.
  // Staging buffers use coherent mapped memory, so fence completion needs no
  // invalidate call.
  struct Buffer {
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkDeviceSize size{};
    void *mapped{};
  };
#include "tiled_light_gpu.inc"
  struct Image {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
  };
  struct Tex {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    unsigned width{}, height{}, levels{1};
    std::uint64_t hash{};
    bool valid{};
  };
  cc_mw3_re::platform::VulkanLoader loader;
  VkInstance instance{};
  VkPhysicalDevice gpu{};
  VkPhysicalDeviceMemoryProperties memory{};
  VkDevice device{};
  VkQueue queue{};
  unsigned family{}, width{}, height{}, descriptorCapacity{};
  bool rgba{};
  Buffer upload, readback, vertexBuffer, normalBuffer, texUpload, sunBuffer,
      effectBuffer;
  VkDeviceSize sunStride{};
  VkDescriptorSetLayout sunSetLayout{}, normalSetLayout{};
  VkShaderModule sunWorldVert{}, sunModelVert{}, sunTlVert{}, sunFrag{},
      nativeGeometryVert{};
  // Optional sun shadow map. Casters are GPU replacement models only.
  struct ShadowCaster {
    VkBuffer buffer{};
    unsigned count{};
    std::array<float, 12> rows{};
  };
  struct ShadowFrame {
    bool active{}, reused{};
    mw34::sun::ShadowView view;
    std::vector<ShadowCaster> casters;
  };
  mw34::sun::ShadowView lastShadowView;
  unsigned scenesSinceShadow{~0u};
  VkImage shadowImage{};
  VkDeviceMemory shadowMemory{};
  VkImageView shadowView{};
  VkSampler shadowSampler{};
  VkRenderPass shadowPass{};
  VkFramebuffer shadowFramebuffer{};
  VkPipelineLayout shadowLayout{};
  VkPipeline shadowPipeline{};
  VkShaderModule shadowVert{};
  void init_shadow() {
    const auto &sun = lighting_settings;
    const unsigned size = sun.shadows ? sun.shadow_size : 1;
    VkFormat format = VK_FORMAT_D32_SFLOAT;
    VkFormatProperties fp{};
    f.vkGetPhysicalDeviceFormatProperties(gpu, format, &fp);
    const VkFormatFeatureFlags need =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((fp.optimalTilingFeatures & need) != need) {
      format = VK_FORMAT_D16_UNORM;
      f.vkGetPhysicalDeviceFormatProperties(gpu, format, &fp);
      if ((fp.optimalTilingFeatures & need) != need)
        throw std::runtime_error(
            "GPU cannot render and sample a sun shadow depth map");
    }
    const bool linear =
        (fp.optimalTilingFeatures &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    VkPhysicalDeviceProperties p{};
    f.vkGetPhysicalDeviceProperties(gpu, &p);
    if (size > p.limits.maxImageDimension2D ||
        size > p.limits.maxFramebufferWidth ||
        size > p.limits.maxFramebufferHeight)
      throw std::runtime_error("Sun shadow map size exceeds the GPU limit");
    image(shadowImage, shadowMemory, shadowView, size, size, format,
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_IMAGE_ASPECT_DEPTH_BIT);
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    check(f.vkCreateSampler(device, &si, nullptr, &shadowSampler),
          "sun shadow sampler");
    if (!sun.shadows)
      return; // 1x1 placeholder keeps the sun descriptor set valid.
    VkAttachmentDescription at{};
    at.format = format;
    at.samples = VK_SAMPLE_COUNT_1_BIT;
    at.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    at.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference dr{0,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.pDepthStencilAttachment = &dr;
    // Previous scene's receiver reads -> depth writes -> this scene's receiver
    // reads.
    std::array<VkSubpassDependency, 2> dep{};
    dep[0] = {VK_SUBPASS_EXTERNAL,
              0,
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              0,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              0};
    dep[1] = {0,
              VK_SUBPASS_EXTERNAL,
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_ACCESS_SHADER_READ_BIT,
              0};
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &at;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sp;
    rpi.dependencyCount = 2;
    rpi.pDependencies = dep.data();
    check(f.vkCreateRenderPass(device, &rpi, nullptr, &shadowPass),
          "sun shadow render pass");
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = shadowPass;
    fi.attachmentCount = 1;
    fi.pAttachments = &shadowView;
    fi.width = fi.height = size;
    fi.layers = 1;
    check(f.vkCreateFramebuffer(device, &fi, nullptr, &shadowFramebuffer),
          "sun shadow framebuffer");
    VkPushConstantRange pr{VK_SHADER_STAGE_VERTEX_BIT, 0, 48};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pr;
    check(f.vkCreatePipelineLayout(device, &pli, nullptr, &shadowLayout),
          "sun shadow pipeline layout");
    VkPipelineShaderStageCreateInfo st{};
    st.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st.stage = VK_SHADER_STAGE_VERTEX_BIT;
    st.module = shadowVert;
    st.pName = "main";
    VkVertexInputBindingDescription vb{0, sizeof(ModelVertex),
                                       VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &va;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp{0, 0, float(size), float(size), 0, 1};
    VkRect2D sc{{0, 0}, {size, size}};
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports = &vp;
    vps.scissorCount = 1;
    vps.pScissors = &sc;
    // Two-sided: replacement meshes are not guaranteed closed. Slope bias
    // plus the receiver's normal offset/bias avoid self-shadow acne.
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasSlopeFactor = 1.5f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendStateCreateInfo bs{};
    bs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkGraphicsPipelineCreateInfo gi{};
    gi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gi.stageCount = 1;
    gi.pStages = &st;
    gi.pVertexInputState = &vi;
    gi.pInputAssemblyState = &ia;
    gi.pViewportState = &vps;
    gi.pRasterizationState = &rs;
    gi.pMultisampleState = &ms;
    gi.pDepthStencilState = &ds;
    gi.pColorBlendState = &bs;
    gi.layout = shadowLayout;
    gi.renderPass = shadowPass;
    check(f.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gi, nullptr,
                                      &shadowPipeline),
          "sun shadow pipeline");
    std::fprintf(stderr,
                 "Sun shadows: %ux%u %s map, range +-%.1f, receiver bias %.3f, "
                 "darkness %.2f, %s PCF; GPU replacement models cast\n",
                 size, size, format == VK_FORMAT_D32_SFLOAT ? "D32" : "D16",
                 double(sun.shadow_range), double(sun.shadow_bias),
                 double(sun.shadow_darkness), linear ? "bilinear" : "point");
  }
  // Centre on the active world camera (from its visible TL receivers),
  // pushed half a range ahead horizontally, then gather each visible GPU
  // replacement draw once with its sun-clip transform.
  ShadowFrame prepare_shadow(const Scene &s, const mw34::sun::Settings &sun) {
    ShadowFrame frame;
    if (!sun.shadows || s.hud)
      return frame;
    // MW3 may draw terrain and mechs in separate BeginScene/EndScene
    // batches. A batch without casters (or without a camera reference)
    // samples the latest map with that map's exact sun transform.
    if (scenesSinceShadow != ~0u)
      ++scenesSinceShadow;
    auto reuse = [&] {
      if (scenesSinceShadow <= 8) {
        frame.active = frame.reused = true;
        frame.view = lastShadowView;
        ++sun_shadow_reused;
      }
      return frame;
    };
    const mw34::sun::Tl *camera = nullptr;
    mw34::sun::Tl model_cam{};
    for (const auto &d : s.draws) {
      if (d.sun_tl && sun_lit(d)) {
        camera = d.sun_tl.get();
        break;
      }
      if (!camera && d.model && sun_lit(d)) {
        model_cam.camera_world = d.model->camera;
        model_cam.projection = d.model->projection;
        camera = &model_cam;
      }
    }
    if (!camera)
      return reuse();
    const auto &m = camera->camera_world;
    std::array<double, 3> centre{m[3], m[7], m[11]};
    const double fx = m[2], fz = m[10], fl = std::sqrt(fx * fx + fz * fz);
    if (fl > 1e-6) {
      centre[0] += fx / fl * sun.shadow_range * .5;
      centre[2] += fz / fl * sun.shadow_range * .5;
    }
    frame.view = mw34::sun::shadow_view(centre, sun);
    std::set<std::pair<VkBuffer, std::array<float, 12>>> seen;
    const double margin = 1 + 64.0 / sun.shadow_range;
    unsigned ahead = 0, outside = 0;
    for (const auto &d : s.draws) {
      if (!d.model || d.model->world || !d.count || !sun_lit(d))
        continue;
      const auto &i = *d.model;
      const auto &b = model_buffers.at(i.geometry->key);
      const auto rows = mw34::sun::shadow_caster(frame.view, i.sun_world);
      if (std::abs(rows[3]) > margin || std::abs(rows[7]) > margin) {
        ++outside;
        continue;
      }
      if (!seen.insert({b.local.buffer, rows}).second)
        continue;
      frame.casters.push_back({b.local.buffer, b.count, rows});
      if ((i.sun_world[3] - m[3]) * fx + (i.sun_world[11] - m[11]) * fz > 0)
        ++ahead;
    }
    frame.active = !frame.casters.empty();
    if (!frame.active)
      return reuse();
    if (const char *audit = bridge_config.sun_shadow_audit.empty()
                                ? nullptr
                                : bridge_config.sun_shadow_audit.c_str();
        audit && std::strtoull(audit, nullptr, 10) == sun_shadow_scenes) {
      std::fprintf(stderr,
                   "Sun shadow audit scene %llu camera %.2f %.2f %.2f\n",
                   (unsigned long long)sun_shadow_scenes, m[3], m[7], m[11]);
      for (const auto &d : s.draws) {
        if (!d.model || d.model->world || !d.count || !sun_lit(d))
          continue;
        const auto &w = d.model->sun_world;
        std::array<double, 3> lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
        for (const auto &v : d.model->geometry->vertices)
          for (unsigned r = 0; r < 3; ++r) {
            const double x = w[r * 4] * v.x + w[r * 4 + 1] * v.y +
                             w[r * 4 + 2] * v.z + w[r * 4 + 3];
            lo[r] = std::min(lo[r], x);
            hi[r] = std::max(hi[r], x);
          }
        std::fprintf(stderr,
                     "  caster key %016llx verts %zu world x %.2f..%.2f y "
                     "%.2f..%.2f z %.2f..%.2f\n",
                     (unsigned long long)d.model->geometry->key,
                     d.model->geometry->vertices.size(), lo[0], hi[0], lo[1],
                     hi[1], lo[2], hi[2]);
      }
    }
    if (frame.active) {
      lastShadowView = frame.view;
      scenesSinceShadow = 0;
    }
    if (frame.active &&
        (sun_shadow_scenes < 3 || sun_shadow_scenes % 600 == 0)) {
      std::fprintf(
          stderr,
          "Sun shadow scene %llu: camera %.2f %.2f %.2f forward %.3f %.3f, "
          "casters %zu (%u ahead, %u outside region), texel %.4f\n",
          (unsigned long long)sun_shadow_scenes, m[3], m[7], m[11], fx, fz,
          frame.casters.size(), ahead, outside, frame.view.texel);
      // Projection contract: screen = centre + scale * camera.xy / camera.z for
      // both receiver kinds.
      const auto &tp = camera->projection;
      std::fprintf(stderr, "  TL receivers: scale %.3f %.3f centre %.2f %.2f\n",
                   double(tp[1]) * tp[0], double(tp[2]) * tp[0], tp[3], tp[4]);
      for (const auto &d : s.draws)
        if (d.model && !d.model->world && sun_lit(d)) {
          const auto &p = d.model->projection;
          const auto &w = d.model->sun_world;
          std::fprintf(stderr,
                       "  GPU model: scale %.3f %.3f centre %.2f %.2f, camera "
                       "origin %.2f %.2f %.2f, world origin %.2f %.2f %.2f\n",
                       double(p[1]) * p[0], double(p[2]) * p[0], p[3], p[4],
                       d.model->camera[3], d.model->camera[7],
                       d.model->camera[11], w[3], w[7], w[11]);
          break;
        }
    }
    return frame;
  }
  std::array<float, 12> skyEye{};
  bool skyEyeValid{};
  void apply_atmosphere(Scene &s, const mw34::sun::Settings &sun) {
    skyEyeValid = false;
    if (!sun.enabled || s.hud ||
        (!sun.sky_active && !sun.haze_active && !sun.sky_shading &&
         !sun.suppress_sky))
      return;
    // Sky shading: each far draw takes the camera of the nearest TL/model
    // receiver in submission order (the same view), else the next one after it.
    std::vector<std::shared_ptr<const mw34::sun::Tl>> view(s.draws.size());
    if (sun.sky_shading) {
      std::shared_ptr<const mw34::sun::Tl> current;
      for (const auto &o : s.order)
        if (!o.clear) {
          const auto &d = s.draws[o.index];
          if (d.sun_tl)
            current = d.sun_tl;
          else if (d.model && (d.model->world || d.model->sun_eligible)) {
            if (!current || std::memcmp(current->camera_world.data(),
                                        d.model->camera.data(),
                                        sizeof(d.model->camera)) != 0) {
              auto tl = std::make_shared<mw34::sun::Tl>();
              tl->camera_world = d.model->camera;
              tl->projection = d.model->projection;
              current = std::move(tl);
            }
          }
          view[o.index] = current;
        }
      current.reset();
      for (auto o = s.order.rbegin(); o != s.order.rend(); ++o)
        if (!o->clear) {
          const auto &d = s.draws[o->index];
          if (d.sun_tl)
            current = d.sun_tl;
          else if (d.model && (d.model->world || d.model->sun_eligible)) {
            if (!current || std::memcmp(current->camera_world.data(),
                                        d.model->camera.data(),
                                        sizeof(d.model->camera)) != 0) {
              auto tl = std::make_shared<mw34::sun::Tl>();
              tl->camera_world = d.model->camera;
              tl->projection = d.model->projection;
              current = std::move(tl);
            }
          }
          if (!view[o->index])
            view[o->index] = current;
        }
      for (const auto &d : s.draws) {
        if (d.sun_tl) {
          skyEye = d.sun_tl->camera_world;
          skyEyeValid = true;
          break;
        }
        if (d.model && (d.model->world || d.model->sun_eligible)) {
          skyEye = d.model->camera;
          skyEyeValid = true;
          break;
        }
      }
    }
    for (size_t at = 0; at < s.draws.size(); ++at) {
      auto &d = s.draws[at];
      // Status previews use original fog colour/factors for per-part damage
      // tint.
      if (d.condition_schematic)
        continue;
      // Unlit fogged world draws (not sky) keep their colours but take the
      // directional haze.
      if (d.sky_view)
        view[at] = d.sky_view;
      const bool routable = (sun.sky_shading || effects_settings.enabled) &&
                            view[at] && !d.model && !d.sun_tl && d.count &&
                            (d.push.mode[0] & 16);
      if (sun.haze_active && (d.native_sky || (d.push.mode[0] & 16))) {
        for (unsigned i = 0; i < 3; ++i)
          d.push.fog[i] =
              sun.haze_manual
                  ? sun.haze_color[i]
                  : std::fmin(1.f, d.push.fog[i] * sun.haze_scale[i]);
        ++haze_draws;
      }
      if (routable && d.key.blend && !d.native_sky) {
        d.sky = 3;
        d.sun_tl = view[at];
        ++haze_routed;
        continue;
      }
      if (d.model || (!d.native_sky && (d.sun_tl || d.key.blend)) || !d.count ||
          (d.texture >= 0 && !sun.sky_active && !sun.sky_shading &&
           !sun.suppress_sky) ||
          (d.texture < 0 && !sun.haze_active && !sun.sky_shading &&
           !sun.suppress_sky))
        continue;
      const bool distant = d.native_sky || mw34::sky::backdrop(
          s.vertices.data() + size_t(d.first) * 32, size_t(d.count) * 32,
          d.detail_role, d.texture >= 0, d.key.zwrite != 0);
      if (!distant) {
        if (routable) {
          d.sky = 3;
          d.sun_tl = view[at];
          ++haze_routed;
        }
        continue;
      }
      if (sun.suppress_sky) {
        d.count = 0;
        continue;
      }
      if (sun.sky_shading && view[at]) {
        d.sky = d.texture < 0 ? 2 : 1;
        d.sun_tl = view[at];
        if (d.sky_view)
          ++sky_own_view;
        if (d.texture < 0)
          ++horizon_draws;
        else {
          ++sky_draws;
          ++sky_tinted;
        }
        continue;
      }
      if (d.texture < 0) {
        // Untextured far backdrop below the dome: the horizon band takes the
        // haze colour.
        for (unsigned v = d.first; v < d.first + d.count; ++v) {
          std::uint32_t c;
          unsigned char *at = s.vertices.data() + size_t(v) * 32 + 16;
          std::memcpy(&c, at, 4);
          std::uint32_t out = c & 0xff000000u;
          for (unsigned i = 0; i < 3; ++i) {
            const unsigned shift = 16 - 8 * i;
            const float x = sun.haze_manual
                                ? sun.haze_color[i] * 255.f
                                : float((c >> shift) & 255) * sun.haze_scale[i];
            out |=
                std::uint32_t(std::lround(std::fmin(255.f, std::fmax(0.f, x))))
                << shift;
          }
          std::memcpy(at, &out, 4);
        }
        ++horizon_draws;
        continue;
      }
      ++sky_draws;
      const std::uint32_t w = d.push.mode[1], op = w & 255, a1 = (w >> 8) & 255,
                          a2 = (w >> 16) & 255;
      auto diffuse = [](std::uint32_t a) { return (a & 15) <= 1 && !(a & 48); };
      if (op == 1 || (op == 2 && diffuse(a1)) || (op == 3 && diffuse(a2)) ||
          ((op >= 4 && op <= 6) && (diffuse(a1) || diffuse(a2)))) {
        for (unsigned v = d.first; v < d.first + d.count; ++v) {
          std::uint32_t c;
          unsigned char *at = s.vertices.data() + size_t(v) * 32 + 16;
          std::memcpy(&c, at, 4);
          std::uint32_t out = c & 0xff000000u;
          for (unsigned i = 0; i < 3; ++i) {
            const unsigned shift = 16 - 8 * i;
            const float x = float((c >> shift) & 255) * sun.sky_tint[i];
            out |= std::uint32_t(std::lround(std::fmin(255.f, x))) << shift;
          }
          std::memcpy(at, &out, 4);
        }
        ++sky_tinted;
      } else if (op == 2 && (a1 & 63) == 2) {
        d.push.mode[1] =
            4u | (2u << 8) | (3u << 16); // MODULATE(TEXTURE, TFACTOR)
        for (unsigned i = 0; i < 3; ++i)
          d.push.factor[i] = sun.sky_tint[i];
        ++sky_tinted;
      } else
        ++sky_untinted;
    }
  }
  void record_shadow(const ShadowFrame &frame) {
    const unsigned size = lighting_settings.shadow_size;
    VkClearValue clear{};
    clear.depthStencil.depth = 1.f;
    VkRenderPassBeginInfo rb{};
    rb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rb.renderPass = shadowPass;
    rb.framebuffer = shadowFramebuffer;
    rb.renderArea = {{0, 0}, {size, size}};
    rb.clearValueCount = 1;
    rb.pClearValues = &clear;
    f.vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    VkBuffer bound{};
    const VkDeviceSize zero = 0;
    for (const auto &c : frame.casters) {
      if (c.buffer != bound) {
        f.vkCmdBindVertexBuffers(cmd, 0, 1, &c.buffer, &zero);
        bound = c.buffer;
      }
      f.vkCmdPushConstants(cmd, shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 48,
                           c.rows.data());
      f.vkCmdDraw(cmd, c.count, 1, 0, 0);
    }
    f.vkCmdEndRenderPass(cmd);
    ++sun_shadow_scenes;
    sun_shadow_casters += frame.casters.size();
  }
  std::map<std::uint64_t, std::uint64_t> sunChecked;
  bool sun_lit(const Draw &d) const {
    return lighting_settings.enabled &&
           (d.sky || d.sun_tl || (d.model && d.model->sun_eligible));
  }

public:
  std::uint64_t sun_world_draws{}, sun_model_draws{}, sun_tl_draws{},
      sun_shadow_scenes{}, sun_shadow_casters{}, sun_shadow_reused{},
      sky_draws{}, sky_tinted{}, sky_untinted{}, haze_draws{}, horizon_draws{},
      haze_routed{}, sky_own_view{};

private:
  struct ModelBuffer {
    Buffer local, staging, indices, index_staging;
    std::weak_ptr<const cc::geometry::Resource> source;
    unsigned index_count{};
    std::uint64_t hash{};
    unsigned count{};
    bool uploaded{};
  };
  std::map<std::uint64_t, ModelBuffer> model_buffers;
#include "fixture_shadow_stage.inc"
  Image colorImage, depthImage;
  Image qualitySource;
  VkDescriptorPool qualityPool{};
  VkDescriptorSet qualitySet{};
  VkPipelineLayout qualityLayout{};
  VkPipeline qualityPipeline{};
  VkShaderModule qualityVert{}, qualityFrag{};
  bool quality_world_dirty{};
  // Render scale (lazy residency on the presenter device): the target images
  // are rw x rh = round(width/height * scale); guest-resolution staging images
  // (colorLo/depthLo) carry uploads and copy-backs through blits. scale_req is
  // set by the provider before a scene; it applies when the target is
  // (re)created.
  Image colorLo, depthLo;
  float scale = 1.f, scale_req = 1.f, scale_x = 1.f;
  bool target_frontend{};
  unsigned rw{}, rh{};
  Tex white, flatNormal;
  std::map<std::uint64_t, Tex> textures;
  std::map<std::uint64_t, VkPipeline> pipelines;
  std::map<unsigned, VkSampler> samplers;
  VkRenderPass renderPass{};
  VkFramebuffer framebuffer{};
  VkDescriptorSetLayout setLayout{};
  VkDescriptorPool descriptorPool{};
  VkPipelineLayout pipelineLayout{}, modelLayout{};
  VkShaderModule vert{}, frag{}, modelVert{}, worldVert{};
  VkCommandPool commandPool{};
  VkCommandBuffer cmd{};
  VkFence fence{};
  // presentation
  bool presentable{};
  cc_mw3_re::platform::NativeWindowHandle presentWindow{}, presentParent{},
      child{};
  VkSurfaceKHR windowSurface{};
  VkSwapchainKHR swapchain{};
  std::vector<VkImage> swapImages;
  // A render-finished semaphore is reusable only after its own image has been
  // acquired again; a render fence alone does not complete presentation waits.
  std::vector<VkSemaphore> swapRendered;
  VkExtent2D swapExtent{};
  VkSemaphore acquired{};
  VkCommandPool presentPool{};
  VkCommandBuffer presentCmd{};
  VkFence presentFence{};
  Image srcImage;
  unsigned srcWidth{}, srcHeight{};
  Buffer presentUpload;

  static void check(VkResult r, const char *what) {
    if (r != VK_SUCCESS)
      throw std::runtime_error(std::string("Vulkan scene renderer: ") + what +
                               " failed (" + std::to_string(int(r)) + ")");
  }
  // Load required entry points explicitly. A presentation request probes the
  // actual window surface before selecting a graphics queue; a headless render
  // does not. Requested quality features fail visibly if the device lacks their
  // contracts.
  void init(cc_mw3_re::platform::NativeWindowHandle forPresent = nullptr) {
    const bool bridge_requested = bridge_config.dxgi_present;
    if (surface_source.create && bridge_requested)
      throw std::runtime_error(
          "Native Vulkan WSI cannot use a DXGI presentation device");
    if (bridge_requested && exclusiveRequested())
      throw std::runtime_error(
          "DXGI presentation bridge requires windowed or borderless mode");
    if (!loader.load())
      throw std::runtime_error(
          "Vulkan scene renderer: system Vulkan loader unavailable");
    auto get = loader.get_instance_proc_addr();
    if (!get)
      throw std::runtime_error(
          "Vulkan scene renderer: vkGetInstanceProcAddr unavailable");
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
        get(nullptr, "vkCreateInstance"));
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "MW34 Direct3D scene renderer";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    const char *layer = "VK_LAYER_KHRONOS_validation";
    if (bridge_config.vk_validation) {
      ici.enabledLayerCount = 1;
      ici.ppEnabledLayerNames = &layer;
    }
    std::vector<const char *> instanceExtensions;
    if (surface_source.create) {
      instanceExtensions = surface_source.extensions;
    } else {
      auto enumerate =
          reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
              get(nullptr, "vkEnumerateInstanceExtensionProperties"));
      unsigned n = 0;
      std::vector<VkExtensionProperties> props;
      if (enumerate && enumerate(nullptr, &n, nullptr) == VK_SUCCESS) {
        props.resize(n);
        enumerate(nullptr, &n, props.data());
      }
      auto has = [&](const char *e) {
        for (const auto &p : props)
          if (!std::strcmp(p.extensionName, e))
            return true;
        return false;
      };
      const char *plat_surface_ext =
          cc_mw3_re::platform::get_platform_surface_extension_name();
      if (plat_surface_ext && has(VK_KHR_SURFACE_EXTENSION_NAME) &&
          has(plat_surface_ext))
        instanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, plat_surface_ext};
      if (exclusiveRequested()) {
        if (!has(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) ||
            !has(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
          throw std::runtime_error("Vulkan exclusive fullscreen instance "
                                   "extensions are unavailable");
        instanceExtensions.push_back(
            VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        instanceExtensions.push_back(
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
      }
    }
    ici.enabledExtensionCount = unsigned(instanceExtensions.size());
    ici.ppEnabledExtensionNames = instanceExtensions.data();
    check(createInstance(&ici, nullptr, &instance), "create instance");
#define MW34_VK_LOAD(n)                                                        \
  f.n = reinterpret_cast<PFN_##n>(get(instance, #n));                          \
  if (!f.n)                                                                    \
    throw std::runtime_error("Vulkan scene renderer: " #n " missing");
    MW34_VK_FNS(MW34_VK_LOAD)
#define MW34_VK_OPT(n) f.n = reinterpret_cast<PFN_##n>(get(instance, #n));
    if (!instanceExtensions.empty()) {
      MW34_VK_WSI_FNS(MW34_VK_OPT)
    }
    VkSurfaceKHR probe{};
    if (surface_source.create && forPresent) {
      windowSurface = surface_source.create(instance);
      if (!windowSurface)
        throw std::runtime_error("SDL did not create a Vulkan window surface");
      probe = windowSurface;
      presentWindow = forPresent;
    } else if (!bridge_requested && forPresent && !instanceExtensions.empty()) {
#if defined(_WIN32)
      if (f.vkCreateWin32SurfaceKHR) {
        probe = cc_mw3_re::platform::create_win32_surface(
            instance, forPresent, f.vkCreateWin32SurfaceKHR);
      }
#endif
    }
    unsigned count = 0;
    check(f.vkEnumeratePhysicalDevices(instance, &count, nullptr),
          "enumerate devices");
    std::vector<VkPhysicalDevice> gpus(count);
    check(f.vkEnumeratePhysicalDevices(instance, &count, gpus.data()),
          "enumerate devices");
    // Prefer a discrete GPU with a graphics queue, then any device with one.
    int best = -1;
    std::string surface_failure;
    for (auto g : gpus) {
      unsigned n = 0;
      f.vkGetPhysicalDeviceQueueFamilyProperties(g, &n, nullptr);
      std::vector<VkQueueFamilyProperties> q(n);
      f.vkGetPhysicalDeviceQueueFamilyProperties(g, &n, q.data());
      VkPhysicalDeviceProperties p{};
      f.vkGetPhysicalDeviceProperties(g, &p);
      for (unsigned i = 0; i < n; ++i)
        if (q[i].queueCount && (q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (!effects_settings.enabled || (q[i].queueFlags & VK_QUEUE_COMPUTE_BIT))) {
          if (forPresent && !bridge_requested) {
            VkBool32 ok = VK_FALSE;
            VkSurfaceCapabilitiesKHR c{};
            if (!probe)
              continue;
            const auto support =
                f.vkGetPhysicalDeviceSurfaceSupportKHR(g, i, probe, &ok);
            if (support != VK_SUCCESS || !ok) {
              surface_failure = std::string(p.deviceName) +
                                " surface support " +
                                std::to_string(int(support)) +
                                " (supported=" + std::to_string(ok) + ")";
              continue;
            }
            const auto capabilities =
                f.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g, probe, &c);
            if (capabilities != VK_SUCCESS) {
              surface_failure = std::string(p.deviceName) +
                                " surface capabilities " +
                                std::to_string(int(capabilities));
              continue;
            }
          }
          const int score =
              p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
          if (score > best) {
            best = score;
            gpu = g;
            family = i;
            device_name = p.deviceName;
          }
          break;
        }
    }
    if (!gpu && forPresent && !bridge_requested) {
      if (surface_source.create)
        throw std::runtime_error(
            "No Vulkan adapter can present to the SDL surface: " +
            surface_failure);
      if (probe)
        f.vkDestroySurfaceKHR(instance, probe, nullptr);
      presentable = false;
      return;
    }
    if (!gpu)
      throw std::runtime_error("Vulkan scene renderer: no graphics device");
    f.vkGetPhysicalDeviceMemoryProperties(gpu, &memory);
    VkFormatProperties depthProperties{};
    f.vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_D32_SFLOAT,
                                          &depthProperties);
    const VkFormatFeatureFlags depthNeed =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    if ((depthProperties.optimalTilingFeatures & depthNeed) != depthNeed)
      throw std::runtime_error("GPU does not support required 32-bit "
                               "floating-point depth attachments and blits");
    VkFormatProperties cp{};
    f.vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_R5G6B5_UNORM_PACK16,
                                          &cp);
    const VkFormatFeatureFlags need =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    rgba = require_rgba8 || (cp.optimalTilingFeatures & need) != need;
    float priority = 1;
    VkDeviceQueueCreateInfo q{};
    q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q.queueFamilyIndex = family;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &q;
    VkPhysicalDeviceFeatures enabled{};
    if (use_native_geometry) {
      VkPhysicalDeviceFeatures supported{};
      f.vkGetPhysicalDeviceFeatures(gpu, &supported);
      if (!supported.shaderClipDistance)
        throw std::runtime_error("GPU geometry requires shaderClipDistance");
      enabled.shaderClipDistance = VK_TRUE;
    }
    dci.pEnabledFeatures = &enabled;
    if (anisotropy() > 1) {
      VkPhysicalDeviceFeatures supported{};
      f.vkGetPhysicalDeviceFeatures(gpu, &supported);
      VkPhysicalDeviceProperties props{};
      f.vkGetPhysicalDeviceProperties(gpu, &props);
      if (!supported.samplerAnisotropy ||
          props.limits.maxSamplerAnisotropy < float(anisotropy()))
        throw std::runtime_error(
            "GPU does not support the requested anisotropic filtering level");
      VkFormatProperties tex{};
      f.vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_R8G8B8A8_UNORM,
                                            &tex);
      const auto bits = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                        VK_FORMAT_FEATURE_BLIT_DST_BIT |
                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      if ((tex.optimalTilingFeatures & bits) != bits)
        throw std::runtime_error(
            "GPU cannot generate the RGBA8 mip chain required for AF");
      enabled.samplerAnisotropy = VK_TRUE;
      dci.pEnabledFeatures = &enabled;
    }
    if (fxaa_enabled()) {
      VkFormatProperties src{}, dst{};
      f.vkGetPhysicalDeviceFormatProperties(
          gpu, rgba ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16,
          &src);
      f.vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_R8G8B8A8_UNORM,
                                            &dst);
      if (!(src.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
          (dst.optimalTilingFeatures &
           (VK_FORMAT_FEATURE_BLIT_DST_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) !=
              (VK_FORMAT_FEATURE_BLIT_DST_BIT |
               VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        throw std::runtime_error(
            "GPU cannot provide the FXAA color-copy/sampling contract");
    }
    const char *swapExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    std::vector<const char *> deviceExtensions;
    {
      unsigned n = 0;
      std::vector<VkExtensionProperties> props;
      if (f.vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, nullptr) ==
          VK_SUCCESS) {
        props.resize(n);
        f.vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, props.data());
      }
      for (const auto &p : props) {
        if (!std::strcmp(p.extensionName, swapExt)) {
          bool native_surface_supported = false;
#if defined(_WIN32)
          native_surface_supported = (f.vkCreateWin32SurfaceKHR != nullptr);
#endif
          presentable = !instanceExtensions.empty() &&
                        (surface_source.create || native_surface_supported);
        }
        if (exclusiveRequested() &&
            exclusive_fullscreen_.is_extension(p.extensionName))
          exclusive_fullscreen_.set_device_exclusive(true);
      }
      if (bridge_requested) {
        for (const auto *required :
             {"VK_KHR_external_memory_win32", "VK_KHR_win32_keyed_mutex"}) {
          const bool supported =
              std::any_of(props.begin(), props.end(), [&](const auto &entry) {
                return !std::strcmp(entry.extensionName, required);
              });
          if (!supported)
            throw std::runtime_error(std::string("DXGI sharing requires ") +
                                     required);
          deviceExtensions.push_back(required);
        }
      }
      if (exclusiveRequested() && !exclusive_fullscreen_.is_device_exclusive())
        throw std::runtime_error(
            "GPU does not support Vulkan exclusive fullscreen");
    }
    if (presentable) {
      deviceExtensions.push_back(swapExt);
      if (exclusive_fullscreen_.is_device_exclusive())
        deviceExtensions.push_back(exclusive_fullscreen_.extension_name());
    }
    dci.enabledExtensionCount = unsigned(deviceExtensions.size());
    dci.ppEnabledExtensionNames = deviceExtensions.data();
    check(f.vkCreateDevice(gpu, &dci, nullptr, &device), "create device");
#define MW34_VK_DLOAD(n)                                                       \
  f.n = reinterpret_cast<PFN_##n>(f.vkGetDeviceProcAddr(device, #n));          \
  if (!f.n)                                                                    \
    throw std::runtime_error("Vulkan scene renderer: " #n " missing");
    MW34_VK_DEV_FNS(MW34_VK_DLOAD)
    if (exclusive_fullscreen_.is_device_exclusive()) {
      exclusive_fullscreen_.init_device(instance, device, get,
                                        f.vkGetDeviceProcAddr);
    }
    // Blits are core rendering commands too (mips, FXAA and scaled targets),
    // even when this device does not have a window surface.
    f.vkCmdBlitImage = reinterpret_cast<PFN_vkCmdBlitImage>(
        f.vkGetDeviceProcAddr(device, "vkCmdBlitImage"));
    if (!f.vkCmdBlitImage)
      throw std::runtime_error("Vulkan image blits unavailable");
    if (presentable || bridge_requested) {
      if (!bridge_requested) {
        MW34_VK_SWAP_FNS(MW34_VK_DLOAD)
      } else {
        f.vkCmdClearColorImage = reinterpret_cast<PFN_vkCmdClearColorImage>(
            f.vkGetDeviceProcAddr(device, "vkCmdClearColorImage"));
        if (!f.vkCmdClearColorImage)
          throw std::runtime_error("DXGI color clear unavailable");
      }
      VkSemaphoreCreateInfo sci{};
      sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      if (!bridge_requested)
        check(f.vkCreateSemaphore(device, &sci, nullptr, &acquired),
              "semaphore");
      VkCommandPoolCreateInfo ppi{};
      ppi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
      ppi.queueFamilyIndex = family;
      check(f.vkCreateCommandPool(device, &ppi, nullptr, &presentPool),
            "present command pool");
      VkCommandBufferAllocateInfo pai{};
      pai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      pai.commandPool = presentPool;
      pai.commandBufferCount = 1;
      check(f.vkAllocateCommandBuffers(device, &pai, &presentCmd),
            "present command buffer");
      VkFenceCreateInfo pfi{};
      pfi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      check(f.vkCreateFence(device, &pfi, nullptr, &presentFence),
            "present fence");
    }
    f.vkGetDeviceQueue(device, family, 0, &queue);
    if (bridge_requested) {
      const auto properties2 =
          reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
              get(instance, "vkGetPhysicalDeviceProperties2"));
      if (!properties2)
        throw std::runtime_error("DXGI sharing requires Vulkan 1.1 properties");
      VkPhysicalDeviceIDProperties identity{};
      identity.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
      VkPhysicalDeviceProperties2 properties{};
      properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      properties.pNext = &identity;
      properties2(gpu, &properties);
      if (properties.properties.apiVersion < VK_API_VERSION_1_1)
        throw std::runtime_error("DXGI sharing requires a Vulkan 1.1 device");
      const auto image_properties =
          reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
              get(instance, "vkGetPhysicalDeviceImageFormatProperties2"));
      if (!image_properties)
        throw std::runtime_error(
            "DXGI shared-image capability query unavailable");
      VkPhysicalDeviceExternalImageFormatInfo external{};
      external.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
      external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
      VkPhysicalDeviceImageFormatInfo2 query{};
      query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
      query.pNext = &external;
      query.format = VK_FORMAT_B8G8R8A8_UNORM;
      query.type = VK_IMAGE_TYPE_2D;
      query.tiling = VK_IMAGE_TILING_OPTIMAL;
      query.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      VkExternalImageFormatProperties shared_properties{};
      shared_properties.sType =
          VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
      VkImageFormatProperties2 limits{};
      limits.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
      limits.pNext = &shared_properties;
      check(image_properties(gpu, &query, &limits),
            "query DXGI shared-image support");
      if (!(shared_properties.externalMemoryProperties.externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
        throw std::runtime_error("GPU cannot import a DXGI shared texture");
      VkFormatProperties format_properties{};
      f.vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_B8G8R8A8_UNORM,
                                            &format_properties);
      if (!(format_properties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_BLIT_DST_BIT))
        throw std::runtime_error("GPU cannot blit into a DXGI shared texture");
      dxgi.attach(device, identity, family, f.vkGetDeviceProcAddr,
                  limits.imageFormatProperties.maxExtent);
      presentable = true;
      std::fprintf(
          stderr,
          "Presentation bridge: Vulkan/DXGI on %s; adapter LUID matched\n",
          device_name.c_str());
    }
    if (probe) {
      if (presentable) {
        windowSurface = probe;
        presentWindow = forPresent;
      } else {
        if (surface_source.create) {
          dropSurface();
          throw std::runtime_error(
              "The Vulkan device cannot create an SDL swapchain");
        }
        f.vkDestroySurfaceKHR(instance, probe, nullptr);
      }
    }
    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.queueFamilyIndex = family;
    check(f.vkCreateCommandPool(device, &cpi, nullptr, &commandPool),
          "command pool");
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = commandPool;
    cai.commandBufferCount = 1;
    check(f.vkAllocateCommandBuffers(device, &cai, &cmd), "command buffer");
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    check(f.vkCreateFence(device, &fci, nullptr, &fence), "fence");
    if (profile_gpu) {
      VkPhysicalDeviceProperties properties{};
      f.vkGetPhysicalDeviceProperties(gpu, &properties);
      unsigned count = 0;
      f.vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
      std::vector<VkQueueFamilyProperties> queues(count);
      f.vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, queues.data());
      profile_timestamp_bits = queues.at(family).timestampValidBits;
      profile_timestamp_period = properties.limits.timestampPeriod;
      if (!profile_timestamp_bits || profile_timestamp_bits > 64 ||
          !(profile_timestamp_period > 0))
        throw std::runtime_error(
            "GPU scene timestamp profiling is unsupported");
      VkQueryPoolCreateInfo query{};
      query.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
      query.queryType = VK_QUERY_TYPE_TIMESTAMP;
      query.queryCount = 2;
      check(f.vkCreateQueryPool(device, &query, nullptr, &profile_queries),
            "scene timestamp query pool");
    }
    const VkDescriptorSetLayoutBinding bindings[2]{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = world_scale_percent() < 100 ? 2 : 1;
    li.pBindings = bindings;
    check(f.vkCreateDescriptorSetLayout(device, &li, nullptr, &setLayout),
          "descriptor set layout");
    VkPushConstantRange pr{VK_SHADER_STAGE_VERTEX_BIT |
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pr;
    check(f.vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout),
          "pipeline layout");
    if (gpu_models()) {
      VkPhysicalDeviceProperties p{};
      f.vkGetPhysicalDeviceProperties(gpu, &p);
      if (p.limits.maxPushConstantsSize < sizeof(ModelPush) ||
          p.limits.maxVertexInputAttributes < 3 ||
          p.limits.maxVertexInputBindingStride < sizeof(ModelVertex))
        throw std::runtime_error(
            "GPU cannot provide the model vertex/push contract");
      VkPushConstantRange mr{VK_SHADER_STAGE_VERTEX_BIT |
                                 VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(ModelPush)};
      pli.pPushConstantRanges = &mr;
      std::array<VkDescriptorSetLayout, 3> sets{setLayout, VK_NULL_HANDLE,
                                                VK_NULL_HANDLE};
      if (lighting_settings.enabled) {
        sunStride = (sizeof(mw34::sun::Uniform) +
                     p.limits.minUniformBufferOffsetAlignment - 1) /
                    p.limits.minUniformBufferOffsetAlignment *
                    p.limits.minUniformBufferOffsetAlignment;
        if (p.limits.maxUniformBufferRange <
            std::max(sizeof(mw34::sun::Uniform),
                     sizeof(mw34::effects::Uniform)))
          throw std::runtime_error("Sun uniform range unavailable");
        const std::array<VkDescriptorSetLayoutBinding, 5> sb{
            {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
             {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
              nullptr},
             {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
             {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
              nullptr},
             {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}};
        VkDescriptorSetLayoutCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        si.bindingCount = 5;
        si.pBindings = sb.data();
        check(
            f.vkCreateDescriptorSetLayout(device, &si, nullptr, &sunSetLayout),
            "sun descriptor layout");
        VkDescriptorSetLayoutBinding nb{
            0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo ni{};
        ni.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ni.bindingCount = 1;
        ni.pBindings = &nb;
        check(f.vkCreateDescriptorSetLayout(device, &ni, nullptr,
                                            &normalSetLayout),
              "normal descriptor layout");
        sets[1] = sunSetLayout;
        sets[2] = normalSetLayout;
        pli.setLayoutCount = 3;
        pli.pSetLayouts = sets.data();
        const auto &sun = lighting_settings;
        std::fprintf(
            stderr,
            "Per-pixel lighting: world +Y up, azimuth %.3f elevation %.3f "
            "strength %.3f ambient %.3f\n",
            double(sun.azimuth), double(sun.elevation), double(sun.strength),
            double(sun.ambient));
      }
      check(f.vkCreatePipelineLayout(device, &pli, nullptr, &modelLayout),
            "model pipeline layout");
    }
    auto module = [&](const std::uint32_t *code, size_t bytes) {
      VkShaderModuleCreateInfo mi{};
      mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      mi.codeSize = bytes;
      mi.pCode = code;
      VkShaderModule m{};
      check(f.vkCreateShaderModule(device, &mi, nullptr, &m), "shader module");
      return m;
    };
    vert = module(d3d_scene_vert_spv, sizeof(d3d_scene_vert_spv));
    frag = module(d3d_scene_frag_spv, sizeof(d3d_scene_frag_spv));
    if (world_scale_percent() < 100)
      coverageFrag =
          module(world_coverage_frag_spv, sizeof(world_coverage_frag_spv));
    if (gpu_models()) {
      modelVert = module(model_scene_vert_spv, sizeof(model_scene_vert_spv));
      worldVert = module(world_scene_vert_spv, sizeof(world_scene_vert_spv));
    }
    if (lighting_settings.enabled) {
      if (!gpu_models())
        throw std::runtime_error("Sun prototype requires GPU geometry");
      sunWorldVert = module(sun_world_vert_spv, sizeof(sun_world_vert_spv));
      sunModelVert = module(sun_model_vert_spv, sizeof(sun_model_vert_spv));
      if (use_native_geometry)
        nativeGeometryVert =
            module(native_geometry_vert_spv, sizeof(native_geometry_vert_spv));
      sunTlVert = module(sun_tl_vert_spv, sizeof(sun_tl_vert_spv));
      sunFrag = module(effect_scene_frag_spv, sizeof(effect_scene_frag_spv));
      if (lighting_settings.shadows)
        shadowVert = module(sun_shadow_vert_spv, sizeof(sun_shadow_vert_spv));
    }
    std::array<VkAttachmentDescription, 2> at{};
    at[0].format =
        rgba ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16;
    at[1].format = VK_FORMAT_D32_SFLOAT;
    for (auto &a : at) {
      a.samples = VK_SAMPLE_COUNT_1_BIT;
      a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    at[0].initialLayout = at[0].finalLayout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    at[1].initialLayout = at[1].finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &cr;
    sp.pDepthStencilAttachment = &dr;
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 2;
    rpi.pAttachments = at.data();
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sp;
    check(f.vkCreateRenderPass(device, &rpi, nullptr, &renderPass),
          "render pass");
    if (lighting_settings.enabled) {
      init_shadow();
      init_fixture_shadow();
      const auto &sun = lighting_settings;
      std::fprintf(
          stderr,
          "Sun colour %.3f %.3f %.3f, ambient tint %.3f %.3f %.3f; sky tint "
          "%.3f %.3f %.3f (%s), haze %s %.3f %.3f %.3f\n",
          sun.color[0], sun.color[1], sun.color[2], sun.ambient_color[0],
          sun.ambient_color[1], sun.ambient_color[2], sun.sky_tint[0],
          sun.sky_tint[1], sun.sky_tint[2],
          sun.sky_manual ? "manual" : (sun.sky_active ? "auto" : "off"),
          sun.haze_manual ? "colour" : (sun.haze_active ? "auto scale" : "off"),
          sun.haze_manual ? sun.haze_color[0] : sun.haze_scale[0],
          sun.haze_manual ? sun.haze_color[1] : sun.haze_scale[1],
          sun.haze_manual ? sun.haze_color[2] : sun.haze_scale[2]);
    }
    if (fxaa_enabled())
      init_quality();
    const auto grid = sample_grid();
    const char *aa_name = "AA off";
    if (fxaa_enabled())
      aa_name = "FXAA (native resolution)";
    else if (grid.y == 2)
      aa_name = "4-sample SSAA";
    else if (grid.x == 2)
      aa_name = "2-sample SSAA";
    if (fxaa_enabled() || anisotropy() > 1 || grid.x > 1)
      std::fprintf(
          stderr,
          "Remaster quality: %s, %ux AF, samplerAnisotropy %u, GPU %s\n",
          aa_name, anisotropy(), enabled.samplerAnisotropy,
          device_name.c_str());
    if (bridge_config.seh_log) {
      VkPhysicalDeviceProperties diagnostic_props{};
      f.vkGetPhysicalDeviceProperties(gpu, &diagnostic_props);
      std::uint64_t local_bytes = 0;
      for (unsigned i = 0; i < memory.memoryHeapCount; ++i)
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
          local_bytes += memory.memoryHeaps[i].size;
      std::fprintf(stderr,
                   "Playtest GPU: vendor=%04x device=%04x driver_raw=%08x "
                   "vulkan_api=%08x device_local_heap_MiB=%llu\n",
                   diagnostic_props.vendorID, diagnostic_props.deviceID,
                   diagnostic_props.driverVersion, diagnostic_props.apiVersion,
                   (unsigned long long)(local_bytes / 1048576));
    }
    pool(256);
    // 1x1 opaque white texture for draws without a texture (D3DTA_TEXTURE then
    // reads 1.0).
    create(white, 1, 1);
    create(flatNormal, 1, 1);
    ensure(texUpload, 8, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    const std::uint32_t one = 0xffffffffu;
    const std::uint32_t flat_norm = 0xffff8080u;
    std::memcpy(texUpload.mapped, &one, 4);
    std::memcpy(static_cast<char *>(texUpload.mapped) + 4, &flat_norm, 4);
    check(f.vkResetCommandPool(device, commandPool, 0), "reset command pool");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    check(f.vkBeginCommandBuffer(cmd, &bi), "begin");
    barrier(white.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    barrier(flatNormal.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    copyIn(texUpload.buffer, 0, white.image, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT);
    copyIn(texUpload.buffer, 4, flatNormal.image, 1, 1,
           VK_IMAGE_ASPECT_COLOR_BIT);
    barrier(white.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    barrier(flatNormal.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    if (shadowImage)
      barrier(shadowImage, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
              VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (fixtureShadowImage)
      barrier(fixtureShadowImage, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
              VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    check(f.vkEndCommandBuffer(cmd), "end");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    check(f.vkQueueSubmit(queue, 1, &si, fence), "submit");
    check(f.vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL),
          "wait");
    white.valid = true;
    flatNormal.valid = true;
  }
  unsigned type(VkMemoryRequirements r, VkMemoryPropertyFlags flags) {
    for (unsigned i = 0; i < memory.memoryTypeCount; ++i)
      if ((r.memoryTypeBits & (1U << i)) &&
          (memory.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    throw std::runtime_error(
        "Vulkan scene renderer: required memory type unavailable");
  }
  bool has_type(VkMemoryRequirements r, VkMemoryPropertyFlags flags) const {
    for (unsigned i = 0; i < memory.memoryTypeCount; ++i)
      if ((r.memoryTypeBits & (1U << i)) &&
          (memory.memoryTypes[i].propertyFlags & flags) == flags)
        return true;
    return false;
  }
  // Grow-only staging allocation. Callers must finish earlier uses before
  // growth destroys the old buffer. Reuse assumes the buffer keeps its original
  // usage role.
  void ensure(Buffer &b, VkDeviceSize size, VkBufferUsageFlags usage,
              bool reserve_hint = true) {
    if (b.buffer && b.size >= size)
      return;
    const auto requested = size;
    const bool reserve = bridge_config.buffer_reserve;
    if (b.buffer && reserve && reserve_hint)
      size = mw34::allocation::stream_capacity(size);
    if (b.buffer) {
      const bool trace = bridge_config.buffer_growth_trace;
      if (trace)
        std::fprintf(stderr,
                     "Buffer growth: present=%llu previous=%llu requested=%llu "
                     "capacity=%llu\n",
                     (unsigned long long)(presents + 1),
                     (unsigned long long)b.size, (unsigned long long)requested,
                     (unsigned long long)size);
      f.vkDestroyBuffer(device, b.buffer, nullptr);
      f.vkFreeMemory(device, b.memory, nullptr);
      b = {};
    }
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    const auto created = f.vkCreateBuffer(device, &bi, nullptr, &b.buffer);
    if (size != requested && (created == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
                              created == VK_ERROR_OUT_OF_HOST_MEMORY)) {
      b = {};
      ensure(b, requested, usage, false);
      return;
    }
    check(created, "buffer");
    VkMemoryRequirements r{};
    f.vkGetBufferMemoryRequirements(device, b.buffer, &r);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    // Readback buffers are read by the CPU every scene: prefer host-cached
    // memory (uncached, write-combined memory makes those reads several times
    // slower). Upload buffers take the first host-visible type.
    const VkMemoryPropertyFlags visible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    ai.allocationSize = r.size;
    ai.memoryTypeIndex =
        (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) &&
                has_type(r, visible | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
            ? type(r, visible | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
            : type(r, visible);
    const auto allocated = f.vkAllocateMemory(device, &ai, nullptr, &b.memory);
    if (size != requested && (allocated == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
                              allocated == VK_ERROR_OUT_OF_HOST_MEMORY)) {
      f.vkDestroyBuffer(device, b.buffer, nullptr);
      b = {};
      ensure(b, requested, usage, false);
      return;
    }
    check(allocated, "buffer memory");
    {
      const auto result = f.vkBindBufferMemory(device, b.buffer, b.memory, 0);
      if (size != requested && (result == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
                                result == VK_ERROR_OUT_OF_HOST_MEMORY)) {
        f.vkDestroyBuffer(device, b.buffer, nullptr);
        f.vkFreeMemory(device, b.memory, nullptr);
        b = {};
        ensure(b, requested, usage, false);
        return;
      }
      check(result, "bind buffer");
    }
    {
      const auto result =
          f.vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped);
      if (size != requested && (result == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
                                result == VK_ERROR_OUT_OF_HOST_MEMORY ||
                                result == VK_ERROR_MEMORY_MAP_FAILED)) {
        f.vkDestroyBuffer(device, b.buffer, nullptr);
        f.vkFreeMemory(device, b.memory, nullptr);
        b = {};
        ensure(b, requested, usage, false);
        return;
      }
      check(result, "map buffer");
    }
    b.size = size;
  }
  // A cached immutable device-local model buffer and its one-time upload.
  ModelBuffer &model_buffer(const ModelGeometry &g) {
    if (g.vertices.empty() ||
        (g.indices.empty()
             ? (g.vertices.size() > 65535 || g.vertices.size() % 3)
             : (g.vertices.size() > 1000000 || g.indices.size() > 6000000 ||
                g.indices.size() % 3)))
      throw std::runtime_error("GPU model geometry size mismatch");
    auto &b = model_buffers[g.key];
    if (b.local.buffer) {
      if (b.hash != g.hash || b.count != g.vertices.size() ||
          b.index_count != g.indices.size())
        throw std::runtime_error("GPU model geometry identity changed");
      return b;
    }
    const VkDeviceSize bytes =
        VkDeviceSize(g.vertices.size()) * sizeof(ModelVertex);
    ensure(b.staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::memcpy(b.staging.mapped, g.vertices.data(), size_t(bytes));
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = bytes;
    ci.usage =
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    check(f.vkCreateBuffer(device, &ci, nullptr, &b.local.buffer),
          "model vertex buffer");
    VkMemoryRequirements r{};
    f.vkGetBufferMemoryRequirements(device, b.local.buffer, &r);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = type(r, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(f.vkAllocateMemory(device, &ai, nullptr, &b.local.memory),
          "model vertex memory");
    check(f.vkBindBufferMemory(device, b.local.buffer, b.local.memory, 0),
          "bind model vertex memory");
    b.local.size = bytes;
    b.hash = g.hash;
    b.count = unsigned(g.vertices.size());
    b.index_count = unsigned(g.indices.size());
    b.source = g.source;
    if (!g.indices.empty()) {
      for (auto index : g.indices)
        if (index >= g.vertices.size())
          throw std::runtime_error("GPU geometry index outside vertices");
      const VkDeviceSize index_bytes = g.indices.size() * sizeof(std::uint32_t);
      ensure(b.index_staging, index_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      std::memcpy(b.index_staging.mapped, g.indices.data(),
                  size_t(index_bytes));
      ci.size = index_bytes;
      ci.usage =
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
      check(f.vkCreateBuffer(device, &ci, nullptr, &b.indices.buffer),
            "model index buffer");
      f.vkGetBufferMemoryRequirements(device, b.indices.buffer, &r);
      ai.allocationSize = r.size;
      ai.memoryTypeIndex = type(r, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      check(f.vkAllocateMemory(device, &ai, nullptr, &b.indices.memory),
            "model index memory");
      check(f.vkBindBufferMemory(device, b.indices.buffer, b.indices.memory, 0),
            "bind model indices");
      b.indices.size = index_bytes;
    }
    return b;
  }
  // Transfer-only images need no view. Sampled/attachment images receive a view
  // spanning every requested mip; initial contents and layout remain undefined.
  void image(VkImage &img, VkDeviceMemory &mem, VkImageView &view, unsigned w,
             unsigned h, VkFormat format, VkImageUsageFlags usage,
             VkImageAspectFlags aspect, unsigned levels = 1) {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {w, h, 1};
    ii.mipLevels = levels;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    check(f.vkCreateImage(device, &ii, nullptr, &img), "image");
    VkMemoryRequirements r{};
    f.vkGetImageMemoryRequirements(device, img, &r);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = type(r, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(f.vkAllocateMemory(device, &ai, nullptr, &mem), "image memory");
    check(f.vkBindImageMemory(device, img, mem, 0), "bind image");
    if (!(usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)))
      return;
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {aspect, 0, levels, 0, 1};
    check(f.vkCreateImageView(device, &vi, nullptr, &view), "image view");
  }
  void create(Tex &t, unsigned w, unsigned h) {
    t.levels = 1;
    if (anisotropy() > 1)
      for (unsigned n = std::max(w, h); n > 1; n >>= 1)
        ++t.levels;
    image(t.image, t.memory, t.view, w, h, VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT, t.levels);
    t.width = w;
    t.height = h;
    t.valid = false;
    t.hash = 0;
  }
  void destroy(Tex &t) {
    if (t.view)
      f.vkDestroyImageView(device, t.view, nullptr);
    if (t.image)
      f.vkDestroyImage(device, t.image, nullptr);
    if (t.memory)
      f.vkFreeMemory(device, t.memory, nullptr);
    t = {};
  }
  // Target replacement waits for the whole device, including presentation
  // users. width/height remain guest dimensions; rw/rh describe physical GPU
  // storage. Guest-sized transfer images bridge the two without resizing guest
  // allocations.
  void target(unsigned w, unsigned h, bool frontend) {
    invalidate_camera_history(mw34::temporal::Reset::transition);
    target_frontend = frontend;
    f.vkDeviceWaitIdle(device);
    destroy_world_target();
    if (framebuffer)
      f.vkDestroyFramebuffer(device, framebuffer, nullptr);
    for (Image *i :
         {&colorImage, &depthImage, &colorLo, &depthLo, &qualitySource}) {
      if (i->view)
        f.vkDestroyImageView(device, i->view, nullptr);
      if (i->image)
        f.vkDestroyImage(device, i->image, nullptr);
      if (i->memory)
        f.vkFreeMemory(device, i->memory, nullptr);
      *i = {};
    }
    scale = scale_req;
    if (scale != 1.f || widescreen()) { // scaling needs blits of both formats
      VkFormatProperties cp{}, dp{};
      f.vkGetPhysicalDeviceFormatProperties(
          gpu, rgba ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16,
          &cp);
      f.vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_D32_SFLOAT, &dp);
      const VkFormatFeatureFlags need =
          VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
      if ((cp.optimalTilingFeatures & need) != need ||
          (dp.optimalTilingFeatures & need) != need) {
        if (remaster_width())
          throw std::runtime_error("GPU cannot provide the requested "
                                   "native-resolution color/depth blits");
        scale = 1.f;
      }
    }
    rw = unsigned(std::lround(double(w) * scale));
    rh = unsigned(std::lround(double(h) * scale));
    // Guest buffers remain at the selected original resolution. A wider
    // physical world target is paired with the original camera/frustum's
    // explicit Hor+ correction; HUD coordinates are handled separately.
    if (remaster_width()) {
      if (target_frontend) {
        const auto fit = mw34::frontend::CanvasFit::fit(w, h, remaster_width(),
                                                        remaster_height());
        rw = unsigned(fit.width);
        rh = unsigned(fit.height);
      } else {
        rw = remaster_width();
        rh = remaster_height();
      }
    }
    const auto samples = sample_grid();
    rw *= samples.x;
    rh *= samples.y;
    VkPhysicalDeviceProperties limits{};
    f.vkGetPhysicalDeviceProperties(gpu, &limits);
    if (rw > limits.limits.maxImageDimension2D ||
        rh > limits.limits.maxImageDimension2D ||
        rw > limits.limits.maxFramebufferWidth ||
        rh > limits.limits.maxFramebufferHeight)
      throw std::runtime_error(
          "Requested resolution and SSAA exceed GPU target limits");
    std::fprintf(
        stderr,
        "Display target: output %ux%u, raster %ux%u, SSAA %ux%u, frontend %u\n",
        remaster_width(), remaster_height(), rw, rh, samples.x, samples.y,
        unsigned(target_frontend));
    scale_x = float(rw) / float(w);
    if (rw != w || rh != h || world_scale_percent() != 100) {
      image(colorLo.image, colorLo.memory, colorLo.view, w, h,
            rgba ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
      image(depthLo.image, depthLo.memory, depthLo.view, w, h,
            VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    image(colorImage.image, colorImage.memory, colorImage.view, rw, rh,
          rgba ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT);
    image(depthImage.image, depthImage.memory, depthImage.view, rw, rh,
          VK_FORMAT_D32_SFLOAT,
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
          VK_IMAGE_ASPECT_DEPTH_BIT);
    const std::array<VkImageView, 2> views{colorImage.view, depthImage.view};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = renderPass;
    fi.attachmentCount = 2;
    fi.pAttachments = views.data();
    fi.width = rw;
    fi.height = rh;
    fi.layers = 1;
    check(f.vkCreateFramebuffer(device, &fi, nullptr, &framebuffer),
          "framebuffer");
    quality_world_dirty = false;
    if (fxaa_enabled()) {
      image(qualitySource.image, qualitySource.memory, qualitySource.view, rw,
            rh, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
      VkDescriptorImageInfo info{sampler(3U | (3U << 4) | (3U << 8)),
                                 qualitySource.view,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      VkWriteDescriptorSet wr{};
      wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      wr.dstSet = qualitySet;
      wr.descriptorCount = 1;
      wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      wr.pImageInfo = &info;
      f.vkUpdateDescriptorSets(device, 1, &wr, 0, nullptr);
    }
    width = w;
    height = h;
    create_world_target();
  }
  void pool(unsigned n) {
    if (descriptorPool)
      f.vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    const std::array<VkDescriptorPoolSize, 4> ps{
        {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, n * 3 + 2},
         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = n * 2 + 1;
    pi.poolSizeCount = lighting_settings.enabled ? 4 : 1;
    pi.pPoolSizes = ps.data();
    check(f.vkCreateDescriptorPool(device, &pi, nullptr, &descriptorPool),
          "descriptor pool");
    descriptorCapacity = n;
  }
  // Sampler key: bit 0 magnification linear, bit 1 minification linear,
  // bits 4..7 U address, 8..11 V address (D3DTADDRESS: 1 wrap, 2 mirror, 3
  // clamp). Bit16 tags world draws for AF; HUD, text and the FXAA input use
  // their own level-zero sampling unchanged.
  VkSampler sampler(unsigned key) {
    auto it = samplers.find(key);
    if (it != samplers.end())
      return it->second;
    auto mode = [](unsigned a) {
      return a == 2   ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
             : a == 3 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                      : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = (key & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.minFilter = (key & 2) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = mode((key >> 4) & 15);
    si.addressModeV = mode((key >> 8) & 15);
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if ((key & 0x10000U) && anisotropy() > 1) {
      si.magFilter = si.minFilter = VK_FILTER_LINEAR;
      si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      si.anisotropyEnable = VK_TRUE;
      si.maxAnisotropy = float(anisotropy());
      si.maxLod = VK_LOD_CLAMP_NONE;
      ++af_samplers;
    }
    VkSampler s{};
    check(f.vkCreateSampler(device, &si, nullptr, &s), "sampler");
    return samplers[key] = s;
  }
  VkPipeline pipeline(const PipelineKey &k, bool model = false,
                      bool world = false, bool lit = false,
                      bool native_geometry = false,
                      bool clockwise_front = false) {
    // points uses bit56 (0/1); bit57 distinguishes this private model pipeline.
    const std::uint64_t key =
        k.packed() | (model ? (1ULL << 57) : 0ULL) |
        (world ? (1ULL << 58) : 0ULL) | (lit ? (1ULL << 59) : 0ULL) |
        (reconstruction_stage == ReconstructionStage::coverage ? (1ULL << 60)
                                                               : 0ULL) |
        (native_geometry ? (1ULL << 61) : 0ULL) |
        (clockwise_front ? (1ULL << 62) : 0ULL);
    auto it = pipelines.find(key);
    if (it != pipelines.end())
      return it->second;
    std::array<VkPipelineShaderStageCreateInfo, 2> st{};
    for (int i = 0; i < 2; ++i) {
      st[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      st[i].stage =
          i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
      st[i].module =
          lit ? (i ? sunFrag
                   : (world ? sunWorldVert
                            : (model ? sunModelVert : sunTlVert)))
              : (i ? frag : (world ? worldVert : (model ? modelVert : vert)));
      if (i && reconstruction_stage == ReconstructionStage::coverage)
        st[i].module = coverageFrag;
      if (!i && native_geometry)
        st[i].module = nativeGeometryVert;
      st[i].pName = "main";
    }
    VkVertexInputBindingDescription vb{0, 32, VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 4> va{
        {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
         {1, 0, VK_FORMAT_R32_UINT, 16},
         {2, 0, VK_FORMAT_R32_UINT, 20},
         {3, 0, VK_FORMAT_R32G32_SFLOAT, 24}}};
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    const std::array<VkVertexInputAttributeDescription, 3> ma{
        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
         {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
         {2, 0, VK_FORMAT_R32G32_SFLOAT, 24}}};
    const std::array<VkVertexInputBindingDescription, 2> wb{
        {{0, 32, VK_VERTEX_INPUT_RATE_VERTEX},
         {1, 32, VK_VERTEX_INPUT_RATE_VERTEX}}};
    const std::array<VkVertexInputAttributeDescription, 5> wa{
        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
         {1, 1, VK_FORMAT_R32_UINT, 16},
         {2, 1, VK_FORMAT_R32_UINT, 20},
         {3, 1, VK_FORMAT_R32G32_SFLOAT, 24},
         {4, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}}};
    const std::array<VkVertexInputBindingDescription, 2> tb{
        {{0, 32, VK_VERTEX_INPUT_RATE_VERTEX},
         {1, 12, VK_VERTEX_INPUT_RATE_VERTEX}}};
    const std::array<VkVertexInputAttributeDescription, 5> ta{
        {va[0], va[1], va[2], va[3], {4, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}}};
    const bool lit_tl = lit && !model;
    vi.vertexBindingDescriptionCount = world || lit_tl ? 2 : 1;
    vi.pVertexBindingDescriptions = world    ? wb.data()
                                    : lit_tl ? tb.data()
                                             : &vb;
    vi.vertexAttributeDescriptionCount = world    ? (lit ? 5 : 4)
                                         : model  ? 3
                                         : lit_tl ? 5
                                                  : 4;
    vi.pVertexAttributeDescriptions = world    ? wa.data()
                                      : model  ? ma.data()
                                      : lit_tl ? ta.data()
                                               : va.data();
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = k.points ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                           : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1;
    // D3DCULL_CW culls clockwise screen-space triangles, D3DCULL_CCW
    // counter-clockwise ones (y down in both APIs).
    rs.cullMode = k.cull == 1 ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    rs.frontFace = clockwise_front || k.cull == 3
                       ? VK_FRONT_FACE_CLOCKWISE
                       : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = k.zenable;
    ds.depthWriteEnable = k.zenable && k.zwrite;
    ds.depthCompareOp = VkCompareOp(k.zfunc - 1);
    auto factor = [](unsigned b) {
      static const VkBlendFactor t[] = {VK_BLEND_FACTOR_ZERO,
                                        VK_BLEND_FACTOR_ZERO,
                                        VK_BLEND_FACTOR_ONE,
                                        VK_BLEND_FACTOR_SRC_COLOR,
                                        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                        VK_BLEND_FACTOR_SRC_ALPHA,
                                        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                        VK_BLEND_FACTOR_DST_ALPHA,
                                        VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
                                        VK_BLEND_FACTOR_DST_COLOR,
                                        VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                        VK_BLEND_FACTOR_SRC_ALPHA_SATURATE};
      return t[b];
    };
    VkPipelineColorBlendAttachmentState cb{};
    cb.colorWriteMask = 15;
    cb.blendEnable = k.blend;
    unsigned src = k.src, dst = k.dst;
    if (src == 12) {
      src = 5;
      dst = 6;
    } else if (src == 13) {
      src = 6;
      dst = 5;
    } // D3DBLEND_BOTHSRCALPHA / BOTHINVSRCALPHA
    cb.srcColorBlendFactor = factor(src);
    cb.dstColorBlendFactor = factor(dst);
    // Match the stock SDL blend modes, including the saved frame alpha.
    cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkPipelineColorBlendStateCreateInfo bs{};
    bs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    bs.attachmentCount = 1;
    bs.pAttachments = &cb;
    const std::array<VkDynamicState, 2> dyn{VK_DYNAMIC_STATE_VIEWPORT,
                                            VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{};
    dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn.data();
    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = st.data();
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vps;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &bs;
    pi.pDynamicState = &dy;
    pi.layout = (model || lit) ? modelLayout : pipelineLayout;
    pi.renderPass = renderPass;
    VkPipeline p{};
    check(f.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr,
                                      &p),
          "graphics pipeline");
    return pipelines[key] = p;
  }
  void barrier(VkImage img, VkImageLayout from, VkImageLayout to,
               VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags ss,
               VkPipelineStageFlags ds, VkImageAspectFlags aspect,
               unsigned base = 0, unsigned levels = 1) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {aspect, base, levels, 0, 1};
    f.vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
  }
  // Round both edges independently, then derive the extent. Rounding an extent
  // alone can leave gaps between adjacent guest rectangles at fractional
  // scales.
  VkRect2D scaled(const VkRect2D &r) const {
    if (rw == width && rh == height)
      return r;
    const int x0 = int(std::lround(r.offset.x * double(scale_x))),
              y0 = int(std::lround(r.offset.y * (double(rh) / height)));
    const int x1 = int(std::lround((r.offset.x + int(r.extent.width)) *
                                   double(scale_x))),
              y1 = int(std::lround((r.offset.y + int(r.extent.height)) *
                                   (double(rh) / height)));
    return {{x0, y0},
            {unsigned(std::max(x1 - x0, 0)), unsigned(std::max(y1 - y0, 0))}};
  }
  void blit_img(VkImage src, unsigned sw, unsigned sh, VkImage dst, unsigned dw,
                unsigned dh, VkImageAspectFlags aspect, VkFilter filter) {
    VkImageBlit b{};
    b.srcSubresource = b.dstSubresource = {aspect, 0, 0, 1};
    b.srcOffsets[1] = {int(sw), int(sh), 1};
    b.dstOffsets[1] = {int(dw), int(dh), 1};
    f.vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, filter);
  }
  // Guest-resolution upload into the target images (which are in
  // transfer-destination layout).
  void copy_in_scaled(VkDeviceSize depthOffset) {
    if (rw == width && rh == height) {
      copyIn(upload.buffer, 0, colorImage.image, width, height,
             VK_IMAGE_ASPECT_COLOR_BIT);
      copyIn(upload.buffer, depthOffset, depthImage.image, width, height,
             VK_IMAGE_ASPECT_DEPTH_BIT);
      return;
    }
    for (auto [img, aspect] :
         {std::pair{colorLo.image,
                    VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT)},
          {depthLo.image, VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT)}})
      barrier(img, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, aspect);
    copyIn(upload.buffer, 0, colorLo.image, width, height,
           VK_IMAGE_ASPECT_COLOR_BIT);
    copyIn(upload.buffer, depthOffset, depthLo.image, width, height,
           VK_IMAGE_ASPECT_DEPTH_BIT);
    for (auto [img, aspect] :
         {std::pair{colorLo.image,
                    VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT)},
          {depthLo.image, VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT)}})
      barrier(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
              aspect);
    blit_img(colorLo.image, width, height, colorImage.image, rw, rh,
             VK_IMAGE_ASPECT_COLOR_BIT, VK_FILTER_NEAREST);
    blit_img(depthLo.image, width, height, depthImage.image, rw, rh,
             VK_IMAGE_ASPECT_DEPTH_BIT, VK_FILTER_NEAREST);
  }
  // Guest color plus full-raster D32 copy. Keep depth resampling out of the
  // driver's image-blit path; copy_back samples it at the guest boundary.
  void copy_out_scaled(VkDeviceSize depthOffset) {
    if (rw == width && rh == height) {
      copyOut(colorImage.image, readback.buffer, 0, VK_IMAGE_ASPECT_COLOR_BIT);
      copyOut(depthImage.image, readback.buffer, depthOffset,
              VK_IMAGE_ASPECT_DEPTH_BIT);
      return;
    }
    barrier(colorLo.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    blit_img(colorImage.image, rw, rh, colorLo.image, width, height,
             VK_IMAGE_ASPECT_COLOR_BIT, VK_FILTER_LINEAR);
    barrier(colorLo.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    copyOut(colorLo.image, readback.buffer, 0, VK_IMAGE_ASPECT_COLOR_BIT);
    copyOut(depthImage.image, readback.buffer, depthOffset,
            VK_IMAGE_ASPECT_DEPTH_BIT, rw, rh);
  }
  void copyIn(VkBuffer buf, VkDeviceSize off, VkImage img, unsigned w,
              unsigned h, VkImageAspectFlags aspect) {
    VkBufferImageCopy c{};
    c.bufferOffset = off;
    c.imageSubresource = {aspect, 0, 0, 1};
    c.imageExtent = {w, h, 1};
    f.vkCmdCopyBufferToImage(cmd, buf, img,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
  }
  void copyOut(VkImage img, VkBuffer buf, VkDeviceSize off,
               VkImageAspectFlags aspect, unsigned copy_width = 0,
               unsigned copy_height = 0) {
    VkBufferImageCopy c{};
    c.bufferOffset = off;
    c.imageSubresource = {aspect, 0, 0, 1};
    c.imageExtent = {copy_width ? copy_width : width,
                     copy_height ? copy_height : height, 1};
    f.vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             buf, 1, &c);
  }

  bool surface(cc_mw3_re::platform::NativeWindowHandle hwnd) {
    if (dxgi.active()) {
      presentWindow = hwnd;
      return true;
    }
    if (surface_source.create) {
      windowSurface = surface_source.create(instance);
      presentWindow = hwnd;
      return windowSurface != VK_NULL_HANDLE;
    }
#if defined(_WIN32)
    windowSurface = cc_mw3_re::platform::create_win32_surface(
        instance, hwnd, f.vkCreateWin32SurfaceKHR);
#else
    windowSurface = VK_NULL_HANDLE;
#endif
    check(windowSurface != VK_NULL_HANDLE ? VK_SUCCESS
                                          : VK_ERROR_INITIALIZATION_FAILED,
          "window surface");
    VkBool32 ok = VK_FALSE;
    check(
        f.vkGetPhysicalDeviceSurfaceSupportKHR(gpu, family, windowSurface, &ok),
        "surface support");
    if (!ok) {
      dropSurface();
      return false;
    }
    presentWindow = hwnd;
    return true;
  }
  void dropSurface() {
    if (windowSurface) {
      if (surface_source.destroy)
        surface_source.destroy(instance, windowSurface);
      else
        f.vkDestroySurfaceKHR(instance, windowSurface, nullptr);
    }
    windowSurface = {};
    presentWindow = {};
  }
  void dropSwapchain() {
    if (dxgi.active()) {
      dxgi.reset_window();
      return;
    }
    if (!swapchain)
      return;
    f.vkDeviceWaitIdle(device);
    exclusive_fullscreen_.release(device, swapchain);
    f.vkDestroySwapchainKHR(device, swapchain, nullptr);
    for (auto sem : swapRendered)
      f.vkDestroySemaphore(device, sem, nullptr);
    swapRendered.clear();
    swapchain = {};
    swapImages.clear();
  }
  // A zero-sized surface is a temporary no-present state. A changed extent
  // drains presentation and rebuilds both the swapchain and its per-image
  // semaphores.
  bool swapchainReady() {
    if (dxgi.active()) {
      long cw = 0, ch = 0;
      if (surface_source.get_drawable_size) {
        surface_source.get_drawable_size(presentWindow, &cw, &ch);
      } else {
        cc_mw3_re::platform::get_window_client_rect(child, &cw, &ch);
      }
      if ((!surface_source.create &&
           cc_mw3_re::platform::is_window_iconic(presentParent)) ||
          cw <= 0 || ch <= 0)
        return false;
      dxgi.prepare(static_cast<HWND>(child), unsigned(cw), unsigned(ch));
      swapExtent = {unsigned(cw), unsigned(ch)};
      return true;
    }
    VkSurfaceCapabilitiesKHR caps{};
    if (const VkResult r = f.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            gpu, windowSurface, &caps);
        r != VK_SUCCESS) {
      long rw = 0, rh = 0;
      cc_mw3_re::platform::get_window_client_rect(presentWindow, &rw, &rh);
      throw std::runtime_error(
          "Vulkan presenter: surface capabilities failed (" +
          std::to_string(int(r)) + ") for window " +
          std::to_string(reinterpret_cast<std::uintptr_t>(presentWindow)) +
          " valid " +
          std::to_string(cc_mw3_re::platform::is_window_valid(presentWindow)) +
          " visible " +
          std::to_string(
              cc_mw3_re::platform::is_window_visible(presentWindow)) +
          " client " + std::to_string(rw) + "x" + std::to_string(rh) +
          " style " +
          std::to_string(cc_mw3_re::platform::get_window_style(presentWindow)) +
          " exstyle " +
          std::to_string(
              cc_mw3_re::platform::get_window_exstyle(presentWindow)));
    }
    VkExtent2D e = caps.currentExtent;
    if (e.width == 0xffffffffu) {
      long rw = 0, rh = 0;
      if (surface_source.get_drawable_size) {
        surface_source.get_drawable_size(presentWindow, &rw, &rh);
      } else {
        cc_mw3_re::platform::get_window_client_rect(presentWindow, &rw, &rh);
      }
      e = {unsigned(rw), unsigned(rh)};
    }
    if (!e.width || !e.height)
      return false;
    if (swapchain && e.width == swapExtent.width &&
        e.height == swapExtent.height)
      return syncExclusive();
    dropSwapchain();
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
      throw std::runtime_error(
          "Vulkan presenter: swapchain images cannot be transfer destinations");
    unsigned n = 0;
    check(
        f.vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, windowSurface, &n, nullptr),
        "surface formats");
    std::vector<VkSurfaceFormatKHR> formats(n);
    check(f.vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, windowSurface, &n,
                                                 formats.data()),
          "surface formats");
    VkSurfaceFormatKHR fmt = formats.at(0);
    for (const auto &x : formats)
      if (x.format == VK_FORMAT_B8G8R8A8_UNORM)
        fmt = x;
    VkSwapchainCreateInfoKHR sc{};
    sc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sc.surface = windowSurface;
    exclusive_fullscreen_.setup_swapchain_chain(presentParent);
    if (exclusive_fullscreen_.is_device_exclusive()) {
      if (!exclusive_fullscreen_.validate_swapchain_support(gpu, windowSurface))
        throw std::runtime_error("Exclusive fullscreen is not supported for "
                                 "this window and monitor");
      sc.pNext = exclusive_fullscreen_.swapchain_pnext();
    }
    sc.minImageCount = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount)
      sc.minImageCount = std::min(sc.minImageCount, caps.maxImageCount);
    sc.imageFormat = fmt.format;
    sc.imageColorSpace = fmt.colorSpace;
    sc.imageExtent = e;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha =
        (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            : VkCompositeAlphaFlagBitsKHR(caps.supportedCompositeAlpha &
                                          -caps.supportedCompositeAlpha);
    sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sc.clipped = VK_TRUE;
    if (!bridge_config.vsync && f.vkGetPhysicalDeviceSurfacePresentModesKHR) {
      unsigned pm = 0;
      f.vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, windowSurface, &pm,
                                                  nullptr);
      std::vector<VkPresentModeKHR> modes(pm);
      f.vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, windowSurface, &pm,
                                                  modes.data());
      auto has = [&](VkPresentModeKHR m) {
        return std::find(modes.begin(), modes.end(), m) != modes.end();
      };
      sc.presentMode =
          has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR
          : has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
                                             : VK_PRESENT_MODE_FIFO_KHR;
      static bool told = false;
      if (!told) {
        told = true;
        std::fprintf(stderr, "Vsync off: present mode %s\n",
                     sc.presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR
                         ? "IMMEDIATE"
                     : sc.presentMode == VK_PRESENT_MODE_MAILBOX_KHR
                         ? "MAILBOX"
                         : "FIFO (no alternative)");
      }
    }
    check(f.vkCreateSwapchainKHR(device, &sc, nullptr, &swapchain),
          "swapchain");
    check(f.vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr),
          "swapchain images");
    swapImages.resize(n);
    check(f.vkGetSwapchainImagesKHR(device, swapchain, &n, swapImages.data()),
          "swapchain images");
    swapRendered.resize(n);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto &sem : swapRendered)
      check(f.vkCreateSemaphore(device, &semaphoreInfo, nullptr, &sem),
            "per-image presentation semaphore");
    swapExtent = e;
    return syncExclusive();
  }
  cc_mw3_re::platform::ExclusiveFullscreenHandler exclusive_fullscreen_;
  bool exclusiveRequested() const {
    return bridge_config.window_mode == "fullscreen";
  }
  bool syncExclusive() {
    return exclusive_fullscreen_.sync(device, swapchain, presentParent);
  }
  bool srcRgba{};
  void presentSource(unsigned w, unsigned h, bool rgba32 = false) {
    f.vkDeviceWaitIdle(device);
    if (srcImage.view)
      f.vkDestroyImageView(device, srcImage.view, nullptr);
    if (srcImage.image)
      f.vkDestroyImage(device, srcImage.image, nullptr);
    if (srcImage.memory)
      f.vkFreeMemory(device, srcImage.memory, nullptr);
    srcImage = {};
    image(srcImage.image, srcImage.memory, srcImage.view, w, h,
          rgba32 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT);
    srcWidth = w;
    srcHeight = h;
    srcRgba = rgba32;
  }
  // Drain all queued work before releasing resources. Views precede images, and
  // image/buffer handles precede their backing memory; the loader is released
  // last.
  void shutdown() {
    if (!device) {
      dropSurface();
      if (instance && f.vkDestroyInstance)
        f.vkDestroyInstance(instance, nullptr);
      loader.unload();
      return;
    }
    f.vkDeviceWaitIdle(device);
    if (coverage_scenes)
      std::fprintf(
          stderr,
          "World reconstruction: %llu opaque scenes, %llu coverage draws\n",
          (unsigned long long)coverage_scenes,
          (unsigned long long)coverage_draws);
    collect_profile_queries();
    if (profile_queries) {
      std::fprintf(stderr,
                   "GPU scene profile (includes warmup/inline quality; "
                   "excludes separate "
                   "quality/presentation): "
                   "%llu submissions, %.4f ms\n",
                   (unsigned long long)profile_gpu_submissions,
                   double(profile_gpu_ticks) * profile_timestamp_period /
                       1000000.0);
      f.vkDestroyQueryPool(device, profile_queries, nullptr);
      profile_queries = VK_NULL_HANDLE;
    }
    if (profile_cpu)
      std::fprintf(
          stderr, "CPU scene wait profile (includes warmup): %.4f ms\n",
          double(profile_wait_ticks) * 1000.0 / SDL_GetPerformanceFrequency());
    std::fprintf(stderr,
                 "Submission: %llu logical draws, %llu GPU draws, %llu "
                 "adjacent TL draws combined\n",
                 (unsigned long long)draws, (unsigned long long)submitted_draws,
                 (unsigned long long)combined_draws);
    if (qualityPipeline)
      f.vkDestroyPipeline(device, qualityPipeline, nullptr);
    if (qualityLayout)
      f.vkDestroyPipelineLayout(device, qualityLayout, nullptr);
    if (qualityPool)
      f.vkDestroyDescriptorPool(device, qualityPool, nullptr);
    if (qualityVert)
      f.vkDestroyShaderModule(device, qualityVert, nullptr);
    if (qualityFrag)
      f.vkDestroyShaderModule(device, qualityFrag, nullptr);
    if (coverageFrag)
      f.vkDestroyShaderModule(device, coverageFrag, nullptr);
    for (auto &[key, b] : model_buffers)
      for (Buffer *p : {&b.local, &b.staging, &b.indices, &b.index_staging}) {
        if (p->buffer)
          f.vkDestroyBuffer(device, p->buffer, nullptr);
        if (p->memory)
          f.vkFreeMemory(device, p->memory, nullptr);
      }
    model_buffers.clear();
    if (emission_draw_records)
      std::fprintf(stderr,
                   "Draw emission storage: %llu draw records, %llu payloads, "
                   "Draw size %zu bytes\n",
                   (unsigned long long)emission_draw_records,
                   (unsigned long long)emission_payloads, sizeof(Draw));
    destroy_tiled();
    for (auto &[k, t] : textures)
      destroy(t);
    destroy(white);
    destroy(flatNormal);
    for (auto &[k, p] : pipelines)
      f.vkDestroyPipeline(device, p, nullptr);
    for (auto &[k, s] : samplers)
      f.vkDestroySampler(device, s, nullptr);
    dxgi.detach();
    dropSwapchain();
    dropSurface();
    if (child)
      cc_mw3_re::platform::destroy_present_child(child);
    if (srcImage.view)
      f.vkDestroyImageView(device, srcImage.view, nullptr);
    if (srcImage.image)
      f.vkDestroyImage(device, srcImage.image, nullptr);
    if (srcImage.memory)
      f.vkFreeMemory(device, srcImage.memory, nullptr);
    if (acquired)
      f.vkDestroySemaphore(device, acquired, nullptr);
    if (presentFence)
      f.vkDestroyFence(device, presentFence, nullptr);
    if (presentPool)
      f.vkDestroyCommandPool(device, presentPool, nullptr);
    for (Buffer *b : {&upload, &readback, &vertexBuffer, &normalBuffer,
                      &texUpload, &presentUpload, &sunBuffer, &effectBuffer})
      if (b->buffer) {
        f.vkDestroyBuffer(device, b->buffer, nullptr);
        f.vkFreeMemory(device, b->memory, nullptr);
      }
    if (worldColor.image) {
      std::fprintf(stderr,
                   "World resolution: reduced scenes %llu, promotions %llu\n",
                   (unsigned long long)reduced_world_scenes,
                   (unsigned long long)world_promotions);
    }
    destroy_world_target();
    if (framebuffer)
      f.vkDestroyFramebuffer(device, framebuffer, nullptr);
    for (Image *i :
         {&colorImage, &depthImage, &colorLo, &depthLo, &qualitySource}) {
      if (i->view)
        f.vkDestroyImageView(device, i->view, nullptr);
      if (i->image)
        f.vkDestroyImage(device, i->image, nullptr);
      if (i->memory)
        f.vkFreeMemory(device, i->memory, nullptr);
    }
    f.vkDestroyRenderPass(device, renderPass, nullptr);
    f.vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (modelLayout)
      f.vkDestroyPipelineLayout(device, modelLayout, nullptr);
    f.vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    f.vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    drop_fixture_shadow();
    if (shadowPipeline)
      f.vkDestroyPipeline(device, shadowPipeline, nullptr);
    if (shadowLayout)
      f.vkDestroyPipelineLayout(device, shadowLayout, nullptr);
    if (shadowFramebuffer)
      f.vkDestroyFramebuffer(device, shadowFramebuffer, nullptr);
    if (shadowPass)
      f.vkDestroyRenderPass(device, shadowPass, nullptr);
    if (shadowSampler)
      f.vkDestroySampler(device, shadowSampler, nullptr);
    if (shadowView)
      f.vkDestroyImageView(device, shadowView, nullptr);
    if (shadowImage)
      f.vkDestroyImage(device, shadowImage, nullptr);
    if (shadowMemory)
      f.vkFreeMemory(device, shadowMemory, nullptr);
    if (shadowVert)
      f.vkDestroyShaderModule(device, shadowVert, nullptr);
    if (sunSetLayout)
      f.vkDestroyDescriptorSetLayout(device, sunSetLayout, nullptr);
    if (normalSetLayout)
      f.vkDestroyDescriptorSetLayout(device, normalSetLayout, nullptr);
    if (sunWorldVert)
      f.vkDestroyShaderModule(device, sunWorldVert, nullptr);
    if (nativeGeometryVert)
      f.vkDestroyShaderModule(device, nativeGeometryVert, nullptr);
    if (sunModelVert)
      f.vkDestroyShaderModule(device, sunModelVert, nullptr);
    if (sunTlVert)
      f.vkDestroyShaderModule(device, sunTlVert, nullptr);
    if (sunFrag)
      f.vkDestroyShaderModule(device, sunFrag, nullptr);
    if (modelVert)
      f.vkDestroyShaderModule(device, modelVert, nullptr);
    if (worldVert)
      f.vkDestroyShaderModule(device, worldVert, nullptr);
    f.vkDestroyShaderModule(device, vert, nullptr);
    f.vkDestroyShaderModule(device, frag, nullptr);
    f.vkDestroyFence(device, fence, nullptr);
    f.vkDestroyCommandPool(device, commandPool, nullptr);
    f.vkDestroyDevice(device, nullptr);
    f.vkDestroyInstance(instance, nullptr);
    loader.unload();
    device = {};
  }
#include "quality_stage.inc"
#include "world_resolution_stage.inc"
};
} // namespace mw34::vkscene
