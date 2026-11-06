// The frame-rate fixes (zfix::Options::per_second, docs/fixes.md): the same
// second of play at 30 and 144 fps comes out the same, and the exe's
// per-frame path does not.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "check.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zrender/zrndr_prim2d.h"
#include "gamez/zrender/zrndr_span.h"
#include "gamez/zrender/zrndr_tint.h"
#include "gamez/zmodel/gmod_puff.h"
#include "gamez/zrender/zrndr_blit.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zvideo/zvid_dd.h"
#include "mech3/hud.h"
#include "mech3/message_panel.h"
#include "mech3/mech_hud.h"
#include "mech3/lance_orders.h"
#include "mech3/mech.h"
#include "gamez/zinput/zinput.h"
#include "mech3/controls_cfg.h"
#include "mech3/mech_control.h"
#include "platform/platform.h"
#include "mech3/mech_fieldbase.h"
#include "mech3/mech_pocket.h"
#include "mech3/pocket_panel.h"
#include "mech3/mech_skin.h"
#include "mech3/mech_skis.h"
#include "mech3/mech_waynet.h"
#include "mech3/zai_attack.h"
#include "mech3/zai_brain.h"
#include "mech3/zrdraiv.h"
#include "mech3/zrdrvalve.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_move.h"
#include "mech3/mech_place.h"
#include "mech3/mission_world.h"
#include "mech3/turret.h"
#include "mech3/weapon.h"
#include "gamez/zmath/zmth_matrix.h"
#include "mech3/veh_controls.h"
#include "mech3/veh_move.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zweapon/zweapon.h"

namespace {

float lava_heat(float fps, bool per_second) {
  zfix::options().per_second = per_second;
  mech3::heat_calls().heat_option = [] { return true; };
  mech3::Foot feet[2];
  mech3::Leg legs[2];
  mech3::Mech m{};
  for (int i = 0; i < 2; ++i) legs[i].foot = &feet[i], m.legs[i] = &legs[i];
  feet[0].soil = 8;
  const int frames = static_cast<int>(fps / 2);  // half a second, under the overheat line
  for (int i = 0; i < frames; ++i) mech3::update_heat(m, 1.0f / fps);
  mech3::heat_calls().heat_option = nullptr;
  zfix::options().per_second = true;
  return m.heat;
}

float smoke_speed(float fps, bool per_second) {
  zfix::options().per_second = per_second;
  zmod::PuffState& s = zmod::puff_state();
  s.particles.clear();
  s.wind = {};
  zmod::Particle p{};
  p.vel[0] = 10.0f, p.life = 100.0f, p.friction = 1.0f, p.wind_factor = 1.0f;
  s.particles.push_back(p);
  ztimer::state().dt = 1.0f / fps;
  for (int i = 0; i < static_cast<int>(fps); ++i) zmod::puff_tick();
  const float v = s.particles.front().vel[0];
  s.particles.clear();
  zfix::options().per_second = true;
  return v;
}

}  // namespace

TEST(fix_lava_heat_per_second) {
  EXPECT(std::fabs(lava_heat(30, true) - 0.75f) < 1e-3f);
  EXPECT(std::fabs(lava_heat(144, true) - 0.75f) < 1e-3f);
  EXPECT(std::fabs(lava_heat(30, false) - 0.75f) < 1e-3f);  // the exe at 30 fps
  EXPECT(lava_heat(144, false) > 3.5f * 0.75f / 2.0f);       // and nearly 5x at 144
}

TEST(fix_smoke_drag_per_second) {
  const float want = 10.0f * std::exp(-1.0f);
  EXPECT(std::fabs(smoke_speed(30, true) - want) < 0.05f);
  EXPECT(std::fabs(smoke_speed(144, true) - want) < 0.05f);
  EXPECT(smoke_speed(144, false) == 10.0f);  // the exe's table: no drag at all
}

// The Mad Cat's and Supernova's damage masks land on their damage textures
// (mcat_d01.., supern_d01..) with the fix, and two / one slots early without.
TEST(fix_damage_lists_line_up) {
  const std::string path = std::string(MW3_GAME_DIR) + "/zbd/reader.zbd";
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    std::fclose(f);
  } else {
    std::printf("  (skipped: no %s)\n", path.c_str());
    return;
  }
  zrdr::init();
  EXPECT(zrdr::open_archive(path.c_str(), true));
  struct Case { const char* file; int first_d; };  // index of the first _d texture
  for (const Case& c : {Case{"dfn_madcat.zrd", 17}, Case{"dfn_supernova.zrd", 15}}) {
    for (bool fix : {true, false}) {
      zfix::options().bugs = fix;
      mech3::SkinSet* set = mech3::read_skins(zrdr::load(c.file));
      EXPECT(set && set->damage);
      if (!set || !set->damage) continue;
      const auto& prim = set->skins[0]->entries;
      const auto& dmg = set->damage->entries;
      EXPECT(std::strncmp(prim[c.first_d]->name, c.file[4] == 'm' ? "mcat_d01" : "supern_d01", 8) == 0);
      const bool lined = dmg.size() == prim.size() && dmg[c.first_d]->name && !dmg[c.first_d - 1]->name;
      EXPECT(lined == fix);
      delete set;
    }
  }
  zfix::options().bugs = true;
  zrdr::close_archives(true);
}

namespace {
// Counts what the sound code asks of the device; every call succeeds.
struct CountingOutput : platform::SoundOutput {
  int plays = 0;
  int32_t create() override { return 0; }
  void release_device() override {}
  int32_t set_cooperative_level(uint32_t, uint32_t) override { return 0; }
  int32_t get_caps(uint32_t*) override { return 0; }
  int32_t create_buffer(const platform::BufferDesc&, platform::SoundBuffer*) override { return 0; }
  int32_t duplicate_buffer(platform::SoundBuffer, platform::SoundBuffer*) override { return 0; }
  void release(platform::SoundBuffer) override {}
  int32_t get_format(platform::SoundBuffer, platform::WaveFormat*) override { return 0; }
  int32_t set_format(platform::SoundBuffer, const platform::WaveFormat&) override { return 0; }
  int32_t get_status(platform::SoundBuffer, uint32_t* s) override { return *s = 0, 0; }
  int32_t restore(platform::SoundBuffer) override { return 0; }
  int32_t lock(platform::SoundBuffer, uint32_t, uint32_t, void**, uint32_t*, void**, uint32_t*) override { return 0; }
  int32_t unlock(platform::SoundBuffer, void*, uint32_t, void*, uint32_t) override { return 0; }
  int32_t play(platform::SoundBuffer, uint32_t) override { return ++plays, 0; }
  int32_t stop(platform::SoundBuffer) override { return 0; }
  int32_t set_current_position(platform::SoundBuffer, uint32_t) override { return 0; }
  int32_t get_current_position(platform::SoundBuffer, uint32_t*, uint32_t*) override { return 0; }
  int32_t get_volume(platform::SoundBuffer, int32_t*) override { return 0; }
  int32_t set_volume(platform::SoundBuffer, int32_t) override { return 0; }
  int32_t set_pan(platform::SoundBuffer, int32_t) override { return 0; }
  int32_t get_frequency(platform::SoundBuffer, uint32_t*) override { return 0; }
  int32_t set_frequency(platform::SoundBuffer, uint32_t) override { return 0; }
};

// A fade from 0 to -30 dB, a second of 30 fps steps: ended or still running.
bool fade_ends(bool fix) {
  zfix::options().bugs = fix;
  CountingOutput out;
  zsnd::hooks().output = &out;
  zsnd::Sound s;
  s.voice.buffer = 1, s.voice.sound = &s;
  auto* f = new zsnd::Fade{-3000.0f, 0.0f, &s.voice, 0};
  zsnd::state().fades.push_back(f);
  for (int i = 0; i < 60; ++i) zsnd::fades_update(1.0f / 30.0f);
  const bool ended = zsnd::state().fades.empty();
  if (!ended) zsnd::state().fades.clear(), delete f;
  for (zsnd::Fade* x : zsnd::state().free_fades) delete x;
  zsnd::state().free_fades.clear();
  zsnd::hooks().output = nullptr;
  zfix::options().bugs = true;
  return ended;
}

// Pause kept one voice of a sound that also has a quieter duplicate: how many
// plays the unpause asks for.
int unpause_plays(bool fix) {
  zfix::options().bugs = fix;
  CountingOutput out;
  zsnd::hooks().output = &out;
  zsnd::Sound s;
  zsnd::Voice dup;
  s.voice.buffer = 1, s.voice.sound = &s;
  dup.buffer = 2, dup.sound = &s, dup.volume = -500;
  s.dups.push_back(&dup);
  zsnd::Snapshot snap;
  zsnd::Record first;
  first.master = zsnd::state().master();
  snap.push_back(first);
  zsnd::Record r;
  r.voice = &s.voice, r.sound = &s;
  snap.push_back(r);
  snap.push_back(r);  // the exe's snapshot repeats a record for a voice it skips
  zsnd::resume_snapshot(&snap);
  zsnd::hooks().output = nullptr;
  zfix::options().bugs = true;
  return out.plays;
}
}  // namespace

TEST(fix_sound_fade_to_a_partial_volume_ends) {
  EXPECT(fade_ends(true));
  EXPECT(!fade_ends(false));
}

TEST(fix_unpause_resumes_only_the_kept_voices) {
  EXPECT(unpause_plays(true) == 1);
  EXPECT(unpause_plays(false) == 4);  // both voices, once per record
}

// The wingmen's six ammo groups keep their names, each line playable twice.
TEST(fix_ammo_groups_keep_their_names) {
  const std::string path = std::string(MW3_GAME_DIR) + "/zbd/reader.zbd";
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    std::fclose(f);
  } else {
    std::printf("  (skipped: no %s)\n", path.c_str());
    return;
  }
  zrdr::init();
  EXPECT(zrdr::open_archive(path.c_str(), true));
  zrdr::Node* groups = zrdr::find(zrdr::load("sounds.zrd"), "SOUND_GROUPS");
  EXPECT(groups != nullptr);
  int found = 0, named_play_count = 0;
  for (bool fix : {true, false}) {
    zfix::options().bugs = fix;
    for (int32_t i = 1; groups && i < groups->list[0].i; ++i) {
      zsnd::Group* g = zsnd::read_group(&groups->list[i], nullptr);
      if (!g) continue;
      if (g->name && std::strcmp(g->name, "dom_low_ammo") == 0) {
        ++found;
        EXPECT(fix && g->entries.size() == 4 && g->entries[0].count == 2 && g->entries[3].count == 2);
      }
      if (g->name && std::strcmp(g->name, "PLAY_COUNT") == 0) ++named_play_count;
      delete g;
    }
  }
  zfix::options().bugs = true;
  EXPECT(found == 1);
  EXPECT(named_play_count == 6);
  zrdr::close_archives(true);
}

// The movement steps (mech_move, mech_skis, mech_collide).
namespace {

// Animations without motion data: no root motion.
struct StillAnim : mech3::MechAnim {
  void velocity(mech3::Vec3*) override {}
};
struct StillAction : mech3::MechAction {
  void hip(float* p, float q[4]) override {
    p[0] = p[1] = p[2] = 0.0f;
    q[0] = 1.0f, q[1] = q[2] = q[3] = 0.0f;
  }
};

mech3::Mech* bare_mech() {
  static mech3::Mech m;
  static mech3::MechControls c;
  static mech3::Torso torso;
  static mech3::Head head;
  static mech3::Hip hip;
  static mech3::Leg legs[2];
  static mech3::Foot feet[2];
  static mech3::MechData pilot;
  static StillAnim anims[13];
  m = mech3::Mech{};
  c = mech3::MechControls{};
  c.owner = &m;
  m.controls = &c;
  m.torso = &torso, m.head = &head, m.hip = &hip, m.pilot = &pilot;
  for (int i = 0; i < 2; ++i) feet[i] = mech3::Foot{}, legs[i].foot = &feet[i], m.legs[i] = &legs[i];
  for (int i = 0; i < 13; ++i) anims[i] = StillAnim{}, anims[i].loop_time = 1.0f, m.anims[i] = &anims[i];
  // No parts to pose.
  mech3::move_calls().jet_pose = [](mech3::Mech&) {};
  mech3::move_calls().action_pose = [](mech3::Mech&, mech3::MechAction&, const mech3::Vec3&, const float*) {};
  m.remote = true;
  m.height = 10.0f;
  m.heat = 0.0f;
  return &m;
}

float carried_momentum(float fps, bool per_second) {
  zfix::options().per_second = per_second;
  mech3::Mech& m = *bare_mech();
  static StillAction act;
  act = StillAction{};
  act.loop_time = 10.0f;
  m.action_objects[5] = &act;
  m.actions.push_back(5);
  m.anim_state = 12;
  m.vec_3c = {10.0f, 0.0f, 0.0f};
  mech3::move_calls().skis_update = [](mech3::Mech&, float) { return false; };
  for (int i = 0; i < static_cast<int>(fps); ++i) m.update_action(1.0f / fps);
  mech3::move_calls().skis_update = nullptr;
  zfix::options().per_second = true;
  return m.momentum.x;
}

float blend_after_half_second(float fps, bool per_second) {
  zfix::options().per_second = per_second;
  mech3::Mech& m = *bare_mech();
  m.anim_state = 5;
  m.detail = 0x10;
  static float blend;
  blend = -1.0f;
  mech3::move_calls().skis_update = [](mech3::Mech&, float) { return false; };
  mech3::move_calls().gait_pose = [](mech3::Mech&, mech3::MechAnim&, float b) { blend = b; };
  const float dt = 1.0f / fps;
  for (int i = 0; i <= static_cast<int>(fps / 2); ++i) {
    m.move(dt, m.anims[4], 0.5f);
    ++m.gait_frames;
    m.gait_steps = m.gait_steps + zfix::frame_steps(dt);
  }
  mech3::move_calls() = mech3::MoveCalls{};
  zfix::options().per_second = true;
  return blend;
}

float jet_climb(float fps, bool per_second) {
  zfix::options().per_second = per_second;
  mech3::Mech& m = *bare_mech();
  m.equipment[11] = 1;
  m.controls->jump = 1.0f;
  const float g = mech3::environment().gravity;
  mech3::environment().gravity = 1000.0f;  // to reach the cap
  float sum = 0;
  for (int i = 0; i < static_cast<int>(fps); ++i) {
    m.impulse = {0, 0, 0};
    m.jet_fuel = 1.0f;
    m.jump_jets(1.0f / fps);
    sum += m.impulse.y;
  }
  mech3::environment().gravity = g;
  zfix::options().per_second = true;
  return sum;
}

// One touch's damage: contact is a one-frame bounce, so a hit counts the same at any rate.
float touch_damage(float fps, bool per_second) {
  zfix::options().per_second = per_second;
  mech3::Mech& m = *bare_mech();
  m.collider.touches.push_back(mech3::CollisionHit{});
  static float sum;
  sum = 0;
  mech3::collide_calls().touch = [](mech3::Handle, const mech3::Vec3*, const mech3::CollisionHit&, float d) {
    sum += d;
    return 0.0f;
  };
  m.frame_dt = 1.0f / fps;
  m.touch_response();
  mech3::collide_calls() = mech3::CollideCalls{};
  zfix::options().per_second = true;
  return sum;
}

float wading_speed(float depth, bool fixed) {
  const bool bugs = zfix::options().bugs;
  zfix::options().bugs = fixed;
  mech3::Mech& m = *bare_mech();
  m.foot(0)->water_depth = depth, m.foot(1)->water_depth = depth;
  m.anim_state = 1;
  m.detail = 0x10;
  mech3::move_calls().move = [](mech3::Mech&, float, mech3::MechAnim*, float) {};
  m.select_gait(1.0f / 30);
  mech3::move_calls() = mech3::MoveCalls{};
  zfix::options().bugs = bugs;
  return m.speed_target;
}

// Half a second holding the throttle-up key: the target's climb.
float throttle_after_half_second(float fps, bool per_second) {
  zfix::options().per_second = per_second;
  static mech3::Mech m;
  static mech3::PlayerControls c;
  m = mech3::Mech{};
  c = mech3::PlayerControls{};
  c.owner = &m;
  m.controls = &c;
  mech3::controls_calls().command = [](int cmd) { return cmd == 0x18 ? 2u : 0u; };  // CMD_THROTTLE_UP held
  mech3::controls_calls().joy_present = [] { return false; };
  for (int i = 0; i < static_cast<int>(fps / 2); ++i) c.update(1.0f / fps);
  mech3::controls_calls() = mech3::ControlsCalls{};
  zfix::options().per_second = true;
  return c.throttle_target;
}

}  // namespace

TEST(fix_throttle_keys_per_second) {
  EXPECT(std::fabs(throttle_after_half_second(30, true) - 0.375f) < 1e-5f);  // 0.025 a frame at 30 fps
  EXPECT(std::fabs(throttle_after_half_second(144, true) - 0.375f) < 1e-3f);
  EXPECT(throttle_after_half_second(30, false) == throttle_after_half_second(30, true));
  EXPECT(throttle_after_half_second(144, false) == 1.0f);  // the exe: 0.025 a frame, full in 0.28 s
}

// The time the throttle (not its target) takes to reach `at` with the key
// held, the mech's easing at 0.5/s.
float throttle_reaches(float fps, float at, bool per_second = true) {
  zfix::options().per_second = per_second;
  static mech3::Mech m;
  static mech3::PlayerControls c;
  m = mech3::Mech{};
  c = mech3::PlayerControls{};
  m.throttle_up = 0.5f;
  c.owner = &m;
  m.controls = &c;
  mech3::controls_calls().command = [](int cmd) { return cmd == 0x18 ? 2u : 0u; };
  mech3::controls_calls().joy_present = [] { return false; };
  int frames = 0;
  while (c.throttle < at && frames < fps * 30) c.update(1.0f / fps), ++frames;
  mech3::controls_calls() = mech3::ControlsCalls{};
  zfix::options().per_second = true;
  return frames / fps;
}

// The snap at 0.009 (0x00426960) was inside the keys' step below 12 ms: above
// 83 fps the throttle jumped to its target every frame, no easing at all, and a
// mech reached a wall nearly 2 s sooner. The same curve at any rate now.
TEST(fix_throttle_easing_same_at_any_rate) {
  const float at30 = throttle_reaches(30, 0.8f);
  std::printf("  throttle to 0.8 (s):");
  for (const float fps : {30.0f, 62.5f, 83.0f, 91.0f, 100.0f, 125.0f, 144.0f, 250.0f}) {
    const float t = throttle_reaches(fps, 0.8f);
    std::printf(" %g fps %.3f", fps, t);
    EXPECT(std::fabs(t - at30) < 0.08f);
  }
  std::printf("; the exe's at 144 fps %.3f\n", throttle_reaches(144, 0.8f, false));
  EXPECT(at30 > 1.5f);
  EXPECT(throttle_reaches(30, 0.8f) == throttle_reaches(30, 0.8f, false));  // the exe's at 30 fps
  // Slower than that the gap stays the exe's 0.009; a gap of 0.012 eases.
  static mech3::Mech m;
  static mech3::PlayerControls c;
  m = mech3::Mech{};
  c = mech3::PlayerControls{};
  m.throttle_up = 0.5f;
  c.owner = &m;
  c.throttle = 0.5f, c.throttle_target = 0.512f;
  c.MechControls::update(1.0f / 15);
  EXPECT(c.throttle > 0.5f && c.throttle < 0.512f);
  c.throttle = 0.5f, c.throttle_target = 0.508f;
  c.MechControls::update(1.0f / 15);
  EXPECT(c.throttle == 0.508f);
}

TEST(fix_action_momentum_per_second) {
  const float want = 10.0f * std::pow(0.6f, 30.0f);
  EXPECT(std::fabs(carried_momentum(30, true) - want) < want * 1e-3f);
  EXPECT(std::fabs(carried_momentum(144, true) - want) < want * 1e-2f);
  EXPECT(carried_momentum(30, false) == carried_momentum(30, true));  // the exe's step at 30 fps
  EXPECT(carried_momentum(144, false) < want * 1e-6f);               // gone far sooner at 144
}

TEST(fix_gait_blend_in_per_second) {
  EXPECT(std::fabs(blend_after_half_second(30, true) - 0.75f) < 1e-5f);
  EXPECT(std::fabs(blend_after_half_second(144, true) - 0.75f) < 0.02f);
  EXPECT(blend_after_half_second(30, false) == blend_after_half_second(30, true));
  EXPECT(blend_after_half_second(144, false) == 1.0f);  // the exe: done after 20 frames, 0.14 s
}

TEST(fix_jet_cap_per_second) {
  EXPECT(std::fabs(jet_climb(30, true) - 180.0f) < 0.1f);  // 6 a frame at 30 fps
  EXPECT(std::fabs(jet_climb(144, true) - 180.0f) < 0.1f);
  EXPECT(jet_climb(30, false) == jet_climb(30, true));
  EXPECT(jet_climb(144, false) > 800.0f);  // the exe: 6 a frame at any rate
}

TEST(fix_touch_damage_the_same_a_hit_at_any_rate) {
  for (const float fps : {15.0f, 30.0f, 60.0f, 125.0f, 144.0f, 250.0f})
    for (const bool per_second : {true, false}) EXPECT(touch_damage(fps, per_second) == 20.0f);
}

// A heat sink lost to a critical hit (0x00460d50): the exe's 0x0045fbc0
// always answers failure, so the "heat sink" callout never played; with zfix
// bugs a sink actually lost counts (docs/fixes.md). The sink goes either way.
TEST(fix_heat_sink_critical_is_called_out) {
  static int voiced = 0;
  auto crit = [](bool fixed) {
    const bool bugs = zfix::options().bugs;
    zfix::options().bugs = fixed;
    mech3::Mech& m = *bare_mech();
    m.active = true;
    m.heat_sinks.count = 10;
    mech3::Mech* was = mech3::world().player;
    mech3::world().player = &m;
    mech3::heat_calls().screen = [](mech3::Mech&) -> void* { return &voiced; };
    mech3::damage_calls().equip_voice = [](int) { ++voiced; };
    voiced = 0;
    m.torso->mech = &m;
    const bool ok = m.torso->critical(mech3::kCritSink, 0);
    const int left = m.heat_sinks.count;
    mech3::heat_calls().screen = nullptr;
    mech3::damage_calls().equip_voice = nullptr;
    mech3::world().player = was;
    zfix::options().bugs = bugs;
    return std::make_tuple(ok, voiced, left);
  };
  const auto [ok_off, voiced_off, left_off] = crit(false);
  const auto [ok_on, voiced_on, left_on] = crit(true);
  std::printf("  sink critical: original %d (%d callouts), fixed %d (%d callouts); sinks left %d / %d\n", ok_off, voiced_off, ok_on,
              voiced_on, left_off, left_on);
  EXPECT(!ok_off && voiced_off == 0 && left_off == 9);
  EXPECT(ok_on && voiced_on == 1 && left_on == 9);
}

TEST(fix_deep_water_curve) {
  // Fixed: full speed at a tenth of the height down to 0.7 at full depth, no step.
  EXPECT(std::fabs(wading_speed(1.0f, true) - 1.0f) < 1e-5f);
  EXPECT(std::fabs(wading_speed(5.5f, true) - 0.85f) < 1e-5f);
  EXPECT(std::fabs(wading_speed(9.99f, true) - 0.7f) < 1e-3f);
  EXPECT(std::fabs(wading_speed(12.0f, true) - 0.7f) < 1e-5f);
  // The exe: near a standstill just under full depth, then back to 0.7.
  EXPECT(wading_speed(9.99f, false) < 0.01f);
  EXPECT(std::fabs(wading_speed(12.0f, false) - 0.7f) < 1e-5f);
}

// A foot just behind its ski's middle: with the fix the middle probe is its
// contact (and its water); the exe's unset width picks the back probe.
// Delayed updates (fixes.md, ai.md section 13): a lone timer whose update
// starts another keeps it with the fix; the exe loses it.
TEST(fix_lone_delayed_update_keeps_the_next) {
  using namespace mech3::ai;
  for (const bool bugs : {false, true}) {
    zfix::options().bugs = bugs;
    valves_end();
    Valve* a = valve_get("a");
    Valve* b = valve_get("b");
    Valve* c = valve_get("c");
    a->add(new DelayUpdateAction(b, 1.0f));  // the game time is 0: due at 1
    b->add(new DelayUpdateAction(c, 1.0f));
    a->set(1);
    valves_tick(2.0f);  // b set; its update starts c's timer while the lone one runs
    valves_tick(3.0f);
    EXPECT(b->get() == 1);
    EXPECT(c->get() == (bugs ? 1 : 0));
    valves_end();
  }
  zfix::options().bugs = true;
}

// Valves that update themselves (FIX (bug), fixes.md, ai.md section 8): an
// update of itself, or a loop of updates, recursed until the stack ran out
// in the exe; here a valve firing inside its own reactions is skipped, so
// each reaction round the loop runs once per set.
namespace {
struct CountFires : mech3::ai::Reaction {
  int* n;
  explicit CountFires(int* c) : n(c) {}
  void fire(mech3::ai::Valve&) override { ++*n; }
};
}  // namespace

TEST(fix_valves_that_update_themselves_fire_once) {
  using namespace mech3::ai;
  valves_end();
  valve_reentries() = 0;
  int na = 0, nb = 0, nc = 0;
  Valve* a = valve_get("a");
  Valve* b = valve_get("b");
  Valve* c = valve_get("c");
  a->add(new UpdateAction(a));  // itself
  a->add(new CountFires(&na));
  EXPECT(a->set(1) == 0 && na == 1 && !a->firing);
  b->add(new UpdateAction(c));  // b -> c -> a -> b
  c->add(new UpdateAction(a));
  a->add(new UpdateAction(b));
  b->add(new CountFires(&nb));
  c->add(new CountFires(&nc));
  na = 0;
  EXPECT(b->set(1) == 0 && na == 1 && nb == 1 && nc == 1);
  EXPECT(!a->firing && !b->firing && !c->firing && valve_reentries() == 3);
  EXPECT(c->set(1) == 0 && na == 2 && nb == 2 && nc == 2);  // again from another valve
  valves_end();
  valve_reentries() = 0;
}

TEST(fix_foot_uses_its_middle_probe) {
  mech3::Skis s{};
  for (int i = 0; i < 6; ++i) s.local[i].z = (i % 3 - 1) * 6.0f;  // front -6, middle 0, back 6
  s.in_water[1] = 1;
  float surface = 0.0f;
  mech3::GroundHit* hit = nullptr;
  s.height_at(0, 1.0f, &hit);
  EXPECT(hit == &s.contact[1] && s.water_at(0, 1.0f, &surface));
  // The width is half x 0.5 = 3, so the middle counts within 1.5 of it, as in Pirate's Moon.
  s.height_at(0, 1.4f, &hit);
  EXPECT(hit == &s.contact[1] && s.water_at(0, 1.4f, &surface));
  s.height_at(0, 1.6f, &hit);
  EXPECT(hit == &s.contact[2] && !s.water_at(0, 1.6f, &surface));
  zfix::options().bugs = false;
  s.height_at(0, 1.0f, &hit);
  EXPECT(hit == &s.contact[2] && !s.water_at(0, 1.0f, &surface));
  zfix::options().bugs = true;
}

namespace {
// One aiv.zrd record of a mission archive, parsed with the fixes on or off.
// Returns the record (owned by the caller) or null without the game files.
mech3::ai::AivRecord* aiv_record(const char* archive, const char* name, bool fix) {
  const std::string path = std::string(MW3_GAME_DIR) + "/zbd/" + archive;
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) std::fclose(f);
  else return nullptr;
  zfix::options().bugs = fix;
  zrdr::init();
  zrdr::open_archive(path.c_str(), true);
  auto* r = new mech3::ai::AivRecord;
  r->parse(zrdr::find(zrdr::load("aiv.zrd"), name), name);
  zfix::options().bugs = true;
  return r;
}
void close_aiv(mech3::ai::AivRecord* r) {
  delete r;
  mech3::ai::aiv_file_reset();
  zrdr::close_archives(true);
}
}  // namespace

// Alan's two misspelt records get his radio voice; the exe gives them none.
TEST(fix_alan_misspelt_gets_his_voice) {
  for (bool fix : {true, false}) {
    mech3::ai::AivRecord* r = aiv_record("c1/readeria1.zbd", "Alan_Mattilla", fix);
    if (!r) return std::printf("  (skipped: no game files)\n"), void();
    EXPECT((r->sound_tag && std::strcmp(r->sound_tag, "alan") == 0) == fix);
    close_aiv(r);
  }
}

// Enemy_12's loose orders on c4 are read; the exe leaves it with none.
TEST(fix_loose_orders_are_read) {
  for (bool fix : {true, false}) {
    mech3::ai::AivRecord* r = aiv_record("c4/readeria1.zbd", "Enemy_12", fix);
    if (!r) return std::printf("  (skipped: no game files)\n"), void();
    EXPECT((r->vlist && !r->vlist->empty()) == fix);
    close_aiv(r);
  }
}

// ZAPS_HUD's second value is the static's hold time (mech3/weapon.cpp); the
// exe reads the first value twice (0x004af71b).
TEST(fix_zaps_hud_reads_its_hold) {
  const std::string path = std::string(MW3_GAME_DIR) + "/zbd/reader.zbd";
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    std::fclose(f);
  } else {
    std::printf("  (skipped: no %s)\n", path.c_str());
    return;
  }
  zrdr::init();
  EXPECT(zrdr::open_archive(path.c_str(), true));
  zrdr::Node* b = zrdr::find(zrdr::load("weapons.zrd"), "BALLISTICS");
  EXPECT(b != nullptr);
  zrdr::Node* erppc = nullptr;
  for (int32_t i = 1; b && i + 1 < zrdr::list_count(b); i += 2)
    if (b->list[i].type == zrdr::String && std::strcmp(b->list[i].s, "ERPPC") == 0) erppc = &b->list[i + 1];
  EXPECT(erppc != nullptr);
  for (bool fix : {true, false}) {
    zfix::options().bugs = fix;
    zwep::WeaponDef d;
    d.name = "ERPPC";
    d.period = 1.0f;
    if (erppc) mech3::make_weapon_type(erppc, &d);
    EXPECT(d.type && d.type->zaps_hud == 0.5f && d.type->zaps_hud_2 == (fix ? 2.0f : 0.5f));
    mech3::free_weapon_type(&d);
  }
  zfix::options().bugs = true;
  zrdr::close_archives(true);
}

namespace {

// TAG's recharge (U_RECHARGE_TIME 60: 166.7 a second) over `seconds` at `fps`,
// hot by the BeamWeapon's factor min(1, max(0.5, 0.5 / heat)).
int32_t tag_recharge(float fps, float heat, float seconds, bool per_second) {
  zfix::options().per_second = per_second;
  const float rate = 10000.0f * 1.0f / 60.0f;
  const float dt = 1.0f / fps;
  float f = heat > 0.5f ? 0.5f / heat : 1.0f;
  if (f < 0.5f) f = 0.5f;
  float carry = 0;
  int32_t n = 0;
  for (int i = 0; i < static_cast<int>(fps * seconds + 0.5f); ++i) n += mech3::beam_recharge(rate, f * dt, dt, &carry);
  zfix::options().per_second = true;
  return n;
}

}  // namespace

TEST(fix_beam_recharge_per_second) {
  EXPECT(tag_recharge(30, 0, 10, false) == 1500);   // the exe at 30 fps: 5 a frame
  EXPECT(tag_recharge(30, 0, 10, true) == 1500);    // unchanged there
  EXPECT(tag_recharge(60, 0, 10, false) == 1200);   // the exe: 2 a frame
  EXPECT(tag_recharge(144, 0, 10, false) == 1440);  // 1 a frame
  EXPECT(tag_recharge(200, 0, 10, false) == 0);     // never refills
  EXPECT(tag_recharge(144, 0.6f, 10, false) == 0);  // nor at 144 fps once a little hot
  for (float fps : {60.0f, 144.0f, 200.0f, 500.0f}) {
    EXPECT(std::abs(tag_recharge(fps, 0, 10, true) - 1500) <= 1);
    EXPECT(std::abs(tag_recharge(fps, 1.0f, 10, true) - tag_recharge(30, 1.0f, 10, true)) <= 1);
  }
}

// The HUD's +0x22e3c (hud.md section 3): with a video mode hud.zrd has no
// block for, the set-up reads whatever the block slot held. The fix clears it
// in the constructor; here the HUD is built over garbage and the set-up runs
// with no block (the exe would read through 0xcdcdcdcd).
TEST(fix_hud_block_cleared) {
  zfix::options().bugs = true;
  void* storage = ::operator new(sizeof(mech3::Hud));
  std::memset(storage, 0xcd, sizeof(mech3::Hud));
  auto* hud = new (storage) mech3::Hud(nullptr);  // the default hooks: no hud.zrd, video mode 0
  EXPECT(hud->block == nullptr);
  hud->~Hud();
  ::operator delete(storage);
}


// FIX (A): an alpha blend into the program's keyed layer records its coverage
// instead of mixing with the key; over a drawn pixel it is the plain blend.
TEST(fix_layer_blend_keeps_its_alpha) {
  const uint16_t key = 0xf81f, grey = 0x8410, red = 0xf800;
  uint16_t layer[4] = {key, key, red, key};
  uint8_t cover_a[4] = {};
  uint16_t cover_c[4] = {};
  zrndr::Target2D& t = zrndr::target2d();
  const zrndr::Target2D saved = t;
  t.base = reinterpret_cast<uint8_t*>(layer), t.width = 4, t.height = 1, t.pitch = 8, t.format = 6;
  uint16_t px[4] = {grey, grey, grey, grey};
  uint8_t al[4] = {0x80, 0x80, 0x80, 0x02};
  zimg::Texture img{};
  img.width = 4, img.height = 1, img.pixels = px, img.alpha = al;
  zrndr::layer_cover() = {cover_a, cover_c, key};
  zrndr::blit16(&img, 0, 0, 0, nullptr);
  EXPECT(layer[0] == grey && cover_a[0] == 0x80 && cover_c[0] == grey);  // the grey, half see-through
  EXPECT(layer[2] != red && layer[2] != grey && cover_a[2] == 0);         // blended over the red: opaque
  EXPECT(layer[3] == key && cover_a[3] == 0);                            // below the threshold: untouched
  zrndr::blit16(&img, 0, 0, 0, nullptr);                                  // again over the half-covered pixel
  EXPECT(cover_a[0] == 0x80 + 0x80 * 0x7f / 255 && cover_c[0] == layer[0]);
  zrndr::layer_cover() = {};
  uint16_t plain[1] = {key};
  t.base = reinterpret_cast<uint8_t*>(plain), t.width = 1, t.pitch = 2;
  img.width = 1;
  zrndr::blit16(&img, 0, 0, 0, nullptr);
  EXPECT(plain[0] != grey && plain[0] != key);  // the original path: mixed with the key
  t = saved;
}

// FIX (A): an opaque write over a blend ends the blend's record, also when it
// writes the colour the blend recorded: the damage diagram's blue outlines
// (dmg_<part>_blue, 0x001f) over its translucent blue fill (dmg_bg_blue) were
// drawn see-through, all but the right arm's 0x003f (hud.md section 16).
// Each writer into the 2D layer: keyed, plain and paletted images, lines and
// pixels.
TEST(fix_layer_opaque_ends_a_blend_of_its_colour) {
  const uint16_t key = 0xf81f, blue = 0x001f;
  zrndr::Target2D& t = zrndr::target2d();
  const zrndr::Target2D saved = t;
  uint16_t layer[2];
  uint8_t cover_a[2];
  uint16_t cover_c[2];
  t.base = reinterpret_cast<uint8_t*>(layer), t.width = 2, t.height = 1, t.pitch = 4, t.format = 6;
  uint16_t fill_px[2] = {blue, blue};
  uint8_t fill_al[2] = {0x80, 0x80};
  zimg::Texture fill{};
  fill.width = fill.width_again = 2, fill.height = 1, fill.pixels = fill_px, fill.alpha = fill_al;
  uint16_t px[2] = {blue, blue}, keyed_px[2] = {0, blue};
  uint8_t idx[2] = {1, 1};
  uint16_t pal[2] = {0, blue};
  uint8_t keyed_idx[2] = {0, 1}, solid_al[2] = {0xff, 0xff};
  zimg::Texture keyed{}, plain{}, half{}, paletted{}, pal_keyed{}, pal_half{}, solid{}, pal_solid{};
  keyed.width = keyed.width_again = 2, keyed.height = 1, keyed.pixels = keyed_px, keyed.flags = zimg::kAlpha;
  plain.width = plain.width_again = 2, plain.height = 1, plain.pixels = px;            // the full-width copy
  half.width = half.width_again = 1, half.height = 1, half.pixels = px;               // the row copy
  paletted.width = paletted.width_again = 2, paletted.height = 1, paletted.pixels = idx, paletted.palette = pal;
  pal_keyed = paletted, pal_keyed.pixels = keyed_idx, pal_keyed.flags = zimg::kAlpha;
  pal_half = paletted, pal_half.width = pal_half.width_again = 1;
  solid = plain, solid.alpha = solid_al;          // an alpha plane, opaque
  pal_solid = paletted, pal_solid.alpha = solid_al;
  const char* names[] = {"keyed image",          "plain image", "plain image's rows", "paletted image", "line",
                         "pixel",                "keyed paletted image", "paletted image's rows",
                         "opaque alpha image",   "opaque alpha paletted image"};
  for (int w = 0; w < 10; ++w) {
    layer[0] = layer[1] = key;
    cover_a[0] = cover_a[1] = 0;
    zrndr::layer_cover() = {cover_a, cover_c, key};
    zrndr::blit16(&fill, 0, 0, 0, nullptr);  // the translucent fill: recorded
    const bool recorded = cover_a[1] == 0x80 && cover_c[1] == blue && layer[1] == blue;
    switch (w) {
      case 0: zrndr::blit16(&keyed, 0, 0, 0, nullptr); break;  // keyed on 0: only pixel 1 drawn
      case 1: zrndr::blit16(&plain, 0, 0, 0, nullptr); break;
      case 2: zrndr::blit16(&half, 1, 0, 0, nullptr); break;
      case 3: zrndr::blit16(&paletted, 0, 0, 0, nullptr); break;
      case 4: zrndr::line16(1, 0, 1, 0, blue); break;
      case 5: zrndr::pixel16(1, 0, blue); break;
      case 6: zrndr::blit16(&pal_keyed, 0, 0, 0, nullptr); break;  // keyed on index 0
      case 7: zrndr::blit16(&pal_half, 1, 0, 0, nullptr); break;
      case 8: zrndr::blit16(&solid, 0, 0, 0, nullptr); break;
      default: zrndr::blit16(&pal_solid, 0, 0, 0, nullptr); break;
    }
    if (!recorded || cover_a[1] != 0) std::printf("  %s: the blend's record left\n", names[w]);
    EXPECT(recorded && layer[1] == blue && cover_a[1] == 0);  // opaque blue
    if (w == 0 || w == 6) EXPECT(cover_a[0] == 0x80);                     // the keyed-out pixel keeps the fill's
  }
  zrndr::layer_cover() = {};
  t = saved;
}

// FIX (bug): the lead point (0x0041d940) with no group selected reads the
// group's weapon through a null pointer in the original; here none, false.
TEST(fix_lead_point_without_a_group) {
  struct NoGroup : mech3::HudHooks {
    int32_t wm_group_index(void*) override { return -1; }
    void* mech_weapons(mech3::Mech*) override { return this; }
    void* mech_target_slot(mech3::Mech*) override { return nullptr; }
    void* slot_contact(void*) override { return nullptr; }
  } hooks;
  mech3::set_hud_hooks(&hooks);
  float out[3] = {1, 2, 3};
  EXPECT(!mech3::lead_point(nullptr, out));
  EXPECT(out[0] == 1 && out[1] == 2 && out[2] == 3);
  mech3::set_hud_hooks(nullptr);
}

// FIX (bug): the HUD's timer (0x0043c390) past an hour: 3700.25 s is
// 01:01:40, not the original's 01:01:3640.
namespace mech3 {
float mouse_y_accel(float current, bool inverted);  // options_menu.h (its Handle clashes with mech.h's here)
}

// The game options' invert box (0x00438100): the exe writes -1.3 or 1.3
// over MouseAccelY; fixed, the player's sensitivity keeps its size.
TEST(fix_inverted_mouse_keeps_sensitivity) {
  zfix::options().bugs = true;
  EXPECT(mech3::mouse_y_accel(2.5f, true) == -2.5f);
  EXPECT(mech3::mouse_y_accel(-2.5f, false) == 2.5f);
  EXPECT(mech3::mouse_y_accel(-0.7f, true) == -0.7f);
  EXPECT(mech3::mouse_y_accel(0.0f, true) == -1.3f);
  zfix::options().bugs = false;
  EXPECT(mech3::mouse_y_accel(2.5f, true) == -1.3f);
  EXPECT(mech3::mouse_y_accel(-2.5f, false) == 1.3f);
  zfix::options().bugs = true;
}

namespace mech3 {
void credit_line_offset(zrdr::Node* line, int32_t off[2]);  // credits.h (its Handle clashes with mech.h's here)
}

// The credits' "Other Voices" heading, `"offset" (20)` (0x00429200): the exe
// keeps the line above's (30 0); fixed, the headings' (20 20).
TEST(fix_credits_other_voices_offset) {
  char key[] = "offset";
  zrdr::Node one[2], line[3];
  one[0].type = zrdr::Int, one[0].i = 2;
  one[1].type = zrdr::Int, one[1].i = 20;
  line[0].type = zrdr::Int, line[0].i = 3;
  line[1].type = zrdr::String, line[1].s = key;
  line[2].type = zrdr::List, line[2].list = one;
  zrdr::Node node;
  node.type = zrdr::List, node.list = line;
  int32_t off[2] = {30, 0};
  zfix::options().bugs = true;
  mech3::credit_line_offset(&node, off);
  EXPECT(off[0] == 20 && off[1] == 20);
  zfix::options().bugs = false;
  off[0] = 30, off[1] = 0;
  mech3::credit_line_offset(&node, off);
  EXPECT(off[0] == 30 && off[1] == 0);
  zfix::options().bugs = true;
}

// TARGET's distance past 1 km (0x00441850): the exe runs "1.50km" straight
// on after the name; fixed, " (1.50km)" as " (500m)" under it.
TEST(fix_target_distance_km_spaced) {
  struct Far : mech3::HudHooks {
    int turret = 0;
    bool ready = false;  // no target while the HUD is built
    bool target_valid(void*) override { return ready; }
    void* slot_contact(void*) override { return &turret; }
    float contact_distance(void*) override { return 1500.0f; }
    void* contact_mech(void*) override { return nullptr; }
    void* contact_vehicle(void*) override { return &turret; }
    const char* object_name(void*) override { return "Turret"; }
  } hooks;
  mech3::set_hud_hooks(&hooks);
  void* storage = ::operator new(sizeof(mech3::Hud));
  std::memset(storage, 0, sizeof(mech3::Hud));
  auto* hud = new (storage) mech3::Hud(nullptr);
  hud->mech = reinterpret_cast<mech3::Mech*>(&hooks);  // only passed back to the hooks
  hud->target.hud = hud;
  hooks.ready = true;
  zfix::options().bugs = true;
  hud->target.data_text();
  EXPECT(std::strcmp(hud->target.texts[1].text, "Turret (1.50km)") == 0);
  zfix::options().bugs = false;
  hud->target.data_text();
  EXPECT(std::strcmp(hud->target.texts[1].text, "Turret1.50km") == 0);
  zfix::options().bugs = true;
  hud->mech = nullptr;
  hud->~Hud();
  ::operator delete(storage);
  mech3::set_hud_hooks(nullptr);
}

// The coolant gauge's easing (0x004456f0): a tenth of the way each call in
// the exe; fixed, a tenth per 1/30 s, so two 60 fps frames make one 30 fps step.
TEST(fix_coolant_gauge_per_second) {
  mech3::set_hud_hooks(nullptr);
  void* storage = ::operator new(sizeof(mech3::Hud));
  std::memset(storage, 0, sizeof(mech3::Hud));
  auto* hud = new (storage) mech3::Hud(nullptr);
  zfix::options().per_second = true;
  mech3::coolant_shown() = 1.0f;
  hud->gauges.set_coolant(0.0f, 1.0f / 60.0f);
  hud->gauges.set_coolant(0.0f, 1.0f / 60.0f);
  EXPECT(std::fabs(mech3::coolant_shown() - 0.9f) < 1e-5f);
  zfix::options().per_second = false;
  mech3::coolant_shown() = 1.0f;
  hud->gauges.set_coolant(0.0f, 1.0f / 60.0f);
  hud->gauges.set_coolant(0.0f, 1.0f / 60.0f);
  EXPECT(std::fabs(mech3::coolant_shown() - 0.81f) < 1e-5f);
  zfix::options().per_second = true;
  mech3::coolant_shown() = 1.0f;
  hud->~Hud();
  ::operator delete(storage);
}

// The message panel's slide (0x0040f8d0): ftol(60 x the frame's seconds)
// pixels a frame, 0 above 60 fps; fixed, the fraction carried (60 a second).
TEST(fix_message_panel_slides_above_60fps) {
  mech3::set_hud_hooks(nullptr);
  mech3::message_panel_destroy();
  mech3::message_panel_create();
  mech3::MessagePanel* p = mech3::message_panel();
  p->on_y = p->off_y - 100;  // room to slide (no fonts here: the text has no height)
  const int32_t start = p->y;
  zfix::options().per_second = true;
  mech3::message_panel_add_for(5.0f, "%s", "Objective complete");
  for (int i = 0; i < 30; ++i) p->slide(1.0f / 120.0f);  // a quarter second
  EXPECT(start - p->y >= 14 && start - p->y <= 15);
  mech3::message_panel_reset();
  zfix::options().per_second = false;
  mech3::message_panel_add_for(5.0f, "%s", "Objective complete");
  for (int i = 0; i < 30; ++i) p->slide(1.0f / 120.0f);
  EXPECT(p->y == start);  // the exe's: 0 a frame
  zfix::options().per_second = true;
  mech3::message_panel_destroy();
}

TEST(fix_hud_timer_past_an_hour) {
  struct Clock : mech3::HudHooks {
    float mission_time(bool) override { return 3700.25f; }
  } hooks;
  zfix::options().bugs = true;
  mech3::set_hud_hooks(&hooks);
  void* storage = ::operator new(sizeof(mech3::Hud));
  std::memset(storage, 0, sizeof(mech3::Hud));
  auto* hud = new (storage) mech3::Hud(nullptr);
  hud->mech = reinterpret_cast<mech3::Mech*>(&hooks);  // only passed back to the hooks
  hud->active = 0;
  hud->target.hide();  // no tracking
  hud->data_update(0.033f);
  EXPECT(std::strcmp(hud->timer.text, "01:01:40") == 0);
  zfix::options().bugs = false;
  hud->data_update(0.033f);
  EXPECT(std::strcmp(hud->timer.text, "01:01:3640") == 0);
  zfix::options().bugs = true;
  hud->mech = nullptr;
  hud->~Hud();
  ::operator delete(storage);
  mech3::set_hud_hooks(nullptr);
}

// A mech placed at y -50 on a bridge over a canyon floor at -100: the exe's
// step of y + 10 reaches only -90, so it takes the floor; the fixed step of
// 10 keeps it on the bridge (0x004161c0, mission-start.md section 11).
static float place_on_bridge(bool fix) {
  zfix::options().bugs = fix;
  static mech3::Mech m;
  static mech3::PlayerControls c;
  static mech3::Skis s;
  m = mech3::Mech{}, c = mech3::PlayerControls{}, s = mech3::Skis{};
  static float matrix[12];
  c.owner = &m, m.controls = &c, s.mech = &m, m.skis = &s, m.transform = matrix;
  static float placed_y;
  mech3::place_calls().ground = [](float, float, float, zcls::GroundHits* out) {
    *out = zcls::GroundHits{};
    out->count = 2;
    out->hit[0].height = -100.0f, out->hit[1].height = -50.0f;
    return 0;
  };
  mech3::place_calls().attach = [](mech3::Handle) {};
  mech3::collide_calls().set_position = [](mech3::Mech&, const mech3::Vec3* p) { placed_y = p->y; };
  mech3::update_calls().moved = [](mech3::Mech&) {};
  const float pos[3] = {0.0f, -50.0f, 0.0f};
  m.place(pos, 0.0f);
  mech3::place_calls() = mech3::PlaceCalls{};
  mech3::collide_calls().set_position = nullptr;
  mech3::update_calls().moved = nullptr;
  zfix::options().bugs = true;
  return placed_y;
}

TEST(fix_place_steps_ten_above) {
  EXPECT(std::fabs(place_on_bridge(false) - -99.99f) < 1e-3f);  // the exe: under the bridge
  EXPECT(std::fabs(place_on_bridge(true) - -49.99f) < 1e-3f);   // fixed: on it
}

// TAG (weapons.md "TAG, end to end"; Owen 2026-10-20). The frames the hold
// needs on the target at a frame rate: the exe's float sum takes 61 at 60
// fps and 145 at 144; in whole milliseconds, one second exactly.
static int tag_hold_frames(int fps, bool fix) {
  zfix::options().bugs = fix;
  float t = 0;
  int n = 0;
  do t = static_cast<float>(1.0 / fps) + t, ++n;
  while (!zwep::designate_held(t, 1.0f));
  zfix::options().bugs = true;
  return n;
}

// The frames a TAG beam is cast, held from a full charge (the start frame
// and each update until the one that stops it): the exe's 32 / 62 / 146 at
// 30 / 60 / 144 fps; fixed, 1.067 s at any rate.
static int tag_beam_frames(int fps, bool fix) {
  zfix::options().bugs = fix;
  static mech3::WeaponType t;
  static mech3::WeaponManager m(nullptr);
  zwep::WeaponDef def{};
  t.fire_time = 1.0f;
  def.type = &t, def.period = 10000.0f, def.flags = zwep::kDesignate;
  mech3::weapon_calls().beam_stop = [](mech3::Handle) {};
  int frames = 0;
  {
    mech3::BeamWeapon w(&def, nullptr, &m);
    w.bin->amount -= 1;  // the trigger's
    w.firing = true;
    const float dt = static_cast<float>(1.0 / fps);
    for (frames = 1; frames < 10 * fps; ++frames) {
      w.update(dt);
      if (!w.firing) break;
    }
  }
  mech3::weapon_calls().beam_stop = nullptr;
  zfix::options().bugs = true;
  return frames;
}

TEST(fix_tag_hold_and_beam_in_milliseconds) {
  const int fps[3] = {30, 60, 144};
  const int exe_hold[3] = {30, 61, 145}, exe_beam[3] = {32, 62, 146};
  for (int i = 0; i < 3; ++i) {
    EXPECT(tag_hold_frames(fps[i], false) == exe_hold[i]);
    EXPECT(tag_beam_frames(fps[i], false) == exe_beam[i]);
    EXPECT(tag_hold_frames(fps[i], true) == fps[i]);  // one second
    const int beam = tag_beam_frames(fps[i], true);
    // 1.067 s (the exe's at 30 fps) to within a frame: ~67 ms of slack at any rate
    EXPECT(std::fabs(beam * 1000.0 / fps[i] - 1066.7) < 1000.0 / fps[i]);
    EXPECT(std::fabs((beam - fps[i]) * 1000.0 / fps[i] - 66.7) < 1000.0 / fps[i]);
  }
}

// The strike homes on the marked mech: fixed, the missiles' lock is the
// unit's live position and velocity; the exe's, the point it stood on.
TEST(fix_tag_strike_homes_on_the_unit) {
  static const zwep::Vec3f3 unit_at{5, 0, -300}, unit_vel{0, 0, -10};
  static const zwep::Vec3f3* seen_at;
  static const zwep::Vec3f3* seen_vel;
  mech3::WeaponCalls& c = mech3::weapon_calls();
  c.strike_target = [](mech3::Handle, zwep::LockRef* out) {
    out->entity = reinterpret_cast<mech3::Handle>(0x1234), out->at = &unit_at, out->velocity = &unit_vel;
    return true;
  };
  c.spawn = [](zwep::WeaponDef*, mech3::Handle, mech3::Handle, const mech3::Vec3f*, const mech3::Vec3f*,
               const mech3::Vec3f*, int32_t, int32_t, bool) -> mech3::Handle {
    const zwep::LockRef* l = zwep::fire_state().lock_target;
    seen_at = l ? l->at : nullptr, seen_vel = l ? l->velocity : nullptr;
    return nullptr;
  };
  mech3::WeaponType t;
  zwep::WeaponDef missile{};
  missile.type = &t;
  t.strike_weapon = &missile;
  const zwep::Vec3f3 dir{0, 0, -1}, at{0, 0, -300};
  for (int fix = 0; fix < 2; ++fix) {
    zfix::options().bugs = fix != 0;
    seen_at = seen_vel = nullptr;
    t.strike(nullptr, dir, at, reinterpret_cast<mech3::Handle>(0x99));
    if (fix) EXPECT(seen_at == &unit_at && seen_vel == &unit_vel);
    else EXPECT(seen_at == &t.strike_point && seen_vel == nullptr);
  }
  zfix::options().bugs = true;
  c.strike_target = nullptr, c.spawn = nullptr;
}

// The brain's home points (FIX (bug), docs/fixes.md, ai.md 17.2): a fight's
// part going before the own order's part stacked on it. In the exe the own
// part keeps the fight's freed home as its under and puts it back on +0xe4
// (the leash's home) when it goes; here it hands over the fight's own under.
// Run under ASan the old way read freed memory at the end.
namespace {
struct HomeControls : mech3::ai::BrainControls {
  zgame::Handle vehicle() override { return 1; }
  void stop() override {}
  void smooth(float) override {}
  bool hit_lately() override { return false; }
  void fire(bool) override {}
  bool reversing() override { return false; }
  float throttle_now() override { return 0; }
  void set_throttle(float) override {}
  void set_turn(float) override {}
  void set_jump(float) override {}
  void set_jet_forward(float) override {}
  float turn_goal() override { return 0; }
  void aim_started() override {}
};
struct HomeHooks : mech3::ai::BrainHooks {
  float at[3] = {};
  uint32_t unported(const char*, std::initializer_list<zgame::Arg>) override { return 0; }
  void position(zgame::Handle, float out[3]) override { std::memcpy(out, at, 12); }
};
}  // namespace

TEST(fix_ai_home_points_out_of_order) {
  using namespace mech3::ai;
  HomeHooks hooks;
  set_brain_hooks(&hooks);
  {
    BuiltinNet net(7, 0);
    AiParams rec;
    Brain b(&net, rec);
    HomeControls c;
    b.controls = &c;
    static const float base[3] = {1, 2, 3};
    b.xe4 = base;
    VehicleNet vn(0);
    Order fight, own;
    fight.owner = own.owner = &b;
    // The fight's part (its home at 10), then the own order's (at 20) on it.
    hooks.at[0] = 10;
    auto* a = new AttackPart(&fight, nullptr);
    hooks.at[0] = 20;
    auto* v = new VehiclePart(&own, &vn);
    EXPECT(b.xe4 == v->home && v->prev == a->home && a->prev == base);
    // The fight's goes first: the top stays, the own part's under is now the base.
    delete a;
    EXPECT(b.xe4 == v->home && v->prev == base);
    // The own part goes: +0xe4 is the base again, not the freed home (the
    // leash reads it: engage_checks; ASan reports the exe's way here).
    delete v;
    const volatile float leash_x = b.xe4 ? b.xe4[0] : 0.0f;
    EXPECT(b.xe4 == base && leash_x == 1.0f && b.homes.empty());
    // In stack order it is the exe's: each part puts back the +0xe4 it found.
    hooks.at[0] = 30;
    auto* a2 = new AttackPart(&fight, nullptr);
    auto* v2 = new VehiclePart(&own, &vn);
    delete v2;
    EXPECT(b.xe4 == a2->home);
    delete a2;
    EXPECT(b.xe4 == base);
  }
  set_brain_hooks(nullptr);
}

// The station keeping's run (FIX (bug), docs/fixes.md, ai.md 17.2): with
// +0x78 below the +0x24 its state took (0.9 of an older +0x78), a vehicle
// between the two sends phase 0 to a phase that hands it straight back,
// for ever in the exe. Here the run stops.
namespace {
struct StationHooks : HomeHooks {
  bool entity_active(zgame::Handle) override { return true; }
  zgame::Handle bearer(zgame::Handle v) override { return v; }
  void position(zgame::Handle v, float out[3]) override {
    out[0] = v == 2 ? 7.0f : 0.0f, out[1] = 0, out[2] = 0;  // the other 7 m ahead (+x)
  }
  void forward(zgame::Handle, float* x, float* z) override { *x = 1, *z = 0; }
  void velocity(zgame::Handle, float out[3]) override { out[0] = out[1] = out[2] = 0; }
  float move_speed(zgame::Handle) override { return 0; }
};
}  // namespace

TEST(fix_ai_station_keeping_no_endless_phases) {
  using namespace mech3::ai;
  StationHooks hooks;
  set_brain_hooks(&hooks);
  {
    BuiltinNet net(7, 0);
    AiParams rec;
    Brain b(&net, rec);
    HomeControls c;
    b.controls = &c;
    b.params.put<float>(0x74, 1600.0f);
    b.params.put<float>(0x78, 10.0f);
    VehicleNet vn(2);
    Order& o = b.order;
    o.net.reset(vn.make_part(o));
    o.state.reset(vn.own_order()->make_state(o));
    auto* st = dynamic_cast<StationState*>(o.state.get());
    EXPECT(st && st->close == 9.0f);
    b.params.put<float>(0x78, 5.0f);  // now below the 9 it took
    st->run(1.0f / 30);  // the exe: phase 0 -> 1 -> 0 -> 1 ... for ever
    EXPECT(b.order.state && st->phase <= 3);
    b.order.state.reset();
    b.order.net.reset();
  }
  set_brain_hooks(nullptr);
}

// The pocket commander's release (FIX (bug), fixes.md, ai.md 17.6a): when the
// player's mech goes while the mode is up, the exe's 0x0046ad80 deletes the
// mode and answers yes, and the mech (whose controls still are the mode)
// deletes it again. Here the mode is left, answers yes, and is deleted once,
// by the mech's side; the player's controls stay and are his again.
// FIX (bug), pocket_frame: the exe reads the HUD's +0x70 (the map up)
// through null when the player is not the view's mech; here no HUD is no map.
TEST(fix_pocket_frame_without_a_hud) {
  using namespace mech3;
  pocket_calls() = PocketCalls{};
  pocket_calls().view_state = [](PocketControls&, int32_t) {};
  frame_calls() = FrameCalls{};
  static int asked;
  asked = 0;
  frame_calls().view_hud = [](Mech&) -> zgame::Handle { return 0; };  // not the view's
  frame_calls().hud_map_up = [](zgame::Handle) { return ++asked, true; };
  const ControlsCalls saved_controls = controls_calls();
  controls_calls().command = [](int) -> uint32_t { return 0; };  // the mode's keys: no zInput map here
  controls_calls().pilot_tick = [](MechControls&) {};
  Mech m;
  auto* prev = new PlayerControls();
  prev->construct(1);
  prev->owner = &m;
  m.controls = prev;
  PlayerControls*& inst = player_controls_instance();
  PlayerControls* const saved = inst;
  inst = prev;
  MechControls* c = lance_dispatch(prev, 2);
  PocketControls* p = c ? c->pocket() : nullptr;
  EXPECT(p && p->panel);
  if (p) {
    p->update(1.0f / 30);
    EXPECT(asked == 0 && p->x1b8 == 0);
    c = p->leave(true);
  }
  EXPECT(c == prev && inst == prev);
  m.controls = nullptr;
  inst = saved;
  delete prev;
  frame_calls() = FrameCalls{};
  pocket_calls() = PocketCalls{};
  controls_calls() = saved_controls;
}

TEST(fix_pocket_commander_freed_once) {
  using namespace mech3;
  pocket_calls() = PocketCalls{};
  pocket_calls().view_state = [](PocketControls&, int32_t) {};  // no camera on a bare mech
  Mech m;
  auto* prev = new PlayerControls();
  prev->construct(1);
  prev->owner = &m;
  m.controls = prev;
  PlayerControls*& inst = player_controls_instance();
  PlayerControls* const saved = inst;
  inst = prev;
  MechControls* c = lance_dispatch(prev, 2);
  PocketControls* p = c ? c->pocket() : nullptr;
  EXPECT(p && inst == p && pocket_globals().active == p);
  m.controls = c;
  const bool yes = p && p->release();  // 0x0041520b: then the mech deletes its controls
  EXPECT(yes && inst == prev && prev->owner == &m && !pocket_globals().active);
  if (yes) delete p;  // once (ASan: the exe's way frees it twice)
  m.controls = nullptr;
  inst = saved;
  delete prev;
  pocket_calls() = PocketCalls{};
}

// vehicles.md, the motion block: a mode with chas_pitch and no chas_roll
// keeps its pitch with zfix bugs on (the exe zeroes it); without platform
// or collision lists (and no model nodes) there are no supports and no
// collision points, where the exe reads through null.
TEST(fix_vehicle_motion_chas_roll_and_missing_lists) {
  using namespace mech3;
  // ("chas_pitch" (1 2 -3)) as zReader nodes: a list of the key and its values.
  zrdr::Node* values = zrdr::new_nodes(zrdr::Float, 4);
  values[0].type = zrdr::Int, values[0].i = 4;
  values[1].f = 1.0f, values[2].f = 2.0f, values[3].f = -3.0f;
  zrdr::Node* items = zrdr::new_nodes(zrdr::Int, 3);
  items[0].i = 3;
  items[1].type = zrdr::String, items[1].s = const_cast<char*>("chas_pitch");
  items[2].type = zrdr::List, items[2].list = values;
  zrdr::Node mode{};
  mode.type = zrdr::List, mode.list = items;
  const bool bugs = zfix::options().bugs;
  for (bool on : {false, true}) {
    zfix::options().bugs = on;
    VehicleControls c;
    int32_t count = 7;
    vehicle_motion_read(c, nullptr, &mode, "veh", "type", &count);
    EXPECT(c.motion.support_count == 0 && count == 0);
    if (on) {
      EXPECT(c.motion.chas_pitch.x == 1.0f && c.motion.chas_pitch.y == 2.0f && c.motion.chas_pitch.z == 3.0f);
      EXPECT(c.motion.chas_roll.x == 0 && c.motion.chas_roll.z == 0);
    } else {
      EXPECT(c.motion.chas_pitch.x == 0 && c.motion.chas_pitch.z == 0);  // as the exe
    }
  }
  zfix::options().bugs = bugs;
  items[1].s = nullptr;  // not the reader's to free
  zrdr::free_contents(&mode);
}

TEST(fix_vehicle_gun_clamp) {
  using namespace mech3;
  TurretLimits lim(-0.2588f, 0.5f);
  const float d[3] = {0.0f, 0.64278764f, -0.76604444f};  // 40 degrees up, ahead
  const bool bugs = zfix::options().bugs;
  for (bool on : {false, true}) {
    zfix::options().bugs = on;
    float out[3];
    EXPECT(!lim.clamp(d, out));
    const double pitch = std::atan2(double{out[1]}, std::hypot(double{out[0]}, double{out[2]}));
    if (on) EXPECT(std::fabs(pitch - 0.5) < 1e-5);
    else EXPECT(std::fabs(pitch - 0.5) > 0.05);  // as the exe: about 24.6 degrees
  }
  zfix::options().bugs = bugs;
}

TEST(fix_vehicle_collision_fire_floor) {
  using namespace mech3;
  // A sweep crossing two floors of soil 5 (fire): the exe adds the first to
  // the segment's own start and the second through null; here the flag only.
  struct Hooks : VehBasicHooks {
    zmod::Material fire{};
    void segments(float*, int32_t, zcls::SegmentHits* out) override {
      fire.soil = 5;
      out[0].count = 2;
      for (int k = 0; k < 2; ++k) {
        out[0].hit[k] = zcls::SegmentHit{};
        out[0].hit[k].normal[1] = 1.0f;
        out[0].hit[k].material = &fire;
      }
    }
    void node_set_intersect(Handle, int32_t) override {}
    Handle hit_object(Handle) override { return nullptr; }
  } hooks;
  set_veh_basic_hooks(&hooks);
  BasicVehicle v;
  VehicleControls c;
  v.controls = &c;
  float segs[6] = {1, 2, 3, 4, 5, 6};
  EXPECT(vehicle_collision_query(v, segs, 2));
  EXPECT(c.x2c == 1 && c.walls.empty() && c.ceilings.empty() && c.objects.empty());
  EXPECT(segs[0] == 1.0f);
  set_veh_basic_hooks(nullptr);
}

// W57 (vehicles.md, High frame rates): a support on the ground takes the
// ground's step over the frame as its speed (0x004a2820). The same 0.2 m rise
// at 144 fps launched a vehicle 4.8 times as hard as at 30; now as at 30.
TEST(fix_vehicle_support_rise_per_second) {
  using namespace mech3;
  struct Hooks : VehBasicHooks {
    float dt = 1.0f / 30.0f;
    float frame_dt() override { return dt; }
    float frame_inv_dt() override { return 1.0f / dt; }
  } hooks;
  set_veh_basic_hooks(&hooks);
  auto rise = [&](float dt) {
    hooks.dt = dt;
    float y = 0.0f, vs = 0.0f;
    int32_t air = 0;
    vehicle_support_step(&y, &vs, 0.2f, &air, 0.0f, 0.0f);
    return vs;
  };
  const bool was = zfix::options().per_second;
  zfix::options().per_second = true;
  const float at30 = rise(1.0f / 30.0f), at144 = rise(1.0f / 144.0f), at20 = rise(1.0f / 20.0f);
  EXPECT(std::fabs(at30 - 6.0f) < 1e-4f && std::fabs(at144 - at30) < 1e-4f);
  EXPECT(std::fabs(at20 - 4.0f) < 1e-4f);  // a slow frame is the exe's
  zfix::options().per_second = false;
  EXPECT(std::fabs(rise(1.0f / 144.0f) - 28.8f) < 1e-3f);  // the exe's
  zfix::options().per_second = was;
  set_veh_basic_hooks(nullptr);
}

// 0x0045a000 after a fourth base (FieldBases::add's fix): only the three
// slots are read; the extra base is inactive.
TEST(fix_field_bases_alive_reads_three_slots) {
  mech3::FieldBases r;
  mech3::FieldBase b[3];
  for (int i = 0; i < 3; ++i) r.bases[i] = &b[i];
  b[1].active = false;
  r.count = 4;
  EXPECT(r.alive() == 2);
}

// The turrets' turns (0x004a70d0, FIX (B)): by game time the pick takes as
// many turns in two seconds at 60 fps as at 30 (a 1/30 s step each, the ids
// in order); the exe's frame count takes twice as many at 60.
TEST(fix_turret_turns_by_time) {
  using namespace mech3::tur;
  struct Hooks : TurretHooks {
    uint32_t frame = 0;
    float now = 0;
    uint32_t frame_number() override { return frame; }
    float clock() override { return now; }
  } hooks;
  set_turret_hooks(&hooks);
  auto turns = [&](bool per_second, int fps, std::vector<int>* by_id) {
    zfix::options().per_second = per_second;
    turret_count() = 0;
    turret_turn() = 0;
    std::vector<Turret*> t;
    for (int i = 0; i < 14; ++i) t.push_back(new Turret(static_cast<uint32_t>(0x100 + i), 0, 0, nullptr));
    hooks.frame = 0;
    hooks.now = 1.0f;
    int n = 0;
    by_id->assign(t.size(), 0);
    for (int f = 0; f < 2 * fps; ++f) {
      ++hooks.frame;
      hooks.now += 1.0f / static_cast<float>(fps);
      for (size_t i = 0; i < t.size(); ++i) {
        const int32_t before = turret_turn();
        t[i]->pick(0);
        if (turret_turn() != before) ++n, ++(*by_id)[i];
      }
    }
    for (Turret* x : t) delete x;
    return n;
  };
  std::vector<int> a, b, c;
  const int at30 = turns(true, 30, &a), at60 = turns(true, 60, &b), exe60 = turns(false, 60, &c);
  EXPECT(at30 >= 59 && at30 <= 61);
  EXPECT(at60 == at30);
  EXPECT(exe60 == 2 * turns(false, 30, &c));
  for (int k : b) EXPECT(k >= 4 && k <= 5);  // 60 steps over 14 turrets, each in turn
  zfix::options().per_second = true;
  set_turret_hooks(nullptr);
}

// FIX (A), playtest W9: the lab's skin swap (0x0044fd90) locks and unlocks
// the 3D inside the screen's own lock. The port's layer goes over the frame
// at the unlock, so the inner unlock ending the lock lost the 2D drawn after
// it: a frame without the lab's screen. The outer lock now stays.
TEST(fix_lock_inside_a_lock_keeps_the_outer) {
  struct Frame {
    uint16_t px[2] = {0, 0};
    int unlocks = 0;
    uint16_t shown[2] = {};  // the layer as the last unlock handed it over
  } f;
  const zvid::SurfaceHooks saved_hooks = zvid::surface_hooks();
  const zvid::Surface saved_back = zvid::surfaces().back;
  const bool saved_fix = zfix::options().nested_lock;
  const zrndr::SoftTarget saved_soft = zrndr::soft_target();
  const zrndr::FinishTarget saved_finish = zrndr::finish_target();
  const zrndr::Target2D saved_2d = zrndr::target2d();
  const zvid::TextTarget saved_text = zvid::text_target();
  zvid::surface_hooks() = {[](void* c, zvid::Surface* s) {
                             auto* fr = static_cast<Frame*>(c);
                             s->width = 2, s->height = 1, s->pitch = 4, s->pixels = fr->px;
                             return 0;
                           },
                           [](void* c, zvid::Surface*) {
                             auto* fr = static_cast<Frame*>(c);
                             ++fr->unlocks, fr->shown[0] = fr->px[0], fr->shown[1] = fr->px[1];
                             return 0;
                           },
                           &f};
  for (int fix = 0; fix < 2; ++fix) {
    zfix::options().nested_lock = fix != 0;
    f = Frame{};
    zvid::surfaces().back = zvid::Surface{};
    zvid::lock_3d();      // the screen's draw
    f.px[0] = 0x1234;     // 2D before the click
    zvid::lock_3d();      // the skin swap's pair
    zvid::unlock_3d();
    f.px[1] = 0x5678;     // 2D after it
    zvid::unlock_3d();    // the screen's unlock
    if (fix) {
      EXPECT(f.unlocks == 1 && f.shown[0] == 0x1234 && f.shown[1] == 0x5678);
    } else {  // the original: the inner unlock ends the lock, the outer is a no-op
      EXPECT(f.unlocks == 1 && f.shown[0] == 0x1234 && f.shown[1] == 0);
    }
    EXPECT(zvid::surfaces().back.locked == 0);
  }
  zfix::options().nested_lock = saved_fix;
  zrndr::soft_target() = saved_soft, zrndr::finish_target() = saved_finish, zrndr::target2d() = saved_2d;
  zvid::text_target() = saved_text;
  zvid::surfaces().back = saved_back;
  zvid::surface_hooks() = saved_hooks;
}

// FIX (bug), AddressSanitizer: the waypoint arrow (one zTwoD image for the
// whole run) kept a pointer to the arrow image the last waynet's end
// released, and the next mission's first edge arrow read its size
// (hud_arrow reads the arrow's image before it sets this one). The arrow now
// lets go of it with the images; the original path keeps it.
TEST(fix_waynet_arrow_lets_go_of_released_images) {
  using namespace mech3;
  struct Hooks : WaynetHooks {
    int released = 0;
    void image_path(const char*) override {}
    Handle icon_make(const char*) override { return nullptr; }
    void icon_free(Handle) override {}
    Handle image_get(const char*) override { return new zimg::Texture(); }
    void image_release(Handle image) override { ++released, delete static_cast<zimg::Texture*>(image); }
  } hooks;
  const bool saved_bugs = zfix::options().bugs;
  set_waynet_hooks(&hooks);
  for (int fix = 0; fix < 2; ++fix) {
    zfix::options().bugs = fix != 0;
    waynet_icons_ref();
    zimg::Texture* shown = new zimg::Texture();  // the arrow's image this mission (one the hooks do not free)
    shown->width = 24, shown->height = 16;
    hooks.arrow_image(shown);
    int32_t w = 0, h = 0;
    hooks.arrow_size(&w, &h);
    EXPECT(w == 24 && h == 16);
    hooks.released = 0;
    waynet_icons_unref();  // the mission's last waynet: the images go
    EXPECT(hooks.released == (fix ? 8 : 4));
    w = h = -1;
    if (fix) {
      hooks.arrow_size(&w, &h);  // no image: 0 by 0
      EXPECT(w == 0 && h == 0);
    }
    hooks.arrow_image(nullptr);
    delete shown;
  }
  set_waynet_hooks(nullptr);
  zfix::options().bugs = saved_bugs;
}

// Artemis IV's double tracking (FIX (bug); weapons.md, Homing): a shot fired
// locked with FCS carries 2 in its +0x5c0, which the exe's homing never reads
// (it reads the global, reset to 1 by the spawn). The same five-missile salvo
// at a target moving across, plain and FCS-locked: with the fix the FCS
// missiles turn twice as fast and pass closer; without it, alike.
TEST(fix_artemis_fcs_doubles_the_turn_rate) {
  zwep::WeaponDef def{};
  def.turn_rate = 0.6f, def.lock_on = 1.0f, def.turn_suspend_time = 0.0f;
  const float dt = 1.0f / 30.0f;
  zwep::hit_state().dt = dt;
  zwep::fire_state().track_scale[0] = zwep::fire_state().track_scale[1] = 1.0f;  // as spawn leaves it
  auto salvo = [&](float track, float* first_turn) {
    float miss = 0.0f;
    for (int k = 0; k < 5; ++k) {
      zwep::Vec3f3 target{60.0f, 0.0f, 300.0f};
      const zwep::Vec3f3 target_v{-15.0f, 0.0f, 0.0f};
      zwep::Projectile p{};
      p.position = {0.4f * static_cast<float>(k - 2), 0.0f, 0.0f};
      p.direction = {0.0f, 0.0f, 1.0f};
      p.speed = p.max_speed = 100.0f;
      p.target_at = &target, p.target_velocity = &target_v;
      p.track_scale[0] = track;
      p.terrain_follow = 0;  // (the FCS climb's ground probe left out)
      float best = 1e9f;
      for (int i = 0; i < 150; ++i) {
        const zwep::Vec3f3 before = p.direction;
        zwep::home(&def, &p);
        if (i == 0 && k == 0 && first_turn)
          *first_turn = std::acos(std::min(1.0f, before.x * p.direction.x + before.y * p.direction.y + before.z * p.direction.z));
        p.age += dt;
        p.position = {p.position.x + p.velocity.x * dt, p.position.y + p.velocity.y * dt, p.position.z + p.velocity.z * dt};
        target = {target.x + target_v.x * dt, target.y, target.z + target_v.z * dt};
        const float dx = p.position.x - target.x, dz = p.position.z - target.z;
        best = std::min(best, std::sqrt(dx * dx + dz * dz));
      }
      miss += best / 5.0f;
    }
    return miss;
  };
  const bool bugs = zfix::options().bugs;
  float turn_plain = 0, turn_fcs = 0;
  zfix::options().bugs = false;  // the original: the shot's 2 never read
  const float off_plain = salvo(1.0f, nullptr), off_fcs = salvo(2.0f, nullptr);
  zfix::options().bugs = true;
  const float on_plain = salvo(1.0f, &turn_plain), on_fcs = salvo(2.0f, &turn_fcs);
  zfix::options().bugs = bugs;
  std::printf("  salvo's mean closest pass: original %.2f / %.2f m, fixed %.2f (plain) / %.2f m (FCS); first turn %.4f / %.4f\n",
              off_plain, off_fcs, on_plain, on_fcs, turn_plain, turn_fcs);
  EXPECT(off_plain == off_fcs);
  EXPECT(on_plain == off_plain);
  EXPECT(on_fcs < on_plain);
  EXPECT(std::fabs(turn_fcs - 2.0f * turn_plain) < 1e-4f);
}

// The arm weapons' cone (Weapon::aim 0x004b36e0, cos 0.89, about 27 degrees;
// FIX (bug), W48's aim): wider than 4:3 the reticle reaches past it, and the
// beams snapped to the arm's line at the old 4:3 edge. Fixed, the cone grows
// with the view (tan of the half angle by the view's width over 4:3's). At
// 16:9 (width 4/3) a target 32 degrees off the arm gets the beam with bugs on
// and the arm's line with them off; at 4:3 nothing changes.
TEST(fix_aim_cone_widens_with_the_view) {
  static float width = 1.0f;
  static const mech3::Vec3f axis{0, 0, 1};  // the part's facing is -axis: (0, 0, -1)
  mech3::WeaponCalls& c = mech3::weapon_calls();
  const mech3::WeaponCalls kept = c;
  c.part_by_id = [](mech3::Handle, int32_t) { return reinterpret_cast<mech3::Handle>(0x2001); };
  c.part_node = [](mech3::Handle p) { return p; };
  c.node_axis = [](mech3::Handle) { return &axis; };
  c.node_id = [](mech3::Handle) { return 5; };  // an arm, not the centre torso (1)
  c.node_position = [](mech3::Handle, mech3::Vec3f*) { return 0; };
  c.view_width = [] { return width; };
  c.beam_stop = [](mech3::Handle) {};
  const mech3::Handle owner = reinterpret_cast<mech3::Handle>(0x1000);
  const mech3::Handle player_was = mech3::weapon_world().player;
  mech3::weapon_world().player = owner;
  static mech3::WeaponType t;
  static mech3::WeaponManager m(nullptr);
  m.owner = owner;
  zwep::WeaponDef def{};
  def.type = &t;
  mech3::Firepoint fp{};
  fp.model_part = 3;
  // Whether a target `deg` degrees off the arm gets the beam (else the arm's line).
  auto follows = [&](float deg, float w, bool bugs) {
    zfix::options().bugs = bugs;
    width = w;
    mech3::BeamWeapon b(&def, nullptr, &m);
    b.part = reinterpret_cast<mech3::Handle>(0x2000), b.firepoint = &fp;
    const double r = deg * 3.14159265358979 / 180.0;
    mech3::Vec3f at{static_cast<float>(100.0 * std::sin(r)), 0.0f, static_cast<float>(-100.0 * std::cos(r))}, mz{}, dir{};
    EXPECT(b.aim(&at, &mz, &dir, nullptr));
    return dir.x > 0.1f;
  };
  const float wide = 4.0f / 3.0f;  // 16:9's
  EXPECT(follows(20, 1, true) && follows(20, 1, false) && follows(20, wide, true) && follows(20, wide, false));
  EXPECT(!follows(32, 1, true) && !follows(32, 1, false));  // 4:3: the exe's snap
  EXPECT(follows(32, wide, true) && !follows(32, wide, false));
  EXPECT(!follows(40, wide, true));  // past 16:9's cone (about 34 degrees)
  zfix::options().bugs = true;
  mech3::weapon_world().player = player_was;
  c = kept;
}

// W50 (Owen, c1m2 on the Mac): zMath's e^-x table (0x00523f70) steps by
// 1/51, so a per-second easing reads 1 on short frames: a rate of 3 at
// 6 ms (166 fps) never eased at all. exp_neg_dt gives the 30 fps frame's
// table value raised to the frame's share: at 1/30 s the table bit for bit
// (play as v1.0 and the original at 30 fps), at any other frame time the
// same rate per second; without per_second, the table. The vehicles keep
// the table at any frame time (W53: off its values near 30 fps c1m2's APCs
// hopped): a vehicle's turn damped to 0 (0x004a2a00) at 6 ms, undamped.
TEST(fix_exp_neg_eases_on_fast_frames) {
  const bool was = zfix::options().per_second;
  zfix::options().per_second = true;
  const float ref = 1.0f / zfix::kReferenceFps;
  // The callers' rates: the turrets' 3 and 8, the arms' recoil 4 and 6 and
  // aim (a few rates); the vehicles' 30, 8, 1 and 0.5 as well, for the function.
  int same = 0, rates = 0;
  for (const float c : {3.0f, 8.0f, 4.0f, 6.0f, 10.0f, 20.0f, 30.0f, 1.0f, 0.5f}) {
    ++rates;
    same += zmth::exp_neg_dt(c * ref, ref) == zmth::exp_neg(c * ref);
    // 6 ms frames: the 30 fps frame's value per second, within float.
    const float k6 = zmth::exp_neg_dt(c * 0.006f, 0.006f);
    const double per_s6 = std::pow(double{k6}, 1.0 / 0.006), per_s30 = std::pow(double{zmth::exp_neg(c * ref)}, 30.0);
    const float k50 = zmth::exp_neg_dt(c * 0.05f, 0.05f);
    std::printf("  rate %4.1f: 30 fps %.6f, 6 ms %.6f (per s %.5f against %.5f), 50 ms %.6f\n", c, zmth::exp_neg(c * ref), k6,
                per_s6, per_s30, k50);
    EXPECT(std::fabs(per_s6 - per_s30) <= 2e-3 * per_s30 + 1e-9);
    EXPECT(k50 >= 0.0f && k50 <= 1.0f && std::fabs(k50 - std::pow(zmth::exp_neg(c * ref), 1.5f)) < 1e-6f);
  }
  EXPECT(same == rates);
  EXPECT(zmth::exp_neg_dt(3.0f * 0.006f, 0.006f) < 1.0f);    // the turret turns at 166 fps (W50)
  EXPECT(zmth::exp_neg_dt(0.5f * ref, ref) == 1.0f);          // the original's 30 fps: this one undamped
  EXPECT(zmth::exp_neg_dt(200.0f * 0.05f, 0.05f) == 0.0f);    // past the table's 5: 0
  EXPECT(zmth::exp_neg_dt(0.3f, 0.0f) == zmth::exp_neg(0.3f));  // no frame time: the table
  mech3::VehicleControls c;
  mech3::VehicleMotion m;
  m.turn_damping = 3.0f;
  c.turn = 0.0f, c.x50.y = 1.0f;
  c.turn_step(m, 0.006f);
  EXPECT(c.x50.y == 1.0f);  // the table, as the original: 3 * 0.006 reads 1
  zfix::options().per_second = false;
  EXPECT(zmth::exp_neg_dt(3.0f * 0.006f, 0.006f) == 1.0f);  // the exe's table[0]: no easing
  c.x50.y = 1.0f;
  c.turn_step(m, 0.006f);
  EXPECT(c.x50.y == 1.0f);
  zfix::options().per_second = was;
}

// W56 (ours, FIX (bug) behind bugs and throttle_reverse; Reddit v1.0, a
// T.16000M: "I can accelerate but can't go in reverse with the slider"): the
// stick's throttle lever over the whole range, through PlayerControls' frame
// with a fake stick: full reverse at the bottom, stop at 37.5% of the travel
// with 5% either side, full ahead at the top; a slider bound the popup's way;
// a gamepad's triggers and the switch off as before; a lever left at its end
// does not back the mech off at the start.
namespace {
platform::JoyState g_lever;
zin::JoyAxis g_lever_axes[6];
bool g_lever_pad = false;
// The throttle target after `frames` at 30 fps with the lever's Z (or the
// slider) at `z`, after a first frame at `first`.
float lever_target(const mech3::AxisBinding& b, int32_t first, int32_t z, bool slider = false) {
  static mech3::Mech m;
  static mech3::PlayerControls c;
  m = mech3::Mech{};
  c = mech3::PlayerControls{};
  c.owner = &m;
  m.controls = &c;
  for (zin::JoyAxis& a : g_lever_axes) a = zin::JoyAxis{}, a.min = -1000, a.max = 1000, a.scale = 0.001f, a.centre = 0;
  const mech3::AxisTable kept = mech3::axis_table();
  mech3::axis_table().axes[0] = b;
  mech3::ControlsCalls& k = mech3::controls_calls();
  k.command = [](int) { return 0u; };
  k.joy_present = [] { return true; };
  k.joy_gamepad = [] { return g_lever_pad; };
  k.joy_state = [] { return static_cast<const void*>(&g_lever); };
  k.joy_config = [] { return static_cast<const void*>(g_lever_axes); };
  auto set = [slider](int32_t v) {
    g_lever = platform::JoyState{};
    if (slider) g_lever.slider[0] = v;
    else g_lever.z = v;
  };
  set(first);
  c.update(1.0f / 30);
  set(z);
  for (int i = 0; i < 3; ++i) c.update(1.0f / 30);
  k = mech3::ControlsCalls{};
  mech3::axis_table() = kept;
  return c.throttle_target;
}
}  // namespace

TEST(fix_throttle_lever_reaches_reverse) {
  const mech3::AxisBinding z{3, -0.5f, 0.5f, 0.05f};  // the defaults' throttle (0x00410950): Z, the top full
  auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
  auto at = [](float u) { return static_cast<int32_t>(std::lround(1000.0f - 2000.0f * u)); };  // u up the travel
  const float lo = mech3::kLeverStop - mech3::kLeverStopZone, hi = mech3::kLeverStop + mech3::kLeverStopZone;
  const float top = lever_target(z, 0, -1000), bottom = lever_target(z, 0, 1000);
  const float stop = lever_target(z, 0, at(mech3::kLeverStop));
  const float in_lo = lever_target(z, 0, at(lo + 0.01f)), in_hi = lever_target(z, 0, at(hi - 0.01f));
  const float past = lever_target(z, 0, at(hi + 0.1f)), under = lever_target(z, 0, at(lo - 0.1f));
  std::printf("  lever: top %.3f bottom %.3f stop %.3f zone %.3f %.3f; past it %.3f, under %.3f\n", top, bottom, stop,
              in_lo, in_hi, past, under);
  EXPECT(near(top, 1.0f) && near(bottom, -1.0f));
  EXPECT(stop == 0.0f && in_lo == 0.0f && in_hi == 0.0f);
  EXPECT(near(past, 0.1f / (1.0f - hi)) && near(under, -0.1f / lo));
  // A kph the same travel both ways: reverse runs at 0.6 (Gait 0x004181c0).
  EXPECT(std::fabs(0.6f / lo - 1.0f / (1.0f - hi)) < 0.15f * (1.0f / (1.0f - hi)));
  // A slider (DirectInput's 0..65535) bound as the popup binds the throttle:
  // -0.5 / 32767.5 and 0.5 + 0.5; its low end full ahead, as Z's.
  const mech3::AxisBinding s{7, -0.5f / 32767.5f, 1.0f, 0.0f};
  EXPECT(near(lever_target(s, 40000, 0, true), 1.0f) && near(lever_target(s, 40000, 65535, true), -1.0f));
  EXPECT(lever_target(s, 1000, static_cast<int32_t>(65535 * (1.0f - mech3::kLeverStop)), true) == 0.0f);
  // Left at the bottom from the start: not counted until it moves; left
  // ahead, it drives at once, as the original's (a centred stick walks).
  EXPECT(lever_target(z, 1000, 1000) == 0.0f);
  EXPECT(near(lever_target(z, -1000, -1000), 1.0f));
  // A gamepad's triggers, the switch off, the bug fixes off: as before (the
  // defaults' 0..1: the top full, the bottom stop).
  g_lever_pad = true;
  EXPECT(near(lever_target(z, 0, 1000), 0.0f) && near(lever_target(z, 0, 0), 0.5f));
  g_lever_pad = false;
  zfix::options().throttle_reverse = false;
  EXPECT(near(lever_target(z, 0, 1000), 0.0f) && near(lever_target(z, 0, -1000), 1.0f));
  zfix::options().throttle_reverse = true;
  zfix::options().bugs = false;
  EXPECT(near(lever_target(z, 0, 1000), 0.0f) && near(lever_target(z, 0, -1000), 1.0f));
  zfix::options().bugs = true;
}

// W59 (Owen's v1.0 crash, zmth::load+0x1d in c4b's last fight): an unbalanced
// matrix stack is refused and logged, never a write through a stray slot.
TEST(fix_matrix_stack_imbalance_is_refused_and_logged) {
  zmth::MatrixStack& st = zmth::matrix_stack();
  zmth::reset_stack();
  zmth::stack_faults() = {};
#if !defined(_WIN32)
  std::FILE* log = std::tmpfile();
  std::fflush(stderr);
  const int saved = dup(2);
  dup2(fileno(log), 2);
#endif
  // Under: a load and a pop with nothing pushed.
  const zmth::Mat34 m = {{1, 0, 0, 0, 1, 0, 0, 0, 1, 7, 8, 9}};
  zmth::load(m);
  zmth::pop();
  zmth::pop();
  EXPECT(st.top == 0 && st.slot[0] == nullptr);
  // Over: 40 pushes into 31 free slots; the 9 refused ones' pops come off first.
  static zmth::Mat34 bufs[40];
  for (zmth::Mat34& b : bufs) zmth::push(&b);
  EXPECT(st.top == zmth::kStackDepth - 1 && st.overflow == 9);
  zmth::load(m);  // into the last slot taken
  EXPECT(bufs[30].m[9] == 7.0f && bufs[31].m[9] == 0.0f);
  for (int i = 0; i < 9; ++i) zmth::pop();
  EXPECT(st.top == zmth::kStackDepth - 1 && st.overflow == 0);
  zmth::push_copy(&bufs[39]);  // full again: refused, not past the array
  EXPECT(st.overflow == 1 && bufs[39].m[9] == 0.0f);
  // The frame's start puts it back to empty.
  zmth::check_stack_at_frame();
  EXPECT(st.top == 0 && st.overflow == 0);
  zmth::check_stack_at_frame();  // balanced now: nothing
  const zmth::StackFaults f = zmth::stack_faults();
  EXPECT(f.empty == 1 && f.under_pop == 2 && f.over_push == 10 && f.frame == 1);
#if !defined(_WIN32)
  std::fflush(stderr);
  dup2(saved, 2);
  close(saved);
  std::string text;
  std::rewind(log);
  for (int c; (c = std::fgetc(log)) != EOF;) text += static_cast<char>(c);
  std::fclose(log);
  int lines = 0;
  for (size_t at = 0; (at = text.find("mw3: matrix stack: ", at)) != std::string::npos; ++at) ++lines;
  EXPECT(lines == 4);  // once each: empty, under, over, frame
  EXPECT(text.find("a push past the last slot, refused") != std::string::npos);
  EXPECT(text.find("from: ") != std::string::npos && text.find("pushed by: ") != std::string::npos);
#endif
  zmth::reset_stack();
  zmth::stack_faults() = {};
}
