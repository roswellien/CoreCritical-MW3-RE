// C:\zipper\GameZMech3\zSound\zsnd_3d.cpp: the listener and 3D placement
// (0x00572050 names the file), with the functions between zsnd_init.cpp and
// it: the device shutdown and the volume fades (docs/sound.md sections 4, 5).
#include <cstring>

#include "gamez/zsound/zsnd.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/ztimer.h"

namespace zsnd {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zSound\\zsnd_3d.cpp";
platform::SoundOutput* out() { return hooks().output; }

uint32_t bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
float from_bits(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// 0x00571830 (0x00571810): a fade of the voice to `level` of its sound's volume.
void fade_init(Fade* f, Voice* v, float level, int32_t stop) {
  f->voice = v;
  f->target = static_cast<float>(volume_db(v->sound->volume * level * state().master()));
  int32_t cur = static_cast<int32_t>(bits(level));  // GetVolume's slot is the argument's
  out()->get_volume(v->buffer, &cur);
  f->current = static_cast<float>(cur);
  f->stop = stop;
}

// 0x00571940: 25 dB a second toward the target. Done only when the volume
// lands exactly on it, which a step only does at full volume or silence
// (docs/sound.md, fades).
int32_t fade_step(Fade* f, float dt) {
  const float dir = (f->target - f->current < 0.0f) ? -1.0f : 1.0f;
  f->current = dir * dt * 2500.0f + f->current;
  // FIX (bug): a step reaching the target ends the fade there, so fades to a
  // partial volume end instead of wobbling around it.
  const bool reached = zfix::options().bugs && (dir > 0.0f ? f->current >= f->target : f->current <= f->target);
  if (reached) f->current = f->target;
  if (f->current > 0.0f) f->current = 0.0f;
  else if (f->current < -10000.0f) f->current = -10000.0f;
  out()->set_volume(f->voice->buffer, static_cast<int32_t>(f->current));
  if (!reached && !(f->current == f->target)) return 0;
  if (f->stop) stop(f->voice);
  state().free_fades.push_back(f);
  return 1;
}
}  // namespace

// Mech3.exe 1.2 @ 0x005715d0 (ECX the user-data free function). 0x005715c0
// is the same.
int32_t close_device(FreeFn fn) {
  State& st = state();
  if (!st.open || !st.initialized) return 0;
  if (hooks().cd_close) hooks().cd_close();
  free_groups(fn);
  free_sets(fn);
  if (st.primary) out()->release(st.primary), st.primary = 0;
  out()->release_device();
  st.open = 0;
  return 1;
}

// Mech3.exe 1.2 @ 0x005716a0: leaving a mission.
void reset() {
  State& st = state();
  if (st.open && st.initialized) {
    stop_queue(0);
    groups_reset();
  }
}

// Mech3.exe 1.2 @ 0x00571a90: every fade one step, the finished ones
// removed (std::remove_if, then erase).
void fades_update(float dt) {
  std::list<Fade*>& l = state().fades;
  for (auto it = l.begin(); it != l.end();) {
    if (fade_step(*it, dt)) it = l.erase(it);
    else ++it;
  }
}

// Mech3.exe 1.2 @ 0x00571b90
void fades_shutdown() {
  State& st = state();
  for (Fade* f : st.fades) {
    stop(f->voice);
    st.free_fades.push_back(f);  // 0x005718f0
  }
  st.fades.clear();
  for (Fade* f : st.free_fades) delete f;
  st.free_fades.clear();
}

// Mech3.exe 1.2 @ 0x00571cc0 (ECX sound, EDX position): plays, then fades
// to `fade_to` unless that is -1 or the volume it starts at.
VoiceBase* play_fading(Object* obj, const float* pos, float volume, const float* vel, float fade_to, int32_t stop_at_end) {
  VoiceBase* v = play_at(obj, pos, volume, vel);
  if (!v) return nullptr;
  if (v->kind == 0 && fade_to != -1.0f && fade_to != volume) fade(v, stop_at_end, fade_to);
  return v;
}

// Mech3.exe 1.2 @ 0x00571d20 (ECX voice, EDX stop at the end, the level).
// Returns the voice, or 0 when it will be stopped.
VoiceBase* fade(VoiceBase* voice, int32_t stop_at_end, float level) {
  if (!voice) return voice;
  State& st = state();
  Fade* f;
  if (st.free_fades.empty()) {
    f = new Fade;
  } else {
    f = st.free_fades.front();
    st.free_fades.pop_front();
  }
  fade_init(f, static_cast<Voice*>(voice), level, stop_at_end);
  st.fades.push_back(f);
  return stop_at_end ? nullptr : voice;
}

// Mech3.exe 1.2 @ 0x00571e50 (ECX the camera's world matrix, EDX its
// velocity, either may be null).
int32_t set_listener(const float* matrix, const float* vel) {
  State& st = state();
  if (matrix) std::memcpy(st.listener, matrix, sizeof st.listener);
  if (vel) std::memcpy(st.listener_vel, vel, sizeof st.listener_vel);
  st.listener_set = 1;
  return 1;
}

// Mech3.exe 1.2 @ 0x00571f30 -> 0x00572050 (ECX voice, EDX position): the
// voice's volume and pan from where it is. Full volume to RANGE's first
// value, then 6 dB less per that distance, silent past the second; pan
// up to 16 dB toward the side. The distance is a fast square root (the
// exponent halved). With `doppler` the frequency is scaled by the closing
// speed each call, and never put back (docs/sound.md section 4). Returns 0
// when out of range.
int32_t update_3d(Voice* v, const float* pos, const float* vel, int32_t doppler) {
  State& st = state();
  if (v->kind != 0) return -1;
  if (pos && !(v->sound->flags & k3D)) return 1;
  if (!v->buffer) return -1;
  int32_t vol, pan;
  if (st.listener_set && (v->has_pos || pos)) {
    Sound* s = v->sound;
    if (s->kind != 0) return -1;
    if (pos) v->has_pos = 1, std::memcpy(v->pos, pos, 12);
    if (vel) std::memcpy(v->vel, vel, 12);
    vol = muted() ? -10000 : v->volume;
    const float* lp = st.listener + 9;
    const float d[3] = {v->pos[0] - lp[0], v->pos[1] - lp[1], v->pos[2] - lp[2]};
    const float d2 = (d[0] * d[0] + d[1] * d[1]) + d[2] * d[2];
    float dist, inv;
    if (d2 == 0.0f) {
      pan = 0;
      dist = s->range_min;
      inv = 1.0f / dist;
    } else {
      dist = from_bits(static_cast<uint32_t>((static_cast<int32_t>(bits(d2)) >> 1) + 0x1fc00000));
      inv = 1.0f / dist;
      const float* r = st.listener;  // the first row: the listener's right
      const float side = (d[0] * r[0] + d[1] * r[1]) + d[2] * r[2];
      pan = static_cast<int32_t>(side * inv * 1600.0f);
    }
    if (dist > s->range_max) {
      out()->set_volume(v->buffer, -10000);
      return 0;
    }
    if (!(dist < s->range_min)) {
      vol += static_cast<int32_t>((dist / s->range_min - 1.0f) * -600.0f);
      if (vol < -10000) vol = -10000;
    }
    if (doppler) {
      const float* lv = st.listener_vel;
      const float rel[3] = {v->vel[0] - lv[0], v->vel[1] - lv[1], v->vel[2] - lv[2]};
      const float closing = (rel[0] * d[0] + rel[1] * d[1]) + rel[2] * d[2];
      const float k = 1.0f - closing * inv * st.inv_speed_of_sound;
      uint32_t hz = 0;
      out()->get_frequency(v->buffer, &hz);
      // fild qword, fmul dword, ftol (0x005722e8), the x87 at 53 bits in play
      // (zfix fpu53); at the emulator's 24 the product rounds to float first.
      const double scaled = static_cast<double>(hz) * k;
      out()->set_frequency(v->buffer, static_cast<uint32_t>(static_cast<int64_t>(zfix::options().fpu53 ? scaled : static_cast<float>(scaled))));
    }
  } else {
    vol = muted() ? -10000 : v->volume;
    pan = 0;
  }
  out()->set_pan(v->buffer, pan);
  const int32_t hr = out()->set_volume(v->buffer, vol);
  if (hr != 0) return ds_error(hr, kFile, 0x164);
  return 1;
}

// Mech3.exe 1.2 @ 0x00572360
float speed_of_sound() { return state().speed_of_sound; }

// Mech3.exe 1.2 @ 0x00572370
void set_speed_of_sound(float v) {
  state().speed_of_sound = v;
  state().inv_speed_of_sound = 1.0f / v;
}

}  // namespace zsnd
