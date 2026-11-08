#pragma once
#include <cstdint>
#include <vector>

#include "gamez/zreader/zreader.h"

namespace mech3 { struct WeaponType; }

// zWeapon\zwep_init.c: weapons.zrd read into the weapon definitions, one per
// BALLISTICS entry (docs/weapons.md, "Definitions"). The MW3 half of each
// definition (WeaponType, def +0x1f0) is made by the callback the game passes.
namespace zwep {

using Handle = void*;  // an object of a system not ported yet (sound, animation, node)

// What plays when a weapon fires (def +0x84), in flight (+0xe0) and where it
// hits, per soil kind (+0x13c). 0x5c bytes, read by 0x00584bb0.
struct Effect {
  uint32_t flags = 0;                   // +0x00  bit 0 RANDOM_ROTATE, bit 1 ANIMATION's second value
  Handle animation = nullptr;           // +0x04
  Handle animation2 = nullptr;          // +0x08  the second animation choice (0x00581f90): never read in
  Handle animation_attached = nullptr;  // +0x0c
  Handle model_animation = nullptr;     // +0x10
  Handle surface_animation = nullptr;   // +0x14
  Handle animation_min_range = nullptr; // +0x18  an impact kind without one takes the default's
  int32_t effect = 0;                   // +0x1c  EFFECT, a zEffect index
  Handle model = nullptr;               // +0x20  MODEL, a node of list 6
  int32_t sound_count = 0;              // +0x24
  Handle sounds[4] = {};                // +0x28
  int32_t bounce_sound_count = 0;       // +0x38
  Handle bounce_sounds[4] = {};         // +0x3c
  int32_t sound_choice = 0;             // +0x4c  the sound to play, -1 none (0x00581f70)
  int32_t animation_choice = 0;         // +0x50  likewise the animation (0x00581f90)
  Handle attach_node = nullptr;         // +0x54  cleared by the reader; the firepoint's node (0x00581fd0)
  const void* attach_offset = nullptr;  // +0x58  and its offset
};

enum Soil : int32_t { kSoilDefault, kSoilWater, kSoilSeafloor, kSoilQuicksand, kSoilLava, kSoilFire, kSoilCount };  // 0x005edbb8, names 0x005edbc0

// Weapon flags (+0x6c), docs/structs.md.
enum : uint32_t {
  kBounces = 1u << 0, kBeam = 1u << 1, kTimedDamage = 1u << 2, kFlyoutHealth = 1u << 3,
  kCrater = 1u << 4, kImpactHeat = 1u << 6, kExpires = 1u << 7, kFixedRotate = 1u << 8,
  kFlyoutUnlit = 1u << 9, kFreeze = 1u << 10, kInstant = 1u << 11, kDesignate = 1u << 12,
  kCatchesFire = 1u << 13, kMine = 1u << 14, kLockOn = 1u << 15, kLockOnLead = 1u << 16,
  kMultiTarget = 1u << 17, kQuicksand = 1u << 18, kReload = 1u << 19, kRemoteDetonate = 1u << 20,
  kTetherGuided = 1u << 21, kTargetHeat = 1u << 22, kAnimationAlways = 1u << 23,
  kRelativeSpeed = 1u << 24, kDetonateAtRange = 1u << 25, kHitOwner = 1u << 26,
  kRandomDeviation = 1u << 27, kShowOnRadar = 1u << 28, kDetonateOnWater = 1u << 30,
  kRangeMinimum = 1u << 31,
};

struct WeaponDef;
struct Projectile;
struct Beam;
struct HitRecord;
// def +0x1ec: run by the impact (0x005826b0) for a shot of this weapon, set by
// the MW3 side (0x00584de0): the salvo spread, the LB-X burst, the airstrike.
// ECX def, EDX the hit, the projectile on the stack. The result's bits skip
// the impact's sound (1), craters (2) and animation (4).
using DefCallback = uint32_t (*)(WeaponDef* def, const HitRecord* hit, Projectile* p);

struct Crater {                // 10 dwords, from the crater module's defaults (0x004ff920)
  uint32_t x00 = 0;
  int32_t points = 0;          // +0x04
  uint32_t x08[2] = {};
  float depth = 0;             // +0x10
  float radius = 0;            // +0x14
  uint32_t x18[4] = {};
};

struct WeaponDef {             // 0x1f4
  const char* key = nullptr;           // +0x000  the BALLISTICS key (in the reader tree)
  const char* name = nullptr;          // +0x004  NAME, else the key
  const char* desc = nullptr;          // +0x008  DESC through the message table, else the key (strdup)
  const char* military_name = nullptr; // +0x00c  MILITARY_NAME likewise (strdup)
  int32_t index = 0;                   // +0x010  (short) the def's index
  float period = 0;                    // +0x014  1 / FIRE_RATE
  float ammo_limit = 50;               // +0x018  AMMO_LIMIT
  float range = 500;                   // +0x01c
  float range_sq = 0;                  // +0x020  set only by RANGE
  float range_min[2] = {};             // +0x024  RANGE_MINIMUM's two values
  float velocity = 120;                // +0x02c  beams: 1 / TIME_TO_MAX_RANGE; with gravity rewritten
  float turn_rate = 0.16f;             // +0x030
  float pitch_rate = 0.16f;            // +0x034
  float acceleration = 0;              // +0x038
  float impact_proximity = 0;          // +0x03c
  float impact_proximity_sq = 0;       // +0x040
  float detonation_distance_sq = 0;    // +0x044  DESIGNATE's sixth value overwrites it
  float damage = 0.1f;                 // +0x048
  float gravity = 0;                   // +0x04c
  float lock_on = 0;                   // +0x050
  float turn_suspend_time = 0;         // +0x054
  float beam_energy = 0;               // +0x058  ENERGY (beams default 1)
  float pulse[2] = {};                 // +0x05c  PULSE's second and third values (beams 0.5, 0.75)
  float pulse_rate = 0;                // +0x064  1 / PULSE's first (beams 1, 0.125 when <= 0)
  float damage_at_max_range = 0;       // +0x068  beams 0.25
  uint32_t flags = 0;                  // +0x06c
  float lock_on_lead_min = 0;          // +0x070
  float lock_on_lead_max = 0;          // +0x074
  float lock_on_lead_span = 0;         // +0x078  1 / (max - min)
  union {
    Projectile* active = nullptr;      // +0x07c  its shots in flight
    Beam* beams;                       //         a BEAM def: its beams firing
  };
  float flyout_health = 0;             // +0x080  -1 without FLYOUT_HEALTH
  Effect fire;                         // +0x084
  Effect flyout;                       // +0x0e0
  Effect* impacts = nullptr;           // +0x13c  one per soil kind
  Handle model_pool = nullptr;         // +0x140  spare copies of the FLYOUT model, linked through node +0x40
  int32_t damage_anim_count = 0;       // +0x144
  struct { float health; Handle anim; } damage_anims[4] = {};  // +0x148
  Handle destroy_animation = nullptr;  // +0x168
  Handle kill_animation = nullptr;     // +0x16c
  int32_t impact_type = 0;             // +0x170
  Crater crater;                       // +0x174  CRATER or QUICKSAND: the minimums
  Crater crater_span;                  // +0x19c  the defaults again, then max - min of each
  float target_heat[6] = {};           // +0x1c8  IMPACT HEAT, FREEZE or DESIGNATE
  float apex = 0;                      // +0x1e0  velocity^2 / (2 gravity), weapons with gravity
  uint32_t x1e4 = 0;                   // +0x1e4
  float terrain_height = 0;            // +0x1e8  height over the ground the homing last saw (shared by all shots)
  DefCallback callback = nullptr;      // +0x1ec
  mech3::WeaponType* type = nullptr;   // +0x1f0  the MW3 side
  // Port-owned extent of impacts; later soil-table changes cannot enlarge it.
  int32_t impact_count = kSoilCount;
};

struct Vec3f3 {
  float x, y, z;
};

// What a lock hands the shot: three dwords the projectile copies to +0x6c.
struct LockRef {
  Handle entity = nullptr;             // the target (its vt+0x24)
  const Vec3f3* at = nullptr;          // its position (0x005a8630)
  const Vec3f3* velocity = nullptr;    // its velocity (vt+0x0c)
};

// What a shot hit, 0x28 bytes (docs/weapons.md, "Hits"); the Collision that
// Mech::TakeDamage reads.
struct HitRecord {
  Vec3f3 normal{};             // +0x00  the surface's
  Vec3f3 point{};              // +0x0c
  uint32_t x18 = 0;
  float distance_sq = 0;       // +0x1c
  Handle material = nullptr;   // +0x20  its +0x20 is the soil kind (1 water)
  Handle node = nullptr;       // +0x24  the node hit
};

// What a query found: the exe's 0x504-byte buffer, a count byte then
// 0x28-byte records (31 fit). The segment query 0x004eed90 and the sphere
// query 0x004f0ac0 write the count in byte 0; the nearest-hit query 0x004eece0
// moves it to byte 1 and puts the nearest record's index in byte 0, which is
// what the hit pick 0x00583130 reads.
struct HitList {
  uint8_t count = 0;           // +0  the count, or the nearest record's index
  uint8_t total = 0;           // +1  the count, after 0x004eece0
  HitRecord rec[31];           // +4
};

// A shot in flight, 0x5d8 bytes (docs/weapons.md, "Projectile"): the pool is
// sized at load and kept on a free list.
struct Projectile {
  Projectile* next = nullptr;  // +0x000  the free list, then the weapon's active list
  Handle owner = nullptr;      // +0x004  the shooter's node
  uint32_t zone = 0;           // +0x008  the shooter's zone set; low byte 4: none (only that byte is written)
  Handle node = nullptr;       // +0x00c  its scene node
  Handle flyout_anim = nullptr;      // +0x010  FLYOUT ANIMATION, following the node
  Handle flyout_attached = nullptr;  // +0x014  FLYOUT ANIMATION_ATTACHED
  Handle model_anim = nullptr;       // +0x018  FLYOUT MODEL_ANIMATION on the pooled model
  Handle model = nullptr;            // +0x01c  the pooled copy of the FLYOUT model
  int32_t state = 0;           // +0x020  1 flying, 0 to remove, 2 a dormant mine
  Vec3f3 spawn_at{};           // +0x024
  Vec3f3 base_velocity{};      // +0x030  the shooter's, kept for a locked shot
  Vec3f3 direction{};          // +0x03c  unit
  Vec3f3 position{};           // +0x048
  Vec3f3 next_position{};      // +0x054  where this frame's move ends
  Vec3f3 velocity{};           // +0x060
  Handle lock_entity = nullptr;           // +0x06c
  const Vec3f3* target_at = nullptr;      // +0x070  the lock's position
  const Vec3f3* target_velocity = nullptr;  // +0x074
  int32_t static_hits = 0;     // +0x078  1: the static world on its path was cast at the spawn
  uint32_t static_frame = 0;   // +0x07c  the frame of that cast ([0x0072f338])
  HitList statics;             // +0x080  what it found, nearest first (mostly: docs/weapons.md)
  int32_t static_next = 0;     // +0x584  the next static record to reach
  float homing_time = 0;       // +0x588
  float speed = 0;             // +0x58c
  float max_speed = 0;         // +0x590
  uint32_t x594 = 0;           // +0x594
  Vec3f3 deviation{};          // +0x598  RANDOM_DEVIATION's drift
  Vec3f3 drift{};              // +0x5a4  the drift so far, held within +-1 (y +-0.75)
  float travelled = 0;         // +0x5b0
  float age = 0;               // +0x5b4
  float health = -1;           // +0x5b8  FLYOUT_HEALTH, -1 none
  float damage_scale = 1;      // +0x5bc  FireState::damage_scale at the shot
  float track_scale[2] = {1, 1};  // +0x5c0  FireState::track_scale ([0] read by the homing behind bugs; [1] by nothing)
  int32_t terrain_follow = 0;  // +0x5c8  FireState::terrain_follow (FCS)
  int32_t net_id = 0;          // +0x5cc  spawn's fifth argument
  void (*tick)(Projectile* p) = nullptr;  // +0x5d0  run each frame (the radar's), ECX the shot
  uintptr_t tick_arg = 0;      // +0x5d4
};

// The zWeapon globals the reader fills.
struct State {
  Handle world = nullptr;              // 0x009a0ea0  ECX of the init
  int32_t init_arg = 0;                // 0x009a0ec0
  zrdr::Node* tree = nullptr;          // 0x009a1778  kept: the defs point into it
  Handle lock_on_warning = nullptr;    // 0x009a177c  sounds
  Handle no_ammo_warning = nullptr;    // 0x009a1794
  Handle trigger_inactive = nullptr;   // 0x009a1798
  Handle weapon_inactive = nullptr;    // 0x009a179c
  float max_crater_radius = 30;        // 0x009a1784  30 from the startup 0x00583750
  std::vector<WeaponDef> defs;         // 0x009a0eac, count 0x009a0ea8
  std::vector<Projectile> pool;        // 0x009a0eb4, count 0x009a0eb0
  Projectile* free_list = nullptr;     // 0x009a0eb8
  Handle tick = nullptr;               // 0x009a0ea4  the weapon_tick object
};
State& state();

// The calls the reader makes into systems not ported here. Unset ones do
// nothing and return 0.
struct InitHooks {
  void (*lights_init)() = nullptr;                          // 0x00586970: 8 lights for impacts
  Handle (*sound)(const char* name) = nullptr;              // 0x0056f6f0, zSound
  const char* (*message)(const char* key) = nullptr;        // 0x00575fb0, Mech3Msg.dll
  Handle (*anim)(const char* name) = nullptr;               // 0x0050b450, the mission's anim defs
  int32_t (*effect)(const char* name) = nullptr;            // 0x0050d9e0, zEffect
  Handle (*find_node)(int32_t list, const char* name) = nullptr;  // 0x004f7fe0
  void (*crater_defaults)(Crater* out) = nullptr;           // 0x004ff920: copies 0x0060bfc0
  void (*set_intersect_surface)(Handle node, int32_t on) = nullptr;  // 0x004eb6d0
  void (*set_active)(Handle node, int32_t on) = nullptr;    // 0x004eb170
  int32_t (*tree_unlit)(Handle node) = nullptr;             // 0x004f9e00 with 0x004f9de0
  void (*pool_objects)(int32_t create) = nullptr;           // 0x005847c0
  Handle (*object3d_create)() = nullptr;                    // 0x004fb740
  void (*set_action_priority)(Handle node, int32_t p) = nullptr;  // 0x004eb5b0
  void (*set_tick_action)(Handle node) = nullptr;           // 0x004eb450 with 0x005823c0 (weapon_tick)
  void (*error)(int32_t level, int32_t line, const char* what) = nullptr;  // 0x0049bfd0
  void (*node_destroy)(Handle node) = nullptr;              // 0x004eaea0, the tick object at shutdown
  bool hardware = false;                                    // [0x0072f348] Direct3D
};
InitHooks& init_hooks();

// The game's half of each definition, called with the def's reader list.
using TypeCallback = void (*)(zrdr::Node* node, WeaponDef* def);

int init(Handle world, const char* path, int32_t arg, TypeCallback callback);  // 0x00583880
// 0x00584930: the game's per-def cleanup first (0x004b0090 frees the WeaponType).
void shutdown(void (*per_def)(WeaponDef* def));
void read_effect(zrdr::Node* parent, Effect* out, const char* key);            // 0x00584bb0
WeaponDef* find_def(const char* name);                                          // 0x00581ed0
inline void set_callback(WeaponDef* def, DefCallback fn) { def->callback = fn; }  // 0x00584de0

}  // namespace zwep
