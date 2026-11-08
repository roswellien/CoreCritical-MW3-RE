#include "mech3/mech_lib.h"

#include <cstdio>
#include <memory>

#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zsound/zsnd.h"
#include "mech3/mech_names.h"
#include "mech3/content_registry.h"
#include "mech3/mech_reader.h"

namespace mech3 {

namespace {
MechLibHooks g_default;
MechLibHooks* g_hooks = &g_default;
std::unique_ptr<zcls::MechLib> g_lib;
Handle g_equipment[16] = {};  // 0x00608fc0
Handle g_weapons[15] = {};    // 0x00609400

const char* const kEquipment[] = {"snd_equip_ams",   "snd_equip_bap",  "snd_equip_case",  "snd_equip_c3",
                                  "snd_equip_ecm",   "snd_equip_engine", "snd_equip_fcs", "snd_equip_gyro",
                                  "snd_equip_masc",  "snd_equip_sink", "snd_equip_dsink", "snd_equip_jjets",
                                  "snd_equip_sensor", "snd_equip_tag",  "snd_equip_tc"};
const char* const kWeapons[] = {"snd_wep_ac",     "snd_wep_erlaser", "snd_wep_erppc", "snd_wep_gaussrifle",
                                "snd_wep_flamer", "snd_wep_laser",   "snd_wep_lrm",   "snd_wep_mgun",
                                "snd_wep_narc",   "snd_wep_ppc",     "snd_wep_pulselaser", "snd_wep_srm",
                                "snd_wep_streak", "snd_equip_tag"};

// 0x00484760 and 0x004847e0's shared part: group 3 while the tree loads.
zcls::Node* load_in_group_3(const char* name) {
  zimg::TextureTable& t = zimg::textures();
  const int32_t group = t.group;  // 0x00518f60
  t.group = 3;                    // 0x00518f50
  zcls::Node* root = load_tree(name);
  if (root) g_hooks->load_textures(3);
  t.group = group;
  return root;
}
}  // namespace

Handle MechLibHooks::sound_find(const char* name) { return zsnd::find(name); }
void MechLibHooks::load_textures(int32_t group) { zimg::load_group(group); }
void set_mechlib_hooks(MechLibHooks* h) { g_hooks = h ? h : &g_default; }

zcls::MechLib* mechlib() { return g_lib.get(); }

// Mech3.exe 1.2 @ 0x00484270. The voices are looked up in table order, each
// table's slot 0 left empty.
bool open_mechlib() {
  if (!g_lib) {
    g_lib = std::make_unique<zcls::MechLib>();  // 0x004fc5d0
    zutl_zar::init(g_lib->archive);
  }
  const bool ok = zcls::mechlib_open(*g_lib, "zbd\\mechlib.zbd");
  g_equipment[0] = nullptr;
  for (int i = 0; i < 15; ++i) g_equipment[i + 1] = g_hooks->sound_find(kEquipment[i]);
  g_weapons[0] = nullptr;
  for (int i = 0; i < 14; ++i) g_weapons[i + 1] = g_hooks->sound_find(kWeapons[i]);
  return ok;
}

Handle equipment_voice(int32_t i) { return i >= 0 && i < 16 ? g_equipment[i] : nullptr; }
Handle weapon_voice(int32_t i) { return i >= 0 && i < 15 ? g_weapons[i] : nullptr; }

// Mech3.exe 1.2 @ 0x00484740
zcls::Node* load_tree(const char* name) { return g_lib ? zcls::mechlib_load(*g_lib, name) : nullptr; }

// Mech3.exe 1.2 @ 0x00484760. The name goes through a shared buffer
// (0x00609440) in the exe.
zcls::Node* mech_model(int32_t roster, int32_t variant) {
  char name[0x40];
  if (variant == 0) std::snprintf(name, sizeof name, "mech_%s.flt", chassis_model_name(roster));
  else std::snprintf(name, sizeof name, "mech_%s_%d.flt", chassis_model_name(roster), variant);
  return load_in_group_3(name);
}

// Mech3.exe 1.2 @ 0x004847e0. Its own buffer (0x00609000).
zcls::Node* hud_model(int32_t roster) {
  char name[0x40];
  std::snprintf(name, sizeof name, "mech_%s_hud.flt", chassis_model_name(roster));
  return load_in_group_3(name);
}

// Mech3.exe 1.2 @ 0x004844b0
bool close_mechlib() {
  if (g_lib) {
    zcls::mechlib_close(*g_lib);
    zutl_zar::destroy(g_lib->archive);  // 0x00484540
    g_lib.reset();
  }
  free_dfns();
  for (Handle& v : g_equipment) v = nullptr;
  for (Handle& v : g_weapons) v = nullptr;
  return true;
}

}  // namespace mech3
