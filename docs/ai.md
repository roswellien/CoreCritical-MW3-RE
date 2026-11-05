# The AI

Mech3.exe 1.2, 2026-10-20. The map of the AI code in the game half
(0x4b4880..0x4eada0), the data files it reads, how one AI tick runs, and a
porting order. The data side, the nets at run time, the AI controls
with the brain's data, the valves at run time and targeting are ported
and emulator-checked (sections 5, 8, 12, 13, 14); the brain's decisions
and the behaviours are not.

Names are ours unless an assert string gives them (`CZAiNetEdit`,
`CFileDialogNet`, the five source paths). "Likely" marks a reading of what the
code does that no string confirms.

## 1. What is where

| Range | Funcs | File | What |
|---|---:|---|---|
| 0x4b4880-0x4b5580 | ~45 | weapon.cpp (tail) | weapon helpers the AI uses: the per-type fire timers 0x4b4bd0..0x4b4c90 (WeaponType +0xc4), the radar list 0x4b49c0; weapons.md |
| 0x4b5580-0x4b7d80 | ~80 | (unnamed, after weapon.cpp) | **the AI's weapon firing**: the per-group fire state machine 0x4b5b80 (states 1..7, game time 0x60991c, called from the brain 0x4e9fb0), its fire-plan objects (vtables 0x5c9458, 0x5c9460: 0x4b6390, 0x4b6790), aim and lead helpers 0x4b70b0..0x4b77e0 |
| 0x4b7d80-0x4bcb70 | ~95 | (unnamed) | **the AI tester dialog**, a developer tool: an MFC CDialog (0x4b80f0, 0x988 bytes, resource 0xa5, sub-dialogs 0xa6..0xb7) opened by 0x4b86e0 from the debug menu (0x404090, 0x40db60). Strings "Direct / Zig Zag / Circle / Jump / Random Move" (movement modes), "player / nearest enemy / ionet" (targets). Not to be ported (FIX (A)) |
| 0x4bcba0-0x4bf1b0 | 34 | ZAiNetEdit.cpp | `CZAiNetEdit` (0x4bcba0 ctor, 0x44 bytes, vtables 0x5ca420/0x5ca388): **a net at run time**, built from its file (section 3): one node object (0x4c6d30, 0xbc) per record; edits, save (0x4bef80 -> 0x4dc2b0), regions (0x4bf000, 0x4bf1b0 "unable to create region") |
| 0x4bf3f0-0x4c1160 | 60 | (unnamed) | `CFileDialogNet` (0x4bf750, the editor's file box, FIX (A)); net classes over the net base 0x4d5120 (vtable 0x5ca6d8: 0x4bf910, 0x4bfbb0, 0x4bfe40; 0x4c0090, 0x20 bytes, five behaviour vtables at once); small move classes (0x5ca738, 0x5ca748, 0x5cab50..0x5cac40) |
| 0x4c11f0-0x4c3ed0 | 67 | ZAiNetMechInOut.cpp | **inside/outside nets**: closed node loops (the reg_*.zrd regions) and which vehicles are inside them ("zero length segment in an inside/outside net" 0x4c11f0); the mission's set-up 0x4c3b80 (from the mission start 0x490ab0); "Unknown class type %d for node %s" 0x4c3ed0 |
| 0x4c3f70-0x4d2660 | 385 | (unnamed) | the bulk of the AI: the net record class (0x4c5fb0 family) and run-time node (0x4c6d30); the region registry (0x4c5c00 find-or-load by name); net classes 0x4c5640, 0x4c5b00; **the behaviour (order) classes** (section 2); the pilot brain object 0x4cf330 (0x2b0 bytes + IControls); the backup and deploy node states (0x4cfad0, 0x4d00a0; "mcbdeploy" the valve deploy holds on, 17.4a); the health_below list 0x4d1f60/0x4d1f80 |
| 0x4d26f0 | 1 | ZAiTarget.cpp | the target check (asserts on a target's collide and weapons callbacks) |
| 0x4d27d0-0x4da8f0 | 229 | (unnamed) | **targets and targeting**: `CZAiTarget` (0x4d2960, 0xf4; 0x4d3080 derived), made at mission start from targets.zrd (0x4d2e20, 0x4d31d0); **the valves** (0x4d3b20..0x4d3e50, registry 0x60b500); `AiTarget`, the vehicle's targetable base (0x4d56e0, vtable 0x5cced8, 0xd4: height +0x68, team masks +0xc0..+0xc8) and 0x4d7f80 (0xd4); the contact pick 0x4d6b90; the sides 0x4d5af0 / 0x4d5b10; the AI controls (0x4d8670, 0x4d8750 = 0x320 bytes); the AI settings block AiParams (0x4d8d30..0x4d9b70); region lookups for the map (0x4d92f0 reg_inside_, 0x4d9360 reg_outside_); the net file cache and its STL (0x4da060..0x4da840) |
| 0x4da900-0x4db8d0 | 11 | ZRdrAiNet.cpp | **net and region files**: header 0x4da900, node records 0x4db850..0x4db1b0, the editor's writers 0x4dad10 / 0x4db8d0 |
| 0x4dc150-0x4de980 | 45 | (unnamed) | the net file classes' methods (record lookup 0x4de730, 0x4de830) and their std::map code |
| 0x4de9c0-0x4e08c0 | 43 | ZRdrAiv.cpp | the reader-file base; **aiv.zrd** (0x4df730) and **targets.zrd** (0x4e08c0); traps.zrd (none ship) |
| 0x4e0c20-0x4e2330 | 25 | (unnamed) | the aiv record cache's std::map (editor only); **valves.zrd** file (0x4e1cb0) and its load at mission start (0x4e2120 from 0x490ab0) |
| 0x4e2330-0x4e44b0 | ~55 | ZRdrValve.cpp (+0x4e2330) | the valve file's reader 0x4e2330 (sound, sound_once, animation, scan_*, canleave, shutdown, mission complete/abort/reset, destroy, setteam, attack_strategy, update, delayupdate, any/all zero/nonzero, namelist, teleport) and the action classes (0x4e2ff0..0x4e4430, vtables 0x5cd458..0x5cd598) |
| 0x4e4d20-0x4e7090 | ~70 | (unnamed) | valve conditions (0x4e4d20.. all_nonzero etc.), valve file class 0x4e5290; a mech hook from the Mech ctor (0x4e61e0) and Mech::Update (0x4e6480); subtitles ("MINDELAY SUBTITLE" 0x4e6aa0) |
| 0x4e7090-0x4eaab0 | ~90 | (unnamed) | **the brain**: vehicle AI controls 0x4e7090/0x4e71c0; lancemate radio 0x4e7c40 ("call_help", "confirm_order", "engaging", "low_ammo", ... built as `<tag>_<event>` 0x4e7dc0); orders from the player 0x4ea000..0x4ea230; the AI update 0x4e9710 |

The zaitarget/zrdrainet unnamed range is big because several small files sit
in it; where one ends is not known.

## 2. Classes

Sizes from `operator new` at the call sites; vtable slot counts are where the
next vtable begins, so they may run long.

| Class (ours) | Ctor | Vtable | Size | Notes |
|---|---|---|---|---|
| RdrFile (CZRdrFile) | 0x4de9c0 | 0x5cd418 | 0x24 | slots: dtor, +4 returns 1, +8 unload 0x4deb80, +0xc parse (empty) |
| net file chain | 0x4da740, inline in 0x4da570 | 0x5cd400, 0x5cd3e0, 0x5cd3c0, 0x5cd3a0 | 0x118 | +0x2c the shared record, +0xe4 the editor's cache, +0xf4 the header; slots +0x14 write node, +0x18 read node, +0x1c write header |
| AivFile | 0x4df640 | 0x5cd428 | 0x28 | one, at 0x60b5e0 |
| RecordFile (targets, traps) | 0x4e0740 | 0x5cd440 | 0x24 | 0x60b608, 0x60b630 |
| valves file | 0x4e5290 | 0x5cd5a8/0x5cd5c8 | | valves.zrd |
| NetNodeRec | 0x4c5fb0 / 0x4c6ca0 | 0x5cae50 / 0x5cae54 | 0xb4 | defaults 0x5e8af8 |
| net node (run time) | 0x4c6d30, 0x4c6da0 | 0x5cae60, 0x5cae58, 0x5caef8 | 0xbc | a record plus +0xb4.. |
| net base | 0x4d5120 | 0x5ccda8 | 0x5c | over a network object (0x58aec0, vtable 0x5cefd0): nets are synced in multiplayer (likely) |
| CZAiNetEdit | 0x4bcba0 | 0x5ca420 (+0x5ca388) | 0x44 | |
| other nets | 0x4bf910 (0x1c), 0x4c5640 (0x1c), 0x4c5b00 | 0x5ca6d8, 0x5cac90, 0x5cae00 | | over 0x4d5120 |
| behaviour base | 0x4c8600 | 0x5cb180 (40 slots) | 0x0c | +4 bit 0, +8 0 |
| behaviours, 40-43 slots | 0x4c8750, 0x4ca370, 0x4caca0, 0x4cad70, 0x4cbe10, 0x4d08d0, 0x4d1c80 | 0x5cb278, 0x5cb7e8, 0x5cb948, 0x5cba20, 0x5cbe18, 0x5cc970, 0x5ccaa8 | | the orders a pilot runs (likely: move along a net, attack, follow, ...) |
| behaviour step base | 0x4c9d00 | 0x5cb3d0 (19) | | over 0x4c8600, +0xc = 0x603fa0 |
| behaviour steps | 0x4c9eb0, 0x4c9f80, 0x4ca0a0, 0x4ca1c0, 0x4cb2a0, 0x4cb950 (+0x4cbcf0, 0x4cbd40), 0x4cce30, 0x4cd4a0, 0x4cd6e0, 0x4cd770, 0x4cda50, 0x4cdc20 | 0x5cb488 ... 0x5cc440 | 0x10-0x20 | |
| behaviour factories | 0x4c8660 base; one per behaviour (0x4c88c0, 0x4c9ef0, 0x4c9fc0, ...) | 0x5cb238 (2 slots) + 2-slot / 7-slot vtables | | +4 the argument |
| AiParams | 0x4d98c0 | 0x5cd348 | 0xdc | section 4 |
| AivRecord | 0x4e0490 | 0x5cd438 | 0xe4 | holds a reference on aiv.zrd |
| pilot brain | 0x4cf330 | 0x5cc698 (+ IControls 0x5bd500 at +0x2b0) | 0x2b0 + | starts with an AiParams copy (0x4d9bf0, vtable 0x5cd360) |
| AI mech controls | 0x4d8670 / 0x4d8750 | 0x5cd1e0 / 0x5cd318 | 0x320 | MechControls + the brain at +0x5c (controls.md) |
| AI vehicle controls | 0x4e7090 / 0x4e71c0 | 0x5cd738 / 0x5cd880 | | the brain at +0xa04 |
| AiTarget | 0x4d56e0 | 0x5cced8 | 0xd4 | base of Vehicle (structs.md) |
| CZAiTarget | 0x4d2960 / 0x4d3080 | 0x5ccb90 / 0x5ccc80 | 0xf4 | a targets.zrd target |
| Valve | 0x4d3b20 | - | 0x0c | name, value holder (vtable 0x5cd558), reactions |
| valve actions | 0x4e2ff0..0x4e3f50 | 0x5cd458..0x5cd528 | 8-0x1c | one class per valves.zrd action |
| valve conditions | 0x4e4d20, 0x4e4df0, 0x4e4ec0, 0x4e4f90 | 0x5cd568..0x5cd598 | 0x14 | all/any zero/nonzero over 0x4e40e0 |
| AI tester dialog | 0x4b80f0 | 0x5c97b8 | 0x988 | MFC, developer tool |

## 3. Data files

Each mission's `zbd\<world>\reader<mission>.zbd` (opened with `reader.zbd` by
0x490630; the search path also names `..\data\<world>\zrdr\aipath` for loose
files, which do not ship) holds, across the 48 archives:

| File | Count | Read by | What |
|---|---:|---|---|
| net_NN.zrd | 308 | ZRdrAiNet (version 106) | AI paths: numbered nodes with links |
| reg_*.zrd | 393 | the same reader (version 108; 3 are 106) | regions: node loops; reg_inside_* / reg_outside_* are the mission's boundary on the map (0x43d920) |
| aiv.zrd | 48 | ZRdrAiv 0x4df730 | the vehicles: one record per entity name |
| valves.zrd | 46 | ZRdrValve 0x4e2330 | the mission's triggers |
| targets.zrd | 27 | 0x4e08c0 | scored objects |
| waynet*, mpinfo.zrd | 2 each | mech_waynet.cpp | multiplayer |

mech3ax does not read these files (its `reader` crate reads any .zrd tree,
nothing AI-specific); the exe is the only source.

### Net and region files

```
( ( "version" (106 ["mech3"])  "name" ("net_01")  ["netparams" ("pos" (x y z))]
    "node_00" (12 (x y z) (links...) props...)        a "12" node (kind 0)
    "node_01" ((links...) props...)                   a list-first node (kind 1)
    ... ) )
```

The header (0x4da900): version 105-108 accepted (105 with "mech3" reads as
106), others reported; 105 files must be named net_NN after their number; a
name "mcbwaynet" is refused. Records start at the first `node_` key. Links
are the list's non-negative values. Node properties (0x4db1b0, each a list
after its key, values copied as raw 32-bit words): speed (on rate), canleave,
wait_interval, wait_time, valve (on name [value]), name (localized) and
auxname (defaults to the name as written), jump, scan_aim, scan_time,
scan_hit, hidden, fixed, autonext, visible, deploy (valve byte valve byte
short), mcbwait (valve byte time), arrangenodes, sound, backup, gate, shunt,
valveunion ((on name [count]) ...), valve_assign (name value). The shipped
files use speed, canleave, wait_interval, wait_time, valve, name, auxname,
jump, scan_*, deploy, arrangenodes, gate, valveunion, valve_assign.

Records are parsed on demand into one shared NetNodeRec (0x4de730). A file
object per name lives for the game in the map at 0x60b5a8 (0x4da570, named
net_NN when asked by number); its tree is loaded on the first reference and
freed on the last.

### aiv.zrd

```
"name" (kind (x y z) yaw [team] tag value ...)
```

`team` is given or comes from the name (0x4d5b10: player 1, stealth 0,
others 2). Tags (0x4df730, all lists of raw words): params (9), p2 (8), p6
(4), p8 (17), p9 (5), p10 (2), p11 (1), p12 (mech name, 2 flags), p13 (1),
pilot (localized name, can_configure), sound_tag, skin, lancemate
(0/1/2 or on/off/none/default), vlist (orders), mech3. The older p3, p4
(an inline order), p5 and p7 are read too; no shipped file has them, and p8
with any of them is reported. Where each lands in AiParams:

| Offset | From | Notes |
|---|---|---|
| +0x04 | kind | the record's first item (0-26 in the shipped files) |
| +0x08..+0x10 | (x y z) | |
| +0x14 | yaw | degrees in the file, radians after finish |
| +0x18 | team | |
| +0x1c, +0x3c, +0x20..+0x38 | params 1, 2, 3..9 | +0x1c (params 1) is compared squared with a contact's distance: sight range (likely); +0x20 (params 3) the attack range (likely); +0x1c, +0x20, +0x24, +0x74 are kept squared |
| +0x40, +0xcc bits 0-2, +0x44..+0x50 | p2 | +0x4c x the GameIntensity value 0x608e54 sets a delay (0x4e9710) |
| +0x54..+0x60 | p6 | |
| +0x64..+0xa4 | p8 1-4, 6-8 at +0x74..+0x7c, 5 at +0x80, 9 at +0x84, 10-17 at +0x88..+0xa4 | +0x64/+0x68: a random interval's bounds (0x4e9710) |
| +0xa8..+0xb0, +0xcc bit 3, +0xb4 | p9 | |
| +0xb8, +0xbc / +0xc0 | p10 / p11 | |
| +0xc4, +0xcc bits 4-5 | p12 | the mech index (0x4845f0), -1 for none |
| +0xcc bit 6 / bit 7 | p13 / can_configure | |
| +0xc8 | skin | |
| +0xcd bits 0-1 | lancemate | |
| +0xd0 | vlist | a list of 0x20-byte triggers (17.5): three floats, two squared ranges, a region by name (0x4c5c00), flags from the 0/1 items, a valve |
| +0xd4 / +0xd8 | pilot / sound_tag | sound_tag defaults to dom / alan / epona for exactly "Dominic_Paine", "Alan_Mattila", "Epona_Rhi" |

The defaults (0x5e8dc0) are stored squared; the reader takes them back to
plain units first (0x4d9b20) and squares again at the end (0x4d9b70).

### targets.zrd

`"name" (kind "health" (h) "target" ("node" [(offset)]) "centroid" (...)
"destroy_anim" ("anim") "name" ("MSG_...") "radar" / "display" /
"reticule_select" / "zoom_select" (bool) "height" (h) "health_below" ("valve"
n h ...) "translate_to_anim_root" ())`. health_below sets a valve when the
target's health falls below each value (likely: the mission scripts' "hit"
and "down" events).

## 4. One AI tick

The AI vehicle's controls are its IControls; Mech::Update calls its Update
(slot +0x0c). For an AI mech that is 0x4e96f0, which runs the brain at
controls +0x5c:

```
0x4e9710 brain update(dt):
  AI off (0x60b938 == 0): stop the controls (+0x14), run the order (vt +4), done
  powered = vehicle vt +0xa0
  0x4cec80: smooth two skill values, then the contacts (0x4ceb50)
  powered down and not +0x2a4: stop, run the order, done
  pick a target: the nearest contact of a side (0x4d6b90: 0 ally, 1 enemy, 2 neither, 3 any;
    the contacts' AiTarget +0xc0 against our masks +0xc4 / +0xc8), within the sight range (+0x1c)
  engaged:
    weapon.cpp's 0x4b4ed0 under 0.25 (+0x2a8 set): 0x4e6d50(1.0), not traced
    0x4e9550: weapons and targets; 0x4e9070: a clear line (likely); the fire timer +0x100
    the attack range (+0x20) and the delay +0x4c x GameIntensity
    a random wait between +0x64 and +0x68
    run the current order (+0x17c, vt +4) until it stops asking to run again (+0x104)
  the order's own update (vt +4); not powered down: the firing state machine (0x4e9fb0 -> 0x4b5b80)
```

Mission start (0x490ab0) builds what the AI uses, in this order: the AI nets
the pilots ask for (CZAiNetEdit through the cache), the inside/outside nets
(0x4c3b80), the vehicles from aiv.zrd by name (0x4e0680 from the vehicle and
mech builders 0x46da80, 0x49e810, 0x4a4de0), the targets (0x4d2e20), the
valves (0x4e2120).

## 5. Ported

All load-time code, checked in Unicorn against the original on every shipped
file (`tests/differential/test_ai_readers.py`, 1211 cases: 698 net and region
files by number and by name, 48 aiv.zrd, 27 targets.zrd, 438 made-up files
reaching 80 marked places in the original). Output streams (records with
valves as registry places, regions as their place in the region list,
strings as text) and the logs of zerr, localize and zSound calls match byte
for byte, at 53-bit and at 24-bit x87. The orders' regions are loaded by the
ported lookup (section 8) on both sides.

| Piece | Original | Port |
|---|---|---|
| reader-file base | 0x4de9c0, 0x4deb20, 0x4deb50, 0x4deba0, 0x4dec80, 0x4deb80, 0x4debd0, 0x4decf0, 0x4ded20, 0x4ded60 | `mech3/zrdraiv` RdrFile |
| net files | 0x4da570, 0x4da8e0, 0x4da840, 0x4da060, 0x4da0d0, 0x4da900, 0x4dac70, 0x4db850, 0x4dae40, 0x4db140, 0x4daee0, 0x4db1b0, 0x4de730, 0x4dc690, 0x4de830, 0x4de010, 0x4de090, 0x4d5280; header 0x4bcb50, 0x4bca20, 0x4bca40, 0x4bca90 | `mech3/zrdrainet` |
| net record | 0x4c5fb0, 0x4c6050, 0x4c63a0, 0x4c62d0, 0x4c6bd0, 0x4d44b0, 0x4d45c0 | `mech3/zainet_node` |
| valves registry | 0x4d3b20, 0x4d3b80, 0x4d3c40, 0x4d3e50, 0x4e4460 | `mech3/zai_valve` |
| valves at run time (section 13) | 0x4d3c00, 0x4d3bf0, 0x4d3d20, 0x4d3d30, 0x4d3ef0, 0x4d3fa0, 0x4d3fd0, 0x4d4080, 0x4d41a0, 0x4d41e0, 0x4d4310, 0x4d4320, 0x4d4480; valves.zrd 0x4e1cb0, 0x4e5290, 0x4e5e30, 0x4e2190, 0x4e1d70, 0x4e2330, 0x4e2120, 0x4e1d00, 0x4e20d0, 0x4e1f10, 0x4e2fb0, 0x4e44b0; the actions 0x4e2ff0, 0x4e3040, 0x4e3070, 0x4e30b0, 0x4e30f0, 0x4e3120, 0x4e3150, 0x4e3180, 0x4e3230, 0x4e31c0, 0x4e3200, 0x4e3350, 0x4e3310, 0x4e3330, 0x4e3500, 0x4e33c0, 0x4e34b0, 0x4e36a0, 0x4e3560, 0x4e3650, 0x4e3870, 0x4e3700, 0x4e37f0, 0x4e3a30, 0x4e38d0, 0x4e39c0, 0x4e3cc0, 0x4e3a90, 0x4e3ba0, 0x4e3da0, 0x4e3d20, 0x4e3e00, 0x4e3d70, 0x4e3d90, 0x4e4020, 0x4e3f50, 0x4e3fb0, 0x4e3e60, 0x4e3f10; the conditions 0x4e40e0, 0x4e4d20, 0x4e4df0, 0x4e4ec0, 0x4e4f90, 0x4e4360, 0x4e43b0, 0x4e4400, 0x4e4430, 0x4e4080, 0x4e5080; the brain's switches 0x4d9c20..0x4d9ca0 | `mech3/zai_valve`, `mech3/zrdrvalve` |
| AiParams | 0x4d98c0, 0x4d99e0, 0x4d9a50, 0x4d9ac0, 0x4d9b20, 0x4d9b70, 0x4d8d30, 0x4d8e20, 0x4d8e40, 0x4d5af0, 0x4d5b10 | `mech3/zai_params` |
| aiv.zrd | 0x4df620, 0x4ded70, 0x4df5f0, 0x4def10, 0x4e0490, 0x4e04f0, 0x4e0600, 0x4e0680, 0x4df730 | `mech3/zrdraiv` |
| targets / traps | 0x4e0720, 0x4e0820, 0x4e0740, 0x4e07d0, 0x4e07e0, 0x4e08c0, 0x4e0890 | `mech3/zrdraiv` |
| reader getters | 0x556fe0, 0x557030, 0x556e90 | `gamez/zreader` |
| mech by name | 0x4845f0 | `mech3/mech_names` mech_index |
| the sensors and the entity sort (section 15) | 0x4a8cd0, 0x4a8710, 0x4a83c0, 0x4a91b0, 0x4a8990, 0x4a8960, 0x4a8b70, 0x4a8530, 0x4a7fd0, 0x4a7d00, 0x4a7c70, 0x4a7c90, 0x4a7cd0, 0x4a7f30, 0x4a9680, 0x4a98c0, 0x4a9910, 0x4a9980, 0x4a9530, 0x4a81f0, 0x4a9a60, 0x4a9cf0, 0x4a92c0, 0x4a9fd0, 0x4aa270, 0x4aa260, 0x4a9e70, 0x4a9fb0, 0x4aa400, 0x448600, 0x4e6fe0; MSVC's sort 0x4ab1f0, 0x4ab500, 0x4ab620; the sort 0x4d5ca0, 0x4d5bc0, 0x4d5be0, 0x4d7360, 0x4d7a40, 0x4d7bc0, 0x4d7650; 0x4d5dc0 | `mech3/sensors`, `crt/msvc_sort.h` |
| the animation-owner registry (section 18) | 0x4d4f20, 0x4d46f0, 0x45b800, 0x508f20, 0x508f30, 0x4d5100, 0x4d47e0, 0x4d4b10, 0x4d4a20, 0x4d4980, 0x4d4700, 0x4d4740, 0x4d4b30, 0x4d4c80, 0x4d4e50 (zEffect's 0x508f40, 0x50b570, 0x50b5e0); 0x4d7270 / 0x4d3340 (a target's messages) | `mech3/zai_anim_owner`, `mech3/zai_target` |
| AI controls and the brain's data (section 12) | 0x49fe90, 0x49ff10 (player.cpp), 0x4d8750, 0x4d8670, 0x4265a0, 0x4e71c0, 0x4e7090, 0x4ac750, 0x4ac740, 0x4a4af0, 0x4a4a70, 0x4a4000, 0x4a3fc0, 0x4cf330, 0x4d9bf0, 0x4d9940, 0x4d8fe0, 0x4b6570, 0x4b6390, 0x4b7060, 0x4b6660, 0x4e82a0, 0x4e8180, 0x4e8070, 0x4e8030, 0x4e7f80, 0x4ce200, 0x4e8090, 0x4cd770, 0x4c9d00, 0x4ce620, 0x4e9380, 0x4e9d80, 0x4e9d90, 0x4c0070, 0x4c0010, 0x4c0520, 0x4c0540, 0x4bffe0, 0x4cbe30, 0x4cbe50, 0x4c8660, 0x4e7e20, 0x4e7c40, 0x4e7dc0; the destructors 0x4d8bf0, 0x4d8c00, 0x4d8c40, 0x4e7820, 0x4e7830, 0x4e7870, 0x4cf630, 0x4e7df0, 0x4e7ff0, 0x4b65f0 | `mech3/mech_control` AiControls, `mech3/zai_brain` |

Hooks (`RdrHooks`): the loose-file search (0x556240; none, FIX (A)),
localize, zSound's lookup. The region lookup 0x4c5c00 is the port's
(`region_find`, section 8).
Not ported on purpose: the editor's writers (0x4dad10, 0x4db8d0, 0x4dc2b0),
the record caches the editor fills (left empty), the AI tester dialog.

Quirks kept: values read as raw words whatever their type; `find` searching
nested lists (a record's key could match a string inside another record; no
shipped file does that); auxname lost when a record is assigned (0x4c63a0,
editor only); the p3 checks swapped as written. FIX (bug) guards where the
exe would crash on malformed data (a non-list where a list belongs, a
non-string where a name belongs, key+5 past a short key); FIX (bug) zeros for
the old tags' uninitialised stack (the test pins the exe's stack the same
way). A string or list where a number belongs gives the exe its pointer bits;
the port 0 (FIX (A)); the tests avoid it.

Breaks: section 7.

## 6. Porting order

Clean boundaries for later work, each a piece with its own test:

1. **Regions and the inside test**: done (section 8), the lookup 0x4c5c00,
   the builder's run-time side it needs, the edges and 0x4c57d0.
   `ZAiNetMechInOut.cpp` (0x4c11f0..0x4c3ed0) is not this: it is the
   mission boundary from the world's `inout` polygons (render.md 6a), read
   at mission load (0x4c3b80 -> 0x4c3930) and asked by the behaviours
   (0x4c3000, seven callers) to keep a destination inside; ported
   (section 11: the reader, the query, the messages, wired in the
   program's brain hooks).
2. **The run-time net**: done (section 9), the builder and the compiled
   net of steps. The mission start's call (0x49e810, player.cpp) is ported
   (mission-start.md). Left: the ground snap and the multiplayer waynet (NetHooks), the built-in nets
   (0x604008, the seven-behaviour net 0x4c0090) and the 105/107 versions.
3. **Net queries**: done (section 10), the nearest step and the way
   finder. What the steps do when a pilot reaches them is step 6.
4. **Valves** at run time: done (section 13), the reader, the conditions,
   every action, the timers, ticked by the missions' frame (0x48f470) and
   ended by the cleanup (0x491780) (mission-frame.md).
5. **Targeting**: done (section 14), AiTarget's fields, the pick
   0x4d6b90, set_target, the target keys, the ranges; CZAiTarget from
   targets.zrd with its damage, triggers and end. The sensors that fill
   the contacts (0x4a8cd0) and the entity sort 0x4d5ca0 are ported too
   (section 15). (0x4ceb50 is the steering's look at the contacts, the
   brain's.)
6. **The behaviours** (0x4c8600 family): done (17.2, 17.4a, items 3a..3i):
   the plain, node, property and mode states, the jump, the attack and the
   fight, the station keeping, the manoeuvre, the repair (0x4d08d0..
   0x4d0ce0). The brain's own step 0x4cd7b0 is dead and not ported (17.2's
   table).
7. **The brain** 0x4e9710 / 0x4e96f0, the order stack, the gates, the vlist
   triggers, steering, the firing state machine 0x4b5b80 with the planner
   and the aim, the lance orders and the radio, the refit: done (17.1..17.8,
   17.9's list). What is left of the brain is unreachable or empty (17.9,
   Not ported on purpose).
8. **The animation-owner registry**: done (section 18): the records,
   claim, release, the messages and the listeners (item 2a), the mission
   load's binder 0x4d4b30 with 0x4d4c80, 0x4d4e50 and zEffect's 0x508f40,
   0x50b570, 0x50b5e0 (item 2b).

## 7. Tests and breaks

`tests/differential/test_ai_readers.py` (probe `tests/probe_ai.cpp`, emulator
side `tests/differential/ai_world.py`). Breaking the port one way at a time showed the test catches: a valve value
read from a 3-item list, negative links kept, the auxname taken from the
localized name, no "mech3" upgrade, the first node one slot late, the
canleave default, one record too many, valves matched in any case, the
valveunion count default, list-first nodes without properties, the reserved
name accepted, the last release one reference late, a deploy byte from the
wrong item, netparams keeping an old has_pos; params item 2 out of order, the
team from the name inverted, the yaw left in degrees, ranges not squared,
Alan's name misspelt, the lancemate bits swapped, p8 item 5 out of order, the
p3-alone copy, p5's jump case, can_configure off by default, mech names
matched by case, an order's region read from item 9, its item 1 dropped; the
targets' reticule default, radar setting the display bit, health_below read
in twos, an int height not converted, a target offset needing 3 items: 32 of
32. Three tried changes made no difference and were replaced: an order's item
17 read without its length check (past the end the port reads the zeroed
spare slot, the same as the default), the targets' header default (the reader
resets the flags itself), and radar on any non-zero value (get_bool_int only
gives 0 or 1).

`tests/differential/test_ai_regions.py` (probe `tests/probe_ai_net.cpp`,
`RegionsRun` in `ai_world.py`): every reg_*.zrd of every mission looked up
twice, every net file and a missing name as regions, then 93 made-up files
(versions, names, loops broken every way, holes before the start,
duplicates, a side of no length, arrangenodes round a loop); the lookups at
53 bits, then a few dozen points per region tested in turn at 24 bits. Streams
(each region's id, name, edges and where its test stopped; the registry's
ids and kinds; the region list) and zerr logs match: 140 of 140. Breaks
caught: the test's start kept after a pass, the plane counting as outside,
the normal flipped, the test always starting at the first edge, a side of
no length's normal, the start's budget not shrinking, the walk starting at
slot 0, names matched in any case, region ids from 10001, arrangenodes not
rounding to a tenth, its angle not snapped, a duplicate record replacing
the first, edges reversed, regions put at the front of the list, a loop
needing 3 nodes: 15 of 15. Three tried breaks were replaced: the dot
product's order (the normal's y is 0 but for sides of no length, whose x and
z are 0, so no order rounds differently), negative links followed (the
reader never keeps a negative link) and the walk following the last link
(the edges then follow another way and the port runs out of memory: caught,
but no use as a break).

`tests/differential/test_ai_nets.py` (`CompiledRun`): 0x402240 for 1..99
in every mission's archive (308 net files), then 137 made-up nets (every
property alone and on edge nodes, special nodes, duplicates, links to
missing and unpositioned nodes, nets built twice, ids out of range, 80
random); the registry and every compiled net's steps (kind, data, links as
slots) match: 174 of 174, and in 29 made-up nets the original crashes
where the port reads (section 9; the emulator pins the bytes just past a
heap block to 0xff, the -1 the port's FIX takes). Breaks caught: the
default valve value on the first link, speed after the waits, edge ends
counted per link, an edge node on its first end only, backup before
mcbwait, plain nodes unnamed, missing links kept, the null step kept by
attach, the deploy target by number, the canleave tests swapped, the scan
aim inverted, edge nodes not counted, the net under the builder's id,
built nets rebuilt: 14 of 14.

`tests/differential/test_ai_paths.py` (`PathsRun`, with a pilot made in the
emulator's memory: brain, vehicle, node name and position, mech and jump
jets): every mission's nets built, then the nearest step to 6 points round
each net's nodes, and in each net of at most 30 nodes 6 ways between random
nodes for pilots near the start, with and without jets, named for a gate or
not, under random valve settings, each way followed up to 7 steps (24
bits); plus a ladder net (a jump, a gate, a valve, a valve union) and a
ring net, 12 rounds each, and a fork whose short way a valve shuts once it
is found (so the finder must search again): 3912 queries, 64 of 64 cases
identical. Ways are not asked in the bigger nets: the walk is exhaustive
up to the best length found, which in them runs past four billion emulated
instructions (and the emulator, which never frees memory, runs out of it);
natively, ways sampled in the biggest shipped nets (246 nodes) take
milliseconds. Breaks caught: the nearest step the last of equals, the
pilot counted at the target within 20, an equal way replacing the best,
only the last leg measured, visited marks kept, the step reached dropped
with the way before it, no new search when the way shuts, valves open on
the other values, jumps for all, gates open to all, unions never open: 11
of 11. Two tried breaks change nothing a caller sees and were dropped:
keeping branches as long as the best (the cut only saves time: a way no
shorter is never kept), and not testing the target when checking a kept
way (targets are node steps, which let every pilot by).

## 8. The net objects

Ported 2026-10-20 (`gamez/znetwork/znet_object`, `mech3/zainet`,
`mech3/zainetedit`, `mech3/zainet_region`); test in section 7.

**Numbered objects.** Every AI net, region and waynet is a numbered object
(0x58aec0, 0x18 bytes: vtable 0x5cefd0, the id at +4, a vector of elements
at +0xc). While it lives it sits in one list (0x603fd8, made by 0x4c7b80),
where 0x58b160 finds the first of an id. 0x58b1c0 hands out a free id in a
range, from the one asked for upwards and wrapping to the range's start
(but a search that started above the start stops when it wraps, so a full
top end gives -1 even with free ids below it; an id asked for outside the
range is taken as it is when free). At mission end 0x58b290 deletes every
object under 10000 (0x5f2a48); the regions (10000 up) are freed by their
own list. The ids likely let a multiplayer session name nets across
machines (the code sits after `znet_dplay.cpp`).

**The builder** (`CZAiNetEdit`, 0x4bcba0, 0x44 bytes). In play it is only
ever made for a moment, with its `build` flag set (0x402240, 0x4797c0,
0x4c5c00); the editing side (flag clear, 0x4bd360 / 0x4bd3d0 from the
debug menu) writes edits back through the record cache and is not ported.
Made for net `n`: its own id comes from 400..499; a net already registered
under `n` is taken as it is (nothing is rebuilt; the builder's file is read
under that net's name); else the file (`net_%02d` of `n` or the name given)
is read into one node (0x4c6d30, 0xbc bytes: the record copied, the
builder at +0xb8) per record, at the slot of the record's number (the
vector grows to the highest number; slots without a record hold a
placeholder node, 0x4c6da0, whose setters do nothing; a second record with
a number is reported, "net %d: ignoring duplicate node %d", and dropped).
Then the link pass (0x4d53f0 -> 0x410590 / 0x4c7170 / 0x4c6fc0: each link
re-stored as the index of the node it names, which changes nothing for the
builder's own nodes) and `arrangenodes` (0x4c74a0, below). With `build`
and at least one record, the file's version picks what is built: 106 a
compiled net (0x4be670, section 9), 108 a region (0x4bf3f0); 105
107 (0x4be690) a waynet, the mission's nav points: every mission's
waynet_N.zrd is 107 (c1's waynet_1: node_00 Start, node_01 Able, node_02
Baker), and those auxnames are the nav points the objectives' "nav" keys
name (0x4797c0 through 0x4bcba0 with the build flag, filling the missions
object's +0xb0; mac 2, 2026-10-21; ported in step 4b3b, vehicles.md, The
nav waynet). 105 (0x4bed90, not ported on purpose: section 17.9) is the
editor's reload: with records, the file's data (0x4dc6f0) and, when a net
was built before (+0x34), every vehicle on zEntity's list whose controls
(vt+0x140) use it (vt+0x64) kept (0x4bf4e0), the old net freed (0x4bf4b0),
the new one built by 0x402590 (a thunk to 0x4025b0, the reader and builder
of an older net class: 0x4021c0 makes it, 0x5c bytes, vtable 0x5bb8d0,
linked on [0x5f4c4c], in the exe's first source file) and handed to each
kept controls (vt+0x04). No shipped net file is version 105 (every one is
106, 107 or 108, and the header turns a "mech3" 105 into 106), so none of
it runs. The builder's destructor leaves what it built alone.

**arrangenodes ( r )** (0x4c74a0 -> 0x4c7300 / 0x4c7340): each node this
one links to is moved onto the circle of radius `r` round it, at the
nearest of `links` even angles (from +z, through atan2(dx, dz)), keeping
its height, each coordinate rounded to a tenth. Nodes are arranged in
slot order, so a node moved by one arrangement can be the centre of a later
one, and a node linking to itself moves its own centre mid-way. In the
shipped nets only `net_` files use it.

**Regions.** 0x4c5c00 is the lookup the vehicles' orders use (aiv.zrd
`vlist` item 10, 0x4dfb00; the old p4 order 0x4e0281). The map's mission
boundary is not looked up again: it is the last of the player's order
regions whose name starts `reg_inside_` / `reg_outside_` (0x4d92f0,
0x4d9360, `strncmp`). A region
already loaded is found by name (0x4c5e50, `strcmp`, so case counts);
otherwise a builder is made for the first free id from 10000 and the
name, and what it built is kept only if it is a region whose own `name`
field is the name asked for (else it is deleted and nothing is kept, so the
file is read again at every lookup). Kept regions go to the end of the list
at 0x60b3f0.

A region (0x4c5b00, 0x20 bytes over the edge net 0x4c5640) is built
(0x4bf3f0) when the builder's net id is 10000 or more, the file's version
is 108 and its nodes form one closed loop (0x4bf000): the loop starts at
the first slot holding a real node (the placeholders before it are skipped
with a budget that shrinks with every slot skipped, so the start must lie
in the first half of the slots) and follows each node's first link until
it comes back to the start; a link to no node is reported (0x800,
"region "%s" node %d points to nonexistant node %d") and ends the walk, as
does a node without links, a negative link or running out of slots. Each
step of the loop becomes an edge (0x4bf1b0 -> 0x4c5960, 0x1c bytes): its
start, and the normal of its vertical plane, (dz, 0, -dx) of the side
normalized (0, 1, 0 for a side of no length on the ground, which then
tests only height). The region keeps the file's `name`.

**Inside** (0x4c57d0 -> 0x4c5a30): a point is inside when it is on the
inner side (dot with the normal not negative) of every edge, so a region
is a convex area drawn with its inside on the right going round (seen from
above, x right and z up: the loop runs clockwise). Heights only count for a
side of no length. The test starts at the edge the last failing test
stopped at and goes round; a pass resets it to the first edge. The region
test of an order (0x4d8e70: vlist item 9 "when inside", item 17 "when
outside"; both set: always) needs the region, so an order whose region did
not load never fires on it.

In the shipped missions, 386 of the 390 region files load (each region in
4184 edges' worth, `test_ai_regions.py`); the four that do not:

| Mission | File | Why |
|---|---|---|
| c1 m1 | `reg_firefly_02_go` | version 106: a net, not a region |
| c1 m1 | `reg_friefly_02_go` | its own name is `reg_firefly_02_go` (the file name is misspelt) |
| c3 m2 | `reg_p2`, `reg_up_circle` | version 106 |

No order names any of them. One order names a region its mission does not
have: in c4b mission 6 the player's own `vlist` sets the valves
`inside_c4m7` and `outside_c4m7` from `reg_inside_c4m7` and
`reg_outside_c4m7`, but the archive's files are `reg_inside_c4bm6/7` and
`reg_outside_c4bm6/7`. The mission's fail objective is `outside_c4m7`
("left the mission boundaries", objectives.zrd `m6`), so in that mission
leaving the area never fails it (and the map has no boundary to draw,
likely).

**Which pair c4b mission 6 meant** (2026-10-22, item 3a; the hulls of the
files' nodes against the aiv.zrd positions). The region file names run one
mission ahead of the missions: c4b m4's archive ships `..._c4bm5`, m5's
`..._c4bm5` and `..._c4bm6` (with `..._c4m5`, the pair it uses), m6's
`..._c4bm6` and `..._c4bm7`. In m6 the `c4bm7` pair is a boundary: inside
x 947..2930, z -3430..-1328, outside a ring around it (x 736..3173, z
-3601..-1145), and all nine of the mission's vehicles start inside both
(the player at (1375, -3135) with Dominic, Epona and Alan beside him).
The `c4bm6` pair is two small areas side by side, not nested (inside 240 by
48 m, outside 870 by 110 m), with no vehicle in either; m5 ships the same
two files, unused. So m6 meant `reg_inside_c4bm7` / `reg_outside_c4bm7`
for `reg_inside_c4m7` / `reg_outside_c4m7`.

Two neighbours, found on the way:
- c4b **m5**'s outside entry sets the valve `reg_outside_c4m5` (the
  region's name, item 16) where its fail objective watches `outside_c4m5`:
  leaving m5's area never fails it either. Its regions load and hold all
  15 vehicles.
- c4b **m4**'s entries run the other way round from every other mission's:
  `outside_c4m4` is set when the player is *inside* `reg_outside_c4bm5`
  (item 9, not 17), and none of m4's 21 vehicles starts inside either m4
  region. So in m4 the pair marks a place the player must not enter, which
  may be meant; left as it is.

The player's own vlist, which these boundaries need, runs since item 3b1
(17.5). **Fixed** (item 3b2, behind `zfix bugs`): the vlist reader
(`zrdraiv`, 0x4dfb00's entries) takes m6's `reg_inside_c4m7` /
`reg_outside_c4m7` as the `..._c4bm7` files and m5's valve
`reg_outside_c4m5` as `outside_c4m5`; no other shipped file has these
names. Headless, walking out of either mission fails it the next frame.

## 9. The compiled net

Ported 2026-10-20 (`mech3/zainetedit` Compiler, `mech3/zai_steps`); test
`tests/differential/test_ai_nets.py`.

**When.** As a mission starts the player's set-up (0x49e810, player.cpp)
asks for nets 1..99 (0x402170 -> 0x402240): a builder for each number reads
`net_NN.zrd` and, for a version-106 file, compiles it (0x4be670 ->
0x4be300). The compiled net (0x4bf910, 0x1c bytes, vtable 0x5ca6d8) is a
numbered object under the net's number, so asking again (or a later
builder) finds it and builds nothing. A pilot's brain (0x4cf330) takes
the net of its aiv.zrd record's first item (AiParams +0x04, "kind" in
section 3: it is the vehicle's net number), as compiled (cast +0x34), or
the built-in one at 0x604008 when there is none. In the port
`nets_build_all` / `net_build`, called by the ported mission start
(`mech3::player_setup`, mission-start.md), whose builders then find each
vehicle's net through znet (0x58b160 -> vt+0x28 -> vt+0x34).

**What.** The net becomes a flat array of behaviours (the order classes of
section 2), one slot per piece:

- each **positioned** node (`node_XX (12 (x y z) (links) ...)`) is a node
  step at the slot 0x4bd670 gave it: plain (0x4cf820, vtable 0x5cc6f0,
  named with the record's auxname), or with `deploy` a deploy node
  (0x4cff10: auxname, both valves and bytes, and the slot of the node its
  last item names), with `mcbwait` an mcbwait node (0x4ccc30: auxname,
  valve, byte, time), with `backup` a backup node (0x4cfa00, no name), in
  that order of precedence. Its position is the record's, put on the ground
  under it when a world is loaded (0x4edab0 / 0x4abd90 at [0x608c84];
  `NetHooks::ground`). With `arrangenodes` the node also joins the
  multiplayer "mcbwaynet" (0x4d5460.., mech_waynet.cpp; `NetHooks`, not
  ported, likely only used by the multiplayer deploy points).
- after it, for **each link** in turn, one step per property the node has
  (0x4bd790), in this order: speed (0x4c9eb0, the rate), wait_interval
  (0x4c9f80), wait_time (0x4ca0a0), valve (0x4ca1c0: the valve and its
  value; a valve given without a value gets 1 on the node's last link and 0
  on the others: the way finder lets a pilot by only while the valve holds
  it, section 10), canleave (0x4cbcf0 with test 0x4d9c80 for `canleave (1
  n)`, n != 0; 0x4cbd40 with 0x4d9ca0 for `(1 0)`), scans (one 0x4cb950
  with a test per scan_aim / scan_hit / scan_time present, each by whether
  its second item is non-zero), jump (0x4cb2a0), sound (0x4cce30), gate
  (0x4cd4a0, the name), shunt (0x4cd6e0), valveunion (0x4cda50, the list
  copied), valve_assign (0x4cdc20); then the same for every **edge node**
  (`node_XX ((from to to...) ...)`, no position) whose first link is this
  node, once for each of its other links equal to the link's target. The
  link's steps form a chain: the node step's link points to the first, each
  to the next (+0x0c), the last to the target's node step.
- slots no piece took hold the shared null step (0x603fa0).

So a path's properties sit on its links, not on its nodes: a node's own
properties apply to every link out of it, an edge node's to the links it
names. Then each step resolves its slot numbers into pointers (+0x6c:
0x4c9e60, 0x4c94a0 dropping links to nothing, 0x4cffd0 for the deploy
target). A link to a node without a position (an edge node) or to no node
is no link (dropped by the node step; a property step whose chain ends
there keeps the slot number as its pointer in the exe: the port's FIX
(bug) makes it the null step).

**Counts.** 0x4bd670 / 0x4bd4e0 / 0x4bd5b0 count the slots first: a node
step, then the node's property count times its links, plus each edge
node's properties times the number of its other ends this node links to.
The shipped nets: 308 files, 12944 nodes of which 3384 edge nodes; the
properties used, most first: valve 4416, canleave 2858, scan_time 2796,
speed 452, wait_interval 363, deploy 288, valve_assign 120, arrangenodes
96, valveunion 94, scan_hit 70, gate 46, wait_time 26, jump 14, scan_aim 3.

**Faults kept or fixed** (fixes.md):

- The trailing links that name no positioned node are cut from a node
  step, but its properties are still put on every link: for a cut link the
  chain starts past the node's links (a write past the vector; with every
  link cut, through a null pointer, a crash at load: 0x4bd790). The port
  starts that chain nowhere (FIX (bug)); the steps are still made.
- A node linking to a node number past the net reads the slot map past
  its end (0x4bde20): heap garbage. One shipped net does it: c2 mission 3's
  `net_13`, a single node linking to a node 1 that is not there. The port
  takes no node (FIX (bug); the test pins the exe's read to the same).
- A node that links to one node twice, with an edge node on that link,
  makes more steps than were counted: the exe writes them past the net's
  vector. The port drops them, so the net is what the exe's ends up as.
- arrangenodes past the nodes: section 8.

## 10. Finding the way

Ported 2026-10-20 (`mech3/zai_path`, the steps' tests in `zai_steps`);
test `tests/differential/test_ai_paths.py`. Per-frame code, at 24 bits.

**Nearest step** (0x4d51d0, a method of every net; eight callers: the
behaviours 0x4c8860, 0x4cae32, 0x4cbfb9, the waynet 0x479b58 / 0x47a8a5,
0x45a159 / 0x45a3a9, 0x4ae45c): the element with a position whose squared
distance to the point is least (the first of equals), else element 0.
Only node steps have positions.

**The way finder** (0x4c7d00, 0x38 bytes, kept by its user through
0x4c7c10 / 0x4c7cb0, made on the first search and dropped when a search
fails or the way is used up; its user in the exe is the brain's order
stack, 0x4c84b0). A search (0x4c7f70 -> 0x4c7e80) from a start step to a
target step:

- a pilot standing within 10 (squared distance under 100) of the target
  has the way [start] at once;
- else a depth-first walk (0x4c7fa0) over the steps' links, in link order,
  that measures the way from where the pilot stands through each step with
  a position (straight lines, 0x51e000), keeps the shortest way to reach
  the target (0x4c8140: only a strictly shorter one replaces it), and cuts
  a branch once it is no shorter than the best (so the first of equal ways
  wins). Steps already on the way are not entered again (each step's
  visited bit, +0x04; the net's +0x18 bit marks a search in progress, and
  a second search of the same net at once finds nothing).
- a step is entered only if it lets this pilot by from where it comes
  (+0xac): a valve step while its valve holds the step's value (0x4ca260),
  a jump step if the pilot's vehicle is a mech with jump jets (0x4cb2c0:
  vehicle slot +0x50, mech +0x3d8 > 0), a gate step if the gate's name is
  the vehicle's node's name (0x4cd5d0), a valve-union step while any of its
  valves holds its count (0x4d4600); every other step always.

The way is kept as a list of steps from the start (the target is not on
it). Asked for the step after one it has reached (0x4c7cb0), the finder
drops the way up to that step (0x4c8330), checks that every step left (and
the target) still lets the pilot by from the one before (0x4c8270), and
answers the next one (the target when the list runs out). If the way is
closed or the step is not on it, it searches again from that step and
answers from the new way; no way, or the target, and the finder is
dropped. So a pilot re-plans only when a valve, gate or the like has shut
its way, and the way is the shortest by straight lines between the nodes,
whatever the ground between them (jump steps aside).

## 11. The mission boundary

`ZAiNetMechInOut.cpp` (0x4c11f0..0x4c3ed0, 68 functions). The reader is
ported (2026-10-22, `mech3/zainet_inout`); test
`tests/differential/test_inout.py`: the original's load run on every world
read into the emulator by its own 0x4ff520, 84 loads identical bit for bit
at 24 bits. The query 0x4c3000 (seven callers) and the messages 0x4c3bd0
are ported (B2); test `tests/differential/test_inout_query.py`: 10,000
segments on every mission's area identical bit for bit at 24 bits. In the
program (B3): WiredBrainHooks::inout_clamp from the vehicle's place
(0x5a85e0); `mission_c1m4_dominic_stops_at_a_no_go_patch_headless`, Dominic
following the player into c1 m4's largest patch: pulled up 3 m short of its
edge, he runs 17 m into it as he slows, is sent back to its pushed-out
outline and stops 5.7 m outside it.

**What it is.** Not a fence round the map: an area's red shapes are
patches the AI keeps out of (in c1 m1, 23 of them, most under 1,000 m^2,
one of 26,000, the map's middle outside all of them), and a few missions
add one large blue shape the AI keeps inside (c2 m2: about 1 km^2 holding
the middle). Blue is red inside out: its corners are reversed and its
inside test inverted. Yellow and cyan shapes are the same but not tested
until a message switches them on; every yellow one shares its name with red
ones (the bridges, the drawbridge `draw01`, c3 m5's `portdoors`), so a
message swaps which of the two sets counts.

**Where it is kept.** A listener at 0x60b338 (vtable 0x5cac78, registered
at start-up through 0x4e78d0 from 0x4c3c9a, freed at exit by 0x4c3290):
its load slot (vt+0x08, 0x4c3da0) reads the areas into its +4 ([0x60b33c],
a vector of 0x88-byte areas, 0x4c3630), its end slot (vt+0x10, 0x4c3dd0)
frees them. After the listeners the mission setup calls 0x4c3b80
(0x490bde), which points [0x60b344] at the current mission's area.

**The variant** (0x4c36c0, a 0x20-byte record on the stack), from the
pilot's game ([0x60861c]+0x6c):

| Game (vt+0x2c) | Name | Areas | Current |
|---|---|---|---|
| 1 campaign | `inoutm` + a number | the operation's missions (0x484840: 4, or 6 for operations 3 and 4) | the mission number (vt+0x4c) - 1 |
| 2 Instant Action | `inoutia` | 1 | 0 |
| 4 training | `inoutt` | 1 | 0 |
| 3 multiplayer, others | none: no areas | | |

A child matches (0x4c3850) by `_stricmp`, or for the campaign by
`_strnicmp` on the prefix and `strtol` on the rest (the whole rest a number;
area = number - 1).

**The read** (0x4c3930): `inout` found by name (0x4f7fe0, list 6); with a
pilot and a variant, each matching child's subtree (0x4c3b50 -> 0x4c3e50)
is walked from the child's place in the world (a fresh matrix, 0x4ed280
mode 3) by 0x4c3ed0: object3ds (5, active) through their matrices as the
scene walk does them (cached world matrix with the tree-valid flag), LODs
(6, active, range <= 0), the grouping classes' (1, 7..11) children; any
other class is reported ("Unknown class type %d for node %s"). Each node
with a model (0x4c3600 -> 0x4c3380): its vertices through the top of the
stack (0x4c3520), each put on the ground with a world loaded (0x4c4240:
0x41eb90 from 100 above, 0x4abd90 within 20, water not counted as
ground; the flag 0x5e8a5c is 1), then every polygon of three corners
or more without flag 0x400 (0x4c32c0) becomes an InOutPolygon (0x4c1ab0,
0x40 bytes, width 3.0 from 0x5e8a18) in the child's area. Whatever was
read, `inout` is then unlinked from each parent (0x4ebb40) and deleted with
its subtree (0x4f9050), so none of it is ever drawn (render.md 6a).

**A polygon** (0x4c1ab0). Its material's colour (exact floats) gives the kind:

| Colour | Kind | +0x35 tested | +0x34 inverted | Corners |
|---|---|---|---|---|
| red 255,0,0 | 0 (also any other colour) | 1 | 0 | in the model's order |
| yellow 255,255,0 | 1 | 0 | 0 | in order |
| blue 0,0,255 | 2 | 1 | 1 | reversed |
| cyan 0,255,255 | 3 | 0 | 1 | reversed |

(The winding flags 0x60b394 = 0 and 0x5e8a1c = 1 are constants.) The
corners are 0x1c bytes: the place, a pushed-out place, the side's length.
0x4c1160 / 0x4c5210 walk them with the two before each, starting from
(n-2, n-1); for each corner v (0x4c11f0) the box (+0x1c / +0x28) takes v,
and the corner before it, c, gets: the length of its side c->v (+0x18,
added to the perimeter +0x14; a length <= 0 reports "zero length segment in
an inside/outside net"), and its pushed-out place (+0x0c): c plus the width
times the normalized sum of the two sides' normals (dz, -dx), each
normalized in x and z (0x4c1030); its y is 0. The name (+0x3c, strdup'd)
is the node's past its first seven characters when it starts with `inout`
and is longer than that.

**An area** (0x4c26b0, 0x88 bytes, vtable 0x5cac70): its polygons (+0x04,
a vector of pointers), the query's state (+0x14..+0x60, copied from the
template at 0x60b348, which the start-up's 0x4c1a00 sets to zero but
+0x20 = FLT_MAX), a box (+0x60 FLT_MAX, +0x6c -FLT_MAX, never set), the
last answer's flags (+0x78 from inside, +0x79 to inside) and normal (+0x7c).
The last 0x10 bytes are left unset: the vector's areas are copies of one
made on the stack (0x4c3630), so they start as its leftovers; every query
sets the flags, and only the vehicle record reads the normal.

**The query's state** (0x4c bytes, the area's +0x14; each polygon's pass
fills its own copy): +0x00 from, +0x04 to, +0x08 the polygon, +0x0c/+0x10
the side crossed first (its two corners), +0x14/+0x18 the side crossed
last, +0x1c the length from -> to in x and z, +0x20 / +0x24 the first and
last crossing's place along from -> to (0..1), +0x28 the near point, +0x34
the far point, +0x40 the crossings, +0x44 / +0x48 the sides met by the ray
from `from` through `to` and by the ray from `to` back through `from`.

**The query** (0x4c2b00, ECX the area, from, to; it returns `to` or one of
its own points):

1. Reset (0x4c2a80), then each tested polygon's pass (0x4c2080): every
   side (from the last corner round, 0x4c1410) that the line from -> to
   separates, and that separates from and to, is a crossing, its place
   t = -sa / (sb - sa) kept when first or last; the rays' counts are
   added up. A polygon with no crossing that holds both ends (both rays'
   counts odd, or even for blue and cyan) becomes the area's polygon with
   both flags set; one whose first crossing comes before the area's
   becomes it, with its own two flags.
2. Nothing crossed and not both ends in one polygon: `to`, unchanged.
3. `from` inside the polygon: the point of its pushed-out outline nearest
   `from` (0x4c2470 over 0x4c22c0: each side's projection in x and z when
   it falls on the side, else its first corner, by the distance in 3D with
   the outline at height 0), and the normal of that side.
4. `to` inside and one crossing: the far point, the polygon's width (3 m)
   short of the last crossing.
5. Otherwise, a way through it: the near point (the width short of the
   first crossing), unless the polygon's pushed-out corners lead round:
   0x4c2f20 measures the outline from the first crossing's side to the
   last's one way (both gaps added) and takes that way when it is under
   half the perimeter, else the other; the corners are walked that way
   from the first side to the last, each one in a clear line from `from`
   (0x4c2e30: no side of any polygon of the area, tested or not, crosses
   it, 0x4c25f0 / 0x4c1770) replacing the answer, so the last clear one
   wins.

The pull 0x4c3000 (ECX the vehicle, EDX `in`, the stack `out`): with a
current area, the query from the vehicle's place (0x5a85e0) to `in`;
pulled when the answer is not `in`, `out` then the answer with `in`'s
height (read after the copy, so when the caller passes one buffer the
answer's own height). It also fills a record on the vehicle (+0xd8: new
0x3c bytes, 0x4c0ed0 / 0x4c0f70, the frame counter, both ends, the flags,
the normal, the vehicle's place) that nothing in the exe reads; not kept.
The callers ported so far: the attack's return 0x4cae90 (its way point),
the drive 0x4c8a70 (its destination), the follow 0x4d1980 (the target's
place) and the walk 0x4cee80 (its carrot, in and out one buffer). Three
are in states not ported (0x4c9894, 0x4d13c0); the repair state's 0x4d0ce0 is ported (item 4b6).

**The messages** (0x4c3bd0, ECX an animation, EDX the AI message, from
the AI's message handler 0x4d4a20, section 18): its node (0x50b5e0) names
shapes; of the current area's polygons with that name only the yellow
(0x39), red (0x3a), cyan (0x3b) or blue (0x3c) ones are tested from then
on (0x4c2a30 over 0x4c29a0); any other message tests the red ones. The
bridges' destroy animations send 0x39 and their resets 0x3a, which reach
this through the records the mission load's binder 0x4d4b30 gives them
(section 18).

**What the worlds hold** (shapes per area; all red but the yellow and blue
counted):

| World | Areas | Yellow | Blue |
|---|---|---|---|
| c1 (op 1) | m1 23, m2 34, m3 22, m4 40; ia 95 | m2 1, m3 2, m4 3; ia 5 (bridges: `lakbr`, `rivbrg`, `span04`, ..) | |
| c2 (op 2) | 36, 15, 15, 6; ia 49 | m3 1 (`vbridge`) | m2 1, m4 1 |
| c3 (op 3) | 18, 20, 26, 36, 59, 30; ia 145 | m3 2, m4 3, m5 3; ia 6 | |
| c4 (op 4, m1..m3) | 62, 27, 42; ia 93 | m2 3 | |
| c4b (op 4, m4..m6) | 12, 5, 5 | | m5 1, m6 1 |
| t1 (training) | t 23 | | |

No world has cyan. `inoutmp` (multiplayer) is never read; c4b and t1 have
no `inoutia`. Polygons hold up to 242 corners.

**Fixes** (fixes.md): a child numbered past the operation's missions is
skipped (the exe writes it past the vector; an operation outside 1..4 makes
no areas at all, so every child would); a mission number past them selects
none (0x4c50e0 is unchecked); and [0x60b344] is cleared at the mission's
end (the exe leaves it on the freed area, so a next mission without
areas, multiplayer or a world without `inout`, would query freed memory).
None happens with the shipped worlds and campaigns. In the query: with
every distance NaN the nearest outline point keeps no side and the exe
reads its stack's leftovers for the normal; the port leaves the normal.

**Not ported.** The vehicle record; the
debug view 0x4c4730 (zaitarget.cpp: the boundary's polygons made into
nodes).

## 12. The AI controls and the brain's data

Ported 2026-10-20 (`mech3/mech_control` AiControls, `mech3/zai_brain`); test
`tests/differential/test_ai_brain.py`. Only what
the constructors set up: what the brain decides each frame (0x4e9710), the
behaviours and the firing are not ported.

**Who makes them.** player.cpp's AI mechs (0x49fa20 -> 0x49fe90:
`new(0x320)`, 0x4d8750(owner 0, net, record, 1)), the campaign's lancemates
at launch (0x49e5e0 through 0x49fa20 on the player's own net, the second caller 0x49e208) and the AI-driven vehicles
(0x49f800 -> 0x49ff10: 0x4ac860(0xcd8), malloc and zeroed, 0x4e71c0). The
net is the vehicle's compiled net (mission-start.md section 3).

**The mech's** (0x320 bytes): MechControls 0x4265a0 with most-derived 0
(view state 2, ThrottleConst off, the hat at 1, every axis 0; +0x40 and
+0x5b left as the heap had them), the brain on +0x5c (a base class, vtable
0x5cd2c8), the IControls virtual base at +0x30c (vtable 0x5cd210, owner
+0x31c, the owner the mech's constructor sets through vt+0x08). Its
vt+0x04 is the brain's set-net 0x4e9380, vt+0x0c the update 0x4e96f0,
vt+0x88 the settings' assignment 0x4d99e0 (the launch gives the controls
their record so), vt+0x90 the brain itself (player.cpp holds its +0x88 at
1 for the power-down's update).

**The vehicle's** (0xcd8): the basic vehicle controls 0x4ac750 (veh_basic.cpp:
its +0x764 block holds at +0x28 a pointer to turret limits at +0x2c,
0x4a4a70: the sines and cosines of -pi/2 and pi/2; the -pi and pi it is
also given are not kept), the brain on +0xa04, +0xcb0 a zero vector,
+0xcbc 0, +0x38 2, the IControls base at +0xcc4 (owner +0xcd4). Every other
byte stays 0 from the allocation.

**The brain** (0x4cf330, 0x2ac bytes; offsets the brain's):

| Off | What |
|---|---|
| +0x00 | the record's settings copied (AiParams, 0x4d9bf0 -> 0x4d9940: the 0xcc bytes, the orders' list copied, an empty one as none, the pilot and voice strdup'd), +0xdc FLT_MAX |
| +0xe0 | the virtual base pointer |
| +0xe4..+0x104 | 0, FLT_MAX, 1.0, 0, FLT_MAX, byte 0, 0, 0 (+0x100 the fire timer), byte 0 (+0x104 the order's run-again flag; section 4) |
| +0x108 | the firing state (0x4b6570, 0x58 bytes): 11 words from 0x5e8868, a template the CRT start-up's initializer 0x4b6500 fills (phase 8, time FLT_MAX, quick delay 3, delay 6, rest 1, plan 0, reach cos 20°, track cos 30°, fire cos 45°, range 50, obstacle 0; the image holds only the 8; W34), +0x2c the brain (0x4b6660, which also calls the brain's IControls vt+0x10(0): nothing then), +0x34 a fire plan (0x4b6390, vtable 0x5c9458), +0x54 the aim error (`new(0x24)`, 0x4b7060: offset 0, drift (1, 0, 0), off, 0, limit 1.0; 17.7) |
| +0x160 | the order stack, a `std::deque` of 0x98-byte orders (blocks of 26, 0xf70 bytes); +0x190 its owner, the brain; the last order is the current one (+0x17c) |
| +0x194 | an order of the brain's own (0x4e8180, vtable 0x5cd8e8), its owner the brain |
| +0x22c..+0x23c | byte 2, then four words 0 (+0x238 and +0x23c objects the destructor deletes; nothing here sets them) |
| +0x240 | a step (0x4cd770 over 0x4c9d00: vtable 0x5cc290, +0x0c the null step 0x603fa0; the base clears only bit 0 of +0x04) |
| +0x250 | steering (0x4ce620, 0x48 bytes, likely: 0x4ce6a0 tests a distance against +0x34): zeros, +0x34 20.0, +0x38 [0x60b4a0] = cos(pi/4) (start-up 0x4ce5b0); [0x60b4b8] / [0x60b4bc], read into +0x0c, +0x10, +0x2c, +0x30, are never written (0) |
| +0x298..+0x2a8 | a zero vector, byte 0, the radio |

Then: without a net, znet's object of the record's first item (0x58b160,
vt+0x28, vt+0x34), else **the built-in net** at 0x604008 (made at start-up
by 0x4c0310 over the seven-behaviour net, not ported: the port has none,
so a pilot whose net is missing gets no order; player.cpp never asks for
one without a net). Then **the first order** (0x4e9380): the net as
compiled (vt+0x34; not compiled: nothing more, no order at all), every order
popped (none at first; each destroyed in place, a block freed as it empties,
the map with the last), and one pushed (0x4e9d90: an empty deque takes a
block and a map of two, `new(0xf70)` then `new(8)`, the order in the
block's middle; the order is 0x4e8070's default, whose template argument
is unused, then +0x14..+0x2f copied from the template at 0x60b898, or from
the last order when there is one, and the brain its owner). In it the net
builds its part at +0x30 (vt+0x5c: 0x4c0070 for a compiled net, vtable
0x5ca748, 0x4c0520 for the built-in one, 0x5cab50; the order and the net)
and the shared behaviour at 0x60b460 (class 0x4cbe10, made at start-up)
its state at +0x54 (vt+0x8c, 0x4cbe30: vtable 0x5cbed0, the order);
+0x0c and +0x10 point at them.

**The radio** (0x4e7e20, `new(0x40)`, 0x4e7c40): for a pilot with a voice
(aiv.zrd `sound_tag`, set by default for the three lancemates by name),
sixteen zSound lookups of `<voice>_<event>` (0x4e7dc0: call_help,
last_shot, confirm_order, confirm_refit, disregard_order, disengages_order,
ejecting, engaging, friendly_fire, low_ammo, out_of_ammo, nuke, follow,
stop, repairs, objective), kept as handles. Only when the player's vehicle
([0x60861c] +0x14) counts the pilot's teams (5 bits at a time, as the team
masks) among its allies, or when there is no player's vehicle. At mission
start there never is (the cleanup 0x491780 clears it, the launch sets it),
so every voiced pilot made then gets a radio, enemies too; the lancemates
made at launch (0x49e5e0, ported: mission-start.md section 12) are checked
against the player's mech.

**Destruction** (IControls slot 0 with 1: 0x4d8bf0 / 0x4e7820): the brain's
destructor 0x4cf630 (the objects at +0x23c and +0x238, the radio, the
orders first to last with the block and the map, the firing state's
0x4b65f0: +0x14 unless it is +0x30, +0x30, +0x50 and the aim, then the
settings), for the vehicle then the basic controls' 0x4ac800 (the owner's
vt+0xec and 0x402510 on its answer), then the block (`delete`, the
vehicle's 0x4ac890).

**Fixes** (fixes.md): the radio's names are built with `sprintf` into 32
bytes, so a voice of 15 or more characters overruns the frame (the shipped
are 3 to 5): cut to fit, FIX (bug). The vehicle controls' destructor reads
through its owner, which a vehicle that failed to build never set
(mission-start.md section 5: a crash): asked only when there is one, FIX
(bug).

**The test** (`test_ai_brain.py`, probe `tests/probe_ai_brain.cpp`): every
aiv.zrd record of the 48 archives (768, mech and vehicle controls in turn)
read by the ported reader, then 600 crafted (random words, first items,
teams packed and odd, voices of 0 to 14 characters or none, pilots, order
lists of 0 to 4 random entries or none); the vehicle's net compiled, not
compiled or none, znet answering a compiled net, the net base or nothing;
no pilot, a pilot without a vehicle, or a vehicle with assorted allies; an
owner or none (the vehicle without one is not deleted). The original runs
first (x87 at 53 bits, the start-up's cos(pi/4) and the shared behaviour
made by their own code, the built-in net's vtable put in place), every call
out stubbed; the port then on a Replay of its answers. Compared: the call
traces (`new` and `delete` with their blocks, znet, the sixteen sounds, the
owner's calls; the settings' list copies and strings apart), every block
the original allocated as the port's object laid out byte by byte (the
step's +0x04 as its bit), the settings' copies (the list walked, the
strings as text), nothing written outside the blocks: 1368 of 1368, 13,595
calls and 5478 blocks identical. Breaking the port showed the test catches
it, 20 of 20: +0xec 0, the firing defaults moved, the steering's 20 as 10,
a radio for every team, two radio events swapped, the map before the block,
the built-in net's part as a compiled one's, any net taking an order, znet
not asked, the turret limits swapped, view state 0, the vehicle's +0x38 0,
the map freed before the block, one team per value for the radio, the
brain's own order without its owner, the aim's (0, 1, 0), the vehicle's
destructor not asking its owner, the order without its owner, the
settings' copy without its orders, a radio without a voice.

**In the program** (`app_mission.cpp`): player.cpp's hooks make the port's
controllers over the vehicle's compiled net and record, and the AI mechs on
them through the ported constructor chain (mech-build.md); the placement
0x4161c0 puts it on the ground at its record's place (mission-start.md
section 11), the power-down runs `Mech::power`, the brain's +0x88
is the brain's settings. The vehicles themselves are not ported, so their
controllers are made and deleted. `mw3tests`
`mission_start_reads_c1m1_vehicles_headless` checks c1 mission 1's AI mech:
owens_01, an Owens (11 parts: its model has no hands) on team 2, powered
down, its brain's settings, one order on net 4 with the net's part and the
shared behaviour's state, no radio (no voice).

## 13. Valves at run time

Ported 2026-10-20 (`mech3/zai_valve`, `mech3/zrdrvalve`); test
`tests/differential/test_ai_valves.py`.

**A valve** (0x4d3b20) is a name, a value holder and its reactions. The
holder (0x4e4460, vtable 0x5cd558) keeps a number: get vt+0x04, set vt+0x08,
vt+0x0c says valves.zrd may replace it. Setting a valve (0x4d3c00) stores
the value and fires its reactions (0x4d3bf0) only when it changes, and
gives back the old one. The reactions sit in one slot (+0x08): the first
alone, the second turns it into a list of both (0x4d3fd0, vtable 0x5ccd70),
later ones join the list (0x4d4320); firing a list fires each in order
(0x4d41a0). Every reaction's vtable: +0x00 the deleting dtor, +0x04 tick,
+0x08 fire, +0x0c the list cast.

**valves.zrd** (one file object at 0x60b668, 0x4e5290 over the reader-file
base, read at the mission setup by 0x4e2120: a reference held while every
record is built). A record is `"valve" (tag (args) tag (args) ...)`; a
valve may have several records (c1 mission 1's `bramp` has four), each
adding to it. The record (0x4e2330 into the 0xc8-byte block at the file's
+0x2c, 0x4e2190) reads, all lists of raw words, names strdup'd: `value`
(n), `sound` / `sound_once` (name n), `animation` / `animation_once`,
`scan_aim` / `scan_hit` / `scan_time` / `canleave` (vehicle on n),
`shutdown` (vehicle down n), `missionreset` / `missionabort` /
`missioncomplete` (n), `destroy` (entity n), `setteam` (entity team n),
`attack_strategy` (entity strategy n), `update` (valve), `delayupdate`
(valve seconds), `any_nonzero` / `any_zero` / `all_nonzero` / `all_zero`
(any value: the shipped files give the string `namelist`, so the next
`namelist` found is the list of names), `teleport` (entity (x y z) yaw n).
The tag lookup searches nested lists (as every reader file's), and the
second of a pair wins (`sound_once` after `sound`).

**The build** (0x4e44b0): the valve found or made (0x4d3c40); a condition
tag needs a non-empty namelist ("Expected non empty namelist"), a namelist
without one is reported ("Expected no namelist") and ignored; a valve that
already holds a condition cannot take another ("Conflicting definition":
the record is dropped). The condition replaces the holder; then `value`
sets the holder directly (no reactions fire), then the actions are added in
a fixed order: sound, animation, the four scans, shutdown, destroy,
setteam, attack_strategy, teleport, missionreset (only reported: "Failed to
create mission reset", nothing is made), missionabort, missioncomplete,
update, delayupdate. Reports go to 0x49bfd0, empty in the release build.

**The conditions** (0x14 bytes over 0x4e40e0): the namelist's valves found
or made (a name that is no string is dropped with a report), each given an
update reaction of the condition's valve (0x4e4080), so a change of any
member fires the condition's reactions. The value is computed on each get:
any nonzero, any zero, all nonzero, all zero (vtables 0x5cd568, 0x5cd578,
0x5cd588, 0x5cd598); a set is ignored, but the set still fires the
reactions when the computed value differs from the one asked.

**The actions** (each fires when its valve's value equals its own, but the
two updates):

| Tag | Made by | Fires |
|---|---|---|
| sound | 0x4e2ff0: zSound's lookup of the name (0x56f6f0) | 0x4e3040: plays it (0x56e7e0, volume 1); `sound_once` (0x4e30b0) only the first time |
| animation | 0x4e30f0: the mission's animation by name (0x50b450) | 0x4e3120: runs it (0x508c10); `_once` the first time |
| scan_aim, scan_hit, scan_time, canleave | 0x4e3230: the named vehicle's brain (0x4d7330, vt+0x40, vt+0x140, IControls vt+0x54) found as the mission starts | 0x4e3200: the brain's switch on or off by the record's flag: +0x98, +0x9c, +0xa0 (aiv.zrd p8's 14th..16th items), canleave +0x3c (params' second item) with +0xdc FLT_MAX (0x4d9c20..0x4d9ca0, the same functions the compiled nets' scan steps name) |
| shutdown | 0x4e3560 | 0x4e3650: a mech of that name powered down or up (0x41cc30, the record's second item) |
| destroy | 0x4e33c0 | 0x4e34b0: a mech destroyed (0x41cfa0(1)), anything else through its vt+0x70 (`WiredValveHooks::mech_destroy`; the name `player` is the launched mech, 0x004140c0's). Shipped on mechs only in c2 m2: the refinery's animation sets valve `apcan01` (event 0x2b, the binder), and with `player_in_gas` or `dominic_in_gas` (the regions `reg_gas`) that mech dies. A turret or target named by a destroy still logs `obj.vt+0x70`; none ships |
| setteam | 0x4e3700 | 0x4e37f0: the entity's AiTarget masks from the team, as the mission start sets them |
| attack_strategy | 0x4e38d0 | 0x4e39c0: the vehicle's brain +0x40 (aiv.zrd p2's first item) |
| teleport | 0x4e3a90 | 0x4e3ba0: zEntity's vehicle (0x5a8210) placed at (x y z) and yaw (vt+0xdc), its controls given the brain's current order's net again (IControls vt+0x04, section 12), so it plans its way from there |
| missionabort | 0x4e3310(0x48f730) | the missions' abort (0x48f9e0) |
| missioncomplete | 0x4e3310(0x48f710) | 0x48f710 is empty (a report): nothing happens |
| update | 0x4e3d20 | 0x4e3d90: the named valve's reactions fire, whatever the value |
| delayupdate | 0x4e3f50 | 0x4e3fb0, on any change: a timer (0x4e3e60) that sets the named valve to this valve's value then, the delay after the game time [0x60991c] |

**Timers** (the slot 0x60b514, one or a list): the missions' update
(0x48f470) calls 0x4d3d30 every frame; a timer whose time is past (0x4e3f10:
time < now) sets its valve, deletes itself and leaves the list (0x4d41e0,
which visits a timer added meanwhile too; an emptied list deletes itself).
A **lone timer** whose valve's reactions start another delayed update is
turned, as it runs, into a list of itself and the new timer; 0x4d3d30 then
clears the slot, so the new timer (and every other one the same update
starts) is lost. FIX (bug), behind `zfix bugs`: the list stays. Two shipped
chains can reach it: c3 mission 2's `ambush_powerup_down_a` (1 s) starts
three 10 s timers for the radio lines `ig3214`, `ig3215`, `ig3205`, and c3
mission 3's `m3n2go`; whether the lines are lost depends on whether another
delayed update was waiting then. At mission end (0x4d3fa0, from the cleanup
0x491780) the timers are deleted and every valve with them (0x4d3ef0).

**In the shipped files** (46 of 48 missions have one, 1512 records): sound
537, delayupdate 294, attack_strategy 236, sound_once 229, shutdown 185,
any_nonzero 68 and all_nonzero 69 (all_zero 2), teleport 5, destroy 4. No
mission uses scans, canleave, animations, setteam, update or the mission
tags. c3 mission 3 keeps a dead record, `:m3n2go_delay` (a colon in its
name, `:` around a shutdown of avatar_02, a mech the mission does not
have: a second Avatar taken out, likely). Every shutdown in
the shipped files has the power flag 0: it **wakes** a mech (the AI mechs
that start powered down, mission-start.md section 8).

**Fixes** (fixes.md): the record's tags read as lists and names as strings
whatever they hold (a crash otherwise: FIX (bug), the six broken files of
the test); a scan whose vehicle has no brain writes through null when it
fires; a teleport of a pilot without an order reads before its deque; the
lone timer above. A valve that updates itself, directly or round a loop of
updates and conditions, recurses until the stack runs out in the exe (the
update action 0x4e3d90 fires its target's reactions whatever the value, so
any such loop never ends). The port skips a valve firing while its own
reactions run (`Valve::fire`, FIX (bug), Owen 2026-10-20: each reaction
round the loop runs once per set). The only valve reaction that fires
another at once is the update, so the guard changes nothing the exe
survives: `test_ai_valves.py` counts the skips, none in any case.

**The test** (`test_ai_valves.py`, probe `tests/probe_ai_valve.cpp`, the
emulator side over `ai_world.py`'s): every mission's valves.zrd loaded,
then forty steps of sets (every valve to the values the records use),
ticks forward in game time and the mission's end; then 300 made-up files
(every tag, conditions over earlier valves, updates of later ones, scans of
a vehicle with a brain, entities found or not) and six broken ones the
original crashes on. The world is fake: names to objects whose vtable
slots answer from a table, brains with one order. Compared: one stream (the
registry, each valve's holder, value and reactions with every field, the
timers, after the load and after every step; the brains' and targets'
words written) and the log of every call out (zerr, the lookups, sounds,
animations, powers, destroys, the objects' calls, places, nets): 352 of
352, the original path (`zfix bugs` off; the lone timer's fix has its own
`mw3tests` case). Breaking the port showed the test catches it, 22 of 22:
sound_once replayed, any_zero as any_nonzero, all_nonzero as any_nonzero,
a set firing unchanged, conditions not firing on a set, a timer due at
its time (the port then loops forever: caught as a hang), delayed updates
only on one value, the lone timer kept, a list's tick stopping at the
first done, a scan's flag and value swapped, sound losing to sound_once's
order, setteam's allies keeping bit 0, shutdown's flag inverted, a
conflicting condition replaced, the initial value firing, a teleport
reusing its first controls, missionreset made, null names kept in a
namelist, the namelist from its second name, members without their
update, the attack strategy one word off, canleave without +0xdc.

**In the program** (`app_mission.cpp`): the mission setup's 0x4e2120 runs
`valves_load`; the actions find the AI mechs by their records' names (the
other vehicles are not ported, so their lookups give none), power them
through `Mech::power`, write their brains' settings, re-plan a teleported
pilot, play sounds through zSound. A teleport places a basic vehicle
through its place and a mech through Mech::set_place (0x00418f30, its
vt+0xdc). Until 2026-10-30 a mech's placement went to the census, and
c2m1's thor_01 stayed where it was
(`mission_c2m1_teleport_valve_places_the_mech_headless`);
animations and destroys are logged; the mission's abort is the port's loss
(0x48f9e0). The missions' frame 0x48f470 ticks the timers each Core frame
and the cleanup 0x491780 ends them (`valves_end`), mission-frame.md.

`mw3tests` `mission_start_reads_c1m1_vehicles_headless` loads c1 mission
1's valves.zrd through the program: `bramp` holds its four reactions, and
setting it wakes the Owens (Mech::power) and starts its two timers.

## 14. Targets and targeting

Ported 2026-10-20 (`mech3/zai_target`); test
`tests/differential/test_ai_targets.py`.

**AiTarget** (0x4d56e0, vtable 0x5cced8, the vehicle's +0x68..+0xd0; the
port's `ai::AiTarget`, which `Mech` derives from):

| Off | What |
|---|---|
| +0x68 | height (10; a mech's from its model's box) |
| +0x6c / +0x70 / +0x74 / +0x78 | the target selected, the last ally selected, the last enemy, the one saved (0x4d7220; 0x4d7240 gives it or the last ally) |
| +0x7c / +0x80 | the sensors' contact on the nearest of the player and his lancemates under orders (+0x80), kept and owned at +0x7c when in no list (section 15) |
| +0x84..+0x90 | a vector of tracked contacts (the constructor's two nulls; nothing fills them): the sensors put each frame's new contact on the same vehicle in place |
| +0x94 / +0x98 | the sensors' contact on the player (+0x98), owned at +0x94 when in no list; a mech's level of detail goes by its distance (0x41e870) |
| +0x9c / +0xa0 / +0xa4 | the sensor range, the range and its square (1000; 0x4d6d00, 0x4d6d20) |
| +0xa8..+0xb4 | **the contacts**: a vector of 0x30-byte records the vehicle owns (0x4a7be0 deletes them), nearest first |
| +0xb8 | the nearest contact when within 8 m (64 squared) |
| +0xbc | bit 0 someone has it targeted, bit 1 seen by an enemy, bit 2 passive radar (the radar mode key), bit 3 the player has seen it (never cleared), bit 4 a lancemate under the player's orders (0x4d5be0) |
| +0xc0 / +0xc4 / +0xc8 | the team masks: a bit per 5-bit team in the value; allies the same bits but bit 0, enemies all the others but bit 0, so team 0 is no one's ally or enemy |
| +0xd0 | a net of its own, made on first ask (0x4d70f0 over 0x4c0090; 17.2, ported) |

**A contact** (made by the sensors, section 15, every frame anew): +0x00
the vehicle seeing it, +0x04 what is seen (the entity's vt+0x24; its
vehicle through vt+0x2c, 0x4e6fe0, whose vt+0x28 is its AiTarget), +0x08
the entity, +0x0c the squared distance, +0x10 the offset to it, +0x1c -1,
+0x2c bit 0 in sight. Only the player's C3 network shares contacts (section
15); allies do not otherwise.

**The pick** 0x4d6b90(side): the first contact of the side (0 ally, 1
enemy, 2 neither, 3 or anything else any), in sight or not. The brain's
(section 4) and the MFB check's 0x45a270. **set_target** (vt+0xc0,
0x4d5e40): unless told to keep, an ally becomes the last ally, an enemy the
last enemy; none clears the last picks it was.

**The player's target keys** (mech.cpp's handler 0x41c28f.., all called
with side 0): W 0x4d64b0 the next ally in sight after the target (else the
first before it), Shift+W 0x4d5ee0 the one before (else the last after),
Ctrl+W 0x4d6970 the nearest; E 0x4d6710, Shift+E 0x4d6260, Ctrl+E 0x4d6a80
the same for **what is not an ally**: enemies and neutral things alike.
Each first goes back to the last pick of its kind when that is not the
current target; with no current target in the list, the nearest.

**The sensor range** 0x4d6d20 (the launch's, mech-build.md): the player's
own vehicle (or one whose vt+0x58 answers) in Instant Action or
multiplayer sees 50000; every other range is capped at 1000.

**CZAiTarget** (ZAiTarget.cpp, 0xf4 bytes: AiTarget through 0x4d7f80, which
keeps every one in the list 0x60b584, then a description at +0xd4 and
+0xf0). The mission start ("Post Vehicle Fixup", 0x4d2e20) makes the HUD's
three markers (`hud_target_ally_sm`, `_enemy_sm`, `_other_sm`, zTwoD images
at 0x60b4f0, 0x4d2d70 by side) and, for each targets.zrd record, a target
per node of list 6 named as the record (0x4d2d80); 0x4d31d0 does the same
for traps.zrd by name prefix (none ship). For each node:

- **the description** (0x4d2480's defaults: health 100 of 100, no name, no
  triggers, flags 0), then 0x4d25c0: the record's `destroy_anim` must be an
  animation of that node (0x50b4e0: in use, the name, its node), else no
  target. 0x4d24f0: the record's health, or the animation's (+0xb0 of
  +0xac) without one; the name strdup'd; `health_below`'s list taken over
  (so of two nodes of one name only the first gets the triggers) and sorted
  highest first (0x4d21f0, MSVC's sort); display, radar, reticule_select
  and zoom_select as four bits. A record without `destroy_anim` keeps the
  defaults (every shipped one has one).
- **the object** 0x4d2960: zEntity3d's node set (0x5a8390, the record's
  `target` and `centroid` subnodes), the node-to-entity map (0x40eaa0), the
  offsets on those subnodes (0x4fc1b0); the destroy animation's node (+0x64,
  else +0x40) gets the target's damage callbacks 0x4d26b0 / 0x4d26d0: new
  ones (0x5833b0, 0x5833f0) when it has none, or the animation's own taken
  over; a node with someone else's callbacks or inherited ones is reported
  (0x4d26f0, 0x4d27d0) and left; the animation claimed in the owner registry
  0x4d4f20 (section 18; released 0x4d5100 before a new node);
  `translate_to_anim_root` moves the animation's root (0x4d28e0) and every
  child but the two subnodes back. The team masks from the record's first
  item, the height from the record or the node's box.
- **damage** 0x4d2c00 (a weapon) and 0x4d2cd0 (a collision): the health
  drops, every trigger whose threshold it is now below (or every one left
  at 0) sets its valve to its second item, at 0 the target is destroyed
  (vt+0x70 0x4d2b30: the destroy animation runs, then 0x4d2b60: the rest of
  the triggers, health 0, inactive); a weapon's hit is told the health left
  (0x583730) or the kill (0x583710).
- **the end**: the cleanup's 0x4d8270 deletes every target (0x4d8360,
  0x4d2aa0: the callbacks taken off its node 0x583400, the description,
  0x4d81e0, 0x4d58a0), its 0x4d3030 the markers.

**Fixes**: none needed; `team_number` (0x4d71a0, the masks back to teams)
would loop for ever on masks of 0 and is not ported (no caller here).

**In the program** (`app_mission.cpp`): the targets are made from the
reader's targets.zrd on zClass's nodes and zEffect's animations, their
callbacks put in zWeapon's damage records (zWeapon's hit code is not ported,
so nothing calls them yet), the triggers set the mission's valves; zEntity3d,
the node map and the markers are logged. The cleanup deletes them.
`mw3tests` `mission_start_reads_c1m1_vehicles_headless`: c1 mission 1's
`satdish` is made with 35 health and its two triggers; a hit of 10 fires
`comcntr_hit`, one of 25 `comcntr_down` and destroys it.

**The test** (`test_ai_targets.py`, probe `tests/probe_ai_target.cpp`).
Part 1, 3000 cases: random contact lists (0 to 12, in sight or not, teams
of one or two bits, team 0 and all bits, duplicate entities and owners) on
an AiTarget with random masks and last picks (in the list, out of it,
none), each running one of the pick, the six keys, set_target, the side
test, the ranges, the save; the call traces (every vt+0x2c and vt+0x28
asked) and the fields after compared: every set_target call site in the
keys reached. Part 2: the 27 shipped targets.zrd and 300 made-up record
sets (traps among them; up to 33 triggers, so the big sort runs) on
made-up nodes and animations (several nodes of one name, boxes or none,
animations not found, on another root, the node's handlers none, the
animation's, a foreign callback, another animation's, collide only,
inherited), then hits, collisions, destructions, another animation taking
a handler, the deletion and the markers; the call traces and every
target's fields, description and triggers, and every node's handler table
after each step: all identical, every marked branch of the original
reached. The x87 at 24 bits. Breaking the port showed the test catches it,
30 of 30: the pick asking the in-sight flag, "neither" asking allies first,
set_target always keeping the sides, none not clearing the last ally, an
ally taken as an enemy first, the enemy keys taking the side, the enemy
keys going back to the last enemy for side 1, the last pick taken when it
is the current, a contact matched by its entity alone, the previous key's
wrap taking the current, the next enemy without the wrap, the wide range
in a campaign, the cap at 2000, the radar flag in bit 0, the animation's
health and maximum swapped, the triggers lowest first, a threshold equal
to the health not firing, a hit to exactly 0 not destroying, the health
left inverted, the kill leaving the entity active, the animation's
handlers taken without the collide check, inherited handlers accepted,
the height along x, no target without destroy_anim, the record's team
ignored, the markers out of order, the destroy animation run twice, the
target subnode moved back by translate_to_anim_root, the old handlers
removed by the weapon slot only, the description's default health 50.

Left: the sensors and the entity sort are section 15's (the turrets' pick
0x4d72b0 is ported with the turrets: vehicles.md, The turrets' frame),
zEntity3d itself, the brain's use of the pick.

### 14a The AI on the targets.zrd objects (2026-10-22)

The sensors already listed targets.zrd objects in every vehicle's contacts;
the program's brain hooks took every contact's entity to be a mech, so the
AI never fired at them. The exe's two questions, kept apart:

- **A contact's entity** (0x4e6fe0: the seen object's vt+0x2c): every
  entity answers itself, the mechs, the targets (0x4d2a70) and the command
  marker. The firing's volley pick, its track and its line check, the
  orders (attack_target, ally_of) ask this.
- **A contact's vehicle** (0x4e6ff0: then vt+0x40): a mech or vehicle
  answers itself, a target and the plain AiTarget 0 (0x40bae0). The
  brain's engaging (walking at an enemy) and the asleep attacker's range
  ask this, so nobody walks at a building; the firing still shoots it.
- The hooks a target meets: its AiTarget, its node's place, its lock
  position (the record's "target" subnode, else the node), its centroid
  (the "centroid" subnode), its +0x08, still, never powered down (vt+0xa0,
  0x40bae0), its health the fraction (vt+0x98, 0x4d2b20); 0x4b6e30's
  aim-at-last-hit only on a mech (the entity's vt+0x50, a target's 0).
- **zEntity3d's node bind** (0x40eaa0 / 0x40ed00, ported for the targets
  as `zcls::node_tree_bind` / `node_tree_unbind`, 0x40ea30 / 0x40ec20):
  a target's node tree takes kind bit 21 (0x200000, 0x40ee10; a trap's bit
  22, 0x40ee70) and the target as owner, so a node a shot or a pick hits
  leads back to it (0x40e920); unbound only where the owner is still it.

Test `mission_c1m4_dominic_fires_on_a_target_headless`: Dominic woken 80 m
off the hut ghsD2 (team 2), the enemy mechs taken 3 km away: his contacts
list the huts, his firing takes ghsD2 (703 frames on it), aims 47 frames
and fires 654, and the hut goes from 30 health to 0 (exact, the fixed
seed). Its checks fail with a target's entity answered as before.

## 15. The sensors and the entity sort

Ported 2026-10-20: `mech3/sensors` (and MSVC's sort as a template,
`crt/msvc_sort.h`); test `tests/differential/test_sensors.py` (probe
`tests/probe_sensors.cpp`); in the program `mech3/app_sensors`. The module
is 0x4a7c70..0x4ab4c0, between turret.cpp and veh_basic.cpp (its file name
is not known). app_hooks.h called 0x4a8cd0 `effects_update`: it is the
sensors (now `sensors_update`), and 0x4d5ca0 `ai_start` the entity sort
(now `entities_sort`).

**The frame** 0x4a8cd0 runs from the Core frame's world tick (game-loop.md:
after the scene and the mech's AI tick, before the missions' frame). The
player P is [0x60861c]+0x14, the view's vehicle V [0x5f4c8c]+0x64, the
stealth entity S [0x609a2c]; the entities are zEntity's list
[0x73655c..0x736560] (sorted at the mission start, below), the targets
targets.zrd's [0x60b584..0x60b588].

1. **A record per vehicle** (0x44 bytes, 0x4a7fd0; the set-up 0x4a8710,
   0x4a83c0): P's first (P's vehicle's vt+0xcc is asked and not used), then
   V's if V is neither P nor S, then every other entity whose vehicle
   (vt+0x2c) is alive (vt+0xcc); S and the dead ones have their state
   cleared (below, the target through vt+0xc0(0, 0)) and +0x09 cleared.
   Each record keeps the vehicle each of the four picks (+0x6c..+0x78)
   and the tracked contacts (+0x88) saw, and the flags: bit 0 passive radar
   (+0xbc bit 2), 1 shut down (vt+0xa0 == 1: the mech's power-down pending or
   done, 0x41cc10), and with a mech (vt+0x50): 2 BAP, 3 C3, 4 C3 slave, 6
   ECM (equipment kinds 4, 6, 7, 8 above 0), 7 a lancemate under orders
   (+0xbc bit 4). On anyone but the player C3 and C3 slave become bit 4
   when he is an ally of the player, else nothing. Then the vehicle's state
   is cleared: +0x94 and +0x7c deleted, the picks, +0x80, +0x98, +0xb8
   zeroed, every contact deleted (0x4a7be0), +0xbc bits 0-1 off, vt+0xc8
   (the mech's brain told, 0x41e810); +0x09 set. zEntity's 0x5a85c0 runs
   first (vt+0x1c when the node has parents).
2. **A record per active target** (+0x08; 0x4a8530) likewise, with no
   vt+0xc8.
3. **The player with everyone** (0x4a9680, the first record with each other
   and each target): each senses the other within its own range (below);
   the other's contact on the player goes to its +0x98 and +0x80, made
   anyway (and owned at +0x94) when the player is beyond its range.
4. **The lancemates under orders** (the records after the first while bit 7,
   0x4a9a60) with every later record and every target: each senses the
   other; the other's +0x80 becomes the nearer of what it held and this
   lancemate (owned at +0x7c, reused, when beyond its range).
5. **Every other pair** (0x4a92c0) of the rest, each with every target too:
   each senses the other within its own range. Targets never sense targets.
6. **C3** (0x4a9fd0, when there is a master): the master's list becomes the
   union, by what each contact saw (pointer order, 0x4ab1f0's sort first),
   of its own and each C3 member's that is an ally of the master; the
   member's contacts become new ones from the master's position, in sight
   (0x4aa270), those on the master dropped; replaced only when the union
   is longer. [0x609a7c] counts the merges; nothing reads it.
7. **Each vehicle's list finished** (0x4a9e70): sorted nearest first
   (MSVC's sort, 0x4ab500 / 0x4ab620, by x87 "below"), the nearest within
   8 m at +0xb8, the last ally, enemy and saved picks set to their new
   contacts (none if not sensed), the target re-selected (vt+0xc0(c, 1))
   when sensed again; a target lost gives way to the nearest ally in sight
   (0x4d6970) if it was the last ally, else the nearest non-ally in sight
   (0x4d6a80). vt+0xd0 false clears +0x09 (the mech's: the level of detail by
   +0x98's distance, 0x41e870). Targets' lists are not sorted.
8. **The view's ping** (0x4aa400): V's contacts nearest first, up to the
   first beyond its range: the first whose vehicle's vt+0x40 has a vt+0xec
   object plays it (0x4adce0 on its +0xe8) at that vehicle's node.

**Sensing** (written out in 0x4a9910, 0x4a9980, 0x4a9cf0, 0x4a92c0 and the
two kept contacts): within range is the squared distance below the
observer's +0xa4 (NaN counts), or [0x60b570] (only ever cleared: a debug
"see everything" switch). The contact (0x4a9530) goes in the observer's
list; it is in sight unless [0x60b570] is off and: the target's vehicle
hides (vt+0xb4: a target without `radar` in targets.zrd, 0x4d3380; never a
mech), or the observer is shut down, or neither (the observer has an
active (not passive) BAP and the target no ECM) nor (the target is not shut
down and not both are passive). The target's +0xbc: bit 3 when the player
sees it, bit 1 when an enemy sees it (in sight), bit 0 when it was the
observer's target. Offsets point from the observer to the target; the
distance is (x² + y²) + z².

**The entity sort** 0x4d5ca0 runs in the Core state's resume(7) (the
mission start, after the fade) while [0x60b56c] is 0 (every AiTarget made
or re-teamed clears it): every vehicle's lance flag (0x4d5be0: the brain's
+0xcd: bit 1 "default" an ally of the player that is not him, else bit 0;
no brain none), then MSVC's sort (0x4d7360, the comparator 0x4d7650 and its
inline copy in 0x4d7bc0): the player first, the stealth entity last, then
entities with a vehicle before those without, allies of the player before
the rest, lancemates before other allies, then by mech, by brain (none
first), by pilot: Dominic Paine, Epona Rhi, Alan Mattila (messages 0xe14,
0xe15, 0xe01), the rest; ties by address (the vehicle's between non-lance
allies, else the entity's). [0x60b550] is the second entity, [0x60b548]
the first after it without a mech allied to the player; [0x60b56c] = 1.

**The allies turn** 0x4d5dc0, from the AI controls' damage handler
0x4ea520 when the player has kept hitting an ally for over 3 s (0x4aec10
keeps the last hit's time, its attacker and when that attacker's run of
hits began, a run broken by a gap over 3 s or another attacker; 0x4aec70
the run's length): every entity in [0x60b550, 0x60b548), every allied
mech, takes the player's team as an enemy and drops it from its allies,
gaining bit 31; the player's enemies lose bit 31. Ported, not wired (the
handler is the AI's).

**Quirks**: the radar mode key (RADAR_RANGE, Alt+R, the mech's 0x41bfc6)
cycles the sensor range 1000, then 500 passive (+0xbc bit 2) once at the
top (2000 with C3), 500 active, 1000...; in the campaign the range is
capped at 1000 anyway (0x4d6d20), so C3's 2000 never reaches the sensors.
Passive radar only stops your BAP and hides you from other passive radars:
the AI is never passive. Only the player leads a C3 network: an AI's C3
does nothing for its side. Powered-down mechs are sensed but not in sight,
except by an active BAP when they carry no ECM. Targets without `radar`
are never in sight. The sort's last tie compares addresses.

**Fixes** (fixes.md): the C3 union in the scratch [0x609a5c] (one slot per
entity, at least 32) can outgrow it, targets counting too, and write past
the heap block: the port's grows. A contact on something without a vehicle
is not written through (the exe would crash; never in play). A lancemate
whose brain has no pilot name ranks last instead of crashing the sort.

**The tests** (`test_sensors.py`, x87 at 24 bits). Part 1, 1500 frames:
crafted worlds of 1 to 26 vehicles and 0 to 5 targets (entities their own
vehicle or another's, alive or not, with mechs carrying BAP, C3, C3 slave,
ECM, shut down, passive, lancemates, hidden, teams of one or two bits,
ranges 150 to 50000, pairs at exactly a range, stale nodes, old contacts
behind every pick and the tracked ones, P, V and S anywhere, [0x60b570]
on); every call out by object (vt+0xc0 recorded outside AiTarget's own
pickers, the node refreshes, the ping's position) and every vehicle's and
target's picks, kept contacts, flags, masks and list, and every +0x09:
identical, every listed branch reached. Part 2, 500 sorts of 0 to 40
entities (own entities made out of order, mechs their own, lancemate
settings, pilot names), with the allies turning: the calls, the order, the
range and the flags identical. Breaking the port showed the test catches
it, 33 of 33: BAP through ECM, passive hiding from all, a shut-down observer
seeing, the range inclusive, the offset reversed, the sum's order, the
player's mark without sight, an ally's mark for an enemy's, the targeted
mark missing, 10 m touching, the lost ally falling to an enemy, the target
re-set without keep, vt+0xd0 skipped, the target dropped instead of
forgotten, targets told of the clear, no lancemates, the farther lancemate
kept, no lance contact on the player, the C3 tie taking the member's, the
C3 ally test missing, others leading C3, ECM from the slave count, the ping
on every contact, the lance tie by entity, Rhi before Paine, the allies'
range without the team test, the player his own lancemate, turned allies
keeping the team, the quicksort down to 8, shut down on any answer, the
saved pick lost, tracked contacts not followed, C3 copying the master's own.

**In the program**: zEntity's list is the port's (`ai::entity_list()`),
filled by the mech build's 0x5a7f50 (app_mech.cpp) and cleared at each
mission start; the sensors' hooks answer as the Mech's and CZAiTarget's
vtables do (`app_sensors.cpp`), positions through zClass's node_to_world;
the targets' centroid and target nodes are their record's named subnodes
or the node (0x5a8390 stands in); the Core state's mission-start fade
(0x48eea0, game-loop.md) runs for its 2 s, then resume(7) sorts. `mw3tests` `mission_c1m1_sensors_fill_the_contacts_headless`: the
Mad Cat and the Owens on the list, the player first; the player senses the
Owens, not in sight while powered down, the Owens senses the player;
after bramp wakes it, it is in sight, Ctrl+E selects it and the next
frame's sensors keep it selected. (The satellite dish, 2 km out and without
`radar`, is beyond the 1000 m. Placed by 0x4161c0, the Mad Cat starts
1433 m from the Owens and does not sense it; the test moves it 300 m short
with SetPlace first.)

Left (the turrets' pick 0x4d72b0 is ported with the turret, step 4c2:
vehicles.md, The turrets' frame): the AI's friendly-fire handler that
turns the allies, the mech's vt slots the sensors call (the HUD's lock
0x41d890, the level of detail 0x41e870, the brain's refresh 0x41e810),
zEntity3d.

### The forget (0x4aa6b0, 0x4aa4d0, 0x4aeec0; ported 2026-10-24)

When an entity goes for good (a wreck deleted, 0x41e140), 0x4aa6b0 releases
its animations (0x4d5100, section 18), then asks
every entity on zEntity's list for its vehicle (vt+0x2c) and that vehicle's
vt+0xd4 with the one going. A mech's, a basic vehicle's and a field base's
vt+0xd4 is 0x4aeec0: its weapons forget the locks on it (0x4b0370: the lock
flag, position and velocity cleared, the entity word left), its controls'
vt+0x60 (the player's 0x4bf620 and the AI's 0x4ea700, both empty), then
AiTarget's 0x4aa4d0; a turret's, a target's and the command marker's is
0x4aa4d0 alone. 0x4aa4d0 on vehicle V and the one going X: X itself, every
pick, kept contact and contact cleared (the owned ones deleted); else each
pick or kept contact whose vehicle (0x4e6fe0) is X dropped (the player's and
the lance's owned ones deleted, the target through vt+0xc0(0, 0)), the
touching contact cleared whoever it is on (as written), the contacts on X
or null taken out (0x4aa470; unfreed in the exe, deleted here: FIX (bug)).
The targets.zrd objects are not on zEntity's list, so they are not asked:
their contacts on X go with the next sensors frame. The port adds the
attackers' FIX (bug) to the mech's forget (fixes.md). `sensors.cpp`
`entities_forget` / `forget_entity`; the program's per class in
app_sensors (`program_mech_forget`, `program_vehicle_forget`). Checked:
`test_sensors.py` part 3 (300 forgets after a sensors frame: a vehicle, an
entity, a target, the player or a stranger going; half the vehicles with
0x4aeec0, weapons and controls stubbed; the calls and every list compared)
and `mission_c1m4_a_wreck_is_deleted_and_forgotten_headless` (an enemy
targeted by Dominic and the player, the player's weapon locked on it,
Dominic's last attacker: killed, then deleted as instant action does, and
nothing in any vehicle, target or weapon still holds it; nor the program's
own lists: its name finds nothing through the valves' lookups, its old
handle answers null, FIX (A) on our handle table). 14 of 15 breaks
(the miss not observable: no list holds a null contact after a sensors
frame, and a contact at distance 0 needs two vehicles in one place).

### Radar contacts (the HUD's, by class; ported 2026-10-25, item 3)

The HUD's radar (0x43dd00's contact loop, hud.md section 15) and TARGET
(hud.md section 14) ask a contact (here: every vehicle's contact list) for
its object and that object's slots. In the exe every class answers through
its own vtable (found by their vt+0xd4); `mech3/entity_classes` is the
port's table of them, each answer the slot's function as written:

| Class (vtable) | vt+0x3c blip | vt+0xa4 name | vt+0xa8 hidden | vt+0x50 mech | vt+0xbc centre |
|---|---|---|---|---|---|
| mech 0x5bcef8 | 0x41ec60 [0x605618] | 0x41df30 (the player's pilot's, else 0x4aee70, else "") | 0 | itself | 0x5a85e0 |
| network mech 0x5c3508 | 0x41ec60 | 0x4709c0 (not ported; no network) | 0 | itself | 0x5a85e0 |
| vehicles 0x5c88e8 (the basic), 0x5c8460 (the stealth) | 0x45b970 [0x60b120] | 0x4aee70 | 0 | none | 0x5a85e0 |
| field base 0x5c23d8 (0x4589d0's) | 0x45b970 [0x60b120] | 0x4aee70 | 0 | none | 0x5a85e0 |
| the vehicles' base class 0x5c8d48 (0x4aeb00; no instance: every vehicle's constructor puts its own; the port's `vehicle_base`) | none (0x410510) | 0x4aee70 | 0 | none | 0x5a85e0 |
| turret 0x5c8770 | 0x4a5fc0 [0x609a38] | none | 0 | none | 0x5a85e0 |
| target 0x5ccb90, trap 0x5ccc80 | 0x4d2d70 [0x60b4f0] | +0xec (vt+0xc4) | not +0xe8 bit 0 (0x4d3350) | none | 0x4d84f0 (the centroid node's) |
| command marker 0x5cced8 | none | none | 0 | none | 0x5a85e0 |
| plain AiTarget 0x5ccfe0 | none | none | 0 | none | 0x4d84f0 |

Each blip table is ally, enemy, other, picked by the object's team
(vt+0x28, itself, +0xc0) against the mech's allies and enemies. The
contact's object (0x4e6fe0) is its seen object's vt+0x2c, itself in every
class (with none the exe returns what EAX held; the port none); its size
(0x4e7070) the object's +0x68, 5 with none; the lead point's place
0x5a8630 (the target node, +0x10) and velocity vt+0xc (a mech's 0x41d480,
the rest zEntity's 0x5a82d0, +0x30). The tables are made by the mechs'
HUD (0x413e60), the stealth object's constructor (vehicles.md), the turrets'
load (0x4a76b0) and the targets' (0x4d2e20); the command marker never shows
on the radar. Until 2026-10-30 the port had the field base under the base
class's vtable (0x5c8d48) and drew it no blip and no TARGET tint; the field
base's own (0x5c23d8, stored by 0x4589d0 at 0x458adc) differs from the basic
vehicles' only in its constructor, destructor, vt+0x34, +0x70, +0xf0/+0xf4
(itself) and the field base's slots from +0x158 (playtest W12).

What reaches the radar: a contact on it (in sight, +0x2c bit 0) or TARGET's
own. A targets.zrd object without `radar` is never in sight (0x4d3380,
section 15), so never on the radar: only t1 m2 and m3 (training) have
targets with `radar` (four each); c1m1's satdish is sensed, never shown.

**In the program** (app_sensors.cpp's ProgramEntityFacts and hud_object_*,
app_hud.cpp's contact hooks): the classes from the program's objects (the
mechs, the basic vehicles and field bases, turrets, targets and traps, the
command marker; no network mechs). The targets' markers are made by their
hooks now (app_mission.cpp), the vehicles' at the stealth step. The HUD's
report counts the blips by their images (mechs, vehicles, turrets,
targets).

**Tests.** `tests/differential/test_entity_classes.py` (probe
`tests/probe_entity_classes.cpp`): 300 cases, objects with each of the 12
vtables and random fields (team, height, a target's flags, name and
centroid node with or without a refresh), the four blip tables (some
entries 0), the pilot on one of the mechs or none; the original runs each
object's own slots (vt+0x3c for each kind, vt+0x28, vt+0x50, vt+0xa8,
vt+0xa4, vt+0xbc, 0x4e6fe0, 0x4e7070 on a contact, and with none) against
entity_classes: 3600 objects, 36000 answers and the calls identical.
Breaks caught: the vehicles reading the turrets' table, the field base
given the vehicles', the +0xe8 bit inverted, a name for the turret and
marker, the targets' centre from 0x5a85e0, vt+0x50 answering for every
class, the player's name not tested, the size's default 0, targets and
traps without blips: 9 of 9. `test_hud.py` now gives some of the radar's
contacts (60%) their own object with a real class vtable and the fields its
slots read, and TARGET's object (half the cases) a target's, trap's, field
base's, marker's or plain one's (their view tint vt+0x68 empty), the blip
tables filled: the exe's contact loop and TARGET over a mixed list against
the port (probe_hud.cpp answers the classed objects through
entity_classes); every class's slot function reached. Six of the breaks
rerun against it: 6 of 6 caught. In the program,
`mission_c1m1_radar_shows_turrets_and_vehicles_headless` (by a turret 4
turret blips, by a vehicle 4 vehicle blips; the satdish a contact, not in
sight, no blip) and `mission_t1m2_radar_shows_its_targets_headless` (the
four targets in sight, 4 target blips).

## 16. The AI mechs' frames (in the program)

Nothing in the AI calls a vehicle's frame. The player tick 0x4a1160 updates
the pilot's vehicle itself, then calls 0x4a1140, which, while [0x5e7284] is
set (initialised 1, never written), runs the entity manager's walk 0x5a8850
on zEntity's list (the manager at 0x736558: the list +0x04..+0x08, resume
+0x10, current +0x14, left +0x18, per_frame +0x1c, which 0x5a8750 sets to
0x40). The walk starts where the last frame's counted update left off,
runs to the end and round from the start, and calls vt+0x34 on each entity
whose +0x09 is set. The sensors (section 15) clear +0x09 on the pilot's
vehicle each frame, so it is not updated twice. Mech::Update's budget
(0x5a8910 / 0x5a8960) counts the full updates: past per_frame, an entity
only gathers its frame time (+0x6b0) for later. The walk then moves the
next frame's start one entity on.

Ported as `zent::Budget::update_all` (`gamez/zentity/zent_manager.h`;
`test_entity_walk.py`: 5,000 walks of up to 40 entities, live or not,
counting or not, identical to the original in the order, the allowed flags
and the budget after; 4 of 4 breaks caught). The program's
`PlayerHooks::entities_update` (`update_entities`, `app_mech.cpp`) runs it
on the program's list, which holds the mechs only: targets.zrd's objects
are not registered there (0x5a7e10 is not ported), so their vt+0x34 does
not run.

An AI mech's frame is Mech::Update with its AiControls: 0x4e96f0 runs the
brain 0x4e9710, not ported. In its place the program records "0x004e9710"
and runs the controls' smoothing (0x426960) on the targets as they are, so
an AI mech stands where it was put, powers up and down, settles on its skis,
keeps its pose, fires nothing and turns to nothing. Its weapons' manager
updates inside Mech::Update (0x4b0c20 / 0x4b0c30).

The mechs collide as mechs: the collider's lookups 0x40ef10 (0x40e9d0, the
node's owner by its +0x28 id) and 0x40ef00 (0x40e960) put another mech's
nodes in the mechs list (`mech_response` 0x423a20: shove, damage unless
allied, blocked[1]) instead of the walls; the bump reads the sensors'
contacts (`Contact::distance` 0x4e7030; a mech's vt+0x2c, +0x40 and +0x50
answer itself, 0x4d2a70; targets.zrd's vt+0x40 answers 0, 0x40bae0).

Test: `mission_c1m1_owens_runs_its_frames_headless`. The Owens is powered
down until bramp, which powers it up (Mech::power(0) is immediate). It runs
a full update every frame from then on, stands on the ground with both feet
down, and blocks the player walking into it from 30 m as a mech, not as a
wall. The bump keeps the player more than 3 m off, and the Owens records the
player as its attacker. Breaking the walk, `node_mech` or the contacts each
fails it.

Left as hooks, for the next items: the brain's decisions 0x4e9710 (the
orders, behaviours, targeting and the firing state machine 0x4e9fb0 ->
0x4b5b80, the planner 0x4b1170); AiControls' vt+0x140 +0x70 (the wall
response's reversal count, `CollideCalls::ai_reverses`); the vehicles that
are not mechs (their frames, the Mobile HQ's bump); targets.zrd's entity
registration 0x5a7e10.

## 17. The brain's decisions (map, not ported)

Mapped 2026-10-20 from the disassembly (the decompiler loses `this` and
the switch fall-throughs here; read `mech3.asm`). "Likely" marks a reading
no string confirms. Offsets are the brain's (section 12) unless said.

### 17.1 The frame (0x4e96f0 -> 0x4e9710)

The AI controls' update (IControls vt+0x0c, 0x4e96f0) returns the brain's
answer. The brain's own vtable (0x5cc698; the mech controls' copy 0x5cd2c8,
the vehicle's 0x5cd6e8) has 19 slots: +0x04 the controls' own update
(the mech's 0x4d89f0 -> 0x426960, the smoothing: the one the program runs
today), +0x0c 0x4e9640 (+0x54 is 0), +0x10 0x4e9650 (sees its target,
the +0x88 / +0x8c choice below), +0x14 0x4e95a0, +0x18 / +0x1c the firing
state's (the mech's 0x4b6370 swaps +0x138, 0x4b6350 returns 0), +0x20 to
+0x48 the lancemate orders (17.6).

```
0x4e9710 brain(dt): answer = 1
  [0x60b938] == 0 (the AI switch, 17.8): controls stop (IControls vt+0x14), vt+0x04(dt), done
  owner = the vehicle (IControls +0x10); down = owner vt+0xa0 == 1 (powered down)
  steering +0x250: 0x4cec80(dt, owner)                                  (17.4)
  down and not +0x2a4: stop, vt+0x04(dt), done
  +0x22c = 2 when +0xcc bit 2 is set, else 0 (the hurt test, 17.3)
  +0x5c and +0xdc < now: +0x3c = 1 (engaging allowed from now), +0xdc = FLT_MAX
  awake = true
  +0x88 == 0: target = the nearest enemy contact 0x4d6b90(1) when +0x8c == 0,
              else the owner's +0x98 (its attacker's contact, section 15)
              with a target: awake = its distance (+0x0c) < the sight range +0x1c (no target: awake)
  awake:
    [0x6099c0] += 1 (a count of awake pilots this frame; Mech::Update counts too)
    a radio, a weapon manager (owner +0xd4) and its ammo value 0x4b4ed0 under 0.25:
      the radio's "low_ammo" line (radio +0x24, 0x4e6d50(1.0))         (17.6)
    the vlist triggers 0x4e9550 -> 0x4d9290 -> 0x4d8e70 each            (17.5)
    +0xf0 == 0, +0x9c, the timer +0x100 < now, IControls vt+0xa0 and the
      current order's state vt+0x08: +0x100 = now + +0xb0, the firing state
      reset (0x4e9590 -> 0x4b6700), vt+0x1c(IControls +0x0c), 0x4e96b0
    the current order's net may engage (net vt+0x54) and its state (vt+0x04),
      and 0x4e8f10 allows it (17.3):
        target = the owner's +0x6c, else the nearest enemy only when none was picked yet
        this frame (one the sight check picked is not used); its vehicle (contact
        +0x04 vt+0x2c, vt+0x40) and distance; under the attack range +0x20:
          the target vehicle's own net (vt+0xd8) and its attack behaviour
          (0x4c0e10): attack (0x4e9ca0), +0x230 = owner vt+0x98 - +0xb4,
          +0xe8 = now + +0x4c x GameIntensity [0x608e54], 0x4e87c0(net, beh)
    not attacking: +0xf0 == 0, +0xa0 and state vt+0x08: a random wait
      (+0xf4: now + rand in [+0x64, +0x68]); due: 0x4e96b0, +0xf4 = FLT_MAX
    the current order's run (vt+0x04) until it stops asking again (+0x104)
    not down, [0x5e9d40] == 1 (always) and +0x80 == 1: the brain's own order +0x194 runs
    vt+0x04(dt); not down: the firing state 0x4e9fb0 -> 0x4b5b80(dt)  (17.7)
  not awake: the owner's attacker within its vehicle's +0xa4: stop, vt+0x04; else answer 0
  +0x22c settled (not 2): +0xcc bit 2 = +0x22c != 0
```

0x4e96b0 (the manoeuvre): by +0xa4 (p8 17), 0 pushes the behaviour at
0x604098, 1 the one at 0x604088 (both class 0x4ca440, 17.2), other values
nothing; either resets +0xf4.

**Ported** (2026-10-20, `mech3/zai_brain` `Brain::frame`, `AiControls::update`
0x4e96f0, `MechControls::stop` 0x428490, `Mech::health` 0x41ba40 in
`mech_damage.cpp`). The brain asks its controls (`BrainControls`: the
vehicle, the stop, the smoothing, "hit lately" IControls vt+0xa0) and
`BrainHooks` for the rest; what later items port is a hook each, recorded
in the program until then: steering 0x4cec80 (item 6), the vlist triggers
0x4e9550 (5), the radio's lines 0x4e6d50 (14), the firing's reset 0x4e9590
and run 0x4e9fb0 (12), the target vehicle's own net vt+0xd8, its attack
behaviour 0x4c0e10, the attack 0x4e9ca0 and the brain's own order 0x4e81a0
(10), and "hit lately" (IControls +0x04, a time the hit code 0x4aec90 sets,
not ported: the program answers no). The brain's vt+0x1c call (the
firing's, with IControls +0x0c) does nothing for the mechs' and the
vehicles' brains (0x4b6350 / 0x58b380) and is left out. Two edges: a NaN
attack range with no target vehicle calls through null in the exe (FIX
(bug): no attack); the attack does not reuse the enemy the sight check
picked (as written, fixes.md candidates).

The AI switch [0x60b938] is `ai::ai_switch()` (0x4ea740 on from the Core
state's start, `Engine::ai_on`; 0x4ea750 off in the cleanup). In the
program (`app_mission.cpp` WiredBrainHooks, `app_mech.cpp` helpers) the
time, health, position, AiTarget, power-down, contacts' vehicles, ranges,
the awake count, GameIntensity and the ammo value 0x4b4ed0 are the ports'.

Test `test_ai_frame.py` (probe `tests/probe_ai_orders.cpp`, over
test_ai_gates.py's brain, fakes and vehicle): the brain's own vtable, its
IControls, its own order, three contacts with what they see and two
vehicles, the weapon manager's ammo value as an x87 `fld` thunk, the later
items' functions as stubs; 200 cases of 60 frames with random settings,
timers, contacts, the switch and the time at 24-bit x87: 12,000 frames,
the answers, the awake count, the ten fields the frame writes, 111,211
calls and the stack identical, all 18 marked branches reached.
Breaking the port showed it catches it, 20 of 20: steering with the AI off,
the power-down ignoring +0x2a4, the engaging delay kept, a NaN distance
blind, the player contact for the pick, the asleep pilot never stopped,
the two ammo lines swapped, the firing reset without the hit, its timer
from +0xb4, no manoeuvre when hit, the picked enemy reused, the attack
range from +0x24, the attack's end without the intensity, the give-up
health without +0xb4, the wait's bounds swapped, the wait kept after an
attack, the order run once, the own order on +0x80 0, the firing when
powered down, the hurt bit kept. `test_mech_health.py` (over test_damage.py's mechs):
400 mechs, 6847 health checks (hurt, dead, invincible, cached) bit for bit;
5 of 5 breaks caught (the cache ignored, the arms weighed 0.5, the side
torsos read as the centre, dead as whole, the flag left set). In the program, `mw3tests`
`mission_c1m1_owens_runs_its_frames_headless`: after bramp the Owens'
brain runs its frame every tick, awake (the vlist, the hit test, its
order's state, its own order and the firing each frame), its orders never
empty, the AI switch on in the mission and off after the cleanup.

### 17.2 The orders and the behaviours

**The order stack** is the deque at +0x160 (MSVC: +0x164 the start
iterator, +0x174 the finish {first, last, cur, map}, +0x184 the map,
+0x188 its size, +0x18c the count). The last order is the current one
(0x4ea760). An order (0x98 bytes) holds a net part at +0x0c (made in
place at +0x30 by the net's vt+0x5c) and a behaviour's state at +0x10 (in
place at +0x54 by the behaviour's vt+0x8c); its run (order vt+0x04,
0x4e8150) is the net part's vt+0x04, then the state's vt+0x14. Its
vt+0x08 (0x4e8170) pops it through its owner.

| Address | What |
|---|---|
| 0x4e80b0 | an order's parts remade: the old state and net part destroyed, then the net's (or the built-in 0x604008's) part and the behaviour's (or the default 0x604068's) state |
| 0x4e8120 | the order's state replaced by a behaviour's |
| 0x4e82e0 | the current order remade on a net (0x4e80b0(net, none)) |
| 0x4e86a0 | push on a net: the current state's vt+0x0c (suspend, likely), a new order (0x4e9d80, the last one's +0x14..+0x2f copied), 0x4e80b0(net, none); 1 |
| 0x4e87c0 | push a net and a behaviour (either may be none: the built-in net, the default behaviour) |
| 0x4e88c0 | push a behaviour on the current order's net (net part vt+0x08) |
| 0x4e8380 | pop: the order destroyed in place, its block freed as it empties, then the new current state's vt+0x10 (resume) |
| 0x4e84a0 / 0x4e8660 | pop when the current order's net is the one given (0x4e8660 casts it, vt+0x34) |
| 0x4e8960 | push on a net given as a znet object (vt+0x34) |
| 0x4e8990 | pop down to an order (exclusive of the last), resuming each new current one |
| 0x4e8c60 | the topmost order without +0x04 set, from the current down |
| 0x4e9100 | (IControls side) when any order is on a given net, pop every order (the stack is left empty) |
| 0x4e9070 | the current state's vt+0x08 (may it fire, likely) |
| 0x4ea4e0 | brain vt+0x40: 0x4e8c60 then 0x4e8990 to it (back to the last order not marked) |
| 0x4d88a0 / 0x4d88d0 | (the AI controls) is the order under the marked ones on the net 0x603fb8; push on it |

Popping the last order leaves the deque empty and 0x4e8380 still resumes
"the current state" there, reading through a null block (a crash); no
caller seen pops the last one (0x4e8990 keeps one), so it is left as the
exe has it (decided with the lead, 2026-10-20): the port resumes nothing
there and the test never does it. Two more edges: with every order marked
0x4e8c60 answers the slot before the first (no order; 0x4e8990 then pops
down to one), and 0x4e8990 given 0 with one order left pops it (no caller
does).

**Ported** (2026-10-20, `mech3/zai_brain`): every function of the table but
0x4d88a0 / 0x4d88d0 (they need the net 0x603fb8, item 10). An order owns
its net part (`NetPart`, made by `AiNet::make_part`, vt+0x5c; the compiled
and built-in nets' `NetLink`) and its state (`BehaviourState`, made by
`Behaviour::make_state`, vt+0x8c; the base state for every behaviour whose
own state is not ported yet, `DefaultState` for 0x604068, `SharedState` for
0x60b460). Their runs are recorded hooks until the behaviours land (items
7-11). The deque is the C++ library's: its blocks and map are not the
exe's. FIX (bug): 0x4e9100's search for a net no order is on walks past a
full last block into the map (the push takes its next block only when it
needs one), a crash; the port answers no.

Test `test_ai_orders.py` (probe `tests/probe_ai_orders.cpp`): fake nets
and behaviours on both sides whose parts and states are numbered and
report every call to one trace; 300 random runs from an empty brain
(65,034 operations, stacks up to 60 orders, past two blocks and the map's
growth): after each operation the trace, the answer and the whole stack
(marks, owners, parts and states by number with their net and behaviour,
+0x14..+0x2f) identical, 144,021 calls. One run reached the exe's crash in
0x4e9100 (counted, not made). Breaking the port showed the test
catches it, 17 of 17: the state made before the part, the part destroyed
before the state, no suspend on a push, the fields copied from the first
order, no resume after a pop, pop_if on any net, the unmarked search from
the first, the unwind keeping its order, the unwind popping the last, the
clear's search on the current order only, the clear keeping the first, may
fire as may break in, the state run before the part, push_behaviour on the
built-in net, remake keeping the behaviour, the stop doing nothing with
every order marked, the default behaviour swapped for the shared one.

**Behaviours** are the objects a net is made of (section 9: node steps,
property steps) plus a few shared ones. Each has the 40-slot vtable of the
base 0x4c8600 (0x5cb180): +0x54 may engage from here, +0x5c a net part,
+0x74 itself as a behaviour, **+0x8c its state**, +0xac may a pilot pass.
**States** (over 0x4c8660, vtable slots): +0x00 destroy, +0x04 may the
brain break in for an attack, +0x08 may it fire (or reset its firing),
+0x0c suspend, +0x10 resume, +0x14 run(dt), +0x18 its behaviour.

| State (ctor, vtable) | Behaviour | Run (+0x14) | Notes |
|---|---|---|---|
| 0x4c8660, 0x5cb238 | the base 0x5cb180, the step base 0x5cb3d0, 0x5c3828 | 0x4c8690: stop, IControls vt+0x10(0) | |
| 0x4c8700, 0x5cb258 | the null step 0x603fa0 (0x5bc8d0) | 0x4c8690 | |
| 0x4c8790, 0x5cb330 | the default 0x604068 (0x5cb278) | 0x4c87f0: stop; the nearest net node to the vehicle (0x4d51d0), its node step's state in place of this one (0x4e8120), run again (+0x104) | no breaking in, fires |
| 0x4c88c0, 0x5cb350 | 0x5bca40 | nothing | |
| 0x4c8940, 0x5cb370 | 0x5bc988 | 0x4c89a0 | |
| 0x4c8a30, 0x5cb390 | made by 0x4c9750, 0x4cc290, 0x4d1320, 0x4d1650, 0x4d1870, 0x4d1cc0 | 0x4c8690 | a wait (likely) |
| 0x4c9ef0 / 0x4c9fc0 / 0x4ca0e0 / 0x4ca290 | speed, wait_interval, wait_time, valve | 0x4c9f40 / 0x4ca070 / 0x4ca190 / 0x4ca2e0 | the link's properties; speed and valve ported (17.4a); the waits ported (item 3, below) |
| 0x4ca3b0, 0x5cb8a0 | 0x5cb7e8 (0x4ca370, at 0x60b450) | 0x4ca420: +0x104 set, pop | resume 0x4ca410: pop; a suspended manoeuvre's; ported (item 3i) |
| 0x4caae0 (0x4ca440), 0x5cb8e8 / 0x5cb908 | 0x604088 / 0x604098 (0x5bcaf8 / 0x5bcbb0) | 0x4ca600 (223 lines) | the combat manoeuvre: lasts now + rand in [+0x6c, +0x70]; suspend 0x4ca5f0; ported (item 3i) |
| 0x4cac20, 0x5cb928 | 0x5cd920 (the attack's first, 0x4e9ca0) | 0x4c8690 | resume 0x4cac80 |
| 0x4cace0, 0x5cba00 | 0x4caca0 (0x5cb948) | 0x4c8690 | resume 0x4cad30 |
| 0x4cadb0, 0x5cbad8 | 0x4cad70 (0x5cba20), given the target | 0x4cae90 (108) | resume 0x4cae10 |
| 0x4cb330, 0x5cbbb0 | jump (0x5cbaf8) | 0x4cb4b0 (170) | ported (item 3d, below) |
| 0x4cbc50, 0x5cbc88 | scan, canleave, canleave-not | 0x4cbca0 | ported (17.4a) |
| 0x4cbe50, 0x5cbed0 | the shared 0x60b460 (0x5cbe18), every first order | 0x4cbeb0: stop; a vehicle without +0xbc bit 4 finds the nearest node (as the default) and pushes its behaviour (0x4e88c0); with it (the player's lancemates, likely) pushes the net 0x603fb8 when the pilot's game's type is 4 (Training), else follows the player's vehicle's net (vt+0xd8, 0x4c0de0) | |
| 0x4ccd00 / 0x4cce70 / 0x4cd640 / 0x4cd720 | mcbwait, sound, gate, shunt | 0x4ccda0 / 0x4ccec0 / 0x4cd690 / 0x4ca2e0 | ported (items 3b, 3c, below) |
| 0x4cd7b0, 0x5cc348 | the brain's step (+0x240, 0x5cc290) | 0x4cd810 (98) | dead: the step is made by the brain's constructor (0x4cd770) and never given to an order (no other reference to +0x240, its vtable or its maker 0x4cd790); its run would turn the pilot on the spot toward the next step's place (0x4cdd40 inline) and hand on as the valve step. Not ported |
| 0x4cdbd0 / 0x4cdca0 | valveunion, valve_assign | 0x4ca2e0 / 0x4cdcf0 | ported (item 3c, below) |
| 0x4ced40, 0x5cc5c0 | the node steps (0x4c9150), backup (0x4cfa80), deploy (0x4d0040) | 0x4cf140 (67): steering to the node, then its links | resume 0x4c9700; ported (17.4a) |
| 0x4d0a10, 0x5cca28 | 0x4d08d0 (0x5cc970, made by 0x4ea260, brain vt+0x3c; its copy at +0x08) | 0x4d0ce0 (157): to the dock point and down while the dock works (vehicles.md, The AI at the dock) | ported (item 4b6, zai_repair) |

**The waits, ported** (2026-10-22, item 3a: `WaitState`). Made by the
wait steps' vt+0x8c (0x4c9fa0, 0x4ca0c0) with the game time ([0x60991c],
+0x0c) and the pilot stopped (brain +0xe0's IControls vt+0x14); each run
hands the order the step's next (+0x0c) through 0x4e8120 once the wait is
over: wait_interval when start + the step's seconds (+0x10) is not at or
past the time (a NaN ends it at once), wait_time once the time is past the
step's value. Neither breaks in nor fires (the base's). Test: in
`test_ai_node.py`'s chains (their values and the clock mostly on its 0.5 s
steps, so the ends are met exactly; now and then a NaN), 9 of 9 breaks.

**Mcbwait, sound and gate, ported** (2026-10-22, item 3b). The mcbwait
node's state (0x4ccce0 -> 0x4ccd00) stops the pilot there (IControls
vt+0x14) and holds the order until the game time is past the node's +0x34
(its link 0 then: vt+0x08(0), +0x60, +0x74) or, not past it (a NaN
included), its valve (+0x2c) holds its value (+0x30: link 1); a link that
is no behaviour leaves it waiting. The node is not walked to: the pilot
waits where it stands. The sound step's (0x4cce50 -> 0x4cce70) plays its
sound (+0x10) at the pilot's place (0x5a85e0, 0x56e7e0 at volume 1) and the
gate step's (0x4cd560 -> 0x4cd640) only hands on; both set the brain's
+0x104 and give the order the next step. FIX (bug): an mcbwait record
without a valve name reads 0, as the deploy node's. Test: in
`test_ai_node.py` (the gate's state real now, the sound a numbered fake,
mcbwait nodes among the nodes with times about the clock and now and then
NaN), 83 branches, 11 of 11 breaks.

**Shunt, valve union, valve_assign, ported** (2026-10-22, item 3c). The
shunt's (0x4cd700 -> 0x4cd720) and the valve union's (0x4cdb60 ->
0x4cdbd0) states run the valve step's 0x4ca2e0 (the way finder's next step,
else the one that follows; +0x104 set). The valve_assign's (0x4cdc80 ->
0x4cdca0, run 0x4cdcf0) first sets its valve to its value (0x4d3c00, the
valve's reactions firing), then does the same. FIX (bug): an assign whose
valve is not found sets nothing (the exe sets through null). Test: in
`test_ai_node.py`'s chains (unions of one or two valve counts, assigns on
the test's valves), 88 branches, 6 of 6 breaks.

**The jump, ported** (2026-10-22, item 3d: `JumpState`, `jump_plan`). The
jump step's state (0x4cb310 -> 0x4cb330) plans the flight once (0x4cb120,
0x4cb250) from the pilot's place to the next step's way point (vt+0x1c):
the jets climb at 0x41d490(1) less gravity (8 a unit of thrust over the
world's gravity [0x5e5d34], 30 unless a script sets it) and fall at
gravity; the step's value is a clearance added to both the climb and the
fall (whichever way the height goes); `k` = 2 / ((fall - climb accel) x
ratio), ratio = climb / fall accel; the burn up sqrt(climb x k), the coast up
-(burn x ratio), the coast down and burn down likewise from the fall; the
speed needed is the flat distance (the CRT's `_hypot`) over their sum
(FLT_MAX when that is not positive), and the pilot's top speed at its share
(vt+0x118) caps it, a slower pilot adding a glide of dist / speed - total.
The phases end at those times summed from now; the glide's jets are
0x41d4b0(gravity) (the throttle the jets need to give gravity's
acceleration, 0). Each run (0x4cb4b0) steers toward the next step's place
while steering is on (0x4ce230, 0x4c8df0 with the node state's turn
settings), sets the jets' forward thrust from the forward speed's error to
the plan's ((|err| - 1) + 0.2, at most 1, against the error's sign:
IControls vt+0x3c), then: the local velocity zeroed and the jets full until
the burn up ends; jets off while coasting up and airborne; the velocity
zeroed and the glide's jets while gliding and airborne; jets off coasting
down; full jets braking, then steering off; the local velocity set to
(0, -2, 0); jets off until the pilot is down (vt+0x144, the Mech's +0x584),
then the next step. A phase passes as soon as its time is up or (but the
first) the pilot is no longer airborne. [0x60b488] (+0x564 set to 1) is
never written: dead. FIX (bug): a jump whose next step has no place (not a
node) steers toward a null pointer in the exe (a crash); it does not turn.
Test: in `test_ai_node.py` (the jump's state real; the CRT's `_hypot` the
root of the summed squares at 24 bits on both sides; the local velocity,
airborne and gravity in the world; jumps landing on nodes; 800 cases so
every phase is met), 115 branches.

**Mode 1, circling, ported** (2026-10-22, item 3f: `CircleState`). The
vehicle net's mode 1 behaviour (slot 2), which the fight hands the order on
arrival, makes 0x4c9730 -> 0x4c9750 (over the waiting state 0x4c8a30):
the flat way from the pilot to the target (normalized, 0x4020d0) as the
radial, its perpendicular (z, 0, -x) as the tangent, the way round 0 and
the speed share the brain's +0x94. Each run (0x4c9880) gives up as the
fight does (the target's place, the pilot's, active, 0x4e8dd0); then, with
the flat way now: the radial error e = (way . radial x dist - R) / R (R the
brain's +0x44); along = way . tangent x dist turns the way round past the
brain's +0x48 (on below -P, off above +P; a NaN turns it on); e's sign is
flipped when the throttle (IControls vt+0x1c) is under 0.1 going round the
other way, or at most -0.1 otherwise; the steering way is e x radial +
tangent (normalized), turned to as 0x4cdd40 inline (keep the last turn,
the node state's settings), IControls vt+0x28; a look-ahead of the
stopping distance at the share (vt+0x128) x 3, at most 10, backwards when
going round the other way, from the pilot along the steering way is put to
the boundary (0x4c3000), a pull turning the way round; the throttle is the
share (negative the other way round) when facing within 60 degrees (the
turn's dot at least 0.5), else 0. Test: `test_ai_attack.py` (mode 1 real;
the boundary's trace carries the point now; the target moved alone between
runs; negative throttles), 59 branches, 17 of 17 breaks.

**The combat manoeuvre, ported** (2026-10-22, item 3i: `ManoeuvreState`,
`AfterManoeuvreState`). The behaviours at 0x604088 (made by 0x4caac0) and
0x604098 (0x4cabc0, given no jets) make 0x4caae0 over 0x4ca440 (0x30 bytes):
+0x08 no jets (given, or no vehicle, mech or jump jets), +0x0c the jets (1
with them), +0x10 the phase, +0x14 the turn, +0x18 its end (now + the
brain's +0x6c + (+0x70 - +0x6c) x rand / 32767), +0x2c its target; the
brain's +0xf0 counts the live ones and IControls vt+0x14 stops the pilot as
one is made and gone (0x4ca570). Both answer 0x604088 as their behaviour
(0x4cab80). Each run (0x4ca600): the jets off below 10% fuel (vt+0x148,
against a double 0.1) and on again past 60%; past its end the order pops;
the throttle brakes against the speed (vt+0x114, or the forward speed
negated while airborne): 0 within 1 m/s, else (speed -/+ 1) x 0.2 negated,
at most 1. Phase 0 waits for the brake to be within 0.0001 of 0; phase 1
picks the target once: the nearest enemy contact (0x4d6b90(1)), else the
last attacker when hit within 1 s (the hit record 0x4aec80, +0x0c), else
any contact (0x4d6b90(3)); none: it brakes on and asks again. With one it
sets the brain's +0xf8 and goes on in the same call to phase 2: the lead
point (0x41d940) when the vehicle's own target contact (+0x6c) sees it,
else the target's place (0x5a8630), and 0x4c8df0's turn written out inline
(a NaN dot counts as below on each test: a full turn; a turn of 0 behind
takes 1); with the brain's +0xcc bit 0x40 and +0xf8 clear again (the
firing clears it) it ends on the next run. Then the jets (IControls vt+0x34)
while below the brain's +0x7c, the brake as the jets' forward thrust (vt+
0x3c) while airborne, else the throttle (vt+0x20), and the turn (vt+0x28;
phase 1's store of it is overwritten by phase 2 in the same call). Pushed
under another order (suspend 0x4ca5f0) it is swapped for 0x60b450's state,
which pops the order when it runs (+0x104 set: run again) or comes back on
top. In the headless c1m4 run Dominic, counted as hit in the first second
(the hit record starts at time 0, 0x4aec00), manoeuvres at once: the first
hut falls within 40 s, not 30 (`test_program`). Test
`test_ai_manoeuvre.py` (over test_ai_node.py's brain and vehicle: 300
cases, 43 branches); 29 of 30 breaks caught (phase 1's turn store, always
overwritten, is not observable).

**Mode 4, ported** (2026-10-22, item 3h: `VeerState`). Mode 4 (slot 6)
makes 0x4d1630 -> 0x4d1650: mode 2's layout, share and way round (resume
0x4d16e0, x -1). Its run 0x4d16f0 is mode 2's steering with no boundary,
no look-ahead and no +0x08: the target's place, then its own (0x5a85e0),
given up as the fight is; the way from the flat way (0x4ce230); the
throttle the share when facing within 60 degrees (a NaN holds). With it
`BrainHooks::vehicle_step_state` is gone: every state the vehicle net's
behaviours make is ported. Test: `test_ai_attack.py` (mode 4 real, drawn
more often, 300 cases), 72 branches; 11 of 12 breaks: the facing
threshold moved up to 0.6 is not caught (few dots between). The target
going inactive under a running state is rare through the frame (it drops
such a fight first), so after a step with mode 2's or 4's state on top the
test sometimes runs it once with the target inactive (its own generator):
both gates' breaks are caught.

**Mode 2, orbiting, ported** (2026-10-22, item 3g: `OrbitState`). Mode 2
(slot 3) makes 0x4d1300 -> 0x4d1320: the speed share the brain's +0x94,
its +0xec when that is smaller; the way round +1. Each run (0x4d13c0, with
its context 0x4d12a0: 0x4cecf0, the target's vehicle net, its attack
behaviour, its place) gives up as the fight does; then, from the unit flat
way u to the target at `dist`, f = (dist - R) / R (R the brain's +0x44),
the steering way (u.z w + f u.x, 0, f u.z - u.x w), normalized (w the way
round); a point R along it is computed and not used; the look-ahead, the
stopping distance at the share x 3 and at least 10 (a floor here, a cap in
mode 1), is put to the boundary (0x4c3000), a pull turning the way round
and holding the throttle at 0; the turn is 0x4cdd40 from the vehicle's
forward; the throttle the share when facing within 60 degrees; +0x08 set
to 1. Each resume (0x4d13b0) turns the way round (x -1). Test:
`test_ai_attack.py` (mode 2 real, a fake order pushed and popped over it,
mode 2 drawn more often, the stopping rate from 0.2), 68 branches, 11 of 11
breaks.

**The plain states, ported** (2026-10-20, `mech3/zai_brain`): the base
state's run 0x4c8690 stops the controls (IControls vt+0x14) and lets the
fire go (vt+0x10 with 0); the null step's 0x4c8700 and 0x604058's
(0x4c8940, run 0x4c89a0) run the same; 0x604028's (0x4c88c0) does
nothing. The default behaviour's (0x4c87f0) stops too, then gives its order
the step of its net nearest the vehicle (0x4d51d0; the port's
`nearest_step`) in its own place (0x4e8120: the state is destroyed while it
runs; the exe reads the order back through the new state's +0x04) and has
the order run again (+0x104). The shared one's (0x4cbeb0) stops, then for a
lancemate the player commands (+0xbc bit 4) pushes the formation net
0x603fb8 when the pilot's game's type ([0x60861c] +0x6c: vt+0x48, a getter
in every game type, then vt+0x2c) is 4, Training, else the player's
vehicle's own net with its follow behaviour (0x4c0de0); the formation net
is one step of the stand behaviour, so in training such a lancemate holds
where he is, and in the campaign (1) and Instant Action (2) he follows
the player (`mission_c2m1_lancemate_forms_up_only_in_training_headless`:
c2m1's Dominic follows 30 m in 260 frames; with the type 4 he stays); for any other pilot it keeps the vehicle's place
(brain +0x298, the order's +0x24) and pushes the nearest step of its net
on it (0x4e88c0, the default behaviour when there is none). Each class's
vt+0x18 answers its own behaviour (the base's 0). The 0x4c8a30 base (three
cleared words, no break-in, no fire) waits for the states built on it
(items 9-10). The formation net, the lance state, a vehicle's own net and
its follow behaviour are `BrainHooks` until items 10 and 14; the program
answers the player's vehicle.

Test `test_ai_states.py` (over test_ai_frame.py's brain and fakes, the fake
states now laid out as the real ones with the order at +0x04): the real
behaviours in the emulator, the formation net as a fake, the pilot and its
lance object, the nearest step and the follow behaviour answered from the
trace; 200 cases of 80 calls: 4054 states made, 4800 runs, 4791 slots asked,
the answers, the calls, every order and the brain's +0x104 / +0x298
identical, all 8 marked branches reached. Breaking the port showed it
catches it, 11 of 11: the stop without the fire, the fire without the stop,
the default not run again, the default replaced by none (a crash), the
formation at lance state 3, the place not kept, the lancemate on bit 5, the
player's net without its follow, the hold running the base, the null step
answering no behaviour, the shared one breaking in. In the program the
Owens' first order now pushes the step of net 4 nearest it (`mw3tests`
`mission_c1m1_owens_runs_its_frames_headless`); that step's own state (the
node state) is item 8, so it still stands.

**The vehicle's own net, ported** (2026-10-22, item 10a: `VehicleNet`,
`VehiclePart`, `vehicle_net_of`). A vehicle's vt+0xd8 (the Mech's
0x4d70f0) makes it on first ask and keeps it at its AiTarget +0xd0 (znet's
clear frees it): 0x4c0090 (0x20 bytes, vtable 0x5caaf0) over the compiled
net's 0x4bfbb0(no list, 7), its id znet's first free in [300, 400), +0x1c
the vehicle. Its seven steps are plain behaviours (0x4c8600, 0xc bytes)
whose classes differ only in the state they make (vt+0x8c, not ported:
the base's) and three type tests (0x4d2a70 "this", 0x40bae0 none): slot 0
follow (0x5caa38), 1 attack (0x5ccaa8, made by 0x4d1c80), 2 and 3 modes 1
and 2 (0x5ca980, 0x5ca8c8), 4 a combat manoeuvre (0x5bcaf8), 5 the brain's
own order (0x5ca810), 6 mode 4 (0x5ca758). The getters are those tests on
their slots: 0x4c0de0 slot 0 by +0x90 (one of the net's own, all but the
manoeuvre), 0x4c0e10 slot 1 by +0x94, 0x4c0e40 slot 5 by +0x98. It allows
no attack (+0x54). Its part (make_part 0x4c02f0 -> 0x4c0ba0, 0x24 bytes,
vtable 0x5cac40): a home point for the leash, the pilot's position when
made (0x5a85e0; when the pilot has no vehicle yet, at its first run
0x4c0d60), stacked on the brain's +0xe4 (the old one kept at +0x14, put
back by the dtor 0x4c0dc0); and +0x1c the behaviour for the pilot's mode
(+0x40, p2): 1..4 slots 2, 3, 4, 6 by +0x90, so mode 3's manoeuvre answers
none (fixes.md candidate); its reader is not found yet. Test:
`test_ai_vehnet.py` (400 nets and parts identical, 12 of 12 breaks).

**The attack, ported** (2026-10-22, item 10b: `mech3/zai_attack`). From the
brain's frame (0x4e9710) when it engages, before it pushes the fight (the
target's vehicle net's attack behaviour, slot 1; its state 0x4d1ca0 ->
0x4d1cc0, run 0x4d1d10, is item 10b2). 0x4e9ca0(n, the pilot's current
net): the current order remade on n (0x4e82e0), a net of the attack's own
(0x4c0810: 0x1c bytes, vtable 0x5cabd0, over 0x4bfbb0(0, 1), its one step
the stand behaviour 0x604058, which its dtor does not free; no attack
(+0x54); its part 0x4c0a70 -> 0x4c0a90, 0x1c bytes, vtable 0x5cac30, a home
point for the leash like the vehicle net's, whose dtor 0x4c0b50, with none
under it, ends the attack: +0xe8 FLT_MAX, +0x230 0; else it counts
[0x60b328], which nothing reads), then three orders on it, made from
behaviours on its stack (their states copy what they need), bottom to top:

1. 0x5cd920 (state 0x4cac00 -> 0x4cac20, +8 the attack's net): resumed
   (0x4cac80), its order popped and the net freed.
2. 0x4caca0 (0x5cb948; state 0x4cace0): resumed (0x4cad30), its order
   popped; +0x60 set: +0xdc FLT_MAX and +0x3c (engaging) off; +0x5c set:
   +0x3c off and +0xdc = now + +0x58 (the frame's re-engage delay).
3. 0x4cad70 (0x5cba20, +0xc the old net; state 0x4cadb0, 0x1c bytes, +8 the
   net, +0xc a waypoint, +0x18 the turn): **the way home**. Resumed
   (0x4cae10), the old net's step nearest the pilot (0x4d51d0) gives its
   position (vt+0x18) as the waypoint; none: the pilot stops and the order
   ends. Run (0x4cae90): the flat way to the waypoint (0x4ce250); the turn
   by the walking states' rule ([0x60b478]: behind, the last turn kept, 1
   for none; past the 5 degree dead zone 0x4c8d10's gain); within +0x28
   (params 5) the throttle 0 and the order ends; else the throttle that
   still stops by the middle of +0x28 and +0x2c (the vehicle's vt+0x12c, the
   Mech's 0x41d240: throttle_down x d / max_speed, 0..1), 0 while the
   smoothed throttle (controls vt+0x1c) is above it, at most +0x84 (the
   speed share +0xec when 0). When the boundary pulls (0x4c3000) it turns
   toward the pull (0x4c8df0), full throttle only facing it within 0.9, and
   the order ends.

**The fight, ported** (2026-10-22, item 10b2: `FightState`, `drive_to`).
What the target vehicle net's attack behaviour (slot 1) makes in the order
the brain pushes on that net (0x4d1ca0 -> 0x4d1cc0, vtable 0x5ccb60, over
the waiting state 0x4c8a30; +0x0c the turn, +0x18 its behaviour). It lets
no order break in and answers no to may_fire (0x4d72a0, 0x4d7290: the
hit-lately reset and the +0xa0 manoeuvre wait; the firing itself runs on).
Run (0x4d1d10): the order's part, taken as the vehicle net's (its vt+0x0c),
gives the target vehicle (its net's +0x1c) and the behaviour for the
pilot's mode (+0x1c); the pilot gives up (its order popped) when either
has no position (0x5a85e0), the target is not active (+0x08) or it must
break off (0x4e8dd0: engaging off, the leash, hurt, the time); else it
drives at the target (0x4c8a70, the way home's rule, with backing off):
within +0x2c (params 6) it backs off at the speed share when facing it
within 0.9, else holds; within +0x28 (params 5) it has arrived, and the
order takes the mode's behaviour (0x4e8120; none for modes 0, 3 and past
4, so the pilot keeps closing on the spot); farther, the speed for the
distance past the middle of the two, as the way home. Those behaviours'
states (0x4c9750, 0x4d1320, 0x4d1650, the follow's 0x4d1870) were the base
state at first; all are ported now (items 3f, 3g, 3h and the follow). Tests:
`test_ai_attack.py` (the fight pushed on a real vehicle net, 0x4d70f0 on a
target vehicle, those states faked on both sides: 703 fights in 200 cases
identical, 21 branches; 14 of 14 breaks caught) and in c1m1
(`test_program`: engaging on and the mode 1 given, the player 160 m off:
the Owens closes to 79.5 m, between its +0x2c 60 and +0x28 80, and takes
the mode's behaviour; then, as above, its way home). In c1m1 the Owens'
mode is 0. A chase from 300 m is given up by the **leash**: the Owens' flags
+0xcc are 0xeb (bit 0 on) and its +0x24 is 16900, so 0x4e8dd0 calls it off
130 m from where the fight began (the vehicle part's home point; checked
in play: 130.7 m at frame 928). Its time (+0xe8, set at the fight's start
to now + +0x4c x the intensity: 10 s for the Owens, flag bit 1 on) would
have ended it 10 s in had the leash not; it attacked again about 15 s
later.

**The brain's own order, ported** (2026-10-22, item 10c:
`Brain::run_own_order`). The order at the brain's +0x194 runs each frame
after the stack's (0x4e81a0, when params +0x80 is 1, [0x5e9d40]): with no
state yet, the nearest contact of any side (0x4d6b90(3)); its vehicle
(0x4e6fe0) and bearer (vt+0x40) within +0x74 (squared) give that vehicle's
own net (vt+0xd8) a part in the order (in place at +0x30: the vehicle
part, its home point on the brain's +0xe4) and the net's own-order
behaviour (slot 5, 0x4c0e40) its state (+0x54; when it makes none the part
is destroyed); then the part's run and the state's. The state is the station keeping (item 10d2: `make_station_state`,
`StationState`, `StationHoldState`). The maker 0x4cc150: none without the
brain's vehicle, the order's part or its vehicle net's vehicle (the net's
vt+0x58 cast, +0x1c); the other is that vehicle's bearer (vt+0x2c, vt+0x40:
a mech itself). A pilot whose vehicle's vt+0xf4 says so (0x40bae0 for every
mech and basic vehicle: no; a mobile field base's 0x4d2a70: itself, vehicles.md)
gets the hold 0x4cc240 unless the other says so too; else the
station keeping 0x4cc290, but none for a lancemate (+0xbc bit 4) by another
lancemate or by the player's vehicle (the exe reads a missing other's +0xbc:
FIX (bug), none). 0x4cc290 (0x30 bytes, vtable 0x5cbf10, over the waiting
state 0x4c8a30): +0x20 the throttle's way (-1 when reversing, IControls
vt+0x18 0x428500), +0x24 +0x78 x 0.9, +0x28 sqrt(+0x74) x 1.1, +0x2c a count,
+0x2e a phase; no break-ins, no firing; its resume 0x4cc380 takes the three
again (never called: the own order is not on the stack).

**Its run, ported** (item 10d3, `StationState::run`, 0x4cc3e0): **giving
way**. Each frame: the flat way to the other vehicle (D, its distance),
the pilot's facing along it (f, 3D), the unit relative velocity (the
pilot's vt+0x08 less the other's) and its size, its closing (rel . D) and
side (D x rel); the speeds (vt+0x114) are read but not used. Phase 0:
beyond +0x28 the order ends; inside +0x24 (too close) phase 1 when the
other is ahead (f > 0.5: throttle -1), 2 behind (f < -0.5: +1), else 3
(its throttle's way kept), at once; else when still closing (|rel| above
0.2, closing above 0, closing x distance within +0x78) it swerves: full
turn away when the side is past 0.17 (else straight), throttle its way,
for 8 frames (+0x2c), then holds the throttle with the turn 0 for the
rest; with no count left it leaves the controls alone. Phase 1 backs off
facing the other (the walking states' turn), phase 2 drives on with its
back to it, phase 3 runs alongside, turning across D on the side it
faces; each goes back to phase 0 in the same run past +0x78 or out of
its bearing (1: f below 0; 2: f above 0; 3: |f| above 0.8). It then sets
the turn and the throttle unless it left them alone. FIX (bug): phase 0
decides the same way twice in one run, so the exe loops for ever when
+0x78 is below the +0x24 its state took (a negative +0x78; the shipped p8
item 7 values are 5 to 50): the port stops the run there. In c1m1 the
Owens (+0x78 30, +0x74 1600: too close at 27 m, letting go at 44 m) gives
way to the player walking at it: it swerves while the player closes,
then, head on, backs off facing the player for as long as it is pushed,
and lets go at 44.2 m, having moved 135 m (from the side it runs
alongside instead). Tests: `test_ai_attack.py` (the run on both sides,
1405 own-order runs, 41 branches; 18 of 18 breaks caught),
`fix_ai_station_keeping_no_endless_phases` (the port alone: the exe's
loop returns; without the guard it hangs) and in c1m1 (`test_program`:
the giving way after the way home, with the own order off while the
player is walked into the Owens for the bump). The hold 0x4cc240 (vtable 0x5cbef0, run 0x4cc020): within
+0x28 of the other, phase 1 (and, under the byte [0x60b48c] nothing sets,
the pilot stopped); farther, or no other, the order ends. The own order's
pop (its vt+0x08, 0x4e8270) destroys its state, then its part: it is not
the stack's pop (the port's `Order::own`). Test: `test_ai_attack.py`
(1390 own-order runs, 540 with a part, the maker, both states and the hold
on both sides; 10 of 11 breaks caught, the one missed an exact tie of the
distance and +0x28) and in c1m1 (the Owens keeps station on the player
within 40 m). c1m1's Owens has +0x80 1 and
+0x74 1600: within 40 m of the player it builds on the player's net. Its
home point sits on the stack's (the attack's parts), and the two come off
apart: in the exe an own part made during a fight keeps a pointer to the
fight's home after that order is gone, a stale pointer into the deque, put
back on +0xe4 as the leash's home when the own part goes (FIX (bug): the
brain keeps the live homes, `Brain::homes`, and a part going from the
middle hands its under to the one above; fixes.md). Test: `test_ai_attack.py` (1429
own-order runs, 586 with a part, identical; 7 of 8 breaks caught, the one
missed equivalent: the part's run only acts while it waits for a vehicle,
and the order is built only with one) and in c1m1 (the build asked for once
the player is near).

So when the fight ends, the pilot walks back to the net it was on, to
within +0x28 of its nearest step, then the timer is set and the net freed.
The waypoint is not set before the first resume (0x4cadb0 leaves the
order slot's old bytes): the fight is always pushed on top at once, so
it is never read unset. Tests: `test_ai_attack.py` (on test_ai_orders'
stack: 200 cases, 1200 attacks, 7746 runs and 1571 pops identical, 14
branches; 26 of 27 breaks caught, the one missed an equivalent: within the
dead zone the side is too small to turn anyway), `test_parts.py` op 29 (the
stop share; 3 of 3 breaks), and in c1m1 (`test_program`: engaging turned on,
the Owens attacks; the fight ended and the Owens put 250 m off, it walks
back and its order ends at 79.8 m, its +0x28 being 80).

### 17.3 The engage gate (0x4e8f10, 0x4e8dd0)

0x4e8f10(owner, net, state) allows an attack when engaging is on (+0x3c,
params 2 or the delay +0x5c), the net (vt+0x54: yes for compiled nets) and
the state (vt+0x04) allow it, and none of these, in this order: +0xcc bit
0 and the owner's position (0x5a85e0) farther than sqrt(+0x24) from the
home point +0xe4 (when there is one); the hurt flag +0x22c, decided once a
frame (the frame sets it to 2, the first check to 1 when +0xcc bit 2 and
the vehicle's health vt+0x98 is under +0x50; at the frame's end bit 2
becomes the flag, so the first check that finds the pilot above the floor
clears bit 2 for good: the floor only holds back a pilot hurt when it
first weighs a fight, until it is repaired over it, fixes.md candidates); +0xcc bit 1 and the game time
at or past the attack's end +0xe8; +0xcc bit 3 and the health under +0x230
(the health the attack began with less +0xb4). The vehicle's health is the
Mech's 0x41ba40: its parts' health multiplied, cached at +0x40c (not
ported). 0x4e8dd0(owner) answers the reverse without the net and state:
must the pilot break off (yes too when engaging is off); the node state and
three of the attack's states ask it (0x4c990b, 0x4d140a, 0x4d176d,
0x4d1d84). The x87's unordered compares (a NaN) fall as written: a NaN
distance or time passes, a NaN health is hurt and under the floor.

The brain's vt+0x0c 0x4e9640 is +0x54 == 0 (p6's first; its use is the
callers'), vt+0x10 0x4e9650 "sees its target": always with +0x88 at 1, else
the contact on the player (owner +0x98, +0x8c set) or on the nearest of his
lance (owner +0x80) within the sight range +0x1c (squared).

**Ported** (2026-10-20, `mech3/zai_brain`: `may_engage`, `must_break_off`,
`p6_clear`, `sees_target`, `manoeuvre`). The vehicle comes in as a handle
and the brain asks `BrainHooks` for its health, position, AiTarget and the
game time; the manoeuvres' behaviours (`StepKind::Manoeuvre`,
`Manoeuvre2`) make the real state since item 3i (17.2, the manoeuvre); the
gates' test keeps fakes for them.

Test `test_ai_gates.py` (probe `tests/probe_ai_orders.cpp`, over
test_ai_orders.py's brain and fakes): the vehicle a fake (its health an x87
`fld` thunk answered from the trace, its position, its two contacts), at
24-bit x87; 200 cases of 100 checks with random settings, flags, hurt
flag, home point, times, positions and contacts, NaNs among them: 18,333
checks, the answers, +0x22c, +0xf4, 24,940 calls and the stack identical,
all 23 marked branches of the five reached. Breaking the port showed the test catches it, 15 of 15: the
hurt flag decided on every call, the leash without its flag, the time on
the wrong flag, a NaN time refused, the health floor at +0x50, a NaN
health not hurt, the net or the state not asked, no break-off with
engaging off, the two contacts swapped, +0x88 not always in sight, the two
manoeuvres swapped, the manoeuvre's timer kept, +0x58 for +0x54, the leash
measured from the origin.

### 17.4 Steering (+0x250, 0x4ce620..0x4cec80)

Steering does not touch the controls. It is a path segment and a carrot on
it that the vehicle is measured from; the states that walk (the node state
0x4cf140 and the others that call 0x4cea30 / 0x4cecf0, items 7-11) set the
segment and turn and throttle the controls toward the carrot.

| Field | What |
|---|---|
| +0x00, +0x0c | the segment's start and its flat unit direction (x, z) |
| +0x18 | its length |
| +0x1c / +0x14 | the carrot's distance along it and how far back it may fall |
| +0x20 / +0x24 | the carrot's offset to the side and the offset it eases to |
| +0x28 | how fast the carrot falls back, a second |
| +0x2c / +0x30 | the vehicle from the carrot, along the segment and to its side |
| +0x34 | 20: the vehicle is near the segment within this |

- **0x4cea30** (to, from, rate, vehicle): the segment from `from` toward
  `to` (0x4ce530 normalises), the carrot at its far end, no side offset,
  the vehicle measured (0x4ceb00), then the contacts' push.
- **0x4cec80** (each frame, from the brain's frame): the side offset eases
  to its target at 2 a second (snapped within 0.05), the carrot falls back
  by the rate times dt but not past +0x14; then **0x4ceb50**: the vehicle
  measured; the carrot moved along (0x4ce800) to stay within 30 m of it
  less its distance to the side; when the vehicle is near the segment
  (0x4ce6a0: its distance to the side, plus how far before the start or
  past the end, under +0x34) the contacts' push.
- **The contacts' push** (0x4ce780, 0x4ce7d0, 0x4ce9f0, 0x4ce860): the
  vehicle's contacts, nearest first, while within (30 + along)^2 + 10^2
  (a NaN distance counts): one ahead of the vehicle and behind the carrot,
  within 10 m of the line, pushes the carrot ahead by the room left (30 +
  its distance along, less its distance to the side), and the more head-on
  it faces (its flat forward against the segment's backward, a full push
  at cos 135 degrees), the more of that push goes to the side away from it
  instead (0x4ce820).
- **0x4ce6f0**: the carrot's point in the world; **0x4ce690**: clears +0x3c
  and +0x44 (the brain's destructor).

**Ported** (2026-10-20, `mech3/zai_brain` `Steer`: `set`, `update`,
`measure`, `near`, `carrot`, `project`, `advance`, `shift`); the brain's
frame runs `update`. The vehicle's contacts, a contact's entity (0x4e6fe0:
vt+0x2c only, no vt+0x40) and its flat forward (+0x24, +0x2c) are
`BrainHooks`; [0x60b494] (cos 135 degrees, the start-up's 0x4ce610) and
[0x60b4c0] (never written, 0) are the port's constants. The x87's
unordered compares are kept as written (a NaN distance is near, a NaN room
pushes nothing).

Test `test_ai_steer.py` (probe `tests/probe_ai_orders.cpp`, over
test_ai_frame.py's brain and vehicle; the emulator given the start-up's
cosines and MSVCRT's `_copysign`): segments, the vehicle near them and up
to six contacts around them on entities facing them or away, some out of
reach, NaNs among the numbers; 200 cases of 150 calls (sets, updates with
random times, rates and side targets, near tests, carrots, the block
loaded at random): 4583 sets, 17,410 updates, 3644 near tests and 3489
carrots, the answers, every call and the 0x48 bytes identical, all 14
marked branches reached. Breaking the port showed it catches it, 14 of 14:
the side eased at 1 a second, no snap, the carrot below its floor, the side
measured the other way, the room not floored, past the end not counted,
the far contacts read on, the reach without the vehicle's distance, the
band at 20, head-on unclamped, the side push toward the contact, the
carrot set at the start, the carrot's side the other way, no push at a set.
The carrot's point skips a side offset of 0 or a NaN (the x87's C3), which
the test's NaNs caught.

In the program the Owens' brain runs the steering each frame; the node
state (17.4a) sets its segments, and it walks.

### 17.4a Walking to a node: the node state (0x4ced40, ported)

A pilot whose order's behaviour is a node of its net (a `node` record,
class 0x4cf820) runs the node state (0x4c9150 makes it in place, 0x28
bytes, vtable 0x5cc5c0; backup 0x4cfa80 and deploy 0x4d0040 derive from
it, items 8b and 8c). It walks the brain's steering segment to the node
under the pilot's own controls, then picks the next link.

| Field | What |
|---|---|
| +0x04, +0x08 | the order, the node |
| +0x0c | 0 set out, 1 walking, 2 at the node with nowhere to go |
| +0x10, +0x18 | the last turn and throttle given the controls (+0x14 unused) |
| +0x1c, +0x20 | the stopping distance at the pilot's speed, its inverse |
| +0x24 | FLT_MAX; when a node with nowhere to go asks again |

- **0x4ced40** (with the context 0x4cecf0: the brain, its vehicle, the
  vehicle's position 0x5a85e0, its flat forward +0x24, the steering): the
  pilot's speed is the vehicle's vt+0x118 on the brain's +0xec share (the
  Mech's 0x41d1c0: top speed +0x518 times it), the segment set (0x4cea30)
  from the node toward where the pilot last was (brain +0x298), so the
  carrot starts there and falls back toward the node at that speed; +0x1c
  is vt+0x120 on the speed (0x41d210: speed over the decel rate +0x534).
- **0x4cf140** (run): at the node, when the carrot has fallen back to it
  (+0x1c <= +0x14) and the vehicle is within the stopping distance (its
  along plus its side from the carrot), the next link (0x4ccf20) given the
  heading (the segment's backward, the way the pilot came) takes the order
  (0x4e8120; the node's place kept at brain +0x298); with none the pilot
  stops (turn and throttle 0) and asks again 5 s later ([0x5cc560] -5),
  waiting in phase 2. Otherwise **0x4cee80** walks: toward the carrot
  (0x51e210 flattens it), the mission boundary pulling a far carrot back
  inside (0x4c3000, not ported, section 11); within half the stopping
  distance it faces along the segment instead. Behind the carrot (its
  along below 0) it backs up: the turn is then 0x4cdee0 (the vehicle's
  back toward the point past 135 degrees off), else 0x4cdd40. The
  throttle is (side - 0.04, floored at 0) + 4 (along - 0.04, floored at 0)
  over the stopping distance, negative backing up, within the brain's
  +0xec share; none when the facing is more than 45 degrees off (steering's
  +0x38), so the pilot turns on the spot first.
- **The turns** (0x4cdd40, 0x4cdee0, the settings [0x60b478] from
  0x4c7900: cos and sin 5 degrees, 1.2, 0.1): within 5 degrees, 0; else the
  sine past 5 degrees times 1.2, at least 0.1 and at most 1; a point behind
  turns fully toward its side; one more than 5 degrees off straight back
  keeps the controls' turn target (IControls vt+0x30), or turns fully with
  none.
- **0x4c9700** (resume): phase 0 and the controls stopped (vt+0x14).
- **The link pick 0x4ccf20**: the brain's way finder (+0x238, 0x4c7cb0)
  first, when the pilot has one (the behaviours that search set it, items
  8c-10). Else each link but the null step is weighed (vt+0xa8): a node
  1048575 ([0x60b44c], 0xfffff from 0x4c7950), a plain step what follows
  it, a valve step only while its valve holds its value and a valve union
  only while it holds (both truncated to a whole number, _ftol), a jump or
  gate step only when the pilot may pass. Weighing a node measures the flat
  way to it from the node picking (0x4c79f0, 0x4c7960; a link nearer than
  1 m is measured along the vehicle's facing instead, and the point it is
  measured from moves for the links after it). A weighed link counts
  (0.01 + 0.495 (1 + cos)) times its weight, cos the angle between the
  heading and the way to it: straight on 1, straight back 0.01. With one
  link it is taken if it weighs anything; with more, the only one that
  counts, or one at random by weight (rand, a lower bound over the running
  sums).

- **The speed and valve steps' states** (taken with the node state, for
  the walk on): a speed step's (0x4c9ed0 -> 0x4c9ef0, vtable 0x5cb540, run
  0x4c9f40) sets the brain's speed share +0xec to the step's value, a
  valve step's (0x4ca1f0 -> 0x4ca290, vtable 0x5cb7c8, run 0x4ca2e0, which
  shunt and valve union states share) takes the way finder's next step or
  the one that follows; both have the order run again (+0x104) on it.

- **The backup node's state** (0x4cfa60 -> 0x4cfa80, vtable 0x5cc860, run
  0x4cfad0; no shipped net has a `backup` node): phase 0 turns the
  vehicle's back toward an aim (dot at least 0.9, 0x5cc568), 1 backs it up
  the segment to the node (the walk with its back as its front, the
  throttle negated), 2 at the node turns its front toward the aim, then
  the node's way on (3 the 5 s wait). The aim is a stack vector the run
  never sets in phases 0 and 2: what the pick's heading left there on an
  earlier frame, or anything another call left. On the frame phase 0
  finishes it walks with its front (the back is made again from the
  context's forward, which now points at the back).
- **The deploy node's state** (0x4d0020 -> 0x4d0040, 0x38 bytes, vtable
  0x5cc950, run 0x4d00a0; 288 shipped deploy nodes): walked to the node as a
  node is (0, 1); there the brain's own order is held (aiv +0x80 0) and
  the vehicle walked to within 1 m (2, +0x1c 1); once it has settled (its
  local vertical speed +0x40 under 0.1, 3) the segment is set from the
  deploy target (its last item's node) to the node and the vehicle turned
  to face along it, away from the target (+0x2c; first with a 1 degree
  dead zone, [0x60b4d8] made on the run's first call, then 5 degrees, 4);
  then slid onto the node at 0.5 m/s (0x29, the Mech's set_place each
  frame, facing away from the target) and set on it. While "mcbdeploy"
  ([0x60b4a8], found or made and set to 0 at every mission load by the
  listener 0x4cfe40 through 0x4e7b40) reads 1 ([0x60b4ac]) and the node's
  second valve is not at its value, it holds there facing that way (0x2a,
  5), setting its first valve to its value; then +0x80 back to 1 and the
  node's way on (6 the wait, which a way finder ends at once). In the
  campaign "mcbdeploy" stays 0: a deploy node places the pilot exactly on
  it facing away from its target, then it goes on.

**Ported** (2026-10-22, `mech3/zai_node`: `NodeState`, `walk_drive`,
`turn_toward`, `turn_or_back`, `turn_params`, `pick_link`, `LinkWay`; the
steps' `weight` and `way_point` slots in `zai_steps`; the brain's way
finder is its `path`); the speed and valve states with the plain ones in
`mech3/zai_brain`. The vehicle's speeds (`Mech::speed_at`,
`Mech::stopping_distance`), the boundary clamp, the pilot as the steps'
tests see it (its name and jump jets) and the controls' throttle, turn
and turn target are hooks; the program answers them from the mechs, and
the clamp stays unported (false, as for a world without a boundary).
[0x60b4c4] (no backing up) and [0x60b4c8] (no facing along the segment
when near) are never written and are the port's constants. The weights'
scratch vector [0x60b444] (reserved by the node constructor) is a local
list. The pick's straight-on share multiplies a float by the double
-0.495 and takes it from the double 0.01: each rounded once to a float
(`crt::x87_mul_f24`, `x87_add_f24`), as the x87 does in play; rounding
through double differed in the last bit (the test found one). A step chain that ends in the null step reaches the base weight
with no position, which the exe reads (a crash): FIX (bug), it weighs 0.

Test `test_ai_node.py` (probe `tests/probe_ai_orders.cpp`, over
test_ai_steer.py's brain, vehicle and steering): the vehicle's vtable
moved off the Mech's +0x518 with the real vt+0x118 / +0x120 / +0x50 over
random top speeds and decel rates, its forward, name and jump jets; the
controls' throttle, turn and turn target, the boundary clamp and the way
finder answered from the trace; nets of 3 to 6 real nodes in the exe's
layout linked to each other (some within 1 m or on top of each other), to
the null step and through chains of real speed, valve, jump and gate
steps (the jump and gate steps' states fakes). 300 cases of 120 calls:
2573 states made, 15,589 runs, 1106 resumes and 5237 link picks alone
(the step picked and the running sums the exe leaves in [0x60b444]), the
calls, the order's state, the steering block and the brain's +0x298,
+0xec and +0x104 identical at 24-bit x87; 24,000 turns alone on random
vectors; the turn settings from the exe's 0x4c7900 bit for bit; all 36
marked branches reached. Breaking the port showed it catches it, 38 of 38:
dead zone at 2 degrees, behind keeps 0, the cross swapped, no least turn,
backing up past 120 degrees, the back's dot kept, a near link not refaced,
the near link's floor 0.2, `from` not moved back, a valve's weight not
whole, a gate passing all, the way finder skipped, no straight-on share,
another share, the one link without its weight, no random pick, the speed
not shared, the segment reversed, resume keeping the phase, arrival by the
along alone, arrival without the carrot, the place not kept, a 1 s wait,
the heading not reversed, near at the full stopping distance, no backing
up, the clamp asked when near, the clamp keeping the reverse, near facing
the carrot, the side weighted 4, no 45-degree stop, the throttle
unclamped, the Mech's speed not shared and its stopping distance by the
accel rate, the speed step's share not set or not run again, the valve
step ignoring the way finder or not run again. Not caught, and left out
as equivalent: the dead zone's exact edge, the pick's upper bound at an
exact tie, the null step weighed (the FIX makes it weigh 0), the last of
one counted link (it is the only one).

**Backup and deploy** (2026-10-22, `BackupState`, `DeployState`,
`deploy_mission_load`; the hooks `vertical_speed` and `place`, the program
wiring the latter to `Mech::set_place`; the mission load's 0x4e7b40 calls
the deploy listener, the other four listeners stay logged). The test
gained backup and deploy nodes (their valves, values and targets), the
"mcbdeploy" valve, the vertical speed, the placement (vt+0xdc logged with
its point and yaw), the valve set 0x4d3c00 and a way finder present or
not; the backup run's unset aim is written into its stack slot on entry
as the port's FIX. 300 cases: 2438 states made, 15,859 runs, all
identical, the fine turn settings and the placement's yaw bit for bit;
57 branches reached. Breaking backup and deploy, 23 of 23 caught: the aim
the other way, phase 0 with the front, turned at 0.8, no first-frame
quirk, the throttle not negated, arrival keeping it, the wait's phase;
+0x80 kept or not restored, closing to 2 m, settled at 0.2, the segment
reversed, the first turn coarse or the later ones fine, the slide's speed,
step or length, facing the target, "mcbdeploy" ignored, the second valve
inverted, the first not set, the wait ignoring the finder, a 2 degree fine
dead zone. 8a's 38 breaks still caught on the extended test.

**The scan and canleave steps' state** (0x4cbc30 -> 0x4cbc50, vtable
0x5cbc88, run 0x4cbca0; scan_time 2796, canleave 2858 in the shipped nets):
each of the step's tests (function, word) is called on the brain plus the
word (always 0), then the order runs again (+0x104) on the next step. The
tests (0x4d9c20..0x4d9ca0, `Brain::scan_test`) only set the pilot's aiv
flags: scan_aim +0x98, scan_hit +0x9c (the frame's manoeuvre when hit,
17.1), scan_time +0xa0 (the random-wait manoeuvre) on or off, and canleave
+0x3c (whether it may break off to engage, 17.3) on or off with the
engage delay's end +0xdc set to FLT_MAX. Test: the scan steps in
`test_ai_node.py`'s nets (the exe's layout, the real test functions), the
five fields compared after every call: 300 cases identical, 67 branches;
8 of 8 breaks caught (the tests not run, only the first, no run again,
aim at +0x9c, hit off setting, time on clearing, the delay kept,
canleave not inverted).

**In the program** (`mission_c1m1_owens_walks_to_its_node_headless`): woken
by bramp, the Owens is on the node of net 4 it stands on, goes on at once
by its link (a speed step, share 0.7, and a valve step) to the next node
237 m off, turns to face it (forward against the way there 1.000), walks
there under its own controls (throttle up to 0.7) and, 14.6 m off, inside
its 16.8 m stopping distance, goes on by that node's link: a scan step,
then its post 119 m on, where it stands (phase 2, asking every 5 s): the
node's one way on is a valve step waiting for `firefly_retreat`, which the
mission sets later. With the nets followed, the mission
cleanup's "Deleting AI Nets" (0x58b290, ported as `znet::clear`) is now
wired: before it, a second mission in one process kept the first one's
nets, whose valve steps pointed at freed valves (a crash in the suite).

### 17.5 The vlist triggers (0x4d8e70)

The aiv.zrd `vlist` entries (section 3) are triggers, not orders. Each
frame the brain (0x4e9550 -> 0x4d9290) runs each entry in turn (0x4d8e70):
an entry whose valve (item 16) already holds its value (flags bit 1, item
1: 1 or 0) does nothing; otherwise the first of these that holds sets the
valve to that value (0x4d3c00, its reactions firing):

| Flag (item) | Holds when |
|---|---|
| bit 3 (4) | the pilot was hit within the last second (IControls vt+0xa0) |
| bit 5 (7) | the vehicle's heat (vt+0x9c, the Mech's +0x554) is at +0x08 (item 8) or more |
| bit 4 (5) | its health (vt+0x98) is at most +0x04 (item 6) |
| bit 2 (2) | its ammo's value (IControls vt+0x9c: 0x4b4ed0 on the manager, 1 without one) is at most +0x00 (item 3) |
| bit 7 (13) | the nearest enemy (picked once per frame, shared with the brain's frame) is nearer than sqrt(+0x10) (item 14) |
| bit 6 (11) | it is nearer than sqrt(+0x0c) (item 12) and there is a clear line to it (0x4d5980, 17.7) |
| region (10) | none of the above: the vehicle (0x5a85e0) inside the region (0x4c57d0) for +0x19 bit 0 (item 9), outside for bit 1 (item 17), anywhere with both |

A NaN health, ammo or distance holds; a NaN heat does not. Bit 0 (item 15)
is not read here.

**The heat condition in the shipped files** (2026-10-25, item 10): of the
2,573 vlist entries in every aiv.zrd (all worlds' campaign, Instant Action,
multiplayer and training blocks, read by the ported reader), 155 have bit 5:
107 in the Instant Action blocks (most every enemy's, flags 0xb7, heat 0.1)
and 48 in campaign missions (c1 m2..m4, c2 m1/m2/m4, c3 m1..m6, c4b m6;
thresholds 0.01 to 0.8, e.g. c1 m2's owens_01 at 0.01, c1 m4's blackhawk_01
at 0.4 and 0.8); no player record has one. Heat is the vehicle's vt+0x9c: a
Mech's +0x554 (0x41ec90); every other entity class (basic vehicles, turrets,
targets, the field base, the plain AiTarget) answers 0x45b950, the constant
0.0 at 0x5c23a0, so only mechs can meet it. The program answered nothing
there until item 10 (BrainHooks::heat logged as unported), so none of the
155 ever held by heat. The player's controls run their own vlist the same way
(IControls +0xa4, 0x4d8990, from their update 0x427d60 while not powered
down): the settings at their +0x5c (0x4d9840: an AiParams with the
defaults, vtable 0x5cd348) hold the launch's aiv.zrd "player" record
(IControls vt+0x88, 0x428440 -> 0x4d99e0), and 0x4d9290 runs their list
(+0x12c) on the vehicle (IControls +0x10), the IControls answering "hit
lately", nothing picked yet. The AI controls' IControls +0xa4 is 0x4e94f0,
the brain's with nothing picked.

**The player's, ported** (2026-10-22, item 3b1: `PlayerControls::settings`,
`set_record`, `pilot_tick`; the program's launch hook gives the record).
The mission boundaries and the other player triggers (c1 mission 1 has 31:
the boundary, the dialogue and the APC regions) now run. The controls are
made once for the game (0x427280), so in the exe the next mission's
placement update (0x4782f0, before the new record) runs the last mission's
list over valves the cleanup freed. FIX (bug): the program clears the
settings with the valves (`valves_end`).

**Ported** (2026-10-20, `mech3/zai_brain` `vlist_run`, `Brain::vlist`,
`Brain::pilot_tick`; the brain's frame runs it). The heat, the clear line
and the region test are `BrainHooks` (the region's default is the port's
`Region::inside`; the clear line is the port's, 17.7, since item 14); the
valves are the ported ones.

Test `test_ai_vlist.py` (probe `tests/probe_ai_orders.cpp`, over
test_ai_frame.py's brain and vehicle; the exe's own IControls vt+0x9c
0x4aed10): the exe's std::list built in its memory, fake valves (value and
set) beside the port's real ones, two regions and the clear line answered
from the trace; 200 cases of 50 lists of 0 to 6 random entries (flags,
values, ranges, regions, valves or none, NaNs) with random valve values, a
picked flag and target: 10,000 lists, 5,681 valves set, the calls, the
picked flag and target after and the four valves identical, all 14
marked branches reached. Breaking the port showed it catches it, 12 of 12:
the heat below, hit lately skipped, the health's equal and NaN not
holding, no ammo without a manager, a pick for every entry, the near range
from +0x0c, the clear line not asked, both region bits never, inside and
outside swapped, the valve's value from bit 0, a valve at its value set
again, only the first entry run. My first reading had the heat the other
way round (the jump is to the set): the test caught it.

### 17.6 The radio and the player's orders

The player's lance commands (the command handler's table at
0x469970..0x4699d8, mech_*.cpp) call 0x4ea000..0x4ea230: each the brain's
vt+0x20..+0x48, then 0x4e9fd0 on the answer (0: a radio line through 0x4e6d50, 1: a
callback, 0x4ea030 or 0x4ea0e0; which lines is not traced). The slots: +0x20 0x4ea450 attack
my target (its net, 0x4c0de0, 0x4e87c0), +0x24 / +0x34 the same,
+0x28 0x4ea4b0 form on me (net 0x603fb8), +0x2c / +0x44 0x4ea390 only an
enemy, +0x30 / +0x48 0x4ea3f0 only an ally, +0x3c 0x4ea260 (0x4d08d0's
order), +0x40 0x4ea4e0 stop (back to the last unmarked order).

**The brain's lance orders, ported** (2026-10-22, item L4a: `Brain::
attack_target`, `form_on_me`, `enemy_only`, `ally_only`, `lance_order`;
`FormationNet` / `FormationPart` in zai_attack; the refit a hook). The
slots by vtable (0x5cc698, the mech brain's 0x5cd2c8 the same): +0x20
0x4ea450 the contact's vehicle (0x4e6fe0) and its own net (vt+0xd8): the
current order stopped (vt+0x40) and the net's follow (0x4c0de0) pushed on
it (0x4e87c0; the follow state, 17.8b), 1; no vehicle or net, 0. +0x24 /
+0x34 0x4ea4a0 the same. +0x28 0x4ea4b0 stopped, then the formation net
0x603fb8 pushed (0x4e86a0 == 1). +0x2c / +0x44 0x4ea390 the same as +0x20
when the contact's vehicle is no ally (owner +0xc4 & its +0xc0), +0x30 /
+0x48 0x4ea3f0 when it is one; a contact that sees nothing is read through
null there (FIX (bug): not taken). +0x38 0x58b380 0. +0x3c 0x4ea260 the
refit (the repair behaviour 0x4d08d0 pushed when 0x45a000 counts bases up,
the dock takes the pilot (0x47a780) and 0x4d0920 finds the dock point;
ported in item 4b6, zai_repair: vehicles.md, The AI at the dock). +0x40
0x4ea4e0 stop.
The wrappers by the dispatcher's order number: 0 +0x20 (line follow,
radio +0x30), 1 +0x28 (stop +0x34), 3 +0x2c, 4 +0x30, 5 +0x34, 6 +0x38,
10 +0x44, 11 +0x48 (confirm_order +0x08), 7 / 12 +0x3c (confirm_refit
+0x0c), 8 +0x24 (follow), 9 +0x40 (disengages_order +0x14); each answers
through 0x4e9fd0: with a radio, 0 says disregard_order (+0x10), 1 the
order's line, anything else nothing. The formation net (0x4c0570 at
start-up, vtable 0x5cab60): one step, the stand behaviour; attacks
allowed; its id the first free from 10000; its part (0x4c07d0) only holds
it. Test `test_ai_lance.py` (on test_ai_attack's world, the brain's
vtable the exe's slots, the formation net built by the exe's 0x4c0570):
3,608 orders (2,244 taken) and 5,546 runs after, the answers, calls,
stacks and steering identical; 9 of 9 breaks.

**The dispatcher, ported** (2026-10-22, item L4b: `mech3/lance_orders`
`lance_dispatch`, from the command handler's lance keys when its hook is
unset). 0x469940 (ECX the controls, EDX the order, reached by 0x469930):
none without controls; the controls' owner (IControls +0x10, vt+0x50)
the player. Outside the command mode (the controls' vt+0x08 none):
0x469400 for orders 0, 1, 7, 8, 9, 12 and 0x469650 for 3, 4, 5, 6, 10,
11, both on the selected lancemate: the player's saved target or last
ally (0x4d7240) when its mech (0x4e6ff0: what the contact sees, vt+0x2c,
then vt+0x50) is a lancemate under orders (+0xbc bit 4), through its
controls (vt+0x140) and brain (vt+0x54). 0x469400 gives the order on the
lancemate's contact on the player (+0x98), so order 0 (vt+0x20, its line
"follow") is "follow me". 0x469650 gives it on a contact made from the
lancemate on the player's target (+0x6c, 0x4e6fe0): 0x4e6ee0, the
template 0x5e9d10, the owner, the entity, what it sees (vt+0x24), the
squared distance ((dx dx + dy dy) + dz dz) and the way; before it,
0x468ca0 changes [0x605f58]'s team for orders 10 and 11 when that is the
lancemate ([0x605f58] is the command mode's, L4c; left out). The
dispatcher returns the controls. Order 2 (the command mode, 0x4690d0) and
both dispatchers' command-mode paths (0x468f00 / 0x468fa0, then the
controls given back) are L4c. Test `test_lance_dispatch.py` (the exe's
0x469940 on made-up objects, the brain's wrappers stubbed; the port on
real Mechs, `tests/probe_lance.cpp`): 3,000 cases, 492 orders given,
identical, 4 branches; 7 of 7 breaks.

**Playtest W11 (2026-10-30): the radio in the program.** 0x4e7e20 reads
the player's vehicle through the pilot ([0x60861c] +0x14) while the
mission loads, before the launch (0x491150) makes it: in the exe that is
empty (the last cleanup, 0x491868, set it to 0), so every brain with a
voice gets its radio. The program's launch had put a stand-in mech block
there (from before the launch was ported), which answered allies 0: no
lancemate had a radio, and no order was ever answered. Removed; the
radio's lines then play (they draw on zSound's rand(), so seeded runs with
a talking lancemate changed). The orders were taken all along, by the
selected lancemate only (0x4d7240: the saved target, else the last ally
targeted); none selected, the keys do nothing, as in the exe.
Only brains built during the load were hit: a lance's mates from its
slots are built after the launch and saw the real player. In c1m4 Dominic
is built at the load, and the player's team there (the aiv record) has no
allies, so W never selects him: orders reach him only in later missions,
in the exe as here. Also fixed: 0x469650 takes the player's target's
vehicle (0x4e6fe0); the port took its mech, so F6 / F7 on a vehicle were
dropped (`test_lance_dispatch.py` now has non-mech targets). Open: a
targets.zrd object as the target, whose vt+0x2c the program does not answer.
`mission_c2m1_lance_order_keys_end_to_end_headless`: the player's keys, W
until Dominic is the ally, E (the ally record kept), F6, F8, F9; E clearing
the ally record fails 4 checks.
`mission_c1m4_lance_orders_reach_dominic_and_he_answers_headless`: F8
unselected (nothing), then selected F8 (follow, his walk to about 100 m
of the player), F9 (stop), F6 (confirm_order, following the enemy), F7
on an enemy (disregard_order), F10 (disregard or confirm_refit). Break:
the stand-in back fails 6 checks. `radio_calls()` records each line.

**A dead pilot's triggers** (0x4d8a00 -> 0x49e540 -> 0x49e4e0, ported:
`player_takes_triggers`): when an AI pilot dies, its vlist entries that
watch a region (+0x14) are appended (0x49e4c0 -> 0x4d9480) to the
player's own settings (his controls' vt+0x90), if it was on his side
(+0x18 the same); no player or no controls: reported, nothing. So a
mission's "a lancemate reaches the area" trigger fires when the player
gets there instead. Test: `test_lance` lance_dead_pilots_region_
triggers_go_to_the_player.

The radio's lines play through 0x4e6d50 -> 0x4e6c60: the line's record is
the sound's zSound user data (0x56f430: the EXTRA's MINDELAY and SUBTITLE,
sound_extra.cpp) or the default at 0x60b800; it plays (zSound's 2D play
0x56e3d0, at volume 1) unless its next time is later than now, sets the
next time when MINDELAY is above 0, and gives the panel its subtitle when
[0x60b878] (never set). The radio's slots are Radio::kEvents. Callers:
the frame's ammo (out_of_ammo +0x28, low_ammo +0x24, 17.1); the hits
(IControls vt+0x94, 0x4ea520: call_help +0x00, friendly_fire +0x20, 17.8b);
a kill (vt+0xb4, 0x4ea630, from the kill credit 0x41b420 with the victim:
last_shot +0x04 unless it is an ally); its death (vt+0xa8: the AI mech
controls' 0x4d8a00, the player's lance told (0x49e540), then 0x4ea6b0:
the base 0x4aed50 (the controls' vt+0xa4 tick once more) and ejecting
+0x18); each order's answer (0x4e9fd0, item L4). The player's controls'
vt+0xa8 is the base's (his vlist once more), vt+0xb4 nothing.

**Ported** (2026-10-22, item L2: `sound_extra_say` 0x4e6c60, `sound_say`
0x4e6d50, `BrainHooks::radio_line` on the radio's slot, `Brain::killed`,
`Brain::died`, `MechControls::tick` / `on_death` / `on_kill` (vt+0xa4,
+0xa8, +0xb4) with the AI's, called from `Mech::die` and `Mech::on_kill`
when their hooks are unset). 0x49e540 is the `lance_lost` hook (L4).
Tests: `test_sound_extra.py` runs 0x4e6c60 (600 cases), `test_ai_hits.py`
0x4ea630 and 0x4ea6b0 on its cases.

**Playtest W17 (2026-10-30): an ordered lancemate and walls.** Owen: told
to follow or attack, Dominic "just runs into a wall" (Op 2). Both orders
push the target's VehicleNet follow behaviour (0x4ea450), whose state
0x4d1980 steers straight at the target with the carrot (walk_drive
0x4cee80). It does not route over the AI net. The AI wall response
(on_path 1, 0x4a5270) reverses the throttle and adds a yaw kick of speed x
0.01. On c2m1, with the player 40 m past a wall, Dominic walked into it and
backed off. The contacts' push (Steer::push_by_contacts, 0x4ce780) then
held his carrot 10 m ahead, inside his slack ((+0x28 - +0x2c) / 2), so
walk_drive gave him no throttle. He stood 73 m off for good, past his
follow range (to +0x28, 60 m). The pieces are the exe's and are
differential-tested (`test_ai_attack.py`, `test_ai_node.py`), so this is
the original's behaviour.

The mission's AI nets cannot route him. They are scripted unit routes, with
9 to 62 placed nodes across a map and one-way links, not a navigation
graph. The exe's way finder (0x4c7f70) found no way between the nodes
nearest Dominic and the player on either map.

Ours (FIX (bug), behind `bugs`, Owen 2026-10-30): a detour inside the
follow state (`FollowState::detour_goal`).
- **Blocked:** past the far end of his follow range (+0x28, where the
  follow never settles: it is content within the slack of the range's
  middle), with no 2 m of progress in
  2 s and no turn of over 20 degrees (a turn on the spot is not being
  stuck).
- **The heading:** flat probes of 25 m, from 4 m out at 4 m up. They use
  the world's segment query (0x4efcd0, the brain hook `wall_between`, a
  level face being a wall), in 15-degree steps off the line to the target.
  He takes the clear heading nearest the target, keeps to that side, and
  probes again every 0.5 s.
- **Resume:** once the line to the target (up to 40 m) is clear, after
  1 s, or 2 m inside his range.
- **Give up:** after 20 s, or with no heading clear. He then follows as the
  exe for 10 s.
On open ground nothing changes. The probes turn `bugs` off.

Test `mission_c2m1_dominic_follows_round_walls_headless`:
- c2m1 behind the wall: he reaches 51.5 m, inside his range. With `bugs`
  off he stands at 73 m, as the exe.
- c2m2 behind a wall: he reaches his range.
- c2m1 open ground: no detour, his walk identical with `bugs` off.
- Every heading blocked (a test switch): he gives up and stands where the
  exe leaves him.
The same holds for a pocket commander's spot on c1m4
(`mission_c1m4_pocket_commander_sends_dominic_to_a_spot_headless`). His
range there is 60 to 80 m, but at about 120 m he bounces off something and
the exe leaves him there (120.879). That test had taken this for his reach.
With the detour he works round it (114.758 by the run's end).

**Playtest W27 (2026-10-30): the leader's way.** Owen, on a build with the
detour: "When I turn a corner he just runs into a wall perpetually", and
in c2m4 Dominic "spent the entire mission ramming into a wall". The probes
see only the straight way's sides, so a leader who walks round a building
or along a canyon leaves him behind. Ours (FIX (bug), behind `bugs`,
`FollowState::trail_goal`):
- **Breadcrumbs:** each follow keeps its leader's places a point each 8 m
  (the last 60). A jump of more than 32 m (a teleport) starts them afresh.
- **When:** past his range (+0x28), with the leader out of sight, or in
  sight but no 2 m of progress in 3 s (the standstill above; then for 6 s).
- **Sight** is a mech's: eight segment queries, the line at 1.5, 4, 6.5 and
  9 m up and its two sides 5 m off at 2.5 and 6.5 m, from 3.5 m out (the
  queries meet the mechs, his own body too), to 6 m short of the leader.
  A thin line at 4 m said clear where a ledge over 6 m, or a corner past
  his width, stopped him.
- **Which crumb:** the nearest in sight first, then crumb by crumb,
  skipping ahead only to one in sight within 30 m; reached at 6 m. No
  progress toward it in 3 s: back one, and no skipping for 6 s.
- **Steering** to a crumb is straight, as the veer state's (0x4d16f0's
  turn, walking only within 60 degrees): the carrot's slack and the
  contacts' push swung him round a near point.
- The W17 detour (now `WallDetour`) stays the fallback with no crumb in
  sight, and also runs in the fight's approach (0x4d1d10, the AI's own
  attacks). "Attack my target" is the target's follow (0x4ea450), so it
  has both.

Tests:
- `mission_c2_dominic_follows_the_players_way_round_corners_headless`: the
  player walks a mech-wide route (A* on a 10 m grid, each step clear by
  the same lines 6 m each side, rising at most 3 m a cell) round the
  nearest wall, about 8 m/s. c2m1: in range 7 s after the player stops
  (the exe never, 88 m at the end); c2m4: 11 s (the exe never, 92 m). On
  these routes the exe's stop is the standstill on open ground, not a wall
  contact (the maps of the run: `MW3_W27_TRACE=dir`).
- `mission_c2m1_dominic_attacks_round_a_wall_headless`: told to attack an
  enemy 80 m past the wall nearest him, he walks round (2 detours) into
  range by frame 1547; the exe stops at 98 m.
- The same test's sharp corner (the lead's): the player walks along the
  face of the wall nearest Dominic, 12 m past its end, turns 90 degrees
  behind the building (14 m past its back face, found by a probe from
  behind) and on 90 m along its far side; Dominic starts 25 m behind him.
  Done when he is in range with the player in sight. c2m1: 6 s after the
  player stops (the exe at once); c2m4: at once, both. On these buildings
  the exe's straight follow cuts round them too: the test keeps ours from
  standing at a corner, but it does not make the exe's perpetual push
  against a wall.
- The W17 test unchanged.

"He popped straight through it" once the enemies were dead: not
reproduced. c2m4 has no teleport valve (the shipped files' five are in
c2m1, thor_01, and c3m2's ambush). The contacts' bump (Mech::collide,
0x4225c0) moves a mech within 9 m of another with no look at walls; a
setup with Dominic at a wall and the player behind him did not push him
through (the wall response held him). Open: Owen's place and what he did.

### 17.6a The pocket commander (mech_pocket.cpp)

The lance order 2 (key 0x4f) puts the player's controls in the "pocket
commander" mode ("leftover pocket commander", the file's reports) and
takes them out again. Ported (2026-10-22, item L4c1: `mech3/mech_pocket`,
`PocketControls`, `pocket_enter`; the dispatcher's order 2 and its paths
in the mode; `MechControls` gained the slots below):

- **The controls** (0x4690d0, 0x1dc bytes, over the player's 0x427320
  with mode 1; vtables 0x5c2fb0 / 0x5c2fa8 / IControls 0x5c2ef0): +0x150
  the camera mode before (-1), +0x154 bit 0 lets the jumps and the trigger
  be, +0x158 the old controls, +0x15c the player controls' instance
  [0x6056ec] before, +0x160 the order 2, +0x168 the cursor's place, +0x178
  the display's width less 0x8c and +0x17c 0x5f (the panel's place), +0x180
  the panel, +0x188 a contact (the template 0x5e9d10), +0x1bc a network
  game's element (0x58b060 of [0x609980] +0x20, 0 outside one); the old
  controls' inputs are copied in (the throttle's state, the axes, the
  trigger) and back out when it ends. [0x605f50] / [0x605f60] are cleared.
- **In** (0x469940's order 2 when the controls' primary vt+0x08 answers
  none): none without a mech; the instance taken, the mode made and made
  the instance, its owner the mech (IControls vt+0x08 0x46a440: the old
  panel freed, the cursor reset, the panel made (0x468250) and the HUD's
  hold let go (0x4484b0(0): the weapons' and equipment's vt+0x64)), the
  view's pocket on (0x4b5290(1)), the camera mode kept and set to 5 (the
  area camera, 0x40ae70), the view state 2 (0x426780), the viewed HUD's
  reticle flag (+0x144c +0x90c) on and its image `ret_on` (0x59bb50 on its
  +0x920), [0x605f64] the mode. The controls the mech then uses.
- **Its frame** (IControls vt+0x0c 0x46a360): the panel's (0x469d00, then
  0x46b0a0; not ported, L4c2); unless +0x154 bit 0, no jump, no jump-jet
  axes and the trigger off. The mech is not driven meanwhile (no 0x427d60).
- **Its keys** (primary vt+0x1c 0x46a860; nothing while powered down):
  0x1b..0x1e pick lancemates 1..4 (the panel's 0x469060: a unit's +0x30
  bit 1 toggled, its vt+0x44 / +0x48), 0x1f..0x23 nothing, the rest the
  player's (0x4274d0).
- **The orders to those picked** (item L4c3, `pocket_order` in
  `mech3/lance_orders`): any lance order but 2 given in the mode goes to
  every unit on the panel (its +0x53c list, +0x70 of them) that is picked
  (the unit's +0x4b0 bit 1) and has a mech (vt+0xb4, vt+0x2c, vt+0x40) and
  a brain (vt+0x140, vt+0x54), each in turn, none skipped when one takes
  it. Follow-like orders (0x468f00 over 0x468c00) go on each one's contact
  on the player (+0x98; none: skipped); target orders (0x468fa0 over
  0x468e20) on a contact made from each on the player's target, else the
  panel's cursor [0x605f5c] (0x4e6ee0). The answer: 1 when any took it,
  else 0 when any refused, else 2 (the record's flags +0xc / +0xd, made by
  0x468bd0 / 0x468df0). Unused by the dispatcher, which leaves the mode
  either way.
- **The command target marker** [0x605f58]: an AiTarget (0x4d7f80, 0xd4
  bytes) made at the mission's start (0x46aaa0 -> 0x468100) on the world's
  `cmd_target.flt` node (else a new object3d), named, its team the
  player's side (0x4d5af0) with the masks below; it starts inactive (+0x08
  0). When the panel's cursor meets the ground and no unit, the panel
  moves the marker there (its vt+0x18), activates it and makes it the
  cursor [0x605f5c] (0x469d00, L4c2), so target orders can name a spot.
- **The team change** (0x468ca0, before each target order): when the
  target is the marker and the order is 10 (0x4ea200, only an enemy) or 11 (0x4ea230, only an ally), the
  target's team (+0xc0) becomes the lowest bit of the lancemate's enemies
  (+0xc8) or allies (+0xc4) (none: bit 31, through the 5-bit packing's
  loop), its allies (+0xc4) that team but bit 0, its enemies (+0xc8) every
  other bit but 0, and [0x60b56c] is cleared. So the spot counts as an
  enemy (or an ally) of the lancemate the order goes to; a real unit's
  side is never changed here.
- **Out**: the order 2 again, or any lance order given in the mode (it
  goes to the lancemates picked, above, then out), or the frame's end
  (+0x38 set by 0x469d00; the mech's update then calls primary vt+0x04,
  0x469cf0: the dispatcher on +0x160): the instance given back, the old
  controls' owner the mech and their control mode set again (0x426650),
  the view's pocket off, the camera mode back, the HUD's flag off, the
  inputs copied back, the mode deleted (0x469310: the HUD's hold back,
  0x4484b0(1); the panel freed, 0x468720).
- **The mech's death** (IControls vt+0xa8 0x46abb0): out, then the old
  controls are the mech's again (0x491120). **The mech going** (primary
  vt+0x18 0x46ad80): out, the old controls asked the same; the exe deletes
  the mode there and answers yes, so the mech, whose controls are still
  the mode (SetOwner sets only their owner), deletes it again: FIX (bug),
  the mode answers yes and the mech deletes it once.

Test `test_pocket.py` (the exe's 0x469940 in and out on a made-up mech,
view and HUD, the old controls built by its 0x427320; the panel, view,
camera and HUD calls stubbed; the port on a real Mech,
`tests/probe_pocket.cpp`): 400 cases (the inputs, the instance, viewed or
not, a HUD or not, powered down, +0x154, keys) identical, the calls, the
mode's fields and the inputs back; 8 of 8 breaks. `test_fixes`
fix_pocket_commander_freed_once, `test_lance` the death.

Test `test_pocket_orders.py` (the exe's 0x468f00 / 0x468fa0 on a made-up
panel, units, mechs and target, the order wrappers stubbed with answers
from a list; the port's `pocket_order` through `PocketCalls::panel_units`
and `LanceCalls`, `tests/probe_pocket.cpp`): 3,000 cases identical, the
orders, contacts, answers and the target's teams; 7 of 7 breaks. The panel
that fills the units is L4c2; until then the program has none.

### 17.6b The pocket commander's panel (mech_pocket.cpp, pocket_panel.cpp)

The mode's panel is a zTwoD window drawn at the top right (x the display's
width less 0x8c, y 0x5f, lines 12 high), made by the mode's set_owner and
freed with it. Ported (2026-10-22, item L4c2a: `mech3/pocket_panel`):

- **The panel** (0x468250, 0x548 bytes, vtable 0x5c2e68 over the Window
  0x5a2b50(1, 1)): +0x58 the mode, +0x5c..+0x68 the lines' box (its right
  the widest line's, its bottom (+0x74 + the orders) lines down), +0x6c the
  line height, +0x70 the lancemates counted (zEntity's list
  [0x73655c..0x736560], each entry's vehicle (vt+0x2c) under orders, +0xbc
  bit 4, and up, +0x08), +0x74 that or 3, +0x78 how many are picked, +0x7c
  the target line, +0x53c the lines.
- **The lines** (0x4bc bytes, a FontText (0x5a00c0) in `ia_small_grey`,
  0x4674f0): +0x4b0 the state (bit 0 the mouse over it, bit 1 picked, bit
  2 lit) picks the colour [0x605ee8 + 8 * state] (made once by the first
  panel, 0x4686e0: grey for 0..3, green, olive, yellow, red; two more at
  0x605cc8 / 0x605cd0 for the panel's draw), +0x4b4 the panel.
  - The target line "   target" (0x467620, its unit none; state 7, red).
  - One line per lancemate (0x467fa0): under orders, up, seen by the
    player (+0xbc bit 3), of his side (its allies, +0xc4, share his team)
    and named (vt+0xa4): "%3d %s", its place in the list and its name. The
    list then holds +0x74 places (empty ones for the missing).
  - One line per order the player has a key for (0x4680c0, 0x4679a0;
    "%s %s", the key's name and the order's message), from the table at
    0x605cd8 (0x54 bytes each, [0x605f54] of them, made at the mission's
    start by 0x467d80 from 0x5e5050's six rows: the actions 84, 85, 82, 81,
    86, 79 with the messages 2195, 2196, 2142, 2143, 2197, 2204 and the
    orders 10, 11, 0, 1, 12, 2). Order 2 (the mode's own key) gets no line.
- **Lit**: a lancemate always; an order (+0xb0, re-asked whenever the picks
  change, 0x4687e0) by its kind: 0 / 1 / 8 when one is picked (0x46a7b0);
  3 / 10 when one is picked and the cursor's entity (0x40eee0 on the mode's
  +0x164) is the marker or not the player's ally (0x46a5a0); 4 / 11 the
  marker or an ally (0x46a670); 5 / 9 a spot (the mode's +0x168, 0x46a740);
  7 / 12 exactly one picked and a repair bay up (0x45a000, 0x46a810).
- **The mouse**: over a line its bit 0 and the hover sound `pckt_rollover`
  (0x4675e0); a lancemate pressed or the number key (0x469060 toggles the
  line's +0x30 bit 1) picks it or lets it go (0x4676c0 / 0x467710: the
  count, the orders re-lit, `pckt_select`, the mode's +0x184 = 2); a lit
  order pressed (0x467c90) puts it in the mode's +0x160 and +0x184 = 1 (the
  mode then leaves with it, 0x469d00, L4c2b), a dark one +0x184 = 2.
- **The mission's start** (0x46aaa0): the seven sounds (0x46a930:
  `sure_sir`, `right_away`, `solong`, `noway`, `cannot`, `pckt_select`,
  `pckt_rollover`), the marker (0x468100, above), the orders' table. Its
  end (0x46ab10 -> 0x46aa40): the sounds forgotten, a mode left up deleted,
  [0x605f50], [0x605f60], the marker cleared.
- The orders over the picked (17.6a) read the first +0x70 places: a
  lancemate's line picked (+0x4b0 bit 1) and its unit (+0xb4, 0x467690).

The frame is 17.6c; the panel's draw 0x468900 and the lancemates' 0x467760
are L4c2c.

Test `test_pocket_panel.py` (the exe's 0x468250 and the lines' virtuals
on twod_world.py's zTwoD outside, a made-up mode, player, entity list,
cursor, repair bays, sounds and colours; the port on the ported zTwoD,
`tests/probe_pocket_panel.cpp`): 200 runs of the build and a random plan
(picks, mouse calls, re-lights, the orders' draws, the count) identical in
the calls out and the dump of the panel and every line; 0x467d80's tables
identical on 200 random sets of bindings and messages; 16 of 16 breaks.

### 17.6c The pocket commander's frame (0x469d00)

Each frame of the mode (its update 0x46a360: this, then its own keys and
camera 0x46b0a0, L4c2c) runs the panel. Ported (2026-10-22, item L4c2b:
`pocket_frame`, `PocketPanel::route`):

- **The mouse over the lines** (0x468820): the zInput mouse (0x51ba60)
  into the panel's +0x14; a line under it entered (+0x30 bit 0, its
  vt+0x3c) or hovered, a left click (bit 2) toggles its press (+0x30 bit
  1, vt+0x44 / +0x48) then its left-up, a right click its right-up; a line
  the mouse left is left.
- **The map**: when the player is the view's mech (+0x64) his HUD
  (0x430aa0); its +0x70 == 1 (the mission map shown) makes +0x1b8 the map
  (0x491c80 on 0x6098d0), copied to [0x605f50] and [0x605f60]. FIX (bug):
  the exe reads that +0x70 through null when the view is on another mech.
- **The cursor** (skipped while an order's press is pending, +0x184):
  - The trigger let go (command 0x3d, TRIGGER1; 0x467480 bit 2, released
    this read, input.md): with the map
    up and the mouse inside it (0x444670 on the reticle, the map's +0xd8
    size) the map's pick (0x4587b0, the spot into +0x16c), else the
    reticle's (0x444330(0), its spot +0x8fc into +0x16c). A pick with a
    node makes it the cursor (+0x164, the place +0x168 = +0x16c, its entity
    0x40eee0 into [0x605f5c] and the target line, "   %s" with its name
    (vt+0xa4) or message 0x89d) and makes that entity the player's target
    when he has a contact on it (0x4a7b40). A bare spot (no entity): the
    marker [0x605f58] goes there (vt+0x18), is shown (0x4eb170(node, 1),
    +0x08) and is the cursor; else the marker is hidden (even when it is
    the cursor). No pick: nothing changes.
  - Else the player's target contact (+0x6c), when it is not the one the
    mode copied last (+0x188 / +0x190, its owner and entity): its entity
    (0x4e6fe0) the cursor with its place (0x5a85e0), +0x1c0 = 0, +0x1c1 =
    1, the marker hidden when it is another.
  - Else, in a network game, a new element (0x58b060 of [0x609980]
    +0x20, kept in +0x1bc; +0x1c0 = 1, +0x1c1 = 0): its +0x18's entity
    (vt+0x2c) the cursor, else the marker at the element's place (vt+0x18).
  - When the cursor moved (or no target), the orders' lines are re-lit
    (0x4687e0(0)).
- **The aim**: the player's target contact copied into +0x188 (none: the
  template 0x5e9d10).
- **The way out** (+0x148): when an order's line was pressed lit (+0x184 =
  1): three frames (+0x14c), the view's pocket off (0x4b5290(0)), the
  jumps and trigger left alone again (+0x154 bit 0) and the camera mode
  back (0x40ae70), one frame, then +0x38: the mech's update unwraps
  (0x469cf0: `mov edx, [ecx+0x160]; jmp 0x469930`, which is `push 1; call
  0x469940`: the dispatcher on the order the line put in +0x160, its
  stack argument 1 the mode order 2 would make, read nowhere else), so the order goes to the lancemates
  picked (17.6a) and the mode ends.

Program: the mission's start and end run through the load's listeners
(0x4e7b40's slot +0x08, 0x4e7bc0's slot +0x10), and the orders' keys come
from zInput (an action's first key and its name). The frame's hooks are
wired in `app_pocket.cpp`: the HUD's pick (the reticle's record, node and
spot), the entities' names and places, the marker's making (0x468100) and
moving, and since 2026-10-30 the map pick. With the radar's map up
(Shift+R, the HUD's +0x70) the frame reads the mission map (0x491c80, the
missions object's +0xbc), its size (the image rect's right and bottom),
the cursor (0x444670: the reticle's vt+0x68 / +0x6c) and, when the cursor
is inside it, the spot under that pixel (0x4587b0: x and z back from the
map's offset by its metres a pixel, the ground from 1000 m up through
0x4edab0 on the world [0x608c84], the hit's node). Before that none of the
four was set, so the frame never saw the map up and a click on it gave the
order at the reticle's aim. `test_map_pick.py` checks 0x4587b0 against the
exe (3000 picks, bit for bit; swapping x and z breaks 2583);
`test_program` mission_c1m4_pocket_commander_sends_dominic_to_a_map_spot
orders Dominic to a pixel of c1m4's map.

Test `test_pocket_frame.py` (the panel test's world plus the view, the
HUD, the map, the pick records, four entities and the marker, the
trigger, the target contacts, a network game's elements; the probe's
frame hooks answer through the trace): 200 runs of frames with the mouse
over the lines among the other ops identical in the calls and the dump of
the panel and the mode, 11 branches reached; 16 of 16 breaks.
`test_fixes` fix_pocket_frame_without_a_hud.

### 17.6d The pocket commander's own keys (0x46b0a0, 0x46af30)

The mode's update (0x46a360) drives the mech itself after the panel's frame
(17.6c), in place of the player's controls (whose update 0x427d60 does not
run meanwhile). Ported (2026-10-22, item L4c2c: `PocketControls::keys`,
`mouse_look`). It is 0x427d60 with these differences:

- No axis table (no joystick, no hat): the mouse look 0x46af30 instead,
  only with the mouse on (+0x08 bit 0): by the view state (+0x0c) its
  scaled motion (0x51b7b0 +0x24 / +0x28, times 1/dt and -0.26 / -0.16)
  into the twist and pitch (view 1 at 1.5x, into the torso, not the free
  look); view 2 keeps the pointer's place (+0x140 / +0x144) and its motion
  (+0x138 / +0x13c) and gives none; view 3 holds the pointer there
  (0x51b440). The other inputs are left as they were.
- The keys: throttle 0x18 / 0x19 (0.025 a frame, clearing +0x3c / +0x3e;
  FIX (B) per 1/30 s as the player's), turn 2 / 3, the jets 0xc..0x12 as
  there, twist 8 / 9 at 1 (the player's 1.5), pitch 4 / 5, the trigger
  0x3d; the viewed HUD's zoom box (+0x1508: vt+0x60(-1) held, vt+0x64 let
  go, unless locked open, +0x167c) with no click sound.
- The throttle as there, but the joystick's path calls SetThrottle (vt+0x20)
  with the step (always 0 there: the keys clear +0x3c), and no free look.
- Pitch and twist ease toward their inputs at 10/s (0x5e50b0; the player's
  6/s), snapped within 0.05. Then the pilot's tick (vt+0xa4, 0x4d8990)
  first, the smoothing (0x426960) last, as there.

Test `test_pocket_keys.py` (0x46b0a0 on the mode the exe's dispatcher made,
test_pocket.py's world plus the commands, the mouse, the jets, the HUD's
zoom box; the port's `keys` on a PocketControls, `tests/probe_pocket.cpp`):
2,000 cases identical in the calls and the fields after, 9 branches; 12 of
13 breaks, the 13th (the joystick path's SetThrottle as a plain store) is
the same behaviour, since that path only ever sees 0.

### 17.6e The pocket commander's draws (0x467760, 0x468900)

Drawn in the HUD's overlay pass (0x40fac0 → 0x46a410: with the mode up,
the view prepared for projections (0x4b51f0), the panel's vt+0x00
(0x46a3f0), then the mode's pending press +0x184 cleared). Ported
(2026-10-22, item L4c2d: `PocketMateItem::draw`, `PocketPanel::update`,
`pocket_draw`):

- **The panel** (0x468900): the clip [0x605ed8] made from the screen's
  rect (0x40d7b0) less a pixel at the right and bottom; the lines drawn
  (the Layer's 0x59fd20); then the cursor marked with a triangle (a
  Shape, 0x5a3170; vtable 0x5c2e70, the Shape's but for its destructor):
  on the cursor's entity (0x40eee0 on the mode's +0x164) in yellow
  ([0x605ccc]) at its node's place, sized fy ([0x73fba0]) x its depth x
  its size (+0x68) x 2 / sqrt(3) (x 2 with doubled pixels) when it is on
  the screen (the projection's bit 3 clear, depth in (0, 1]), else (or on
  the mission map, [0x605f60]) a point; or, without an entity, on the
  cursor's spot (+0x168), 13.86 across, yellow when its node is marked
  (0x40ee50: the node's +0x28 bit 21), else white ([0x605cd4]).
- **A lancemate's line and the target line** (0x467760): drawn in their
  state's colour (as an order's, 0x467d40), then a line, clipped to
  [0x605ed8], in the colour's pixel, from the text's middle (its top and
  bottom halved) at its left edge, or its right edge once the unit's point
  is past it, to the unit's point on the screen (0x53cf10; on the mission
  map in pixels, 0x4586e0, when [0x605f50] holds it); the target line's
  unit is the cursor's entity (+0x4b8 written by the frame).

Program: the overlay pass calls `pocket_draw` after the HUD's update; the
places (the brain hooks' position), sizes (AiTarget +0x68), the screen
rect, the projection and the view's preparation are the HUD's. Not wired:
the map's pixels and the node mark (with the cursor picks, 17.6c).

Test: `test_pocket_frame.py` draws the panel among the frames ('D': the
exe's 0x46a410 against `pocket_draw`), with the entities' nodes, sizes,
the projection (its depth 0.5, 0, 1.5 or 1 and the off-screen bit), the
map's pixels, the screen and fy added: 200 runs identical in the calls
(the lines and the triangles' points) and the dump (with the clip), 18
branches; the draws' breaks 13 of 15, the other two the same behaviour
(the edge test's first half, implied by the second; a depth of 0, whose
size is 0 either way).

### 17.6f The pocket commander in the program (app_pocket.cpp)

Wired 2026-10-22: the mode works in play on the HUD.

- **The marker** (0x468100, made by the mission start's listener): the
  world's `cmd_target.flt` node or a new object3d so named, under the
  world; a `PocketMarker` (an AiTarget) on it, its node bound (+0x28 bit 16,
  owner), named message 0x89d, on the player's side, in zEntity's list;
  shown and hidden through its node (0x4eb170) and +0x08, moved by its
  node's translation (vt+0x18). The previous mission's is freed when the
  next is made.
- **The AI on it**: the brain hooks take the marker as a target (its
  AiTarget, its node's place, up, still, health 100), so the orders on it
  work as on a mech (its own net, the follow). The sensors ask it what any
  entity is asked (2026-10-25, item 7): a plain AiTarget (vtable
  0x5ccfe0) answers itself to vt+0x24 / +0x28 / +0x2c (0x4d2a70), 0 to
  +0x40 / +0x50 / +0xa0 (0x40bae0) and +0xb4 (0x4d7290), +0x08 to +0xcc
  (0x4d7200), 1 to +0xd0 (0x4d7210); its vtable ends at +0xe8.
- **The picks**: the viewed mech's HUD (0x430aa0), its radar's map flag,
  the reticle's aim record (+0x928, its node) and spot (+0x8fc), the
  trigger (command 0x3d), a node's entity (0x40eee0), an entity's node and
  place, its name (the pilot's, the brain's, the marker's), the target
  pick 0x4a7b40 (the contact on it, then TARGET on the view's HUD), the
  node mark (bit 21); the lancemates' names (the brains' pilots).
- **Not wired** (their code is not ported): the mission map (0x491c80 and
  its pixels 0x4586e0, the map's mouse 0x444670 and pick 0x4587b0), so the
  picks on the map do nothing; a network game's elements (0x58b060); the
  repair bays (0x45a000: the refit order's line stays dark); the view's
  pocket layer (0x4b5290).

Test `mission_c1m4_pocket_commander_sends_dominic_to_a_spot_headless`:
c1m4 with the player 40 m off Dominic (in sight, so his line shows): the
mode opened with its key, Dominic picked with the number key, the
program's reticle pick checked against the real HUD, then the cursor put
on a bare spot 200 m off with the trigger let go: the marker placed and
shown; "only an enemy" (order 10) on it: the marker one of Dominic's
enemies, his order the follow on its net for 902 frames, and he closes
from 199.35 m to 120.73 m, his reach of an enemy (exact, the fixed seed).

### 17.7 Firing (0x4b5b80, the firing state +0x108)

A state machine over +0x00, run until a step asks for no more: 1 / 6 pick a
plan (+0x30, else +0x50, else 0x4b6790: the weapon manager's volley,
0x4b5a20 -> 0x4b1860 in weapon.cpp), 2 aim (the plan's vt+0x10 / +0x04
with the delays +0x08 / +0x0c), 3 lead and lock (0x4b72a0, 0x4b77e0,
0x4b0a70 alpha for 0.2 s when the plan's kind +0x10 is 3), 4 fire (the
trigger IControls vt+0x10(1), 0x4b4ba0 the type's wait, 0x4b5940 the
contacts in the line), 5 the wait (0x4b63f0, the parts' 0x465a00), 7 the
burst's end, 8 reset (0x4b6700 is the reset from the brain). The weapon
manager's side (0x4b04c0, 0x4b0a70, 0x4b0bc0, 0x4b4ba0, 0x4b4c90) is
ported (weapons.md), and so is the planner's volley pick 0x4b1860 (below);
its start-up object 0x4b1170 is read by nothing.

**The machine, ported** (2026-10-22, `mech3/zai_firing`: `Firing::run`,
`stand_down`, `LookAround`; item 12a). The firing (0x58 bytes at the
brain's +0x108): +0x00 phase, +0x04 time, +0x08 / +0x0c the aim delays
(quick or not), +0x10 the rest after a drop, +0x14 the plan, +0x18 /
+0x1c / +0x20 the plan's arguments for reach, tracking and firing, +0x24
the sensor range (copied at the start), +0x2c the brain, +0x30 / +0x50 two
plan sources, +0x34 the look-around, +0x54 the aim. A pass runs until a
step asks for no more; nothing without a weapon manager (+0xd4).

**The random draws.** The volley pick (0x4b1860's pick_index) and the
other random choices draw on MSVCRT's one `rand()`, which the exe seeds
from the clock (`srand(time(0))`): at the mission's start in zDEClient's
init (0x501ab0) and zEffect's (0x508fc0), and on a multiplayer host's lock
(0x49aca0, protection.md). So no two runs draw alike, and a mech's volley,
with its heat and then its speed cap (heat over 0.6 slows it), varies from
run to run; in c1m4 Dominic's first shots add 0.034 or 0.19 heat by the
second the mission starts in. The port keeps the clock
(`crt::msvc_srand`); the headless tests set `crt::rand_fixed_seed()`
(reset_process_state) so a run repeats bit for bit and its outcome can be
checked exactly.

- 8, the start: the range copied, the look-around started; on to 1.
- 1 / 6, a plan: the first source's (freed when it says it is spent), the
  second's (always freed), else the volley pick (0x4b6790, item 12b).
  None: dropped (0x4b6690: the trigger let go, the rest from now, the plan
  freed, the manager's aim cleared), the brain's vt+0x14 (+0xf8 = 0), to
  5. Else it is tracked once and the aim's wait set: the quick delay, or
  the delay with the AI controls told (IControls vt+0x80, 0x4d8930: with
  scan_aim on and not attacking, a manoeuvre every aiv +0xa8 s); to 2.
- 2, aiming, and 3, tracked: no target (vehicle +0x6c): dropped. Tracked
  and in reach (and, in 3, ready): 2 goes to 3; 3 aims (0x4b72a0) and
  either strikes (kind 3: the lead 0x4b77e0, an alpha strike of 0.2 s,
  then a rest of aiv +0x38 + +0x34) or goes to 4 with the time now + aiv
  +0x34. Not yet, once its wait is over: still in reach to fire (and not
  a lancemate under orders, +0xbc bit 4; in 3 ready) it aims and bursts
  (7, until now + aiv +0xac), else it is dropped.
- 4, firing, and 7, the burst: no target ends it. 4 fires while the
  weapon's type may fire again (0x4b4ba0, the LRM wait), its reload has
  not run out before the time, it has ammo and the plan tracks; 7 while
  the time has not passed the reload. A shot needs the plan in reach, its
  line flag (+0x0d) and a clear line (0x4b5940 over the vehicle's
  contacts, nearest first: the first in a cone around the line to the
  target blocks it unless it is the target; the cone is 10 m wide to 30 m,
  then narrows to 10 m at 200 m, [0x60b2c8] from 0x4b5860 at start-up):
  then the lead, the weapon type's flight hold (0x4b4c90) and the
  trigger (IControls vt+0x10(1)); else the trigger let go. When it may
  not fire: the trigger let go, rest aiv +0x38, the plan freed, to 5.
- 5, resting: the torso recentred (0x465a00(1)) each pass; while resting
  the look-around (0x4b63f0) turns the torso's gaze back, left (2.5 s),
  right (5 s each) and again; when the rest is over, to 6.

The plan's slots (+0x04 in reach, +0x10 track, +0x18 ready) answer
bytes: in reach is taken when 1, tracking when not 0 (when 1 before a
shot), ready when not 0. The brain's 0x4e9590 (a hit lately) is
`stand_down` (0x4b6700). Test `test_ai_firing.py` (over test_ai_frame.py's
brain; fake plans and sources, the weapon manager, weapons and their
types, the volley pick and the aim answered from the trace; six contacts
with the exe's own distance 0x4e7030): 300 cases of 60 calls, every phase,
13,513 runs, 2203 stand-downs and 2284 scan_aims identical, 34 branches;
36 of 36 breaks caught.

**The volley plan pick, ported** (2026-10-22, `volley_plan_pick`,
`VolleyPlan`; item 12b1). 0x4b6790 (vehicle, firing): the vehicle's first
256 contacts become 16-byte candidates (where the contact is kept, a score,
two flags), scored nearest first by 0x4b6b30. A candidate is rejected (the
record [0x60b2b8], removed after) without a live, active entity (0x4e6fe0,
+0x08), when nearer than 1 (squared), when past aiv +0x30 (and every one
after it), when an ally (the vehicle's +0xc4 against its vt+0x28's team
+0xc0), when more than 0.5 up or down, or when it lies near the line to the
firing's remembered obstacle (+0x28: within 10 m, or the square of its
distance from the line under 400; the obstacle is forgotten at the first
that is not). The score starts at the squared distance (100 within 40 m),
times ((2 - its facing) x 0.25 / (0.25 - its pitch squared)) squared,
doubled when the vehicle cannot face it (vt+0x8c), times (health + 0.5) x
2/3, times (1.5 - heat) x 2/3, ten times when the line to it is not clear
(0x4d5980). Lower is better: a partial sort to one place (0x4b79e0 with
0x4b5b60) swaps each better one to the front; then the vehicle takes the
first it can as its target (0x4b5a80: vt+0xc0, AiTarget's 0x4d5e40). Taken,
the sensor range is halved but kept past the target (its distance + 128,
at most +0x30, 0x4d6d00); none, doubled up to +0x30. The plan (0x24 bytes,
vtable 0x5c9460) keeps the target slot (the vehicle's +0x6c), the score,
the face flag (aim with the short delay), the clear-line flag, the kind the
volley picked (0x4b5a20: the weapon bearer's vt+0x40 manager's 0x4b1860,
item 13) and the bearer. Its slots: in reach the bearer's vt+0x150 == 1,
ready the clear line asked again, slot 0 itself while its target lives,
the line to it is clear and the volley still picks one (then tracked at
1.0); track (0x4b6e30) is item 12b2.

**Heat in the pick** (2026-10-25, item 10): a target's heat (vt+0x9c) runs
from 0 to 2, so the factor (1.5 - heat) goes from 1.5 down to -0.5: a
hotter mech scores lower, and past heat 1.5 its score turns negative and
beats any cool one whatever the distance (vehicles, turrets and targets are
always at 0). In the program the brain's heat question went unanswered
until item 10, so every pick ran as if all were cold.
`mission_c2m1_ai_pick_goes_to_the_hotter_mech_headless`: an enemy with the
player and Dominic 200 and 210 m off on either side picks one while both
are cool and the other once that one is at heat 1.4.

Test `test_ai_plan.py` (over test_ai_frame.py's brain and vehicle; four
entities at fixed addresses with their active byte, team, health and heat,
six contacts on them, the vehicle's questions, the clear line, the volley
and the plan's track answered from the trace; obstacles placed near the
lines): 300 cases of 20 worlds: 6000 picks (940 plans), 2927 in-reach,
2961 ready and 5874 takes, the calls, the plan, the ranges, the obstacle,
the vehicle's target and the contacts' distances identical at 24-bit x87;
23 branches. 31 of 31 breaks caught (one more, the bearer check, is
equivalent: the volley asks the bearer again).

**The plan's track and the lead point, ported** (2026-10-22, item 12b2a:
`VolleyPlan::track`, `lead_point`; `Torso::face_angles`, `update_facing`,
`Mech::can_face`, `clamp_aim`, `faces_within`, `set_torso_aim`,
`lead_velocity`). The track (0x4b6e30, its argument unread): the target's
mech aims others at its last hit part (0x41d130); the bearer's selected
group's weapon (manager +0x28, +0x2c[], +0x10); its lead point (the
bearer's vt+0x84, 0x4d6de0); the way to it from the weapon's node
(0x4ed530), unit, turned by the bearer's matrix (+0x0c), clamped (vt+0x90,
0x41dca0) and given the torso to aim along (vt+0x154, 0x41dbd0: the AI's
aim in the world's frame, the facing again, 0x465ad0). The lead point
aims a beam or an instant weapon (def +0x6c 0x800 or bit 1), or one of no
speed, at the target's own target node (0x5a8630); else the time t to
meet solves (s^2 - v.v) t^2 + 2 d.v t + d.d = 0 for the relative
velocity v (each vehicle's vt+0x0c; a mech's 0x41d400: airborne its world
velocity, else its forward at the throttle's share of its top speed) and
the way d from the muzzle: d.d / (2 s d.v) with no square term, |d| / s
when v is nearly still (v.v under 0.001: the point then the target's
own), else the smaller positive root, through a fast square root (the
float's bits halved plus 0x1fc00000); t must come out above 0, and the
point is the target's plus t v. The torso's in-reach (vt+0x150, 0x41dba0)
is its facing (+0x98) above the plan's argument; can-face (vt+0x8c,
0x41dc70) is aim_angles of the direction turned by the mech's matrix
(0x465c70). Tests: `test_parts.py` (the five mech and torso functions,
ops 24-28: 20,000 trials identical, 11 of 11 breaks caught) and
`test_ai_plan.py` (the track and the lead point on the real slot: 5235
tracks identical, 32 branches; 14 of 16 breaks caught, the two not caught
being exact ties: a double root and a time of exactly 0).

**The aim error, ported** (2026-10-22, item 12b2b: `Aim`, at the
firing's +0x54). The pilot does not shoot at the target itself but at
its position plus an offset across the line of sight, which drifts. Its
size is the pilot's +0xb8 times the difficulty's second float
([0x608e58]: low 1.2, medium 1.0, high 0.6), times the pilot's +0xc0 when
the vehicle's mech (vt+0x50) carries a targeting computer (+0x3e4) or a
fire control system (+0x3b8) (0x4b70b0); zero means no error (+0x18 off):
the manager is aimed at the position (0x5a8630) itself. The frame is the
unit way from the target to the muzzle (row 2), the unit axis least along
it less its share along it (row 0) and row 2 x row 0. The track (0x4b72a0,
before a shot) puts the offset at r = (0.6 to 1.0, rand) x size x 0.02
([0x60b2ec], 2/100 at start-up 0x4b7030) x the range, at a random angle in
the plane across the line of sight; the drift (0x4b71a0) then points back
over the target within 45 degrees, at size x 0.02 x range per second,
and that is the limit too (+0x20, squared); +0x1c = now + 0.5 (unread).
The lead (0x4b77e0, each shot) sets the manager's +0x88, moves the offset
by drift x dt and aims the manager (0x4b0bc0) at the target's position
plus it; past the limit it first re-picks (0x4b74f0): the aim point
carried along its line from the muzzle back to the target's range, the
offset taken from there (the unit axis when that is exactly 0), and a new
drift back over the target; a point behind the muzzle drops the offset.
So the shots sweep across the target, at a medium pilot of +0xb8 = 1 to
within 2% of the range (a 2 m circle at 100 m). Test: `test_ai_aim.py`
(7079 tracks and 16072 leads identical over 300 cases; 10 branches, the
re-pick's exact-zero offset not reached; 23 of 23 breaks caught).

**The volley, ported** (2026-10-22, item 13: `mech3/weapon_planner`
`volley_pick`). What the AI fires (0x4b1860, from 0x4b5a20 on the plan's
target): the pilot is the brain behind the manager's vehicle (+0x84: vt+0x2c,
vt+0x40, the controls vt+0x140, their brain vt+0x54), read for its +0x34
(params 8: the seconds a burst holds the trigger, as the firing's phase 3
waits) and +0x90 (p8 item 12: the alpha share). The heat budget is 0.9 less
the owner mech's heat (+0x554) x 2/3, or less 1 above 1.5. Each weapon
(0x4b1470 over the manager's list) makes a shot record (0x4b1370: its group,
heat = the type's HEAT, times the hold for a timed-damage beam; damage =
+0x18 (damage a second) x the hold when ready (0x4b4ba0, 0x4b3a20) and the
target's contact +0x0c (distance squared) is within its squared ranges
0x4b3990 / 0x4b39e0); a record with damage, a speed (beams and instant
weapons count as fast), in range and within the budget is a candidate; its
group's record sums the heat and damage of every weapon in it either way.
The roll r = rand(): below +0x90 x 32767, and with the summed heat below
the budget and damage above 0, an alpha strike (3, single fire set), but the
sums are doubled rather than added (0x4b17a9) and stay 0, so never; below
that plus (1 - +0x90) x 26213 x 32767 (nearly always) a random group within
the budget that deals damage (0x4b1440 / 0x4b17d0, in order; rand x n >>
15), group fire set (+0x1c = 2) and the group selected (0x4b0730): 2; else
a random candidate, single fire set, its group selected and the weapon in
it (0x4b1de0): 1; nothing: 0 (fixes.md, two candidates). Test:
`test_weapon_planner.py` (after every operation on test_weapons_run's real
managers, 200 managers: 14378 volleys identical, the manager's state and
calls included; 17 of 20 breaks caught, the other three equivalent on the
shipped weapons: a heat cap of 1.4 (both budgets then below 0), an
instant weapon's speed (none has speed 0), the group roll's 26213 (it
matters only within 1 of the roll)). In c1m1, 30 m off, the Owens picks a
group volley and tracks the player, but the plan's ready (the clear line
0x4d5980, not ported) answers no, so it does not fire yet.

**The clear line, ported** (2026-10-22, item 14: `zai_target`
`clear_line`, `gmod_cull` `zones_overlap`). Whether vehicle A can see B
(0x4d5980, ECX A; the point given, or B's centroid, +0x14, its data +0x84
redone first when stale): not when their zone lists (+0x64: a count, then
up to three ids, 0xff for all) share no zone (0x53cc90: the zone mode
[0x5ea1d0] off, an empty list or an "all" passes); else both nodes (+0x04)
are left out of the world's queries (INTERSECT, 0x4eb710 / 0x4eb6d0), the
first-hit mode set (0x4ed9c0(1)) and nodes with flag 0x40000 excluded
(0x4eda00), and the world's segment query (0x4eed90 on [0x608c84] +4)
runs from A's centroid to the point; the flags put back, the mode cleared.
Blocked when the query answers 0 with a crossing. Callers: the scorer, the
plan's ready and take (take with the target's lock point, 0x5a8630), the
vlist triggers' bit 6, and two outside the AI (0x401a0b, 0x4d72f4). The
vehicles' zone lists are kept by zEntity3d (0x5a80f0, from the ground under
them), not ported: the program's lists say "all". In the program the query
is cls_di's `segment_list` on the world. Test: `test_ai_clearline.py` (6000
lines identical, 3420 clear and 2580 blocked; 7 branches; 12 of 12 breaks
caught). With it the c1m1 Owens fires at the player 30 m off: its two lasers
fire as the burst starts, the charge drains and refills (`test_program`).

The HUD's three (0x443d20, 0x446240) read the selected group (manager
+0x28, +0x1c group mode 2 = all its weapons, else the current one):
0x4b1bd0 its longest range (a NaN range is taken, as fcompp does), 0x4b1c70
whether any weapon locks on (def +0x6c bit 15, LOCK_ON), 0x4b1cd0 whether
any has ammo (vt+0x14 > 0 or -1). Ported (`mech3/weapon_planner`,
`group_max_range`, `group_locks_on`, `group_has_ammo`). 0x4b1cd0 leaves a
byte of its saved ECX (the top byte of the manager's address) as the answer
for an empty group in group fire and for a group without a current weapon
in the other modes: FIX (bug), no ammo (fixes.md).

Test `test_weapon_planner.py` (over `test_weapons_run.py`'s managers,
probe `probe_wm_planner`): after every operation of 200 managers (14,123
operations: loadouts, fire modes, ammo used up, beams in and out of water,
part loss), the three as the manager stands and with a random mode and
group set on both sides: 28,608 checks identical, among them 2364 without a
group, 4096 in group fire, 1696 empty groups in group fire, 4950 without a
current weapon (the fix's two cases, where the exe's byte and the port's no
are each checked). Breaking the port showed the test catches
it, 8 of 8: the shortest range, group fire on the current weapon, the
group's last weapon only, the LOCK_ON_LEAD bit, the current weapon's
lock-on from the group's first, unlimited ammo not counted, zero ammo
counted, group fire as mode 1.

### 17.7a Why an AI lands few hits on a target (diagnosis, 2026-10-27)

Mission-plan item 8: in `mission_c1m4_dominic_fires_on_a_target` Dominic
(group 0 two ER Medium lasers, group 1 a Gauss rifle) took 40 s to bring
the hut ghsD3 from 30 to 9. Traced frame by frame (temporary traces, not
kept), the hits are few for reasons that are each the exe's code as ported
and emulator-checked; no port fault was found:

1. **Not range, not aim.** The contact is 80.7 m off at the start and 31.5 m
   by 15 s (his spot is in a no-go patch, so he moves); the beams meet the
   huts' nearer parts about 10 m from the muzzle. The aim error (0x4b7060)
   stays at 0.2-0.8 m.
2. **The trigger is held, the lasers fire once per charge.** The firing is
   in phase 4 (trigger down) on 78 of 87 sampled frames. The manager reads
   every frame as a new press (0x4b0c30: a press that fires nothing clears
   the trigger), and a beam fires only from a full charge (0x4b3fc0): the
   bin of 1250 empties in about 0.15 s and refills at about 400 a second,
   so both lasers fire together every 3.1 s or more (starts at 2.6, 5.9,
   9.7, 15.2 s, then the rest and new plans).
3. **A beam does its damage once per firing.** Without TIMED_DAMAGE,
   `beam_damage` (0x5814b0's) gives the damage (about 7 here) on the first
   frame it meets something and 0 after (beam flag 2), however long it
   stays on.
4. **Half the hut does not take it.** A target claims the weapon handler
   only on its destroy animation's owner node (0x4d2xxx, `zai_target.cpp`
   set_node): its `bhealthy` part. Hits on its `healthy` part go to another
   live handler and leave the target's health alone; one firing hit the
   player's Mad Cat's toe on the way.

So in the 40 s six contacts carried damage, three reached ghsD3 (7 each:
30 -> 23 -> 16 -> 9). The Gauss rifle fires once and misses (item 8b,
below).

**8b, the Gauss rifle.** The volley pick (0x4b1860) does not pass it over:
with Dominic cool it takes either group at random (both in range, Gauss
0..880 m, heat 0.034 of a 0.9 budget), and once he is hot (budget 0.18) the
lasers' 0.38 is out and the Gauss alone is kept. The plan's track re-picks
each frame, so the group changes while he turns. The Gauss reaches phase 4
for 111 frames and fires once (t 15.22 s, ammo 16 -> 15, reload 2.5 s),
and the slug flies about 80 degrees off the target and hits nothing:
the firing's reach, track and fire thresholds (+0x18 / +0x1c / +0x20) were
0 in the port, so `in_reach` (vt+0x150, 0x41dba0: the torso's facing +0x98
strictly above the cosine) passed as soon as the target was within 90
degrees of the torso's front, while it was still turning; the weapon fired
along its own arc-limited direction. **Corrected (W34):** that was a port
bug, not the original's design. The defaults' template 0x5e8868 holds
zeros in the image, but the CRT start-up's initializer 0x4b6500 fills it
(cos 20°, 30°, 45° and the waits 3 s, 6 s, 1 s); the emulator never ran it,
so the brain's differential test agreed with the zeros. The test now runs
it and compares the fields (break: the zeros caught). Playtest #23 is the same
mechanism seen from the player's side (step 2): the AI is not delayed by
it beyond the recharge, since it presses again every frame. Nothing here is
a port change; whether the original plays the same scene alike can only be
checked in play (no whole-mission emulation).

What the real waits do (W34, from the tests that had been pinned on the
zeros):

- A plan is kept to its end. Phase 3 waits for a clear line (the plan's
  ready, 0x4d5980) until the delay (6 s, 3 for a quick plan) runs out; the
  pick (0x4b6790) does not run meanwhile, so a nearer enemy that turns up
  waits for it. The pick itself prefers the near one (under 40 m the
  distance term is 100; a blocked line multiplies the score by 10).
- A field base's centroid is its node's origin, on the ground: the
  constructor 0x458a70 calls 0x4d6c90(node, 0, 0, 0), whose 0x5a8390
  finds or makes the centroid child at the origin, and nothing raises it
  (a mech's set-up puts its centroid at half its height). The line from
  an AI's centroid to a base far off grazes the ground, so a plan on a
  base often waits out its whole delay (c1m1: the Owens 534 m off, 6 s,
  then a 1 s rest and the same base again).
- The fire threshold (cos 45) is against the torso's facing (+0x98), and
  the torso stops at its twist limit (Dominic's 1.92 rad): an enemy behind
  a mech walking away from it is aimed at but never fired on (c1m4,
  Dominic following the player). Under the zeros (90 degrees) he fired,
  his shots along the torso's facing.

### 17.8 The switches

[0x60b938] is the AI switch: on at the end of the mission start
(0x4811f2 -> 0x4ea740), off in the cleanup (0x49191c -> 0x4ea750); off,
every brain only stops and smooths. [0x5e9d40] is 1 and never written.
[0x6099c0] counts awake pilots. The AI tester dialog (section 1) is the
only other way in.

### 17.8a The brain's refresh (0x4aedd0)

A mech's vt+0xc8 (0x41e810), which the sensors call on every vehicle each
frame, first runs 0x4aedd0: the controls (vt+0x140), their brain (vt+0x54),
then the brain's vt+0x08. In every brain-like vtable (0x5cc698, 0x5cd190,
0x5cd2c8, 0x5cd6e8, 0x5cd830: those with 0x4e9640 at +0x0c) that slot is
0x5ac5c0, a bare `ret`, so the refresh does nothing in 1.2. The port calls
nothing (app_sensors.cpp); the rest of 0x41e810 is the level of detail's
budget (movement.md).

### 17.8b How the player's allies fight (traced 2026-10-22)

Asked by Owen ("allies fight back when attacked but never start a fight on
their own"). As the exe has it, that is half right: they never *go* to a
fight on their own, but they shoot at any enemy within their weapons'
reach whether or not anyone attacked them. Each part, with the code:

- **Who they shoot.** The firing (17.7) picks its own target at every new
  plan (phases 1 and 6, 0x4b6790): every contact within +0x30 (Dominic,
  Epona, Alan and the c4 lancemates 1000 m) that is active, not straight up
  or down (|dir.y| over 0.5) and not an ally (0x4b79e0: `mine->allies &
  theirs->team`, so neutrals count), scored by distance, facing, the
  other's health and heat and a clear line; the best is made the vehicle's
  target (0x4b5a80: vt+0xc0, which always takes it, 0x4d5e40). Nothing in
  it reads the orders, the engage switch or who hit whom. It fires when the
  plan is in reach and tracks (phases 2..4). The order's state does not stop
  it: the follow state answers "may not fire" (vtable 0x5cca88 +0x08,
  0x4d7290), but only the frame's "hit lately" reset reads that (17.1),
  not the firing.
- **Going to a fight.** Only the frame's attack (17.1, 0x4e9ca0) moves a
  pilot to an enemy on its own, and it needs engaging on (+0x3c, the aiv
  params' second item, 17.3). Every shipped ally has it at 0, and no lance
  order writes it (0x4ea260..0x4ea4e0); only the canleave valve (none
  ships), the nets' scan steps and the rearm after an attack do (0x4cad30).
  So they never close on an enemy by themselves.
- **When an ally is hit.** The AI's IControls +0x94 (0x4ea520) first runs
  the base 0x4aec90: the hit's time and attacker kept (0x4aec10: a run of
  hits restarts with a new attacker or after 3 s), and when the attacker
  counts the victim an ally, the attacker's firing is told (IControls
  +0x98, 0x4ea500 -> 0x4b78d0: its +0x28, the obstacle the scorer passes
  over targets behind, and the firing's stand-down). Then only radio
  lines: an enemy's hit while the victim's contact on the player is
  farther than [0x5e8e8c] (500, squared: 22.4 m), call_help (radio +0x00);
  the player's own hit, friendly_fire (+0x20). It writes neither +0x6c nor
  +0x3c. The frame's "hit lately" branch (17.1)
  needs +0x9c (aiv p8 item 14), 0 for every shipped ally, and none of their
  vlist triggers has the hit flag (item 4). So being hit changes nothing:
  they shoot back only because the attacker is an enemy in reach, as they
  would anyway, and the hit does not make them move.
- **The player hitting allies.** When the player is the attacker (his
  mech, vt+0x50, is [0x60861c] +0x14) and his run of hits on an ally
  passes 3 s ([0x5cd918]; 0x4aec70: the last hit less the run's start),
  0x4d5dc0 makes the player's team an enemy of the allies after him in the
  sorted entity list (sensors.cpp, [0x60b550, 0x60b548)): their enemies
  (+0xc8) gain it and their allies (+0xc4) lose it. From then on their
  scorer takes the player as an enemy, and, as written, each other too
  where they share his team.
- **"Attack my target".** The lance order (0x4ea450, by the command
  handler 0x469866..0x469910 for the selected lancemate, +0xbc bit 4)
  stops the current order (vt+0x40) and pushes the target vehicle's own
  net's follow behaviour (0x4c0de0): its state (0x4d1870, run 0x4d1980)
  steers after that vehicle, keeping off by half the gap between its
  +0x28 and +0x2c, until the vehicle is gone or not active (+0x08), when
  it pops itself (order vt+0x08) and the order under it runs again.

  **The follow, ported** (2026-10-22, item L3: `FollowState` in
  `zai_attack`, VehFollow's state; `walk_drive` takes 0x4cee80's two
  slacks, the nodes' 0.04 each). The steering segment runs from the
  target back to the pilot (0x4cea30(pilot, target, speed, vehicle)),
  the carrot falling toward the target at the pilot's speed (vt+0x118 of
  +0x84, or +0xec when that is 0 or NaN), +0x38 cos 10 degrees
  ([0x60b4a4]). Each run: with no target place, no own place or the
  target not active, the order pops; else the target's place through the
  boundary (0x4c3000). Not pulled: the segment set again, the carrot
  where it was from the pilot (its along taken off, its side offset
  kept), no nearer than (+0x2c + +0x28) / 2; the walk's slacks (+0x28 -
  +0x2c) / 2 along and 2 aside ([0x5e8be0]); +0x10 cleared. Pulled: the
  same toward the pulled point (unless it is where the segment starts
  already and +0x10 is set; a NaN counts as the same), no floor, no
  slacks; +0x10 set. Then the measure (0x4ceb50), the away and their
  sum (0x4cee50), the walk (0x4cee80, near 2 [0x5e8be4], 0.05 for the
  inverse [0x5e8be8]) and the controls' turn and throttle. Test:
  `test_ai_attack.py` pushes the follow in half its fights (slot 0): 365
  follows over the runs, the calls, the stack rows (the state's +0x10,
  the carrot and its floor) and the whole steering block identical, 5
  marked branches; the test sets the steering's offsets between runs (the
  update 0x4cec80 is not run there). 9 of 10 breaks; the tenth (the
  run's +0x38 left as the constructor set it) is the same. It sets no target: the lancemate still shoots whatever the firing
  scores best, usually the ordered one once it is the nearest.
- **When the ordered target dies.** The follow pops (above); the sensors
  (0x4a9e70) drop the lost contact and make the nearest of the rest the
  target (0x4d6a80 with side 0: any non-ally), and the next plan picks
  again anyway. Nothing else answers the target's death: 0x4ea630 (read
  first as "target lost") is the kill line, the killer's last_shot (17.6),
  and 0x4ea6b0 the dying pilot's ejecting.
- **The player attacked or attacking.** Nothing reaches the allies from
  either, except the 3-second rule above and the lance orders.

Checked headless (`test_program` mission_c1m4_dominic_shoots_a_near_enemy_
unordered_headless, MW3_LANCE_LOG=1, MW3_LANCE_AT=metres): in c1 mission 4,
Dominic woken (`dominic_wakes`) with the powered-down blackhawk_01 put in
front of him and no order given: at 60 m he takes it as his target and
fires (697 of 765 frames); at 150 m and 300 m in that direction he takes
it and aims for the whole 25 s but never fires: the ground blocks the line
there (the plan's ready, 0x4d5980), not the range; on a clear line (90
degrees round) he fires from 150, 400 and 600 m alike. Engaging is off
throughout, no attack. Since the follow state's port (L3) he walks after
the player meanwhile (his own order, the follow on the player's vehicle,
every frame), and never comes nearer the enemy than where it was put.

**The lance end to end** (item L5, `test_program`
mission_c1m4_dominic_takes_the_order_and_the_lance_turns_headless): the
player selects Dominic (his saved target), targets the enemy 400 m from
him on a clear line and presses the lance key 0x54 ("only an enemy",
order 10, through `player_command` 0x426cb0 and the dispatcher): the
order is taken, Dominic's current state is the follow on the enemy's own
net, and he closes to about 60 m (from 400), firing all the way (he fired
from 400 m before, too). Then the player's hits on him, one a frame for
about 6 s (`on_hit` from the player), run past 3 s: 0x4d5dc0 makes the
player's team his enemy and no longer his ally; put 30 m off, the player
is then often his next target (when depends on the walk's last bits,
which vary between runs: Dominic's place differs by 1e-4 m from about
2.5 s into the walk, and the player's handle by one; not the AI's, found
here). His own shots' splash at close range is another attacker and
starts the run of hits again, so the test takes the enemy away first.

**The hits, ported** (2026-10-22, item L1: `ai::HitRecord` on
`MechControls::hit` (IControls +0x04), `controls_hit` 0x4aec90 and
`MechControls::on_hit` (vt+0x94, called by `Mech::take_damage`),
`Brain::on_hit` 0x4ea520 and `hit_an_ally` 0x4ea500 for the AI controls,
`hit_lately` 0x4aed40 from the record; the program's hooks give a mech's
controls and `allies_turn`). Test `test_ai_hits.py`: the exe's 0x4aec90
and 0x4ea520 on test_ai_frame's brain with four attackers, the teams,
the player's vehicle, the radio and the record random (NaNs, runs either
side of 3 s, a new attacker or the same): 10,000 hits, the calls and the
record identical, 7 marked branches reached; 0x4ea500's obstacle each
time. The radio's lines are still the `radio_line` hook (L2).

The record starts at zero (0x4aec00) and the game time at 0 (0x48f400),
so for the mission's first second every vehicle counts as hit lately: a
pilot awake then (none in play; the c1m1 tests wake the Owens 0.1 s in and
set its last hit back) runs the frame's hit branch (17.1, with +0x9c) and
its vlist hit triggers at once.

### 17.9 Porting order

Each item is one family with its own differential test; what it calls
goes through a hooks struct the test stubs the same on both sides.

1. **The HUD's planner getters** 0x4b1bd0, 0x4b1c70, 0x4b1cd0: done
   (17.7).
2. **The order stack**: done (17.2), but 0x4d88a0 / 0x4d88d0 (with the
   net 0x603fb8, item 10).
3. **The gates**: done (17.3).
4. **The brain's frame** 0x4e9710 / 0x4e96f0: done (17.1), with the
   Mech's health 0x41ba40 and the controls' stop 0x428490.
5. **The vlist triggers** 0x4d9290, 0x4d8e70, 0x4e94f0: done (17.5).
6. **Steering** 0x4ce620..0x4cec80: done (17.4).
7. **The plain states**: done (17.2): 0x4c8660, 0x4c8700, 0x4c8790,
   0x4c88c0, 0x4c8940 and the shared 0x4cbe50; 0x4c8a30 waits for its
   derived states.
8. **The node state** 0x4ced40: done (17.4a), with the speed and valve
   steps' states, backup 0x4cfa80 and deploy 0x4d0040.
9. **The property states**: scan / canleave done (17.4a); the waits done
   (item 3a); mcbwait, sound, gate done (3b); shunt, valve union / assign
   done (3c); the jump done (3d).
10. **The vehicle's own net** 0x4d70f0 / 0x4c0090: done (10a); the attack
    0x4e9ca0 with its net 0x4c0810 and three states: done (10b); the fight
    0x4d1ca0 / 0x4d1d10 with 0x4c8a70: done (10b2); the brain's own order
    0x4e81a0: done (10c); its station-keeping states 0x4cc150 and the run
    0x4cc3e0: done (10d2, 10d3); the
    modes' states: modes 1, 2 and 4 (0x4c9750, 0x4d1320, 0x4d1650) done
    (items 3f, 3g, 3h).
11. **The manoeuvre** 0x4ca440 / 0x4caae0 / 0x4cabe0 (run 0x4ca600): done
    (item 3i), with 0x60b450's state 0x4ca3b0.
12. **The firing state machine** 0x4b5b80: done (17.7, 12a), with the
    volley plan pick 0x4b6790 (12b1), the plan's track 0x4b6e30 with the
    lead point 0x4d6de0 and the torso's aims (12b2a), the aim error
    0x4b70b0..0x4b77e0 (12b2b).
13. **The planner's volley** 0x4b1860, 0x4b1350, 0x4b1470, 0x4b1170: done
    (17.7, item 13).
14. **The radio** 0x4e6c60, 0x4e6d50, 0x4e9fd0, subtitles; **the player's
    orders** 0x4ea000..0x4ea230 and brain vt+0x20..+0x48; the IControls
    events 0x4ea520, 0x4ea630, 0x4ea6b0, 0x4ea700, 0x4e95b0.
15. **The rest**: 0x4ea260 with 0x4d08d0 / 0x4d0a10 done (vehicles.md, The
    AI at the dock); the switches 0x4ea740 / 0x4ea750 done; the brain's
    step 0x4cd7b0 and the controls' slots below not ported on purpose.

**Not ported on purpose** (checked 2026-10-24):

- The version-105 nets (checked 2026-10-25, item 6): CZAiNetEdit's build
  0x4bed90 (the editor's reload, the vehicles using the old net re-attached
  to the new one: section 9) and the older net class it builds through
  0x402590 / 0x4025b0 (0x4021c0, vtable 0x5bb8d0, about 1,800 decompiled
  lines in the exe's first source file). No shipped net file is version
  105 (the header 0x4da900 reads a "mech3" 105 as 106), so nothing reaches
  them; a hand-made 105 file would.

- The brain's step 0x4cd7b0 (vtable 0x5cc348, its run 0x4cd810, maker
  0x4cd790): the brain's constructor makes it at +0x240 (0x4cd770) and
  nothing ever gives it to an order: no other reference to +0x240, its
  vtable or its maker. Its run would turn the pilot on the spot toward the
  next step's place and hand on as the valve step.
- The AI controls' IControls vt+0x74 0x4d88a0 (whether the topmost order not
  marked is on the formation net 0x603fb8), vt+0x78 0x4d88d0 (push one on
  it, 0x4e86a0) and vt+0x7c 0x4d88f0 (pop down to that order when its net's
  vt+0x50 answers): only the AI controls' IControls vtable 0x5cd210 holds
  them (through the thunks 0x4d8cc0 / 0x4d8cd0 / 0x4d8ce0; the vehicles'
  0x5cd0d8 has 0x40bae0 there), and no call through a controls' virtual
  base reaches those slots. The lance's "form on me" goes through the
  brain's own vt+0x38 (0x4ea4b0, ported).
- IControls vt+0x84 0x4e95b0 (both AI vtables; the base's 0x4e0700 answers
  1): given an entity, drop the target if it is that one's and stand the
  firing down (0x4e9590); no caller reaches that slot either.
- IControls vt+0x60 0x4ea700, the AI controls' answer to an entity going
  (a vehicle's vt+0xd4 0x4aeec0 calls it from the sensors' forget
  0x4aa6b0): both of its calls are the empty 0x4bf620, so it does nothing.
  It comes with that forget chain, not ported yet (mech_dead.cpp's
  interim FIX (A)).
- The AI controls' destructor body 0x4d8820 (the vtables put back, the
  base 0x4cf630): the C++ destructors (section 12's list).
- The switches 0x4ea740 / 0x4ea750 are ported (`ai::ai_switch_on/off`).

## 18. The animation-owner registry (0x4d4f20 family; ported 2026-10-24, item 2a)

How the mission's animations talk to entities. An animation's callback
(+0x6c, set with its argument +0x70 by 0x508f00) hears the value of every
callback event it plays (event 35, the event's +0x0c) and 0 when it stops
(0x506ca0). The AI's callback is 0x4d4b10, its argument a record of the
registry: 8 bytes, the owner entity (+0x00) and a valve (+0x04).

**The registry** [0x60b518] (vtable 0x5ccd90, made at start-up by
0x4d4840 -> 0x4d48f0) holds the records in a list [0x60b51c] (head
[0x60b520], count [0x60b524]). It is one of the mission listeners
(0x4e78d0): at the mission's load (slot +0x08) nothing; at the mission's
end (slot +0x10, 0x4d4700, from 0x4e7bc0 in the cleanup 0x491780) every
record's owner and valve set to 0, before the animations are stopped (so
the stop's 0 reaches no one); when the world goes (slot +0x0c, 0x4d4740,
from 0x4e7b80 in 0x490480) every record freed and the list emptied, before
the animations are freed (0x50b380). So the list holds at most one record
per animation of the loaded world: a next mission on the same world keeps
the emptied records, and the claims and the binder reuse them through the
animations' +0x70. No leak.

**The claim** 0x4d4f20 (ECX the owner, EDX the animation; returns whether
it took): with no callback, a new record; with ours, the animation's record
when the list still holds it, else (none, or one the list no longer holds)
a new one; with another callback (zEffect's own, a weapon's), refused. A
record held by another owner is refused too. A new record is copied from
a temporary (0x4d46f0) and put at the list's end (0x45b800); either way the
animation gets 0x4d4b10 and the record (0x508f00). Callers: a target's
attach (0x4d34a2, its destroy animation; 0x4d33a5 releases first when it
had a node), a turret's load (0x4a66af, each AUX_ANIM), and the forget
(0x4aa6b8 releases the entity going, section 15).

**The release** 0x4d5100 -> 0x4d47e0: every record of that owner gets owner
0 (the valve is tested and stored 0 when 0: nothing). The record stays.

**The handler** (0x4d4b10: with a record, 0x4d4a20(record; animation,
value)): the owner, if any, hears every value (vt+0x74), then:

| Value | What |
|---|---|
| 0x11 | the player's vehicle ([0x60861c]+0x14) moved by the node `viaNode` (0x4d4980): its translation added to the vehicle's place, its rotation's y as the yaw, through vt+0xdc |
| 0x2a, 0x2c, 0x2e, 0x30 | the record's valve set to 0 (0x4d3c00) |
| 0x2b, 0x2d, 0x2f, 0x31 | the record's valve set to 1; the four pairs differ only in which valve the binder gives the record (item 2b) |
| 0x39..0x3c | the inout tests (0x4c3bd0, section 11): the polygons named as the animation's node |
| the rest | nothing more |

The owners' vt+0x74 (the vtables found by their vt+0xd4): a turret's
0x4a74f0 (0x32 wakes it, 0x33 sends it idle, 0x34 shuts it down destroyed;
the rest AiTarget's); everything else is AiTarget's 0x4d7270, which sends 0
to vt+0x7c and 0x38 to vt+0x78. Only a target fills those: 0x38 is its kill
(0x4d2b60, the triggers fire) and 0 clears its destroyed flag (+0xf0,
0x4d3340; written only, nothing reads it); a mech's, a basic vehicle's, a field base's and the command
marker's are empty (0x5ac5c0, 0x49bfd0). The placement (vt+0xdc): a mech's
0x418f30, a basic vehicle's 0x4a3630; the others' slots are empty or answer
a value (0x40baf0, 0x40bae0, 0x4d2a70, a turret's none), and only the
player's vehicle is ever placed.

**What the shipped animations send** (event 35 values, every anim.zbd):

| Value | Count | Who |
|---|---|---|
| 0x2b (43) | 20 c1, 9 c2, 44 c3, 14 c4 | bridges, ramps, walls and buildings going from healthy to destroyed: the valve the mission script waits on |
| 0x2f (47) | 2 c4 | brgprt: the valve two parents up |
| 0x32 (50) | 2 c1, 3 c2, 6 c3, 5 c4, 9 c4b | turrets raised or switched on (`raise_bturret1`, `turn_on_choptur1`, `raise_turret1`) |
| 0x33 (51) | 1 c3, 2 c4, 3 c4b | turrets lowered or switched off |
| 0x34 (52) | 1 c3 | a wall turret shut down as its wall falls |
| 0x38 (56) | 12 c1, 8 c2, 15 c3, 7 c4, 1 c4b | buildings, ships and trains destroyed: the target killed |
| 0x39 (57) | 6 c1, 1 c2, 7 c3, 3 c4 | bridges down: their yellow outline tested |
| 0x3a (58) | 6 c1, 1 c2, 7 c3, 3 c4 | the same bridges' resets: back to the red one |

No shipped animation sends 0x11 and no world has a `viaNode`: the
teleport is unused. FIX (bug) (fixes.md): with no such node the exe places
the vehicle at its stack's leftovers plus its place (0x4fc020 and 0x4fbe90
report and write nothing); the port places nothing. With no vehicle the exe
would crash; the port does nothing.

**The binder** 0x4d4b30 (ported 2026-10-25, item 2b), at the end of the
mission load (0x490f90, after the vehicles' fixup and "Ready To Launch"'s
lists): every def in zEffect's array from index 1 (0x50b570: index 0 is
never walked) not freed (status 5) and with HAS_CALLBACKS (+0x94 bit 0x10),
each callback event of each sequence in order (0x508f40: the reset
sequence is not looked at), through 0x4d4c80 with a binding on the stack
(the def, its record or a temporary one, whether something was bound):

- a new def first commits the one before (0x4d4e50) and takes its record
  as the claim does (ours and listed: that one; none, or ours unlisted: a
  temporary; another's: none, and its events do nothing);
- 0 and 0x38: the def's node's entity (0x50b5e0, 0x40eee0) becomes the
  owner, unless none or another owner holds the record;
- 0x2a..0x31: (value - 0x2a) / 2 parents up from the def's node, each the
  node's only parent (+0x54 1, +0x58[0] not 0); past the root (a node with
  no parent, several, or a null first one) nothing; else the valve named as
  that node (0x4d3c40, made if missing) becomes the record's, unless
  another valve holds it;
- 0x39..0x3c: bound with nothing (the record only, for the inout tests);
- the rest nothing.

The commit (0x4d4e50, inline for the last def): when something was bound,
a temporary record becomes a new one at the list's end, and the def gets
0x4d4b10 and the record. A next mission on the same world binds again onto
the emptied records (the defs keep them). On the shipped worlds 121 defs
are bound (c1 29, c2 14, c3 56, c4 22; c4b and t1 none: c4b's callback
events are its turrets' and a 0x38 whose node has no entity), 89 with a
valve and 27 with an owner (with the test's answers for the entities).
FIX (bug): a valve message two or more levels up on a def without a node
would read the parent count of nothing (no shipped def has one); the port
binds nothing.

What it does in play, on c1m1: the dropship ramp's animation call_ramp
(on call, node bramp; called by the mission's start animation
start_single_player_m1) sends 0x2b in its effect sequence, which turns on the
valve `bramp`; c1m1's valves.zrd then powers the Owens owens_01 up
(`shutdown` with 0), sends the trucks after 18 s and plays the fourth
dialogue after 22 s. Before the binder the Owens stayed down. Most
buildings' and bridges' destroy animations turn on the valve of their own
node the same way (citybuild01's good_to_bad), and brgprt on c4 that of
the node two parents up.

**The port**: `mech3/zai_anim_owner` (AnimOwnerHooks: the animation's
callback and record, the owner's message, the valve, the inout tests,
viaNode's node, translation, rotation, the vehicle's place and placement);
CZAiTarget::message for 0x4d7270 on a target. In the program
(app_sensors.cpp, ProgramAnimOwner): the callback is a port function
standing for 0x4d4b10, the owner's class picks its vt+0x74 and vt+0xdc as
above, the valves and the inout tests are the ported ones; the targets'
and turrets' claims and the forget's release call it (app_mission.cpp,
app_turret.cpp), and the missions' end and the world's unload empty and free
the records (app_mission.cpp's listeners).

**Tests.** `tests/differential/test_anim_owner.py` (probe
`tests/probe_anim_owner.cpp`): 400 cases, each the registry built by its
constructor, six animations in a def array of seven (index 0 the one never
walked; no callback, ours with a record no list holds or none, another's;
some freed or without HAS_CALLBACKS; up to three sequences of callback and
other events), ten nodes in chains (one parent, none, two, or a null first
one), owners none, two mechs, a turret and a target, three valves, the
player's vehicle and a viaNode or none (a quarter); 40 random operations
(claims, releases, messages on the record or none, every value 0..0x3f
across the cases, a listed record's valve set, the binder, the mission's
end, and last perhaps the world's free); after each the traces, every
animation's callback, record by list place, owner and valve, and the list
compared; 35 branches reached, the walk past the root both ways among
them; the 25 messages 0x11 with no viaNode checked up to the departure.
2392 claims taken, 2621 messages, 575 binds. Breaks
caught: the valve bit flipped, the owner not told of 0, 0x32 taken as a
valve message, 0x2a dropped, a null valve set, 0x3c dropped, the list
check skipped, another's callback taken, any owner taking, the release
letting every owner go, the end keeping the valve, the free keeping the
list, viaNode absolute, the yaw from x, 0x11 dropped, a null record
handled: 16 of 16; the binder's: the walk not halved, one level too far,
past the root still bound, an owner or a valve taken over another, 0x38
dropped, the inout messages binding nothing, a binding committed with
nothing bound, the last def not committed, defs merged, 0x31 dropped,
another's callback bound: 12 of 12.
`tests/differential/test_anim_binder.py`: every shipped world read and its
animations loaded on both sides (test_anim_run.py's), then the binder, the
mission's end and the binder again, then a made-up round (bound defs freed
or their flag flipped, def 0 given another's sequences) and the binder once
more; the calls (the node's entity by node, the valve by name) and every
def's callback, record, owner and valve compared: 6 of 6 worlds. Breaks in
zEffect's pieces caught: def 0 walked, freed defs walked, the last sequence
skipped, the value read from +0x10: 4 of 4. Dropping either HAS_CALLBACKS
test alone (0x50b570's or 0x508f40's) changes nothing: each covers the
other, in the exe too. In the program,
`mission_c1_turret_woken_by_its_animation_headless`: on c1m1 the boss
turret tur_boss01 starts idle with raise_bturret1 claimed (25 records
after the load, the binder's with them); the animation started, the turret
is awake the next frame; the mission's end keeps the 25 records emptied and
the world's unload leaves none.
`mission_c1m1_a_destroy_animation_turns_its_valve_on_headless`: after the
load citybuild01's def holds a record naming the valve `citybuild01` (20
records with a valve), off; rammed (0x502df0 with 10^6), its good_to_bad
sequence turns the valve on the next frame.
`mission_c1m1_player_fires_on_the_owens_headless` now stands 40 m off (was
80): the opening's call_ramp powers the Owens up a few frames in, it
moves, and from 80 m the machine guns miss it.

## Open

- Whether mission loading runs at 24 or 53 bits: hardware mode has a
  Direct3D device by then. The readers give the same result either way.
- What the behaviours, the order items and most AiParams numbers mean (the
  brain is not mapped beyond section 4).
- Duplicate record names in a file: by number both are read, by name the
  first wins.

## The developer tools (decided with Owen, 2026-10-20)

The AI tester dialog and the in-game AI path editor (ZAiNetEdit's editing
side), reached in the exe through the debug menu, are to be ported later
behind a `--debug` switch, off by default, as hidden as the original's
menu. They are not needed to play.

## 19. The reset lists and the vehicle controls' end (item 9, 2026-10-25)

**The four lists** (mech3/zai_list): objects of one small class (ctor
0x4d8540, vtable 0x5cd0d4 with only its deleting destructor 0x4d8570, dtor
0x4d8590): a list node sentinel (+0x08) and a count (+0x0c), made by the
static constructors at 0x4100e0..0x410204. The reset 0x4d8610 takes each
entry off the front (the node freed, the count down) and then calls the
entry's vt+0x04 with no arguments, reading the front again each time, so
an entry told may add or take others. It runs on [0x604038] each Core
frame (0x480f5e, after the view's draw), on [0x604048] and [0x604078] at
the mission load's "Post Vehicle Fixup" (0x490f23, 0x490f2d) and on
[0x603f90] after it (0x490f7c). Nothing in the exe adds to any of them:
their addresses appear only in those calls, the constructors and the
destructors, and no code takes them by pointer. So in play every reset
finds an empty list; what kind of object the entries were is not known.
Ported as written (`ResetList::reset`, `reset_list` by the exe's address),
the program's four empty.

**The basic vehicle controls' destructor** 0x4ac800 (`vehicle_controls_gone`,
from the AI's 0x4e7870 for each AI vehicle's controls): the vtables put
back, then the owner's vt+0xec (a vehicle's itself, 0x4d2a70) and 0x402510
on the answer, which drops the basic path's temporary entries (+0xa0c's
with a negative +0x2c, through the vehicle's controls' vt+0x00; an AI
vehicle's controls give none, veh_basic_object.h). The missing owner's
FIX (bug) is fixes.md's "AI vehicle controls' destructor".

**Checked.** `test_ai_lists.py` (probe `tests/probe_ai_lists.cpp`): 3000
lists of 0..8 entries whose vt+0x04 follows a plan both sides share
(nothing, a higher entry added at the back, another taken off): the order
told (15,013), the count left, every node freed. `test_ai_brain.py` runs
0x4ac800 through 0x4e7870 on each deleted vehicle controller (1,014).
`mw3tests` `mission_c1m1_restarted_twice_keeps_no_ai_headless`: c1
mission 1 loaded, then restarted twice (the cleanup, the Load state):
each load has 24 entities, 1 target, 7 basic vehicles, 1 AI mech, 1 AI
mech controller and 7 AI vehicle controllers alive and the four lists
empty, the same each time; after the last cleanup the controllers alive are
back to none. Also clean under ASan. Breaking the port showed the tests
catch it, 6 of 6: the reset from the back, an entry told before it is off
the list, a copy of the list walked, the owner not asked, the path not
told, a vehicle's controls not deleted.

## 20. The nets' begin and end (0x4d52b0, 0x4d5300; item 17, 2026-10-25)

The mission start (player.cpp, after the nets are built) and its end walk
znet's registry ([0x603fdc], each object's vt+0x28 is itself, 0x4d2a70)
and call each object's vt+0x2c (begin) and vt+0x30 (end). By class:

| object | vtable | vt+0x2c | vt+0x30 |
|---|---|---|---|
| a numbered object, a net edit | 0x5ccda8, 0x5ca420 | 0x4d5350 | 0x4d53f0 |
| a region's net of edges, a region loop | 0x5cac90, 0x5cae00 | 0x4d5350 | 0x4d53f0 |
| a waynet, the mcbwaynet | 0x5c37b0, 0x5c38e0 | 0x479a50 (0x4d5350 first) | 0x5ac5c0 (nothing) |

0x4d5350, over the object's elements (+0x0c): each element's vt+0x60
(a cast), that one's vt+0x64 stored back in the element's slot (a swizzle),
then each one's vt+0x6c, then each one's vt+0x38. 0x4d53f0: vt+0x68 stored
back, then vt+0x38. For every element class these objects hold (net nodes
0x5cae60 and their placeholder 0x5ca388, region edges 0x5cad70, waynet
points 0x5c3828), vt+0x60, +0x64 and +0x68 return the element itself
(0x4d2a70) and vt+0x6c does nothing (0x4a5bc0), so the swizzle leaves every
pointer as it is: the port's elements are pointers already. vt+0x38 is
0x410590, each link (vt+0x04 the count, vt+0x08 the target) told which node
points at it (vt+0x50): the points and edges have no links (vt+0x04
0x4d72a0, 0), and the net nodes' back links are set when the net is built
(the builder's link pass, `AiNet::link_pass`, zainetedit.cpp), and setting
them again sets the same slots. The port keeps no net edits past the build
(each net built from a local edit), so their begin has nothing to run on.
The compiled nets' steps, whose vt+0x6c turns slot numbers into pointers
(0x4c9e60, 0x4c94a0), are not begun here: the compiled net's vt+0x2c is
0x5ac5c0, and the steps are attached as the net compiles (zai_steps).

So only the waynets' begin changes state: 0x479a50 finds the "waypoint"
marker once ([0x608ba4], shared) and selects the waynet's point 0. The
first waynet in the registry wins it; the others find it taken. The nav
waynet (waynet_N, made by the mission reset 0x490720) comes before the
mcbwaynet in every mission; the port had begun only the mcbwaynet, which
selected its point 0 (the origin: its element 0 is the default behaviour)
until the nav waynet's frame put the marker back on its own selected point.
`program_nets_begin` now begins every waynet in the registry's order, and
the end does nothing (the waynets' vt+0x30 is empty). Checked:
`missions_waynets_begin_in_the_registrys_order_headless` (c1m1, c2m3,
c4bm5: the nav waynet first, the mcbwaynet second where there is one, the
marker on the nav waynet's selected point). The order is not visible after
the first mission frame, since the nav waynet's frame places the marker
each frame.

