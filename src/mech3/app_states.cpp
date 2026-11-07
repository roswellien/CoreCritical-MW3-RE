#include "mech3/app_states.h"
#include "mech3/content_campaign.h"
#include "mech3/content_stock_campaign.h"
#include "gamez/zutil/zfix.h"
#include "mech3/app_shadows.h"

#include "crt/msvc_math.h"
#include "gamez/zvideo/zvid_format.h"
#include "mech3/app.h"
#include "mech3/pilot.h"

namespace mech3 {

namespace {
States* g_states = nullptr;
}  // namespace

States& states() {
  if (!g_states) g_states = new States;
  return *g_states;
}
void reset_states() {
  delete g_states;
  g_states = new States;
}

Engine& State::e() { return app().engine(); }
Mech3App& State::a() { return app(); }

namespace {
// The render-target setup that opens several states: the back buffer, a depth or
// buffer word, a rectangle and the target (0x00557f90's four arguments).
void target_menu(Engine& e) {  // the 0x00576c70 / 0x00579880 form
  const Handle a = e.video_back_buffer();
  const Handle b = e.video_front_buffer();
  const Handle r = e.video_screen_rect();
  e.render_set_target(e.video_target(), r, b, a);
}
void target_second(Engine& e) {  // 0x00558290 with 0x00576da0 / 0x00576d90
  const Handle a = e.video_back_buffer();
  const Handle b = e.video_buffer_da0();
  const Handle r = e.video_buffer_d90();
  e.render_set_target2(e.video_target(), r, b, a);
}
// new(0x20) + 0x00598cc0(0, 0, 0), told the main window, then the named fmv.zrd
// script started when it loads.
Handle start_movie(Engine& e, const char* name) {
  const Handle m = e.movie_new();
  const Handle frame = app().frame_;
  const Handle hwnd = frame ? app().engine().frame_hwnd(frame) : 0;
  if (hwnd) e.movie_set_window(m, hwnd);
  if (e.movie_load(m, "fmv.zrd", name) != -1) e.movie_play(m);
  return m;
}
void end_movie(Engine& e, Handle m) {
  e.movie_stop(m, 0);
  if (m) e.movie_delete(m);
}
}  // namespace

// ---------------------------------------------------------------------------
// Attract

// Mech3.exe 1.2 @ 0x00482330: the CD music pauses for the movie.
bool AttractState::enter() {
  Engine& en = e();
  target_menu(en);
  en.render_set_pitch(en.video_pitch());
  target_second(en);
  if (a().fmv()) {
    en.cd_pause();
    movie_ = start_movie(en, "ATTRACT");
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00482430
bool AttractState::update() {
  Engine& en = e();
  en.input_poll(0);
  if (movie_ == 0 || !en.movie_playing(movie_)) {
    a().pop(1);
    en.cd_resume(6);
  }
  return false;
}

// Mech3.exe 1.2 @ 0x00482470; the movie pointer is left as it was.
void AttractState::leave() {
  if (movie_) end_movie(e(), movie_);
}

// ---------------------------------------------------------------------------
// Win Movie

// Mech3.exe 1.2 @ 0x0047a9b0: the movie whatever the -nofmv setting.
bool WinMovieState::enter() {
  movie_ = start_movie(e(), "c4win");
  return true;
}

// Mech3.exe 1.2 @ 0x0047aa50: once the movie is over (or there is none), the
// main menu's credits flag [0x00608e4e] set and Main in this state's place.
bool WinMovieState::update() {
  Engine& en = e();
  bool done = movie_ == 0;
  if (movie_) {
    done = !en.movie_playing(movie_);
    if (done) {
      end_movie(en, movie_);
      movie_ = 0;
    }
  }
  if (done) {
    a().reopen_credits() = 1;
    a().replace(a().state(2), 0);
  }
  return false;
}

// Mech3.exe 1.2 @ 0x0047aac0
void WinMovieState::leave() {
  if (movie_) end_movie(e(), movie_);
  movie_ = 0;
}

// ---------------------------------------------------------------------------
// IA Dummy

// Mech3.exe 1.2 @ 0x0047ce80. A mission won or lost ([0x00609925]) counts in
// Attack and Defend only; left before its end (the pause screen's QUIT) it
// counts nothing but the kills.
bool IaDummyState::enter() {
  Engine& en = e();
  en.action_tally();
  Pilot* p = current_pilot();
  ActionGame* g = p ? p->action() : nullptr;
  if (g && en.mission_ended()) {
    const bool lost = en.mission_failed();  // [0x00609926]
    if (g->mode == 3) ++(lost ? g->attack_losses : g->attack_wins);
    else if (g->mode == 4) ++(lost ? g->defend_losses : g->defend_wins);
  }
  if (p) p->save();  // (the exe saves the pilot unchecked)
  en.mission_cleanup(1);
  a().set_reopen_instant_action(1, 1);
  en.replace_state(2);  // 0x00590250 with 0x00482970(2)
  return true;
}

// ---------------------------------------------------------------------------
// TR Dummy

// Mech3.exe 1.2 @ 0x00491ab0: won ([0x00609925] and not [0x00609926]), the
// training mission chosen (+0x40) passed (+0x3b + it) and the pilot saved.
bool TrDummyState::enter() {
  Engine& en = e();
  if (en.mission_ended() && !en.mission_failed()) {
    Pilot* p = current_pilot();
    if (TrainingGame* t = p ? p->training_game() : nullptr) {
      if (t->mission >= 1 && t->mission <= 4) t->passed[t->mission - 1] = 1;  // (the exe writes +0x3b + any value)
      p->save();
    }
  }
  en.mission_cleanup(1);
  a().set_reopen_training(1);  // 0x0047fe00(1)
  en.replace_state(2);
  return true;
}

// ---------------------------------------------------------------------------
// Multi-Exit

// Mech3.exe 1.2 @ 0x00470710
bool MultiExitState::enter() {
  Engine& en = e();
  en.mission_cleanup(1);  // 0x00491780
  en.replace_state(2);    // 0x00590250 with 0x00482970(2): the main menu
  return true;
}

// ---------------------------------------------------------------------------
// Intro

// Mech3.exe 1.2 @ 0x00482070. -nofmv skips INTRO only: the SPLASH logos in
// update() check skip_intro alone.
bool IntroState::enter() {
  Engine& en = e();
  const Handle back = en.video_back_buffer();
  const Handle depth = en.display_depth();
  const Handle rect = en.display_rect();
  en.render_set_target(en.video_target(), rect, depth, back);
  en.render_set_pitch(en.vstride());
  target_second(en);
  if (!a().lobby_flow_ && a().fmv()) intro_ = start_movie(en, "INTRO");
  return true;
}

// Mech3.exe 1.2 @ 0x00482170
bool IntroState::update() {
  Engine& en = e();
  en.input_poll(0);
  if (intro_) {
    if (en.movie_playing(intro_)) return false;
    end_movie(en, intro_);
    intro_ = 0;
    return false;
  }
  if (splash_ == 0 && !a().lobby_flow_) splash_ = start_movie(en, "SPLASH");
  if (splash_ && !en.movie_playing(splash_)) {
    end_movie(en, splash_);
    splash_ = 0;
  }
  if (splash_ == 0) a().replace(a().state(12), 0);
  return false;
}

// Mech3.exe 1.2 @ 0x004822d0: forgets the movies without stopping them.
void IntroState::leave() {
  splash_ = 0;
  intro_ = 0;
}

// ---------------------------------------------------------------------------
// Exit

// Mech3.exe 1.2 @ 0x00482060
bool ExitState::enter() {
  a().stop_game();
  return true;
}

// ---------------------------------------------------------------------------
// Pre Lab

// Mech3.exe 1.2 @ 0x00480b30: "Default Player" in the lab game (type 0), world 6.
bool PreLabState::enter() {
  Engine& en = e();
  const Handle pilot = en.pilot_get("Default Player");
  en.set_pilot(pilot);
  en.pilot_select_game(pilot, 0);
  en.vcall(en.field32(en.pilot(), 0x6c), 0x50, {6});
  return true;
}

// Mech3.exe 1.2 @ 0x00480b60
bool PreLabState::update() {
  Engine& en = e();
  a().replace(a().state(6), 0);
  const Handle r = en.vcall(en.field32(en.pilot(), 0x6c), 0x34, {0});
  en.lab_start(r, 3);
  return false;
}

// ---------------------------------------------------------------------------
// Core

// Mech3.exe 1.2 @ 0x00481280
void CoreState::on_activate(uint32_t) { e().video_restore(); }

// Mech3.exe 1.2 @ 0x00480c70. FIX (A): SystemParametersInfo(SPI_SETSCREENSAVERRUNNING)
// (Windows 9x: no Alt-Tab or Ctrl-Alt-Del) goes through the platform hook.
bool CoreState::enter() {
  Engine& en = e();
  if (a().multiplayer()) a().lock_system_keys() = 1;
  if (a().lock_system_keys()) en.lock_system_keys(1);
  en.options_apply_all();
  const Handle pilot = en.pilot();
  if (pilot == 0 || en.field32(pilot, 0x14) == 0) a().replace(a().state(2), 0);
  paused_ = 0;
  en.set_screen_mode(en.in_game_vmode());
  pick_renderer();
  message_box_ = en.message_box_new("dlgMessage.zrd", "MESSAGEBOX");
  en.vcall(message_box_, 0x04, {0});
  start_mission();
  if (Handle ff = en.force_feedback()) en.force_feedback_spring(ff, 1);
  if (en.pixel_double() == 0) {
    en.fade(7, -1, 0, zgame::bits(kFadeSeconds), 0, 0, 0);
  } else {
    resume(7);
  }
  if (en.field8(en.pilot(), 0x59) == 0) {
    if (Handle s = en.sound_find("mech_powerup")) en.sound_play(s, 1.0f);
  }
  en.mouse_set_accel(en.mouse_accel_x(), en.mouse_accel_y());
  active_ = 1;
  return true;
}

// Mech3.exe 1.2 @ 0x00480bf0
void CoreState::pick_renderer() {
  renderer_ = kNone;
  if (e().video_mode() != 0) renderer_ = kHardware;
  else renderer_ = e().pixel_double() ? kDoubled : kSoftware;
}

// Mech3.exe 1.2 @ 0x00480df0: the frame clock is ticked twice around a 30 ms
// sleep, the view and its window set up, then the mission's CD track: -cdN
// if valid, else on a disc of 2..5 tracks one picked by mission number, on a
// larger one a random track from 1 up, which can be the data track (track 1,
// silent; docs/discoveries.md).
void CoreState::start_mission() {
  Engine& en = e();
  en.timer_update();
  en.sleep(30);
  en.timer_update();
  en.debug_mark("Dummy Frame");
  en.video_show_cursor(1);
  en.video_set_clear(0);
  en.input_poll(0);
  en.input_reset();
  en.input_center_mouse();
  if (en.hw_card_flag() != 0) {
    en.camera_set_5ea53c(0);
    en.camera_set_hse_test(0);  // the objects' hidden-surface test off with a card
  }
  const float dt = en.frame_dt();
  en.lists_update();
  render_rect_ = en.render_rect();
  display_rect_ = en.display_rect();
  window_rect_ = en.window_rect();
  Handle back, depth, target;
  if (en.hw_card_flag() != 0) {
    en.video_clear_a(0);
    en.video_clear_b(0);
    back = en.video_back_buffer();
    depth = en.display_depth();
    target = en.video_target();
  } else {
    back = en.video_buffer_cd0();
    depth = en.display_depth();
    target = en.video_buffer_ca0();
  }
  en.render_set_target(target, render_rect_, depth, back);
  const Handle veh = en.vcall(en.field32(en.player_view(), 0x64), 0x50, {});
  bool cockpit = false;
  if (veh) {
    const Handle cam = en.field32(en.player_view(), 0x58);
    cockpit = cam && en.field32(cam, 0x4) == 1;
  }
  if (cockpit) en.node_set_active(en.vehicle_node(veh), 0);
  en.view_reset(en.player_view());
  en.vcall(en.player_view(), 0x08, {zgame::bits(dt)});
  en.vcall(en.player_view(), 0x04, {});
  if (cockpit) {
    en.node_set_active(en.vehicle_node(veh), 1);
    const Handle v = en.player_view();
    const Handle part = en.cockpit_node(veh);
    en.view_draw_node(en.field32(v, 0x4), part, en.field32(v, 0x14));
  }
  en.ai_reset(0x604038);
  if (en.video_mode() != 0) {
    en.video_begin_3d();
    en.video_end_3d();
  } else {
    en.video_lock();
    en.video_unlock();
  }
  if (en.pixel_double() != 0) en.video_double(0, 0);
  en.nav_prebuild();  // ours (W58)
  en.input_reset();
  elapsed_ = 0;
  en.debug_mark(" L e t s  P l a y ! ");
  a().mission_started_ = 1;
  for (uint32_t i = 0; i < 0x19; ++i) {  // the 25 HUD text lines at 0x005fc848
    en.hud_text_clear(0x5fc848 + i * 0x4b0);
  }
  const int32_t tracks = en.cd_track_count();
  const int32_t forced = a().cd_track();
  int32_t track;
  if (forced > 0 && forced < tracks) {
    track = forced;
  } else if (tracks <= 1) {
    track = 0;
  } else if (tracks <= 5) {
    const int32_t mission = static_cast<int32_t>(en.vcall(en.field32(en.pilot(), 0x6c), 0x4c, {}));
    track = mission % (tracks - 1) + 2;
  } else {
    track = static_cast<int32_t>(en.rand()) % tracks + 1;
  }
  if (track > 0) en.music_play(track, 6);
  if (en.video_mode() != 0) {  // the Direct3D fog colour
    en.poke32(0x72f3f4, zgame::bits(25.5f));
    en.poke32(0x72f3f8, zgame::bits(51.0f));
    en.poke32(0x72f3fc, zgame::bits(76.5f));
    if (zgame::from_bits(en.peek32(0x72f400)) != 25.5f || zgame::from_bits(en.peek32(0x72f404)) != 51.0f ||
        zgame::from_bits(en.peek32(0x72f408)) != 76.5f) {
      en.poke32(0x72f400, zgame::bits(25.5f));
      en.poke32(0x72f404, zgame::bits(51.0f));
      en.poke32(0x72f408, zgame::bits(76.5f));
      en.video_apply_fog();
    }
  }
}

// Mech3.exe 1.2 @ 0x00481e70, one mission frame. True when command 1 (Escape)
// asks for the pause menu; in multiplayer the message box asks instead.
bool CoreState::frame() {
  Engine& en = e();
  elapsed_ = en.frame_dt() + elapsed_;
  if (!paused_) {
    en.input_poll(1);
    if (!a().fullscreen() && (a().input_mode_ & 0xc) == 8) {
      int32_t x = 0, y = 0;
      en.cursor_pos(&x, &y);
      en.mouse_set_pos(x, y);
    }
  }
  en.lists_update();
  en.sound_update(0, 1);
  const Handle view = en.player_view();
  const Handle target = en.field32(view, 0x64);
  const Handle veh = target ? en.vcall(target, 0x50, {}) : 0;
  bool flag = false;
  if (veh) {
    const Handle cam = en.field32(en.player_view(), 0x58);
    flag = cam && en.field32(cam, 0x4) == 1;
  }
  bool stop = false;
  switch (renderer_) {
    case kSoftware: stop = draw_software(veh, flag); break;
    case kDoubled: stop = draw_doubled(veh, flag); break;
    case kHardware: stop = draw_hardware(veh, flag); break;
    default: break;  // the original calls through a null object here
  }
  if (stop) {
    if (en.vcall(en.field32(en.pilot(), 0x6c), 0x2c, {}) != 3) return true;
    en.message_box_quit_prompt(message_box_);
  }
  // The hardware path's, past its stop, is in its 2D with the HUD (W46).
  if (renderer_ != kHardware || stop) en.vcall(message_box_, 0x00, {zgame::bits(en.frame_dt())});
  en.video_flip(0, 0, 0, 0);
  return false;
}

// Mech3.exe 1.2 @ 0x00481f70: Escape pushes the pause menu; a new screen mode
// picks the drawing path again.
bool CoreState::update() {
  Engine& en = e();
  if (frame()) a().push(a().state(3), 0);
  if (en.vmode() != globals().last_vmode) {
    pick_renderer();
    globals().last_vmode = en.vmode();
  }
  return false;
}

namespace {
// The player's mech parts drawn into the cockpit view (0x004f73f0).
void draw_part(Engine& en, Handle part) {
  const Handle v = en.player_view();
  en.view_draw_node(en.field32(v, 0x4), part, en.field32(v, 0x14));
}
Handle mech_part(Engine& en, Handle veh, int off) { return en.arm_node(veh, off); }
Handle mech_torso(Engine& en, Handle veh) { return en.cockpit_node(veh); }
// The HUD update and the overlay (0x0040fac0, 0x0040e4b0, then the debug view or
// 0x0041ab90 on the pilot's mech).
void hud(Engine& en) {
  en.hud_update();
  en.hud_draw();
  if (Handle dv = globals().debug_view) en.vcall(dv, 0x04, {});
  else en.mech_draw_cockpit(en.field32(en.pilot(), 0x14));
}
// The static on the view target's HUD (0x0043cd20), its HUD asked twice.
void hud_static(Engine& en, Handle veh) {
  if (!veh) return;
  const Handle hud = veh == en.field32(en.player_view(), 0x64) ? en.pilot_hud(en.field32(veh, 0x684)) : 0;
  if (!hud) return;
  const Handle hud2 = veh == en.field32(en.player_view(), 0x64) ? en.pilot_hud(en.field32(veh, 0x684)) : 0;
  en.hud_static(hud2, en.frame_dt());
}
// The reticle's zoom window's 3D pass (+0x144c vt+0x28).
void zoom_pass(Engine& en, Handle veh) {
  if (!veh) return;
  en.hud_zoom_pass(en.pilot_hud(en.field32(veh, 0x684)), en.frame_dt());
}
void world_tick(Engine& en) {
  en.sensors_update();
  en.missions_update(0x6098d0, en.frame_dt());
  en.pilot_update(en.pilot(), en.frame_dt());
  en.camera_update();
}
}  // namespace

// Mech3.exe 1.2 @ 0x00481290: the software path, one view.
bool CoreState::draw_software(Handle veh, bool flag) {
  Engine& en = e();
  const Handle render = en.render_rect();
  en.display_rect();
  const Handle window = en.window_rect();
  en.video_set_frame_mode(0);
  en.video_clear(window);
  const Handle a = en.video_back_buffer();
  const Handle b = en.display_depth();
  en.render_set_target(en.video_target(), render, b, a);
  if (flag) en.node_set_active(en.vehicle_node(veh), 0);
  const Handle scene = en.scene_begin();
  en.vcall(en.player_view(), 0x04, {});
  en.scene_draw(scene);
  if (veh) zoom_pass(en, veh);
  if (en.key_state(1) & 3) {
    if (flag && veh) en.node_set_active(en.vehicle_node(veh), 1);
    return true;
  }
  world_tick(en);
  en.weather_rect(0, render);
  en.video_lock();
  en.weather_tick(en.frame_dt());
  en.render_flush(window);
  en.render_finish();
  en.video_unlock();
  if (flag) {
    en.node_set_active(en.vehicle_node(veh), 1);
    draw_part(en, mech_part(en, veh, 0x394));
    draw_part(en, mech_part(en, veh, 0x398));
    draw_part(en, mech_torso(en, veh));
    en.scene_draw(scene);
  }
  en.view_camera_refresh(en.player_view());
  en.scene_end(scene);
  en.scene_set_scale(0, 1.0f);
  en.scene_present();
  en.scene_reset();
  en.vcall(en.field32(en.player_view(), 0x64), 0xe4, {zgame::bits(en.frame_dt())});
  en.video_lock();
  hud(en);
  hud_static(en, veh);
  en.video_unlock();
  return false;
}

// 0x004814e0's two sums (app_shadows.h).
float shadow_fade(float d2) {
  if (d2 >= 10000.0f) return 0.0f;  // x87 "below": NaN goes on
  if (!(d2 > 2500.0f)) return 1.0f;
  return (10000.0f - d2) * (1.0f / (10000.0f - 2500.0f));
}

uint8_t shadow_grey(float ambient, float diffuse, float dir_y, float fade) {
  float t;  // the ambient share
  if (!(ambient < 0.0f || ambient > 0.0f)) {
    t = 0.0f;
  } else {
    const float lit = ambient - dir_y * diffuse;  // direction y: below 0 from above
    t = !(lit < 0.0f || lit > 0.0f) ? ambient : ambient / lit;
  }
  int32_t grey = crt::msvc_ftol(255.5f - (1.0f - t) * fade * 255.0f);
  if (grey > 255) grey = 255;
  else if (grey < 0) grey = 0;
  return static_cast<uint8_t>(grey);
}

// Mech3.exe 1.2 @ 0x004814e0. For each object in the list at 0x0073655c: the
// player's mech, or one within 100 m of it, keeps a caster (mech +0xcc; 64x64
// for the player's, 32x32 others), redrawn every frame along the sun
// ([0x006098d4] +4, its light +0x84) onto the mech's ground. Its grey is the
// share of the light that is ambient where the clouds put it, faded to white
// from 50 to 100 m. Further away the caster is freed.
void update_shadows(Engine& en) {
  if (!en.model_global_light()) return;  // 0x00526a80, shadows on
  const Handle sun = en.peek32(0x6098d4);
  if (!sun || !en.field32(sun, 4)) return;
  const Handle light = en.field32(en.field32(sun, 4), 0x38);
  auto f = [&en](Handle a) { return zgame::from_bits(en.peek32(a)); };
  for (Handle it = en.peek32(0x73655c); it != en.peek32(0x736560); it += 4) {
    const Handle mech = en.vcall(en.peek32(it), 0x2c, {});
    const Handle pos = en.entity_position(mech);
    const Handle player = en.field32(en.pilot(), 0x14);
    float fade;
    if (player == en.vcall(mech, 0x50, {})) {
      fade = 1.0f;
    } else {
      // 0x0051dfa0: the squared distance, summed (x + y) + z.
      const Handle p = en.entity_position(en.field32(en.pilot(), 0x14));
      const float dx = f(p) - f(pos), dy = f(p + 4) - f(pos + 4), dz = f(p + 8) - f(pos + 8);
      const float xx = dx * dx, yy = dy * dy, zz = dz * dz;
      const float xy = xx + yy;
      fade = shadow_fade(xy + zz);
    }
    Handle caster = en.field32(mech, 0xcc);
    if (!(fade > 0.0f)) {
      if (caster) {
        en.shadow_release_texture(caster);
        en.shadow_delete(caster);
        en.set_field32(mech, 0xcc, 0);
      }
      continue;
    }
    if (!caster) {
      const Handle pl = en.field32(en.pilot(), 0x14);
      const int32_t size = en.vcall(mech, 0x50, {}) != pl ? 32 : 64;
      caster = en.shadow_new();
      en.set_field32(mech, 0xcc, caster);
      en.shadow_set_size(caster, size, size);
      en.shadow_set_node(caster, en.field32(mech, 4));
      en.shadow_enable(caster, 1);
    }
    const float clouds = en.clouds_at(f(pos), f(pos + 8));
    const uint8_t g = shadow_grey(f(light + 0xa0) * clouds, f(light + 0x9c) * clouds, f(light + 0x88), fade);
    en.shadow_set_color(caster, zvid::pack_bytes(g, g, g));
    // The ground: the mech's +0x50 object's +0x41c +0x13c, else its own height.
    const Handle owner = en.vcall(mech, 0x50, {});
    const float ground = owner ? f(en.field32(en.vcall(mech, 0x50, {}), 0x41c) + 0x13c) : f(pos + 4);
    en.shadow_set_position(caster, en.entity_position(mech));
    en.shadow_draw(caster, light + 0x84, ground);
    en.shadow_upload(caster);
  }
}

// Mech3.exe 1.2 @ 0x00481790: the Direct3D path.
bool CoreState::draw_hardware(Handle veh, bool flag) {
  Engine& en = e();
  const Handle render = en.render_rect();
  en.display_rect();
  const Handle window = en.window_rect();
  en.video_clear_b(window);
  en.update_shadows();
  const Handle a = en.video_back_buffer();
  const Handle b = en.display_depth();
  en.render_set_target(en.video_target(), render, b, a);
  en.scene_begin_3d();
  if (flag) en.node_set_active(en.vehicle_node(veh), 0);
  const Handle scene = en.scene_begin();
  en.vcall(en.player_view(), 0x04, {});
  if (veh) zoom_pass(en, veh);
  if (en.key_state(1) & 3) {
    if (flag && veh) en.node_set_active(en.vehicle_node(veh), 1);
    return true;
  }
  world_tick(en);
  en.weather_rect(0, window);
  en.video_begin_3d();
  en.weather_tick(en.frame_dt());
  en.video_end_3d();
  en.video_begin_scene();
  en.video_draw_3d();
  en.video_end_scene();
  if (flag) {
    en.node_set_active(en.vehicle_node(veh), 1);
    const int32_t light = en.model_global_light();
    en.model_set_global_light(0);
    draw_part(en, mech_torso(en, veh));
    en.model_set_global_light(light);
    draw_part(en, mech_part(en, veh, 0x394));
    draw_part(en, mech_part(en, veh, 0x398));
  }
  en.view_camera_refresh(en.player_view());
  if (veh) {
    en.node_set_shadow(mech_torso(en, veh), 1);
    en.camera_draw_scene(en.field32(en.field32(en.player_view(), 0x4), 0x38), scene);
    en.node_set_shadow(mech_torso(en, veh), 0);
  }
  en.vcall(en.field32(en.player_view(), 0x64), 0xe4, {zgame::bits(en.frame_dt())});
  en.video_begin_3d();
  hud(en);
  hud_static(en, veh);
  // FIX (A), playtest W44/W46: the quit box (the exe's update after the frame,
  // 0x00481e70) draws here, with the HUD's 2D, else nothing showed it.
  en.vcall(message_box_, 0x00, {zgame::bits(en.frame_dt())});
  en.video_end_3d();
  return false;
}

namespace {
// The view's window (zClass window data at [[view]+8]+8..+0x38) set to a
// rectangle, inlined twice in 0x00481a10: the size and origin go to the
// window node, the scale factors are kept when the rectangle changes.
void set_view_window(Engine& en, Handle rect) {
  const Handle data = en.field32(en.player_view(), 0x8);
  for (int i = 0; i < 4; ++i) {
    const uint32_t v = en.field32(rect, 4 * i);
    en.set_field32(data, 0x8 + 4 * i, v);
    en.set_field32(data, 0x18 + 4 * i, v);
  }
  const int32_t x0 = static_cast<int32_t>(en.field32(data, 0x8));
  const int32_t y0 = static_cast<int32_t>(en.field32(data, 0xc));
  const int32_t x1 = static_cast<int32_t>(en.field32(data, 0x10));
  const int32_t y1 = static_cast<int32_t>(en.field32(data, 0x14));
  const int32_t u0 = static_cast<int32_t>(en.field32(data, 0x18));
  const int32_t v0 = static_cast<int32_t>(en.field32(data, 0x1c));
  const int32_t u1 = static_cast<int32_t>(en.field32(data, 0x20));
  const int32_t v1 = static_cast<int32_t>(en.field32(data, 0x24));
  const bool changed = !(u1 == x1 && v1 == y1 && u0 == x0 && v0 == y0);
  en.set_field8(data, 0x28, changed ? 1 : 0);
  if (changed) {
    const float sx = static_cast<float>(x1 - x0) / static_cast<float>(u1 - u0);
    const float sy = static_cast<float>(y1 - y0) / static_cast<float>(v1 - v0);
    en.set_field32(data, 0x2c, zgame::bits(sx));
    en.set_field32(data, 0x30, zgame::bits(sy));
    en.set_field32(data, 0x34, zgame::bits(1.0f / sx));
    en.set_field32(data, 0x38, zgame::bits(1.0f / sy));
  }
  const Handle node = en.field32(data, 0x4);
  en.window_set_size(node, static_cast<int32_t>(en.field32(data, 0x10) - en.field32(data, 0x8)),
                     static_cast<int32_t>(en.field32(data, 0x14) - en.field32(data, 0xc)));
  en.window_set_origin(node, static_cast<int32_t>(en.field32(data, 0x8)),
                       static_cast<int32_t>(en.field32(data, 0xc)));
}
}  // namespace

// Mech3.exe 1.2 @ 0x00481a10: the doubled software path (VMode 3): the scene is
// drawn at the render size into the window, doubled, then the view's window is
// set back to the render size.
bool CoreState::draw_doubled(Handle veh, bool flag) {
  Engine& en = e();
  const Handle render = en.render_rect();
  const Handle display = en.display_rect();
  const Handle window = en.window_rect();
  en.video_set_frame_mode(0);
  en.video_clear(window);
  const Handle a = en.video_buffer_cd0();
  const Handle b = en.video_front_buffer();
  const Handle r = en.field32(en.player_view(), 0x8) + 0x8;
  en.render_set_target(en.video_buffer_ca0(), r, b, a);
  if (flag) en.node_set_active(en.vehicle_node(veh), 0);
  const Handle scene = en.scene_begin();
  en.vcall(en.player_view(), 0x04, {});
  en.scene_draw(scene);
  if (en.key_state(1) & 3) {
    if (flag && veh) en.node_set_active(en.vehicle_node(veh), 1);
    return true;
  }
  if (flag) {
    en.node_set_active(en.vehicle_node(veh), 1);
    draw_part(en, mech_part(en, veh, 0x394));
    en.scene_draw(scene);
    draw_part(en, mech_part(en, veh, 0x398));
    en.scene_draw(scene);
    draw_part(en, mech_torso(en, veh));
    en.scene_draw(scene);
  }
  world_tick(en);
  en.weather_rect(0, render);
  en.video_begin_3d();
  en.weather_tick(en.frame_dt());
  en.video_end_3d();
  en.video_double(render, display);
  en.video_lock();
  en.render_flush(window);
  en.view_camera_refresh(en.player_view());
  en.scene_set_scale_b(2.0f);
  en.scene_end(scene);
  set_view_window(en, window);
  en.window_apply(en.field32(en.field32(en.player_view(), 0x4), 0x38));
  en.scene_set_scale(0, 1.0f);
  en.scene_present();
  set_view_window(en, render);
  en.scene_reset();
  en.video_unlock();
  en.video_lock();
  if (veh) zoom_pass(en, veh);
  hud(en);
  en.video_unlock();
  en.vcall(en.field32(en.player_view(), 0x64), 0xe4, {zgame::bits(en.frame_dt())});
  return false;
}

// Mech3.exe 1.2 @ 0x00481fc0
void CoreState::leave() {
  Engine& en = e();
  en.mouse_set_accel(1.3f, 1.3f);
  en.set_screen_mode(en.menu_vmode());  // 5 (ours: the wide canvas with wide_menus)
  en.cd_stop();
  en.reader_close_archives(0);
  en.anim_stop(1);
  const Handle s = en.sound_listener();
  en.sound_stop_all(s);
  en.sound_release_all(s);
  en.sound_reset();
  renderer_ = kNone;
  if (message_box_) en.vcall(message_box_, 0x08, {1});
  message_box_ = 0;
  if (a().lock_system_keys()) {
    en.lock_system_keys(0);
    if (a().multiplayer()) a().lock_system_keys() = 0;
  }
  active_ = 0;
}

// Mech3.exe 1.2 @ 0x00481250
void CoreState::suspend(uint32_t) {
  Engine& en = e();
  en.mouse_set_accel(1.3f, 1.3f);
  if (Handle dv = globals().debug_view) {
    en.vcall(dv, 0x00, {1});
    globals().debug_view = 0;
  }
}

// Mech3.exe 1.2 @ 0x00481170: 4 is back from the Mech Lab (the new loadout
// goes on the mech), 7 is the start of the mission.
void CoreState::resume(uint32_t arg) {
  Engine& en = e();
  en.mouse_set_accel(en.mouse_accel_x(), en.mouse_accel_y());
  en.set_screen_mode(en.in_game_vmode());
  pick_renderer();
  if (arg == 4) {
    const Handle cfg = en.vcall(en.field32(en.pilot(), 0x6c), 0x34, {0});
    const uint32_t id = en.field32(cfg, 0x4);
    const Handle mech = en.field32(en.pilot(), 0x14);
    if (id != en.field32(en.field32(mech, 0x684), 0xc)) en.pilot_set_mech(en.pilot(), id);
    const Handle cfg2 = en.vcall(en.field32(en.pilot(), 0x6c), 0x34, {0});
    en.mech_apply_config(en.field32(en.pilot(), 0x14), en.field32(cfg2, 0x8));
  } else if (arg == 7) {
    en.input_reset();
    if (en.field8(en.pilot(), 0x59) == 0) {
      en.mech_set_mode_a(en.field32(en.pilot(), 0x14), 0);
      en.mech_set_mode_b(en.field32(en.pilot(), 0x14), 0);
    }
    if (en.peek8(0x60b56c) == 0) en.entities_sort();
    en.shadows_reset();
    en.ai_on();
  }
}

// ---------------------------------------------------------------------------
// Key list

// Mech3.exe 1.2 @ 0x00482570: builds once; the text layout is the 2D front end's.
bool KeyListState::enter() {
  if (list_ == 0) list_ = e().key_list_build();
  return true;
}

// Mech3.exe 1.2 @ 0x00482520
bool KeyListState::update() {
  Engine& en = e();
  en.input_poll(1);
  if (en.key_state(1) != 0) {
    en.input_clear();
    a().pop(0);
  }
  return false;
}

// Mech3.exe 1.2 @ 0x00482550
void KeyListState::leave() {
  if (list_) e().vcall(list_, 0x00, {3});
  list_ = 0;
}

// ---------------------------------------------------------------------------
// Shell (the menus' base)

// Mech3.exe 1.2 @ 0x0048ebf0
bool ShellState::enter() {
  Engine& en = e();
  cursor_ = en.video_show_cursor(0);
  en.twod_set_cursor(0);
  en.input_reset();
  refresh_target();
  en.input_capture();
  if (screen_) {
    en.vcall(screen_, 0x04, {1});
    en.screen_refresh(screen_);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x0048ed30: Escape goes to the state's +0x28.
bool ShellState::update() {
  Engine& en = e();
  en.input_poll(0);
  if (!a().fullscreen() && (a().input_mode_ & 0xc) == 8) {
    int32_t x = 0, y = 0;
    en.cursor_pos(&x, &y);
    en.mouse_set_pos(x, y);
  }
  en.sound_update(0, 0);
  if (en.key_state(1) == 1) escape();
  en.video_lock();
  if (screen_) en.vcall(screen_, 0x00, {zgame::bits(en.frame_dt())});
  en.video_unlock();
  en.video_flip(0, 0, 1, 1);
  return false;
}

// Mech3.exe 1.2 @ 0x0048ec40
void ShellState::leave() {
  Engine& en = e();
  en.video_lock();
  if (screen_) {
    en.vcall(screen_, 0x04, {0});
    en.screen_hide(screen_);
  }
  en.video_unlock();
  en.video_show_cursor(cursor_);
  en.twod_set_cursor(cursor_);
  en.input_clear();
  en.input_release();
}

// Mech3.exe 1.2 @ 0x0048ec80
void ShellState::suspend(uint32_t arg) {
  Engine& en = e();
  if (screen_ == 0 || arg == 0) return;
  en.video_lock();
  en.vcall(screen_, 0x04, {0});
  en.screen_hide(screen_);
  en.video_unlock();
  en.video_flip(0, 0, 1, 1);
}

// Mech3.exe 1.2 @ 0x0048ecc0: redraws the screen once with its +0x18 flag set.
void ShellState::resume(uint32_t arg) {
  Engine& en = e();
  refresh_target();
  if (screen_ == 0 || arg != 1) return;
  const uint8_t old = static_cast<uint8_t>(en.vcall(screen_, 0x18, {1}));
  en.video_lock();
  en.vcall(screen_, 0x04, {1});
  en.screen_refresh(screen_);
  en.vcall(screen_, 0x00, {0});
  en.video_unlock();
  en.vcall(screen_, 0x18, {old});
  en.video_flip(0, 0, 1, 1);
}

// Mech3.exe 1.2 @ 0x0048ee00: the screen deleted (its +0x08(1)).
void ShellState::clear_screen() {
  if (screen_) e().vcall(screen_, 0x08, {1});
  screen_ = 0;
}

// Mech3.exe 1.2 @ 0x0048edc0
void ShellState::refresh_target() {
  Engine& en = e();
  en.video_show_cursor(0);
  en.twod_set_cursor(0);
  target_menu(en);
}

// ---------------------------------------------------------------------------
// Main menu

namespace {
// The main menu screen's buttons (fields of the 0x00486260 screen, each pressed
// through its vtable +0x30; mech3/mainmenu).
enum MainButton : uint32_t {
  kButtonInstantAction = 0x111ec, kButtonCampaign = 0x11494, kButtonMulti = 0x1173c, kButtonTraining = 0x119e4,
  kButtonOptions = 0x11c8c, kButtonCredits = 0x11f34, kButtonQuit = 0x121dc,
};
// The menu objects the main menu makes (0x00609858..0x00609878): their
// globals and constructors, in the order made (the Action, Campaign, Training,
// Multiplayer states among them, gametypes.md).
const struct {
  uint32_t global, ctor;
} kMenus[] = {{0x60985c, 0x48a6c0}, {0x609858, 0x488910}, {0x609864, 0x48aef0},
              {0x609874, 0x48b120}, {0x609868, 0x438be0}, {0x60986c, 0x43a640},
              {0x609870, 0x42d9d0}, {0x609860, 0x49c900}, {0x609878, 0x4295e0}};
void delete_object(Engine& en, uint32_t global) {
  if (Handle o = en.peek32(global)) en.vcall(o, 0x00, {1});
}
}  // namespace

// Mech3.exe 1.2 @ 0x00486990: the screen, the nine menu objects, then the launch
// flags press a button at once.
bool MainState::enter() {
  Engine& en = e();
  ShellState::enter();
  screen_ = en.construct(0x486260, {addr()});  // new(0x12c1c)
  en.screen_refresh(screen_);
  for (const auto& m : kMenus) en.poke32(m.global, en.construct(m.ctor, {}));
  en.twod_reset(en.peek32(0x609854));
  reset_idle();
  Mech3App& ap = a();
  if (ap.multiplayer() || en.network_lobby_active()) en.vcall(screen_ + kButtonMulti, 0x30, {});
  if (ap.reopen_instant_action()) {
    en.vcall(screen_ + kButtonInstantAction, 0x30, {});
    ap.set_reopen_instant_action(0, ap.instant_action_arg());
  } else if (ap.reopen_training()) {
    ap.set_reopen_training(0);
    en.vcall(screen_ + kButtonTraining, 0x30, {});
  } else if (ap.reopen_credits()) {
    ap.reopen_credits() = 0;
    en.vcall(screen_ + kButtonCredits, 0x30, {});
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00486d40: 60 idle seconds push the attract movie; the
// hotkeys press the screen's buttons.
bool MainState::update() {
  Engine& en = e();
  const bool r = ShellState::update();
  idle_ = idle_ - en.frame_dt();
  const Handle s = screen_;
  if (!(idle_ > 0.0f)) {
    a().push(a().state(0), 0);
    reset_idle();
  }
  static const struct {
    int32_t key;
    uint32_t button;
  } kKeys[] = {{0x18, kButtonOptions}, {0x2e, kButtonCampaign}, {0x32, kButtonMulti}, {0x17, kButtonInstantAction},
               {0x14, kButtonTraining}, {0x10, kButtonQuit}, {0x13, kButtonCredits}};
  for (const auto& k : kKeys) {
    if (en.key_state(k.key) & 1) {
      en.vcall(s + k.button, 0x30, {});
      return r;
    }
  }
  return r;
}

// Mech3.exe 1.2 @ 0x00486c50
void MainState::leave() {
  Engine& en = e();
  ShellState::leave();
  en.shell_clear(addr());
  for (const auto& m : kMenus) {
    delete_object(en, m.global);
    en.poke32(m.global, 0);
  }
  if (save_pilots_) en.pilot_delete("mechlab");
}

// Mech3.exe 1.2 @ 0x0048e400
void MainState::suspend(uint32_t arg) {
  reset_idle();
  ShellState::suspend(arg);
}

// Mech3.exe 1.2 @ 0x00486e80: back from a game (4 or 5) the pilots are saved.
void MainState::resume(uint32_t arg) {
  Engine& en = e();
  if (arg >= 4 && arg <= 5) en.pilot_delete("mechlab");
  if (screen_) en.screen_refresh(screen_);
  reset_idle();
  ShellState::resume(arg);
}

// Mech3.exe 1.2 @ 0x00486ec0
void MainState::escape() { a().confirm_quit(); }

// ---------------------------------------------------------------------------
// Pause

// Mech3.exe 1.2 @ 0x00493040: a short busy wait (a stack object made by
// 0x00599f70(4, 1)) before the pause screen.
bool PauseState::enter() {
  Engine& en = e();
  ShellState::enter();
  cursor_ = en.video_show_cursor(0);
  en.twod_set_cursor(0);
  en.wait_new(4, 1);
  en.wait_start();
  while (en.wait_poll() & 0xff) {
  }
  en.wait_end();
  screen_ = en.construct(0x492750, {addr()});  // new(0x11b20)
  return true;
}

// Mech3.exe 1.2 @ 0x004931e0: also the three option menus the main menu made.
void PauseState::leave() {
  Engine& en = e();
  ShellState::leave();
  if (screen_) en.vcall(screen_, 0x08, {1});
  delete_object(en, 0x609868);
  delete_object(en, 0x60986c);
  delete_object(en, 0x609870);
  screen_ = 0;
  en.poke32(0x609868, 0);
  en.poke32(0x60986c, 0);
  en.poke32(0x609870, 0);
}

// Mech3.exe 1.2 @ 0x00493250: Escape returns to the mission (resume arg 6).
void PauseState::escape() { a().pop(6); }

// FIX (B), playtest W36: back from the options (which switch to the menus'
// mode 5, 0x00438c90), the pause shows over the mission again in its mode,
// not the menus' 640x480 until the mission resumes (the exe's modes were all
// 4:3, the jump unseen).
void PauseState::resume(uint32_t arg) {
  if (zfix::options().any_resolution) e().set_screen_mode(e().in_game_vmode());
  ShellState::resume(arg);
}

// ---------------------------------------------------------------------------
// Load (the briefing)

// Mech3.exe 1.2 @ 0x004091e0: in a campaign, a briefing movie object when the
// mission has one (0x0040a820).
bool LoadState::enter() {
  Engine& en = e();
  en.load_state_enter();
  ShellState::enter();
  build_ = 1;
  const Handle game = en.pilot_game(en.pilot());
  if (en.vcall(game, 0x2c, {}) == 1) movie_ = en.briefing_has_movie() ? en.movie_new() : 0;
  return true;
}

// Mech3.exe 1.2 @ 0x00409440 (briefing.cpp, built without optimisation): the
// movie first (started on the first pass, polled after), then the briefing
// screen is built once, then the menu runs.
bool LoadState::update() {
  Engine& en = e();
  if (movie_) {
    if (build_) {
      en.briefing_play_movie(addr());
      build_ = 0;
    } else {
      en.input_poll(0);
      build_ = en.movie_playing(movie_) ? 0 : 1;
      if (build_) {
        en.movie_stop(movie_, 0);
        if (movie_) en.movie_delete(movie_);
        movie_ = 0;
      }
    }
    return false;
  }
  if (!build_) return ShellState::update();
  const Handle game = en.pilot_game(en.pilot());
  const int32_t type = static_cast<int32_t>(en.vcall(game, 0x2c, {}));
  const uint32_t campaign = en.pilot_campaign_number(en.pilot());
  const char* dir = en.mission_dir(campaign);
  const int32_t mission = static_cast<int32_t>(en.vcall(game, 0x4c, {}));
  en.sound_load(en.app_window(), "sounds.zrd", 0x4e6b00, 0);
  char path[256];
  en.format("zbd;zbd\\%s", {dir}, path, sizeof path);
  en.geometry_init(path);
  en.missions_reset(0x6098d0);
  char name[256];
  static const char* const kNames[] = {"brief_%s%d", "brief_ia%s%d", "brief_mp%s%d", "brief_%s%d"};
  if (type >= 1 && type <= 4) en.format(kNames[type - 1], {dir, mission}, name, sizeof name);
  else en.format("default", {}, name, sizeof name);
  en.sounds_nop(en.app_sounds(), 0);  // an empty function, called with a stray argument
  if (en.app_flag_1c3() == 0) en.voice_start(name);
  en.sounds_load(en.app_sounds(), "briefing sounds", 1);
  screen_ = en.construct(0x405230, {name});  // new(0x12e6c)
  en.poke32(0x5f4c80, screen_);
  en.sounds_load(en.app_sounds(), "briefing menu", 1);
  en.briefing_init(screen_);
  en.screen_refresh(screen_);
  en.briefing_script_start(screen_);
  en.missions_start(0x6098d0);
  en.briefing_start(screen_);
  build_ = 0;
  return false;
}

// Mech3.exe 1.2 @ 0x004093e0
void LoadState::leave() {
  Engine& en = e();
  ShellState::leave();
  if (Handle s = en.peek32(0x5f4c80)) en.vcall(s, 0x08, {1});
  en.poke32(0x5f4c80, 0);
  screen_ = 0;
}

// Mech3.exe 1.2 @ 0x004092c0 / 0x004092e0: back from the Mech Lab (4) the
// briefing's loadout is refreshed.
void LoadState::suspend(uint32_t arg) { ShellState::suspend(arg); }
void LoadState::resume(uint32_t arg) {
  Engine& en = e();
  if (arg == 4) {
    en.briefing_refresh(en.peek32(0x5f4c80));
    en.briefing_show(en.peek32(0x5f4c80), 1);
  }
  ShellState::resume(1);
}

// ---------------------------------------------------------------------------
// Win (the debriefing)

// Mech3.exe 1.2 @ 0x0047ac20. A campaign mission counts only when nothing marked
// it failed ([0x00609926]) and no cheat was used (protection.md): then the
// results are kept and the pilot's campaign record is updated; operation 4
// mission 6 won ends the campaign. Otherwise 0x0047ae10(1, 1) drops them.
bool WinState::enter() {
  Engine& en = e();
  const int32_t campaign = static_cast<int32_t>(en.vcall(en.field32(en.pilot(), 0x6c), 0x48, {}));
  const int32_t mission = static_cast<int32_t>(en.vcall(en.field32(en.pilot(), 0x6c), 0x4c, {}));
  // [0x00609994] and [0x00609926] are the missions' +0xc4 (cheats) and +0x56
  // (aborting), read from the ported Missions (peek8 is not them).
  const bool clean = !en.mission_cheated();
  const bool lost = en.mission_failed();
  campaign_won_ = 0;
  cursor_ = en.video_show_cursor(0) != 0 ? 1 : 0;
  en.twod_set_cursor(0);
  const int32_t type = static_cast<int32_t>(en.vcall(en.field32(en.pilot(), 0x6c), 0x2c, {}));
  if (type == 1) {
    if (!lost && clean) campaign_won_ = (campaign == 4 && mission == 6) ? 1 : 0;
    else en.mission_results(1, 1);
  }
  screen_ = en.construct(0x47b430, {addr(), campaign, mission, addr() + 0x10});  // new(0x13bf4)
  if (!lost && clean && type == 1) {
    en.mission_results(0, (campaign == 4 && mission == 4) ? 0 : 1);
    const int32_t n = static_cast<int32_t>(en.vcall(en.field32(en.pilot(), 0x6c), 0x38, {}));
    Pilot* selected = current_pilot();
    CampaignGame* managed = selected ? selected->campaign() : nullptr;
    const bool managed_campaign = managed && !managed->content_mission.empty();
    if (managed_campaign) en.win_keep_scores(screen_, n);
    en.pilot_record_mission(en.pilot(), 1);
    if (!managed_campaign) en.win_keep_scores(screen_, n);
    if (a().no_voices() == 0 && (!managed_campaign || content_stock_campaign(*managed))) {
      en.format("c%dm%dwin", {campaign, mission}, voice_, sizeof voice_);
      en.voice_start(voice_);
      en.debrief_voice_play(addr());
    }
  }
  if (type == 1) {
    Pilot* selected = current_pilot();
    CampaignGame* managed = selected ? selected->campaign() : nullptr;
    if (managed && !managed->content_mission.empty()) {
      if (lost || !clean) en.pilot_record_mission(en.pilot(), 0);
      if (!content_stock_campaign(*managed)) campaign_won_ = content_campaign_terminal() ? 1 : 0;
    }
  }
  ShellState::enter();
  return true;
}

// Mech3.exe 1.2 @ 0x0047aed0
bool WinState::update() {
  Engine& en = e();
  if (movie_ == 0) return ShellState::update();
  if (en.movie_playing(movie_)) return false;
  en.movie_stop(movie_, 0);
  if (movie_) en.movie_delete(movie_);
  movie_ = 0;
  return false;
}

// Mech3.exe 1.2 @ 0x0047af20
void WinState::leave() {
  Engine& en = e();
  en.video_show_cursor(cursor_);
  en.twod_set_cursor(cursor_);
  if (en.field32(addr(), 0x1c)) {
    en.voice_stop(en.field32(addr(), 0x1c));
    en.voice_release(voice_);
    en.set_field32(addr(), 0x1c, 0);
  }
  campaign_won_ = 0;
  en.shell_clear(addr());
  ShellState::leave();
}

// ---------------------------------------------------------------------------
// Salvage

// Mech3.exe 1.2 @ 0x0047d310
bool SalvageState::enter() {
  screen_ = e().construct(0x47d3b0, {addr()});  // new(0x166a8)
  return ShellState::enter();
}

// Mech3.exe 1.2 @ 0x0047d390
void SalvageState::leave() {
  ShellState::leave();
  if (screen_) e().vcall(screen_, 0x08, {1});
  screen_ = 0;
}

// ---------------------------------------------------------------------------
// The fade state (docs/game-loop.md, The fade state)

// Mech3.exe 1.2 @ 0x0048eea0
void fade_push(zgame::State* next, uint32_t arg, int32_t direction, int32_t buffer, float seconds,
               uint8_t r, uint8_t g, uint8_t b) {
  FadeState& f = states().fade;
  f.buffer_ = buffer;
  f.mode_ = FadeState::kPush;
  f.arg_ = arg;
  f.next_ = next;
  f.setup(direction, seconds, r, g, b);
  app().push(&f, 0);
}

// Mech3.exe 1.2 @ 0x0048ef00
void fade_replace(zgame::State* next, uint32_t arg, int32_t direction, int32_t buffer, float seconds,
                  uint8_t r, uint8_t g, uint8_t b) {
  FadeState& f = states().fade;
  f.buffer_ = buffer;
  f.mode_ = FadeState::kReplace;
  f.arg_ = arg;
  f.next_ = next;
  f.setup(direction, seconds, r, g, b);
  app().replace(&f, 0);
}

// Mech3.exe 1.2 @ 0x0048f060
void FadeState::setup(int32_t direction, float seconds, uint8_t r, uint8_t g, uint8_t b) {
  direction_ = direction;
  colour_ = zvid::pack_bytes(r, g, b);
  seconds_ = seconds;
  t_ = 0.0f;
  one_ = 1.0f;
  rate_ = 1.0f / seconds;
}

// Mech3.exe 1.2 @ 0x0048ef60: the still is taken as the fade is queued.
void FadeState::on_queued() { image_ = e().video_grab(buffer_); }

// Mech3.exe 1.2 @ 0x0048ef70
bool FadeState::enter() {
  Engine& en = e();
  cursor_ = en.video_show_cursor(0);
  en.twod_set_cursor(0);
  return true;
}

// Mech3.exe 1.2 @ 0x0048ef90
bool FadeState::update() {
  if (step(e().frame_dt())) return false;
  switch (mode_) {
    case kPush:
      a().pop(arg_);
      if (next_) a().push(next_, arg_);
      break;
    case kPopTwice:
      a().pop(0);
      a().pop(arg_);
      break;
    case kReplace:
      if (next_) a().replace(next_, 0);
      else a().pop(arg_);
      break;
    default: break;
  }
  return false;
}

// Mech3.exe 1.2 @ 0x0048f030: the cursor's old state goes to both calls.
void FadeState::leave() {
  Engine& en = e();
  en.video_show_cursor(cursor_);
  en.twod_set_cursor(cursor_);
  en.image_free(image_);
  image_ = 0;
}

// Mech3.exe 1.2 @ 0x0048f0b0: the still, then the colour over it at the
// fade's alpha. Time-based (the frame's dt), so no per-frame scaling. With no
// still it ends at once.
bool FadeState::step(float dt) {
  if (!image_) return false;
  Engine& en = e();
  bool going = true;
  float alpha = t_ = rate_ * dt + t_;
  if (direction_ == -1) {
    alpha = 1.0f - alpha;
    if (!(alpha > 0.0f)) {  // x87 "not above": NaN ends it too
      alpha = 0.0f;
      going = false;
    }
  } else if (alpha > 1.0f) {
    alpha = 1.0f;
    going = false;
  }
  if (en.video_mode() != 0) en.video_begin_3d();
  else en.video_lock();
  en.image_draw(image_, 0, 0, 0, 0);
  if (en.video_mode() != 0) en.video_end_3d();
  en.screen_tint(colour_, 0, alpha);
  if (en.video_mode() != 0) {
    en.video_begin_scene();
    en.video_draw_3d();
    en.video_end_scene();
    en.video_flip(0, 0, 0, 0);
  } else {
    en.render_finish();
    en.video_unlock();
    en.video_flip(0, 0, 1, 1);
  }
  return going;
}

}  // namespace mech3
