#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "gamez/zgame/unported.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zweapon/zwep_init.h"
#include "gamez/ztwod/twod.h"

// The HUD (docs/hud.md). File unknown: the code between mech3\mech_data.cpp
// and mech3\mech_map.cpp (0x0043a7c0..0x00448880); named after what it is.
// One HUD per mech type (MechData +0x224), a zTwoD Layer holding its pieces;
// each piece is built here on the ported zTwoD classes. The comments give
// each member's offset in the original's 0x22e40-byte object.
namespace mech3 {

struct Mech;
class Hud;
struct Objectives;

// One part's damage silhouettes: blue, green, yellow, red, black, and where
// the part sits in the silhouette (a 0x20-byte entry of [0x00605888]).
struct DamageImages {
  zimg::Texture* image[5] = {};
  int32_t x = 0, y = 0;
  void set(const char* const names[5], int32_t px, int32_t py);  // 0x0043ae50
};
// The silhouettes every HUD shares ([0x00605888..0x006059a8], loaded once by
// 0x0043afc0 and the constructor): by the table's order, rtorso, lleg, rarm,
// ctorso, ltorso, head, larm, rleg; three backgrounds and their size.
struct DamageSet {
  bool loaded = false;               // 0x00605990
  DamageImages parts[8];             // 0x00605888
  int32_t bg_w = 0, bg_h = 0;        // 0x00605998, 0x0060599c
  zimg::Texture* bg[3] = {};         // 0x006059a0 blue, red, green
};
DamageSet& damage_set();
float& coolant_shown();  // [0x005e3a14] the coolant bar's eased level, 1 at start
void load_damage_set();              // 0x0043afc0

// The compass (0x0043f3e0, vtable 0x005c02e0, 0x678 bytes): a tape scrolled
// under a frame by the heading.
class HudCompass : public ztwod::Object2D {
 public:
  HudCompass();
  void set_heading(float radians);   // 0x0043f590
  void update(float dt) override;    // +0x24  0x0043f620
  float scale = 206.90143f;          // +0x34  pixels a radian
  int32_t half = 0;                  // +0x38  half the frame's width
  int32_t centre = 0xb8;             // +0x3c  the tape's x at north
  ztwod::Rect src{};                 // +0x40  the tape's part shown
  ztwod::Image tape;                 // +0x50  hud_compass at (245, 19)
  ztwod::Image frame;                // +0x10c hud_compass_frame at (245, 11)
  void read_layout(const Hud* hud);  // 0x0043f500 (COMPASS)
  ztwod::FontText text;              // +0x1c8
};

// A text in the font mech_string (vtable 0x005bc598, inlined constructor:
// FontText's with its own destructor 0x0048c050).
class HudText : public ztwod::FontText {
 public:
  HudText();
};

// The panels' green (0, 64, 0) and opacity: 0.6 in Direct3D, opaque in software.
uint16_t hud_green();                // 0x0043b170
float hud_opacity();                 // 0x0043a7c0

// A see-through panel behind a piece (vtable 0x005c0158, 0x108 bytes): an
// image of one colour and alpha made to its size (0x0043ab10), with the
// corners Direct3D draws it by.
class HudPanel : public ztwod::Image {
 public:
  HudPanel();                        // 0x0043a7e0
  ~HudPanel() override;              // 0x0043a890: its image freed
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x0043a8f0
  void set_color(uint16_t c);        // 0x0043aaa0
  void set_alpha(uint8_t a);         // 0x0043a9d0: 0xfe and up opaque
  void update(float dt) override;    // +0x24  0x0043ad00: here only when opaque or in software
  void draw3d(float dt) override;    // +0x28  0x0043ad20: its quad in a scene of its own
  void draw_quad();                  // 0x0043ad70: Direct3D's see-through quad, else the image
  void set_opacity(float f);         // 0x0043aa60
  void set_size(int32_t w, int32_t h);  // 0x0043ab10: its image made or resized
  ztwod::Rect rect{};                // +0xbc
  float corners[4][3] = {};          // +0xcc  (x, y, 0.9): top left, bottom left, bottom right, top right
  uint16_t color = 0;                // +0xfc
  uint8_t alpha = 0;                 // +0xfe
  uint16_t* colors = nullptr;        // +0x100 the image's pixels
  uint8_t* alphas = nullptr;         // +0x104 its alpha
};

// The objectives (vtable 0x005c0918, 0xaac bytes): a text box of the
// objectives over a panel, and one of the multiplayer scores.
class HudObjectives : public ztwod::TextBox {
 public:
  HudObjectives();                   // 0x00446700
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x00446870: the scores 260 to the right
  void update(float dt) override;    // +0x24  0x00447410
  void show_for(float seconds) override;  // +0x60  0x00446820
  void hide() override;              // +0x64  0x00446850
  void set_text(const char* format, ...) override;  // +0x7c  0x00446930: and a blank score beside it
  void fill();                       // 0x00446ad0: the lines from the mission's objectives
  void add_objective(uint8_t done, const char* text);  // 0x00446970: "Completed" / "In Progress" beside it
  void add_more(uint8_t done, const char* score, const char* text);  // 0x00446a20: score null: none
  void data_update(float dt);        // 0x00446900: shown, kept above the message panel's point
  void place_above(int32_t px, int32_t py);  // 0x004468b0: its panel's height above it, the panel 4 left
  HudPanel panel;                    // +0x4c8
  ztwod::TextBox scores;             // +0x5d4
  ztwod::Handle font_title = 0;             // +0xa9c rtobjective_title
  ztwod::Handle font_complete = 0;          // +0xaa0 rtobjective_complete
  ztwod::Handle font_incomplete = 0;        // +0xaa4 rtobjective_incomplete
  ztwod::Handle font_scores = 0;            // +0xaa8 rt_mp_scores
};

// The equipment list's fonts and lights, shared ([0x006059d0..0x006059ec],
// loaded by the first entry made).
struct EquipmentSet {
  zimg::Texture* light[3] = {};      // 0x006059d0 red, yellow, green
  ztwod::Handle font_red = 0;               // 0x006059e0
  ztwod::Handle font_yellow = 0;            // 0x006059e4
  ztwod::Handle font_green = 0;             // 0x006059e8
  ztwod::Handle font_grey = 0;              // 0x006059ec
};
EquipmentSet& equipment_set();

// One line of the equipment list (vtable 0x005c0ae0, 0xa40 bytes): its name,
// a value right-aligned at the column and a light.
class HudEquipLine : public ztwod::Text {
 public:
  HudEquipLine();                    // 0x00447c40
  void place(int32_t px, int32_t py, int32_t column);  // 0x00447fe0
  void reset();                      // 0x00447e10
  void update(float dt) override;    // +0x24  0x00448030
  void set_item(Mech* m, int32_t kind);  // 0x00447e40: shown for the kind, its light and font by its state
  HudText value;                     // +0x4cc
  ztwod::Image light;                // +0x97c
  int32_t state = 0;                 // +0xa38
  int32_t item = 0;                  // +0xa3c
};

// The equipment list (vtable 0x005c0b90, 0xa548 bytes): sixteen lines over a panel.
class HudEquipment : public ztwod::Object2D {
 public:
  HudEquipment();                    // 0x00448070
  void place(int32_t px, int32_t py, int32_t dy);  // 0x00448140
  void update(float dt) override;    // +0x24  0x00448450: the lines up to the first hidden
  void data_update(float dt);        // 0x00448320: a line for each part the mech has, by kind, up to 16
  void read_layout(Hud* hud);        // 0x00448200 (EQUIPMENT)
  void* hud = nullptr;               // +0x34 the HUD it shows
  HudEquipLine lines[16];            // +0x38
  HudPanel panel;                    // +0xa438
  uint8_t below = 1;                 // +0xa540 under the point; else left of it by the column
  int32_t column = 0x6e;             // +0xa544 the values' right edge
};

// The weapon list's lights and fonts, shared ([0x006059b0..0x006059cc],
// loaded by the first line made).
struct WeaponSet {
  zimg::Texture* light[3] = {};      // 0x006059b0 green, yellow, red
  ztwod::Handle font_red = 0;        // 0x006059c0
  ztwod::Handle font_green = 0;      // 0x006059c4
  ztwod::Handle font_yellow = 0;     // 0x006059c8
  ztwod::Handle font_grey = 0;       // 0x006059cc
};
WeaponSet& weapon_set();

// One line of the weapon list (vtable 0x005c0808, 0x1120 bytes): a slider
// (the recycle bar) with the weapon's name, two texts, its light and a mark.
class HudWeaponLine : public ztwod::Slider {
 public:
  HudWeaponLine();                   // 0x00445760
  ~HudWeaponLine() override;         // 0x00445970: the bar's images (the texts') let go first
  void place(int32_t px, int32_t py, int32_t column);  // 0x00445ba0
  void reset();                      // 0x00445a30
  bool set_look(bool red, bool grey);  // 0x00445a60: the texts' fonts; false when unchanged
  void set_weapon(void* w, zimg::Texture* mark = nullptr);  // 0x00445d10: its name, the look forced, a refresh
  bool refresh_line(zimg::Texture* mark);  // 0x00445dc0: the bar, light, count and look from the weapon; true while it recycles
  void update(float dt) override;    // +0x24  0x00445c10: the bar's images from the texts when changed
  HudText name;                      // +0x188
  HudText text2;                     // +0x638
  HudText count_text;                // +0xae8 "%3d", right-aligned at the column
  ztwod::Image light;                // +0xf98
  ztwod::Image mark;                 // +0x1054 left of the line
  int32_t count = 0;                 // +0x1110 the number shown
  uint8_t red = 1;                   // +0x1118
  uint8_t grey = 0;                  // +0x1119 (not set by the constructor)
  uint8_t changed = 0;               // +0x111a
  uint8_t x111b = 0;                 // +0x111b
  void* weapon = nullptr;            // +0x111c
};

// The weapon list (vtable 0x005c0898, 0x11444 bytes): sixteen lines over a panel.
class HudWeapons : public ztwod::Object2D {
 public:
  HudWeapons();                      // 0x00445f80
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x004460c0: the lines 10 apart
  void update(float dt) override;    // +0x24  0x00446680
  void show_for(float seconds) override;  // +0x60  0x00446200: then the lines refreshed (0x00446520)
  void refresh_all();                // 0x00446520: layout, then looks
  void layout();                     // 0x00446240: the lines by group, the current one bracketed
  void looks();                      // 0x00446540: each shown line's mark and refresh
  void hide() override;              // +0x64  0x00446220
  void read_layout(Hud* hud);        // 0x00446120 (WEAPONS)
  void* hud = nullptr;               // +0x34 the HUD it shows
  void* weapons = nullptr;           // +0x38 the mech's weapon manager
  ztwod::PolyLine poly;              // +0x3c
  zimg::Texture* select = nullptr;   // +0x128 hud_weapon_select
  zimg::Texture* chain = nullptr;    // +0x12c hud_weapon_chain
  HudWeaponLine lines[16];           // +0x130
  HudPanel panel;                    // +0x11330
  uint8_t shown = 1;                 // +0x11438 the lines refreshed each frame (some recycling)
  void* next_weapon = nullptr;       // +0x1143c the weapon after the current one (else the first)
  int32_t column = 0x6e;             // +0x11440
};

// A gauge's bar (vtable 0x005c0788, 0x140 bytes; 0x00448530 builds one too):
// a four-point polygon.
class HudBar : public ztwod::Polygon {
 public:
  void set_box(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x00448550: w + 1 wide
  int32_t height = 0;                // +0x138
  int32_t width = 0;                 // +0x13c
};

// The gauges (vtable 0x005c0708, 0x168c bytes): speed, coolant and fuel
// frames with their bars, the heat bar and meter, and three texts.
class HudGauges : public ztwod::Object2D {
 public:
  HudGauges();                       // 0x00444760
  void update(float dt) override;    // +0x24  0x004452c0
  void set_speed(float v);           // 0x004453a0: m/s, shown in km/h, red backwards
  void set_throttle(float v);        // 0x00445450: -1..1, backwards shorter and red
  void set_heat(float level, float rate);  // 0x004455e0: the bar by the level, the toggler by level + rate
  void set_coolant(float v, float dt);  // 0x004456f0: eased toward it, 10% a call (a 30 fps frame with per_second)
  void set_jump(bool jets, float fuel);  // 0x00445520: the fuel frame and bar shown with jets
  float speed = 0.0f;                // +0x34 km/h
  ztwod::FontText coolant_text;      // +0x38   message 0x1d5
  ztwod::FontText heat_text;         // +0x4e8  message 0x1d6 (hud.zrd heat_text)
  HudText speed_text;                // +0x998
  HudBar speed_bar;                  // +0xe48
  HudBar fuel_bar;                   // +0xf88
  HudBar coolant_bar;                // +0x10c8
  ztwod::Image speed_frame;          // +0x1208 hud_speed_frame at (575, 256)
  ztwod::Image coolant_frame;        // +0x12c4 hud_coolant_frame at (8, 290)
  ztwod::Image fuel_frame;           // +0x1380 hud_fuel_frame at (527, 296)
  int32_t x143c = 0;                 // +0x143c
  ztwod::Rect heat_src{};            // +0x1440 the heat bar's shown part: left, top (its unlit height), width, height
  ztwod::Image heat_bar;             // +0x1450 hud_heat_bar at (18, 163)
  int32_t heat_bottom = 0;           // +0x150c the heat bar's bottom
  ztwod::Image heat_meter;           // +0x1510 hud_heat_meter at (16, 163)
  ztwod::Image toggler;              // +0x15cc lab_toggler_green at (10, 150)
  int32_t toggler_y = 0;             // +0x1688 the meter's bottom less half the toggler
  void read_layout(const Hud* hud);  // 0x00444dc0 (GAUGES)
};

// A light of n images, one shown (vtable 0x005c0260, 0xd0 bytes; its
// constructor inlined).
class HudLight : public ztwod::Image {
 public:
  explicit HudLight(int32_t n);
  ~HudLight() override;
  void select(int32_t i);            // inlined: the image of light i
  int32_t count;                     // +0xc0
  zimg::Texture** images;            // +0xbc calloc'd
  int32_t current = -1;              // +0xc4
  float interval = 0.0f;             // +0xc8 seconds an image, 0: the current one stays
  float time = 0.0f;                 // +0xcc
  void update(float dt) override;    // +0x24  0x004486e0
};

// The radar (vtable 0x005c01e0, 0xdc0 bytes): a filled box, the view cone's
// lines, the range texts, the light and two polylines. Two sizes: small
// (55, 55, radius 50) and zoomed (320, 240, radius 180), moved to in steps.
class HudRadar : public ztwod::FilledBox {
 public:
  explicit HudRadar(float fov_degrees);  // 0x0043d260
  int32_t target_at[2] = {};  // ours, for the tests: where the selected target's triangle went last
  int32_t target_draws = 0;
  void show_for(float seconds) override;  // +0x60  0x0043db70: grows back
  void hide() override;              // +0x64  0x0043dc00: shrinks away, stays shown
  void attach(void* hud);            // 0x0043d6d0: the zoomed size from the display
  void set_amber(bool on);           // 0x0043d630: amber instead of green
  void set_fade(float f);            // 0x0043dc70
  void set_zoom(bool zoomed);        // 0x0043da50
  void toggle_zoom();                // 0x0043da40: R, small or large
  void toggle_map();                 // 0x0043dc10: Shift+R, over the mission map when there is one
  void reset_boundary();             // 0x0043d920: the mission boundary's two outlines for the polylines
  uint32_t select_map();             // 0x0043da00: the mission map for the size, kept at [0x0060998c]
  void set_fov(float radians);       // 0x0043f0c0
  void set_dir(const float* v);      // 0x0043f130: the cone's edges from the view direction
  void update(float dt) override;    // +0x24  0x0043dd00 (part 1: the radar; the contacts part 2)
  void update_contacts(float dt, float noise_px);  // 0x0043dd00 from 0x0043e445: the contacts, the projectiles, the network's
  uint16_t x58 = 0;                  // +0x58
  uint16_t zoomed = 0;               // +0x5a
  uint8_t active = 0;                // +0x5c the mission map shown (the radar over it)
  uint8_t changed = 0;               // +0x5d
  int32_t cx[2] = {}, cy[2] = {}, r[2] = {};  // +0x60, +0x68, +0x70 by zoom
  int32_t to_r = 0, to_x = 0, to_y = 0;      // +0x78 the size moved to
  int32_t r_steps = 0, x_steps = 0, y_steps = 0;  // +0x84
  float dir[3] = {};                 // +0x90
  float half_fov = 0.0f;             // +0xa8
  float cos_half = 0.0f, sin_half = 0.0f;  // +0xac, +0xb0
  ztwod::ClipLine2D lines[3];        // +0xb4 the cone's edges (green) and a red one
  HudText text1;                     // +0x1a4
  HudText text2;                     // +0x654
  HudLight light{3};                 // +0xb04 green, yellow, red
  uint32_t xbd4 = 0, xbd8 = 0;       // +0xbd4 the polylines' point counts (drawn from 4)
  ztwod::PolyLine polys[2];          // +0xbdc
  uint32_t xdb4 = 0, xdb8 = 0;       // +0xdb4 their point lists (vt+0xc(i): an item, its vt+0x18 its place)
  void* source = nullptr;            // +0xdbc the HUD it shows
};

// zView's window (0x004b5440, vtable 0x005c91a4), 0x3c bytes: a window node
// and its area on screen.
struct HudViewWindow {
  explicit HudViewWindow(const char* name);  // 0x004b5440
  void set_area(const ztwod::Rect& r);  // inlined in 0x0043f660: the area and its target
  uint32_t node = 0;                 // +0x04
  ztwod::Rect area{};                // +0x08
  ztwod::Rect target{};              // +0x18
  uint8_t scaled = 0;                // +0x28 area and target differ
  float sx = 0.0f, sy = 0.0f, isx = 0.0f, isy = 0.0f;  // +0x2c their ratio (unset when equal)
};

// A zView (0x004b5150, vtable 0x005c9118), 0x18 bytes: a camera node in a
// window, in the world.
struct HudZView {
  HudZView(const char* name, HudViewWindow* w, uint32_t world);  // 0x004b5150
  uint32_t camera = 0;               // +0x04
  HudViewWindow* window = nullptr;   // +0x08 (0x004b51b0)
  uint32_t world = 0;                // +0x0c (0x004b51d0)
  uint32_t x10 = 0;                  // +0x10
  int32_t hardware = 0;              // +0x14 [0x0072f348]
};

// A 3D view in a HUD box (vtable 0x005c0368, 0x16c bytes): the MFD's mech,
// TARGET's and the reticle's zoom. Partial: built; its drawing and camera
// work not ported (the render work, hud.md).
class HudZoomView : public ztwod::Object2D {
 public:
  HudZoomView(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x0043f660
  ~HudZoomView() override;           // 0x0043f9c0: its window freed
  HudZoomView(const HudZoomView&) = delete;
  HudZoomView& operator=(const HudZoomView&) = delete;
  void show_for(float seconds) override;  // +0x60  0x0043fc10: the camera on too
  void hide() override;              // +0x64  0x0043fc60: the camera off too
  HudZView view;                     // +0x34 (its vtable 0x005c0360)
  uint32_t x4c = 0xffff;             // +0x4c
  ztwod::Rect rect{};                // +0x50
  HudPanel panel;                    // +0x60
  void* source = nullptr;            // +0x168 the HUD it shows
  void set_source(void* s) { if (s) source = s; }  // 0x0043fa20
  void place(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x0043f890
};

// The MFD's 3D view (0x00442620, vtable 0x005c0500, 0x258 bytes): the zoom
// view on a panel of its size, boxed.
class HudMfdView : public HudZoomView {
 public:
  HudMfdView(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x00442620
  void place(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x004427b0
  void update(float dt) override;    // +0x24  0x00442880: the box (the 3D view draws in the scene)
  void draw3d(float dt) override;    // +0x28  0x00442870: its zView's render (hooked)
  ztwod::PolyLine box;               // +0x16c (vtable 0x005c0580)
};

// One part's damage image in a damage view (vtable 0x005c09e0, 0xdc bytes).
class HudDamagePart : public ztwod::FlashImage {
 public:
  HudDamagePart(DamageImages* images, int32_t px, int32_t py);  // 0x00447460
  void set_level(float internal);    // 0x004475c0: the image by the part's internal 0..1
  DamageImages* images = nullptr;    // +0xd8
};

// A mech's damage silhouette (vtable 0x005c0a60, 0x7e4 bytes): the background
// and the eight parts, in one of two orders (the player's own, or a target's
// seen from the front).
class HudDamageView : public ztwod::Object2D {
 public:
  // 0x00447680 (own; a third argument unused) and 0x00447820 (target).
  HudDamageView(int32_t px, int32_t py, bool target);
  void update(float dt) override;    // +0x24  0x00447b50
  ztwod::Image bg;                   // +0x34
  HudDamagePart parts[8];            // +0xf0
  void* mech = nullptr;              // +0x7d0 (not set by the constructor)
  ztwod::Rect rect{};                // +0x7d4 the background's
  bool set_mech(void* m);            // 0x00447c00: shown with one
  void update_levels();              // 0x00447a50: each part's image from the mech's parts
  void move(int32_t px, int32_t py);  // 0x004479a0
};

// An MFD (vtable 0x005c0470, 0xcd8 bytes): the damage view, the 3D view, the
// nav list and a title. The MFD itself (0x00442060) and TARGET's base
// (0x0043fcb0 through 0x00441da0) differ in the damage view and the nav
// list's sizes.
class HudMfd : public ztwod::Object2D {
 public:
  HudMfd(int32_t px, int32_t py, int32_t w, int32_t h, bool target);  // 0x00442060 / 0x00441da0
  ~HudMfd() override;                // 0x004422b0: the nav list, then the view, deleted
  void place(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x00442230 (MFD in hud.zrd)
  void attach(void* hud);            // 0x004423c0
  void update(float dt) override;    // +0x24  0x00442540: by the mode, then the title
  virtual int32_t shown_mode() { return mode; }  // +0x80  0x004487d0: 1 the 3D view, 2 the damage, 3 the nav list
  virtual void set_mode(int32_t m);  // +0x7c  0x00442420: kept for the next MFD
  void cycle_mode();                 // 0x00442440: D (the MFD), Ctrl+D (TARGET)
  void update_mode(float dt);        // the shared middle of 0x00442540 and 0x00441490
  void data_update(float dt);        // 0x004424b0: the damage view's levels (mode 2) or the nav list's mech (3)
  // 0x004425d0: the shown piece's rect by the mode (asked again): 1 the 3D
  // view's, 2 the damage view's (0x00447bf0), 3 the nav list's (+0x1c84 of
  // it, hooked); else none.
  const ztwod::Rect* shown_rect();
  void draw3d(float dt) override;    // +0x28  0x00442500: mode 1 the view's, 2 the damage view's (empty)
  void* hud = nullptr;               // +0x34
  int32_t mode = 0;                  // +0x38
  HudDamageView damage;              // +0x3c
  uint32_t nav = 0;                  // +0x820 the nav list (hooked)
  HudMfdView* view = nullptr;        // +0x824
  HudText title;                     // +0x828
};

// TARGET (vtable 0x005c03e8, 0x2414 bytes): an MFD for the target, with its
// arrows, markers and three texts above it.
class HudTarget : public HudMfd {
 public:
  HudTarget(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x0043fcb0
  ~HudTarget() override;             // 0x00440190: the edge arrows let go, then the MFD's
  void hide() override;              // +0x64  0x004415f0
  void show_for(float seconds) override;  // +0x60  0x00441560: the lines and three texts too
  void set_mode(int32_t m) override;  // +0x7c  0x004400f0: kept for the next TARGET
  void set_target(void* t);          // 0x00441800: shown with a live target
  void set_shown(bool on);           // 0x00441780: a new target's pulse, the reticle reset
  void update_texts(bool clear);     // 0x00441660: the damage view's mech, the mode, the nav list, the name
  void data_text();                  // 0x00441850: the name, distance, aimed part, the ally's target; colours by side
  bool over(int32_t px, int32_t py); // 0x00440a30: a NARCed target, or the point on the shape or the circle
  void place(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x00440110 (TARGET in hud.zrd)
  void attach(void* hud);            // 0x00441760
  void update(float dt) override;    // +0x24  0x00441490
  int32_t shown_mode() override;     // +0x80  0x004413a0: none without a live target
  void track(float dt);              // 0x00440b00: the camera, the arrow or circle, the triangle (hud.md section 14)
  void triangle(int32_t team, float dt, bool zoomed);  // 0x00440510: the three lines around the target
  void draw3d(float dt) override;    // +0x28  0x00441410: mode 1 the target through the view
  ztwod::Shape shape1;               // +0xcd8
  ztwod::Line2D lines[3];            // +0xdfc (0x004400e0: all zero)
  ztwod::Shape shape2;               // +0xee0
  ztwod::Frame frame;                // +0x1004
  zimg::Texture* arrows[12] = {};    // +0x1058 enemy, ally, other: left, right, up, down
  ztwod::Image marker{0};            // +0x1088
  HudText texts[4];                  // +0x1144
  void* target = nullptr;            // +0x2404
  uint8_t shown = 0;                 // +0x2408
  float x240c = 0.0f;                // +0x240c the new target's pulse: the triangle starts large and shrinks
  int32_t line_h = 0;                // +0x2410
};

// The reticle's zoom window (0x00442ac0, vtable 0x005c0608): the zoom
// camera's view in a box or circle, its light and text.
class HudZoomWindow : public HudZoomView {
 public:
  HudZoomWindow(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x00442ac0
  void show_for(float seconds) override;  // +0x60  0x004438d0: the view's, its text hidden
  void hide() override;              // +0x64  0x00443880
  void update(float dt) override;    // +0x24  0x00442e30: the scale grows (shown) or shrinks
  void set_rect(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x004438f0: scaled by the zoom, kept on screen
  void set_view_rect(int32_t* px, int32_t* py, int32_t* w, int32_t* h);  // 0x0043fa40: 2 pixels inside the display
  void follow(int32_t px, int32_t py);  // 0x00443660: centred on the reticle, when shown or scaled
  void toggle_lock();                // 0x00442e00: keypad 0, the window locked open or closed
  // 0x004436c0: the zoom camera at the player camera's place, turned to the
  // reticle's aim point, in its zone; the text (distance, the aimed object's
  // name). Nothing while hidden with no scale.
  void place_camera();
  // +0x28 0x00442f50: the zoom's 3D pass (hud.md section 23). Direct3D only
  // (the software path is not ported, CLAUDE.md Rendering).
  void draw3d(float dt) override;
  // Its zView's vt+0x04 (vtable 0x005c0600: 0x00443320, not zView's
  // 0x004b52c0): the camera drawn with the world's fog pushed back, its near
  // range half way to its far, then restored.
  void render_view(float dt);
  uint8_t round = 0;                 // +0x16c a circle, not a box
  float zoom = 0.0f;                 // +0x170 the window's scale
  uint8_t x174 = 0;                  // +0x174 locked open (keypad 0): the zoom key leaves it alone
  int32_t base_w = 0, base_h = 0;    // +0x178
  HudText text;                      // +0x180
  HudLight light{4};                 // +0x630 hud_zoomoff, on, over, cant
  ztwod::Frame frame;                // +0x700 the circle
  ztwod::PolyLine box;               // +0x754 (vtable 0x005c0580)
};

// The reticle (vtable 0x005c0688, 0x95c bytes): its images by state and the
// zoom window.
class HudReticle : public ztwod::Image {
 public:
  HudReticle(const char* on, const char* off);  // 0x00443b60
  ~HudReticle() override;            // 0x00444280: its four images let go, then the zoom window
  // +0x60 show_for 0x00443cf0: Object2D's.
  void hide() override;              // +0x64  0x00443d00: the zoom window too
  void attach(void* hud);            // 0x00444250
  void draw3d(float dt) override;    // +0x28  0x00444230: the zoom window's
  void reset();                      // 0x00443cc0
  void update(float dt) override;    // +0x24  0x004441a0: the zoom, then itself while the zoom is away
  // 0x004445f0: at the display's centre plus (fx, fy) of its half size, the
  // aim worked out, the zoom window following.
  void place(float fx, float fy);
  void aim();                        // 0x00444350: the ray through the reticle into the world
  void set_hit(const zwep::HitRecord* h, float d);  // 0x00444710
  void set_miss(const float* far_point, float far_z);  // 0x004446a0
  HudZoomWindow zoom{10, 0x172, 0x5a, 0x5a};  // +0xbc
  float aim_point[3] = {};           // +0x8fc the world point the weapons aim at (0x0041cd40)
  int32_t state = 0;                 // +0x908
  uint8_t locked = 0;                // +0x90c the image kept
  zimg::Texture* img_off = nullptr;  // +0x910
  zimg::Texture* img_on = nullptr;   // +0x914
  zimg::Texture* img_over = nullptr; // +0x918 ret_over
  zimg::Texture* img_cant = nullptr; // +0x91c ret_cant
  zimg::Texture* img_x920 = nullptr; // +0x920 ret_on again
  float aim_distance = 0.0f;         // +0x924 to the aim point from the near plane (0x00444690)
  zwep::HitRecord hit{};             // +0x928 what the aim hit (its node 0: nothing)
  int32_t lock = 0;                  // +0x950 the missile lock: 0 none, 1 acquiring, 2 waiting, 3 locked, 4 lost
  float lock_timer = 0.0f;           // +0x954 0.6 after a reset ([0x005e3a0c]); counts down in update
  void aim_state(bool over);         // 0x00443d20: the lock and the reticle's image
  void* source = nullptr;            // +0x958 (not set by the constructor)
};

// What the HUD calls outside it.
class HudHooks : public zgame::Unported {
 public:
  // zRender's nodes for the zoom views (not ported: the render work).
  // The nav list in an MFD (0x0047e840 on a new 0x2fb8-byte object; not
  // ported yet, hud.md): its handle.
  virtual uint32_t nav_list_new(int32_t x, int32_t y, int32_t w, int32_t a, int32_t b) {
    return unported("0x0047e840", {x, y, w, a, b});
  }
  // Its deleting destructor, vt+0 with 1 (from the MFD's 0x004422b0).
  virtual void nav_list_free(uint32_t nav) { unported("nav.vt+0", {nav, 1}); }
  // The pilot's game when it is multiplayer (0x00479480 on [0x0060861c]).
  virtual uint32_t multiplayer_game() { return unported("0x00479480", {}); }
  // What the objectives box (0x00446ad0) reads: the missions object
  // ([0x006098d0]) and the pilot's game ([0x0060861c]+0x6c).
  struct ObjectivesSource {
    const Objectives* objectives = nullptr;  // +0x78..+0x88
    uint8_t aborting = 0;                    // +0x56 [0x00609926]: lost
    int32_t game_type = 1;                   // vt+0x2c
    int32_t operation = 0, mission = 0;      // vt+0x48, vt+0x4c
    const char* action_name = nullptr;       // Instant Action's game (0x00479460) vt+0x0c; null none
    const char* training_name = nullptr;     // 0x00437060 of 0x004794a0(vt+0x4c)
  };
  virtual ObjectivesSource objectives_source() {
    unported("0x00446ad0", {});
    return {};
  }
  virtual const char* nav_name(uint32_t nav) { unported("nav+0x28", {nav}); return ""; }  // a nav point's
  // A command's first key's name (0x0051d200, then 0x0051d330 into 0x28 bytes).
  virtual std::string command_key(int32_t cmd) { unported("0x0051d330", {cmd}); return std::string(); }
  // Multiplayer's scores (0x004361e0, 0x00435b60(0x14, 0xc) on its game, then
  // its +0x3c..+0x40 lines).
  virtual std::vector<std::string> multiplayer_scores() { unported("0x004361e0", {}); return {}; }
  virtual const char* message(int32_t id);  // 0x00576040, Mech3Msg.dll's text
  // The MFD's and TARGET's last modes ([0x00605988], [0x0060598c]; 0 at first).
  virtual int32_t saved_mfd_mode() { return static_cast<int32_t>(unported("peek32", {0x605988})); }
  virtual void save_target_mode(int32_t m) { unported("poke32", {0x60598c, m}); }
  virtual int32_t saved_target_mode() { return static_cast<int32_t>(unported("peek32", {0x60598c})); }
  virtual void save_mfd_mode(int32_t m) { unported("poke32", {0x605988, m}); }
  // The mission maps (0x00458590 on [0x00605acc]: the first with +0x3c set
  // whose +0xc0 name holds `name`, or any with +0x3c set for none) and the
  // one the radar shows ([0x0060998c]).
  virtual uint32_t map_find(const char* name) { return unported("0x00458590", {name ? name : ""}); }
  virtual void set_radar_map(uint32_t m) { unported("poke32", {0x60998c, m}); }
  virtual uint32_t window_node_new() { return unported("0x004f9e90", {}); }
  virtual uint32_t camera_node_new() { return unported("0x004f32e0", {}); }
  virtual void node_set_name(uint32_t node, const char* name) { unported("0x004eb300", {node, name}); }
  virtual void camera_set_flag8(uint32_t cam, bool on) { unported("0x004f34c0", {cam, on ? 1u : 0u}); }
  virtual void node_set_active(uint32_t node, bool on) { unported("0x004eb170", {node, on ? 1u : 0u}); }
  virtual void window_set_size(uint32_t node, int32_t w, int32_t h) { unported("0x004f9fa0", {node, w, h}); }
  virtual void window_set_pos(uint32_t node, int32_t x, int32_t y) { unported("0x004fa0b0", {node, x, y}); }
  virtual void camera_set_window(uint32_t cam, uint32_t window) { unported("0x004f3620", {cam, window}); }
  virtual void camera_set_world(uint32_t cam, uint32_t world) { unported("0x004f3560", {cam, world}); }
  virtual void camera_set_zone(uint32_t cam, uint32_t zone) { unported("0x004f6c80", {cam, zone}); }
  virtual void camera_set_zoom(uint32_t cam, float z) { unported("0x004f3ff0", {cam, zgame::bits(z)}); }
  virtual void camera_set_fov(uint32_t cam, float x, float y) {
    unported("0x004f3d90", {cam, zgame::bits(x), zgame::bits(y)});
  }
  virtual void camera_set_clip(uint32_t cam, float near_z, float far_z) {
    unported("0x004f3a70", {cam, zgame::bits(near_z), zgame::bits(far_z)});
  }
  // The player view's camera clip (0x004f3b00 on [0x005f4c8c] +4).
  virtual void player_camera_clip(float* near_z, float* far_z) {
    *near_z = zgame::from_bits(unported("0x004f3b00.near", {}));
    *far_z = zgame::from_bits(unported("0x004f3b00.far", {}));
  }
  // The options' display and window rects (0x0040d750, 0x0040d7b0): left, top, right, bottom.
  virtual ztwod::Rect display_rect();
  virtual ztwod::Rect window_rect();
  // The aim ray (0x00444350): the player view prepared for a projection
  // (0x004b51f0 on [0x005f4c8c]), a screen point (x, y, 1/z) into the world
  // (0x00523930), a node left out of queries (0x004eb6d0), the world's
  // segment query (0x004eed90 on [0x00608c84] +4; nonzero: failed), a
  // material's game soil ([0x00608c00]), the mech's node (+4).
  virtual void view_prepare() { unported("0x004b51f0", {}); }
  virtual void unproject_world(const float screen[3], float out[3]);
  virtual void node_set_intersect(uint32_t node, bool on) { unported("0x004eb6d0", {node, on ? 1u : 0u}); }
  virtual int32_t segment_list(uint32_t world, const float a[3], const float b[3], zwep::HitList* out);
  virtual uint32_t material_soil(zwep::Handle material);
  virtual uint32_t mech_node(Mech* m) { return unported("peek32", {id(m) + 4}); }
  // The world ([0x00608c84]) and its node (+4).
  virtual uint32_t world() { return unported("peek32", {0x608c84}); }
  virtual uint32_t world_node(uint32_t w) { return unported("peek32", {w + 4}); }
  virtual uint32_t id(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
  // A recorded answer as a handle (the probe's emulator addresses).
  static void* ptr(uint32_t v) { return reinterpret_cast<void*>(static_cast<uintptr_t>(v)); }
  // The mission's current world entry (0x00491c80 on [0x006098d0]) and its
  // +0xd0 (above 0: the map is flipped, the compass turned half round).
  virtual uint32_t mission_entry() { return unported("0x00491c80", {}); }
  virtual float entry_flip(uint32_t entry) { return zgame::from_bits(unported("@entry+0xd0", {entry})); }
  // 0x0043da50's sound for the zoom on the source HUD's mech (0x00432990).
  virtual void radar_zoom_sound(void* source, bool zoomed) {
    unported("0x0043da50.sound", {id(source), zoomed ? 1u : 0u});
  }
  // The set-up's outside: the video mode (**[0x005f4d0c], 0x0040d9d0), the
  // display's size (0x0040d760 / 0x0040d770), the player camera's field of
  // view (its data +0xe8), hud.zrd (0x00556740).
  virtual int32_t vmode() { return static_cast<int32_t>(unported("peek32", {unported("peek32", {0x5f4d0c})})); }
  virtual int32_t display_w();
  virtual int32_t display_h();
  virtual float player_camera_fov() { return zgame::from_bits(unported("0x004f3b00.fov", {})); }
  virtual zrdr::Node* load_hud_zrd() { return zrdr::load("hud.zrd"); }
  // The mech's (not ported here): vt+0xa0 (dead), 0x0041df10 (its
  // definition's kind), +0xbc bit 2, the weapon manager +0xd4 and its count
  // +0x24, a part's equipment count +0x3ac, the target slot +0x6c.
  virtual bool mech_dead(Mech* m) { return unported("mech.vt+0xa0", {id(m)}) != 0; }
  virtual int32_t mech_kind(Mech* m) { return static_cast<int32_t>(unported("0x0041df10", {id(m)})); }
  virtual bool mech_flag_bc4(Mech* m) { return (unported("peek8", {id(m) + 0xbc}) >> 2 & 1) != 0; }
  virtual void* mech_weapons(Mech* m) { return ptr(unported("peek32", {id(m) + 0xd4})); }
  virtual int32_t weapons_count(void* w) { return static_cast<int32_t>(unported("peek32", {id(w) + 0x24})); }
  virtual int32_t mech_part_count(Mech* m, int32_t i) {
    return static_cast<int32_t>(unported("peek32", {id(m) + 0x3ac + 4 * i}));
  }
  virtual void* mech_target_slot(Mech*) { return nullptr; }  // wired with the mech (item 10)
  virtual bool target_valid(void* t) { return t && unported("peek32", {id(t)}) != 0; }
  // A target slot's object (0x004e6fe0 on what the slot holds: its +4's
  // vt+0x2c), that object's vt+0xa8 (true: TARGET shows nothing) and its
  // mech (vt+0x50, ai.md).
  virtual void* target_object(void* t) {
    return reinterpret_cast<void*>(uintptr_t(unported("0x004e6fe0", {unported("peek32", {id(t)})})));
  }
  virtual bool object_a8(void* o) { return (unported("obj.vt+0xa8", {id(o)}) & 0xff) != 0; }
  virtual bool object_mech(void* o) { return unported("obj.vt+0x50", {id(o)}) != 0; }
  // TARGET's texts (0x00441660): the object's mech (vt+0x50) and name
  // (vt+0xa4, a C string); a mech's NARC timer (+0x570, 0x00440a30).
  virtual void* object_mech_ptr(void* o) { return ptr(unported("obj.vt+0x50", {id(o)})); }
  virtual const char* object_name(void* o) {
    unported("obj.vt+0xa4", {id(o)});
    return nullptr;
  }
  virtual float mech_narc(void* m) { return zgame::from_bits(unported("peek32", {id(m) + 0x570})); }
  // The HUD's sound channels (0x0041e7a0: a channel's sound stopped; with
  // `fade`, faded instead).
  virtual void hud_sound_stop(int32_t channel, int32_t fade) { unported("0x0041e7a0", {channel, fade}); }
  // The nav list: given the mech (0x0047eba0), placed (0x0047eb60).
  virtual void nav_list_attach(uint32_t nav, Mech* m) { unported("0x0047eba0", {nav, id(m)}); }
  virtual void nav_list_place(uint32_t nav, int32_t x, int32_t y, int32_t h, int32_t a, int32_t b) {
    unported("0x0047eb60", {nav, x, y, h, a, b});
  }
  // The HUD frame's pieces not ported yet: the debug overlays (0x0043c820's
  // first part: the mech's numbers into five debug texts, while the debug
  // text 0x005fc378 shows, toggled by 0x0040dc34 and [0x005f4d6c]; its flags
  // are [0x005fc384]), whether the reticle's point is over TARGET's shape or
  // circle while a live target is in range (0x00440a30), the reticle's aim
  // state from it (0x00443d20, HudReticle::aim_state), the nav list's update (its vt+0x24), the
  // network's (0x00479ed0 while [0x00609980]).
  virtual void hud_debug(void* hud) {
    if (!(unported("peek32", {0x5fc384}) >> 4 & 1)) unported("0x0043c820.debug", {id(hud)});
  }
  virtual bool reticle_over_target(void* target, int32_t x, int32_t y);  // 0x00440a30 (HudTarget::over)
  virtual void reticle_aim_state(void* reticle, bool over);  // 0x00443d20 (HudReticle::aim_state)
  // The aim state's outside (0x00443d20). The weapon manager's selected
  // group (mac 4, the planner): its longest range (0x004b1bd0), whether any
  // of its weapons locks (0x004b1c70) and 0x004b1cd0; its +0x88, the lock
  // shown. TARGET's slot's contact ([slot]), its mech (0x004e6ff0) and
  // distance (0x004e7030, zai_target); a mech's kind (+0x684 +0xc) and
  // +0x3cc. The aimed node's object (0x0040eee0 → 0x0040e920), the object's
  // +8 byte and the node's +0xbc. The HUD's sounds (0x0041e800 playing,
  // 0x0041e770 played at 1.0, -1.0).
  virtual float group_range(void* wm) { return zgame::from_bits(unported("0x004b1bd0", {id(wm)})); }
  virtual bool group_locks(void* wm) { return (unported("0x004b1c70", {id(wm)}) & 0xff) != 0; }
  virtual bool group_4b1cd0(void* wm) { return (unported("0x004b1cd0", {id(wm)}) & 0xff) != 0; }
  virtual void set_lock_shown(void* wm, bool on) { unported("poke8", {id(wm) + 0x88, on ? 1u : 0u}); }
  virtual void* slot_contact(void* slot) { return ptr(unported("peek32", {id(slot)})); }
  virtual void* contact_mech(void* c) { return ptr(unported("0x004e6ff0", {id(c)})); }
  virtual float contact_distance(void* c) { return zgame::from_bits(unported("0x004e7030", {id(c)})); }
  virtual int32_t mech_kind_of(void* m) {
    return static_cast<int32_t>(unported("peek32", {unported("peek32", {id(m) + 0x684}) + 0xc}));
  }
  virtual int32_t mech_3cc(void* m) { return static_cast<int32_t>(unported("peek32", {id(m) + 0x3cc})); }
  virtual void* node_object(void* node) { return ptr(unported("0x0040e920", {id(node)})); }
  virtual bool object_byte8(void* o) { return (unported("peek8", {id(o) + 8}) & 0xff) != 0; }
  virtual uint32_t node_bc(void* node) { return unported("peek32", {id(node) + 0xbc}); }
  virtual bool hud_sound_playing(int32_t channel) { return unported("0x0041e800", {channel}) != 0; }
  virtual void hud_sound_play(int32_t channel) { unported("0x0041e770", {channel}); }
  virtual void nav_list_update(uint32_t nav, float dt) { unported("nav.vt+0x24", {nav, zgame::bits(dt)}); }
  virtual void network_hud() {
    if (unported("peek32", {0x609980})) unported("0x00479ed0", {});
  }
  // The Direct3D pass's render calls: a dirty rect (0x00576ce0, through
  // [0x00809348]), the scene (0x00579af0 / 0x00579b10), a flat see-through
  // quad (four vertices into the TL buffer 0x0072f41c from the last, then
  // [0x0072f3b0] = 0x0057bf80 with the colour, alpha, 4, 0, 0), the
  // transparent flush ([0x0072f3e4]), the message panel (0x0040fb30: the
  // panel [0x00603d7c] while shown), a zView's render (0x00442870, its +4),
  // TARGET's view of its target (0x004428b0 on the view and the target's
  // node, between the target's vt+0x68(1) and (0)).
  virtual void dirty_rect(const ztwod::Rect* r) {
    unported("0x00576ce0", {r ? r->left : 0, r ? r->top : 0, r ? r->right : 0, r ? r->bottom : 0});
  }
  virtual void scene_begin() { unported("0x00579af0", {}); }
  virtual void scene_end() { unported("0x00579b10", {}); }
  virtual void flat_quad(const float v[4][3], int16_t colour, uint8_t alpha) {
    unported("0x0057bf80", {colour, alpha, zgame::bits(v[0][0]), zgame::bits(v[0][1]), zgame::bits(v[0][2]),
                            zgame::bits(v[1][0]), zgame::bits(v[1][1]), zgame::bits(v[1][2]), zgame::bits(v[2][0]),
                            zgame::bits(v[2][1]), zgame::bits(v[2][2]), zgame::bits(v[3][0]), zgame::bits(v[3][1]),
                            zgame::bits(v[3][2])});
  }
  virtual void flush_transparent() { unported("[0x0072f3e4]", {}); }
  virtual const ztwod::Rect* nav_rect(uint32_t nav) {  // the nav list's +0x1c84 (no call)
    static ztwod::Rect r;
    r = {static_cast<int32_t>(unported("peek32", {nav + 0x1c84})), static_cast<int32_t>(unported("peek32", {nav + 0x1c88})),
         static_cast<int32_t>(unported("peek32", {nav + 0x1c8c})), static_cast<int32_t>(unported("peek32", {nav + 0x1c90}))};
    return &r;
  }
  virtual void zview_render(void* view, float dt) { unported("0x00442870", {id(view), zgame::bits(dt)}); }
  virtual void object_view_flag(void* o, int32_t on) { unported("obj.vt+0x68", {id(o), on}); }
  virtual uint32_t object_node(void* o) { return unported("peek32", {id(o) + 4}); }
  // The zoom's 3D pass (0x00442f50, 0x004436c0): the player view's camera's
  // place (data +0x14) and zone (+0x1e4); a camera's window's origin and size
  // (0x004f3630, 0x004fa130, 0x004fa020); the depth mask's fan (7 screen
  // points at depth 0.99 through [0x0072f3ac] = 0x0057bd50, the clear
  // texture); the reticle's aim point turned into angles (0x00523ac0).
  // [[[0x005f4c8c] +4] +0x38]: the player view's camera's data.
  uint32_t player_camera_data() {
    return unported("peek32", {unported("peek32", {unported("peek32", {0x5f4c8c}) + 4}) + 0x38});
  }
  virtual void player_camera_place(float out[3]) {
    const uint32_t d = player_camera_data();
    for (int i = 0; i < 3; ++i) out[i] = zgame::from_bits(unported("peek32", {d + 0x14 + 4 * i}));
  }
  virtual uint32_t player_camera_zone() { return unported("peek32", {player_camera_data() + 0x1e4}); }
  virtual void camera_window_rect(uint32_t cam, int32_t* x, int32_t* y, int32_t* w, int32_t* h) {
    const uint32_t win = unported("0x004f3630", {cam});
    *x = static_cast<int32_t>(unported("0x004fa130.x", {win}));
    *y = static_cast<int32_t>(unported("0x004fa130.y", {win}));
    *w = static_cast<int32_t>(unported("0x004fa020.w", {win}));
    *h = static_cast<int32_t>(unported("0x004fa020.h", {win}));
  }
  // The zoom's own zView render (0x00443320): the world's fog range
  // (0x004f24c0, 0x004f2440) and its push to the device (0x004f1df0).
  virtual void world_fog_range(uint32_t world, float* near_range, float* far_range) {
    *near_range = zgame::from_bits(unported("0x004f24c0.near", {world}));
    *far_range = zgame::from_bits(unported("0x004f24c0.far", {world}));
  }
  virtual void world_set_fog_range(uint32_t world, float near_range, float far_range) {
    unported("0x004f2440", {world, zgame::bits(near_range), zgame::bits(far_range)});
  }
  virtual void world_push_fog(uint32_t world) { unported("0x004f1df0", {world}); }
  virtual void mask_fan(const float* xyz, int32_t count) {
    for (int32_t i = 0; i < count; ++i)
      unported("[0x0072f3ac]", {zgame::bits(xyz[3 * i]), zgame::bits(xyz[3 * i + 1]), zgame::bits(xyz[3 * i + 2])});
  }
  virtual void view_draw_node(void* view, uint32_t node) { unported("0x004428b0", {id(view), node}); }
  // TARGET's tracking's outside (0x00440b00, 0x00440510): a contact's
  // direction (+0x20, set by 0x004e7030), entity (+8) and size (0x004e7070:
  // its vehicle's +0x68, else 5); an entity's node position (+0x14, its data
  // +0x38: +0x84, after 0x004ed280 when its flags have 0x20); a vehicle's
  // centre (vt+0xbc) and team (vt+0x28, +0xc0); the mech's ally and enemy
  // masks (+0xc4, +0xc8); a view prepared for a projection (0x004b51f0,
  // the zoom window's or the player's); a point on the screen (0x0053cf10,
  // zmod::project_point); TARGET's camera placed (0x004f3870) and turned
  // (0x004f36b0); the damage view (0x00447a50) and nav list (0x0047ef10) for
  // the target.
  virtual void contact_dir(void* c, float out[3]) { vec3_at(id(c) + 0x20, out); }
  virtual void* object_seen(void* c) { return ptr(unported("peek32", {id(c) + 4})); }
  virtual void* contact_entity(void* c) { return ptr(unported("peek32", {id(c) + 8})); }
  virtual float contact_size(void* c) { return zgame::from_bits(unported("0x004e7070", {id(c)})); }
  virtual void entity_position(void* e, float out[3]) {
    const uint32_t node = unported("peek32", {id(e) + 0x14});
    const uint32_t data = unported("peek32", {node + 0x38});
    if (unported("peek8", {data}) & 0x20) unported("0x004ed280", {node, 1});
    vec3_at(data + 0x84, out);
  }
  virtual void vehicle_center(void* v, float out[3]) { vec3_at(unported("obj.vt+0xbc", {id(v)}), out); }
  virtual uint32_t object_team(void* o) { return unported("peek32", {unported("obj.vt+0x28", {id(o)}) + 0xc0}); }
  virtual uint32_t mech_allies(Mech* m) { return unported("peek32", {id(m) + 0xc4}); }
  virtual uint32_t mech_enemies(Mech* m) { return unported("peek32", {id(m) + 0xc8}); }
  virtual void zview_prepare(void*) { unported("0x004b51f0", {}); }
  virtual uint32_t project(const float in[3], float out[3]) {
    const uint32_t code = unported("0x0053cf10", {zgame::bits(in[0]), zgame::bits(in[1]), zgame::bits(in[2])});
    vec3_at(unported("0x0053cf10.out", {}), out);
    return code;
  }
  virtual void camera_set_position(uint32_t cam, const float p[3]) {
    unported("0x004f3870", {cam, zgame::bits(p[0]), zgame::bits(p[1]), zgame::bits(p[2])});
  }
  virtual void camera_set_angles(uint32_t cam, const float a[3]) {
    unported("0x004f36b0", {cam, zgame::bits(a[0]), zgame::bits(a[1]), zgame::bits(a[2])});
  }
  virtual void nav_list_target(uint32_t nav, void* mech) { unported("0x0047ef10", {nav, id(mech)}); }
  // TARGET's text (0x00441850): a mech's network flag (vt+0x5c), name
  // (vt+0xa4), loadout's name (its Config +0x3ec, +0x18) and display name by kind
  // (0x00484670); the player's aimed part (+0xf8, 0x0041d510 on the mech,
  // its name 0x00484d70); its saved or ally contact (0x004d7240); message
  // 0xdd4 with a name (0x00575fd0).
  virtual bool mech_remote(void* m) { return unported("cmech.vt+0x5c", {id(m)}) != 0; }
  virtual const char* mech_object_name(void* m) {
    unported("cmech.vt+0xa4", {id(m)});
    return nullptr;
  }
  virtual const char* mech_config_name(void* m) {
    unported("peek32", {unported("peek32", {id(m) + 0x3ec}) + 0x18});
    return nullptr;
  }
  virtual const char* mech_display_name(int32_t kind) {
    unported("0x00484670", {static_cast<uint32_t>(kind)});
    return nullptr;
  }
  virtual int32_t mech_aim_part(Mech* m) { return static_cast<int32_t>(unported("peek32", {id(m) + 0xf8})); }
  virtual bool part_by_id(Mech* m, int32_t part) { return unported("0x0041d510", {id(m), static_cast<uint32_t>(part)}) != 0; }
  virtual const char* part_display_name(int32_t part) {
    unported("0x00484d70", {static_cast<uint32_t>(part)});
    return nullptr;
  }
  virtual void* saved_or_ally(Mech* m) { return ptr(unported("0x004d7240", {id(m)})); }
  virtual const char* message_arg(int32_t msg, const char* arg) {
    unported("0x00575fd0", {static_cast<uint32_t>(msg)});
    (void)arg;
    return nullptr;
  }
  // The damage view's (0x00447a50): a mech's part (+0x38c legs, +0x394 arms,
  // +0x39c torso, +0x3a4 head) and its internal 0..1 by section (vt+0x48,
  // Part::get_internal).
  virtual float part_internal(void* mech, uint32_t part, int32_t section) {
    return zgame::from_bits(unported("part.vt+0x48", {unported("peek32", {id(mech) + part}), static_cast<uint32_t>(section)}));
  }
  // The static's (0x0043cd20, hud.md section 21): the mission's player
  // ([0x0060861c] +0x14; the tests make it the HUD's mech), GameIntensity's
  // flag ([0x00608e4c]), the finish target on the frame's surface (0x00576ca0..
  // 0x00576cd0 with a device, 0x00576d80..0x00576db0 without, to 0x00558290)
  // and the static itself (0x00558c10: the rect, or null for the target, the
  // level, three colours).
  virtual void* mission_player(void* hud_mech) { return hud_mech; }
  virtual bool intensity_flag() { return (unported("peek32", {0x608e4c}) & 0xff) != 0; }
  virtual void static_target() { unported("0x00558290", {}); }
  virtual void static_fill(const ztwod::Rect* r, double level, uint16_t lo, uint16_t mid, uint16_t hi) {
    uint64_t b;
    std::memcpy(&b, &level, 8);
    const int32_t z[4] = {};
    const int32_t* q = r ? &r->left : z;
    unported("0x00558c10", {r ? 1 : 0, q[0], q[1], q[2], q[3], static_cast<uint32_t>(b), static_cast<uint32_t>(b >> 32),
                            lo, mid, hi});
  }
  // The HUD's data update's outside (0x0043c390): the mech's controls'
  // throttle and its target (IControls vt+0x1c, vt+0x2c on +0x37c), its
  // forward (+0x24), the radar's positions (0x0041cdf0, or with the radar
  // active the view camera's +0x20 through 0x00522170 / 0x004020d0, then
  // 0x0043f2d0), the gauges' setters (0x004453a0 speed, 0x00445450 throttle
  // target, 0x004455e0 heat, 0x004456f0 coolant, 0x00445520 jump jets with
  // the mech's vt+0x148), TARGET's text (0x00441850), the equipment's
  // (0x00448320), the MFD's (0x004424b0), the objectives' (0x00446900), the
  // network's (0x00479ef0 while [0x00609980]), the mission's time
  // ([0x0060991c], a network game's [0x00609920]).
  virtual float controls_throttle(Mech*) { return zgame::from_bits(unported("controls.vt+0x1c", {})); }
  virtual float controls_throttle_target(Mech*) { return zgame::from_bits(unported("controls.vt+0x2c", {})); }
  virtual void mech_forward(Mech* m, float out[3]) { vec3_at(id(m) + 0x24, out); }
  virtual float mech_f(Mech* m, uint32_t off) { return zgame::from_bits(unported("peek32", {id(m) + off})); }
  virtual float mech_jump_fuel(Mech* m) { return zgame::from_bits(unported("mech.vt+0x148", {id(m)})); }
  virtual void radar_view_dir(float out[3]) { vec3_at(unported("0x00522170", {}), out); }  // the view camera's +0x20
  virtual float normalize(float v[3]) {  // 0x004020d0, in place; the length
    return zgame::from_bits(unported("0x004020d0", {zgame::bits(v[0]), zgame::bits(v[1]), zgame::bits(v[2])}));
  }
  virtual void radar_heading(void* radar, Mech* m) { unported("0x0043f2d0", {id(radar), id(m) + 0x24}); }
  virtual void mech_radar_dir(Mech* m, float out[3]) { vec3_at(unported("0x0041cdf0", {id(m)}), out); }
  // The equipment's: the mech's AMS (+0x3f0: on, rounds +0x18),
  // an item kind's name (0x00484980: message 0x1c6 + kind).
  virtual bool mech_ams(Mech* m, bool* on, int32_t* count) {
    const uint32_t r = unported("peek32", {id(m) + 0x3f0});
    if (!r) return false;
    *on = (unported("peek8", {r}) & 0xff) != 0;
    *count = static_cast<int32_t>(unported("peek32", {r + 0x18}));
    return true;
  }
  virtual const char* equipment_name(int32_t kind) {
    unported("0x00484980", {static_cast<uint32_t>(kind)});
    return nullptr;
  }
  virtual void network_data(int32_t active, float dt) {
    if (unported("peek32", {0x609980})) unported("0x00479ef0", {static_cast<uint32_t>(active), zgame::bits(dt)});
  }
  virtual float mission_time(bool network) {
    return zgame::from_bits(unported("peek32", {network ? 0x609920u : 0x60991cu}));
  }
  // The radar's outside (0x0043dd00): the mission map (0x00491c80 on
  // 0x006098d0; its +0xd4 scale, +0xd8 rect, a world point on it 0x004586e0,
  // drawn by its +0x24), the mech's sensor range (+0x9c), +0xbc flags and
  // orientation (+0x48, a quaternion), the polylines' lists.
  virtual void* radar_map() { return ptr(unported("0x00491c80", {})); }
  virtual const ztwod::Rect* map_rect(void* map) {  // +0xd8 (none: 0)
    const uint32_t a = unported("peek32", {id(map) + 0xd8});
    if (!a) return nullptr;
    static ztwod::Rect r;
    r = {static_cast<int32_t>(unported("peek32", {a})), static_cast<int32_t>(unported("peek32", {a + 4})),
         static_cast<int32_t>(unported("peek32", {a + 8})), static_cast<int32_t>(unported("peek32", {a + 12}))};
    return &r;
  }
  virtual float map_scale(void* map) { return zgame::from_bits(unported("peek32", {id(map) + 0xd4})); }
  virtual bool map_point(void* map, const float p[3], int32_t out[2]) {
    const uint32_t ok = unported("0x004586e0", {id(map), zgame::bits(p[0]), zgame::bits(p[1]), zgame::bits(p[2])});
    out[0] = static_cast<int32_t>(unported("0x004586e0.x", {}));
    out[1] = static_cast<int32_t>(unported("0x004586e0.y", {}));
    return (ok & 0xff) != 0;
  }
  virtual void map_update(void* map, float dt) { unported("map.vt+0x24", {id(map), zgame::bits(dt)}); }
  virtual float mech_sensor_range(Mech* m) { return zgame::from_bits(unported("peek32", {id(m) + 0x9c})); }
  virtual uint8_t mech_bc(Mech* m) { return static_cast<uint8_t>(unported("peek8", {id(m) + 0xbc})); }
  virtual void mech_orientation(Mech* m, float q[4]) {
    for (int i = 0; i < 4; ++i) q[i] = zgame::from_bits(unported("peek32", {id(m) + 0x48 + 4u * i}));
  }
  // The mission boundary (0x0043d920): the player's settings (its controls'
  // vt+0x90, +0xd0) last vlist region named reg_inside_ (0x004d92f0) or
  // reg_outside_ (0x004d9360), 0 for none; a list's point count (vt+0x10).
  virtual uint32_t radar_boundary(int32_t outside) { return unported(outside ? "0x004d9360" : "0x004d92f0", {}); }
  virtual uint32_t radar_point_count(uint32_t list) { return unported("list.vt+0x10", {list}); }
  virtual void radar_point(uint32_t list, int32_t i, float out[3]) {
    vec3_at(unported("item.vt+0x18", {unported("list.vt+0xc", {list, static_cast<uint32_t>(i)})}), out);
  }
  // The radar's contacts (0x0043dd00 from 0x0043e445): the mech's contacts
  // (+0xac..+0xb0, pointers), one on the radar (+0x2c bit 0), two the same
  // (their +8 and vtable), an object's place (0x005a85e0) and blip by kind
  // (vt+0x3c: 0 ally, 1 enemy, 2 other; the Mech's mech_radar_blip), a blip
  // placed (vt+0xc) and drawn (vt+4). The network's marker: the player's
  // element ([0x00609980]'s 0x0058b060 by its +0x20): the mission waynet's
  // selected point (vehicles.md, The nav waynet), its image by its +0xc
  // ([0x00608bac] set, else [0x00608ba8]), place (vt+0x18) and name (+0x28).
  // The weapons' radar list (std::list at 0x0060b228, weapon.h radar_blips:
  // a shot's place its +0x48, hostile); the landmines (0x00604e48's vector
  // +4..+8 of 16 bytes: armed, the place).
  virtual int32_t mech_contact_count(Mech* m) {
    return static_cast<int32_t>((unported("peek32", {id(m) + 0xb0}) - unported("peek32", {id(m) + 0xac})) / 4);
  }
  virtual void* mech_contact(Mech* m, int32_t i) {
    return ptr(unported("peek32", {unported("peek32", {id(m) + 0xac}) + 4u * i}));
  }
  virtual bool contact_on_radar(void* c) { return (unported("peek8", {id(c) + 0x2c}) & 1) != 0; }
  // Ours: black sheep wall (cheats.h) on the mech's radar: every contact
  // shown, and the mechs and vehicles it has none of.
  virtual bool radar_all(Mech* m) { (void)m; return false; }
  virtual std::vector<void*> radar_unsensed(Mech* m) { (void)m; return {}; }
  virtual bool contact_same(void* a, void* b) {
    return unported("peek32", {id(a) + 8}) == unported("peek32", {id(b) + 8}) &&
           unported("peek32", {id(a)}) == unported("peek32", {id(b)});
  }
  virtual void object_position(void* o, float out[3]) { vec3_at(unported("0x005a85e0", {id(o)}), out); }
  // Ours (W32): a targets.zrd object's centroid for the radar, false for any
  // other object (its 0x005a85e0 place kept).
  virtual bool object_radar_place(void* o, float out[3]) {
    (void)o, (void)out;
    return false;
  }
  virtual void* object_blip(void* o, int32_t kind) { return ptr(unported("obj.vt+0x3c", {id(o), static_cast<uint32_t>(kind)})); }
  virtual void blip_place(void* b, int32_t x, int32_t y) { unported("blip.vt+0xc", {id(b), static_cast<uint32_t>(x), static_cast<uint32_t>(y)}); }
  virtual void blip_draw(void* b) { unported("blip.vt+0x4", {id(b)}); }
  virtual void* network_marker() {
    const uint32_t net = unported("peek32", {0x609980});
    if (!net) return nullptr;
    return ptr(unported("0x0058b060", {net, unported("peek32", {net + 0x20})}));
  }
  virtual void* marker_image(void* e) {
    return ptr(unported("peek32", {unported("peek8", {id(e) + 0xc}) & 0xff ? 0x608bacu : 0x608ba8u}));
  }
  virtual void marker_position(void* e, float out[3]) { vec3_at(unported("marker.vt+0x18", {id(e)}), out); }
  // The name (+0x28) as the message's %1, a string (the program's). The
  // default answers the original's address as the pointer: only for a
  // format that is stubbed (the tests'), never read through.
  virtual const char* marker_name(void* e) {
    return reinterpret_cast<const char*>(static_cast<uintptr_t>(unported("peek32", {id(e) + 0x28})));
  }
  virtual bool radar_shot(int32_t i, float out[3], bool* hostile) {
    const uint32_t head = unported("peek32", {0x60b22c});
    uint32_t n = unported("peek32", {head});
    for (int32_t k = 0; k < i && n != head; ++k) n = unported("peek32", {n});
    if (n == head) return false;
    const uint32_t b = unported("peek32", {n + 8});
    vec3_at(unported("peek32", {b}) + 0x48, out);
    *hostile = (unported("peek8", {b + 4}) & 0xff) != 0;
    return true;
  }
  virtual int32_t mine_count() {
    return static_cast<int32_t>((unported("peek32", {0x604e50}) - unported("peek32", {0x604e4c})) / 16);
  }
  virtual bool mine(int32_t i, float out[3]) {
    const uint32_t m = unported("peek32", {0x604e4c}) + 16u * i;
    vec3_at(m + 4, out);
    return (unported("peek8", {m}) & 0xff) != 0;
  }
  // The lead point's outside (0x0041d940, mech3/mech_hud lead_point): a
  // group's current weapon (+0x10); a weapon's projectile speed (def +0x2c)
  // and def flags (+0x6c); a contact's vehicle (0x004e6fe0); the mech's own
  // aim point (+0xec while +0xf8 is not -1); positions (0x005a85e0 the
  // mech's, 0x005a8630 a vehicle's) and velocities (vt+0xc), as pointers
  // to three floats in the original.
  virtual void* group_current_weapon(void* g) { return ptr(unported("peek32", {id(g) + 0x10})); }
  virtual float weapon_speed(void* w) {
    return zgame::from_bits(unported("peek32", {unported("peek32", {id(w) + 0x2c}) + 0x2c}));
  }
  virtual uint32_t weapon_def_flags(void* w) { return unported("peek32", {unported("peek32", {id(w) + 0x2c}) + 0x6c}); }
  virtual void* contact_vehicle(void* c) { return ptr(unported("0x004e6fe0", {id(c)})); }
  virtual bool mech_aim_point(Mech* m, float out[3]) {
    if (static_cast<int32_t>(unported("peek32", {id(m) + 0xf8})) == -1) return false;
    for (int i = 0; i < 3; ++i) out[i] = zgame::from_bits(unported("peek32", {id(m) + 0xec + 4u * i}));
    return true;
  }
  void vec3_at(uint32_t a, float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = zgame::from_bits(unported("peek32", {a + 4u * i}));
  }
  virtual void mech_position(Mech* m, float out[3]) { vec3_at(unported("0x005a85e0", {id(m)}), out); }
  virtual void vehicle_position(void* v, float out[3]) { vec3_at(unported("0x005a8630", {id(v)}), out); }
  virtual void mech_velocity(Mech* m, float out[3]) { vec3_at(unported("mech.vt+0xc", {id(m)}), out); }
  virtual void vehicle_velocity(void* v, float out[3]) { vec3_at(unported("obj.vt+0xc", {id(v)}), out); }
  // Whether the MFD's title shows: [0x00609994] alone, [0x00605f88] clear in a
  // multiplayer game.
  virtual bool title_flag() { return (unported("peek8", {0x609994}) & 0xff) != 0; }
  virtual bool title_mp_off() { return unported("peek32", {0x605f88}) != 0; }
  // The weapon list's outside, read-only (weapon.h's WeaponManager and
  // Weapon): the manager's mode +0x1c, group index +0x28 (-1 none), groups
  // +0x2c; a group's count +8 and weapons +0x14; the current weapon
  // (0x004b04c0); a weapon's group +0x60, name (def +0x2c, +8), vt+8,
  // recycle fraction vt+0x34, damage 0..1 (0x004b3010), enabled +0x74, ammo
  // vt+0x14.
  virtual int32_t wm_mode(void* wm) { return static_cast<int32_t>(unported("peek32", {id(wm) + 0x1c})); }
  virtual int32_t wm_group_index(void* wm) { return static_cast<int32_t>(unported("peek32", {id(wm) + 0x28})); }
  virtual void* wm_group(void* wm, int32_t i) { return ptr(unported("peek32", {id(wm) + 0x2c + 4 * i})); }
  virtual int32_t group_count(void* g) { return static_cast<int32_t>(unported("peek32", {id(g) + 8})); }
  virtual void* group_weapon(void* g, int32_t k) { return ptr(unported("peek32", {id(g) + 0x14 + 4 * k})); }
  virtual void* wm_current(void* wm) { return ptr(unported("0x004b04c0", {id(wm)})); }
  virtual int32_t weapon_group(void* w) { return static_cast<int32_t>(unported("peek32", {id(w) + 0x60})); }
  virtual const char* weapon_name(void* w) {
    unported("weapon.name", {id(w)});
    return "";
  }
  virtual void weapon_vt8(void* w) { unported("weapon.vt+0x8", {id(w)}); }
  virtual float weapon_recycle(void* w) { return zgame::from_bits(unported("weapon.vt+0x34", {id(w)})); }
  virtual float weapon_damage(void* w) { return zgame::from_bits(unported("0x004b3010", {id(w)})); }
  virtual bool weapon_enabled(void* w) { return (unported("peek8", {id(w) + 0x74}) & 0xff) != 0; }
  virtual int32_t weapon_ammo(void* w) { return static_cast<int32_t>(unported("weapon.vt+0x14", {id(w)})); }
};
HudHooks& hud_hooks();
ztwod::Rect& triangle_rect();
ztwod::Rect& radar_rect();     // [0x005e39c8] the radar's polylines' clip (0x0043dd00)  // [0x005e39d8] TARGET's triangle's clip (0x00440510)
void set_hud_hooks(HudHooks* h);

// The timer (+0x18410): a FontText "00:00:00.00" at (0x244, 0x0f) in
// mech_string, set as it is made (vtable 0x005bc598, FontText's slots).
class HudTimer : public ztwod::FontText {
 public:
  HudTimer();
};

// The HUD object (0x0043b190, vtable 0x005c01d8).
class Hud : public ztwod::Layer {
 public:
  explicit Hud(Mech* mech);          // 0x0043b190
  ~Hud() override;                   // 0x0043b860: the pieces', in the exe's order (members' reverse)
  static int32_t live();             // ours, for the tests: the HUDs made and not freed
  static int32_t setups();           // ours, for the tests: setup's runs (the mode change's refit, 0x0041cdc0)
  void setup(Mech* m);               // 0x0043bb80: hud.zrd's layout for the video mode, the mech given out
  void set_active(int32_t on) override;  // +0x04  0x0043baf0: the radar shown or hidden
  // The mech's power down and up (Mech::finish_power_down, Mech::power):
  // the HUD off with the reticle hidden, then back as it was.
  bool power_down();             // 0x0043bb20: true when it was on
  void power_up(uint8_t saved);  // 0x0043bb50
  void update(float dt) override;    // +0x00  0x0043c820: the HUD's frame
  void draw3d(float dt);             // 0x0043c6b0: the Direct3D pass (from the mech's 0x0041cd90)
  void data_update(float dt);        // 0x0043c390: the game's numbers into the pieces (from 0x0040f8d0)
  void reset();                      // 0x0043be10
  Mech* mech = nullptr;              // +0x10
  HudRadar radar{60.0f};             // +0x14
  HudCompass compass;                // +0xdd4
  HudReticle reticle{"ret_on", "ret_off"};  // +0x144c
  HudGauges gauges;                  // +0x1da8
  HudMfd mfd{0x212, 0x172, 0x4b, 100, false};  // +0x3434 (a fifth argument 1, unused)
  HudTarget target{10, 0x172, 0x4b, 100};  // +0x410c
  HudWeapons weapons;                // +0x6520
  HudObjectives objectives;          // +0x17964
  HudTimer timer;                    // +0x18410
  HudEquipment equipment;            // +0x188c0
  // The static (0x0043cd20, hud.md section 21): TARGET's (+0x22e08), the
  // MFD's (+0x22e14), the weapons' and equipment's (+0x22e20), the radar's
  // fade (+0x22e2c), each a floor, its level and a hold.
  struct Static {
    float floor = 0.0f, level = 0.0f, hold = 0.0f;
  };
  Static statics[4];                 // +0x22e08..+0x22e34
  void static_tick(float dt);        // 0x0043cd20: from the frame's draw paths
  void add_static(float step, int32_t kind, float hold, float most);  // 0x0043bfe0
  void raise_static(float level, int32_t kind, float hold);          // 0x0043c1f0
  zrdr::Node* reader = nullptr;      // +0x22e38 hud.zrd's root during the set-up
  // +0x22e3c the resolution's block during it. Not set by the original's
  // constructor; FIX (bug), behind zfix bugs: cleared here (docs/hud.md).
  zrdr::Node* block;
  // FIX (B), any_resolution (resolution.md step 4): past mode 10 hud.zrd has no
  // block, so V640x480's positions are laid out on the display (the canvas)
  // by each piece's anchor (hud_anchor); 0 while the block is read as it is.
  int32_t anchor_w = 0, anchor_h = 0;
  // A position from the block, anchored when anchor_w is set.
  bool pos(zrdr::Node* n, const char* piece, const char* key, int32_t out[2]) const;
};

// FIX (B) (resolution.md step 4, hud.md section 5): a hud.zrd position of
// V640x480 moved for a display of w x h by its piece's anchor, as the
// original's larger blocks move it: from the right edge (x + w - 640), the
// centre (x + (w - 640) / 2), the bottom (y + h - 480), else as it is. False
// for a key that is not a position (sizes, widths).
bool hud_anchor(const char* piece, const char* key, int32_t w, int32_t h, int32_t p[2]);

}  // namespace mech3
