#pragma once
#include <cstdint>

#include "gamez/zgame/unported.h"
#include "gamez/zutil/zutl_reg.h"
#include "mech3/controls_cfg.h"

namespace zrdr { struct Node; }

// The game's options: detail settings, gameplay switches and screen modes.
// Original file unknown: between mech3\briefing.cpp and mech3\mech.cpp, with
// the detail.zrd loader 0x0040c420 (docs/settings.md). In the original each
// option is a settings store entry (0x00597d90) reached through a pointer at
// 0x005f4cc8..0x005f4d4c; here each is a field the entry points at, named after
// its registry value (docs/options.md). Getters and setters keep the original's
// side effects.
namespace mech3 {

using zgame::Handle;

// A Render/Display/Window entry (0x28 bytes): a rectangle, kept in both forms.
struct Rect {
  int32_t x = 0, y = 0;          // +0x00 +0x04
  int32_t x_end = 0, y_end = 0;  // +0x08 +0x0c  x + w, y + h
  int32_t w = 0, h = 0;          // +0x10 +0x14
  int32_t x_last = 0, y_last = 0;  // +0x18 +0x1c  x + w - 1, y + h - 1
  int32_t depth = 0;             // +0x20  (Display: bits per pixel, read by 0x0040d790)
  Handle target = 0;             // +0x24  the zClass window or display node that follows it
};

// The Camera entry (0xc bytes).
struct CameraEntry {
  uint32_t unknown0 = 0, unknown4 = 0;
  Handle camera = 0;             // +0x08  the player's camera node
};

struct Options {
  zreg::Machine machine;              // 0x005f4c98  the loader's copy (0x00597fa0)
  zreg::Entry* default_cmd_map = nullptr;  // [0x005f4d28] zInput's bindings' entry
  // Defaults: the detail.zrd loader's own (0x0040c420) when nothing overrides.
  int32_t hw = 0;                     // 0x005f4cc8  which _SW/_HW entry applies (HWCardFlag)
  int32_t hw_card_flag = 0;           // [0x005f4ccc] HWCardFlag
  int32_t hw_card_dev = -1;           // [0x005f4cd0] HWCardDev
  int32_t texture_memory[2] = {};     // [0x005f4cd4] TextureMemory_SW, [0x005f4cd8] _HW
  int32_t gfx_flags[2] = {0x0b, 0x1b};  // [0x005f4cdc] GfxFlags_SW, [0x005f4ce0] _HW
  int32_t object_lod[2] = {};         // [0x005f4ce4] ObjectLOD_SW, [0x005f4ce8] _HW
  int32_t effects_level[2] = {1, 0};  // [0x005f4cec] EffectsLevel_SW, [0x005f4cf0] _HW
  int32_t shadow_parts[2] = {};       // [0x005f4cf4] ShadowParts_SW, [0x005f4cf8] _HW
  int32_t shadow = 1;                 // [0x005f4cfc] Shadow (hardware only)
  int32_t sound_lod = 0;              // [0x005f4d00] SoundLOD
  uint32_t game_options = 0x49;       // [0x005f4d04] GameOptions: camera 1, heat, joystick
  int32_t game_intensity = 1;         // [0x005f4d08] GameIntensity 0 low, 1 medium, 2 high
  int32_t vmode = 5;                  // [0x005f4d0c] VMode
  int32_t in_game_vmode = 5;          // [0x005f4d10] InGameVMode
  int32_t vstride = 1;                // [0x005f4d14] VStride
  // FIX (B) (resolution.md): mode 16's size, saved as ScreenWidth / ScreenHeight.
  int32_t screen_w = 1920, screen_h = 1080;
  CameraEntry camera;                 // [0x005f4d18] Camera
  Rect render, display, window;       // [0x005f4d1c] Render, [0x005f4d20] Display, [0x005f4d24] Window
  float mouse_accel[2] = {1.3f, 1.3f};  // [0x005f4d2c] MouseAccelX, [0x005f4d30] Y
  float joy_accel[2] = {1.3f, 1.3f};    // [0x005f4d34] JoyAccelX, [0x005f4d38] Y
  float sound_frequency = 0.5f;          // [0x005f4d3c] SoundVolume
  char active_ctl_cfg[0x104] = {};    // [0x005f4d40] ActiveCtlCfg
  int32_t mouse_mode = 2;             // [0x005f4d44] MouseMode
  float cd_volume = 0.5f;             // [0x005f4d48] CDVolume
  int32_t pixel_double = 0;           // 0x005f4d4c  VMode 3: 320x240 drawn at 640x480

  // GameIntensity's preset (0x0040cc20, from intensity.zrd).
  float intensity[3] = {1, 1, 1};     // 0x00608e54 0x00608e58 0x00608e5c (the app's +0x1cc..)
  uint8_t intensity_flag[2] = {};     // 0x00608e4c 0x00608e4d (the app's +0x1c4, +0x1c5)

  // The control axis table (0x006040d0) is axis_table(), controls_cfg.h.

  uint8_t cheated = 0;                // 0x00609994  set by turning a cheat on (protection.md)
  uint8_t multiplayer = 0;            // 0x00608e45  a network game is on (the app's +0x1bd):
                                      // the cheat setters leave the mech alone
};

// GameOptions bits ([0x005f4d04]).
enum GameOption : uint32_t {
  kCamera1 = 0x001,        // CameraMode 1 (bit 8 is mode 2)
  kInvincible = 0x002,
  kInfiniteAmmo = 0x004,
  kHeat = 0x008,           // HeatMgmt: heat on; turning it off is a cheat
  kTorsoAutoReturn = 0x010,
  kThrottleConst = 0x020,
  kUseJoystick = 0x040,
  kMw2JumpJets = 0x080,
  kCamera2 = 0x100,
  kFastMenu = 0x200,       // set by -seasick
};

Options& options();

// The calls the setters make into code not ported yet.
class OptionsHooks : public zgame::Unported {
 public:
  virtual Handle player_view() { return unported("[0x005f4c8c]", {}); }  // the player's view, or 0
  virtual Handle player_mech() { return unported("[[0x0060861c]+0x14]", {}); }  // the pilot's mech, or 0
  virtual void view_set_camera_mode(Handle view, int32_t mode) {  // 0x0040ae70
    unported("0x0040ae70", {view, mode});
  }
  virtual void mech_set_invincible(Handle mech, uint8_t on) {     // mech +0x630
    unported("mech+0x630=", {mech, on});
  }
  virtual Handle mech_weapons(Handle mech) { return unported("[mech+0xd4]", {mech}); }
  virtual void weapons_set_infinite_ammo(Handle weapons, uint8_t on) {  // 0x004b0220
    unported("0x004b0220", {weapons, on});
  }
  virtual void mech_set_torso_auto_return(Handle mech, uint8_t on) {  // [mech+0x39c]+0xa0
    unported("[mech+0x39c]+0xa0=", {mech, on});
  }
  virtual void mech_set_throttle_decay(Handle mech, uint8_t on) {    // [mech+0x37c]+0x3f
    unported("[mech+0x37c]+0x3f=", {mech, on});
  }
  virtual void mech_set_jump_jet_mode(Handle mech, int32_t mode) {  // 0x0041cf30
    unported("0x0041cf30", {mech, mode});
  }
  virtual void shadows_set(uint8_t on) { unported("0x004e6920", {on}); }       // zClass shadows
  virtual void effects_set_level(int32_t level) { unported("0x00508fb0", {level}); }  // zEffect
  virtual int32_t video_mode() { return static_cast<int32_t>(unported("[0x0072f348]", {})); }
  virtual uint32_t video_is_3d() { return unported("0x00579ea0", {}); }
  virtual uint32_t video_has_cap(int32_t cap) { return unported("0x00579b70", {cap}); }
  virtual void model_set_global_light(int32_t on) { unported("0x00526a70", {on}); }  // gmod_init
  virtual void camera_set_lod_scale(Handle camera, float s) {      // 0x004f3ff0
    unported("0x004f3ff0", {camera, zgame::bits(s)});
  }
  virtual void mechs_set_lod_scale(float s) { unported("0x00430440", {zgame::bits(s)}); }
  virtual Handle find_node(int32_t kind, const char* name) {      // 0x004f7fe0, zClass list
    return unported("0x004f7fe0", {kind, name});
  }
  virtual void node_set_active(Handle node, int32_t on) { unported("0x004eb170", {node, on}); }
  virtual void sound_set_volume(float v) { unported("0x0056f260", {zgame::bits(v)}); }
  virtual void music_set_volume(int32_t left, int32_t right) {   // 0x004ac230, mech3/music
    unported("0x004ac230", {left, right});
  }
  virtual void camera_get_fov(Handle camera, float* x, float* y) {  // 0x004f3ee0
    unported("0x004f3ee0", {camera});
    *x = 1.0f;
    *y = 1.0f;
  }
  virtual void camera_set_fov(Handle camera, float x, float y) {   // 0x004f3d90
    unported("0x004f3d90", {camera, zgame::bits(x), zgame::bits(y)});
  }
  virtual void window_set_size(Handle w, int32_t width, int32_t height) {  // 0x004f9fa0
    unported("0x004f9fa0", {w, width, height});
  }
  virtual void window_set_origin(Handle w, int32_t x, int32_t y) {  // 0x004fa0b0
    unported("0x004fa0b0", {w, x, y});
  }
  virtual void display_set_size(Handle d, int32_t width, int32_t height) {  // 0x004fa580
    unported("0x004fa580", {d, width, height});
  }
  virtual void display_set_origin(Handle d, int32_t x, int32_t y) {  // 0x004fa600
    unported("0x004fa600", {d, x, y});
  }
  virtual void input_set_screen(int32_t w, int32_t h) { unported("0x0051b590", {w, h}); }
  // Ours, FIX (B): the screen a first start plays at (the desktop's), or false.
  virtual bool first_start_screen(int32_t* w, int32_t* h) { (void)w, (void)h; return false; }
  virtual zrdr::Node* reader_load(const char* name);               // 0x00556740, ported: zrdr::load
  virtual void reader_free(zrdr::Node* n);                         // 0x00556820, ported: zrdr::free_node
  virtual const char* message(int32_t id) {                        // 0x00576040, Mech3Msg.dll
    unported("0x00576040", {id});
    return "";
  }
  // mech3's controls (input.md, Open): the default bindings and CMD_ names,
  // the controls file named by ActiveCtlCfg, and freeing the groups.
  virtual void controls_set_defaults() { unported("0x00411150", {}); }
  virtual void controls_load(const char* name) { unported("0x004120a0", {name}); }
  virtual void controls_free() { unported("0x00410a80", {}); }
};
void set_options_hooks(OptionsHooks* hooks);
OptionsHooks& options_hooks();

void select_hw(int32_t hw);                 // 0x0040d4c0  sets HWCardFlag and the _SW/_HW choice
int32_t hw_card_flag();                     // 0x0040d4d0
void set_hw_card_dev(int32_t dev);          // 0x0040cbf0  and picks hardware when dev >= 0
int32_t hw_card_dev();                      // 0x0040cc10
void set_game_intensity(int32_t level);     // 0x0040cc20
int32_t game_intensity();                   // 0x0040cdf0
void set_game_options(uint32_t bits);       // 0x0040ce00
void set_camera_mode(int32_t mode);         // 0x0040ce10
int32_t camera_mode();                      // 0x0040ce70
void set_fast_menu(int32_t on);             // 0x0040ce90
int32_t fast_menu();                        // 0x0040cec0
void set_invincible(int32_t on);            // 0x0040ced0
int32_t invincible();                       // 0x0040cf20
void set_infinite_ammo(int32_t on);         // 0x0040cf30
int32_t infinite_ammo();                    // 0x0040cf80
void set_heat(int32_t on);                  // 0x0040cf90
int32_t heat();                             // 0x0040cfc0
void set_torso_auto_return(int32_t on);     // 0x0040cfd0
int32_t torso_auto_return();                // 0x0040d010
void set_throttle_const(int32_t on);        // 0x0040d020
int32_t throttle_const();                   // 0x0040d060
void set_use_joystick(int32_t on);          // 0x0040d070
int32_t use_joystick();                     // 0x0040d1b0
void set_mw2_jump_jets(int32_t on);         // 0x0040d1c0
int32_t mw2_jump_jets();                    // 0x0040d1e0
void set_shadow_parts(int32_t v);           // 0x0040d1f0
int32_t shadow_parts();                     // 0x0040d220
void set_shadow(int32_t v);                 // 0x0040d240
int32_t shadow();                           // 0x0040d280
void set_effects_level(int32_t v);          // 0x0040d2a0
int32_t effects_level();                    // 0x0040d2f0
void set_object_lod(int32_t v);             // 0x0040d310
int32_t object_lod();                       // 0x0040d3b0
void set_sound_lod(int32_t v);              // 0x0040d3d0
int32_t sound_lod();                        // 0x0040d3e0
void set_texture_memory(int32_t v);         // 0x0040d3f0
int32_t texture_memory();                   // 0x0040d410
void set_gfx_flags(int32_t v);              // 0x0040d430
int32_t gfx_flags();                        // 0x0040d480
void set_pixel_double(int32_t on);          // 0x0040d4a0
int32_t pixel_double();                     // 0x0040d4b0
void clamp_to(const Rect& r, float p[2]);   // 0x0040d540: into r's first and last pixel
void screen_to_render(float p[2]);          // 0x0040d9e0: a display point into the render target
void render_to_screen(float p[2]);          // 0x0040da30: a render-target point out to the window
void rect_set_origin(Rect& r, int32_t x, int32_t y);  // 0x0040d4e0
void rect_set_size(Rect& r, int32_t w, int32_t h);    // 0x0040d510
void set_camera(Handle camera);             // 0x0040d590
float wide_fov(float vfov, int32_t w, int32_t h);  // FIX (B): set_camera's angle for a shape not 4:3
Handle camera();                            // 0x0040d5e0
void set_render_size(int32_t w, int32_t h);   // 0x0040d600
void set_render_origin(int32_t x, int32_t y); // 0x0040d630
void set_render_target(Handle window);      // 0x0040d670
Rect& render();                             // 0x0040d6a0
void set_display_target(Handle display);    // 0x0040d6b0
void set_display_origin(int32_t x, int32_t y);  // 0x0040d6e0
void set_display_size(int32_t w, int32_t h);    // 0x0040d720
Rect& display();                            // 0x0040d750
int32_t display_w();                        // 0x0040d760
int32_t display_h();                        // 0x0040d770
void set_display_depth(int32_t bpp);        // 0x0040d780
int32_t display_depth();                    // 0x0040d790
int32_t vstride();                          // 0x0040d7a0
Rect& window();                             // 0x0040d7b0
int32_t window_h();                         // 0x0040d7c0
void set_window_size(int32_t w, int32_t h);   // 0x0040d7d0
void set_window_origin(int32_t x, int32_t y); // 0x0040d7f0
void set_vmode(int32_t mode);               // 0x0040d810
// A mode's size: the original's 3 (drawn 320x240) and 5..10, and with
// zfix's any_resolution 11..15 (1280x720 .. 3840x2160) and 16 the player's
// own; false for a mode that keeps the sizes it had (resolution.md).
// Append presets after the existing custom ID so saved modes 3..16 keep
// their meaning. The graphics menu can select ultrawide on existing profiles.
constexpr int32_t kLastVMode = 20, kCustomVMode = 16;
// Full-width 32:9 at 2160p. Desktop shapes fit uniformly when possible;
// impossible/extreme shapes still honor the historical 640x480 minimum.
constexpr int32_t kMaxScreen[2] = {7680, 2160};
bool vmode_size(int32_t mode, int32_t* w, int32_t* h);
// FIX (B), any_resolution: the 2D canvas past mode 10 (480 high, the frame
// over art_scale), and its rects onto the frame (resolution.md step 4).
float art_scale(int32_t frame_h);
bool vmode_canvas(int32_t mode, int32_t* w, int32_t* h);
bool canvas_scaled();  // the display (2D) smaller than the render (3D) rect, the fix on
void canvas_to_render(int32_t* x, int32_t* y, int32_t* w, int32_t* h);
void canvas_point_to_render(float* x, float* y);
// A projected point (frame pixels) onto the canvas, unclamped: for the 2D
// pieces the exe places from 0x0053cf10 without 0x0040da30 (the waypoint
// arrow; TARGET through the zoom window), which then sit on the frame's scale.
void render_point_to_canvas(float* x, float* y);
int32_t vmode();                            // 0x0040d9d0
void set_sound_frequency(float v);             // 0x0040da80
float sound_frequency();                       // 0x0040daa0
void set_cd_volume(float v);                // 0x0040dab0
float cd_volume();                          // 0x0040dae0
void apply_all();                           // 0x0040cb30  every setter with its own value

// The settings loader (docs/settings.md): every option's entry, its value from
// detail.zrd's list for this machine (or the default), then the saved values.
// 1, or 0 when `path` is given and cannot be read.
int32_t load_options(const char* path);     // 0x0040c420
void save_options();                        // 0x0040cb20  the store saved, the controls freed
// detail.zrd's value for `name` on this machine (0x0040c3b0): the first entry
// whose condition holds, else `def` (also without a file).
int32_t detail_value(zrdr::Node* root, const char* name, int32_t def);

}  // namespace mech3
