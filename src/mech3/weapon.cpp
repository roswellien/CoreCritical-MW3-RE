#include "mech3/weapon.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <list>

#include "crt/msvc_math.h"
#include "crt/msvc_rand.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zweapon/zweapon.h"
#include "mech3/mech_names.h"
#include "mech3/content_registry.h"
#include "mech3/cheats.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

using zrdr::Node;

// Mech3.exe 1.2 @ 0x004af5a0
WeaponType::WeaponType(Node* node, WeaponDef* d)
    : def(d), x80(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(node))) {
  id = weapon_index(def->name);
  range = def->range;
  half_range = range * 0.5f;
  range_sq = range * range;
  half_range_sq = half_range * half_range;
  if (Node* n = zrdr::find(node, "HEAT")) {
    heat = n->list[1].f;
    heat_2 = n->list[2].f;
  }
  if (Node* n = zrdr::find(node, "SHOW_ON_RADAR"); n && n->type == zrdr::List) {
    if (zrdr::list_count(n) > 1 && n->list[1].type == zrdr::Int) show_on_radar = n->list[1].i != 0;
  }
  // ZAPS_HUD: the HUD static a hit adds, then how long it holds before it
  // fades (0x0043be70 keeps the longest hold; 0x0043d07a counts it down).
  // FIX (bug): the exe reads the first value for both (0x004af71b), so the
  // ERPPC's (0.5 2.0) held its static 0.5 s, not 2 s (docs/fixes.md).
  if (Node* n = zrdr::find(node, "ZAPS_HUD"); n && n->type == zrdr::List && zrdr::list_count(n) > 1) {
    float* out[2] = {&zaps_hud, &zaps_hud_2};
    for (int i = 1; i <= 2 && i < zrdr::list_count(n); ++i) {
      const zrdr::Node& v = n->list[zfix::options().bugs ? i : 1];
      if (v.type == zrdr::Int) *out[i - 1] = static_cast<float>(v.i);
      else if (v.type == zrdr::Float) *out[i - 1] = v.f;
    }
  }
  if (Node* n = zrdr::find(node, "SALVO")) {
    salvo_count = 1;
    salvo_delta = 0.1f;
    salvo_truecount = 1;
    salvo_radius_sq = 0;
    salvo_delay = 0;
    zrdr::get_int(n, "count", &salvo_count);
    zrdr::get_float(n, "delta", &salvo_delta);
    zrdr::get_float(n, "radius", &salvo_radius_sq);
    zrdr::get_float(n, "delay", &salvo_delay);
    if (!zrdr::get_int(n, "truecount", &salvo_truecount)) salvo_truecount = salvo_count;
    salvo_radius_sq = salvo_radius_sq * salvo_radius_sq;
    if (salvo_radius_sq > 0.0f && salvo_delay > 0.0f) zwep::set_callback(def, salvo_spread);
  }
  if (Node* mass = zrdr::find(node, "MASS")) {
    const char* keys[2] = {"clan", "sphere"};
    for (int t = 0; t < 2; ++t) {
      float v[2] = {0, 0};
      if (zrdr::get_vec2(mass, keys[t], v)) {
        lab[t].tons = v[0];
        lab[t].slots = crt::msvc_ftol(v[1]);
        lab[t].present = true;
      }
    }
    zrdr::get_int(mass, "ammo", &rounds_per_ton);
    if (salvo_count != 0) rounds_per_ton *= salvo_truecount;
    tons_per_round = rounds_per_ton == 0 ? 0.0f : 1.0f / static_cast<float>(rounds_per_ton);
  }
  if (Node* n = zrdr::find(node, "BURST")) {
    const char* wep = zrdr::get_string(n, "wep");
    zrdr::get_int(n, "count", &burst_count);
    zrdr::get_float(n, "radius", &burst_radius);
    burst = zwep::find_def(wep);
    if (burst) {
      // The pellets fly on from where the shell bursts: the reach is both ranges.
      range = burst->range + range;
      range_sq = range * range;
      def->flags |= zwep::kDetonateAtRange;
      zwep::set_callback(def, mech3::burst);
    }
  }
  zrdr::get_bool(node, "ISNARC", &is_narc);
  zrdr::get_float(node, "IMPACT_FORCE", &impact_force);
  zrdr::get_float(node, "RECOIL", &recoil);
  zrdr::get_float(node, "U_FIRE_TIME", &fire_time);
  if (zrdr::get_float(node, "U_RECHARGE_TIME", &recharge_rate))
    recharge_rate = def->period * fire_time / recharge_rate;
  if (const char* name = zrdr::get_string(node, "U_DESIGNATE_AIRSTRIKE")) {
    strike_weapon = zwep::find_def(name);
    zwep::set_callback(def, mech3::airstrike);
  }
  zwep::InitHooks& h = zwep::init_hooks();
  auto sound = [&h](const char* s) { return h.sound ? h.sound(s) : nullptr; };
  if (const char* s = zrdr::get_string(node, "U_LOOPED")) looped_sound = sound(s);
  if (const char* s = zrdr::get_string(node, "U_RECHARGE_SND")) recharge_sound = sound(s);
  if (Node* n = zrdr::find(node, "NAMESOUND")) {
    name_sound = sound(n->list[1].s);
    part_damage_sound = sound("mech_part_damage");
    part_destroy_sound = sound("mech_part_destroy");
  }
  float v = def->velocity;
  if (v < 100.0f) v = 100.0f;
  else if (v > 5000.0f) v = 5000.0f;
  if (weapon_is_lrm(id)) lrm_flight = crt::x87_mul_f24(range, 0.7) / v;  // LRM5..LRM20
  strike_lock.at = &strike_point;
}

// Mech3.exe 1.2 @ 0x004afb10
WeaponType::~WeaponType() {
  for (SalvoMark* m : salvo_marks) delete m;
  salvo_marks.clear();
}

// Mech3.exe 1.2 @ 0x004b0020
void make_weapon_type(Node* node, WeaponDef* def) { def->type = new WeaponType(node, def); }

// Mech3.exe 1.2 @ 0x004b0090 (with the destructor 0x004afb10)
void free_weapon_type(WeaponDef* def) {
  delete def->type;
  def->type = nullptr;
}

const WeaponType* weapon_type(const char* name) {
  const WeaponDef* d = zwep::find_def(name);
  return d ? d->type : nullptr;
}


// ---------------------------------------------------------------------------
// Run time.

namespace {

WeaponWorld g_world;
WeaponCalls g_calls;

WeaponType* type_of(const WeaponDef* d) { return d ? d->type : nullptr; }
bool is_player(Handle owner) { return owner && owner == g_world.player; }

// rand() - 16383.5 scaled to about -1..1 (0x005e4008, 0x005e4004).
float rand_unit() {
  return (static_cast<float>(crt::msvc_rand()) - 16383.5f) * 0x1.0002p-14f;  // 0x38800100, 1 / 16383.5
}

Handle part_of(Handle owner, const Firepoint* fp) {
  return g_calls.part_by_id ? g_calls.part_by_id(owner, fp->model_part) : nullptr;
}
Handle node_of(Handle part) { return part && g_calls.part_node ? g_calls.part_node(part) : nullptr; }
bool in_water(Handle part) { return part && g_calls.part_in_water && g_calls.part_in_water(part); }
void halt(Handle& s) {
  if (s && g_calls.stop_sound) g_calls.stop_sound(s);
}
void net_event(Handle owner, Weapon* w) {
  if (g_calls.net_event) g_calls.net_event(owner, 0x16, w);
}
void set_lock_globals(int32_t* flag, zwep::LockRef* target) { zwep::set_lock(flag, target); }
void mech_sound(int32_t i) {
  if (g_calls.mech_sound) g_calls.mech_sound(i);
}

// Damage a second as the AI and the crits rate it: shots a second x damage,
// a burst counted as its pellets, a salvo's rounds only when asked.
float shot_rating(const WeaponDef* d, bool salvo) {
  float rate = d->period;
  if (rate != 0.0f) rate = 1.0f / rate;
  const WeaponType* t = d->type;
  float dmg;
  if (t->burst) dmg = t->burst->damage * static_cast<float>(t->burst_count);
  else dmg = d->damage;
  if (salvo) dmg = dmg * static_cast<float>(t->salvo_truecount);
  return rate * dmg;
}

// A beam: damage over one fire-and-recharge cycle.
float beam_cycle_rating(const WeaponDef* d) {
  const WeaponType* t = d->type;
  const float fire = t->fire_time;
  float cycle = fire + d->period * fire / t->recharge_rate;
  if (cycle != 0.0f) cycle = 1.0f / cycle;
  float dmg = d->damage;
  if (d->flags & zwep::kTimedDamage) dmg = dmg * fire * d->damage;
  return cycle * dmg;
}

// 0x00461670, MechPart::WeaponFired: the firing heat, and a projectile's recoil.
void weapon_fired(Handle part, Weapon* w) {
  const WeaponType* t = type_of(w->def);
  Handle mech = g_calls.part_mech ? g_calls.part_mech(part) : nullptr;
  if (w->def->flags & zwep::kBeam) {
    const float h = (w->def->flags & zwep::kTimedDamage) ? t->heat * g_world.dt : t->heat;
    if (g_calls.add_heat) g_calls.add_heat(mech, h);
  } else {
    if (g_calls.add_heat) g_calls.add_heat(mech, t->heat);
    if (g_calls.recoil) g_calls.recoil(part, t->recoil);
  }
}

float dot(const Vec3f& a, const Vec3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// 0x0051e3d0: out = b - a, normalised; returns the length.
float direction_to(const Vec3f& a, const Vec3f& b, Vec3f* out) {
  *out = {b.x - a.x, b.y - a.y, b.z - a.z};
  float v[3] = {out->x, out->y, out->z};
  const float len = zmod::normalize(v);
  *out = {v[0], v[1], v[2]};
  return len;
}

}  // namespace

WeaponWorld& weapon_world() { return g_world; }
WeaponCalls& weapon_calls() { return g_calls; }

namespace {
zmth::Vec3 v3(const zwep::Vec3f3& v) { return {v.x, v.y, v.z}; }
const Vec3f* vf(const zmth::Vec3* v) { return reinterpret_cast<const Vec3f*>(v); }
}  // namespace

// Mech3.exe 1.2 @ 0x004afc00: a salvo's later shots that land within its
// radius of an earlier one, before its delay is up, skip the sound, craters
// and animation (7). Expired marks go first. Damage is not affected.
uint32_t WeaponType::salvo_mark(const zwep::Vec3f3& at) {
  const float now = g_world.clock;
  const zmth::Vec3 p = v3(at);
  SalvoMark* near = nullptr;
  for (auto it = salvo_marks.begin(); it != salvo_marks.end();) {
    SalvoMark* m = *it;
    if (m->until < now) {
      delete m;
      it = salvo_marks.erase(it);
      continue;
    }
    if (!near && zmth::distance_sq(v3(m->at), p) < salvo_radius_sq) near = m;
    ++it;
  }
  if (near) return 7;
  salvo_marks.push_back(new SalvoMark{at, salvo_delay + g_world.clock});
  return 0;
}

// Mech3.exe 1.2 @ 0x004afe70
uint32_t salvo_spread(WeaponDef* def, const zwep::HitRecord*, zwep::Projectile* p) {
  return def->type->salvo_mark(p->position);
}

// Mech3.exe 1.2 @ 0x004afed0: TAG's strike, four missiles from 1500 back along
// the beam and 400 up, 50 apart, homing on the point. FIX (bug): on the unit
// the beam marked, so a moving one is hit too (Owen 2026-10-20; zfix bugs).
void WeaponType::strike(zwep::Handle owner_node, const zwep::Vec3f3& dir, const zwep::Vec3f3& at,
                        zwep::Handle hit_node) {
  strike_point = at;
  strike_lock = zwep::LockRef{};
  strike_lock.at = &strike_point;
  if (zfix::options().bugs && g_calls.strike_target) {
    zwep::LockRef unit;
    if (g_calls.strike_target(hit_node, &unit) && unit.at) strike_lock = unit;
  }
  zmth::Vec3 start{dir.x * -1500.0f, dir.y * -1500.0f, dir.z * -1500.0f};
  start = {at.x + start.x, at.y + start.y, at.z + start.z};
  start.y = start.y - -400.0f;
  zmth::Vec3 d;
  zmth::direction_to(start, v3(at), &d);
  const zmth::Vec3 still{0, 0, 0};
  for (int i = 0; i < 4; ++i) {
    zmth::Vec3 from;
    from.x = rand_unit() * 50.0f + start.x;
    from.y = rand_unit() * 50.0f + start.y;
    from.z = start.z;
    zwep::set_lock(&strike_flag, &strike_lock);
    spawn_projectile(strike_weapon, owner_node, nullptr, vf(&from), vf(&d), vf(&still), 0, 0, false);
  }
  zwep::set_lock(nullptr, nullptr);
}

// Mech3.exe 1.2 @ 0x004afea0
uint32_t airstrike(WeaponDef* def, const zwep::HitRecord* hit, zwep::Projectile* p) {
  def->type->strike(p->owner, p->direction, hit->point, hit->node);
  return 0;
}

// Mech3.exe 1.2 @ 0x004b44d0: an LB-X shell bursts into BURST count pellets
// in a cone: each gets the shell's velocity plus up to `radius` at a random
// angle around its path.
uint32_t burst(WeaponDef* def, const zwep::HitRecord*, zwep::Projectile* p) {
  WeaponType* t = def ? def->type : nullptr;
  if (!t || !p || !t->burst) return 0;
  const zmth::Vec3 pos = v3(p->position);
  const float s = p->speed;
  const zmth::Vec3 dir = v3(p->direction);
  const zmth::Vec3 vel{s * dir.x, s * dir.y, s * dir.z};
  zmth::Vec3 side;
  zmth::perpendicular(dir, &side);
  for (int32_t n = t->burst_count; n != 0; --n) {
    const float angle = static_cast<float>(crt::msvc_rand()) * 0x1.0002p-15f * 0x1.921fb6p+2f;  // 1 / 32767.5, 2 pi
    zmth::Quat q;
    zmth::quat_axis_angle(dir, angle, &q);
    zmth::Vec3 off;
    zmth::quat_rotate(q, side, &off);
    const float k = static_cast<float>(crt::msvc_rand()) * 0x1.0002p-15f * t->burst_radius;
    off = {k * off.x, k * off.y, k * off.z};
    const zmth::Vec3 v{vel.x + off.x, vel.y + off.y, vel.z + off.z};
    float d[3] = {v.x, v.y, v.z};
    zmod::normalize(d);
    const zmth::Vec3 dn{d[0], d[1], d[2]};
    spawn_shot(t->burst, p->owner, nullptr, vf(&pos), vf(&dn), vf(&v), 0, 0, false);
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004b4c70
bool WeaponType::ready() const { return !(g_world.time < ready_at); }

// Mech3.exe 1.2 @ 0x004b4c90
void WeaponType::hold_for_flight() { ready_at = lrm_flight + g_world.time; }

// Mech3.exe 1.2 @ 0x004b4bd0
void reset_ready_times() {
  for (int32_t i = -1; i < weapon_count(); ++i)
    if (WeaponType* t = const_cast<WeaponType*>(weapon_type(weapon_name(i)))) t->set_ready_at(0.0f);
}

// Mech3.exe 1.2 @ 0x004b4900. Without the weapon's definition: 100 over 150,
// 20 over 20, else 1.
int32_t lab_rounds(int32_t weapon, int32_t n) {
  if (const WeaponDef* d = zwep::find_def(weapon_name(weapon))) {
    const WeaponType* t = d->type;
    if (!t) return 1;
    return t->rounds_per_ton < n ? t->rounds_per_ton : n;
  }
  if (n > 150) return 100;
  if (n > 20) return 20;
  return 1;
}

// Mech3.exe 1.2 @ 0x004b4ba0
bool Weapon::type_ready() const { return def && def->type && def->type->ready(); }

// Mech3.exe 1.2 @ 0x004b4cc0. The first weapon on a bin gives it its manager
// and damage.
bool AmmoBin::attach(Weapon* w) {
  if (manager) return false;
  manager = w->manager;
  damage = w->def->damage;
  return true;
}

// Mech3.exe 1.2 @ 0x004b4d10
void AmmoBin::set_amount(int32_t n) {
  if (amount != n) {
    if (max < n) {
      if (WeaponManager* m = manager) {
        float most = m->ammo_value_inv;
        if (most != 0.0f) most = 1.0f / most;
        most = most + static_cast<float>(n - max) * damage;
        m->ammo_value_inv = most <= 0.0f ? 0.0f : 1.0f / most;
      }
      max = n;
    }
    if (manager) manager->ammo_value = static_cast<float>(n - amount) * damage + manager->ammo_value;
    amount = n;
  }
  if (amount != 0) warned = false;
}

// Mech3.exe 1.2 @ 0x004b1db0
int32_t WeaponGroup::find(Weapon* w) const {
  for (uint32_t i = 0; i < count; ++i)
    if (weapons[i] == w) return static_cast<int32_t>(i);
  return -1;
}

// Mech3.exe 1.2 @ 0x004b1de0
bool WeaponGroup::select(Weapon* w) {
  const int32_t i = find(w);
  if (i == -1) return false;
  current = i;
  current_weapon = weapons[i];
  return true;
}

// Mech3.exe 1.2 @ 0x004b1eb0
bool WeaponGroup::add(Weapon* w) {
  if (find(w) == -1 && count < 16) {
    weapons[count++] = w;
    select(weapons[0]);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x004b1ef0. The last weapon out leaves the group's current
// pointing at it: weapons[0] is no longer found.
bool WeaponGroup::remove(Weapon* w) {
  const int32_t i = find(w);
  if (i == -1) return false;
  for (uint32_t j = static_cast<uint32_t>(i); j < count - 1; ++j) weapons[j] = weapons[j + 1];
  --count;
  select(weapons[0]);
  return true;
}

// Mech3.exe 1.2 @ 0x004b1f40: the next weapon with ammo that is not destroyed.
void WeaponGroup::next() {
  if (count <= 1) return;
  uint32_t i = static_cast<uint32_t>(current) <= count ? static_cast<uint32_t>(current) : count;
  current_weapon = nullptr;
  Weapon* w;
  do {
    if (++i == count) i = 0;
    w = at(i);
    if (w && w->ammo() != 0 && w->damage != 1.0f) current_weapon = w;
  } while (!current_weapon && i != static_cast<uint32_t>(current));
  if (w) current = static_cast<int32_t>(i);
  current_weapon = at(static_cast<uint32_t>(current));
}

// Mech3.exe 1.2 @ 0x004b2000
void WeaponGroup::prev() {
  if (count <= 1) return;
  int32_t i = static_cast<uint32_t>(current) <= count ? current : static_cast<int32_t>(count);
  current_weapon = nullptr;
  Weapon* w;
  do {
    if (--i < 0) i = static_cast<int32_t>(count) - 1;
    w = at(static_cast<uint32_t>(i));
    if (w && w->ammo() != 0 && w->damage != 1.0f) current_weapon = w;
  } while (!current_weapon && i != current);
  if (w) current = i;
  current_weapon = at(static_cast<uint32_t>(current));
}

// Mech3.exe 1.2 @ 0x004b20b0
void WeaponGroup::release() {
  if (!current_weapon) return;
  if (manager->mode == 2) {
    for (uint32_t i = 0; i < count; ++i) weapons[i]->release();
  } else {
    current_weapon->release();
  }
}

// Mech3.exe 1.2 @ 0x004b2100. In group fire every beam but the best rated one
// is told not to play its ground effect.
bool WeaponGroup::trigger() {
  bool fired = false;
  if (manager->mode == 2) {
    int32_t best = -1;
    float best_rating = 0;
    uint32_t beams = 0;
    for (uint32_t i = 0; i < count; ++i) {
      Weapon* w = weapons[i];
      if (w->is_beam()) {
        ++beams;
        if (w->rating > best_rating) {
          best = static_cast<int32_t>(i);
          best_rating = w->rating;
        }
      }
    }
    for (uint32_t i = 0; i < count; ++i) {
      if (beams > 1 && weapons[i]->is_beam() && best != static_cast<int32_t>(i)) zwep::fire_state().beam_ground_fx = 0;
      if (weapons[i]->trigger()) fired = true;
    }
  } else if (current_weapon) {
    fired = current_weapon->trigger();
  }
  return fired;
}

// Mech3.exe 1.2 @ 0x004b21f0
void WeaponGroup::held() {
  if (manager->mode == 2) {
    for (uint32_t i = 0; i < count; ++i) weapons[i]->held();
  } else if (current_weapon) {
    current_weapon->held();
  }
}

// Mech3.exe 1.2 @ 0x004b00d0
WeaponManager::WeaponManager(Handle v) : vehicle(v) {
  owner = g_calls.vehicle_mech ? g_calls.vehicle_mech(v) : nullptr;
  make_groups();
}

void WeaponManager::make_groups() {
  for (int32_t i = 0; i < 5; ++i) {
    groups[i] = new WeaponGroup;
    groups[i]->manager = this;
    groups[i]->index = i;
  }
}

// Mech3.exe 1.2 @ 0x004b01d0
WeaponManager::~WeaponManager() {
  for (uint32_t i = 0; i < count; ++i) delete weapons[i];
  for (WeaponGroup* g : groups) delete g;
}

void WeaponManager::refresh_hud() const {
  if (is_player(owner) && g_calls.hud_groups) g_calls.hud_groups(owner);
}

// Mech3.exe 1.2 @ 0x004b0220
void WeaponManager::set_unlimited(bool on) {
  for (uint32_t i = 0; i < count; ++i) {
    Weapon* w = weapons[i];
    if (w->bin && weapon_uses_ammo(w->index)) w->bin->infinite = on;
  }
  refresh_hud();
}

// Mech3.exe 1.2 @ 0x004b0290. The selected group and the destroyed count stay.
void WeaponManager::reset() {
  for (uint32_t i = 0; i < count; ++i) {
    delete weapons[i];
    weapons[i] = nullptr;
  }
  count = 0;
  for (WeaponGroup*& g : groups) {
    delete g;
    g = nullptr;
  }
  make_groups();
}

// Mech3.exe 1.2 @ 0x004b0370
void WeaponManager::forget_target(Handle target) {
  for (uint32_t i = 0; i < count; ++i) {
    Weapon* w = weapons[i];
    if (w->lock_flag && w->lock.entity == (g_calls.target_key ? g_calls.target_key(target) : nullptr)) {
      w->lock_flag = 0;
      w->lock.velocity = nullptr;
      w->lock.at = nullptr;
    }
  }
}

// Mech3.exe 1.2 @ 0x004b03c0
Weapon* WeaponManager::find(int32_t index, uint32_t from) const {
  for (uint32_t i = from; i < count; ++i)
    if (weapons[i]->index == index) return weapons[i];
  return nullptr;
}

// Mech3.exe 1.2 @ 0x004b0400. Nothing here stops a 17th weapon
// (docs/weapons.md, "Latent overflow").
Weapon* WeaponManager::add(WeaponItem* item) {
  if (!item) return nullptr;
  const int32_t g = item->group > 4 ? 4 : (item->group < 0 ? 0 : item->group);
  Weapon* w = make_weapon(item, this);
  if (w) {
    weapons[count++] = w;
    w->set_group(g);
  }
  return w;
}

// Mech3.exe 1.2 @ 0x004b0460, given the node (the vehicle-and-name form looks
// it up first, 0x004abb60).
Weapon* WeaponManager::add(int32_t index, Handle node, int32_t g) {
  Weapon* w = make_vehicle_weapon(index, node, this);
  if (w) {
    weapons[count++] = w;
    w->set_group(g);
  }
  return w;
}

// Mech3.exe 1.2 @ 0x004b0460, the named form: the node is looked up by name
// under `node` first (0x004abb60); none found, no weapon.
Weapon* WeaponManager::add(int32_t index, Handle node, const char* name, int32_t g) {
  if (name) {
    node = g_calls.node_by_name ? g_calls.node_by_name(node, name) : nullptr;
    if (!node) return nullptr;
  }
  return add(index, node, g);
}

// Mech3.exe 1.2 @ 0x004b04c0
Weapon* WeaponManager::current() const {
  WeaponGroup* g = group_ptr();
  return g ? g->current_weapon : nullptr;
}

// Mech3.exe 1.2 @ 0x004b04f0: the next group with a beam or a weapon with ammo.
bool WeaponManager::next_group() {
  bool found = false;
  int32_t g = group;
  for (;;) {
    g = (g + 1) % 5;
    if (g == group) return false;
    WeaponGroup* grp = groups[g];
    const uint32_t n = grp->count;
    for (uint32_t i = 0; i < n && !found; ++i) {
      Weapon* w = grp->at(i);
      if (w && w->damage != 1.0f && (w->is_beam() || w->ammo() != 0)) found = true;
    }
    if (found) break;
  }
  release();
  group = g;
  refresh_hud();
  return true;
}

// Mech3.exe 1.2 @ 0x004b0610. Unlike next_group it wants ammo > 0, so a group
// with only an unlimited-ammo weapon that is not a beam (the Flamer) is skipped.
bool WeaponManager::prev_group() {
  bool found = false;
  int32_t g = group;
  for (;;) {
    g = (g + 4) % 5;
    if (g == group) return false;
    WeaponGroup* grp = groups[g];
    const uint32_t n = grp->count;
    for (uint32_t i = 0; i < n && !found; ++i) {
      Weapon* w = grp->at(i);
      if (w->damage != 1.0f && (w->is_beam() || w->ammo() > 0)) found = true;
    }
    if (found) break;
  }
  release();
  group = g;
  refresh_hud();
  return true;
}

// Mech3.exe 1.2 @ 0x004b0730
bool WeaponManager::select_group(int32_t g) {
  if (groups[g]->count < 1) return false;
  release();
  group = g;
  refresh_hud();
  return true;
}

// Mech3.exe 1.2 @ 0x004b0790
void WeaponManager::next_weapon() {
  release();
  if (WeaponGroup* g = group_ptr()) g->next();  // FIX (bug) as in update
  refresh_hud();
}

// Mech3.exe 1.2 @ 0x004b07e0
void WeaponManager::prev_weapon() {
  release();
  if (WeaponGroup* g = group_ptr()) g->prev();  // FIX (bug) as in update
  refresh_hud();
}

// Mech3.exe 1.2 @ 0x004b0830
void WeaponManager::mode_sound() const {
  if (mode == 0) mech_sound(0x16);
  else if (mode == 1) mech_sound(0x14);
  else if (mode == 2) mech_sound(0x15);
}

// Mech3.exe 1.2 @ 0x004b0880
void WeaponManager::release() {
  if (trigger_down || trigger_was) {
    if (WeaponGroup* g = group_ptr()) g->release();
  }
  trigger_was = false;
  trigger_down = false;
}

// Mech3.exe 1.2 @ 0x004b08c0: a weapon fired over the network (action 0
// release, 1 trigger, 2 held, 3 stop), with the shooter's target.
bool WeaponManager::net_fire(uint32_t i, Handle target, int32_t action) {
  if (i >= count) return false;
  Weapon* w = weapons[i];
  if (w->is_beam()) {
    switch (action) {
      case 0:
        w->release();
        if (mode == 0 && group_ptr()) group_ptr()->next();  // FIX (bug) as in update
        break;
      case 1: w->trigger(); break;
      case 2: w->held(); break;
      case 3: w->release(); break;
      default: break;
    }
    return w->is_firing();
  }
  if (target) {
    if (Handle t = g_calls.net_target ? g_calls.net_target(target) : nullptr) {
      w->lock_flag = 1;
      w->lock.entity = g_calls.target_key ? g_calls.target_key(t) : nullptr;
      w->lock.at = g_calls.target_position ? g_calls.target_position(t) : nullptr;
      w->lock.velocity = g_calls.target_velocity ? g_calls.target_velocity(t) : nullptr;
      set_lock_globals(&w->lock_flag, &w->lock);
    }
  }
  return w->aim_and_fire(&aim, true);
}

// Mech3.exe 1.2 @ 0x004b0a70
void WeaponManager::alpha_strike(float seconds) {
  state = 1;
  for (uint32_t i = 0; i < count; ++i) weapons[i]->trigger();
  alpha_end = g_world.clock + seconds;
}

// Mech3.exe 1.2 @ 0x004b0ab0
void WeaponManager::alpha_release() {
  for (uint32_t i = 0; i < count; ++i) weapons[i]->release();
  trigger_was = false;
  trigger_down = false;
}

// Mech3.exe 1.2 @ 0x004b0ae0. Unlimited-ammo bins are emptied too.
bool WeaponManager::jettison() {
  WeaponGroup* g = group_ptr();
  if (!g) return false;
  bool done = false;
  if (mode == 2) {
    for (uint32_t i = 0; i < g->count; ++i) {
      Weapon* w = g->at(i);
      if (!w->is_beam()) {
        w->bin->set_amount(0);
        done = true;
      }
    }
  } else if (Weapon* w = g->current_weapon; w && !w->is_beam()) {
    w->bin->set_amount(0);
    done = true;
  }
  if (done) {
    refresh_hud();
    release();
  }
  return done;
}

// Mech3.exe 1.2 @ 0x004b0bc0
void WeaponManager::set_aim(const Vec3f* at) {
  if (at) {
    aim = *at;
    aim_valid = true;
  } else {
    aim_valid = false;
  }
}

// Mech3.exe 1.2 @ 0x004b0bf0
void WeaponManager::set_online(bool on) {
  if (!on) {
    release();
    set_aim(nullptr);
  }
  online = on;
}

// Mech3.exe 1.2 @ 0x004b0c30. Chain fire (mode 0) steps to the group's next
// weapon on release; an alpha strike holds everything for its time.
void WeaponManager::update(float dt) {
  for (uint32_t i = 0; i < count; ++i) weapons[i]->update(dt);
  // FIX (bug): without a selected group the original calls through a null
  // group (a crash); here nothing fires (docs/fixes.md).
  WeaponGroup* g = group_ptr();
  if (alpha_end == 0.0f) {
    state = 0;
    if (trigger_down) {
      if (!trigger_was) {
        state = 1;
        if (!g || !g->trigger()) trigger_down = false;
      } else {
        state = 2;
        if (g) g->held();
      }
    } else if (trigger_was) {
      state = 3;
      if (g) {
        g->release();
        if (mode == 0) g->next();
      }
    }
  } else if (!(g_world.clock < alpha_end)) {
    state = 3;
    alpha_release();
    alpha_end = 0.0f;
  } else {
    state = 2;
  }
}

// Mech3.exe 1.2 @ 0x004b0de0: a critical on `part` (-1 any) destroys the
// weapon, or with `ammo` empties the bin, that is worth the least.
bool WeaponManager::critical(int32_t part, bool ammo) {
  float best = HUGE_VALF;
  bool result = false;
  if (ammo) {
    Config* cfg = g_calls.config ? g_calls.config(owner) : nullptr;
    ConfigItem* best_item = nullptr;
    Weapon* best_w = nullptr;
    for (ConfigItem* it : cfg->items) {
      if (it->kind() == 2) {
        if (part != -1 && it->part != part) continue;
        Weapon* w = find(it->id(), 0);
        if (!w) continue;
        const int32_t n = w->ammo();
        if (n == 0) continue;
        const float v = std::fabs(w->rating * static_cast<float>(n));
        if (v < best) {
          best = v;
          best_item = it;
          best_w = w;
        }
      }
      // A CASE item in the part is looked at and ignored (dead code, 0x0004b0ee9).
    }
    if (best_item) {
      best_w->bin->set_amount(0);
      result = true;
      release();
    }
    return result;
  }
  Weapon* best_w = nullptr;
  for (uint32_t i = 0; i < count; ++i) {
    Weapon* w = weapons[i];
    if (part != -1) {
      Handle n = node_of(w->part);
      const int32_t id = n && g_calls.node_id ? g_calls.node_id(n) : -1;
      if (id != part) continue;
    }
    if (w->damage == 1.0f) continue;
    const uint32_t f = w->def->flags;
    float v;
    if ((f & zwep::kBeam) && !(f & zwep::kTimedDamage)) v = w->rating;
    else v = std::fabs(static_cast<float>(w->ammo()) * w->rating);
    if (v < best) {
      best = v;
      best_w = w;
    }
  }
  if (best_w) {
    best_w->add_damage(1.0f);
    if (is_player(owner) && g_calls.voice_destroyed) g_calls.voice_destroyed(best_w->index);
    release();
    result = true;
  }
  return result;
}

// Mech3.exe 1.2 @ 0x004b1040: every weapon on a part that came off.
void WeaponManager::part_lost(int32_t part) {
  for (uint32_t i = 0; i < count; ++i) {
    Weapon* w = weapons[i];
    if (w->firepoint && w->firepoint->part == part && w->damage != 1.0f) {
      ++destroyed;
      w->destroy();
    }
  }
  if (WeaponGroup* g = group_ptr(); g && g->count == 0) next_group();
  refresh_hud();
  release();
}

// Mech3.exe 1.2 @ 0x004b1110
void WeaponManager::destroy_all() {
  for (uint32_t i = 0; i < count; ++i) {
    if (weapons[i]->damage != 1.0f) {
      ++destroyed;
      weapons[i]->destroy();
    }
  }
  release();
}

// Mech3.exe 1.2 @ 0x004b2da0. In the side views, or in free look with the
// torso turned more than 35 degrees from the view, only one arm's weapons fire.
bool WeaponManager::arm_restriction() {
  int32_t side = -1;
  if (is_player(owner)) {
    Handle v = g_calls.view ? g_calls.view() : nullptr;
    if (v) {
      const int32_t k = g_calls.view_kind ? g_calls.view_kind(v) : -1;
      if (k == 0) side = 2;
      else if (k == 1) side = 3;
    }
    if (side == -1 && g_calls.control_mode && g_calls.control_mode(owner) == 1) {
      const zmth::Quat* body = g_calls.torso_aim ? g_calls.torso_aim(owner) : nullptr;
      if (const zmth::Quat* look = g_calls.torso_look ? g_calls.torso_look(owner) : nullptr) {
        if (!g_world.cos_35_made) {
          g_world.cos_35 = static_cast<float>(std::cos(static_cast<double>(crt::x87_mul_f24(35.0, 0.017453292519943295))));
          g_world.cos_35_made = true;
        }
        const zmth::Vec3 ahead{0.0f, 0.0f, -1.0f};  // 0x005eda80 with z = -1
        zmth::Vec3 a{}, b{};
        zmth::quat_rotate(*body, ahead, &a);
        zmth::quat_rotate(*look, ahead, &b);
        if ((a.x * b.x + a.y * b.y) + a.z * b.z < g_world.cos_35) {
          const float cross_y = a.z * b.x - a.x * b.z;
          side = cross_y > 0.0f ? 2 : 3;
        }
      }
    }
  }
  bool changed = false;
  for (uint32_t i = 0; i < count; ++i)
    if (weapons[i]->set_enabled(side, false)) changed = true;
  return changed;
}

// Mech3.exe 1.2 @ 0x004b2530
Weapon::Weapon(WeaponItem* it, WeaponManager* m, bool beam) : item(it), manager(m) {
  Handle owner = m->owner;
  std::vector<Firepoint*>* fps = g_calls.firepoints ? g_calls.firepoints(owner) : nullptr;
  def = zwep::find_def(weapon_name(it->weapon));
  const int32_t fi = it->part;
  const int32_t n = fps ? static_cast<int32_t>(fps->size()) : 0;
  firepoint = fi >= 0 && fi < n ? (*fps)[static_cast<size_t>(fi)] : nullptr;
  // A second weapon on a firepoint is moved off it, unless it fires salvos.
  if (!(type_of(def)->salvo_truecount > 0)) {
    for (uint32_t i = 0; i < m->count; ++i) {
      if (m->weapons[i]->firepoint == firepoint) {
        spread.x = firepoint->share * rand_unit();
        spread.y = firepoint->share * rand_unit();
        spread.z = 0.0f;
        break;
      }
    }
  }
  index = it->weapon;
  part = part_of(owner, firepoint);
  node = node_of(part);
  if (beam) return;
  rating = def ? shot_rating(def, salvo != 0) : 0.0f;
  // One bin per weapon type: share another weapon's (each match adds a reference).
  AmmoBin* b = nullptr;
  for (uint32_t i = 0; i < m->count; ++i) {
    Weapon* o = m->weapons[i];
    if (o->def == def && o->bin) {
      b = o->bin;
      ++b->refs;
      bin = b;
      b->attach(this);
    }
  }
  if (!b) {
    bin = new AmmoBin;
    bin->attach(this);
  }
}

// Mech3.exe 1.2 @ 0x004b28d0: a weapon on a vehicle that is not a mech,
// its firepoint found by the node's name.
Weapon::Weapon(WeaponDef* d, Handle n, WeaponManager* m, bool beam) : node(n), def(d), manager(m) {
  part = g_calls.node_part ? g_calls.node_part(n) : nullptr;
  if (part) {
    Handle mech = g_calls.part_mech ? g_calls.part_mech(part) : nullptr;
    if (mech) {
      std::vector<Firepoint*>* fps = g_calls.firepoints ? g_calls.firepoints(mech) : nullptr;
      firepoint = nullptr;
      if (fps && !fps->empty()) {
        const char* name = g_calls.node_name ? g_calls.node_name(n) : "";
        for (Firepoint* fp : *fps) {
          if (std::strcmp(name, fp->name) == 0) {
            firepoint = fp;
            break;
          }
        }
      }
    }
  }
  if (beam) return;
  rating = def ? shot_rating(def, salvo != 0) : 0.0f;
  AmmoBin* b = nullptr;
  for (uint32_t i = 0; i < m->count; ++i) {
    Weapon* o = m->weapons[i];
    if (o->def == def && o->bin) {
      b = o->bin;
      ++b->refs;
      bin = b;
      b->attach(this);
    }
  }
  if (!b) {
    bin = new AmmoBin;
    bin->attach(this);
  }
}

// Mech3.exe 1.2 @ 0x004b2bc0. The sounds are stopped, not cleared.
Weapon::~Weapon() {
  if (bin && --bin->refs == 0) delete bin;
  bin = nullptr;
  halt(loop_sound);
  halt(recharge_sound);
}

// Mech3.exe 1.2 @ 0x004b2230
Weapon* make_weapon(WeaponItem* item, WeaponManager* m) {
  if (!item || !m) return nullptr;
  WeaponDef* def = zwep::find_def(weapon_name(item->weapon));
  if (!def) return nullptr;
  if (def->flags & zwep::kBeam) {
    Weapon* w = new BeamWeapon(item, m);
    w->slot = static_cast<int32_t>(m->count);
    return w;
  }
  Weapon* w = new Weapon(item, m, false);
  w->slot = static_cast<int32_t>(m->count);
  if (w->bin && !weapon_uses_ammo(item->weapon)) w->bin->infinite = true;
  if (const int32_t s = type_of(w->def)->salvo_count) w->salvo = s;
  return w;
}

// Mech3.exe 1.2 @ 0x004b23f0
Weapon* make_vehicle_weapon(int32_t index, Handle node, WeaponManager* m) {
  if (index == -1 || !node) return nullptr;
  WeaponDef* def = zwep::find_def(weapon_name(index));
  if (!def) return nullptr;
  if (def->flags & zwep::kBeam) {
    Weapon* w = new BeamWeapon(def, node, m);
    w->index = index;
    w->slot = static_cast<int32_t>(m->count);
    return w;
  }
  Weapon* w = new Weapon(def, node, m, false);
  w->index = index;
  w->slot = static_cast<int32_t>(m->count);
  if (w->bin && !weapon_uses_ammo(index)) w->bin->infinite = true;
  if (const int32_t s = type_of(w->def)->salvo_count) w->salvo = s;
  return w;
}

// Mech3.exe 1.2 @ 0x004b4a40
Handle spawn_projectile(WeaponDef* def, Handle node, Handle x64, const Vec3f* muzzle, const Vec3f* dir,
                        const Vec3f* velocity, int32_t p5, int32_t p6, bool hostile) {
  Handle p = spawn_shot(def, node, x64, muzzle, dir, velocity, p5, p6, hostile);
  if (p && type_of(def)->show_on_radar) {
    if (g_calls.radar_track) {
      g_calls.radar_track(p, hostile);
    } else {
      auto* q = static_cast<zwep::Projectile*>(p);
      q->tick = hostile ? radar_hostile : radar_not_hostile;
      q->tick_arg = reinterpret_cast<uintptr_t>(q);
    }
  }
  return p;
}

namespace {
std::list<RadarBlip*> g_blips;     // 0x0060b228
Handle g_radar_node = nullptr;     // 0x0060b26c
}  // namespace

std::list<RadarBlip*>& radar_blips() { return g_blips; }

// Mech3.exe 1.2 @ 0x004b49c0: a shot on the radar for this frame (each shot's
// tick puts it back every frame; the radar's action empties the list).
void radar_add(zwep::Projectile* p, bool hostile) { g_blips.push_back(new RadarBlip{p, hostile}); }

// Mech3.exe 1.2 @ 0x004b4a20
void radar_not_hostile(zwep::Projectile* p) { radar_add(p, false); }

// Mech3.exe 1.2 @ 0x004b4a30
void radar_hostile(zwep::Projectile* p) { radar_add(p, true); }

// Mech3.exe 1.2 @ 0x004b4b10 (the list's clear), run as the radar object's
// action 0x004b49b0.
void radar_clear() {
  for (RadarBlip* b : g_blips) delete b;
  g_blips.clear();
}

// Mech3.exe 1.2 @ 0x004b4ab0: the object whose action clears the list, at
// priority 2 (after the weapons' tick at 3, likely).
void radar_objects_create() {
  g_radar_node = g_calls.object3d_create ? g_calls.object3d_create() : nullptr;
  if (g_calls.set_action_priority) g_calls.set_action_priority(g_radar_node, 2);
  if (g_calls.set_action) g_calls.set_action(g_radar_node, radar_clear);
}

// Mech3.exe 1.2 @ 0x004b4ae0
void radar_objects_destroy() {
  radar_clear();
  if (!g_radar_node) return;
  if (g_calls.set_action) g_calls.set_action(g_radar_node, nullptr);
  if (g_calls.node_destroy) g_calls.node_destroy(g_radar_node);
  g_radar_node = nullptr;
}

// Mech3.exe 1.2 @ 0x00414501..0x00414732 (mech.cpp)
void configure_weapons(Handle mech, WeaponManager* m, Config* cfg, Config** kept, int32_t equipment[15], Ams* ams,
                       Handle node) {
  m->reset();
  for (ConfigItem* it : cfg->items) {
    if (it->kind() != 1) continue;
    auto* w = static_cast<WeaponItem*>(it);
    if (!m->add(w)) {
      char msg[256];
      std::snprintf(msg, sizeof msg, "Failed to add weapon (%s) firepoint=(%d)", weapon_name(w->weapon), w->part);
      zerr::report(0x400, "C:\\zipper\\Mech3\\mech.cpp", 0x1b1, msg);
    }
  }
  *kept = cfg;
  for (ConfigItem* it : cfg->items) {
    if (it->kind() == 2) {
      auto* a = static_cast<AmmoItem*>(it);
      if (!weapon_uses_ammo(a->weapon)) continue;
      Weapon* w = m->find(a->weapon, 0);
      if (w && a->count && w->bin) w->bin->add(a->count);
    } else if (it->kind() == 3) {
      const int32_t e = equipment_family(static_cast<EquipmentItem*>(it)->equipment);
      if (e < 0 || e >= 15) {
        zerr::report(0x400, "content", 0, "Equipment item has no implemented behavior family");
        continue;
      }
      if (e == 11) {
        if (g_calls.add_jump_jet) g_calls.add_jump_jet(mech);
      } else {
        ++equipment[e];
      }
      if (e == 0) {
        ams->rounds = equipment[0] * 100;
      } else if (e == 13) {
        const std::vector<Firepoint*>* fps = g_calls.firepoints ? g_calls.firepoints(mech) : nullptr;
        const Firepoint* fp = fps && !fps->empty() ? (*fps)[0] : nullptr;
        if (!fp) continue;
        if (Weapon* t = m->add(0x20, node, fp->name, 4)) {
          t->set_group(4);
        } else {
          char msg[256];
          std::snprintf(msg, sizeof msg, "Failed to add weapon (%s) firepoint=(%s)", weapon_name(0x20), fp->name);
          zerr::report(0x400, "C:\\zipper\\Mech3\\mech.cpp", 0x1e0, msg);
        }
      }
    }
  }
}

namespace {
void vehicle_error(int line, const char* fmt, const char* a, const char* b, const char* c) {
  char msg[512];
  std::snprintf(msg, sizeof msg, fmt, a, b, c);
  zerr::report(0x200, "C:\\zipper\\Mech3\\weapon.cpp", line, msg);
}
}  // namespace

// Mech3.exe 1.2 @ 0x004b4660: each item of `weapons` is (name n n ammo ...
// "firepoint" (names)). The firepoint is looked up in the whole list, not the
// item, so every weapon takes the first item's first firepoint (a quirk:
// docs/weapons.md). Ammo 0 is an endless bin, for all but beams; group 4,
// then 0.
WeaponManager* build_vehicle_weapons(Handle vehicle, WeaponManager* old, zrdr::Node* def) {
  delete old;
  zrdr::Node* list = zrdr::find(def, "weapons");
  if (!list) return nullptr;
  auto* m = new WeaponManager(vehicle);
  Handle vnode = g_calls.vehicle_node ? g_calls.vehicle_node(vehicle) : nullptr;
  const char* vname = g_calls.node_name ? g_calls.node_name(vnode) : "";
  const char* fp = "firepoint";
  const int32_t n = zrdr::list_count(list) - 1;
  for (int32_t i = 1; i <= n; ++i) {
    const zrdr::Node* item = list->list[i].list;
    const char* name = item[1].s;
    const int32_t ammo = item[4].i;  // read as an int whatever its type
    if (const char* f = zrdr::get_string(list, "firepoint")) fp = f;
    WeaponDef* d = zwep::find_def(name);
    Handle fpn = g_calls.node_find ? g_calls.node_find(vnode, fp) : nullptr;
    const int32_t idx = weapon_index(name);
    if (d && idx != -1 && fpn) {
      if (Weapon* w = m->add(idx, vnode, fp, 4)) {
        w->set_group(0);
        if (AmmoBin* b = w->bin) {
          if (ammo == 0) {
            if (!w->is_beam()) b->infinite = true;
          } else {
            b->add(ammo);
          }
        }
      }
      continue;
    }
    if (!d) vehicle_error(0xd94, "vehicle %s: weapon %s: no ZWEP_ID", vname, name, nullptr);
    if (idx == -1) vehicle_error(0xd97, "vehicle %s: weapon %s: no MECH_WEAPON", vname, name, nullptr);
    if (!fpn) vehicle_error(0xd9a, "vehicle %s: weapon %s: no firepoint node named %s", vname, name, fp);
  }
  return m;
}

// Mech3.exe 1.2 @ 0x004b0d80: the player's HUD ammo gauge redrawn (the HUD
// code is not ported; these are its calls). The original reads the screen of
// a null owner when there is no player; here nothing happens.
void WeaponManager::ammo_gauge() const {
  if (!owner || owner != g_world.player) return;
  Handle screen = g_calls.hud_screen ? g_calls.hud_screen(owner) : nullptr;
  if (!screen) return;
  const int32_t a = g_calls.gauge_6c ? g_calls.gauge_6c(screen) : 0;
  const int32_t b = g_calls.gauge_68 ? g_calls.gauge_68(screen) : 0;
  const int32_t v = g_calls.hud_440a30 ? g_calls.hud_440a30(screen, b, a) : 0;
  if (g_calls.hud_443d20) g_calls.hud_443d20(screen, v);
}

// Mech3.exe 1.2 @ 0x004b3ab0
Ams::Ams(float r, float p, int32_t n) : on(true), range(r), next(0), period(p), target(nullptr), rounds(n) {}

// Mech3.exe 1.2 @ 0x004b3ae0: the radar's nearest shot within range, by
// |dx| + |dz|: side 0 the shots not hostile to the player only (an enemy's
// AMS: a mech whose enemies hold the player's team, 0x0041927e; a turret's),
// 1 the hostile ones only (the player's side's), else all.
bool Ams::find(int32_t side, const zwep::Vec3f3* at) {
  if (rounds <= 0) return false;
  float best = range;
  for (RadarBlip* b : g_blips) {
    if (side == 0 && b->hostile) continue;
    if (side == 1 && !b->hostile) continue;
    const zwep::Projectile* p = b->shot;
    const float dz = std::fabs(p->position.z - at->z);
    const float dx = std::fabs(p->position.x - at->x);
    const float d = dz + dx;
    if (d < best) {
      best = d;
      target = b->shot;
    }
  }
  return target != nullptr;
}

// Mech3.exe 1.2 @ 0x004b3ba0
const zwep::Vec3f3* Ams::target_at() const { return target ? &target->position : nullptr; }

// Mech3.exe 1.2 @ 0x004b3bb0: once the period (shared out over `count` AMS)
// has passed, the target is shot down and a round spent; the target is
// forgotten either way.
void Ams::fire(int32_t count) {
  if (rounds != 0 && !(g_world.clock < next)) {
    const float f = count == 1 ? 1.0f : 1.0f / static_cast<float>(count);
    target->health = 0;
    --rounds;
    next = period * f + g_world.clock;
  }
  target = nullptr;
}

// 0x00585010 through the hook a test sets, else the port.
Handle spawn_shot(WeaponDef* def, Handle node, Handle x64, const Vec3f* muzzle, const Vec3f* dir,
                  const Vec3f* velocity, int32_t p5, int32_t p6, bool hostile) {
  if (g_calls.spawn) return g_calls.spawn(def, node, x64, muzzle, dir, velocity, p5, p6, hostile);
  return zwep::spawn(def, node, static_cast<const uint32_t*>(x64), reinterpret_cast<const zwep::Vec3f3*>(muzzle),
                     reinterpret_cast<const zwep::Vec3f3*>(dir), reinterpret_cast<const zwep::Vec3f3*>(velocity), p5,
                     reinterpret_cast<zwep::Projectile*>(static_cast<uintptr_t>(static_cast<uint32_t>(p6))));
}

// Mech3.exe 1.2 @ 0x004b4de0
float Weapon::dps() const { return def ? shot_rating(def, salvo != 0) : 0.0f; }

// Mech3.exe 1.2 @ 0x004b2840
float Weapon::ready() const {
  const float p = def->period;
  if (p == 0.0f) return 1.0f;
  return (p - reload) / p;
}

// Mech3.exe 1.2 @ 0x004b3a90
WeaponGroup* Weapon::group_ptr() const { return group == -1 ? nullptr : manager->groups[group]; }

// Mech3.exe 1.2 @ 0x004b1e10
bool Weapon::set_group(int32_t g) {
  if (g == group) return true;
  WeaponGroup* from = group_ptr();
  WeaponGroup* to = g == -1 ? nullptr : manager->groups[g];
  if (from) from->remove(this);
  if (to) to->add(this);
  group = g;
  if (item) item->group = g;
  manager->refresh_hud();
  return true;
}

// Mech3.exe 1.2 @ 0x004b2f90: under water only beams fire; `side` (2 or 3)
// keeps one arm. Returns whether it changed.
bool Weapon::set_enabled(int32_t side, bool force) {
  const bool under = !(def->flags & zwep::kBeam) && in_water(part);
  const bool match = side == -1 || firepoint->part == side;
  const bool on = (!under && match) || force;
  const bool changed = on != enabled;
  enabled = on;
  return changed;
}

// Mech3.exe 1.2 @ 0x004b2d50
void Weapon::add_damage(float d) {
  damage = d + damage;
  if (damage > 1.0f) damage = 1.0f;
  else if (damage < 0.0f) damage = 0.0f;
  if (damage == 1.0f) destroy();
}

// Mech3.exe 1.2 @ 0x004b3020
void Weapon::destroy() {
  damage = 1.0f;
  release();
  if (is_beam()) static_cast<BeamWeapon*>(this)->stop();
  const WeaponType* t = type_of(def);
  Handle owner = manager->owner;
  if (is_player(owner) && g_calls.destroyed_sounds) g_calls.destroyed_sounds(t->name_sound, t->part_destroy_sound);
  if (manager->current() == this) {
    if (is_player(owner) && g_calls.screen && g_calls.screen(owner)) {
      // The HUD's highlighted weapon becomes the current one.
      if (Weapon* hl = g_calls.hud_highlight ? g_calls.hud_highlight(owner) : nullptr) {
        if (manager->select_group(hl->group)) manager->group_ptr()->select(hl);
      }
      if (g_calls.hud_weapons) g_calls.hud_weapons(owner);
    } else {
      manager->next_weapon();
    }
  }
  halt(loop_sound);
  loop_sound = nullptr;
  halt(recharge_sound);
  recharge_sound = nullptr;
}

// Mech3.exe 1.2 @ 0x004b3a20
bool Weapon::can_fire() const {
  if (!def || !enabled || !manager->online || !(reload <= 0.0f)) return false;
  if (!(ammo() > 0 || ammo() == -1)) return false;
  return damage < 1.0f;
}

// Mech3.exe 1.2 @ 0x004b3990
float Weapon::min_range_sq() const { return def->range_min[0] * def->range_min[0]; }

// Mech3.exe 1.2 @ 0x004b39a0
float Weapon::max_range() const {
  const WeaponType* t = type_of(def);
  return (def->flags & zwep::kBeam) && in_water(part) ? t->half_range : t->range;
}

// Mech3.exe 1.2 @ 0x004b39e0
float Weapon::max_range_sq() const {
  const WeaponType* t = type_of(def);
  return (def->flags & zwep::kBeam) && in_water(part) ? t->half_range_sq : t->range_sq;
}

// Mech3.exe 1.2 @ 0x004b2c20
void Weapon::update(float dt) {
  if (damage == 1.0f) return;
  const WeaponType* t = type_of(def);
  if (Handle owner = manager->owner) {
    part = part_of(owner, firepoint);
    node = node_of(part);
  }
  dt *= cheat_rate(manager->owner && manager->owner == g_world.mission_player);  // ours: operation cwal (cheats.h)
  if (reload > dt) {
    reload = reload - dt;
    if (manager->owner == g_world.mission_player && t->recharge_sound && !recharge_sound && g_calls.play_loop)
      recharge_sound = g_calls.play_loop(t->recharge_sound);
  } else {
    reload = 0.0f;
    if (recharge_sound) {
      halt(recharge_sound);
      recharge_sound = nullptr;
    }
  }
  // The rest of a salvo: one projectile a frame, without the fire sound and
  // animation, re-aimed at the aim point.
  if (salvo_left != 0) {
    zwep::set_fire_sound(def, -1);
    zwep::set_fire_animation(def, -1);
    const Vec3f* vel = g_calls.vehicle_velocity ? g_calls.vehicle_velocity(manager->vehicle) : nullptr;
    target = manager->aim;
    if (aim(&target, &muzzle, &direction, vel)) fire(vel);
    --salvo_left;
  }
}

// Mech3.exe 1.2 @ 0x004b3330
bool Weapon::try_lock(Handle record, bool* f) {
  if (!record || !manager->has_target) return false;
  return lock_on(g_calls.record_target ? g_calls.record_target(record) : nullptr, f);
}

// Mech3.exe 1.2 @ 0x004b3370. FCS in the firing part, against a target without
// ECM, sets the doubled tracking that zWeapon then resets unread
// (docs/weapons.md, "Lock-on").
bool Weapon::lock_on(Handle tgt, bool* f) {
  if (!(def->flags & zwep::kLockOn)) return false;
  if (!tgt) {
    set_lock_globals(nullptr, nullptr);
    return true;
  }
  bool ecm = false;
  if (Handle tm = g_calls.target_mech ? g_calls.target_mech(tgt) : nullptr) {
    if (g_calls.lock_notice) g_calls.lock_notice(tm, manager->vehicle, def);
    if (g_calls.ecm_count && g_calls.ecm_count(tm) > 0) ecm = true;
  }
  lock.entity = tgt;
  lock.at = g_calls.target_position ? g_calls.target_position(tgt) : nullptr;
  lock.velocity = g_calls.target_velocity ? g_calls.target_velocity(tgt) : nullptr;
  lock_flag = 1;
  set_lock_globals(&lock_flag, &lock);
  if (!ecm && *f) {
    zwep::fire_state().track_scale[0] = 2.0f;
    zwep::fire_state().track_scale[1] = 2.0f;
    zwep::fire_state().terrain_follow = 1;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x004b3450: one projectile.
bool Weapon::fire(const Vec3f* velocity) {
  locked = false;
  Handle e = manager->vehicle && g_calls.vehicle_entity ? g_calls.vehicle_entity(manager->vehicle) : nullptr;
  Handle owner = manager->owner;
  if (owner && firepoint && g_calls.fcs_count && g_calls.fcs_count(owner) > 0)
    fcs = g_calls.equipment_in_part && g_calls.equipment_in_part(owner, 3, firepoint->part);
  else
    fcs = false;
  projectile_target = nullptr;
  Handle record = g_calls.entity_target ? g_calls.entity_target(e) : nullptr;
  if (owner && !(g_calls.owner_x58 && g_calls.owner_x58(owner))) {
    locked = try_lock(record, &fcs);
    if (g_world.lock_targets && locked) {
      Handle t = g_calls.record_target ? g_calls.record_target(record) : nullptr;
      projectile_target = t && g_calls.target_projectile ? g_calls.target_projectile(t) : nullptr;
    }
  }
  if ((def->fire.flags & 2) && firepoint) zwep::attach_fire(def, node, firepoint->position);
  const uint32_t team = g_calls.player_team ? g_calls.player_team() : 0;
  const uint32_t enemies = g_calls.entity_enemies ? g_calls.entity_enemies(e) : 0;
  spawn_projectile(def, g_calls.entity_node ? g_calls.entity_node(e) : nullptr,
                   g_calls.entity_x64 ? g_calls.entity_x64(e) : nullptr, &muzzle, &direction, velocity, 0, 0,
                   (team & enemies) != 0);  // hostile to the player
  if (owner == g_world.mission_player && g_world.force_feedback && g_calls.ff_weapon) g_calls.ff_weapon(index);
  if (g_calls.test_fired) g_calls.test_fired(*this);
  if (owner) net_event(owner, this);
  set_lock_globals(nullptr, nullptr);
  reload = def->period;
  // Ammo for a whole salvo goes with its first projectile.
  if (bin && salvo_left == 0) {
    int32_t n = salvo ? type_of(def)->salvo_truecount : 1;
    if (n >= bin->amount) n = bin->amount;
    bin->amount -= n;
    if (bin->manager) bin->manager->ammo_value = bin->manager->ammo_value - static_cast<float>(n) * bin->damage;
  }
  if (is_player(owner) && g_calls.hud_fire) g_calls.hud_fire(owner);
  if (part) weapon_fired(part, this);
  return true;
}

// Mech3.exe 1.2 @ 0x004b3660
bool Weapon::aim_and_fire(const Vec3f* at, bool force) {
  if (!force && !can_fire()) return false;
  const Vec3f* vel = g_calls.vehicle_velocity ? g_calls.vehicle_velocity(manager->vehicle) : nullptr;
  target = *at;
  if (!aim(&target, &muzzle, &direction, vel)) return false;
  x6c = 0;
  return fire(vel);
}

// The cone's cosine (0.89, about 27 degrees). FIX (bug): wider than 4:3, the
// player's reticle reaches past it, and beams snapped to the part's line at
// the old 4:3 edge; the cone widens with the view so the snap starts at the
// same place on screen (fixes.md).
static double cone_cos(Handle owner) {
  constexpr double kCos = 0.89;
  const float r = zfix::options().bugs && is_player(owner) && g_calls.view_width ? g_calls.view_width() : 1.0f;
  if (!(r > 1.0f)) return kCos;
  const double t = std::sqrt(1.0 - kCos * kCos) / kCos * r;
  return 1.0 / std::sqrt(1.0 + t * t);
}

// Mech3.exe 1.2 @ 0x004b36e0: the muzzle (the firepoint, spread out, in the
// world) and the direction to `at`. Weapons off the centre torso keep within
// about 27 degrees (cos 0.89) of their part's facing.
bool Weapon::aim(Vec3f* at, Vec3f* mz, Vec3f* dir, const Vec3f*) {
  Handle n;
  if (part && firepoint) {
    n = node_of(part);
    *mz = {firepoint->position[0], firepoint->position[1], firepoint->position[2]};
  } else {
    n = node;
    *mz = {0.0f, 0.0f, 0.0f};
  }
  if (salvo_left != 0 && firepoint) {
    mz->x = firepoint->radius * rand_unit() + mz->x;
    mz->y = firepoint->radius * rand_unit() + mz->y;
  } else {
    *mz = {mz->x + spread.x, mz->y + spread.y, mz->z + spread.z};
  }
  if (g_calls.node_position && g_calls.node_position(n, mz) != 0) return false;
  Handle owner = manager->owner;
  if (firepoint && owner) {
    Handle p2 = part_of(owner, firepoint);
    Handle n2 = node_of(p2);
    if (!p2 || !n2) return true;
    const Vec3f ax = *g_calls.node_axis(n2);
    const Vec3f back{-ax.x, -ax.y, -ax.z};
    if (at) {
      direction_to(*mz, *at, dir);
      if (!part) return true;
      Handle pn = node_of(part);
      const int32_t id = pn && g_calls.node_id ? g_calls.node_id(pn) : -1;
      if (id == 1) return true;
      const float d = dot(back, *dir);
      if (static_cast<double>(d) < cone_cos(owner)) *dir = back;
      return true;
    }
    *dir = back;
    return true;
  }
  if (at) {
    direction_to(*mz, *at, dir);
    return true;
  }
  if (!node) return false;
  const Vec3f ax = *g_calls.node_axis(node);
  *dir = {-ax.x, -ax.y, -ax.z};
  return true;
}

void Weapon::out_of_ammo_note(bool fired) {
  if (bin && !fired && !bin->warned && bin->left() == 0 && is_player(manager->owner)) {
    bin->warned = true;
    mech_sound(0xe);
  }
}

// Mech3.exe 1.2 @ 0x004b3190
bool Weapon::trigger() {
  const bool fired = aim_and_fire(&manager->aim, false);
  if (fired) {
    if (salvo) salvo_left = salvo - 1;
    const WeaponType* t = type_of(def);
    if (t->looped_sound && !loop_sound && g_calls.play_at) loop_sound = g_calls.play_at(t->looped_sound, &muzzle);
  }
  out_of_ammo_note(fired);
  return fired;
}

// Mech3.exe 1.2 @ 0x004b3260
void Weapon::release() {
  halt(loop_sound);
  loop_sound = nullptr;
}

// Mech3.exe 1.2 @ 0x004b3280: held down, a weapon fires again as soon as it
// has reloaded; a salvo weapon starts a new salvo.
void Weapon::held() {
  bool again = false;
  if (salvo && reload <= 0.0f) {
    trigger();
    again = true;
  }
  if (!again) aim_and_fire(&manager->aim, false);
  if (ammo() == 0 && loop_sound) {
    halt(loop_sound);
    loop_sound = nullptr;
  }
  out_of_ammo_note(again);
}

// Mech3.exe 1.2 @ 0x004b3c10
BeamWeapon::BeamWeapon(WeaponItem* it, WeaponManager* m) : Weapon(it, m, true) { init(); }

// Mech3.exe 1.2 @ 0x004b3d70
BeamWeapon::BeamWeapon(WeaponDef* d, Handle n, WeaponManager* m) : Weapon(d, n, m, true) { init(); }

// The beam half of both constructors: its rating and its charge, which no
// manager counts.
void BeamWeapon::init() {
  beam = nullptr;
  firing = false;
  rating = def ? beam_cycle_rating(def) : 0.0f;
  bin = new AmmoBin;
  const int32_t full = crt::msvc_ftol(type_of(def)->fire_time * def->period);
  bin->amount = full;
  bin->max = full;
}

// Mech3.exe 1.2 @ 0x004b3e90. The base destructor stops the two sounds again.
BeamWeapon::~BeamWeapon() {
  if (firing) {
    BeamWeapon::release();
    stop();
  }
  halt(loop_sound);
  halt(recharge_sound);
  delete bin;
  bin = nullptr;
  if (beam) {
    if (g_calls.beam_free) beam = g_calls.beam_free(beam);
    else beam = reinterpret_cast<Handle>(static_cast<intptr_t>(zwep::beam_free(static_cast<zwep::Beam*>(beam))));
  }
}

// Mech3.exe 1.2 @ 0x004b4e50
float BeamWeapon::dps() const { return def ? beam_cycle_rating(def) : 0.0f; }

// Mech3.exe 1.2 @ 0x004b4470
float BeamWeapon::ready() const {
  if (bin->left() == bin->capacity()) return 1.0f;
  return static_cast<float>(bin->left()) / static_cast<float>(bin->capacity());
}

// Mech3.exe 1.2 @ 0x004b3f40
bool BeamWeapon::aim_and_fire(const Vec3f* at, bool) {
  if (!can_fire()) return false;
  aim_at = at;
  if (!aim(const_cast<Vec3f*>(at), &muzzle, &direction, nullptr)) return false;
  if (!beam) {
    Handle owner_node = g_calls.vehicle_node ? g_calls.vehicle_node(manager->vehicle) : nullptr;
    if (g_calls.beam_create) beam = g_calls.beam_create(def, owner_node, node, &muzzle, &direction);
    else beam = zwep::beam_make(def, owner_node, nullptr, reinterpret_cast<const zwep::Vec3f3*>(&muzzle),
                                reinterpret_cast<const zwep::Vec3f3*>(&direction), 1);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x004b3fc0: with a recharge time, only from a full charge.
bool BeamWeapon::trigger() {
  bool fired = false;
  bool ok = true;
  const WeaponType* t = type_of(def);
  if (t->recharge_rate != 0.0f) {
    if (bin->left() < bin->capacity()) {
      ok = false;
    } else if (recharge_sound) {
      halt(recharge_sound);
      recharge_sound = nullptr;
    }
  }
  if (!firing && ok && aim_and_fire(&manager->aim, false) && beam) {
    fired = true;
    fired_time = 0;
    if (in_water(part)) zwep::fire_state().beam_range_scale = 0.5f;
    const int32_t used = bin->amount > 1 ? 1 : bin->amount;
    bin->amount -= used;
    if (bin->manager) bin->manager->ammo_value = bin->manager->ammo_value - static_cast<float>(used) * bin->damage;
    if (g_calls.beam_start) g_calls.beam_start(beam);
    else zwep::beam_start(static_cast<zwep::Beam*>(beam));
    firing = true;
    if (Handle owner = manager->owner) {
      projectile_target = nullptr;
      net_event(owner, this);
      if (is_player(owner) && g_calls.hud_fire) g_calls.hud_fire(owner);
      if (part) weapon_fired(part, this);
      if (owner == g_world.mission_player && g_world.force_feedback && g_calls.ff_weapon) g_calls.ff_weapon(index);
    }
  }
  return fired;
}

// Mech3.exe 1.2 @ 0x004b4140
void BeamWeapon::stop() {
  if (!firing) return;
  if (g_calls.beam_stop) g_calls.beam_stop(beam);
  else zwep::beam_stop(static_cast<zwep::Beam*>(beam));
  firing = false;
  if (Handle owner = manager->owner) net_event(owner, this);
}

// Mech3.exe 1.2 @ 0x004b4180: only the long beams (pulse lasers) stop when let go.
void BeamWeapon::release() {
  if (type_of(def)->fire_time > 0.5f) stop();
}

// Mech3.exe 1.2 @ 0x004b41b0
void BeamWeapon::held() {
  if (!firing) {
    trigger();
    return;
  }
  if (!aim(const_cast<Vec3f*>(aim_at), &muzzle, &direction, nullptr)) return;
  Handle owner = manager->owner;
  if (part) {
    if (!owner) return;
    net_event(owner, this);
  }
  if (is_player(owner) && g_calls.hud_fire) g_calls.hud_fire(owner);
}

// The recharge step of 0x004b4230. FIX (B): the exe truncates rate x dt each
// frame, so the faster the game runs the more it loses: TAG (166.7 a second)
// refills in 67 s at 30 fps, 83 s at 60, never above 166 fps or at 144 fps
// once hot (docs/weapons.md, "TAG, end to end"). With per_second the 30 fps
// frame's whole units are spread over the frame and the fraction carried:
// the exe's amount at 30 fps, the same per second at any rate.
int32_t beam_recharge(float rate, float x, float dt, float* carry) {
  if (!zfix::options().per_second) return crt::msvc_ftol(rate * x);
  const float s = zfix::frame_steps(dt);
  if (s == 1.0f) return crt::msvc_ftol(rate * x);
  const float gain = static_cast<float>(crt::msvc_ftol(rate * (x / s))) * s + *carry;
  const int32_t n = static_cast<int32_t>(std::floor(gain));
  *carry = gain - static_cast<float>(n);
  return n;
}

// Mech3.exe 1.2 @ 0x004b4230. The charge drains while firing and comes back
// at U_RECHARGE_TIME, slower when hot (docs/weapons.md, "Beams").
void BeamWeapon::update(float dt) {
  if (damage == 1.0f) return;
  const WeaponType* t = type_of(def);
  // FIX (bug), behind bugs: the exe re-aims a beam only while its trigger is
  // held (0x004b41b0), and a short laser fires on after the release until its
  // charge is spent, from the last held frame's muzzle: behind a walking mech
  // (playtest #24, fixes.md). Here it follows the weapon as when held.
  if (firing && aim_at && zfix::options().bugs) aim(const_cast<Vec3f*>(aim_at), &muzzle, &direction, nullptr);
  float v = def->period * dt;
  if (!(v > 1.0f)) v = 1.0f;
  const int32_t n = crt::msvc_ftol(v);
  if (firing && (def->flags & zwep::kDesignate) && zfix::options().bugs && bin->left() >= 0) {
    // FIX (bug): TAG's beam lasts what it does at 30 fps in the exe, in time:
    // the same slack over the one-second hold at any frame rate (zfix bugs).
    fired_time += dt;
    const int32_t per = std::max(1, crt::msvc_ftol(def->period / 30.0f));  // the 30 fps frame's drain
    const int32_t drains = (bin->max - 1 + per - 1) / per;                 // updates that empty it after the trigger's 1
    if (std::floor(fired_time * 1000.0f + 0.5f) < std::floor((drains + 1) * 1000.0f / 30.0f + 0.5f)) {
      const float left = static_cast<float>(bin->max - 1) * (1.0f - fired_time * 30.0f / static_cast<float>(drains));
      bin->amount = left > 0.0f ? static_cast<int32_t>(left) : 0;
      return;
    }
    bin->amount = 0;
    stop();
    if (manager->owner == g_world.mission_player && t->recharge_sound && !recharge_sound && g_calls.play_loop)
      recharge_sound = g_calls.play_loop(t->recharge_sound);
    return;
  }
  if (firing) {
    if (bin->left() > 0) {
      const int32_t used = n < bin->amount ? n : bin->amount;
      bin->amount -= used;
      if (bin->manager) bin->manager->ammo_value = bin->manager->ammo_value - static_cast<float>(used) * bin->damage;
      if (def->flags & zwep::kTimedDamage) {
        const float share = static_cast<float>(used) / static_cast<float>(bin->capacity());
        if (Handle owner = manager->owner; owner && g_calls.add_heat) g_calls.add_heat(owner, t->heat * share);
      }
      return;
    }
    stop();
    if (manager->owner == g_world.mission_player && t->recharge_sound && !recharge_sound && g_calls.play_loop)
      recharge_sound = g_calls.play_loop(t->recharge_sound);
    return;
  }
  if (bin->left() < bin->capacity()) {
    const float h = g_calls.heat ? g_calls.heat(manager->owner) : 0.0f;
    float x;
    if (h > 0.5f) {
      float f = 0.5f / h;
      if (f > 1.0f) f = 1.0f;
      else if (f < 0.5f) f = 0.5f;
      x = f * dt;
    } else {
      x = dt;
    }
    int32_t amt = bin->amount + beam_recharge(t->recharge_rate, x, dt, &recharge_carry);  // FIX (B)
    if (amt >= bin->max) {
      amt = bin->max;
      recharge_carry = 0;
    }
    bin->set_amount(amt);
  } else if (recharge_sound) {
    halt(recharge_sound);
    recharge_sound = nullptr;
  }
}

}  // namespace mech3
