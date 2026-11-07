// The mech lab's screen (docs/mechlab.md section 2), step 2: the screen,
// its own controls, the page and slot switches. Original file unknown
// (0x00448830..0x004561ff, between mech_data.cpp and mech_map.cpp).
#include "mech3/content_registry.h"
#include "mech3/lab_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <iterator>
#include <string>

#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "gamez/ztwod/twod_host.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zvideo/zvid_format.h"
#include "mech3/app_states.h"
#include "mech3/pilot.h"

namespace mech3 {

// mech_names.h (its Engine clashes with the app's): 0x00484670's message and 0x004846e0.
int32_t mech_name_message(int32_t mech);
float max_mass(int32_t mech);
int32_t internal_points(int32_t mech, int32_t column);    // 0x00485400..0x00485490
int32_t armor_points(int32_t tech, bool ferro, float tons);  // 0x00424760
constexpr int32_t kClan = 1, kAnyTech = 3;               // mech_names.h's Tech
int32_t lab_rounds(int32_t weapon, int32_t n);           // weapon.h (0x004b4900)
int32_t mech_tech(int32_t mech);                         // 0x00484720
int32_t equip_name_message(int32_t kind);                // 0x00484980's table (its text through message)
double engine_weight_of(int32_t index, uint8_t xl);      // 0x00432f40 (as on the x87)
int32_t engine_rating_of(int32_t index);                 // 0x00432fa0
int32_t engine_maker_message(int32_t index);             // 0x00432ec0's message (mech_salvage.h)
float engine_kph(int32_t engine, float mass, bool run);  // 0x00433050 (mech_data.h)
bool weapon_uses_ammo(int32_t weapon);                   // 0x00484960
int32_t weapon_count();                                  // 37, the plain PPC's 38th with zfix bugs

namespace {
int32_t g_none = -1;  // the tabs' and mech buttons' id until bound

// Mech3.exe 1.2 @ 0x00484670: a mech's name from Mech3Msg.dll, "notmech"
// out of range; CString::Format's "%s" gives "(null)" for none.
std::string display_name(Engine& e, int32_t mech) {
  if (mech < 0 || mech >= chassis_count()) return "notmech";
  if (const char* label = content_label("chassis", mech)) return label;
  const char* s = e.message(mech_name_message(mech));
  return s ? s : "(null)";
}

// MSVC 6's std::sort (VC98 <algorithm>), as the exe has it inlined: the
// comparator's calls are in the trace, so its order of comparisons matters.
template <typename It, typename Less>
void vc6_insert(It last, typename std::iterator_traits<It>::value_type v, Less less) {
  for (It m = last; less(v, *--m); last = m) *last = *m;
  *last = v;
}
template <typename It, typename Less>
void vc6_insertion_sort(It f, It l, Less less) {
  if (f == l) return;
  for (It m = f; ++m != l;) {
    auto v = *m;
    if (!less(v, *f)) {
      vc6_insert(m, v, less);
    } else {
      std::copy_backward(f, m, m + 1);
      *f = v;
    }
  }
}
template <typename T, typename Less>
T vc6_median(T a, T b, T c, Less less) {
  if (less(a, b)) return less(b, c) ? b : less(a, c) ? c : a;
  return less(a, c) ? a : less(b, c) ? c : b;
}
template <typename It, typename T, typename Less>
It vc6_partition(It f, It l, T piv, Less less) {
  for (;; ++f) {
    for (; less(*f, piv); ++f) {}
    for (; less(piv, *--l);) {}
    if (!(f < l)) return f;
    std::iter_swap(f, l);
  }
}
template <typename It, typename Less>
void vc6_sort_loop(It f, It l, Less less) {
  while (l - f > 16) {
    It m = vc6_partition(f, l, vc6_median(*f, *(f + (l - f) / 2), *(l - 1), less), less);
    if (l - m <= m - f) {
      vc6_sort_loop(m, l, less);
      l = m;
    } else {
      vc6_sort_loop(f, m, less);
      f = m;
    }
  }
}
template <typename It, typename Less>
void vc6_sort(It f, It l, Less less) {
  if (l - f <= 16) {
    vc6_insertion_sort(f, l, less);
    return;
  }
  vc6_sort_loop(f, l, less);
  vc6_insertion_sort(f, f + 16, less);
  for (f += 16; f != l; ++f) vc6_insert(f, *f, less);
}
}  // namespace

void LabLater::call(uint32_t slot, std::initializer_list<Arg> args) const {
  char key[24];
  std::snprintf(key, sizeof key, "lab.vt+0x%x", slot);
  const Arg* a = args.begin();
  switch (args.size()) {
    case 0: e->unported(key, {at}); break;
    case 1: e->unported(key, {at, a[0]}); break;
    default: e->unported(key, {at, a[0], a[1]}); break;
  }
}

uint32_t LabLater::ask(const char* key) const { return e->unported(key, {at}); }

void LabLater::call_fn(const char* key, std::initializer_list<Arg> args) const {
  const Arg* a = args.begin();
  if (args.size() == 0) e->unported(key, {at});
  else e->unported(key, {at, a[0]});
}

// ---------------------------------------------------------------------------
// The panels

// Mech3.exe 1.2 @ 0x0044b070
LabPanel::~LabPanel() {
  set_image(static_cast<zimg::Texture*>(nullptr));
  if (single) ztwod::host().image_release(single);
  if (twice) ztwod::host().image_release(twice);
  single = twice = nullptr;
}

// Mech3.exe 1.2 @ 0x0044b100
bool LabPanel::read(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool ok = ztwod::Image::read(node, dialog);
  if (!ok) return ok;
  if (const char* s = zrdr::get_string(node, "double")) twice = ztwod::host().image_get(s);
  if (const char* s = zrdr::get_string(node, "single")) single = ztwod::host().image_get(s);
  if (const char* s = zrdr::get_string(node, "double2")) twice2 = ztwod::host().image_get(s);
  return ok;
}

// Mech3.exe 1.2 @ 0x0044b180
void LabPanel::show_look(int32_t look) {
  if (look == 1) set_image(single);
  else if (look == 2) set_image(twice);
  else if (look == 3) set_image(twice2);
}

// ---------------------------------------------------------------------------
// The edit bar

// Mech3.exe 1.2 @ 0x0044a710
void LabEditBar::set_pos(int32_t px, int32_t py) {
  ztwod::Image::set_pos(label_x + px, label_y + py);
  if (!labels.empty()) labels[0]->set_pos(px, py);
}

// ---------------------------------------------------------------------------
// The pages' controls: their own steps

// Mech3.exe 1.2 @ 0x0044b230. Each kind is looked for by a probe item
// (0x004714c0, the first like it) and counted over the loadout (0x00473510);
// the row shows the first one's label (vt+0x1c).
void LabLoadoutList::refresh(Handle cfg) {
  clear_items();
  layout();
  if (!cfg) return;
  Engine& en = e_;
  const std::vector<Handle> items = en.lab_config_items(cfg);
  auto find = [&](const ConfigItem& probe, int32_t* n) -> Handle {
    Handle first = 0;
    *n = 0;
    for (Handle h : items) {
      const ConfigItem* it = en.item_of(h);
      if (!it || !same_item(it, &probe)) continue;
      if (!first) first = h;
      *n += it->count;
    }
    return first;
  };
  char line[0x100];
  for (int32_t w = 0; w < weapon_count(); ++w)  // FIX (bug): 0x25 in the exe; the plain PPC too (fixes.md)
    for (int32_t tech = 1; tech < 3; ++tech) {
      WeaponItem probe;  // 0x0044ec90(1)
      probe.count = 1, probe.weapon = w, probe.tech = tech;
      int32_t n = 0;
      const Handle it = find(probe, &n);
      if (!it) continue;
      if (weapon_uses_ammo(w)) {
        AmmoItem ammo;
        ammo.count = 1, ammo.weapon = w;
        int32_t rounds = 0;
        find(ammo, &rounds);  // (0 when none)
        std::snprintf(line, sizeof line, "%s",
                      en.message_format_isi(0x1c5, n, en.lab_item_label(it).c_str(), rounds).c_str());
      } else {
        std::snprintf(line, sizeof line, "%2d %s", n, en.lab_item_label(it).c_str());
      }
      add_item(line, 0);
    }
  add_item("", 0);
  const char* kind = en.message(en.lab_config_double_sinks(cfg) ? 0x1d0 : 0x1cf);
  std::snprintf(line, sizeof line, "%s", en.message_format_is(0xf4b, en.lab_config_sinks(cfg), kind ? kind : "").c_str());
  add_item(line, 0);
  for (int32_t k = 0; k < equipment_count(); ++k)
    for (int32_t tech = 1; tech < 3; ++tech) {
      EquipmentItem probe;
      probe.count = 1, probe.part = -1, probe.equipment = k, probe.tech = tech;
      int32_t n = 0;
      const Handle it = find(probe, &n);
      if (!it) continue;
      const int32_t q = equipment_family(en.lab_item_equipment(it));
      if (q == 1 || q == 2 || q == 9 || q == 10) continue;  // armour and sinks: their own lines
      std::snprintf(line, sizeof line, "%2d %s", n, en.lab_item_label(it).c_str());
      add_item(line, 0);
    }
}

// Mech3.exe 1.2 @ 0x00449180
LabChassisList::~LabChassisList() {
  for (FileRow* r : rows) {
    if (r->attached) {
      delete r->attached;
      r->attached = nullptr;
    }
  }
}
// Mech3.exe 1.2 @ 0x00449220
void LabChassisList::set_store(Handle s) {
  selected = 0;
  x218 = 0;
  store = s;
  refill();
}

// Mech3.exe 1.2 @ 0x00449160
void LabChassisList::set_sort(int32_t by) {
  sort = by;
  refill();
}

int32_t LabChassisList::mech_at(int32_t index) {
  if (index < 0 || index >= item_count()) return -1;
  const ConfigItem* it = e_.item_of(static_cast<Handle>(items[static_cast<size_t>(index)]->value));
  return it && it->kind() == 0 ? it->id() : -1;
}

// Mech3.exe 1.2 @ 0x00449a80
int32_t LabChassisList::find_mech(int32_t mech) {
  for (int32_t i = 0; i < item_count(); ++i) {
    const ConfigItem* it = e_.item_of(static_cast<Handle>(items[static_cast<size_t>(i)]->value));
    if (it && it->kind() == 0 && it->id() == mech) return i;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x00449b00
bool LabChassisList::select_mech(int32_t mech) {
  const int32_t i = find_mech(mech);
  FileList::select(i);  // 0x0048d0d0, not the lab's select
  return i != 0;
}

// Mech3.exe 1.2 @ 0x00449250: the rows from the store (0x00449570 on each
// item: a mech by its name), sorted (std::sort; 0x004496c0 compares), and
// the shown slot's mech chosen by its row before the sort.
void LabChassisList::refill() {
  Engine& e = e_;
  clear_items();
  layout();
  const LabState& lab = states().lab;
  const Handle slot = e.lab_slot(lab.pilot_, lab.slot_);
  if (!slot) return;
  const int32_t mech = e.lab_slot_mech(slot);
  if (e.pilot() && e.pilot_campaign(e.pilot()) && lab.slot_ != 0) add_item(e.message(0x23d), 0);
  int32_t found = 0;
  if (Store* s = store ? e.store_of(store) : nullptr) {
    for (const ConfigItem* it : s->items) {
      if (it->kind() != 0) continue;
      const std::string name = display_name(e, it->id());
      const int32_t n = item_count();
      items.push_back(new Item{strdup(name.c_str()), static_cast<int32_t>(e.item_handle(it))});
      layout();
      if (it->id() == mech) found = n;
    }
  }
  // 0x004496c0: by the mech's number (0, the names are looked up and not
  // used) or its tonnage then number (1); anything else keeps no order.
  // FIX (bug), zfix bugs: SORT_CHASSISNAME by the names it looks up, then the number.
  auto mech_of = [&](const Item* x) {
    const ConfigItem* it = e.item_of(static_cast<Handle>(x->value));
    return x->value && it ? it->id() : -1;
  };
  const int32_t by = sort;
  vc6_sort(items.begin(), items.end(),
           [&](const Item* a, const Item* b) { return chassis_before(e, by, mech_of(a), mech_of(b)); });
  FileList::select(found);  // 0x0048d0d0
}

// Mech3.exe 1.2 @ 0x004496c0
bool chassis_before(Engine& e, int32_t by, int32_t ma, int32_t mb) {
  const std::string na = display_name(e, ma), nb = display_name(e, mb);
  if (by == 0) {
    if (zfix::options().bugs && na != nb) return na < nb;
    return ma < mb;
  }
  if (by != 1) return false;
  if (max_mass(mb) != max_mass(ma)) return max_mass(ma) < max_mass(mb);
  return ma < mb;
}

// Mech3.exe 1.2 @ 0x0044f4c0 (0x0048ccf0, then 0x00449760): each mech's
// tonnage beside its row, the chosen row's in red.
void LabChassisList::layout() {
  StoreList::layout();
  auto tons = [&](FileRow* r, Handle font, bool chosen) {
    const int32_t k = r->index;  // 0x0048d960: the row's item, none out of range
    const ConfigItem* it =
        k >= 0 && k < item_count() ? e_.item_of(static_cast<Handle>(items[static_cast<size_t>(k)]->value)) : nullptr;
    if (!it || it->kind() != 0) {
      if (r->attached) r->attached->hide();
      return;
    }
    auto* t = static_cast<ztwod::Text*>(r->attached);
    if (!t) r->attached = t = new ztwod::Text;
    if (chosen) {
      t->show_for(-1.0f);
      t->set_font(font);
    } else {
      t->set_font(font);
      t->show_for(-1.0f);
    }
    t->set_pos(r->get_x() + 175, r->get_y());
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", static_cast<double>(max_mass(it->id())));
    // The chosen row's number is its own format: harmless, "%.1f" never prints a '%'.
    if (chosen) t->set_text(b);
    else t->set_text("%s", b);
  };
  const Handle green = ztwod::host().font_find("lab_large_greenR");
  for (FileRow* r : rows)
    if (!(r->flags & ztwod::kHidden)) tons(r, green, false);
  if (!selected_row || selected < top) return;
  const Handle red = ztwod::host().font_find("lab_large_redR");
  const uint32_t i = static_cast<uint32_t>(selected - top);
  if (i >= rows.size()) return;
  FileRow* r = rows[i];
  tons(r, red, true);
  selected_row->attached = r->attached;
}

// Mech3.exe 1.2 @ 0x00449b30: a row pressed. A changed loadout asks first
// (0x004ac150 0x137 / 0x1ed with the mech and the loadout).
void LabChassisList::select(int32_t index) {
  Engine& e = e_;
  LabScreen* s = e.lab_screen(states().lab.lab_screen_);
  if (s && s->config_edit.x38c != 0 && !s->config_edit.finish(false)) return;
  const int32_t mech = mech_at(index);
  const LabState& lab = states().lab;
  if (const Handle slot = e.lab_slot(lab.pilot_, lab.slot_)) {
    const Handle cfg = e.lab_slot_cfg(slot);
    if (cfg && mech == e.config_mech(cfg)) return;
    if (cfg && e.lab_config_modified(cfg)) {
      if (s) s->cursor_obj.erase();
      const int32_t m = e.config_mech(cfg);
      const std::string name = e.lab_config_name(cfg);
      const std::string mn = display_name(e, m);
      if (e.message_box2(1, 0x137, 0x1ed, mn.c_str(), name.c_str()) != 1) return;
    }
  }
  play_sound(1, 1.0f);
  FileList::select(index);
  states().lab.set_mech(mech);
}

// Mech3.exe 1.2 @ 0x0044b1c0: the 3D model's skin follows when one is up.
void LabSkinList::select(int32_t i) {
  CycleButton::select(i);
  Engine& e = e_;
  const LabState& lab = states().lab;
  if (lab.model_) e.lab_model_skin(lab.model_, i);
  if (e.lab_slot(lab.pilot_, 0)) e.lab_slot_set_skin(e.lab_slot(lab.pilot_, 0), i);
}

// Mech3.exe 1.2 @ 0x0044f390
bool LabConfigList::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool r = FileList::read_control(node, dialog);
  dialog->bind_button(&add, "CONFIG_ADD");
  dialog->bind_button(&del, "CONFIG_DELETE");
  dialog->bind_button(&save, "CONFIG_SAVE");
  dialog->bind_button(&rename, "CONFIG_RENAME");
  return r;
}

// Mech3.exe 1.2 @ 0x0044f400
void LabConfigList::show_for(float seconds) {
  FileList::show_for(seconds);
  add.show_for(seconds);
  del.show_for(seconds);
  rename.show_for(seconds);
  save.show_for(seconds);
}

// Mech3.exe 1.2 @ 0x0044f460
void LabConfigList::hide() {
  FileList::hide();
  add.hide();
  del.hide();
  rename.hide();
  save.hide();
}

Handle LabConfigList::value_at(int32_t i) const {
  return i >= 0 && i < item_count() ? static_cast<Handle>(items[static_cast<size_t>(i)]->value) : 0;
}

// Mech3.exe 1.2 @ 0x00449fb0
void LabConfigList::fill() {
  if (flags & ztwod::kHidden) return;
  Engine& e = e_;
  clear_items();
  layout();
  const LabState& lab = states().lab;
  const Handle slot = e.lab_slot(lab.pilot_, lab.slot_);
  if (!slot) return;
  const int32_t mech = e.lab_slot_mech(slot);
  const Handle dfn = e.mech_dfn_handle(mech);
  if (!dfn) return;
  const Handle list = e.lab_dfn_configs(dfn);
  const Handle store = lab.store_;
  LabScreen* s = e.lab_screen(lab.lab_screen_);
  if (!e.lab_slot_cfg(slot) && list && s) s->default_config(slot, store);
  const Handle cur = e.lab_slot_cfg(slot);
  for (Handle c : e.lab_list_configs(list)) {
    if (!c || e.lab_config_byte(c, 0x1e) || (lab.flag_ && e.lab_config_byte(c, 0x1c))) continue;
    std::string text = e.lab_config_name(c);
    if (!e.lab_store_full(store) && !e.lab_store_has_config(store, mech, cur, c)) {
      const char* m = e.message(0x23e);
      text += m ? m : "";
    }
    items.push_back(new Item{strdup(text.c_str()), static_cast<int32_t>(c)});
    layout();
  }
  select_config(cur);
  enable_buttons();
}

// Mech3.exe 1.2 @ 0x00449d40: every row of that name is chosen in turn (the last stays).
void LabConfigList::select_config(Handle config) {
  Engine& e = e_;
  if (!config || e.lab_config_modified(config)) {
    FileList::select(-1);
    return;
  }
  const std::string name = e.lab_config_name(config);
  for (int32_t i = 0; i < item_count(); ++i) {
    const Handle v = value_at(i);
    if (v && e.lab_config_name(v) == name) FileList::select(i);
  }
}

// Mech3.exe 1.2 @ 0x0044a1f0: CONFIG_ADD is never enabled; RENAME and
// DELETE for the player's own; SAVE with a loadout, not in multiplayer's lab.
void LabConfigList::enable_buttons() {
  if (flags & ztwod::kHidden) return;
  Engine& e = e_;
  const LabState& lab = states().lab;
  const Handle cur = e.lab_slot_cfg(e.lab_slot(lab.pilot_, lab.slot_));
  const Handle v = value_at(selected);
  add.enabled = 0;
  add.refresh();
  rename.enabled = v && e.lab_config_byte(v, 0x1c) ? 1 : 0;
  rename.refresh();
  del.enabled = v && e.lab_config_byte(v, 0x1c) ? 1 : 0;
  del.refresh();
  save.enabled = cur && !lab.flag_ ? 1 : 0;
  save.refresh();
}

// Mech3.exe 1.2 @ 0x00449c40: a modified loadout asks first (as 0x00449b30).
void LabConfigList::select(int32_t index) {
  Engine& e = e_;
  const LabState& lab = states().lab;
  LabScreen* s = e.lab_screen(lab.lab_screen_);
  if (s && s->config_edit.x38c != 0 && !s->config_edit.finish(false)) return;
  const Handle slot = e.lab_slot(lab.pilot_, lab.slot_);
  const Handle cfg = e.lab_slot_cfg(slot);  // (read unchecked)
  if (e.lab_config_modified(cfg)) {
    if (s) s->cursor_obj.erase();
    const std::string mn = display_name(e, e.config_mech(cfg));
    const std::string name = e.lab_config_name(cfg);
    if (e.message_box2(1, 0x137, 0x1ed, mn.c_str(), name.c_str()) != 1) return;
  }
  play_sound(1, 1.0f);
  FileList::select(index);
  e.lab_slot_set_config(slot, value_at(selected), lab.store_);
  if (s) s->refresh();
}

// Mech3.exe 1.2 @ 0x0044f340
void LabPart::on_left_up() {
  if (enabled && active) {
    ztwod::CheckButton::on_left_up();
    notify(1, 0);
  }
  notify(1, part);
}

// Mech3.exe 1.2 @ 0x0044f2c0
int32_t LabCheck::set_checked(int32_t on) {
  const int32_t old = ztwod::CheckButton::set_checked(on);
  for (ztwod::Text* t : labels) {
    if (on) t->hide();
    else t->show_for(-1.0f);
  }
  return old;
}

// ---------------------------------------------------------------------------
// UNASSIGNED_LIST (docs/mechlab.md, The unassigned list)

namespace {
// Mech3.exe 1.2 @ 0x00453770 and 0x00454580 (`armour` true): the tab's kind,
// and armour (equipment 1 armor, 2 armorff) only on the armour tab.
bool tab_item(Engine& e, Handle h, bool armour) {
  const LabScreen* s = e.lab_screen(states().lab.lab_screen_);
  const int32_t want = s ? s->unassigned.kind : 0;  // FIX (bug): read unchecked by the exe
  const int32_t kind = e.lab_item_kind(h);
  bool is_armour = false;
  if (e.lab_item_kind(h) == 3 && h) {
    e.lab_item_kind(h);
    const int32_t k = equipment_family(e.lab_item_equipment(h));
    is_armour = k == 1 || k == 2;
  }
  return kind == want && is_armour == armour;
}
bool kind_item(Engine& e, Handle h) { return tab_item(e, h, false); }
bool armour_item(Engine& e, Handle h) { return tab_item(e, h, true); }

// Mech3.exe 1.2 @ 0x004564a0: the sort's order, `by` 1 the mass (ammo by
// id), 2 the store's count (unlimited by id), 3 the slots; ties by id.
bool unassigned_before(Engine& e, int32_t by, const LabItem* a, const LabItem* b) {
  const Handle ia = a->object, ib = b->object;
  const int32_t ida = e.lab_item_id(ia), idb = e.lab_item_id(ib);
  switch (by) {
    case 1: {
      if (e.lab_item_kind(ia) == 2 || e.lab_item_kind(ib) == 2) break;
      const float wa = e.lab_item_mass(ia);
      const float wb = e.lab_item_mass(ib);
      if (!(wb < wa || wb > wa)) break;  // (fcomp: unordered as equal)
      const float wa2 = e.lab_item_mass(ia);  // both asked again
      return e.lab_item_mass(ib) > wa2;
    }
    case 2:
      if (!e.lab_item_unlimited(ia) && !e.lab_item_unlimited(ib)) {
        const int32_t ca = e.lab_item_count(ia), cb = e.lab_item_count(ib);
        if (ca != cb) return ca < cb;
      }
      break;
    case 3:
      if (a->count != b->count) return a->count < b->count;
      break;
    default:  // 0; nothing sets another
      break;
  }
  return ida < idb;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004535d0
LabUnassignedList::LabUnassignedList(Engine& e)
    : LabItemList(e), sort_item(e, 0x1b), sort_qty(e, 0x1c), sort_mass(e, 0x1d), sort_crit(e, 0x1e) {
  filter = kind_item;
}

namespace {
// Mech3.exe 1.2 @ 0x00456750 (ECX the mech, EDX the part): a part's most
// armour, twice its internal structure (0x00485400..0x00485490); the head 9.
int32_t part_armor_max(int32_t mech, int32_t part) {
  switch (part) {
    case 0: case 3: return 2 * internal_points(mech, 1);
    case 1: case 2: case 4: case 5: return 2 * internal_points(mech, 2);
    case 6: return 9;
    case 7: case 8: return 2 * internal_points(mech, 3);
    case 9: case 10: return 2 * internal_points(mech, 4);
    default: return 0;
  }
}
// Ferro-fibrous's slots by its tech: 7 Clan, else 14.
int32_t ferro_need(int32_t tech) { return tech == kClan ? 7 : 14; }
}  // namespace

// Mech3.exe 1.2 @ 0x00453840 (docs/mechlab.md, The item rules): the item of
// row `index` on the shown slot's loadout (ASSIGNED_LIST's +0x224), into the
// part ASSIGNED_LIST has chosen (none: no part to fit); never while the lab
// is read-only. The weight first, then the armour tab's or the others' rules.
bool LabUnassignedList::can_xa8(int32_t index) {
  Engine& en = e_;
  const LabState& lab = states().lab;
  if (lab.flag_) return false;
  LabScreen* s = en.lab_screen(lab.lab_screen_);  // 0x00453810: [0x00605a58] + 0x1a13c
  if (!s) return false;  // (never: the screen asks)
  const Handle cfg = s->assigned.x224;
  const int32_t part = s->assigned.part();
  const Handle item = object_at(index);
  const Handle slot = en.lab_slot(lab.pilot_, lab.slot_);
  const Handle dfn = en.mech_dfn_handle(slot ? en.lab_slot_mech(slot) : -1);
  if (!item) return false;
  const int32_t k = en.lab_item_kind(item);
  const int32_t equip = equipment_family(en.lab_item_equipment(item));
  const bool armour = k == 3 && (equip == 1 || equip == 2);
  const int32_t mech = en.config_mech(cfg);
  if (!armor_tab) {
    const float one = k == 2 ? 1.0f : en.lab_item_mass(item);  // a ton of ammo
    const float max = max_mass(mech);
    if (max < en.lab_config_weight(cfg) + one) return false;
  } else {
    // Armour: its points within the parts' most (all but the first three),
    // half a ton more when it is the kind on the mech, and the slots for
    // ferro-fibrous.
    if (!armour) return false;
    int32_t most = 0;
    for (int32_t p = 3; p < 11; ++p) most += part_armor_max(mech, p);
    const float tons = en.lab_config_armor_tons(cfg);
    const int32_t tech = en.lab_config_armor_tech(cfg);
    const int32_t same = en.lab_config_armor_ferro(cfg) ? 2 : 1;
    const int32_t itech = en.lab_item_tech(item);
    const float add = tech == itech && same == equip ? 0.5f : 0.0f;
    if (armor_points(itech, equip == 2, add + tons) > most) return false;
    if (max_mass(mech) < en.lab_config_weight(cfg) + add) return false;
    const int32_t left = en.lab_config_slots_left(cfg) + en.lab_config_ferro_slots(cfg);
    return (equip == 2 ? ferro_need(itech) : 0) <= left;
  }
  // 16 weapons, TAG among them; some equipment one only, or up to five.
  if ((k == 3 && equip == 13) || k == 1)
    if (en.lab_config_weapons(cfg) >= 16) return false;
  if (k == 3) {
    const int32_t n = en.lab_config_equipment(cfg, equip, kAnyTech);
    bool ok = true;
    switch (equip) {
      case 0: case 3: ok = n <= 4; break;                        // AMS, FCS
      case 4: case 8: case 12: case 13: case 14: ok = n == 0; break;  // BAP, ECM, MASC, TAG, TC
      case 6: ok = n == 0 && en.lab_config_equipment(cfg, 7, kAnyTech) == 0; break;  // C3, not with a slave
      case 7: ok = n == 0 && en.lab_config_equipment(cfg, 6, kAnyTech) == 0; break;
      default: break;
    }
    if (!ok) return false;
  }
  if (part == -1) return true;
  if (k == 3 && equip == 11 && (part == 2 || part == 3 || part == 6)) return false;  // jump jets
  const int32_t left = en.lab_config_slots_left(cfg);
  const int32_t free = en.lab_config_part_free(cfg, part);
  if (armour) {
    const int32_t rest = left - en.lab_config_ferro_slots(cfg);
    return equip == 1 ? 0 <= rest : ferro_need(en.lab_item_tech(item)) <= rest;
  }
  if (k == 3 && (equip == 9 || equip == 10)) {
    // Never: the engine holds 10 sinks (as in refill, discoveries.md).
    const int32_t limit = en.lab_config_sink_limit(cfg) - 10;
    if (limit > en.lab_config_sink_items(cfg)) return true;
    const int32_t n = en.lab_item_slots(item);
    return !(free < n) && !(left < n);
  }
  const int32_t n = en.lab_item_slots(item);
  if (free < n || left < n) return false;
  if (k != 1) return true;
  for (const Engine::LabFirepoint& f : en.lab_dfn_firepoints(dfn))  // a weapon needs a firepoint in the part
    if (f.part == part) return true;
  return false;
}

// ---------------------------------------------------------------------------
// ASSIGNED_LIST (docs/mechlab.md, The criticals page)

namespace {
// Mech3.exe 1.2 @ 0x00425db0 (ECX the part): its critical slots, 12 for the
// torsos and arms, 6 for the head and legs; 78 for the whole mech (-1).
int32_t part_slots(int32_t part) {
  switch (part) {
    case 1: case 2: case 3: case 7: case 8: return 12;
    case 4: case 5: case 6: return 6;
    case -1: return 0x4e;
    default: return 0;
  }
}
}  // namespace

bool LabAssignedList::editable() const {
  return x224 && e_.lab_config_byte(x224, 0x1c) && !states().lab.flag_;
}

// Mech3.exe 1.2 @ 0x00455d70
bool LabAssignedList::is_part_row(int32_t row) const {
  const Handle o = object_at(row);
  return o >= 1 && o <= 8;
}
// Mech3.exe 1.2 @ 0x00455df0
bool LabAssignedList::is_item_row(int32_t row) const { return object_at(row) != 0 && !is_part_row(row); }
// Mech3.exe 1.2 @ 0x00455e50
Handle LabAssignedList::item_at_row(int32_t row) const { return is_item_row(row) ? object_at(row) : 0; }
// Mech3.exe 1.2 @ 0x00455ec0
int32_t LabAssignedList::part_of_row(int32_t row) const {
  return is_part_row(row) ? static_cast<int32_t>(object_at(row)) : -1;
}
// Mech3.exe 1.2 @ 0x004560d0
int32_t LabAssignedList::row_of_part(int32_t p) const {
  if (!store || p == -1) return -1;
  for (int32_t i = 0; i < item_count(); ++i)
    if (part_of_row(i) == p) return i;
  return -1;
}

// Mech3.exe 1.2 @ 0x00455bf0: a text row in STATIC_FONT.
int32_t LabAssignedList::add_text(const char* text, int32_t n) {
  if (!text) return 0;
  const int32_t i = LabItemList::add_text(text, n);
  if (i >= 0 && i < item_count()) items[static_cast<size_t>(i)]->font = font_static;
  return i;
}

// Mech3.exe 1.2 @ 0x00455b60: an item in NORMAL_FONT when it can come off
// (STATIC_FONT otherwise).
int32_t LabAssignedList::add_object(Handle item, int32_t n) {
  const int32_t i = LabItemList::add_object(item, n);
  if (i >= 0 && i < item_count()) items[static_cast<size_t>(i)]->font = editable() ? font_normal : font_static;
  return i;
}

// Mech3.exe 1.2 @ 0x00455af0: SELECTED_FONT on the chosen row, else LARGE;
// the part listed from now on.
int32_t LabAssignedList::add_part(int32_t p) {
  const int32_t i = LabItemList::add_part(p);
  if (i >= 0 && i < item_count()) items[static_cast<size_t>(i)]->font = i == selected ? font_selected : font_large;
  listing = p;
  return i;
}

// Mech3.exe 1.2 @ 0x004559e0: the arrows step the chosen part in the list's
// order, head, centre, left and right torso, arms, legs; no wrapping at
// either end.
void LabAssignedList::scroll(int32_t by) {
  const bool down = by > 0;
  switch (part()) {
    case 1: choose(down ? 7 : 6); return;
    case 2: choose(down ? 3 : 8); return;
    case 3: choose(down ? 4 : 2); return;
    case 4: choose(down ? 5 : 3); return;
    case 5: choose(down ? 5 : 4); return;  // (the last: down stays)
    case 6: choose(down ? 1 : 6); return;  // (the first: up stays)
    case 7: choose(down ? 8 : 1); return;
    case 8: choose(down ? 2 : 7); return;
    default: choose(6); return;
  }
}

// Mech3.exe 1.2 @ 0x00455f30: part rows, and item rows the player can take off.
bool LabAssignedList::can_light(int32_t index) {
  const bool edit = editable();
  if (is_part_row(index)) return true;
  return edit && is_item_row(index);
}

// Mech3.exe 1.2 @ 0x00456130
void LabAssignedList::choose(int32_t p) { select(row_of_part(p)); }

// Mech3.exe 1.2 @ 0x00456150: the row chosen, the 3D view's part, the part
// buttons, and UNASSIGNED_LIST's rules again.
void LabAssignedList::pick(int32_t p) {
  selected = row_of_part(p);
  Engine& en = e_;
  en.lab_section_highlight(p);
  if (LabScreen* s = en.lab_screen(states().lab.lab_screen_)) {
    s->check_part(p);
    s->unassigned.refill();
  }
}

// Mech3.exe 1.2 @ 0x00455fb0: an item row takes the item back to the lab's
// store; a part row is chosen (the list from it, the buttons, the 3D view).
void LabAssignedList::select(int32_t index) {
  on_leave();
  on_enter();
  Engine& en = e_;
  const LabState& lab = states().lab;
  if (!is_part_row(index)) {
    if (is_item_row(index) && !lab.flag_) en.lab_take_off(*this, index);
    return;
  }
  if (index < 0 || index >= item_count()) return;
  top = index;
  selected = index;
  refill();
  if (LabScreen* s = en.lab_screen(lab.lab_screen_)) s->check_part(part_of_row(index));
  en.lab_section_highlight(part_of_row(index));
}

// Mech3.exe 1.2 @ 0x004553f0 (and 0x00455360 on each item): with `spread`,
// the part's free slots take ferro-fibrous and then endo steel rows from the
// counts left; then the loadout's items on the part (sinks of no group none
// of its slots).
void LabAssignedList::add_part_items(bool spread, int32_t* ferro, int32_t* endo) {
  Engine& en = e_;
  if (spread) {
    int32_t free = en.lab_config_part_free(x224, listing);
    if (free != 0) {
      if (*ferro != 0) {
        const int32_t n = std::min(free, *ferro);
        if (n != 0) {
          free -= n;
          *ferro -= n;
          add_text(en.message(equip_name_message(2)), n);  // 0x00484980: ferro-fibrous's name
        }
      }
      if (free != 0 && *endo != 0) {
        const int32_t n = std::min(free, *endo);
        if (n != 0) {
          *endo -= n;
          add_text("Endo-Steel Internals", n);
        }
      }
    }
  }
  const int32_t mech = en.config_mech(x224);
  for (Handle it : en.lab_config_items(x224)) {
    if (!it || en.lab_item_location(it, mech) != listing) continue;
    int32_t n = en.lab_item_slots(it);
    if (en.lab_item_kind(it) == 3) {
      const int32_t k = equipment_family(en.lab_item_equipment(it));
      if ((k == 9 || k == 10) && en.lab_item_group(it) == -1) n = 0;
    }
    add_object(it, n);
  }
}

// Mech3.exe 1.2 @ 0x00455c50: each part row's used and total slots (all
// used when the loadout has none left), the line in the row's colour.
void LabAssignedList::show_slots() {
  Engine& en = e_;
  const int32_t left = en.lab_config_slots_left(x224);
  for (int32_t i = 0; i < item_count(); ++i) {
    if (!is_part_row(i)) continue;
    const int32_t p = part_of_row(i);
    if (p == -1) continue;
    LabItemRow* row = nullptr;  // 0x00452de0: the shown row of that item
    for (LabItemRow* r : rows)
      if (r->index >= 0 && r->index < item_count() && items[static_cast<size_t>(r->index)] == items[static_cast<size_t>(i)]) {
        row = r;
        break;
      }
    if (!row) continue;
    const int32_t used = left == 0 ? part_slots(p) : part_slots(p) - en.lab_config_part_free(x224, p);  // 0x00425e00
    row->show_slots(used, part_slots(p));
    const uint32_t c = ztwod::host().font_fields(row->named).color_top;
    row->line.color = zvid::pack_bytes(static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16));
  }
}

// Mech3.exe 1.2 @ 0x004554d0 (shown only). The chosen part (the head when
// none) stays chosen; the parts in the exe's order, head first, each with
// its fixed slots' rows; then the layout, the parts' slot counts and each
// item row's mass and count.
void LabAssignedList::refill() {
  if (flags & ztwod::kHidden) return;
  Engine& en = e_;
  int32_t chosen = part();
  if (chosen == -1) chosen = 6;
  clear();  // 0x00455d60
  if (!store) return;
  listing = -1;
  const Handle cfg = x224;
  if (!cfg) return;
  const LabState& lab = states().lab;
  const bool clan = mech_tech(en.lab_slot_mech(en.lab_slot(lab.pilot_, lab.slot_))) == 1;
  const int32_t xl = en.lab_config_byte(cfg, 0x28) ? (clan ? 2 : 3) : 0;  // an XL engine's side-torso slots
  en.lab_config_recount(cfg);
  const bool spread = en.lab_config_slots_left(cfg) == 0;
  int32_t ferro = en.lab_config_byte(cfg, 0x34) ? (en.lab_config_armor_tech(cfg) == 1 ? 7 : 14) : 0;
  int32_t endo = en.lab_config_byte(cfg, 0x20) ? (clan ? 7 : 14) : 0;
  auto text = [&](int32_t id, int32_t n) { add_text(en.message(id), n); };
  add_part(6);  // the head: cockpit, life support (2), sensors
  text(0x24e, 2);
  text(0x24f, 2);
  text(0x250, 1);
  add_part_items(spread, &ferro, &endo);
  add_part(1);  // the centre torso: the engine, the gyro
  {
    const std::string name = en.lab_engine_name(cfg);
    const char* t = en.message_format(0x251, name.c_str());  // (into 0x40 bytes)
    add_text(t ? std::string(t).substr(0, 0x3f).c_str() : "", 6);
  }
  text(0x252, 4);
  add_part_items(spread, &ferro, &endo);
  for (int32_t side : {7, 8}) {  // the side torsos: an XL engine's slots
    add_part(side);
    if (xl) text(0x253, xl);
    add_part_items(spread, &ferro, &endo);
  }
  for (int32_t arm : {2, 3}) {  // the arms: shoulder, upper arm
    add_part(arm);
    text(0x255, 1);
    text(0x256, 1);
    add_part_items(spread, &ferro, &endo);
  }
  for (int32_t leg : {4, 5}) {  // the legs: hip, upper leg, lower leg, foot
    add_part(leg);
    text(0x2d5, 1);
    text(0x257, 1);
    text(0x258, 1);
    text(0x259, 1);
    add_part_items(spread, &ferro, &endo);
  }
  pick(chosen);
  layout();
  show_slots();
  const Handle font = lab.flag_ ? font_count : font_mass;
  for (LabItemRow* row : rows) {
    if (row->flags & ztwod::kHidden) continue;
    if (is_part_row(row->index)) continue;
    const Handle it = object_at(row->index);
    if (!it) continue;
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", en.lab_item_weight(it, -1));
    row->mass.set_text("%s", b);
    row->mass.set_font(font);
    row->mass.show_for(-1.0f);
    if (!en.lab_item_unlimited(it)) {
      row->qty.set_text("%d", en.lab_item_count(it));
      row->qty.set_font(font);
      row->qty.show_for(-1.0f);
    }
  }
}

// Mech3.exe 1.2 @ 0x0044d3e0
void LabScreen::check_part(int32_t p) {
  part_head.set_checked(p == 6);
  for (LabPart& b : parts) b.set_checked(p == b.part);
}

// Engine's take-off by default: what the lab test leaves out (its loadouts
// are stand-ins).
void Engine::lab_take_off(LabAssignedList& list, int32_t index) {
  (void)list;
  unported("lab.take_off", {static_cast<uint32_t>(index)});
}

// Mech3.exe 1.2 @ 0x00455fb0's item row: all of the item back to the lab's
// store ([0x00605a80]) and off the shown slot's loadout, then the refresh.
void lab_take_off(Engine& en, LabAssignedList& list, int32_t index) {
  const LabState& lab = states().lab;
  const Handle slot = en.lab_slot(lab.pilot_, lab.slot_);  // 0x0044e680
  Config* cfg = en.lab_config_of(slot ? en.lab_slot_cfg(slot) : 0);
  auto* item = const_cast<ConfigItem*>(en.item_of(list.item_at_row(index)));
  Store* store = en.store_of(lab.store_);
  if (cfg && item && store && store->add_copy(item, item->count)) {
    cfg->remove(item, item->count);
    cfg->modified = 1;
    cfg->recount();
    list.play_sound(1, 1.0f);
  }
  en.lab_page_refresh(lab.lab_screen_);
}

// Engine's put-on by default: the lab test's stand-in for +0xac.
void Engine::lab_put_on(LabUnassignedList& list, int32_t index) { list.call(0xac, {static_cast<uint32_t>(index)}); }

uint8_t& lab_firepoint_toggle() {
  static uint8_t toggle = 1;
  return toggle;
}

// Mech3.exe 1.2 @ 0x00454600. Ammo goes on by the lab's rounds; weapons
// past 16 are refused with a message; armour goes on half a ton at a time,
// or replaces the mech's other kind (the old back to the store, as much of
// the new as there was of the old, the sections spread anew when over);
// sinks of the other kind are refused, and jump jets past the mech's
// limit; then a weapon takes a firepoint in the chosen part, alternately the
// last and the first that fits (missiles only those that scatter).
void lab_put_on(Engine& en, LabUnassignedList& list, int32_t index) {
  list.on_leave();
  list.on_enter();
  const Handle ih = list.object_at(index);
  if (!ih || !list.can_xa8(index)) return;
  const LabState& lab = states().lab;
  // 0x0044dc80: the shown slot (0x0044e680) has a loadout and the lab can change it.
  const Handle shown = en.lab_slot(lab.pilot_, lab.slot_);
  if (!shown || !en.lab_slot_cfg(shown) || lab.flag_) return;
  Store* store = en.store_of(list.store);
  Config* cfg = en.lab_config_of(en.lab_slot_cfg(en.lab_slot(lab.pilot_, lab.slot_)));
  auto* item = const_cast<ConfigItem*>(en.item_of(ih));  // (the store's own item)
  if (!store || !cfg || !item) return;  // (never: the rule passed)
  int32_t n = 1;
  bool put = true;
  const int k = item->kind();
  const int32_t equipment_id = k == 3 ? static_cast<EquipmentItem*>(item)->equipment : -1;
  const int32_t equip = equipment_family(equipment_id);
  const int32_t tech = k == 3 ? static_cast<EquipmentItem*>(item)->tech : 0;
  if (k == 2) {
    n = lab_rounds(static_cast<AmmoItem*>(item)->weapon, store->full ? 100000 : item->count);  // 0x004b4900
  } else if (k == 1) {
    if (cfg->weapon_count() >= 16) {
      put = false;
      en.lab_message("Only 16 weapons are allowed");
    }
  } else if (k == 3 && (equip == 1 || equip == 2)) {
    put = false;
    EquipmentItem probe;  // 0x0041ec00
    probe.count = 1, probe.equipment = equipment_id, probe.tech = tech;
    if (ConfigItem* same = cfg->find_like(&probe)) {  // half a ton more
      if (++same->count < 0) same->count = 0;
      store->remove(item, 1);
      cfg->armor_tons = cfg->armor_tons - -0.5f;
      if (cfg->armor_tons < 0.0f) cfg->armor_tons = 0.0f;
      cfg->modified = 1;
      cfg->recount();
    } else {
      ConfigItem* was = nullptr;
      for (ConfigItem* candidate : cfg->items) {
        if (candidate->kind() == 3 && equipment_family(candidate->id()) == (cfg->ferro ? 2 : 1)) {
          was = candidate;
          break;
        }
      }
      if (was) {
        const int32_t had = was->count;
        const int32_t take = item->b10 ? had : std::min(item->count, had);
        store->add_copy(was, had);
        cfg->remove(was, had);
        if (cfg->add_item(item, take)) store->remove(item, take);
        cfg->modified = 1;
        cfg->recount();
        int32_t sum = 0;  // 0x004513f0
        for (int32_t v : cfg->armor) sum += v;
        if (armor_points(cfg->armor_tech, cfg->ferro != 0, cfg->armor_tons) - sum < 0) cfg->spread_armor(cfg->mech);
      } else if (cfg->add_item(item, 1)) {
        store->remove(item, 1);
        cfg->modified = 1;
        cfg->recount();
      }
    }
    item = nullptr;
  } else if (k == 3 && (equip == 9 || equip == 10)) {
    EquipmentItem probe;  // 0x0044ec90(1): one kind of sink on a mech
    probe.count = 1, probe.equipment = equipment_id, probe.tech = tech;
    put = cfg->find_like(&probe) != nullptr || cfg->sink_items() == 0;
    if (active_content() && cfg->sink_items() != 0)
      put = cfg->equipment_count(equip, tech) == cfg->sink_items();
    if (!put) en.lab_message(en.message(0xf02));  // "Incompatable Heat Sink Type"
  } else if (k == 3 && equip == 11) {
    put = cfg->jump_jets() < cfg->jump_jet_limit();
    if (!put) {
      const int32_t limit = cfg->jump_jet_limit();
      const std::string name = display_name(en, cfg->mech);  // 0x00484670
      // 0xfc1 "%1 can only carry %2!d! jump jets" into 0x46 bytes, then the
      // text as CString::Format's format and the message's.
      std::string s = en.message_format_sd(0xfc1, name.c_str(), limit, true).substr(0, 0x45);
      en.lab_message(s.c_str());
    }
  }
  const int32_t part = en.lab_assigned_part();
  const Handle dfn = en.mech_dfn_handle(cfg->mech);
  int32_t at = part;
  if (item && item->kind() == 1) {
    const std::vector<Engine::LabFirepoint> fps = en.lab_dfn_firepoints(dfn);
    std::vector<int32_t> fit;  // the part's, by their place in the definition
    for (size_t i = 0; i < fps.size(); ++i)
      if (fps[i].part == part) fit.push_back(static_cast<int32_t>(i));
    if (fit.empty()) {
      put = false;
    } else {
      const int32_t w = static_cast<WeaponItem*>(item)->weapon;
      const bool missile = weapon_is_missile(w);
      const auto ok = [&](int32_t i) { return !missile || fps[static_cast<size_t>(fit[static_cast<size_t>(i)])].radius > 0.0f; };
      int32_t chosen = -1;
      const int32_t count = static_cast<int32_t>(fit.size());
      if (lab_firepoint_toggle()) {  // the last that fits
        lab_firepoint_toggle() = 0;
        for (int32_t i = 0; i < count; ++i)
          if (ok(i)) chosen = i;
      } else {  // the first
        lab_firepoint_toggle() = 1;
        for (int32_t i = count - 1; i >= 0; --i)
          if (ok(i)) chosen = i;
      }
      // None that fits leaves the part as its "firepoint" (the exe's).
      if (chosen != -1) {
        put = true;
        at = fit[static_cast<size_t>(chosen)];
      }
    }
  }
  if (put && item) {
    if (ConfigItem* added = cfg->add_item(item, n)) {
      list.play_sound(1, 1.0f);  // 0x00452c80
      added->set_part(at);
      store->remove(item, n);
      cfg->modified = 1;
      cfg->recount();
    }
  }
  list.on_leave();
  list.on_enter();
  en.lab_page_refresh(lab.lab_screen_);  // 0x0044cf50
  list.on_leave();
  list.on_enter();
}

bool LabUnassignedList::call_bool(uint32_t slot, int32_t index) const {
  char key[24];
  std::snprintf(key, sizeof key, "lab.vt+0x%x", slot);
  return (e->unported(key, {at, index}) & 0xff) != 0;
}

// Mech3.exe 1.2 @ 0x00454db0
void LabUnassignedList::show_for(float seconds) {
  LabItemList::show_for(seconds);
  titles.show_for(seconds);
  weapon.show_for(seconds);
  ammo.show_for(seconds);
  equip.show_for(seconds);
  armor.show_for(seconds);
  sort_item.show_for(seconds);
  sort_mass.show_for(seconds);
  sort_qty.show_for(seconds);
  sort_crit.show_for(seconds);
  set_tab(kind, armor_tab != 0);
}

// Mech3.exe 1.2 @ 0x00454e70: the engine's table goes too.
void LabUnassignedList::hide() {
  LabItemList::hide();
  titles.hide();
  for (LabSelect* b : {&weapon, &ammo, &equip, &armor}) b->set_checked(0);
  for (LabSelect* b : {&weapon, &ammo, &equip, &armor}) b->hide();
  sort_item.hide();
  sort_mass.hide();
  sort_qty.hide();
  sort_crit.hide();
  if (LabScreen* s = e_.lab_screen(states().lab.lab_screen_)) s->engine_table.hide();
}

// Mech3.exe 1.2 @ 0x00454fc0: the base's reading, SHORT_SIZE, then its buttons.
bool LabUnassignedList::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool r = LabItemList::read_control(node, dialog);
  zrdr::get_ivec2(node, "SHORT_SIZE", short_size);
  long_size[0] = size[0];
  long_size[1] = size[1];
  down_dy = down->get_y() - size[1];
  dialog->bind_button(&weapon, "SELECT_WEAPON");
  dialog->bind_button(&ammo, "SELECT_AMMO");
  dialog->bind_button(&equip, "SELECT_EQUIP");
  dialog->bind_button(&armor, "SELECT_ARMOR");
  dialog->bind_button(&sort_item, "SORT_ITEM");
  dialog->bind_button(&sort_qty, "SORT_QTY");
  dialog->bind_button(&sort_mass, "SORT_MASS");
  dialog->bind_button(&sort_crit, "SORT_CRIT");
  dialog->bind_button(&titles, "ALIST_TITLES");
  return r;
}

// Mech3.exe 1.2 @ 0x00454f60: the base's, in NORMAL_FONT.
int32_t LabUnassignedList::add_object(Handle item, int32_t n) {
  const int32_t i = LabItemList::add_object(item, n);
  if (i >= 0 && i < item_count()) items[static_cast<size_t>(i)]->font = font_normal;
  // (else the exe writes the font to address 0xc; add never fails)
  return i;
}

// Mech3.exe 1.2 @ 0x004550d0: a tab's button checked; the equipment and
// armour tabs take SHORT_SIZE (the engine's table under the equipment's).
void LabUnassignedList::set_tab(int32_t k, bool arm) {
  LabScreen* s = e_.lab_screen(states().lab.lab_screen_);
  if (kind != k || arm) top = 0;
  kind = k;
  armor_tab = arm ? 1 : 0;
  if ((flags & ztwod::kHidden) || !s) return;
  for (LabSelect* b : {&weapon, &ammo, &equip, &armor}) b->set_checked(0);
  const int32_t* use = long_size;
  if (k == 1) {
    weapon.set_checked(1);
  } else if (k == 2) {
    ammo.set_checked(1);
  } else if (k == 3) {
    equip.set_checked(armor_tab == 0);
    armor.set_checked(armor_tab);
    use = short_size;
  } else {
    return;
  }
  size[0] = use[0];
  size[1] = use[1];
  if (down) down->set_pos(down->get_x(), size[1] + down_dy);
  if (k == 3 && !armor_tab) s->engine_table.show_for(-1.0f);
  else s->engine_table.hide();
  s->panel_right.show_look(1);
}

// Mech3.exe 1.2 @ 0x00453f00
void LabUnassignedList::set_sort(int32_t by) {
  sort = by;
  refill();
}

// Mech3.exe 1.2 @ 0x00453f20: ALIST_TITLES's slot counts, the tab's items
// sorted, each row's mass and count (greyed when +0xa4 says it cannot go
// on), then the screen's controls for the tab: the armour tab hides the
// assigned list and the parts for ARMOR_TITLES.
void LabUnassignedList::refill() {
  Engine& en = e_;
  const LabState& lab = states().lab;
  const Handle cfg = en.lab_slot(lab.pilot_, lab.slot_) ? en.lab_slot_cfg(en.lab_slot(lab.pilot_, lab.slot_)) : 0;
  if (!(flags & ztwod::kHidden)) {
    const int32_t left = cfg ? en.lab_config_slots_left(cfg) : 0;
    const int32_t ferro = cfg ? en.lab_config_ferro_slots(cfg) : 0;
    const int32_t endo = cfg ? en.lab_config_endo_slots(cfg) : 0;
    // FIX (bug): a missing label is a call through null in the exe (a broken dialog file).
    auto label = [&](size_t i) { return i < titles.labels.size() ? titles.labels[i] : nullptr; };
    if (ztwod::Text* t = label(0)) t->set_text_id(0xf5b, left);
    size_t i = 1;
    if (endo) {
      if (ztwod::Text* t = label(1)) t->set_text_id(0x24c, endo);
      if (ztwod::Text* t = label(1)) t->show_for(-1.0f);
      i = 2;
    }
    if (ferro) {
      if (ztwod::Text* t = label(i)) t->set_text_id(0x24d, ferro);
      if (ztwod::Text* t = label(i)) t->show_for(-1.0f);
      ++i;
    }
    for (; i < 3; ++i)
      if (ztwod::Text* t = label(i)) t->hide();
  }
  set_tab(kind, armor_tab != 0);
  if ((flags & ztwod::kHidden) || !store) return;
  filter = armor_tab ? armour_item : kind_item;
  clear();
  if (Store* st = en.store_of(store))
    for (const ConfigItem* it : st->items) {
      const Handle h = en.item_handle(it);
      if (!filter || filter(en, h)) add_object(h, -1);
    }
  // Never true: the engine holds 10 sinks and the test is fewer than none
  // (docs/discoveries.md); it would list the store's sinks with no slots.
  if (en.lab_config_sink_limit(cfg) - 10 > en.lab_config_sink_items(cfg) && !armor_tab && kind == 3) {
    for (LabItem* it : items) {
      const Handle h = it->object;
      if (!h || en.lab_item_kind(h) != 3) continue;
      en.lab_item_kind(h);
      const int32_t k = equipment_family(en.lab_item_equipment(h));
      if (k == 9 || k == 10) it->count = 0;
    }
  }
  vc6_sort(items.begin(), items.end(), [&](LabItem* a, LabItem* b) { return unassigned_before(en, sort, a, b); });
  layout();
  for (LabItemRow* row : rows) {
    if (row->flags & ztwod::kHidden) continue;
    const int32_t i = row->index;
    Handle grey = 0;
    if (!can_light(i)) {
      grey = font_grey;
      row->set_font(font_grey);
    }
    const Handle h = object_at(i);
    if (en.lab_item_kind(h) == 2) {
      en.lab_item_kind(h);
      row->mass.set_text_plain("1.0");  // a ton of ammo
    } else {
      char b[32];
      std::snprintf(b, sizeof b, "%.1f", static_cast<double>(en.lab_item_mass(h)));
      row->mass.set_text("%s", b);
    }
    row->mass.set_font(grey ? font_count : font_mass);
    row->mass.show_for(-1.0f);
    if (!en.lab_item_unlimited(h)) {
      row->qty.set_text("%d", en.lab_item_count(h));
      row->qty.set_font(grey ? font_count : font_mass);
      row->qty.show_for(-1.0f);
    }
    if (grey) row->count.set_font(font_count);
  }
  LabScreen* s = en.lab_screen(lab.lab_screen_);
  if (!s) return;  // FIX (bug): read unchecked by the exe
  LabAssignedList& as = s->assigned;
  if ((as.flags & ztwod::kHidden) && !armor_tab) {
    as.show_for(-1.0f);
    as.refill();
  } else if (armor_tab) {
    en.lab_section_highlight(-1);
    as.hide();
  }
  for (LabPart* p : {&s->part_head, &s->parts[0], &s->parts[1], &s->parts[2], &s->parts[3], &s->parts[4],
                     &s->parts[5], &s->parts[6]}) {
    if (armor_tab) p->hide();
    else p->show_for(-1.0f);
  }
  if (armor_tab) s->armor_titles->show_for(-1.0f);
  else s->armor_titles->hide();
}

// ---------------------------------------------------------------------------
// The weapon groups page (docs/mechlab.md, The weapon groups page)

// Mech3.exe 1.2 @ 0x0044dfb0
void LabLocationTag::set_pos(int32_t px, int32_t py) {
  HudPanel::set_pos(px, py);
  const int32_t w = image ? image->width : 0;
  text.set_pos(px - 5 + w, py);
}

// Mech3.exe 1.2 @ 0x0044dff0
void LabLocationTag::update(float dt) {
  if (flags & ztwod::kHidden) return;
  HudPanel::update(dt);
  text.update(dt);
}

// Mech3.exe 1.2 @ 0x0044dee0
LabWeaponList::~LabWeaponList() {
  for (FileRow* r : rows) {
    delete r->attached;
    r->attached = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x0044df70
bool LabWeaponList::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool r = FileList::read_control(node, dialog);
  font_static = ztwod::host().font_find(zrdr::get_string(node, "STATIC_FONT"));
  return r;
}

// Mech3.exe 1.2 @ 0x0044e630
int32_t LabWeaponList::group_of(int32_t row) const {
  const int32_t v = row >= 0 && row < item_count() ? items[static_cast<size_t>(row)]->value : 0;
  return v < 0 || v > 4 ? -1 : v;
}

// Mech3.exe 1.2 @ 0x0044e560 (the grouped's; the other's 0x0040bad0)
bool LabWeaponList::can_choose(int32_t i) { return !grouped || group_of(i) != -1; }
// Mech3.exe 1.2 @ 0x0044e540
bool LabWeaponList::can_roll(int32_t i) { return !grouped || !can_choose(i); }

// Mech3.exe 1.2 @ 0x0044e450 (the ungrouped's: the row's value) and
// 0x0044e5e0 (the grouped's: not a group's row)
Handle LabWeaponList::weapon_at(int32_t row) const {
  if (grouped && group_of(row) != -1) return 0;
  return row >= 0 && row < item_count() ? static_cast<Handle>(items[static_cast<size_t>(row)]->value) : 0;
}

// Mech3.exe 1.2 @ 0x0044e3f0 (the ungrouped's: into the chosen group, -1
// when none is) and 0x0044e490 (the grouped's: out of its group; a group's
// row is chosen).
void LabWeaponList::select(int32_t index) {
  Engine& en = e_;
  LabScreen* s = en.lab_screen(states().lab.lab_screen_);
  if (!s) return;  // (the exe's [0x00605a58], always there)
  const int32_t group = grouped ? -1 : s->grouped.group_of(s->grouped.selected);
  const Handle it = weapon_at(index);
  if (!it) {
    if (grouped) FileList::select(index);
    return;
  }
  play_sound(1, 1.0f);  // 0x0048d3b0
  en.lab_set_item_group(it, group);
  s->refresh_groups();
}

// Mech3.exe 1.2 @ 0x0044e3d0
void LabWeaponList::add_weapon(Handle item) {
  const std::string label = e_.lab_item_label(item);
  add_item(label.c_str(), static_cast<int32_t>(item));
}

// Mech3.exe 1.2 @ 0x0044e580
void LabWeaponList::add_group(int32_t g) {
  const char* t = e_.message_format_n(0x24b, g + 1);  // (into 0x40 bytes)
  add_item(t ? std::string(t).substr(0, 0x3f).c_str() : "", g);
}

// Mech3.exe 1.2 @ 0x0044e4e0
void LabWeaponList::select_group(int32_t g) {
  if (g < 0 || g > 4) g = 0;
  for (int32_t i = 0; i < item_count(); ++i)
    if (group_of(i) == g) select(i);
}

// Mech3.exe 1.2 @ 0x0044f240
void LabWeaponList::layout() {
  FileList::layout();
  lay_rows();
}

// Mech3.exe 1.2 @ 0x0044e020: down from OFFSET, each shown row (or the
// selected one's place) a row height and 5 apart: a weapon's row in
// NORMAL_FONT with its tag (the part it is on, 172 x 12, 3 to the left), a
// group's in STATIC_FONT, 5 lower after the first, the selected row's text
// and BAR on it.
void LabWeaponList::lay_rows() {
  constexpr int32_t kTagW = 172, kTagH = 12;  // [0x005e42b8], [0x005e42bc]
  int32_t y = get_y() + offset_y;
  if (rows.empty()) return;
  Engine& en = e_;
  const LabState& lab = states().lab;
  if (!lab.lab_screen_) return;
  const Handle slot = en.lab_slot(lab.pilot_, lab.slot_);
  if (!slot) return;
  const int32_t mech = en.lab_slot_mech(en.lab_slot(lab.pilot_, lab.slot_));
  int32_t i = 0;
  for (FileRow* row : rows) {
    const bool here = selected - top == i && selected_row && !(selected_row->flags & ztwod::kHidden);
    if ((row->flags & ztwod::kHidden) && !here) continue;
    const int32_t v = row->index >= 0 && row->index < item_count() ? items[static_cast<size_t>(row->index)]->value : 0;
    if (v >= 5 && v != 0) {  // a weapon
      row->set_pos(row->get_x(), y);
      row->set_font(font_normal);
      auto* tag = static_cast<LabLocationTag*>(row->attached);
      if (!tag) {
        tag = new LabLocationTag;
        row->attached = tag;
        tag->set_size(kTagW, kTagH);
        tag->set_color(zvid::pack_bytes(0x19, 0x19, 0x19));  // 0x0043aae0
        tag->set_opacity(1.0f);
      }
      tag->show_for(-1.0f);
      tag->set_pos(row->get_x() - 3, row->get_y());
      tag->text.set_font(font_normal);
      tag->text.align = 2;
      tag->text.set_text_plain(en.part_text(en.lab_item_location(static_cast<Handle>(v), mech)));  // 0x00484d70
    } else {  // a group
      row->set_font(font_static);
      if (row->attached) row->attached->hide();
      row->set_pos(row->get_x(), y + (i != 0 ? 5 : 0));
      if (here) {
        selected_row->set_pos(row->get_x(), row->get_y());
        if (bar_image) bar_image->set_pos(row->get_x() + bar_dx, row->get_y() + bar_dy);
      }
    }
    ++i;
    y = row->get_y() + kTagH + 5;
  }
}

// Mech3.exe 1.2 @ 0x0044dcf0: on the weapon groups page with a loadout
// shown, both lists emptied and refilled: the weapons of no group, then
// each group's row and its weapons; the chosen group kept.
void LabScreen::refresh_groups() {
  if (page != 4) return;
  Engine& e = engine();
  const LabState& lab = states().lab;
  // The shown slot's loadout, asked for again each time (0x0044e680 twice a read).
  auto shown = [&]() -> Handle {
    return e.lab_slot(lab.pilot_, lab.slot_) ? e.lab_slot_cfg(e.lab_slot(lab.pilot_, lab.slot_)) : 0;
  };
  if (!shown()) return;
  const Handle cfg = shown();
  const int32_t chosen = grouped.group_of(grouped.selected);
  shown();  // (its mech read, unused)
  ungrouped.clear_items();
  ungrouped.layout();
  grouped.clear_items();
  grouped.layout();
  const std::vector<Handle> items = e.lab_config_items(cfg);
  for (Handle it : items)
    if (e.lab_item_kind(it) == 1 && e.lab_item_group(it) == -1) ungrouped.add_weapon(it);
  for (int32_t g = 0; g < 5; ++g) {
    grouped.add_group(g);
    for (Handle it : items)
      if (e.lab_item_kind(it) == 1 && e.lab_item_group(it) == g) grouped.add_weapon(it);
  }
  grouped.select_group(chosen);
}

// ---------------------------------------------------------------------------
// The data tables and ENGINE_TABLE

// Mech3.exe 1.2 @ 0x0044f190
void LabRow::update(float dt) {
  ztwod::Object2D::update(dt);
  value.update(dt);
}
// Mech3.exe 1.2 @ 0x0044ef50
void LabRow::set_int(int32_t v, const char* unit) {
  if (unit) value.set_text("%d %s", v, unit);
  else value.set_text("%d", v);
}
// Mech3.exe 1.2 @ 0x0044ef00
void LabRow::set_int(const char* label, int32_t v, const char* unit) {
  set_text(label);
  set_int(v, unit);
}
// Mech3.exe 1.2 @ 0x0044ee80
void LabRow::set_float(const char* label, float v, const char* unit) {
  set_text(label);
  char b[16];
  std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
  if (unit) value.set_text(" %s", b, unit);
  else value.set_text("");
}

// Mech3.exe 1.2 @ 0x00457f90
void LabRow::set_float(float v, const char* unit) {
  char b[16];
  std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
  if (unit) value.set_text(" %s", b, unit);
  else value.set_text("");
}

// Mech3.exe 1.2 @ 0x0044f530
LabTable::~LabTable() {
  for (LabRow*& r : rows) {
    delete r;
    r = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x0044f040
void LabTable::add_rows(int32_t n) {
  if (static_cast<int32_t>(rows.capacity()) < n) rows.reserve(static_cast<size_t>(n < 0 ? 0 : n));
  while (static_cast<int32_t>(rows.size()) < n) rows.push_back(new LabRow);
}

// Mech3.exe 1.2 @ 0x00456c30: each row's label at OFFSET, its value WIDTH to
// the right, the next SPACING lower.
void LabTable::set_pos(int32_t px, int32_t py) {
  ztwod::Control::set_pos(px, py);
  const int32_t rx = spacing[0] + offset[0] + px;
  int32_t ry = offset[1] + py;
  for (LabRow* r : rows) {
    r->x = rx;
    r->y = ry;
    r->value.set_pos(width + rx, ry);
    ry += spacing[1];
  }
}

// Mech3.exe 1.2 @ 0x00456bc0
void LabTable::update(float dt) {
  if (flags & ztwod::kHidden) return;
  ztwod::Object2D::update(dt);
  for (LabRow* r : rows) r->update(dt);
}

// Mech3.exe 1.2 @ 0x00456a60: all flags but hidden cleared; the fonts, the
// layout's numbers, then the rows' fonts and places.
bool LabTable::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  ztwod::Control::read_control(node, dialog);
  flags &= ztwod::kHidden;
  const char* label = zrdr::get_string(node, "LABEL_FONT");
  const char* val = zrdr::get_string(node, "VALUE_FONT");
  const char* alt = zrdr::get_string(node, "ALTERNATE_FONT");
  zrdr::get_int(node, "WIDTH", &width);
  zrdr::get_ivec2(node, "SPACING", spacing);
  zrdr::get_ivec2(node, "OFFSET", offset);
  zrdr::get_ivec2(node, "POSITION", pos);
  font_label = ztwod::host().font_find(label);
  font_value = ztwod::host().font_find(val);
  font_alt = ztwod::host().font_find(alt);
  for (LabRow* r : rows) {
    r->set_font(font_label);
    r->value.set_font(font_value);
  }
  set_pos(pos[0], pos[1]);
  return true;
}

// Mech3.exe 1.2 @ 0x004569c0 (the table), the buttons, 0x0044f040(7)
LabEngineTable::LabEngineTable(Engine& e)
    : e_(e), faster(e, 0x13), slower(e, 0x14), type_right(e, 0x15), type_left(e, 0x16) {
  add_rows(7);
}

// Mech3.exe 1.2 @ 0x0044b830: TOGGLE_RIGHT makes faster and the right type
// button, TOGGLE_LEFT slower and the left one.
bool LabEngineTable::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  if (!node || !dialog) return false;
  const bool r = LabTable::read_control(node, dialog);
  if (zrdr::Node* n = zrdr::find(node, "TOGGLE_RIGHT")) {
    faster.read_control(n, dialog);
    faster.after_bind();
    type_right.read_control(n, dialog);
    type_right.after_bind();
  }
  if (zrdr::Node* n = zrdr::find(node, "TOGGLE_LEFT")) {
    slower.read_control(n, dialog);
    slower.after_bind();
    type_left.read_control(n, dialog);
    type_left.after_bind();
  }
  return r;
}

// Mech3.exe 1.2 @ 0x0044b770
void LabEngineTable::show_for(float seconds) {
  ztwod::Control::show_for(seconds);
  for (LabButton* b : {&faster, &slower, &type_right, &type_left}) b->show_for(-1.0f);
}
// Mech3.exe 1.2 @ 0x0044b7e0
void LabEngineTable::hide() {
  ztwod::Control::hide();
  for (LabButton* b : {&faster, &slower, &type_right, &type_left}) b->hide();
}

// Mech3.exe 1.2 @ 0x0044b4b0: the rating, maker, type, tons and critical
// slots; the buttons beside the rating's and the type's values. The type's
// are for the player's own loadouts, faster and slower within the
// definition's ratings (+0x10 the least, +0x14 the most).
void LabEngineTable::refresh(Handle cfg) {
  if (!cfg) return;
  Engine& e = e_;
  const int32_t index = e.lab_config_engine(cfg);
  const uint8_t xl = e.lab_config_byte(cfg, 0x28);
  // (0x004846e0 asked and dropped)
  const int32_t rating = engine_rating_of(index);
  rows[0]->set_text(e.message(0x23f));
  rows[0]->set_int(rating, nullptr);
  const char* m = e.message(engine_maker_message(index));  // 0x00432ec0: copied out
  const std::string maker = m ? m : "";
  rows[1]->set_text(e.message(0x240));
  rows[1]->value.set_text("%s", maker.c_str());
  const std::string type = e.message_format(xl ? 0x241 : 0x2d2, nullptr);  // Extra Light, Standard
  rows[2]->set_text(e.message(0x242));
  rows[2]->value.set_text("%s", type.c_str());
  const std::string tons = e.message_format(0x244, nullptr);
  const float w = static_cast<float>(engine_weight_of(index, xl));
  rows[3]->set_float(e.message(0x243), w, tons.c_str());
  e.message_format(0x245, nullptr);  // "kph", unused
  const int32_t slots = e.lab_config_engine_slots(cfg);
  rows[4]->set_int(e.message(0x248), slots, nullptr);
  const int32_t x = rows[0]->value.get_x(), y = rows[0]->value.get_y();
  faster.set_pos(x + 50, y + 2);
  slower.set_pos(x - 10, y + 2);
  const int32_t y2 = rows[2]->value.get_y();
  type_right.set_pos(x + 50, y2 + 2);
  type_left.set_pos(x - 10, y2 + 2);
  const uint8_t mine = e.lab_config_byte(cfg, 0x1c);
  type_right.enabled = mine;
  type_right.refresh();
  type_left.enabled = mine;
  type_left.refresh();
  if (const Handle dfn = e.mech_dfn_handle(e.config_mech(cfg))) {
    faster.enabled = rating < e.lab_dfn_engine_max(dfn) ? 1 : 0;
    faster.refresh();
    slower.enabled = rating > e.lab_dfn_engine_min(dfn) ? 1 : 0;
    slower.refresh();
  }
}

// ---------------------------------------------------------------------------
// MECH_DATA and CHASSIS_DATA

namespace {
// 0x0044e680 twice, as the panels ask for the shown slot's loadout.
Handle shown_config(Engine& e) {
  const LabState& lab = states().lab;
  return e.lab_slot(lab.pilot_, lab.slot_) ? e.lab_slot_cfg(e.lab_slot(lab.pilot_, lab.slot_)) : 0;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00457130
void LabEndoCycle::show_config(Handle config) {
  select(config && e_.lab_config_byte(config, 0x20) ? 1 : 0);
}

// Mech3.exe 1.2 @ 0x00457160: to endo steel needs 14 free critical slots (7
// on a Clan mech; else 0xdd1 with both); back to standard the loadout must
// stay within the mech's tons with standard structure, a tenth of them
// (else 0xdd2 with the excess). Then the loadout changed and the page anew.
void LabEndoCycle::step(int32_t by) {
  Engine& e = e_;
  LabScreen* s = e.lab_screen(states().lab.lab_screen_);
  if (!s || index == -1) return;
  const Handle cfg = shown_config(e);
  if (!cfg) return;
  if (!e.lab_config_byte(cfg, 0x20)) {
    const int32_t free = e.lab_config_slots_left(cfg);
    const int32_t need = mech_tech(e.config_mech(cfg)) != kClan ? 14 : 7;
    if (free < need) {
      s->message.set_text_id(0xdd1, need, free);
      return;
    }
  } else {
    const float all = e.lab_config_weight_of(cfg, -1);
    const float rest = all - e.lab_config_weight_of(cfg, 3);  // without the structure
    const float most = max_mass(e.config_mech(cfg));
    const float over = most * 0.1f + rest - max_mass(e.config_mech(cfg));  // 0x005c2268
    if (over > 0.0f) {  // (fcom: at it, or unordered, fits)
      char b[32];
      std::snprintf(b, sizeof b, "%.1f", static_cast<double>(over));
      s->message.set_text_id(0xdd2, b);
      return;
    }
  }
  CycleButton::step(by);
  e.lab_config_set_endo(cfg, index != 0 ? 1 : 0);
  e.lab_config_set_modified(cfg, 1);
  e.lab_config_recount(cfg);
  s->refresh();
}

// Mech3.exe 1.2 @ 0x00456cd0: 0x0044f040(4), each row's label and 0.
LabMechData::LabMechData(Engine& e) : e_(e), endo(e) {
  const std::string kph = e.message_format(0x245, nullptr);
  const std::string tons = e.message_format(0x244, nullptr);
  add_rows(4);
  static const struct {
    int32_t label;
    bool speed;
  } kRows[] = {{0x247, true}, {0x246, true}, {0x25a, false}, {0x25b, false}};  // run, walk, tons, most
  for (int i = 0; i < 4; ++i) {
    rows[static_cast<size_t>(i)]->set_text(e.message(kRows[i].label));
    rows[static_cast<size_t>(i)]->set_float(0.0f, (kRows[i].speed ? kph : tons).c_str());
  }
}

// Mech3.exe 1.2 @ 0x004570a0: the table's reading, then ENDOSTEEL with its
// two choices (MECH_DATA's ARMORTYPE and ARMORMASS are not read).
bool LabMechData::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool r = LabTable::read_control(node, dialog);
  if (zrdr::Node* n = zrdr::find(node, "ENDOSTEEL")) {
    endo.read_control(n, dialog);
    endo.after_bind();
    endo.add_item(e_.message(0x2d2), 0);  // Standard
    endo.add_item(e_.message(0x25c), 1);
  }
  return r;
}

// Mech3.exe 1.2 @ 0x004572e0
void LabMechData::show_for(float seconds) {
  ztwod::Control::show_for(seconds);
  endo.show_for(seconds);
}
// Mech3.exe 1.2 @ 0x00457310
void LabMechData::hide() {
  ztwod::Control::hide();
  endo.hide();
}

// Mech3.exe 1.2 @ 0x00457330: the speeds at the mech's most tons
// (0x00425300(8)), the loadout's tons in the alternate font when over them.
// The units go through " %s" and never show.
void LabMechData::refresh(Handle config) {
  Engine& e = e_;
  float most = 0.0f, run = 0.0f, walk = 0.0f;
  if (config) {
    most = e.lab_config_weight_of(config, 8);
    run = engine_kph(e.lab_config_engine(config), most, true);
    walk = engine_kph(e.lab_config_engine(config), most, false);
  }
  const std::string kph = e.message_format(0x245, nullptr);
  const std::string tons = e.message_format(0x244, nullptr);
  endo.show_config(config);
  rows[0]->set_float(run, kph.c_str());
  rows[1]->set_float(walk, kph.c_str());
  const float w = config ? e.lab_config_weight_of(config, -1) : 0.0f;
  const bool over = !(w <= most || std::isnan(w) || std::isnan(most));  // (fcomp's C0 or C3)
  rows[2]->set_font(font_label);
  rows[2]->value.set_font(over ? font_alt : font_value);
  rows[2]->set_float(w, tons.c_str());
  rows[3]->set_float(most, tons.c_str());
}

// Mech3.exe 1.2 @ 0x00457600: 0x0044f040(10), each row's label and 0.
LabChassisData::LabChassisData(Engine& e) : e_(e) {
  const std::string tons = e.message_format(0x244, nullptr);
  add_rows(10);
  // engine, heat sinks, jump jets, structure, armour, weapons, ammunition
  static const int32_t kLabels[] = {0x253, 0x25d, 0x2d6, 0x25e, 0x25f, 0x295, 0x296};
  for (size_t i = 0; i < 7; ++i) {
    rows[i]->set_text(e.message(kLabels[i]));
    rows[i]->set_float(0.0f, tons.c_str());
  }
  rows[7]->set_float(e.message(0x297), 0.0f, tons.c_str());  // equipment
  rows[8]->set_float(e.message(0x25a), 0.0f, tons.c_str());  // the total
  rows[9]->set_float(e.message(0x260), 0.0f, tons.c_str());  // the mech's most
}

// Mech3.exe 1.2 @ 0x00457e80: as the table's, with half a SPACING more
// before the total.
void LabChassisData::set_pos(int32_t px, int32_t py) {
  ztwod::Control::set_pos(px, py);
  const int32_t rx = spacing[0] + offset[0] + px;
  int32_t ry = offset[1] + py;
  auto place = [&](LabRow* r) {
    r->x = rx;
    r->y = ry;
    r->value.set_pos(width + rx, ry);
  };
  for (size_t i = 0; i < 8; ++i, ry += spacing[1]) place(rows[i]);
  ry += spacing[1] / 2;
  place(rows[8]);
  ry += spacing[1];
  place(rows[9]);
}

// Mech3.exe 1.2 @ 0x00457940: placed anew, then the tons by kind (0x00425300's
// 0..7), the total (-1) and the mech's most (8). (The speeds are computed
// and dropped.)
void LabChassisData::refresh(Handle config) {
  Engine& e = e_;
  const float most = config ? e.lab_config_weight_of(config, 8) : 0.0f;
  const std::string tons = e.message_format(0x244, nullptr);
  set_pos(get_x(), get_y());
  for (int32_t k = 0; k < 8; ++k)
    rows[static_cast<size_t>(k)]->set_float(config ? e.lab_config_weight_of(config, k) : 0.0f, tons.c_str());
  rows[8]->set_float(config ? e.lab_config_weight_of(config, -1) : 0.0f, tons.c_str());
  rows[9]->set_float(most, tons.c_str());
}

// ---------------------------------------------------------------------------
// ARMOR_TITLES (docs/mechlab.md, The armour panel)

LabArmorTitles::LabArmorTitles(Engine& e)
    : e_(e), remove(e),
      toggles{LabArmorToggle(e), LabArmorToggle(e), LabArmorToggle(e), LabArmorToggle(e), LabArmorToggle(e),
              LabArmorToggle(e), LabArmorToggle(e), LabArmorToggle(e), LabArmorToggle(e), LabArmorToggle(e),
              LabArmorToggle(e)},
      distribute(e) {}

// Mech3.exe 1.2 @ 0x004510a0. Its buttons bound first, then itself; the
// toggles in two columns (the front parts and, beside the torsos, the rear
// three), a row apart, from places kept from the first panel read.
bool LabArmorTitles::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  dialog->bind_button(&remove, "ARMOR_REMOVE");
  dialog->bind_button(&distribute, "ARMOR_DISTRIBUTE");
  ztwod::Control::read_control(node, dialog);
  LabState& lab = states().lab;
  if (!(lab.armor_set_ & 1)) lab.armor_set_ |= 1, lab.armor_front_x_ = get_x() + 0x70;
  if (!(lab.armor_set_ & 2)) lab.armor_set_ |= 2, lab.armor_rear_x_ = get_x() + 0xac;
  if (!(lab.armor_set_ & 4)) lab.armor_set_ |= 4, lab.armor_y_ = get_y() + 0x91;
  constexpr int32_t kRow = 15;  // [0x005e479c]
  auto most = [&](size_t i) { return i < labels.size() ? labels[i] : nullptr; };
  const int32_t fx = lab.armor_front_x_, rx = lab.armor_rear_x_;
  int32_t y = lab.armor_y_;
  toggles[6].place(dialog, 6, most(0x13), fx, y);  // the head
  y += kRow;
  toggles[0].place(dialog, 0, most(0x14), fx, y);
  toggles[3].place(dialog, 3, nullptr, rx, y);
  y += kRow;
  toggles[1].place(dialog, 1, most(0x15), fx, y);
  toggles[4].place(dialog, 4, nullptr, rx, y);
  y += kRow;
  toggles[2].place(dialog, 2, most(0x16), fx, y);
  toggles[5].place(dialog, 5, nullptr, rx, y);
  for (int32_t k = 7; k < 11; ++k) {
    y += kRow;
    toggles[k].place(dialog, k, most(static_cast<size_t>(0x10 + k)), fx, y);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00450840
void LabArmorToggle::place(ztwod::Dialog* dialog, int32_t part, ztwod::Text* text, int32_t px, int32_t py) {
  dialog->bind_button(this, "ARMOR_TOGGLE");
  set_pos(px, py);
  section = part;
  most = text;
}

// Mech3.exe 1.2 @ 0x00450fb0. The toggles' arrows and REMOVE are on only
// while the lab is editable ([0x00605a84] clear).
void LabArmorTitles::show_for(float seconds) {
  ztwod::Control::show_for(seconds);
  remove.show_for(seconds);
  distribute.show_for(seconds);
  const uint8_t on = states().lab.flag_ == 0 ? 1 : 0;
  for (LabArmorToggle& t : toggles) {
    t.show_for(seconds);
    t.set_arrows(on, on);
  }
  remove.enabled = on;
  remove.refresh();
}

// Mech3.exe 1.2 @ 0x00451050
void LabArmorTitles::hide() {
  ztwod::Control::hide();
  remove.hide();
  distribute.hide();
  for (LabArmorToggle& t : toggles) t.hide();
}

// Mech3.exe 1.2 @ 0x00450e00. Its texts 4 and 6 the points the tons give and
// those not on a section, 3 the armour's type; DISTRIBUTE enabled while some
// are left. FIX (bug): the texts are taken unchecked; a missing one is skipped.
void LabArmorTitles::refresh(Handle config) {
  if (!config) return;
  Engine& e = e_;
  const float tons = e.lab_config_armor_tons(config);
  const int32_t points = armor_points(e.lab_config_armor_tech(config), e.lab_config_armor_ferro(config) != 0, tons);
  int32_t on = 0;
  for (int32_t k = 0; k < 11; ++k) on += e.lab_config_section(config, k);
  const int32_t left = points - on;
  remove.set_tons(tons);
  auto text = [&](size_t i) { return i < labels.size() ? labels[i] : nullptr; };
  if (ztwod::Text* t = text(4)) t->set_text_id(0xf37, points);
  if (ztwod::Text* t = text(6)) t->set_text_id(0xf37, left);
  const char* type = e.lab_config_armor_tech(config) == kClan ? e.message(0xfb1)
                                                              : e.message(e.lab_config_armor_ferro(config) ? 0x1b5 : 0x2d2);
  if (ztwod::Text* t = text(3)) t->set_text("%s", type ? type : "");
  distribute.enabled = left != 0;
  distribute.refresh();
  for (LabArmorToggle& t : toggles) t.refresh(config);
}

// Mech3.exe 1.2 @ 0x00450bd0: "<tons> tons" in its second normal, rollover
// and disabled texts. FIX (bug): taken unchecked; a missing one is skipped.
void LabArmorRemove::set_tons(float tons) {
  const char* unit = e_.message(0xf38);
  char b[16];
  std::snprintf(b, sizeof b, "%.1f", static_cast<double>(tons));
  for (std::vector<ztwod::Text*>* v : {&labels, &rollover_labels, &disable_labels})
    if (v->size() > 1) (*v)[1]->set_text("%s %s", b, unit ? unit : "");
}

// Mech3.exe 1.2 @ 0x00450880: the section's points its one choice; its most
// (twice the internal structure, the head 9) in its text.
void LabArmorToggle::refresh(Handle config) {
  if (!config) return;
  Engine& e = e_;
  const int32_t v = section >= 0 && section < 11 ? e.lab_config_section(config, section) : 0;
  clear_items();
  char b[16];
  std::snprintf(b, sizeof b, "%d", v);
  add_item(b, v);
  if (most) most->set_text("%d", part_armor_max(e.config_mech(config), section));
}

// Mech3.exe 1.2 @ 0x00450cc0. Half a ton of the mech's armour back to the
// store; the sections spread anew when more are on them than the tons give.
// Nothing of it on the loadout: only the press itself.
void LabArmorRemove::on_left_up() {
  if (!enabled) return;
  Engine& e = e_;
  const Handle store = states().lab.store_;  // [0x00605a80]
  const Handle cfg = shown_config(e);
  if (!cfg || !store) return;
  ShellButton::on_left_up();
  const int32_t kind = e.lab_config_armor_ferro(cfg) ? 2 : 1;
  const int32_t tech = e.lab_config_armor_tech(cfg);
  EquipmentItem probe;  // 0x0041ec00's
  probe.count = 1, probe.equipment = kind, probe.tech = tech;
  Handle found = 0;  // 0x004714c0: the first like it
  for (Handle h : e.lab_config_items(cfg)) {
    const ConfigItem* it = e.item_of(h);
    if (it && same_item(it, &probe)) {
      found = h;
      break;
    }
  }
  if (!found) return;
  e.lab_store_add_equipment(store, kind, tech, 1);
  e.lab_config_remove(cfg, found, 1);
  int32_t on = 0;  // (0x004513f0's sum, inline)
  for (int32_t k = 0; k < 11; ++k) on += e.lab_config_section(cfg, k);
  const float tons = e.lab_config_armor_tons(cfg);
  if (armor_points(e.lab_config_armor_tech(cfg), e.lab_config_armor_ferro(cfg) != 0, tons) - on < 0)
    e.lab_config_spread_armor(cfg);
  e.lab_config_set_modified(cfg, 1);
  e.lab_config_recount(cfg);
  if (LabScreen* s = e.lab_screen(states().lab.lab_screen_)) s->refresh();  // 0x0044cf50
}

// Mech3.exe 1.2 @ 0x00450c70: the points the tons give spread over the sections.
void LabArmorDistribute::on_left_up() {
  Engine& e = e_;
  const Handle cfg = shown_config(e);
  if (!cfg) return;
  e.lab_config_spread_armor(cfg);
  e.lab_config_set_modified(cfg, 1);
  e.lab_config_recount(cfg);
  if (LabScreen* s = e.lab_screen(states().lab.lab_screen_)) s->refresh();  // 0x0044cf50
}

// Mech3.exe 1.2 @ 0x004509b0 (docs/mechlab.md, The armour panel). Down: a
// point off (none below 0). Up: a point from those left while the section
// and its rear (or front) together are under the most; else, at the most or
// with none left, one moved over from the other side. At the most with none
// on the other side nothing changes and the page is not redrawn.
void LabArmorToggle::step(int32_t by) {
  const int32_t old = index;
  CycleButton::step(by);
  if (old == -1) return;
  Engine& e = e_;
  const Handle cfg = shown_config(e);
  if (!cfg) return;
  auto get = [&](int32_t k) { return k >= 0 && k < 11 ? e.lab_config_section(cfg, k) : 0; };
  auto set = [&](int32_t k, int32_t v) {  // 0x00426560: none below 0
    if (k >= 0 && k < 11) e.lab_config_set_section(cfg, k, v < 0 ? 0 : v);
  };
  const int32_t s = section;
  if (by < 0) {
    set(s, get(s) - 1);
  } else {
    const int32_t most = part_armor_max(e.config_mech(cfg), s);  // 0x004264e0: twice the structure, the head 9
    const int32_t own = get(s);
    const int32_t left = armor_points(e.lab_config_armor_tech(cfg), e.lab_config_armor_ferro(cfg) != 0,
                                      e.lab_config_armor_tons(cfg)) -
                         [&] { int32_t n = 0; for (int32_t k = 0; k < 11; ++k) n += get(k); return n; }();  // 0x004513f0
    const int32_t other = s >= 0 && s < 6 ? (s + 3) % 6 : -1;  // the torsos' front and rear
    const int32_t theirs = get(other);
    if (own + theirs == most && theirs < 1) return;
    if (own + theirs < most && left != 0) {
      set(s, get(s) + 1);
    } else if (other != -1 && theirs > 0) {
      set(other, get(other) - 1);
      set(s, get(s) + 1);
    }
  }
  e.lab_config_set_modified(cfg, 1);
  e.lab_config_recount(cfg);
  if (LabScreen* sc = e.lab_screen(states().lab.lab_screen_)) sc->refresh();  // 0x0044cf50
}

// Mech3.exe 1.2 @ 0x0044dc80 (the shown slot asked once, unchecked)
bool LabScreen::engine_editable() {
  Engine& e = engine();
  const LabState& lab = states().lab;
  const Handle slot = e.lab_slot(lab.pilot_, lab.slot_);
  return slot && e.lab_slot_cfg(slot) && !lab.flag_;
}

// Mech3.exe 1.2 @ 0x0044d4d0, its cases 19..22 (docs/mechlab.md, The
// engine). Faster: one step up, kept only when the loadout is still within
// the mech's tons (else "Need to free up %1 tons"); slower: one down. The
// type: an XL engine needs room in both side torsos (3 slots each, 2 on a
// Clan mech) and twice that free in all; back to standard it must weigh
// under the mech's tons (at them is refused). Each change marks the loadout
// changed, counts its slots and refreshes the page.
void LabScreen::engine_press(int32_t id) {
  Engine& e = engine();
  const LabState& lab = states().lab;
  auto cfg = [&]() {  // 0x0044e680 twice when there is a slot
    return e.lab_slot(lab.pilot_, lab.slot_) ? e.lab_slot_cfg(e.lab_slot(lab.pilot_, lab.slot_)) : 0;
  };
  auto step = [&](Handle c, int32_t by) {  // 0x00432e70 (up to the table's last), 0x00432e90 (down to 0)
    if (!c) return;
    const int32_t v = e.lab_config_engine(c);
    if (by > 0) e.lab_config_set_engine(c, v < 78 ? v + 1 : 78);
    else e.lab_config_set_engine(c, v > 0 ? v - 1 : v);
  };
  if (!engine_editable()) return;
  if (id == 19) {
    step(cfg(), 1);
    const float w = e.lab_config_weight(cfg());
    const Handle c = cfg();
    const float max = max_mass(c ? e.config_mech(c) : 0);
    step(cfg(), -1);
    if (!(w <= max || std::isnan(w) || std::isnan(max))) {  // (fcomp's C0 or C3: unordered fits too)
      char b[32];
      std::snprintf(b, sizeof b, "%.1f", static_cast<double>(w) - static_cast<double>(max));
      message.set_text_id(0xdce, b);
      return;
    }
    step(cfg(), 1);
  } else if (id == 20) {
    step(cfg(), -1);
  } else {
    Handle c = cfg();
    if (!c) return;
    bool ok;
    if (!e.lab_config_byte(c, 0x28)) {
      c = cfg();
      const int32_t n = (mech_tech(e.config_mech(c)) != kClan) + 2;
      if (e.lab_config_part_free(cfg(), 7) < n || e.lab_config_part_free(cfg(), 8) < n) {
        message.set_text_id(0xdcf, n);
        return;
      }
      ok = e.lab_config_slots_left(cfg()) >= 2 * n;
      if (!ok) message.set_text_id(0xdd0, 2 * n);
    } else {
      const Handle a = cfg();
      const Handle b = cfg();
      float w = e.lab_config_weight(a);
      w = static_cast<float>(static_cast<double>(w) - engine_weight_of(e.lab_config_engine(b), e.lab_config_byte(b, 0x28)));
      int32_t index = e.lab_config_engine(cfg());
      index = index > 79 ? 79 : index < 0 ? 0 : index;  // (79: past the table, 0 tons)
      w = static_cast<float>(engine_weight_of(index, 0) + static_cast<double>(w));
      const double over = static_cast<double>(w) - static_cast<double>(max_mass(e.config_mech(cfg())));
      ok = over < 0.0 || std::isnan(over);  // (fcom's C0)
      if (!ok) {
        char t[32];
        std::snprintf(t, sizeof t, "%.1f", over);
        message.set_text_id(0xdce, t);
      }
    }
    if (!ok) return;
    const uint8_t xl = e.lab_config_byte(cfg(), 0x28);
    e.lab_config_set_xl(cfg(), xl ? 0 : 1);
  }
  const Handle c = cfg();
  if (!c) return;
  e.lab_config_set_modified(c, 1);
  e.lab_config_recount(c);
  refresh();
}

// ---------------------------------------------------------------------------
// CONFIG_EDIT

// Mech3.exe 1.2 @ 0x0044a6e0
void LabEditBar::on_return() {
  if (e_.video_mode() != 0) {
    e_.video_begin_3d();
    e_.video_end_3d();
  }
  finish(false);
}

// Mech3.exe 1.2 @ 0x0044f4b0
void LabEditBar::on_cancel() { finish(true); }

// Mech3.exe 1.2 @ 0x0044a2f0
void LabEditBar::start(LabConfigList& list, bool rename) {
  const Handle v = list.value_at(list.selected);
  x38c = v;
  if (!v || !list.selected_row) return;
  x390 = rename ? 1 : 0;
  set_text(e_.lab_config_name(v).c_str());
  set_pos(list.selected_row->get_x(), list.selected_row->get_y());
  list.selected_row->hide();
  show_for(-1.0f);
  set_focus(1);
}

// Mech3.exe 1.2 @ 0x0044a3a0: the typed name taken (or, cancelled or empty,
// SAVE's copy thrown away). A name another loadout has asks to overwrite it
// (0x137 / 0xdcb), unless that one is the game's own: message 0xfb2 and the
// bar stays. The slot's loadout and the named one both get the name; the
// list is written.
bool LabEditBar::finish(bool cancel) {
  if (!x38c) return true;
  Engine& e = e_;
  const LabState& lab = states().lab;
  LabScreen* s = e.lab_screen(lab.lab_screen_);
  const std::string text = get_text();
  const Handle editing = static_cast<Handle>(x38c);
  if (!cancel && !text.empty()) {
    const Handle list = e.lab_dfn_configs(e.mech_dfn_handle(e.config_mech(editing)));
    bool taken = false;
    if (!x390 || text != e.lab_config_name(editing)) taken = e.lab_list_find(list, text.c_str()) != 0;
    auto slot_cfg = [&]() {  // 0x0044e680's slot, asked twice when there is one
      return e.lab_slot(lab.pilot_, lab.slot_) ? e.lab_slot_cfg(e.lab_slot(lab.pilot_, lab.slot_)) : 0;
    };
    auto rename = [&](Handle c) {
      e.lab_config_set_name(c, text.c_str());
      e.lab_config_set_modified(c, 0);
      e.lab_config_recount(c);
    };
    if (taken) {
      const Handle found = e.lab_list_find(list, text.c_str());
      if (!e.lab_config_byte(found, 0x1c)) {
        if (s) s->message.set_text_id(0xfb2);
        return false;
      }
      if (s) s->cursor_obj.erase();
      set_focus(0);
      const bool yes = e.message_box(1, 0x137, 0xdcb) == 1;
      set_focus(1);
      if (!yes) return false;
      rename(slot_cfg());
      rename(editing);
      if (editing == found) {
        e.lab_list_save(list);
      } else {
        e.lab_list_remove(list, found);
        e.lab_config_delete(found);
        e.lab_list_save(list);
      }
    } else {
      rename(slot_cfg());
      rename(editing);
      e.lab_list_save(list);
    }
  } else if (!x390) {
    const Handle list = e.lab_dfn_configs(e.mech_dfn_handle(e.config_mech(editing)));
    e.lab_list_remove(list, editing);
    e.lab_config_delete(editing);
    x38c = 0;
  }
  hide();
  x38c = 0;
  set_focus(0);
  if (s) s->refresh();
  return true;
}

// ---------------------------------------------------------------------------
// MESSAGE_AREA

// Mech3.exe 1.2 @ 0x0044e700: SOUND, played with each text.
bool LabMessage::read(zrdr::Node* node, ztwod::Dialog* dialog) {
  x4b0 = static_cast<uint32_t>(ztwod::host().sound_find(zrdr::get_string(node, "SOUND")));
  x4b4 = 1.0f;
  return ztwod::FontText::read(node, dialog);
}

// Mech3.exe 1.2 @ 0x0044e770
void LabMessage::set_text_id(uint32_t id, ...) {
  va_list args;
  va_start(args, id);
  set_text_id_v(id, args);
  va_end(args);
  if (x4b0) ztwod::host().sound_play(static_cast<int32_t>(x4b0), x4b4);
}

// Mech3.exe 1.2 @ 0x0044e740
void LabMessage::set_text(const char* format, ...) {
  va_list args;
  va_start(args, format);
  set_text_v(format, args);
  va_end(args);
  if (x4b0) ztwod::host().sound_play(static_cast<int32_t>(x4b0), x4b4);
}

// ---------------------------------------------------------------------------
// The screen

// Mech3.exe 1.2 @ 0x0044b920
LabScreen::LabScreen(Engine& e, int32_t slots)
    : MenuScreen(e, 0),
      loadouts(e),
      tabs(e, g_none),
      mechs(e),
      chassis(e),
      configs(e),
      config_edit(e),
      skins(e),
      sort_name(e, 0x1f),
      sort_mass(e, 0x20),
      unassigned(e),
      assigned(e),
      ungrouped(e, false),
      grouped(e, true),
      engine_table(e) {
  const struct {
    LabLater* l;
    uint32_t at;
  } later[] = {{&unassigned, 0x19290}};
  for (const auto& x : later) x.l->e = &e, x.l->at = x.at;
  chassis_data = new LabChassisData(e);  // new(0x188)
  mech_data = new LabMechData(e);        // new(0x5ac)
  armor_titles = new LabArmorTitles(e);  // new(0x31e0), made inline
  if (load("dlgMechLabII.zrd", "LABMENU", true)) {
    bind_primitive(&temp_background, "TEMP_BACKGROUND");
    bind_primitive(&panel_left, "PANEL_LEFT");
    bind_primitive(&panel_right, "PANEL_RIGHT");
    bind_primitive(&panel_center, "PANEL_CENTER");
    bind_primitive(&message, "MESSAGE_AREA");
    bind_button(&current_chassis, "CURRENT_CHASSIS");
    bind_primitive(&icon_glow, "ICON_GLOW");
    static const char* const kTabs[] = {"ICON_CHASSIS",    "ICON_WEAPONS",     "ICON_EQUIPMENT", "ICON_CRITICALS",
                                        "ICON_WEAPONGROUP", "ICON_MISSIONDATA", "ICON_ACCEPT"};
    for (int i = 0; i < 7; ++i) {
      tabs[i].id = i;
      bind_button(&tabs[i], kTabs[i]);
    }
    bind_primitive(&pilot_glow, "PILOT_GLOW");
    static const char* const kMechs[] = {"ICON_MECH1", "ICON_MECH2", "ICON_MECH3", "ICON_MECH4"};
    for (int i = 0; i < 4; ++i) {
      mechs[i].id = 8 + i;
      bind_button(&mechs[i], kMechs[i]);
    }
    // The parts by their number (0..8 the mech's parts) and the order bound.
    part_head.part = 6;
    bind_button(&part_head, "PART_HEAD");
    static const struct {
      int idx, part;
      const char* name;
    } kParts[] = {{0, 1, "PART_TORSO"}, {1, 7, "PART_LTORSO"}, {2, 8, "PART_RTORSO"}, {5, 4, "PART_LLEG"},
                  {6, 5, "PART_RLEG"},  {3, 2, "PART_LARM"},   {4, 3, "PART_RARM"}};
    for (const auto& p : kParts) {
      parts[p.idx].part = p.part;
      bind_button(&parts[p.idx], p.name);
    }
    bind_button(&chassis, "CHASSIS_LIST");
    bind_button(&configs, "CONFIG_LIST");
    bind_button(&config_edit, "CONFIG_EDIT");
    bind_button(&engine_table, "ENGINE_TABLE");
    bind_button(&ungrouped, "WEAPONS_UNGROUPED");
    bind_button(&grouped, "WEAPONS_GROUPED");
    bind_button(&sort_mass, "SORT_CHASSISMASS");
    bind_button(&sort_name, "SORT_CHASSISNAME");
    bind_button(&skins, "SKIN_LIST");
    bind_button(&unassigned, "UNASSIGNED_LIST");
    bind_button(&assigned, "ASSIGNED_LIST");
    bind_button(armor_titles, "ARMOR_TITLES");
    bind_button(mech_data, "MECH_DATA");
    bind_button(chassis_data, "CHASSIS_DATA");
    bind_button(&loadouts, "LOADOUT_LIST");
    unload();
  }
  set_flags_all(0);
  set_active(1);
  config_edit.hide();
  engine_table.hide();
  for (int i = 0; i < 7; ++i) show_page(i, false);
  tabs[5].enabled = 0;  // ICON_MISSIONDATA
  tabs[5].refresh();
  for (int i = slots < 4 ? slots : 4; i < 4; ++i) mechs[i].hide();
  message.set_text(e.message(0xdcc));
}

// Mech3.exe 1.2 @ 0x0044c600: the panels made with new deleted first, the
// members after (in the original's order, the reverse of their making).
LabScreen::~LabScreen() {
  delete chassis_data;
  delete mech_data;
  delete armor_titles;
}

// Mech3.exe 1.2 @ 0x0044d4d0: a press. A rename under way (CONFIG_EDIT's
// +0x38c) must finish first (0x0044a3a0, step 4) unless it is CONFIG_SAVE
// (16). 0..6 the pages, 8..11 the lance's slots; the rest are the pages' own.
void LabScreen::on_button(int32_t id, int32_t a, int32_t b) {
  (void)a;
  Engine& e = engine();
  if (id != 0x10 && config_edit.x38c != 0 && !config_edit.finish(false)) return;
  if (id >= 0 && id < 7) {
    switch_page(id);
    return;
  }
  const LabState& lab = states().lab;
  auto slot_cfg = [&]() {  // 0x0044e680's slot, asked twice when there is one
    const Handle slot = e.lab_slot(lab.pilot_, lab.slot_);
    return slot ? e.lab_slot_cfg(e.lab_slot(lab.pilot_, lab.slot_)) : 0;
  };
  if (id == 16) {  // CONFIG_SAVE: a copy at the list's end, named over its row when the name is taken
    if (config_edit.x38c) {
      config_edit.finish(false);
      return;
    }
    if (!slot_cfg()) return;
    const Handle list = e.lab_dfn_configs(e.mech_dfn_handle(e.lab_slot_mech(e.lab_slot(lab.pilot_, lab.slot_))));
    const Handle cfg = slot_cfg();
    const std::string name = e.lab_config_name(cfg);
    const Handle existing = e.lab_list_find(list, name.c_str());
    const Handle copy = e.lab_config_copy(name.c_str(), cfg);
    e.lab_list_append(list, copy);
    configs.fill();
    configs.select_config(copy);
    if (existing) edit_name(true, false);
    return;
  }
  if (id == 15) {  // CONFIG_DELETE: the player's own only; the list written
    const Handle v = configs.value_at(configs.selected);
    if (!v || !e.lab_config_byte(v, 0x1c)) return;
    const Handle list = e.lab_dfn_configs(e.mech_dfn_handle(e.config_mech(v)));
    e.lab_list_remove(list, v);
    e.lab_config_delete(v);
    e.lab_list_save(list);
    refresh();
    return;
  }
  if (id == 14) {  // CONFIG_RENAME
    if (slot_cfg()) edit_name(true, true);
    return;
  }
  if (id >= 8 && id < 12) {
    states().lab.show_slot(id - 8);
    return;
  }
  if (id == 31 || id == 32) {  // SORT_CHASSISNAME, SORT_CHASSISMASS
    chassis.set_sort(id - 31);
    return;
  }
  if (id >= 23 && id <= 26) {  // SELECT_AMMO, _EQUIP, _WEAPON, _ARMOR: the tab, from the top
    static const int32_t kKind[] = {2, 3, 1, 3};
    unassigned.kind = kKind[id - 23];
    unassigned.armor_tab = id == 26 ? 1 : 0;
    unassigned.top = 0;
    unassigned.refill();
    return;
  }
  if (id >= 27 && id <= 30) {  // SORT_ITEM, SORT_QTY, SORT_MASS, SORT_CRIT
    static const int32_t kSort[] = {0, 2, 1, 3};
    unassigned.set_sort(kSort[id - 27]);
    return;
  }
  if (id == 18) {  // a part button: its part chosen in ASSIGNED_LIST
    assigned.choose(b);
    return;
  }
  if (id >= 19 && id <= 22) engine_press(id);
  // 17 (CONFIG_EDIT's) and any other id the exe ignores (its jump table covers 14..32).
}

// Mech3.exe 1.2 @ 0x0044ce30: ACCEPT takes the shown slot's mech back out of
// the lab's store and leaves (0x00448870); a page hides the old one's
// controls, shows its own and redraws (0x0044cf50).
void LabScreen::switch_page(int32_t p) {
  Engine& e = engine();
  LabState& lab = states().lab;
  if (p == 6) {
    const int32_t mech = e.lab_slot_mech(e.lab_slot(lab.pilot_, lab.slot_));
    e.lab_store_remove(lab.store_, mech);
    lab.accept();
    return;
  }
  show_page(page, false);
  page = p;
  show_page(p, true);
  refresh();
}

// Mech3.exe 1.2 @ 0x0044ce90
void LabScreen::show_slots(int32_t slot) {
  Engine& e = engine();
  LabState& lab = states().lab;
  for (int i = 0; i < 4; ++i) {
    const int32_t m = e.lab_slot(lab.pilot_, i) ? e.lab_slot_mech(e.lab_slot(lab.pilot_, i)) : -1;
    mechs[i].set_mech(m, false, nullptr);
  }
  const int32_t m = e.lab_slot(lab.pilot_, lab.slot_) ? e.lab_slot_mech(e.lab_slot(lab.pilot_, lab.slot_)) : -1;
  mechs[slot].set_mech(m, true, &pilot_glow);
  chassis.set_store(lab.store_);
  chassis.select_mech(m);
  refresh();
}

// Mech3.exe 1.2 @ 0x0044cf50: the shown slot read unchecked (one is always shown).
void LabScreen::refresh() {
  Engine& e = engine();
  const LabState& lab = states().lab;
  refresh(e.lab_slot_cfg(e.lab_slot(lab.pilot_, lab.slot_)));
}

// Mech3.exe 1.2 @ 0x0044cf70: what every page shows for a loadout (none:
// the slot's mech without one). CONFIG_LIST emptied, SKIN_LIST from the
// mech's skins with the player's (slot 0's) chosen, CURRENT_CHASSIS's second
// label, the data panels, the item lists, ARMOR_TITLES, ENGINE_TABLE and
// the groups (later steps' hooks); CRITICALS and WEAPON GROUPS only with a
// loadout, WEAPONS, EQUIPMENT and MISSION DATA hidden, ACCEPT only while
// every slot's loadout passes its check (0x004254d0).
void LabScreen::refresh(Handle config) {
  Engine& e = engine();
  const LabState& lab = states().lab;
  configs.clear_items();  // 0x0044e7b0, 0x004aa700
  configs.layout();
  const int32_t mech = config ? e.config_mech(config) : e.lab_slot_mech(e.lab_slot(lab.pilot_, lab.slot_));
  if (const Handle dfn = e.mech_dfn_handle(mech)) {
    std::vector<Engine::LabSkin> list;
    e.lab_dfn_skins(dfn, &list);
    const int32_t n = static_cast<int32_t>(list.size());
    int32_t skin = e.lab_slot(lab.pilot_, 0) ? e.lab_slot_skin(e.lab_slot(lab.pilot_, 0)) : 0;
    skins.clear_items();
    for (const auto& s : list)
      if (s.skin) skins.add_item(s.named ? s.name.c_str() : nullptr, static_cast<int32_t>(s.skin));
    if (skin > n) skin = n;  // (n itself: past the last)
    else if (skin < 0) skin = 0;
    skins.select(skin);
    if (config_edit.x38c == 0) skins.set_arrows(1, 1);
    configs.fill();
  }
  const Handle cfg = e.lab_slot_cfg(e.lab_slot(lab.pilot_, lab.slot_));
  ztwod::Text* line = current_chassis.labels.size() > 1 ? current_chassis.labels[1] : nullptr;
  if (cfg) {
    e.lab_config_recount(cfg);
    if (line) {
      // (0x00484700 and 0x00484720 asked and dropped)
      const std::string name = display_name(e, e.config_mech(cfg));
      const std::string cname = e.lab_config_name(cfg);
      if (e.lab_config_modified(cfg)) line->set_text_id(0xdcd, name.c_str(), cname.c_str());
      else line->set_text("%s: %s", name.c_str(), cname.c_str());
    }
    chassis_data->refresh(cfg);
    mech_data->refresh(cfg);
    loadouts.refresh(cfg);
    unassigned.x224 = 0;
    unassigned.store = lab.store_;
    unassigned.refill();
    assigned.x224 = cfg;
    assigned.store = cfg;
    assigned.refill();
    armor_titles->refresh(cfg);
    engine_table.refresh(cfg);
    refresh_groups();
  } else {
    if (line) line->set_text_plain(e.message(0x249));
    chassis_data->refresh(0);
    mech_data->refresh(0);
    loadouts.refresh(0);
  }
  const int32_t has = cfg ? 1 : 0;
  tabs[4].enabled = has;  // WEAPON GROUPS
  tabs[4].refresh();
  tabs[3].enabled = has;  // CRITICALS
  tabs[3].refresh();
  tabs[1].hide();
  tabs[2].hide();
  tabs[5].hide();
  bool ok = true;
  for (int32_t i = 0; i < lab.slots_ && ok; ++i) {
    const Handle c = e.lab_slot_cfg(e.lab_slot(lab.pilot_, i));
    if (c) ok = e.lab_config_valid(c);
  }
  tabs[6].enabled = ok ? 1 : 0;  // ACCEPT
  tabs[6].refresh();
}

// Mech3.exe 1.2 @ 0x0044dca0
void LabScreen::edit_name(bool start, bool rename) {
  if (!start) {
    config_edit.finish(false);
    return;
  }
  skins.set_arrows(0, 0);
  config_edit.start(configs, rename);
}

// Mech3.exe 1.2 @ 0x00449e10
void LabScreen::default_config(Handle slot, Handle store) {
  Engine& e = engine();
  if (!slot || e.lab_slot_cfg(slot)) return;
  const int32_t mech = e.lab_slot_mech(slot);
  const Handle dfn = e.mech_dfn_handle(mech);
  if (!dfn) return;
  const std::vector<Handle> list = e.lab_list_configs(e.lab_dfn_configs(dfn));
  const LabState& lab = states().lab;
  Handle done = 0;
  for (size_t i = 0; i < list.size() && !done; ++i) {
    const Handle c = list[i];
    if (!c || e.lab_config_byte(c, 0x1e) || (lab.flag_ && e.lab_config_byte(c, 0x1c))) continue;
    if (!e.lab_store_has_config(store, mech, 0, c)) continue;
    e.lab_slot_set_config(slot, c, store);
    done = e.lab_slot_cfg(slot);
  }
  if (!done) e.lab_slot_set_config(slot, list.empty() ? 0 : list[0], store);
}

// Mech3.exe 1.2 @ 0x0044ca20
HelpButton* LabScreen::tab(int32_t p) { return p >= 0 && p < 7 ? &tabs[p] : nullptr; }

// Mech3.exe 1.2 @ 0x0044ca50: a page's controls shown or hidden. Every call
// shows the mech's data panel and CURRENT_CHASSIS; the glow goes to the
// page's tab (0x0044af90).
void LabScreen::show_page(int32_t p, bool show) {
  Engine& e = engine();
  // The shown slot's mech's tech (0x00484720), asked and dropped: only the
  // slot's lookups (twice) remain.
  const LabState& lab = states().lab;
  if (e.lab_slot(lab.pilot_, lab.slot_)) e.lab_slot(lab.pilot_, lab.slot_);
  mech_data->show_for(-1.0f);
  mech_data->endo.set_arrows(0, 0);
  current_chassis.show_for(-1.0f);
  uint32_t text = 0;
  if (p == 0) {
    if (show) {
      panel_left.show_look(2);
      panel_right.show_look(2);
    }
    if (show) chassis.show_for(-1.0f);
    else chassis.hide();
    if (show) configs.show_for(-1.0f);
    else configs.hide();
    if (show) chassis_data->show_for(-1.0f);
    else chassis_data->hide();
    if (show) loadouts.show_for(-1.0f);
    else loadouts.hide();
    const bool skins_on = show && e.vcall(e.field32(lab.pilot_, 0x6c), 0x2c, {}) != 3;
    if (skins_on) skins.show_for(-1.0f);
    else skins.hide();
    if (show) sort_name.show_for(-1.0f);
    else sort_name.hide();
    if (show) sort_mass.show_for(-1.0f);
    else sort_mass.hide();
    text = 0x18c;
  } else if (p == 3) {
    if (show) {
      panel_left.show_look(1);
      panel_right.show_look(1);
      const uint8_t on = lab.flag_ == 0 ? 1 : 0;
      mech_data->endo.set_arrows(on, on);
    }
    armor_titles->hide();
    if (show) assigned.show_for(-1.0f);
    else assigned.hide();
    if (show) part_head.show_for(-1.0f);
    else part_head.hide();
    for (LabPart& part : parts) {
      if (show) part.show_for(-1.0f);
      else part.hide();
    }
    if (show) unassigned.show_for(-1.0f);
    else unassigned.hide();
    if (!show) e.lab_section_highlight(-1);
    text = 0x1aa;
  } else if (p == 4) {
    if (show) {
      panel_left.show_look(1);
      panel_right.show_look(1);
      refresh_groups();
    }
    if (show) ungrouped.show_for(-1.0f);
    else ungrouped.hide();
    if (show) grouped.show_for(-1.0f);
    else grouped.hide();
    text = 0x18d;
  }
  if (text) {
    ztwod::Text* first = current_chassis.labels.empty() ? nullptr : current_chassis.labels[0];
    first->set_text_plain(e.message(static_cast<int32_t>(text)));
  }
  HelpButton* t = tab(p);
  icon_glow.hide();
  if (t) lab_tab_glow(*t, show, &icon_glow);
}

// Mech3.exe 1.2 @ 0x0044a890
void MechButton::set_mech(int32_t m, bool sel, ztwod::Image* glow) {
  if (m != mech) {
    e_.mech_name_of(m);  // 0x00484650, its answer unused
    zimg::Texture* old = img_normal;
    img_normal = set_image(static_cast<zimg::Texture*>(nullptr));
    ztwod::host().image_release(old);
    ztwod::host().image_release(img_disabled);
    img_disabled = nullptr;
    ztwod::host().image_release(img_activate);
    img_activate = nullptr;
    img_normal = set_image(mech_icon(e_, m, 1));
    img_disabled = mech_icon(e_, m, 3);
    img_activate = mech_icon(e_, m, 0);
    mech = m;
    if (!(name.flags & ztwod::kHidden)) show_name(true);  // the name already showing
  }
  LabScreen::lab_tab_glow(*this, sel, glow);
}

// Mech3.exe 1.2 @ 0x0044af90: the page's tab chosen (no blink, its
// ACTIVATE image) with the glow placed at its GLOW_OFFSET, or not.
void LabScreen::lab_tab_glow(HelpButton& t, bool on, ztwod::Image* glow) {
  t.selected = on ? 1 : 0;
  if (on) {
    t.set_blink(false);
    if (glow) {
      glow->show_for(-1.0f);
      glow->set_pos(t.get_x() + t.glow[0], t.get_y() + t.glow[1]);
    }
    t.set_image(t.img_activate);
    return;
  }
  t.set_image(t.img_normal);
}

}  // namespace mech3
