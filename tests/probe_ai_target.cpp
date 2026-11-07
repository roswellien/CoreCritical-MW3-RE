// Probes for test_ai_targets.py (docs/ai.md section 14): AiTarget's picks and
// target keys (mech3/zai_target) and the CZAiTarget objects of targets.zrd
// and traps.zrd. Every call out goes to the shared callback; what the code
// reads of other modules' objects (nodes, animations, handler tables, the
// vehicles' teams) is a memory model the test fills, as the emulator's
// memory on the original's side.
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mech3/zai_target.h"
#include "mech3/zrdraiv.h"
#include "probe_app.h"

namespace {
using mech3::ai::AiTarget;
using mech3::ai::Contact;
using mech3::ai::CZAiTarget;
using mech3::ai::HealthBelow;
using mech3::ai::TargetRec;
using probe_app::call;
using zgame::Handle;

std::map<uint32_t, uint32_t> g_mem;  // dwords by address; bytes are dwords here
uint32_t peek(uint32_t a) {
  auto it = g_mem.find(a);
  return it == g_mem.end() ? 0 : it->second;
}
float peekf(uint32_t a) { return zgame::from_bits(peek(a)); }

// Contacts by the emulator's addresses.
std::vector<std::unique_ptr<Contact>> g_contacts;
std::map<const Contact*, uint32_t> g_contact_addr;
std::map<uint32_t, Contact*> g_contact_of;
uint32_t addr_of(const Contact* c) { return c ? g_contact_addr.at(c) : 0; }

// The records the reader hooks hand out, and their strings.
struct Rec {
  std::unique_ptr<TargetRec> rec;
  std::string key, name, anim, target, centroid;
  bool has_name = false, has_anim = false, has_target = false, has_centroid = false, parsed = true;
};
std::vector<std::unique_ptr<Rec>> g_recs[2];  // kept in place: the records point into their strings
std::vector<uint32_t> g_handles;  // the targets' addresses, in the order made
size_t g_next_handle = 0;

std::vector<uint32_t> g_sets;  // the keys' vt+0xc0 calls, by contact
struct Recording : mech3::ai::TargetHooks {
  void target_set(AiTarget& t, Contact* c) override {
    g_sets.push_back(addr_of(c));
    t.set_target(c, false);
  }
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override { return call(key, args); }
  uint32_t team_of(Handle t) override { return peek(t + 0xc0); }
  void contact_delete(Contact*) override {}
  bool have_pilot() override { return peek(0x60861c) != 0; }
  Handle player_vehicle() override { return peek(peek(0x60861c) + 0x14); }
  void sorted_clear() override { g_mem[0x60b56c] = 0; }
  void records_open(bool traps) override { call(traps ? "0x004deb50/traps" : "0x004deb50", {}); }
  int32_t record_count(bool traps) override {
    return static_cast<int32_t>(call(traps ? "0x004decf0/traps" : "0x004decf0", {}));
  }
  TargetRec* record(bool traps, int32_t i, const char** name) override {
    call("0x004ded20", {i});
    std::vector<std::unique_ptr<Rec>>& v = g_recs[traps ? 1 : 0];
    if (i < 0 || static_cast<size_t>(i) >= v.size()) return nullptr;
    Rec& r = *v[i];
    *name = r.key.c_str();
    call(traps ? "0x004e07e0" : "0x004e08c0", {*name});
    return r.parsed ? r.rec.get() : nullptr;
  }
  void records_close(bool traps) override { call(traps ? "0x004deba0/traps" : "0x004deba0", {}); }
  Handle target_handle(CZAiTarget*) override { return g_next_handle < g_handles.size() ? g_handles[g_next_handle++] : 0; }
  int32_t node_box(Handle node, float box[6]) override {  // 0x004ebd50 as the test's nodes are
    for (int i = 0; i < 6; ++i) box[i] = peekf(node + 0x74 + 4 * i);
    return (peek(node + 0x24) & 0x100) == 0 ? 1 : 0;
  }
  float anim_health(Handle a) override { return a ? peekf(a + 0xb0) : 0.0f; }
  float anim_max_health(Handle a) override { return a ? peekf(a + 0xac) : 0.0f; }
  Handle anim_node(Handle a) override { return a && (peek(a + 0x98) & 0xff) != 5 ? peek(a + 0x40) : 0; }
  Handle anim_node2(Handle a) override { return a && (peek(a + 0x98) & 0xff) != 5 ? peek(a + 0x64) : 0; }
  Handle node_handlers(Handle node) override { return peek(node + 0xbc); }
  uint32_t handler_word(Handle t, int32_t i) override { return peek(t + 4 * i); }
  void set_handler_word(Handle t, int32_t i, uint32_t v) override { g_mem[t + 4 * i] = v; }
  void valve_set(void* valve, int32_t v) override {
    call("0x004d3c00", {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(valve)), static_cast<uint32_t>(v)});
  }
  void entity_set_node(Handle self, Handle node, Handle a, const char* centroid, const char* target,
                       Handle* target_node, Handle* centroid_node) override {
    const uint32_t r = call("0x005a8390", {self, node, a, centroid, target});
    *target_node = node ? r : 0;
    *centroid_node = node && r ? r + 0x100 : 0;
  }
  int32_t node_child_count(Handle node) override { return static_cast<int32_t>(peek(node + 0x5c)); }
  Handle node_child(Handle node, int32_t i) override { return peek(peek(node + 0x60) + 4 * i); }
  // As the test's zWeapon: the node's table (made when it has none) gets the slot.
  int32_t set_weapon_handler(Handle self, Handle node, uint32_t fn) override { return reg(0, self, node, fn); }
  int32_t set_collide_handler(Handle self, Handle node, uint32_t fn) override { return reg(1, self, node, fn); }
  int32_t reg(int slot, Handle self, Handle node, uint32_t fn) {
    const uint32_t v = slot == 0 ? call("0x005833b0", {self, node, 1, fn}) : call("0x005833f0", {self, node, fn});
    if (v) {
      if (!peek(node + 0xbc)) g_mem[node + 0xbc] = peek(node + 0xc0);
      const uint32_t t = peek(node + 0xbc);
      g_mem[t + 8 * slot] = self, g_mem[t + 8 * slot + 4] = fn;
    }
    return static_cast<int32_t>(v);
  }
};
Recording g_hooks;

AiTarget g_target;  // the AiTarget part's test object

// The stream a target's state is written to (the test reads it back).
std::vector<uint32_t> g_out;
void put(uint32_t v) { g_out.push_back(v); }
void putf(float f) { put(zgame::bits(f)); }
void put_str(bool has, const std::string& s) {
  if (!has) return put(0xffffffff);
  put(static_cast<uint32_t>(s.size()));
  for (char c : s) put(static_cast<uint8_t>(c));
}
void put_target(const CZAiTarget& t) {
  put(t.self), put(t.trap), put(t.node), put(t.active), put(t.target_node), put(t.centroid_node);
  putf(t.height), put(t.team), put(t.allies), put(t.enemies);
  putf(t.sensor_range), putf(t.range), putf(t.range2);
  putf(t.desc.max_health), putf(t.desc.health), put(t.desc.destroy_anim), put(t.desc.x0c), put(t.desc.flags);
  put_str(t.desc.named, t.desc.name);
  if (!t.desc.triggers) {
    put(0xffffffff);
  } else {
    put(static_cast<uint32_t>(t.desc.triggers->size())), put(static_cast<uint32_t>(t.desc.next));
    for (const HealthBelow& h : *t.desc.triggers)
      put(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h.valve))), put(static_cast<uint32_t>(h.a)), putf(h.health);
  }
  put(t.destroyed);
}

}  // namespace

extern "C" {

void probe_targets_reset() {
  g_mem.clear();
  g_contacts.clear();
  g_contact_addr.clear();
  g_contact_of.clear();
  g_recs[0].clear(), g_recs[1].clear();
  g_handles.clear();
  g_next_handle = 0;
  g_target = AiTarget{};
  mech3::ai::set_target_hooks(&g_hooks);
  {
    probe_app::Mute m;  // what the last case left
    mech3::ai::targets_end();
    mech3::ai::targets_delete_all();
  }
}
void probe_targets_poke(uint32_t a, uint32_t v) { g_mem[a] = v; }
uint32_t probe_targets_peek(uint32_t a) { return peek(a); }

// --- AiTarget -----------------------------------------------------------------------
// fields: target, ally, enemy, saved (addresses or 0), team, allies, enemies.
void probe_aitarget_set(const uint32_t* contacts, int32_t n, const uint32_t* fields) {
  g_sets.clear();
  g_target = AiTarget{};
  for (int32_t i = 0; i < n; ++i) {
    const uint32_t* c = contacts + 5 * i;  // address, owner, seen, entity, flags
    auto p = std::make_unique<Contact>();
    p->owner = c[1], p->seen = c[2], p->entity = c[3], p->flags = static_cast<uint8_t>(c[4]);
    g_contact_addr[p.get()] = c[0];
    g_contact_of[c[0]] = p.get();
    g_target.contacts.push_back(p.get());
    g_contacts.push_back(std::move(p));
  }
  auto of = [](uint32_t a) -> Contact* {
    if (!a) return nullptr;
    auto it = g_contact_of.find(a);
    if (it != g_contact_of.end()) return it->second;
    auto p = std::make_unique<Contact>();  // one not in the list
    p->owner = peek(a), p->seen = peek(a + 4), p->entity = peek(a + 8), p->flags = static_cast<uint8_t>(peek(a + 0x2c));
    g_contact_addr[p.get()] = a;
    g_contact_of[a] = p.get();
    Contact* r = p.get();
    g_contacts.push_back(std::move(p));
    return r;
  };
  g_target.target = of(fields[0]), g_target.ally_target = of(fields[1]);
  g_target.enemy_target = of(fields[2]), g_target.saved_target = of(fields[3]);
  g_target.team = fields[4], g_target.allies = fields[5], g_target.enemies = fields[6];
}
// fn: 0 first_of, 1..6 the keys (prev ally, prev enemy, next ally, next enemy,
// nearest ally, nearest enemy), 7 set_target(arg contact, keep), 8
// set_sensor_range(arg float bits), 9 set_range, 10 save_target, 11
// saved_or_ally, 12 on_side.
uint32_t probe_aitarget_call(int32_t fn, int32_t side, uint32_t arg, uint32_t self) {
  AiTarget& t = g_target;
  switch (fn) {
    case 0: return addr_of(t.first_of(side));
    case 1: t.prev_ally(side); break;
    case 2: t.prev_enemy(side); break;
    case 3: t.next_ally(side); break;
    case 4: t.next_enemy(side); break;
    case 5: t.nearest_ally(side); break;
    case 6: t.nearest_enemy(side); break;
    case 7: return t.set_target(arg ? g_contact_of.at(arg) : nullptr, side != 0) ? 1 : 0;
    case 8: t.set_sensor_range(self, zgame::from_bits(arg)); break;
    case 9: t.set_range(zgame::from_bits(arg)); break;
    case 10: t.save_target(); break;
    case 11: return addr_of(t.saved_or_ally());
    case 12: return t.on_side(side, arg) ? 1 : 0;
    default: break;
  }
  return 0;
}
// The keys' vt+0xc0 calls of the last case.
int32_t probe_aitarget_sets(uint32_t* out, int32_t max) {
  const int32_t n = static_cast<int32_t>(g_sets.size());
  for (int32_t i = 0; i < n && i < max; ++i) out[i] = g_sets[static_cast<size_t>(i)];
  return n;
}
// target, ally, enemy, saved, sensor range, range, range squared.
void probe_aitarget_state(uint32_t* out) {
  const AiTarget& t = g_target;
  out[0] = addr_of(t.target), out[1] = addr_of(t.ally_target), out[2] = addr_of(t.enemy_target);
  out[3] = addr_of(t.saved_target);
  out[4] = zgame::bits(t.sensor_range), out[5] = zgame::bits(t.range), out[6] = zgame::bits(t.range2);
}

// --- CZAiTarget -------------------------------------------------------------------------
// One record: words kind, health, flags, height, 3 target offsets, 3 centroid
// offsets, parsed, then the strings (key, name, destroy_anim, target,
// centroid: null when has[] says so), then the health_below entries
// (valve handle, a, health) or none (n < 0).
void probe_targets_add_record(int32_t traps, const uint32_t* w, const char* const* s, const int32_t* has,
                              const uint32_t* hb, int32_t n) {
  Rec r;
  r.rec = std::make_unique<TargetRec>();
  TargetRec& t = *r.rec;
  t.kind = static_cast<int32_t>(w[0]);
  t.health = zgame::from_bits(w[1]);
  t.flags = static_cast<uint8_t>(w[2]);
  t.height = zgame::from_bits(w[3]);
  for (int i = 0; i < 3; ++i) t.target_offset[i] = zgame::from_bits(w[4 + i]), t.centroid_offset[i] = zgame::from_bits(w[7 + i]);
  r.parsed = w[10] != 0;
  r.key = s[0];
  r.has_name = has[1], r.has_anim = has[2], r.has_target = has[3], r.has_centroid = has[4];
  r.name = has[1] ? s[1] : "", r.anim = has[2] ? s[2] : "", r.target = has[3] ? s[3] : "", r.centroid = has[4] ? s[4] : "";
  if (n >= 0) {
    t.health_below = new std::vector<HealthBelow>;
    for (int32_t i = 0; i < n; ++i) {
      HealthBelow h;
      h.valve = reinterpret_cast<mech3::ai::Valve*>(static_cast<uintptr_t>(hb[3 * i]));
      h.a = static_cast<int32_t>(hb[3 * i + 1]);
      h.health = zgame::from_bits(hb[3 * i + 2]);
      t.health_below->push_back(h);
    }
  }
  g_recs[traps ? 1 : 0].push_back(std::make_unique<Rec>(std::move(r)));
  Rec& b = *g_recs[traps ? 1 : 0].back();
  b.rec->name = b.has_name ? b.name.c_str() : nullptr;
  b.rec->destroy_anim = b.has_anim ? b.anim.c_str() : nullptr;
  b.rec->target = b.has_target ? b.target.c_str() : nullptr;
  b.rec->centroid = b.has_centroid ? b.centroid.c_str() : nullptr;
}
void probe_targets_handles(const uint32_t* h, int32_t n) { g_handles.assign(h, h + n); }
void probe_targets_create(int32_t traps) { traps ? mech3::ai::traps_create() : mech3::ai::targets_create(); }
// op 0 a hit (def, hit, damage bits), 1 a collision (damage), 2 destroy
// (vt+0x70), 3 every target deleted, 4 the markers deleted.
uint32_t probe_targets_op(int32_t op, uint32_t self, uint32_t a, uint32_t b, uint32_t c) {
  CZAiTarget* t = nullptr;
  for (CZAiTarget* x : mech3::ai::target_list())
    if (x->self == self) t = x;
  switch (op) {
    case 0: return t ? t->on_hit(a, b, zgame::from_bits(c)) : 0;
    case 1: return t ? zgame::bits(t->on_collide(zgame::from_bits(a))) : 0;
    case 2: if (t) t->destroy(); break;
    case 3: mech3::ai::targets_delete_all(); break;
    case 4: mech3::ai::targets_end(); break;
    default: break;
  }
  return 0;
}
// The list in order, each target's state; returns the words written.
int32_t probe_targets_state(uint32_t* out, int32_t cap) {
  g_out.clear();
  put(static_cast<uint32_t>(mech3::ai::target_list().size()));
  for (const CZAiTarget* t : mech3::ai::target_list()) put_target(*t);
  for (int i = 0; i < 3; ++i) put(mech3::ai::target_marker(i));
  const int32_t n = static_cast<int32_t>(g_out.size());
  for (int32_t i = 0; i < n && i < cap; ++i) out[i] = g_out[i];
  return n;
}

}  // extern "C"
