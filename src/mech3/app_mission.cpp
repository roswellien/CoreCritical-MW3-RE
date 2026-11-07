// The mission load's hooks wired to the ports (docs/mission-load.md), and the
// pieces of the program a mission needs to come up through the real Load and
// Core states: the pilot's game for a debug launch (our addition), the view
// the Core state draws through. Not original code: it stands where the exe's
// other modules will be. What has no port is logged once by address.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "gamez/zmodel/gmod_const.h"
#include "gamez/zsave/zsave.h"
#include "gamez/zrender/zrndr_draw.h"
#include "gamez/zclass/camera.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/light.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zclass/sound.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zdeclient/zdec_crater.h"
#include "gamez/zdeclient/zdec_init.h"
#include "gamez/zgeometry/zgeo_model.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zeffect/zeff_init.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zinterp/zinterp.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_hit.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zmodel/gmod_puff.h"
#include "gamez/znetwork/znet_object.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/ztwod/twod_weather.h"
#include "gamez/zutil/zutl_msg.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zvideo/zvid_dd.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zvideo/zvid_format.h"
#include "gamez/zweapon/zwep_init.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zweapon/zweapon.h"
#include "gamez/zentity/zent_init.h"
#include "mech3/cheats.h"
#include "mech3/cockpit_sound.h"
#include "mech3/landmines.h"
#include "mech3/entity_classes.h"
#include "mech3/message_panel.h"
#include "mech3/mech_objective.h"
#include "mech3/mech_shade.h"
#include "mech3/zai_anim_owner.h"
#include "mech3/zai_list.h"
#include "mech3/zai_target.h"
#include "mech3/zai_valve.h"
#include "mech3/app.h"
#include "mech3/app_engine.h"
#include "mech3/app_hud.h"
#include "mech3/hud.h"
#include "mech3/mission_map.h"
#include "mech3/app_mech.h"
#include "mech3/app_sensors.h"
#include "mech3/app_states.h"
#include "mech3/app_turret.h"
#include "mech3/turret.h"
#include "mech3/app_vehicle.h"
#include "mech3/app_view.h"
#include "mech3/app_keys.h"
#include "gamez/zinput/zinput.h"
#include "mech3/player_view.h"
#include "mech3/mech3_interp.h"
#include "mech3/mission.h"
#include "gamez/zutil/zfix.h"
#include "mech3/zai_attack.h"
#include "mech3/zai_nav.h"
#include "mech3/mech_config.h"
#include "mech3/pilot.h"
#include "crt/msvc_rand.h"
#include "mech3/mech_control.h"
#include "mech3/veh_basic.h"
#include "mech3/zainet_inout.h"
#include "mech3/zainet_region.h"
#include "mech3/mech_pocket.h"
#include "mech3/pocket_panel.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_skin.h"
#include "mech3/mission_world.h"
#include "gamez/zclass/cls_di.h"
#include "mech3/options.h"
#include "mech3/player.h"
#include "mech3/sensors.h"
#include "mech3/zai_brain.h"
#include "mech3/zai_node.h"
#include "mech3/zai_target.h"
#include "mech3/zrdraiv.h"
#include "mech3/zainetedit.h"
#include "mech3/zrdrvalve.h"
#include "platform/render3d.h"

namespace mech3 {

// From mech3/mech_names.h and mech3/weapon.h, whose own Engine and Handle
// clash with app_hooks.h's.
int32_t mech_index(const char* name);                        // 0x004845f0
void make_weapon_type(zrdr::Node* node, zwep::WeaponDef* def);  // 0x004b0020
void free_weapon_type(zwep::WeaponDef* def);                     // 0x004b0090
void reset_ready_times();                                        // 0x004b4bd0
void engine_sounds_clear();  // mech3/veh_basic_object.h: 0x004add10
void vehicle_markers_create();  // mech3/veh_basic_object.h: 0x004ac3d0
void vehicle_markers_delete();  // 0x004ac4e0
bool launch_player();                                        // 0x00491150 (mech3/mission_launch.h)
void radar_objects_create();                                 // 0x004b4ab0
void hit_globals_init(bool network);                         // 0x00413e60's first part (mech_parts.h)
void radar_objects_destroy();                                // 0x004b4ae0
const char* mech_name(int32_t mech);                         // 0x00484650
int32_t weight_class(int32_t mech);                          // 0x00484700 (mech3/mech_names.h)

void hook_census_install();  // app_hook_census.cpp

// The puffers' tick node (0x00549410): its priority (0x004eb5b0), its action
// puff_tick (0x004eb450, 0x005490e0), its delete at shutdown (0x004eaea0).
// Unset, the puffers never stepped: smoke, fire and trails stood still.
void wire_puff_node() {
  zmod::PuffHooks& h = zmod::puff_hooks();
  h.set_priority = [](void*, zcls::Node* n, int32_t p) { return zcls::node_set_priority(n, p); };
  h.set_callback = [](void*, zcls::Node* n, void (*tick)()) {
    return zcls::node_set_action(n, tick ? +[](zcls::Node*) { zmod::puff_tick(); } : nullptr);
  };
  h.destroy_node = [](void*, zcls::Node* n) { zcls::node_delete(n); };
}

namespace {

GameEngine* g_mission_engine = nullptr;
GameEngine* engine_of_hooks() { return g_mission_engine; }

zvid::D3DTexture g_fallback{};  // the default record (0x0080931c) for textures that fail

zcls::Node* mission_node(GameEngine& e, Handle h) { return static_cast<zcls::Node*>(e.object_of(h)); }

// The world parameters' weather block (the 0x98 bytes they write, mech3::
// Weather) with the zTwoD particle system it is in the exe (ztwod::Weather):
// the block's fields go to it as it is added, and again each tick (the
// camera is set after the add, 0x00491de0's end).
struct ProgramWeather : Weather {
  std::unique_ptr<ztwod::Weather> obj;
};
void weather_sync(ProgramWeather& w) {
  ztwod::Weather& o = *w.obj;
  o.color = w.get<uint16_t>(0x44);
  o.alpha_tail = w.get<float>(0x48), o.alpha_head = w.get<float>(0x4c);
  o.wind_dir = w.get<float>(0x68), o.wind_vel = w.get<float>(0x6c), o.gravity = w.get<float>(0x70);
  o.camera = w.camera;
}
std::vector<ProgramWeather*> g_weathers;  // added to the manager, synced each tick

// A world parameters' block for a new rain or snow, its defaults the
// constructor's (0x005a5470), for the reader to start from.
ProgramWeather* program_weather(std::unique_ptr<ztwod::Weather> obj) {
  auto* w = new ProgramWeather;
  w->obj = std::move(obj);
  w->put<uint16_t>(0x44, w->obj->color);
  w->put<float>(0x48, w->obj->alpha_tail), w->put<float>(0x4c, w->obj->alpha_head);
  w->put<float>(0x68, w->obj->wind_dir), w->put<float>(0x6c, w->obj->wind_vel), w->put<float>(0x70, w->obj->gravity);
  return w;
}

// world.zrd's block through the ported reader 0x00491de0, with the hooks it
// needs: the wind and the clouds to zModel, the sun to the world's light, the
// rain and snow to the weather manager [0x00736328].
void read_world_params(const char* file, const char* block, zcls::Node* camera, int32_t type, int32_t number) {
  static int32_t s_type, s_number;
  s_type = type, s_number = number;
  WorldHooks& h = world_hooks();
  h = WorldHooks{};
  h.game_type = [] { return s_type; };
  h.mission_number = [] { return s_number; };
  h.hw_card_flag = [] { return hw_card_flag(); };
  h.world = camera ? zcls::camera_world(camera) : nullptr;
  h.camera = camera;
  zmod::puff_hooks().create_node = [](void*) { return zcls::object3d_create(); };
  wire_puff_node();
  h.puffer_tick_reset = zmod::puff_tick_reset;
  h.wind_velocity = zmod::wind_set_velocity;
  h.wind_max_speed = zmod::wind_set_max_speed;
  h.wind_random_accel = zmod::wind_set_random_accel;
  h.wind_random_angvel = zmod::wind_set_random_angvel;
  h.light_get_orientation = [](zcls::Node* l, float* x, float* y, float* z) { zcls::light_get_orientation(l, x, y, z); };
  h.light_set_orientation = [](zcls::Node* l, float x, float y, float z) { zcls::light_set_orientation(l, x, y, z); };
  h.light_set_ambient = [](zcls::Node* l, float v) { zcls::light_set_ambient(l, v); };
  h.light_set_diffuse = [](zcls::Node* l, float v) { zcls::light_set_diffuse(l, v); };
  h.create_snow = [](int32_t particles) -> Weather* {  // new(0x98) + 0x005a5ad0
    return program_weather(std::make_unique<ztwod::Snow>(particles));
  };
  h.create_rain = [](int32_t particles) -> Weather* {  // new(0x98) + 0x005a60a0
    return program_weather(std::make_unique<ztwod::Rain>(particles));
  };
  h.weather_add = [](Weather* w) {  // 0x0059fbe0 on [0x00736328]
    auto* p = static_cast<ProgramWeather*>(w);
    weather_sync(*p);
    ztwod::weather_layer().add(p->obj.get());
    if (std::find(g_weathers.begin(), g_weathers.end(), p) == g_weathers.end()) g_weathers.push_back(p);
  };
  h.weather_remove = [](Weather* w) {  // 0x0059fc80
    auto* p = static_cast<ProgramWeather*>(w);
    ztwod::weather_layer().remove(p->obj.get());
    g_weathers.erase(std::remove(g_weathers.begin(), g_weathers.end(), p), g_weathers.end());
  };
  h.weather_destroy = [](Weather* w) {  // vt+0x00(1)
    g_weathers.erase(std::remove(g_weathers.begin(), g_weathers.end(), w), g_weathers.end());
    delete static_cast<ProgramWeather*>(w);
  };
  h.weather_start = [](Weather* w, float t) { static_cast<ProgramWeather*>(w)->obj->show_for(t); };  // vt+0x60
  h.weather_stop = [](Weather* w) { static_cast<ProgramWeather*>(w)->obj->hide(); };              // vt+0x64
  h.fluctuate_on = zmod::clouds_set_on;
  h.fluctuate_period = zmod::clouds_set_period;
  h.fluctuate_wavelength = zmod::clouds_set_wavelength;
  h.fluctuate_base_amp = zmod::clouds_set_level;
  h.fluctuate_heading = zmod::clouds_set_heading;
  h.software_color = [](const float*) {};
  // The frame's cloud drift (0x004926d0): the camera's rotation, the frame's dt.
  h.camera_angles = []() -> const float* {
    static float none[3] = {};
    GameEngine* g = engine_of_hooks();
    zcls::Node* cam = g ? mission_node(*g, g->mission_camera()) : nullptr;
    return cam && cam->data ? static_cast<zcls::Camera*>(cam->data)->floats(0x20) : none;
  };
  h.frame_dt = [] { return engine_of_hooks() ? engine_of_hooks()->frame_dt() : 0.0f; };
  h.clouds_advance = zmod::clouds_advance;
  MissionWorld& m = missions().world;  // the missions object's first 0x4c bytes
  m = MissionWorld{};
  world_init(m);
  if (!world_read_params(m, file, block)) std::fprintf(stderr, "mw3: no block %s in %s\n", block, file);
}

// The AI mechs made this mission (app_mech.h), with their records' names.
std::vector<AiMechMade> g_ai_mechs;
std::vector<std::string> g_ai_names;

// The campaign a launch's lancemates belong to: the menus' pilot's, else for
// a debug campaign start one of the program's own (our addition: the debug
// launch has no pilot's game behind it), made with the launch.
std::unique_ptr<CampaignGame> g_debug_campaign;
CampaignGame* launch_campaign(GameEngine* e) {
  if (auto* c = static_cast<CampaignGame*>(program_campaign())) return c;
  if (e->launch().type != 1) return nullptr;
  if (!g_debug_campaign) {
    // Made as the next mission's, so a new campaign's store (c1 mission 1's,
    // through the pilot's hooks) is not filled; then given the launch's.
    g_debug_campaign = std::make_unique<CampaignGame>(nullptr, e->launch().op, e->launch().number + 1);
    g_debug_campaign->mission = e->launch().number;
    g_debug_campaign->slot.mech = e->launch().mech, g_debug_campaign->slot.skin = e->launch().skin;
  }
  return g_debug_campaign.get();
}

// Instant Action's game: the debug start's (made by the caller, the Action
// screen not being ported), else the chosen pilot's.
ActionGame* launch_action(GameEngine* e) {
  if (e->launch().type != 2) return nullptr;
  if (e->launch().action) return e->launch().action;
  return current_pilot() ? current_pilot()->action_game : nullptr;
}
// Training's game of the launch: the chosen pilot's (training launches only
// from the menus).
TrainingGame* launch_training() {
  if (!g_mission_engine || g_mission_engine->launch().type != 4) return nullptr;
  Pilot* p = current_pilot();
  return p ? p->training_game() : nullptr;
}
// Its fields by the exe's offsets (+0x44 mode, +0x48 field bases, +0x68 the
// best wave, +0x6c the wave, +0x70 waves won, +0x7c slots).
bool action_field(ActionGame* a, uint32_t off, uint32_t* get, const uint32_t* put) {
  int32_t* f = off == 0x44 ? &a->mode : off == 0x68 ? &a->waves : off == 0x6c ? &a->x6c : off == 0x70 ? &a->x70
             : off == 0x7c ? &a->count : nullptr;
  if (off == 0x48) {
    if (get) *get = a->field_bases;
    if (put) a->field_bases = static_cast<uint8_t>(*put);
    return true;
  }
  if (!f) return false;
  if (get) *get = static_cast<uint32_t>(*f);
  if (put) *f = static_cast<int32_t>(*put);
  return true;
}

// player.cpp's hooks (docs/mission-start.md): the reader, the records, the
// mech definitions, the nets, zClass, the AI controllers (mech3/zai_brain)
// and the mechs are ported; the other vehicles are not, so their
// constructors are logged and give none (their controllers are deleted).
struct WiredPlayerHooks : PlayerHooks {
  GameEngine* e = nullptr;
  void tick_41e3f0(float dt) override { mech_dead_tick(dt); }  // 0x0041e3f0 on 0x00605510
  void tick_537f20(int32_t v) override { zmod::material_sweep(v); }  // 0x00537f20: the unused materials freed (IA, multiplayer)
  std::map<Handle, int> controls;  // the AI controllers made: 1 a mech's, 2 a vehicle's
  std::set<Handle> params;         // their brains' settings, as vt+0x90 gives them
  std::set<Handle> mechs;          // the AI mechs made
  std::set<Handle> vehicles;       // the basic vehicles made
  // Handles are by address and kept for good (GameEngine::handle_of), so an
  // object made where a dead one lived gets its handle: these sets hold this
  // mission's objects only, or a new mech is taken for an old brain's
  // settings (set_field32 then writes AiParams offsets into it).
  void forget() {
    controls.clear(), params.clear(), mechs.clear(), vehicles.clear(), slots.clear();
    program_vehicles_forget();
  }
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return e->unported(key, args);
  }
  template <class T>
  T* as(Handle h) const { return static_cast<T*>(e->object_of(h)); }

  void nets_build_all() override {
    g_ai_mechs.clear(), g_ai_names.clear();
    ai::nets_build_all();
  }
  Handle object3d_create() override { return e->handle_of(zcls::object3d_create()); }
  void set_action_priority(Handle node, int32_t p) override { zcls::node_set_priority(as<zcls::Node>(node), p); }
  // The player's tick runs with the action lists (0x004f7d80, the Core
  // state's frame).
  void set_action(Handle node, uint32_t fn) override {
    if (fn == kPlayerTick) zcls::node_set_action(as<zcls::Node>(node), [](zcls::Node*) { player_tick(); });
  }
  Handle reader_load(const char* path) override { return e->handle_of(zrdr::load(path)); }
  Handle reader_find(Handle node, const char* key) override {
    zrdr::Node* n = as<zrdr::Node>(node);
    return n ? e->handle_of(zrdr::find(n, key)) : 0;
  }
  void reader_free(Handle node) override {
    if (zrdr::Node* n = as<zrdr::Node>(node)) zrdr::free_node(n);
  }
  float reader_item_float(Handle node, int32_t i) override { return as<zrdr::Node>(node)->list[i].f; }
  Handle reader_item(Handle node, int32_t i) override { return e->handle_of(&as<zrdr::Node>(node)->list[i]); }
  int32_t game_type() override { return e->launch().type; }
  Handle campaign() override {
    CampaignGame* c = launch_campaign(e);
    return c ? e->handle_of(c) : 0;
  }
  // The campaign's slots (pilot.h): their fields by the exe's offsets.
  std::set<Handle> slots;
  CampaignGame* camp(Handle c) const {
    CampaignGame* g = launch_campaign(e);
    return g && c == e->handle_of(g) ? g : nullptr;
  }
  Handle slot_handle(GameSlot* s) {
    if (!s) return 0;
    const Handle h = e->handle_of(s);
    slots.insert(h);
    return h;
  }
  Handle lancemate_init(Handle, int32_t mech) override { return slot_handle(new GameSlot(mech)); }  // 0x00433cc0
  void campaign_set_lancemate(Handle c, int32_t i, Handle slot) override {
    CampaignGame* g = camp(c);
    if (!g || !slots.count(slot)) return PlayerHooks::campaign_set_lancemate(c, i, slot);
    const bool kept = static_cast<size_t>(i) == g->lancemates.size();
    g->set_lancemate(i, as<GameSlot>(slot));
    if (!kept) slots.erase(slot);
  }
  void campaign_set_lancemates(Handle c, int32_t n) override {
    if (CampaignGame* g = camp(c)) g->set_lancemate_count(n);
    else PlayerHooks::campaign_set_lancemates(c, n);
  }
  Handle game_slot(int32_t i) override {
    CampaignGame* g = launch_campaign(e);
    return g ? slot_handle(g->slot_at(i)) : PlayerHooks::game_slot(i);
  }
  Handle campaign_slot(Handle c, int32_t i) override {
    CampaignGame* g = camp(c);
    return g ? slot_handle(g->slot_at(i)) : PlayerHooks::campaign_slot(c, i);
  }
  int32_t campaign_slot_count(Handle c) override {
    CampaignGame* g = camp(c);
    return g ? g->slot_count() : PlayerHooks::campaign_slot_count(c);
  }
  // Mech3.exe 1.2 @ 0x00437800 (training's vt+0x34): slot 0 its one slot (+0x20), the player's.
  Handle training_slot(Handle t) override {
    TrainingGame* g = t ? launch_training() : nullptr;
    return g ? slot_handle(&g->slot) : PlayerHooks::training_slot(t);
  }
  // Mech3.exe 1.2 @ 0x00433df0 as player_setup calls it (c, 0, 0): a copy of the record's loadout.
  void slot_set_config(Handle slot, Handle config) override {
    if (!slots.count(slot)) return PlayerHooks::slot_set_config(slot, config);
    as<GameSlot>(slot)->set_config(as<Config>(config));
  }
  bool slot_field(Handle h, uint32_t off, uint32_t* get, const uint32_t* put) {
    if (!slots.count(h)) return false;
    GameSlot* s = as<GameSlot>(h);
    int32_t* f = off == 0x04 ? &s->mech : off == 0x0c ? &s->x0c : off == 0x10 ? &s->skin : off == 0x14 ? &s->x14 : nullptr;
    if (off == 0x08) {
      if (get) *get = e->handle_of(s->config);
      return get != nullptr;  // the loadout is set through its own calls
    }
    if (!f) return false;
    if (get) *get = static_cast<uint32_t>(*f);
    if (put) *f = static_cast<int32_t>(*put);
    return true;
  }
  Handle training() override { return e->launch().type == 4 ? e->pilot_game(e->pilot()) : 0; }
  Handle mech_dfn(int32_t mech) override { return e->handle_of(mech3::mech_dfn(mech)); }
  int32_t dfn_config_count(Handle dfn) override {
    const MechDfn* d = as<MechDfn>(dfn);
    return d && d->configs ? static_cast<int32_t>(d->configs->configs.size()) : 0;
  }
  Handle dfn_config(Handle dfn, int32_t i) override { return e->handle_of(as<MechDfn>(dfn)->configs->configs[i]); }
  int32_t dfn_skin_count(Handle dfn) override {
    const MechDfn* d = as<MechDfn>(dfn);
    return d && d->skins ? static_cast<int32_t>(d->skins->skins.size()) : 0;
  }
  Handle dfn_skin(Handle dfn, int32_t i) override { return e->handle_of(as<MechDfn>(dfn)->skins->skins[i]); }
  int32_t mech_index(const char* name) override { return mech3::mech_index(name); }
  Handle znet_find(int32_t id) override { return e->handle_of(znet::find(id)); }
  Handle net_object(Handle o) override { return e->handle_of(dynamic_cast<ai::AiNet*>(as<znet::Object>(o))); }
  Handle net_compiled(Handle o) override { return e->handle_of(as<ai::AiNet>(o)->as_compiled()); }
  Handle alloc(uint32_t size) override { return e->alloc_block(size); }
  ActionGame* action(Handle g) const {
    ActionGame* a = launch_action(e);
    return a && g == e->handle_of(a) ? a : nullptr;
  }
  void set_field32(Handle h, uint32_t off, uint32_t v) override {
    if (slot_field(h, off, nullptr, &v)) return;
    if (ActionGame* a = action(h); a && action_field(a, off, nullptr, &v)) return;
    if (params.count(h)) return as<ai::AiParams>(h)->put<uint32_t>(static_cast<int>(off), v);
    if (mechs.count(h) && mech_set_mask(e->object_of(h), off, v)) return;
    if (vehicles.count(h) && program_vehicle_mask(e->object_of(h), off, nullptr, &v)) return;
    e->poke32(h + off, v);
  }
  uint32_t field32(Handle h, uint32_t off) override {
    if (params.count(h)) return as<ai::AiParams>(h)->get<uint32_t>(static_cast<int>(off));
    uint32_t v = 0;
    if (slot_field(h, off, &v, nullptr)) return v;
    if (ActionGame* a = action(h); a && action_field(a, off, &v, nullptr)) return v;
    if ((mechs.count(h) || player_mech(h)) && off == 0x37c) return e->handle_of(mech_controls(e->object_of(h)));
    if ((mechs.count(h) || player_mech(h)) && mech_get_mask(e->object_of(h), off, &v)) return v;
    if (vehicles.count(h)) {  // the controls and the node as handles, the masks
      if (off == 0xe4) return e->handle_of(program_vehicle_controls(e->object_of(h)));
      if (off == 0x04) return e->handle_of(program_vehicle_node(e->object_of(h)));
      if (program_vehicle_mask(e->object_of(h), off, &v, nullptr)) return v;
    }
    return e->peek32(h + off);
  }
  // The AI controllers: the port's, over the vehicle's compiled net and its
  // record (the blocks player.cpp allocates for them are not used).
  Handle ai_mech_controls(Handle, Handle net, const ai::AivRecord* rec) override {
    const Handle h = e->handle_of(new AiControls(nullptr, as<ai::AiNet>(net), *rec));
    controls[h] = 1;
    return h;
  }
  Handle alloc_4ac860(uint32_t size) override { return e->alloc_block(size); }
  Handle ai_vehicle_controls(Handle, Handle net, const ai::AivRecord* rec) override {
    const Handle h = e->handle_of(new ai::AiVehicleControls(nullptr, as<ai::AiNet>(net), *rec));
    controls[h] = 2;
    return h;
  }
  Handle controls_interface(Handle ctl) override { return ctl; }
  void controls_delete(Handle ictl) override {
    auto it = controls.find(ictl);
    if (it == controls.end()) return PlayerHooks::controls_delete(ictl);
    // Their handles go with them (ours): else the next object made at one of
    // these addresses takes the old handle, and the entities' order (and the
    // runs) differed by allocation luck.
    const auto forget_params = [&](ai::Brain* b) {
      if (!b) return;
      params.erase(e->handle_of(&b->params));
      e->forget_object(&b->params);
    };
    if (it->second == 1) {
      AiControls* c = as<AiControls>(ictl);
      forget_params(c->brain.get());
      e->forget_object(c);
      delete c;
    } else {
      ai::AiVehicleControls* c = as<ai::AiVehicleControls>(ictl);
      forget_params(c->brain.get());
      e->forget_object(c);
      delete c;
    }
    controls.erase(it);
  }
  // vt+0x90 (0x004e8dc0): the brain, whose +0x88 player.cpp holds at 1 for
  // the power-down's update.
  Handle controls_vt90(Handle ictl) override {
    auto it = controls.find(ictl);
    if (it == controls.end()) {  // the player's (0x00428710 -> 0x00428470): his settings (+0x5c)
      void* p = player_mech(pilot_vehicle());
      MechControls* c = p ? mech_controls(p) : nullptr;
      auto* pc = c && e->handle_of(c) == ictl ? dynamic_cast<PlayerControls*>(c) : nullptr;
      if (!pc) return PlayerHooks::controls_vt90(ictl);
      const Handle h = e->handle_of(&pc->settings);
      params.insert(h);
      return h;
    }
    ai::Brain* b = it->second == 1 ? as<AiControls>(ictl)->brain.get() : as<ai::AiVehicleControls>(ictl)->brain.get();
    if (!b) return 0;
    const Handle h = e->handle_of(&b->params);
    params.insert(h);
    return h;
  }
  // The mech itself is the port's (mech3/mech_build) on the AI's controller.
  Handle mech_create(int32_t mech, Handle ctl, int32_t config, const char* name) override {
    void* c = e->object_of(ctl);
    if (!c || controls.count(ctl) == 0 || controls[ctl] != 1) return PlayerHooks::mech_create(mech, ctl, config, name);
    void* m = build_ai_mech(mech, static_cast<MechControls*>(static_cast<AiControls*>(c)), config, name);
    if (!m) return 0;
    g_ai_names.emplace_back(name ? name : "");
    g_ai_mechs.push_back({m, c, nullptr});
    const Handle h = e->handle_of(m);
    mechs.insert(h);
    return h;
  }
  // 0x004161c0: the port's mech on the ground at the record's place.
  int32_t mech_place(Handle m, const ai::AivRecord* rec) override {
    void* mech = e->object_of(m);
    if (!mech || !mechs.count(m)) return PlayerHooks::mech_place(m, rec);
    const float pos[3] = {rec->get<float>(0x08), rec->get<float>(0x0c), rec->get<float>(0x10)};
    return place_mech(mech, pos, rec_yaw(rec));
  }
  void mech_power(Handle m, int32_t down) override { mech3::mech_power(e->object_of(m), down); }
  // vt+0x34 (0x00419fc0): the powered-down start's one 0.1 s update, its controller held.
  void mech_update(Handle m, float dt) override {
    if (mechs.count(m)) update_mech(e->object_of(m), dt);
    else PlayerHooks::mech_update(m, dt);
  }
  // The basic vehicles (app_vehicle.cpp): made, named, placed and set up as
  // player.cpp asks; their frame is not run yet (vehicles.md, In the program).
  Handle vehicle_create(Handle ctl, const char* type, const char* name) override {
    void* c = ctl ? e->object_of(ctl) : nullptr;
    if (ctl && !c) return PlayerHooks::vehicle_create(ctl, type, name);
    void* v = program_vehicle_create(c, type, name);
    if (!v) return 0;
    const Handle h = e->handle_of(v);
    vehicles.insert(h);
    return h;
  }
  // The mobile field base (mech_fieldbase): the same, from 0x004589d0.
  Handle mobilehq_create(Handle ctl, const char* type, const char* name) override {
    void* c = ctl ? e->object_of(ctl) : nullptr;
    if (ctl && !c) return PlayerHooks::mobilehq_create(ctl, type, name);
    void* v = program_field_base_create(c, type, name);
    if (!v) return 0;
    const Handle h = e->handle_of(v);
    vehicles.insert(h);
    return h;
  }
  void entity_name(Handle v, const char* name) override {  // 0x005a84c0: the node's name
    if (!vehicles.count(v)) return PlayerHooks::entity_name(v, name);
    zcls::node_set_name(static_cast<zcls::Node*>(program_vehicle_node(e->object_of(v))), name);
  }
  void vehicle_place(Handle v, const ai::AivRecord* rec) override {
    if (!vehicles.count(v)) return PlayerHooks::vehicle_place(v, rec);
    const float pos[3] = {rec->get<float>(0x08), rec->get<float>(0x0c), rec->get<float>(0x10)};
    program_vehicle_place(e->object_of(v), pos, rec_heading(rec));
  }
  void vehicle_setup(Handle v, Handle common, const char* name, int32_t kind) override {
    if (!vehicles.count(v)) return PlayerHooks::vehicle_setup(v, common, name, kind);
    program_vehicle_setup(e->object_of(v), e->object_of(common), name, kind);
  }
  // 0x0049ec90's area is the controls' +0x764: the controls' handle here plus that.
  void vehicle_anims(Handle area, Handle node, Handle mode, const char* name, const char* type, Handle slot) override {
    const Handle ctl = area ? area - 0x764 : 0;
    for (Handle v : vehicles)
      if (ctl && e->handle_of(program_vehicle_controls(e->object_of(v))) == ctl)
        return program_vehicle_motion(e->object_of(v), e->object_of(mode), name, type);
    PlayerHooks::vehicle_anims(area, node, mode, name, type, slot);
  }
  void vehicle_49f660(Handle v) override {
    if (vehicles.count(v)) program_vehicle_points(e->object_of(v));
    else PlayerHooks::vehicle_49f660(v);
  }
  void vehicle_touch(Handle v, Handle node) override {
    if (vehicles.count(v)) program_vehicle_touch(e->object_of(v));
    else PlayerHooks::vehicle_touch(v, node);
  }
  void vehicle_5a85c0(Handle v) override {
    if (vehicles.count(v)) program_vehicle_refresh(e->object_of(v));
    else PlayerHooks::vehicle_5a85c0(v);
  }
  void mech_set_skin(Handle mech, Handle skin) override { set_mech_skin(e->object_of(mech), e->object_of(skin)); }
  // The launch's lancemates (0x0049e5e0): the AI mech's loadout and roster,
  // the player's mech's own net, controls and settings.
  void* ai_mech(Handle m) const { return mechs.count(m) ? e->object_of(m) : nullptr; }
  void* player_mech(Handle v) const {
    void* o = v ? e->object_of(v) : nullptr;
    return o && is_launched_mech(o) ? o : nullptr;
  }
  void mech_set_config(Handle m, Handle cfg) override {
    if (void* p = ai_mech(m)) program_mech_configure(p, e->object_of(cfg));
    else PlayerHooks::mech_set_config(m, cfg);
  }
  int32_t mech_roster(Handle m) override {
    void* p = ai_mech(m);
    return p ? program_mech_roster(p) : PlayerHooks::mech_roster(m);
  }
  Handle vehicle_net(Handle v) override {
    void* p = player_mech(v);
    return p ? e->handle_of(program_mech_own_net(p, v)) : PlayerHooks::vehicle_net(v);
  }
  Handle vehicle_controls(Handle v) override {
    void* p = player_mech(v);
    return p ? e->handle_of(mech_controls(p)) : 0;
  }
  // Instant Action (0x0049dc80): the game and its line-up (ActionGame), the
  // roster's classes and names, a mech's controls, the player's centroid, rand.
  Handle action_game() override {
    ActionGame* a = launch_action(e);
    return a ? e->handle_of(a) : 0;
  }
  Handle action_slot(Handle g, int32_t i) override {
    ActionGame* a = action(g);
    return a ? slot_handle(a->slot_at(i)) : PlayerHooks::action_slot(g, i);
  }
  bool action_ally(Handle g, int32_t i) override {
    ActionGame* a = action(g);
    return a ? a->ally(i) : PlayerHooks::action_ally(g, i);
  }
  void action_drop_enemies(Handle g) override {
    if (ActionGame* a = action(g)) a->drop_enemies();
    else PlayerHooks::action_drop_enemies(g);
  }
  Handle action_add(Handle g, int32_t mech, uint8_t side, Handle config, int32_t paint) override {
    ActionGame* a = action(g);
    if (!a) return PlayerHooks::action_add(g, mech, side, config, paint);
    return slot_handle(a->add(mech, side, as<Config>(config), paint));
  }
  void slot_set_mech(Handle slot, Handle m) override {  // 0x00433ee0: +0x0c
    if (!slot_field(slot, 0x0c, nullptr, &m)) PlayerHooks::slot_set_mech(slot, m);
  }
  void slot_set_paint(Handle slot, int32_t paint) override {  // 0x005a2b90: +0x10
    const uint32_t v = static_cast<uint32_t>(paint);
    if (!slot_field(slot, 0x10, nullptr, &v)) PlayerHooks::slot_set_paint(slot, paint);
  }
  int32_t weight_class(int32_t mech) override { return mech3::weight_class(mech); }
  const char* mech_name(int32_t mech) override { return mech3::mech_name(mech); }
  void mech_set_controls(Handle m, Handle ctl) override {
    void* p = ai_mech(m);
    if (!p) p = player_mech(m);
    if (p) program_mech_set_controls(p, e->object_of(ctl));
    else PlayerHooks::mech_set_controls(m, ctl);
  }
  void vehicle_position(Handle v, float out[3]) override {
    if (!program_vehicle_centre(e->object_of(v), out)) PlayerHooks::vehicle_position(v, out);
  }
  int32_t crt_rand() override { return crt::msvc_rand(); }
  void take_triggers(Handle settings, const ai::AivRecord* rec) override {
    if (params.count(settings)) mech3::take_triggers(*as<ai::AiParams>(settings), *rec);
    else PlayerHooks::take_triggers(settings, rec);
  }
  Handle mech_dfn_of(Handle mech) override {
    return e->handle_of(const_cast<void*>(mech_definition(e->object_of(mech))));
  }
  void report(int32_t level, int32_t line, const char* text) override {
    if (!e->quiet) std::fprintf(stderr, "mw3: report %#x player.cpp:%d %s\n", level, line, text);
  }
  float frame_dt() override { return e->frame_dt(); }
  Handle pilot_vehicle() override { return e->field32(e->pilot(), 0x14); }
  // Mech3.exe 1.2 @ 0x00419fc0 (vtable +0x34): the pilot's mech is the
  // port's (the launch), its frame.
  void vehicle_41aba0(Handle v, float dt) override {
    void* m = e->object_of(v);
    if (!m || !is_launched_mech(m)) return PlayerHooks::vehicle_41aba0(v, dt);
    static GameEngine* s_e;
    s_e = e;
    mech_after_tick(m, [](const char* k) { return s_e->unported(k, {}); });
  }
  void vehicle_update(Handle v, float dt) override {
    void* m = e->object_of(v);
    if (m && is_launched_mech(m)) update_mech(m, dt);
    else PlayerHooks::vehicle_update(v, dt);
  }
  void stealth_update(Handle s, float dt) override {  // vt+0x34 while the view is on it
    if (s && e->object_of(s) == program_stealth()) program_stealth_frame(dt);
    else PlayerHooks::stealth_update(s, dt);
  }
  void entities_update(float dt) override { update_entities(dt); }  // 0x004a1140
  // 0x004d52b0 / 0x004d5300: every znet object's vt+0x2c / vt+0x30 (vt+0x28
  // is itself, 0x004d2a70). Only the waynets' begin (0x00479a50) changes the
  // port's state; the nets' and regions' 0x004d5350 / 0x004d53f0 are no-ops
  // here (docs/ai.md, The nets' begin and end).
  void nets_begin() override { program_nets_begin(); }
  void nets_end() override {}
  Handle view() override { return e->player_view(); }
  Handle view_target(Handle v) override { return e->field32(v, 0x64); }
  // view.vt+0x08 (0x0040ada0): the port's view, each player tick.
  void view_tick(Handle v, float dt) override {
    if (!v || !program_view()) return PlayerHooks::view_tick(v, dt);
    program_view()->update(dt);
    e->sync_view();
  }
  Handle stealth() override { return e->peek32(0x609a2c); }
  void clear_6099c0() override { e->poke32(0x6099c0, 0); }
};
WiredPlayerHooks g_player_hooks;
}  // namespace

void program_launch_lancemates() { launch_lancemates(); }
void program_action_lineup(bool launch) { action_lineup(launch); }
void* program_launch_campaign() { return g_player_hooks.e ? launch_campaign(g_player_hooks.e) : nullptr; }

void GameEngine::ai_reset(uint32_t list) {
  if (ai::ResetList* l = ai::reset_list(list)) l->reset();
}

// A mech's controls when the mech goes (Mech::release, the controls' vt+0x18):
// the AI's, which the vehicle builder made here; the player's stay.
void mission_controls_free(void* controls) {
  // The pocket commander's (0x0046ad80): the mode left, the old controls
  // asked the same; the mode deleted.
  if (PocketControls* p = controls ? static_cast<MechControls*>(controls)->pocket() : nullptr) {
    if (p->release()) delete p;
    return;
  }
  const Handle h = g_player_hooks.e ? g_player_hooks.e->handle_of(controls) : 0;
  if (h && g_player_hooks.controls.count(h)) return g_player_hooks.controls_delete(h);
  // The network mech's are the mech's to delete (their vt+0x18, 0x004d7210).
  if (auto* c = static_cast<MechControls*>(controls); c && c->release()) delete c;
}

namespace {

// The brain's calls out (mech3/zai_brain): the player's vehicle is the
// pilot's +0x14 (none until the launch makes it).
struct WiredBrainHooks : ai::BrainHooks {
  GameEngine* e = nullptr;
  void on_new(uint32_t, const void* p) override { e->forget_object(p); }  // as WiredBuild's (app_mech.cpp)
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return e->unported(key, args);
  }
  bool player_allies(uint32_t* allies) override {
    const Handle p = e->pilot();
    const Handle v = p ? e->field32(p, 0x14) : 0;
    if (!v) return false;
    *allies = mech_allies(e->object_of(v));
    return true;
  }
  // The brain's frame (docs/ai.md 17.1) on the AI mechs and the basic
  // vehicles (app_vehicle.cpp: their slots; zEntity's fields are shared).
  void* mech(Handle v) const { return e->object_of(v); }
  void* veh(Handle v) const {
    void* o = v ? e->object_of(v) : nullptr;
    return is_program_vehicle(o) ? o : nullptr;
  }
  // The pocket commander's marker (app_pocket.cpp) is the one entity here
  // that is not a mech: an AiTarget at its node's place, always up.
  static PocketMarker* marker(Handle v) { return program_pocket_marker(v); }
  // A turret (app_turret.cpp; its handle its `self`): its vtable 0x005c8770
  // answers like a target's but for its slots below.
  static tur::Turret* turret(Handle v) { return v ? program_turret_by_handle(v) : nullptr; }
  // A targets.zrd object (CZAiTarget, its handle its `self`).
  static ai::CZAiTarget* target(Handle v) {
    if (!v) return nullptr;
    for (ai::CZAiTarget* t : ai::target_list())
      if (t->self == v) return t;
    return nullptr;
  }
  void node_position(Handle node, float out[3]) const {
    out[0] = out[1] = out[2] = 0;
    auto* n = static_cast<zcls::Node*>(e->object_of(node));
    if (n && n->data && n->cls() == 5) zcls::node_world_position(n, out);
  }
  static void marker_position(PocketMarker* k, float out[3]) {
    out[0] = out[1] = out[2] = 0;
    auto* n = static_cast<zcls::Node*>(k->node);
    if (!n || !n->data || n->cls() != 5) return;
    zcls::node_world_position(n, out);
  }
  Handle vehicle_handle(void* vehicle) override { return e->handle_of(vehicle); }
  float now() override { return mech_game_time(); }
  // A target's vt+0x98 is its health's fraction (0x004d2b20).
  float health(Handle v) override {
    if (tur::Turret* t = turret(v)) return t->alive();  // vt+0x98 0x0046b780: 1 while active
    if (ai::CZAiTarget* t = target(v)) return t->health_fraction();
    if (void* b = veh(v)) return program_vehicle_health(b);
    return marker(v) ? 100.0f : mech_health(mech(v));
  }
  void position(Handle v, float out[3]) override {
    if (PocketMarker* k = marker(v)) return marker_position(k, out);
    if (tur::Turret* t = turret(v)) return node_position(t->node, out);
    if (ai::CZAiTarget* t = target(v)) return node_position(t->node, out);
    mech_world_position(mech(v), out);
  }
  // 0x0056e7e0: a sound step's sound at the pilot's place.
  void sound_at(void* sound, const float pos[3], float volume) override {
    if (sound) zsnd::play_at(static_cast<zsnd::Object*>(sound), pos, volume, nullptr);
  }
  bool wall_between(const float from[3], const float to[3]) override {
    zcls::Node* w = world_hooks().world;
    if (!w) return false;
    float seg[6] = {from[0], from[1], from[2], to[0], to[1], to[2]};
    static zcls::SegmentHits hits[1];
    zcls::segment_query(w, seg, 2, hits);
    for (int i = 0; i < hits[0].count; ++i)
      if (std::fabs(hits[0].hit[i].normal[1]) < 0.5f) return true;
    return false;
  }
  // 0x004c3000: the mission's boundary, from the vehicle's place (0x005a85e0).
  bool inout_clamp(Handle v, const float in[3], float out[3]) override {
    float pos[3];
    position(v, pos);
    return ai::inout_pull(pos, in, out);
  }
  ai::AiTarget* targetable(Handle v) override {
    if (PocketMarker* k = marker(v)) return k;
    if (tur::Turret* t = turret(v)) return t;
    if (ai::CZAiTarget* t = target(v)) return t;
    if (void* b = veh(v)) return static_cast<ai::AiTarget*>(program_vehicle_target(b));
    return v ? mech_ai_target(mech(v)) : nullptr;
  }
  int32_t powered_down(Handle v) override {  // 0x0041cc10; a target's vt+0xa0 is 0 (0x0040bae0)
    if (tur::Turret* t = turret(v)) return t->idle_now() ? 1 : 0;  // vt+0xa0 0x004a7a60: IS_IDLE
    if (marker(v) || target(v) || veh(v)) return 0;  // a vehicle's vt+0xa0 is 0x0040bae0 too
    return mech_powered_down(mech(v)) ? 1 : 0;
  }
  // 0x004e6ff0 (0x004e6fe0 then vt+0x40): a mech and a basic vehicle
  // (0x004d2a70) answer themselves; the targets.zrd objects and the command
  // marker (AiTarget's 0x0040bae0) 0.
  Handle contact_vehicle(const ai::Contact* c) override {
    for (const ai::Handle s : ai::entity_list())
      if (c->seen && s == c->seen) return entity_is_mech(e->object_of(s)) || veh(s) ? s : 0;
    return 0;
  }
  float contact_range2(Handle v) override { return targetable(v)->range2; }
  void count_awake() override { e->poke32(0x6099c0, e->peek32(0x6099c0) + 1); }
  float intensity() override { return options().intensity[0]; }
  bool ammo_value(Handle v, float* out) override { return mech_ammo_value(mech(v), out); }
  // The walking states (docs/ai.md 17.4a). A contact's entity is a mech too.
  float speed_at(Handle v, float share) override {
    if (void* b = veh(v)) return program_vehicle_speed_at(b, share);
    return mech_speed_at(mech(v), share);
  }
  float stopping_distance(Handle v, float speed) override {
    if (void* b = veh(v)) return program_vehicle_stopping_distance(b, speed);
    return mech_stopping_distance(mech(v), speed);
  }
  float stop_share(Handle v, float d) override {
    if (void* b = veh(v)) return program_vehicle_stop_share(b, d);
    return mech_stop_share(mech(v), d);
  }
  float move_speed(Handle v) override {
    if (void* b = veh(v)) return program_vehicle_move_speed(b);
    return mech_throttle_speed(mech(v));
  }
  void forward(Handle v, float* x, float* z) override {
    float f[3] = {0, 0, 0};
    if (void* m = mech(v)) mech_forward(m, f);
    *x = f[0], *z = f[2];
  }
  // The firing's (docs/ai.md 17.7) on the mechs' weapon managers.
  Handle weapons(Handle v) override { return e->handle_of(mech_weapon_manager(mech(v))); }
  Handle wm_current(Handle wm) override { return e->handle_of(wm_current_weapon(e->object_of(wm))); }
  void wm_clear_aim(Handle wm) override { mech3::wm_clear_aim(e->object_of(wm)); }
  void wm_alpha(Handle wm, float s) override { wm_alpha_strike(e->object_of(wm), s); }
  bool weapon_type_ready(Handle w) override { return mech3::weapon_type_ready(e->object_of(w)); }
  float weapon_reload(Handle w) override { return mech3::weapon_reload(e->object_of(w)); }
  int32_t weapon_ammo(Handle w) override { return mech3::weapon_ammo(e->object_of(w)); }
  void weapon_hold_for_flight(Handle w) override { mech3::weapon_hold_for_flight(e->object_of(w)); }
  void torso_centre(Handle v) override {  // a vehicle's vt+0x50 is none
    if (entity_is_mech(mech(v))) mech_torso_centre(mech(v), 1);
  }
  // The volley pick's (ai.md 17.7): a contact's entity (0x004e6fe0, a mech
  // answers itself; targets.zrd's objects are not mechs: 0) and its active
  // flag (+0x08).
  // 0x004e6fe0 (the seen object's vt+0x2c): every entity answers itself,
  // the mechs, the targets.zrd objects and the marker.
  Handle contact_entity(const ai::Contact* c) override {
    if (!c || !c->seen) return 0;
    if (target(c->seen) || marker(c->seen) || turret(c->seen)) return c->seen;
    return contact_vehicle(c);
  }
  // vt+0x9c: a mech's heat (0x0041ec90, +0x554); every other class's is the
  // constant 0 (0x0045b950 on [0x005c23a0]: vehicles, turrets, targets).
  float heat(Handle v) override { return mech_heat(e->object_of(v)); }
  // 0x004ac800's: a vehicle's vt+0xec is itself (0x004d2a70); 0x00402510
  // on the basic vehicles' path.
  void* owner_vehicle(void* owner) override { return owner; }
  void path_drop_temporary(void* v) override { program_vehicle_path_drop(v); }
  bool entity_active(Handle v) override {
    if (PocketMarker* k = marker(v)) return k->active != 0;
    if (tur::Turret* t = turret(v)) return t->active != 0;
    if (ai::CZAiTarget* t = target(v)) return t->active != 0;  // +0x08
    return mech_active(mech(v));
  }
  // The plan's track and the lead point (ai.md 17.7) on the mechs.
  bool can_face(Handle v, const float dir[3]) override {
    if (void* b = veh(v)) return program_vehicle_can_face(b, dir);
    return mech_can_face(mech(v), dir);
  }
  uint8_t bearer_in_reach(Handle b, float c) override {
    if (void* x = veh(b)) return program_vehicle_in_reach(x, c) ? 1 : 0;
    return mech_faces_within(mech(b), c) ? 1 : 0;
  }
  bool bearer_aim_clamp(Handle b, const float d[3], float out[3]) override {
    if (void* x = veh(b)) return program_vehicle_aim_clamp(x, d, out);
    return mech_clamp_aim(mech(b), d, out);
  }
  void bearer_set_aim(Handle b, const float d[3]) override {
    if (void* x = veh(b)) return program_vehicle_set_aim(x, d);
    mech_set_torso_aim(mech(b), d);
  }
  // 0x004b6e30's: the entity's mech (vt+0x50; a target's and the marker's 0),
  // then 0x0041d130.
  void mech_aim_at_last_hit(Handle e) override {
    if (entity_is_mech(mech(e))) mech3::mech_aim_at_last_hit(mech(e));
  }
  void velocity(Handle v, float out[3]) override {
    if (marker(v) || target(v) || turret(v)) return void(out[0] = out[1] = out[2] = 0);  // still
    if (void* b = veh(v)) return program_vehicle_velocity(b, out);
    mech_lead_velocity(mech(v), out);
  }
  void vehicle_matrix(Handle v, float m[9]) override {
    mech_matrix(marker(v) || target(v) || turret(v) ? nullptr : mech(v), m);
  }
  // Where a weapon aims: a mech's target node; a target's record "target"
  // subnode (+0x10), else its node.
  void entity_lock_position(Handle v, float out[3]) override {
    if (PocketMarker* k = marker(v)) return marker_position(k, out);
    if (tur::Turret* t = turret(v)) return node_position(t->target_node ? t->target_node : t->node, out);
    if (ai::CZAiTarget* t = target(v)) return node_position(t->target_node ? t->target_node : t->node, out);
    mech_target_position(mech(v), out);
  }
  Handle wm_group_weapon(Handle wm) override { return e->handle_of(mech3::wm_group_weapon(e->object_of(wm))); }
  void weapon_node_position(Handle w, float out[3]) override { mech3::weapon_node_position(e->object_of(w), out); }
  float weapon_speed(Handle w) override { return mech3::weapon_speed(e->object_of(w)); }
  uint32_t weapon_flags(Handle w) override { return mech3::weapon_flags(e->object_of(w)); }
  // The volley (ai.md 17.7, weapon_planner): the pilot behind the manager's
  // mech (its AiControls' brain), the contact's distance, the mech's heat.
  // The station keeping's (ai.md 17.2): a mech's vt+0xf4 is 0x0040bae0, a
  // basic vehicle's too; a field base's itself (0x004d2a70).
  bool vehicle_xf4(Handle v) override { return program_is_field_base(veh(v)); }
  // The clear line (ai.md 17.7) on the mechs and the world's query.
  // A target's node (+0x04) and centroid (the record's subnode, +0x14, else
  // the node); the marker's node.
  Handle entity_node(Handle v) override {
    if (tur::Turret* t = turret(v)) return t->node;
    if (ai::CZAiTarget* t = target(v)) return t->node;
    if (PocketMarker* k = marker(v)) return e->handle_of(k->node);
    if (void* b = veh(v)) return e->handle_of(program_vehicle_node(b));
    return e->handle_of(mech_node_of(mech(v)));
  }
  Handle entity_centroid(Handle v) override {
    if (tur::Turret* t = turret(v)) return t->centroid_node ? t->centroid_node : t->node;
    if (ai::CZAiTarget* t = target(v)) return t->centroid_node ? t->centroid_node : t->node;
    if (PocketMarker* k = marker(v)) return e->handle_of(k->node);
    if (void* b = veh(v)) return e->handle_of(program_vehicle_centroid(b));
    return e->handle_of(mech_centroid_of(mech(v)));
  }
  void node_world_cached(Handle n, float out[3]) override { mech3::node_world_cached(e->object_of(n), out); }
  bool node_intersect(Handle n) override { return node_intersects(e->object_of(n)); }
  void node_set_intersect(Handle n, bool on) override { node_intersect_set(e->object_of(n), on); }
  int32_t segment_first(const float a[3], const float b[3], uint32_t exclude, uint8_t* count) override {
    return world_segment_first(a, b, exclude, count);
  }
  int32_t volley_pick(Handle wm, ai::Contact* c) override {
    return mech3::ai_volley_pick(e->object_of(wm), c ? &c->dist2 : nullptr);
  }
  // The aim (ai.md 17.7): the difficulty's error, the targeting aids.
  float aim_intensity() override { return options().intensity[1]; }
  bool targeting_aid(Handle v) override { return entity_is_mech(mech(v)) && mech_targeting_aid(mech(v)); }  // vt+0x50
  void wm_aim_at(Handle wm, const float at[3]) override { mech3::wm_aim_at(e->object_of(wm), at); }
  float vertical_speed(Handle v) override { return mech(v) ? mech_vertical_speed(mech(v)) : 0.0f; }
  float forward_speed(Handle v) override { return mech(v) ? mech_forward_speed(mech(v)) : 0.0f; }
  void set_local_velocity(Handle v, float x, float y, float z) override {
    if (void* m = mech(v)) mech_set_local_velocity(m, x, y, z);
  }
  // A vehicle's vt+0x144 is 0 (0x004d7290), vt+0x148 [0x005c23a0] 0; its
  // vt+0x50 (the lead point's mech) none.
  bool airborne(Handle v) override { return !veh(v) && mech(v) && mech_airborne(mech(v)); }
  float jump_fuel(Handle v) override { return !veh(v) && mech(v) ? mech_jet_fuel(mech(v)) : 0.0f; }
  bool lead_point(Handle v, Handle target, float out[3]) override {
    return entity_is_mech(mech(v)) && mech_lead_point_at(mech(v), target, out);
  }
  float gravity() override { return environment().gravity; }  // [0x005e5d34]
  void place(Handle v, const float p[3], float yaw) override {
    if (void* b = veh(v)) return program_vehicle_place(b, p, yaw);  // 0x004a3630
    if (void* m = mech(v)) move_mech(m, p, yaw);
  }
  // The way finder's pilot: 0x004c7e80 reads the vehicle's 0x005a85e0 (the
  // port gave none until playtest W10: every search failed, so the field
  // bases had no way to their places; vehicles.md, The request).
  void pilot_of(Handle v, ai::Pilot* p) override {
    p->pos = vehicle_position(e->object_of(v));
    if (void* b = veh(v)) return void(p->name = program_vehicle_name(b));  // no mech: no jump jets
    int32_t n = 0;
    const AiMechMade* m = mission_ai_mechs(&n);
    for (int32_t i = 0; i < n; ++i)
      if (m[i].mech == mech(v)) p->name = m[i].name;
    p->jump_jets = mech(v) ? mech_jump_jets(mech(v)) : 0;
  }
  Handle player_vehicle() override {
    const Handle p = e->pilot();
    return p ? e->field32(p, 0x14) : 0;
  }
  // The pilot's game's type ([0x0060861c]+0x6c vt+0x2c, after vt+0x48, a
  // getter): the launch's type. 4 (Training) puts the lancemates on the
  // formation net; -1 without a pilot.
  int32_t player_lance_state() override { return e->pilot() ? e->launch().type : -1; }
  // The hit handlers' (docs/ai.md 17.8b): a mech is its own vehicle's mech
  // (vt+0x50) and its controls are its +0x37c (vt+0x40, vt+0x140); what is
  // not a program mech (a target or a turret) is logged.
  Handle vehicle_mech(Handle v) override {
    if (veh(v)) return 0;  // 0x0040bae0
    return entity_is_mech(mech(v)) ? v : ai::BrainHooks::vehicle_mech(v);
  }
  void ally_hit(Handle attacker, Handle victim) override {
    if (void* b = veh(attacker)) return program_vehicle_hit_an_ally(b, mech(victim));
    if (!entity_is_mech(mech(attacker))) return ai::BrainHooks::ally_hit(attacker, victim);
    if (MechControls* c = mech_controls(mech(attacker))) c->hit_an_ally(mech(victim));
  }
  void allies_turn() override { ai::allies_turn(); }  // 0x004d5dc0 (sensors.cpp)
  // The refit's (zai_repair): the field bases and the dock (app_vehicle).
  int32_t bases_alive() override { return program_bases_alive(); }
  bool mfb_ask(Handle v) override { return program_mfb_ask(mech(v)); }
  bool has_mcb() override { return program_has_mcb(); }
  bool dock_point(float out[3]) override { return program_mcb_point(out); }
  bool dock_drive() override { return program_dock_drive(); }
  bool dock_contact() override { return program_dock_contact(); }
  void mech_power(Handle m, int32_t down) override { mech3::mech_power(mech(m), down); }
};
WiredBrainHooks g_brain_hooks;

// valves.zrd's actions (mech3/zrdrvalve) on what the program has: the AI
// mechs by their records' names (the other vehicles are not ported), their
// controls and brains, Mech::power, zSound. The rest is logged.
// 0x004d7330: zEntity's list by name (0x005a8210: the AI mechs by their
// records' names, the turrets and vehicles by their nodes'), then the
// targets by their nodes' names; each's vt+0x2c (itself). The valves' and
// the objectives' lookup both.
Handle entity_by_name(GameEngine& e, const char* name) {
  if (!name) return 0;
  if (std::strcmp(name, "player") == 0)  // the launch names its mech so (0x004140c0, mission_launch.cpp)
    if (void* p = mech_mission_player()) return e.handle_of(p);
  int32_t n = 0;
  const AiMechMade* m = mission_ai_mechs(&n);
  for (int32_t i = 0; i < n; ++i)
    if (m[i].name && std::strcmp(m[i].name, name) == 0) return e.handle_of(m[i].mech);
  for (tur::Turret* t : tur::turret_list()) {
    zcls::Node* nd = mission_node(e, t->node);
    if (nd && nd->name() && std::strcmp(nd->name(), name) == 0) return e.handle_of(t);
  }
  if (void* v = program_vehicle_named(name)) return e.handle_of(v);
  for (ai::CZAiTarget* t : ai::target_list()) {
    zcls::Node* nd = mission_node(e, t->node);
    if (nd && std::strcmp(nd->name(), name) == 0) return e.handle_of(t);
  }
  return 0;
}

struct WiredValveHooks : ai::ValveHooks {
  GameEngine* e = nullptr;
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return e->unported(key, args);
  }
  void report(int32_t level, int32_t line, const char* text) override {
    if (!e->quiet) std::fprintf(stderr, "mw3: report %#x ZRdrValve.cpp:%d %s\n", level, line, text);
  }
  Handle sound_find(const char* name) override { return e->sound_find(name); }
  void sound_play(Handle s) override {
    if (auto* o = static_cast<zsnd::Object*>(e->object_of(s))) zsnd::play_at(o, nullptr, 1.0f, nullptr);
  }
  // 0x004d7330 (a turret or a target by name too: c1m1's kills reached it).
  Handle target_find(const char* name) override { return entity_by_name(*e, name); }
  Handle entity_find(const char* name) override {
    const Handle h = target_find(name);
    return h ? h : ValveHooks::entity_find(name);
  }
  bool mech(Handle h) const {
    int32_t n = 0;
    const AiMechMade* m = mission_ai_mechs(&n);
    for (int32_t i = 0; i < n; ++i)
      if (e->object_of(h) == m[i].mech) return true;
    return is_launched_mech(e->object_of(h));
  }
  // A basic vehicle answers itself to vt+0x2c and vt+0x40 (0x004d2a70), 0
  // to vt+0x50 (0x0040bae0); its vt+0x140 is its controls (0x004020b0).
  bool veh(Handle h) const { return is_program_vehicle(e->object_of(h)); }
  Handle as_vehicle(Handle o) override { return mech(o) || veh(o) ? o : ValveHooks::as_vehicle(o); }
  Handle vehicle_of(Handle o) override { return mech(o) || veh(o) ? o : ValveHooks::vehicle_of(o); }
  Handle mech_of(Handle o) override { return veh(o) ? 0 : mech(o) ? o : ValveHooks::mech_of(o); }
  Handle controls_of(Handle v) override {
    if (veh(v)) return e->handle_of(program_vehicle_controls(e->object_of(v)));
    return mech(v) ? e->handle_of(mech_controls_of(e->object_of(v))) : ValveHooks::controls_of(v);
  }
  void destroy(Handle o) override {
    if (veh(o)) return program_vehicle_destroy(e->object_of(o));
    ValveHooks::destroy(o);
  }
  void vehicle_place(Handle v, const float pos[3], float yaw) override {
    if (veh(v)) return program_vehicle_place(e->object_of(v), pos, yaw);
    if (mech(v)) return move_mech(e->object_of(v), pos, yaw);  // Mech vt+0xdc, 0x00418f30 (c2m1's teleport_thor)
    ValveHooks::vehicle_place(v, pos, yaw);
  }
  ai::Brain* brain(Handle c) const {
    if (void* b = program_vehicle_brain(e->object_of(c))) return static_cast<ai::Brain*>(b);
    auto* ctl = dynamic_cast<AiControls*>(static_cast<MechControls*>(e->object_of(c)));
    return ctl ? ctl->brain.get() : nullptr;
  }
  Handle brain_of(Handle c) override { return e->handle_of(brain(c)); }
  void mech_power(Handle m, int32_t down) override { mech3::mech_power(e->object_of(m), down); }
  void mech_destroy(Handle m) override { mech_destroy_part(e->object_of(m), 1); }  // 0x0041cfa0(1): the torso
  Handle brain_order_net(Handle b) override {
    auto* br = static_cast<ai::Brain*>(e->object_of(b));
    return br && !br->orders.empty() ? e->handle_of(br->current().net->net()) : 0;
  }
  void controls_set_net(Handle c, Handle net) override {
    if (ai::Brain* b = brain(c)) b->set_net(static_cast<ai::AiNet*>(e->object_of(net)));
  }
  void brain_write(Handle b, uint32_t off, uint32_t v) override {
    auto* br = static_cast<ai::Brain*>(e->object_of(b));
    if (!br) return;
    if (off < ai::AiParams::kEnd) return br->params.put_raw(static_cast<int>(off), v);
    if (off == 0xdc) std::memcpy(&br->xdc, &v, 4);
  }
  void target_masks(Handle o, uint32_t t, uint32_t a, uint32_t en) override {
    void* m = e->object_of(o);
    if (veh(o)) {
      program_vehicle_mask(m, 0xc0, nullptr, &t), program_vehicle_mask(m, 0xc4, nullptr, &a);
      program_vehicle_mask(m, 0xc8, nullptr, &en);
    } else {
      mech_set_mask(m, 0xc0, t), mech_set_mask(m, 0xc4, a), mech_set_mask(m, 0xc8, en);
    }
    player_clear_60b56c();
  }
  float game_time() override { return mech_game_time(); }
  void mission_abort() override { missions().lose(); }  // 0x0048f730 -> 0x0048f9e0
};
WiredValveHooks g_valve_hooks;

// targets.zrd's objects (mech3/zai_target) on what the program has: the
// reader's files, zClass's walk of list 6, zEffect's animations, zWeapon's
// handler tables; zEntity3d, the radar's node map and the HUD's markers are
// not ported (logged). The handlers as zWeapon calls them (0x004d26b0,
// 0x004d26d0).
uint32_t target_weapon_hit(zwep::Handle owner, zwep::WeaponDef* def, zwep::HitRecord* hit, float damage) {
  (void)def, (void)hit;  // read by nothing in 0x004d2c00
  return static_cast<ai::CZAiTarget*>(owner)->on_hit(0, 0, damage);
}
float target_collide(zwep::Handle owner, float damage) { return static_cast<ai::CZAiTarget*>(owner)->on_collide(damage); }

struct WiredTargetHooks : ai::TargetHooks {
  void target_set(ai::AiTarget& t, ai::Contact* c) override {  // vt+0xc0: 0x0041d890 on the view's mech
    t.set_target(c, false);
    program_hud_target_set_by(&t);
  }
  GameEngine* e = nullptr;
  std::unique_ptr<ai::TargetRec> rec[2];
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return e->unported(key, args);
  }
  ai::RecordFile& file(bool traps) { return traps ? ai::traps_file() : ai::targets_file(); }
  void records_open(bool traps) override {
    rec[traps] = std::make_unique<ai::TargetRec>();  // 0x004e0870 / 0x004e07d0
    file(traps).add_ref();
  }
  int32_t record_count(bool traps) override { return file(traps).record_count(); }
  ai::TargetRec* record(bool traps, int32_t i, const char** name) override {
    zrdr::Node* value = file(traps).record(i, name);
    return rec[traps]->parse(value, *name);
  }
  void records_close(bool traps) override {
    file(traps).release();
    rec[traps].reset();
  }
  Handle target_handle(ai::CZAiTarget* t) override { return e->handle_of(t); }
  // zEntity3d's node bind (0x0040eaa0) and unbind (0x0040ed00) for a
  // targets.zrd object: vt+0x60 (0x004d2a70, itself) then its vt+0xe4, a
  // trap's (bit 22, 0x0040ee70) or vt+0xdc, a target's (bit 21, 0x0040ee10),
  // over the node's tree; so the node a shot or a pick hits leads back to it
  // (0x0040e920).
  void node_bind(Handle n, Handle self) override {
    auto* t = static_cast<ai::CZAiTarget*>(e->object_of(self));
    if (t) zcls::node_tree_bind(node(n), t, t->trap ? 0x400000u : 0x200000u, 0);
  }
  void node_unbind(Handle n, Handle self) override {
    auto* t = static_cast<ai::CZAiTarget*>(e->object_of(self));
    if (t) zcls::node_tree_unbind(node(n), t, t->trap ? 0x400000u : 0x200000u, 0);
  }
  // A contact's sight and teams (0x004e6fe0, vt+0x28): a mech's and a
  // target's vt+0x2c and vt+0x28 answer themselves.
  Handle seen_vehicle(Handle s) override { return s; }
  Handle ai_target(Handle v) override { return v; }
  uint32_t team_of(Handle t) override {
    const ai::AiTarget* a = ai::sensor_hooks().fields(t);
    return a ? a->team : 0;
  }
  // 0x005a8390: the record's centroid and target subnodes found by name,
  // else made under its node (zent::marker_nodes). The rest is not needed
  // here: the name passed is 0 (the targets name themselves by desc.name),
  // the node's matrix (+0x0c) is read by nothing, and a node's place is
  // refreshed when read.
  void entity_set_node(Handle, Handle n, Handle, const char* centroid, const char* target,
                       Handle* target_node, Handle* centroid_node) override {
    zcls::Node* nd = node(n);
    zcls::Node *c = nullptr, *t = nullptr;
    zent::marker_nodes(nd, centroid, target, &c, &t);
    *target_node = nd ? e->handle_of(t ? t : nd) : 0;
    *centroid_node = nd ? e->handle_of(c ? c : nd) : 0;
  }
  void node_offset(Handle n, const float v[3]) override {  // 0x004fc1b0: the record's centroid / target offset
    zcls::object3d_add_translation(node(n), v[0], v[1], v[2]);
  }
  bool have_pilot() override { return e->pilot() != 0; }
  Handle player_vehicle() override { return e->pilot() ? e->field32(e->pilot(), 0x14) : 0; }
  // vt+0x50: a mech is itself (0x004d2a70), the other classes none
  // (0x0040bae0); vt+0x58 is 0x0040bae0 in every class.
  Handle self_vehicle(Handle self) override { return entity_is_mech(e->object_of(self)) ? self : 0; }
  Handle self_x58(Handle) override { return 0; }
  int32_t game_type() override { return e->launch().type; }
  void sorted_clear() override { player_clear_60b56c(); }
  // zClass's list 6 by name (0x004f8c30) or by prefix (0x004f8b80).
  static bool prefixed(zcls::Node* n) {
    const char* key = static_cast<const char*>(zcls::list_walk().key);
    return std::strncmp(n->name(), key, std::strlen(key)) == 0;
  }
  Handle nodes_first(const char* name, bool prefix) override {
    return e->handle_of(prefix ? zcls::list_walk_next(name, 6, prefixed) : zcls::list_walk_by_name(name, 6));
  }
  Handle nodes_next(bool prefix) override {
    return e->handle_of(prefix ? zcls::list_walk_next(nullptr, 6, prefixed) : zcls::list_walk_by_name(nullptr, 6));
  }
  zcls::Node* node(Handle h) const { return static_cast<zcls::Node*>(e->object_of(h)); }
  int32_t node_box(Handle n, float box[6]) override {
    zcls::Node* nd = node(n);
    if (!nd || !nd->data) return 5;
    std::memcpy(box, nd->raw + 0x74, 6 * sizeof(float));  // 0x004ebd50
    return (nd->flags() & 0x100) == 0 ? 1 : 0;
  }
  // 0x0050b4e0: the first def in use named so whose node is this one.
  Handle anim_of_node(const char* name, int32_t len, Handle n) override {
    zeff::AnimState& a = zeff::anim();
    if (!name || len <= 0 || !n) return 0;
    for (int32_t i = 1; a.defs && i < a.def_count(); ++i) {
      zeff::AnimDef& d = a.defs[i];
      if (d.raw[0x98] != 5 && std::strncmp(d.name(), name, len) == 0 && d.node == node(n)) return e->handle_of(&d);
    }
    return 0;
  }
  zeff::AnimDef* def(Handle h) const { return static_cast<zeff::AnimDef*>(e->object_of(h)); }
  float anim_health(Handle a) override { return def(a) ? def(a)->get<float>(0xb0) : 0.0f; }
  float anim_max_health(Handle a) override { return def(a) ? def(a)->get<float>(0xac) : 0.0f; }
  Handle anim_node(Handle a) override { return def(a) && def(a)->raw[0x98] != 5 ? e->handle_of(def(a)->node) : 0; }
  Handle anim_node2(Handle a) override { return def(a) && def(a)->raw[0x98] != 5 ? e->handle_of(def(a)->root) : 0; }
  void anim_run(Handle a) override {
    if (zeff::AnimDef* d = def(a)) zeff::anim_start(d, nullptr, 0, 0, 0);
  }
  // The HUD's markers for the radar and TARGET (0x004d2e20, 0x004d3030).
  Handle marker_create(const char* name) override { return e->handle_of(new ztwod::Image(name, 0, 0, 1)); }  // 0x0059b830
  void marker_delete(Handle m) override {
    auto* img = static_cast<ztwod::Image*>(e->object_of(m));
    e->forget_object(img);
    delete img;
  }
  void anim_owner_release(Handle self) override { ai::anim_owner_release(self); }  // 0x004d5100
  uint8_t anim_owner_claim(Handle self, Handle a) override { return ai::anim_owner_claim(self, a) ? 1 : 0; }  // 0x004d4f20
  // zWeapon's handler tables (zweapon.h): the callbacks as the exe's
  // addresses for the checks.
  void remove_handlers(Handle n) override { zwep::remove_handlers(node(n)); }  // 0x00583400
  void hit_killed() override { zwep::note_kill(reinterpret_cast<zwep::Handle>(1), nullptr); }  // 0x00583710
  void hit_health(float fraction) override { zwep::set_health_after(fraction); }              // 0x00583730
  Handle node_handlers(Handle n) override { return node(n) ? e->handle_of(zwep::handlers_of(node(n))) : 0; }
  uint32_t handler_word(Handle t, int32_t i) override {
    auto* r = static_cast<zwep::NodeHandlers*>(e->object_of(t));
    if (!r) return 0;
    const zwep::NodeHandlers::Slot& s = r->slot[i / 2];
    if (i % 2 == 0) return e->handle_of(s.object);
    if (!s.fn) return 0;
    if (s.fn == reinterpret_cast<void (*)()>(&zeff::anim_on_weapon_hit)) return 0x502e60;
    if (s.fn == reinterpret_cast<void (*)()>(&zeff::anim_on_collide)) return 0x502df0;
    if (s.fn == reinterpret_cast<void (*)()>(&target_weapon_hit)) return 0x4d26b0;
    if (s.fn == reinterpret_cast<void (*)()>(&target_collide)) return 0x4d26d0;
    return e->handle_of(reinterpret_cast<void*>(s.fn));
  }
  static void (*callback_of(uint32_t v))() {
    if (v == 0x4d26b0) return reinterpret_cast<void (*)()>(&target_weapon_hit);
    if (v == 0x4d26d0) return reinterpret_cast<void (*)()>(&target_collide);
    return nullptr;
  }
  void set_handler_word(Handle t, int32_t i, uint32_t v) override {
    auto* r = static_cast<zwep::NodeHandlers*>(e->object_of(t));
    if (!r) return;
    if (i % 2 == 0) r->slot[i / 2].object = e->object_of(v);
    else r->slot[i / 2].fn = callback_of(v);
  }
  uint32_t handlers_own(Handle n) override {
    zcls::Node* nd = node(n);
    const uintptr_t h = nd ? reinterpret_cast<uintptr_t>(zwep::handlers_of(nd)) : 0;
    return h && zwep::handler_owned(nd, h) ? 1u : 0u;
  }
  int32_t set_weapon_handler(Handle self, Handle n, uint32_t fn) override {  // 0x005833b0(.., 1, fn)
    return zwep::set_damage_handler(e->object_of(self), node(n), reinterpret_cast<zwep::DamageFn>(callback_of(fn)), 1);
  }
  int32_t set_collide_handler(Handle self, Handle n, uint32_t fn) override {  // 0x005833f0
    return zwep::set_touch_handler(e->object_of(self), node(n), reinterpret_cast<zwep::TouchFn>(callback_of(fn)));
  }
  void valve_set(void* valve, int32_t v) override {
    if (valve) static_cast<ai::Valve*>(valve)->set(v);
  }
  void report(int32_t line, const char* fmt, Handle n, Handle anim) override {
    if (e->quiet) return;
    char text[512];
    const zeff::AnimDef* a = def(anim);
    std::snprintf(text, sizeof text, fmt, node(n) ? node(n)->name() : "?", a ? a->name() : "?");
    std::fprintf(stderr, "mw3: report 0x200 ZAiTarget.cpp:%d %s\n", line, text);
  }
};
WiredTargetHooks g_target_hooks;

}  // namespace
const char* program_pilot_name();  // app_menus.cpp: the current pilot's (+0x18)
namespace {

// The mission's objectives (mech_objective.cpp, docs/objectives.md) on the
// program's ports: entities by name among the AI mechs and targets, zClass's
// nodes, the valves, the message panel, the mission's voice. Not ported
// behind them: the nav points (none found), the HUD's objectives box
// (0x00446ad0), Instant Action's game and its enemies.
struct WiredObjectiveHooks : ObjectiveHooks {
  GameEngine* e = nullptr;
  Objectives objs;  // the missions object's +0x78..+0x88
  // 0x0045c1d0 in training: the game's vt+0x44 (0x004379d0): its lessons freed.
  void training_free() override {
    if (TrainingGame* t = launch_training()) t->lessons->clear();
  }
  // Training's types (objectives.md, Training's types): the controls' command
  // by name and its bindings, zInput's keys and buttons, the clock, the view
  // camera's angles; the player's mech's and its HUD's fields.
  int32_t command_find(const char* name) override { return controls_command(name); }
  int32_t command_key(int32_t cmd) override { return zin::key_of(cmd); }
  int32_t command_joy(int32_t cmd) override { return static_cast<int32_t>(zin::joy_of(cmd)); }
  int32_t command_mouse(int32_t cmd) override { return static_cast<int32_t>(zin::mouse_of(cmd)); }
  int32_t key_by_name(const char* name) override { return zin::key_by_name(name); }
  uint8_t key_down(int32_t key) override { return static_cast<uint8_t>(zin::key_state(key)); }
  uint8_t joy_button(int32_t b) override { return static_cast<uint8_t>(zin::joy_button(b)); }
  uint8_t mouse_button(int32_t b) override { return static_cast<uint8_t>(zin::mouse_button(b)); }
  float mission_time() override { return mech_game_time(); }
  void view_angles(float out[3]) override {
    zcls::Node* cam = mission_node(*e, e->mission_camera());
    const float* a = cam && cam->data ? static_cast<zcls::Camera*>(cam->data)->floats(0x20) : nullptr;
    for (int i = 0; i < 3; ++i) out[i] = a ? a[i] : 0.0f;
  }
  float screen_speed(Handle screen) override { return hud_speed(e->object_of(screen)); }
  Handle aim_hit_node(Handle screen) override {
    void* n = hud_aim_node(e->object_of(screen));
    return n ? e->handle_of(n) : 0;
  }
  Handle node_parent(Handle node) override {
    auto* n = static_cast<zcls::Node*>(e->object_of(node));
    return n && n->parent_count() == 1 && n->parents ? e->handle_of(n->parents[0]) : 0;
  }
  Handle vehicle_target_slot(Handle v) override {
    void* c = mech_target_contact(e->object_of(v));
    return c ? e->handle_of(c) : 0;
  }
  Handle contact_entity(Handle slot) override {
    return ai::brain_hooks().contact_entity(static_cast<const ai::Contact*>(e->object_of(slot)));
  }
  int32_t vehicle_aimed_part(Handle v) override { return mech_aimed_part(e->object_of(v)); }
  void vehicle_facing(Handle v, float out[3]) override { mech_forward(e->object_of(v), out); }
  int32_t vehicle_heat_warning(Handle v) override { return static_cast<int32_t>(mech_heat_state(e->object_of(v))); }
  Handle vehicle_weapons(Handle v) override {
    void* wm = mech_weapon_manager(e->object_of(v));
    return wm ? e->handle_of(wm) : 0;
  }
  int32_t current_weapon_type(Handle wm) override { return weapons_current_index(e->object_of(wm)); }
  int32_t weapon_group(Handle wm) override { return weapons_group(e->object_of(wm)); }
  uint8_t weapon_fired(Handle screen, int32_t w) override { return hud_weapon_recycling(e->object_of(screen), w); }
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return e->unported(key, args);
  }
  void report(int32_t level, int32_t line, const char* fmt, const char* a, Handle b) override {
    if (e->quiet) return;
    char text[512];
    std::snprintf(text, sizeof text, fmt, a ? a : "", b);
    std::fprintf(stderr, "mw3: report %#x mech_objective.cpp:%d %s\n", level, line, text);
  }
  const char* message(int32_t id) override {  // 0x00576040
    const zutl::Messages& ms = zutl::messages();
    const char* t = ms.get_text ? ms.get_text(id) : nullptr;
    return t ? t : "";
  }
  std::string message_with(int32_t id, const char* s) override {
    return zutl::format_message(id, s).substr(0, 0x3f);  // into 0x40 bytes
  }
  std::string message_number(int32_t id, bool has_n, int32_t n) override {
    return (has_n ? zutl::format_message_ints(id, {n}) : zutl::format_message(id, {})).substr(0, 0xff);
  }
  Handle sound_find(const char* name) override { return name ? e->sound_find(name) : 0; }
  // 0x0045c4a0's "destroy the enemies" (Instant Action and multiplayer): the
  // game's mode and wave, zEntity's list (each entry's vt+0x2c), an entry's
  // +0x08, its team against the player's enemies, vt+0x48 (a turret's
  // itself, 0x004d2a70; every other class 0, 0x0040bae0).
  int32_t ia_mode() override {
    ActionGame* a = launch_action(e);
    return a ? a->mode : ObjectiveHooks::ia_mode();
  }
  int32_t ia_number() override {
    ActionGame* a = launch_action(e);
    return a ? a->x6c : ObjectiveHooks::ia_number();
  }
  std::vector<Handle> world_objects() override {
    std::vector<Handle> out;
    for (Handle x : ai::entity_list()) out.push_back(ai::sensor_hooks().vcall(x, 0x2c, {x}));
    return out;
  }
  uint8_t object_active(Handle o) override { return ai::brain_hooks().entity_active(o) ? 1 : 0; }
  bool object_hostile(Handle o) override {
    const ai::AiTarget* p = ai::brain_hooks().targetable(e->pilot() ? e->field32(e->pilot(), 0x14) : 0);
    const ai::AiTarget* t = ai::brain_hooks().targetable(o);
    return p && t && (p->enemies & t->team) != 0;
  }
  int32_t object_x48(Handle o) override { return program_turret_by_handle(o) ? static_cast<int32_t>(o) : 0; }
  ai::CZAiTarget* target(Handle h) const {
    for (ai::CZAiTarget* t : ai::target_list())
      if (e->object_of(h) == t) return t;
    return nullptr;
  }
  Handle entity_find(const char* name) override { return entity_by_name(*e, name); }  // 0x004d7330
  // +0x08: a target's, a turret's, a vehicle's or a mech's active flag; for
  // anything else, its being in zEntity's list.
  uint8_t entity_flag(Handle h) override {
    if (ai::CZAiTarget* t = target(h)) return t->active;
    if (tur::Turret* t = program_turret_by_handle(h)) return t->active;
    if (void* v = e->object_of(h); v && is_program_vehicle(v)) return program_vehicle_active(v);
    // A mech's +0x08 too, cleared once its death fall ends (0x00419fc0): a
    // campaign wreck stays on zEntity's list, so the list alone would keep a
    // killed mech's destroy objective open (campaign.md section 14).
    if (void* m = e->object_of(h); m && entity_is_mech(m)) return mech_active(m) ? 1 : 0;
    const auto& list = ai::entity_list();
    return std::find(list.begin(), list.end(), h) != list.end() ? 1 : 0;
  }
  // vt+0xa4: a target's is 0 (0x0040bae0); the rest by their class
  // (entity_name: a mech's pilot, a vehicle's brain's, a turret's none).
  const char* entity_label(Handle h) override {
    if (target(h)) return nullptr;
    return e->object_of(h) ? entity_name(h) : nullptr;
  }
  Handle node_find(const char* name) override { return e->handle_of(zcls::list_find_by_name(6, name)); }
  Handle subnode_find(Handle node, const char* name) override {
    zcls::Node* n = mission_node(*e, node);
    return n ? e->handle_of(zcls::node_find_in_tree(n, name)) : 0;
  }
  uint8_t node_active(Handle node) override {
    zcls::Node* n = mission_node(*e, node);
    return n ? (n->flags() >> 2) & 1 : 0;
  }
  void node_position(Handle node, float out[3]) override {
    if (zcls::Node* n = mission_node(*e, node)) zcls::node_world_position(n, out);
  }
  Handle valve_find(const char* name) override { return e->handle_of(ai::valve_get(name)); }
  void valve_set(Handle v, int32_t x) override {
    if (auto* valve = static_cast<ai::Valve*>(e->object_of(v))) valve->set(x);
  }
  int32_t valve_value(Handle v) override {
    auto* valve = static_cast<ai::Valve*>(e->object_of(v));
    return valve ? valve->get() : 0;
  }
  int32_t game_type() override { return e->launch().type; }
  Handle player_vehicle() override {
    const Handle p = e->pilot();
    return p ? e->field32(p, 0x14) : 0;
  }
  void object_position(Handle o, float out[3]) override { mech_world_position(e->object_of(o), out); }
  void ask_voice(Handle sound, float priority) override { missions().ask_voice(sound, priority); }
  // The nav points (the mission's waynet, vehicles.md, The nav waynet).
  Handle nav_find(const char* name) override {
    void* p = program_nav_find(name);
    return p ? e->handle_of(p) : 0;
  }
  void nav_show(Handle nav, uint8_t on) override { program_nav_watch(e->object_of(nav), on); }
  void nav_position(Handle nav, float out[3]) override { program_waypoint_position(e->object_of(nav), out); }
  // 0x0040fe70: the panel shown for the time, then the message's line.
  void hud_message(float seconds, int32_t id, const char* text) override {
    if (!message_panel()) return;
    message_panel()->show_for(seconds);
    const std::string t = zutl::format_message(id, text);
    if (!t.empty()) message_panel_add("%s", t.c_str());
  }
  // 0x0040fe20: FIX (bug) the line is not a printf format here (a % in a
  // description would read arguments that are not there).
  void hud_message_line(float seconds, const char* text) override {
    if (message_panel()) message_panel_add_for(seconds, "%s", text ? text : "");
  }
  // As the mission's cockpit test: [0x005f4c8c]+0x64; the screen (0x00430aa0
  // on its +0x684) and its objectives box refilled (0x00446ad0 on +0x17964).
  Handle view_vehicle() override { return e->field32(e->peek32(0x5f4c8c), 0x64); }
  Handle mech_screen(Handle vehicle) override {
    void* hud = program_mech_hud(e->object_of(vehicle));
    return hud ? e->handle_of(hud) : 0;
  }
  void objectives_box_reset(Handle screen) override { program_hud_objectives_fill(e->object_of(screen)); }
  ObjectiveGroup* group(Handle h) const { return static_cast<ObjectiveGroup*>(e->object_of(h)); }
  // The groups' handles into the missions object's +0x7c..+0x88.
  void sync() {
    Missions& ms = missions();
    ms.objectives[1] = e->handle_of(objs.primary.get());
    ms.objectives[2] = e->handle_of(objs.secondary.get());
    ms.objectives[3] = e->handle_of(objs.final.get());
    ms.objectives[4] = e->handle_of(objs.fail.get());
  }
};
WiredObjectiveHooks g_objective_hooks;


// [0x00479480]+0xa8: the lobby's ALLOW MOBILE BASES (piece 11c); a debug
// start's game has none.
bool mp_mobile_bases() {
  const MultiplayerGame* g = current_pilot() ? current_pilot()->multiplayer() : nullptr;
  if (!g && g_mission_engine && g_mission_engine->launch().type == 3) g = g_mission_engine->launch().multi;
  return g && g->ba8;
}

struct WiredMissionHooks : MissionHooks {
  void hooks_check() override { hook_census_install(); }
  Handle multiplayer() override { return e->launch().type == 3 ? 1 : 0; }  // 0x00479480
  // The loss in a network game (0x0048f9e0): the player's centre torso
  // (0x0041d510's part 1) given no internal structure (its vt+0x3c with 0, 0).
  Handle mech_action(Handle mech, int32_t v) override {
    void* part = program_mech_part(e->object_of(mech), v);
    return part ? e->handle_of(part) : 0;
  }
  void action_start(Handle a) override { program_part_set_internal(e->object_of(a), 0.0f, 0); }
  uint8_t mp_flag() override { return mp_mobile_bases() ? 1 : 0; }
  void mp_stations(Handle h) override { program_mp_stations(e->object_of(h)); }  // 0x00490950
  WiredObjectiveHooks& ob() { return g_objective_hooks; }
  // The end's cockpit (mission.cpp cockpit): 0x00430aa0 on the view's own mech's
  // +0x684, and its objectives box (+0x17964) shown for a time (vt+0x60, -1 held).
  Handle mech_screen(Handle data) override {
    void* hud = program_pilot_hud(e->object_of(data));
    return hud ? e->handle_of(hud) : 0;
  }
  void screen_flash(Handle obj, float v) override {
    void* hud = program_mech_hud(mech_mission_player());
    if (hud && obj == e->handle_of(hud) + 0x17964) static_cast<Hud*>(hud)->objectives.show_for(v);
    else MissionHooks::screen_flash(obj, v);
  }
  void screen_reset(Handle obj) override {  // 0x00446ad0 on the box: its lines from the objectives again
    void* hud = program_mech_hud(mech_mission_player());
    if (hud && obj == e->handle_of(hud) + 0x17964) program_hud_objectives_fill(hud);
    else MissionHooks::screen_reset(obj);
  }
  // The cleanup 0x00491780's calls that have ports (docs/mission-frame.md,
  // The cleanup): zClass's delete mode, the view let go of the player's mech
  // before it is deleted and its camera mode 0, the radar objects, the
  // regions (unfreed, a later mission's lookups by name found the earlier
  // mission's), the player controls' pilot record, the mission's archives.
  Handle fn_4eb040() override { return static_cast<Handle>(zcls::immediate_delete()); }
  void fn_4eb030(Handle v) override { zcls::immediate_delete() = static_cast<int32_t>(v); }
  void view_fn_40ae70(Handle) override { program_view_set_camera(0); }
  void view_fn_40adf0(Handle) override { program_view_detach(); }
  void fn_4b4ab0(Handle) override { radar_objects_create(); }
  void keys_reset() override { zin::kbd_reset(); }  // 0x0051a300
  // 0x005964c0: the scripts' ScrollAlways list, its offsets zeroed (0) or freed with its holder (1).
  void interp_5964c0(int32_t v) override { zint::scroll_list_clear(v != 0); }
  void fn_4b4ae0() override { radar_objects_destroy(); }
  void fn_4c5fa0() override { ai::regions_clear(); }  // 0x004c5fa0 -> 0x004c5f00
  // 0x004582c0 on the mission's map (+0xbc): every map's image released; 0.
  Handle fn_4582c0(Handle) override {
    maps_unload();
    return 0;
  }
  void fn_4d71f0() override { engine_sounds_clear(); }  // 0x004d71f0 -> 0x004add10
  // 0x00459e80: the field bases' registry and the dock's states for the
  // mission (vehicles.md, The repair dock).
  void fn_459e80(Handle) override { program_field_bases_mission_start(); }
  // The mission's end: the mcbwaynet's clear (0x0047a720 on [0x0060b544]) and delete (0x004d54f0).
  void fn_47a720(Handle) override { program_mission_waynet_clear(); }  // [0x0060b544] is the port's
  void fn_4d54f0() override { program_mcb_waynet_free(); }
  // The mission's waynet's frame (vt+0x6c): the mcbwaynet's 0x004cd490 does nothing.
  void stations_update(Handle st, float dt, Handle vehicle) override {
    if (program_mcb_waynet() && st == e->handle_of(program_mcb_waynet())) return;
    if (void* w = e->object_of(st); w && program_is_nav_waynet(w)) {  // vt+0x6c 0x00479c50
      program_nav_waynet_frame(w, e->object_of(vehicle));
      return;
    }
    MissionHooks::stations_update(st, dt, vehicle);
  }
  void fn_479850() override { program_waynet_icons_free(); }
  void map_update(float dt) override { program_field_bases_frame(dt); }  // 0x0045a270
  void fn_427300() override { player_controls_mission_end(); }
  void fn_5573d0() override { zrdr::close_archives(false); }
  void objectives_load(const char* file, const char* block, Handle, Handle, Handle, Handle, Handle) override {
    mech3::objectives_load(file, block, ob().objs, ob(), mission_dir(e->launch().index));
    ob().sync();
  }
  void objectives_start(Handle, Handle, Handle, Handle) override {  // 0x0045c4a0
    add_enemy_objectives(ob().objs.primary, ob().objs.secondary, ob());
    ob().sync();
  }
  // Training's lessons (the training game's vt+0x40, 0x004794a0, 0x004373b0).
  void game_objectives(const char* file, const char* block, Handle, Handle, Handle, Handle, Handle) override {
    if (TrainingGame* t = launch_training()) {
      training_load(*t->lessons, file, block, ob().objs, ob());
      ob().sync();
    } else {
      MissionHooks::game_objectives(file, block, 0, 0, 0, 0, 0);
    }
  }
  Handle training(Handle, Handle, Handle, Handle, Handle) override { return launch_training() ? 1 : 0; }
  uint8_t training_next(Handle t) override {
    TrainingGame* g = t ? launch_training() : nullptr;
    if (!g) return 0;
    const bool more = g->lessons->next(ob().objs) >= 0;
    ob().sync();
    return more;
  }
  uint8_t objective_check(Handle g) override { return ob().group(g) ? ob().group(g)->check_group(ob(), 1) : 0; }
  uint8_t objective_done(Handle g) override { return ob().group(g) ? ob().group(g)->done : 0; }
  uint8_t condition_met(Handle g) override { return ob().group(g) ? ob().group(g)->check(ob()) : 0; }
  uint8_t objectives_announce(Handle, Handle, Handle, Handle) override {  // 0x0045c180
    return objectives_visible(ob().objs.primary.get(), ob().objs.final.get(), ob().objs.fail.get());
  }
  void objectives_free(Handle, Handle, Handle, Handle, Handle) override {
    mech3::objectives_free(ob().objs, ob());
    ob().sync();
  }
  // The cleanup's deletes: the pilot's mech (its vt+0), then 0x0049dc40's
  // 0x005a8280 on zEntity's list: each entity's vt+0x30 (a mech's 0x004b3d30
  // does nothing) and its delete, the first each time; the AI nets' free
  // (0x00402560) is the AI's; the player's tick object goes.
  // The cleanup's sounds (0x00491780): the queue stopped (0x00570410(1)),
  // every playing voice snapshotted, stopped and the snapshot freed
  // (0x0056eb10, 0x0056f0a0, 0x0056f190), as Core's leave does.
  void fn_40db60() override { register_app_keys(); }  // the app's keys (playtest #5: X)
  void fn_570410() override { zsnd::stop_queue(1); }
  void fn_584fe0() override { zwep::kill_every_shot(); }  // 0x00584fe0: the live shots killed
  Handle sounds_bank() override { return e->sound_listener(); }
  void sounds_stop(Handle bank) override { e->sound_stop_all(bank); }
  void sounds_free(Handle bank) override { e->sound_release_all(bank); }
  void fn_414000() override { mechs_shutdown(); }  // 0x00414000 (app_mech.cpp)
  void fn_50b2f0() override { zeff::anim_free_copies(); }        // the instances' own node trees
  void fn_5394c0() override { zmod::models_purge(true, true); }  // 0x005394c0(1, 1): unused models, materials
  // 0x00576270(0) frees zUtil's default search paths ([0x0072eebc]), which the
  // port never keeps (zsnd_parm.cpp, zinterp.cpp): nothing to free.
  void fn_576270(int32_t) override {}
  void delete_object(Handle obj) override {
    if (program_nav_waynet_delete(e->object_of(obj))) return;  // the missions' +0xb4
    if (!mech_free(e->object_of(obj)) && !program_vehicle_free(e->object_of(obj)) && !program_turret_free(e->object_of(obj)))
      MissionHooks::delete_object(obj);
  }
  void fn_49dc40() override {
    auto& list = ai::entity_list();
    while (!list.empty()) {
      const Handle h = list.front();
      const size_t before = list.size();
      if (!mech_free(e->object_of(h)) && !program_vehicle_free(e->object_of(h)) && !program_turret_free(e->object_of(h)) &&
          !program_pocket_marker_end(h))
        e->unported("entity vt+0x0", {h, 1});
      // A mech without a node stays on the list (its destructor takes it off
      // only with one, as the exe's would): the front goes regardless, so the
      // loop always ends and nothing is deleted twice.
      if (list.size() == before && !list.empty() && list.front() == h) list.erase(list.begin());
    }
    // 0x00402560 frees the AI nets' edits 0x00402170 kept ([0x005f4c4c]): the
    // port builds each net from a local edit, and the next mission's build
    // replaces the net (net_hooks().rebuilt): nothing is kept to free.
    PlayerState& ps = player_state();  // [0x00609a00], the player's tick object
    if (ps.tick) {
      zcls::node_set_action(static_cast<zcls::Node*>(e->object_of(ps.tick)), nullptr);  // 0x004eb450(0)
      zcls::node_release(static_cast<zcls::Node*>(e->object_of(ps.tick)));              // 0x004f3390 -> 0x004eb050
      ps.tick = 0;
    }
  }
  void pilot_set_vehicle(Handle v) override {  // 0x004782c0 on the pilot
    const Handle p = e->pilot();
    if (!p) return;
    if (v == 0) {
      int32_t mode = 0;
      if (mech_player_release(e->object_of(e->field32(p, 0x14)), &mode)) e->set_field32(p, 0x80, static_cast<uint32_t>(mode));
    }
    e->set_field32(p, 0x14, v);
  }
  void fn_4ea750() override { ai::ai_switch_off(); }  // the AI switch off
  // The mission listeners ([0x0060b888], 0x004e78d0): the pocket commander
  // (0x0046ab73, vtable 0x005c3370), the weapon types (0x004b4c53,
  // 0x005c90f8), the mission boundary (0x004c3c9a, 0x005cac78), the deploy
  // state (0x004cfed3, 0x005cc880) and the animation owners (0x004d490f,
  // 0x005ccd90); every slot not named below is 0x004d7210 (nothing). Slot
  // +0x04 at the load's step 9 (0x004e7b00) is only the boundary's read
  // (0x004c3da0), which the port makes with the others here (it needs only
  // the world, loaded by then).
  void fn_4e7b00() override {}
  // Slot +0x08 as the mission loads (0x004e7b40): the pocket commander's
  // (0x0046aaa0), the weapon types' ready times (0x004b4bd0) and "mcbdeploy"
  // (0x004cfe40); each independent of the others.
  void fn_4e7b40() override {
    pocket_mission_start();
    cheats_clear();  // ours: the cheats last one mission (cheats.h)
    reset_ready_times();
    ai::deploy_mission_load();
    ai::inout_mission_load();  // the boundary's slot +0x04 (above)
  }
  void fn_4d4b30() override { ai::anim_owner_bind_all(); }  // the animations' owners and valves
  // Their slot +0x0c as the world goes (0x004e7b80): the animation owners'
  // (0x004d4740) and the boundary's free (0x004c3dd0), which the port makes at
  // every mission's end instead (fn_4e7bc0: its FIX (bug) on the current area
  // needs it there).
  void ai_lists_free() override { ai::anim_owner_free(); }
  void inout_load() override { ai::inout_select(); }  // 0x004c3b80: the mission's boundary
  // The mission's landmines (landmines.cpp; the list is 0x00604e48's).
  void landmines_load(Handle, const char* file, const char* block) override { mech3::landmines_load(file, block); }
  void landmines_free() override { mech3::landmines_free(); }
  // Their slot +0x10 at the mission's end (0x004e7bc0): the pocket
  // commander's (0x0046ab10) and the animation owners' (0x004d4700); the
  // boundary's free is its slot +0x0c (0x004c3dd0, above), made here.
  void fn_4e7bc0() override {
    pocket_mission_end();
    ai::inout_mission_end();
    ai::anim_owner_mission_end();  // 0x004d4700
  }
  void fn_4c3240() override {}  // 0x004c3240: an empty function
  void fn_4e6910() override { shade_off(); }          // the mechs' sun shading off (mech_shade)
  void fn_40f040() override { message_panel_reset(); }        // the message panel cleared and out
  void fn_40f0b0(uint8_t v) override { if (message_panel()) message_panel_set(v != 0); }  // slid in or out
  GameEngine* e = nullptr;
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return e->unported(key, args);
  }
  // The loading screen is the briefing's ([0x005f4c80]: 0x004050d0, 0x00405120);
  // its lines also go to the log.
  void progress(float f) override {
    if (f == 0.0f) load_t0_ = std::chrono::steady_clock::now();  // a mission load begins (mission.cpp)
    program_briefing_progress(f);
  }
  std::chrono::steady_clock::time_point load_t0_ = std::chrono::steady_clock::now();
  void fn_431800() override { mech3::mech_canned_nodes_free(); }  // the canned effects' node copies
  // The mission's start animation (start_single_player_m<n>, else without the
  // number): found 0x0050b450, run at the load's end 0x00508c10(anim, 0, 0, 0, 0).
  Handle anim_start(const char* name) override { return e->handle_of(zeff::anim_find(name)); }
  void anim_run(Handle a) override {
    if (auto* d = static_cast<zeff::AnimDef*>(e->object_of(a))) zeff::anim_start(d, nullptr, 0, 0, 0);
  }
  // 0x0048ef00 of the end screen: the debriefing's Win State (the missions
  // object's +0xd0), or training's TR Dummy State, after a 2 s fade.
  void fade_out(Handle seq) override {
    if (seq == 0x6099a0) fade_replace(&states().end_win, 0, 1, 2, kFadeSeconds, 0, 0, 0);
    else if (seq == states().tr_dummy.addr()) fade_replace(&states().tr_dummy, 0, 1, 2, kFadeSeconds, 0, 0, 0);
    else MissionHooks::fade_out(seq);
  }
  void progress_text(const char* s) override {
    program_briefing_line(s);
    if (s && *s && !e->quiet)
      std::printf("mw3: load: %s (%lld ms)\n", s,
                  static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - load_t0_).count()));
  }
  int32_t game_mission_index() override { return e->launch().index; }
  int32_t game_type() override { return e->launch().type; }
  int32_t game_mission_number() override { return e->launch().number; }
  // The pilot's game's vt+0x28 from the menus (a campaign's saved game read
  // back by zSave); the debug start has no game.
  uint8_t game_saved() override {
    Pilot* p = e->launch().keep_pilot ? current_pilot() : nullptr;
    return p && p->current && p->current->restore() ? 1 : 0;
  }
  void fn_484270(int32_t) override { mech3::mech_types_open(); }  // 0x00484270: mechlib and the voices
  // 0x00413e60, the mechs' set-up: the hit factors by the network flag
  // ([0x00608e45]), the motions (0x005a9820, before the field bases' arms
  // read theirs) and the cockpit sounds (0x004141a0). The radar blips are
  // made when first needed.
  void mechs_init() override {
    hit_globals_init(options().multiplayer != 0);
    program_motions_open();
    mech3::cockpit_sounds_load();
  }
  void fn_4319a0() override { mech3::mech_types_refresh(); }    // 0x004319a0: the mech types' sounds again
  uint8_t app_rgb() override { return app().byte_at(0x1b9); }       // 0x00608e41
  uint8_t app_turrets() override { return app().byte_at(0x1b2); }   // 0x00608e3a
  // [0x006061cc] (HOLES): the skins' damage mode (mech_skin), as the mission load decides.
  void set_holes(uint8_t on) override {
    e->poke8(0x6061cc, on);
    skin_globals().damage_mode = on;
  }
  void set_use_archive() override { zint::globals().use_archive = 1; }
  void clear_9a17c0() override { e->poke32(0x9a17c0, 0); }
  void set_view(Handle v) override { e->poke32(0x5f4c8c, v); }
  Handle view() override { return e->peek32(0x5f4c8c); }
  void set_608c84(Handle v) override { e->poke32(0x608c84, v); }
  Handle get_608c84() override { return e->peek32(0x608c84); }
  void set_stealth(Handle v) override { e->poke32(0x609a2c, v); }
  // The stealth object (veh_stealth.h, editor.md): its controls (0x004ac860
  // and 0x004a51b0 in one), then the object; its constructor makes the
  // vehicles' radar and TARGET markers (0x004ac3d0), its destructor at the
  // world's unload frees them (0x004a4fc0 -> 0x004ac4e0).
  Handle stealth_4ac860(int32_t) override { return e->handle_of(program_stealth_controls()); }
  Handle stealth_4a51b0(Handle obj, int32_t) override { return obj; }
  Handle stealth_4a4de0(Handle, Handle ctl) override {
    void* s = ctl ? program_stealth_create(e->object_of(ctl)) : nullptr;
    return s ? e->handle_of(s) : 0;
  }
  // 0x005a84c0 on the stealth: its node renamed (the vehicles' is entity_name).
  void entity_named(Handle h, const char* name) override {
    void* s = program_stealth();
    if (!h || !s || e->object_of(h) != s) return MissionHooks::entity_named(h, name);
    if (void* node = program_vehicle_node(s); node && name) zcls::node_set_name(static_cast<zcls::Node*>(node), name);
  }
  // zEntity's list (0x005a89c0, 0x005a8820): the unload keeps the stealth out
  // while it frees the rest, then puts it back last.
  void entities_remove(Handle h) override {
    auto& l = ai::entity_list();
    l.erase(std::remove(l.begin(), l.end(), h), l.end());
  }
  void entities_add(Handle h) override { ai::entity_list().push_back(h); }
  Handle stealth() override { return e->peek32(0x609a2c); }
  // Instant Action's game (the win's waves, 0x0048f740) and its next wave.
  Handle ia_game() override {
    ActionGame* a = launch_action(e);
    return a ? e->handle_of(a) : 0;
  }
  void fn_5392c0() override { zmod::models_free_unused(); }  // 0x005392c0: the last wave's unused models freed
  void fn_49dc80(uint8_t v) override { action_lineup(v != 0); }
  void ai_list_reset(Handle list) override {  // 0x004d8610
    if (ai::ResetList* l = ai::reset_list(list)) l->reset();
  }
  uint32_t field32(Handle h, uint32_t off) override {
    uint32_t v = 0;
    if (ActionGame* a = launch_action(e); a && h == e->handle_of(a) && action_field(a, off, &v, nullptr)) return v;
    if (off == 0x578)  // a mech's animation state (the loss's "already down", 10)
      if (const int32_t st = program_mech_anim_state(e->object_of(h)); st != -1) return static_cast<uint32_t>(st);
    return e->peek32(h + off);
  }
  void set_field32(Handle h, uint32_t off, uint32_t v) override {
    if (ActionGame* a = launch_action(e); a && h == e->handle_of(a) && action_field(a, off, nullptr, &v)) return;
    e->poke32(h + off, v);
  }
  // The mission unload (Missions::unload, 0x00490480) on the ports.
  void camera_flag_off(Handle camera) override {  // 0x004f3430(camera, 0): no longer zSound's listener
    if (zcls::Node* n = mission_node(*e, camera)) zcls::camera_set_listener(n, 0);
  }
  void mech_types_clear() override { mech3::mech_types_forget(); }  // 0x004318e0
  void mech_defs_clear() override { mech3::mech_types_close(); }    // 0x004844b0: mechlib and the definitions
  void sound_set_unload_608dd4() override {}  // the app's +0x14c is never written: no set by that name
  void effects_free() override { zeff::effects_free(); }   // 0x0050d570
  void anims_free() override { zeff::anim_free_all(); }    // 0x0050b380
  void weapons_free() override { zwep::shutdown(mech3::free_weapon_type); }  // 0x00584a60(0x004b0090)
  void view_free(Handle view) override {  // its window (+0x08), then itself: the port's view
    program_view_free();
    if (view) e->set_mission_view(0, 0);
  }
  // [0x00608c84] (0x0047f2b0) is a block the program keeps nothing in, but
  // its world goes with it: the next load's nets and waynets, built before
  // the world parameters are read again, then see no world (as in the exe).
  void delete_608c84(Handle) override { world_hooks().world = nullptr, world_hooks().camera = nullptr; }
  void classes_free() override { zcls::classes_reset(); }  // 0x004f8fb0
  void models_free() override {  // 0x00526920 with the materials' 0x005372c0
    zmod::ModelPool& p = zmod::models();
    for (int32_t i = 0; i < p.count; ++i) zmod::model_release(&p.slots[i]);  // 0x00539150
    zmod::models() = zmod::ModelPool{};
    zmod::materials() = zmod::MaterialTable{};
  }
  void textures_free() override { zimg::textures_free(); }  // 0x00516f30: 0x00518b70
  void set_field8(Handle h, uint32_t off, uint8_t v) override { e->poke8(h + off, v); }
  Handle alloc(uint32_t size) override { return e->alloc_block(size); }
  void interp_define(const char* name) override {
    if (zint::Interp* in = interp()) zint::macro_set_true(*in, name);
  }
  uint8_t interp_source(const char* path) override {
    zint::Interp* in = interp();
    return in && zint::source(*in, path) ? 1 : 0;
  }
  int32_t hw_card_flag() override { return mech3::hw_card_flag(); }
  int32_t texture_memory() override { return mech3::texture_memory(); }
  uint32_t install_options(uint32_t mask) override { return e->install_options(mask); }
  void set_render_target(Handle w) override { mech3::set_render_target(w); }
  void set_display_target(Handle d) override { mech3::set_display_target(d); }
  void set_camera(Handle c) override { mech3::set_camera(c); }
  void render_rect(int32_t out[4]) override {
    const Rect& r = mech3::render();
    out[0] = r.x, out[1] = r.y, out[2] = r.x_end, out[3] = r.y_end;
  }
  Handle window_rect() override { return e->window_rect(); }
  Handle display_rect() override { return e->display_rect(); }
  const char* video_driver_name() override { return e->video_driver_label(); }  // 0x005799f0 (ours)
  const char* video_mode_name() override { return e->video_mode_label(); }      // 0x00579040 (ours)
  uint32_t video_texture_memory(int32_t dev, uint32_t* total, uint32_t* free_) override {
    const uint32_t r = e->video_card_memory(dev, total);
    *free_ = *total;
    return r;
  }
  uint32_t video_fn_390(int32_t dev) override { return e->video_card_fn_390(dev); }
  const char* version_number() override { return e->version_number(); }
  const char* message(int32_t id) override { return e->message(id); }

  // zImage's texture libraries (0x005182a0, 0x005190d0), found through the
  // mounts: each asset root's zbd/<mission>/ and zbd/ (FIX (A): the
  // original's search path "zbd;zbd\<dir>" set by the Load state).
  // 0x00518f50: the current texture group.
  void fn_518f50(int32_t v) override { zimg::textures().group = v; }
  // 0x00516f20 -> 0x005184e0 with ECX 0 (the only call): the table cleared
  // and, on Direct3D, the default record ([0x72f364], here g_fallback).
  void textures_begin(int32_t) override {
    zimg::TextureTable& t = zimg::textures();
    std::memset(t.entries, 0, sizeof t.entries);
    t.count = 0, t.group = 0;
    zimg::state() = zimg::State{};
    zimg::state().display_format = 6;
    // The mission's directory searched before the common one (the Load
    // state's "zbd;zbd\\<dir>"); the last mission's mounts taken out first.
    static std::vector<std::string> s_added;
    std::vector<std::string>& read = zutl::mounts().read;
    for (const std::string& m : s_added) {
      const auto it = std::find(read.begin(), read.end(), m);
      if (it != read.end()) read.erase(it);
    }
    s_added.clear();
    const std::string dir = mission_dir(e->launch().index);
    const std::vector<std::string> roots = read;
    for (const std::string& r : roots) s_added.push_back(r + "/zbd/" + dir);
    for (const std::string& r : roots) s_added.push_back(r + "/zbd");
    for (const std::string& m : s_added) zutl::mount(m);
    zimg::texture_load() = zimg::TextureLoad{};
    zimg::texture_hooks() = zimg::TextureHooks{};
    zimg::texture_hooks().release = [](void* s) { zvid::release_texture(static_cast<zvid::D3DTexture*>(s)); };  // 0x0057bb00
    zimg::texture_hooks().reload = [](void* s, zimg::Texture* img) {  // [0x0072f374] = 0x0057baa0: a changed image to its texture
      zvid::reload_texture(static_cast<zvid::D3DTexture*>(s), img);
    };
    zimg::texture_hooks().release_all = [] {};  // [0x0072f37c] = 0x005ac5c0, which does nothing
    zimg::texture_hooks().projected = zmod::mark_is_map;  // 0x00542710: a mark image stays a software image
    zvid::set_pixel_format(5, 6, 5);
    zvid::channel_masks() = {0xf800, 0x07e0, 0x001f};
    zvid::set_render3d(e->platform_parts().render);
    zvid::default_texture() = &g_fallback;
    zimg::default_entry()->surface = &g_fallback;
  }
  void texture_library(const char* name, int32_t level, int32_t hw) override {
    zimg::new_library(name, level, hw != 0);
    if (std::strcmp(name, "texture") != 0) zimg::register_image_library(1);  // after the mech set, as 0x0048fcfe registers
  }
  void textures_load() override { zimg::load_group_0(); }

  // zClass's reset 0x004f8f70 and zModel's 0x00526800: fresh pools and the
  // draw table ([0x0074a2e0] = 0x0053f2e0 on Direct3D).
  void classes_reset() override {
    zcls::nodes() = zcls::NodePool{};
    zcls::nodes().free_head = -1;
    zcls::lists() = zcls::Lists{};
    zmth::reset_stack();
  }
  void models_reset() override {
    zmod::materials() = zmod::MaterialTable{};
    zmod::models() = zmod::ModelPool{};
    zmod::reset_pool();
    zmod::fog() = zmod::Fog{};
    zrndr::fog_color() = zrndr::FogColor{};
    zrndr::device_fog().active = 1;  // the Direct3D path
    zvid::install_model_draw_table();
    zmod::use_device_light_output();  // the lighting's point colour and fog mix to the device
    zcls::scene_walk() = zcls::SceneWalk{};
    zmod::walk_state() = zmod::WalkState{};
    zmod::walk_state().draw = zmod::draw_model;
    zmod::zones() = zmod::Zones{};
    zmod::shadow_casters() = zmod::ShadowCasters{};
    zmod::draw_state() = zmod::DrawState{};
    zmod::cull_hooks() = zmod::CullHooks{};
    zcls::update_hooks() = zcls::UpdateHooks{};
    zcls::WorldRenderHooks& h = zcls::world_render_hooks();
    h = zcls::WorldRenderHooks{};
    h.begin_scene = [](void*) { zvid::scene_begin(); };
    h.end_scene = [](void*) { zvid::scene_end(); };
    h.flush_transparent = [](void*) { zvid::flush_transparent(); };
    h.flush_overlay = [](void*) { zvid::flush_overlay(); };
    h.draw_quad = [](void*) { zvid::draw_tint_quad(); };
    h.puffers = [](void*) { zmod::puff_draw(); };
    h.world_sounds = [](void*, zcls::Node* world) { zcls::world_sounds(world); };  // 0x004f3170
    // 0x004eda10 at the camera: the ground below kept to one hit, its polygon's zone word.
    h.zone_probe = [](void*, zcls::Node* world, const float pos[3], uint32_t* zone) -> int32_t {
      static zcls::GroundHits hits;
      zcls::ground_below(world, pos, &hits);
      if (!hits.count) return 0;
      *zone = hits.hit[0].zone[0];
      return 1;
    };
    h.listener_speed_limit = [](void*) { return zsnd::speed_of_sound(); };       // 0x00572360
    h.listener = [](void*, const float* m, const float v[3]) { zsnd::set_listener(m, v); };  // 0x00571e50
  }
  Handle find_node(int32_t list, const char* name) override { return e->handle_of(zcls::list_find_by_name(list, name)); }
  void lists_4f7f70() override { zcls::frame_update(); }
  void lists_4f7bd0() override { zcls::purge_lists(); }
  // The world files keep a 320x200 window; set to the screen mode's.
  void window_set_size(Handle node, int32_t w, int32_t h) override {
    zcls::Node* n = mission_node(*e, node);
    if (n && n->data) {
      auto* d = static_cast<zcls::Window*>(n->data);
      d->put<int32_t>(0x8, w), d->put<int32_t>(0xc, h);
    }
    if (e->platform_parts().render) e->platform_parts().render->set_viewport(0, 0, w, h);
  }
  void window_set_origin(Handle node, int32_t x, int32_t y) override {
    zcls::Node* n = mission_node(*e, node);
    if (n && n->data) {
      auto* d = static_cast<zcls::Window*>(n->data);
      d->put<int32_t>(0x0, x), d->put<int32_t>(0x4, y);
    }
  }
  // The view and its window (0x0040aca0, 0x004b52e0, not ported): blocks in
  // the engine's memory, the window node at the window's +4 as the
  // constructor keeps it; the camera the Core state draws through.
  Handle make_view_window(Handle mem, Handle window) override {
    unported("0x004b52e0", {mem, window});
    e->poke32(mem + 4, window);
    return mem;
  }
  // 0x0040aca0 is PlayerView's constructor (player_view.cpp), the program's.
  Handle make_view(Handle mem, Handle camera, Handle window, Handle) override {
    e->set_mission_view(mem, camera);
    program_view_make(mission_node(*e, camera));
    return mem;
  }
  Handle make_608c84(Handle mem, Handle world) override {
    unported("0x0047f2b0", {mem, world});
    build_soil_table();  // 0x0047f400, in the constructor
    // [0x00608c84]+4, the world node, from here on (the stealth object, made
    // later in this load, goes into it; read_world_params sets it again).
    world_hooks().world = world ? static_cast<zcls::Node*>(e->object_of(world)) : nullptr;
    return mem;
  }
  void anim_allow_loading(int32_t on) override { zeff::anim_allow_loading(on); }
  // The animations' calls out: puffers (gmod_puff) and sounds (zSound) are
  // ported; zEffect's effect table 0x0050d9e0 is not, so a def naming an
  // effect is left out of the post-load pass (anim-postload.md).
  void anim_set_world(Handle world) override {
    zeff::AnimHooks& a = zeff::anim_hooks();
    // 0x004f9b50 with [0x005ebcb4] and [0x005ebcb8] (both 1): a running def's
    // second instance at no node (an impact while the last one plays) copies its tree.
    a.copy_tree = [](zcls::Node* n) { return zcls::tree_copy_materials(n, 1, 1); };
    a.create_puffer = []() -> void* { return zmod::puffer_create(); };
    a.find_sound = [](const char* name) -> void* { return zsnd::find(name); };
    a.effect_index = zeff::effect_find;  // 0x0050d9e0
    zcls::set_find_sound(a.find_sound);
    zmod::puff_hooks().create_node = [](void*) { return zcls::object3d_create(); };
    wire_puff_node();
    wire_anim_run();
    zeff::anim_set_world(mission_node(*e, world));
  }
  // zEffect's calls while the animations run (zeff::AnimRunHooks), unset
  // until now: an anim's sounds were silent, its falls found no ground
  // (c1m3's buildings, campaign.md section 14), and the rest were skipped
  // without the census seeing them. The ground (0x004edab0), zSound's plays
  // and a weapon gone off (0x00581ed0, 0x005822c0) are the ports; the others
  // go to the census by their address.
  static void wire_anim_run() {
    zeff::AnimRunHooks& r = zeff::anim_run_hooks();
    r = zeff::AnimRunHooks{};
    r.play_sound = [](void*, void* s, float v) {  // 0x0056e3d0
      if (s) zsnd::play(static_cast<zsnd::Object*>(s), v);
    };
    r.play_sound_at = [](void*, void* s, const float p[3], float v) {  // 0x0056e7e0
      if (s) zsnd::play_at(static_cast<zsnd::Object*>(s), p, v, nullptr);
    };
    r.ground = [](void*, zcls::Node* w, float x, float y, float z, zeff::GroundHits* out) {  // 0x004edab0
      static zcls::GroundHits g;
      out->count = 0;
      if (!w || zcls::ground_point(w, x, y, z, &g) != 0) return;
      for (int32_t i = 0; i < g.count && i < 36; ++i) {
        const zcls::GroundHit& h = g.hit[i];
        out->hit[i].y = h.height;
        out->hit[i].has_surface = h.material != nullptr;  // +0x20, its +0x20 the soil
        out->hit[i].surface = h.material ? static_cast<int32_t>(h.material->soil) : 0;
        out->count = i + 1;
      }
    };
    r.detonate = [](void*, uint8_t* name, const float p[3]) {  // 0x00581ed0, then 0x005822c0
      if (zwep::WeaponDef* d = zwep::find_def(reinterpret_cast<const char*>(name))) {
        const zwep::Vec3f3 at{p[0], p[1], p[2]};
        zwep::burst_at(d, &at);
      }
    };
    r.ray_down = [](void*, zcls::Node*, const float*, float, int32_t*) -> int32_t {
      if (GameEngine* g = engine_of_hooks()) g->unported("0x00507220", {});
      return 1;
    };
    r.effect = [](void*, zeff::AnimDef*, uint8_t*) {
      if (GameEngine* g = engine_of_hooks()) g->unported("0x005031b0", {});
    };
    r.camera_state = [](void*, zeff::AnimDef*, uint8_t*) {
      if (GameEngine* g = engine_of_hooks()) g->unported("0x00503c10", {});
    };
    r.camera_from_to = [](void*, zeff::AnimDef*, zeff::SeqDef*, uint8_t*) -> int32_t {
      if (GameEngine* g = engine_of_hooks()) g->unported("0x00503d50", {});
      return 2;
    };
    r.remove_child = [](void*, zeff::AnimDef*, uint8_t*) {
      if (GameEngine* g = engine_of_hooks()) g->unported("0x004ebb40", {});
    };
    // Event 36's tint to the weather manager's (0x005a6730), drawn by its
    // next update (anim-run.md, The screen tint).
    r.screen_tint = [](void*, uint16_t colour, double alpha) { ztwod::weather_tint(colour, alpha); };
    r.fbfx_wave = [](void*, zeff::AnimDef*, zeff::SeqDef*, uint8_t*) -> int32_t {
      if (GameEngine* g = engine_of_hooks()) g->unported("0x005076c0", {});
      return 2;
    };
    r.message = [](void*, uint8_t*) {
      if (GameEngine* g = engine_of_hooks()) g->unported("0x00507970", {});
    };
  }
  void anim_load(const char*) override { zeff::anim_load(); }
  void anim_cleanup() override { zeff::anim_stop_all(1); }  // 0x0050ace0(1)
  // 0x00501ab0: declient.zrd's crater and quicksand settings (craters.md).
  // 0x00502020: the craters' history, the quicksand frames and the texture
  // table go with the mission.
  void decals_free() override { zdec::decals_free(); }
  // The craters' ties: zGeometry finds the world through zDEClient, and the
  // dust is an animation at the crater (0x00508aa0, no node, unturned).
  void decals_load(Handle world, int32_t) override {
    zdec::init(mission_node(*e, world));
    zgeo::hooks().world_node = zdec::world_node;
    zdec::crater_hooks().play_anim = [](zeff::AnimDef* a, const float at[3]) {
      static const float zero[3] = {0, 0, 0};
      zeff::anim_start_at(a, nullptr, at, zero, zero);
    };
  }
  // 0x0050d2c0: zEffect's table from effects.zrd (an empty list in both games).
  void effects_load(Handle, Handle, const char* file) override {
    zeff::EffectHooks& h = zeff::effect_hooks();
    h.unported = [](const char* key, int32_t i) {
      if (GameEngine* g = engine_of_hooks()) g->unported(key, {i});
    };
    h.error = [](const char* f) {
      if (GameEngine* g = engine_of_hooks(); g && !g->quiet) std::fprintf(stderr, "mw3: Failed to read %s\n", f);
    };
    zeff::effects_init(file);
  }
  void world_params(const char* file, const char* block) override {
    read_world_params(file, block, mission_node(*e, e->mission_camera()), e->launch().type, e->launch().number);
  }
  void reader_open(const char* path, int32_t current) override { zrdr::open_archive(path, current != 0); }
  // 0x00583880: weapons.zrd into zWeapon's definitions (each def's game half
  // by 0x004b0020, mech3/weapon), the projectile pool, its nodes and the
  // impact lights, the weapon_tick object (priority 3, its action
  // zwep::tick). The animations are the mission's (0x0050b450), the effects
  // zEffect's table, the crater defaults zDEClient's (read just before).
  void weapons_load(Handle world, const char* file, int32_t a, uint32_t callback) override {
    zwep::InitHooks& w = zwep::init_hooks();
    w = zwep::InitHooks{};
    w.sound = [](const char* n) -> zwep::Handle { return zsnd::find(n); };
    w.message = zutl::localize;
    w.effect = zeff::effect_find;                                    // 0x0050d9e0
    w.anim = [](const char* n) -> zwep::Handle { return zeff::anim_find(n); };  // 0x0050b450
    w.lights_init = []() { zwep::lights_init(); };                    // 0x00586970
    w.crater_defaults = [](zwep::Crater* out) {  // 0x004ff920
      const zwep::CraterOrder& c = zdec::state().crater;
      *out = zwep::Crater{};
      out->x00 = c.x00, out->points = c.points, out->x08[1] = c.x0c, out->depth = c.depth, out->radius = c.radius;
    };
    w.set_intersect_surface = [](zwep::Handle n, int32_t on) {        // 0x004eb6d0
      zcls::node_set_intersect(static_cast<zcls::Node*>(n), on);
    };
    w.tree_unlit = [](zwep::Handle n) {                               // 0x004f9e00 with 0x004f9de0
      return zcls::node_tree_any(static_cast<zcls::Node*>(n), zcls::node_is_billboard);
    };
    w.find_node = [](int32_t list, const char* n) -> zwep::Handle { return zcls::list_find_by_name(list, n); };
    w.set_active = [](zwep::Handle n, int32_t on) { zcls::node_set_active(static_cast<zcls::Node*>(n), on); };
    w.node_destroy = [](zwep::Handle n) { zcls::node_delete(static_cast<zcls::Node*>(n)); };  // 0x004eaea0
    w.object3d_create = []() -> zwep::Handle { return zcls::object3d_create(); };
    w.pool_objects = [](int32_t create) { zwep::pool_objects(create); };  // 0x005847c0: the shots' nodes
    w.set_action_priority = [](zwep::Handle n, int32_t p) {
      zcls::node_set_priority(static_cast<zcls::Node*>(n), p);
    };
    w.set_tick_action = [](zwep::Handle n) {
      zcls::node_set_action(static_cast<zcls::Node*>(n), [](zcls::Node*) { zwep::tick(); });
    };
    w.error = [](int32_t level, int32_t line, const char* what) {
      if (GameEngine* g = engine_of_hooks(); g && !g->quiet)
        std::fprintf(stderr, "mw3: report %#x zwep_init.cpp:%d %s\n", level, line, what);
    };
    w.hardware = true;
    if (callback != 0x004b0020) unported("0x00583880", {world, file, a, callback});
    zwep::init(e->object_of(world), file, a, mech3::make_weapon_type);
  }
  // 0x0049e810: the mission's vehicles (mech3/player).
  void player_setup(uint8_t full) override {
    player_setup_ran = true;
    g_player_hooks.forget();  // the last mission's AI is gone
    mech3::player_setup(full != 0);
  }
  bool player_setup_ran = false;
  Handle sound_find(const char* name) override { return e->sound_find(name); }
  // The mission's valves.zrd (0x004e2120, mech3/zrdrvalve).
  void fn_4e2120() override { ai::valves_load(); }
  // targets.zrd and traps.zrd (0x004d2e20, 0x004d31d0, mech3/zai_target).
  void fn_4d2e20() override { ai::targets_create(); }
  void fn_4d31d0() override { ai::traps_create(); }

  // The game time [0x0060991c] to the ports' copies (the mechs', zWeapon's)
  // with each write: the mission start's 0 (0x0048f400), the frame's, the
  // cleanup's.
  void game_time_set(float t) override { mech_set_game_time(t); }
  // The frame (0x0048f470, docs/mission-frame.md): the valves' timers.
  void valves_tick(float) override { ai::valves_tick(missions().time); }
  Handle player_vehicle() override { return e->pilot() ? e->field32(e->pilot(), 0x14) : 0; }
  // 0x0051d2f0 with 0x0048f440: Exit Mission (Ctrl+Q) and 0x72 to the missions' handler.
  void bind_command(int32_t cmd, uint32_t handler) override {
    if (handler != kMissionCommand) return (void)unported("0x0051d2f0", {cmd, handler});
    zin::set_callback(cmd, [](int32_t c) { missions().on_command(c); });
  }
  uint8_t app_network() override { return options().multiplayer; }
  int32_t invincible() override { return mech3::invincible(); }
  int32_t infinite_ammo() override { return mech3::infinite_ammo(); }
  int32_t heat() override { return mech3::heat(); }
  uint8_t app_1c2() override { return app().byte_at(0x1c2); }  // 0x00608e4a, never set
  zsnd::VoiceBase* voice_of(Handle s) const { return static_cast<zsnd::VoiceBase*>(e->object_of(s)); }
  uint32_t sound_playing(Handle s) override { return static_cast<uint32_t>(zsnd::status(voice_of(s))); }
  void sound_stop(Handle s) override { zsnd::stop(voice_of(s)); }
  Handle sound_play(Handle s, float volume) override {
    auto* o = static_cast<zsnd::Object*>(e->object_of(s));
    return o ? e->handle_of(zsnd::play(o, volume)) : 0;
  }
  // The cleanup (0x00491780): the valves end, the voice set of the mission
  // goes; the clean-ups of modules not ported are logged.
  void cleanup(int32_t unload) override { missions().cleanup(unload != 0); }
  // "Deleting AI Nets" (0x0058b290 on [0x00603fd8]): the mission's nets,
  // regions and waynets go before the valves their steps point at.
  void znet_free() override { znet::clear(); }
  // FIX (bug): the player's controls live for the game (0x00427280) and keep
  // the last mission's aiv.zrd record, whose vlist names that mission's
  // valves; the next launch's placement update runs it before the new record
  // comes (a read of freed valves in the exe). Here it goes with the valves.
  void valves_end() override {
    ai::valves_end();
    if (PlayerControls* pc = player_controls_instance()) pc->settings = ai::AiParams{};
  }
  void targets_end() override { ai::targets_end(); }
  void fn_4d8270() override { ai::targets_delete_all(); }  // the targets and traps deleted (0x004d8360)
  // The turrets (app_turret.cpp): aiTurrets.zrd's (0x004a76b0, `at` the
  // camera's zones, which the turrets take from the view here), their
  // markers and file freed (0x004a7970).
  void turrets_load(const char* file, Handle) override { program_turrets_load(file); }
  void fn_4a7970() override { program_turrets_end(); }
  void voice_release(const char* name) override { zsnd::unload_set(name); }
  void sound_reset() override { zsnd::reset(); }
  void zerr(int32_t level, const char*, int32_t line, const char* fmt, const char* arg) override {
    char text[512];
    std::snprintf(text, sizeof text, fmt, arg);
    std::fprintf(stderr, "mw3: report %#x mission.cpp:%d %s\n", level, line, text);
  }
};
WiredMissionHooks g_mission_hooks;
}  // namespace
void program_objectives_start() { g_mission_hooks.objectives_start(0, 0, 0, 0); }  // 0x0045c4a0

void mission_ai_mech_forget(const void* mech, uint32_t handle) {
  for (size_t i = 0; i < g_ai_mechs.size(); ++i) {
    if (g_ai_mechs[i].mech != mech) continue;
    g_ai_mechs.erase(g_ai_mechs.begin() + static_cast<std::ptrdiff_t>(i));
    g_ai_names.erase(g_ai_names.begin() + static_cast<std::ptrdiff_t>(i));
    break;
  }
  g_player_hooks.mechs.erase(handle);
}

const AiMechMade* mission_ai_mechs(int32_t* count) {
  for (size_t i = 0; i < g_ai_mechs.size(); ++i) g_ai_mechs[i].name = g_ai_names[i].c_str();
  if (count) *count = static_cast<int32_t>(g_ai_mechs.size());
  return g_ai_mechs.data();
}

void mission_lose() { missions().lose(); }
void mission_win() { missions().complete(); }
Objectives& mission_objectives() { return g_objective_hooks.objs; }
ObjectiveHooks& mission_objective_hooks() { return g_objective_hooks; }
void* mission_sun() { return missions().world.sun ? missions().world.sun->node : nullptr; }
int mission_game_type() { return g_mission_engine ? g_mission_engine->launch().type : 0; }
uint8_t mission_aborting() { return missions().aborting; }
// Mech3.exe 1.2 @ 0x00491c80
void* mission_map() {
  if (!g_mission_engine) return nullptr;
  Missions& m = missions();
  if (!m.xbc) {
    if (!m.xb8) m.xb8 = g_mission_engine->handle_of(new MissionMap(nullptr));
    m.xbc = m.xb8;
  }
  return g_mission_engine->object_of(m.xbc);
}
uint32_t mission_map_find(const char* name) {
  MissionMap* m = maps_find(name);
  return m && g_mission_engine ? g_mission_engine->handle_of(m) : 0;
}
void mission_map_set(uint32_t map) { missions().xbc = map; }  // [0x0060998c]

// The chains' (ours): an entity by the valves' and objectives' name lookup
// (0x004d7330: the AI mechs, turrets, vehicles, targets), as a handle.
uint32_t chain_entity_named(const char* name) {
  return g_mission_engine ? static_cast<uint32_t>(entity_by_name(*g_mission_engine, name)) : 0;
}
void* mission_map_of(uint32_t map) { return g_mission_engine ? g_mission_engine->object_of(map) : nullptr; }
// The campaign's vt+0x48 and vt+0x4c (0x00437870, 0x00437880), else the launch's.
int32_t mission_operation() {
  if (!g_mission_engine) return 0;
  const CampaignGame* c = launch_campaign(g_mission_engine);
  return c ? c->operation() : g_mission_engine->launch().op;
}
void program_check_cheats() { missions().check_cheats(); }
ActionGame* mission_action_game() { return g_mission_engine ? launch_action(g_mission_engine) : nullptr; }
void mission_action_add(int32_t field, int32_t kills) {
  ActionGame* a = mission_action_game();
  if (!a) return;
  (field == 0x4c ? a->player_kills : field == 0x50 ? a->lancemate_kills : a->enemy_kills) += kills;
}

// Mech3.exe 1.2 @ 0x004378b0
int32_t mission_action_world() {
  const ActionGame* a = g_mission_engine ? launch_action(g_mission_engine) : nullptr;
  return a && a->x74 >= 0 && a->x74 <= 3 ? a->x74 + 1 : 0;
}
const char* mission_action_name() {  // 0x0040a840
  const ActionGame* a = g_mission_engine ? launch_action(g_mission_engine) : nullptr;
  return a ? a->x3c : nullptr;
}
int32_t mission_number_of_game() {
  if (!g_mission_engine) return 0;
  const CampaignGame* c = launch_campaign(g_mission_engine);
  return c ? c->mission_number() : g_mission_engine->launch().number;
}
const char* mission_nav_name(uint32_t nav) {
  return program_nav_name(g_mission_engine ? g_mission_engine->object_of(nav) : nullptr);
}

void program_objectives_again() { missions().objectives_again(); }
void program_net_clock(float left, bool on) {
  missions().clock = left;
  missions().clock_down = on ? 1 : 0;
}
void program_options_apply() { apply_all(); }

namespace {
// The pilot's network game, else a network debug start's (ours).
MultiplayerGame* net_debug_game() {
  if (MultiplayerGame* g = current_pilot() ? current_pilot()->multiplayer() : nullptr) return g;
  return g_mission_engine && g_mission_engine->launch().type == 3 ? g_mission_engine->launch().multi : nullptr;
}
}  // namespace

MultiplayerGame* mission_multiplayer_game() { return net_debug_game(); }
std::vector<std::string> mission_multiplayer_score_lines() {
  std::vector<std::string> out;
  MultiplayerGame* g = net_debug_game();
  if (!g) return out;
  g->rescore();
  g->score_texts(0x14, 0xc);
  for (const auto& l : g->score_lines) out.push_back(l->text);
  return out;
}

void wire_mission_hooks(GameEngine* e) {
  g_mission_hooks.e = e;
  g_objective_hooks.e = e;
  g_player_hooks.e = e;
  g_player_hooks.forget();  // a new engine numbers its handles afresh
  g_mission_engine = e;
  set_mission_hooks(e ? &g_mission_hooks : nullptr);
  program_set_mp_mobile_bases(e ? &mp_mobile_bases : nullptr);
  // The mission's waynet, the missions' +0xb0 ([0x00609980]): as a handle there.
  program_waynet_mission(
      e ? +[]() -> void* { return g_mission_engine->object_of(missions().stations); } : nullptr,
      e ? +[](void* w) { missions().stations = w ? g_mission_engine->handle_of(w) : 0; } : nullptr);
  AppKeyCalls& keys = app_key_calls();
  const bool debug_camera = keys.debug_camera;  // the program's flag, set before
  keys = AppKeyCalls{};
  keys.debug_camera = debug_camera;
  if (e) {
    keys.cycle_camera = [] {  // 0x0040aed0 on [0x005f4c8c]
      if (PlayerView* v = program_view()) v->cycle_camera();
    };
    keys.message_toggle = [] {  // 0x0040f070
      if (message_panel()) message_panel_toggle();
    };
    keys.vmode_cycle = [] { g_mission_engine->vmode_toggle(); };  // 0x004ab910 (resolution.md step 3)
    keys.debug_7e = [] { g_mission_engine->unported("0x004abbc0", {}); };
  }
  set_player_hooks(e ? &g_player_hooks : nullptr);
  g_brain_hooks.e = e;
  ai::set_brain_hooks(e ? &g_brain_hooks : nullptr);
  g_valve_hooks.e = e;
  ai::set_valve_hooks(e ? &g_valve_hooks : nullptr);
  g_target_hooks.e = e;
  ai::set_target_hooks(e ? &g_target_hooks : nullptr);
  // The mission boundary's (zainet_inout): the launch's game, and the
  // corners on the world's ground (0x004c4240).
  ai::InOutHooks& io = ai::inout_hooks();
  io = ai::InOutHooks{};
  if (e) {
    io.have_game = [] { return g_mission_engine != nullptr; };
    io.game_type = [] { return g_mission_engine->launch().type; };
    io.operation = [] { return g_mission_engine->launch().op; };
    io.mission_number = [] { return g_mission_engine->launch().number; };
    io.ground = [](float p[3]) {
      zcls::Node* w = world_hooks().world;
      static zcls::GroundHits hits;
      if (!w || zcls::ground_point(w, p[0], p[1] + 100.0f, p[2], &hits) != 0 || hits.count == 0) return;
      int32_t best, in_water;
      float surface;
      p[1] = ground_pick(hits, p[1], 20.0f, &best, 1, &in_water, &surface);  // 0x004abd90
    };
  }
  // The mech build and the launch (app_mech.cpp, docs/mech-build.md).
  if (!e) {
    wire_sensors(nullptr, nullptr);
    return wire_mech_build(nullptr);
  }
  static MechWiring w;
  w.engine = e;
  auto en = [](void* p) { return static_cast<GameEngine*>(p); };
  w.unported = [](void* p, const char* key, std::initializer_list<zgame::Arg> args) {
    return static_cast<GameEngine*>(p)->unported(key, args);
  };
  w.peek32 = [](void* p, uint32_t a) { return static_cast<GameEngine*>(p)->peek32(a); };
  w.poke32 = [](void* p, uint32_t a, uint32_t v) { static_cast<GameEngine*>(p)->poke32(a, v); };
  w.poke8 = [](void* p, uint32_t a, uint8_t v) { static_cast<GameEngine*>(p)->poke8(a, v); };
  w.object_of = [](void* p, uint32_t h) { return static_cast<GameEngine*>(p)->object_of(h); };
  w.handle_of = [](void* p, const void* o) { return static_cast<GameEngine*>(p)->handle_of(const_cast<void*>(o)); };
  w.vcall = [](void* p, uint32_t obj, uint32_t slot, std::initializer_list<zgame::Arg> args) {
    return static_cast<GameEngine*>(p)->vcall(obj, slot, args);
  };
  w.pilot = [](void* p) { return static_cast<GameEngine*>(p)->pilot(); };
  w.view = [](void* p) { return static_cast<GameEngine*>(p)->player_view(); };
  w.forget_object = [](void* p, const void* o) { static_cast<GameEngine*>(p)->forget_object(o); };
  (void)en;
  wire_mech_build(&w);
  wire_sensors(&w, [](void* p, int32_t id) { return static_cast<GameEngine*>(p)->message(id); });
  net_set_game_clear([] {  // the network start's (0x0046ceb0): the network game's +0x7c, +0x80, +0x84
    if (MultiplayerGame* g = net_debug_game()) g->clock_next = g->end_time = 0, g->end_state = 0;
  });
  NetGameAccess acc;
  acc.fields = [](int32_t* players, float** clock_next, float** end_time, int32_t** end_state, int32_t** kills,
                  int32_t** deaths, int32_t* score_limit, uint8_t** flags) {
    MultiplayerGame* g = net_debug_game();  // 0x00479480
    if (!g) return false;
    *players = static_cast<int32_t>(znet::lists().players.size());  // +0x50, the lobby's count (piece 11: the session's)
    *clock_next = &g->clock_next, *end_time = &g->end_time, *end_state = &g->end_state;
    *kills = &g->kills, *deaths = &g->deaths, *score_limit = g->x70;
    *flags = &g->b8c;  // +0x8c..+0x8f
    return true;
  };
  acc.exit_fade = [] { fade_replace(&states().multi_exit, 0, 1, 2, kFadeSeconds, 0, 0, 0); };  // 0x0048ef00
  acc.game_time = [] { return missions().time; };        // [0x0060991c]
  acc.clock_left = [] { return missions().clock; };      // [0x00609920]
  acc.clock_on = [] { return missions().clock_down != 0; };  // [0x00609924]
  acc.set_clock_left = [](float v) { missions().clock = v; };
  net_set_game_access(acc);
  net_set_launch_game([](NetLaunchGame* out) {  // the network launch's (0x0046da80): slot 0 is +0x20 (0x00437800)
    MultiplayerGame* g = net_debug_game();
    if (!g) return false;
    out->loadout = g->slot.config, out->skin = g->slot.skin;
    out->unlimited_ammo = g->b8d != 0, out->outside_view = g->b8e != 0;
    return true;
  });
}

// 0x00490880 on the missions object: the load, then (a debug launch, our
// addition standing in for the briefing's Launch button) the Core state.
void GameEngine::missions_start(uint32_t list) {
  if (list != 0x6098d0) return Engine::missions_start(list);
  Missions& m = missions();
  // zEntity's list: emptied by the last cleanup (0x0049dc40) but for the
  // stealth object, which the cleanup puts back (the world's, kept when the
  // next mission is on the same world); a debug start's own.
  const Handle stealth = peek32(0x609a2c);
  ai::entity_list().clear();
  if (stealth) ai::entity_list().push_back(stealth);
  mech_entities_forget();
  m.start();
  if (m.failed) std::fprintf(stderr, "mw3: the mission load failed\n");
  // Typed mission-change notification, fired even when the load failed (with
  // the empty identity) so renderers drop any stale mission state.
  if (auto *render = platform_parts().render) {
    const platform::MissionIdentity identity = platform::make_mission_identity(
        launch_.type, static_cast<int32_t>(launch_.index),
        static_cast<int32_t>(launch_.number), !m.failed);
    render->mission_changed(identity);
  }
  zcls::Node* cam = mission_node(*this, camera_);
  if (launch_.active && launch_.have_pos && cam) {  // as the debug viewer places it (render.md section 0)
    zcls::camera_set_rotation(cam, -launch_.pitch, launch_.yaw + 3.14159265f, 0.0f);
    zcls::camera_set_position(cam, launch_.eye[0], launch_.eye[1], launch_.eye[2]);
  }
  if (launch_.active && launch_.auto_launch && !m.failed) {
    // The briefing's MAP pick (0x00405160 -> 0x004582a0), which a debug start
    // has none of (our addition): the campaign briefings' MAP is the
    // mission's name, c1m1 and so on.
    if (!m.xbc) {
      char map[32];
      std::snprintf(map, sizeof map, "%sm%d", mission_dir(launch_.index), launch_.number);
      ztwod::host().image_path("..\\data\\common\\images\\maps");
      briefing_pick_map(map);
    }
    // The briefing's Launch (0x0040500a): a network game's start first
    // (0x0046ceb0, briefing.cpp), then the player's mech (0x00491150).
    if (launch_.type == 3) multiplayer_launch_prep();
    LaunchedMech s;
    if (!launch_player() || !launched_mech(&s)) {
      std::fprintf(stderr, "mw3: the launch made no mech\n");
    } else if (!quiet) {
      std::fprintf(stderr, "mw3: launched the %s: %d parts, %d weapons, %d heat sinks, %.1f m tall, %.1f m/s\n",
                   mech_name(s.roster), s.parts, s.weapons, s.sinks, static_cast<double>(s.height),
                   static_cast<double>(s.max_speed));
    }
    app().replace(app().state(5), 0);
  }
}

// 0x004a8cd0 and 0x004d5ca0: the ports (mech3/sensors).
void GameEngine::sensors_update() { ai::sensors_update(); }
void GameEngine::entities_sort() { ai::entities_sort(); }
void GameEngine::ai_on() { ai::ai_switch_on(); }
// Mech3.exe 1.2 @ 0x00575a00 (ztimer::update), once a frame from zGameApp::Run:
// the step [0x0072c52c] is one global in the exe, so its copies in the mech,
// weapon and zWeapon modules are set with it.
// Mech3.exe 1.2 @ 0x004783a0 (ECX the pilot), each Core frame: its mech's
// world position (0x005a85e0) is the player the mission animations' range
// tests measure from (0x00502d40). The exe has no check for a pilot without
// a mech.
void GameEngine::pilot_update(Handle p, float) {
  void* mech = p ? object_of(field32(p, 0x14)) : nullptr;
  float pos[3];
  if (mech && mech_world_position(mech, pos)) zeff::anim_set_player(pos);
}

// The GfxFlags (docs/render-lighting.md): the starts' part (0x005266f0 in
// 0x00526510, 0x00557a95 in 0x005578f0; the rest logged as before), then the
// settings entry the app and the options menu name (0x00526750, 0x00557ae0).
int32_t GameEngine::models_init() {
  zmod::gfx_flags_start(zrndr::device_fog().active != 0);
  return Engine::models_init();
}
int32_t GameEngine::render_init() {
  zrndr::gfx_flags_start(zrndr::device_fog().active != 0);
  return Engine::render_init();
}
void GameEngine::models_load(const char* name, int32_t flags) { zmod::use_gfx_flags(name, static_cast<uint32_t>(flags)); }
void GameEngine::render_load(const char* name, int32_t flags) { zrndr::use_gfx_flags(name, static_cast<uint32_t>(flags)); }
void GameEngine::model_use_gfx_flags(const char* name, int32_t flags) {
  zmod::use_gfx_flags(name, static_cast<uint32_t>(flags));
}
int32_t GameEngine::weapons_init() { return zwep::startup(); }
void GameEngine::save_init() { zsave::init(); }
void GameEngine::save_shutdown() { zsave::shutdown(); }
void GameEngine::anim_enable(int32_t on) { zeff::anim_enable_save(on); }
void GameEngine::weapons_set_mode(int32_t v) { zwep::set_network(v); }

void GameEngine::timer_update() {
  movie_cd_pending_step();  // ours (W47): a movie's held-back CD resume
  ztimer::update();
  sync_frame_step(ztimer::state().dt, ztimer::state().total_scaled);
}

// Mech3.exe 1.2 @ 0x00583870 -> 0x00584920: zWeapon down (its pool's nodes
// and the defs), without the weapon types' free callback, as the exe.
void GameEngine::weapons_shutdown() { zwep::shutdown(nullptr); }

// Mech3.exe 1.2 @ 0x0041ca30 on the launched mech
void GameEngine::mech_set_mode_a(Handle mech, int32_t a) {
  if (void* m = object_of(mech); m && is_launched_mech(m)) mech_set_hud_fall(m, a);
  else Engine::mech_set_mode_a(mech, a);
}
// Mech3.exe 1.2 @ 0x0041cc30 (Mech::power) on the launched mech
void GameEngine::mech_set_mode_b(Handle mech, int32_t a) {
  if (void* m = object_of(mech); m && is_launched_mech(m)) mech_power(m, a);
  else Engine::mech_set_mode_b(mech, a);
}

// 0x0048ee70: the port's fade (mech3::fade_push, next 0); the Core state's
// mission start fades in from black over 2 s, then gets resume(7).
void GameEngine::fade(int32_t a, int32_t b, int32_t c, uint32_t d, int32_t e, int32_t f, int32_t g) {
  fade_push(nullptr, static_cast<uint32_t>(a), b, c, zgame::from_bits(d), static_cast<uint8_t>(e),
            static_cast<uint8_t>(f), static_cast<uint8_t>(g));
}

// 0x0048f470 on the missions object, each Core frame: the port's (its
// valves' tick copies the game time to the ports' [0x0060991c]).
void GameEngine::missions_update(uint32_t list, float dt) {
  if (list != 0x6098d0) return Engine::missions_update(list, dt);
  missions().update(dt);
}

// 0x00490720 (the Load state's): the mission number (+0x64, the game's
// vt+0x4c), which the cleanup names the mission's voice set by, the
// mission's readers (0x00490630), and the sound
// sets the mission plays from: "common" (the weapons' own sounds), "mech",
// the mission directory's and "<dir><block>" (its voices), unless the game is
// of kind 6 (vt+0x54) or the briefing voices are off (app +0x1c3); then
// the mission's nav waynet (0x004797c0 into +0xb0 / +0xb4) by the game type
// (vt+0x2c): waynet_N for 0, 1 and 4, waynet_iaN for 2, waynet_mpN for 3.
void GameEngine::missions_reset(uint32_t list) {
  // Ours: the last mission's world is freed by now in the port (its cleanup's
  // world_clear); the waynets built here see none, as the exe's do before
  // the world parameters are read again.
  if (list == 0x6098d0) world_hooks().world = nullptr, world_hooks().camera = nullptr;
  if (list != 0x6098d0 || !pilot_) return void(Engine::missions_reset(list));  // the exe reads the pilot's game
  missions().number = launch_.number;
  missions().open_readers(mission_dir(launch_.index));  // 0x00490630
  if (launch_.index != 6 && !app().no_voices()) {
    const char* dir = mission_dir(launch_.index);
    zsnd::load_set("common");  // 0x0056f520
    zsnd::load_set("mech");
    zsnd::load_set(dir);
    char block[32] = {};
    missions().block_name(block);  // 0x00490600
    zsnd::load_set((std::string(dir) + block).c_str());  // "%s%s"
  }
  const int32_t type = launch_.type;
  const char* fmt = type == 2 ? "waynet_ia%d" : type == 3 ? "waynet_mp%d"  // 0x005e754c, 0x005e7540
                    : (type == 0 || type == 1 || type == 4) ? "waynet_%d" : nullptr;  // 0x005e7558
  if (fmt) {
    char name[32];
    std::snprintf(name, sizeof name, fmt, launch_.number);
    void* w = program_nav_waynet_make(name);
    missions().xb4 = missions().stations = w ? handle_of(w) : 0;
  }
}

// The debug launch's pilot and game (our addition): [0x0060861c] a pilot
// whose game (+0x6c) answers the type, number and index. Its vehicle (+0x14)
// is none until the launch (0x00491150) makes the player's mech, as the
// exe's cleanup leaves it (0x00491868): the AI brains made while the mission
// loads see no player yet, so each with a voice gets its radio (0x004e7e20).
// A stand-in mech block there until 2026-10-30 made them see a player
// without allies: no lancemate ever answered an order (playtest W11).
void GameEngine::set_launch(const Launch& l) {
  launch_ = l;
  launch_.active = true;
  g_debug_campaign.reset();
  slot_ = 0;  // made again for this launch's mech: a mission unload frees the last one's types
  // The blocks are made once and reused; the chosen pilot's handle is kept.
  const Handle real = peek32(0x60861c);
  if (!stub_pilot_) stub_pilot_ = alloc_block(0x100);
  if (!game_) game_ = alloc_block(0x100);
  pilot_ = l.keep_pilot && real ? real : stub_pilot_;
  poke32(0x60861c, pilot_);
  poke32(pilot_ + 0x6c, game_);
  poke32(pilot_ + 0x14, 0);
}
Handle GameEngine::pilot_game(Handle p) { return p == pilot_ && pilot_ ? game_ : Engine::pilot_game(p); }
uint32_t GameEngine::pilot_campaign_number(Handle p) {
  return p == pilot_ && pilot_ ? static_cast<uint32_t>(launch_.index) : Engine::pilot_campaign_number(p);
}

// Mech3.exe 1.2 @ 0x0040ad90: the draw path resets the view before its update
// (app_states.cpp), so the camera snaps to the mech each frame.
void GameEngine::view_reset(Handle view) {
  if (view && view == view_ && program_view()) {
    program_view()->reset();
    return;
  }
  Engine::view_reset(view);
}

// The cockpit view (playtest #4): the mech hidden for the world, then its
// cockpit model and arms drawn in passes of their own (app_states).
Handle GameEngine::vehicle_node(Handle veh) {
  void* m = object_of(veh);
  return entity_is_mech(m) ? handle_of(mech_node_of(m)) : Engine::vehicle_node(veh);
}
Handle GameEngine::cockpit_node(Handle veh) {
  void* m = object_of(veh);
  return entity_is_mech(m) ? handle_of(mech_cockpit_node(m)) : Engine::cockpit_node(veh);
}
Handle GameEngine::arm_node(Handle veh, int32_t off) {
  void* m = object_of(veh);
  return entity_is_mech(m) ? handle_of(mech_arm_node(m, off == 0x398 ? 1 : 0)) : Engine::arm_node(veh, off);
}
void GameEngine::node_set_active(Handle node, int32_t on) {
  if (zcls::Node* n = mission_node(*this, node)) zcls::node_set_active(n, on);
}
void GameEngine::node_set_shadow(Handle node, int32_t on) {
  if (zcls::Node* n = mission_node(*this, node)) zcls::node_set_intersect(n, on);
}
// The weather manager's rect (the frame's render or window rect, by its
// number) and its tick, each weather's block copied over first.
void GameEngine::weather_rect(int32_t i, Handle rect) {
  static ztwod::Rect rects[2];
  if (i < 0 || i >= 2) return;
  const Rect* from = rect == render_rect() ? &mech3::render()
                     : rect == display_rect() ? &mech3::display()
                     : rect == window_rect() ? &mech3::window() : nullptr;
  if (from) rects[i] = {from->x, from->y, from->x_end, from->y_end};
  ztwod::weather_rect(i, from ? &rects[i] : nullptr);
}
void GameEngine::weather_tick(float dt) {
  for (ProgramWeather* w : g_weathers) weather_sync(*w);
  ztwod::weather_tick(dt);
}

// 0x004f7350 on the player camera's data (the program's camera node's).
void GameEngine::camera_draw_scene(Handle, Handle first) {
  zcls::Node* cam = mission_node(*this, camera_);
  if (cam && cam->data) zcls::camera_draw_flares(static_cast<zcls::Camera*>(cam->data), static_cast<int32_t>(first));
}
// Mech3.exe 1.2 @ 0x004b51f0 (ECX the player view): its camera node's matrices
// (0x004f4170) and the model renderer's camera (0x00526e10) set again, after
// the cockpit passes, with the matrix stack pushed around them.
void GameEngine::view_camera_refresh(Handle) {
  zcls::Node* cam = mission_node(*this, camera_);
  if (!cam || !cam->data) return;
  zmth::Mat34 local;
  zmth::push(&local);
  zcls::camera_update(cam);
  zmod::set_camera(static_cast<zcls::Camera*>(cam->data));
  zmth::pop();
}
void GameEngine::view_draw_node(Handle, Handle node, Handle) {
  zcls::Node* cam = mission_node(*this, camera_);
  zcls::Node* n = mission_node(*this, node);
  if (!cam || !n) return;
  zcls::render_subtree(cam, n, 1);  // the Direct3D path
  scene_drawn_ = true;
}

uint32_t GameEngine::vcall(Handle obj, uint32_t slot, std::initializer_list<Arg> args) {
  uint32_t r = 0;
  // A mech answers vt+0x50 with itself (vehicles.md, Telling a mech).
  if (obj && slot == 0x50 && entity_is_mech(object_of(obj))) return obj;
  if (menu_vcall(obj, slot, args, &r)) return r;
  // The chosen pilot's own game (its +0x6c from the menus, peek32): its type.
  if (Pilot* p = current_pilot(); obj && obj != game_ && p && p->current && obj == handle_of(p->current) && slot == 0x2c)
    return static_cast<uint32_t>(p->current->type());
  if (obj && obj == game_) {
    switch (slot) {
      case 0x2c: return static_cast<uint32_t>(launch_.type);
      case 0x34:  // the pilot's slot: the mech, its first configuration, skin 0
        // From the menus the pilot's game's slot as it is now (the mech lab
        // may have given it another mech or loadout since the Load state).
        if (const bool changed = launch_.keep_pilot && launch_slot_refresh(); !slot_ || changed) {
          if (!slot_) slot_ = alloc_block(0x20);
          poke32(slot_ + 4, static_cast<uint32_t>(launch_.mech));
          poke32(slot_ + 8, launch_.config ? launch_.config
                                           : debug_slot_config(launch_.mech, this, [](void* p, const void* o) {
                                               return static_cast<GameEngine*>(p)->handle_of(const_cast<void*>(o));
                                             }));
          poke32(slot_ + 0x10, static_cast<uint32_t>(launch_.skin));
        }
        return slot_;
      case 0x4c: return static_cast<uint32_t>(launch_.number);
      case 0x48: return static_cast<uint32_t>(launch_.op);
      case 0x38: return program_slot_count();  // the pilot's campaign's (the Win State's records)
      case 0x54: return static_cast<uint32_t>(launch_.index);
      case 0x28: return 0;
      default: break;
    }
  }
  // The view target's vt+0xe4 (0x0041cd90): its HUD's Direct3D pass.
  if (obj && slot == 0xe4 && view_ && obj == field32(view_, 0x64)) {
    program_hud_draw3d(zgame::from_bits(args.begin()->v));
    return 0;
  }
  // The view's +0x04 runs its camera: the world drawn by the scene walk
  // (0x004f7030), the frame counted as the class lists' pass counts it.
  // +0x08 its update (0x0040ada0, the port's).
  if (obj && obj == view_ && slot == 0x08 && program_view()) {
    program_view()->update(zgame::from_bits(args.begin()->v));
    sync_view();
    return 0;
  }
  if (obj && obj == view_ && slot == 0x04) {
    if (zcls::Node* cam = mission_node(*this, camera_)) {
      ++zcls::frames().frame;
      zcls::render_world(cam, 0);
      scene_drawn_ = true;
    }
    return 0;
  }
  return Engine::vcall(obj, slot, args);
}

const float* program_mission_fog(bool water) {
  static float f[7];
  const MissionWorld& w = missions().world;
  const float* r = water ? w.water_range : w.fog_range;
  const float* a = water ? w.water_altitude : w.fog_altitude;
  const float* c = water ? w.water_color : w.fog_color;
  f[0] = r[0], f[1] = r[1], f[2] = a[0], f[3] = a[1], f[4] = c[0], f[5] = c[1], f[6] = c[2];
  return f;
}

// The player view's camera id and target into its block, as the app code
// reads them (+0x58 a block whose +4 is the id, +0x64 the mech's handle).
void GameEngine::sync_view() {
  if (!view_) return;
  Handle mode = 0;
  if (const int32_t id = program_view_mode_id(); id >= 0) {
    Handle& b = view_modes_[id];
    if (!b) b = alloc_block(0x10);
    poke32(b + 4, static_cast<uint32_t>(id));
    mode = b;
  }
  poke32(view_ + 0x58, mode);
  void* t = program_view_target();
  poke32(view_ + 0x64, t ? handle_of(t) : 0);
}

void GameEngine::set_mission_view(Handle view, Handle camera) {
  view_ = view;
  camera_ = camera;
}

Handle GameEngine::alloc_block(uint32_t size) {
  const Handle h = next_block_;
  next_block_ += (size + 0xff) & ~0xffu;
  return h;
}

// The census stand-ins' report (app_hook_census.cpp).
void hook_census_report(const char* key) {
  if (GameEngine* g = engine_of_hooks()) g->unported(key, {});
}

// Ours (W58, docs/lancemate-pathing.md): as play starts, the 3 x 3 tiles
// round the player built when a pilot uses the grid (the lance starts by him).
void GameEngine::nav_prebuild() {
  if (zfix::options().bugs) ai::nav::restart();
  void* p = mech_mission_player();
  if (!p) return;
  int32_t made = 0;
  const AiMechMade* ai_mechs = mission_ai_mechs(&made);
  bool any = false;
  for (int32_t i = 0; i < made && !any; ++i) any = ai_mechs[i].mech && ai::nav_for(handle_of(ai_mechs[i].mech));
  if (!any) return;
  float pos[3];
  mech_world_position(p, pos);
  const auto t0 = std::chrono::steady_clock::now();
  ai::nav::Grid& g = ai::nav::grid();
  const int tx = ai::nav::tile_of(ai::nav::cell_of(pos[0])), tz = ai::nav::tile_of(ai::nav::cell_of(pos[2]));
  for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx) {
      int budget = 1 << 30;
      g.build(tx + dx, tz + dz, &budget);
    }
  if (!quiet)
    std::printf("mw3: nav: %d tiles round the player built in %lld ms\n", g.tiles_done(),
                static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()));
}

}  // namespace mech3
