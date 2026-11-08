// zWeapon's projectiles: the pool, the spawn, the frame (tick, update,
// homing, collision, commit), mines and detonations, and the impact light
// pool (docs/weapons.md, "Projectiles"). The file name is not in the exe;
// these sit around zwep_init.c (0x00583880).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "crt/msvc_rand.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zweapon/zweapon.h"

#pragma STDC FP_CONTRACT OFF

namespace zwep {

namespace {
RunHooks g_rh;
RunState g_rs;
HitList g_seg;      // 0x007301c0  the moving query's hits (no static cast)
HitList g_dynamic;  // 0x0072fcb8  the moving query's hits beside the static cast

zmth::Vec3 v3(const Vec3f3& v) { return {v.x, v.y, v.z}; }
Vec3f3 f3(const zmth::Vec3& v) { return {v.x, v.y, v.z}; }

// The x87 tests the code makes: fcomp then test ah,0x40 (equal, or NaN).
bool eq0(float v) { return !(v < 0.0f) && !(v > 0.0f); }

float d2(const Vec3f3& a, const Vec3f3& b) {  // 0x0051dfa0
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return (dx * dx + dy * dy) + dz * dz;
}
void add_scaled(const Vec3f3& base, const Vec3f3& dir, float s, Vec3f3* out) {  // 0x0051e0e0
  zmth::Vec3 o;
  zmth::add_scaled(v3(base), v3(dir), s, &o);
  *out = f3(o);
}
float norm(Vec3f3* v) {  // 0x004020d0
  zmth::Vec3 t = v3(*v);
  const float len = zmth::normalize(t);
  *v = f3(t);
  return len;
}
// The node rotation a direction gives: pitch asin(y), yaw atan2(-x, -z).
float pitch_of(const Vec3f3& d) { return static_cast<float>(std::asin(static_cast<double>(d.y))); }
float yaw_of(const Vec3f3& d) {
  return static_cast<float>(std::atan2(-static_cast<double>(d.x), -static_cast<double>(d.z)));
}
float rand_angle() {  // ((rand / 32767) - 0.5) pi
  float r = static_cast<float>(crt::msvc_rand());
  r = r * 3.05185094e-05f;
  r = r - 0.5f;
  return r * 3.14159274f;
}

int32_t parents(Handle n) { return g_rh.parent_count ? g_rh.parent_count(n) : 0; }
int32_t children(Handle n) { return g_rh.child_count ? g_rh.child_count(n) : 0; }
void remove_child(Handle parent, Handle child) {
  if (g_rh.remove_child) g_rh.remove_child(parent, child);
}
Handle link_of(Handle n) { return g_rh.link ? g_rh.link(n) : nullptr; }
void set_link(Handle n, Handle next) {
  if (g_rh.set_link) g_rh.set_link(n, next);
}
void set_intersect(Handle n, int32_t on) {
  if (hit_hooks().set_intersect) hit_hooks().set_intersect(n, on);
}
void stop_anim(Handle* a) {
  if (*a) {
    if (g_rh.anim_stop) g_rh.anim_stop(*a);
    *a = nullptr;
  }
}
void set_rotation(Handle n, float x, float y, float z) {
  if (g_rh.set_rotation) g_rh.set_rotation(n, x, y, z);
}
void set_translation(Handle n, const Vec3f3& p) {
  if (g_rh.set_translation) g_rh.set_translation(n, p.x, p.y, p.z);
}
void set_scale(Handle n, float x, float y, float z) {
  if (g_rh.set_scale) g_rh.set_scale(n, x, y, z);
}
void spin(Handle n) {  // 200 degrees a second about y
  if (g_rh.rotate) g_rh.rotate(n, 0.0f, hit_state().dt * 3.49065852f, 0.0f);
}
void play_at(Handle anim, const Vec3f3& p) {  // 0x00508aa0 at a point, unturned
  const float a[9] = {p.x, p.y, p.z, 0, 0, 0, 0, 0, 0};
  if (hit_hooks().effect) hit_hooks().effect(anim, a);
}
int32_t segment(const Vec3f3* from, const Vec3f3* to, HitList* out) {
  return g_rh.segment ? g_rh.segment(from, to, out) : 1;
}
void impact_type(int32_t t) {
  if (g_rh.impact_type) g_rh.impact_type(t);
}
void query_mask(uint32_t m) {
  if (g_rh.query_mask) g_rh.query_mask(m);
}
// A zeroed stand-in shot (the stack copies 0x005820c0 and 0x005822c0 make).
Projectile zeroed() {
  Projectile p;
  p.health = 0;
  p.track_scale[0] = p.track_scale[1] = 0;
  return p;
}
}  // namespace

RunHooks& run_hooks() { return g_rh; }
RunState& run_state() { return g_rs; }

uint32_t query_zone() {
  uint32_t z;
  std::memcpy(&z, zmod::zones().list, 4);
  return z;
}
void set_query_zone(uint32_t zone) { std::memcpy(zmod::zones().list, &zone, 4); }

// Mech3.exe 1.2 @ 0x005830b0: a spare copy of the FLYOUT model, or a new one.
Handle model_take(WeaponDef* def) {
  if (Handle m = def->model_pool) {
    def->model_pool = link_of(m);
    set_link(m, nullptr);
    return m;
  }
  return g_rh.clone ? g_rh.clone(def->flyout.model) : nullptr;
}

// Mech3.exe 1.2 @ 0x005830e0
void model_return(WeaponDef* def, Projectile* p) {
  if (p->model_anim && g_rh.anim_stop) g_rh.anim_stop(p->model_anim);
  if (g_rh.obj_remove_child) g_rh.obj_remove_child(p->node, p->model);
  set_link(p->model, def->model_pool);
  def->model_pool = p->model;
  p->model = nullptr;
}

// Mech3.exe 1.2 @ 0x00583120: the model animation's end callback.
void model_anim_done(Projectile* p) { p->model_anim = nullptr; }

// Mech3.exe 1.2 @ 0x00584df0: a slot off the free list, with the FLYOUT
// model under its node (a pooled copy when the model is animated).
Projectile* pool_take(WeaponDef* def) {
  State& s = state();
  Projectile* p = s.free_list;
  if (!p) return nullptr;
  s.free_list = p->next;
  if (Handle m = def->flyout.model) {
    if (def->flyout.model_animation) {
      p->model = model_take(def);
      m = p->model;
    }
    if (g_rh.obj_add_child) g_rh.obj_add_child(p->node, m);
  }
  p->state = 1;
  p->speed = 0;
  p->tick = nullptr;
  p->static_hits = 0;
  return p;
}

// Mech3.exe 1.2 @ 0x00584e60: back on the free list, once stopped.
void pool_release(WeaponDef* def, Projectile* p) {
  if (p->speed > 0.0f) return;
  while (parents(p->node)) remove_child(g_rh.first_parent(p->node), p->node);
  if (Handle m = def->flyout.model) {
    if (!p->model) {
      if (children(p->node)) remove_child(p->node, m);
    } else {
      model_return(def, p);
    }
  }
  while (children(p->node)) remove_child(p->node, g_rh.first_child(p->node));
  State& s = state();
  p->next = s.free_list;
  s.free_list = p;
  set_scale(p->node, 1.0f, 1.0f, 1.0f);
  set_rotation(p->node, 0.0f, 0.0f, 0.0f);
  set_translation(p->node, {0.0f, 0.0f, 0.0f});
  if (g_rh.clear_handler) g_rh.clear_handler(p->node);
}

// Mech3.exe 1.2 @ 0x00584f40
void kill(WeaponDef* def, Projectile* p) {
  p->speed = 0;
  stop_anim(&p->flyout_anim);
  stop_anim(&p->flyout_attached);
  if (p->model) model_return(def, p);
  if (parents(p->node)) remove_child(state().world, p->node);
  pool_release(def, p);
}

// Mech3.exe 1.2 @ 0x00584fb0
void kill_all(WeaponDef* def) {
  Projectile* p = def->active;
  def->active = nullptr;
  while (p) {
    Projectile* n = p->next;
    kill(def, p);
    p = n;
  }
}

// Mech3.exe 1.2 @ 0x00584fe0. FIX (A): the original runs this on the BEAM
// defs too, taking a firing beam (0x150 bytes) for a projectile (0x5d8):
// it writes past the beam and stops its sound handles as animations. Beams
// are left to their weapons here (docs/fixes.md).
void kill_all() {
  for (WeaponDef& d : state().defs)
    if (!(d.flags & kBeam)) kill_all(&d);
}

// Mech3.exe 1.2 @ 0x005847c0: the pool's scene nodes (and the impact lights)
// made (1) or freed (0).
int32_t pool_objects(int32_t create) {
  State& s = state();
  if (create == 0 && g_rs.pool_objects) {
    kill_all();
    for (WeaponDef& d : s.defs) {
      while (Handle m = d.model_pool) {
        d.model_pool = link_of(m);
        if (g_rh.node_free) g_rh.node_free(m);
      }
    }
    for (Projectile& p : s.pool) {
      if (Handle n = p.node) {
        if (parents(n) == 1) remove_child(g_rh.first_parent(n), n);
        if (g_rh.node_free) g_rh.node_free(n);
      }
    }
    lights_free();
    g_rs.pool_objects = 0;
    return 0;
  }
  if (create == 1 && !g_rs.pool_objects) {
    for (size_t i = 0; i < s.pool.size(); ++i) {
      Projectile& p = s.pool[i];
      p.node = g_rh.object3d_create ? g_rh.object3d_create() : nullptr;
      char name[32];
      std::snprintf(name, sizeof name, "Projectile_%d", static_cast<int>(i));
      if (g_rh.set_name) g_rh.set_name(p.node, name);
      p.flyout_anim = p.flyout_attached = p.model_anim = nullptr;
      set_intersect(p.node, 0);
      if (g_rh.set_flag_4eb620) g_rh.set_flag_4eb620(p.node, 0);
      if (g_rh.set_flag_4eb750) g_rh.set_flag_4eb750(p.node, 1);
    }
    lights_init();
    g_rs.pool_objects = 1;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x00585010: a shot leaves the muzzle. The shooter's speed
// carries over; a straight shot casts the static world along its whole range
// once, here. The fire sound, effect and animation play at the muzzle.
Projectile* spawn(WeaponDef* def, Handle owner, const uint32_t* zone, const Vec3f3* muzzle, const Vec3f3* dir,
                  const Vec3f3* velocity, int32_t net_id, Projectile* slot) {
  State& s = state();
  FireState& f = fire_state();
  if (s.init_arg && g_rh.net_gate && g_rh.net_gate(def, &net_id) == 0) return nullptr;
  Projectile* p = slot ? slot : pool_take(def);
  if (!p) return nullptr;
  p->next = def->active;
  def->active = p;
  if (g_rh.add_child) g_rh.add_child(s.world, p->node);
  p->spawn_at = *muzzle;
  p->position = *muzzle;
  p->next_position = *muzzle;
  p->direction = *dir;
  p->owner = owner;
  const uint32_t flags = def->flags;
  if (flags & kRandomDeviation) {
    p->deviation = {0, 0, 0};
    p->drift = {0, 0, 0};
  }
  p->travelled = 0;
  p->age = 0;
  p->net_id = net_id;
  if (zone) p->zone = *zone;
  else p->zone = (p->zone & ~0xffu) | 4;
  p->damage_scale = f.damage_scale;
  p->track_scale[0] = f.track_scale[0];
  p->track_scale[1] = f.track_scale[1];
  p->terrain_follow = f.terrain_follow;
  f.damage_scale = 1;
  f.track_scale[0] = f.track_scale[1] = 1;
  f.terrain_follow = 0;
  p->max_speed = def->velocity;
  if (eq0(def->acceleration) && !(flags & kInstant)) {
    p->speed = def->velocity;
    add_scaled(*velocity, *dir, def->velocity, &p->velocity);
    if (eq0(def->gravity) && !(flags & kLockOn)) {
      p->static_hits = 1;
      p->static_frame = g_rs.frame;
      p->static_next = 0;
      Vec3f3 end;
      add_scaled(p->position, *dir, def->range, &end);
      hide_shooter(def, p);
      if (g_rh.query_static) g_rh.query_static(1);
      const int32_t r = g_rh.segment_list ? g_rh.segment_list(&p->position, &end, &p->statics) : 1;
      if (g_rh.query_static) g_rh.query_static(0);
      show_shooter(def, p);
      HitList& l = p->statics;
      if (r == 1) {
        l.count = 0;
      } else {
        for (int i = 0; i < l.count; ++i) l.rec[i].distance_sq = d2(p->position, l.rec[i].point);
        // An insertion sort that compares the shifted record with the one
        // before it instead of with the record being placed: the hits are not
        // always left in order (docs/weapons.md, "Static hits").
        for (int i = 1; i < l.count; ++i) {
          if (!(l.rec[i - 1].distance_sq > l.rec[i].distance_sq)) continue;
          const HitRecord t = l.rec[i];
          l.rec[i] = l.rec[i - 1];
          int j = i - 1;
          while (j > 0 && l.rec[j - 1].distance_sq > l.rec[j].distance_sq) {
            l.rec[j] = l.rec[j - 1];
            --j;
          }
          l.rec[j] = t;
        }
      }
    }
  } else {
    p->speed = 0.0001f;
    p->velocity = *velocity;
    const Vec3f3& v = p->velocity;
    const float len = std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z);
    p->max_speed = len + p->max_speed;
    p->speed = len + p->speed;
    const Vec3f3 t{len * dir->x, dir->y * len, dir->z * len};
    p->velocity = {v.x - t.x, v.y - t.y, v.z - t.z};
  }
  Effect& fire = def->fire;
  if (fire.sound_choice != -1 && g_rh.sound_at) g_rh.sound_at(fire.sounds[fire.sound_choice], &p->position, 1.0f);
  if (fire.effect) {
    if (hit_hooks().impact_effect) hit_hooks().impact_effect(fire.effect, &p->position);
  } else if (fire.animation_choice != -1) {
    Handle a = fire.animation_choice == 0 ? fire.animation : fire.animation2;
    if (a) {
      const float roll = (fire.flags & 1) ? rand_angle() : 0.0f;
      if (fire.attach_node) {
        if (g_rh.anim_attached) g_rh.anim_attached(a, fire.attach_node, fire.attach_offset);
        fire.attach_node = nullptr;
      } else {
        const float yaw = yaw_of(*dir);
        const float args[9] = {p->position.x, p->position.y, p->position.z, pitch_of(*dir), yaw, roll, 0, 0, 0};
        if (hit_hooks().effect) hit_hooks().effect(a, args);
      }
    }
  }
  const Effect& fly = def->flyout;
  if (!(flags & kMine) && (((flags & kFlyoutUnlit) && fly.model) || (fly.animation && !fly.model))) {
    const float yaw = yaw_of(*dir);
    set_rotation(p->node, pitch_of(*dir), yaw, 0.0f);
  }
  set_translation(p->node, p->position);
  if (fly.animation_choice != -1) {
    if (fly.animation && g_rh.anim_follow) p->flyout_anim = g_rh.anim_follow(fly.animation, p->node);
    if (fly.animation_attached && g_rh.anim_attached)
      p->flyout_attached = g_rh.anim_attached(fly.animation_attached, p->node, nullptr);
    if (fly.model_animation) {
      p->model_anim = g_rh.anim_model ? g_rh.anim_model(fly.model_animation, p->model) : nullptr;
      if (g_rh.anim_on_done) g_rh.anim_on_done(p->model_anim, p);
    }
  }
  p->base_velocity = {0, 0, 0};
  p->homing_time = 0;
  p->target_at = nullptr;
  p->target_velocity = nullptr;
  if ((flags & kLockOn) && f.lock_target) {
    p->base_velocity = *velocity;
    if (f.lock_flag && *f.lock_flag > 0) {
      p->lock_entity = f.lock_target->entity;
      p->target_at = f.lock_target->at;
      p->target_velocity = f.lock_target->velocity;
    }
    f.lock_flag = nullptr;
  }
  if (flags & kFlyoutHealth) {
    set_intersect(p->node, 1);
    if (g_rh.set_shootable) g_rh.set_shootable(p->node, p);
    p->health = def->flyout_health;
  } else {
    set_intersect(p->node, 0);
    if (g_rh.set_shootable) g_rh.set_shootable(p->node, nullptr);
    p->health = -1.0f;
  }
  fire.sound_choice = 0;
  fire.animation_choice = 0;
  def->flyout.animation_choice = 0;
  return p;
}

// Mech3.exe 1.2 @ 0x005823c0: the weapon_tick action, once a frame: every
// weapon's shots or beams move, then the projectiles' nodes follow. The
// splash queue it drains first is never filled (zweapon.cpp, splash).
void tick() {
  g_rs.tick_zone = query_zone();
  State& s = state();
  for (WeaponDef& d : s.defs) {
    if (!d.key) continue;
    if (d.flags & kBeam) beam_update(&d);
    else update(&d);
  }
  for (WeaponDef& d : s.defs)
    if (d.key && !(d.flags & kBeam)) commit(&d);
  set_query_zone(g_rs.tick_zone);
}

// Mech3.exe 1.2 @ 0x00585710: one frame of every shot of a weapon: shot down,
// a mine lying in wait, or flying (homing, then the step); then what the move
// crosses.
void update(WeaponDef* def) {
  // The original's local keeps the last shot's move: a stopped shot that is
  // not a mine moves by it (none does: speed is 0 only for mines and the dead).
  Vec3f3 move{};
  for (Projectile* p = def->active; p; p = p->next) {
    p->state = 1;
    if (p->tick) p->tick(p);
    set_query_zone((p->zone & 0xff) == 4 ? g_rs.tick_zone : p->zone);
    if (eq0(p->health)) {
      if (def->destroy_animation) play_at(def->destroy_animation, p->position);
      else air_burst(def, p);
      p->state = 0;
    } else if (eq0(p->speed)) {
      if (def->flags & kMine) mine_idle(def, p);
    } else {
      if ((def->flags & kLockOn) && p->target_at) home(def, p);
      if (!(def->flags & kInstant)) {
        step(def, p, &move);
      } else {
        const float r = def->range;
        move = {r * p->direction.x, p->direction.y * r, p->direction.z * r};
      }
    }
    if (p->state == 1) {
      p->next_position = {p->position.x + move.x, p->position.y + move.y, p->position.z + move.z};
      hide_shooter(def, p);
      if (HitRecord* h = collide(def, p, &p->next_position)) {
        if (!(def->flags & kMine)) hit(def, p, h);
        else mine_land(def, p, h, &p->next_position);
      }
      show_shooter(def, p);
    }
    if (def->flags & kInstant) p->state = 0;
  }
}

// Mech3.exe 1.2 @ 0x005858b0: the dead leave the list, the flying move.
void commit(WeaponDef* def) {
  Projectile** link = &def->active;
  Projectile* p = *link;
  while (p) {
    if (p->state == 0) {
      *link = p->next;
      retire(def, p);
      p = *link;
    } else {
      if (p->state == 1) commit_one(def, p);
      link = &p->next;
      p = p->next;
    }
  }
}

// Mech3.exe 1.2 @ 0x005864c0: the node follows the shot. It is put where the
// shot was at the frame's start (a constant 1 at 0x005cec0c picks that over
// the move's end), so what is drawn trails by a frame.
void commit_one(WeaponDef* def, Projectile* p) {
  const uint32_t mine = def->flags & kMine;
  if (!mine && !eq0(def->gravity) && !(def->flags & kFixedRotate)) {
    Vec3f3 v = p->velocity;
    norm(&v);
    const float yaw = yaw_of(v);
    set_rotation(p->node, pitch_of(v), yaw, 0.0f);
  } else if (mine) {
    if (p->age > 0.0f) spin(p->node);
    if (!(p->age >= 1.0f)) {
      const float s = 1.0f - p->age * -4.0f;  // a laid mine grows to 5 in its first second
      set_scale(p->node, s, s, s);
    }
  }
  p->age = hit_state().dt + p->age;
  set_translation(p->node, p->position);
  p->position = p->next_position;
}

// Mech3.exe 1.2 @ 0x00586600: a dead shot back to the pool. A TETHER_GUIDED
// shot is not returned: its slot is lost for the mission (docs/weapons.md).
void retire(WeaponDef* def, Projectile* p) {
  p->speed = 0;
  p->state = 1;
  stop_anim(&p->flyout_anim);
  stop_anim(&p->flyout_attached);
  remove_child(state().world, p->node);
  if (!(def->flags & kTetherGuided)) pool_release(def, p);
  else p->owner = nullptr;
}

// Mech3.exe 1.2 @ 0x00585900: a locked shot turns toward its target, at most
// TURN_RATE x factor a second; the factor is 0 until TURN_SUSPEND_TIME, then
// (t - suspend) / LOCK_ON, or 1 with no suspend time. Tethered and FCS shots
// follow the ground. Each turn costs speed; the shooter's speed fades out over
// LOCK_ON.
void home(WeaponDef* def, Projectile* p) {
  const FireState& f = fire_state();
  const float dt = hit_state().dt;
  zmth::Vec3 want;
  zmth::direction(v3(p->position), v3(*p->target_at), want);
  if ((def->flags & kLockOnLead) && p->age >= def->lock_on_lead_min && p->target_velocity) {
    zmth::Vec3 lead;
    if (zmth::intercept(v3(p->position), v3(*p->target_at), p->speed, v3(*p->target_velocity), &lead)) {
      if (!(p->age < def->lock_on_lead_max)) {
        want = lead;
      } else {
        zmth::Vec3 o;
        zmth::slerp(want, lead, (p->age - def->lock_on_lead_min) * def->lock_on_lead_span, &o);
        want = o;
      }
    }
  }
  float factor;
  if (!(p->homing_time >= def->turn_suspend_time)) factor = 0.0f;
  else if (def->lock_on + def->turn_suspend_time > def->lock_on)
    factor = (p->homing_time - def->turn_suspend_time) / def->lock_on;
  else factor = 1.0f;
  // The exe reads the global, which spawn has reset to 1, not the shot's copy
  // (+0x5c0, 2 when fired locked with Artemis FCS): FIX (bug) the copy
  // (fixes.md, docs/weapons.md Homing).
  const float track = zfix::options().bugs ? p->track_scale[0] : f.track_scale[0];
  const float turn = def->turn_rate * track * factor * dt;
  if ((def->flags & kTetherGuided) || p->terrain_follow) {
    HitList g;
    if (g_rh.ground) g_rh.ground(&p->position, &g);
    if (g.count == 1) {
      const float h = p->position.y - g.rec[0].point.y;
      def->terrain_height = h;
      if (def->flags & kTetherGuided) {
        if (h > hit_state().tether_ceiling - 5.0f && want.y > 0.0f) want.y = 0.0f;
      } else if (!(h >= 10.0f)) {
        // FCS: low over the ground, climb if the line to the target is blocked
        // by something with no damage handler.
        if (def->flags & kFlyoutHealth) set_intersect(p->node, 0);
        HitList q;
        const int32_t r = segment(&p->position, p->target_at, &q);
        if (def->flags & kFlyoutHealth) set_intersect(p->node, 1);
        const uintptr_t hd = hit_hooks().node_handler ? hit_hooks().node_handler(q.rec[q.count].node) : 0;
        if (r == 0 && hd <= 1) {
          want.y = want.y - def->pitch_rate * track * -0.636619925f;
          zmth::normalize(want);
        }
      }
    } else {
      p->travelled = def->range;  // nothing under it: gone next step
    }
  }
  Vec3f3& d = p->direction;
  const float c = (d.x * want.x + d.y * want.y) + d.z * want.z;
  float cc = c;
  if (c > 1.0f) cc = 1.0f;
  else if (!(c >= 0.0f)) cc = 0.0f;  // beyond 90 degrees counts as 90
  const float angle = static_cast<float>(std::acos(static_cast<double>(cc)));
  if (angle > 0.0f) {
    float a;
    if (!(turn < angle)) {
      d = f3(want);
      a = angle;
    } else {
      zmth::Vec3 o;
      zmth::slerp(v3(d), want, turn / angle, &o);
      d = f3(o);
      norm(&d);
      a = turn;
    }
    const float co = static_cast<float>(std::cos(static_cast<double>(a)));
    p->speed = (0.8f - co * -0.2f) * p->speed;
  }
  const float s = p->speed;
  if (!(p->homing_time <= def->lock_on)) {
    p->velocity = {s * d.x, d.y * s, d.z * s};
  } else {
    const float k = (def->lock_on - p->homing_time) / def->lock_on;
    p->homing_time = dt + p->homing_time;
    const Vec3f3& b = p->base_velocity;
    p->velocity = {d.x * p->speed + b.x * k, d.y * p->speed + b.y * k, d.z * p->speed + b.z * k};
  }
  const float yaw = yaw_of(d);
  set_rotation(p->node, pitch_of(d), yaw, 0.0f);
}

// Mech3.exe 1.2 @ 0x00586160: before a shot's query its shooter (unless
// HIT_OWNER and older than half a second) and a shootable shot's own node are
// taken out, and the query sees the 0x40000 class mask.
void hide_shooter(WeaponDef* def, Projectile* p) {
  if (!(def->flags & kHitOwner) || !(p->age > 0.5f)) {
    g_rs.owner_hidden = 1;
    set_intersect(p->owner, 0);
  } else {
    g_rs.owner_hidden = 0;
  }
  if (def->flags & kFlyoutHealth) set_intersect(p->node, 0);
  query_mask(0x40000);
}

// Mech3.exe 1.2 @ 0x005861c0
void show_shooter(WeaponDef* def, Projectile* p) {
  if (def->flags & kFlyoutHealth) set_intersect(p->node, 1);
  if (g_rs.owner_hidden) set_intersect(p->owner, 1);
}

// Mech3.exe 1.2 @ 0x005861f0: what this frame's move hits. A straight shot
// takes its next static hit once the distance travelled reaches it (one a
// frame, skipping nodes that moved since the cast), and queries only what moved
// since then; the nearer of the two wins.
HitRecord* collide(WeaponDef* def, Projectile* p, const Vec3f3* to) {
  impact_type(def->impact_type);
  if (!p->static_hits) {
    const int32_t r = segment(&p->position, to, &g_seg);
    return pick(def, r, &g_seg);
  }
  HitRecord* st = nullptr;
  HitList& l = p->statics;
  const uint8_t n = l.count;
  while (p->static_next < n) {
    HitRecord& rec = l.rec[p->static_next];
    if (rec.node && g_rh.node_frame && g_rh.node_frame(rec.node) > static_cast<int32_t>(p->static_frame)) {
      ++p->static_next;
      continue;
    }
    if (!(p->travelled * p->travelled < rec.distance_sq)) {
      l.count = static_cast<uint8_t>(p->static_next);
      st = pick(def, 0, &l);
      ++p->static_next;
      l.count = n;
    }
    break;
  }
  if (g_rh.query_since) g_rh.query_since(1, p->static_frame);
  const int32_t r = segment(&p->position, to, &g_dynamic);
  if (g_rh.query_since) g_rh.query_since(0, 0xffffffffu);
  HitRecord* dyn = pick(def, r, &g_dynamic);
  if (!st) return dyn;
  if (!dyn) return st;
  dyn->distance_sq = d2(p->position, dyn->point);
  st->distance_sq = d2(p->position, st->point);
  if (!(st->distance_sq > dyn->distance_sq)) return st;
  return dyn;
}

// Mech3.exe 1.2 @ 0x00583130: the record a query's answer hits. Water stops
// only DETONATE_ON_WATER shots; the others splash (the water animation) and
// take the nearest record that is not water, if the list holds more.
HitRecord* pick(WeaponDef* def, int32_t status, HitList* list) {
  if (status != 0) return nullptr;
  HitRecord* r = &list->rec[list->count];
  if (def->flags & kDetonateOnWater) return r;
  if (soil_of(def, r->material) != kSoilWater) return r;
  if (def->impacts && def->impact_count > kSoilWater && def->impacts[kSoilWater].animation) {
    play_at(def->impacts[kSoilWater].animation, r->point);
  }
  const int n = list->total;
  if (n == 1) return nullptr;
  HitRecord* best = nullptr;
  double bd = HUGE_VAL;
  for (int i = list->count == 0 ? 1 : 0; i < n; ++i) {
    HitRecord* q = &list->rec[i];
    if (soil_of(def, q->material) != kSoilWater && q != r && bd > static_cast<double>(q->distance_sq)) {
      bd = q->distance_sq;
      list->count = static_cast<uint8_t>(i);
      best = q;
    }
  }
  return best;
}

// Mech3.exe 1.2 @ 0x00582b10: 0 the record is something with a damage handler
// (and records the shot line), 1 a BOUNCES shot bounces off it (the reflected
// direction and the move's length), 2 it passes.
int32_t classify(WeaponDef* def, HitRecord* rec, const Vec3f3* from, const Vec3f3* to, float* len,
                 float* bounce_len, Vec3f3* bounce_dir) {
  const float l = zmth::distance(v3(rec->point), v3(*from));
  *len = l;
  if (eq0(l)) return 2;
  const uintptr_t hd = hit_hooks().node_handler ? hit_hooks().node_handler(rec->node) : 0;
  if (!(def->flags & kRemoteDetonate) && hd != 0) {
    HitState& h = hit_state();
    if (h.record_shot == 1) {
      h.shot_from = *from;
      h.shot_to = *to;
    }
    return 0;
  }
  if (def->flags & kBounces) {
    const zmth::Vec3 d{to->x - from->x, to->y - from->y, to->z - from->z};
    zmth::Vec3 o;
    zmth::reflect(v3(rec->normal), d, &o);
    *bounce_len = zmth::normalize(o);
    *bounce_dir = f3(o);
    return 1;
  }
  return 2;
}

// Mech3.exe 1.2 @ 0x00586670: a laid mine waits, spinning, until something
// other than its layer comes within IMPACT_PROXIMITY (REMOTE_DETONATE mines
// only by command); in a network game it also goes off when its time is up.
void mine_idle(WeaponDef* def, Projectile* p) {
  bool go = false;
  HitList list;
  p->state = 2;
  if (state().init_arg && !(g_rs.net_clock <= p->homing_time)) {
    splash_query(def, p, 0, &list);
    go = true;
  } else if (!(def->flags & kRemoteDetonate)) {
    go = splash_query(def, p, 0, &list) != 0;
  }
  if (go) {
    splash(def, p, &list, 0);
    p->state = 0;
    HitState& h = hit_state();
    h.splash_enabled = 0;
    ground_burst(def, p);
    h.splash_enabled = 1;
    return;
  }
  spin(p->node);
}

// Mech3.exe 1.2 @ 0x00586740: a thrown mine meets the ground: it bounces at a
// quarter of its speed until it is slower than 0.2, then lies down (scaled to
// 5); a thing with a damage handler sets it off.
void mine_land(WeaponDef* def, Projectile* p, HitRecord* rec, Vec3f3* to) {
  const int32_t soil = soil_of(def, rec->material);
  HitList list;
  // The bounce length is the last one used (halved): a record at the shot's
  // own position passes (2) without one (none the first time: here 0).
  float len, blen = 0;
  for (;;) {
    const int32_t r = classify(def, rec, &p->position, to, &len, &blen, &p->direction);
    if (r == 0) {
      p->state = 0;
      impact(def, rec, p, nullptr);
      return;
    }
    if (!(p->speed > 0.2f)) {
      p->speed = 0;
      if (state().init_arg) p->homing_time = (g_rs.net_clock + p->homing_time) - -120.0f;  // two minutes
      p->state = 2;
      p->travelled = def->range;
      *to = rec->point;
      spin(p->node);
      set_scale(p->node, 5.0f, 5.0f, 5.0f);
      return;
    }
    const float old = p->speed;
    p->speed = 0.25f * p->speed;
    blen = blen * 0.5f;  // halved in place: a later pass without a bounce reuses it
    const float s = p->speed;
    p->velocity = {p->direction.x * s, p->direction.y * s, p->direction.z * s};
    add_scaled(rec->point, p->direction, blen, to);
    if (old > 0.2f) bounce_sound(def, rec, soil, (old / p->max_speed - -1.0f) * 0.5f);
    impact_type(def->impact_type);
    query_mask(0x40000);
    if (segment(&rec->point, to, &list) != 0) return;
    p->position = rec->point;
    rec = &list.rec[list.count];
  }
}

// Mech3.exe 1.2 @ 0x005821c0: a shot goes off where it lies: an impact on
// what is under it (from 1 m above down to a tenth of IMPACT_PROXIMITY below),
// then splash from 1 m above its position, which it keeps.
int32_t ground_burst(WeaponDef* def, Projectile* p) {
  Vec3f3 from = p->position;
  from.y = from.y - -1.0f;
  Vec3f3 to = p->position;
  to.y = to.y - def->impact_proximity * 0.1f;
  int32_t hit = 0;
  HitList list;
  if (segment(&from, &to, &list) == 0) {
    impact(def, &list.rec[list.count], p, nullptr);
    hit = 1;
  }
  if (hit_state().splash_enabled && def->impact_proximity > 0.0f) {
    HitList near;
    if (splash_query(def, p, 1, &near)) {
      p->position = from;
      splash(def, p, &near, 0);
    }
  }
  return hit;
}

// Mech3.exe 1.2 @ 0x005820c0: REMOTE_DETONATE. With no point every stopped
// shot of the weapon (of `owner`, or of anyone) goes off; with one, a burst
// there. The network hears of any that hit.
int32_t detonate(WeaponDef* def, const Vec3f3* at, Handle owner) {
  int32_t n = 0;
  if (!at) {
    Projectile** link = &def->active;
    Projectile* p = def->active;
    while (p) {
      Projectile* next = p->next;
      if (eq0(p->speed) && (!owner || p->owner == owner)) {
        *link = next;
        n += ground_burst(def, p);
        kill(def, p);
      } else {
        link = &p->next;
      }
      p = next;
    }
  } else {
    Projectile t = zeroed();
    t.owner = owner;
    t.position = *at;
    n = ground_burst(def, &t);
  }
  if (n && g_rh.net_detonated) g_rh.net_detonated(def, at, owner);
  return n;
}

// Mech3.exe 1.2 @ 0x005822c0: a burst at a point, by no one.
int32_t burst_at(WeaponDef* def, const Vec3f3* at) {
  Projectile t = zeroed();
  t.position = *at;
  return ground_burst(def, &t);
}

// Mech3.exe 1.2 @ 0x00581ff0 (a second argument is unused): the slope to aim
// a GRAVITY shot at, or -1; the distance by the bit-trick square root.
float aim_slope(WeaponDef* def, const Vec3f3* from, const Vec3f3* to, float* dist) {
  const Vec3f3 d{to->x - from->x, to->y - from->y, to->z - from->z};
  const float sq = (d.x * d.x + d.y * d.y) + d.z * d.z;
  int32_t bits;
  std::memcpy(&bits, &sq, 4);
  bits = (bits >> 1) + 0x1fc00000;
  float r;
  std::memcpy(&r, &bits, 4);
  *dist = r;
  if (!eq0(def->gravity)) {
    if (r < def->range) return d.y / r - r / def->range * -0.24f;
    if (def->flags & kMine) return d.y / r - -0.24f;
  }
  return -1.0f;
}

// Mech3.exe 1.2 @ 0x00581e50
void pool_stats(PoolStats* out) {
  State& s = state();
  *out = {static_cast<int32_t>(s.pool.size()), 0, 0, 0};
  for (Projectile* p = s.free_list; p; p = p->next) ++out->free;
  for (WeaponDef& d : s.defs)
    for (Projectile* p = d.active; p; p = p->next) ++out->flying;
}

// Mech3.exe 1.2 @ 0x00582f60: one of the soil's impact sounds, at random.
void sound(WeaponDef* def, HitRecord* rec, int32_t soil, float volume) {
  if (!def || !def->impacts || !rec) return;
  const int32_t checked = checked_soil(def, soil);
  const Effect& e = def->impacts[checked];
  if (!e.sound_count) return;
  const uint32_t i = (static_cast<uint32_t>(crt::msvc_rand()) * static_cast<uint32_t>(e.sound_count)) >> 15;
  if (g_rh.sound_at) g_rh.sound_at(e.sounds[i], &rec->point, volume);
}

// Mech3.exe 1.2 @ 0x00582fc0: likewise a bounce sound.
void bounce_sound(WeaponDef* def, HitRecord* rec, int32_t soil, float volume) {
  if (!def || !def->impacts || !rec) return;
  const int32_t checked = checked_soil(def, soil);
  const Effect& e = def->impacts[checked];
  if (!e.bounce_sound_count) return;
  const uint32_t i = (static_cast<uint32_t>(crt::msvc_rand()) * static_cast<uint32_t>(e.bounce_sound_count)) >> 15;
  if (g_rh.sound_at) g_rh.sound_at(e.bounce_sounds[i], &rec->point, volume);
}

// Mech3.exe 1.2 @ 0x00586970: eight "Thermal glow" lights at the origin, range
// 0.1 to 0.2, on a list through node +0x40.
int32_t lights_init() {
  for (int i = 0; i < 8; ++i) {
    Handle l = g_rh.light_create ? g_rh.light_create() : nullptr;
    if (g_rh.set_name) g_rh.set_name(l, "Thermal glow");
    if (g_rh.light_position) g_rh.light_position(l, 0.0f, 0.0f, 0.0f);
    if (g_rh.light_range) g_rh.light_range(l, 0.1f, 0.2f);
    set_link(l, g_rs.lights);
    g_rs.lights = l;
  }
  return 1;
}

// Mech3.exe 1.2 @ 0x005869f0: the lights destroyed, if the node pool is still there.
int32_t lights_free() {
  Handle l = g_rs.lights;
  if (g_rh.pool_live && g_rh.pool_live() && l) {
    do {
      Handle next = link_of(l);
      set_link(l, nullptr);
      if (g_rh.node_destroy) g_rh.node_destroy(l);
      l = next;
    } while (l);
  }
  g_rs.lights = nullptr;
  return 1;
}

// Mech3.exe 1.2 @ 0x00586b20: a light off the list, coloured and put in the world.
Handle light_take(const Vec3f3* color) {
  Handle l = g_rs.lights;
  if (!l) return nullptr;
  g_rs.lights = link_of(l);
  if (g_rh.light_range) g_rh.light_range(l, 0.1f, 0.2f);
  if (g_rh.light_color) g_rh.light_color(l, color->x, color->y, color->z);
  if (g_rh.world_add_light) g_rh.world_add_light(l);
  return l;
}

// Mech3.exe 1.2 @ 0x005869d0
void Glow::clear() {
  light = nullptr, x08 = 0, level = 0;
  flags &= ~3u;
  x14 = 0;
}

// Mech3.exe 1.2 @ 0x00586a30 (ECX the definition, EDX the glow, the stack the
// share). The light keeps the pool's range (0.1 to 0.2) and nothing reads
// the level, so all a hit shows is a light of the colour that reaches little
// past the node's origin (docs/vehicles.md, The glow).
int32_t glow_hit(Glow& g, Handle node, WeaponDef* def, float amount) {
  g.weapon = def;
  g.flags |= 3;
  g.level = (def->flags & kFreeze) ? g.level - amount : amount + g.level;
  if (g.level > 1.0f) g.level = 1.0f;                        // 0x005cec68; a NaN goes on
  else if (!(g.level >= -1.0f)) g.level = -1.0f;              // 0x005cec6c: below or NaN
  if (!g.light) {
    const Vec3f3 color{def->target_heat[3], def->target_heat[4], def->target_heat[5]};
    g.light = light_take(&color);
    if (g.light && g_rh.add_child) g_rh.add_child(node, g.light);  // 0x004eb9b0
  }
  if (!(g.x08 >= -0.5f)) return 2;  // 0x005cec70: below or NaN
  if (!(g.x08 > 0.0f)) return 1;    // 0x005cec74
  return 0;
}

// Mech3.exe 1.2 @ 0x00586af0
void glow_end(Glow& g, Handle node) {
  if (!g.light) return;
  if (g_rh.remove_child) g_rh.remove_child(node, g.light);  // 0x004ebb40
  light_return(g.light);
  g.clear();
}

// Mech3.exe 1.2 @ 0x00586b70
void light_return(Handle l) {
  if (g_rh.light_range) g_rh.light_range(l, 0.1f, 0.2f);
  if (g_rh.world_remove_light) g_rh.world_remove_light(l);
  set_link(l, g_rs.lights);
  g_rs.lights = l;
}

}  // namespace zwep
