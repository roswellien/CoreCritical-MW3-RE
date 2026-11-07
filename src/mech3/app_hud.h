#pragma once
#include <string>
#include <vector>
#include <cstdint>

// The program's HUD (our addition; docs/hud.md section 10): the player's HUD
// made on its mech data when first asked, the reticle placed each frame from
// the player's look and the weapons aimed at its world point, on the ported
// engine (zClass's segment query, the camera, zMath's unproject).
namespace mech3 {

void wire_hud();                  // the hooks onto the ports; called with the mech frame's
void unwire_hud();
void program_radar_light(void* mech);  // 0x0041ded0, a mech's vt+0xb8: the HUD's passive light
void* program_hud_node_object(void* node);  // for the tests: the HUD's reticle pick (0x0040e920 through its hooks)
void program_hud_update(float dt);
void program_hud_weapons_layout(void* hud);  // 0x00446240 on its +0x6520
void* program_pilot_hud(void* mech_data);        // 0x00430aa0 on a MechData (null: null)
// For the tests: a mech's cockpit HUD on (its layer's +0x04) and its reticle
// shown (+0x144c not hidden); false without a HUD.
bool program_hud_power_state(void* mech, bool* on, bool* reticle_shown);
void* program_mech_hud(void* mech);              // 0x00430aa0 on the mech's +0x684 (null: null)
void program_hud_objectives_fill(void* hud);      // 0x00446ad0 on its +0x17964
void program_hud_static(void* hud, float dt);    // 0x0043cd20 on a HUD
void program_hud_zoom_pass(void* hud, float dt); // +0x144c vt+0x28 (0x00444230) on a HUD
bool program_intensity_flag();                   // GameIntensity's flag [0x00608e4c] (options)  // 0x0040fac0's first call: the view target's 0x0041cd40
bool program_cheated();                          // [0x00609994]: a cheat used this mission (options().cheated)
// For the tests: the view target's reticle (x, y), its world point and
// whether the ray hit something; false without a HUD.
// Tests only: the player's HUD as it stands (hud.md sections 12, 13): the
// manager's weapons, the shown weapon lines, those named by their weapon's
// DESC, the bracket shown, the lock state.
struct HudReport {
  int weapons = 0, lines = 0, named = 0, bracketed = 0, lock = 0;
  int mfd_armor_most = 0, mfd_armor_w = 0;  // the MFD's armor display (mode 3), laid out for the mech
  int mfd_bar_full[11] = {}, mfd_bar_height[11] = {};  // each bar's filled height and its whole (W35)
  float lock_timer = 0.0f;
  int target_hidden = 0, shape_hidden = 0, rx = 0, ry = 0, apex_y = 0, base_y = 0;  // TARGET and the reticle
  float shape_x = 0.0f;
  // The radar: shown, its radius and centre, the blips drawn since the last
  // report and the last one's place, the sensor range.
  int radar_shown = 0, radar_r = 0, radar_x = 0, radar_y = 0, blips = 0, blip_x = 0, blip_y = 0;
  int radar_target_x = 0, radar_target_y = 0, radar_target_draws = 0;  // the selected target's triangle
  int markers = 0;  // the waypoint marker's draws (its icon, not a contact's blip)
  // The other classes' blips drawn (blips counts the mechs'), by their images:
  // the basic vehicles' 0x004ac3d0, the turrets' and targets.zrd's markers.
  int vehicle_blips = 0, turret_blips = 0, target_blips = 0;
  int field_base_blips = 0, field_base_other_blips = 0;  // of the vehicles' blips, a field base's as an ally, else
  int field_base_on_radar = 0;                           // the player's contacts on the radar that are field bases
  int contacts = 0, on_radar = 0, vehicles = 0;  // the player's contacts, those on the radar, with a vehicle
  // The damage displays: the parts not showing intact, TARGET's and the MFD's.
  int target_damaged = 0, mfd_damaged = 0, target_mode = 0;
  float target_internal = 1.0f;  // the lowest internal of TARGET's mech's parts (2: no mech)
  // The gauges and lists (hud.md section 17): the equipment lines shown and the
  // count the mech's parts call for (up to 16), the speed shown (km/h), the
  // throttle bar's top above its bottom, the fuel gauge shown, the heat bar's
  // unlit height.
  int equip_lines = 0, equip_wanted = 0, throttle_up = 0, fuel_shown = 0, heat_unlit = 0;
  float speed_kmh = 0.0f;
  float sensor_range = 0.0f;
  // The HUD keys' state: the zoom window shown and locked, the radar large,
  // over the map and amber, the MFD's and TARGET's modes, the objectives
  // shown, the HUD on.
  int zoom_shown = 0, zoom_locked = 0, radar_large = 0, radar_map = 0, radar_amber = 0;
  int mfd_mode = 0, target_mode_set = 0, objectives_shown = 0, hud_on = 0;
  // The weapon lines' bars (playtest #8): the lowest value shown, the lines
  // below full, the lowest recycle fraction of the manager's weapons.
  float bar_min = 1.0f, recycle_min = 1.0f;
  int bars_partial = 0;
  // The static (hud.md section 21): its four levels, the fills since the last report.
  float statics[4] = {};
  int static_fills = 0;
  // The 3D views (hud-plan item 6c1): the camera and window nodes made, the
  // views drawn and the depth fills since the last report, the MFD's mode.
  int view_cameras = 0, view_windows = 0, views_drawn = 0, depth_fills = 0, mfd_shown = 0;
  // A node drawn alone in a view (0x004428b0: the MFD's mech, TARGET's
  // target) since the last report; the MFD camera's place (0x00442900) from
  // its mech: across the ground, above its place, and along its front.
  int node_draws = 0, target_shown = 0;
  // TARGET's pulse (+0x240c) and the target sounds asked for since the start.
  float target_pulse = 0.0f;
  int target_sounds = 0;
  // The radar's boundary lines (0x0043d920): each outline's corners, its
  // colour, the points its polyline holds now and whether it shows.
  int boundary_points[2] = {}, boundary_drawn[2] = {}, boundary_shown[2] = {};
  uint32_t boundary_color[2] = {};
  // The mission's map (hud-plan items 7-8): the one the radar shows, its
  // image's size and the mech's pixel on it (0x004586e0; -1 off it).
  char map_name[32] = {};
  int map_w = 0, map_h = 0, map_px[2] = {-1, -1};
  float mfd_cam_dist = 0, mfd_cam_height = 0, mfd_cam_front = 0;
  // TARGET's camera (0x00440b00) on a mech target: its height and the
  // target's centroid's over that mech's place (W7: the camera at its feet).
  float target_cam_height = 0, target_centroid_height = 0;
  // The zoom window (6c2): shown, round, its mask fans since the last report,
  // its camera's place against the player camera's, its text.
  int zoom_shown_now = 0, zoom_round = 0, mask_fans = 0;
  float zoom_cam_off = -1.0f;  // the distance between the two places
  char zoom_text[64] = {};
};
bool program_hud_report(HudReport* r);
// Tests only: the view target's objectives box (0x00446ad0's lines): each
// line's text, the scores column's, whether it is shown, the panel's height.
struct HudObjectivesReport {
  std::vector<std::string> lines, scores;
  bool shown = false;
  int32_t panel_height = 0, line_height = 0;
};
bool program_hud_objectives(HudObjectivesReport* r);
// For checking: F12 on the view target's HUD (0x0041be80's case 0x43).
void program_hud_objectives_toggle();
// Tests only: Shift+R on it (0xa5, 0x0043dc10).
void program_hud_map_toggle();
// Tests only: the view target's HUD's zoom window locked open (keypad 0, 0x00442e00).
void program_hud_zoom_show();
// Tests only: the view target's HUD's MFD set to mode `m` (vt+0x7c).
void program_hud_mfd_mode(int m);
// Tests only: a mech's torso's internal structure, all three sections.
void program_set_torso_internal(void* mech, float v);
// Tests only: the mech sounds of `id` played from now on (both the cockpit's
// 0x00432990 paths) counted into `*count`; (0, null) puts the hooks back.
void program_count_sounds(int id, int* count);
// Tests only: the player's target set to its contact on `mech` (TARGET given
// it, as the targeting keys would) and TARGET's mode set; false without one.
bool program_hud_target(void* mech, int mode);
bool program_hud_target_other(const void* object, int mode);  // tests: on a turret or targets.zrd object
// Tests only: the first group that locks selected and the player's target
// set to its contact on `mech`; the group, -1 for none.
int program_hud_lock_on(void* mech, bool* target_set);
// Tests only: the reticle counted over TARGET's box, so the lock can run
// before TARGET's tracking (0x00440b00, 0x00440a30) is ported.
void program_hud_force_over(bool on);
// An object's ZEntity +0x08 byte (its active flag) by its class, the node's
// owner (entity_bind.cpp): a mech's or vehicle's, a turret's, a targets.zrd
// object's or trap's; null for another object.
uint8_t* entity_active_flag(void* object);
// Tests only: the player's reticle's hit set to `node` at `distance` and its
// image worked out (0x00443d20, the lock reset first): the state, 0 off,
// 1 on, 2 over (yellow), 3 can't; -1 without a HUD.
int program_hud_aim_at(void* node, float distance);
int program_hud_pick_at(const float at[3], void** node, float* distance, float* range, float* far_clip);  // for the tests: 0x00444350 there
void program_hud_draw3d(float dt);   // the view target's vt+0xe4, 0x0041cd90: the HUD's Direct3D pass
void program_hud_data(float dt);     // 0x0040f8d0's HUD part: the view target's HUD data update (0x0043c390)
void program_hud_target_set(void* mech);  // 0x0041d890's HUD part: the view mech's TARGET on for its new target
namespace ai { struct AiTarget; }
void program_hud_target_set_by(const ai::AiTarget* t);  // the same, when t is the view's mech
bool program_hud_aim(int32_t reticle[2], float point[3], bool* hit);
// For the tests: the player's controls' look (-1..1 of the display's half
// size; PlayerControls +0x140), as the mouse would set it.
bool program_hud_look(float x, float y);

}  // namespace mech3
