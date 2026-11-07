#pragma once
#include <cstdint>

// Mech, part, weapon and equipment names and ids (docs/mechdef-parts.md,
// docs/mechdef-configs.md). Original file unknown: between mech_waynet.cpp and
// mission.cpp (the engine and armor helpers sit elsewhere, noted on each).
namespace mech3 {

int32_t part_id(const char* name);   // 0x00484e40, -1 when unknown
const char* part_name(int32_t id);   // 0x00484cc0, "none" when unknown
// 0x00484d70: a part's name for the player, Mech3Msg.dll's for 1..8 (by
// `message`), the exe's English for 9..15 (hands, calves, feet, hip), else "none".
const char* part_display_name(int32_t id, const char* (*message)(void* ctx, int32_t), void* ctx);

enum Tech : int32_t { kClan = 1, kInnerSphere = 2, kAnyTech = 3 };
int32_t mech_tech(int32_t mech);           // 0x00484720
const char* mech_name(int32_t mech);       // 0x00484650, "notmech" when out of range
int32_t mech_index(const char* name);      // 0x004845f0, any case; -1 when unknown
int32_t chassis_count();
int32_t equipment_count();
int32_t equipment_family(int32_t item);
int32_t tech_id(const char* name);         // 0x004852b0
int32_t equip_kind(const char* name);      // 0x004849a0, -1 when unknown
int32_t weapon_index(const char* name);    // 0x004848a0, -1 when unknown
int32_t engine_index(int32_t rating);      // 0x00432e30
int32_t armor_points(int32_t tech, bool ferro, float tons);  // 0x00424760
bool equipment_restricted(int32_t mech, int32_t kind, int32_t tech);  // 0x004d6dc0
const char* weapon_name(int32_t weapon);   // 0x00484900, "noweapon" when out of range
int32_t weapon_count();                    // 37 in the exe, 38 with the plain PPC (FIX (bug))
bool weapon_uses_ammo(int32_t weapon);     // 0x00484960, the table's byte at 0x005e6504
bool weapon_has_tech(int32_t weapon, int32_t tech);  // 0x00484940, the table's mask at 0x005e6500
bool equip_has_tech(int32_t kind, int32_t tech);    // 0x00484a00, the table's mask at 0x005e6878
// The Mech3Msg.dll ids of the shown names (the tables' first words): a mech's
// 0x00484670 (0x005e6098), a weapon's 0x00484920 (0x005e64f4), equipment's
// 0x00484980 (0x005e6874); -1 out of range ("notmech", "noweapon", "none").
int32_t mech_name_message(int32_t mech);
int32_t weapon_name_message(int32_t weapon);
int32_t equip_name_message(int32_t kind);
float max_mass(int32_t mech);              // 0x004846e0, 10 when out of range
int32_t weight_class(int32_t mech);        // 0x00484700: 0..3 (the roster's +0x2c), 0 when out of range
// 0x00485400 / 0x00485430 / 0x00485460 / 0x00485490: the internal structure
// points by tonnage (0x005e6998), column 1 centre torso, 2 side torso, 3 arm, 4 leg.
int32_t internal_points(int32_t mech, int32_t column);
float equip_weight(int32_t kind, int32_t tech);  // 0x00484aa0
int32_t equip_slots(int32_t kind, int32_t tech); // 0x00484a20

// The engine table 0x005e2f50 (79 entries: rating, name id, tons), through
// a config's engine (+0x24 index, +0x28 xl).
struct Engine {
  int32_t index;
  uint8_t xl;
};
float engine_weight(const Engine& e);      // 0x00432f40, XL: half the tons rounded up
double engine_weight_x87(const Engine& e); // the same, as left on the x87 stack
int32_t engine_rating(const Engine& e);    // 0x00432fa0
double engine_weight_of(int32_t index, uint8_t xl);  // the same two on plain numbers
int32_t engine_rating_of(int32_t index);

// A weapon's mech-lab block: by default read from the loaded weapon
// definitions (WeaponType, mech3/weapon.h); a test can set its own lookup.
struct WeaponLab {
  float weight[2];                 // +0x1c Clan, +0x28 otherwise
  int32_t slots[2];                // +0x20, +0x2c
  float ammo_weight;               // +0x34
};
using WeaponLabFn = const WeaponLab* (*)(const char* name);  // 0x00581ed0 then +0x1f0; 0 when none
void set_weapon_lab(WeaponLabFn fn);
float weapon_weight(int32_t weapon, int32_t tech);           // 0x00484b70
float ammo_weight(int32_t weapon);                           // 0x00484bc0
int32_t weapon_slots(int32_t weapon, int32_t tech, bool none);  // 0x00484c00

}  // namespace mech3
