#pragma once
#include <cstdint>
#include <initializer_list>

#include <string>
#include <vector>

#include "gamez/zgame/unported.h"

// The mech build's and the launch's hooks wired to the program (app_mech.cpp,
// docs/mech-build.md section 9). Not original code. The engine's side comes
// as plain functions, so this header has neither of the two Handle types
// (app_hooks.h's numbers, mech.h's pointers).
namespace mech3 {
namespace ai { struct AiTarget; }

struct MechWiring {
  void* engine = nullptr;
  uint32_t (*unported)(void* e, const char* key, std::initializer_list<zgame::Arg> args) = nullptr;
  uint32_t (*peek32)(void* e, uint32_t a) = nullptr;
  void (*poke32)(void* e, uint32_t a, uint32_t v) = nullptr;
  void (*poke8)(void* e, uint32_t a, uint8_t v) = nullptr;
  void* (*object_of)(void* e, uint32_t h) = nullptr;
  uint32_t (*handle_of)(void* e, const void* p) = nullptr;
  uint32_t (*vcall)(void* e, uint32_t obj, uint32_t slot, std::initializer_list<zgame::Arg> args) = nullptr;
  uint32_t (*pilot)(void* e) = nullptr;   // [0x0060861c]
  uint32_t (*view)(void* e) = nullptr;    // [0x005f4c8c]
  void (*forget_object)(void* e, const void* p) = nullptr;  // an object freed: its handle forgotten (FIX (A))
};
void wire_mech_build(const MechWiring* w);  // null: the hooks' defaults again
void net_start();  // 0x0046ceb0 (mech_net.cpp): the mission's network start
void net_end();    // 0x0046d100: its end
void net_set_game_clear(void (*fn)());  // the start's clear of the network game's +0x7c..+0x84
void net_set_lobby_left(void (*fn)(uint32_t));
// The network game's part in the launch (0x0046da80): its slot 0's loadout
// and skin, its unlimited ammo (+0x8d) and outside view (+0x8e).
struct Config;
struct NetLaunchGame {
  Config* loadout = nullptr;
  int32_t skin = 0;
  bool unlimited_ammo = false, outside_view = false;
};
void net_set_launch_game(bool (*fn)(NetLaunchGame* out));
// The network game's fields and the mission's clock for the sends (0x0046ef70).
struct NetGameAccess {
  bool (*fields)(int32_t* players, float** clock_next, float** end_time, int32_t** end_state, int32_t** kills,
                 int32_t** deaths, int32_t* score_limit, uint8_t** flags) = nullptr;
  float (*game_time)() = nullptr;   // [0x0060991c]
  float (*clock_left)() = nullptr;  // [0x00609920]
  bool (*clock_on)() = nullptr;     // [0x00609924]
  void (*set_clock_left)(float v) = nullptr;  // [0x00609920] =
  void (*exit_fade)() = nullptr;    // 0x0048ef00 to the Multi-Exit State
};
void net_set_game_access(const NetGameAccess& a);
// Ours, for the tests (multiplayer-port.md piece 5c): in the mission that is
// up, a network game over the in-process hub; another player joins with the
// pilot's own mech type and loadout, sends its first state 30 m east of the
// pilot, then leaves; then the end. What was seen of its mech.
struct NetMechSeen {
  int32_t made = 0, remote = 0, collides = 1, torso_mode = -1, in_world = 0, configs = 0, config_named = 0;
  int32_t moved = 0, after_leave = -1, handlers_after = -1;
  int32_t state_sent = 0;  // the pilot's state (0xf) reached the other player, at the pilot's place
  int32_t followed = 0;    // a second state 10 m on (to be taken at once, flag bit 0) and a frame: the mech there
  int32_t respawned = 0;   // dead (inactive), a 0x12 20 m on: made again in place, active, there, still remote
};
bool program_net_mech_check(NetMechSeen* out);
// Ours, for the tests (piece 8c): another player over the hub, with its
// network mech made, sends what a hostile or broken peer could. Each count
// is what reached the handler's next step.
struct NetHostileSeen {
  int32_t made = 0;
  int32_t craters_bad = -1, craters_good = -1;   // a ground past the table; ground 0
  int32_t effects_bad = -1, effect_good = -1;    // nodes -5, past the pool, free, a connector's -7; node -1
  int32_t name_ended = 0;                        // a 32-letter name with no NUL: ended at [0x1f]
  int32_t fires = -1;                            // a state saying 127 shots, holding 4
  int32_t actions_queued = -1;                   // postures 1000 and -3
  int32_t fuzzed = 0;                            // random messages of every routed type, any length, sent
};
bool program_net_hostile_check(NetHostileSeen* out);
// Ours, for the tests (piece 8): a network game started over the hub in the
// mission that is up, then its end (0x004707c0). Whether the game was up.
bool program_net_game_over();
// Ours, for the tests (piece 14): the chat panel on the pilot's mech in the
// network game that is up over the hub: CHAT ALL typed and sent, a peer's
// line, an empty one, CHAT TEAM outside team play; what was seen.
struct NetChatSeen {
  int32_t opened = 0, prompt_all = 0, commands_off = 0, closed = 0, commands_back = 0;
  int32_t sent = 0;            // the peer heard 0x1f, flags 0, our text
  int32_t empty_sent = 0;      // an empty line sent anything
  int32_t team_prompt_all = 0; // CHAT TEAM without team play is to all
  int32_t hover_kept = 0;      // the mouse's entry (+0x3c) left the panel up, nothing sent
  std::string own, peer;       // the HUD's lines
};
bool program_net_chat_check(NetChatSeen* out);
// The 1.1 read-me's "crashes or loses keyboard control when one player
// respawns while another has the chat window open" (14c's checks): with our
// panel open Bob's mech is made again (0x12), then ours is (the respawn's
// remake); after each the keys type and the commands come back.
struct NetChatRespawnSeen {
  int32_t peer_rebuilt = 0;       // Bob's mech made again while our panel was up
  int32_t open_after_peer = 0;    // the panel still up, our text sent after it
  int32_t sent_after_peer = 0;
  int32_t commands_after_peer = 0;  // the game's commands back at its close
  int32_t own_closed = 0;         // our mech's remake closed the open panel
  int32_t commands_after_own = 0; // and gave the commands back
  int32_t reopened = 0, sent_after_own = 0;  // a new panel on the new mech sends
};
bool program_net_chat_respawn_check(NetChatRespawnSeen* out);
// The pilot's chat panel for the tests: opened (CHAT_ALL), its line after
// the prompt, closed. False / "" without a mech or a panel.
// The 1.2 read-me's "weapon groupings are saved after a player dies and
// respawns": a weapon moved to another group, the pilot respawned by the
// network game's respawn (the dead list's, 0x0046dde0), its group after.
struct NetGroupsSeen {
  int32_t moved_from = -1, moved_to = -1, after = -1;  // weapon 0's group
  int32_t other_before = -1, other_after = -1;         // weapon 1's, left alone
  int32_t respawned = 0;
};
bool program_net_groups_check(NetGroupsSeen* out);
// The 1.2 read-me's "arms no longer flail" and "no longer run in place on
// other players' screens": Bob's mech from his 0xf states, a frame at a
// time (call each Core frame from 0): standing (frames 1..60, his place,
// no throttle, his aim held), walking at 10 m/s (61..150, throttle 0.6),
// standing again (151..240). Each phase after its settling frames (40, 15,
// 70): frames at stand (slot 2) and at any other gait, his arms' largest
// aim change, how far he moved. First his ping answer with a delay past
// our round trip (the latency's FIX (bug): 0, not 2^32 ms).
struct NetPoseSeen {
  struct Phase {
    int32_t frames = 0, stand = 0, walking = 0;  // frames at stand, at any other gait
    float aim_spread = 0;    // the largest change of an arm's aim pitch or yaw from the phase's first
    float aim_jump = 0;      // and from one frame to the next (a flail's jerk)
    float moved = 0;         // from the phase's first place
  } phase[3];
  float latency = -1;  // his after a ping answer with his delay past our round trip
  bool ready = false;
};
bool program_net_pose_step(int32_t frame, NetPoseSeen* out);
// The player's death in a mission (14's census items): the pilot's zoom
// window locked open (keypad 0), a weapon destroyed (0x004b3020: its name
// and "destroyed" voices, 0x004e6dc0), the mech killed (the head): the HUD
// off and the window closed (0x0041b263..0x0041b30c).
struct PlayerDeathSeen {
  int32_t hud_before = -1, hud_after = -1;          // the HUD's active
  int32_t locked_before = -1, locked_after = -1;    // the zoom window's lock (+0x174)
  int32_t hidden_after = -1;                        // its hidden bit (+0xc bit 4)
  int32_t voices_wanted = 0, voices_started = 0;    // the destroyed weapon's voices, the voices that began
  int32_t killed = 0;
};
bool program_player_death_check(PlayerDeathSeen* out);
// The kill's score in a network game (0x00470170's 0x00446ad0): with the
// view's mech's objectives box shown, it is filled again (the header
// "Network Game Scores"); hidden, it is left alone.
struct NetScoresBoxSeen {
  int32_t refilled_open = 0;    // lines again after the kill, the box shown
  std::string header;           // its first line
  int32_t refilled_closed = -1; // lines after a kill with the box hidden
};
bool program_net_scores_box_check(NetScoresBoxSeen* out);
bool program_chat_open();
std::string program_chat_line();
void program_chat_close();
// Ours, for the soak test until the lobby (piece 11): each player its own
// team, its start by id among the session's players, and the kills and
// deaths kept here as the lobby would; the pilot driven by a pattern (the
// commands it injects); a line of where everything is; the score table.
void program_net_debug_lobby();
// The lobby's records as the network game's (piece 11c; 0x00435910..
// 0x004361e0 on the pilot's multiplayer game, 0x00495b10's +0x18), set by
// the lobby's START in place of the stand-in above.
struct NetLobbyAccess {
  uint32_t (*team_of)(uint32_t player) = nullptr;    // 0x00435910
  int32_t (*start_of)(uint32_t player) = nullptr;    // the record's +0x20
  void (*left)(uint32_t player) = nullptr;           // the record's +0x18 = 1
  void (*suicide)(uint32_t player) = nullptr;        // 0x00435ac0(p, 1)
  void (*kill)(uint32_t player) = nullptr;           // 0x004359b0(p, 1)
  int32_t (*kills)(uint32_t player) = nullptr;       // 0x00435a30
  int32_t (*score)(uint32_t player) = nullptr;       // 0x00435a50: toward the limit
  int32_t (*deaths)(uint32_t player) = nullptr;      // 0x00435b40
  int32_t (*scores)(uint32_t* out, int32_t cap) = nullptr;  // (id, kills, deaths) each
  void (*set_kills)(uint32_t player, uint32_t n) = nullptr;   // 0x004359f0
  void (*set_deaths)(uint32_t player, uint32_t n) = nullptr;  // 0x00435b00
  void (*rescore)() = nullptr;                       // 0x004361e0
  uint32_t (*team_id)(uint32_t player) = nullptr;    // 0x00495b60: its team, 0 none
};
void program_net_lobby(const NetLobbyAccess& a);
// 0x0041ab90 on the pilot's mech: its chat panel drawn (mech3/chat_panel).
void program_mech_chat_draw(void* mech);
// The tests' look at what the network game asks (NetCalls' team_of, start_of
// and score; 0 when unset).
uint32_t program_net_team_of(uint32_t player);
int32_t program_net_start_of(uint32_t player);
int32_t program_net_score_of(uint32_t player);
// [0x00609920] and [0x00609924]: the mission clock START sets (0x0049b790).
void program_net_clock(float left, bool on);
uint32_t program_net_drive(int cmd, double seconds, uint32_t seed);
std::string program_net_soak_line();
// A kill staged on the pilot (hits that land are too few to kill in a soak):
// its head destroyed, the nearest remote mech within 300 m its attacker
// (else none: a suicide). False when it is not alive.
bool program_net_staged_kill();
// The pilot held `metres` from the nearest remote mech in play, facing it
// (a damage check at a known range); false without one.
bool program_net_hold_near(float metres);
// The pilot's mech made again as `roster` in place (0x004783c0; the
// respawn's, and the exe's mech change).
void program_pilot_set_mech(int32_t roster);
bool program_net_wreck_fades();  // the dead list's net_fade: ALLOW RESPAWN's (the tests)
// The mission's loss in a network game (0x0048f9e0): a mech's +0x578 (its
// animation state, 10 down; -1 not a mech), its part by id (0x0041d510),
// and the part's vt+0x3c (set_internal).
int32_t program_mech_anim_state(const void* mech);
void* program_mech_part(void* mech, int32_t id);
void program_part_set_internal(void* part, float v, int32_t section);
void program_hud_scores_refill();
std::string program_hud_objectives_text();  // app_hud.cpp: the shown objectives box's lines, " | " between ("" hidden)  // app_hud.cpp: net_score_kill's 0x00446ad0 on the view's mech's open box
int32_t program_net_mech_count();  // [0x00605f88]: the remote mechs in the network game
// Ours (W40's probe): the pilot's weapons' trigger (the manager's +0x00)
// and the sum of their reloads (a shot raises one); false without a mech.
bool program_pilot_fire_probe(int32_t* trigger, float* reload_sum);
bool program_net_running();  // a network game is up (net_start ran, net_end not yet)
void program_objectives_again();  // app_mission.cpp: Missions::objectives_again (0x00491050)
void program_options_apply();     // app_mission.cpp: every option set again (0x0040cb30)
// For the tests: the pilot's mech alive (active, not dead), its health and place.
bool program_pilot_alive(float* health, float pos[3]);
std::string program_net_scores();
// The pocket commander's program side (app_pocket.cpp), from wire_mech_build;
// the marker a handle names, if it is the mission's.
void wire_pocket(const MechWiring* w);
struct PocketMarker;
PocketMarker* program_pocket_marker(uint32_t h);
bool program_pocket_marker_end(uint32_t h);  // the mission's end: its caster freed
// The weapons' hooks (app_weapons.cpp): the manager's, zWeapon's, the
// queries; and a mech's damage handler (0x005833b0 with 0x004161a0).
void wire_weapons(uint32_t (*unported)(const char* key));  // what is not ported, logged once
void unwire_weapons();
void sync_frame_step(float dt, float clock);  // [0x0072c52c] and [0x0072c530] into the modules' copies
// 0x00478f10 on the pilot the pilot screen chose (app_menus.cpp); false when none is.
bool program_record_mission(bool next);
bool program_pilot_file(int op);  // the current pilot's file: 0 open (0x00478ce0), 1 the header (0x004794f0), 2 the record (vt+0x04), 3 closed
int32_t mech_select_hot_group(void* mech);
int32_t mech_current_group(void* mech);  // the manager's +0x28, -1 none
bool mech_select_group(void* mech, int32_t g);
// Tests only: the larger of the two arms' recoil (+0x34, 0x00466610's kick), 0 without arms.
float mech_arm_recoil(void* mech);
// Tests only: the ammo left in group g's weapons (vt+0x14), summed.
int32_t mech_group_ammo(void* mech, int32_t g);
// Tests only: the least of group g's weapons' ready fraction (vt+0x34, the HUD's bar: a beam's charge).
float mech_group_ready(void* mech, int32_t g);
// Tests only: group g's first weapon's muzzle and direction (+0x40, +0x4c) from its last aim.
bool mech_group_muzzle(void* mech, int32_t g, float muzzle[3], float dir[3]);
// Tests only: group g's first weapon as a beam: firing, its beam shown, the
// origin the beam draws from and the firepoint's place now, the ready
// fraction; the manager's trigger (+0x00, +0x01) and state.
struct BeamProbe {
  bool firing = false, beam_active = false, trigger_down = false, trigger_was = false;
  int32_t state = 0;
  float ready = 0, origin[3] = {}, live[3] = {};
};
bool mech_group_beam(void* mech, int32_t g, BeamProbe* out);
std::string mech_group_names(void* mech, int32_t g);
// Tests only: the HUD's lock on the manager (+0x88), and the object of the target selected (+0x6c).
bool mech_lock_shown(void* mech);
void* mech_target_object(void* mech);
std::vector<std::string>& weapon_fx_log();
// Ours: what the mechs' parts played ("step <sound>", "print <effect>", "wake <level>").
std::vector<std::string>& mech_parts_fx_log();  // the first 4,096
// For the program's tests: hits the mechs' damage handler took (and the last
// shooter node), a mech's node, whether a group's weapons are firing or reloading.
int32_t mech_hits_taken(void** attacker_node);
void* mech_node(void* mech);
bool mech_group_reloading(void* mech, int32_t group);
// For the tests (playtest W18): a mech's AMS (fitted, on, rounds; false without
// one), switched on or off (made as the loadout makes it when it has none), and
// n shots of `weapon` fired at it from `dist` m away, hostile to the player.
bool program_mech_ams(void* mech, int32_t* fitted, bool* on, int32_t* rounds);
void program_mech_ams_set(void* mech, bool on);
int32_t program_fire_at(void* mech, const char* weapon, int32_t n, float dist);
void mech_after_tick(void* mech, uint32_t (*unported)(const char*));  // 0x0041aba0  // a group whose weapons heat (as F1..F5 would), -1 none  // [0x0072c52c] into the mech, weapon and zWeapon modules
void mech_set_damage_handler(void* mech);

// The mech types (mech_lib.h, mech_data.h) for the engine and the mission:
// mechlib opened with its voices (0x00484270), the cached types' sounds and
// effects made again for a new mission (0x004319a0), mechlib closed and the
// definitions freed (0x004844b0).
void mech_types_open();
void mech_types_refresh();
void mech_types_close();
// As a new process starts: no mech type cached (the exe clears them only at
// a mission's unload, 0x004318e0 in 0x00490480, which the program has not).
void mech_types_forget();
void mech_canned_nodes_free();
void mission_lose();
void mission_win();  // Missions::complete 0x0048f740 (the cheats')
void mission_controls_free(void* controls);  // app_mission.cpp: an AI mech's controls (and brain) deleted
// A mech (one on zEntity's list) deleted (vtable +0 with 1); false for anything else.
bool mech_free(void* obj);
void mech_dead_tick(float dt);  // 0x0041e3f0, the player tick's: the dead mechs' frame
void mech_dead_clear();         // 0x00414000's list: emptied at the mission's cleanup
void mechs_shutdown();          // 0x00414000: the mechs' mission end (app_mission's cleanup)
// For the tests: the radar blips made, a motion loading from the archive, the scripts' path made.
void mechs_end_state(bool* blips, bool* motion, bool* path);
void mechs_make_blips();  // for the tests: the radar's first need (app_sensors.cpp)
void* mech_last_attacker(const void* mech);  // for the tests: +0x664, the last to hit it
struct UnbindCalls;
const UnbindCalls& entity_unbind_calls();  // 0x0040ed00's: a part's mech (0x00460000); targets bind their own
// 0x004782c0(0): the pilot's mech let go: its fire mode out (false without
// weapons), the mech globals cleared.
bool mech_player_release(void* mech, int32_t* fire_mode);
void* mission_player_mech();  // for the tests: world().mission_player ([0x0060861c] +0x14's mech)
int32_t mechs_alive();  // mech_build.cpp: made and not yet deleted
void program_motions_open();  // motion.zbd opened (0x005a9820), as the mechs' set-up does
void program_check_cheats();  // app_mission.cpp: Missions::check_cheats (0x00491d20)
class MultiplayerGame* mission_multiplayer_game();
std::vector<std::string> mission_multiplayer_score_lines();  // app_mission.cpp: 0x004361e0, then 0x00435b60(0x14, 0xc)'s lines  // app_mission.cpp: 0x00479480, the pilot's network game (a debug start's), else null
int mission_game_type();  // app_mission.cpp: the pilot's game's vt+0x2c (gametypes.md)  // app_mission.cpp: Missions::lose (0x0048f9e0), the player's mech destroyed  // 0x00431800: the canned effects' node copies

// What the last launch built (0x00491150), for the program's log and its tests.
struct LaunchedMech {
  int32_t roster = -1;     // the type
  int32_t parts = 0;       // hip, torso, head, arms, hands, legs, calves, feet with their nodes
  int32_t weapons = 0;     // mounted (the manager's count)
  int32_t sinks = 0;       // the heat sinks
  int32_t toes = 0;        // both feet's
  float height = 0;        // from the model's box
  float max_speed = 0;     // m/s, from the engine
  float torso_armor_scale = 0;  // the centre torso's 100 / armor points
  uint32_t team = 0;       // the AiTarget mask
  bool skis = false, ams = false;
  bool motions = false;    // the walk and its blends copied from the type's motions
  bool powering_down = false;  // asked to power down (0x0041cc30), or down
  bool powered_down = false;   // +0xde
  float pos[3] = {};       // the node's translation
  float yaw_sin = 0;       // the orientation's y (sin of half the yaw)
  bool in_world = false;   // the node's parent is the world node
  bool feet_down = false;  // both skis on the ground
  float armor = 0;         // armor and structure left, summed over the parts (fractions)
  float heat = 0;          // +0x554
  int32_t group = -1;      // the manager's selected group (+0x28), -1 without one
  const void* weapon = nullptr;  // that group's current weapon (+0x10)
  const void* config = nullptr;  // +0x3ec  the loadout it was given (0x00414430), a Config
  int32_t jets = 0;        // jump jets installed (equipment[11])
  int32_t jets_firing = 0; // +0x580
  bool jet_flame = false;  // +0x620: the jets' flame effect started (0x00432830)
  bool crouched = false;   // +0x586
  bool invincible = false, throttle_returns = false, torso_absolute = false;  // +0x630, controls +0x3f, torso +0xa0
  float alive = 0;         // +0x414  seconds of full updates
  int32_t anim_state = 0;  // +0x578
  bool blocked[4] = {};    // +0x374  the last collide: vehicle, mech or bump, ceiling, wall or touch
  const void* attacker = nullptr;  // the last mech to hit it
  int32_t action = 0;      // the action queue's front (0 none; 7, 8 the slow death falls)
  bool deadmech = false;   // the node renamed "deadmech" (the death)
  bool aim_torso = false;  // zEntity's target node (+0x10) is the torso's (0x0041d130)
  uint32_t kills[6] = {};  // the kill credit (+0x3f4): mechs, vehicles, buildings, then their points
  int32_t debris = 0;      // nodes of parts that flew off (+0x6d0)
  bool debris_in_world = false;  // each of them under the world node
  uint32_t detail = 0;     // +0x688  the level of detail's bits (mech_lod.cpp)
  uint32_t lod = 0;        // +0x690  0 the full model, 1 the low-detail one
  bool dead_listed = false;  // on the dead mechs' list (0x00605510)
};
bool launched_mech(LaunchedMech* out);  // false when the launch made none
bool describe_mech(const void* mech, LaunchedMech* out);  // the same of any mech (a Mech*)
void mech_kill(void* mech, void* killer = nullptr);  // for the tests: its centre torso destroyed (Destroy(0, 1), 0x00465f40), `killer` its last attacker
int32_t mission_salvage_items();
void mech_destroy_part(void* mech, int32_t id);  // for the tests: Mech::DestroyPart (0x0041cfa0)  // for the tests: the items in the mission's salvage store
int32_t canned_nodes_count();  // for the tests: the canned effects' node copies (the eject seats)

// player.cpp's AI mechs (mission-start.md section 4): 0x00414150 on a
// controller the program made (a MechControls), the definition's skin
// (0x00414390), the mech's definition (0x0041e9a0). Pointers as void*.
void* build_ai_mech(int32_t mech, void* controls, int32_t config, const char* name);
bool set_mech_skin(void* mech, void* skin);
// The launch's lancemates (player.cpp 0x0049e5e0): an AI mech's loadout
// (0x00414430), its roster number, and the player's mech's own net
// (vt+0xd8, 0x004d70f0) as an ai::AiNet.
bool program_mech_configure(void* mech, void* config);
int32_t program_mech_roster(const void* mech);
void* program_mech_own_net(void* mech, uint32_t self);
void program_launch_lancemates();  // app_mission.cpp: player.cpp's 0x0049e5e0
void program_action_lineup(bool launch);  // app_mission.cpp: player.cpp's 0x0049dc80
void program_objectives_start();   // app_mission.cpp: 0x0045c4a0 on the mission's objectives
// 0x00491120: a mech's controls (+0x37c), the controls told their owner.
void program_mech_set_controls(void* mech, void* controls);
// 0x005a85e0: a vehicle's centroid (its node's world position); false without one.
bool program_vehicle_centre(void* vehicle, float out[3]);
void* program_launch_campaign();   // app_mission.cpp: the launch's CampaignGame (the menus' or the debug start's)
const void* mech_definition(void* mech);
void mech_power(void* mech, int32_t down);       // 0x0041cc30 (Mech::power)
// For the tests: knocked down (Mech::knocked_down: 3 back, 4 front) and asked to get up (0x0041b840).
void mech_knock_down(void* mech, int32_t kind);
void mech_get_up(void* mech);
int32_t place_mech(void* mech, const float pos[3], uint32_t yaw);  // 0x004161c0 (Mech::place)
void move_mech(void* mech, const float pos[3], float yaw);         // vtable +0xdc, 0x00418f30 (Mech::set_place)
int32_t spawn_updates();                                           // the spawn's update steps run so far
const float* spawn_position();                                     // the placed mech's position before its last step
bool is_launched_mech(const void* mech);                           // the pilot's mech, the port's
void update_mech(void* mech, float dt);
void update_entities(float dt);                                   // 0x004a1140: every other entity's frame
void* node_owner(void* node);                                     // 0x0040e9d0 (app_weapons.cpp)
void* node_entity(void* node);                                    // 0x0040e920 (app_weapons.cpp)
struct MechControls;
MechControls* mech_controls(void* mech);  // its +0x37c (vt+0x140's IControls)
bool entity_is_mech(const void* o);  // a program mech (zEntity's entry answering vt+0x50 with itself); others and freed ones not
void mech_entities_forget();         // the mechs' registry emptied with zEntity's list
// 0x004814e0 on the program's entities: each one's projected shadow kept, drawn
// along the mission's sun (app_states' update_shadows is the handle version).
void update_mech_shadows();
void* mission_sun();                 // the mission's "sunlight" node ([0x006098d4] +4), or null
const void* mech_shadow(const void* mech);  // its caster (+0xcc, a zmod::ShadowCaster), for the tests
void mech_lock_warning(void* mech);  // 0x0041abd0: the view's mech hears it is locked on
bool mech_aim_at(void* mech, const float at[3]);                   // 0x0041bc20 (Mech::aim_at), as the HUD's reticle would (0x0041cd40)                            // vtable +0x34, 0x00419fc0 (Mech::update)
void mech_set_hud_fall(void* mech, int32_t v);                     // 0x0041ca30
uint32_t mech_allies(const void* mech);          // AiTarget +0xc4
// The AiTarget masks player.cpp writes as words (+0xc0 team, +0xc4 allies,
// +0xc8 enemies); false for any other offset.
bool mech_set_mask(void* mech, uint32_t off, uint32_t v);
void* mech_controls_of(void* mech);              // +0x37c (a MechControls*)
float mech_game_time();                          // [0x0060991c]
bool mech_world_position(void* mech, float out[3]);  // 0x005a85e0; false (0, 0, 0) without a node
const float* vehicle_position(void* vehicle);        // 0x005a85e0's pointer (a mech's or vehicle's node data); null without one
float mech_health(void* mech);                   // vt+0x98, 0x0041ba40
float mech_speed_at(void* mech, float share);    // vt+0x118, 0x0041d1c0
float mech_stopping_distance(void* mech, float speed);  // vt+0x120, 0x0041d210
float mech_stop_share(void* mech, float d);      // vt+0x12c, 0x0041d240
float mech_throttle_speed(void* mech);           // vt+0x114, 0x0041d1d0
void mech_forward(void* mech, float out[3]);     // +0x24, the flat forward
int32_t mech_jump_jets(void* mech);              // +0x3d8, the jump jets installed
float mech_vertical_speed(void* mech);           // +0x40, the local velocity's y
float mech_forward_speed(void* mech);            // +0x44, its z
void mech_set_local_velocity(void* mech, float x, float y, float z);  // +0x3c..+0x44
bool mech_airborne(void* mech);                  // vt+0x144, +0x584
float mech_jet_fuel(void* mech);                 // vt+0x148, +0x564
void program_mech_set_jet_fuel(void* mech, float fuel);  // for the tests: +0x564
// For the tests: each projectile a weapon spawns (Weapon::fire, 0x004b3450).
struct FiredShot {
  void* owner;
  const char* weapon;
  float muzzle[3], dir[3], target[3];
  int32_t salvo_left;
  bool locked;
  // The owner's torso, a mech's: its mode, the AI's aim on and its direction,
  // facing, yaw and limit, its forward; the mech's yaw (radians).
  int32_t torso_mode, torso_aim;
  float aim_dir[3], facing, torso_yaw, max_yaw, front[3], mech_yaw;
};
void program_watch_shots(void (*seen)(const FiredShot&));
// The manoeuvre's aim (0x004ca749): the lead point (0x0041d940) when the
// mech's selected target (+0x6c) sees `target`.
bool mech_lead_point_at(void* mech, uint32_t target, float out[3]);
// The AI firing's (docs/ai.md 17.7): the mech's weapon manager (+0xd4, a
// WeaponManager*), its current weapon (0x004b04c0), the aim cleared
// (0x004b0bc0(0)), an alpha strike (0x004b0a70); a weapon's type wait
// (0x004b4ba0), reload (+0x30), ammo (vt+0x14) and its type's flight hold
// (0x004b4c90); the torso recentred (0x00465a00). Null-safe.
void* mech_weapon_manager(void* mech);
int32_t wm_weapon_count(const void* wm);        // for the tests: +0x24
void* wm_weapon_node(const void* wm, int32_t i);  // its weapon i's node (+0x20)
const void* wm_weapon_def(const void* wm, int32_t i);  // its weapon i's definition (+0x2c)
// Ours (the campaign chain): a mech's or basic vehicle's weapon types, as its salvage reads them.
std::vector<int32_t> salvage_weapon_types(void* object);
void* wm_current_weapon(void* wm);
void wm_clear_aim(void* wm);
// The aim's (docs/ai.md 17.7): a targeting computer (+0x3e4) or a fire
// control system (+0x3b8) aboard; the manager aimed at a point (+0x88 set,
// then 0x004b0bc0).
bool mech_targeting_aid(void* mech);
// The volley pick (0x004b1860) for an AI mech's manager: its brain's +0x34
// and +0x90, its heat; 0 without a manager or a brain.
int32_t ai_volley_pick(void* wm, const float* dist2);
// The clear line's world (0x004d5980, docs/ai.md 17.7): a mech's node and
// centroid, a node's cached world position (redone when stale), its
// INTERSECT flag, and the world's first-hit segment query past `exclude`
// (the node: mech_node_of).
void* mech_centroid_of(void* mech);
void node_world_cached(void* node, float out[3]);
bool node_intersects(void* node);
void node_intersect_set(void* node, bool on);
int32_t world_segment_first(const float from[3], const float to[3], uint32_t exclude, uint8_t* count);
void wm_aim_at(void* wm, const float at[3]);
void wm_alpha_strike(void* wm, float seconds);
bool weapon_type_ready(void* w);
float weapon_reload(void* w);
int32_t weapon_ammo(void* w);
void weapon_hold_for_flight(void* w);
void mech_torso_centre(void* mech, int32_t how);
bool mech_active(void* mech);                    // +0x08
// The AI's aim and lead (docs/ai.md 17.7): the torso's limits (vt+0x8c,
// vt+0x90), its aim cosine (vt+0x150) and aim (vt+0x154), the last-hit aim
// point (0x0041d130), the velocity others lead it by (vt+0x08 0x0041d400),
// its matrix (+0x0c), its target node's position (0x005a8630); the
// manager's selected group's weapon, a weapon's node position (0x004ed530),
// speed (def +0x2c) and flags (def +0x6c). Null-safe.
bool mech_can_face(void* mech, const float dir[3]);
bool mech_clamp_aim(void* mech, const float dir[3], float out[3]);
bool mech_faces_within(void* mech, float cosine);
void mech_set_torso_aim(void* mech, const float dir[3]);
void mech_aim_at_last_hit(void* mech);
void mech_lead_velocity(void* mech, float out[3]);
void mech_matrix(void* mech, float m[9]);
void mech_target_position(void* mech, float out[3]);
void program_mech_forget(void* mech, uint32_t self, uint32_t x);
bool program_mech_lock_on(void* mech, void* target);                 // for the tests
std::vector<std::string> program_holders_of(uint32_t x, const void* gone);  // for the tests: who still holds it
void program_mech_set_attacker(void* mech, void* attacker);  // for the tests
void program_mech_wreck_gone(void* mech);                            // for the tests: 0x0041e140's delete  // vt+0xd4 0x004aeec0 (with the attacker FIX)
// The IA Dummy State's tally (0x0047ce80's loop over zEntity's list): each
// mech's kills (+0x3f4) to the Instant Action game's player (+0x4c), ally
// (+0x50) or enemy (+0x54) count, then cleared.
void program_action_tally();
// The training checks' reads (objectives.md, Training's types): a mech's
// target contact (+0x6c), aimed part (+0xf8), heat state (+0x69c) (its
// forward and weapon manager are mech_forward's and mech_weapon_manager's); a manager's current weapon's index
// (0x004b04c0, +0x28) and group (+0x28); a HUD's speed (+0x1ddc), the node
// its reticle's hit names (+0x144c, 0x00444330, +0x24), whether a line of
// its weapon list (+0x6520, 0x004464d0) shows that weapon recycling.
void* mech_target_contact(void* mech);
int32_t mech_aimed_part(void* mech);
uint32_t mech_heat_state(void* mech);
int32_t weapons_current_index(void* wm);
int32_t weapons_group(void* wm);
float hud_speed(void* hud);
void* hud_aim_node(void* hud);
bool hud_weapon_recycling(void* hud, int32_t weapon);
// Instant Action's game of the launch (the Action screen's, or a debug
// start's), null outside one; `kills` added to its field +0x4c, +0x50 or +0x54.
class ActionGame* mission_action_game();
void mission_action_add(int32_t field, int32_t kills);
bool program_mech_kill_credit(void* killer, void* victim);  // a mech killer's 0x0041b420; false: not a mech
bool program_entity_zones(const void* e, uint8_t out[4]);  // a mech's or vehicle's +0x64; false: neither
void* wm_group_weapon(void* wm);
void weapon_node_position(void* w, float out[3]);
float weapon_speed(void* w);
uint32_t weapon_flags(void* w);
bool mech_set_heat(void* mech, float heat);   // for the tests: +0x554
// vt+0x9c (0x0041ec90): a mech's heat (+0x554); 0 when it is no mech.
float mech_heat(const void* mech);
// For the tests: a mech's zones (ZEntity3d +0x64, count then ids) and its node's zone byte.
bool mech_zones(const void* mech, uint8_t out[4], int32_t* node_zone);
// For the tests: a hit of `damage` on the part with that id (0x00460970, no weapon).
bool mech_hit_part(void* mech, int32_t id, float damage);  // 6: the head
// For the tests: the throttle target set and held, as a throttle key held to v would.
bool mech_hold_throttle(void* mech, float v);
int32_t mech_controls_view(void* mech);
// For the tests: the world y of the mech's node, target node (+0x10) and
// centroid, and its height (0x005a8500 keeps the two markers' matrices).
bool mech_marker_heights(void* mech, float out[4]);
// For the tests: the textures on the mech's model (+0x694) and its low-detail
// copy (+0x698), by name (each textured polygon's), and its skin's (+0x3e8).
struct MechTextures {
  std::vector<std::string> names;  // the model's
  std::vector<std::string> lod;    // the low-detail copy's
  std::string skin;                // the mech's skin's name
  int loaded = 0;                  // polygons whose texture has an image
};
bool mech_textures(void* mech, MechTextures* out);
// For the tests: the mech's ceiling response (0x00423600) to `node` as the
// ceiling crossed; the head's and torso's structure after it (6 floats: head,
// torso sections 0..2, the hip's offset y, 1 when it had a box, the crouch depth).
bool mech_ceiling_hit(void* mech, void* node, float out[7]);  // for the tests: the controls' view state (+0x0c, 0x00426780)
float mech_throttle_target(void* mech);
// The cockpit frame's nodes (app_states' vehicle_node, cockpit_node, arm_node): the
// mech's node, its type's cockpit model (0x00430ad0) and an arm's (0 left, 1 right).
void* mech_node_of(void* mech);
void* mech_cockpit_node(void* mech);
void* mech_arm_node(void* mech, int side);  // for the tests: the controls' +0x44, 0 without
bool mech_powered_down(void* mech);              // vt+0xa0, 0x0041cc10
ai::AiTarget* mech_ai_target(void* mech);        // the Mech's AiTarget base
// 0x004b4ed0 on the mech's weapon manager (+0xd4): the ammo's value against
// its most, 1 before it has any; false without a manager.
bool mech_ammo_value(void* mech, float* out);
void mech_set_game_time(float t);
bool mech_get_mask(const void* mech, uint32_t off, uint32_t* v);
// The debriefing's lance (0x0047baf0's loop over zEntity's list): every
// mech, the mission's player's or not, its team, type and +0x3f4..+0x408.
class Store;
struct DebriefMech {
  void* mech = nullptr;
  bool player = false;
  uint32_t team = 0;
  int32_t type = -1;
  int32_t stats[6] = {};
};
std::vector<DebriefMech> mech_debrief_lance();
uint32_t mech_player_allies();
// 0x004aee70: an AI mech's name, its brain's vehicle record's pilot (AiParams
// +0xd4); none for the player's or a mech without a brain.
const char* mech_brain_name(void* mech);
// The mission's player mech (0x0060861c +0x14), for 0x0041df30's test.
void* mech_mission_player();
// A lance card's armor display's read (0x0047eba0, 0x0047ef10): the mech's
// type, its loadout's armor and its parts' armor left in the bars' order.
bool mech_lance_armor(void* mech, int32_t* type, int32_t armor[11], float left[11]);
// 0x0047ae10's won path on the ported mechs: the player's mech into `own`
// (the campaign's +0x64), its allies' into the mission's salvage store
// (mech_salvage.h).
void mech_lance_salvage(Store& own, bool team);  // the mission's player mech's +0xc4, 0 without one

// The AI mechs player.cpp built this mission (app_mission.cpp, our record of
// them for the log and the tests): each mech (a Mech*) and its controller (an
// AiControls*), in the order they were made.
struct AiMechMade {
  void* mech = nullptr;
  void* controls = nullptr;
  const char* name = nullptr;  // the aiv.zrd record's
};
const AiMechMade* mission_ai_mechs(int32_t* count);
// A mech freed: off the AI mechs made (their names, for the valves' lookups) and the player hooks' set.
void mission_ai_mech_forget(const void* mech, uint32_t handle);
// The mission's objectives as the program read them (mech_objective.h), and
// the hooks it gives them.
struct Objectives;
class ObjectiveHooks;
Objectives& mission_objectives();
void* mech_shade_of(void* mech);  // its sun shade (+0x68c, mech_shade.h), for the tests
ObjectiveHooks& mission_objective_hooks();
// The objectives box's sources (0x00446ad0): the missions object's +0x56 and
// the launch's game, and an objective's nav point's name (its +0x28).
uint8_t mission_aborting();
// The mission's map (mission_map.h): 0x00491c80 on the missions object (its
// +0xbc, else an empty map made once at +0xb8), a map by name (0x00458590) as
// a handle, and +0xbc set ([0x0060998c], the radar's Shift+R).
void* mission_map();
// The options' setters on the player's mech (options.cpp; mission-plan
// item 12): +0x630, the weapons' 0x004b0220, the torso's +0xa0, the
// controls' +0x3f, the control mode 0x0041cf30.
void program_mech_set_invincible(void* mech, bool on);
void wire_cheats(bool on);  // app_cheats.cpp: the cheat codes' calls (cheats.h)
// For the tests: the cheat box open on the mech, a line typed into it (each
// character, then Enter; "\x1b" alone is Escape), the mech's heat dissipation.
bool cheat_box_open(void* mech);
void cheat_box_type(void* mech, const char* text);
float mech_heat_dissipation(void* mech);
void* program_mech_weapons(void* mech);
void program_weapons_set_unlimited(void* weapons, bool on);
void program_mech_set_torso_absolute(void* mech, bool on);
void program_mech_set_throttle_returns(void* mech, bool on);
void program_mech_control_mode(void* mech, int32_t mode);
uint32_t mission_map_find(const char* name);
void mission_map_set(uint32_t map);
void* mission_map_of(uint32_t map);
int32_t mission_operation();
// Instant Action's header (hud.md 24): the game's vt+0x48 (0x004378b0: the world
// +0x74 as 1..4, else 0) and vt+0x0c (+0x3c, the scenario's name; null none).
int32_t mission_action_world();
const char* mission_action_name();
int32_t mission_number_of_game();
const char* mission_nav_name(uint32_t nav);

// The debug launch's slot (our addition, mission-load.md section 4): the
// mech by roster index, its first configuration, the definition's skin 0.
uint32_t debug_slot_config(int32_t mech, void* e, uint32_t (*handle_of)(void* e, const void* p));

}  // namespace mech3
