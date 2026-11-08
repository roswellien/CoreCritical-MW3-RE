// mw3: MechWarrior 3 rebuilt, on SDL3 (docs/program.md). The original's own
// start-up runs (the app constructor, InitInstance, the engine start, the
// main loop zGameApp::Run and its game states) with the platform stand-ins of
// src/frontend/sdl underneath; what it reaches that is not ported is logged
// once by its original address and does nothing.
//
//   mw3 [install-dir] [options] [-nofmv] [-seasick] [-cdN]
//
//   --install DIR        the MW3 folder, any version (default: mw3.cfg's install=, else searched, else asked: sdl_install.h)
//   --install-from PATH  install from the disc first (ours, disc_install.h): a drive or folder, a .iso, or a
//                        .cue/.bin (its audio tracks the music); --patch too makes it 1.2
//     --install-to DIR   where (default: the write dir's install/); saved as mw3.cfg's install=
//   --assets DIR         a folder of replacement files searched first (repeatable)
//   --write-dir DIR      where saves and settings go (default: the user's app data)
//   --mission NAME       debug start into a mission through the Load and Core
//                        states: c1, c1m2 (mission 2 of c1), c4b, t1 ...; c1mp1 a
//                        network game on the map with --host/--join (the --net-*
//                        soak options, multiplayer-port.md piece 8b)
//   --fly NAME           debug start: the world alone with a free camera
//     --variant BLOCK    the world.zrd block (default m1 or the first)
//     --pos X Y Z, --look YAW PITCH (degrees): the camera's start
//   --dialog FILE NAME   debug start showing one zTwoD dialog, e.g. dialog.zrd MAINMENU
//   --movie NAME         debug start playing one fmv.zrd block (INTRO, c1m2 ...); with
//                        --nosound on a clock stepping 1/60 s each poll, so each
//                        frame is shown once (--shot F --shot-frame N: the movie's
//                        N-th frame, numbered from 1); a movie's shape on a wide
//                        screen measured before its first frame (docs/fmv.md section 8);
//                        the movies are the importer's (mw3import), in the write dir
//   --shot FILE.bmp      save frame N (--shot-frame N, default 30) and quit; hides the window
//     --shot-any         count every frame shown (the fade's too), not only the 3D scene's
//   --frames N           quit after N frames shown (a debug start's; from the menus each flip counts,
//                        a movie's at its own rate: the intro's 15 a second); also keeps the window
//                        out of fullscreen
//   --action-chain all|cN|cN:S  Instant Action played through, each world and scenario (ours, modes_chain.h)
//   --campaign-chain new:N|resume:N|kills:N|retry:N  a pilot through the campaign as a player (ours,
//                        campaign_chain.h): N missions won, shots in --write-dir
//   --frame-ms N         each frame shown is N ms on the game's clock, however
//                        fast it is drawn (ours: scripted runs keep their timing)
//   --seed N             rand seeded with N where the game seeds it from the clock (ours)
//   --window WxH         window size (default 1280x960; with any_resolution the
//                        frame's shape, and a given size pillarboxes the frame)
//   --nosound            no audio device (a silent DirectSound stand-in)
//   --hidden             keep the window hidden
//   --windowed           a window, not fullscreen (also --window, or windowed=1 in mw3.cfg)
//   --cc-vulkan          experimental Windows Vulkan/DXGI backend (requires a window)
//   --cc-vulkan-wsi      experimental Vulkan swapchain on an SDL surface
//   --cc-aa MODE         off, fxaa, ssaa2 or ssaa4 on the Core Critical backend
//   --cc-af LEVEL        1, 2, 4, 8 or 16 on the Core Critical backend
//   --verbose            list every unported function reached at exit
#include "crt/msvc_rand.h"
#include "light_invariance.h"
#include "platform/prepared_visuals.h"
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <functional>
#include <filesystem>
#include <map>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <exception>

#include "frontend/sdl/campaign_chain.h"
#include "frontend/sdl/modes_chain.h"
#include "frontend/sdl/debug_states.h"
#include "frontend/sdl/first_run_import.h"
#include "frontend/sdl/crash_report.h"
#include "frontend/sdl/data_folder_start.h"
#include "frontend/sdl/run_log.h"
#include "frontend/sdl/sdl_child.h"
#include "frontend/sdl/launcher.h"
#include "frontend/sdl/sdl_install.h"
#include "mech3/game_setup.h"
#include "frontend/sdl/sdl_force.h"
#include "frontend/sdl/sdl_input.h"
#include "frontend/sdl/sdl_music.h"
#include "frontend/sdl/sdl_platform.h"
#include "frontend/sdl/sdl_sound.h"
#include "frontend/sdl/sdl_twod_host.h"
#include "frontend/sdl/sdl_vfw.h"
#include "frontend/sdl/net_enet.h"
#include "frontend/sdl/net_upnp.h"
#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/ztwod/twod_host.h"
#include "gamez/zeffect/zeff_anim_save.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/app.h"
#include "mech3/mods.h"
#include "mech3/app_engine.h"
#include "mech3/app_hud.h"
#include "mech3/app_keys.h"
#include "mech3/pilot.h"
#include "mech3/app_mech.h"
#include "mech3/app_vehicle.h"
#include "mech3/app_states.h"
#include "mech3/briefing.h"
#include "mech3/mech_config.h"
#include "gamez/zinput/zinput.h"
#include "gamez/zweapon/zweapon.h"
#include "mech3/mech_control.h"
#include "mech3/data_version.h"
#include "mech3/patch_import.h"
#include "mech3/mission.h"
#include "mech3/app_sensors.h"
#include "mech3/turret.h"
#include "platform/reg_file.h"
#include "platform/silent_sound.h"

namespace {

// What the player's game holds (--verbose): its mission, the store and the
// mission's salvage together by item, the player's mech and loadout, the
// lancemates.
std::string campaign_summary() {
  mech3::Pilot* p = mech3::current_pilot();
  auto* c = p ? p->campaign() : nullptr;
  if (!c) return "no campaign";
  std::map<std::pair<int, int32_t>, int32_t> held;
  for (const mech3::Store* st : {&c->store, &c->store3c})
    for (const mech3::ConfigItem* it : st->items) held[{it->kind(), it->id()}] += it->count;
  std::string out = "Op " + std::to_string(c->operation()) + " Mission " + std::to_string(c->mission_number()) +
                    ", " + std::to_string(held.size()) + " kinds held:";
  for (const auto& [k, n] : held)
    out += " " + std::to_string(k.first) + "/" + std::to_string(k.second) + "x" + std::to_string(n);
  const mech3::Config* cf = c->slot.config;
  out += "; mech " + std::to_string(c->slot.mech) + " (" + (cf && cf->name ? cf->name : "-") + ", " +
         std::to_string(cf ? static_cast<int>(cf->items.size()) : 0) + " items)";
  out += "; lancemates " + std::to_string(c->lancemates.size()) + "; field bases " + std::to_string(c->a8);
  return out;
}

struct Args {
  sdlfe::RendererBackend backend = sdlfe::RendererBackend::sdl;
  sdlfe::CoreCriticalQuality quality;
  sdlfe::CoreCriticalAssets cc_assets;
  SDL_Window* launcher_window = nullptr;  // the launcher's, for the game to take (one window; ours)
  std::string install, write_dir, mission, variant, dialog_file, dialog_name, shot, movie;
  std::string cfg_install;  // mw3.cfg's install= (the command line's wins)
  std::string cfg_mw1_game;  // mw3.cfg's mw1_game= (ours: the launcher's MechWarrior 1989 folder)
  std::string cfg_joystick, cfg_joystick_axes;  // mw3.cfg's joystick=, joystick_axes= (ours, W54: sdl_input.h)
  int wide_menus = -1, cfg_wide_menus = -1;  // --wide-menus / --no-wide-menus, mw3.cfg's widemenus= (ours)
  bool debug_camera = false;  // --debug-camera or debugcamera=1 (ours, docs/editor.md S4): the game editor on Shift+F12
  std::vector<std::string> assets;
  // Mods (ours, docs/modding.md): --mod NAME (repeatable, this run's list),
  // else mw3.cfg's mod= lines, in order; --no-mods none; --mods lists them.
  std::vector<std::string> mods, cfg_mods;
  bool mods_given = false, no_mods = false, list_mods = false;
  std::string check_mod;  // --check-mod DIR

  std::string original;  // -nofmv -seasick -cdN, passed on as the command line
  // The bug fixes (zfix bugs; fixes.md, The bug fixes switch): mw3.cfg's
  // bugfixes=0/1 (default 1), --original turns them off whatever it says.
  bool play_original = false;
  sdlfe::HudOptions hud;
  bool hud_mode_given = false, hud_artwork_given = false, hud_text_given = false;
  int cfg_bugfixes = 1;
  int cfg_throttle_reverse = 1;  // mw3.cfg's throttle_reverse= (ours, W56: zfix.h)
  bool first_start = false;   // the install found by the search, not mw3.cfg's (ours: the first start's notes)
  float pos[3] = {0, 60, 0};
  float look[2] = {0, -2.9f};
  bool have_pos = false, nosound = false, hidden = false, verbose = false, fly = false;
  float vehicle_ahead = 0;  // --vehicle-ahead M (ours): the mission's first (armed) vehicle placed M m before the player
  bool field_base_fixture =
      false; // --field-base-ahead: typed MFB selection, not a name guess
  bool vehicle_hold = false;  // --vehicle-hold (ours): and the player turned to face it each frame (to watch it fire)
  float turret_ahead = 0;  // --turret-ahead M (ours): the player M m from the nearest live turret, facing it
  float mech_ahead = 0;  // --mech-ahead M (ours): the player kept M to 1.5 M m from the nearest enemy mech in play, facing it
  int shot_frame = 30, frames = 0, win_w = 1280, win_h = 960, fire = 0, fire_group = 0, objectives = 0;
  int click = 0, click_up = 0, shot_series = 0;
  bool no_import = false, force_import = false;  // --no-import, --import (ours: first_run_import.h)
  bool no_launcher = false;   // --no-launcher (ours: launcher.h): no find screen, the old dialog if there is no install
  std::string ready_file;     // --ready-file F (ours, sdl_child.h): written at the first frame shown, for mw3launcher
  std::string vfw_helper;     // --vfw-helper EXE (ours, sdl_vfw.h): the movies' Video for Windows helper; "-" none
  std::string patch;          // --patch PATH (ours, patch_import.h): the official 1.2 patch into the write dir's overlay
  std::string install_from, install_to;  // --install-from PATH, --install-to DIR (ours, disc_install.h)
  bool windowed = false;      // --windowed (or windowed=1 in mw3.cfg): not fullscreen
  bool window_given = false;  // --window: kept, the frame pillarboxed in it (else the frame's shape)
  std::string chain;          // --campaign-chain new:N / resume:N (ours, campaign_chain.h)
  std::string modes;          // --action-chain all / c2 / c2:4 (ours, modes_chain.h)
  int frame_ms = 0;           // --frame-ms N: each frame shown N ms on the game's clock (ours)
  int64_t seed = -1;          // --seed N: rand seeded with N wherever the game seeds it from the clock (ours)
  int win = 0;  // --win N (ours): the mission won N frames in, by its Exit Mission
  int frame_times = 0;  // --frame-times N (ours): each frame's wall time from the N-th frame shown, summed up at the end
  int lab_mech = -1;  // --lab-mech N (ours): the lab's 3D view shows chassis N (alone: through c1m2's briefing)
  struct Tap { int down, up, dik, ingame = 0; };
  std::vector<Tap> taps;  // --tap DIK@N[:M]
  // group: a wide menu's left (0), middle (1) or right (2); players: N > 0
  // counts the frame from the first one the session holds N players (ours)
  struct ClickAt { int frame, x, y, group = 0, players = 0, back = 0, ingame = 0, hover = 0; };
  std::vector<ClickAt> clicks_at;  // --click-at X,Y@N (the game's 640 x 480 pixels)
  std::string briefing;  // --briefing NAME (ours): a campaign mission's briefing, as the menus reach it
  bool walk = false;  // --walk: full throttle (THROTTLE0's key) at the start (ours)
  // The network (ours, multiplayer-port.md piece 4, until the lobby screens):
  // --host / --join HOST[:PORT] start a session before the game, as the
  // player --net-name; --net-port the port (a host's default 27930, a
  // joiner's the system's), --net-bind the address (127.0.0.1 in tests),
  // --net-say TEXT@N a chat line to everyone at the N-th pass.
  bool net_host = false;
  std::string net_join, net_name = "Pilot", net_bind = "0.0.0.0", net_say;
  int net_port = -1, net_say_at = 0;
  // The soak test's (ours, piece 8b, until the lobby): --mission <dir>mp<N>
  // a network game on the map; --net-wait N the start held until N players
  // are in; --net-minutes M the game's clock; --net-drive SEED the pilot
  // driven by a pattern; --net-soak a line of where every mech is each half
  // second, the scores at the game's end, and a quit at the menu after it.
  int net_wait = 0, net_minutes = 0;
  int net_seconds = 0;  // --net-seconds: a network game the menus start lasts this long (each one)
  int net_kill_at = 0;  // --net-kill-at S: a kill staged on the pilot S seconds (of 30 frames) into each such game
  int blur[2] = {0, 0};  // --blur-ingame N:MS (ours, W45): inactive at game frame N, a focus back MS ms later
  bool fire_probe = false;  // --fire-probe (ours, W40): each menu game frame, the trigger and the reloads, timed
  int net_walk[2] = {0, 0};  // --net-walk A,B: the throttle held up from A to B seconds (of 30 frames) into each
  long long net_drive = -1;
  bool net_staged = true;  // --net-staged 0: the drive stages no kills
  float net_ahead = 0;     // --net-ahead M: the pilot held M m from the nearest remote mech, facing it
  // The multiplayer screens' (ours, for the tests on loopback, where no
  // broadcast arrives): --net-discovery N the LAN search answered on N when
  // hosting, --net-lan IP:PORT the search's LAN probe sent there.
  int net_discovery = sdlfe::kDiscoveryPort;
  std::string net_lan;
  // --net-upnp URL: the router's description there (the tests' fake one);
  // without it a transport bound to loopback asks no router.
  std::string net_upnp;
  bool net_soak = false;
  bool shot_any = false;
};

void usage() {
  std::printf(
      "mw3 [install-dir] [--install DIR | --install-from DISC|ISO|CUE "
      "[--install-to DIR]] "
      "[--assets DIR]... [--write-dir DIR] [--version]\n"
      "    [--mission NAME | --fly NAME [--variant BLOCK]] [--pos X Y Z] "
      "[--look YAW PITCH] "
      "[--vehicle-ahead M [--vehicle-hold]] [--field-base-ahead M] "
      "[--turret-ahead M] [--mech-ahead M] [--dialog FILE "
      "NAME]\n"
      "    [--shot FILE.bmp [--shot-frame N] [--shot-any] [--shot-series K]] "
      "[--fire N "
      "[--fire-group 1..5]] [--objectives N] [--win N] [--lab-mech N]\n"
      "    [--cc-model CHASSIS LOD MESH BWTEX1]... [--cc-textures BWTEX1]\n"
      "    [--cc-environment-pack DIR]...\n"
      "    [--cc-model-materials CHASSIS LOD BWSFM1]...\n"
      "    [--cc-model-normals CHASSIS LOD BWNRM2]... [--cc-lighting "
      "off|flat|normal] [--cc-sun "
      "AZIMUTH ELEVATION]\n"
      "    [--cc-hud original|core-critical] [--cc-hud-artwork DIR] "
      "[--cc-hud-text 0|1]\n"
      "    [--cc-geometry legacy|partial|gpu|cpu-reference] "
      "[--cc-geometry-debug normals]\n"
      "    [--cc-render-thread 0|1]\n"
      "    [--windowed] [--cc-vulkan | --cc-vulkan-wsi] [--cc-aa MODE] "
      "[--cc-af LEVEL] "
      "[--original] [--import | --no-import] [--no-launcher] [--debug-camera] "
      "[--wide-menus | "
      "--no-wide-menus] [--mod NAME]... [--no-mods] [--mods] [--check-mod DIR] "
      "[--patch ZIP|DIR] "
      "[--vfw-helper EXE | --no-vfw-helper] [--briefing c1m2] [--click N[:M]] "
      "[--click-at "
      "X,Y@N]... [--click-players X,Y@N/P]... [--click-return X,Y@N]... "
      "[--walk] [--tap "
      "DIK@N[:M]]... [--frames N] [--frame-ms N] [--frame-times N] [--seed N] "
      "[--window WxH] "
      "[--nosound] [--hidden] [--verbose]\n"
      "    [-nofmv] [-seasick] [-cdN]\n"
      "See docs/program.md.\n");
}

bool parse(int argc, char** argv, Args& a) {
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    auto next = [&](int n) { return i + n < argc; };
    if (s == "--help" || s == "-h") return usage(), false;
    if (s == "--version") {  // ours: the build, and (on Windows) that every DLL it needs loaded
#ifndef MW3_VERSION
#define MW3_VERSION "unknown"
#endif
      std::printf("mw3 (MechWarrior 3 rebuild) %s\n", MW3_VERSION);
      std::exit(0);
    }
    else if (s == "--install" && next(1)) a.install = argv[++i];
    else if (s == "--assets" && next(1)) a.assets.emplace_back(argv[++i]);
    else if (s == "--write-dir" && next(1)) a.write_dir = argv[++i];
    else if (s == "--mod" && next(1)) a.mods.emplace_back(argv[++i]), a.mods_given = true;
    else if (s == "--no-mods") a.no_mods = true;
    else if (s == "--mods") a.list_mods = true;
    else if (s == "--check-mod" && next(1)) a.check_mod = argv[++i];
    else if (s == "--mission" && next(1)) a.mission = argv[++i];
    else if (s == "--fly" && next(1)) a.mission = argv[++i], a.fly = true;
    else if (s == "--variant" && next(1)) a.variant = argv[++i];
    else if (s == "--dialog" && next(2)) a.dialog_file = argv[++i], a.dialog_name = argv[++i];
    else if (s == "--movie" && next(1)) a.movie = argv[++i];
    else if (s == "--shot" && next(1)) a.shot = argv[++i];
    else if (s == "--shot-frame" && next(1)) a.shot_frame = std::atoi(argv[++i]);
    else if (s == "--shot-any") a.shot_any = true;
    else if (s == "--shot-series" && next(1)) a.shot_series = std::atoi(argv[++i]);
    else if (s == "--frame-times" && next(1)) a.frame_times = std::atoi(argv[++i]);
    else if (s == "--fire" && next(1)) a.fire = std::atoi(argv[++i]);
    else if (s == "--fire-group" && next(1)) a.fire_group = std::atoi(argv[++i]);
    else if (s == "--objectives" && next(1)) a.objectives = std::atoi(argv[++i]);
    else if (s == "--win" && next(1)) a.win = std::atoi(argv[++i]);
    else if (s == "--lab-mech" && next(1)) a.lab_mech = std::atoi(argv[++i]);
    else if (s == "--frames" && next(1)) a.frames = std::atoi(argv[++i]);
    else if (s == "--host") a.net_host = true;
    else if (s == "--join" && next(1)) a.net_join = argv[++i];
    else if (s == "--net-name" && next(1)) a.net_name = argv[++i];
    else if (s == "--net-port" && next(1)) a.net_port = std::atoi(argv[++i]);
    else if (s == "--net-bind" && next(1)) a.net_bind = argv[++i];
    else if (s == "--net-wait" && next(1)) a.net_wait = std::atoi(argv[++i]);
    else if (s == "--net-minutes" && next(1)) a.net_minutes = std::atoi(argv[++i]);
    else if (s == "--net-seconds" && next(1)) a.net_seconds = std::atoi(argv[++i]);
    else if (s == "--net-kill-at" && next(1)) a.net_kill_at = std::atoi(argv[++i]);
    else if (s == "--fire-probe") a.fire_probe = true;
    else if (s == "--blur-ingame" && next(1)) std::sscanf(argv[++i], "%d:%d", &a.blur[0], &a.blur[1]);
    else if (s == "--net-walk" && next(1)) std::sscanf(argv[++i], "%d,%d", &a.net_walk[0], &a.net_walk[1]);
    else if (s == "--net-drive" && next(1)) a.net_drive = std::strtoll(argv[++i], nullptr, 10);
    else if (s == "--net-soak") a.net_soak = true;
    else if (s == "--net-staged" && next(1)) a.net_staged = std::atoi(argv[++i]) != 0;
    else if (s == "--net-ahead" && next(1)) a.net_ahead = std::strtof(argv[++i], nullptr);
    else if (s == "--net-discovery" && next(1)) a.net_discovery = std::atoi(argv[++i]);
    else if (s == "--net-lan" && next(1)) a.net_lan = argv[++i];
    else if (s == "--net-upnp" && next(1)) a.net_upnp = argv[++i];
    else if (s == "--net-say" && next(1)) {
      const std::string v = argv[++i];
      const size_t atpos = v.rfind('@');
      a.net_say = v.substr(0, atpos), a.net_say_at = atpos == std::string::npos ? 1 : std::atoi(v.c_str() + atpos + 1);
    }
    else if (s == "--campaign-chain" && next(1)) a.chain = argv[++i];
    else if (s == "--action-chain" && next(1)) a.modes = argv[++i];
    else if (s == "--frame-ms" && next(1)) a.frame_ms = std::atoi(argv[++i]);
    else if (s == "--seed" && next(1)) a.seed = std::strtoll(argv[++i], nullptr, 10);
    else if (s == "--window" && next(1)) std::sscanf(argv[++i], "%dx%d", &a.win_w, &a.win_h), a.window_given = true;
    else if (s == "--nosound") a.nosound = true;
    else if (s == "--walk") a.walk = true;
    else if (s == "--briefing" && next(1)) a.briefing = argv[++i];
    else if (s == "--click-at" && next(1)) {
      Args::ClickAt c{};
      if (std::sscanf(argv[++i], "%d,%d@%d", &c.x, &c.y, &c.frame) == 3) a.clicks_at.push_back(c);
    }
    else if (s == "--click-players" && next(1)) {  // X,Y@N/P: N frames after the session first holds P players
      Args::ClickAt c{};
      if (std::sscanf(argv[++i], "%d,%d@%d/%d", &c.x, &c.y, &c.frame, &c.players) == 4 && c.players > 0)
        a.clicks_at.push_back(c);
    }
    else if ((s == "--tap-ingame" || s == "--hold-ingame") && next(1)) {  // DIK@N[:M] / N:M (the left button), each game's frames
      const bool hold = s == "--hold-ingame";
      const char* v = argv[++i];
      const char* at = hold ? v - 1 : std::strchr(v, '@');
      const char* c = at ? std::strchr(at + 1, ':') : nullptr;
      if (at) a.taps.push_back({std::atoi(at + 1), c ? std::atoi(c + 1) : std::atoi(at + 1) + 1,
                                hold ? -1 : static_cast<int>(std::strtol(v, nullptr, 0)), 1});
    }
    else if (s == "--hover-at" && next(1)) {  // X,Y@N: the cursor there at frame N, no click (a rollover)
      Args::ClickAt c{};
      if (std::sscanf(argv[++i], "%d,%d@%d", &c.x, &c.y, &c.frame) == 3) c.hover = 1, a.clicks_at.push_back(c);
    }
    else if (s == "--click-ingame" && next(1)) {  // X,Y@N: N frames into each game the menus start
      Args::ClickAt c{};
      if (std::sscanf(argv[++i], "%d,%d@%d", &c.x, &c.y, &c.frame) == 3) c.ingame = 1, a.clicks_at.push_back(c);
    }
    else if (s == "--click-return" && next(1)) {  // X,Y@N: N frames after each return to the lobby from a game
      Args::ClickAt c{};
      if (std::sscanf(argv[++i], "%d,%d@%d", &c.x, &c.y, &c.frame) == 3) c.back = 1, a.clicks_at.push_back(c);
    }
    else if (s == "--tap" && next(1)) {
      const char* v = argv[++i];
      const char* at = std::strchr(v, '@');
      const char* c = at ? std::strchr(at, ':') : nullptr;
      if (at) a.taps.push_back({std::atoi(at + 1), c ? std::atoi(c + 1) : std::atoi(at + 1) + 1,
                                static_cast<int>(std::strtol(v, nullptr, 0))});
    }
    else if (s == "--click" && next(1)) {
      const char* v = argv[++i];
      a.click = std::atoi(v);
      const char* c = std::strchr(v, ':');
      a.click_up = c ? std::atoi(c + 1) : a.click + 3;
    }
    else if (s == "--hidden") a.hidden = true;
    else if (s == "--windowed") a.windowed = true;
    else if (s == "--no-import") a.no_import = true;
    else if (s == "--cc-vulkan") a.backend = sdlfe::RendererBackend::core_critical_dxgi, a.no_launcher = true;
    else if (s == "--cc-hud" && next(1)) a.hud.mode = argv[++i], a.hud_mode_given = true;
    else if (s == "--cc-hud-artwork" && next(1)) a.hud.artwork = argv[++i], a.hud_artwork_given = true;
    else if (s == "--cc-hud-text" && next(1)) {
      const std::string value = argv[++i];
      if (value != "0" && value != "1") return false;
      a.hud.text = value == "1", a.hud_text_given = true;
    }
    else if (s == "--cc-vulkan-wsi") a.backend = sdlfe::RendererBackend::core_critical_wsi, a.no_launcher = true;
    else if (s == "--cc-aa" && next(1)) a.quality.antialiasing = argv[++i];
    else if (s == "--cc-af" && next(1)) a.quality.anisotropy = argv[++i];
    else if (s == "--cc-textures" && next(1)) a.cc_assets.textures = argv[++i];
    else if (s == "--cc-lighting" && next(1)) a.cc_assets.lighting = argv[++i];
    else if (s == "--cc-render-thread" && next(1)) {
      const std::string value = argv[++i];
      if (value != "0" && value != "1") {
        std::fprintf(stderr, "--cc-render-thread supports 0 or 1\n");
        return false;
      }
      a.cc_assets.render_thread = value == "1";
    } else if (s == "--cc-geometry" && next(1))
      a.cc_assets.geometry = argv[++i];
    else if (s == "--cc-geometry-debug" && next(1)) {
      if (std::string(argv[++i]) != "normals") {
        std::fprintf(stderr,"--cc-geometry-debug supports normals\n");
        return false;
      }
      a.cc_assets.geometry_debug_normals = true;
    }
    else if (s == "--cc-sun" && next(2)) {
      a.cc_assets.sun_explicit = true;
      a.cc_assets.sun_azimuth = argv[++i], a.cc_assets.sun_elevation = argv[++i];
    } else if (s == "--cc-environment-pack" && next(1)) {
      a.cc_assets.environment_packs.push_back(argv[++i]);
    } else if (s == "--cc-model-materials" && next(3)) {
      sdlfe::PreparedMaterialBinding binding;
      binding.chassis = argv[++i], binding.lod = argv[++i],
      binding.path = argv[++i];
      a.cc_assets.materials.push_back(std::move(binding));
    } else if (s == "--cc-model-normals" && next(3)) {
      sdlfe::PreparedNormalBinding binding;
      binding.chassis = argv[++i], binding.lod = argv[++i], binding.path = argv[++i];
      a.cc_assets.normals.push_back(std::move(binding));
    } else if (s == "--cc-model" && next(4)) {
      sdlfe::PreparedModelBinding binding;
      binding.chassis = argv[++i], binding.lod = argv[++i];
      binding.mesh = argv[++i], binding.textures = argv[++i];
      a.cc_assets.models.push_back(std::move(binding));
    } else if (s == "--no-launcher")
      a.no_launcher = true;
    else if (s == "--ready-file" && next(1)) a.ready_file = argv[++i];
    else if (s == "--wide-menus") a.wide_menus = 1;
    else if (s == "--debug-camera") a.debug_camera = true;
    else if (s == "--no-wide-menus") a.wide_menus = 0;
    else if (s == "--original") a.play_original = true;
    else if (s == "--import") a.force_import = true;
    else if (s == "--patch" && next(1)) a.patch = argv[++i];
    else if (s == "--install-from" && next(1)) a.install_from = argv[++i];
    else if (s == "--install-to" && next(1)) a.install_to = argv[++i];
    else if (s == "--vfw-helper" && next(1)) a.vfw_helper = argv[++i];
    else if (s == "--no-vfw-helper") a.vfw_helper = "-";
    else if (s == "--verbose") a.verbose = true;
    else if (s == "--pos" && next(3)) {
      for (float& v : a.pos) v = std::strtof(argv[++i], nullptr);
      a.have_pos = true;
    } else if (s == "--field-base-ahead" && next(1)) {
      a.vehicle_ahead = std::strtof(argv[++i], nullptr);
      a.field_base_fixture = true;
    } else if (s == "--vehicle-ahead" && next(1)) {
      a.vehicle_ahead = std::strtof(argv[++i], nullptr);
    } else if (s == "--turret-ahead" && next(1)) {
      a.turret_ahead = std::strtof(argv[++i], nullptr);
    } else if (s == "--mech-ahead" && next(1)) {
      a.mech_ahead = std::strtof(argv[++i], nullptr);
    } else if (s == "--vehicle-hold") {
      a.vehicle_hold = true;
    } else if (s == "--look" && next(2)) {
      for (float& v : a.look) v = std::strtof(argv[++i], nullptr);
    } else if (s.size() > 1 && s[0] == '-' && s[1] != '-') {
      a.original += " " + s;  // the exe's own switches (startup.md, Command line)
    } else if (s[0] != '-' && a.install.empty()) {
      a.install = s;
    } else {
      std::fprintf(stderr, "mw3: unknown argument %s\n", s.c_str());
      return usage(), false;
    }
  }
  // --lab-mech alone (ours): the lab is reached from c1m2's briefing (MECHLAB,
  // which Op 1 Mission 1 greys), so it takes that route and clicks MECHLAB.
  if (a.lab_mech >= 0 && a.briefing.empty() && a.mission.empty()) {
    a.briefing = "c1m2";
    a.clicks_at.push_back({2500, 300, 435, 1});  // MECHLAB (the briefing: one block)
  }
  return true;
}

// mw3.cfg in the write dir: install=DIR and assets=DIR lines.
void read_config(const std::string& dir, Args& a) {
  std::ifstream f(dir + "/mw3.cfg");
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("install=", 0) == 0) a.cfg_install = line.substr(8);
    else if (line.rfind("mw1_game=", 0) == 0) a.cfg_mw1_game = line.substr(9);
    else if (line.rfind("assets=", 0) == 0) a.assets.push_back(line.substr(7));
    else if (line.rfind("cc_hud=", 0) == 0 && !a.hud_mode_given) a.hud.mode = line.substr(7);
    else if (line.rfind("cc_hud_artwork=", 0) == 0 && !a.hud_artwork_given) a.hud.artwork = line.substr(15);
    else if (line == "cc_hud_text=0" && !a.hud_text_given) a.hud.text = false;
    else if (line == "cc_hud_text=1" && !a.hud_text_given) a.hud.text = true;
    else if (line.rfind("mod=", 0) == 0) a.cfg_mods.push_back(line.substr(4));
    else if (line.rfind("joystick=", 0) == 0) a.cfg_joystick = line.substr(9);
    else if (line.rfind("joystick_axes=", 0) == 0) a.cfg_joystick_axes = line.substr(14);
    else if (line == "windowed=1") a.windowed = true;
    else if (line == "bugfixes=0") a.cfg_bugfixes = 0;
    else if (line == "bugfixes=1") a.cfg_bugfixes = 1;
    else if (line == "widemenus=0") a.cfg_wide_menus = 0;
    else if (line == "widemenus=1") a.cfg_wide_menus = 1;
    else if (line == "throttle_reverse=0") a.cfg_throttle_reverse = 0;
    else if (line == "throttle_reverse=1") a.cfg_throttle_reverse = 1;
    else if (line == "debugcamera=1") a.debug_camera = true;
  }
}

// "c1m2" is zbd/c1 with world block m2 and mission number 2; "c4b" is a dir.
void split_mission(const std::string& name, std::string& dir, std::string& variant, int& number) {
  dir = name, number = 1;
  const size_t m = name.rfind('m');
  if (m != std::string::npos && m > 0 && m + 1 < name.size() &&
      name.find_first_not_of("0123456789", m + 1) == std::string::npos && !zutl::exists(("zbd\\" + name + "\\gamez.zbd").c_str())) {
    dir = name.substr(0, m);
    if (variant.empty()) variant = name.substr(m);
    number = std::atoi(name.c_str() + m + 1);
  }
}

int run(const Args& a, platform::VideoForWindows* vfw);

// The network's log until the lobby screens are ported (ours, piece 4): its
// local messages as lines, a chat's text, and the per-pass work.
std::function<void()> g_net_pass;
void net_log(uint32_t from, const uint8_t* m, size_t size) {
  const uint16_t type = static_cast<uint16_t>(m[0] | m[1] << 8);
  const char* name = znet::player_name(from);
  const std::string who = std::to_string(from) + (name ? std::string(" (") + name + ")" : std::string());
  switch (type) {
    case 0x3: std::printf("mw3: network: player %s joined\n", who.c_str()); break;
    case 0x6: std::printf("mw3: network: player %s left\n", who.c_str()); break;
    case 0x7: std::printf("mw3: network: we were kicked out\n"); break;
    case 0xd: std::printf("mw3: network: player %s is the host\n", who.c_str()); break;
    case 0xc: std::printf("mw3: network: error: %.*s\n", static_cast<int>(size > 4 ? size - 5 : 0), m + 4); break;
    case 0x1f:
      if (size > 8) std::printf("mw3: network: player %s says \"%.*s\"\n", who.c_str(), static_cast<int>(strnlen(reinterpret_cast<const char*>(m + 8), size - 8)), m + 8);
      break;
    default: break;
  }
  std::fflush(stdout);
}
void net_log_handlers() {
  for (uint16_t t : {0x3, 0x6, 0x7, 0xd, 0xc, 0x1f}) znet::on(t, &net_log);
}
// A chat line (0x1f: +4 the team bit, +8 the text) to everyone.
void net_say(const std::string& text) {
  std::vector<uint8_t> m(8 + text.size() + 1, 0);
  const uint16_t type = 0x1f, size = static_cast<uint16_t>(m.size());
  std::memcpy(m.data(), &type, 2);
  std::memcpy(m.data() + 2, &size, 2);
  std::memcpy(m.data() + 8, text.data(), text.size());
  znet::send(m.data(), size, false, 0);
}

// --install-from (ours, disc_install.h): the disc's files into a folder of
// our own, no Windows installer run, the patch with it when given (then not
// the overlay), the folder saved as mw3.cfg's install=. False: reported.
bool install_from_disc(Args& a) {
  const std::string out = !a.install_to.empty() ? a.install_to : a.write_dir.empty() ? std::string() : a.write_dir + "/install";
  mech3::setup::Result r;
  if (out.empty()) {
    r.summary = "There is no settings folder: give --install-to DIR.";
  } else {
    std::printf("mw3: installing MechWarrior 3 from %s into %s\n", a.install_from.c_str(), out.c_str());
    std::string stage;
    int tenth = -1;
    r = mech3::setup::install_from_disc(a.install_from, out, a.patch, a.write_dir.empty() ? std::string() : a.write_dir + "/music",
                                        [&](const mech3::setup::Progress& p) {
                                          const int t = static_cast<int>(p.fraction * 10);
                                          if (p.stage != stage || t != tenth)
                                            std::printf("mw3: %s (%d%%)\n", p.stage.c_str(), static_cast<int>(p.fraction * 100));
                                          stage = p.stage, tenth = t;
                                          return true;
                                        });
  }
  if (a.verbose) std::printf("%s", r.detail.c_str());
  for (const std::string& w : r.warnings) std::printf("mw3: %s\n", w.c_str());
  if (!r.ok) {
    std::fprintf(stderr, "mw3: %s\n", r.summary.c_str());
    if (!a.verbose && !r.detail.empty()) std::fprintf(stderr, "%s", r.detail.c_str());
    if (!a.hidden) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MechWarrior 3", r.summary.c_str(), nullptr);
    return false;
  }
  std::printf("mw3: %s\n", r.summary.c_str());
  a.install = out;
  a.patch.clear();  // in the install now
  if (!a.write_dir.empty() && sdlfe::save_install(a.write_dir, out)) std::printf("mw3: install %s saved to mw3.cfg\n", out.c_str());
  return true;
}

// No install named on the command line: mw3.cfg's, else the search and the
// dialog (sdl_install.h; ours, FIX (A)); one found by search is saved to
// mw3.cfg. Checks and shots never ask. False: quit (cancelled, or none found
// with no dialog allowed, reported).
bool find_install(Args& a) {
  const bool may_ask = !a.hidden && a.frames == 0 && a.shot.empty() && a.chain.empty() && a.modes.empty();
  std::vector<std::string> dev;
#ifdef MW3_GAME_DIR
  if (!std::getenv("MW3_FIND_INSTALL")) dev.push_back(MW3_GAME_DIR);  // a development build's copy (not with MW3_FIND_INSTALL=1)
#endif
  bool searched = false;
  a.install = sdlfe::find_install(a.cfg_install, dev, may_ask, &searched);
  a.first_start = searched;
  if (a.install.empty()) {
    if (may_ask) std::printf("mw3: no install chosen\n");
    else std::fprintf(stderr, "mw3: No MechWarrior 3 install found (mw3.cfg's install=, the installer's registry "
                                "values and folders). Start mw3 with the folder: mw3 \"DIR\" (README.txt).\n");
    return false;
  }
  if (searched && (dev.empty() || a.install != dev[0]) && sdlfe::save_install(a.write_dir, a.install))
    std::printf("mw3: install %s saved to mw3.cfg\n", a.install.c_str());
  return true;
}

// The Indeo helper: --vfw-helper's, else on Windows mw3indeo.exe beside mw3.
std::string vfw_helper_path(const std::string& given) {
  if (given == "-") return {};
  if (!given.empty()) return given;
#if defined(_WIN32)
  if (const char* base = SDL_GetBasePath()) {
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::path(base) / "mw3indeo.exe";
    if (std::filesystem::exists(p, ec)) return p.string();
  }
#endif
  return {};
}

#if defined(_WIN32)
constexpr const char* kImporter = "mw3import.exe";
#else
constexpr const char* kImporter = "mw3import";
#endif

// The notes for a player's start (ours): the data's version when it is not
// 1.2's, and no music. Lines for a box; flat() makes one for the log.
std::vector<std::string> start_notes(const mech3::DataVersion& d, const std::string& write_dir) {
  std::vector<std::string> out;
  auto list = [](const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size() && i < 6; ++i) s += (i ? ", " : "") + v[i];
    if (v.size() > 6) s += ", ...";
    return s;
  };
  // 1.0's data and a mix need no note: they load as 1.2 (docs/patch12.md).
  if (d.name == "unknown")
    out.push_back(mech3::mod_note(list(d.unknown)));
  else if (d.name == "incomplete")
    out.push_back("Some MechWarrior 3 files are missing from the install: " + list(d.unknown) +
                  ". Reinstall MechWarrior 3 from its disc.");
  bool music = false;
  for (int t = 2; t <= 20 && !music; ++t)
    for (const char* ext : {"ogg", "flac", "wav", "mp3"}) {
      char name[32];
      std::snprintf(name, sizeof name, "music/track%02d.%s", t, ext);
      if (zutl::exists(name)) music = true;
    }
  if (!music)
    out.push_back(std::string("No music: the original played it from the CD's audio tracks, which an install does not have. "
                              "With a disc image (a .cue and its .bin):\n    ") +
                  kImporter + " --cue \"<the disc's .cue>\" --music-only\nor put your own rip in " +
                  (write_dir.empty() ? std::string("music") : (std::filesystem::path(write_dir) / "music").make_preferred().string()) +
                  " as track02.ogg, track03.ogg and so on (README.txt, Music).");
  return out;
}
std::string flat(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '\n') c = ' ';
    if (c != ' ' || out.empty() || out.back() != ' ') out += c;
  }
  return out;
}

// The install's Indeo 5 codec, any case.
std::string codec_path(const std::string& install) {
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(install, ec)) {
    std::string n = e.path().filename().string();
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n == "ir50_32.dll") return e.path().string();
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  if (sdlfe::crash_box_main(argc, argv)) return 0;
  // In order with the log on stderr. Windows' CRT has no line buffering (and takes size 0 as
  // an invalid argument): unbuffered there.
#if defined(_WIN32)
  std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
  Args a;
  if (!parse(argc, argv, a)) return 2;
  if (!a.shot.empty()) a.hidden = true;
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK)) {
    sdlfe::fatal_box(std::string("MechWarrior 3 cannot start: ") + SDL_GetError(), a.hidden);
    return 1;
  }
  // The write dir: saves and settings, outside the install (assets-layer.md).
  // --write-dir wins; else the usual folder, or the one its pointer names (data_folder.h).
  const bool write_dir_given = !a.write_dir.empty();
  if (!write_dir_given && !sdlfe::data_folder_at_start(&a.write_dir, !a.hidden)) {
    SDL_Quit();
    return 1;
  }
  while (a.write_dir.size() > 1 && a.write_dir.back() == '/') a.write_dir.pop_back();
  // A --write-dir that does not exist is made, as SDL_GetPrefPath makes the default.
  if (!a.write_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(a.write_dir, ec);
    if (ec) std::fprintf(stderr, "mw3: cannot make the write dir %s: %s\n", a.write_dir.c_str(), ec.message().c_str());
  }
  // Everything printed from here also goes to <write dir>/mw3.log, the last
  // start's kept as mw3.old.log (ours): with no console (the Windows package
  // build) stdout and stderr go to the file (log_to_file), else they are
  // copied to it (run_log.h).
  sdlfe::RunLog run_log;
  if (sdlfe::log_to_file(a.write_dir))
    std::printf("mw3 (MechWarrior 3 rebuild) %s: %s\n", MW3_VERSION, argc > 1 ? "started with arguments" : "started");
  else if (!a.write_dir.empty() && run_log.open(a.write_dir))
    std::printf("mw3 (MechWarrior 3 rebuild) %s, log %s\n", MW3_VERSION, run_log.path().c_str());
  if (!a.hidden && !a.write_dir.empty()) sdlfe::crash_report_install("mw3", a.write_dir + "/mw3.log", true);
  if (!a.write_dir.empty()) read_config(a.write_dir, a);
  if (!a.mods_given) a.mods = a.cfg_mods;
  if (a.no_mods) a.mods.clear();
  if (!a.check_mod.empty()) {  // read the mod as the game would, nothing mounted into play
    int errors = 0;
    const std::string install = !a.install.empty() ? a.install : a.cfg_install;
    for (const std::string& line : mech3::mod_check(a.check_mod, install, &errors)) std::printf("%s\n", line.c_str());
    SDL_Quit();
    return errors ? 1 : 0;
  }
  if (a.list_mods) {
    const std::vector<mech3::ModInfo> found = mech3::mods_found(a.write_dir);
    std::printf("mw3: mods in %s/mods: %zu\n", a.write_dir.c_str(), found.size());
    for (const mech3::ModInfo& m : found) {
      const auto at = std::find(a.mods.begin(), a.mods.end(), m.folder);
      std::printf("  %s: %s%s%s\n", m.folder.c_str(), m.error.empty() ? (m.name + " " + m.version).c_str() : m.error.c_str(),
                  at == a.mods.end() ? "" : ", enabled #", at == a.mods.end() ? "" : std::to_string(at - a.mods.begin() + 1).c_str());
    }
    SDL_Quit();
    return 0;
  }
  // Every play-changing bug fix off with --original or bugfixes=0 (ours; the
  // platform fixes that let it run stay on: fixes.md, The bug fixes switch).
  zfix::options().bugs = !a.play_original && a.cfg_bugfixes != 0;
  // The menus' 16:9 (a display choice, kept by --original): the command
  // line's, else mw3.cfg's widemenus=, else on.
  zfix::options().wide_menus = a.wide_menus >= 0 ? a.wide_menus != 0 : a.cfg_wide_menus != 0;
  zfix::options().throttle_reverse = a.cfg_throttle_reverse != 0;  // with bugs (lever_reverse_on)
  mech3::app_key_calls().debug_camera = a.debug_camera;
  if (a.debug_camera) std::printf("mw3: --debug-camera: the game editor on Shift+F12 (docs/editor.md)\n");
  if (!zfix::options().bugs)
    std::printf("mw3: bug fixes off (%s): the original's play\n", a.play_original ? "--original" : "bugfixes=0 in mw3.cfg");
  // The launcher (ours: launcher.h, docs/launcher.md, games-layout.md): a
  // player's plain start goes straight into the game when an install is
  // found, else to the find screen alone and on into the game once there is
  // one (the launcher's main screen is mw3launcher's). Checks, shots, debug starts and a
  // named install or source go straight on.
  const bool player_start = !a.hidden && a.frames == 0 && a.shot.empty() && a.chain.empty() && a.modes.empty() &&
                            a.mission.empty() && !a.fly && a.dialog_file.empty() && a.movie.empty() && a.install.empty() &&
                            a.install_from.empty() && a.patch.empty() && a.original.find("-nofmv") == std::string::npos;
  bool setup_only = false;
  if (!a.ready_file.empty()) sdlfe::set_ready_file(a.ready_file);
  if (player_start && !a.no_launcher) {
    std::vector<std::string> more;
#ifdef MW3_GAME_DIR
    if (!std::getenv("MW3_FIND_INSTALL")) more.push_back(MW3_GAME_DIR);
#endif
    if (!a.write_dir.empty()) more.push_back(a.write_dir + "/install");  // SETUP's own
    bool searched = false;
    const std::string found = sdlfe::find_install(a.cfg_install, more, false, &searched);
    if (!found.empty()) {
      a.install = found, a.first_start = searched;
      if (searched && !a.write_dir.empty() && sdlfe::save_install(a.write_dir, found))
        std::printf("mw3: install %s saved to mw3.cfg\n", found.c_str());
    } else {
      setup_only = true;
      std::printf("mw3: no install found: the find screen\n");
    }
  }
  if (setup_only) {
    sdlfe::LauncherOptions lo;
    lo.setup_only = setup_only;
    lo.write_dir_fixed = write_dir_given;
    lo.write_dir = a.write_dir, lo.cfg_install = a.cfg_install, lo.cfg_mw1_game = a.cfg_mw1_game;
#ifdef MW3_GAME_DIR
    if (!std::getenv("MW3_FIND_INSTALL")) lo.more_installs.push_back(MW3_GAME_DIR);
#endif
    lo.fullscreen = !a.windowed && !a.window_given;
    lo.sound = !a.nosound;
    lo.no_import = a.no_import;
    lo.win_w = a.win_w, lo.win_h = a.win_h;
    const sdlfe::LauncherChoice c = sdlfe::run_launcher(lo);
    if (c.kind != sdlfe::LauncherChoice::kPlay) {
      SDL_Quit();
      return 0;
    }
    a.install = c.install;
    a.launcher_window = c.window;
    if (setup_only) a.first_start = true;
  }
  if (std::getenv("MW3_START_ONLY")) {  // checks: where a start goes, and no further
    std::printf("mw3: start only: install %s\n", a.install.empty() ? "(none yet)" : a.install.c_str());
    SDL_Quit();
    return 0;
  }
  if (!a.install_from.empty() && !install_from_disc(a)) {
    SDL_Quit();
    return 1;
  }
  if (a.install.empty() && !find_install(a)) {
    SDL_Quit();
    return 1;
  }
  for (const std::string& d : a.assets) zutl::mount(d);
  // The 1.2 patch's files, imported (ours: patch_import.h), over the install's.
  const std::string patch_dir = a.write_dir.empty() ? std::string() : a.write_dir + "/patch12";
  // The enabled mods, in front of the patch and the install (docs/modding.md).
  mech3::mods_identity_set(a.write_dir, a.mods);  // for the lobby: the gameplay mods every player must share
  if (!a.mods.empty())
    for (const std::string& line : mech3::mods_mount(a.write_dir, a.mods, {patch_dir, a.install}))
      std::printf("mw3: %s\n", line.c_str());
  if (!a.patch.empty()) {
    const mech3::setup::Result r = mech3::setup::apply_patch(a.patch, a.write_dir, nullptr);
    if (a.verbose) std::printf("%s", r.detail.c_str());
    if (!r.ok) {
      std::fprintf(stderr, "mw3: %s\n", r.summary.c_str());
      if (!a.hidden) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MechWarrior 3", r.summary.c_str(), nullptr);
      SDL_Quit();
      return 1;
    }
    std::printf("mw3: %s\n", r.summary.c_str());
  }
  std::error_code patch_ec;
  if (!patch_dir.empty() && std::filesystem::is_directory(patch_dir, patch_ec)) zutl::mount(patch_dir);
  zutl::mount(a.install);
  zutl::set_write_dir(a.write_dir);
  zutl::mounts().confined = true;  // ours: names from data and the network stay inside these folders
  std::error_code ec;
  if (zutl::any_case(a.install, "zbd/reader.zbd").empty()) {
    const std::string msg = "No MechWarrior 3 install at \"" + a.install +
                            "\". Start mw3 with the folder that holds Mech3.exe and zbd (README.txt).";
    std::fprintf(stderr, "mw3: %s\n", msg.c_str());
    if (!a.hidden) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MechWarrior 3", msg.c_str(), nullptr);
    SDL_Quit();
    return 1;
  }
  std::printf("mw3: install %s, write dir %s\n", a.install.c_str(), a.write_dir.c_str());
  const mech3::DataVersion data = mech3::detect_data_version();
  std::printf("mw3: %s\n", data.describe().c_str());  // ours: assets.md, Data versions
  // The movies on a first run (ours, FIX (A): first_run_import.h): on a
  // player's plain start, not on checks, shots or debug starts, nor -nofmv.
  const bool plain = !a.hidden && a.frames == 0 && a.shot.empty() && a.chain.empty() && a.modes.empty() && a.mission.empty() && !a.fly &&
                     a.dialog_file.empty() && a.movie.empty() && a.original.find("-nofmv") == std::string::npos;
  // The movies through Windows' own Video for Windows and the install's Indeo
  // 5 codec in the 32-bit helper (ours, FIX (A): sdl_vfw.h); the importer's
  // files only when it is missing or fails.
  std::unique_ptr<sdlfe::SdlVfwHelper> vfw;
  const std::string helper = vfw_helper_path(a.vfw_helper), codec = codec_path(a.install);
  if (!helper.empty() && !codec.empty()) {
    vfw = std::make_unique<sdlfe::SdlVfwHelper>(helper, codec);
    const bool up = vfw->ready();
    std::printf("mw3: movies %s (%s)\n", up ? "through the Indeo helper" : "not through the Indeo helper", vfw->hello().c_str());
    if (!up) vfw.reset();
  }
  // What a player may want to do about the data and the music: in the log
  // each start, in a box on the first (ours).
  const std::vector<std::string> notes = start_notes(data, a.write_dir);
  for (const std::string& n : notes) std::printf("mw3: note: %s\n", flat(n).c_str());
  if (plain && a.first_start && !notes.empty()) {
    std::string text;
    for (const std::string& n : notes) text += (text.empty() ? "" : "\n\n") + n;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "MechWarrior 3: first start", text.c_str(), nullptr);
  }
  if (a.force_import || (plain && !a.no_import && !vfw)) sdlfe::first_run_import(a.install, a.write_dir, a.force_import);

  // The network's session, before the game (ours, piece 4).
  std::unique_ptr<sdlfe::EnetTransport> net;
  if (a.net_host || !a.net_join.empty()) {
    sdlfe::NetOptions o;
    o.bind = a.net_bind;
    o.port = static_cast<uint16_t>(a.net_port >= 0 ? a.net_port : (a.net_host ? sdlfe::kDefaultPort : 0));
    net = std::make_unique<sdlfe::EnetTransport>(o);
    if (!net->ok()) {
      std::printf("mw3: network: cannot use port %u on %s\n", o.port, o.bind.c_str());
    } else {
      znet::initialise(net.get());
      net_log_handlers();
      bool ok;
      if (a.net_host) {
        znet::SessionDesc d;
        d.name = a.net_name + "'s game";
        ok = znet::host_session(d, a.net_name.c_str());
      } else {
        ok = znet::join_session(a.net_join, a.net_name.c_str());
      }
      if (ok)
        std::printf("mw3: network: %s on port %u as player %u (%s), the host %u\n",
                    a.net_host ? "hosting" : ("joined " + a.net_join).c_str(), net->port(), znet::local_id(),
                    a.net_name.c_str(), znet::session().host_id);
      else
        std::printf("mw3: network: %s failed\n", a.net_host ? "hosting" : ("joining " + a.net_join).c_str());
      // --net-wait N: the start held (up to two minutes) until N players are in.
      if (ok && a.net_wait > 0) {
        const uint64_t until = SDL_GetTicks() + 120000;
        while (znet::lists().players.size() < static_cast<size_t>(a.net_wait) && SDL_GetTicks() < until) {
          znet::poll(-1);
          SDL_Delay(10);
        }
        std::printf("mw3: network: %zu players in\n", znet::lists().players.size());
      }
      // Each pass: --net-say's line (the transport answers the LAN's search itself).
      g_net_pass = [&a] {
        static int passes = 0;
        ++passes;
        if (!a.net_say.empty() && passes == a.net_say_at) net_say(a.net_say);
      };
    }
  }
  int result = 1;
  try {
    result = run(a, vfw.get());
    std::printf("mw3: closed, result %d\n", result);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "mw3: rendering failed: %s\n", error.what());
  }
  g_net_pass = nullptr;
  vfw.reset();
  SDL_Quit();  // after every SDL object above is gone
  return result;
}

namespace {

int run(const Args& a, platform::VideoForWindows* vfw) {
  sdlfe::SdlWindow window;
  if (a.backend == sdlfe::RendererBackend::sdl &&
      (a.quality.antialiasing != "off" || a.quality.anisotropy != "1" || !a.cc_assets.empty()))
    return std::fprintf(stderr, "mw3: Core Critical options require a Core Critical Vulkan backend\n"), 1;
  if (a.backend != sdlfe::RendererBackend::sdl && !a.windowed && !a.window_given)
    return std::fprintf(stderr, "mw3: Core Critical Vulkan requires --windowed or --window WxH\n"), 1;
  auto selected_assets = a.cc_assets;
  if (!selected_assets.environment_packs.empty())
    std::fprintf(stderr, "Environment packs requested: %zu\n",
                 selected_assets.environment_packs.size());
  if (!selected_assets.environment_packs.empty())
    selected_assets.environment =
        std::make_shared<const platform::prepared::EnvironmentSelection>(
            platform::prepared::load_environment_packs(
                selected_assets.environment_packs));
  if (selected_assets.environment)
    std::fprintf(stderr,
                 "Environment packs admitted: %zu regions, %zu banks, %zu "
                 "authored and %zu world catalogs\n",
                 selected_assets.environment->regions.size(),
                 selected_assets.environment->banks.size(),
                 selected_assets.environment->authored_manifests.size(),
                 selected_assets.environment->world_manifests.size());
  struct BankScope {
    std::shared_ptr<const zutl::BankOverrides> previous;
    ~BankScope() { zutl::set_bank_overrides(std::move(previous)); }
  } bank_scope{zutl::set_bank_overrides(
      selected_assets.environment ? std::make_shared<const zutl::BankOverrides>(
                                        selected_assets.environment->banks)
                                  : nullptr)};
  if (!window.init(a.hidden, a.launcher_window, a.backend, a.quality,
                   selected_assets)) {
    // The reason is in the line before this (SdlWindow::init).
    sdlfe::fatal_box(std::string("MechWarrior 3 could not open its window or graphics (Vulkan on Linux): ") + SDL_GetError(), a.hidden);
    return 1;
  }
  sdlfe::SdlInput input;
  input.joystick_want = a.cfg_joystick, input.joystick_axes = a.cfg_joystick_axes;
  sdlfe::SdlForceDevice force([&input] { return input.joystick(); });  // FIX (A): IFORCE2 as rumble (sdl_force.h)
  input.window = window.sdl();
  input.mickey_scale = a.win_w > 0 ? 640.0f / static_cast<float>(a.win_w) : 1.0f;
  input.shape = &window.shape();       // any_resolution: the frame's rect, not 640 (sdl_fit.h)
  window.set_auto_size(!a.window_given);
  // Fullscreen by default (ours): the desktop's size, the frame fitted in it
  // (sdl_fit.h). Checks and shots stay in a window.
  window.set_fullscreen(!a.windowed && !a.window_given && !a.hidden && a.frames == 0 && a.chain.empty() && a.modes.empty());
  // FIX (B): a first start's mission at the desktop's size; checks and shots keep 640x480.
  window.set_first_start_desktop(!a.hidden && a.frames == 0 && a.chain.empty() && a.modes.empty());
  sdlfe::SdlZinHost zin_host(&input);
  zin::set_host(&zin_host);
  sdlfe::SdlClock clock;
  clock.frame_ms = static_cast<uint32_t>(a.frame_ms);
  // --seed: every srand(time()) takes N instead (with --frame-ms a run repeats).
  if (a.seed >= 0) {
    crt::rand_fixed_seed() = static_cast<uint32_t>(a.seed);
    std::printf("mw3: rand seeded with %lld\n", static_cast<long long>(a.seed));
  }
  // --movie with --nosound: the movie's clock steps a quarter frame (1/60 s)
  // each time it is read, the silent sound's cursor follows the same time.
  struct StepClock : platform::Clock {
    double ms = 0;
    uint32_t ticks_ms() override { return static_cast<uint32_t>(ms += 1000.0 / 60.0); }
  } movie_clock;
  struct ReadClock : platform::Clock {
    const StepClock* c = nullptr;
    uint32_t ticks_ms() override { return static_cast<uint32_t>(c->ms); }
  } sound_clock;
  sound_clock.c = &movie_clock;
  const bool stepped = !a.movie.empty() && a.nosound;
  sdlfe::SdlAlert alert(window.sdl(), a.hidden);
  sdlfe::SdlScreen2D screen(window.render(), a.hud, window.sdl(), &window.shape());
  std::unique_ptr<platform::SoundOutput> sound;
  if (a.nosound) {
    auto silent = std::make_unique<platform::SilentSoundOutput>();
    silent->clock = stepped ? static_cast<platform::Clock*>(&sound_clock) : &clock;  // the movies' sound sets their frame
    sound = std::move(silent);
  }
  else sound = std::make_unique<sdlfe::SdlSoundOutput>();
  sdlfe::SdlMusicOutput music;
  mech3::MountFiles files;

  mech3::Platform p;
  p.window = &window, p.alert = &alert, p.clock = &clock, p.input = &input, p.force = &force, p.vfw = vfw, p.sound = sound.get();
  if (stepped) p.movie_clock = &movie_clock;
  p.music = &music, p.files = &files, p.render = window.render(), p.screen = &screen;
  p.window_width = a.win_w, p.window_height = a.win_h;
  // The registry's keys as a file in the write dir (docs/settings.md).
  platform::RegFile settings(a.write_dir.empty() ? std::string() : a.write_dir + "/settings.reg");
  if (!a.write_dir.empty()) p.settings = &settings;
  p.ram_mb = static_cast<uint32_t>(SDL_GetSystemRAM());
  // The multiplayer screens' transport (FIX (A): DirectPlay's instance): UDP
  // 27930, or a port the system picks when it is taken (another game here).
  std::unique_ptr<sdlfe::EnetTransport> menu_net;
  std::unique_ptr<sdlfe::PortMapper> menu_upnp;  // after menu_net: gone first, the mapping removed
  p.network_open = [&menu_net, &menu_upnp, &a]() -> znet::Transport* {
    sdlfe::NetOptions o;
    o.bind = a.net_bind;
    o.port = static_cast<uint16_t>(a.net_port >= 0 ? a.net_port : sdlfe::kDefaultPort);
    o.discovery_port = a.net_discovery;
    menu_net = std::make_unique<sdlfe::EnetTransport>(o);
    if (!menu_net->ok()) {
      o.port = 0;
      menu_net = std::make_unique<sdlfe::EnetTransport>(o);
    }
    if (!menu_net->ok()) {
      menu_net.reset();
      return nullptr;
    }
    if (!a.net_lan.empty()) {  // the LAN's probe to this address and port
      const size_t colon = a.net_lan.rfind(':');
      menu_net->lan_address = a.net_lan.substr(0, colon);
      if (colon != std::string::npos) menu_net->search_port = static_cast<uint16_t>(std::atoi(a.net_lan.c_str() + colon + 1));
    }
    std::printf("mw3: network: up on UDP port %u\n", menu_net->port());
    // Internet hosting (piece 9c): the router asked over UPnP, unless this
    // machine is reachable on loopback alone (the tests) and no router is given.
    const bool loopback = a.net_bind.rfind("127.", 0) == 0;
    if (!loopback || !a.net_upnp.empty()) menu_upnp = std::make_unique<sdlfe::PortMapper>(menu_net->port(), a.net_upnp);
    return menu_net.get();
  };
  p.network_close = [&menu_net, &menu_upnp] {
    menu_upnp.reset();
    menu_net.reset();
  };
  p.network_reach = [&menu_net, &menu_upnp, &a]() {
    mech3::Engine::NetReach r;
    r.port = menu_net ? menu_net->port() : sdlfe::kDefaultPort;
    const std::string port = ":" + std::to_string(r.port);
    std::string lan = a.net_bind.rfind("127.", 0) == 0 ? a.net_bind : sdlfe::local_lan_address();
    if (menu_upnp) {
      const sdlfe::UpnpStatus s = menu_upnp->status();
      r.state = static_cast<int32_t>(s.state);  // the same order
      if (!s.lan_ip.empty()) lan = s.lan_ip;
      if (!s.public_ip.empty() && !sdlfe::unreachable_address(s.public_ip)) r.internet = s.public_ip + port;
    } else {
      r.state = mech3::Engine::NetReach::kNoRouter;
    }
    if (!lan.empty()) r.lan = lan + port;
    return r;
  };
  p.network_hosting = [&menu_upnp](bool on) {
    if (!menu_upnp) return;
    if (on) menu_upnp->map();
    else menu_upnp->unmap();
  };
  p.copy_text = [](const char* text) { SDL_SetClipboardText(text); };

  int result = 1;
  {
    mech3::GameEngine engine(p, "mech3.exe" + a.original);
    sdlfe::SdlTwodHost host(&engine, &input, &window);
    host.enable_hud_text(a.hud.mode == "core-critical" && a.hud.text);
    ztwod::set_host(&host);
    mech3::reset_states();
    mech3::Mech3App app(&engine);
    mech3::set_app(&app);
    // FIX (A), ours: in a shown window the menus leave the mouse free and a
    // mission's can be let go (sdl_input.h); fullscreen and hidden runs hold it.
    input.windowed = [&window, hidden = a.hidden] { return !hidden && !window.fullscreen(); };
    input.in_mission = [&app] { return app.current() == &mech3::states().core; };
    input.absolute_cursor = [](float x, float y) {
      zin::mouse_from_cursor(static_cast<int32_t>(std::lround(x)), static_cast<int32_t>(std::lround(y)));
    };

    std::unique_ptr<mech3::State> debug;
    std::unique_ptr<mech3::ActionGame> ia_game;  // --mission ia<N>: the launch's game
    std::unique_ptr<mech3::MultiplayerGame> multi_game;  // --mission <dir>mp<N>: the launch's
    std::vector<double> frame_ms;  // --frame-times: each frame's wall time
    const char* benchmark_timestamps = std::getenv("MW3_BENCHMARK_TIMESTAMPS");
    struct FrameTimestamp { int frame; uint64_t counter; };
    std::vector<FrameTimestamp> frame_timestamps;
    if (benchmark_timestamps) {
      if (a.frame_times <= 0) throw std::runtime_error("Benchmark timestamps require --frame-times");
      frame_timestamps.reserve(a.frames > 0 ? std::size_t(a.frames) : 4096);
      std::printf("mw3: benchmark main thread %llu, counter frequency %llu\n",
                  (unsigned long long)SDL_GetCurrentThreadID(),
                  (unsigned long long)SDL_GetPerformanceFrequency());
      std::fflush(stdout);
    }
    uint64_t frame_last = 0;
    auto record_frame_time = [&](int frame) {
      if (a.frame_times <= 0 || frame < a.frame_times) return;
      const uint64_t now = SDL_GetPerformanceCounter();
      if (benchmark_timestamps) frame_timestamps.push_back({frame,now});
      if (frame_last) frame_ms.push_back(static_cast<double>(now - frame_last) * 1000.0 / SDL_GetPerformanceFrequency());
      frame_last = now;
    };
    sdlfe::LightSimulationTrace light_trace(
        a.frames, !a.mission.empty() && !a.fly && !a.shot_any);
    bool launch = false, launch_done = false;
    if (!a.mission.empty() && !a.fly) {
      // Through the original's states: the Load state loads the mission
      // (mech3/mission), then the Core state runs it (docs/mission-load.md).
      std::string dir, variant;
      mech3::GameEngine::Launch l;
      // --mission ia<N>[w<W>] (ours, for measuring): Instant Action map N in
      // wave mode from wave W (1), four allies (Champions) with the player,
      // as the Action screen would set it (the AI survey's start).
      // <dir>ia<N>[w<W>] the same on another world (c3ia2: c3's readeria2).
      int ia_map = 0, ia_wave = 1;
      const size_t ia_at = a.mission.find("ia");
      if (ia_at != std::string::npos &&
          std::sscanf(a.mission.c_str() + ia_at, "ia%dw%d", &ia_map, &ia_wave) >= 1 && ia_map > 0) {
        ia_game = std::make_unique<mech3::ActionGame>(nullptr);
        ia_game->mode = 2, ia_game->x6c = ia_wave, ia_game->slot.mech = 9;
        for (int i = 0; i < 4; ++i) ia_game->add(5, 1, nullptr, 0);
        l.type = 2, l.number = ia_map, l.action = ia_game.get();
        dir = ia_at ? a.mission.substr(0, ia_at) : "c1";
      } else if (const size_t mp = a.mission.rfind("mp");
                 mp != std::string::npos && mp > 0 && mp + 2 < a.mission.size() &&
                 a.mission.find_first_not_of("0123456789", mp + 2) == std::string::npos) {
        // --mission <dir>mp<N> (ours, the soak test's): a network game on the
        // map (readermp<N>), its game a stand-in with no score limit to reach.
        if (!znet::active()) {
          std::fprintf(stderr, "mw3: --mission %s: a network game needs --host or --join\n", a.mission.c_str());
          return 2;
        }
        dir = a.mission.substr(0, mp), l.number = std::atoi(a.mission.c_str() + mp + 2);
        multi_game = std::make_unique<mech3::MultiplayerGame>(nullptr);
        multi_game->x70 = 1 << 30;  // the kills to win (+0x70)
        l.type = 3, l.multi = multi_game.get();
        mech3::program_net_debug_lobby();
      } else {
        split_mission(a.mission, dir, variant, l.number);
      }
      l.index = -1;
      for (int32_t i = 0; i < 9; ++i)
        if (dir == mech3::mission_dir(i)) l.index = i;
      if (l.index < 0) {
        std::fprintf(stderr, "mw3: --mission %s: not a mission (c1 c2 c3 c4 c4b t1)\n", a.mission.c_str());
        return 2;
      }
      // The mission's reader archive (mission.cpp's reader_open): c1 and c2
      // have m1..m4, c3 m1..m6, c4 m1..m3, c4b m4..m6. Without it the world
      // loads with no player and no mech.
      const std::string reader = "zbd\\" + dir + (l.type == 3 ? "\\readermp" : "\\readerm") + std::to_string(l.number) + ".zbd";
      if (!ia_game && !zutl::exists(reader.c_str())) {
        std::fprintf(stderr, "mw3: --mission %s: no such mission (%s not found)\n", a.mission.c_str(), reader.c_str());
        return 2;
      }
      // The operation the objectives box names ("Operation %d, Mission %d",
      // playtest S2): a campaign start has it from the campaign; here by the
      // directory, as --briefing's (c4b is operation 4's second half).
      if (dir == "c2") l.op = 2;
      else if (dir == "c3") l.op = 3;
      else if (dir == "c4" || dir == "c4b") l.op = 4;
      l.have_pos = a.have_pos;
      for (int i = 0; i < 3; ++i) l.eye[i] = a.pos[i];
      l.yaw = a.look[0] * 0.0174533f, l.pitch = a.look[1] * 0.0174533f;
      engine.set_launch(l);
      launch = true;
      const bool l_type3 = l.type == 3;
      int shots = 0;
      // --fire N (ours, for checking the weapons' effects): from the N-th frame
      // shown, the fire command reads as held.
      static bool s_fire = false;
      if (a.fire > 0)
        mech3::controls_calls().command = [](int cmd) {
          return zin::command(cmd) | (s_fire && cmd == 0x3d ? 1u : 0u);
        };
      // --net-drive SEED (ours, the soak test's): the pilot's commands a pattern
      // from the first Core frame on (app_mech.h).
      static double s_drive_from = -1;
      static uint32_t s_drive_seed = 0;
      if (a.net_drive >= 0) {
        s_drive_seed = static_cast<uint32_t>(a.net_drive);
        mech3::controls_calls().command = [](int cmd) {
          if (s_drive_from < 0) return zin::command(cmd);
          return zin::command(cmd) | mech3::program_net_drive(cmd, SDL_GetTicks() / 1000.0 - s_drive_from, s_drive_seed);
        };
      }
      bool net_core = false, net_over = false;
      uint64_t net_line_at = 0;
      // The callback runs after this launch-configuration scope has ended.
      engine.on_present = [&, shots, l_type3, net_core, net_over, net_line_at](int) mutable {
        // The soak test's (ours): the clock and the drive from the first Core
        // frame, the lines each half second, the scores at the end, the quit
        // at the menu after it.
        if (l_type3 && app.current() == &mech3::states().core && !net_core) {
          net_core = true;
          s_drive_from = SDL_GetTicks() / 1000.0;
          if (a.net_minutes > 0) mech3::missions().clock = a.net_minutes * 60.0f, mech3::missions().clock_down = 1;
        }
        // --net-drive: a kill staged on the pilot every two minutes, from a
        // minute in, staggered by the seed (the scores' traffic, the respawn).
        static int s_kills = 0;
        if (a.net_drive >= 0 && a.net_staged && net_core && !net_over && app.current() == &mech3::states().core &&
            SDL_GetTicks() / 1000.0 - s_drive_from >= 60.0 + 29.0 * (a.net_drive % 4) + 120.0 * s_kills)
          if (mech3::program_net_staged_kill()) std::printf("mw3: soak staged kill %d\n", ++s_kills);
        if (a.net_ahead > 0 && net_core && !net_over && app.current() == &mech3::states().core)
          mech3::program_net_hold_near(a.net_ahead);
        if (a.net_soak && net_core && !net_over) {
          const uint64_t now = SDL_GetTicks();
          if (app.current() == &mech3::states().core && now >= net_line_at) {
            net_line_at = now + 500;
            const long long wall = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch()).count();
            zwep::PoolStats ps;
            zwep::pool_stats(&ps);
            std::printf("mw3: soak %lld rss %zu clock %.1f shots %d %s\n", wall, sdlfe::resident_kb(), mech3::missions().clock,
                        ps.flying, mech3::program_net_soak_line().c_str());
          }
          // The end: the fade, the Multi-Exit State and the main menu, which
          // goes on to the Multiplayer State (unported until piece 9) in the
          // same frame: any state but these two (or the pause) after Core.
          const zgame::State* st = app.current();
          if (st != &mech3::states().core && st != &mech3::states().fade && st != &mech3::states().pause) {
            net_over = launch_done = true;
            std::printf("mw3: soak game over, scores %s\n", mech3::program_net_scores().c_str());
          }
        }
        // --verbose (ours): each state the app moves to, by name, as the menus' driver prints them.
        static const zgame::State* launch_state = nullptr;
        if (a.verbose && app.current() != launch_state) {
          launch_state = app.current();
          if (auto* st = dynamic_cast<const mech3::State*>(launch_state)) std::printf("mw3: state %s (frame %d)\n", st->name(), shots);
        }
        // Counted from the first frame with the scene: then every frame shown
        // (the pause and end screens draw none, and --frames must still end the run).
        if (!a.shot_any && !engine.drew_scene() && shots == 0) return;
        ++shots;
        record_frame_time(shots);
        // --vehicle-ahead (ours, for checking a vehicle up close: its shadow, its model, its weapons).
        static void* held = nullptr;
        static float held_at[3];
        mech3::VehicleSummary hs;
        if (held && a.vehicle_hold && mech3::is_program_vehicle(held) && mech3::describe_vehicle(held, &hs))
          if (void* m = mech3::mech_mission_player())  // the player kept where it was, facing it (yaw from -z)
            mech3::move_mech(m, held_at, std::atan2(held_at[0] - hs.pos[0], held_at[2] - hs.pos[2]));
        if (a.vehicle_ahead > 0 && shots == 1)
          if (void* m = mech3::mech_mission_player()) {
            float p[3] = {}, f[3] = {};
            mech3::mech_world_position(m, p);
            mech3::mech_forward(m, f);
            // An armed one first (its weapons, weapons.md), else the first.
            bool placed = false;
            for (int armed = 1; armed >= 0 && !placed; --armed)
              for (void* v : mech3::program_vehicles()) {
                mech3::VehicleSummary vs;
                if (!mech3::describe_vehicle(v, &vs) || !vs.in_world) continue;
                if (a.field_base_fixture && !vs.field_base)
                  continue;
                if (armed && mech3::wm_weapon_count(mech3::mech_weapon_manager(v)) == 0) continue;
                const float to[3] = {p[0] + f[0] * a.vehicle_ahead, p[1], p[2] + f[2] * a.vehicle_ahead};
                mech3::program_vehicle_place(v, to, 0.0f);
                held = v, held_at[0] = p[0], held_at[1] = p[1], held_at[2] = p[2];
                std::printf("mw3: %s placed %.0f m ahead (%d weapons)\n", vs.name, a.vehicle_ahead,
                            mech3::wm_weapon_count(mech3::mech_weapon_manager(v)));
                if (a.field_base_fixture)
                  std::printf(
                      "mw3: field-base fixture selected: %s registry=%d\n",
                      vs.name, vs.registry);
                placed = true;
                break;
              }
          }
        // --turret-ahead (ours, for checking the reticle over a turret, playtest
        // W6): the player taken M m from the nearest live turret on the first
        // frame, then turned to face it each frame.
        if (a.turret_ahead > 0)
          if (void* m = mech3::mech_mission_player()) {
            static mech3::tur::Turret* t = nullptr;
            if (shots == 1) {
              float p[3], best = 1e30f;
              mech3::mech_world_position(m, p);
              for (mech3::tur::Turret* x : mech3::tur::turret_list()) {
                const float d = std::hypot(x->pos[0] - p[0], x->pos[2] - p[2]);
                if (x->active && d < best) t = x, best = d;
              }
              if (t) mech3::chain_player_near(engine.object_of(t->node), a.turret_ahead);
              if (t) std::printf("mw3: the player %.0f m from the nearest turret (%.0f m away)\n", a.turret_ahead, best);
            }
            if (t) {
              float p[3];
              mech3::mech_world_position(m, p);
              mech3::move_mech(m, p, std::atan2(p[0] - t->pos[0], p[2] - t->pos[2]));  // facing it: forward is -z
            }
          }
        // --mech-ahead (ours, for checking a mech up close: its damage decals,
        // docs/damage-decals.md): the player kept M to 1.5 M m from the nearest
        // enemy mech in play, facing it.
        static void* ahead = nullptr;
        if (a.mech_ahead > 0)
          if (void* m = mech3::mech_mission_player()) {
            if (shots == 1) {  // the nearest enemy mech in play (one not yet in, hidden, stands anywhere)
              float pp[3], best = 1e30f;
              mech3::mech_world_position(m, pp);
              std::string name;
              for (const mech3::ChainTarget& t : mech3::chain_hostiles()) {
                void* o = engine.object_of(t.h);
                const uint8_t* in_play = std::strcmp(t.kind, "mech") == 0 ? mech3::entity_active_flag(o) : nullptr;
                float at[3];
                if (!in_play || !*in_play || !mech3::mech_world_position(o, at)) continue;
                const float d = std::hypot(at[0] - pp[0], at[2] - pp[2]);
                if (d < best) best = d, ahead = o, name = t.name;
              }
              if (ahead) std::printf("mw3: the player %.0f m from %s (it was %.0f m off)\n", a.mech_ahead, name.c_str(), best);
            }
            if (ahead && mech3::entity_is_mech(ahead)) {
              float at[3], p[3];
              mech3::mech_world_position(ahead, at);
              mech3::chain_player_near(mech3::mech_node(ahead), a.mech_ahead, true);  // moves only beyond 1.5 M; its floor, not a roof
              mech3::mech_world_position(m, p);
              mech3::move_mech(m, p, std::atan2(p[0] - at[0], p[2] - at[2]));  // facing it: forward is -z
            }
          }
        if (a.fire > 0 && shots == a.fire) {
          s_fire = true;
          void* m = engine.object_of(engine.field32(engine.pilot(), 0x14));
          const int32_t g = a.fire_group > 0 ? (mech3::mech_select_group(m, a.fire_group - 1), a.fire_group - 1)
                                              : mech3::mech_select_hot_group(m);
          std::printf("mw3: firing group %d: %s\n", g + 1, mech3::mech_group_names(m, g).c_str());
        }
        // --objectives N (ours, for checking): F12's box shown from the N-th frame.
        if (a.objectives > 0 && shots == a.objectives) mech3::program_hud_objectives_toggle();
        if (a.walk && (shots == 2 || shots == 4))  // THROTTLE0's key (100%) pressed and let go
          for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc)
            if (sdlfe::dik_from_scancode(static_cast<SDL_Scancode>(sc)) == (zin::key1_of(0x24) & 0xff)) {
              SDL_Event ev{};
              ev.type = shots == 2 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
              ev.key.scancode = static_cast<SDL_Scancode>(sc);
              ev.key.down = shots == 2;
              SDL_PushEvent(&ev);
              break;
            }
        // --tap DIK@N[:M] (ours, for checking): the key pressed after the N-th
        // frame, let go after the M-th (the next by default).
        for (const Args::Tap& t : a.taps)
          if (shots == t.down || shots == t.up)
            for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc)
              if (sdlfe::dik_from_scancode(static_cast<SDL_Scancode>(sc)) == t.dik) {
                SDL_Event ev{};
                ev.type = shots == t.down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                ev.key.scancode = static_cast<SDL_Scancode>(sc);
                ev.key.down = shots == t.down;
                SDL_PushEvent(&ev);
                break;
              }
        // --click N[:M] (ours, for checking the fire): the left button pressed
        // as an SDL event after the N-th frame, let go after the M-th; each
        // frame from N logged (the current group's first weapon as a beam).
        if (a.click > 0 && a.fire_group > 0 && shots == a.click - 1) {
          void* m = engine.object_of(engine.field32(engine.pilot(), 0x14));
          mech3::mech_select_group(m, a.fire_group - 1);
          std::printf("mw3: group %d: %s\n", a.fire_group, mech3::mech_group_names(m, a.fire_group - 1).c_str());
        }
        if (a.click > 0 && (shots == a.click || shots == a.click_up)) {
          SDL_Event ev{};
          ev.type = shots == a.click ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
          ev.button.button = SDL_BUTTON_LEFT;
          ev.button.down = shots == a.click;
          SDL_PushEvent(&ev);
        }
        if (a.click > 0 && shots >= a.click && shots < a.click + 40) {
          void* m = engine.object_of(engine.field32(engine.pilot(), 0x14));
          mech3::BeamProbe b;
          if (mech3::mech_group_beam(m, mech3::mech_current_group(m), &b)) {
            const float off = std::sqrt((b.origin[0] - b.live[0]) * (b.origin[0] - b.live[0]) +
                                        (b.origin[1] - b.live[1]) * (b.origin[1] - b.live[1]) +
                                        (b.origin[2] - b.live[2]) * (b.origin[2] - b.live[2]));
            std::printf("mw3: frame %d dt %.1f ms: trigger %d/%d state %d, firing %d beam %d, ready %.2f, origin off %.2f m\n",
                        shots, engine.frame_dt() * 1000.0f, b.trigger_down, b.trigger_was, b.state, b.firing,
                        b.beam_active, b.ready, b.beam_active ? off : 0.0f);
          } else {
            const int32_t g = mech3::mech_current_group(m);
            std::printf("mw3: frame %d dt %.1f ms: group %d ammo %d ready %.2f\n", shots, engine.frame_dt() * 1000.0f,
                        g + 1, mech3::mech_group_ammo(m, g), mech3::mech_group_ready(m, g));
          }
        }
        // --shot-series K (ours, for checking flicker): the K frames up to the
        // shot's each into FILE_<frame>.bmp.
        static std::deque<std::string> series;
        if (!a.shot.empty() && a.shot_series > 1 && shots >= a.shot_frame - a.shot_series && shots < a.shot_frame - 1) {
          std::string base = a.shot;
          if (base.size() > 4 && base.compare(base.size() - 4, 4, ".bmp") == 0) base.resize(base.size() - 4);
          series.push_back(base + "_" + std::to_string(shots + 1) + ".bmp");
          window.render()->capture_next(series.back().c_str());
        }
        if (!a.shot.empty() && shots == a.shot_frame - 1) window.render()->capture_next(a.shot.c_str());
        if (!a.shot.empty() && shots == a.shot_frame) std::printf("mw3: wrote %s (frame %d)\n", a.shot.c_str(), shots);
        if (a.fire > 0 && shots >= a.fire && (shots - a.fire) % 5 == 0) {
          zwep::PoolStats ps;
          zwep::pool_stats(&ps);
          std::printf("mw3: frame %d, %d shots in flight\n", shots, ps.flying);
        }
        light_trace.sample(shots);
        if ((!a.shot.empty() && shots >= a.shot_frame) || (a.frames > 0 && shots >= a.frames)) launch_done = true;
      };
    } else if (!a.mission.empty()) {
      sdlfe::DebugMissionOptions o;
      o.variant = a.variant;
      split_mission(a.mission, o.mission, o.variant, o.number);
      if (a.have_pos) for (int i = 0; i < 3; ++i) o.eye[i] = a.pos[i];
      o.yaw = a.look[0] * 0.0174533f, o.pitch = a.look[1] * 0.0174533f;
      o.shot = a.shot, o.shot_frame = a.shot_frame, o.frames = a.frames;
      debug = std::make_unique<sdlfe::DebugMissionState>(&engine, &input, window.render(), o);
    } else if (!a.dialog_file.empty()) {
      debug = std::make_unique<sdlfe::DebugDialogState>(&engine, &input, window.render(), a.dialog_file,
                                                        a.dialog_name, a.shot, a.shot_frame);
    } else if (!a.movie.empty()) {
      debug = std::make_unique<sdlfe::DebugMovieState>(&engine, window.render(), a.movie, a.shot, a.shot_frame);
    }

    // The menus' driver (ours, for checking the front end): from the plain
    // start, each frame shown counted; --tap keys, --click-at left clicks at a
    // window pixel (the motion, then the press and the release a frame
    // apart), --shot / --shot-series as in a mission.
    int menu_frames = 0;
    std::vector<Args::ClickAt> auto_clicks;  // --briefing's own clicks
    std::deque<std::string> menu_series;
    std::unique_ptr<sdlfe::CampaignChain> chain;
    if (!a.chain.empty()) {
      sdlfe::ChainOptions co;
      co.resume = a.chain.rfind("resume", 0) == 0;
      co.retry = a.chain.rfind("retry", 0) == 0;
      co.kills = co.retry || a.chain.rfind("kills", 0) == 0;
      const size_t colon = a.chain.find(':');
      if (colon != std::string::npos) co.wins = std::atoi(a.chain.c_str() + colon + 1);
      co.dir = a.write_dir;
      chain = std::make_unique<sdlfe::CampaignChain>(engine, app, window.render(), co);
    }
    std::unique_ptr<sdlfe::ModesChain> modes;
    if (!a.modes.empty()) {
      modes = std::make_unique<sdlfe::ModesChain>(engine, app, a.modes);
      modes->key = [](int dik, bool down) {  // as --tap presses: the scan code of the DIK
        for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc)
          if (sdlfe::dik_from_scancode(static_cast<SDL_Scancode>(sc)) == dik) {
            SDL_Event ev{};
            ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            ev.key.scancode = static_cast<SDL_Scancode>(sc);
            ev.key.down = down;
            SDL_PushEvent(&ev);
            return;
          }
      };
    }
    if (!debug && !launch)
      engine.on_present = [&](int) {
        ++menu_frames;
        record_frame_time(menu_frames);
        if (chain) chain->frame();
        if (modes) modes->frame();
        auto key = [&](int dik, bool down) {
          for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc)
            if (sdlfe::dik_from_scancode(static_cast<SDL_Scancode>(sc)) == dik) {
              SDL_Event ev{};
              ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
              ev.key.scancode = static_cast<SDL_Scancode>(sc);
              ev.key.down = down;
              SDL_PushEvent(&ev);
              return;
            }
        };
        for (const Args::Tap& t : a.taps)
          if (!t.ingame && (menu_frames == t.down || menu_frames == t.up)) key(t.dik, menu_frames == t.down);
        std::vector<Args::ClickAt> clicks = a.clicks_at;
        clicks.insert(clicks.end(), auto_clicks.begin(), auto_clicks.end());
        // --net-soak (ours) in a network game the menus started: the soak's
        // line each half second in play (who each machine sees).
        static uint64_t s_menu_net_line_at = 0;
        // Each game's first Core frame: --net-seconds its clock; its end: any
        // state but Core, the fade or the pause after it.
        static bool s_menu_in_game = false, s_menu_killed = false;
        static int s_menu_game_from = 0;
        const bool menu_net = a.mission.empty() && engine.launch().type == 3;
        if (menu_net && !s_menu_in_game && app.current() == &mech3::states().core) {
          s_menu_in_game = true;
          s_menu_killed = false;
          s_menu_game_from = menu_frames;
          if (a.net_seconds > 0) mech3::missions().clock = static_cast<float>(a.net_seconds), mech3::missions().clock_down = 1;
          if (a.net_soak) std::printf("mw3: soak menus game in\n");
        } else if (s_menu_in_game && app.current() != &mech3::states().core && app.current() != &mech3::states().fade &&
                   app.current() != &mech3::states().pause) {
          s_menu_in_game = false;
          if (a.net_soak) std::printf("mw3: soak menus game over, rss %zu\n", sdlfe::resident_kb());  // its scores: the lobby's line
        }
        if (a.net_ahead > 0 && s_menu_in_game && app.current() == &mech3::states().core) mech3::program_net_hold_near(a.net_ahead);
        // --net-walk: THROTTLE_UP (0x18) held in its window; let go, the
        // throttle eases back to 0 (ThrottleConst off) and the mech stops.
        static bool s_walk = false;
        static bool s_walk_hooked = false;
        s_walk = a.net_walk[1] > a.net_walk[0] && s_menu_in_game && menu_frames >= s_menu_game_from + 30 * a.net_walk[0] &&
                 menu_frames < s_menu_game_from + 30 * a.net_walk[1];
        if (a.net_walk[1] > a.net_walk[0] && !s_walk_hooked) {
          s_walk_hooked = true;
          mech3::controls_calls().command = [](int cmd) { return zin::command(cmd) | (s_walk && cmd == 0x18 ? 1u : 0u); };
        }
        // --tap-ingame and --hold-ingame: keys and the left button by each game's frames.
        for (const Args::Tap& t : a.taps) {
          const int f = s_menu_in_game ? menu_frames - s_menu_game_from : -1;
          if (!t.ingame || (f != t.down && f != t.up)) continue;
          if (t.dik >= 0) {
            key(t.dik, f == t.down);
          } else {
            SDL_Event ev{};
            ev.type = f == t.down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.down = f == t.down;
            SDL_PushEvent(&ev);
          }
        }
        // --blur-ingame: the app deactivated as a lost focus does (without
        // --frames' stays_active), and an SDL timer's focus event back, which
        // must arrive whether frames run or not.
        if (a.blur[1] > 0 && s_menu_in_game && menu_frames - s_menu_game_from == a.blur[0]) {
          std::printf("mw3: blur at wall %lld\n", static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                         std::chrono::system_clock::now().time_since_epoch()).count()));
          app.on_window_activate(0);
          SDL_AddTimer(static_cast<Uint32>(a.blur[1]), [](void*, SDL_TimerID, Uint32) -> Uint32 {
            SDL_Event ev{};
            ev.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            SDL_PushEvent(&ev);
            return 0;
          }, nullptr);
        }
        // --net-soak: the objectives box's lines while shown (F12), once a second.
        if (a.net_soak && s_menu_in_game && app.current() == &mech3::states().core && (menu_frames - s_menu_game_from) % 30 == 0) {
          const std::string box = mech3::program_hud_objectives_text();
          if (!box.empty()) std::printf("mw3: soak menus box: %s\n", box.c_str());
        }
        if (a.fire_probe && s_menu_in_game && app.current() == &mech3::states().core) {
          int32_t trig = 0;
          float reload = 0;
          if (mech3::program_pilot_fire_probe(&trig, &reload))
            std::printf("mw3: fire probe frame %d ms %llu buttons %u trigger %d reload %.3f\n", menu_frames,
                        static_cast<unsigned long long>(SDL_GetTicks()), input.mouse_buttons, trig, reload);
        }
        if (a.net_kill_at > 0 && s_menu_in_game && !s_menu_killed && app.current() == &mech3::states().core &&
            menu_frames >= s_menu_game_from + 30 * a.net_kill_at) {
          s_menu_killed = true;
          if (mech3::program_net_staged_kill()) std::printf("mw3: soak menus staged kill\n");
        }
        if (a.net_soak && menu_net && app.current() == &mech3::states().core &&
            static_cast<uint64_t>(menu_frames) >= s_menu_net_line_at) {
          s_menu_net_line_at = static_cast<uint64_t>(menu_frames) + 15;  // half a second of game time
          const long long wall = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch()).count();
          std::printf("mw3: soak menus clock %.1f rss %zu wall %lld %s\n", mech3::missions().clock, sdlfe::resident_kb(), wall,
                      mech3::program_net_soak_line().c_str());
        }
        // --click-players: the first frame each player count was reached.
        static int s_players_at[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
        const size_t held = znet::session().open ? znet::lists().players.size() : 0;
        for (size_t n = 1; n <= held && n < 9; ++n)
          if (s_players_at[n] < 0) s_players_at[n] = menu_frames;
        for (Args::ClickAt& c : clicks)
          if (c.players > 0) c.frame = c.players < 9 && s_players_at[c.players] >= 0 ? s_players_at[c.players] + c.frame : -100;
        // --click-return: from the frame the lobby was last entered after a game.
        static int s_entries = 0, s_back_at = -1;
        if (engine.lobby_entries() != s_entries) {
          s_entries = engine.lobby_entries();
          if (s_entries >= 2) s_back_at = menu_frames;
        }
        for (Args::ClickAt& c : clicks)
          if (c.back) c.frame = s_back_at >= 0 ? s_back_at + c.frame : -100;
        for (Args::ClickAt& c : clicks)
          if (c.ingame) c.frame = s_menu_in_game ? s_menu_game_from + c.frame : -100;
        for (const Args::ClickAt& c : clicks) {
          if (menu_frames != c.frame && menu_frames != c.frame + 1 && menu_frames != c.frame + 2) continue;
          // The cursor put on the point (zInput's 0x0051b440, the game's
          // 640 x 480 about its centre), then the press and the release.
          // A 640x480 frame pixel; over a mission on a canvas the dialogs' frame
          // is centred (resolution.md step 5); the menus' frame is at 0, and
          // with wide menus a right group's piece is the extra width further.
          const float dw = static_cast<float>(mech3::display().w > 0 ? mech3::display().w : 640);
          const float dh = static_cast<float>(mech3::display().h > 0 ? mech3::display().h : 480);
          const bool over_mission = app.current() == &mech3::states().core || app.current() == &mech3::states().pause;
          const int32_t extra = std::max(0, mech3::display().w - 640);
          const int32_t ox = over_mission ? ztwod::dialog_origin().x : c.group == 2 ? extra : c.group == 1 ? extra / 2 : 0;
          const int32_t oy = over_mission ? ztwod::dialog_origin().y : 0;
          const float px = static_cast<float>(c.x + ox), py = static_cast<float>(c.y + oy);
          int ww = 0, wh = 0;
          window.client_size(&ww, &wh);
          float wx = 0, wy = 0;
          sdlfe::frame_to_window(px, py, static_cast<float>(ww), static_cast<float>(wh), window.shape(), &wx, &wy);
          if (menu_frames == c.frame) {
            zin::mouse_set((px - dw / 2) / (dw / 2), (py - dh / 2) / (dh / 2));
            // Windowed menus follow SDL's absolute cursor. Keep the synthetic
            // motion and buttons in window coordinates, like physical input.
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.x = wx, ev.motion.y = wy;
            SDL_PushEvent(&ev);
          }
          if (!c.hover && (menu_frames == c.frame + 1 || menu_frames == c.frame + 2)) {
            SDL_Event ev{};
            ev.type = menu_frames == c.frame + 1 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.down = menu_frames == c.frame + 1;
            ev.button.x = wx, ev.button.y = wy;
            SDL_PushEvent(&ev);
          }
        }
        if (!a.shot.empty() && a.shot_series > 1 && menu_frames >= a.shot_frame - a.shot_series &&
            menu_frames < a.shot_frame - 1) {
          std::string base = a.shot;
          if (base.size() > 4 && base.compare(base.size() - 4, 4, ".bmp") == 0) base.resize(base.size() - 4);
          menu_series.push_back(base + "_" + std::to_string(menu_frames + 1) + ".bmp");
          window.render()->capture_next(menu_series.back().c_str());
        }
        if (!a.shot.empty() && menu_frames == a.shot_frame - 1) window.render()->capture_next(a.shot.c_str());
        if (!a.shot.empty() && menu_frames == a.shot_frame) std::printf("mw3: wrote %s (frame %d)\n", a.shot.c_str(), menu_frames);
        // --verbose (ours): each state the app moves to, by name.
        static const zgame::State* last_state = nullptr;
        if (a.verbose && app.current() != last_state) {
          last_state = app.current();
          if (auto* st = dynamic_cast<const mech3::State*>(last_state))
            std::printf("mw3: state %s (frame %d)\n", st->name(), menu_frames);
        }
        // --lab-mech N (ours, for checking the lab's 3D view): once the lab is
        // up its view is handed chassis N (0..18), as the slot switch hands it
        // the slot's (+0x38); the slot itself keeps its mech.
        static bool lab_mech_given = false;
        if (a.lab_mech >= 0 && !lab_mech_given && app.current() == &mech3::states().lab) {
          mech3::states().lab.mech_ = a.lab_mech;
          lab_mech_given = true;
        }
        // --win N (ours, for checking the save): N frames into a mission it
        // is won as a player wins it, by Exit Mission once the objectives are
        // met (campaign.md section 1): the end screen, its fade to the
        // debriefing's Win State, whose save writes the next mission's game.
        static int win_frames = 0;
        if (a.win > 0 && app.current() == &mech3::states().core && ++win_frames == a.win) {
          mech3::missions().ended = 1;
          mech3::missions().on_command(0x71);
        }
        // --verbose (ours, for checking the save): the pilot's game as the
        // Win State saved it, and as a briefing finds it once its mission
        // has loaded (the saved game read back into it).
        static const void* said = nullptr;
        if (a.verbose && app.current() == &mech3::states().end_win && said != &mech3::states().end_win) {
          said = &mech3::states().end_win;
          std::printf("mw3: saved at the win: %s\n", campaign_summary().c_str());
        }
        if (a.verbose && app.current() == &mech3::states().load && said != &mech3::states().load) {
          auto* b = dynamic_cast<mech3::BriefingScreen*>(engine.screen_of(mech3::states().load.screen_));
          if (b && b->loaded) {
            said = &mech3::states().load;
            std::printf("mw3: at the briefing: %s; anim log %d\n", campaign_summary().c_str(),
                        zeff::anim_log().count);
          }
        }
        // --verbose (ours, for checking the lab's loadout reaching the
        // launch): the player's mech as built, 30 frames into a mission.
        static int core_frames = 0;
        mech3::LaunchedMech m;
        if (a.verbose && app.current() == &mech3::states().core && ++core_frames == 30 && mech3::launched_mech(&m)) {
          const auto* c = static_cast<const mech3::Config*>(m.config);
          std::printf("mw3: launched mech: %d weapons, %d sinks, engine %d, %.2f m/s, centre torso armour %d\n",
                      m.weapons, m.sinks, c ? c->engine : -1, m.max_speed, c ? c->armor[0] : -1);
        }
      };

    if (app.init_instance()) {
      // The state after the intro (the app's +0x1dc): the debug start takes
      // the place the lobby launch gives the Load state.
      if (debug) app.post_intro_ = debug.get();
      if (!a.movie.empty()) app.lobby_flow_ = 1;  // the Intro state's own movies skipped
      if (launch) app.post_intro_ = &mech3::states().load;
      // --briefing c1m2 (ours, for checking the front end): a pilot "Briefing"
      // whose newest saved campaign stands at that mission (pilot.md: the
      // newest save is the pilot's game), then the clicks a player makes:
      // CAMPAIGN, the pilot (the list's first row), ACCEPT. The Campaign state
      // goes on to the Load state and the mission's own briefing (MECHLAB and
      // SALVAGE past Op 1 Mission 1), its store a new campaign's carried on.
      if (!a.briefing.empty() && !debug && !launch) {
        std::string dir, variant;
        int number = 1;
        split_mission(a.briefing, dir, variant, number);
        const int op = dir == "c1" ? 1 : dir == "c2" ? 2 : dir == "c3" ? 3 : dir == "c4" || dir == "c4b" ? 4 : 0;
        mech3::Pilot* p = op ? mech3::create_pilot("Briefing") : nullptr;
        if (!p) {
          std::fprintf(stderr, "mw3: --briefing %s: not a campaign mission (c1m1 .. c4bm7)\n", a.briefing.c_str());
          return 2;
        }
        // The pilot's new campaign (Op 1 Mission 1, the starting store) stepped
        // on as each won mission's end does (0x00434e00: the stores, the
        // slots, the field bases), each kept as 0x00478f10 keeps it.
        auto* c = p->campaigns.empty() ? nullptr : dynamic_cast<mech3::CampaignGame*>(p->campaigns.front());
        while (c && (c->operation() != op || c->mission_number() != number)) {
          mech3::CampaignGame* n = c->next_mission(true, false);
          if (!n) c = nullptr;
          else p->campaigns.push_back(n), c = n;
        }
        if (!c) {
          std::fprintf(stderr, "mw3: --briefing %s: no such campaign mission\n", a.briefing.c_str());
          return 2;
        }
        p->current = c;
        p->save();
        auto_clicks = {{40, 70, 60}, {80, 150, 135}, {100, 470, 320, 2}};  // CAMPAIGN, the pilot, ACCEPT
      }

      sdlfe::SdlPump pump(&input, &app);
      if (clock.frame_ms || g_net_pass)
        pump.each_pass = [&] {
          if (clock.frame_ms) clock.frame();
          if (g_net_pass) g_net_pass();
        };
      // Ours: Alt+Enter, and the game's sounds silent while the window is away.
      if (a.backend == sdlfe::RendererBackend::sdl)
        pump.toggle_fullscreen = [&] { window.switch_fullscreen(!window.fullscreen()); };
      if (auto* s = dynamic_cast<sdlfe::SdlSoundOutput*>(sound.get())) pump.focus_changed = [s](bool on) { s->set_focused(on); };
      if (!debug && !launch && (a.frames > 0 || !a.shot.empty()))
        pump.should_quit = [&] { return (a.frames > 0 && menu_frames >= a.frames) || (!a.shot.empty() && menu_frames > a.shot_frame); };
      if (launch) pump.should_quit = [&] { return launch_done; };
      if (chain) pump.should_quit = [&] { return chain->done(); };
      if (modes) pump.should_quit = [&] { return modes->done(); };
      pump.stays_active = a.frames > 0 || !a.shot.empty() || chain || modes;  // a check: no pause for focus
      // MW3_FOCUS_LOST_AT=N (checks): the window's focus lost at the pump's
      // Nth pass, as when another app is in front.
      int focus_lost_at = 0, passes = 0;
      if (const char* f = SDL_getenv("MW3_FOCUS_LOST_AT")) focus_lost_at = std::atoi(f);
      if (focus_lost_at > 0) {
        auto before = pump.each_pass;
        pump.each_pass = [&, before] {
          if (before) before();
          if (++passes != focus_lost_at) return;
          SDL_Event ev{};
          ev.type = SDL_EVENT_WINDOW_FOCUS_LOST;
          ev.window.windowID = SDL_GetWindowID(window.sdl());
          SDL_PushEvent(&ev);
          std::printf("mw3: focus lost (MW3_FOCUS_LOST_AT)\n");
        };
      }
      // A quit outside the Exit state returns through ExitInstance, as the exe's WM_QUIT does,
      // and leaves the world to the process's end: stop_game after zSound's shutdown freed
      // the sounds the world's nodes still held (the exit's SIGTRAP, docs/program.md).
      result = app.run(pump);
      window.render()->drain();
      if (!frame_ms.empty()) {  // --frame-times: the frames' wall times
        std::vector<double> s = frame_ms;
        std::sort(s.begin(), s.end());
        double sum = 0;
        for (double v : s) sum += v;
        auto at = [&](double q) { return s[std::min(s.size() - 1, static_cast<size_t>(q * s.size()))]; };
        std::printf("mw3: frame times over %zu frames: mean %.4f ms, median %.4f, p95 %.4f, p99 %.4f, max %.4f\n",
                    s.size(), sum / s.size(), at(0.5), at(0.95), at(0.99), s.back());
      }
    }
    light_trace.finish();
    // Save the buffered clock samples after timing, never with per-frame I/O.
    if (benchmark_timestamps && !frame_timestamps.empty()) {
      auto* output = std::fopen(benchmark_timestamps,"w");
      if (!output) throw std::runtime_error("Cannot write benchmark timestamps");
      std::fprintf(output,"# counter_frequency %llu\nframe,counter\n",
                   (unsigned long long)SDL_GetPerformanceFrequency());
      for (const auto& sample : frame_timestamps)
        std::fprintf(output,"%d,%llu\n",sample.frame,(unsigned long long)sample.counter);
      const bool failed = std::ferror(output) != 0;
      const int closed = std::fclose(output);
      if (failed || closed) throw std::runtime_error("Incomplete benchmark timestamps");
    }
    app.destroy();  // the app's destructor: the settings saved
    if (a.verbose) engine.print_unported_summary();
    else {
      size_t gaps = 0;
      for (const auto& [k, n] : engine.unported_counts()) gaps += mech3::GameEngine::known_unported(k) ? 0 : 1;
      std::fprintf(stderr, "mw3: %zu original functions reached are not ported (--verbose lists them and %zu known)\n",
                   gaps, engine.unported_counts().size() - gaps);
    }
    ztwod::set_host(nullptr);
    mech3::set_app(nullptr);
    input.in_mission = nullptr;  // the app is gone
  }
  zin::set_host(nullptr);
  return result;
}

}  // namespace
