#include "mech3/veh_basic_object.h"

#include "mech3/content_registry.h"
#include "mech3/content_world.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "crt/msvc_math.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zweapon/zweapon.h"
#include "gamez/zweapon/zwep_init.h"
#include "mech3/mech_build.h"
#include "mech3/mech_fieldbase.h"
#include "mech3/mission_world.h"
#include "mech3/player_view.h"
#include "mech3/app_view.h"
#include "mech3/veh_basic.h"
#include "mech3/weapon.h"

namespace mech3 {

int32_t game_intensity();  // options.h (its Handle clashes with mech.h's)
void* node_entity(void* node);  // app_mech.h's 0x0040e920
float player_dt();         // player.h's [0x005e8110] and [0x005e8114] (likewise)
float player_inv_dt();

namespace {
VehBasicHooks g_default_hooks;
VehBasicHooks* g_hooks = &g_default_hooks;
VehicleMarkers g_markers;
EngineSoundList g_engine_sounds;  // 0x0060b110
CameraList g_cameras;             // 0x0060b130 (freed at exit, 0x004ac300)
bool g_cameras_pending = true;    // 0x005e8578
zsnd::Object* snd(Handle h) { return static_cast<zsnd::Object*>(h); }
zsnd::VoiceBase* voice(Handle h) { return static_cast<zsnd::VoiceBase*>(h); }
}  // namespace

void set_veh_basic_hooks(VehBasicHooks* h) { g_hooks = h ? h : &g_default_hooks; }
VehBasicHooks& veh_basic_hooks() { return *g_hooks; }
float VehBasicHooks::frame_dt() { return player_dt(); }
float VehBasicHooks::game_time() { return zgame::from_bits(unported("[0x0060991c]", {})); }
void VehBasicHooks::segments(float* segs, int32_t points, zcls::SegmentHits* out) {
  if (zcls::Node* w = world_hooks().world) zcls::segment_query(w, segs, points, out);
}
void VehBasicHooks::node_set_intersect(Handle node, int32_t on) { zcls::node_set_intersect(static_cast<zcls::Node*>(node), on); }
Handle VehBasicHooks::hit_object(Handle node) { return node_entity(node); }
uint32_t VehBasicHooks::frame() { return zcls::frames().frame; }
void VehBasicHooks::node_set_active(Handle node, int32_t on) { zcls::node_set_active(static_cast<zcls::Node*>(node), on); }
void VehBasicHooks::node_set_rotation(Handle node, float x, float y, float z) {
  zcls::object3d_set_rotation(static_cast<zcls::Node*>(node), x, y, z);
}
void VehBasicHooks::node_update(Handle node) { zcls::node_update(static_cast<zcls::Node*>(node)); }
Handle VehBasicHooks::node_model(Handle node) { return static_cast<zcls::Node*>(node)->model; }
void VehBasicHooks::model_scroll(Handle model, float u, float v) {
  unported("0x00526ae0", {low(model), 1, zgame::bits(u), zgame::bits(v)});
  unported("0x00541ad0", {low(model)});
}
void VehBasicHooks::weapons_frame(void* w, bool fire, float dt) {
  auto* m = static_cast<WeaponManager*>(w);
  m->set_trigger(fire);
  m->update(dt);
}
float VehBasicHooks::frame_inv_dt() { return player_inv_dt(); }

void VehBasicHooks::entity_set_node(Vehicle& v, Handle node) {
  unported("0x004d6c90", {ref(&v), low(node), 0, 0, 0});
  vehicle_set_node(v, node);
}

void VehBasicHooks::weapons_free(void* w) { delete static_cast<WeaponManager*>(w); }

Handle VehBasicHooks::sound_find(const char* name) { return zsnd::find(name); }
const char* VehBasicHooks::sound_name(Handle sound) { return zsnd::name_of(snd(sound)); }
void VehBasicHooks::voice_unlock(Handle v) { zsnd::unlock(voice(v)); }
void VehBasicHooks::voice_stop(Handle v) { zsnd::stop(voice(v)); }
int32_t VehBasicHooks::voice_update(Handle v, const float* pos, const float* vel, int32_t doppler) {
  return zsnd::update_3d(static_cast<zsnd::Voice*>(voice(v)), pos, vel, doppler);
}
Handle VehBasicHooks::sound_play(Handle sound, const float* pos, float volume, const float* vel) {
  return zsnd::play_at(snd(sound), pos, volume, vel);
}
void VehBasicHooks::voice_lock(Handle v) { zsnd::lock(voice(v)); }
Handle VehBasicHooks::anim_find(const char* name) { return zeff::anim_find(name); }
void VehBasicHooks::report(int32_t level, const char* file, int32_t line, const char* message) {
  zerr::report(level, file, line, message);
}
zrdr::Node* VehBasicHooks::reader_find(zrdr::Node* node, const char* key) { return zrdr::find(node, key); }
int32_t VehBasicHooks::reader_type(zrdr::Node* node) { return node->type; }
const char* VehBasicHooks::reader_string(zrdr::Node* node, const char* key) { return zrdr::get_string(node, key); }
bool VehBasicHooks::reader_string_value(zrdr::Node* node, const char* key, const char** out) {
  return zrdr::get_string_value(zrdr::find(node, key), out);
}

// vt+0x70 and vt+0x160 by the class.
void VehBasicHooks::die(BasicVehicle& v) {
  if (v.field_base) return field_base_die(static_cast<FieldBase&>(v));
  vehicle_die(v);
}
int32_t VehBasicHooks::damage(BasicVehicle& v, Handle weapon, uint32_t x, float amount) {
  if (v.field_base) return field_base_damage(static_cast<FieldBase&>(v), weapon, x, amount);
  return vehicle_damage(v, weapon, x, amount);
}
uint32_t VehBasicHooks::weapon_flags(Handle weapon) { return static_cast<zwep::WeaponDef*>(weapon)->flags; }
const float* VehBasicHooks::shot_line() { return reinterpret_cast<const float*>(zwep::shot_line()); }  // from, then to
int32_t VehBasicHooks::game_intensity() { return mech3::game_intensity(); }
const uint8_t* VehBasicHooks::camera_zones() {
  const PlayerView* v = program_view();
  return v ? static_cast<const uint8_t*>(static_cast<zcls::Node*>(v->camera)->data) + 0x1e4 : nullptr;
}
int32_t VehBasicHooks::node_flag8(Handle node) {
  int32_t f = 0;
  zcls::node_get_flag8(static_cast<zcls::Node*>(node), &f);
  return f;
}
void VehBasicHooks::node_set_flag8(Handle node, int32_t on) { zcls::node_set_flag8(static_cast<zcls::Node*>(node), on); }
void VehBasicHooks::node_set_zone(Handle node, uint8_t zone) { zcls::node_set_zone(static_cast<zcls::Node*>(node), zone); }
const char* VehBasicHooks::node_name(Handle node) { return static_cast<const char*>(node); }  // a node starts with its name
const float* VehBasicHooks::entity_position(Vehicle& v) {
  static float p[3];
  zcls::node_world_position(static_cast<zcls::Node*>(v.node), p);
  return p;
}

EngineSoundList& engine_sounds() { return g_engine_sounds; }

// Mech3.exe 1.2 @ 0x004ad930: the entry whose sound is named `name`, else a
// new one if zSound has the sound and the list has room.
EngineSoundEntry* engine_sound_find(const char* name) {
  VehBasicHooks& h = *g_hooks;
  std::vector<EngineSoundEntry>& list = g_engine_sounds.entries;
  for (EngineSoundEntry& e : list) {
    const char* n = h.sound_name(e.sound);
    if (name && n && std::strcmp(name, n) == 0) return &e;
  }
  EngineSoundEntry made;  // 0x004adb90 -> 0x004adbb0
  made.sound = name ? h.sound_find(name) : nullptr;
  if (!made.sound) return nullptr;
  if (list.size() >= EngineSoundList::kCapacity) {
    h.report(0x200, "C:\\zipper\\Mech3\\veh_basic.cpp", 0x358, "CZEngineSoundList is too small");
    return nullptr;
  }
  list.push_back(made);  // within the reserve: the entries never move
  return &list.back();
}

// Mech3.exe 1.2 @ 0x004add10: each entry's voice stopped (0x004adbf0), then
// the list emptied.
void engine_sounds_clear() {
  VehBasicHooks& h = *g_hooks;
  for (EngineSoundEntry& e : g_engine_sounds.entries) {
    if (e.voice) {
      h.voice_unlock(e.voice);
      h.voice_stop(e.voice);
    }
    e = EngineSoundEntry{};
  }
  g_engine_sounds.entries.clear();
}

// Mech3.exe 1.2 @ 0x004adb20.
void EngineSound::set(const char* name) { entry = engine_sound_find(name); }

// Mech3.exe 1.2 @ 0x004adb60 (a start-up initialiser): sqrt(1.0208e38) by
// 0.5, -0.5, 0.5, about 5e18 out.
const float kEngineFarPoint[3] = {
    static_cast<float>(std::sqrt(1.0208470804804034e+38) * 0.5),
    static_cast<float>(std::sqrt(1.0208470804804034e+38) * -0.5),
    static_cast<float>(std::sqrt(1.0208470804804034e+38) * 0.5)};

// Mech3.exe 1.2 @ 0x004adc30
void EngineSoundEntry::release() {
  if (!owner) return;
  owner = nullptr;
  if (voice) g_hooks->voice_update(voice, kEngineFarPoint, nullptr, 0);
}

// Mech3.exe 1.2 @ 0x004adc60: taken by the first vehicle to ask this frame;
// the voice moved to `pos`, or made there at full volume and locked.
int32_t EngineSoundEntry::claim(const void* by, const float* pos, const float* vel, uint8_t doppler) {
  if (owner != by && owner) return -1;
  owner = by;
  if (voice) return g_hooks->voice_update(voice, pos, vel, doppler);
  voice = g_hooks->sound_play(sound, pos, 1.0f, nullptr);
  if (voice) g_hooks->voice_lock(voice);
  return -1;
}

// Mech3.exe 1.2 @ 0x004adcd0
void EngineSound::release() {
  if (entry) entry->release();
}

// Mech3.exe 1.2 @ 0x004adce0
int32_t EngineSound::play(const float* pos, const float* vel, uint8_t doppler) {
  return entry ? entry->claim(this, pos, vel, doppler) : -1;
}

// Mech3.exe 1.2 @ 0x004ade40, over 0x004ade70 (the running one stopped).
void MotionAnim::set(const char* name) {
  VehBasicHooks& h = *g_hooks;
  if (anim) {
    h.anim_stop(anim);
    anim = nullptr;
  }
  def = nullptr;
  if (name) def = h.anim_find(name);
}

// Mech3.exe 1.2 @ 0x004ac320 (ECX the controls, EDX the type).
Handle VehBasicHooks::node_in_tree(Handle node, const char* name) {
  return zcls::node_find_in_tree(static_cast<zcls::Node*>(node), name);
}
BasicVehicle* vehicle_create(Handle ctl, const char* type, const char* name) {
  if (active_content() && content_index("vehicle", type) >= 0 && !ctl)
    cc::content::fail("vehicle-controller", "A managed vehicle requires an implemented native AI controller; parked/player controls are unavailable", {}, type);
  VehBasicHooks& h = *g_hooks;
  Handle node = h.node_named(name);
  if (!node) node = create_content_vehicle_node(type, name);
  if (!node) {
    Handle model = h.node_named(type);
    if (!model) model = h.node_named(content_vehicle_model(type));
    if (!model) return nullptr;
    node = h.node_clone(model);
    if (!node) return nullptr;
    h.node_set_name(node, name);
    h.world_add(node);
  }
  if (content_mission_actor_required(name)) {
    for (const auto& group : {std::make_pair("support", 4), std::make_pair("collide", 12)})
      for (int i = 0; i < group.second; ++i) {
        char point[20];
        std::snprintf(point, sizeof point, "%s%02d", group.first, i);
        if (!h.node_in_tree(node, point))
          cc::content::fail("vehicle-geometry", "Required managed vehicle lacks an authored support or collision point", {}, content_selected_mission(), point);
      }
  }
  auto* v = new BasicVehicle;
  h.on_new(0xfc, v);
  if (!ctl) h.no_ai_controls(type, name);  // ours: its own controls (0x004ae270) are not ported
  vehicle_construct(*v, ctl, node);
  return v;
}

// Mech3.exe 1.2 @ 0x004ac530 (ctl, node).
void vehicle_construct(BasicVehicle& v, Handle ctl, Handle node) {
  VehBasicHooks& h = *g_hooks;
  vehicle_base(v);
  v.controls = static_cast<VehicleControls*>(ctl);
  v.engine = EngineSound{};  // 0x004adaf0
  v.motion = MotionAnim{};  // 0x004aec00
  v.node = nullptr;
  if (!v.controls) {
    const Handle mem = h.controls_alloc(0xa70);
    v.controls = mem ? static_cast<VehicleControls*>(h.controls_make(mem, v)) : nullptr;
  } else {
    h.controls_set_owner(v.controls, v);
  }
  v.xe0 = 0;
  h.cameras_once();
  if (node) {
    h.entity_set_node(v, node);
    h.entity_register(v);
  }
}

namespace {

// Mech3.exe 1.2 @ 0x004aeb50, the Vehicle base, with AiTarget's 0x004d58a0
// and ZEntity3d's 0x005a7f90.
void vehicle_base_destroy(Vehicle& v) {
  VehBasicHooks& h = *g_hooks;
  if (v.node) {
    h.entity_set_node(v, nullptr);
    h.entity_unregister(v);
  }
  if (v.ammo) {
    h.weapons_free(v.ammo);
    v.ammo = nullptr;
  }
  if (v.net_collision) {
    h.net_collision_free(v.net_collision);
    v.net_collision = nullptr;
  }
  v.clear_contacts();  // 0x004d58a0: 0x004a7be0
  if (v.node) {
    h.entity_set_node(v, nullptr);
    h.entity_unregister(v);
  }
  v.net = 0;
  v.release_shadow();
  if (v.node) h.zentity_set_node(v, nullptr);  // ZEntity3d's 0x005a7f90
  h.entity_unregister(v);                      // there whether listed or not
  h.zentity_set_node(v, nullptr);              // ZEntity's 0x005a8370
}

}  // namespace

// Mech3.exe 1.2 @ 0x0049ec00.
void vehicle_setup(BasicVehicle& v, zrdr::Node* def, const char* name, int32_t kind) {
  VehBasicHooks& h = *g_hooks;
  zrdr::Node* sounds = h.reader_find(def, "sounds");
  if (sounds && h.reader_type(sounds) == zrdr::List) {
    const char* engine = h.reader_string(sounds, "engine");
    if (engine) v.engine.set(engine);
  }
  const char* motion = nullptr;
  if (h.reader_string_value(def, "motion_anim", &motion) && motion) v.motion.set(motion);
  h.controls_setup(v.controls, def, name, kind);
}

void VehBasicHooks::ground_many(float* probes, int32_t count, float top, zcls::GroundHits* out) {
  if (zcls::Node* w = world_hooks().world) zcls::ground_query(w, probes, count, top, out);
}
int32_t VehBasicHooks::ground(float x, float y, float z, zcls::GroundHits* out) {
  zcls::Node* w = world_hooks().world;  // [0x00608c84]+4
  if (!w) {
    out->count = 0;  // the exe needs a world
    return 1;
  }
  return zcls::ground_point(w, x, y, z, out);
}
void VehBasicHooks::set_position(BasicVehicle& v, const float pos[3]) {
  unported("veh.vt+0x18", {ref(&v), zgame::bits(pos[0]), zgame::bits(pos[1]), zgame::bits(pos[2])});
}

// Mech3.exe 1.2 @ 0x004a3630. The exe writes the controls without asking
// whether there are any (a vehicle always has them unless their block could
// not be had).
void vehicle_place(BasicVehicle& v, const float pos[3], float yaw) {
  VehBasicHooks& h = *g_hooks;
  float p[3] = {pos[0], pos[1], pos[2]};
  zcls::GroundHits hits{};
  if (h.ground(p[0], p[1] - -10.0f, p[2], &hits) == 0 && hits.count > 0) p[1] = hits.hit[0].height - -0.1f;
  h.set_position(v, p);
  VehicleControls& c = *v.controls;
  c.angles.y = yaw;
  c.clear_commands();  // IControls vt+0x14
  h.refresh(v);
  c.x70 = Vec3c{};
  for (int32_t i = 0; i < c.motion.support_count && i < 16; ++i) c.motion.support_word[i] = 0;
  c.x20 = 0, c.x24 = 0;
  if (c.serial)
    for (int32_t i = 0; i < c.point_count && i < 15; ++i) c.points[i] = {p[0], p[1], p[2]};
}

// --- the speeds and turns -----------------------------------------------------

namespace {
const VehicleMotion* motion(const BasicVehicle& v) { return v.controls ? &v.controls->motion : nullptr; }
// The x87's clamp to 0..1: over 1 (ordered) gives 1, under 0 or NaN 0.
float unit(float x) {
  if (x > 1.0) return 1.0f;
  if (!(x >= 0.0)) return 0.0f;
  return x;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004acfa0: the IControls' throttle (vt+0x140, 0x004020b0;
// its vt+0x1c, +0x08) times the top speed.
float vehicle_move_speed(const BasicVehicle& v) {
  const VehicleMotion* m = motion(v);
  return m ? v.controls->throttle * m->rates[1] : 0.0f;
}

// Mech3.exe 1.2 @ 0x004acf70
float vehicle_speed_at(const BasicVehicle& v, float share) {
  const VehicleMotion* m = motion(v);
  return m ? m->rates[1] * share : 0.0f;
}

// Mech3.exe 1.2 @ 0x004acfe0 (a top speed of 0, or NaN, gives 0).
float vehicle_share_for(const BasicVehicle& v, float speed) {
  const VehicleMotion* m = motion(v);
  return m && !(m->rates[1] == 0.0f || std::isnan(m->rates[1])) ? speed / m->rates[1] : 0.0f;
}

// Mech3.exe 1.2 @ 0x004ad020
float vehicle_stopping_distance(const BasicVehicle& v, float speed) {
  const VehicleMotion* m = motion(v);
  return m ? speed / m->rate_damping[1] : 0.0f;
}

// Mech3.exe 1.2 @ 0x004ad050
float vehicle_stopping_now(const BasicVehicle& v) {
  return vehicle_stopping_distance(v, motion(v) ? std::fabs(vehicle_move_speed(v)) : 0.0f);
}

// Mech3.exe 1.2 @ 0x004ad0b0
float vehicle_stopping_at(const BasicVehicle& v, float share) {
  return vehicle_stopping_distance(v, std::fabs(vehicle_speed_at(v, share)));
}

// Mech3.exe 1.2 @ 0x004ad100: the throttle share that slows to a stop within
// `d`.
float vehicle_stop_share(const BasicVehicle& v, float d) {
  const VehicleMotion* m = motion(v);
  if (!m) return 0.0f;
  const float t = m->rate_damping[1] * d;
  const float x = !(m->rates[1] == 0.0f || std::isnan(m->rates[1])) ? t / m->rates[1] : 0.0f;
  return unit(x);
}

// Mech3.exe 1.2 @ 0x004ad180: `angle` over the turn rate (at least 0.0001,
// a NaN rate too).
float vehicle_turn_time(const BasicVehicle& v, float angle) {
  const VehicleMotion* m = motion(v);
  float rate = m ? m->turns[1] : 0.0f;
  if (!(rate >= 0.0001f)) rate = 0.0001f;  // 0x005e857c
  return angle / rate;
}

// Mech3.exe 1.2 @ 0x004ad250: the controls' |+0x78| (with IControls'
// GetTurn, vt+0x24, which 0x004ad180 does not read).
float vehicle_turn_time_now(const BasicVehicle& v) {
  return v.controls ? vehicle_turn_time(v, std::fabs(v.controls->x70.z)) : 0.0f;
}

// Mech3.exe 1.2 @ 0x004ad290
float vehicle_turn_time_at(const BasicVehicle& v, float share) {
  return vehicle_turn_time(v, std::fabs(vehicle_speed_at(v, share)));
}

// Mech3.exe 1.2 @ 0x004ad1c0: |turns[1] b| a over the top speed, 0..1.
float vehicle_turn_share(const BasicVehicle& v, float a, float b) {
  const VehicleMotion* m = motion(v);
  if (!m || m->rates[1] == 0.0f || std::isnan(m->rates[1])) return 0.0f;
  return unit(std::fabs(m->turns[1] * b) * a / m->rates[1]);
}

// Mech3.exe 1.2 @ 0x004ac650.
void vehicle_destroy(BasicVehicle& v) {
  VehBasicHooks& h = *g_hooks;
  if (v.engine.entry) v.engine.entry = nullptr;  // 0x004adb40
  if (v.controls) h.controls_delete(v.controls);
  const Handle node = v.node;
  v.controls = nullptr;
  if (node) {
    h.node_detach(node);
    h.entity_set_node(v, nullptr);
    h.entity_unregister(v);
    h.world_remove(node);
    h.node_free(node);
  }
  if (v.motion.anim) {  // 0x004addb0 -> 0x004ade70
    h.anim_stop(v.motion.anim);
    v.motion.anim = nullptr;
  }
  vehicle_base_destroy(v);
}

// Mech3.exe 1.2 @ 0x004ac630(1).
void vehicle_delete(BasicVehicle* v) {
  if (!v) return;
  vehicle_destroy(*v);
  delete v;
}

VehicleMarkers& vehicle_markers() { return g_markers; }

// Mech3.exe 1.2 @ 0x004ac3d0.
void vehicle_markers_create() {
  VehBasicHooks& h = *g_hooks;
  g_markers.ally = h.marker_create("hud_veh_ally_sm");
  g_markers.enemy = h.marker_create("hud_veh_enemy_sm");
  g_markers.other = h.marker_create("hud_veh_other_sm");
}

// Mech3.exe 1.2 @ 0x004ac4e0: each one's vt+0 (1), then none.
void vehicle_markers_delete() {
  VehBasicHooks& h = *g_hooks;
  for (Handle* m : {&g_markers.ally, &g_markers.enemy, &g_markers.other}) {
    if (*m) h.marker_delete(*m);
    *m = nullptr;
  }
}

CameraList& vehicle_cameras() { return g_cameras; }
void vehicle_cameras_reset() {
  for (CameraMode*& c : g_cameras.modes) delete c;
  g_cameras.modes.clear();
  g_cameras.current = 0;
  g_cameras_pending = true;
}

// Mech3.exe 1.2 @ 0x004ace00: plain modes (0x0040a950), none of the mechs'
// definition blocks; behind is pitched down 0.06 rad, above straight down.
void vehicle_cameras_make() {
  if (!g_cameras_pending) return;
  g_cameras_pending = false;
  auto* behind = new CameraMode(0);
  zmth::quat_pitch(zgame::from_bits(0xbd75c28f), *reinterpret_cast<zmth::Quat*>(behind->delta_rot));
  behind->desired_pos[0] = 0.0f;
  behind->desired_pos[1] = zgame::from_bits(0x3edc28f6);  // 0.43, not normalised
  behind->desired_pos[2] = 1.0f;
  behind->dist_vary_factor = 1.0f;
  g_cameras.modes.push_back(behind);

  auto* on = new CameraMode(1);
  on->dist = 1.0f;
  on->delta_rot[0] = 1.0f, on->delta_rot[1] = on->delta_rot[2] = on->delta_rot[3] = 0.0f;  // 0x005eda50
  on->desired_pos[0] = 0.0f;
  on->desired_pos[1] = 1.0f;
  on->desired_pos[2] = 0.0f;
  on->dist_vary_factor = 0.0f;
  g_cameras.modes.push_back(on);

  auto* above = new CameraMode(3);
  above->dist = 150.0f;
  zmth::quat_pitch(zgame::from_bits(0xbfc90fd8), *reinterpret_cast<zmth::Quat*>(above->delta_rot));
  above->desired_pos[0] = 0.0f;
  above->desired_pos[1] = 1.0f;
  above->desired_pos[2] = 0.0f;
  above->dist_vary_factor = 0.0f;
  above->dist_vary = 0.0f;
  above->dist_factor = 0.0f;
  g_cameras.modes.push_back(above);
}

// Mech3.exe 1.2 @ 0x004acde0
CameraMode* vehicle_camera(int32_t id) { return g_cameras.find(id); }
// Mech3.exe 1.2 @ 0x004acdc0
CameraMode* vehicle_camera_current() { return g_cameras.find(g_cameras.current); }
// Mech3.exe 1.2 @ 0x004acd90
void vehicle_view_attach(BasicVehicle& v) { g_hooks->view_attach(v, vehicle_camera_current()); }

// ---------------------------------------------------------------------------
// Damage and death (step 1d)

// Mech3.exe 1.2 @ 0x004ad3f0. A heat weapon (every laser's IMPACT HEAT) takes
// its damage and warms the glow by the share lost; a freeze one only chills
// (the vehicle frozen solid below -0.5 takes nothing more from it); once
// frozen, other hits take x08 x health more (x08 negative then: a gain, as
// written; no shipped weapon freezes). Dead at 0 or not a number. A hit over
// 5 by a weapon without heat jolts it along the shot.
int32_t vehicle_damage(BasicVehicle& v, Handle weapon, uint32_t unused, float amount) {
  (void)unused;
  VehBasicHooks& h = *g_hooks;
  VehicleControls& c = *v.controls;
  if (h.controls_destroyed(c)) {
    v.active = false;
    v.engine.entry = nullptr;  // 0x004adb40
    return 0;
  }
  const uint32_t flags = weapon ? h.weapon_flags(weapon) : 0;
  if (weapon && (flags & zwep::kTargetHeat)) {
    if (flags & zwep::kFreeze) {
      if (h.glow_hit(c.x398, c.node, weapon, amount * 0.01f) == 2) {
        c.frozen = 1;
        return 0;
      }
    } else {
      c.health = c.health - amount;
    }
    h.glow_hit(c.x398, c.node, weapon, 1.0f - c.health / c.health_max);
  } else if (c.frozen) {
    c.health = c.health - (c.x398.x08 * c.health + amount);
  } else {
    c.health = c.health - amount;
  }
  if (!(c.health > 0.0f)) {
    if (!weapon) {
      h.die(v);
      return 1;
    }
    if (Handle a = h.hit_attacker()) {
      if (h.node_object(a)) h.credit_kill(h.node_object(a), v);
    }
    h.hit_killed();
    h.die(v);
    return 0;
  }
  h.hit_health(c.health / c.health_max);
  if (weapon && !(h.weapon_flags(weapon) & zwep::kTargetHeat) && amount > 5.0f) {
    const float k = c.motion.inv_mass * amount;
    const float turn = k * 0.025f, push = k * 1.667f;
    const float* s = h.shot_line();
    const float dx = s[0] - s[3], dy = s[1] - s[4], dz = s[2] - s[5];
    const float r = 1.0f / std::sqrt((dz * dz + dy * dy) + dx * dx);
    const float dir[3] = {dx * r, dy * r, dz * r};
    vehicle_jolt(v, dir, turn, push);
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004accf0
void vehicle_jolt(BasicVehicle& v, const float dir[3], float turn, float push) {
  VehicleControls& c = *v.controls;
  const float* t = g_hooks->node_matrix(v.node);
  const float x = dir[0], y = dir[1], z = dir[2];
  const float lx = (t[2] * z + t[1] * y) + t[0] * x;  // 0x00522640: by the inverse rotation
  const float lz = (t[8] * z + t[7] * y) + t[6] * x;
  c.angles.x = c.angles.x - lz * turn;
  c.angles.z = lx * turn + c.angles.z;
  c.x70.x = c.x70.x - lx * push;
  c.x70.z = c.x70.z - lz * push;
}

// Mech3.exe 1.2 @ 0x004ad2d0
void vehicle_die(BasicVehicle& v) {
  if (!v.active) return;
  VehBasicHooks& h = *g_hooks;
  VehicleControls& c = *v.controls;
  v.active = false;
  v.engine.entry = nullptr;  // 0x004adb40
  c.health = 0.0f;
  c.state = 4;
  h.controls_died(c);
  h.path_drop_temporary(v);
  h.anim_start(c.motion.destroy_anim, v.node);
  if (c.beam_on) {
    c.beam_on = 0;
    h.beam_stop(c.first->beam);
  }
  h.glow_end(c.x398, c.node);
  vehicle_weapons_free(v);
  h.hit_killed();
}

// Mech3.exe 1.2 @ 0x004b48c0
void vehicle_weapons_free(BasicVehicle& v) {
  if (!v.ammo) return;
  g_hooks->weapons_settle(v.ammo);
  if (v.ammo) g_hooks->weapons_free(v.ammo);
  v.ammo = nullptr;
}

// Mech3.exe 1.2 @ 0x004af550
int32_t vehicle_hit(BasicVehicle& v, Handle weapon, uint32_t x, float amount) {
  switch (g_hooks->game_intensity()) {
    case 0: amount = amount + amount; break;
    case 2: amount = static_cast<float>(amount * 0.5); break;
    default: break;
  }
  return g_hooks->damage(v, weapon, x, amount);
}

// Mech3.exe 1.2 @ 0x004ad390
void vehicle_view_tint(BasicVehicle& v, uint8_t on) {
  static constexpr float kRed[3] = {1.0f, 0.0f, 0.0f};  // 0x005e8580
  if (on) {
    const VehicleControls& c = *v.controls;
    g_hooks->node_tint(v.node, kRed, (1.0f - c.health / c.health_max) * 0.6f);
  }
  g_hooks->node_tinted(v.node, on);
}

// Mech3.exe 1.2 @ 0x004a2ef0: line 1 what drives it (the controls' vt+0x20
// answers 0 in MW3, so the dynamics line never shows), line 2 where it is,
// lines 3 and 4 its zones and the camera's. Each line's text goes to the
// line's printf as its format, as written.
void vehicle_debug_lines(BasicVehicle* v) {
  if (!v) return;
  VehBasicHooks& h = *g_hooks;
  VehicleControls& c = *v->controls;
  static const char* const kModes[] = {"BASIC", "FLY", "SUB", "TRACK", "HOVER", "AMPHIB"};
  const uint32_t m = static_cast<uint32_t>(c.motion.mode);
  const char* mode = m <= 5 ? kModes[m] : "UNKNOWN";
  char buf[512] = "";
  const char* name = h.node_name(v->node);
  if (h.controls_dynamics(c) == 1) {
    std::snprintf(buf, sizeof buf, c.x20 ? "%s using %s dynamics - A" : "%s using %s dynamics", name, mode);
  } else if (h.controls_on_path(c)) {
    const int32_t goal = h.controls_goal(c);
    std::snprintf(buf, sizeof buf, "%s is in mode %d and had goal node %d", name, h.controls_mode(c), goal);
  } else if (h.controls_destroyed(c)) {
    std::snprintf(buf, sizeof buf, "%s is DEAD!", name);
  }
  h.hud_line(1, buf);
  const int32_t yaw = crt::msvc_ftol(static_cast<float>(c.angles.y * 57.29577951308));  // a 24-bit product
  const int32_t z = crt::msvc_ftol(h.entity_position(*v)[2]);
  const int32_t y = crt::msvc_ftol(h.entity_position(*v)[1]);
  const int32_t x = crt::msvc_ftol(h.entity_position(*v)[0]);
  char line[128];
  std::snprintf(line, sizeof line, "POS %d %d %d YAW %d", x, y, z, yaw);
  h.hud_line(2, line);
  auto zones = [&](const char* head, uint32_t count, const uint8_t* ids) {
    int n = std::snprintf(buf, sizeof buf, head, count);
    for (uint32_t i = 0; i < std::min<uint32_t>(count, 3) && n < static_cast<int>(sizeof buf); ++i)
      n += std::snprintf(buf + n, sizeof buf - static_cast<size_t>(n), "%d ", ids[i]);
  };
  zones("VEH ZONES(%d): ", v->x64, v->zones);  // a record holds 3 (the exe would read on)
  h.hud_line(3, buf);
  const uint8_t* cam = h.camera_zones();
  zones("CAM ZONES(%d): ", cam ? cam[0] : 0u, cam ? cam + 1 : nullptr);
  h.hud_line(4, buf);
}

// ---------------------------------------------------------------------------
// The gun's aim (2b1)

namespace {
void to_frame(const BasicVehicle& v, const float d[3], float out[3]) {  // 0x004a4060 / 0x004a40f0
  const float* m = v.transform;
  out[0] = (d[0] * m[0] + d[1] * m[1]) + d[2] * m[2];
  out[1] = (d[0] * m[3] + d[1] * m[4]) + d[2] * m[5];
  out[2] = (d[0] * m[6] + d[1] * m[7]) + d[2] * m[8];
}
}  // namespace

// Mech3.exe 1.2 @ 0x004ad740 (the exe reads through null without controls: none is)
bool vehicle_gun_within(const BasicVehicle& v, const float d[3]) { return v.controls->turret.within(d); }
// Mech3.exe 1.2 @ 0x004ad780
bool vehicle_gun_within_world(const BasicVehicle& v, const float d[3]) {
  float l[3];
  to_frame(v, d, l);
  return v.controls->turret.within(l);
}
// Mech3.exe 1.2 @ 0x004ad7c0
bool vehicle_gun_clamp(const BasicVehicle& v, const float d[3], float out[3]) { return v.controls->turret.clamp(d, out); }
// Mech3.exe 1.2 @ 0x004ad7f0
bool vehicle_gun_clamp_world(const BasicVehicle& v, const float d[3], float out[3]) {
  float l[3];
  to_frame(v, d, l);
  return v.controls->turret.clamp(l, out);
}
// Mech3.exe 1.2 @ 0x004ad820
float vehicle_health_share(const BasicVehicle& v) { return v.controls->health / v.controls->health_max; }
// Mech3.exe 1.2 @ 0x004ad6e0
bool vehicle_can_aim(BasicVehicle& v, float cos_limit) { return v.controls ? v.controls->can_aim(cos_limit) : false; }
// Mech3.exe 1.2 @ 0x004ad700
void vehicle_aim_at(BasicVehicle& v, const float dir[3], uint32_t unused) {
  (void)unused;
  if (v.controls) v.controls->aim_at(dir);
}

// ---------------------------------------------------------------------------
// The setup's points and zone (player.cpp)

// Mech3.exe 1.2 @ 0x0049f660
void vehicle_points_place(BasicVehicle& v) {
  VehBasicHooks& h = *g_hooks;
  VehicleControls& c = *v.controls;
  c.reset_1c();  // vt+0x18
  for (int32_t i = 0; i < c.point_count; ++i) {
    const float* p = h.entity_position(v);
    const Vec3c& m = c.motion.collision[i];
    c.points[i] = {m.x + p[0], m.y + p[1], m.z + p[2]};
  }
  vehicle_zone_from_ground(v);
}

// Mech3.exe 1.2 @ 0x0049f710
void vehicle_zone_from_ground(BasicVehicle& v) {
  VehBasicHooks& h = *g_hooks;
  const int32_t flag = h.node_flag8(v.node);
  h.node_set_flag8(v.node, 0);
  const float z = h.entity_position(v)[2];
  const float x = h.entity_position(v)[0];
  zcls::GroundHits hits{};
  const int32_t r = h.ground(x, 500.0f, z, &hits);
  h.node_set_flag8(v.node, flag);
  int32_t best = 0, in_water = 0;
  float surface = 0;
  ground_pick(hits, h.entity_position(v)[1], 4.0f, &best, 1, &in_water, &surface);
  if (r != 0) return;
  uint8_t rec[4];
  std::memcpy(rec, &hits.hit[best].zone[0], 4);
  const uint8_t* cam = h.camera_zones();
  int32_t zone = cam ? zmod::zone_common(cam, rec) : 0xff;
  if (zone == -1) zone = rec[1];
  v.x64 = rec[0];  // 0x005a80d0: the record into +0x64, the id onto the node (0x005a80c0)
  std::memcpy(v.zones, rec + 1, 3);
  h.node_set_zone(v.node, static_cast<uint8_t>(zone));
}

}  // namespace mech3

