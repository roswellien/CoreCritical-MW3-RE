// The pilot's games, their slots and stores (docs/pilot.md, The games).
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "gamez/znetwork/znet_session.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsave/zsave.h"
#include "gamez/zutil/zutl_msg.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/mech_config.h"
#include "mech3/mech_names.h"
#include "mech3/content_registry.h"
#include "mech3/content_state.h"
#include "mech3/mech_objective.h"
#include "mech3/mech_reader.h"
#include "mech3/pilot.h"
#include "mech3/content_campaign.h"
#include "mech3/content_stock_campaign.h"
#include "mech3/content_world.h"
#include "content/persistence.h"

namespace mech3 {

namespace {
// ReadFile and WriteFile with their counts ignored, as the exe does: a short
// read leaves the rest of the field as it was.
void rd(std::FILE* f, void* p, size_t n) {
  if (f) std::fread(p, 1, n, f);
}
void wr(std::FILE* f, const void* p, size_t n) {
  if (f) std::fwrite(p, 1, n, f);
}

class DefaultPilotHooks : public PilotHooks {};
DefaultPilotHooks g_default_hooks;
PilotHooks* g_hooks = &g_default_hooks;
ActionSetup g_action_setup;
}  // namespace

void set_pilot_hooks(PilotHooks* h) { g_hooks = h ? h : &g_default_hooks; }
PilotHooks& pilot_hooks() { return *g_hooks; }
ActionSetup& action_setup() { return g_action_setup; }

Config* PilotHooks::config_load(const char* name, std::FILE* f) { return Config::from_file(name, f); }
void PilotHooks::save_world(const char* path, int32_t kind) { zsave::save(path, kind); }
bool PilotHooks::save_world_checked(const char* path, int32_t kind) { return zsave::save(path, kind); }
bool PilotHooks::load_world(const char* path, int32_t kind) { return zsave::load(path, kind); }
void PilotHooks::config_save(Config* c, std::FILE* f) {
  if (c) c->save(f);
}
const char* PilotHooks::message(int32_t id) {
  const zutl::Messages& m = zutl::messages();
  const char* s = m.get_text ? m.get_text(id) : nullptr;
  return s ? s : "";
}
// 0x00575fd0: FormatMessageA with the numbers as its inserts (%1!d!, ...).
std::string PilotHooks::format_ints(int32_t id, std::initializer_list<int32_t> args) {
  return zutl::format_message_ints(id, args);
}

// Mech3.exe 1.2 @ 0x00484840: operations 1 and 2 have four missions, 3 and 4 six.
// Mech3.exe 1.2 @ 0x004733b0: the dfn's loadout by that name into the store
// (0x00473400, mech and items); a full store takes it as it is.
void PilotHooks::store_fill(Store& s, int32_t) { s.fill_all(); }

int32_t PilotHooks::store_add_mech(Store& s, int32_t mech, const char* config) {
  if (s.full) return 1;
  const MechDfn* d = mech_dfn(mech);
  const Config* c = d && d->configs ? d->configs->find(config) : nullptr;
  return c ? (s.give_config(mech, c) ? 1 : 0) : 0;
}

int32_t PilotHooks::campaign_missions(int32_t campaign) {
  switch (campaign) {
    case 1: case 2: return 4;
    case 3: case 4: return 6;
    default: return 0;
  }
}
Config* PilotHooks::config_copy(const Config& c) { return Config::copy_of(c.name, c); }
void PilotHooks::config_recount(Config& c) { c.recount(); }

// ---- the store ----

// Mech3.exe 1.2 @ 0x00470ce0
Store::Store(uint8_t flag) : b04(flag) {
  if (flag) full = 1;
}

// Mech3.exe 1.2 @ 0x00470d20
Store::~Store() { clear(); }

void Store::clear() {
  for (ConfigItem*& i : items) delete i, i = nullptr;
  items.clear();
}

// Mech3.exe 1.2 @ 0x00470e30: emptied (the exe drops the list's nodes, its
// items leaked), then one of each, unlimited (+0x10): every mech but the
// Elemental (7); every weapon in each tech it comes in (Inner Sphere
// first) and its ammo when it takes some; every equipment kind, armour in
// any tech (3). FIX (bug) with the plain PPC (weapon_count).
void Store::fill_all() {
  clear();
  full = 1;
  auto put = [this](ConfigItem* it) {
    it->count = 1;
    it->b10 = 1;
    items.push_back(it);
  };
  for (int32_t m = 0; m < chassis_count(); ++m) {
    if (m == 7) continue;  // 0x00426080
    auto* b = new BaseItem;
    b->value = m;
    put(b);
  }
  for (int32_t w = 0; w < weapon_count(); ++w) {
    if (equipment_restricted(-1, w, 3)) continue;
    for (int32_t tech : {2, 1}) {
      if (!weapon_has_tech(w, tech)) continue;
      auto* it = new WeaponItem;
      it->weapon = w;
      it->tech = tech;
      put(it);
    }
    if (weapon_uses_ammo(w)) {
      auto* a = new AmmoItem;
      a->weapon = w;
      put(a);
    }
  }
  for (int32_t k = 0; k < equipment_count(); ++k) {
    if (equipment_restricted(-1, k, 3)) continue;
    for (int32_t tech : k == 1 ? std::initializer_list<int32_t>{3} : std::initializer_list<int32_t>{2, 1}) {
      if (tech != 3 && !equip_has_tech(k, tech)) continue;
      auto* it = new EquipmentItem;
      it->equipment = k;
      it->tech = tech;
      put(it);
    }
  }
}

// Mech3.exe 1.2 @ 0x004714c0
ConfigItem* Store::find_like(const ConfigItem* item) {
  for (ConfigItem* i : items)
    if (same_item(i, item)) return i;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00471460
ConfigItem* Store::find(const ConfigItem* item) { return find_item(items, item); }

// Mech3.exe 1.2 @ 0x00472630. A copy of `item`'s kind, id, tech and group
// with `n` of it, or `n` more of the one like it (its place reset: 0 for a
// weapon, else -1). The count never goes below 0.
ConfigItem* Store::add_copy(const ConfigItem* item, int32_t n) {
  if (!item) return nullptr;
  if (ConfigItem* found = find(item)) {
    found->count += n;
    if (found->count < 0) found->count = 0;
    found->part = found->kind() == 1 ? 0 : -1;
    return found;
  }
  ConfigItem* copy = nullptr;
  switch (item->kind()) {
    case 0: {
      auto* c = new BaseItem;
      c->value = item->id();
      copy = c;
      break;
    }
    case 1: {
      auto* c = new WeaponItem;
      c->weapon = static_cast<const WeaponItem*>(item)->weapon;
      c->tech = static_cast<const WeaponItem*>(item)->tech;
      copy = c;
      break;
    }
    case 2: {
      auto* c = new AmmoItem;
      c->weapon = static_cast<const AmmoItem*>(item)->weapon;
      copy = c;
      break;
    }
    case 3: {
      auto* c = new EquipmentItem;
      c->equipment = static_cast<const EquipmentItem*>(item)->equipment;
      c->tech = static_cast<const EquipmentItem*>(item)->tech;
      copy = c;
      break;
    }
    case 4: {
      auto* c = new EngineItem;
      c->engine = static_cast<const EngineItem*>(item)->engine;
      c->xl = static_cast<const EngineItem*>(item)->xl;
      copy = c;
      break;
    }
    default: return nullptr;  // no such item exists (the exe would crash here)
  }
  copy->count = n;
  copy->group = item->group;
  items.push_back(copy);
  return copy;
}

// Mech3.exe 1.2 @ 0x00471940
AmmoItem* Store::add_ammo(int32_t weapon, int32_t n) {
  AmmoItem probe;
  probe.weapon = weapon;
  if (auto* found = static_cast<AmmoItem*>(find_like(&probe))) {
    found->count += n;
    if (found->count < 0) found->count = 0;
    return found;
  }
  auto* a = new AmmoItem;
  a->count = n;
  a->weapon = weapon;
  items.push_back(a);
  return a;
}

// Mech3.exe 1.2 @ 0x00471a20. Kind 1's tech is always 3 (any).
EquipmentItem* Store::add_equipment(int32_t kind, int32_t tech, int32_t n) {
  if (equipment_family(kind) == 1) tech = 3;
  EquipmentItem probe;
  probe.count = 1, probe.equipment = kind, probe.tech = tech;
  if (auto* found = static_cast<EquipmentItem*>(find_like(&probe))) {
    found->count += n;
    if (found->count < 0) found->count = 0;
    return found;
  }
  auto* e = new EquipmentItem;
  e->count = n, e->equipment = kind, e->tech = tech;
  items.push_back(e);
  return e;
}

// Mech3.exe 1.2 @ 0x00471b20: one more (the XL flag only for a new one).
EngineItem* Store::add_engine(int32_t engine, uint8_t xl) {
  EngineItem probe;
  probe.count = 1, probe.engine = engine, probe.xl = xl;
  if (auto* found = static_cast<EngineItem*>(find_like(&probe))) {
    if (++found->count < 0) found->count = 0;
    return found;
  }
  auto* e = new EngineItem;
  e->count = 1, e->engine = engine, e->xl = xl;
  items.push_back(e);
  return e;
}

// Mech3.exe 1.2 @ 0x00471840: one more, its place cleared.
WeaponItem* Store::add_weapon(int32_t weapon, int32_t tech) {
  WeaponItem probe;
  probe.count = 1, probe.weapon = weapon, probe.tech = tech;
  if (auto* found = static_cast<WeaponItem*>(find_like(&probe))) {
    if (++found->count < 0) found->count = 0;
    found->part = -1;
    return found;
  }
  auto* w = new WeaponItem;
  w->count = 1, w->weapon = weapon, w->tech = tech;
  items.push_back(w);
  return w;
}

// Mech3.exe 1.2 @ 0x00471d20. A store's own (b10) items are never taken.
bool Store::remove(ConfigItem* item, int32_t n) {
  if (!item || n <= 0) return false;
  ConfigItem* found = find(item);
  if (!found || found->b10) return false;
  found->count -= n;
  if (found->count < 0) found->count = 0;
  if (found->count != 0) return false;
  items.remove(found);
  delete found;
  return true;
}

// Mech3.exe 1.2 @ 0x00472a30
void Store::remove_mech(int32_t mech) {
  BaseItem probe;
  probe.value = mech;
  remove(find_like(&probe), 1);
}

// Mech3.exe 1.2 @ 0x004732b0
void Store::add_all(const Store& from) {
  for (ConfigItem* i : from.items) add_copy(i, i->count);
}

// Mech3.exe 1.2 @ 0x00473300
void Store::assign(const Store& from) {
  clear();
  add_all(from);
}

// Mech3.exe 1.2 @ 0x00471c10. Merged with one like it (its place kept, unlike
// add_copy's); none for -1 or 0x13.
BaseItem* Store::add_mech(int32_t mech) {
  if (mech < 0 || mech >= chassis_count()) return nullptr;
  BaseItem probe;
  probe.count = 1, probe.value = mech;
  if (auto* found = static_cast<BaseItem*>(find_like(&probe))) {
    if (++found->count < 0) found->count = 0;
    return found;
  }
  auto* b = new BaseItem;
  b->count = 1, b->value = mech;
  items.push_back(b);
  return b;
}

// Mech3.exe 1.2 @ 0x00473510: the counts of the items like `item`.
int32_t Store::count_like(const ConfigItem* item) const {
  if (!item) return 0;
  int32_t n = 0;
  for (const ConfigItem* it : items)
    if (same_item(it, item)) n += it->count;
  return n;
}

// Mech3.exe 1.2 @ 0x00472d40
bool Store::remove_config(const Config* c) {
  if (!c) return false;
  for (ConfigItem* it : c->items) remove(it, it->count);
  return true;
}

// Mech3.exe 1.2 @ 0x00409f00
ConfigItem* Store::has_mech(int32_t mech) {
  BaseItem probe;
  probe.count = 1, probe.value = mech;
  return find_like(&probe);
}

// Mech3.exe 1.2 @ 0x004735f0. The shortfall is logged (0x0049bfd0, a no-op).
bool Store::has_config(int32_t mech, const Store* other, const Config* c) {
  // The exe compares `other` with the loadout (0x00473614), never equal.
  if (!c || full || static_cast<const void*>(other) == static_cast<const void*>(c)) return true;
  if (mech != -1 && !has_mech(mech)) return false;
  for (const ConfigItem* it : c->items) {
    if (!it) continue;
    int32_t have = count_like(it);
    if (other) have += other->count_like(it);
    if (have < it->count) return false;
  }
  return true;
}

// The same with a loadout as `other` (the mech lab's 0x00449fb0 passes the
// slot's: its items count as the store's, as the exe reads both item lists
// at +0x08 alike); the same loadout is always there.
bool Store::has_config(int32_t mech, const Config* other, const Config* c) {
  if (!c || full || other == c) return true;
  if (mech != -1 && !has_mech(mech)) return false;
  for (const ConfigItem* it : c->items) {
    if (!it) continue;
    int32_t have = count_like(it);
    if (other)
      for (const ConfigItem* o : other->items)
        if (same_item(o, it)) have += o->count;
    if (have < it->count) return false;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00473440
bool Store::take_config(int32_t mech, const Config* c) {
  if (full) return true;
  if (mech != -1) remove_mech(mech);
  return remove_config(c);
}

// Mech3.exe 1.2 @ 0x00473400
bool Store::give_config(int32_t mech, const Config* c) {
  if (full) return true;
  if (mech != -1) add_mech(mech);
  return add_config(c);
}

// Mech3.exe 1.2 @ 0x00424010
Config* config_copy_store(const char* name, const Config& c, Store* store, bool take) {
  return Config::copy_of(name, c, [&](Config& d) {
    if (!store) return;
    if (!store->has_config(-1, static_cast<const Store*>(nullptr), &d)) {
      // Each item cut to what the store holds, from the first again after
      // each cut; then the emptied ones dropped.
      for (bool again = true; again;) {
        again = false;
        for (ConfigItem* it : d.items) {
          if (!it) continue;
          const int32_t have = store->count_like(it);
          if (have < it->count) {
            it->count = it->count + (have - it->count);
            if (it->count < 0) it->count = 0;
            again = true;
            break;
          }
        }
      }
      for (auto i = d.items.begin(); i != d.items.end();) {
        if (*i && (*i)->count == 0) {
          delete *i;  // the exe leaks it (fixes.md)
          i = d.items.erase(i);
        } else {
          ++i;
        }
      }
      d.modified = 1;
      const int32_t sink_tech = c.sink_tech;
      const uint8_t doubles = c.double_sinks;
      d.sinks = d.sink_items() + 10;
      d.sink_tech = sink_tech;
      d.double_sinks = doubles;
    }
    if (take) store->take_config(-1, &d);
  });
}

// Mech3.exe 1.2 @ 0x00472cf0
bool Store::add_config(const Config* c) {
  if (!c) return false;
  for (ConfigItem* i : c->items) add_copy(i, i->count);
  return true;
}

// Mech3.exe 1.2 @ 0x00471790 -> 0x004717a0
uint32_t Store::save(std::FILE* f) const {
  return active_content()?save_managed_store(f,*this):save_item_list(items,full,f);
}

// Mech3.exe 1.2 @ 0x00471530 -> 0x00471540
void Store::load(std::FILE* f) {
  if(active_content())load_managed_store(f,*this);
  else load_item_list(items,f);
}

// ---- the slot ----

// Mech3.exe 1.2 @ 0x00433cc0
GameSlot::GameSlot(int32_t m, Config* c, int32_t s) : mech(m), config(c), skin(s) {}

// Mech3.exe 1.2 @ 0x00433d60
GameSlot::~GameSlot() {
  delete config;
  config = nullptr;
}

// Mech3.exe 1.2 @ 0x00433d90. A modified loadout stays marked modified in the copy.
void GameSlot::copy_from(const GameSlot& s) {
  mech = s.mech;
  skin = s.skin;
  x0c = 0;
  x14 = s.x14;
  set_config(s.config);
  if (config && s.config && s.config->modified) {
    config->modified = 1;
    pilot_hooks().config_recount(*config);
  }
}

// Mech3.exe 1.2 @ 0x00433df0
bool GameSlot::set_config(const Config* c, Store* store, bool take) {
  if (config == c) return true;
  if (c && c->mech != mech) return false;
  if (store && !store->full && take) {
    if (config) store->give_config(-1, config);
    delete config;
    config = c ? config_copy_store(c->name, *c, store, take) : nullptr;
    return true;
  }
  return set_config(c);
}

// Mech3.exe 1.2 @ 0x00433df0 with no store (the copy's only caller): the
// slot's loadout replaced by a copy of `c`, or by none; a loadout of another
// mech is refused (false) and the old one kept.
bool GameSlot::set_config(const Config* c) {
  if (config == c) return true;
  if (c && c->mech != mech) return false;
  delete config;
  config = c ? pilot_hooks().config_copy(*c) : nullptr;
  return true;
}

// Mech3.exe 1.2 @ 0x00433ef0
void GameSlot::save(Pilot& p) {
  if(active_content()) {save_managed_slot(p.file,*this);return;}
  wr(p.file, &mech, 4);
  wr(p.file, &x14, 4);
  wr(p.file, &skin, 4);
  const int32_t has = config != nullptr;
  wr(p.file, &has, 4);
  if (has) pilot_hooks().config_save(config, p.file);
}

// Mech3.exe 1.2 @ 0x00433f80. A configuration read replaces the one held
// without deleting it (a leak in the exe; deleted here).
void GameSlot::load(Pilot& p) {
  if(active_content()) {load_managed_slot(p.file,*this);return;}
  rd(p.file, &mech, 4);
  rd(p.file, &x14, 4);
  rd(p.file, &skin, 4);
  int32_t has = 0;
  rd(p.file, &has, 4);
  if (has) {
    Config* c = pilot_hooks().config_load("temp", p.file);
    delete config;
    config = c;
  }
}

// ---- the games ----

Game::Game(Pilot* p) : pilot(p), slot(3, nullptr, 0) {}

// Mech3.exe 1.2 @ 0x00434560
void Game::save() {
  slot.save(*pilot);
  store.save(pilot->file);
}

// Mech3.exe 1.2 @ 0x00434580
void Game::load() {
  slot.load(*pilot);
  store.load(pilot->file);
}

// Mech3.exe 1.2 @ 0x004373e0
LabGame::LabGame(Pilot* p) : Game(p) {
  pilot_hooks().store_fill(store, kMechLab);
  lineup[0].mech = 3;  // 0x005a2ba0 on each
  lineup[1].mech = 8;
  lineup[2].mech = 13;
}

// Mech3.exe 1.2 @ 0x004374d0
LabGame::~LabGame() = default;

// Mech3.exe 1.2 @ 0x004345a0. Operation 1, mission 1 gets the starting
// store: two mechs 3 with the "Primary" loadout (message 0xf20), one of the
// mech's own taken back, and ammunition (pilot.md, A new campaign).
CampaignGame::CampaignGame(Pilot* p, int32_t c, int32_t m) : Game(p), mission(m), campaign(c) {
  if (c == 1 && m == 1) {
    PilotHooks& h = pilot_hooks();
    h.store_add_mech(store, 3, h.message(0xf20));
    h.store_add_mech(store, 3, h.message(0xf20));
    store.remove_mech(3);
    store.add_ammo(0x11, 750);
    store.add_ammo(0x15, 4000);
    store.add_ammo(2, 1000);
  }
}

// Mech3.exe 1.2 @ 0x00434820. The fields after +0xa0 are the file's (0 here
// before it, heap garbage in the exe).
CampaignGame::CampaignGame(Pilot* p) : Game(p), a8(0), ac(0) { load(); }

// Mech3.exe 1.2 @ 0x00434740, with 0x00435240(1)
CampaignGame::~CampaignGame() {
  for (GameSlot*& s : lancemates) delete s, s = nullptr;
  lancemates.clear();
}

// Mech3.exe 1.2 @ 0x00434990
void CampaignGame::save() {
  adopt_content_stock_campaign(*this);
  if (active_content()) bind_content_campaign_run(*this);
  refresh_content_campaign_semantics(*this);
  Game::save();
  std::FILE* f = pilot->file;
  wr(f, &campaign, 4);
  wr(f, &mission, 4);
  wr(f, &b38, 1);
  wr(f, &a8, 4);
  wr(f, &ac, 4);
  wr(f, &c4, 4);
  wr(f, b4, 0x10);
  store3c.save(f);
  store64.save(f);
  store50.save(f);
  store78.save(f);
  const int32_t n = static_cast<int32_t>(lancemates.size());
  wr(f, &n, 4);
  if (n)
    for (GameSlot* s : lancemates) s->save(*pilot);  // 0x00434010
  if (active_content()) {
    if (!f || !cc::content::valid_campaign_run_id(content_run) ||
        std::fwrite("CCRUN01\n", 1, 8, f) != 8 ||
        std::fwrite(content_run.data(), 1, 64, f) != 64)
      cc::content::fail("pilot-run", "Cannot write the managed campaign run trailer");
  }
}

// Mech3.exe 1.2 @ 0x00434ae0. FIX (bug): a damaged count reads on past the
// file's end, making a slot each time (up to 4 billion: a hang); here the
// reading stops at the end (docs/fixes.md).
void CampaignGame::load() {
  Game::load();
  std::FILE* f = pilot->file;
  rd(f, &campaign, 4);
  rd(f, &mission, 4);
  rd(f, &b38, 1);
  rd(f, &a8, 4);
  rd(f, &ac, 4);
  rd(f, &c4, 4);
  rd(f, b4, 0x10);
  store3c.load(f);
  store64.load(f);
  store50.load(f);
  store78.load(f);
  uint32_t n = 0;
  rd(f, &n, 4);
  if (n) {
    if (lancemates.capacity() < n && n < 0x10000) lancemates.reserve(n);
    for (; n; --n) {  // 0x00434040
      auto s = std::make_unique<GameSlot>(-1, nullptr, 0);
      s->load(*pilot);
      lancemates.push_back(s.release());
      if (f && std::feof(f)) break;
    }
  }
  if (active_content()) {
    char magic[8]{}, run[64]{};
    if (!f || std::fread(magic, 1, 8, f) != 8 || std::memcmp(magic, "CCRUN01\n", 8) != 0 ||
        std::fread(run, 1, 64, f) != 64 ||
        !cc::content::valid_campaign_run_id(std::string(run, 64)))
      cc::content::fail("pilot-run", "Managed campaign run trailer is missing or malformed");
    content_run.assign(run, 64);
  }
  read_content_campaign(*this);
  if (content_mission.empty() || content_stock_campaign(*this)) merge_salvage();
}

// Mech3.exe 1.2 @ 0x00434cb0. What the last mission left in two stores is
// added to the campaign's own; those and the one at +0x64 are emptied.
void CampaignGame::merge_salvage() {
  store.add_all(store50);
  store.add_all(store78);
  a8 = ac;
  store50.clear();
  store64.clear();
  store78.clear();
}

// Mech3.exe 1.2 @ 0x00434e00 (vt+0x24). With `next`, the game of the mission
// after this one (null when the campaign is over): this one's stores, slot,
// record and lancemates carried over, the mission's salvage its +0x3c, the
// world saved into its file; this one's salvage then merged. Without, this one
// settled in place: the slots' mechs and loadouts listed in +0x50 (docs/campaign.md 4).
CampaignGame* CampaignGame::next_mission(bool next_one, bool save) {
  const bool managed_stock = adopt_content_stock_campaign(*this);
  if (!content_mission.empty() && !managed_stock) return advance_content_campaign(*this, next_one, save);
  PilotHooks& h = pilot_hooks();
  int32_t m = mission_number(), op = operation();
  const bool over = campaign_advance(op, m);
  if (next_one) {
    if (over) {
      if (managed_stock) finish_content_stock_campaign(*this);
      return nullptr;
    }
    auto* g = new CampaignGame(pilot, op, m);
    g->store.assign(store);
    g->store.add_all(store64);
    std::memcpy(g->b4, b4, sizeof b4);
    if (Store* salvage = h.mission_salvage()) g->store3c.assign(*salvage);
    else g->store3c.clear();  // [0x00609990] is always set in the exe
    g->slot.copy_from(slot);
    g->a8 = (m == 1 || (op == 4 && m == 4)) ? 3 : a8;
    g->c4 = c4;
    g->copy_lancemates(*this);
    if (managed_stock) carry_content_stock_campaign(*this, *g);
    else if (save) h.save_world(g->path(), 2);
    b0 = 0;
    g->b0 = 1;
    merge_salvage();
    return g;
  }
  store64.clear();
  store50.clear();
  for (int32_t i = 0; i < slot_count(); ++i) {
    GameSlot* s = slot_at(i);
    if (s && s->mech != -1) {
      store50.add_mech(s->mech);
      store50.add_config(s->config);
    }
  }
  ac = a8;
  store78.clear();
  b0 = 0;
  return this;
}

// Mech3.exe 1.2 @ 0x00435110
GameSlot* CampaignGame::slot_at(int32_t i) {
  if (i == 0) return &slot;
  const auto n = static_cast<uint32_t>(lancemates.size());
  if (n != 0 && i > 0 && static_cast<uint32_t>(i) <= n) return lancemates[static_cast<size_t>(i) - 1];
  return nullptr;
}

// Mech3.exe 1.2 @ 0x004351c0
void CampaignGame::set_lancemate(int32_t i, GameSlot* s) {
  if (static_cast<size_t>(i) == lancemates.size()) {
    lancemates.push_back(s);  // 0x0049ced0
    return;
  }
  lancemates[static_cast<size_t>(i)]->x14 = s->x14;
  delete s;  // 0x00433d60
}

// Mech3.exe 1.2 @ 0x00435180: the player and the lance's first +0x9c
// lancemates (unsigned: a negative +0x9c takes them all).
int32_t CampaignGame::slot_count() const {
  const auto n = static_cast<uint32_t>(lancemates.size());
  const auto want = static_cast<uint32_t>(x9c);
  return static_cast<int32_t>((n < want ? n : want) + 1);
}

// Mech3.exe 1.2 @ 0x00435240: with `del` the lancemates deleted (0x00434270),
// else their +0x14 cleared.
void CampaignGame::reset_lancemates(bool del) {
  if (!del) {
    for (GameSlot* s : lancemates) s->x14 = 0;
    return;
  }
  for (GameSlot*& s : lancemates) delete s, s = nullptr;
  lancemates.clear();
}

// Mech3.exe 1.2 @ 0x00434300: the old ones deleted, a copy of each (0x00433cf0).
void CampaignGame::copy_lancemates(const CampaignGame& from) {
  reset_lancemates(true);
  for (const GameSlot* s : from.lancemates) {
    auto* c = new GameSlot(-1, nullptr, 0);
    c->copy_from(*s);
    lancemates.push_back(c);
  }
}

// Mech3.exe 1.2 @ 0x00434950
const char* CampaignGame::path_for(const char* pilot_name) {
  char buf[0x200];
  if (active_content() && (!content_run.empty() || !content_mission.empty())) {
    if (!cc::content::valid_campaign_run_id(content_run))
      cc::content::fail("pilot-run", "Managed campaign path requires its expected run identity");
    std::snprintf(buf, sizeof buf, "pilots//games//%s%02d%02d-%s.mw3", pilot_name,
                  operation(), mission_number(), content_run.c_str());
  } else {
    std::snprintf(buf, sizeof buf, "pilots//games//%s%02d%02d.mw3", pilot_name, operation(), mission_number());
  }
  text = buf;
  return text.c_str();
}

// Mech3.exe 1.2 @ 0x00434980
const char* CampaignGame::path() { return path_for(pilot->name); }

// Mech3.exe 1.2 @ 0x00435330 (0x00576080: _access)
bool CampaignGame::readable() {
  if (!content_mission.empty()) return zutl::exists((std::string(path()) + ".ccsave").c_str());
  return zutl::exists(path());
}

// Mech3.exe 1.2 @ 0x004352d0: a game read from the pilot's file (not the one
// the last mission's end made, +0xb0), past Op 1 Mission 1 and not Op 4
// Mission 4, whose file is there: zSave reads it (kind 2) into the world.
bool CampaignGame::restore() {
  if (!content_mission.empty()) return restore_content_campaign(*this);
  if (b0 || mission == 1 || (campaign == 4 && mission == 4) || !readable()) return false;
  return pilot_hooks().load_world(path(), 2);
}

// Mech3.exe 1.2 @ 0x00434900: message 0x21d, "Op %1!d!, Mission %2!d!".
const char* CampaignGame::game_text(int32_t slot) {
  if(content_mission.empty() || content_stock_campaign(*this))return slot==0x0c?name():nullptr;
  const auto content=active_content();const auto* mission=content?content->find("mission",content_mission):nullptr;
  if(!mission)return nullptr;
  if(slot==0x0c)text=mission->data.value("name",mission->id);
  else if(slot==0x10)text=mission->data.value("description",std::string{});
  else if(slot==0x08) {
    const auto* world=content->find("world",mission->data.at("world").get<std::string>());
    text=world->data.value("name",world->id);
  } else if(slot==0x04)text="Authored mission";
  else return nullptr;
  return text.c_str();
}
const char* CampaignGame::name() {
  if (!content_mission.empty() && !content_stock_campaign(*this)) {
    text = content_campaign_label(*this);
    return text.c_str();
  }
  text = pilot_hooks().format_ints(0x21d, {operation(), mission_number()});
  return text.c_str();
}

// Mech3.exe 1.2 @ 0x00436650
ActionGame::ActionGame(Pilot* p) : Game(p) {
  store.full = 1;
  pilot_hooks().store_fill(store, kInstantAction);
}

// Mech3.exe 1.2 @ 0x00436770
ActionGame::~ActionGame() {
  std::free(x38), std::free(x3c), std::free(x40);
}

// Mech3.exe 1.2 @ 0x004368b0
GameSlot* ActionGame::slot_at(int32_t i) {
  if (i == 0) return &slot;
  return i > 0 && i < 11 ? &lineup[i - 1] : nullptr;
}

// Mech3.exe 1.2 @ 0x00436a90
bool ActionGame::ally(int32_t i) const {
  if (i == 0) return true;
  return i > 0 && i < 11 && side[i - 1] != 0;
}

// Mech3.exe 1.2 @ 0x00436af0
bool ActionGame::remove(int32_t i) {
  if (i < 0 || i > count) return false;
  for (; i < count - 1; ++i) {
    lineup[i].copy_from(lineup[i + 1]);  // 0x00433d90
    side[i] = side[i + 1];
  }
  --count;
  return true;
}

// Mech3.exe 1.2 @ 0x00436ac0: from the last slot down, each enemy's out.
// It asks slot i's side (lineup[i - 1]) but removes lineup[i]: with the
// allies first, as the screen keeps them, that is the same slot or the one
// past the end (docs/mission-start.md section 13).
void ActionGame::drop_enemies() {
  for (int32_t i = count; i >= 0; --i)
    if (!ally(i)) remove(i);
}

// Mech3.exe 1.2 @ 0x00436840
GameSlot* ActionGame::add(int32_t mech, uint8_t s, const Config* config, int32_t paint) {
  if (count > 9) return nullptr;
  side[count] = s;
  GameSlot& g = lineup[count++];
  g.x0c = 0;          // 0x00433ee0
  g.mech = mech;      // 0x005a2ba0
  g.set_config(config);
  g.skin = paint;     // 0x005a2b90
  return &g;
}

// Mech3.exe 1.2 @ 0x004368e0, 0x00436950, 0x004369c0. FIX (bug): always
// reallocated (the exe keeps an old buffer for a text of up to 4 bytes,
// whatever the old buffer's size).
void ActionGame::set_text(char** field, const char* text) {
  if (!text) text = "";
  const size_t n = std::strlen(text) + 1;
  if (void* b = std::realloc(*field, n)) {
    *field = static_cast<char*>(b);
    std::memcpy(*field, text, n);
  }
}

// Mech3.exe 1.2 @ 0x00436b50
void ActionGame::save() {
  Game::save();
  std::FILE* f = pilot->file;
  const int32_t stats[8] = {player_kills, lancemate_kills, enemy_kills, attack_wins,
                            attack_losses, defend_wins, defend_losses, waves};
  for (int32_t v : stats) wr(f, &v, 4);
  ActionSetup& a = action_setup();
  wr(f, &a.g874, 4);
  wr(f, &a.g878, 4);
  wr(f, &a.g87c, 4);
  for (int32_t& v : a.g820) wr(f, &v, 4);
  for (int i = 0; i < 6; ++i) {
    wr(f, &a.g838[i], 4);
    wr(f, &a.g858[i], 4);
  }
}

// Mech3.exe 1.2 @ 0x00436cc0
void ActionGame::load() {
  Game::load();
  std::FILE* f = pilot->file;
  int32_t* stats[8] = {&player_kills, &lancemate_kills, &enemy_kills, &attack_wins,
                       &attack_losses, &defend_wins, &defend_losses, &waves};
  for (int32_t* v : stats) rd(f, v, 4);
  ActionSetup& a = action_setup();
  rd(f, &a.g874, 4);
  rd(f, &a.g878, 4);
  rd(f, &a.g87c, 4);
  for (int32_t& v : a.g820) rd(f, &v, 4);
  for (int i = 0; i < 6; ++i) {
    rd(f, &a.g838[i], 4);
    rd(f, &a.g858[i], 4);
  }
}

// Mech3.exe 1.2 @ 0x004353a0
MultiplayerGame::MultiplayerGame(Pilot* p) : Game(p) {
  store.full = 1;
  pilot_hooks().store_fill(store, kMultiplayer);
}

// Mech3.exe 1.2 @ 0x00435500
MultiplayerGame::~MultiplayerGame() = default;

// Mech3.exe 1.2 @ 0x00435690
void MultiplayerGame::save() {
  Game::save();
  std::FILE* f = pilot->file;
  wr(f, &kills, 4);
  wr(f, &deaths, 4);
  wr(f, &x70, 4);
  wr(f, &x74, 4);
  wr(f, &b8c, 1);
  wr(f, &b8d, 1);
  wr(f, &b8e, 1);
  wr(f, &ba8, 1);
  wr(f, &x90, 4);
  wr(f, &x94, 4);
  wr(f, &b8f, 1);
  wr(f, &x78, 4);
}

// Mech3.exe 1.2 @ 0x004357d0
void MultiplayerGame::load() {
  Game::load();
  std::FILE* f = pilot->file;
  rd(f, &kills, 4);
  rd(f, &deaths, 4);
  rd(f, &x70, 4);
  rd(f, &x74, 4);
  rd(f, &b8c, 1);
  rd(f, &b8d, 1);
  rd(f, &b8e, 1);
  rd(f, &ba8, 1);
  rd(f, &x90, 4);
  rd(f, &x94, 4);
  rd(f, &b8f, 1);
  rd(f, &x78, 4);
}

// Mech3.exe 1.2 @ 0x00437920, 0x00436500, 0x00437950, 0x00437930
const char* MultiplayerGame::game_text(int32_t slot) {
  switch (slot) {
    case 0x04: return pilot_hooks().message(0x10d);
    case 0x08: {
      if (x94 > 2) {  // an add-on's: the reader's mpinfo.zrd ([0x00605850] holds it)
        static std::string name;
        zrdr::Node* info = zrdr::load("mpinfo.zrd");
        const char* s = zrdr::get_string(info, "db_name");
        name = s ? s : "";
        if (info) zrdr::free_node(info);
        return name.c_str();
      }
      if (x90 >= 0 && x90 <= 3 && (x94 == 1 || x94 == 2)) return pilot_hooks().message(0x320 + x90 * 2 + x94 - 1);
      return pilot_hooks().message(0xd75);  // "Unknown"
    }
    case 0x0c: return znet::session().desc.name.c_str();
    case 0x10: return roster.c_str();
    default: return nullptr;
  }
}

// Mech3.exe 1.2 @ 0x00436470
void MultiplayerGame::set_roster(const std::vector<std::string>& lines) {
  roster.clear();
  for (const std::string& l : lines) roster += l + "\n";
}

void MultiplayerGame::set_world(int32_t value) {
  if(select_content_multiplayer(value,x90,x94)) {
    const auto content=active_content();
    const auto* mission=content->by_handle("mission",value&0x3fffffff);
    content_multiplayer_mission=mission->id;
  } else {
    content_multiplayer_mission.clear();x90=value&255;x94=(value>>8)&255;
  }
}

int32_t MultiplayerGame::world() const {
  if(!content_multiplayer_mission.empty()) {
    const auto content=active_content();const auto* mission=content?content->find("mission",content_multiplayer_mission):nullptr;
    if(!mission)cc::content::fail("multiplayer-mission", "Selected multiplayer mission is unavailable");
    return 0x40000000|mission->handle;
  }
  return x94<<8|x90;
}

// ---- the lobby's records (multiplayer-port.md, piece 11) ----

// Mech3.exe 1.2 @ 0x00495b10
LobbyPlayer* MultiplayerGame::lobby_player(uint32_t id) {
  for (auto& p : lobby_players)
    if (p->id == id) return p.get();
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00495b60
uint32_t MultiplayerGame::lobby_team_id(uint32_t player) {
  const LobbyPlayer* p = lobby_player(player);
  return p ? p->team : 0;
}

// Mech3.exe 1.2 @ 0x00495bb0
int32_t MultiplayerGame::lobby_skin(uint32_t player) {
  const LobbyPlayer* p = lobby_player(player);
  return p ? p->skin : 0;
}

// Mech3.exe 1.2 @ 0x00495c00
bool MultiplayerGame::in_team(uint32_t player, uint32_t team) {
  for (auto& p : lobby_players)
    if (p->id == player && p->team == team) return true;
  return false;
}

// Mech3.exe 1.2 @ 0x00495d20
LobbyTeam* MultiplayerGame::lobby_team(uint32_t id) {
  for (auto& t : lobby_teams)
    if (t->id == id) return t.get();
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00495a50, 0x00495c60, 0x00495d70
void MultiplayerGame::clear_lobby() {
  lobby_players.clear();
  lobby_teams.clear();
  score_lines.clear();
}

// Mech3.exe 1.2 @ 0x00435910
uint32_t MultiplayerGame::net_team(uint32_t player) {
  if (!multiplayer_team_play()) {
    const LobbyPlayer* p = lobby_player(player);
    return p ? static_cast<uint32_t>(p->order) : 0;
  }
  const LobbyTeam* t = lobby_team(lobby_team_id(player));
  return t ? static_cast<uint32_t>(t->skin) : 0;
}

// Mech3.exe 1.2 @ 0x00435960
int32_t MultiplayerGame::net_skin(uint32_t player) {
  if (!multiplayer_team_play()) return lobby_skin(player);
  const LobbyTeam* t = lobby_team(lobby_team_id(player));
  return t ? t->skin : 0;
}

// Mech3.exe 1.2 @ 0x004359b0
int32_t MultiplayerGame::add_kills(uint32_t player, int32_t n) {
  LobbyPlayer* p = lobby_player(player);
  if (!p) return -1;
  p->kills += n;
  rescore();
  return p->kills;
}

// Mech3.exe 1.2 @ 0x004359f0
int32_t MultiplayerGame::set_kills(uint32_t player, int32_t n) {
  LobbyPlayer* p = lobby_player(player);
  if (!p) return -1;
  p->kills = n;
  rescore();
  return p->kills;
}

// Mech3.exe 1.2 @ 0x00435a30
int32_t MultiplayerGame::kills_of(uint32_t player) {
  const LobbyPlayer* p = lobby_player(player);
  return p ? p->kills : -1;
}

// Mech3.exe 1.2 @ 0x00435a50
int32_t MultiplayerGame::score_of(uint32_t player) {
  if (!multiplayer_team_play()) return kills_of(player) - deaths_of(player);
  rescore();
  const LobbyTeam* t = lobby_team(lobby_team_id(player));
  return t && t->score ? t->score->score : -1;
}

// Mech3.exe 1.2 @ 0x00435ac0
int32_t MultiplayerGame::add_deaths(uint32_t player, int32_t n) {
  LobbyPlayer* p = lobby_player(player);
  if (!p) return -1;
  p->deaths += n;
  rescore();
  return p->deaths;
}

// Mech3.exe 1.2 @ 0x00435b00
int32_t MultiplayerGame::set_deaths(uint32_t player, int32_t n) {
  LobbyPlayer* p = lobby_player(player);
  if (!p) return -1;
  p->deaths = n;
  rescore();
  return p->deaths;
}

// Mech3.exe 1.2 @ 0x00435b40
int32_t MultiplayerGame::deaths_of(uint32_t player) {
  const LobbyPlayer* p = lobby_player(player);
  return p ? p->deaths : -1;
}

namespace {
uint32_t floor0(int32_t kills, int32_t deaths) {
  const int32_t s = kills - deaths;
  return s < 0 ? 0u : static_cast<uint32_t>(s);
}
}  // namespace

// Mech3.exe 1.2 @ 0x004361e0: each score kills less suicides, not under 0;
// a team's the sum of its players'. The order key puts each team's line
// over its players' (the +1), the teams by score, then the players.
void MultiplayerGame::rescore() {
  if (!multiplayer_team_play()) {
    for (auto& p : lobby_players) {
      if (!p->score) continue;
      const uint32_t s = floor0(p->kills, p->deaths);
      p->score->score = static_cast<int32_t>(s);
      p->score->key = ((static_cast<uint32_t>(p->skin) & 7) + (s & 0xfff) * 8) * 2;
    }
    return;
  }
  for (auto& t : lobby_teams) {
    if (!t->score) continue;
    t->score->score = 0;
    t->kills = 0;
    for (auto& p : lobby_players)
      if (p->team == t->id) {
        t->kills += p->kills;
        t->score->score += static_cast<int32_t>(floor0(p->kills, p->deaths));
      }
  }
  for (auto& t : lobby_teams) {
    if (!t->score) continue;
    const uint32_t ts = static_cast<uint32_t>(t->score->score) & 0xfff, skin = static_cast<uint32_t>(t->skin) & 7;
    t->score->key = (skin + (ts + ts * 0x1000) * 8) * 2 + 1;
    for (auto& p : lobby_players)
      if (p->team == t->id && p->score) {
        const uint32_t s = floor0(p->kills, p->deaths);
        p->score->score = static_cast<int32_t>(s);
        p->score->key = (skin + ((s & 0xfff) + ts * 0x1000) * 8) * 2;
      }
  }
}

// Mech3.exe 1.2 @ 0x00435b60: SCORES_LIST's lines (under the heading 0xdd6,
// "Player Score Kills Suicides"): the name and two numbers cut to their
// columns, then the suicides; a team's line its name and score. The best
// score and how many have it noted; the lines sorted by their keys
// (std::sort in the exe, its order of equal keys unspecified: stable here).
void MultiplayerGame::score_texts(int32_t name_width, int32_t number_width) {
  const auto left = [](const std::string& s, int32_t n) { return s.substr(0, n < 0 ? 0 : static_cast<size_t>(n)); };
  char buf[256];
  best = 0, best_count = 0;
  const auto line = [&](LobbyPlayer& p, const char* name_format) {
    ScoreLine* l = p.score;
    if (!l) return;
    if (l->score > best) best = l->score, best_count = 1;
    else if (l->score == best) ++best_count;
    std::snprintf(buf, sizeof buf, name_format, p.name.c_str());
    std::string t = left(buf, name_width) + " ";
    std::snprintf(buf, sizeof buf, "%d             ", l->score);
    t += left(buf, number_width);
    std::snprintf(buf, sizeof buf, "%d             ", p.kills);
    t += left(buf, number_width);
    std::snprintf(buf, sizeof buf, "%d", p.deaths);
    l->text = t + buf;
  };
  if (!multiplayer_team_play()) {
    for (auto& p : lobby_players) line(*p, "%s                        ");  // [0x005e3790]
  } else {
    for (auto& t : lobby_teams) {
      if (t->score) {
        std::snprintf(buf, sizeof buf, "%s ( %d )", t->name.c_str(), t->score->score);
        t->score->text = buf;
      }
      for (auto& p : lobby_players)
        if (p->team == t->id) line(*p, "  %s                  ");  // [0x005e37c0]
    }
  }
  std::stable_sort(score_lines.begin(), score_lines.end(),
                   [](const std::unique_ptr<ScoreLine>& a, const std::unique_ptr<ScoreLine>& b) { return a->key > b->key; });
}

// Mech3.exe 1.2 @ 0x00436e30
TrainingGame::TrainingGame(Pilot* p) : Game(p), lessons(std::make_unique<TrainingLessons>()) {
  pilot_hooks().store_fill(store, kTraining);
}

// Mech3.exe 1.2 @ 0x00436f10: its lessons freed (0x0045ef10).
TrainingGame::~TrainingGame() = default;

// Mech3.exe 1.2 @ 0x00437060, 0x004370d0
int32_t TrainingGame::name_id(int32_t i) {
  static const int32_t kNames[] = {0x21e, 0x222, 0x224, 0x226};
  return kNames[(i > 4 ? 4 : i < 1 ? 1 : i) - 1];
}
int32_t TrainingGame::text_id(int32_t i) {
  static const int32_t kTexts[] = {0x21f, 0x223, 0x225, 0x227};
  return kTexts[(i > 4 ? 4 : i < 1 ? 1 : i) - 1];
}

// Mech3.exe 1.2 @ 0x00437980, 0x00437990, 0x004379b0 (+0x38, 0x004487d0, is
// 0 in every training game, 0x00436e30): the briefing's texts.
const char* TrainingGame::text_of(int32_t slot, int32_t mission) {
  switch (slot) {
    case 0x04: return pilot_hooks().message(0x10b);
    case 0x0c: return pilot_hooks().message(name_id(mission));
    case 0x10: return pilot_hooks().message(text_id(mission));
    default: return nullptr;
  }
}

// Mech3.exe 1.2 @ 0x004378a0, 0x004487d0, 0x0040a840, 0x00437970
const char* ActionGame::game_text(int32_t slot) {
  switch (slot) {
    case 0x04: return pilot_hooks().message(0x10c);
    case 0x08: return x38;
    case 0x0c: return x3c;
    case 0x10: return x40;
    default: return nullptr;
  }
}

// Mech3.exe 1.2 @ 0x00436fe0
void TrainingGame::save() {
  Game::save();
  wr(pilot->file, &mission, 4);
  wr(pilot->file, passed, 4);
}

// Mech3.exe 1.2 @ 0x00437020
void TrainingGame::load() {
  Game::load();
  rd(pilot->file, &mission, 4);
  rd(pilot->file, passed, 4);
}

}  // namespace mech3
