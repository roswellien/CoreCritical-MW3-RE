#include "mech3/mech_control.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gamez/zinput/zinput.h"
#include "gamez/zutil/zfix.h"
#include "mech3/mech.h"
#include "mech3/mech_build.h"
#include "mech3/mech_parts.h"
#include "mech3/player_view.h"
#include "platform/platform.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
constexpr float kExpScale = 12102200.0f;  // 0x005bd5e8: 2^23 / ln 2
constexpr float kJumpRise = 7.0f;         // 0x005e2598
constexpr float kJumpFall = 9.0f;         // 0x005e259c, toward a zero target

// MSVCRT _ftol (0x005ac340): truncates.
int32_t ftol(float x) { return static_cast<int32_t>(x); }

}  // namespace

// exp(x) by writing x / ln 2 straight into a float's exponent (Schraudolph):
// within a few percent of the true value, and what the game uses.
float fast_exp(float x) {
  const int32_t bits = ftol(x * kExpScale) + 0x3f800000;
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

// Mech3.exe 1.2 @ 0x00426960
int MechControls::update(float dt) {
  if (owner->powered_down) {
    zero();
    return 1;
  }
  if (throttle_target - throttle != 0.0f) {
    // FIX (B): the snap's gap goes with the frame, as the keys' step does; else
    // below 12 ms that step is inside it and the easing never runs (fixes.md).
    if (std::fabs(throttle_target - throttle) < 0.009 * std::min(zfix::frame_steps(dt), 1.0f)) {
      throttle = throttle_target;
    } else {
      const float rate = std::fabs(throttle) <= std::fabs(throttle_target) ? owner->throttle_up
                                                                           : owner->throttle_down;
      const float k = fast_exp(-(rate * dt));
      throttle = (1.0f - k) * throttle_target + k * throttle;
    }
  }
  if (turn != turn_target) {
    float rate = owner->turn_accel;
    if (flags & 4) rate = rate + rate;
    const float target = turn_target;
    turn = (target - turn) * rate * dt + turn;
    if (std::fabs(target - turn) < 0.05f) turn = target;
  }
  if (jump != jump_target) {
    const float rate = jump_target == 0.0f ? kJumpFall : kJumpRise;
    const float target = jump_target;
    jump = (target - jump) * rate * dt + jump;
    if (std::fabs(target - jump) < 0.05f) jump = target;
  }
  return 1;
}

// Mech3.exe 1.2 @ 0x00426780. Leaving 0 or 1 puts the mouse back in the
// middle (and 1 the cockpit camera's look rate back to 6); 1 makes it 8 and,
// outside the cockpit, becomes 2; 2 and 3 with the controls' flag 4 become 0.
// The view's camera looks along the torso when the view is on this mech, and
// the torso holds its view while in 1.
void MechControls::set_view(int32_t v) {
  ControlsCalls& k = controls_calls();
  auto mouse_centre = [&] {
    if (k.mouse_set) k.mouse_set(0.0f, 0.0f);
    else zin::mouse_set(0.0f, 0.0f);
  };
  Mech* m = owner;
  if (!m) return;
  for (;;) {
    CameraMode* cockpit = k.mech_camera ? k.mech_camera(*m, 1) : nullptr;
    if (view == 0) {
      mouse_centre();
    } else if (view == 1) {
      mouse_centre();
      if (cockpit) cockpit->look_rate = 6.0f;
    }
    view = v;
    const bool viewed = k.view_target && k.view_target() == m;
    CameraMode* mode = k.view_mode ? k.view_mode() : nullptr;
    // The mode's vt+0x1c: itself for the mech cameras, null for the base.
    auto torso_look = [&] {
      if (auto* mc = dynamic_cast<MechCamera*>(mode)) mc->torso_look = 1;
    };
    if (v == 2 || v == 3) {
      if (flags & 4) {
        v = 0;
        continue;
      }
      m->torso->set_mode(1);
      if (viewed) torso_look();
    } else if (v == 0) {
      m->torso->set_mode(1);
      if (viewed) torso_look();
    } else if (v == 1) {
      m->torso->set_mode(1);
      if (cockpit) cockpit->look_rate = 8.0f;
      if (viewed) {
        if (mode && mode->id == 1) torso_look();
        else set_view(2);
      }
    }
    break;
  }
  m->torso->hold_view(view == 1);
}

// Mech3.exe 1.2 @ 0x00426b60
void MechControls::set_throttle(float v) {
  if (v > 0.0f) reverse = false;
  else if (v < 0.0f) reverse = true;
  throttle_target = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

// Mech3.exe 1.2 @ 0x00426c80
void MechControls::zero() {
  jj_strafe = jj_forward = twist = pitch = axis_30 = axis_2c = 0;
  throttle_target = throttle = turn_target = turn = jump_target = jump = 0;
}

namespace {
ControlsCalls g_calls;


// The commands the player's controls read (the CMD_ ids, docs/controls.md).
constexpr int kTurnLeft = 2, kTurnRight = 3, kPitchUp = 4, kPitchDown = 5, kTwistLeft = 8, kTwistRight = 9;
constexpr int kJetsUp = 0xc, kJetsForward = 0xd, kJetsBack = 0xe, kJetsLeft = 0xf, kJetsRight = 0x10;
constexpr int kJetsTurnLeft = 0x11, kJetsTurnRight = 0x12, kZoom = 0x14, kThrottleUp = 0x18;
constexpr int kThrottleDown = 0x19, kFire = 0x3d;
constexpr int kZoomSound = 0x17, kZoomSoundSlot = 23;  // mech +0x614
constexpr float kThrottleStep = 0.025f;    // 0x3ccccccd, a frame
constexpr float kThrottleReturn = -0.7f;   // 0x005e25b0, per second
constexpr double kThrottleZero = 0.05;     // 0x005bd648
constexpr float kInputRate = 6.0f;         // 0x005e25b4, per second
constexpr double kInputSnap = 0.05000000074505806;  // 0x005bd5f0
constexpr float kTwistKey = 1.5f;          // 0x3fc00000
constexpr float kMouseYaw = -0.26f, kMousePitch = -0.16f;  // 0x005e25a0, 0x005e25a4: per pixel a frame
constexpr float kMouseLook = 1.5f;         // 0x005bd634
constexpr float kLookReturn = 5.0f;        // 0x005e25ac

const platform::JoyState* joy() {
  return static_cast<const platform::JoyState*>(g_calls.joy_state ? g_calls.joy_state() : zin::joy_state());
}
const zin::MouseInfo* mouse() {
  return static_cast<const zin::MouseInfo*>(g_calls.mouse_info ? g_calls.mouse_info() : zin::mouse_info());
}
const zin::JoyAxis* joy_axes() {
  return static_cast<const zin::JoyAxis*>(g_calls.joy_config ? g_calls.joy_config() : zin::joy_config());
}
bool held(int cmd) { return ((g_calls.command ? g_calls.command(cmd) : zin::command(cmd)) & 3) != 0; }
void run_command(int cmd) {
  if (g_calls.run_command) g_calls.run_command(cmd);
  else zin::run_callback(cmd);
}

float clamp1(float v) { return v > 1.0f ? 1.0f : !(v >= -1.0f) ? -1.0f : v; }

// (target - v) * 6 dt + v, snapped within 0.05.
void approach(float& v, float target, float dt) {
  const float n = (target - v) * kInputRate * dt + v;
  v = n;
  if (std::fabs(target - n) < kInputSnap) v = target;
}

// Mech3.exe 1.2 @ 0x0042fff0: a source's motion this read (the stick's
// velocities, never filled; the mouse's scaled motion).
float source_delta(int32_t src, const platform::JoyState* j, const zin::MouseInfo* m) {
  if (src >= 1 && src <= 8) return j ? static_cast<float>(j->rest[src - 1]) : 0.0f;
  if (src == 9) return m->fdx;
  if (src == 10) return m->fdy;
  if (src == 11) return m->fdz;
  return 0.0f;
}
}  // namespace

// Mech3.exe 1.2 @ 0x0042fea0: a source's reading. The stick's axes are
// centred and scaled by zInput's setup (0x0051da90); 9..11 the mouse's
// position from the centre.
float source_value(int32_t src, const platform::JoyState* j, const zin::MouseInfo* m) {
  const zin::JoyAxis* a = joy_axes();
  if (src >= 1 && src <= 6) {
    if (!j) return 0.0f;
    const int32_t v[6] = {j->x, j->y, j->z, j->rx, j->ry, j->rz};
    return (static_cast<float>(v[src - 1]) + a[src - 1].centre) * a[src - 1].scale;
  }
  if (src == 7 || src == 8) return j ? static_cast<float>(j->slider[src - 7]) : 0.0f;
  if (src >= 9 && src <= 11) {
    if (!m) return 0.0f;
    return src == 9 ? m->fx : src == 10 ? m->fy : m->fz;
  }
  return 0.0f;
}


ControlsCalls& controls_calls() { return g_calls; }

// Mech3.exe 1.2 @ 0x00426650. Every call takes the joystick once more
// (0x0051b2c0, its use count), as the original does.
void MechControls::set_mode(uint32_t mode) {
  flags = mode;
  throttle_held = false;
  AxisTable& t = axis_table();
  if (t.ready) {
    if (mode & 4) {
      t.axes[0] = {3, -0.5f, 0.5f, 0.05f};
      t.ready = 0;
      t.set3(1, 6, -1.0f, 0.0f);
      t.axes[1].deadzone = 0.0f;
      t.ready = 0;
      t.set3(2, 2, 1.0f, 0.0f);
      t.axes[2].deadzone = 0.0f;
      t.ready = 0;
      t.set(3, 1, -1.0f, 0.0f, 0.0f);
      t.ready = 1;
    } else if (mode & 1) {
      t.axes[2] = {10, 1.0f, 0.0f, 0.0f};
      t.axes[1] = {9, 1.0f, 0.0f, 0.0f};
      t.axes[0].source = 0;
      t.axes[3].source = 0;
      t.ready = 1;
    }
  }
  if (g_calls.joy_use) g_calls.joy_use();
  else zin::joy_use();
}

// Mech3.exe 1.2 @ 0x004300e0: an axis source's name in the controls file.
// (It calls zInput's joystick setup 0x0051da90 first and drops the result.)
const char* axis_source_name(int32_t source) {
  switch (source) {
    case 1: case 9: return "X";
    case 2: case 10: return "Y";
    case 3: case 11: return "Z";
    case 4: return "Rx";
    case 5: return "Ry";
    case 6: return "Rz";
    case 7: return "Slider0";
    case 8: return "Slider1";
    default: return "";
  }
}

// Mech3.exe 1.2 @ 0x0042fe10: scaled and offset, 0 inside the dead zone.
float AxisBinding::value(const void* j, const void* m) const {
  const float r = source_value(source, static_cast<const platform::JoyState*>(j), static_cast<const zin::MouseInfo*>(m));
  const float v = scale * r + offset;
  return deadzone > std::fabs(v) ? 0.0f : v;
}

bool lever_reverse_on(const AxisBinding& b) {
  if (!zfix::options().bugs || !zfix::options().throttle_reverse || b.source < 1 || b.source > 8) return false;
  // No stick: the original's reading (its axes then read the offset).
  if (!(g_calls.joy_present ? g_calls.joy_present() : zin::joy_present())) return false;
  return !(g_calls.joy_gamepad ? g_calls.joy_gamepad() : zin::joy_gamepad());
}

float lever_throttle(const AxisBinding& b, const void* joy) {
  const platform::JoyState* j = static_cast<const platform::JoyState*>(joy);
  const float r = source_value(b.source, j, nullptr);
  // -1..1: the axes as zInput scaled them, a slider from DirectInput's
  // default range (zInput sets none on them, 0x0051d700).
  const float n = b.source <= 6 ? r : r / 32767.5f - 1.0f;
  float u = (b.scale < 0.0f ? 1.0f - n : 1.0f + n) * 0.5f;  // 0 the reverse end, 1 full ahead
  u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
  const float lo = kLeverStop - kLeverStopZone, hi = kLeverStop + kLeverStopZone;
  if (u < lo) return -(lo - u) / lo;
  if (u > hi) return (u - hi) / (1.0f - hi);
  return 0.0f;
}

// Mech3.exe 1.2 @ 0x0042fe70: the motion, scaled and offset (no dead zone).
float AxisBinding::delta(const void* j, const void* m) const {
  return source_delta(source, static_cast<const platform::JoyState*>(j), static_cast<const zin::MouseInfo*>(m)) *
             scale + offset;
}

// Mech3.exe 1.2 @ 0x00427870. The bound axes: the turn, the throttle (its
// sign flipped in reverse), and twist and pitch. With both on the mouse
// (sources 9..11) they are motion turned into rates by the view state (+0x0c:
// 0 the torso, 1 the free look at 1.5x, 2 a pointer, 3 the torso with the
// pointer held at the mouse look's place); else positions, the free look in
// view 1, and the camera's look drifting back by the pitch. A stick: its
// throttle counts only once it has moved (then it sets the target outright),
// and a change of the hat runs the camera's commands.
void PlayerControls::read_axes(float dt, float* thr, float* tn, float* p, float* tw, float* lp, float* ly) {
  const platform::JoyState* j = joy();
  const zin::MouseInfo* m = mouse();
  const int cam = g_calls.camera_view ? g_calls.camera_view() : 0;
  const AxisBinding* ax = axis_table().axes;
  // FIX (bug): with no stick a saved file's stick axes stay bound (only the stock
  // file is moved off it, 0x00480570) and read their offset each frame: the mech
  // walked and its torso turned by itself (docs/fixes.md).
  const bool no_stick = zfix::options().bugs && !(g_calls.joy_present ? g_calls.joy_present() : zin::joy_present());
  const auto bound = [&](const AxisBinding& a) { return a.source != 0 && !(no_stick && a.source <= 8); };
  if (bound(ax[3])) *tn = ax[3].value(j, m);
  if (bound(ax[0])) {
    // Ours: a lever with reverse of its own is not flipped by the flag, which a
    // tapped key or button 10 could leave set until the mission's end (fixes.md).
    const float sign = reverse ? -1.0f : 1.0f;  // IControls +0x18
    *thr = lever_reverse_on(ax[0]) ? lever_throttle(ax[0], j) : ax[0].value(j, m) * sign;
  }
  if (ax[2].source >= 9 && ax[1].source >= 9) {
    const float inv = 1.0f / dt;
    const float fx = inv * kMouseYaw, fy = inv * kMousePitch;
    switch (view) {
      case 0:
        look_y = look_x = look_dy = look_dx = 0.0f;
        *tw = ax[1].delta(j, m) * fx;
        *p = ax[2].delta(j, m) * fy;
        break;
      case 1:
        look_y = look_x = look_dy = look_dx = 0.0f;
        *ly = ax[1].delta(j, m) * fx * kMouseLook;
        *lp = ax[2].delta(j, m) * fy * kMouseLook;
        break;  // (the exe writes 2 over its copy of the camera's view here; the hat, not read in view 1, never sees it)
      case 2: {
        const float a = ax[1].value(j, m), b = ax[2].value(j, m);
        look_dx = look_x - a;
        look_dy = look_y - b;
        look_x = a;
        look_y = b;
        *p = 0.0f;
        *tw = 0.0f;
        break;
      }
      case 3:
        *tw = ax[1].delta(j, m) * fx;
        *p = ax[2].delta(j, m) * fy;
        if (g_calls.mouse_set) g_calls.mouse_set(look_x, look_y);
        else zin::mouse_set(look_x, look_y);
        break;
      default:
        break;
    }
  } else {
    const bool free = cam == 0 || cam == 1;
    if (bound(ax[1])) {
      *tw = clamp1(ax[1].value(j, m));
      if (view == 1) *ly = *tw, *tw = 0.0f;
    }
    if (bound(ax[2])) {
      *p = clamp1(ax[2].value(j, m));
      if (view == 1) *lp = *p, *p = 0.0f;
    }
    look_y = free ? clamp1(look_y - kLookReturn * dt * *p) : 0.0f;
  }
  if (!(g_calls.joy_present ? g_calls.joy_present() : zin::joy_present())) return;
  // Ours (W56): with reverse on the lever, a first reading in reverse is taken
  // as the last one, so a lever left at its end (stop, before) does not back
  // the mech off at the start; it counts once it moves. Ahead, it drives at
  // once, as the original's does.
  if (lever_reverse_on(axis_table().axes[0]) && !lever_seen) {
    lever_seen = true;
    if (*thr < 0.0f) joy_throttle = *thr;
  }
  if (!joy_throttle_set) {
    if (!(*thr < joy_throttle || *thr > joy_throttle)) {
      *thr = 0.0f;
    } else {
      joy_throttle_set = true;
      joy_throttle = *thr;
    }
  } else {
    joy_throttle = *thr;
  }
  const uint32_t pov = j->pov[0];
  if (last_pov == pov || view == 1) return;
  last_pov = pov;
  if ((pov & 0xffff) == 0xffff) return;  // centred
  const int dir = pov < 4500 ? 0 : pov <= 13500 ? 1 : pov < 22500 ? 2 : pov > 31500 ? 0 : 3;
  static const int kHatCommand[4] = {0x65, 0x63, 0x64, 0x62};  // up, right, down, left (camera commands)
  static const int kHatView[4] = {4, 0, 5, 1};  // the view in which each gives 0x66 (up: 0x66 in view 4)
  const int cmd = dir == 0 ? 0x65 + (cam == 4 ? 1 : 0) : cam == kHatView[dir] ? 0x66 : kHatCommand[dir];
  run_command(cmd);
}

namespace {
// What the player's vlist asks of its controls: the vehicle (IControls +0x10)
// and "hit lately" (vt+0xa0, 0x004aed40); the rest is the brain's alone.
struct PilotControls : ai::BrainControls {
  MechControls& m;
  explicit PilotControls(MechControls& c) : m(c) {}
  zgame::Handle vehicle() override { return ai::brain_hooks().vehicle_handle(m.owner); }
  bool hit_lately() override { return m.hit_lately(); }
  void stop() override {}
  void smooth(float) override {}
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
}  // namespace

// Mech3.exe 1.2 @ 0x00427300
void player_controls_mission_end() {
  PlayerControls* p = player_controls_instance();
  if (!p) return;
  delete p->settings.vlist, p->settings.vlist = nullptr;  // 0x004d9cc0 -> 0x004d91e0
  p->reverse = false;  // +0x50
}

// Mech3.exe 1.2 @ 0x004d8990
void PlayerControls::pilot_tick() {
  PilotControls c(*this);
  bool picked = false;
  ai::Contact* target = nullptr;
  ai::vlist_run(settings, c, c.vehicle(), &picked, &target);
}

// Mech3.exe 1.2 @ 0x00427d60. Not while powering down: the pilot's tick
// (lance orders, likely), the axes, then the keys over them; the reticule
// zoom; the throttle (a step a frame, or the joystick's outright, or easing
// back to zero with ThrottleConst off); the turn and jump; pitch, twist and
// the free look eased toward their inputs at 6/s. Then the smoothing.
int PlayerControls::update(float dt) {
  Mech& m = *owner;
  if (!m.is_powered_down()) {  // vtable +0xa0, 0x0041cc10
    if (g_calls.pilot_tick) g_calls.pilot_tick(*this);
    else pilot_tick();
    float d = 0, tn = 0, jump_in = 0, p = 0, tw = 0, lp = 0, ly = 0, strafe = 0, ahead = 0;
    void* screen = &m == world().player && heat_calls().screen ? heat_calls().screen(m) : nullptr;
    read_axes(dt, &d, &tn, &p, &tw, &lp, &ly);
    // FIX (B): the throttle keys' step is per 1/30 s, not per frame (fixes.md, Frame rate).
    const float step = kThrottleStep * zfix::frame_steps(dt);
    if (held(kThrottleUp)) {
      d = step;
      joy_throttle_set = throttle_held = false;
    } else if (held(kThrottleDown)) {
      d = -step;
      joy_throttle_set = throttle_held = false;
    }
    if (held(kTurnLeft)) tn = 1.0f;
    else if (held(kTurnRight)) tn = -1.0f;
    if (m.equipment[11] > 0) {  // jump jets
      if (held(kJetsUp)) jump_in = 1.0f;
      if (g_calls.mw2_jets && g_calls.mw2_jets()) {
        if (held(kJetsForward)) jump_in = 1.0f, ahead = 1.0f;
        else if (held(kJetsBack)) jump_in = 1.0f, ahead = -1.0f;
        if (held(kJetsLeft)) jump_in = 1.0f, strafe = -1.0f;
        else if (held(kJetsRight)) jump_in = 1.0f, strafe = 1.0f;
        if (m.airborne()) {
          if (held(kJetsTurnLeft)) tn = 1.0f;
          else if (held(kJetsTurnRight)) tn = -1.0f;
        }
      }
    }
    if (screen) {
      // Held zooms in, let go zooms out; the click plays as it changes (the
      // HUD's +0x1514 bit 4 set: not zoomed).
      const bool zoom = held(kZoom);
      auto busy = [&] { return g_calls.screen_busy ? g_calls.screen_busy(screen) : uint8_t{0}; };
      const uint32_t fl = part_calls().screen_flags ? part_calls().screen_flags(screen) : 0;
      if (!busy() && ((fl & 0x10) != 0) == zoom)
        m.sounds[kZoomSoundSlot] = part_calls().sound ? part_calls().sound(m, kZoomSound, 1.0f) : nullptr;
      if (!busy() && g_calls.zoom) g_calls.zoom(screen, zoom);
    }
    if (held(kTwistLeft)) tw = kTwistKey;
    else if (held(kTwistRight)) tw = -kTwistKey;
    if (held(kPitchUp)) p = 1.0f;
    else if (held(kPitchDown)) p = -1.0f;
    fire = held(kFire) ? 1 : 0;  // 0x00467480
    d = clamp1(d);
    tn = clamp1(tn);
    jump_in = jump_in > 1.0f ? 1.0f : !(jump_in >= 0.0f) ? 0.0f : jump_in;
    if (!joy_throttle_set) {
      if (!(d < 0.0f || d > 0.0f) && throttle_returns && !throttle_held) {
        throttle_target = fast_exp(kThrottleReturn * dt) * throttle_target;
        if (std::fabs(throttle_target) < kThrottleZero) throttle_target = 0.0f;
      } else {
        set_throttle(throttle_target + d);  // IControls +0x20
      }
    } else {
      throttle_target = clamp1(d);  // straight in: the reverse flag is not touched
    }
    set_turn(tn);
    if (!jump_held || jump_in < 0.0f || jump_in > 0.0f) set_jump(jump_in);
    approach(pitch, p, dt);
    approach(twist, tw, dt);
    approach(axis_2c, lp, dt);
    approach(axis_30, ly, dt);
    jj_strafe = strafe;
    jj_forward = ahead;
  }
  MechControls::update(dt);
  return 1;
}

// Mech3.exe 1.2 @ 0x004265a0 (the base of every mech controls; its most
// derived branch is the class's own). +0x40 and +0x5b are left as they were.
void MechControls::init() {
  lever_seen = false;
  view = 2;
  axis_2c = axis_30 = 0;
  jump_held = false;
  moves[1] = true;
  throttle_returns = true;
  last_pov = 1;
  throttle = turn = pitch = twist = jump = 0;
  jj_forward = jj_strafe = 0;
  throttle_target = turn_target = jump_target = 0;
  flags = 0;
  moves[0] = false;
  x5a = false;
  fire = 0;
  reverse = false;
  joy_throttle_set = false;
  throttle_held = false;
  wrapped = 0;
}

// Mech3.exe 1.2 @ 0x004d8750 (most derived: the IControls base 0x004aec00 at
// +0x30c, its owner +0x31c), over 0x004d8670: the base 0x004265a0, then the
// brain 0x004cf330 on +0x5c with the net and the record.
namespace {
int32_t g_mech_controls_alive = 0;
}  // namespace
int32_t ai_mech_controls_alive() { return g_mech_controls_alive; }
AiControls::AiControls() { ++g_mech_controls_alive; }

AiControls::AiControls(Mech* o, ai::AiNet* net, const ai::AiParams& rec) {
  ++g_mech_controls_alive;
  owner = o;
  init();
  brain = std::make_unique<ai::Brain>(net, rec);
  brain->controls = this;
}

// Mech3.exe 1.2 @ 0x004d8c00 -> 0x004d8c40: the brain's destructor
// 0x004cf630, then the base's vtables back (nothing to free).
AiControls::~AiControls() { --g_mech_controls_alive; }

// Mech3.exe 1.2 @ 0x004e96f0: the brain's frame (0x004e9710, on +0x5c),
// which sets the targets and runs the smoothing itself (0x004d89f0).
// Controls made without a record have no brain: the hook, else the smoothing.
int AiControls::update(float dt) {
  if (brain) return brain->frame(*this, dt) ? 1 : 0;
  if (g_calls.ai_brain) return g_calls.ai_brain(*this, dt) != 0 ? 1 : 0;
  MechControls::update(dt);
  return 1;
}

zgame::Handle AiControls::vehicle() { return ai::brain_hooks().vehicle_handle(owner); }
// Mech3.exe 1.2 @ 0x004ea520; without a brain (the tests' controls) the base's.
void AiControls::on_hit(void* attacker) {
  ai::BrainHooks& h = ai::brain_hooks();
  if (brain) brain->on_hit(hit, vehicle(), h.vehicle_handle(attacker));
  else MechControls::on_hit(attacker);
}

// Mech3.exe 1.2 @ 0x004e94f0
void AiControls::tick() {
  if (brain) brain->pilot_tick(*this);
}

// Mech3.exe 1.2 @ 0x0049e4e0 (ECX the player's settings): a pilot of the
// same side (+0x18) leaves its region triggers (vlist entries with a region,
// +0x14) to them (0x0049e4c0 -> 0x004d9480, their end).
void take_triggers(ai::AiParams& to, const ai::AiParams& dead) {
  if (!dead.vlist || to.team() != dead.team()) return;
  for (const ai::VListEntry& e : *dead.vlist) {
    if (!e.region) continue;
    if (!to.vlist) to.vlist = new std::list<ai::VListEntry>;
    to.vlist->push_back(e);
  }
}

// 0x0049e540's path on a death (player.cpp's assume_duties, the mech's +0x04
// only for its reports): the player's own settings (his controls' vt+0x90).
// No player or no controls: reported in the exe, nothing here.
void player_takes_triggers(const ai::AiParams& dead) {
  Mech* p = world().mission_player;
  auto* pc = p ? dynamic_cast<PlayerControls*>(p->controls) : nullptr;
  if (pc) take_triggers(pc->settings, dead);
}

// Mech3.exe 1.2 @ 0x004d8a00: the player takes its triggers (0x0049e540),
// then 0x004ea6b0: the base's (the tick once more) and the radio's ejecting.
void AiControls::on_death() {
  if (brain) player_takes_triggers(brain->params);
  MechControls::on_death();  // 0x004aed50
  if (brain) brain->died();
}

// Mech3.exe 1.2 @ 0x004ea630
void AiControls::on_kill(void* victim) {
  if (brain) brain->killed(vehicle(), ai::brain_hooks().vehicle_handle(victim));
}

// Mech3.exe 1.2 @ 0x004ea500
void AiControls::hit_an_ally(void* victim) {
  if (brain) brain->hit_an_ally(ai::brain_hooks().vehicle_handle(victim));
}

// Mech3.exe 1.2 @ 0x004aec90
void MechControls::on_hit(void* attacker) {
  ai::BrainHooks& h = ai::brain_hooks();
  ai::controls_hit(hit, h.vehicle_handle(owner), h.vehicle_handle(attacker));
}

// Mech3.exe 1.2 @ 0x004aed40
bool MechControls::hit_lately() const { return hit.within(1.0f, ai::brain_hooks().now()); }

// Mech3.exe 1.2 @ 0x00426c10
void MechControls::halt() {
  jj_strafe = 0, jj_forward = 0, pitch = 0, turn = 0;
  set_throttle(0);  // IControls +0x20
  set_turn(0);      // +0x28
  set_jump(0);      // +0x34: 0x00426b20(0, 0)
  joy_throttle_set = false;
  fire = 0;         // +0x10: 0x00428580
}

// Mech3.exe 1.2 @ 0x00428490, its five setters through IControls (+0x20,
// +0x28, +0x34, +0x3c, +0x44).
void MechControls::stop() {
  set_throttle(0);
  set_turn(0);
  set_jump(0);
  jj_forward = 0;  // 0x00428560
  jj_strafe = 0;   // 0x00428570
}

// --- the player's controls made (docs/mech-build.md section 7) -----------------

namespace {
PlayerControls* g_player = nullptr;  // 0x006056ec
}  // namespace

PlayerControls*& player_controls_instance() { return g_player; }

// Mech3.exe 1.2 @ 0x00427280
PlayerControls* player_controls(uint32_t mode) {
  if (!g_player) {
    register_player_commands();
    auto* pc = new PlayerControls();
    mech_build_hooks().on_new(0x160, pc);
    pc->construct(mode);
    g_player = pc;
  } else {
    g_player->set_mode(mode);
  }
  return g_player;
}

// Mech3.exe 1.2 @ 0x00427320 (most derived), over 0x004265a0(0, 0): at rest,
// view state 2, ThrottleConst off (the throttle returns), the hat centred.
void PlayerControls::construct(uint32_t mode) {
  owner = nullptr;  // the IControls base (0x004aec00 on +0x150; owner +0x15c)
  init();
  mech_build_hooks().controls_brain_init(*this);
  flags = 0;
  joy_throttle = 0;
  set_mode(mode);
  look_dx = look_dy = 0;
  look_y = look_x = 0;
}

// Mech3.exe 1.2 @ 0x00426ce0: the commands whose keys reach the controls and
// the viewed vehicle (0x00426cb0), in the exe's order.
void register_player_commands() {
  static const int32_t kCommands[] = {
      0x24, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x1a, 0x25, 0x26, 0x27, 0x2a, 0x2b,
      0x28, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x39, 0x06, 0x07, 0x3b, 0x3a,
      0x6e, 0x6b, 0x68, 0x74, 0x75, 0xa5, 0x69, 0x47, 0x4e, 0x4d, 0x46, 0x4c, 0x4b, 0x48, 0x14, 0x15,
      0x49, 0x4a, 0x29, 0x99, 0x41, 0x16, 0x17, 0x76, 0x77, 0x92, 0x13, 0x3e, 0x3f, 0x40, 0x43, 0xa6,
      0x42, 0x44, 0x36, 0x37, 0x38, 0x52, 0x51, 0x4f, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x62, 0x63,
      0x66, 0x67, 0x64, 0x65, 0x45, 0xa1, 0x73, 0x59, 0x5a, 0x5d, 0x5e, 0x5f, 0x60, 0x5c, 0x5b};
  for (const int32_t cmd : kCommands) mech_build_hooks().set_command_callback(cmd);
}

// Mech3.exe 1.2 @ 0x004274d0
void PlayerControls::command(int32_t cmd) {
  if (!owner || owner->is_powered_down()) return;  // the vehicle's vt+0x50, then vt+0xa0
  if (cmd == 0x1a) {  // THROTTLE_REV
    if (throttle_target != 0.0f) set_throttle(-throttle_target);  // IControls +0x2c, +0x20
    else reverse = !reverse;
    return;
  }
  if (cmd == 0x1b) {  // THROTTLE1: 0
    set_throttle(0.0f);
    joy_throttle_set = false;
    throttle_held = true;
    return;
  }
  static const float kSteps[] = {0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f};  // 0x1c..0x24
  if (cmd < 0x1c || cmd > 0x24) return;
  const float v = kSteps[cmd - 0x1c];
  set_throttle(reverse ? -v : v);  // IControls +0x18 IsReverse
  throttle_held = true;
  joy_throttle_set = false;
}

// Mech3.exe 1.2 @ 0x00426cb0
void player_command(int32_t cmd) {
  MechBuildHooks& h = mech_build_hooks();
  h.controls_command(g_player, cmd);  // the controls' vt+0x1c (0x004274d0)
  if (const Handle view = h.view())
    if (const Handle target = h.view_target(view)) h.view_target_command(target, cmd);  // vt+0xfc
}

}  // namespace mech3
