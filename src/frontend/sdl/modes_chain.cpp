#include "frontend/sdl/modes_chain.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "gamez/zclass/class.h"
#include "gamez/zinput/zinput.h"
#include "gamez/zreader/zreader.h"
#include "mech3/action_menu.h"
#include "mech3/app_vehicle.h"
#include "mech3/controls_cfg.h"
#include "mech3/mech_objective.h"
#include "mech3/app.h"
#include "mech3/app_engine.h"
#include "mech3/app_hud.h"
#include "mech3/app_mech.h"
#include "mech3/app_sensors.h"
#include "mech3/app_states.h"
#include "mech3/briefing.h"
#include "mech3/debriefing.h"
#include "mech3/mainmenu.h"
#include "mech3/mission.h"
#include "mech3/pause_menu.h"
#include "mech3/pilot.h"
#include "mech3/pilot_menu.h"
#include "mech3/zai_valve.h"

namespace sdlfe {
namespace {

const char* const kScenarios[] = {"None", "Battle", "Battle Waves", "Battle Progression", "Attack", "Defend"};
const char* const kWorlds[] = {"c1", "c2", "c3", "c4"};

struct Tally {
  int32_t player = 0, attack_wins = 0, attack_losses = 0, defend_wins = 0, defend_losses = 0, waves = 0;
};
Tally tally() {
  Tally t;
  mech3::Pilot* p = mech3::current_pilot();
  if (mech3::ActionGame* g = p ? p->action() : nullptr)
    t = {g->player_kills, g->attack_wins, g->attack_losses, g->defend_wins, g->defend_losses, g->waves};
  return t;
}
Tally g_before;

}  // namespace

// "all", or "<world>:<scenario>" (c2:4), or a world alone (c3).
ModesChain::ModesChain(mech3::GameEngine& e, mech3::Mech3App& app, std::string which) : e_(e), app_(app) {
  training_ = which == "training";
  if (training_) return;
  if (which != "all" && !which.empty()) {
    const int w = which.size() >= 2 && which[0] == 'c' ? which[1] - '1' : -1;
    if (w >= 0 && w <= 3) first_world_ = last_world_ = w;
    const size_t colon = which.find(':');
    if (colon != std::string::npos) first_scenario_ = last_scenario_ = std::atoi(which.c_str() + colon + 1);
  }
  world_ = first_world_, scenario_ = first_scenario_;
}

void ModesChain::frame() {
  if (done_) return;
  mech3::States& st = mech3::states();
  zgame::State* now = app_.current();
  if (now != last_) last_ = now, frames_in_state_ = 0;
  ++frames_in_state_;
  ++frames_;
  for (size_t i = 0; i < releases_.size();)
    if (releases_[i].first <= frames_) {
      if (key) key(releases_[i].second, false);
      releases_.erase(releases_.begin() + static_cast<long>(i));
    } else {
      ++i;
    }
  if (now == &st.main && mech3::main_menu() && frames_in_state_ == 30 && !pilot_done_) {
    const_cast<mech3::MainMenu*>(mech3::main_menu())->buttons[training_ ? 3 : 0].on_left_up();  // TRAINING, INSTANT ACTION
  } else if (auto* ps = dynamic_cast<mech3::PilotState*>(now)) {
    if (!ps->screen || frames_in_state_ != 20 || !app_.pending_list().empty()) return;
    if (training_) {  // the first mission not passed is chosen (0x004873d0's list)
      mech3::Pilot* p = ps->screen->chosen;
      mech3::TrainingGame* g = p ? p->training_game() : nullptr;
      if (pilot_done_ && g && g->passed[0] && g->passed[1] && g->passed[2] && g->passed[3]) {
        std::printf("mw3: modes training done: %d missions passed by their lessons\n", missions_done_);
        done_ = true;
        return;
      }
    } else if (pilot_done_) {
      return;
    }
    if (!pilot_done_) {
      ps->screen->new_bar.set_text(training_ ? "Trainee" : "Actions");
      ps->screen->new_bar.on_return();
    }
    ps->screen->on_button(0x18, 0, 0);  // ACCEPT
    pilot_done_ = true, accepted_ = false;
  } else if (auto* gs = dynamic_cast<mech3::GameMenuState*>(now); gs && gs->kind == mech3::GameMenuState::kAction) {
    if (frames_in_state_ == 20) action_screen();
  } else if (now == &st.load) {
    mech3::BriefingScreen* b = mech3::briefing_screen();
    if (b && b->loaded && frames_in_state_ > 10 && !accepted_) {
      accepted_ = true;
      b->on_button(mech3::BriefingScreen::kBtnLaunch, 0, 0);
    }
  } else if (now == &st.core) {
    if (frames_in_state_ == 1) core_frames_ = 0, kills_ = hits_ = 0, ended_logged_ = quitting_ = false, lesson_ = -2, stuck_ = 0;
    training_ ? training_core() : core();
  } else if (now == &st.pause) {
    auto* p = dynamic_cast<mech3::PauseScreen*>(e_.screen_of(st.pause.screen_));
    if (p && frames_in_state_ == 5) p->on_button(mech3::PauseScreen::kQuit, 0, 0);
  } else if (now == &st.end_win && frames_in_state_ == 60) {
    debriefing();
  }
}

// The case's choices on the Action screen, then ACCEPT. Back from a case's
// end (the IA Dummy State reopened it) the last case's tallies are checked.
void ModesChain::action_screen() {
  auto* gs = static_cast<mech3::GameMenuState*>(app_.current());
  auto* a = dynamic_cast<mech3::ActionScreen*>(e_.screen_of(gs->screen_));
  if (!a) return;
  if (!case_.empty()) {
    const Tally t = tally();
    const bool attack = scenario_ == 4, defend = scenario_ == 5;
    const bool counted = (attack && (win_round_ ? t.attack_wins == g_before.attack_wins + 1 : t.attack_losses == g_before.attack_losses + 1)) ||
                         (defend && (win_round_ ? t.defend_wins == g_before.defend_wins + 1 : t.defend_losses == g_before.defend_losses + 1)) ||
                         (!attack && !defend && t.attack_wins == g_before.attack_wins && t.defend_wins == g_before.defend_wins &&
                          t.attack_losses == g_before.attack_losses && t.defend_losses == g_before.defend_losses);
    std::printf("mw3: modes case %s ended: player kills %d -> %d, attack %d/%d, defend %d/%d, best wave %d: %s\n",
                case_.c_str(), g_before.player, t.player, t.attack_wins, t.attack_losses, t.defend_wins, t.defend_losses,
                t.waves, counted ? "tallied as the exe tallies" : "NOT tallied as expected");
    counted ? ++results_ok_ : ++results_bad_;
    next_case();
    if (done_) return;
  }
  // The world, the scenario (its step refills the enemies), a line-up.
  a->worlds.select(world_);
  a->scenarios.step(scenario_ - 1 - a->scenarios.index);  // its rows: Battle (0) .. Defend (4)
  const int lancemates = cases_ % 3;  // none, one, two
  for (int i = 0; i < lancemates; ++i) a->allies[2 + i].select(6 + i);
  if (scenario_ >= 4) a->allies[1].select(1);  // the field base, in Attack and Defend
  int enemies = 1;
  if (a->fixed)  // a custom line-up (+0x16f9c 1): up to four enemies; else the wave's
    for (int i = 1; i <= cases_ % 4; ++i) a->enemies[i].select(3 + i), ++enemies;
  win_round_ = cases_ % 2 == 0;  // the round ABORT tallies: won in even cases, lost in odd
  round_ = 1;
  case_ = std::string(kWorlds[world_]) + " " + kScenarios[scenario_];
  std::printf("mw3: modes case %s: world %d, scenario %d (%s), %d lancemate(s), %d enemy slot(s)%s; ends %s\n",
              case_.c_str(), a->worlds.selected_value(), a->scenarios.index, a->scenarios.labels.empty() ? "?" : a->scenarios.labels[0]->text,
              lancemates, enemies, a->fixed ? "" : " (the wave's)", win_round_ ? "won" : "lost");
  g_before = tally();
  accepted_ = false;
  a->on_button(4, 0, 0);  // ACCEPT
}

void ModesChain::next_case() {
  ++cases_;
  if (++scenario_ > last_scenario_) scenario_ = first_scenario_, ++world_;
  if (world_ > last_world_) {
    std::printf("mw3: modes done: %d cases, %d tallied as expected, %d not\n", cases_, results_ok_, results_bad_);
    done_ = true;
  }
}

// The round: won (shooting until the objectives end the mission; the wave
// modes two waves cleared, then QUIT) or lost (the pause's REPLAY at frame 90).
void ModesChain::core() {
  ++core_frames_;
  mech3::Missions& m = mech3::missions();
  mech3::Pilot* p = mech3::current_pilot();
  mech3::ActionGame* g = p ? p->action() : nullptr;
  const int mode = g ? g->mode : 0;
  const bool waves = mode == 1 || mode == 2;
  const bool win = round_ == 2 ? win_round_ : !win_round_;
  if (core_frames_ == 2) {
    waves_start_ = g ? g->x6c : 0;
    std::printf("mw3: modes %s round %d: world index %d loaded, the game's world %d mission %d mode %d wave %d\n",
                case_.c_str(), round_, m.current, g ? g->x74 : -1, g ? g->x78 : -1, mode, waves_start_);
  }
  if (m.ended) {
    if (!ended_logged_) {
      ended_logged_ = true;
      std::printf("mw3: modes %s round %d %s at core frame %d (%d kills, %d hits)\n", case_.c_str(), round_,
                  m.aborting ? "lost" : "won by its objectives", core_frames_, kills_, hits_);
      if (!m.aborting) m.on_command(0x71);  // Exit Mission
    }
    return;
  }
  if (quitting_) return;
  if (!win && core_frames_ == 90) {
    m.abort_now();  // the pause's REPLAY: lost at once (0x0048fa80)
    return;
  }
  if (core_frames_ > 2 && win && waves && g && g->x6c >= waves_start_ + 2) {  // the start read at frame 2
    std::printf("mw3: modes %s round %d: waves %d and %d cleared at core frame %d (%d kills); QUIT\n", case_.c_str(), round_,
                waves_start_, waves_start_ + 1, core_frames_, kills_);
    quitting_ = true;
    app_.push(app_.state(3), 0);  // Escape, as the Core State's update pushes it: the Pause State, whose QUIT leaves
    return;
  }
  if (core_frames_ == 20000) {
    std::printf("mw3: modes %s round %d NOT won by its objectives after 20000 frames: Exit Mission\n", case_.c_str(), round_);
    m.ended = 1;
    m.on_command(0x71);
    return;
  }
  if (core_frames_ < 60 || !win) return;
  static void* gun = nullptr;
  if (!gun) gun = mech3::chain_weapon();
  for (const mech3::ChainTarget& t : mech3::chain_hostiles()) {
    ++hits_;
    if (mech3::chain_hit(t.h, gun)) ++kills_;
  }
}

// Round 1's debriefing CONTINUEs (the next round), round 2's ABORTs.
void ModesChain::debriefing() {
  mech3::States& st = mech3::states();
  auto* d = dynamic_cast<mech3::DebriefScreen*>(e_.screen_of(st.end_win.screen_));
  if (!d) return;
  std::printf("mw3: modes %s debriefing rows round %d: %s\n", case_.c_str(), round_, mech3::debrief_rows(*d).c_str());
  if (round_ == 1) {
    std::printf("mw3: modes %s debriefing round 1: score %d, CONTINUE (the next round)\n", case_.c_str(), d->score);
    round_ = 2, accepted_ = false;
    d->on_button(mech3::DebriefScreen::kContinue, 0, 0);
  } else {
    std::printf("mw3: modes %s debriefing round 2: score %d, ABORT\n", case_.c_str(), d->score);
    d->on_button(mech3::DebriefScreen::kAbort, 0, 0);
  }
}

// A key code as zInput keeps it (zinput.h: DIK | 0x100 Alt | 0x200 Ctrl |
// 0x400 Shift): the modifiers held around the key, `hold` frames.
void ModesChain::press_key(int code, int hold) {
  const int dik = code & 0xff;
  if (!key || dik <= 0) return;
  const int mods[3][2] = {{0x100, 0x38}, {0x200, 0x1d}, {0x400, 0x2a}};  // DIK_LMENU, DIK_LCONTROL, DIK_LSHIFT
  for (const auto& m : mods)
    if (code & m[0]) key(m[1], true);
  key(dik, true);
  releases_.push_back({frames_ + hold, dik});
  for (const auto& m : mods)
    if (code & m[0]) releases_.push_back({frames_ + hold, m[1]});
}

void ModesChain::press(const char* command, int hold) {
  press_key(static_cast<int>(zin::key1_of(mech3::controls_command(command))), hold);
}

// A training mission: its lessons logged as they change, each acted on until
// the mission is won (Exit Mission then, the end screen's TR Dummy State).
void ModesChain::training_core() {
  ++core_frames_;
  mech3::Missions& m = mech3::missions();
  mech3::Pilot* p = mech3::current_pilot();
  mech3::TrainingGame* g = p ? p->training_game() : nullptr;
  const int lesson = g && g->lessons ? g->lessons->current : -1;
  if (m.ended) {
    if (!ended_logged_) {
      ended_logged_ = true;
      std::printf("mw3: modes training mission %d %s at core frame %d (lesson %d)\n", g ? g->mission : 0,
                  m.aborting ? "LOST" : "won by its lessons", core_frames_, lesson + 1);
      if (!m.aborting) ++missions_done_, m.on_command(0x71);  // Exit Mission
    }
    return;
  }
  if (lesson != lesson_) {
    if (lesson_ >= 0)
      std::printf("mw3: modes training mission %d lesson %d done at core frame %d (%d frames)\n", g ? g->mission : 0,
                  lesson_ + 1, core_frames_, core_frames_ - lesson_at_);
    lesson_ = lesson, lesson_at_ = core_frames_, act_ = 0;
  }
  if (core_frames_ - lesson_at_ == 6000) {
    std::printf("mw3: modes training mission %d lesson %d NOT met after 6000 frames: Exit Mission\n", g ? g->mission : 0,
                lesson + 1);
    ++stuck_;
    m.ended = 1;
    m.on_command(0x71);
    return;
  }
  if (core_frames_ > 30 && core_frames_ % 20 == 0) lesson_act();
}

namespace {
// The group's objectives not done, depth first (a group met together needs
// them all at once: t1m4's place and shutdown).
void open_leaves(const mech3::Objective* o, std::vector<const mech3::Objective*>& out) {
  if (!o || o->done) return;
  if (auto* g = dynamic_cast<const mech3::ObjectiveGroup*>(o)) {
    for (const auto& c : g->items) open_leaves(c.get(), out);
    return;
  }
  out.push_back(o);
}
// An AI record's trigger ("vlist" in aiv.zrd) that sets `valve`: its record
// (the entity), its region, and what it waits for, by the entry's fields as
// the shipped ones read: 2 a weapon fired (fired_mgun), 5 the health below
// 6 (mech_hit: the record's mech hit), 9 the player in the region
// (inst_spotted: reg_instruct).
struct Trigger {
  std::string record, region;
  bool fired = false, hit = false, inside = false;
};
bool find_trigger(const zrdr::Node* n, const char* valve, const std::string& record, Trigger* out, int depth = 0) {
  if (!n || n->type != zrdr::List || !n->list) return false;
  const int32_t count = zrdr::list_count(n);
  auto i = [&](int32_t k) { return k < count && n->list[k].type == zrdr::Int ? n->list[k].i : 0; };
  for (int32_t k = 1; k < count; ++k) {
    const zrdr::Node& c = n->list[k];
    if (c.type == zrdr::String && c.s && std::strcmp(c.s, valve) == 0 && k > 10 && n->list[10].type == zrdr::String) {
      out->record = record, out->region = n->list[10].s ? n->list[10].s : "";
      out->fired = i(2) == 1, out->hit = i(5) == 1, out->inside = i(9) == 1;
      return true;
    }
    if (c.type == zrdr::List) {
      // a record's value follows its name in the top list
      const bool top = depth <= 1;  // the file's list: record names, then their values
      const std::string rec = top && k > 1 && n->list[k - 1].type == zrdr::String && n->list[k - 1].s ? n->list[k - 1].s : record;
      if (find_trigger(&c, valve, rec, out, depth + 1)) return true;
    }
  }
  return false;
}
bool valve_trigger(const char* valve, Trigger* out) {
  zrdr::Node* aiv = zrdr::load("aiv.zrd");
  const bool found = find_trigger(aiv, valve, "", out);
  if (aiv) zrdr::free_node(aiv);
  return found;
}
bool named_at(const char* name, float out[3]) {
  const uint32_t h = name ? mech3::chain_entity_named(name) : 0;
  return h && mech3::chain_handle_at(h, out);
}
const char* const kKinds[] = {"group", "destroy", "node_change", "reach_area", "valve_change", "reach_velocity",
                              "aim_at_object", "target_object", "time_limit", "view_object", "face_object",
                              "heat_warning", "User_Command", "joystick_press", "key_press", "mouse_press",
                              "weapon_fire", "weapon_select", "weapon_group_select"};
}  // namespace

// What a player does for the current lesson's first open primary objective.
void ModesChain::lesson_act() {
  using O = mech3::Objective;
  const mech3::Objectives& ob = mech3::mission_objectives();
  std::vector<const O*> open;
  open_leaves(ob.primary.get(), open);
  if (open.empty()) return;
  ++act_;
  if (act_ == 1) {
    std::string kinds;
    for (const O* o : open) kinds += std::string(kinds.empty() ? "" : ", ") + (o->kind < sizeof kKinds / sizeof kKinds[0] ? kKinds[o->kind] : "?");
    std::printf("mw3: modes training lesson %d wants %s\n", lesson_ + 1, kinds.c_str());
  }
  bool fire = false;
  for (const O* o : open) {
    if (o->kind == O::kWeaponFire) {  // one press for every weapon_fire: fire, then the next weapon
      fire = true;
      continue;
    }
    act_on(o);
  }
  if (fire) press(act_ % 2 ? "CMD_TRIGGER1" : (act_ % 16 == 0 ? "CMD_WEPGRP_NEXT" : "CMD_WEP_NEXT"));
}

void ModesChain::act_on(const mech3::Objective* o) {
  using O = mech3::Objective;
  static void* gun = nullptr;
  if (!gun) gun = mech3::chain_weapon();
  switch (o->kind) {
    case O::kUserCommand:
    case O::kKeyPress:
      if (auto* in = dynamic_cast<const mech3::InputObjective*>(o)) press_key(in->x34);
      break;
    case O::kWeaponSelect:  // the next weapon, the group's next every sixth press (next only cycles a group)
      press(act_ % 6 ? "CMD_WEP_NEXT" : "CMD_WEPGRP_NEXT");
      break;
    case O::kWeaponGroupSelect:
      if (auto* w = dynamic_cast<const mech3::WeaponObjective*>(o)) {
        char cmd[16];
        std::snprintf(cmd, sizeof cmd, "CMD_WEPGRP_%d", w->id + 1);
        press(cmd);
      }
      break;
    case O::kHeatWarning:
      press("CMD_ALPHASTRIKE");
      break;
    case O::kReachVelocity:
      if (auto* v = dynamic_cast<const mech3::ReachVelocityObjective*>(o))
        v->test <= 1 ? press("CMD_THROTTLE_UP", 18) : press("CMD_THROTTLE0");  // held: the throttle climbs while it is down
      break;
    case O::kFaceObject:
    case O::kViewObject:
    case O::kTarget:
    case O::kDestroy: {
      auto* d = dynamic_cast<const mech3::DestroyObjective*>(o);
      float at[3];
      if (!d || !d->name || !named_at(d->name->c_str(), at)) break;
      if (o->kind == O::kDestroy) {
        ++hits_;
        mech3::chain_hit(mech3::chain_entity_named(d->name->c_str()), gun);
        break;
      }
      mech3::chain_player_face(at);
      // Q on what the reticle's centre line meets; faced level at its
      // origin, the line can pass between a mech's legs (t1m2's bushwacker_02
      // standing face-on since the AI's real waits, W34), so every other try
      // is the next enemy (E), as a player would.
      if (o->kind == O::kTarget) press(act_ % 2 ? "CMD_TARGET_CURSOR" : "CMD_TARGET_NEXT_E");
      break;
    }
    case O::kAimAt:
      if (auto* a = dynamic_cast<const mech3::AimAtObjective*>(o)) {
        float at[3];
        if (auto* n = static_cast<zcls::Node*>(e_.object_of(a->node))) zcls::node_world_position(n, at), mech3::chain_player_face(at);
      }
      break;
    case O::kReachArea:
      if (auto* r = dynamic_cast<const mech3::ReachAreaObjective*>(o)) {
        float at[3] = {r->point[0], r->point[1], r->point[2]};
        if (r->nav) mech3::program_waypoint_position(e_.object_of(r->nav), at);
        else if (r->node) zcls::node_world_position(static_cast<zcls::Node*>(e_.object_of(r->node)), at);
        mech3::chain_player_at(at);
      }
      break;
    case O::kValveChange:
      if (auto* v = dynamic_cast<const mech3::ValveChangeObjective*>(o)) {
        const auto* x = static_cast<const mech3::ai::Valve*>(e_.object_of(v->watch));
        const char* name = x && x->name ? x->name : "";
        char target[32] = {};
        if (mech3::chain_player_to_region(("reg_" + std::string(name)).c_str())) break;
        Trigger t;  // an AI record's trigger
        if (valve_trigger(name, &t)) {
          if (act_ == 1)
            std::printf("mw3: modes training lesson %d: valve %s from %s's trigger (%s%s%s%s)\n", lesson_ + 1, name,
                        t.record.c_str(), t.region.c_str(), t.inside ? ", the player inside" : "",
                        t.hit ? ", its mech hit" : "", t.fired ? ", a weapon fired" : "");
          if (t.inside && mech3::chain_player_to_region(t.region.c_str())) break;
          if (t.hit) {  // one hit on the record's entity, not a kill
            if (act_ % 5 == 1) ++hits_, mech3::chain_hit(mech3::chain_entity_named(t.record.c_str()), gun);
            break;
          }
          if (t.fired) {
            press("CMD_TRIGGER1");
            break;
          }
        }
        if (std::sscanf(name, "%31[a-z0-9]_destroyed", target) == 1) {  // targ01_destroyed: the target shot
          ++hits_;
          mech3::chain_hit(mech3::chain_entity_named(target), gun);
          break;
        }
        if (act_ == 1) std::printf("mw3: modes training lesson %d: valve %s, nothing to do for it\n", lesson_ + 1, name);
      }
      break;
    default:
      break;
  }
}

}  // namespace sdlfe
