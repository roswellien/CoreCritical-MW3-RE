#include "gamez/zweapon/zwep_init.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "crt/msvc_math.h"
#include "gamez/zmodel/gmod_matl.h"

#pragma STDC FP_CONTRACT OFF

namespace zwep {

namespace {

State g_state;
InitHooks g_hooks;

using zrdr::Node;


int32_t count(const Node* n) { return n->list[0].i; }
const Node& item(const Node* n, int i) { return n->list[i]; }

char* dup(const char* s) {  // MSVCRT _strdup: NULL in, NULL out
  if (!s) return nullptr;
  const size_t n = std::strlen(s) + 1;
  char* out = static_cast<char*>(std::malloc(n));
  std::memcpy(out, s, n);
  return out;
}

Handle sound(const char* name) { return g_hooks.sound ? g_hooks.sound(name) : nullptr; }
Handle anim(const char* name) { return g_hooks.anim ? g_hooks.anim(name) : nullptr; }
const char* message(const char* key) { return g_hooks.message ? g_hooks.message(key) : nullptr; }

void set_bit(uint32_t& flags, int bit, int32_t v) { flags = (flags & ~(1u << bit)) | (uint32_t(v & 1) << bit); }

// A boolean key into one flag bit, left alone when the key is missing.
void read_bit(Node* n, const char* key, uint32_t& flags, int bit) {
  int32_t v = 0;
  if (zrdr::get_bool_int(n, key, &v)) set_bit(flags, bit, v);
}

// CRATER or QUICKSAND: the defaults twice, then each value as (min max):
// the first block keeps the minimums, the second the spans.
void read_crater(WeaponDef& d, Node* n) {
  if (g_hooks.crater_defaults) {
    g_hooks.crater_defaults(&d.crater);
    g_hooks.crater_defaults(&d.crater_span);
  }
  int32_t points[2] = {d.crater.points, d.crater.points};
  float radius[2] = {d.crater.radius, d.crater.radius};
  float depth[2] = {d.crater.depth, d.crater.depth};
  zrdr::get_ivec2(n, "points", points);
  zrdr::get_vec2(n, "radius", radius);
  zrdr::get_vec2(n, "depth", depth);
  d.crater.radius = radius[0];
  d.crater.points = points[0];
  d.crater_span.radius = radius[1] - radius[0];
  d.crater.depth = depth[0];
  d.crater_span.points = points[1] - points[0];
  d.crater_span.depth = depth[1] - depth[0];
}

// IMPACT's HEAT, FREEZE and DESIGNATE: six values, all into the same slots.
void read_target_heat(WeaponDef& d, const Node* n) {
  for (int i = 0; i < 6; ++i) d.target_heat[i] = item(n, 1 + i).f;
}

}  // namespace

State& state() { return g_state; }
InitHooks& init_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x00581ed0. Defs not read yet have no key and are skipped.
WeaponDef* find_def(const char* name) {
  if (!name) return nullptr;
  for (WeaponDef& d : g_state.defs) {
    if (d.key && std::strcmp(name, d.key) == 0) return &d;
  }
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00584bb0
void read_effect(Node* parent, Effect* e, const char* key) {
  e->attach_node = nullptr;
  Node* b = zrdr::find(parent, key);
  if (!b) return;
  if (Node* n = zrdr::find(b, "EFFECT")) {
    if (count(n) > 1) e->effect = g_hooks.effect ? g_hooks.effect(item(n, 1).s) : 0;
  } else if ((n = zrdr::find(b, "MODEL")) && count(n) > 1) {
    e->model = g_hooks.find_node ? g_hooks.find_node(6, item(n, 1).s) : nullptr;
  }
  if (Node* n = zrdr::find(b, "ANIMATION_ATTACHED"); n && count(n) > 1) e->animation_attached = anim(item(n, 1).s);
  if (Node* n = zrdr::find(b, "ANIMATION_MIN_RANGE"); n && count(n) > 1) e->animation_min_range = anim(item(n, 1).s);
  if (Node* n = zrdr::find(b, "MODEL_ANIMATION"); n && count(n) > 1) e->model_animation = anim(item(n, 1).s);
  if (Node* n = zrdr::find(b, "SURFACE_ANIMATION"); n && count(n) > 1) e->surface_animation = anim(item(n, 1).s);
  if (Node* n = zrdr::find(b, "ANIMATION")) {
    if (count(n) > 1) e->animation = anim(item(n, 1).s);
    if (count(n) > 2) set_bit(e->flags, 1, item(n, 2).i);
  }
  if (Node* n = zrdr::find(b, "RANDOM_ROTATE")) set_bit(e->flags, 0, item(n, 1).i);
  // FIX (bug): each sound list has room for 4; the original writes on past
  // them into the next fields. No shipped weapon has more (docs/fixes.md).
  if (Node* n = zrdr::find(b, "SOUND")) {
    e->sound_count = std::clamp(count(n) - 1, 0, 4);
    for (int i = 1; i < count(n) && i <= 4; ++i) e->sounds[i - 1] = sound(item(n, i).s);
  }
  if (Node* n = zrdr::find(b, "BOUNCE_SOUND")) {
    e->bounce_sound_count = std::clamp(count(n) - 1, 0, 4);
    for (int i = 1; i < count(n) && i <= 4; ++i) e->bounce_sounds[i - 1] = sound(item(n, i).s);
  }
}

// Mech3.exe 1.2 @ 0x00583880
int init(Handle world, const char* path, int32_t arg, TypeCallback callback) {
  State& s = g_state;
  s.world = world;
  if (g_hooks.lights_init) g_hooks.lights_init();
  s.tree = zrdr::load(path);
  if (!s.tree) {
    if (g_hooks.error) g_hooks.error(0x200, 0xcd, "Failed to read %s");
    return -1;
  }
  Node* root = s.tree;
  Node* ver = zrdr::find(root, "VERSION");
  if (!ver) {
    if (g_hooks.error) g_hooks.error(0x400, 0xe2, "No ZWEP version found");
    return -1;
  }
  if (item(ver, 1).i != 2) {
    if (g_hooks.error) {
      char text[64];
      std::snprintf(text, sizeof text, "Incorrect ZWEP version (found %d, wanted %d)", item(ver, 1).i, 2);
      g_hooks.error(0x400, 0xda, text);
    }
    return -1;
  }
  if (Node* n = zrdr::find(root, "LOCK_ON_WARNING")) s.lock_on_warning = sound(item(n, 1).s);
  if (Node* n = zrdr::find(root, "NO_AMMO_WARNING")) s.no_ammo_warning = sound(item(n, 1).s);
  if (Node* n = zrdr::find(root, "TRIGGER_INACTIVE")) s.trigger_inactive = sound(item(n, 1).s);
  if (Node* n = zrdr::find(root, "WEAPON_INACTIVE")) s.weapon_inactive = sound(item(n, 1).s);
  zrdr::get_float(root, "MAX_CRATER_RADIUS", &s.max_crater_radius);

  int32_t pool_count = 0;
  const std::vector<std::string>& soils = zmod::soil_names();  // 0x005edbc0, count 0x005edbb8
  Node* bal = zrdr::find(root, "BALLISTICS");
  if (bal) {
    s.defs.assign(static_cast<size_t>((count(bal) - 1) / 2), WeaponDef{});
    // The pool size below reads RANGE's value from a local that only RANGE
    // sets, so a def without RANGE would use the one before it.
    // FIX (bug): it uses the def's own range (docs/fixes.md).
    int16_t index = 0;
    WeaponDef* d = s.defs.data();
    for (uint32_t k = 1; k < static_cast<uint32_t>(count(bal)); k += 2, ++d) {
      d->flags |= kDetonateOnWater;
      d->index = index++;
      d->impacts = new Effect[soils.size()]();  // one per soil name known now (zmod::soil_names)
      d->impact_count = static_cast<int32_t>(soils.size());
      Node* n = zrdr::find(bal, item(bal, k).s);
      if (n) {
        d->key = item(bal, k).s;
        Node* v;
        d->name = (v = zrdr::find(n, "NAME")) ? item(v, 1).s : d->key;
        d->desc = dup((v = zrdr::find(n, "DESC")) ? message(item(v, 1).s) : d->key);
        d->military_name = dup((v = zrdr::find(n, "MILITARY_NAME")) ? message(item(v, 1).s) : d->key);
        zrdr::get_float(n, "ACCELERATION", &d->acceleration);
        zrdr::get_float(n, "AMMO_LIMIT", &d->ammo_limit);
        if (Node* beam = zrdr::find(n, "BEAM")) {
          d->flags |= kBeam | kTimedDamage;
          d->velocity = 10000.0f;
          d->beam_energy = 1.0f;
          d->damage_at_max_range = 0.25f;
          d->pulse_rate = 1.0f;
          d->pulse[0] = 0.5f;
          d->pulse[1] = 0.75f;
          read_bit(beam, "BOUNCES", d->flags, 0);  // the file says BOUNCE: never read (docs/discoveries.md)
          read_bit(beam, "TIMED_DAMAGE", d->flags, 2);
          zrdr::get_float(beam, "ENERGY", &d->beam_energy);
          zrdr::get_float(beam, "TIME_TO_MAX_RANGE", &d->velocity);
          d->velocity = d->velocity > 0.0f ? 1.0f / d->velocity : 10000.0f;
          zrdr::get_float(beam, "DAMAGE_AT_MAX_RANGE", &d->damage_at_max_range);
          if (Node* p = zrdr::find(beam, "PULSE"); p && count(p) > 3) {
            d->pulse_rate = item(p, 1).f > 0.0f ? 1.0f / item(p, 1).f : 0.125f;
            d->pulse[0] = item(p, 2).f;
            d->pulse[1] = item(p, 3).f;
          }
        }
        read_bit(n, "CATCHES_FIRE", d->flags, 13);
        if (Node* c = zrdr::find(n, "CRATER")) {
          d->flags |= kCrater;
          read_crater(*d, c);
        } else {
          d->flags &= ~kCrater;
        }
        zrdr::get_float(n, "DAMAGE", &d->damage);
        if ((v = zrdr::find(n, "DETONATION_DISTANCE"))) d->detonation_distance_sq = item(v, 1).f * item(v, 1).f;
        read_bit(n, "DETONATE_AT_RANGE", d->flags, 25);
        read_bit(n, "DETONATE_ON_WATER", d->flags, 30);
        if ((v = zrdr::find(n, "DESTROY_ANIMATION"))) d->destroy_animation = anim(item(v, 1).s);
        read_bit(n, "RANDOM_DEVIATION", d->flags, 27);
        read_bit(n, "EXPIRES", d->flags, 7);
        if ((v = zrdr::find(n, "FIRE_RATE"))) d->period = 1.0f / item(v, 1).f;
        read_bit(n, "FIXED_ROTATE", d->flags, 8);
        // The LOCK_ON test is dead: LOCK_ON is read further down.
        if ((v = zrdr::find(n, "GRAVITY")) && !(d->flags & kLockOn)) d->gravity = item(v, 1).f;
        read_bit(n, "HIT_OWNER", d->flags, 26);
        if ((v = zrdr::find(n, "IMPACT_PROXIMITY"))) {
          d->impact_proximity = item(v, 1).f;
          d->impact_proximity_sq = item(v, 1).f * item(v, 1).f;
        }
        zrdr::get_int(n, "IMPACT_TYPE", &d->impact_type);
        read_bit(n, "INSTANT", d->flags, 11);
        if ((v = zrdr::find(n, "LOCK_ON"))) {
          d->lock_on = item(v, 1).f;
          d->flags |= kLockOn;
        }
        if ((v = zrdr::find(n, "LOCK_ON_LEAD"))) {
          d->flags |= kLockOnLead;
          if (count(v) > 2) d->lock_on_lead_min = item(v, 1).f;
          // FIX (bug): the original tests the first value against 3, not the
          // count, before reading the second (0x00583fb9; docs/fixes.md).
          if (count(v) > 3) d->lock_on_lead_max = item(v, 2).f;
          if (d->lock_on_lead_min > d->lock_on_lead_max) d->lock_on_lead_min = d->lock_on_lead_max;
          if (d->lock_on_lead_min != d->lock_on_lead_max)
            d->lock_on_lead_span = 1.0f / (d->lock_on_lead_max - d->lock_on_lead_min);
        }
        read_bit(n, "MINE", d->flags, 14);
        read_bit(n, "MULTI_TARGET", d->flags, 17);
        if (Node* q = zrdr::find(n, "QUICKSAND")) {
          d->flags |= kQuicksand;
          if (!(d->flags & kCrater)) {
            d->flags |= kCrater;
            read_crater(*d, q);
          }
        }
        if ((v = zrdr::find(n, "RANGE"))) {
          d->range = item(v, 1).f;
          d->range_sq = d->range * d->range;
        }
        if ((v = zrdr::find(n, "RANGE_MINIMUM"))) {
          d->flags |= kRangeMinimum;
          d->range_min[0] = item(v, 1).f;
          d->range_min[1] = item(v, 2).f;
        }
        read_bit(n, "RELATIVE_SPEED", d->flags, 24);
        read_bit(n, "REMOTE_DETONATE", d->flags, 20);
        read_bit(n, "TETHER_GUIDED", d->flags, 21);
        zrdr::get_float(n, "TURN_RATE", &d->turn_rate);
        zrdr::get_float(n, "TURN_SUSPEND_TIME", &d->turn_suspend_time);
        zrdr::get_float(n, "PITCH_RATE", &d->pitch_rate);
        if ((v = zrdr::find(n, "VELOCITY")) && !(d->flags & kBeam)) d->velocity = item(v, 1).f;
        read_bit(n, "RELOAD", d->flags, 19);
        d->type = nullptr;
        if (callback) callback(n, d);
        read_effect(n, &d->fire, "FIRE");
        read_effect(n, &d->flyout, "FLYOUT");
        if ((v = zrdr::find(n, "FLYOUT_HEALTH"))) {
          d->flags |= kFlyoutHealth;
          d->flyout_health = static_cast<float>(item(v, 1).i);
          if (d->flyout.model && g_hooks.set_intersect_surface) g_hooks.set_intersect_surface(d->flyout.model, 0);
        } else {
          d->flyout_health = -1.0f;
        }
        read_bit(n, "SHOW_ON_RADAR", d->flags, 28);
        if (Node* imp = zrdr::find(n, "IMPACT")) {
          read_effect(imp, &d->impacts[kSoilDefault], soils[kSoilDefault].c_str());
          for (size_t k2 = 1; k2 < soils.size(); ++k2) {
            if (zrdr::find(imp, soils[k2].c_str())) {
              read_effect(imp, &d->impacts[k2], soils[k2].c_str());
              if (!d->impacts[k2].animation_min_range)
                d->impacts[k2].animation_min_range = d->impacts[kSoilDefault].animation_min_range;
            } else {
              d->impacts[k2] = d->impacts[kSoilDefault];
            }
          }
          if (zrdr::find(imp, "ANIMATION_ALWAYS")) d->flags |= kAnimationAlways;
          if ((v = zrdr::find(imp, "FREEZE")) && count(v) > 6) {
            read_target_heat(*d, v);
            d->flags |= kTargetHeat | kFreeze;
          }
          if ((v = zrdr::find(imp, "HEAT")) && count(v) > 6) {
            read_target_heat(*d, v);
            d->flags |= kTargetHeat | kImpactHeat;
          }
          if ((v = zrdr::find(imp, "DESIGNATE")) && count(v) > 6) {
            d->target_heat[0] = item(v, 1).f;
            d->target_heat[1] = item(v, 2).f;
            d->target_heat[2] = 0.0f;
            d->target_heat[3] = item(v, 3).f;
            d->target_heat[4] = item(v, 4).f;
            d->target_heat[5] = item(v, 5).f;
            d->flags = (d->flags & ~(kTargetHeat | kImpactHeat)) | kDesignate;
            d->detonation_distance_sq = item(v, 6).f;
          }
          if ((v = zrdr::find(imp, "KILL_ANIMATION"))) d->kill_animation = anim(item(v, 1).s);
          if ((v = zrdr::find(imp, "DAMAGE_ANIMATION"))) {
            d->damage_anim_count = 1;
            d->damage_anims[0].health = 1.0f;
            d->damage_anims[0].anim = anim(item(v, 1).s);
          }
          if (g_hooks.hardware && (v = zrdr::find(imp, "DAMAGE_ANIM_ON_HEALTH"))) {
            d->damage_anim_count = count(v) - 1;
            // FIX (bug): room for 4; the original writes on into the next fields.
            for (int i = 0; i < d->damage_anim_count && i < 4; ++i) {
              const Node* pair = item(v, 1 + i).list;
              d->damage_anims[i].health = pair[1].f;
              d->damage_anims[i].anim = anim(pair[2].s);
            }
          }
        }
      }
      // Shells with gravity: the apex height of a straight-up shot, and a
      // muzzle speed for the range, sqrt(2 g range) by the bit trick.
      if (d->gravity != 0.0f) {
        d->apex = d->velocity * d->velocity / (d->gravity + d->gravity);
        const float x = d->gravity * d->range + d->gravity * d->range;
        int32_t bits;
        std::memcpy(&bits, &x, 4);
        bits = (bits >> 1) + 0x1fc00000;
        std::memcpy(&d->velocity, &bits, 4);
      }
      if (Handle m = d->flyout.model) {
        if (g_hooks.set_active) g_hooks.set_active(m, 1);
        const int32_t unlit = g_hooks.tree_unlit ? g_hooks.tree_unlit(m) : 0;
        set_bit(d->flags, 9, unlit == 0);
      }
      // One pool slot per shot that can be in the air at once.
      if (!(d->flags & kBeam)) pool_count += 1 + crt::msvc_ftol(d->range / d->velocity / d->period);
    }
  }
  Projectile blank;  // calloc'd in the original: every field 0
  blank.health = blank.damage_scale = 0;
  blank.track_scale[0] = blank.track_scale[1] = 0;
  s.pool.assign(static_cast<size_t>(pool_count), blank);
  s.free_list = nullptr;
  for (Projectile& p : s.pool) {
    p.next = s.free_list;
    s.free_list = &p;
  }
  if (g_hooks.pool_objects) g_hooks.pool_objects(1);
  s.tick = g_hooks.object3d_create ? g_hooks.object3d_create() : nullptr;
  if (s.tick) {
    if (g_hooks.set_action_priority) g_hooks.set_action_priority(s.tick, 3);
    if (g_hooks.set_tick_action) g_hooks.set_tick_action(s.tick);
  } else if (g_hooks.error) {
    g_hooks.error(0x400, 0x328, "Error allocating weapon_tick callback");
  }
  s.init_arg = arg;
  return 0;
}

// Mech3.exe 1.2 @ 0x00584930
void shutdown(void (*per_def)(WeaponDef* def)) {
  State& s = g_state;
  if (g_hooks.pool_objects) g_hooks.pool_objects(0);
  for (WeaponDef& d : s.defs) {
    if (per_def) per_def(&d);
    delete[] d.impacts;
    d.impacts = nullptr;
    d.impact_count = 0;
    std::free(const_cast<char*>(d.desc));
    d.desc = nullptr;
    std::free(const_cast<char*>(d.military_name));
    d.military_name = nullptr;
  }
  s.defs.clear();
  s.pool.clear();
  s.free_list = nullptr;
  s.world = nullptr;
  if (s.tree) zrdr::free_node(s.tree);
  s.tree = nullptr;
  if (s.tick && g_hooks.node_destroy) g_hooks.node_destroy(s.tick);
  s.tick = nullptr;
}

}  // namespace zwep
