// The mech build's and the launch's hooks wired to the ports (docs/mech-build.md
// section 9). Not original code: it stands where the exe's other modules
// will be. zClass's nodes, the mech data (0x00430d10) and mechlib are the
// ports; what has no port is logged once by address through the engine.
#include "mech3/entity_bind.h"
#include "mech3/landmines.h"
#include "mech3/pocket_panel.h"
#include "mech3/cockpit_sound.h"
#include "gamez/zinput/zinput.h"
#include "mech3/message_panel.h"
#include "gamez/zimage/zimg_texture.h"
#include "mech3/app_mech.h"
#include "mech3/app_vehicle.h"
#include "mech3/chat_panel.h"
#include "mech3/cheats.h"
#include "mech3/app_turret.h"
#include "mech3/turret.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <algorithm>
#include <map>
#include <memory>
#include <string>

#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zeffect/zeff_anim_save.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/list.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zvideo/zvid_format.h"
#include "mech3/app_shadows.h"
#include "gamez/zinterp/zinterp.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/app_view.h"
#include "mech3/mech3_interp.h"
#include "mech3/mech_anim.h"
#include "mech3/mech_build.h"
#include "mech3/mech_dead.h"
#include "mech3/app_sensors.h"
#include "mech3/mech_net.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/znetwork/znet_dplay.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "mech3/mech_lod.h"
#include "mech3/mech_skin.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_command.h"
#include "mech3/app_hud.h"
#include "mech3/mech_move.h"
#include "mech3/mech_skis.h"
#include "mech3/mech_wake.h"
#include "mech3/sound_extra.h"
#include "mech3/player_view.h"
#include "mech3/weapon.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zweapon/zweapon.h"
#include "mech3/mech_config.h"
#include "mech3/mech_data.h"
#include "mech3/mech_lib.h"
#include "mech3/mech_shade.h"
#include "gamez/zclass/camera.h"
#include "gamez/zmodel/gmod_draw.h"
#include "mech3/mech_names.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_salvage.h"
#include "crt/msvc_rand.h"
#include "mech3/mission_launch.h"
#include "mech3/mission_world.h"
#include "gamez/zentity/zent_manager.h"
#include "mech3/mech_control.h"
#include "mech3/zrdraiv.h"
#include "mech3/sensors.h"
#include "mech3/mech_fieldbase.h"
#include "mech3/hud.h"
#include "mech3/mech_hud.h"
#include "mech3/zai_target.h"
#include "mech3/weapon_planner.h"
#include "mech3/zai_brain.h"
#include "mech3/weapon.h"

namespace mech3 {

int32_t invincible();  // 0x0040cf20, mech3/options.h

namespace {
// The program's mechs (ours): what a zEntity entry's vt+0x2c then vt+0x50
// answers in the exe (a mech itself; the basic vehicle, a turret or a target
// 0). zEntity's list holds handles of any entity; only these are Mechs.
std::unordered_set<const void*>& mech_objects() {
  static std::unordered_set<const void*> s;
  return s;
}
// The parts freed in the mission (the FIX (A) on d.free_part below).
std::vector<std::unique_ptr<MechPart>>& freed_parts() {
  static std::vector<std::unique_ptr<MechPart>> v;
  return v;
}
Mech* entity_mech(ai::Handle h);
Vec3 mech_position(Mech& m);
}  // namespace

namespace {
int32_t g_spawn_updates = 0;  // the spawn's update steps run (0x004782f0)
float g_spawn_pos[3] = {};    // where the placement left the mech, before that step

MechWiring g_w;
zcls::Node* zn(Handle h) { return static_cast<zcls::Node*>(h); }

// The mech data's calls (mech_data.cpp): the ports, and the engine for what
// has none (the cameras, the effects by name, the game type, the error box).
class WiredData : public MechDataHooks {
 public:
  // The pilot's game by type: 3 (0x00479480) or 2 (0x00479460), a non-zero
  // handle when it is that one.
  uint32_t game_multiplayer() override { return mission_game_type() == 3 ? 1 : 0; }
  uint32_t game_instant_action() override { return mission_game_type() == 2 ? 1 : 0; }
 public:
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  Handle cameras(MechData& p) override { return program_cameras(p.roster); }      // 0x00430e70
  void cameras_free(Handle list) override { program_cameras_free(list); }  // 0x0040abf0
  void hud_free(Handle hud) override { delete static_cast<Hud*>(hud); }  // 0x0043b860 and the delete
  Handle anim_find(const char* name) override { return zeff::anim_find(name); }  // 0x0050b450, the mission's
  // The motions the gaits read (0x005a9ad0's archive) open with the first mech.
  void animations(MechData& p) override {
    if (open_motions("zbd/motion.zbd")) load_animations(p);
  }
};

// The mechs' sun shading (mech_shade.cpp, docs/shade.md) on the program's
// ports: the mission's sun, the mechs' nodes and parts, zClass's queries and
// flags, the scene walk's before and after calls, the model draw's bake.
class WiredShade : public ShadeHooks {
 public:
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  static uint32_t id(const void* p) { return p ? g_w.handle_of(g_w.engine, p) : 0; }
  static void* obj(uint32_t h) { return h ? g_w.object_of(g_w.engine, h) : nullptr; }
  static zcls::Node* node(uint32_t h) { return static_cast<zcls::Node*>(obj(h)); }
  static zcls::Light* light(uint32_t h) { return static_cast<zcls::Light*>(obj(h)); }
  zgame::Handle world_sun() override {
    auto* sun = static_cast<zcls::Node*>(mission_sun());
    return sun && sun->data ? id(sun->data) : 0;
  }
  float sun_diffuse(zgame::Handle s) override { return light(s) ? light(s)->get<float>(0x9c) : 1.0f; }
  void set_sun_diffuse(zgame::Handle s, float v) override {
    if (light(s)) light(s)->put<float>(0x9c, v);
  }
  void sun_direction(zgame::Handle s, float out[3]) override {
    for (int i = 0; i < 3; ++i) out[i] = light(s) ? light(s)->get<float>(0x84 + 4 * i) : 0.0f;
  }
  zgame::Handle entity_node(zgame::Handle e) override {
    auto* m = static_cast<Mech*>(obj(e));
    return m ? id(m->node) : 0;
  }
  zgame::Handle entity_centroid(zgame::Handle e) override {
    auto* m = static_cast<Mech*>(obj(e));
    return m ? id(m->centroid) : 0;
  }
  void node_world_cached(zgame::Handle h, float out[3]) override { mech3::node_world_cached(node(h), out); }
  void node_world_position(zgame::Handle h, float out[3]) override {
    if (zcls::Node* n = node(h)) zcls::node_world_position(n, out);
  }
  void node_intersect(zgame::Handle h, int32_t on) override {
    if (zcls::Node* n = node(h)) zcls::node_set_intersect(n, on);
  }
  void query_mode(int32_t on) override { zcls::segment_options().first_hit = on; }
  void query_exclude(uint32_t mask) override { zcls::segment_options().exclude = mask; }
  int32_t segment(const float a[3], const float b[3], uint8_t* first) override {
    static zcls::LineHits r;
    zcls::Node* w = world_hooks().world;
    if (!w) return *first = 0, 1;
    const int32_t res = zcls::segment_list(w, a, b, &r);
    *first = r.count;
    return res;
  }
  void node_flags(zgame::Handle h, uint8_t on) override {  // 0x004eb8a0, 0x004eb8e0: bits 5 and 6
    zcls::Node* n = node(h);
    if (!n) return;
    const uint32_t v = n->get<uint32_t>(0x2c) & ~0x60u;
    n->put<uint32_t>(0x2c, v | (on & 1 ? 0x60u : 0u));
  }
  int32_t frame() override { return static_cast<int32_t>(zcls::frames().frame); }
  void walk_callbacks(bool on) override {
    zcls::scene_walk().pre = on ? +[](zcls::Node* n) { shade_before(id(n)); } : nullptr;
    zcls::scene_walk().post = on ? +[](zcls::Node* n) { shade_after(id(n)); } : nullptr;
  }
  void static_lights(int32_t on) override { zmod::draw_state().static_lights = on; }
  void stamps_frozen(int32_t on) override { zcls::frames().frozen = on; }
  // 0x0040ef30 / 0x0040ef10 on the node: a part's +0x68, else its owner's +0x68c.
  ShadeLevel* part_level(zgame::Handle h) override {
    zcls::Node* n = node(h);
    if (!n || !(n->get<uint32_t>(0x28) & 0xff)) return nullptr;
    auto* p = static_cast<MechPart*>(n->owner);
    return p ? static_cast<ShadeLevel*>(p->x68) : nullptr;
  }
  ShadeLevel* mech_level(zgame::Handle h) override {
    auto* m = static_cast<Mech*>(node_owner(node(h)));
    return m && m->x68c ? &static_cast<SunShade*>(m->x68c)->level : nullptr;
  }
  int32_t part_count(SunShade& sh) override {
    auto* m = static_cast<Mech*>(obj(sh.mech));
    return m ? static_cast<int32_t>(m->parts.size()) : 0;
  }
  ShadeLevel* part(SunShade& sh, int32_t i, zgame::Handle* n) override {
    auto* m = static_cast<Mech*>(obj(sh.mech));
    *n = 0;
    if (!m) return nullptr;
    auto it = m->parts.begin();
    std::advance(it, i);
    if (!*it) return nullptr;
    *n = id((*it)->node);
    return static_cast<ShadeLevel*>((*it)->x68);
  }
  int32_t entity_count() override { return static_cast<int32_t>(ai::entity_list().size()); }
  SunShade* entity_shade(int32_t i) override {
    Mech* m = entity_mech(ai::entity_list()[static_cast<size_t>(i)]);
    return m ? static_cast<SunShade*>(m->x68c) : nullptr;
  }
  float game_time() override { return mech_game_time(); }
};
WiredShade g_shade_hooks;

class WiredBuild : public MechBuildHooks {
 public:
  // The exe's operator new (ours): an address just allocated has no handle
  // yet, so one still mapped to it is a freed object's. Forgotten, the new
  // object gets a fresh handle: the entities' sort (0x004d7360, which breaks
  // ties on the address) then keeps their creation order, run after run.
  void on_new(uint32_t, const void* p) override {
    if (g_w.forget_object) g_w.forget_object(g_w.engine, p);
  }
  // new(0x24) the mech's shade, new(0xc) a part's level (mech_shade.h).
  Handle alloc(uint32_t size) override {
    if (size == 0x24) return new SunShade;
    if (size == 0xc) return new ShadeLevel;
    return MechBuildHooks::alloc(size);
  }
  Handle x68c_init(Handle mem, Mech& m) override {  // 0x004e61e0(mech, &mech+0x380)
    auto* s = static_cast<SunShade*>(mem);
    const uint32_t me = g_w.handle_of(g_w.engine, &m);
    shade_init(*s, me, me);
    return s;
  }
  void x68c_unlink(Handle x68c) override { shade_reset(*static_cast<SunShade*>(x68c)); }  // 0x004e67c0
  Handle node_ref_init(Handle mem) override {  // 0x004e6250
    auto* l = static_cast<ShadeLevel*>(mem);
    *l = ShadeLevel{};
    shade_level_reset(*l, 0);
    return l;
  }
  void node_ref_set(Handle ref, Handle n) override {  // 0x004e66a0
    shade_level_reset(*static_cast<ShadeLevel*>(ref), WiredShade::id(n));
  }
  void entity_update(Mech& m) override { m.refresh_markers(); }  // 0x005a8500
  // 0x004d9840 on +0x5c: the pilot's AI settings' defaults (ai::AiParams's).
  void controls_brain_init(PlayerControls& c) override { c.settings = ai::AiParams{}; }
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  // 0x005833b0(mech, node, 1, 0x004161a0): hits on the mech's node go to
  // Mech::TakeDamage (app_weapons.cpp).
  void touch_handler(Mech& m, Handle) override { mech_set_damage_handler(&m); }
  MechData* mech_data(int32_t index, int32_t create) override { return mech3::mech_data(index, create); }
  Handle node_find(Handle root, const char* name) override { return zcls::node_find_in_tree(zn(root), name); }
  // Each mech its own tree: the models and their materials copied.
  Handle node_clone(Handle node) override {
    zcls::Node* copy = zcls::tree_copy(zn(node), 1, 1);
    // Gameplay clones the library's child (LOD 0) or grandchild (LOD 1).
    // Mark that actual instance boundary without changing the native tree.
    if (copy && copy->source_asset) copy->source_asset_root = true;
    return copy;
  }
  void node_remove_child(Handle parent, Handle child) override { zcls::node_remove_child(zn(parent), zn(child)); }
  void node_free(Handle node) override { zcls::tree_destroy(zn(node)); }
  void node_tree_update(Handle node) override { zcls::update_subtree(zn(node)); }
  void nodes_pending() override { zcls::update_changed(); }
  void node_set_model(Handle node, Handle model) override {
    zcls::node_set_model(zn(node), static_cast<zmod::Model*>(model));
  }
  Handle model_new() override { return zmod::model_alloc(); }
  void model_set_priority(Handle model, int32_t p) override {  // 0x0053bb20
    zmod::model_set_priority(static_cast<zmod::Model*>(model), p);
  }
  void model_add_polygon(Handle model, int32_t n, const float* v) override {  // 0x004776d0
    std::vector<float> points(v, v + 3 * n);  // the add drops colinear points in place
    ski_polygon(static_cast<zmod::Model*>(model), n, points.data());
  }
  void node_set_action(Handle node, uint32_t) override { zcls::node_set_action(zn(node), nullptr); }
  void node_set_flag8(Handle node, int32_t on) override { zcls::node_set_flag8(zn(node), on); }
  // 0x005a7f50: onto zEntity's list, the port's (mech3/sensors).
  void entity_register(Mech& m) override {  // 0x005a7f50
    ai::entity_list().push_back(g_w.handle_of(g_w.engine, &m));
    mech_objects().insert(&m);
  }
  void node_set_active(Handle node, int32_t on) override { zcls::node_set_active(zn(node), on); }
  void node_set_name(Handle node, const char* name) override { zcls::node_set_name(zn(node), name); }
  void node_add_child(Handle parent, Handle child) override {
    if (parent && child) zcls::node_add_child(zn(parent), zn(child));
  }
  void node_set_translation(Handle node, float x, float y, float z) override {
    zcls::object3d_set_translation(zn(node), x, y, z);
  }
  Handle object3d_create() override { return zcls::object3d_create(); }
  Handle view() override { return h(g_w.view ? g_w.view(g_w.engine) : 0); }
  Handle view_camera(Handle v) override { return v ? h(g_w.peek32(g_w.engine, low(v) + 0x58)) : nullptr; }
  uint32_t camera_mode(Handle c) override { return g_w.peek32(g_w.engine, low(c) + 4); }
  void view_set_camera(Handle, uint32_t mode) override { program_view_set_camera(static_cast<int32_t>(mode)); }  // 0x0040ae70
  Handle view_target(Handle v) override { return v ? h(g_w.peek32(g_w.engine, low(v) + 0x64)) : nullptr; }
  // vt+0xfc: the port's handler on the pilot's mech (mech_command.cpp).
  void view_target_command(Handle target, int32_t cmd) override {
    void* m = g_w.object_of(g_w.engine, low(target));
    if (is_launched_mech(m)) static_cast<Mech*>(m)->command(cmd);
    else MechBuildHooks::view_target_command(target, cmd);
  }
};

bool (*g_launch_game)(NetLaunchGame* out) = nullptr;  // net_set_launch_game's

class WiredLaunch : public LaunchHooks {
 public:
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return g_w.unported ? g_w.unported(g_w.engine, key, args) : 0;
  }
  Handle missions() override { return h(0x6098d0); }
  void fn_491d20(Handle) override { program_check_cheats(); }  // 0x00491d20, Missions::check_cheats
  Handle pilot() override { return h(g_w.pilot(g_w.engine)); }
  Handle view() override { return h(g_w.view(g_w.engine)); }
  uint32_t field32(Handle o, uint32_t off) override { return o ? g_w.peek32(g_w.engine, low(o) + off) : 0; }
  void set_field32(Handle o, uint32_t off, uint32_t v) override {
    if (o) g_w.poke32(g_w.engine, low(o) + off, v);
  }
  void set_field8(Handle o, uint32_t off, uint8_t v) override {
    if (o) g_w.poke8(g_w.engine, low(o) + off, v);
  }
  Config* config_of(uint32_t handle) override { return static_cast<Config*>(g_w.object_of(g_w.engine, handle)); }
  Handle game_slot(Handle game) override { return h(g_w.vcall(g_w.engine, low(game), 0x34, {0})); }
  int32_t game_type(Handle game) override { return static_cast<int32_t>(g_w.vcall(g_w.engine, low(game), 0x2c, {})); }
  int32_t use_joystick() override { return 0; }   // keyboard and mouse
  int32_t camera_mode() override { return program_camera_option(); }  // 0x0040ce70
  void set_camera_mode(int32_t m) override { program_camera_set(m); }  // 0x0040ce10
  void mech_attach_view(Mech& m) override { program_view_attach(m); }   // vt+0x108, 0x0041ce30
  void view_set_camera(Handle, uint32_t mode) override { program_view_set_camera(static_cast<int32_t>(mode)); }
  // The pilot's vehicle is the mech (as the exe's 0x004782c0 sets it).
  void pilot_set_mech(Handle p, Mech& m) override {
    set_field32(p, 0x14, g_w.handle_of(g_w.engine, &m));
    // The mech's +0x684, its MechData, for the draw paths' HUD calls (0x00430aa0).
    if (m.pilot) g_w.poke32(g_w.engine, g_w.handle_of(g_w.engine, &m) + 0x684, g_w.handle_of(g_w.engine, m.pilot));
    // The mech globals: [0x0060861c]+0x14, and the view's target (+0x64,
    // set by the view attach 0x0041ce30, not ported) is the same mech.
    world().mission_player = &m;
    world().player = &m;
    weapon_world().mission_player = &m;
    weapon_world().player = &m;
  }
  // 0x00430aa0: the pilot's HUD (made when first asked), and the two calls
  // the start makes on its objectives (+0x17964): hidden (vt+0x64), shown
  // for a time (vt+0x60). The radar's reset 0x0043d920 on HUD + 0x14: the
  // handle is the exe's 32-bit address, so the player's HUD (the view's, as
  // the launch's test says) is found again.
  Handle mech_screen(MechData* data) override { return data ? pilot_hud(*data) : nullptr; }
  void hud_reset(Handle screen) override {
    if (screen) static_cast<Hud*>(screen)->objectives.hide();
  }
  void hud_zoom(Handle screen, float v) override {
    if (screen) static_cast<Hud*>(screen)->objectives.show_for(v);
  }
  void hud_43d920(Handle) override {
    if (void* hud = program_mech_hud(mech_mission_player())) static_cast<Hud*>(hud)->radar.reset_boundary();
  }
  void interp_set_mech(Handle, Mech& m) override {
    if (zint::Interp* in = mech3::interp()) mech3::interp_set_mech(*in, &m, zn(m.node));
  }
  void interp_set_cycle_node(Handle, Handle node) override {  // the script object's +0xb8
    if (zint::Interp* in = mech3::interp()) in->current = zn(node);
  }
  // The cockpit screens' cycled textures, added by the scripts, loaded
  // (playtest #19: unloaded, they drew with no surface).
  void textures_load() override { zimg::load_group_0(); }
  // IControls vt+0x88: the player's controls keep the record (0x00428440),
  // whose vlist their tick runs (0x004d8990).
  void controls_set_record(MechControls* c, const ai::AivRecord* rec) override {
    if (auto* pc = dynamic_cast<PlayerControls*>(c); pc && rec) pc->set_record(*rec);
    else LaunchHooks::controls_set_record(c, rec);
  }
  uint8_t interp_source(Handle, const char* path) override {
    zint::Interp* in = mech3::interp();
    return in && zint::source(*in, path) ? 1 : 0;
  }
  // 0x004782f0 on the mech the launch just built (the pilot's +0x14).
  void pilot_place(Handle p, const float pos[3], uint32_t yaw) override {
    if (Mech* m = mech3::launched_mech()) place_pilot_mech(*this, p, *m, pos, yaw);
    else LaunchHooks::pilot_place(p, pos, yaw);  // logged: no mech to place
  }
  // Mech3.exe 1.2 @ 0x004782f0's vtable +0x34: the spawn's one step, the
  // port's frame (0x00419fc0).
  void mech_update(Mech& m, float dt) override {
    ++g_spawn_updates;
    if (m.transform) std::memcpy(g_spawn_pos, m.transform + 9, sizeof g_spawn_pos);
    m.update(dt);
  }
  // 0x00478f10(0) at a campaign's launch: the pilot the screen chose, settled
  // and saved (pilot.md section 7); a debug start's stand-in has no campaign
  // to settle.
  void pilot_478f10(Handle) override { program_record_mission(false); }
  void multiplayer(Handle) override {  // 0x0046da80 on the pilot
    NetLaunchGame g;
    Mech* m = launched_mech();
    if (m && g_launch_game && g_launch_game(&g)) {
      if (!g.loadout) g.loadout = m->config;  // a debug start's game has no slot loadout: the mech's own
      if (g.loadout) net_launch_player(*m, g.loadout->packed(), g.skin, g.unlimited_ammo, g.outside_view);
    }
  }
  // The Instant Action (and multiplayer) launch's pilot save: the file opened
  // (0x00478ce0), its header (0x004794f0) and record (vt+0x04) written, closed.
  void pilot_478ce0(Handle) override { program_pilot_file(0); }
  uint8_t pilot_4794f0(Handle) override { return program_pilot_file(1) ? 1 : 0; }
  void pilot_vt4(Handle) override { program_pilot_file(2); }
  void close_handle(uint32_t) override { program_pilot_file(3); }  // the pilot's +0x10
  void fn_40f0a0() override { message_panel_clear(); }  // 0x0040f0a0: the message panel's lines
  // 0x004f3430(camera, 1): the view's camera is zSound's listener. The exe
  // reads it at the view's +0x04; the program's PlayerView keeps it itself.
  void camera_update(Handle camera) override {
    auto* n = static_cast<zcls::Node*>(g_w.object_of(g_w.engine, low(camera)));
    if (!n)
      if (PlayerView* v = program_view()) n = zn(v->camera);
    if (n) zcls::camera_set_listener(n, 1);
  }
  // 0x0049e5e0 at a campaign's launch: the lancemates' mechs (player.cpp).
  void lancemates() override { program_launch_lancemates(); }
  // 0x0049dc80(1) and 0x0045c4a0 at Instant Action's launch: its line-up,
  // then "destroy the enemies" over the mission's objectives.
  void instant_action() override { program_action_lineup(true); }
  void objectives(Handle, Handle, Handle, Handle) override { program_objectives_start(); }
  // 0x004d6d20: the mech's sensor range, capped (ai::AiTarget).
  void target_4d6d20(Mech& m, uint32_t v) override {
    m.set_sensor_range(g_w.handle_of(g_w.engine, &m), zgame::from_bits(v));
  }
  uint8_t cheat_invincible() override { return static_cast<uint8_t>(g_w.peek32(g_w.engine, 0x608e40)); }
  int32_t option_invincible() override { return mech3::invincible(); }  // options.h (not includable with mech.h)
  // The missions' list at +0xc0 is not in the program's memory.
  void free(Handle) override {}
};

WiredBuild g_build;
WiredData g_data;
WiredLaunch g_launch;

}  // namespace

namespace {
SalvageMech salvage_view(Mech& m);
const SalvageCalls& salvage_calls();
void salvage_weapons(const void* ammo, SalvageMech& v);
}  // namespace

namespace {
// Mech3.exe 1.2 @ 0x005a85e0: the entity's node's world position, its world
// matrix refreshed first when stale (0x0051e9d0, 0x0051fde0, 0x004ed280, 0x0051ea00).
const Vec3* entity_position(Vehicle& m) {
  auto* n = zn(m.node);
  if (!n || !n->data || n->cls() != 5) return nullptr;
  auto* d = static_cast<zcls::Object3d*>(n->data);
  if (d->get<uint32_t>(0) & 0x20) {
    zmth::Mat34 buf;
    zmth::push(&buf);
    zmth::load_identity();
    zcls::node_to_world(n, 1);
    zmth::pop();
  }
  return reinterpret_cast<const Vec3*>(d->floats(0x84));
}

// The mech's frame (Mech::update and below) on the engine's ports.
void wire_mech_frame() {
  PoseCalls& p = pose_calls();
  p.node_set = [](Handle n, const float* m) { zcls::object3d_set_matrix(zn(n), m); };       // 0x004fc300
  p.node_matrix = [](Handle n) { return zcls::object3d_matrix(zn(n)); };                    // 0x004fc3f0
  SkisCalls& s = skis_calls();
  s.node_matrix = p.node_matrix;
  s.set_node_matrix = p.node_set;
  s.position = [](Mech& m) { return entity_position(m); };
  s.soil = [](Handle mat) {  // [0x00608c00 + 4 * material +0x20]
    return static_cast<int>(game_soil(static_cast<const zmod::Material*>(mat)->soil));
  };
  // The centre probe's zone record (0x00476550): the view's camera's list,
  // and zEntity's 0x005a80d0 (the record at +0x64, the id on the node).
  s.camera_zones = [] { return program_view_camera_zones(); };
  s.set_zone = [](Mech& m, const uint8_t record[4], uint8_t id) {
    std::memcpy(&m.x64, record, 4);
    zcls::node_set_zone(zn(m.node), id);
  };
  CollideCalls& c = collide_calls();
  c.refresh_box = [](Handle n) { zcls::node_children_box(zn(n)); };                         // 0x004ecf70
  // A wall's reverse (0x004231c0): the mech's vt+0x140 (its controls at
  // +0x37c as IControls, null without), then their vt+0x70.
  c.ai_reverses = [](Mech& m) -> int32_t { return m.controls && m.controls->on_path() ? 1 : 0; };
  c.node_box = [](Handle n) { return reinterpret_cast<const float*>(zn(n)->raw + 0xa4); };
  c.set_position = [](Mech& m, const Vec3* at) {  // vtable +0x18, zEntity3d's 0x005a8550
    zcls::object3d_set_translation(zn(m.node), at->x, at->y, at->z);
    m.entity_moved();
  };
  update_calls().moved = [](Mech& m) { m.entity_moved(); };                                 // 0x005a85c0
  update_calls().landmines = [](const Vec3* at) { landmines_check(at ? &at->x : nullptr); };  // 0x00413560
  landmine_hooks().ground = [](float x, float y, float z, zcls::GroundHits* out) -> int32_t {    // 0x004edab0
    zcls::Node* w = world_hooks().world;
    if (!w) return 1;
    return zcls::ground_point(w, x, y, z, out);
  };
  // What a probe hit (0x0040ef10 / 0x0040ef00): a mech's nodes are its own,
  // so it blocks and is shoved as a mech rather than as a wall.
  c.node_mech = [](Handle n) { return static_cast<Mech*>(node_owner(n)); };
  c.node_entity = [](Handle n) -> Handle {  // 0x0040e960
    const zcls::Node* z = zn(n);
    if (!z) return nullptr;
    const uint32_t id = z->get<uint32_t>(0x28);
    if (id & 0x40000) return z->owner;
    if (Handle o = node_owner(n)) return o;
    return id & 0x80000 ? z->owner : nullptr;  // 0x0040e9b0
  };
  // The bump looks through the sensors' contacts. A contact's vehicle (vt+0x2c)
  // and mech (vt+0x50) are the mech itself (0x004d2a70); targets.zrd's
  // objects answer vt+0x40 with 0 (0x0040bae0), and the vehicles that do
  // (the Mobile HQ) are not ported.
  c.contact_count = [](Mech& m) { return static_cast<int32_t>(m.contacts.size()); };
  c.contact = [](Mech& m, int32_t i) -> Handle { return m.contacts[i]; };
  c.contact_mech = [](Handle ct) -> Mech* {  // 0x004e6ff0
    const ai::Handle s = static_cast<ai::Contact*>(ct)->seen;
    return s ? entity_mech(s) : nullptr;
  };
  c.contact_target = [](Handle) { return false; };
  c.contact_dist2 = [](Handle ct) { return static_cast<ai::Contact*>(ct)->dist2; };
  c.contact_dist = [](Handle ct) { return static_cast<ai::Contact*>(ct)->distance(); };
  c.contact_dir = [](Handle ct) { return reinterpret_cast<const Vec3*>(static_cast<ai::Contact*>(ct)->dir); };
  // The AI mechs' controls run their brains' frames (AiControls::update).
  // The player's keys (Mech::command): what is not ported is logged once by
  // the engine; the HUD's keys wait for the HUD's own commands.
  CommandCalls& k = command_calls();
  k.unported = [](const char* key, int32_t cmd) { return g_w.unported(g_w.engine, key, {cmd}); };
  k.handle = [](Mech& m) { return g_w.handle_of(g_w.engine, &m); };
  // Z and Q's aimed object (0x0040eee0) by its handle: a mech's team and no
  // refusal (0x004d7290), a targets.zrd object's by its zoom_select /
  // reticule_select (0x004d3360, 0x004d3370); anything else (the vehicles,
  // not ported) refuses nothing and has no team.
  k.node_entity = [](void* n) { return n ? g_w.handle_of(g_w.engine, node_entity(n)) : 0u; };
  k.entity_team = [](uint32_t e) -> uint32_t {
    if (Mech* m = entity_mech(e)) return m->team;
    for (const ai::CZAiTarget* t : ai::target_list())
      if (t->self == e) return t->team;
    return 0;
  };
  k.entity_refuses = [](uint32_t e, bool zoom) {
    for (const ai::CZAiTarget* t : ai::target_list())
      if (t->self == e) return !(t->desc.flags & (zoom ? 8 : 4));
    return false;
  };
  // The look keys (0x62..0x67) and FREELOOK: the view's mode vt+0x10, the
  // free look (playtest #14: wired to the camera switch before).
  k.view_camera = program_view_look;
  // 0x005a8630 on a contact's vehicle (the part aim's fallback): a mech's
  // target node (its torso), not its place (W39); the other entities' are
  // not ported (none).
  k.seen_position = [](uint32_t s) -> const float* {
    static Vec3 at;
    Mech* m = entity_mech(s);
    if (!m) return nullptr;
    if (const float* p = weapon_lock_point(m)) return p;
    at = mech_position(*m);
    return &at.x;
  };
  k.set_view_option = program_set_mouse_mode;  // [[0x005f4d44]]
}

// The mech's world position (0x005a85e0).
Vec3 mech_position(Mech& m) {
  if (skis_calls().position) return *skis_calls().position(m);
  return m.transform ? Vec3{m.transform[9], m.transform[10], m.transform[11]} : Vec3{0, 0, 0};
}

// A node's handler word (+0xbc) above 1 with a touch (slot 1, +0x0c).
bool handler_touchable(Handle n) {
  const zwep::RunHooks& rh = zwep::run_hooks();
  const uintptr_t h = n && rh.handler ? rh.handler(n) : 0;
  return h > 1 && reinterpret_cast<const zwep::NodeHandlers*>(h)->slot[1].fn != nullptr;
}

// What the parts log as they play (ours, for the headless tests): "step
// <sound>", "print <effect>", "wake <level>", "pilot <sound>".
std::vector<std::string> g_parts_fx;
void note_part(const char* what, const std::string& name) {
  if (g_parts_fx.size() < 4096) g_parts_fx.push_back(std::string(what) + " " + name);  // as weapon_fx_log: bounded in play
}
std::string sound_name(const void* s) {
  const auto* o = static_cast<const zsnd::Object*>(s);
  if (!o) return "-";
  return o->kind == 0 ? static_cast<const zsnd::Sound*>(o)->name : static_cast<const zsnd::Group*>(o)->name;
}

// pilot_sound with the parts log's "pilot <sound>" line.
Sound pilot_play(Mech& m, int id, float volume, const Vec3* at) {
  if (!m.pilot) return nullptr;
  note_part("pilot", sound_name(m.pilot->sounds[id]));
  return pilot_sound(m.pilot->sounds, id, volume, at ? &at->x : nullptr, nullptr);
}

// The parts' outside calls in the game (PartCalls, mech_parts.h; the parts'
// share of DamageCalls; the collider's touch): the engine's ports.
void wire_mech_parts() {
  PartCalls& pc = part_calls();
  pc = PartCalls{};
  pc.register_part = [](Mech& m, MechPart& p) { m.parts.push_back(&p); };  // 0x0041b8d0
  pc.tree_update = [](Handle n) { zcls::update_subtree(zn(n)); };           // 0x004f7db0, the legs' setup
  pc.foot_effect = []() -> Handle {                                         // 0x0045f980
    return foot_wake_new(world_hooks().world, "mech_leg_wake01", "mech_leg_wake02", "mech_leg_wake03");
  };
  pc.effect_free = [](Handle e) { foot_wake_free(static_cast<FootWake*>(e), world_hooks().world); };  // 0x0045fab0
  pc.effect_playing = [](Handle e) { return foot_wake_playing(static_cast<FootWake*>(e)); };   // 0x0045fb80
  pc.effect_start = [](Handle e, float t) {                                                    // 0x0045faf0
    auto* w = static_cast<FootWake*>(e);
    const int32_t before = w->level;
    foot_wake_start(w, t);
    if (w->level != before) note_part("wake", std::to_string(w->level));
  };
  pc.effect_place = [](Handle e, float x, float y, float z) {                                  // 0x004fc0b0
    zcls::object3d_set_translation(static_cast<FootWake*>(e)->node, x, y, z);
  };
  pc.step_on = [](const Vec3& at, GroundHit& hit, float force) {  // 0x005836b0 with the contact as the record
    if (!handler_touchable(hit.node)) return;
    zwep::HitRecord r;
    r.point = {hit.x, hit.height, hit.z};
    r.material = hit.material, r.node = hit.node;
    const zwep::Vec3f3 from{at.x, at.y, at.z};
    zwep::touch(&from, &r, force);
  };
  pc.play_at = [](Handle s, const Vec3* at, float volume) {                                    // 0x0056e7e0(.., 0)
    note_part("step", sound_name(s));
    if (s) zsnd::play_at(static_cast<zsnd::Object*>(s), &at->x, volume, nullptr);
  };
  pc.look_limits = [](Mech& m, float* max_yaw, float* max_pitch, float* min_pitch) {
    // vt+0x10c(1): the cockpit camera; its vt+0x1c (0x004d2a70) is itself.
    auto* list = m.pilot ? static_cast<CameraList*>(m.pilot->cameras) : nullptr;
    auto* cam = list ? static_cast<MechCamera*>(list->find(1)) : nullptr;
    if (!cam) return;
    *max_yaw = cam->look_left, *max_pitch = cam->look_up, *min_pitch = cam->look_down;  // +0x68, +0x74, +0x70
  };
  pc.aim_point = [](Mech& m, Vec3* at) {  // +0xd4 (the weapons): +0x18 an aim, +0x0c the point
    const auto* wm = static_cast<const WeaponManager*>(m.ammo);
    if (!wm || !wm->aim_valid) return false;
    *at = Vec3{wm->aim.x, wm->aim.y, wm->aim.z};
    return true;
  };
  pc.ground_at = [](float x, float y, float z, void* hits) {  // 0x004edab0 on [0x00608c84] +4
    if (zcls::Node* w = world_hooks().world) zcls::ground_point(w, x, y, z, static_cast<zcls::GroundHits*>(hits));
  };
  DamageCalls& d = damage_calls();
  // The damage decals (docs/damage-decals.md): 0x00414370 the definition's
  // skins (+0x1c of 0x004846a0), then 0x00474d90 on the mech's skin (+0x3e8)
  // and the hit's material (the collision's +0x20).
  d.roster_x1c = [](Mech& m) -> Handle {
    const MechDfn* f = m.pilot ? mech_dfn(m.pilot->roster) : nullptr;
    return f ? f->skins : nullptr;
  };
  d.damage_decal = [](Handle set, Handle skin, Collision* coll, float) {
    static_cast<SkinSet*>(set)->damage_decal(static_cast<const Skin*>(skin), coll ? coll->x20 : nullptr);
  };
  d.spawn_effect = [](Handle effect, Handle node, const float args[9]) -> Handle {  // 0x00508aa0
    if (!effect) return nullptr;
    auto* a = static_cast<zeff::AnimDef*>(effect);
    note_part("print", a->name());
    return zeff::anim_start_at(a, zn(node), args, args + 3, args + 6);
  };
  d.spawn_effect_at = [](Handle effect, Handle node, float x, float y, float z) -> Handle {  // 0x00508c10
    return effect ? zeff::anim_start(static_cast<zeff::AnimDef*>(effect), zn(node), x, y, z) : nullptr;
  };
  d.node_set_active = [](Handle n, int on) { zcls::node_set_active(zn(n), on); };          // 0x004eb170
  d.node_set_name = [](Handle n, const char* name) { zcls::node_set_name(zn(n), name); };  // 0x004eb300
  d.stop_foot_effect = [](Handle e) { foot_wake_stop(static_cast<FootWake*>(e)); };       // 0x0045fb50
  // The deaths and damage reactions (c1m1 item 2, docs/damage.md).
  d.death_prep = [](Mech& m) { m.death_prep(); };                 // 0x0041d070
  d.death_anim = [](Mech& m, int anim) { m.death_fall(anim); };   // 0x0041b760
  d.stop_action = [](Handle a) { if (a) static_cast<MechAction*>(a)->stop(); };  // 0x00421060
  d.blend_anim = [](Handle base, Handle limp) -> Handle {         // 0x0041ff30
    return make_blend(static_cast<MechAnim*>(base), static_cast<MechAnim*>(limp));
  };
  d.on_effect_end = [](Handle e, MechPart& p) {                   // 0x00508f00 with 0x00461c90
    zeff::anim_set_callback(
        static_cast<zeff::AnimDef*>(e),
        [](zeff::AnimDef* def, void* part, int32_t v) { part_effect_ended(def, *static_cast<MechPart*>(part), v); },
        &p);
  };
  d.weapons_stop = [](Mech& m) {                                   // 0x004b1110 on +0xd4
    if (auto* wm = static_cast<WeaponManager*>(m.ammo)) wm->destroy_all();
  };
  d.weapons_lost = [](Mech& m, int part) {                         // 0x004b1040 on +0xd4
    if (auto* wm = static_cast<WeaponManager*>(m.ammo)) wm->part_lost(part);
  };
  // FIX (A): an arm's loss frees its hand (0x00466c50), which the arm
  // restriction (0x004b2da0 -> 0x004b2f90's in-water test) reads again that
  // frame through its weapons' part; Win32's heap kept the bytes, so the
  // memory is kept here until the next mission starts (fixes.md).
  d.free_part = [](MechPart* p) { freed_parts().emplace_back(p); };
  d.weapons_critical = [](Mech& m, int part, int ammo) {           // 0x004b0de0 on +0xd4
    auto* wm = static_cast<WeaponManager*>(m.ammo);
    return wm && wm->critical(part, ammo != 0);
  };
  // A part flying off (0x00461d80, mech_parts.cpp) on zClass.
  FlyCalls& f = fly_calls();
  f.copy = [](Handle stub) -> Handle { return zcls::tree_copy(zn(stub), 0, 0); };  // 0x004f9af0
  f.create = []() -> Handle { return zcls::object3d_create(); };                   // 0x004fb740
  f.report = [](const char* mech, int32_t id) {
    char msg[96];
    std::snprintf(msg, sizeof msg, "Failed to copy stub for %s partId %d", mech ? mech : "", id);
    zerr::report(0x400, "C:\\zipper\\Mech3\\mech_parts.cpp", 0x4a4, msg);
  };
  f.rename = [](Handle n, Handle from) { zcls::node_set_name(zn(n), zn(from)->name()); };  // 0x004eb300
  f.parent = [](Handle n) -> Handle { return zn(n)->parent_count() > 0 ? zn(n)->parents[0] : nullptr; };
  f.set_flag8 = [](Handle n, int32_t on) { zcls::node_set_flag8(zn(n), on); };          // 0x004eb620
  f.remove_child = [](Handle p, Handle c) { zcls::node_remove_child(zn(p), zn(c)); };  // 0x004ebb40
  f.attach = [](Handle n) {                                                              // [0x00608c84] vt+0
    if (zcls::Node* w = world_hooks().world) zcls::node_add_child(w, zn(n));
  };
  f.set_flags2c = [](Handle n, int32_t bit, int32_t on) {  // 0x004eb8a0, 0x004eb8e0
    const uint32_t v = zn(n)->get<uint32_t>(0x2c);
    zn(n)->put<uint32_t>(0x2c, (v & ~(1u << bit)) | (static_cast<uint32_t>(on & 1) << bit));
  };
  f.set_pose = [](Handle n, const float m[12]) {  // 0x00461d30
    zmth::Mat34 mat;
    std::memcpy(&mat, m, sizeof mat);
    float a[3];
    zmth::matrix_to_euler(mat, a);  // 0x00523bc0
    zcls::object3d_set_translation(zn(n), m[9], m[10], m[11]);
    zcls::object3d_set_rotation(zn(n), a[0], a[1], a[2]);
  };
  f.add_child = [](Handle p, Handle c) { zcls::node_add_child(zn(p), zn(c)); };  // 0x004eb9b0
  f.clear_ids = [](Handle n) {  // 0x00462000
    struct Clear {
      static void run(zcls::Node* x) {
        x->put<uint32_t>(0x28, 0);
        x->owner = nullptr;
        x->damage = nullptr;
        for (int32_t i = 0; i < x->child_count(); ++i) run(x->children[i]);
      }
    };
    Clear::run(zn(n));
  };
  f.set_node = [](MechPart& p, Handle n) { part_set_node(p, n); };          // vt+0x0c, 0x00460190
  f.mark = [](MechPart& p, int32_t id) { part_mark_nodes(p, id, nullptr); };  // 0x00460050(id, 0)
  f.opacity_used = [](Handle n) {  // 0x004fbb10
    uint32_t on = 0;
    return zcls::object3d_opacity_used(zn(n), &on) == 0 && on != 0;
  };
  f.use_opacity = [](Handle n, int32_t on) { zcls::object3d_use_opacity(zn(n), on); };  // 0x004fbb90
  f.opacity = [](Handle n) {                                                              // 0x004fbaa0
    float v = 0.0f;
    zcls::object3d_get_opacity(zn(n), &v);
    return v;
  };
  f.set_opacity = [](Handle n, float v) { zcls::object3d_set_opacity(zn(n), v); };  // 0x004fba20

  f.instant_action = [] { return mission_game_type() == 2; };  // 0x00479460
  // Tearing parts and mechs down (mech_parts.cpp release, Mech::release).
  FreeCalls& fr = free_calls();
  fr.stop_effect = [](Handle e) { if (e) zeff::anim_stop(static_cast<zeff::AnimDef*>(e)); };  // 0x00506ca0
  fr.rename = [](Handle n, const char* name) { zcls::node_set_name(zn(n), name); };            // 0x004eb300
  fr.set_flags2c = fly_calls().set_flags2c;
  fr.clear_ids = [](Handle n) {  // 0x004600b0: +0x28 and +0x40, not the handlers
    struct Clear {
      static void run(zcls::Node* x) {
        x->put<uint32_t>(0x28, 0);
        x->owner = nullptr;
        for (int32_t i = 0; i < x->child_count(); ++i) run(x->children[i]);
      }
    };
    Clear::run(zn(n));
  };
  fr.debris_free = [](Handle n) {  // 0x00413db0
    if (zn(n)->parent_count() != 0) zcls::node_remove_child(zn(n)->parents[0], zn(n));
    zcls::tree_destroy(zn(n));
  };
  fr.decals_free = [](Handle d) { delete static_cast<Skin*>(d); };  // 0x00473f40
  fr.viewed = [](Mech& m) { return program_view_target() == static_cast<void*>(&m); };
  fr.view_detach = [] { if (PlayerView* v = program_view()) v->attach(nullptr, nullptr); };  // 0x0040adf0(0, 0)
  fr.torso_cockpit_off = [](Mech& m) { if (m.torso) m.torso->set_cockpit(false); };  // 0x0041ce10(0)
  fr.is_child = [](Handle p, Handle c) {  // 0x004600e0(c, 0, 0): a direct child
    for (int32_t i = 0; p && i < zn(p)->child_count(); ++i)
      if (zn(p)->children[i] == zn(c)) return true;
    return false;
  };
  fr.first_parent = [](Handle n) -> Handle { return zn(n)->parent_count() > 0 ? zn(n)->parents[0] : nullptr; };
  fr.remove_child = [](Handle p, Handle c) { zcls::node_remove_child(zn(p), zn(c)); };
  fr.add_child = [](Handle p, Handle c) { zcls::node_add_child(zn(p), zn(c)); };
  fr.mark = [](MechPart& p, int32_t id, Handle n) { part_mark_nodes(p, id, n); };
  fr.cockpit_node = [](MechData& p) -> Handle { return p.cockpit_model ? p.cockpit_model->node : nullptr; };  // +0x228 +4
  fr.handlers_off = [](Mech& m) {  // the other model under the node while its handlers go
    zcls::Node* other = zn(m.x690 == 0 ? m.lod_node : m.model_node);
    zcls::node_add_child(zn(m.node), other);
    zwep::remove_handlers(m.node);  // 0x00583400
    zcls::node_remove_child(zn(m.node), other);
  };
  fr.controls_free = mission_controls_free;
  fr.node_free = [](Mech& m) {
    zcls::Node* n = zn(m.node);
    if (n->parent_count() == 1) zcls::node_remove_child(n->parents[0], n);
    for (int32_t i = 0; i < n->child_count(); ++i) {  // as the exe: a removal moves the next one into i
      zcls::Node* c = n->children[i];
      if (c == zn(m.model_node) || c == zn(m.lod_node)) zcls::node_remove_child(n, c);
    }
    zcls::node_set_name(n, "deletedmech");  // 0x005a84c0
    zcls::tree_destroy(n);
    zcls::tree_destroy(zn(m.model_node));
    zcls::tree_destroy(zn(m.lod_node));
    m.model_node = m.lod_node = nullptr;
  };
  fr.entity_free = [](Mech& m) {  // 0x005a7f70: off zEntity's list
    mech_objects().erase(&m);
    auto& list = ai::entity_list();
    for (auto it = list.begin(); it != list.end();)
      it = g_w.object_of(g_w.engine, *it) == &m ? list.erase(it) : std::next(it);
  };

  // The level of detail's model switch (mech_lod.cpp) on zClass.
  LodCalls& lc = lod_calls();
  lc.player_target = [](Mech& player) -> Mech* {  // 0x004e6ff0 on +0x6c: the contact's vehicle, a mech
    const ai::Handle seen = player.target ? player.target->seen : 0;
    return seen ? entity_mech(seen) : nullptr;
  };
  lc.remove_child = [](Handle p, Handle c) { zcls::node_remove_child(zn(p), zn(c)); };
  lc.add_child = [](Handle p, Handle c) { zcls::node_add_child(zn(p), zn(c)); };
  lc.find = [](Handle n, const char* name) -> Handle { return zcls::node_find_in_tree(zn(n), name); };  // 0x004f9d60
  lc.node_set_active = [](Handle n, int32_t on) { zcls::node_set_active(zn(n), on); };
  lc.node_set_matrix = [](Handle n, const float* m) { zcls::object3d_set_matrix(zn(n), m); };
  lc.set_node = [](MechPart& p, Handle n) { part_set_node(p, n); };
  lc.stop_effect = [](Handle e) { if (e) zeff::anim_stop(static_cast<zeff::AnimDef*>(e)); };
  lc.jets_effect = [](Mech& m) { return canned_effect(*m.pilot, 1, nullptr, nullptr, nullptr); };

  // The dead mechs (mech_dead.cpp).
  update_calls().add_dead = [](Mech& m) { dead_add(m); };  // 0x0041e350 on 0x00605510
  DeadCalls& dc = dead_calls();
  dc.networked = [] { return mission_game_type() == 3; };       // 0x00479480
  dc.instant_action = [] { return mission_game_type() == 2; };  // 0x00479460
  dc.net_fade = [] {  // [0x0060861c]+0x8c, ALLOW RESPAWN's: a network wreck fades out
    const NetGameFields* g = net_calls().game ? net_calls().game() : nullptr;
    return g && g->flags && g->flags[0] != 0;
  };
  dc.node_set_active = [](Handle n, int32_t on) { zcls::node_set_active(zn(n), on); };
  dc.use_opacity = [](Handle n, int32_t on) { zcls::object3d_use_opacity(zn(n), on); };
  dc.set_opacity = [](Handle n, float v) { zcls::object3d_set_opacity(zn(n), v); };
  dc.ia_tally = [](int32_t field, int32_t kills) { mission_action_add(field, kills); };  // 0x00479460's
  dc.sensors_forget = [](Mech& m) { ai::entities_forget(g_w.handle_of(g_w.engine, &m)); };  // 0x004aa6b0
  dc.free = [](Mech& m) { mech_free(&m); };  // vt+0x00(1), and off the program's registry

  // The kill credit: a mech killer's vtable +0x80 is on_kill (0x0041b420),
  // a basic vehicle's and a turret's 0x004a5bc0 do nothing.
  d.networked = [] { return mission_game_type() == 3; };  // 0x00479480, as the dead list's
  d.killer_notify = [](Handle killer, Mech& victim) {
    if (entity_is_mech(killer)) return static_cast<Mech*>(killer)->on_kill(&victim);
    if (is_program_vehicle(killer) || program_turret(killer)) return;  // 0x004a5bc0: nothing
    if (g_w.unported) g_w.unported(g_w.engine, "killer vt+0x80", {});
  };
  // What on_kill asks of its victim: a basic vehicle answers itself to
  // vt+0x2c and vt+0x40, 0 to vt+0x50 and vt+0x48 (0x004d2a70, 0x0040bae0).
  d.victim_view = [](Handle v, int slot) -> Handle {
    if (is_program_vehicle(v)) return slot == 0x50 || slot == 0x48 ? nullptr : v;
    if (program_turret(v)) return slot == 0x50 || slot == 0x40 ? nullptr : v;  // a "building" (vt+0x48 itself)
    return slot == 0x48 ? nullptr : v;
  };
  d.salvage_vehicle = [](Handle v) {  // 0x00472ec0 on the mission's store
    SalvageMech s;
    salvage_weapons(mech_weapon_manager(v), s);
    add_vehicle_salvage(mission_salvage(), s, salvage_calls());
  };
  d.salvage_mech = [](Handle m) {  // 0x00472f00 on the mission's store
    SalvageMech v = salvage_view(*static_cast<Mech*>(m));
    add_salvage(mission_salvage(), v, salvage_calls());
  };
  d.mission_player_died = mission_lose;                            // 0x0048f9e0 on 0x006098d0
  d.view_mode = [] { return static_cast<int>(program_view_mode_id()); };  // [0x005f4c8c] +0x58 +4
  d.view_camera = [](int mode) { program_view_set_camera(mode); };  // 0x0040ae70
  d.position = [](Mech& m) -> const Vec3* {                         // 0x005a85e0
    static Vec3 at;
    at = mech_position(m);
    return &at;
  };
  d.node_update = [](MechPart& p) {                                 // 0x004ed530 on the part's node
    float at[3];
    if (p.node) zcls::node_world_position(zn(p.node), at);
  };
  // Its world 3x4, object3d +0x60 (0x00461cf0 copies node +0x38 +0x60); a
  // part's node is always an object3d, as the exe assumes.
  d.node_matrix = [](MechPart& p) -> const float* {
    return static_cast<zcls::Object3d*>(zn(p.node)->data)->floats(0x60);
  };
  // The pilot's sounds (0x00432990 on +0x684, through the sounds' EXTRA
  // records, sound_extra.cpp): unplaced, or at the mech where the exe passes
  // its position.
  pc.sound = [](Mech& m, int id, float volume) -> void* { return pilot_play(m, id, volume, nullptr); };
  MoveCalls& mv = move_calls();
  mv.pilot_sound = [](Mech& m, int id, float volume, float) -> Sound {  // the fifth argument is not read
    const Vec3 at = mech_position(m);
    return pilot_play(m, id, volume, &at);
  };
  mv.fade_sound = [](Sound s) -> Sound {  // 0x00571d20(s, 1, 0)
    return s ? zsnd::fade(static_cast<zsnd::VoiceBase*>(s), 1, 0.0f) : nullptr;
  };
  update_calls().purge = [](Mech& m, float dt) { m.fade_debris(dt); };  // 0x00413cb0
  update_calls().set_volume = [](Sound s, float v) {  // 0x0056f370
    if (s) zsnd::set_voice_volume(static_cast<zsnd::VoiceBase*>(s), v);
  };
  update_calls().set_position = [](Sound s, const Vec3* at) {  // 0x00571f30(at, 0, 0): a group's voice is skipped there
    auto* v = static_cast<zsnd::VoiceBase*>(s);
    if (v && at && v->kind == 0) zsnd::update_3d(static_cast<zsnd::Voice*>(v), &at->x, nullptr, 0);
  };
  mv.stop_effect = [](Handle e) { if (e) zeff::anim_stop(static_cast<zeff::AnimDef*>(e)); };  // 0x00506ca0
  mv.pilot_effect = [](Mech& m, int slot, Handle node) {  // 0x00432830(slot, node, zero, zero): the jets' flames
    return canned_effect(*m.pilot, slot, node, nullptr, nullptr);
  };
  mv.weapons_enable = [](Mech& m, int on) {                                                    // 0x004b0bf0 on +0xd4
    if (auto* wm = static_cast<WeaponManager*>(m.ammo)) wm->set_online(on != 0);
  };
  HeatCalls& hc = heat_calls();
  hc.game_type = mission_game_type;  // [0x0060861c] +0x6c, vt+0x2c
  hc.throttle = [](Mech& m) { return m.controls ? m.controls->throttle : 0.0f; };  // +0x37c GetThrottle
  hc.play_sound = [](Mech& m, int id) -> Sound { return pilot_play(m, id, 1.0f, nullptr); };
  hc.play_sound_at = [](Mech& m, int id) -> Sound {
    const Vec3 at = mech_position(m);
    return pilot_play(m, id, 1.0f, &at);
  };
  hc.stop_sound = [](Sound s) { if (s) zsnd::stop(static_cast<zsnd::VoiceBase*>(s)); };  // 0x0056e9f0
  // The cockpit sounds (playtest #17): the fire modes, the gyro, MASC, the
  // jettison, the coolant, the hits (0x0041e770 at 1.0; 0x0041e7a0(i, 0)).
  hc.play_mech_sound = [](int i) { cockpit_sound_play(i, 1.0f); };
  update_calls().stop_cockpit_sound = [](int i) { cockpit_sound_stop(i, 0); };
  CollideCalls& c = collide_calls();
  c.node_touchable = [](Handle n) { return handler_touchable(n); };  // node +0xbc above 1 with slot 1 (0x004230a0)
  // The ceiling's crush (0x00423600): the box of what the mech stands under
  // (0x004ec9c0). Unwired before, the crush never happened in play.
  c.hit_box = [](Handle n) { return zcls::node_box(static_cast<zcls::Node*>(n)); };
  c.touch = [](Handle n, const Vec3* from, const CollisionHit& h, float damage) {  // 0x005836b0
    zwep::HitRecord r;
    std::memcpy(&r.normal, h.hit.normal, 12);
    std::memcpy(&r.point, h.hit.point, 12);
    r.material = h.hit.material, r.node = n;
    const zwep::Vec3f3 f{from->x, from->y, from->z};
    return zwep::touch(&f, &r, damage);
  };
}
}  // namespace

// 0x005a9820 from the mechs' set-up 0x00413e60, at each mission's start.
void program_motions_open() { open_motions("zbd/motion.zbd"); }

const char* entity_name(zgame::Handle o);  // entity_classes.h (its entity_mech clashes with this file's)

// The mission's network side (mech_net.cpp, multiplayer-port.md piece 5):
// what it calls outside the network code.
void wire_net(bool on) {
  NetCalls& nc = net_calls();
  const NetCalls kept = nc;  // the force feedback's are app_force.cpp's
  nc = NetCalls{};
  nc.ff_stop = kept.ff_stop, nc.ff_spawn = kept.ff_spawn, nc.ff_respawn = kept.ff_respawn;
  nc.game_clear = kept.game_clear;  // the pilot's side's
  nc.game = kept.game, nc.game_time = kept.game_time, nc.clock_left = kept.clock_left, nc.clock_on = kept.clock_on;
  nc.set_clock_left = kept.set_clock_left, nc.exit_fade = kept.exit_fade;
  if (!on) return;
  nc.set_position = [](NetMech& m, const float pos[3]) {  // vtable +0x18
    const Vec3 v{pos[0], pos[1], pos[2]};
    if (collide_calls().set_position) collide_calls().set_position(m, &v);
  };
  nc.hud_message = [](float seconds, int32_t id, const char* name) {  // 0x0040fe70
    if (!message_panel()) return;
    message_panel()->show_for(seconds);
    const std::string t = zutl::format_message(id, name);
    if (!t.empty()) message_panel_add("%s", t.c_str());
  };
  nc.hud_line = [](float seconds, const char* text) {  // 0x0040fe20
    if (!message_panel()) return;
    message_panel()->show_for(seconds);
    message_panel_add("%s", text);
  };
  nc.chat = net_chat_in;  // 0x00470490 (piece 14)
  nc.hud_scores = program_hud_scores_refill;  // 0x00446ad0 on the box
  nc.forget = [](NetMech& m) { ai::entities_forget(g_w.handle_of(g_w.engine, &m)); };  // 0x004aa6b0
  nc.free = [](NetMech* m) {  // vt+0 (1), and off the program's own lists (as mech_free)
    net_mech_unlist(m);
    mission_ai_mech_forget(m, g_w.handle_of ? g_w.handle_of(g_w.engine, m) : 0);
    m->release();
    mech_objects().erase(m);
    if (g_w.forget_object) g_w.forget_object(g_w.engine, m);
    delete m;
  };
  nc.view_outside = [] { program_view_set_camera(1); };  // 0x0040ae70(1)
  // The pilot's respawn's (0x0046dde0, piece 8d; net_start sets the dead list's call of it).
  nc.pilot_set_mech = [](Mech&, int32_t roster) { program_pilot_set_mech(roster); };  // 0x004783c0
  nc.view_camera = [](int32_t id) { program_view_set_camera(id); };                 // 0x0040ae70
  nc.options_apply = [] { program_options_apply(); };                                // 0x0040cb30
  nc.objectives_again = [] { program_objectives_again(); };                          // 0x00491050
  nc.pilot_save = [] {  // 0x00478ce0, the header, vt+0x04, the handle closed (0x004707c0's)
    program_pilot_file(0);
    if (program_pilot_file(1)) program_pilot_file(2);
    program_pilot_file(3);
  };
  // The sends (0x0046f160): the mech's and its torso node's world positions,
  // the target's player (its contact's vehicle, when a network mech).
  nc.position = [](Mech& m, float out[3]) { return mech_world_position(&m, out); };
  nc.torso_position = [](Mech& m, float out[3]) {
    if (!m.torso || !m.torso->node) return false;
    return zcls::node_world_position(zn(m.torso->node), out) != 0;
  };
  nc.target_player = [](Mech& m) -> uint32_t {
    void* v = m.target ? hud_object_mech(hud_contact_vehicle(m.target)) : nullptr;  // 0x004e7010: vt+0x2c, vt+0x58
    const NetMech* t = v ? net_mech_of(v) : nullptr;
    return t ? t->owner : 0;
  };
  nc.sensor_range = [](Mech& m) { m.set_sensor_range(g_w.handle_of(g_w.engine, &m), m.sensor_range); };
  nc.world_add = [](NetMech& m) {  // [0x00608c84] vt+0x00
    if (zcls::Node* w = world_hooks().world) zcls::node_add_child(w, zn(m.node));
  };
  nc.spawn_point = [](const char* name, float pos[3], float* yaw) {  // aiv.zrd's (0x004e0680)
    ai::AivRecord rec;
    const ai::AivRecord* r = g_launch.aiv_read_named(rec, name);
    if (!r) return false;
    pos[0] = r->get<float>(0x08), pos[1] = r->get<float>(0x0c), pos[2] = r->get<float>(0x10);
    *yaw = r->get<float>(0x14);
    return true;
  };
  // The mech's network calls (vt+0x168, vt+0x158): the sends, and for a
  // network mech its dead reckoning (piece 6b).
  damage_calls().net_event = [](Mech& m, int kind, intptr_t arg) { net_send_event(m, kind, arg); };
  weapon_calls().net_event = [](Handle owner, int32_t kind, Weapon* w) {
    if (entity_is_mech(owner)) net_send_event(*static_cast<Mech*>(owner), kind, reinterpret_cast<intptr_t>(w));
  };
  const auto net_update = [](Mech& m, float dt) {
    if (!m.remote) return net_mech_tick(m);
    auto& n = *static_cast<NetMech*>(&m);
    net_calls().reckon ? net_calls().reckon(n, dt) : net_reckon(n, dt);
  };
  // The apply's (0x0046e4d0): the target (0x004a7b40, as the pocket
  // commander's pick), the radar's light, the pilot's mech; the shots'
  // target players (0x004b08c0's lookup).
  nc.pick_target = [](NetMech& m, Handle v) {
    const zgame::Handle e = v ? g_w.handle_of(g_w.engine, v) : 0;
    if (m.target && m.target->seen == e) return;
    ai::Contact* found = nullptr;
    for (ai::Contact* c : m.contacts)
      if (e && c->seen == e) {
        found = c;
        break;
      }
    m.set_target(found, false);
  };
  nc.radar_toggled = [](NetMech& m) { program_radar_light(&m); };
  // The events (piece 7a): the HUD's two-name line, a mech's name, the
  // collisions' push on a network mech (0x00470320).
  nc.hud_message2 = [](float seconds, int32_t id, const char* a, const char* b) {  // 0x0040fe70
    if (!message_panel()) return;
    message_panel()->show_for(seconds);
    const std::string t = zutl::format_message(id, {a ? a : "", b ? b : ""});
    if (!t.empty()) message_panel_add("%s", t.c_str());
  };
  nc.mech_name = [](Mech& m) { return entity_name(g_w.handle_of(g_w.engine, &m)); };  // vt+0xa4
  nc.node_mech = [](Handle node) -> Mech* {  // 0x0040ef10: the node's owner, when a mech
    const Handle o = damage_calls().node_owner ? damage_calls().node_owner(node) : nullptr;
    return o && entity_is_mech(o) ? static_cast<Mech*>(o) : nullptr;
  };
  collide_calls().net_shove = [](Mech&, Mech& other, float damage, float vx, float vz) {
    net_send_shove(other, damage, vx, vz);
  };
  nc.pilot_mech = [] { return world().mission_player; };
  weapon_calls().net_target = [](Handle id) -> Handle {
    const auto player = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(id));
    if (player == znet::local_id()) return world().mission_player;
    return net_mech(player);
  };
  update_calls().net_update = net_update;
  dead_calls().net_update = net_update;
  nc.player_mech_end = [] {  // 0x0041e580 on the pilot's mech
    if (Mech* m = world().mission_player; m && free_calls().player_object_free) free_calls().player_object_free(*m);
  };
}

void net_set_game_clear(void (*fn)()) { net_calls().game_clear = fn; }
void net_set_launch_game(bool (*fn)(NetLaunchGame* out)) { g_launch_game = fn; }
namespace {
NetGameAccess g_game_access;
}  // namespace
void net_set_game_access(const NetGameAccess& a) {
  g_game_access = a;
  NetCalls& nc = net_calls();
  nc.game = []() -> NetGameFields* {
    static NetGameFields f;
    if (!g_game_access.fields ||
        !g_game_access.fields(&f.players, &f.clock_next, &f.end_time, &f.end_state, &f.kills, &f.deaths, &f.score_limit,
                              &f.flags))
      return nullptr;
    return &f;
  };
  nc.game_time = a.game_time, nc.clock_left = a.clock_left, nc.clock_on = a.clock_on;
  nc.set_clock_left = a.set_clock_left;
  nc.exit_fade = a.exit_fade;
}

namespace {
struct DebugScore {
  int32_t kills = 0, deaths = 0;
};
std::map<uint32_t, DebugScore> g_debug_scores;
}  // namespace

void program_net_debug_lobby() {
  NetCalls& c = net_calls();
  g_debug_scores.clear();
  c.lobby_team_id = [](uint32_t) { return 0u; };  // each player its own side, no teams
  c.team_of = [](uint32_t p) { return p; };
  c.start_of = [](uint32_t p) -> int32_t {
    int32_t rank = 1;
    for (const auto& r : znet::lists().players) rank += r->id < p;
    return rank;
  };
  c.lobby_suicide = [](uint32_t p) { ++g_debug_scores[p].deaths; };
  c.lobby_kill = [](uint32_t p) { ++g_debug_scores[p].kills; };
  c.lobby_kills = [](uint32_t p) { return g_debug_scores[p].kills; };
  c.lobby_score = c.lobby_kills;
  c.lobby_deaths = [](uint32_t p) { return g_debug_scores[p].deaths; };
  c.lobby_scores = [](uint32_t* out, int32_t cap) {
    int32_t n = 0;
    for (const auto& r : znet::lists().players) {
      if (n >= cap) break;
      const DebugScore& d = g_debug_scores[r->id];
      out[3 * n] = r->id, out[3 * n + 1] = static_cast<uint32_t>(d.kills), out[3 * n + 2] = static_cast<uint32_t>(d.deaths);
      ++n;
    }
    return n;
  };
  c.lobby_set_kills = [](uint32_t p, uint32_t v) { g_debug_scores[p].kills = static_cast<int32_t>(v); };
  c.lobby_set_deaths = [](uint32_t p, uint32_t v) { g_debug_scores[p].deaths = static_cast<int32_t>(v); };
}

void program_net_lobby(const NetLobbyAccess& a) {
  NetCalls& c = net_calls();
  c.team_of = a.team_of, c.start_of = a.start_of, c.lobby_left = a.left;
  c.lobby_suicide = a.suicide, c.lobby_kill = a.kill, c.lobby_kills = a.kills, c.lobby_deaths = a.deaths;
  c.lobby_score = a.score;
  c.lobby_scores = a.scores, c.lobby_set_kills = a.set_kills, c.lobby_set_deaths = a.set_deaths;
  c.scores_reset = a.rescore;
  c.lobby_team_id = a.team_id;
}

void program_mech_chat_draw(void* mech) {
  if (mech) mech_chat_draw(*static_cast<Mech*>(mech));
}

uint32_t program_net_team_of(uint32_t p) { return net_calls().team_of ? net_calls().team_of(p) : 0; }
int32_t program_net_start_of(uint32_t p) { return net_calls().start_of ? net_calls().start_of(p) : 0; }
int32_t program_net_score_of(uint32_t p) { return net_calls().lobby_score ? net_calls().lobby_score(p) : 0; }

uint32_t program_net_drive(int cmd, double t, uint32_t seed) {
  // The throttle up (0x18) held throughout (THROTTLE0's 0x24 is a key's
  // callback, not polled, and with ThrottleConst off a let-go throttle eases
  // back to zero). Turned (left 2, right 3) toward the nearest remote mech in
  // play, the trigger (TRIGGER1, 0x3d) held while it is ahead within 400 m;
  // inside 120 m, or with none, turns of a few seconds by the seed. Walking
  // into something (under 3 m in 3 s): a turn by the seed's side for 1.5 s,
  // then straight on for 3.5 s.
  if (cmd == 0x18) return 1u;
  if (cmd != 2 && cmd != 3 && cmd != 0x3d) return 0;
  Mech* me = launched_mech();
  float p[3] = {}, f[3] = {}, best = 1e30f, to[2] = {};
  if (me) mech_world_position(me, p), mech_forward(me, f);
  static float s_was[2] = {};
  static double s_was_t = -1, s_free_at = -1;
  if (t < s_was_t) s_was_t = s_free_at = -1;  // a new game
  if (t >= s_free_at && (s_was_t < 0 || t - s_was_t > 3.0)) {
    if (s_was_t >= 0 && std::hypot(p[0] - s_was[0], p[2] - s_was[1]) < 3.0f) s_free_at = t + 5.0;
    s_was[0] = p[0], s_was[1] = p[2], s_was_t = s_free_at > t ? s_free_at : t;
  }
  if (t < s_free_at) return t < s_free_at - 3.5 && cmd == (seed % 2 ? 2 : 3) ? 1u : 0u;
  for (NetMech* m : net_game().mechs) {
    if (!me || (m->state.flags >> 1 & 1)) continue;
    float q[3];
    mech_world_position(m, q);
    const float d = std::hypot(q[0] - p[0], q[2] - p[2]);
    if (d < best) best = d, to[0] = q[0] - p[0], to[1] = q[2] - p[2];
  }
  if (best < 1e29f && best > 120.0f) {
    const float side = f[2] * to[0] - f[0] * to[1];  // > 0: the target to the left
    const float ahead = (f[0] * to[0] + f[2] * to[1]) / best;
    const float cos_aim = std::sqrt(f[0] * f[0] + f[2] * f[2]) > 0 ? ahead / std::sqrt(f[0] * f[0] + f[2] * f[2]) : 0;
    if (cmd == 2) return side > 0 && cos_aim < 0.99f ? 1u : 0u;
    if (cmd == 3) return side <= 0 && cos_aim < 0.99f ? 1u : 0u;
    return best < 400.0f && cos_aim > 0.95f ? 1u : 0u;
  }
  const double phase = std::fmod(t + seed % 7, 10.0);
  if (cmd == 2) return phase < 3.0 ? 1u : 0u;
  if (cmd == 3) return phase >= 7.0 ? 1u : 0u;
  return best < 1e29f && std::fmod(t + seed % 3, 7.0) < 2.0 ? 1u : 0u;
}

// Mech3.exe 1.2 @ 0x004783c0 (ECX the pilot [0x0060861c], its mech +0x14):
// the pilot's mech made again as `roster` where it stands, facing as it
// did: its team and the weapons' fire mode (kept in the pilot's +0x80) kept,
// its own loadout again for its own roster (0x00414430), the view taken off
// it and put back in the mode it had (1 without a view), the level of
// detail -1 (0x0041e9e0), the HUD reset when it is the view's, then powered
// up from down. The stealth object's view attach (vt+0x108 on [0x00609a2c])
// takes the view's target ([0x005f4c8c]+0x64) off the mech while it is made
// again, so its weapons' adding asks no HUD of it: here the target is none
// (the stealth object is not made) until the mech's own attach.
void player_clear_60b56c();  // mech3/player.h ([0x0060b56c] = 0), not includable with mech.h
namespace {
int32_t g_pilot_fire_mode = 0;  // the pilot's +0x80
}  // namespace
void program_pilot_set_mech(int32_t roster) {
  Mech* m = world().mission_player;
  if (!m) return;
  const int32_t cam = program_view() ? program_view_camera_id() : 1;
  float pos[3];
  mech_world_position(m, pos);
  const float yaw = std::atan2(m->forward.x, -m->forward.z);
  Config* cfg = m->pilot && roster == m->pilot->roster ? m->config : nullptr;
  const uint32_t team = m->team;
  if (const auto* wm = static_cast<const WeaponManager*>(m->ammo)) g_pilot_fire_mode = wm->mode;
  char name[64] = {};
  if (const char* n = entity_name(g_w.handle_of(g_w.engine, m))) std::snprintf(name, sizeof name, "%s", n);  // vt+0xa4
  world().player = nullptr, weapon_world().player = nullptr;  // the stealth's vt+0x108
  mech_rebuild(*m, roster, name);  // 0x004148c0
  m->place(pos, yaw);              // 0x004161c0
  m->allies = team & ~1u, m->team = team, m->enemies = ~m->allies & ~1u;
  player_clear_60b56c();
  program_view_attach(*m);  // vt+0x108
  world().player = m, weapon_world().player = m;  // the view's target again
  mech_configure(*m, cfg);  // 0x00414430
  if (program_view()) {
    program_view_look(2);     // +0x58 vt+0x10
    program_view_set_camera(cam);  // 0x0040ae70
  }
  m->detail = static_cast<uint32_t>(-1);  // 0x0041e9e0
  if (m == program_view_target())
    if (auto* hud = static_cast<Hud*>(program_mech_hud(m))) hud->reset();  // 0x00430aa0, 0x0043be10
  m->powered_down = true;
  m->hud_saved = 1;
  m->power(0);  // 0x0041cc30
  if (auto* wm = static_cast<WeaponManager*>(m->ammo)) wm->mode = g_pilot_fire_mode;
}

int32_t program_mech_anim_state(const void* mech) {
  return entity_is_mech(mech) ? static_cast<const Mech*>(mech)->anim_state : -1;
}
void* program_mech_part(void* mech, int32_t id) {
  return entity_is_mech(mech) ? part_by_id(*static_cast<Mech*>(mech), id) : nullptr;
}
void program_part_set_internal(void* part, float v, int32_t section) {
  if (part) static_cast<MechPart*>(part)->set_internal(v, section);
}
int32_t program_net_mech_count() { return static_cast<int32_t>(net_game().mechs.size()); }
bool program_pilot_fire_probe(int32_t* trigger, float* reload_sum) {
  Mech* m = launched_mech();
  const auto* wm = m ? static_cast<const WeaponManager*>(m->ammo) : nullptr;
  if (!wm) return false;
  *trigger = wm->trigger_down ? 1 : 0;
  float r = 0;
  for (uint32_t i = 0; i < wm->count && i < 16; ++i)
    if (wm->weapons[i]) r += wm->weapons[i]->reload + (wm->weapons[i]->is_firing() ? 100.0f : 0.0f);  // a beam's: firing
  *reload_sum = r;
  return true;
}
bool program_net_running() { return net_game().running; }
bool program_pilot_alive(float* health, float pos[3]) {
  Mech* m = world().mission_player;
  if (!m) return false;
  *health = mech_health(m);
  mech_world_position(m, pos);
  return m->active && m->anim_state != 10;
}

bool program_net_hold_near(float metres) {
  Mech* me = launched_mech();
  if (!me || !me->active || me->anim_state == 10) return false;
  float p[3], q[3], best = 1e30f, at[3] = {};
  mech_world_position(me, p);
  for (NetMech* m : net_game().mechs) {
    if (m->state.flags >> 1 & 1) continue;
    mech_world_position(m, q);
    const float d = std::hypot(q[0] - p[0], q[2] - p[2]);
    if (d < best) best = d, std::memcpy(at, q, sizeof at);
  }
  if (best > 1e29f) return false;
  static float dir[2] = {0, 0};  // the side it stays on, from the first hold
  if (dir[0] == 0 && dir[1] == 0) {
    const float d = best > 0.01f ? best : 1.0f;
    dir[0] = (p[0] - at[0]) / d, dir[1] = (p[2] - at[2]) / d;
  }
  const float to[3] = {at[0] + dir[0] * metres, at[1], at[2] + dir[1] * metres};
  me->set_place(to, std::atan2(to[0] - at[0], to[2] - at[2]));  // facing it: forward is -z
  return true;
}

bool program_net_staged_kill() {
  Mech* me = launched_mech();
  if (!me || !me->active || me->anim_state == 10) return false;
  float p[3], q[3], best = 300.0f;
  mech_world_position(me, p);
  me->attacker = nullptr;
  for (NetMech* m : net_game().mechs) {
    if (m->state.flags >> 1 & 1) continue;
    mech_world_position(m, q);
    const float d = std::hypot(q[0] - p[0], q[2] - p[2]);
    if (d < best) best = d, me->attacker = m;
  }
  return mech_hit_part(me, 6, 1.0e6f);  // the head
}

bool program_net_wreck_fades() {
  const DeadCalls& dc = dead_calls();
  return dc.net_fade && dc.net_fade();
}

std::string program_net_soak_line() {
  char buf[256];
  std::string s;
  Mech* me = launched_mech();
  float p[3] = {0, 0, 0};
  if (me) mech_world_position(me, p);
  float f[3] = {0, 0, 0};
  if (me) mech_forward(me, f);
  std::snprintf(buf, sizeof buf, "me %u %.2f %.2f %.2f %d heat %.2f %x anim %d facing %.2f %.2f health %.3f", znet::local_id(),
                p[0], p[1], p[2], me && me->active && me->anim_state != 10 ? 1 : 0, me ? me->heat : 0.0f,
                me ? me->heat_state : 0u, me ? me->anim_state : 0, f[0], f[2], me ? mech_health(me) : 0.0f);
  s = buf;
  if (me) {
    std::snprintf(buf, sizeof buf, " gait %d arms %.4f %.4f %.4f %.4f", me->anim_index, me->arms[0] ? me->arms[0]->aim_pitch : 0.0f,
                  me->arms[0] ? me->arms[0]->aim_yaw : 0.0f, me->arms[1] ? me->arms[1]->aim_pitch : 0.0f,
                  me->arms[1] ? me->arms[1]->aim_yaw : 0.0f);
    s += buf;
  }
  for (NetMech* m : net_game().mechs) {
    mech_world_position(m, p);
    const uint32_t now = net_calls().now_ms ? net_calls().now_ms() : ztimer::ticks_ms();
    std::snprintf(buf, sizeof buf, "; sees %u %.2f %.2f %.2f %d health %.3f gait %d arms %.4f %.4f %.4f %.4f heard %u", m->owner,
                  p[0], p[1], p[2], (m->state.flags >> 1 & 1) ? 0 : 1, mech_health(m), m->anim_index,
                  m->arms[0] ? m->arms[0]->aim_pitch : 0.0f, m->arms[0] ? m->arms[0]->aim_yaw : 0.0f,
                  m->arms[1] ? m->arms[1]->aim_pitch : 0.0f, m->arms[1] ? m->arms[1]->aim_yaw : 0.0f, now - m->heard);
    s += buf;
  }
  return s;
}

std::string program_net_scores() {
  std::string s;
  char buf[64];
  std::vector<uint32_t> ids;  // by id: each machine lists them as it heard of them
  for (const auto& r : znet::lists().players) ids.push_back(r->id);
  std::sort(ids.begin(), ids.end());
  for (uint32_t id : ids) {
    const DebugScore& d = g_debug_scores[id];
    std::snprintf(buf, sizeof buf, "%s%u:%d/%d", s.empty() ? "" : " ", id, d.kills, d.deaths);
    s += buf;
  }
  return s;
}

bool program_net_game_over() {
  static znet::Hub* hub = nullptr;  // kept to the end of the process: the session is shut by the end
  delete hub;
  hub = new znet::Hub();
  znet::session() = znet::Session{};
  znet::initialise(hub->endpoint());
  if (!znet::host_session(znet::SessionDesc{}, "Host")) return false;
  net_start();
  const bool up = net_game().running;
  net_game_over();
  znet::shutdown();
  return up;
}

namespace {
// The chat checks' session: hosting over a hub, Bob in with a mech of our
// type 30 m to our right; his transport out.
bool chat_session(Mech* me, znet::Transport** bob) {
  if (net_game().running) net_end();  // the launch's (a network debug start's), on its own session
  static znet::Hub* hub = nullptr;  // kept: the session is shut by the end
  delete hub;
  hub = new znet::Hub();
  znet::session() = znet::Session{};
  znet::initialise(hub->endpoint());
  if (!znet::host_session(znet::SessionDesc{}, "Host")) return false;
  znet::Transport* b = *bob = hub->endpoint();
  if (!b->join("loopback") || b->create_player("Bob", "Bob") != 2) return false;
  // Bob's mech, as program_net_mech_check's (a player without one is not heard).
  if (me->config && me->pilot) {
    const std::vector<uint32_t> pk = me->config->packed();
    const uint32_t n = pk[0] & 0xffff;
    std::vector<uint8_t> rec(n + 0xc, 0);
    const int32_t head[3] = {2, 0, me->pilot->roster};
    std::memcpy(rec.data(), head, 0xc);
    std::memcpy(rec.data() + 0xc, pk.data(), n);
    b->set_data(false, 2, rec.data(), static_cast<uint32_t>(rec.size()));
  }
  znet::poll(-1);
  net_start();
  {
    float at[3];
    mech_world_position(me, at);
    at[0] += 30.0f;
    uint8_t state[0x40] = {0x0f, 0, 0x40, 0};
    std::memcpy(state + 4, at, 12);
    b->send(1, state, sizeof state, true, nullptr);
    znet::poll(-1);
  }
  return true;
}
}  // namespace

bool program_net_chat_check(NetChatSeen* out) {
  Mech* me = launched_mech();
  znet::Transport* b = nullptr;
  if (!me || !chat_session(me, &b)) return false;
  static std::vector<std::string> lines;
  lines.clear();
  NetCalls& c = net_calls();
  const auto keep = c.hud_line;
  c.hud_line = [](float, const char* t) { lines.emplace_back(t); };
  auto has_line = [](ChatPanel* p, const char* t) {
    for (const ztwod::Text& l : p->lines)
      if (std::strcmp(l.text, t) == 0) return true;
    return false;
  };
  me->command(0x77);  // CHAT_ALL
  if (ChatPanel* p = me->chat) {
    out->opened = 1;
    out->prompt_all = has_line(p, "CHAT ALL");
    out->commands_off = zin::current_map() && !zin::current_map()->enabled;
    for (const char* k = " hi there "; *k; ++k) p->key(*k, 0);
    static_cast<ztwod::Object2D*>(p)->on_enter();  // the mouse coming over it (Window::update's +0x3c)
    out->hover_kept = me->chat == p;
    p->key('\r', 0x1c);
  }
  out->closed = me->chat == nullptr;
  out->commands_back = zin::current_map() && zin::current_map()->enabled;
  znet::Packet pk;
  while (b->receive(&pk))
    if (pk.from == 1 && pk.data.size() >= 9 && pk.data[0] == 0x1f && pk.data[4] == 0 &&
        std::string(reinterpret_cast<const char*>(pk.data.data() + 8)) == "hi there")
      out->sent = 1;
  if (!lines.empty()) out->own = lines.back();
  const char evil[] = "%s%n\x1b[";
  std::vector<uint8_t> m(8 + sizeof evil, 0);
  m[0] = 0x1f, m[2] = static_cast<uint8_t>(m.size()), m[4] = 1;
  std::memcpy(m.data() + 8, evil, sizeof evil);
  b->send(0, m.data(), m.size(), false, nullptr);
  znet::poll(-1);
  if (!lines.empty()) out->peer = lines.back();
  me->command(0x76);  // CHAT_TEAM, outside team play: to all
  if (ChatPanel* p = me->chat) {
    out->team_prompt_all = has_line(p, "CHAT ALL");
    p->key('\r', 0x1c);
  }
  while (b->receive(&pk))
    if (pk.from == 1 && !pk.data.empty() && pk.data[0] == 0x1f) out->empty_sent = 1;
  c.hud_line = keep;
  if (me->chat) mech_chat_close(*me);
  net_end();  // Bob's mech freed while the world stands (a next check's session would free it after)
  znet::shutdown();
  return true;
}

bool program_net_chat_respawn_check(NetChatRespawnSeen* out) {
  Mech* me = launched_mech();
  znet::Transport* b = nullptr;
  if (!me || !chat_session(me, &b)) return false;
  auto heard = [b](const char* text) {
    bool got = false;
    znet::Packet pk;
    while (b->receive(&pk))
      got |= pk.from == 1 && pk.data.size() > 8 && pk.data[0] == 0x1f &&
             std::string(reinterpret_cast<const char*>(pk.data.data() + 8)) == text;
    return got;
  };
  auto type = [](Mech* m, const char* text) {
    for (const char* k = text; *k; ++k) m->chat->key(*k, 0);
    m->chat->key('\r', 0x1c);
  };
  NetMech* bob = nullptr;
  for (NetMech* m : net_game().mechs)
    if (m->owner == 2) bob = m;
  me->command(0x77);  // CHAT_ALL
  if (bob && me->chat) {
    me->chat->key('h', 0);  // part way through a line
    bob->active = 0;        // Bob dead, then his respawn (0x12) at his place
    float at[3];
    mech_world_position(bob, at);
    uint8_t msg[0x10] = {0x12, 0, 0x10, 0};
    std::memcpy(msg + 4, at, 12);
    b->send(1, msg, sizeof msg, true, nullptr);
    znet::poll(-1);
    for (NetMech* m : net_game().mechs) out->peer_rebuilt |= m->owner == 2 && m->active;
    out->open_after_peer = me->chat != nullptr;
    if (me->chat) type(me, "i");
    out->sent_after_peer = heard("hi");
    out->commands_after_peer = zin::current_map() && zin::current_map()->enabled;
  }
  me->command(0x77);
  if (me->chat) {
    me->chat->key('x', 0);
    program_pilot_set_mech(me->pilot ? me->pilot->roster : 0);  // ours made again with the panel up
    Mech* now = launched_mech();
    out->own_closed = now && now->chat == nullptr;
    out->commands_after_own = zin::current_map() && zin::current_map()->enabled;
    if (now) {
      now->command(0x77);
      out->reopened = now->chat != nullptr;
      if (now->chat) type(now, "back");
      out->sent_after_own = heard("back");
      if (now->chat) mech_chat_close(*now);
    }
  }
  net_end();  // Bob's mech freed while the world stands (a next check's session would free it after)
  znet::shutdown();
  return true;
}

bool program_net_groups_check(NetGroupsSeen* out) {
  Mech* me = launched_mech();
  znet::Transport* b = nullptr;
  if (!me || !chat_session(me, &b)) return false;
  auto* wm = static_cast<WeaponManager*>(me->ammo);
  if (!wm || wm->count < 2 || !wm->weapons[0] || !wm->weapons[1]) return false;
  out->moved_from = wm->weapons[0]->group;
  out->moved_to = (out->moved_from + 1) % 5;
  wm->weapons[0]->set_group(out->moved_to);  // 0x004b1e10, as the group keys do
  out->other_before = wm->weapons[1]->group;
  const DeadCalls& dc = dead_calls();
  out->respawned = dc.respawn && dc.respawn(*me);
  Mech* now = launched_mech();
  auto* w2 = now ? static_cast<WeaponManager*>(now->ammo) : nullptr;
  if (w2 && w2->weapons[0]) out->after = w2->weapons[0]->group;
  if (w2 && w2->weapons[1]) out->other_after = w2->weapons[1]->group;
  net_end();  // Bob's mech freed while the world stands (a next check's session would free it after)
  znet::shutdown();
  return true;
}

bool program_net_pose_step(int32_t frame, NetPoseSeen* out) {
  static znet::Transport* b = nullptr;
  static float start[3], first_place[3][3], first_aim[3][4], last_aim[4];
  Mech* me = launched_mech();
  if (!me) return false;
  if (frame == 0) {
    if (!chat_session(me, &b)) return false;
    mech_world_position(me, start);
    start[0] += 30.0f;
    out->ready = true;
    return true;
  }
  if (!out->ready || frame > 240) return false;
  NetMech* bob = nullptr;
  for (NetMech* m : net_game().mechs)
    if (m->owner == 2) bob = m;
  if (!bob) return false;
  if (frame == 1) {  // his answer to our ping, his delay past our round trip (a stall, a skewed clock)
    const uint32_t now = net_calls().now_ms ? net_calls().now_ms() : ztimer::ticks_ms();
    const uint32_t ans[5] = {0x10 | 0x14u << 16, 2, bob->ping_id, 0, now - bob->ping_sent + 5000};
    b->send(1, ans, sizeof ans, true, nullptr);
    znet::poll(-1);
    out->latency = bob->latency;
  }
  const int ph = frame <= 60 ? 0 : frame <= 150 ? 1 : 2;
  // His place: still, then 1/3 m a frame along -z (10 m/s), then still there.
  const int walked = frame <= 60 ? 0 : frame <= 150 ? frame - 60 : 90;
  NetStateIn st;
  st.type = 0xf, st.size = sizeof st;
  st.pos[0] = start[0], st.pos[1] = start[1], st.pos[2] = start[2] - walked / 3.0f;
  if (ph == 1) st.vel[2] = static_cast<int16_t>(-10 * 127), st.throttle = static_cast<int8_t>(0.6f * 127);
  st.aim[0] = 0.1f + 0.0005f * walked, st.aim[1] = -0.05f, st.range = 200.0f;  // walking, his aim drifts up
  st.flags = static_cast<uint32_t>(me->anim_state & 0x1f) << 2;
  b->send(1, &st, sizeof st, true, nullptr);
  znet::poll(-1);
  NetPoseSeen::Phase& p = out->phase[ph];
  float at[3];
  mech_world_position(bob, at);
  const float aim[4] = {bob->arms[0] ? bob->arms[0]->aim_pitch : 0.0f, bob->arms[0] ? bob->arms[0]->aim_yaw : 0.0f,
                        bob->arms[1] ? bob->arms[1]->aim_pitch : 0.0f, bob->arms[1] ? bob->arms[1]->aim_yaw : 0.0f};
  if (p.frames++ == 0) {
    std::memcpy(first_place[ph], at, sizeof at);
    std::memcpy(first_aim[ph], aim, sizeof aim);
  }
  // Each phase's first frames settle: the aim's slerp (40), the gait's change
  // (15), the stop's ease (70: his own throttle eases back as the owner's).
  if (p.frames > (ph == 0 ? 40 : ph == 1 ? 15 : 70)) {
    p.stand += bob->anim_index == 2;
    p.walking += bob->anim_index != 2;
    for (int i = 0; i < 4; ++i) {
      p.aim_spread = std::max(p.aim_spread, std::fabs(aim[i] - first_aim[ph][i]));
      p.aim_jump = std::max(p.aim_jump, std::fabs(aim[i] - last_aim[i]));
    }
  } else {
    std::memcpy(first_aim[ph], aim, sizeof aim);
  }
  std::memcpy(last_aim, aim, sizeof aim);
  p.moved = std::hypot(at[0] - first_place[ph][0], at[2] - first_place[ph][2]);
  if (frame == 240) net_end(), znet::shutdown();  // its mechs freed while the world stands
  return true;
}

bool program_player_death_check(PlayerDeathSeen* out) {
  Mech* me = launched_mech();
  Hud* h = me ? static_cast<Hud*>(program_mech_hud(me)) : nullptr;
  auto* wm = me ? static_cast<WeaponManager*>(me->ammo) : nullptr;
  if (!h || !wm) return false;
  HudZoomWindow& z = h->reticle.zoom;
  if (!z.x174) z.toggle_lock();  // keypad 0: locked open
  out->hud_before = h->active, out->locked_before = z.x174;
  auto playing = [] {  // the voices playing and the radio lines queued (QUEUE sounds wait their turn)
    zsnd::Snapshot* s = zsnd::snapshot();
    const int32_t n = static_cast<int32_t>(s->size()) - 1;  // the master volume's record first
    zsnd::free_snapshot(s);
    const std::list<zsnd::Queued*>* q = zsnd::state().queue;
    return n + (q ? static_cast<int32_t>(q->size()) : 0);
  };
  Weapon* w = nullptr;  // the first with a NAMESOUND (weapons.zrd), its voices
  const WeaponType* t = nullptr;
  for (uint32_t i = 0; i < wm->count && i < 16 && !w; ++i)
    if (Weapon* c = wm->weapons[i]; c && c->def && c->def->type && c->def->type->name_sound) w = c, t = c->def->type;
  if (!w) return false;
  out->voices_wanted = (t->name_sound ? 1 : 0) + (t->part_destroy_sound ? 1 : 0);
  const int32_t before = playing();
  w->destroy();
  out->voices_started = playing() - before;
  out->killed = mech_hit_part(me, 6, 1.0e6f);  // the head
  out->hud_after = h->active, out->locked_after = z.x174, out->hidden_after = z.flags >> 4 & 1;
  return true;
}

bool program_net_scores_box_check(NetScoresBoxSeen* out) {
  Mech* me = launched_mech();
  znet::Transport* b = nullptr;
  if (!me || !chat_session(me, &b)) return false;
  Hud* h = static_cast<Hud*>(program_mech_hud(me));
  if (!h) return false;
  HudObjectives& box = h->objectives;
  auto written = [&box] {  // its lines with a text (it keeps 25)
    int32_t n = 0;
    for (const ztwod::Text& t : box.lines) n += t.text && *t.text;
    return n;
  };
  box.show_for(-1.0f);  // F12: shown and filled
  box.clear();
  net_score_kill(1, 1);  // our own death: the host scores it
  out->refilled_open = written();
  if (box.lines.size() && box.lines[0].text) out->header = box.lines[0].text;
  box.hide();
  box.clear();
  net_score_kill(1, 1);
  out->refilled_closed = written();
  net_end();
  znet::shutdown();
  return true;
}

bool program_chat_open() {
  Mech* me = launched_mech();
  if (!me) return false;
  if (!me->chat) me->command(0x77);
  return me->chat != nullptr;
}
std::string program_chat_line() {
  Mech* me = launched_mech();
  if (!me || !me->chat) return {};
  const char* t = me->chat->editor ? me->chat->editor->text() : nullptr;
  const char* gt = t ? std::strchr(t, '>') : nullptr;
  return gt ? std::string(gt + 1 + (gt[1] == ' ')) : std::string(t ? t : "");
}
void program_chat_close() {
  if (Mech* me = launched_mech(); me && me->chat) mech_chat_close(*me);
}

bool program_net_mech_check(NetMechSeen* out) {
  Mech* me = launched_mech();
  if (!me || !me->config || !me->pilot) return false;
  znet::Hub hub;
  znet::session() = znet::Session{};
  znet::initialise(hub.endpoint());
  if (!znet::host_session(znet::SessionDesc{}, "Host")) return false;
  znet::Transport* b = hub.endpoint();
  if (!b->join("loopback") || b->create_player("Bob", "Bob") != 2) return false;
  const std::vector<uint32_t> p = me->config->packed();
  const uint32_t n = p[0] & 0xffff;
  std::vector<uint8_t> rec(n + 0xc, 0);
  const int32_t head[3] = {2, 0, me->pilot->roster};
  std::memcpy(rec.data(), head, 0xc);
  std::memcpy(rec.data() + 0xc, p.data(), n);
  b->set_data(false, 2, rec.data(), static_cast<uint32_t>(rec.size()));
  znet::poll(-1);
  const size_t before = znet::session().handlers.size();
  net_start();
  float at[3];
  mech_world_position(me, at);
  at[0] += 30.0f;
  uint8_t state[0x40] = {0x0f, 0, 0x40, 0};
  std::memcpy(state + 4, at, 12);
  b->send(1, state, sizeof state, true, nullptr);
  znet::poll(-1);
  if (NetMech* m = net_mech(2)) {
    out->made = 1, out->remote = m->remote, out->collides = m->collides;
    out->torso_mode = m->torso ? m->torso->mode : -1;
    out->in_world = zn(m->node)->parent_count() > 0;
    float t[3];
    mech_world_position(m, t);
    out->moved = std::fabs(t[0] - at[0]) < 0.01f && std::fabs(t[2] - at[2]) < 0.01f;
    // A second state 10 m on, its flag bit 0 set: the reckoning puts it there.
    float on[3] = {at[0] + 10.0f, at[1], at[2]};
    std::memcpy(state + 4, on, 12);
    const uint32_t snap = 1;
    std::memcpy(state + 0x38, &snap, 4);
    b->send(1, state, sizeof state, true, nullptr);
    znet::poll(-1);
    net_reckon(*m, 1.0f / 30.0f);
    mech_world_position(m, t);
    out->followed = std::fabs(t[0] - on[0]) < 0.01f && std::fabs(t[2] - on[2]) < 0.01f;
    // Dead, then made again by a 0x12 (piece 7a): the same mech, rebuilt in place.
    m->active = false;
    uint8_t re[0x10] = {0x12, 0, 0x10, 0};
    const float back[3] = {at[0] + 20.0f, at[1], at[2]};
    std::memcpy(re + 4, back, 12);
    b->send(1, re, sizeof re, true, nullptr);
    znet::poll(-1);
    mech_world_position(m, t);
    out->respawned = net_mech(2) == m && m->active && m->remote && !m->collides && std::fabs(t[0] - back[0]) < 0.01f &&
                     std::fabs(t[2] - back[2]) < 0.01f;
  }
  // The pilot's own state, as the timer sends it (0x0046f160 kind 0xf).
  net_send_event(*me, 0xf, 0);
  float mine[3];
  mech_world_position(me, mine);
  for (znet::Packet pk; b->receive(&pk);)
    if (pk.from == 1 && pk.data.size() == 0x3c && pk.data[0] == 0xf) {
      float got[3];
      std::memcpy(got, pk.data.data() + 4, 12);
      out->state_sent = got[0] == mine[0] && got[1] == mine[1] && got[2] == mine[2];
    }
  out->configs = static_cast<int32_t>(net_game().configs.size());
  out->config_named = out->configs == 1 && std::strcmp(net_game().configs.front()->name, "Bob's Config") == 0;
  b->close();
  znet::poll(-1);
  out->after_leave = static_cast<int32_t>(net_game().mechs.size());
  net_end();
  out->handlers_after = static_cast<int32_t>(znet::session().handlers.size() - before);
  znet::shutdown();
  return true;
}

bool program_net_hostile_check(NetHostileSeen* out) {
  Mech* me = launched_mech();
  if (!me || !me->config || !me->pilot) return false;
  znet::Hub hub;
  znet::session() = znet::Session{};
  znet::initialise(hub.endpoint());
  if (!znet::host_session(znet::SessionDesc{}, "Host")) return false;
  znet::Transport* b = hub.endpoint();
  if (!b->join("loopback") || b->create_player("Bob", "Bob") != 2) return false;
  const std::vector<uint32_t> p = me->config->packed();
  const uint32_t n = p[0] & 0xffff;
  std::vector<uint8_t> rec(n + 0xc, 0);
  const int32_t head[3] = {2, 0, me->pilot->roster};
  std::memcpy(rec.data(), head, 0xc);
  std::memcpy(rec.data() + 0xc, p.data(), n);
  b->set_data(false, 2, rec.data(), static_cast<uint32_t>(rec.size()));
  znet::poll(-1);
  net_start();
  float at[3];
  mech_world_position(me, at);
  at[0] += 30.0f;
  uint8_t state[0x40] = {0x0f, 0, 0x40, 0};
  std::memcpy(state + 4, at, 12);
  auto send = [&](const void* m, size_t size) {
    b->send(1, m, static_cast<uint32_t>(size), true, nullptr);
    znet::poll(-1);
  };
  send(state, sizeof state);
  NetMech* m = net_mech(2);
  if (m) {
    out->made = 1;
    NetCalls& c = net_calls();
    const NetCalls kept = c;
    static int32_t s_craters, s_effects, s_fires;
    static char s_last31;
    c.crater_make = [](const zwep::CraterOrder*) -> int32_t { return ++s_craters, 1; };
    c.log_has = [](const zeff::AnimLogRecord* r) { return ++s_effects, s_last31 = r->name[0x1f], true; };
    c.fire = [](NetMech&, uint32_t, uint32_t, int32_t) { ++s_fires; };
    // The crater (0x19, from the host's side: made and resent unless bit 7).
    auto crater = [&](uint32_t material) {
      uint8_t q[0x1c] = {0x19, 0, 0x1c, 0};
      std::memcpy(q + 8, &material, 4);
      const float r = 5.0f;
      std::memcpy(q + 0xc, at, 12), std::memcpy(q + 0x18, &r, 4);
      send(q, sizeof q);
    };
    s_craters = 0;
    crater(static_cast<uint32_t>(zmod::materials().capacity));
    crater(0x7fffffffu);
    out->craters_bad = s_craters;
    s_craters = 0;
    crater(0);
    out->craters_good = s_craters;
    // The effect (0x1a): the record after the header.
    auto effect = [&](int32_t type, int32_t node, uint32_t arg0, bool long_name) {
      uint8_t q[0x54] = {0x1a, 0, 0x54, 0};
      zeff::AnimLogRecord r;
      r.type = type, r.node = node, r.args[0] = arg0, r.args[4] = static_cast<uint32_t>(-1);
      std::memset(r.name, 'A', long_name ? sizeof r.name : 4);
      std::memcpy(q + 4, &r, sizeof r);
      send(q, sizeof q);
    };
    const zcls::NodePool& pool = zcls::nodes();
    s_effects = 0;
    effect(2, -5, 0, false);
    effect(2, static_cast<int32_t>(pool.blocks.size()) * pool.block_size, 0, false);
    if (pool.free_head >= 0) effect(2, pool.free_head, 0, false);
    effect(3, -1, static_cast<uint32_t>(-7), false);
    out->effects_bad = s_effects;
    s_effects = 0, s_last31 = 'A';
    effect(2, -1, 0, true);
    out->effect_good = s_effects;
    out->name_ended = s_effects == 1 && s_last31 == 0;
    // A state saying 127 shots (flags bits 12..18) in a message holding 4.
    uint8_t shots[0x40] = {0x0f, 0, 0x40, 0};
    std::memcpy(shots + 4, at, 12);
    const uint32_t f = 127u << 12;
    std::memcpy(shots + 0x38, &f, 4);
    s_fires = 0;
    send(shots, sizeof shots);
    out->fires = s_fires;
    // Postures (0x17) of no action.
    const size_t before = m->actions.size();
    for (int32_t kind : {1000, -3, 10}) {
      uint8_t q[8] = {0x17, 0, 8, 0};
      std::memcpy(q + 4, &kind, 4);
      send(q, sizeof q);
    }
    out->actions_queued = static_cast<int32_t>(m->actions.size() - before);
    c = kept;
    // Every routed type, any length to 0x60, random bytes: none may crash. The
    // game's clock (0x11) and a leave are left out: they end what is tested.
    static const uint16_t kTypes[] = {0xf, 0x10, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1f, 0x21};
    uint32_t seed = 12345;
    auto next = [&seed] { return seed = seed * 1103515245u + 12345u, seed >> 8; };
    for (int i = 0; i < 3000; ++i) {
      std::vector<uint8_t> q(next() % 0x61);
      for (uint8_t& v : q) v = static_cast<uint8_t>(next());
      const uint16_t type = kTypes[next() % std::size(kTypes)];
      if (q.size() >= 2) q[0] = static_cast<uint8_t>(type), q[1] = static_cast<uint8_t>(type >> 8);
      if (q.size() >= 4 && next() % 2) q[2] = static_cast<uint8_t>(q.size()), q[3] = 0;
      if (q.size() >= 2) send(q.data(), q.size()), ++out->fuzzed;
    }
  }
  b->close();
  znet::poll(-1);
  net_end();
  znet::shutdown();
  return true;
}

void wire_mech_build(const MechWiring* w) {
  if (!w) {
    g_w = MechWiring{};
    pose_calls() = PoseCalls{};
    skis_calls() = SkisCalls{};
    collide_calls() = CollideCalls{};
    controls_calls().ai_brain = nullptr;
    update_calls().moved = nullptr, update_calls().purge = nullptr, update_calls().add_dead = nullptr, update_calls().set_volume = nullptr, update_calls().set_position = nullptr;
    command_calls() = CommandCalls{};
    part_calls() = PartCalls{};
    move_calls() = MoveCalls{};
    heat_calls().throttle = nullptr, heat_calls().game_type = nullptr, heat_calls().play_sound = nullptr, heat_calls().play_sound_at = nullptr;
    heat_calls().stop_sound = nullptr, heat_calls().play_mech_sound = nullptr;
    update_calls().stop_cockpit_sound = nullptr;
    collide_calls().touch = nullptr, collide_calls().node_touchable = nullptr, collide_calls().hit_box = nullptr;
    unwire_hud();
    wire_cheats(false);
    unwire_weapons();
    set_mech_build_hooks(nullptr);
    set_shade_hooks(nullptr);
    update_calls().x68c_update = nullptr;
    free_calls().ref_free = nullptr, free_calls().ref_delete = nullptr;
    free_calls().player_object_free = nullptr;
    damage_calls().node_unlink = nullptr;
    set_mech_data_hooks(nullptr);
    set_launch_hooks(nullptr);
    panel_calls().entity_mech = nullptr, panel_calls().key_binding = nullptr, panel_calls().key_name = nullptr;
    wire_pocket(nullptr);
    wire_net(false);
    return;
  }
  g_w = *w;
  wire_net(true);
  // The pocket commander's panel (docs/ai.md 17.6b): zEntity's mechs, the
  // orders' keys (an action's first key, 0x0051d1f0, and its name,
  // 0x0051d330). Its names, cursor picks and marker are not wired yet (L4c2c).
  panel_calls().entity_mech = entity_mech;
  wire_pocket(w);
  wire_vehicles(w);
  wire_turrets(w);
  panel_calls().key_binding = [](int32_t action) -> zgame::Handle { return zin::key1_of(action); };
  panel_calls().key_name = [](zgame::Handle k, char* out, int32_t n) { zin::key_name(k, out, static_cast<size_t>(n)); };
  set_shade_hooks(&g_shade_hooks);
  update_calls().x68c_update = [](Handle s, float dt) { shade_update(*static_cast<SunShade*>(s), dt); };
  free_calls().ref_free = [](Handle ref, Handle n) {  // 0x004e66a0, then the delete
    shade_level_reset(*static_cast<ShadeLevel*>(ref), WiredShade::id(n));
    delete static_cast<ShadeLevel*>(ref);
  };
  free_calls().ref_delete = [](Handle s) { delete static_cast<SunShade*>(s); };  // vt+0 (0x004e6220)
  free_calls().player_object_free = [](Mech& m) { mech_chat_close(m); };  // 0x0041e580
  damage_calls().node_unlink = [](Handle ref, Handle n) {                        // 0x004e66a0
    shade_level_reset(*static_cast<ShadeLevel*>(ref), WiredShade::id(n));
  };
  set_mech_build_hooks(&g_build);
  set_mech_data_hooks(&g_data);
  set_launch_hooks(&g_launch);
  // The weapons a mech or a basic vehicle mounts look up its owner,
  // firepoints and jump jets (weapon.h). +0x80 is the vehicle's mech (vt+0x2c
  // then vt+0x50): 0 for a basic vehicle, whose weapons fire from their own
  // nodes (0x004b4660's firepoints, weapons.md).

  wire_mech_frame();
  wire_mech_parts();
  wire_hud();
  wire_cheats(true);
  wire_weapons([](const char* key) { return g_w.unported ? g_w.unported(g_w.engine, key, {}) : 0u; });
  WeaponCalls& c = weapon_calls();
  // (A mech's manager is made while the mech is built, before zEntity knows
  // it: the basic vehicles are the ones told apart.)
  c.vehicle_mech = [](Handle v) -> Handle { return is_program_vehicle(v) ? nullptr : v; };
  c.vehicle_node = [](Handle v) -> Handle {  // +0x04
    return is_program_vehicle(v) ? program_vehicle_node(v) : static_cast<Mech*>(v)->node;
  };
  c.add_jump_jet = [](Handle m) { static_cast<Mech*>(m)->add_jump_jet(true); };
  c.firepoints = [](Handle m) -> std::vector<Firepoint*>* {
    const MechDfn* d = mech_dfn(static_cast<Mech*>(m)->pilot->roster);
    return d ? d->firepoints : nullptr;
  };
}

void mech_types_open() {
  if (!open_mechlib()) std::fprintf(stderr, "mw3: no zbd\\mechlib.zbd: the mechs have no models\n");
}
void mech_types_refresh() { mech_data_refresh(); }
void mech_types_close() { close_mechlib(); }
void mech_types_forget() { mech_data_clear(); }
void mech_canned_nodes_free() { canned_nodes_free(); }
void mech_destroy_part(void* mech, int32_t id) { static_cast<Mech*>(mech)->destroy_part(id); }
// Mech3.exe 1.2 @ 0x004782c0 with 0: the pilot lets its mech go, keeping
// the weapons' fire mode (+0xd4 +0x1c) for its +0x80; the mech globals the
// launch set (pilot_set_mech) go with it.
bool mech_player_release(void* mech, int32_t* fire_mode) {
  auto* m = static_cast<Mech*>(mech);
  const auto* wm = m ? static_cast<const WeaponManager*>(m->ammo) : nullptr;
  if (wm) *fire_mode = wm->mode;
  world().mission_player = world().player = nullptr;
  weapon_world().mission_player = weapon_world().player = nullptr;
  return wm != nullptr;
}

void* mission_player_mech() { return world().mission_player; }

void mech_dead_tick(float dt) { dead_tick(dt); }
void mech_dead_clear() { dead_mechs().clear(); }

// Mech3.exe 1.2 @ 0x00414000: the scripts' path list freed ([0x00736584],
// 0x00576120: the next set-up makes it again), the motion archive closed
// (0x005a98a0), the dead mechs' list emptied (its mechs are gone with the
// rest) and the three radar blips deleted.
void mechs_shutdown() {
  field_bases_path_made() = false;
  close_motions();
  mech_dead_clear();
  mech_radar_blips_destroy();
}
void* mech_last_attacker(const void* mech) {
  return entity_is_mech(mech) ? static_cast<const Mech*>(mech)->attacker : nullptr;
}
const UnbindCalls& entity_unbind_calls() {
  static const UnbindCalls c = [] {
    UnbindCalls u;
    u.part_mech = [](zcls::Node* n) -> void* { return n->owner ? static_cast<MechPart*>(n->owner)->mech : nullptr; };
    return u;
  }();
  return c;
}
void mechs_make_blips() {
  if (!mech_radar_blip(0)) mech_radar_blips_create();
}
void mechs_end_state(bool* blips, bool* motion, bool* path) {
  *blips = mech_radar_blip(0) != nullptr;
  zsis::Script sc;
  *motion = load_motion(sc, "vulture_walk");
  if (*motion) script_free_parts(sc);
  *path = field_bases_path_made();
}

bool mech_free(void* obj) {
  if (!entity_is_mech(obj)) return false;
  // The program's own lists of it (not the exe's): the AI mechs by name, the handle.
  mission_ai_mech_forget(obj, g_w.handle_of ? g_w.handle_of(g_w.engine, obj) : 0);
  mech_delete(static_cast<Mech*>(obj));
  mech_objects().erase(obj);  // a mech without a node keeps its zEntity entry (as the exe's)
  if (g_w.forget_object) g_w.forget_object(g_w.engine, obj);
  return true;
}
void mech_kill(void* mech, void* killer) {
  auto* m = static_cast<Mech*>(mech);
  if (killer) m->attacker = killer;
  m->torso->destroy(0, 1);
}

std::vector<std::string>& mech_parts_fx_log() { return g_parts_fx; }

bool launched_mech(LaunchedMech* out) { return describe_mech(mech3::launched_mech(), out); }

bool describe_mech(const void* mech, LaunchedMech* out) {
  if (!entity_is_mech(mech)) return false;  // not a mech (or gone): nothing to read
  const Mech* m = static_cast<const Mech*>(mech);
  LaunchedMech s;
  s.roster = m->pilot->roster;
  const MechPart* parts[] = {m->hip, m->torso, m->head, m->arms[0], m->arms[1], m->legs[0], m->legs[1]};
  for (const MechPart* p : parts) {
    s.parts += p && p->node;
    if (p) s.armor += p->armor + p->internal;
  }
  s.heat = m->heat;
  for (const Arm* a : m->arms) s.parts += a && a->hand && a->hand->node;
  for (const Leg* l : m->legs) {
    if (!l) continue;
    s.parts += l->calf && l->calf->node;
    s.parts += l->foot && l->foot->node;
    if (l->foot) s.toes += static_cast<int32_t>(l->foot->toe_count);
  }
  if (const auto* wm = static_cast<const WeaponManager*>(m->ammo)) {
    s.weapons = static_cast<int32_t>(wm->count);
    s.group = wm->group;
    if (const WeaponGroup* g = wm->group >= 0 && wm->group < 5 ? wm->groups[wm->group] : nullptr) s.weapon = g->current_weapon;
  }
  s.jets = m->equipment[11];
  s.jets_firing = m->jump_jets_firing;
  s.jet_flame = m->jet_effect != nullptr;
  s.invincible = m->invincible;
  s.throttle_returns = m->controls && m->controls->throttle_returns;
  s.torso_absolute = m->torso && m->torso->absolute;
  s.crouched = m->crouched;
  s.alive = m->alive_time;
  s.anim_state = m->anim_state;
  for (int i = 0; i < 4; ++i) s.blocked[i] = m->blocked[i];
  s.attacker = m->attacker;
  s.action = m->actions.empty() ? 0 : m->actions.front();
  if (auto* n = zn(m->node)) s.deadmech = std::strcmp(n->name(), "deadmech") == 0;
  s.aim_torso = m->torso && m->target_node == m->torso->node;
  for (int i = 0; i < 6; ++i) s.kills[i] = m->x3f4[i];
  s.detail = m->detail, s.lod = m->x690;
  for (const Mech* d : dead_mechs()) s.dead_listed |= d == m;
  s.debris = static_cast<int32_t>(m->x6d0.size());
  s.debris_in_world = !m->x6d0.empty();
  for (Handle d : m->x6d0)
    if (!(zn(d)->parent_count() > 0 && zn(d)->parents[0] == world_hooks().world)) s.debris_in_world = false;
  s.sinks = m->heat_sinks.count;
  s.height = m->height;
  s.max_speed = m->max_speed;
  s.config = m->config;
  s.torso_armor_scale = m->torso ? m->torso->armor_scale : 0;
  s.team = m->team;
  s.skis = m->skis != nullptr;
  s.ams = m->ams != nullptr;
  s.motions = m->anims[4] && m->anims[8];
  s.powering_down = m->power_down_pending || m->powered_down;
  s.powered_down = m->powered_down;
  if (m->transform) s.pos[0] = m->transform[9], s.pos[1] = m->transform[10], s.pos[2] = m->transform[11];
  s.yaw_sin = m->orientation[2];
  s.feet_down = m->skis && m->skis->down[0] && m->skis->down[1];
  const zcls::Node* n = static_cast<const zcls::Node*>(m->node);
  for (int32_t i = 0; n && i < n->parent_count(); ++i) s.in_world |= n->parents[i] == world_hooks().world && n->parents[i];
  *out = s;
  return true;
}

void* build_ai_mech(int32_t mech, void* controls, int32_t config, const char* name) {
  return mech_create(mech, static_cast<MechControls*>(controls), config, name);
}
void program_mech_set_controls(void* mech, void* controls) {
  auto* m = static_cast<Mech*>(mech);
  auto* c = static_cast<MechControls*>(controls);
  m->controls = c;
  if (c) c->set_owner(m);  // IControls vt+0x08
}
bool program_vehicle_centre(void* vehicle, float out[3]) {
  const Vec3* p = vehicle ? entity_position(*static_cast<Vehicle*>(vehicle)) : nullptr;
  if (!p) return false;
  out[0] = p->x, out[1] = p->y, out[2] = p->z;
  return true;
}
bool program_mech_configure(void* mech, void* config) {
  return mech && mech_configure(*static_cast<Mech*>(mech), static_cast<Config*>(config));
}
int32_t program_mech_roster(const void* mech) { return static_cast<const Mech*>(mech)->pilot->roster; }
void* program_mech_own_net(void* mech, uint32_t self) {
  return static_cast<ai::AiNet*>(ai::vehicle_net_of(*static_cast<Mech*>(mech), self));
}
bool set_mech_skin(void* mech, void* skin) { return mech && mech_set_skin(*static_cast<Mech*>(mech), skin); }
int32_t place_mech(void* mech, const float pos[3], uint32_t yaw) {
  float angle;
  std::memcpy(&angle, &yaw, 4);
  return mech ? static_cast<Mech*>(mech)->place(pos, angle) : 0;
}
int32_t spawn_updates() { return g_spawn_updates; }
const float* spawn_position() { return g_spawn_pos; }
bool is_launched_mech(const void* mech) { return mech && mech == mech3::launched_mech(); }
// Mech3.exe 1.2 @ 0x0041abd0: a lock on the view's mech plays its pilot
// sound 9 (0x00432990(9, 1.0, 0, 0, -1)) into +0x5dc.
void mech_lock_warning(void* mech) {
  if (!mech || mech != program_view_target()) return;
  Mech& m = *static_cast<Mech*>(mech);
  m.sounds[9] = pilot_play(m, 9, 1.0f, nullptr);
}
bool mech_aim_at(void* mech, const float at[3]) {
  if (!mech) return false;
  const Vec3 p{at[0], at[1], at[2]};
  return static_cast<Mech*>(mech)->aim_at(&p, nullptr, true);
}
void update_mech(void* mech, float dt) {
  if (mech) static_cast<Mech*>(mech)->update(dt);
}
// Mech3.exe 1.2 @ 0x004a1140 ([0x005e7284] is 1): the entity manager's walk
// 0x005a8850 over zEntity's list, the program's mechs; the sensors leave the
// player's +0x09 clear, so its frame is the player tick's alone. The basic
// vehicles' frame is 0x004a1040 (app_vehicle.cpp).
void update_entities(float dt) {
  zent::budget().update_all(ai::entity_list(), [dt](ai::Handle h) {
    Mech* m = entity_mech(h);
    if (!m) {  // another entity's vt+0x34
      if (program_pocket_marker(h)) return;  // the command marker's (0x004d7f80's: nothing to do)
      if (void* s = g_w.object_of(g_w.engine, h); s && s == program_stealth()) {
        if (program_vehicle_sensed(s)) program_vehicle_frame(s, dt);  // +0x09, which the stealth keeps 0
        return;
      }
      if (void* v = g_w.object_of(g_w.engine, h); is_program_vehicle(v)) return program_vehicle_frame(v, dt);
      if (tur::Turret* t = program_turret_by_handle(h)) {  // 0x004a6be0 while the sensors keep it up (+0x09)
        if (t->sensed) program_turret_frame(t);
        return;
      }
      if (g_w.unported) g_w.unported(g_w.engine, "entity vt+0x34", {h});
      return;
    }
    if (m->x09) m->update(dt);  // vt+0x34
  });
}
// Mech3.exe 1.2 @ 0x0041ca30: the player's mech takes the value, any other 1.
void mech_set_hud_fall(void* mech, int32_t v) {
  if (!mech) return;
  Mech* m = static_cast<Mech*>(mech);
  m->hud_fall = m == world().mission_player ? v : 1;  // [0x0060861c]+0x14
}
void move_mech(void* mech, const float pos[3], float yaw) {
  if (mech) static_cast<Mech*>(mech)->set_place(pos, yaw);
}
void mech_knock_down(void* mech, int32_t kind) {
  if (mech) static_cast<Mech*>(mech)->knocked_down(kind);
}
void mech_get_up(void* mech) {
  if (mech) static_cast<Mech*>(mech)->get_up();
}
void mech_power(void* mech, int32_t down) {
  if (mech) static_cast<Mech*>(mech)->power(down);
}
uint32_t mech_allies(const void* mech) { return mech ? static_cast<const Mech*>(mech)->allies : 0; }
std::vector<DebriefMech> mech_debrief_lance() {
  std::vector<DebriefMech> r;
  for (const ai::Handle h : ai::entity_list()) {
    Mech* m = entity_mech(h);
    if (!m) continue;
    DebriefMech d;
    d.mech = m;
    d.player = m == world().mission_player;
    d.team = m->team;
    d.type = m->pilot ? m->pilot->roster : -1;
    for (int i = 0; i < 6; ++i) d.stats[i] = static_cast<int32_t>(m->x3f4[i]);
    r.push_back(d);
  }
  return r;
}
namespace {
// A ported mech as 0x00472f00 reads it.
// The salvage's own calls on the ported mechs.
const SalvageCalls& salvage_calls() {
  static const SalvageCalls c = [] {
    SalvageCalls k;
    k.rand = [] { return static_cast<int32_t>(crt::msvc_rand()); };
    k.empty_bin = [](const SalvageMech& v, int32_t bin) {
      const_cast<AmmoBin*>(static_cast<const AmmoBin*>(v.owner_bins[bin]))->set_amount(0);
    };
    return k;
  }();
  return c;
}

// A weapon manager's slots and bins as 0x00472da0 reads them.
void salvage_weapons(const void* ammo, SalvageMech& v) {
  const auto* wm = static_cast<const WeaponManager*>(ammo);
  if (!wm) return;
  v.has_manager = true;
  std::vector<const AmmoBin*> bins;
  for (uint32_t i = 0; i < wm->count && i < 16; ++i) {
    SalvageWeapon w;
    const Weapon* x = wm->weapons[i];
    if (x) {
      w.present = true;
      w.type = x->def && x->def->type ? x->def->type->id : -1;
      w.has_item = x->item != nullptr;
      w.item_tech = x->item ? x->item->tech : 0;
      w.beam = x->is_beam();
      if (x->bin) {
        size_t k = 0;
        while (k < bins.size() && bins[k] != x->bin) ++k;
        if (k == bins.size()) {
          bins.push_back(x->bin);
          v.bins.push_back(x->bin->left());
        }
        w.bin = static_cast<int32_t>(k);
      }
    }
    v.weapons.push_back(w);
  }
  v.owner_bins.assign(bins.begin(), bins.end());
}

SalvageMech salvage_view(Mech& m) {
  SalvageMech v;
  v.has_data = m.pilot != nullptr;
  v.type = m.pilot ? m.pilot->roster : -1;
  // 0x0041df10: mech_tech of the definition's +0x00, which is its roster
  // index (MechDfn::index, the cache 0x004846a0 is by roster); a built
  // mech's definition is already loaded, so the cache's lazy load is moot.
  v.tech = mech_tech(v.type);
  v.torso_gone = m.torso && m.torso->destroyed;
  v.head_gone = m.head && m.head->destroyed;
  for (int i = 0; i < 2; ++i) v.leg_gone[i] = m.legs[i] && m.legs[i]->destroyed;
  salvage_weapons(m.ammo, v);
  v.config = m.config;
  for (int id = 0; id < 16; ++id) {
    const MechPart* p = part_by_id(m, id);
    v.parts[id] = p ? (p->destroyed ? 2 : 1) : 0;
  }
  if (cheats().whole) {  // ours: there is no cow level's kills, as if undamaged (cheats.h)
    v.torso_gone = v.head_gone = v.leg_gone[0] = v.leg_gone[1] = false;
    for (int8_t& p : v.parts)
      if (p == 2) p = 1;
  }
  v.sink_tech = m.heat_sinks.tech;
  v.sinks = m.heat_sinks.count;
  v.doubles = m.heat_sinks.doubles;
  return v;
}
}  // namespace

void mech_lance_salvage(Store& own, bool team) {
  std::vector<SalvageEntity> list;
  Mech* player = world().mission_player;
  for (const ai::Handle h : ai::entity_list()) {
    Mech* m = entity_mech(h);
    SalvageEntity e;
    if (m) {
      e.mech = true;
      e.player = m == player;
      e.team = m->team;
      e.m = salvage_view(*m);
    }
    list.push_back(std::move(e));
  }
  lance_salvage(mission_salvage(), own, list, player ? player->allies : 0, team, salvage_calls());

}

// Departures: a mech without a pilot (+0x684), a loadout (+0x3ec) or one of
// the parts reads as type -1, no armor, nothing left, where the exe would
// read through null.
bool mech_lance_armor(void* mech, int32_t* type, int32_t armor[11], float left[11]) {
  auto* m = static_cast<Mech*>(mech);
  if (!m) return false;
  *type = m->pilot ? m->pilot->roster : -1;
  if (m->config)
    for (int i = 0; i < 11; ++i) armor[i] = m->config->armor[i];
  // Bar k: part, section (0x0047ef10's order).
  static const int8_t kParts[11][2] = {{1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5},
                                       {6, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}};
  for (int k = 0; k < 11; ++k) {
    const MechPart* p = part_by_id(*m, kParts[k][0]);
    left[k] = p ? p->get_armor(kParts[k][1]) : 0.0f;
  }
  return true;
}
const char* mech_brain_name(void* mech) {
  auto* m = static_cast<Mech*>(mech);
  auto* c = m ? dynamic_cast<AiControls*>(m->controls) : nullptr;  // vt+0x140, then vt+0x54
  return c && c->brain ? c->brain->params.pilot : nullptr;
}
void* mech_mission_player() { return world().mission_player; }
bool entity_is_mech(const void* o) { return o && mech_objects().count(o) != 0; }

void mech_entities_forget() {
  mech_objects().clear();
  freed_parts().clear();
}

void program_mech_set_invincible(void* mech, bool on) {
  if (entity_is_mech(mech)) static_cast<Mech*>(mech)->invincible = on;  // +0x630
}
void* program_mech_weapons(void* mech) { return entity_is_mech(mech) ? static_cast<Mech*>(mech)->ammo : nullptr; }
void program_weapons_set_unlimited(void* weapons, bool on) {
  if (weapons) static_cast<WeaponManager*>(weapons)->set_unlimited(on);  // 0x004b0220
}
void program_mech_set_torso_absolute(void* mech, bool on) {
  if (entity_is_mech(mech) && static_cast<Mech*>(mech)->torso) static_cast<Mech*>(mech)->torso->absolute = on;
}
void program_mech_set_throttle_returns(void* mech, bool on) {
  if (entity_is_mech(mech) && static_cast<Mech*>(mech)->controls) static_cast<Mech*>(mech)->controls->throttle_returns = on;
}
void program_mech_control_mode(void* mech, int32_t mode) {
  if (entity_is_mech(mech)) mech_control_mode(*static_cast<Mech*>(mech), static_cast<uint32_t>(mode));  // 0x0041cf30
}

void* mech_shade_of(void* mech) { return entity_is_mech(mech) ? static_cast<Mech*>(mech)->x68c : nullptr; }

const void* mech_shadow(const void* mech) {
  return entity_is_mech(mech) ? static_cast<const Mech*>(mech)->shadow : nullptr;
}

namespace {
// What 0x004814e0 reads of a zEntity entry: its vt+0x2c object's fields
// (+0xcc the caster), its node, and vt+0x50 (the mech, for its skis'
// ground; 0 for the vehicles, field bases, turrets and the pocket marker).
struct ShadowEntity {
  ai::AiTarget* fields = nullptr;
  zcls::Node* node = nullptr;
  Mech* mech = nullptr;
};
ShadowEntity shadow_entity(ai::Handle h) {
  void* o = h ? g_w.object_of(g_w.engine, h) : nullptr;
  if (Mech* m = entity_mech(h)) return {m, zn(m->node), m};
  if (is_program_vehicle(o))  // field bases too
    return {static_cast<ai::AiTarget*>(program_vehicle_target(o)), zn(program_vehicle_node(o)), nullptr};
  if (tur::Turret* t = program_turret_by_handle(h)) return {t, zn(g_w.object_of(g_w.engine, t->node)), nullptr};
  if (PocketMarker* k = program_pocket_marker(h)) return {k, zn(k->node), nullptr};
  return {};
}
// 0x005a85e0 on a node: its world position, the world matrix refreshed when stale.
const Vec3* node_world_position(zcls::Node* n) {
  if (!n || !n->data || n->cls() != 5) return nullptr;
  auto* d = static_cast<zcls::Object3d*>(n->data);
  if (d->get<uint32_t>(0) & 0x20) {
    zmth::Mat34 buf;
    zmth::push(&buf);
    zmth::load_identity();
    zcls::node_to_world(n, 1);
    zmth::pop();
  }
  return reinterpret_cast<const Vec3*>(d->floats(0x84));
}
}  // namespace

// Mech3.exe 1.2 @ 0x004814e0, on the program's entities: within 100 m of the
// player (the player's own always, 64x64; others 32x32) an entity keeps a
// caster (+0xcc), redrawn along the sun onto the ground: a mech's centre
// ski's, any other's own height. Further away it is freed.
void update_mech_shadows() {
  if (!zmod::shadows_on()) return;  // 0x00526a80
  auto* sun = static_cast<zcls::Node*>(mission_sun());
  if (!sun || !sun->data) return;
  auto* light = static_cast<zcls::Light*>(sun->data);
  Mech* player = world().mission_player;
  for (ai::Handle h : ai::entity_list()) {
    const ShadowEntity e = shadow_entity(h);
    if (!e.fields) continue;  // the stealth object (not ported)
    const Vec3* pos = node_world_position(e.node);
    if (!pos) continue;
    float fade = 1.0f;
    if (!player || e.mech != player) {
      const Vec3* p = player ? entity_position(*player) : nullptr;
      if (!p) {
        fade = 0.0f;  // no player to measure from (the exe always has one)
      } else {
        const float dx = p->x - pos->x, dy = p->y - pos->y, dz = p->z - pos->z;
        const float xx = dx * dx, yy = dy * dy, zz = dz * dz;
        const float xy = xx + yy;  // 0x0051dfa0's order
        fade = shadow_fade(xy + zz);
      }
    }
    zmod::ShadowCaster*& caster = e.fields->shadow;
    if (!(fade > 0.0f)) {
      e.fields->release_shadow();
      continue;
    }
    if (!caster) {
      const int32_t size = player && e.mech == player ? 64 : 32;
      caster = zmod::shadow_new();
      zmod::shadow_set_size(caster, size, size);
      zmod::shadow_set_node(caster, e.node);
      zmod::shadow_enable(caster, 1);
    }
    const float clouds = zmod::clouds_at(pos->x, pos->z);
    const uint8_t g = shadow_grey(*light->floats(0xa0) * clouds, *light->floats(0x9c) * clouds,
                                  *light->floats(0x88), fade);
    zmod::shadow_set_color(caster, zvid::pack_bytes(g, g, g));
    // vt+0x50: a mech's skis' +0x13c, the centre probe's ground; else the height.
    const float ground = e.mech && e.mech->skis ? e.mech->skis->ground[6] : pos->y;
    const float at[3] = {pos->x, pos->y, pos->z};
    zmod::shadow_set_position(caster, at);
    zmod::shadow_draw(caster, light->floats(0x84), ground);
    zmod::shadow_upload(caster);
  }
}

namespace {
// A zEntity entry's mech (vt+0x2c then vt+0x50), none for other entities.
Mech* entity_mech(ai::Handle h) {
  void* o = h ? g_w.object_of(g_w.engine, h) : nullptr;
  return entity_is_mech(o) ? static_cast<Mech*>(o) : nullptr;
}
}  // namespace
uint32_t mech_player_allies() { return world().mission_player ? world().mission_player->allies : 0; }
bool mech_set_mask(void* mech, uint32_t off, uint32_t v) {
  Mech* m = static_cast<Mech*>(mech);
  if (!m) return false;
  if (off == 0xc0) return m->team = v, true;
  if (off == 0xc4) return m->allies = v, true;
  if (off == 0xc8) return m->enemies = v, true;
  return false;
}
void* mech_controls_of(void* mech) { return mech ? static_cast<Mech*>(mech)->controls : nullptr; }
float mech_game_time() { return world().time; }
float mech_health(void* mech) { return static_cast<Mech*>(mech)->health(); }
float mech_speed_at(void* mech, float share) { return static_cast<Mech*>(mech)->speed_at(share); }
float mech_stopping_distance(void* mech, float speed) { return static_cast<Mech*>(mech)->stopping_distance(speed); }
float mech_stop_share(void* mech, float d) { return mech ? static_cast<Mech*>(mech)->stop_share(d) : 0.0f; }
float mech_throttle_speed(void* mech) { return mech ? static_cast<Mech*>(mech)->throttle_speed() : 0.0f; }
void mech_forward(void* mech, float out[3]) {  // any entity's +0x24
  const Vehicle* m = static_cast<Vehicle*>(mech);
  out[0] = m->forward.x, out[1] = m->forward.y, out[2] = m->forward.z;
}
int32_t mech_jump_jets(void* mech) { return static_cast<Mech*>(mech)->equipment[11]; }
float mech_vertical_speed(void* mech) { return static_cast<Vehicle*>(mech)->vec_3c.y; }
float mech_forward_speed(void* mech) { return static_cast<Vehicle*>(mech)->vec_3c.z; }
void mech_set_local_velocity(void* mech, float x, float y, float z) { static_cast<Vehicle*>(mech)->vec_3c = {x, y, z}; }
bool mech_airborne(void* mech) { return static_cast<Mech*>(mech)->airborne(); }
float mech_jet_fuel(void* mech) { return static_cast<Mech*>(mech)->jet_fuel; }
void program_mech_set_jet_fuel(void* mech, float fuel) { static_cast<Mech*>(mech)->jet_fuel = fuel; }
namespace {
void (*g_watch_shots)(const FiredShot&) = nullptr;
}
void program_watch_shots(void (*seen)(const FiredShot&)) {
  g_watch_shots = seen;
  weapon_calls().test_fired = !seen ? nullptr : +[](const Weapon& w) {  // + : a function pointer (GCC wants one type)
    FiredShot s{};
    s.owner = w.manager ? w.manager->owner : nullptr;
    s.weapon = w.def && w.def->name ? w.def->name : "?";
    s.muzzle[0] = w.muzzle.x, s.muzzle[1] = w.muzzle.y, s.muzzle[2] = w.muzzle.z;
    s.dir[0] = w.direction.x, s.dir[1] = w.direction.y, s.dir[2] = w.direction.z;
    s.target[0] = w.target.x, s.target[1] = w.target.y, s.target[2] = w.target.z;
    s.salvo_left = w.salvo_left;
    s.locked = w.locked;
    if (s.owner && entity_is_mech(s.owner))
      if (const Torso* t = static_cast<Mech*>(s.owner)->torso) {
        const Mech& m = *static_cast<Mech*>(s.owner);
        s.torso_mode = t->mode, s.torso_aim = t->aim;
        s.aim_dir[0] = t->aim_dir.x, s.aim_dir[1] = t->aim_dir.y, s.aim_dir[2] = t->aim_dir.z;
        s.facing = t->facing, s.torso_yaw = t->yaw, s.max_yaw = t->max_yaw;
        s.front[0] = t->front.x, s.front[1] = t->front.y, s.front[2] = t->front.z;
        s.mech_yaw = static_cast<float>(std::atan2(-m.transform[6], -m.transform[8]));
      }
    g_watch_shots(s);
  };
}
bool mech_lead_point_at(void* mech, uint32_t target, float out[3]) {
  Mech* m = static_cast<Mech*>(mech);
  const ai::Contact* c = m->target;
  return c && c->seen == target && lead_point(m, out);
}
void* mech_weapon_manager(void* mech) { return mech ? static_cast<Vehicle*>(mech)->ammo : nullptr; }  // a vehicle's too
int32_t wm_weapon_count(const void* wm) { return wm ? static_cast<int32_t>(static_cast<const WeaponManager*>(wm)->count) : 0; }
void* wm_weapon_node(const void* wm, int32_t i) {
  const auto* m = static_cast<const WeaponManager*>(wm);
  return m && i >= 0 && static_cast<uint32_t>(i) < m->count ? m->weapons[i]->node : nullptr;
}
const void* wm_weapon_def(const void* wm, int32_t i) {
  const auto* m = static_cast<const WeaponManager*>(wm);
  return m && i >= 0 && static_cast<uint32_t>(i) < m->count ? m->weapons[i]->def : nullptr;
}
void* wm_current_weapon(void* wm) { return wm ? static_cast<WeaponManager*>(wm)->current() : nullptr; }
// FIX (bug): the exe's firing clears a missing manager's aim through null
// (a vehicle without weapons standing down).
void wm_clear_aim(void* wm) {
  if (wm) static_cast<WeaponManager*>(wm)->set_aim(nullptr);
}
void* mech_centroid_of(void* mech) { return entity_is_mech(mech) ? static_cast<Mech*>(mech)->centroid : nullptr; }
// Mech3.exe 1.2 @ 0x004ed530, on a matrix of its own: v1.0 ran 0x004ed280 on
// the stack's top for a moved node, outside a draw its empty base (W59).
void node_world_cached(void* node, float out[3]) {
  out[0] = out[1] = out[2] = 0;
  if (auto* n = static_cast<zcls::Node*>(node); n && n->data) zcls::node_world_position(n, out);
}
bool node_intersects(void* node) { return node && (static_cast<zcls::Node*>(node)->flags() & 0x10); }  // 0x004eb710
void node_intersect_set(void* node, bool on) {
  if (node) zcls::node_set_intersect(static_cast<zcls::Node*>(node), on ? 1 : 0);
}
int32_t world_segment_first(const float from[3], const float to[3], uint32_t exclude, uint8_t* count) {
  static zcls::LineHits r;
  *count = 0;
  zcls::Node* w = world_hooks().world;
  if (!w) return 1;
  zcls::segment_options().first_hit = 1;  // 0x004ed9c0(1)
  zcls::segment_options().exclude = exclude;  // 0x004eda00, cleared by the query
  const int32_t res = zcls::segment_list(w, from, to, &r);
  zcls::segment_options().first_hit = 0;
  *count = r.count;
  return res;
}
int32_t ai_volley_pick(void* wm, const float* dist2) {
  auto* m = static_cast<WeaponManager*>(wm);
  if (!m) return 0;
  // The manager's vehicle (+0x84) is a mech (also its +0x80) or a basic
  // vehicle (a truck, an APC; +0x80 0); the brain behind either (the exe asks
  // the vehicle's vtable, ai.md 17.7).
  void* who = m->owner ? m->owner : m->vehicle;
  Mech* mech = entity_is_mech(who) ? static_cast<Mech*>(who) : nullptr;
  auto* ctl = mech ? dynamic_cast<AiControls*>(mech->controls) : nullptr;
  ai::Brain* b = ctl ? ctl->brain.get() : nullptr;
  if (!mech && is_program_vehicle(who))
    b = static_cast<ai::Brain*>(program_vehicle_brain(program_vehicle_controls(who)));
  VolleyPilot p;
  if (b) p.hold = b->params.get<float>(0x34), p.chance = b->params.get<float>(0x90);
  const float heat = mech ? mech->heat : 0.0f;
  return volley_pick(*m, b ? &p : nullptr, dist2, mech ? &heat : nullptr, [] { return int32_t{crt::msvc_rand()}; });
}
bool mech_targeting_aid(void* mech) {
  auto* m = static_cast<Mech*>(mech);
  return m && (m->equipment[14] > 0 || m->equipment[3] > 0);
}
void wm_aim_at(void* wm, const float at[3]) {
  if (!wm) return;
  auto* x = static_cast<WeaponManager*>(wm);
  x->has_target = true;
  const Vec3f p{at[0], at[1], at[2]};
  x->set_aim(&p);
}
void wm_alpha_strike(void* wm, float seconds) {
  if (wm) static_cast<WeaponManager*>(wm)->alpha_strike(seconds);
}
bool weapon_type_ready(void* w) { return w && static_cast<Weapon*>(w)->type_ready(); }
float weapon_reload(void* w) { return w ? static_cast<Weapon*>(w)->reload : 0.0f; }
int32_t weapon_ammo(void* w) { return w ? static_cast<Weapon*>(w)->ammo() : 0; }
// FIX (bug): a weapon without a definition (or a type) would be written
// through null; it holds nothing.
void weapon_hold_for_flight(void* w) {
  auto* x = static_cast<Weapon*>(w);
  if (x && x->def && x->def->type) x->def->type->hold_for_flight();
}
bool mech_active(void* mech) { return mech && static_cast<Vehicle*>(mech)->active; }
bool mech_can_face(void* mech, const float d[3]) {
  auto* m = static_cast<Mech*>(mech);
  return m && m->torso && m->can_face(Vec3{d[0], d[1], d[2]});
}
bool mech_clamp_aim(void* mech, const float d[3], float out[3]) {
  auto* m = static_cast<Mech*>(mech);
  Vec3 o{d[0], d[1], d[2]};
  const bool r = m && m->torso ? m->clamp_aim(Vec3{d[0], d[1], d[2]}, &o) : true;
  out[0] = o.x, out[1] = o.y, out[2] = o.z;
  return r;
}
bool mech_faces_within(void* mech, float cosine) {
  auto* m = static_cast<Mech*>(mech);
  return m && m->torso && m->faces_within(cosine);
}
void mech_set_torso_aim(void* mech, const float d[3]) {
  auto* m = static_cast<Mech*>(mech);
  if (!m || !m->torso) return;
  const Vec3 v{d[0], d[1], d[2]};
  m->set_torso_aim(&v);
}
void mech_aim_at_last_hit(void* mech) {
  if (mech) static_cast<Mech*>(mech)->aim_at_last_hit();
}
void mech_lead_velocity(void* mech, float out[3]) {
  Vec3 v{};
  if (auto* m = static_cast<Mech*>(mech); m && m->controls) m->lead_velocity(&v);
  out[0] = v.x, out[1] = v.y, out[2] = v.z;
}
void mech_matrix(void* mech, float m9[9]) {
  const float* t = mech ? static_cast<Vehicle*>(mech)->transform : nullptr;
  for (int i = 0; i < 9; ++i) m9[i] = t ? t[i] : (i % 4 == 0 ? 1.0f : 0.0f);
}
// A mech's vt+0xd4 (0x004aeec0) for the entity `x` going: its weapons'
// locks on it (0x004b0370), its controls' vt+0x60 (the player's 0x004bf620
// and the AI's 0x004ea700 do nothing), then AiTarget's 0x004aa4d0.
// FIX (bug): the last attacker (+0x664) naming it is cleared too: the exe
// keeps it, and a mech it hit that dies later credits a freed one
// (docs/fixes.md).
void program_mech_forget(void* mech, uint32_t self, uint32_t x) {
  auto* m = static_cast<Mech*>(mech);
  void* gone = g_w.object_of(g_w.engine, x);
  if (auto* w = static_cast<WeaponManager*>(m->ammo)) w->forget_target(gone);
  if (m->attacker && m->attacker == gone) m->attacker = nullptr;
  ai::forget_entity(self, x);
}
// For the tests: the mech's first weapon locked on `target` (its +0x04 set,
// the lock's entity as 0x004b0370 compares it).
bool program_mech_lock_on(void* mech, void* target) {
  auto* w = entity_is_mech(mech) ? static_cast<WeaponManager*>(static_cast<Mech*>(mech)->ammo) : nullptr;
  Weapon* first = w && w->count ? w->weapons[0] : nullptr;
  if (!first) return false;
  first->lock_flag = 1;
  first->lock.entity = target;
  return true;
}
// For the tests: who still holds the entity `x` (handle) or `gone` (its
// object): every listed vehicle's picks, kept contacts and contacts, the
// mechs' weapon locks and last attackers, the targets.zrd objects' contacts.
std::vector<std::string> program_holders_of(uint32_t x, const void* gone) {
  std::vector<std::string> out;
  auto on = [&](const ai::Contact* c) { return c && c->seen == x; };
  auto name = [&](uint32_t h) {
    void* o = g_w.object_of(g_w.engine, h);
    const char* n = entity_is_mech(o) ? mech_brain_name(o) : nullptr;
    return std::string(n ? n : "?");
  };
  for (const ai::Handle h : ai::entity_list()) {
    const ai::AiTarget* a = ai::sensor_hooks().fields(h);
    if (!a) continue;
    const ai::Contact* picks[] = {a->target, a->ally_target, a->enemy_target, a->saved_target, a->own_lance,
                                  a->nearest_lance, a->own_player, a->player_contact, a->touching,
                                  a->tracked[0], a->tracked[1]};
    for (const ai::Contact* c : picks)
      if (on(c)) out.push_back(name(h) + ": a pick");
    for (const ai::Contact* c : a->contacts)
      if (on(c)) out.push_back(name(h) + ": a contact");
    void* o = g_w.object_of(g_w.engine, h);
    if (!entity_is_mech(o)) continue;
    const Mech* m = static_cast<const Mech*>(o);
    if (m->attacker && m->attacker == gone) out.push_back(name(h) + ": its last attacker");
    if (const auto* w = static_cast<const WeaponManager*>(m->ammo))
      for (uint32_t i = 0; i < w->count; ++i)
        if (w->weapons[i]->lock_flag && w->weapons[i]->lock.entity == gone) out.push_back(name(h) + ": a weapon's lock");
  }
  for (const ai::CZAiTarget* t : ai::target_list())
    for (const ai::Contact* c : t->contacts)
      if (on(c)) out.push_back("a targets.zrd object: a contact");
  return out;
}
// For the tests: `mech`'s last attacker (+0x664) set.
void program_mech_set_attacker(void* mech, void* attacker) {
  if (entity_is_mech(mech)) static_cast<Mech*>(mech)->attacker = attacker;
}
// For the tests: a dead mech hidden and deleted as instant action does at 180 s (0x0041e140).
void* mech_target_contact(void* mech) { return entity_is_mech(mech) ? static_cast<Mech*>(mech)->target : nullptr; }
int32_t mech_aimed_part(void* mech) { return entity_is_mech(mech) ? static_cast<Mech*>(mech)->xf8 : -1; }
uint32_t mech_heat_state(void* mech) { return entity_is_mech(mech) ? static_cast<Mech*>(mech)->heat_state : 0; }
// Mech3.exe 1.2 @ 0x004b04c0 and its +0x28 (the exe reads a null weapon's; -1 here).
int32_t weapons_current_index(void* wm) {
  const Weapon* w = wm ? static_cast<WeaponManager*>(wm)->current() : nullptr;
  return w ? w->index : -1;
}
int32_t weapons_group(void* wm) { return wm ? static_cast<WeaponManager*>(wm)->group : -1; }
float hud_speed(void* hud) { return hud ? static_cast<Hud*>(hud)->gauges.speed : 0.0f; }
void* hud_aim_node(void* hud) { return hud ? static_cast<Hud*>(hud)->reticle.hit.node : nullptr; }
// Mech3.exe 1.2 @ 0x004464d0: a line shown, recycling (+0x111b) and of that weapon (+0x28).
bool hud_weapon_recycling(void* hud, int32_t weapon) {
  if (!hud) return false;
  for (const HudWeaponLine& l : static_cast<Hud*>(hud)->weapons.lines)
    if (!(l.flags & ztwod::kHidden) && l.x111b && l.weapon && static_cast<const Weapon*>(l.weapon)->index == weapon)
      return true;
  return false;
}

// Mech3.exe 1.2 @ 0x0047ce80 (its loop): as 0x0041e140's tally, for every
// mech still in zEntity's list.
void program_action_tally() {
  const DeadCalls& dc = dead_calls();
  Mech* player = world().mission_player;
  for (const ai::Handle h : ai::entity_list()) {
    Mech* m = entity_mech(h);
    if (!m || !m->pilot || m->pilot->roster == -1) continue;
    const int32_t kills = static_cast<int32_t>(m->x3f4[0]);
    if (!kills) continue;
    // `player &&`: the exe reads the player's +0xc4 unchecked
    const int32_t field = m == player ? 0x4c : (player && (m->team & player->allies)) ? 0x50 : 0x54;
    if (dc.ia_tally) dc.ia_tally(field, kills);
    m->x3f4[0] = 0;
  }
}
void program_mech_wreck_gone(void* mech) {
  if (entity_is_mech(mech)) dead_hide(*static_cast<Mech*>(mech), false);
}
// obj.vt+0x80 for a killer that is a mech (0x0041b420); false otherwise.
bool program_mech_kill_credit(void* killer, void* victim) {
  if (!entity_is_mech(killer)) return false;
  static_cast<Mech*>(killer)->on_kill(victim);
  return true;
}
// A mech's or a basic vehicle's zone record (+0x64); false for anything else.
bool program_entity_zones(const void* e, uint8_t out[4]) {
  if (!entity_is_mech(e) && !is_program_vehicle(e)) return false;
  const auto* v = static_cast<const Vehicle*>(e);
  out[0] = v->x64;
  std::memcpy(out + 1, v->zones, 3);
  return true;
}
void mech_target_position(void* mech, float out[3]) {
  out[0] = out[1] = out[2] = 0;
  if (auto* m = static_cast<Vehicle*>(mech); m && m->target_node)  // 0x005a8630, any entity's
    zcls::node_world_position(static_cast<zcls::Node*>(m->target_node), out);
}
// The selected group's weapon (+0x28, +0x2c[], its +0x10). FIX (bug): the
// exe's track reads it with no group selected through null; none here.
void* wm_group_weapon(void* wm) {
  auto* w = static_cast<WeaponManager*>(wm);
  if (!w || w->group < 0 || w->group >= 5 || !w->groups[w->group]) return nullptr;
  return w->groups[w->group]->current_weapon;
}
void weapon_node_position(void* w, float out[3]) {
  out[0] = out[1] = out[2] = 0;
  if (auto* x = static_cast<Weapon*>(w); x && x->node) zcls::node_world_position(static_cast<zcls::Node*>(x->node), out);
}
float weapon_speed(void* w) {
  auto* x = static_cast<Weapon*>(w);
  return x && x->def ? x->def->velocity : 0.0f;
}
uint32_t weapon_flags(void* w) {
  auto* x = static_cast<Weapon*>(w);
  return x && x->def ? x->def->flags : 0;
}
void mech_torso_centre(void* mech, int32_t how) {
  if (auto* m = static_cast<Mech*>(mech); m && m->torso) m->torso->centre(how);
}
bool mech_zones(const void* mech, uint8_t out[4], int32_t* node_zone) {
  if (!entity_is_mech(mech)) return false;
  const auto* m = static_cast<const Mech*>(mech);
  std::memcpy(out, &m->x64, 4);
  *node_zone = m->node ? zn(m->node)->get<uint8_t>(0x30) : -1;
  return true;
}
float mech_heat(const void* mech) { return entity_is_mech(mech) ? static_cast<const Mech*>(mech)->heat : 0.0f; }
bool mech_set_heat(void* mech, float heat) {
  if (!entity_is_mech(mech)) return false;
  static_cast<Mech*>(mech)->heat = heat;
  return true;
}
bool mech_hit_part(void* mech, int32_t id, float damage) {
  if (!entity_is_mech(mech)) return false;
  for (MechPart* p : static_cast<Mech*>(mech)->parts)
    if (p && p->id == id) {
      p->take_hit(nullptr, nullptr, damage, nullptr);
      return true;
    }
  return false;
}
bool mech_ceiling_hit(void* mech, void* node, float out[7]) {
  Mech* m = entity_is_mech(mech) ? static_cast<Mech*>(mech) : nullptr;
  if (!m || !m->head || !m->torso || !m->hip) return false;
  CollisionHit h{};
  h.hit.node = static_cast<zcls::Node*>(node);
  m->ceiling_response(h);
  out[0] = m->head->get_internal(0);
  for (int s = 0; s < 3; ++s) out[1 + s] = m->torso->get_internal(s);
  out[4] = m->hip->offset.y;  // the duck (0x004622a0)
  out[5] = collide_calls().hit_box && collide_calls().hit_box(node) ? 1.0f : 0.0f;
  out[6] = m->crouch_depth;
  return true;
}
bool mech_textures(void* mech, MechTextures* out) {
  Mech* m = entity_is_mech(mech) ? static_cast<Mech*>(mech) : nullptr;
  if (!m) return false;
  auto walk = [&](auto& self, zcls::Node* n, std::vector<std::string>& names) -> void {
    if (!n) return;
    if (zmod::Model* md = n->model)
      for (uint32_t k = 0; k < md->count(0x10); ++k)
        if (const zmod::Material* mat = md->polygons[k].material; mat && (mat->flags & 1) && mat->texture) {
          names.push_back(mat->texture->name);
          out->loaded += mat->texture->image != nullptr;
        }
    for (int32_t k = 0; k < n->child_count(); ++k) self(self, n->children[k], names);
  };
  walk(walk, static_cast<zcls::Node*>(m->model_node), out->names);
  walk(walk, static_cast<zcls::Node*>(m->lod_node), out->lod);
  if (const auto* s = static_cast<const Skin*>(m->decals)) out->skin = s->name ? s->name : "";
  return true;
}
bool mech_marker_heights(void* mech, float out[4]) {
  Mech* m = entity_is_mech(mech) ? static_cast<Mech*>(mech) : nullptr;
  if (!m || !m->node || !m->target_node || !m->centroid) return false;
  float p[3];
  zcls::node_world_position(static_cast<zcls::Node*>(m->node), p), out[0] = p[1];
  zcls::node_world_position(static_cast<zcls::Node*>(m->target_node), p), out[1] = p[1];
  zcls::node_world_position(static_cast<zcls::Node*>(m->centroid), p), out[2] = p[1];
  out[3] = m->height;
  return true;
}
int32_t mech_controls_view(void* mech) {
  Mech* m = entity_is_mech(mech) ? static_cast<Mech*>(mech) : nullptr;
  return m && m->controls ? m->controls->view : -1;
}
bool mech_hold_throttle(void* mech, float v) {
  Mech* m = entity_is_mech(mech) ? static_cast<Mech*>(mech) : nullptr;
  if (!m || !m->controls) return false;
  m->controls->throttle_target = v;
  m->controls->throttle_held = true;
  return true;
}
float mech_throttle_target(void* mech) {
  Mech* m = entity_is_mech(mech) ? static_cast<Mech*>(mech) : nullptr;
  return m && m->controls ? m->controls->throttle_target : 0.0f;
}
void* mech_node_of(void* mech) { return entity_is_mech(mech) ? static_cast<Mech*>(mech)->node : nullptr; }
void* mech_cockpit_node(void* mech) {
  Mech* m = entity_is_mech(mech) ? static_cast<Mech*>(mech) : nullptr;
  return m && m->pilot && m->pilot->cockpit_model ? m->pilot->cockpit_model->node : nullptr;
}
void* mech_arm_node(void* mech, int side) {
  Mech* m = entity_is_mech(mech) ? static_cast<Mech*>(mech) : nullptr;
  return m && m->arms[side & 1] ? m->arms[side & 1]->node : nullptr;
}
bool mech_powered_down(void* mech) { return static_cast<Mech*>(mech)->is_powered_down(); }
ai::AiTarget* mech_ai_target(void* mech) { return static_cast<Mech*>(mech); }
MechControls* mech_controls(void* mech) { return mech ? static_cast<Mech*>(mech)->controls : nullptr; }
bool mech_ammo_value(void* mech, float* out) {
  const auto* m = static_cast<const WeaponManager*>(static_cast<Vehicle*>(mech)->ammo);
  if (!m) return false;
  *out = m->ammo_value_inv != 0 ? m->ammo_value * m->ammo_value_inv : 1.0f;
  return true;
}
const float* vehicle_position(void* vehicle) {
  const Vec3* p = vehicle ? entity_position(*static_cast<Vehicle*>(vehicle)) : nullptr;
  return p ? &p->x : nullptr;
}
bool mech_world_position(void* mech, float out[3]) {
  const Vec3* p = entity_position(*static_cast<Vehicle*>(mech));  // a vehicle's too
  out[0] = p ? p->x : 0, out[1] = p ? p->y : 0, out[2] = p ? p->z : 0;
  return p != nullptr;
}
// The missions object's +0x4c (0x0048f470) is [0x0060991c]: the copies the ports keep.
void mech_set_game_time(float t) { world().time = weapon_world().time = t; }
bool mech_get_mask(const void* mech, uint32_t off, uint32_t* v) {
  const Mech* m = static_cast<const Mech*>(mech);
  if (!m) return false;
  if (off == 0xc0) return *v = m->team, true;
  if (off == 0xc4) return *v = m->allies, true;
  if (off == 0xc8) return *v = m->enemies, true;
  return false;
}
const void* mech_definition(void* mech) { return mech ? mech_dfn(static_cast<Mech*>(mech)->pilot->roster) : nullptr; }

uint32_t debug_slot_config(int32_t mech, void* e, uint32_t (*handle_of)(void* e, const void* p)) {
  const MechDfn* d = mech_dfn(mech);
  if (!d || !d->configs || d->configs->configs.empty()) return 0;
  return handle_of(e, d->configs->configs[0]);
}

// Ours (the campaign chain's kills): the weapon types a mech or a basic
// vehicle carries, each weapon manager slot that holds one, as its salvage
// (0x00472f00, 0x00472ec0) reads them.
std::vector<int32_t> salvage_weapon_types(void* o) {
  SalvageMech s;
  if (entity_is_mech(o)) s = salvage_view(*static_cast<Mech*>(o));
  else if (is_program_vehicle(o)) salvage_weapons(mech_weapon_manager(o), s);
  std::vector<int32_t> out;
  for (const SalvageWeapon& w : s.weapons)
    if (w.present) out.push_back(w.type);
  return out;
}

}  // namespace mech3
