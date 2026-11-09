#include "frame_renderer.hpp"

#include "frontend/sdl/sdl_child.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "renderer/campaign_environment_policy.hpp"
#include "renderer/campaign_lighting.hpp"
#include "renderer/campaign_sun_settings.hpp"
#include "renderer/effect_light_observations.hpp"
#include "renderer/effect_lighting_stage.inc"
#include "renderer/material_normals.hpp"
#include "renderer/native_surface_identity.hpp"
#include "renderer/surface_detail.hpp"
#include "renderer/surface_materials.hpp"
#include <limits>

namespace mw34::materials {
void set_active_surface_key(const SurfaceKey &key);
void clear_active_surface_key();
bool get_active_surface_key(SurfaceKey *out);
} // namespace mw34::materials
#include "tests/prepared_visual_fixture.h"
#include <cfenv>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string_view>

namespace {
bool threaded_checks = false;
std::unique_ptr<cc_mw3_re::VulkanFrameRenderer>
make_check_renderer(SDL_Window *window, sdlfe::RendererBackend backend,
                    const sdlfe::CoreCriticalQuality &quality = {},
                    sdlfe::CoreCriticalAssets assets = {}) {
  assets.render_thread = threaded_checks;
  return cc_mw3_re::make_renderer(window, backend, quality, assets);
}

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

#include "local_light_checks.inc"

void quad(platform::Render3D &renderer, float low, float high, float depth,
          uint32_t color, const platform::DrawState &state = {}) {
  const platform::TLVertex vertices[] = {{low, low, depth, 1, color, 0, 0, 0},
                                         {high, low, depth, 1, color, 0, 1, 0},
                                         {high, high, depth, 1, color, 0, 1, 1},
                                         {low, high, depth, 1, color, 0, 0, 1}};
  renderer.draw(platform::Primitive::kFan, vertices, 4, state);
}

#include "local_light_gpu_checks.inc"

void check_scene(SDL_Window *window, sdlfe::RendererBackend backend) {
  auto renderer = make_check_renderer(window, backend);
  renderer->set_viewport(0, 0, 32, 32);
  renderer->clear(true, 0, 0, 255);
  renderer->begin_scene();
  quad(*renderer, 8, 24, .75f, 0xff00ff00);
  quad(*renderer, 4, 28, .25f, 0xffff0000);
  renderer->end_scene();
  auto pixel = [&](int x, int y) {
    std::vector<uint32_t> pixels;
    int width = 0, height = 0;
    require(renderer->read_back(&pixels, &width, &height) && width == 32 &&
                height == 32,
            "Could not read the Vulkan scene");
    return pixels.at(y * width + x);
  };
  require(pixel(16, 16) == 0xff00ff00 && pixel(6, 6) == 0xffff0000 &&
              pixel(2, 2) == 0xff0000ff,
          "Draw order or reversed depth changed");

  require(renderer->hold_frame(), "Could not hold the composed frame");
  renderer->clear(false, 0, 0, 0);
  const uint8_t yellow[] = {255, 255, 0, 255};
  const uint8_t cyan[] = {0, 255, 255, 255};
  const auto first = renderer->create_texture(1, 1, yellow, 1);
  platform::DrawState textured{};
  textured.texture = first;
  quad(*renderer, 12, 20, .125f, 0xffffffff, textured);
  renderer->release_texture(first);
  require(pixel(16, 16) == 0xffffff00 && pixel(6, 6) == 0xffff0000,
          "Depth-only clear changed color or a released texture lost its "
          "queued draw");
  textured.texture = renderer->create_texture(1, 1, cyan, 1);
  quad(*renderer, 12, 20, .5f, 0xffffffff, textured);
  renderer->release_texture(textured.texture);
  require(pixel(16, 16) == 0xff00ffff, "Reused slot kept stale texture pixels");
  require(renderer->restore_frame() && pixel(16, 16) == 0xff00ff00,
          "Restored frame lost its saved pixels");
  renderer->drop_frame();

  renderer->reset_scene_resources();
  renderer->clear(true, 0, 0, 0);
  textured.texture = renderer->create_texture(1, 1, cyan, 1);
  quad(*renderer, 12, 20, .5f, 0xffffffff, textured);
  renderer->release_texture(textured.texture);
  require(pixel(16, 16) == 0xff00ffff && pixel(6, 6) == 0xff000000,
          "A new scene inherited the previous color or texture");

  renderer->clear(true, 0, 0, 255);
  const uint8_t premultiplied_red[] = {128, 0, 0, 128};
  platform::DrawState hud{};
  hud.texture = renderer->create_texture(1, 1, premultiplied_red, 4);
  hud.address_u = hud.address_v = 3;
  hud.sharp = hud.blend = hud.depth_always = true;
  hud.depth_write = false;
  quad(*renderer, 0, 32, 0, 0xffffffff, hud);
  renderer->release_texture(hud.texture);
  require(pixel(16, 16) == 0xff80007f,
          "The sharp HUD layer lost its premultiplied color or alpha");

  // A cropped 4x1 resource drawn over 13x32 pixels has different X/Y
  // densities. A full-resource/window ratio incorrectly blurs this edge.
  const uint8_t cropped[] = {0, 255, 0, 255, 128, 0,   0, 128,
                             0, 0,   0, 0,   0,   255, 0, 255};
  hud.texture = renderer->create_texture(4, 1, cropped, 4);
  const platform::TLVertex sprite[] = {
      {.25f, 0, 0, 1, 0xffffffff, 0, .25f, 0},
      {13.25f, 0, 0, 1, 0xffffffff, 0, .75f, 0},
      {13.25f, 32, 0, 1, 0xffffffff, 0, .75f, 1},
      {.25f, 32, 0, 1, 0xffffffff, 0, .25f, 1}};
  renderer->clear(true, 0, 0, 255);
  renderer->draw(platform::Primitive::kFan, sprite, 4, hud);
  renderer->release_texture(hud.texture);
  require(pixel(5, 16) == 0xff80007f && pixel(6, 16) == 0xff60009f &&
              pixel(8, 16) == 0xff0000ff,
          "Sharp sprite density, crop or transparent-edge color changed");

  renderer->clear(true, 200, 100, 50);
  const uint8_t shadow_color[] = {128, 255, 128, 128};
  platform::DrawState shadow{};
  shadow.texture = renderer->create_texture(1, 1, shadow_color, 4);
  shadow.multiply = true;
  quad(*renderer, 0, 32, .5f, 0xffffffff, shadow);
  renderer->release_texture(shadow.texture);
  require(pixel(16, 16) == 0xff646419,
          "The multiply blend changed stock color or alpha");

  // Vary reciprocal W so a screen-linear shader would give a different sample.
  platform::TLVertex triangle[] = {{0, 0, .5f, 1, 0xffff0000, 0xff000000, 0, 0},
                                   {32, 0, .5f, .25f, 0xff0000ff, 0, 0, 0},
                                   {0, 32, .5f, .25f, 0xff0000ff, 0, 0, 0}};
  renderer->clear(true, 0, 0, 0);
  renderer->draw(platform::Primitive::kFan, triangle, 3, {});
  require(pixel(8, 8) == 0xffc70038,
          "Diffuse interpolation lost perspective correction");
  for (auto &vertex : triangle)
    vertex.diffuse = 0xffffffff;
  renderer->clear(true, 0, 0, 0);
  platform::DrawState fogged{};
  fogged.fog = true;
  renderer->set_fog_color(0, 0, 0);
  renderer->draw(platform::Primitive::kFan, triangle, 3, fogged);
  require(pixel(8, 8) == 0xffc7c7c7,
          "Fog interpolation lost perspective correction");

  const auto ready =
      "cc-adapter-ready-" + std::to_string(SDL_GetPerformanceCounter());
  require(!std::filesystem::exists(ready), "Ready marker path already exists");
  sdlfe::set_ready_file(ready);
  renderer->present();
  sdlfe::set_ready_file("");
  require(!std::filesystem::remove(ready),
          "A hidden frame signaled first presentation");
  require(renderer->presentation_count() == 0,
          "A hidden frame counted as a presentation");

  if (backend == sdlfe::RendererBackend::core_critical_wsi) {
    require(SDL_ShowWindow(window) && SDL_SyncWindow(window), SDL_GetError());
    require(SDL_MinimizeWindow(window) && SDL_SyncWindow(window),
            SDL_GetError());
    SDL_PumpEvents();
    require((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0,
            "The test window did not minimize");
    sdlfe::set_ready_file(ready);
    renderer->present();
    require(!std::filesystem::exists(ready),
            "A minimized frame signaled first presentation");
    require(renderer->presentation_count() == 0,
            "A minimized frame counted as a presentation");
    require(SDL_RestoreWindow(window) && SDL_SyncWindow(window),
            SDL_GetError());
    for (int i = 0; i < 30 && !std::filesystem::exists(ready); ++i) {
      SDL_PumpEvents();
      renderer->present();
      SDL_Delay(10);
    }
    sdlfe::set_ready_file("");
    require(std::filesystem::remove(ready),
            "A restored Vulkan window did not present");
    require(renderer->presentation_count() > 0,
            "A restored frame did not count as a presentation");
    for (int size : {64, 48, 32}) {
      require(SDL_SetWindowSize(window, size, size) && SDL_SyncWindow(window),
              SDL_GetError());
      SDL_PumpEvents();
      const auto before = renderer->presentation_count();
      // Allow transient OUT_OF_DATE results while the window changes size.
      for (int i = 0; i < 30 && renderer->presentation_count() == before; ++i) {
        renderer->present();
        SDL_PumpEvents();
        SDL_Delay(10);
      }
      require(renderer->presentation_count() > before,
              "A resized Vulkan window did not complete a presentation");
    }
    require(pixel(8, 8) == 0xffc7c7c7,
            "Swapchain resizing changed the resident scene");
  }
}

} // namespace

namespace {
void check_quality(SDL_Window *window, sdlfe::RendererBackend backend,
                   const sdlfe::CoreCriticalQuality &quality) {
  require(SDL_SetWindowSize(window, 64, 64) && SDL_SyncWindow(window),
          SDL_GetError());
  for (const auto invalid : {sdlfe::CoreCriticalQuality{"smaa", "1"},
                             sdlfe::CoreCriticalQuality{"fsr4", "1"},
                             sdlfe::CoreCriticalQuality{"off", "0"},
                             sdlfe::CoreCriticalQuality{"off", "3"},
                             sdlfe::CoreCriticalQuality{"off", "16junk"}}) {
    bool rejected = false;
    try {
      auto unsupported = make_check_renderer(window, backend, invalid);
    } catch (const std::exception &error) {
      rejected = std::string_view(error.what()).find("--cc-") == 0;
    }
    require(rejected, "An unsupported quality request was silently accepted");
  }
  auto original = make_check_renderer(window, backend);
  original->set_viewport(0, 0, 64, 64);
  original->clear(true, 0, 0, 0);
  std::vector<uint32_t> original_pixels;
  int original_width = 0, original_height = 0;
  require(
      original->read_back(&original_pixels, &original_width, &original_height),
      "The original-quality renderer did not initialize");
  auto renderer = make_check_renderer(window, backend, quality);
  renderer->set_viewport(0, 0, 64, 64);
  renderer->clear(true, 0, 0, 0);
  std::vector<uint8_t> checker(64 * 64 * 4, 255);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
      for (int channel = 0; channel < 3; ++channel)
        checker[(y * 64 + x) * 4 + channel] = ((x + y) & 1) ? 255 : 0;
  platform::DrawState filtered{};
  filtered.texture = renderer->create_texture(64, 64, checker.data(), 1);
  const platform::TLVertex tiled[] = {{0, 0, .5f, 1, 0xffffffff, 0, 0, 0},
                                      {64, 0, .5f, 1, 0xffffffff, 0, 16, 0},
                                      {64, 64, .5f, 1, 0xffffffff, 0, 16, 16},
                                      {0, 64, .5f, 1, 0xffffffff, 0, 0, 16}};
  renderer->draw(platform::Primitive::kFan, tiled, 4, filtered);
  renderer->release_texture(filtered.texture);
  std::vector<uint32_t> pixels;
  int width = 0, height = 0;
  require(renderer->read_back(&pixels, &width, &height) && width == 64 &&
              height == 64,
          "Quality readback changed game-facing output dimensions");
  if (quality.anisotropy != "1") {
    const auto value = pixels[32 * 64 + 32] & 255U;
    require(value >= 120 && value <= 135,
            "AF failed to filter the minified checker texture");
    const auto stats = renderer->quality_statistics();
    require(
        stats.anisotropic_samplers > 0 && stats.mip_levels >= 6,
        "World filtering did not create anisotropic samplers and mip levels");
  }

  renderer->clear(true, 0, 0, 0);
  const platform::TLVertex diagonal[] = {{33, 1, .5f, 1, 0xffffffff, 0, 0, 0},
                                         {63, 56, .5f, 1, 0xffffffff, 0, 0, 0},
                                         {33, 63, .5f, 1, 0xffffffff, 0, 0, 0}};
  renderer->draw(platform::Primitive::kFan, diagonal, 3, {});
  renderer->end_scene();
  // A rasterized opaque glyph over the left half must stay byte-exact after AA.
  std::vector<uint8_t> glyph(64 * 64 * 4);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 32; ++x) {
      glyph[(y * 64 + x) * 4 + 1] = x + y / 2 < 28 ? 255 : 0;
      glyph[(y * 64 + x) * 4 + 3] = 255;
    }
  platform::DrawState hud{};
  hud.texture = renderer->create_texture(64, 64, glyph.data(), 4);
  hud.sharp = hud.blend = hud.depth_always = true;
  hud.depth_write = false;
  hud.address_u = hud.address_v = 3;
  quad(*renderer, 0, 64, 0, 0xffffffff, hud);
  renderer->release_texture(hud.texture);
  require(renderer->read_back(&pixels, &width, &height),
          "Quality frame did not read back");
  unsigned intermediate = 0;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      const auto pixel = pixels[y * 64 + x];
      if (x < 32)
        require(pixel == (x + y / 2 < 28 ? 0xff00ff00U : 0xff000000U),
                "World antialiasing changed sharp HUD pixels");
      else if ((pixel & 255U) > 0 && (pixel & 255U) < 255)
        ++intermediate;
    }
  if (quality.antialiasing != "off")
    require(intermediate > 0,
            "World antialiasing did not smooth the authored diagonal");
  if (quality.antialiasing == "fxaa")
    require(renderer->quality_statistics().fxaa_passes >= 2,
            "World FXAA pass did not execute");
  original->clear(true, 1, 2, 3);
  require(
      original->read_back(&original_pixels, &original_width,
                          &original_height) &&
          original_width == 64 && original_height == 64 &&
          original_pixels[0] == 0xff010203,
      "Another renderer's quality changed the original renderer's readback");
  require(original->quality_statistics().fxaa_passes == 0 &&
              original->quality_statistics().anisotropic_samplers == 0,
          "Quality settings leaked between renderer instances");
  original.reset();
  require(renderer->hold_frame(), "Could not hold an antialiased frame");
  renderer->clear(true, 0, 0, 0);
  require(renderer->restore_frame(), "Could not restore an antialiased frame");
  std::vector<uint32_t> restored;
  require(renderer->read_back(&restored, &width, &height) && pixels == restored,
          "Held quality frame did not preserve its resolved pixels");
  const auto hud_passes = renderer->quality_statistics().fxaa_passes;
  renderer->clear_depth_rect(0, 0, 16, 16);
  renderer->end_scene();
  require(
      renderer->read_back(&restored, &width, &height) && pixels == restored &&
          renderer->quality_statistics().fxaa_passes == hud_passes,
      "A depth-only batch after HUD composition dirtied or filtered its color");
  for (const uint8_t alpha : {uint8_t(128), uint8_t(0)}) {
    const uint8_t translucent[] = {64, 32, 16, alpha};
    platform::DrawState straight{};
    straight.texture = renderer->create_texture(1, 1, translucent, 4);
    renderer->clear(true, 0, 0, 0);
    quad(*renderer, 0, 64, .5f, 0xffffffff, straight);
    renderer->release_texture(straight.texture);
    require(renderer->read_back(&restored, &width, &height) &&
                restored[32 * 64 + 32] == (uint32_t(alpha) << 24 | 0x00402010U),
            "The straight-alpha held-frame input was not preserved");
    require(renderer->hold_frame(), "Could not hold straight-alpha pixels");
    renderer->clear(true, 0, 0, 0);
    require(renderer->restore_frame(),
            "Could not restore straight-alpha pixels");
    std::vector<uint32_t> copied;
    require(renderer->read_back(&copied, &width, &height) && copied == restored,
            "Held-frame restore changed straight-alpha or alpha-zero RGB");
  }

  auto depth_clear_case = [&](bool clear_depth) {
    const auto before = renderer->quality_statistics().fxaa_passes;
    renderer->clear(true, 0, 0, 0);
    renderer->draw(platform::Primitive::kFan, diagonal, 3, {});
    renderer->end_scene();
    if (clear_depth)
      renderer->clear_depth_rect(0, 0, 16, 16);
    quad(*renderer, 0, 4, .5f, 0xff000000);
    std::vector<uint32_t> result;
    require(renderer->read_back(&result, &width, &height),
            "Depth-clear frame did not read back");
    if (quality.antialiasing == "fxaa")
      require(renderer->quality_statistics().fxaa_passes - before == 1,
              "A depth-only clear triggered extra world FXAA");
    return result;
  };
  const auto without_depth_clear = depth_clear_case(false);
  require(depth_clear_case(true) == without_depth_clear,
          "A depth-only rectangle clear changed unaffected color pixels");
  const auto previous_stats = renderer->quality_statistics();
  renderer->reset_scene_resources();
  renderer->clear(true, 10, 20, 30);
  require(renderer->read_back(&restored, &width, &height) &&
              restored[0] == 0xff0a141e,
          "A quality scene reload retained old pixels");
  require(renderer->quality_statistics().fxaa_passes >=
              previous_stats.fxaa_passes,
          "Quality counters were lost during scene reload");
  require(SDL_ShowWindow(window) && SDL_SyncWindow(window), SDL_GetError());
  for (const auto size : {std::array{64, 64}, std::array{48, 72}}) {
    require(SDL_SetWindowSize(window, size[0], size[1]) &&
                SDL_SyncWindow(window),
            SDL_GetError());
    SDL_PumpEvents();
    renderer->clear(true, 10, 20, 30);
    const auto before = renderer->presentation_count();
    for (int i = 0; i < 30 && renderer->presentation_count() == before; ++i) {
      renderer->present();
      SDL_PumpEvents();
      SDL_Delay(10);
    }
    require(renderer->presentation_count() > before,
            "Quality presentation did not survive resize");
    require(renderer->read_back(&restored, &width, &height) &&
                width == size[0] && height == size[1],
            "Quality resize changed game-facing readback dimensions");
  }
}
} // namespace

namespace {
std::string surface_metadata(const char *asset, const char *material,
                             uint64_t mesh_hash, float roughness = .7f,
                             float reflectivity = .04f, float metallic = 0.f,
                             bool bound = true) {
  prepared_fixture::Bytes metadata;
  metadata.value.assign("BWSFM1", 6);
  metadata.value.append(2, char(0));
  metadata.u32(1);
  metadata.u32(1);
  metadata.u32(1);
  metadata.u32(0);
  metadata.u64(mesh_hash);
  metadata.u32(bound ? 0 : mw34::materials::no_surface_material);
  auto text = [&](const char *value) {
    metadata.u32(uint32_t(std::strlen(value)));
    metadata.value += value;
  };
  text(asset);
  text(material);
  metadata.u32(0xffffffffU);
  metadata.u32(0xffffffffU);
  metadata.f32(0);
  metadata.f32(0);
  metadata.f32(0);
  metadata.u32(1);
  metadata.f32(1);
  metadata.f32(1);
  metadata.f32(0);
  metadata.f32(0);
  metadata.u32(1);
  metadata.u32(1);
  metadata.f32(0);
  metadata.f32(0);
  metadata.f32(1);
  metadata.f32(1);
  metadata.f32(0);
  metadata.f32(1);
  metadata.f32(1);
  metadata.f32(1);
  metadata.f32(0);
  metadata.f32(0);
  metadata.f32(roughness);
  metadata.f32(reflectivity);
  metadata.f32(metallic);
  return metadata.value;
}

void check_terrain_surface_normals(SDL_Window *window,
                                   sdlfe::RendererBackend backend) {
  const auto folder = std::filesystem::temp_directory_path() /
                      ("cc-terrain-normal-check-" +
                       std::to_string(SDL_GetPerformanceCounter()));
  require(std::filesystem::create_directory(folder),
          "Terrain normal fixture directory already exists");
  struct Files {
    std::filesystem::path folder;
    ~Files() {
      std::error_code error;
      std::filesystem::remove_all(folder, error);
#if defined(_WIN32)
      _putenv_s("MW34_WORLD_NORMALS", "");
#else
      unsetenv("MW34_WORLD_NORMALS");
#endif
    }
  } files{folder};

  // Model fixture so PreparedModels admits per-pixel lighting
  std::ofstream(folder / "mesh.bin", std::ios::binary)
      << prepared_fixture::mesh(2, 0xffff0000);
  std::ofstream(folder / "textures.bin", std::ios::binary)
      << prepared_fixture::textures();
  std::ofstream(folder / "normals.bin", std::ios::binary)
      << prepared_fixture::normals(0xffd980d9);

  // Primary t1m1 ground terrain tile SurfaceKey: ggter1, 128x128, format 1
  // (RGB565), hash 0x6335d0d1f5c866
  const mw34::materials::SurfaceKey t1m1_terrain_key{128, 128, 1,
                                                     0x6335d0d1f5c866ULL};
  const auto page_path = folder / "page-0.bin";
  const auto world_path = folder / "world.txt";

  // Build valid BWSNR2 page containing t1m1_terrain_key and non-neutral normal
  // (0xffd980d9)
  {
    prepared_fixture::Bytes out;
    out.value.assign("BWSNR2\0\0", 8);
    out.u32(1); // 1 key
    out.u32(t1m1_terrain_key.width);
    out.u32(t1m1_terrain_key.height);
    out.u32(t1m1_terrain_key.format);
    out.u32(uint32_t(t1m1_terrain_key.hash & 0xffffffff));
    out.u32(uint32_t(t1m1_terrain_key.hash >> 32));
    out.name("terrain0");

    out.value.append("BWNRM2\0\0", 8);
    out.u32(1); // batches
    out.u32(1); // images count
    out.u32(0); // binding: batch 0 -> image 0
    out.name("terrain0");
    out.u32(1); // width 1
    out.u32(1); // height 1
    uint64_t hash = 0xcbf29ce484222325ULL;
    const uint32_t normal_pixel = 0xffd980d9;
    for (unsigned shift = 0; shift < 32; shift += 8) {
      hash ^= uint8_t(normal_pixel >> shift);
      hash *= 0x100000001b3ULL;
    }
    out.u64(hash);
    out.u32(normal_pixel);

    std::ofstream pf(page_path, std::ios::binary);
    pf.write(out.value.data(), out.value.size());
  }

  // Build manifest
  {
    std::ofstream wf(world_path);
    wf << "MW34SNP1\npage-0.bin\n";
  }

#if defined(_WIN32)
  _putenv_s("MW34_WORLD_NORMALS", world_path.string().c_str());
#else
  setenv("MW34_WORLD_NORMALS", world_path.string().c_str(), 1);
#endif

  sdlfe::CoreCriticalAssets options;
  options.textures = (folder / "textures.bin").u8string();
  options.models.push_back(
      {"authored", "0", (folder / "mesh.bin").u8string(), options.textures});
  options.normals.push_back(
      {"authored", "0", (folder / "normals.bin").u8string()});
  options.lighting = "normal";
  // The screen-aligned fixture faces -Z. Light that face so perturbations
  // affect direct illumination rather than comparing two ambient-only images.
  options.sun_azimuth = "-90";

  auto renderer = make_check_renderer(window, backend, {}, options);
  require(renderer->asset_statistics().world_normal_decoded_bytes == 0,
          "World pages decoded before first material lookup");
#if defined(_WIN32)
  _putenv_s("MW34_WORLD_NORMALS", (folder / "missing.txt").string().c_str());
#else
  setenv("MW34_WORLD_NORMALS", (folder / "missing.txt").string().c_str(), 1);
#endif
  renderer->set_viewport(0, 0, 64, 64);
  renderer->clear(true, 0, 0, 0);

  // Producer active surface key channel
  mw34::materials::set_active_surface_key(t1m1_terrain_key);
  std::vector<uint8_t> green_data(128 * 128 * 4);
  for (size_t i = 0; i < 128 * 128; ++i) {
    green_data[i * 4 + 0] = 34;
    green_data[i * 4 + 1] = 139;
    green_data[i * 4 + 2] = 34;
    green_data[i * 4 + 3] = 255;
  }
  const auto tex =
      renderer->create_named_texture("ggter1", 128, 128, green_data.data(), 1);
  mw34::materials::clear_active_surface_key();

  platform::DrawState state{};
  state.texture = tex;
  quad(*renderer, 0, 64, .5f, 0xffffffff, state);
  std::vector<uint32_t> pixels;
  int width = 0, height = 0;
  require(renderer->read_back(&pixels, &width, &height),
          "Could not read back terrain normal frame");

  const auto normal_pixels = pixels;
  auto stats = renderer->asset_statistics();
  require(stats.normal_draws > 0, "Terrain surface normal binding test failed "
                                  "with 0 matched normal bindings");
  require(stats.world_normal_decoded_bytes == 4 &&
              stats.world_normal_peak_decoded_bytes == 4 &&
              stats.world_normal_loaded_pages == 1,
          "World page decoded-byte/loaded-page accounting is incorrect");

  // Test3: reject stale bindings on same-handle texture updates
  const auto base_draws = stats.normal_draws;
  require(base_draws > 0, "base normal_draws > 0");
  const auto saved_green = green_data;
  green_data[0] ^= 0xFF;
  green_data[1] ^= 0xFF;
  green_data[2] ^= 0xFF; // edited RGBA
  auto bad_key = t1m1_terrain_key;
  bad_key.hash ^= 1;
  mw34::materials::set_active_surface_key(bad_key);
  renderer->update_texture(tex, 128, 128, green_data.data());
  mw34::materials::clear_active_surface_key();
  renderer->clear(true, 0, 0, 0);
  quad(*renderer, 0, 64, .5f, 0xffffffff, state);
  require(renderer->read_back(&pixels, &width, &height),
          "Could not read back updated surface");
  require(renderer->asset_statistics().normal_draws == base_draws,
          "altered-key same-handle update rejected");
  mw34::materials::clear_active_surface_key();
  renderer->update_texture(tex, 128, 128, green_data.data());
  mw34::materials::clear_active_surface_key();
  renderer->clear(true, 0, 0, 0);
  quad(*renderer, 0, 64, .5f, 0xffffffff, state);
  require(renderer->read_back(&pixels, &width, &height),
          "Could not read back updated surface");
  require(renderer->asset_statistics().normal_draws == base_draws,
          "unauthenticated update rejected");
  green_data = saved_green; // restore original RGBA
  mw34::materials::set_active_surface_key(t1m1_terrain_key);
  renderer->update_texture(tex, 128, 128, green_data.data());
  mw34::materials::clear_active_surface_key();
  renderer->clear(true, 0, 0, 0);
  quad(*renderer, 0, 64, .5f, 0xffffffff, state);
  require(renderer->read_back(&pixels, &width, &height),
          "Could not read back updated surface");
  require(renderer->asset_statistics().normal_draws > base_draws,
          "authenticated update accepted");
  require(pixels == normal_pixels, "pixels equal normal_pixels");

  stats = renderer->asset_statistics();

  // Verify reset_scene_resources invalidates epoch and stale texture slot
  // bindings
  renderer->reset_scene_resources();
  require(renderer->asset_statistics().world_normal_decoded_bytes == 0 &&
              renderer->asset_statistics().world_normal_loaded_pages == 0,
          "Scene reset retained decoded world pages");
  renderer->clear(true, 0, 0, 0);
  quad(*renderer, 0, 64, .5f, 0xffffffff, state);
  require(renderer->read_back(&pixels, &width, &height),
          "Could not read back after reset_scene_resources");
  require(renderer->asset_statistics().normal_draws == stats.normal_draws,
          "Scene reset reused a stale surface normal binding");
  require(renderer->asset_statistics().world_normal_decoded_bytes == 0,
          "Stale texture caused a world-page decode after reset");

  // Recreated native textures must acquire the new epoch and reload the
  // catalog.
  mw34::materials::set_active_surface_key(t1m1_terrain_key);
  state.texture =
      renderer->create_named_texture("ggter1", 128, 128, green_data.data(), 1);
  mw34::materials::clear_active_surface_key();
  quad(*renderer, 0, 64, .5f, 0xffffffff, state);
  require(renderer->read_back(&pixels, &width, &height) &&
              renderer->asset_statistics().normal_draws > stats.normal_draws &&
              renderer->asset_statistics().world_normal_decoded_bytes == 4,
          "New-epoch texture did not reload its world normal page");

  {
    bool rejected = false;
    try {
      auto missing_renderer = make_check_renderer(window, backend, {}, options);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "Configured missing world catalog was not rejected "
                      "before mission callbacks");
#if defined(_WIN32)
    _putenv_s("MW34_WORLD_NORMALS", world_path.string().c_str());
#else
    setenv("MW34_WORLD_NORMALS", world_path.string().c_str(), 1);
#endif
    auto repaired_renderer = make_check_renderer(window, backend, {}, options);
    require(repaired_renderer->asset_statistics().world_normal_decoded_bytes ==
                0,
            "Catalog admission decoded pixels before a material lookup");
  }

  std::vector<uint32_t> flat_pixels;
  // Flat lighting control: normal_draws == 0
  {
    auto flat_options = options;
    flat_options.lighting = "flat";
    auto flat_renderer = make_check_renderer(window, backend, {}, flat_options);
    flat_renderer->set_viewport(0, 0, 64, 64);
    flat_renderer->clear(true, 0, 0, 0);
    mw34::materials::set_active_surface_key(t1m1_terrain_key);
    const auto flat_tex = flat_renderer->create_named_texture(
        "ggter1", 128, 128, green_data.data(), 1);
    mw34::materials::clear_active_surface_key();
    platform::DrawState flat_state{};
    flat_state.texture = flat_tex;
    quad(*flat_renderer, 0, 64, .5f, 0xffffffff, flat_state);
    require(flat_renderer->read_back(&pixels, &width, &height),
            "Could not read back flat terrain frame");
    flat_pixels = pixels;
    require(pixels != normal_pixels,
            "Visible terrain normal fixture did not differ from flat lighting");
    require(flat_renderer->asset_statistics().normal_draws == 0,
            "Flat lighting produced non-zero normal draws");
    require(flat_renderer->asset_statistics().world_normal_decoded_bytes == 0,
            "Flat lighting decoded a world normal page");
  }

  // Off lighting control: normal_draws == 0
  {
    sdlfe::CoreCriticalAssets off_options;
    off_options.lighting = "off";
    auto off_renderer = make_check_renderer(window, backend, {}, off_options);
    off_renderer->set_viewport(0, 0, 64, 64);
    off_renderer->clear(true, 0, 0, 0);
    mw34::materials::set_active_surface_key(t1m1_terrain_key);
    const auto off_tex = off_renderer->create_named_texture(
        "ggter1", 128, 128, green_data.data(), 1);
    mw34::materials::clear_active_surface_key();
    platform::DrawState off_state{};
    off_state.texture = off_tex;
    quad(*off_renderer, 0, 64, .5f, 0xffffffff, off_state);
    require(off_renderer->read_back(&pixels, &width, &height),
            "Could not read back off terrain frame");
    require(off_renderer->asset_statistics().normal_draws == 0,
            "Off lighting produced non-zero normal draws");
    require(off_renderer->asset_statistics().world_normal_decoded_bytes == 0,
            "Off lighting decoded a world normal page");
  }
  // Optional content absent: normal mode is neutral, not a synthetic pack.
#if defined(_WIN32)
  _putenv_s("MW34_WORLD_NORMALS", "");
#else
  unsetenv("MW34_WORLD_NORMALS");
#endif
  {
    auto neutral_renderer = make_check_renderer(window, backend, {}, options);
    neutral_renderer->set_viewport(0, 0, 64, 64);
    neutral_renderer->clear(true, 0, 0, 0);
    mw34::materials::set_active_surface_key(t1m1_terrain_key);
    platform::DrawState neutral_state{};
    neutral_state.texture = neutral_renderer->create_named_texture(
        "ggter1", 128, 128, green_data.data(), 1);
    mw34::materials::clear_active_surface_key();
    quad(*neutral_renderer, 0, 64, .5f, 0xffffffff, neutral_state);
    require(
        neutral_renderer->read_back(&pixels, &width, &height) &&
            pixels == flat_pixels &&
            neutral_renderer->asset_statistics().normal_draws == 0 &&
            neutral_renderer->asset_statistics().world_normal_decoded_bytes ==
                0,
        "Missing optional world pack did not remain neutral");
  }
  // A real source model/material scope, authenticated by an authored BWSFM1
  // expectation rather than a first-observed UV cache entry.
  {
    struct NativeEnvironment {
      ~NativeEnvironment() {
#if defined(_WIN32)
        _putenv_s("MW34_NATIVE_MATERIALS", "");
        _putenv_s("MW34_NATIVE_MATERIAL_ID", "");
#else
        unsetenv("MW34_NATIVE_MATERIALS");
        unsetenv("MW34_NATIVE_MATERIAL_ID");
#endif
      }
    } native_environment;
    float positions[]{0, 0, 2, 2, 0, 2, 0, 2, 2};
    float uvs[]{0, 0, 1, 0, 0, 1};
    uint32_t indices[]{0, 1, 2};
    zmod::Material material{};
    material.flags = 1;
    zimg::TextureEntry source_texture{};
    std::strcpy(source_texture.name, "ggter1");
    zvid::D3DTexture device_texture{};
    source_texture.surface = &device_texture;
    material.texture = &source_texture;
    zmod::Polygon polygon{};
    polygon.flags = 3;
    polygon.vertex_indices = indices;
    polygon.uvs = uvs;
    polygon.material = &material;
    zmod::Model source_model{};
    const uint32_t one = 1, three = 3;
    std::memcpy(source_model.header + 0x10, &one, 4);
    std::memcpy(source_model.header + 0x14, &three, 4);
    source_model.polygons = &polygon;
    source_model.vertices = positions;
    const auto native_hash = cc_mw3_re::native_mesh_uv_hash(source_model);
    require(native_hash.has_value(),
            "Authored native source hash was unavailable");
    struct Batch {
      std::vector<std::array<float, 8>> vertices;
    };
    std::vector<Batch> canonical(1);
    for (unsigned corner = 0; corner < 3; ++corner)
      canonical[0].vertices.push_back({positions[corner * 3],
                                       positions[corner * 3 + 1],
                                       positions[corner * 3 + 2], 0, 0, 0,
                                       uvs[corner * 2], uvs[corner * 2 + 1]});
    require(*native_hash ==
                mw34::materials::compute_mesh_uv_hash(canonical, 0, 1),
            "Native source hash does not match the canonical BWSFM1 stream");
    uvs[2] = .75f;
    require(
        cc_mw3_re::native_mesh_uv_hash(source_model) != native_hash,
        "Changed live UV coordinates did not change native source identity");
    uvs[2] = 1;
    indices[0] = 3;
    require(!cc_mw3_re::native_mesh_uv_hash(source_model),
            "Native source hash accepted an out-of-range vertex");
    indices[0] = 0;

    const auto metadata =
        surface_metadata("native:fixture", "ggter1", *native_hash);
    const auto metadata_path = folder / "native-material.bin";
    std::ofstream(metadata_path, std::ios::binary)
        .write(metadata.data(), metadata.size());
#if defined(_WIN32)
    _putenv_s("MW34_WORLD_NORMALS", world_path.string().c_str());
    _putenv_s("MW34_NATIVE_MATERIALS", metadata_path.string().c_str());
    _putenv_s("MW34_NATIVE_MATERIAL_ID", "native:fixture");
#else
    setenv("MW34_WORLD_NORMALS", world_path.string().c_str(), 1);
    setenv("MW34_NATIVE_MATERIALS", metadata_path.string().c_str(), 1);
    setenv("MW34_NATIVE_MATERIAL_ID", "native:fixture", 1);
#endif
    auto checked_renderer = make_check_renderer(window, backend, {}, options);
    checked_renderer->set_viewport(0, 0, 64, 64);
    mw34::materials::set_active_surface_key(t1m1_terrain_key);
    device_texture.handle = checked_renderer->create_named_texture(
        "ggter1", 128, 128, green_data.data(), 1);
    mw34::materials::clear_active_surface_key();
    zcls::Node node{};
    std::strcpy(reinterpret_cast<char *>(node.raw), "nativefixture");
    node.model = &source_model;
    platform::ModelDrawContext model_context;
    model_context.node = &node;
    model_context.ordinary = true;
    platform::MaterialDrawContext material_context;
    material_context.model = &source_model;
    material_context.material = &material;
    material_context.texture_entry = &source_texture;
    auto draw_native = [&] {
      checked_renderer->clear(true, 0, 0, 0);
      checked_renderer->begin_model_draw(model_context);
      checked_renderer->begin_material_draw(material_context);
      platform::DrawState native_state;
      native_state.texture = device_texture.handle;
      const platform::TLVertex triangle[] = {
          {0, 0, .5f, 1, 0xffffffff, 0, 0, 0},
          {64, 0, .5f, 1, 0xffffffff, 0, 1, 0},
          {0, 64, .5f, 1, 0xffffffff, 0, 0, 1}};
      checked_renderer->draw(platform::Primitive::kFan, triangle, 3,
                             native_state);
      checked_renderer->end_material_draw();
      checked_renderer->end_model_draw();
      require(checked_renderer->read_back(&pixels, &width, &height),
              "Could not read native UV fixture");
    };
    draw_native();
    const auto qualified = checked_renderer->asset_statistics();
    require(qualified.normal_draws > 0 &&
                qualified.binding_rejected_mismatch == 0,
            "Correct authored native UV binding was rejected");
    uvs[2] = .75f;
    draw_native();
    require(checked_renderer->asset_statistics().normal_draws ==
                    qualified.normal_draws &&
                checked_renderer->asset_statistics().binding_rejected_mismatch >
                    0,
            "Wrong native mesh_uv_hash was accepted");
    uvs[2] = 1;
    material_context.texture_entry = nullptr;
    draw_native();
    require(checked_renderer->asset_statistics().normal_draws ==
                qualified.normal_draws,
            "Mismatched texture-entry identity was accepted");
    material_context.texture_entry = &source_texture;
    draw_native();
    require(checked_renderer->asset_statistics().normal_draws >
                qualified.normal_draws,
            "Restored authored native UV binding was not usable");
    struct DeferredBackendScope {
      platform::Render3D *renderer = zvid::render3d();
      zvid::DrawBuffers buffers = zvid::draw_buffers();
      ~DeferredBackendScope() {
        zvid::set_render3d(renderer);
        zvid::draw_buffers() = buffers;
      }
    } deferred_backend;
    zvid::set_render3d(checked_renderer.get());
    platform::TLVertex deferred_vertices[] = {
        {0, 0, .5f, 1, 0xffffffff, 0, 0, 0},
        {64, 0, .5f, 1, 0xffffffff, 0, 1, 0},
        {0, 64, .5f, 1, 0xffffffff, 0, 0, 1}};
    zvid::draw_buffers().tl =
        reinterpret_cast<zvid::TLVertex *>(deferred_vertices);
    zvid::device_state().primitive = 6;
    require(zvid::transparent_queue().count == 0 &&
                zvid::overlay_list().count == 0,
            "Native deferred fixtures require empty queues");
    checked_renderer->clear(true, 0, 0, 0);
    const auto before_deferred = checked_renderer->asset_statistics();
    checked_renderer->begin_model_draw(model_context);
    checked_renderer->begin_material_draw(material_context);
    for (int i = 0; i < 4; ++i)
      zvid::draw_textured_white(3, &device_texture, 0, .5f, i & 1);
    const auto admission = checked_renderer->asset_statistics();
    require(admission.native_fingerprint_evaluations ==
                before_deferred.native_fingerprint_evaluations + 1,
            "Native model fingerprint was recomputed per polygon");
    auto receipt = checked_renderer->capture_deferred_material_binding(
        device_texture.handle);
    checked_renderer->end_material_draw();
    checked_renderer->end_model_draw();
    zvid::flush_transparent();
    require(checked_renderer->read_back(&pixels, &width, &height),
            "Transparent readback failed");
    const auto after_transparent = checked_renderer->asset_statistics();
    require(after_transparent.normal_draws > before_deferred.normal_draws &&
                after_transparent.binding_rejected_mismatch ==
                    before_deferred.binding_rejected_mismatch,
            "Transparent queue lost qualified material admission");
    zvid::flush_overlay();
    require(checked_renderer->read_back(&pixels, &width, &height),
            "Overlay readback failed");
    const auto after_deferred = checked_renderer->asset_statistics();
    require(after_deferred.normal_draws > after_transparent.normal_draws &&
                after_deferred.binding_rejected_mismatch ==
                    before_deferred.binding_rejected_mismatch,
            "Overlay queue lost qualified material admission");
    // Same handle after an upload cannot reuse a previously admitted receipt.
    mw34::materials::set_active_surface_key(t1m1_terrain_key);
    checked_renderer->update_texture(device_texture.handle, 128, 128,
                                     green_data.data());
    mw34::materials::clear_active_surface_key();
    platform::DrawState receipt_state;
    receipt_state.texture = device_texture.handle;
    checked_renderer->begin_deferred_material_draw(receipt);
    checked_renderer->draw(platform::Primitive::kFan, deferred_vertices, 3,
                           receipt_state);
    checked_renderer->end_deferred_material_draw();
    require(checked_renderer->read_back(&pixels, &width, &height),
            "Stale admission readback failed");
    require(checked_renderer->asset_statistics().normal_draws ==
                    after_deferred.normal_draws &&
                checked_renderer->asset_statistics().binding_rejected_mismatch >
                    after_deferred.binding_rejected_mismatch,
            "Deferred admission survived a same-handle upload");
    // A source mutation inside a model scope explicitly invalidates its hash.
    checked_renderer->begin_model_draw(model_context);
    checked_renderer->begin_material_draw(material_context);
    checked_renderer->capture_deferred_material_binding(device_texture.handle);
    const auto before_mutation = checked_renderer->asset_statistics();
    uvs[2] = .75f;
    checked_renderer->invalidate_material_geometry();
    checked_renderer->capture_deferred_material_binding(device_texture.handle);
    require(
        checked_renderer->asset_statistics().native_fingerprint_evaluations ==
                before_mutation.native_fingerprint_evaluations + 1 &&
            checked_renderer->asset_statistics().binding_rejected_mismatch >
                before_mutation.binding_rejected_mismatch,
        "In-scope source UV mutation did not invalidate authentication");
    uvs[2] = 1;
    checked_renderer->end_material_draw();
    checked_renderer->end_model_draw();
    // The first admission after reset must reload catalogs before its lookup.
    checked_renderer->reset_scene_resources();
    mw34::materials::set_active_surface_key(t1m1_terrain_key);
    checked_renderer->update_texture(device_texture.handle, 128, 128,
                                     green_data.data());
    mw34::materials::clear_active_surface_key();
    checked_renderer->begin_model_draw(model_context);
    checked_renderer->begin_material_draw(material_context);
    auto reloaded_receipt = checked_renderer->capture_deferred_material_binding(
        device_texture.handle);
    require(bool(reloaded_receipt),
            "First deferred material after reset was not admitted");
    checked_renderer->end_material_draw();
    checked_renderer->end_model_draw();
    const auto before_reloaded = checked_renderer->asset_statistics();
    checked_renderer->begin_deferred_material_draw(reloaded_receipt);
    checked_renderer->draw(platform::Primitive::kFan, deferred_vertices, 3,
                           receipt_state);
    checked_renderer->end_deferred_material_draw();
    require(checked_renderer->read_back(&pixels, &width, &height),
            "Reloaded admission readback failed");
    require(
        checked_renderer->asset_statistics().normal_draws >
                before_reloaded.normal_draws &&
            checked_renderer->asset_statistics().binding_rejected_mismatch ==
                before_reloaded.binding_rejected_mismatch,
        "First deferred material after reset lost normal binding");
    const auto before_old_epoch = checked_renderer->asset_statistics();
    checked_renderer->begin_deferred_material_draw(receipt);
    checked_renderer->draw(platform::Primitive::kFan, deferred_vertices, 3,
                           receipt_state);
    checked_renderer->end_deferred_material_draw();
    require(checked_renderer->read_back(&pixels, &width, &height),
            "Old-epoch admission readback failed");
    require(checked_renderer->asset_statistics().normal_draws ==
                    before_old_epoch.normal_draws &&
                checked_renderer->asset_statistics().binding_rejected_mismatch >
                    before_old_epoch.binding_rejected_mismatch,
            "Deferred admission survived scene resource reset");
#include "native_material_only_checks.inc"
  }
}

void check_sealed_environment(SDL_Window *window,
                              sdlfe::RendererBackend backend) {
  require(SDL_SetWindowSize(window, 32, 32) && SDL_SyncWindow(window),
          SDL_GetError());
  const uint8_t green[4] = {0, 255, 0, 255};
  for (const char *mode : {"flat", "off"}) {
    sdlfe::CoreCriticalAssets assets;
    assets.lighting = mode;
    auto renderer = make_check_renderer(window, backend, {}, assets);
    renderer->set_viewport(0, 0, 32, 32);
    auto read = [&]() {
      std::vector<uint32_t> px;
      int w = 0, h = 0;
      require(renderer->read_back(&px, &w, &h),
              "Sealed environment readback failed");
      require(w == 32 && h == 32 && px.size() == 1024,
              "Sealed environment size mismatch");
      return px;
    };
    auto px_at = [&](const std::vector<uint32_t> &px, int x, int y) {
      return px[y * 32 + x];
    };
    const platform::MissionIdentity sealed_ids[] = {{1, 2}, {1, 4}, {2, 5}};
    for (auto id : sealed_ids) {
      renderer->mission_changed(id);
      renderer->clear(true, 37, 89, 143);
      renderer->begin_scene();
      renderer->end_scene();
      auto px = read();
      uint32_t expected = (mode[0] == 'f') ? 0xFF000000 : 0xFF25598F;
      require(px_at(px, 30, 2) == expected, "sealed clear");
      renderer->begin_scene();
      quad(*renderer, 0.0f, 32.0f, 0.0001f, 0xFFFF0000);
      auto green_handle = renderer->create_texture(1, 1, green, 4);
      platform::DrawState ds;
      ds.texture = green_handle;
      quad(*renderer, 8.0f, 24.0f, 0.00015f, 0xFFFFFFFF, ds);
      platform::DrawState hud;
      hud.sharp = true;
      hud.depth_write = false;
      hud.depth_always = true;
      quad(*renderer, 0.0f, 6.0f, 0.0f, 0xFF0000FF, hud);
      renderer->end_scene();
      px = read();
      uint32_t sky_expected = (mode[0] == 'f') ? 0xFF000000 : 0xFFFF0000;
      require(px_at(px, 30, 2) == sky_expected, "sealed sky");
      require(px_at(px, 16, 16) == 0xFF00FF00, "terrain");
      require(px_at(px, 2, 2) == 0xFF0000FF, "hud");
      renderer->clear(false, 255, 0, 0);
      renderer->begin_scene();
      renderer->end_scene();
      auto px2 = read();
      require(px_at(px2, 16, 16) == 0xFF00FF00, "depth clear preserve");
      renderer->release_texture(green_handle);
    }
    const platform::MissionIdentity outdoor_ids[] = {{5, 1}, {9, 1}, {}};
    for (auto id : outdoor_ids) {
      renderer->mission_changed(id);
      renderer->clear(true, 37, 89, 143);
      renderer->begin_scene();
      renderer->end_scene();
      auto px = read();
      require(px_at(px, 30, 2) == 0xFF25598F, "outdoor clear");
      renderer->begin_scene();
      quad(*renderer, 0.0f, 32.0f, 0.0001f, 0xFFFF0000);
      renderer->end_scene();
      auto px2 = read();
      require(px_at(px2, 30, 2) == 0xFFFF0000, "outdoor sky");
    }
    renderer->mission_changed({1, 2});
    renderer->reset_scene_resources();
    renderer->clear(true, 37, 89, 143);
    renderer->begin_scene();
    renderer->end_scene();
    auto px = read();
    require(px_at(px, 30, 2) == 0xFF25598F, "reset clear");
  }
}

void check_prepared(SDL_Window *window, sdlfe::RendererBackend backend,
                    const sdlfe::CoreCriticalQuality &quality,
                    bool materials = false) {
  const auto folder =
      std::filesystem::temp_directory_path() /
      ("cc-prepared-check-" + std::to_string(SDL_GetPerformanceCounter()));
  require(std::filesystem::create_directory(folder),
          "Authored fixture directory already exists");
  struct Files {
    std::filesystem::path folder;
    ~Files() {
      std::error_code error;
      std::filesystem::remove(folder / "mesh.bin", error);
      std::filesystem::remove(folder / "textures.bin", error);
      std::filesystem::remove(folder / "normals.bin", error);
      std::filesystem::remove(folder / "materials.bin", error);
      std::filesystem::remove(folder, error);
    }
  } files{folder};
  // Diagnostic red must not tint the bound green albedo a second time.
  std::ofstream(folder / "mesh.bin", std::ios::binary)
      << prepared_fixture::mesh(2, 0xffff0000);
  std::ofstream(folder / "textures.bin", std::ios::binary)
      << prepared_fixture::textures();
  sdlfe::CoreCriticalAssets options;
  options.textures = (folder / "textures.bin").u8string();
  options.models.push_back(
      {"authored", "0", (folder / "mesh.bin").u8string(), options.textures});
  if (materials) {
    std::ofstream(folder / "normals.bin", std::ios::binary)
        << prepared_fixture::normals(0xffd980d9);
    options.normals.push_back(
        {"authored", "0", (folder / "normals.bin").u8string()});
    options.lighting = "normal";
    std::istringstream mesh_input(prepared_fixture::mesh(2, 0xffff0000),
                                  std::ios::binary);
    const auto mesh = ::platform::prepared::read_mesh(mesh_input);
    const auto hash = mw34::materials::compute_mesh_uv_hash(
        mesh.batches, 0, mesh.batches.size());
    const auto metadata = surface_metadata("authored:close", "authored", hash,
                                           .7f, .04f, 0.f, false);
    const auto path = folder / "materials.bin";
    options.materials.push_back({"authored", "0", path.u8string()});
    auto wrong = metadata;
    wrong[24] ^= 1;
    std::ofstream(path, std::ios::binary).write(wrong.data(), wrong.size());
    bool rejected = false;
    try {
      auto invalid = make_check_renderer(window, backend, quality, options);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected,
            "Prepared renderer accepted mismatched authored mesh_uv_hash");
    std::ofstream(path, std::ios::binary)
        .write(metadata.data(), metadata.size());
  }
  require(SDL_SetWindowSize(window, 64, 64) && SDL_SyncWindow(window),
          SDL_GetError());
  std::unique_ptr<zcls::Node, int (*)(zcls::Node *)> root(
      zcls::object3d_create(), zcls::tree_destroy);
  auto *control = zcls::object3d_create(), *panel = zcls::object3d_create();
  require(root && control && panel,
          "Authored native nodes could not be created");
  require(zcls::object3d_add_child(root.get(), control) == 0 &&
              zcls::object3d_add_child(control, panel) == 0,
          "Authored native hierarchy could not be linked");
  zcls::node_set_name(root.get(), "actor");
  zcls::node_set_name(control, "control");
  zcls::node_set_name(panel, "panel");
  auto identity = std::make_shared<zcls::SourceAsset>();
  identity->name = "mech_authored.flt";
  identity->nodes = {{"actor", 1}, {"control", 1}, {"panel", 1}};
  root->source_asset = control->source_asset = panel->source_asset = identity;
  root->source_asset_root = true;
  zmod::Model source_model{};
  const uint32_t source_count = 3;
  std::memcpy(source_model.header + 0x14, &source_count, sizeof source_count);
  panel->model = &source_model;
  panel->source_model_vertices = source_count;
  struct ModelPointer {
    zcls::Node *node;
    ~ModelPointer() { node->model = nullptr; }
  } model_pointer{panel};
  auto renderer = make_check_renderer(window, backend, quality, options);
  struct RenderBinding {
    platform::Render3D *previous = zvid::render3d();
    ~RenderBinding() { zvid::set_render3d(previous); }
  } binding;
  zvid::set_render3d(renderer.get());
  renderer->set_viewport(0, 0, 64, 64);
  float matrix[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  platform::ModelDrawContext context;
  context.node = panel;
  context.model_to_camera = context.model_to_world = matrix;
  context.near_z = context.clip_near = .1f;
  context.far_z = 100;
  context.fx_near = context.fy_near = 320;
  context.cx = context.cy = 32;
  context.ordinary = true;
  auto frame = [&](cc_mw3_re::VulkanFrameRenderer *target = nullptr,
                   bool read = true, bool early = false) {
    auto *active = target ? target : renderer.get();
    RenderBinding active_binding;
    zvid::set_render3d(active);
    active->clear(true, 0, 0, 0);
    std::fenv_t saved;
    require(std::fegetenv(&saved) == 0,
            "Cannot save authored floating-point environment");
    std::feclearexcept(FE_ALL_EXCEPT);
    std::feraiseexcept(FE_DIVBYZERO);
    const auto rounding = std::fegetround();
    if (early)
      require(active->submit_prepared_geometry(context, {}) ==
                  cc::geometry::Submission::submitted,
              "Material response did not reach early prepared submission");
    else
      active->begin_model_draw(context);
    const auto flags = std::fetestexcept(FE_ALL_EXCEPT);
    const auto next_rounding = std::fegetround();
    std::fesetenv(&saved);
    require(flags == FE_DIVBYZERO && rounding == next_rounding,
            "Visual rendering changed the native floating-point environment");
    if (!early) {
      require(active->replaces_model_geometry(),
              "Prepared model did not admit its authored source");
      const auto queued = zvid::transparent_queue().count;
      require(zvid::queue_transparent(6, 3, nullptr) == nullptr &&
                  zvid::transparent_queue().count == queued,
              "Deferred source geometry escaped its replacement scope");
      quad(*active, 0, 64, .9f, 0xffff00ff);
      active->end_model_draw();
      require(!active->replaces_model_geometry(),
              "Prepared model scope did not end");
    }
    if (!read)
      return std::vector<uint32_t>{};
    std::vector<uint32_t> pixels;
    int width = 0, height = 0;
    require(active->read_back(&pixels, &width, &height) && width == 64 &&
                height == 64,
            "Prepared model did not render at the output extent");
    return pixels;
  };
  const auto first = frame();
  require((first[20 * 64 + 23] & 0xff00) != 0 &&
              (first[20 * 64 + 23] & 255) == 0,
          "Prepared geometry/materials were not drawn or source geometry drew "
          "twice");
  const auto initial = renderer->asset_statistics();
  require(initial.gpu_model_draws > 0 && initial.geometry_uploads == 1,
          "Prepared rigid geometry did not use an immutable Vulkan upload");
  if (materials && threaded_checks) {
    // Compare an evolving worker-backed renderer against fresh-renderer GPU
    // baselines. Read the previous pending frame immediately after
    // notification: removing the drain or moving the settings update before it
    // changes that image, even if a later full-frame clear would conceal the
    // ordering bug.
    const platform::MissionIdentity missions[] = {
        {5, 1}, {0, 1}, {1, 2}, {9, 1}};
    std::vector<std::vector<uint32_t>> automatic_profiles;
    for (unsigned variant = 0; variant < 3; ++variant) {
      auto configured = options;
      configured.sun_explicit = variant == 1;
      configured.sun_azimuth = "45";
      configured.sun_elevation = "35";
      if (variant == 2)
        configured.lighting = "off";
      auto evolving = make_check_renderer(window, backend, quality, configured);
      evolving->set_viewport(0, 0, 64, 64);
      auto baseline = [&](platform::MissionIdentity mission) {
        auto fresh = make_check_renderer(window, backend, quality, configured);
        fresh->set_viewport(0, 0, 64, 64);
        fresh->mission_changed(mission);
        return frame(fresh.get());
      };
      const auto configured_pixels = baseline({});
      auto previous_pixels = frame(evolving.get());
      require(previous_pixels == configured_pixels,
              "Initial live lighting differs from fresh configuration");
      bool profile_changed = false, explicit_angle_changed = false;
      std::size_t mission_index = 0;
      for (const auto mission : missions) {
        const auto expected = baseline(mission);
        frame(evolving.get(), false);
        evolving->mission_changed(mission);
        std::vector<uint32_t> pending;
        int pending_width = 0, pending_height = 0;
        require(
            evolving->read_back(&pending, &pending_width, &pending_height) &&
                pending == previous_pixels,
            "Mission transition relit a pending frame or failed to drain the "
            "render worker");
        const auto actual = frame(evolving.get());
        require(actual == expected, "Live backend lighting update differs from "
                                    "a fresh-renderer baseline");
        profile_changed = profile_changed || actual != configured_pixels;
        if (variant == 2)
          require(actual == configured_pixels,
                  "Off-mode live renderer inherited a mission profile");
        if (variant == 0)
          automatic_profiles.push_back(actual);
        if (variant == 1)
          explicit_angle_changed =
              explicit_angle_changed ||
              actual != automatic_profiles.at(mission_index);
        ++mission_index;
        previous_pixels = actual;
      }
      if (variant == 1)
        require(explicit_angle_changed,
                "Explicit sun angles did not affect live profile lighting");
      if (variant != 2)
        require(
            profile_changed,
            "Live mission profile fixture produced no visible lighting change");
      evolving->mission_changed({0, 1});
      const auto known_before_reset = frame(evolving.get());
      if (variant != 2)
        require(
            known_before_reset != configured_pixels,
            "Reset control must start from distinct known-profile lighting");
      frame(evolving.get(), false);
      evolving->reset_scene_resources();
      require(frame(evolving.get()) == configured_pixels,
              "Scene reset retained previous mission lighting");
    }
  }
  matrix[9] = .5f;
  require(zcls::object3d_set_matrix(control, matrix) == 0,
          "Authored control pose could not change");
  const auto moved = frame();
  require(moved != first && (moved[20 * 64 + 23] & 0xffffff) == 0 &&
              renderer->asset_statistics().geometry_uploads == 1,
          "Native control motion did not move the prepared geometry or "
          "reuploaded its vertices");
  matrix[2] = 1;
  matrix[9] = 0;
  matrix[11] = -1.5f;
  require(zcls::object3d_set_matrix(control, matrix) == 0,
          "Authored clipped pose could not change");
  const auto clipped = frame();
  require(
      renderer->asset_statistics().cpu_clipped_triangles > 0 &&
          std::any_of(clipped.begin(), clipped.end(),
                      [](uint32_t value) { return (value & 0xffffff) != 0; }),
      "Prepared near-plane clipping did not produce visible bounded triangles");
  const uint8_t original[] = {64, 64, 64, 128};
  platform::DrawState named{};
  named.texture = renderer->create_named_texture("authored", 1, 1, original, 4);
  renderer->clear(true, 0, 0, 0);
  quad(*renderer, 0, 64, .5f, 0xffffffff, named);
  std::vector<uint32_t> named_pixels;
  int width = 0, height = 0;
  require(renderer->read_back(&named_pixels, &width, &height) &&
              (named_pixels[32 * 64 + 32] >> 24) == 128 &&
              (named_pixels[32 * 64 + 32] & 255) == 0,
          "Prepared named texture did not retain its source alpha mask");
  const uint8_t transparent[] = {64, 64, 64, 0};
  renderer->update_texture(named.texture, 1, 1, transparent);
  renderer->clear(true, 0, 0, 0);
  quad(*renderer, 0, 64, .5f, 0xffffffff, named);
  require(
      renderer->read_back(&named_pixels, &width, &height) &&
          named_pixels[32 * 64 + 32] == 0,
      "Prepared texture reload lost transparent pixels or retained old alpha");
  renderer->release_texture(named.texture);
  require(renderer->asset_statistics().named_textures == 2,
          "Named texture replacement was not recorded");
  renderer->reset_scene_resources();
  matrix[2] = matrix[9] = matrix[11] = 0;
  require(zcls::object3d_set_matrix(control, matrix) == 0,
          "Authored pose could not reset");
  require(frame() == first &&
              renderer->asset_statistics().geometry_uploads == 2,
          "Prepared scene reload reused stale geometry or changed its pixels");
  if (!materials)
    return;
  require(renderer->asset_statistics().lit_draws > 0 &&
              renderer->asset_statistics().normal_draws > 0,
          "Prepared materials did not reach a per-pixel normal-map draw");

  // Use a white albedo so lighting has a direct numeric oracle independent of
  // UV filtering.
  auto albedo = prepared_fixture::textures();
  albedo.replace(albedo.size() - 4, 4, std::string(4, char(0xff)));
  std::ofstream(folder / "textures.bin", std::ios::binary) << albedo;
  auto render_material = [&](const char *mode, uint32_t normal,
                             bool absent = false, bool mirror = false,
                             bool degenerate = false,
                             std::array<float, 3> source_normal = {0, 0, 1},
                             bool rank_one_uv = false, float roughness = -1.f,
                             float reflectivity = .04f, float metallic = 0.f) {
    zvid::set_render3d(nullptr);
    renderer.reset();
    std::ofstream(folder / "mesh.bin", std::ios::binary)
        << prepared_fixture::mesh(2, 0xffffffff, source_normal, mirror,
                                  degenerate, rank_one_uv);
    // Each deliberate UV/basis variant has its own authored compatibility hash.
    std::ifstream geometry(folder / "mesh.bin", std::ios::binary);
    const auto mesh = ::platform::prepared::read_mesh(geometry);
    const auto hash = mw34::materials::compute_mesh_uv_hash(
        mesh.batches, 0, mesh.batches.size());
    const auto metadata = surface_metadata(
        "authored:close", "authored", hash, roughness >= 0 ? roughness : .7f,
        reflectivity, metallic, roughness >= 0);
    std::ofstream(folder / "materials.bin", std::ios::binary)
        .write(metadata.data(), metadata.size());
    std::ofstream(folder / "normals.bin", std::ios::binary)
        << prepared_fixture::normals(normal, absent);
    options.lighting = mode;
    renderer = make_check_renderer(window, backend, quality, options);
    renderer->set_viewport(0, 0, 64, 64);
    zvid::set_render3d(renderer.get());
    return frame(nullptr, true,
                 options.geometry == "gpu" ||
                     options.geometry == "cpu-reference");
  };
  {
    // Measure the authored BRDF profile without the SDR display curve.
    const auto properties = SDL_GetWindowProperties(window);
    const auto hdr_property = SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN;
    const bool hdr_enabled = SDL_GetBooleanProperty(properties, hdr_property, false);
    require(SDL_SetBooleanProperty(properties, hdr_property, true),
            "Cannot isolate the BWSFM fixture from the SDR curve");
    const auto azimuth = options.sun_azimuth;
    const auto elevation = options.sun_elevation;
    const auto focal_x = context.fx_near, focal_y = context.fy_near;
    context.fx_near = context.fy_near = 2000;
    options.sun_azimuth = "270";
    options.sun_elevation = "1";
    auto response = [&](const char *mode, float roughness,
                        float reflectivity = .04f, float metallic = 1.f) {
      return render_material(mode, 0xffff8080, false, false, false, {0, 0, -1},
                             false, roughness, reflectivity, metallic);
    };
    const auto smooth = response("normal", .1f);
    const auto medium = response("normal", .7f);
    const auto rough = response("normal", 1.f);
    require(smooth != medium && medium != rough && smooth != rough,
            "BWSFM roughness did not change directional response");
    auto profile = [](const std::vector<uint32_t> &pixels) {
      int peak = 0, width = 0;
      for (int x = 0; x < 64; ++x)
        peak = std::max(peak, int(pixels[32 * 64 + x] & 255));
      const float half = 77.f + float(peak - 77) * .5f;
      for (int x = 0; x < 64; ++x)
        width += float(pixels[32 * 64 + x] & 255) >= half;
      return std::array<int, 2>{peak, width};
    };
    const auto smooth_profile = profile(smooth);
    const auto medium_profile = profile(medium);
    const auto rough_profile = profile(rough);
    std::fprintf(stderr,
                 "[BWSFM_PROFILE] roughness=.1 peak=%d width=%d; "
                 ".7 peak=%d width=%d; 1 peak=%d width=%d\n",
                 smooth_profile[0], smooth_profile[1], medium_profile[0],
                 medium_profile[1], rough_profile[0], rough_profile[1]);
    // The LDR smooth peak can saturate; the finite panel also bounds width.
    require(smooth_profile[0] > medium_profile[0] &&
                medium_profile[0] > rough_profile[0] &&
                smooth_profile[1] < medium_profile[1] &&
                medium_profile[1] <= rough_profile[1],
            "BWSFM highlight peak/width did not track roughness");
    require(response("flat", .1f) == response("flat", 1.f),
            "BWSFM response leaked into flat mode");
    require(response("off", .1f) == response("off", 1.f),
            "BWSFM response leaked into off mode");
    require(response("normal", .7f, 0.f, 0.f) !=
                response("normal", .7f, .5f, 0.f),
            "BWSFM reflectivity did not affect dielectric response");
    require(response("normal", .7f, .04f, 0.f) != medium,
            "BWSFM metallic did not affect response");
    require(response("normal", -1.f) != medium,
            "Missing BWSFM record enabled material response");
    const auto geometry_mode = options.geometry;
    for (const char *mode : {"gpu", "cpu-reference"}) {
      options.geometry = mode;
      const auto actual = response("normal", .7f);
      for (size_t pixel = 0; pixel < actual.size(); ++pixel)
        for (unsigned shift : {0U, 8U, 16U})
          require(std::abs(int((actual[pixel] >> shift) & 255) -
                           int((medium[pixel] >> shift) & 255)) <= 2,
                  "BWSFM GPU/reference response differs from prepared TL");
    }
    options.geometry = geometry_mode;
    options.sun_azimuth = azimuth;
    options.sun_elevation = elevation;
    context.fx_near = focal_x;
    context.fy_near = focal_y;
    require(SDL_SetBooleanProperty(properties, hdr_property, hdr_enabled),
            "Cannot restore display mode after BWSFM check");
  }
  const auto flat = render_material("flat", 0xffd980d9);
  require(render_material("normal", 0xffff8080) == flat,
          "Neutral BWNRM2 map changed flat material lighting");
  require(render_material("normal", 0, true) == flat,
          "Explicit absent normal map changed flat material lighting");
  require(render_material("normal", 0xff808080) == flat,
          "Zero tangent vector produced invalid material lighting");
  const auto normal = render_material("normal", 0xffd980d9);
  const auto mirrored = render_material("normal", 0xffd980d9, false, true);
  const size_t centre = 32 * 64 + 32;
  require(int(normal[centre] & 255) > int(mirrored[centre] & 255) + 30 &&
              normal != flat,
          "Mirrored UV handedness or tangent normal response was lost");
  require(render_material("normal", 0xffd980d9, false, false, true) == flat,
          "Degenerate UVs did not retain the geometric normal");
  require(render_material("normal", 0xff80ff80, false, false, false, {0, 0, 1},
                          true) == flat,
          "Rank-one UVs did not retain finite geometric lighting");
  require(render_material("normal", 0xffd980d9, false, false, false, {0, 0, 1},
                          true) == flat,
          "Rank-one UVs accepted tangent perturbation");
  const auto off = render_material("off", 0xffd980d9);
  render_material("normal", 0xffd980d9);
  require(render_material("off", 0xffd980d9) == off,
          "Material mode leaked across renderer instances");

  matrix[0] = 1.7f;
  matrix[4] = .8f;
  matrix[8] = .5f;
  require(zcls::object3d_set_matrix(control, matrix) == 0,
          "Cannot stage nonuniform source scale");
  options.sun_azimuth = "-45";
  const auto scaled =
      render_material("flat", 0xffff8080, false, false, false, {1, 1, 1});
  const double nx = 1.0 / 1.7, ny = 1.0 / .8, nz = 1.0 / .5;
  const double len = std::hypot(nx, ny, nz);
  const double radians = 3.14159265358979323846 / 180;
  const double direct = std::max(
      0.0, ((nx - nz) * std::cos(35 * radians) * std::cos(45 * radians) +
            ny * std::sin(35 * radians)) /
               len);
  const bool hdr_enabled = SDL_GetBooleanProperty(
      SDL_GetWindowProperties(window), SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN,
      false);
  const double ambient = hdr_enabled ? .30 : .36;
  const auto display_red = [hdr_enabled](double red) {
    red = std::clamp(red, 0.0, 1.0);
    return int(std::lround(
        red * (hdr_enabled ? 1.0 : 1.0 + .78 * (1.0 - red)) * 255));
  };
  const int expected = display_red(ambient + .85 * direct);
  require(
      std::abs(int((scaled[centre] >> 16) & 255) - expected) <= 1,
      "Nonuniform source scale did not use inverse-transpose material normals");
  options.sun_azimuth = "90";
  const auto scaled_map = render_material("normal", 0xffd980d9);
  // The native XY scale gives the cotangent frame a 0.8/1.7 axis ratio.
  // Transforming positions with the normal matrix would invert that ratio.
  const double ratio = .8 / 1.7;
  const double mapped_z = 1.0 / std::sqrt(1 + ratio * ratio);
  const int mapped_expected =
      display_red(ambient + .85 * std::cos(35 * radians) * mapped_z);
  require(
      std::abs(int((scaled_map[centre] >> 16) & 255) - mapped_expected) <= 1,
      "Tangent basis used normal coordinates instead of true world positions");

  matrix[0] = matrix[4] = matrix[8] = 1;
  matrix[2] = 1;
  matrix[11] = -1.5f;
  require(zcls::object3d_set_matrix(control, matrix) == 0,
          "Cannot stage clipped material source");
  options.sun_azimuth = "45";
  const auto clipped_flat =
      render_material("flat", 0xffff8080, false, false, false, {1, 0, 1});
  const auto clipped_normal =
      render_material("normal", 0xffd980d9, false, false, false, {1, 0, 1});
  const int clipped_expected = display_red(
      ambient + .85 * std::cos(35 * radians) * std::sin(45 * radians));
  require(
      std::abs(int((clipped_flat[centre] >> 16) & 255) - clipped_expected) <= 1,
      "Clipped geometry lost its authored smooth normals");
  require(clipped_normal != clipped_flat &&
              renderer->asset_statistics().normal_draws > 0 &&
              renderer->asset_statistics().cpu_clipped_triangles > 0,
          "Clipped prepared geometry lost per-pixel normal response");

  // Black albedo isolates native fog from the lighting equation. Geometry has
  // varying depth, so this catches interpolation changes and accidental sky
  // dithering.
  const auto saved_fog = zmod::fog();
  auto &fog = zmod::fog();
  fog = {};
  fog.on = 1;
  fog.far_range = 10;
  fog.far_sq = 100;
  fog.inv_range = .1f;
  fog.alt_low = 1;
  fog.alt_high = 100;
  fog.inv_alt = 1.f / 99;
  fog.color[0] = .13f;
  fog.color[1] = .37f;
  fog.color[2] = .61f;
  albedo.replace(albedo.size() - 4, 4, std::string(4, char(0)));
  std::ofstream(folder / "textures.bin", std::ios::binary) << albedo;
  const auto clipped_fog = render_material("off", 0xffff8080);
  require(render_material("normal", 0xffff8080) == clipped_fog,
          "Clipped material lighting changed native fog");
  matrix[11] = 2.5f;
  require(zcls::object3d_set_matrix(control, matrix) == 0,
          "Cannot stage varying-depth fog source");
  const auto gpu_fog = render_material("off", 0xffff8080);
  require(render_material("normal", 0xffff8080) == gpu_fog,
          "Rigid material lighting changed native fog");
  platform::DrawState hud;
  hud.sharp = hud.depth_always = true;
  quad(*renderer, 0, 16, .5f, 0xff112233, hud);
  std::vector<uint32_t> composed;
  require(renderer->read_back(&composed, &width, &height) &&
              composed[8 * 64 + 8] == 0xff112233 &&
              composed[centre] == gpu_fog[centre],
          "Prepared lighting changed sharp HUD composition or existing world "
          "color");
  zmod::fog() = saved_fog;
  if (materials) {
    check_terrain_surface_normals(window, backend);
  }
}
} // namespace

#include "geometry_checks.inc"
#include "platform_checks.inc"
#include "prepared_gpu_checks.inc"
#include "ultrawide_checks.inc"

void check_campaign_lighting_and_policy() {
  using namespace mw34::campaign;
  mw34::sun::Settings base{};
  base.enabled = true;
  const auto sdr = mw34::sun::display_ambient(base, true, false);
  require(sdr.ambient == base.ambient * 1.2f && sdr.color == base.color &&
              sdr.strength == base.strength && sdr.sdr_shadow_lift == .78f,
          "SDR should boost only ambient light");
  require(mw34::sun::display_ambient(base, true, true).ambient == base.ambient &&
              mw34::sun::display_ambient(base, false, false).ambient == base.ambient,
          "HDR and lighting-off should retain authored ambient");
  const auto c1m2_lift =
      mission_sun_settings(base, resolve_mission_lighting("c1m2"), true, false,
                           nullptr, "c1m2");
  const auto c1m1_lift =
      mission_sun_settings(base, resolve_mission_lighting("c1m1"), true, false,
                           nullptr, "c1m1");
  require(c1m2_lift.sdr_extra_shadow_lift > 0.f &&
              c1m1_lift.sdr_extra_shadow_lift == 0.f &&
              base.sdr_extra_shadow_lift == 0.f,
          "C1M2 SDR tuning leaked into other missions");
  const auto training = resolve_mission_lighting("t1m1");
  const auto lit = mission_sun_settings(base, training, true, false);
  require(lit.azimuth == 95.f && lit.elevation == 6.f && lit.strength == 1.05f,
          "Training lighting was not applied");
  const auto manual = mission_sun_settings(base, training, true, true);
  require(manual.azimuth == 45.f && manual.elevation == 35.f,
          "Explicit default sun angles were overwritten");
  const auto off = mission_sun_settings(base, training, false, false);
  require(off.strength == base.strength && off.color == base.color &&
              off.azimuth == base.azimuth,
          "Off-mode inherited campaign lighting");
  const auto restored = mission_sun_settings(
      base, resolve_mission_lighting("unknown"), true, false);
  require(restored.color == base.color && restored.ambient == base.ambient,
          "Unknown mission inherited stale lighting");
  require(
      mission_sun_settings(base, resolve_mission_lighting("c2m2"), true, false)
              .strength == 0.f,
      "Sealed mission retained directional sun");
  {
    const auto campaign_identity =
        platform::make_mission_identity(1, 1, 2, true);
    require(campaign_identity.world_index == 1 && campaign_identity.number == 2,
            "Campaign identity must retain coordinates");
    const auto training_identity =
        platform::make_mission_identity(4, 5, 1, true);
    require(training_identity.world_index == 5 && training_identity.number == 1,
            "Training identity must retain coordinates");
    const auto campaign_sun = mission_sun_settings(
        base,
        resolve_mission_lighting(canonical_mission_id(
            campaign_identity.world_index, campaign_identity.number)),
        true, false);
    require(campaign_sun.strength == 0.f, "Campaign c2m2 must be sealed");
    for (const auto identity :
         {platform::make_mission_identity(2, 1, 2, true),
          platform::make_mission_identity(3, 1, 2, true),
          platform::make_mission_identity(9, 1, 2, true),
          platform::make_mission_identity(1, 1, 2, false)}) {
      require(identity.world_index == -1 && identity.number == 0,
              "Non-campaign or failed load identity must be empty");
      const auto id =
          canonical_mission_id(identity.world_index, identity.number);
      require(id.empty(), "Non-campaign load acquired a campaign profile");
      const auto settings =
          mission_sun_settings(base, resolve_mission_lighting(id), true, false);
      require(settings.strength == base.strength &&
                  settings.color == base.color &&
                  settings.ambient == base.ambient,
              "Non-campaign or failed load must restore configured lighting");
    }
  }
  {
    platform::prepared::EnvironmentSelection selection{};
    selection.mission_ambient.emplace(
        "c1m1", platform::prepared::MissionAmbientOverride{
                    {0.18f, 0.22f, 0.32f}, 0.14f});
    auto resolve_sequence = [&](const platform::MissionIdentity &identity,
                                bool enabled, bool explicit_angles = false) {
      const auto id =
          canonical_mission_id(identity.world_index, identity.number);
      ResolvedMissionLighting resolved{};
      if (!id.empty())
        resolved = resolve_mission_lighting(id);
      return mission_sun_settings(base, resolved, enabled, explicit_angles,
                                  &selection, id);
    };
    const auto c1m1_overridden =
        resolve_sequence(platform::make_mission_identity(1, 0, 1, true), true);
    require(c1m1_overridden.ambient_color ==
                    std::array<float, 3>{0.18f, 0.22f, 0.32f} &&
                c1m1_overridden.ambient == 0.14f &&
                c1m1_overridden.azimuth == 245.0f &&
                c1m1_overridden.elevation == 7.0f &&
                c1m1_overridden.color ==
                    std::array<float, 3>{1.0f, 0.45f, 0.2f} &&
                c1m1_overridden.strength == 1.4f &&
                c1m1_overridden.night_world_gain == 1.0f &&
                !c1m1_overridden.suppress_sky,
            "c1m1 pack ambient override or untouched directional/policy fields "
            "mismatched");
    const auto c1m1_manual = resolve_sequence(
        platform::make_mission_identity(1, 0, 1, true), true, true);
    require(c1m1_manual.azimuth == base.azimuth &&
                c1m1_manual.elevation == base.elevation &&
                c1m1_manual.ambient_color ==
                    std::array<float, 3>{0.18f, 0.22f, 0.32f} &&
                c1m1_manual.ambient == 0.14f && c1m1_manual.strength == 1.4f,
            "Explicit sun angles with pack ambient override failed");
    const auto c1m2_builtin =
        resolve_sequence(platform::make_mission_identity(1, 0, 2, true), true);
    require(c1m2_builtin.ambient_color ==
                    std::array<float, 3>{0.18f, 0.22f, 0.32f} &&
                c1m2_builtin.ambient == 0.14f &&
                c1m2_builtin.azimuth == 210.0f &&
                c1m2_builtin.elevation == 25.0f &&
                c1m2_builtin.color ==
                    std::array<float, 3>{0.35f, 0.45f, 0.65f} &&
                c1m2_builtin.strength == 0.28f &&
                c1m2_builtin.night_world_gain == 0.24f &&
                !c1m2_builtin.suppress_sky,
            "Absent c1m2 pack entry did not restore c1m2 built-in ambient and "
            "directional fields");
    selection.mission_ambient["c1m1"] = {{0.85f, 0.65f, 0.35f}, 0.45f};
    const auto c1m1_distinct =
        resolve_sequence(platform::make_mission_identity(1, 0, 1, true), true);
    require(c1m1_distinct.ambient_color ==
                    std::array<float, 3>{0.85f, 0.65f, 0.35f} &&
                c1m1_distinct.ambient == 0.45f &&
                c1m1_distinct.strength == 1.4f,
            "Adjusted c1m1 pack ambient override was not applied");
    const auto c1m2_after_distinct =
        resolve_sequence(platform::make_mission_identity(1, 0, 2, true), true);
    require(c1m2_after_distinct.ambient_color ==
                    std::array<float, 3>{0.18f, 0.22f, 0.32f} &&
                c1m2_after_distinct.ambient == 0.14f &&
                c1m2_after_distinct.strength == 0.28f,
            "Transition to absent c1m2 leaked previous c1m1 pack ambient");
    for (const auto invalid_identity :
         {platform::MissionIdentity{},
          platform::make_mission_identity(1, 0, 1, false),
          platform::make_mission_identity(2, 0, 1, true)}) {
      const auto reset_settings = resolve_sequence(invalid_identity, true);
      require(reset_settings.ambient_color == base.ambient_color &&
                  reset_settings.ambient == base.ambient &&
                  reset_settings.color == base.color &&
                  reset_settings.strength == base.strength &&
                  reset_settings.azimuth == base.azimuth &&
                  reset_settings.elevation == base.elevation,
              "Invalid or reset mission identity did not return base settings");
    }
    const auto off_settings =
        resolve_sequence(platform::make_mission_identity(1, 0, 1, true), false);
    require(off_settings.ambient_color == base.ambient_color &&
                off_settings.ambient == base.ambient &&
                off_settings.color == base.color &&
                off_settings.strength == base.strength &&
                off_settings.azimuth == base.azimuth &&
                off_settings.elevation == base.elevation,
            "Lighting off did not ignore pack ambient override and return base");
    auto disabled_base = base;
    disabled_base.enabled = false;
    const auto base_off_settings = mission_sun_settings(
        disabled_base, resolve_mission_lighting("c1m1"), true, false,
        &selection, "c1m1");
    require(base_off_settings.ambient_color == disabled_base.ambient_color &&
                base_off_settings.ambient == disabled_base.ambient &&
                base_off_settings.strength == disabled_base.strength,
            "Disabled base lighting did not ignore pack ambient override");
  }
  unsigned identities = 0;
  for (int world = 0; world < 6; ++world)
    for (int number = 0; number <= 7; ++number) {
      const auto id = canonical_mission_id(world, number);
      if (!id.empty()) {
        ++identities;
        require(resolve_mission_lighting(id).has_profile,
                "Native mission identity failed profile lookup");
      }
    }
  require(identities == 24 && canonical_mission_id(4, 4) == "c4m4" &&
              canonical_mission_id(-1, 1).empty(),
          "Native mission mapping has wrong bounds");

  // 1. All 24 real mission IDs distinct and findable
  static constexpr const char *const kAllMissionIds[24] = {
      "t1m1", "t1m2", "t1m3", "t1m4", "c1m1", "c1m2", "c1m3", "c1m4",
      "c2m1", "c2m2", "c2m3", "c2m4", "c3m1", "c3m2", "c3m3", "c3m4",
      "c3m5", "c3m6", "c4m1", "c4m2", "c4m3", "c4m4", "c4m5", "c4m6"};

  const auto *fuel_depot = find_builtin_environment("c3m3");
  require(fuel_depot &&
              fuel_depot->sky_tint == std::array<float, 3>{2.60f, .70f, .35f} &&
              fuel_depot->sky_brightness == 1.15f &&
              fuel_depot->sky_grade == std::array<float, 3>{.78f, .75f, .60f},
          "Fuel Depot must use current authored mission sky values");
  const auto &db = builtin_mission_database();
  require(db.size() == 24,
          "Builtin mission database must contain exactly 24 profiles");

  for (size_t i = 0; i < 24; ++i) {
    const char *id = kAllMissionIds[i];
    const MissionEnvironment *env = find_builtin_environment(id);
    require(env != nullptr,
            "Mission profile must be found in builtin database");
    require(std::string_view(env->id) == id, "Mission environment ID mismatch");

    const auto resolved = resolve_mission_lighting(id);
    require(resolved.has_profile == true,
            "Resolved mission lighting must indicate has_profile == true");
    require(resolved.environment == env,
            "Resolved environment pointer mismatch");

    // Verify distinctness across database entries
    for (size_t j = i + 1; j < 24; ++j) {
      require(std::string_view(kAllMissionIds[i]) != kAllMissionIds[j],
              "Mission IDs must be distinct");
      require(find_builtin_environment(kAllMissionIds[i]) !=
                  find_builtin_environment(kAllMissionIds[j]),
              "Mission environment pointers must be distinct");
    }
  }

  // 2. Unknown case explicit fallback (no profile, stock fallback, no crash/log
  // spam)
  for (const char *unknown_id : {"", "unknown", "c0m0", "c9m9", "t2m1"}) {
    require(find_builtin_environment(unknown_id) == nullptr,
            "Unknown mission ID must return nullptr");
    const auto unk = resolve_mission_lighting(unknown_id);
    require(unk.has_profile == false,
            "Unknown mission must yield has_profile == false");
    require(unk.environment == nullptr,
            "Unknown mission must yield null environment");
    require(unk.policy.inside == false,
            "Unknown mission must default inside == false");
    require(unk.policy.suppress_sun == false,
            "Unknown mission must default suppress_sun == false");
    require(unk.policy.suppress_sky == false,
            "Unknown mission must default suppress_sky == false");
    require(unk.policy.sky_source == SkySource::OriginalClouds,
            "Unknown mission must default to SkySource::OriginalClouds");
  }

  // 3. Sealed policy independent of potentially contradictory profile input
  // Sealed missions: c2m2, c2m4, c3m5
  for (const char *sealed_id : {"c2m2", "c2m4", "c3m5"}) {
    require(sealed_mission(sealed_id) == true,
            "Expected sealed mission identity");
    const auto sealed_res = resolve_mission_lighting(sealed_id);
    require(sealed_res.policy.inside == true,
            "Sealed mission must enforce inside == true");
    require(sealed_res.policy.suppress_sun == true,
            "Sealed mission must suppress sun");
    require(sealed_res.policy.suppress_sky == true,
            "Sealed mission must suppress sky");
    require(sealed_res.policy.sky_source == SkySource::Interior,
            "Sealed mission must resolve to SkySource::Interior");

    // Independent of contradictory profile input: even if profile_interior ==
    // false
    const auto forced_ext = resolve_environment_policy(sealed_id, false);
    require(forced_ext.inside == true,
            "Sealed policy must override contradictory exterior flag");
    require(forced_ext.suppress_sun == true,
            "Sealed policy must override contradictory sun flag");
    require(forced_ext.suppress_sky == true,
            "Sealed policy must override contradictory sky flag");
    require(forced_ext.sky_source == SkySource::Interior,
            "Sealed policy must override contradictory sky source");
  }

  // Unsealed mission with profile_interior == true gets inside policy
  const auto unsealed_interior = resolve_environment_policy("c1m1", true);
  require(unsealed_interior.inside == true,
          "Profile interior flag must set inside == true");
  require(unsealed_interior.suppress_sun == true,
          "Profile interior flag must suppress sun");
  require(unsealed_interior.suppress_sky == true,
          "Profile interior flag must suppress sky");
  require(unsealed_interior.sky_source == SkySource::Interior,
          "Profile interior flag must set Interior sky");

  // Normal unsealed mission (c1m1)
  const auto c1m1_res = resolve_mission_lighting("c1m1");
  require(c1m1_res.policy.inside == false, "c1m1 must not be inside");
  require(c1m1_res.policy.suppress_sun == false, "c1m1 must not suppress sun");
  require(c1m1_res.policy.suppress_sky == false, "c1m1 must not suppress sky");
  require(c1m1_res.policy.sky_source == SkySource::Twilight,
          "c1m1 must have Twilight sky");

  // 4. Expected sky categories
  // ClearDay: t1m2, c2m1, c3m1, c3m2, c4m6
  for (const char *id : {"t1m2", "c2m1", "c3m1", "c3m2", "c4m6"}) {
    require(mission_sky_source(id) == SkySource::ClearDay,
            "Expected ClearDay sky source");
  }
  // Twilight: t1m1, t1m4, c1m1, c3m3, c3m4, c4m3, c4m4, c4m5
  for (const char *id :
       {"t1m1", "t1m4", "c1m1", "c3m3", "c3m4", "c4m3", "c4m4", "c4m5"}) {
    require(mission_sky_source(id) == SkySource::Twilight,
            "Expected Twilight sky source");
  }
  // ClearNight: c3m6
  require(mission_sky_source("c3m6") == SkySource::ClearNight,
          "c3m6 must be ClearNight");
  // DustHaze: c2m3
  require(mission_sky_source("c2m3") == SkySource::DustHaze,
          "c2m3 must be DustHaze");
  // Interior: c2m2, c2m4, c3m5
  for (const char *id : {"c2m2", "c2m4", "c3m5"}) {
    require(mission_sky_source(id) == SkySource::Interior,
            "Expected Interior sky source");
  }
  // OriginalClouds: t1m3, c1m2, c1m3, c1m4, c4m1, c4m2
  for (const char *id : {"t1m3", "c1m2", "c1m3", "c1m4", "c4m1", "c4m2"}) {
    require(mission_sky_source(id) == SkySource::OriginalClouds,
            "Expected OriginalClouds sky source");
  }

  // 5. Representative authored numeric values
  const auto *t1m1 = find_builtin_environment("t1m1");
  require(t1m1 && t1m1->azimuth == 95.0f && t1m1->elevation == 6.0f,
          "t1m1 sun angle mismatch");
  require(t1m1->sun_strength == 1.05f && t1m1->fog_distance == 1100.0f,
          "t1m1 metrics mismatch");

  const auto *t1m2 = find_builtin_environment("t1m2");
  require(t1m2 && t1m2->heat_shimmer == 0.70f && t1m2->azimuth == 115.0f,
          "t1m2 metrics mismatch");

  const auto *t1m3 = find_builtin_environment("t1m3");
  require(t1m3 && t1m3->rain_intensity == 0.70f && !t1m3->shadows,
          "t1m3 metrics mismatch");

  const auto *c1m1 = find_builtin_environment("c1m1");
  require(c1m1 && c1m1->azimuth == 245.0f && c1m1->elevation == 7.0f,
          "c1m1 sun angle mismatch");
  require(c1m1->sun_strength == 1.40f && c1m1->shadow_range == 300.0f,
          "c1m1 metrics mismatch");

  const auto *c1m2 = find_builtin_environment("c1m2");
  require(c1m2 && c1m2->env_lights.size() == 4 && c1m2->rain_intensity == 0.35f,
          "c1m2 light emitters mismatch");

  const auto *c1m3 = find_builtin_environment("c1m3");
  require(c1m3 && c1m3->has_lightning && c1m3->rain_intensity == 0.95f,
          "c1m3 storm metrics mismatch");

  const auto *c2m2 = find_builtin_environment("c2m2");
  require(c2m2 && c2m2->is_interior && c2m2->sun_strength == 0.0f,
          "c2m2 interior metrics mismatch");

  const auto *c3m5 = find_builtin_environment("c3m5");
  require(c3m5 && c3m5->env_lights.size() == 4 && c3m5->is_interior,
          "c3m5 interior light emitters mismatch");

  const auto *c4m1 = find_builtin_environment("c4m1");
  require(c4m1 && c4m1->env_lights.size() == 3 && c4m1->volcanic_ash == 0.75f &&
              c4m1->lava_glow == 0.85f,
          "c4m1 volcanic metrics mismatch");

  const auto *c4m6 = find_builtin_environment("c4m6");
  require(c4m6 && c4m6->azimuth == 110.0f && c4m6->elevation == 28.0f &&
              c4m6->sun_strength == 1.45f,
          "c4m6 metrics mismatch");

  std::fprintf(stderr, "check_campaign_lighting_and_policy: passed (24 mission "
                       "profiles, sealed maps, sky categories)\n");
}

void check_renderer_bridge_config() {
  // 1. Zero function-static env cache test
  require(mw34::detail::presentation_time(1, 10.5) == 10.5,
          "detail_fixed_time 10.5 mismatch");
  require(mw34::detail::presentation_time(1, 25.0) == 25.0,
          "detail_fixed_time 25.0 mismatch");
  require(mw34::detail::presentation_time(1, std::nullopt) >= 0.0,
          "detail_fixed_time dynamic mismatch");

  cc_mw3_re::RendererBridgeConfig c1;
  c1.world_scale_percent = 75;
  require(c1.world_scale_percent == 75, "world_scale_percent 75 mismatch");
  cc_mw3_re::RendererBridgeConfig c2;
  c2.world_scale_percent = 90;
  require(c2.world_scale_percent == 90, "world_scale_percent 90 mismatch");

  // 2. Config propagation via configure_bridge without env mutation
  sdlfe::CoreCriticalQuality quality{};
  quality.antialiasing = "fxaa";
  quality.anisotropy = "16";

  sdlfe::CoreCriticalAssets assets{};
  assets.lighting = "normal";
  assets.geometry = "gpu";
  assets.sun_azimuth = "60";
  assets.sun_elevation = "40";

  auto cfg_dxgi = cc_mw3_re::configure_bridge(
      sdlfe::RendererBackend::core_critical_dxgi, quality, assets);
  require(cfg_dxgi.dxgi_present == true, "dxgi_present true mismatch");
  require(cfg_dxgi.fxaa == true, "fxaa propagation mismatch");
  require(cfg_dxgi.af_level == 16, "af_level propagation mismatch");
  require(cfg_dxgi.normal_maps_enabled == true,
          "normal_maps_enabled propagation mismatch");
  require(cfg_dxgi.prepared_cache == true,
          "prepared_cache propagation mismatch");
  require(cfg_dxgi.sun == true, "sun propagation mismatch");
  require(cfg_dxgi.sun_normals == true, "sun_normals propagation mismatch");
  require(std::abs(cfg_dxgi.sun_azimuth - 60.0f) < 0.01f,
          "sun_azimuth propagation mismatch");
  require(std::abs(cfg_dxgi.sun_elevation - 40.0f) < 0.01f,
          "sun_elevation propagation mismatch");
  require(cfg_dxgi.world_scale_percent == 100,
          "world_scale_percent propagation mismatch");
  require(cfg_dxgi.temporal_inputs == false,
          "temporal_inputs propagation mismatch");
  require(cfg_dxgi.fixture_lights == false,
          "fixture_lights propagation mismatch");
  require(cfg_dxgi.shadow_batch == false, "shadow_batch propagation mismatch");

  auto cfg_wsi = cc_mw3_re::configure_bridge(
      sdlfe::RendererBackend::core_critical_wsi, {}, {});
  require(cfg_wsi.dxgi_present == false, "dxgi_present false mismatch");
  require(cfg_wsi.normal_maps_enabled == false,
          "normal_maps_enabled false mismatch");
  require(cfg_wsi.sun == false, "sun false mismatch");

  check_campaign_lighting_and_policy();

  std::fprintf(stderr, "check_renderer_bridge_config: passed\n");
}

int main(int argc, char **argv) {
  bool geometry_check = false, ultrawide_check = false;
  bool native_wsi = false, quality_check = false, prepared_check = false,
       material_check = false;
  sdlfe::CoreCriticalQuality quality;
  for (int i = 1; i < argc; ++i) {
    const std::string_view option(argv[i]);
    if (option == "--render-thread")
      threaded_checks = true;
    else if (option == "--wsi")
      native_wsi = true;
    else if (option == "--prepared")
      prepared_check = true;
    else if (option == "--geometry")
      geometry_check = true;
    else if (option == "--ultrawide")
      ultrawide_check = true;
    else if (option == "--materials")
      prepared_check = material_check = true;
    else if (option == "--quality" && i + 1 < argc) {
      quality.antialiasing = argv[++i];
      quality.anisotropy = "16";
      quality_check = true;
    } else {
      std::fprintf(stderr, "usage: mw3ccvulkan_checks [--wsi] "
                           "[--prepared|--materials] [--quality MODE]\n");
      return 1;
    }
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "%s\n", SDL_GetError());
    return 1;
  }
  auto *window = SDL_CreateWindow("Core Critical adapter checks", 32, 32,
                                  SDL_WINDOW_HIDDEN |
                                      (native_wsi ? SDL_WINDOW_VULKAN : 0));
  int result = 0;
  try {
    check_local_light_core();
    check_renderer_bridge_config();
    cc_mw3_re::platform::run_platform_checks();
    require(window != nullptr, SDL_GetError());
    if (native_wsi) {
      auto *invalid =
          SDL_CreateWindow("Non-Vulkan window", 32, 32, SDL_WINDOW_HIDDEN);
      require(invalid != nullptr, SDL_GetError());
      bool rejected = false;
      try {
        auto renderer = make_check_renderer(
            invalid, sdlfe::RendererBackend::core_critical_wsi);
      } catch (const std::exception &error) {
        rejected =
            std::string_view(error.what()).find("SDL Vulkan surface:") == 0;
      }
      SDL_DestroyWindow(invalid);
      require(rejected,
              "Native WSI accepted a window without Vulkan surface support");

      auto tracker =
          std::make_shared<cc_mw3_re::platform::SdlWindowStateTracker>(window);
      require(tracker->is_owner_thread(),
              "Adapter check must execute on the SDL owner thread");
      bool foreign_sample_rejected = false;
      std::thread foreign([&] {
        try {
          tracker->sample_owner();
        } catch (const std::runtime_error &) {
          foreign_sample_rejected = true;
        }
      });
      foreign.join();
      require(foreign_sample_rejected,
              "Foreign thread must not query SDL window state");

      // Verify worker resident/resize handle resolution and coherent scope
      const auto native = cc_mw3_re::platform::get_window_native_handle(window);
      const auto hwnd = cc_mw3_re::platform::get_window_hwnd(window);
      bool worker_setup_ok = false;
      std::thread worker([&] {
        try {
          cc_mw3_re::platform::ExclusiveFullscreenHandler handler;
          handler.setup_swapchain_chain(native);
          const auto resolved =
              cc_mw3_re::platform::resolve_window_hwnd(native);
          if (resolved == hwnd) {
            worker_setup_ok = true;
          }
        } catch (...) {
          worker_setup_ok = false;
        }
      });
      worker.join();
      require(worker_setup_ok, "Worker thread must resolve cached native "
                               "window handle during swapchain setup");

      // Verify coherent snapshot and scope across queued job and owner update
      std::atomic<bool> worker_scope_ok{false};
      std::thread worker_job([&] {
        cc_mw3_re::platform::SdlWindowStateScope scope(tracker.get(),
                                                       {true, 1920, 1080});
        if (tracker->worker_drawable_width() == 1920 &&
            tracker->worker_drawable_height() == 1080 &&
            tracker->worker_visible()) {
          worker_scope_ok.store(true);
        }
      });
      tracker->sample_owner();
      worker_job.join();
      require(worker_scope_ok.load(), "Worker job must retain immutable "
                                      "captured state during owner sampling");
    }
    const auto backend = native_wsi
                             ? sdlfe::RendererBackend::core_critical_wsi
                             : sdlfe::RendererBackend::core_critical_dxgi;
    if (ultrawide_check)
      check_ultrawide(window, backend);
    else if (geometry_check) {
      check_strict_geometry(window, backend);
      const auto gpu = check_native_geometry(window, backend);
      const auto reference = check_native_geometry(window, backend, true);
      require(gpu.size() == reference.size(),
              "Geometry comparison lost a scenario");
      for (std::size_t frame = 0; frame < gpu.size(); ++frame) {
        require(gpu[frame].size() == reference[frame].size(),
                "Geometry comparison lost pixels");
        for (std::size_t pixel = 0; pixel < gpu[frame].size(); ++pixel)
          for (unsigned shift : {0U, 8U, 16U})
            require(std::abs(int((gpu[frame][pixel] >> shift) & 255) -
                             int((reference[frame][pixel] >> shift) & 255)) <=
                        2,
                    "GPU/reference transform, fog, clipping or normal debug "
                    "mismatch");
      }
      check_gpu_prepared(window, backend);
    } else if (prepared_check) {
      if (material_check && threaded_checks)
        check_sealed_environment(window, backend);
      check_prepared(window, backend, quality, material_check);
    } else if (quality_check)
      check_quality(window, backend, quality);
    else {
      check_local_light_gpu(window, backend);
      check_scene(window, backend);
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    result = 1;
  }
  if (window) {
    SDL_DestroyWindow(window);
  }
  SDL_Quit();
  return result;
}
