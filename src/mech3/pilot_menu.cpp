#include "mech3/pilot_menu.h"
#include "mech3/content_campaign.h"

#include <cstring>
#include <string>

#include "mech3/app.h"
#include "mech3/pilot.h"

namespace mech3 {

namespace {
// [0x0060861c] set by the screen (our addition: the engine hears of it, so the
// parts still reached through its memory see the pilot too).
void set_current(Engine& e, Pilot* p) {
  current_pilot() = p;
  e.pilot_chosen(p);
}

PilotScreen* screen_of(ztwod::Layer* owner) { return dynamic_cast<PilotScreen*>(owner); }

// The render target from zVideo's buffers (0x00558290), as the other menus'.
void target_buffers(Engine& e) {
  const Handle a = e.video_back_buffer();
  const Handle b = e.video_buffer_da0();
  const Handle r = e.video_buffer_d90();
  e.render_set_target2(e.video_target(), r, b, a);
}

// The bars' constructors inline in 0x00486f40: what a file name cannot hold
// refused, tab too, 16 bytes (15 characters).
void setup_bar(ShellEditBar& b) {
  b.editor.set_reject(":|\\<>/?\"*\t");
  b.set_size(0x10);
  b.set_focus(0);
}

// The current game's pilot screen object, through the state at [0x00609858].
void pilot_state_mode(Engine& e, int32_t mode) { e.pilot_state_set_mode(e.peek32(0x609858), mode); }
}  // namespace

// ---- the lists, cycles and bars ----

// Mech3.exe 1.2 @ 0x00487e80. An open rename or new name is finished first;
// the row's pilot is chosen (none for no row).
void PilotList::select(int32_t index) {
  PilotScreen* s = screen_of(owner);
  if (s && s->editing()) s->finish_edits();
  FileList::select(index);
  Pilot* p = nullptr;
  if (index >= 0 && index < item_count()) p = find_pilot(items[static_cast<size_t>(index)]->text);
  if (s) s->choose(p);
}

// Mech3.exe 1.2 @ 0x00488040
void GameList::on_left_up() {
  PilotScreen* s = screen_of(owner);
  if (s && s->mode == kInstantAction) return;
  FileList::on_left_up();
}

// Mech3.exe 1.2 @ 0x00487f90: only a campaign's list chooses; the pilot plays
// the save chosen (the list's index is the pilot's campaign).
void GameList::select(int32_t index) {
  PilotScreen* s = screen_of(owner);
  if (!s || s->mode != kCampaign) return;
  FileList::select(index);
  if (s->chosen) s->chosen->select_campaign(index);
}

// Mech3.exe 1.2 @ 0x00487440. The pilot's training mission becomes the
// choice's value, within 1..4.
void ScenarioCycle::select(int32_t i) {
  CycleButton::select(i);
  PilotScreen* s = screen_of(owner);
  if (!s) return;
  s->descriptions.select(i);
  s->completed.select(i);
  Pilot* p = s->chosen;
  if (!p || !p->training_game()) return;
  const Item* it = item(index);
  int32_t v = it ? it->value : 0;
  if (v > 4) v = 4;
  else if (v < 1) v = 1;
  p->training_game()->mission = v;
}

NewPilotBar::NewPilotBar(Engine& e) : ShellEditBar(e) {}
RenameBar::RenameBar(Engine& e) : ShellEditBar(e) {}

// Mech3.exe 1.2 @ 0x00487dc0: a click gives the bar the focus, unless a rename is open.
void NewPilotBar::on_left_up() {
  PilotScreen* s = screen_of(owner);
  if (s && s->rename_bar.active) return;
  active = 1;
  set_focus(1);
}
void RenameBar::on_left_up() {
  PilotScreen* s = screen_of(owner);
  if (s && s->rename_bar.active) return;
  active = 1;
  set_focus(1);
}

// Mech3.exe 1.2 @ 0x00487cf0: Enter makes the pilot (none for no name).
void NewPilotBar::on_return() {
  const char* t = get_text();
  active = 0;
  set_focus(0);
  if (!t || !*t) return;
  Pilot* p = create_pilot(get_text());
  if (PilotScreen* s = screen_of(owner)) s->refresh(true, p);
}

// Mech3.exe 1.2 @ 0x00487d40: Escape with no pilots and no name leaves the
// screen; otherwise the name is cleared.
void NewPilotBar::on_cancel() {
  PilotScreen* s = screen_of(owner);
  const char* t = get_text();
  if (pilots().empty() && (!t || !*t)) {
    active = 0;
    set_focus(0);
    if (s) s->cancel();
    e_.input_reset();
    return;
  }
  set_text("");
  if (s) s->refresh(true, nullptr);
  active = 0;
  set_focus(0);
  e_.input_reset();
}

// Mech3.exe 1.2 @ 0x00487de0: Enter renames (not to no name).
void RenameBar::on_return() {
  const char* t = get_text();
  if (!t || !*t) return;
  if (PilotScreen* s = screen_of(owner)) s->rename(false, false);
}

// Mech3.exe 1.2 @ 0x00487e10
void RenameBar::on_cancel() {
  if (PilotScreen* s = screen_of(owner)) s->rename(false, true);
  e_.input_reset();
}

// ---- the screen ----

// Mech3.exe 1.2 @ 0x00486f40. The dialog is the main menu's tree (not
// unloaded here); its BACKGROUND is the main menu's image, read unchecked.
PilotScreen::PilotScreen(Engine& e, Handle s)
    : MenuScreen(e, s), cancel_btn(e, 0x17), accept(e, 0x18), delete_btn(e, 0x19), rename_btn(e, 0x1a),
      rename_bar(e), new_bar(e), pilots(e), games(e), scenarios(e), descriptions(e, -1), completed(e, -1) {
  setup_bar(rename_bar);
  setup_bar(new_bar);
  target_buffers(e);
  const bool moving = !e.fast_menu();
  if (load_from(e.main_menu_definition(), "PILOTDIALOG", moving)) {
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
    bind_primitive(&lines, "LINES");
    bind_primitive(&lines2, "LINES2");
    bind_button(&cancel_btn, "CANCEL");
    bind_button(&accept, "ACCEPT");
    if (!moving) {
      bake();
      remove(&background);
      remove(&lines);
      remove(&lines2);
      cancel_btn.set_baked(true);
      accept.set_baked(true);
    }
    bind_button(&pilots, "PILOTLIST");
    bind_button(&games, "GAMELIST");
    bind_button(&new_bar, "EDITBAR");
    bind_button(&rename_bar, "RENAMEBAR");
    bind_button(&delete_btn, "DELETE");
    bind_button(&rename_btn, "RENAME");
    bind_button(&scenarios, "SCENARIO_LIST");
    bind_button(&descriptions, "SCENARIO_DESC");
    bind_button(&completed, "SCENARIO_COMPLETE");
  }
  set_flags_all(0);
  rename_bar.hide();
  set_active(1);
  chosen = nullptr;
  load_pilots();
  refresh(false, nullptr);
}

// Mech3.exe 1.2 @ 0x004878b0
PilotScreen::~PilotScreen() { delete_other_pilots(); }

// Mech3.exe 1.2 @ 0x00488170: Delete deletes the chosen pilot, Ctrl+R renames it.
void PilotScreen::update(float dt) {
  Window::update(dt);
  if (editing()) return;
  Engine& e = engine();
  if (e.key_state(0xd3) == 1) {
    delete_chosen();
    return;
  }
  if (e.key_state(0x213) == 1) rename(true, false);
}

// Mech3.exe 1.2 @ 0x004880a0
void PilotScreen::on_button(int32_t id, int32_t a, int32_t b) {
  (void)a, (void)b;
  switch (id) {
    case 0x17:  // CANCEL
      finish_edits();
      cancel();
      break;
    case 0x18:  // ACCEPT: the chosen pilot goes on (2)
      finish_edits();
      if (chosen) {
        set_current(engine(), chosen);
        engine().pop_state(2);
      }
      break;
    case 0x19:  // DELETE
      finish_edits();
      delete_chosen();
      break;
    case 0x1a:  // RENAME
      if (!editing()) rename(true, false);
      break;
    default: break;
  }
  dirty_all();
}

// Mech3.exe 1.2 @ 0x00487b30
void PilotScreen::reset() {
  pilots.x200 = 0;
  rename_bar.hide();
  rename_bar.active = 0;
  rename_bar.set_focus(0);
  new_bar.active = 0;
  new_bar.set_focus(0);
  chosen = current_pilot();
  refresh(true, nullptr);
}

// Mech3.exe 1.2 @ 0x00487b90: a rename is finished, a new name is made (Enter).
bool PilotScreen::finish_edits() {
  if (rename_bar.active) {
    rename(false, false);
    return true;
  }
  if (new_bar.active) new_bar.on_return();
  return true;
}

// Mech3.exe 1.2 @ 0x00487bd0. Starting puts the bar over the chosen row with
// its name; ending renames the pilot (unless cancelled) and lists again.
void PilotScreen::rename(bool start, bool cancel) {
  if (new_bar.active) return;
  FileRow* row = pilots.selected_row;
  if (start) {
    if (rename_bar.active || !chosen) return;
    if (!row) return;
    row->hide();
    rename_bar.set_pos(row->get_x(), row->get_y());
    rename_bar.set_text(row->rendered_text());
    rename_bar.show_for(-1.0f);
    rename_bar.active = 1;
    rename_bar.set_focus(1);
    return;
  }
  if (!rename_bar.active) return;
  if (!cancel && chosen) chosen->rename(rename_bar.get_text());
  rename_bar.hide();
  if (pilots.selected_row) pilots.selected_row->show_for(-1.0f);
  rename_bar.active = 0;
  rename_bar.set_focus(0);
  refresh(true, nullptr);
}

// Mech3.exe 1.2 @ 0x00487e30: "Ok to delete pilot: %1?" (0x1e0, 0xf26).
// FIX (bug): with no pilot chosen (DELETE pressed before there is one) the
// exe formats the name at address 0x18 and crashes; nothing happens here.
void PilotScreen::delete_chosen() {
  Pilot* p = chosen;
  cursor_obj.erase();
  if (!p) return;
  if (engine().message_box1(1, 0x1e0, 0xf26, p->name) != 1) return;
  delete_pilot(p->name);
  refresh(true, nullptr);
}

// Mech3.exe 1.2 @ 0x00488060
void PilotScreen::cancel() {
  if (editing()) return;
  set_current(engine(), nullptr);
  engine().pop_state(3);
}

// Mech3.exe 1.2 @ 0x004881e0: the games listed for the pilot, its game
// selected; the new name's bar emptied.
void PilotScreen::choose(Pilot* p) {
  chosen = p;
  games.clear_items();
  games.layout();
  if (chosen) {
    chosen->select_game(mode);
    fill_games();
  }
  new_bar.set_text("");
}

// Mech3.exe 1.2 @ 0x004882b0. The campaign's saves (the newest chosen), the
// Instant Action record, the multiplayer score, or the training missions.
void PilotScreen::fill_games() {
  games.clear_items();
  games.layout();
  games.show_for(-1.0f);
  if (!chosen) return;
  ActionGame* action = chosen->action();
  TrainingGame* training = chosen->training_game();
  MultiplayerGame* multi = chosen->multiplayer();
  Engine& e = engine();
  auto label = [&](int32_t id) {
    const char* m = e.message(id);
    if (!games.labels.empty()) games.labels.front()->set_text_plain(m);
  };
  auto line = [&](int32_t id, std::initializer_list<int32_t> args) {
    games.add_item(e.message_format_ints(id, args).c_str(), 0);
  };
  switch (mode) {
    case kCampaign: {
      append_content_campaign_choices(*chosen);
      label(0x27c);
      for (int32_t i = 0; i < chosen->campaign_count(); ++i)
        games.add_item(static_cast<CampaignGame*>(chosen->campaigns[static_cast<size_t>(i)])->name(), i);
      games.select_index(chosen->campaign_count() - 1);
      break;
    }
    case kInstantAction:
      label(0x279);
      if (!action) break;
      line(0x184, {action->player_kills});
      line(0x185, {action->lancemate_kills});
      line(0x186, {action->enemy_kills});
      games.add_item(" ", 0);  // sprintf(" ")
      line(0x187, {action->attack_wins});
      line(0x188, {action->attack_losses});
      line(0x189, {action->defend_wins});
      line(0x18a, {action->defend_losses});
      games.add_item(" ", 0);
      line(0xdb7, {action->waves});
      break;
    case kMultiplayer:
      label(0x27d);
      if (multi) line(0x27e, {multi->kills, multi->deaths});
      break;
    case kTraining: {
      games.hide();
      scenarios.clear_items();
      descriptions.clear_items();
      completed.clear_items();
      int32_t first_open = -1;
      for (int32_t i = 1; training && i <= 4; ++i) {
        scenarios.add_item(e.message(TrainingGame::name_id(i)), i);  // 0x00437060
        descriptions.add_item(e.message(TrainingGame::text_id(i)), i);  // 0x004370d0
        const bool passed = training->passed[i - 1] != 0;
        completed.add_item(e.message_format_ints(0x280 + (passed ? 1 : 0), {}).c_str(), i);
        if (first_open < 0 && !passed) first_open = i - 1;
      }
      if (first_open < 0) first_open = 0;
      scenarios.select(first_open);
      break;
    }
    default: break;
  }
}

// Mech3.exe 1.2 @ 0x00488770. With no pilots at all the new name's bar takes
// the focus (when asked).
void PilotScreen::refresh(bool focus_new, Pilot* p) {
  if (!p) p = chosen;
  pilots.clear_items();
  pilots.layout();
  int32_t row = 0;
  for (Pilot* q : mech3::pilots()) {
    const int32_t i = pilots.add_item(q->name, 0);  // FIX (A): the exe's value is the pilot's address (unread)
    if (q == p) row = i;
  }
  pilots.select(row);
  if (chosen && mode == kTraining) {
    games.hide();
    show_lists();
    fill_games();
  }
  if (mech3::pilots().empty() && focus_new) {
    new_bar.active = 1;
    new_bar.set_focus(1);
  }
}

void PilotScreen::show_lists() {
  const bool show = mode == kTraining && chosen;
  ztwod::Object2D* const cycles[] = {&scenarios, &descriptions, &completed};
  for (ztwod::Object2D* c : cycles) {
    if (show) c->show_for(-1.0f);
    else c->hide();
  }
}

// ---- the state ----

// Mech3.exe 1.2 @ 0x00488910
PilotState::PilotState(Engine& e, Handle addr) : MenuState("Pilot State", addr), e_(e) {
  screen = new PilotScreen(e, addr);  // new(0x13184)
  screen_ = e.adopt_screen(screen, 0x13184);
  if (!screen_) {  // no engine to own it: this state does
    delete screen;
    screen = nullptr;
  }
}

PilotState::~PilotState() {
  if (screen_) e_.vcall(screen_, 0x08, {1});
  screen_ = 0;
}

// Mech3.exe 1.2 @ 0x004889a0
bool PilotState::enter() {
  prepare();
  return ShellState::enter();
}

// 0x004889a0 before the shell's enter. From a lobby the lobby's player is the
// pilot, made when there is none, and the screen goes at once (2).
void PilotState::prepare() {
  Engine& en = e_;
  if (en.network_lobby_active()) {
    if (!current_pilot()) {
      const Handle session = en.lobby_session();
      set_current(en, create_pilot(en.lobby_player_name(session)));
    }
    en.pop_state(2);
    return;
  }
  if (screen) {
    screen->mode = mode;
    if (mode == kTraining) screen->games.hide();
    else screen->games.show_for(-1.0f);
    screen->show_lists();
    screen->fill_games();
    screen->reset();
  }
}

// Mech3.exe 1.2 @ 0x00488990
void PilotState::escape() {
  if (screen) screen->cancel();
}

// Mech3.exe 1.2 @ 0x00488ad0
void PilotState::set_mode(int32_t m) {
  mode = m;
  if (!screen) return;
  screen->mode = m;
  if (m == kTraining) screen->games.hide();
  else screen->games.show_for(-1.0f);
  screen->show_lists();
  screen->fill_games();
}

// ---- the game states ----

namespace {
const char* name_of(GameMenuState::Kind k) {
  switch (k) {
    case GameMenuState::kAction: return "Action State";
    case GameMenuState::kCampaign: return "Campaign State";
    case GameMenuState::kTraining: return "Training State";
    default: return "Multiplayer State";
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x0048a6c0 (its screen 0x00488bc0, new(0x17730)),
// 0x0048aef0, 0x0048b120, 0x0049c900 (its three screens 0x00493260,
// 0x00495f90, 0x0049bd80): the screens are the engine's to make (the
// multiplayer ones not ported: 0 here).
GameMenuState::GameMenuState(Kind k, Handle addr) : MenuState(name_of(k), addr), kind(k) {
  Engine& en = e();
  if (k == kAction) screen_ = en.construct(0x488bc0, {addr});
  if (k == kMultiplayer) {
    screens[0] = en.construct(0x493260, {addr});  // new(0x13b04)
    screens[1] = en.construct(0x495f90, {addr});  // new(0x1505c)
    screens[2] = en.construct(0x49bd80, {addr});  // new(0x11f3c)
  }
}

// Mech3.exe 1.2 @ 0x0048a750, 0x0048afd0, 0x0048b140, 0x0049cab0
bool GameMenuState::enter() {
  Engine& en = e();
  Mech3App& ap = a();
  switch (kind) {
    case kAction:
      shell_enter();
      pilot_state_mode(en, mech3::kInstantAction);
      if (ap.instant_action_arg()) {  // back from a game: straight on with the pilot
        if (current_pilot()) current_pilot()->select_game(mech3::kInstantAction);
        ap.instant_action_arg() = 0;
        en.action_open(screen_);  // 0x0048aac0
        return true;
      }
      break;
    case kCampaign:
      shell_enter();
      pilot_state_mode(en, mech3::kCampaign);
      break;
    case kTraining:
      shell_enter();
      pilot_state_mode(en, mech3::kTraining);
      break;
    case kMultiplayer:
      page = 0;
      x19 = 1;
      want = en.network_lobby_active() ? 2 : 1;
      shell_enter();
      pilot_state_mode(en, mech3::kMultiplayer);
      break;
  }
  en.push_state(en.peek32(0x609858), 0);
  return true;
}

// Mech3.exe 1.2 @ 0x0048af20: after the opening movie (when one plays) the
// briefing replaces the menus.
bool GameMenuState::update() {
  Engine& en = e();
  if (kind == kMultiplayer && page != want) en.multiplayer_switch(addr());  // 0x0049cb90: 0x0049cb20
  if (kind == kCampaign) {
    bool done = true;
    if (movie) {
      done = !en.movie_playing(movie);
      if (done) {
        en.movie_stop(movie, 0);
        if (movie) en.movie_delete(movie);
        movie = 0;
      }
    }
    if (done) {
      en.pop_state(0);
      a().replace(a().state(6), 0);
    }
  }
  return shell_update();
}

// Mech3.exe 1.2 @ 0x0048afa0
void GameMenuState::leave() {
  if (kind == kCampaign) {
    if (movie) e().movie_delete(movie);
    movie = 0;
    return;
  }
  ShellState::leave();
}

// Mech3.exe 1.2 @ 0x0048a7b0, 0x0048b000, 0x0048b170, 0x0049cbb0: what the
// pilot screen ended with, 2 a pilot, 3 cancelled.
void GameMenuState::resume(uint32_t arg) {
  Engine& en = e();
  Mech3App& ap = a();
  Pilot* p = current_pilot();
  switch (kind) {
    case kAction:
      if (arg == 2) {
        if (p) p->select_game(mech3::kInstantAction);
        shell_resume(1);
        en.action_open(screen_);
      } else if (arg == 3) {
        shell_resume(0);
        en.pop_state(1);
      } else if (arg == 4) {
        shell_resume(1);
      }
      break;
    case kCampaign:
      if (arg == 2) {
        if (!p) return;
        CampaignGame* c = p->campaign();
        if (ap.fmv() && c && c->mission_number() == 1 && c->operation() == 1) {
          movie = en.movie_new();  // new(0x20) + 0x00598cc0(0, 0, 0)
          const Handle hwnd = ap.frame_ ? en.frame_hwnd(ap.frame_) : 0;
          if (hwnd) en.movie_set_window(movie, hwnd);
          if (en.movie_load(movie, "fmv.zrd", "campaign") != -1) en.movie_play(movie);
        }
        shell_resume(1);
      } else if (arg == 3) {
        en.pop_state(1);
      }
      break;
    case kTraining:
      if (arg == 2) {
        shell_resume(1);
        en.pop_state(0);
        if (p) p->select_game(mech3::kTraining);
        a().replace(a().state(6), 0);
      } else if (arg == 3) {
        shell_resume(0);
        en.pop_state(1);
      }
      break;
    case kMultiplayer:
      if (arg == 2) {
        shell_resume(1);
        en.network_set(1);  // 0x0047fc40
        if (!current_pilot()) {
          const Handle session = en.lobby_session();
          set_current(en, create_pilot(en.lobby_player_name(session)));
        }
        if (current_pilot()) current_pilot()->select_game(mech3::kMultiplayer);
        en.multiplayer_take_pilot(screens[0], screens[1]);  // 0x00494920, the second's pilot and game
      } else if (arg == 3) {
        shell_resume(0);
        en.network_set(0);
        en.pop_state(1);
        escape();
      } else if (arg == 4) {
        shell_resume(1);
        en.multiplayer_resume(screens[1]);  // 0x0049b660
      }
      break;
  }
}

// Mech3.exe 1.2 @ 0x00438c80; the campaign's +0x28 is 0x005ac5c0 (nothing)
// and the multiplayer's 0x0049bfd0 (nothing).
void GameMenuState::escape() {
  if (kind == kCampaign || kind == kMultiplayer) return;
  MenuState::escape();
}

}  // namespace mech3
