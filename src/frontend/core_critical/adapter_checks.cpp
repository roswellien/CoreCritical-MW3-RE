#include "frame_renderer.hpp"
#include "frontend/sdl/sdl_child.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "tests/prepared_visual_fixture.h"
#include <cfenv>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void quad(platform::Render3D &renderer, float low, float high, float depth, uint32_t color,
          const platform::DrawState &state = {}) {
    const platform::TLVertex vertices[] = {{low, low, depth, 1, color, 0, 0, 0},
                                           {high, low, depth, 1, color, 0, 1, 0},
                                           {high, high, depth, 1, color, 0, 1, 1},
                                           {low, high, depth, 1, color, 0, 0, 1}};
    renderer.draw(platform::Primitive::kFan, vertices, 4, state);
}

void check_scene(SDL_Window *window, sdlfe::RendererBackend backend) {
    auto renderer = cc_mw3_re::make_renderer(window, backend);
    renderer->set_viewport(0, 0, 32, 32);
    renderer->clear(true, 0, 0, 255);
    renderer->begin_scene();
    quad(*renderer, 8, 24, .75f, 0xff00ff00);
    quad(*renderer, 4, 28, .25f, 0xffff0000);
    renderer->end_scene();
    auto pixel = [&](int x, int y) {
        std::vector<uint32_t> pixels;
        int width = 0, height = 0;
        require(renderer->read_back(&pixels, &width, &height) && width == 32 && height == 32,
                "Could not read the Vulkan scene");
        return pixels.at(y * width + x);
    };
    require(pixel(16, 16) == 0xff00ff00 && pixel(6, 6) == 0xffff0000 && pixel(2, 2) == 0xff0000ff,
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
            "Depth-only clear changed color or a released texture lost its queued draw");
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

    renderer->clear(true, 200, 100, 50);
    const uint8_t shadow_color[] = {128, 255, 128, 128};
    platform::DrawState shadow{};
    shadow.texture = renderer->create_texture(1, 1, shadow_color, 4);
    shadow.multiply = true;
    quad(*renderer, 0, 32, .5f, 0xffffffff, shadow);
    renderer->release_texture(shadow.texture);
    require(pixel(16, 16) == 0xff646419, "The multiply blend changed stock color or alpha");

    // Vary reciprocal W so a screen-linear shader would give a different sample.
    platform::TLVertex triangle[] = {{0, 0, .5f, 1, 0xffff0000, 0xff000000, 0, 0},
                                     {32, 0, .5f, .25f, 0xff0000ff, 0, 0, 0},
                                     {0, 32, .5f, .25f, 0xff0000ff, 0, 0, 0}};
    renderer->clear(true, 0, 0, 0);
    renderer->draw(platform::Primitive::kFan, triangle, 3, {});
    require(pixel(8, 8) == 0xffc70038, "Diffuse interpolation lost perspective correction");
    for (auto &vertex : triangle)
        vertex.diffuse = 0xffffffff;
    renderer->clear(true, 0, 0, 0);
    platform::DrawState fogged{};
    fogged.fog = true;
    renderer->set_fog_color(0, 0, 0);
    renderer->draw(platform::Primitive::kFan, triangle, 3, fogged);
    require(pixel(8, 8) == 0xffc7c7c7, "Fog interpolation lost perspective correction");

    const auto ready = "cc-adapter-ready-" + std::to_string(SDL_GetPerformanceCounter());
    require(!std::filesystem::exists(ready), "Ready marker path already exists");
    sdlfe::set_ready_file(ready);
    renderer->present();
    sdlfe::set_ready_file("");
    require(!std::filesystem::remove(ready), "A hidden frame signaled first presentation");
    require(renderer->presentation_count() == 0, "A hidden frame counted as a presentation");

    if (backend == sdlfe::RendererBackend::core_critical_wsi) {
        require(SDL_ShowWindow(window) && SDL_SyncWindow(window), SDL_GetError());
        require(SDL_MinimizeWindow(window) && SDL_SyncWindow(window), SDL_GetError());
        SDL_PumpEvents();
        require((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0,
                "The test window did not minimize");
        sdlfe::set_ready_file(ready);
        renderer->present();
        require(!std::filesystem::exists(ready), "A minimized frame signaled first presentation");
        require(renderer->presentation_count() == 0, "A minimized frame counted as a presentation");
        require(SDL_RestoreWindow(window) && SDL_SyncWindow(window), SDL_GetError());
        for (int i = 0; i < 30 && !std::filesystem::exists(ready); ++i) {
            SDL_PumpEvents();
            renderer->present();
            SDL_Delay(10);
        }
        sdlfe::set_ready_file("");
        require(std::filesystem::remove(ready), "A restored Vulkan window did not present");
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
        require(pixel(8, 8) == 0xffc7c7c7, "Swapchain resizing changed the resident scene");
    }
}

} // namespace

namespace {
void check_quality(SDL_Window *window, sdlfe::RendererBackend backend,
                   const sdlfe::CoreCriticalQuality &quality) {
    require(SDL_SetWindowSize(window, 64, 64) && SDL_SyncWindow(window), SDL_GetError());
    for (const auto invalid :
         {sdlfe::CoreCriticalQuality{"smaa", "1"}, sdlfe::CoreCriticalQuality{"fsr4", "1"},
          sdlfe::CoreCriticalQuality{"off", "0"}, sdlfe::CoreCriticalQuality{"off", "3"},
          sdlfe::CoreCriticalQuality{"off", "16junk"}}) {
        bool rejected = false;
        try {
            auto unsupported = cc_mw3_re::make_renderer(window, backend, invalid);
        } catch (const std::exception &error) {
            rejected = std::string_view(error.what()).find("--cc-") == 0;
        }
        require(rejected, "An unsupported quality request was silently accepted");
    }
    auto original = cc_mw3_re::make_renderer(window, backend);
    original->set_viewport(0, 0, 64, 64);
    original->clear(true, 0, 0, 0);
    std::vector<uint32_t> original_pixels;
    int original_width = 0, original_height = 0;
    require(original->read_back(&original_pixels, &original_width, &original_height),
            "The original-quality renderer did not initialize");
    auto renderer = cc_mw3_re::make_renderer(window, backend, quality);
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
    require(renderer->read_back(&pixels, &width, &height) && width == 64 && height == 64,
            "Quality readback changed game-facing output dimensions");
    if (quality.anisotropy != "1") {
        const auto value = pixels[32 * 64 + 32] & 255U;
        require(value >= 120 && value <= 135, "AF failed to filter the minified checker texture");
        const auto stats = renderer->quality_statistics();
        require(stats.anisotropic_samplers > 0 && stats.mip_levels >= 6,
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
    require(renderer->read_back(&pixels, &width, &height), "Quality frame did not read back");
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
        require(intermediate > 0, "World antialiasing did not smooth the authored diagonal");
    if (quality.antialiasing == "fxaa")
        require(renderer->quality_statistics().fxaa_passes >= 2, "World FXAA pass did not execute");
    original->clear(true, 1, 2, 3);
    require(original->read_back(&original_pixels, &original_width, &original_height) &&
                original_width == 64 && original_height == 64 && original_pixels[0] == 0xff010203,
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
    require(renderer->read_back(&restored, &width, &height) && pixels == restored &&
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
        require(renderer->restore_frame(), "Could not restore straight-alpha pixels");
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
    require(renderer->read_back(&restored, &width, &height) && restored[0] == 0xff0a141e,
            "A quality scene reload retained old pixels");
    require(renderer->quality_statistics().fxaa_passes >= previous_stats.fxaa_passes,
            "Quality counters were lost during scene reload");
    require(SDL_ShowWindow(window) && SDL_SyncWindow(window), SDL_GetError());
    for (const auto size : {std::array{64, 64}, std::array{48, 72}}) {
        require(SDL_SetWindowSize(window, size[0], size[1]) && SDL_SyncWindow(window),
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
        require(renderer->read_back(&restored, &width, &height) && width == size[0] &&
                    height == size[1],
                "Quality resize changed game-facing readback dimensions");
    }
}
} // namespace

namespace {
void check_prepared(SDL_Window *window, sdlfe::RendererBackend backend,
                    const sdlfe::CoreCriticalQuality &quality) {
    const auto folder = std::filesystem::temp_directory_path() /
                        ("cc-prepared-check-" + std::to_string(SDL_GetPerformanceCounter()));
    require(std::filesystem::create_directory(folder), "Authored fixture directory already exists");
    struct Files {
        std::filesystem::path folder;
        ~Files() {
            std::error_code error;
            std::filesystem::remove(folder / "mesh.bin", error);
            std::filesystem::remove(folder / "textures.bin", error);
            std::filesystem::remove(folder, error);
        }
    } files{folder};
    // Diagnostic red must not tint the bound green albedo a second time.
    std::ofstream(folder / "mesh.bin", std::ios::binary) << prepared_fixture::mesh(2, 0xffff0000);
    std::ofstream(folder / "textures.bin", std::ios::binary) << prepared_fixture::textures();
    sdlfe::CoreCriticalAssets options;
    options.textures = (folder / "textures.bin").u8string();
    options.models.push_back({"authored", "0", (folder / "mesh.bin").u8string(), options.textures});
    require(SDL_SetWindowSize(window, 64, 64) && SDL_SyncWindow(window), SDL_GetError());
    std::unique_ptr<zcls::Node, int (*)(zcls::Node *)> root(zcls::object3d_create(),
                                                            zcls::tree_destroy);
    auto *control = zcls::object3d_create(), *panel = zcls::object3d_create();
    require(root && control && panel, "Authored native nodes could not be created");
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
    auto renderer = cc_mw3_re::make_renderer(window, backend, quality, options);
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
    auto frame = [&]() {
        renderer->clear(true, 0, 0, 0);
        std::fenv_t saved;
        require(std::fegetenv(&saved) == 0, "Cannot save authored floating-point environment");
        std::feclearexcept(FE_ALL_EXCEPT);
        std::feraiseexcept(FE_DIVBYZERO);
        const auto rounding = std::fegetround();
        renderer->begin_model_draw(context);
        const auto flags = std::fetestexcept(FE_ALL_EXCEPT);
        const auto next_rounding = std::fegetround();
        std::fesetenv(&saved);
        require(flags == FE_DIVBYZERO && rounding == next_rounding,
                "Visual rendering changed the native floating-point environment");
        require(renderer->replaces_model_geometry(),
                "Prepared model did not admit its authored source");
        const auto queued = zvid::transparent_queue().count;
        require(zvid::queue_transparent(6, 3, nullptr) == nullptr &&
                    zvid::transparent_queue().count == queued,
                "Deferred source geometry escaped its replacement scope");
        quad(*renderer, 0, 64, .9f, 0xffff00ff);
        renderer->end_model_draw();
        require(!renderer->replaces_model_geometry(), "Prepared model scope did not end");
        std::vector<uint32_t> pixels;
        int width = 0, height = 0;
        require(renderer->read_back(&pixels, &width, &height) && width == 64 && height == 64,
                "Prepared model did not render at the output extent");
        return pixels;
    };
    const auto first = frame();
    require((first[20 * 64 + 23] & 0xff00) != 0 && (first[20 * 64 + 23] & 255) == 0,
            "Prepared geometry/materials were not drawn or source geometry drew twice");
    const auto initial = renderer->asset_statistics();
    require(initial.gpu_model_draws > 0 && initial.geometry_uploads == 1,
            "Prepared rigid geometry did not use an immutable Vulkan upload");
    matrix[9] = .5f;
    require(zcls::object3d_set_matrix(control, matrix) == 0,
            "Authored control pose could not change");
    const auto moved = frame();
    require(moved != first && (moved[20 * 64 + 23] & 0xffffff) == 0 &&
                renderer->asset_statistics().geometry_uploads == 1,
            "Native control motion did not move the prepared geometry or reuploaded its vertices");
    matrix[2] = 1;
    matrix[9] = 0;
    matrix[11] = -1.5f;
    require(zcls::object3d_set_matrix(control, matrix) == 0,
            "Authored clipped pose could not change");
    const auto clipped = frame();
    require(renderer->asset_statistics().cpu_clipped_triangles > 0 &&
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
    require(renderer->read_back(&named_pixels, &width, &height) && named_pixels[32 * 64 + 32] == 0,
            "Prepared texture reload lost transparent pixels or retained old alpha");
    renderer->release_texture(named.texture);
    require(renderer->asset_statistics().named_textures == 2,
            "Named texture replacement was not recorded");
    renderer->reset_scene_resources();
    matrix[2] = matrix[9] = matrix[11] = 0;
    require(zcls::object3d_set_matrix(control, matrix) == 0, "Authored pose could not reset");
    require(frame() == first && renderer->asset_statistics().geometry_uploads == 2,
            "Prepared scene reload reused stale geometry or changed its pixels");
}
} // namespace

int main(int argc, char **argv) {
    bool native_wsi = false, quality_check = false, prepared_check = false;
    sdlfe::CoreCriticalQuality quality;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--wsi")
            native_wsi = true;
        else if (option == "--prepared")
            prepared_check = true;
        else if (option == "--quality" && i + 1 < argc) {
            quality.antialiasing = argv[++i];
            quality.anisotropy = "16";
            quality_check = true;
        } else {
            std::fprintf(stderr, "usage: mw3ccvulkan_checks [--wsi] [--quality MODE]\n");
            return 1;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    auto *window = SDL_CreateWindow("Core Critical adapter checks", 32, 32,
                                    SDL_WINDOW_HIDDEN | (native_wsi ? SDL_WINDOW_VULKAN : 0));
    int result = 0;
    try {
        require(window != nullptr, SDL_GetError());
        if (native_wsi) {
            auto *invalid = SDL_CreateWindow("Non-Vulkan window", 32, 32, SDL_WINDOW_HIDDEN);
            require(invalid != nullptr, SDL_GetError());
            bool rejected = false;
            try {
                auto renderer =
                    cc_mw3_re::make_renderer(invalid, sdlfe::RendererBackend::core_critical_wsi);
            } catch (const std::exception &error) {
                rejected = std::string_view(error.what()).find("SDL Vulkan surface:") == 0;
            }
            SDL_DestroyWindow(invalid);
            require(rejected, "Native WSI accepted a window without Vulkan surface support");
        }
        const auto backend = native_wsi ? sdlfe::RendererBackend::core_critical_wsi
                                        : sdlfe::RendererBackend::core_critical_dxgi;
        if (prepared_check)
            check_prepared(window, backend, quality);
        else if (quality_check)
            check_quality(window, backend, quality);
        else
            check_scene(window, backend);
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
