// Any screen size and 16:9 (docs/resolution.md section 5): the modes' sizes,
// the device's range and RESOLUTION's list, and the camera's horizontal angle,
// with zfix's any_resolution on and off (off: the original's).
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "frontend/sdl/sdl_fit.h"
#include "gamez/zclass/camera.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "mech3/app_hooks.h"
#include "mech3/hud.h"
#include "mech3/options.h"
#include "mech3/options_menu.h"

namespace {

struct FixOn {
  bool was = zfix::options().any_resolution;
  explicit FixOn(bool on) { zfix::options().any_resolution = on; }
  ~FixOn() { zfix::options().any_resolution = was; }
};

// A camera with plain angles in place of the engine's: what 0x004f3ee0 reads
// and 0x004f3d90 writes.
struct FovHooks : mech3::OptionsHooks {
  float h = 1.3962634f, v = 1.0471976f;
  int sets = 0;
  void camera_get_fov(mech3::Handle, float* x, float* y) override { *x = h, *y = v; }
  void camera_set_fov(mech3::Handle, float x, float y) override { h = x, v = y, ++sets; }
  uint32_t unported(const char*, std::initializer_list<zgame::Arg>) override { return 0; }
};

float camera_h_at(int32_t w, int32_t h, float vfov = 1.0471976f) {
  FovHooks hooks;
  hooks.v = vfov;
  mech3::OptionsHooks& old = mech3::options_hooks();
  mech3::set_options_hooks(&hooks);
  mech3::set_render_size(w, h);
  mech3::set_camera(1);
  mech3::set_camera(0);
  mech3::set_options_hooks(&old);
  return hooks.v == vfov ? hooks.h : -1.0f;
}

// The original's 0x0040d590, as test_app_options.py checks it.
float linear(int32_t w, int32_t h, float vfov) {
  return static_cast<float>(static_cast<double>(w) * vfov / static_cast<double>(h));
}

// Horizontal over vertical scale of the projection (render.md section 0).
double squeeze(float hfov, float vfov, int32_t w, int32_t h) {
  return (w / std::tan(hfov * 0.5)) / (h / std::tan(vfov * 0.5));
}

struct RangeEngine : mech3::Engine {
  int32_t total = 4 << 20, used = 0;
  int32_t video_memory_total() override { return total; }
  int32_t video_memory_used() override { return used; }
  const char* message(int32_t id) override {
    static std::string s;
    s = "msg" + std::to_string(id);
    return s.c_str();
  }
  uint32_t unported(const char*, std::initializer_list<zgame::Arg>) override { return 0; }
};

}  // namespace

TEST(resolution_original_modes_keep_their_sizes) {
  for (bool fix : {false, true}) {
    FixOn f(fix);
    const int32_t sizes[][3] = {{5, 640, 480},   {6, 800, 600},   {7, 1024, 768},
                                {8, 1152, 864},  {9, 1280, 1024}, {10, 1600, 1200}};
    for (const auto& s : sizes) {
      mech3::set_vmode(s[0]);
      EXPECT(mech3::render().w == s[1] && mech3::render().h == s[2]);
      EXPECT(mech3::display().w == s[1] && mech3::display().h == s[2]);
      EXPECT(mech3::window().w == s[1] && mech3::window().h == s[2]);
    }
    mech3::set_vmode(3);  // 320x240 doubled
    EXPECT(mech3::render().w == 320 && mech3::display().w == 640);
    mech3::set_vmode(5);
    mech3::set_vmode(4);  // keeps the sizes
    EXPECT(mech3::display().w == 640 && mech3::display().h == 480);
  }
}

TEST(resolution_new_modes_only_with_the_fix) {
  {
    FixOn f(false);
    mech3::set_vmode(5);
    for (int32_t m = 11; m <= mech3::kLastVMode; ++m) {
      mech3::set_vmode(m);  // the exe: an unknown mode keeps the sizes
      EXPECT(mech3::display().w == 640 && mech3::display().h == 480);
    }
  }
  FixOn f(true);
  const int32_t sizes[][3] = {{11, 1280, 720},  {12, 1600, 900},  {13, 1920, 1080},
                              {14, 2560, 1440}, {15, 3840, 2160}, {17, 2560, 1080},
                              {18, 3440, 1440}, {19, 3840, 1080}, {20, 5120, 1440}};
  for (const auto& s : sizes) {
    mech3::set_vmode(s[0]);
    EXPECT(mech3::render().w == s[1] && mech3::render().h == s[2]);
    int32_t cw = 0, ch = 0;  // the display: the 2D canvas over it (step 4, resolution_canvas_past_mode_10)
    EXPECT(mech3::vmode_canvas(s[0], &cw, &ch) && mech3::display().w == cw && mech3::display().h == ch);
  }
  mech3::Options& o = mech3::options();
  const int32_t keep_w = o.screen_w, keep_h = o.screen_h;
  // Custom modes keep the full requested frame, including 21:9 and 32:9.
  const int32_t shapes[][4] = {{2560, 1080, 2560, 1080}, {3440, 1440, 3440, 1440},
                               {3840, 1080, 3840, 1080}, {5120, 1440, 5120, 1440},
                               {7680, 2160, 7680, 2160}, {1280, 1024, 1280, 1024},
                               {1920, 1080, 1920, 1080}};
  for (const auto& s : shapes) {
    o.screen_w = s[0], o.screen_h = s[1];
    mech3::set_vmode(mech3::kCustomVMode);
    EXPECT(mech3::render().w == s[2] && mech3::render().h == s[3]);
  }
  o.screen_w = 2560, o.screen_h = 1080;
  mech3::set_vmode(mech3::kCustomVMode);
  EXPECT(mech3::display().w == 1138 && mech3::display().h == 480);  // canvas at native aspect
  o.screen_w = 10000, o.screen_h = 100;  // invalid short height is bounded; no 16:9 crop
  mech3::set_vmode(mech3::kCustomVMode);
  EXPECT(mech3::render().w == 7680 && mech3::display().w == 7680 && mech3::display().h == 480);
  o.screen_w = keep_w, o.screen_h = keep_h;
  mech3::set_vmode(5);
}

TEST(resolution_range_and_list) {
  RangeEngine e;
  int32_t lo = 0, hi = 0;
  {
    FixOn f(false);
    e.video_mode_range(0, &lo, &hi);  // 4 MB free: 1024x768 cut to 800x600
    EXPECT(lo == 5 && hi == 6);
    e.video_mode_range(-1, &lo, &hi);
    EXPECT(lo == 3 && hi == 5);
  }
  FixOn f(true);
  e.video_mode_range(0, &lo, &hi);
  EXPECT(lo == 5 && hi == mech3::kLastVMode);
  e.video_mode_range(-1, &lo, &hi);  // software stays as it was
  EXPECT(lo == 3 && hi == 5);

  mech3::ResolutionCycle list(e);
  list.rebuild(0);
  std::vector<std::string> labels;
  for (auto* it : list.items) labels.push_back(it->text ? it->text : "");
  EXPECT(list.count() == 16);                                                      // 5..20
  EXPECT(list.count() == 16 && labels[3] == "msg4058" && labels[5] == "msg4060");  // 8..10 by name
  EXPECT(list.count() == 16 && labels[8] == "1920x1080" && list.value_at(8) == 13);
  EXPECT(list.count() == 16 && list.value_at(11) == mech3::kCustomVMode);
  EXPECT(list.count() == 16 && list.value_at(15) == 20 && labels[15] == "5120x1440");
  list.rebuild(-1);
  EXPECT(list.count() == 2);  // software: 3 and 5
}

TEST(resolution_ultrawide_bounds_preserve_valid_aspects) {
  FixOn enabled(true);
  const auto saved = mech3::options();
  // Oversized 16:9, 32:9 and square requests fit uniformly. Extreme inputs
  // exercise bounded integer arithmetic rather than overflow or allocation.
  const int32_t cases[][4] = {{5120, 2880, 3840, 2160},
                              {10240, 2880, 7680, 2160},
                              {2147483647, 2147483647, 2160, 2160},
                              {-2147483647, -2147483647, 640, 480}};
  for (const auto& size : cases) {
    mech3::options().screen_w = size[0];
    mech3::options().screen_h = size[1];
    int32_t width = 0, height = 0;
    EXPECT(mech3::vmode_size(mech3::kCustomVMode, &width, &height));
    EXPECT(width == size[2] && height == size[3]);
  }
  mech3::options() = saved;
}

TEST(resolution_ultrawide_hud_and_pointer_share_the_frame) {
  FixOn enabled(true);
  const auto saved = mech3::options();
  for (const auto& size : {std::array<int32_t, 2>{3440, 1440}, {5120, 1440}, {3840, 1080}}) {
    mech3::options().screen_w = size[0];
    mech3::options().screen_h = size[1];
    mech3::set_vmode(mech3::kCustomVMode);
    const auto& canvas = mech3::display();
    const sdlfe::FrameShape shape{size[0], size[1], true, canvas.w, canvas.h};
    const auto fit = sdlfe::fit_rect(float(size[0]), float(size[1]), shape);
    EXPECT(fit.x == 0 && fit.y == 0 && fit.w == size[0] && fit.h == size[1]);
    for (float fraction : {.1f, .5f, .9f}) {
      float x = 0, y = 0, logical_x = 0, logical_y = 0;
      sdlfe::frame_to_window(canvas.w * fraction, canvas.h * fraction, float(size[0]),
                             float(size[1]), shape, &x, &y);
      EXPECT(std::fabs(x - size[0] * fraction) < .001f);
      EXPECT(std::fabs(y - size[1] * fraction) < .001f);
      sdlfe::window_to_frame(x, y, float(size[0]), float(size[1]), shape, &logical_x, &logical_y);
      EXPECT(std::fabs(logical_x - canvas.w * fraction) < .001f);
      EXPECT(std::fabs(logical_y - canvas.h * fraction) < .001f);
    }
    int32_t right[] = {550, 380}, left[] = {20, 380}, centre[] = {280, 10};
    EXPECT(mech3::hud_anchor("MFD", "position", canvas.w, canvas.h, right));
    EXPECT(right[0] == canvas.w - 90 && right[1] == 380);
    EXPECT(mech3::hud_anchor("TARGET", "position", canvas.w, canvas.h, left));
    EXPECT(left[0] == 20 && left[1] == 380);
    EXPECT(mech3::hud_anchor("COMPASS", "tape", canvas.w, canvas.h, centre));
    EXPECT(centre[0] == 280 + (canvas.w - 640) / 2);
    float cursor[] = {canvas.w * .5f, canvas.h * .5f};
    mech3::screen_to_render(cursor);
    EXPECT(std::fabs(cursor[0] - size[0] * .5f) < .001f && cursor[1] == size[1] * .5f);
    mech3::render_to_screen(cursor);
    EXPECT(std::fabs(cursor[0] - canvas.w * .5f) < .001f && cursor[1] == 240);
  }
  mech3::options() = saved;
}

TEST(resolution_camera_4_3_is_the_original) {
  for (bool fix : {false, true}) {
    FixOn f(fix);
    const int32_t shapes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1600, 1200}, {320, 240}};
    for (const auto& s : shapes)
      for (float v : {1.0471976f, 0.7f, 1.2f}) EXPECT(camera_h_at(s[0], s[1], v) == linear(s[0], s[1], v));
  }
}

TEST(resolution_camera_wide_keeps_the_4_3_look) {
  const float v = 1.0471976f;
  const double look = squeeze(linear(640, 480, v), v, 640, 480);  // about 0.9175
  EXPECT(std::fabs(look - 0.9175) < 1e-3);
  {
    FixOn f(false);  // the original's linear rule squeezes 16:9 further
    EXPECT(camera_h_at(1920, 1080) == linear(1920, 1080, v));
    EXPECT(squeeze(camera_h_at(1920, 1080), v, 1920, 1080) < 0.8);
  }
  FixOn f(true);
  const int32_t shapes[][2] = {{1920, 1080}, {1280, 720},  {2560, 1080}, {3440, 1440},
                               {5120, 1440}, {7680, 2160}, {1280, 1024}, {3840, 2160}};
  for (const auto& s : shapes) {
    const float hf = camera_h_at(s[0], s[1]);
    EXPECT(std::fabs(squeeze(hf, v, s[0], s[1]) - look) < 1e-5);
  }
  EXPECT(std::fabs(camera_h_at(1920, 1080) * 57.29578 - 96.4) < 0.1);  // degrees across
  mech3::set_render_size(640, 480);
}

// The wired hooks' targets: 0x004f3ee0 reads what 0x004f3d90 set.
TEST(resolution_camera_angles_on_a_node) {
  zcls::Node node{};
  zcls::Camera cam{};
  node.data = &cam;
  node.put<int32_t>(0x34, 1);
  cam.put(0xd8, 1.0f), cam.put(0xdc, 1.0f);
  EXPECT(zcls::camera_set_fov_angles(&node, 1.6825f, 1.0471976f) == 0);
  float h = 0, v = 0;
  EXPECT(zcls::camera_get_fov_angles(&node, &h, &v) == 0);
  EXPECT(h == 1.6825f && v == 1.0471976f);
  EXPECT(cam.get<float>(0xe8) == 1.6825f);  // the angles setter does not cap at 80 degrees
  node.put<int32_t>(0x34, 5);
  EXPECT(zcls::camera_get_fov_angles(&node, &h, &v) == 3);  // not a camera
}

// The HUD by anchors (resolution.md step 4, hud.md section 5): V640x480's
// positions, each moved by its piece's anchor, give every larger block
// hud.zrd ships, piece for piece; sizes and widths have no anchor. 1.0's
// hud.zrd has only V800x600 and V1024x768 of the five (1.2 adds the other
// three; assets.md, Data versions); the blocks a version lacks are skipped.
TEST(resolution_hud_anchors_make_the_original_blocks) {
  const std::string zbd = std::string(MW3_GAME_DIR) + "/zbd/reader.zbd";
  if (!std::filesystem::exists(zbd)) return;
  zrdr::init();
  zrdr::close_archives(true);
  EXPECT(zrdr::open_archive(zbd.c_str(), true));
  zrdr::Node* hud = zrdr::load("hud.zrd");
  EXPECT(hud != nullptr);
  if (!hud) return;
  zrdr::Node* base = zrdr::find(hud, "V640x480");
  struct Block { const char* name; int32_t w, h; };
  const Block blocks[] = {{"V800x600", 800, 600}, {"V1024x768", 1024, 768}, {"V1152x864", 1152, 864},
                          {"V1280x1024", 1280, 1024}, {"V1600x1200", 1600, 1200}};
  const char* keys[][2] = {{"MFD", "position"}, {"TARGET", "position"}, {"WEAPONS", "position"},
                           {"EQUIPMENT", "position"}, {"COMPASS", "tape"}, {"COMPASS", "frame"},
                           {"TIMER", "position"}, {"GAUGES", "speed_frame"}, {"GAUGES", "fuel_frame"},
                           {"GAUGES", "heat_bar"}, {"GAUGES", "heat_meter"}, {"GAUGES", "heat_text"},
                           {"GAUGES", "heat_arrow"}, {"GAUGES", "coolant_frame"}};
  int same = 0, compared = 0, found = 0;
  for (const Block& b : blocks) {
    zrdr::Node* blk = zrdr::find(hud, b.name);
    if (!blk) {
      std::printf("hud anchors: no %s in this data\n", b.name);
      continue;
    }
    ++found;
    for (const auto& k : keys) {
      int32_t p[2], want[2];
      if (!zrdr::get_ivec2(zrdr::find(base, k[0]), k[1], p) || !zrdr::get_ivec2(zrdr::find(blk, k[0]), k[1], want))
        continue;
      ++compared;
      EXPECT(mech3::hud_anchor(k[0], k[1], b.w, b.h, p));
      if (p[0] == want[0] && p[1] == want[1]) ++same;
      else std::printf("anchor %s %s at %s: %d %d, the block %d %d\n", k[0], k[1], b.name, p[0], p[1], want[0], want[1]);
    }
  }
  std::printf("hud anchors: %d of %d positions in the %d larger blocks made from V640x480\n", same, compared, found);
  EXPECT(found >= 2 && compared == 14 * found && same == compared);
  int32_t q[2] = {75, 100};
  EXPECT(!mech3::hud_anchor("MFD", "size", 1920, 1080, q) && q[0] == 75 && q[1] == 100);
  zrdr::free_node(hud);
  zrdr::close_archives(true);
}

// The canvas past mode 10 (resolution.md step 4): 480 high and the frame's
// shape (the art scale its height over 480), the display rect the canvas and the render and
// window rects the frame, the points and rects between them; modes 3..10 and
// the fix off as the original.
TEST(resolution_canvas_past_mode_10) {
  const mech3::Options keep = mech3::options();
  {
    FixOn on(true);
    const int32_t sizes[][3] = {{5, 640, 480},   {10, 1600, 1200}, {11, 853, 480}, {12, 853, 480},
                                {13, 853, 480},  {14, 853, 480},   {15, 853, 480}, {17, 1138, 480},
                                {18, 1147, 480}, {19, 1707, 480},  {20, 1707, 480}};
    for (const auto& s : sizes) {
      int32_t w = 0, h = 0;
      EXPECT(mech3::vmode_canvas(s[0], &w, &h) && w == s[1] && h == s[2]);
    }
    mech3::set_vmode(13);
    EXPECT(mech3::render().w == 1920 && mech3::render().h == 1080);
    EXPECT(mech3::window().w == 1920 && mech3::window().h == 1080);
    EXPECT(mech3::display().w == 853 && mech3::display().h == 480);
    EXPECT(mech3::canvas_scaled());
    float p[2] = {426.5f, 240};
    mech3::screen_to_render(p);
    EXPECT(p[0] == 960 && p[1] == 540);
    mech3::render_to_screen(p);
    EXPECT(p[0] == 426.5f && p[1] == 240);
    int32_t x = 640, y = 240, w = 64, h = 48;  // whole frame pixels, the far edge rounded down
    mech3::canvas_to_render(&x, &y, &w, &h);
    EXPECT(x == 1440 && y == 540 && w == 144 && h == 108);
    mech3::set_vmode(5);
    EXPECT(!mech3::canvas_scaled());
    float q[2] = {100, 100};
    mech3::screen_to_render(q);
    EXPECT(q[0] == 100 && q[1] == 100);
    mech3::set_vmode(3);  // the original's doubling stays
    mech3::screen_to_render(q);
    EXPECT(q[0] == 50 && q[1] == 50);
  }
  {
    FixOn off(false);
    int32_t w = 0, h = 0;
    EXPECT(!mech3::vmode_canvas(13, &w, &h));
    mech3::set_vmode(7);
    EXPECT(mech3::display().w == 1024 && mech3::render().w == 1024 && !mech3::canvas_scaled());
  }
  mech3::options() = keep;
}
