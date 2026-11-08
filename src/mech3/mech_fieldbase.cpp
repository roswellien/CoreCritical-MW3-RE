#include "mech3/mech_fieldbase.h"

#include "mech3/content_registry.h"
#include <cmath>

#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_matrix.h"
#include "mech3/mech.h"
#include "mech3/mech_anim.h"
#include "mech3/veh_move.h"
#include "mech3/zai_brain.h"
#include "mech3/zai_valve.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
FieldBaseHooks g_default;
FieldBaseHooks* g_hooks = &g_default;
FieldBases g_bases;
bool g_drive = false;
bool g_contact = false;
float g_quiet = 0.0f;
bool g_path_set = false;

zcls::Node* zn(Handle h) { return static_cast<zcls::Node*>(h); }

// The parts and nodes by name (0x005e4a98..0x005e4a58).
const char* const kParts[7] = {"arm01", "arm02", "arm03", "arm04", "cover_left", "cover_right", "cover_middle"};
}  // namespace

void set_field_base_hooks(FieldBaseHooks* h) { g_hooks = h ? h : &g_default; }
FieldBaseHooks& field_base_hooks() { return *g_hooks; }
FieldBases& field_bases() { return g_bases; }
bool& field_bases_drive() { return g_drive; }
bool& field_bases_contact() { return g_contact; }
float& field_bases_quiet_until() { return g_quiet; }

// Mech3.exe 1.2 @ 0x00459b80 (the static's constructor, at start-up): the
// states, each knowing the registry; the idle one queued, to begin. Their
// sounds' lookups (mcb_contact, mcb_waiting, mcb_complete) find nothing
// this early and are replaced by each begin.
FieldBases::FieldBases() {
  for (DockState* st : {static_cast<DockState*>(&idle), static_cast<DockState*>(&contact),
                        static_cast<DockState*>(&places), static_cast<DockState*>(&deploy),
                        static_cast<DockState*>(&repair), static_cast<DockState*>(&close),
                        static_cast<DockState*>(&reset), static_cast<DockState*>(&parked)})
    st->reg = this;
  states.push_back(&idle);
  begin_next = true;
}
bool& field_bases_path_made() { return g_path_set; }
bool is_field_base(const BasicVehicle& v) { return v.field_base; }

ai::Valve* FieldBaseHooks::valve_get(const char* name) { return ai::valve_get(name); }
void FieldBaseHooks::valve_set(ai::Valve* v, int32_t value) { v->set(value); }
Handle FieldBaseHooks::node_find(Handle root, const char* name) { return zcls::node_find_in_tree(zn(root), name); }
ai::AiParams* FieldBaseHooks::brain_params(BasicVehicle& v) {
  auto* c = dynamic_cast<ai::AiVehicleControls*>(v.controls);
  return c && c->brain ? &c->brain->params : nullptr;
}
float* FieldBaseHooks::node_matrix(Handle node) { return zcls::object3d_matrix(zn(node)); }
void FieldBaseHooks::node_set_matrix(Handle node, const float* m) { zcls::object3d_set_matrix(zn(node), m); }
bool FieldBaseHooks::node_in_world(Handle node) { return zn(node)->parent_count() != 0; }
void FieldBaseHooks::node_unparent(Handle node) { zcls::node_remove_child(zn(node)->parents[0], zn(node)); }
void FieldBaseHooks::anims_stop_on(Handle node) { zeff::anim_stop_using(zn(node), 1, 1); }
uint32_t FieldBaseHooks::player_allies() {
  // FIX (bug): the exe reads the player's vehicle unchecked (address 0xc4
  // without one); none has no allies.
  const Mech* p = world().mission_player;
  return p ? p->allies : 0;
}
float FieldBaseHooks::difficulty() { return world().difficulty; }
void FieldBaseHooks::motion_step(BasicVehicle& v, float throttle) { vehicle_motion_anim_step(v, throttle); }
uint32_t FieldBaseHooks::entity_team(Handle entity) { return static_cast<uint32_t>(unported("entity.vt+0x28+0xc0", {low(entity)})); }
void FieldBaseHooks::controls_hit(BasicVehicle& v, Handle weapon, uint32_t x, float amount, Handle entity) {
  unported("ictl.vt+0x94", {ref(v.controls), low(weapon), x, zgame::bits(amount), low(entity)});
}

// Mech3.exe 1.2 @ 0x00458890 (the script's 0x005a9990 with "mobilehq"): its
// loop a second, played once (+0x70 clear).
FieldBaseArms* FieldBaseHooks::arms_make() {
  auto* a = new FieldBaseArms;
  a->active = load_motion(*a, "mobilehq");
  if (!a->active) zerr::reportf(0x400, "C:\\zipper\\GameZMech3\\zSIScript\\si_script.cpp", 0x75,
                                "Script (%s) not loaded", "mobilehq");
  else a->set_fps(30.0f);
  a->set_loop_time(1.0f);
  a->wraps = false;
  return a;
}

FieldBaseArms::~FieldBaseArms() { script_free_parts(*this); }

// Mech3.exe 1.2 @ 0x00458900. A NaN difference stops; a NaN time goes to 0.
bool FieldBaseArms::step(float dt) {
  const float d = target - now;
  if (d == 0.0f || std::isnan(d)) return false;  // test ah, 0x40 against a double 0
  const int32_t way = d < 0.0f ? -1 : 1;
  now = 0.2f * dt * static_cast<float>(way) + now;  // 0x005e4a0c
  if (now > 1.0f) {
    now = 1.0f;
    return true;
  }
  if (!(now >= 0.0f)) now = 0.0f;
  return true;
}

// Mech3.exe 1.2 @ 0x0045a000
int32_t FieldBases::alive() const {
  int32_t n = 0;
  // FIX (bug): past three the exe reads the stray write (add's fix below);
  // the extras are inactive, so the first three are counted.
  for (int32_t i = 0; i < count && i < 3; ++i)
    if (bases[i]->active) ++n;  // 0x004d7200
  return n;
}

// Mech3.exe 1.2 @ 0x0045a030: three at most, in a campaign no more than it
// has left (+0xa8); a base past them is put out of play (inactive, silent,
// hidden) but counted.
bool FieldBases::add(FieldBase& b) {
  FieldBaseHooks& h = *g_hooks;
  bool room = count < 3;
  if (room && h.game_type() == 1) room = count < h.campaign_bases();
  if (room) {
    bases[count] = &b;
    valves[count] = h.valve_get(veh_basic_hooks().node_name(b.node));  // the node (its name first)
    if (valves[count]) h.valve_set(valves[count], 0);
    ++count;
    return true;
  }
  b.active = false;
  b.engine.entry = nullptr;  // 0x004adb40
  veh_basic_hooks().node_set_active(b.node, 0);  // 0x004eb170
  // FIX (bug): a fourth base is written past the three (over the valves);
  // none of the shipped missions has one. Here it is only counted.
  if (count < 3) bases[count] = &b;
  ++count;
  return false;
}

// Mech3.exe 1.2 @ 0x00459e50 (the registry's other fields with the dock, 4b)
void FieldBases::clear() {
  count = 0;
  requester = nullptr;
  deploy_valve = point_valve = nullptr;
  field_bases_repair_anim() = nullptr;  // [0x00605c48]
  for (int i = 0; i < 3; ++i) bases[i] = nullptr, valves[i] = nullptr;
}

void field_bases_mission_start() {
  static const char* const kSounds[11] = {"mcb_friendly_fire", "mcb_under_attack", "mcb_lost_vehicle",
                                          "mcb_occupied",      "mcb_pos_wait",     "mcb_pos_cancel",
                                          "mcb_contact",       "mcb_waiting",      "mcb_deploy",
                                          "mcb_abort",         "mcb_complete"};
  VehBasicHooks& vh = veh_basic_hooks();
  g_bases.clear();
  for (int i = 0; i < 11; ++i) g_bases.sounds[i] = vh.sound_find(kSounds[i]);  // 0x0056f6f0
  g_bases.effects = vh.anim_find("mfbeffects");                              // 0x0050b450
  g_bases.states.clear();
  g_bases.states.push_back(&g_bases.idle);
  g_bases.begin_next = true;
  g_bases.park = false;
  g_bases.deploy_valve = g_hooks->valve_get("mcbdeploy");  // 0x004d3c40
  if (g_bases.deploy_valve) g_hooks->valve_set(g_bases.deploy_valve, 0);
  g_drive = false;
}

// Mech3.exe 1.2 @ 0x004589d0
FieldBase* field_base_create(Handle ctl, const char* type, const char* name) {
  if (active_content() && content_index("vehicle", type) >= 0 && !ctl)
    cc::content::fail("vehicle-controller", "A managed field base requires an implemented native AI controller", {}, type);
  VehBasicHooks& vh = veh_basic_hooks();
  Handle model = vh.node_named(type);
  if (!model) model = vh.node_named(content_vehicle_model(type));
  if (!model) return nullptr;
  const Handle node = vh.node_clone(model);
  if (!node) return nullptr;
  vh.node_set_name(node, name);
  vh.world_add(node);
  auto* b = new FieldBase;
  g_hooks->on_new(0x160, b);
  if (!ctl) veh_basic_hooks().no_ai_controls(type, name);  // ours: as vehicle_create's
  field_base_construct(*b, ctl, node);
  return b;
}

// Mech3.exe 1.2 @ 0x00458a70: the basic vehicle made without its node, then
// given it here (16 high for the AI).
void field_base_construct(FieldBase& b, Handle ctl, Handle node) {
  VehBasicHooks& vh = veh_basic_hooks();
  g_hooks->base_construct(b, ctl, nullptr);  // 0x004ac530(ctl, 0)
  b.field_base = true;
  b.begin_next = false;
  b.anim = nullptr;
  if (node) {
    vh.entity_set_node(b, node);  // 0x004d6c90
    vh.entity_register(b);        // 0x005a7f50
    b.height = 16.0f;
  }
  g_bases.add(b);
  vh.node_set_flag8(node, 0);  // 0x004eb620
}

// Mech3.exe 1.2 @ 0x00458d40. The AI's record: never asleep (+0x88, p8 item
// 10), the p6 and p8 values at +0x5c / +0x60 / +0x80, can_leave (+0x3c) and
// the three scan switches (+0x98..+0xa0) off.
void field_base_setup(FieldBase& b, zrdr::Node* def, const char* name, int32_t kind) {
  FieldBaseHooks& h = *g_hooks;
  h.base_setup(b, def, name, kind);  // 0x0049ec00
  for (int i = 0; i < 7; ++i) b.nodes[i] = h.node_find(b.node, kParts[i]);
  h.script_path_add("..\\data\\common\\mechs\\animations", !g_path_set);
  g_path_set = true;
  b.arms = h.arms_make();
  for (int i = 0; i < 7; ++i) b.parts[i] = b.arms ? h.arms_part(*b.arms, kParts[i]) : nullptr;
  b.x09 = true;
  if (!b.controls) return;  // vt+0x140
  if (ai::AiParams* p = h.brain_params(b)) {  // IControls vt+0x54
    p->put<uint32_t>(0x60, 1);
    p->put<uint32_t>(0x3c, 0);
    p->put<uint32_t>(0x80, 1);
    p->put<uint32_t>(0x5c, 0);
    p->put<uint32_t>(0x88, 1);
    p->put<uint32_t>(0x98, 0);
    p->put<uint32_t>(0x9c, 0);
    p->put<uint32_t>(0xa0, 0);
  }
}

// Mech3.exe 1.2 @ 0x00459640: the front step run (begun, ended when done and
// the next begun, else stepped), the basic vehicle's frame without its path
// test, player aim and weapons (the throttle held at 0 unless the dock lets
// it drive), then the front step's after and the arms. A destroyed base
// still runs its steps and arms.
void field_base_frame(FieldBase& b, float dt) {
  VehBasicHooks& vh = veh_basic_hooks();
  if (!b.actions.empty()) {
    DockStep* a = b.actions.front();
    if (b.begin_next) {
      a->begin();
      b.begin_next = false;
    } else if (a->done()) {
      a->end();
      field_base_drop_front(b);
      if (!b.actions.empty()) b.actions.front()->begin();
    } else if (a->step(dt)) {
      a->finished = true;
    }
  }
  FieldBaseHooks& h = *g_hooks;
  VehicleControls& c = *b.controls;
  const bool ran = c.update(dt) == 1;  // IControls vt+0x0c
  if (c.destroyed()) {                 // vt+0x1c
    b.active = false;
    b.engine.entry = nullptr;  // 0x004adb40
    if (b.motion.anim) {       // 0x004ade70
      vh.anim_stop(b.motion.anim);
      b.motion.anim = nullptr;
    }
  } else if (ran) {
    if (!g_drive) c.throttle = 0.0f;  // IControls vt+0x20 0x004a5b70
    vh.move(b);                       // 0x004a1240
    h.motion_step(b, c.throttle);     // vt+0x1c 0x004a5b50
    vh.refresh(b);
  }
  if (!b.actions.empty()) b.actions.front()->after(dt);
  field_base_arms_step(b, dt);
}

// Mech3.exe 1.2 @ 0x004597d0: while the arms move, each part's pose at their
// time onto its node.
void field_base_arms_step(FieldBase& b, float dt) {
  FieldBaseHooks& h = *g_hooks;
  if (!b.arms || !b.arms->step(dt)) return;
  b.arms->rate = 1.0f;  // vt+0x08 0x0040e820
  for (int i = 0; i < 7; ++i) {
    zsis::Part* p = b.parts[i];
    if (!p) continue;
    float t[3], q[4];
    h.part_translation(p, b.arms->now, t);
    h.part_rotation(p, b.arms->now, q);
    float* m = h.node_matrix(b.nodes[i]);
    m[9] = t[0], m[10] = t[1], m[11] = t[2];
    zmth::quat_matrix(q, m);  // 0x00525740
    h.node_set_matrix(b.nodes[i], m);
  }
}

// Mech3.exe 1.2 @ 0x00458f50. Off multiplayer, a base of the player's side
// takes the difficulty's share; its loss is called on the radio (none left:
// 0x0047a720), a hit by an ally or an enemy too, and its controls hear of it.
int32_t field_base_damage(FieldBase& b, Handle weapon, uint32_t x, float amount) {
  FieldBaseHooks& h = *g_hooks;
  VehBasicHooks& vh = veh_basic_hooks();
  if (!b.active) return 0;
  if (h.game_type() != 3 && (h.player_allies() & b.team) != 0) amount = h.difficulty() * amount;
  const int32_t r = h.base_damage(b, weapon, x, amount);  // 0x004ad3f0
  if (!b.active) {
    if (g_bases.alive() > 0) h.say(g_bases.sounds[2]);
    else h.marker_clear();
    return r;
  }
  if (!weapon) return r;
  const Handle attacker = vh.hit_attacker();  // 0x00581e40
  const Handle e = vh.node_object(attacker);  // 0x0040eee0
  if (!e) return r;
  h.say((h.entity_team(e) & b.allies) != 0 ? g_bases.sounds[0] : g_bases.sounds[1]);
  if (b.controls) h.controls_hit(b, weapon, x, amount, e);  // vt+0x140, IControls vt+0x94
  return r;
}

// Mech3.exe 1.2 @ 0x004590b0: the basic vehicle's death (0x004ad2d0, once),
// then the campaign's loss, every time it is called.
void field_base_die(FieldBase& b) {
  g_hooks->base_die(b);
  if (const Handle c = g_hooks->campaign()) g_hooks->base_lost(c);
}

// Mech3.exe 1.2 @ 0x00458bc0: the registry's animation stopped, the node
// taken from its first parent and freed when it has one, the arms deleted,
// the steps' list emptied (the steps are the dock's), then the basic
// vehicle's (0x004ac650, with no node left).
void field_base_destroy(FieldBase& b) {
  FieldBaseHooks& h = *g_hooks;
  VehBasicHooks& vh = veh_basic_hooks();
  field_base_effects_stop(b);
  if (b.node) {
    vh.node_detach(b.node);  // 0x00583400
    if (h.node_in_world(b.node)) {
      h.node_unparent(b.node);
      h.anims_stop_on(b.node);
      vh.node_free(b.node);  // 0x004f9050
    }
    vh.entity_set_node(b, nullptr);
    vh.entity_unregister(b);
  }
  if (b.arms) h.arms_free(b.arms);
  b.arms = nullptr;
  while (!b.actions.empty()) field_base_drop_front(b);
  h.base_destroy(b);
}

// Mech3.exe 1.2 @ 0x00458b30(1)
void field_base_delete(FieldBase* b) {
  if (!b) return;
  field_base_destroy(*b);
  delete b;
}

void field_base_drop_front(FieldBase& b) {
  DockStep* s = b.actions.front();
  b.actions.pop_front();
  if (auto* d = dynamic_cast<FieldBaseDrive*>(s); d && d->orphan) g_hooks->drive_free(d);
}

// Mech3.exe 1.2 @ 0x00458cf0
void field_base_effects_start(FieldBase& b) {
  if (!b.anim && g_bases.effects) b.anim = g_hooks->effects_start(g_bases.effects, b.node);
}

// Mech3.exe 1.2 @ 0x00458d20
void field_base_effects_stop(FieldBase& b) {
  if (!b.anim) return;
  veh_basic_hooks().anim_stop(b.anim);  // 0x00506ca0
  b.anim = nullptr;
}

}  // namespace mech3
