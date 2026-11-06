#include "mech3/hud.h"

#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

#include "crt/msvc_math.h"
#include "crt/msvc_rand.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zutil/zfix.h"
#include "gamez/ztwod/twod_host.h"
#include "gamez/zvideo/zvid_format.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "mech3/mech_hud.h"
#include "mech3/mech_objective.h"
#include "mech3/message_panel.h"
#include "mech3/options.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
int32_t g_hud_setups = 0;  // ours: Hud::setup's runs (the tests)
}  // namespace

namespace {
constexpr int32_t kWeaponBracket = 11;  // [0x005e3a18] the bracket's half width
constexpr int32_t kWeaponLine = 10;     // [0x005e3a1c] a weapon line's height
}  // namespace

namespace {
DamageSet g_damage;
EquipmentSet g_equipment;
WeaponSet g_weapons;
HudHooks g_default_hooks;
HudHooks* g_hooks = &g_default_hooks;

// The parts' images by the table's order (0x00605888 + 0x20 i) and where each
// sits in the silhouette.
struct PartImages {
  int index;
  const char* names[5];
  int32_t x, y;
};
// In the constructor's order: head, ltorso, rtorso, ctorso, larm, rarm, lleg, rleg.
constexpr PartImages kParts[8] = {
    {5, {"dmg_head_blue", "dmg_head_grn", "dmg_head_yel", "dmg_head_red", "dmg_head_blk"}, 0x1e, 2},
    {4, {"dmg_ltorso_blue", "dmg_ltorso_grn", "dmg_ltorso_yel", "dmg_ltorso_red", "dmg_ltorso_blk"}, 0x2e, 6},
    {0, {"dmg_rtorso_blue", "dmg_rtorso_grn", "dmg_rtorso_yel", "dmg_rtorso_red", "dmg_rtorso_blk"}, 0x13, 6},
    {3, {"dmg_ctorso_blue", "dmg_ctorso_grn", "dmg_ctorso_yel", "dmg_ctorso_red", "dmg_ctorso_blk"}, 0x1f, 0xf},
    {6, {"dmg_larm_blue", "dmg_larm_grn", "dmg_larm_yel", "dmg_larm_red", "dmg_larm_blk"}, 0x3a, 8},
    {2, {"dmg_rarm_blue", "dmg_rarm_grn", "dmg_rarm_yel", "dmg_rarm_red", "dmg_rarm_blk"}, 2, 8},
    {1, {"dmg_lleg_blue", "dmg_lleg_grn", "dmg_lleg_yel", "dmg_lleg_red", "dmg_lleg_blk"}, 0x27, 0x2a},
    {7, {"dmg_rleg_blue", "dmg_rleg_grn", "dmg_rleg_yel", "dmg_rleg_red", "dmg_rleg_blk"}, 7, 0x2a},
};
}  // namespace

DamageSet& damage_set() { return g_damage; }
float g_coolant_shown = 1.0f;  // [0x005e3a14]: one for every HUD, kept across missions
float& coolant_shown() { return g_coolant_shown; }
EquipmentSet& equipment_set() { return g_equipment; }
WeaponSet& weapon_set() { return g_weapons; }
HudHooks& hud_hooks() { return *g_hooks; }
void set_hud_hooks(HudHooks* h) { g_hooks = h ? h : &g_default_hooks; }

int32_t HudHooks::display_w() { return display().w; }
int32_t HudHooks::display_h() { return display().h; }

ztwod::Rect HudHooks::display_rect() {
  const Rect& r = display();
  return {r.x, r.y, r.x_end, r.y_end};
}
ztwod::Rect HudHooks::window_rect() {
  // FIX (B): on a canvas the 2D's window is the display (the window rect is the frame's; resolution.md step 4).
  const Rect& r = canvas_scaled() ? display() : window();
  return {r.x, r.y, r.x_end, r.y_end};
}

void HudHooks::unproject_world(const float screen[3], float out[3]) {
  unported("0x00523930", {zgame::bits(screen[0]), zgame::bits(screen[1]), zgame::bits(screen[2])});
  out[0] = out[1] = out[2] = 0.0f;
}
int32_t HudHooks::segment_list(uint32_t world, const float a[3], const float b[3], zwep::HitList* out) {
  out->count = 0;
  return static_cast<int32_t>(unported("0x004eed90", {world, zgame::bits(a[0]), zgame::bits(a[1]), zgame::bits(a[2]),
                                                      zgame::bits(b[0]), zgame::bits(b[1]), zgame::bits(b[2])}));
}
uint32_t HudHooks::material_soil(zwep::Handle material) {
  return material ? unported("0x00608c00", {id(material)}) : 0;
}

const char* HudHooks::message(int32_t id) {
  unported("0x00576040", {id});
  return nullptr;
}

HudTimer::HudTimer() : FontText("00:00:00.00", 0x244, 0xf) { set_font(ztwod::host().font_find("mech_string")); }

// Mech3.exe 1.2 @ 0x0043ae50: a null name keeps the slot.
void DamageImages::set(const char* const names[5], int32_t px, int32_t py) {
  for (int i = 0; i < 5; ++i)
    if (names[i]) image[i] = ztwod::host().image_get(names[i]);
  x = px, y = py;
}

// Mech3.exe 1.2 @ 0x0043afc0 (also inlined in the constructor): once; the
// background size from the first background found.
void load_damage_set() {
  DamageSet& d = g_damage;
  if (d.loaded) return;
  for (const PartImages& p : kParts) d.parts[p.index].set(p.names, p.x, p.y);
  d.bg[0] = ztwod::host().image_get("dmg_bg_blue");
  d.bg[1] = ztwod::host().image_get("dmg_bg_red");
  d.bg[2] = ztwod::host().image_get("dmg_bg_grn");
  const zimg::Texture* t = d.bg[0] ? d.bg[0] : d.bg[1] ? d.bg[1] : d.bg[2];
  if (t) d.bg_w = t->width, d.bg_h = t->height;
  d.loaded = true;
}

// Mech3.exe 1.2 @ 0x0043f3e0
HudCompass::HudCompass()
    : Object2D(0, 0), tape("hud_compass", 0xf5, 0x13, 0), frame("hud_compass_frame", 0xf5, 0xb, 0), text(nullptr, 0, 0) {
  half = (frame.image ? frame.image->width : 0) / 2;
  src.top = 0;
  src.bottom = tape.image ? tape.image->height : 0;
  set_heading(0.0f);
  tape.src = &src;
  show_for(-1.0f);
}

// Mech3.exe 1.2 @ 0x0043f590: the tape's window around the heading, turned
// half round on a flipped map.
void HudCompass::set_heading(float h) {
  HudHooks& k = *g_hooks;
  if (k.mission_entry() && k.entry_flip(k.mission_entry()) > 0.0f) h = h - 3.1415927f;  // 0x005c0100
  while (h < 0.0f) h = h - -6.2831855f;            // 0x005c0104
  const int32_t x = crt::msvc_ftol(scale * h) + centre;
  src.right = x + half;
  src.left = x - half;
}

HudText::HudText() : FontText(nullptr, 0, 0) { set_font(ztwod::host().font_find("mech_string")); }

// Mech3.exe 1.2 @ 0x0043b170
uint16_t hud_green() { return zvid::pack_bytes(0, 0x40, 0); }

// Mech3.exe 1.2 @ 0x0043a7c0
float hud_opacity() { return ztwod::host().video_mode() != 0 ? 0.6f : 1.0f; }

// Mech3.exe 1.2 @ 0x0043a7e0
HudPanel::HudPanel() : Image(0) {
  color = zvid::pack_bytes(0, 0x40, 0);
  alpha = static_cast<uint8_t>(crt::msvc_ftol(hud_opacity() * 255.0f));  // 0x005c0078
}

// Mech3.exe 1.2 @ 0x0043a8f0
void HudPanel::set_pos(int32_t px, int32_t py) {
  Image::set_pos(px, py);
  const int32_t w = image ? image->width : 0, h = image ? image->height : 0;
  rect = {px, py, px + w, py + h};
  const float fx = static_cast<float>(px), fy = static_cast<float>(py);
  const float x1 = static_cast<float>(w) + fx, y1 = static_cast<float>(h) + fy;
  const float c[4][3] = {{fx, fy, 0.9f}, {fx, y1, 0.9f}, {x1, y1, 0.9f}, {x1, fy, 0.9f}};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 3; ++j) corners[i][j] = c[i][j];
}

// Mech3.exe 1.2 @ 0x0043aaa0
void HudPanel::set_color(uint16_t c) {
  color = c;
  if (colors)
    for (int32_t i = 0; i < image->pixel_count; ++i) colors[i] = color;
}

// Mech3.exe 1.2 @ 0x0043a9d0
void HudPanel::set_alpha(uint8_t a) {
  alpha = a;
  if (!image) return;
  image->alpha = nullptr;
  if (alpha > 0xfd) {
    image->flags = 0x61;  // 0x00517c60
    return;
  }
  image->flags = 0x63;
  image->alpha = alphas;
  if (alphas)
    for (int32_t i = 0; i < image->pixel_count; ++i) alphas[i] = alpha;
}

// Mech3.exe 1.2 @ 0x0043aa60
void HudPanel::set_opacity(float f) {
  set_alpha(static_cast<uint8_t>(crt::msvc_ftol(std::floor(static_cast<double>(f * 255.0f)))));
}

// Mech3.exe 1.2 @ 0x0043a890: the alpha set to 100, then its image (made by
// set_size, owning its colours and alpha) freed.
HudPanel::~HudPanel() {
  set_alpha(100);
  ztwod::host().texture_free(image);
}

// Mech3.exe 1.2 @ 0x0043ab10: the corners at the rect's place (Direct3D's
// depth 0.9); a new image of the colour and alpha, or the old one resized.
void HudPanel::set_size(int32_t w, int32_t h) {
  const float fx = static_cast<float>(rect.left), fy = static_cast<float>(rect.top);
  const float y1 = static_cast<float>(h) + fy, x1 = static_cast<float>(w) + fx;
  const float c[4][3] = {{fx, fy, 0.9f}, {fx, y1, 0.9f}, {x1, y1, 0.9f}, {x1, fy, 0.9f}};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 3; ++j) corners[i][j] = c[i][j];
  rect.bottom = rect.top + h;
  rect.right = rect.left + w;
  const int32_t n = w * h;
  if (!image) {
    auto* t = static_cast<zimg::Texture*>(std::calloc(1, sizeof(zimg::Texture)));  // 0x00517c10
    set_image(t);
    t->flags = 0x61;                                     // 0x00517c60
    colors = static_cast<uint16_t*>(std::malloc(static_cast<size_t>(n) * 2));
    alphas = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(n)));
  } else if (image->pixel_count == n) {
    image->width = static_cast<int16_t>(w);             // 0x00517c90
    image->height = static_cast<int16_t>(h);
    image->width_again = static_cast<int16_t>(w);
    image->pixel_count = static_cast<int16_t>(h) * static_cast<int16_t>(w);
    return;
  } else {
    colors = static_cast<uint16_t*>(std::realloc(colors, static_cast<size_t>(n) * 2));
    alphas = static_cast<uint8_t*>(std::realloc(alphas, static_cast<size_t>(n)));
  }
  image->pixels = colors;                                // 0x00517c70
  image->alpha = alphas;
  if (alphas) image->flags |= 2;
  image->width = static_cast<int16_t>(w);               // 0x00517c90
  image->height = static_cast<int16_t>(h);
  image->width_again = static_cast<int16_t>(w);
  image->pixel_count = static_cast<int16_t>(h) * static_cast<int16_t>(w);
  set_color(color);
  set_alpha(alpha);
}

// Mech3.exe 1.2 @ 0x00446700
HudObjectives::HudObjectives() : TextBox(0x19), scores(0x19) {
  ztwod::Host& h = ztwod::host();
  font_title = h.font_find("rtobjective_title");
  font_incomplete = h.font_find("rtobjective_incomplete");
  font_complete = h.font_find("rtobjective_complete");
  font_scores = h.font_find("rt_mp_scores");
  TextBox::set_font(font_title);
  scores.set_font(font_title);
  panel.show_for(-1.0f);
  panel.set_color(hud_green());
  panel.set_opacity(hud_opacity());
  hide();
}

// Mech3.exe 1.2 @ 0x00446870: the box's size measured and dropped.
void HudObjectives::set_pos(int32_t px, int32_t py) {
  TextBox::set_pos(px, py);
  scores.set_pos(px + 0x104, py);
  total_height();
  max_width();
}

// Mech3.exe 1.2 @ 0x00446820
void HudObjectives::show_for(float seconds) {
  Object2D::show_for(seconds);
  scores.show_for(seconds);
  fill();
}

// Mech3.exe 1.2 @ 0x00446930
void HudObjectives::set_text(const char* format, ...) {
  va_list args;
  va_start(args, format);
  set_text_v(format, args);
  va_end(args);
  scores.set_text("%s", "");  // [0x005f4c90]
}

namespace {
// Mech3.exe 1.2 @ 0x0045e080 (with 1): an objective's text, its nav point's
// "OP: <name>" after it, into 0x40 bytes.
std::string objective_text(const Objective& o) {
  if (!o.nav) return o.desc ? *o.desc : std::string();
  const std::string op = zutl::format_message(0x200, g_hooks->nav_name(o.nav)).substr(0, 0x3f);
  char out[0x40];
  if (o.desc) std::snprintf(out, sizeof out, "%s  %s", o.desc->c_str(), op.c_str());
  else std::snprintf(out, sizeof out, "%s", op.c_str());
  return out;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00446970. The exe hands the text to the line as its
// format; here as the format's argument (a '%' in it would read the stack).
void HudObjectives::add_objective(uint8_t done, const char* text) {
  TextBox::set_text("%s", text);  // vt+0xa0, no blank score
  scores.set_text("%s", g_hooks->message(done ? 0x22f : 0x230));  // "Completed" : "In Progress"
  const ztwod::Handle f = done ? font_complete : font_incomplete;
  line().set_font(f);
  scores.line().set_font(f);
}

// Mech3.exe 1.2 @ 0x00446a20: the text's further lines.
void HudObjectives::add_more(uint8_t done, const char* score, const char* text) {
  TextBox::set_text("%s", text);
  if (score) scores.set_text("%s", score);
  const ztwod::Handle f = done ? font_complete : font_incomplete;
  line().set_font(f);
  scores.line().set_font(f);
}

// Mech3.exe 1.2 @ 0x00446ad0: the mission's header, then each group's title
// and its visible objectives (each line of the text a line), the failed
// ones, the mission's end and how to close it; the panel sized to the lines
// (docs/hud.md, The objectives box).
void HudObjectives::fill() {
  HudHooks& k = *g_hooks;
  const HudHooks::ObjectivesSource src = k.objectives_source();
  const Objectives* o = src.objectives;
  const ObjectiveGroup* primary = o ? o->primary.get() : nullptr;
  const ObjectiveGroup* secondary = o ? o->secondary.get() : nullptr;
  const ObjectiveGroup* final = primary && primary->done ? o->final.get() : nullptr;  // 0x0048fab0
  const ObjectiveGroup* fail = o && o->fail && o->fail->done ? o->fail.get() : nullptr;  // 0x0048fad0
  bool all_done = true;
  const int32_t height = text_height();
  int32_t lines = 0;
  clear();
  scores.clear();
  auto put = [&](const std::string& s) { set_text("%s", s.c_str()); };  // vt+0x7c
  auto msg = [&](int32_t id) { return std::string(k.message(id)); };
  if (o && o->desc) {
    put(*o->desc);
    lines = 1;
  } else {
    switch (src.game_type) {
      case 1:  // "Operation %1!d!, Mission %2!d!"
        put(zutl::format_message_ints(0x177, {src.operation, src.mission}));
        lines = 1;
        break;
      case 2: {
        std::string s;
        if (src.action_name) {
          s = zutl::format_message(0x231, {std::to_string(src.operation).c_str(), src.action_name});
        } else {
          s = zutl::format_message_ints(0x232, {src.operation});
        }
        if (!s.empty()) put(s);
        lines = 1;
        break;
      }
      case 3:  // "Network Game Scores", its columns
        set_font(font_scores);
        put(msg(0xdd5));
        put(msg(0xdd6));
        lines = 2;
        break;
      case 4:  // "T R A I N I N G   %1"
        put(zutl::format_message(0x233, src.training_name ? src.training_name : ""));
        lines = 1;
        break;
      default:
        break;
    }
  }
  // Each visible objective with a text: its lines (strtok on '\n').
  auto items = [&](const ObjectiveGroup& g, bool count_done) {
    for (const std::unique_ptr<Objective>& it : g.items) {
      const uint8_t done = it->done;
      if (count_done) all_done = all_done && done;
      if (!it->visible) continue;
      const std::string text = objective_text(*it);
      if (text.empty()) continue;
      std::string copy = text;
      bool first = true;
      for (char* t = std::strtok(&copy[0], "\n"); t; t = std::strtok(nullptr, "\n")) {
        if (first) add_objective(done, t);
        else add_more(done, "", t);
        first = false;
        ++lines;
      }
    }
  };
  if (primary && !primary->items.empty()) {
    line();
    if (!objective_text(*primary).empty()) {
      put(objective_text(*primary));
      ++lines;
    } else if (src.game_type == 4) {
      put(msg(0x235));  // "P R I M A R Y  O B J E C T I V E S"
      ++lines;
    } else if (src.game_type != 2) {
      put(zutl::format_message_ints(0x236, {src.operation, src.mission}));
      ++lines;
    }
    items(*primary, true);
  }
  if (secondary && !secondary->items.empty()) {
    line();
    if (!objective_text(*secondary).empty()) put(objective_text(*secondary));
    else put(msg(0x237));  // "S E C O N D A R Y  O B J E C T I V E S"
    ++lines;
    items(*secondary, false);
  }
  if (final && !final->items.empty()) {
    put(objective_text(*final));  // "" without a text
    ++lines;
    items(*final, true);
  }
  if (!k.multiplayer_game() && fail && !fail->items.empty()) {
    put("");
    ++lines;
    if (!objective_text(*fail).empty()) put(objective_text(*fail));
    else put(msg(0x238));  // "F A I L E D  O B J E C T I V E S"
    ++lines;
    for (const std::unique_ptr<Objective>& it : fail->items) {
      if (!it->done || !it->visible) continue;
      const std::string text = objective_text(*it);
      if (text.empty()) continue;
      add_more(1, nullptr, text.c_str());
      ++lines;
    }
  }
  const std::string exit_key = k.command_key(0x71);  // Exit Mission
  if (k.multiplayer_game()) {
    set_font(font_scores);
    for (const std::string& s : k.multiplayer_scores()) {
      put(s);
      ++lines;
    }
  } else if (src.aborting) {
    put("");
    put(msg(0x239));  // "M I S S I O N  F A I L U R E"
    put(zutl::format_message(0x23a, exit_key.c_str()));  // "Press %1 To Exit Mission Area"
    lines += 4;
  } else if (all_done) {
    put("");
    put(msg(0x23b));  // "M I S S I O N  C O M P L E T E"
    put(zutl::format_message(0x23a, exit_key.c_str()));
    lines += 3;
  }
  put(zutl::format_message(0x23c, k.command_key(0x43).c_str()));  // "Press %1 To Close Objectives Display"
  ++lines;
  panel.set_size(0x140, lines * height);
}

// Mech3.exe 1.2 @ 0x00446850
void HudObjectives::hide() {
  Object2D::hide();
  scores.hide();
}

// Mech3.exe 1.2 @ 0x00447c40
HudEquipLine::HudEquipLine() : light(0) {
  ztwod::Host& h = ztwod::host();
  EquipmentSet& e = g_equipment;
  if (!e.light[0]) e.light[0] = h.image_get("hud_light_red");
  if (!e.light[1]) e.light[1] = h.image_get("hud_light_yellow");
  if (!e.light[2]) e.light[2] = h.image_get("hud_light_green");
  if (!e.font_red) e.font_red = h.font_find("wepstring_red");
  if (!e.font_green) e.font_green = h.font_find("wepstring_green");
  if (!e.font_yellow) e.font_yellow = h.font_find("wepstring_yellow");
  if (!e.font_grey) e.font_grey = h.font_find("wepstring_grey");
}

// Mech3.exe 1.2 @ 0x00447fe0: the value's right edge at the column, the light after it.
void HudEquipLine::place(int32_t px, int32_t py, int32_t col) {
  x = px;
  y = py;
  value.set_pos(col - 0xf + px, py);
  value.align = 2;
  light.set_pos(col - 9 + px, py + 1);
}

// Mech3.exe 1.2 @ 0x00447e10: empty, the hidden bit kept.
void HudEquipLine::reset() {
  item = -1;
  state = 2;
  flags &= ztwod::kHidden;
}

// Mech3.exe 1.2 @ 0x00448070
HudEquipment::HudEquipment() : Object2D(0, 0) {
  place(0x212, 0x1e, 0);
  read_layout(nullptr);
  show_for(-1.0f);
}

// Mech3.exe 1.2 @ 0x00448140: unmoved when already there.
void HudEquipment::place(int32_t px, int32_t py, int32_t dy) {
  if (below)
    py += dy;
  else
    px += -0xf - column;
  if (px == get_x() && py == get_y()) return;
  x = px;
  y = py;
  dirty();
  panel.set_pos(get_x(), get_y());
  for (HudEquipLine& l : lines) {
    l.place(px, py, column);
    l.reset();
    py += 10;
  }
}

// Mech3.exe 1.2 @ 0x00445760
HudWeaponLine::HudWeaponLine() : light(0), mark(0) {
  ztwod::Host& h = ztwod::host();
  WeaponSet& w = g_weapons;
  if (!w.light[0]) w.light[0] = h.image_get("hud_light_green");
  if (!w.light[1]) w.light[1] = h.image_get("hud_light_yellow");
  if (!w.light[2]) w.light[2] = h.image_get("hud_light_red");
  if (!w.font_red) w.font_red = h.font_find("wepstring_red");
  if (!w.font_green) w.font_green = h.font_find("wepstring_green");
  if (!w.font_yellow) w.font_yellow = h.font_find("wepstring_yellow");
  if (!w.font_grey) w.font_grey = h.font_find("wepstring_grey");
  hide();  // 0x00445dc0 with no weapon
  flags &= ztwod::kHidden;
}

// Mech3.exe 1.2 @ 0x00445970: the bar's fill and empty images are the two
// texts' own, freed with the texts; set to none here, before the Slider's
// destructor (0x0059e170) would release them again.
HudWeaponLine::~HudWeaponLine() {
  fill = nullptr;
  dirty();
  empty = nullptr;
  dirty();
}

// Mech3.exe 1.2 @ 0x00445ba0
void HudWeaponLine::place(int32_t px, int32_t py, int32_t col) {
  Image::set_pos(px, py);
  count_text.set_pos(col - 0xf + px, py);
  light.set_pos(col - 9 + px, py + 1);
  mark.set_pos(px - 10, py + 2);
}

// Mech3.exe 1.2 @ 0x00445a30
void HudWeaponLine::reset() {
  weapon = nullptr;
  red = 1;
  set_look(false, false);
  changed = 1;
  count = 0;
}

// Mech3.exe 1.2 @ 0x00445a60: red, the name red and the rest green; else all
// grey or all yellow.
bool HudWeaponLine::set_look(bool r, bool g) {
  if (red == r && grey == g) return false;
  const WeaponSet& w = g_weapons;
  const ztwod::Handle f1 = r ? w.font_red : g ? w.font_grey : w.font_yellow;
  const ztwod::Handle f2 = r ? w.font_green : f1;
  name.set_named_font(f1, false);
  text2.set_named_font(f2, false);
  if (count_text.set_named_font(f2, false)) count_text.align = 2;
  red = r;
  grey = g;
  changed = 1;
  return true;
}

// Mech3.exe 1.2 @ 0x00445f80
HudWeapons::HudWeapons() : Object2D(0, 0) {
  ztwod::Host& h = ztwod::host();
  select = h.image_get("hud_weapon_select");
  chain = h.image_get("hud_weapon_chain");
  poly.color = zvid::pack_bytes(1, 0xa2, 0x1f);
  set_pos(0x212, 0x1e);
  read_layout(nullptr);
  panel.set_opacity(hud_opacity());
  panel.set_color(zvid::pack_bytes(0, 0x40, 0));  // 0x0043aae0
}

// Mech3.exe 1.2 @ 0x004460c0
void HudWeapons::set_pos(int32_t px, int32_t py) {
  x = px;
  y = py;
  dirty();
  for (HudWeaponLine& l : lines) {
    l.place(px, py, column);
    l.reset();
    py += 10;
  }
}

// Mech3.exe 1.2 @ 0x00446200
void HudWeapons::show_for(float seconds) {
  Object2D::show_for(seconds);
  shown = 1;
  refresh_all();
}

// Mech3.exe 1.2 @ 0x00446520
void HudWeapons::refresh_all() {
  layout();
  looks();
}

// Mech3.exe 1.2 @ 0x00446240: the five groups' weapons fill the lines in
// order; the current group's get the bracket; the rest hidden; the panel
// sized to them and the equipment list put below.
void HudWeapons::layout() {
  HudHooks& h = *g_hooks;
  const int32_t current = h.wm_group_index(weapons);
  const int32_t px = get_x();
  int32_t py = get_y();
  int32_t total = 0;
  poly.hide();
  next_weapon = 0;
  for (int32_t i = 0; i < 5; ++i) {
    void* g = i == -1 ? nullptr : h.wm_group(weapons, i);
    const int32_t n = g ? h.group_count(g) : 0;
    if (n == 0) continue;
    if (current == i) {
      poly.show_for(-1.0f);
      poly.set_point(0, px + kWeaponBracket, py);
      poly.set_point(1, px - kWeaponBracket, py);
      poly.set_point(2, px - kWeaponBracket, n * kWeaponLine + py);
      poly.set_point(3, px + kWeaponBracket, n * kWeaponLine + py);
    }
    for (int32_t k = 0; k < n; ++k) {
      void* w = k < h.group_count(g) ? h.group_weapon(g, k) : nullptr;
      lines[total + k].set_weapon(w, nullptr);
      py += kWeaponLine;
    }
    total += n;
  }
  const int32_t height = kWeaponLine * total;
  for (int32_t i = total; i < 16; ++i) lines[i].hide();
  panel.set_pos(get_x(), get_y());
  panel.set_size(column, height);
  if (hud) {
    Hud* hd = static_cast<Hud*>(hud);
    hd->equipment.place(get_x(), get_y(), height + 6);
    if (!h.group_locks(h.mech_weapons(hd->mech))) hd->reticle.reset();
  }
  panel.show_for(-1.0f);
  shown = 1;
}

// Mech3.exe 1.2 @ 0x00446540: the current group's lines get the select mark
// (or chain for the others in chain fire), the rest none.
void HudWeapons::looks() {
  HudHooks& h = *g_hooks;
  const int32_t gi = h.wm_group_index(weapons);
  void* group = gi == -1 ? nullptr : h.wm_group(weapons, gi);
  shown = 0;
  next_weapon = 0;
  bool after_current = false;
  for (HudWeaponLine& l : lines) {
    if (l.flags & ztwod::kHidden) continue;
    void* w = l.weapon;
    zimg::Texture* mark = nullptr;
    if (after_current) next_weapon = w;
    after_current = w == h.wm_current(weapons);
    if (w && group && h.weapon_group(w) == gi) {
      const int32_t mode = h.wm_mode(weapons);
      if (mode == 0) mark = after_current ? select : chain;
      else if (mode == 2 || after_current) mark = select;
    }
    const bool recycling = l.refresh_line(mark);
    shown = shown || recycling;
  }
  if (!next_weapon) next_weapon = lines[0].weapon;
}

// Mech3.exe 1.2 @ 0x00446220
void HudWeapons::hide() {
  Object2D::hide();
  shown = 0;
}

// Mech3.exe 1.2 @ 0x00448550
void HudBar::set_box(int32_t px, int32_t py, int32_t w, int32_t h) {
  set_point(0, static_cast<float>(px), static_cast<float>(py));
  set_point(1, static_cast<float>(px), static_cast<float>(h + py));
  const float x1 = static_cast<float>(w + 1 + px);
  set_point(2, x1, static_cast<float>(h + py));
  set_point(3, x1, static_cast<float>(py));
  height = h;
  width = w;
}

namespace {
// The gauges' texts: Arial 8, green with a dark green shadow.
void gauge_font(ztwod::FontText& t) {
  t.create_font("Arial", 8, 300, 6, 0, 0, 2);
  if (t.color_top != 0x1fa201 || t.color_bottom != 0x1fa201) {
    t.color_top = t.color_bottom = 0x1fa201;
    t.needs_render = 1;
  }
}
void gauge_shadow(ztwod::FontText& t) {
  t.shadow = 1;
  t.shadow_dx = t.shadow_dy = 1;
  t.shadow_color = 0x184e18;
}
int32_t image_h(const ztwod::Image& i) { return i.image ? i.image->height : 0; }
}  // namespace

// Mech3.exe 1.2 @ 0x00444760
HudGauges::HudGauges()
    : Object2D(0, 0), coolant_text(nullptr, 0, 0), heat_text(nullptr, 0, 0),
      speed_frame("hud_speed_frame", 0x23f, 0x100, 0), coolant_frame("hud_coolant_frame", 8, 0x122, 0),
      fuel_frame("hud_fuel_frame", 0x20f, 0x128, 0), heat_bar("hud_heat_bar", 0x12, 0xa3, 0),
      heat_meter("hud_heat_meter", 0x10, 0xa3, 0), toggler("lab_toggler_green", 10, 0x96, 0) {
  speed_text.set_pos(speed_frame.get_x() + 0x1b, speed_frame.get_y() + 0x30);
  gauge_font(speed_text);
  gauge_shadow(speed_text);
  coolant_text.set_pos(coolant_frame.get_x() + 0x16, coolant_frame.get_y() + 0x21);
  gauge_font(coolant_text);
  coolant_text.set_text_id(0x1d5);
  gauge_shadow(coolant_text);
  heat_text.set_pos(0x1e, 0x115);
  gauge_font(heat_text);
  heat_text.set_text_id(0x1d6);
  gauge_shadow(heat_text);
  speed_bar.set_box(speed_frame.get_x() + 0xf, speed_frame.get_y() + 5, 4, 0x2f);
  coolant_bar.color = zvid::pack_bytes(0, 0, 0xff);
  coolant_bar.set_box(coolant_frame.get_x() + 9, coolant_frame.get_y() + 7, 4, image_h(coolant_frame) - 8);
  fuel_bar.color = zvid::pack_bytes(1, 0xa2, 0x1f);
  fuel_bar.set_box(fuel_frame.get_x() + 0x10, fuel_frame.get_y() + 5, 4, image_h(fuel_frame) - 8);
  toggler_y = heat_meter.get_y() + image_h(heat_meter) - (image_h(toggler) >> 1) - 1;
  heat_src.top = 0;
  heat_bottom = heat_bar.get_y() + image_h(heat_bar);
  heat_src.bottom = image_h(heat_bar);
  heat_src.left = 0;
  heat_src.right = heat_bar.image ? heat_bar.image->width : 0;
  show_for(-1.0f);
}

// The original's calloc(4, n): n 32-bit pointers.
HudLight::HudLight(int32_t n)
    : Image(0), count(n), images(static_cast<zimg::Texture**>(std::calloc(n, sizeof(zimg::Texture*)))) {}
// Mech3.exe 1.2 @ 0x00448640 (inlined in its owners' destructors, 0x00442d00,
// 0x0043d810): each image let go, the last first, then the table.
HudLight::~HudLight() {
  while (count-- != 0) ztwod::host().image_release(images[count]);  // 0x00517b10
  std::free(images);
  images = nullptr;
}

void HudLight::select(int32_t i) {
  if (current == i) return;
  current = i;
  set_image(images[i]);
}

// Mech3.exe 1.2 @ 0x004486e0
void HudLight::update(float dt) {
  if (interval != 0.0f) {
    time += dt;
    const uint32_t i = static_cast<uint32_t>(crt::msvc_ftol(std::floor(time / interval))) % static_cast<uint32_t>(count);
    if (static_cast<uint32_t>(current) != i) {
      current = static_cast<int32_t>(i);
      set_image(images[i]);
    }
  }
  Image::update(dt);
}

namespace {
constexpr float kLockWait = 0.6f;  // [0x005e3a0c]
ztwod::ClipLine2D clip_line() { return ztwod::ClipLine2D(0, 0, 0, 0, 0); }  // 0x0043d620
constexpr int32_t kRadarStep = 10;  // [0x005e39c0]: pixels a zoom step
}  // namespace

// Mech3.exe 1.2 @ 0x0043d260
HudRadar::HudRadar(float fov) : FilledBox(0, 0, 0, 0, nullptr), lines{clip_line(), clip_line(), clip_line()} {
  color = zvid::pack_bytes(0, 0xff, 0);
  source = nullptr;
  if (text1.color_top != 0xff00 || text1.color_bottom != 0xff00) {
    text1.color_top = text1.color_bottom = 0xff00;
    text1.needs_render = 1;
  }
  lines[0].color = zvid::pack_bytes(0, 0xff, 0);
  lines[1].color = zvid::pack_bytes(0, 0xff, 0);
  lines[2].color = zvid::pack_bytes(0xff, 0, 0);
  ztwod::Host& h = ztwod::host();
  light.images[0] = h.image_get("hud_light_green");
  light.images[1] = h.image_get("hud_light_yellow");
  light.images[2] = h.image_get("hud_light_red");
  light.select(0);
  light.set_pos(0xb, 0xb);
  active = 0;
  HudRadar::hide();
  for (auto& l : lines) l.hide();
  r[0] = 0x32;
  r[1] = 0xb4;
  cx[0] = 0x37;
  cy[0] = 0x37;
  cx[1] = 0x140;
  cy[1] = 0xf0;
  r_steps = x_steps = y_steps = 0;
  to_r = to_x = to_y = 0;
  x58 = 0;
  set_zoom(false);
  x = cx[0];
  y = cy[0];
  dirty();
  radius = 0;
  radius_sq = 0;
  set_fov(static_cast<float>(static_cast<double>(fov) * 0.017453292519943295));  // 0x005c00c8
  const float down[3] = {0.0f, 0.0f, -1.0f};
  set_dir(down);
  text2.set_pos(cx[0] - r[0], cy[0] + r[0] + 0xf);
  text2.shadow = 1;
  text2.shadow_dx = text2.shadow_dy = 1;
  text2.shadow_color = 0x184e18;
  if (text2.color_top != 0x1fa201 || text2.color_bottom != 0x1fa201) {
    text2.color_top = text2.color_bottom = 0x1fa201;
    text2.needs_render = 1;
  }
  xdb8 = xdb4 = 0;
  xbd8 = xbd4 = 0;
}

// Mech3.exe 1.2 @ 0x0043d920 (from the mission start, 0x00491696): the
// polylines' point lists from the mission boundary's regions, the inner one
// yellow, the outer red (hud.md section 15).
void HudRadar::reset_boundary() {
  HudHooks& h = *g_hooks;
  xbd8 = xbd4 = 0;
  xdb8 = xdb4 = 0;
  xdb4 = h.radar_boundary(0);
  xdb8 = h.radar_boundary(1);
  if (xdb4) {
    xbd4 = h.radar_point_count(xdb4);
    polys[0].color = zvid::pack_bytes(0xc3, 0xad, 3) & 0xffff;
  }
  if (xdb8) {
    xbd8 = h.radar_point_count(xdb8);
    polys[1].color = zvid::pack_bytes(0xff, 0, 0) & 0xffff;
  }
}

// Mech3.exe 1.2 @ 0x0043dc00
void HudRadar::hide() {
  to_r = 0;
  changed = 1;
}

// Mech3.exe 1.2 @ 0x0043da00: over the mission map, the map named for the
// size ("large" zoomed, else "small"), else any.
uint32_t HudRadar::select_map() {
  if (!active) return 0;
  HudHooks& k = *g_hooks;
  uint32_t m = k.map_find(zoomed == 1 ? "large" : "small");
  if (!m) m = k.map_find(nullptr);
  k.set_radar_map(m);
  return m;
}

// Mech3.exe 1.2 @ 0x0043da40
void HudRadar::toggle_zoom() { set_zoom(zoomed == 0); }

// Mech3.exe 1.2 @ 0x0043dc10: turned back off when no map is found; off,
// straight to the size and place it steps toward.
void HudRadar::toggle_map() {
  active = !active;
  if (active && !select_map()) active = !active;
  if (!active) {
    radius = to_r;
    radius_sq = to_r * to_r;
    set_pos(to_x, to_y);
  }
  changed = 1;
}

// Mech3.exe 1.2 @ 0x0043da50: the size to move to and the steps there.
void HudRadar::set_zoom(bool on) {
  zoomed = on ? 1 : 0;
  to_r = r[zoomed];
  to_x = cx[zoomed];
  to_y = cy[zoomed];
  x_steps = std::abs((to_x - get_x()) / kRadarStep);
  y_steps = std::abs((to_y - get_y()) / kRadarStep);
  r_steps = std::abs((to_r - radius) / kRadarStep);
  if (source) g_hooks->radar_zoom_sound(source, zoomed != 0);
  select_map();
  changed = 1;
}

// Mech3.exe 1.2 @ 0x0043f0c0
void HudRadar::set_fov(float a) {
  half_fov = a * 0.5f;
  cos_half = static_cast<float>(std::cos(static_cast<double>(half_fov)));
  sin_half = static_cast<float>(std::sin(static_cast<double>(half_fov)));
}

// Mech3.exe 1.2 @ 0x0043f130: on a flipped map the direction turned round.
void HudRadar::set_dir(const float* d) {
  if (d) dir[0] = d[0], dir[1] = d[1], dir[2] = d[2];
  float v[3] = {dir[0], dir[1], dir[2]};
  if (active) {
    HudHooks& k = *g_hooks;
    if (k.entry_flip(k.mission_entry()) > 0.0f) v[0] = -v[0], v[1] = -v[1], v[2] = -v[2];  // 0x005c007c
  }
  const float rr = static_cast<float>(radius);
  int32_t y1 = get_y() + crt::msvc_ftol((v[2] * cos_half - v[0] * sin_half) * rr);
  int32_t x1 = get_x() + crt::msvc_ftol((v[2] * sin_half + v[0] * cos_half) * rr);
  lines[0].set(get_x(), get_y(), x1, y1);
  y1 = get_y() + crt::msvc_ftol((v[0] * sin_half + v[2] * cos_half) * rr);
  x1 = get_x() + crt::msvc_ftol((v[0] * cos_half - v[2] * sin_half) * rr);
  lines[1].set(get_x(), get_y(), x1, y1);
}

// Mech3.exe 1.2 @ 0x004b5440
HudViewWindow::HudViewWindow(const char* name) {
  HudHooks& k = *g_hooks;
  node = k.window_node_new();
  k.node_set_name(node, name);
  set_area(ztwod::Rect{});
}

// Inlined (0x004b5440, 0x0043f660): the ratio of the area to its target, then
// the node's size and place.
void HudViewWindow::set_area(const ztwod::Rect& r) {
  area = r;
  target = r;
  scaled = !(target.right == area.right && target.bottom == area.bottom && target.left == area.left &&
             target.top == area.top);
  if (scaled) {
    sx = static_cast<float>(area.right - area.left) / static_cast<float>(target.right - target.left);
    sy = static_cast<float>(area.bottom - area.top) / static_cast<float>(target.bottom - target.top);
    isx = 1.0f / sx;  // 0x005c91a0
    isy = 1.0f / sy;
  }
  int32_t x = area.left, y = area.top, w = area.right - area.left, h = area.bottom - area.top;
  canvas_to_render(&x, &y, &w, &h);  // FIX (B): the 3D view's window on the frame, the box on the canvas (resolution.md step 4)
  g_hooks->window_set_size(node, w, h);
  g_hooks->window_set_pos(node, x, y);
}

// Mech3.exe 1.2 @ 0x004b5150
HudZView::HudZView(const char* name, HudViewWindow* w, uint32_t wd) {
  HudHooks& k = *g_hooks;
  camera = k.camera_node_new();
  k.node_set_name(camera, name);
  x10 = 0;
  window = w;
  k.camera_set_window(camera, w ? w->node : 0);
  world = wd;
  k.camera_set_world(camera, k.world_node(wd));
  hardware = ztwod::host().video_mode();
  k.camera_set_zone(camera, 0xffffff01);  // [0x005e8854]: the default zone
}

// Mech3.exe 1.2 @ 0x0043f9c0: the window the constructor made is freed (the
// port had left it, one each zoom view; MW3 frees it too).
HudZoomView::~HudZoomView() { delete view.window; }

// Mech3.exe 1.2 @ 0x0043f660
HudZoomView::HudZoomView(int32_t px, int32_t py, int32_t w, int32_t h)
    : Object2D(0, 0), view("camx", new HudViewWindow("winx"), g_hooks->world()) {
  rect = {px, py, px + w, py + h};
  view.window->set_area(rect);
  HudHooks& k = *g_hooks;
  k.camera_set_zoom(view.camera, 1.0f);
  k.camera_set_fov(view.camera, 0.87266463f, 1.0471976f);  // 50 and 60 degrees
  k.camera_set_clip(view.camera, 1.0f, 500.0f);
  source = nullptr;
  HudZoomView::hide();
}

// Mech3.exe 1.2 @ 0x0043fc10
void HudZoomView::show_for(float seconds) {
  Object2D::show_for(seconds);
  g_hooks->node_set_active(view.camera, true);
  if (x4c != 0xffff) g_hooks->unported("0x0043fc60.light", {x4c});
}

// Mech3.exe 1.2 @ 0x0043fc60
void HudZoomView::hide() {
  Object2D::hide();
  g_hooks->node_set_active(view.camera, false);
  if (x4c != 0xffff) g_hooks->unported("0x0043fc60.light", {x4c});  // a light's flag (0x004b5560); never at build
}

// Mech3.exe 1.2 @ 0x00442620
HudMfdView::HudMfdView(int32_t px, int32_t py, int32_t w, int32_t h) : HudZoomView(px, py, w, h) {
  g_hooks->camera_set_flag8(view.camera, true);
  panel.set_size(w, h);
  panel.set_pos(px, py);
  panel.set_color(zvid::pack_bytes(0, 0x40, 0));  // 0x0043aae0
  panel.set_opacity(hud_opacity());
  box.set_point(0, px, py);
  box.set_point(1, px + w, py);
  box.set_point(2, px + w, py + h);
  box.set_point(3, px, py + h);
  box.set_point(4, px, py);
  box.color = zvid::pack_bytes(1, 0xa2, 0x1f);
  box.show_for(-1.0f);
  panel.show_for(-1.0f);
  show_for(-1.0f);
}

// Mech3.exe 1.2 @ 0x00447460: at the part's place in the silhouette, blue.
HudDamagePart::HudDamagePart(DamageImages* d, int32_t px, int32_t py) : FlashImage(0), images(d) {
  Image::set_pos(d->x + px, d->y + py);
  set_image(d->image[0]);
}

namespace {
// 0x0043afc0 on a part's entry: the set loaded, the entry back.
DamageImages* loaded(int i) {
  load_damage_set();
  return &g_damage.parts[i];
}
// The parts by the table's index: own head, rtorso, ltorso, ctorso, rarm,
// larm, rleg, lleg; a target's head, ltorso, rtorso, ctorso, larm, rarm, lleg, rleg.
constexpr int kOwnOrder[8] = {5, 0, 4, 3, 2, 6, 7, 1};
constexpr int kTargetOrder[8] = {5, 4, 0, 3, 6, 2, 1, 7};
const int* order(bool t) { return t ? kTargetOrder : kOwnOrder; }
}  // namespace

// Mech3.exe 1.2 @ 0x00447680, 0x00447820
HudDamageView::HudDamageView(int32_t px, int32_t py, bool t)
    : Object2D(0, 0), bg(static_cast<zimg::Texture*>(nullptr), px, py, 0),
      parts{{loaded(order(t)[0]), px, py}, {loaded(order(t)[1]), px, py}, {loaded(order(t)[2]), px, py},
            {loaded(order(t)[3]), px, py}, {loaded(order(t)[4]), px, py}, {loaded(order(t)[5]), px, py},
            {loaded(order(t)[6]), px, py}, {loaded(order(t)[7]), px, py}} {
  rect = {px, py, px + g_damage.bg_w, py + g_damage.bg_h};
}

// Mech3.exe 1.2 @ 0x00444280: on, off, over, can't (+0x914, +0x910, +0x918,
// +0x91c; +0x920 is ret_on again, not let go), each when there.
HudReticle::~HudReticle() {
  for (zimg::Texture** p : {&img_on, &img_off, &img_over, &img_cant})
    if (*p) *p = ztwod::host().image_release(*p);  // 0x00517b10: 0
}

// Mech3.exe 1.2 @ 0x00440190: every arrow let go (0x00517b10, an empty one too).
HudTarget::~HudTarget() {
  for (zimg::Texture*& a : arrows) {
    ztwod::host().image_release(a);
    a = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x004422b0
HudMfd::~HudMfd() {
  if (nav) g_hooks->nav_list_free(nav);
  delete view;
}

// Mech3.exe 1.2 @ 0x00442060 (target false), 0x00441da0 (true)
HudMfd::HudMfd(int32_t px, int32_t py, int32_t w, int32_t h, bool t) : Object2D(0, 0), damage(px, py, t) {
  HudHooks& k = *g_hooks;
  view = new HudMfdView(px, py, w, h);
  nav = k.nav_list_new(px, py + 0xe, h - 0xf, 5, 5);  // [0x005e39f8] / [0x005e39f0] and the next: 5
  damage.bg.set_image(g_damage.bg[0]);
  title.set_text("%s", k.message(k.multiplayer_game() ? 0xfbb : 0xdbe));
  title.set_pos(px, py - title.text_height() - 1);
  if (title.color_top != 0xff || title.color_bottom != 0xff) {
    title.color_top = title.color_bottom = 0xff;
    title.needs_render = 1;
  }
  title.show_for(-1.0f);
  mode = k.saved_mfd_mode();
  if (!mode) mode = ztwod::host().video_mode() != 0 ? 1 : 3;  // 0x00441d90
}

// Mech3.exe 1.2 @ 0x004475c0: two bands (above 0.2, above 0) share the
// fourth image; the fifth only at 0 or below.
void HudDamagePart::set_level(float v) {
  if (v == 1.0f) set_image(images->image[0]);
  else if (v > 0.7f) set_image(images->image[1]);  // [0x005e3a20]
  else if (v > 0.4f) set_image(images->image[2]);  // [0x005e3a24]
  else if (v > 0.2f) set_image(images->image[3]);  // [0x005e3a28]
  else if (v > 0.0f) set_image(images->image[3]);
  else set_image(images->image[4]);
}

// Mech3.exe 1.2 @ 0x00447a50: head, the torso's right, left and centre,
// the arms, the legs, into the parts in their order.
void HudDamageView::update_levels() {
  if (!mech) return;
  HudHooks& h = *g_hooks;
  static constexpr struct {
    uint32_t part;
    int32_t section;
  } kFrom[8] = {{0x3a4, 0}, {0x39c, 1}, {0x39c, 2}, {0x39c, 0}, {0x394, 0}, {0x398, 0}, {0x38c, 0}, {0x390, 0}};
  for (int i = 0; i < 8; ++i) parts[i].set_level(h.part_internal(mech, kFrom[i].part, kFrom[i].section));
}

// Mech3.exe 1.2 @ 0x00442420
void HudMfd::set_mode(int32_t m) {
  mode = m;
  if (m) g_hooks->save_mfd_mode(m);
}

// Mech3.exe 1.2 @ 0x00442440: the next of the 3D view, the damage and the nav
// list, only while the mode is the one shown (TARGET without a live target
// shows none); at pixel doubling straight to the nav list.
void HudMfd::cycle_mode() {
  if (pixel_double()) {
    mode = 3;
    return;
  }
  const int32_t m = mode;
  if (m != shown_mode()) return;
  switch (m) {
    case 0: case 3: set_mode(1); break;
    case 1: set_mode(2); break;
    case 2: set_mode(3); break;
    default: break;
  }
}

// Mech3.exe 1.2 @ 0x004424b0
void HudMfd::data_update(float) {
  switch (shown_mode()) {
    case 2: damage.update_levels(); break;
    case 3: hud_hooks().nav_list_target(nav, hud ? static_cast<Hud*>(hud)->mech : nullptr); break;
    default: break;
  }
}

// Mech3.exe 1.2 @ 0x00447c00
bool HudDamageView::set_mech(void* m) {
  mech = m;
  if (m) {
    show_for(-1.0f);
    return true;
  }
  hide();
  return false;
}

namespace {
ztwod::Line2D zero_line() { return ztwod::Line2D(0, 0, 0, 0, 0); }  // 0x004400e0
const char* const kArrows[12] = {
    "hud_enarrow_left",     "hud_enarrow_right",     "hud_enarrow_up",     "hud_enarrow_down",
    "hud_allyarrow_left",   "hud_allyarrow_right",   "hud_allyarrow_up",   "hud_allyarrow_down",
    "hud_othererrow_left",  "hud_othererrow_right",  "hud_othererrow_up",  "hud_othererrow_down"};
void set_colors(ztwod::FontText& t, uint32_t c) {
  if (t.color_top != c || t.color_bottom != c) {
    t.color_top = t.color_bottom = c;
    t.needs_render = 1;
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x0043fcb0
HudTarget::HudTarget(int32_t px, int32_t py, int32_t w, int32_t h)
    : HudMfd(px, py, w, h, true), lines{zero_line(), zero_line(), zero_line()} {
  target = nullptr;
  texts[0].set_text_id(0x22a);
  line_h = texts[0].text_height();
  texts[0].set_pos(px, py - line_h * 2 - 1);
  texts[1].set_pos(px, py - line_h - 1);
  texts[2].set_pos(px, py - line_h * 3 - 1);
  set_colors(texts[0], 0xff00);
  texts[0].set_text("");  // [0x005f4c90]
  set_colors(texts[1], 0xff00);
  texts[1].set_text("");
  set_colors(texts[2], 0xff0000);
  texts[2].set_text("");
  ztwod::Host& hs = ztwod::host();
  for (int i = 0; i < 12; ++i) arrows[i] = hs.image_get(kArrows[i]);
  shape1.hide();
  for (auto& l : lines) l.hide();
  marker.hide();
  for (int i = 0; i < 3; ++i) texts[i].hide();
  set_target(nullptr);
  mode = g_hooks->saved_target_mode();  // 0x0043fca0
  if (!mode) mode = ztwod::host().video_mode() != 0 ? 1 : 3;
}

// Mech3.exe 1.2 @ 0x004415f0
void HudTarget::hide() {
  Object2D::hide();
  for (auto& l : lines) l.hide();
  for (int i = 0; i < 3; ++i) texts[i].hide();
}

// Mech3.exe 1.2 @ 0x004400f0
void HudTarget::set_mode(int32_t m) {
  mode = m;
  if (m && m != g_hooks->saved_target_mode()) g_hooks->save_target_mode(m);
}

// Mech3.exe 1.2 @ 0x00441560
void HudTarget::show_for(float seconds) {
  Object2D::show_for(seconds);
  for (auto& l : lines) l.show_for(-1.0f);
  for (int i = 0; i < 3; ++i) texts[i].show_for(-1.0f);
}

// Mech3.exe 1.2 @ 0x00441800: the slot TARGET reads (the mech's +0x6c).
void HudTarget::set_target(void* t) {
  target = t;
  if (g_hooks->target_valid(t)) {
    show_for(-1.0f);
    set_shown(true);
    return;
  }
  hide();
  set_shown(false);
}

// Mech3.exe 1.2 @ 0x00441780: with a live target, the triangle's pulse
// (0.3 s) and the reticle reset.
void HudTarget::set_shown(bool on) {
  shown = on && g_hooks->target_valid(target);
  if (shown) {
    x240c = 0.3f;  // 0x3e99999a
    static_cast<Hud*>(hud)->reticle.reset();
    update_texts(false);
    return;
  }
  x240c = 0.0f;
  update_texts(true);
}

namespace {
// A text's two colours set, re-rendered only on a change.
void text_colour(HudText& t, uint32_t c) {
  if (t.color_top == c && t.color_bottom == c) return;
  t.color_top = t.color_bottom = c;
  t.needs_render = 1;
}
// CString::Format's %s of none.
const char* or_null(const char* s) { return s ? s : "(null)"; }
}  // namespace

// Mech3.exe 1.2 @ 0x00441850 (from the HUD's data update): the second text
// is "mech: loadout" (a network mech's only its mech, a vehicle its name), its
// distance and the player's aimed part; the third the player's saved or ally
// target's mech; the texts, the view's box and the silhouette's background
// coloured by the target's side (hud.md section 16).
void HudTarget::data_text() {
  HudHooks& h = *g_hooks;
  if (!h.target_valid(target)) {
    hide();
    update_texts(true);
    return;
  }
  show_for(-1.0f);
  update_texts(false);
  void* c = h.slot_contact(target);
  const float dist = h.contact_distance(c);
  void* m = h.contact_mech(c);
  std::string a;
  char buf[128];
  if (!m) {
    const char* n = h.object_name(h.contact_vehicle(c));
    a = n ? n : "";
  } else if (!h.mech_remote(m)) {
    const char* config = h.mech_config_name(m);
    std::snprintf(buf, sizeof buf, "%s: %s", or_null(h.mech_display_name(h.mech_kind_of(m))), or_null(config));  // [0x005e4024]
    a = buf;
  } else {
    const char* n = h.mech_display_name(h.mech_kind_of(m));
    a = n ? n : "";
  }
  if (dist > 1000.0f) {  // [0x005c00ec]
    // FIX (bug): " (1.23km)" as " (500m)" below; the exe runs "1.23km" straight on (fixes.md).
    std::snprintf(buf, sizeof buf, zfix::options().bugs ? " (%.2fkm)" : "%.2fkm",  // [0x005e401c]
                  static_cast<double>(dist * 0.001f));  // [0x005c0120]
  } else {
    std::snprintf(buf, sizeof buf, " (%dm)", crt::msvc_ftol(std::ceil(static_cast<double>(dist))));  // [0x005e4014]
  }
  a += buf;
  void* v = h.contact_vehicle(c);
  Mech* me = static_cast<Hud*>(hud)->mech;
  const int32_t part = h.mech_aim_part(me);
  if (h.part_by_id(me, part)) {
    std::snprintf(buf, sizeof buf, "[%s]", or_null(h.part_display_name(part)));  // [0x005e400c]
    a += buf;
  }
  texts[1].set_text_plain(a.c_str());
  void* sc = h.saved_or_ally(me);
  void* sm = sc ? h.contact_mech(sc) : nullptr;
  const char* sn = sm ? h.mech_object_name(sm) : nullptr;
  const char* t2 = sn ? h.message_arg(0xdd4, sn) : nullptr;
  texts[2].set_text_plain(t2 ? t2 : "");
  zimg::Texture* bg;
  if (!v) {
    view->box.color = zvid::pack_bytes(0xc3, 0xad, 3);
    bg = g_damage.bg[2];
  } else if (h.object_team(v) & h.mech_allies(me)) {
    view->box.color = zvid::pack_bytes(0, 0, 0xff);
    text_colour(texts[0], 0xff0000);  // COLORREFs: blue
    text_colour(texts[1], 0xff0000);
    text_colour(texts[2], 0xff0000);
    bg = g_damage.bg[0];
  } else if (h.object_team(v) & h.mech_enemies(me)) {
    view->box.color = zvid::pack_bytes(0xff, 0, 0);
    text_colour(texts[0], 0xff);
    text_colour(texts[1], 0xff);
    text_colour(texts[2], 0xff0000);
    bg = g_damage.bg[1];
  } else {
    view->box.color = zvid::pack_bytes(1, 0xa2, 0x1f);
    text_colour(texts[0], 0xff00);
    text_colour(texts[1], 0xff);
    text_colour(texts[2], 0xff0000);
    bg = g_damage.bg[2];
  }
  damage.bg.set_image(bg);
}

// Mech3.exe 1.2 @ 0x00441660: with a live target, its mech into the damage
// view and the nav list and the saved (or default) mode; a dead one's none;
// the first text the target's name.
void HudTarget::update_texts(bool clear) {
  HudHooks& h = *g_hooks;
  const char* name = nullptr;
  if (!clear && h.target_valid(target)) {
    void* o = h.target_object(target);  // 0x004e6fe0
    if (!h.object_a8(o)) {
      if (mode == 0) {
        mode = h.saved_target_mode();  // 0x0043fca0
        if (!mode) mode = ztwod::host().video_mode() != 0 ? 1 : 3;  // 0x00441d90
      }
      damage.set_mech(h.object_mech_ptr(o));
      set_mode(mode);  // vt+0x7c
      if (nav) h.nav_list_attach(nav, static_cast<Mech*>(h.object_mech_ptr(o)));
    } else {
      set_mode(0);
      damage.set_mech(nullptr);
    }
    name = h.object_name(o);
  } else {
    set_mode(0);
    damage.set_mech(nullptr);
  }
  texts[0].set_text_plain(name ? name : "");  // [0x005f4c90]
}

// Mech3.exe 1.2 @ 0x00440a30: a NARCed target counts as under the reticle
// wherever it is; else the point on TARGET's shape or circle.
bool HudTarget::over(int32_t px, int32_t py) {
  HudHooks& h = *g_hooks;
  bool narc = false;
  if (h.target_valid(target) && h.contact_mech(h.slot_contact(target)))
    narc = h.mech_narc(h.contact_mech(h.slot_contact(target))) > 0.0f;
  if (narc) return true;
  if (!(shape1.flags & ztwod::kHidden) && shape1.contains(px, py)) return true;
  return !(frame.flags & ztwod::kHidden) && frame.contains(px, py);
}

bool HudHooks::reticle_over_target(void* t, int32_t x, int32_t y) { return static_cast<HudTarget*>(t)->over(x, y); }

// Mech3.exe 1.2 @ 0x00442ac0
HudZoomWindow::HudZoomWindow(int32_t px, int32_t py, int32_t w, int32_t h) : HudZoomView(px, py, w, h) {
  base_w = w;
  base_h = h;
  HudZoomWindow::hide();
  set_rect(px, py, w, h);
  light.show_for(-1.0f);
  text.hide();
  HudHooks& k = *g_hooks;
  k.node_set_name(view.camera, "zoomcam");
  k.camera_set_fov(view.camera, 0.08726646f, 0.08726646f);  // 5 degrees
  float near_z, far_z;
  k.player_camera_clip(&near_z, &far_z);
  k.camera_set_clip(view.camera, near_z, far_z);
  x174 = 0;
  k.camera_set_zoom(view.camera, 10.0f);
  light.center = 1;
  ztwod::Host& hs = ztwod::host();
  light.images[0] = hs.image_get("hud_zoomoff");
  light.images[1] = hs.image_get("hud_zoomon");
  light.images[2] = hs.image_get("hud_zoomover");
  light.images[3] = hs.image_get("hud_zoomcant");
}

// Mech3.exe 1.2 @ 0x00443880
void HudZoomWindow::hide() {
  HudZoomView::hide();
  if (x4c != 0xffff) g_hooks->unported("0x0043fc60.light", {x4c});
  text.hide();
  x174 = 0;
}

// Mech3.exe 1.2 @ 0x004438d0
void HudZoomWindow::show_for(float seconds) {
  HudZoomView::show_for(seconds);
  text.hide();
}

// Mech3.exe 1.2 @ 0x004436c0 (it returns 1, unread).
void HudZoomWindow::place_camera() {
  if ((flags & ztwod::kHidden) && zoom == 0.0f) return;
  HudHooks& h = *g_hooks;
  Hud* hd = static_cast<Hud*>(source);  // +0x168
  const zwep::HitRecord& hit = hd->reticle.hit;  // 0x00444330
  float place[3], angles[3];
  h.player_camera_place(place);
  h.camera_set_position(view.camera, place);
  const float to[3] = {hit.point.x, hit.point.y, hit.point.z};
  zmth::direction_angles(place, to, angles);
  h.camera_set_angles(view.camera, angles);
  h.camera_set_zone(view.camera, h.player_camera_zone());
  text.show_for(-1.0f);
  float distance = hd->reticle.aim_distance;  // 0x00444690
  const char* name = nullptr;
  if (hit.node) {
    // The aimed object, when it is the mech's target: the contact's distance.
    void* slot = h.mech_target_slot(hd->mech);
    if (void* contact = slot ? h.slot_contact(slot) : nullptr) {
      void* aimed = h.node_object(hit.node);  // 0x0040eee0
      if (h.target_object(slot) == aimed) distance = h.contact_distance(contact);  // 0x004e6fe0, 0x004e7030
    }
    if (h.node_object(hit.node)) {  // asked again for its name (0x004437e1, 0x004437ee)
      name = h.object_name(h.node_object(hit.node));  // vt+0xa4
      if (name && !*name) name = nullptr;
    }
  }
  char d[32];
  std::snprintf(d, sizeof d, "%.1f", static_cast<double>(distance));  // 0x005e406c
  if (name) text.set_text_id(0x22d, name, d);
  else text.set_text_id(0x22e, d);
}

// Mech3.exe 1.2 @ 0x00442f50. The zoom camera placed and the player's own
// mech off while it draws: a box is its rect's depth cleared, then the view;
// a circle masks its four corners first, with quarter fans of the clear
// texture at depth 0.99 (0x0057bd50), and clears the depth again after.
void HudZoomWindow::draw3d(float dt) {
  if ((flags & ztwod::kHidden) && zoom == 0.0f) return;
  HudHooks& h = *g_hooks;
  place_camera();
  Hud* hd = static_cast<Hud*>(source);
  const uint32_t me = h.object_node(hd->mech);
  h.node_set_active(me, false);
  int32_t x, y, w, ht;
  h.camera_window_rect(view.camera, &x, &y, &w, &ht);
  const ztwod::Rect r{x, y, x + w - 1, y + ht - 1};
  if (ztwod::host().video_mode() != 0) {  // without a device, the software path (not ported)
    if (round) {
      const float hw = static_cast<float>(w) * 0.5f, hh = static_cast<float>(ht) * 0.5f;  // 0x005c00f4
      const float cx = static_cast<float>(x) + hw, cy = static_cast<float>(y) + hh;
      h.dirty_rect(&r);
      h.scene_begin();
      for (int32_t q = 0; q < 4; ++q) {
        const float sx = (q & 1) ? -1.0f : 1.0f, sy = (q & 2) ? -1.0f : 1.0f;
        float pts[21];
        float a = 0.0f;
        for (int i = 0; i < 6; ++i) {
          const float c = static_cast<float>(std::cos(static_cast<double>(a)));
          const float s = static_cast<float>(std::sin(static_cast<double>(a)));
          pts[3 * i] = c * sx * hw + cx;
          pts[3 * i + 1] = s * sy * hh + cy;
          pts[3 * i + 2] = 0.99f;  // 0x3f7d70a4
          a = a - -0.31415927f;    // 0x005c0130: a tenth of a turn's half
        }
        pts[18] = sx * hw + cx, pts[19] = sy * hh + cy, pts[20] = 0.99f;
        h.mask_fan(pts, 7);
      }
      h.scene_end();
      render_view(dt);
      h.dirty_rect(&r);
    } else {
      h.dirty_rect(&r);
      render_view(dt);
    }
  }
  h.node_set_active(me, true);
}

// Mech3.exe 1.2 @ 0x00443320 (Direct3D; without a device the software
// path, not ported).
void HudZoomWindow::render_view(float dt) {
  HudHooks& h = *g_hooks;
  const uint32_t world = h.world_node(view.world);  // [[+0x0c] +4]
  float near_range, far_range;
  h.world_fog_range(world, &near_range, &far_range);
  h.world_set_fog_range(world, (far_range + near_range) * 0.5f, far_range);  // 0x005c00f4
  h.world_push_fog(world);
  h.zview_render(this, dt);  // 0x004f7030 on its camera
  h.world_set_fog_range(world, near_range, far_range);
  h.world_push_fog(world);
}

// Mech3.exe 1.2 @ 0x00442e00
void HudZoomWindow::toggle_lock() {
  x174 = !x174;
  if (x174) show_for(-1.0f);
  else hide();
}

// Mech3.exe 1.2 @ 0x004438f0
void HudZoomWindow::set_rect(int32_t px, int32_t py, int32_t w, int32_t h) {
  int32_t sw = crt::msvc_ftol(static_cast<float>(w) * zoom);
  int32_t sh = crt::msvc_ftol(static_cast<float>(h) * zoom);
  if (sw < 2) sw = 2;
  if (sh < 2) sh = 2;
  px += (w - sw) / 2;
  py += (h - sh) / 2;
  const ztwod::Rect r = g_hooks->window_rect();
  if (px < r.left) px = r.left;
  if (px + sw > r.right) px = r.right - sw;
  if (py < r.top) py = r.top;
  if (py + sh > r.bottom) py = r.bottom - sh;
  set_view_rect(&px, &py, &sw, &sh);
  light.set_pos(sw / 2 + px, sh / 2 + py);
  text.set_pos(px - 1, py + sh + 1);
  if (round) {
    frame.set_pos(sw / 2 + px, sh / 2 + py);
    frame.radius = sw / 2;
    frame.radius_sq = frame.radius * frame.radius;
    frame.color = zvid::pack_bytes(1, 0xa2, 0x1f);
    return;
  }
  const int32_t x0 = px - 1, y0 = py - 1, bw = sw + 1, bh = sh + 1;
  box.set_pos(x0, y0);
  box.set_point(0, x0, y0);
  box.set_point(1, bw + x0, y0);
  box.set_point(2, bw + x0, bh + y0);
  box.set_point(3, x0, bh + y0);
  box.set_point(4, x0, y0);
  box.color = zvid::pack_bytes(1, 0xa2, 0x1f);
}

// Mech3.exe 1.2 @ 0x0043fa40: an edge past the display's inner border moved
// back (one per axis), the window's area set.
void HudZoomWindow::set_view_rect(int32_t* px, int32_t* py, int32_t* w, int32_t* h) {
  rect = {*px, *py, *px + *w, *py + *h};
  const ztwod::Rect d = g_hooks->display_rect();
  const int32_t right = d.right - 2, left = d.left + 2, top = d.top + 2, bottom = d.bottom - 2;
  bool moved = true;
  if (right < rect.left) rect.left = right;
  else if (rect.left < left) rect.left = left;
  else if (right < rect.right) rect.right = right;
  else if (rect.right < left) rect.right = left;
  else moved = false;
  if (moved) {
    *px = rect.left;
    *w = rect.right - rect.left;
  }
  moved = true;
  if (bottom < rect.top) rect.top = bottom;
  else if (rect.top < top) rect.top = top;
  else if (bottom < rect.bottom) rect.bottom = bottom;
  else if (rect.bottom < top) rect.bottom = top;
  else moved = false;
  if (moved) {
    *py = rect.top;
    *h = rect.bottom - rect.top;
  }
  view.window->set_area(rect);
}

// Mech3.exe 1.2 @ 0x00443b60
HudReticle::HudReticle(const char* on, const char* off) : Image(0) {
  locked = 0;
  lock = 0;
  lock_timer = 0.0f;
  Image::set_pos(0, 0);
  ztwod::Host& h = ztwod::host();
  img_on = h.image_get(on);
  img_off = h.image_get(off);
  img_over = h.image_get("ret_over");
  img_cant = h.image_get("ret_cant");
  img_x920 = h.image_get("ret_on");
  center = 1;
  u40 = 1;
  state = 5;
  dirty();
  if (state != 0) {  // inlined: the state set to off
    state = 0;
    if (!locked) set_image(img_off);
  }
  Object2D::show_for(-1.0f);
  zoom.hide();
  hit = zwep::HitRecord{};
}

// Mech3.exe 1.2 @ 0x004445f0
void HudReticle::place(float fx, float fy) {
  HudHooks& k = *g_hooks;
  const int32_t hw = k.display_w() / 2, hh = k.display_h() / 2;
  const int32_t py = crt::msvc_ftol(static_cast<float>(hh) * fy) + hh;
  const int32_t px = crt::msvc_ftol(static_cast<float>(hw) * fx) + hw;
  Image::set_pos(px, py);
  aim();
  zoom.follow(get_x(), get_y());
}

// Mech3.exe 1.2 @ 0x00444350: the reticle's point unprojected at the player
// camera's near and far planes, the segment between queried with the mech
// left out; the nearest hit counts unless its ground is soil kind 1 (one
// hit alone: kept unless so). No hit: the far point (hud.md section 10).
void HudReticle::aim() {
  HudHooks& k = *g_hooks;
  float s[3] = {static_cast<float>(get_x()), static_cast<float>(get_y()), 0.0f};
  screen_to_render(s);
  Mech* m = static_cast<Hud*>(source)->mech;
  static zwep::HitList hits;
  hits = zwep::HitList{};
  float near_z = 0.0f, far_z = 0.0f;
  k.player_camera_clip(&near_z, &far_z);
  k.view_prepare();
  float a[3], b[3];
  s[2] = 1.0f / near_z;  // 0x005c0074
  k.unproject_world(s, a);
  s[2] = 1.0f / far_z;
  k.unproject_world(s, b);
  // FIX (bug): the pick ended at the far clip (800 m), short of an ER large
  // laser's 1000 m: in range, the reticle stayed green (W33; fixes.md).
  float e[3] = {b[0], b[1], b[2]};
  if (zfix::options().bugs) {
    const float range = k.group_range(k.mech_weapons(m));
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len > 0.0f && range > len)
      for (int i = 0; i < 3; ++i) e[i] = a[i] + d[i] * (range / len);
  }
  const uint32_t node = k.mech_node(m);
  k.node_set_intersect(node, false);
  int32_t best = -1;
  bool missed = true;
  if (k.segment_list(k.world_node(k.world()), a, e, &hits) == 0) {
    if (hits.count == 1) {
      if (k.material_soil(hits.rec[0].material) != 1) best = 0;
    } else if (hits.count != 0) {
      float nearest = 3.4028235e38f;
      for (int32_t i = 0; i < hits.count; ++i) {
        if (k.material_soil(hits.rec[i].material) == 1) continue;
        const float d = zmth::distance_sq(*reinterpret_cast<const zmth::Vec3*>(a),
                                          *reinterpret_cast<const zmth::Vec3*>(&hits.rec[i].point));  // 0x0051dfa0
        if (d < nearest) {
          nearest = d;
          best = i;
        }
      }
    }
    hits.count = static_cast<uint8_t>(best);
    if (best >= 0) missed = false;
  }
  k.node_set_intersect(node, true);
  const float* p;
  if (!missed) {
    const zwep::HitRecord& h = hits.rec[hits.count];
    p = &h.point.x;
    const float d[3] = {a[0] - p[0], a[1] - p[1], a[2] - p[2]};
    set_hit(&h, std::sqrt((d[0] * d[0] + d[1] * d[1]) + d[2] * d[2]));
  } else {
    p = b;
    set_miss(b, far_z);
  }
  aim_point[0] = p[0];
  aim_point[1] = p[1];
  aim_point[2] = p[2];
}

// Mech3.exe 1.2 @ 0x00444710
void HudReticle::set_hit(const zwep::HitRecord* h, float d) {
  hit = h ? *h : zwep::HitRecord{};
  aim_distance = d;
}

// Mech3.exe 1.2 @ 0x004446a0: a record of nothing at the far point, facing up.
void HudReticle::set_miss(const float* p, float far_z) {
  hit = zwep::HitRecord{};
  hit.normal = {0.0f, 1.0f, 0.0f};  // [0x005eda70]
  hit.point = {p[0], p[1], p[2]};
  hit.distance_sq = far_z * far_z;
  aim_distance = far_z;
}

// Mech3.exe 1.2 @ 0x00443660
void HudZoomWindow::follow(int32_t px, int32_t py) {
  if (!(flags & ztwod::kHidden) || zoom != 0.0f) set_rect(px - base_w / 2, py - base_h / 2, base_w, base_h);
}

// Mech3.exe 1.2 @ 0x00443d00
void HudReticle::hide() {
  Object2D::hide();
  zoom.hide();
}

// ---------------------------------------------------------------------------
// The set-up (hud.md section 3)

namespace {
// 0x00557110 on a key: (x, y) or (w, h).
bool ivec2(zrdr::Node* n, const char* key, int32_t out[2]) { return zrdr::get_ivec2(n, key, out); }

// The anchors (hud.md section 5, from V640x480 against V800x600 ..
// V1600x1200): 'R' the right edge, 'C' the centre, 'L' the left; 'B' the
// bottom, 'T' the top.
struct Anchor {
  const char* piece;
  const char* key;
  char x, y;
};
constexpr Anchor kAnchors[] = {
    {"MFD", "position", 'R', 'B'},        {"TARGET", "position", 'L', 'B'},
    {"WEAPONS", "position", 'R', 'T'},    {"EQUIPMENT", "position", 'R', 'T'},
    {"COMPASS", "tape", 'C', 'T'},        {"COMPASS", "frame", 'C', 'T'},
    {"TIMER", "position", 'R', 'T'},      {"GAUGES", "speed_frame", 'R', 'T'},
    {"GAUGES", "fuel_frame", 'R', 'T'},   {"GAUGES", "heat_bar", 'L', 'T'},
    {"GAUGES", "heat_meter", 'L', 'T'},   {"GAUGES", "heat_text", 'L', 'T'},
    {"GAUGES", "heat_arrow", 'L', 'T'},   {"GAUGES", "coolant_frame", 'L', 'T'},
};
constexpr const char* kModeBlock[] = {nullptr, nullptr, nullptr, "V640x480", nullptr, "V640x480", "V800x600",
                                      "V1024x768", "V1152x864", "V1280x1024", "V1600x1200"};
// The parts the equipment list counts, by mech +0x3ac index ([0x005e3a30..0x005e3a50]).
constexpr int32_t kEquipParts[8] = {0, 12, 3, 8, 14, 6, 7, 4};
}  // namespace

bool hud_anchor(const char* piece, const char* key, int32_t w, int32_t h, int32_t p[2]) {
  for (const Anchor& a : kAnchors) {
    if (std::strcmp(a.piece, piece) != 0 || std::strcmp(a.key, key) != 0) continue;
    if (a.x == 'R') p[0] += w - 640;
    else if (a.x == 'C') p[0] += (w - 640) / 2;
    if (a.y == 'B') p[1] += h - 480;
    return true;
  }
  return false;
}

bool Hud::pos(zrdr::Node* n, const char* piece, const char* key, int32_t out[2]) const {
  if (!ivec2(n, key, out)) return false;
  if (anchor_w > 0) hud_anchor(piece, key, anchor_w, anchor_h, out);
  return true;
}

// Mech3.exe 1.2 @ 0x0043bb80: an unlisted video mode keeps +0x22e3c as it
// is (FIX (bug) in the constructor, hud.md section 3).
void Hud::setup(Mech* m) {
  ++g_hud_setups;
  HudHooks& k = *g_hooks;
  if (m) mech = m;
  reader = k.load_hud_zrd();
  if (reader) {
    const int32_t v = k.vmode();
    if (v >= 0 && v <= 10 && kModeBlock[v]) block = zrdr::find(reader, kModeBlock[v]);
    anchor_w = anchor_h = 0;
    if (zfix::options().any_resolution && v > 10) {  // FIX (B): V640x480 by the anchors (resolution.md step 4)
      block = zrdr::find(reader, "V640x480");
      anchor_w = k.display_w(), anchor_h = k.display_h();
    }
  }
  if (block) {
    int32_t a[2], b[2];
    zrdr::Node* n = zrdr::find(block, "MFD");
    if (n && ivec2(n, "size", b) && pos(n, "MFD", "position", a)) mfd.place(a[0], a[1], b[0], b[1]);
    n = zrdr::find(block, "TARGET");
    if (n && ivec2(n, "size", b) && pos(n, "TARGET", "position", a)) target.place(a[0], a[1], b[0], b[1]);
    n = zrdr::find(block, "TIMER");
    if (n && pos(n, "TIMER", "position", a)) timer.set_pos(a[0], a[1]);
  }
  reset();
  reticle.attach(this);
  mfd.attach(this);
  target.attach(this);
  target.set_target(k.mech_target_slot(m));
  radar.attach(this);
  equipment.read_layout(this);
  weapons.read_layout(this);
  compass.read_layout(this);
  gauges.read_layout(this);
  set_active(m ? (k.mech_dead(m) ? 0 : 1) : 0);
  if (m) reticle.zoom.round = k.mech_kind(m) == 2;  // 0x00442df0
  if (reader) {
    reader = reinterpret_cast<zrdr::Node*>(static_cast<intptr_t>(zrdr::free_node(reader)));
    block = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x0043baf0
void Hud::set_active(int32_t on) {
  active = on;
  if (on == 1)
    radar.show_for(-1.0f);
  else
    radar.hide();
}

// Mech3.exe 1.2 @ 0x0043bb20
bool Hud::power_down() {
  const bool was = active == 1;
  set_active(0);
  reticle.hide();  // +0x144c vt+0x64
  return was;
}

// Mech3.exe 1.2 @ 0x0043bb50
void Hud::power_up(uint8_t saved) {
  set_active(saved);
  reticle.show_for(-1.0f);  // +0x144c vt+0x60: for good
}

// Mech3.exe 1.2 @ 0x0043be10
void Hud::reset() {
  for (Static& s : statics) s = Static{};
  radar.set_fade(0.0f);
}

// Mech3.exe 1.2 @ 0x0043bfe0. Kind 0 the radar's fade (set at once), 1 the
// MFD's static, 2 TARGET's, 3 the weapons' and equipment's; 4 all four, 5 a
// random one. The level grows by `step` up to `most`, the hold to `hold`.
void Hud::add_static(float step, int32_t kind, float hold, float most) {
  static constexpr int kChannel[4] = {3, 1, 0, 2};  // the kind's +0x22e08 slot
  if (kind == 4) {
    for (int32_t k = 0; k < 4; ++k) add_static(step, k, hold, most);
    return;
  }
  if (kind == 5) {
    add_static(step, crt::msvc_rand() & 3, hold, most);
    return;
  }
  if (kind < 0 || kind > 3) return;
  Static& s = statics[kChannel[kind]];
  if (s.level < most) {
    s.level = step + s.level;
    if (most < s.level) s.level = most;
  }
  if (s.hold < hold) s.hold = hold;
  if (kind == 0) radar.set_fade(s.level);
}

// Mech3.exe 1.2 @ 0x0043c1f0: the channel (kinds as add_static's) at least
// `level`, its hold at least `hold`; the radar's fade set (0x0043dc70) for
// kind 0. The heat's haze (mech.cpp): a random one over the haze's heat.
void Hud::raise_static(float level, int32_t kind, float hold) {
  static constexpr int kChannel[4] = {3, 1, 0, 2};
  if (kind == 4) {
    for (int32_t k = 0; k < 4; ++k) raise_static(level, k, hold);
    return;
  }
  if (kind == 5) {
    raise_static(level, crt::msvc_rand() & 3, hold);
    return;
  }
  if (kind < 0 || kind > 3) return;
  Static& s = statics[kChannel[kind]];
  if (s.level < level) s.level = level;
  if (s.hold < hold) s.hold = hold;
  if (kind == 0) radar.set_fade(s.level);
}

// Mech3.exe 1.2 @ 0x0043cd20 (the frame's draw paths, after the HUD). Below
// 0.4 of the internal structure of the player's torso (any of its three
// sections) or head, a random panel gets static (GameIntensity's flag off):
// 0.1 a frame up to (5 - 20 x) / 4 under 0.2, (0.4 - x) x 5 / 4 above; then
// each shown panel's static is drawn and every level falls 0.3 a second after
// its hold, to its floor. FIX (B), behind per_second: the 0.1 is a 30 fps
// frame's.
void Hud::static_tick(float dt) {
  HudHooks& k = *g_hooks;
  const float fall = dt * 0.3f;  // 0x005c00a4
  if (void* p = k.mission_player(mech)) {
    float least = k.part_internal(p, 0x39c, 0);
    float v = k.part_internal(p, 0x39c, 1);
    if (v < least) least = k.part_internal(p, 0x39c, 1);
    v = k.part_internal(p, 0x39c, 2);
    if (v < least) least = k.part_internal(p, 0x39c, 2);
    v = k.part_internal(p, 0x3a4, 0);
    if (v < least) least = k.part_internal(p, 0x3a4, 0);
    if (least < 0.4f && !k.intensity_flag()) {  // 0x005c00a8
      const float most = (least > 0.2f ? (0.2f - (least - 0.2f)) * 5.0f : 5.0f - least * 20.0f) * 0.25f;
      const float step = zfix::options().per_second ? 0.1f * zfix::frame_steps(dt) : 0.1f;
      add_static(step, 5, dt, most);
    }
  }
  // Not while the radar shows zoomed over the mission map.
  if (active && ((radar.flags & ztwod::kHidden) || !radar.zoomed || !radar.active)) {
    k.static_target();
    auto draw = [&](const ztwod::Rect* r, float level) {  // 0x0043cca0
      if (level > 0.9f) level = 0.9f;  // 0x005c00a0
      else if (level < 0.0f) level = 0.0f;
      k.static_fill(r, level, zvid::pack_bytes(0x20, 0x40, 0x20), zvid::pack_bytes(0x40, 0x80, 0x40),
                    zvid::pack_bytes(0x60, 0xc0, 0x60));
    };
    if (target.shown_mode() != 0 && statics[0].level > 0.0f) {
      // FIX (bug): a mode without a rect (the nav list unmade) would be read
      // through null in the exe; nothing is drawn.
      if (const ztwod::Rect* q = target.shown_rect()) {
        ztwod::Rect r = *q;
        r.right += 1, r.bottom += 1;
        draw(&r, statics[0].level);
      }
    }
    if (!(mfd.flags & ztwod::kHidden) && statics[1].level > 0.0f) draw(mfd.shown_rect(), statics[1].level);
    if (!(weapons.flags & ztwod::kHidden) && statics[2].level > 0.0f) draw(&weapons.panel.rect, statics[2].level);
    if (!(weapons.flags & ztwod::kHidden) && !(equipment.flags & ztwod::kHidden) && statics[2].level > 0.0f)
      draw(&equipment.panel.rect, statics[2].level);
  }
  for (int i = 0; i < 4; ++i) {
    Static& s = statics[i];
    if (s.level > s.floor) {
      if (s.hold > 0.01f) {  // 0x005c00c0
        s.hold = s.hold - dt;
        continue;
      }
      s.hold = 0.0f;
      s.level = s.level - fall;
      if (s.level < s.floor) s.level = s.floor;
      if (i == 3) radar.set_fade(s.level);
    } else if (s.level < s.floor) {
      s.level = s.floor;
    }
  }
}

// Mech3.exe 1.2 @ 0x0043dc70
void HudRadar::set_fade(float f) {
  if (f > 1.0f) f = 1.0f;
  else if (f < 0.0f) f = 0.0f;
  set_noise(f);
  for (auto& l : lines) l.set_noise(f);
}

// Mech3.exe 1.2 @ 0x0043d630
void HudRadar::set_amber(bool on) {
  if (x58 != (on ? 1 : 0)) {
    const uint16_t c = on ? zvid::pack_bytes(0xff, 0xe2, 4) : zvid::pack_bytes(0, 0xff, 0);
    color = c;
    lines[0].color = c;
    lines[1].color = c;
    x58 = on ? 1 : 0;
  }
  changed = 1;
}

// Mech3.exe 1.2 @ 0x0043d6d0: the zoomed radar centred on the display, its
// radius 180 at 480 lines.
void HudRadar::attach(void* hud) {
  if (!hud) return;
  HudHooks& k = *g_hooks;
  source = hud;
  Mech* m = static_cast<Hud*>(hud)->mech;
  set_amber(m && k.mech_flag_bc4(m));
  cx[1] = k.display_w() / 2;
  cy[1] = k.display_h() / 2;
  r[1] = k.display_h() * 0xb4 / 0x1e0;
  to_r = r[zoomed];
  to_x = cx[zoomed];
  to_y = cy[zoomed];
  set_pos(cx[zoomed], cy[zoomed]);
  radius = r[zoomed];
  radius_sq = radius * radius;
  x_steps = std::abs(to_x - get_x());
  y_steps = std::abs(to_y - get_y());
  r_steps = std::abs(to_r - radius);
  changed = 1;
  set_fov(k.player_camera_fov());
}

// Mech3.exe 1.2 @ 0x0043db70
void HudRadar::show_for(float seconds) {
  to_r = r[zoomed];
  Object2D::show_for(seconds);
  lines[0].show_for(seconds);
  lines[1].show_for(seconds);
  if (xbd4 > 3) polys[0].show_for(-1.0f);
  if (xbd8 > 3) polys[1].show_for(-1.0f);
  changed = 1;
}

// Mech3.exe 1.2 @ 0x0043f500
void HudCompass::read_layout(const Hud* hud) {
  if (!hud || !hud->block) return;
  zrdr::Node* n = zrdr::find(hud->block, "COMPASS");
  int32_t a[2];
  if (hud->pos(n, "COMPASS", "tape", a)) tape.set_pos(a[0], a[1]);
  if (hud->pos(n, "COMPASS", "frame", a)) frame.set_pos(a[0], a[1]);
}

// Mech3.exe 1.2 @ 0x00444dc0: the frames moved, then the texts, bars and
// heat marks laid out again from them.
void HudGauges::read_layout(const Hud* hud) {
  if (!hud || !hud->block) return;
  zrdr::Node* n = zrdr::find(hud->block, "GAUGES");
  int32_t a[2];
  if (hud->pos(n, "GAUGES", "speed_frame", a)) speed_frame.set_pos(a[0], a[1]);
  if (hud->pos(n, "GAUGES", "fuel_frame", a)) fuel_frame.set_pos(a[0], a[1]);
  if (hud->pos(n, "GAUGES", "heat_bar", a)) heat_bar.set_pos(a[0], a[1]);
  if (hud->pos(n, "GAUGES", "heat_meter", a)) heat_meter.set_pos(a[0], a[1]);
  if (hud->pos(n, "GAUGES", "heat_text", a)) heat_text.set_pos(a[0], a[1]);
  if (hud->pos(n, "GAUGES", "heat_arrow", a)) toggler.set_pos(a[0], a[1]);
  if (hud->pos(n, "GAUGES", "coolant_frame", a)) coolant_frame.set_pos(a[0], a[1]);
  speed_text.set_pos(speed_frame.get_x() + 0x1b, speed_frame.get_y() + 0x30);
  coolant_text.set_pos(coolant_frame.get_x() + 0x16, coolant_frame.get_y() + 0x21);
  speed_bar.set_box(speed_frame.get_x() + 0xf, speed_frame.get_y() + 5, 4, 0x2f);
  coolant_bar.set_box(coolant_frame.get_x() + 9, coolant_frame.get_y() + 7, 4, image_h(coolant_frame) - 8);
  fuel_bar.set_box(fuel_frame.get_x() + 0x10, fuel_frame.get_y() + 5, 4, image_h(fuel_frame) - 8);
  toggler_y = heat_meter.get_y() - 1 + (image_h(heat_meter) - (image_h(toggler) >> 1));
  heat_src.top = 0;
  heat_bottom = heat_bar.get_y() + image_h(heat_bar);
  heat_src.bottom = image_h(heat_bar);
  heat_src.left = 0;
  heat_src.right = heat_bar.image ? heat_bar.image->width : 0;
}

namespace {
// A bar's top edge (points 0 and 3) the fraction of its height above its bottom.
void bar_level(HudBar& b, float v) {
  const int32_t up = crt::msvc_ftol(std::ceil(static_cast<double>(static_cast<float>(b.height) * v)));
  const float y = static_cast<float>(crt::msvc_ftol(b.pts[1][1]) - up);
  b.pts[0][1] = b.pts[3][1] = y;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004453a0: the speed text formatted, then used as the
// format itself ([0x005e4154] "%0.1f\n%s", message 0x1d7).
void HudGauges::set_speed(float v) {
  speed = v * 3.6f;  // [0x005c013c]
  char buf[16];
  const char* unit = hud_hooks().message(0x1d7);
  std::snprintf(buf, sizeof buf, "%0.1f\n%s", static_cast<double>(speed), unit ? unit : "(null)");
  speed_text.set_text(buf);
  const uint32_t c = speed >= 0.0f ? 0x1fa201 : 0xff;
  if (speed_text.color_top != c || speed_text.color_bottom != c) {
    speed_text.color_top = speed_text.color_bottom = c;
    speed_text.needs_render = 1;
  }
}

// Mech3.exe 1.2 @ 0x00445450
void HudGauges::set_throttle(float v) {
  if (v > 1.0f) v = 1.0f;
  else if (v < -1.0f) v = -1.0f;
  if (v < 0.0f) v *= 0.525f;  // [0x005c0140]
  speed_bar.color = v < 0.0f ? zvid::pack_bytes(0xff, 0, 0) : zvid::pack_bytes(0xc3, 0xad, 3);
  bar_level(speed_bar, v);
}

// Mech3.exe 1.2 @ 0x004455e0: the toggler rides the meter at level + rate,
// the bar shows the level from its bottom.
void HudGauges::set_heat(float level, float rate) {
  float v = rate + level;
  if (v > 1.0f) v = 1.0f;
  else if (v < 0.0f) v = 0.0f;
  const int32_t mh = heat_meter.image ? heat_meter.image->height : 0;
  toggler.set_y(toggler_y - crt::msvc_ftol(std::ceil(static_cast<double>(static_cast<float>(mh) * v))));
  const int32_t bh = heat_bar.image ? heat_bar.image->height : 0;
  const int32_t up = crt::msvc_ftol(std::ceil(static_cast<double>(static_cast<float>(bh) * level)));
  heat_src.top = (heat_bar.image ? heat_bar.image->height : 0) - up;
  heat_bar.src = &heat_src;
  const int32_t y = heat_bottom - up;
  heat_bar.set_pos(heat_bar.get_x(), y);
}

// Mech3.exe 1.2 @ 0x004456f0: a tenth of the way there each call (each frame).
// FIX (B): per second, at the 30 fps frame's tenth (fixes.md).
void HudGauges::set_coolant(float v, float dt) {
  if (zfix::options().per_second)
    g_coolant_shown = v + (g_coolant_shown - v) * std::pow(0.9f, zfix::frame_steps(dt));
  else
    g_coolant_shown = g_coolant_shown - (v - g_coolant_shown) * -0.1f;  // [0x005c0144]
  bar_level(coolant_bar, g_coolant_shown);
}

// Mech3.exe 1.2 @ 0x00445520
void HudGauges::set_jump(bool jets, float fuel) {
  if (fuel > 1.0f) fuel = 1.0f;
  else if (fuel < 0.0f) fuel = 0.0f;
  if (jets) {
    fuel_frame.show_for(-1.0f);
    fuel_bar.show_for(-1.0f);
  } else {
    fuel_frame.hide();
    fuel_bar.hide();
  }
  bar_level(fuel_bar, fuel);
}

// Mech3.exe 1.2 @ 0x00446900
void HudObjectives::data_update(float) {
  if (flags & ztwod::kHidden) return;
  int32_t p[2];
  message_panel_point(p);
  place_above(p[0], p[1]);
}

// Mech3.exe 1.2 @ 0x004468b0
void HudObjectives::place_above(int32_t px, int32_t py) {
  set_pos(px, panel.rect.top - panel.rect.bottom - 1 + py);
  const int32_t y = get_y();
  panel.set_pos(get_x() - 4, y);
}

// Mech3.exe 1.2 @ 0x00447e40: kind 0 (the AMS, +0x3f0) red and grey
// without one, else green or yellow by its on flag, with its rounds; kind 12
// (MASC) green or yellow by the mech's MASC time +0x574; others green, no value. A new kind
// renames the line and sets the green font over the one just set.
void HudEquipLine::set_item(Mech* m, int32_t kind) {
  if (kind == -1) {
    hide();
    return;
  }
  show_for(-1.0f);
  HudHooks& h = hud_hooks();
  EquipmentSet& e = equipment_set();
  switch (kind) {
    case 0: {
      bool on;
      int32_t count;
      if (!h.mech_ams(m, &on, &count)) {
        light.set_image(e.light[0]);
        set_named_font(e.font_grey, false);
        break;
      }
      light.set_image(on ? e.light[2] : e.light[1]);
      set_named_font(on ? e.font_green : e.font_yellow, false);
      value.set_text("%d", count);  // [0x005e1f4c]
      break;
    }
    case 12: {
      const bool on = h.mech_f(m, 0x574) != 0.0f;
      light.set_image(on ? e.light[2] : e.light[1]);
      set_named_font(on ? e.font_green : e.font_yellow, false);
      break;
    }
    default:
      light.set_image(e.light[2]);
      value.hide();
      break;
  }
  if (item != kind) {
    set_text_plain(h.equipment_name(kind));
    set_font(e.font_green);
    item = kind;
  }
}

// Mech3.exe 1.2 @ 0x00448320: the kinds in the order 0, 12, 3, 8, 14, 6, 7, 4
// ([0x005e3a30]); the panel 11 a line tall.
void HudEquipment::data_update(float) {
  static constexpr int32_t kKinds[8] = {0, 12, 3, 8, 14, 6, 7, 4};
  HudHooks& h = hud_hooks();
  Mech* m = hud ? static_cast<Hud*>(hud)->mech : nullptr;
  int32_t n = 0;
  if (m) {
    for (const int32_t* k = kKinds; k < kKinds + 8 && n < 16; ++k) {
      if (h.mech_part_count(m, *k) <= 0) continue;
      for (int32_t j = 0; n < 16 && j < h.mech_part_count(m, *k); ++j) lines[n++].set_item(m, *k);
    }
  }
  if (n == 0) {
    hide();
    return;
  }
  show_for(-1.0f);
  for (int32_t i = n; i < 16; ++i) lines[i].hide();
  panel.set_size(column, n * 11);
}

// Mech3.exe 1.2 @ 0x00448200: with a mech, the lines go below the point
// unless the weapons and the listed parts' equipment pass 16.
void HudEquipment::read_layout(Hud* h) {
  if (!h || !h->mech) {
    hud = nullptr;
    return;
  }
  HudHooks& k = *g_hooks;
  if (h->block) {
    if (zrdr::Node* n = zrdr::find(h->block, "EQUIPMENT")) {
      zrdr::get_int(n, "width", &column);
      int32_t a[2];
      if (h->pos(n, "EQUIPMENT", "position", a)) place(a[0], a[1], 0);
    }
  }
  hud = h;
  for (HudEquipLine& l : lines) l.reset();
  panel.set_opacity(hud_opacity());
  panel.set_color(zvid::pack_bytes(0, 0x40, 0));  // 0x0043aae0
  int32_t n = k.weapons_count(k.mech_weapons(h->mech));
  for (int32_t i : kEquipParts) {
    const int32_t c = k.mech_part_count(h->mech, i);
    if (c > 0) n += c;
  }
  below = n < 0x11;
}

// Mech3.exe 1.2 @ 0x00446120
void HudWeapons::read_layout(Hud* h) {
  next_weapon = 0;
  if (!h || !h->mech) {
    weapons = 0;
    hud = nullptr;
    return;
  }
  HudHooks& k = *g_hooks;
  if (h->block) {
    if (zrdr::Node* n = zrdr::find(h->block, "WEAPONS")) {
      zrdr::get_int(n, "width", &column);
      int32_t a[2];
      if (h->pos(n, "WEAPONS", "position", a)) set_pos(a[0], a[1]);
    }
  }
  hud = h;
  weapons = k.mech_weapons(h->mech);
  for (HudWeaponLine& l : lines) l.set_weapon(nullptr);
  refresh_all();
}

// Mech3.exe 1.2 @ 0x00445d10: both looks flipped, then set back, so the
// fonts are set again for the new name.
void HudWeaponLine::set_weapon(void* w, zimg::Texture* mark) {
  weapon = w;
  show_for(-1.0f);
  if (!w) {
    hide();
    refresh_line(mark);
    return;
  }
  const char* n = g_hooks->weapon_name(w);
  name.set_text_plain(n);
  text2.set_text_plain(n);
  red = !red;
  grey = !grey;
  set_look(!red, !grey);
  refresh_line(mark);
}

// Mech3.exe 1.2 @ 0x00445dc0: the bar the recycle fraction (clamped to 0..1;
// +0x111b set when it drops from full), the light by damage (green, yellow
// over 0.5, red over 0.8), the count the ammo, grey when destroyed.
bool HudWeaponLine::refresh_line(zimg::Texture* mark_image) {
  HudHooks& h = *g_hooks;
  if (!weapon) {
    hide();
    return false;
  }
  void* w = weapon;
  h.weapon_vt8(w);
  float f = h.weapon_recycle(w);
  if (f > 1.0f) f = 1.0f;
  else if (f < 0.0f) f = 0.0f;
  const bool recycling = f != 1.0f;
  x111b = value == 1.0f && f != 1.0f;
  set_value(f);  // vt+0x8c
  int32_t light_index = 0;
  if (h.weapon_damage(w) > 0.8f) light_index = 2;
  else if (h.weapon_damage(w) > 0.5f) light_index = 1;
  const bool destroyed = h.weapon_damage(w) == 1.0f;
  const bool live = !destroyed && h.weapon_enabled(w);
  mark.set_image(mark_image);
  const int32_t n = h.weapon_ammo(w);
  const bool r = live && n != 0;
  if (count != n) {
    count = n;
    count_text.set_text("%3d", n);  // [0x005e41a4]
  }
  set_look(r, destroyed);
  light.set_image(g_weapons.light[light_index]);
  return recycling;
}

// Mech3.exe 1.2 @ 0x004479a0
void HudDamageView::move(int32_t px, int32_t py) {
  for (HudDamagePart& p : parts) p.set_pos(p.images->x + px, p.images->y + py);  // 0x00447530
  bg.set_pos(px, py);
  rect = {px, py, px + g_damage.bg_w, py + g_damage.bg_h};
}

// Mech3.exe 1.2 @ 0x0043f890
void HudZoomView::place(int32_t px, int32_t py, int32_t w, int32_t h) {
  rect = {px, py, w + px, h + py};
  view.window->set_area(rect);
}

// Mech3.exe 1.2 @ 0x004427b0
void HudMfdView::place(int32_t px, int32_t py, int32_t w, int32_t h) {
  HudZoomView::place(px, py, w, h);
  panel.set_size(w, h);
  panel.set_pos(px, py);
  box.set_point(0, px, py);
  box.set_point(1, w + px, py);
  box.set_point(2, w + px, h + py);
  box.set_point(3, px, h + py);
  box.set_point(4, px, py);
}

// Mech3.exe 1.2 @ 0x00442230
void HudMfd::place(int32_t px, int32_t py, int32_t w, int32_t h) {
  damage.move(px, py);
  view->place(px, py, w, h);
  g_hooks->nav_list_place(nav, px, py + 0xe, h - 0xf, 6, 4);  // [0x005e3a00], [0x005e3a04]
  title.set_pos(px, py - title.text_height() - 1);
}

// Mech3.exe 1.2 @ 0x004423c0
void HudMfd::attach(void* h) {
  hud = h;
  view->set_source(h);
  Mech* m = h ? static_cast<Hud*>(h)->mech : nullptr;
  damage.set_mech(m);
  g_hooks->nav_list_attach(nav, m);
}

// Mech3.exe 1.2 @ 0x00440110
void HudTarget::place(int32_t px, int32_t py, int32_t w, int32_t h) {
  HudMfd::place(px, py, w, h);
  texts[0].set_pos(px, py + line_h * -2 - 1);
  texts[1].set_pos(px, py - line_h - 1);
  texts[2].set_pos(px, py + line_h * -3 - 1);
}

// Mech3.exe 1.2 @ 0x00441760
void HudTarget::attach(void* h) {
  HudMfd::attach(h);
  update_texts(false);
}

// Mech3.exe 1.2 @ 0x00444230
void HudReticle::draw3d(float dt) { zoom.draw3d(dt); }

// Mech3.exe 1.2 @ 0x00444250
void HudReticle::attach(void* h) {
  if (h) source = h;
  zoom.set_source(source);
  reset();
}

// Mech3.exe 1.2 @ 0x00443cc0: the lock tone's channels stopped.
void HudReticle::reset() {
  lock = 0;
  lock_timer = 0.6f;  // [0x005e3a0c]
  g_hooks->hud_sound_stop(0, 0);
  g_hooks->hud_sound_stop(0xc, 0);
}

namespace {
int32_t g_huds = 0;
}  // namespace
int32_t Hud::setups() { return g_hud_setups; }
int32_t Hud::live() { return g_huds; }

// Mech3.exe 1.2 @ 0x0043b860: the members' destructors do the rest.
Hud::~Hud() { --g_huds; }

// Mech3.exe 1.2 @ 0x0043b190. The pieces are made in the members' order.
Hud::Hud(Mech* m) {
  ++g_huds;
  if (zfix::options().bugs) block = nullptr;
  load_damage_set();
  mech = nullptr;
  setup(m);
  add(&compass);
  add(&gauges);
  add(&target);
  add(&mfd);
  add(&weapons);
  add(&equipment);
  timer.show_for(-1.0f);
}

}  // namespace mech3

namespace mech3 {

// Mech3.exe 1.2 @ 0x0043ad00: a see-through panel in Direct3D draws in the
// 3D pass instead (+0x28).
void HudPanel::update(float dt) {
  if (ztwod::host().video_mode() == 0 || alpha == 0xff) Image::update(dt);
}

namespace {
// FIX (B): a canvas rect onto the frame for the 3D pass (resolution.md step 4).
ztwod::Rect frame_rect(const ztwod::Rect& r) {
  int32_t x = r.left, y = r.top, w = r.right - r.left, h = r.bottom - r.top;
  canvas_to_render(&x, &y, &w, &h);
  return {x, y, x + w, y + h};
}
}  // namespace

// Mech3.exe 1.2 @ 0x0043ad20
void HudPanel::draw3d(float) {
  if ((flags & ztwod::kHidden) || ztwod::host().video_mode() == 0 || alpha >= 0xff) return;
  HudHooks& h = hud_hooks();
  const ztwod::Rect fr = frame_rect(rect);
  h.dirty_rect(&fr);
  h.scene_begin();
  draw_quad();
  h.flush_transparent();
  h.scene_end();
}

// Mech3.exe 1.2 @ 0x0043ad70: the corners go in from the last vertex back.
void HudPanel::draw_quad() {
  if (ztwod::host().video_mode() == 0 || alpha == 0xff) return Image::draw();
  float v[4][3] = {{corners[3][0], corners[3][1], corners[3][2]},
                   {corners[2][0], corners[2][1], corners[2][2]},
                   {corners[1][0], corners[1][1], corners[1][2]},
                   {corners[0][0], corners[0][1], corners[0][2]}};
  for (auto& p : v) canvas_point_to_render(&p[0], &p[1]);  // FIX (B): onto the frame
  hud_hooks().flat_quad(v, static_cast<int16_t>(color), alpha);
}

// Mech3.exe 1.2 @ 0x00448030
void HudEquipLine::update(float dt) {
  Text::update(dt);
  light.update(dt);
  value.update(dt);
}

// Mech3.exe 1.2 @ 0x00445c10: the texts are not drawn themselves; rendered,
// they are the bar's images, the second filling over the name. (Two calls
// of 0x005a1380 whose results go unused are left out.)
void HudWeaponLine::update(float dt) {
  if (flags & ztwod::kHidden) return;
  if (changed) {
    if (text2.needs_render) text2.render();
    fill = text2.image;
    dirty();
    if (name.needs_render) name.render();
    empty = name.image;
    dirty();
    set_value(value);  // vt+0x8c
    changed = 0;
  }
  Object2D::update(dt);
  if (count != -1) count_text.update(dt);
  light.update(dt);
  mark.update(dt);
}

namespace {
// 0x0043dce0: `v` kept within -steps..steps.
void clamp_step(int32_t& v, int32_t steps) {
  if (steps < v) v = steps;
  else if (v < -steps) v = -steps;
}
ztwod::Rect g_radar_rect{1, 1, 639, 479};  // [0x005e39c8] the polylines' clip: the radar's square (the exe's start)
}  // namespace
ztwod::Rect& radar_rect() { return g_radar_rect; }

// Mech3.exe 1.2 @ 0x0043dd00: the radar each frame (hud.md section 15).
// Its size and place step toward the zoomed or small ones, or over the
// mission map follow the mech; the range text, the mech's light (green,
// yellow, red by its +0xbc), the polylines' points turned by the mech into
// the radar, the box and cone drawn. Then the contacts (part 2).
void HudRadar::update(float dt) {
  HudHooks& h = *g_hooks;
  Hud* hd = static_cast<Hud*>(source);
  bool moved = false;
  if (changed) {
    moved = true;
    bool mapped = false;
    const ztwod::Rect* rect = nullptr;
    if (active) {
      rect = h.map_rect(h.radar_map());
      mapped = rect != nullptr;
      polys[0].hide();
      polys[1].hide();
    } else {
      if (xbd4 > 3) polys[0].show_for(-1.0f);
      if (xbd8 > 3) polys[1].show_for(-1.0f);
    }
    clipped = mapped;
    set_clip(rect);  // vt+0x70
    for (ztwod::ClipLine2D& l : lines) {
      l.clipped = mapped;
      l.clip = rect;
    }
  }
  if (active) {
    void* map = h.radar_map();
    const float range = h.mech_sensor_range(hd->mech);
    radius = crt::msvc_ftol(std::fabs(static_cast<float>(crt::msvc_ftol(h.map_scale(map) * range))));
    radius_sq = radius * radius;
    moved = true;
  } else if (int32_t d = radius - to_r) {
    clamp_step(d, r_steps);
    radius -= d;
    radius_sq = radius * radius;
    moved = true;
  }
  if (active) {
    float p[3];
    h.mech_position(hd->mech, p);
    int32_t pt[2];
    h.map_point(h.radar_map(), p, pt);
    set_pos(pt[0], pt[1]);
    moved = true;
  } else {
    int32_t dx = get_x() - to_x;
    int32_t dy = get_y() - to_y;
    if (dx || dy) {
      clamp_step(dx, x_steps);
      clamp_step(dy, y_steps);
      const int32_t py = get_y() - dy;
      set_pos(get_x() - dx, py);
      moved = true;
    }
  }
  if (radius == 0) Object2D::hide();  // 0x0059ff50
  if (!hd->active || (flags & ztwod::kHidden)) return;
  const float static_px = noise * 100.0f;  // 0x005a3ba0, [0x005c00d0]: the contacts' throw (part 2)
  (void)static_px;
  if (moved) {
    if (active) {
      light.set_pos(get_x(), get_y());
    } else {
      const int32_t k = crt::msvc_ftol(static_cast<double>(radius) * 0.714178);  // [0x005c00d8]
      const int32_t ty = get_y() + k;
      text1.set_pos(get_x() + k, ty);
      const int32_t ly = get_y() + k - 10;
      light.set_pos(get_x() + k + 0xe, ly);
    }
    set_dir(nullptr);
  }
  Mech* m = hd->mech;
  const float range = h.mech_sensor_range(m);
  void* map = active ? h.radar_map() : nullptr;
  if (active && map) {
    h.map_update(h.radar_map(), dt);
  } else {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1fkm", static_cast<double>(range * 0.001f));  // [0x005e3ec4], [0x005c00e0]
    text1.set_text("%s", buf);
    text1.update(dt);
  }
  const uint8_t bc = h.mech_bc(m);
  light.select(bc & 1 ? 2 : bc & 2 ? 1 : 0);
  bool light_drawn = true;
  if (active && h.radar_map()) {
    const ztwod::Rect* r = h.map_rect(h.radar_map());
    const zimg::Texture* li = light.image;
    light_drawn = r && light.get_x() > r->left && light.get_x() + (li ? li->width : 0) < r->right &&
                  light.get_y() > r->top && light.get_y() + (li ? li->height : 0) < r->bottom;
  }
  if (light_drawn) light.update(dt);
  if ((xbd4 > 3 || xbd8 > 3) && radius > 0 && !active) {
    float q[4], qc[4], mtx[9];
    h.mech_orientation(m, q);
    zmth::quat_conjugate(*reinterpret_cast<const zmth::Quat*>(q), *reinterpret_cast<zmth::Quat*>(qc));
    zmth::quat_matrix(qc, mtx);
    const float k = static_cast<float>(radius) / range;
    if (moved) {
      g_radar_rect = {get_x() - radius, get_y() - radius, get_x() + radius, get_y() + radius};
      polys[0].clip = polys[1].clip = &g_radar_rect;
    }
    const uint32_t counts[2] = {xbd4, xbd8}, lists[2] = {xdb4, xdb8};
    for (int p = 0; p < 2; ++p) {
      for (uint32_t i = 0; i < counts[p]; ++i) {
        float wp[3], me[3];
        h.radar_point(lists[p], static_cast<int32_t>(i), wp);
        h.mech_position(m, me);
        float d[3] = {wp[0] - me[0], wp[1] - me[1], wp[2] - me[2]};
        d[0] = d[0] * k, d[1] = d[1] * k, d[2] = d[2] * k;
        const float rx = d[0] * mtx[0] + d[1] * mtx[3] + d[2] * mtx[6];
        const float rz = d[0] * mtx[2] + d[1] * mtx[5] + d[2] * mtx[8];
        const int32_t py = crt::msvc_ftol(static_cast<float>(get_y()) + rz);
        const int32_t px = crt::msvc_ftol(static_cast<float>(get_x()) + rx);
        polys[p].set_point(static_cast<int32_t>(i), px, py);
        if (i == 0) {
          const int32_t qy = crt::msvc_ftol(static_cast<float>(get_y()) + rz);
          const int32_t qx = crt::msvc_ftol(static_cast<float>(get_x()) + rx);
          polys[p].set_point(static_cast<int32_t>(counts[p]), qx, qy);
        }
      }
    }
    polys[0].round = 1;
    polys[1].round = 1;
    polys[0].update(dt);
    polys[1].update(dt);
  }
  Object2D::update(dt);  // 0x0059ffb0: the box
  lines[0].update(dt);
  lines[1].update(dt);
  if (active) {
    lines[2].show_for(-1.0f);
    lines[2].update(dt);
  }
  if (radius > 0) update_contacts(dt, static_px);
  changed = 0;  // 0x0043f0a7 (not on the early returns above)
}

namespace {
// The contacts' static: each place thrown up to `px` either way on x and z.
void radar_jitter(float p[3], float px) {
  if (px == 0.0f) return;
  const float a = static_cast<float>(crt::msvc_rand()) * 3.0518509e-05f;  // [0x005c00e4]
  p[0] = (a + a - 1.0f) * px + p[0];
  const float b = static_cast<float>(crt::msvc_rand()) * 3.0518509e-05f;
  p[2] = (b + b - 1.0f) * px + p[2];
}
// A vector turned into the mech's frame (the matrix by rows, x and z kept).
void radar_turn(const float d[3], const float m[9], float out[3]) {
  out[0] = d[0] * m[0] + d[1] * m[3] + d[2] * m[6];
  out[1] = d[0] * m[1] + d[1] * m[4] + d[2] * m[7];
  out[2] = d[0] * m[2] + d[1] * m[5] + d[2] * m[8];
}
int32_t radar_px(int32_t base, float off) { return crt::msvc_ftol(static_cast<float>(base) + off); }
}  // namespace

// The contacts' part of 0x0043dd00 (from 0x0043e445; hud.md section 15): the
// mech's contacts, the network player's marker, the shots in the air, the
// landmines, each placed by its distance in the mech's frame (over the map:
// on the map).
void HudRadar::update_contacts(float dt, float static_px) {
  HudHooks& h = *g_hooks;
  Hud* hd = static_cast<Hud*>(source);
  Mech* m = hd->mech;
  const float k = static_cast<float>(radius) / h.mech_sensor_range(m);
  float mtx[9] = {};
  if (!active) {
    float q[4], qc[4];
    h.mech_orientation(m, q);
    zmth::quat_conjugate(*reinterpret_cast<const zmth::Quat*>(q), *reinterpret_cast<zmth::Quat*>(qc));
    zmth::quat_matrix(qc, mtx);
  }
  auto offset = [&](const float p[3], float scale, float v[3]) {  // p from the mech, scaled, turned
    float me[3];
    h.mech_position(m, me);
    float d[3] = {p[0] - me[0], p[1] - me[1], p[2] - me[2]};
    const float len = h.normalize(d);
    const float sd[3] = {scale * d[0], scale * d[1], scale * d[2]};
    radar_turn(sd, mtx, v);
    return len;
  };
  const bool all = h.radar_all(m);  // ours (cheats.h)
  for (int32_t i = 0; i < h.mech_contact_count(m); ++i) {
    void* c = h.mech_contact(m, i);
    void* o = h.contact_vehicle(c);
    bool selected = false;
    if (hd->target.target) {
      void* t = h.slot_contact(hd->target.target);
      selected = t == c || (t && c && h.contact_same(t, c));
    }
    if (!selected && !all && !h.contact_on_radar(c)) continue;
    float p[3];
    h.object_position(o, p);
    // FIX (bug): a targets.zrd building's node sits at the world's origin, its
    // mesh in world co-ordinates, so the exe's place (0x005a85e0) put its
    // blip towards the origin; the record's centroid instead (fixes.md, W32).
    if (zfix::options().bugs) h.object_radar_place(o, p);
    radar_jitter(p, static_px);
    float v[3] = {};
    if (!active) {
      float s = h.contact_distance(c) * k;
      if (s > static_cast<float>(radius)) s = static_cast<float>(radius);
      offset(p, s, v);
    }
    void* blip;
    uint16_t colour;
    if (h.object_team(o) & h.mech_allies(m)) {
      blip = h.object_blip(o, 0);
      colour = zvid::pack_bytes(0, 0, 0xff);
    } else if (h.object_team(o) & h.mech_enemies(m)) {
      blip = h.object_blip(o, 1);
      colour = zvid::pack_bytes(0xff, 0, 0);
    } else {
      blip = h.object_blip(o, 2);
      colour = zvid::pack_bytes(1, 0xa2, 0x1f);
    }
    int32_t pt[2];
    if (!selected) {
      if (!blip) continue;
      if (active) {
        h.map_point(h.radar_map(), p, pt);
      } else {
        pt[1] = radar_px(get_y(), v[2]);
        pt[0] = radar_px(get_x(), v[0]);
      }
      h.blip_place(blip, pt[0], pt[1]);
      h.blip_draw(blip);
      continue;
    }
    // The selected target: a triangle instead (made for the frame).
    ztwod::Shape tri;
    static ztwod::Rect screen;  // [0x0072f328]
    screen = ztwod::host().screen_rect();
    tri.clip = &screen;
    if (active) {
      h.map_point(h.radar_map(), p, pt);
    } else {
      pt[1] = radar_px(get_y(), v[2]);
      pt[0] = radar_px(get_x(), v[0]);
    }
    tri.set_pos(pt[0], pt[1]);
    target_at[0] = pt[0], target_at[1] = pt[1], ++target_draws;
    tri.color = colour;
    tri.set_scale(5.773503f);  // 0x40b8c08e
    tri.update(dt);
  }
  // Ours: black sheep wall (cheats.h): the mechs and vehicles with no
  // contact, by their distance.
  if (all) {
    for (void* o : h.radar_unsensed(m)) {
      float p[3], me[3];
      h.object_position(o, p);
      radar_jitter(p, static_px);
      float v[3] = {};
      if (!active) {
        h.mech_position(m, me);
        const float d[3] = {p[0] - me[0], p[1] - me[1], p[2] - me[2]};
        float s = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * k;
        if (s > static_cast<float>(radius)) s = static_cast<float>(radius);
        offset(p, s, v);
      }
      const uint32_t team = h.object_team(o);
      void* blip = h.object_blip(o, (team & h.mech_allies(m)) ? 0 : (team & h.mech_enemies(m)) ? 1 : 2);
      if (!blip) continue;
      int32_t pt[2];
      if (active) {
        h.map_point(h.radar_map(), p, pt);
      } else {
        pt[1] = radar_px(get_y(), v[2]);
        pt[0] = radar_px(get_x(), v[0]);
      }
      h.blip_place(blip, pt[0], pt[1]);
      h.blip_draw(blip);
    }
  }
  // The network player's marker, with its name and distance in TEXT2.
  if (void* e = h.network_marker()) {
    void* img = h.marker_image(e);
    float p[3];
    h.marker_position(e, p);
    radar_jitter(p, static_px);
    float me[3];
    h.mech_position(m, me);
    float d[3] = {p[0] - me[0], p[1] - me[1], p[2] - me[2]};
    const float len = h.normalize(d);
    if (active) {
      if (img) {
        int32_t pt[2];
        h.map_point(h.radar_map(), p, pt);
        h.blip_place(img, pt[0], pt[1]);
        h.blip_draw(img);
      }
    } else {
      float s = len * k;
      if (s > static_cast<float>(radius)) s = static_cast<float>(radius);
      else if (s < 0.0f) s = 0.0f;
      const float sd[3] = {s * d[0], s * d[1], s * d[2]};
      float v[3];
      radar_turn(sd, mtx, v);
      if (img) {
        const int32_t py = radar_px(get_y(), v[2]);
        h.blip_place(img, radar_px(get_x(), v[0]), py);
        h.blip_draw(img);
      }
    }
    if (len > 1000.0f) {  // [0x005c00ec]
      char buf[32];
      std::snprintf(buf, sizeof buf, "%.2f", static_cast<double>(len * 0.001f));  // [0x005e3ebc]
      text2.set_text_id(0x228, h.marker_name(e), buf);
    } else {
      text2.set_text_id(0x229, h.marker_name(e), crt::msvc_ftol(std::ceil(static_cast<double>(len))));
    }
    text2.show_for(-1.0f);
    text2.update(dt);
  }
  // The shots in the air: a pixel each, red those hostile to the player.
  float p[3];
  bool hostile;
  for (int32_t i = 0; h.radar_shot(i, p, &hostile); ++i) {
    ztwod::Pixel2D px(0, 0, 0);
    radar_jitter(p, static_px);
    if (active) {
      int32_t pt[2];
      if (!h.map_point(h.radar_map(), p, pt)) continue;
      px.x = pt[0], px.y = pt[1];
      px.dirty();
    } else {
      float me[3];
      h.mech_position(m, me);
      const float d[3] = {(p[0] - me[0]) * k, (p[1] - me[1]) * k, (p[2] - me[2]) * k};
      const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
      if (static_cast<float>(radius * radius) < dd) continue;
      float v[3];
      radar_turn(d, mtx, v);
      const int32_t py = radar_px(get_y(), v[2]);
      px.x = radar_px(get_x(), v[0]), px.y = py;
      px.dirty();
    }
    px.color = hostile ? zvid::pack_bytes(0xff, 0, 0) : zvid::pack_bytes(1, 0xa2, 0x1f);
    px.update(dt);
  }
  // The armed landmines within 100 m: a red X each.
  for (int32_t i = 0, n = h.mine_count(); i < n; ++i) {
    if (!h.mine(i, p)) continue;
    radar_jitter(p, static_px);
    float me[3];
    h.mech_position(m, me);
    float d[3] = {p[0] - me[0], p[1] - me[1], p[2] - me[2]};
    if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 10000.0f) continue;  // [0x005c00f0]
    d[0] *= k, d[1] *= k, d[2] *= k;
    if (static_cast<float>(radius * radius) < d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) continue;
    int32_t pt[2];
    if (active) {
      if (!h.map_point(h.radar_map(), p, pt)) continue;
    } else {
      float v[3];
      radar_turn(d, mtx, v);
      pt[0] = radar_px(get_x(), v[0]);
      pt[1] = radar_px(get_y(), v[2]);
    }
    ztwod::Line2D a(pt[0] - 1, pt[1] - 1, pt[0] + 1, pt[1] + 1, zvid::pack_bytes(0xff, 0, 0));
    ztwod::Line2D b(pt[0] + 1, pt[1] - 1, pt[0] - 1, pt[1] + 1, zvid::pack_bytes(0xff, 0, 0));
    a.update(dt);
    b.update(dt);
  }
}

// Mech3.exe 1.2 @ 0x0043f620
void HudCompass::update(float dt) {
  if (flags & ztwod::kHidden) return;
  frame.update(dt);
  tape.update(dt);
}

// Mech3.exe 1.2 @ 0x00447410
void HudObjectives::update(float dt) {
  if (flags & ztwod::kHidden) return;
  panel.update(dt);
  TextBox::update(dt);
  scores.update(dt);
}

// Mech3.exe 1.2 @ 0x004452c0
void HudGauges::update(float dt) {
  if (flags & ztwod::kHidden) return;
  for (ztwod::Object2D* o : std::initializer_list<ztwod::Object2D*>{
           &speed_text, &coolant_text, &heat_text, &speed_bar, &speed_frame, &fuel_bar, &fuel_frame,
           &coolant_bar, &coolant_frame, &heat_bar, &heat_meter, &toggler})
    o->update(dt);
}

// Mech3.exe 1.2 @ 0x00442880
void HudMfdView::update(float dt) {
  if (!(flags & ztwod::kHidden)) box.update(dt);
}

// Mech3.exe 1.2 @ 0x00447b50: drawn hidden or not, each part by its own flags.
void HudDamageView::update(float dt) {
  bg.update(dt);
  for (HudDamagePart& p : parts) p.update(dt);
}

void HudMfd::update_mode(float dt) {
  switch (shown_mode()) {
    case 1: view->update(dt); break;
    case 2: damage.update(dt); break;
    case 3: hud_hooks().nav_list_update(nav, dt); break;
    default: break;
  }
}

// Mech3.exe 1.2 @ 0x00442540: no hidden test; the title only with the
// title flag, or in a multiplayer game while [0x00605f88] is clear.
void HudMfd::update(float dt) {
  update_mode(dt);
  HudHooks& h = hud_hooks();
  if (h.multiplayer_game() == 0 ? h.title_flag() : !h.title_mp_off()) title.update(dt);
}

// Mech3.exe 1.2 @ 0x004413a0: the mode without a target is none; a target
// that is not a mech shows its 3D view only.
int32_t HudTarget::shown_mode() {
  HudHooks& h = hud_hooks();
  void* o = h.target_valid(target) ? h.target_object(target) : nullptr;
  if (!o || h.object_a8(o)) return 0;
  if (h.object_mech(o)) return mode;
  return mode != 0;
}

// Mech3.exe 1.2 @ 0x00441490: no title; the fourth text not drawn.
void HudTarget::update(float dt) {
  if (flags & ztwod::kHidden) return;
  for (ztwod::Line2D& l : lines) l.update(dt);
  update_mode(dt);
  texts[0].update(dt);
  texts[1].update(dt);
  texts[2].update(dt);
  marker.update(dt);
  frame.update(dt);
}

// Mech3.exe 1.2 @ 0x00442e30: hidden, the scale shrinks at 7 a second;
// shown, it grows at 4.8. Full size, the light shows the reticle's state.
void HudZoomWindow::update(float dt) {
  const bool hidden = (flags & ztwod::kHidden) != 0;
  zoom -= dt * (hidden ? 7.0f : -4.8f);
  if (zoom > 1.0f) zoom = 1.0f;
  else if (zoom < 0.0f) zoom = 0.0f;
  if (hidden && zoom == 0.0f) return;
  if (zoom == 1.0f) {
    light.select(static_cast<Hud*>(source)->reticle.state);
    light.update(dt);
    text.update(dt);
  }
  if (round) frame.update(dt);
  else box.update(dt);
}

// Mech3.exe 1.2 @ 0x004441a0: the reticle itself only while the zoom window
// is away; nothing more once the HUD is off and its mech dead.
void HudReticle::update(float dt) {
  zoom.update(dt);
  Hud* h = static_cast<Hud*>(source);
  if (h && h->mech && hud_hooks().mech_dead(h->mech) && h->active == 0) return;
  if (zoom.flags & ztwod::kHidden) Image::update(dt);
  lock_timer -= dt;
  if (lock_timer < 0.0f) lock_timer = 0.0f;  // [0x005c007c]
}

void HudHooks::reticle_aim_state(void* r, bool over) { static_cast<HudReticle*>(r)->aim_state(over); }

// Mech3.exe 1.2 @ 0x00443d20: the missile lock (hud.md section 12). Over a
// target with a group that locks, in range and not of kind 7, the lock
// starts (tone 0xc, 0.6 s, 1.2 when the target's +0x3cc is above 0), then
// holds (tone 0, 3.5 s past the last good frame) or is lost.
void HudReticle::aim_state(bool over) {
  HudHooks& h = hud_hooks();
  Hud* hud = static_cast<Hud*>(source);
  void* wm = h.mech_weapons(hud->mech);
  const float range = h.group_range(wm);
  const bool lockable = h.group_locks(wm) && h.group_4b1cd0(wm);
  void* contact = h.slot_contact(hud->target.target);
  void* mech = contact ? h.contact_mech(contact) : nullptr;
  const bool kind7 = mech && h.mech_kind_of(mech) == 7;
  const bool in_range = contact && h.contact_distance(contact) < range;
  const bool aim_close = aim_distance < range;
  const bool near = in_range || aim_close;
  const bool good = over && lockable && near && contact && !kind7;
  switch (lock) {
    case 0:
    case 4: lock = good ? 1 : 0; break;
    case 1: lock = good ? 2 : 4; break;
    case 2:
      if (!good) lock = 4;
      else if (lock_timer <= 0.0f) lock = 3;
      break;
    case 3: lock = good || lock_timer > 0.0f ? 3 : 4; break;
    default: break;
  }
  const bool is_locked = lock == 3;
  if (!near && !is_locked) lock = 4;
  switch (lock) {
    case 1:
      if (!h.hud_sound_playing(0xc)) h.hud_sound_play(0xc);
      lock_timer = mech && h.mech_3cc(mech) > 0 ? kLockWait + kLockWait : kLockWait;
      break;
    case 3:
      if (good) lock_timer = 3.5f;  // [0x005e3a10]
      h.hud_sound_stop(0xc, 0);
      if (!h.hud_sound_playing(0)) h.hud_sound_play(0);
      break;
    case 4:
      h.hud_sound_stop(0, 0);
      h.hud_sound_stop(0xc, 0);
      break;
    default: break;
  }
  auto show = [&](int32_t s, zimg::Texture* img) {  // `locked`: the image kept
    if (state == s) return;
    state = s;
    if (!locked) set_image(img);
  };
  show(is_locked ? 1 : 0, is_locked ? img_on : img_off);
  h.set_lock_shown(wm, is_locked);
  if (is_locked) return;
  if (lock == 2) return show(2, img_over);
  void* node = hit.node;
  if (!node) {
    if (!aim_close) show(3, img_cant);
    return;
  }
  if (h.node_object(node)) {  // asked twice, as written
    if (h.object_byte8(h.node_object(node)) && aim_close) show(2, img_over);
  } else if (h.node_bc(node) > 1) {
    show(2, img_over);
  } else if (!aim_close) {
    show(3, img_cant);
  }
}

// Mech3.exe 1.2 @ 0x00446680
void HudWeapons::update(float dt) {
  if ((flags & ztwod::kHidden) || weapons == 0) return;
  if (shown) looks();
  panel.update(dt);
  for (HudWeaponLine& l : lines) l.update(dt);
  poly.update(dt);
  Object2D::update(dt);
}

// Mech3.exe 1.2 @ 0x00448450
void HudEquipment::update(float dt) {
  if (flags & ztwod::kHidden) return;
  panel.update(dt);
  for (HudEquipLine& l : lines) {
    if (l.flags & ztwod::kHidden) break;
    l.update(dt);
  }
  Object2D::update(dt);
}

// Mech3.exe 1.2 @ 0x00442870
void HudMfdView::draw3d(float dt) { hud_hooks().zview_render(this, dt); }

// Mech3.exe 1.2 @ 0x004425d0
const ztwod::Rect* HudMfd::shown_rect() {
  switch (shown_mode()) {
    case 1: return &view->rect;
    case 2: return &damage.rect;
    case 3: return hud_hooks().nav_rect(nav);
    default: return nullptr;
  }
}

// Mech3.exe 1.2 @ 0x00442500
void HudMfd::draw3d(float dt) {
  switch (shown_mode()) {
    case 1: view->draw3d(dt); break;
    case 2: damage.draw3d(dt); break;
    default: break;
  }
}

// --- TARGET's tracking (hud.md section 14) ---

namespace {
ztwod::Rect g_triangle_rect{1, 1, 639, 479};  // [0x005e39d8] the zoom window's or the window's rect (the exe's start)
}  // namespace
ztwod::Rect& triangle_rect() { return g_triangle_rect; }
namespace {
constexpr int32_t kCircle = 6;  // [0x005e39ec] the lead circle's radius
constexpr int32_t kJitter = 4;  // [0x005e39e8] ECM's throw, pixels

int32_t ftol(float v) { return crt::msvc_ftol(v); }

// Mech3.exe 1.2 @ 0x00440290: as zTwoD's clip_round (0x005a3580) in floats:
// p0..p1 within the circle across `r` into o0..o1; false when none of it is.
bool clip_round_f(const ztwod::Rect& r, const float* p0, const float* p1, float* o0, float* o1) {
  const float X = p0[0] - static_cast<float>(r.left + r.right) * 0.5f;
  const float rad = static_cast<float>(r.right - r.left) * 0.5f;
  const float Y = p0[1] - static_cast<float>(r.bottom + r.top) * 0.5f;
  const float dx = p1[0] - p0[0], dy = p1[1] - p0[1];
  const float c = (Y * Y + X * X) - rad * rad;
  const float A = dy * dy + dx * dx;
  const float B = Y * dy + X * dx;
  float t[2] = {A, 0.0f};
  int32_t roots = 0;
  if (A > 0.0f) {
    const float disc = B * B - c * A;
    if (disc >= 0.0f) {
      const float sq = std::sqrt(disc);
      roots = 2;
      t[0] = (-B - sq) / A;
      t[1] = (sq - B) / A;
    }
  }
  int32_t i = 0;
  auto next = [&] {
    while (i < roots && !(t[i] > 0.0f && t[i] < 1.0f)) ++i;
    return i < roots;
  };
  if (c > 0.0f && !next()) return false;
  o0[0] = p0[0], o0[1] = p0[1];
  if (c > 0.0f) {
    o0[0] = dx * t[i] + o0[0];
    o0[1] = dy * t[i] + o0[1];
    ++i;
  }
  if ((c - B * -2.0f) + A <= 0.0f) {
    o1[0] = p1[0], o1[1] = p1[1];
    return true;
  }
  if (!next()) return false;
  o1[0] = dx * t[i] + p0[0];
  o1[1] = dy * t[i] + p0[1];
  return true;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00440510: a triangle round the target, its height the
// target's on screen (its centre to its centre plus its size up), in the
// team's colour (red enemy, blue ally, else green); a new target's starts
// larger by the pulse (+0x240c x 3.33 of the screen's height) and shrinks.
// In a round zoom window the sides are cut to its circle, else to its (or
// the window's) rect.
void HudTarget::triangle(int32_t team, float dt, bool zoomed) {
  HudHooks& h = *g_hooks;
  const uint16_t c = team == 0 ? zvid::pack_bytes(0xff, 0, 0) : team == 1 ? zvid::pack_bytes(0, 0, 0xff)
                                                                          : zvid::pack_bytes(1, 0xa2, 0x1f);
  for (ztwod::Line2D& l : lines) l.color = c;
  HudZoomWindow& z = static_cast<Hud*>(hud)->reticle.zoom;
  bool round = false;
  if (zoomed) {
    h.zview_prepare(&z.view);
    g_triangle_rect = z.rect;
    round = z.round != 0;
  } else {
    g_triangle_rect = h.window_rect();  // 0x0040d7b0
  }
  void* contact = h.slot_contact(target);
  float size = h.contact_size(contact);
  float centre[3];
  h.vehicle_center(h.contact_vehicle(contact), centre);
  float a[3];
  h.project(centre, a);
  if (!zoomed) render_to_screen(a);
  else render_point_to_canvas(&a[0], &a[1]);  // FIX (B): the zoom window's frame pixels onto the canvas
  float top[3] = {centre[0], size + centre[1], centre[2]};
  h.project(top, top);
  if (!zoomed) render_to_screen(top);
  else render_point_to_canvas(&top[0], &top[1]);
  size = std::fabs(top[1] - a[1]);
  if (x240c > 0.0f) {
    const float k = x240c * 3.3333333f;  // [0x005c010c]
    const int32_t sh = h.display_h();
    shown = 0;
    x240c = x240c - dt;
    size = static_cast<float>(sh) * k + size;
  }
  float pos[3];
  h.entity_position(h.contact_entity(contact), pos);
  h.project(pos, a);
  if (!zoomed) render_to_screen(a);
  else render_point_to_canvas(&a[0], &a[1]);
  shape1.show_for(-1.0f);
  shape1.set_translation(a);
  shape1.set_scale(size * 1.1547010f);  // [0x005c0110]
  const float A = size * 0.333333f;     // [0x005c0118]
  const float B = size * 0.5773505f;    // [0x005c0114]
  const float p0[2] = {a[0], a[1] - (A + A)};
  const float p1[2] = {a[0] - B, A + a[1]};
  const float p2[2] = {B + a[0], A + a[1]};
  const float* ends[3][2] = {{p0, p1}, {p1, p2}, {p2, p0}};
  if (round) {
    for (int i = 0; i < 3; ++i) {
      float o0[2], o1[2];
      if (clip_round_f(g_triangle_rect, ends[i][0], ends[i][1], o0, o1)) {
        lines[i].set(ftol(o0[0]), ftol(o0[1]), ftol(o1[0]), ftol(o1[1]));
        lines[i].show_for(-1.0f);
      } else {
        lines[i].hide();
      }
    }
    return;
  }
  for (int i = 0; i < 3; ++i)
    lines[i].set(ftol(ends[i][0][0]), ftol(ends[i][0][1]), ftol(ends[i][1][0]), ftol(ends[i][1][1]));
  for (ztwod::Line2D& l : lines) {
    l.clip = &g_triangle_rect;
    l.clipped = 1;
  }
  for (ztwod::Line2D& l : lines) l.show_for(-1.0f);
}

// Mech3.exe 1.2 @ 0x00440b00: TARGET's camera behind the target and turned
// toward it; the target projected (through the zoom window first when it
// shows); on screen, the targeting computer's lead circle (with ECM it
// jitters and turns yellow) and the triangle; off it, an arrow at the edge
// in the team's colour. Then the MFD modes' tails for the target.
void HudTarget::track(float dt) {
  if (flags & ztwod::kHidden) return;
  HudHooks& h = *g_hooks;
  if (!h.target_valid(target)) return;
  void* contact = h.slot_contact(target);
  h.contact_distance(contact);  // 0x004e7030: works out the direction too
  float dir[3];
  h.contact_dir(contact, dir);
  float tp[3], op[3];
  h.entity_position(h.contact_entity(contact), tp);
  Hud* hd = static_cast<Hud*>(hud);
  h.entity_position(hd->mech, op);
  const float k = h.contact_size(contact) * 1.2f;  // [0x005c011c]
  float angles[3];
  zmth::direction_angles(op, tp, angles);
  dir[0] = k * dir[0], dir[1] = k * dir[1], dir[2] = k * dir[2];
  const float cam[3] = {tp[0] - dir[0], tp[1] - dir[1], tp[2] - dir[2]};
  h.camera_set_position(view->view.camera, cam);
  h.camera_set_angles(view->view.camera, angles);
  HudZoomWindow& z = hd->reticle.zoom;
  bool zoomed = !(z.flags & ztwod::kHidden);
  bool on_main = false;  // projected through the player's view
  float sp[3], zp[3];
  int32_t code;
  if (zoomed) {
    h.zview_prepare(&z.view);
    const uint32_t oz = h.project(tp, zp);
    render_to_screen(zp);
    if (oz == 0) {
      code = 0;
      sp[0] = zp[0], sp[1] = zp[1], sp[2] = zp[2];
    } else {
      h.view_prepare();
      code = static_cast<int32_t>(h.project(tp, sp));
      render_to_screen(sp);
      if (code != 0) {
        on_main = true;
      } else {
        zoomed = false;
        // On the screen but off the zoom window: kept only when it lies past
        // the zoom window's own outcode side.
        if (!((oz & 1) && sp[0] < zp[0]) && !((oz & 2) && sp[0] > zp[0]) && !((oz & 4) && sp[1] < zp[1]) &&
            !((oz & 8) && sp[1] > zp[1]))
          code = -1;
      }
    }
  } else {
    h.view_prepare();
    code = static_cast<int32_t>(h.project(tp, sp));
    render_to_screen(sp);
    on_main = true;
  }
  for (ztwod::Line2D& l : lines) l.hide();
  marker.hide();
  frame.hide();
  shape1.hide();
  void* tm = h.contact_mech(contact);
  const bool ecm = tm && h.mech_3cc(tm) > 0;
  int32_t team = 2;
  if (Mech* own = hd->mech) {
    if (void* seen = h.object_seen(contact)) {
      if (h.mech_allies(own) & h.object_team(seen)) team = 1;
      else if (h.mech_enemies(own) & h.object_team(seen)) team = 0;
    }
  }
  const zimg::Texture* mk = marker.image;
  switch (code) {
    case 0: {
      float lp[3], cp[3];
      if (h.mech_part_count(hd->mech, 14) > 0 && lead_point(hd->mech, lp) && h.project(lp, cp) == 0) {
        render_to_screen(cp);
        uint16_t colour;
        if (ecm) {
          const float u = (static_cast<float>(crt::msvc_rand()) - 16383.5f) * 6.1037019e-05f;  // [0x005e4008], [0x005e4004]
          cp[0] = static_cast<float>(kJitter) * u + cp[0];
          const float v = (static_cast<float>(crt::msvc_rand()) - 16383.5f) * 6.1037019e-05f;
          cp[1] = static_cast<float>(kJitter) * v + cp[1];
          colour = zvid::pack_bytes(0xff, 0xe2, 4);
        } else {
          colour = zvid::pack_bytes(0, 0xff, 0);
        }
        frame.color = colour;
        const float hx = static_cast<float>(h.display_w() - kCircle - 1), lo = static_cast<float>(kCircle + 1);
        if (cp[0] > hx) cp[0] = hx;
        else if (cp[0] < lo) cp[0] = lo;
        const float hy = static_cast<float>(h.display_h() - kCircle - 1);
        if (cp[1] > hy) cp[1] = hy;
        else if (cp[1] < lo) cp[1] = lo;
        frame.show_for(-1.0f);
        frame.set_pos(ftol(cp[0]), ftol(cp[1]));
        frame.radius = kCircle;
        frame.radius_sq = kCircle * kCircle;
      }
      triangle(team, dt, zoomed);
      if (zoomed) h.view_prepare();
      break;
    }
    case 1:
    case 2: {  // left, right: the arrow at that edge, its height kept on screen
      if (!on_main) break;
      const int32_t mh = mk ? mk->height : 0;
      if (static_cast<float>(h.display_h() - mh) < sp[1]) sp[1] = static_cast<float>(h.display_h() - (mk ? mk->height : 0));
      else if (sp[1] < 0.0f) sp[1] = 0.0f;
      if (code == 1) {
        const int32_t py = ftol(sp[1]);
        marker.set_image(arrows[team * 4 + 0]);
        marker.set_pos(0, py);
      } else {
        const int32_t mw = mk ? mk->width : 0;
        const int32_t py = ftol(sp[1]);
        const int32_t px = h.display_w() - mw;
        marker.set_image(arrows[team * 4 + 1]);
        marker.set_pos(px, py);
      }
      marker.show_for(-1.0f);
      break;
    }
    case 4:
    case 8: {  // top, bottom
      if (!on_main) break;
      const int32_t mw = mk ? mk->width : 0;
      if (static_cast<float>(h.display_w() - mw) < sp[0]) sp[0] = static_cast<float>(h.display_w() - (mk ? mk->width : 0));
      else if (sp[0] < 0.0f) sp[0] = 0.0f;
      if (code == 4) {
        const int32_t px = ftol(sp[0]);
        marker.set_image(arrows[team * 4 + 2]);
        marker.set_pos(px, 0);
      } else {
        const int32_t mh = mk ? mk->height : 0;
        const int32_t py = h.display_h() - mh;
        const int32_t px = ftol(sp[0]);
        marker.set_image(arrows[team * 4 + 3]);
        marker.set_pos(px, py);
      }
      marker.show_for(-1.0f);
      break;
    }
    default: break;
  }
  const int32_t mode = shown_mode();
  if (mode == 2) damage.update_levels();
  else if (mode == 3) h.nav_list_target(nav, h.contact_mech(h.slot_contact(target)));
}

// Mech3.exe 1.2 @ 0x00441410
void HudTarget::draw3d(float dt) {
  switch (shown_mode()) {
    case 1: {
      HudHooks& h = hud_hooks();
      void* o = h.target_valid(target) ? h.target_object(target) : nullptr;
      if (!o) return;
      h.object_view_flag(o, 1);
      h.view_draw_node(view, h.object_node(o));
      h.object_view_flag(o, 0);
      break;
    }
    case 2: damage.draw3d(dt); break;
    default: break;
  }
}

// Mech3.exe 1.2 @ 0x0043c6b0: with the HUD on, the 3D views' rects marked
// (TARGET's by 0x004425d0, which asks its mode again), then in one scene the
// see-through panels: the views' in mode 1, the weapons', the equipment's
// while the weapons show too, the message panel, the objectives'. Then the
// 3D views themselves, TARGET's first.
void Hud::draw3d(float dt) {
  if (!mech) return;
  const bool on = active != 0;
  if (ztwod::host().video_mode() != 0) {
    HudHooks& h = hud_hooks();
    const bool mfd1 = mfd.shown_mode() == 1;
    const bool tgt1 = target.shown_mode() == 1;
    if (on && tgt1) {
      const ztwod::Rect r = frame_rect(*target.shown_rect());  // FIX (B): onto the frame
      h.dirty_rect(&r);
    }
    if (on && mfd1) {
      const ztwod::Rect r = frame_rect(mfd.view->rect);
      h.dirty_rect(&r);
    }
    h.scene_begin();
    if (on) {
      if (tgt1) target.view->panel.draw_quad();
      if (mfd1) mfd.view->panel.draw_quad();
      if (!(weapons.flags & ztwod::kHidden)) weapons.panel.draw_quad();
      if (!(weapons.flags & ztwod::kHidden) && !(equipment.flags & ztwod::kHidden)) equipment.panel.draw_quad();
    }
    message_panel_draw();
    if (!(objectives.flags & ztwod::kHidden)) objectives.panel.draw_quad();
    h.flush_transparent();
    h.scene_end();
  }
  if (on) {
    target.draw3d(dt);
    mfd.draw3d(dt);
  }
}

// Mech3.exe 1.2 @ 0x0043c390: the game's numbers into the pieces each
// frame, from the camera update (0x0040f8d0) for the view's target. With the
// HUD on: the radar's positions, the compass's heading (the mech's flat
// forward), the gauges, TARGET's text, the equipment and MFD; always the
// mission timer (H:M:S), the objectives, TARGET's tracking.
void Hud::data_update(float dt) {
  if (!mech) return;
  HudHooks& h = hud_hooks();
  const float thr = h.controls_throttle(mech) * h.mech_f(mech, 0x528);  // speed_factor
  const float speed = h.mech_f(mech, 0x518) * thr * (thr > 0.0f ? 1.0f : -1.0f);  // max_speed, [0x005c0080]
  gauges.set_speed(speed);
  if (active) {
    float v[3];
    if (radar.active) {
      h.radar_view_dir(v);
      v[1] = 0.0f;
      h.normalize(v);
      radar.set_dir(v);
      h.radar_heading(&radar, mech);
    } else {
      h.mech_radar_dir(mech, v);
      radar.set_dir(v);
    }
    float fwd[3];
    h.mech_forward(mech, fwd);
    compass.set_heading(std::atan2(fwd[0], -fwd[2]));
    gauges.set_throttle(h.controls_throttle_target(mech));
    const float heat = h.mech_f(mech, 0x554);
    const float level = heat > 1.5f ? 1.0f : heat * 0.6666667f;  // [0x005c0084], [0x005c0088]
    gauges.set_heat(level, h.mech_f(mech, 0x558) / dt - h.mech_f(mech, 0x55c));
    gauges.set_coolant(h.mech_f(mech, 0x56c), dt);
    const bool jets = h.mech_part_count(mech, 11) > 0;  // +0x3d8
    gauges.set_jump(jets, h.mech_jump_fuel(mech));
    target.data_text();
    if (!(weapons.flags & ztwod::kHidden)) equipment.data_update(dt);
    mfd.data_update(dt);
  }
  float t = h.mission_time(h.multiplayer_game() != 0);
  const bool negative = t < 0.0f;
  if (negative) t = -t;
  const float minutes = t * 0.016666668f;  // [0x005c0098]
  float hours = std::floor(minutes * 0.016666668f);
  const float mins = std::floor(minutes - hours * 60.0f);  // [0x005c009c]
  // FIX (bug): the original's seconds are t less the minutes alone, so past
  // an hour they run on into the thousands ("01:01:3640"); the hours too here.
  const float secs = zfix::options().bugs ? std::floor(t - mins * 60.0f - hours * 3600.0f) : std::floor(t - mins * 60.0f);
  if (negative) hours = -hours;
  timer.set_text("%02d:%02d:%02d", crt::msvc_ftol(hours), crt::msvc_ftol(mins), crt::msvc_ftol(secs));  // [0x005e3d88]
  objectives.data_update(dt);
  target.track(dt);
  h.network_data(active, dt);
}

// Mech3.exe 1.2 @ 0x0043c820: the debug overlays (hooked), the layer's own
// objects, the timer, the objectives and the radar; the reticle's aim state
// while it is 5 (none set this frame), then the reticle.
void Hud::update(float dt) {
  if (!mech) return;
  HudHooks& h = hud_hooks();
  h.hud_debug(this);
  Layer::update(dt);
  timer.update(dt);
  objectives.update(dt);
  radar.update(dt);
  if (reticle.state == 5) h.reticle_aim_state(&reticle, h.reticle_over_target(&target, reticle.get_x(), reticle.get_y()));
  reticle.update(dt);
  reticle.state = 5;
  h.network_hud();
}

}  // namespace mech3
