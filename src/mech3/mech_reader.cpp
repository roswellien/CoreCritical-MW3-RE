#include "mech3/mech_reader.h"

#include <cmath>

#include "crt/msvc_math.h"
#include "crt/msvc_string.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_msg.h"
#include "mech3/mech_config.h"
#include "mech3/mech_names.h"
#include "mech3/content_registry.h"
#include <map>
#include "mech3/mech_skin.h"

namespace mech3 {
namespace {
std::map<int32_t, MechDfn*> g_loading_dfns;
}

// Mech3.exe 1.2 @ 0x0046b890. The defaults every mech starts from.
MechDfn::MechDfn(int32_t i, zrdr::Node* tree) : index(i), reader(tree) {
  put<int32_t>(0x10, 10), put<int32_t>(0x14, 400);
  static const struct { int off; uint32_t bits; } kDefaults[] = {
      {0x148, 0x3f800000}, {0x14c, 0x42be0000}, {0x150, 0x42aa0000}, {0x154, 0x3edf66f3},
      {0x158, 0x3f800000}, {0x15c, 0x3f800000}, {0x160, 0x3efa35dd}, {0x164, 0x3edf66f3},
      {0x168, 0x3e61307a}, {0x16c, 0x41600000}, {0x170, 0x4039999a}, {0x180, 0x3f000000},
      {0x188, 0x3eb2b8c2}, {0x18c, 0x40400000}, {0x190, 0xbfb2b8c2}, {0x194, 0x3f9c61aa},
      {0x198, 0xbfb2b8c2}, {0x19c, 0x3fb2b8c2}, {0x1a0, 0x3e99999a}};
  for (const auto& d : kDefaults) put(d.off, d.bits);
}

// Mech3.exe 1.2 @ 0x0046ba20. The node is never read.
bool PartSlot::set(int32_t part, zrdr::Node*) {
  name = part_name(part);
  id = part;
  return true;
}

namespace {
constexpr double kRad = 0.01745329251994;  // 0x005c3398

// A float key into the image; the getters write only on success.
bool float_key(MechDfn& d, zrdr::Node* node, const char* key, int off) {
  float v = d.get<float>(off);
  const bool ok = zrdr::get_float(node, key, &v);
  d.put(off, v);
  return ok;
}

void angle_key(MechDfn& d, zrdr::Node* node, const char* key, int off) {
  if (float_key(d, node, key, off)) d.put(off, static_cast<float>(d.get<float>(off) * kRad));
}
}  // namespace

// The movement, torso, lean, recoil and aim settings (docs/mechdef-head.md).
// Angles are degrees in the file, radians here; the file's engine maximum is
// replaced by 400.
bool read_head(MechDfn& d, HeadValues* out) {
  zrdr::Node* tree = d.reader;
  out->tech = mech_tech(d.index);
  int32_t version = d.get<int32_t>(0xc);
  const bool has_version = zrdr::get_int(tree, "version", &version);
  d.put(0xc, version);
  if (!has_version || version != 2) return false;
  bool chicken = d.raw[0x18] != 0;
  zrdr::get_bool(tree, "chicken", &chicken);
  d.raw[0x18] = chicken;
  out->half_turn_rate = 1.25663686f;  // 72 degrees, 0x3fa0d97a
  out->accel_rate = 0.5f, out->decel_rate = 1.3f, out->turn_accel_factor = 1.5f;
  zrdr::get_float(tree, "max_turn_rate", &out->half_turn_rate);
  zrdr::get_float(tree, "accel_rate", &out->accel_rate);
  zrdr::get_float(tree, "decel_rate", &out->decel_rate);
  zrdr::get_float(tree, "turn_accel_factor", &out->turn_accel_factor);
  out->half_turn_rate *= 0.5f;
  float_key(d, tree, "crouch_height", 0x148);
  int32_t engine[2] = {d.get<int32_t>(0x10), d.get<int32_t>(0x14)};
  zrdr::get_ivec2(tree, "engine_minmax", engine);
  d.put(0x10, engine[0]), d.put(0x14, 400);
  // Uninitialised stack in the exe; only a wrongly typed component would show it.
  float torso[3] = {};
  if (zrdr::get_vec3(tree, "torso_limits", torso)) {
    d.put(0x174, static_cast<float>(torso[0] * kRad));
    d.put(0x17c, static_cast<float>(torso[1] * kRad));
    d.put(0x178, static_cast<float>(torso[2] * kRad));
  }
  if (zrdr::Node* shake = zrdr::find(tree, "torso_shake")) {
    angle_key(d, shake, "pitch_amp", 0x160);
    angle_key(d, shake, "yaw_amp", 0x164);
    angle_key(d, shake, "roll_amp", 0x168);
    float_key(d, shake, "freq", 0x16c);
    float_key(d, shake, "damp_freq", 0x170);
  }
  float lod[2] = {};
  if (zrdr::get_vec2(tree, "lod_range", lod)) d.put(0x14c, lod[0]), d.put(0x150, lod[1]);
  angle_key(d, tree, "lean_angle", 0x154);
  float_key(d, tree, "lean_factor", 0x158);
  float_key(d, tree, "lean_speed", 0x15c);
  if (float_key(d, tree, "recoil_bend", 0x180))
    d.put(0x180, static_cast<float>(std::sin(d.get<float>(0x180) * kRad)));
  float_key(d, tree, "recoil_slide", 0x184);
  angle_key(d, tree, "recoil_twist", 0x188);
  float_key(d, tree, "aim_slerp_freq", 0x18c);
  angle_key(d, tree, "aim_min_pitch", 0x190);
  angle_key(d, tree, "aim_max_pitch", 0x194);
  angle_key(d, tree, "aim_min_yaw", 0x198);
  angle_key(d, tree, "aim_max_yaw", 0x19c);
  float_key(d, tree, "aim_arm_pitch_factor", 0x1a0);
  bool ignore = d.raw[0x1a4] != 0;
  zrdr::get_bool(tree, "aim_ignore_arms", &ignore);
  d.raw[0x1a4] = ignore;
  return true;
}

// The list is always made, empty when the file has no firepoints. Missing
// radius or share read as 0.
void read_firepoints(MechDfn& dfn) {
  zrdr::Node* found = zrdr::find(dfn.reader, "firepoints");
  dfn.firepoints = new std::vector<Firepoint*>();
  if (!found) return;
  zrdr::Node* list = found->list;
  const int32_t pairs = (list[0].i - 1) / 2;
  if (pairs > 0) dfn.firepoints->reserve(pairs);
  for (int32_t i = 0; i < pairs; ++i) {
    zrdr::Node* body = &list[2 + 2 * i];
    float radius = 0, share = 0;
    zrdr::get_float(body, "radius", &radius);
    zrdr::get_float(body, "share", &share);
    const int32_t part = part_id(zrdr::get_string(body, "part"));
    dfn.firepoints->push_back(new Firepoint{list[1 + 2 * i].s, radius, share, part});
  }
}

// The file's "parts" (no shipped file has one) is looked up and ignored: every
// mech gets the same 13 slots, and the side torsos (7, 8) none.
void read_part_slots(MechDfn& dfn) {
  zrdr::Node* node = zrdr::find(dfn.reader, "parts");
  for (int32_t id : {15, 1, 2, 3, 9, 10, 4, 5, 11, 12, 13, 14, 6}) dfn.parts[id].set(id, node);
}

namespace {
const char kFile[] = "C:\\zipper\\Mech3\\mech_reader.cpp";

int32_t int_key(zrdr::Node* node, const char* key) {
  int32_t v = 0;
  zrdr::get_int(node, key, &v);
  return v;
}

void read_equipment(Config& c, zrdr::Node* body, const HeadValues& head) {
  zrdr::Node* eq = zrdr::find(body, "equipment");
  if (!eq) return;
  int32_t tech = 0;  // not reset per item: a tech given once sticks
  for (int32_t k = 1; k < eq->list[0].i; ++k) {
    const zrdr::Node& e = eq->list[k];
    const char* kname = nullptr;
    int32_t part = -1;
    if (e.type == zrdr::String) {
      kname = e.s;
    } else if (e.type == zrdr::List) {
      kname = e.list[1].s;
      part = part_id(e.list[2].s);
      if (e.list[0].i > 3) tech = tech_id(e.list[3].s);
    }
    const int32_t kind = equip_kind(kname);
    if (tech == 0) tech = head.tech;
    if (kind != -1 && !equipment_restricted(c.mech, kind, tech)) {
      EquipmentItem tmp;
      tmp.count = 1, tmp.equipment = kind, tmp.tech = tech;
      // The exe calls SetPart on a null result (n <= 0 sinks); never with n = 1.
      if (ConfigItem* added = c.add_item(&tmp, 1)) added->set_part(part);
    }
  }
}

void read_armor(Config& c, zrdr::Node* body, const HeadValues& head) {
  zrdr::Node* a = zrdr::find(body, "armor");
  if (!a) return;
  bool ff = false;
  float mass = 0;
  zrdr::get_bool(a, "ferro-fibrous", &ff);
  zrdr::get_float(a, "mass", &mass);
  c.add_equipment(ff ? 2 : 1, head.tech, crt::msvc_ftol(std::floor(2.0 * mass + 0.5)));
  const int32_t legs = int_key(a, "legs"), arms = int_key(a, "arms"), headp = int_key(a, "head");
  int32_t torso[2] = {}, lr[2] = {};
  zrdr::get_ivec2(a, "torso", torso);
  zrdr::get_ivec2(a, "torsolr", lr);
  int32_t* p = c.armor;  // +0x38
  p[10] = p[9] = legs, p[8] = p[7] = arms, p[6] = headp;
  p[0] = torso[0], p[3] = torso[1], p[2] = p[1] = lr[0], p[5] = p[4] = lr[1];
  const int32_t computed = armor_points(c.armor_tech, c.ferro != 0, c.armor_tons);
  int32_t assigned = 0;
  for (int32_t v : c.armor) assigned += v;
  if (!(mass > 0) || computed != assigned) {
    char msg[600];
    std::snprintf(msg, sizeof msg,
                  "***ARMOR ALLOCATION ERROR***\n  Armor Allocation Error (%s: %s configuration)\n"
                  "  Mass %.2f tons of %s armor\n  Computed Factor (%d), Assigned Factor (%d)\n"
                  "    Head               :  %d\n    Center Torso       :  %d\n"
                  "    Center Torso (rear):  %d\n    R/L Torso          :  %d\n"
                  "    R/L Torso (rear)   :  %d\n    R/L Arm            :  %d\n    R/L Leg            :  %d",
                  mech_name(c.mech), c.name, mass, ff ? "ferro-fibrous" : "standard", computed, assigned,
                  headp, torso[0], torso[1], lr[0], lr[1], arms, legs);
    zerr::report(0x400, kFile, 362, msg);
  }
}

void read_weapons(MechDfn& dfn, Config& c, zrdr::Node* body, const HeadValues& head) {
  zrdr::Node* w = zrdr::find(body, "weapons");
  if (!w) return;
  int32_t n = w->list[0].i - 1;
  if (n > 16) {
    char msg[120];
    std::snprintf(msg, sizeof msg, "Too Many Weapons (%d>%d) in (%s:%s)", n, 16, mech_name(c.mech), c.name);
    zerr::report(0x400, kFile, 392, msg);
    n = 16;
  }
  for (int32_t k = 1; k <= n; ++k) {
    zrdr::Node* e = &w->list[k];
    const char* wname = e->list[1].s;
    const int32_t idx = weapon_index(wname);
    if (idx == -1) {
      char msg[120];
      std::snprintf(msg, sizeof msg, "Unrecognized Weapon (%s)", wname);
      zerr::report(0x200, kFile, 400, msg);
      continue;
    }
    if (equipment_restricted(c.mech, idx, mech_tech(c.mech))) continue;
    const char* fpname = e->list[2].s;
    std::vector<Firepoint*>& fps = *dfn.firepoints;
    int32_t fp = 0;
    while (fp < static_cast<int32_t>(fps.size()) && std::strcmp(fps[fp]->name, fpname) != 0) ++fp;
    if (fp == static_cast<int32_t>(fps.size())) fps.push_back(new Firepoint{fpname, 0.0f, -1.0f, -1});
    int32_t rounds = 0, apart = -1, extra[3] = {};
    if (zrdr::Node* am = zrdr::find(e, "ammo")) {
      rounds = am->list[1].i;
      apart = part_id(zrdr::get_string(am, "part"));
      if (zrdr::Node* x = zrdr::find(am, "extra")) {
        const int32_t nx = x->list[0].i - 1 < 3 ? x->list[0].i - 1 : 3;
        for (int32_t j = 0; j < nx; ++j)
          if (x->list[1 + j].type == zrdr::Int) extra[j] = x->list[1 + j].i;
      }
    }
    int32_t g = 0;
    zrdr::get_int(e, "group", &g);
    g = g - 1 < 0 ? 0 : g - 1 > 4 ? 4 : g - 1;
    auto* wi = new WeaponItem;
    wi->count = 1, wi->group = g, wi->part = fp, wi->weapon = idx, wi->tech = head.tech;
    c.items.push_back(wi);
    if (rounds == 0) continue;  // the extras go with it
    for (int32_t r : {rounds, extra[0], extra[1], extra[2]}) {
      if (r == 0) continue;
      auto* ai = new AmmoItem;
      ai->count = r, ai->weapon = idx;
      c.items.push_back(ai);
      ai->set_part(apart);
    }
  }
}
}  // namespace

// Each loadout: its equipment, internal structure, engine, armor and weapons.
bool read_configurations(MechDfn& dfn, const HeadValues& head) {
  zrdr::Node* found = zrdr::find(dfn.reader, "configurations");
  if (!found) return false;
  zrdr::Node* list = found->list;
  const int32_t pairs = (list[0].i - 1) / 2;
  dfn.configs = new ConfigList(dfn.index, pairs);
  for (int32_t i = 0; i < pairs; ++i) {
    const char* name = zutl::localize(list[1 + 2 * i].s);
    zrdr::Node* body = &list[2 + 2 * i];
    auto* c = new Config(dfn.index, name);
    dfn.configs->configs.push_back(c);
    c->accel_rate = head.accel_rate, c->decel_rate = head.decel_rate;
    c->turn_rate = head.half_turn_rate, c->turn_accel = head.turn_accel_factor;
    if (std::strncmp(name, "train", 5) == 0) c->training = 1;
    read_equipment(*c, body, head);
    zrdr::Node* internal = zrdr::find(body, "internal");
    if (internal && internal->list[1].type == zrdr::String && crt::msvc_stricmp(internal->list[1].s, "endosteel") == 0)
      c->endo_steel = 1;
    int32_t rating = 200;
    bool xl = false;
    if (zrdr::Node* e = zrdr::find(body, "engine")) {
      zrdr::get_int(e, "rating", &rating);
      zrdr::get_bool(e, "xl", &xl);
      zrdr::get_float(e, "max_turn_rate", &c->turn_rate);  // not halved, unlike the mech's
    }
    c->set_engine(rating);
    c->xl = xl;
    read_armor(*c, body, head);
    read_weapons(dfn, *c, body, head);
  }
  return true;
}

namespace {
LoadSaved g_load_saved = [](ConfigList* c) { c->load_saved(); };
SaveAll g_save_all = [](ConfigList* c) { c->save_all(); };
}  // namespace

void set_load_saved(LoadSaved fn) { g_load_saved = fn; }
void set_save_all(SaveAll fn) { g_save_all = fn; }

// Mech3.exe 1.2 @ 0x0046b9b0. Every cached definition is destroyed at shutdown,
// which is when the player's loadouts are written. FIX (bug): the exe saves
// without checking for loadouts and crashes when a bad `version` stopped the
// read before they were made; skipped here (docs/fixes.md).
MechDfn::~MechDfn() {
  if (configs && g_save_all) g_save_all(configs);
  delete skins;
  delete configs;
  if (firepoints) {
    for (Firepoint*& f : *firepoints) delete f, f = nullptr;
    delete firepoints;
  }
  skins = nullptr, configs = nullptr, firepoints = nullptr;
  zrdr::free_node(reader);
  reader = nullptr;
}

// Mech3.exe 1.2 @ 0x0046ba40
bool read_dfn(MechDfn& dfn) {
  if (!dfn.reader) return false;
  if (dfn.loaded) return true;
  HeadValues head;
  if (!read_head(dfn, &head)) return false;
  dfn.skins = read_skins(dfn.reader);
  apply_content_skins(dfn);
  read_firepoints(dfn);
  read_part_slots(dfn);
  if (read_configurations(dfn, head)) {
    apply_content_loadouts(dfn);
    if (g_load_saved) g_load_saved(dfn.configs);
  }
  dfn.loaded = 1;
  return true;
}

// Mech3.exe 1.2 @ 0x0046b7a0
MechDfn* load_dfn(int32_t mech) {
  char name[64];
  std::snprintf(name, sizeof name, "dfn_%s.zrd", mech_name(mech));
  zrdr::Node* tree = active_content() ? load_content_chassis_reader(mech) : zrdr::load(name);
  if (!tree) return nullptr;
  auto* dfn = new MechDfn(mech, tree);
  g_loading_dfns[mech] = dfn;
  bool read = false;
  try { read = read_dfn(*dfn); }
  catch (...) {
    g_loading_dfns.erase(mech);
    // Failed package activation must not persist a partially constructed loadout.
    delete dfn->configs;
    dfn->configs = nullptr;
    delete dfn;
    throw;
  }
  g_loading_dfns.erase(mech);
  if (!read) {
    delete dfn;
    return nullptr;
  }
  return dfn;
}

// Mech3.exe 1.2 @ 0x004846a0. Cached in the mech table (0x005e60b0 + 0x3c * i);
// a failed load is tried again next time.
namespace {
std::map<int32_t, MechDfn*> g_dfns;  // stock IDs retained; additions have owned cache entries
}  // namespace

MechDfn* mech_dfn(int32_t mech) {
  if (mech < 0 || mech >= chassis_count()) return nullptr;
  const auto loading = g_loading_dfns.find(mech);
  if (loading != g_loading_dfns.end()) return loading->second;
  if (!g_dfns[mech]) g_dfns[mech] = load_dfn(mech);
  return g_dfns[mech];
}

MechDfn*& mech_dfn_slot(int32_t mech) { return g_dfns[mech]; }

// The loop of 0x004844b0.
void free_dfns() {
  for (auto& entry : g_dfns) delete entry.second;
  g_dfns.clear();
}

}  // namespace mech3
