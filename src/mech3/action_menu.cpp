#include "mech3/action_menu.h"

#include "mech3/content_registry.h"
#include "mech3/content_campaign.h"
#include <cstdio>
#include <string>

#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/mission.h"
#include "mech3/mods.h"
#include "mech3/mech_config.h"
#include "mech3/pilot.h"
#include "mech3/player.h"

namespace mech3 {

// mech_names.h (its Engine clashes with the app's): 0x00484670's message.
int32_t mech_name_message(int32_t mech);

// 0x005e6fb0
const ActionScenario kActionScenarios[6] = {
    {0, -1, 0x27f, 0x27f},   // None
    {1, 0, 0x282, 0x283},    // Battle
    {1, 1, 0xdaf, 0xdb0},    // Battle Waves
    {1, 2, 0x284, 0x285},    // Battle Progression
    {2, 3, 0x286, 0x287},    // Attack
    {3, 4, 0x288, 0x289},    // Defend
};

namespace {
ActionScreen* screen_of(ztwod::Layer* owner) { return dynamic_cast<ActionScreen*>(owner); }

bool authored_scenario(int32_t value) { return value>=0 && (value & 0x40000000) != 0; }
void show_authored_controls(ActionScreen& screen) {
  const bool authored=authored_scenario(screen.scenarios.value_at(screen.scenarios.index));
  if(authored) {
    screen.worlds.hide();screen.waves.hide();screen.enemy_title.hide();
    for(auto& enemy:screen.enemies)enemy.hide();
    for(auto& config:screen.enemy_configs)config.hide();
    for(int i=1;i<5;++i)screen.allies[i].hide();
  } else {
    screen.worlds.show_for(-1);screen.enemy_title.show_for(-1);
    for(int i=1;i<5;++i)screen.allies[i].show_for(-1);
  }
}
int32_t mode_of(int32_t scenario) {
  return scenario >= 0 && scenario < kActionScenarioCount ? kActionScenarios[scenario].mode : -1;
}

// 0x00484670: a mech's shown name, "notmech" out of range.
const char* mech_label(Engine& e, int32_t mech) {
  if (mech < 0 || mech >= chassis_count()) return "notmech";
  if (const char* label = content_label("chassis", mech)) return label;
  const char* s = e.message(mech_name_message(mech));
  return s ? s : "";
}

// The render target from zVideo's buffers (0x00558290), as the other menus'.
void target_buffers(Engine& e) {
  const Handle a = e.video_back_buffer();
  const Handle b = e.video_buffer_da0();
  const Handle r = e.video_buffer_d90();
  e.render_set_target2(e.video_target(), r, b, a);
}

// The pilot's game's slot 0 (vt+0x34(0): +0x20 in every game), null without one.
GameSlot* player_slot() {
  Pilot* p = current_pilot();
  return p && p->current ? &p->current->slot : nullptr;
}

// The LABEL's `font` (both fonts) and `inactive_font`.
void read_fonts(zrdr::Node* node, Handle* font, Handle* inactive) {
  zrdr::Node* label = zrdr::find_flat(node, "LABEL", 1);
  if (!label) return;
  *font = *inactive = ztwod::host().font_find(zrdr::get_string(label, "font"));
  if (const char* s = zrdr::get_string(label, "inactive_font")) *inactive = ztwod::host().font_find(s);
}
}  // namespace

// ---- the cycles ----

// Mech3.exe 1.2 @ 0x00489320
bool ActionCycle::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool r = CycleButton::read_control(node, dialog);
  read_fonts(node, &font, &inactive_font);
  return r;
}

// Mech3.exe 1.2 @ 0x00489580: OFFSET's two values read as ints whatever their type.
bool LineupCycle::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool r = CycleButton::read_control(node, dialog);
  read_fonts(node, &font, &inactive_font);
  if (zrdr::Node* o = zrdr::find_flat(node, "OFFSET", 1)) {
    offset_x = o->list[1].i;
    offset_y = o->list[2].i;
  }
  return r;
}

// Mech3.exe 1.2 @ 0x00489390. The old scenario's line-up is kept unless it was
// None or Progression; the test for Progression inside reads this same cycle,
// so the wave is never kept here (only by ACCEPT).
void ScenarioList::step(int32_t by) {
  ActionScreen* s = screen_of(owner);
  const int32_t old = mode_of(value_at(index));
  if (s && old != -1 && old != 2) s->keep_lineup();
  CycleButton::step(by);
  if (!s) return;
  s->descriptions.step(by);  // the cycle after this one in the screen (+0x42c)
  show_authored_controls(*s);
  if (mode_of(value_at(index)) == 2) {
    int32_t best = 0;
    if (Pilot* p = current_pilot()) {
      if (ActionGame* a = p->action()) best = a->waves < 0x14 ? a->waves : 0x13;
    }
    for (EnemyList& l : s->enemies) {
      l.show_for(-1.0f);
      l.step(0);
    }
    s->waves.fill(best);
    s->show_wave(best);
    return;
  }
  for (EnemyList& l : s->enemies) {
    l.show_for(-1.0f);
    l.step(0);
  }
  s->show_custom();
  show_authored_controls(*s);
}

// Mech3.exe 1.2 @ 0x00489660
void AllyList::select(int32_t i) {
  CycleButton::select(i);
  Item* it = item(index);
  set_label_font(!it || it->value != -1 ? font : inactive_font);
}

const Config* ConfigCycle::config_at(int32_t i) {
  Item* it = item(i);
  if (!it || it->value < 0 || it->value >= static_cast<int32_t>(configs.size())) return nullptr;
  return configs[static_cast<size_t>(it->value)];
}

// Mech3.exe 1.2 @ 0x00489c10. No choice reads as mech 0 (the value of none),
// so an empty list lists the Annihilator's loadouts. A wave's enemy shows only
// the wave's loadout; training loadouts are never listed.
void EnemyList::select(int32_t i) {
  CycleButton::select(i);
  const int32_t mech = value_at(index);
  set_label_font(mech != -1 ? font : inactive_font);  // 0x00489e40
  ConfigCycle& c = *configs;
  const ConfigList* loadouts = e_.mech_loadouts(mech);
  if (!loadouts || mech == -1) {
    c.clear_items();
    c.configs.clear();
    c.hide();
    return;
  }
  c.show_for(-1.0f);
  c.clear_items();
  c.configs.clear();
  ActionScreen* s = screen_of(owner);
  const std::vector<Config*>* list = &loadouts->configs;
  const int32_t n = static_cast<int32_t>(list->size());
  auto add = [&c](const Config* cfg) {
    c.configs.push_back(cfg);
    c.add_item(cfg->name, static_cast<int32_t>(c.configs.size()) - 1);
  };
  if (s && !s->fixed) {
    const ActionWaveSlot* row = action_wave(s->wave);  // past the table: none (the exe reads on)
    const int32_t k = row && s->filling >= 0 && s->filling < 6 ? row[s->filling].config : -1;
    const Config* cfg = k >= 0 && k < n ? (*list)[static_cast<size_t>(k)] : nullptr;
    if (!cfg || cfg->training) return;
    add(cfg);
  } else {
    for (int32_t k = 0; k < n; ++k) {
      const Config* cfg = (*list)[static_cast<size_t>(k)];
      if (cfg && !cfg->training) add(cfg);
    }
  }
  if (c.count() <= 0) return;
  c.show_for(-1.0f);
  if (c.count() != 1) {
    c.set_arrows(1, 1, 1);
    c.set_label_font(c.font);
  } else {
    c.set_arrows(0, 0, 0);
    c.set_label_font(c.inactive_font);
  }
}

// Mech3.exe 1.2 @ 0x00489710
void WaveList::step(int32_t by) {
  CycleButton::step(by);
  Item* it = item(index);
  if (ActionScreen* s = screen_of(owner)) s->show_wave(it ? it->value : 0);
}

// Mech3.exe 1.2 @ 0x004897b0
void WaveList::fill(int32_t best) {
  clear_items();
  if (best > 0x13) best = 0x13;
  for (int32_t i = 0; i <= best; ++i) add_item(e_.message_format_ints(0xdb8, {i + 1}).c_str(), i);
  if (ActionScreen* s = screen_of(owner)) s->wave = best;
  select(best);
  if (count() < 2) {
    set_arrows(0, 0, 0);
    set_label_font(inactive_font);
  } else {
    set_arrows(1, 1, 1);
    set_label_font(font);
  }
}

// ---- the screen ----

// Mech3.exe 1.2 @ 0x00488bc0. The five ALLY_LIST, six ENEMY_LIST and six
// ENEMY_CONFIG_LIST are one control each, the copies placed OFFSET apart.
ActionScreen::ActionScreen(Engine& e, Handle s)
    : MenuScreen(e, s),
      worlds(e),
      scenarios(e),
      descriptions(e),
      ally_title(e, -1),
      allies{AllyList(e), AllyList(e), AllyList(e), AllyList(e), AllyList(e)},
      enemy_title(e),
      enemies{EnemyList(e), EnemyList(e), EnemyList(e), EnemyList(e), EnemyList(e), EnemyList(e)},
      enemy_configs{ConfigCycle(e), ConfigCycle(e), ConfigCycle(e), ConfigCycle(e), ConfigCycle(e), ConfigCycle(e)},
      waves(e),
      cancel_btn(e, 3),
      accept_btn(e, 4),
      title(e) {
  for (int i = 0; i < 6; ++i) enemies[i].configs = &enemy_configs[i];
  target_buffers(e);
  const bool moving = !e.fast_menu();
  if (load_from(e.main_menu_definition(), "ACTIONDIALOG", moving)) {
    zimg::Texture* img = nullptr;
    uint8_t bg_moving = 0;
    e.main_menu_background(&img, &bg_moving);
    add(&background);
    background.set_image(img);
    background.moving = bg_moving;
    if (!bg_moving) {
      background.set_pos(0, 0);
      background.src = &background.view;
    }
    bind_primitive(&lines, "LINES");
    bind_primitive(&lines1, "LINES1");
    bind_primitive(&lines2, "LINES2");
    bind_primitive(&lines3, "LINES3");
    bind_button(&ally_title, "ALLY_TITLE");
    bind_button(&cancel_btn, "CANCEL");
    bind_button(&accept_btn, "ACCEPT");
    if (!moving) {
      bake();
      remove(&background);
      remove(&lines);
      remove(&lines1);
      remove(&lines2);
      remove(&lines3);
      remove(&ally_title);
      cancel_btn.set_baked(true);
      accept_btn.set_baked(true);
    }
    bind_button(&worlds, "DATABASE_LIST");
    bind_button(&scenarios, "SCENARIO_LIST");
    bind_button(&descriptions, "SCENARIO_DESC");
    bind_button(&enemy_title, "ENEMY_TITLE");
    for (int i = 0; i < 5; ++i) {
      AllyList& l = allies[i];
      bind_button(&l, "ALLY_LIST");
      const int32_t dx = l.offset_x, dy = l.offset_y;
      const int32_t y = allies[0].get_y() + dy * i;
      l.set_pos(allies[0].get_x() + dx * i, y);
      l.set_arrows(1, 1, 1);
    }
    for (int i = 0; i < 6; ++i) {
      EnemyList& l = enemies[i];
      bind_button(&l, "ENEMY_LIST");
      int32_t dx = l.offset_x, dy = l.offset_y;
      int32_t y = enemies[0].get_y() + dy * i;
      l.set_pos(enemies[0].get_x() + dx * i, y);
      ConfigCycle& c = enemy_configs[i];
      bind_button(&c, "ENEMY_CONFIG_LIST");
      dx = c.offset_x, dy = c.offset_y;
      y = enemy_configs[0].get_y() + dy * i;
      c.set_pos(enemy_configs[0].get_x() + dx * i, y);
      c.hide();
    }
    bind_button(&waves, "ENEMY_WAVE");
  }
  for (int32_t i = 0; i < 4; ++i) worlds.add_item(e.message(0xe33 + i), i);  // c1..c4
  // Ours: the worlds mods add (ia_world= in mod.txt, docs/modding.md), after them.
  for (const ModIaWorld& w : mod_ia_worlds()) {
    std::string name;
    if (!zutl::game_text(w.name, &name)) name = w.folder;
    worlds.add_item(name.c_str(), mission_dir_index(w.folder.c_str()));
  }
  for (int32_t i = 1; i < kActionScenarioCount; ++i) {
    scenarios.add_item(e.message(kActionScenarios[i].name), i);
    descriptions.add_item(e.message(kActionScenarios[i].desc), i);
  }
  if(const auto content=active_content())for(const auto* mission:content->of_kind("mission")) {
    if(mission->owner=="stock" || mission->data.value("multiplayer",false) || !mission->data.value("selectable",true))continue;
    const auto name=mission->data.value("name",mission->id);
    const auto description=mission->data.value("description",std::string("Authored mission: ")+name);
    scenarios.add_item(name.c_str(),0x40000000|mission->handle);
    descriptions.add_item(description.c_str(),0x40000000|mission->handle);
  }
  worlds.set_arrows(1, 1, 1);
  scenarios.set_arrows(1, 1, 1);
  set_flags_all(0);
  set_active(1);
}

// Mech3.exe 1.2 @ 0x0048a680: CANCEL back to the pilot screen's state (1),
// ACCEPT on to the briefing; everything drawn again either way.
void ActionScreen::on_button(int32_t id, int32_t a, int32_t b) {
  (void)a, (void)b;
  if (id == 3) {
    engine().pop_state(1);
  } else if (id == 4) {
    accept();
  }
  dirty_all();
}

// Inline in 0x00489390 and 0x0048a140: the allies' choices, and the wave in
// Progression or else the enemies' and their loadouts'.
void ActionScreen::keep_lineup() {
  ActionSetup& k = action_setup();
  for (int i = 0; i < 5; ++i) k.g820[i] = allies[i].index;
  if (mode_of(scenarios.value_at(scenarios.index)) == 2) {
    k.g87c = waves.index;
  } else {
    for (int i = 0; i < 6; ++i) {
      k.g838[i] = enemies[i].index;
      k.g858[i] = enemy_configs[i].index;
    }
  }
}

// Mech3.exe 1.2 @ 0x0048a810: the wave's enemies (-1 none), each with only its
// loadout, nothing to choose.
void ActionScreen::show_wave(int32_t w) {
  Engine& e = engine();
  fixed = 0;
  wave = w;
  waves.show_for(-1.0f);
  for (EnemyList& l : enemies) l.clear_items();
  // FIX (bug): past the table (the best wave 20, from the reset) the exe reads
  // the strings after it as mechs; here none (the wave shown goes back to the
  // first at once either way).
  const ActionWaveSlot* row = action_wave(w);
  for (int32_t i = 0; i < 6; ++i) {
    filling = i;
    const int32_t mech = row ? row[i].mech : -1;
    const std::string text =
        mech == -1 ? e.message_format_ints(0x28e, {}) : std::string(e.message_format(0x28f, mech_label(e, mech)));
    EnemyList& l = enemies[i];
    l.add_item(text.c_str(), mech);
    l.select(0);
    l.set_arrows(0, 0, 0);
    l.set_label_font(l.inactive_font);
  }
}

// Mech3.exe 1.2 @ 0x0048a930: every mech for each enemy (the first without
// none, the player's mech chosen; the others none), then the kept choices.
void ActionScreen::show_custom() {
  Engine& e = engine();
  fixed = 1;
  waves.hide();
  GameSlot* slot = player_slot();
  for (EnemyList& l : enemies) l.clear_items();
  for (int i = 0; i < 6; ++i) {
    EnemyList& l = enemies[i];
    int32_t chosen = 0, n = 0;
    for (int32_t mech = -1; mech < chassis_count(); ++mech) {
      if (i == 0) {
        if (slot && mech == slot->mech) chosen = n;
        if (mech == -1) continue;
        ++n;
      }
      const std::string text =
          mech == -1 ? e.message_format_ints(0x28e, {}) : std::string(e.message_format(0x28f, mech_label(e, mech)));
      l.add_item(text.c_str(), mech);
    }
    l.set_arrows(1, 1, 1);
    l.set_label_font(l.font);
    l.select(chosen);
  }
  const ActionSetup& k = action_setup();
  for (int i = 0; i < 6; ++i) {
    if (k.g838[i] < 0) continue;
    enemies[i].select(k.g838[i]);
    enemy_configs[i].select(k.g858[i]);
  }
}

// Mech3.exe 1.2 @ 0x0048aac0. Quirk: the commander's mech is chosen by its
// number, not its row (mech 7 has no row), so past it the next mech shows;
// the kept choice ([0x00605820]) replaces it when there is one.
void ActionScreen::reset() {
  Engine& e = engine();
  const ActionSetup& k = action_setup();
  GameSlot* slot = player_slot();
  worlds.select(k.g874 < worlds.count() ? k.g874 : 0);  // ours: a mod's world kept by a pilot, the mod since gone
  scenarios.select(k.g878);
  descriptions.select(k.g878);
  for (AllyList& l : allies) l.clear_items();
  for (EnemyList& l : enemies) l.clear_items();
  int32_t chosen = 0;
  for (int32_t mech = 0; mech < chassis_count(); ++mech) {
    if (mech == 7) continue;
    allies[0].add_item(e.message_format(0x18b, mech_label(e, mech)), mech);
    if (slot && mech == slot->mech) chosen = mech;
  }
  allies[0].select(chosen);
  allies[1].add_item(e.message(0x28a), -1);  // Mobile Field Base: off
  allies[1].add_item(e.message(0x28b), 10);  // on
  for (int i = 2; i < 5; ++i) {
    for (int32_t mech = -1; mech < chassis_count(); ++mech) {
      if (mech == 7) continue;
      const std::string text =
          mech == -1 ? e.message_format_ints(0x28c, {}) : std::string(e.message_format(0x28d, mech_label(e, mech)));
      allies[i].add_item(text.c_str(), mech);
    }
    allies[i].select(0);
  }
  if (mode_of(scenarios.value_at(scenarios.index)) == 2) {
    if (Pilot* p = current_pilot()) {
      if (ActionGame* a = p->action()) wave = a->waves;
    }
    const int32_t best = wave;
    waves.fill(best);  // inline here: the same lines
    show_wave(best);
  } else {
    show_custom();
  }
  for (int i = 0; i < 5; ++i) {
    if (k.g820[i] >= 0) allies[i].select(k.g820[i]);
  }
  if (mode_of(scenarios.value_at(scenarios.index)) == 2) {
    ActionGame* a = current_pilot() ? current_pilot()->action() : nullptr;
    const int32_t best = a ? a->waves : 0;  // (the exe reads the pilot's unchecked)
    waves.select(k.g87c < best ? best : k.g87c);
    waves.step(0);
  }
  show_authored_controls(*this);
}

// Mech3.exe 1.2 @ 0x0048a140. The scenario's mission is looked for (its AI
// paths or reader); without one an earlier scenario is taken, whose reader is
// looked for without ".zbd" (never found). Then the line-up: the commander's
// mech, the field base, the lancemates on the player's side and, unless in
// Progression, the enemies with their loadouts; the choices kept, the pilot
// saved, the briefing.
void ActionScreen::accept() {
  Engine& e = engine();
  Pilot* p = current_pilot();
  ActionGame* g = p ? p->action() : nullptr;
  if (!g) return;  // (the exe goes on unchecked)
  g->count = 0;  // 0x00436a80(0)
  int32_t sc = scenarios.value_at(scenarios.index);
  if (authored_scenario(sc)) {
    const auto content = active_content();
    const auto* mission = content ? content->by_handle("mission", sc & 0x3fffffff) : nullptr;
    if (!mission)
      cc::content::fail("mission-selection", "Selected Instant Action mission is unavailable");
    auto& selected = select_content_standalone(*p, mission->id);
    // Its campaign record owns the save; the standalone keeps Instant Action's inventory.
    selected.store.fill_all();
    selected.content_state["initialized"] = true;
    selected.slot.copy_from(g->slot);
    const int32_t commander = allies[0].value_at(allies[0].index);
    if (commander != selected.slot.mech || !selected.slot.config ||
        selected.slot.config->mech != commander) {
      const auto* loadouts = e.mech_loadouts(commander);
      if (!loadouts || loadouts->configs.empty() || !loadouts->configs.front())
        cc::content::fail("mission-loadout", "Selected commander has no native loadout");
      Config* copy = pilot_hooks().config_copy(*loadouts->configs.front());
      if (!copy) cc::content::fail("mission-loadout", "Cannot copy the selected commander loadout");
      selected.slot.mech = commander;
      delete selected.slot.config;
      selected.slot.config = copy;
      selected.slot.skin = 0;
    }
    action_setup().g878 = scenarios.index;
    keep_lineup();
    p->save();
    e.pop_state(0);
    e.replace_state(6);
    return;
  }
  const char* dir = e.mission_dir(static_cast<uint32_t>(worlds.value_at(worlds.index)));
  char paths[2][0x100];
  std::snprintf(paths[0], sizeof paths[0], "..\\data\\%s\\zrdr\\aipath\\ia%d", dir, kActionScenarios[sc].mission);
  std::snprintf(paths[1], sizeof paths[1], ".\\zbd\\%s\\readeria%d.zbd", dir, kActionScenarios[sc].mission);
  while (!e.file_exists(paths[0]) && !e.file_exists(paths[1]) && sc > 1) {
    --sc;
    std::snprintf(paths[0], sizeof paths[0], "..\\data\\%s\\zrdr\\aipath\\ia%d", dir, kActionScenarios[sc].mission);
    std::snprintf(paths[1], sizeof paths[1], ".\\zbd\\%s\\readeria%d", dir, kActionScenarios[sc].mission);
  }
  g->mode = kActionScenarios[sc].mode;
  if (CycleButton::Item* it = worlds.item(worlds.index)) g->set_text(&g->x38, it->text);  // (the exe reads it unchecked)
  g->set_text(&g->x3c, e.message(kActionScenarios[sc].name));
  g->set_text(&g->x40, e.message(kActionScenarios[sc].desc));
  g->field_bases = allies[1].value_at(allies[1].index) != -1;
  if (GameSlot* s = player_slot()) s->mech = allies[0].value_at(allies[0].index);
  for (int i = 2; i < 5; ++i) {
    const int32_t mech = allies[i].value_at(allies[i].index);
    if (mech != -1) g->add(mech, 1, nullptr, 0);
  }
  g->x6c = g->mode == 2 ? action_setup().g87c + 1 : 1;
  g->x70 = 0;
  if (kActionScenarios[sc].mode == 2) {
    CycleButton::Item* it = waves.item(waves.index);
    g->x6c = (it ? it->value : 0) + 1;
  } else {
    for (int i = 0; i < 6; ++i) {
      const int32_t mech = enemies[i].value_at(enemies[i].index);
      if (mech == -1) continue;
      g->add(mech, 0, enemy_configs[i].config_at(enemy_configs[i].index), 0);
    }
  }
  g->x74 = worlds.value_at(worlds.index);
  g->x78 = kActionScenarios[sc].mission;
  ActionSetup& k = action_setup();
  k.g874 = worlds.index;
  k.g878 = scenarios.index;
  keep_lineup();
  p->save();
  e.pop_state(0);
  e.replace_state(6);
}

}  // namespace mech3
