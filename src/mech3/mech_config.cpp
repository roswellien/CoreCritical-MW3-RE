#include "mech3/mech_config.h"
#include "mech3/content_registry.h"
#include "mech3/content_state.h"
#include "mech3/content_network.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "crt/msvc_math.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/mech_names.h"
#include "mech3/mech_reader.h"

namespace mech3 {

namespace {
// _strdup: the name is malloc'd, as the loader's read_string makes it.
char* dup(const char* s) {
  if (!s) return nullptr;  // as _strdup (0x004260c0 makes a loadout with no name)
  char* d = static_cast<char*>(std::malloc(std::strlen(s) + 1));
  std::strcpy(d, s);
  return d;
}

// ReadFile with the count ignored, as the exe does: a short read leaves the rest.
void rd(std::FILE* f, void* p, size_t n) { std::fread(p, 1, n, f); }

// WriteFile; the exe's counts are what WriteFile reports.
uint32_t wr(std::FILE* f, const void* p, size_t n) { return static_cast<uint32_t>(std::fwrite(p, 1, n, f)); }

constexpr int32_t kEngineLimit = 79;  // [0x005e3304], the engine table's last

EquipmentItem* new_equipment(int32_t n, int32_t kind, int32_t tech) {
  auto* e = new EquipmentItem;
  e->count = n, e->equipment = kind, e->tech = tech;
  return e;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00473480. Same kind of equipment and tech, 3 matching either.
bool EquipmentItem::same_as(const ConfigItem& other) const {
  const auto* e = other.kind() == 3 ? static_cast<const EquipmentItem*>(&other) : nullptr;
  const int32_t other_tech = e ? e->tech : 0;
  if (other.kind() != kind() || !e || e->equipment != equipment) return false;
  return tech == other_tech || tech == kAnyTech || other_tech == kAnyTech;
}

// Mech3.exe 1.2 @ 0x00423d00, with the base 0x00470ce0(0).
Config::Config(int32_t m, const char* n)
    : mech(m), name(dup(n)), armor_tech(mech_tech(m)), sink_tech(mech_tech(m)),
      turn_rate(1.25663686f), accel_rate(0.5f), decel_rate(1.3f), turn_accel(3.0f) {}

// Mech3.exe 1.2 @ 0x00424350, with the item list's 0x00470d20
Config::~Config() {
  std::free(name);
  name = nullptr;
  for (ConfigItem*& i : items) delete i, i = nullptr;
  items.clear();
}

// Mech3.exe 1.2 @ 0x004723c0. Base and engine items merge with one like them;
// weapons and ammo are always new, equipment goes through add_equipment. A new
// engine item is not made (null).
ConfigItem* Config::add_item(ConfigItem* item, int32_t n) {
  if (!item) return nullptr;
  ConfigItem* found = find_item(items, item);
  modified = 1;
  const int k = item->kind();
  if (found && k != 1 && k != 3 && k != 2) {
    found->count += n;
    if (found->count < 0) found->count = 0;
    return found;
  }
  ConfigItem* made = nullptr;
  switch (k) {
    case 0: {
      auto* b = new BaseItem;  // 0x0044ec90
      b->count = n, b->value = item->id();
      made = b;
      break;
    }
    case 1: {
      auto* w = new WeaponItem;
      w->count = n;
      w->weapon = static_cast<const WeaponItem*>(item)->weapon;
      w->tech = static_cast<const WeaponItem*>(item)->tech;
      made = w;
      break;
    }
    case 2: {
      auto* a = new AmmoItem;
      a->count = n, a->weapon = static_cast<const AmmoItem*>(item)->weapon;
      made = a;
      break;
    }
    case 3: {
      const auto* e = static_cast<const EquipmentItem*>(item);
      return add_equipment(e->equipment, e->tech, n);
    }
    default: break;
  }
  if (!made) return nullptr;
  made->group = item->group;
  items.push_back(made);
  return made;
}

// Mech3.exe 1.2 @ 0x004714c0 (the shared base's, on the loadout's list)
ConfigItem* Config::find_like(const ConfigItem* item) {
  for (ConfigItem* i : items)
    if (same_item(i, item)) return i;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00471de0. Standard armour's kind is set to 3 (any tech).
bool Config::remove(ConfigItem* item, int32_t n) {
  if (!item || n < 1) return false;
  ConfigItem* found = find_item(items, item);
  if (!found) return false;
  found->count -= n;
  if (found->count < 0) found->count = 0;
  if (found->kind() == 3) {
    const auto* q = static_cast<const EquipmentItem*>(found);
    if (equipment_family(q->equipment) == 1 || equipment_family(q->equipment) == 2) {
      armor_tons = static_cast<float>(q->count) * 0.5f;  // [0x005c36d4]
      ferro = equipment_family(q->equipment) == 2;
      armor_tech = ferro ? q->tech : 3;
    } else if (equipment_family(q->equipment) == 9 || equipment_family(q->equipment) == 10) {
      sinks -= n;
    }
  }
  if (found->count != 0) return false;
  items.remove(found);
  delete found;
  return true;
}

// Mech3.exe 1.2 @ 0x00423e00 (the copy constructor) and 0x00424010's path
// with no store. The items are added one by one (add_equipment recounting
// the sinks), each keeping its place, then the fields copied over; it is
// player made and not modified, its checksum 0.
Config* Config::copy_of(const char* new_name, const Config& c) { return copy_of(new_name, c, nullptr); }

Config* Config::copy_of(const char* new_name, const Config& c, const std::function<void(Config&)>& before_recount) {
  auto* d = new Config(c.mech, "");
  d->player_made = 1;
  d->armor_tech = 0, d->sink_tech = 3, d->sinks = 0;
  for (const ConfigItem* i : c.items) {
    ConfigItem* m = d->add_item(const_cast<ConfigItem*>(i), i->count);
    if (!m) continue;  // a new engine item: the exe writes through null here
    m->part = i->kind() == 1 ? i->part : i->location(-1);
  }
  d->armor_tons = c.armor_tons;
  d->ferro = c.ferro;
  d->armor_tech = c.armor_tech;
  std::memcpy(d->armor, c.armor, sizeof d->armor);
  d->sink_tech = c.sink_tech;
  d->sinks = c.sinks;
  d->double_sinks = c.double_sinks;
  d->mech = c.mech;
  d->g70 = c.g70, d->turn_rate = c.turn_rate, d->g78 = c.g78;
  d->accel_rate = c.accel_rate, d->decel_rate = c.decel_rate, d->turn_accel = c.turn_accel;
  d->endo_steel = c.endo_steel;
  d->engine = c.engine;
  d->xl = c.xl;
  d->training = c.training;
  d->b1d = c.b1d;
  std::free(d->name);
  d->name = new_name ? dup(new_name) : nullptr;
  d->modified = 0;
  if (before_recount) before_recount(*d);
  d->recount();
  return d;
}

// Mech3.exe 1.2 @ 0x00425bd0. Each part's critical slots (0x00425db0: 12 in
// the arms, legs and side torsos, 6 in the head and the two centre parts; 78
// in all), less what the items there take and the fixed equipment (the
// cockpit, engine, actuators); an XL engine takes 2 or 3 more in each side
// torso. 0x00425d80 follows, its answer unused.
void Config::recount() {
  auto slots_of = [](int32_t part) {
    switch (part) {
      case 1: case 2: case 3: case 7: case 8: return 12;
      case 4: case 5: case 6: return 6;
      case -1: return 0x4e;
      default: return 0;
    }
  };
  for (int32_t p : {2, 3, 7, 8, 1, 6, 4, 5}) free_in[p] = slots_of(p);
  free_slots = slots_of(-1);
  for (const ConfigItem* i : items) {
    const int32_t at = i->location(mech);
    if (at == -1) continue;
    const int32_t n = i->slots();
    // FIX (bug): a part outside 0..8 (a damaged file) wrote past the table.
    if (at >= 0 && at < 9) free_in[at] -= n;
    free_slots -= n;
  }
  free_in[2] -= 2, free_in[3] -= 2, free_in[1] -= 10, free_in[6] -= 5;
  free_in[4] -= 4, free_in[5] -= 4, free_slots -= 0x15;
  if (xl) {
    const int32_t k = (mech_tech(mech) != 1) + 2;
    free_in[7] -= k, free_in[8] -= k;
  }
}

// Mech3.exe 1.2 @ 0x00471460
ConfigItem* find_item(const std::list<ConfigItem*>& items, const ConfigItem* item) {
  for (ConfigItem* i : items)
    if (i == item) return i;
  for (ConfigItem* i : items)
    if (same_item(i, item)) return i;
  return nullptr;
}

bool same_item(const ConfigItem* in, const ConfigItem* probe) {
  if (in->kind() == 3) return static_cast<const EquipmentItem*>(in)->same_as(*probe);
  if (probe->kind() != in->kind() || probe->id() != in->id()) return false;
  if (in->kind() == 1)
    return static_cast<const WeaponItem*>(probe)->tech == static_cast<const WeaponItem*>(in)->tech;
  return true;
}

// Mech3.exe 1.2 @ 0x00473220: the part of the mech's firepoint the weapon is on,
// -1 when there is none.
int32_t WeaponItem::location(int32_t m) const {
  const MechDfn* d = mech_dfn(m);
  if (!d || !d->firepoints) return -1;
  const auto n = static_cast<int32_t>(d->firepoints->size());
  const Firepoint* f = part >= 0 && part < n ? (*d->firepoints)[static_cast<size_t>(part)] : nullptr;
  return f ? f->part : -1;
}

// Mech3.exe 1.2 @ 0x00471fb0. Armor (kinds 1, 2) is one item counting half
// tons, merged with a matching one; each heat sink is its own item.
EquipmentItem* Config::add_equipment(int32_t kind, int32_t tech, int32_t n) {
  const int32_t family = equipment_family(kind);
  const bool is_ferro = family == 2;
  if (is_ferro || family == 1) {
    if (!is_ferro) tech = kAnyTech;
    EquipmentItem probe;
    probe.equipment = kind, probe.tech = tech;
    EquipmentItem* found = nullptr;
    for (ConfigItem* i : items) {
      auto* e = i->kind() == 3 ? static_cast<EquipmentItem*>(i) : nullptr;
      if (e && e->same_as(probe)) {
        found = e;
        break;
      }
    }
    if (found) {
      found->count += n;
      if (found->count < 0) found->count = 0;
    } else {
      found = new_equipment(n, kind, tech);
      items.push_back(found);
    }
    ferro = is_ferro;
    armor_tons = static_cast<float>(found->count) * 0.5f;  // 0x005c36d4
    armor_tech = is_ferro ? tech : kAnyTech;
    modified = 1;
    return found;
  }
  if (family == 9 || family == 10) {
    EquipmentItem* last = nullptr;
    for (int32_t i = 0; i < n; ++i) items.push_back(last = new_equipment(1, kind, tech));
    sinks = sink_items() + 10;
    sink_tech = tech;
    double_sinks = family == 10;
    modified = 1;
    return last;
  }
  EquipmentItem* e = new_equipment(n, kind, tech);
  items.push_back(e);
  modified = 1;
  return e;
}

// Mech3.exe 1.2 @ 0x00425f30
int32_t Config::sink_items() const {
  int32_t n = 0;
  for (const ConfigItem* i : items)
    if (i->kind() == 3) {
      const int32_t k = equipment_family(static_cast<const EquipmentItem*>(i)->equipment);
      if (k == 9 || k == 10) ++n;
    }
  return n;
}

// Mech3.exe 1.2 @ 0x00425ea0: the weapons, TAG (equipment 13) counted as one.
int32_t Config::weapon_count() const {
  int32_t n = 0;
  for (const ConfigItem* i : items)
    if (i->kind() == 1 || (i->kind() == 3 && equipment_family(static_cast<const EquipmentItem*>(i)->equipment) == 13)) ++n;
  return n;
}

// Mech3.exe 1.2 @ 0x00473570 (a Store method on the loadout's own list): the
// equipment items of a kind, of a tech or (3) any; entries, not counts.
int32_t Config::equipment_count(int32_t kind, int32_t tech) const {
  int32_t n = 0;
  for (const ConfigItem* i : items) {
    if (i->kind() != 3) continue;
    const auto* q = static_cast<const EquipmentItem*>(i);
    if (equipment_family(q->equipment) == kind && (tech == 3 || tech == q->tech)) ++n;
  }
  return n;
}

namespace {
// Mech3.exe 1.2 @ 0x004264e0 (ECX the mech, EDX the section): its internal
// structure (0x00485400..0x00485490); the head 3.
int32_t section_structure(int32_t mech, int32_t s) {
  switch (s) {
    case 0: case 3: return internal_points(mech, 1);
    case 1: case 2: case 4: case 5: return internal_points(mech, 2);
    case 6: return 3;
    case 7: case 8: return internal_points(mech, 3);
    case 9: case 10: return internal_points(mech, 4);
    default: return 0;
  }
}
// A section's most: twice its structure, the head 9.
int32_t section_most(int32_t mech, int32_t s) { return s == 6 ? 9 : 2 * section_structure(mech, s); }
}  // namespace

// Mech3.exe 1.2 @ 0x004247a0. The head first (up to 9), the rest by each
// section's share rounded and capped, the rear torsos (3..5) cut to what
// their front (0..2) leaves, then a point at a time off (round from the
// first) while over and on while under; a point goes on wherever the room
// left is not exactly 0, over the most included (the exe's test).
void Config::spread_armor(int32_t mech) {
  const auto points = [&] { return armor_points(armor_tech, ferro != 0, armor_tons); };  // 0x00424760
  const auto sum = [&] {
    int32_t n = 0;
    for (int32_t v : armor) n += v;
    return n;
  };
  const int32_t pts = points();
  int32_t most = 0;
  for (int32_t s = 3; s < 11; ++s) most += section_most(mech, s);
  if (pts > most || pts == sum()) return;
  if (static_cast<float>(pts) == 0.0f) {
    for (int32_t& v : armor) v = 0;
    return;
  }
  float share[11];
  for (int32_t s = 0; s < 11; ++s) share[s] = static_cast<float>(armor[s]) / static_cast<float>(points());
  const int32_t head = pts > 9 ? 9 : pts;
  armor[6] = head;
  const int32_t rest = pts - head;
  for (int32_t s = 0; s < 11; ++s) {
    if (s == 6) continue;
    armor[s] = crt::msvc_ftol(std::floor(static_cast<double>(static_cast<float>(rest) * share[s] - -0.5f)));
    const int32_t cap = 2 * section_structure(mech, s);
    if (armor[s] > cap) armor[s] = cap;
    else if (armor[s] < 0) armor[s] = 0;
  }
  for (int32_t s = 3; s <= 5; ++s)
    if (section_most(mech, s) - armor[s - 3] - armor[s] < 0) armor[s] = section_most(mech, s) - armor[s - 3];
  // 0x00426560 sets a section (negative to 0), 0x00426540 reads it back (unused).
  auto set = [&](int32_t s, int32_t v) { armor[s] = v < 0 ? 0 : v; };
  int32_t over = sum() - points();
  for (int32_t s = 0; over > 0; s = (s + 1) % 11) {
    if (s == 6) continue;
    set(s, armor[s] - 1);
    over = sum() - points();
  }
  static const int8_t kPair[6] = {3, 4, 5, 0, 1, 2};  // front and rear torsos share a section's most
  for (int32_t s = 0; over < 0; s = (s + 1) % 11) {
    int32_t room = section_most(mech, s) - armor[s];
    if (s <= 5) room -= armor[kPair[s]];
    if (room != 0) {
      set(s, armor[s] + 1);
      over = sum() - points();
    }
  }
}

// Mech3.exe 1.2 @ 0x00425d80: the critical slots left for items.
int32_t Config::slots_left() const { return free_slots_in(-1) - ferro_slots() - endo_slots() - engine_slots(); }

// Mech3.exe 1.2 @ 0x00425e40: ferro-fibrous armour's, 7 when its tech is Clan.
int32_t Config::ferro_slots() const { return ferro ? (armor_tech != 1 ? 14 : 7) : 0; }

// Mech3.exe 1.2 @ 0x00425e80: endo steel's, 7 on a Clan mech.
int32_t Config::endo_slots() const { return endo_steel ? (mech_tech(mech) != 1 ? 14 : 7) : 0; }

// Mech3.exe 1.2 @ 0x00425e60 with 0x00432f70: an XL engine's, 10 on a Clan mech.
int32_t Config::engine_slots() const { return xl ? (mech_tech(mech) != 1 ? 12 : 10) : 6; }

// Mech3.exe 1.2 @ 0x00432ea0
void Config::set_engine(int32_t rating) { engine = engine_index(rating); }

// Mech3.exe 1.2 @ 0x00424a90
ConfigList::ConfigList(int32_t m, int32_t n) : count(n), mech(m) {
  if (n > 0) configs.reserve(n);
}

// Mech3.exe 1.2 @ 0x00425110
ConfigList::~ConfigList() {
  for (Config*& c : configs) delete c, c = nullptr;
  configs.clear();
}

// Mech3.exe 1.2 @ 0x00472b20. The id goes through set_id. FIX (A): when the
// file ends early the exe's id is whatever ECX held, the item's own address;
// here it is INT32_MAX, which no table knows either.
void ConfigItem::load(std::FILE* f) {
  int32_t v = INT32_MAX;
  rd(f, &count, 4);
  rd(f, &v, 4);
  rd(f, &part, 4);
  rd(f, &group, 4);
  set_id(v);
}

// Mech3.exe 1.2 @ 0x00472a80. The kind and id come from vt+0x0c and vt+0x14.
uint32_t ConfigItem::save(std::FILE* f) const {
  const int32_t k = kind(), v = id();
  return wr(f, &k, 4) + wr(f, &count, 4) + wr(f, &v, 4) + wr(f, &part, 4) + wr(f, &group, 4);
}

// Mech3.exe 1.2 @ 0x00472bc0
uint32_t WeaponItem::save(std::FILE* f) const { return ConfigItem::save(f) + wr(f, &tech, 4); }

// Mech3.exe 1.2 @ 0x00472c40
uint32_t EquipmentItem::save(std::FILE* f) const { return ConfigItem::save(f) + wr(f, &tech, 4); }

// Mech3.exe 1.2 @ 0x00472cb0
uint32_t EngineItem::save(std::FILE* f) const { return ConfigItem::save(f) + wr(f, &xl, 1); }

// Mech3.exe 1.2 @ 0x00472b90
void WeaponItem::load(std::FILE* f) {
  ConfigItem::load(f);
  rd(f, &tech, 4);
}

// Mech3.exe 1.2 @ 0x00472c00. Standard armor is always tech 3.
void EquipmentItem::load(std::FILE* f) {
  ConfigItem::load(f);
  rd(f, &tech, 4);
  if (equipment == 1) tech = kAnyTech;
}

// Mech3.exe 1.2 @ 0x00472c80
void EngineItem::load(std::FILE* f) {
  ConfigItem::load(f);
  rd(f, &xl, 1);
}

// Mech3.exe 1.2 @ 0x004730e0. The weights stay on the x87 stack, so the
// products here are doubles.
double WeaponItem::weight(int32_t n) const {
  return static_cast<double>(weapon_weight(weapon, tech)) * (n == -1 ? count : n);
}

// Mech3.exe 1.2 @ 0x00473140
double AmmoItem::weight(int32_t n) const { return static_cast<double>(ammo_weight(weapon)) * (n == -1 ? count : n); }

// Mech3.exe 1.2 @ 0x00473110
double EquipmentItem::weight(int32_t n) const {
  return static_cast<double>(equip_weight(equipment, tech)) * (n == -1 ? count : n);
}

// Mech3.exe 1.2 @ 0x00473160. The index is clamped to 0..79, one past the table.
double EngineItem::weight(int32_t n) const {
  Engine e{engine > 79 ? 79 : engine < 0 ? 0 : engine, xl};
  return engine_weight_x87(e) * (n == -1 ? count : n);
}

// Mech3.exe 1.2 @ 0x004731b0
int32_t WeaponItem::slots() const { return weapon_slots(weapon, tech, false); }

// Mech3.exe 1.2 @ 0x004731c0. Ferro-fibrous keeps its slot count in the group field.
int32_t EquipmentItem::slots() const { return equipment_family(equipment) == 2 ? group : equip_slots(equipment, tech); }

// Mech3.exe 1.2 @ 0x00471540. A kind above 4 makes no item and reads nothing
// more, so the rest of the file is read out of step. FIX (bug): the exe goes
// on counting down a damaged count of up to 4 billion past the file's end, a
// hang; this stops there, with the same items (docs/fixes.md).
void load_item_list(std::list<ConfigItem*>& items, std::FILE* f) {
  int32_t n = 0;
  rd(f, &n, 4);
  while (n != 0) {
    --n;
    int32_t kind = -1;
    rd(f, &kind, 4);
    if (std::feof(f)) break;  // a short read: no item now or after
    ConfigItem* item = nullptr;
    switch (kind) {
      case 0: item = new BaseItem; break;
      case 1: item = new WeaponItem; break;
      case 2: item = new AmmoItem; break;
      case 3: item = new EquipmentItem; break;
      case 4: item = new EngineItem; break;
      default: break;
    }
    if (!item) continue;
    item->load(f);
    items.push_back(item);
  }
}
void Config::load_items(std::FILE* f) { load_item_list(items, f); }

// Mech3.exe 1.2 @ 0x00424600. Byte counts are never checked. The engine
// (0x00432ff0) and sinks (0x004243c0) loads are inline here.
bool Config::load(std::FILE* f) {
  if (!f) return false;
  if (load_managed_config(f, *this)) return true;
  std::free(name);
  name = nullptr;
  zutl_zar::read_string(f, &name);
  load_items(f);
  rd(f, &mech, 4);
  rd(f, &g70, 4), rd(f, &turn_rate, 4), rd(f, &g78, 4);
  rd(f, &accel_rate, 4), rd(f, &decel_rate, 4), rd(f, &turn_accel, 4);
  rd(f, &endo_steel, 1);
  rd(f, &crc, 4);
  rd(f, &b1d, 1), rd(f, &modified, 1);
  rd(f, &armor_tons, 4), rd(f, &armor_tech, 4), rd(f, &ferro, 1), rd(f, armor, sizeof armor);
  rd(f, &engine, 4), rd(f, &xl, 1);
  rd(f, &sink_tech, 4), rd(f, &sinks, 4), rd(f, &double_sinks, 1);
  player_made = 1;
  return validate(false);  // FIX (trap): the exe passes true, dropping hand-edited loadouts (docs/protection.md)
}

// Mech3.exe 1.2 @ 0x004717a0. The base flag (+0x10) saves an empty list.
uint32_t save_item_list(const std::list<ConfigItem*>& items, uint8_t base_flag, std::FILE* f) {
  const uint32_t n = base_flag ? 0 : static_cast<uint32_t>(items.size());
  uint32_t total = wr(f, &n, 4);
  if (n)
    for (const ConfigItem* i : items) total += i->save(f);
  return total;
}
uint32_t Config::save_items(std::FILE* f) const { return save_item_list(items, base_flag, f); }

// Mech3.exe 1.2 @ 0x00424480, with the checksum 0x004255a0 and the engine
// (0x00432fb0) and sinks (0x00424420) saves inline. The count it returns
// leaves out the name and armor tons, tech and ferro byte (one variable
// reused for several writes), 22 to 35 bytes short; the archive stores it as
// the entry's length, harmless since loading reads on from the entry's start.
uint32_t Config::save(std::FILE* f) {
  if (!f) return 0;
  if (active_content()) return save_managed_config(f,*this);
  crc = checksum();
  zutl_zar::write_string(f, name);
  uint32_t n = save_items(f);
  n += wr(f, &mech, 4);
  uint8_t move[12];  // +0x70..+0x7b, one write
  std::memcpy(move, &g70, 4), std::memcpy(move + 4, &turn_rate, 4), std::memcpy(move + 8, &g78, 4);
  n += wr(f, move, 12);
  n += wr(f, &accel_rate, 4) + wr(f, &decel_rate, 4) + wr(f, &turn_accel, 4);
  n += wr(f, &endo_steel, 1) + wr(f, &crc, 4) + wr(f, &b1d, 1) + wr(f, &modified, 1);
  wr(f, &armor_tons, 4), wr(f, &armor_tech, 4), wr(f, &ferro, 1);
  n += wr(f, armor, sizeof armor);
  n += wr(f, &engine, 4) + wr(f, &xl, 1);
  n += wr(f, &sink_tech, 4) + wr(f, &sinks, 4) + wr(f, &double_sinks, 1);
  return n;
}

// Mech3.exe 1.2 @ 0x00425650. Bit fields over the config, then three words per
// weapon, ammo or equipment item; the length (low 16 bits of word 0) counts
// only those, the buffer every item.
std::vector<uint32_t> Config::packed() const {
  if (managed_gameplay_session()) return pack_managed_config(*this);
  std::vector<uint32_t> p((static_cast<size_t>(item_count()) * 0xc + 0x34) / 4 + 1, 0);
  p[0] = static_cast<uint32_t>(mech) << 16;
  uint32_t w = (player_made != 0) | (endo_steel != 0) << 1 | (static_cast<uint32_t>(engine) & 0x7f) << 2 |
               (xl != 0) << 9 | (ferro != 0) << 10;
  w |= (static_cast<uint32_t>(crt::msvc_ftol(static_cast<double>(armor_tons) * 2)) & 0x7ffff) << 11;
  p[1] = (w & 0x3fffffff) | static_cast<uint32_t>(armor_tech) << 30;
  for (int i = 0; i < 11; ++i) p[2 + i] = static_cast<uint32_t>(armor[i]);
  size_t at = 13;
  uint32_t n = 0;
  for (const ConfigItem* i : items) {
    const int k = i->kind();
    if (k < 1 || k > 3) continue;
    uint32_t tech = 0;
    if (k == 1) tech = static_cast<uint32_t>(static_cast<const WeaponItem*>(i)->tech);
    if (k == 3) tech = static_cast<uint32_t>(static_cast<const EquipmentItem*>(i)->tech);
    // ammo leaves bits 19-20 clear
    p[at] = k | (static_cast<uint32_t>(i->id()) & 0xffff) << 3 | (tech & 3) << 19 |
            static_cast<uint32_t>(i->count) << 21;
    p[at + 1] = static_cast<uint32_t>(i->group);
    p[at + 2] = static_cast<uint32_t>(i->part);
    at += 3, ++n;
  }
  p[0] |= (n * 0xc + 0x34) & 0xffff;
  return p;
}

// Mech3.exe 1.2 @ 0x004259c0 (ECX the packed form), the network's: a
// loadout named "remote config" from another player's packed one. The armour
// tons come back doubled (the packer already gave half tons); an armour item
// sets them again as it is added. FIX (bug): no items are read past `words`
// (the exe reads as many as the size word says).
Config* Config::unpacked(const uint32_t* p, size_t words) {
  if (!p||!words) return nullptr;
  if ((p[0]&0xffff0000u)==content_loadout_marker) {
    try {return unpack_managed_config(p,words);}
    catch (const std::exception& error) {std::fprintf(stderr,"mw3 content network: %s\n",error.what());return nullptr;}
  }
  if (words<13) return nullptr;
  const uint32_t n = ((p[0] & 0xffff) - 0x34) / 0xc;
  auto* c = new Config(static_cast<int32_t>(p[0] >> 16), "remote config");
  c->player_made = 1;
  const uint32_t w = p[1];
  c->endo_steel = (w >> 1) & 1;
  c->engine = std::clamp(static_cast<int32_t>((w >> 2) & 0x7f), 0, kEngineLimit);
  c->xl = (w >> 9) & 1;
  const float half_tons = static_cast<float>((w >> 11) & 0x7ffff);
  c->armor_tons = half_tons + half_tons;
  c->ferro = (w >> 10) & 1;
  c->armor_tech = c->ferro ? static_cast<int32_t>(w >> 30) : kAnyTech;
  for (int i = 0; i < 11; ++i) c->armor[i] = static_cast<int32_t>(p[2 + i]);
  const size_t fit = words > 13 ? (words - 13) / 3 : 0;
  const size_t count = static_cast<int32_t>(n) > 0 ? std::min<size_t>(n, fit) : 0;
  for (size_t k = 0; k < count; ++k) {
    const uint32_t* q = p + 13 + 3 * k;
    const int32_t id = static_cast<int32_t>((q[0] >> 3) & 0xffff);
    const int32_t tech = static_cast<int32_t>((q[0] >> 19) & 3), num = static_cast<int32_t>(q[0] >> 21);
    ConfigItem* item = nullptr;
    switch (q[0] & 7) {
      case 1:  // a weapon: its group and firepoint as they are
        item = c->add_weapon(id, tech);
        item->group = static_cast<int32_t>(q[1]);
        item->part = static_cast<int32_t>(q[2]);
        continue;
      case 2: item = c->add_ammo(id, num); break;
      case 3: item = c->add_equipment(id, tech, num); break;
      default: continue;
    }
    item->group = static_cast<int32_t>(q[1]);
    item->set_part(static_cast<int32_t>(q[2]));
  }
  return c;
}

// Mech3.exe 1.2 @ 0x00472210 (vt+0x08): n more rounds of a weapon's ammo,
// onto the item like it unless that would weigh over a ton.
AmmoItem* Config::add_ammo(int32_t weapon, int32_t n) {
  AmmoItem probe;
  probe.count = 1, probe.weapon = weapon;
  auto* found = static_cast<AmmoItem*>(find_like(&probe));
  const double per = ammo_weight(weapon);
  if (found) {
    const int32_t total = found->count + n;
    if (static_cast<double>(total) * per > 1.0) {  // [0x005c36d8]
      found = nullptr;
    } else {
      found->count = total < 0 ? 0 : total;
    }
  }
  if (!found) {
    found = new AmmoItem;
    found->count = n, found->weapon = weapon;
    items.push_back(found);
  }
  modified = 1;
  return found;
}

// Mech3.exe 1.2 @ 0x00472330 (vt+0x10): one weapon of that tech, at the end.
WeaponItem* Config::add_weapon(int32_t weapon, int32_t tech) {
  auto* w = new WeaponItem;
  w->count = 1, w->weapon = weapon, w->tech = tech;
  items.push_back(w);
  modified = 1;
  return w;
}

// Mech3.exe 1.2 @ 0x004255b0. CRC-32 (0x00576c10) of the name, the packed form
// and the six movement values.
uint32_t Config::checksum() const {
  const std::vector<uint32_t> p = packed();
  uint32_t c = 0;
  c = zutl_zar::crc32(c, name, std::strlen(name));
  c = zutl_zar::crc32(c, p.data(), p[0] & 0xffff);
  const uint32_t move[3] = {g70, 0, g78};
  uint8_t m[12];
  std::memcpy(m, move, 12);
  std::memcpy(m + 4, &turn_rate, 4);
  c = zutl_zar::crc32(c, m, 12);
  c = zutl_zar::crc32(c, &accel_rate, 4);
  c = zutl_zar::crc32(c, &decel_rate, 4);
  return zutl_zar::crc32(c, &turn_accel, 4);
}

namespace {
// 0x004251d0: one item's share of a weight category.
bool in_category(const ConfigItem* i, int32_t what) {
  const int k = i->kind();
  switch (what) {
    case 2: return k == 3 && equipment_family(i->id()) == 11;  // jump jets
    case 4: return k == 3 && (equipment_family(i->id()) == 1 || equipment_family(i->id()) == 2);  // armor (Weight(4) itself reads +0x2c)
    case 5: return k == 1;
    case 6: return k == 2;
    case 7: {
      if (k != 3) return false;
      const int32_t e = equipment_family(i->id());
      return e != 11 && e != 9 && e != 10 && e != 1 && e != 2;
    }
    default: return false;
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x00425300. 0 engine, 1 heat sinks past the free 10, 2 jump
// jets, 3 structure, 4 armor, 5 weapons, 6 ammo, 7 other equipment, 8 max
// mass; -1 sums 0..7 with the 3 t cockpit and the engine's own shielding.
// Running sums are stored as floats, as the exe's local is.
double Config::weight(int32_t what) const {
  switch (what) {
    case -1: {
      float sum = 0.0f;
      for (int32_t i = 0; i < 8; ++i) sum = static_cast<float>(weight(i) + sum);
      const Engine e{engine, xl};
      const auto shielding = static_cast<float>(std::ceil(engine_rating(e) * static_cast<double>(0.009999f)));
      double t = static_cast<double>(sum) + 3.0 + shielding;  // 0x005e24f0
      if (mech == 9) t -= 0.5;  // the Mad Cat is half a ton lighter
      return t;
    }
    case 0: return engine_weight_x87(Engine{engine, xl});
    case 1: {
      if (active_content()) {
        double total = 0;
        int32_t explicit_sinks = 0;
        for (const ConfigItem* item : items) {
          if (item->kind() != 3) continue;
          const int32_t family = equipment_family(item->id());
          if (family == 9 || family == 10) {
            total += item->weight(-1);
            explicit_sinks += item->count;
          }
        }
        const int32_t built_in = std::max(0, sinks - 10 - explicit_sinks);
        return total + static_cast<double>(equip_weight(double_sinks ? 10 : 9, sink_tech)) * built_in;
      }
      const double w = static_cast<double>(equip_weight(double_sinks ? 10 : 9, sink_tech)) * (sinks - 10);
      return w < 0.0 ? 0.0 : w;
    }
    case 3: {
      const double w = static_cast<double>(max_mass(mech)) * static_cast<double>(0.1f);
      return endo_steel ? std::floor(w + 0.5) * 0.5 : w;
    }
    case 4: return armor_tons;
    case 8: return max_mass(mech);
    default: {
      float sum = 0.0f;
      for (const ConfigItem* i : items)
        if (in_category(i, what)) sum = static_cast<float>(i->weight(-1) + sum);
      return sum;
    }
  }
}

// Mech3.exe 1.2 @ 0x00424220. An item with no place that still needs slots.
bool Config::unplaced_item() const {
  for (const ConfigItem* i : items)
    if (i->part == -1 && i->slots() != 0) return true;
  return false;
}

// Mech3.exe 1.2 @ 0x00425ff0
int32_t Config::jump_jets() const {
  int32_t n = 0;
  for (const ConfigItem* i : items)
    if (i->kind() == 3 && equipment_family(static_cast<const EquipmentItem*>(i)->equipment) == 11) ++n;
  return n;
}

// Mech3.exe 1.2 @ 0x00425fb0
int32_t Config::jump_jet_limit() const {
  const float m = max_mass(mech);
  if (m <= 40.0f) return 2;
  return m > 65.0f ? 4 : 3;
}

// Mech3.exe 1.2 @ 0x004254d0. Elementals are never checked (docs/mechdef-head.md).
// The checksum test stays for callers that ask; load no longer does.
bool Config::validate(bool check_crc) const {
  if (mech == 7) return true;
  if (check_crc && crc != checksum()) return false;
  if (static_cast<float>(weight(-1) - 0.24f) > max_mass(mech)) return false;
  if (unplaced_item()) return false;
  if (jump_jets() > jump_jet_limit()) return false;
  int32_t points = 0;
  for (int32_t a : armor) points += a;
  return points <= armor_points(armor_tech, ferro != 0, armor_tons);
}

// Mech3.exe 1.2 @ 0x004242a0
Config* Config::from_file(const char* n, std::FILE* f) {
  if (!n || !f) return nullptr;
  auto c = std::make_unique<Config>(-1, n);
  if (!c->load(f)) return nullptr;
  return c.release();
}

// Mech3.exe 1.2 @ 0x00424b70. Entry 0 is skipped by position whatever it is,
// and a config is kept whichever mech it names. True when the file opened.
bool ConfigList::load_saved() {
  char path[128];
  std::snprintf(path, sizeof path, "configs\\%s.mw3", mech_name(this->mech));
  zutl_zar::Archive a;
  zutl_zar::init(a);
  if (!zutl_zar::open(a, path)) {
    zutl_zar::destroy(a);
    return false;
  }
  uint32_t version = 0, size = 4;
  if (zutl_zar::read(a, zutl_zar::find(a, "version"), &version, &size) == 0 && version == 15) {  // 0x00423cf0
    for (int i = 1; i < a.count; ++i) {
      const char* entry = a.entries[i].name;
      if (Config* c = Config::from_file(entry, zutl_zar::seek(a, i, nullptr))) configs.push_back(c);
    }
  }
  zutl_zar::close(a);
  zutl_zar::destroy(a);
  return true;
}

// Mech3.exe 1.2 @ 0x00424f60, with 0x00424f30 and the callback 0x00426590.
// Writes configs\\<mech>.mw3 from scratch: `version` 15, then each of the
// player's own configs under its name.
bool ConfigList::save_all() {
  // 0x00576080 ".\\configs" and _mkdir: zutl::open makes the folder in the write dir.
  char path[128];
  std::snprintf(path, sizeof path, "configs\\%s.mw3", mech_name(this->mech));
  zutl_zar::Archive a;
  zutl_zar::init(a);
  if (!zutl_zar::create(a, path)) {
    zutl_zar::destroy(a);
    return false;
  }
  const uint32_t version = 15;  // 0x00423cf0
  zutl_zar::add(a, "version", &version, 4);
  for (Config* c : configs)
    if (c->player_made)
      zutl_zar::add_written(a, c->name, [](std::FILE* f, void* c) { return static_cast<Config*>(c)->save(f); }, c);
  zutl_zar::close(a);
  zutl_zar::destroy(a);
  return true;
}

}  // namespace mech3


namespace mech3 {

// Mech3.exe 1.2 @ 0x00425090
bool ConfigList::remove(const Config* c) {
  if (!c || !c->player_made) return false;
  configs.erase(std::remove(configs.begin(), configs.end(), c), configs.end());
  return true;
}

// Mech3.exe 1.2 @ 0x00424e90
Config* ConfigList::find(const char* name) const {
  if (!name) return nullptr;
  for (Config* c : configs)
    if (c && c->name && std::strcmp(c->name, name) == 0) return c;
  return nullptr;
}

}  // namespace mech3

#include <cstdio>
#include <filesystem>
#include <string>

#include "gamez/zutil/zutl_path.h"

namespace mech3 {

const char* mech_name(int32_t mech);

// FIX (A) for 0x00426180 (Ctrl+X in the mech lab): the file dialog's
// default name in the write dir's export folder; the file is 0x00426090's.
bool lab_export_config(const Config& c) {
  char name[300];
  std::snprintf(name, sizeof name, "export\\%s_%s.m3c", mech_name(c.mech), c.name ? c.name : "");
  std::FILE* f = zutl::open(name, "wb");
  if (!f) return false;
  zutl_zar::write_string(f, c.name ? c.name : "");
  const_cast<Config&>(c).save(f);
  std::fclose(f);
  return true;
}

// FIX (A) for 0x00426300 (Ctrl+I): the next import\\*.m3c in place of the
// open dialog, read as 0x004260c0 (the name first, then the loadout), then
// renamed *.m3c.done so it is read once.
Config* lab_import_config() {
  for (const std::string& file : zutl::list("import\\*.m3c")) {
    const std::string path = "import\\" + file;
    std::FILE* f = zutl::open(path.c_str(), "rb");
    if (!f) continue;
    char* name = nullptr;  // the loadout made with it (0x00423d00(-1, name)), then freed
    zutl_zar::read_string(f, &name);
    auto* c = new Config(-1, name);
    std::free(name);
    // FIX (trap): the exe also drops a loadout whose checksum (0x004255b0)
    // is not its +0x88, as the saved ones' (docs/protection.md).
    const bool ok = c->load(f);
    std::fclose(f);
    // An earlier .done of the same name is replaced (Windows' rename keeps
    // it and fails, and the file would be read again each time).
    std::error_code ec;
    std::filesystem::rename(zutl::write_path(path.c_str()), zutl::write_path((path + ".done").c_str()), ec);
    if (ok) return c;
    delete c;
  }
  return nullptr;
}

}  // namespace mech3
