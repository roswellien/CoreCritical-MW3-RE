#include "gamez/zeffect/zeff_anim_save.h"
#include "gamez/zclass/display.h"
#include "gamez/zclass/window.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zimage/zimg_library.h"
#include "mech3/app_engine.h"
#include "mech3/game_editor.h"
#include "mech3/mods.h"
#include "mech3/force_feedback.h"

#if !defined(_WIN32)
#include <execinfo.h>
#endif
#include "gamez/zfmv/fmv_script.h"
#include "mech3/mission_map.h"
#include "mech3/patch12.h"
#include "mech3/app.h"
#include "mech3/hud.h"
#include "mech3/message_panel.h"
#include "mech3/sensors.h"

#include "gamez/zdeclient/zdec_crater.h"
#include "mech3/sound_extra.h"
#include "gamez/zdeclient/zdec_init.h"
#include "gamez/zeffect/zeff_init.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "mech3/mech_shade.h"
#include "mech3/app_mech.h"
#include "mech3/pilot.h"
#include "mech3/app_hud.h"
#include "mech3/controls_cfg.h"
#include "mech3/options.h"
#include "mech3/options_menu.h"
#include "mech3/pocket_panel.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "gamez/zclass/camera.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zinput/zinput.h"
#include "gamez/zinterp/zinterp.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zrender/zrndr_span.h"
#include "gamez/zrender/zrndr_flare.h"
#include "gamez/ztwod/twod.h"
#include "platform/platform.h"
#include "gamez/zrender/zrndr_blit.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zrender/zrndr_tint.h"
#include "gamez/zvideo/zvid_dd.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zvideo/zvid_format.h"
#include "mech3/mech3_interp.h"
#include "mech3/app_states.h"
#include "mech3/mission.h"
#include "mech3/app_view.h"
#include "mech3/mech_control.h"
#include "mech3/music.h"
#include "mech3/player.h"
#include "platform/platform.h"
#include "platform/render3d.h"
#include "platform/sound_output.h"

namespace mech3 {

void set_lod_scale(float s);  // mech_data.h (0x00430440)

namespace {

GameEngine* g_engine = nullptr;

// The options' calls into modules (mech3/options), wired where those are ported.
struct WiredOptionsHooks : OptionsHooks {
  void sound_set_volume(float v) override { zsnd::set_volume(v); }  // 0x0056f260
  void music_set_volume(int32_t l, int32_t r) override {           // 0x004ac230
    mech3_music::set_volume(static_cast<uint16_t>(l), static_cast<uint16_t>(r));
  }
  void model_set_global_light(int32_t on) override { zmod::set_shadows(on); }  // 0x00526a70
  void effects_set_level(int32_t level) override { zeff::anim_set_level(level); }  // 0x00508fb0
  void mechs_set_lod_scale(float s) override { mech3::set_lod_scale(s); }          // 0x00430440
  void shadows_set(uint8_t on) override { shade_set_option(on); }                  // 0x004e6920
  void input_set_screen(int32_t w, int32_t h) override { zin::mouse_set_screen(w, h); }  // 0x0051b590
  bool first_start_screen(int32_t* w, int32_t* h) override {
    platform::Window* win = g_engine ? g_engine->platform_parts().window : nullptr;
    int fw = 0, fh = 0;
    if (!win || !win->first_start_size(&fw, &fh)) return false;
    *w = fw, *h = fh;
    return true;
  }
  void camera_get_fov(Handle cam, float* x, float* y) override {  // 0x004f3ee0
    *x = *y = 1.0f;
    if (g_engine) zcls::camera_get_fov_angles(static_cast<zcls::Node*>(g_engine->object_of(cam)), x, y);
  }
  void camera_set_fov(Handle cam, float x, float y) override {  // 0x004f3d90
    if (g_engine) zcls::camera_set_fov_angles(static_cast<zcls::Node*>(g_engine->object_of(cam)), x, y);
  }
  int32_t video_mode() override { return g_engine ? static_cast<int32_t>(g_engine->peek32(0x72f348)) : 0; }
  // [0x0080271c] is zVideo's "opened" (0x00579c60 sets it, 0x00579ef0 clears it).
  uint32_t video_is_3d() override { return g_engine && g_engine->video_opened() ? 1 : 0; }  // 0x00579ea0
  uint32_t video_has_cap(int32_t cap) override { return g_engine ? g_engine->video_has_cap(cap) : 0; }
  const char* message(int32_t id) override { return g_engine ? g_engine->message(id) : ""; }  // 0x00576040
  void controls_set_defaults() override { mech3::controls_set_defaults(); }        // 0x00411150
  void controls_load(const char* name) override {  // 0x004120a0, after the saved map is read
    mech3::controls_load(name);
    // FIX (bug), W40: the registry's read filled the bindings only; without a
    // controls file the reverse tables stayed the defaults' (input.md).
    zin::map_refresh();
    mech3::controls_repair_mouse_fire();  // ours (W40): an older build's damage undone
    mech3::controls_keep_saved();         // the map the settings save (W40)
    mech3::settings_keep_mouse();         // and the mouse's values (ours)
  }
  void controls_free() override { mech3::controls_free_groups(); }                 // 0x00410a80
  Handle player_view() override { return g_engine ? g_engine->player_view() : 0; }  // [0x005f4c8c]
  void view_set_camera_mode(Handle, int32_t mode) override { program_view_set_camera(mode); }  // 0x0040ae70
  Handle find_node(int32_t kind, const char* name) override {  // 0x004f7fe0
    zcls::Node* n = zcls::list_find_by_name(kind, name);
    return n && g_engine ? g_engine->handle_of(n) : 0;
  }
  void node_set_active(Handle node, int32_t on) override {  // 0x004eb170
    if (g_engine) zcls::node_set_active(static_cast<zcls::Node*>(g_engine->object_of(node)), on);
  }
  void camera_set_lod_scale(Handle cam, float s) override {  // 0x004f3ff0
    if (g_engine) zcls::camera_set_lod_scale(static_cast<zcls::Node*>(g_engine->object_of(cam)), s);
  }
  // The options' setters on the player's mech (item 12): the pilot's +0x14.
  Handle player_mech() override { return g_engine && g_engine->pilot() ? g_engine->field32(g_engine->pilot(), 0x14) : 0; }
  void mech_set_invincible(Handle m, uint8_t on) override { if (g_engine) program_mech_set_invincible(g_engine->object_of(m), on != 0); }
  Handle mech_weapons(Handle m) override {
    return g_engine ? g_engine->handle_of(program_mech_weapons(g_engine->object_of(m))) : 0;
  }
  void weapons_set_infinite_ammo(Handle w, uint8_t on) override {  // 0x004b0220
    if (g_engine && w) program_weapons_set_unlimited(g_engine->object_of(w), on != 0);
  }
  void mech_set_torso_auto_return(Handle m, uint8_t on) override {  // [mech+0x39c]+0xa0
    if (g_engine) program_mech_set_torso_absolute(g_engine->object_of(m), on != 0);
  }
  void mech_set_throttle_decay(Handle m, uint8_t on) override {  // [mech+0x37c]+0x3f
    if (g_engine) program_mech_set_throttle_returns(g_engine->object_of(m), on != 0);
  }
  void mech_set_jump_jet_mode(Handle m, int32_t mode) override {  // 0x0041cf30
    if (g_engine) program_mech_control_mode(g_engine->object_of(m), mode);
  }
  // The display rect onto the mission's display (0x0040d6b0; item 15).
  void display_set_size(Handle d, int32_t width, int32_t height) override {  // 0x004fa580
    if (g_engine) zcls::display_set_size(static_cast<zcls::Node*>(g_engine->object_of(d)), width, height);
  }
  void display_set_origin(Handle d, int32_t x, int32_t y) override {  // 0x004fa600
    if (g_engine) zcls::display_set_origin(static_cast<zcls::Node*>(g_engine->object_of(d)), x, y);
  }
  // The render rect onto the mission's window (0x0040d670; item 12).
  void window_set_size(Handle w, int32_t width, int32_t height) override {  // 0x004f9fa0
    if (g_engine) zcls::window_set_size(static_cast<zcls::Node*>(g_engine->object_of(w)), width, height);
  }
  void window_set_origin(Handle w, int32_t x, int32_t y) override {  // 0x004fa0b0
    if (g_engine) zcls::window_set_origin(static_cast<zcls::Node*>(g_engine->object_of(w)), x, y);
  }
  uint32_t unported(const char* key, std::initializer_list<Arg> args) override {
    return g_engine ? g_engine->unported(key, args) : 0;
  }
};
WiredOptionsHooks g_options_hooks;

// zSound's engine variables (0x00597d10): the options that hold them.
void* find_var(const char* name) {
  if (std::strcmp(name, "SoundLOD") == 0) return &options().sound_lod;
  if (std::strcmp(name, "SoundVolume") == 0) return &options().sound_frequency;
  return nullptr;  // MuteSound is not an option of MW3's
}

void zerr_to_log(int level, const char* file, int line, const char* message) {
  std::fprintf(stderr, "mw3: report %#x %s:%d %s\n", level, file ? file : "", line, message ? message : "");
}

}  // namespace

int32_t program_camera_option() { return camera_mode(); }  // 0x0040ce70
void program_camera_set(int32_t mode) { set_camera_mode(mode); }  // 0x0040ce10
int32_t program_mouse_mode() { return options().mouse_mode; }
void program_set_mouse_mode(int32_t v) { options().mouse_mode = v; }

bool MountFiles::exists(const std::string& path) { return zutl::exists(path.c_str()); }

GameEngine::GameEngine(const Platform& p, std::string command_line)
    : p_(p), command_line_(std::move(command_line)), buffer_(640 * 480) {
  g_engine = this;
  ztimer::set_clock(p_.clock);
  zsnd_cd::set_backend(p_.files, p_.music, "music");
  set_options_hooks(&g_options_hooks);
  wire_mission_hooks(this);
  wire_pilot_hooks(this);
  wire_fmv(this);
  wire_frame_hooks(this);
  static ViewProgram vp;
  vp.ctx = this;
  vp.unported = [](void* c, const char* key) { return static_cast<GameEngine*>(c)->unported(key, {}); };
  vp.hardware = [](void* c) { return static_cast<GameEngine*>(c)->video_mode(); };
  vp.sync = [](void* c) { static_cast<GameEngine*>(c)->sync_view(); };
  wire_view(&vp);
  patch12::install();  // 1.0's data read as 1.2's (docs/patch12.md), and our own record edits
  options_sensitivity_slider() = true;  // ours: the options' MOUSE SENSITIVITY
  patch12::provide_files();  // and 1.2's added maps beside it, in the write dir
}

GameEngine::~GameEngine() {
  if (force_) program_force_feedback(nullptr), force_.reset();  // an engine gone without the app's stop: its calls off
  delete_screens();
  if (g_engine == this) {
    g_engine = nullptr;
    set_options_hooks(nullptr);
    wire_mission_hooks(nullptr);
    wire_pilot_hooks(nullptr);
    wire_fmv(nullptr);
    wire_frame_hooks(nullptr);
    wire_view(nullptr);
  }
  zutl::install_messages(nullptr);
  if (!g_engine) patch12::uninstall();
}

namespace {
// What each known key is (mission-plan item 11; program.md, The census):
// "own: ..." the program's equivalent, "deferred: ..." the decision,
// "no effect: ..." why nothing is lost.
const std::map<std::string, const char*>& known_keys() {
  static const std::map<std::string, const char*> k = {
      {"0x005561a0", "own: zrdr reads the archives through the mounts (zutl::mount); the loose-text path is unused"},
      {"0x005561e0", "own: the same (RdrAddPath)"},
      {"0x00516f50", "own: zutl::mount (the start-up's zbd through the 2D host's image_path); loose textures unused"},
      {"[0x0072f3f0]", "own: the fog is zrndr::device_fog in the Render3D pass"},
      {"0x00557f90", "own: zvid::lock_3d gives the fillers the back buffer's base and pitch each frame; the rect is unread"},
      {"0x00558290", "own: zvid::lock_3d sets the tint's buffer (zrndr::set_finish_target), 640x480"},
      {"0x00576d90", "own: the back buffer's size, zvid's Surface (lock_3d); here only the targets' arguments"},
      {"0x00576da0", "own: the back buffer's height, the same (lock_3d)"},
      {"0x00576dc0", "own: the back buffer's pitch, the same"},
      {"vt+0x7c", "deferred: developer only (the 25 lines cleared)"},
      {"hudline.vt+0x7c", "deferred: developer only (the 25 lines cleared)"},
      {"0x0040f7f0", "deferred: developer only (the 25 lines' Arial font)"},
      {"0x0043c820.debug", "deferred: developer only (the HUD frame's debug numbers into the five debug texts, hud.md)"},
      {"0x00609980", "no effect: the pocket's network element ([0x00609980]'s by its +0x20), 0 outside a network game"},
      {"0x0040f890", "deferred: developer only (the lines' and debug text's layers on)"},
      {"0x0040fef0", "no effect: the timing marks act only with [0x005e14f0], 0 in the data"},
      {"0x004b52e0", "no effect: the view's window object, its two rects the same (scale 1; item 6)"},
      {"0x0047f2b0", "no effect: defaults the world reader sets again, the console's vars (item 6)"},
      {"0x00518790", "no effect: zImage's flag set to 1, its default (zimg library enabled)"},
      {"0x005269e0", "no effect: [0x0074a2d4] is set only in software (gmod_light single)"},
      {"0x004f8f20", "no effect: zClass's two values set to their defaults (update_frame 1, frozen 0)"},
      {"0x004f3540", "no effect: [0x005ea53c] is never read"},
      {"0x00558070", "no effect: [0x008007dc] / [0x008007e0] are only written"},
      {"0x005a7e10", "own: CZAiTarget's defaults, the port's AiTarget initialisers (called with 0)"},
      {"0x005a7f90", "own: ~CZAiTarget, the port's AiTarget destructor"},
      {"0x00576c50", "own: zutl::crc32 builds its table on first use"},
      {"0x00535ed0", "no effect: the threshold is read only by the software draw 0x0053e050"},
      {"0x004f8f60", "no effect: zSave registration with flag 0; the port's classes_reset registers none either"},
      {"0x00537520", "no effect: Releases the used textures' system surfaces; the port's record is one Render3D handle"},
      {"0x00519e50", "no effect: zFont's bitmap fonts, loaded but never drawn or measured in 1.2 (only TextBase's own vtable reaches its draw, and no object keeps it; its centring needs +0x238, which nothing sets; twod.md section 4)"},
      {"0x00526510", "own: zModel's start, the port's struct initialisers; what differs is unread or set first (module-starts.md)"},
      {"0x005578f0", "own: zRender's start, the same (module-starts.md)"},
      {"0x00557b30", "own: zRender's reset, the software span table; the port's fillers are direct calls (module-starts.md)"},
      {"0x00575f70", "own: Mech3Msg.dll's FreeLibrary; the port reads the DLL as a file (zutl::MessageDll), freed with the engine"},
      {"0x00576c60", "own: zutl's search paths freed (0x00576250); the port's are the mounts, freed at the process's end"},
      {"0x00516f00", "own: zImage's tables freed at the app's stop (memory only, no write); the port's free themselves"},
      {"0x00526780", "own: zModel's materials and model pool freed at the app's stop (memory only); the port's free themselves"},
      {"0x00557b10", "own: two zRender buffers ([0x0072c1dc], [0x0072c1e0], 0x00558250) freed at the app's stop (memory only)"},
      {"0x005646d0", "own: the span fillers' two buffers freed at the app's stop (memory only)"},
      {"0x005a8390", "own: a target's centroid and target nodes (zent::marker_nodes); its name is 0, its +0x0c unread"},
  };
  return k;
}
}  // namespace

const char* GameEngine::known_unported(const std::string& key) {
  const auto& k = known_keys();
  const auto it = k.find(key);
  return it == k.end() ? nullptr : it->second;
}

uint32_t GameEngine::unported(const char* key, std::initializer_list<Arg> args) {
  // The base's records of calls it makes itself (for the call-trace tests).
  if (std::strcmp(key, "CString::Format") == 0 || std::strcmp(key, "sprintf") == 0) return 0;
  uint32_t& n = counts_[key];
  if (n++ == 0 && !quiet && !known_unported(key)) {
    std::string a;
    for (const Arg& x : args) {
      char buf[48];
      if (x.s) std::snprintf(buf, sizeof buf, " \"%.32s\"", x.s);
      else std::snprintf(buf, sizeof buf, " %#x", x.v);
      a += buf;
    }
    std::fprintf(stderr, "mw3: not ported: %s(%s ) -> 0 (logged once)\n", key, a.c_str());
  }
  // Ours, for finding a hook's caller: MW3_TRACE_UNPORTED=<key> prints where
  // each of its first eight reaches comes from.
#if !defined(_WIN32)  // execinfo is POSIX
  static const char* trace = std::getenv("MW3_TRACE_UNPORTED");
  if (trace && std::strcmp(trace, key) == 0 && n <= 8) {
    void* frames[16];
    const int k = backtrace(frames, 16);
    std::fprintf(stderr, "mw3: %s reached from:\n", key);
    backtrace_symbols_fd(frames, k, 2);
  }
#endif
  return 0;
}

void GameEngine::print_unported_summary() const {
  size_t gaps = 0;
  for (const auto& [k, n] : counts_) gaps += known_unported(k) ? 0 : 1;
  std::fprintf(stderr, "mw3: %zu original functions reached that are not ported:", gaps);
  int i = 0;
  for (const auto& [k, n] : counts_)
    if (!known_unported(k)) std::fprintf(stderr, "%s %s x%u", i++ % 6 ? "" : "\n ", k.c_str(), n);
  std::fprintf(stderr, "\n");
  std::fprintf(stderr, "mw3: %zu more reached, known (the program's own, deferred, or without effect):\n",
               counts_.size() - gaps);
  for (const auto& [k, n] : counts_)
    if (const char* why = known_unported(k)) std::fprintf(stderr, "  %s x%u: %s\n", k.c_str(), n, why);
  const auto& nulls = zint::null_calls();  // the scripts' engine entries left null (ours)
  std::fprintf(stderr, "mw3: %zu script engine calls skipped (no port):", nulls.size());
  i = 0;
  for (const auto& [k, n] : nulls) std::fprintf(stderr, "%s %s x%u", i++ % 4 ? "" : "\n ", k.c_str() + (k.rfind("e.", 0) == 0 ? 2 : 0), n);
  std::fprintf(stderr, "\n");
}

uint32_t GameEngine::peek32(uint32_t addr) {
  // The chosen pilot's +0x6c, its game (W42's lead): from the menus, before
  // any launch writes the stand-in game there, the pilot's current game, so
  // a game's queries (vt+0x2c: the lab's skins, the quit box) reach it.
  if (Pilot* p = current_pilot(); p && p->current && addr == handle_of(p) + 0x6c && !memory_.count(addr))
    return handle_of(p->current);
  return static_cast<uint32_t>(peek8(addr)) | static_cast<uint32_t>(peek8(addr + 1)) << 8 |
         static_cast<uint32_t>(peek8(addr + 2)) << 16 | static_cast<uint32_t>(peek8(addr + 3)) << 24;
}
uint8_t GameEngine::peek8(uint32_t addr) {
  if (addr == 0x60b56c) return player_get_60b56c();  // the ports' copy (mech3/player)
  auto it = memory_.find(addr);
  return it == memory_.end() ? 0 : it->second;
}
void GameEngine::poke32(uint32_t addr, uint32_t v) {
  for (int i = 0; i < 4; ++i) poke8(addr + i, static_cast<uint8_t>(v >> (8 * i)));
}
void GameEngine::poke8(uint32_t addr, uint8_t v) { memory_[addr] = v; }

// UpdateWindow: the window is shown once the engine has started.
void GameEngine::update_window(uint32_t) {
  if (p_.window) p_.window->show();
}

uint32_t GameEngine::message_box_text(uint32_t, const char* text, const char* caption, uint32_t type) {
  if (!p_.alert) return 1;
  return static_cast<uint32_t>(p_.alert->message_box(text ? text : "", caption ? caption : "", type));
}

void GameEngine::sleep(uint32_t ms) {
  if (p_.clock) p_.clock->sleep(ms);
}

// 0x00575f30: Mech3Msg.dll, found through the mounts. Missing, the original
// shows the system's error text and exits; so does this.
uint32_t GameEngine::message_dll_load(const char* name) {
  const std::string path = zutl::find(name);
  if (path.empty() || !zutl::read_message_dll(path, dll_)) {
    // Case differs between the exe's "mech3Msg.dll" and the file's name.
    const std::string alt = zutl::find("Mech3Msg.dll");
    if (alt.empty() || !zutl::read_message_dll(alt, dll_)) {
      if (p_.alert) p_.alert->abort_message(std::string("Cannot read ") + name + " from the game folder.");
      exit_process(1);
      return 0;
    }
  }
  patch12::messages(dll_);
  // Ours: the mods' strings.txt over the table (docs/modding.md).
  if (!zutl::mounts().mods.empty())
    for (const std::string& line : mods_apply_strings(dll_.text, dll_.ids)) std::printf("mw3: %s\n", line.c_str());
  zutl::install_messages(&dll_);
  return 1;
}

const char* GameEngine::message_format(int32_t id, const char* arg) {
  formatted_ = zutl::format_message(id, arg);
  return formatted_.c_str();
}
const char* GameEngine::message_format_n(int32_t id, int32_t n) {
  formatted_ = zutl::format_message_ints(id, {n});
  return formatted_.c_str();
}

// Mech3.exe 1.2 @ 0x004ac150: the game's message box, dlgMessage.zrd's
// MESSAGEBOX (the ported zTwoD box) over the screen, run in its own loop until
// a button answers; FastMenu off meanwhile. The text is formatted by the
// callers (0x00575ff0 with their arguments); the answer is the box's (IDOK 1,
// IDCANCEL 2, IDYES 6 ...).
uint32_t GameEngine::game_message_box(int32_t style, int32_t caption, const std::string& text) {
  video_begin_3d();
  video_end_3d();
  const int32_t fast = exchange_fast_menu(0);
  int32_t r = 0;
  {
    ztwod::MessageBox box("dlgMessage.zrd", "MESSAGEBOX", false);
    box_ = &box;
    const char* cap = message(caption);
    r = box.run(text.c_str(), cap ? cap : "", static_cast<uint32_t>(style));
    box_ = nullptr;
  }
  exchange_fast_menu(fast & 0xff);
  return static_cast<uint32_t>(r);
}

// FIX (A): the box's loop (0x005a7280) polls zInput only; the program's input
// arrives through the window's events, so the loop pumps them. A quit ends
// the box (no answer) and is posted again for the app's loop.
void GameEngine::pump_events() {
  zgame::App* a = app_if_any();
  platform::MessagePump* p = a ? a->message_pump() : nullptr;
  if (!p) return;
  while (p->peek()) {
    if (!p->pump()) {
      p->post_quit();
      if (box_) box_->frames = 0;
      return;
    }
  }
}

uint32_t GameEngine::message_box(int32_t a, int32_t caption, int32_t text, int32_t arg) {
  if (on_message_box) return on_message_box(a, caption, text);
  char buf[16];
  std::snprintf(buf, sizeof buf, "%d", arg);
  return game_message_box(a, caption, zutl::format_message(text, buf));
}

// 0x004ac150 with three strings (the controls screen's reassignment questions).
uint32_t GameEngine::message_box3(int32_t a, int32_t caption, int32_t text, const char* s1, const char* s2,
                                  const char* s3) {
  if (on_message_box) {
    last_box_text = zutl::format_message(text, {s1, s2, s3});
    return on_message_box(a, caption, text);
  }
  return game_message_box(a, caption, zutl::format_message(text, {s1, s2, s3}));
}

uint32_t GameEngine::message_box_plain(int32_t a, int32_t caption, const std::string& text) {
  if (on_message_box) {
    last_box_text = text;
    return on_message_box(a, caption, 0);
  }
  return game_message_box(a, caption, text);
}

Handle GameEngine::joy_state() { return handle_of(zin::joy_state()); }
float GameEngine::source_value(int32_t src, Handle joy) {
  return mech3::source_value(src, static_cast<const platform::JoyState*>(object_of(joy)), nullptr);
}

// 0x00410700 on a new interp object: "interp.zbd" with "DamageScale".
Handle GameEngine::script_new() {
  auto* in = new zint::Interp;
  interp_create(*in);
  interp() = in;
  return handle_of(in);
}
void GameEngine::script_delete(Handle s) {
  auto* in = static_cast<zint::Interp*>(object_of(s));
  if (interp() == in) interp() = nullptr;
  // Ours: interp.zbd, kept open since its first script (0x00596620), closed
  // with its object (the exe's stays open to the process's end; a second
  // app in one process, as the tests make, would leak a file each).
  if (in && in->archive.f) std::fclose(in->archive.f), in->archive.f = nullptr;
  delete in;
}

// CWnd::CreateEx for the 640x480 frame, shown larger (the game draws at its
// own resolution; the platform scales the frame to the window).
Handle GameEngine::frame_create(Handle) {
  hwnd_ = p_.window ? p_.window->create("", p_.window_width, p_.window_height) : 0x20000;
  return hwnd_;
}
void GameEngine::frame_set_title(Handle, const char* title) {
  if (p_.window) p_.window->set_title(title ? title : "");
}

// 0x0050da50 opens mech3.err; the rebuild's error reports go to stderr.
void GameEngine::error_log_open(uint32_t, uint32_t, const char*) { zerr::set_sink(zerr_to_log); }

void GameEngine::reader_init(const char*) { zrdr::init(); }
void GameEngine::reader_open_archive(const char* path, int32_t current) { zrdr::open_archive(path, current != 0); }
void GameEngine::reader_close_archives(int32_t all) { zrdr::close_archives(all != 0); }
// The app's end (0x0047fab0): zInput's maps and names, then the reader.
void GameEngine::input_maps_free() { zin::shutdown(); }
// The app's stop (0x004808a0, the Exit state's): the mechs' module.
void GameEngine::mechs_shutdown() { mech3::mechs_shutdown(); }
void GameEngine::reader_shutdown() { zrdr::shutdown(); }

// 0x00598210: the store's key, and the machine the detail settings are
// picked for. FIX (A): the platform describes a modern machine instead of the
// CPUID timing loop, GlobalMemoryStatus and DirectDraw's caps (settings.md).
void GameEngine::settings_init(const char* company, const char* product, const char* version) {
  zreg::Machine m;
  std::memcpy(m.vendor, "GenuineIntel", 13);
  m.cpu_class = 6;     // CPU_CLASS_PENTIUM_PRO: a P6-family CPU, as every modern x86 is
  m.cpu_mhz = 2000;
  m.flags = 0x41;      // MMX, a 3D card
  const uint64_t kb = static_cast<uint64_t>(p_.ram_mb) * 1024;
  m.ram_kb = static_cast<uint32_t>(kb > 0x3fffff ? 0x3fffff : kb);  // dwTotalPhys stops at 4 GB
  m.video_kb = 256 * 1024;
  zreg::init(company, product, version, m, install_options_);
  zreg::set_settings(p_.settings);
}

// 0x0040c420, the detail.zrd loader with the saved settings (mech3/options).
int32_t GameEngine::options_load(const char* name) { return load_options(name); }

int32_t GameEngine::sound_init(uint32_t window) {
  zsnd::Hooks& h = zsnd::hooks();
  h.output = p_.sound;
  h.find_var = find_var;
  h.cd_open = [](zrdr::Node* cd_tracks) { zsnd_cd::open(zsnd_cd::read_cues(cd_tracks)); };
  h.cd_close = [] { zsnd_cd::close(); };
  return zsnd::init(window);
}
void GameEngine::sound_set_mode(int32_t v) { zsnd::set_archives(v); }
// 0x004e6b00: each sound's EXTRA record (its repeat delay and subtitle),
// freed again by 0x004e6be0 when the groups shut down.
void GameEngine::sound_load(uint32_t window, const char* name, uint32_t callback, uint32_t a) {
  zsnd::load(window, name, callback ? mech3::sound_extra_load : nullptr, static_cast<int32_t>(a));
}
void GameEngine::sound_shutdown_groups(uint32_t callback) {
  zsnd::close_device(callback ? mech3::sound_extra_free : nullptr);
}
void GameEngine::sound_shutdown() { zsnd::shutdown(); }
void GameEngine::light_init() { Engine::light_init(); }

// Mech3.exe 1.2 @ 0x00516e90: zImage's start, which registers the image
// library (rimage.zbd: the menus' backgrounds, cursors and buttons) and, with
// ECX set, reads the bitmap fonts (0x00519e50, not ported: nothing draws them, twod.md section 4).
void GameEngine::fonts_init(const char* name, int32_t fonts) {
  zimg::register_image_library(1);
  if (fonts) unported("0x00519e50", {name});
}
// 0x00516f50: zImage's search directories. The start-up's "zbd" goes to the
// 2D host's mounts (SdlTwodHost::image_path), so the intro's logos find the
// image library; the missions' "zbd;zbd\<op>" and the lab's stay logged, as
// those mounts would stay for the next mission's files.
void GameEngine::geometry_init(const char* dir) {
  if (dir && std::strcmp(dir, "zbd") == 0) return ztwod::host().image_path(dir);
  Engine::geometry_init(dir);
}
void GameEngine::update_shadows() { update_mech_shadows(); }
void GameEngine::effects_set_a(int32_t v) { zeff::anim_log().save_on = v; }
void GameEngine::effects_set_b(int32_t v) { zeff::anim_log().net_on = v; }
void GameEngine::video_set_clear(int32_t on) { zvid::display().clear_on = on; }
void GameEngine::camera_set_hse_test(int32_t on) { zcls::scene_walk().hidden_surface = on; }
Handle GameEngine::app_sounds() { return app().field_1e4_; }
uint8_t GameEngine::app_flag_1c3() { return app().no_voices(); }

// zInput, ported (gamez/zinput, docs/input.md) on the platform's input.
void GameEngine::input_init(int32_t commands, const char* map) { zin::init(commands, map); }
void GameEngine::input_start(uint32_t window, uint32_t cmd_show) {
  zin::set_input(p_.input);
  zin::start(window, cmd_show);
}
void GameEngine::input_devices_shutdown() { zin::stop(); }
void GameEngine::input_acquire() { zin::resume(); }
void GameEngine::input_unacquire() { zin::suspend(); }
uint32_t GameEngine::joystick_present() { return zin::joy_present() ? 1 : 0; }
uint32_t GameEngine::force_feedback_present() { return zin::force_feedback(); }

// The force feedback (mech3/force_feedback): made by the app's start when
// zInput's joystick has it, on the platform's device (FIX (A): IFORCE2);
// the game's calls go to it while it is up (app_force.cpp).
Handle GameEngine::force_feedback_new() {
  force_ = std::make_unique<ForceFeedback>(p_.force);
  program_force_feedback(force_.get());
  return handle_of(force_.get());
}
void GameEngine::force_feedback_delete(Handle) {
  program_force_feedback(nullptr);
  force_.reset();
}
void GameEngine::force_feedback_spring(Handle, int32_t on) {
  if (force_) force_->spring(on != 0);
}
void GameEngine::input_set_mode(uint32_t mode) { zin::set_mode(mode); }
void GameEngine::input_poll(uint8_t callbacks) { zin::poll(callbacks != 0); }
uint32_t GameEngine::key_state(int32_t key) { return zin::key_state(key); }
void GameEngine::input_clear() { zin::kbd_reset(); }
void GameEngine::input_reset() { zin::reset(); }
void GameEngine::model_purge(int32_t models, int32_t materials) { zmod::models_purge(models != 0, materials != 0); }
void GameEngine::scene_begin_3d() { zrndr::flares_clear(); }
Handle GameEngine::scene_begin() { return static_cast<Handle>(zrndr::flare_count()); }
void GameEngine::input_capture() { zin::mouse_use(); }
void GameEngine::input_release() { zin::mouse_unuse(); }
void GameEngine::input_center_mouse() { zin::mouse_centre(); }
void GameEngine::mouse_set_pos(int32_t x, int32_t y) { zin::mouse_from_cursor(x, y); }
void GameEngine::mouse_set_accel(float x, float y) { zin::mouse_set_accel(x, y); }

void GameEngine::clock_init() { ztimer::init(); }

// 0x00579c60: the screen mode. Direct3D (video mode 1) when a card was
// chosen; the software renderer's modes are not offered (render.md).
int32_t GameEngine::video_open(uint32_t, int32_t hw, int32_t, int32_t) {
  poke32(0x72f348, hw ? 1u : 0u);
  if (!hw) unported("0x00579c60 software", {});
  video_opened_ = true;  // [0x0080271c]
  const Rect& d = mech3::display();
  const Rect& r = mech3::render();
  screen_size(d.w > 0 ? d.w : 640, d.h > 0 ? d.h : 480, r.w, r.h);
  return 0;
}

// The screen at a size (FIX (A): DirectDraw's surfaces and the device made
// for a mode): the 16-bit buffer, Render3D's viewport, the window's shape.
void GameEngine::screen_size(int32_t w, int32_t h, int32_t frame_w, int32_t frame_h) {
  if (p_.screen) p_.screen->cancel_overlay_16();
  width_ = w, height_ = h;
  // FIX (B): on a canvas (2D smaller than the frame) the dialogs' 640x480 frame centred on it.
  const bool canvas = frame_w > 0 && (frame_w != w || frame_h != h) && w >= 640 && h >= 480;
  ztwod::dialog_origin() = canvas ? ztwod::DialogOrigin{(w - 640) / 2, (h - 480) / 2} : ztwod::DialogOrigin{};
  buffer_.assign(static_cast<size_t>(width_) * height_, 0);
  buffer_is_screen_ = false;
  // FIX (B), playtest W21: the 2D target on the new buffer at once. The mode
  // switch (0x004ab6b0) locks before it, so the target kept the old mode's
  // size and the freed buffer; after the options' 640x480 a 16:9 mission's
  // HUD past x 640 (the weapon names, the speed gauge's ticks) was cut off.
  zrndr::Target2D& t = zrndr::target2d();
  t.base = reinterpret_cast<uint8_t*>(buffer_.data());
  t.width = static_cast<uint32_t>(width_), t.height = static_cast<uint32_t>(height_);
  t.pitch = static_cast<uint32_t>(width_ * 2);
  zrndr::set_finish_target(buffer_.data(), width_, height_, width_ * 2);
  // The 3D's viewport the frame (the 2D layers set their own, the canvas's; resolution.md step 4).
  if (p_.render) p_.render->set_viewport(0, 0, frame_w > 0 ? frame_w : w, frame_h > 0 ? frame_h : h);
  // The window's shape (ours, resolution.md step 2): the 3D frame, the 2D canvas over it (step 4).
  if (p_.window) p_.window->frame_size(frame_w > 0 ? frame_w : w, frame_h > 0 ? frame_h : h, w, h);
  // The tint quad as 0x00579c60 and the device's setup (0x0057a900, not
  // ported) leave it: unfogged, at depth 0.99 (without them the fades' quad
  // took the mission's fog colour); and fog enabled, as the setup leaves it
  // (without it nothing outside a mission's frames was fogged or tinted:
  // the lab's section highlight).
  zvid::reset_tint_quad();
  zvid::set_fog(1);             // 0x0057b05a: fog on from the device's setup
  zvid::set_tint_depth(0.99f);  // 0x3f7d70a4
}

// 0x00576c80: zVideo's sizes for the mode (0x0057a1a0: 5..10 as the options'
// modes; FIX (B) 11..16 with any_resolution, mech3::vmode_size), then the
// surfaces and the device made again (0x00577cd0), here the screen at that
// size. A mode zVideo does not know keeps the size it had.
int32_t GameEngine::video_set_mode(int32_t mode) {
  int32_t fw = 0, fh = 0, w = width_, h = height_;
  if (!mech3::vmode_size(mode, &fw, &fh) || !mech3::vmode_canvas(mode, &w, &h)) fw = w = width_, fh = h = height_;
  screen_size(w, h, fw, fh);
  return 0;
}

// 0x0040f330 on the options' rects by their handles (1 render, 2 display, 3 window).
void GameEngine::message_panel_place(Handle kept, Handle area) {
  auto rect = [](Handle r) -> const int32_t* {
    const Rect* p = r == 1 ? &mech3::render() : r == 2 ? &mech3::display() : r == 3 ? &mech3::window() : nullptr;
    return p ? &p->x : nullptr;
  };
  mech3::message_panel_place(rect(kept), rect(area));
}

// 0x004ab6b0's loop on the program's entities (zEntity's list is ours): every mech.
void GameEngine::screen_mode_cockpits() {
  for (const Handle e : ai::entity_list())
    if (entity_is_mech(object_of(e))) cockpit_refit(e);
}
void GameEngine::hud_setup(Handle hud, Handle mech) {
  if (auto* h = static_cast<Hud*>(object_of(hud))) h->setup(static_cast<Mech*>(object_of(mech)));
}

// [0x0072f38c]: the card's memory. A modern GPU, reported as 256 MB.
uint32_t GameEngine::video_card_memory(int32_t, uint32_t* total) {
  *total = 256u << 20;
  return 1;
}

namespace {
bool read_frame(platform::Render3D* r, int width, int height, std::vector<uint16_t>* out);
}

// 0x00576de0: the 16-bit back buffer locked for the 2D blits.
void GameEngine::video_lock() {
  // FIX (A): the exe's back buffer still held the last frame shown; after a
  // frame through Render3D the buffer holds only the layer, so the shown
  // frame is read into it first (once: the buffer is then the screen). The
  // message box over a mission's end draws on it, and its grabs see what it
  // drew (twod.md, the message box).
  if (last_via_render_ && !buffer_is_screen_) {
    std::vector<uint16_t> shown;
    if (read_frame(p_.render, width_, height_, &shown) && shown.size() == buffer_.size())
      std::copy(shown.begin(), shown.end(), buffer_.begin());
    buffer_is_screen_ = true;
  }
  zrndr::Target2D& t = zrndr::target2d();
  t.base = reinterpret_cast<uint8_t*>(buffer_.data());
  t.width = static_cast<uint32_t>(width_);
  t.height = static_cast<uint32_t>(height_);
  t.pitch = static_cast<uint32_t>(width_ * 2);
  t.format = 6;
  zrndr::set_finish_target(buffer_.data(), width_, height_, width_ * 2);  // 0x00558290, as 0x00576de0 sets it
}
void GameEngine::video_clear(Handle) {
  zrndr::observe_framebuffer_write(buffer_.data(), "frame clear");
  std::fill(buffer_.begin(), buffer_.end(), uint16_t{0});
}
// 0x00576ea0: the back buffer to the screen.
// A frame whose 3D scene was drawn (the Core state's) shows that instead.
void GameEngine::video_flip(int32_t, int32_t, int32_t, int32_t) {
  // FIX (B), playtest W25: a message box over a GPU frame shows over that
  // frame at its own size, not over the 16-bit buffer's copy of it (the
  // buffer's size: the menus' 640x480, low-res at 16:9 or in the lab).
  // FIX (B), playtest W55: the message box's own empty 3D lock (0x004ac150's
  // first) draws only the key colour; it does not count as a frame drawn,
  // or the lab's held frame was dropped and the box shown over the key's
  // magenta.
  if (held_ && !scene_drawn_ && (!layer_drawn_ || layer_blank_) && p_.render && p_.screen && p_.render->restore_frame()) {
    p_.screen->overlay_16(buffer_.data(), width_, height_, width_ * 2, true, kLayerKey, nullptr, nullptr);
    p_.render->present();
    ++frames_presented;
    if (on_present) on_present(frames_presented);
    return;
  }
  if (held_) drop_held();
  if ((scene_drawn_ || layer_drawn_) && p_.render) p_.render->present();
  else if (p_.screen) p_.screen->present_16(buffer_.data(), width_, height_, width_ * 2, true);
  last_drew_scene_ = scene_drawn_;
  last_via_render_ = (scene_drawn_ || layer_drawn_) && p_.render;
  buffer_is_screen_ = !last_via_render_;
  scene_drawn_ = layer_drawn_ = false;
  layer_blank_ = true;
  ++frames_presented;
  if (on_present) on_present(frames_presented);
}
// 0x004f7d80: the zClass action lists, each followed by the frame update
// (docs/action-lists.md): the player's tick (2), weapon_tick (3), the
// mission's animations and the scripts' ticks.
void GameEngine::lists_update() { zcls::run_action_lists(); }
void GameEngine::classes_shutdown() { zcls::classes_reset(); }
int32_t GameEngine::effects_init() {
  zeff::effects_init();
  return 0;
}
void GameEngine::effects_shutdown() { zeff::effects_shutdown(); }
void GameEngine::decals_set_mode(int32_t v) { zdec::set_mode(static_cast<uint32_t>(v)); }
void GameEngine::decals_shutdown() { zdec::decals_free(); }
void GameEngine::missions_shutdown() { mech3::mech_types_close(); }
const char* GameEngine::video_driver_label() const { return p_.render ? p_.render->driver_name() : "Default"; }
const char* GameEngine::video_mode_label() {
  static char buf[32];
  std::snprintf(buf, sizeof buf, "%d x %d x 16", width_, height_);
  return buf;
}
// 0x00576ce0 -> 0x00577770: a DDBLT_DEPTHFILL Blt over `rect`, the whole
// depth surface for 0 (the Core state's start).
// Mech3.exe 1.2 @ 0x00558010: the 16-bit target's size ([0x008007cc],
// [0x008007d0]) from the rect, in both of the port's views of it.
// The rect is a handle: the screen's (0x00579880, the 16-bit buffer's size)
// or one of the options' three, not memory (reading it as memory gave 0 x 0,
// and every 2D draw after the mech lab's call was clipped away).
void GameEngine::render_flush(Handle rect) {
  if (!rect) return;
  int32_t w = width_, h = height_;
  if (rect != kScreenRect) {
    const Rect* r = rect == render_rect() ? &mech3::render()
                    : rect == display_rect() ? &mech3::display()
                    : rect == window_rect() ? &mech3::window() : nullptr;
    if (!r) return;
    w = r->w, h = r->h;
  }
  zrndr::soft_target().width = w, zrndr::soft_target().height = h;
  zrndr::target2d().width = static_cast<uint32_t>(w), zrndr::target2d().height = static_cast<uint32_t>(h);
}
// Mech3.exe 1.2 @ 0x004f8f50: [0x0060bf48], set by zClass's set-up
// (0x004f8f70) and cleared by its reset: the node pool live (as the weapons' pool_live).
bool GameEngine::world_up() { return !zcls::nodes().blocks.empty(); }
// 0x0050ace0 at the Core state's leave: every mission animation stopped.
void GameEngine::anim_stop(int32_t a) { zeff::anim_stop_all(a); }
// 0x0050ad60 with ECX 1, the debriefing's retry (0x0047d030) and Instant
// Action's next round: the mission's animations back to their start.
void GameEngine::anim_reset() { zeff::anim_reset_all(1); }
void GameEngine::video_clear_a(Handle rect) {
  const zvid::Surface& b = zvid::surfaces().back;
  int32_t r[4] = {0, 0, b.width, b.height};
  if (rect)
    for (int i = 0; i < 4; ++i) r[i] = static_cast<int32_t>(peek32(rect + 4 * i));
  zvid::depth_fill(r);
}
// 0x00576cf0: the frame before the Core state draws. The exe's colour clear is off here (0x005777f0), depth
// only. FIX (bug): with bug fixes on, the fog's colour fills what no draw covers (zmod::fog_frame_fill).
void GameEngine::video_clear_b(Handle) {
  if (!p_.render) return;
  uint8_t r, g, b;
  zmod::fog_frame_fill(&r, &g, &b);
  p_.render->clear(zfix::options().bugs, r, g, b);
}
// 0x00579840: the cursor's on/off, the old state back. The menus draw their own.
uint32_t GameEngine::video_show_cursor(uint32_t on) {
  const uint32_t old = cursor_;
  cursor_ = on;
  return old;
}

// FIX (A): the fade's DirectDraw and Direct3D calls on the 16-bit buffer
// (fixes.md, Running on modern machines).
// 0x00579500: the screen to a new image. The port has one buffer, whichever
// the original asked for. Handles of freed stills are reused. FIX (A): the
// screen is the GPU's back buffer, read back and scaled to the game's 16-bit
// size (the exe's Lock read the DirectDraw surface); without a readback, the
// 16-bit buffer (the menus' screens, drawn there in full).
namespace {
// The read-back frame scaled to the buffer's size, in the buffer's format.
bool read_frame(platform::Render3D* r, int width, int height, std::vector<uint16_t>* out) {
  std::vector<uint32_t> px;
  int w = 0, ht = 0;
  if (!r || !r->read_back(&px, &w, &ht) || w <= 0 || ht <= 0) return false;
  out->assign(static_cast<size_t>(width) * height, 0);
  for (int y = 0; y < height; ++y) {
    const uint32_t* row = px.data() + static_cast<size_t>(y * ht / height) * w;
    for (int x = 0; x < width; ++x) {
      const uint32_t c = row[x * w / width];
      (*out)[static_cast<size_t>(y) * width + x] = zvid::pack_bytes(c >> 16 & 0xff, c >> 8 & 0xff, c & 0xff);
    }
  }
  return true;
}
}  // namespace

Handle GameEngine::video_grab(int32_t) {
  Handle h = kGrabs;
  while (grabs_.count(h)) h += 0x10;
  std::vector<uint16_t> still;
  if (video_mode() != 0 && read_frame(p_.render, width_, height_, &still)) grabs_[h] = {width_, height_, std::move(still)};
  else grabs_[h] = {width_, height_, buffer_};
  return h;
}
bool GameEngine::hold_shown_frame() {
  if (held_) return true;
  if (!p_.render || !p_.screen || !last_via_render_ || buffer_is_screen_ || !p_.render->hold_frame()) return false;
  held_ = true;
  std::fill(buffer_.begin(), buffer_.end(), kLayerKey);
  buffer_is_screen_ = true;  // no read-back into it at the next lock: it is the box's layer
  return true;
}
void GameEngine::drop_held() {
  if (!held_) return;
  held_ = false;
  if (p_.render) p_.render->drop_frame();
}
bool GameEngine::shown_frame(std::vector<uint16_t>* out) {
  return last_via_render_ && !buffer_is_screen_ && read_frame(p_.render, width_, height_, out);
}
// 0x0055b1f0: the fade's still back over the buffer, whole, at 0, 0. A still
// taken at another size (the mission's canvas, 853x480 at 16:9, faded out
// after the switch to the menus' 640x480) is scaled to the buffer: ours
// (fixes.md, The screen grab in Direct3D).
void GameEngine::image_draw(Handle image, int32_t x, int32_t y, int32_t key, Handle rect) {
  auto it = grabs_.find(image);
  if (it == grabs_.end() || x || y || rect) return (void)Engine::image_draw(image, x, y, key, rect);
  const Grab& g = it->second;
  zrndr::observe_framebuffer_write(buffer_.data(), "frame grab blit");
  if (g.width == width_ && g.height == height_ && g.pixels.size() == buffer_.size()) {
    buffer_ = g.pixels;
    return;
  }
  if (g.width <= 0 || g.height <= 0 || g.pixels.size() != static_cast<size_t>(g.width) * g.height) return;
  for (int yy = 0; yy < height_; ++yy) {
    const uint16_t* src = g.pixels.data() + static_cast<size_t>(yy * g.height / height_) * g.width;
    for (int xx = 0; xx < width_; ++xx) buffer_[static_cast<size_t>(yy) * width_ + xx] = src[xx * g.width / width_];
  }
}
// 0x00558540, ported (zrndr::tint): kept for the software finish, or the
// Direct3D quad. A rect is four ints in the modules' memory.
void GameEngine::screen_tint(uint16_t colour, Handle rect, double alpha) {
  int32_t r[4] = {};
  if (rect)
    for (int i = 0; i < 4; ++i) r[i] = static_cast<int32_t>(peek32(rect + 4 * i));
  zrndr::tint(colour, rect ? r : nullptr, alpha, video_mode() != 0);
}
void GameEngine::image_free(Handle image) {
  if (!grabs_.erase(image)) Engine::image_free(image);
}

// The Direct3D frame (render-frame.md): the ported lock, scene nesting, quad
// and finish. FIX (A): the back buffer's Lock gives the 16-bit layer, filled
// with kLayerKey; its Unlock lays what the 2D code drew over the 3D frame.
// 0x0040f8d0: the message panel's slide, then the view target's HUD data.
void GameEngine::camera_update() {
  if (MessagePanel* p = message_panel()) p->slide(frame_dt());
  program_hud_data(frame_dt());
}
// Mech3.exe 1.2 @ 0x0040fac0. Between the view target's frame and the
// pocket commander the exe also runs the developer lines (0x0040fb50(2) and
// their layer 0x005fc338), the frame-rate text (0x0040f1f0 on 0x005fc378)
// and the debug text's layer 0x005fc838, and after it the interp console's
// layer 0x00731ac0: none reachable in the release (hud.md section 20).
void GameEngine::hud_update() {
  const float dt = frame_dt();
  program_hud_update(dt);                                // [[0x005f4c8c]+0x64] vt+0xe0
  if (MessagePanel* p = message_panel()) p->update(dt);  // [0x00603d7c] vt+0x24: its text
  pocket_draw(dt);                                       // 0x0046a410
}
void GameEngine::hud_draw() { editor_frame(); }
Handle GameEngine::pilot_hud(Handle data) { return handle_of(program_pilot_hud(object_of(data))); }
void GameEngine::hud_static(Handle hud, float dt) { program_hud_static(object_of(hud), dt); }
bool program_intensity_flag() { return options().intensity_flag[0] != 0; }
bool program_cheated() { return options().cheated != 0; }
void GameEngine::hud_zoom_pass(Handle hud, float dt) { program_hud_zoom_pass(object_of(hud), dt); }
void GameEngine::render_reset() {
  zrndr::finish_init();
  unported("0x00557b30", {});
}
void GameEngine::message_panel_init(Handle, Handle) { mech3::message_panel_create(); }
void GameEngine::message_panel_free() { mech3::message_panel_destroy(); }
// The mission's network start and end (mech_net.cpp, multiplayer-port.md piece 5).
void GameEngine::multiplayer_launch_prep() { net_start(); }
void GameEngine::network_leave() { net_end(); }
void GameEngine::video_begin_3d() { zvid::lock_3d(); }
void GameEngine::video_end_3d() { zvid::unlock_3d(); }
void GameEngine::video_begin_scene() { zvid::scene_begin(); }
void GameEngine::video_draw_3d() { zvid::draw_tint_quad(); }
void GameEngine::video_end_scene() { zvid::scene_end(); }
void GameEngine::render_finish() { zrndr::finish(); }

// The layer's alpha blends are recorded while it is locked, so they go over
// the frame see-through instead of mixed with the key (zrndr::LayerCover).
int32_t GameEngine::lock_back(zvid::Surface* s) {
  // The 2D picture the buffer held, kept for a lock that draws nothing (the
  // message box's 0x004ac150 locks and unlocks first): the exe's lock left
  // the back buffer's picture as it was.
  if (buffer_is_screen_) kept_screen_ = buffer_;
  else kept_screen_.clear();
  std::fill(buffer_.begin(), buffer_.end(), kLayerKey);
  buffer_is_screen_ = false;
  cover_alpha_.assign(buffer_.size(), 0);
  cover_colour_.resize(buffer_.size());
  zrndr::layer_cover() = {cover_alpha_.data(), cover_colour_.data(), kLayerKey};
  s->width = width_, s->height = height_, s->pitch = width_ * 2, s->pixels = buffer_.data();
  if (p_.screen) p_.screen->begin_overlay_16(buffer_.data(), width_, height_, width_ * 2);
  return 0;
}
int32_t GameEngine::unlock_back(zvid::Surface*) {
  zrndr::layer_cover() = {};
  if (!kept_screen_.empty() &&
      std::all_of(buffer_.begin(), buffer_.end(), [](uint16_t p) { return p == kLayerKey; })) {
    if (p_.screen) p_.screen->cancel_overlay_16();
    std::copy(kept_screen_.begin(), kept_screen_.end(), buffer_.begin());  // in place: the targets point at it
    kept_screen_.clear();
    buffer_is_screen_ = true;
    return 0;
  }
  // Scanned only where it can matter (the held path in video_flip: no scene
  // drawn), stopping at the first pixel drawn.
  const bool blank = !scene_drawn_ && kept_screen_.empty() &&
                     std::all_of(buffer_.begin(), buffer_.end(), [](uint16_t p) { return p == kLayerKey; });
  kept_screen_.clear();
  if (p_.screen)
    p_.screen->overlay_16(buffer_.data(), width_, height_, width_ * 2, true, kLayerKey, cover_alpha_.data(),
                          cover_colour_.data());
  layer_drawn_ = true;
  layer_blank_ = layer_blank_ && blank;
  return 0;
}

// 0x0057dd80 behind the colour tint (0x00558540). FIX (B): its rect is the 2D
// canvas's (the finish target's, or the caller's), and the quad is drawn in the
// frame's pixels; at a mode past 640x480 the canvas is smaller than the frame,
// so the fades darkened only its top-left part (playtest S14). Scaled onto the
// frame as the HUD's panels are (resolution.md step 4).
void tint_quad_on_frame(uint16_t colour, const int32_t* rect, double alpha) {
  if (!rect) return zvid::set_tint_quad(colour, nullptr, alpha);
  int32_t x = rect[0], y = rect[1], w = rect[2] - rect[0], h = rect[3] - rect[1];
  canvas_to_render(&x, &y, &w, &h);
  const int32_t r[4] = {x, y, x + w, y + h};
  zvid::set_tint_quad(colour, r, alpha);
}

void wire_frame_hooks(GameEngine* e) {
  zvid::surface_hooks() = e ? zvid::SurfaceHooks{[](void* c, zvid::Surface* s) {
                                                    return static_cast<GameEngine*>(c)->lock_back(s);
                                                  },
                                                  [](void* c, zvid::Surface* s) {
                                                    return static_cast<GameEngine*>(c)->unlock_back(s);
                                                  },
                                                  e}
                            : zvid::SurfaceHooks{};
  zrndr::tint_hooks().quad = e ? tint_quad_on_frame : nullptr;
  zvid::scene_depth() = 0;
}

Handle GameEngine::handle_of(void* p) {
  if (!p) return 0;
  auto it = handles_.find(p);
  if (it != handles_.end()) return it->second;
  objects_.push_back(p);
  const Handle h = 0x00100000u + static_cast<Handle>(objects_.size());
  handles_[p] = h;
  return h;
}
void GameEngine::forget_object(const void* p) {
  auto it = handles_.find(const_cast<void*>(p));
  if (it == handles_.end()) return;
  objects_[it->second - 0x00100001u] = nullptr;
  handles_.erase(it);
}
void* GameEngine::object_of(Handle h) const {
  if (h <= 0x00100000u || h - 0x00100000u > objects_.size()) return nullptr;
  return objects_[h - 0x00100001u];
}

void GameEngine::sound_update(int32_t a, int32_t b) { zsnd::frame(a, b); }
Handle GameEngine::sound_find(const char* name) { return handle_of(zsnd::find(name)); }
void GameEngine::sound_play(Handle s, float v) {
  if (auto* o = static_cast<zsnd::Object*>(object_of(s))) zsnd::play(o, v);
}
Handle GameEngine::sound_listener() { return handle_of(zsnd::snapshot()); }
void GameEngine::sound_stop_all(Handle s) {
  if (auto* o = static_cast<zsnd::Snapshot*>(object_of(s))) zsnd::stop_snapshot(o);
}
void GameEngine::sound_resume_all(Handle s) {
  if (auto* o = static_cast<zsnd::Snapshot*>(object_of(s))) zsnd::resume_snapshot(o);
}
Handle GameEngine::sound_take_playing() { return handle_of(zsnd::take_playing()); }
// The list is zSound's again (or freed) after either: its handle goes.
void GameEngine::sound_restore_playing(Handle l) {
  if (auto* o = static_cast<std::list<zsnd::GroupVoice*>*>(object_of(l))) {
    handles_.erase(o);
    objects_[l - 0x00100001u] = nullptr;
    zsnd::restore_playing(o);
  }
}
void GameEngine::sound_free_playing(Handle l) {
  if (auto* o = static_cast<std::list<zsnd::GroupVoice*>*>(object_of(l))) {
    handles_.erase(o);
    objects_[l - 0x00100001u] = nullptr;
    zsnd::free_playing(o);
  }
}
void GameEngine::mission_abort() { missions().abort_now(); }
void GameEngine::maps_init(const char* name) { maps_read(name); }
void GameEngine::mech_data_shutdown() { maps_free(); }
void GameEngine::set_reopen_training(uint8_t v) { app().set_reopen_training(v); }
// 0x00590250 (the app's replace, arg 0) by a state's address: the Win Movie
// State's the debriefing's after the campaign's last mission.
void GameEngine::replace_state_at(Handle state) {
  if (state == states().win_movie.addr()) app().replace(&states().win_movie, 0);
  else Engine::replace_state_at(state);
}
// 0x0048ef00 on the missions object's +0xd0 (the pause screen's QUIT in
// Instant Action): the debriefing's Win State after the fade; on the IA
// Dummy State (the debriefing's ABORT in Instant Action).
void GameEngine::fade_to_state(Handle next, uint32_t arg, int32_t direction, int32_t buffer, float seconds,
                               uint8_t r, uint8_t g, uint8_t b) {
  if (next == 0x6099a0) fade_replace(&states().end_win, arg, direction, buffer, seconds, r, g, b);
  else if (next == 0x609848) fade_replace(&states().multi_exit, arg, direction, buffer, seconds, r, g, b);  // the quit box's (0x0048e390): a second Multi-Exit
  else if (next == states().ia_dummy.addr()) fade_replace(&states().ia_dummy, arg, direction, buffer, seconds, r, g, b);
  else Engine::fade_to_state(next, arg, direction, buffer, seconds, r, g, b);
}
void GameEngine::sound_release_all(Handle s) {
  if (auto* o = static_cast<zsnd::Snapshot*>(object_of(s))) {
    zsnd::free_snapshot(o);
    handles_.erase(o);
    objects_[s - 0x00100001u] = nullptr;
  }
}
void GameEngine::sound_reset() { zsnd::reset(); }
void GameEngine::voice_start(const char* name) { zsnd::load_set(name); }
void GameEngine::voice_stop(Handle s) {
  if (auto* v = static_cast<zsnd::VoiceBase*>(object_of(s))) zsnd::stop(v);
}
void GameEngine::voice_release(const char* name) { zsnd::unload_set(name); }

int32_t GameEngine::model_global_light() { return zmod::shadows_on(); }
void GameEngine::model_set_global_light(int32_t on) { zmod::set_shadows(on); }
void GameEngine::effects_set_level(int32_t v) { zeff::anim_set_level(v); }  // 0x00508fb0
void GameEngine::shadows_reset() { shade_apply(); }  // 0x004e6900: the option as set

}  // namespace mech3

namespace mech3 {
// W45: a network game in play runs on while the window is inactive (FIX (A)).
bool GameEngine::run_while_inactive() { return program_net_running(); }
}  // namespace mech3
