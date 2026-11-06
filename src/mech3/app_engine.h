#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "mech3/app_hooks.h"
#include "mech3/lab_view.h"
#include "mech3/options.h"
#include "platform/force_feedback.h"
#include "platform/vfw.h"
#include "platform/platform.h"

namespace zvid { struct Surface; }
namespace zfmv { class BlurAction; }
namespace zimg { struct Texture; }
namespace ztwod { class MessageBox; }

namespace platform {
class Render3D;
class SoundOutput;
}  // namespace platform

// The program's mech3::Engine (docs/program.md): every hook of the app and its
// states that has a port is wired to it, the Windows calls go to the platform
// (FIX (A), each named below), and the rest stay the base's unported() calls,
// logged once each by the original address. Our addition, not original code:
// it stands where the exe's other modules will be.
namespace mech3 {

// The CD's table of contents over the mounts: music/trackNN.* in the write dir
// or any mounted folder (CLAUDE.md, Music).
struct MountFiles : platform::Files {
  bool exists(const std::string& path) override;
};

struct Platform {
  platform::Window* window = nullptr;
  platform::Alert* alert = nullptr;
  platform::Clock* clock = nullptr;
  platform::Clock* movie_clock = nullptr;  // zFMV's timeGetTime (ours: the tests' fast one); none: `clock`
  platform::Input* input = nullptr;
  platform::ForceDevice* force = nullptr;  // FIX (A): IFORCE2's effects (none: no force feedback)
  platform::VideoForWindows* vfw = nullptr;  // FIX (A): the movies' AVIFile and Indeo 5 (none: the imported files)
  platform::SoundOutput* sound = nullptr;
  platform::MusicOutput* music = nullptr;
  platform::Files* files = nullptr;     // what music/trackNN.* the "disc" holds
  platform::Render3D* render = nullptr;
  platform::Screen2D* screen = nullptr;
  platform::Settings* settings = nullptr;  // the registry's stand-in (settings.md); none: nothing saved
  uint32_t ram_mb = 4096;               // the machine's memory, for detail.zrd's RAM_KB
  int window_width = 1280, window_height = 960;  // the window made for the 640x480 game
  // FIX (A): DirectPlay's instance (0x00586ba0) is our transport, made when
  // the multiplayer screens switch the network on and given up when off;
  // none: no network (the menus' MULTIPLAYER finds nothing).
  std::function<znet::Transport*()> network_open;
  std::function<void()> network_close;
  // Internet hosting's (ours, piece 9c): the router's UPnP as the screens
  // see it, hosting's mapping, the clipboard.
  std::function<Engine::NetReach()> network_reach;
  std::function<void(bool hosting)> network_hosting;
  std::function<void(const char* text)> copy_text;
};

class ActionGame;
class MultiplayerGame;
class GameEngine;
class MenuScreen;
class QuitBox;
class MenuState;
void wire_mission_hooks(GameEngine* e);  // mech3/mission's hooks to the ports (app_mission.cpp)
void wire_pilot_hooks(GameEngine* e);    // the pilot module's calls out (app_menus.cpp)
void wire_fmv(GameEngine* e);            // zFMV's host (app_fmv.cpp)

// The briefing screen open ([0x005f4c80]) hears the mission load's fraction
// (0x004050d0) and lines (0x00405120); none open, nothing (app_menus.cpp).
void program_briefing_progress(float f);
void program_briefing_line(const char* s);
void program_mech_chat_draw(void* mech);  // app_mech.h: 0x0041ab90's chat panel
uint32_t program_slot_count();  // the chosen pilot's campaign's slot count (0x00435180), 0 without one

class GameEngine : public Engine {
 public:
  void nav_prebuild() override;  // ours (W58, app_mission.cpp)
  // A mission started from the command line (our addition, docs/program.md):
  // the pilot's game answers these, and the load goes on to the Core state.
  struct Launch {
    int32_t index = 0;   // the mission directory (0x00484c50): c1 c2 c3 c4 c4b t1
    int32_t type = 1;    // the game type: 1 a campaign mission (gametypes.md)
    int32_t number = 1;  // the mission number (world.zrd's m<n>, the start animation's)
    int32_t mech = 9;    // the player's mech by roster index (9 the Mad Cat), its first configuration
    bool have_pos = false;  // the camera placed (--pos/--look): there is no mech to follow yet
    float eye[3] = {}, yaw = 0, pitch = 0;
    bool active = false;
    // A debug start goes on to the Core state as soon as the mission is
    // loaded (in place of the briefing's LAUNCH); from the menus the
    // briefing's LAUNCH does (campaign.md section 7, item 5).
    bool auto_launch = true;
    // The slot's loadout and skin (the pilot's game's; the Config is the
    // slot's own, alive while the pilot's game is), else the mech's first.
    Handle config = 0;
    int32_t skin = 0;
    int32_t op = 1;  // a campaign's operation (the game's vt+0x48)
    // The chosen pilot's handle stays in [0x0060861c] (a debug start puts a
    // stand-in there).
    bool keep_pilot = false;
    // An Instant Action start's game (type 2): the line-up, mode and wave the
    // Action screen would set (not ported: the caller makes it). Else the
    // chosen pilot's.
    ActionGame* action = nullptr;
    // A network debug start's game (type 3, ours: the lobby is not ported):
    // its rules and totals; the pilot's own when it has one.
    MultiplayerGame* multi = nullptr;
  };
  void set_launch(const Launch& l);
  // The launch's mech, loadout and skin read again from the chosen pilot's
  // game's slot; true when one changed (app_menus.cpp).
  bool launch_slot_refresh();
  const Launch& launch() const { return launch_; }
  void missions_start(uint32_t list) override;   // 0x00490880: mech3::missions().start()
  void missions_update(uint32_t list, float dt) override;  // 0x0048f470: mech3::missions().update()
  void missions_reset(uint32_t list) override;   // 0x00490720: only the mission number is kept
  void sensors_update() override;                // 0x004a8cd0: mech3::ai::sensors_update
  void entities_sort() override;                 // 0x004d5ca0: mech3::ai::entities_sort
  void ai_reset(uint32_t list) override;         // 0x004d8610 on one of the AI's four lists (zai_list)
  void ai_on() override;                         // 0x004ea740: mech3::ai::ai_switch_on
  // Core's resume(7) on the pilot's mech: the HUD's fall flag, then the
  // power-up (the spawn left it powered down).
  void mech_set_mode_a(Handle mech, int32_t a) override;  // 0x0041ca30
  void pilot_update(Handle p, float dt) override;  // 0x004783a0
  int32_t weapons_init() override;                // 0x00583750: zwep::startup
  void save_init() override;                      // 0x005a8b30: zsave::init
  void save_shutdown() override;                  // 0x005a8bb0: zsave::shutdown
  void anim_enable(int32_t on) override;          // 0x005090d0: the anim registrants on
  // The GfxFlags (app_mission.cpp; docs/render-lighting.md).
  int32_t models_init() override;
  int32_t render_init() override;
  void models_load(const char* flags_name, int32_t flags) override;
  void render_load(const char* flags_name, int32_t flags) override;
  void model_use_gfx_flags(const char* name, int32_t flags) override;
  void weapons_set_mode(int32_t v) override;      // 0x00583740
  void timer_update() override;                  // 0x00575a00, then the step's copies (sync_frame_step)
  void weapons_shutdown() override;               // 0x00583870: zwep::shutdown, no per-def callback (0x00584920)
  void mech_set_mode_b(Handle mech, int32_t a) override;  // 0x0041cc30 (Mech::power)
  void fade(int32_t a, int32_t b, int32_t c, uint32_t d, int32_t e, int32_t f, int32_t g) override;
  Handle pilot_game(Handle p) override;
  uint32_t pilot_campaign_number(Handle p) override;  // 0x00479420: the launch's mission
  // The briefing (mech3/briefing, campaign.md section 7, item 5): the Load
  // state's game from the pilot the pilot screen chose, the ported screen's
  // calls into the ported mission, launch, states and zSound.
  void load_state_enter() override;
  const char* game_text(int32_t slot) override;  // the pilot's game's vt+0x04 .. +0x10
  Handle pilot_campaign(Handle p) override;      // 0x00479440: the launch's game
  void mission_results(uint8_t a, uint8_t b) override;  // 0x0047ae10
  Handle mission_salvage_store() override;              // [0x00609990], the ported store
  uint8_t briefing_has_movie() override;                // 0x0040a820
  // The app's accessors the briefing's load reads (0x0040a7d0..0x0040a830) and
  // the two empty functions it calls on the app's +0x1e4 (item 10).
  uint32_t app_window() override { return hwnd_; }  // 0x0040a7d0: [app +0x20] +0x20, the frame's window
  Handle app_sounds() override;                      // 0x0040a800: the app's +0x1e4
  uint8_t app_flag_1c3() override;                   // 0x0040a830: the app's +0x1c3 (no briefing voices)
  void sounds_nop(Handle, uint32_t) override {}      // 0x004a5bc0: ret 4
  void sounds_load(Handle, const char*, int32_t) override {}  // 0x0040baf0: ret 8
  void briefing_play_movie(uint32_t state) override;    // 0x00409320
  void briefing_refresh(Handle s) override;             // 0x00408bd0 on the ported briefing
  void briefing_show(Handle s, int32_t a) override;     // 0x00404ae0
  // The mech lab (mech_lab.cpp): opened on the ported Lab State, its slots the
  // ported campaign's; no 3D world yet (mechlab.md item 9), so its load passes.
  void mech_lab_open(Handle store, int32_t a) override;
  void salvage_open() override;
  int32_t lab_slot_count(Handle pilot) override;
  Handle lab_slot(Handle pilot, int32_t i) override;
  void lab_slot_release(Handle pilot, int32_t i) override;
  Handle lab_slot_config(Handle pilot, int32_t i) override;
  void node_frame_update() override;                       // 0x004f7f70: zcls::frame_update
  // The lab's 3D view (lab_view.cpp, mechlab.md section 9).
  bool lab_world_load(Handle state) override;              // 0x0044ffc0
  void lab_world_unload(Handle state) override;            // 0x00450390
  void lab_view_begin(Handle state) override;              // 0x00450810
  void lab_view_end(Handle state) override;                // 0x00450800
  void lab_world_frame(Handle state, float dt) override;   // 0x00450690
  void lab_model_skin(Handle entry, int32_t skin) override;  // 0x0044fd90
  void lab_section_highlight(int32_t section) override;    // 0x004504d0
  zcls::Node* lab_model_node(Handle entry);                // 0x0044fd20
  void lab_place_window();                                 // 0x00450460 with 0x0044fea0
  void lab_clear();                                        // 0x00576cf0 in the lab's colour
  LabView* lab_view() { return lab_view_.get(); }
  LabScreen* lab_screen(Handle h) override;            // the ported screen (0x0044b920)
  void lab_show_slot(Handle state, int32_t slot) override;  // 0x00448f80, the ported Lab State's
  void lab_page(Handle screen, int32_t page) override;     // 0x0044ce30
  int32_t lab_slot_mech(Handle slot) override;
  void lab_slot_drop_config(Handle slot, Handle store) override;  // 0x00433df0 on the ported slot
  void lab_slot_set_mech(Handle slot, int32_t mech) override;
  // The chassis page's (step 3): the ported slots, loadouts and definitions.
  int32_t lab_slot_skin(Handle slot) override;
  void lab_slot_set_skin(Handle slot, int32_t skin) override;
  Handle lab_slot_cfg(Handle slot) override;
  int32_t config_mech(Handle config) override;
  uint8_t lab_config_modified(Handle config) override;
  std::string lab_config_name(Handle config) override;
  void lab_config_recount(Handle config) override;
  int32_t lab_config_slots_left(Handle config) override;
  int32_t lab_config_ferro_slots(Handle config) override;
  int32_t lab_config_endo_slots(Handle config) override;
  int32_t lab_config_sink_items(Handle config) override;
  int32_t lab_config_sink_limit(Handle config) override;
  float lab_config_weight(Handle config) override;
  int32_t lab_config_weapons(Handle config) override;
  int32_t lab_config_equipment(Handle config, int32_t kind, int32_t tech) override;
  int32_t lab_config_part_free(Handle config, int32_t part) override;
  float lab_config_armor_tons(Handle config) override;
  int32_t lab_config_armor_tech(Handle config) override;
  uint8_t lab_config_armor_ferro(Handle config) override;
  int32_t lab_config_section(Handle config, int32_t part) override;
  void lab_config_set_section(Handle config, int32_t part, int32_t v) override;
  void lab_config_spread_armor(Handle config) override;
  void lab_config_remove(Handle config, Handle item, int32_t n) override;
  void lab_store_add_equipment(Handle store, int32_t kind, int32_t tech, int32_t n) override;
  std::vector<LabFirepoint> lab_dfn_firepoints(Handle dfn) override;
  Config* lab_config_of(Handle config) override;
  void lab_message(const char* text) override;
  int32_t lab_assigned_part() override;
  void lab_put_on(LabUnassignedList& list, int32_t index) override;
  void lab_take_off(LabAssignedList& list, int32_t index) override;
  std::vector<Handle> lab_config_items(Handle config) override;
  int32_t lab_item_location(Handle item, int32_t mech) override;
  std::string lab_engine_name(Handle config) override;
  int32_t lab_config_engine(Handle config) override;
  void lab_config_set_engine(Handle config, int32_t index) override;
  void lab_config_set_xl(Handle config, uint8_t v) override;
  int32_t lab_config_engine_slots(Handle config) override;
  int32_t lab_dfn_engine_min(Handle dfn) override;
  int32_t lab_dfn_engine_max(Handle dfn) override;
  float lab_config_weight_of(Handle config, int32_t what) override;
  void lab_config_set_endo(Handle config, uint8_t v) override;
  int32_t lab_config_sinks(Handle config) override;
  uint8_t lab_config_double_sinks(Handle config) override;
  bool lab_config_valid(Handle config) override;
  Handle mech_dfn_handle(int32_t mech) override;
  void lab_dfn_skins(Handle dfn, std::vector<LabSkin>* out) override;
  uint32_t message_box2(int32_t a, int32_t caption, int32_t text, const char* s1, const char* s2) override {
    return message_box3(a, caption, text, s1, s2, nullptr);
  }
  void lab_page_part(Handle screen, Handle) override { lab_page_refresh(screen); }  // 0x0044cf40
  void lab_page_refresh(Handle screen) override;  // 0x0044cf50
  // The loadouts page's (step 4): the ported lists, loadouts and store.
  Handle lab_dfn_configs(Handle dfn) override;
  std::vector<Handle> lab_list_configs(Handle list) override;
  void lab_list_append(Handle list, Handle config) override;
  bool lab_list_remove(Handle list, Handle config) override;
  void lab_list_save(Handle list) override;
  Handle lab_config_copy(const char* name, Handle config) override;
  void lab_config_delete(Handle config) override;
  void lab_config_set_name(Handle config, const char* name) override;
  void lab_config_set_modified(Handle config, uint8_t v) override;
  uint8_t lab_config_byte(Handle config, uint32_t off) override;
  bool lab_store_has_config(Handle store, int32_t mech, Handle other, Handle config) override;
  uint8_t lab_store_full(Handle store) override;
  void lab_slot_set_config(Handle slot, Handle config, Handle store) override;
  // FIX (A): Ctrl+X / Ctrl+I without Windows' file dialogs (0x00426180,
  // 0x00426300): export\<mech>_<loadout>.m3c in the write dir, and the next
  // import\*.m3c (renamed *.m3c.done once read).
  void config_export(Handle config) override;
  Handle config_import() override;
  std::string message_format_sd(int32_t id, const char* s, int32_t n, bool has_n) override;
  std::string message_format_is(int32_t id, int32_t a, const char* s) override;
  std::string message_format_isi(int32_t id, int32_t a, const char* s, int32_t b) override;
  std::vector<LanceMech> debrief_lance() override;     // the ported mechs
  std::vector<Objective> debrief_objectives(int32_t which) override;  // the mission's lists
  const char* objective_text(Handle obj) override;     // 0x0045e080(0)
  uint32_t player_allies() override;
  bool vehicle_armor(Handle v, LanceArmor* a) override;
  const char* mech_name_of(int32_t mech) override;
  void win_keep_scores(Handle screen, int32_t n) override;  // into the ported campaign
  void briefing_script_start(Handle s) override;
  void briefing_init(Handle s) override;
  void briefing_start(Handle s) override;
  void briefing_map_image(ztwod::Dialog* screen, zrdr::Node* section) override;
  bool missions_loaded() override;
  bool mission_launch() override;
  void mission_cleanup(int32_t unload) override;
  void replace_state(int32_t n) override;
  Handle voice_play(Handle s, float v) override;
  int32_t voice_status(Handle v) override;
  void sound_cue_briefing(Handle s) override;
  uint8_t app_flag_1ba() override;
  Handle mission_navs() override;
  // The briefing map's waypoints (briefing.cpp, "waypoint"; item 12): the nav
  // point by label, its place on the mission's map, the map's image.
  Handle nav_find(Handle navs, const char* name) override;  // 0x00479be0 on the nav waynet
  Handle mission_map() override;                             // 0x00491c80
  Handle nav_position(Handle nav) override { return nav; }   // nav.vt+0x18: map_project reads the nav itself
  bool map_project(Handle map, Handle nav, int32_t xy[2]) override;  // 0x004586e0 at the nav's place
  zimg::Texture* map_image(Handle map) override;             // map +0x3c
  uint32_t vcall(Handle obj, uint32_t slot, std::initializer_list<Arg> args) override;
  // The cockpit view's frame on the program's objects (playtest #4).
  Handle vehicle_node(Handle veh) override;
  Handle cockpit_node(Handle veh) override;
  Handle arm_node(Handle veh, int32_t off) override;
  void node_set_active(Handle node, int32_t on) override;   // 0x004eb170
  void node_set_shadow(Handle node, int32_t on) override;   // 0x004eb6d0
  void view_draw_node(Handle cam, Handle node, Handle world) override;  // 0x004f73f0
  void view_reset(Handle view) override;  // 0x0040ad90, the port's on the program's view
  // The menu screens that are ported (app_menus.cpp): the shell states hold a
  // handle, a block of the module memory standing for the object, whose
  // offsets lead to its parts (the screen, its buttons).
  Handle construct(uint32_t ctor, std::initializer_list<Arg> args) override;
  void screen_refresh(Handle s) override;
  void screen_hide(Handle s) override;
  void twod_reset(Handle h) override;
  void twod_set_cursor(uint32_t v) override;  // 0x0059fb80: ztwod::set_page_flip
  void shell_clear(uint32_t state) override;  // 0x0048ee00: ShellState::clear_screen
  void push_state(Handle state, uint32_t arg) override;
  bool menu_vcall(Handle obj, uint32_t slot, std::initializer_list<Arg> args, uint32_t* out);
  MenuScreen* screen_of(Handle h, uint32_t* offset = nullptr) const;
  void delete_screens();  // the ones still open at exit, and the menu objects
  MenuState* menu_state_of(Handle h) const;
  MenuState* popup_state_of(Handle h) const;  // the controls screen's popups (+0x16760, +0x1677c)
  Handle adopt_screen(MenuScreen* screen, uint32_t size) override;
  Handle state_screen(Handle state) override;
  Handle joy_state() override;                 // 0x0051dce0: zin::joy_state
  float source_value(int32_t src, Handle joy) override;  // 0x0042fea0: mech3::source_value
  uint32_t message_box3(int32_t a, int32_t caption, int32_t text, const char* s1, const char* s2,
                        const char* s3) override;
  uint32_t message_box_plain(int32_t a, int32_t caption, const std::string& text) override;
  void credits_restart(Handle screen) override;
  void pilot_state_set_mode(Handle state, int32_t mode) override;
  void action_open(Handle screen) override;
  void action_tally() override;
  void ai_targets_shutdown() override {}  // 0x004d72a0 returns 0
  void pilot_chosen(Pilot* p) override;
  void pilot_record_mission(Handle p, int32_t a) override;
  void pilots_delete() override;
  void pilot_delete(const char* name) override;
  uint32_t message_box1(int32_t a, int32_t caption, int32_t text, const char* s) override;
  void sync_view();  // the player view's mode and target into its block (app_view.cpp)
  void set_mission_view(Handle view, Handle camera);  // the view 0x0040aca0 makes, and its camera
  Handle mission_camera() const { return camera_; }
  Handle alloc_block(uint32_t size);  // a block of the modules' memory (the peek/poke map)
  void video_clear_a(Handle rect) override;  // 0x00576ce0: the depth filled (0: all of it)
  bool video_opened() const { return video_opened_; }  // [0x0080271c]
  // The load screen's two lines (ours, for 0x005799f0 / 0x00579040's
  // DirectDraw driver and Direct3D device names).
  const char* video_driver_label() const;
  const char* video_mode_label();
  // 0x00579b70: the card's caps for blended and modulated textures (1, 2):
  // a modern GPU, through Render3D, has them all.
  uint32_t video_has_cap(int32_t) override { return 1; }
  void video_clear_b(Handle rect) override;
  void lists_update() override;  // 0x004f7d80: zcls::run_action_lists
  void classes_shutdown() override;   // 0x004f8f40: zcls::classes_reset
  int32_t effects_init() override;    // 0x0050d270: zEffect's table and animations started
  void effects_shutdown() override;   // 0x0050d2b0: freed
  void decals_set_mode(int32_t v) override;  // 0x00501f90
  void decals_shutdown() override;           // 0x00502020
  void missions_shutdown() override;  // 0x004844b0: mech3::close_mechlib
  std::function<void(int frame)> on_present;  // after each frame shown (the 2D or the 3D)
  bool drew_scene() const { return last_drew_scene_; }  // the frame just shown was the 3D scene
  // FIX (A): the frame just shown, when it went through the 3D path (the
  // scene, or the 2D layer over it), read back at the buffer's size; false
  // otherwise (the buffer is the frame then). The pause screen's grab.
  bool shown_frame(std::vector<uint16_t>* out);
  // Ours (playtest W25): a message box over a frame the GPU drew keeps that
  // frame at its own size behind the box (Render3D::hold_frame), the 16-bit
  // buffer then holding only the box over the layer's key; false when the
  // frame shown was the buffer's own. drop_held lets it go.
  bool hold_shown_frame();
  void drop_held();
  bool held() const { return held_; }
  int frames_presented = 0;

  GameEngine(const Platform& p, std::string command_line);
  ~GameEngine() override;

  // The not-ported calls: logged the first time each key is reached.
  uint32_t unported(const char* key, std::initializer_list<Arg> args) override;
  const std::map<std::string, uint32_t>& unported_counts() const { return counts_; }
  size_t handle_count() const { return objects_.size(); }  // the handles given out (kept for good)
  // Ours (mission-plan item 11): a reached key the program answers its own
  // way, or leaves by a decision, or that has no effect: why, with the
  // program's equivalent; null for a real gap. Counted all the same; not
  // logged, and listed apart by --verbose and the census.
  static const char* known_unported(const std::string& key);
  void print_unported_summary() const;
  bool quiet = false;  // no log lines (tests)
  int lobby_entries() const { return lobby_entries_; }  // ours: the lobby's page entered this many times

  // The memory of modules not ported: what is poked reads back.
  uint32_t peek32(uint32_t addr) override;
  uint8_t peek8(uint32_t addr) override;
  void poke32(uint32_t addr, uint32_t v) override;
  void poke8(uint32_t addr, uint8_t v) override;

  // Windows (FIX (A)).
  uint32_t lobby_connection() override { return 0; }
  // zNetwork (multiplayer-port.md piece 1): the session's checks and its end.
  uint32_t network_lobby_active() override { return znet::active() ? 1 : 0; }  // 0x00586ce0
  Handle lobby_session() override { return znet::local_id(); }                   // 0x00586d10
  void network_shutdown() override { znet::shutdown(); }                         // 0x00586c20
  const char* lobby_player_name(Handle id) override {  // 0x00588a60; an unknown player "" (the exe's null)
    const char* n = znet::player_name(id);
    return n ? n : "";
  }
  // The mission's network start and end (piece 5).
  void multiplayer_launch_prep() override;  // 0x0046ceb0
  void network_set(int32_t on) override;    // 0x0047fc40
  void multiplayer_switch(Handle state) override;  // 0x0049cb20
  void multiplayer_page(Handle state, int32_t page, uint8_t reenter) override;  // 0x0049cb00
  void multiplayer_take_pilot(Handle first, Handle second) override;  // 0x00494920
  void multiplayer_resume(Handle screen) override;  // 0x0049b660
  void mech_draw_cockpit(Handle mech) override { program_mech_chat_draw(object_of(mech)); }  // 0x0041ab90
  NetReach network_reach() override { return p_.network_reach ? p_.network_reach() : NetReach{}; }
  void network_hosting(bool on) override {
    if (p_.network_hosting) p_.network_hosting(on);
  }
  void copy_text(const char* text) override {
    if (p_.copy_text) p_.copy_text(text);
  }
  void network_leave() override;            // 0x0046d100
  uint32_t activate_running_copy() override { return 1; }
  uint32_t register_window_class() override { return 1; }
  void unregister_window_class() override {}
  void update_window(uint32_t hwnd) override;
  void message_beep(uint32_t) override {}
  uint32_t message_box_text(uint32_t hwnd, const char* text, const char* caption, uint32_t type) override;
  uint32_t focus_window() override { return hwnd_; }
  void lock_system_keys(uint32_t) override {}
  bool run_while_inactive() override;  // ours (W45): a network game runs on while inactive
  void sleep(uint32_t ms) override;
  void remove_debug_menu(uint32_t) override {}
  void attach_debug_menu(uint32_t) override {}
  const char* command_line() override { return command_line_.c_str(); }
  void run_dir() override {}
  uint32_t message_dll_load(const char* name) override;
  uint32_t directx_version() override { return 0x700; }
  uint32_t install_options(uint32_t mask) override { return mask & install_options_; }  // 0x00597d80
  int32_t pick_video_device(int32_t, int32_t) override { return 0; }
  const char* message_format(int32_t id, const char* arg) override;
  const char* message_format_n(int32_t id, int32_t n) override;
  uint32_t message_box(int32_t a, int32_t caption, int32_t text, int32_t arg = 0) override;
  // Ours, for the tests: answers message_box's boxes in their place (the box's
  // own loop runs up to 100,000 frames).
  std::function<uint32_t(int32_t style, int32_t caption, int32_t text)> on_message_box;
  std::string last_box_text;  // the test hook's: message_box3's text as formatted
  uint32_t game_message_box(int32_t style, int32_t caption, const std::string& text);  // 0x004ac150
  void pump_events();                // FIX (A): the window's events, inside the box's loop
  ztwod::MessageBox* message_box_open() const { return box_; }  // the box being answered, or null

  // The app's objects.
  Handle script_new() override;
  void script_delete(Handle s) override;
  void free_block(Handle) override {}
  Handle frame_new() override { return kFrame; }
  Handle frame_create(Handle frame) override;
  uint32_t frame_hwnd(Handle) override { return hwnd_; }
  void frame_set_title(Handle frame, const char* title) override;
  void error_log_open(uint32_t hwnd, uint32_t a, const char* name) override;

  // Engine modules.
  void settings_init(const char* company, const char* product, const char* version) override;
  void list_pool_init(int32_t) override {}  // the rebuild's containers stand in (startup.md step 6)
  void list_pool_free() override {}
  void reader_init(const char* dir) override;
  void reader_open_archive(const char* path, int32_t current) override;
  void reader_close_archives(int32_t all) override;
  void input_maps_free() override;                 // 0x0051d0c0 (zin::shutdown)
  void mechs_shutdown() override;                  // 0x00414000 (app_mech.cpp)
  void render_flush(Handle rect) override;         // 0x00558010
  bool world_up() override;                        // 0x004f8f50
  void debrief_voice_play(Handle state) override;  // 0x0047abe0
  void anim_stop(int32_t a) override;              // 0x0050ace0 (zeff::anim_stop_all)
  void anim_reset() override;                      // 0x0050ad60 (zeff::anim_reset_all)
  // zVideo's devices (0x00579a10, 0x00579a40, 0x00579aa0, 0x00579a70): one,
  // the GPU, reported as 256 MB with none used (as video_card_memory).
  int32_t video_memory_total() override { return 256 << 20; }
  int32_t video_memory_used() override { return 0; }
  uint32_t video_device_count() override { return 1; }
  const char* video_device_name(uint32_t) override { return video_driver_label(); }
  void reader_shutdown() override;                 // 0x00556210 (zrdr::shutdown)
  int32_t options_load(const char* name) override;
  int32_t sound_init(uint32_t window) override;
  void sound_set_mode(int32_t v) override;
  void sound_load(uint32_t window, const char* name, uint32_t callback, uint32_t a) override;
  void sound_shutdown_groups(uint32_t callback) override;
  void sound_shutdown() override;
  void light_init() override;
  void fonts_init(const char* name, int32_t fonts) override;  // 0x00516e90: zImage's start
  void geometry_init(const char* dir) override;  // 0x00516f50: the start-up's "zbd" as the 2D host's mount
  // The settings the start-up and the Core state's start change from the
  // data (mission-plan item 13).
  void effects_set_a(int32_t v) override;    // 0x0050d1e0: the animations' records kept for the save
  void effects_set_b(int32_t v) override;    // 0x0050d1f0: and handed to the network
  void video_set_clear(int32_t on) override;  // 0x0057a3c0: the frame clear fills the colour
  void camera_set_hse_test(int32_t on) override;  // 0x004f3550
  void update_shadows() override;  // 0x004814e0 on the program's entities (app_mech)
  // zInput (gamez/zinput, docs/input.md).
  void input_init(int32_t commands, const char* map) override;
  void input_start(uint32_t window, uint32_t cmd_show) override;
  void input_devices_shutdown() override;
  void input_acquire() override;
  void input_unacquire() override;
  uint32_t joystick_present() override;
  uint32_t force_feedback_present() override;
  Handle force_feedback_new() override;                 // 0x00433090 (mech3/force_feedback)
  void force_feedback_delete(Handle f) override;        // 0x004333f0
  void force_feedback_spring(Handle f, int32_t on) override;  // 0x004338a0
  void input_set_mode(uint32_t mode) override;
  void input_poll(uint8_t callbacks) override;
  uint32_t key_state(int32_t key) override;
  void input_clear() override;
  void input_reset() override;
  void model_purge(int32_t models, int32_t materials) override;  // 0x005394c0: zmod::models_purge
  void weather_rect(int32_t i, Handle rect) override;  // 0x005a6790: the overlay's rect slot (weather.md)
  void weather_tick(float dt) override;               // 0x005a67c0: the rain and snow
  // The lens flares around the Direct3D frame (render.md, Lens flares).
  void scene_begin_3d() override;                       // 0x00567570: the flare list cleared
  Handle scene_begin() override;                        // 0x005674d0: its count (the frame's first)
  void camera_draw_scene(Handle cam, Handle first) override;  // 0x004f7350: the flares drawn
  void view_camera_refresh(Handle view) override;      // 0x004b51f0
  void input_capture() override;
  void input_release() override;
  void input_center_mouse() override;
  void mouse_set_pos(int32_t x, int32_t y) override;
  void mouse_set_accel(float x, float y) override;
  void clock_init() override;
  void cd_init(int32_t) override {}  // the MCI notify window: the pump stands in
  void video_select_device(int32_t dev) override { device_ = dev; }
  int32_t video_open(uint32_t window, int32_t hw, int32_t restore, int32_t vmode) override;
  int32_t span_init(int32_t) override { return 0; }
  uint32_t video_card_fn_390(int32_t) override { return 0; }
  uint32_t video_card_memory(int32_t dev, uint32_t* total) override;
  void video_shutdown() override { video_opened_ = false; }  // 0x00579ef0: [0x0080271c] 0
  void video_restore_window() override {}
  void video_restore() override {}  // 0x00577cb0: the lost surfaces restored; none in the port
  int32_t video_set_mode(int32_t mode) override;  // 0x00576c80: the screen at the mode's size
  void message_panel_place(Handle kept, Handle area) override;  // 0x0040f330
  void screen_mode_cockpits() override;
  void hud_setup(Handle hud, Handle mech) override;  // 0x0043bb80

  // zVideo's frame: the 16-bit buffer the 2D draws into, shown by the flip.
  Handle video_back_buffer() override { return kBackBuffer; }
  Handle video_front_buffer() override { return kFrontBuffer; }
  Handle video_target() override { return kTarget; }
  Handle video_screen_rect() override { return kScreenRect; }
  void video_lock() override;
  void video_unlock() override {}
  void video_clear(Handle rect) override;
  void video_flip(int32_t a, int32_t b, int32_t c, int32_t d) override;
  uint32_t video_show_cursor(uint32_t on) override;
  // The fade's stills (0x00579500, 0x0055b1f0, 0x00517cc0) on the 16-bit buffer;
  // the colour is the ported tint (0x00558540).
  Handle video_grab(int32_t buffer) override;
  void image_draw(Handle image, int32_t x, int32_t y, int32_t key, Handle rect) override;
  void screen_tint(uint16_t colour, Handle rect, double alpha) override;
  void image_free(Handle image) override;
  bool is_grab(Handle h) const { return grabs_.count(h) != 0; }
  // zFMV's scripts and the pause screen's blur (app_fmv.cpp, docs/fmv.md).
  Handle movie_new() override;
  void movie_set_window(Handle m, uint32_t hwnd) override;
  int32_t movie_load(Handle m, const char* file, const char* name) override;
  void movie_play(Handle m) override;
  uint32_t movie_playing(Handle m) override;
  void movie_stop(Handle m, int32_t all) override;
  void movie_delete(Handle m) override;
  // FIX (A), playtest W47: the CD music paused under a movie, resumed after
  // (the movie whose play paused it, the mode it played in).
  Handle movie_paused_cd_ = 0;
  int movie_cd_mode_ = 0;
  const void* movie_cd_pending_ = nullptr;  // the state a resume waits on (the next pass's)
  void movie_cd_resume(Handle m);
  void movie_cd_pending_step();
  Handle wait_new(int32_t steps, int32_t passes) override;
  void wait_start() override;
  uint32_t wait_poll() override;
  void wait_end() override;
  // Ours on a wide buffer (docs/fmv.md section 8): rows `src_top`..`src_bottom`
  // of the image (-1: all of it) kept in their shape over the whole buffer.
  void fmv_stretch(zimg::Texture* image, int32_t left, int32_t top, int32_t right, int32_t bottom, int32_t src_top = -1,
                   int32_t src_bottom = -1);
  struct MovieStats {  // ours, for checking: the PLAYAVI frames drawn, the last one, its movie's length
    int drawn = 0, last = -1, length = 0;
  } movie_stats;
  // Ours (docs/fmv.md section 8): a movie's shape measured before its first
  // frame instead of beside it (the --movie debug start's, for its shots).
  bool movie_shape_first = false;
  // The Direct3D frame: zvid's lock (0x00576d00 / 0x00576d60), scene nesting
  // (0x00579af0 / 0x00579b10), the quad ([0x0072f3ec]) and the finish (0x00558610).
  // 0x0040f8d0: the message panel's slide, then the view target's HUD data update.
  void camera_update() override;
  void hud_update() override;  // 0x0040fac0: the HUD frame, its parts in the exe's order
  void hud_draw() override;    // 0x0040e4b0: the game editor's frame (game_editor)
  Handle pilot_hud(Handle data) override;          // 0x00430aa0 on the MechData
  void hud_static(Handle hud, float dt) override;  // 0x0043cd20
  void hud_zoom_pass(Handle hud, float dt) override;  // 0x00444230: the reticle's zoom window's 3D pass
  void render_reset() override;                    // 0x00557b20: 0x005581c0, then 0x00557b30 (known)
  void message_panel_init(Handle, Handle) override;  // 0x0040f410
  void message_panel_free() override;                // 0x0040f7d0
  void video_begin_3d() override;
  void video_end_3d() override;
  void video_begin_scene() override;
  void video_draw_3d() override;
  void video_end_scene() override;
  void render_finish() override;
  int32_t lock_back(zvid::Surface* s);
  int32_t unlock_back(zvid::Surface* s);
  // The 2D layer's untouched pixels (FIX (A)): a magenta no 2D draw in the game uses as a colour.
  static constexpr uint16_t kLayerKey = 0xf81f;
  uint16_t* buffer16() { return buffer_.data(); }
  int buffer_width() const { return width_; }
  void widen_screen(class MenuScreen* s, uint32_t ctor);  // ours: a menu screen spread over a wide canvas (app_menus.cpp)
  int buffer_height() const { return height_; }

  // Sound: zsnd objects by handle.
  void sound_update(int32_t a, int32_t b) override;
  Handle sound_find(const char* name) override;
  void sound_play(Handle s, float v) override;
  Handle sound_listener() override;
  void sound_stop_all(Handle s) override;
  void sound_release_all(Handle s) override;
  void sound_resume_all(Handle s) override;
  Handle sound_take_playing() override;
  void sound_restore_playing(Handle l) override;
  void sound_free_playing(Handle l) override;
  void mission_abort() override;
  void maps_init(const char* name) override;  // 0x004583b0: mech3::maps_read
  void briefing_pick_map(const char* name);   // 0x00405160's 0x004582a0 -> 0x0040a0e0
  void mech_data_shutdown() override;         // 0x004584e0: mech3::maps_free
  void set_reopen_training(uint8_t v) override;
  // The mission's quit box (0x0048dfe0, quit_box.h): one, the Core state's.
  Handle message_box_new(const char* file, const char* name) override;
  void message_box_quit_prompt(Handle box) override;
  void replace_state_at(Handle state) override;   // 0x00590250 by address
  void fade_to_state(Handle next, uint32_t arg, int32_t direction, int32_t buffer, float seconds, uint8_t r,
                     uint8_t g, uint8_t b) override;
  void sound_reset() override;
  void voice_start(const char* name) override;   // 0x0056f520: zsnd::load_set
  void voice_stop(Handle s) override;            // 0x0056e9f0: zsnd::stop
  void voice_release(const char* name) override; // 0x0056f530: zsnd::unload_set

  int32_t model_global_light() override;
  void model_set_global_light(int32_t on) override;
  void effects_set_level(int32_t v) override;  // 0x00508fb0: zeff::anim_set_level
  void shadows_reset() override;                // 0x004e6900: mech_shade

  Handle handle_of(void* p);  // a 32-bit handle for a port object (0 for null)
  // FIX (A): the handles are ours (the exe passes raw pointers); an object
  // freed in play forgets its handle, so the old one answers null and a new
  // object at that address gets a new one (docs/fixes.md).
  void forget_object(const void* p);
  void* object_of(Handle h) const;

  const Platform& platform_parts() const { return p_; }
  zutl::MessageDll& messages() { return dll_; }

  static constexpr Handle kFrame = 0x00010000, kBackBuffer = 0x00010010, kFrontBuffer = 0x00010020,
                          kTarget = 0x00010030, kScreenRect = 0x00010040,
                          kGrabs = 0x00010100;  // the fade's stills, 0x10 apart

 private:
  // The 2D buffer at the canvas (w x h), the viewport, the window at the 3D frame (video_open, video_set_mode).
  void screen_size(int32_t w, int32_t h, int32_t frame_w, int32_t frame_h);
  Platform p_;
  bool network_on_ = false;  // the app's +0x1bd (0x0047fc40)
  int lobby_entries_ = 0;    // ours: the lobby's entries (multiplayer_switch), for the tests
  std::string command_line_;
  bool video_opened_ = false;
  std::map<std::string, uint32_t> counts_;
  std::unordered_map<uint32_t, uint8_t> memory_;
  uint32_t hwnd_ = 0;
  int32_t device_ = -1;
  uint32_t install_options_ = 0x070707;  // HKLM's InstallOptions: a full install (FIX (A), settings.md)
  int width_ = 640, height_ = 480;
  std::vector<uint16_t> buffer_;
  std::vector<uint8_t> cover_alpha_;   // the layer's blends (zrndr::LayerCover), FIX (A)
  std::vector<uint16_t> cover_colour_;
  struct Grab {
    int width = 0, height = 0;  // the buffer's when it was taken
    std::vector<uint16_t> pixels;
  };
  std::map<Handle, Grab> grabs_;  // the fade's stills, by handle
  uint32_t cursor_ = 1;
  zutl::MessageDll dll_;
  std::string formatted_;
  ztwod::MessageBox* box_ = nullptr;  // the message box being answered (0x004ac150)
  std::vector<void*> objects_;           // handle - 0x00100000 - 1
  Launch launch_;
  Handle pilot_ = 0, game_ = 0, view_ = 0, camera_ = 0;
  std::map<int32_t, Handle> view_modes_;  // the view's camera ids' blocks (sync_view)
  Handle stub_pilot_ = 0;  // the stand-in's block, made once
  Handle slot_ = 0;  // the debug launch's game slot (vt+0x34): the mech, its configuration, its skin
  Handle next_block_ = 0x01000000;
  bool scene_drawn_ = false, last_drew_scene_ = false, layer_drawn_ = false;
  std::unique_ptr<LabView> lab_view_;  // the Lab State's 3D view while it is up
  std::unique_ptr<class ForceFeedback> force_;  // [0x00608620]
  std::unique_ptr<zfmv::BlurAction> pause_blur_;  // the pause screen's (wait_new)
  bool last_via_render_ = false;  // the frame just shown went through Render3D
  bool held_ = false;             // a shown frame kept behind a message box (hold_shown_frame)
  bool layer_blank_ = true;       // every 2D layer drawn since the last flip was only the key colour
  bool buffer_is_screen_ = true;  // the 16-bit buffer holds the frame shown (not the 3D lock's layer)
  std::vector<uint16_t> kept_screen_;  // that frame while a 3D lock has the buffer
  std::unordered_map<void*, Handle> handles_;
  struct OpenScreen {
    MenuScreen* screen;
    uint32_t size;  // the original object's, for the offsets into it
    bool owned = false;  // made by another screen's part (adopt_screen), deleted with it
  };
  std::map<Handle, OpenScreen> screens_;
  QuitBox* quit_box_ = nullptr;  // at quit_box_at_
  Handle quit_box_at_ = 0;
  std::map<Handle, MenuState*> menu_states_;  // the heap menu objects, by handle (menu_states.h)
};

// The frame's surface and tint hooks to `e`'s buffer and Render3D (null: none).
void wire_frame_hooks(GameEngine* e);
// The colour tint's Direct3D quad (0x0057dd80) with its canvas rect onto the
// frame (FIX (B), playtest S14); wire_frame_hooks sets it as zrndr's hook.
void tint_quad_on_frame(uint16_t colour, const int32_t* rect, double alpha);

}  // namespace mech3
