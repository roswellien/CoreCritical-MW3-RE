#include "mech3/zai_target.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_cull.h"
#include "mech3/zai_brain.h"
#include "mech3/zrdraiv.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3::ai {

namespace {
TargetHooks g_default_hooks;
TargetHooks* g_hooks = &g_default_hooks;
std::vector<CZAiTarget*> g_list;   // [0x0060b584..0x0060b588]
bool g_list_deleting = false;      // [0x0060b590]
Handle g_markers[3] = {};          // [0x0060b4f0..0x0060b4f8]

constexpr uint32_t kOnHit = 0x4d26b0;      // the weapon callback a target puts on its node
constexpr uint32_t kOnCollide = 0x4d26d0;  // and the collision one
constexpr uint32_t kAnimHit = 0x502e60;    // an animation's (zeff_anim_run)
constexpr uint32_t kAnimCollide = 0x502df0;
}  // namespace

void set_target_hooks(TargetHooks* h) { g_hooks = h ? h : &g_default_hooks; }
TargetHooks& target_hooks() { return *g_hooks; }
std::vector<CZAiTarget*>& target_list() { return g_list; }

// --- AiTarget -----------------------------------------------------------------------

void AiTarget::set_team(uint32_t t) {
  team = 1u << (t & 31);
  for (t >>= 5; t; t >>= 5) team |= 1u << (t & 31);
  allies = team & ~1u;
  enemies = ~allies & ~1u;
  g_hooks->sorted_clear();
}

// Mech3.exe 1.2 @ 0x004e6fe0: a contact's vehicle (none without what it saw).
// Mech3.exe 1.2 @ 0x004e7030: worked out once, on the first ask.
float Contact::distance() {
  if (dist < 0.0f) {
    zmth::Vec3 d{delta[0], delta[1], delta[2]};
    dist = zmth::normalize(d);
    dir[0] = d.x, dir[1] = d.y, dir[2] = d.z;
  }
  return dist;
}

Handle AiTarget::vehicle_of(const Contact* c) const { return c->seen ? g_hooks->seen_vehicle(c->seen) : 0; }
uint32_t AiTarget::team_of(Handle vehicle) const { return g_hooks->team_of(g_hooks->ai_target(vehicle)); }

// Mech3.exe 1.2 @ 0x004d6130
bool AiTarget::is_ally(Handle vehicle) const { return (allies & team_of(vehicle)) != 0; }
// Mech3.exe 1.2 @ 0x004d6160
bool AiTarget::is_enemy(Handle vehicle) const { return (enemies & team_of(vehicle)) != 0; }

// Mech3.exe 1.2 @ 0x004d6190. The inline copies in the pick and the target
// keys ask the same, in the same order (enemies first for "neither").
bool AiTarget::on_side(int32_t side, Handle vehicle) const {
  switch (side) {
    case kAlly: return is_ally(vehicle);
    case kEnemy: return is_enemy(vehicle);
    case kNeutral: return !is_enemy(vehicle) && !is_ally(vehicle);
    default: return true;
  }
}

// Mech3.exe 1.2 @ 0x004d5e40. Without keep_sides a target remembers itself
// as the last ally or enemy picked; none clears those it was.
bool AiTarget::set_target(Contact* c, bool keep_sides) {
  if (!keep_sides) {
    if (!c) {
      if (ally_target == target) ally_target = nullptr;
      if (enemy_target == target) enemy_target = nullptr;
    } else {
      const Handle v = vehicle_of(c);
      if (is_ally(v)) {
        ally_target = target = c;
        return true;
      }
      if (is_enemy(v)) {
        enemy_target = target = c;
        return true;
      }
    }
  }
  target = c;
  return true;
}

// Mech3.exe 1.2 @ 0x004d6b90: the first contact of the side, in sight or
// not; the list is nearest first. The brain's pick.
Contact* AiTarget::first_of(int32_t side) const {
  for (Contact* c : contacts)
    if (on_side(side, vehicle_of(c))) return c;
  return nullptr;
}

namespace {
// The target keys (0x004d5ee0, 0x004d6260, 0x004d64b0, 0x004d6710, 0x004d6970,
// 0x004d6a80), written out per key in the exe; the enemy keys take a contact
// in sight that is not of the side, the ally keys one that is.
struct Keys {
  AiTarget& t;
  int32_t side;
  bool enemy;
  bool wanted(Contact* c, Handle vehicle_of_c) const {
    return (c->flags & 1) && t.on_side(side, vehicle_of_c) != enemy;
  }
};
}  // namespace

// Mech3.exe 1.2 @ 0x004d6970 (enemy false) and 0x004d6a80 (true).
void AiTarget::nearest_ally(int32_t side) {
  Keys k{*this, side, false};
  for (Contact* c : contacts)
    if (k.wanted(c, vehicle_of(c))) return g_hooks->target_set(*this, c);
}
void AiTarget::nearest_enemy(int32_t side) {
  Keys k{*this, side, true};
  for (Contact* c : contacts)
    if (k.wanted(c, vehicle_of(c))) return g_hooks->target_set(*this, c);
}

namespace {
// The last pick of the other kind first: the ally keys go back to the last
// ally for side 0 (the last enemy for 1), the enemy keys the other way.
bool last_pick(AiTarget& t, int32_t side, bool enemy) {
  Contact* last = nullptr;
  if (side == 0) last = enemy ? t.enemy_target : t.ally_target;
  else if (side == 1) last = enemy ? t.ally_target : t.enemy_target;
  if (last && last != t.target) {
    g_hooks->target_set(t, last);
    return true;
  }
  return false;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004d5ee0 (Shift+W) and 0x004d6260 (Shift+E): the one
// before the current target that is wanted, else the last one wanted after
// it; with no current target in the list, the nearest.
void AiTarget::prev_ally(int32_t side) {
  if (last_pick(*this, side, false)) return;
  Keys k{*this, side, false};
  Contact* cur = target;
  if (cur) {
    const size_t n = contacts.size();
    size_t p = n;  // one past the current
    while (p != 0 && !same(contacts[p - 1], cur)) --p;
    if (p != 0) {
      size_t q = p;
      for (;;) {
        if (q - 1 == 0) break;
        Contact* c = contacts[q - 2];
        if (k.wanted(c, vehicle_of(c))) break;
        --q;
      }
      if (q - 1 != 0) return g_hooks->target_set(*this, contacts[q - 2]);
      for (size_t it = n; it != p; --it) {
        Contact* c = contacts[it - 1];
        if (k.wanted(c, vehicle_of(c))) return g_hooks->target_set(*this, c);
      }
      return;
    }
  }
  nearest_ally(side);
}
void AiTarget::prev_enemy(int32_t side) {
  if (last_pick(*this, side, true)) return;
  Keys k{*this, side, true};
  Contact* cur = target;
  if (cur) {
    const size_t n = contacts.size();
    size_t p = n;
    while (p != 0 && !same(contacts[p - 1], cur)) --p;
    if (p != 0) {
      size_t q = p;
      for (;;) {
        if (q - 1 == 0) break;
        Contact* c = contacts[q - 2];
        if (k.wanted(c, vehicle_of(c))) break;
        --q;
      }
      if (q - 1 != 0) return g_hooks->target_set(*this, contacts[q - 2]);
      for (size_t it = n; it != p; --it) {
        Contact* c = contacts[it - 1];
        if (k.wanted(c, vehicle_of(c))) return g_hooks->target_set(*this, c);
      }
      return;
    }
  }
  nearest_enemy(side);
}

// Mech3.exe 1.2 @ 0x004d64b0 (W) and 0x004d6710 (E): the one after the
// current target that is wanted, else the first one wanted before it.
void AiTarget::next_ally(int32_t side) {
  if (last_pick(*this, side, false)) return;
  Keys k{*this, side, false};
  Contact* cur = target;
  if (cur && !contacts.empty()) {
    const size_t n = contacts.size();
    size_t p = 0;
    while (!same(contacts[p], cur) && ++p != n) {}
    if (p != n) {
      size_t q = p;
      for (;;) {
        if (++q == n) break;
        Contact* c = contacts[q];
        if (k.wanted(c, vehicle_of(c))) break;
      }
      if (q == n) {
        for (q = 0; q != p; ++q) {
          Contact* c = contacts[q];
          if (k.wanted(c, vehicle_of(c))) break;
        }
        if (q == p) return;
      }
      return g_hooks->target_set(*this, contacts[q]);
    }
  }
  nearest_ally(side);
}
void AiTarget::next_enemy(int32_t side) {
  if (last_pick(*this, side, true)) return;
  Keys k{*this, side, true};
  Contact* cur = target;
  if (cur && !contacts.empty()) {
    const size_t n = contacts.size();
    size_t p = 0;
    while (!same(contacts[p], cur) && ++p != n) {}
    if (p != n) {
      size_t q = p;
      for (;;) {
        if (++q == n) break;
        Contact* c = contacts[q];
        if (k.wanted(c, vehicle_of(c))) break;
      }
      if (q == n) {
        for (q = 0; q != p; ++q) {
          Contact* c = contacts[q];
          if (k.wanted(c, vehicle_of(c))) break;
        }
        if (q == p) return;
      }
      return g_hooks->target_set(*this, contacts[q]);
    }
  }
  nearest_enemy(side);
}

// Mech3.exe 1.2 @ 0x004d6d00
void AiTarget::set_range(float r) {
  range = r;
  range2 = r * r;
}

// Mech3.exe 1.2 @ 0x004d6d20: the player's own vehicle (or one whose vt+0x58
// answers) in Instant Action or multiplayer sees 50000 (0x005e8d8c); every
// other range is capped at 1000 (0x005e8d84); a NaN is kept (C0|C3).
void AiTarget::set_sensor_range(Handle self, float r) {
  TargetHooks& h = *g_hooks;
  bool wide = false;
  sensor_range = r;
  if (h.have_pilot()) {
    if (h.self_vehicle(self) == h.player_vehicle() || h.self_x58(self) != 0) {
      const int32_t type = h.game_type();
      wide = type == 3 || type == 2;
    }
  }
  if (wide) r = 50000.0f;
  else if (r > 1000.0f) r = 1000.0f;
  range = r;
  range2 = r * r;
}

// Mech3.exe 1.2 @ 0x004a7be0
void AiTarget::clear_contacts() {
  g_hooks->contact_delete(own_player);
  g_hooks->contact_delete(own_lance);
  own_player = own_lance = nullptr;
  for (Contact*& c : contacts) {
    g_hooks->contact_delete(c);
    c = nullptr;
  }
  contacts.clear();
}

// --- The description (0x004d2480..0x004d2660) -----------------------------------------

// Mech3.exe 1.2 @ 0x004d2660
void desc_free(TargetDesc& d) {
  delete d.triggers;
  d.triggers = nullptr;
  d.next = 0;
  d.named = false;
  d.name.clear();
}

namespace {
// MSVC's std::sort for health_below (0x004d21f0 and the pieces it calls,
// 0x004d3680, 0x004d38f0, 0x004d3890, 0x004d39f0, 0x004d3a80): ascending by
// `before`, which here puts the higher health first.
bool before(const HealthBelow& a, const HealthBelow& b) { return b.health < a.health; }  // 0x004d1f40

using It = std::vector<HealthBelow>::iterator;
void insertion_sort(It f, It l) {
  if (f == l) return;
  for (It i = f + 1; i != l; ++i) {
    HealthBelow v = *i;
    if (before(v, *f)) {
      std::move_backward(f, i, i + 1);
      *f = v;
    } else {
      It j = i;
      for (It k = j - 1; before(v, *k); --k) *j = *k, j = k;
      *j = v;
    }
  }
}
const HealthBelow& median(const HealthBelow& a, const HealthBelow& b, const HealthBelow& c) {
  if (before(a, b)) return before(b, c) ? b : before(a, c) ? c : a;
  return before(a, c) ? a : before(b, c) ? c : b;
}
It partition(It f, It l, HealthBelow piv) {
  for (;; ++f) {
    while (before(*f, piv)) ++f;
    for (--l; before(piv, *l); --l) {}
    if (!(f < l)) return f;
    std::iter_swap(f, l);
  }
}
void sort_big(It f, It l) {
  while (l - f > 16) {
    It m = partition(f, l, median(*f, *(f + (l - f) / 2), *(l - 1)));
    if (l - m <= m - f) {
      sort_big(m, l);
      l = m;
    } else {
      sort_big(f, m);
      f = m;
    }
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x004d21f0
void sort_triggers(std::vector<HealthBelow>& v) {
  if (v.size() <= 16) return insertion_sort(v.begin(), v.end());
  sort_big(v.begin(), v.end());
  insertion_sort(v.begin(), v.begin() + 16);
  for (It i = v.begin() + 16; i != v.end(); ++i) {  // the unguarded rest
    HealthBelow x = *i;
    It j = i;
    for (It k = j - 1; before(x, *k); --k) *j = *k, j = k;
    *j = x;
  }
}

// Mech3.exe 1.2 @ 0x004d24f0: the health (the record's, else the
// animation's), the triggers taken over, the flags.
void desc_take(TargetDesc& d, TargetRec* rec, Handle anim) {
  TargetHooks& h = *g_hooks;
  d.named = rec->name != nullptr;
  d.name = rec->name ? rec->name : "";
  d.destroy_anim = anim;
  if (rec->flags & 1) {
    d.health = d.max_health = rec->health;
  } else {
    d.health = h.anim_health(anim);
    d.max_health = h.anim_max_health(d.destroy_anim);
  }
  d.triggers = rec->health_below;
  rec->health_below = nullptr;
  if (d.triggers) {
    sort_triggers(*d.triggers);
    d.next = 0;
  }
  const uint8_t f = rec->flags;
  d.flags = static_cast<uint8_t>((d.flags & 0xf0) | ((f >> 3) & 1) | ((f >> 1) & 2) | ((f >> 2) & 4) | ((f >> 2) & 8));
}

// Mech3.exe 1.2 @ 0x004d25c0, with the description 0x004d2d80 made first
// (0x004d2480: the defaults 0x005e8bf0).
bool desc_build(TargetDesc& d, Handle node, TargetRec* rec) {
  if (!node || !rec || !rec->destroy_anim) return true;
  const int32_t len = static_cast<int32_t>(std::strlen(rec->destroy_anim));
  const Handle anim = g_hooks->anim_of_node(rec->destroy_anim, len, node);
  if (!anim) return false;
  desc_take(d, rec, anim);
  return true;
}

// --- CZAiTarget ------------------------------------------------------------------------

// Mech3.exe 1.2 @ 0x004d2960 (0x004d3080 a trap's: the same with its own
// vtable), over 0x004d7f80 (AiTarget 0x004d56e0(0, 0) and the list).
CZAiTarget::CZAiTarget(Handle s, Handle n, TargetDesc& d, TargetRec* rec, bool is_trap) : self(s), trap(is_trap) {
  TargetHooks& h = *g_hooks;
  h.entity_init(self);   // zEntity3d 0x005a7e10(0)
  set_team(0);           // 0x004d56e0
  g_list.push_back(this);
  desc = std::move(d);   // 0x004d24b0: the triggers move, the name is copied
  d.triggers = nullptr;
  d.named = false;
  d.name.clear();
  destroyed = 0;
  if (n) attach(rec, n, 0, rec->centroid, rec->target);
  set_team(static_cast<uint32_t>(rec->kind));
  if (rec->height > 0.0f) {
    height = rec->height;
  } else {
    float box[6];
    if (h.node_box(node, box) == 0) height = box[4] - box[1];
  }
}

// Mech3.exe 1.2 @ 0x004d58fc (inside 0x004d58a0, zEntity3d's base end).
void AiTarget::release_shadow() {
  if (!shadow) return;
  zmod::shadow_release_texture(shadow);  // 0x00542e40
  zmod::shadow_delete(shadow);           // 0x00542ca0
  shadow = nullptr;
}

// Mech3.exe 1.2 @ 0x004d2aa0 (0x004d30d0 a trap's first detaches the same
// way), 0x004d81e0, 0x004d58a0.
CZAiTarget::~CZAiTarget() {
  TargetHooks& h = *g_hooks;
  if (node) attach(nullptr, 0, 0, nullptr, nullptr);
  desc_free(desc);
  if (node) set_entity_node(0, 0, nullptr, nullptr);   // 0x004d81e0's 0x004d8250
  if (!g_list_deleting) {                               // 0x004d83c0: the last takes its place
    for (auto it = g_list.begin(); it != g_list.end(); ++it)
      if (*it == this) {
        *it = g_list.back();
        g_list.pop_back();
        break;
      }
  }
  clear_contacts();                                     // 0x004d58a0
  if (node) {
    set_entity_node(0, 0, nullptr, nullptr);
    h.entity_unlist(self);
  }
  net = 0;
  release_shadow();
  h.entity_end(self);
}

// Mech3.exe 1.2 @ 0x004d6c90 (through 0x004d8250): the node-to-entity map
// follows the entity's node.
void CZAiTarget::set_entity_node(Handle n, Handle a, const char* centroid, const char* tgt) {
  TargetHooks& h = *g_hooks;
  if (node) h.node_unbind(node, self);
  node = n;
  h.entity_set_node(self, n, a, centroid, tgt, &target_node, &centroid_node);
  if (n) h.node_bind(n, self);
}

// Mech3.exe 1.2 @ 0x004d27d0 with 0x004d26f0: the node the destroy
// animation lives on (+0x64, else +0x40) and its handlers; true when it has
// none, or when they are the animation's own and the node holds them itself.
bool CZAiTarget::handlers_ok(Handle* table, Handle* owner_node, bool quiet) {
  TargetHooks& h = *g_hooks;
  const Handle anim = desc.destroy_anim;
  const Handle n40 = h.anim_node(anim);
  const Handle n64 = h.anim_node2(anim);
  *owner_node = n64 ? n64 : n40;
  *table = h.node_handlers(*owner_node);
  if (!*table) return true;
  const Handle t = *table;
  const uint32_t weapon = h.handler_word(t, 1);
  if (weapon && weapon != kAnimHit) {
    if (!quiet) h.report(0xbd, "target %s: weapons callback is not zEffAnimApplyDestroyDamage", node);
    return false;
  }
  if (weapon && h.handler_word(t, 0) != anim) {
    if (!quiet) h.report(0xbf, "target %s: weapons callback does not point to animation %s", node, anim);
    return false;
  }
  const uint32_t collide = h.handler_word(t, 3);
  if (collide && collide != kAnimCollide) {
    if (!quiet) h.report(0xc1, "target %s: collide callback is not zEffAnimApplyCollideDamage", node);
    return false;
  }
  if (collide && h.handler_word(t, 2) != anim) {
    if (!quiet) h.report(0xc3, "target %s: collide callback does not point to animation %s", node, anim);
    return false;
  }
  return h.handlers_own(*owner_node) != 0;
}

// Mech3.exe 1.2 @ 0x004d3390 (0x004d35d0 with no record, as the destructors
// detach). The destroy animation's node gets the target's callbacks: new
// ones when it has none, else the animation's taken over.
void CZAiTarget::attach(const TargetRec* rec, Handle n, Handle a, const char* centroid, const char* tgt) {
  TargetHooks& h = *g_hooks;
  if (const Handle old = node) {
    h.anim_owner_release(self);
    if (desc.destroy_anim) {
      Handle o = h.anim_node2(desc.destroy_anim);
      if (!o) o = old;
      if (const Handle t = h.node_handlers(o)) {
        if ((h.handler_word(t, 1) == kOnHit && h.handler_word(t, 0) == self) ||
            (h.handler_word(t, 3) == kOnCollide && h.handler_word(t, 2) == self))
          h.remove_handlers(o);
      }
    }
  }
  set_entity_node(n, a, centroid, tgt);
  if (!n || node != n) return;
  if (rec) {
    if (centroid_node) h.node_offset(centroid_node, rec->centroid_offset);
    if (target_node) h.node_offset(target_node, rec->target_offset);
  }
  if (!desc.destroy_anim) return;
  Handle table = 0, owner = 0;
  if (!handlers_ok(&table, &owner, false)) return;
  h.anim_owner_claim(self, desc.destroy_anim);
  if (!table) {
    if (h.set_weapon_handler(self, owner, kOnHit) != 0) h.set_collide_handler(self, owner, kOnCollide);
  } else {
    h.set_handler_word(table, 1, kOnHit);
    h.set_handler_word(table, 0, self);
    if (h.handler_word(table, 3) != 0) {
      h.set_handler_word(table, 3, kOnCollide);
      h.set_handler_word(table, 2, self);
    }
  }
  if (!rec || !(rec->flags & 2)) return;
  // translate_to_anim_root: the animation's node moved to the root's place
  // (0x004d28e0 says it is elsewhere), its children but the centroid and the
  // target node moved back.
  if (!h.anim_root_moved(desc.destroy_anim)) return;
  const Handle root = h.anim_node(desc.destroy_anim);
  h.node_set_matrix(root);
  h.matrix_invert();
  h.matrix_push();
  const int32_t count = h.node_child_count(root);
  for (int32_t i = 0; i < count; ++i) {
    const Handle child = h.node_child(root, i);
    if (child == centroid_node || child == target_node) continue;
    h.matrix_top();
    h.matrix_mul(h.node_matrix(child));
    h.node_set_matrix(child);
    h.matrix_pop();
  }
  h.matrix_pop();
}

// The walk the damage and the kill share: each threshold the health is now
// below (or at 0, every one left), highest first, sets its trigger.
void CZAiTarget::fire_triggers() {
  if (!desc.triggers) return;
  std::vector<HealthBelow>& v = *desc.triggers;
  while (desc.next != v.size()) {
    const HealthBelow& t = v[desc.next];
    if (desc.health > 0.0f && t.health <= desc.health) break;
    g_hooks->valve_set(t.valve, t.a);
    ++desc.next;  // 0x004d2460
  }
}

// Mech3.exe 1.2 @ 0x004d2c00 (from 0x004d26b0, zWeapon's hit): the triggers,
// destroyed at 0, and the hit told the health left.
uint32_t CZAiTarget::on_hit(Handle, Handle, float damage) {
  TargetHooks& h = *g_hooks;
  desc.health = desc.health - damage;
  fire_triggers();
  if (!(desc.health > 0.0f)) destroy();
  if (!(desc.health > 0.0f)) {
    h.hit_killed();
    return 0;
  }
  h.hit_health(desc.health / desc.max_health);
  return 0;
}

// Mech3.exe 1.2 @ 0x004d2cd0 (from 0x004d26d0, a collision): the health left.
float CZAiTarget::on_collide(float damage) {
  desc.health = desc.health - damage;
  fire_triggers();
  if (!(desc.health > 0.0f)) destroy();
  return desc.health;
}

// Mech3.exe 1.2 @ 0x004d2b30
void CZAiTarget::destroy() {
  if (desc.destroy_anim) g_hooks->anim_run(desc.destroy_anim);
  kill();
}

// Mech3.exe 1.2 @ 0x004d2b60: every trigger left fires, the entity goes
// inactive.
void CZAiTarget::kill() {
  destroyed = 1;
  if (desc.health > 0.0f) {
    desc.health = 0;
    fire_triggers();
  }
  desc.health = 0;
  active = 0;
}

// Mech3.exe 1.2 @ 0x004d7270 with the target's vt+0x7c 0x004d3340 and vt+0x78.
void CZAiTarget::message(int32_t msg) {
  if (msg == 0) destroyed = 0;
  else if (msg == 0x38) kill();
}

// --- Making them (0x004d2d80..0x004d3270) ---------------------------------------------

namespace {
// Mech3.exe 1.2 @ 0x004d2d80 (0x004d3130 a trap's).
CZAiTarget* make_one(Handle node, TargetRec* rec, bool trap) {
  if (!node || !rec) return nullptr;
  TargetDesc d;  // 0x004d2480
  CZAiTarget* t = nullptr;
  if (desc_build(d, node, rec)) {
    void* mem = ::operator new(sizeof(CZAiTarget));  // new(0xf4)
    const Handle self = g_hooks->target_handle(static_cast<CZAiTarget*>(mem));
    t = new (mem) CZAiTarget(self, node, d, rec, trap);
  }
  desc_free(d);
  return t;
}

void make_all(bool traps) {
  TargetHooks& h = *g_hooks;
  h.records_open(traps);
  const int32_t n = h.record_count(traps);
  for (int32_t i = 0; i < n; ++i) {
    const char* name = nullptr;
    TargetRec* rec = h.record(traps, i, &name);
    h.nodes_first(name, traps);
    while (const Handle node = h.nodes_next(traps)) make_one(node, rec, traps);
  }
  h.records_close(traps);
}
}  // namespace

// Mech3.exe 1.2 @ 0x004d2e20
void targets_create() {
  static const char* const kNames[3] = {"hud_target_ally_sm", "hud_target_enemy_sm", "hud_target_other_sm"};
  for (int i = 0; i < 3; ++i) g_markers[i] = g_hooks->marker_create(kNames[i]);
  make_all(false);
}

// Mech3.exe 1.2 @ 0x004d31d0
void traps_create() { make_all(true); }

// Mech3.exe 1.2 @ 0x004d3030
void targets_end() {
  for (Handle m : g_markers)
    if (m) g_hooks->marker_delete(m);
  g_markers[0] = g_markers[1] = g_markers[2] = 0;
}

// Mech3.exe 1.2 @ 0x004d2d70
Handle target_marker(int32_t side) { return side >= 0 && side < 3 ? g_markers[side] : 0; }

// Mech3.exe 1.2 @ 0x004d8360: each deleted (vt+0 with 1) while the list
// keeps them, then emptied.
void targets_delete_all() {
  g_list_deleting = true;
  for (CZAiTarget* t : g_list) delete t;
  g_list_deleting = false;
  g_list.clear();
}

// Mech3.exe 1.2 @ 0x004d5980. The nodes' INTERSECT flags are put back
// after; the query's own answer 0 with a crossing is a blocked line.
bool clear_line(Handle self, Handle other, const float* at) {
  BrainHooks& h = brain_hooks();
  float to[3];
  if (!at) {
    h.node_world_cached(h.entity_centroid(other), to);
    at = to;
  }
  uint8_t za[4], zb[4];
  h.zone_list(self, za);
  h.zone_list(other, zb);
  if (!zmod::zones_overlap(za, zb)) return false;
  const Handle na = h.entity_node(self), nb = h.entity_node(other);
  const bool fa = h.node_intersect(na), fb = h.node_intersect(nb);
  h.node_set_intersect(na, false);
  h.node_set_intersect(nb, false);
  float from[3];
  h.node_world_cached(h.entity_centroid(self), from);
  uint8_t count = 0;
  const int32_t res = h.segment_first(from, at, 0x40000, &count);
  h.node_set_intersect(na, fa);
  h.node_set_intersect(nb, fb);
  return !(res == 0 && count > 0);
}

}  // namespace mech3::ai
