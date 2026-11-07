#include "mech3/mech_salvage.h"

#include <cstdio>
#include <algorithm>

#include "mech3/mech_config.h"
#include "mech3/mech_names.h"
#include "mech3/content_registry.h"
#include "mech3/pilot.h"
#include "mech3/cheats.h"

namespace mech3 {

namespace {
constexpr int32_t kElemental = 7;
constexpr int32_t kTag = 0x20;          // never salvaged
constexpr int32_t kSingleSink = 9, kDoubleSink = 10;
}  // namespace

// Mech3.exe 1.2 @ 0x00472da0. A weapon not made in its tech is taken in the
// other (Clan for anything but Clan). FIX (bug): a weapon without a def was
// a null read; here it is skipped.
bool add_weapons(Store& s, SalvageMech& m, int32_t tech, const SalvageCalls& c) {
  if (!m.has_manager) return false;
  for (const SalvageWeapon& w : m.weapons) {
    if (!w.present || w.type < 0 || w.type == kTag) continue;
    int32_t t = w.has_item ? w.item_tech : tech;
    if (!weapon_has_tech(w.type, t)) t = t != 1 ? 1 : 2;
    if (!weapon_has_tech(w.type, t)) continue;
    s.add_weapon(w.type, t);
    if (w.beam || w.bin < 0) continue;
    const int32_t left = m.bins[w.bin];
    if (left == 0 || left == -1) continue;
    s.add_ammo(w.type, left);
    m.bins[w.bin] = 0;
    if (c.empty_bin) c.empty_bin(m, w.bin);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00472f00
bool add_salvage(Store& s, SalvageMech& m, const SalvageCalls& c) {
  if (!m.has_data || m.type == kElemental) return false;
  bool mech = !m.torso_gone || m.head_gone;
  // Ours: food for thought wins the coin (cheats.h).
  if (mech && (m.leg_gone[0] || m.leg_gone[1])) mech = cheats().max_salvage || (c.rand() & 1) != 0;
  if (mech) s.add_mech(m.type);
  add_weapons(s, m, m.tech, c);
  int32_t remaining_sinks = std::max(0, m.sinks - 10);
  if (m.config) {
    for (const ConfigItem* it : m.config->items) {
      if (it->kind() != 3) continue;
      const int32_t part = it->location(-1);
      const int32_t kind = equipment_family(static_cast<const EquipmentItem*>(it)->equipment);
      const int8_t st = part >= 0 && part < 16 ? m.parts[part] : 0;
      if (kind == kSingleSink || kind == kDoubleSink) {
        // Stock saves aggregate sinks. Added definitions must retain their
        // identity through salvage rather than becoming a stock sink row.
        if (active_content() && it->id() >= 15 && it->group != 1 && (part == -1 || st != 2)) {
          const int32_t take = std::min(remaining_sinks, it->count);
          if (take > 0) s.add_copy(it, take);
          remaining_sinks -= take;
        }
        continue;
      }
      if (active_content() && it->id() >= 15 && it->group == 1) continue;
      if (part == -1 || st != 2) s.add_copy(it, it->count);
    }
  }
  const int32_t extra = remaining_sinks;  // the engine's ten stay with it
  if (extra > 0) s.add_equipment(kSingleSink + (m.doubles ? 1 : 0), m.sink_tech, extra);
  return true;
}

// Mech3.exe 1.2 @ 0x00472ec0
bool add_vehicle_salvage(Store& s, SalvageMech& m, const SalvageCalls& c) {
  if (!cheats().max_salvage && !(c.rand() & 1)) return true;  // ours: food for thought wins it (cheats.h)
  return add_weapons(s, m, 1, c);
}

// Mech3.exe 1.2 @ 0x0047ae10 (its CL 0 path)
void lance_salvage(Store& salvage, Store& own, std::vector<SalvageEntity>& list, uint32_t allies, bool team,
                   const SalvageCalls& c) {
  for (SalvageEntity& e : list) {
    if (!e.mech) continue;
    if (e.player) add_salvage(own, e.m, c);
    else if (team && (allies & e.team)) add_salvage(salvage, e.m, c);
  }
}

namespace {
const char* text_of(const MessageText& msg, int32_t id, const char* none) {
  if (id < 0) return none;
  const char* t = msg(id);
  return t ? t : "(null)";
}
std::string printf_s(const char* fmt, const std::string& a, int32_t n) {
  char b[512];
  std::snprintf(b, sizeof b, fmt, a.c_str(), n);
  return b;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004738f0 (a mech), 0x00473900 (a weapon, " (CLAN)" for
// Clan tech), 0x004739c0 (ammo: its weapon's), 0x00473960 (equipment).
std::string item_name(const ConfigItem& it, const MessageText& msg) {
  switch (it.kind()) {
    case 0: {
      if (const char* label = content_label("chassis", it.id())) return label;
      return text_of(msg, mech_name_message(it.id()), "notmech");
    }
    case 1: {
      const char* label = content_label("weapon", it.id());
      std::string s = label ? label : text_of(msg, weapon_name_message(it.id()), "noweapon");
      if (static_cast<const WeaponItem&>(it).tech == 1) s += " (CLAN)";
      return s;
    }
    case 2: {
      if (const char* label = content_label("weapon", it.id())) return label;
      return text_of(msg, weapon_name_message(it.id()), "noweapon");
    }
    case 3: {
      auto& e = static_cast<const EquipmentItem&>(it);
      const char* label = content_label("equipment", e.equipment);
      std::string s = label ? label : text_of(msg, equip_name_message(e.equipment), "none");
      if (e.tech == 1) s += " (CLAN)";
      return s;
    }
    case 4: {  // 0x004739d0 -> 0x00432f00
      auto& e = static_cast<const EngineItem&>(it);
      return engine_name(e.engine, e.xl, msg);
    }
    default: return std::string();
  }
}

// 0x005e2f50's makers (each entry's +0x04, less 0x210).
const uint8_t kMaker[79] = {
    0, 1, 2, 0, 6, 3, 1, 1, 4, 3, 5, 6, 0, 1, 3, 4, 4, 6, 7, 4, 1, 1, 1, 8, 9, 7, 5,
    0, 1, 1, 10, 3, 4, 0, 1, 1, 4, 6, 6, 8, 1, 11, 4, 3, 5, 1, 2, 9, 9, 12, 9, 8, 1, 11,
    3, 2, 0, 1, 8, 1, 9, 1, 2, 3, 3, 5, 3, 8, 9, 10, 7, 7, 9, 1, 1, 10, 9, 7, 10};

// Mech3.exe 1.2 @ 0x00432ec0's message (the exe reads past the table out of range; 0..78 here)
int32_t engine_maker_message(int32_t engine) {
  return 0x210 + kMaker[engine > 78 ? 78 : engine < 0 ? 0 : engine];
}

// Mech3.exe 1.2 @ 0x00432f00
std::string engine_name(int32_t engine, uint8_t xl, const MessageText& msg) {
  const int32_t index = engine > 78 ? 78 : engine < 0 ? 0 : engine;
  const char* maker = msg(engine_maker_message(index));
  char b[512];
  std::snprintf(b, sizeof b, "%s %d%s", maker ? maker : "(null)", engine_rating(Engine{index, xl}), xl ? " XL" : "");
  return b;
}

// Mech3.exe 1.2 @ 0x00473700 (a mech), 0x00473740 (a weapon): "name (n)"
// unless one or the store's own; 0x00473860 (ammo): "name (n rounds)";
// 0x00473780 (equipment): armor as tons ("name (2.5 tons)"), else "name (n)".
std::string item_label(const ConfigItem& it, const MessageText& msg) {
  switch (it.kind()) {
    case 0:
    case 1:
    case 4: {  // an engine's: 0x004738b0
      const std::string name = item_name(it, msg);
      if (!it.b10 && it.count != 1) return printf_s("%s (%d)", name, it.count);
      return name;
    }
    case 2: {
      if (it.b10) return item_name(it, msg);
      const std::string rounds = text_of(msg, 0xfc2, "");  // asked before the name
      char b[512];
      std::snprintf(b, sizeof b, "%s (%d %s)", item_name(it, msg).c_str(), it.count, rounds.c_str());
      return b;
    }
    case 3: {
      if (it.b10) return item_name(it, msg);
      const int32_t kind = equipment_family(static_cast<const EquipmentItem&>(it).equipment);
      if (kind == 1 || kind == 2) {
        char tons[16], b[512];
        std::snprintf(tons, sizeof tons, "%.1f", static_cast<double>(static_cast<float>(it.count) * 0.5f));
        const std::string unit = text_of(msg, 0xf38, "");  // asked before the name
        std::snprintf(b, sizeof b, "%s (%s %s)", item_name(it, msg).c_str(), tons, unit.c_str());
        return b;
      }
      const std::string name = item_name(it, msg);
      if (it.count > 1) return printf_s("%s (%d)", name, it.count);
      return name;
    }
    default: return std::string();
  }
}

int32_t mission_salvage_items() { return mission_salvage().count(); }  // app_mech.h, for the tests

Store& mission_salvage() {
  static Store store(1);
  return store;
}

}  // namespace mech3
