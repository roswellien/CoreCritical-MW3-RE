# The Battlesport game editor and the stealth object

MW3 1.2 still carries an in-game editor titled for a game called Battlesport:
a mouse menu headed "BATTLESPORT GAME EDITOR" over a mission,
with the view on the stealth object, an AI net editor and a vehicle editor.
Nothing in the release reaches it: it opens on debug command 0x9a, which no
key is bound to (0x0040db60 registers only 0x61, 0x70, 0x7e and 0x6c to the
debug handler 0x0040dba0). Mapped 2026-10-31 (dev 2); the entry, exit,
panel, main menu and the view on the stealth ported 2026-11-01 (S4, below;
the AI net and vehicle editors behind its rows come after v1.0). The
port reaches it with Shift+F12 under `--debug-camera` (or `debugcamera=1`
in mw3.cfg) only, as Owen asked: "exactly what they did", a developer's
tool. (F11 is the lancemate command screen in the original's default keys,
Shift+F11 and Ctrl+F11 the awaiting orders; Shift+F12 is bound to nothing,
on every keyboard. Agreed with the lead 2026-11-01.)

Why the name is open. These docs used to say Zipper made Battlesport before
MW3; nothing supports that. The public credits give BattleSport (1996) to
Cyclone Studios, with its ports by Unexpected Development, and don't list it
among Zipper's games. The exe has only the title string, in MW3 and in Pirate's
Moon, so it shows neither that this is the same game nor how the name got here.

## The entry and the exit

**Command 0x9a** (0x0040dba0's switch, decoded from its byte table at
0x0040dea4 and jump table 0x0040de34) calls **0x0040e430**:

- [0x005fc314] = 1 (the editor is up);
- a command map pushed (0x0051d3a0 with null: a copy of the current one,
  so every game key still works) with two bindings (0x0051d2c0): Space
  (key 0x39) and mouse button 1 -> command 0x9b (pick), key 0x439 and mouse
  button 2 -> command 0x9c (a trace, empty in the release: 0x005ac5c0);
- the view on the stealth object: [0x00609a2c]'s vt+0x108 (0x004acd90 ->
  0x0040adf0 with the vehicles' current camera, [0x0060b140]);
- the debug lines hidden when they were up ([0x005f4dac] = 0x0040f880, the
  state of [0x005fc33c]; when set, 0x0040f8b0 hides the two debug-line
  layers 0x005fc338 and 0x005fc838). The release never turns them on (hud.md
  section 20), so this does nothing there; corrected 2026-11-01: an earlier
  reading here took it for the camera mode;
- the editor panel's layer on ([0x005f4db0]'s vt+0x04(1));
- 0x0040e7d0: the main menu (below).

**0x0040e7f0** ("EXIT GAME EDIT", its argument unread): the command map
popped (0x0051d490), the panel's layer off (vt+0x04(0)), the debug lines
back when they were up (0x0040f890), [0x005fc314] = 0. It leaves the view
on the stealth object: in the original the camera stays where the editor
left it, and the pilot's mech goes on without its view.

## The panel and its menus

The panel ([0x005f4db0], vtable 0x005bc510, built by 0x0040e270(5, 0x5f),
freed by 0x0040e3b0) holds 25 text rows (0x4b0 bytes each, row object
vtable 0x005bc590, 12 pixels apart from y 0x5f).

A menu is data: `{int count; {char text[20]; void (*handler)(arg);} x count}`.
**0x0040e7b0**(ECX mode, EDX table, the title, "") sets [0x005fc30c] (mode)
and [0x005fc318] (table) and fills the rows (**0x0040e6a0**: every row
cleared with "" at 0x005f4c90, then each entry's text, the panel's width
the widest row's, its height count x 12).

**The frame, 0x0040e4b0** (from the HUD's draw; while [0x005fc314]): the
row under the mouse ((y - 0x5f) / 12, rows 0..24, then the row's vt+0x5c
hit test) lit green (0xff00 in both colours, +0x24c/+0x250, +0x474 to
render again), the one before back in the panel's colours; the title row
is never lit. Space or the left button (command 0x9b's state, 0x0051d300
bit 4: pressed) on a row calls its entry's handler with the mouse's y: a
shown row its own (the input's state cleared first, 0x0051b1c0), the title
the menu's first (none in the main menu); the panel's layer updated
(0x0059fd20 with [0x0072c52c]); in modes 1..3 the net editor's 0.1 s step
(0x00403140); 0x9c held calls the empty trace; then, up or not, the net
editor's frame 0x00404290 (nothing without a net editor).

### The main menu (0x005e0d30, 8 rows; 0x0040e7d0)

| Row | Text | Handler | What it does |
|---|---|---|---|
| 0 | PICK A COMMAND | none | the header |
| 1 | EDIT AI NETS | 0x00402d60 | the AI net editor (below) |
| 2 | EDIT VEHICLES | 0x004040c0 -> 0x004b86e0 | the vehicle editor: a 0x988-byte object (0x004b80f0, vtables 0x005c9898, 0x005c97c0), [0x0060b310]; also debug command 0x9d's |
| 3 | EDIT WEAPONS | 0x004bf620 | the shared empty function: nothing in MW3 |
| 4..6 | (blank) | none | |
| 7 | EXIT GAME EDIT | 0x0040e7f0 | the exit above |

### The AI net editor (0x005e0f18, 13 rows; installed by 0x00403690)

0x00402d60 first picks a point or net node under the mouse (0x0040e120 ->
0x004d55c0, the camera's pick vt+0xf8, 0x0058b1c0), then installs the menu
"AI NET EDITOR" in mode 1. It is the editing side of `CZAiNetEdit`
(0x004bd360, 0x004bd3d0; docs/ai.md: only the builder side is ported).

| Row | Text | Handler |
|---|---|---|
| 0 | Pick CMD or Point | 0x004bf620 (empty) |
| 1 | GOTO Net | 0x004036c0 |
| 2 | Change Net Params | 0x004036f0 |
| 3 | Node Params | 0x00403710 |
| 4 | Add veh to node | 0x00403820 |
| 5 | Move Node | 0x00403ac0 |
| 6 | Delete Node | 0x00403ae0 |
| 7 | Delete Net | 0x004bf620 (empty) |
| 8 | Save Changes | 0x00402e90 |
| 9 | Tree View | 0x00402c70 |
| 10, 11 | (blank) | none |
| 12 | Exit Net Editor | 0x00402ba0 |

"Save Changes" writes the net files; the port will write them to the write
dir, never into the install (FIX (A), agreed 2026-10-31).

## The stealth object

Made at every mission load ("Creating Stealth", **0x004a4de0**, [0x00609a2c],
vtable 0x005c8460, 0x104 bytes; vehicle.zrd's `stealth`: no model, sounds
or weapons, health 10000, `mode` basic, rates 40 / 65, turns 2.5 / 3.5);
renamed "stealth" by the mission load (0x005a84c0); deleted by the unload
(0x00490480, its destructor 0x004a4fc0). Its +0x09 is 0, so the entity walk
never runs it; its frame runs from the player's tick while the view is on
it. Ported in S1 (mech3/veh_stealth; vehicles.md, The stealth object):

- **0x004a4de0**: the basic vehicle's constructor (0x004ac530) over its
  controls, the HUD markers (0x004ac3d0), a new node named "Stealth" in the
  world, the entity's node and list, the origin, the node out of the
  segment queries (0x004eb6d0) and flag 8 off (0x004eb620), the controls'
  motion mode (BASIC) and state zeroed, the overlay, +0x09 cleared;
  vehicle.zrd's `stealth` (its common_mode through 0x0049ec00, its motion
  block through 0x0049ec90), the points (0x0049f660); placed where aiv.zrd's
  `stealth` record says when the mission has one (0x004e0680, 0x004a3630).
- **The overlay** (0x004a55c0, 0x108 bytes, vtable 0x005c86e8): a zTwoD
  layer, active from the start, holding the image "ret_off" (0x004a5850:
  centred at the origin, shown for good); its frame (0x004a56b0) draws a
  readout of the movement mode (BASIC, TRACK, HOVER, AMPHIB, SUB) through
  [0x005fc848]; turning it off calls 0x0040fb50(0). The readout and that
  call come with the view, S4.

**Its frame** (vt+0x34, **0x004a5050**; ported in S4): the controls' frame
(below), the overlay's reticle (**0x004a5a00**: the image at
(320 - trunc(-320 mouse_x), 240 - trunc(-240 mouse_y)) on the 640x480
screen, then its pick 0x004a58d0, the world point under it through the
view's camera, which only the editors read), the move 0x004a1240 (its
motion mode's step: BASIC) and the node's matrix 0x005a85c0. Unlike a basic
vehicle's frame (0x004a1040) it asks nothing of a path; its vt+0x100 is
empty and its vt+0x104 (**0x004a50d0**: the view letting go of it) turns the
overlay off.

**Its controls** (0x004a51b0 over the base 0x004ac750, 0xa1c bytes;
vtables 0x005c86b0, IControls 0x005c85f8; corrected 2026-11-01: an earlier
reading here took the basic vehicle's own controls, 0x004ae270, for them)
are the base's but for vt+0x1c (0x004d72a0: never destroyed), vt+0x20
(0x004a5270: 1) and the frame, IControls vt+0x0c (0x004a5ae0 ->
**0x004a5280**): each frame the player's own movement commands (0x0051d300:
THROTTLE_UP/DN 0x18/0x19, TURN_LEFT/RIGHT 2/3, PITCH_UP/DN 4/5,
CENTER_TORSO 6) and the mouse (0x0051ba60), clamped to -1..1, smoothed by
0x004abcf0(x, 0.5, 1) into the throttle and turn (IControls +0x20, +0x28)
and two more axes (+0x9c0, +0x9bc). So the stealth is a camera flown with
the original's own keys while the view is on it. The basic vehicle's own
controls (0x004ae270, 0x004ae850, the path modes 0..4 of 0x00401050) are a
driverless vehicle's, not the stealth's; no shipped mission makes one, so
they wait (vehicles.md, The basic vehicle's own controls).

**BASIC** (0x004a1d90, its ground 0x004a1f00), the stealth's movement
mode: the steering (0x004a20d0: the turn into the heading +0x330, wrapped
at 2 pi) or, in camera mode 3 (straight down), north-up panning (0x004a2000:
heading 0, the throttle on the sideways speed, stopped dead before
reversing); the speed step 0x004a2b10 (ported); the move on the horizontal
plane; then the four platform points' ground (0x004a2190), the highest plus
+0x88 of the motion block, pitch and roll 0: a level sled above the ground.

## The port, in sittings (agreed 2026-10-31; scope set by Owen 2026-11-01)

About 140 unported functions for S1..S6 and about 200 (5,500 lines of
pseudocode) for the vehicle editor.

**For v1.0: S1 to S4**, the stealth object and the view on it.

1. **S1** The stealth vehicle 0x004a4de0 with 0x004a55c0, 0x004a5850,
   0x005a84c0, its controls 0x004a51b0 (their frame 0x004a5280 a stub until
   S3), the destructor 0x004a4fc0; made at every mission load, as the
   original's "Creating Stealth" does, deleted at the unload. Done
   2026-11-01: test_veh_stealth.py. Tests:
   construct and read against the original; the program makes it in c1m1;
   the full suites and the campaign runs as the proof that play does not
   change, with the mission load time before and after.
2. **S2** BASIC: 0x004a1d90, 0x004a1f00, 0x004a20d0, 0x004a2000,
   0x004a2190; differential tests on random states. Done 2026-11-01
   (veh_move.cpp, test_veh_basicmode.py; vehicles.md, The stealth object).
3. **S3** The stealth's controls' frame 0x004a5280 (the player's
   movement commands and the mouse) and 0x004abcf0; differential tests.
   Done 2026-11-01 (veh_stealth: `stealth_controls_frame`, `input_curve`;
   test_veh_stealth.py's frames).
   (The basic vehicle's own controls and their path modes, which an
   earlier reading took for the stealth's, wait: vehicles.md.)
4. **S4** The entry 0x0040e430 and the exit 0x0040e7f0 for the camera: its
   command map (0x9b, 0x9c), the camera on the stealth and the camera
   mode's save and restore (0x0040f880, 0x0040f890, 0x0040f8b0); the key
   under `--debug-camera` / `debugcamera=1` only (Shift+F12: the default
   keymap binds F11), no keys of ours
   besides, the mission running on as in the original. The panel and its
   main menu (0x0040e270, 0x0040e6a0, 0x0040e7b0, 0x0040e7d0, the frame
   0x0040e4b0) too if the row object draws with primitives already ported:
   EDIT AI NETS and EDIT VEHICLES then say "not ported yet" in the log only,
   EXIT works; if the panel needs new HUD primitives it waits, and the key
   toggles the camera alone. Test: the key over c1m1 puts the view on the
   stealth and gives it back; without the flag, nothing.
   Done 2026-11-01 (mech3/game_editor; the stealth's frame 0x004a5050 and
   reticle 0x004a5a00 in veh_stealth, test_veh_stealth.py; the view's
   target any vehicle, as the original's: player_view, view.md). Tests:
   test_editor.cpp (the entry's and exit's calls in order, the main menu's
   rows, the hovered row lit, a press on each kind of row) and
   mission_c1m1_game_editor_on_shift_f12_headless. In that test, with
   THROTTLE_UP held for a second, the stealth flies 20.6 m and the pilot's
   mech under it moves 0.30 m, as it does with the key and no editor (0.00 m
   with no key): **the mech drives too**, as in the original. The player's
   tick (0x004a1160) runs the pilot's vehicle every tick and the stealth's
   frame besides while the view is on it, and the editor's command map is
   a copy of the game's, so the same movement commands reach both. Kept.
   **Ours (debug only):** the exit puts the view back on the pilot's mech
   (its 0x0041ce30), as the mech swap does; the original's leaves it on the
   stealth (above). The panel's rows draw through zTwoD's FontText with
   the font "mech_string" (the HUD's own). The pick under the reticle
   (0x004a58d0) and the readout of the movement mode (0x004a56b0) are not
   ported: the first is read only by the editors (S5 on), the second writes
   into the debug lines, which the release never shows.

**After Pirate's Moon is finished (Owen, 2026-11-05):** MW3's version on `main`,
PM's differences on `pm` (its zainetedit.cpp changed: 35 changed functions, 85
new):

5. **S5, S6** The AI net editor: 0x00402d60, 0x0040e120, 0x00403690, the
   handlers above, the tree view; Save Changes to the write dir (FIX (A)),
   saying on screen where it saved; typed fields (names, numbers) bounded
   like any input.
6. **S7** The vehicle editor mapped (0x004b80f0's object, its screens and
   what each control edits and writes, to which file; every save to the
   write dir), then its own sittings (likely 3 to 5), named from that map.
7. **S8** The "GameZ Interpreter" console 0x5960a0, with its log callback
   0x596130, its line runner 0x596190 and its toggle 0x4810d0 (`interp.md`,
   Hidden debug console). The exe's only way in, action 0xa3, is likely
   unreachable, so the key is ours, under a debug flag as the camera's is.
