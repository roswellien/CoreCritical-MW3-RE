#include "mech3/veh_controls.h"

#include "mech3/content_registry.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "crt/msvc_math.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zweapon/zweapon.h"
#include "gamez/zweapon/zwep_init.h"
#include "mech3/mech.h"
#include "mech3/weapon.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

float player_gravity();  // mech3/player.h (its Handle clashes with mech.h's): [0x00609a14]

namespace {
VehControlsHooks g_default_hooks;
VehControlsHooks* g_hooks = &g_default_hooks;
int32_t g_serial = 0;           // [0x0060b148]
const Vec3c kUnit5eda80{0, 0, 1};  // [0x005eda80..88]

zcls::Node* zn(void* h) { return static_cast<zcls::Node*>(h); }
Vehicle* veh(void* owner) { return static_cast<Vehicle*>(owner); }
}  // namespace

void set_veh_controls_hooks(VehControlsHooks* h) { g_hooks = h ? h : &g_default_hooks; }

const float* VehControlsHooks::node_world_matrix(Handle node) {
  return reinterpret_cast<const float*>(static_cast<const uint8_t*>(zn(node)->data) + 0x60);
}
void VehControlsHooks::node_add_rotation(Handle node, float x, float y, float z) {
  zcls::object3d_add_rotation(zn(node), x, y, z);
}
const float* VehControlsHooks::owner_matrix(void* owner) { return veh(owner)->transform; }
void VehControlsHooks::owner_x18(void* owner, float out[3]) {
  const Vehicle* v = veh(owner);
  out[0] = v->x18.x, out[1] = v->x18.y, out[2] = v->x18.z;
}
int32_t& vehicle_serial() { return g_serial; }

// --- the hooks' defaults: the ports -------------------------------------------
void VehControlsHooks::node_rotation(Handle node, float* x, float* y, float* z) {
  zcls::object3d_get_rotation(zn(node), x, y, z);
}
VehControlsHooks::Handle VehControlsHooks::node_find(Handle root, const char* name) {
  return zcls::node_find_in_tree(zn(root), name);
}
VehControlsHooks::Handle VehControlsHooks::node_child(Handle node, const char* name) {
  return zcls::node_find_in_tree(zn(node), name);
}
void VehControlsHooks::node_position(Handle node, Vec3c& out) {
  const float* m = zcls::object3d_matrix(zn(node));
  if (m) out = {m[9], m[10], m[11]};
}
void VehControlsHooks::node_translation(Handle node, float* x, float* y, float* z) {
  zcls::object3d_get_translation(zn(node), x, y, z);
}
VehControlsHooks::Handle VehControlsHooks::node_model(Handle node) { return zn(node)->model; }
void VehControlsHooks::node_set_model(Handle node, Handle model) {
  zcls::node_set_model(zn(node), static_cast<zmod::Model*>(model));
}
void VehControlsHooks::model_release(Handle model) { zmod::model_release(static_cast<zmod::Model*>(model)); }
void VehControlsHooks::node_remove_child(Handle parent, Handle child) { zcls::node_remove_child(zn(parent), zn(child)); }
void VehControlsHooks::node_free(Handle node) { zcls::tree_destroy(zn(node)); }
zrdr::Node* VehControlsHooks::reader_find(zrdr::Node* node, const char* key) { return zrdr::find(node, key); }
VehControlsHooks::Handle VehControlsHooks::anim_find(const char* name) { return zeff::anim_find(name); }
void VehControlsHooks::anim_start(Handle def, Handle node) {
  zeff::anim_start(static_cast<zeff::AnimDef*>(def), zn(node), 0, 0, 0);
}
void VehControlsHooks::weapon_slots(VehicleControls& c, void* owner, zrdr::Node* def) {
  vehicle_weapon_slots(c, owner, def);
}
VehControlsHooks::Handle VehControlsHooks::weapon_def(const char* name) { return zwep::find_def(name); }
const char* VehControlsHooks::weapon_def_name(Handle def) { return static_cast<zwep::WeaponDef*>(def)->name; }
uint32_t VehControlsHooks::weapon_def_flags(Handle def) { return static_cast<zwep::WeaponDef*>(def)->flags; }
void VehControlsHooks::node_set_active(Handle node, int32_t on) { zcls::node_set_active(zn(node), on); }
VehControlsHooks::Handle VehControlsHooks::beam_make(Handle def, Handle owner_node, Handle node, const Vec3c* muzzle,
                                                     const Vec3c* dir, int32_t segments) {
  (void)node;  // pushed, not read (as the mechs' call)
  return zwep::beam_make(static_cast<zwep::WeaponDef*>(def), owner_node, nullptr,
                         reinterpret_cast<const zwep::Vec3f3*>(muzzle), reinterpret_cast<const zwep::Vec3f3*>(dir),
                         segments);
}
bool VehControlsHooks::reader_float(zrdr::Node* node, const char* key, float* out) {
  return zrdr::get_float(node, key, out);
}
void VehControlsHooks::report(int32_t level, const char* file, int32_t line, const char* message) {
  zerr::report(level, file, line, message);
}
void VehControlsHooks::owner_weapons(void* owner, zrdr::Node* def) {
  Vehicle* v = veh(owner);
  v->ammo = build_vehicle_weapons(owner, static_cast<WeaponManager*>(v->ammo), def);
}

// Mech3.exe 1.2 @ 0x004a3fc0 (through 0x004a4a70, 0x004a4000).
TurretLimits::TurretLimits(float lo, float hi)
    : lo_sin(static_cast<float>(std::sin(static_cast<double>(lo)))),
      lo_cos(static_cast<float>(std::cos(static_cast<double>(lo)))),
      hi_sin(static_cast<float>(std::sin(static_cast<double>(hi)))),
      hi_cos(static_cast<float>(std::cos(static_cast<double>(hi)))) {}

// Mech3.exe 1.2 @ 0x004a4180 (the vehicles' 0x004a4a90): "not past" either
// sine, so a NaN is within.
bool TurretLimits::within(const float d[3]) const { return !(lo_sin > d[1]) && !(d[1] > hi_sin); }

// Mech3.exe 1.2 @ 0x004a41b0 (the vehicles' 0x004a4aa0). Unordered compares
// count as past the limit, the low one first.
bool TurretLimits::clamp(const float d[3], float out[3]) const {
  float s, co;
  if (!(d[1] >= lo_sin)) {
    s = lo_sin, co = lo_cos;
  } else if (!(hi_sin >= d[1])) {
    s = hi_sin, co = hi_cos;
  } else {
    out[0] = d[0], out[1] = d[1], out[2] = d[2];
    return true;
  }
  // FIX (bug): hypot(x, y) in the exe; the heading's length is hypot(x, z).
  const double len = zfix::options().bugs ? std::hypot(double{d[0]}, double{d[2]}) : std::hypot(double{d[0]}, double{d[1]});
  if (len == 0.0 || std::isnan(len)) {
    out[0] = 0.0f, out[1] = s, out[2] = -co;
    return false;
  }
  const float f = static_cast<float>(co / len);
  out[0] = f * d[0];
  out[1] = s;
  out[2] = d[2] * f;
  return false;
}

// Mech3.exe 1.2 @ 0x004ac750 (0x004ac740 on +0x764 -> 0x004a4af0: the
// turret limits of -pi/2 and pi/2).
VehicleControls::VehicleControls() : turret(-1.57079637f, 1.57079637f) {}

// Mech3.exe 1.2 @ 0x004ab990. The x87's fsin/fcos range check (|a| over
// 9.22e18 skips nothing here: both branches compute the same).
void euler_rotation(const float a[3], float out[9]) {
  const float sx = std::sin(a[0]), cx = std::cos(a[0]);
  const float sy = std::sin(a[1]), cy = std::cos(a[1]);
  const float sz = std::sin(a[2]), cz = std::cos(a[2]);
  out[0] = sy * sx * sz + cz * cy;
  out[1] = sz * cx;
  out[2] = sz * cy * sx - cz * sy;
  out[3] = sy * sx * cz - sz * cy;
  out[4] = cz * cx;
  out[5] = cz * cy * sx + sz * sy;
  out[6] = sy * cx;
  out[7] = -sx;
  out[8] = cy * cx;
}

// Mech3.exe 1.2 @ 0x004ac8a0: from the owner's node: a serial number, the
// node's rotation into the owner's matrix, the body, turret, shadow and gun
// nodes, the gun's firepoints; the health 100.
void VehicleControls::init() {
  VehControlsHooks& h = *g_hooks;
  Vehicle* v = veh(owner);
  if (!v || !v->node) return;
  float* m = v->transform;
  serial = g_serial++;
  h.node_rotation(v->node, &angles.x, &angles.y, &angles.z);
  angles0 = angles;
  x50 = Vec3c{};
  euler_rotation(&angles.x, m);
  up = {m[3], m[4], m[5]};
  h.owner_refresh(v);
  body = h.node_find(v->node, "body");
  turret_node = h.node_find(v->node, "turret");
  shadow = h.node_find(v->node, "shadow");
  gun = h.node_find(v->node, "gun");
  if (gun) h.node_position(gun, gun_pos);
  for (uint32_t& w : x94) w = 0;
  x5fc = kUnit5eda80;
  x760 = 0;
  x650 = 1;
  x5ec = 1;
  if (gun) {
    if (Handle c = h.node_child(gun, "fpnt_c")) {
      h.node_translation(c, &fpnt_c.x, &fpnt_c.y, &fpnt_c.z);
      Handle model = h.node_model(c);
      h.node_set_model(c, nullptr);
      h.model_release(model);
      fire_c_scale = 1.0f / std::sqrt(fpnt_c.x * fpnt_c.x + fpnt_c.y * fpnt_c.y + fpnt_c.z * fpnt_c.z);
    } else {
      muzzle = {0, 0, -1.0f};
      fire_c_scale = 1.0f;
    }
    if (Handle l = h.node_child(gun, "fpnt_l")) {
      h.node_translation(l, &fpnt_l.x, &fpnt_l.y, &fpnt_l.z);
      h.node_remove_child(gun, l);
      h.node_free(l);
    } else {
      fire_l = {1.51f, 0.396f, -2.46f};
    }
    if (Handle r = h.node_child(gun, "fpnt_r")) {
      h.node_translation(r, &fpnt_r.x, &fpnt_r.y, &fpnt_r.z);
      h.node_remove_child(gun, r);
      h.node_free(r);
    } else {
      fire_r = {-1.51f, 0.396f, -2.46f};
    }
  }
  frozen = 0, x2c = 0;
  health = health_max = 100.0f;
}

// Mech3.exe 1.2 @ 0x004acbc0 (vt+0x0c): the init (vt+0x10), the
// definition's start_anims started on the owner's node, its health, the
// owner's own 0x004af2a0 and weapons.
void VehicleControls::setup(zrdr::Node* def) {
  VehControlsHooks& h = *g_hooks;
  Vehicle* v = veh(owner);
  if (!v || !v->node) return;
  init();
  if (zrdr::Node* anims = h.reader_find(def, "start_anims")) {
    const zrdr::Node* list = anims->list;
    const int32_t n = list[0].i - 1;
    for (int32_t i = 0; i < n; ++i) {
      const std::string name = list[1 + i].s;  // copied to 80 bytes in the exe
      h.anim_start(h.anim_find(name.c_str()), v->node);
    }
  }
  if (zrdr::Node* hp = h.reader_find(def, "health")) health = health_max = hp->list[1].f;
  h.weapon_slots(*this, v, def);
  h.owner_weapons(v, def);
  node = v->node;
  x398.clear();
}

namespace {

const char kPlayerCpp[] = "C:\\zipper\\Mech3\\player.cpp";

// A key's list value: item k (1-based) as the float it holds.
float item_f(const zrdr::Node* n, int k) { return n->list[k].f; }

// Mech3.exe 1.2 @ 0x0049eed0: the model's support00..03, hidden; false at
// the first missing (those before it read).
bool supports_from_nodes(VehicleMotion& m, void* node) {
  VehControlsHooks& h = *g_hooks;
  for (int i = 0; i < 4; ++i) {
    char name[80];
    std::snprintf(name, sizeof name, "support%02d", i);
    void* s = h.node_find(node, name);
    if (!s) return false;
    h.node_translation(s, &m.support[i].x, &m.support[i].y, &m.support[i].z);
    h.node_set_active(s, 0);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x0049ecd0: the model's collide00..11, hidden, kept in
// the order 0-2, 6-8, 3-5, 9-11 once all twelve are there; false at the
// first missing (the block untouched).
bool collision_from_nodes(VehicleMotion& m, void* node) {
  VehControlsHooks& h = *g_hooks;
  Vec3c p[12];
  for (int i = 0; i < 12; ++i) {
    char name[80];
    std::snprintf(name, sizeof name, "collide%02d", i);
    void* s = h.node_find(node, name);
    if (!s) return false;
    h.node_translation(s, &p[i].x, &p[i].y, &p[i].z);
    h.node_set_active(s, 0);
  }
  static const int kOrder[12] = {0, 1, 2, 6, 7, 8, 3, 4, 5, 9, 10, 11};
  for (int i = 0; i < 12; ++i) m.collision[i] = p[kOrder[i]];
  return true;
}

// A key's (a, b) or a default pair.
void pair(zrdr::Node* mode, const char* key, float* a, float* b, float da, float db) {
  if (zrdr::Node* n = g_hooks->reader_find(mode, key)) {
    *a = item_f(n, 1), *b = item_f(n, 2);
  } else {
    *a = da, *b = db;
  }
}
float one(zrdr::Node* mode, const char* key, float d) {
  zrdr::Node* n = g_hooks->reader_find(mode, key);
  return n ? item_f(n, 1) : d;
}

}  // namespace

// Mech3.exe 1.2 @ 0x0049ef40
void vehicle_motion_read(VehicleControls& c, void* node, zrdr::Node* mode, const char* name, const char* type,
                         int32_t* collision_count) {
  VehControlsHooks& h = *g_hooks;
  VehicleMotion& m = c.motion;
  m.right_morphs = h.node_find(node, "right_morphs");
  m.left_morphs = h.node_find(node, "left_morphs");
  m.chassis = h.node_find(node, "chassis");
  m.rtracks = h.node_find(node, "rtracks");
  m.ltracks = h.node_find(node, "ltracks");
  m.x260 = 0;
  m.x258 = 0;
  m.support_count = 4;
  if (!supports_from_nodes(m, node)) {
    char msg[256];
    std::snprintf(msg, sizeof msg, "Cannot find support points for %s!", name);
    h.report(0x800, kPlayerCpp, 0x2d7, msg);
    // FIX (bug): without a platform list either, the exe reads through null
    // (every shipped vehicle has one); here no supports.
    zrdr::Node* platform = h.reader_find(mode, "platform");
    m.support_count = platform ? zrdr::list_count(platform) - 1 : 0;
    for (int32_t i = 0; i < m.support_count && i < 16; ++i) {
      const zrdr::Node* pt = platform->list[1 + i].list;
      m.support[i] = {pt[1].f, pt[2].f, pt[3].f};
      m.support_word[i] = 0;
    }
  }
  if (!collision_from_nodes(m, node)) {
    char msg[256];
    std::snprintf(msg, sizeof msg, "Cannot find collision points for %s!", name);
    h.report(0x800, kPlayerCpp, 0x2e6, msg);
    // FIX (bug): likewise without a collision list.
    zrdr::Node* col = h.reader_find(mode, "collision");
    *collision_count = col ? zrdr::list_count(col) - 1 : 0;
    for (int32_t i = 0; i < *collision_count && i < 12; ++i) {
      const zrdr::Node* pt = col->list[1 + i].list;
      m.collision[i] = {pt[1].f, pt[2].f, pt[3].f};
    }
  }
  pair(mode, "rates", &m.rates[0], &m.rates[1], 10.0f, 30.0f);
  if (zrdr::Node* n = h.reader_find(mode, "friction")) {
    m.friction[0] = item_f(n, 1), m.friction[1] = item_f(n, 2), m.friction[2] = item_f(n, 3);
  } else {
    m.friction[0] = 10000.0f, m.friction[1] = 10.0f, m.friction[2] = 0;
  }
  m.stopping = one(mode, "stopping", 8.0f);
  if (m.friction[0] < m.friction[1]) m.friction[1] = static_cast<float>(m.friction[0] * 0.9);
  m.quicksand_slowdown = one(mode, "quicksand_slowdown", 0.9f);
  m.lava_slowdown = one(mode, "lava_slowdown", 0.8f);
  pair(mode, "turns", &m.turns[0], &m.turns[1], 0.6f, 2.0f);
  m.turn_damping = one(mode, "turn_damping", 30.0f);
  pair(mode, "rate_damping", &m.rate_damping[0], &m.rate_damping[1], 30.0f, 30.0f);
  m.a_damping = one(mode, "a_damping", 8.0f);
  m.mass = one(mode, "mass", 1.0f);
  m.inv_mass = 1.0f / m.mass;
  pair(mode, "gun_pitch", &m.gun_lo, &m.gun_hi, zgame::from_bits(0xbe84816f), 0.5f);  // -sin(15 degrees)
  c.turret = TurretLimits(m.gun_lo, m.gun_hi);  // 0x004a4b30 with the full turn (-2 pi, 2 pi): 0x004a4a70
  if (zrdr::Node* n = h.reader_find(mode, "mode")) {
    const std::string s = n->list[1].s;  // copied to 20 bytes in the exe
    if (std::strncmp(s.c_str(), "basic", 2) == 0) m.mode = VehicleMotion::kBasic;
    else if (std::strncmp(s.c_str(), "track", 2) == 0) m.mode = VehicleMotion::kTrack;
    else if (std::strncmp(s.c_str(), "hover", 2) == 0) m.mode = VehicleMotion::kHover;
    else if (std::strncmp(s.c_str(), "amphib", 2) == 0) m.mode = VehicleMotion::kAmphib;
    else if (std::strncmp(s.c_str(), "sub", 2) == 0) m.mode = VehicleMotion::kSub;
    else if (std::strncmp(s.c_str(), "fly", 2) == 0) m.mode = VehicleMotion::kFly;
    // anything else: the mode as it was
  } else {
    m.mode = VehicleMotion::kTrack;
  }
  for (const char* key : {"amphib_wave", "hover_wave", "sub_wave"})
    if (zrdr::Node* n = h.reader_find(mode, key))
      for (int k = 0; k < 7; ++k) m.wave[k] = item_f(n, 1 + k);
  if (!h.reader_float(mode, "mode_alt", &m.mode_alt)) m.mode_alt = 2.0f;
  {
    zrdr::Node* n = h.reader_find(mode, "chas_smooth");
    m.chas_smooth = n ? std::fabs(item_f(n, 1)) : 0;
  }
  if (zrdr::Node* n = h.reader_find(mode, "chas_pitch")) {
    m.chas_pitch = {item_f(n, 1), item_f(n, 2), std::fabs(item_f(n, 3))};
  } else {
    m.chas_pitch = Vec3c{};
  }
  if (zrdr::Node* n = h.reader_find(mode, "chas_roll")) {
    m.chas_roll = {item_f(n, 1), item_f(n, 2), std::fabs(item_f(n, 3))};
  } else if (zfix::options().bugs) {
    m.chas_roll = Vec3c{};  // FIX (bug): the exe zeroes chas_pitch's here (a copy's slip; no shipped vehicle has either key)
  } else {
    m.chas_pitch = Vec3c{};
  }
  pair(mode, "collision_d", &m.collision_d[0], &m.collision_d[1], 0.5f, 0.15f);
  m.destroy_anim = h.anim_find(name);
  if (!m.destroy_anim) m.destroy_anim = h.anim_find(content_vehicle_model(type));
  if (!m.destroy_anim) {
    char msg[256];
    std::snprintf(msg, sizeof msg, "No destroy animation for (%s) or (%s)", name, type);
    h.report(0x200, kPlayerCpp, 0x3ce, msg);
  }
}

// Mech3.exe 1.2 @ 0x004af2a0
void vehicle_weapon_slots(VehicleControls& c, void* owner, zrdr::Node* def) {
  VehControlsHooks& h = *g_hooks;
  for (int i = 0; i < 10; ++i) {
    WeaponSlot& s = c.slots[i];
    s.index = i;
    s.flags &= ~4u;
    s.x18 = 0;
    s.node = nullptr;
    s.beam = nullptr;
  }
  const int32_t segments = c.serial == 1 ? 8 : 1;
  if (zrdr::Node* weapons = h.reader_find(def, "weapons")) {
    const zrdr::Node* list = weapons->list;
    const int32_t n = list[0].i - 1;
    for (int32_t i = 0; i < n; ++i) {
      const zrdr::Node* item = list[1 + i].list;
      const std::string name = item[1].s;  // copied to 20 bytes in the exe
      WeaponSlot& s = c.slots[i];
      s.def = h.weapon_def(name.c_str());
      s.flags = (s.flags & ~2u) | ((static_cast<uint32_t>(item[3].i) & 1) << 1);
      if (item[2].i) s.flags = (s.flags & ~4u) | (s.def ? 4u : 0u);
      s.x18 = (s.flags & 4) ? static_cast<float>(item[4].i) : 0.0f;
      s.x24 = 0;
      s.x28 = static_cast<uint32_t>(item[5].i);
      s.x2c = static_cast<uint32_t>(item[6].i);
      s.x30 = static_cast<uint32_t>(item[7].i);
      s.flags = (s.flags & ~1u) | (static_cast<uint32_t>(item[8].i) & 1);
      if (i == 0) {
        c.first = &s;
        c.x5ec = 0;
      }
      // FIX (bug): a weapon zWeapon does not have leaves no definition, and
      // the exe reads its name and flags through null (no shipped vehicle
      // names one; a modded vehicle.zrd would crash). Here the slot stays
      // without nodes or a beam.
      if (!s.def) continue;
      if (c.gun) {
        const char* wname = content_weapon_model_name(h.weapon_def_name(s.def));
        if (s.flags & 2) {
          char buf[80];
          std::snprintf(buf, sizeof buf, "%s_L", wname);
          s.node = h.node_child(c.gun, buf);
          std::snprintf(buf, sizeof buf, "%s_R", wname);
          s.node_r = h.node_child(c.gun, buf);
          if (s.node) h.node_set_active(s.node, 0);
          if (s.node_r) h.node_set_active(s.node_r, 0);
        } else {
          s.node = h.node_child(c.gun, wname);
          if (s.node) h.node_set_active(s.node, 0);
        }
      }
      if (h.weapon_def_flags(s.def) & 2)
        s.beam = h.beam_make(s.def, veh(owner)->node, s.node, &c.muzzle, &c.aim_dir, segments);
    }
  }
  if (c.first && c.first->node) {
    h.node_set_active(c.first->node, 1);
    if (c.first->node_r) h.node_set_active(c.first->node_r, 1);
  }
}

// Mech3.exe 1.2 @ 0x004ad640 (IControls vt+0x14 through the thunk
// 0x004e7690): SetThrottle(0) and SetTurn(0) (0x004a5b70, 0x004a5bb0); the
// jump jets' SetJump, +0x3c and +0x44 are empty for vehicles (0x004a5bc0).
void VehicleControls::clear_commands() {
  throttle = 0;
  turn = 0;
}

// Mech3.exe 1.2 @ 0x004accd0
void VehicleControls::reset_1c() {
  x30 = 0;
  x350 = 0;
  x3c = 0;
  x1c = player_gravity();
  x5e8 = 0;
}

// ---------------------------------------------------------------------------
// The turret's aim (2b1)

// Mech3.exe 1.2 @ 0x004020d0
float vec_normalise(float v[3]) {
  const float len = std::sqrt((v[0] * v[0] + v[2] * v[2]) + v[1] * v[1]);
  if (std::fabs(len) != 0.0f) {
    const float r = 1.0f / len;
    v[0] = r * v[0], v[1] = r * v[1], v[2] = r * v[2];
  }
  return len;
}

namespace {
// Into the turret's frame: (row . v) for each row of its world 3x3, x first.
void by_rows(const float* m, const float v[3], float out[3]) {
  out[0] = (v[0] * m[0] + v[1] * m[1]) + v[2] * m[2];
  out[1] = (v[0] * m[3] + v[1] * m[4]) + v[2] * m[5];
  out[2] = (v[0] * m[6] + v[1] * m[7]) + v[2] * m[8];
}
}  // namespace

// Mech3.exe 1.2 @ 0x004e7570: a direction in the vehicle's frame (the track
// turns it so first, 0x004b6ef3) into the world by the owner's matrix.
void turret_aim_at(VehicleControls& c, TurretAim& a, const float dir[3]) {
  a.dir[0] = dir[0], a.dir[1] = dir[1], a.dir[2] = dir[2];
  a.set = 1;
  const float* m = g_hooks->owner_matrix(c.owner);
  const float x = a.dir[0], y = a.dir[1], z = a.dir[2];
  a.dir[2] = (x * m[2] + y * m[5]) + z * m[8];
  a.dir[1] = (x * m[1] + y * m[4]) + z * m[7];
  a.dir[0] = (x * m[0] + y * m[3]) + z * m[6];
}

// Mech3.exe 1.2 @ 0x004e7490
bool turret_can_aim(const VehicleControls& c, const TurretAim& a, float cos_limit) {
  if (!c.turret_node) return true;
  float l[3];
  by_rows(g_hooks->node_world_matrix(c.turret_node), a.dir, l);
  l[1] = 0.0f;
  const float len = vec_normalise(l);
  if (len == 0.0f || std::isnan(len)) return false;
  return -l[2] > cos_limit;
}

// Mech3.exe 1.2 @ 0x004e72f0. The rate, -ln(0.5) / 0.125, made once ([0x0060b880]).
int32_t turret_turn(VehicleControls& c, const TurretAim& a, float dt) {
  static const float k = static_cast<float>(-(std::log(0.5) / 0.125));
  if (!c.turret_node) return 1;
  float aim[3];
  if (a.set) aim[0] = a.dir[0], aim[1] = a.dir[1], aim[2] = a.dir[2];
  else g_hooks->owner_x18(c.owner, aim);
  float l[3];
  by_rows(g_hooks->node_world_matrix(c.turret_node), aim, l);
  l[1] = 0.0f;
  const float len = vec_normalise(l);
  if (len == 0.0f || std::isnan(len)) return 1;
  const float angle = static_cast<float>(std::atan2(-double{l[0]}, -double{l[2]}));
  if (!(std::fabs(angle) > 0.001)) return 1;
  // 2^x as float bits: (int)(x * 2^23 / ln 2 ...) + 1.0f's bits (the exe's 12102200).
  const float t = -(k * dt) * 12102200.0f;
  const uint32_t bits = static_cast<uint32_t>(crt::msvc_ftol(t)) + 0x3f800000u;
  float f;
  std::memcpy(&f, &bits, 4);
  const float turn = (1.0f - f) * angle;
  g_hooks->node_add_rotation(c.turret_node, 0.0f, turn, 0.0f);
  return 1;
}

}  // namespace mech3

