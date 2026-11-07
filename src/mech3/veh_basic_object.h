#pragma once
#include <cstdint>
#include <vector>

#include "gamez/zgame/unported.h"
#include "gamez/zclass/cls_di.h"
#include "mech3/mech.h"
#include "mech3/veh_controls.h"

namespace zrdr {
struct Node;
}

// The basic vehicle (trucks, APCs, tanks; the field base and the stealth
// object are made over it), docs/vehicles.md. Original file: these sit just
// before mech3\veh_basic.cpp (0x004ad930 is its first named function), with
// ground_pick (mech3/veh_basic.h); kept apart from it because mech.h's Handle
// clashes with player.h's.
namespace mech3 {

class CameraMode;
struct CameraList;
void vehicle_cameras_make();

// One engine sound (CZEngineSound, 0xc bytes, 0x004adb90 / 0x004adbb0): the
// sound by its name and the voice playing it, shared by every vehicle with
// that engine.
struct EngineSoundEntry {
  const void* owner = nullptr;  // +0x00  the EngineSound playing it this frame
  Handle sound = nullptr;       // +0x04  zSound's (0x0056f6f0)
  Handle voice = nullptr;       // +0x08  made by the first claim, kept (locked)
  void release();               // 0x004adc30: the voice sent to the far point
  int32_t claim(const void* by, const float* pos, const float* vel, uint8_t doppler);  // 0x004adc60
};
extern const float kEngineFarPoint[3];  // [0x0060b150] (0x004adb60): where a released voice goes

// The vehicles' engine sounds (CZEngineSoundList, the global at 0x0060b110):
// made at start-up (0x004ada90 -> 0x004ad840) with room for 64 and never
// grown, emptied at each mission's end (0x004d71f0 -> 0x004add10).
struct EngineSoundList {
  std::vector<EngineSoundEntry> entries;  // +0x04..+0x0c
  EngineSoundList() { entries.reserve(kCapacity); }
  static constexpr size_t kCapacity = 0x300 / 0xc;
};
EngineSoundList& engine_sounds();
EngineSoundEntry* engine_sound_find(const char* name);  // 0x004ad930; null when none or full
void engine_sounds_clear();                             // 0x004add10 (0x004d71f0)

// The engine sound at +0xe8 (vtable 0x005c8b90): the definition's
// sounds/engine entry, from the vehicles' shared cache at 0x0060b110
// (0x004adb20 -> 0x004ad930, step 1b), else null.
// Each frame the sensors release it (vt+0xc8) and their radar ping
// (0x004aa400) plays the nearest vehicle's at its place (docs/vehicles.md,
// The engine sound).
struct EngineSound {
  EngineSoundEntry* entry = nullptr;  // +0x04
  void set(const char* name);         // 0x004adb20
  void release();                     // 0x004adcd0
  int32_t play(const float* pos, const float* vel, uint8_t doppler);  // 0x004adce0: -1 without an entry
};

// The motion animation at +0xf0 (0x004aec00 zeroes it, a body the linker
// shares with IControls' hit record): the definition's motion_anim
// (0x004ade40, step 1b), stopped (0x00506ca0) as it goes (0x004addb0 ->
// 0x004ade70).
struct MotionAnim {
  Handle def = nullptr;    // +0x00  zEffect's anim def (0x0050b450)
  Handle anim = nullptr;   // +0x04  the one running (step 2)
  float until = 0;         // +0x08  the game time it runs to (0x004addc0)
  void set(const char* name);  // 0x004ade40
};

// 0xfc bytes, vtable 0x005c88e8 (89 slots).
struct BasicVehicle : Vehicle {
  uint32_t xe0 = 0;              // +0xe0
  VehicleControls* controls = nullptr;  // +0xe4  given (the AI's), else its own made here (0x004ae270)
  EngineSound engine;            // +0xe8  (0x004adaf0)
  MotionAnim motion;             // +0xf0
  bool field_base = false;       // ours: the class is the field base (vtable 0x005c23d8, mech_fieldbase.h)
};

// The calls out, by the original's address.
class VehBasicHooks : public zgame::Unported {
 public:
  static uint32_t low(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
  static Handle h(uint32_t v) { return reinterpret_cast<Handle>(static_cast<uintptr_t>(v)); }
  virtual uint32_t ref(const void* p) { return low(p); }
  // The vehicle's own block (operator new 0xfc; silent here).
  virtual void on_new(uint32_t size, const void* p) { (void)size, (void)p; }

  virtual Handle node_named(const char* name) { return h(unported("0x004f7fe0", {6, name})); }
  virtual Handle node_in_tree(Handle node, const char* name);
  virtual Handle node_clone(Handle node) { return h(unported("0x004f9af0", {low(node), 0, 0})); }
  virtual void node_set_name(Handle node, const char* name) { unported("0x004eb300", {low(node), name}); }
  virtual void world_add(Handle node) { unported("world.vt+0x0", {low(node)}); }  // [0x00608c84]
  virtual void world_remove(Handle node) { unported("0x004ebb40", {low(node)}); }  // from [0x00608c84]+4
  virtual void node_free(Handle node) { unported("0x004f9050", {low(node)}); }
  virtual void node_detach(Handle node) { unported("0x00583400", {low(node)}); }
  // The vehicle's own controls (0x004ac860(0xa70) zeroed, then 0x004ae270(this, 0, 1); item 2a).
  virtual Handle controls_alloc(uint32_t size) { return h(unported("0x004ac860", {size})); }
  virtual Handle controls_make(Handle mem, BasicVehicle& v) {
    return h(unported("0x004ae270", {low(mem), ref(&v), 0, 1}));
  }
  virtual void controls_set_owner(Handle c, BasicVehicle& v) { unported("ctl.ictl.vt+0x8", {low(c), ref(&v)}); }
  virtual void controls_delete(Handle c) { unported("ctl.ictl.vt+0x0", {low(c), 1}); }
  virtual void cameras_once() { vehicle_cameras_make(); }  // 0x004ace00
  // Ours: a vehicle made with no AI controls, so it would make its own
  // (0x004ae270, not ported: vehicles.md, The basic vehicle's own controls).
  virtual void no_ai_controls(const char* type, const char* name) { (void)type, (void)name; }
  // 0x0040adf0 on the view ([0x005f4c8c]): the view onto the vehicle in `mode`.
  virtual void view_attach(BasicVehicle& v, CameraMode* mode) {
    unported("0x0040adf0", {ref(&v), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mode))});
  }
  // 0x004d6c90(node, 0, 0, 0): zEntity's node and its markers (vehicle_set_node).
  virtual void entity_set_node(Vehicle& v, Handle node);
  // ZEntity's own 0x005a8390(node, 0, 0, 0) (the destructors' with null).
  virtual void zentity_set_node(Vehicle& v, Handle node) {
    unported("0x005a8390", {ref(&v), low(node), 0, 0, 0});
    v.node = node;
  }
  virtual void entity_register(Vehicle& v) { unported("0x005a7f50", {ref(&v)}); }
  virtual void entity_unregister(Vehicle& v) { unported("0x005a7f70", {ref(&v)}); }
  virtual void anim_stop(Handle anim) { unported("0x00506ca0", {low(anim)}); }
  virtual void weapons_free(void* w);  // 0x004b01d0 and the delete
  virtual void net_collision_free(void* n) { unported("delete", {low(n)}); }
  virtual void shadow_release(Vehicle& v) { unported("0x004d58a0", {ref(&v)}); }  // AiTarget's dtor
  // The engine sounds and the motion animation (step 1b).
  virtual Handle sound_find(const char* name);           // 0x0056f6f0: zsnd::find
  virtual const char* sound_name(Handle sound);          // 0x0056f740: zsnd::name_of
  virtual void voice_unlock(Handle voice);               // 0x0056f410: zsnd::unlock
  virtual void voice_stop(Handle voice);                 // 0x0056e9f0: zsnd::stop
  virtual int32_t voice_update(Handle voice, const float* pos, const float* vel, int32_t doppler);  // 0x00571f30
  virtual Handle sound_play(Handle sound, const float* pos, float volume, const float* vel);  // 0x0056e7e0
  virtual void voice_lock(Handle voice);                 // 0x0056f3f0: zsnd::lock
  virtual Handle anim_find(const char* name);            // 0x0050b450: zeff::anim_find
  // 0x0049bfd0, empty in this build (zerr::report here).
  virtual void report(int32_t level, const char* file, int32_t line, const char* message);
  // The definition (zReader): 0x005569e0, its node's type, 0x005569f0, 0x00557090.
  virtual zrdr::Node* reader_find(zrdr::Node* node, const char* key);
  virtual int32_t reader_type(zrdr::Node* node);
  virtual const char* reader_string(zrdr::Node* node, const char* key);
  virtual bool reader_string_value(zrdr::Node* node, const char* key, const char** out);
  // The controls' vt+0x0c (the AI's setup).
  virtual void controls_setup(Handle c, zrdr::Node* def, const char* name, int32_t kind) {
    unported("ctl.vt+0xc", {low(c), low(def), name, kind});
  }
  // The placement's (step 1c).
  virtual int32_t ground(float x, float y, float z, zcls::GroundHits* out);  // 0x004edab0 on [0x00608c84]+4
  // 0x004ee2f0 on [0x00608c84]+4: `count` probes (x, y, z each), one record each, from `top` down.
  virtual void ground_many(float* probes, int32_t count, float top, zcls::GroundHits* out);
  virtual void set_position(BasicVehicle& v, const float pos[3]);  // vt+0x18, zEntity3d's 0x005a8550
  virtual void refresh(Vehicle& v) { unported("0x005a85c0", {ref(&v)}); }
  // The three HUD markers (zTwoD images 0x0059b830, vtable 0x005bce78, +0x48 1).
  virtual Handle marker_create(const char* name) { return h(unported("0x0059b830", {name, 0, 0, 1})); }
  virtual void marker_delete(Handle m) { unported("marker.vt+0x0", {ref(m), 1}); }

  // The damage and death (step 1d). The controls' own (step 2): vt+0x1c,
  // destroyed (the AI's: the owner no longer active; the basic ones' state
  // 4); IControls vt+0xa8 at death (the AI's 0x004ea6b0, the basic ones'
  // their vt+0xa4); 0x00402510, the basic ones' path dropping its entries
  // with a negative id (through vt+0x00, null for the others).
  // The vehicle's own slots, virtual in the exe (the derived classes have their own).
  virtual void die(BasicVehicle& v);                                               // vt+0x70
  virtual int32_t damage(BasicVehicle& v, Handle weapon, uint32_t x, float amount);  // vt+0x160
  virtual uint32_t weapon_flags(Handle weapon);  // the definition's +0x6c (a read)
  virtual bool controls_destroyed(VehicleControls& c) { return c.destroyed(); }
  virtual void controls_died(VehicleControls& c) { c.died(); }
  // 0x00402510: nothing for controls that are not the basic path ones (vt+0x00 null; 2a2, deferred).
  virtual void path_drop_temporary(BasicVehicle& v) { (void)v; }
  // zWeapon's glow on the controls' +0x398 (the node after it, +0x3b0): a
  // hit (0x00586a30: 2 frozen solid, 1 cold, 0 not) and its end (0x00586af0).
  virtual int32_t glow_hit(Block398& b, Handle node, Handle weapon, float amount) {
    (void)node;
    return static_cast<int32_t>(unported("0x00586a30", {low(weapon), low(&b), zgame::bits(amount)}));
  }
  virtual void glow_end(Block398& b, Handle node) {
    (void)node;
    unported("0x00586af0", {low(&b)});
  }
  // zWeapon's hit state: the attacker's node (0x00581e40), the shot's two
  // ends (0x00581e30: from, then to), the health after (0x00583730), a
  // kill (0x00583710(1, 0)); the beam's stop (0x00581400).
  virtual Handle hit_attacker() { return h(unported("0x00581e40", {})); }
  virtual const float* shot_line();
  virtual void hit_health(float fraction) { unported("0x00583730", {zgame::bits(fraction)}); }
  virtual void hit_killed() { unported("0x00583710", {1, 0}); }
  virtual void beam_stop(Handle beam) { unported("0x00581400", {low(beam)}); }
  // The attacker's object (0x0040eee0 on the node, asked twice) and its
  // kill credit (vt+0x80 with the vehicle).
  virtual Handle node_object(Handle node) { return h(unported("0x0040eee0", {low(node)})); }
  virtual void credit_kill(Handle object, BasicVehicle& v) { unported("obj.vt+0x80", {low(object), ref(&v)}); }
  virtual void anim_start(Handle def, Handle node) { unported("0x00508c10", {low(def), low(node), 0, 0, 0}); }
  virtual void weapons_settle(void* w) { unported("0x004b1110", {low(w)}); }  // the weapons' 0x004b1110
  // The jolt's: the node's local matrix (0x004fc3f0, a 3x4, rows), read
  // through the matrix stack (0x0051e9d0, 0x0051fd70, 0x00522640, 0x0051ea00).
  virtual const float* node_matrix(Handle node) { return reinterpret_cast<const float*>(h(unported("0x004fc3f0", {low(node)}))); }
  // The difficulty (0x0040cdf0: 0 low, 1 medium, 2 high).
  virtual int32_t game_intensity();
  // The target view's tint (0x004fb8c0 on the node: colour, amount) and
  // its switch (0x004fb840).
  virtual void node_tint(Handle node, const float colour[3], float amount) {
    unported("0x004fb8c0", {low(node), zgame::bits(colour[0]), zgame::bits(colour[1]), zgame::bits(colour[2]),
                            zgame::bits(amount)});
  }
  virtual void node_tinted(Handle node, int32_t on) { unported("0x004fb840", {low(node), on}); }

  // The debug readout's (vt+0xe0): the controls' vt+0x20 (0 in every
  // MW3 controls), +0x20, vt+0x04 / vt+0x08 (mode, goal node), IControls
  // vt+0x70 (on a path); a node's name; zEntity's position (0x005a85e0);
  // the view's camera's zones ([[0x005f4c8c]+4] data +0x1e4: count, ids);
  // the HUD's text line `i` (0x005fc848 + 0x4b0 i, vt+0x7c, printf-like).
  virtual int32_t controls_dynamics(VehicleControls& c) { return c.dynamics(); }
  virtual bool controls_on_path(VehicleControls& c) { return c.on_path(); }
  virtual int32_t controls_mode(VehicleControls& c) { return c.path_mode(); }
  virtual int32_t controls_goal(VehicleControls& c) { return c.path_goal(); }
  virtual const char* node_name(Handle node);
  virtual const float* entity_position(Vehicle& v);
  virtual const uint8_t* camera_zones();
  // The setup's zone (0x0049f710): a node's flag 8 (0x004eb660 / 0x004eb620,
  // the ground query's own exclusion), its zone byte (0x004eb810).
  virtual int32_t node_flag8(Handle node);
  virtual void node_set_flag8(Handle node, int32_t on);
  virtual void node_set_zone(Handle node, uint8_t zone);
  // The frame's (2c3): the motion animation's start (0x00508c10(def, node,
  // 0, 0, 0), its handle) and the game time [0x0060991c]; the collisions
  // (0x0049ff80, step 2c4) and BASIC's step (0x004a1d90); the node's shown
  // flag (0x004eb170), rotation (0x004fbd80) and matrix (0x004eca00); a
  // node's model (0x004eb420) and its texture scroll (0x00526ae0(model, 1,
  // u, v), 0x00541ad0); the player's mech's lock position ([0x0060861c]
  // +0x14, 0x005a8630; null without one); the weapons' trigger and update
  // (0x004b0c20, 0x004b0c30).
  virtual Handle anim_run(Handle def, Handle node) { return h(unported("0x00508c10", {low(def), low(node), 0, 0, 0})); }
  virtual float game_time();
  virtual void collisions(BasicVehicle& v);  // 0x0049ff80 (vehicle_collisions, veh_move)
  virtual void basic_step(BasicVehicle& v);  // 0x004a1d90 (veh_move: vehicle_basic_step)
  virtual void move(BasicVehicle& v);  // 0x004a1240 (vehicle_move, veh_move)
  virtual void basic_ground(BasicVehicle& v);  // 0x004a1f00 (vehicle_basic_ground), after a BASIC one's move
  // The view's camera id ([0x005f4c8c]+0x58 +0x04): 3 the overhead one.
  virtual int32_t view_camera_id() { return static_cast<int32_t>(unported("[0x005f4c8c]+0x58+0x4", {})); }
  virtual void node_set_active(Handle node, int32_t on);
  virtual void node_set_rotation(Handle node, float x, float y, float z);
  virtual void node_update(Handle node);
  virtual Handle node_model(Handle node);
  virtual void model_scroll(Handle model, float u, float v);
  virtual const float* player_lock_position() { return nullptr; }
  virtual void weapons_frame(void* w, bool fire, float dt);
  // The collisions' (2c4): the world's segment query (0x004efcd0, one record
  // per two points), the node's INTERSECT flag (0x004eb6d0), a hit node's
  // object (0x0040eee0 -> 0x0040e920), the frame counter [0x0072f338], the
  // wall and object response (0x004a0b40, step 2c4b).
  virtual void segments(float* segs, int32_t points, zcls::SegmentHits* out);
  virtual void node_set_intersect(Handle node, int32_t on);
  virtual Handle hit_object(Handle node);
  virtual uint32_t frame();
  virtual void wall_response(BasicVehicle& v, const CollisionHit& hit);  // 0x004a0b40 (vehicle_wall_response, veh_move)
  // The wall response's touch: a hit node with its own handler table
  // (+0xbc above 1) whose slot 1 is set, and zWeapon's touch (0x005836b0)
  // with the segment's start and the crossing, damage 1.
  virtual bool touchable(Handle node);
  virtual float touch(Handle node, const float from[3], const CollisionHit& hit, float damage);
  // The movement's frame time and its inverse ([0x005e8110], [0x005e8114]).
  virtual float frame_dt();
  virtual float frame_inv_dt();
  virtual void hud_line(int32_t i, const char* text) { unported("hudline.vt+0x7c", {i, text}); }
};
void set_veh_basic_hooks(VehBasicHooks* h);  // null: the defaults
VehBasicHooks& veh_basic_hooks();

// Mech3.exe 1.2 @ 0x004ac320: the vehicle `name` of `type`, its node the
// world's `name` or a copy of `type`'s; null without either.
BasicVehicle* vehicle_create(Handle ctl, const char* type, const char* name);
void vehicle_construct(BasicVehicle& v, Handle ctl, Handle node);  // 0x004ac530
// Mech3.exe 1.2 @ 0x0049ec00 (vt+0x158): the definition's sounds/engine and
// motion_anim, then the controls' own setup.
void vehicle_setup(BasicVehicle& v, zrdr::Node* def, const char* name, int32_t kind);
// Mech3.exe 1.2 @ 0x004a3630 (vt+0xdc): on the ground under `pos` (from 10
// above it, 0.1 over the first polygon) or where given, facing `yaw`.
void vehicle_place(BasicVehicle& v, const float pos[3], float yaw);
// The speeds and turns the AI asks a vehicle (vt+0x114..+0x13c, the slots
// the mechs answer in mech.h), from its motion block: the top speed is
// rates[1], the slowing rate_damping[1], the turn rate turns[1]. Without
// controls, 0 (the exe reads address 0x1c for the stopping distance).
float vehicle_move_speed(const BasicVehicle& v);                  // vt+0x114 0x004acfa0: the throttle's
float vehicle_speed_at(const BasicVehicle& v, float share);       // vt+0x118 0x004acf70
float vehicle_share_for(const BasicVehicle& v, float speed);      // vt+0x11c 0x004acfe0
float vehicle_stopping_distance(const BasicVehicle& v, float speed);  // vt+0x120 0x004ad020
float vehicle_stopping_now(const BasicVehicle& v);                // vt+0x124 0x004ad050
float vehicle_stopping_at(const BasicVehicle& v, float share);    // vt+0x128 0x004ad0b0
float vehicle_stop_share(const BasicVehicle& v, float d);         // vt+0x12c 0x004ad100, 0..1
float vehicle_turn_time(const BasicVehicle& v, float angle);      // vt+0x130 0x004ad180 (a second argument unread)
float vehicle_turn_time_now(const BasicVehicle& v);               // vt+0x134 0x004ad250
float vehicle_turn_time_at(const BasicVehicle& v, float share);   // vt+0x138 0x004ad290
float vehicle_turn_share(const BasicVehicle& v, float a, float b);  // vt+0x13c 0x004ad1c0, 0..1
void vehicle_destroy(BasicVehicle& v);  // 0x004ac650 over 0x004aeb50
void vehicle_delete(BasicVehicle* v);   // 0x004ac630(1)

// Mech3.exe 1.2 @ 0x004ac3d0: the vehicles' HUD markers, ally, enemy, other
// ([0x0060b120], [0x0060b124], [0x0060b128]).
struct VehicleMarkers {
  Handle ally = nullptr, enemy = nullptr, other = nullptr;
};
VehicleMarkers& vehicle_markers();
void vehicle_markers_create();
void vehicle_markers_delete();  // 0x004ac4e0, from the stealth object's destructor 0x004a4fc0

// The vehicles' cameras (docs/vehicles.md, The cameras): one list for every
// vehicle (the static at 0x0060b130), three plain modes made once
// (0x004ace00, the flag [0x005e8578]): 0 behind, 1 on the vehicle, 3 above.
// Its current id (+0x10, [0x0060b140]) is never set after start-up, so a
// vehicle's view is always mode 0.
// The damage and death (docs/vehicles.md, Damage and death).
// Mech3.exe 1.2 @ 0x004ad3f0 (vt+0x160): `amount` off the controls' health
// from `weapon` (null: none; its +0x6c flags pick heat or freeze); 1 when
// it died without one, else 0.
int32_t vehicle_damage(BasicVehicle& v, Handle weapon, uint32_t unused, float amount);
void vehicle_die(BasicVehicle& v);  // vt+0x70 0x004ad2d0, once
// Mech3.exe 1.2 @ 0x004accf0: a hit's push along `dir` (world), turned into
// the node's frame: `turn` into the angles, `push` into +0x70.
void vehicle_jolt(BasicVehicle& v, const float dir[3], float turn, float push);
// Mech3.exe 1.2 @ 0x004af550: zWeapon's hit callback (0x005833b0(node, 1,
// this)): double damage on low difficulty, half on high, then vt+0x160.
int32_t vehicle_hit(BasicVehicle& v, Handle weapon, uint32_t x, float amount);
void vehicle_view_tint(BasicVehicle& v, uint8_t on);  // vt+0x68 0x004ad390: red by the damage, in TARGET's view (the byte passed on)
void vehicle_weapons_free(BasicVehicle& v);       // 0x004b48c0
// Mech3.exe 1.2 @ 0x0049f660 (player.cpp, after the motion block): the
// controls' vt+0x18, each of their points at the motion's collision point
// off the entity's position, then the zone (0x0049f710: the ground under it
// from 500 up, the vehicle's node left out; the picked polygon's zone record
// becomes the vehicle's, its id the one it shares with the view's camera,
// else its first; zEntity's 0x005a80d0).
void vehicle_points_place(BasicVehicle& v);
void vehicle_zone_from_ground(BasicVehicle& v);  // 0x0049f710
// The gun's aim (docs/vehicles.md, The gun's aim): the controls' pitch
// limits (+0x78c) asked of a direction in the vehicle's frame, or of a
// world one turned into it (by the rows of the vehicle's matrix, +0x0c).
bool vehicle_gun_within(const BasicVehicle& v, const float d[3]);                  // vt+0x88 0x004ad740
bool vehicle_gun_within_world(const BasicVehicle& v, const float d[3]);            // vt+0x8c 0x004ad780
bool vehicle_gun_clamp(const BasicVehicle& v, const float d[3], float out[3]);    // vt+0x90 0x004ad7c0
bool vehicle_gun_clamp_world(const BasicVehicle& v, const float d[3], float out[3]);  // vt+0x94 0x004ad7f0
float vehicle_health_share(const BasicVehicle& v);                                 // vt+0x98 0x004ad820
bool vehicle_can_aim(BasicVehicle& v, float cos_limit);                            // vt+0x150 0x004ad6e0: false without controls
void vehicle_aim_at(BasicVehicle& v, const float dir[3], uint32_t unused);        // vt+0x154 0x004ad700

// Mech3.exe 1.2 @ 0x004a2ef0 (vt+0xe0, the view target's HUD slot; its
// frame time unread): a debug readout in the HUD's text lines 1 to 4.
void vehicle_debug_lines(BasicVehicle* v);

CameraList& vehicle_cameras();
void vehicle_cameras_reset();                 // the probes' fresh start (the exe never empties it)
void vehicle_cameras_make();                  // 0x004ace00
CameraMode* vehicle_camera(int32_t id);       // vt+0x10c 0x004acde0
CameraMode* vehicle_camera_current();         // vt+0x110 0x004acdc0
void vehicle_view_attach(BasicVehicle& v);    // vt+0x108 0x004acd90: the view onto it in the current mode

}  // namespace mech3
