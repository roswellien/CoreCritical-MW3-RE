#pragma once
#include <cstdint>

#include "gamez/zweapon/zwep_init.h"

// zWeapon's run time: the code from 0x00581230 to zwep_init.c (0x00583880)
// and after it to 0x00586b70 (docs/weapons.md, "zWeapon run time"). The file
// names are not in the exe; this is named after the module: zweapon.cpp the
// hits, zwep_shot.cpp the projectiles, zwep_beam.cpp the beams.
namespace zwep {

// A node's damage and touch handlers, 0x10 bytes at node +0xbc (1 there
// means a shootable shot instead), shared by the node's subtree: 0x00583270
// hands a new table down to every child that had the old one.
struct NodeHandlers {
  struct Slot {
    Handle object = nullptr;   // ECX of the call
    void (*fn)() = nullptr;    // slot 0 DamageFn, slot 1 TouchFn
  } slot[2];                   // 0 damage (0x005833b0), 1 touch (0x005833f0)
};
using DamageFn = uint32_t (*)(Handle object, WeaponDef* def, HitRecord* hit, float damage);
// EDX is left holding the hit's point.z when the shot line is recorded, else
// the record (0x005836b0): not an argument.
using TouchFn = float (*)(Handle object, float damage);  // the health left (0x004d26d0, 0x00502df0)

// What a crater or a quicksand pit is made from (the crater module's
// 0x004ffaa0 and 0x00502110 take them): the module's defaults (0x004ff920
// copies 0x0060bfc0's ten dwords; 0x005020f0 its own), then the weapon's
// numbers. Craters: 0x28 bytes; quicksand: 0x2c.
struct CraterOrder {
  uint32_t x00 = 0;
  int32_t points = 0;          // +0x04
  Handle material = nullptr;   // +0x08  the surface's
  uint32_t x0c = 0;
  float depth = 0;             // +0x10
  float radius = 0;            // +0x14
  Vec3f3 at{};                 // +0x18
  Handle owner = nullptr;      // +0x24  the shooter's node
};
struct QuicksandOrder {
  uint32_t x00 = 0;
  int32_t points = 0;          // +0x04
  Handle cycle = nullptr;      // +0x08  the pit's animated material (null: zDEClient's)
  Handle still = nullptr;      // +0x0c  its still frame
  uint32_t x10 = 0;            // +0x10  declient.zrd's slope (float bits), not read
  float depth = 0;             // +0x14
  float radius = 0;            // +0x18
  Vec3f3 at{};                 // +0x1c
  Handle owner = nullptr;      // +0x28
};

// What the game half hands the next shot or beam (globals 0x009a0ee0..,
// 0x009a17cc..; reset to these values by the startup 0x00583750 and by every
// spawn, docs/weapons.md "Fire modifiers").
struct FireState {
  int32_t* lock_flag = nullptr;        // 0x009a0ee0  the firing weapon's lock flag
  LockRef* lock_target = nullptr;      // 0x009a0ee4  and its target (MULTI_TARGET beams: an array they sort)
  int32_t beam_ground_fx = 1;          // 0x009a17cc  0: the beam plays no ground impact
  float damage_scale = 1;              // 0x009a17d0
  float beam_range_scale = 1;          // 0x009a17d4  0.5 under water
  float track_scale[2] = {1, 1};       // 0x009a17d8, 0x009a17dc  FCS: 2, 2; [0] the homing's (spawn has reset it: 1)
  int32_t terrain_follow = 0;          // 0x009a17e0  FCS: 1
};
FireState& fire_state();

// What a hit leaves for the damage code (globals 0x009a0ec4.., 0x009a1788..).
struct HitState {
  Vec3f3 shot_from{}, shot_to{};       // 0x009a0ec4, 0x009a0ed0  the shot's line (Mech::TakeDamage reads it)
  Handle attacker = nullptr;           // 0x009a0edc  the shooter's node
  int32_t record_shot = 1;             // 0x009a0eec  0 while a hit's own impact and splash run
  int32_t splash_enabled = 1;          // 0x009a0ee8  air bursts splash only with it
  int32_t beam_ground_fx = 1;          // 0x009a0ef0  the beam's copy of FireState::beam_ground_fx
  Handle killed = nullptr;             // 0x009a1788  set by the handler (0x00583710): the hit killed
  HitRecord* killed_at = nullptr;      // 0x009a1790  and where, for the craters
  float health_after = 1;              // 0x009a178c  set by the handler (0x00583730)
  void (*pre_hit)(Handle handler, Handle node, float damage, WeaponDef* def) = nullptr;  // 0x009a17a4
  int32_t hits = 0;                    // 0x009a17c0  hits by `counted` (network "WeaponData")
  Handle counted = nullptr;            // 0x009a17c4
  float dt = 0;                        // 0x0072c52c  the frame's, set by the game loop
  float tether_ceiling = 30;           // 0x009a17a0  a tethered shot's height limit
};
HitState& hit_state();

// One drawn piece of a beam, 0x20 bytes: a node scaled along z to its length.
struct BeamSegment {
  Handle node = nullptr;               // +0x00  "BeamReflect_%d", the FLYOUT model under it
  Vec3f3 start{};                      // +0x04  MULTI_TARGET: its start; else the ray's end
  Vec3f3 dir{};                        // +0x10  MULTI_TARGET only
  float length = 0;                    // +0x1c
};

// A beam in the air, 0x150 bytes (0x00584a70), on its def's list (+0x7c)
// while it fires.
struct Beam {
  Beam* next = nullptr;                // +0x00
  Beam* prev = nullptr;                // +0x04
  WeaponDef* def = nullptr;            // +0x08
  Handle owner = nullptr;              // +0x0c  the shooter's node, out of its own rays
  const uint32_t* zone = nullptr;      // +0x10  the shooter's zone set, or none
  Handle sound = nullptr;              // +0x14  the FLYOUT sound, looping while it fires
  uint32_t x18 = 0;                    // +0x18
  Handle light = nullptr;              // +0x1c  DESIGNATE: a light from the pool on the spot
  const Vec3f3* origin = nullptr;      // +0x20  the weapon's muzzle, followed each frame
  const Vec3f3* dir = nullptr;         // +0x24  and its direction
  float length = 0;                    // +0x28  grows toward RANGE x range scale
  float timer = 0;                     // +0x2c  DESIGNATE: time on a target
  float age = 0;                       // +0x30
  float damage_scale = 1;              // +0x34  FireState::damage_scale at the start
  float range_scale = 1;               // +0x38  FireState::beam_range_scale (0.5 under water)
  int32_t segment_count = 0;           // +0x3c  made: up to 8, 1 for DESIGNATE
  int32_t active = 0;                  // +0x40  shown this frame
  BeamSegment segments[8];             // +0x44
  const int32_t* lock_count = nullptr;     // +0x144  LOCK_ON: the weapon's lock flag, a count for MULTI_TARGET
  LockRef* targets = nullptr;              // +0x148  and its targets (an array for MULTI_TARGET)
  uint32_t flags = 0;                  // +0x14c  bit 0 ground effect, bit 1 damage dealt
};

// The scene, effects and sounds a hit touches; unset ones do nothing.
struct HitHooks {
  void (*surface_mark)(HitRecord* hit) = nullptr;                   // 0x00542060 (not for target-heat weapons)
  uintptr_t (*node_handler)(Handle node) = nullptr;                 // node +0xbc: 0 none, 1 a projectile, else a handler
  Projectile* (*node_projectile)(Handle node) = nullptr;            // node +0x40
  bool (*handler_live)(uintptr_t handler) = nullptr;                // handler +4 (its function) is set
  uint32_t (*call_handler)(uintptr_t handler, WeaponDef* def, HitRecord* hit, float damage) = nullptr;  // handler +4 on +0
  int32_t (*soil)(Handle material) = nullptr;                       // material +0x20
  void (*effect)(Handle anim, const float args[9]) = nullptr;       // 0x00508aa0
  int32_t (*crater)(WeaponDef* def, HitRecord* hit, int32_t soil, Projectile* p) = nullptr;     // 0x005825b0
  int32_t (*quicksand)(WeaponDef* def, HitRecord* hit, int32_t soil, Projectile* p) = nullptr;  // 0x005824b0
  void (*sound)(WeaponDef* def, HitRecord* hit, int32_t soil, float volume) = nullptr;         // 0x00582f60
  void (*impact_effect)(int32_t effect, const Vec3f3* at) = nullptr;                           // 0x0050d650
  void (*surface_angles)(const Vec3f3* normal, float out[3]) = nullptr;  // 0x00525a50 from 0x005eda70, then 0x00525f80
  bool (*node_intersect)(Handle node) = nullptr;                    // node +0x24 bit 4
  void (*set_intersect)(Handle node, int32_t on) = nullptr;         // 0x004eb6d0
  // 0x004f0ac0 on the world: what lies within `radius` of `at`, into `out`;
  // 0 when it found something.
  int32_t (*query)(Handle node, const Vec3f3* at, float radius, HitList* out) = nullptr;
};
HitHooks& hit_hooks();

// Checked soil selection: maps legacy unset (-1/UINT32_MAX) and out-of-range soils
// to kSoilDefault, while preserving valid extended soil IDs within the definition's allocation.
int32_t checked_soil(const WeaponDef* def, int32_t soil);
int32_t soil_of(const WeaponDef* def, Handle material);

float beam_damage(WeaponDef* def, Beam* b);                                        // 0x00583020
uint32_t apply_damage(WeaponDef* def, Handle owner, const Vec3f3* from, HitRecord* hit, float damage,
                      Handle anim);                                                // 0x00583480
uint32_t impact(WeaponDef* def, HitRecord* hit, Projectile* p, Handle anim);        // 0x005826b0
int32_t splash_query(WeaponDef* def, Projectile* p, int32_t flag, HitList* out);   // 0x00582920
int32_t splash(WeaponDef* def, Projectile* p, HitList* list, uintptr_t exclude);   // 0x005829b0
void hit(WeaponDef* def, Projectile* p, HitRecord* rec);                           // 0x00586350
void air_burst(WeaponDef* def, Projectile* p);                                     // 0x00582320
void note_kill(Handle killed, HitRecord* at);                                      // 0x00583710
void step(WeaponDef* def, Projectile* p, Vec3f3* move);                            // 0x00585cc0
int32_t crater(WeaponDef* def, HitRecord* hit, Projectile* p);                       // 0x005825b0
// The node handler slots (0x00583220..0x00583440) and the touch 0x005836b0.
// One table for every node in the game: mechs, animations and targets
// register through set_damage_handler / set_touch_handler.
NodeHandlers* handlers_of(Handle node);  // the node's table (+0xbc), its own or handed down; null without
bool handler_owned(Handle node, uintptr_t h);                                       // 0x00583220
bool own_handler(Handle node);                                                       // 0x00583250
void handler_set_slot(NodeHandlers* t, uintptr_t old, Handle object, Handle node, void (*fn)(), int32_t slot);  // 0x00583270
void handler_hand_down(Handle node, NodeHandlers* t, uintptr_t old);                 // 0x005832a0
int32_t set_handler(Handle object, Handle node, void (*fn)(), int32_t slot);         // 0x005832f0
int32_t set_damage_handler(Handle object, Handle node, DamageFn fn, int32_t flag);   // 0x005833b0 (flag, then fn on the stack)
int32_t set_touch_handler(Handle object, Handle node, TouchFn fn);                   // 0x005833f0
int32_t remove_handlers(Handle node);                                                // 0x00583400
// The rebuild's: the object and function of the damage handler a node has
// (its own or handed down), null when none (the TAG strike's fix).
Handle damage_object(Handle node, DamageFn* fn);
// DESIGNATE's hold reached (in 0x005814b0); FIX (bug) in whole ms.
bool designate_held(float timer, float hold);
void handler_clear(Handle node, uintptr_t h);                                        // 0x00583440
float touch(const Vec3f3* from, HitRecord* hit, float damage);                       // 0x005836b0: the handler's result
void quicksand(WeaponDef* def, HitRecord* hit, int32_t soil, Projectile* p);         // 0x005824b0
inline void set_health_after(float h) { hit_state().health_after = h; }           // 0x00583730

// The engine the projectiles drive (class.c, object3d.c, the animations,
// zSound, cls_di.c's queries); unset ones do nothing and return 0. The
// queries run on the world (State::world).
struct RunHooks {
  // The scene graph: a node's parents (+0x54, +0x58) and children (+0x5c, +0x60).
  int32_t (*parent_count)(Handle node) = nullptr;
  Handle (*first_parent)(Handle node) = nullptr;
  int32_t (*child_count)(Handle node) = nullptr;
  Handle (*first_child)(Handle node) = nullptr;
  void (*add_child)(Handle parent, Handle child) = nullptr;        // 0x004eb9b0
  void (*remove_child)(Handle parent, Handle child) = nullptr;     // 0x004ebb40
  void (*obj_add_child)(Handle node, Handle child) = nullptr;      // 0x004fb7a0
  void (*obj_remove_child)(Handle node, Handle child) = nullptr;   // 0x004fb7f0
  Handle (*clone)(Handle node) = nullptr;                          // 0x004f9af0(node, 0, 1)
  Handle (*link)(Handle node) = nullptr;                           // node +0x40, the model pools' link
  void (*set_link)(Handle node, Handle next) = nullptr;
  // FLYOUT_HEALTH: node +0x24 bit 27, +0xbc = 1, +0x40 = the shot; null clears the bit.
  void (*set_shootable)(Handle node, Projectile* p) = nullptr;
  void (*clear_handler)(Handle node) = nullptr;                    // node +0xbc = 0
  int32_t (*node_frame)(Handle node) = nullptr;                    // node +0xc4, the frame it last moved (likely)
  uint32_t (*node_flags)(Handle node) = nullptr;                   // node +0x24 (0x10000: takes craters)
  Handle (*parent)(Handle node, int32_t i) = nullptr;              // +0x58[i]
  Handle (*child)(Handle node, int32_t i) = nullptr;               // +0x60[i]
  uintptr_t (*handler)(Handle node) = nullptr;                     // +0xbc
  void (*set_handler)(Handle node, uintptr_t h) = nullptr;
  // The crater module (zcrater, not ported).
  void (*crater_defaults)(CraterOrder* out) = nullptr;             // 0x004ff920
  int32_t (*crater_make)(const CraterOrder* c) = nullptr;          // 0x004ffaa0, 0 made
  void (*quicksand_defaults)(QuicksandOrder* out) = nullptr;       // 0x005020f0
  void (*quicksand_make)(const QuicksandOrder* q) = nullptr;       // 0x00502110
  void (*set_active)(Handle node, int32_t on) = nullptr;           // 0x004eb170
  void (*set_name)(Handle node, const char* name) = nullptr;       // 0x004eb300
  void (*set_flag_4eb620)(Handle node, int32_t on) = nullptr;      // 0x004eb620
  void (*set_flag_4eb750)(Handle node, int32_t on) = nullptr;      // 0x004eb750
  void (*set_flag_4eb790)(Handle node, int32_t on) = nullptr;      // 0x004eb790
  void (*node_free)(Handle node) = nullptr;                        // 0x004f9050
  void (*node_destroy)(Handle node) = nullptr;                     // 0x004eaea0
  bool (*pool_live)() = nullptr;                                   // 0x004f8f50
  // object3d.c
  Handle (*object3d_create)() = nullptr;                           // 0x004fb740
  void (*set_scale)(Handle node, float x, float y, float z) = nullptr;        // 0x004fbc10
  void (*set_rotation)(Handle node, float x, float y, float z) = nullptr;     // 0x004fbd80
  void (*set_translation)(Handle node, float x, float y, float z) = nullptr;  // 0x004fc0b0
  void (*rotate)(Handle node, float x, float y, float z) = nullptr;           // 0x004fbef0
  void (*set_uniform_scale)(Handle node, float s) = nullptr;                  // 0x004fba20
  void (*set_flag_4fbb90)(Handle node, int32_t on) = nullptr;                 // 0x004fbb90
  // light.c
  Handle (*light_create)() = nullptr;                              // 0x004fa720
  void (*light_position)(Handle light, float x, float y, float z) = nullptr;  // 0x004faf80 (likely)
  void (*light_range)(Handle light, float near_, float far_) = nullptr;     // 0x004fadf0
  void (*light_color)(Handle light, float r, float g, float b) = nullptr;   // 0x004fb510 (likely)
  void (*world_add_light)(Handle light) = nullptr;                 // 0x004f2d50 on the world
  void (*world_remove_light)(Handle light) = nullptr;              // 0x004f2e00 on the world
  // Animations (zeffect) and sounds.
  Handle (*anim_attached)(Handle anim, Handle node, const void* offset) = nullptr;  // 0x00508da0(anim, 0, node, offset, 0)
  Handle (*anim_follow)(Handle anim, Handle node) = nullptr;       // 0x00508ee0(anim, 0, node, 0, node, 0)
  Handle (*anim_model)(Handle anim, Handle model) = nullptr;       // 0x00508c10(anim, model, 0, 0, 0)
  void (*anim_on_done)(Handle a, Projectile* p) = nullptr;         // 0x00508f00(a, 0x00583120, p)
  void (*anim_stop)(Handle a) = nullptr;                           // 0x00508460(a, 0)
  void (*anim_free)(Handle a) = nullptr;                           // 0x00506ca0
  Handle (*effect_handle)(Handle anim, const float args[9]) = nullptr;  // 0x00508aa0 when its result is kept
  Handle (*sound_at)(Handle sound, const Vec3f3* at, float volume) = nullptr;  // 0x0056e7e0(s, at, volume, 0)
  void (*sound_stop)(Handle s) = nullptr;                          // 0x0056e9f0
  void (*sound_frequency)(Handle s, float t) = nullptr;               // 0x0056f280: the pitch, 0..1 over its range
  // cls_di.c's queries and their settings.
  void (*impact_type)(int32_t t) = nullptr;                        // 0x005426a0
  void (*query_static)(int32_t on) = nullptr;                      // 0x004ed9d0
  void (*query_since)(int32_t on, uint32_t frame) = nullptr;       // 0x004ed9e0
  void (*query_mask)(uint32_t mask) = nullptr;                     // 0x004eda00
  // 0x004eed90: what the segment a-b crosses, in walk order; 0 when it found any.
  int32_t (*segment_list)(const Vec3f3* a, const Vec3f3* b, HitList* out) = nullptr;
  // 0x004eece0: the same with the nearest marked (HitList); 0 when it found any.
  int32_t (*segment)(const Vec3f3* from, const Vec3f3* to, HitList* out) = nullptr;
  void (*ground)(const Vec3f3* at, HitList* out) = nullptr;        // 0x004eda10: byte 0 1 = found, +0x14 the height
  // The network.
  int32_t (*net_gate)(WeaponDef* def, int32_t* net_id) = nullptr;  // [0x0072fcac]: 0 refuses the shot (Mech3.exe never sets it)
  void (*net_detonated)(WeaponDef* def, const Vec3f3* at, Handle owner) = nullptr;  // [0x0072fcb4]
};
RunHooks& run_hooks();

// Globals of other modules the run time reads and writes.
struct RunState {
  uint32_t frame = 0;                  // 0x0072f338  the frame counter
  float net_clock = 0;                 // 0x0072c538
  uint32_t tick_zone = 0;              // 0x009a17a8  the zone the tick found
  int32_t owner_hidden = 0;            // 0x009a17ac  the shooter's node was taken out of the query
  int32_t pool_objects = 0;            // 0x009a17b0  the pool's scene nodes exist
  Handle lights = nullptr;             // 0x009a0ebc  the impact light pool, linked through +0x40
};
RunState& run_state();
// [0x0074a328], the zone set the queries use: zModel's one list
// (zmod::zones().list, the camera's from 0x0040b080), read and written as the
// exe's word. view.md section 7.
uint32_t query_zone();
void set_query_zone(uint32_t zone);

// The startup (0x00583750): zWeapon's globals back to their first values,
// the definitions and the pool emptied; with the network mode (0x00583740,
// [0x005f1e5c]) the network's handler would be registered (not ported).
// Returns 0, which the start's report reads as passed.
int32_t startup();
void set_network(int32_t on);  // 0x00583740
// The mission cleanup's 0x00584fe0: every definition's live shots killed.
void kill_every_shot();

// The projectiles (zwep_shot.cpp).
Projectile* pool_take(WeaponDef* def);                                     // 0x00584df0
void pool_release(WeaponDef* def, Projectile* p);                          // 0x00584e60
void kill(WeaponDef* def, Projectile* p);                                  // 0x00584f40
void kill_all(WeaponDef* def);                                             // 0x00584fb0
void kill_all();                                                           // 0x00584fe0
int32_t pool_objects(int32_t create);                                      // 0x005847c0
Handle model_take(WeaponDef* def);                                         // 0x005830b0
void model_return(WeaponDef* def, Projectile* p);                          // 0x005830e0
void model_anim_done(Projectile* p);                                       // 0x00583120
Projectile* spawn(WeaponDef* def, Handle owner, const uint32_t* zone, const Vec3f3* muzzle, const Vec3f3* dir,
                  const Vec3f3* velocity, int32_t net_id, Projectile* slot);  // 0x00585010
void tick();                                                               // 0x005823c0, the weapon_tick action
void beam_update(WeaponDef* def);                                          // 0x005814b0 (zwep_beam.cpp)
void update(WeaponDef* def);                                               // 0x00585710
void commit(WeaponDef* def);                                               // 0x005858b0
void commit_one(WeaponDef* def, Projectile* p);                            // 0x005864c0
void retire(WeaponDef* def, Projectile* p);                                // 0x00586600
void home(WeaponDef* def, Projectile* p);                                  // 0x00585900
HitRecord* collide(WeaponDef* def, Projectile* p, const Vec3f3* to);        // 0x005861f0
void hide_shooter(WeaponDef* def, Projectile* p);                          // 0x00586160
void show_shooter(WeaponDef* def, Projectile* p);                          // 0x005861c0
HitRecord* pick(WeaponDef* def, int32_t status, HitList* list);            // 0x00583130
int32_t classify(WeaponDef* def, HitRecord* rec, const Vec3f3* from, const Vec3f3* to, float* len,
                 float* bounce_len, Vec3f3* bounce_dir);                   // 0x00582b10
void mine_idle(WeaponDef* def, Projectile* p);                             // 0x00586670
void mine_land(WeaponDef* def, Projectile* p, HitRecord* rec, Vec3f3* to); // 0x00586740
int32_t ground_burst(WeaponDef* def, Projectile* p);                       // 0x005821c0
int32_t detonate(WeaponDef* def, const Vec3f3* at, Handle owner);          // 0x005820c0
int32_t burst_at(WeaponDef* def, const Vec3f3* at);                        // 0x005822c0
float aim_slope(WeaponDef* def, const Vec3f3* from, const Vec3f3* to, float* dist);  // 0x00581ff0
struct PoolStats { int32_t size, free, flying, x0c; };
void pool_stats(PoolStats* out);                                           // 0x00581e50
void sound(WeaponDef* def, HitRecord* rec, int32_t soil, float volume);    // 0x00582f60
void bounce_sound(WeaponDef* def, HitRecord* rec, int32_t soil, float volume);  // 0x00582fc0
inline const Vec3f3* shot_line() { return &hit_state().shot_from; }        // 0x00581e30
inline Handle attacker() { return hit_state().attacker; }                  // 0x00581e40

// The impact light pool: eight lights made with the pool objects.
int32_t lights_init();                                                     // 0x00586970
int32_t lights_free();                                                     // 0x005869f0
Handle light_take(const Vec3f3* color);                                    // 0x00586b20
void light_return(Handle light);                                           // 0x00586b70

// The glow a FREEZE or IMPACT HEAT hit leaves on a vehicle (its controls'
// +0x398, 0x18 bytes; the vehicle's node follows at +0x18). docs/vehicles.md,
// The glow.
struct Glow {
  uint32_t flags = 0;        // +0x00  bits 0 and 1 set by a hit
  Handle weapon = nullptr;   // +0x04  the last hit's definition
  float x08 = 0;             // +0x08  the hit's answer reads it; only the clear writes it
  float level = 0;           // +0x0c  the hits' shares summed, -1..1 (FREEZE takes off); read by nothing
  Handle light = nullptr;    // +0x10  one of the pool's, on the node
  uint32_t x14 = 0;          // +0x14
  void clear();              // 0x005869d0 (the weapon is kept)
};
// A hit: the share added (FREEZE: taken), the pool's light of IMPACT HEAT's
// colour (+0x1d4) put on `node` the first time; 2 frozen solid (+0x08 below
// -0.5), 1 cold (at most 0), else 0.
int32_t glow_hit(Glow& g, Handle node, WeaponDef* def, float amount);   // 0x00586a30
// The vehicle's end: the light off `node` and back in the pool, the glow cleared.
void glow_end(Glow& g, Handle node);                                    // 0x00586af0

// The beams (zwep_beam.cpp).
Beam* beam_make(WeaponDef* def, Handle owner, const uint32_t* zone, const Vec3f3* origin, const Vec3f3* dir,
                int32_t segments);                                         // 0x00584a70
int32_t beam_free(Beam* b);                                                // 0x00584b40
Handle beam_segment_node(Handle model);                                    // 0x00584db0
int32_t beam_start(Beam* b);                                               // 0x00581230
int32_t beam_stop(Beam* b);                                                // 0x00581400
void beam_sort_targets(Beam* b, float* along, Vec3f3* far_dir);            // 0x00582c10
int32_t beam_cast(WeaponDef* def, Beam* b, BeamSegment* s, const Vec3f3* to);  // 0x00582de0
void beam_draw(const BeamSegment* s);                                      // 0x00582f00

void set_lock(int32_t* flag, LockRef* target);                      // 0x00581f60
bool set_fire_sound(WeaponDef* def, int32_t choice);                 // 0x00581f70
bool set_fire_animation(WeaponDef* def, int32_t choice);             // 0x00581f90
void attach_fire(WeaponDef* def, Handle node);                       // 0x00581fb0
void attach_fire(WeaponDef* def, Handle node, const void* offset);   // 0x00581fd0

}  // namespace zwep
