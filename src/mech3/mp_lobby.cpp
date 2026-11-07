#include "mech3/mp_lobby.h"
#include "mech3/mission.h"
#include "mech3/mods.h"
#include "mech3/content_network.h"
#include "mech3/content_registry.h"
#include "mech3/content_world.h"

#include <cctype>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/app_mech.h"
#include "mech3/pilot.h"

namespace mech3 {

int32_t mech_name_message(int32_t mech);  // mech3/mech_names.h (its Engine clashes with app_hooks.h's)
float max_mass(int32_t mech);              // the same

uint8_t g_lobby_dirty = 0;  // [0x006099cc]
uint8_t g_lobby_back = 0;   // [0x006099c8]

namespace {
constexpr uint32_t kModsMark = 0x53444f4d;  // "MODS": our block in a player's lobby data (the mods check)
LobbyScreen* g_lobby = nullptr;  // [0x006099d4]
int32_t g_order_next = 1;        // [0x005e7920]

ModsIdentity lobby_mods_identity() {
  ModsIdentity result=mods_identity();
  if(managed_gameplay_session()) {
    auto content=active_content();
    result.hash^=std::stoull(content->gameplay_digest.substr(0,16),nullptr,16);
    result.names.push_back("Core Critical content protocol 1");
  }
  return result;
}
bool managed_world(int32_t value) {return (value&0x40000000)!=0;}
int16_t legacy_world_field(int32_t value) {
  if(managed_world(value))return -1;
  if(value<0||value>32767)cc::content::fail("network-world","World is outside the legacy rules range");
  return static_cast<int16_t>(value);
}

void set_labels(ztwod::Control& c, const char* text) {  // 0x00495240
  for (std::vector<ztwod::Text*>* l : {&c.activate_labels, &c.disable_labels, &c.rollover_labels, &c.labels})
    if (!l->empty() && l->front()) l->front()->set_text_plain(text);
}
void set_enabled(ztwod::Control& c, int32_t on) {
  c.enabled = on;
  c.refresh();  // +0x80
}
std::string lower(std::string s) {  // CString::MakeLower
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
// A skin's name as the lists show it: " {gunmetal} " (0xdd7.., the format
// [0x005e7c3c] or the teams' "  {%s} " [0x005e7c50]).
std::string skin_tag(Engine& e, const char* format, int32_t skin) {
  char buf[96];
  std::snprintf(buf, sizeof buf, format, e.message(0xdd6 + skin));
  return lower(buf);
}

// A player's data as the lobby reads it (0x00499030, 0x0049ab30): more than
// a word or nothing. Kind 1 (the lobby's): its skin, its state, then the
// add-on worlds it has (their count, the values); the launch's kind 2
// (0x0046da80) replaces it.
std::vector<uint32_t> player_data(uint32_t id) {
  uint32_t size = znet::player_data_size(id);
  if (size <= 7) return {};
  std::vector<uint32_t> words((size + 3) / 4, 0);
  if (!znet::get_player_data(id, words.data(), &size)) return {};
  return words;
}

// 0x00499030: each network player's record, from its data (none: skin 1,
// state 2), its place in the count unless kept, its score line.
void add_player(uint32_t id, uint32_t team, bool host, const char* name, void*) {
  MultiplayerGame* g = g_lobby ? g_lobby->game : nullptr;
  if (!g) return;
  int32_t state = 2, skin = 1;
  const std::vector<uint32_t> d = player_data(id);
  if (!d.empty()) {
    if (d[0] == 1 && d.size() > 2) state = static_cast<int32_t>(d[2]);
    skin = static_cast<int32_t>(d[1]);
  }
  auto p = std::make_unique<LobbyPlayer>();
  p->id = id, p->team = team, p->host = host ? 1 : 0, p->name = name ? name : "";
  p->skin = skin, p->state = state;
  if (!g_lobby->keep_order) p->order = g_order_next++;
  auto line = std::make_unique<ScoreLine>();
  line->kind = 1;
  p->score = line.get();
  g->score_lines.push_back(std::move(line));
  g->lobby_players.push_back(std::move(p));
}

// 0x00499280: each network team's record, its data the owner and skin.
void add_team(uint32_t id, uint32_t, bool, const char* name, void*) {
  MultiplayerGame* g = g_lobby ? g_lobby->game : nullptr;
  if (!g) return;
  auto t = std::make_unique<LobbyTeam>();
  t->id = id, t->name = name ? name : "";
  uint32_t data[2] = {}, size = sizeof data;
  znet::get_team_data(id, data, &size);
  t->owner = data[0], t->skin = static_cast<int32_t>(data[1]);
  auto line = std::make_unique<ScoreLine>();
  line->kind = 2;
  t->score = line.get();
  g->score_lines.push_back(std::move(line));
  g->lobby_teams.push_back(std::move(t));
}
}  // namespace

// The block (enter's): untrusted, read only as far as the data goes, at most
// 16 names of 64 bytes, control characters '?'. A player without one (no
// mods block): hash 0.
bool LobbyScreen::mods_of(const std::vector<uint32_t>& d, uint64_t* hash, std::vector<std::string>* names) {
  *hash = 0;
  names->clear();
  if (d.size() < 4 || d[0] != 1) return false;
  const size_t worlds = d[3] < 64 ? d[3] : 64;
  size_t at = std::max<size_t>(5, 4 + worlds);
  if (at + 4 > d.size() || d[at] != kModsMark) return true;
  *hash = static_cast<uint64_t>(d[at + 1]) | static_cast<uint64_t>(d[at + 2]) << 32;
  const uint32_t count = std::min<uint32_t>(d[at + 3], 16);
  at += 4;
  for (uint32_t k = 0; k < count && at < d.size(); ++k) {
    const uint32_t len = std::min<uint32_t>(d[at++], 64);
    std::string n;
    for (uint32_t b = 0; b < len && at + b / 4 < d.size(); ++b) {
      const char c = static_cast<char>(d[at + b / 4] >> (8 * (b % 4)) & 0xff);
      n += static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? '?' : c;
    }
    at += (len + 3) / 4;
    names->push_back(n);
  }
  return true;
}

std::string LobbyScreen::mods_text(const std::vector<std::string>& names) {
  if (names.empty()) return "no gameplay mods";
  std::string t;
  for (const std::string& n : names) t += (t.empty() ? "" : ", ") + n;
  return t;
}

LobbyScreen* lobby_screen() { return g_lobby; }

// ---- the controls ----

// Mech3.exe 1.2 @ 0x00496880
void WorldCycle::step(int32_t by) {
  CycleButton::step(by);
  if (count() > 1 && index == 0) CycleButton::step(by);
}

void LobbyList::empty() {
  clear_items();
  records.clear();
  layout();  // +0xa4
}

void LobbyList::add(const std::string& text, LobbyEntry* e) {
  add_item(text.c_str(), static_cast<int32_t>(records.size()));
  records.push_back(e);
  layout();
}

LobbyEntry* LobbyList::chosen() {
  if (selected < 0 || selected >= item_count()) return nullptr;
  const int32_t v = items[static_cast<size_t>(selected)]->value;
  return v >= 0 && static_cast<size_t>(v) < records.size() ? records[static_cast<size_t>(v)] : nullptr;
}

// ---- the screen ----

// Mech3.exe 1.2 @ 0x00495f90
LobbyScreen::LobbyScreen(Engine& e, Handle s)
    : MenuScreen(e, s), chat_edit(e, this), cancel_btn(e, 1), start_btn(e, 0), mechlab_btn(e, 10), lock_btn(e, 0xb),
      worlds(e), kills_list(e, 0xd), time_list(e, 0xe), weight_list(e, 0xf), join_team_btn(e, 4), remove_team_btn(e, 5),
      kick_btn(e, 6), new_team_btn(e, 7), pick_skin_btn(e, 8), kick2_btn(e, 9), chat_log(e, this),
      scores_list(e, -1), teams_list(e, -1), players_list(e, 0x17), player_name(e, -1), session_name(e, -1),
      internet_line(e), lan_line(e) {
  chat_edit.set_size(0x27);
  int32_t id = 0x10;
  for (InvertCheck* c : {&regen_box, &mobile_box, &stock_box, &ammo_box, &select_box}) c->id = id++;
  ready_box.id = 0x15;
  const bool moving = !e.fast_menu();
  if (load_from(e.main_menu_definition(), "MULTIPLAYER_HOST_DIALOG", moving)) {
    zimg::Texture* img = nullptr;
    uint8_t bg_moving = 0;
    e.main_menu_background(&img, &bg_moving);
    background.set_image(img);
    background.moving = bg_moving;
    if (!bg_moving) {
      background.set_pos(0, 0);
      background.src = &background.view;
    }
    add(&background);
    bind_primitive(&lines1, "LINES1");
    if (!moving) {
      bake();
      remove(&background);
      remove(&lines1);
    }
    bind_button(&session_name, "SESSION_NAME");
    bind_button(&cancel_btn, "CANCEL_BUTTON");
    bind_button(&start_btn, "START_BUTTON");
    bind_button(&mechlab_btn, "MECHLAB_BUTTON");
    bind_button(&lock_btn, "LOCK_OPTIONS_BUTTON");
    bind_button(&worlds, "DATABASE_LIST");
    bind_button(&kills_list, "KILLS_LIST");
    bind_button(&time_list, "TIME_LIST");
    bind_button(&weight_list, "WEIGHT_LIST");
    bind_button(&regen_box, "REGEN_CHECKBOX");
    bind_button(&mobile_box, "MOBILE_CHECKBOX");
    bind_button(&stock_box, "STOCK_CHECKBOX");
    bind_button(&ammo_box, "AMMO_CHECKBOX");
    bind_button(&select_box, "SELECT_CHECKBOX");
    bind_button(&player_name, "PLAYER_NAME");
    bind_button(&chat_edit, "CHAT_ENTER_EDIT");
    bind_button(&chat_team, "CHAT_TEAM_CHECKBOX");
    bind_button(&join_team_btn, "JOIN_TEAM_BUTTON");
    bind_button(&remove_team_btn, "REMOVE_TEAM_BUTTON");
    bind_button(&kick_btn, "KICK_PLAYER_BUTTON");
    bind_button(&new_team_btn, "NEW_TEAM_BUTTON");
    bind_button(&pick_skin_btn, "PICK_SKIN_BUTTON");
    bind_button(&kick2_btn, "KICK_PLAYER2_BUTTON");
    bind_button(&scores_list, "SCORES_LIST");
    bind_button(&teams_list, "TP_LIST");
    bind_button(&players_list, "PLAYERS_LIST");
    bind_button(&ready_box, "READY_CHECKBOX");
    bind_primitive(&chat_log, "CHAT_REC_LIST");
    bind_button(&chat_log.up_btn, "CHAT_UP");
    bind_button(&chat_log.down_btn, "CHAT_DOWN");
    teams_list.id = 0x16;
  }
  // Ours (piece 9c): the host's two address lines, under CANCEL and LOCK.
  const Handle font = ztwod::host().font_find("ia_small_green");
  int32_t y = 460;
  for (CopyLine* l : {&internet_line, &lan_line}) {
    if (font) l->FontText::set_font(font);
    l->set_pos(20, y);
    y += 10;
    add(l);
    l->hide();
  }
  set_flags_all(0);
  set_active(1);
  g_lobby = this;
}

// Mech3.exe 1.2 @ 0x00496b90
LobbyScreen::~LobbyScreen() {
  if (handlers_on_) handlers(false);
  if (g_lobby == this) g_lobby = nullptr;
}

void LobbyScreen::take_pilot(Handle p, MultiplayerGame* g) {
  pilot = p;
  game = g;
}

// Mech3.exe 1.2 @ 0x00497720: the names; the scores; the local player's data
// made (kind 1: its skin, the first free one in a game without teams, and
// each add-on world it has, multiplayer-port.md 11); the rules' lists filled
// and set from the game; the lists and buttons cleared; the handlers; the
// game open again (its fourth word 1); the network's players and teams.
void LobbyScreen::enter() {
  Engine& e = engine();
  if (!game) return;
  const uint32_t me = znet::local_id();
  const char* my_name = znet::player_name(me);
  set_labels(player_name, my_name && *my_name ? my_name : "Unknown");  // [0x005e56f0]
  const std::string& game_name = znet::session().desc.name;
  set_labels(session_name, game_name.empty() ? "Some Game" : game_name.c_str());  // [0x005e7c30]
  scores(true);
  std::vector<uint32_t> data(5, 0);  // 0x0046dd90(1, 0x14)
  data[0] = 1;
  data[1] = static_cast<uint32_t>(game->net_skin(me));
  keep_order = 0;
  refresh_lists();
  const bool team = multiplayer_team_play();
  if (data[1] == 0 && !team)
    for (int32_t i = 0; i < 8; ++i)
      if (free_skin(i + 1, -1) == 0) {
        data[1] = static_cast<uint32_t>(i + 1);
        break;
      }
  worlds.clear_items();
  worlds.add_item(e.message(0xfdf), 0);  // "UNAVAILABLE"
  worlds.add_item(e.message(0x320), 0x100);  // the shipped eight: (number << 8) | folder
  static const int32_t kShipped[7][2] = {{0, 2}, {1, 1}, {1, 2}, {2, 1}, {2, 2}, {3, 1}, {3, 2}};
  for (int32_t i = 0; i < 7; ++i) worlds.add_item(e.message(0x321 + i), kShipped[i][1] << 8 | kShipped[i][0]);
  // The add-on worlds (readermp3 on): each folder's numbered readers, named
  // by their mpinfo.zrd's db_name, recorded in the data. Ours: on past the
  // exe's six to the mods' worlds (9 on), t1, lab and dev1/dev2 still left out.
  for (int32_t dir = 0; dir < mission_dir_count(); ++dir) {
    if (dir >= 6 && dir < 9) continue;
    const std::string folder = e.mission_dir(static_cast<uint32_t>(dir));
    for (const std::string& file : e.list_files(("zbd\\" + folder + "\\readermp*.zbd").c_str())) {
      if (file.size() < 8) continue;
      const int32_t n = std::atoi(file.substr(8, file.find('.') == std::string::npos ? std::string::npos : file.find('.') - 8).c_str());
      if (n <= 2) continue;
      char path[160];
      std::snprintf(path, sizeof path, "zbd\\%s\\readermp%d.zbd", folder.c_str(), n);
      if (!zrdr::open_archive(path, false)) continue;
      zrdr::Node* info = zrdr::load("mpinfo.zrd");
      if (const char* s = zrdr::get_string(info, "db_name")) {
        const int32_t v = n << 8 | dir;
        worlds.add_item(s, v);
        data[3] += 1;  // 0x00498160
        data.resize(4 + data[3]);
        data[3 + data[3]] = static_cast<uint32_t>(v);
      }
      if (info) zrdr::free_node(info);  // FIX (bug): the exe kept each tree (a leak per entry)
      zrdr::close_archives(false);
    }
  }
  if (data.size() < 5) data.resize(5);
  // Ours (the mods check): after the add-on worlds, "MODS", the gameplay
  // hash (low, high), the gameplay mods' names (a count, then each one's
  // length and its bytes, four a word). The exe's readers stop at the worlds.
  {
    const ModsIdentity mods = lobby_mods_identity();
    data.push_back(kModsMark);
    data.push_back(static_cast<uint32_t>(mods.hash));
    data.push_back(static_cast<uint32_t>(mods.hash >> 32));
    data.push_back(static_cast<uint32_t>(std::min<size_t>(mods.names.size(), 16)));
    for (size_t i = 0; i < mods.names.size() && i < 16; ++i) {
      const std::string cut = mods.names[i].substr(0, 64);
      data.push_back(static_cast<uint32_t>(cut.size()));
      for (size_t k = 0; k < cut.size(); k += 4) {
        uint32_t w = 0;
        for (size_t b = 0; b < 4 && k + b < cut.size(); ++b) w |= static_cast<uint32_t>(static_cast<uint8_t>(cut[k + b])) << (8 * b);
        data.push_back(w);
      }
    }
  }
  mods_checked = 0;
  append_content_session(data);
  znet::set_player_data(me, data.data(), static_cast<uint32_t>(data.size() * 4));
  // KILLS_LIST: 1, 5, 10, 20, 40 (doubled with teams) and unlimited (100).
  const int32_t unit = team ? 2 : 1;
  kills_list.clear_items();
  kills_list.add_item(unit < 2 ? e.message(0x2b1) : zutl::format_message_f(0x2b2, unit).c_str(), unit);
  for (int32_t m : {5, 10, 20, 40}) kills_list.add_item(zutl::format_message_f(0x2b2, unit * m).c_str(), unit * m);
  kills_list.add_item(e.message(0x2b5), 100);
  kills_list.select_value(5);
  time_list.clear_items();
  static const int32_t kMinutes[4] = {5, 10, 20, 30};
  for (int32_t i = 0; i < 4; ++i) time_list.add_item(e.message(0x2b6 + i), kMinutes[i]);
  time_list.add_item(e.message(0x2ba), 100);
  weight_list.clear_items();
  for (int32_t t = 30; t < 0x69; t += 5) weight_list.add_item(zutl::format_message_f(0x2bb, t).c_str(), t);
  weight_list.select_value(0x37);
  for(const auto& entry:content_multiplayer_entries())worlds.add_item(entry.first.c_str(),entry.second);
  if (worlds.select_value(game->world()) == -1) {
    worlds.select(0);  // both, as the exe: BEACHHEAD in the end
    worlds.select(1);
  }
  kills_list.select_value(game->x70);
  time_list.select_value(static_cast<int32_t>(game->x74));
  weight_list.select_value(game->x78);
  regen_box.set_checked(game->b8c);
  const int32_t w = worlds.value_at(worlds.index);
  const bool mobile_world = !((w & 0xff) == 4 && (w >> 8) > 2);
  mobile_box.set_checked(game->ba8 && mobile_world ? 1 : 0);
  stock_box.set_checked(game->b8f);
  ammo_box.set_checked(game->b8d);
  select_box.set_checked(game->b8e);
  chat_team.set_checked(0);
  teams_list.empty();
  players_list.empty();
  chat_log.clear_log();  // 0x00499680
  chat_edit.set_text("");
  chat_edit.set_focus(0);
  chat_edit.set_focus(1);
  locked = 0, all_ready = 0, start_pending = 0;
  host_controls();
  for (ztwod::Control* c : std::initializer_list<ztwod::Control*>{&join_team_btn, &remove_team_btn, &kick_btn,
                                                                  &new_team_btn, &pick_skin_btn, &kick2_btn,
                                                                  &mechlab_btn, &start_btn})
    set_enabled(*c, 0);
  ready_box.set_checked(0);
  set_enabled(ready_box, 0);
  if (team) chat_team.show_for(-1.0f);
  else chat_team.hide();
  g_lobby_dirty = 1;
  g_lobby_back = 0;
  handlers(true);
  znet::GameInfo info;
  if (znet::is_host() && znet::game_info(-1, &info)) {
    info.user[3] = 1;  // open (2 in progress, 3 locked)
    znet::set_game_info(info);  // 0x00586f20
  }
  znet::enum_players();
  if (team) {
    znet::enum_teams();
    znet::enum_team_players();
  }
}

// Mech3.exe 1.2 @ 0x004984e0. Ours: the host's addresses each half second.
void LobbyScreen::update(float dt) {
  if (g_lobby_back) {
    on_button(1, 0, 0);
    g_lobby_back = 0;
  }
  if (g_lobby_dirty) {
    send_rules();
    refresh_lists();
    refresh_buttons();
    g_lobby_dirty = 0;
  }
  // Ours (the mods check): a joiner whose gameplay mods differ from the
  // host's is turned back, once the host's data says what they are.
  if (!mods_checked && !znet::is_host() && znet::session().open) {
    uint64_t theirs = 0;
    std::vector<std::string> their_names;
    if (mods_of(player_data(znet::session().host_id), &theirs, &their_names)) {
      mods_checked = 1;
      const ModsIdentity mine = lobby_mods_identity();
      std::string content_reason;
      if (theirs != mine.hash || !content_sessions_match(player_data(znet::session().host_id),&content_reason)) {
        cursor_obj.erase();
        engine().message_box_plain(0x30, 0x2a8, "Your mods don't match the host's. The host uses: " + mods_text(their_names) +
                                                     ". You use: " + mods_text(mine.names) + "." +
                                                     (content_reason.empty() ? "" : " " + content_reason));
        g_lobby_back = 1;
      }
    }
  }
  if (start_pending) start();
  if ((reach_timer -= dt) <= 0.0f) {
    show_reach();
    reach_timer = 0.5f;
  }
  Window::update(dt);
}

// Mech3.exe 1.2 @ 0x00497410
void LobbyScreen::on_button(int32_t id, int32_t a, int32_t b) {
  (void)a, (void)b;
  Engine& e = engine();
  switch (id) {
    case 1:  // CANCEL: the handlers off, back, the network off, the records gone
      handlers(false);
      e.pop_state(1);
      e.network_set(0);
      g_lobby_dirty = 0;
      if (game) game->clear_lobby();
      return;
    case 0: start(); return;
    case 2: chat_send(0); return;  // 0x004999f0
    case 3: {                      // 0x00499a00
      const LobbyPlayer* me = game ? game->lobby_player(znet::local_id()) : nullptr;
      chat_send(me ? me->team : 0);
      return;
    }
    case 4: join_team(); return;
    case 7: open_new_team(); return;
    case 8: open_pick_skin(); return;
    case 5: remove_team(); return;
    case 10: mech_lab(); return;
    case 0x15: ready(); return;
    case 0xb: lock(); return;
    case 0xc: world_changed(); return;
    case 0xd:
    case 0xe:
    case 0xf:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:  // a rule changed: sent with the next refresh
      g_lobby_dirty = 1;
      return;
    case 6: kick(teams_list); return;
    case 9: kick(players_list); return;
    case 0x16: team_chosen(); return;
    case 0x17: player_chosen(); return;
    default: return;
  }
}

// Mech3.exe 1.2 @ 0x00498550. FIX (A): with `report` and a lobby's launch
// ([0x00608e24]) the scores went to the game service's COM object
// ([0x007307e0], 0x00589d00..0x00589e30); there is no such service.
void LobbyScreen::scores(bool report) {
  (void)report;
  scores_list.clear_items();
  scores_list.layout();
  shown_scores.clear();
  if (!game) return;
  if (game->lobby_players.empty()) {
    scores_list.add_item(engine().message(0x2bd), 0);  // "  No Scores to report"
    return;
  }
  game->score_texts(0x15, 7);
  for (const auto& l : game->score_lines) {
    scores_list.add_item(l->text.c_str(), 0);
    shown_scores.push_back(l->text);
  }
}

// Mech3.exe 1.2 @ 0x00498710: the records made again from the network's
// players (and teams); TP_LIST each team ("name  {skin} ") with its players
// under it, PLAYERS_LIST the players without a team: the host's "++", "**"
// for one missing the chosen add-on world (PLAYERS_LIST only), "In MechLab"
// or "Ready!" (any other: not all ready), the skin outside team play, our
// own mech's name. Our slot's skin is our side's; a changed player count
// asks the session and the players again.
void LobbyScreen::refresh_lists() {
  Engine& e = engine();
  if (!game) return;
  game->clear_lobby();
  all_ready = 1;
  g_order_next = 1;
  znet::each_player(add_player, nullptr);
  const bool team = multiplayer_team_play();
  if (team) {
    znet::each_team(add_team, nullptr);
    teams_list.empty();
    for (auto& t : game->lobby_teams) {
      t->count = 0;
      teams_list.add(t->name + skin_tag(e, "  {%s} ", t->skin), t.get());
      for (auto& p : game->lobby_players) {
        if (p->team != t->id) continue;
        std::string s = "   " + p->name;  // [0x005e7c4c]
        if (p->host) s += "++";
        s += " ";
        if (p->state == 1) s += e.message(0x2bf);  // "In MechLab"
        if (p->state == 3) s += e.message(0x2c0);  // "Ready!"
        else all_ready = 0;
        teams_list.add(s, p.get());
        ++t->count;
      }
    }
  }
  players_list.empty();
  const int32_t chosen_world = worlds.value_at(worlds.index);
  for (auto& p : game->lobby_players) {
    GameSlot* mine = p->id == znet::local_id() ? game->slot_at(0) : nullptr;
    if (p->team != 0) continue;
    std::string s = p->name;
    if (p->host) s += "++";
    if (!managed_world(chosen_world) && (chosen_world >> 8) > 2 && !has_world(p->id, chosen_world)) s += "**";
    s += " ";
    if (p->state == 1) s += e.message(0x2bf);
    if (p->state == 3) s += e.message(0x2c0);
    else all_ready = 0;
    if (!team && p->skin != 0) s += skin_tag(e, " {%s} ", p->skin);
    if (mine) {
      const int32_t m = mech_name_message(mine->mech);
      s += m < 0 ? "notmech" : e.message(m);  // 0x00484670
    }
    players_list.add(s, p.get());
  }
  game->slot_at(0)->skin = game->net_skin(znet::local_id()) - 1;  // 0x005a2b90
  znet::GameInfo info;
  if (znet::game_info(-1, &info) && game->lobby_players.size() != info.players) {
    znet::refresh_session();  // 0x00586e10
    znet::enum_players();
  }
}

// Mech3.exe 1.2 @ 0x004993f0: once the options are locked, READY for a
// player with a skin (a team in team play), MECHLAB until ready; START for
// the host when everyone is; ready, the lists' choices and their buttons
// off; NEW_TEAM for the host while there are under 8 teams.
void LobbyScreen::refresh_buttons() {
  const uint32_t me = znet::local_id();
  const bool team = multiplayer_team_play();
  const bool placed = locked && (team ? game->lobby_team_id(me) != 0 : game->lobby_skin(me) != 0);
  set_enabled(ready_box, placed ? 1 : 0);
  const bool in = team ? locked && game->lobby_team_id(me) != 0 : locked != 0;
  set_enabled(mechlab_btn, in && !ready_box.checked ? 1 : 0);
  set_enabled(start_btn, znet::is_host() && all_ready ? 1 : 0);
  if (ready_box.checked) {
    teams_list.select_index(-1);
    players_list.select_index(-1);
    for (ztwod::Control* c : std::initializer_list<ztwod::Control*>{&pick_skin_btn, &join_team_btn, &remove_team_btn,
                                                                    &kick_btn, &kick2_btn})
      set_enabled(*c, 0);
  }
  if (team && znet::is_host())
    set_enabled(new_team_btn, game->lobby_teams.size() < 8 && !ready_box.checked ? 1 : 0);
}

// Mech3.exe 1.2 @ 0x00498260: the rules the host's (until locked): the
// lists' arrows, the boxes, LOCK and (team play) NEW_TEAM. "UNAVAILABLE"
// chosen becomes BEACHHEAD; a world without MOBILE (an add-on of folder 4)
// clears and disables it.
void LobbyScreen::host_controls() {
  const bool on = znet::is_host() && !locked;
  for (CycleButton* c : {static_cast<CycleButton*>(&worlds), &kills_list, &time_list, &weight_list})
    c->set_arrows(on, on);
  for (ztwod::Control* c : std::initializer_list<ztwod::Control*>{&regen_box, &mobile_box, &stock_box, &ammo_box,
                                                                  &select_box, &lock_btn})
    set_enabled(*c, on ? 1 : 0);
  set_enabled(new_team_btn, on && multiplayer_team_play() ? 1 : 0);
  if (!on) return;
  if (worlds.item(worlds.index) && worlds.item(worlds.index)->value == 0) {
    worlds.select(1);
    g_lobby_dirty = 1;
  }
  const int32_t w = worlds.item(worlds.index) ? worlds.item(worlds.index)->value : -1;
  if ((w & 0xff) == 4 && (w >> 8) > 2 && mobile_box.checked) {
    mobile_box.set_checked(0);
    set_enabled(mobile_box, 0);
    g_lobby_dirty = 1;
  }
}

// Mech3.exe 1.2 @ 0x00497fbf..0x004980b6 (on), 0x00499a30 (off).
void LobbyScreen::handlers(bool on) {
  static const uint16_t kChange[] = {3, 4, 9, 0xa, 5, 6, 8, 2};
  for (uint16_t t : kChange) on ? (void)znet::on(t, on_change) : (void)znet::off(t, on_change);
  if (on) {
    znet::on(7, on_kicked);
    znet::on(0xd, on_host);
    znet::on(0x20, on_rules);
    znet::on(0x1e, on_start);
    znet::on(0x14, on_scores);
    znet::on(0x1f, on_chat);
  } else {
    znet::off(7, on_kicked);
    znet::off(0xd, on_host);
    znet::off(0x20, on_rules);
    znet::off(0x1e, on_start);
    znet::off(0x14, on_scores);
    znet::off(0x1f, on_chat);
  }
  handlers_on_ = on;
}

// Mech3.exe 1.2 @ 0x00499600
void LobbyScreen::on_change(uint32_t, const uint8_t*, size_t) { g_lobby_dirty = 1; }

// Mech3.exe 1.2 @ 0x0049a1d0: "You have been kicked out of this game.", then
// back to the connection screen (the update's CANCEL).
void LobbyScreen::on_kicked(uint32_t, const uint8_t*, size_t) {
  if (!g_lobby) return;
  g_lobby->cursor_obj.erase();  // [+0x58] vt+8
  g_lobby->engine().message_box(0x30, 0x2a8, 0x2c3);
  g_lobby_back = 1;
}

// Mech3.exe 1.2 @ 0x0049a200: a new host takes the rules.
void LobbyScreen::on_host(uint32_t, const uint8_t*, size_t) {
  if (g_lobby && znet::is_host()) g_lobby->host_controls();
  g_lobby_dirty = 1;
}

// Mech3.exe 1.2 @ 0x0049a150, 0x0049a7a0: the chosen player out of the game;
// not ourselves (0x2c1).
void LobbyScreen::kick(LobbyList& from) {
  LobbyEntry* en = from.chosen();
  if (!en || en->kind() != 1) return;
  if (en->id == znet::local_id()) {
    cursor_obj.erase();
    engine().message_box(0x30, 0x2a8, 0x2c1);  // "You can't kick yourself out of the game!"
    return;
  }
  znet::kick(en->id);
}

// Mech3.exe 1.2 @ 0x0049a3a0: a team chosen gives PICK_SKIN to the host while
// there are under 8 teams, JOIN when we are not in it, REMOVE to the host
// for an empty one; a player chosen KICK to the host (not ourselves). Ready,
// nothing is chosen.
void LobbyScreen::team_chosen() {
  LobbyEntry* en = teams_list.chosen();
  if (!en) return;
  if (ready_box.checked) {
    teams_list.select_index(-1);
    return;
  }
  for (ztwod::Control* c : std::initializer_list<ztwod::Control*>{&join_team_btn, &remove_team_btn, &kick_btn,
                                                                  &pick_skin_btn})
    set_enabled(*c, 0);
  const uint32_t me = znet::local_id();
  if (en->kind() == 2) {
    if (znet::is_host()) set_enabled(pick_skin_btn, game->lobby_teams.size() < 8 ? 1 : 0);
    if (!game->in_team(me, en->id)) set_enabled(join_team_btn, 1);
    if (znet::is_host() && static_cast<LobbyTeam*>(en)->count == 0) set_enabled(remove_team_btn, 1);
  } else if (en->kind() == 1) {
    if (znet::is_host() && en->id != me) set_enabled(kick_btn, 1);
  }
}

// Mech3.exe 1.2 @ 0x0049a6a0: KICK2 to the host for another player, PICK_SKIN
// for ourselves outside team play.
void LobbyScreen::player_chosen() {
  LobbyEntry* en = players_list.chosen();
  if (!en) return;
  if (ready_box.checked) {
    players_list.select_index(-1);
    return;
  }
  set_enabled(kick2_btn, 0);
  set_enabled(pick_skin_btn, 0);
  if (en->kind() != 1) return;
  const uint32_t me = znet::local_id();
  if (znet::is_host() && en->id != me) set_enabled(kick2_btn, 1);
  if (!multiplayer_team_play() && en->id == me) set_enabled(pick_skin_btn, 1);
}

// Mech3.exe 1.2 @ 0x0049bd10: in team play, the team with that skin (not
// `except`), 0 none; outside team play always 0.
int32_t LobbyScreen::free_skin(int32_t skin, int32_t except) {
  if (!multiplayer_team_play() || !game || skin == except) return 0;
  for (auto& t : game->lobby_teams)
    if (t->skin == skin) return static_cast<int32_t>(t->id);
  return 0;
}

// Mech3.exe 1.2 @ 0x0049ab30
bool LobbyScreen::has_world(uint32_t player, int32_t world) {
  const std::vector<uint32_t> d = player_data(player);
  if (d.size() < 4 || d[0] != 1) return false;
  for (uint32_t k = 0; k < d[3] && 4 + k < d.size(); ++k)
    if (static_cast<int32_t>(d[4 + k]) == world) return true;
  return false;
}

// ---- the rules (piece 11b) ----

namespace {
uint32_t box_flags(const LobbyScreen& l) {
  return (l.regen_box.checked ? 1u : 0u) | (l.mobile_box.checked ? 2u : 0u) | (l.stock_box.checked ? 4u : 0u) |
         (l.ammo_box.checked ? 8u : 0u) | (l.select_box.checked ? 0x10u : 0u) | (l.locked ? 0x20u : 0u);
}
bool no_mobile(int32_t world) { return !managed_world(world) && (world & 0xff) == 4 && (world >> 8) > 2; }
}  // namespace

// Mech3.exe 1.2 @ 0x00499b10: the host's, to every player, guaranteed (the
// screen's vt+0x10 before it is empty here).
void LobbyScreen::send_rules() {
  if (!znet::is_host()) return;
  const bool managed=managed_gameplay_session();
  uint8_t m[0x14] = {0x20, 0, static_cast<uint8_t>(managed?0x14:0x10), 0};
  const uint32_t flags = box_flags(*this)|(managed?0x80000000u:0);
  const int16_t v[4] = {legacy_world_field(worlds.selected_value()), static_cast<int16_t>(kills_list.selected_value()),
                        static_cast<int16_t>(time_list.selected_value()), static_cast<int16_t>(weight_list.selected_value())};
  std::memcpy(m + 4, &flags, 4);
  std::memcpy(m + 8, v, 8);
  if(managed) {int32_t world=worlds.selected_value();std::memcpy(m+16,&world,4);}
  znet::send(m, managed?20:16, true, 0);
}

// The game's rules block from the lists and the boxes (0x0049aca0's host
// branch, 0x00499cf0's), a random word, then its CRC-32 (0x00576c10, from
// 0). The lock seeds the generator with the time first.
void LobbyScreen::keep_rules(bool seeded) {
  if (!game) return;
  MultiplayerGame::Rules& r = game->rules;
  r = MultiplayerGame::Rules{};
  game->rules_crc = 0;
  r.world = legacy_world_field(worlds.selected_value());
  r.kills = static_cast<int16_t>(kills_list.selected_value());
  r.time = static_cast<int16_t>(time_list.selected_value());
  r.weight = static_cast<int16_t>(weight_list.selected_value());
  r.flags = box_flags(*this);
  if (seeded) std::srand(static_cast<unsigned>(std::time(nullptr)));
  r.seed = std::rand();
  uint8_t b[0x10];
  std::memcpy(b, &r.flags, 4);
  std::memcpy(b + 4, &r.world, 2), std::memcpy(b + 6, &r.kills, 2), std::memcpy(b + 8, &r.time, 2);
  std::memcpy(b + 10, &r.weight, 2), std::memcpy(b + 12, &r.seed, 4);
  game->rules_crc = zutl_zar::crc32(0, b, sizeof b);
  if(managed_gameplay_session()) {int32_t world=worlds.selected_value();game->rules_crc=zutl_zar::crc32(game->rules_crc,&world,4);}
}

// Mech3.exe 1.2 @ 0x00499cf0: the host's rules shown: a world not in our
// list is UNAVAILABLE; a value the lists lack leaves theirs; the boxes set
// and not ours to change; the block kept (with the lock as it was); then
// the lock as sent, and on its first lock the players again and our mech
// fitted to the rules (bits past the six are never read). FIX (A): only
// from the host, and whole (the exe took any sender's, and read what the
// message lacked).
void LobbyScreen::on_rules(uint32_t from, const uint8_t* msg, size_t size) {
  LobbyScreen* l = g_lobby;
  if (!l || !l->game || size < 0x10 || from != znet::session().host_id || znet::is_host()) return;
  uint32_t flags;
  uint16_t v[4];
  std::memcpy(&flags, msg + 4, 4);
  std::memcpy(v, msg + 8, 8);
  int32_t selected=v[0];
  if(flags&0x80000000u) {
    if(size!=20||!managed_gameplay_session()||!content_sessions_match(player_data(from)))return;
    std::memcpy(&selected,msg+16,4);
  } else if(managed_gameplay_session())return;
  if (l->worlds.select_value(selected) == -1) l->worlds.select(0);
  l->kills_list.select_value(v[1]);
  l->time_list.select_value(v[2]);
  l->weight_list.select_value(v[3]);
  int32_t bit = 1;
  for (InvertCheck* c : {&l->regen_box, &l->mobile_box, &l->stock_box, &l->ammo_box, &l->select_box}) {
    c->set_checked((flags & bit) ? 1 : 0);
    bit <<= 1;
  }
  for (InvertCheck* c : {&l->regen_box, &l->mobile_box, &l->stock_box, &l->ammo_box, &l->select_box}) set_enabled(*c, 0);
  l->keep_rules(false);
  const uint8_t was = l->locked;
  l->locked = (flags >> 5) & 1;
  g_lobby_dirty = 1;
  if (!was && l->locked) {
    znet::enum_players();
    if (multiplayer_team_play()) {
      znet::enum_teams();
      znet::enum_team_players();
    }
    l->lock_mech();
  }
}

// Mech3.exe 1.2 @ 0x00497410 case 0xc: a world without MOBILE (an add-on of
// folder 4) clears and disables it.
void LobbyScreen::world_changed() {
  const bool ok = !no_mobile(worlds.selected_value());
  if (!ok) mobile_box.set_checked(0);
  set_enabled(mobile_box, ok ? 1 : 0);
  g_lobby_dirty = 1;
}

// Mech3.exe 1.2 @ 0x0049aca0: not while a player lacks the chosen add-on
// world (0xfe0 with their names); asked first when there is neither a score
// nor a time limit (0xfbd), then whether to lock (0x2c4). Locked: the
// players again, the game shown as locked to the search (its fourth word
// 3), the host's controls off, the rules sent, our mech fitted, the block
// kept.
void LobbyScreen::lock() {
  Engine& e = engine();
  cursor_obj.erase();
  // Ours (the mods check): every player's gameplay mods the host's, as the
  // add-on world below, with their names.
  if (game) {
    const ModsIdentity mine = lobby_mods_identity();
    std::string lines;
    for (auto& p : game->lobby_players) {
      if (p->id == znet::local_id()) continue;
      uint64_t theirs = 0;
      std::vector<std::string> names;
      mods_of(player_data(p->id), &theirs, &names);
      if (theirs != mine.hash) lines += p->name + " uses: " + mods_text(names) + ". ";
      std::string content_reason;
      if (!content_sessions_match(player_data(p->id),&content_reason)) lines+=p->name+": "+content_reason+". ";
    }
    if (!lines.empty()) {
      e.message_box_plain(0, 0x1e3, "Every player needs the same mods. " + lines + "You use: " + mods_text(mine.names) + ".");
      return;
    }
  }
  const int32_t w = worlds.selected_value();
  if (!managed_world(w) && (w >> 8) > 2 && game) {
    std::string names;
    int32_t n = 0;
    for (auto& p : game->lobby_players)
      if (p->id != znet::local_id() && !has_world(p->id, w)) names += p->name + " ", ++n;
    if (n > 0) {
      e.message_box3(0, 0x1e3, 0xfe0, names.c_str(), nullptr, nullptr);
      return;
    }
  }
  if (kills_list.selected_value() == 100 && time_list.selected_value() == 100 && e.message_box(1, 0x1e3, 0xfbd) != 1)
    return;
  if (e.message_box(1, 0x1e3, 0x2c4) != 1) return;
  znet::enum_players();
  if (multiplayer_team_play()) {
    znet::enum_teams();
    znet::enum_team_players();
  }
  znet::GameInfo info;
  if (znet::is_host() && znet::game_info(-1, &info)) {
    info.user[3] = 3;
    znet::set_game_info(info);
  }
  locked = 1;
  host_controls();
  send_rules();
  g_lobby_dirty = 1;
  lock_mech();
  if (!game) return;
  game->rules = MultiplayerGame::Rules{};
  game->rules_crc = 0;
  if (znet::is_host()) keep_rules(true);
}

// Mech3.exe 1.2 @ 0x0049b360 (0x0049b1c0, 0x0049b2d0): the store's mechs
// out, then each within WEIGHT_LIST's tons in (marked); a slot mech over
// the limit (or none) becomes the heaviest within it with its first stock
// loadout; one within keeps its loadout, given the first stock one when it
// has none, or with STOCK on when it is not one of the mech's stock
// loadouts. The slot's loadout is its own copy, never one of the list's,
// so with STOCK on the exe always made it the first (multiplayer-port.md 11b).
void LobbyScreen::lock_mech() {
  if (!game) return;
  Engine& e = engine();
  const float tons = static_cast<float>(weight_list.selected_value());
  const bool stock = stock_box.checked != 0;
  Store& st = game->store;
  for (auto it = st.items.begin(); it != st.items.end();) {
    if ((*it)->kind() == 0) {
      delete *it;
      it = st.items.erase(it);
    } else {
      ++it;
    }
  }
  int32_t best = -1;
  float best_mass = 0;
  for (int32_t m = 0; m < chassis_count(); ++m) {
    if (m == 7) continue;  // 0x00426080, and the check again
    const float mass = max_mass(m);
    if (tons < mass) continue;
    if (best == -1 || best_mass < mass) best = m, best_mass = mass;
    if (BaseItem* b = st.add_mech(m)) b->b10 = 1;
  }
  GameSlot& slot = game->slot;
  const auto first = [&](int32_t mech) -> const Config* {
    const ConfigList* l = mech >= 0 ? e.mech_loadouts(mech) : nullptr;
    return l && !l->configs.empty() ? l->configs[0] : nullptr;
  };
  if (slot.mech == -1 || max_mass(slot.mech) > tons) {
    slot.mech = best;
    slot.set_config(first(best));
    return;
  }
  if (!slot.config) {
    slot.set_config(first(slot.mech));
    return;
  }
  if (!stock) return;
  const ConfigList* l = e.mech_loadouts(slot.mech);
  int32_t at = -1;  // 0x00425b70: by pointer, which the slot's copy never is
  // FIX (bug): a chosen stock loadout kept, found by its name (fixes.md).
  const bool by_name = zfix::options().bugs && slot.config->name;
  if (l)
    for (size_t i = 0; i < l->configs.size(); ++i)
      if (by_name ? l->configs[i]->name && std::strcmp(l->configs[i]->name, slot.config->name) == 0
                  : l->configs[i] == slot.config) {
        at = static_cast<int32_t>(i);
        if (by_name) break;
      }
  if (l && at >= 0 && at <= l->count - 1) return;
  slot.set_config(first(slot.mech));
}

// ---- the start, and each player's own (piece 11c) ----

namespace {
// Our own data, the lobby's kind 1 (0x0049a970, 0x0049b500, 0x0049b660 read
// it so; anything else is left alone).
bool my_data(std::vector<uint32_t>* d) {
  *d = player_data(znet::local_id());
  return d->size() >= 3 && (*d)[0] == 1;
}
void set_my_data(const std::vector<uint32_t>& d) {
  znet::set_player_data(znet::local_id(), d.data(), static_cast<uint32_t>(d.size() * 4));
}

// The network game's lobby calls on the started game (0x00435910..).
MultiplayerGame* g_net_game = nullptr;
NetLobbyAccess net_lobby_access() {
  NetLobbyAccess a;
  a.team_of = [](uint32_t p) { return g_net_game ? g_net_game->net_team(p) : 0u; };
  a.start_of = [](uint32_t p) {
    const LobbyPlayer* r = g_net_game ? g_net_game->lobby_player(p) : nullptr;
    return r ? r->order : 0;
  };
  a.left = [](uint32_t p) {
    if (LobbyPlayer* r = g_net_game ? g_net_game->lobby_player(p) : nullptr) r->left = 1;
  };
  a.suicide = [](uint32_t p) {
    if (g_net_game) g_net_game->add_deaths(p, 1);
  };
  a.kill = [](uint32_t p) {
    if (g_net_game) g_net_game->add_kills(p, 1);
  };
  a.kills = [](uint32_t p) { return g_net_game ? g_net_game->kills_of(p) : -1; };
  a.score = [](uint32_t p) { return g_net_game ? g_net_game->score_of(p) : -1; };
  a.deaths = [](uint32_t p) { return g_net_game ? g_net_game->deaths_of(p) : -1; };
  a.scores = [](uint32_t* out, int32_t cap) {
    int32_t n = 0;
    if (g_net_game)
      for (auto& r : g_net_game->lobby_players) {
        if (n >= cap) break;
        out[3 * n] = r->id, out[3 * n + 1] = static_cast<uint32_t>(r->kills), out[3 * n + 2] = static_cast<uint32_t>(r->deaths);
        ++n;
      }
    return n;
  };
  a.set_kills = [](uint32_t p, uint32_t v) {
    if (g_net_game) g_net_game->set_kills(p, static_cast<int32_t>(v));
  };
  a.set_deaths = [](uint32_t p, uint32_t v) {
    if (g_net_game) g_net_game->set_deaths(p, static_cast<int32_t>(v));
  };
  a.rescore = [] {
    if (g_net_game) g_net_game->rescore();
  };
  a.team_id = [](uint32_t p) { return g_net_game ? g_net_game->lobby_team_id(p) : 0u; };
  return a;
}
}  // namespace

// Mech3.exe 1.2 @ 0x0049b790: the host tells everyone their places (0x1e:
// each place's player, 1..8) and shows the game in progress (its fourth word
// 2); everyone then gives the game the lobby's settings (a limit of 100 is
// none: the kills 0x7fffffff, the clock off), the rules block, the world
// (UNAVAILABLE folder -1) and the players' lines (the briefing's), drops the
// lobby's handlers, and goes to the Load state (0x0049ca80). Ours: the
// network game's lobby calls on this game (the debug start's stand-ins
// otherwise).
void LobbyScreen::start() {
  Engine& e = engine();
  start_pending = 0;
  if (!game) return;
  if (znet::is_host()) {
    uint8_t m[0x24] = {0x1e, 0, 0x24, 0};
    for (auto& p : game->lobby_players)
      if (p->order > 0 && p->order < 9) std::memcpy(m + 4 * p->order, &p->id, 4);
    znet::send(m, sizeof m, true, 0);
    znet::GameInfo info;
    if (znet::game_info(-1, &info)) {
      info.user[3] = 2;  // in progress
      znet::set_game_info(info);
    }
    keep_order = 1;
  }
  const int32_t kills = kills_list.selected_value();
  game->x70 = kills < 100 ? kills : 0x7fffffff;
  game->b8c = regen_box.checked != 0;
  game->b8d = ammo_box.checked != 0;
  game->b8e = select_box.checked != 0;
  game->b8f = stock_box.checked != 0;
  game->ba8 = mobile_box.checked != 0;
  game->x78 = weight_list.selected_value();
  const int32_t minutes = time_list.selected_value();
  game->x74 = static_cast<float>(minutes);
  MultiplayerGame::Rules& r = game->rules;
  r.world = legacy_world_field(worlds.selected_value());
  r.kills = static_cast<int16_t>(kills);
  r.time = static_cast<int16_t>(minutes);
  r.weight = static_cast<int16_t>(game->x78);
  r.flags |= box_flags(*this) & 0x1f;  // or'd onto the lock's
  handlers(false);
  const float t = static_cast<float>(minutes);
  program_net_clock(t >= 100.0f ? 0.0f : t * 60.0f, t < 100.0f);  // [0x00609920], [0x00609924]
  const int32_t w = worlds.selected_value();
  game->set_world(w);
  LobbyList& lines = multiplayer_team_play() ? teams_list : players_list;
  std::vector<std::string> roster;
  for (int32_t i = 0; i < lines.item_count(); ++i) roster.emplace_back(lines.items[static_cast<size_t>(i)]->text);
  game->set_roster(roster);
  g_net_game = game;
  program_net_lobby(net_lobby_access());
  e.pop_state(0);
  e.replace_state(6);  // 0x0049ca80: the Load state
}

// Mech3.exe 1.2 @ 0x0049bcb0: the host's places for its players; then our
// start. FIX (A): only the host's, whole (the exe took any player's).
void LobbyScreen::on_start(uint32_t from, const uint8_t* msg, size_t size) {
  LobbyScreen* l = g_lobby;
  if (!l || !l->game || size < 0x24 || from != znet::session().host_id || znet::is_host()) return;
  for (int32_t i = 0; i < 8; ++i) {
    uint32_t id;
    std::memcpy(&id, msg + 4 + 4 * i, 4);
    if (id)
      if (LobbyPlayer* p = l->game->lobby_player(id)) p->order = i + 1;
  }
  l->keep_order = 1;
  l->start_pending = 1;
}

// Mech3.exe 1.2 @ 0x0049b150: the host's scores between games, each player's
// kills and suicides; the lines again. FIX (A): read only as far as the
// message goes.
void LobbyScreen::on_scores(uint32_t, const uint8_t* msg, size_t size) {
  LobbyScreen* l = g_lobby;
  if (!l || !l->game || size < 6) return;
  int16_t n;
  std::memcpy(&n, msg + 4, 2);
  for (int32_t k = 0; k < n && 6 + 12 * static_cast<size_t>(k + 1) <= size; ++k) {
    uint32_t w[3];
    std::memcpy(w, msg + 6 + 12 * k, 12);
    l->game->set_kills(w[0], static_cast<int32_t>(w[1]));
    l->game->set_deaths(w[0], static_cast<int32_t>(w[2]));
  }
  l->game->rescore();
  l->scores(false);
}

// Mech3.exe 1.2 @ 0x0049a970: READY (after its press) into our data's state,
// 3 ready or 2 not. Its check of another team with our skin (0x0049bd10
// with the skin as the exception, which never answers one) is dead.
void LobbyScreen::ready() {
  if (!game) return;
  const LobbyPlayer* me = game->lobby_player(znet::local_id());
  if (ready_box.checked && me && free_skin(me->skin, me->skin) != 0) {
    ready_box.set_checked(0);  // (with 0xf47 / 0xf48 in team play)
    return;
  }
  std::vector<uint32_t> d;
  if (!my_data(&d)) return;
  d[2] = ready_box.checked ? 3 : 2;
  set_my_data(d);
}

// Mech3.exe 1.2 @ 0x0049b500: our state kept, then "In MechLab" (1); the
// lab on the game's store, stock mechs only with STOCK.
void LobbyScreen::mech_lab() {
  if (!game) return;
  std::vector<uint32_t> d;
  if (!my_data(&d)) return;
  saved_state = static_cast<int32_t>(d[2]);
  d[2] = 1;
  set_my_data(d);
  Engine& e = engine();
  e.mech_lab_open(e.store_handle(&game->store), stock_box.checked ? 1 : 0);  // 0x00479430, 0x00448830
}

// Mech3.exe 1.2 @ 0x0049b660: back from the lab, our state as it was; the
// chat's bar given the keyboard again.
void LobbyScreen::resume() {
  std::vector<uint32_t> d;
  if (my_data(&d)) {
    d[2] = static_cast<uint32_t>(saved_state);
    set_my_data(d);
  }
  chat_edit.set_focus(0);
  chat_edit.set_focus(1);
}

// Mech3.exe 1.2 @ 0x0049a220: a player into a team (the network's, then the
// record); a team's first player its owner (its data's first word).
bool LobbyScreen::add_to_team(uint32_t team, uint32_t player) {
  if (!game) return false;
  LobbyTeam* t = game->lobby_team(team);
  LobbyPlayer* p = t ? game->lobby_player(player) : nullptr;
  if (!p || !znet::add_to_team(team, player)) return false;
  p->team = team;
  if (t->owner == 0) {
    t->owner = player;
    uint32_t data[2] = {}, size = sizeof data;
    znet::get_team_data(team, data, &size);
    data[0] = player;
    znet::set_team_data(team, data, sizeof data);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x0049a300: a team made with its skin (its data {0,
// skin}) and its record; with `join` we leave ours for it.
void LobbyScreen::new_team(const char* name, int32_t skin, bool join) {
  if (!game) return;
  const uint32_t id = znet::create_team(name);
  if (!id) return;
  const uint32_t data[2] = {0, static_cast<uint32_t>(skin)};
  znet::set_team_data(id, data, sizeof data);
  add_team(id, 0, false, name, nullptr);  // 0x00499280
  if (!join) return;
  const uint32_t me = znet::local_id();
  LobbyPlayer* p = game->lobby_player(me);
  if (p && p->team && znet::remove_from_team(p->team, me)) p->team = 0;
  add_to_team(id, me);
}

// Mech3.exe 1.2 @ 0x0049a850: our skin, outside team play in our data (its
// second word), in team play our team's (its data's second word).
void LobbyScreen::set_skin(int32_t skin) {
  if (!game) return;
  const uint32_t me = znet::local_id();
  if (!multiplayer_team_play()) {
    std::vector<uint32_t> d = player_data(me);
    if (d.size() < 2) return;
    d[1] = static_cast<uint32_t>(skin);
    set_my_data(d);
    return;
  }
  const uint32_t team = game->lobby_team_id(me);
  uint32_t data[2] = {}, size = sizeof data;
  znet::get_team_data(team, data, &size);
  data[1] = static_cast<uint32_t>(skin);
  znet::set_team_data(team, data, sizeof data);
}

// Mech3.exe 1.2 @ 0x0049a550: into the chosen team (out of ours first).
void LobbyScreen::join_team() {
  if (!game) return;
  const uint32_t me = znet::local_id();
  LobbyPlayer* p = game->lobby_player(me);
  if (!p) return;
  LobbyEntry* en = teams_list.chosen();
  if (!en || en->kind() != 2 || en->id == 0) return;
  if (p->team && znet::remove_from_team(p->team, me)) p->team = 0;
  add_to_team(en->id, me);
  set_enabled(remove_team_btn, 0);
  set_enabled(join_team_btn, 0);
}

// Mech3.exe 1.2 @ 0x0049a630: the chosen team, when empty, gone; the list's
// first chosen.
void LobbyScreen::remove_team() {
  LobbyEntry* en = teams_list.chosen();
  if (!en || en->kind() != 2 || static_cast<LobbyTeam*>(en)->count != 0) return;
  znet::destroy_team(en->id);
  teams_list.select_index(0);
}

// ---- the chat (piece 12) ----

ChatLog::ChatLog(Engine& e, LobbyScreen* s)
    : ztwod::TextBox(0xe), up_btn(e, this, true), down_btn(e, this, false), owner(s) {
  top = bottom = history.end();
}

// Mech3.exe 1.2 @ 0x004997a0
bool ChatLog::at_top() const { return top == history.begin(); }

// Mech3.exe 1.2 @ 0x00499760
bool ChatLog::at_bottom() const {
  return static_cast<int32_t>(history.size()) <= visible || (!history.empty() && bottom == std::prev(history.end()));
}

// Mech3.exe 1.2 @ 0x00499680: the lines gone but three spaces, shown, the
// window on it.
void ChatLog::clear_log() {
  history.clear();
  history.push_back("   ");  // 0x00495df0 ([0x005e7c4c])
  set_text("%s", "   ");
  top = bottom = history.begin();
}

// Mech3.exe 1.2 @ 0x004996d0: a full log at its top first scrolled a line
// (CHAT_DOWN's press); the line kept (the oldest past 100 dropped,
// 0x00495e60); shown when we were at the bottom, the window with it.
void ChatLog::add(const char* line) {
  if (at_top() && static_cast<int32_t>(history.size()) >= 100) down_btn.on_left_up();
  const bool was = at_bottom();
  history.push_back(line ? line : "");
  if (static_cast<int32_t>(history.size()) > max_lines) history.pop_front();
  if (!was) return;
  set_text("%s", line ? line : "");
  ++bottom;
  if (visible < static_cast<int32_t>(history.size())) ++top;
}

// Mech3.exe 1.2 @ 0x004997c0: up a line, the one above written at the top.
void ChatLog::scroll_up() {
  if (at_top()) return;
  --top, --bottom;
  set_text_top("%s", top->c_str());
}

// Mech3.exe 1.2 @ 0x00499820: down a line, the one below written at the bottom.
void ChatLog::scroll_down() {
  if (at_bottom()) return;
  ++top, ++bottom;
  set_text("%s", bottom->c_str());
}

void ChatScroll::on_left_up() { up ? log->scroll_up() : log->scroll_down(); }

// Mech3.exe 1.2 @ 0x00499880
void ChatBar::on_return() {
  if (!screen) return;
  if (multiplayer_team_play() && screen->chat_team.checked) screen->on_button(3, 0, 0);
  else screen->on_button(2, 0, 0);
}

// Mech3.exe 1.2 @ 0x004998b0: the bar's text trimmed (none: nothing), as
// 0x1f {flags (1 to a team), the text with its 0} in its length and 0xc,
// sent unguaranteed to the team (or all), shown here as it is to the others,
// the bar emptied.
void LobbyScreen::chat_send(uint32_t team) {
  std::string text = chat_edit.get_text() ? chat_edit.get_text() : "";
  const size_t a = text.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return;
  text = text.substr(a, text.find_last_not_of(" \t\r\n") - a + 1);
  const uint32_t size = static_cast<uint32_t>(text.size()) + 0xc;
  std::vector<uint8_t> m(size, 0);
  m[0] = 0x1f, m[1] = 0;
  m[2] = static_cast<uint8_t>(size), m[3] = static_cast<uint8_t>(size >> 8);
  if (team) m[4] |= 1;
  std::memcpy(m.data() + 8, text.c_str(), text.size() + 1);
  znet::send(m.data(), size, false, team);
  on_chat(znet::local_id(), m.data(), m.size());
  chat_edit.set_text("");
}

// Mech3.exe 1.2 @ 0x00499610: the sender's line ("< %1 to team >" 0xf43 or
// "< %1 to all >" 0xf44, 0x40 bytes at most), then the text. FIX (A): a
// message cut short is dropped, the text ends within it, its control
// characters are '?'; neither the text nor the name is a format (the
// message's %1 is filled once, zutl::format_message).
void LobbyScreen::on_chat(uint32_t from, const uint8_t* msg, size_t size) {
  LobbyScreen* l = g_lobby;
  if (!l || size < 9) return;
  const char* who = znet::player_name(from);
  std::string head = zutl::format_message_f((msg[4] & 1) ? 0xf43 : 0xf44, who ? who : "");
  if (head.size() > 0x3f) head.resize(0x3f);
  l->chat_log.add(head.c_str());
  const char* text = reinterpret_cast<const char*>(msg + 8);
  std::string line(text, strnlen(text, size - 8));
  for (char& c : line)  // FIX (A): no control characters on the screen; the text only ever a "%s" argument
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = '?';
  l->chat_log.add(line.c_str());
}

// ---- the new team's dialog (piece 13) ----

// Mech3.exe 1.2 @ 0x0049a2b0: a new team, its name and skin, JOIN checked;
// the chat's bar without the keyboard, TP_LIST's choice off; page 3.
void LobbyScreen::open_new_team() {
  if (!state) return;
  team_join = team_new = team_skin = 1;
  chat_edit.set_focus(0);
  teams_list.select_index(-1);
  engine().multiplayer_page(state, 3, 1);  // 0x0049cb00
}

// Mech3.exe 1.2 @ 0x0049a820: our skin alone; page 3.
void LobbyScreen::open_pick_skin() {
  team_skin = 1;
  team_join = team_new = 0;
  if (state) engine().multiplayer_page(state, 3, 1);
}

void TeamNameBar::on_return() { screen->on_button(0, 0, 0); }

// Mech3.exe 1.2 @ 0x0049bd80
TeamScreen::TeamScreen(Engine& e, Handle s)
    : MenuScreen(e, s), cancel_btn(e, 1), ok_btn(e, 0), name_edit(e, this), skins(e, 2) {
  name_edit.editor.set_reject(":|\\<>/?\"*");  // [0x005e28e8]
  name_edit.set_size(0x19);
  if (load_from(e.main_menu_definition(), "MULTIPLAYER_NEW_TEAM_DIALOG", true)) {
    add(&backdrop);
    bind_primitive(&skin_image, "SKIN_TEXTURES");
    bind_button(&cancel_btn, "CANCEL_BUTTON");
    bind_button(&ok_btn, "OK_BUTTON");
    bind_button(&name_edit, "NEW_TEAM_NAME_EDIT");
    bind_button(&join_box, "JOIN_TEAM_CHECKBOX");
    bind_button(&skins, "SKIN_LIST");
  }
  set_flags_all(0);
  set_active(1);
}

// Mech3.exe 1.2 @ 0x0049c170
TeamScreen::~TeamScreen() {
  if (still_tex) ztwod::host().image_release(still_tex);
}

// Mech3.exe 1.2 @ 0x0049c2c0: the screen under it kept as its backdrop; the
// name's bar shown with the keyboard for a new team; SKIN_LIST on to choose
// a skin, holding each one no other team has (every one outside team play),
// its image; a new team named by its first free skin's letter ("A Team",
// 0xf64); JOIN shown and checked for a new team.
void TeamScreen::enter(LobbyScreen* l) {
  lobby = l;
  still_tex = ztwod::host().screen_grab(2);
  if (still_tex) backdrop.set_image(still_tex);
  if (!lobby) return;
  if (lobby->team_new) {
    name_edit.show_for(-1.0f);
    name_edit.set_focus(1);
  } else {
    name_edit.set_focus(0);
    name_edit.hide();
  }
  skins.set_baked(lobby->team_skin != 0);  // +0x90: the button and its arrows on or off
  if (lobby->team_join) join_box.show_for(-1.0f);
  else join_box.hide();
  Engine& e = engine();
  int32_t first = 1000;
  skins.clear_items();
  for (int32_t i = 0; i < 8; ++i)
    if (lobby->free_skin(i + 1, -1) == 0) {
      skins.add_item(e.message(0xdd6 + i + 1), i + 1);
      if (first >= i) first = i;
    }
  show_skin();
  if (lobby->team_new) {
    const char letter[2] = {static_cast<char>('A' + (first < 0x19 ? first : 0x19)), 0};  // [0x005e3e10]
    name_edit.set_text(zutl::format_message_f(0xf64, letter).c_str());
  } else {
    name_edit.set_text("");
  }
  if (lobby->team_join) join_box.set_checked(1);
}

void TeamScreen::show_skin() {
  char name[32];
  std::snprintf(name, sizeof name, "popup_box%d", skins.selected_value());  // [0x005e7cb8]
  skin_image.set_image(name);
}

// Mech3.exe 1.2 @ 0x0049c550: OK, CANCEL (the backdrop freed after either),
// SKIN_LIST's change its image.
void TeamScreen::on_button(int32_t id, int32_t, int32_t) {
  if (id == 2) {
    show_skin();
    return;
  }
  if (id != 0 && id != 1) return;
  if (id == 0) ok();
  else back();
  if (still_tex) ztwod::host().image_release(still_tex);  // 0x00517b10
  still_tex = nullptr;
  backdrop.set_image(static_cast<zimg::Texture*>(nullptr));
}

// Mech3.exe 1.2 @ 0x0049c680: the chat's bar the keyboard again; the lobby.
void TeamScreen::back() {
  if (!state || !lobby) return;
  lobby->chat_edit.set_focus(0);
  lobby->chat_edit.set_focus(1);
  engine().multiplayer_page(state, 2, 0);
}

// Mech3.exe 1.2 @ 0x0049c6c0: a new team needs a name (0x1e5) no team has
// (0x1e6, 0x00589680), then is made with SKIN_LIST's skin, joined with JOIN
// (0x0049a300); else our skin (0x0049a850); back to the lobby.
void TeamScreen::ok() {
  if (!state || !lobby) return;
  Engine& e = engine();
  if (lobby->team_new) {
    std::string name = name_edit.get_text() ? name_edit.get_text() : "";
    const size_t a = name.find_first_not_of(" \t\r\n");
    name = a == std::string::npos ? "" : name.substr(a, name.find_last_not_of(" \t\r\n") - a + 1);
    const int32_t why = name.empty() ? 0x1e5 : znet::team_named(name.c_str()) ? 0x1e6 : 0;
    if (why) {
      cursor_obj.erase();
      e.message_box(0x30, 0x1e4, why);
      set_flags_all(0);
      return;
    }
    lobby->new_team(name.c_str(), skins.selected_value(), join_box.checked != 0);
  } else if (lobby->team_skin) {
    lobby->set_skin(skins.selected_value());
  }
  lobby->chat_edit.set_focus(0);
  lobby->chat_edit.set_focus(1);
  engine().multiplayer_page(state, 2, 0);
}

// The host's lines (the connection screen's, mp_connect.cpp); none for a
// player who joined.
void LobbyScreen::show_reach() {
  if (!znet::is_host()) {
    internet_line.show("", "");
    lan_line.show("", "");
    return;
  }
  const Engine::NetReach r = engine().network_reach();
  const std::string line = reach_line(r);
  if (line != internet_line.line) internet_line.show(line, reach_copy(r));
  const std::string lan = r.lan.empty() ? "" : "LAN: " + r.lan + "  - click to copy";
  if (lan != lan_line.line) lan_line.show(lan, r.lan);
}

}  // namespace mech3
