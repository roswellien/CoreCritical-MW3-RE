#include "mech3/mission.h"


#include "mech3/options.h"
#include "mech3/content_world.h"
#include "crt/msvc_string.h"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace mech3 {

namespace {
Missions g_missions;
MissionHooks g_default_hooks;
MissionHooks* g_hooks = &g_default_hooks;

// MFC's CString::Format (and sprintf) for the formats mission.cpp uses.
std::string format(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return buf;
}
}  // namespace

Missions& missions() { return g_missions; }
void set_mission_hooks(MissionHooks* h) { g_hooks = h ? h : &g_default_hooks; }
MissionHooks& mission_hooks() { return *g_hooks; }

void MissionHooks::render_rect(int32_t out[4]) {
  unported("0x0040d6a0", {});
  out[0] = out[1] = out[2] = out[3] = 0;
}
uint32_t MissionHooks::video_texture_memory(int32_t dev, uint32_t* total, uint32_t* free_) {
  *total = *free_ = 0;
  return unported("[0x0072f38c]", {dev});
}

// Mech3.exe 1.2 @ 0x00484c50
const char* mission_dir(int32_t index) {
  static const char* const kDirs[] = {"c1", "c2", "c3", "c4", "c4b", "t1", "lab", "dev1", "dev2"};
  if (index >= 9 && index - 9 < static_cast<int32_t>(mod_worlds().size())) return mod_worlds()[static_cast<size_t>(index - 9)].c_str();
  return index >= 0 && index < 9 ? kDirs[index] : "none";
}
std::vector<std::string>& mod_worlds() {
  static std::vector<std::string> w;
  return w;
}
int32_t mission_dir_count() { return 9 + static_cast<int32_t>(mod_worlds().size()); }
int32_t mission_dir_index(const char* dir) {
  for (int32_t i = 0; dir && i < mission_dir_count(); ++i)
    if (crt::msvc_stricmp(mission_dir(i), dir) == 0) return i;
  return -1;
}

// Mech3.exe 1.2 @ 0x004902a0: the megabytes of textures to load, 0 in
// software. A 3D card's own memory, capped by the TextureMemory option
// (TEXMEM_MAX no cap; 12, 10 and 8 MB all cap at 8; 6, 4, 2). Always true.
bool Missions::texture_budget(uint32_t* mb) {
  MissionHooks& h = *g_hooks;
  if (h.hw_card_flag() != 0 && h.video_fn_390(-1) == 0) {
    uint32_t free_ = 0;
    *mb = h.video_texture_memory(-1, mb, &free_) ? (*mb >> 20) : 1;
  } else {
    *mb = 0;
  }
  auto cap = [&] {
    switch (h.texture_memory()) {
      case 1: case 2: case 3: return 8;
      case 4: return 6;
      case 5: return 4;
      case 6: return 2;
      default: return -1;  // 0 (TEXMEM_MAX) and anything else
    }
  };
  int32_t c = cap();
  if (c == -1) return true;
  if (*mb == 0) {
    *mb = static_cast<uint32_t>(cap());
    return true;
  }
  c = cap();
  if (static_cast<uint32_t>(c) < *mb) *mb = static_cast<uint32_t>(cap());
  return true;
}

// Mech3.exe 1.2 @ 0x00490600: the reader block of the game: "ia%d" for
// type 2, "mp%d" for 3, else "m%d", by mission number.
void Missions::block_name(char* out) { block_name(g_hooks->game_mission_number(), out); }

// Mech3.exe 1.2 @ 0x00490590
void Missions::block_name(int32_t n, char* out) {
  const int32_t type = g_hooks->game_type();
  std::snprintf(out, 0x20, type == 2 ? "ia%d" : type == 3 ? "mp%d" : "m%d", n);
}

// Mech3.exe 1.2 @ 0x00490630: the mission's reader archives and loose paths.
void Missions::open_readers(const char* name) {
  MissionHooks& h = *g_hooks;
  h.reader_open(format("zbd\\%s\\reader.zbd", name).c_str(), 0);
  h.reader_set_path(format("..\\data\\common\\zrdr;..\\data\\common\\zrdr\\mechs;..\\data\\%s\\zrdr;..\\data\\%s\\zrdr\\aipath",
                           name, name).c_str());
  char block[0x20];
  block_name(block);
  h.reader_add_path(format("..\\data\\%s\\zrdr\\aipath\\%s", name, block).c_str());
  h.reader_open(format("zbd\\%s\\reader%s.zbd", name, block).c_str(), 0);
}

// Mech3.exe 1.2 @ 0x0048faf0: the world. A mission already up is kept, another
// one unloaded first. The texture levels: `level` for "texture" (2 under 4 MB
// of card memory, 3 under 8, else 0), `flags` 0x11 for the mech textures under
// 4 MB or when the install has no 16-bit set (InstallOptions 2 or 4); the
// mech set is "mechtex" on a 3D card, "mechtexS" in software.
bool Missions::load_world(int32_t index) {
  MissionHooks& h = *g_hooks;
  h.progress(0.1f);
  h.fn_518f50(0);
  if (current != -1) {
    if (current == index) return true;
    h.unload();
    return load_world(index);
  }
  refresh_content_mission();
  const std::string name = mission_dir(index);
  h.interp_define("USEZBD");
  h.fn_484270(1);
  h.set_use_archive();
  h.anim_allow_loading(1);
  const bool hw = h.hw_card_flag() != 0;
  uint32_t mb = 0;
  texture_budget(&mb);
  const uint8_t rgb_option = h.app_rgb();
  uint8_t rgb = rgb_option;
  int32_t flags = 0, level = -1;
  auto check_16bit = [&] {
    if (h.install_options(2) == 0 && h.install_options(4) == 0) flags = 0x11, rgb = 0;
  };
  if (mb == 0) {
    rgb = hw;
    check_16bit();
    level = 0;
  } else if (mb < 4) {
    flags = 0x11;
    level = 2;
  } else if (mb < 8) {
    check_16bit();
    level = 3;
  } else {
    rgb = hw;
    check_16bit();
    level = 0;
  }
  if (hw) {
    h.interp_define("HARDWARE");
    h.light_5269e0(0);
  } else {
    rgb = 0;
  }
  const uint8_t holes = (rgb || rgb_option) ? 1 : 0;
  h.set_holes(holes);
  h.fn_576270(0);
  uint32_t total = 0, free_ = 0;
  if (h.video_texture_memory(-1, &total, &free_)) {
    const double k = 9.5367431640625e-07;  // 2^-20
    h.progress_text(format("Texture Memory: %.2f (%.2f)", total * k, free_ * k).c_str());
  }
  if (abort) return false;
  h.textures_begin(0);
  h.texture_library("texture", level, hw ? 1 : 0);
  h.texture_library(hw ? "mechtex" : "mechtexS", flags, 1);
  h.progress_text(format("MechWarrior3 (%s) (%s)", "PRODUCTION", h.version_number()).c_str());
  h.progress_text(h.video_driver_name());
  h.progress_text(h.video_mode_name());
  h.interp_source(format("support\\%ssize.gw", name.c_str()).c_str());  // its result is ignored
  h.classes_reset();
  h.models_reset();
  if (abort) return false;
  h.progress_text(format("Loading [ %s ]", name.c_str()).c_str());
  h.fn_535ed0(0.5f);
  const std::string script = format("%s.gs", name.c_str());
  if (!(content_world_is_authored(index) ? load_content_world(index) : h.interp_source(script.c_str()) != 0)) {
    h.zerr(0x800, "C:\\zipper\\Mech3\\mission.cpp", 0x261, "Failed to Interpret: %s -- Mission Load Aborted",
           script.c_str());
    h.cleanup(1);
    return false;
  }
  if (abort) return false;
  h.progress(0.3f);
  if (abort) return false;
  h.images_path("..\\data\\common\\images\\hud;zbd");
  if (abort) return false;
  h.progress_text(format("Loading Textures (%d/%d) %s %s", level, flags, hw ? "RGB" : "PAL",
                         holes ? "HOLES" : "NOHOLES").c_str());
  h.textures_load();
  h.progress(0.4f);
  if (abort) return false;
  h.progress_text(h.message(0x104));
  const Handle world = h.find_node(0xd, "world1");
  const Handle camera = h.find_node(8, "camera1");
  const Handle window = h.find_node(0xe, "window1");
  const Handle display = h.find_node(0xf, "display");
  Handle mem = h.alloc(8);
  h.set_608c84(mem ? h.make_608c84(mem, world) : 0);
  mem = h.alloc(0x3c);
  const Handle vw = mem ? h.make_view_window(mem, window) : 0;
  mem = h.alloc(0x7c);
  h.set_view(mem ? h.make_view(mem, camera, vw, h.get_608c84()) : 0);
  h.lists_4f7f70();
  h.lists_4f7bd0();
  h.set_render_target(window);
  h.set_display_target(display);
  h.set_camera(camera);
  // The view window's two rectangles (+0x8, +0x18) both from the render
  // rectangle, so they never differ and the scale is never computed.
  int32_t r[4];
  h.render_rect(r);
  for (int i = 0; i < 4; ++i) {
    h.set_field32(vw, 0x8 + 4 * i, static_cast<uint32_t>(r[i]));
    h.set_field32(vw, 0x18 + 4 * i, static_cast<uint32_t>(r[i]));
  }
  const bool changed = false;  // the compare of the two copies (0x00490008)
  h.set_field8(vw, 0x28, changed ? 1 : 0);
  h.window_set_size(h.field32(vw, 0x4), r[2] - r[0], r[3] - r[1]);
  h.window_set_origin(h.field32(vw, 0x4), r[0], r[1]);
  h.clear_9a17c0();
  if (abort) return false;
  h.fn_4e7b00();
  h.progress_text("Loading Effects");
  h.effects_load(world, camera, "effects.zrd");
  h.progress(0.5f);
  if (abort) return false;
  h.progress_text("Loading Animations");
  h.anim_set_world(world);
  if (!content_world_is_authored(index)) h.anim_load("anim.zrd");
  h.progress(0.6f);
  if (abort) return false;
  h.progress_text("Loading DEClient");
  h.decals_load(world, 0);
  if (abort) return false;
  h.progress_text("Loading Weapons");
  h.weapons_load(world, "weapons.zrd", 0, 0x4b0020);
  h.progress(0.7f);
  if (abort) return false;
  h.progress_text("Loading HUD");
  const Handle w = h.window_rect();
  h.hud_load(h.display_rect(), w);
  h.fn_40db60();
  current = index;
  h.progress_text("Creating Stealth");
  mem = h.alloc(0x104);
  if (mem) {
    const Handle obj = h.stealth_4ac860(0xa1c);
    h.set_stealth(h.stealth_4a4de0(mem, obj ? h.stealth_4a51b0(obj, 1) : 0));
  } else {
    h.set_stealth(0);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00490ab0: the mission behind the world. The game type
// picks the objectives and the start animation (`start_single_player_m<n>`,
// `switch_ia_m<n>`, `start_multiplayer_m<n>`, `start_training_m<n>`, each
// falling back to the name without the number).
bool Missions::setup() {
  MissionHooks& h = *g_hooks;
  const int32_t index = h.game_mission_index();
  const std::string name = mission_dir(index);
  h.fn_576270(0);
  begin();
  h.progress(0.0f);
  const int32_t type = h.game_type();
  const int32_t number = h.game_mission_number();
  open_readers(name.c_str());
  if (!load_world(index)) return false;
  open_readers(name.c_str());
  h.mechs_init();
  h.fn_4319a0();
  h.fn_459e80(0x605ae0);
  std::string block = format("m%d", number);
  char reader_block[0x20];
  block_name(reader_block);
  h.world_params("world.zrd", reader_block);
  h.fn_4e7b40();
  h.entity_named(h.stealth(), "stealth");
  h.progress(0.8f);
  h.fn_459e80(0x605ae0);
  h.progress_text("Loading InOut data");
  h.inout_load();
  h.landmines_load(0x604e48, "landmines.zrd", block.c_str());
  h.fn_4b4ab0(0x60b228);
  h.player_setup(h.app_turrets());
  if (h.game_saved()) h.progress_text("Saved Game Loaded");
  if (abort) return false;
  for (int i = 1; i < 5; ++i) objectives[i] = 0;  // +0x7c..+0x88; +0x78 is left
  const Handle out[5] = {0x6098d0 + 0x78, 0x6098d0 + 0x7c, 0x6098d0 + 0x80, 0x6098d0 + 0x84, 0x6098d0 + 0x88};
  auto turrets = [&] {
    if (h.app_turrets()) h.turrets_load("aiTurrets.zrd", h.field32(h.field32(h.view(), 0x4), 0x38) + 0x1e4);
  };
  Handle anim = 0;
  auto start_anim = [&](const char* fmt, const char* fallback) {
    if (content_world_is_authored(index)) return;
    anim = h.anim_start(format(fmt, number).c_str());
    if (!anim) anim = h.anim_start(fallback);
  };
  switch (type + 1) {
    case 0: case 1: case 2:  // -1, 0, 1: single player
      turrets();
      block = format("m%d", number);
      h.progress_text("Creating Objectives");
      h.objectives_load("objectives.zrd", block.c_str(), out[0], out[1], out[2], out[3], out[4]);
      start_anim("start_single_player_m%d", "start_single_player");
      break;
    case 3:  // 2: instant action
      block = format("ia%d", number);
      h.progress_text("Creating Objectives");
      h.objectives_load("objectives.zrd", block.c_str(), out[0], out[1], out[2], out[3], out[4]);
      start_anim("switch_ia_m%d", "switch_ia");
      break;
    case 4:  // 3: multiplayer
      block = format("mp%d", number);
      h.progress_text("Creating Objectives");
      h.objectives_load(number > 2 ? "mpinfo.zrd" : "objectives.zrd", number > 2 ? "objectives" : block.c_str(),
                        out[0], out[1], out[2], out[3], out[4]);
      start_anim("start_multiplayer_m%d", "start_multiplayer");
      if (h.mp_flag()) h.mp_stations(stations);
      break;
    case 5:  // 4: training
      turrets();
      block = format("m%d", number);
      h.progress_text("Creating Objectives");
      h.game_objectives("objectives.zrd", block.c_str(), out[0], out[1], out[2], out[3], out[4]);
      start_anim("start_training_m%d", "start_training");
      break;
    default:
      break;
  }
  validate_content_mission_objectives();
  sounds[0] = h.sound_find("com_success");
  sounds[1] = h.sound_find("com_failed");
  sounds[2] = h.sound_find("obj_primary");
  sounds[3] = h.sound_find("obj_secondary");
  h.progress_text("Post Vehicle Fixup");
  h.fn_4d2e20();
  h.fn_4d31d0();
  h.fn_4e2120();
  if (abort) return false;
  h.ai_list_reset(0x604048);
  h.ai_list_reset(0x604078);
  h.keys_reset();
  for (int i = 0; i < 25; ++i) h.hud_line_clear(i);  // the HUD's 25 text lines (0x005fc848, 0x4b0 apart)
  h.fn_40f040();
  h.fn_40f890();
  loaded = 1;
  if (abort) return false;
  h.progress(0.9f);
  h.ai_list_reset(0x603f90);
  ended = aborting = 0;
  end_delay = 0;
  h.fn_4d4b30();
  if (abort) return false;
  h.progress_text("Ready To Launch");
  if (anim) h.anim_run(anim);
  h.interp_5964c0(0);
  h.progress(1.0f);
  return true;
}

// Mech3.exe 1.2 @ 0x00490880 and its thread 0x00490900. FIX (A): the load
// runs here, not on a thread at idle priority beside the briefing: the
// world's textures go to the GPU, which wants the main thread. The briefing
// sees it finished at once (0x00490930).
void Missions::start() {
  g_hooks->game_mission_index();
  done = 0;
  abort = 0;
  failed = setup() ? 0 : 1;
  done = 1;
}

// --- The mission at run time (docs/mission-frame.md) ---------------------------

namespace {
constexpr Handle kThis = 0x6098d0;
// The objectives' places in the missions object, as their functions take them.
constexpr Handle kObj[5] = {kThis + 0x78, kThis + 0x7c, kThis + 0x80, kThis + 0x84, kThis + 0x88};

// The cockpit's screen (0x00430aa0 on the mech's data) when the player's view
// is on the player's own mech, else none.
Handle cockpit(MissionHooks& h) {
  const Handle v = h.player_vehicle();
  if (v != h.field32(h.view(), 0x64)) return 0;
  return h.mech_screen(h.field32(v, 0x684));
}
}  // namespace

// Mech3.exe 1.2 @ 0x0048f400: the game time from 0, MISSION_COMPLETE bound.
bool Missions::begin() {
  MissionHooks& h = *g_hooks;
  loaded = 0;
  time = 0;
  h.game_time_set(time);
  abort = 0;
  voice = 0;
  h.bind_command(0x71, kMissionCommand);
  h.bind_command(0x72, kMissionCommand);
  check_cheats();
  h.hooks_check();
  return true;
}

// Mech3.exe 1.2 @ 0x00491d20: invincibility, unlimited ammo or heat off,
// outside a network game.
void Missions::check_cheats() {
  MissionHooks& h = *g_hooks;
  // [0x00609994] is one byte in the exe, which the cheat setters also set
  // (0x0040ced0..): the port's is options().cheated, +0xc4 its mirror here.
  if (h.app_network()) {
    cheats = 0;
  } else {
    cheats = (h.invincible() || h.infinite_ammo() || !h.heat()) ? 1 : 0;
  }
  options().cheated = cheats;
}

// Mech3.exe 1.2 @ 0x0048f440. Exit Mission (Ctrl+Q) works only once the
// mission is won or lost; 0x72 is bound to it too but does nothing.
void Missions::on_command(int32_t cmd) {
  if (cmd != 0x71) return;
  if (aborting || ended) end_screen();
}

// Mech3.exe 1.2 @ 0x0048f470: one frame. The game time [0x0060991c] is +0x4c.
void Missions::update(float dt) {
  MissionHooks& h = *g_hooks;
  h.hooks_check();
  time = dt + time;
  h.game_time_set(time);
  clock = clock_down ? clock - dt : dt + clock;
  world_update(world);
  h.valves_tick(dt);
  h.map_update(dt);
  if (stations) h.stations_update(stations, dt, h.player_vehicle());
  voice_next = 0;
  if (aborting) {
    end_delay = end_delay - dt;
    if (!(end_delay > 0.0f) && !h.app_1c2()) {
      end_screen();
      play_voice();
      return;
    }
  } else {
    if (objectives[2]) {  // the secondary objectives
      objective_sound = sounds[3];
      h.objective_check(objectives[2]);
    }
    if (!ended) {
      uint8_t done_now = 0, done_before = 0;
      if (objectives[1]) {  // the primary ones: their being done is the win's first test
        objective_sound = sounds[2];
        done_before = h.objective_done(objectives[1]);
        done_now = h.objective_check(objectives[1]);
      }
      if (done_now) {
        if (objectives[3]) {
          if (done_now != done_before) {
            const Handle scr = cockpit(h);
            if (scr && h.objectives_announce(kObj[1], kObj[2], kObj[3], kObj[4])) h.screen_flash(scr + 0x17964, -1.0f);
          }
          objective_sound = 0;
          done_now = h.condition_met(objectives[3]);
        }
        if (done_now) complete();
      }
    }
    if (objectives[4]) {
      objective_sound = 0;
      if (h.condition_met(objectives[4])) lose();
    }
  }
  play_voice();
}

// Mech3.exe 1.2 @ 0x00491050: a network game's objectives freed and read
// again (as the load's: "mp<N>" of objectives.zrd, or mpinfo.zrd's
// "objectives" past map 2), not won or lost any more. The respawn's.
void Missions::objectives_again() {
  MissionHooks& h = *g_hooks;
  const int32_t number = h.game_mission_number();  // the game's vt+0x4c
  const std::string block = format("mp%d", number);
  h.objectives_free(kObj[0], kObj[1], kObj[2], kObj[3], kObj[4]);  // 0x0045c1d0
  h.objectives_load(number > 2 ? "mpinfo.zrd" : "objectives.zrd", number > 2 ? "objectives" : block.c_str(), kObj[0],
                    kObj[1], kObj[2], kObj[3], kObj[4]);  // 0x0045c2d0
  ended = aborting = 0;
}

// Mech3.exe 1.2 @ 0x0048f630: the line asked for this frame plays unless a
// line of higher priority still does.
void Missions::play_voice() {
  MissionHooks& h = *g_hooks;
  if (voice && h.sound_playing(voice) == 0) {
    voice = 0;
    voice_priority = -1.0f;
  }
  if (!voice_next) return;
  if (!voice || voice_next_priority >= voice_priority) {
    if (voice) h.sound_stop(voice);
    voice = h.sound_play(voice_next, 1.0f);
    voice_priority = voice_next_priority;
  }
  voice_next = 0;
}

// Mech3.exe 1.2 @ 0x0048f6c0: no sound is the objective's own line at 0.
void Missions::ask_voice(Handle sound, float priority) {
  if (!sound) {
    sound = objective_sound;
    priority = 0.0f;
    if (!sound) return;
  }
  if (!voice_next || priority >= voice_next_priority) {
    voice_next = sound;
    voice_next_priority = priority;
  }
}

// Mech3.exe 1.2 @ 0x0048f740: the win. Instant Action's endless and wave
// modes (+0x44 1 and 2) load the next wave's objectives instead, up to wave
// 20 in mode 2; training goes on to its next lesson while it has one.
void Missions::complete() {
  MissionHooks& h = *g_hooks;
  if (ended) return;
  ended = 1;
  aborting = 0;
  const Handle scr = cockpit(h);
  const uint8_t announce = h.objectives_announce(kObj[1], kObj[2], kObj[3], kObj[4]);
  bool voice_line = true;
  const int32_t type = h.game_type();
  auto mode = [&h] { return h.field32(h.ia_game(), 0x44); };
  if (type == 2) {
    if (mode() == 1 || mode() == 2) {
      const Handle ia = h.ia_game();
      const int32_t wave = static_cast<int32_t>(h.field32(ia, 0x6c));
      if (h.field32(ia, 0x44) == 2 && wave > static_cast<int32_t>(h.field32(ia, 0x68))) h.set_field32(ia, 0x68, wave);
      h.set_field32(ia, 0x6c, h.field32(ia, 0x6c) + 1);
      h.set_field32(ia, 0x70, h.field32(ia, 0x70) + 1);
      const Handle ia2 = h.ia_game();
      if (h.field32(ia2, 0x44) != 2 || static_cast<int32_t>(h.field32(ia2, 0x6c)) - 1 <= 0x13) {
        h.fn_5392c0();
        h.fn_49dc80(0);
        h.objectives_free(kObj[0], kObj[1], kObj[2], kObj[3], kObj[4]);
        const std::string block = format("ia%d", h.game_mission_number());
        h.objectives_load("objectives.zrd", block.c_str(), kObj[0], kObj[1], kObj[2], kObj[3], kObj[4]);
        h.objectives_start(kObj[1], kObj[2], kObj[3], kObj[4]);
        ended = aborting = 0;
        if (scr) h.screen_reset(scr + 0x17964);
      }
    }
  } else if (type == 4) {
    const Handle t = h.training(kObj[0], kObj[1], kObj[2], kObj[3], kObj[4]);
    if (h.training_next(t)) {
      voice_line = false;
      ended = aborting = 0;
      if (scr) h.screen_reset(scr + 0x17964);
    }
  }
  if (voice_line && (!h.ia_game() || (mode() != 1 && mode() != 2))) ask_voice(sounds[0], 2.0f);  // com_success
  if (scr && announce) h.screen_flash(scr + 0x17964, -1.0f);
}

// Mech3.exe 1.2 @ 0x0048f9e0: the loss (also the valves' missionabort and
// the player's mech destroyed). In multiplayer the player's mech is put
// down unless it already is (+0x578 10) and the game goes on.
void Missions::lose() {
  MissionHooks& h = *g_hooks;
  if (h.multiplayer()) {
    const Handle v = h.player_vehicle();
    if (h.field32(v, 0x578) != 10) h.action_start(h.mech_action(v, 1));
    aborting = 0;
    return;
  }
  if (aborting) return;
  ended = aborting = 1;
  end_delay = 8.0f;
  h.sound_play(sounds[1], 1.0f);  // com_failed
  if (const Handle scr = cockpit(h)) h.screen_flash(scr + 0x17964, -1.0f);
}

// Mech3.exe 1.2 @ 0x0048fa80: the pause screen's REPLAY. The loss without its
// sound or flash and with no delay: the end screen comes on the next update.
void Missions::abort_now() {
  if (g_hooks->multiplayer()) return;
  end_delay = 0.0f;
  ended = 1;
  aborting = 1;
}

// Mech3.exe 1.2 @ 0x00491bf0 (also through 0x00491c70): the debriefing's
// fade (0x0048ef00 on the campaign's sequence 0x006099a0 or training's
// 0x006098c8); multiplayer and other games get none.
void Missions::end_screen() {
  MissionHooks& h = *g_hooks;
  switch (static_cast<uint32_t>(h.game_type())) {
    case 0: case 1:
      h.fn_40f0b0(0);
      h.fade_out(0x6099a0);
      break;
    case 2: h.fade_out(0x6099a0); break;
    case 4: h.fade_out(0x6098c8); break;
    default: break;
  }
  h.fn_40f0b0(0);
}

void MissionHooks::unload() { missions().unload(); }

// Mech3.exe 1.2 @ 0x00490480 (ECX the missions). Everything the world's load
// brought, in this order, then no world (+0x60). The voice set it unloads is
// named by the app's +0x14c, which nothing writes (an empty name: no set).
bool Missions::unload() {
  MissionHooks& h = *g_hooks;
  if (const Handle view = h.view()) h.camera_flag_off(h.field32(view, 4));
  h.ai_lists_free();
  h.fn_414000();
  h.mech_types_clear();
  h.mech_defs_clear();
  h.interp_5964c0(1);
  if (const Handle s = h.stealth()) h.delete_object(s);
  h.set_stealth(0);
  h.sound_set_unload_608dd4();
  h.effects_free();
  h.anims_free();
  h.decals_free();
  h.weapons_free();
  h.set_render_target(0);
  h.set_display_target(0);
  h.set_camera(0);
  h.view_free(h.view());  // its window (+0x08), then itself
  h.set_view(0);
  if (const Handle o = h.get_608c84()) h.delete_608c84(o);
  h.set_608c84(0);
  h.classes_free();
  h.models_free();
  h.textures_free();
  clear_content_world();
  for (int i = 0; i < 25; ++i) h.hud_line_clear(i);  // vt+0x7c(line, 0x005f4c90)
  current = -1;
  return true;
}

// Mech3.exe 1.2 @ 0x00491780: the mission's end. `unload` also drops the
// world (0x00490480); without it 0x005394c0(1, 1). Nothing when no mission
// is up. The two calls to the empty 0x005ac5c0 are left out.
bool Missions::cleanup(bool unload) {
  // Content snapshots change only after the complete world lifecycle ends.
  // A new authored mission never inherits stale native nodes or readers.
  if (content_world_is_authored(current)) unload = true;
  MissionHooks& h = *g_hooks;
  if (!loaded) return false;
  const Handle saved = h.fn_4eb040();
  if (unload) h.fn_4eb030(0);
  if (event) {
    h.close_handle(event);
    event = 0;
  }
  // `done` stands in for the event's signal (0x00490930): gone with it, so
  // the next briefing waits for its own mission's load before fitting the
  // lance (campaign.md section 13).
  done = 0;
  if (thread) {
    h.close_handle(thread);
    thread = 0;
    h.set_thread_priority(h.field32(0x608cb4, 0), 2);  // the game thread back to normal
  }
  h.fn_4e7bc0();
  if (xbc) xbc = h.fn_4582c0(xbc);
  h.anim_cleanup();
  h.fn_570410();
  const Handle bank = h.sounds_bank();
  h.sounds_stop(bank);
  h.sounds_free(bank);
  h.sound_reset();
  if (h.view()) {
    h.view_fn_40ae70(h.view());
    h.view_fn_40adf0(h.view());
  }
  const Handle vehicle = h.player_vehicle();
  h.pilot_set_vehicle(0);
  if (vehicle) h.delete_object(vehicle);
  const Handle stealth = h.stealth();
  if (stealth) h.entities_remove(stealth);  // kept out of what follows, then put back
  h.fn_4e6910();
  h.fn_4b4ae0();
  h.landmines_free();
  h.fn_49dc40();
  h.fn_4a7970();
  h.fn_4d8270();
  h.fn_584fe0();
  if (stealth) h.entities_add(h.stealth());
  h.fn_47a720(h.field32(0x60b544, 0));
  if (xb4) h.delete_object(xb4);
  xb4 = 0;
  stations = 0;
  h.fn_4d54f0();
  h.fn_479850();
  h.fn_4ea750();
  h.progress_text("Deleting AI Nets");
  h.znet_free();
  h.fn_4c5fa0();
  h.fn_4c3240();
  h.valves_end();
  h.targets_end();
  h.fn_4d71f0();
  h.fn_427300();
  h.lists_4f7bd0();
  h.objectives_free(kObj[0], kObj[1], kObj[2], kObj[3], kObj[4]);
  world_clear(world);
  h.fn_414000();
  char block[0x20];
  block_name(number, block);
  h.voice_release(format("%s%s", mission_dir(current), block).c_str());  // the mission's lines, "c1m1"
  h.fn_431800();
  h.fn_50b2f0();
  if (unload) {
    h.unload();
    h.progress_text("Done:Unloading db");
  } else {
    h.fn_5394c0();
  }
  h.fn_5573d0();
  h.fn_576270(0);
  h.fn_4eb030(saved);
  h.lists_4f7bd0();
  time = 0;
  h.game_time_set(time);
  return true;
}

}  // namespace mech3
