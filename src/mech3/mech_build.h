#pragma once
#include <cstdint>

#include "gamez/zgame/unported.h"
#include "mech3/mech.h"

// Building a mech (docs/mech-build.md): mech.cpp's constructor chain from
// 0x00414150 / 0x004140c0 down (the bases, the collider, the weapon manager,
// the model and parts, the motions and skis, the configuration), with the
// parts' constructors (mech_parts.cpp), the skis' (mech_skis.cpp) and the
// mech data's model copies (mech_data.cpp). Every call into a module that is
// not part of the mech is a MechBuildHooks method named by the original's
// address; ported code the build reaches (the parts' setups, the collider's,
// the weapons, the gaits) goes through a hook whose default runs the port, so
// the tests can stub it as they stub the original.
namespace mech3 {

struct Config;
struct MechDfn;
struct MechControls;
struct PartSlot;
struct PlayerControls;
struct WeaponManager;
class Skis;

class MechBuildHooks : public zgame::Unported {
 public:
  static uint32_t low(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
  static Handle h(uint32_t v) { return reinterpret_cast<Handle>(static_cast<uintptr_t>(v)); }
  // How an object goes in a call's arguments: the tests give the port's
  // objects the addresses the original's had.
  virtual uint32_t ref(const void* p) { return low(p); }
  // The original's operator new and calloc for an object the port owns
  // (silent here; the tests check the sizes and their order).
  virtual void on_new(uint32_t size, const void* p) { (void)size, (void)p; }
  virtual void on_calloc(uint32_t n, uint32_t size, const void* p) { (void)n, (void)size, (void)p; }
  // A block for an object that is not ported (its constructor is a hook).
  virtual Handle alloc(uint32_t size) { return h(unported("new", {size})); }

  // The mech data (mech_data.cpp) and definitions (mech_reader.cpp).
  virtual MechData* mech_data(int32_t index, int32_t create) {
    unported("0x00430d10", {index, create});
    return nullptr;
  }
  virtual const MechDfn* dfn(int32_t roster);  // 0x004846a0: mech3::mech_dfn

  // zClass: the scene nodes.
  virtual Handle node_find(Handle root, const char* name) { return h(unported("0x004f9d60", {low(root), name})); }
  virtual Handle node_clone(Handle node) { return h(unported("0x004f9af0", {low(node), 1, 1})); }
  virtual void node_content_chassis(Handle node, const char* id);
  virtual void node_set_action(Handle node, uint32_t fn) { unported("0x004eb450", {low(node), fn}); }
  virtual void node_set_flag8(Handle node, int32_t on) { unported("0x004eb620", {low(node), on}); }
  virtual void node_set_active(Handle node, int32_t on) { unported("0x004eb170", {low(node), on}); }
  virtual void node_set_name(Handle node, const char* name) { unported("0x004eb300", {low(node), name}); }
  virtual void node_add_child(Handle parent, Handle child) { unported("0x004eb9b0", {low(parent), low(child)}); }
  virtual void node_remove_child(Handle parent, Handle child) { unported("0x004ebb40", {low(parent), low(child)}); }
  virtual void node_free(Handle node) { unported("0x004f9050", {low(node)}); }
  virtual void node_tree_update(Handle node) { unported("0x004f7db0", {low(node)}); }
  virtual void nodes_pending() { unported("0x004f7e50", {}); }
  // 0x004ebd50: the node's box (min xyz, max xyz); 0 when it has one.
  virtual int32_t node_box(Handle node, float box[6]);
  virtual void node_set_translation(Handle node, float x, float y, float z) {
    unported("0x004fc0b0", {low(node), zgame::bits(x), zgame::bits(y), zgame::bits(z)});
  }
  virtual void node_get_translation(Handle node, float* x, float* y, float* z);  // 0x004fc020
  virtual Handle object3d_create() { return h(unported("0x004fb740", {})); }
  virtual void node_set_model(Handle node, Handle model) { unported("0x004eb370", {low(node), low(model)}); }
  virtual Handle model_new() { return h(unported("0x00538fe0", {})); }
  virtual void model_set_priority(Handle model, int32_t p) { unported("0x0053bb20", {low(model), p}); }
  // 0x004776d0: a polygon of n vertices (x y z each) into the model.
  virtual void model_add_polygon(Handle model, int32_t n, const float* v);
  // Silent: the nodes' fields (zClass Node: +0 name, +0x28 the part id,
  // +0x34 class, +0x38 data, +0x3c model, +0x40 owner, +0x54/+0x58
  // parents, +0x5c/+0x60 children, +0x78 the box's min y).
  virtual const char* node_name(Handle node);
  virtual int32_t node_class(Handle node);
  virtual Handle node_data(Handle node);
  virtual Handle node_model(Handle node);
  virtual int32_t node_child_count(Handle node);
  virtual Handle node_child(Handle node, int32_t i);
  virtual int32_t node_parent_count(Handle node);
  virtual Handle node_parent(Handle node, int32_t i);
  virtual float node_min_y(Handle node);
  virtual int32_t node_id(Handle node);  // +0x28
  virtual void node_set_owner(Handle node, int32_t id, void* owner);  // +0x28, +0x40
  virtual void* node_owner(Handle node);  // 0x0040ea10: +0x40 when the id's low byte is set
  virtual float peek_float(const float* p) { return *p; }  // a float in a node's data

  // zEntity and the AI's entity code.
  virtual void entity_set_node(Mech& m, Handle node, const char* name);  // 0x004d6c90(node, name, 0, 0)
  virtual void entity_register(Mech& m) { unported("0x005a7f50", {ref(&m)}); }  // zEntity's list 0x00736558
  virtual void entity_update(Mech& m) { unported("0x005a8500", {ref(&m)}); }
  virtual void touch_handler(Mech& m, Handle node) { unported("0x005833b0", {ref(&m), low(node), 1, 0x004161a0}); }
  virtual Handle x68c_init(Handle mem, Mech& m) {   // 0x004e61e0(mech, &mech+0x380)
    return h(unported("0x004e61e0", {low(mem), ref(&m), ref(&m) + 0x380}));
  }
  virtual void x68c_unlink(Handle x68c) { unported("0x004e67c0", {low(x68c)}); }
  virtual Handle node_ref_init(Handle mem) { return h(unported("0x004e6250", {low(mem)})); }
  virtual void node_ref_set(Handle ref, Handle node) { unported("0x004e66a0", {low(ref), low(node)}); }
  virtual void controls_set_owner(MechControls* c, Mech& m);  // IControls vt+0x08 (0x00428750 the player's)
  virtual uint32_t controls_ref(MechControls* c) { return ref(c); }  // its IControls base

  // The mech's look (skins.cpp) and motions (mech_data.cpp, mech_anim.cpp).
  virtual Handle skins_decals(Handle skins, Handle node);      // 0x00475a80(0, node): SkinSet::mech_skin
  virtual uint8_t skins_apply(Handle skins, Mech& m, Handle skin);  // 0x004757f0: SkinSet::set_mech_skin
  virtual MechAnim* gait_copy(MechData& p, int32_t i);       // 0x00431fb0
  virtual MechAction* action_copy(MechData& p, int32_t i);   // 0x00432030
  virtual MechAnim* make_blend(MechAnim* a, MechAnim* b); // 0x0041ff30

  // Ported code the build calls (the defaults run it).
  virtual void part_setup(MechPart& p);                  // vtable +0x04
  virtual void part_init(MechPart& p, Config* cfg);      // vtable +0x08 (0x0045fe00)
  virtual void collider_setup(Mech& m);                  // 0x00422360
  virtual void ski_set(Skis& s, int32_t side);           // 0x004764a0(side, 0, 0)
  virtual bool config_valid(Config* cfg);                // 0x004254d0(0)
  virtual void config_ungroup_equipment(Config* cfg);    // 0x00425960
  // 0x00414501..0x00414732, the weapons and equipment of the configuration
  // (mech3::configure_weapons); the configuration is kept at +0x3ec there.
  virtual void configure_weapons(Mech& m, Config* cfg);
  virtual void weapons_select(WeaponManager* wm, int32_t group);  // 0x004b0730

  // The player's controls (mech_control.cpp, 0x00427280) and the mech's
  // control mode (0x0041cf30): zInput, the options, the view [0x005f4c8c].
  virtual void set_command_callback(int32_t cmd);              // 0x0051d2f0 with 0x00426cb0
  virtual void controls_brain_init(PlayerControls& c) {        // 0x004d9840 on +0x5c (the AI's base)
    unported("0x004d9840", {ref(&c) + 0x5c});
  }
  virtual void controls_command(MechControls* c, int32_t cmd);  // 0x004274d0, ported (PlayerControls::command)
  virtual void view_target_command(Handle target, int32_t cmd) { unported("0x0041be80", {low(target), cmd}); }
  virtual Handle view_target(Handle view) { (void)view; return nullptr; }  // silent: its +0x64
  virtual bool joy_present();                                  // 0x0051dc10
  virtual int32_t torso_auto_return();                         // 0x0040d010
  virtual Handle view() { return nullptr; }                    // silent: [0x005f4c8c]
  virtual Handle view_camera(Handle view) { (void)view; return nullptr; }  // silent: its +0x58
  virtual uint32_t camera_mode(Handle camera) { (void)camera; return 0; }  // silent: its +0x04
  virtual void view_set_camera(Handle view, uint32_t mode) { unported("0x0040ae70", {low(view), mode}); }

  // zError's report, and the message box 0x004ac150(0, 0x1e0, message, name).
  virtual void report(int32_t level, int32_t line, const char* text) {
    unported("0x0049bfd0", {level, "C:\\zipper\\Mech3\\mech.cpp", line, text});
  }
  virtual void message_box(int32_t message, const char* name) { unported("0x004ac150", {0, 0x1e0, message, name}); }
};

void set_mech_build_hooks(MechBuildHooks* h);
MechBuildHooks& mech_build_hooks();

// mech.cpp's globals.
struct MechBuildState {
  int32_t made = 0;  // 0x00605628  mechs made: the unnamed ones are <mech><nn>
};
MechBuildState& mech_build_state();

Mech* mech_create(int32_t index, MechControls* ctl, int32_t config, const char* name);  // 0x00414150
Mech* mech_new(MechData* data, MechControls* ctl, Config* cfg, const char* name);        // 0x004140c0
int32_t mechs_alive();      // ours, for the tests: mech_new's not yet deleted
void mech_delete(Mech* m);  // vtable +0 with 1 (0x00414f20): release, then the delete
void vehicle_base(Vehicle& v);  // 0x004aeb00: the base every vehicle and mech shares
// 0x004d6c90's stand-in (the entity record is not ported): the node, its
// matrix and its two markers.
void vehicle_set_node(Vehicle& v, Handle node);
void mech_construct(Mech& m,MechData* data, MechControls* ctl, Config* cfg, const char* name);  // 0x004149f0
void mech_rebuild(Mech& m, int32_t roster, const char* name);  // 0x004148c0: made again in place
bool mech_model(Mech& m, const char* name);   // 0x00415b00
bool mech_parts(Mech& m);                     // 0x00415e40
void mech_motions(Mech& m);                   // 0x00415ca0
bool mech_configure(Mech& m, Config* cfg);    // 0x00414430
bool mech_set_skin(Mech& m, Handle skin);     // 0x00414390
void mech_control_mode(Mech& m, uint32_t mode);  // 0x0041cf30
void mech_firepoints(const MechDfn& dfn, Mech& m);  // 0x0046cb80 (mech_reader.cpp)

// The mech data's model copies (mech_data.cpp).
Handle mech_data_model(MechData& p, Handle* decals);   // 0x00430c60
Handle mech_data_lod(MechData& p, int32_t i);      // 0x00430c20
Config* mech_data_config(MechData& p, int32_t i);      // 0x00432950 (0x00432940: the definition's configurations)

// The parts' constructors (mech_parts.cpp).
void part_construct(MechPart& p, Mech& m, const PartSlot& slot);  // 0x0045fbe0
bool part_set_node(MechPart& p, Handle node);       // 0x00460190
void part_mark_nodes(MechPart& p, int32_t id, Handle node);  // 0x00460050
Hip* make_hip(Mech& m, const PartSlot& s);          // 0x004620a0
Torso* make_torso(Mech& m, const PartSlot& s);      // 0x00464000
Head* make_head(Mech& m, const PartSlot& s);        // 0x004671a0
Arm* make_arm(Mech& m, const PartSlot& arm, const PartSlot& hand);  // 0x00466280 (hand 0x00466da0)
Leg* make_leg(Mech& m, const PartSlot& thigh, const PartSlot& calf, const PartSlot& foot);  // 0x004635e0
Foot* make_foot(Mech& m, const PartSlot& s);        // 0x00462590

// The ground probes (mech_skis.cpp).
Skis* make_skis(Mech& m, float half_track, float length);  // 0x00476320
void skis_make_ski(Skis& s, int32_t side);                 // 0x00476090

}  // namespace mech3
