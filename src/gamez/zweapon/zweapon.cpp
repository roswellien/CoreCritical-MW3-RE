#include "gamez/zweapon/zweapon.h"

#include <cmath>

#include "crt/msvc_math.h"
#include "crt/msvc_rand.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_draw.h"

#pragma STDC FP_CONTRACT OFF

namespace zwep {

namespace {
FireState g_fire;
HitState g_hit;
HitHooks g_hh;

void play_at(Handle anim, const Vec3f3& p) {
  const float a[9] = {p.x, p.y, p.z, 0, 0, 0, 0, 0, 0};
  if (g_hh.effect) g_hh.effect(anim, a);
}
}  // namespace

FireState& fire_state() { return g_fire; }
HitState& hit_state() { return g_hit; }
HitHooks& hit_hooks() { return g_hh; }

int32_t checked_soil(const WeaponDef* def, int32_t soil) {
  const int32_t limit = def ? def->impact_count : kSoilCount;
  return (soil >= 0 && soil < limit) ? soil : kSoilDefault;
}

int32_t soil_of(const WeaponDef* def, Handle material) {
  const int32_t raw = material && g_hh.soil ? g_hh.soil(material) : kSoilDefault;
  return checked_soil(def, raw);
}

// Mech3.exe 1.2 @ 0x00583020: a beam's damage this frame. Short beams deal it
// once a shot; it falls off as the cosine over the beam's reach, down to
// DAMAGE_AT_MAX_RANGE (lasers 0.75). Under water the reach halves, the damage not.
float beam_damage(WeaponDef* def, Beam* b) {
  const bool timed = (def->flags & kTimedDamage) != 0;
  if (!timed && (b->flags & 2)) return 0.0f;
  float v;
  if (def->damage_at_max_range < 1.0f) {
    const float x = crt::x87_mul_f24(b->length, 3.141592653589793) / (b->range_scale * def->range);
    // fcos leaves an extended result: round once, after adding the 1.
    float f = static_cast<float>(std::cos(static_cast<double>(x)) + 1.0) * 0.5f;
    if (def->damage_at_max_range > f) f = def->damage_at_max_range;
    v = f * def->damage;
  } else {
    v = def->damage;
  }
  if (timed) return v * g_hit.dt * b->damage_scale;
  b->flags |= 2;
  return v * b->damage_scale;
}

// Mech3.exe 1.2 @ 0x00585cc0: one frame of flight into `move`: speed up to
// the maximum, gravity, RANDOM_DEVIATION's drift, the distance travelled,
// and what happens at RANGE or at the lock's DETONATION_DISTANCE.
void step(WeaponDef* def, Projectile* p, Vec3f3* move) {
  const float dt = g_hit.dt;
  p->state = 1;
  bool rebuild = false;
  if (!(def->flags & kMine) && p->speed < p->max_speed) {
    p->speed = def->acceleration * dt + p->speed;
    if (p->max_speed < p->speed) p->speed = p->max_speed;
    if (!(def->flags & kLockOn) || !p->target_at || (def->flags & kTetherGuided)) rebuild = true;
  }
  auto rebuild_velocity = [p] {  // 0x0051e0e0: base + direction x speed
    p->velocity = {p->speed * p->direction.x + p->base_velocity.x, p->direction.y * p->speed + p->base_velocity.y,
                   p->direction.z * p->speed + p->base_velocity.z};
  };
  if (def->gravity != 0.0f) {
    p->velocity.y = p->velocity.y - dt * def->gravity;
    // FIX (bug): here the original passes ECX = 1 as the base velocity (a
    // crash); no shipped weapon has gravity. The base is used (docs/fixes.md).
    if (rebuild) rebuild_velocity();
  } else if (rebuild) {
    rebuild_velocity();
  }
  *move = {p->velocity.x * dt, p->velocity.y * dt, p->velocity.z * dt};
  if (def->flags & kRandomDeviation) {
    bool drift = true;
    if ((def->flags & kLockOn) && p->target_at) {
      const zmth::Vec3 a{p->position.x, p->position.y, p->position.z};
      const zmth::Vec3 b{p->target_at->x, p->target_at->y, p->target_at->z};
      const float d2 = zmth::distance_sq(a, b);
      if (d2 < def->detonation_distance_sq * 4.0f || d2 < 400.0f) drift = false;  // steady near the target
    }
    if (drift) {
      const float r = dt * 5.0f;
      auto rnd = [r] {
        float v = static_cast<float>(crt::msvc_rand()) * 0x1.0002p-15f;  // 1 / 32767
        return (v + v - 1.0f) * r;
      };
      Vec3f3 step_dev;
      step_dev.x = rnd();
      step_dev.y = rnd();
      step_dev.z = rnd();
      const Vec3f3& dv = p->deviation;
      if ((dv.x * step_dev.x + dv.y * step_dev.y) + dv.z * step_dev.z < 0.0f) step_dev = {-step_dev.x, -step_dev.y, -step_dev.z};
      p->deviation = {dv.x + step_dev.x, dv.y + step_dev.y, dv.z + step_dev.z};
      // The drift so far is held within +-1 (y +-0.75) by pushing back.
      const Vec3f3& lim = p->drift;
      if (lim.x > 1.0f) p->deviation.x = p->deviation.x - dt * 10.0f;
      else if (lim.x < -1.0f) p->deviation.x = p->deviation.x - dt * -10.0f;
      if (lim.y > 0.75f) p->deviation.y = p->deviation.y - dt * 10.0f;
      else if (lim.y < -0.75f) p->deviation.y = p->deviation.y - dt * -10.0f;
      if (lim.z > 1.0f) p->deviation.z = p->deviation.z - dt * 10.0f;
      else if (lim.z < -1.0f) p->deviation.z = p->deviation.z - dt * -10.0f;
      const Vec3f3 d{p->deviation.x * dt, p->deviation.y * dt, p->deviation.z * dt};
      p->drift = {p->drift.x + d.x, p->drift.y + d.y, p->drift.z + d.z};
      *move = {move->x + d.x, move->y + d.y, move->z + d.z};
    }
  }
  if ((def->flags & kTetherGuided) && def->terrain_height + move->y > g_hit.tether_ceiling)
    move->y = g_hit.tether_ceiling - def->terrain_height;
  const float len = std::sqrt((move->x * move->x + move->y * move->y) + move->z * move->z);
  p->travelled = len + p->travelled;
  if (def->flags & kMine) p->travelled = p->travelled * 0.5f;
  bool burst = false, gone = false;
  if (!(p->travelled < def->range)) {
    // At RANGE: a homing shot without EXPIRES bursts, so does DETONATE_AT_RANGE;
    // the rest vanish (every shipped missile has EXPIRES).
    if (((def->flags & kLockOn) && !(def->flags & kExpires)) || (def->flags & kDetonateAtRange)) burst = true;
    gone = true;
  } else if ((def->flags & kLockOn) && p->target_at && def->detonation_distance_sq != 0.0f) {
    const zmth::Vec3 a{p->position.x, p->position.y, p->position.z};
    const zmth::Vec3 b{p->target_at->x, p->target_at->y, p->target_at->z};
    if (!(zmth::distance_sq(a, b) > def->detonation_distance_sq)) burst = gone = true;
  }
  if (burst) air_burst(def, p);
  if (gone) p->state = 0;
}

namespace {
// rand / 32767 times a span (the craters' random parts).
float rand_span(float span) {
  float r = static_cast<float>(crt::msvc_rand());
  r = r * 3.05185094e-05f;
  return r * span;
}
bool craterable(Handle node) {
  const RunHooks& rh = run_hooks();
  return rh.node_flags && (rh.node_flags(node) & 0x10000);
}
}  // namespace

// Mech3.exe 1.2 @ 0x005825b0: a crater where a CRATER shot hit, on a node
// that takes them: points, radius and depth each the minimum plus a random
// share of the span. 1 when it was made.
int32_t crater(WeaponDef* def, HitRecord* rec, Projectile* p) {
  if (!craterable(rec->node)) return 0;
  const RunHooks& rh = run_hooks();
  CraterOrder c;
  if (rh.crater_defaults) rh.crater_defaults(&c);
  c.material = rec->material;
  c.at = rec->point;
  if (const int32_t span = def->crater_span.points) {
    float r = static_cast<float>(crt::msvc_rand());
    r = r * 3.05185094e-05f;
    c.points = crt::msvc_ftol(r * static_cast<float>(span)) + def->crater.points;
  }
  if (def->crater_span.radius > 0.0f) c.radius = rand_span(def->crater_span.radius) + def->crater.radius;
  if (def->crater_span.depth > 0.0f) c.depth = rand_span(def->crater_span.depth) + def->crater.depth;
  c.owner = p->owner;
  return rh.crater_make && rh.crater_make(&c) == 0 ? 1 : 0;
}

// Mech3.exe 1.2 @ 0x005824b0: a quicksand pit, likewise; only on the default
// soil.
void quicksand(WeaponDef* def, HitRecord* rec, int32_t soil, Projectile* p) {
  if (soil != 0 || !craterable(rec->node)) return;
  const RunHooks& rh = run_hooks();
  QuicksandOrder q;
  if (rh.quicksand_defaults) rh.quicksand_defaults(&q);
  q.at = rec->point;
  if (const int32_t span = def->crater_span.points) {
    float r = static_cast<float>(crt::msvc_rand());
    r = r * 3.05185094e-05f;
    q.points = crt::msvc_ftol(r * static_cast<float>(span)) + def->crater.points;
  }
  if (def->crater_span.radius > 0.0f) q.radius = rand_span(def->crater_span.radius) + def->crater.radius;
  if (def->crater_span.depth > 0.0f) q.depth = rand_span(def->crater_span.depth) + def->crater.depth;
  q.owner = p->owner;
  if (rh.quicksand_make) rh.quicksand_make(&q);
}

// The handler tables live at node +0xbc (zcls::Node::damage); a test's run
// hooks may stand in for the nodes.
namespace {
zcls::Node* zn(Handle n) { return static_cast<zcls::Node*>(n); }
uintptr_t handler_of(Handle n) {
  return run_hooks().handler ? run_hooks().handler(n) : reinterpret_cast<uintptr_t>(zn(n)->damage);
}
void put_handler(Handle n, uintptr_t h) {
  if (run_hooks().set_handler) run_hooks().set_handler(n, h);
  else zn(n)->damage = reinterpret_cast<void*>(h);
}
int32_t parent_count(Handle n) { return run_hooks().parent_count ? run_hooks().parent_count(n) : zn(n)->parent_count(); }
int32_t child_count(Handle n) { return run_hooks().child_count ? run_hooks().child_count(n) : zn(n)->child_count(); }
Handle parent_at(Handle n, int32_t i) { return run_hooks().parent ? run_hooks().parent(n, i) : zn(n)->parents[i]; }
Handle child_at(Handle n, int32_t i) { return run_hooks().child ? run_hooks().child(n, i) : zn(n)->children[i]; }
}  // namespace

NodeHandlers* handlers_of(Handle node) { return node ? reinterpret_cast<NodeHandlers*>(handler_of(node)) : nullptr; }

// Mech3.exe 1.2 @ 0x00583220: no parent (up to the first null one) has the
// same table: the node owns it.
bool handler_owned(Handle node, uintptr_t h) {
  const int32_t n = parent_count(node);
  int32_t i = 0;
  for (; i < n; ++i) {
    Handle p = parent_at(node, i);
    if (!p || handler_of(p) == h) break;
  }
  return i >= n;
}

Handle damage_object(Handle node, DamageFn* fn) {
  const auto* t = node ? reinterpret_cast<const NodeHandlers*>(handler_of(node)) : nullptr;
  if (fn) *fn = t ? reinterpret_cast<DamageFn>(t->slot[0].fn) : nullptr;
  return t ? t->slot[0].object : nullptr;
}

// Mech3.exe 1.2 @ 0x00583250
bool own_handler(Handle node) {
  if (!node) return false;
  const uintptr_t h = handler_of(node);
  return h && handler_owned(node, h);
}

// Mech3.exe 1.2 @ 0x00583270
void handler_set_slot(NodeHandlers* t, uintptr_t old, Handle object, Handle node, void (*fn)(), int32_t slot) {
  t->slot[slot] = {object, fn};
  handler_hand_down(node, t, old);
}

// Mech3.exe 1.2 @ 0x005832a0
void handler_hand_down(Handle node, NodeHandlers* t, uintptr_t old) {
  if (handler_of(node) != old) return;
  for (int32_t i = 0; i < child_count(node); ++i) handler_hand_down(child_at(node, i), t, old);
  put_handler(node, reinterpret_cast<uintptr_t>(t));
}

// Mech3.exe 1.2 @ 0x005832f0: a slot of the node's own table, made (or split
// from the parent's, the other slot left empty) as needed; 0 when the slot
// is taken. FIX (A): new for calloc.
int32_t set_handler(Handle object, Handle node, void (*fn)(), int32_t slot) {
  const uintptr_t h = handler_of(node);
  if (h == 0) {
    handler_set_slot(new NodeHandlers, 0, object, node, fn, slot);
    return 1;
  }
  if (handler_owned(node, h)) {
    NodeHandlers* t = reinterpret_cast<NodeHandlers*>(h);
    if (t->slot[slot].fn) return 0;
    t->slot[slot] = {object, fn};
    return 1;
  }
  handler_set_slot(new NodeHandlers, h, object, node, fn, slot);
  return 1;
}

// Mech3.exe 1.2 @ 0x005833b0
int32_t set_damage_handler(Handle object, Handle node, DamageFn fn, int32_t flag) {
  const int32_t r = set_handler(object, node, reinterpret_cast<void (*)()>(fn), 0);
  if (r && flag) {
    if (run_hooks().set_flag_4eb790) run_hooks().set_flag_4eb790(node, 1);
    else zcls::node_set_proximity(zn(node), 1);  // near misses count (0x40)
  }
  return r;
}

// Mech3.exe 1.2 @ 0x005833f0
int32_t set_touch_handler(Handle object, Handle node, TouchFn fn) {
  return set_handler(object, node, reinterpret_cast<void (*)()>(fn), 1);
}

// Mech3.exe 1.2 @ 0x00583400: the node's own table off its subtree and freed.
int32_t remove_handlers(Handle node) {
  if (!node) return 0;
  const uintptr_t h = handler_of(node);
  if (!h || !handler_owned(node, h)) return 0;
  handler_clear(node, h);
  delete reinterpret_cast<NodeHandlers*>(h);
  return 1;
}

// Mech3.exe 1.2 @ 0x00583440
void handler_clear(Handle node, uintptr_t h) {
  for (int32_t i = 0; i < child_count(node); ++i) handler_clear(child_at(node, i), h);
  if (handler_of(node) == h) put_handler(node, 0);
}

// Mech3.exe 1.2 @ 0x005836b0: something walks into a node: its touch handler
// (slot 1) with the damage; the shot line is set to from -> the contact.
float touch(const Vec3f3* from, HitRecord* hit, float damage) {
  const NodeHandlers* t = reinterpret_cast<const NodeHandlers*>(handler_of(hit->node));
  if (g_hit.record_shot == 1) {
    g_hit.shot_from = *from;
    g_hit.shot_to = hit->point;
  }
  return reinterpret_cast<TouchFn>(t->slot[1].fn)(t->slot[1].object, damage);
}

// Mech3.exe 1.2 @ 0x00583710
void note_kill(Handle killed, HitRecord* at) {
  if (at && at->node) g_hit.killed_at = at;
  g_hit.killed = killed;
}

// Mech3.exe 1.2 @ 0x00583480: the damage to what a record hit, through the
// node's handler (for a mech: Mech::TakeDamage), and the impact animation.
uint32_t apply_damage(WeaponDef* def, Handle owner, const Vec3f3* from, HitRecord* hit, float damage, Handle anim) {
  g_hit.killed = nullptr;
  g_hit.killed_at = nullptr;
  if (!(def->flags & kTargetHeat) && g_hh.surface_mark) g_hh.surface_mark(hit);
  const uintptr_t h = g_hh.node_handler ? g_hh.node_handler(hit->node) : 0;
  if (h == 0) return 0;
  uint32_t r = 0;
  if (h == 1) {
    // A projectile with FLYOUT_HEALTH: any hit wears it down.
    Projectile* p = g_hh.node_projectile ? g_hh.node_projectile(hit->node) : nullptr;
    float v = p->health - damage;
    if (!(v > 0.0f)) v = 0.0f;
    p->health = v;
  } else if (g_hh.handler_live && g_hh.handler_live(h)) {
    const int32_t soil = soil_of(def, hit->material);
    if (g_hit.record_shot == 1) {
      g_hit.shot_from = *from;
      g_hit.shot_to = hit->point;
    }
    g_hit.health_after = 1.0f;
    g_hit.attacker = owner;
    if (g_hit.pre_hit) g_hit.pre_hit(reinterpret_cast<Handle>(h), hit->node, damage, def);
    r = g_hh.call_handler ? g_hh.call_handler(h, def, hit, damage) : 0;
    if (g_hit.killed) {
      if (def->kill_animation) play_at(def->kill_animation, hit->point);
    } else if (anim) {
      play_at(anim, hit->point);
    } else if (def->damage_anim_count) {
      for (int32_t i = 0; i < def->damage_anim_count; ++i) {
        if (!(g_hit.health_after > def->damage_anims[i].health)) {
          play_at(def->damage_anims[i].anim, hit->point);
          break;
        }
      }
    } else if ((def->flags & kBeam) && g_hit.beam_ground_fx && def->impacts && def->impacts[soil].animation) {
      play_at(def->impacts[soil].animation, hit->point);
    }
  }
  if (g_hit.counted == owner) ++g_hit.hits;
  return r;
}

// Mech3.exe 1.2 @ 0x005826b0: one shot's impact: the weapon's callback, the
// damage, craters, the sound, animations and effect. The callback's bits skip
// the sound (1), craters and effect (2), the animation (4).
uint32_t impact(WeaponDef* def, HitRecord* rec, Projectile* p, Handle anim) {
  const int32_t soil = soil_of(def, rec->material);
  int32_t skip = 0;
  uint32_t flags = 0;
  if (def->callback) flags = def->callback(def, rec, p);
  const uint32_t r = apply_damage(def, p->owner, &p->position, rec, def->damage * p->damage_scale, anim);
  if ((def->flags & kCrater) && !(flags & 2)) {
    if (!(def->flags & kAnimationAlways)) skip = 1;
    skip &= g_hh.crater ? g_hh.crater(def, rec, soil, p) : crater(def, rec, p);
  } else if ((def->flags & kQuicksand) && !(flags & 2)) {
    if (HitRecord* k = g_hit.killed_at) {
      if (g_hh.quicksand) g_hh.quicksand(def, k, soil_of(def, k->material), p);
      else quicksand(def, k, soil_of(def, k->material), p);
    } else if (g_hh.quicksand) {
      g_hh.quicksand(def, rec, soil, p);
    } else {
      quicksand(def, rec, soil, p);
    }
  }
  // A kill makes its own crater where it fell.
  if (g_hit.killed && (def->flags & kCrater) && g_hit.killed_at && !(flags & 2)) {
    if (g_hh.crater) g_hh.crater(def, g_hit.killed_at, soil_of(def, g_hit.killed_at->material), p);
    else crater(def, g_hit.killed_at, p);
  }
  if ((def->flags & kQuicksand) && g_hit.killed_at && !(flags & 2)) {
    if (g_hh.quicksand) g_hh.quicksand(def, g_hit.killed_at, soil_of(def, g_hit.killed_at->material), p);
    else quicksand(def, g_hit.killed_at, soil_of(def, g_hit.killed_at->material), p);
    return r;
  }
  if (!(flags & 1)) {
    if (g_hh.sound) g_hh.sound(def, rec, soil, 1.0f);
    else sound(def, rec, soil, 1.0f);  // 0x00582f60 (zwep_shot.cpp)
  }
  if (!skip && def->impacts) {
    if (!(flags & 4) && r == 0) {
      if (Handle a = def->impacts[soil].animation) play_at(anim ? anim : a, rec->point);
    }
    if (Handle s = def->impacts[soil].surface_animation) {
      float n[3] = {rec->normal.x, rec->normal.y, rec->normal.z};
      zmod::normalize(n);
      rec->normal = {n[0], n[1], n[2]};
      float ang[3] = {0, 0, 0};
      if (g_hh.surface_angles) g_hh.surface_angles(&rec->normal, ang);
      const float a[9] = {rec->point.x, rec->point.y, rec->point.z, ang[0], ang[1], ang[2], 0, 0, 0};
      if (g_hh.effect) g_hh.effect(s, a);
    }
  }
  if (def->impacts) {
    if (const int32_t e = def->impacts[soil].effect; e && !(flags & 2) && g_hh.impact_effect)
      g_hh.impact_effect(e, &rec->point);
  }
  return r;
}

// Mech3.exe 1.2 @ 0x00582920: what lies within IMPACT_PROXIMITY (scaled with
// the shot's damage) of the shot. The shot's own node is kept out of the query.
int32_t splash_query(WeaponDef* def, Projectile* p, int32_t flag, HitList* out) {
  Handle node = p->node;
  bool toggled = false;
  if (node && g_hh.node_intersect && g_hh.node_intersect(node)) {
    toggled = true;
    if (g_hh.set_intersect) g_hh.set_intersect(node, 0);
  }
  int32_t n = g_hh.query ? g_hh.query(state().world, &p->position, def->impact_proximity * p->damage_scale, out) : 1;
  if (toggled && g_hh.set_intersect) g_hh.set_intersect(node, 1);
  // Only the shooter in range: no splash (both callers pass flag 1, so dead).
  if (flag == 0 && out->count == 1 && out->rec[0].node == p->owner) n = 1;
  return n == 0;
}

// Mech3.exe 1.2 @ 0x005829b0: splash damage, falling off with the square of
// the distance (MINE weapons at full damage), to all in range but `exclude`
// (the handler of what the shot hit). The original queues a hit instead
// when 31 are already queued and the weapon does not set fires; nothing
// ever queues, so the queue (0x009a0ef4) and its drain in the tick are dead.
int32_t splash(WeaponDef* def, Projectile* p, HitList* list, uintptr_t exclude) {
  int32_t any = 0;
  const int n = list->count;
  for (int i = 0; i < n; ++i) {
    HitRecord* rec = &list->rec[i];
    const uintptr_t h = g_hh.node_handler ? g_hh.node_handler(rec->node) : 0;
    if (h == exclude) continue;
    float dmg;
    if (def->flags & kMine) dmg = def->damage;
    else dmg = (1.0f - rec->distance_sq / def->impact_proximity_sq) * def->damage;
    dmg = p->damage_scale * dmg;
    apply_damage(def, p->owner, &p->position, rec, dmg, nullptr);
    any = 1;
  }
  return any;
}

// Mech3.exe 1.2 @ 0x00586350: a projectile hits. Inside RANGE_MINIMUM a shot
// with no second value is a dud; otherwise its damage (and splash) is scaled
// by it, LRMs (90 0.25) to a quarter, with the min-range animation.
void hit(WeaponDef* def, Projectile* p, HitRecord* rec) {
  if (g_hit.record_shot == 1) {
    g_hit.shot_from = p->position;
    g_hit.shot_to = rec->point;
  }
  const bool min_range = (def->flags & kRangeMinimum) != 0;
  if (min_range && def->range_min[1] == 0.0f && p->travelled < def->range_min[0]) {
    p->health = 0.0f;
    return;
  }
  Handle anim = nullptr;
  if (min_range && p->travelled < def->range_min[0]) {
    const int32_t soil = soil_of(def, rec->material);
    if (def->impacts && def->impacts[soil].animation_min_range) anim = def->impacts[soil].animation_min_range;
    else if (def->destroy_animation) anim = def->destroy_animation;
    p->damage_scale = def->range_min[1] * p->damage_scale;
  }
  p->state = 0;
  g_hit.record_shot = 0;
  p->position = rec->point;
  impact(def, rec, p, anim);
  if (def->impact_proximity > 0.0f) {
    HitList list;
    if (splash_query(def, p, 1, &list)) {
      const uintptr_t exclude = g_hh.node_handler ? g_hh.node_handler(rec->node) : 0;
      splash(def, p, &list, exclude);
    }
  }
  g_hit.record_shot = 1;
}

// Mech3.exe 1.2 @ 0x00582320: a shot bursts in the air (at range, or shot
// down): an impact on its own node at its position, then splash with no one
// spared. FIX (bug): the original's record has no surface normal (stack
// garbage) for the surface animation; here it is zero (docs/fixes.md).
void air_burst(WeaponDef* def, Projectile* p) {
  HitRecord rec;
  rec.point = p->position;
  rec.node = p->node;
  impact(def, &rec, p, nullptr);
  if (g_hit.splash_enabled && def->impact_proximity > 0.0f) {
    HitList list;
    if (splash_query(def, p, 1, &list)) splash(def, p, &list, 0);
  }
}

// Mech3.exe 1.2 @ 0x00581f60
void set_lock(int32_t* flag, LockRef* target) {
  g_fire.lock_flag = flag;
  g_fire.lock_target = target;
}

// Mech3.exe 1.2 @ 0x00581f70: which FIRE sound the next shot plays, -1 none.
bool set_fire_sound(WeaponDef* def, int32_t choice) {
  if (!def || choice < -1 || choice >= def->fire.sound_count || choice >= 4) return false;
  def->fire.sound_choice = choice;
  return true;
}

// Mech3.exe 1.2 @ 0x00581f90
bool set_fire_animation(WeaponDef* def, int32_t choice) {
  if (!def || choice >= 2) return false;
  def->fire.animation_choice = choice;
  return true;
}

// Mech3.exe 1.2 @ 0x00581fb0
void attach_fire(WeaponDef* def, Handle node) {
  def->fire.attach_node = node;
  def->fire.attach_offset = nullptr;
}

// Mech3.exe 1.2 @ 0x00581fd0
void attach_fire(WeaponDef* def, Handle node, const void* offset) {
  def->fire.attach_node = node;
  def->fire.attach_offset = offset;
}

namespace {
int32_t g_network = 0;  // [0x005f1e5c]
}  // namespace

// Mech3.exe 1.2 @ 0x00583740
void set_network(int32_t on) { g_network = on; }

// Mech3.exe 1.2 @ 0x00583750. [0x009a0ef4] and [0x009a1780], also cleared,
// have no reader the port has found.
int32_t startup() {
  HitState& h = hit_state();
  h.splash_enabled = 1, h.record_shot = 1;
  h.killed = nullptr, h.killed_at = nullptr, h.pre_hit = nullptr;
  h.hits = 0, h.counted = nullptr;
  h.tether_ceiling = 30.0f;  // 0x41f00000
  FireState& f = fire_state();
  f.beam_ground_fx = 1;
  f.lock_flag = nullptr, f.lock_target = nullptr;
  f.damage_scale = f.beam_range_scale = 1.0f;
  f.track_scale[0] = f.track_scale[1] = 1.0f;
  f.terrain_follow = 0;
  State& st = state();
  st.world = nullptr, st.tick = nullptr;
  st.defs.clear(), st.pool.clear();
  st.free_list = nullptr;
  st.max_crater_radius = 30.0f;
  run_state().pool_objects = 0;
  // With the network mode: 0x005a8a20(0x00583850, 1000, 0), the shots'
  // network messages (not ported).
  return 0;
}

// Mech3.exe 1.2 @ 0x00584fe0
void kill_every_shot() {
  for (WeaponDef& d : state().defs) kill_all(&d);
}

}  // namespace zwep
