# The HUD

The in-mission display: radar, compass, reticle and zoom window, gauges
(speed, jump fuel, heat, coolant), the two damage displays (likely the MFD
for your mech, TARGET for what you have selected), the weapons list, objectives and
scores, the mission timer and the equipment list, laid out per resolution by
hud.zrd. Original file unknown: the code between mech3\mech_data.cpp and
mech3\mech_map.cpp, from about 0x0043a7c0 to 0x00448880 (the options
screens come before it, the mech lab's Lab State after). Mapped here; the
porting order is section 7. Status: mapped, nothing ported yet.

## 1. Where it lives and when it runs

- **One HUD per mech type**, owned by the mech data (MechData +0x224), made on
  first use by 0x00430ad0 (`new 0x22e40`, constructor 0x0043b190) and
  returned by 0x00430aa0 (0 when the data is not loaded). The same call makes
  the **cockpit model** at MechData +0x228: an 8-byte object (vtable 0x005bd2f4)
  holding the mech's cockpit node from mechlib (0x004847e0), or a new
  object3d (0x004fb740) when it has none; with a `cockpit` reader node it is
  placed at the origin, made non-intersecting and flagged (0x004eb6d0,
  0x004eb620). The Core frame draws the cockpit part through the view (camera
  id 1; game-loop.md, the Core frame). Ported 2026-10-21 (playtest #4):
  `mech3::make_cockpit` (mech_data.cpp), run by `pilot_hud` with the HUD.
  MechData +0x228 is a `CockpitModel` (its node); 0x00430580 deletes it.
- **Set-up for a mech** (0x0043bb80): from the mech's view attach (0x0041ce30)
  and 0x0041cdc0; reads hud.zrd (section 3).
- **Each frame**, three entries:
  - 0x0043c390 from the world tick's camera update (0x0040f8d0): the game
    data into the pieces (radar contacts, compass heading, the gauges' speed,
    jump fuel and heat, the target's text, the timer, objectives, the
    equipment's damage).
  - The HUD's own +0x00 (0x0043c820) through the target mech's vt+0xe0
    (0x0041cd40, from the Core frame's HUD update 0x0040fac0, only for the
    view's target): the HUD's frame (section 11: the debug overlays, the
    pieces' updates and draws, the reticle's aim state 0x00443d20), then
    0x0041bd80 aims at the reticle's point +0x1d48 unless the mech's +0x638.
  - Drawing: the pieces are zTwoD objects in the HUD's Layer (0x0059fba0);
    zTwoD's per-frame update (0x0059fd20) draws them inside the Core frame's
    locked back buffer (render-frame.md). The 25 message lines at
    [0x005fc848] (0x4b0 each, y = 0x5f + 12 i) are drawn and hit-tested by
    0x0040e4b0.
- **Other entries**: the camera's look (0x00446240 on +0x6520, from
  0x00421c00), the ZAPS static and hold 0x0043be70 / 0x0043d07a (weapons.md
  ZAPS_HUD), the reset 0x0043d920 (from the mission's 0x00491696), the weapon
  HUD update 0x00446520 (from the weapons: 0x00415a48, 0x00460e83.., and the
  list itself), the ammo gauge 0x00443d20.
- **The free** (0x0043b860, mission-plan item 9): the mech type's free
  (0x00430580, when its last mech goes or the types are cleared) deletes
  the HUD; its destructor runs each piece's, the members' reverse order.
  Those with more than their zTwoD objects: the reticle (0x00444280) lets
  its on, off, over and can't images go (not +0x920, ret_on again); the
  zoom window (0x00442d00) and the radar (0x0043d810) let their lights'
  images go, the last first, then the tables; TARGET (0x00440190) lets its
  twelve edge arrows go, empty ones too; each MFD (0x004422b0) deletes its
  nav list (vt+0, 1) and its view. The port had no free: each mission's
  HUD stayed with its images. `test_hud.py` frees half its HUDs after the
  dump, the traces compared; `mission_end_frees_what_the_next_does_not_need`
  checks the player's HUD goes with the mission (`Hud::live`, ours).

## 2. The HUD object (0x22e40 bytes; vtable 0x005c01d8)

A zTwoD Layer (0x0059fba0) with two virtuals of its own: +0x00 the frame
update (0x0043c820) and +0x04 shown or not (0x0043baf0; the set-up passes
whether the mech is alive, vt+0xa0). The pieces live inside it:

| Offset | Piece | Constructor (args) | Final vtable | Update (+0x24) | Added to the Layer |
|---|---|---|---|---|---|
| +0x00010 | the mech it shows | | | | |
| +0x00014 | radar | 0x0043d260 (60.0) | 0x005c01e0 (FilledBox) | 0x0043dd00 | no (drawn by the Core frame's radar call) |
| +0x00dd4 | compass | 0x0043f3e0 | 0x005c02e0 | 0x0043f620 | yes |
| +0x0144c | reticle, with the zoom window (0x00442ac0 at 10, 370, 90 x 90) | 0x00443b60 ("ret_on", "ret_off") | 0x005c0688 (Image) | 0x004441a0 | no |
| +0x01da8 | gauges | 0x00444760 | 0x005c0708 | 0x004452c0 | yes |
| +0x03434 | MFD (your mech's damage) | 0x00442060 (530, 370, 75, 100, 1) | 0x005c0470 | 0x00442540 | yes |
| +0x0410c | TARGET (the target's damage and text) | 0x0043fcb0 (10, 370, 75, 100) | 0x005c03e8 | 0x00441490 | yes |
| +0x06520 | weapons list | 0x00445f80 | 0x005c0898 | 0x00446680 | yes |
| +0x17964 | objectives and multiplayer scores (a TextBox) | 0x00446700 | 0x005c0918 | 0x00447410 | no |
| +0x18410 | mission timer (a FontText, "00:00:00.00", font 0x005e14e4) | 0x005a00c0 | 0x005bc598 | | no (shown for good) |
| +0x188c0 | equipment list | 0x00448070 | 0x005c0b90 | 0x00448450 | yes |
| +0x22e08..+0x22e34 | cleared by the constructor | | | | |
| +0x22e38 | hud.zrd's root while the set-up runs | | | | |
| +0x22e3c | the resolution's block while the set-up runs | | | | |

The damage silhouettes are loaded once for every HUD (0x0043ae50 per part,
flag [0x00605990]): `dmg_<part>_blue/grn/yel/red/blk` for head, ltorso,
rtorso, ctorso, larm, rarm, lleg, rleg with each part's offset in the
silhouette (head 30, 2; ltorso 46, 6; rtorso 19, 6; ctorso 31, 15; larm 58,
8; rarm 2, 8; lleg 39, 42; rleg 7, 42), plus three background images whose
size goes to [0x00605998] / [0x0060599c].

### What each piece draws

- **Radar** (0xdc0 bytes): a FilledBox (green) with the view cone's two
  edges and a red line (HUD lines 0x005a6ae0), two texts (mech_string; the
  range `%.1fkm`), a light (`hud_light_green/yellow/red`, at 11, 11) and two
  polylines. Two sizes, moved to in steps of 10 pixels (0x0043da50): small,
  radius 50 at (55, 55); zoomed, radius 180 at (320, 240). The cone is the
  view's 60 degrees (0x0043f0c0), turned round on a flipped map (0x0043f130).
- **Compass** (0x678): the `hud_compass` tape (245 x 19) scrolled under
  `hud_compass_frame` (245 x 11) by the heading (0x0043f590).
- **Reticle** (0x95c): `ret_on`, `ret_off`, `ret_over`, `ret_cant` (aimed,
  idle, over a target, out of range; `ret_on` is fetched twice, +0x914 and
  +0x920); the zoom window (0x00442ac0): the `zoomcam` camera (5 degrees,
  zoom 10, the player camera's clip), its light `hud_zoomoff/on/over/cant`,
  a text, and a box (or a circle when +0x16c), sized by its scale +0x170 and
  kept 2 pixels inside the display (0x004438f0, 0x0043fa40).
- **Gauges** (0x168c): `hud_speed_frame` (575, 256), `hud_coolant_frame` (8,
  290), `hud_fuel_frame` (527, 296), `hud_heat_bar` (18, 163),
  `hud_heat_meter` (16, 163), the heat arrow (`lab_toggler_green`, 10, 150),
  the coolant and heat texts (Arial 8, messages 0x1d5 / 0x1d6, green with a
  dark green shadow), the speed text, and three bars (four-point polygons,
  0x005a6bd0; coolant blue, fuel green) inside the frames.
- **MFD and TARGET** (0xcd8, 0x2414; one base, 0x00442060 and 0x00441da0):
  the damage silhouette in five colours per part (the player's own, or a
  target's mirrored as seen from the front, 0x00447680 / 0x00447820), a 3D
  view of the mech (0x00442620: a zoom view on a see-through panel, boxed),
  the nav list (0x0047e840) and a title (message 0xdbe, 0xfbb in a
  multiplayer game). TARGET adds two Shapes, three lines, a Frame, twelve
  arrows (`hud_enarrow`, `hud_allyarrow`, `hud_othererrow` left, right, up,
  down; "othererrow" as spelled), a marker and three texts above it
  (message 0x22a; `%s: %s`, `%.2fkm`, ` (%dm)`, 0x00441850). Their modes
  (+0x38) survive in [0x00605988] / [0x0060598c]; at first 1 in Direct3D, 3
  in software (0x00441d90).
- **Weapons list** (0x11444): sixteen lines (0x00445760: a Slider, the
  recycle bar, with the name and two texts, `wepstring_red/green/yellow/grey`,
  a light `hud_light_*` and a mark), `hud_weapon_select`, `hud_weapon_chain`,
  the ammo (`%3d`, 0x00445dc0), over a see-through panel.
- **Objectives** (0xaac): a TextBox of 25 lines over a panel and the scores
  box 260 pixels to its right (0x00446870), fonts
  `rtobjective_title/incomplete/complete`, `rt_mp_scores`; hidden at first.
- **Timer**: the mission clock (`%02d:%02d:%02d`; the multiplayer clock
  [0x00609920] in a network game, else the game time [0x0060991c]).
- **Equipment** (0xa548): sixteen lines (0x00447c40: the name, a value
  right-aligned at column 110, a light `hud_light_red/yellow/green`; fonts
  `wepstring_*`), 10 pixels apart under (530, 30), over a panel.

The see-through panels (0x0043a7e0, 0x108 bytes) are an image of one colour,
green (0, 64, 0), at 60% opacity in Direct3D and opaque in software
(0x0043a7c0), with the four corners Direct3D draws it by (depth 0.9).

## 3. hud.zrd and the set-up (0x0043bb80)

hud.zrd holds one block per resolution, `V640x480`, `V800x600`,
`V1024x768`, `V1152x864`, `V1280x1024`, `V1600x1200`, each with:

| Key | Fields | Read by |
|---|---|---|
| MFD | position (x y), size (w h) | 0x00442230 on +0x3434 |
| TARGET | position, size | 0x00440110 on +0x410c |
| WEAPONS | position, width | 0x00446120 |
| EQUIPMENT | position, width | 0x00448200 |
| COMPASS | tape (x y), frame (x y) | 0x0043f500 |
| TIMER | position | the timer's set_pos (vt+0x0c) |
| GAUGES | speed_frame, fuel_frame, heat_bar, heat_meter, heat_text, heat_arrow, coolant_frame (x y each) | 0x00444dc0 / 0x00444e55 |

The set-up keeps the mech (+0x10), loads hud.zrd (0x00556740) and picks the
block by the video mode (0x0040d9d0: 3 and 5 V640x480, 6 V800x600, 7
V1024x768, 8 V1152x864, 9 V1280x1024, 10 V1600x1200; any other mode keeps
+0x22e3c as it is: 0 after an earlier set-up, which clears it at its end, but
on the first set-up whatever `new` left there, as the constructor does not
clear it; FIX (bug), section 9). With a block: MFD and TARGET (both `size`
and `position` needed; 0x00442230 moves the silhouette, the 3D view, its
panel, box and nav list, and the title; 0x00440110 also TARGET's three
texts), TIMER's `position`. Then, block or not:

- 0x0043be10: +0x22e08..+0x22e34 cleared, the radar's fade 0 (0x0043dc70:
  the box's and lines' +0x54 / +0x4c).
- The reticle, MFD and TARGET get the HUD (0x00444250 with the lock tone's
  channels 0 and 12 stopped, 0x0041e7a0; 0x004423c0 gives the silhouette the
  mech and the nav list the mech, 0x0047eba0; 0x00441760 also clears
  TARGET's texts), TARGET the mech's target slot (+0x6c, 0x00441800).
- The radar (0x0043d6d0): amber (255, 226, 4) instead of green when the
  mech's +0xbc bit 2 is set (0x0043d630); the zoomed size from the display:
  centred, radius 180 at 480 lines (h x 180 / 480); the view cone from the
  player camera's field of view.
- EQUIPMENT (0x00448200, with a mech): `width`, then `position`; the lines
  hang below the point unless the weapons (manager +0x24) and the listed
  parts' equipment (mech +0x3ac by index 0, 12, 3, 8, 14, 6, 7, 4; only the
  positive counts) come to 17 or more.
- WEAPONS (0x00446120, with a mech): `width`, `position`, the weapon manager
  (+0xd4), every line emptied (0x00445d10), the lines filled from the mech
  (0x00446520).
- COMPASS (0x0043f500): `tape` and `frame`.
- GAUGES (0x00444dc0): `speed_frame`, `fuel_frame`, `heat_bar`,
  `heat_meter`, `heat_text` (the text of message 0x1d6), `heat_arrow` (the
  toggler), `coolant_frame`; then the texts, bars and heat marks laid out
  again from the frames, as the constructor did.

Last: the HUD shown when the mech lives (mech vt+0xa0; 0x0043baf0 shows or
hides the radar, 0x0043db70 / 0x0043dc00), the zoom window round when the
mech's 0x0041df10 is 2 (0x00442df0), hud.zrd released (+0x22e38 gets
0x00556820's answer, +0x22e3c 0).

## 4. The primitives it uses

- zTwoD (twod.md, ported): Object2D groups (0x0059fda0), Image (0x0059b970,
  named 0x0059b830), FontText and TextBase (0x005a00c0 with a fonts.zrd font
  0x00517680, 0x005a0660; text by message id 0x00409bd0), Text (0x005a15a0),
  TextBox (0x005a18f0), Slider (0x0059e0f0), PolyLine (0x005a3830), the
  Layer's add 0x0059fbe0.
- zTwoD's HUD side, built and set up but not drawn yet (twod.md): Frame and
  FilledBox (0x005a39f0, 0x005a3a60, 0x005a3b60), Shape (0x005a3170,
  0x005a34c0..0x005a3530), the HUD lines (0x005a67e0, 0x005a6880, 0x005a6ae0),
  the polygon bars (0x005a6bd0, 0x005a6c30). Not ported: their draws,
  Shape's transformed points 0x005a3580, zRender's 2D line list 0x00562cb0
  and boxes 0x005630c0 / 0x00563440, the line clip helpers
  0x0059b2f0..0x0059b7e0.
- 3D: the zoom window's camera (`zoomcam`: 0x004f3a70, 0x004f3d90,
  0x004f3ff0, 0x004b5150, 0x004b5440, a second view drawn through
  0x004f9fa0 / 0x004fa0b0); the cockpit model's node.

## 5. 640x480 assumptions (for the any-resolution work)

- Every piece's default position in the constructors is a 640x480 one (the
  MFD at 530, 370; TARGET at 10, 370; the gauges' images; the zoom window at
  10, 370; the compass); hud.zrd then moves the pieces for the six listed
  modes. A mode not in that list keeps the 640x480 layout.
- The radar's area is 0x140 x 0xf0 (half of 640x480) with its centre at
  (0x37, 0x37); the message lines step 12 pixels from y 0x5f.
- hud.zrd's larger layouts keep some 640x480 values: the GAUGES heat bar,
  meter, text and arrow and the coolant frame are the same at every size (on
  the left edge, as at 640x480).
- Fonts are fixed sizes (Arial 8 for the heat text): text does not scale.

For any resolution (done 2026-10-28, resolution.md step 4): the pieces laid
out by anchor from the 640x480 block. Each anchor is read from how the
original's larger blocks move the piece. Every position of V800x600 ..
V1600x1200 is V640x480's, moved by w - 640 (the right edge), (w - 640) / 2
(the centre) or h - 480 (the bottom), and nothing else:

| Piece | Key | x | y | 640x480 | 1600x1200 |
|---|---|---|---|---|---|
| MFD | position | right | bottom | 530, 370 | 1490, 1090 |
| TARGET | position | left | bottom | 10, 370 | 10, 1090 |
| WEAPONS | position | right | top | 530, 30 | 1490, 30 |
| EQUIPMENT | position | right | top | 530, 30 | 1490, 30 |
| COMPASS | tape, frame | centre | top | 245, 19 / 11 | 725, 19 / 11 |
| TIMER | position | right | top | 580, 15 | 1540, 15 |
| GAUGES | speed_frame | right | top | 575, 256 | 1535, 256 |
| GAUGES | fuel_frame | right | top | 527, 296 | 1487, 296 |
| GAUGES | heat_bar, heat_meter, heat_text, heat_arrow, coolant_frame | left | top | 18, 163 .. 8, 290 | the same |

Sizes and widths are the same in every block, so they have no anchor. The
speed and fuel frames stay at their y at every size: they hang from the
top. Pieces hud.zrd does not place keep the constructor's place, as at the
original's larger modes: the radar (0x37, 0x37), the message lines from y
0x5f, and the zoom window at (10, 370). The message panel is centred by
0x0040f330 across the display. `mech3::hud_anchor` holds the table.
`Hud::pos` applies it to every position the set-up and the pieces' layout
readers take, past mode 10 with any_resolution, where hud.zrd has no
block and V640x480 is read. `resolution_hud_anchors_make_the_original_blocks`
remakes all 70 positions of the five larger blocks.

## 6. Per-frame data flow (0x0043c390)

With a mech: its controls' update (vt+0x1c on the controls' sub-object),
0x004453a0; the radar's positions (0x0041cdf0 or, with +0x70, 0x00522170 and
the normalise), 0x0043f130 / 0x0043f2d0; the compass heading from the
mech's forward (atan2 of +0x24, -+0x2c) into 0x0043f590; the controls'
vt+0x2c; the gauges: 0x00445450, 0x004455e0 (jump fuel: +0x554 over 1.5, and
the ratio +0x558 / dt - +0x55c), 0x004456f0, 0x00445520 (+0x3d8 > 0, the
mech's vt+0x148: heat); the target's text 0x00441850; the equipment
0x00448320 when shown; the MFD 0x004424b0. Always: the timer's text,
0x00446900 (objectives), 0x00440b00 (TARGET), and with a mission waynet
([0x00609980]: the nav waynet, or the mcbwaynet while the MFB command
picks) the edge arrow toward its selected point, 0x00479ef0
(vehicles.md, The nav waynet).

## 7. Porting order

Small items, each with a differential test:

1. **Construction and data**:
   - 1b, done: the HUD object (0x0043b190) and every piece's constructor
     with their defaults and helpers, on the ported zTwoD (`mech3/hud`,
     section 9).
   - 1a, done: the set-up 0x0043bb80 with every hud.zrd reader and the
     pieces' attach calls (section 3), on the shipped hud.zrd at every video
     mode and on crafted variants.
2. **The HUD primitives** zTwoD has left: FilledBox / Frame, PolyLine and
   Shape with the transformed points and zRender's line list, the line clip
   helpers, the key-list bars (twod.md, Left). Done (checked 2026-10-28):
   0x005a3580, zRender's 0x00562cb0 / 0x005630c0 / 0x00563440
   (zrndr_prim2d.cpp), the clips 0x0059b2f0.. (twod_shapes.cpp);
   test_twod_prims.py.
3. **Compass** (0x0043f590, 0x0043f620) and the **timer**. Done.
4. **Gauges** (0x00444250, 0x00445450..0x004456f0, 0x004452c0): speed, jump
   fuel, heat, coolant.
5. **Weapons list** (0x00445f80.., 0x00446520, 0x00446680, 0x00447c40) and
   the ammo gauge 0x00443d20; the ZAPS static and hold 0x0043be70, 0x0043d07a.
6. **MFD and TARGET** damage displays (0x004423c0, 0x00442540, 0x00441760,
   0x00441490, 0x00441850, 0x00440b00).
7. **Radar** (0x0043d6d0, 0x0043dd00, 0x0043f130, 0x0043f2d0, 0x0043da00).
8. **Reticle and the zoom window** (0x004441a0, 0x00444230, 0x00442ac0..,
   0x004436c0, 0x00442df0).
9. **Objectives, scores, equipment** (0x00446900, 0x00447410, 0x00448320,
   0x00448450).
10. **The frame entries** (0x0043c390, 0x0043c820, 0x0040fac0, 0x0040e4b0,
    the message lines), the reset 0x0043d920, the look 0x00446240, the
    cockpit model (0x00430ad0's second half), and the program's wiring.

## 8. Open

- What the mech's +0xbc bit 2 is (the radar turns amber with it,
  0x0043d630) and what 0x0041df10 answers (2 makes the zoom window round).

- Which buttons and options show the large radar, the zoom window and the
  equipment list; what +0x70 of the HUD selects for the radar.
- 0x0043be10, 0x00441800 (with the mech's +0x6c) and 0x0041bd80 are not
  read yet. (Done since: 0x0043be10 is Hud::reset, 0x00441800 TARGET's
  set_target, 0x0041bd80 mech_hud's aim_at_point. What 0x0041df10 answers
  is known too: the definition's kind, 2 rounding the zoom window. Still
  open: the mech's +0xbc bit 2, and the buttons for the large radar.)

## 9. As ported (`mech3/hud`)

`mech3::Hud` is the 0x22e40-byte object, its pieces members in the
original's order (constructed in it, so the recorded calls come in the same
order): `HudRadar`, `HudCompass`, `HudReticle` (with `HudZoomWindow`),
`HudGauges` (`HudBar`), `HudMfd` (`HudDamageView`, `HudMfdView`),
`HudTarget`, `HudWeapons` (`HudWeaponLine`), `HudObjectives`, the timer and
`HudEquipment` (`HudEquipLine`), with the shared `HudPanel`, `HudText`
(a FontText in mech_string, vtable 0x005bc598) and `HudLight`. The lights,
fonts and damage silhouettes every HUD shares are `weapon_set()`,
`equipment_set()` and `damage_set()`. The constructors and the set-up
(`Hud::setup`, the pieces' `read_layout`, `place` and `attach`) are ported;
the frame and the pieces' updates too (section 11).

The engine sits behind `HudHooks`: hud.zrd's load, the video mode, the
display's size, the mech's fields the set-up reads (vt+0xa0, 0x0041df10,
+0xbc, +0xd4 and its +0x24, +0x3ac, the target slot +0x6c), the HUD's
sound channels, the nav list's attach and place, the weapon list's refresh
0x00446520, the mission entry's flip, zRender's nodes for the zoom views (window and camera nodes,
names, size, place, zoom, field of view, clip, active), the player camera's
clip, the options' display and window rects, the nav list 0x0047e840, the
pilot's game, Mech3Msg.dll's messages and the saved MFD modes.

The zoom views (0x0043f660: the MFD's and TARGET's mech, the reticle's zoom)
are built as structs with the original's fields (`HudZView`: the camera node,
its window, the world, the render path; `HudViewWindow`: the window node,
its area and target and their ratio) and their engine calls recorded.

FIX (bug), behind `zfix::options().bugs`: the constructor clears +0x22e3c,
which the original leaves as `new` left it and the set-up reads for a video
mode without a hud.zrd block (section 3; `fixes.md`).

Test: `test_hud.py` (`tests/probe_hud.cpp`) builds the HUD in the emulator
and in the port (the constructor runs the set-up), with random render paths,
display and window rects, saved modes, message tables, mission flips, video
modes (listed and not), hud.zrd shipped, crafted (blocks, pieces and keys
dropped or renamed, odd values, MFD and TARGET resized) or missing, and a
fake mech or none, and compares the call traces and every field the
constructor and set-up set, on 100 runs. `test_fixes.cpp`
fix_hud_block_cleared checks the fix. Breaking the set-up: 22 of 25
caught (the misses: the MFD's two keys read in the other order, an amber that
packs to the same 565 pixel, steps computed after the move that makes them
0, and TARGET's clear that set_target repeats). Breaking the constructors: 33
of 41 mutations caught; the eight misses change nothing a constructor does (a
wrap constant that rounds to the same float, flags already clear, the red
look never taken, red 1 and 0 packing to the same 565 pixel, steps already
positive, a text already empty, points set in order, and +0x22e3c, which only
the set-up reads: item 1a's test).

### Left

Refreshed 2026-10-28 against the code: each item marked.

- (Done) The set-up with a target in the mech's slot (TARGET's 0x00441800 path).
- (Done, sections 22 and 23) The zoom views' drawing and camera work, with the render work: the zView
  and window creation (0x004b5150, 0x004b5440) and the camera setters stay
  recorded hooks until then (lead, 2026-10-20).
- (Done) What this doc calls "the nav list" (0x0047e840, 0x2fb8 bytes) is
  the armor display the debriefing's lance cards use: eleven armor bars with
  H/T/A/L labels (campaign.md section 3, `LanceDisplay` in
  armor_display.h / debriefing.cpp). The HUD keeps its hooks (test_hud.py
  records them); the program's (app_hud.cpp) build, place (0x0047eb60),
  lay out (0x0047eba0, from the player's mech), fill (0x0047ef10, the
  target's) and update the ported display; mw3tests'
  `mission_c1m1_hud_weapon_lines_and_lock_headless` checks it is laid out.
- (Done) The paths with a target or a mech: TARGET's 0x00441800 / 0x00441780 /
  0x00441660 with a target, the weapon line's refresh 0x00445dc0 with a weapon,
  the equipment's and weapons' readers 0x00448200 / 0x00446120 with a HUD.
- (Done) The zoom window's show_for 0x004438d0: the zoom view's (0x0043fc10),
  then its text hidden; test_hud.py op 33 shows it.
- (Done) The show_for overrides 0x0043db70 (radar),
  0x00441560 (TARGET), and the objectives' and weapons' parts of theirs
  (0x00446ad0, 0x00446520).
- (Open) TextBox's own message-id setters 0x005a1cd0 / 0x005a1cf0 (FontText's run).
- (Done) The weapon group calls of the lock (section 12): 0x004b1bd0, 0x004b1c70,
  0x004b1cd0 stay recorded hooks: mac 4, the planner (they sit in the AI
  weapon planner's range 0x004b1170..0x004b1d7f). Ported since in
  weapon_planner.cpp; the HUD's tests keep the hooks.
- (Done, section 25) The mission maps' list (0x00458590 on [0x00605acc]) for Shift+R's map
  mode (section 19).
- The radar in the program (section 15): the mission map (its map mode),
  the nav route's polylines (+0xbd4.. lists), the network marker
  (0x0058b060) and the landmines (0x00413560's list) are not ported and
  answer none. (2026-10-28: the mission map is done, section 25; the
  +0xbd4 polylines are the mission boundary, done with playtest #30; the
  landmines are done (landmines.cpp, the radar's mine_count). Open: the
  network marker, multiplayer.) (Done, 2026-10-25) The contacts' objects are of every class
  (ai.md section 15, Radar contacts): their place, team and blip (vt+0x3c)
  by class, the basic vehicles', turrets' and targets.zrd's with their own
  markers, the field base's and command marker's none (never drawn); TARGET's
  object likewise (its name, hidden test, mech, centre, size, the lead
  point's place and velocity), so a turret or a target can be TARGET's with
  its own name and colours. A mech's name (vt+0xa4) is now TARGET's first
  text too, as in the exe.
- (Done) TARGET's third text in the program: a mech's own name (vt+0xa4,
  0x0041df30): the player's pilot's, else its brain's (0x004aee70: the
  brain's vehicle record's pilot, AiParams +0xd4), "" without one.

## 10. The aim (the reticle's world point)

Every weapon aims at one world point the HUD works out each frame.

1. **Placing** (the mech's 0x0041ce60, from Mech::update for the view's
   target): the controls' vtable +0x14 gives where the player looks, (x, y)
   in -1..1 of the display's half size (PlayerControls 0x00428800: its
   +0x140; every other controls class answers none, and in view 0 the
   controls keep it at 0, so the reticle stays centred). 0x004445f0 puts the
   reticle there (display centre + offset x half size), then:
2. **The ray** (0x00444350): the reticle's point, through 0x0040d9e0 (halved
   into the render target when pixels are doubled, VMode 3), unprojected
   into the world at the player camera's near and far planes (0x004b51f0 to
   set the camera's matrices, 0x00523930 with 1/z = 1/near and 1/far). The
   world's segment query (0x004eed90) runs between them with the mech's own
   node left out (0x004eb6d0 off, then on). One hit counts unless its
   material's game soil is 1 (water); with several, the nearest by squared
   distance from the near point among the non-water ones. A failed query or
   no hit: the far point.
   The far clip is 800 m, short of the ER large laser's 1000 m (W33): with
   the bug fixes on the query reaches the selected group's longest range
   instead, the miss's point unchanged (fixes.md).
3. **Kept** at reticle +0x8fc (the aim point, = HUD +0x1d48), +0x924 its
   distance from the near point (the far plane's when nothing was hit), and
   +0x928 the hit's record (0x00444710; 0x004446a0 builds one facing up at
   the far point). The zoom window follows the reticle when shown or scaled
   (0x00443660).
4. **Handed over** (the mech's 0x0041cd40, its vtable +0xe0, from the Core
   frame's 0x0040fac0): the HUD's frame (+0x00, 0x0043c820, not ported), then
   0x0041bd80 aims the mech at the point (0x0041bc20) unless +0x638.

In the program (`mech3/app_hud`, `mech_hud`): the player's HUD is made on
its mech data when first asked (0x00430aa0) and set up at the view attach
(0x0041ce30); `update_calls().player_frame` runs 0x0041ce60; the Core frame's
HUD update runs 0x0041cd40 before the rest of 0x0040fac0 (still unported);
the hooks use zClass's segment query, the camera's clip and field of view,
zModel's camera set-up and zMath's unproject (0x00523930, ported). Tests:
`test_hud.py` places the reticle on precomputed rays and hit lists
(missing, failing, water and several hits) and compares every field;
mw3tests `mission_c1m1_player_aims_with_the_reticle_headless` pitches the
torso until the reticle's ray ends on the Owens 80 m away and checks the
weapons, with no other aim, take its armor down. Breaking the aim: 10 of 10
caught.


## 11. The frame (0x0043c820) and the pieces' updates

The HUD's vtable +0x00, run by 0x0041cd40 each frame for the view's target,
only with a mech:

1. The **debug overlays**, while the debug text 0x005fc378 shows (toggled by
   0x0040dc34 and [0x005f4d6c]; the test reads its flags [0x005fc384]): the
   mech's gait, speeds and the like into five global texts
   (0x005fe918..0x005ffbd8, 0x4b0 apart). Hooked (`hud_debug`).
2. zTwoD's **Layer update** (0x0059fd20, only while the layer is active):
   each piece the constructor added (compass, gauges, TARGET, MFD, weapons,
   equipment) runs its +0x24.
3. The **timer**, the **objectives** and the **radar** (0x0043dd00, hooked
   until item 7), which are not in the layer's list.
4. While the reticle's state is still 5 (nothing set it this frame):
   whether its point lies over TARGET's shape or circle with a live target
   (0x00440a30) into the **aim state** (0x00443d20: the weapons' range and
   lock, their sounds, the reticle's image by state 0 on, 1 off, 2 over,
   3 can't). Both hooked; item 5's ammo gauge in the porting order.
5. The **reticle** (0x004441a0), then its state back to 5; the network's
   edge arrow drawn (0x00479ed0) with a mission waynet ([0x00609980]).

Each piece's update draws only while shown (kHidden clear), except the MFD
(0x00442540) and the damage silhouette (0x00447b50), which leave it to their
children:

| Piece | Update | Draws |
|---|---|---|
| Compass | 0x0043f620 | the frame, then the tape |
| Gauges | 0x004452c0 | the speed, coolant and heat texts; the speed bar and frame, the fuel bar and frame, the coolant bar and frame; the heat bar, meter and toggler |
| MFD | 0x00442540 | by its mode (vt+0x80): 1 its 3D view's box (0x00442880), 2 the damage silhouette, 3 the nav list; the title only with [0x00609994], or in a multiplayer game while [0x00605f88] is clear |
| TARGET | 0x00441490 | the three lines, the mode's piece as the MFD, three of the four texts, the marker, the circle; no title. Its mode (0x004413a0) is none without a live target and only the 3D view for a target that is not a mech |
| Damage silhouette | 0x00447b50 | the background, the eight parts (each a flashing image, 0x005a6d10) |
| Weapons | 0x00446680 | with a weapon manager: the lines' looks (0x00446540, hooked) while shown, the panel, the 16 lines (0x00445c10), the bracket |
| Weapon line | 0x00445c10 | when changed, the second text's and the name's rendered images become the bar's fill and empty; the bar, the count (unless -1), the light, the mark |
| Equipment | 0x00448450 | the panel, the lines up to the first hidden one (0x00448030: name, light, value) |
| Objectives | 0x00447410 | the panel, the text, the scores |
| Panel | 0x0043ad00 | in software or when opaque; a see-through panel in Direct3D draws in the 3D pass |
| Light | 0x004486e0 | with an interval, the image steps each interval |
| Zoom window | 0x00442e30 | its scale grows at 4.8 a second while shown and shrinks at 7 while hidden; at full size its light shows the reticle's state, then its text; then the circle or the box |
| Reticle | 0x004441a0 | the zoom window; nothing more once the HUD is off with its mech dead; itself only while the zoom window is hidden; +0x954 counts down to 0 |

A flashing image (zTwoD's update 0x005a6d10): mode 1 blinks
(drawn on phase 1), mode 2 swaps its image with a second one each half
period (+0xbc, 0.35 s). Mode 0 falls through into mode 1's code (a missing
`break`): it draws, then draws again on phase 1. Ported as written.

**The Direct3D pass** (0x0043c6b0, the HUD's own, from the mech's vt+0xe4
0x0041cd90 for the view's target, in the Core frame before the 2D layer's
lock), with a mech and in Direct3D:

1. With the HUD on: TARGET's and the MFD's 3D view rects (in mode 1; for
   TARGET through 0x004425d0) given to the dirty rects (0x00576ce0).
2. One scene (0x00579af0 / 0x00579b10) of see-through quads (0x0043ad70: the
   panel's four corners into the TL buffer from the last one back, then the
   flat transparent draw 0x0057bf80 with the panel's colour and alpha; an
   opaque panel draws its image instead): with the HUD on, TARGET's and the
   MFD's view panels in mode 1, the weapons' while shown, the equipment's
   while it and the weapons show; always the message panel (0x0040fb30 on
   [0x00603d7c]) and the objectives' while shown; then the transparent
   flush ([0x0072f3e4]).
3. With the HUD on, TARGET's and the MFD's +0x28: in mode 1 the 3D view
   (TARGET: the target's vt+0x68(1), 0x004428b0 on its view and the
   target's node, vt+0x68(0); the MFD: its view's zView +4, 0x00442870);
   in mode 2 the damage view's +0x28, which is empty.

A panel's own +0x28 (0x0043ad20) is the same quad in a scene of its own;
nothing in the HUD calls it. Ported (`Hud::draw3d`, `HudPanel::draw3d` and
`draw_quad`, the MFD's and TARGET's `draw3d`); the program runs it at the
view target's vt+0xe4 with the scene, the quad and the flush on the zVideo
ports. Hooked: the dirty rects, the message panel and the 3D views'
rendering (0x004428b0, 0x00442870: the render work). Test: test_hud.py's op
17 runs the pass between frames with the render calls logged (100/100).
Breaking it: 9 of 10 caught; the miss (the quad's opaque test at 0xfe) is
equivalent, set_alpha (0x0043a9d0) making 0xfe and up opaque.

In the program, `hud_handoff` runs the frame (`Hud::update`) inside the Core
frame's 2D layer, and the Direct3D pass (`Hud::draw3d`) at the view
target's vt+0xe4; the hooks the program does not implement answer 0 (no
debug overlay, the aim state left alone, no nav list; the radar's outside
as section 15 and Left say).

Test: `test_hud.py` runs, after the build and the aim, a random sequence of
frames (dt 0 to 2.5 s) with changes between them (the HUD on or off, the
zoom window shown or hidden and round, the reticle's state, the zoom light's
interval, a damage part's flash, the MFD's mode, any piece shown or hidden),
the unported pieces stubbed (the aim state sets a state 0..3, as in play),
zRender's 2D draws logged, and compares every draw and the HUD's fields
after, on 100 runs (126,437 calls). Ops 13 to 16 (every child dirty or
shown, texts and images set) are there because most children are managed
(drawn only when dirty) and empty until their unported setters run. Breaking
it: 21 of 23 caught; the misses are equivalent here: the scores box has no
lines (its setter 0x00446ad0 is not ported) and the MFD view's box, a managed
PolyLine, is never made dirty in the test.

## 12. The missile lock and the reticle's image (0x00443d20)

Run from the HUD's frame while the reticle's state is still 5 (once a
frame), with whether the reticle's point lies over TARGET's shape or circle
with a live target in range (0x00440a30, hooked). Reads:

- the weapon manager's selected group (mech +0xd4): its longest range
  (0x004b1bd0), whether any weapon in it locks (0x004b1c70) and 0x004b1cd0
  (both needed); not ported, mac 4 ports them with the AI weapon planner;
- TARGET's slot's contact: its mech (0x004e6ff0; kind 7 at +0x684 +0xc never
  locks) and its distance (0x004e7030, zai_target);
- the aim's distance (+0x924) against the same range.

"Near": the contact or the aim point within the group's range. A good frame:
over the target, a group that locks, near, a contact, not kind 7.

| +0x950 | Next | On entering |
|---|---|---|
| 0 none, 4 lost | 1 on a good frame, else 0 | 4: both lock tones stopped (HUD sounds 0 and 0xc) |
| 1 acquiring | 2 on a good frame, else 4 | tone 0xc unless playing; +0x954 = 0.6 s, 1.2 s when the target mech's +0x3cc is above 0 |
| 2 waiting | 3 once +0x954 reaches 0, 4 on a bad frame | |
| 3 locked | stays on good frames or while +0x954 is above 0, else 4 | on a good frame +0x954 = 3.5 s; tone 0xc stopped, tone 0 unless playing |

Not near and not locked: 4. The reticle's update counts +0x954 down. While
locked the weapon manager's +0x88 is set and the reticle shows ret_on
(state 1); otherwise ret_off (0), then ret_over (2) while waiting, or by the
aimed hit's node (+0x94c): its object (0x0040e920, asked twice) with its +8
byte and the aim near: over; no object and the node's +0xbc above 1: over;
nothing near: ret_cant (3). With the image kept (+0x90c) the state still
changes but the image does not.

Ported (`HudReticle::aim_state`); the outside through HudHooks. In the
program (`app_hud`) the hooks are the player's mech, its WeaponManager with
mac 4's group getters (`weapon_planner.h`), its target contact (TARGET's
slot is the mech's `target`, +0x6c) and the contact's mech and distance
(zai_target); "kind 7" is the target Pilot's roster index, the Elemental,
and +0x3cc its ECM count. "Over TARGET's box" (0x00440a30) waits for
TARGET's tracking (0x00440b00), so the program never locks yet. The aimed
node's object is mac 2's `node_entity` (0x0040e920, app_weapons.cpp); its
+8 byte is ZEntity's active flag (set by the constructor, cleared at death),
read by the object's class (`entity_active_flag`, app_hud.cpp: a mech's or
vehicle's, a turret's, a targets.zrd object's or trap's); the node's +0xbc
is its damage callbacks' record (zwep_damage), "above 1" meaning it has one.
Each frame the image goes to off first; then an object with the flag and
the aim in range shows over (yellow); no object but a damage record shows
over at any range; neither, out of range, can't.

Playtest W6 (2026-10-29): until then only a mech's flag was read, so a
turret, vehicle or targets.zrd object never turned the reticle yellow
(buildings with only a damage record did). mw3tests
`mission_c1m1_reticle_turns_yellow_over_each_kind_headless` aims at one of
each in c1m1 (in range: over; out of range or dead: off) and at a building
(support00, a damage record and no object: over at any range); with the old
mech-only read, the turret, vehicle and target checks fail.
mw3tests `mission_c1m1_hud_weapon_lines_and_lock_headless` forces "over"
(`program_hud_force_over`) with the Owens as the target and the LRM group:
acquiring, waiting, locked 0.6 s later.
Test: test_hud.py runs the original 0x00443d20 in the frames, its outside
stubbed (the range and distance on the x87 stack), the hits' nodes real
blocks with +0xbc; the weapon manager's +0x88 compared after. Breaking it:
12 of 12 caught.

## 13. The weapon lines (0x00446520, 0x00446240, 0x00446540, 0x00445dc0)

Refreshed (0x00446520: layout, then looks) when the list is shown
(0x00446200), when its reader gives it the mech (0x00446120), and by the
weapons and the camera's look from outside (hud.md section 1).

- **Layout** (0x00446240): the manager's five groups in order fill the
  sixteen lines (0x00445d10 per weapon); the current group's lines get the
  bracket (the PolyLine: 11 pixels either side of the list's x, from the
  group's first line to its last, 10 pixels a line); unused lines hidden;
  the panel moved to the list and sized to the column by 10 per line; with a
  HUD, the equipment list placed 6 pixels below, and the reticle reset when
  the group cannot lock.
- **A weapon in a line** (0x00445d10): the weapon's name into the name and
  the second text; both look flags flipped and set back (0x00445a60), so the
  fonts are set again; then the refresh.
- **Looks** (0x00446540), each shown line: the select mark for the current
  weapon's line in the current group (single fire), every line of the
  current group (group fire), or select for the current one and chain for
  the rest (chain fire); none outside the current group. Then the refresh;
  the list keeps refreshing every frame while any line recycles. +0x1143c:
  the weapon after the current one (else the first line's).
- **Refresh** (0x00445dc0): the weapon's vt+8, then the bar at its recycle
  fraction (vt+0x34, clamped to 0..1; +0x111b when it drops from full), the
  light by damage (0x004b3010: green, yellow over 0.5, red over 0.8), the
  count its ammo (vt+0x14, "%3d"), the look red when it has ammo and is
  enabled (+0x74) and not destroyed, grey when destroyed (damage 1).

- **The bar** (a Slider): the second text (the name in wepstring_green,
  or the look's font) is the fill, drawn up to the value; the name (red,
  yellow or grey) the rest, from the cut. Both are the texts' own images,
  so the line's destructor (0x00445970) sets them to none before the
  Slider's (0x0059e170) would release them.
- **Every frame**: the list's update (0x00446680) runs the looks (and so
  the refresh) only once +0x11438 is set, by a shot (0x004464c0, the
  weapons' fire paths) and kept while any line recycles. In the program
  that is `WeaponCalls::hud_fire`; wired in 258364d. Before it the bars
  never moved: playtest #8.

Ported (`HudWeapons::refresh_all/layout/looks`, `HudWeaponLine::set_weapon/
refresh_line`, `~HudWeaponLine`); the manager and weapons read through HudHooks (weapon.h's
WeaponManager and Weapon, read-only). The HUD's handles are pointers
(`void*`): the program passes its own WeaponManager*, Weapon* and Mech*; the
probe's are the emulator's 32-bit addresses, mapped back by `id()`. In c1m1
the Mad Cat's ten weapons show, each named by its definition's DESC, the
current group bracketed (mw3tests above).
Test: test_hud.py gives the fake mech a manager with up to five groups of
fake weapons (16 lines at most) and runs the original's refresh: 100/100.
Breaking it: 11 of 13 caught; the misses are equivalent here (the unused
lines are already hidden by the reader's set_weapon(nullptr), and the bar's
set_value clamps a negative fraction to 0 itself).

## 14. TARGET's tracking: the target on the screen

The pieces, ported in order (the lead's plan, 2026-10-20):

1. **A world point on the screen** (0x0053cf10, `zmod::project_point`,
   zmodel/gmod_screen): through the view (on zMath's stack: push, load_view,
   pop), projected (0x005234f0) and clamped to the point rect (0x0074031c);
   the outcode: 1 left, 2 right, 4 above, 8 below. A point behind the near
   plane (0x00740304) gets 8, y on the bottom edge and x spread across the
   screen (-5000..5000 over the clip's width from its left; the clip's right
   0x00740314). FIX (bug): there the original ORs the low bits of the input
   point's address (an unset local when the top is the identity) into the
   outcode; the port returns 8. And its way back with pixels doubled
   (0x0040da30, `render_to_screen` in options: x doubled, y from the
   render's top doubled onto the window's, clamped to the window). Test:
   `test_project_point.py`, 3000/3000 each (586 behind, 575 at the near
   plane); breaks 9/9.
2. **The tracking** (0x00440b00, `HudTarget::track`, each frame with a live
   target, from the HUD's data update 0x0043c390): TARGET's camera put
   behind the target (the target's node position less 1.2 x the contact's
   size, 0x004e7070, along the contact's direction, 0x004f3870) and turned
   from the player toward it (`zmth::direction_angles` 0x00523ac0,
   0x004f36b0). The target's position projected (part 1), through the zoom
   window's view first when it shows: on the zoom window it is used there;
   off it, through the player's view, kept on screen only when it lies past
   the zoom window's side its outcode names (else nothing). The lines,
   marker, circle and shape hidden, then by the outcode:
   - **On screen**: with a targeting computer (equipment kind 14, mech
     +0x3e4) the **lead circle** (radius 6, the Frame) at the lead point
     (0x0041d940, `lead_point` in mech_hud: where to aim the selected
     group's current weapon so its shot, at the definition's speed or
     instantly for beams and instant weapons, meets the target moving at
     its velocity relative to the mech; from the mech's own aim point when
     it has one), clamped 7 pixels inside the display; green, or with ECM
     on the target yellow (255, 226, 4) and thrown up to 4 pixels each way
     by rand. Then the **triangle** (0x00440510): three lines round the
     target's projected position, its height the target's on screen (its
     centre against its centre raised by its size; a new target's pulse
     +0x240c adds 3.33 x that of the screen's height and runs down), red for
     an enemy, blue for an ally (the mech's +0xc8 / +0xc4 masks against the
     target's +0xc0 team), else green; in a round zoom window each side cut
     to its circle (0x00440290, zTwoD's 0x005a3580 in floats), else clipped
     to its rect or the window's ([0x005e39d8]); TARGET's shape at its
     centre scaled 1.155 x the height (Shape's 0x005a3530 is a float scale:
     zTwoD's `set_scale`).
   - **Off an edge** (1 left, 2 right, 4 above, 8 below), projected through
     the player's view: the team's arrow (enemy, ally, other: left, right,
     up, down) on that edge, kept on screen along it. Corners (3, 5, 6, 7)
     and the rest show nothing.
   - Then the MFD mode's tail for the target: 2 the damage view (0x00447a50),
     3 the nav list (0x0047ef10), both hooked.
   FIX (bug): the lead point with no group selected (the original reads
   through a null group, a crash; `fix_lead_point_without_a_group`). Test:
   test_hud.py's ops 18 (the tracking; projections answered from a table,
   rand an MSVCRT LCG on both sides) and 19 (the lead point, on a second
   fake mech with a contact and an aim point): 100/100. Breaking it: 15 of
   21 caught; the six misses (the targeting computer's kind, the lead
   circle's ECM colour, jitter and clamp, the new target's pulse, the round
   clip's end test at equality) need a target in the player mech's own slot,
   which the set-up only takes once set_target (0x00441800) is ported:
   part 3 reruns them.
3. **A target given to TARGET** (0x00441800, `set_target`, with the slot the
   set-up hands it: the mech's own +0x6c): with a live contact TARGET shows
   with its three lines and texts (0x00441560) and turns on (0x00441780: the
   triangle's 0.3 s pulse, the reticle reset); its texts (0x00441660): the
   target's mech into the damage view and the nav list, the mode the saved
   one (else 1 in Direct3D, 3 in software), the first text the target's
   name (vt+0xa4); a target whose vt+0xa8 answers (dead) gets mode 0 and no
   mech. **Over the target** (0x00440a30, `over`, the lock's "over"): a
   NARCed target (its mech's +0x570 above 0) always; else the reticle's
   point inside TARGET's triangle shape (zTwoD 0x005a3440: below its apex,
   above its base, within 0.577 of the depth either side, after the shape's
   points are worked out, 0x005a3270, also on its update 0x005a3550) or
   inside the lead circle (0x005a3b30). Test: test_hud.py with a target in
   the fake mech's own slot half the time, the original 0x00440a30 in the
   frames, contact mechs with NARC timers: 100/100. Breaking parts 2 and 3
   together: of the 17 left from part 2 and new here, 11 caught; the misses:
   the ECM colour's blue 4 (the same 565 pixel as 0), TARGET's shape never
   drawn by TARGET's update (its transform matters only to the hit test),
   the circle's and the shape's edges only at equality, the round clip's
   end at equality, and the dead target's mode (rarely reached).
4. **The HUD's data update** (0x0043c390, `Hud::data_update`, each frame
   from the camera update 0x0040f8d0 for the view's target): the speed bar
   (the controls' throttle, IControls vt+0x1c, x the speed factor +0x528 x
   the top speed +0x518, made positive); with the HUD on, the radar's
   direction (the mech's 0x0041cdf0, or with the radar active the view
   camera's +0x20 flattened and normalised, then 0x0043f2d0), the compass
   heading (atan2 of the mech's flat forward +0x24: x against -z), the
   throttle target (vt+0x2c), the heat gauge (heat +0x554 over 1.5, capped
   at 1; its rate: +0x558 a second less +0x55c), the coolant (+0x56c), the
   jump jets (equipment kind 11, +0x3d8, and the fuel, vt+0x148), TARGET's
   text (0x00441850), the equipment list (while the weapons show), the MFD;
   always the mission timer ("%02d:%02d:%02d" of the game time, a network
   game's own), the objectives, TARGET's tracking, the network's. Hooked:
   the gauges' setters, the radar's parts, TARGET's text, the equipment,
   MFD and objectives updates (items 4, 6, 7, 9 of section 7). FIX (bug):
   the timer's seconds past an hour (`fix_hud_timer_past_an_hour`). In the
   program the camera update (GameEngine::camera_update) runs it for the
   view's mech; the message panel's slide before it is not ported.
   In the program (`app_hud`) the tracking's and the lead point's outside
   is the player's and the target's mechs: positions from their nodes, the
   contact's direction and distance, the mech's height as the contact's
   size, teams from AiTarget, `zmod::project_point` for the projection; the
   targeting key path turns TARGET on through `program_hud_target_set` (the
   HUD part of the mech's set_target 0x0041d890). Left: the target's centre
   (vt+0xbc) is taken as its node's place, the mech's own aim point
   (+0xec / +0xf8) is not modelled, the target's name (vt+0xa4) and the
   TARGET and zoom cameras (0x004f3870 / 0x004f36b0, the render work).
   mw3tests `mission_c1m1_hud_weapon_lines_and_lock_headless`: the Owens
   targeted with the LRM group, the player turned to it and the torso
   pitched to keep the reticle in TARGET's triangle: acquiring, waiting,
   locked 37 frames (0.6 s) later, nothing forced. test_hud.py's op 20 runs
   the original 0x0043c390 (the controls through the IControls virtual
   base, the gauges' setters stubbed): 100/100; breaking it 9 of 10 caught
   (the radar's view direction with the radar active, which the test never
   sets: the radar's item).

## 15. The radar's update (0x0043dd00)

Run by the HUD's frame each frame. In order:

1. **A changed mode** (+0x5d, cleared at the end; set by the show, hide,
   zoom and attach): over the mission map (+0x5c) the polylines hide and the
   box and its three lines clip to the map's rect (the mission map from
   0x00491c80, its +0xd8), else the polylines show when they hold more than
   three points.
2. **Size and place**: over the map the radius is the mech's sensor range
   (+0x9c) x the map's scale (+0xd4) and the place the mech's on the map
   (0x004586e0: -(dx, dz) x the scale from the map's origin, a report when
   it falls off 0..640); else they step toward the target size and place by
   the step sizes (0x0043dce0 clamps each step). A radius of 0 hides it.
3. Nothing more while the HUD is off or the radar hidden. Moved: over the
   map the light marks the mech; else the range text and the light sit at
   the radar's lower right (0.714 of the radius, the light 14 right and 10
   up), and the cone's edges are worked out again (0x0043f130).
4. **The range text** "%.1fkm" of the sensor range (over the map the map
   draws itself instead, its +0x24); **the light** green, yellow or red by
   the mech's +0xbc (bit 1 yellow, bit 0 red), over the map drawn only
   inside the map's rect.
5. **The polylines** (+0xbd4 / +0xbd8 points from the lists +0xdb4 /
   +0xdb8, each item's place by its vt+0x18; the mission boundary, below): each
   point relative to the mech, scaled by radius over range, turned into the
   mech's frame (its orientation +0x48, conjugated, 0x00525580 / 0x00525740),
   closed back to the first, cut to the radar's circle (clip [0x005e39c8],
   the radar's square, set when it moves).
6. The box (the FilledBox with its static), the cone's two edges, over the
   map the third line; then, with a radius, the contacts (below) and the
   mode flag cleared.

**The boundary lines** (0x0043d920, from the mission start at 0x00491696
on the view's HUD + 0x14; playtest #30, hud-plan item 9). The two lists are
the mission boundary's regions, from the player's settings (its controls'
vt+0x90, the settings' +0xd0 vlist): 0x004d92f0 keeps the last vlist entry's
region whose name starts `reg_inside_` (11 characters), 0x004d9360
`reg_outside_` (12). Each list's count is the region's corners (vt+0x10),
its points the edges' starts (item vt+0x18). The inner line is yellow
(0x00579370(0xc3, 0xad, 3) into +0xcbc), the outer red ((0xff, 0, 0) into
+0xda8). A polyline holds 21 points and the radar writes count + 1 (the
loop closed), so a region of more than 20 corners would run past it; the
shipped ones have 6 to 12 (survey in mission_c1m1_radar_shows_the_boundary_headless,
MW3_SURVEY=1; c1m1 6 and 6). In the program `ProgramHudHooks` reads
PlayerControls' settings, and the launch's hook finds the player's HUD (its
argument is the exe's 32-bit HUD + 0x14). Test: c1m1's radar shows both, 6
corners each, closed, yellow and red; break: no region found, no lines.
The survey's zeros (checked against the archives' region names): c2m2
ships no `reg_inside_` / `reg_outside_` region at all (its readerm2.zbd's
regions are `reg_1to2`, `reg_convoy`, `reg_m2n2defend`...; the player's
boundary entries set the valves `inside_c2m2` / `outside_c2m2` with no
region in item 10, and no condition bit either, so they never fire: 0x004d8e70
needs a region for its inside/outside test; nothing else, no script or
animation, sets `outside_c2m2`, so the mission's "left the mission area"
loss cannot happen), so its radar rightly shows no boundary; c4b m7 is no
mission (c4b ships m4..m6; the `c4bm7` regions in readerm6.zbd are m6's
boundary, fixes.md), nor are the numbers past each operation's last.

**The contacts** (from 0x0043e445, `HudRadar::update_contacts`). Every
place below is first thrown by the radar's static: x and z each up to
noise x 100 metres either way (two `rand()`s, 1/32767 [0x005c00e4]), so a
jammed radar's blips jump about. Off the map a place becomes a point in the
circle by its distance times radius over range, turned into the mech's frame
(the conjugated orientation's matrix, x and z kept), from the radar's centre.

- **The mech's contacts** (its +0xac..+0xb0): the one TARGET holds (the same
  pointer, or the same entity +8 and class) always, the rest only while on
  the radar (+0x2c bit 0). The distance is the contact's own (0x004e7030),
  held to the radius, so a contact out of range sits on the rim. The blip is
  the vehicle's vt+0x3c by side: 0 ally (its team +0xc0 in the mech's allies
  +0xc4), 1 enemy (+0xc8), 2 other; a mech's are three shared images,
  hud_mech_ally_sm, _enemy_sm and _other_sm (made with the mechs,
  0x00413e60; `mech_radar_blip` 0x0041ec60). TARGET's own contact is drawn
  instead as a small triangle (a Shape scaled 5.77, clipped to the screen
  0x00579880) in blue, red or the HUD green by the same sides.
  The place is the object's (0x005a85e0, its node's world position +0x84,
  refreshed when stale): for a targets.zrd building that is the world's
  origin, the building's mesh being in world co-ordinates (c3m1's hqbuild:
  node at 0, 0, 0, centroid at -860, 24, 193), so the exe points its
  triangle at the origin at the contact's range (they are on the radar only
  when targeted). With the bug fixes the port takes a targets.zrd object's
  centroid (W32, fixes.md); `mission_c3m1_hq_targeted_is_ahead_on_the_radar_headless`.
- **The waypoint marker** ([0x00609980]'s selected element, 0x0058b060:
  the mission waynet's point, vehicles.md, The nav waynet; first taken for a
  network player's): its image by its +0xc (hud_mfbwaypoint_icon
  [0x00608bac] for a dock point, else hud_waypoint_icon [0x00608ba8]),
  placed like a contact but by its own distance (held to 0..radius), and
  the second text: message 0x228, "OP: %1 [%2km]", with its name (+0x28)
  and the distance in km ("%.2f") past 1000 m, else 0x229, "OP: %1
  [%2!d!m]", with the metres rounded up.
- **The shots in the air** (the weapons' radar list, weapon.h
  `radar_blips`: the missiles, SHOW_ON_RADAR; each shot's tick lists it
  again every frame and the list's object 0x004b4ab0, made at the
  mission's setup, empties it at action priority 2): a pixel each (Pixel2D,
  0x005a6b80) inside the circle, red for the shots hostile to the player
  (0x4b4a40's flag: the player's team among the shooter's enemies), the HUD
  green for the rest; over the map only where the map takes the point.
- **The landmines** (0x00604e48, landmines.zrd): every armed one within
  100 m (the squared distance against 10000 [0x005c00f0]) and inside the
  circle, a red X of two 3-pixel lines.

Over the map each place goes through the map (0x004586e0) instead; the
contacts and the marker ignore its answer, the shots and the mines are left
out when it says no.

The exe's data starts the radar's and TARGET's clip rects at (1, 1, 639,
479), the port too. Ported whole (`HudRadar::update`, `update_contacts`), the outside on
HudHooks (the map, the mech's range, flags and orientation, the polylines'
lists, the contacts and their vehicles, the marker, the shots and mines
read-only). Test: test_hud.py, ops 21-24 (the polylines' counts and lists,
the map mode and the changed flag), the map with a rect and scale; the
mech's contacts (TARGET's, one the same by entity, others on and off the
radar), blips or none, a marker near and far, shots and mines; the
marker's message arguments logged on both sides. Breaking it: 35 of 36
caught; the miss (TEXT2 shown again each frame) cannot show, nothing hides
it.

In the program the radar reads the mech's sensor range, flags,
orientation and contacts, the torso's flat front for the cone (0x0041cdf0:
the torso +0x16c), and the weapons' radar list; the mechs' blip images are
made when first needed (the original makes them with the mechs,
0x00413e60) and freed when the HUD is unwired. mw3tests'
mission_c1m1_hud_weapon_lines_and_lock_headless checks it in c1m1: the
radar shown, no blip while the Owens is powered down (not in sight), its
blip 150 m straight ahead once awake (8 px out of the wanted 7.5), and the
triangle instead once targeted.

## 16. The damage displays and TARGET's text

**A part's image** (0x004475c0, `HudDamagePart::set_level`): five images
per part (the silhouette sets, section 4), picked by the part's internal
structure 0..1: exactly 1.0 the first (intact), above 0.7 the second, above
0.4 the third, above 0 the fourth (the band above 0.2 and the one below it
share it, as coded), 0 or less the fifth (destroyed). Armor plays no part.

**Images the data lacks** (playtest #10): of the 52 images the HUD asks
for here and for TARGET's arrows, 12 are in no shipped library, in MW3 1.2
or Pirate's Moon: `dmg_<part>_blk` for the eight parts and
`hud_othererrow_left/right/up/down`. Only the exe names them. zImage's
lookup (0x005179d0) returns 0 for them without a report, so a destroyed
part's image is null (nothing drawn: the part leaves the silhouette) and so
is TARGET's arrow for a target that is neither ally nor enemy (team 2,
section 14). The port does the same; the SDL host reports each missing name
once. rimage.zbd does hold `hud_navarrow_*` and `hud_mfbarrow_*`, which no
HUD code asks for (fixes.md, Candidates). Test:
`playtest_hud_images_the_data_lacks`.

**The view's levels** (0x00447a50, `HudDamageView::update_levels`): with a
mech, its parts' internal (Part vt+0x48, `get_internal`) into the eight
images: the head (+0x3a4), the torso's sections 1, 2 and 0 (right, left,
centre; +0x39c), the arms (+0x394, +0x398), the legs (+0x38c, +0x390). The
player's view and a target's differ only in where each image sits (the
target's mirrored, section 4's orders).

**The MFD's data update** (0x004424b0, `HudMfd::data_update`, from the HUD's
data update): mode 2 the damage view's levels, mode 3 the nav list given
the HUD's mech (0x0047ef10, hooked). TARGET runs its view's levels from its
tracking (section 14) while in mode 2.

**TARGET's text** (0x00441850, `HudTarget::data_text`, from the HUD's data
update). Without a live target: hidden, the texts cleared (0x00441660).
Else shown, then:

- The second text: for a mech "mech: loadout" (the mech's display name by
  kind, 0x00484670: message 0xd82 + kind, and its loadout's name, the
  Config +0x3ec's +0x18), a network mech
  (vt+0x5c) its mech's name only, any other vehicle its own name (vt+0xa4);
  then the distance, " (500m)" (metres rounded up) to 1000 m, "%.2fkm" past
  it, run straight on with no space or bracket ("Owens: Standard1.23km", a
  slip; the rebuild writes " (1.23km)" behind zfix bugs, fixes.md); then "[part]" when the player has a
  part aimed at (+0xf8, 0x0041d510; its name 0x00484d70).
- The third text: message 0xdd4 with the name of the mech the player's
  saved or ally contact holds (0x004d7240), else empty.
- By the target's side (its team +0xc0 against the player's allies +0xc4,
  enemies +0xc8): the 3D view's box blue, red or the HUD green (none: a
  yellow-brown 0xc3, 0xad, 3); the texts all blue for an ally; red, red, blue
  for an enemy; green, red, blue otherwise; the silhouette's background the
  blue, red or green one.

Ported whole; the outside on HudHooks (the contact's distance and mech, the
names, the aimed part, the ally contact, the message). Test: test_hud.py,
the fake mechs with parts answering their internal and a vtable (network
flag, name), a pilot, the names by kind and part, MFC's CString as a
pointer to a copy of the text (untraced), the message's argument logged. 100
seeds match. Breaking it: 13 of 14 caught in TARGET's text and 11 of 11
in the damage displays; the miss (the box's colour for a target with no
vehicle) is out of the test's reach, since the tracking reads the vehicle
unchecked, so the test always answers one.

In the program the damage views read the ported parts' get_internal, the
names come from Mech3Msg.dll (a mech kind's message 0xd82 + kind; the
parts' 0x194..0x19a, then literal names for the hands, calves, feet and
hip), the loadout's name from the mech's Config, the aimed part from +0xf8
and the weapons' part_by_id, the ally contact from the AI's saved_or_ally.
A mech's own name (vt+0xa4 0x0041df30: the player's pilot's name, else the
brain's 0x004aee70, its vehicle record's pilot) is wired in the program
(`mech_brain_name`); the c1m1 HUD test checks the AI mechs have one.
mw3tests' mission_c1m1_player_aims_with_the_reticle_headless puts TARGET on
the Owens in mode 2 and fires: the hits wear a torso section's internal to
0.899 and its silhouette part changes image (at frame 89), the rest stay
intact. Headless runs load no 2D images, so the test gives the silhouette
set distinct stand-ins first.

**What it shows** (Owen's playtest, 2026-10-29: "only the left arm
outlined (or the right one)"). Each part is an outline over the
background's translucent fill (`dmg_bg_blue`, an alpha image of 0x001f;
red or green for TARGET's enemy or neutral): blue at full health, green
above 0.7, yellow above 0.4, red above 0, nothing when destroyed (the
`_blk` images are not shipped). In the program seven of the eight blue
outlines are the fill's own 0x001f (`dmg_rarm_blue` alone is 0x003f), and
the layer's cover (render-frame.md, FIX (A)) took them for the fill and
drew them see-through, so at full health only the right arm showed (the
left of the silhouette, the player's own seen from behind); damaged parts'
colours differ and showed. Fixed in the cover, not here: an opaque write
ends a pixel's record. mw3sdltests
`sdl_host_damage_diagram_shows_every_part` draws the view with the shipped
images at every band for each part and checks every outline texel opaque;
with the cover unfixed it shows Owen's picture (seven parts with 0 texels
at full health). Shots: c1m1, `--tap 0x20@800` (all blue at frame 900);
with `--vehicle-ahead 40 --vehicle-hold` the vehicle's hits turn the left
arm and leg green by frame 2400.

## 17. The gauges' setters, the objectives' place, the equipment's update

Run by the HUD's data update each frame (section 14 part 4).

**The gauges** (`HudGauges`):

- Speed (0x004453a0): the mech's speed x 3.6 ([0x005c013c], m/s to km/h)
  kept at +0x34; the text "%0.1f\n%s" ([0x005e4154]) with message 0x1d7
  (the unit), the result used as the text's format in turn; green
  (0x1fa201) from 0 up, red going backwards.
- Throttle (0x00445450): clamped to -1..1, backwards x 0.525 ([0x005c0140])
  and red, else the yellow-brown (0xc3, 0xad, 3); the bar's top (its points
  0 and 3) its height x the level (rounded up) above its bottom (point 1).
- Heat (0x004455e0): the toggler on the meter at level + rate (clamped
  0..1; the rate is the heat's change a second less the cooling), the bar's
  lit part the level of its image from the bottom (its source rect +0x1440,
  the bar moved down by the unlit height).
- Coolant (0x004456f0): eased a tenth of the way toward the value each call,
  that is each frame, in one value for every HUD ([0x005e3a14], 1 at
  start): the bar settles faster at higher frame rates; the rebuild eases
  a tenth per 1/30 s behind zfix per_second (fixes.md).
- Jump jets (0x00445520): the fuel frame and bar shown with jets, hidden
  without; the bar by the fuel (clamped 0..1).

**The objectives** (0x00446900 / 0x004468b0): shown, they sit their
panel's height above the message panel's point ([0x00603d7c] +0x4e8,
0x0040fed0), the panel 4 to their left.

**The equipment** (0x00448320): a line for each part the mech has of the
kinds 0, 12, 3, 8, 14, 6, 7, 4 in that order ([0x005e3a30]; the counts
+0x3ac), up to 16; none hides the list; the rest hidden; the panel 11 a line
tall. A line (0x00447e40): kind 0, the AMS, reads the mech's +0x3f0 (none:
the red light and grey font; else green or yellow by its on flag, with its
rounds +0x18 as the value), kind 12, MASC, green or yellow by the mech's
MASC time +0x574, any other green with no value; the names are message
0x1c6 + kind (0x00484980); a new kind renames the line (0x00484980) and
sets the green font over the one just set, so for one frame the line is
green whatever its state.

Ported (`HudGauges::set_*`, `HudObjectives::data_update`,
`HudEquipment::data_update`, `HudEquipLine::set_item`). Test: test_hud.py,
the fake mech with an AMS record and a MASC time, the message panel's point,
the item names stubbed; the speed text's double formatted in the sprintf
stub; op 26 changes the jump jets' count mid-run. 100 seeds match;
breaking it: 26 of 26 caught.

In the program: the AMS (Ams on/rounds), the MASC time, the item names
(message 0x1c6 + kind), and a stand-in for the message panel's point
(Left). mw3tests' mission_c1m1_hud_weapon_lines_and_lock_headless opens the
throttle for a few frames: the throttle bar stands 3 pixels up and the speed
reads 0.1 km/h at frame 10; the player's mech has no equipment and no jump
jets, so the list and the fuel gauge are hidden, as the counts call for.

## 18. The message panel (0x0040f410.., [0x00603d7c])

The in-mission messages ("Objective complete" and the like): a text box of
four lines on a see-through panel (the HUD green and opacity, 4 pixels
left of the text, sized to it), 320 wide, centred, in the hud_messages font
(hud_messages_bright made too). Ported as `mech3/message_panel`.

- **Made** once at start-up (0x0040f410, before the maps and the clock; the
  program's set-up calls it, ECX and EDX unused), freed at the end
  (0x0040f7d0). It starts hidden just under the display's bottom edge (y =
  display height + 1); shown, it sits its text's height above the bottom.
- **A message** (0x0040fe20: with a time; 0x0040fe50: without) adds a line;
  with a time over 0 (show_for 0x0040f5d0), a hidden panel or one going out
  starts coming in, holding for that time; one already in keeps the longer
  time. Callers: the objectives (0x0045a270, 0x0045e110), salvage
  (0x00470490), the debug keys (0x0040db60), the player (0x004a50e0), the
  AI's valves (0x004e6cxx).
- **Each frame** (0x0040f8d0's first part, before the HUD's data update):
  the time runs down and, at 0, the panel turns round; it moves 60 pixels a
  second toward its place (in: shown, up to its height above the bottom;
  out: down to its hidden place, then cleared and hidden). The step is the
  frame's 60 x seconds truncated: 3 a frame at 20 fps (60 a second), 2 at
  25 (50), 1 from 30 to 60 fps (30 to 60 a second), 0 above 60 fps, so on
  a faster machine the panel never moves (FIX (B): the fraction carried,
  the 60 a second the panel's speed says, behind zfix per_second,
  fixes.md).
- **Switches**: the panel key (mech command 0xa6, 0x0040f070) turns it
  round; the mission sets it out (0x0040f0b0) and resets it (0x0040f040:
  cleared, out, at its hidden place) at a mission's start.
- **Drawn** in the HUD's Direct3D pass (0x0040fb30, its quad while shown);
  its point (0x0040fed0) places the objectives above it (section 17), so
  they ride up and down with it.

Test: test_hud.py makes the panel after the HUD (0x0040f410) and runs ops
27-32 (a message with a time, one without, the toggle, set, reset, a slide
frame through 0x0040f8d0 with no view target); the panel's text, places,
time and quad in both dumps. 100 seeds match. Breaking it: 15 of 18 caught;
the misses cannot show (the time only runs while sliding in, so its turn
is always outward; the reset's x is the same hidden or shown; the panel's
and the text's draw order is not traced).

In the program GameEngine makes and frees it (the hooks
message_panel_init / message_panel_free, once misnamed mechs_init and
world_shutdown), slides it each frame before the HUD's data update, the
mission's hooks reset and set it, and command 0xa6 toggles it. Its text is
drawn by its update (vt+0x24, 0x0040f680) in the HUD frame 0x0040fac0
(`GameEngine::hud_update`, since HUD plan item 6; before it only the quad
was drawn and the lines stayed blank). The c1m1 HUD test checks the posted
line is drawn while the panel is up.
mw3tests' c1m1 HUD test adds a message held a second at frame 12: the panel
is up at frame 14 and down and hidden at 76 (headless text has no height,
so its travel is short); fix_message_panel_slides_above_60fps checks the
step at 120 fps.

## 19. The HUD keys (playtest #7, #13, #20)

The mech's command handler (0x0041be80, `Mech::command`) runs the player's
HUD keys on the view's HUD (0x00430aa0, null off the view). In the port its
HUD cases go through `CommandCalls::hud`, which the program answers in
`app_hud.cpp` (`hud_command`):

| Key | Command | The HUD's side |
|---|---|---|
| Z | 0x14 | none: the pick below (the zoom itself is the held key, below) |
| keypad 0 | 0x15 | 0x00442e00 on the zoom window (HUD +0x1508): its lock (+0x174) flipped, then shown (vt+0x60, -1) or hidden (vt+0x64); the click 0x17 at the mech's +0x614; then Z's pick |
| Q | 0x48 | the pick, without the side check |
| T | 0x49 | the target cleared through the mech's vt+0xc0 (0x0041d890): TARGET's 0x00441780(1) |
| F12 | 0x43 | the objectives (+0x17964): shown for good when hidden, else hidden |
| R | 0x68 | 0x0043da40 on the radar: 0x0043da50 with the other size |
| H | 0x6b | the HUD's vt+4 (0x0043baf0) with its +4 flipped |
| D | 0x74 | 0x0043d240: 0x00442440 on the MFD (+0x3434) |
| Ctrl+D | 0x75 | 0x0043d250: 0x00442440 on TARGET (+0x410c) |
| Shift+R | 0xa5 | 0x0043dc10 on the radar |
| Alt+R | 0x69 | the mech's range step, then vt+0xb8 (0x0041ded0): the radar amber (0x0043d630) while passive (+0xbc bit 2) |

- **The pick** (Z, keypad 0, Q; 0x0041c1ab..0x0041c285): the node the
  reticle's aim hit (0x00444330: the record at +0x928, its +0x24), its game
  object (0x0040eee0); for Z one of the mech's allies (its vt+0x28 +0xc0
  against the mech's +0xc4) is skipped; then the object's vt+0xac (Z) or
  vt+0xb0 (Q) may refuse: a mech never (0x004d7290), a targets.zrd object
  without zoom_select (bit 3 of its description's +0x14, 0x004d3360) or
  reticule_select (bit 2, 0x004d3370). Then 0x004a7b40: already the target,
  nothing; else the contact whose seen object (+4) it is becomes the target
  (vt+0xc0), and with none among the contacts the target is cleared.
- **The target set** (0x0041d890, the mech's vt+0xc0): AiTarget's
  0x004d5e40, then on the view's mech TARGET's 0x00441780(1) and the
  target sound (0x00432990 with 0x18) on the new target's mech, kept at its
  +0x618 (`program_hud_target_set`). The target keys (E, W and their Ctrl and
  Shift forms, 0x004d5ee0..0x004d6a80) set through this too, so each new
  target pulses, restarts the lock (the reticle's reset, 0.6 s) and plays its
  mech's own sound (`mech_<name>`); in the port `TargetHooks::target_set`.
- **The held zoom key** (the controls' update 0x00427d60): with the window
  not locked, the click whenever the window's hidden bit (+0x1514 bit 4)
  says the opposite of the key, then vt+0x60(-1) held or vt+0x64 let go,
  every frame. The program wires `ControlsCalls::screen_busy` (+0x167c, the
  lock), `zoom` and `PartCalls::screen_flags` (+0x1514) to the zoom window.
  Before, with none of them wired, the flags read 0 (shown) with the key up,
  so the click played every frame and the window never opened: playtest
  #20. `screen_flags` also gives the torso's slow turn while zoomed
  (30% of the rate, `mech_part_update.cpp`).
- **0x00442440** (the MFD's and TARGET's next mode): at pixel doubling
  (0x0040d4b0) the mode set straight to 3; else only when the mode is the
  one shown (vt+0x80), 0 or 3 to 1, 1 to 2, 2 to 3 through vt+0x7c (the
  MFD's 0x00442420 keeps it at [0x00605988], TARGET's 0x004400f0 at
  [0x0060598c]). TARGET shows none without a live target (0x004413a0), so
  Ctrl+D does nothing then; for a target that is not a mech it shows 1
  for any mode but 0, so one press from 1 leaves the mode at 2 and further
  presses do nothing.
- **0x0043dc10** (the radar over the mission map): +0x5c flipped; turned
  on, 0x0043da00 looks for the map (0x00458590 on the map list
  [0x00605acc]: the first with +0x3c set whose name +0xc0 holds "large"
  when the radar is large, else "small", then any with +0x3c set; kept at
  [0x0060998c]) and turns it back off when none; off, the radius and place
  are set straight to the ones it was stepping toward. +0x5d set.
  0x0043da00 also runs after every size change (0x0043da50).

Ported in `hud.cpp` (`HudZoomWindow::toggle_lock`, `HudRadar::toggle_zoom`
/ `toggle_map` / `select_map`, `HudMfd::set_mode` / `cycle_mode`) and
`mech_command.cpp` (the pick, the target set). The mission maps' list
(0x00458590) is not ported: the program answers none (recorded), so
Shift+R turns itself back off. F12's box's text (0x00446ad0) is section
24.

Tests: `test_hud.py` ops 34..38 (keypad 0's lock, R, D, Ctrl+D, Shift+R
on the HUD in the emulator, with pixel doubling, the maps' answers and
the saved modes in the dump; 100 seeds match); `test_command.py` runs the
pick for real in the emulator (0x004a7b40 on contacts whose seen objects
are the aimed one or not, the side check, the refusals) against the port
(6096 commands match); mw3tests' `playtest_hud_keys_zoom_radar_mfd_headless`
presses the keys in c1m1 (one click per keypad 0 press and per Z change,
none between; R's radar_up once; D, F12, H; Alt+R amber in passive and
green after). Breaking it: 27 of 28 caught; the miss (the MFD's mode kept
even when 0) cannot show, the keys never set mode 0.

## 20. The 25 text lines and the frame's 2D ticks (traced 2026-10-25, hud-plan item 1)

**The lines** ([0x5fc848], 25 objects of 0x4b0 bytes, y = 0x5f + 12 i,
the Arial 10 font set by 0x40f7f0 from the mission load 0x48faf0). Every
writer in the exe:

| Writer | What | Reached in the release |
|---|---|---|
| 0x480df0, 0x490480, 0x490ab0 | all 25 cleared (vt+0x7c with "") | yes, at the Core state's start and the mission's load and cleanup |
| 0x4a2ef0 | lines 1-4: a vehicle's developer readout (vehicles.md, The debug readout) | only with a vehicle as the view's target (its vt+0xe0); the player's view targets the player's mech |
| 0x4a56b0 | the stealth object's readout (BASIC / FLY ...) | only with the stealth object as the target: not in the release (vehicles.md, The stealth object) |
| 0x4a33f0 | all 25 cleared, then a listing of the mechs ([0x73655c]) | only from the app key handler's command 0x89 |
| 0x40fb50 | mode 0 clears all, 1 arms ([0x603f8c] = 1), 2 (each frame, from 0x40fac0) picks a line under the cursor | modes 0 and 1 only from command 0x83; so mode 2 never acts |
| 0x43c820 | the HUD's debug overlays (lines 6.., section 11) | only while the debug text [0x5fc378] shows: command 0x82 |
| 0x40e4b0 | the lines drawn and hit-tested by the mouse (hover colour 0xff00) | its hit test needs [0x5fc314], set by 0x40e430 and cleared by 0x40e7d0, only from the same key handler |

The app's key handler 0x40dba0 has 27 cases (commands 0x61..0xa4), but its
address appears three times in the exe, all in 0x40db60, which registers it
for commands 0x61 (X, the camera), 0x6c (Shift+V) and 0x70 (`, the message
panel) only (0x7e goes to 0x4abbc0). The other 24 cases (among them 0x82
the debug text, 0x83 the lines' arming, 0x89 the mech listing) are dead in
the release: the developers' bindings were taken out and the code left. So
**the 25 lines are developer-only**: a player only ever sees them cleared,
and the vehicles' readout only if a vehicle could become the view's target
(whether the camera cycle, X, can do that is not traced; hud-plan.md
section 6).

**The frame-rate text** 0x40f1f0 runs every frame (from 0x40fac0) on the
debug text object [0x5fc378]: frames counted over each second (+0x4b0
accumulates the frame time, +0x4b8 the frames, +0x4b4 their ratio), the
text formatted and drawn (0x5a11b0, 0x59ffb0). The object shows only when
command 0x82 toggles it, so it is never seen in the release.

**0x40fac0's order**: the view target's frame (vt+0xe0), 0x40fb50(2), a
0x59fd20 tick of the lines' layer 0x5fc338, 0x40f1f0 on the debug text
[0x5fc378], a tick of the debug text's layer 0x5fc838, the message panel's
update (vt+0x24), the pocket commander 0x46a410, and a tick of 0x731ac0, the
text console layer of the interp's debug console (twod.md). The program
runs it as one call (`GameEngine::hud_update`): the view target's frame,
the message panel and the pocket commander; the developer pieces are left
out.

**0x5a6790 / 0x5a67c0 are not the HUD's**: both take the object at
[0x736328], the manager the mission's weather is added to (0x59fbe0;
world-params.md section 7). 0x5a6790(i, rect) sets its rect slot i
(0x5a66b0); 0x5a67c0(dt) ticks its objects (0x59fd20, vt+0x24) and clears
its +0x1ec (0x5a6600). The Direct3D frame calls both after the world tick
(0x48187b, 0x48188b), so they update and draw the rain or snow. In the
program `create_snow`, `create_rain` and `weather_add` (app_mission.cpp)
were empty, so no weather was made: c3, c4 and t1 set
RAIN and c4b missions 4 and 6 SNOW in world.zrd. Both are ported since item
19 (`weather.md`).

## 21. The static (0x0043cd20, hud-plan item 6b)

When the player's mech is badly hurt the HUD's panels break up with green
static. The Direct3D and software frames call 0x0043cd20 on the view
target's HUD after the HUD (app_states' `hud_static`, once misnamed the AI's
radar update; its HUD is 0x00430aa0 on the mech's +0x684, asked twice).

- **Four channels** at +0x22e08 (`Hud::statics`), each a floor, a level
  and a hold: TARGET's (+0x22e08), the MFD's (+0x22e14), the weapons' and
  equipment's (+0x22e20), and the radar's fade (+0x22e2c, 0x0043dc70).
- **Adding** (0x0043bfe0, step, kind, hold, top): kind 0 the radar's fade
  (set on the radar at once), 1 the MFD's, 2 TARGET's, 3 the weapons'; 4
  all four, 5 a random one (rand & 3). The level grows by the step up to
  the top; the hold becomes the larger. Its only caller is the tick below.
- **The warning**: the least internal structure of the mission player's
  torso (sections 0-2, vt+0x48: +0x38, +0x1a0, +0x1a4) and head (+0x38).
  Under 0.4, and GameIntensity's flag [0x00608e4c] clear, a random channel
  gets 0.1 this frame (FIX (B), behind per_second: a 30 fps frame's) up to
  a top of (5 - 20 x) / 4 at or under 0.2, (0.4 - x) x 5 / 4 above (0.25
  at 0.2, 1.25 at 0), held for the frame.
- **The draw**, while the HUD is active and the radar is not shown zoomed
  over the map: the finish target on the frame's surface (the 3D lock's
  back buffer with a device, the software surface without; 0x00558290),
  then each channel with a level over 0 on its panel, clamped to 0..0.9
  (0x0043cca0): TARGET's on its shown rect (0x004425d0) one pixel wider
  and taller when its mode is not 0; the MFD's on its shown rect while
  shown (none: the whole target); the third on the weapons' panel while
  shown, and again on the equipment's while both are. FIX (bug): TARGET in
  a mode without a rect (the nav list unmade) is read through null in the
  exe; nothing is drawn.
- **The static itself** (zRender's 0x00558c10, `zrndr::static_fill`): per
  row a random start into the noise table (0x6400 bytes of rand made by the
  set-up 0x005581c0, from 0x00557b20 at start-up), per pixel its byte's
  third picks dark, middle or bright green ((32 64 32), (64 128 64), (96
  192 96)) when the next byte is under level x 256. No rect: the target
  less its last column and row. FIX (A): the table has a zeroed byte past
  its end, which the row's last pixel can read.
- **The fall**: past its hold (each counts down by the frame's time) a
  level falls 0.3 a second to its floor (the radar's fade set as it
  falls); under the floor it is raised to it.

Tests: test_hud.py ops 39 (the tick, the mission's player the HUD's mech),
40 (an add of any kind) and 41 (GameIntensity's flag), the fill logged
with its rect, level and colours; 100 HUDs match, 262 fills, the null
TARGET read skipped as the port. test_static.py checks the table and the
fill pixel for pixel (300 cases, 96 million pixels). 20 breaks caught.
mission_c1m1_hud_static_when_hurt_headless: the torso at 0.1, the static
up and drawn, gone after a repair. Screenshots: c1m1 with and without.

Open: 0x00557b30, the set-up's other half, clears GfxFlags bit 2, which the
software finish reads as MMX (render-tint.md); not traced whether the
release ever uses the MMX rows.

## 22. The 3D views in the program (hud-plan item 6c1)

The HUD's three zoom views (0x0043f660: the MFD's, TARGET's, the reticle's
zoom window) each make a camera and a window node (HudZView 0x004b5150,
HudViewWindow 0x004b5440). The program answers the HUD's node hooks with
zClass: the camera 0x004f32e0, the window 0x004f9e90 (a class 3 node, 1 x
1, +0xe4 -1, on list 14; its software target +0xe8..+0xf4, 0x00558080, is
left 0: no software renderer), the name 0x004eb300, the window's size
0x004f9fa0 and place 0x004fa0b0, the camera's window, world (the mission's,
the player camera's), zone, LoD scale (0x004f3ff0, the hook's "zoom"), field
of view, clip, flag 8 (0x004f34c0, data +0x10 bit 3), position and angles.
`ProgramHudHooks` keeps the nodes in a table and hands out 32-bit handles.

Drawing, in the Direct3D frame's HUD pass (0x0043c6b0): the 3D views'
rects have their depth cleared first (0x00576ce0 -> [0x00809348] =
0x00577770, the depth surface's DDBLT_DEPTHFILL Blt with 0; the port's
`zvid::depth_fill`, Render3D's `clear_depth_rect`, a scene of its own when
none is open), then a view in mode 1 draws its zView (0x00442870 -> vt+0x04
0x004b52c0: the camera's scene walk 0x004f7030, `render_world`; 0x004f6dc0
without a device, the software path, is not ported). Projection and clip2d
already place a view's vertices inside its window's rect, so the device's
viewport stays the screen's.

Test: mission_c1m1_hud_views_drawn_headless (a device and a zTwoD host
reading the engine's video mode): three cameras and three windows made;
with the MFD in mode 1, its view drawn each frame and its rect's depth
filled (a break that drops the device's fill caught).

**The MFD's 3D view (mode 1; playtest #29).** Its draw (0x00442500 ->
0x00442870) runs the view's zView's vt+0x04, and the MFD view's zView is
not the plain one: its constructor (0x00442620) gives it vtable 0x005c04f8,
whose +0x04 is 0x00442900 (the plain zView's, 0x005c9118, is 0x004b52c0, the
camera's scene walk). So 0x00442900 is that render, not a slot nothing
calls: the camera 13 m before the HUD's mech (the mech's local -z,
[0x005e3a08] -13) at 1.25 ([0x005c0124]) times its centroid's height over
its place (0x00448600, 0x005a85e0), through the mech's world 3x4 (+0x0c),
turned to the centroid (0x00523ac0, 0x004f36b0); then, with the mech's
see-through flag (object3d flag 2, 0x004fbb90) and an attached cockpit model
(MechData +0x228's node under the torso's) off, the mech's node drawn alone
(0x004428b0) between its vt+0x68(1) and (0). Mode 1 shows the player's own
mech from the front, as Owen describes; mode 2 is the damage diagram, mode
3 the armour bars (section 16).

**TARGET's view of its target (hud-plan 6c3).** 0x00441410 in mode 1
draws the target alone the same way: 0x004428b0 (the view's panel, vt+0x24
0; the world's fog off, 0x004f23e0 on [0x00608c84]+4 with 0, then 1; the
node through the view's camera, 0x004f73f0, `zcls::render_subtree`)
between the target's vt+0x68 tint. In the program `ProgramHudHooks`
dispatches the zView render by the view's class (`zview_render`: the MFD
view's 0x00442900, `mfd_view`) and draws a node alone (`view_draw_node`).

Test: mission_c1m1_mfd_shows_the_mech_and_target_its_target_headless: the
MFD in mode 1 draws the mech alone each frame, its camera 13.0 m before it
and 6.8 m up; with the Owens targeted and TARGET in mode 1 both draw (10
nodes in 5 frames). Break: the MFD view rendered as a plain zView, its
camera stays at the world's origin (2.2 km away) and draws no node.
Screenshots (c1m1, `build/mw3 --tap 0x20@N`): mode 1 the Mad Cat from the
front, mode 2 the parts diagram, mode 3 the armour bars.

**The target's node** (the object's +0x04, whatever its class). TARGET's
mode 1 shows any target that is not hidden (vt+0xa8), so turrets as well
as mechs and vehicles; targets.zrd's objects answer vt+0xa8 and show mode
0. The program read every target's node as a mech's (`Mech::node`). A
turret's `self` and `node` are 32-bit handles, so that read took the two
handles for a pointer, and `render_subtree` crashed on it (Owen's Windows
playtest, 2026-10-29: `Node::parent_count`). `object_node` now takes the
node by class (`hud_object_node`, the sensors' lookup). A removed object
resolves to null through its handle, and a dead mech keeps its node as
the wreck. Test: mission_c1m1_target_views_a_turret_headless (40 m from a
turret, the MFD on its nav list, TARGET in mode 1 on the turret: 5 nodes
in 5 frames). Break: the mech read put back crashes in
`Node::parent_count` under `render_subtree`, as in the playtest.

**TARGET's camera** (0x00440b00, each frame): the target's centroid (the
entity's +0x14 node, its data +0x84) less the contact's direction times
1.2 x its size, turned along the line from the player's centroid to the
target's. Both are centroids, a mech's too. The program took a mech's place
(its +0x04 node, on the ground) instead, so the view centred on the target's
feet: legs halfway up the window, the head off its top, on every mech
(playtest W7); the target triangle (0x00440510, centred on that point) sat at the feet
too. `entity_position` reads the centroid now; the place stays
`object_position` (0x005a85e0). Test:
mission_c1m1_mfd_shows_the_mech_and_target_its_target_headless, TARGET's
camera 5.2 m above the Owens' place, level with its centroid. Break: the
place read put back, the camera 0.0 m up.

## 23. The zoom window's 3D pass (hud-plan item 6c2)

The draw paths' `hud_zoom_pass` (the reticle's vt+0x28, 0x00444230) runs the
zoom window's 0x00442f50 while it shows or still has a scale (+0x170):

1. **Its camera** (0x004436c0): at the player view's camera's place (its
   data +0x14), turned toward the reticle's aim point (the hit record +0xc,
   0x00523ac0 from the camera's new place), in the player camera's zone
   (+0x1e4); the text shown (vt+0x60(-1)): message 0x22e "[range = %1m]" with
   the reticle's aim distance in "%.1f", or 0x22d "%1 [range=%2m]" with the
   aimed object's name (the hit node's object, vt+0xa4, none when empty);
   when that object is the mech's target (+0x6c), the distance is the
   contact's (0x004e7030) instead.
2. **The player's own mech hidden** (its node's active flag off, 0x004eb170)
   for the pass, on again at the end.
3. **The window's rect** from its camera's window node (0x004f3630,
   0x004fa130, 0x004fa020): x, y to x + w - 1, y + h - 1.
4. With a device: a **box** has the rect's depth cleared (0x00576ce0), then
   the view; a **circle** (+0x16c) has the depth cleared, then in one scene
   four quarter fans (each six points on the quarter arc, 0 to pi/2 by a
   tenth of pi, and the corner, all at depth 0.99) through [0x0072f3ac] =
   0x0057bd50 with the clear texture "zVidDefaultClear" ([0x00809320]):
   nothing seen, but the corners' depth set near, so the view only shows in
   the circle; then the view, then the rect's depth cleared again.
5. **The view** is the zoom's own zView class (vtable 0x005c0600, not
   zView's 0x005c9118): 0x00443320 pushes the world's fog back for the pass
   (the near range half way to the far, 0x004f24c0 / 0x004f2440 / 0x004f1df0),
   draws the camera (0x004f7030) and puts the fog back. Without a device it
   is the software path (0x00564460, 0x004f6dc0), not ported.

FIX (A): the port makes no clear texture; the fans are drawn untextured at
alpha 0, blended, depth written (`zvid::clear_fan`): the same nothing.
FIX (A): FormatMessageA's `%1`/`%2` with string arguments, for every
zTwoD text set by a message id (0x00575ff0), in the SDL host
(`zutl::format_message_v`); before it those texts were empty in the
program.

Tests: test_hud.py op 42 (the reticle's +0x28; the window rect, the fog,
the fans, the render and sprintf stubbed, the text's characters compared):
100 HUDs, 75 zoom passes, 120 fans, the name and distance texts. With a
device only. mission_c1m1_hud_zoom_drawn_headless: the zoom locked open
(keypad 0), drawn each frame with one depth fill (c1m1's is a box), the
camera at the player camera's place, the text "[range = 69.8m]".

## 24. The objectives box's text (0x00446ad0, playtest #26)

F12 (and any show, 0x00446820) refills the box from the missions object
([0x006098d0]) and the pilot's game ([0x0060861c]+0x6c); so does an
objective's completion while the player's mech is the view's (0x0045e110 on
the cockpit's +0x17964). The box (0x4c8 bytes of text lines) has a second
text box beside it, the scores column at +0x104 (+0x5d4): its own vt+0x7c
(0x00446930) writes a line and a blank score, so the columns stay in step.
In order, with the line count kept for the panel:

- **Header**: the mission's `desc` (+0x78); else by game type: 1
  "Operation %1, Mission %2" (0x177, the game's vt+0x48 / vt+0x4c), 2 the
  Instant Action line (0x231 with its name, 0x232 without), 3 "Network Game
  Scores" and its columns (0xdd5, 0xdd6, in rt_mp_scores, which stays the
  box's font), 4 "T R A I N I N G %1" (0x233).
- **Primary** (+0x7c), when it has conditions: the group's text (its
  default "Primary Objectives", 0x16d), else 0x235 in training, 0x236
  (with the operation and mission) outside Instant Action. Then each
  visible condition with a text, split on new lines: its first line by
  0x00446970 ("Completed" 0x22f or "In Progress" 0x230 in the score column,
  both in rtobjective_complete / _incomplete), the rest by 0x00446a20 (a
  blank score). The text is 0x0045e080: the description, two spaces and
  "OP: <nav point>" (0x200) when the condition names one, cut to 63
  characters.
- **Secondary** (+0x80): its text or 0x237, then its conditions.
- **Final** (+0x84, only once the primary group is done, 0x0048fab0): its
  text or an empty line, then its conditions.
- **Failed** (+0x88, only once the fail group is done, 0x0048fad0; not in
  multiplayer): an empty line, its text or 0x238, then each failed visible
  condition (no score).
- **The end**: in a network game the scores again (0x004361e0) and their
  lines at widths 0x14 and 0xc (0x00435b60), each a line in rt_mp_scores
  (ported 2026-11-01: `mission_multiplayer_score_lines`); else, lost
  (+0x56): an empty line, "M I S S I O N  F A I L U R E" (0x239) and "Press
  %1 To Exit Mission Area" (0x23a, Exit Mission's key, command 0x71); else,
  when every primary and final condition is done (visible or not, a
  mission without them included): the same with "M I S S I O N  C O M P L E
  T E" (0x23b). Last "Press %1 To Close Objectives Display" (0x23c, command
  0x43's key).
- The panel: 320 wide, the line count times the box's text height
  (0x005a1380) tall (0x0043ab10).

Quirks kept: the loss adds four to the count for its three lines, so the
panel is a line taller. The exe hands each condition's text to the line as
its format (with nothing behind it on the stack), so a '%' in a text would
read garbage; the port passes the text as the format's argument (FIX (bug),
fixes.md). No shipped text has one: every objectives.zrd `desc` is a
message key, and the messages with a '%' that objectives use (0x261,
0xdb1, 0xde0) are formatted before they become the text.

In the program `ProgramHudHooks` gives the mission's objectives
(`mission_objectives()`), +0x56, the launch's campaign's operation and
mission, the nav points' names and the keys' names (zInput's 0x0051d200 /
0x0051d330); the objective hooks give the cockpit (`program_mech_hud`) and
its refill. Training's names and multiplayer's scores are not wired (no
such launch yet). Instant Action's header (closed with frontend-plan item
12b): at 0x446bb4..0x446bee the 0x231 line's %1!d! is vt+0x48 of the
pilot's game ([0x0060861c] +0x6c; ActionGame's 0x004378b0: the world +0x74
as 1..4, else 0) and its %2 vt+0x0c of 0x00479460's object (0x0040a840: +0x3c,
the scenario's name the Action screen's ACCEPT set); without a name 0x232
with the world alone. The program answers both from the launch's ActionGame
(`mission_action_world`, `mission_action_name`); checked in
`instant_action_menus_to_wave1_tallies_headless` ("I N S T A N T  A C T I O
N   1   Battle Progression" on Big Lake).

Test: `mission_c1m1_objectives_box_text_headless` (F12 on c1m1: "Operation
1, Mission 1", "Primary Objectives", "Take Com. Center Offline  OP: Able"
and "Destroy Fixed Emplacements" In Progress, "Press F12 To Close
Objectives Display"; the four SRM emplacements killed, the second reads
Completed while the box is open; F12 again hides it). Break: without the
fill no line; without 0x00446930's blank score the columns differ.
`build/mw3 --mission c1m1 --objectives N` (ours) opens the box from the
N-th frame, for a screenshot.

## 24b. The player's death and a network kill's scores (2026-11-01)

- **The player's death** (0x0041b263..0x0041b30c, inside Die 0x0041b0d0,
  on the view's mech's HUD): the zoom window hidden unless locked
  (+0x1508 vt+0x64 unless +0x167c); still shown (locked open, +0x1514
  bit 4 clear), its lock let go (0x00442e00, which hides it); then the
  HUD off (vt+0x04 with 0: the radar hidden). `DamageCalls::player_died_hud`.
- **A weapon destroyed on the player's mech** (0x004b3020's 0x004e6dc0 with
  1.0, 0, 0, -1.0, 0): its NAMESOUND and `mech_part_destroy` voices through
  0x00571cc0 (no place, volume 1, no fade; both QUEUE radio lines, so they
  wait their turn); their names into the message panel only with
  [0x0060b878], which nothing sets. `WeaponCalls::destroyed_sounds`.
- **A kill's score in a network game** (0x00470170's end): the host's count
  sent (0x14), then the view's mech's objectives box filled again while it
  is shown (+0x17970 bit 4 clear). The exe reads the box through a null HUD
  when the player's mech is not the view's target; the port does nothing
  then (FIX (bug), fixes.md). `NetCalls::hud_scores`.
- The HUD hooks' `multiplayer_game` (0x00479480) answers the pilot's
  network game; it answered 0, so the box's network header and the MFD's
  network title were never taken.

Test: `mission_c1m1_player_death_hud_and_voices_headless` (the window
locked open, a weapon with a NAMESOUND destroyed: its line queued; the head
lost: the HUD off, the window closed and its lock let go; none of the three
on the census), `net_chat_panel_c1mp1_headless` (the kill's score refills
the open box, "Network Game Scores" first, and leaves a closed one empty).
Breaks caught: the voices left a stand-in, the death's HUD call unwired,
the scores' refill unwired.

## 25. The mission maps (hud-plan items 7-8; mech_map.cpp, `mission_map`)

maps.zrd (reader.zbd) is read once at the app's start (0x00480837 ->
0x004583b0, the flag [0x00605ad8]): its `maps` list, one map a block (0xdc
bytes, vtable 0x005c2308, a zTwoD Image; 0x004580c0): `name` (+0xc0),
`bitmap` (+0xbc), `offset` (+0xc4, a vector, default zero), `mpp` (+0xd0,
metres a pixel) and its inverse (+0xd4, 0 for 0). The list is [0x00605acc]
(count [0x00605ad0]); 1.2 ships 50 maps, each mission's and its `_small`.
No image is loaded yet.

A mission's briefing (0x00405160) sets zTwoD's image path to
`..\data\common\images\maps`, takes its section's `MAP` (the mission's
name, c1m1 and so on) and 0x004582a0: every map whose name holds it loads
its bitmap (0x00458660 -> 0x00458300, a calloc'd rect over the image),
then the first loaded map whose name holds "large" ([0x005e3eb4]), else
the first loaded one, goes into the missions object's +0xbc (0x0040a0e0;
[0x0060998c]). No map's name holds "large", so it is the mission's own
(c1m1, not c1m1_small). 0x00491c80 answers +0xbc, making an empty map once
at +0xb8 when there is none. The mission's cleanup releases every map's
image (0x004582c0, 0x00458390); the app's exit deletes them (0x004584e0).

Shift+R (0x0043dc10) turns the radar's map mode on when 0x0043da00 finds a
map (0x00458590: the first loaded map whose name holds "large" for the big
radar, "small" for the small one, else the first loaded) and sets it as
[0x0060998c]; with the map, the radar draws it under the contacts and puts
the mech at its pixel (0x004586e0: -(x, z) from the offset times 1/mpp,
truncated; false outside 0..640, the release's report empty). A debug start
(our addition) has no briefing: the program picks the map by the mission's
name at the launch, as the briefing would.

In the program: `GameEngine::maps_init` / `mech_data_shutdown` (the
startup's read, the exit's free), the mission hooks' `fn_4582c0` (the
cleanup), `briefing_pick_map` (the briefing's and the debug start's), and
`ProgramHudHooks`' map hooks (the missions object's +0xbc by the engine's
handles). Test: mission_c1m1_maps_and_the_radar_map_mode_headless: 50 maps
read, c1m1 picked, the mech at (600, 162) on it, Shift+R over c1m1_small
and off again (the images 640 x 640 stand-ins: the headless host loads
none). The live program draws c1m1_small in the radar's box with the mech's
circle on it.

## 26. The power down and up (0x0043bb20 / 0x0043bb50; ported 2026-10-29)

When the player's mech powers down (Mech::finish_power_down 0x0041cac0),
0x0043bb20:
- keeps whether the HUD was on (+0x04 == 1);
- turns it off (vt+0x04(0), 0x0043baf0, which also hides the radar);
- hides the reticle (+0x144c vt+0x64).
When it powers up again (Mech::power), 0x0043bb50 gives the HUD back the
kept state (vt+0x04, the radar with it) and shows the reticle for good
(vt+0x60(-1)). Until now neither ran: a census stand-in. So the cockpit HUD
stayed on through a power down.
- Port: `Hud::power_down` and `Hud::power_up`, through
  `HeatCalls::screen_power_down` and `screen_power_up`.
- test_hud.py runs both on the built HUD of each case (none, the down, or
  the down then the up with a random kept byte) and compares the dump after.
  Breaks caught: the reticle left shown, the kept state ignored.
- `mission_c1m1_power_down_turns_the_hud_off_and_back_headless`: on at the
  start; off with the reticle hidden in the frame the mech powers down; on
  with the reticle in the frame after the power up.


## 27. The globals the HUD reads (the peek sweep, 2026-10-30)

The HUD's hooks (`HudHooks`) read some exe globals and mech fields through
stand-ins: `unported("peek32", {addr})` and the like. The program's
`ProgramHudHooks` overrode most of them; the rest answered 0. Its
`unported` was the base's, which reports nothing, so the census never
listed one. It now reports, and `tools/analysis/peeks.py` lists every read
of a global with its writers and whether its stand-in is overridden. Wired
to the ported fields:

- **The compass and the radar on a scaled map.** 0x0043f590 turns the
  compass's heading by π, and 0x0043f130 reverses the radar cone's
  direction (while the map is up), when the missions' map (0x00491c80:
  their +0xbc, the briefing's MAP, `mission_map()`) has its +0xd0 above 0.
  That field is the map's `mpp`, so every real map does it. The port's 0
  left the compass half a turn from the original's on every campaign
  mission, and the cone pointing the other way.
- **The MFD's and TARGET's last modes** ([0x00605988], [0x0060598c]): kept
  for the process, so a mission's MFD and TARGET start in the last one's
  mode. The port started them at their defaults each time.
- **The MFD's title** (message 0xdbe, "DISHONORABLE"): in single player it
  is drawn only while the cheat flag [0x00609994] is set (0x00442540). In
  multiplayer, "You are alone." while [0x00605f88] is clear: no remote
  mech in the game ([0x00605f84]'s count, `net_game().mechs`; wired
  2026-11-01). [0x00609994] is one byte in the exe. The cheat setters
  (0x0040ced0..) set it, and the mission's check (0x00491d20) sets or
  clears it at the start. The port kept two copies, so a cheat turned on
  mid-mission was missed by the debriefing (the mission still counted).
  `options().cheated` is now the one read, and `Missions::cheats` mirrors
  it for the differential's layout.
- **The zoom window's shape** (0x0041df10: the mech's tech, 0x00484720 on
  its data's roster entry): round for a Clan mech (tech 2).
- **A powered-down mech's HUD** (vt+0xa0, 0x0041cc10): off at its set-up
  and its frame skipped.
- **The radar's amber** (+0xbc bit 2) at the HUD's set-up.

Left as they were, with their reasons: the pilot's multiplayer game
(0x00479480, 0: no multiplayer game is ported) and the HUD frame's debug
numbers (0x0043c820's, developer only, in the census's known table).

Test `mission_c1m1_hud_reads_the_ported_globals_headless`. With the fixes
out, every check fails: the tape and the cone unchanged, both modes 0, the
mid-mission cheat not seen, tech 0, the powered-down HUD on.
