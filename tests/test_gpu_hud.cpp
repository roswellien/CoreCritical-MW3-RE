#include <algorithm>
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "frontend/sdl/gpu_hud.h"
#include "frontend/sdl/sdl_render3d.h"
#include "frontend/sdl/sdl_twod_host.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zrender/zrndr_blur.h"
#include "gamez/zrender/zrndr_span.h"
#include "gamez/zrender/zrndr_tint.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zvideo/zvid_format.h"
#include "platform/prepared_hud.h"

namespace {
int random_calls = 0;
int32_t fixed_random() {
  ++random_calls;
  return 12345;
}
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class Function>
void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  require(rejected, "Malformed HUD input was accepted");
}
class Recorder : public sdlfe::FrameRenderer {
 public:
  struct Draw {
    platform::Primitive primitive;
    std::vector<platform::TLVertex> vertices;
    platform::DrawState state;
  };
  std::vector<Draw> draws;
  std::vector<std::vector<uint32_t>> images;
  std::vector<int> released;
  bool flushed = false;
  bool allocation_fails = false;
  int x = 3, y = 4, w = 5, h = 6;
  bool ok() const override { return true; }
  void capture_next(const char*) override {}
  void set_shape(const sdlfe::FrameShape*) override {}
  void flush() override { flushed = true; }
  void viewport(int* a, int* b, int* c, int* d) const override {
    *a = x;
    *b = y;
    *c = w;
    *d = h;
  }
  void set_viewport(int a, int b, int c, int d) override {
    require(flushed, "HUD changed viewport before flushing world draws");
    x = a;
    y = b;
    w = c;
    h = d;
  }
  int32_t create_texture(int width, int height, const uint8_t* pixels, uint8_t) override {
    if (allocation_fails) return -1;
    std::vector<uint32_t> copy(size_t(width) * height);
    std::memcpy(copy.data(), pixels, copy.size() * 4);
    images.push_back(std::move(copy));
    return int(images.size() - 1);
  }
  void release_texture(int32_t handle) override { released.push_back(handle); }
  void update_texture(int32_t, int, int, const uint8_t*) override {
    throw std::runtime_error("Unexpected texture update");
  }
  void clear(bool, uint8_t, uint8_t, uint8_t) override {}
  void begin_scene() override {}
  void end_scene() override {}
  void draw(platform::Primitive primitive, const platform::TLVertex* vertices, int count,
            const platform::DrawState& state) override {
    draws.push_back({primitive, {vertices, vertices + count}, state});
  }
  void set_fog_color(uint8_t, uint8_t, uint8_t) override {}
  void present() override {}
};
void artwork_checks(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  const auto path = directory / "0000000000000001.rgba";
  std::array<uint8_t, 36> bytes{};
  std::memcpy(bytes.data(), "MW3H", 4);
  bytes[4] = 2;
  bytes[8] = 1;
  const float uv[] = {.25f, 0, .75f, 1};
  std::memcpy(bytes.data() + 12, uv, sizeof uv);
  const uint8_t pixels[] = {200, 100, 50, 128, 255, 200, 100, 0};
  std::memcpy(bytes.data() + 28, pixels, 8);
  auto write = [&](size_t count) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), count);
  };
  write(bytes.size());
  auto loaded = platform::load_hud_artwork(directory);
  require(loaded.size() == 1 && loaded.at(1)->pixels == std::vector<uint32_t>{0x80193264, 0},
          "MW3H premultiplication changed");
  require(loaded.at(1)->u0 == .25f && loaded.at(1)->u1 == .75f, "MW3H crop was lost");
  write(35);
  rejects([&] { platform::load_hud_artwork(directory); });
  bytes[4] = 0;
  write(36);
  rejects([&] { platform::load_hud_artwork(directory); });
  bytes[4] = 2;
  bytes[24] = 0;
  bytes[25] = 0;
  bytes[26] = 0x80;
  bytes[27] = 0x7f;
  write(36);
  rejects([&] { platform::load_hud_artwork(directory); });
  const uint16_t opaque[] = {0xf800, 0};
  require(platform::hud_identity(2, 1, opaque, 2) == 0xdaa81911a07d967eULL,
          "Private HUD fingerprint contract changed");
  rejects([&] { platform::hud_identity(2, 2, opaque, 2); });
}
void drawing_checks() {
  Recorder recorder;
  sdlfe::HudOptions options;
  options.mode = "core-critical";
  options.text = false;
  std::array<uint16_t, 48> baseline{}, shadow{};
  baseline.fill(0xf81f);
  shadow = baseline;
  uint16_t source_pixels[] = {0xf800, 0x07e0, 0x001f, 0};
  zimg::Texture source{};
  source.width = source.height = 2;
  source.width_again = 2;
  source.pixels = source_pixels;
  source.flags = zimg::kAlpha | zimg::kTwoBytes;
  auto target = [&](auto& pixels) {
    zrndr::target2d() = {reinterpret_cast<uint8_t*>(pixels.data()), 8, 6, 16, 6};
  };
  auto paint = [&] {
    zrndr::blit16(&source, -1, 2, 0, nullptr);
    zrndr::line16(1, 1, 4, 1, 0xffff);
    zrndr::span_state().dest = zrndr::target2d().base + 4 * 16;
    zrndr::span_fill16(0x001f, 3);
  };
  target(baseline);
  paint();
  target(shadow);
  {
    sdlfe::GpuHud hud(recorder, options);
    const int previous = std::fegetround();
    std::fesetround(FE_DOWNWARD);
    std::feraiseexcept(FE_INVALID);
    const int flags = std::fetestexcept(FE_ALL_EXCEPT);
    hud.begin(shadow.data(), 8, 6, 16, 18);
    paint();
    require(hud.finish(), "Ordinary native HUD did not use GPU composition");
    require(std::fegetround() == FE_DOWNWARD && std::fetestexcept(FE_ALL_EXCEPT) == flags,
            "GPU HUD polluted native FP state");
    std::feclearexcept(FE_ALL_EXCEPT);
    std::fesetround(previous);
    require(shadow == baseline, "Observing draws changed the native CPU shadow");
    require(recorder.draws.size() == 3 && recorder.draws[0].state.sharp &&
                recorder.draws[1].primitive == platform::Primitive::kStrip &&
                recorder.draws[1].vertices.size() == 22,
            "GPU HUD lost draw ordering or logical pixel coverage");
    const auto& vertices = recorder.draws[0].vertices;
    require(vertices[0].sx == 0 && vertices[0].sy == 2 && vertices[0].tu == .5f &&
                vertices[2].sx == 1 && vertices[2].sy == 4,
            "GPU image clip/crop changed");
    require(recorder.x == 3 && recorder.y == 4 && recorder.w == 5 && recorder.h == 6,
            "HUD did not restore world viewport");
    source_pixels[0] = 0;
    require(recorder.images[0][0] == 0xff0000ff, "GPU texture borrowed native pixels");
    hud.begin(shadow.data(), 8, 6, 16, 18);
    zrndr::observe_framebuffer_write(shadow.data(), "fixture read/write effect");
    require(!hud.finish(), "Read/write effect discarded the CPU compatibility layer");
    hud.begin(shadow.data(), 8, 6, 16, 18);
    zrndr::line16(1, 1, 4, 1, 0xffff);
    zrndr::set_finish_target(shadow.data(), 8, 6, 16);
    zrndr::blur(nullptr, 3);
    require(!hud.finish(), "Pause combined blur discarded the CPU compatibility layer");
    baseline = shadow;
    zrndr::noise_table().assign(0x6401, 32);
    target(baseline);
    zrndr::line16(1, 1, 4, 1, 0xffff);
    zrndr::set_finish_target(baseline.data(), 8, 6, 16);
    random_calls = 0;
    zrndr::static_fill(nullptr, 1, 0x001f, 0x07e0, 0xf800, fixed_random);
    const int baseline_calls = random_calls;
    target(shadow);
    hud.begin(shadow.data(), 8, 6, 16, 18);
    zrndr::line16(1, 1, 4, 1, 0xffff);
    zrndr::set_finish_target(shadow.data(), 8, 6, 16);
    random_calls = 0;
    zrndr::static_fill(nullptr, 1, 0x001f, 0x07e0, 0xf800, fixed_random);
    require(
        !hud.finish() && shadow == baseline && random_calls == baseline_calls,
        "Damage static changed native pixels/random calls or discarded compatibility composition");
    hud.begin(shadow.data(), 8, 6, 16, 18);
    source_pixels[0] = 0xf81f;
    source.flags = zimg::kTwoBytes;
    zrndr::blit16(&source, 0, 0, 0, nullptr);
    require(!hud.finish(), "Unkeyed layer erase became opaque magenta");
    hud.begin(shadow.data(), 8, 6, 16, 18);
    hud.cancel();
    require(!zrndr::draw_observer() && !hud.finish(), "Cancelled target left a dangling observer");
    hud.begin(shadow.data(), 8, 6, 16, 18);
    require(!hud.finish(), "Empty list suppressed an unobserved canvas");
  }
  require(recorder.released.size() == recorder.images.size(),
          "HUD GPU textures leaked at shutdown");
  Recorder failing;
  failing.allocation_fails = true;
  {
    sdlfe::GpuHud hud(failing, options);
    hud.begin(shadow.data(), 8, 6, 16, 18);
    source_pixels[0] = 0xf800;
    source.flags = zimg::kAlpha | zimg::kTwoBytes;
    rejects([&] { zrndr::blit16(&source, 0, 0, 0, nullptr); });
    hud.cancel();
    require(failing.draws.empty(), "Texture allocation failure drew an untextured HUD");
  }
  zrndr::target2d() = {};
}
void text_checks(const char* fonts) {
  const auto saved = zutl::mounts();
  zutl::mounts() = {};
  zutl::mount(fonts);
  zvid::set_pixel_format(5, 6, 5);
  sdlfe::SdlTwodHost host(nullptr, nullptr, nullptr);
  host.enable_hud_text(true);
  auto* image = host.image_new(64, 32);
  image->flags |= zimg::kAlpha;
  std::fill_n(static_cast<uint16_t*>(image->pixels), 64 * 32, uint16_t(0xf81f));
  const auto font = host.CreateFontA(-16, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 1, 0, "Arial");
  require(font != 0, "Text fixture font is unavailable");
  const auto dc = host.image_dc(image);
  host.SelectObject(dc, font);
  host.SetTextColor(dc, 0x0000ff);
  host.SetBkMode(dc, 1);
  ztwod::Rect clip{4, 4, 40, 24};
  host.DrawTextA(dc, "MW3", -1, &clip, 0);
  const auto enhanced = host.hud_text_image(image, 3, 0xf81f);
  require(enhanced && enhanced->width == 192 && enhanced->height == 96,
          "HUD text was not rasterized at output density");
  unsigned antialiased = 0, opaque = 0;
  for (unsigned y = 0; y < 96; ++y)
    for (unsigned x = 0; x < 192; ++x) {
      const auto pixel = enhanced->pixels[y * 192 + x];
      if (x < 12 || x >= 120 || y < 12 || y >= 72)
        require(pixel == 0, "Enhanced text escaped native clip");
      const unsigned alpha = pixel >> 24;
      antialiased += alpha > 0 && alpha < 255;
      opaque += alpha == 255;
      require((pixel & 0xffff00) == 0 && (pixel & 255) == alpha,
              "Enhanced glyph lost premultiplied red");
    }
  require(antialiased > 0 && opaque > 0, "Enhanced glyph lacks antialiased coverage");
  host.DrawTextA(dc, "MW3", -1, &clip, 0);
  require(host.hud_text_image(image, 3, 0xf81f) == enhanced,
          "Unchanged native text accumulated recipes");
  host.SetTextColor(dc, 0xff0000);
  host.DrawTextA(dc, "MW3", -1, &clip, 0);
  host.SetTextColor(dc, 0x0000ff);
  host.DrawTextA(dc, "MW3", -1, &clip, 0);
  const auto repainted = host.hud_text_image(image, 3, 0xf81f);
  require(
      repainted && std::count(repainted->pixels.begin(), repainted->pixels.end(), 0xff0000ff) > 10,
      "Intervening text paint changed recipe order");
  static_cast<uint16_t*>(image->pixels)[0] = 0;
  require(!host.hud_text_image(image, 3, 0xf81f),
          "Stale text replaced externally changed native pixels");
  host.DeleteDC(dc);
  host.DeleteObject(font);
  host.texture_free(image);
  zutl::mounts() = saved;
}
}  // namespace
int main(int argc, char** argv) {
  const auto directory =
      std::filesystem::temp_directory_path() /
      std::filesystem::path("mw3-hud-check-" + std::to_string(SDL_GetPerformanceCounter()));
  try {
    artwork_checks(directory);
    drawing_checks();
    if (argc == 3 && std::string(argv[1]) == "--fonts")
      text_checks(argv[2]);
    else
      require(argc == 1, "usage: mw3hud_checks [--fonts DIR]");
    std::filesystem::remove_all(directory);
    std::puts(
        "HUD payload, identity, ordering, ownership, clipping, native shadow and FP checks passed");
    return 0;
  } catch (const std::exception& error) {
    std::filesystem::remove_all(directory);
    std::fprintf(stderr, "HUD check failed: %s\n", error.what());
    return 1;
  }
}
