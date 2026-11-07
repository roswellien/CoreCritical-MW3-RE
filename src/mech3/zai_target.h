#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gamez/zgame/unported.h"

// What a vehicle aims at and what can be aimed at (docs/ai.md section 14):
// AiTarget, the targetable part of every vehicle (0x004d56e0, vtable
// 0x005cced8; the Mech's +0x68..+0xd0), its contact list and the targets it
// picks (0x004d5e40..0x004d6d20); C:\zipper\Mech3\ZAiTarget.cpp's
// CZAiTarget, a scored object of targets.zrd (0x004d2960) with its damage,
// its health_below triggers and its destruction. AiTarget's own file is not
// known; it sits after ZAiTarget.cpp, before ZRdrAiNet.cpp.
namespace zmod {
struct ShadowCaster;
}

namespace mech3::ai {

using zgame::Handle;
struct TargetRec;
struct HealthBelow;

// One thing a vehicle's sensors see (0x30 bytes, made by 0x004aa270 in the
// sensors' module between turret.cpp and veh_basic.cpp, not ported). The
// vehicle owns them (0x004a7be0 deletes them).
struct Contact {
  Handle owner = 0;        // +0x00  the vehicle that sees it
  Handle seen = 0;         // +0x04  what is seen: its vehicle is vt+0x2c (0x004e6fe0)
  Handle entity = 0;       // +0x08
  float dist2 = 0;         // +0x0c  squared distance: the list is nearest first
  float delta[3] = {};     // +0x10
  float dist = -1.0f;      // +0x1c  the distance, -1 until distance() works it out
  float dir[3] = {};       // +0x20  the unit delta, with it
  uint8_t flags = 0;       // +0x2c  bit 0: in sight now (likely)

  float distance();        // 0x004e7030
};

// The sides a pick asks for: 0 an ally, 1 an enemy, 2 neither, 3 any.
enum Side : int32_t { kAlly = 0, kEnemy = 1, kNeutral = 2, kAny = 3 };

// The fields 0x004d56e0 sets up, at their offsets in the vehicle.
struct AiTarget {
  float height = 10.0f;              // +0x68  (the Mech's from its model's box)
  Contact* target = nullptr;         // +0x6c  the target selected
  Contact* ally_target = nullptr;    // +0x70  the last ally selected
  Contact* enemy_target = nullptr;   // +0x74  the last enemy selected
  Contact* saved_target = nullptr;   // +0x78  0x004d7220
  // The sensors' (mech3/sensors, docs/ai.md section 15): the contact on the
  // nearest of the player and his lancemates under orders (+0x80), kept at
  // +0x7c when it is in no list; the one on the player (+0x98), kept at +0x94.
  Contact* own_lance = nullptr;      // +0x7c  owned (0x004a7be0 deletes it)
  Contact* nearest_lance = nullptr;  // +0x80
  uint8_t x84 = 0;                   // +0x84  a vector's allocator byte (the constructor's first argument)
  // +0x88: a vector of contacts kept track of: each frame the sensors put
  // the new contact on the same vehicle in its place. The constructor's two
  // are null and nothing fills them (end, capacity +0x8c, +0x90).
  Contact* tracked[2] = {};
  Contact* own_player = nullptr;     // +0x94  owned (0x004a7be0 deletes it)
  Contact* player_contact = nullptr; // +0x98
  float sensor_range = 1000.0f;      // +0x9c  0x005e8d84; the launch sets it (0x004d6d20)
  float range = 1000.0f;             // +0xa0  capped at 1000; the player's 50000 (0x005e8d8c) in IA and multiplayer
  float range2 = 1.0e6f;             // +0xa4  its square
  uint8_t xa8 = 0;                   // +0xa8  the contacts' allocator byte (the first argument)
  std::vector<Contact*> contacts;    // +0xac..+0xb4  nearest first
  Contact* touching = nullptr;       // +0xb8  the nearest contact when within 8 m (0x004a9e70)
  // +0xbc: bit 0 someone's target, bit 1 seen by an enemy, bit 2 passive
  // radar (the mech's radar mode key), bit 3 the player has seen it (never
  // cleared), bit 4 a lancemate the player commands (0x004d5be0).
  uint8_t xbc = 0;
  uint32_t team = 1;                 // +0xc0  one bit per team (team 0: bit 0)
  uint32_t allies = 0;               // +0xc4  the same bits but bit 0
  uint32_t enemies = 0xfffffffe;     // +0xc8  every other bit but bit 0
  zmod::ShadowCaster* shadow = nullptr;  // +0xcc  its projected shadow (0x004814e0 keeps it)
  class VehicleNet* net = nullptr;   // +0xd0  a net of its own, made on first ask (0x004d70f0; znet owns it)

  // The masks from a 5-bit team in each 5 bits (0x004d56e0, 0x004d2960):
  // team 0's bit 0 is no one's ally or enemy. [0x0060b56c] is cleared.
  void set_team(uint32_t t);
  uint32_t team_number() const;                     // 0x004d71a0: the masks back to 5-bit teams
  bool is_ally(Handle vehicle) const;               // 0x004d6130
  bool is_enemy(Handle vehicle) const;              // 0x004d6160
  bool on_side(int32_t side, Handle vehicle) const; // 0x004d6190
  bool set_target(Contact* c, bool keep_sides);     // 0x004d5e40 (vt+0xc0)
  Contact* first_of(int32_t side) const;            // 0x004d6b90: the nearest, in sight or not
  // The player's target keys (0x0041c28f..0x0041c2d9), each called with
  // side 0: the enemy keys take what is not of that side.
  void prev_ally(int32_t side);      // 0x004d5ee0  Shift+W
  void prev_enemy(int32_t side);     // 0x004d6260  Shift+E
  void next_ally(int32_t side);      // 0x004d64b0  W
  void next_enemy(int32_t side);     // 0x004d6710  E
  void nearest_ally(int32_t side);   // 0x004d6970  Ctrl+W
  void nearest_enemy(int32_t side);  // 0x004d6a80  Ctrl+E
  void set_range(float r);           // 0x004d6d00
  void set_sensor_range(Handle self, float r);       // 0x004d6d20
  void save_target() { saved_target = target; }      // 0x004d7220
  Contact* saved_or_ally() const { return saved_target ? saved_target : ally_target; }  // 0x004d7240
  void clear_contacts();             // 0x004a7be0: the contacts deleted (and +0x7c, +0x94)
  void release_shadow();             // 0x004d58fc: the caster's texture and the caster freed

 private:
  Handle vehicle_of(const Contact* c) const;         // 0x004d6fe0's 0x004e6fe0
  uint32_t team_of(Handle vehicle) const;            // vt+0x28's +0xc0
  bool same(const Contact* a, const Contact* b) const { return a->entity == b->entity && a->owner == b->owner; }
};

// --- CZAiTarget (ZAiTarget.cpp) --------------------------------------------------

// What 0x004d24f0 takes from the record (0x1c bytes; defaults 0x005e8bf0).
struct TargetDesc {
  float max_health = 100.0f;     // +0x00
  float health = 100.0f;         // +0x04
  Handle destroy_anim = 0;       // +0x08  the record's destroy_anim among the node's (0x0050b4e0)
  uint32_t x0c = 1;              // +0x0c
  // +0x10: health_below's list (0x14 bytes, 0x004d1f60), taken from the
  // record, highest first after the sort 0x004d21f0, with the next one due.
  std::vector<HealthBelow>* triggers = nullptr;
  size_t next = 0;
  // +0x14: bit 0 display, bit 1 radar, bit 2 reticule_select, bit 3
  // zoom_select (the record's 0x08, 0x04, 0x10, 0x20).
  uint8_t flags = 0;
  bool named = false;            // +0x18  the record's (localized) name, strdup'd (0x0049d1c0)
  std::string name;
};

// A targets.zrd object (0xf4 bytes: AiTarget's 0xd4 by 0x004d7f80, then
// the description and +0xf0). Its zEntity3d part is the port's few fields.
class CZAiTarget : public AiTarget {
 public:
  Handle self = 0;               // its handle as the hooks see it
  bool trap = false;             // vtable 0x005ccc80 (0x004d3080), else 0x005ccb90
  Handle node = 0;               // +0x04  zEntity3d's node
  uint8_t active = 1;            // +0x08
  uint8_t sensed = 1;            // +0x09  (ZEntity) cleared and set by the sensors
  Handle target_node = 0;        // +0x10  the node a weapon aims at (the record's "target")
  Handle centroid_node = 0;      // +0x14  the HUD's (the record's "centroid")
  TargetDesc desc;               // +0xd4
  uint8_t destroyed = 0;         // +0xf0

  CZAiTarget(Handle self, Handle node, TargetDesc& d, TargetRec* rec, bool trap);  // 0x004d2960, 0x004d3080
  ~CZAiTarget();                 // 0x004d2aa0 (0x004d30d0 a trap's), 0x004d81e0, 0x004d58a0
  CZAiTarget(const CZAiTarget&) = delete;
  CZAiTarget& operator=(const CZAiTarget&) = delete;

  void attach(const TargetRec* rec, Handle node, Handle a, const char* centroid, const char* target);  // 0x004d3390
  uint32_t on_hit(Handle def, Handle hit, float damage);  // 0x004d26b0 -> 0x004d2c00
  float on_collide(float damage);                         // 0x004d26d0 -> 0x004d2cd0
  void destroy();                // 0x004d2b30, vt+0x70: the destroy animation, then kill
  void kill();                   // 0x004d2b60, vt+0x78
  // 0x004d7270 (vt+0x74, the AI messages): 0 vt+0x7c 0x004d3340 (+0xf0
  // cleared), 0x38 vt+0x78 the kill; the rest nothing.
  void message(int32_t msg);
  float health_fraction() const { return desc.health / desc.max_health; }  // 0x004d2b20
  bool shown() const { return (desc.flags & 1) != 0; }                  // 0x004d3350's complement

 private:
  void fire_triggers();          // the walk 0x004d2c00, 0x004d2cd0 and 0x004d2b60 share
  bool handlers_ok(Handle* table, Handle* owner_node, bool quiet);       // 0x004d27d0 with 0x004d26f0
  void set_entity_node(Handle node, Handle a, const char* centroid, const char* target);  // 0x004d6c90
};

// The objects 0x004d7f80 keeps ([0x0060b584..0x0060b588]): the targets
// and traps, in the order made; 0x004d83c0 takes one out by its last.
std::vector<CZAiTarget*>& target_list();
void targets_delete_all();       // 0x004d8360 on 0x0060b580 (the cleanup's 0x004d8270)

void desc_free(TargetDesc& d);   // 0x004d2660
// 0x004d25c0 on a description just made (0x004d2480): false when the
// record's destroy_anim is not among the node's animations; true with the
// defaults untouched when there is no record, node or destroy_anim.
bool desc_build(TargetDesc& d, Handle node, TargetRec* rec);
void desc_take(TargetDesc& d, TargetRec* rec, Handle anim);   // 0x004d24f0
// 0x004d21f0: highest health first, the first due next. Up to 16 by
// insertion, more by MSVC's quicksort.
void sort_triggers(std::vector<HealthBelow>& v);

// 0x004d2e20: as the mission starts ("Post Vehicle Fixup"), the HUD's three
// target markers, then a CZAiTarget for each node named as a targets.zrd
// record (0x004d2d80); 0x004d31d0 the same for traps.zrd by name prefix
// (0x004d3130; none ship).
void targets_create();
void traps_create();
void targets_end();                  // 0x004d3030: the markers deleted
Handle target_marker(int32_t side);  // 0x004d2d70: [0x0060b4f0 + 4 side], ally, enemy, other

// The calls out, by the original's address.
class TargetHooks : public zgame::Unported {
 public:
  // The vehicles a contact leads to, and their teams.
  virtual Handle seen_vehicle(Handle seen) { return unported("seen.vt+0x2c", {seen}); }
  virtual Handle ai_target(Handle vehicle) { return unported("veh.vt+0x28", {vehicle}); }
  virtual uint32_t team_of(Handle target) { (void)target; return 0; }  // +0xc0, a field
  virtual void contact_delete(Contact* c) { delete c; }
  // The target keys' vt+0xc0(c, 0): a mech's 0x0041d890 tells the HUD too.
  virtual void target_set(AiTarget& t, Contact* c) { t.set_target(c, false); }
  // 0x004d6d20.
  virtual Handle self_vehicle(Handle self) { return unported("self.vt+0x50", {self}); }
  virtual Handle self_x58(Handle self) { return unported("self.vt+0x58", {self}); }
  virtual bool have_pilot() { return unported("[0x0060861c]", {}) != 0; }
  virtual Handle player_vehicle() { return unported("[0x0060861c]+0x14", {}); }
  virtual int32_t game_type() { return static_cast<int32_t>(unported("game.vt+0x2c", {})); }
  virtual void sorted_clear() {}  // [0x0060b56c] = 0
  // The records (mech3/zrdraiv's targets and traps files).
  virtual void records_open(bool traps) { unported(traps ? "0x004deb50/traps" : "0x004deb50", {}); }
  virtual int32_t record_count(bool traps) { return static_cast<int32_t>(unported(traps ? "0x004decf0/traps" : "0x004decf0", {})); }
  virtual TargetRec* record(bool traps, int32_t i, const char** name) {
    (void)traps, (void)i, (void)name;
    return nullptr;
  }
  virtual void records_close(bool traps) { unported(traps ? "0x004deba0/traps" : "0x004deba0", {}); }
  // The HUD's markers (zTwoD images 0x0059b830 with the vtable 0x005bce78, +0x48 1).
  virtual Handle marker_create(const char* name) { return unported("0x0059b830", {name, 0, 0, 1}); }
  virtual void marker_delete(Handle m) { unported("marker.vt+0x0", {m, 1}); }
  // A new target's handle (operator new 0xf4).
  virtual Handle target_handle(CZAiTarget* t) { (void)t; return 0; }
  // zClass's walk of list 6 by name (0x004f8c30) and by prefix (0x004f8b80).
  virtual Handle nodes_first(const char* name, bool prefix) {
    return unported(prefix ? "0x004f8b80" : "0x004f8c30", {name, 6});
  }
  virtual Handle nodes_next(bool prefix) { return unported(prefix ? "0x004f8b80" : "0x004f8c30", {0u, 6}); }
  virtual int32_t node_box(Handle node, float box[6]) {  // 0x004ebd50: 0 when it has one
    (void)box;
    return static_cast<int32_t>(unported("0x004ebd50", {node}));
  }
  // zEffect's animations.
  virtual Handle anim_of_node(const char* name, int32_t len, Handle node) {
    return unported("0x0050b4e0", {name, len, node});
  }
  virtual float anim_health(Handle anim) { (void)anim; return 0; }      // 0x00502db0: +0xb0
  virtual float anim_max_health(Handle anim) { (void)anim; return 0; }  // 0x00502dd0: +0xac
  virtual Handle anim_node(Handle anim) { (void)anim; return 0; }       // 0x0050b5e0: +0x40 unless free
  virtual Handle anim_node2(Handle anim) { (void)anim; return 0; }      // 0x0050b600: +0x64 unless free
  virtual void anim_run(Handle anim) { unported("0x00508c10", {anim, 0, 0, 0, 0}); }
  virtual void anim_owner_release(Handle self) { unported("0x004d5100", {self}); }
  virtual uint8_t anim_owner_claim(Handle self, Handle anim) {
    return static_cast<uint8_t>(unported("0x004d4f20", {self, anim}));
  }
  // zWeapon's handlers on a node (+0xbc: weapon owner, weapon callback,
  // collide owner, collide callback).
  virtual Handle node_handlers(Handle node) { (void)node; return 0; }
  virtual uint32_t handler_word(Handle table, int32_t i) { (void)table, (void)i; return 0; }
  virtual void set_handler_word(Handle table, int32_t i, uint32_t v) { (void)table, (void)i, (void)v; }
  virtual uint32_t handlers_own(Handle node) { return unported("0x00583250", {node}); }
  virtual int32_t set_weapon_handler(Handle self, Handle node, uint32_t fn) {
    return static_cast<int32_t>(unported("0x005833b0", {self, node, 1, fn}));
  }
  virtual int32_t set_collide_handler(Handle self, Handle node, uint32_t fn) {
    return static_cast<int32_t>(unported("0x005833f0", {self, node, fn}));
  }
  virtual void remove_handlers(Handle node) { unported("0x00583400", {node}); }
  virtual void hit_killed() { unported("0x00583710", {1, 0}); }
  virtual void hit_health(float fraction) { unported("0x00583730", {zgame::bits(fraction)}); }
  virtual void valve_set(void* valve, int32_t v) { unported("0x004d3c00", {0u, static_cast<uint32_t>(v)}); (void)valve; }
  // zEntity3d (0x005a7e10..0x005a8390) and the node-to-entity map (0x0040eaa0, 0x0040ed00).
  virtual void entity_init(Handle self) { unported("0x005a7e10", {self, 0}); }
  virtual void entity_end(Handle self) { unported("0x005a7f90", {self}); }
  virtual void entity_unlist(Handle self) { unported("0x005a7f70", {self}); }
  virtual void node_unbind(Handle node, Handle self) { unported("0x0040ed00", {node, self}); }
  virtual void node_bind(Handle node, Handle self) { unported("0x0040eaa0", {node, self}); }
  // Sets the entity's node; gives the subnodes named `target` and `centroid`.
  virtual void entity_set_node(Handle self, Handle node, Handle a, const char* centroid, const char* target,
                               Handle* target_node, Handle* centroid_node) {
    unported("0x005a8390", {self, node, a, centroid, target});
    *target_node = *centroid_node = 0;
  }
  virtual void node_offset(Handle node, const float v[3]) {
    unported("0x004fc1b0", {node, zgame::bits(v[0]), zgame::bits(v[1]), zgame::bits(v[2])});
  }
  // translate_to_anim_root (0x004d28e0 and the zModel matrix calls after it).
  virtual uint8_t anim_root_moved(Handle anim) { return static_cast<uint8_t>(unported("0x004d28e0", {anim})); }
  virtual void node_set_matrix(Handle node) { unported("0x004fc300", {node}); }
  virtual void matrix_invert() { unported("0x00523c50", {1}); }
  virtual void matrix_push() { unported("0x0051e9d0", {}); }
  virtual void matrix_top() { unported("0x0051e930", {}); }
  virtual Handle node_matrix(Handle node) { return unported("0x004fc3f0", {node}); }
  virtual void matrix_mul(Handle m) { unported("0x0051fee0", {m, 1}); }
  virtual void matrix_pop() { unported("0x0051ea00", {}); }
  virtual int32_t node_child_count(Handle node) { (void)node; return 0; }  // +0x5c
  virtual Handle node_child(Handle node, int32_t i) { (void)node, (void)i; return 0; }  // +0x60
  // 0x0049bfd0 (0x200): `fmt` with the node's name, and the animation's
  // (0x0050b620, empty in this build: the exe passes what EAX holds).
  virtual void report(int32_t line, const char* fmt, Handle node, Handle anim = 0) {
    (void)anim;
    unported("0x0049bfd0", {0x200, line, fmt, node});
  }
};
// Mech3.exe 1.2 @ 0x004d5980: whether nothing stands between vehicle
// `self`'s centroid and `at` (null: `other`'s centroid). Not when their zones
// do not meet (0x0053cc90); else the world's segment query, both vehicles'
// nodes left out of it, stopping at the first crossing, past nodes with flag
// 0x40000 (docs/ai.md 17.7).
bool clear_line(Handle self, Handle other, const float* at);

void set_target_hooks(TargetHooks* h);
TargetHooks& target_hooks();

}  // namespace mech3::ai
