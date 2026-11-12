// zWeapon's beams (docs/weapons.md, "Beams"): made, started, stopped and
// freed by the game's BeamWeapon, and cast each frame by the tick. The file
// name is not in the exe; these open the module at 0x00581230.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "crt/msvc_math.h"
#include "crt/msvc_rand.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zweapon/zweapon.h"

#pragma STDC FP_CONTRACT OFF

namespace zwep {

namespace {
HitList g_list;  // the frame's query answers (a stack buffer in the original)

zmth::Vec3 v3(const Vec3f3& v) { return {v.x, v.y, v.z}; }
Vec3f3 f3(const zmth::Vec3& v) { return {v.x, v.y, v.z}; }
bool eq0(float v) { return !(v < 0.0f) && !(v > 0.0f); }
const RunHooks& rh() { return run_hooks(); }

float pitch_of(const Vec3f3& d) { return static_cast<float>(std::asin(static_cast<double>(d.y))); }
float yaw_of(const Vec3f3& d) {
  return static_cast<float>(std::atan2(-static_cast<double>(d.x), -static_cast<double>(d.z)));
}
float rand_unit() {  // rand / 32767 - 0.5
  float r = static_cast<float>(crt::msvc_rand());
  r = r * 3.05185094e-05f;
  return r - 0.5f;
}
float direction(const Vec3f3& from, const Vec3f3& to, Vec3f3* out) {  // 0x0051e3d0
  zmth::Vec3 o;
  const float len = zmth::direction(v3(from), v3(to), o);
  *out = f3(o);
  return len;
}
void add_scaled(const Vec3f3& base, const Vec3f3& dir, float s, Vec3f3* out) {  // 0x0051e0e0
  zmth::Vec3 o;
  zmth::add_scaled(v3(base), v3(dir), s, &o);
  *out = f3(o);
}
void set_active(Handle n, int32_t on) {
  if (rh().set_active) rh().set_active(n, on);
}
void set_intersect(Handle n, int32_t on) {
  if (hit_hooks().set_intersect) hit_hooks().set_intersect(n, on);
}
void set_translation(Handle n, const Vec3f3& p) {
  if (rh().set_translation) rh().set_translation(n, p.x, p.y, p.z);
}
void set_rotation_to(Handle n, const Vec3f3& d) {
  const float yaw = yaw_of(d);
  if (rh().set_rotation) rh().set_rotation(n, pitch_of(d), yaw, 0.0f);
}
void set_scale(Handle n, float x, float y, float z) {
  if (rh().set_scale) rh().set_scale(n, x, y, z);
}
void sound_frequency(Handle s, float v) {
  if (rh().sound_frequency) rh().sound_frequency(s, v);
}
Handle sound_at(Handle s, const Vec3f3* at) { return rh().sound_at ? rh().sound_at(s, at, 1.0f) : nullptr; }
void play_at(Handle anim, const Vec3f3& p) {  // 0x00508aa0 at a point, unturned
  const float a[9] = {p.x, p.y, p.z, 0, 0, 0, 0, 0, 0};
  if (hit_hooks().effect) hit_hooks().effect(anim, a);
}
int32_t segment(const Vec3f3* from, const Vec3f3* to, HitList* out) {
  return rh().segment ? rh().segment(from, to, out) : 1;
}
void query_setup(const WeaponDef* def) {
  if (rh().impact_type) rh().impact_type(def->impact_type);
  if (rh().query_mask) rh().query_mask(0x40000);
}
Projectile stand_in() {  // the zeroed stack projectile of 0x005814b0
  Projectile p;
  p.health = 0;
  p.damage_scale = 0;
  p.track_scale[0] = p.track_scale[1] = 0;
  return p;
}

void multi_target(WeaponDef* def, Beam* b, int32_t* count);
}  // namespace

// Mech3.exe 1.2 @ 0x00584db0: a segment's node, with the FLYOUT model under it.
Handle beam_segment_node(Handle model) {
  Handle n = rh().object3d_create ? rh().object3d_create() : nullptr;
  set_active(n, 1);
  if (model && rh().obj_add_child) rh().obj_add_child(n, model);
  return n;
}

// Mech3.exe 1.2 @ 0x00584a70: a beam with its segment nodes (up to 8, one
// for DESIGNATE), hidden, in the world. The game passes no zone and one
// segment. FIX (A): new for calloc.
Beam* beam_make(WeaponDef* def, Handle owner, const uint32_t* zone, const Vec3f3* origin, const Vec3f3* dir,
                int32_t segments) {
  Beam* b = new Beam;
  b->damage_scale = b->range_scale = 0;
  b->origin = origin;
  b->def = def;
  b->owner = owner;
  b->zone = zone;
  b->dir = dir;
  if (segments > 8) segments = 8;
  else if (def->flags & kDesignate) segments = 1;
  b->segment_count = segments;
  for (int32_t i = 0; i < segments; ++i) {
    BeamSegment& s = b->segments[i];
    s.node = beam_segment_node(def->flyout.model);
    char name[32];
    std::snprintf(name, sizeof name, "BeamReflect_%d", static_cast<int>(i));
    if (rh().set_name) rh().set_name(s.node, name);
    set_active(s.node, 0);
    if (!(def->flags & kMultiTarget) && rh().set_flag_4fbb90) rh().set_flag_4fbb90(s.node, 1);
    if (rh().add_child) rh().add_child(state().world, s.node);
  }
  return b;
}

// Mech3.exe 1.2 @ 0x00584b40
int32_t beam_free(Beam* b) {
  for (int32_t i = 0; i < b->segment_count; ++i) {
    Handle n = b->segments[i].node;
    while (rh().parent_count && rh().parent_count(n) > 0) rh().remove_child(rh().first_parent(n), n);
    while (rh().child_count && rh().child_count(n) > 0) rh().remove_child(n, rh().first_child(n));
    if (rh().node_destroy) rh().node_destroy(n);
  }
  delete b;
  return 0;
}

// Mech3.exe 1.2 @ 0x00581230: the trigger: the fire sound, the looping FLYOUT
// sound (silent for DESIGNATE until it touches), the muzzle effect or
// animation, the fire state taken, a light for DESIGNATE; onto the def's list.
int32_t beam_start(Beam* b) {
  WeaponDef* def = b->def;
  FireState& f = fire_state();
  sound_at(def->fire.sounds[0], b->origin);
  b->sound = sound_at(def->flyout.sounds[0], b->origin);
  if (def->flags & kDesignate) sound_frequency(b->sound, 0.0f);
  if (def->fire.effect) {
    if (hit_hooks().impact_effect) hit_hooks().impact_effect(def->fire.effect, b->origin);
  } else if (Handle a = def->fire.animation) {
    float roll = 0.0f;
    if (def->fire.flags & 1) roll = rand_unit() * 3.14159274f;
    const Vec3f3& o = *b->origin;
    const float yaw = yaw_of(*b->dir);
    const float args[9] = {o.x, o.y, o.z, pitch_of(*b->dir), yaw, roll, 0, 0, 0};
    // The running animation goes in the fire block's +0x08 (the second choice).
    def->fire.animation2 = rh().effect_handle ? rh().effect_handle(a, args) : nullptr;
  } else {
    def->fire.animation2 = nullptr;
  }
  b->length = b->timer = b->age = 0;
  b->flags &= ~2u;
  b->damage_scale = f.damage_scale;
  f.damage_scale = 1;
  b->range_scale = f.beam_range_scale;
  f.beam_range_scale = 1;
  b->flags = (b->flags & ~1u) | (static_cast<uint32_t>(f.beam_ground_fx) & 1u);
  f.beam_ground_fx = 1;
  if (def->flags & kLockOn) {
    b->lock_count = f.lock_flag;
    b->targets = f.lock_target;
    f.lock_flag = nullptr;
  }
  if (def->flags & kDesignate) {
    const Vec3f3 color{def->target_heat[3], def->target_heat[4], def->target_heat[5]};
    b->light = light_take(&color);
    if (rh().light_range) rh().light_range(b->light, def->target_heat[0], def->target_heat[1]);
    set_active(b->light, 0);
  }
  if (def->beams) def->beams->prev = b;
  b->prev = nullptr;
  b->next = def->beams;
  def->beams = b;
  return 0;
}

// Mech3.exe 1.2 @ 0x00581400: the release: the loop stops, the fire sound
// plays again, the muzzle animation is freed; off the list, the light back.
int32_t beam_stop(Beam* b) {
  WeaponDef* def = b->def;
  if (b->sound && rh().sound_stop) rh().sound_stop(b->sound);
  if (def->fire.sounds[0]) sound_at(def->fire.sounds[0], b->origin);
  if (def->fire.animation2) {
    if (rh().anim_free) rh().anim_free(def->fire.animation2);
    def->fire.animation2 = nullptr;
  }
  if (b->next) b->next->prev = b->prev;
  if (b->prev) b->prev->next = b->next;
  if (b == def->beams) def->beams = b->next;
  b->prev = nullptr;
  b->next = nullptr;
  if (b->light) light_return(b->light);
  for (int32_t i = 0; i < b->active; ++i) set_active(b->segments[i].node, 0);
  b->active = 0;
  return 0;
}

// Mech3.exe 1.2 @ 0x00582f00: a segment shown from its start along its direction.
void beam_draw(const BeamSegment* s) {
  set_active(s->node, 1);
  set_translation(s->node, s->start);
  set_rotation_to(s->node, s->dir);
  set_scale(s->node, 1.0f, 1.0f, s->length);
}

// Mech3.exe 1.2 @ 0x00582de0: MULTI_TARGET: one segment cast toward `to`; what
// it hits takes the beam's damage if it has a handler; the segment ends there.
int32_t beam_cast(WeaponDef* def, Beam* b, BeamSegment* s, const Vec3f3* to) {
  query_setup(def);
  set_intersect(b->owner, 0);
  HitList list;
  const int32_t r = segment(&s->start, to, &list);
  set_intersect(b->owner, 1);
  if (HitRecord* h = pick(def, r, &list)) {
    if (hit_hooks().node_handler && hit_hooks().node_handler(h->node) != 0) {
      const float d = beam_damage(def, b);
      if (!eq0(d)) apply_damage(def, b->owner, &s->start, h, d, nullptr);
    }
    sound(def, h, soil_of(def, h->material), 1.0f);
    s->length = zmth::distance(v3(s->start), v3(h->point));
    return 1;
  }
  s->length = zmth::distance(v3(s->start), v3(*to));
  return 0;
}

// Mech3.exe 1.2 @ 0x00582c10: MULTI_TARGET: the targets ordered along the
// direction to the farthest one (a bubble sort on the weapon's own lock
// array); `along` gets each one's distance along it. With every target at the
// muzzle the original aims at an unset pointer; here at the muzzle.
void beam_sort_targets(Beam* b, float* along, Vec3f3* far_dir) {
  LockRef* t = b->targets;
  const int32_t n = *b->lock_count;
  float best = 0.0f;
  const Vec3f3* far_at = b->origin;
  for (int32_t i = 0; i < n; ++i) {
    const float d = zmth::distance(v3(*b->origin), v3(*t[i].at));
    if (d > best) {
      best = d;
      far_at = t[i].at;
    }
  }
  direction(*b->origin, *far_at, far_dir);
  for (int32_t i = 0; i < n; ++i) {
    const Vec3f3& a = *t[i].at;
    const Vec3f3 v{a.x - b->origin->x, a.y - b->origin->y, a.z - b->origin->z};
    along[i] = (far_dir->x * v.x + far_dir->y * v.y) + far_dir->z * v.z;
  }
  bool swapped;
  do {
    swapped = false;
    for (int32_t i = 0; i < n - 1; ++i) {
      if (along[i] > along[i + 1]) {
        const LockRef x = t[i];
        t[i] = t[i + 1];
        t[i + 1] = x;
        const float y = along[i];
        along[i] = along[i + 1];
        along[i + 1] = y;
        swapped = true;
      }
    }
  } while (swapped);
}

// FIX (bug): the hold counted in whole milliseconds; the exe's float sum of
// 60 x (1/60) falls short of 1.0 and costs a frame (zfix bugs, weapons.md TAG).
bool designate_held(float timer, float hold) {
  if (!zfix::options().bugs) return timer >= hold;
  return std::floor(timer * 1000.0f + 0.5f) >= std::floor(hold * 1000.0f + 0.5f);
}

// Mech3.exe 1.2 @ 0x005814b0: every beam of a BEAM def, once a frame. The ray
// grows toward RANGE (x the range scale) at TIME_TO_MAX_RANGE's rate and is
// cast from the muzzle: a thing with a handler takes the damage (DESIGNATE:
// after its sixth value in seconds on it, the impact and its airstrike),
// BOUNCES reflects into the next segment, anything else ends it. Each touch
// plays the impact sound, every frame.
void beam_update(WeaponDef* def) {
  HitState& hs = hit_state();
  RunState& rs = run_state();
  const float dt = hs.dt;
  for (Beam* b = def->beams; b; b = b->next) {
    hs.beam_ground_fx = static_cast<int32_t>(b->flags & 1);
    int32_t n = 0;
    set_query_zone(b->zone ? *b->zone : rs.tick_zone);
    if (def->flags & kMultiTarget) {
      multi_target(def, b, &n);
    } else {
      const float reach = b->range_scale * def->range;
      if (b->length < reach || b->length > reach) {
        const float d = reach - b->length;
        if (!(static_cast<double>(d) > 0.1)) {
          b->length = reach;
        } else {
          const float l = d * def->velocity * dt + b->length;
          b->length = !(l >= reach) ? l : reach;
        }
      }
      b->age = dt + b->age;
      const Vec3f3* from = b->origin;
      const Vec3f3* dir = b->dir;
      float acc = 0;
      Vec3f3 bounce_dir{};
      float bounce_len = 0;
      float len = 0;
      BeamSegment* s = b->segments;
      bool bounced;
      do {
        ++n;
        bounced = false;
        set_active(s->node, 1);
        set_translation(s->node, *from);
        set_rotation_to(s->node, *dir);
        const float phase = crt::x87_mul_f24(b->age * def->pulse_rate, 6.28318530718);
        const float sn = static_cast<float>(std::sin(static_cast<double>(phase)));
        const float pulse = (def->pulse[1] - def->pulse[0]) * ((sn - -1.0f) * 0.5f) + def->pulse[0];
        if (rh().set_uniform_scale) rh().set_uniform_scale(s->node, pulse);
        len = b->length - acc;
        add_scaled(*from, *dir, len, &s->start);  // the ray's end
        query_setup(def);
        if (n == 1) set_intersect(b->owner, 0);
        const int32_t r = segment(from, &s->start, &g_list);
        if (n == 1) set_intersect(b->owner, 1);
        if (pick(def, r, &g_list)) {
          HitRecord* rec = &g_list.rec[g_list.count];
          const int32_t cls = classify(def, rec, from, &s->start, &len, &bounce_len, &bounce_dir);
          sound(def, rec, soil_of(def, rec->material), 1.0f);
          acc = acc + len;
          if (cls == 0) {
            if (def->flags & kDesignate) {
              b->timer = dt + b->timer;
              if (!designate_held(b->timer, def->detonation_distance_sq)) {
                apply_damage(def, b->owner, from, rec, 0.0f, nullptr);
                set_active(b->light, 1);
                if (rh().light_position) rh().light_position(b->light, rec->point.x, rec->point.y, rec->point.z);
              } else {
                Projectile t = stand_in();
                t.owner = b->owner;
                t.position = *b->origin;
                t.direction = *b->dir;
                t.damage_scale = b->damage_scale;
                impact(def, rec, &t, nullptr);
                b->timer = 0;
              }
            } else {
              b->length = acc;  // the beam stops at what it burns
              const float dmg = beam_damage(def, b);
              if (!eq0(dmg)) apply_damage(def, b->owner, from, rec, dmg, nullptr);
            }
          } else {
            const int32_t soil = soil_of(def, rec->material);
            if (def->flags & kDesignate) {
              const float t = b->timer - dt;
              b->timer = t;
              if (!(t > 0.0f)) b->timer = 0;
              else sound_frequency(b->sound, t / def->detonation_distance_sq);
            } else {
              if ((b->flags & 1) && def->impacts && def->impacts[soil].animation) play_at(def->impacts[soil].animation, rec->point);
              b->timer = dt + b->timer;
            }
          }
          s->start = rec->point;
          if (cls == 1) {
            dir = &bounce_dir;
            from = &s->start;
            bounced = true;
          }
        } else if (def->flags & kDesignate) {
          sound_frequency(b->sound, 0.0f);
          b->timer = 0;
          set_active(b->light, 0);
        }
        set_scale(s->node, 1.0f, 1.0f, len);
        ++s;
      } while (bounced && n < b->segment_count);
    }
    for (int32_t i = n; i < b->active; ++i) set_active(b->segments[i].node, 0);
    b->active = n;
  }
}

namespace {

// The MULTI_TARGET half of 0x005814b0: no shipped weapon has it. With two or
// more locks (four at most) the beam runs down the line of targets, kinked
// at random between them, with a branch to each; with one it kinks once at
// half way; with none it is a straight ray of full reach.
void multi_target(WeaponDef* def, Beam* b, int32_t* count) {
  int32_t k = 0;
  if (b->lock_count) {
    k = *b->lock_count;
    if (k > 8) k = 8;
  }
  BeamSegment* sg = b->segments;
  sg[0].start = *b->origin;
  sg[0].dir = *b->dir;
  int32_t n = 0;
  if (k > 1) {
    // FIX (A): the original's buffer holds 8 distances; more locks overrun it.
    float along[256];
    Vec3f3 far_dir;
    beam_sort_targets(b, along, &far_dir);
    if (k > 4) k = 4;
    for (int32_t i = 0; i < k - 1; ++i) along[i] = along[i] * 0.4f;
    Vec3f3 p = *b->origin;
    for (int32_t i = 0; i < k; ++i) {
      sg[i].start = p;
      add_scaled(sg[i].start, far_dir, along[i], &p);
    }
    const float jitter = along[k - 1] * 0.1f;
    for (int32_t i = 0; i < k - 1; ++i) {
      Vec3f3& q = sg[i + 1].start;
      q.x = rand_unit() * jitter + q.x;
      q.z = rand_unit() * jitter + q.z;
    }
    int32_t i = 0, r = 0;
    for (; i < k - 1; ++i) {
      BeamSegment& a = sg[i];
      BeamSegment& c = sg[k + i];
      a.length = direction(a.start, sg[i + 1].start, &a.dir);
      r = beam_cast(def, b, &a, &sg[i + 1].start);
      beam_draw(&a);
      ++n;
      if (r) break;
      c.start = sg[i + 1].start;
      c.length = direction(c.start, *b->targets[i].at, &c.dir);
      a.length = direction(a.start, c.start, &a.dir);
      beam_cast(def, b, &c, b->targets[i].at);
      beam_draw(&c);
      ++n;
    }
    if (!r) {
      BeamSegment& a = sg[i];
      a.length = direction(a.start, *b->targets[i].at, &a.dir);
      beam_cast(def, b, &a, b->targets[i].at);
      beam_draw(&a);
      ++n;
    }
  } else if (k == 1) {
    const Vec3f3* at = b->targets[0].at;
    sg[0].length = direction(sg[0].start, *at, &sg[0].dir) * 0.5f;
    add_scaled(sg[0].start, sg[0].dir, sg[0].length, &sg[1].start);
    const float jitter = sg[0].length * 0.2f;
    sg[1].start.x = rand_unit() * jitter + sg[1].start.x;
    sg[1].start.z = rand_unit() * jitter + sg[1].start.z;
    sg[0].length = direction(sg[0].start, sg[1].start, &sg[0].dir);
    const int32_t r = beam_cast(def, b, &sg[0], &sg[1].start);
    beam_draw(&sg[0]);
    n = 1;
    if (!r) {
      sg[1].length = direction(sg[1].start, *at, &sg[1].dir);
      beam_cast(def, b, &sg[1], at);
      beam_draw(&sg[1]);
      n = 2;
    }
  } else {
    const float reach = b->range_scale * def->range;
    sg[0].length = reach;
    Vec3f3 p;
    add_scaled(sg[0].start, sg[0].dir, reach, &p);
    beam_cast(def, b, &sg[0], &p);
    beam_draw(&sg[0]);
    n = 1;
  }
  *count = n;
}

}  // namespace

}  // namespace zwep
