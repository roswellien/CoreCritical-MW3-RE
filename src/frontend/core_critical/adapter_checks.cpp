#include "frame_renderer.hpp"
#include "frontend/sdl/sdl_child.h"

#include <cstdio>
#include <filesystem>
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

int main(int argc, char **argv) {
    const bool native_wsi = argc == 2 && std::string_view(argv[1]) == "--wsi";
    if (argc != 1 && !native_wsi) {
        std::fprintf(stderr, "usage: mw3ccvulkan_checks [--wsi]\n");
        return 1;
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
        check_scene(window, native_wsi ? sdlfe::RendererBackend::core_critical_wsi
                                       : sdlfe::RendererBackend::core_critical_dxgi);
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
