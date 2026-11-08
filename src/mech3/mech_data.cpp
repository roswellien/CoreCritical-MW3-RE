#include "mech3/mech_data.h"
#include "mech3/mech_anim.h"
#include "mech3/mech_skin.h"

#include <cstdio>

#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zreader/zreader.h"
#include "mech3/mech_config.h"
#include "mech3/mech_lib.h"
#include "mech3/mech_names.h"
#include "mech3/content_registry.h"
#include "mech3/mech_reader.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
MechDataCalls g_calls;
const char kFile[] = "C:\\zipper\\Mech3\\mech_data.cpp";
constexpr uint32_t kGaitFrames = 60;    // 0x3c, every gait resampled to 60 keys
constexpr uint32_t kActionFrames = 20;  // 0x14, passed and unused (0x00421120)

MechDataHooks g_default_hooks;
MechDataHooks* g_hooks = &g_default_hooks;
std::list<MechData*> g_cache;  // 0x00605704, count 0x00605708
std::list<Handle> g_canned_nodes;  // 0x00605714, count 0x00605718: canned_effect's node copies

const MechDfn* dfn_of(MechData& p) { return g_calls.dfn ? g_calls.dfn(p) : g_hooks->dfn(p.roster); }

const Config* first_config(MechData& p) {
  const MechDfn* d = dfn_of(p);
  if (!d || !d->configs || d->configs->configs.empty()) return nullptr;  // 0x00432950(0): 0, then a crash
  return d->configs->configs.front();
}

Handle hh(void* p) { return static_cast<Handle>(p); }
}  // namespace

MechDataCalls& mech_data_calls() { return g_calls; }

Handle MechDataHooks::mech_model(int32_t roster, int32_t variant) { return hh(mech3::mech_model(roster, variant)); }
void MechDataHooks::node_set_intersect(Handle node, int32_t on) {
  zcls::node_set_intersect(static_cast<zcls::Node*>(node), on);
}
const MechDfn* MechDataHooks::dfn(int32_t roster) { return mech_dfn(roster); }
Handle MechDataHooks::node_copy(Handle node) { return hh(zcls::tree_copy(static_cast<zcls::Node*>(node), 0, 0)); }
void MechDataHooks::node_place(Handle node, float x, float y, float z) {
  zcls::object3d_set_translation(static_cast<zcls::Node*>(node), x, y, z);
}
Handle MechDataHooks::hud_model(int32_t roster) { return hh(mech3::hud_model(roster)); }
Handle MechDataHooks::object3d_create() { return hh(zcls::object3d_create()); }
void MechDataHooks::node_set_name(Handle node, const char* name) {
  zcls::node_set_name(static_cast<zcls::Node*>(node), name);
}
void MechDataHooks::node_set_flag8(Handle node, int32_t on) { zcls::node_set_flag8(static_cast<zcls::Node*>(node), on); }
void MechDataHooks::skins_apply(const void* skins, Handle node) {
  const_cast<SkinSet*>(static_cast<const SkinSet*>(skins))->apply_first(node);
}
void MechDataHooks::anim_restart(Handle anim, Handle node) {
  zeff::anim_restart(static_cast<zeff::AnimDef*>(anim), static_cast<zcls::Node*>(node));
}
Handle MechDataHooks::effect_start(Handle effect, Handle node, const float args[9]) {
  return effect ? hh(zeff::anim_start_at(static_cast<zeff::AnimDef*>(effect), static_cast<zcls::Node*>(node), args,
                                         args + 3, args + 6))
                : nullptr;
}
void MechDataHooks::node_free(Handle h) {
  auto* node = static_cast<zcls::Node*>(h);
  // FIX (bug): the exe loops until the parents are gone; a refused removal would hang it.
  while (node->parent_count() != 0)
    if (zcls::node_remove_child(node->parents[0], node) != 0) break;
  zeff::anim_stop_using(node, 1, 1);
  zcls::tree_destroy(node);
}
Handle MechDataHooks::sound_find(const char* name) { return hh(zsnd::find(name)); }
Handle MechDataHooks::node_named(int32_t list, const char* name) { return hh(zcls::list_find_by_name(list, name)); }
void MechDataHooks::animations(MechData& p) { load_animations(p); }
Handle MechDataHooks::reader_find(Handle node, const char* key) {
  return hh(zrdr::find(static_cast<zrdr::Node*>(node), key));
}
bool MechDataHooks::reader_vec3(Handle node, const char* key, float out[3]) {
  return zrdr::get_vec3(static_cast<zrdr::Node*>(node), key, out);
}
const char* MechDataHooks::reader_string(Handle node, const char* key) {
  return zrdr::get_string(static_cast<zrdr::Node*>(node), key);
}
const char* MechDataHooks::reader_value(Handle node) { return static_cast<zrdr::Node*>(node)->list[1].s; }

void set_mech_data_hooks(MechDataHooks* h) { g_hooks = h ? h : &g_default_hooks; }
MechDataHooks& mech_data_hooks() { return *g_hooks; }
std::list<MechData*>& mech_data_cache() { return g_cache; }

// Mech3.exe 1.2 @ 0x00430490. The gaits' default throttle bounds (stand 0 to
// 0.01, step to 0.04, walk 0.02 to 0.45, trot 0.41 to 0.85, run 0.75 to 1)
// and the limps' stride speed 13.
void mech_data_init(MechData& p) {
  p = MechData{};
  p.roster = -1;
  p.trans[2][1] = 0.01f;   // 0x3c23d70a
  p.trans[3][1] = 0.04f;   // 0x3d23d70a
  p.trans[4][0] = 0.02f, p.trans[4][1] = 0.45f;  // 0x3ca3d70a, 0x3ee66666
  p.trans[5][0] = 0.41f, p.trans[5][1] = 0.85f;  // 0x3ed1eb85, 0x3f59999a
  p.trans[6][0] = 0.75f, p.trans[6][1] = 1.0f;
}

// Mech3.exe 1.2 @ 0x004303d0
MechData* mech_data_new(int32_t roster) {
  auto* p = new MechData;
  g_hooks->on_new(0x234, p);
  mech_data_init(*p);
  p->roster = roster;
  // This name addresses the donor's motions, sounds and model components.
  // Gameplay identity remains the independent registered roster handle.
  p->name = chassis_model_name(roster);
  mech_data_load(*p);
  g_hooks->on_new(0xc, nullptr);  // the list's node
  g_cache.push_back(p);
  return p;
}

// Mech3.exe 1.2 @ 0x00430d10. The exe also builds and destroys an empty one
// on the stack around the lookup, which leaves no trace.
MechData* mech_data(int32_t roster, int32_t create) {
  MechData* p = nullptr;
  for (MechData* c : g_cache)
    if (c->roster == roster) {
      p = c;
      break;
    }
  if (!p && create) p = mech_data_new(roster);
  if (p) ++p->refs;
  return p && p->x20 ? p : nullptr;
}

// Mech3.exe 1.2 @ 0x00430840: the definition's two ranges (+0x14c, +0x150)
// times the scale, squared.
void load_ranges(MechData& p) {
  const MechDfn* d = g_hooks->dfn(p.roster);
  if (!d) return;
  const float a = d->get<float>(0x14c) * lod_scale(), b = d->get<float>(0x150) * lod_scale();
  p.range_sq[0] = a * a;
  p.range_sq[1] = b * b;
}

float& lod_scale() {
  static float s = 1.0f;  // 0x005e2928
  return s;
}

// Mech3.exe 1.2 @ 0x00430440
void set_lod_scale(float s) {
  lod_scale() = s;
  for (MechData* p : g_cache) load_ranges(*p);
}

namespace {
// The sound slots by name; the last is mech_<name>.
const char* const kSounds[24] = {"mech_engine", "mech_engine_water", "mech_limp", "mech_jumpjet",
                                 "mech_servo", "mech_heat_warning", "mech_heat_warning1", "mech_engine_up",
                                 "mech_engine_down", "mech_lockon_warning", "mech_eject", "mech_crouch",
                                 "mech_landing", "mech_collide", "mech_jumpjet_start", "mech_jumpjet_stop",
                                 "radar_up", "radar_down", "mech_falling", "mech_fall",
                                 "mech_shutdown_warning", "mech_shutdown", "mech_shutdown_off", "mech_zoom"};
// The order 0x004306d0 looks them up in, as slots.
const int kSoundOrder[24] = {0, 1, 2, 7, 8, 4, 9, 11, 12, 10, 16, 17, 14, 3, 15, 13, 23, 18, 19, 5, 6, 20, 21, 22};
}  // namespace

// Mech3.exe 1.2 @ 0x004306d0. Slot 24 is the mech's own, mech_<name>.
void load_sounds(MechData& p) {
  for (int k : kSoundOrder) p.sounds[k] = g_hooks->sound_find(kSounds[k]);
  char name[64];
  std::snprintf(name, sizeof name, "mech_%s", p.name);
  p.sounds[24] = g_hooks->sound_find(name);
}

// Mech3.exe 1.2 @ 0x004320b0. Instant action and multiplayer use their own
// ("m") disable and destroy effects, except for the Elemental.
void load_effects(MechData& p) {
  const bool m = (g_hooks->game_multiplayer() != 0 || g_hooks->game_instant_action() != 0) && p.roster != 7;
  char name[96];
  auto find = [&](const char* fmt) {
    std::snprintf(name, sizeof name, fmt, p.name);
    return g_hooks->anim_find(name);
  };
  const char* const death[2][4] = {{"%s_disable", "%s_destroy", "%s_fwd_disable", "%s_back_disable"},
                                   {"%s_mdisable", "%s_mdestroy", "%s_fwd_mdisable", "%s_back_mdisable"}};
  for (int i = 0; i < 4; ++i) p.death_effects[i] = find(death[m][i]);
  p.landing_dust = find("%s_landing_effects");
  p.landing_splash = g_hooks->anim_find("mech_splash");
  p.water_death = g_hooks->anim_find("nuke_splash");
  // By part id: 4 lthigh, 5 rthigh, 2 larm, 3 rarm; destroy, detach, stub.
  const int ids[4] = {4, 5, 2, 3};
  const char* const parts[4] = {"lthigh", "rthigh", "larm", "rarm"};
  const char* const kinds[3] = {"destroy", "detach", "stub"};
  char fmt[48];
  for (int k = 0; k < 4; ++k)
    for (int j = 0; j < 3; ++j) {
      std::snprintf(fmt, sizeof fmt, "%%s_%s_%s%s", parts[k], m ? "m" : "", kinds[j]);
      p.part_effects[ids[k]][j] = find(fmt);
    }
  p.stubs[2] = g_hooks->node_named(6, "lleg_stub01.flt");  // +0x21c
  p.stubs[3] = g_hooks->node_named(6, "rleg_stub01.flt");
  p.stubs[0] = g_hooks->node_named(6, "larm_stub01.flt");
  p.stubs[1] = g_hooks->node_named(6, "rarm_stub01.flt");
  p.cockpit_explosion = find("cpit_effects_explosion");
}

const char* soil_name(int32_t i) {
  static const char* const names[11] = {"default", "water", "grass", "dirt", "mud", "concrete",
                                        "snow", "mech", "lava", "silt", "noslip"};
  return i >= 0 && i < 11 ? names[i] : "unknown";
}

// Mech3.exe 1.2 @ 0x00432670 with the constructor 0x004326d0: by soil, the
// step sound and footprint named <name><Soil> (the soil capitalised), the
// missing ones soil<Soil>; grass prints at 0.9.
void load_soils(MechData& p) {
  auto* s = new MechData::Soils;
  g_hooks->on_new(0xb0, s);
  for (int i = 0; i < 11; ++i) {
    char soil[16];
    std::snprintf(soil, sizeof soil, "%s", soil_name(i));
    soil[0] = static_cast<char>(soil[0] - 0x20);
    char name[80];
    std::snprintf(name, sizeof name, "%s%s", p.name, soil);
    s->step_sound[i] = g_hooks->sound_find(name);
    s->footprint[i] = g_hooks->anim_find(name);
    if (!s->step_sound[i] || !s->footprint[i]) {
      std::snprintf(name, sizeof name, "soil%s", soil);
      if (!s->step_sound[i]) s->step_sound[i] = g_hooks->sound_find(name);
      if (!s->footprint[i]) s->footprint[i] = g_hooks->anim_find(name);
    }
    s->scale[i] = i == 2 ? 0.9f : 1.0f;  // 0x3f666666
  }
  p.soils = s;
}

// Mech3.exe 1.2 @ 0x00431750 with the names of 0x004317d0: the effect named
// first in the block, its offset ("ofs"), and the node ("node") on list 6.
bool load_canned(MechData& p, Handle canned, int i) {
  const char* const names[3] = {"eject", "jumpjets", "narc"};
  Handle n = g_hooks->reader_find(canned, i >= 0 && i < 3 ? names[i] : "unknown");
  if (!n) return false;
  MechData::Canned& c = p.canned[i];
  c.effect = g_hooks->anim_find(g_hooks->reader_value(n));
  g_hooks->reader_vec3(n, "ofs", c.offset);
  if (const char* node = g_hooks->reader_string(n, "node")) c.node = g_hooks->node_named(6, node);
  return true;
}

// Mech3.exe 1.2 @ 0x00430ad0, its second half (the first makes the HUD).
void make_cockpit(MechData& p) {
  MechDataHooks& h = *g_hooks;
  const Handle model = h.hud_model(p.roster);  // ECX = +0x0c
  auto* c = new CockpitModel;
  h.on_new(8, c);
  if (model) {
    c->node = model;
  } else {
    c->node = h.object3d_create();
    h.node_set_name(c->node, "null_hud_cage");
  }
  p.cockpit_model = c;
  const MechDfn* d = h.dfn(p.roster);  // 0x004846a0 on +0x0c, then its reader (+0x08)
  const Handle hud = d ? h.reader_find(d->reader, "hud") : nullptr;
  if (!model || !hud) return;
  h.node_place(c->node, 0.0f, 0.0f, 0.0f);  // [0x0063d870], the zero vector
  h.node_set_intersect(c->node, 0);
  h.node_set_flag8(c->node, 0);
  // The explosion is moved onto this cockpit and reset: its cracks and dings
  // hidden until the head is hit (docs/cockpit-damage.md).
  if (p.cockpit_explosion) h.anim_restart(p.cockpit_explosion, c->node);
}

// Mech3.exe 1.2 @ 0x00430580 and the delete: off the cache, then what it
// owns: the HUD, the cockpit block (its node stays: mechlib's), the motions.
void mech_data_free(MechData* p) {
  if (!p) return;
  g_cache.remove(p);
  if (p->hud) g_hooks->hud_free(p->hud);
  delete p->cockpit_model;
  for (MechAnim*& a : p->anims) delete a, a = nullptr;
  for (MechAction*& a : p->actions) delete a, a = nullptr;
  if (p->cameras) g_hooks->cameras_free(p->cameras);
  delete p->soils;
  delete p;
}

// Mech3.exe 1.2 @ 0x004318e0
void mech_data_clear() {
  while (!g_cache.empty()) mech_data_free(g_cache.front());
  canned_nodes_free();
}

// Mech3.exe 1.2 @ 0x00432830. The offset is in the world's axes, not the
// node's: the eject seat starts the same way whichever way the mech faces.
Handle canned_effect(MechData& p, int32_t slot, Handle node, const float* at, const float* vel) {
  static const float kZero[3] = {};  // 0x0063d870
  const MechData::Canned& c = p.canned[slot];
  const float* a = at ? at : kZero;
  const float* v = vel ? vel : kZero;
  const float pos[3] = {c.offset[0] + a[0], c.offset[1] + a[1], c.offset[2] + a[2]};
  if (!node) {
    if (c.node) {
      node = g_hooks->node_copy(c.node);
      if (!node) return nullptr;
      g_canned_nodes.push_back(node);
    }
    if (!node) return nullptr;
  }
  if (at) g_hooks->node_place(node, pos[0], pos[1], pos[2]);
  const float args[9] = {pos[0], pos[1], pos[2], v[0], v[1], v[2], 0, 0, 0};
  return g_hooks->effect_start(c.effect, node, args);
}

int32_t canned_nodes_count() { return static_cast<int32_t>(g_canned_nodes.size()); }

// Mech3.exe 1.2 @ 0x00431800
void canned_nodes_free() {
  for (Handle& n : g_canned_nodes) {
    g_hooks->node_free(n);
    n = nullptr;
  }
  g_canned_nodes.clear();
}

// Mech3.exe 1.2 @ 0x004319a0 with 0x004319f0 on each: a new mission's
// sounds and effects. The soils are made again, the canned effects read
// from the definition's "animations" block (its "canned").
void mech_data_refresh() {
  for (MechData* p : g_cache) {
    delete p->soils;
    p->soils = nullptr;
    const MechDfn* d = g_hooks->dfn(p->roster);
    // FIX (bug): a type whose definition failed stays cached, and the exe
    // reads its reader through null here (0x00431a20).
    Handle anims = g_hooks->reader_find(d ? d->reader : nullptr, "animations");
    Handle canned = g_hooks->reader_find(anims, "canned");
    for (int i = 0; i < 3; ++i) load_canned(*p, canned, i);
    load_sounds(*p);
    load_soils(*p);
  }
}

// Mech3.exe 1.2 @ 0x004308a0. Both mechlib trees (the intersection flag set
// on each), the definition; then each step in turn while +0x20 holds. The
// soils load whatever happened; a failure shows the error box with no name.
bool mech_data_load(MechData& p) {
  p.x20 = 0;
  p.motion_refused = 0;
  for (int v = 0; v < 2; ++v) {
    p.models[v] = g_hooks->mech_model(p.roster, v);
    g_hooks->node_set_intersect(p.models[v], 1);
  }
  p.model = p.models[0];
  if (!p.model) {
    g_hooks->message_box(0, 0x1e0, 0xf2c, p.name);
    return false;
  }
  if (!g_hooks->dfn(p.roster)) {
    g_hooks->zerr(0x800, kFile, 0xe2, "Unable to setup (%s): invalid data file", p.name);
    return false;
  }
  p.x20 = 1;
  if (const MechDfn* d = g_hooks->dfn(p.roster); d->skins) g_hooks->skins_apply(d->skins, p.model);
  for (int id = 1; id < 6; ++id)  // +0x1b0..+0x1eb
    for (Handle& e : p.part_effects[id]) e = nullptr;
  for (Handle& e : p.death_effects) e = nullptr;
  p.landing_dust = p.landing_splash = p.water_death = nullptr;
  p.x210 = nullptr;
  for (Handle& s : p.stubs) s = nullptr;
  p.x1fc = p.cockpit_explosion = nullptr;
  load_ranges(p);
  if (p.x20) p.cameras = g_hooks->cameras(p);
  if (p.x20) load_sounds(p);
  if (p.x20) g_hooks->animations(p);
  if (p.x20) load_effects(p);
  load_soils(p);
  if (!p.x20 && !p.motion_refused) g_hooks->message_box(0, 0x1e0, 0xf2d, "");  // ours: a refused motion is logged
  return p.x20 != 0;
}

// Mech3.exe 1.2 @ 0x00431560
const char* gait_name(int i) {
  switch (i) {
    case 0: case 1: return "limp";
    case 2: case 12: return "stand";
    case 3: return "step";
    case 4: return "walk";
    case 5: return "trot";
    case 6: return "run";
    default: return "unknown";
  }
}

// Mech3.exe 1.2 @ 0x004316e0
const char* action_name(int i) {
  static const char* const names[] = {"unknown", "fallf", "fallb", "landf", "landb",
                                      "getupf", "getupb", "collapsel", "collapser"};
  return i >= 1 && i <= 8 ? names[i] : names[0];
}

// Mech3.exe 1.2 @ 0x00433030 (0x00433050 with its flag set)
float engine_speed(int32_t engine, float mass) {
  const float rating = static_cast<float>(engine_rating(Engine{engine, 0}));
  return rating / mass * 1.5f * 10.8f * 0.2778f;  // 0x005bec10, 0x005bec04
}

// Mech3.exe 1.2 @ 0x00433050
float engine_kph(int32_t engine, float mass, bool run) {
  const float rating = static_cast<float>(engine_rating(Engine{engine, 0}));
  return rating / mass * (run ? 1.5f : 1.0f) * 10.8f;  // 0x005bec10
}

float config_speed(MechData& p, bool roster_mass) {
  const Config* c = first_config(p);
  if (!c) return 0.0f;
  return engine_speed(c->engine, max_mass(roster_mass ? p.roster : c->mech));
}

// Mech3.exe 1.2 @ 0x00431350. The motion is <name>_<gait> unless the
// gait's block names one ("anim"); "trans" replaces the throttle bounds.
// Stand and step keep no hip velocity; the limps run at their own stride
// speed, the right one the left mirrored.
bool load_gait(MechData& p, zrdr::Node* motion, int i) {
  float pair[2] = {p.trans[i][0], p.trans[i][1]};
  const char* g = gait_name(i);
  zrdr::Node* n = zrdr::find(motion, g);
  char name[128];
  if (const char* s = zrdr::get_string(n, "anim")) std::snprintf(name, sizeof name, "%s", s);
  else std::snprintf(name, sizeof name, "%s_%s", p.name, g);
  if (zrdr::get_vec2(n, "trans", pair)) p.trans[i][0] = pair[0], p.trans[i][1] = pair[1];
  if (i == 2 || i == 3 || i == 12) {
    p.anims[i] = load_anim(name, 0.0f, kGaitFrames);
    // FIX (bug): the exe frees the hip's velocity keys of a missing stand or
    // step without a check (a crash); never with the shipped motions.
    if (p.anims[i]) zsis::free_velocities(p.anims[i]->hip_part);
  } else {
    const float speed = i == 0 || i == 1 ? p.limp_speed : config_speed(p, false);
    p.anims[i] = load_anim(name, speed, kGaitFrames);
  }
  if (!p.anims[i]) return false;
  if (i == 1) p.anims[1]->mirror_init(p.limp_speed, kGaitFrames);
  return true;
}

// Mech3.exe 1.2 @ 0x004315d0. The one-shots run at 20 frames a second; the
// collapses at 15, the left one the right mirrored, and a mech without a
// collapse falls backwards (fallb at 20) instead.
bool load_action_motion(MechData& p, int i) {
  float fps = 20.0f;
  char name[128];
  std::snprintf(name, sizeof name, "%s_%s", p.name, action_name(i == 7 ? 8 : i));
  p.actions[i] = load_action(name, kActionFrames);
  if (i == 7 || i == 8) {
    if (!p.actions[i]) {
      std::snprintf(name, sizeof name, "%s_fallb", p.name);
      p.actions[i] = load_action(name, kActionFrames);
    } else {
      fps = 15.0f;
    }
  }
  if (p.actions[i]) {
    p.actions[i]->set_fps(fps);
    if (i == 7) p.actions[7]->mirror_init(0.0f, p.actions[7]->frame_count);
  }
  return p.x20 != 0;
}

// Mech3.exe 1.2 @ 0x00431a70. The gaits, their bounds with stand.hi, step
// and walk.lo forced, a report where a gait's bounds do not overlap the
// next's, the footfalls, and the step turned so its left footfall falls on
// the walk's key; then the one-shots.
void load_animations(MechData& p) {
  const MechDfn* d = dfn_of(p);
  zrdr::Node* anims = zrdr::find(d ? d->reader : nullptr, "animations");
  zrdr::Node* canned = zrdr::find(anims, "canned");
  for (int i = 0; i < 3; ++i)
    g_calls.canned ? g_calls.canned(p, canned, i) : load_canned(p, canned, i);
  zrdr::Node* motion = zrdr::find(anims, "motion");
  for (int i : {2, 3, 4, 5, 6, 0, 1}) load_gait(p, motion, i);
  // Ours (docs/modding.md, M3): a gait a mod broke, with no whole copy in
  // any mount, refuses the mech, said with its motion's name (a log line;
  // mech_data_load shows no box for it); so does a step or walk missing
  // from every copy (the exe reads both just below). Any other missing gait
  // stays null, as in the exe (the shipped data has no elemental_limp).
  for (int i = 0; i <= 6; ++i) {
    if (p.anims[i]) continue;
    const char* s = zrdr::get_string(zrdr::find(motion, gait_name(i)), "anim");
    char name[128];
    if (s) std::snprintf(name, sizeof name, "%s", s);
    else std::snprintf(name, sizeof name, "%s_%s", p.name, gait_name(i));
    const bool broken = motion_broken(name);
    if (!broken && i != 3 && i != 4) continue;
    std::printf("mw3: the mech %s cannot be used: its motion %s is %s\n", p.name, name,
                broken ? "broken and no copy of motion.zbd has it whole" : "in no copy of motion.zbd");
    p.x20 = 0;
    p.motion_refused = 1;
    return;
  }
  p.trans[2][1] = 0.01f;
  p.trans[3][0] = 0.0f;
  p.trans[3][1] = 0.05f;
  p.trans[4][0] = 0.02f;
  char msg[256];
  for (int i = 2; i <= 6; ++i) {
    const bool rising = p.trans[i][0] < p.trans[i][1];
    const bool overlaps = i >= 6 || p.trans[i][1] > p.trans[i + 1][0];
    if (!rising) {
      std::snprintf(msg, sizeof msg, "AnimTransition ( %s ): [%s]  min = %.3f > max = %.3f", p.name, gait_name(i),
                    double{p.trans[i][0]}, double{p.trans[i][1]});
      zerr::report(0x200, kFile, 0x31b, msg);
    }
    if (!overlaps) {
      std::snprintf(msg, sizeof msg, "AnimTransition ( %s ): [%s max = %.3f] < [%s max = %.3f]", p.name,
                    gait_name(i), double{p.trans[i][1]}, gait_name(i + 1), double{p.trans[i + 1][0]});
      zerr::report(0x200, kFile, 0x320, msg);
    }
  }
  MechAnim* step = p.anims[3];
  MechAnim* walk = p.anims[4];
  step->footfalls();
  walk->footfalls();
  walk->set_phase(walk->events[0], true);
  const int32_t wk = walk->key0;
  step->set_phase(step->events[0], true);
  const int32_t sk = step->key0;
  const int32_t k = wk - sk;
  step->rotate_keys(k > 0 ? step->frame_count - static_cast<uint32_t>(k) : static_cast<uint32_t>(-k));
  step->footfalls();
  for (int i : {2, 1, 6, 5, 4, 3, 7, 8}) load_action_motion(p, i);
}

// Mech3.exe 1.2 @ 0x00431fb0
MechAnim* gait_copy(const MechData& p, int i) {
  if (!p.anims[i]) return nullptr;
  auto* a = new MechAnim;
  a->copy_from(*p.anims[i]);
  return a;
}

// Mech3.exe 1.2 @ 0x00432030 (0x00420e60)
MechAction* action_copy(const MechData& p, int i) {
  if (!p.actions[i]) return nullptr;
  auto* a = new MechAction;
  a->copy_from(*p.actions[i]);
  a->bind_leg_parts();
  return a;
}

// Mech3.exe 1.2 @ 0x004329c0
void scale_gaits(MechData& p, float gait[13][2], float max_speed) {
  const float k = config_speed(p, true) / max_speed;
  for (int i = 0; i < 13; ++i) {
    gait[i][0] = k * p.trans[i][0];
    gait[i][1] = k * p.trans[i][1];
  }
}

}  // namespace mech3
