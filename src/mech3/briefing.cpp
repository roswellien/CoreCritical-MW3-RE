#include "mech3/briefing.h"

#include <algorithm>

#include <cstring>

#include "gamez/zimage/zimg_library.h"
#include "gamez/zreader/zreader.h"
#include "gamez/ztwod/twod_host.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zvideo/zvid_format.h"
#include "mech3/mech_reader.h"
#include "mech3/content_registry.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
// DIK codes the frame reads (0x00406440).
constexpr int32_t kKeyL = 0x26, kKeyM = 0x32, kKeySpace = 0x39, kKeyS = 0x1f;

// Standalone missions use campaign records for stable saves, while the briefing
// retains Instant Action's unrestricted loadout selection.
bool standalone_content_briefing() {
  const auto* game = current_pilot() ? current_pilot()->campaign() : nullptr;
  return game && !game->content_mission.empty() && game->content_campaign.empty();
}
}  // namespace

int32_t& briefing_reached() {
  static int32_t v = -1;  // [0x005e043c]
  return v;
}

Handle& briefing_voice() {
  static Handle v = 0;  // [0x005f4c84]
  return v;
}

BriefingScreen*& briefing_screen() {
  static BriefingScreen* s = nullptr;  // [0x005f4c80]
  return s;
}

// ---- the current game, as the screen reads it (app_hooks.h) ----

int32_t Engine::game_kind() {
  Pilot* p = current_pilot();
  return p && p->current ? p->current->type() : -1;
}

bool Engine::game_campaign(int32_t* operation, int32_t* mission) {
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  if (!c) return false;
  *operation = c->operation();
  *mission = c->mission_number();
  return true;
}

bool Engine::game_salvage_left() {
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  return c && c->store3c.count() != 0;
}

int32_t Engine::game_multiplayer_kind() {
  MultiplayerGame* g = current_pilot() ? current_pilot()->multiplayer() : nullptr;
  return g ? g->x94 : 0;
}

void Engine::campaign_merge_salvage() {
  if (CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr) c->merge_salvage();
}

// ---- the script ----

// Mech3.exe 1.2 @ 0x00405ed0
void briefing_cue(int32_t marker) { briefing_reached() = marker; }

BriefingVoice::BriefingVoice(Engine& en, const char* sound) : e(en) {
  std::strncpy(name, sound, sizeof name);
}

// Mech3.exe 1.2 @ 0x00405e10
bool BriefingVoice::step(float) {
  const Handle sound = e.sound_find(name);
  if (briefing_voice()) {
    e.voice_stop(briefing_voice());
    briefing_voice() = 0;
  }
  if (!cues) {
    if (sound) briefing_voice() = e.voice_play(sound, volume);
  } else if (!sound) {
    briefing_reached() = 999;
  } else {
    e.sound_cue_briefing(sound);
    briefing_voice() = e.voice_play(sound, volume);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00405db0
bool BriefingMarker::step(float) {
  if (marker == -1) return e.voice_status(briefing_voice()) == 0;
  return briefing_reached() >= marker;
}

// Mech3.exe 1.2 @ 0x00405d50
bool BriefingShow::step(float) {
  obj->show_for(-1.0f);
  obj->dirty();
  return true;
}

// Mech3.exe 1.2 @ 0x00405d90
void BriefingShow::reset() { obj->hide(); }

BriefingScript::BriefingScript(Engine& e) {
  briefing_reached() = -1;
  if (briefing_voice()) e.voice_stop(briefing_voice());
  briefing_voice() = 0;
}

// Mech3.exe 1.2 @ 0x004063e0
void BriefingScript::stop(Engine& e) {
  briefing_reached() = -1;
  if (briefing_voice()) e.voice_stop(briefing_voice());
  briefing_voice() = 0;
  playing = 0;
  at = steps.end();
}

// The head of 0x00406440: the entry playing steps; done, the next one plays
// from the next frame.
void BriefingScript::step(float dt) {
  if (!playing || at == steps.end()) return;
  if ((*at)->step(dt)) ++at;
}

// ---- the map's objects ----

namespace {
uint16_t pack(const int32_t c[3]) {  // 0x00579370
  return zvid::pack_bytes(static_cast<uint8_t>(c[0]), static_cast<uint8_t>(c[1]), static_cast<uint8_t>(c[2]));
}
}  // namespace

// Mech3.exe 1.2 @ 0x00407c90
MapText::MapText() : FontText(nullptr, 0, 0) { hide(); }

// Mech3.exe 1.2 @ 0x00407cf0
void MapText::set_pos(int32_t px, int32_t py) {
  if (needs_render) render();  // 0x00409c60: the width
  ztwod::Rect r{x, y, x + width, y + text_height()};
  set_bg_rect(&r);
  erase();
  x = px;
  y = py;
}

MapCircle::MapCircle(Engine& e, int32_t px, int32_t py, int32_t r, uint32_t color)
    : Frame(px, py, r, color, [] {
        static ztwod::Rect screen;
        screen = ztwod::host().screen_rect();
        return &screen;
      }()),
      e_(e) {
  hide();
  label.hide();
}

// Mech3.exe 1.2 @ 0x004080a0
void MapCircle::set_pos(int32_t px, int32_t py) {
  Object2D::set_pos(px, py);
  set_radius(radius);
}

// Mech3.exe 1.2 @ 0x00408050
void MapCircle::set_radius(int32_t r) {
  radius = r;  // 0x00409d80
  radius_sq = r * r;
  label.set_pos(x + r, y + r);
}

// Mech3.exe 1.2 @ 0x00407e40
void MapCircle::show_for(float seconds) {
  if (sound) ztwod::host().sound_play(static_cast<int32_t>(sound), 1.0f);
  Object2D::show_for(seconds);
}

// Mech3.exe 1.2 @ 0x00407ea0
void MapCircle::update(float dt) { shrink(dt); }

// From the first radius to the last, max(20 x dt, 1) pixels a frame: a pixel
// a frame above 20 fps. FIX (B): with zfix's per_second, 30 pixels a second
// (the 30 fps frame's pixel), the fraction carried.
void MapCircle::shrink(float dt) {
  if (flags & ztwod::kHidden) return;
  bg_rect = {x - radius, y - radius, x + 1 + radius, y + 1 + radius};
  if (end_radius < radius) {
    int32_t step;
    if (zfix::options().per_second) {
      owed_ += zfix::frame_steps(dt);
      step = static_cast<int32_t>(owed_);
      owed_ -= static_cast<float>(step);
    } else {
      float f = static_cast<float>(static_cast<double>(dt) * 20.0);
      if (f < 1.0f) f = 1.0f;
      step = static_cast<int32_t>(f);
    }
    set_radius(radius - step);
  }
  if (radius < end_radius) set_radius(end_radius);
  Object2D::update(dt);
  dirty();
  label.update(dt);
}

// Mech3.exe 1.2 @ 0x004081a0
void MapWaypoint::update(float dt) {
  if ((flags & ztwod::kHidden) || !nav || !e_.mission_map() || !e_.world_up()) return;
  int32_t xy[2];
  e_.map_project(map, e_.nav_position(nav), xy);
  set_pos(xy[0] + shift_x, xy[1]);
  shrink(dt);
}

// 0x005a31e0 (the zTwoD triangle's sized constructor) with the subclass's fields.
MapTriangle::MapTriangle(Engine& e, int32_t px, int32_t py, int32_t size, uint32_t c) : e_(e) {
  Shape::set_pos(px, py);
  set_scale(static_cast<float>(size) * 1.154701f);
  color = c;
  transform();
  hide();
}

// Mech3.exe 1.2 @ 0x00408470: the background at the rectangle's corner.
void MapTriangle::erase() {
  if (background) ztwod::host().blit(background, bg_rect.left, bg_rect.top, 0, &bg_rect);
}

// Mech3.exe 1.2 @ 0x004084b0
void MapTriangle::show_for(float seconds) {
  if (sound) ztwod::host().sound_play(static_cast<int32_t>(sound), 1.0f);
  Object2D::show_for(seconds);
}

// Mech3.exe 1.2 @ 0x004084f0
void MapTriangle::update(float dt) {
  if (flags & ztwod::kHidden) return;
  const float fx = static_cast<float>(get_x()), fy = static_cast<float>(get_y());
  bg_rect.left = static_cast<int32_t>(fx - scale);
  bg_rect.top = static_cast<int32_t>(fy - scale);
  bg_rect.right = static_cast<int32_t>(fx + scale + 1.0f);
  bg_rect.bottom = static_cast<int32_t>(fy + scale + 1.0f);
  Shape::update(dt);
  dirty();
}

// Mech3.exe 1.2 @ 0x00408600
void MapLine::update(float dt) {
  Object2D::update(dt);
  dirty();
}

// ---- the screen ----

// Mech3.exe 1.2 @ 0x00405230. The dialog is the briefing's own section of
// briefing.zrd (multiplayer's from mpinfo.zrd), else "default"; the buttons
// are BRIEFINGDIALOG's. Built, it is drawn once at once (the load starts next).
BriefingScreen::BriefingScreen(Engine& e, const char* name, bool draw_now)
    : MenuScreen(e, 0),
      abort_btn(e, -1),
      launch_btn(e, -1),
      lab_btn(e, -1),
      replay_btn(e, -1),
      salvage_btn(e, -1),
      loading(e, -1),
      realloc_ok(e, 7),
      realloc_list(e, 0, 5) {
  lines.set_pos(300, 12);
  lines.set_font(ztwod::host().font_find("debug_font"));
  const int32_t fast = e.fast_menu();
  const bool mp_info = e.game_multiplayer_kind() > 2;
  tree = zrdr::load("briefing.zrd");
  zrdr::Node* mp_tree = nullptr;
  zrdr::Node* section;
  if (!mp_info) {
    section = zrdr::find(tree, name);
  } else {
    mp_tree = zrdr::load("mpinfo.zrd");
    section = zrdr::find(mp_tree, "briefing");
  }
  if (!section) section = zrdr::find(tree, "default");
  e.briefing_map_image(this, section);
  load_node(tree, section, true);
  add(&lines);
  zrdr::Node* dlg = zrdr::find(tree, "BRIEFINGDIALOG");
  if (dlg) {
    struct {
      HelpButton* b;
      int32_t id;
      const char* name;
    } const bind[] = {{&abort_btn, kBtnAbort, "ABORT"},       {&replay_btn, kBtnReplay, "REPLAY"},
                      {&lab_btn, kBtnMechLab, "MECHLAB"},     {&salvage_btn, kBtnSalvage, "SALVAGE"},
                      {&launch_btn, kBtnLaunch, "LAUNCH"},    {&loading, kBtnLoading, "LOADING"}};
    for (const auto& x : bind) {
      x.b->id = x.id;
      ztwod::bind_button(this, dlg, x.b, x.name);
    }
  }
  for (HelpButton* b : {&abort_btn, &replay_btn, &lab_btn, &salvage_btn, &launch_btn, &loading}) b->enable(0);
  version.set_font(ztwod::host().font_find("debug_font"));
  version.set_text("%s %s", e.message(7), e.version_number());
  if (version.needs_render) version.render();  // 0x00409c60: the width
  const float w = static_cast<float>(version.width + 5);
  const float h = static_cast<float>(version.text_height() + 3);
  version.set_pos(static_cast<int32_t>(640.0f - w), static_cast<int32_t>(480.0f - h));
  add(&version);
  if (section) {
    bind_button(&bar, "TRANSPORT_PROGRESS");
    read_script(section);
  }
  if (e.app_flag_1ba()) lines.show_for(-1.0f);
  else lines.hide();
  if (dlg) {
    ztwod::bind_button(this, dlg, &realloc_list, "REALLOC_LIST");
    ztwod::bind_button(this, dlg, &realloc_ok, "REALLOC_OK");
    show_realloc(false);
  }
  dirty_all();
  set_flags_all(0);
  e.set_fast_menu(fast);
  set_active(1);
  if (mp_tree) zrdr::free_node(mp_tree);
  unload();
  loaded = 1;
  set_loaded(false);
  if (draw_now) draw_first();
}

// The constructor's end (0x00405230): drawn once at once, the load next.
// Ours apart: a wide canvas's screen draws only once widened, or its first
// frame showed at 640 over the menus (GameEngine::construct).
void BriefingScreen::draw_first() {
  Engine& e = engine();
  first_map_w = images[0].image ? images[0].image->width : 0;
  e.sound_update(1, 0);
  e.video_lock();
  update(e.frame_dt());
  e.video_unlock();
  e.video_flip(0, 0, 1, 1);
}

// Mech3.exe 1.2 @ 0x00406170: the script stopped and its entries deleted.
BriefingScreen::~BriefingScreen() {
  Engine& e = engine();
  script.stop(e);
  for (BriefingStep*& s : script.steps) {
    delete s;
    s = nullptr;
  }
  script.stop(e);  // the list's own destructor (0x004063e0 again)
  script.steps.clear();
  for (ztwod::Object2D*& o : objects) {
    delete o;
    o = nullptr;
  }
  objects.clear();
  if (briefing_screen() == this) briefing_screen() = nullptr;
}

// Mech3.exe 1.2 @ 0x00406440. FIX (B): the bar crept up 0.005 a frame; with
// zfix's per_second, 0.15 a second (0.005 at 30 fps).
void BriefingScreen::update(float dt) {
  Engine& e = engine();
  script.step(dt);
  set_loaded(e.missions_loaded());
  float v = bar.value;
  if (v < target) v += 0.005f * zfix::frame_steps(dt);
  if (1.0f < v) v = 1.0f;
  else if (v < 0.0f) v = 0.0f;
  if (loaded) v = 1.0f;
  bar.set_value(v);
  Window::update(dt);
  if (loaded) {
    if (e.key_state(kKeyL) & 3) launch_btn.on_left_up();
    if (e.key_state(kKeyM) & 3) lab_btn.on_left_up();
  }
  if ((e.key_state(kKeySpace) & 3) || (e.key_state(kKeyS) & 3)) {
    e.input_clear();
    script.stop(e);
  }
}

// Mech3.exe 1.2 @ 0x00404c30
void BriefingScreen::on_button(int32_t id, int32_t a, int32_t b) {
  (void)a, (void)b;
  Engine& e = engine();
  switch (id) {
    case kBtnAbort:
      script.stop(e);
      abort();
      break;
    case kBtnLaunch:
      launch();
      break;
    case kBtnMechLab:
      script.stop(e);
      lab_btn.on_leave();
      e.mech_lab_open(e.store_handle(build_lab_store()), 0);
      break;
    case kBtnReplay:  // every entry back to its start, then played from the first
      script.stop(e);
      for (BriefingStep* s : script.steps) s->reset();
      script.step(0.0f);  // (stopped: nothing)
      script.start();
      break;
    case kBtnSalvage:
      script.stop(e);
      salvage_btn.on_leave();
      e.salvage_open();
      salvage_seen = 1;
      break;
    case kBtnReallocOk:
      show_realloc(false);
      break;
    default: break;
  }
}

// Mech3.exe 1.2 @ 0x00408ef0. LAUNCH follows the load; the others by the
// game: a campaign's MECHLAB and SALVAGE not at Op 1 Mission 1; Instant
// Action has no SALVAGE or REPLAY, training no MECHLAB either, multiplayer
// only LAUNCH, and launches by itself.
void BriefingScreen::set_loaded(bool on) {
  if ((loaded != 0) == on) return;
  Engine& e = engine();
  bool lab = on, abort_on = on, replay = on, salvage = false, auto_launch = false;
  switch (standalone_content_briefing() ? kInstantAction : e.game_kind()) {
    case kMechLab:
      salvage = on;
      break;
    case kCampaign: {
      int32_t op = 0, mission = 0;
      e.game_campaign(&op, &mission);
      if (op == 1 && mission == 1) lab = false;
      salvage = lab;
      if (on) salvage_seen = 0;
      break;
    }
    case kInstantAction:
      salvage_btn.hide();
      replay_btn.hide();
      replay = false;
      break;
    case kMultiplayer:
      lab_btn.hide();
      salvage_btn.hide();
      replay_btn.hide();
      abort_btn.hide();
      lab = abort_on = replay = false;
      auto_launch = on;
      break;
    case kTraining:
      salvage_btn.hide();
      lab_btn.hide();
      replay_btn.hide();
      lab = replay = false;
      break;
    default: break;
  }
  if (on) cursor_obj.show_for(-1.0f);  // 0x00409a90
  else cursor_obj.hide();
  abort_btn.enable(abort_on);
  launch_btn.enable(on);
  lab_btn.enable(lab);
  salvage_btn.enable(salvage);
  replay_btn.enable(replay);
  loaded = on ? 1 : 0;
  if (loaded) loading.hide();
  if (loaded) lance_loadouts();
  if (auto_launch) launch();
}

// Mech3.exe 1.2 @ 0x00405050: once loaded, the pilot saved, a campaign's
// salvage merged, the mission unloaded, back to the main menu.
void BriefingScreen::abort() {
  if (!loaded) return;
  Engine& e = engine();
  e.pilot_record_mission(e.pilot(), 0);
  if (e.game_kind() == kCampaign) e.campaign_merge_salvage();
  e.mission_cleanup(1);
  e.replace_state(2);
}

// Mech3.exe 1.2 @ 0x00404f70. A campaign with salvage not looked at asks
// first (messages 0x1e0, 0x1e1); a failed launch goes back to the main menu.
void BriefingScreen::launch() {
  Engine& e = engine();
  const int32_t kind = e.game_kind();
  if (kind == kCampaign) {
    if (!salvage_seen && e.game_salvage_left()) {
      cursor_obj.erase();
      if (e.message_box(1, 0x1e0, 0x1e1) != 1) return;
    }
  } else if (kind == kMultiplayer) {
    e.multiplayer_launch_prep();
  }
  if (!e.mission_launch()) {
    e.mission_cleanup(1);
    e.replace_state(2);
  } else {
    e.replace_state(5);
  }
}

// Inline in 0x00405230: SOUND ("voice" ... MARKERS ((name (..) ...) ...)):
// the voice, then for each marker its step and the objects shown at it
// (0x00406650 reads each), then the end of the voice.
void BriefingScreen::read_script(zrdr::Node* section) {
  zrdr::Node* sound = zrdr::find(section, "SOUND");
  if (!sound) return;
  Engine& e = engine();
  script.steps.push_back(new BriefingVoice(e, sound->list[1].s));
  zrdr::Node* markers = zrdr::find(sound, "MARKERS");
  // FIX (bug): no MARKERS crashed (read unchecked); here, no map steps.
  const int32_t n = markers ? markers->list[0].i - 1 : 0;
  for (int32_t i = 0; i < n; ++i) {
    script.steps.push_back(new BriefingMarker(e, i));
    zrdr::Node* objs = markers->list[i + 1].list;
    const int32_t m = (objs[0].i - 1) / 2;
    for (int32_t j = 0; j < m; ++j) {
      ztwod::Object2D* o = read_object(&objs[1 + 2 * j]);
      if (!o) continue;
      o->set_background(static_bg, nullptr);
      script.steps.push_back(new BriefingShow(o));
      add(o);
    }
  }
  script.steps.push_back(new BriefingMarker(e, -1));
}

// Mech3.exe 1.2 @ 0x00406650. `entry` is a MARKERS step's name, its list
// after it. Texts take FONT (else brief_grey), the shapes COLOR (else white).
ztwod::Object2D* BriefingScreen::read_object(zrdr::Node* entry) {
  Engine& e = engine();
  ztwod::Host& h = ztwod::host();
  int32_t at[2] = {320, 240};
  int32_t color[3] = {255, 255, 255};
  const char* kind = entry->s;
  zrdr::Node* v = entry + 1;
  ztwod::Object2D* made = nullptr;
  const char* font_name = zrdr::get_string(v, "font");
  const Handle font = h.font_find(font_name ? font_name : "brief_grey");
  zrdr::get_ivec3(v, "color", color);
  static const char* const kGameTexts[] = {"game_type", "db_name", "scenario_name", "scenario_desc"};
  int32_t game_text = -1;
  for (int32_t i = 0; i < 4; ++i)
    if (!std::strcmp(kind, kGameTexts[i])) game_text = i;
  if (!std::strcmp(kind, "circle")) {
    int32_t radius[2] = {10, 3};
    zrdr::get_ivec2(v, "at", at);
    zrdr::get_ivec2(v, "radius", radius);
    auto* c = new MapCircle(e, at[0], at[1], radius[0], pack(color));
    c->set_radius(radius[0]);
    c->end_radius = radius[1];
    if (const char* snd = zrdr::get_string(v, "sound")) c->sound = static_cast<Handle>(h.sound_find(snd));
    made = c;
  } else if (!std::strcmp(kind, "text")) {
    if (zrdr::get_ivec2(v, "at", at)) {
      const char* text = h.message(zrdr::get_string(v, "string"));
      auto* t = new MapText();
      t->set_pos(at[0], at[1]);
      t->set_font(font);
      t->set_text_plain(text);
      made = t;
    }
  } else if (game_text >= 0) {
    if (zrdr::get_ivec2(v, "at", at)) {
      const char* text = e.game_text(4 + 4 * game_text);  // the game's vt+0x04 .. +0x10
      auto* t = new MapText();
      t->set_pos(at[0], at[1]);
      t->set_font(font);
      t->set_text_plain(text);
      t->show_for(-1.0f);
      made = t;
    }
  } else if (!std::strcmp(kind, "bitmap")) {
    if (zrdr::get_ivec2(v, "at", at)) {
      auto* b = new ztwod::Image(zrdr::get_string(v, "image"), at[0], at[1], 0);  // vtable 0x005bbf30
      b->hide();
      made = b;
    }
  } else if (!std::strcmp(kind, "waypoint")) {
    // The nav point is looked up by LABEL, which also names it (NAME is not read).
    int32_t radius[2] = {10, 3};
    const char* name = zrdr::get_string(v, "label");
    const char* text = h.message(zrdr::get_string(v, "label"));
    if (const Handle navs = e.mission_navs()) {
      if (const Handle nav = e.nav_find(navs, name)) {
        zrdr::get_ivec2(v, "radius", radius);
        zrdr::get_ivec3(v, "color", color);
        if (!text) text = name;
        const Handle map = e.mission_map();
        auto* w = new MapWaypoint(e, 0, 0, 0, pack(color));
        w->map = map;
        w->nav = nav;
        int32_t xy[2];
        e.map_project(map, e.nav_position(nav), xy);
        w->set_radius(radius[0]);
        w->end_radius = radius[1];
        w->label.set_text_id(0x200, text);  // (with no text, 0x0040a930's: never, text is the name then)
        w->label.show_for(-1.0f);
        w->label.set_font(font);
        w->label.set_background(e.map_image(map), nullptr);
        w->Object2D::set_pos(xy[0], xy[1]);  // 0x00409a40
        w->radius_sq = w->radius * w->radius;
        w->label.set_pos(w->x + w->radius, w->y + w->radius);
        made = w;
      }
    }
  } else if (!std::strcmp(kind, "triangle")) {
    int32_t height[2] = {10, 3};
    zrdr::get_ivec2(v, "at", at);
    zrdr::get_ivec2(v, "height", height);
    auto* t = new MapTriangle(e, at[0], at[1], height[0], pack(color));
    if (const char* snd = zrdr::get_string(v, "sound")) t->sound = static_cast<Handle>(h.sound_find(snd));
    t->set_scale(static_cast<float>(height[0]));
    t->end = static_cast<float>(height[1]);
    made = t;
  } else if (!std::strcmp(kind, "line")) {
    if (zrdr::Node* pts = zrdr::find(v, "points")) {
      auto* l = new MapLine();
      l->hide();
      const int32_t n = pts->list[0].i - 1;
      int32_t k = 0;
      for (int32_t i = 0; i < n; ++i) {
        int32_t p[2];
        if (zrdr::get_ivec2(&pts->list[i + 1], p)) l->set_point(k++, p[0], p[1]);
      }
      l->color = pack(color);  // 0x00409e00
      made = l;
    }
  }
  if (!made) e.report(0x200, "C:\\zipper\\Mech3\\Briefing.cpp", 0x499, "Failed to create locator (%s)", kind);
  else objects.push_back(made);
  return made;
}

// Mech3.exe 1.2 @ 0x00405120. FIX (bug): the line went in as the format
// (a '%' in it read arguments that are not there).
void BriefingScreen::log(const char* s) { lines.set_text("%s", s); }

// ---- the reallocation (0x00408660..0x00408bd0, 0x00404ae0) ----

// The engine's defaults: the ported pilot's games.
Store* Engine::game_store() {
  Pilot* p = current_pilot();
  return p && p->current ? &p->current->store : nullptr;
}
Store* Engine::campaign_salvage() {
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  return c ? &c->store3c : nullptr;
}
int32_t Engine::campaign_units() {
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  return c ? c->a8 : 0;
}

namespace {
constexpr float kTonsPerUnit = 300.0f;  // 0x00470cc0
constexpr int32_t kMechsPerUnit = 2;    // 0x00470cd0
}  // namespace

// Mech3.exe 1.2 @ 0x00409fd0
float store_tons(const Store& s) {
  float t = 0.0f;
  for (const ConfigItem* it : s.items) t = static_cast<float>(it->weight(-1) + t);
  return t;
}

// Mech3.exe 1.2 @ 0x00472970
int32_t store_mechs(const Store& s) {
  int32_t n = 0;
  for (const ConfigItem* it : s.items)
    if (it->kind() == 0) n += it->count;
  return n;
}

// Mech3.exe 1.2 @ 0x00473510
int32_t store_count_like(const Store& s, const ConfigItem* item) { return s.count_like(item); }

// Mech3.exe 1.2 @ 0x00408690. A store's own (b10) items are not taken from
// `from` (0x00471d20 refuses), but still copied: kept.
void store_take(Store& from, const Store& owner, Store& out, float tons, int32_t mechs) {
  const bool limited = !(mechs == 0 && tons == 0.0f);
  float have_tons = store_tons(out);
  int32_t have_mechs = store_mechs(out);
  for (ConfigItem* it : owner.items) {
    if (!it) continue;
    const int32_t n = it->count;
    int32_t k = std::min(store_count_like(from, it), n);
    const float room = tons - have_tons;
    if (k <= 0) continue;
    if (limited) {
      if (it->kind() == 0) {
        k = std::min(k, mechs - have_mechs);
      } else {
        while (k > 0 && it->weight(k) > static_cast<double>(room)) --k;
      }
    }
    if (k <= 0) continue;
    out.add_copy(it, k);
    from.remove(it, k);
    have_tons = store_tons(out);
    have_mechs = store_mechs(out);
  }
}

// Mech3.exe 1.2 @ 0x00408850. A mech short of room counts its shortfall as
// over: with +0xa8 at 0 (never in play) the salvage gains more mechs than
// there were (campaign.md section 9).
void store_reallocate(Store& lab, Store& game, Store& salvage, Store* realloc, int32_t units) {
  const float tons = static_cast<float>(kTonsPerUnit * static_cast<float>(units));
  const int32_t mechs = units * kMechsPerUnit;
  Store fit(0), over(0);
  store_take(lab, game, fit, tons, mechs);
  store_take(lab, salvage, over, 0.0f, 0);
  game.assign(fit);
  salvage.assign(over);
  fit.clear();
  over.clear();
  for (const ConfigItem* it : lab.items) {
    const float used = static_cast<float>(store_tons(fit) + store_tons(game));
    const float room = tons - used;
    const int32_t n = it->count;
    int32_t k = 0;
    if (it->kind() == 0) {
      const int32_t free = mechs - (store_mechs(game) + store_mechs(fit));
      k = free < n ? free : n;
    } else {
      k = n;
      while (k > 0 && it->weight(k) > static_cast<double>(room)) --k;
    }
    const int32_t rest = n - k;
    if (k > 0) fit.add_copy(it, k);
    if (rest > 0) over.add_copy(it, rest);
  }
  if (realloc) realloc->assign(over);
  game.add_all(fit);
  salvage.add_all(over);
}

// Mech3.exe 1.2 @ 0x00408b40: a campaign's lab store is the game's store and
// the salvage together; any other game's lab works on the game's store.
Store* lab_store_build(Store& lab, Store* game, Store* salvage) {
  lab.clear();
  if (!salvage) return game;
  if (game) lab.assign(*game);  // 0x00408660
  lab.add_all(*salvage);
  return &lab;
}
Store* BriefingScreen::build_lab_store() {
  Engine& e = engine();
  if (standalone_content_briefing()) return e.game_store();
  return lab_store_build(lab_store, e.game_store(), e.campaign_salvage());
}

// Mech3.exe 1.2 @ 0x00408bd0
void BriefingScreen::refresh() {
  if (standalone_content_briefing()) return;
  Engine& e = engine();
  Store* salvage = e.campaign_salvage();
  Store* game = e.game_store();
  if (!salvage || !game) return;
  store_reallocate(lab_store, *game, *salvage, &realloc_store, e.campaign_units());
}

// Mech3.exe 1.2 @ 0x00404ae0: shown only with something left over; the six
// buttons off while it is.
void BriefingScreen::show_realloc(bool on) {
  Engine& e = engine();
  const bool show = on && realloc_store.count() != 0;  // 0x0040a0c0
  HelpButton* bs[] = {&abort_btn, &launch_btn, &lab_btn, &replay_btn, &salvage_btn, &loading};
  for (HelpButton* b : bs) b->enable(show ? 0 : 1);
  realloc_list.x209 = 0;  // 0x0040a780 .. 0x0040a7c0
  realloc_list.x20a = 1;
  realloc_list.x20b = 0;
  realloc_list.x20c = 0;
  realloc_list.x20d = 0;
  realloc_list.x218 = 0;  // 0x0040a760
  realloc_list.store = e.store_handle(&realloc_store);
  realloc_list.refill();
  if (show) realloc_list.show_for(-1.0f), realloc_ok.show_for(-1.0f);  // 0x00409a90
  else realloc_list.hide(), realloc_ok.hide();
  realloc_list.enable(show ? 1 : 0);
  realloc_ok.enable(show ? 1 : 0);
  dirty_all();
}

// ---- the lance's loadouts (0x00408c30) ----

// The game's vt+0x38 and vt+0x34: the campaign's (0x00435180, 0x00435110),
// Instant Action's (0x00436a30, 0x004368b0) or a network game's (0x004a5270,
// one; 0x00437800, its +0x20). The port had the campaign's only, so the lab
// opened from Instant Action's briefing had no mech (playtest W37), and from
// the lobby too (W42).
int32_t Engine::game_slot_count() {
  Pilot* p = current_pilot();
  if (CampaignGame* c = p ? p->campaign() : nullptr) return c->slot_count();
  if (ActionGame* a = p ? p->action() : nullptr) return a->slot_count();
  if (p && p->multiplayer()) return 1;
  return 0;
}
GameSlot* Engine::game_slot(int32_t i) {
  Pilot* p = current_pilot();
  if (CampaignGame* c = p ? p->campaign() : nullptr) return c->slot_at(i);
  if (ActionGame* a = p ? p->action() : nullptr) return a->slot_at(i);
  if (MultiplayerGame* m = p ? p->multiplayer() : nullptr) return m->slot_at(i);
  return nullptr;
}

void lance_slot(GameSlot& s, Store& store, bool keep) {
  int32_t mech = s.mech;
  if (mech == -1) return;
  const Config* cfg = s.config;
  const int32_t count = chassis_count();
  if (mech < 0 || mech >= count) mech = -1;
  if (!keep) {
    // Search each session chassis once. A fixed stock modulus could never
    // return to an added chassis, hanging when the store had no replacement.
    int32_t checked = 0;
    while (mech >= 0 && !store.has_mech(mech) && checked < count) {
      mech = (mech + 1) % count;
      ++checked;
    }
    if (checked == count) mech = -1;
  }
  const MechDfn* dfn = mech_dfn(mech);  // 0x004846a0: none for -1
  if (!dfn || mech == -1) {
    s.mech = -1;  // 0x005a2ba0
    s.set_config(nullptr);
    return;
  }
  if (cfg && cfg->mech != mech) {
    s.set_config(nullptr, &store, true);
    cfg = s.config;
  }
  s.mech = mech;
  if (!cfg) {
    const Config* first = dfn->configs && !dfn->configs->configs.empty() ? dfn->configs->configs[0] : nullptr;
    if (keep) s.set_config(first);
    else s.set_config(first, &store, true);
  } else if (keep) {
    s.set_config(cfg);
  } else {
    Config* copy = config_copy_store(cfg->name, *cfg, &store, false);  // 0x00424010(store, 0)
    s.set_config(nullptr);
    s.set_config(copy, &store, true);
    delete copy;
  }
  if (!keep) store.remove_mech(mech);
}

// Mech3.exe 1.2 @ 0x00408c30, when the load ends (set_loaded): the lance as
// the lab's store allows, then the split (0x00408bd0).
void BriefingScreen::lance_loadouts() {
  Engine& e = engine();
  Store* store = build_lab_store();
  int32_t op = 0, mission = 0;
  const bool campaign = e.game_campaign(&op, &mission);
  std::vector<GameSlot*> slots;
  for (int32_t i = 0, n = e.game_slot_count(); i < n; ++i) slots.push_back(e.game_slot(i));
  if (store) lance_fit(*store, slots, lance_keeps(campaign, op, mission));
  refresh();
}

bool lance_keeps(bool campaign, int32_t op, int32_t mission) { return campaign && op == 4 && mission == 6; }

void lance_fit(Store& store, const std::vector<GameSlot*>& slots, bool keep) {
  for (size_t i = 0; i < slots.size(); ++i)
    if (slots[i]) lance_slot(*slots[i], store, i > 0 && keep);
}

}  // namespace mech3

