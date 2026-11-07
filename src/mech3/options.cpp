#include "mech3/options.h"

#include "mech3/controls_cfg.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"

namespace mech3 {

namespace {
Options g_options;
OptionsHooks g_default_hooks;
OptionsHooks* g_hooks = &g_default_hooks;

uint32_t& word() { return g_options.game_options; }
void set_bit(uint32_t bit, bool on) { word() = on ? (word() | bit) : (word() & ~bit); }

// 0x00556fe0: a node's number as a float; a list stands for its first item.
bool node_float(const zrdr::Node* n, float* out) {
  if (!n) return false;
  if (n->type == zrdr::List && n->list[0].i > 1) n = &n->list[1];
  if (n->type == zrdr::Int) {
    *out = static_cast<float>(n->i);
    return true;
  }
  if (n->type == zrdr::Float) {
    *out = n->f;
    return true;
  }
  return false;
}
}  // namespace

Options& options() { return g_options; }
void set_options_hooks(OptionsHooks* hooks) { g_hooks = hooks ? hooks : &g_default_hooks; }
OptionsHooks& options_hooks() { return *g_hooks; }

// Mech3.exe 1.2 @ 0x0040d4c0
void select_hw(int32_t hw) {
  g_options.hw_card_flag = hw;
  g_options.hw = hw;
}
// Mech3.exe 1.2 @ 0x0040d4d0
int32_t hw_card_flag() { return g_options.hw_card_flag; }

// Mech3.exe 1.2 @ 0x0040cbf0
void set_hw_card_dev(int32_t dev) {
  g_options.hw_card_dev = dev;
  select_hw(dev >= 0 ? 1 : 0);
}
// Mech3.exe 1.2 @ 0x0040cc10
int32_t hw_card_dev() { return g_options.hw_card_dev; }

zrdr::Node* OptionsHooks::reader_load(const char* name) { return zrdr::load(name); }
void OptionsHooks::reader_free(zrdr::Node* n) { zrdr::free_node(n); }

// Mech3.exe 1.2 @ 0x0040cc20. The level is stored as given and clamped only to
// pick the preset; a complete (name f f f b b) entry in intensity.zrd overrides it.
void set_game_intensity(int32_t level) {
  g_options.game_intensity = level;
  struct Preset {
    const char* name;
    float f[3];
    int32_t b[2];
  };
  static const Preset kPresets[3] = {{"low", {0.5f, 1.2f, 0.7f}, {1, 1}},
                                     {"medium", {1.0f, 1.0f, 1.0f}, {0, 0}},
                                     {"high", {1.2f, 0.6f, 1.3f}, {0, 0}}};
  if (level < 0) level = 0;
  if (level > 2) level = 2;
  Preset cur = kPresets[level];
  if (zrdr::Node* root = g_hooks->reader_load("intensity.zrd")) {
    if (zrdr::Node* n = zrdr::find(root, cur.name)) {
      Preset tmp = kPresets[level];
      if (n->type == zrdr::List && n->list[0].i == 6 && node_float(&n->list[1], &tmp.f[0]) &&
          node_float(&n->list[2], &tmp.f[1]) && node_float(&n->list[3], &tmp.f[2]) &&
          zrdr::get_bool_int(&n->list[4], &tmp.b[0]) && zrdr::get_bool_int(&n->list[5], &tmp.b[1]))
        cur = tmp;
    }
    zrdr::free_node(root);
  }
  g_options.intensity[0] = cur.f[0];
  g_options.intensity[2] = cur.f[2];
  g_options.intensity[1] = cur.f[1];
  g_options.intensity_flag[0] = cur.b[0] != 0;
  g_options.intensity_flag[1] = cur.b[1] != 0;
}
// Mech3.exe 1.2 @ 0x0040cdf0
int32_t game_intensity() { return g_options.game_intensity; }

// Mech3.exe 1.2 @ 0x0040ce00
void set_game_options(uint32_t bits) { word() = bits; }

// Mech3.exe 1.2 @ 0x0040ce10: 0 and 1 use bit 0, 2 is bit 8; other values only
// reach the view.
void set_camera_mode(int32_t mode) {
  if (mode == 0) {
    word() &= ~kCamera1;
    word() &= ~kCamera2;
  } else if (mode == 1) {
    word() |= kCamera1;
  } else if (mode == 2) {
    word() &= ~kCamera1;
    word() |= kCamera2;
  }
  if (Handle view = g_hooks->player_view()) g_hooks->view_set_camera_mode(view, mode);
}
// Mech3.exe 1.2 @ 0x0040ce70
int32_t camera_mode() {
  if (word() & kCamera1) return 1;
  return static_cast<int32_t>((word() >> 7) & 2);
}

// Mech3.exe 1.2 @ 0x0040ce90; it reads the old value (0x0040cec0) and ignores it.
void set_fast_menu(int32_t on) {
  fast_menu();
  set_bit(kFastMenu, on != 0);
}
// Mech3.exe 1.2 @ 0x0040cec0
int32_t fast_menu() { return static_cast<int32_t>((word() >> 9) & 1); }

// Mech3.exe 1.2 @ 0x0040ced0
void set_invincible(int32_t on) {
  set_bit(kInvincible, on != 0);
  if (on) g_options.cheated = 1;
  if (Handle mech = g_hooks->player_mech())
    g_hooks->mech_set_invincible(mech, on != 0 && !g_options.multiplayer ? 1 : 0);
}
// Mech3.exe 1.2 @ 0x0040cf20
int32_t invincible() { return static_cast<int32_t>((word() >> 1) & 1); }

// Mech3.exe 1.2 @ 0x0040cf30
void set_infinite_ammo(int32_t on) {
  set_bit(kInfiniteAmmo, on != 0);
  if (on) g_options.cheated = 1;
  if (g_options.multiplayer) return;
  if (Handle mech = g_hooks->player_mech())
    g_hooks->weapons_set_infinite_ammo(g_hooks->mech_weapons(mech), on == 1 ? 1 : 0);
}
// Mech3.exe 1.2 @ 0x0040cf80
int32_t infinite_ammo() { return static_cast<int32_t>((word() >> 2) & 1); }

// Mech3.exe 1.2 @ 0x0040cf90: turning heat off is the cheat.
void set_heat(int32_t on) {
  set_bit(kHeat, on != 0);
  if (!on) g_options.cheated = 1;
}
// Mech3.exe 1.2 @ 0x0040cfc0
int32_t heat() { return static_cast<int32_t>((word() >> 3) & 1); }

// Mech3.exe 1.2 @ 0x0040cfd0
void set_torso_auto_return(int32_t on) {
  set_bit(kTorsoAutoReturn, on != 0);
  if (Handle mech = g_hooks->player_mech())
    g_hooks->mech_set_torso_auto_return(mech, on == 1 ? 1 : 0);
}
// Mech3.exe 1.2 @ 0x0040d010
int32_t torso_auto_return() { return static_cast<int32_t>((word() >> 4) & 1); }

// Mech3.exe 1.2 @ 0x0040d020: the mech's flag is the inverse (throttle decays).
void set_throttle_const(int32_t on) {
  set_bit(kThrottleConst, on != 0);
  if (Handle mech = g_hooks->player_mech()) g_hooks->mech_set_throttle_decay(mech, on == 0 ? 1 : 0);
}
// Mech3.exe 1.2 @ 0x0040d060
int32_t throttle_const() { return static_cast<int32_t>((word() >> 5) & 1); }

// Mech3.exe 1.2 @ 0x0040d070: when the axis table is ready, switching rewrites it
// for joystick or mouse, then the mech's control mode follows (6 joystick, 3 not).
void set_use_joystick(int32_t on) {
  AxisTable& t = axis_table();
  if (on) {
    word() |= kUseJoystick;
    if (t.ready) {
      t.set3(0, 3, -0.5f, 0.5f);
      t.axes[0].deadzone = 0.05f;
      t.ready = 0;
      t.set3(1, 6, -1.0f, 0.0f);
      t.axes[1].deadzone = 0.0f;
      t.ready = 0;
      t.set(2, 2, 1.0f, 0.0f, 0.0f);
      t.set(3, 1, -1.0f, 0.0f, 0.0f);
      t.ready = 1;
    }
  } else {
    word() &= ~kUseJoystick;
    if (t.ready) {
      t.set3(2, 10, 1.0f, 0.0f);
      t.axes[2].deadzone = 0.0f;
      t.ready = 0;
      t.set3(1, 9, 1.0f, 0.0f);
      t.axes[1].deadzone = 0.0f;
      t.ready = 0;
      t.set_source(0, 0);
      t.set_source(3, 0);
      t.ready = 1;
    }
  }
  if (Handle mech = g_hooks->player_mech())
    g_hooks->mech_set_jump_jet_mode(mech, use_joystick() == 1 ? 6 : 3);
}
// Mech3.exe 1.2 @ 0x0040d1b0
int32_t use_joystick() { return static_cast<int32_t>((word() >> 6) & 1); }

// Mech3.exe 1.2 @ 0x0040d1c0
void set_mw2_jump_jets(int32_t on) { set_bit(kMw2JumpJets, on != 0); }
// Mech3.exe 1.2 @ 0x0040d1e0
int32_t mw2_jump_jets() { return static_cast<int32_t>((word() >> 7) & 1); }

// Mech3.exe 1.2 @ 0x0040d1f0
void set_shadow_parts(int32_t v) {
  g_options.shadow_parts[g_options.hw ? 1 : 0] = v;
  g_hooks->shadows_set(v != 0 ? 1 : 0);
}
// Mech3.exe 1.2 @ 0x0040d220
int32_t shadow_parts() { return g_options.shadow_parts[g_options.hw ? 1 : 0]; }

// Mech3.exe 1.2 @ 0x0040d240: hardware only; forced off on a 3D card without
// capability 3.
void set_shadow(int32_t v) {
  if (!g_options.hw) return;
  g_options.shadow = v;
  if (g_hooks->video_mode() == 0) return;
  if (g_hooks->video_is_3d() != 0 && g_hooks->video_has_cap(3) == 0) v = 0;
  g_hooks->model_set_global_light(v);
}
// Mech3.exe 1.2 @ 0x0040d280
int32_t shadow() { return g_options.hw ? g_options.shadow : 0; }

// Mech3.exe 1.2 @ 0x0040d2a0: the effects module counts the other way.
void set_effects_level(int32_t v) {
  g_options.effects_level[g_options.hw ? 1 : 0] = v;
  if (v == 0) g_hooks->effects_set_level(2);
  else if (v == 1) g_hooks->effects_set_level(1);
  else if (v == 2) g_hooks->effects_set_level(0);
}
// Mech3.exe 1.2 @ 0x0040d2f0
int32_t effects_level() { return g_options.effects_level[g_options.hw ? 1 : 0]; }

// Mech3.exe 1.2 @ 0x0040d310: the camera's and the mechs' level-of-detail scales.
void set_object_lod(int32_t v) {
  float cam_scale = 1.0f, mech_scale = 1.0f;
  const Handle cam = camera();
  g_options.object_lod[g_options.hw ? 1 : 0] = v;
  if (v == 0) {
    cam_scale = 1.0f;
    mech_scale = 1.0f;
  } else if (v == 1) {
    cam_scale = 0.75f;
    mech_scale = 0.75f;
  } else if (v == 2) {
    cam_scale = 0.4f;
    mech_scale = 0.0f;
  }
  if (cam) g_hooks->camera_set_lod_scale(cam, cam_scale);
  g_hooks->mechs_set_lod_scale(mech_scale);
}
// Mech3.exe 1.2 @ 0x0040d3b0
int32_t object_lod() { return g_options.object_lod[g_options.hw ? 1 : 0]; }

// Mech3.exe 1.2 @ 0x0040d3d0 / 0x0040d3e0
void set_sound_lod(int32_t v) { g_options.sound_lod = v; }
int32_t sound_lod() { return g_options.sound_lod; }

// Mech3.exe 1.2 @ 0x0040d3f0 / 0x0040d410
void set_texture_memory(int32_t v) { g_options.texture_memory[g_options.hw ? 1 : 0] = v; }
int32_t texture_memory() { return g_options.texture_memory[g_options.hw ? 1 : 0]; }

// Mech3.exe 1.2 @ 0x0040d430: bit 4 turns the world's "sunlight" node on.
void set_gfx_flags(int32_t v) {
  g_options.gfx_flags[g_options.hw ? 1 : 0] = v;
  if (Handle sun = g_hooks->find_node(6, "sunlight")) g_hooks->node_set_active(sun, (v & 0x10) ? 1 : 0);
}
// Mech3.exe 1.2 @ 0x0040d480
int32_t gfx_flags() { return g_options.gfx_flags[g_options.hw ? 1 : 0]; }

// Mech3.exe 1.2 @ 0x0040d4a0 / 0x0040d4b0
void set_pixel_double(int32_t on) { g_options.pixel_double = on; }
int32_t pixel_double() { return g_options.pixel_double; }

// Mech3.exe 1.2 @ 0x0040d540
void clamp_to(const Rect& r, float p[2]) {
  if (p[0] < static_cast<float>(r.x)) p[0] = static_cast<float>(r.x);
  else if (static_cast<float>(r.x_last) < p[0]) p[0] = static_cast<float>(r.x_last);
  if (p[1] < static_cast<float>(r.y)) p[1] = static_cast<float>(r.y);
  else if (static_cast<float>(r.y_last) < p[1]) p[1] = static_cast<float>(r.y_last);
}

// Mech3.exe 1.2 @ 0x0040d9e0: with pixels doubled (VMode 3) the point is
// halved into the render target and kept on it.
void screen_to_render(float p[2]) {
  const Rect& rr = g_options.render;
  const Rect& dr = g_options.display;
  if (!pixel_double() && canvas_scaled()) {  // FIX (B): the canvas's point onto the frame (resolution.md step 4)
    p[0] = static_cast<float>(rr.x) + (p[0] - static_cast<float>(dr.x)) * static_cast<float>(rr.w) / static_cast<float>(dr.w);
    p[1] = static_cast<float>(rr.y) + (p[1] - static_cast<float>(dr.y)) * static_cast<float>(rr.h) / static_cast<float>(dr.h);
    clamp_to(rr, p);
    return;
  }
  if (!pixel_double()) return;
  p[0] = p[0] * 0.5f;  // 0x005bc458
  p[1] = static_cast<float>(rr.y) + (p[1] - static_cast<float>(dr.y)) * 0.5f;
  clamp_to(rr, p);
}

// Mech3.exe 1.2 @ 0x0040da30: with pixels doubled the point is doubled out
// into the window and kept on it (0x0040d9e0's inverse).
void render_to_screen(float p[2]) {
  const Rect& rr = g_options.render;
  const Rect& wr = g_options.window;
  if (!pixel_double() && canvas_scaled()) {  // FIX (B): a frame point onto the canvas (the display)
    const Rect& dr = g_options.display;
    p[0] = static_cast<float>(dr.x) + (p[0] - static_cast<float>(rr.x)) * static_cast<float>(dr.w) / static_cast<float>(rr.w);
    p[1] = static_cast<float>(dr.y) + (p[1] - static_cast<float>(rr.y)) * static_cast<float>(dr.h) / static_cast<float>(rr.h);
    clamp_to(dr, p);
    return;
  }
  if (!pixel_double()) return;
  p[0] = p[0] + p[0];
  const float d = p[1] - static_cast<float>(rr.y);
  p[1] = static_cast<float>(wr.y) + (d + d);
  clamp_to(wr, p);
}

// Mech3.exe 1.2 @ 0x0040d4e0
void rect_set_origin(Rect& r, int32_t x, int32_t y) {
  r.x = x;
  r.y = y;
  r.x_end = r.w + x;
  r.y_end = r.h + y;
  r.x_last = r.x_end - 1;
  r.y_last = r.y_end - 1;
}
// Mech3.exe 1.2 @ 0x0040d510
void rect_set_size(Rect& r, int32_t w, int32_t h) {
  r.w = w;
  r.h = h;
  r.x_end = r.x + w;
  r.y_end = r.y + h;
  r.x_last = r.x_end - 1;
  r.y_last = r.y_end - 1;
}

// FIX (B) (resolution.md section 3): the horizontal angle for a w x h view
// that is not 4:3, keeping the vertical one and the original's 4:3 look
// (the linear rule's 4:3 angle, about 9% wider than square pixels).
float wide_fov(float vfov, int32_t w, int32_t h) {
  const double t = std::tan(double{vfov} * 0.5);
  const double k = std::tan(double{vfov} * (2.0 / 3.0)) / (t * (4.0 / 3.0));
  return static_cast<float>(2.0 * std::atan(t * static_cast<double>(w) / static_cast<double>(h) * k));
}

// Mech3.exe 1.2 @ 0x0040d590: the horizontal field of view follows the vertical
// one and the render size's aspect.
void set_camera(Handle cam) {
  g_options.camera.camera = cam;
  if (!cam) return;
  float fx = 0, fy = 0;
  g_hooks->camera_get_fov(cam, &fx, &fy);
  const Rect& r = g_options.render;
  float x = static_cast<float>(static_cast<double>(r.w) * fy / static_cast<double>(r.h));
  if (zfix::options().any_resolution && r.w * 3 != r.h * 4 && r.h > 0) x = wide_fov(fy, r.w, r.h);
  g_hooks->camera_set_fov(g_options.camera.camera, x, fy);
}
// Mech3.exe 1.2 @ 0x0040d5e0
Handle camera() { return g_options.camera.camera; }

// Mech3.exe 1.2 @ 0x0040d600
void set_render_size(int32_t w, int32_t h) {
  Rect& r = g_options.render;
  rect_set_size(r, w, h);
  if (r.target) g_hooks->window_set_size(r.target, r.w, r.h);
}
// Mech3.exe 1.2 @ 0x0040d630
void set_render_origin(int32_t x, int32_t y) {
  Rect& r = g_options.render;
  rect_set_origin(r, x, y);
  if (r.target) {
    g_hooks->window_set_size(r.target, r.w, r.h);
    g_hooks->window_set_origin(r.target, r.x, r.y);
  }
}
// Mech3.exe 1.2 @ 0x0040d670
void set_render_target(Handle window) {
  Rect& r = g_options.render;
  r.target = window;
  if (window) {
    g_hooks->window_set_size(r.target, r.w, r.h);
    g_hooks->window_set_origin(r.target, r.x, r.y);
  }
}
// Mech3.exe 1.2 @ 0x0040d6a0
Rect& render() { return g_options.render; }

// Mech3.exe 1.2 @ 0x0040d6b0
void set_display_target(Handle d) {
  Rect& r = g_options.display;
  r.target = d;
  if (d) {
    g_hooks->display_set_size(r.target, r.w, r.h);
    g_hooks->display_set_origin(r.target, r.x, r.y);
  }
}
// Mech3.exe 1.2 @ 0x0040d6e0
void set_display_origin(int32_t x, int32_t y) {
  Rect& r = g_options.display;
  rect_set_origin(r, x, y);
  if (r.target) {
    g_hooks->display_set_size(r.target, r.w, r.h);
    g_hooks->display_set_origin(r.target, r.x, r.y);
  }
}
// Mech3.exe 1.2 @ 0x0040d720
void set_display_size(int32_t w, int32_t h) {
  Rect& r = g_options.display;
  rect_set_size(r, w, h);
  if (r.target) g_hooks->display_set_size(r.target, r.w, r.h);
}
// Mech3.exe 1.2 @ 0x0040d750 .. 0x0040d790
Rect& display() { return g_options.display; }
int32_t display_w() { return g_options.display.w; }
int32_t display_h() { return g_options.display.h; }
void set_display_depth(int32_t bpp) { g_options.display.depth = bpp; }
int32_t display_depth() { return g_options.display.depth; }
// Mech3.exe 1.2 @ 0x0040d7a0 .. 0x0040d7f0
int32_t vstride() { return g_options.vstride; }
Rect& window() { return g_options.window; }
int32_t window_h() { return g_options.window.h; }
void set_window_size(int32_t w, int32_t h) { rect_set_size(g_options.window, w, h); }
void set_window_origin(int32_t x, int32_t y) { rect_set_origin(g_options.window, x, y); }

// Mech3.exe 1.2 @ 0x0040d810: the screen modes. Mode 3 draws 320x240 and doubles
// it to 640x480; mode 4 keeps the sizes it had; 5..10 are 640x480 to 1600x1200.
void set_vmode(int32_t mode) {
  g_options.vmode = mode;
  set_render_origin(0, 0);
  set_window_origin(0, 0);
  set_display_origin(0, 0);
  set_display_depth(16);
  int32_t w = 0, h = 0;
  if (mode == 3) {
    set_render_size(320, 240);
    set_window_size(640, 480);
    set_display_size(640, 480);
    set_pixel_double(1);
  } else if (vmode_size(mode, &w, &h)) {
    // FIX (B): past 10 the 2D (the display) is the canvas, the 3D (render and
    // the window node) the whole frame (resolution.md step 4); else all three
    // the mode's size.
    int32_t cw = w, ch = h;
    vmode_canvas(mode, &cw, &ch);
    set_render_size(w, h);
    set_window_size(w, h);
    set_display_size(cw, ch);
    set_pixel_double(0);
  }
  g_hooks->input_set_screen(display_w(), display_h());
}

bool vmode_size(int32_t mode, int32_t* w, int32_t* h) {
  static const int32_t kSizes[][2] = {
      {640, 480},   {800, 600},   {1024, 768},  {1152, 864},
      {1280, 1024}, {1600, 1200}, {1280, 720},  {1600, 900},
      {1920, 1080}, {2560, 1440}, {3840, 2160}, {0, 0},  // 16: custom, handled below
      {2560, 1080}, {3440, 1440}, {3840, 1080}, {5120, 1440}};
  if (mode == 3) {
    *w = 640, *h = 480;
    return true;
  }
  const int32_t last = zfix::options().any_resolution ? kLastVMode : 10;
  if (mode < 5 || mode > last) return false;
  if (mode == kCustomVMode) {  // FIX (B): the player's own size, kept inside the cap
    // The requested shape controls both camera FOV and HUD/input mapping.
    // Clamping dimensions independently would distort an oversized desktop;
    // use integer ratios to avoid leaking FP flags into native game code.
    int64_t width = std::max(g_options.screen_w, 640);
    int64_t height = std::max(g_options.screen_h, 480);
    if (width > kMaxScreen[0] || height > kMaxScreen[1]) {
      if (width * kMaxScreen[1] >= height * kMaxScreen[0]) {
        height = std::max<int64_t>(480, (height * kMaxScreen[0] + width / 2) / width);
        width = kMaxScreen[0];
      } else {
        width = std::max<int64_t>(640, (width * kMaxScreen[1] + height / 2) / height);
        height = kMaxScreen[1];
      }
    }
    *w = static_cast<int32_t>(width);
    *h = static_cast<int32_t>(height);
    return true;
  }
  *w = std::min(kSizes[mode - 5][0], kMaxScreen[0]);
  *h = std::min(kSizes[mode - 5][1], kMaxScreen[1]);
  return true;
}
// FIX (B), any_resolution (resolution.md step 4): the modes past 10 draw the
// 2D on a canvas 480 high and as wide as the frame's shape, scaled onto the
// frame by its height over 480 (1.5 at 720p, 2.25 at 1080p, 4.5 at 4K), so
// the 640x480 art covers the screen as it did at 640x480; the HUD is laid
// out on it by anchors.
float art_scale(int32_t frame_h) { return std::max(1.0f, static_cast<float>(frame_h) / 480.0f); }
bool vmode_canvas(int32_t mode, int32_t* w, int32_t* h) {
  int32_t fw = 0, fh = 0;
  if (!vmode_size(mode, &fw, &fh)) return false;
  *w = fw, *h = fh;
  if (!zfix::options().any_resolution || mode <= 10 || fh <= 480) return true;
  *w = (fw * 480 + fh / 2) / fh, *h = 480;
  return true;
}
bool canvas_scaled() {
  const Rect& rr = g_options.render;
  const Rect& dr = g_options.display;
  return zfix::options().any_resolution && dr.w > 0 && dr.h > 0 && (dr.w != rr.w || dr.h != rr.h);
}
void render_point_to_canvas(float* x, float* y) {
  if (!canvas_scaled()) return;
  const Rect& rr = g_options.render;
  const Rect& dr = g_options.display;
  *x = *x * static_cast<float>(dr.w) / static_cast<float>(rr.w);
  *y = *y * static_cast<float>(dr.h) / static_cast<float>(rr.h);
}
void canvas_point_to_render(float* x, float* y) {
  if (!canvas_scaled()) return;
  const Rect& rr = g_options.render;
  const Rect& dr = g_options.display;
  *x = *x * static_cast<float>(rr.w) / static_cast<float>(dr.w);
  *y = *y * static_cast<float>(rr.h) / static_cast<float>(dr.h);
}
void canvas_to_render(int32_t* x, int32_t* y, int32_t* w, int32_t* h) {
  if (!canvas_scaled()) return;
  const Rect& rr = g_options.render;
  const Rect& dr = g_options.display;
  const int32_t x1 = (*x + *w) * rr.w / dr.w, y1 = (*y + *h) * rr.h / dr.h;
  *x = *x * rr.w / dr.w, *y = *y * rr.h / dr.h;
  *w = x1 - *x, *h = y1 - *y;
}

// Mech3.exe 1.2 @ 0x0040d9d0
int32_t vmode() { return g_options.vmode; }

// Mech3.exe 1.2 @ 0x0040da80
void set_sound_frequency(float v) {
  g_options.sound_frequency = v;
  g_hooks->sound_set_volume(v);
}
// Mech3.exe 1.2 @ 0x0040daa0
float sound_frequency() { return g_options.sound_frequency; }

// Mech3.exe 1.2 @ 0x0040dab0: 0..1 to the 16-bit volume of both channels.
void set_cd_volume(float v) {
  const int32_t n = static_cast<int32_t>(static_cast<double>(v) * 65535.0);  // x87, truncated
  g_options.cd_volume = v;
  g_hooks->music_set_volume(n, n);
}
// Mech3.exe 1.2 @ 0x0040dae0
float cd_volume() { return g_options.cd_volume; }

// Mech3.exe 1.2 @ 0x0040cb30
void apply_all() {
  set_camera_mode(camera_mode());
  set_invincible(invincible());
  set_infinite_ammo(infinite_ammo());
  set_torso_auto_return(torso_auto_return());
  set_throttle_const(throttle_const());
  set_shadow_parts(shadow_parts());
  set_effects_level(effects_level());
  set_object_lod(object_lod());
  set_gfx_flags(gfx_flags());
  set_vmode(vmode());
  set_use_joystick(use_joystick());
  set_game_intensity(game_intensity());
  set_shadow(shadow());
  set_sound_frequency(sound_frequency());
  set_cd_volume(cd_volume());
}

// ---------------------------------------------------------------------------
// The settings loader (docs/settings.md).

namespace {
// detail.zrd's names for its values (0x005e0710, 30 pairs).
struct Named {
  const char* name;
  int32_t value;
};
constexpr Named kNamed[] = {
    {"TRUE", 1}, {"FALSE", 0}, {"HIGH", 0}, {"MEDIUM", 1}, {"LOW", 2},
    {"CPU_CLASS_8086", 0}, {"CPU_CLASS_80286", 2}, {"CPU_CLASS_80386", 3}, {"CPU_CLASS_80486", 4},
    {"CPU_CLASS_PENTIUM", 5}, {"CPU_CLASS_PENTIUM_PRO", 6}, {"CPU_CLASS_PENTIUM_NEWER", 7},
    {"TEXMEM_MAX", 0}, {"TEXMEM_12MB", 1}, {"TEXMEM_10MB", 2}, {"TEXMEM_8MB", 3}, {"TEXMEM_6MB", 4},
    {"TEXMEM_4MB", 5}, {"TEXMEM_2MB", 6},
    {"ZVID_320x200x16", 2}, {"ZVID_320x240x16", 3}, {"ZVID_640x400x16", 4}, {"ZVID_640x480x16", 5},
    {"ZVID_800x600x16", 6}, {"ZVID_1024x768x16", 7},
    {"SOUND_API_DSOUND", 0}, {"SOUND_API_A3D", 1},
    {"camAttach", 1}, {"camTether", 0}, {"camTetherZ", 2},
};

const char* str(const zrdr::Node& n) { return n.type == zrdr::String && n.s ? n.s : ""; }

// Mech3.exe 1.2 @ 0x0040bec0: a value's name, 0 when unknown.
int32_t named_value(const char* s) {
  for (const Named& n : kNamed)
    if (std::strcmp(n.name, s) == 0) return n.value;
  return 0;
}

// _ftol's low dword: truncated; 0 when out of the 64-bit range (its 0x8000...).
int32_t ftol32(float f) {
  if (!(f > -9.2233720368547758e18f && f < 9.2233720368547758e18f)) return 0;
  return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(f))));
}

// Mech3.exe 1.2 @ 0x0040bf20: a node's value as an int.
int32_t node_value(const zrdr::Node& n) {
  if (n.type == zrdr::Int) return n.i;
  if (n.type == zrdr::Float) return ftol32(n.f);
  if (n.type == zrdr::String) return named_value(n.s ? n.s : "");
  return 0;
}

// Mech3.exe 1.2 @ 0x0040bf50: the machine's value against detail.zrd's, by
// the operator's name; "~=" is within 2% of the machine's; unknown ones fail.
int32_t compare(const char* op, int32_t machine, int32_t value) {
  if (std::strcmp(op, "==") == 0) return machine == value;
  if (std::strcmp(op, "<") == 0) return machine < value;
  if (std::strcmp(op, ">") == 0) return machine > value;
  if (std::strcmp(op, "<=") == 0) return machine <= value;
  if (std::strcmp(op, ">=") == 0) return machine >= value;
  if (std::strcmp(op, "!=") == 0) return machine != value;
  if (std::strcmp(op, "~=") == 0) {
    int32_t d = static_cast<int32_t>(static_cast<uint32_t>(machine) - static_cast<uint32_t>(value));
    d = d < 0 ? static_cast<int32_t>(0u - static_cast<uint32_t>(d)) : d;  // cdq/xor/sub: INT_MIN stays
    return static_cast<double>(machine) * 0.02 > static_cast<double>(d);  // x87 at 53 bits
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0040c1a0: "DEFAULT", or (NAME OP VALUE) on the machine.
// An unknown NAME compares 0.
int32_t condition(const zrdr::Node& c) {
  if (c.type == zrdr::String) return std::strcmp(str(c), "DEFAULT") == 0;
  if (c.type != zrdr::List || c.list[0].i != 4) return 0;
  const char* name = str(c.list[1]);
  const char* op = str(c.list[2]);
  const int32_t value = node_value(c.list[3]);
  const zreg::Machine& m = g_options.machine;
  int32_t have = 0;
  if (std::strcmp(name, "CPU_CLASS") == 0) have = static_cast<int32_t>(m.cpu_class);
  else if (std::strcmp(name, "CPU_MHZ") == 0) have = static_cast<int32_t>(m.cpu_mhz);
  else if (std::strcmp(name, "VIDEO_KB") == 0) have = static_cast<int32_t>(m.unset20);  // a dword off: always 0
  else if (std::strcmp(name, "RAM_KB") == 0) have = static_cast<int32_t>(m.ram_kb);
  else if (std::strcmp(name, "HW_ACCEL") == 0) have = static_cast<int32_t>((m.flags >> 6) & 1);
  return compare(op, have, value);
}

zreg::Entry* make(const char* name, int32_t type, void* field, uint32_t size = 0, int32_t scope = zreg::kUser) {
  return zreg::create(name, type, size, scope, field);
}
}  // namespace

// Mech3.exe 1.2 @ 0x0040c3b0. Each item after the name is (CONDITION VALUE).
int32_t detail_value(zrdr::Node* root, const char* name, int32_t def) {
  if (!root) return def;
  zrdr::Node* n = zrdr::find(root, name);
  if (!n || n->type != zrdr::List) return def;  // not a list: the original reads it as one
  const int32_t count = n->list[0].i;
  for (int32_t i = 1; i < count; ++i) {
    const zrdr::Node& e = n->list[i];
    if (e.type != zrdr::List || e.list[0].i < 3) continue;  // the original assumes (cond value)
    if (condition(e.list[1])) return node_value(e.list[2]);
  }
  return def;
}

// Mech3.exe 1.2 @ 0x0040c420. Each option's entry is made (bound to its field),
// set through its setter from detail.zrd or the default, then the store reads
// the saved values over them (raw, not through the setters: InitInstance's
// apply-all 0x0040cb30 applies them later). With path null, defaults only.
int32_t load_options(const char* path) {
  Options& o = g_options;
  o.machine = zreg::Machine{};  // the 0x2d dwords at 0x005f4c98: the copy, hw, the entry pointers
  o.hw = 0;
  o.default_cmd_map = nullptr;
  zrdr::Node* root = nullptr;
  if (path && !(root = g_hooks->reader_load(path))) return 0;
  o.machine = zreg::machine();
  auto d = [&](const char* name, int32_t def) { return detail_value(root, name, def); };
  using zreg::kBinary4;
  using zreg::kDword;

  if (make("HWCardFlag", kDword, &o.hw_card_flag)) select_hw(static_cast<int32_t>(zreg::install_options(0xff)));
  if (make("HWCardDev", kDword, &o.hw_card_dev)) set_hw_card_dev(zreg::install_options(0xff) ? 999 : -1);
  if (make("TextureMemory_SW", kDword, &o.texture_memory[0])) o.hw = 0, set_texture_memory(d("TextureMemory_SW", 0));
  if (make("TextureMemory_HW", kDword, &o.texture_memory[1])) o.hw = 1, set_texture_memory(d("TextureMemory_HW", 0));
  for (int hw = 0; hw < 2; ++hw) {
    if (!make(hw ? "GfxFlags_HW" : "GfxFlags_SW", kDword, &o.gfx_flags[hw])) continue;
    int32_t f = (o.machine.flags & 1) ? 0x04 : 0;  // MMX
    o.hw = hw;
    if (d("Transparency", 1)) f |= 0x02;
    if (d("Lighting", 1)) f |= 0x01;
    if (d("Perspective", 1)) f |= 0x08;
    if (d(hw ? "GlobalLight_HW" : "GlobalLight_SW", hw)) f |= 0x10;
    if (d("AllVideoBuffer", 0)) f |= 0x10000;
    set_gfx_flags(f);
  }
  if (make("SoundLOD", kDword, &o.sound_lod)) set_sound_lod(d("SoundLOD", 0));
  if (make("ObjectLOD_SW", kDword, &o.object_lod[0])) o.hw = 0, set_object_lod(d("ObjectLOD_SW", 0));
  if (make("ObjectLOD_HW", kDword, &o.object_lod[1])) o.hw = 1, set_object_lod(d("ObjectLOD_HW", 0));
  if (make("EffectsLevel_SW", kDword, &o.effects_level[0])) o.hw = 0, set_effects_level(d("EffectsLevel_SW", 1));
  if (make("EffectsLevel_HW", kDword, &o.effects_level[1])) o.hw = 1, set_effects_level(d("EffectsLevel_HW", 0));
  if (make("ShadowParts_SW", kDword, &o.shadow_parts[0])) o.hw = 0, set_shadow_parts(d("ShadowParts_SW", 0));
  if (make("ShadowParts_HW", kDword, &o.shadow_parts[1])) o.hw = 1, set_shadow_parts(d("ShadowParts_HW", 0));
  if (make("Shadow", kDword, &o.shadow)) set_shadow(d("Shadow", 1));
  if (make("GameOptions", kDword, &o.game_options)) {
    set_game_options(0);
    set_camera_mode(d("CameraMode", 1));
    set_invincible(d("Invincible", 0));
    set_infinite_ammo(d("InfiniteAmmo", 0));
    set_heat(d("HeatMgmt", 1));
    set_torso_auto_return(d("TorsoAutoRtn", 0));
    set_throttle_const(d("ThrottleConst", 0));
    set_use_joystick(d("UseJoystick", 1));
    set_mw2_jump_jets(d("MW2JJets", 0));
    set_fast_menu(d("FastMenu", 0));
  }
  if (make("MouseAccelX", kBinary4, &o.mouse_accel[0])) o.mouse_accel[0] = 1.3f;
  if (make("MouseAccelY", kBinary4, &o.mouse_accel[1])) o.mouse_accel[1] = 1.3f;
  if (make("JoyAccelX", kBinary4, &o.joy_accel[0])) o.joy_accel[0] = 1.3f;
  if (make("JoyAccelY", kBinary4, &o.joy_accel[1])) o.joy_accel[1] = 1.3f;
  if (make("GameIntensity", kDword, &o.game_intensity)) set_game_intensity(1);
  if (make("MouseMode", kDword, &o.mouse_mode)) o.mouse_mode = 2;
  if (make("SoundVolume", kBinary4, &o.sound_frequency)) set_sound_frequency(0.5f);
  if (make("CDVolume", kBinary4, &o.cd_volume)) set_cd_volume(0.5f);
  // Not saved (scope 2): the stride, the camera and the three rectangles, VMode.
  if (make("VStride", kDword, &o.vstride, 0, zreg::kNotSaved)) o.vstride = 1;
  make("Camera", zreg::kBlock, &o.camera, sizeof o.camera, zreg::kNotSaved);
  make("Render", zreg::kBlock, &o.render, sizeof o.render, zreg::kNotSaved);
  make("Display", zreg::kBlock, &o.display, sizeof o.display, zreg::kNotSaved);
  make("Window", zreg::kBlock, &o.window, sizeof o.window, zreg::kNotSaved);
  if (zfix::options().any_resolution) {  // FIX (B): mode 16's size (resolution.md)
    if (make("ScreenWidth", kDword, &o.screen_w)) o.screen_w = 1920;
    if (make("ScreenHeight", kDword, &o.screen_h)) o.screen_h = 1080;
  }
  if (make("VMode", kDword, &o.vmode, 0, zreg::kNotSaved)) set_vmode(d("VMode", 5));
  // The mode a mission starts in: saved, and read as any number, so 8..10
  // (1152x864, 1280x1024, 1600x1200, which the menu does not offer) work.
  if (make("InGameVMode", kDword, &o.in_game_vmode)) {
    o.in_game_vmode = d("InGameVMode", 5);
    // FIX (B): a first start plays 16:9 (or the screen's shape) at the
    // desktop's size, mode 16; a saved mode or size wins (resolution.md).
    int32_t w = 0, h = 0;
    if (zfix::options().any_resolution && g_hooks->first_start_screen(&w, &h))
      o.in_game_vmode = kCustomVMode, o.screen_w = w, o.screen_h = h;
  }
  if (make("ActiveCtlCfg", 3, o.active_ctl_cfg, sizeof o.active_ctl_cfg))
    std::strncpy(o.active_ctl_cfg, g_hooks->message(0xfc0), sizeof o.active_ctl_cfg);  // "DefaultCtlConfig"
  g_hooks->controls_set_defaults();
  o.default_cmd_map = zreg::find("DefaultCmdMap");
  zreg::read_all();
  // FIX (bug): a saved name without its 0 ran past the buffer (fixes.md).
  const std::string ctl(o.active_ctl_cfg, strnlen(o.active_ctl_cfg, sizeof o.active_ctl_cfg));
  g_hooks->controls_load(ctl.c_str());
  // Handles of objects of the last run would be stale: cleared.
  o.camera.camera = 0;
  o.render.target = 0;
  o.display.target = 0;
  o.window.target = 0;
  if (root) g_hooks->reader_free(root);
  return 1;
}

// Mech3.exe 1.2 @ 0x0040cb20, from the app's destructor 0x0047fab0.
void save_options() {
  controls_write_settings();  // FIX (bug), W40: zreg::write_all with the kept map
  g_hooks->controls_free();
}

}  // namespace mech3
