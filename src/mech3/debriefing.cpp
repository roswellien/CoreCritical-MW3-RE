#include "mech3/debriefing.h"
#include "mech3/options.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "crt/msvc_math.h"
#include "gamez/zreader/zreader.h"
#include "gamez/ztwod/twod_host.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zvideo/zvid_format.h"
#include "mech3/mission.h"
#include "mech3/pilot.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

// ---- the engine's defaults (app_hooks.h) ----

bool Engine::mission_failed() { return missions().aborting != 0; }
bool Engine::mission_cheated() { return options().cheated != 0; }  // [0x00609994]: the setters' and 0x00491d20's
float Engine::mission_time() { return missions().time; }
void Engine::pilot_save_now() {
  if (Pilot* p = current_pilot()) p->save();
}
void Engine::campaign_reset_lancemates() {
  if (CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr) c->reset_lancemates(false);
}
void Engine::campaign_set_b0(uint8_t v) {
  if (CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr) c->b0 = v;
}
int32_t Engine::campaign_score() {
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  return c ? c->c4 : 0;
}
int32_t Engine::campaign_record(int32_t i) {
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  return c && i >= 0 && i < 4 ? c->b4[i] : 0;
}
bool Engine::mission_ended() { return missions().ended != 0; }
void Engine::action_rounds(int32_t* mode, int32_t* rounds, int32_t* waves) {
  ActionGame* g = current_pilot() ? current_pilot()->action() : nullptr;
  *mode = g ? g->mode : 0;
  *rounds = g ? g->x6c : 0;
  *waves = g ? g->x70 : 0;
}
void Engine::win_keep_scores(Handle screen, int32_t n) {
  for (int32_t i = 0; i < n; ++i) {
    const uint32_t v = field32(screen, 0x13be4 + 4 * i);
    set_field32(pilot_campaign(pilot()), 0xb4 + 4 * i, v);
  }
  const uint32_t v = field32(screen, 0x13be0);
  set_field32(pilot_campaign(pilot()), 0xc4, v);
}

// ---- the mech lab's mech button ----

// Mech3.exe 1.2 @ 0x0044a770
MechButton::MechButton(Engine& e) : HelpButton(e, -1) {}

// Mech3.exe 1.2 @ 0x0044a840: the name joins the dialog, hidden.
bool MechButton::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool r = HelpButton::read_control(node, dialog);
  dialog->add(&name);
  name.set_font(ztwod::host().font_find("lab_small_white"));
  show_name(false);
  return r;
}

// Mech3.exe 1.2 @ 0x0044aa70: the mech's name under the image.
void MechButton::show_name(bool on) {
  if (!on) {
    name.hide();
    return;
  }
  name.show_for(-1.0f);
  name.set_text_plain(e_.mech_name_of(mech));
  const int32_t h = image ? image->height : 0;
  name.set_pos(get_x(), get_y() + h);
}

// ---- the lance cards ----

// Mech3.exe 1.2 @ 0x0047af70
LanceCard::LanceCard(Engine& e) : MechButton(e) {}

// Mech3.exe 1.2 @ 0x0047b180
LanceCard::~LanceCard() {
  vehicle = 0;
  display.reset();
}

// Mech3.exe 1.2 @ 0x0047b2b0: Object2D's update (no blink), then the line's
// and the display's.
void LanceCard::update(float dt) {
  if (flags & ztwod::kHidden) return;
  Object2D::update(dt);
  line.update(dt);
  if (display) display->update(dt);
}

// Mech3.exe 1.2 @ 0x0047b070: HTAL places the mech's display (pos from the
// card, height, width, space). FIX (bug): a missing key left the stack's
// value; here 0.
bool LanceCard::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool r = MechButton::read_control(node, dialog);
  if (zrdr::Node* h = zrdr::find(node, "HTAL")) {
    int32_t pos[2] = {}, height = 0, width = 0, space = 0;
    zrdr::get_ivec2(h, "pos", pos);
    pos[0] += get_x();
    pos[1] += get_y();
    zrdr::get_int(h, "height", &height);
    zrdr::get_int(h, "width", &width);
    zrdr::get_int(h, "space", &space);
    display = std::make_unique<LanceDisplay>([this](int32_t id) { return e_.message(id); }, pos[0], pos[1], height, width, space);
  }
  return r;
}

// Mech3.exe 1.2 @ 0x0047b2f0: the rank by the score, a step per 100,000
// (0 to 4): its icon and its name (messages 0x1d8..0x1dc).
void RankIcon::set_rank(int32_t score) {
  int32_t r = score / 100000;
  if (r > 4) r = 4;
  else if (r < 0) r = 0;
  char name[32];
  std::snprintf(name, sizeof name, "icon_rank%d_green", r);
  if (img_normal) ztwod::host().image_release(img_normal);
  img_normal = set_image(ztwod::host().image_get(name));
  if (!labels.empty()) labels[0]->set_text_plain(e_.message(0x1d8 + r));
}

// Mech3.exe 1.2 @ 0x0047b210: the display laid out for the vehicle and
// filled, the card's image its mech's (green).
void LanceCard::set_vehicle(Handle v) {
  vehicle = v;
  if (!v || !display) return;
  Engine::LanceArmor a;
  e_.vehicle_armor(v, &a);
  display->layout(a);
  display->fill(a);
  set_mech_image(a.type, 1);
}

// Mech3.exe 1.2 @ 0x0047b260
void LanceCard::set_mech_image(int32_t m, int32_t color) {
  zimg::Texture* old = img_normal;
  img_normal = set_image(static_cast<zimg::Texture*>(nullptr));
  ztwod::host().image_release(old);  // 0x00517b10, none or not
  zimg::Texture* t = set_image(mech_icon(e_, m, color));
  mech = m;
  img_normal = t;
}

// Mech3.exe 1.2 @ 0x0044a9b0
zimg::Texture* mech_icon(Engine& e, int32_t m, int32_t color) {
  const char* name = e.mech_name_of(m);  // 0x00484650
  std::string s = std::string("icon_") + (name ? name : "(null)") + "_";
  static const char* const kColors[] = {"red", "green", "yellow", "grey"};
  if (color >= 0 && color < 4) s += kColors[color];
  return ztwod::host().image_get(s.c_str());
}

// ---- the lance card's armor display (0x0047e520..0x0047f140) ----

namespace {
// 0x005ac340 on the x87's product (truncated).
int32_t ftol(double v) { return crt::msvc_ftol(v); }
}  // namespace

// Mech3.exe 1.2 @ 0x0047e520: the bar at (px, py), w wide and h tall,
// all standing (the lost part an empty box for now); the rear's dimmer.
void LanceDisplay::Bar::place(int32_t px, int32_t py, int32_t w, int32_t h, bool dim) {
  full.set_pos(px, py);
  width = w;
  height = h;
  x = px;
  y = py;
  const float fx = static_cast<float>(px), fy = static_cast<float>(py);
  const float fx1 = static_cast<float>(px + w + 1), fy1 = static_cast<float>(py + h);
  full.set_point(0, fx, fy);
  full.set_point(1, fx, fy1);
  full.set_point(2, fx1, fy1);
  full.set_point(3, fx1, fy);
  full_h = h;
  full_w = w;
  lost.set_point(0, 0.0f, 0.0f);
  lost.set_point(1, 0.0f, 0.0f);
  lost.set_point(2, 1.0f, 0.0f);
  lost.set_point(3, 1.0f, 0.0f);
  lost_h = 0;
  lost_w = 0;
  full.color = zvid::pack_bytes(0, dim ? 0xaa : 0xff, 0);
  lost.color = zvid::pack_bytes(dim ? 0xaa : 0xff, 0, 0);
}

// Mech3.exe 1.2 @ 0x0047e680 (its first argument, the display's scale, unused).
void LanceDisplay::Bar::fill(float left) {
  int32_t n = ftol(std::floor(static_cast<float>(height) * left));  // single precision, as in play
  if (n > height) n = height;
  else if (n < 0) n = 0;
  const float fx = static_cast<float>(x), fx1 = static_cast<float>(x + width + 1);
  full.set_point(0, fx, static_cast<float>(y));
  full.set_point(1, fx, static_cast<float>(y + n));
  full.set_point(2, fx1, static_cast<float>(y + n));
  full.set_point(3, fx1, static_cast<float>(y));
  full_w = width;
  full_h = n;
  const int32_t rest = height - n, y2 = y + n;
  lost.set_point(0, fx, static_cast<float>(y2));
  lost.set_point(1, fx, static_cast<float>(y2 + rest));
  lost.set_point(2, fx1, static_cast<float>(y2 + rest));
  lost.set_point(3, fx1, static_cast<float>(y2));
  lost_h = rest;
  lost_w = width;
}

// Mech3.exe 1.2 @ 0x0047e810
void LanceDisplay::Bar::update(float dt) {
  full.Object2D::update(dt);
  lost.update(dt);
}

// Mech3.exe 1.2 @ 0x0047e840
LanceDisplay::LanceDisplay(const std::function<const char*(int32_t)>& message, int32_t x0, int32_t y0, int32_t h,
                           int32_t w, int32_t sp)
    : Line2D(0, 0, 0, 0, 0), height(h), width(w), space(sp) {
  px = x0;
  py = y0;
  const Handle font = ztwod::host().font_find("hud_htal");
  for (ztwod::FontText& l : labels) l.set_font(font);
  for (int i = 0; i < 4; ++i) labels[i].set_text_plain(message(0xf3b + i));  // H, T, A, L
  line.color = zvid::pack_bytes(1, 0xa2, 0x1f);
}

// Mech3.exe 1.2 @ 0x0047eb60
void LanceDisplay::place(int32_t x, int32_t y, int32_t h, int32_t w, int32_t sp) {
  px = x;
  py = y;
  height = h;
  width = w;
  space = sp;
}

// Mech3.exe 1.2 @ 0x0047f0e0
void LanceDisplay::update(float dt) {
  for (Bar& b : bars) b.update(dt);
  line.update(dt);
  for (ztwod::FontText& l : labels) l.update(dt);
}

// Mech3.exe 1.2 @ 0x0047eba0: the bars side by side: the head, the torso's
// six (front and rear of each side and the centre; half wide), the arms,
// the legs, scaled to the most armor any vehicle shown had.
void LanceDisplay::layout(const LanceArmor& a) {
  int32_t x = px;
  const int32_t y = py;
  const int32_t label_y = y - labels[0].text_height();
  for (int i = 0; i < 11; ++i)
    if (most <= a.armor[i]) most = a.armor[i];
  const int32_t half = width >> 1;
  scale = static_cast<float>(height) / static_cast<float>(most);  // the x87 at 24 bits (CLAUDE.md)
  // (No armor at all: most 0, scale inf, armor x scale NaN, ftol's low dword 0: height 0, as the exe.)
  const int32_t step = (half & ~1) > 4 ? half >> 1 : 2;
  left = x;
  top = y;
  bottom = y + height;
  auto h = [&](int i) { return ftol(static_cast<float>(a.armor[i]) * scale); };
  bars[6].place(x, y, width, h(6), false);  // (the quotient still on the x87: the same at 24 bits)
  labels[0].set_pos(x, label_y);
  x += space + width;
  bars[1].place(x, y, half, h(1), false);
  const int32_t sh = step + half;
  x += sh;
  bars[4].place(x, y, half, h(4), true);
  x += sh;
  bars[0].place(x, y, half, h(0), false);
  x += sh;
  labels[1].set_pos(x, label_y);
  bars[3].place(x, y, half, h(3), true);
  x += sh;
  bars[2].place(x, y, half, h(2), false);
  x += sh;
  bars[5].place(x, y, half, h(5), true);
  x += half + space;
  bars[7].place(x, y, width, h(7), false);
  labels[2].set_pos(half + x, label_y);
  x += step + width;
  bars[8].place(x, y, width, h(8), false);
  x += space + width;
  bars[9].place(x, y, width, h(9), false);
  labels[3].set_pos(half + x, label_y);
  x += step + width;
  bars[10].place(x, y, width, h(10), false);
  x += width;
  right = x;
  line.set(left, top, x, top);
  ++right;
}

// Mech3.exe 1.2 @ 0x0047ef10: each bar's standing armor.
void LanceDisplay::fill(const LanceArmor& a) {
  for (int i = 0; i < 11; ++i) bars[i].fill(a.left[i]);
}

// ---- the screen ----

namespace {
// The Win Movie State ([0x00608be8]): the campaign's end.
constexpr Handle kWinMovieState = 0x608be8;
// The IA Dummy State ([0x00608bd0]): Instant Action's tallies, then the main menu.
constexpr Handle kIaDummyState = 0x608bd0;

ztwod::Text* label(ztwod::Control& c, size_t i) { return i < c.labels.size() ? c.labels[i] : nullptr; }
ztwod::Text* help(HelpButton& b) { return b.help.empty() ? nullptr : b.help[0]; }
}  // namespace

// Mech3.exe 1.2 @ 0x0047b430
DebriefScreen::DebriefScreen(Engine& e, Handle state, int32_t campaign, int32_t mission, const uint8_t* w)
    : MenuScreen(e, state),
      continue_btn(e, -1),
      replay_btn(e, -1),
      abort_btn(e, -1),
      cards{LanceCard(e), LanceCard(e), LanceCard(e), LanceCard(e)},
      rank(e),
      salvage(e, 0, 5),
      won(w) {
  clean = e.mission_cheated() ? 0 : 1;
  const int32_t fast = e.exchange_fast_menu(0);
  if (load("dlgWin.zrd", "WINMENU", true)) {
    bind_primitive(&background, "WIN_BACKGROUND");
    abort_btn.id = kAbort;
    bind_button(&abort_btn, "ICON_ABORT");
    replay_btn.id = kReplay;
    bind_button(&replay_btn, "ICON_REPLAY");
    continue_btn.id = kContinue;
    bind_button(&continue_btn, "ICON_CONTINUE");
    static const char* const kCards[] = {"ICON_PLAYER0", "ICON_PLAYER1", "ICON_PLAYER2", "ICON_PLAYER3"};
    for (int i = 0; i < 4; ++i) bind_button(&cards[i], kCards[i]);
    bind_button(&title, "WIN_TITLE");
    bind_button(&salvage, "SALVAGE_LIST");
    bind_button(&rank, "ICON_RANK");
    unload();
  }
  e.set_fast_menu(fast);
  set_flags_all(0);
  set_active(1);
  const int32_t kind = e.game_kind();
  abort_btn.enabled = 1;
  abort_btn.refresh();
  continue_btn.enabled = 1;
  continue_btn.refresh();
  // The mission's time. FIX (bug): the seconds were the time less the
  // minutes only, so past an hour 01:01:3640 (fixes.md, as the HUD's timer).
  const float t = e.mission_time();
  const float m = t * 0.016666668f;
  const float hours = std::floor(m * 0.016666668f);
  const float minutes = std::floor(m - hours * 60.0f);
  const float seconds = zfix::options().bugs ? std::floor(t - hours * 3600.0f - minutes * 60.0f)
                                             : std::floor(t - minutes * 60.0f);
  char time[32];
  std::snprintf(time, sizeof time, "%02d:%02d:%02d", static_cast<int32_t>(hours), static_cast<int32_t>(minutes),
                static_cast<int32_t>(seconds));
  if (kind == kInstantAction) {
    const char* name = e.game_text(0xc);
    const std::string s = e.message_format_is(0xdc0, campaign, name);
    if (ztwod::Text* l = label(title, 2)) {
      if (!s.empty()) l->set_text("%s", s.c_str());
      else l->set_text_plain(e.game_text(0xc));
    }
    if (ztwod::Text* l = label(title, 3)) l->set_text_plain(time);
    if (ztwod::Text* h = help(abort_btn)) h->set_text_plain(e.message(0xdbf));
    if (ztwod::Text* l = label(title, 1)) l->set_message(0x176);
    if (ztwod::Text* h = help(continue_btn)) h->set_message(0x173);
    salvage.show_for(-1.0f);
    replay_btn.hide();
  } else if (kind == kCampaign) {
    if (ztwod::Text* l = label(title, 2)) l->set_text_id(0x177, campaign, mission);
    if (ztwod::Text* l = label(title, 3)) l->set_text_plain(time);
    const bool failed = e.mission_failed();
    if (ztwod::Text* l = label(title, 1)) l->set_message(failed ? 0x180 : 0x181);
    if (ztwod::Text* h = help(continue_btn)) h->set_message(clean && !failed ? 0x174 : 0x173);
    if (!failed) {
      salvage.show_for(-1.0f);
      salvage.store = e.mission_salvage_store();
      salvage.x218 = 0;
      salvage.refill();
      if (clean) replay_btn.show_for(-1.0f);
      else replay_btn.hide();
    } else {
      salvage.hide();
      replay_btn.hide();
    }
    if (*won)
      if (ztwod::Text* h = help(continue_btn)) h->set_message(0xfb8);
  }
  results(kind);
}

namespace {
// The results' layout (constants in the exe's data, never written).
constexpr int32_t kX = 0xe1;          // [0x005e5af0]  the left column
constexpr int32_t kIndent = 0xf;      // [0x005e5af8]
constexpr int32_t kRight = 0xbd;      // [0x005e5aec]  the numbers' column, from kX
constexpr int32_t kRow = 14;          // [0x005e5ae8]
constexpr int32_t kHeader = 16;       // [0x005e5ae4]
constexpr int32_t kTop = 0x46;        // [0x005e5af4]
constexpr int32_t kTotals = 0xf0;     // [0x005e5b04]
int32_t g_lance_count = 0;            // [0x00608bf8]
}  // namespace

int32_t& debrief_lance_count() { return g_lance_count; }

// Mech3.exe 1.2 @ 0x0047cab0: a line of text at (x, *y), kept for the
// destructor; *y moves on by step.
ztwod::Text* DebriefScreen::add_text(const char* s, int32_t x, int32_t* y, Handle font, int32_t step) {
  auto* t = new ztwod::Text;          // 0x005a15f0: the text, the font, then x and y as they are
  t->set_text_plain(s);
  t->set_font(font);
  t->x = x;
  t->y = *y;
  texts.push_back(t);
  *y += step;
  return t;
}

// Mech3.exe 1.2 @ 0x0047cce0
ztwod::Text* DebriefScreen::add_number(int32_t v, int32_t x, int32_t* y, Handle font, int32_t step) {
  char b[16];
  std::snprintf(b, sizeof b, "%d", v);
  return add_text(b, x, y, font, step);
}

// Mech3.exe 1.2 @ 0x0047cd70: 0 shows message 0x183.
ztwod::Text* DebriefScreen::add_points(int32_t v, int32_t x, int32_t* y, Handle font, int32_t step) {
  if (v) return add_number(v, x, y, font, step);
  const char* m = engine().message(0x183);
  return add_text(m ? m : "", x, y, font, step);
}

// Mech3.exe 1.2 @ 0x0047baf0..0x0047c84b (inlined in 0x0047b430): the lance
// on the cards, the objectives and bonuses with their points, the score and
// the rank (campaign.md section 3, The results).
void DebriefScreen::results(int32_t kind) {
  Engine& e = engine();
  struct Record {
    const char* name = nullptr;
    int32_t kills = 0, slot = -1;
  } recs[4];
  int32_t bonus[3] = {};              // the lance's points for mechs, vehicles, turrets, when clean
  int32_t count = 1;
  const uint32_t allies = e.player_allies();
  for (const Engine::LanceMech& m : e.debrief_lance()) {
    if (count >= 4) break;
    if ((!m.player && !(m.team & allies)) || m.type == -1) continue;
    const int32_t slot = m.player ? 0 : count;
    const int32_t kills = m.stats[0], vehicles = m.stats[1];  // +0x3f4 mechs, +0x3f8 vehicles destroyed
    if (clean)
      for (int i = 0; i < 3; ++i) bonus[i] += m.stats[3 + i];
    LanceCard& card = cards[slot];
    card.show_for(-1.0f);
    card.set_vehicle(m.mech);
    if (kind == kCampaign) {
      // Which lancemate's record: by the pilot's name (messages 0xe14,
      // 0xe15, 0xe01), the player's own 0.
      Record& r = recs[slot];
      r.name = m.name;
      r.kills = kills;
      r.slot = -1;
      if (slot == 0) {
        r.slot = 0;
      } else {
        static const int32_t kNames[] = {0xe14, 0xe15, 0xe01};
        for (int i = 0; i < 3; ++i) {
          const char* n = e.message(kNames[i]);
          if (n && m.name && std::strcmp(m.name, n) == 0) {
            r.slot = i + 1;
            break;
          }
        }
      }
    }
    ztwod::Text* l5 = label(card, 5);
    if (ztwod::Text* l = label(card, 0)) l->set_text_plain(m.name);
    if (ztwod::Text* l = label(card, 4)) l->hide();
    if (l5) l5->set_text("%d", kills + vehicles);
    if (ztwod::Text* l = label(card, 6)) l->set_text("%d", kills);
    if (ztwod::Text* l = label(card, 7)) l->set_text("%d", vehicles);
    if (l5) {
      // The card's line, from x 100 to 5 left of the total's text.
      const int32_t y = l5->get_y() + 6;
      if (l5->needs_render) l5->render();
      const int32_t x = l5->get_x() - 5 - l5->width;
      card.line.show_for(-1.0f);
      card.line.set(100, y, x, y);
      card.line.color = 0xffff;
    }
    if (!m.player) ++count;
  }
  g_lance_count = count;
  for (int32_t i = count; i < 4; ++i) {
    cards[i].set_vehicle(0);
    cards[i].hide();
  }

  ztwod::Host& h = ztwod::host();
  const Handle large = h.font_find("lab_large_white"), large_r = h.font_find("lab_large_whiteR");
  const Handle red = h.font_find("lab_small_red"), white = h.font_find("lab_small_white");
  const Handle green = h.font_find("lab_small_green"), red_r = h.font_find("lab_small_redR");
  const Handle white_r = h.font_find("lab_small_whiteR"), green_r = h.font_find("lab_small_greenR");
  int32_t y = kTop + kRow + kHeader;
  int32_t points = 0, primary = 0, secondary = 0;
  if (kind == kInstantAction) {
    // A thousand a wave, and a hundred for each round left at each wave.
    int32_t mode = 0, rounds = 0, waves = 0;
    e.action_rounds(&mode, &rounds, &waves);
    if (mode != 1 && mode != 2) {
      points = 1000;
    } else {
      if (!e.mission_ended() || e.mission_failed()) --rounds;
      points = waves * 1000;
      for (int32_t i = 0, step = rounds * 100; i < waves; ++i, step -= 100) points += step;
    }
    add_text(e.message(0x16d), kX, &y, white, kHeader);
  } else if (kind == kCampaign) {
    // The objectives shown, each 2500 when done (green) or 0 (red); a long
    // line moves the next down by its extra height.
    auto list = [&](int32_t which, int32_t* sum) {
      for (const Engine::Objective& o : e.debrief_objectives(which)) {
        if (!o.shown || !e.objective_text(o.obj)) continue;
        ztwod::Text* t = add_text(e.objective_text(o.obj), kX + kIndent, &y, o.done ? green : red, 0);
        const int32_t v = static_cast<int32_t>(o.done ? 2500.0f : 0.0f);
        *sum += v;
        add_points(v, kX + kRight, &y, o.done ? green_r : red_r, kRow);
        const int32_t th = t->text_height();
        if (th > kRow) y += th - kRow;
      }
    };
    if (!e.debrief_objectives(0).empty()) {
      add_text(e.message(0x16d), kX, &y, white, kHeader);
      list(0, &primary);
    }
    y += kHeader - kRow;
    add_text(e.message(0x16e), kX, &y, white, kHeader);
    list(1, &secondary);
  }
  secondary += bonus[0];
  if (bonus[0]) {
    add_text(e.message(0xfab), kX + kIndent, &y, green, 0);
    add_number(bonus[0], kX + kRight, &y, green_r, kRow);
  }
  int32_t top = 0;
  if (kind == kCampaign) {
    for (int i = 1; i < 3; ++i) {
      if (!bonus[i]) continue;
      add_text(e.message(i == 1 ? 0xfac : 0xfad), kX + kIndent, &y, green, 0);
      add_number(bonus[i], kX + kRight, &y, green_r, kRow);
    }
    // The mission's total; the score is the campaign's, with the total
    // added when the mission counts.
    points += primary + secondary + bonus[1] + bonus[2];
    score = e.campaign_score();
    if (!e.mission_failed() && clean) score += points;
    y = kTotals;
    add_text(e.message(0x171), kX, &y, large, 0);
    add_number(score, kX + kRight, &y, large_r, kHeader);
    lines[1].set(kX, y, kX + kRight, y);
    lines[1].color = zvid::pack_bytes(0xdc, 0xdc, 0xdc);
    add(&lines[1]);
    y += 5;
    add_text(e.message(0x170), kX, &y, white, 0);
    add_number(points, kX + kRight, &y, white_r, kHeader);
    y += 10;
    add_text(e.message(0x169), kX + kIndent, &y, white, kRow);
    // Each lancemate's record: its campaign total and this mission's kills.
    for (int32_t i = 0; i < g_lance_count; ++i) {
      const Record& r = recs[i];
      if (!r.name || r.slot == -1) continue;
      lance_scores[i] = e.campaign_record(r.slot) + r.kills;
      lance_scored[i] = true;
      add_text(r.name, kX + kIndent + 10, &y, white, 0);
      add_number(lance_scores[i], kX + kRight, &y, white_r, kRow);
    }
    top = score;
  } else {
    points += primary + secondary;
    top = points;
  }
  y = kTop;
  add_text(e.message(0x170), kX, &y, large, 0);
  add_number(points, kX + kRight, &y, large_r, kHeader);
  lines[0].set(kX, y, kX + kRight, y);
  lines[0].color = zvid::pack_bytes(0xdc, 0xdc, 0xdc);
  add(&lines[0]);
  for (ztwod::Text* t : texts) add(t);
  rank.set_rank(top);
}

// (ours) The results' texts in order, for the logs and the tests.
std::string debrief_rows(const DebriefScreen& d) {
  std::string out;
  for (const ztwod::Text* t : d.texts) {
    if (!out.empty()) out += " | ";
    out += t->text;
  }
  return out;
}

// Mech3.exe 1.2 @ 0x0047c920
DebriefScreen::~DebriefScreen() {
  for (ztwod::Text*& t : texts) {
    remove(t);  // 0x0059fab0
    delete t;
    t = nullptr;
  }
  texts.clear();
}

// Mech3.exe 1.2 @ 0x0042efe0
void DebriefScreen::update(float dt) { Window::update(dt); }

// Mech3.exe 1.2 @ 0x0047d030. CONTINUE: a campaign won goes to its movie;
// a mission passed (and clean) to the next briefing; failed or cheated, the
// lancemates go back and the same mission is briefed again. ABORT: to the
// main menu, the pilot saved (Instant Action: a fade to the IA Dummy
// State). REPLAY: the mission's voice again.
void DebriefScreen::on_button(int32_t id, int32_t a, int32_t b) {
  (void)a, (void)b;
  Engine& e = engine();
  const bool no_cheat = !e.mission_cheated();
  const int32_t kind = e.game_kind();
  if (id == kReplay) {
    e.debrief_voice_play(state);
    return;
  }
  if (id == kAbort) {
    if (kind == kInstantAction) {
      e.fade_to_state(kIaDummyState, 0, 1, 2, 2.0f, 0, 0, 0);  // [0x005e7274]: 2 s
      return;
    }
    if (kind != kCampaign) return;
    if (e.mission_failed() || !no_cheat) e.campaign_reset_lancemates();
    e.pilot_save_now();
    e.mission_cleanup(1);
    e.replace_state(2);
    return;
  }
  if (id != kContinue) return;
  if (kind == kMechLab) {
    e.mission_cleanup(0);
  } else if (kind == kCampaign) {
    if (*won) {
      e.replace_state_at(kWinMovieState);
      return;
    }
    if (!e.mission_failed() && no_cheat) {
      e.mission_cleanup(0);
    } else {
      e.campaign_reset_lancemates();
      int32_t op = 0, mission = 0;
      e.game_campaign(&op, &mission);
      e.mission_cleanup(mission == 1 || (op == 4 && mission == 4) ? 1 : 0);
      e.anim_reset();
      e.campaign_set_b0(0);
    }
  } else if (kind == kInstantAction) {
    e.action_next_round();
    e.mission_cleanup(0);
    e.anim_reset();
  } else {
    return;
  }
  e.replace_state(6);
}

}  // namespace mech3
