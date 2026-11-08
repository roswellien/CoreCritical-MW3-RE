#include "mech3/mech_build.h"

#include "gamez/zentity/zent_init.h"
#include "mech3/mech_parts.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zinput/zinput.h"
#include "mech3/mech_anim.h"
#include "mech3/mech_config.h"
#include "mech3/mech_control.h"
#include "mech3/mech_data.h"
#include "mech3/mech_move.h"
#include "mech3/mech_names.h"
#include "mech3/content_registry.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_skin.h"
#include "mech3/mech_skis.h"
#include "mech3/weapon.h"

// C:\zipper\Mech3\mech.cpp: a mech built (docs/mech-build.md). The order
// of every step, field and call is the original's: the tests compare the
// built objects and the calls with the exe's.
namespace mech3 {

int32_t torso_auto_return();  // 0x0040d010 (mech3/options.h, whose Handle clashes with mech.h's)
void player_clear_60b56c();   // mech3/player.h, likewise

namespace {

MechBuildHooks g_default_hooks;
MechBuildHooks* g_hooks = &g_default_hooks;
MechBuildState g_state;

zcls::Node* zn(Handle h) { return static_cast<zcls::Node*>(h); }

const Vec3 kZero{};                       // 0x0063d870
const float kIdentity[4] = {1, 0, 0, 0};  // 0x005eda50

template <class T>
T* made(uint32_t size, T* p) {
  g_hooks->on_new(size, p);
  return p;
}

}  // namespace

void set_mech_build_hooks(MechBuildHooks* h) { g_hooks = h ? h : &g_default_hooks; }
MechBuildHooks& mech_build_hooks() { return *g_hooks; }
MechBuildState& mech_build_state() { return g_state; }
void MechBuildHooks::node_content_chassis(Handle root, const char* id) {
  if (!root) return;
  std::vector<zcls::Node*> pending{zn(root)};
  for (size_t i = 0; i < pending.size(); ++i) {
    zcls::Node* node = pending[i];
    node->content_chassis_id = id ? id : "";
    for (int32_t j = 0; j < node->child_count(); ++j) pending.push_back(node->children[j]);
  }
}

// --- the hooks' defaults: the ports, or zClass's nodes ------------------------
const MechDfn* MechBuildHooks::dfn(int32_t roster) { return mech_dfn(roster); }

int32_t MechBuildHooks::node_box(Handle node, float box[6]) {
  zcls::Node* n = zn(node);
  if (!n) return 5;
  std::memcpy(box, n->raw + 0x74, 6 * sizeof(float));
  return 0;
}
void MechBuildHooks::node_get_translation(Handle node, float* x, float* y, float* z) {
  zcls::object3d_get_translation(zn(node), x, y, z);
}
void MechBuildHooks::model_add_polygon(Handle model, int32_t n, const float* v) {
  uint32_t a[12] = {};  // four vertices: what the build makes
  for (int i = 0; i < 12 && i < 3 * n; ++i) a[i] = zgame::bits(v[i]);
  unported("0x004776d0", {low(model), n, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]});
}
const char* MechBuildHooks::node_name(Handle node) { return node ? zn(node)->name() : ""; }
int32_t MechBuildHooks::node_class(Handle node) { return node ? zn(node)->cls() : 0; }
Handle MechBuildHooks::node_data(Handle node) { return node ? zn(node)->data : nullptr; }
Handle MechBuildHooks::node_model(Handle node) { return node ? zn(node)->model : nullptr; }
int32_t MechBuildHooks::node_child_count(Handle node) { return node ? zn(node)->child_count() : 0; }
Handle MechBuildHooks::node_child(Handle node, int32_t i) {
  return node && zn(node)->children ? zn(node)->children[i] : nullptr;
}
int32_t MechBuildHooks::node_parent_count(Handle node) { return node ? zn(node)->parent_count() : 0; }
Handle MechBuildHooks::node_parent(Handle node, int32_t i) {
  return node && zn(node)->parents ? zn(node)->parents[i] : nullptr;
}
float MechBuildHooks::node_min_y(Handle node) { return node ? zn(node)->get<float>(0x78) : 0.0f; }
int32_t MechBuildHooks::node_id(Handle node) { return node ? zn(node)->get<int32_t>(0x28) : -1; }
void MechBuildHooks::node_set_owner(Handle node, int32_t id, void* owner) {
  if (!node) return;
  zn(node)->put<int32_t>(0x28, id);
  zn(node)->owner = owner;
}
void* MechBuildHooks::node_owner(Handle node) {
  if (!node || !(zn(node)->get<int32_t>(0x28) & 0xff)) return nullptr;
  return zn(node)->owner;
}

// 0x004d6c90 on a mech: zEntity's node (0x005a8390: its matrix, a centroid
// and a target node found or made under it) and the node's entity (0x0040eaa0:
// bits 0x1000 and the owner, over the tree), which the mech's set-up gives it
// too (0x00415ae0, mech_build's node_set_owner): the stand-in keeps the node,
// its matrix and the two markers (zent::marker_nodes). 0x005a8390 names the
// node too (0x005a84c0: the AI record's name, else "<model>NN"), by which the
// objectives and the valves find the mech (t1m2's aim_at_object bushwacker_02).
void MechBuildHooks::entity_set_node(Mech& m, Handle node, const char* name) {
  vehicle_set_node(m, node);
  if (node && name) node_set_name(node, name);
}

void vehicle_set_node(Vehicle& m, Handle node) {
  m.node = node;
  m.transform = node ? zcls::object3d_matrix(zn(node)) : nullptr;
  zcls::Node *centroid = nullptr, *target = nullptr;
  zent::marker_nodes(zn(node), nullptr, nullptr, &centroid, &target);  // 0x004d6c90 passes 0, 0
  m.centroid = centroid ? centroid : node;
  m.target_node = target ? target : node;
}

void MechBuildHooks::controls_set_owner(MechControls* c, Mech& m) { c->owner = &m; }

// The mech's own skin (+0x3e8): the definition's first, copied for it and put
// on its model (0x00475a80), and a skin chosen later swapped in (0x004757f0).
Handle MechBuildHooks::skins_decals(Handle skins, Handle node) {
  return static_cast<SkinSet*>(skins)->mech_skin(0, node);
}
uint8_t MechBuildHooks::skins_apply(Handle skins, Mech& m, Handle skin) {
  return static_cast<SkinSet*>(skins)->set_mech_skin(&m.decals, m.model_node, m.lod_node, static_cast<Skin*>(skin))
             ? 1
             : 0;
}

MechAnim* MechBuildHooks::gait_copy(MechData& p, int32_t i) { return mech3::gait_copy(p, i); }
MechAction* MechBuildHooks::action_copy(MechData& p, int32_t i) { return mech3::action_copy(p, i); }
MechAnim* MechBuildHooks::make_blend(MechAnim* a, MechAnim* b) { return mech3::make_blend(a, b); }
void MechBuildHooks::part_setup(MechPart& p) { p.setup(); }
void MechBuildHooks::part_init(MechPart& p, Config* cfg) { p.init(cfg); }
void MechBuildHooks::collider_setup(Mech& m) { m.setup_collider(); }
void MechBuildHooks::ski_set(Skis& s, int32_t side) { s.set_ski(side, 0.0f, 0.0f); }
bool MechBuildHooks::config_valid(Config* cfg) { return cfg->validate(false); }
void MechBuildHooks::config_ungroup_equipment(Config* cfg) {
  // Mech3.exe 1.2 @ 0x00425960 (mech_config.cpp): every equipment item's +0x08 cleared.
  for (ConfigItem* it : cfg->items)
    if (it->kind() == 3) it->group = 0;
}
void MechBuildHooks::configure_weapons(Mech& m, Config* cfg) {
  mech3::configure_weapons(&m, static_cast<WeaponManager*>(m.ammo), cfg, &m.config, m.equipment, m.ams, m.node);
}
void MechBuildHooks::weapons_select(WeaponManager* wm, int32_t group) { wm->select_group(group); }

// --- mech.cpp --------------------------------------------------------------

// Mech3.exe 1.2 @ 0x00414150: an AI mech's, from the roster index and the
// configuration's index (the first configuration when that one is missing).
Mech* mech_create(int32_t index, MechControls* ctl, int32_t config, const char* name) {
  if (!ctl) return nullptr;
  MechData* data = g_hooks->mech_data(index, 1);
  if (!data) return nullptr;
  Config* cfg = mech_data_config(*data, config);
  if (!cfg) cfg = mech_data_config(*data, 0);
  return mech_new(data, ctl, cfg, name);
}

// Mech3.exe 1.2 @ 0x004140c0
namespace {
int32_t g_alive = 0;  // ours: the mechs made and not yet deleted
}  // namespace
int32_t mechs_alive() { return g_alive; }

Mech* mech_new(MechData* data, MechControls* ctl, Config* cfg, const char* name) {
  if (!cfg || !data || !ctl) return nullptr;
  Mech* m = made(0x6d8, new Mech());
  ++g_alive;
  mech_construct(*m, data, ctl, cfg, name);
  return m;
}

// Mech3.exe 1.2 @ 0x004148c0: the mech made again in its own memory as
// `roster` (-1 its own): its loadout when the roster is its own, else the
// type's first; its controls taken first (left off it when nothing is
// made); the destructor (vt+0 with 0), then the constructor with `name`
// (its vt+0xa4, the caller's); its old skin again by name; the HUD's fall
// mode 0 (0x0041ca30, inline). Nothing without the type's data.
void mech_rebuild(Mech& m, int32_t roster, const char* name) {
  const int32_t own = m.pilot ? m.pilot->roster : -2;
  const bool same = roster == own || roster == -1;
  Config* cfg = same ? m.config : nullptr;
  MechData* data = g_hooks->mech_data(same ? own : roster, 1);  // 0x00430d10
  if (!data) return;
  MechControls* ctl = m.controls;
  m.controls = nullptr;
  char nm[64] = {}, skin[64] = {};
  if (name) std::snprintf(nm, sizeof nm, "%s", name);
  if (const auto* old = static_cast<const Skin*>(m.decals); old && old->name) std::snprintf(skin, sizeof skin, "%s", old->name);
  if (!cfg) cfg = mech_data_config(*data, 0);  // 0x00432950
  if (ctl && cfg) {
    m.release();
    m.~Mech();
    new (&m) Mech();
    mech_construct(m, data, ctl, cfg, nm);
  }
  const MechDfn* dfn = g_hooks->dfn(m.pilot ? m.pilot->roster : roster);  // 0x004846a0
  mech_set_skin(m, dfn && dfn->skins ? dfn->skins->named(skin) : nullptr);  // 0x00475b00, 0x00414390
  m.hud_fall = &m == world().mission_player ? 0 : 1;  // 0x0041ca30(0)
}

// Mech3.exe 1.2 @ 0x00414f20 (vtable +0, 1)
void mech_delete(Mech* m) {
  if (!m) return;
  m->release();
  delete m;
  --g_alive;
}

// Mech3.exe 1.2 @ 0x004152d0, the destructor, with its body 0x00414f40 and
// the bases' (0x004aeb50). What the mech made goes, in the exe's order: the
// flown parts, the decals, the cockpit, the view, the contacts, the sounds,
// the node's handlers, the weapons, the parts, the skis, the AMS, the
// motions, the type's data (when no other mech holds it), the controls,
// the node and its models, zEntity's entry, and the shadow.
void Mech::release() {
  FreeCalls& f = free_calls();
  if (this == world().mission_player && f.player_object_free) f.player_object_free(*this);  // 0x0041e580
  for (Handle d : x6d0)  // 0x00413db0
    if (d && f.debris_free) f.debris_free(d);
  if (decals) {
    if (f.decals_free) f.decals_free(decals);
    decals = nullptr;
  }
  if (f.torso_cockpit_off) f.torso_cockpit_off(*this);  // 0x0041ce10(0)
  if (f.viewed && f.viewed(*this) && f.view_detach) f.view_detach();  // 0x0040adf0(0, 0)
  clear_contacts();  // 0x004a7be0, +0x94 with them (the exe's later delete of +0x94 frees null)
  for (Sound& s : sounds) {
    if (s && heat_calls().stop_sound) heat_calls().stop_sound(s);  // 0x0056e9f0
    s = nullptr;
  }
  for (int i = 0; i < 30; ++i)  // 0x0041e7a0(i, 0)
    if (update_calls().stop_cockpit_sound) update_calls().stop_cockpit_sound(i);
  if (node && f.handlers_off) f.handlers_off(*this);
  delete static_cast<WeaponManager*>(ammo);  // 0x004b01d0
  ammo = nullptr;
  part_delete(hip), hip = nullptr;
  part_delete(torso), torso = nullptr;
  part_delete(legs[0]), legs[0] = nullptr;
  part_delete(legs[1]), legs[1] = nullptr;
  part_delete(arms[0]), arms[0] = nullptr;
  part_delete(arms[1]), arms[1] = nullptr;
  delete skis, skis = nullptr;  // 0x00476460
  part_delete(head), head = nullptr;
  delete ams, ams = nullptr;    // 0x005ac5c0
  for (MechAnim*& a : anims) delete a, a = nullptr;             // 0x004152c0 -> 0x005a9dc0
  for (MechAction*& a : action_objects) delete a, a = nullptr;  // +0x458, nine in the exe
  if (pilot && --pilot->refs <= 0) {  // 0x00430e30
    pilot->refs = 0;
    mech_data_free(pilot);  // 0x00430580 and the delete
  }
  pilot = nullptr;
  parts.clear();
  if (x68c && f.ref_delete) f.ref_delete(x68c);
  x68c = nullptr;
  if (controls && f.controls_free) f.controls_free(controls);  // when its vt+0x18 says the mech owns it
  controls = nullptr;
  if (node) {
    if (f.node_free) f.node_free(*this);
    if (f.entity_free) f.entity_free(*this);  // vt+0x04(0, 0, 0, 0), 0x005a7f70
    node = nullptr;
  }
  release_shadow();  // the bases' 0x004d58a0
  x6d0.clear(), net_shots.clear(), actions.clear();
}

// Mech3.exe 1.2 @ 0x004aeb00 (veh_basic.cpp's neighbour): the vehicle over
// AiTarget 0x004d56e0(0, 0), ZEntity3d 0x005a7e10(0), ZEntity 0x005a8310(0).
void vehicle_base(Vehicle& m) {
  m.active = true, m.x09 = true;  // ZEntity
  m.node = nullptr, m.target_node = nullptr, m.centroid = nullptr;
  m.x18 = kZero, m.forward = kZero, m.velocity = kZero, m.vec_3c = kZero;  // ZEntity3d
  std::memcpy(m.orientation, kIdentity, sizeof m.orientation);
  m.vec_58 = kZero;
  m.x64 = 0;
  m.height = 10.0f;  // AiTarget
  m.target = m.ally_target = m.enemy_target = m.saved_target = nullptr;
  m.own_lance = m.nearest_lance = nullptr;
  m.x84 = 0;
  m.tracked[0] = m.tracked[1] = nullptr;
  g_hooks->on_new(8, m.tracked);
  m.own_player = m.player_contact = nullptr;
  m.sensor_range = m.range = 1000.0f;  // 0x005e8d84
  m.range2 = m.range * m.range;
  m.xa8 = 0;
  m.contacts.clear();
  m.touching = nullptr;
  m.xbc &= 0xf0;
  m.team = 1, m.allies = 0, m.enemies = 0xfffffffe;  // team 0
  player_clear_60b56c();  // [0x0060b56c]
  m.shadow = nullptr, m.net = 0;
  m.ammo = nullptr, m.net_collision = nullptr;  // Vehicle
}

namespace {

// Mech3.exe 1.2 @ 0x004222e0, on +0xfc.
void collider_init(Mech& m) {
  Collider& c = m.collider;
  for (bool& b : m.blocked) b = false;
  c.x370 = 0;
  c.clear = 0;
  c.count = 11;
  c.restitution = 0.1f;
  c.yaw_kick = 0.01f;
  for (int i = 0; i < 15; ++i) c.local[i] = c.world[i] = c.last[i] = kZero;
  static const float kMatrix[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};  // 0x004abaf0 with the translation
  std::memcpy(c.matrix, kMatrix, sizeof kMatrix);
  c.vehicles.clear(), c.mechs.clear(), c.ceilings.clear(), c.walls.clear(), c.touches.clear();
}

}  // namespace

// Mech3.exe 1.2 @ 0x004149f0. `data` is the mech type (+0x684), `cfg` its
// loadout, `name` the entity's (null: <mech><nn>).
void mech_construct(Mech& m, MechData* data, MechControls* ctl, Config* cfg, const char* name) {
  MechBuildHooks& h = *g_hooks;
  vehicle_base(m);
  m.power_down_pending = m.heat_shutdown = m.powered_down = false;
  m.xe8 = nullptr;
  collider_init(m);
  h.on_new(0xc, &m.parts);
  m.decals = nullptr;
  m.config = cfg;
  for (uint32_t& v : m.x3f4) v = 0;
  m.x40c = 1.0f;
  m.damaged = true;
  m.killed_by_nuke = false;
  h.on_new(0xc, &m.actions);
  m.walk_speed = 0, m.x514 = 0, m.max_speed = 0;  // three vectors from 0x0063d870
  m.x51c = 0, m.turn_rate = 0, m.x524 = 0;
  m.speed_factor = m.speed_target = 1.0f;
  m.lean = 0;
  m.x550 = 1.0f;
  m.heat = m.heat_added = 0;
  m.jet_burn = 0;
  m.jet_fuel = 1.0f;
  m.infinite_fuel = false;
  m.narc_time = 0;
  m.masc_time = 0;
  m.anim_state = 1, m.prev_state = 1;
  m.knee_back = m.crouched = false;
  m.hud_saved = 1;
  m.wading = false;
  m.gait_frames = 0;
  m.lie_time = 0;
  m.impulse = kZero, m.push = kZero;
  m.collides = true;
  m.heat_sinks.tech = 3, m.heat_sinks.count = 0, m.heat_sinks.doubles = false;
  h.on_new(0xc, &m.net_shots);
  m.pilot = data;
  m.x68c = nullptr;
  m.frame_dt = 0;
  m.dead_time = 0;
  h.on_new(0xc, &m.x6d0);
  // The knockdown: 0.47, 0.376, 15.04, and 0.376 x 10, x 30 (docs/damage.md).
  m.knockdown[0] = 0.47f, m.knockdown[1] = 0.376000017f, m.knockdown[2] = 15.04f;  // 0x3ec08313: not 0.376f
  m.knockdown[3] = 3.76f, m.knockdown[4] = 11.28f;
  (void)m.tonnage();  // 0x00416770 compared with 10 and asked again: nothing is kept
  m.hip = nullptr, m.torso = nullptr;
  m.chat = nullptr;
  m.legs[1] = m.legs[0] = nullptr;
  m.arms[1] = m.arms[0] = nullptr;
  m.jet_node = nullptr;
  m.attacker = nullptr;
  m.limp = -1;
  m.xf8 = -1;
  m.ams = nullptr;
  m.detail = static_cast<uint32_t>(-2);  // 0x0041e9e0
  m.model_node = m.lod_node = nullptr;
  m.x690 = m.net_damaged = 0;
  m.alive_time = 0;
  m.anim_index = 2;
  for (MechAnim*& a : m.anims) a = nullptr;
  for (int i = 0; i < 9; ++i) m.action_objects[i] = nullptr;
  for (Sound& s : m.sounds) s = nullptr;
  m.x61c = 0, m.jet_effect = nullptr, m.x624 = m.x628 = m.x62c = 0;
  m.controls = ctl;
  h.controls_set_owner(ctl, m);
  auto* wm = new WeaponManager(&m);  // 0x004b00d0: its five groups
  h.on_new(0x94, wm);
  for (WeaponGroup* g : wm->groups) h.on_new(0x54, g);
  m.ammo = wm;
  m.x63c = 1.0f;
  m.coolant = 1.0f;
  m.heat = 0;
  m.hud_fall = 1;
  m.x634 = 0;
  m.heat_added = 0;
  m.heat_dissipation = 0.02f;
  m.invincible = false;
  m.no_power_down = false;
  m.jump_jets_firing = 0;
  m.skis = nullptr;
  for (int32_t& e : m.equipment) e = 0;
  m.x640 = kZero;
  m.heat_state = 0;
  m.shutdown_time = m.beep_time = m.voice_time = 0;
  m.last_hit_time = -1.0f;
  m.last_hit_part = nullptr;
  m.x6ac = 0;
  m.heat_sinks.tech = mech_tech(data->roster);  // 0x00484720
  m.heat_sinks.count = 10;
  m.heat_sinks.doubles = false;
  if (!mech_model(m, name)) h.message_box(0xf25, name);
  const Handle mem = h.alloc(0x24);
  m.x68c = mem ? h.x68c_init(mem, m) : nullptr;
  m.knock = 0;
  m.knock_vec = kZero;
}

// Mech3.exe 1.2 @ 0x00415b00: the mech's copy of its type's model, named,
// with its parts; the walk ends with no node's action set.
bool mech_model(Mech& m, const char* name) {
  MechBuildHooks& h = *g_hooks;
  const Handle node = mech_data_model(*m.pilot, &m.decals);
  if (!node || !m.config) {
    h.report(0x400, 0x3d3, "Failed to copy Mech Database Tree");
    return false;
  }
  m.model_node = h.node_child(node, 0);
  m.lod_node = mech_data_lod(*m.pilot, 1);
  ++g_state.made;
  char text[256];
  if (name) std::snprintf(text, sizeof text, "%s", name);
  else std::snprintf(text, sizeof text, "%s%02d", m.pilot->name, g_state.made);
  // 0x0041de80: the node set with the x68c object off its list meanwhile.
  if (m.x68c) h.x68c_unlink(m.x68c);
  h.entity_set_node(m, node, text);
  m.xe8 = m.target_node;  // 0x005a8690
  if (m.x68c) h.x68c_unlink(m.x68c);
  h.entity_register(m);  // 0x005a7f50: into zEntity's list (0x00736558)
  h.node_set_owner(m.node, 0x1000, &m);  // 0x00415ae0
  const bool ok = mech_parts(m);
  for (Sound& s : m.sounds) s = nullptr;
  // 0x00415c70: every node of the tree loses its action.
  struct Walk {
    static void run(MechBuildHooks& hk, Handle n) {
      if (!n) return;
      hk.node_set_action(n, 0);
      for (int32_t i = 0; i < hk.node_child_count(n); ++i) run(hk, hk.node_child(n, i));
    }
  };
  Walk::run(h, m.node);
  return ok;
}

// Mech3.exe 1.2 @ 0x00415e40: the parts from the definition's slots, the
// AMS, the firepoints, the box's height, the motions, the collider, the
// configuration, the jump jets' node ("butt").
bool mech_parts(Mech& m) {
  MechBuildHooks& h = *g_hooks;
  const MechDfn& dfn = *h.dfn(m.pilot->roster);
  m.knee_back = dfn.get<uint8_t>(0x18) != 0;
  m.hip = make_hip(m, dfn.parts[kHip]);
  m.torso = make_torso(m, dfn.parts[kTorso]);
  m.head = make_head(m, dfn.parts[kHead]);
  m.arms[0] = make_arm(m, dfn.parts[kLeftArm], dfn.parts[kLeftHand]);
  m.arms[1] = make_arm(m, dfn.parts[kRightArm], dfn.parts[kRightHand]);
  m.legs[0] = make_leg(m, dfn.parts[kLeftThigh], dfn.parts[kLeftCalf], dfn.parts[kLeftFoot]);
  m.legs[1] = make_leg(m, dfn.parts[kRightThigh], dfn.parts[kRightCalf], dfn.parts[kRightFoot]);
  h.part_setup(*m.torso);
  h.part_setup(*m.hip);
  h.part_setup(*m.arms[0]);
  h.part_setup(*m.arms[1]);
  h.part_setup(*m.legs[0]);
  h.part_setup(*m.legs[1]);
  h.part_setup(*m.head);
  m.ams = made(0x1c, new Ams(150.0f, 0.6f, 100));
  mech_firepoints(dfn, m);
  h.nodes_pending();
  h.node_set_flag8(m.node, 0);
  float box[6];
  if (h.node_box(m.node, box) == 0) m.height = box[4] - box[1];
  else m.height = 12.0f;
  h.node_set_translation(m.centroid, 0.0f, m.height * 0.5f, 0.0f);
  h.entity_update(m);
  h.node_set_translation(m.target_node, 0.0f, m.height * 0.65f, 0.0f);  // 0x005bcd7c
  h.entity_update(m);
  h.node_add_child(m.node, m.lod_node);
  h.touch_handler(m, m.node);
  h.node_remove_child(m.node, m.lod_node);
  mech_motions(m);
  h.collider_setup(m);
  mech_configure(m, m.config);
  m.jet_node = h.node_find(m.node, "butt");
  return true;
}

// Mech3.exe 1.2 @ 0x00415ca0: the type's gaits and one-shots copied, the
// four blends between neighbouring gaits, the skis between the feet (4 m
// long for the Elemental, 12 for the rest).
void mech_motions(Mech& m) {
  MechBuildHooks& h = *g_hooks;
  for (int i = 0; i <= 6; ++i) m.anims[i] = h.gait_copy(*m.pilot, i);
  m.anims[12] = h.gait_copy(*m.pilot, 12);
  for (int i = 1; i < 9; ++i) m.action_objects[i] = h.action_copy(*m.pilot, i);
  for (int i = 0; i < 4; ++i) m.anims[7 + i] = h.make_blend(m.anims[2 + i], m.anims[3 + i]);
  m.anims[11] = nullptr;
  const float right = h.peek_float(&m.foot(1)->translation->x);
  const float d = right - h.peek_float(&m.foot(0)->translation->x);
  const float length = m.pilot->roster == 7 ? 4.0f : 12.0f;
  m.skis = make_skis(m, d * 0.5f, length);
}

// Mech3.exe 1.2 @ 0x00414430: the configuration's speeds, top speed and
// heat sinks, its weapons and equipment, each part's armor, the model's
// firepoint markers removed, the first weapon group that has weapons.
bool mech_configure(Mech& m, Config* cfg) {
  MechBuildHooks& h = *g_hooks;
  if (!cfg || !h.config_valid(cfg) || m.pilot->roster != cfg->mech) return false;
  m.x51c = cfg->g70;
  std::memcpy(&m.turn_rate, &cfg->turn_rate, 4);
  m.x524 = cfg->g78;
  m.throttle_up = cfg->accel_rate;
  m.throttle_down = cfg->decel_rate;
  m.turn_accel = cfg->turn_accel;
  for (int32_t& e : m.equipment) e = 0;
  m.set_max_speed(engine_speed(cfg->engine, max_mass(cfg->mech)));  // 0x00433030 with the XL flag
  m.x514 = 0.0f;  // 0x00424290 returns 0
  h.configure_weapons(m, cfg);
  m.heat_sinks.tech = cfg->sink_tech;
  m.heat_sinks.count = cfg->sinks;
  m.heat_sinks.doubles = cfg->double_sinks != 0;
  h.config_ungroup_equipment(cfg);
  h.part_init(*m.legs[0], m.config);
  h.part_init(*m.legs[1], m.config);
  h.part_init(*m.arms[0], m.config);
  h.part_init(*m.arms[1], m.config);
  h.part_init(*m.head, m.config);
  h.part_init(*m.torso, m.config);
  for (Handle base : {m.model_node, m.lod_node}) {
    if (!base) continue;
    for (int i = 0; i < 100; ++i) {
      char name[16];
      std::snprintf(name, sizeof name, "firepoint%02d", i);
      const Handle n = h.node_find(base, name);
      if (!n) continue;
      if (h.node_parent_count(n)) h.node_remove_child(h.node_parent(n, 0), n);
      h.node_set_active(n, 0);
      h.node_free(n);
    }
  }
  auto* wm = static_cast<WeaponManager*>(m.ammo);
  if (wm->count) {
    int32_t g = 0;
    for (int32_t i = 0; i < 5; ++i)
      if (wm->groups[i]->count) {
        g = i;
        break;
      }
    h.weapons_select(wm, g);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00414390: a skin of the definition's (aiv.zrd `skin`).
bool mech_set_skin(Mech& m, Handle skin) {
  MechBuildHooks& h = *g_hooks;
  if (!h.dfn(m.pilot->roster)->skins) return false;  // 0x00414370
  return h.skins_apply(h.dfn(m.pilot->roster)->skins, m, skin) != 0;
}

// Mech3.exe 1.2 @ 0x0041cf30: the control mode (3 keyboard and mouse, 6 a
// joystick, docs/controls.md). Mode 4 without a joystick becomes 3; with
// one the torso's input is absolute when TorsoAutoReturn is 1; mode 1 makes
// it relative. The view's camera keeps its mode.
void mech_control_mode(Mech& m, uint32_t mode) {
  MechBuildHooks& h = *g_hooks;
  bool done = false;
  if (mode & 4) {
    if (h.joy_present()) {
      m.torso->absolute = h.torso_auto_return() == 1;
      done = true;
    } else {
      mode = 3;
    }
  }
  if (!done && (mode & 1)) m.torso->absolute = false;
  const Handle view = h.view();
  if (const Handle cam = h.view_camera(view)) h.view_set_camera(view, h.camera_mode(cam));
  m.controls->set_mode(mode);
}

bool MechBuildHooks::joy_present() { return zin::joy_present(); }
int32_t MechBuildHooks::torso_auto_return() { return mech3::torso_auto_return(); }
void MechBuildHooks::set_command_callback(int32_t cmd) { zin::set_callback(cmd, player_command); }
void MechBuildHooks::controls_command(MechControls* c, int32_t cmd) {
  if (c) static_cast<PlayerControls*>(c)->command(cmd);
}

// --- mech_data.cpp: the type's models ------------------------------------------

// Mech3.exe 1.2 @ 0x00430c60: a copy of the model's first child, and the
// skins' decal state for it (when the definition has skins).
Handle mech_data_model(MechData& p, Handle* decals) {
  MechBuildHooks& h = *g_hooks;
  *decals = nullptr;
  const Handle node = h.node_clone(h.node_child(p.model, 0));
  if (!node) return nullptr;
  if (const auto* content = content_definition("chassis", p.roster)) h.node_content_chassis(node, content->id.c_str());
  if (SkinSet* skins = h.dfn(p.roster)->skins) *decals = h.skins_decals(skins, node);
  return node;
}

// Mech3.exe 1.2 @ 0x00430c20: a copy of model i's grandchild (i = 1: the low-detail model).
Handle mech_data_lod(MechData& p, int32_t i) {
  MechBuildHooks& h = *g_hooks;
  Handle n = p.models[i];
  n = n ? h.node_child(h.node_child(n, 0), 0) : nullptr;
  Handle copy = n ? h.node_clone(n) : nullptr;
  if (const auto* content = content_definition("chassis", p.roster)) h.node_content_chassis(copy, content->id.c_str());
  return copy;
}

// Mech3.exe 1.2 @ 0x00432950
Config* mech_data_config(MechData& p, int32_t i) {
  const ConfigList* cl = g_hooks->dfn(p.roster)->configs;  // 0x00432940
  const uint32_t n = static_cast<uint32_t>(cl->configs.size());
  return static_cast<uint32_t>(i) < n && i >= 0 ? cl->configs[i] : nullptr;
}

// --- mech_reader.cpp ---------------------------------------------------------

// Mech3.exe 1.2 @ 0x0046cb80: each firepoint's part (the id of the part that
// owns its node) and its place in the part. FIX (bug): a firepoint whose node
// is missing, or has no part, reads through a null part in the exe (a crash);
// it keeps -1 here.
void mech_firepoints(const MechDfn& dfn, Mech& m) {
  MechBuildHooks& h = *g_hooks;
  if (!dfn.firepoints) return;
  for (Firepoint* fp : *dfn.firepoints) {
    const Handle node = h.node_find(m.node, fp->name);
    auto* part = static_cast<MechPart*>(h.node_owner(node));
    fp->model_part = part && part->node ? h.node_id(part->node) : -1;
    h.node_get_translation(node, &fp->position[0], &fp->position[1], &fp->position[2]);
  }
}

}  // namespace mech3
