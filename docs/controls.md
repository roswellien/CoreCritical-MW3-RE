# Mech controls (Mech +0x37c)

Mech3.exe 1.2 (researched by a subagent, spot-checked: GetThrottle 0x428510, SetThrottle 0x426b60), 2026-10-19. Static analysis only (objdump + func2file). Guesses are marked "likely".

## Summary

- `Mech +0x37c` points at the start of a **controls object**. Every kind of controls
  derives from one class, called here `MechControls` (ctor 0x4265a0, mech_anim/mech_data
  area). It has a **virtual base**, called here `IControls` (vtable 0x5bd500, default
  methods in the veh_basic area, 0x4aed60...). The vbptr sits at +4, and the vbtable gives
  the offset to `IControls`. Mech code also calls `MechControls` methods directly (0x426780),
  so the pointer has the `MechControls*` type, not `IControls*`.
- **vtable +0x1c is `GetThrottle()`.** It returns the current, smoothed throttle in -1..1
  (negative means reverse). That makes heat's `|value| > 0.6` test a test on throttle.
- **+0x38 is `GetJumpThrust()`**: the current, smoothed jump-jet thrust (0..1).
- **+0x4c is `Stop()`**: it sets every movement command (throttle, turn, jump, strafe) to
  zero. `Mech::Power` calls it when the mech powers down.
- Concrete classes:

| class (name here) | ctor | size | primary vtable | IControls vtable (at offset) | made by | role |
|---|---|---|---|---|---|---|
| MechControls | 0x4265a0 | 0x74 | 0x5bd4e0 | 0x5bd428 (+0x60) | (base) | shared state + smoothing |
| PlayerControls | 0x427320 | 0x160 | 0x5bd710 (+0x5c: 0x5bd708) | 0x5bd650 (+0x14c) | 0x427280, singleton `[0x6056ec]` | keyboard/joystick input |
| PocketControls (likely name) | 0x4690d0 | 0x1dc | (derives PlayerControls) | 0x5c2ef0 (+0x1c8) | 0x469940 kind 2, for CMD_DIRECT_POCKET | temporary stand-in for the player's controls |
| NetControls (likely) | inline in 0x46d450 | 0x74 | 0x5c34e0 | 0x5c3428 (+0x60) | net msg 0xf handler 0x46d880 | remote mech; values come from packets |
| AI base (likely) | 0x4d8670 | | 0x5cd318 (+0x5c: 0x5cd2c8) | 0x5cd0d8 | | |
| AIControls (likely `zAI...`) | 0x4d8750 | 0x320 | 0x5cd318 (+0x5c: 0x5cd2c8) | 0x5cd210 (+0x30c) | 0x49e208, 0x49fed4 (player.cpp area) | AI pilot |

## Layout (offsets from the start of MechControls)

ctor 0x4265a0(owner, isMostDerived), ret 8.

| off | type | meaning | evidence |
|---|---|---|---|
| +0x00 | vtbl | primary vtable (zClass-like: +0x4 returns this, 0x4d2a70) | 0x4265e3 |
| +0x04 | vbptr | vbtable (0x5bd5b8 for the base; vbtable[1] = 0x5c gives the IControls offset) | 0x4265b6 |
| +0x08 | u32 | control-mode flags (bits 1 and 4 load the joystick setup at 0x6040d0..0x604110) | 0x426650 |
| +0x0c | int | view/state 0..3 (switch in 0x426780; mech calls it from 0x41c6cb...) | 0x426780 |
| +0x10 | float | **throttle, current** (smoothed toward +0x44) | 0x428510, 0x426970 |
| +0x14 | float | **turn, current** (smoothed toward +0x48) | 0x426af0, 0x426a10 |
| +0x18 | float | pitch input, smoothed (player) | 0x428314 |
| +0x1c | float | torso twist input, smoothed, +-1.5 (player) | 0x428351 |
| +0x20 | float | **jump-jet thrust, current** (smoothed toward +0x4c) | 0x428530, 0x426a5f |
| +0x24 | float | JJ strafe (CMD_JJ_LEFT -1 / CMD_JJ_RIGHT +1) | 0x42840e, 0x428570/0x428550 |
| +0x28 | float | JJ forward/back (CMD_JJ_FORWARD +1 / CMD_JJ_BACKWARD -1) | 0x428414, 0x428560/0x428540 |
| +0x2c, +0x30 | float | two more smoothed axes (from joystick 0x427870; likely look/free-look) | 0x4283cb.. |
| +0x34 | u8 | set by IControls +0x10 | 0x428580 |
| +0x38 | int | cleared at mech death, which then calls primary vt+4 (likely an order/wrapper pointer) | 0x41aa61 |
| +0x3c..+0x3f, +0x58..+0x5a | u8 | flags (init 0/1); copied over when the pocket mode ends | 0x469ac7 |
| +0x44 | float | **throttle target**, clamped to -1..1 | 0x426b60, 0x428520 |
| +0x48 | float | **turn target** | 0x426b10, 0x426b00 |
| +0x4c | float | **jump target** (+0x3d byte cleared with it) | 0x426b20 |
| +0x50 | u8 | **reverse**: 1 when the last nonzero throttle target was < 0 | 0x426b60, 0x428500 |
| +0x54 | int | init 1 | |
| +0x60 | IControls | virtual base: vtable, then +0x4 a sub-object (ctor 0x4aec00), then **+0x10 (=+0x70) the owner vehicle** | 0x4265c6..cd, 0x428750 |

## IControls vtable (slots as called through the virtual base)

The getters and setters are shared by every class: the derived vtables hold `this`-adjusting
thunks (`sub ecx,[ecx-4]; sub ecx,0xec/0x168/0x2ac; jmp`) to the same functions. The table
below gives the thunk targets. Field offsets are from the start of MechControls (the
functions' own `[ecx-N]` is relative to the virtual base, at MechControls +0x60).

| slot | function | name | what it does |
|---|---|---|---|
| +0x00 | per class | scalar deleting dtor | Mech dtor 0x414f40 calls it with 1 (0x41522c) |
| +0x04 | 0x410510 (ret 0); AI 0x4e9380 | ? | |
| +0x08 | 0x428750; pocket 0x46a440 | `SetOwner(vehicle)` | stores +0x70. Called by `Mech::SetControls` 0x491120 and by the mech ctor (0x414d81) |
| +0x0c | base 0x426960, player 0x427d60, pocket 0x46a360, net 0x46ef60, AI 0x4e96f0 | `Update(dt)` | see below |
| +0x10 | 0x428580 | set byte +0x34 | |
| +0x14 | 0x428490 | `ClearCommands()` (likely) | SetThrottle(0), SetTurn(0), SetJump(0), +0x3c(0), +0x44(0), ... |
| +0x18 | 0x428500 | `IsReverse()` | byte +0x50 |
| +0x1c | 0x428510 | **`GetThrottle()`** | float +0x10 |
| +0x20 | 0x426b60 | `SetThrottle(v)` | v>0: reverse=0; v<0: reverse=1; +0x44 = clamp(v,-1,1) |
| +0x24 | 0x426af0 | `GetTurn()` | float +0x14 |
| +0x28 | 0x426b10 | `SetTurn(v)` | +0x48 |
| +0x2c | 0x428520 | `GetThrottleTarget()` | +0x44 |
| +0x30 | 0x426b00 | `GetTurnTarget()` | +0x48 |
| +0x34 | 0x426b40 | `SetJump(v)` | 0x426b20(v,0): +0x4c = v, +0x3d = 0 |
| +0x38 | 0x428530 | **`GetJumpThrust()`** | float +0x20 (jump jets 0x4173b0 read it) |
| +0x3c/+0x40 | 0x428560 / 0x428540 | `SetJJForward` / `GetJJForward` | +0x28 |
| +0x44/+0x48 | 0x428570 / 0x428550 | `SetJJStrafe` / `GetJJStrafe` | +0x24 |
| +0x4c | 0x426c10 | **`Stop()`** | zeroes +0x24, +0x28, +0x18, +0x14; SetThrottle(0), SetTurn(0), SetJump(0); byte +0x3c = 0; slot+0x10(0). `Mech::Power` 0x41cc30 calls it |
| +0x50 | 0x4287b0 | `AsMechControls()` | returns this-0x60 (the MechControls start) |
| +0x54 | 0x40bae0 (ret 0); AI 0x4d1e60 | ? | |

The IControls defaults (vtable 0x5bd500) return 0.0f (`[0x5c8ea4]`) or do nothing. Slot +0x2c
defaults to calling +0x1c, and +0x30 to calling +0x24.

## MechControls::Update(dt) @ 0x426960 (base; the net class uses only this)

`owner = [+0x70]`:
- If `owner[0xde]` (powered_down) is set: 0x426c80 zeroes every axis, current and target
  (+0x10/14/18/1c/20/24/28/2c/30/44/48/4c).
- Throttle: `d = target(+0x44) - cur(+0x10)`. If `|d| < 0.009`, cur = target. Otherwise
  `rate = owner[+0x530]` when |target| > |cur|, else `owner[+0x534]`, and
  `k = exp(-rate*dt)` (a Schraudolph fast exp: `float_bits(int(x*12102200)) + 0x3f800000`,
  0x5ac340 is ftol), `cur = (1-k)*target + k*cur`.
  So +0x530 is the mech's throttle-up rate and +0x534 its throttle-down rate (likely
  accel/decel). Both are copied from a mech data block at +0x7c/+0x80 (0x4144a5).
- Turn: `cur(+0x14) += (target(+0x48) - cur) * owner[+0x538] * (flags&4 ? 2 : 1) * dt`,
  snapping to target within 0.05. +0x538 is the turn rate, from data +0x84.
- Jump: `cur(+0x20) += (target(+0x4c) - cur) * (target==0 ? [0x5e259c]=9.0 : [0x5e2598]=7.0) * dt`,
  snapping within 0.05. Ported with the throttle and turn (`mech3/mech_control`,
  `movement.md`).

## PlayerControls::Update(dt) @ 0x427d60 (ported)

`mech3/mech_control.cpp` (`PlayerControls`), checked by `test_player.py`.
ECX is the IControls (+0x14c); the fields below are from the start, which
is the MechControls layout. Commands are read with 0x51d300 (zInput's
`command`, `& 3` held; input.md):

```
powering down (mech vt +0xa0, 0x41cc10: +0xdc or +0xde): nothing but the smoothing
pilot tick: IControls +0xa4 (0x4d8990 -> 0x4d9290, the +0x5c pilot base; lance orders, likely)
locals 0; the HUD screen when the mech is the viewed one
0x427870: the axes (below)
CMD_THROTTLE_UP / DOWN (0x18 / 0x19): delta +-0.025 a frame; clears +0x3c and +0x3e
CMD_TURN_LEFT / RIGHT (2 / 3): turn +-1
with jump jets (mech +0x3d8 > 0): CMD_JJ_UP (0xc) jump 1; with the MW2JJets option
  (0x40d1e0, [0x5f4d04] bit 7): JJ_FORWARD / BACKWARD (0xd / 0xe) jump and +-1 ahead,
  JJ_LEFT / RIGHT (0xf / 0x10) jump and -+1 strafe, and airborne JJ_ROTLEFT / RIGHT
  (0x11 / 0x12) the turn
CMD_RETICULE_ZOOM (0x14), with a screen whose +0x167c is clear: held zooms in (+0x1508
  vt +0x60(-1)), let go zooms out (vt +0x64); the click (pilot sound 0x17, mech +0x614)
  as the state changes (+0x1514 bit 4 set: not zoomed)
CMD_TWIST_LEFT / RIGHT (8 / 9) twist +-1.5; CMD_PITCH_UP / DOWN (4 / 5) pitch +-1
fire (+0x34) = command 0x3d held (0x467480)
throttle, turn clamped to +-1, jump to 0..1
+0x3c clear: delta 0, ThrottleConst off (+0x3f) and +0x3e clear: target x exp(-0.7 dt),
             0 under 0.05; else SetThrottle(target + delta)
+0x3c set (the stick's throttle): target = the value straight (the reverse flag kept)
SetTurn; SetJump unless +0x3d (a held jump) and jump 0
pitch +0x18, twist +0x1c, the free look +0x2c / +0x30 toward theirs at 6/s, snap 0.05
JJ strafe +0x24, ahead +0x28
MechControls::Update (the smoothing)
```

The axes 0x427870 read the axis table 0x6040d0 (four 16-byte entries:
source, scale, offset, dead zone; throttle, twist, pitch, turn; set by the
controls file, the UseJoystick option and the controls' mode: Setup below).
A source is 1..6 the stick's
axes (centred and scaled by zInput's setup, 0x42fea0), 7 and 8 its sliders,
9..11 the mouse's position from the centre; the value (0x42fe10) is `scale
x reading + offset`, 0 inside the dead zone; the motion (0x42fe70, 0x42fff0)
the stick's velocities (DirectInput never fills them) or the mouse's scaled
motion. Turn and throttle (its sign flipped in reverse) are values. With
twist and pitch both on the mouse they are motion over dt (x -0.26 and
-0.16) by the view state +0x0c: 0 the torso, 1 the free look (x1.5), 2 a
pointer (+0x138..+0x144), 3 the torso with the pointer held (0x51b440);
else values clamped to +-1 (the free look in view 1), and in camera views 0
and 1 the look's y drifts by 5 dt x pitch (0 in the others). With a
joystick: its throttle counts only once it has moved from its last reading
(+0x40), then sets the target outright (+0x3c); a change of the hat (+0x54,
not in view 1) runs a camera command (0x51c440): up 0x65 (0x66 in camera
view 4), right 0x63, down 0x64, left 0x62, each 0x66 in camera view 0, 5, 1.
The exe also writes 2 over its copy of the camera view in view 1, where the
hat is not read: no effect.

Reverse on a stick, in the exe: the throttle axis's value times -1 while the
reverse flag is set, and the flag is set only by SetThrottle's sign (the
keys) or THROTTLE_REV (0x1a, Backspace and button 10: the target's sign
flipped, or at 0 the flag toggled; 0x004274d0); the stick's own value goes
into the target straight (+0x3c) and never touches it. So the defaults' 0..1
lever drives 0..-1 after Backspace. The axis popup's OK (0x0042e5b0) takes
the lever's place at OK as 0: pressed with the lever centred, its lower half
is reverse; at an end, stop to full only. Ours (W56, fixes.md): with bugs the
lever covers reverse itself (`lever_throttle`), and the flag no longer flips it:
a tapped throttle-down key with the lever behind stop, or Backspace or button 10
with it ahead, left up meaning reverse until the mission's end, while Backspace
with it behind stop did nothing. The keys' throttle keeps the flag. A lever that
reads high at its top runs backwards until Invert is unticked in Assign Axis.
Open: a Thrustmaster player (model unknown, a Reddit comment, no log) said the
reversed option changed nothing. Not reproduced: Invert flips the lever through
the screens, saved and restarted, on Z and a slider, reading either way. The
T.16000M lever reading low at its top is an assumption, never checked on a stick
and with no public source, so the default stays.

FIX (B): the throttle keys' 0.025 is per 1/30 s (`zfix::frame_steps`;
fixes.md), and so is the 0.009 at which the throttle takes its target
(0x00426960), at most 0.009.

The singleton `[0x6056ec]` is made by 0x427280(mode) (ported, mech-build.md section 7), called from 0x491150 (mission.cpp),
with mode 3 or 6. A second call only reapplies the mode (0x426650). The fields
+0x3c..+0x3f: the stick's throttle set, a held jump (0x426b20(v, 1)), the
throttle held (set by the THROTTLE1..0 callback 0x4274d0, which also clears
+0x3c), ThrottleConst off (1 from the constructor 0x4265a0; the option's
setter 0x40d052, options.md).

## Setup: the commands' names, the defaults and the controls file (ported)

`mech3/controls_cfg` (the original file is unknown: the code sits between
`briefing.cpp` and `mech.cpp`, after the options, 0x410950..0x4130xx),
checked against the exe by `test_ctlcfg.py`. The settings loader 0x40c420
calls the defaults before the registry read and the controls file after it
(settings.md); the app's destructor frees the groups (0x40cb20 ->
0x410a80). The controls screen (0x42a590, ported: menus.md section 6,
`mech3/controls_menu`) lists the groups, binds keys, buttons and axes, and
loads, saves and resets them: 0x42d0b0 writes or reads `keys\<name>.zrd`
(`sprintf("%s\\%s.zrd", "keys", name)`, `_mkdir("keys")`) and puts that path
in ActiveCtlCfg; 0x42d210, 0x42d2b0 and 0x42d730 call the defaults again,
the first two then save them as `keys\DefaultCtlConfig.zrd`.

### State

| Address | What |
|---|---|
| 0x6040b8 | the name map: `std::map<const char*, int>` in strcmp order (MSVC `_Tree`: header `[0x6040bc]`, nil `[0x604e38]`; nodes left, parent, right, key +0xc, value +0x10). Static constructor 0x411020, destructor registered with atexit (0x411040) |
| 0x6040c8 | the group the defaults add to |
| 0x6040d0 | the axis table: four 16-byte entries {source, scale, offset, dead zone}, throttle, twist, pitch, turn |
| 0x604110 | the table is "ready": the UseJoystick option (0x40d070) and the controls' mode (0x426650) rewrite it only then |
| 0x604111 | "stock": the controls file's four axes were the joystick's defaults; at start (0x480570, no joystick) it makes the table ready again |
| 0x60411c | the groups: a vector of 0x14-byte groups {CString title, a byte, vector<int> commands} |
| 0x604128 | the commands' entries, 0x14 bytes each for the 0xa7 commands: +0 the name's message, +4 the CMD_ name, +8 a byte set for the keys that also have an axis, +0x10 that axis (0 throttle, 1 twist, 2 pitch, 3 turn); +0xc unused. The controls screen reads them (0x42c160, 0x42c4e0) |

The static constructor 0x410950 starts the table with the joystick's axes
(throttle Z x -0.5 + 0.5 with a 0.05 dead zone, twist Rz x -1, pitch Y x 1,
turn X x -1), ready, not stock.

### The defaults, 0x411150

Frees the groups (0x410a80), then for each of seven groups makes it
(0x410b70: its title is a message) and adds its commands through 0x4110d0
(cmd, message, key1, key2, joystick button, mouse button, CMD_ name): zInput's
bind 0x51d2c0 with the message as the command's name, the command added to
the current group (0x410dc0), the entry's message and name, and the name
mapped to the command (inserted if new, then set). Last, command 1 is bound
to Alt-F4 (0x13e) with message 0x8a1 "Quick Exit": no group, no CMD_ name,
so neither the controls screen nor a controls file knows it. Returns 1. A
later call makes the groups again and keeps the map and the entries.

Keys are zInput codes (input.md: the DIK code, Alt 0x100, Ctrl 0x200, Shift
0x400). Mouse 1 left, 2 right. A bind takes a key from the command that had
it in the same slot only: Insert and Page Up are the turns' second keys and
the jet rotations' first keys at once, so both commands read them.

| group | command | name | keys | joystick | mouse |
|---|---|---|---|---|---|
| Movement | 0x18 `THROTTLE_UP` | Forward throttle | Up |  |  |
|  | 0x19 `THROTTLE_DN` | Reverse throttle | Down |  |  |
|  | 0x02 `TURN_LEFT` | Turn Left | Left, Insert |  |  |
|  | 0x03 `TURN_RIGHT` | Turn Right | Right, Page Up |  |  |
|  | 0x08 `TWIST_LEFT` | Twist Left | , |  |  |
|  | 0x09 `TWIST_RIGHT` | Twist Right | . |  |  |
|  | 0x04 `PITCH_UP` | Pitch Up | - |  |  |
|  | 0x05 `PITCH_DN` | Pitch Down | = |  |  |
|  | 0x06 `CENTER_TORSO` | Center torso to legs | Shift+/ |  |  |
|  | 0x07 `CENTER_LEGS` | Center Legs | / | 8 |  |
|  | 0x1a `THROTTLE_REV` | Toggle throttle direction | Backspace | 10 |  |
|  | 0x1b `THROTTLE1` | 0% throttle | 1 |  |  |
|  | 0x1c `THROTTLE2` | 10% throttle | 2 |  |  |
|  | 0x1d `THROTTLE3` | 30% throttle | 3 |  |  |
|  | 0x1e `THROTTLE4` | 40% throttle | 4 |  |  |
|  | 0x1f `THROTTLE5` | 50% throttle | 5 |  |  |
|  | 0x20 `THROTTLE6` | 60% throttle | 6 |  |  |
|  | 0x21 `THROTTLE7` | 70% throttle | 7 |  |  |
|  | 0x22 `THROTTLE8` | 80% throttle | 8 |  |  |
|  | 0x23 `THROTTLE9` | 90% throttle | 9 |  |  |
|  | 0x24 `THROTTLE0` | 100% throttle | 0 |  |  |
|  | 0x0c `JJ_UP` | Jump Jet | J | 7 |  |
|  | 0x0d `JJ_FORWARD` | Jump Jet Forward | Home |  |  |
|  | 0x0e `JJ_BACKWARD` | Jump Jet Reverse | End |  |  |
|  | 0x0f `JJ_LEFT` | Jump Jet Left | Delete |  |  |
|  | 0x10 `JJ_RIGHT` | Jump Jet Right | Page Down |  |  |
|  | 0x11 `JJ_ROTLEFT` | Rotate Left | Insert |  |  |
|  | 0x12 `JJ_ROTRIGHT` | Rotate Right | Page Up |  |  |
|  | 0x44 `GETUP` | Get up | G |  |  |
|  | 0x13 `CROUCH` | Crouch | C |  |  |
|  | 0x3f `SHUTDOWN` | Startup/Shutdown  | S |  |  |
|  | 0x6e `MASC` | Toggle MASC | V |  |  |
| Weapon | 0x3d `TRIGGER1` | Fire weapon | Space | 1 | left |
|  | 0x2a `WEPGRP_NEXT` | Next weapon group | ] | 4 |  |
|  | 0x2b `WEPGRP_PREV` | Previous weapon group | [ |  |  |
|  | 0x26 `WEP_PREV` | Previous Weapon in Group | Ctrl+[ |  |  |
|  | 0x25 `WEP_NEXT` | Next Weapon in Group | Ctrl+] |  |  |
|  | 0x27 `NEXT_WEAPON` | Next weapon  | Enter |  |  |
|  | 0x2c `WEPGRP_1` | Weapon Group 1 | F1 |  |  |
|  | 0x2d `WEPGRP_2` | Weapon Group 2 | F2 |  |  |
|  | 0x2e `WEPGRP_3` | Weapon Group 3 | F3 |  |  |
|  | 0x2f `WEPGRP_4` | Weapon Group 4 | F4 |  |  |
|  | 0x30 `WEPGRP_5` | Weapon Group 5 | F5 |  |  |
|  | 0x31 `MOVETOWG_1` | Assign weapon to group 1 | Shift+F1 |  |  |
|  | 0x32 `MOVETOWG_2` | Assign weapon to group 2 | Shift+F2 |  |  |
|  | 0x33 `MOVETOWG_3` | Assign weapon to group 3 | Shift+F3 |  |  |
|  | 0x34 `MOVETOWG_4` | Assign weapon to group 4 | Shift+F4 |  |  |
|  | 0x35 `MOVETOWG_5` | Assign weapon to group 5 | Shift+F5 |  |  |
|  | 0x28 `FIREMODE` | Toggle fire mode  | \\ | 5 |  |
|  | 0x39 `ALPHASTRIKE` | Alpha Strike | Keypad Enter | 6 |  |
|  | 0x41 `COOLANTFLUSH` | Flush Coolant | F |  |  |
|  | 0x40 `SHUT_OVER` | Override Thermal Shutdown | O |  |  |
|  | 0x3b `JETTISON_AMMO` | Jettison ammo | Shift+K |  |  |
| View | 0x61 `CYCLE_CAM` | Cycle Camera Mode | X |  |  |
|  | 0x62 `LOOK_LEFT` | Look Left | Alt+Keypad 4 |  |  |
|  | 0x63 `LOOK_RIGHT` | Look Right | Alt+Keypad 6 |  |  |
|  | 0x64 `LOOK_UP` | Look Up | Alt+Keypad 2 |  |  |
|  | 0x65 `LOOK_DOWN` | Look down | Alt+Keypad 8 |  |  |
|  | 0x67 `LOOK_REAR` | Look back | Alt+Keypad . |  |  |
|  | 0x66 `LOOK_FRONT` | Look forward | Alt+Keypad 5 |  |  |
|  | 0x6c `VMODE_TOGGLE` | Cycle Resolution | Shift+V |  |  |
| Targeting | 0x46 `TARGET_NEXT_E` | Target next enemy | E | 3 |  |
|  | 0x4d `TARGET_PREV_E` | Target previous enemy | Shift+E |  |  |
|  | 0x4c `TARGET_CLOSE_E` | Target nearest enemy | Ctrl+E |  |  |
|  | 0x47 `TARGET_NEXT_F` | Target next ally | W |  |  |
|  | 0x4e `TARGET_PREV_F` | Target previous ally | Shift+W |  |  |
|  | 0x4b `TARGET_CLOSE_F` | Target nearest ally | Ctrl+W |  |  |
|  | 0x48 `TARGET_CURSOR` | Target object under reticule | Q |  |  |
|  | 0x49 `TARGET_NONE` | Clear Target Selection | T |  |  |
|  | 0x59 `TARGET_HEAD` | Target Head | Keypad 8 |  |  |
|  | 0x5d `TARGET_LARM` | Target Left Arm | Keypad 7 |  |  |
|  | 0x5e `TARGET_RARM` | Target Right Arm | Keypad 9 |  |  |
|  | 0x5a `TARGET_TORSO` | Target Center Torso | Keypad 5 |  |  |
|  | 0x5b `TARGET_LTORSO` | Target Left Torso | Keypad 4 |  |  |
|  | 0x5c `TARGET_RTORSO` | Target Right Torso | Keypad 6 |  |  |
|  | 0x5f `TARGET_LLEG` | Target Left Leg | Keypad 1 |  |  |
|  | 0x60 `TARGET_RLEG` | Target Right Leg | Keypad 3 |  |  |
| Cockpit | 0x14 `RETICULE_ZOOM` | Zoom Reticule | Z |  |  |
|  | 0x15 `ZOOM_LOCK` | Lock Zoom reticule | Keypad 0 | 2 |  |
|  | 0x16 `TOGGLE_TMODE` | Toggle Torso Mode | Ctrl+Keypad 0 |  |  |
|  | 0x17 `FREELOOK` | Freelook | L |  | right |
|  | 0x74 `DAMAGE_TOGGLE` | Toggle Damage display | D |  |  |
|  | 0x75 `EDAMAGE_TOGGLE` | Toggle Enemy Damage Display | Ctrl+D |  |  |
|  | 0x68 `RADAR_TOGGLE` | Toggle Radar size | R |  |  |
|  | 0x69 `RADAR_RANGE` | Toggle Radar mode | Alt+R |  |  |
|  | 0xa5 `RADAR_MAP_TOGGLE` | Toggle Radar map | Shift+R |  |  |
|  | 0x6b `HUD_TOGGLE` | Toggle HUD on/off | H |  |  |
|  | 0x3a `AMS_TOGGLE` | Toggle AMS on/off | A |  |  |
| Communications | 0x54 `DIRECT_ATTACKT` | Attack my target | F6 |  |  |
|  | 0x55 `DIRECT_DEFENDT` | Defend my target | F7 |  |  |
|  | 0x52 `DIRECT_FOLLOW` | Follow Me | F8 |  |  |
|  | 0x51 `DIRECT_PAUSE` | Hold position | F9 |  |  |
|  | 0x56 `DIRECT_REFITT` | Repair in MFB | F10 |  |  |
|  | 0x4f `DIRECT_POCKET` | Lancemate command screen | F11 |  |  |
|  | 0x57 `DIRECT_AWAITCMD` | Lock Awaiting orders | Shift+F11 |  |  |
|  | 0x58 `DIRECT_AWAITCMDCANCEL` | Cancel Awaiting orders | Ctrl+F11 |  |  |
|  | 0x73 `MCB_COMMAND` | Order MFB | M |  |  |
|  | 0x36 `NAVPT_NEXT` | Next Op point | N |  |  |
|  | 0x37 `NAVPT_PREV` | Previous Op point | Shift+N |  |  |
|  | 0x38 `NAVPT_NEAR` | Nearest Op point | Ctrl+N |  |  |
|  | 0x76 `CHAT_TEAM` |  Send message to team | Keypad - |  |  |
|  | 0x77 `CHAT_ALL` |  Send message to all | Keypad * |  |  |
| Mission | 0x43 `OBJECTIVES` | Mission Objectives | F12 |  |  |
|  | 0x71 `MISSION_COMPLETE` | Exit Mission | Ctrl+Q |  |  |
|  | 0x70 `MESSAGE_TOGGLE` | Message Window | Grave (the key left of 1) |  |  |

The texts are Mech3Msg.dll's. THROTTLE1..0 set 0, 20, 30, .. 90 and 100%
(0x4274d0; THROTTLE2 sets 0.2, though its text says 10%), negative in
reverse.

Commands with a handler but no default key and no CMD_ name (0x29, 0x3e,
0x42, 0x45, 0x53, 0x72, 0x7e, 0x92, 0x99, 0xa1, 0xa6; set_callback in
0x40db60, 0x426ce0, 0x48f400) cannot be bound from the screen or a file;
nothing in the release binds them, likely the debug key setup (0x40e430,
not traced).

### The controls file

A text .zrd (zreader.md, Text files), written by 0x411e60 (fopen "w",
fprintf):

```
(
	KEYS (
		(CMD_ALPHASTRIKE	keya(0x9c)	joybtn(0x6))
		...
	)
	AXES (
		(Throttle	joystick(Z)	slope(-0.500000)	intercept(0.500000)	deadzone(0.050000))
		...
	)
)
```

KEYS lists every name in the map in strcmp order (0x410fe0 finds each one's
command again) with what it has: keya (key1), keyb (key2), mousebtn,
joybtn, each written only when not 0. AXES lists each axis whose source is
not 0 by name (Throttle, Twist, Pitch, LR for the turn) with `joystick` (a
source below 9) or `mouse` and the source's name (0x4300e0: X, Y, Z, Rx, Ry,
Rz, Slider0, Slider1; 9..11 X, Y, Z), then the three floats as `%f`.

The reader 0x4120a0(path) loads it with zrdr::load (an archive first by its
file name alone, then the text file). KEYS (found anywhere, depth first):
zInput's map is cleared (0x51d1d0(0), the callbacks kept); each entry's
first item names the command (0x410fe0; a name not in the map is added as
command 0, so its keys end up on command 0, which does nothing), its four
values are read by 0x412520 (a string `0x..` in hex, a string starting with
a digit in decimal, anything else 0: `keya(28)` is an int and reads as 0,
`keya(28x)` as 28) and bound with no name; then the map's tables are
rebuilt (0x51d1c0). Every command the file leaves out ends unbound, Quick
Exit too. AXES: every source is set to 0 and the table is not ready; each
entry names an axis and a source (`joystick(..)` 1..8, else `mouse(..)`
9..11, an unknown name 0) and the three floats (defaults 1, 0, 0); an entry
with neither source keeps the source of the entry before it, for the first
the list's count (a register left as it was). An entry that is exactly the
joystick's default for its axis marks that axis stock (never unmarked by a
later entry); stock is set when all four are. The table stays not ready.

Writing and reading back are exact for every value the writer produces;
floats keep the six decimals `%f` gives.

### Quirks and bugs

- **An axis named other than the four** is written as a fifth entry,
  0x604110..0x60411f: the flags (reset after), two bytes after them and the
  first two words and the begin pointer of the groups' vector, so the game
  crashes at exit or in the controls screen. FIX (bug): skipped.
- **An entry of KEYS or AXES that is not a list starting with a name** (or
  a KEYS or AXES that is not a list) is read as one: a crash. FIX (bug):
  skipped.
- **The name map keeps the pointer it was given**: for a name read from a
  file, a string the reader frees at the end of the load. FIX (bug): the
  map keeps a copy.
- A file named like one of reader.zbd's (`keys\detail.zrd`) loads the
  archive's file instead: nothing in it is KEYS or AXES, so nothing changes.

- **A saved stick axis with no stick walks the mech.** With no stick, 0x00480570
  sets `ready` only for a file exactly like the defaults (`t.stock`), which lets
  `set_use_joystick(0)` move the axes off the stick. Any other file keeps them, and
  `read_axes` returns at "no stick" with `*thr` already the axis's offset, added to
  the throttle each frame. Measured on the program, a slider binding (offset 1) or
  stick Z with deadzone 0.1, c1m1: target 1.00, 15.7 m in 150 frames; a slider on
  twist or pitch (offset -1): the torso at -1.00. Unplugged in a mission it is not so:
  the turning axes read centred, the throttle's (Z, sliders) their last value, so the
  mech holds its speed and keys still change it (`neutral`, W54). Fixed behind
  `bugs` (fixes.md); read from the port, not run on the exe.

### Where the file lives

FIX (A): the original's `keys\` is relative to the game's folder (its
working directory); the port reads and writes it in the write dir through
zutl::open, which makes the folder (the screen's `_mkdir`), and reads it
from the mounts too. Text mode as the original: CR LF on Windows, LF
elsewhere; the reader takes either. ActiveCtlCfg starts as
"DefaultCtlConfig" (message 0xfc0), a file only the controls screen's two
reset buttons write, so until one of them is pressed or a profile is saved
or loaded there the bindings are the defaults plus what the registry's
DefaultCmdMap restored.

### The controls' mode, 0x426650 (ported)

`MechControls::set_mode(mode)`: +0x08 = mode, +0x3e cleared; while the
table is ready, bit 4 puts the joystick's axes in it (as 0x410950), else
bit 1 the mouse's (pitch Y = 10, twist X = 9, scale 1, throttle and turn
unbound), each leaving it ready. Then the joystick is taken once more
(0x51b2c0), on every call: its use count grows. Callers: the player's
controls (0x427280, 0x427320), the mech (0x41cf30, which turns mode 4
into 3 without a joystick), the lance orders' wrappers (0x469400, 0x469650,
0x469940, 0x46ab90, 0x46ad80).

### Testing

`test_ctlcfg.py`: one run of 400 random steps on both sides, the original
from its static constructors (0x410950, 0x411020) on: the defaults (again
and again), controls files (as the writer makes them with random bindings
and axes; unknown command names, decimal and odd values, missing sections
and keys, axes with neither source or an unknown one, the stock axes exact,
one value off, and followed by another entry; unknown axis names, whose
bytes the original writes over are put back after its read), saves (the
text compared, and read back), the controls' mode on random tables, the
groups freed. zInput's map and the messages are stubs on both sides, every
call logged and answered from one model; after each step the command
entries, the groups, the name map in order and the axis table must match.
Result: 400 steps identical. Breaking the port showed the test catches it,
17 of 17: unknown names not added, KEYS not clearing the map, joystick and
mouse buttons swapped, decimal values ignored, a missing source as 0, the
stock dead zone unchecked, a later entry unmarking stock, AXES leaving the
table ready, mouse source Z unknown, the writer's button order, source 9
written as a joystick's, one group only, Quick Exit unbound, the free
look's mouse button dropped, the mouse mode leaving the turn bound, the
joystick taken only when ready. `mw3tests` (`test_controls_cfg.cpp`): the
defaults, a save and load on the real zInput map, the stock flag, and the
fixes the original cannot be run on (it crashes there).

## Who sets Mech +0x37c

| site | what |
|---|---|
| 0x414913 | mech ctor path: = 0 |
| 0x414d7b | mech ctor 0x4149f0: = ctor argument, then IControls::SetOwner(this) (0x414d81) |
| 0x415231 | mech dtor 0x414f40: deletes controls (vt+0 with 1) when the mech owns them, then = 0 |
| 0x41aa7c | 0x419fc0 (likely death/cleanup): if `[ctrl+0x38]`, clear it, ctrl = ctrl->primary vt+4() (likely unwraps an order wrapper) |
| 0x41c689 | mech command handler 0x41be80: lance orders swap the controls through 0x469930(ctrl, kind): CMD_DIRECT_PAUSE 0x51 = 1, FOLLOW 0x52 = 0, POCKET 0x4f = 2, 0x53 (no CMD_ name in the binding table) = 9, DEFENDT 0x55 = 0xb, REFITT 0x56 = 0xc. Only when 0x479480([0x60861c]) is 0 |
| 0x46e130 | net mech class: = 0 |
| 0x491120 | `Mech::SetControls(ctrl)`: store, then ctrl->SetOwner(this). Callers: 0x46ad70 (pocket mode ends, restores the old controls), 0x49de3d (SetControls(0)), 0x49e223 (new AIControls 0x4d8750(0, esi, ebx, 1), player.cpp 0x49dc80) |
| 0x49ff9d, 0x49fff0, 0x4a00e9 | **not mech controls**: 0x49ff80 writes 0/1 flags into a different object (`[edi+0xe4]`) at +0x374/+0x378/+0x37c |

0x469940 kinds: 0, 1, 3..12 wrap the current controls by passing a 12-byte member-function
pointer (0x4ea000, 0x4ea040, ... 0x4ea230, AI code) to 0x469400 or 0x469650. These are
likely lancemate AI orders. Kind 2 toggles PocketControls: on, it saves the player singleton
(+0x15c), takes over `[0x6056ec]`, and sets camera mode 5 (0x40ae70). Off, it copies the
state fields into the wrapped controls (+0x158), puts the singleton back, and deletes itself.

## AI controls

AIControls 0x4d8750(owner, a2, a3, isMostDerived). It calls 0x4d8670 and puts the
virtual base at +0x30c. Its Update (0x4e96f0) adjusts `this` to the sub-object at +0x5c
(vtable 0x5cd2c8, code in 0x4e9xxx/0x4eaxxx, near zrdrvalve.cpp/zclass) and runs
0x4e9710, which is likely the AI brain, and returns its byte as 0 or 1 (ported as
`AiControls`; the brain is `ControlsCalls::ai_brain`, and until it is ported the
smoothing alone stands in; `test_player.py`). The brain runs the smoothing itself
through 0x4d89f0. PlayerControls also has a +0x5c sub-object
(0x4d9840, vtable 0x5bd708), likely a shared AI/pilot base. I did not trace how the AI
brain sets throttle and turn, but it can only do so through the same IControls setters.

## The mech's commands, 0x41be80 (ported)

`Mech::command` (`mech3/mech_command.cpp`), vtable +0xfc. The player's keys
reach it through 0x426cb0 (`player_command`): the controls' +0x1c first, then
this on the vehicle at the view's +0x64. Two switches. The first runs only
while the mech is active (+0x08):

| Commands | What happens |
|---|---|
| 0x06 / 0x07 CENTER_TORSO / LEGS | 0x465a00(1 / 2) on the torso (hook) |
| 0x13 CROUCH | `crouch(!crouched)` unless powered down (vt+0xa0, 0x41cc10) |
| 0x14 RETICULE_ZOOM, 0x48 TARGET_CURSOR | with the screen: the object under the reticle (0x444330, 0x40eee0), handed to 0x4a7b40 after a side check (vt+0x28 +0xc0 against +0xc4 and vt+0xac for 0x14; vt+0xb0 for 0x48). HUD hook |
| 0x15 ZOOM_LOCK | 0x442e00 on screen +0x1508 (with no screen too), sound 0x17 kept at +0x614, then 0x14's |
| 0x25 / 0x26 WEP_NEXT / PREV | the manager's 0x4b0790 / 0x4b07e0 |
| 0x27 NEXT_WEAPON | the weapon the HUD highlights (screen +0x1795c): its group selected (0x4b0730), and if that took, the weapon selected in it (0x4b1de0) |
| 0x28 FIREMODE | mode = (mode + 1) % 3, the group selected again, 0x4b0830's sound |
| 0x2a / 0x2b WEPGRP_NEXT / PREV | 0x4b04f0 / 0x4b0610 |
| 0x2c..0x30 WEPGRP_1..5 | `select_group(cmd - 0x2c)` |
| 0x31..0x35 MOVETOWG_1..5 | the current weapon (0x4b04c0), if any, into that group (0x4b1e10), then as 0x27 |
| 0x36..0x38 NAVPT_NEXT / PREV / NEAR | [0x609980] vt+0x60 / +0x64 / +0x68(0x5a85e0's position) (hook) |
| 0x39 ALPHASTRIKE | 0x4b0a70(0.2) unless powered down |
| 0x3a AMS_TOGGLE | the AMS's on byte flipped, when there is one |
| 0x3b JETTISON_AMMO | 0x4b0ae0; when it did, mech sound 0x10 |
| 0x3e (no key) | 0x41d070 (hook) |
| 0x3f SHUTDOWN | `power(0)` when powered down, else `power(1)` |
| 0x40 SHUT_OVER | 0x419190(1), the override (heat.md) |
| 0x41 COOLANTFLUSH | 0x419e60 (heat.md, Coolant flush) |
| 0x44 GETUP | 0x41b840 |
| 0x46, 0x47, 0x4b..0x4e | the target keys (zai_target), each with side 0 |
| 0x4f, 0x51..0x56 | lance orders: unless 0x479480 says multiplayer, +0x37c = 0x469930(+0x37c, order): POCKET 0x4f = 2, PAUSE 0x51 = 1, FOLLOW 0x52 = 0, 0x53 = 9, ATTACKT 0x54 = 0xa, DEFENDT 0x55 = 0xb, REFITT 0x56 = 0xc (hook) |
| 0x57 / 0x58 DIRECT_AWAITCMD / CANCEL | +0x78 = +0x6c / 0 |
| 0x59..0x60 the part keys | 0x41d810(head 6, centre torso 1, left torso 7, right torso 8, left arm 2, right arm 3, left leg 0xb, right leg 0xc) (hook) |
| 0x62..0x67 the look keys | the view's camera (+0x58) vt+0x10(left 0, right 1, up 4, down 5, front 2, rear 3) (hook) |
| 0x68, 0x74, 0x75, 0xa5 | with the screen: 0x43da40, 0x43d240, 0x43d250, 0x43dc10 (HUD hook) |
| 0x69 RADAR_RANGE | not passive (+0xbc bit 2): below the reach (2000 with a C3 computer, equipment kind 6, else 1000) the range doubles (0x4d6d20) and that is all; at it, 500 and passive. Passive: 500, bit cleared. Both then vt+0xb8 (0x41ded0, the HUD's light) |
| 0x6b HUD_TOGGLE | with the screen, unless powered down: screen vt+4(screen +4 == 0) |
| 0x6e MASC | with MASC (kind 12): `masc(masc_time == 0)` |
| 0x73 MCB_COMMAND, 0x76 / 0x77 the chat keys | 0x47a910, 0x41e4e0(0 / 1) (hooks) |

The second runs always: 0x16 TOGGLE_TMODE (view 0 or 1 → 0x426780(2), 2 or 3
→ 0x426780(0)), 0x17 FREELOOK (view 0 → 0x426780(1); view 1 → 0x426780(0)
and the camera vt+0x10(2)), both then storing the controls' view (+0xc)
through [0x5f4d44] (the options' MouseMode); 0x43 OBJECTIVES (the screen's
+0x17964 vt+0x60(-1) or +0x64 by +0x17970 bit 4); 0x49 TARGET_NONE (vt+0xc0,
0x41d890: `set_target(null, 0)` and the HUD's lock); 0xa6 (no key) 0x40f070.
Every command ends in the controls' vt+0, `ret 4` for every controls class.

The screen is 0x430aa0 on the pilot (+0x684), only for the view's mech.
The program wires the handler on the pilot's mech (`WiredBuild::
view_target_command`, `app_mech.cpp`); the HUD, camera, torso, nav, lance and
chat calls stay hooks, logged once by the engine as unported. 0x426780 is
`update_calls().controls_view`, unported.

Tests: `tests/differential/test_command.py` runs 0x41be80 in Unicorn with
every callee a logging stub against the port (`CommandCalls::own` answers the
mech's and the manager's calls the same way): 6,000 random commands of 97
kinds and a sweep of the radar's edges, the call log and the fields written
identical; 0x419190 and 0x419e60 run for real, 3,000 cases identical.
`mission_c1m1_player_commands_headless` (mw3tests) drives the Thor (c1m1 with
`l.mech = 17`: the Mad Cat has no jump jets) by keyboard: F2 selects the
second group, S powers down, it stays down 30 frames, S powers up, J held
fires the jets and lifts it.

## Open

- IControls +0x04 and +0x54 (AI overrides), +0x10 (byte +0x34), and the full
  ClearCommands body (0x428490 continues past 0x4284ef).
- MechControls +0x0c states 0..3 (0x426780).
- The controls screen's load/save popup (menus.md section 10) and what the
  commands without a key were for (Setup).
- Names of the classes: there is no RTTI. File guesses come from func2file neighbours only.
