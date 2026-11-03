# Work log: mw3 dev

The single worker session from 2026-10-25 (Owen), formerly "mw3 mac 4".
Its earlier log, the AI items and everything up to HUD plan item 1, is
`ai.md`; read its last sections for the state at the switch. The protocol
is in `README.md`.

## Queue at the switch (2026-10-25)

- Item 19, the weather: rain (c3, c4, t1) and snow (c4b m4 / m6), the
  manager's rect and tick (0x5a6790 / 0x5a67c0, renamed weather_rect /
  weather_tick), app_mission's create_rain / create_snow / weather_add.
  Rain first, snow second if it needs another sitting.
- Then `hud-plan.md` items 6-9: 0x40fac0 as one call, the mission maps
  (maps.zrd), the radar's map mode (Shift+R), the radar's polylines.
- Later: the front-end screens (campaign, Instant Action, the mech lab,
  briefing and debriefing, salvage, movies), planned the same way.

## Item 19: the weather, rain first (2026-10-25)

- Assigned (the lead): the weather base 0x5a5470 with rain 0x5a60a0 and
  snow 0x5a5ad0 (vtables 0x5d0528 / 0x5d0660 / 0x5d05e0), the manager's
  rect and tick (0x5a6790 / 0x5a67c0 renamed weather_rect / weather_tick),
  app_mission's create_rain / create_snow / weather_add, the frame order
  and blend; split rain (c3, c4, t1) and snow (c4b m4 / m6) if large; a
  differential of the update, breaks, headless on a rain world, a
  screenshot pair. Branch mac4/weather off main (now 8181f8d).
- Traced: the weather is a zTwoD object (Object2D, 0x98 bytes) on the
  manager [0x736328] (a Layer with two rect slots +0x10 / +0x14, the text
  target +0x18 and timed texts to +0x1ec). Its own slots: +0x00 dtor,
  +0x04 draw 0x5a5360 (+0x7c once per manager rect), +0x24 update (rain
  0x5a6110, snow 0x5a5b40 with 0x5a5e10), +0x7c the particles' draw
  0x5a57d0 (a quad each through draw_textured_white, the "SnowFX" 16x8
  ramp texture, clip test 0x5a5a60). Particles live in screen space and
  are re-randomized every frame (0x5a56e0); the streak is one frame's
  motion (gravity + wind - the camera's move, in camera space).
- Queue after it (Owen, via the lead, 2026-10-25: this session is the one
  working session, the lead reviews): snow (19b if split), then HUD plan
  items 6-9 in order (0x40fac0 as one call; maps.zrd; the radar's map
  mode; the polylines), then the front-end screens.
- Done 19a (rain): `ztwod/twod_weather` (base, rain, the overlay's rect and
  tick), the program's `ProgramWeather` and `GameEngine::weather_rect` /
  `weather_tick` (the rect by its number: render, display, window). Found
  on the way: the overlay layer is active from static init (0x5a4590's
  vt+4(1)) and holds its own hidden object and five texts; the streak is
  scaled to 0.1 only when |d|^2 > 0.01 (a short move is its own streak).
  FIXes: the first frame's unset size (bug), the zeroed ramp (A), the
  slant per frame rate (B, per_second). Tests: test_weather.py 400 cases
  (2 ulps / a pixel for fsin / fcos, 9 frames in 2,581), 13/13 breaks,
  mission_c4m2_rain_made_and_drawn_headless, a screenshot pair in
  claude-files/outbox/2026-10-25 (weather_c4m2_rain_on/off.png). Docs:
  weather.md, fixes, CHANGES, discoveries, symbols, twod, world-params,
  hud-plan item 5. Next: 19b, snow.

## Item 19b: the snow (2026-10-25)

- Assigned (the lead, after reviewing 19a): the snow 0x5a5ad0 (vtable
  0x5d05e0), its update 0x5a5b40 with 0x5a5e10, create_snow in app_mission;
  stacked on mac4/weather (19a being merged).
- Done 19b: `ztwod::Snow` (0x5a5ad0, update 0x5a5b40; Ghidra's 0x5a5e10 is
  its tail, not a function), the two updates' shared start as
  `Weather::motion` / `project`, a clock each (rain [0x73653c]/[0x736528],
  snow [0x736538]/[0x736518]), create_snow through `program_weather`.
  Snow keeps its places (reset only off the view: |y| or |x| > |z|, z > 1,
  !(z >= 0.5)); flakes ftol(3.5 k) pixels thick. The per_second FIX covers
  the snow's fall too. Tests: test_weather.py 400 cases (200 snow, 5,227
  frames; 8 ulps / 1e-7 of a place / a pixel, the snow's places synced each
  frame; 69 frames tolerated), 20/20 breaks, the two headless runs sharing
  run_weather_mission, a snow pair (weather_c4bm4_snow_on/off.png).

## HUD plan item 6: 0x40fac0 as one call (2026-10-25)

- Assigned (the lead): hud-plan.md items 6-9, one at a time, each branched
  off main. Item 6 on mac4/hud-frame off cdfe0f4.
- Done: `GameEngine::hud_update` is 0x40fac0 (the view target's frame, the
  message panel's update, the pocket commander; the developer lines,
  frame-rate text and console layers left out), app_states' hud() calls it
  alone. Found: the message panel's text had never been drawn (its update,
  vt+0x24, is 0x40fac0's); the c1m1 HUD test now checks it (break caught).
  WiredLaunch's mech_screen gives `pilot_hud`; hud_reset / hud_zoom hide or
  show the objectives. Noted for later (hud-plan item 6): the radar reset
  0x43d920, and app_states' `radar` / `ai_tick` are the HUD's 0x43cd20 and
  reticle update (`Engine::mech_ai` is 0x430aa0), still logged.

## HUD plan item 6b: the HUD's static and the zoom pass (2026-10-25)

- Assigned (the lead, from item 6's finding): app_states' radar() /
  ai_tick() are the HUD's: 0x43cd20 on the pilot's HUD and the reticle's
  (+0x144c) vt+0x28. Branch mac4/hud-static off main.
- Traced: 0x43cd20 is the HUD's screen static. Four channels (+0x22e08..
  +0x22e34: three static levels with a floor and a hold, and the radar's
  fade); with the lowest internal structure of the torso's three sections
  and the head under 0.4 (and GameIntensity's flag clear) it adds to a
  random one (0x43bfe0 kind 5: 0.1 a frame up to k / 4); each shown
  channel draws static (0x43cca0 -> 0x558c10: noise-table pixels under the
  level replaced by three greys); the levels decay 0.3 a second after
  their hold. The reticle's +0x28 is 0x444230 -> the zoom window's +0x28
  0x442f50 (its 3D pass, 198 lines): split off as 6c.
- Done 6b: `Hud::static_tick` / `add_static` / the draw (0x43cd20,
  0x43bfe0, 0x43cca0), `zrndr::finish_init` / `static_fill` (0x5581c0,
  0x558c10) and `GameEngine::render_reset` (0x557b20: the noise filled at
  start-up as the exe, 0x557b30 logged). The draw paths' calls renamed
  (`pilot_hud`, `hud_static`, `hud_zoom_pass`; the exe's call shapes kept as
  defaults), the mech's +0x684 written by WiredLaunch. FIXes: the 0.1 a
  frame (B, per_second), TARGET's null rect (bug), the table's spare byte
  (A). Tests: test_hud ops 39-41 (100 HUDs, 262 fills; the exe's null read
  hooked to the port's FIX), test_static.py (pixels), breaks. Next: 6c.

## HUD plan item 6d: c1m1's emplacements (2026-10-25)

- Assigned (the lead, before 6c): c1m1's "Completed: Destroy SRM
  Emplacement" lines at 00:00:01. Branch mac4/emplacements off main 0f23363.
- Found: the turrets are built (the ported loader, 14 in c1); the
  objectives' entity_find (0x4d7330) searched only the AI mechs and the
  targets, so every turret's and vehicle's destroy objective counted as
  destroyed at once. Fixed: turrets and vehicles by node name, their +0x08
  as the flag, labels through entity_name (vt+0xa4; a turret's is none).
  The c1m1 won/lost test checks the four emplacements alive and their
  objectives open, and kills them to win (break caught). Survey of every
  mission: all found but c2m3's firefly_01 / firefly_02 (the mission makes
  firefly_03 / 04): a data typo in the original, a candidate for Owen.

## c2m3's Firefly objectives (2026-10-25)

- Assigned (the lead, Owen approved): the data typo found in 6d as a FIX
  (bug). Done: `fix_objective_data` (mech_objective) after the read, keyed
  on c2 / m3, renames the two secondary destroy objectives to firefly_03 /
  04 and finds them again; objectives_load takes the mission's directory
  (null in the probes). fixes.md's Data typos, CHANGES, objectives.md,
  discoveries; mission_c2m3_firefly_objectives_open_headless (break
  caught). Next: 6c1.

## HUD plan item 6c1: the HUD's 3D views (2026-10-25)

- Assigned (the lead, the split): the views' nodes and render first. Branch
  mac4/hud-views off main 05a3b01.
- Done: zClass window_create / set_size / set_origin (0x4f9e90, 0x4f9fa0,
  0x4fa0b0) and camera_set_flag8 (0x4f34c0); Render3D::clear_depth_rect
  (a default) and zvid::depth_fill (0x576ce0 -> 0x577770's DEPTHFILL);
  ProgramHudHooks' node table and the camera / window hooks, zview_render
  through render_world (hardware only, as 0x4b52c0), dirty_rect as the
  depth fill. Test mission_c1m1_hud_views_drawn_headless (break caught).
  Open [Owen]: the MFD's mode 1 camera (0x442900 has no caller found).

## HUD plan item 6c2: the zoom window's 3D pass (2026-10-25)

- Assigned (the lead, the split): 0x442f50 with 0x4436c0, stacked on
  mac4/hud-views. Found: the zoom's zView is its own class (vtable
  0x5c0600): its render 0x443320 pushes the fog back for the pass; the
  zoom hides the player's own mech; the round window's corners are masked
  with four clear-texture fans at depth 0.99; the program never formatted
  zTwoD message texts (FormatMessage), so the zoom's range (and any other
  set_text_id text) was empty: SdlTwodHost::format_message.
- Tests: test_hud op 42 (100 HUDs, 75 passes, 120 fans), breaks,
  mission_c1m1_hud_zoom_drawn_headless.

## Playtest #23 / #24 / #28: firing, aim, recoil (2026-10-25)

- Assigned (the lead, Owen's second playtest): some clicks fire nothing
  until clicked again (#23), a laser does not land where the reticle was at
  the click (#24), no recoil (#28: WeaponCalls::recoil never set). Each with
  a headless check that reproduces it first. Branch mac4/playtest-firing
  off main.
- #28 fixed: `c.recoil` wired in app_weapons.cpp (an Arm kicks, a Hand
  passes it to its arm, other parts none). The Bushwhacker's LRM/AC group
  went from recoil 0 to 0.43 (mission_c1m1_arm_weapon_recoils_headless).
- #23 as the original: five clicks pressed and released inside one frame
  (mouse and key) all fire once the bar is full
  (mission_c1m1_short_clicks_fire_headless); a beam fires only from a full
  charge (0x4b3fc0), so clicks while it recharges are dropped. Buffering
  them is in fixes.md's candidates.
- #24 as the original: the beam fires at exactly the aim point the click
  saw, turning or not, and a held beam follows the reticle
  (mission_c1m1_laser_{lands,turning}_on_the_reticle_headless; a sideways
  1 m aim error fails both). A trap on the way: at 80 m the reticle's ray
  passes 0.9 m beside the Owens' torso (its AI turns it about 12°) and
  hits the building behind, while the arm's beam still crosses the Owens.
  Test helpers mech_group_ammo / _ready / _muzzle, mech_arm_recoil.
## Playtest #25: LRMs lock but don't home (2026-10-25)

- Assigned (the lead): in the original, lock, aim at the sky, fire, and the
  missiles climb and come down on the target; here they fly straight.
  Branch mac4/playtest-lrm off main. Lead: the program never wires
  WeaponCalls' lock hooks (entity_target, record_target, target_position,
  target_velocity, target_mech), so Weapon::try_lock (0x4b3330) finds no
  target and the shot spawns without one.
- Fixed: app_weapons wires entity_target (+0x6c), record_target (the
  contact's object), target_mech, target_position (app_sensors
  weapon_lock_point: 0x5a8630's live pointer into the target node),
  target_velocity (weapon_lock_velocity: a mech's 0x41d480 into the new
  Mech::lock_velocity +0x64c, a vehicle's live +0x30), ecm_count, fcs_count,
  equipment_in_part, lock_notice (mech_lock_warning, 0x41abd0). The
  missiles now spawn with the lock and home (traced: target_at follows the
  Owens). LRM5's TURN_RATE 0.25 (rad/s, as the exe reads it) means 10
  degrees off at 150 m hits and 22 does not; the test fires 10 degrees off,
  and fails without the wiring (record_target null: no hit).
- Open: what drops an in-flight shot's lock when its target goes.
- Test isolation: in the full suite the LRM test's E did nothing after
  campaign_menus_to_c1m1_through_the_briefing_headless (bisected). The
  player controls ([0x6056ec]) survived that test, so 0x427280 did not
  register the command callbacks again on the new zInput map each test's
  init makes (E is a callback command; throttle and fire are polled).
  reset_process_state now clears the instance, as a new process has none.
  The program makes zInput once, so play is not affected.

## Playtest #27: shots don't destroy c1m1's missile turrets (2026-10-25)

- Assigned (lead): shots at the c1m1 missile turrets don't destroy them.
  Branch mac4/playtest-turrets, off mac4/playtest-lrm (the #25 hooks).
- Plan: a headless repro firing at a tur_* node; follow the hit from the
  segment query to Turret::hit and the deactivation at health 0.
- Not reproduced headless. Projectiles from 60 m at the centroid kill all
  four SRM emplacements in two PPC hits each (healthy off, destroyed on);
  the Mad Cat with its reticle pitched onto tur_srm04 kills it with its
  lasers in three volleys. Probes found: the node origin is the turret's
  base (shots at it hit the ground); tur_srm02 is inside rktun01, whose
  walls take shots along x; teleporting the player outside c1m1's area
  sets outside_c1m1 (lose(), the debriefing 8 s later), so probes stay in
  bounds; CountingPump's limit counts passes, not frames.
- Tests: mission_c1m1_shots_destroy_the_srm_turrets_headless,
  mission_c1m1_player_destroys_an_srm_turret_headless; break (no hit
  callback set): health stays 1.000, both fail.
- Asked the lead for Owen's case (turret, weapon, what he saw).

## Playtest #26: F12's objectives box has no text (2026-10-25)

- Assigned (lead): F12 opens the objectives box with no text; its filler
  0x446ad0 (HudObjectives, from show_for 0x446820) is unported. Branch
  mac4/playtest-objbox off main 6ba7b85.
- Ported 0x446ad0 (HudObjectives::fill) with 0x446930 (its vt+0x7c, a blank
  score per line: without it the "In Progress" column sat beside the
  header), 0x446970, 0x446a20 and 0x45e080 (the "  OP: <nav>" suffix).
  ProgramHudHooks::objectives_source gives mission_objectives(), +0x56,
  the campaign's operation and mission; nav names and keys' names too. The
  objective hooks now give the cockpit (program_mech_hud) and refill the
  box on a completion (0x45e110). Screenshot via the new dev option
  `build/mw3 --objectives N`.
- Test mission_c1m1_objectives_box_text_headless; breaks: no fill (no
  lines), no blank score (columns differ). FIX (bug): the texts go in as
  "%s" arguments (fixes.md; no shipped text has a '%').

## Playtest #23/#24 reopened: the click and the beam's start (2026-10-25)

- Assigned (lead, Owen's words): a charged, ready weapon does not fire on a
  click, only when the mouse button is held; and when the laser leaves the
  arm the mech has moved on but the beam starts where the mech was at the
  click. Check the real SDL mouse path (buffered vs state, which frame the
  edge is read, a press seen only while still down), time press -> fire in
  frames, and whether the beam's origin (+0x40 muzzle) is taken at the
  press and not refreshed at the spawn. Branch mac4/playtest-click.
- Repro tools (ours): `build/mw3 --mission c1m1 --walk --fire-group G
  --click N:M` pushes real SDL mouse events (SDL_PushEvent) at frames N / M
  and logs each frame's dt, the manager's trigger and state, the beam and
  how far its origin is from the firepoint now; `--walk` presses
  THROTTLE0. Test sdl_program_mouse_click_fires_the_laser feeds SdlInput
  the same events.
- Found: no press delay. In the live program (~110 fps) every group (MGUN,
  small, medium, large lasers, LRM20) fires on the frame after the press
  event (the press frame's poll), ready or not shown at 1.00 before.
- Found: the stale beam. A short laser keeps firing after the button is let
  go until its charge drains (release 0x4b4180 stops only fire_time > 0.5),
  and its muzzle is re-aimed only on the trigger (0x4b3f40) and the held
  frames (0x4b41b0); BeamWeapon's update (0x4b4230) does not aim. Walking
  at full speed the beam's origin trails the firepoint by 0.17 m a frame
  after the release, 1.4 m by the beam's end (ERLaser_Medium, ~140 ms).
  The exe's only aim callers are 0x4b2d2d (projectile update), 0x4b36b0,
  0x4b3f6b, 0x4b41d9: the same in the original.
- Fixed (bugs): BeamWeapon::update re-aims a firing beam each frame (the
  held path's aim), so the beam leaves from the arm after a click. The SDL
  test runs both paths: fixed 0.00 m, the original 1.28 m behind; the live
  program likewise (0.00 m over frames 404..415).
- "Has to hold it to fire" not reproduced: every press fires on its frame.
  Fits the original's rules: a click before the charge is completely full
  does nothing (0x4b3fc0) and a hold fires the moment it is; in chain fire
  a click fires only the current weapon. Asked for Owen's case.

## Playtest #22: ground textures blink (2026-10-25)

- Assigned (lead): the ground's textures blink in play. Branch
  mac4/playtest-blink off main 0e5ee80.
- Tools (ours): `build/mw3 --shot F.bmp --shot-frame N --shot-series K`
  writes the K frames before N as F_<frame>.bmp; `--tap DIK@N[:M]` presses
  a key as an SDL event (X 0x2d for the outside view, Left 0xcb to turn);
  tools/analysis/blinks.py marks pixels that differ from both neighbouring
  frames while those agree.
- Not reproduced: c1m1 still (water and shore animate, a mast, birds),
  walking in the cockpit (the frame's edges bob, birds), outside still (0
  px) and turning; c1m2 still (a parked vehicle's edges flicker: its
  model's faces, not the ground); c2m1 still (0-1 px). Code: D32 float
  reversed depth from the game's sz, cycled vertex uploads, no texture
  eviction in zRender or Render3D. Questions to Owen through the lead.

## Playtest #29: the MFD's three modes, the mode-1 camera with 6c3 (2026-10-25)

- Assigned (lead): the MFD's mode-1 camera caller (MFD vt+0x8c, 0x442900)
  with hud-plan 6c3 (TARGET's object view 0x4428b0); check all three D
  modes against Owen's description (1 the own mech in 3D from the front,
  2 the parts damage diagram, 3 the armour bar graph, green, red where
  damaged), a screenshot each. Branch mac4/playtest-mfd off
  mac4/playtest-blink (its --tap).
- Found the "caller": 0x442900 is no MFD slot but the MFD view's zView's
  render: 0x442620 sets the view's +0x34 vtable to 0x5c04f8, whose +0x04 is
  0x442900 (the plain zView 0x5c9118 has 0x4b52c0). It puts the camera 13 m
  before the mech (local -z) at 1.25 x the centroid's height and draws the
  mech alone (0x4428b0). Ported in ProgramHudHooks (zview_render dispatches
  on HudMfdView; mfd_view; view_draw_node for 0x4428b0, also TARGET's 6c3).
- Screenshots: mode 1 the Mad Cat from the front, 2 the parts diagram,
  3 the armour bars; Owen's description. Pitfall: a reused --write-dir keeps
  the view option a --tap of X set (the next runs start outside).
- Found and fixed a crash: zutl::format_message_v read every %N as a string,
  so the radar's nav line 0x229 "OP: %1 [%2!d!m]" crashed with the nav point
  under 1 km. It now reads each by its !type! (msgdll_formats_each_argument_by_its_type).
- Test mission_c1m1_mfd_shows_the_mech_and_target_its_target_headless;
  break: the MFD view as a plain zView (camera at the origin, no node).

## Playtest #30 / HUD plan item 9: the radar's boundary lines (2026-10-25)

- Assigned (lead): no mission boundaries (yellow and red lines) on the
  radar; the radar's polylines (hud-plan item 9, needs zTwoD's PolyLine).
  Branch mac4/playtest-radar off main f5e2ecf.
- The polylines are the mission boundary, not the nav route: 0x43d920 (the
  mission start's, 0x491696) takes the player's settings' vlist regions
  reg_inside_ (0x4d92f0) and reg_outside_ (0x4d9360), counts their corners,
  colours them yellow (c3,ad,03) and red. The draw (0x43dd00 step 5) was
  already ported. Ported the reset (HudRadar::reset_boundary) and the
  program's hooks (radar_boundary, radar_point_count, radar_point); the
  launch's hud_43d920 finds the player's HUD (its handle is truncated).
- Survey: shipped boundaries have 6..12 corners (a polyline holds 21, the
  radar writes count + 1): no overflow. Screenshot: c1m1's radar shows both
  lines just south of the start.
- Test mission_c1m1_radar_shows_the_boundary_headless; break: no region.

## HUD plan items 7-8: the mission maps and the radar's map mode (2026-10-25)

- Assigned (lead): item 7, the mission maps (0x4583b0 and family: maps.zrd
  read at the start, freed at the end); item 8, the radar's map mode (the
  map drawn under the contacts, Shift+R). Branch mac4/hud-maps off
  mac4/playtest-radar.
- Ported the map family into mech3/mission_map (mech_map.cpp's): the map
  class 0x4580c0 (an Image), the read 0x4583b0, load/unload, find 0x458590,
  pick 0x4582a0 ("large", else the first loaded), point 0x4586e0. Wired the
  startup read and exit free (GameEngine maps_init / mech_data_shutdown),
  the cleanup's 0x4582c0, the briefing's pick into missions +0xbc (and the
  debug start's, ours), the HUD's map hooks and 0x491c80.
- Test mission_c1m1_maps_and_the_radar_map_mode_headless (image stand-ins:
  the headless zTwoD host loads none). Live: Shift+R draws c1m1_small in the
  radar's box with the mech's circle at its pixel.
- Break: the briefing's pick not stored: no map at the start (c1m1 expected).
  With Shift+R on too, that run crashes at the quit's cleanup instead:
  zsnd::snapshot calls out()->get_status for a dup voice while the headless
  test's zSound has no output (hooks().output null). Open, test-only: the
  headless runs give zSound no output; a dup appears once Shift+R plays
  over an empty mission map.
- The full suite first crashed in the new test: its image stand-ins
  answered every name while the maps' path was set (a world texture with
  no fields scrolled); now only the maps' bitmaps. And the maps' read flag
  outlived a test (0x4584e0 keeps it, as the exe); reset_process_state
  clears the maps as a new process has none.

## Front-end census and plan, docs/frontend-plan.md (2026-10-25)

- Assigned (lead): a census and plan like hud-plan.md. The campaign loop as
  a player sees it (main menu, pilot, campaign / Operation screen, briefing,
  mission, debriefing, salvage, mech lab, next mission, save and load), then
  Instant Action setup, training and the movies; per screen its object and
  state, what is ported, what runs now (live), what is missing, the size;
  a porting order in small items, the campaign loop first (c1m1 -> c1m2
  with salvage and the lab), each with a test plan. For review before item
  1. Also: the c1m2 parked vehicle's edge flicker (#22) as an open render
  note. Branch mac4/frontend-plan off main 78b1f9b.
- Wrote docs/frontend-plan.md: the loop's screens with their ports and
  sizes, IA, training, the movies, a porting order of 13 items (salvage
  first). Live through the menus with a new driver in mw3_main (ours):
  `--click-at X,Y@N` (game pixels, zInput's 0x51b440), `--tap`, `--shot`
  from the plain start (the stale "main menu not ported" line removed).
  Seen: main menu, pilot screen, c1m1's briefing (MECHLAB and SALVAGE grey
  on Op 1 Mission 1, as 0x408ef0 has it). render.md 6(c): c1m2's parked
  vehicle edge flicker, open.

## Front-end item 0: --briefing (2026-10-25)

- Branch mac4/fe-briefing off main 39e1251. `--briefing c1m2` (ours): a
  pilot "Briefing" saved with its newest campaign at the mission, then the
  driver clicks CAMPAIGN, the pilot, ACCEPT; the Campaign state goes on to
  the Load state. Two dead ends first: post_intro = Load (the briefing came
  up but the load stalled at "Loading Textures (0/17)" and the quit hung
  waiting for it: the Main state's set-up was skipped), and app.replace from
  the frame hook (queued but never run there).
- Seen: c1m2's briefing (map, route, MECHLAB and SALVAGE lit); MECHLAB opens
  the lab with an empty stockpile (a new campaign's store fill 0x470e30 /
  0x4733b0 not ported; plan item 1a added).

## Front-end item 1a: a new campaign's store (2026-10-25)

- Branch mac4/fe-store off main 724b11c. Finding: already ported
  (0x4345a0 with 0x4733b0, carried by 0x434e00); `--briefing` built its game
  at the mission (no stock, no field bases: the salvage capacity would be
  0). It now steps the pilot's new campaign with next_mission; the lab shows
  the Bushwhacker. 0x470e30 is the lab / IA / MP / training store (item 12).
- campaign_menus_to_c1m1 now checks c1m2's store: 0 mechs, 4 kind 1, 3
  ammo, 3 kind 3. Break (no new-campaign stock): a spare mech appears and
  the lab's ACCEPT checks fail. Traced (the lead asked why that reads
  backwards): with the stock the store is 1 Bushwhacker, 6463 units; each
  briefing takes the player's mech and loadout into the slot (0 mechs, 6106
  units) and 0x434e00 brings the lance back for the next mission (1, 6463)
  before c1m2's briefing takes it again. Without the stock the store is
  empty, the slot's take finds nothing (its mech -1, the player launches
  without the store's), and the Bushwhacker returned after c1m1 (1 mech,
  357 units) stays a spare. The kinds' counts matched because they count
  entries, not units (corrected).

## Front-end item 1: the salvage screen (2026-10-25)

- Branch mac4/fe-salvage2 off main 8a5c6d4 (the notes below came across
  from mac4/fe-salvage). Ported whole: `mech3/salvage_screen` (the screen,
  its lists, the two moves), the second Salvage State 0x608bd8 and
  `GameEngine::salvage_open`, construct 0x47d3b0.
- Seen live: `--briefing c1m2 --click-at 420,435@2500`: the stockpile, 6
  empty bays for 3 bases, the lance row; clicks moved 200 MG rounds, a heat
  sink, a CASE. The tonnage values overlap their captions (right-aligned
  font, the port's widths; for Owen).
- Tests: test_salvage_screen.py (new, 100/100; found the destruction order
  of BACKGROUND and SALVAGE_TEXT), test_salvage.py moves (200/200; floor,
  ftol and 0x4b4900 stood in), the campaign test's salvage stage. Breaks:
  screen 13/14, moves 4/5, headless 5/5 (the misses cannot show).

## Front-end item 1 (the salvage screen), mapped, set aside for 1a (2026-10-25)

Notes for when it resumes (branch mac4/fe-salvage):
- Open: 0x47d2f0 pushes the second Salvage State 0x608bd8 (app push
  0x590400). SalvageState (0x47d310/0x47d390) is ported; its construct
  0x47d3b0 (new 0x166a8) is the screen, vtable 0x5c3c88: +0x08 dtor
  0x47db10 (body 0x47db60), +0x14 press 0x47e490, +0x00 0x5a2bb0.
- Screen fields: +0x10fec state, +0x11368 ICON_ACCEPT (id 10), cards
  0x6a0 each (ctor 0x47db30 = LanceCard 0x47af70 with vtable 0x5c3d58,
  slot 0 0x47dcd0): 6 bays at +0x114c0, 4 lance at +0x13c80 (ids 4..7,
  ICON_PILOT, moved by (0x5e5b10 * i, 0x5e5b18), their children by the
  functor 0x47f1c0 / 0x47f140 / 0x47f1e0), PLAYER_SALVAGE +0x15700 (id 11,
  store +0x214 = 0x479430's), MISSION_SALVAGE +0x15dd0 (id 12, store the
  campaign's +0x3c, else [0x609990]), SALVAGE_TEXT +0x1655c (children at
  +0x110: [3] used tons, [4] capacity, [5] 0x163/0x164 with the bases),
  BACKGROUND +0x164a0. Lists' vtable 0x5c3ca8: +0x98 0x47da70 (a choice
  goes to the screen's +0x14 with the id +0x150 and the row).
- Constants: 0x5e5b08..18 = 120, 80, 60, -50, -70; 300 t and 2 mechs a
  field base (0x470cc0, 0x470cd0, mech_fieldbase_cut).
- 0x47e110 (player -> mission): n = 1; kind 2: 0x4b4900(+0x14, +0x04);
  kind 3 with +0x18 in {1, 2}: by count * 0.5 < 10, 20, 50, 200, 500:
  1, 2, 10, 20, 40, else 100; to.add_copy(item, n), on success
  from.remove(item, n); true.
- 0x47e270 (mission -> player): cap 300 * bases t, 2 * bases mechs; used
  = the player store's weight(-1) sum; kind 0: 1 if a bay is free (else
  the negative rest: refused); kind 2: 0x4b4900; kind 3 sub 1/2: min(count,
  ftol(2 floor(cap - used))); n > 0 and used + item.weight(n) <= cap:
  add, then remove; true.
- Refill 0x47dcf0: the lance cards from the game's slots (vt+0x34(i), vt+0x38
  the count: green 1 under it, else 3), the bays from the store's mechs
  (0x47b260, 0x484650 name) then empty ones up to 2 * bases, the lists
  refilled (vt+0x40, +0x3c, +0xa8), texts "%.1f %s" with the tons.

## Text widths (2026-10-25)

- Lead's item before lab 5b2: the tonnage overlap on the salvage screen.
  Branch mac4/fe-fonts off main 197a289.
- Found: the faces are the game's own TTFs and FreeType's MONO hinting
  gives GDI's advances (equal to Arial's and Impact's hdmx at every listed
  size); the gaps were the size for a positive (cell) height (scaled by
  FreeType's line height: Arial 12 at size 10, GDI 9 by VDMX, as Wine's
  tests record) and SDL_ttf's kerning. The salvage caption is "Current
  Tonnage:" (its colon was under the value).
- Ported GDI's rule (`frontend/sdl/gdi_font_size`, Wine's load_VDMX and
  calc_ppem_for_height), the ascent and descent from VDMX, kerning off.
  Shots before and after: the salvage panel, a briefing, the main menu,
  the HUD, the lab (scratchpad fonts/).


## Lab 5b2: the item rules (2026-10-25)

- Assignment (lead): plan item 2, 0x00453840 (may an item go on) and
  0x00454600 (put it on) with the Config helpers they ask; tested with
  test_lab_screen.py sessions with adds against the exe, breaks. Branch
  mac4/lab-5b2 off main d751ead.
- First half done: the item rule 0x00453840 (`can_xa8`), 0x00456750,
  Config's 0x00425ea0 / 0x00473570 (test_mechsave checks both, breaks
  2/2). test_lab_screen runs the exe's rule: 80/80, breaks 14 of 15 (the
  15th, armour's slot test with a part, is dead code). Harness: the float
  stub's `fld` thunk pops the argument itself (`ret 4`); the plan keeps to
  play's states (the rule reads the shown loadout unchecked).
- Second half next: the put-on 0x00454600 operates inline on the real
  loadout and store (armour by half tons and swaps, 0x004513f0 and
  0x004247a0, sinks and jump jets with messages, the firepoint pick with
  the static toggle 0x005e47d4); its own differential with exe objects.
- Second half done: the put-on 0x00454600 (`lab_put_on`, an Engine hook so
  the lab test keeps its stand-in), Config::remove 0x00471de0, find_like,
  spread_armor 0x004247a0. test_salvage.py's puton part runs the exe's
  put-on on real loadouts and stores: 200/200, breaks 12/14 (the two left
  are the remove's, unreachable from the put-on: test_mechsave covers the
  remove, 3/4, and the spread, 6/7; the misses cannot differ). Finds: the
  firepoint toggle (last, first, alternating, across mechs) and missiles
  with no scattering firepoint getting the part number as their firepoint.

## Lab 6: the criticals page (2026-10-26)

- Assignment (lead): plan item 3 (mechlab.md step 6), ASSIGNED_LIST
  (0x005c1fe0) and the part buttons; also answer whether the put-on's
  part-number firepoint can index past a mech's list. Branch mac4/lab-6 off
  mac4/lab-5b2 (5b2 under review).
- The firepoint question: no, not with shipped data (fixes.md candidate
  updated): only the Elemental has non-scattering firepoints, its arm
  parts 2 and 3 are within its 4, and the Elemental never reaches a lab.
- ASSIGNED_LIST mapped so far (vtable 0x5c1fe0, ctor 0x4552e0 = the base
  0x451b40(-1)): own slots +0x00 dtor 0x455300, +0x60 show 0x455340 (the
  base's), +0x64 hide 0x455350, +0x84 read 0x455320 (the base's), +0x90
  add_text 0x455bf0 (STATIC_FONT +0x1c4), +0x94 add_object 0x455b60
  (NORMAL_FONT when the loadout is the player's (+0x1c) and the lab not
  read-only, else STATIC), +0x98 add_part 0x455af0 (SELECTED_FONT when it
  is the selected row, else LARGE; +0x22c = the part being listed), +0x9c
  scroll 0x4559e0 (steps the chosen part through a fixed order via
  0x456130 -> 0x4560d0 its row -> +0xac), +0xa4 can_light 0x455f30 (a
  part row, or an item row on an editable lab), +0xa8 true, +0xac press
  0x455fb0 (an item: back to the store [0x605a80], off the loadout,
  recount, sound, refresh; a part: top = selected = row, refill, the part
  buttons 0x44d3e0, the 3D pick 0x4504d0), +0xb0 0x455f90 (not a part
  row), +0xb4 0x455f20, +0xb8 refill 0x4554d0.
- Refill 0x4554d0 (shown only): part = 0x455eb0 (-1: 6); clear
  (0x455d60); needs +0x220 and +0x224; +0x22c = -1; the slot's mech
  tech (0x484720); engine XL (+0x28) slots (3, 2 Clan) per side torso;
  recount; slots left; ferro (+0x34: 7 Clan / 14) and endo (+0x20: 7 Clan
  / 14) counters; then per part in order 6 head (msgs 0x24e, 0x24f x2,
  0x250), 1 CT (engine 0x432f00 name via 0x575fd0 0x251 with 6, gyro 0x252
  4), 7 LT / 8 RT (XL slots 0x253), 2 LA / 3 RA (0x255, 0x256), 4 LL / 5 RL
  (0x2d5, 0x257..0x259): add_part, add_text rows, then 0x4553f0 (the
  part's ferro / endo rows, "Endo Steel Internals", then its items via
  0x455360: an item whose vt+0x08 location(mech) is the part, its slots
  as the count, sinks of no group 0); then 0x456150(part) (selected row,
  the 3D pick 0x4504d0, the part buttons 0x44d3e0, UNASSIGNED refill),
  layout +0xb4, 0x455c50 (the part rows' slot bars 0x451870 and colour),
  each item row's mass "%.1f" (sprintf 0x5e406c) and count, their fonts
  COUNT when read-only else MASS.
- Done: ASSIGNED_LIST (`LabAssignedList`, every own slot and helper),
  LabPart's press 0x44f340, check_part 0x44d3e0, the row's slot bar
  0x451870, lab_take_off (an Engine hook), engine_name 0x432f00 shared.
  test_lab_screen runs the exe's list (loadouts with mirrored items, part
  buttons and arrows in the plan): 80/80; test_salvage's puton part takes
  items off with the exe's 0x455fb0: 200/200. Breaks 16/17. Finds: the
  arrows stop at the ends (my first table wrapped: the test caught it), and
  ferro-fibrous / endo steel rows appear in the parts only with no slots
  left.

## Lab 7: the weapon groups page (2026-10-26)

- Assignment (lead): plan item 4 (mechlab.md step 7), WEAPONS_UNGROUPED /
  WEAPONS_GROUPED. Branch mac4/lab-7 off main.
- Done: both weapon lists (`LabWeaponList`), the rows' location tags
  (`LabLocationTag`, vtable 0x5c16c0), LabScreen::refresh_groups 0x44dcf0
  (the shown slot read six times, as the exe; the test caught the one
  read I had), message_format_n for 0x24b. Harness: probe_lab sets the
  lab screen handle after the screen is made (the exe's [0x605a58] is 0
  during the constructor); a weapon groups session in the plan.
- Breaks 13/15 (the mouse-only can_choose; select_group's out-of-range
  case never differed in the runs, not traced further).

## Lab 8a (2026-10-26): ENGINE_TABLE and the engine presses

Assigned by the lead after lab 7 merged (8f5cd8c): split lab 8 into 8a (engine,
sinks, armour controls) and 8b (the data panels); fold in a plan case for
0x44e4e0's out-of-range branch.

- Found: there are no sink presses; presses 19..22 are the engine (19 up, 20
  down, 21/22 the XL toggle, both the same); 18 is a part button (ASSIGNED_LIST
  0x456130). The armour controls live in MECH_DATA (ARMORTYPE/ARMORMASS
  toggles) and ARMOR_TITLES, so they move to 8b. The data-table base
  (vtable 0x5c20a0, `LabTable`) is ported here, shared with 8b.
- Quirks: the weight row's format is " %s", so "tons" never shows; switching
  XL off needs the result strictly under the mech's tons (engine up allows
  equal).
- 0x44e4e0's out-of-range case is unreachable by presses (the grouped list
  only selects group rows; group_of maps rows outside the list to 0), so the
  plan sets a weapon row as chosen directly (op O).
- The fault was the XL weight's ceil (0x5bb61c, an unstubbed import); the
  test now serves it as FLOOR's twin. test_lab_screen 80/80; the refusals
  counted (0xdce x16, 0xdcf x18, 0xdd0 x3) and the XL type shown in 33 runs.
  The message box's text goes through 0x575ff0 (buffer, id), not vt+0x98.
- Plan widened after the first breaks: engine indices biased to the ends
  (0, 1, 77, 78), the weight stub at, just over, and (for XL loadouts) at
  the XL-to-standard limit and half an XL under it, XL loadouts half the
  time, 12..31 engine presses a session. Final: 80/80, refusals 0xdce x18,
  0xdcf x72, 0xdd0 x7; breaks 20/21 (the reading's flag clear is inert:
  nothing but hidden is set before a table reads).
- Docs: mechlab.md step 8a section, the dialog table's ids corrected (the
  SELECT_* are 23..26, the parts all 18, ENGINE_TABLE's 19..22), porting
  order split; discoveries (the engine panel); symbols.csv (27 rows).

## Lab 8b1 (2026-10-26): MECH_DATA, ENDOSTEEL and CHASSIS_DATA

Assigned by the lead after 8a merged (561cf2a): 8b, split if large. Split
(agreed): 8b1 MECH_DATA with its toggle and CHASSIS_DATA; 8b2 ARMOR_TITLES
and LOADOUT_LIST.

- Found: the dialog's ARMORTYPE / ARMORMASS toggles under MECH_DATA are
  dead data (neither string is in the exe); MECH_DATA's one control is
  ENDOSTEEL (a CycleButton at +0x188 whose step 0x457160 changes endo
  steel, with refusals 0xdd1 / 0xdd2). ARMOR_TITLES is the armour-by-section
  editor (11 section cycles 0x450880, a re-spread button 0x450c70).
- Ported: LabMechData, LabEndoCycle, LabChassisData, LabRow::set_float(v,
  unit) 0x457f90, engine_kph 0x433050; hooks lab_config_weight_of
  (0x425300 by kind) and lab_config_set_endo.
- Test: the panels are real on both sides now (the fakes and stubs gone);
  dump_data; op D (ENDOSTEEL stepped); 0x425300 answered by kind (8 the
  mech's most, 3 often at endo's limit). 80/80, 0xdd1 x59, 0xdd2 x6.
- Breaks 22 of 22 (the first run missed the slot need 14 to 12: the plan
  drew no 12 or 13 free slots; 6, 7, 12, 13 added). Related tests and both
  suites pass (202, 12).
- State: sent for review on mac4/lab-8b.

## Lab 8b2 (2026-10-26): LOADOUT_LIST and ARMOR_TITLES' display

Assigned after 8b1 merged (48cc1e8). ARMOR_TITLES turned out bigger than
mapped, so split again (agreed): 8b2 LOADOUT_LIST and ARMOR_TITLES' making,
reading, show/hide and refresh; 8b3 its three presses (REMOVE 0x450cc0,
DISTRIBUTE 0x450c70, the section step 0x4509b0). Lead for 8b3: the plan must
step a section at both ends (front 0, rear at its cap) and press REMOVE
with nothing left to take off.

- Found: ARMOR_TITLES is a Control with ARMOR_REMOVE (+0x14c), eleven
  ARMOR_TOGGLE cycles (+0x2a4, toggle k = section k, all reading the same
  entry) and ARMOR_DISTRIBUTE (+0x3088); the toggles' places are function
  statics ([0x605aac..0x605ab8], guard [0x605ab4]), kept in LabState.
  LOADOUT_LIST lists by probe items (find_like/count_like), "(0 rounds)"
  for an ammo weapon with no ammo (discoveries).
- Ported: LabLoadoutList (0x44b230, can_light/choose false), LabArmorTitles,
  LabArmorRemove (set_tons), LabArmorToggle (place, refresh, step's start),
  LabArmorDistribute; the presses call the original for now. Hooks
  lab_config_sinks/double_sinks/section, message_format_isi; GameEngine's
  message_format_is was missing (the debriefing's 0xdc0 came out empty),
  added with format_message_f. zutl::format_message_v now keeps a width
  ("%1!2d!", 0x1c5): it read that number as a string.
- FIX (bug): the panel's texts read unchecked (fixes.md); the summary loops
  weapon_count() (the exe's 0x25; the plain PPC with zfix bugs).
- Test: the fake ARMOR_TITLES and the 0x44b230 / 0x450e00 stubs gone; the
  loadouts get sinks and section points (some bare); weapons with and
  without ammo; an armour session; synthetic dialogs keep ARMOR_TITLES /
  ARMOR_REMOVE (moved, texts cut past 6). lab_screen 80/80 identical (43 seeds with rounds lines, 42 with plain weapon or equipment lines; the armour panel shown and hidden, moved, DISTRIBUTE on and off). Breaks 31 of 31 caught (12 on the loadout list, 19 on the armour panel; armour kinds in a loadout were added to catch the last).
- State: done on mac4/lab-8b2, sent to the lead.


## Lab 8b3 (2026-10-26): ARMOR_TITLES' presses

Assigned after 8b2 merged (fd3f05f). The lead asked for a section stepped at both ends
(front at 0, rear at its cap) and a REMOVE press with nothing to take off.

- Ported: the section step 0x4509b0 (down a point, none below 0; up under the most with
  points left, else one from the torso's other side; at the most with that side empty a
  return, no redraw), REMOVE 0x450cc0 (the click, then the mech's armour probe, half a ton
  to the store and off the loadout, the spread when over) and DISTRIBUTE 0x450c70.
  New hooks: lab_config_set_section, lab_config_spread_armor (Config::spread_armor),
  lab_config_remove, lab_store_add_equipment; GameEngine's on Config and Store.
- No changes from the original (no FIX).
- Test: stubs for the spread (0x4247a0), the store's add (0x471a20) and the loadout's
  remove (0x471de0, through a vtable the loadouts now have), none changing anything; half
  the loadouts (70%) carry their armour as an item; the armour session runs first and
  sets sections (A, the most from the front toggle's text), steps toggles (I) and presses
  REMOVE/DISTRIBUTE (F), the lead's ends first, and sets a section so none are left (A 3). lab_screen 80/80 identical. Breaks 28 of 28 caught (13 on REMOVE, 3 on DISTRIBUTE, 12 on the step; a section set so none are left (A mode 3) was added to catch the last two).
- State: done on mac4/lab-8b3, sent to the lead. Related set: lab_list, salvage, mechsave, salvage_screen, field_cut, mw3tests 202, mw3sdltests 12 pass.

## Lab 9 (2026-10-26): the lab's loadout reaching the launch

- Branch mac4/lab-9 off main 6832ebc.
- Found: the engine's launch record took the slot's mech, loadout and skin at
  the Load state's start (load_state_enter), and the slot block the launch
  asks for (game vcall 0x34) was built from it. Choosing a loadout in the lab
  (LOADOUT_LIST, 0x00433df0) frees the slot's config and makes a copy; when
  the copy lands elsewhere the launch read the freed one (a crash in
  Config::weight). The exe reads the slot at the launch (0x491150).
- Changed: `GameEngine::launch_slot_refresh` reads the pilot's game's slot
  again when the block is asked for (from the menus). LaunchedMech gains the
  config (+0x3ec). `--verbose` from the menus prints the player's mech 30
  frames into the mission. No FIX (the port's wiring).
- Test: the campaign test's lab stage picks the loadout from LOADOUT_LIST,
  takes a weapon off (criticals, its row), engine a step slower (20), centre
  torso a point off (toggle 0), ACCEPT, LAUNCH; the launched mech has the
  live slot's config with 5 weapons (6), engine 52 (53), 22.09 m/s (c1m1's
  22.50), centre torso 22 (23), both passes. Break (the refresh off): the
  fix=false pass crashes in the launch on the freed config; the fix=true pass's
  copy lands at the old address, so its stale handle still names the new one.
- Live: `--briefing c1m2 --nosound --hidden --verbose`, clicks MECHLAB
  300,435@2500, CRITICALS 150,450, the ER Laser row 40,219, Armor 607,45,
  centre torso front down 100,235, Equipment 557,45, rating down 572,363,
  ACCEPT 590,450@3060, LAUNCH 556,435@3300: "5 weapons, 11 sinks, engine 52,
  22.09 m/s, centre torso armour 22" against the unedited run's "6, 11, 53,
  22.50, 23". Shots of the five pages in the outbox (2026-10-26/lab9).
- 8b3's 26 older breaks against the final plan: all 26 still caught.

## Save 1 (2026-10-26): zSave and the campaign's read-back

- Branch mac4/save-load off main aa405ed. frontend-plan item 7 split in
  three with the lead: save 1 (this), save 2 the anim registrants, save 3
  live and "Replace existing game?".
- Found: a saved campaign's `pilots\games` file is zSave's (0x5a8a20..
  0x5a9970, not zEntity's as the hooks were named); without it the pilot
  read back drops every saved campaign. MW3's InitInstance switches on only
  the three anim registrants (GWWorld, craters, weapons off). Saved at a win
  with that mission loaded, into the next game's file; read back by the
  mission load through the game's vt+0x28 (0x4352d0) for a game read from
  the pilot file; every other game's vt+0x28 answers true ("Saved Game
  Loaded" in IA and training).
- Ported: `gamez/zsave` whole (registry, sort, save, header, load, temps),
  CampaignGame::restore, Game::restore, the pilot hooks' save_world /
  load_world defaults, save_init / save_shutdown (renamed from
  entity_init / entity_shutdown), game_saved. FIX (bug) x3 for damaged
  files (zar's table count, a name without a sub, an entry longer than the
  file); zar's seek keeps SetFilePointer's signed distance (FIX (A) made
  faithful).
- Tests: test_zsave.py 200/200 (fake registrants from x86, damaged files,
  the fixes applied to the original by hooks, --nofix shows the exe's
  crashes); breaks 18/18. The campaign test's round trip (a new app reading
  the pilots back, the test registrant's entry restored, slot, loadout,
  lance and holdings as they were) in both zfix passes; breaks 4/4.

## Save 2 (2026-10-26): the anim registrants

- Branch mac4/save-2 off main fff16b0.
- Found: the restore is by def and node index, which is sound: an
  operation's missions share one world and the two missions the restore
  skips (Op 1 M1, Op 4 M4) are the world changes. A reloaded campaign brings
  the last mission's animation state into the next (discoveries). At the win
  CoreState::leave has run 0x50ace0(1) (wrong, corrected in save 3: the
  Win State's save comes before the Core state leaves).
- Ported in zeff_anim_save: the three savers and loads, 0x50be30, the log's
  accessors and replay (0x50b640/6f0/700, 0x50ca60, 0x50ca90), the switch
  0x5090d0 (GameEngine::anim_enable) and the registration in anim_init
  (anim_register_savers; the hook register_entities is gone). Quirks kept:
  the save binds defs to their nodes; an instance's running record goes onto
  the def; a missing instance is made idle; the model flag only set back.
  FIX (bug) for damaged entries (short, index outside the defs, too many
  sequences).
- Tests: test_anim_save.py, all six worlds identical (mid and win saves,
  entries compared as words, loads, the exe's entries cross-loaded by the
  port, 20 frames after; damaged entries loaded by the port). test_anim_run
  refactored into prepare/play (behaviour unchanged).
- Breaks 19/27 caught on c1 (the 8 not seen are explained in save.md: data
  no world has, a flag the replays restore anyway, states random play never
  reaches at a save on any world). The three damaged-entry fixes left out
  crash the port. Related set: test_anim_run, anim_binder, zsave, anim,
  anim_owner, puffers, pilot, mw3tests 202 and mw3sdltests 12, all pass.

## Save 3 (2026-10-26): the live round trip and "Replace existing game?"

- Branch mac4/save-3 off main 6712fbd.
- First, save 2's follow-up (lead): test_anim_save forces the four states
  random play never reached at a save (a def in error, a running copy at
  another node, a resting copy, a nodeless reset record loaded onto an
  invalid def). The running copy needed its def's last log record moved to
  the def's own node: the activations' save otherwise binds the def onto the
  copy's node, and the def and copy are saved at the same node. Breaks now
  23/27 (the 4 left: data no world has, or a flag the replays restore).
- The damaged loads, now on all six worlds, crashed the port on c4: a
  running entry's sound parent index past the node pool. FIX (bug)
  saved_node for every node index a load reads (fixes.md row extended).
- Found live: the Win State's save runs with the mission as it is (the Core
  state is only suspended under the end screen, fade and Win State), so the
  win's save holds the runs going on (c1m1: 80 RunningAnim entries); save 2's
  "stopped first" was wrong. test_anim_save's saves relabelled (the win's,
  then one after 0x50ace0), save.md, discoveries corrected.
- `--win N` (ours, program.md): Exit Mission once won, through the end
  screen, fade, debriefing and Win State; `--verbose` prints the pilot's
  game at the win and at each loaded briefing.
- tools/analysis/savecheck.py: straight on and reloaded c1m2 briefings hold
  the same (store and salvage, mech and loadout, lancemates), "Saved Game
  Loaded" only on the reload, Replace No keeps the file, OK rewrites it.
  8/8 checks; breaks 5/5. Clicks (game pixels): CAMPAIGN 70,60, the pilot
  150,135, ACCEPT 470,320, the first saved game 467,46, LAUNCH 556,435, the
  debriefing's checkmark 595,445, the Replace box's OK 246,290 and NO
  392,290. Shots: outbox/2026-10-27/save3.
- Open: the Replace box is drawn over a flat light grey screen (the end
  screen's fade); whether the original looks the same is not checked.

## Message box backdrop (2026-10-27)

- Branch mac4/msgbox off main b9931e9.
- The Replace box's flat grey was the end fade's: the tint quad's vertices
  never got the exe's setup (0x579c60: specular 0xff000000; 0x57dd40 from
  the device setup: z/rhw 0.99), so the quad came out fully fogged (c1's
  fog 214,211,214). The exe's call sites all fade to black.
- The grey (then black) squares under the box's buttons: the dialog grabs
  the screen after drawing its static parts and the buttons redraw from
  that grab; after a 3D frame screen_grab returned the frame shown (pause
  FIX (A)), which lacks the dialog. FIX (A): the first 2D lock after a
  Render3D frame reads it into the buffer (the exe's back buffer kept the
  last frame).
- The quit box on the main menu stood on the key magenta: 0x004ac150's
  empty lock/unlock of the back buffer key-filled the menu's picture. FIX
  (A): an empty 3D lock over a 2D picture gives it back (copied in place:
  the 2D targets point at the buffer).
- tools/analysis/boxcheck.py 3/3, breaks 3/3. The pause test now accepts the
  frame already in the buffer. Related: twod_msgbox, fade, app_states,
  fadecheck, savecheck, mw3tests 202, mw3sdltests 12.

## Item 8 (2026-10-27): an operation's end and the campaign's

- Branch mac4/op-end off main 6ff2b6b. Not split: the operation's end is the
  ordinary passed-mission path; the new pieces are the Win Movie State
  (0x47a960/9b0/a50/ac0, `WinMovieState`, GameEngine::replace_state_at for
  0x608be8) and the Load state's briefing movie (0x40a820 the app's +0x1c1,
  0x409320 fmv.zrd c<op>m<n>: the opener), movies still not played (zFMV).
- The campaign's last mission is Op 4 Mission 6 (c4bm6), not m7.
- Headless `campaign_operation_end_and_campaign_end_headless`: c1m4 won ->
  c2m1's briefing, the field bases lowered to 1 in the mission and back to
  3, the movies asked for c1m4 then c2m1; Op 4 Mission 6 won -> c4win asked
  for, Main, Credits. A movie-recording GameEngine subclass; the stepped
  campaign's salvage marked seen (else LAUNCH's box answers no headless).
- Live (`tools/analysis/opcheck.py`, --verbose now prints each state by
  name): c1m4 -> Win State -> Load State, c2m1's briefing (a lancemate,
  field bases 3); c4bm6 -> Win State -> Credits State (Win Movie and Main
  replace within a frame). 5/5.
- Breaks 5/5 headless, 3/5 live (the field bases and the movie names are not
  visible live). Discovery: the ending movie ignores -nofmv.

## Item 9a (2026-10-27): the lab's 3D view

- Branch mac4/lab3d off main 8a23e87. Split with the lead: 9a the world,
  camera and turning model; 9b the armour section's highlight (0x4504d0 /
  0x450470: there is no mouse picking; 0x4507c0 is the chassis lookup).
- Ported: zcls world_create 0x4f1bd0, the area 0x4f2500 / 0x4f2530, the
  grid 0x4f2560; mech3 lab_view.cpp: the load 0x44ffc0, unload 0x450390,
  the window 0x450460/0x44fea0, the entries 0x44fcc0/ce0/d20, the camo
  0x44fd90, the frame 0x450690 (lab_view_frame), the target 0x450810 and
  clear 0x450800; GameEngine::node_frame_update (0x4f7f70, was a hook: the
  copied tree never got its matrices). The turn is time-based (dt * 0.8).
- test_lab_view.py 300/300 bit for bit, breaks 14/14 (Compare(tol=0): the
  default tolerance let small integers pass as tiny floats; raised with the
  lead for the other tests using it). --lab-mech N (ours) for the shots: all
  19 chassis and a camo swap, outbox 2026-10-27/lab3d.

## Item 9b (2026-10-27): the armour section's highlight

- Branch mac4/lab-highlight off main cf36ef9. Ported 0x4504d0 / 0x450470
  (lab_highlight, lab_highlight_part; the hook renamed lab_section_highlight).
  test_lab_view.py adds the highlight: 300/300, breaks 9/9 (two sections in
  a row so the off pass shows).
- Live, the parts did not turn green: the tint reaches the GPU only through
  the vertices' fog, and the port's FOGENABLE came on only in the shadow
  passes (a mission frame). The exe's device setup turns fog on for good
  (0x57bb70(1)): ported as zvid::set_fog, called in video_open. Then the
  torso, arms and legs show green (outbox 2026-10-27/lab3d).
## Compare (2026-10-27): integers exactly

- test_anim_run's Compare (used by test_anim_run, test_anim_save) gave every
  differing word the float tolerance, so small integers and flag words
  (denormals as floats) passed: 1 against 0 within 1e-4. Now only words
  that are zero or normal floats on both sides get it. test_puffers' own
  compare already types its words; test_anim_binder does not use it.
- Reruns: test_anim_run (6 worlds, 60 frames) and test_anim_save (6 worlds,
  60 + 20) identical, the same counts within tolerance: nothing had been
  hidden. test_anim_save's four uncaught breaks again: the active flag not
  restored is now caught (it was the tolerance, not the replays as save.md
  said); the three others stay unseen (data no world has). 24/27.
- The breaks script's "stopped" match also took "running_stopped" in a
  passing summary line: read rc with it.


## Item 10 (2026-10-27): zFMV's script without video

- Branch mac4/fmv-script off main cfad444. Ported fmv_script.cpp
  (0x00598cc0..0x0059a490: the script, its load, play and poll, the image,
  wait, fade, blur and sound actions; PLAYAVI a stub that ends at once, as
  the exe's without the file; PLAYMCI unused, not ported) to
  src/gamez/zfmv, and the blur passes 0x0055a540 / 0x00559ec0 / 0x0055a1b0
  / 0x0055a3b0 to zrender/zrndr_blur. GameEngine's movie hooks and the
  pause screen's blur (wait_new / start / poll / end) run them
  (mech3/app_fmv.cpp, a zfmv::Host on the Engine's hooks). docs/fmv.md.
- test_fmv.py: 428 scripts (every shipped block named, random ones with
  every verb and bad entries), 115k calls identical with each poll's answer,
  the current action and every loaded action's fields; the pause blur; the
  passes bit for bit (the target padded: a narrow rect's walk reads the rows
  around it). Breaks 31/31 (the two first missed, a PLAYAVI flag default and
  SHOWIMAGE with one coordinate, are caught by the fields check and
  three-item entries).
- Two 24-bit details: the poll's t - start is a float (the fade's alpha was
  one bit off without it); a 0-second fade's NaN alpha is the x87's negative
  NaN, ARM's positive (the test ignores the NaN's sign).
- Live: the first image fetch at the intro marked zImage's library missing
  for good: the shell's start-up 0x00516f50("zbd") was never wired in
  GameEngine (the main menu mounted "zbd" itself later). Now
  GameEngine::geometry_init gives it to the 2D host's mounts.
- The SPLASH logos are stretched into the 2D surface and shown only by the
  WAIT's end flip (frame 3 with --shot-any), i.e. while the main menu
  loads; the 4 seconds before show the previous screen. In the port that
  is black for 5 s at start (INTRO's WAIT 1 and SPLASH's 4, the movie
  skipped). To check on the original.
- mw3tests: the programs through the Intro now meet SPLASH's 4 s; the tests'
  FakeClock (16 ms a call) took them past their pump limits. Platform gets a
  `movie_clock` (zFMV's timeGetTime; none: the frame clock) and the tests a
  second-a-call one. Five c1m4 AI tests with counts recorded from a start
  without the wait set `lobby_flow_` (which only skips the Intro's movies).
  The start test now expects 0x00598cc0 ported. geometry_init forwards only
  the start-up's "zbd": the missions' "zbd;zbd\<op>" as mounts would stay
  for the next mission's files (kept as before: logged).

## Pause check and item 11 (2026-10-27): the movies' video

- Pause: --tap Escape in c1 did not pause because with --shot-any every
  presented frame counts, and c1's loading and start fade present about 600
  before the Core state's first frame; the taps landed before the mission
  polled input, and the Core state's start unacquires the keyboard
  (DirectInput's buffer goes with it). --tap 1@900:902 --shot-frame 960
  pauses over the blurred battlefield (outbox 2026-10-27/fmv). No change.
- Branch mac4/fmv-video off main 469bec8. The AVIs: 640x480 Indeo 5 at 15
  fps, PCM 22,050 Hz (8-bit mono; INTRO and C1 16-bit stereo); INTRO all key
  frames, the rest one every 15. pl_mpeg reads 22 kHz Layer II, but MPEG-1
  has no 15 fps: the importer writes 30 fps with every frame twice and the
  player decodes by count. The lead: keep the sound as the AVI's PCM.
- mw3import (tools/import, optional, FFmpeg's libraries): .m1v (an MPEG-1
  video stream: the program stream's muxer warned of buffer underflow at q
  2), .wav (the AVI's PCM bytes), .fmv (the stream headers and idx1's key
  frames, text). Indeo packets that decode to no picture repeat the last.
- fmv_stream.cpp ported (0x0059a4a0..0x0059b040): the opens, the frame with
  the key-frame catch-up, the sound's frame from the play cursor, the
  refills (the second piece's count quirk kept). PLAYAVI's step and start
  (0x00599de0 / 0x00599ea0) on it. Video for Windows goes to the Host:
  app_movie.cpp answers from the importer's files, pl_mpeg vendored
  (third_party/pl_mpeg, compiled as C).
- --nosound's silent output kept the play cursor at 0, so the sound's frame
  was the end at once: it moves with a clock now (FIX (A), fixes.md).
- test_fmv.py: 200 movie runs among 628 scripts, 167k calls identical (every
  decompress flag, both key searches, locks split, the cursor at the halves);
  breaks 22/22 (two first missed: a format past 0x6c, the cursor exactly at
  the half). mw3 --movie NAME (ours): on a clock stepping 1/60 s a poll each
  frame is drawn once; every block drew its AVIs' dwLength frames, the last
  dwLength - 1.

## Item 12a (2026-10-27): the Action screen

- Branch mac4/instant-action off main 290b3af. Item 12 split: 12a the
  Action screen and its state (0x00488bc0..0x0048aac0, the ACCEPT's
  ActionGame fill); 12b the launch from it (the 0x00470e30 store, the 0x231
  header, the IA Dummy State, headless wave 1 to the tallies).
- Mapped from the asm (the decompile loses ECX in 0x0048aac0, 0x0048a810,
  0x0048a930): the five scenarios (0x005e6fb0: ia mission, mode, name,
  description), the worlds 0xe33..0xe36, the line-up's copies placed OFFSET
  apart, the enemies' loadouts from the definitions (training ones never),
  the progression waves (0x004897b0, at most 20).
- Ported as action_menu.{h,cpp}; the program's construct(0x00488bc0) makes
  it, action_open resets it. Live: --click-at 60,180@40 (INSTANT ACTION),
  a pilot typed, ACCEPT: the screen as the original (outbox 2026-10-27/ia).
- The decompile's 0x0048a140 read the world's row as the scenario's: the
  emulator's crash (an out-of-range world, read unchecked) showed +0x38 is
  the world's name. A wave outside the table (the best 20 at the reset, or
  below 0) reads strings as mechs: FIX (bug), hooked in the test.
- test_actionscreen.py: 60/60 (876k calls, the pilot file's bytes too);
  breaks 34/35 (the miss unobservable). The first batch missed four: the
  dump lacked the items' values, and the best wave 20 was rare (weighted).

## Item 12b (2026-10-27): the launch from the Action screen

- Branch mac4/ia-launch off main f8143ad. The store fill 0x00470e30 ported
  (Store::fill_all, equip_has_tech 0x00484a00); test_store_fill.py 4/4,
  124 items (125 with the plain PPC fix).
- The program's Load state refused Instant Action (back to the main menu):
  it now takes the world (vt+0x54, +0x74) as the directory and the ia
  mission (vt+0x4c, +0x78).
- The 0x231 header: %1!d! is vt+0x48 (world + 1), %2 vt+0x0c (+0x3c, the
  scenario's name): hud.md 24's open note closed.
- The IA Dummy State (0x0047ce80) and the wreck's tally wired to the
  launch's ActionGame; CONTINUE's round (Engine::action_next_round, the
  debriefing probe's model). test_actionscreen.py enters the state: 60/60.
- Headless instant_action_menus_to_wave1_tallies_headless: menus, the
  Action screen on Progression, the briefing, wave 1 killed, wave 2, the
  header, Exit Mission, ABORT, the tallies (2/0/0, best 1), the pilot file,
  the Action screen again with Wave 2. The test first acted inside the
  Action State's resume (its flip presents a frame while the pilot's pop
  is still running): it now waits for an empty queue.

## Item 13a (2026-10-27): training's fourteen condition types

- Branch mac4/training off main 2b41549. Item 13 split: 13a the fourteen
  types' reading (0x0045c920's other branches) and checks; 13b the lesson
  list (the training game's vt+0x40 0x00437140, 0x0045ed40..0x0045f080,
  0x004373b0), the TR Dummy State (0x006098c8, enter 0x00491ab0: the
  mission passed, the pilot saved, Main with the training menu again), the
  program's answers for the checks, headless t1 lesson 1 (m11: a 1 s
  time_limit; m12 User_Command CMD_SHUTDOWN; m13 face_object).
- Read from the asm (the decompile is mostly inlined strcmps): each
  branch's keys, its object's size and vtable, its required values.
  aim_at_object asks 0x00444330 on the screen's +0x144c.
- test_objectives.py: the t1 blocks are compared whole now (were "must be
  reported unported"), made-up training conditions, coverage of every
  branch. test_objective_check.py: a training world from a shared mix
  (speed, aim hit and node parents, target slot and entity, aimed part,
  facing, heat, weapon manager and group, current weapon, weapon fired,
  clock, view angles, buttons); the Python fake's 0x005a85e0 returned one
  buffer for every object (entity - player was 0): each object its own.
  FIX (bug)s hooked into the original: view_object and weapon_group_select
  with no vehicle / weapon manager.

## Item 13b (2026-10-27): training's lessons and the TR Dummy State

- Branch mac4/training-lessons off mac4/training (main 90765a1 merged in).
- The lessons: TrainingLessons (0x0045ed40..0x0045f080) and training_load
  (vt+0x40, 0x00437140) in mech_objective, the training game's `lessons`.
  test_training_lessons.py found the shared description slot: the `_set`
  lessons and the failed reads leave their desc in the mission's slot.
- The TR Dummy State (0x00491ab0) in app_states; the end screen's fade to
  it; test_actionscreen.py enters it (training selected first: 0x004794a0
  answers only for the current game, the exe writes through null else).
- The program answers the training checks (zInput, the controls, the
  clock, the camera; the mech and HUD fields through app_mech.cpp helpers:
  mech.h / hud.h / weapon.h clash with app_mission.cpp's Handle).
- Headless training_menus_to_lesson3_headless: 14 lessons, m11 done at
  frame 63, CMD_SHUTDOWN's key, m13 at 65, mission 1 passed, the pilot
  file, the pilot screen again.

## Item 14 (2026-10-27): the mission plan

- docs/mission-plan.md: the census (c1m1 with input, 30 s: 84 unported,
  163 on 10-21; every campaign mission and t1 idle: all in c1m1's set but
  c4b's three mech hooks), each system's state, an 11-item order.
- `--mission c4m4` loads world c4 (no such mission: no mech); c4b's are
  c4bm4..c4bm6. The 180 s census hangs (30 s is fine); one c1m1 quit
  trapped (133) once in four; c4m2 is 8x slower headless.

## Mission-plan item 1 (2026-10-27): the hooks still reached

- Branch mac4/wire-hooks. Wired in the program's hooks (the defaults stay:
  test_mech_build records 0x004d9840 / 0x00491d20 through them): the cheat
  check, the brain's settings, the view's constructor log, find_node /
  node_set_active / camera_set_lod_scale, the HUD screen (0x00430aa0) and
  its weapons layout, the game-type answers, ai_targets_shutdown.
- c4b's 0x0041aba0 / 0x0041ca30 / 0x0041cc30 came from `c4m4` (no mech).
- `MW3_TRACE_UNPORTED` found 0x00430aa0's caller (MechCamera::set_look).
- Census 84 -> 73; c4bm4 79; mw3tests 205/0.

## Mission-plan item 2 (2026-10-27): the quit trap and the census hang

- Branch mac4/quit (main 9188039). The crash reports (EXC_BREAKPOINT in
  xzone malloc, in Metal's and CoreAudio's teardown) were heap damage;
  build-asan/mw3 named it: mw3_main's stop_game after run's ExitInstance
  (zsnd::unlock on freed voices). Dropped: the exe leaves the world to
  the process's end on WM_QUIT.
- The 180 s census: not a hang. sample showed HudRadar walking
  radar_blips; 0x004b4ab0 (the list's clearing object) was unwired, and
  so were WeaponCalls' object3d_create / set_action_priority / set_action
  / node_destroy. 30 s: 2,500 entries before, at most 4 after.
- Checks: the c1m1 Owens test (0x004b4ab0 not reached unported) and the
  census (at most 100 shots listed); both fail unwired. 180 s census 91 s.
- Note: mw3tests filters by MW3_TEST, not its argument.

## Mission-plan item 4 (2026-10-27): the vehicles' shadows

- Branch mac4/vehicle-shadows (main 1787227). The exe's vtables: vt+0x2c
  itself and vt+0x50 0x40bae0 (0) for vehicles, field bases, turrets and
  the pocket marker (0x5ccfe0), so all of them cast 32x32 at their own
  height. `shadow_entity` resolves each zEntity entry through the
  program's registries; `update_mech_shadows` keeps the exe's sums.
- Test: the c1m1 shadows test moves the first vehicle 40 m from the player
  (skipping the frame of the move: the upkeep runs in the draw before it)
  and checks near/far casters, 32x32, drawn. Breaks: vehicles dropped from
  the lookup (both checks fail).
- `--vehicle-ahead M` (ours) for the shot; `move_mech` at frame 1 moves the
  reported position but not what the camera follows (not chased).
- Shots: outbox/2026-10-27/item4_c1m3_*.png (on, off, the crop).
- The pocket marker is a plain AiTarget (no destructor): its caster would
  outlive the mission. The mission's end (0x0049dc40) now frees it
  (`program_pocket_marker_end`, the base end 0x004d58a0's part; matched by
  the object: the panel's globals are reset first); the entry was the
  unported "entity vt+0x0" before. The c1m4 pocket test moves the marker by
  the player at the quit, checks it casts and that no caster is left.

## Mission-plan item 5 (2026-10-27): the model priority and the skis' quads

- Branch mac4/model-priority (main 63cb569). 0x0053bb20 in gmod_const
  (`zmod::model_set_priority`), 0x004776d0 / 0x00477730 in mech_skis
  (`ski_polygon`, `ski_material`); the program's WiredBuild and the script
  engine's Object3DSetPriority wired; the hooks' defaults stay (test_mech_build
  stubs both as calls out).
- The ski model's priority is set before its quads exist (a no-op); the
  quads and the feet's _shadow models are inactive, so nothing shows. What
  changes in play: the build's rand()s (at least 12 a mech), as the exe's.
- test_ski_polygon.py: 1,325 runs, the exe's rand() stubbed with MSVC's
  LCG on the same seed; the material's flag bits 3..7 are stack garbage
  in the exe (compared: bits 0..2). Breaks: the seed, the points, the
  priority, r/g swapped, the threshold, the green mask.
- The rand()s moved three c1m4 runs' exact values (port-only pins, not the
  exe's): re-pinned. In "fires on a target" he turns from ghsD2 to ghsD3
  and lands two hits in 40 s (30 -> 16 -> 9) where the hut was down
  before; he holds the trigger most of the run.
- Open: so few hits landing from 80 m (two in ~1,000 firing frames) may be
  worth a look on its own (aim, line of sight, the lasers' recharge).
- Open: `move_mech` on the first Core frame (item 4's shot) moves the
  reported position but not what the camera follows.

## Mission-plan item 6 (2026-10-27): the start-up's leftovers

- Branch mac4/startup-leftovers (main bb6d4b8). Also mission-plan.md's new
  item 8, AI hit rate at range (lead).
- MW3_TRACE_UNPORTED on each key (the frames need c++filt) named the
  callers. 0x00413e60: alone, the limp structure 0x005e1f78 is 0.4, the
  port read the data's 0.3; the damage scale is the scripts' DamageScale
  (interp_globals), which mech_parts / mech_damage now read. HitGlobals in
  mech_parts, hit_globals_init from the program's mechs_init,
  hit_globals_reset in the tests' reset_process_state.
- test_hit_globals.py: 0x413e60 with its other calls stubbed, both flags,
  and the values before; breaks: the flag, the values before.
- 0x00490720: GameEngine::missions_reset called the logging base first.
- No effect: 0x004b52e0, 0x0047f2b0, 0x004ac860 (stealth), 0x004ab6b0
  (resolution step 3). symbols.csv has all six.

## Mission-plan item 7 (2026-10-27): the script engine's live calls

- Branch mac4/script-calls (main 5b457fa). The dispatcher's `call` became a
  macro over `call_named`, counting null entries by field
  (`zint::null_calls`); `--verbose` prints them. 21 missions, 600 frames
  each: the same 14, all from the support scripts (interp_dump.py's
  scripts.txt shows the values).
- Ported: 0x53cef0 / 0x53cf00, 0x538070, 0x538080 (gmod_const's planar and
  colinear became globals), 0x526df0, 0x526a00, 0x579240. Wired as nothing:
  0x536a60 (empty), 0x5269a0 (gamez.zbd sets the pool), the three
  software perspective setters, the three loose-file paths.
- Tests: test_script_setters.py (400; the getter's st(0) stored by a stub
  fstp; breaks: the backface, the getter), test_gmod_add.py at set
  tolerances (break 3: the colinear), the c1m1 Owens test (none skipped,
  the four settings; break: vertex shading unwired).

## Mission-plan item 8 (2026-10-27): the AI's hit rate (diagnosis only)

- Branch mac4/ai-hit-rate (main 5d1f7a2). Temporary traces (removed): the
  firing's phase and aim, the manager's trigger and the beam's charge, the
  beam's ray hits and their parents, zWeapon's apply_damage handler and the
  target's on_hit. Findings in ai.md 17.7a: the exe's design at each step.
- Open: the Gauss rifle (group 1) never fires in that run.

## Mission-plan item 8b (2026-10-27): the Gauss rifle in fires_on_a_target

- Branch mac4/gauss-pick (main 8558212). Traces (removed): the volley
  pick's inputs and choice, phases 2-4 with the current weapon, the torso's
  facing, the slug's flight, the target's lock point. The Gauss is picked
  and fires once at 15.22 s, 80 degrees off (in_reach at facing > 0: the
  firing's thresholds are the exe's 0s, read from 0x5e8868; nothing writes
  them). The original's design; ai.md 17.7a corrected (the 10 m was the
  beams' contact, the target 80.7 -> 31.5 m).
## Mission-plan item 9 (2026-10-27): the HUD's free

- Branch mac4/hud-free (main 8558212; 8b is docs). WiredData::hud_free
  deletes the Hud; ~Hud, ~HudReticle (0x00444280), ~HudLight's release
  loop (inlined in 0x00442d00 / 0x0043d810), ~HudTarget (0x00440190),
  ~HudMfd (0x004422b0) with the new hook nav_list_free (the program's
  LanceDisplay map). Hud::live counts HUDs (ours, tests).
- test_hud.py: a free after the dump on half the seeds. Its fake nav
  objects got a vtable (vt+0 recorded) and the MFD views' swapped vtable
  keeps the real slots (vt+0 the deleting destructor). The first
  differences found each missing release, in order: TARGET's arrows,
  the lights' images, the reticle's four.
- Breaks: hud_free empty (the mission-end test), the reticle's releases
  dropped (test_hud seed 0, call 4869).

## Mission-plan item 10 (2026-10-27): the app's sound set

- Branch mac4/app-sounds (main faefeba). The "sound set" is the app's
  +0x1e4 passed to two empty functions (0x0040baf0 ret 8, 0x004a5bc0 ret
  4) by the briefing's load (0x004096b0..); GameEngine answers them and
  0x0040a7d0 / 0x0040a830. Check in the c1m1 Owens test (break: app_sounds
  back to the hook).

## Mission-plan item 11 (2026-10-27): the census's known keys

- Branch mac4/plumbing (main 390007c). GameEngine::known_unported: 22 keys
  with their reason (own 4, deferred 11, no effect 7); not logged, listed
  apart by --verbose and the census; unported_counts unchanged (tests read
  the stealth key's count). Census: 39 unported, 22 known.
- The plan's "~45 plumbing" was too many: checked one by one, the rest are
  ported-but-unwired calls, settings the exe changes from the data (the
  HSE test, the animation records, the frame clear), the span target's
  rect (the fade's tint), and ones still to trace: mission-plan items
  12-15.

## Mission-plan item 13 (2026-10-27): the start-up's settings

- Branch mac4/start-settings (main 2a750f2). GameEngine: effects_set_a /
  _b, video_set_clear, camera_set_hse_test (the hooks renamed from
  camera_set_near / _far: 0x004f3540 writes [0x005ea53c], unread; 0x004f3550
  is CameraSetObjectHSETest). reset_process_state restores the data's.
- Measured with temporary switches and counters (removed): HSE 20k-81k
  calls in 300 frames, 0 hidden; records 74 against 132; no trails with
  the clear off (t1's rain does not smear over the black).
- Check in the c1m1 Owens test; break: video_set_clear back to the hook.

## Mission-plan item 14 (2026-10-27): the span target's rect (no change)

- Branch mac4/fade-rect (off mac4/start-settings). Seen in the census's
  quit (Ctrl+Q, Enter, the fade to the lobby): the tint's quad 0,0-640,480,
  alpha 0.99 down. Item 11 confused zrndr_tint's FinishTarget (set by
  zvid::lock_3d) with the fillers' SoftTarget. The fillers read base and
  pitch only; 0x00558070's globals are write-only. Six keys moved to the
  known table (28 now).

## Mission-plan item 12 (2026-10-28): ported but not wired

- Branch mac4/wire-ported (main 1257bca). Each traced (MW3_TRACE_UNPORTED +
  c++filt) and wired in the program's hooks; see mission-plan.md item 12.
  Not straight but done here: the briefing waypoints (five hooks, all
  ported) and video_is_3d (its flag is "opened"; with has_cap unwired it
  would have turned the shadows off).
- Checks: the c1m1 Owens test (none of the nine keys reached, an option
  set mid-mission reaches the mech and back); shots of c1m1, t1, the main
  menu and c1m1's briefing ("Start" shown), all --nosound (stream day).

## Mission-plan item 15 (2026-10-28): the rest traced

- Branch mac4/trace-rest (off mac4/wire-ported d1bf611). Ported
  camera_set_listener 0x004f3430 and display_set_size / _origin 0x004fa580
  / 0x004fa600; wired them, the console's scroll clear, the texture group,
  WiredBuild's view_set_camera (the options' control-mode path reached the
  base) and the load screen's driver and mode lines (Render3D::driver_name,
  new). The launch's camera handle is 0 in engine memory; camera_update
  falls back to program_view()->camera.
- Known now 36 (eight added, reasons in program.md). 0x00537520's
  [0x0072f370] is 0x0057ba80, a Release of the record's system surface.
- Left listed: 0x00526510, 0x005578f0, 0x00557b30, item 17.
- Checks: the Owens test (item 15's keys absent, the listener within 15 of
  the mech); breaks: the WiredBuild override removed, the texture group
  removed, the listener set off: each fails it. All runs --nosound.
- mw3sdltests crashed on its second run in one process once 0x005964c0(0)
  was wired: the first run's nodes stayed on the scroll list (its teardown
  skips the world's unload). Its two runs now reset the list as
  mw3tests' reset_process_state does. Shots of c1m1, t1 and the menu draw.

## Mission-plan item 17 (2026-10-28): the modules' starts

- Branch mac4/module-starts (main bc2c0ab). The 157 writes taken from the
  decomp, every direct read from mech3.asm by containing function, a
  reader counted as run when it has a port (script in the scratchpad;
  the table in docs/module-starts.md).
- No change: the fog differs but the world read sets it all before a draw;
  checked with a temporary print of zmod::fog() at each draw where it
  changed and a switch putting the exe's start values in (removed): one
  identical line per mission (c1m1, t1, c3m1, c4m2). Shots are not
  repeatable run to run (rain, timing), so no byte compare.
- The three known; census 0 unported. Break: a reason without its prefix
  fails known_unported_keys_name_their_reason.

## Mission-plan items 16 and 18 (2026-10-28): the docs, the lab's start

- Branch mac4/docs-16 (main 319826f). ai.md section 6 (the boundary, the
  sensors), damage.md (0x464ce0 is Torso::Steer; the jolt decays in
  0x464e80, ported), movement.md Left (only force feedback left).
- `--lab-mech N` alone: --briefing c1m2 and MECHLAB at 300,435@2500 (the
  route item 9's shots used). The lab's fog checked with the item 17
  print and switch (removed): off, all 0, either start.

## Mission-plan item 19 (2026-10-28): the lance state, three hooks

- Branch mac4/lance-hooks (main 7bbc29a); items 19-23 added to the plan.
- player_lance_state: 0x4cbeb0 calls the game's vt+0x48 (a getter in all
  five game types, read from the exe's vtables) then vt+0x2c, the type;
  4 only for Training (0x473a20). Wired as the launch's type. Test:
  mission_c2m1_lancemate_forms_up_only_in_training_headless, c2m1 run as
  launched (1) and with the type 4: following 262/262 frames, 29.6 m,
  against the formation part 262/262, 0 m. Break: the hook back to -1
  fails 3 checks.
- The three mech hooks: my all-missions list had c1m5, c1m6, c2m5, c2m6
  and c4bm7, which do not exist; the launch found no "player" record (no
  reader archive loaded), made no mech, and the player hooks fell through.
  --mission now refuses a mission without zbd\<op>\readermN.zbd.

## Mission-plan item 20 (2026-10-28): the all-missions census

- Branch mac4/census-all (off item 19, 34b25f7). The test runs 21
  missions (not 20 as I first counted), 1200 frames each, then
  stop_game, exit_instance and destroy (no test called destroy before).
- Its first run found five more keys than the program's runs: the
  program's own exit skips stop_game (item 2), but the Exit state (QUIT)
  runs it. All five are frees; 0x00414000's archive close is the motion
  archive's.
- Game texts: the four slots per game type read from the exe's vtables;
  a debug start answers by its launch (t1 the training lesson).
- Break: game_text back to the logged base fails the census (game.vt in
  t1). The known table is 46 now.

## Mission-plan item 21 (2026-10-28): the census with input

- Branch mac4/census-input (off item 20, 3c7ac04).
- Routes, each through `build/mw3 --verbose --nosound`: walking and
  firing, c1m1 won (--win) through the debriefing, the lab through
  LAUNCH, Instant Action's and Training's pilot screens, options,
  credits, multiplayer, c1m1's pause, Training's lesson 1 launched.
  All end at 0 keys outside the table.
- 15 keys found: 11 wired (the debriefing's voice, the Core state's
  anim stop, the 2D target's size, zClass's flag, zVideo's three device
  queries, training's slot and 0x00433df0, the end's cockpit HUD and its
  objectives box), 4 multiplayer known (deferred). The known table is 50.
- Training's lesson 1 had launched with the slot's default armed loadout;
  now with training1 (equipment only), as the exe. Without it the
  menus' training test never reaches lesson 2.
- The first pause run (`--mission c1m1 --tap 0x01@200`) ran 14 minutes
  and was killed; redone through the menus. (The cause, found in item 22:
  a launched mission's --frames counted only frames with the scene, and the
  pause draws none.)
- Tests: training_menus_to_lesson3_headless checks the loadout and the
  five keys; known_unported_keys_name_their_reason the 50 and the 15.
  Breaks: training's slot unwired fails 6 checks; 0x00586ce0 renamed in
  the table fails 2.

## Mission-plan item 23 (2026-10-28): the list for Owen

- Branch mac4/owen-list (off main 4c2d7cf); docs only, nothing changed.
- `docs/for-owen.md` from playtest.md, fixes.md (Candidates, the table's
  candidate rows, Decided's c4b m4 note) and the topic docs they cite.
- playtest.md's rows #11, #18 and #26 are stale ("waiting", but ported).
  Left for the lead or Owen to close, as the item asked for no changes.
## Mission-plan item 22 (2026-10-28): the 180 s runs

- Branch mac4/long-runs (off main 4c2d7cf).
- The census takes a mission (MW3_CENSUS_MISSION) and prints each
  cycle's numbers (MW3_CENSUS_STATS). A first 360 s run overlapped my
  hang tests, and macOS's resident size fell as pages were compressed
  (79 to 21 MB), so I switched to the footprint (TASK_VM_INFO) and redid
  the runs quietly.
- c4m2: entity.vt+0x28+0xc0 from a field base hit by a turret (team 4,
  unnamed); wired through hud_object_team.
- The live nodes were found by walking the pool's free list. A node has
  no live bit; its link field holds the free chain.
- The hang: not a debug start's end (the free camera's update true quits
  through zgameapp.cpp's post_quit), but a launched mission's frame count
  stopping in the pause. Fixed and tested by the program itself, spawned
  from mw3sdltests (MW3_BIN). Break: the old count is killed at 90 s.
- Open: c4m2's last cycles are 12% slower.

## Resolution step 2 (2026-10-28): the window's shape

- Branch mac4/window-shape (off main a67d090).
- sdl_fit.h (pure geometry), platform::Window::frame_size (from video_open),
  SdlWindow's shape and auto size, the back buffer at the fit rect with the
  black bars in present and in the capture, SdlZinHost and SdlInput through
  the rect.
- VMode can't be set from settings.reg (it is detail.zrd's, never saved),
  so no 16:9 frame reaches the screen before step 3's mode switch. The
  shots for Owen are 640x480 frames: at 1280x720 pillarboxed (c1m1 and the
  main menu) and at 640x480.

## Resolution step 3 (2026-10-28): the mode switch

- Branch mac4/mode-switch (off main 12be988).
- 0x004ab6b0, 0x004ab910 and 0x0041cdc0 are ported on Engine (app_hooks.cpp)
  and checked in test_app_states (three cases, 0x004ab6b0 and 0x0041cdc0
  unstubbed for their own). 0x0040f330 is ported on the real panel and
  checked in test_hud (op 43, with the six globals in the dump).
- The asm's 0x0040f330 call passes ECX the display rect and EDX the render
  rect. My first names for the hook's parameters had them the other way.
- 0x004ab860 always returns 1 (`mov al, 1`), so Shift+V always goes on.
- Program: c1m1 at InGameVMode 13 at 1920x1080, 0 calls outside the table.
  The HUD keeps its 640x480 block (step 4). Its Core state started at frame
  99 here, against 576 in item 22's runs (the fade is timed), so I
  corrected program.md's claim.

## Resolution step 4 (2026-10-28): the HUD by anchors

- Branch mac4/hud-anchors (off main 013378d).
- Anchors read from hud.zrd: every piece moves by w - 640, (w - 640) / 2 or
  h - 480, or not at all. The speed and fuel frames hang from the top.
- The scale: a canvas (the display rect) of the frame over a whole scale,
  so the 16-bit layer and the HUD are drawn small and stretched. The
  render and window rects keep the frame. A first try put the canvas in
  the window rect too, and the 3D projected onto 960x540 inside 1920.
- Two port gaps showed. screen_size set the 3D viewport to the buffer's
  (now canvas) size. The program's HUD hooks answered vmode 0, so the
  set-up never read hud.zrd, at any mode.
- The panels' quads and the views' depth clears are canvas pixels drawn in
  the 3D pass: mapped onto the frame.
- Review (the lead): the waypoint arrow was on the frame's scale. Every
  0x0053cf10 projection is now audited (resolution.md section 8's table).
  Two had no 0x0040da30 in the exe: the arrow, and TARGET through the zoom
  window. Both are mapped. The headless arrow read takes frame 5; the
  pump's 400 passes end before frame 30.

## Resolution step 5 (2026-10-28): the menus

- Branch mac4/menus-any-size (off main f5fc1c3).
- The front end, lab and briefing run at VMode 5 always, so step 2's fit
  covers them. What was left: the pause screen and the quit box over a
  canvas mission. FIX (B) ztwod::dialog_origin centres the 640x480 frame on
  the canvas, read by Dialog's and MenuScreen's constructors.
- The real-click test needs a host whose mouse() is zInput's (the default
  host answers none). Headless buttons have no images, so their rects are
  lines (top == bottom).
- My item 21 bug: render_flush read its rect handle as memory, and the mech
  lab was black from that merge on. Found by bisecting the five merges in
  a scratch worktree (scratchpad, wt-pre). Fixed and tested.

## The blank-screen guard (2026-10-28)

- Branch mac4/screen-guard (off main 8c305fc).
- sdl_program_screens_are_not_blank: 11 program runs at once (21 s), lit
  and green shares from each BMP. The menus' driver now prints each state
  with its frame (--verbose), which is how the shot frames were picked:
  Pilot 44, Action 104, Lab 2504, debriefing's Win 3915.
- Break: the render_flush memory read fails the three lab tabs (lit 0.000).


## The docs refresh (2026-10-28)

- Branch mac4/docs-refresh (off main 29dc01f); docs only.
- Every Missing and Left item in frontend-plan.md, twod.md section 9,
  menus.md section 14, hud.md sections 7, 8 and 9 (Left), mechlab.md
  section 5 and mission-plan.md section 1 checked: a port of the address
  in src (grep of the Mech3.exe comments) and the live screens of the
  blank-screen guard.
- Still open: multiplayer; zFont (next); TextBox's 0x005a1cd0 / 0x005a1cf0,
  0x005961d0, 0x005a22c0, 0x004ac050, Movie2D's update and draw, the
  developer consoles, the software paths; the +0xbc bit 2 question; c4m2's
  late +12%.
- My "c4m2 8x" in the gap list was stale: item 3 closed it.

## zFont (2026-10-28): nothing to port

- Branch mac4/zfont (off main). Narrowed as the lead asked, then further:
  no object in 1.2 runs TextBase's draw or measure (twod.md section 4: the
  vtable's stores, the callers, the data references). zFont's reader only
  loads. The known reason for 0x00519e50 is now "no effect". My gap list's
  "bitmap fonts on every screen" was wrong.

## The campaign chain (2026-10-28)

- Branch mac4/campaign-chain (off main aaf668a).
- --campaign-chain (campaign_chain.cpp): 20 missions in about 2 minutes.
- Found the lance-fit order bug (campaign.md section 13): `done` not cleared
  by the cleanup. Traced through the resume's lost salvage: a Store::clear
  backtrace, then store_reallocate's inputs, then lance_slot's, then the
  slot count at the briefing's first refresh.
- Two driver bugs of my own on the way. Selecting row 0 of the game list
  resumed the oldest game, while the screen itself chooses the newest. And
  the shot path's buffer died before the frame: capture_next keeps the
  pointer.
- The store's 10 units at c4m2 are the real clock (both runs lost them
  once). The test compares the briefings without the unit totals.


## The campaign's end (2026-10-29)

- Branch mac4/campaign-end (off main be2040e).
- zgame::App::on_state_change (ours): called after each run_command; the
  chain prints "mw3: chain state X (frame N, credits flag F)" for each
  state entered.
- After c4bm6: Win Movie, Main, Credits in one frame (no movie yet);
  credits entered once; the chain presses CANCEL at 120 frames, then
  NEW CAMPAIGN at the main menu; done at the Pilot State.
- sdl_program_campaign_chain checks the sequence on the whole and the
  resumed run (campaign.md section 13).

## The salvage chain by kills (2026-10-29)

- Branch mac4/salvage-chain (off main 6936722).
- `--campaign-chain kills:N`: each Core frame, one hit on every enemy.
  The hit is zWeapon's impact with a stand-in shot of the player's
  (chain_hit, app_sensors.cpp), so the death and kill credit are the real
  ones. Then valves by region (reg_<valve>, else the MFB) and the
  objectives' buildings, with the player set near and facing them.
  Exit Mission once won.
- c1m1, c1m2 and c1m4 are won by their objectives. c1m3's two gdbldg
  nodes are never switched off (open, campaign.md section 14), so the
  chain falls back to --win at 1500 frames.
- Salvage: per kill against the victim's weapons; at the debriefing, the
  list is the mission's store. SALVAGE moves the weapon rows; the lab's
  criticals page fits one (taking a gun off if full); the next launch
  carries it.
- Fixed: the objectives' entity flag for mechs reads `active`, not list
  membership (test mission_c1m2_destroy_objectives_met_by_mech_kills,
  break 0 of 2). 0x004d7330 is shared and covers turrets and targets.
- Dead ends noted: the leg anims' handlers sit on their roots (the
  `healthy` child), and the building anims tick only when drawn.

## c1m3 by its objectives (2026-10-29)

- Branch mac4/c1m3-buildings (off main 19f6e43).
- In the data, `gdbldg` goes off in the fall's BOUNCE_SEQUENCE
  (bldg_to_land2 / bldghit3). The fall never landed because the program
  never set zEffect's AnimRunHooks, so the ground was null. Sounds and
  event 41 were also null.
- Wired ground, sounds and 41; the rest report to the census. Event 36
  (screen tint) is reached in c1m2, c1m4 and c4bm5: deferred, known key.
- mfb_reach: the valve_assign on net_01's 77->32 link, the bases' drive
  to MFB Charlie. The chain now gives the MFB command (two presses) from
  Charlie's region, and goes by the MFB at most once after.
- --frame-ms N (SdlClock stepped by the pump): the kills chain wins all of
  Op 1 by objectives in about 105 s; on the real clock c1m3 alone took
  7.5 min.

## The hooks audit (2026-10-29)

- Branch mac4/hook-audit (off main 39343a8).
- Tools and checks:
  - tools/analysis/hookaudit.py: the hooks structs' fields and their
    setters; it writes tests/hook_fields.inc.
  - program_hooks_set_after_a_mission_load_headless: every field is set
    after c1m1's load, or on tests/hook_allow.txt with a reason.
  - src/mech3/hook_census.inc and app_hook_census.cpp: census stand-ins for
    the pure skips, put in by MissionHooks::hooks_check (ours) at each
    mission's start and frame.
- 866 fields: 346 null before, 29 wired, 52 stand-ins, 265 null and allowed.
- Wired:
  - the puffers' tick node;
  - the arm restriction's view and torso inputs;
  - the view key;
  - the heat-shutdown ammo explosion;
  - the AI torso aim;
  - the AI nets on the ground;
  - the reticle aim state;
  - and smaller ones (hooks-audit.md).
- Found, not wired: vehicles get no weapons, because the 0x004b4660
  firepoint lookups were null and the manager's +0x80 needs porting. This
  is the next item.
- The Dominic tests' exact counts were re-taken: the puffers' rand()s.

## The vehicles' weapons (2026-10-29)

- Branch mac4/vehicle-weapons (off main 385cfa6).
- The builder 0x004b4660's finds by name are wired: `node_find` (0x004eb0d0)
  and `node_by_name` (0x004abb60, `node_find_prefix`).
  - test_node_find.py compares both with the exe on the six worlds, with
    150 made-up nodes each and finds from the null node with its report:
    7200 finds identical.
  - Break tests: the children searched last to first, the prefix find
    comparing whole names, and the null report dropped. All three are
    caught.
- The owner: the manager's +0x80 is 0 for a basic vehicle (vt+0x50), and
  +0x84 is the vehicle.
  - 0x004b3450 and 0x004b36e0 already read the two apart, so a vehicle's
    shot leaves its weapon's node and 0x0040ef30 is never asked.
  - `vehicle_mech` tells vehicles apart with `is_program_vehicle`. While a
    mech is being built, zEntity doesn't know it yet.
  - `ai_volley_pick` finds the brain through +0x84.
- The zone: `entity_x64` copied only the +0x64 record's count byte, so
  every shot queried zone 0. The vehicles' shots crossed nothing and flew
  through the player. It now copies the whole record.
- In play (`mission_c1m1_c1m2_vehicles_fire_at_the_player_headless`):
  - c1m1: 150 shots, 41 hits.
  - c1m2: 119 shots, 39 hits.
  - Every shot leaves one of its vehicle's weapon nodes and is of a weapon
    the vehicle carries.
- The kills chain: of the 20 vehicles killed, 10 salvaged their weapons
  (APC weapon 21 with 300 rounds; the Bulldogs' weapon 0 with 400), 8 came
  up none on the coin, and 2 trucks carry nothing. The test checks the
  three cases.
- What the zone fix did for mechs (measured 2026-10-29 for event 36): the
  player taken to each enemy mech in turn, the shots' impacts by owner,
  one-byte zone vs whole record. Mechs' impacts: c1m1 0/0, c1m2 0/4, c1m3
  9/4 (the runs split once vehicles fight), c1m4 163/163. Beams pass no
  zone (zwep_beam.cpp), so lasers never depended on it. For mechs the fix
  is nearly nothing; the vehicles' shots go from 2 to 51 impacts on c1m1
  and from 0 to 104 on c1m2.
- `--vehicle-hold` (ours) turns the player to face the placed vehicle, for
  the outbox shots. Placing the vehicle each frame reset its AI, so it never
  fired.
- Not touched: app_turret.cpp keeps its own copy of 0x004eb0d0
  (`find_first`), which has no null report. It could become
  `zcls::node_find`.
- Seen, not this item: c1m2's canyon floor goes black after about 4 s on
  main too (the lighting). It's queued after event 36 and ahead of #3:
  `mw3 --mission c1m2 --vehicle-ahead 45 --shot F --shot-frame 240` shows
  it (bright at frame 120).

## Event 36, the screen tint (2026-10-29)

- Branch mac4/screen-tint (off main c2ae162).
- Event 36 (0x005074b0) is ported as `zeff::anim_fbfx_color`.
  `AnimRunHooks.fbfx_color` became `screen_tint` (0x005a6730). The census
  stand-in and the known entry are gone; known_unported now expects 60.
- The weather manager's tint object is ported as `ztwod::ScreenTint`, at
  +0x28, vtable 0x005d0290:
  - 0x005a6620 stores its colour and alpha and shows it for 0 s;
  - its draw 0x005a53c0 goes to the ported 0x00558540;
  - it shares 0x005a5360 with the rain and snow through `ManagerObject`
    (ours);
  - `WeatherLayer`'s constructor adds it hidden, first in the list, as
    0x005a4590 does. The c4m2 rain and c4b snow tests now skip over it.
- test_screen_tint.py: 2000 events identical, through the quad at
  0x0057dd80, at 24-bit precision. Four break tests are caught: the
  rounding, the time left, the tint never shown, and the end at the
  duration.
- test_anim_run.py now runs 0x005074b0 for real, with only 0x005a6730
  traced. Its 360 frames reach no event 36, so it doesn't check the event.
- In play: c1m2's missile hits (impact_missle_mech) flash white to orange
  for 11 frames, about every 10.8 s from frame 211. Shots are in the outbox.

## The tint's range, then the black canyon floor (2026-10-29)

- Branch mac4/canyon-floor (off main c445a26).
- The flash's open point is closed (anim-run.md, The screen tint). c1m2's
  flash every 10.8 s is turret tur_srm03's SRM2 hitting the player.
  - Every event-36 tint sits inside an "if" on the player's distance to the
    def's root: 20 m for missiles, 17 for NARC, 12 for PPC, 15 for launches.
  - The port runs the same if (test_anim_run).
  - Over 6000 frames each of c1m2, c1m4 and c2m1, far hits (on the ground
    or on mechs, beyond 50 m) never started a flash. Nothing to fix.
- The black canyon floor: it isn't the lighting, the hits or the clouds. It
  is the static light bake's refresh.
  - zClass marks a node settled 160 frames after it last changed (+0xc4 =
    frame + 0xa0). The model draw 0x53f2e0 then bakes the model's static
    light (0x544ed0), stamps the node (0x4ed980) and reads the bake.
  - The program never set `DrawHooks.refresh_static`: the audit's census
    stand-in filled it with a call that did nothing. So the draw read an
    empty cache: black terrain from about 5 s in (frame 158 in --fly, 220
    in c1m2), on main too; checked in c1m2 and the fly view, probably every mission.
  - Fixed in gmod_draw: with the hook unset, the ported bake and the stamp
    run, and the stand-in and known entry are gone. Now the floor is lit
    at frame 240 (floor 56, cliffs 80; before 10 and 25).
  - Test: `mission_c1m2_static_light_baked_headless`, 47 drawn models baked
    by core frame 400. Break: without the fallback, 0.
  - Seen: in --fly the baked cliffs come out darker than the live ones
    (cliffs 20 vs 47). The fly viewer sets up the lighting its own way; the
    mission's baked look is close to its live one.

## The chain's seed (2026-10-29)

- Branch mac4/chain-seed (off main 94c739d).
- Noted, left as is (the lead): in `--fly` the baked cliffs come out darker
  than the live ones (c1 m2 at -1491 -5 -864: cliffs 20 baked, 47 live).
  The fly viewer sets up the lighting its own way.
- `--seed N` (mw3, ours) sets `crt::rand_fixed_seed`, which already existed
  for the headless tests. The kills chain runs with `--seed 1999`.
- The seed alone did not make runs repeat: the same seed gave c1m3 at 9351
  and 16058. Two program-side causes, both ours (no exe change):
  - **Stale handles.** `GameEngine` maps object addresses to handles. A
    freed object whose handle was not forgotten passed its old (lower)
    handle to the next object made at its address. The entities' sort
    (0x004d5ca0 / 0x004d7360) breaks its ties on the address, which the
    port reads as the handle. So a mech on a recycled handle jumped ahead
    of the others, and the AI's order changed with allocation luck.
    - Leaks fixed: the pocket commander's marker (deleted at the next
      mission's start) and the AI controls with their brains' params
      (`controls_delete`).
    - The general guard: the exe's operator new hooks (`on_new`) in the
      mech build, the basic vehicles, the field bases and the brain forget
      any handle still mapped to a new address. Every new entity gets a
      fresh handle, in creation order.
  - **The clock.** `--frame-ms` stepped on every look at the message queue,
    including passes that only took an event. Now only the passes that run
    the game count. Before, a run alone gave 16779 and the same run beside
    two others 7544.
- Now with `--seed 1999`: three runs in parallel and one alone are
  identical line for line (c1m3 at core frame 6636).
- The bake's link, settled: this branch with and without the static bake's
  fallback (NO_BAKE, a temporary switch) gives identical logs. The bake
  doesn't reach the game's logic. c445a26 and 94c739d themselves can't be
  compared with a seed, since they lack `--seed` and these fixes.

## c4m2's late cycles (#3), with the chain's split check (2026-10-29)

- Branch mac4/c4m2-cycles (off main c20b229).
- The campaign chain's save/resume runs (new:4 + resume:16 against new:20)
  ran on the real clock, unseeded, so a store's unit total could differ by
  ammunition spent (10 rounds in c4m2). All three runs, and the kills chain,
  now take `--frame-ms 33 --seed 1999`. Every mission's start reseeds rand,
  so a mission plays the same split or whole, and the check is strict:
  every briefing line identical, units included. Two runs passed, and the
  test takes 102 s instead of several minutes.
- #3, c4m2's late +12%. The census on c4m2, 360 s, MW3_CENSUS_STATS; the
  ratio is cycles 13-18 against 3-12.
  - Today's main: level (0.997; 1.003 and 1.008 in later runs).
  - Bisected over the merges since item 22:
    - 346616a: 1.068. be2040e: 1.085. 39343a8: 1.091.
    - 385cfa6 (the hooks audit): 1.013. c445a26: 0.991. 94c739d: 1.012.
  - On 39343a8, `sample` early and late: the AI's `clear_line` sight-line
    queries grew fourfold, from a fight the scripted walk met late. On
    main the same checks are high early and low late. Game work, not a
    leak.
  - Ruled out on main, each switched off: the puffers' tick (0.989), the
    AI nets' ground (1.009), the AI torso aim (1.009).
  - Both builds are Debug, so the profiles are dominated by unoptimised
    STL in the 16-bit layer's fill and copy (`unlock_back`), early and late
    alike.

## A mech's collisions with other entities (2026-10-29)

- Branch mac4/mech-collide (off main c5e5e49).
- Wired the vehicle path of 0x004237c0: entity_dead, entity_target,
  entity_body, entity_team, entity_damage, body_velocity, body_spin,
  body_mass and entity_matrix (`wire_vehicle_collisions`, app_vehicle.cpp).
  The slots were read from the vtables: the basic vehicle's 0x5c88e8, the
  field base's 0x5c23d8, and the controls' IControls tables
  (movement-collision.md, In the program).
- `ai_reverses` is the controls' IControls vt+0x70: `MechControls::on_path`,
  false for the player's and the base's, true for `AiControls` (0x004e7790).
- 0x00476550 is ported as `Skis::zone_update`. The old `SkisCalls.zone`
  became `camera_zones` + `set_zone`. test_skis.py runs it for real; its
  made-up ground now holds valid zone records. A break is caught.
- The census stand-ins and the four known entries are gone
  (known_unported expects 55); hook_fields.inc is regenerated.
- In play (`mission_c1m2_a_mech_bumps_a_vehicle_a_mech_and_a_wreck_headless`):
  - A vehicle blocks (6.4 m), is damaged (1.0 -> 0.876) and is shoved.
    Before, the player walked through it.
  - An enemy mech blocks (8.9 m) and both take damage, as before.
  - A Bulldog's wreck is walked over: only foot probes meet it.
- The zone record against the shots: impacts by mechs and vehicles on c1m1-c1m4
  are the same with and without it (0/0, 4/80, 4/10, 92/0).
- The Dominic tests' exact numbers were re-taken. The change is the AI's
  wall reverse: with ai_reverses off the old numbers come back, while the
  zone record and the vehicle hooks change nothing there. Contacts 2979 ->
  2163, aiming 68 -> 96, firing 901 -> 841; unordered: aiming 44 -> 43,
  firing 657 -> 658, moved 70.446 -> 69.151; pocket: closest 120.729 ->
  120.879.
- The hooks allow list lost entity_team, body_mass and entity_matrix (set
  now), and the census lost the vehicle path's inert stand-ins.
- Seen, left: test_skis.py with the camera lists drawn from the walks' own
  generator met a 2-ulp difference in a probe's height (c4b walk 3 frame
  164, skis +0x98: 0.0529936 against 0.0529937). It's a rounding
  difference outside the zone code. The camera lists now draw from their
  own generator, so the walks are the ones main tests, and it doesn't show.

## The cockpit HUD's power down and up (2026-10-29)

- Branch mac4/hud-power (off main 552b13f).
- 0x0043bb20 / 0x0043bb50 ported as `Hud::power_down` / `Hud::power_up`
  and wired (HeatCalls). The census stand-ins and the known entry are gone
  (known_unported expects 54).
- test_hud.py runs them on the built HUDs: 20 cases identical, with the
  power step in about two of three. Breaks caught: the reticle left shown,
  the kept state ignored.
- In play (`mission_c1m1_power_down_turns_the_hud_off_and_back_headless`):
  off with the reticle hidden in the frame the mech powers down, back the
  frame after the power up. Before, the HUD stayed on.

## The fall camera (2026-10-29)

- Branch mac4/fall-camera (off main e13afa5).
- 0x00413af0 / 0x00413b70 ported as `PlayerView::mech_falls` / `mech_stands`
  and wired (MoveCalls). The [0x00605624] mode-switch branch is dead: the
  byte is never written. The census stand-ins and the known entry are gone
  (known_unported expects 53).
- In play (`mission_c1m1_a_fall_fixes_the_cockpit_camera_until_up_headless`,
  through the new test accessors mech_knock_down, mech_get_up and
  program_view_mode): the cockpit camera is fixed from the fall's first
  frame and turns again on getting up.
- No new differential: the two are a target check and one virtual call
  each, onto the already ported CockpitCamera::on_switch (0x00421d50).


## Any data version (2026-10-30)

- Branch mac4/data-versions (off main cd45f5f).
- `mech3::detect_data_version` (ours, src/mech3/data_version.cpp): the 26 data
  files the 1.2 patch changes plus Mech3Msg.dll, by size and CRC against
  1.0's and 1.2's; `mw3` prints it at start. "No MechWarrior 3 install"
  no longer says 1.2.
- game/mw3-1.0 from the disc's DATA1.CAB + DATA1.HDR (unshield, the groups
  merged; video linked to 1.2's). The two installs differ in exactly the
  patch's files. No 1.1 in the backups.
- MW3_GAME_DIR is a CMake cache variable; build-10 builds against 1.0. On
  1.0: 222 tests pass, the SDL tests pass, the kills chain wins c1m1..c1m4
  on 1.2's frames. The pinned outcomes (Dominic's three, the HUD anchors'
  blocks) are checked on 1.2's data only.
- FIX (bug): a message the table lacks reads as "" (the exe's null reached
  strncpy for the default ActiveCtlCfg).
- discoveries.md: what the 1.2 patch changed (weapons, torso twist, heat
  sinks, c1m1's objective, the c2m2 jet ceiling, sounds, HUD layouts).
## Playtest W6: the reticle stays green over turrets (2026-10-29)

- Branch mac4/reticle-yellow (off main 9df3631).
- 0x00443d20 turns the reticle over (yellow) when the picked node's object
  has its ZEntity +0x08 flag set and the aim is in range. The hook read the
  flag only for a mech, so turrets, vehicles and targets.zrd objects logged
  "object+8" and stayed green. Red (the lock) has no such test, so it
  worked. Now `entity_active_flag` answers by class: Vehicle (mechs and
  vehicles), tur::Turret, ai::CZAiTarget (targets and traps).
- Test `mission_c1m1_reticle_turns_yellow_over_each_kind_headless` through
  `program_hud_aim_at`: turret, vehicle, mech, target each over in range,
  off out of range and dead. Breaks: the mech-only read (3 fail, Owen's
  bug), the flag ignored (4 fail).
- Shot: `--turret-ahead 30 --tap 12@150:175`, c1m1's nearest turret,
  before green, after yellow (outbox).
## The damage decals (2026-10-29)

- Branch mac4/damage-decals (off main a3a6eb3).
- 0x00474d90 `SkinSet::damage_decal`, 0x00474cd0 `Skin::find_texture`, the
  cookie 0x00542730 (`zmod::cookie`) and zImage's named image cache
  0x005179d0 / 0x00517b10 ported; 0x00414370 and the decal wired
  (DamageCalls). The SkinGlobals fetch, release and entry release now fall
  back to the ports (the entry release is zImage's 0x00518b30, which only
  ran once the damage skin loaded). Census stand-ins and the known entry
  gone (known_unported expects 52).
- test_decal.py: 3000 cookies and 3000 decals identical. Breaks caught:
  565 green's sign bit, the 555 threshold, the hole pick's n - 1, a cached
  entry matched by its image.
- Found on the way: the program never had holes. InstallOptions 0x101 (2 MB
  card set only) made the mission load pick the reduced mech textures
  (rmechtex16) and turn HOLES off. With the lead's go it is now 0x070707, a
  full install; the bits are mapped from the exe's readers and SETUP.INS
  (settings.md).
- In play (`mission_c1m2_damage_decals_are_painted_headless`): 8 holes, about
  150 decals at the hits' UVs on the Mad Cat's textures. My first guess, that
  the UV stays 0,0, was wrong: flag-2 materials always store it.
- `--mech-ahead M` (ours) keeps the player near the first enemy mech, for
  shots.
- Known duplicate noted: SdlTwodHost's image cache is a second 0x005179d0.

## A Windows player's first run: movies and music (2026-10-30)

- Branch mac4/first-run (off main aea13d3).
- CMake finds FFmpeg by pkg-config, else a prebuilt shared build in
  deps\ffmpeg-*shared* (or -DFFMPEG_DIR) and copies its DLLs beside
  mw3import.exe. windows.md names BtbN's LGPL 8.1 shared build (the Mac has 8.1.2).
- mw3import: --missing (only movies without a .fmv; the .fmv written last),
  "progress K N name" lines, --cue DISC.cue [--music-only].
- First run (first_run_import.cpp, ours, FIX (A)): a plain start with
  movies missing and mw3import beside mw3 runs it through SDL's process API
  with a small progress window; import-failed marker; --import / --no-import.
  Mac: 19 movies in 59 s into a fresh write dir.
- Music: platform/cue_audio (read_cue, write_cue_audio): each AUDIO track
  from INDEX 01 as music\trackNN.wav. PM's track 2 matches the bin past its
  2 s pregap. Test cue_audio_writes_each_audio_track_from_index_01 (break:
  the pregap kept, 3 checks fail).
## Playtest W8: the dock never lets go while the player waits (2026-10-30)

- Branch mac4/dock-release (off main aea13d3).
- Not reproduced. In the exe's logic a player on the dock point or a slot
  blocks nothing: the vehicles' collision count (+0x214) is never written,
  so a base's sweep crosses nothing but a slide's side points (checked in
  the port: zero hits all through c1m1, the base drives through the mech);
  a field base scans for nothing, so no station keeping; the contact wants
  the player within 12 m and powered down.
- The port's dock ran to idle with the player on the point (standing, or
  powered down early), on each slot, 11 m out on a base's drive, and on
  c1m3's slots. Kept as `mission_c1m1_dock_runs_with_the_player_waiting_headless`.
- Asked the lead for Owen's details; a standing (not powered-down) player
  is the one wait, by design.
## Playtest W10: c1m3's third MFB drives past the op point (2026-10-30)

- Branch mac4/mfb-overshoot (off main e1bef87).
- The exe parks all three: the request (0x0045a780) sends base i to link i
  of "MFB Charlie" (nodes 22..24, each a deploy node naming its base) with
  the brain's go-to (0x004c84b0), whose way finder starts from the vehicle's
  position (0x005a85e0). The port's `pilot_of` never set that position, so
  every go-to failed and the bases walked net_01's links by chance (the
  tour through MFB 2..4), taking whichever slot they reached. A port bug,
  not a FIX candidate.
- Fix: `pilot_of` gives `vehicle_position` (app_mech.cpp, the node's data).
  Only the dock's request calls go_to, so nothing else changes.
- Test `mission_c1m3_field_bases_park_at_charlie_headless`: all three on
  their own slots (within 1 m) and the dock's places step done, in about
  half the time. Break (no position): 3 checks fail, base 02 169 m off,
  base 03 805 m off.

## SDL3_sound on Windows (2026-10-30)

- Branch mac4/sdl-sound-win (off main c2719e1).
- build.cmd: SDL3_sound_ROOT from deps\SDL3_sound-* (the -devel-VC.zip);
  CMake already finds it by config. On Windows mw3's runtime DLLs
  ($<TARGET_RUNTIME_DLLS>) are copied beside mw3.exe after each build.
- mw3soundcheck --music FILE: the music player on one file for 3 s; on the
  Mac a 5 s .ogg and .flac both decode and play.
## Playtest #23/#24 again: the fire lag (2026-10-30)

- Branch mac4/fire-lag (off main).
- No lag in the port: from a full charge a click fires on the next frame,
  headless (10/10, mouse and key) and in the program (--click in a visible
  window: pressed after frame 300, fired at 301; --hidden has no focus, so
  the mouse is refused there, as DirectInput's foreground mode).
- The exe's rules (weapons.md, Trigger and modes): a press that fires
  nothing is dropped; held, a beam fires when ready. The manager starts in
  chain fire (mode 0): only the group's current weapon fires a press. That
  is the likely "a charged weapon lags": another weapon full, the current
  one charging. Asked Owen for the mode and group.
- The short-click test asserts the one frame.

## The campaign by its objectives, Ops 2-4 (2026-10-30)

- Branch mac4/chain-ops (off main f26c451). Seed 1999, frame-ms 33.
- The chain's two new steps (chain limitations, not port bugs):
  - an open reach_area: the player is set on the ground at its nav point
    (c2m2's Charlie, in a cave: ground_at below a ceiling);
  - a valve a net link sets (valve_assign edge): the MFB command is given
    from the link's start node, so the nearest dock point is that one
    (c3m1's MFB Charlie, c3m3's MFB Able). The net files are unloaded
    once built; add_ref/release around the read.
- Each objective's completion frame is logged. c2m4's destroy
  mineturret01 is met at frame 0: the chain killed it in c2m1, and the
  op's animation log restores it destroyed. That is the exe's rule.
- Every campaign mission won by its objectives (core frames): c2m1 3833,
  c2m2 201, c2m3 135, c2m4 70, c3m1 5545, c3m2 204, c3m3 4305, c3m4 190,
  c3m5 66, c3m6 183, c4m1 65, c4m2 73, c4m3 190, c4bm4 207, c4bm5 193,
  c4bm6 207; the census 0. c4m3's tur_ult03 and tur_ams01 are met at
  frame 0 like c2m4's turret (killed in c4m1, same world).

## The campaign regression test (2026-10-30)

- Branch mac4/chain-test (off mac4/chain-ops 0a57bd3).
- `sdl_program_campaign_by_objectives` (MW3_CAMPAIGN_ALL=1): kills:20
  from a new pilot, 295 s; 20 of 20 by their objectives, the census 0,
  the credits. Frames printed, not pinned. Break (no valve-link step):
  c3m1 and c3m3 named as --win, 913 s, fails.
- The full fresh run reached veh.vt+0xdc x4: c2m1's teleport_thor
  places thor_01, a mech, and the valve hooks placed only basic
  vehicles. Now Mech::set_place (0x00418f30). Test
  `mission_c2m1_teleport_valve_places_the_mech_headless`; break: 1566 m
  off and the census 1.

## A failed mission's retry (2026-10-30)

- Branch mac4/restart-state (off mac4/chain-test 86099c3). Free item:
  community-issues.md's "a restarted mission keeps state".
- The exe's retry (0x0047d030, failed CONTINUE): cleanup (1 for the two
  first missions, the world unloaded), anim reset 0x0050ad60(1), +0xb0
  cleared (the save read back). A clean restart by design.
- The chain's retry:N fails each mission at core frame 75 (the pause's
  REPLAY) and compares every node's active flag at frame 30.
- Port gap 1: the Win State read the loss and cheat flags through peek8
  (never written), so a lost or cheated campaign mission counted as won.
  Now the missions' fields. Test
  campaign_lost_or_cheated_mission_is_briefed_again_headless (break: the
  next briefing Op 1 Mission 3, 3 games).
- Port gap 2: 0x0050ad60 was unported; c1m3's retry kept two destroyed
  buildings that never fell again (unwinnable). Ported as
  zeff::anim_reset_all; differential test_anim_reset.py. Op 1's four
  retries are won (c1m3 at 6490); a few effect nodes still differ, as
  the exe's reset leaves them.

## The peek sweep (2026-10-30)

- Branch mac4/peek-sweep (off mac4/restart-state d9f5bac).
- tools/analysis/peeks.py: every read of an exe global (peek*, field* on a
  constant, the hooks' peek and [0x...] stand-ins) with its writers, the
  address's other mentions and whether a stand-in is overridden.
- ProgramHudHooks' unported was the base's silent one: the census never
  saw a HUD stand-in. Now it reports; Op 1 reached 7 keys, all answered.
- Wired: the compass and radar flip on a scaled map, the MFD and TARGET
  modes, the DISHONORABLE title, the one cheat byte [0x00609994], the
  zoom's round for Clan mechs, the powered-down HUD, the radar amber.
  Left with reasons: multiplayer's game (0), the HUD debug numbers.
- False positives the script cannot fold: 0x00609858 (written through
  the main menu's table); overridden in the program: 0x006098d4 and
  0x0073655c (update_shadows), the screen-mode cockpits, frame_dt.
- Test mission_c1m1_hud_reads_the_ported_globals_headless (break: all 6
  checks fail). The campaign: 20 of 20 by objectives, the census 0.

## The game's other modes: Instant Action (2026-10-30)

- Branch mac4/modes (off mac4/peek-sweep). `--action-chain all|cN|cN:S`
  (frontend/sdl/modes_chain): 20 cases, each a lost and a won round,
  CONTINUE then ABORT, the IA Dummy State's tallies checked. 111 s, all
  20 as the exe tallies, census 0 (campaign.md section 15).
- Ported from the census: the IA launch's pilot save (program_pilot_file),
  0x00537f20 material_sweep, 0x005392c0 models_free_unused; differential
  test_model_sweep.py (six worlds identical; breaks: the marks kept fails
  at call 659, the use count test off frees 2231 for 684).
- Tests: sdl_program_action_chain (MW3_CAMPAIGN, 114 s), the launch save
  in instant_action_menus_to_wave1_tallies_headless (break: fails).
- Chain bugs on the way (not the port's): the scenario rows have no
  None, the custom flag is +0x16f9c set, the wave count read from frame 2.
- Training (same branch): `--action-chain training`, t1's four missions by
  their lessons in 47 s, census 0 (campaign.md section 16). Ported or
  wired: the AI mechs' node names (0x005a8390's name), the heat's static
  0x0043c1f0, the zoom view's prepare, the pocket view's green dim
  0x0055a740; FIX (bug) the pocket mode at the mission's end (0x0046ab10,
  a use after free: exit 139). Tests: mission_c1m1_training_pieces_headless
  (break: 0 of 1 named), sdl_program_training_chain.

## Joysticks and force feedback (2026-10-30)

- Branch mac4/joystick (off mac4/modes 9bb4575).
- Joystick: the raw path checked on a virtual flight stick (ranges, dead
  zones as DirectInput's, hat, buttons). FIX (A): a gamepad SDL knows is
  read in DirectInput's XInput layout (the raw one pinned the default
  twist); missing axes read 0.
- Force feedback: the object at [0x00608620] (misnamed the network game
  and statistics before) ported (mech3/force_feedback, app_force.cpp), its
  effects from force_eff.ifr. FIX (A): IFORCE2 as platform::ForceDevice,
  SdlForceDevice playing the parts as rumble (input.md section 7).
- Finds: the machine gun, flamer, LB 20-X, NARC, TAG, Ultra AC/2 had no
  force feedback; i_fall never loaded (the file's "impact fall").
- Tests: three virtual-device tests (stick, pad, pad through zInput), the
  rumble device on the shipped file, the object on a recording device and
  in c1m1. Breaks: the pad raw; the calls unwired.

## The 1.2 patch import (2026-10-30)

- Branch mac4/patch12 (off main 8f59259).
- The official patch: an InstallShield 5 setup folder; data1.cab holds the
  31 files whole (all equal to game/mw3-1.2; unshield the cross-check).
  `mw3 --patch ZIP|DIR` (mech3/patch_import, tinfl vendored in
  third_party/miniz) writes the 28 the port reads (force_eff.ifr added to
  the version table: it differs too) into <write dir>/patch12, mounted
  before the install; checked by size, CRC and SHA-256; all or nothing.
- The patch for the tests: game/patch-1.2/ (the folder and a ZIP of it).
- Find: 1.0's exe and ifr have no death effects and call the landing
  "impact fall"; 1.2 renamed it and added i_fall, which its own file names
  "impact fall".
- Test: patch_import_upgrades_10_data_from_the_official_patch (on build and
  build-10). Breaks: the descriptor's data offset, the chunk length, the
  SHA-256, the ZIP CRC.

## The Indeo helper (2026-10-30)

- Branch mac4/indeo (off mac4/patch12).
- mw3indeo.exe (tools/indeo, 32-bit, build_indeo.cmd): Ir50_32.dll loaded
  and ICInstall'd as a function driver, AVIFIL32/MSVFW32 answering the
  movie player's calls over a pipe (platform/vfw_protocol.h); `--check`
  for the PC. Cross-compiled here with Homebrew's mingw-w64 (i686) only to
  check it builds; never run here.
- mw3: sdl_vfw.cpp (the client), app_fmv.cpp routing each stream to the
  helper or the imported files; `--vfw-helper`/`--no-vfw-helper`; the
  first-run import skipped while the helper is up.
- Test: sdl_movies_through_the_indeo_helper_pipe with mw3indeo_fake (the
  same messages answered from an imported movie).
## The fall's force feedback (2026-10-30)

- Branch mac4/ifall (off main 04aff71). FIX (bug), behind bugs: no
  "i_fall" in the file, "impact fall" taken (force_feedback.cpp). Tests:
  the recording device with bugs off and on, the real file's effect
  (sdl_force_device_plays_the_ifr_effects_as_rumble). Break: the lookup
  off, 2 checks fail.

## W12, the field bases on the radar (2026-10-30)

- Branch mac4/w12-radar (off main). A port bug: the field base's class was
  given the vehicles' base class's vtable (0x5c8d48, no blip, no tint);
  its own (0x5c23d8) has the vehicles' blip 0x45b970 and tint 0x4ad390.
  entity_classes (blip table), app_sensors (the tint), ai.md's table,
  vehicles.md. The report counts the field bases' blips and their on-radar
  contacts; the repair test waits on the dock point powered up and checks
  them driving in and parked. Break: field_base without a blip.
## Visual pass 3 (2026-10-30)

- Branch mac4/pass3 (off main 04aff71). 210 shots: 21 missions at 1080,
  720 and 480 (start, outside, enemy), objectives and pause in three
  missions, the c1m1 briefing and debriefing (playtest.md, Visual pass 3).
- S15 fixed (ours, the HUD scale's regression): the fade's still taken at
  the mission's canvas and drawn after the switch to 640x480 fell through
  to the census; now scaled (GameEngine::image_draw), tested in
  frame_lock_lays_the_layer_and_the_tint_goes_through_the_ports (break:
  the old fall-through, 1 check). S16..S18 reported.

## W16 and W8, the field bases' second call (2026-10-30)

- Branch mac4/w16 (off main). c2m2's routing right (all four points
  reached). The jam: called to the point they last docked at, the bases
  stand on their slots, nothing to drive, the valves never set, the places
  step waits for good (the exe's). FIX (bug) behind bugs: counted as arrived
  at the request; a base still 30 s short of its slot given up on.
  Test: mission_c2m2_field_bases_answer_a_second_call_headless (two seeds,
  the original, base 02 held both ways).
## W18, the AMS (2026-10-30)

- Branch mac4/w18 (off main). The mech's AMS frame 0x00419200 was only a
  hook (UpdateCalls::equipment_update), never set and with no fallback:
  ported as update_ams (mech.cpp) and run when unset. Test helpers in
  app_weapons.cpp (program_mech_ams, _set, program_fire_at). Test:
  mission_c1m1_ams_shoots_down_incoming_missiles_headless (break: the
  fallback removed).
## The bug fixes switch (2026-10-30)

- Branch mac4/bugfix-switch (off main). Audit: CHANGES' play list against
  the code: folded into bugs the deep-water curve (its own flag), the
  heat-sink callout, the fade-in typo, the animations' last-def search and
  missing-prerequisite fix (with its seam). Crash fixes, (A)/(B) and
  per_second stay on. mw3.cfg bugfixes=0/1 and --original (wins). The
  differentials that patched the exe to match a fix now compare the
  original (test_damage.py's 0x45fbc0 stub, test_puffers.py --fixed for the
  fixed path, probe_set_bugs); test_fixes fix_heat_sink_critical_is_called_out,
  sdl_program_bugfixes_switch.
## Artemis IV's double tracking (2026-10-30)

- Branch mac4/artemis (off main). FIX (bug), behind bugs: the homing's two
  reads of [0x9a17d8] (TURN_RATE 0x5859f5, the FCS climb's pitch rate
  0x585b08) take the shot's +0x5c0 copy (zwep_shot.cpp home). +0x5c8 (the
  terrain following) was always read from the shot; +0x5c4 by nothing.
  Test: fix_artemis_fcs_doubles_the_turn_rate (breaks: the global always,
  the copy always).
## The command map's pick (2026-10-30)

- Branch mac4/map-pick (off main d02a386). A port gap, not a fix: none of
  FrameCalls.map, map_size, map_mouse, map_pick was set (the audit missed
  fc.map, a ternary to a census note), so with Shift+R's map up a click
  ordered at the reticle's aim. Now MissionMap::pick (0x4587b0) and the
  four hooks in app_pocket.cpp; ai.md 17.6c, symbols.csv.
- test_map_pick.py: 3000 picks against the exe bit for bit (ground
  stubbed at 0x4edab0, world or none); BREAK=1 (x and z swapped) 2583
  differ. Play: mission_c1m4_pocket_commander_sends_dominic_to_a_map_spot
  (the map up, Dominic brought onto the map, the cursor on the pixel 200 m west of him, the marker
  on the ground there, order 10, he walks to it).
## Installing from the disc (2026-10-30)

- Branch mac4/disc-install (off main fa9aab7). `mw3 --install-from PATH
  [--install-to DIR] [--patch P]`: a drive or folder, a .iso (2048 or raw
  2352), a .cue (first data track; MODE1/2352, MODE2/2352, MODE1/2048).
  mech3/disc_install (Disc: ISO 9660; install_from_disc), patch_import's
  IS5 reader made streaming (each_installshield) with each file's MD5
  (+0x2a; all 31 of the patch's match), DataFile.sha256_10, CueTrack.mode.
  assets.md (Installing from the disc), program.md, README, the Windows
  README, CHANGES.
- Owen's ISO (from the backups' zip, 37 s): 126 cabinet files + 19 movies
  identical to game/mw3-1.0, 16 of 16 1.0; patched 28 of 28 1.2; mw3
  starts c1 on it. PM's Redump .cue: 102 files = game/pm, track02.wav.
  Not done here: Owen's MW3 Redump .cue on the PC.
- Tests: disc_install_from_an_image_a_cue_and_a_folder (breaks: no MD5,
  20 checks; raw sectors without the 16-byte header, the cue fails);
  disc_install_from_the_mw3_disc with MW3_DISC.
## Pirate's Moon decompiled and sorted (2026-10-30)

- Branch mac4/pm-diff (off main fa9aab7). No porting. Ghidra headless
  (tools/ghidra/DumpFunctions.java) on PM into analysis-output/pm-decomp
  (11,344 .c and functions.jsonl), the same dump of MW3 into
  pm-diff/mw3; pm.asm, pm-func2file.json. tools/analysis/pmdiff.py pairs
  them (6,875, 86% of MW3's 8,030 after the unwind funclets): 1,488
  identical, 1,531 changed in calls/strings/constants/imports, 3,856 in
  instructions only, 1,692 new, 1,155 MW3 only. PM was rebuilt with
  VC6 (MW3: VC5), so instructions differ nearly everywhere.
  tools/analysis/pmdata.py: the data. pirates-moon.md (two sections),
  assets.md (Pirate's Moon), tools/analysis/README.md.
- Readers on PM's data: archives v2, gamez 41, anim 50, mechlib 41
  rejected; interp, textures, motions read (checked with a local build
  taking v2, not committed).
- Found on the way: twod.md says 0x005a3040 has no caller; MW3 calls it
  four times (reported to the lead).
## TextBase rechecked (2026-10-30)

- Branch mac4/textbase (off main fa9aab7). aaf4c7c's conclusion stands:
  0x005a3040 (the centring) has four callers (FontText's three set-texts,
  TextBase's printf), all behind +0x238, which nothing in 1.2 sets (ctor
  clears, copy/assign copy; the other +0x238 writes are other classes').
  twod.md section 4 corrected; the census note for 0x00519e50 says
  "drawn or measured". Nothing to port.

## W20, the jump jets' lighting (2026-10-30)

- Branch mac4/w20-jet-fog (off main d7831d0). The jets' anim (jumpjets)
  decoded: puffs and two saturated flicker lights only outside the
  cockpit (anim block +0x3c) or for a mech over 13 m away. Found and fixed
  the GPU layer's fog colour: per draw (Run::fog), not the frame's last;
  test with a break. Owen's case not reproduced (one fog colour every
  frame in c1m1/c2m1/c3m1 with a Thor's jets, cockpit and outside): asked
  for his view and mission. The Mad Cat (the debug start's default) has
  no jets; the jets' key is J (0x24).
## W23 and W24, c2m3 (2026-10-30)

- Branch mac4/w23-w24 (off main d7831d0). Both as the original (data);
  docs only. W23: the tunnel is the second warning region
  (reg_inside2_c2m3 sets inside_c2m3, its far-end box reg_outside2 the
  fail), the final "Enter Tunnel" is reach_area 10 m (3D) round Charlie,
  150 m west of the region; the warning valve is never cleared (plays
  once). W24: the objective's firefly_01/02 do not exist (the mission's
  are 03/04; missing names count as destroyed) and the APCs are on no
  list: a fixes.md candidate.
## W31, jets that barely lift (2026-10-30)

- Branch mac4/w31-jets (off main 98665790). Skis::integrate (0x477940)
  sets a probe under the ground to (ground - y) / max(dt, 1/30 s); above
  30 fps that is slower than the jets' climb it replaces, and walking up a
  slope the probes stay under the ground every frame, so the mech skims.
  The exe does the same. FIX (B) behind per_second: above 30 fps the
  speed over the frame's own step (as 30 fps had it); the hard landing
  still by the 1/30 s speed. A first version (the catch-up only raising
  the speed) left a leftover climb at 60 fps and changed Dominic's c1m4
  run more (his target survived); this one keeps it destroyed, his
  aiming 96 -> 145 and firing 841 -> 792 frames. Test: 25 states with fixes on and off (frame steps
  1..100 ms, a 400 ms hitch, fuel 0.1/0.26/0.3, hot, walking); the
  original's path skims at 60 and 144 fps walking. Fuel just under 0.25
  still lights: the ground refills it while the key's thrust ramps.
  Destroying a leg (mech_destroy_part 9/10) did not change the gait in
  the headless run, so it is not among the cases.

## W32, c3m1 HQ arrow and TARGET view (2026-10-30)

- As the original, docs only (on mac4/w31-jets). The yellow bottom arrow
  is the nav point's (0x479ef0), Mission Start behind-left: behind the
  camera 0x53cf10 returns the bottom code with x squeezed from +-5000.
  TARGET's view for a targets.zrd object needs the record's `display`
  (0x4d3350); no shipped record sets it.

## W33, the reticle green past 800 m (2026-10-30)

- Branch mac4/w33-reticle (on mac4/w31-jets). HudReticle::aim
  (0x444350) queries near..far clip (800 m); ER large reaches 1000 m.
  FIX (bug) behind bugs: the segment reaches group_range (0x4b1bd0)
  when longer; the miss's point stays the far point. Test helper
  program_hud_pick_at (project, reticle there, aim, aim_state). The
  test needs the player 150 m up: at ground level c1m1's hills or
  buildings block every 900 m line round the Owens.

## W34, the AI's missiles (2026-10-30 night)

- Branch mac4/w34-ally-missiles (on mac4/w33-reticle, after f27414ce, the
  W31 revision). Firing's defaults: the exe's constructor 0x4b6570 copies
  11 words from 0x5e8868, which the CRT initializer 0x4b6500 fills (time
  FLT_MAX, waits 3/6/1 s, cosines of 20/30/45 degrees, range 50). The port
  had 0 (the image's), so the AI fired at once and up to 90 degrees off;
  the launcher clamp (0x4b36e0, 27 degrees) then sent shots along the
  part's facing. Fixed in zai_firing.h. test_ai_brain.py runs 0x4b6500
  and probe_ai_brain.cpp writes the fields (was hard-coded zeros); break
  caught. Headless: c2m1, the strider facing the player 150 m off; 13 of
  23 missiles past 27 degrees before, 0 of 5 after. Test-only hook
  program_watch_shots (WeaponCalls::test_fired). Pinned AI runs re-pinned
  where the slower firing moved them (see the commit).

## State for the morning (2026-10-30 night)

- Holding after W34 (the lead: one worker overnight; resume in the morning).
- W32 reopened (not started): Owen Q-targets buildings (the HQ) that are
  not on the radar unless targeted, and their radar blip (maybe Shift+R's
  map too) sits in the wrong place. Check the radar position read for a
  targeted building or targets.zrd object (node or centroid vs a mech's
  +0x14, offsets, world vs local) against 0x43dd00. Test: c3m1 hqbuild
  Q-targeted, its blip at its world bearing and range like a mech's.
- Release work: tools/deps/build_deps.sh (mac4/deps) is waiting on Owen's
  permission to build the downloaded code (the classifier denied it here;
  not worked around). The WSL packages are installed (A, the lead ran the
  apt command). C approved (appimagetool, linuxdeploy from GitHub
  releases). Then: point make_app.sh at the prefix (universal, macOS 12),
  bundle mw3import with FFmpeg, the Linux build, tests and AppImage.
- Branches stacked: mac4/w31-jets (c967cb18) <- mac4/w33-reticle
  (99c4f4af) <- mac4/w34-ally-missiles (f27414ce W31 revision, then W34);
  merging w34 brings all three.
- The full run on the W34 tree: 257 tests, 13 failed checks in 5 AI tests
  (owens_runs_its_frames 8404/8410, lancemate_forms_up_only_in_training
  9505, and the three c1m4 Dominic tests: takes_the_order 10661-10665,
  fires_on_a_target 10997/10998, shoots_a_near_enemy_unordered (now aims_at_...)
  11309-11323). Not only pins: the Owens no longer fires on the player 30 m
  off, and Dominic no longer fires on a near enemy (11319: firing 0).
  Traced (the Owens): its volley plan is on a field base about 500 m off
  whose line is blocked (from y 8 to a centroid at y -12, through the
  ground); the plan's phase 2 passes (phase 2 skips ready()), phase 3
  waits on ready() = the clear line for the 6 s delay, then rests 1 s and
  the pick takes the same field base again, so the player 30 m off is
  never taken. With the old zeros the delay ran out each tick and the pick
  ran each tick, which is why the tests saw it fire. Open: whether the
  pick (0x4b6790) choosing a blocked target over a near clear one is the
  original's (its differential test passes) or our world state (the field
  base's centroid underground, its zones). Held for the lead's call
  before any re-pin; tree clean, W34 commit c333cd9e unchanged.
- Settled (the lead: finish W34 tonight, same item). The pick is right: under
  40 m the distance term is 100 and a blocked line only multiplies by 10, so
  the player 30 m off wins; the Owens' base plan was picked before the
  player was moved, and phase 3 keeps a plan to its delay (6 s). The y -12
  is the exe's: 0x458a70 -> 0x4d6c90(node, 0, 0, 0) -> 0x5a8390 finds or
  makes the centroid at the node's origin and nothing raises it (mechs get
  height / 2), and the base stands on ground at -12. Not a clear-intent
  bug either way (the delay is the aim time; the low centroid may be
  unintended, a candidate at most). The five tests re-read:
  - owens_runs: phase 3 at frame 200 (the base plan waiting out the line);
    in the bump window the base plan held to frame 379, the rest, the
    player picked about 10 m off and turned to, no burst before 500; the
    firing and shooting counts moved to the attack stage (500..2400:
    bursts of about 157 frames, rests of 4 s).
  - lancemate training: moved under 2 m (his feet shuffle 1.3 m turning in
    place about 110 degrees in his rests; under the zeros under 1 m).
  - Dominic's order and target runs: re-pinned (no shot before the order,
    103 frames aiming, the player held 186 frames; the first hut ghsD5, the
    hut still destroyed).
  - shoots_a_near_enemy_unordered renamed aims_at_a_near_enemy_unordered:
    his torso at its twist limit (1.92 rad) stays 55..68 degrees off the
    enemy behind him, short of cos 45, so he aims (639 frames) and never
    fires; under the zeros he fired 658 frames along the torso's facing.
  Break: the zeros back in, 5 of the 6 firing tests fail (the lancemate's
  is about the formation, not the firing). Full mw3tests 257 tests, 0
  failed checks; test_ai_brain.py 1368 cases match. Docs: ai.md 17.7,
  discoveries.md (two entries), playtest.md W34 row.

## W28, W29 (2026-10-30)

- W29 (mac4/w29-recentre, docs only): as the original; / is Center Legs,
  either centring cancelled by any twist or look input (test_parts.py).
- W28 (mac4/w28-lead): the HUD's mech_velocity hook took the raw +0x030;
  the exe's vt+0xc is the lead velocity (0x0041d480). Fixed through
  weapon_lock_velocity; test with a break. The part aim is the exe's.
## The setup API (2026-10-30)

- Branch mac4/setup-api (off lead/batch2 584d494). mech3/game_setup:
  install_from_disc, apply_patch, add_music, check_install, probe_source,
  find_discs (the last two asked for by dev 2). Progress by bytes,
  cancellation leaving nothing (music via music.new), plain-words
  results. disc_install gains failure kinds and a progress callback;
  patch_import list_installshield (the header only); cue_audio a
  progress/cancel callback. mw3 --install-from and --patch wrap them.
  Test setup_api_probes_installs_cancels_and_explains; the real ISO
  through mw3 (progress 0..100%, "MechWarrior 3 1.0 is installed ...").
- The installer: tools/windows/installer.iss (Inno Setup: Program Files\
  MechWarrior 3 Rebuild or per-user, Start-menu and desktop shortcuts,
  uninstaller keeping %APPDATA%), package.cmd's last step (iscc found on
  the PATH or its folders; skipped with a warning without it). Not run
  here (no Windows); the commands for the PC are in windows.md.
- The GUI build: MW3_PACKAGE on Windows links /SUBSYSTEM:WINDOWS
  /ENTRY:mainCRTStartup; with no console the output goes to mw3.log
  (sdl_child.cpp log_to_file) and the helpers start windowless
  (start_child, SDL's background process). mingw compile-checked only.

## The W31/W33/W34 stack on main (2026-10-31)

- main (ffef80f0) merged into mac4/w34-ally-missiles (49643747; playtest.md
  and dev.md rows from both sides). Two checks failed there, both from W34's
  real waits meeting main's newer runs, neither a game fault:
  - mission_c3m1_field_bases_repair_the_player_twice, bugs 1: the repair
    restores everything (1.00 / 1.00 the frame step 4 ends, both runs); the
    test read the wear at the rest, about 5 s later, and c3m1's Clanners
    (three, the "Clanner" pilots) fire on the player all through the dock.
    With the zero waits they hit him even more while docking (internals
    0.24) but no shot fell in that window; with the real ones one does (0.99
    / 0.87). The test now reads the wear when step 4 ends and prints the
    rest's beside it. Break: the repair skipping a torso section's internals
    is caught.
  - sdl_program_training_chain, t1m2 lesson 12 (target_object
    bushwacker_02): the driver faces the target level at its origin and
    presses Q; the reticle's centre line met its left calf with the zero
    waits (yaw_sin -0.840) and with the real ones, the Bushwacker standing
    about 30 degrees differently (-0.952), passed between its legs to the
    ground behind (g8767, 393 m). The driver now alternates Q with the
    next enemy (E), as a player would.

## W32 reopened, the radar's targeted buildings (2026-10-31)

- Branch mac4/w32-radar (on 60b438ba). The radar places a contact by its
  object's 0x005a85e0 (node +4, data +0x84; called straight at 0x43e503
  after 0x4e6fe0): a targets.zrd building's node is at the world origin
  (hqbuild: node 0, 0, 0, class 5, not stale; centroid -860, 24, 193), the
  exe's attach 0x4d3390 -> 0x4d8250 sets that node too, so the original
  points the triangle at the origin, at the contact's own range. FIX (bug)
  behind bugs: HudHooks::object_radar_place gives a targets.zrd object's
  (or trap's) centroid; radar and map both. HudRadar keeps the triangle's
  last place for the report (radar_target_x/y). Test
  mission_c3m1_hq_targeted_is_ahead_on_the_radar_headless: c3 (index 2) m1,
  the player 400 m south facing the HQ, Q until targeted; with bugs +0 -20
  (400/1000 of the 50 px radius), without +16 -12 (55 degrees right);
  break (the hook off) caught. The field-base centroid candidate went into
  fixes.md's candidates (the lead: not behind bugs).

## The Mac libraries and the universal app (2026-10-31)

- Branch mac4/deps-build (on mac4/w32-radar, with c1f34115 cherry-picked as
  5e539289). Owen allowed build_deps.sh in this session. JOBS env caps -j
  (the lead's batch shares the Mac). FreeType 2.13.2 needs
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 under CMake 4.2. 2.6 min at JOBS=4;
  all pins match; seven dylibs universal, min 12.0, @rpath ids.
- make_app.sh --deps: the prefix, arm64;x86_64, deployment 12.0,
  PKG_CONFIG_LIBDIR the prefix's; the bundler follows @rpath names into the
  prefix, takes licences from the source trees (FreeType's FTL.TXT too),
  deletes the build's absolute rpath (once: a universal binary lists it per
  arch) and parses otool -L without its per-arch headers (they had been read
  as libraries, mw3 itself copied into Frameworks). Without --deps it warns
  it is not the release form. THIRD-PARTY.txt now the --deps build's; the
  README says macOS 12, Apple silicon and Intel.
- Checked: three dylibs bundled, every binary x86_64 + arm64 min 12.0,
  LSMinimumSystemVersion 12.0, only @executable_path/../Frameworks, ad hoc,
  no team; DMG verifies (8.2 MB); with /opt/homebrew unreadable both slices
  run c1m1 60 frames (arch -arm64, arch -x86_64 under Rosetta), exit 0.
  Not run on a real macOS 12 or Intel Mac. Seen on the way: an explicit
  --write-dir that does not exist is not created (dev build alike; the
  default SDL_GetPrefPath one is), so settings.reg is not written there.

## mw3import in the Mac app (2026-10-31)

- Branch mac4/mac-import (on mac4/deps-build). make_app.sh --deps builds
  mw3import, puts it in Contents/MacOS, bundles both executables' libraries
  (seven dylibs), gives each only the bundle's rpath and signs it before the
  app; the launcher's art into Contents/Resources/assets/launcher;
  THIRD-PARTY.txt's FFmpeg section (LGPL), the README's first-start line.
- first_run_import: in a Mac app SDL's base is Contents/Resources, so the
  importer is looked for in ../MacOS too. mw3_main makes an explicit
  --write-dir that does not exist (the lead's small fix).
- The in-app import first failed "no MPEG-1 encoder": an FFmpeg 7 in
  /usr/local/include (root-owned, Dec 2024) won over the prefix's -isystem
  headers, so mw3import's AVCodecContext layout was 7's (the decoder's size
  read 480x640). NO_SYSTEM_FROM_IMPORTED on mw3import: -I.
- The package build had MW3_ASSETS_DIR (the source tree's path) compiled in:
  this Mac's path in the release binary, and the art found there. Now
  development builds only. Checked by a launcher shot from the app against a
  copy without its assets (324 vs 37 colours sampled); strings: no
  /Volumes or /Users path in either binary.
- Checked, /opt/homebrew unreadable, fresh write dir, --import: arm64 19
  movies in 75 s, x86_64 (Rosetta) in 104 s, then c1m1; c1m2 plays 475 of
  475 frames in both slices. arm64's files byte-identical to the dev
  importer's; x86_64's video 0.1% smaller (no asm in its FFmpeg), sound and
  index identical.
## W38, the intro's dropped frames (2026-10-31)

- Branch mac4/movie-start (off main 2b315d3a). MW3_MOVIE_TRACE: per new
  movie frame the clock frame, the frame shown, skips, gap, decode, flip;
  the Indeo helper's start-up and calls over 20 ms, per-op totals; the
  sound device's period and the first callbacks.
- Measured on Owen's PC (a worktree build-devtree in the PC repo, git-ignored,
  junctions to deps and game; bundles carried over in build-devxfer; runs as
  one-off /it scheduled tasks, deleted after). Windowed, not fullscreen:
  --frames keeps the window out of fullscreen (mw3_main), noticed after.
  Before: through mw3indeo one frame in three repeated and one skipped for
  the whole 180 s (2690 frames), the files path the same (131 of 452 in
  30 s); decode 5-6 ms, steps an even 67 ms, the helper up in 4 ms with the
  codec in 39 ms; the sound device 44100 Hz, 441 frames (10 ms) a period.
  Mac with CoreAudio (1024): 16 skips in 198.
- Cause: AviAction::step asks only when its own 15 fps clock changes frame;
  the stream answers the sound's frame; both clocks start on the same step,
  so each ask falls on the sound's frame change (a 10 ms cursor: just before
  or after, a regular beat). FIX (bug) behind bugs: with a streamed sound,
  ask each step, show once the sound's frame changes (Stream::sound_frame),
  from the clock's frame 1 (cursor 0 just after the start reads as the end).
- After, on the PC: helper 453 steps in 30 s, 0 repeats, 0 skipped; files
  453, 1 repeat (the start), 0 skipped; gaps 60-70 ms (the 10 ms periods).
  Test sdl_intro_follows_its_sound_without_a_beat (SDL dummy driver at 441
  frames: fixed 1/0, --original 19/2; break caught). Owen's "10 seconds" not
  seen: the beat is steady all through and nothing stalls at the start.
- --frames during the intro counts the movie's flips (15 a second), so
  3000 was 200 s: run 1 hit my 180 s watchdog. Not changed; its usage line
  says so now.
## Multiplayer, piece 1: zNetwork's session (2026-10-31)

- Plan accepted (mac4/mp-plan, 8d4e7936 with the NAT note); ENet approved.
  Branch mac4/mp on it. znet_session.{h,cpp}: the globals, active / local_id
  / is_host / send / shutdown, initialise / host_session / join_session, the
  in-process Hub. The engine's network_lobby_active, lobby_session and
  network_shutdown overrides; damage_calls().force_variant0 = znet::active
  (its SKIP gone from hook_census.inc); the census's deferred 0x00586ce0 and
  0x00586c20 lines gone. The shutdown's poll, handler clear and lists come
  with pieces 2 and 3.
- test_znet_session.py: the exe's three checks over 32 global states and
  0x5888a0 with a fake IDirectPlay4A (SendEx at +0xc4 keeping its ten
  arguments; 0x58a880 stubbed: 0 for an error), 56 identical; BREAK=1/2
  caught. mw3tests znet_session_* (3; the hub delivering to its sender
  caught).

## Multiplayer, piece 2: the handlers and the poll (2026-10-31)

- znet::on / off / clear_handlers / dispatch / poll; Session::handlers (a
  vector for the exe's list), dispatching depth, system_message (piece 3's).
  AppHooks::network_poll now znet::poll (ported, out of line like
  timer_update); the census's deferred 0x00588930 gone, known-keys test 49.
- test_znet_dispatch.py: the exe's list via 0x587aa0/0x587b20/0x587a00/
  0x587c40 with a made sentinel node ([0x730794]), new/delete stubbed
  (0x5abd26 / 0x5abd1a), handlers as logging code stubs (ECX sender, EDX the
  message); the poll with a fake Receive at [0x730724]+0x64 growing the
  buffer through DPERR_BUFFERTOOSMALL and the IAT realloc. Pitfalls met: the
  emulator's heap rewinds at begin(), so the fake object goes after it; and
  0x49bfd0 is Emulated's ZERR stub already (a second hook returns twice).

## Multiplayer, piece 3a: players, teams, the translation (2026-10-31)

- znet_dplay.{h,cpp}: PlayerRecord / TeamRecord / lists(), player,
  player_name, each_player / each_team, system_message (0x588a70, read from
  the disassembly: Ghidra folds the local message's words), network_error
  (0x58ae10), on_host (0x586d50). Session gains desc, Player is_host; the
  transport gains set_name and is_host; Packet a SystemEvent (the hub fills
  it; its old words kept). initialise wires 0xd and the translation; the
  shutdown empties the lists. The engine's lobby_player_name wired.
- 0x588a70's details: DPMSG_CREATEPLAYERORGROUP's DPNAME at +0x18 (short
  +0x20, long +0x24), add/delete group +4 / player +8; the host's 0xd on
  every arrival (even a known one); destroy dispatches before unlinking;
  SESSIONLOST is "MSG_DPERR_SESSIONLOST" by 0x575fb0 (zutl::localize).
- test_znet_players.py: DPMSG structs in the emulator, fake GetPlayerCaps
  and SendEx, strncpy stubbed at its IAT slot 0x5bb648, 0x575fb0 answering
  its name; 250 sequences identical. My probe's first name parse was wrong
  ("Player N" starts with P too); fixed in the probe.

## Multiplayer, piece 3b: teams, data, kicks (2026-10-31)

- znet_dplay: create/add/remove/destroy team, enum teams / team players /
  players, leave, kick, player and team data. Transport::Member, the group
  and data operations; the hub's. Read from the disassembly (Ghidra loses
  the register arguments): add (ECX team, EDX player), the data calls (ECX
  id, EDX data, the size or its pointer on the stack; 0x5897f0 passes its
  own argument slot's address, so its caller never sees the size).
- test_znet_teams.py: the enumerations' callbacks are labels, called
  directly after the fake Enum* (no re-entry into Unicorn); the team
  enumeration's answer read as [0x7307c0] after them. A prefix gives the
  teams members so the removal break shows. create_team on failure: no
  record (0x5883fb), fixed after the first version kept one.

## Multiplayer, piece 4: the ENet transport (2026-10-31)

- ENet 1.3.18 vendored (both downloads in the scratchpad's enet-dl; release
  tarball 2a8a0c53..., GitHub tag archive 28603c89..., every kept file the
  same). CMake: an `enet` library, unix.c's HAS_* macros, ws2_32 / winmm on
  Windows; mw3sdl links it.
- 0x5886c0 read: the record, the three enumerations, CreatePlayer(+0x18),
  the caps, a poll, the record appended, host -> 0xd. The transport's
  host/join now only open; create_player makes the player. The hub follows
  (no events of existing players to a joiner: it enumerates).
- net_enet: frames (kGame .. kRefused); the WELCOME roster's addresses as
  the host sees them; the joiner's links to the others, held when a MESH
  beats the host's PLAYER (a race the first version had). Blocking waits
  (join, create_player) run NetOptions::while_waiting each round: the
  in-process tests pump the host there.
- mw3 --host/--join; the network log lines; the two-process test reads the
  host's port from its log. Its ending is timing (who leaves first), so it
  checks either side's view; the joiner's "the host N" line can show 0
  (the 0xd can come after), so its "is the host" line is checked.

## Multiplayer, piece 5a: the mission's network start, end and router (2026-10-31)

- Piece 5 split: 5a here, 5b the network mech (0x46d450, 0x46d820,
  0x46e280). `mech3/mech_net`: NetMech (owner +0x6fc, heard +0x73c), NetGame
  (the flag, the remote mechs, configs, missing names, the reckoning gains),
  NetCalls (the handlers of pieces 5b-7 and what the router calls).
- Read from the disassembly: the router's jump table (0x15 dropped), which
  handlers get the mech in ECX, on the stack, or only the message; the first
  0xf's path (create, look up again, vt+0x18 with msg+4, timeGetTime at
  +0x73c, the router again, vt+0x158 with [0x72c52c]). The network mech's
  destructor 0x46e3c0 takes it off 0x605f84 and the dead list 0x605510.
- The app's files see Handle as uint32_t: mech_net.h stays out of them;
  net_start/net_end and net_set_game_clear are declared in app_mech.h, the
  wiring is app_mech.cpp's wire_net. ztimer::ticks_ms added for timeGetTime;
  zeff::log_set_net is 0x50d1d0.
- test_mech_net.py: the probe's game clear logs after the handlers (the
  exe's stub sees it then); the exe's slot byte is logged only when
  0x495b10 was asked.
- The hooks' lifetime (settled in 5b): wire_mission_hooks runs in the
  GameEngine's constructor and its null in the destructor, so wire_net's
  calls are live from start-up to exit, at the briefing and the quit box
  alike. The census stand-ins go in at a mission's start, for the fields
  still null.

## Multiplayer, piece 5b: the records (2026-10-31)

- 5b split again: 5b the records, 5c the network mech. 0x46da80 (the
  launch) is LaunchHooks::multiplayer; the app's side (app_mission.cpp,
  pilot.h) hands slot 0's loadout and the game's +0x8d/+0x8e through
  net_set_launch_game; net_launch_player takes the packed form (the probe
  hands any).
- 0x4259c0: Ghidra's float line is right, the exe doubles the half tons
  (fild, fadd st0); the armour item's add_equipment sets the tons again,
  so stock loadouts unpack as valid as they were (checked over all 62).
- Config vt+0x08 (ammo, 0x472210) and vt+0x10 (weapon, 0x472330) ported for it.

## Multiplayer, piece 5c: the network mech (2026-10-31)

- 0x46d450's missing list holds the player's id (0x45b800 copies *param_3,
  the [esp+0x18] slot that holds the sender): fixed from piece 5a's
  strings. Written, never read.
- NetCalls.create left out of the hook census: its null means the port's
  net_mech_create (a census stand-in there made nothing).
- Torso::set_mode now honours vt+0x5c (Mech::remote): 2 for network mechs.
- The app frees a network mech as mech_free does (registry, handle) through
  NetCalls.free; mission_controls_free deletes controls whose release()
  says so (the network's).

## Multiplayer, piece 6a: the sends (2026-10-31)

- 0x46f160's asm read for the 0xf layout (network-messages.md now has every
  field and flag bit). The controls' getters go through the IControls thunks
  (vtordisp 0): vt+0x2c the throttle target (+0x44), vt+0x24 the turn
  (+0x14), vt+0x38 the jump (+0x20).
- 0x46ef70 read from the asm (Ghidra had the resend counter off by one and
  lost the rate's ECX: the game's +0x50, the lobby's count).
- test_mech_net_send.py: the CRT asin and copysign stand-ins (test_matrix's)
  for 0x525f80; ceil (test_mechsave's); the kinds of pieces 7/8 are caught
  where their case starts and sent to the epilogue. The host's ping reads
  the game unchecked: only sent from the timer, which has one.

## Multiplayer, piece 6b: the receive side (2026-10-31)

- 0x46e4d0 and 0x46ed70 read from the asm: the respawn's call has the mech
  in ECX and the static 0x12 message on the stack; the aim extrapolation
  sets the quaternion's z (its 4th word) to 0; the reckoning's blend is
  0x51e380 (p = (1-k) recv + k p).
- The apply's mech calls, the weapons' aim and fire are test hooks
  (NetCalls::mech_call, aim, fire); the probe's parts log their setters.
- test_mech_net_apply.py: the aim points (the torso's place plus a rotated
  range, ~1000 m terms) are compared to those terms' rounding.

## Multiplayer, piece 7a: a mech's events (2026-10-31)

- The lag-correction fix (the lead's, from 6b) is in this commit: the
  latency in ms behind bugs; test_mech_net_apply.py checks the fixed path
  as the exe given the latency x 1000.
- The network mech's vtable read properly: vt+0x168 0x00410490 (0),
  vt+0x164 0x0046ef50 (0: no local damage), vt+0x160 0x0041ec50 (local id),
  vt+0xa4 0x004709c0 (the player's name), vt+0x58 itself.
- 0x0046e0f0 rebuilds in place: the port runs unlist, release, the
  destructor and placement new, then net_mech_construct (split out of
  net_mech_new).
- 0x004705b0: ECX and the stack argument are both the sender; the exe sets
  the sender's attacker. Kept, the fix behind bugs.

## Multiplayer, piece 7b: the world's events (2026-10-31)

- main merged in (0723eac1: the lead's NOMINMAX fix for the Windows build).
- 0x0046fde0's resend: ECX the message copied, bit 7 set, guaranteed to all.
  0x004700e0 is called with ECX the history entry's words; the port's
  history keeps the material's index, the exe's its pointer (the test hands
  each its own).
- The test hooks added for 7b: crater_make, take_material, material_index,
  log_has, log_replay, station.
- A crater in the headless c1m1 (the hub test) was not made at any place
  tried, so the crater path is checked by the emulator test only.

## Multiplayer, piece 8: the game's end (2026-10-31)

- The rules check left out with its folds: the folds only feed the next
  check (protection.md section 4). test_mech_net_send.py's CRC stand-in
  always passes, so the exe's path matches the port's.
- "Multi-Exit State" (0x00605fa8): its vtable's +0x0c is the enter
  (0x00470710): Missions cleanup(1), then Replace(state(2)) — the main menu.
- In the c1m1 test, Missions::loaded stays set after cleanup (as the exe's
  +0x5c); the test checks the load event (`done`) instead.

## Multiplayer, pieces 8b and 8c: the soak test, a peer's packets (2026-11-01)

- 8b: the debug start's network game (`--mission c1mp1 --host|--join`,
  `--net-wait`, `--net-minutes`, `--net-drive`, `--net-soak`; Launch::multi,
  the stand-in lobby, the autopilot, the staged kills, the lines) and
  `sdl_network_soak_four_players` behind MW3_SOAK=1 (multiplayer-port.md,
  piece 8b). Found: the join race in our ENet transport (fixed, tested);
  THROTTLE0 is a key callback (the autopilot holds the polled 0x18); a
  network game's end passes Multi-Exit and the main menu within a frame on
  to the Multiplayer State; landing hits do about 0.4% damage in two
  minutes of the autopilots' fight, so kills are staged.
- 8c: the lead's review items, FIX (A) for a peer's packets (fixes.md row,
  `net_hostile_peer_c1m1_headless`).
- 8d: the pilot's respawn 0x0046dde0 with the pilot's mech change 0x004783c0
  and the rebuild in place 0x004148c0, the objectives again 0x00491050; the
  hook census in a network game (multiplayer-port.md, piece 8d). Found
  while porting: the rebuilt mech's weapons asked the HUD of a freed
  manager (the exe's view is on the stealth object meanwhile).
- The 30-minute soak with the respawn: passes (tracking p95 3.8-4.1 m over
  ~9700 samples each, memory down, scores alike: 1:12/5 2:6/7 3:10/4 4:10/5).
- The lead's damage question: sdl_network_damage_point_blank (A held 40 m
  from a still B): the path works, the soak's low damage is the autopilot's
  aim and the ground (multiplayer-port.md, 8d).
- Piece 9's plan accepted (multiplayer-port.md); 9a done: zNetwork's
  connections, the search over the transport, its answers checked.
- 9b done: the connection screen (multiplayer-port.md, 9b). The search's
  amplification guards (the lead's review of 9a) went in before it.
- 9c done: miniupnpc vendored, the PortMapper, the address lines with
  copy; a double-router state of its own (a private "public" address),
  not "your provider blocks hosting".
- Piece 10 folded into 9b (the lead). 11 in three sittings: 11a the frame,
  the records, the lists, kicks; 11b the rules and LOCK; 11c START, READY,
  the mech lab, the teams' buttons, the scores' 0x14.
- 11a done (multiplayer-port.md, 11a): the lobby screen, the multiplayer
  game's lobby records and scores, zNetwork's walks with ids and team,
  `set_game_info` / `refresh_session` and ENet's kDesc. Found: the world
  list's hidden UNAVAILABLE, the add-on worlds by file name and the "**"
  mark, teams as colours in play, the Zone's score report (dropped, FIX
  (A)). The address lines sit under CANCEL and LOCK (a shot of the host's
  lobby).
- The lead's 9c item: the port mapper's quit bounded to 1 s (multiplayer-port.md).
- The lead's real-router check: `net_upnp_real_router` behind MW3_UPNP_REAL=1
  (the lead ran it on the Mac: passes, double router).
- 11b done (multiplayer-port.md, 11b): the rules out and in, LOCK_OPTIONS,
  the lock's mech; FIX (A) for the rules message, the locked door. Found:
  STOCK on resets every loadout to the first stock one on the lock (a
  candidate, fixes.md).
- The lead: the lock's stock loadout as FIX (bug) behind bugs (066dd199).
- 11c done (multiplayer-port.md, 11c): START, the places, READY, MECHLAB,
  the scores, the teams' calls, the Load state for a multiplayer game, the
  network game's lobby calls on the real game. Found: mech_net's one kills
  hook was two exe calls (0x00435a30 kills, 0x00435a50 score); the lobby
  needs the setter told of its own data (both transports now do). A host
  alone goes from the menus to Beachhead.
- The lead's order: the two-process lobby-to-game test first
  (`sdl_two_processes_from_the_lobby_into_a_game`, `--click-players`), then
  the mobile bases' stations (0x00490950), then 12.
- The mobile bases' stations (0x00490950): no null place in the exe (the
  emulator on c1's waynet_mp1 and mp2: all elements points), ported as is.
- 12 done (multiplayer-port.md, 12): the chat log, its scroll, the send
  and the 0x1f, FIX (A) for a short or open message.
- The lead's chat check: names and chat text never a format, control
  characters as '?' (names in zNetwork's records).
- 13 done (multiplayer-port.md, 13): the new team's dialog, NEW_TEAM and
  PICK_SKIN.
- 14a done (multiplayer-port.md, 14a): zTwoD's console, the chat panel,
  the chat keys, the HUD's chat line.
- 14b checked (no code), 14c done (the way back to the lobby, the wreck's
  fade), 16:9 for the multiplayer screens (multiplayer-port.md, 14b, 14c,
  16:9). The whole-flow test and the 30-minute soak over the menu path;
  the soak's leak (every mission's animations: a dword store over the def
  count in 0x0050b380) fixed; the 1.2 read-me's six checks (two fixes: the
  ping's latency, a Mac's keypad; the network loss ported).
- The census's three (the player's death, a destroyed weapon's voices, a
  network kill's box refill), the lobby's address lines at 16:9, the
  briefing's first frame after its widening.
- The mods check in the lobby (dev 2's M1 merged in; modding.md,
  multiplayer-port.md, the mods check), on mac4/mods-check; with M2
  merged, the lobby's add-on scan on to the mod worlds.
- M3's share (modding.md): the texture and image libraries, the sounds and
  interp.zbd's scripts bounded, with --check-mod looking inside them
  (mac4/bounded-tex).
- W40's probe (mac4/w40): --fire-probe in any Core game, the mouse's
  capture, focus and buttons; Owen's run pending.
- The overnight differential run's four (mac4/diff-fix): test_app_loop
  (the probe records the now-ported poll 0x588930 as a boundary),
  test_app_states (the quit box's move into the 2D named as the one
  departure; the Escape frame's box update back after the frame),
  test_veh_dock (the W8/W16 stuck check's start behind bugs), test_command
  (CHAT_TEAM / CHAT_ALL back through the command's other hook).
- Next: the lead's.

## W40: the Mac's mouse never fires (2026-11-02)

- Owen's probe log: every click focused, captured, relative, not muted,
  the buttons down for the hold, and the trigger never set; Space set it.
  Not SDL, focus or the take-back. Owen's saved DefaultCmdMap: TRIGGER1
  0x3d on Space and joystick 1, no mouse button, nothing on mouse 1.
- Cause: zInput's map is the settings' block (SdlZinHost::settings_block),
  saved at exit by the app's destructor; a network match's quit box
  (0x0048e2d0, W44) unbinds mouse 1 while up, so a program closed with it
  up saved the map without it, for every later game. Fix (bug, always):
  GameEngine::input_maps_free closes an open quit box first.
- Test: playtest_w40_quit_box_at_exit_keeps_the_mouse_fire_headless (bug
  fixes off; the box up at the exit, the next start reads mouse 1 on
  TRIGGER1). Break: without the close, the next start has it unbound.
- The test's zin host answers settings_block as the SDL host does; the
  usual test host keeps the map in memory, so no test saw it saved.
- Doubled down/up/down in one ms: SDL's Cocoa button reconcile on a
  trackpad tap, likely; it ends down for the hold, harmless.
- The lead's follow-up: a map an older build saved that way is mended at
  its load (controls_repair_mouse_fire, from the engine's controls_load
  hook so the probes do not see it): TRIGGER1 without a mouse button and
  button 1 on nothing. The registry's read leaves the reverse tables the
  defaults' (mouse_command(1) still 0x3d), so it sets the word and the
  table entry, not bind_mouse. Tests: the unit one in test_controls_cfg,
  and the playtest test's damaged and rebound saved maps; three breaks.
- The lead's sturdier version (Owen): the live map is never written. A
  kept copy (controls_keep_saved) is taken at the load (after the read,
  the file, the tables' rebuild and the repair) and at the controls
  screen's ACCEPT and its load/save box (load, save, both resets, CANCEL's
  re-read); both settings writes (0x40cb20, the options' ACCEPT) go
  through controls_write_settings. The exit close of the quit box is gone.
  The tables are rebuilt after the read (zin::map_refresh), so the box
  frees the button really bound and the repair uses bind_mouse.
- Tests: the playtest one (box at exit, a broken map, a rebound one) and
  the controls screen's ACCEPT saved and read back, CANCEL not saved; four
  breaks (the live map written, no keep at ACCEPT, no rebuild, no repair).
- The blink/clickthrough candidate is held on mac4/w40-candidate.

## W40's lag: Magnet (2026-11-02)

- External: Magnet's filtering event tap on left-mouse-down (CGGetEventTapList:
  998.9 ms measured). Probe runs: the down reached SDL after its up, macOS's
  pressedMouseButtons 0 through the press, with relative mode off (B) and
  with the run loop serviced each frame (C1/C2); on time with Magnet quit (C).
- README known issues, the release notes' line, W40 closed in playtest.md, a
  porting-lessons item (list the taps first). The probe branch
  (mac4/w40-probe2) stays off main.

## Launcher for v1.0 (2026-11-02)

- Hover: only the round badges ripple; square plates light their edge in
  amber (34e87eb4). Text: a space under a fifth of the font's size is
  doubled (DIN Condensed, "MechWarrior 3was"; b97b76e7).
- Rework (Owen and the lead): the games and QUIT only; a game without its
  files opens the find screen (the explanation, FIND MY GAME: the finder and
  the drives first, else A DISC IMAGE OR DOWNLOAD / A FOLDER; BACK). Found or
  installed: straight into the game, through the notes when the install
  lacks movies or music (their OK the start). The notes' ADD FROM MY DISC:
  setup::add_from_disc (add_movies: VIDEO\*.AVI from a drive, a folder, an
  .iso or a .cue into <write dir>/video; the music from a .cue). The game
  finds the write dir's movies first; first_run_import converts them too.
  Gone: Setup, Folder instead, Search again, the patch (kept unreachable).
  The installer's no-launcher shortcut dropped.
- Each screen logs its plates; MW3_LAUNCHER_PICK also presses the notes' OK.
- Open for the lead: after the notes' OK nothing offers ADD FROM MY DISC
  again (no Setup); the first-run import of the write dir's movies has no
  unit test (mw3-only code).

## W47: the menu music under the campaign's movie (2026-11-02)

- Headless: Campaign, a pilot, ACCEPT; the CD looped through fmv.zrd's
  "campaign" movie (played from the campaign GameMenuState, which has no
  screen for shell_resume to reactivate) and stopped at the Load state (the
  main menu's end, 0x485780). The original's code does the same: its CD
  stops are 0x47fa10, 0x481fc0, 0x485780, 0x4ac290, 0x590bd0 and
  play_sounds 0x59ef60 (stop_sounds is only called with 0); its movie code
  uses MCI only for MPEGVideo. In 1999 the drive reading the movie stopped
  the audio.
- FIX (A): GameEngine's movie hooks pause the CD music under a movie and
  resume it after (only the movie that paused it, only while still
  paused); the attract movie's own pause left alone. Test and two breaks.
- The lead: no blip. The resume waits a pass (timer_update) and happens
  only in the same state with the CD still paused; the test checks the
  menu music is never heard between the movie and the briefing (break:
  resume at once). Three breaks.

## The launcher's one picker (2026-11-02, before v1.0)

- Owen: one option. FIND THE GAME searches (an installed copy or a disc in
  a drive used with no picker); with nothing found it says so and opens one
  file picker. sdlfe::pick_source: an image or archive is the source, any
  other file its folder (install_of_pick: zbd\ and video\ up one), a .bin
  its .cue. ADD FROM MY DISC: a drive first, then the same one picker. The
  two-plate pick states are gone. SDL 3.4 has no file-or-folder dialog (the
  Mac's panel included), so a folder is picked by one of its files.
- launcher_one_picker_takes_a_file_for_its_folder (two breaks); the screen
  tests on the new plates; mingw syntax check of the changed files.

## The Mac's fresh-install findings (2026-11-02, before v1.0)

- Gatekeeper on macOS 15+: README and release notes give Privacy &
  Security > Open Anyway > Open; right-click Open for 12 to 14.
- The 1.0 data warnings (BindPrimitive LINES1/OPTIONTEXT, BindButton
  CALL_BUTTON/PLAYER_NAME, action_line6/7 not found): the same six lines
  with 1.2 data (mw3 run on the 1.2 copy to the menus); both versions'
  dialog.zrd name them, no data has the images, 1.2 changed only the MP
  chat arrows there. The original data's own; nothing for patch12.
- The movies converted in the launcher (movie_import.h shared with mw3's
  fallback): before the game and after ADD FROM MY DISC, on the work
  screen, cancelable; not on Windows; --no-import / MW3_NO_IMPORT /
  MW3_IMPORTER (a stand-in for the test).
- The picker: SDL's sheet NSOpenPanel answered through a main run loop the
  vsync frame loop served once a frame; while it is up the launcher waits in
  SDL_WaitEventTimeout and logs the call, the focus and the answer.
- Mouse sensitivity (Owen): a slider of ours, moved (the lead) from under
  INVERT MOUSE Y, where the 200-pixel image ran past the panel, to GRAPHICS &
  AUDIO among the volumes (MUSIC/SFX up 10, VIDEO_DEVICE down 12)
  (patch12::apply_ours: our own record edits on any data, the graphics
  screen's slider art, the label spaced like the original's), MouseAccelX and
  Y's magnitude with Y keeping the invert sign, 0.5..3.0 in tenths, 1.3 by
  default; bound only in the program (options_sensitivity_slider), so the
  probes run the original's screen. Menus keep 1.3 (CoreState::leave).
- The lead asked whether a mission's end loses the sensitivity: no.
  CoreState::leave's 1.3 goes through 0x0051b790 into zInput's working copy
  (0x005ece48/4c); the registry entries are the options' (0x005f4d2c/30).
  Proven by a round trip (2.5 inverted, c1m1, pause QUIT, the program's
  end, the next start and its mission). Then Owen's rule: MouseAccelX/Y
  kept like the map (settings_keep_mouse at the load and the options'
  apply), the writes saving the kept values; break: the live ones written.

## W48 and W35 again: c3m5 on Owen's 1.0 data (2026-11-02)

- The MFB parts vanishing at 16:9 were not the frustum (the planes are wide,
  and on 1.2's data every culled part really is off screen). Owen's PC plays
  1.0's data. patch12's c3 world rebuild wrote each model's payload offset at
  header +0x00 (the type) instead of +0x5c, so every c3 model had a junk
  type: model_box (0x53b4e0) gave no box for types other than 0 and 1, the
  parts culled as points at their pivots, and the facades drew unturned.
  Fixed in patch12_world.cpp; patch12_c3_world_reads_as_12s checks each
  header's type and offset (break: 3,359 types changed).
- W35 (bars after an MFB repair): Owen's log shows the store ran out after
  the first repair (the exe's rule: armour only from spare armour of the
  mech's kind and tech). Pinned by
  mission_c2m1_field_bases_repair_armour_only_from_the_store_headless (break:
  armour from any store). The old "c3m1" repair test launched index 1
  (c2m1); renamed to c2m1, since in the real c3m1 the Clanners kill the
  bases before a second repair. A discoveries.md line for players.
- The aim cone (Weapon::aim 0x4b36e0, cos 0.89) is next, as Owen decided.

## Pirate's Moon zSound, pm-map step 7a (2026-11-03, mac4/pm-sound)

- From mac5/pm (dfaf157e). PM's sounds.zrd reader is a rewrite (sets by
  name in std::lists, INCLUDE_SETS, COMPILE_FORMAT, QPRIORITY, a 3-item step
  over keywords), sets load by count and are found in any case, the queue
  plays oldest first, unpause resumes only what played (MW3's FIX), 4
  duplicates, 345 m/s, the range scale (0.5 at low object detail), CD mode
  3 loops. Forked through zgame::Table (its own block); the rest of zSound
  pairs with MW3's (sound.md, Pirate's Moon has the table and the 16 gone).
- Shared changes, MW3's behaviour unmoved: Set::sounds a deque, State's PM
  fields, the by-name wrappers skip a missing set, mw3_ names behind
  dispatchers, resume_voice / path_find / load_from_archive /
  clear_position out of their anonymous namespaces.
- test_pm_sound.py: test_sound's harness on PM's exe at its own addresses
  (a PmCpu with the map only for the import slots), sounds.zrd's tree built
  in the emulator, MW3's probe in pmprobe with PM's table and the fixes
  off; variants through a temporary v2 reader.zbd mounted before the
  install. run_exit.py prints nothing for a SystemExit message, so the
  break runs used a runner that prints it.
