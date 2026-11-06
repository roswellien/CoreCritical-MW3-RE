# Mech movement and physics

Mech3.exe 1.2, 2026-10-20. How throttle and turn become motion, from the asm.
The pieces:

- this file: the frame, controls, gait, turning, the motion integration, the
  animation objects, and what is ported;
- `movement-jump.md`: jump jets, landing, falls, getting up, knockdown;
- `movement-ground.md`: the ground probes ("skis"), the terrain query, the
  feet, torso, hip and arm updates;
- `movement-collision.md`: buildings, mechs, vehicles, touch damage, mines.

Axes: the local velocity +0x3c is (x right, y up, z), **forward is -z**
(`movement-jump.md`). Units are metres and seconds.

## The frame (Mech::Update 0x419fc0, vtable +0x34)

```
dt = +0x6b0 += dt                  accumulated: a frame the update budget 0x5a8910 skips carries over
+0x594 (jet impulse vector) = 0
budget gate 0x5a8910; FinishPowerDown; +0x414 += dt
state 14 (suspended): return.  NARC timer +0x570 -= dt
dead (10) with no action queued: deactivate, stop all sounds, 0x41e350, return
player: camera view keys (0x426780)
controls Update (vt +0x0c) unless dead with actions; 0 -> skip to the tail
0x5a8960; player 0x41ce60(dt)
yaw rate +0x5c += -yaw * 3 dt, 0 below 0.05; +0x58 = +0x60 = 0
not jetting and not airborne: local x velocity +0x3c = 0
if controls +0x58 || +0x59 (enabled): actions queued ? ActionUpdate 0x416860 : Gait 0x4181c0
state 12/13, no actions, powered: lying timer +0x590 -= dt, GetUp at < 0 (local)
no action: Turn 0x418fc0, torso inputs, torso and hip Update
collision (+0x5b0): Collide 0x4225c0
no action: legs and arms Update
powered down: angular and local velocity = 0
lean (detail bit 5): +0x54c toward def lean_angle * |thr| * turn * speed factor
no block: orientation +0x48 = normalize(expmap(dt/2 * w) * q)
collider stop: local velocity = 0 but y
world velocity +0x30 = rotate(q, local) (+ push +0x5a0 unless blocked); move the node by +0x30 dt
push decay or throttle stop (movement-collision.md); landmines; vt +0x14c
weapons; 0x419200; UpdateHeat; movement sounds; controls swap; 0x4e6480; +0x6b0 = 0
tail: vt +0x158(dt), knock decay, 0x413cb0
```

## Throttle and turn (MechControls::Update 0x426960)

The controls object (`controls.md`) holds the commanded targets; every kind
of controls ends its update in this base smoothing (the network class uses
only it):

```
powered down: zero every axis (0x426c80)
throttle: |target - cur| < 0.009: snap; else rate = |cur| <= |target| ? +0x530 : +0x534,
          k = exp(-rate dt) (fast exp: float bits ftol(x * 12102200) + 0x3f800000),
          cur = (1 - k) target + k cur
turn:     cur += (target - cur) * +0x538 (x2 with flag 4) * dt, snap within 0.05
jump:     cur += (target - cur) * (target == 0 ? 9 : 7) * dt, snap within 0.05
```

`controls.md` had 7 and 9 the other way round. +0x530 / +0x534 / +0x538 are
the definition's `accel_rate`, `decel_rate`, `turn_accel_factor` (config
+0x7c..+0x84, copied at 0x4144a5). SetThrottle 0x426b60 clamps to -1..1 and
sets the reverse flag by the sign.

## Speed and gait (Gait 0x4181c0)

The throttle is a fraction of the gait table, not of a speed. Per mech:

```
D = state not 1/2/3 and |throttle target| >= step.hi (+0x4bc)
fallen (12/13) or powered down: return
F = both feet planted (+0x94 == 3); E = turning or torso centring legs (+0x1b0 == 2)
airborne: Move(dt, no anim, throttle); return
f = 1; heat > 0.6: f *= clamp(0.6 / heat, 0.6, 1)
d = shallower foot's water depth; d > 0.1 H: f *= d >= H ? 0.7 : 0.7 (H - d) / H   (wading FF effect)
limping: f *= 0.6; reverse: f *= 0.6
+0x52c = f; speed factor +0x528 += (f - +0x528) * 0.6 dt
thr *= +0x528; spd = |thr|; crouched and spd > step.hi: stand up
leaving stand/step (index <= 3, spd >= walk.lo, !D): copy the step's phase to walk and the
  step/walk blend; wait for a footfall of the step (its event times within 1.25 frame dt
  of the phase t / loop, by direction); until then hold spd at walk.lo - 0.0001 and
  thr2 = clamp(5 thr2, +-5 L)
may stop = !E && (spd <= step.lo || (spd <= walk.lo && target == 0)) && !D
           and, from the step with a foot up, only within 2 dt of the step's loop start
detail bit 4 clear (far away): walk, state 5
limping (+0x5ac 0/1) with a limp blend: stop -> stand; spd >= walk.lo -> limp (state 0,
  index 8, the blend's weight 0.5, thr clamped to trot.lo)
stop: stand (index 2, state 1), +0x58c = 0
spd < walk.lo && !D: step (3), speed (0.4 + thr2) * 2.5, +0x58c = 0
spd < step.hi && !D: step/walk blend (8, state 4), weight (spd - walk.lo) / (step.hi - walk.lo)
spd < trot.lo: walk (4, state 5);  < walk.hi: walk/trot (9, 6);  < run.lo: trot (5, 7)
spd < trot.hi: trot/run (10, 8);   > run.lo: run (6, 9); then +0x58c++
each switch copies the old animation's phase into the new one (0x41ef00)
```

**Deep water, a fix.** The water factor is meant to slow a wading mech the
deeper it goes, to 70% when the water reaches its height: the deep case
(d >= H) is exactly 0.7, the constant the shallow case multiplies by. But the
shallow case multiplies 0.7 by `(H - d) / H`, the part of the mech still above
the water, so the factor starts at 0.63 at a tenth of the height, falls to a
standstill as the water nears the top, and jumps back to 0.7 deeper still: a
mech crossing a river of its own height stops dead in it (Owen: game-breaking).
The port draws the line it was meant to be, from full speed at 0.1 H to 0.7
at H, then 0.7 (`FIX (bug)`, behind `zfix::options().bugs`; the probes turn it
off, so `test_gait.py` checks the exe's curve; `tests/test_fixes.cpp`
checks the line). The step from 1 to 0.63 at 0.1 H goes with it.

**Per frame, now per second.** The movement's per-frame steps (a fall's
momentum x0.6, the skis' step, slope and ledge pushes and the far mechs'
push x3, the jets' climb cap of 6, the 20-frame blend-in) run per 1/30 s in the port (`FIX (B)`, `zfix::frame_steps`,
fixes.md), equal to the exe's at 30 fps. So do the throttle keys' 0.025 a
frame (controls.md).

States (+0x578, previous +0x57c) are named in `movement-jump.md`. Animation
slots (+0x424 + 4i, made by 0x415ca0): 0/1 limp left/right, 2 stand, 3 step,
4 walk, 5 trot, 6 run, 7..10 the blends stand/step, step/walk, walk/trot,
trot/run, 11 walk/limp (StartLimp), 12 the jump pose (the jets play it; the stand when missing).

**The gait table** (+0x4a0, 13 pairs lo/hi): the mech data's "trans" pairs
read from each mech's `animations / motion / {stand, step, walk, trot, run}`
(0x431350, mech_data.cpp), with stand.hi forced to 0.01, step to (0, 0.05)
and walk.lo to 0.02 whatever the file says (0x431a70), then scaled by
`base_speed / max_speed` each time the max speed changes (0x4329c0 from
set_max_speed 0x4143c0). So MASC (max speed x1.25) lowers every threshold by
0.8. The mech offsets: +0x4b8 step.lo, +0x4bc step.hi, +0x4c0 walk.lo,
+0x4c4 walk.hi, +0x4c8 trot.lo, +0x4cc trot.hi, +0x4d0 run.lo.

## Move (0x417a80) and root motion

```
grounded, local: knockdown check (movement-jump.md)
anim: SetSpeed(speed) (vt 08), Advance(dt) (vt 10), GetVelocity (vt 20): writes +0x44 only
jump jets 0x4173b0; ground and landing 0x4162c0
no anim or far away: the feet's ground update (0x462a10) and return
sample the feet (in the ski frames), hip (its z dropped), torso, arms, hands, toes
the first 20 frames of walk or faster without jets: blend from the last pose by
  clamp(+0x58c / 20, 0.15, 1), velocity included (stuck at 0.15 in the step/walk blend,
  where +0x58c stays 0); the right foot's rotation is never blended (q0 used twice: bug)
leg IK 0x463e40 per leg; hip, torso, arms, hands, toes set
```

So the speed sets the animation rate and the animation's hip velocity is the
mech's forward speed: `+0x44 = hip velocity(t).z * rate`, rate = speed x
stride multiplier (`s * loop / stride`, set by 0x41ef90 from the max speed).
Stand and step have no velocity keys: they move nothing.

## The animation objects (mech_anim.cpp)

| Class | Size | Vtable |
|---|---|---|
| MechAnim (over CZSIScript) | 0xdc | 0x5bd1f0 |
| BlendAnim (A +0xe0, B +0xe4, weight +0xdc) | 0xe8 | 0x5bd240 |
| LegOneShot (the actions) | 0xf0 | 0x5bd290 |

MechAnim: script +0x04 rate, +0x08 stride multiplier, +0x0c stride, +0x54
loop, +0x88 time t (before the rate), +0x8c stride speed, +0xd0 / +0xd4 the
left and right footfall times, +0xd8 a quarter cycle (0x41fd30). The script's
active byte +0x10 is set by these constructors, and the loop time is forced
to frames / 30 (/ 15 for the one-shots) after loading. Methods: SetSpeed
0x41ee80 (`f = mult x; if f: t = rate t / f; rate = f`), Advance 0x41ef60
(`t += dt`), Seek 0x41fce0 (`t = rate * event[i]`), CopyPhase 0x41ef00,
SetStrideSpeed 0x41ef90. Sampling: `phase = frac(rate t / loop)`, keys lerped
and slerped (0x525ce0); velocity keys `(trans[i] - trans[i-1]) * 30`
(0x5aac60).

## Turning (Turn 0x418fc0)

```
turn (controls +0x14) == 0: return (the yaw rate keeps decaying)
a = 0.85 (airborne)  else a = |throttle|, and below 0.99 walk.lo the throttle (current)
                      is set to +-0.99 walk.lo: turning makes a standing mech creep
w = turn * (1 - 0.8 a) * +0x520           +0x520 = def max_turn_rate * 0.5
detail bit 2 and a pivot leg (0x418e00: the one planted foot, else the lower): the local
  velocity += pivot foot position x (0, w, 0); that foot counter-spins (0x462dc0(-w))
angular velocity +0x58 = (0, w, 0)
```

## Integration

The yaw rate decays by 3/s; the orientation quaternion +0x48 (w, x, y, z)
steps by `expmap(dt/2 * w)` (0x525900 uses the full angle, so this is the
exact rotation) and is normalised with the bit-trick square root (0x525500);
the world velocity is the local one rotated (0x525840) plus the push +0x5a0;
the node moves by velocity * dt (0x4fc1b0). The lean +0x54c (detail bit 5)
rolls the display matrix only.

## MASC (0x41df50)

With MASC equipped: on, max speed x1.25 and the throttle rescaled to keep the
current speed, +0x574 = 0.001; off, the base max speed with no rescale, so the
speed drops at once.

## Ported and checked

All at the game's 24-bit x87 precision, bit for bit unless noted; the calls
into code not ported go through `MoveCalls`, `SkisCalls` and the existing
`HeatCalls` / `DamageCalls`, stubbed and logged identically on both sides.

| Piece | Original | Port | Test |
|---|---|---|---|
| Controls smoothing, SetThrottle, zero | 0x426960, 0x426b60, 0x426c80 | `mech3/mech_control` | `test_controls.py`: 400 runs x 120 frames |
| Gait choice, turning, pivot leg, foot pivot; CopyPhase, Seek, SetSpeed, Advance, SetStrideSpeed | 0x4181c0, 0x418fc0, 0x418e00, 0x462dc0; 0x41ef00, 0x41fce0, 0x41ee80, 0x41ef60, 0x41ef90 | `mech3/mech_move`, `mech_anim` | `test_gait.py`: 30000 calls on the 19 shipped gait tables (`mechdata.py`), every gait and state |
| Ground probes: Update, parabola, height, water, ski pitch, ground pick, integrate, slope slide; the vector helpers | 0x4765b0, 0x475c80, 0x475de0, 0x475ea0, 0x4764a0, 0x477820, 0x477940, 0x477620; 0x4020d0, 0x51e3d0, 0x51e210, 0x525840 | `mech3/mech_skis`, `gamez/zmath/zmth_vector` | `test_skis.py`: 14400 frames walking, jetting and falling on the six worlds, the original's ground query 0x4ee2f0 run on each; plus made-up ground for the pushes. The ski pitch's sine and cosine within 2e-7 (the CRT asin is an x87 stand-in) |
| Move, jump jets, AddJumpJet, landing (with the knockdown it starts) | 0x417a80, 0x4173b0, 0x417300, 0x4162c0 | `mech3/mech_move` | `test_jets.py`: 40000 calls |
| Action queue and state machine: Queue, ActionUpdate, StateForAction, SoundForAction, DeathFall, Stagger, GetUp, KnockedDown on the real queue; the action's Reset, Advance, IsStarted, Stop | 0x416780, 0x416860, 0x416660, 0x416720, 0x41b760, 0x41b7f0, 0x41b840, 0x41b660; 0x420f40, 0x420f80, 0x421000, 0x421060 | `mech3/mech_move`, `mech_anim` | `test_actions.py`: 1500 runs, 114750 calls through falls, lying down, getting up, deaths, remote mechs |
| Mech::Update whole: the entity budget, death stop, view key, yaw decay, lying countdown, the call order (gait or actions, turn, torso inputs, part updates, Collide), lean, orientation step, world velocity and push, display matrix, push decay or throttle stop, hit timeout, movement sounds, controls unwrap, knock decay; the quaternion helpers; object3d translate-by | 0x419fc0, 0x5a8910, 0x5a8960, 0x466200, 0x466240, 0x41d190; 0x525210, 0x525500, 0x5255d0, 0x525900; 0x4fc1b0 | `mech3/mech_update`, `gamez/zentity/zent_manager`, `zmath/zmth_vector`, `zclass/object3d` | `test_update.py`: 20000 frames, every call out logged; 12 of 12 breaks caught |
| Collision: Collide whole, probe setup, the sweep, query and sort, the wall (with reflect), ceiling, vehicle, mech and touch responses, the bump off nearby mechs, the network's record, TurnRate | 0x4225c0, 0x422360, 0x422d20, 0x422ef0, 0x423010, 0x4231c0, 0x51e280, 0x423600, 0x4237c0, 0x423a20, 0x423c90, 0x418f70 | `mech3/mech_collide` | `test_collide.py`: 20000 Collides on scripted crossings (the query itself is in `test_di`); 17 of 17 breaks caught |
| The engine's ground and segment queries (cls_di.c) with their node walks, box tests, the model's polygon tests, the 2D box corners, the textured crossing's UV gradient | 0x4ee2f0, 0x4ee790, 0x4eeb50, 0x4eec10, 0x4f1350; 0x4efcd0, 0x4efef0, 0x4f0480, 0x4f08c0, 0x4f09b0, 0x4f1640; 0x54c5f0, 0x54c330, 0x54f450, 0x54e2b0, 0x54ea30, 0x54fa10; 0x4ec150, 0x522d60, 0x5240d7 | `zclass/cls_di`, `zmodel/gmod_hit`, `zclass/class`, `zmath` | `test_di.py`: 9000 ground and 9000 segment queries on the six worlds (half of them with a quarter of the polygons made strips and some nodes boxed), every record slot, the model caches and node flags |

Breaking the port showed each test catches what it covers: accel and decel
swapped, the jump thrust rate, the deeper foot slowing instead of the
shallower, the turn creep at walk.lo, the limp's slot 11, water as ground,
gravity x3 on the ground, the ledge threshold, the ceiling's 4 s, the jet
refill rate, the sideways landing's fall chance, the blend-in floor, landb
after fallb, the time lying down, the stagger direction: 15 of 15 caught.

Corrections found while porting the frame: Collide's result is
`== 1` (blocked), its booleans read straight; the orientation step's sine goes
through a float before the division (fsincos, not fsin); the display matrix
is not refreshed on a frame the collider stops the mech (+0x374, +0x375 or
+0x377); a mech lying down only skips the turn and the part updates when
powered (a powered-down one turns and updates them); the lean is halved in
the cockpit view; 0x413cb0 gets the frame time after it was cleared (0 on a
full update). The shared matrix product (0x51fee0, 0x51fb30, 0x521670) adds
its z column from the last term and its z translation with t[11] first: the
port's product had the textbook order, an ulp off now and then (fixed in
zmth_matrix; the render tests pass either way, the segment query caught it).

## Ported since: animations, the mech data, the pose

| Piece | Original | Port | Test |
|---|---|---|---|
| zSIScript sampling: phase, translation, rotation, velocity and their blends, slerp, velocity keys, resample, key rotation, mirror, find / free parts | 0x5abae0, 0x432a80, 0x5aafe0..0x5ab8c0, 0x525ce0, 0x5aac60, 0x5aa780, 0x5aaa40, 0x5aa940, 0x5aa270, 0x5aa310 | `gamez/zsiscript`, `zmath` | `test_mechanim.py`: every gait and one-shot of the 19 mechs loaded from motion.zbd on both sides (272 motions, every key), 7600 random sampler calls; 15 of 16 breaks |
| MechAnim (vtable 0x5bd1f0: init 0x41f580, footfalls 0x41fd30, start at the left foot forward 0x41fc40, mirror 0x41f680, copy 0x41f180), the blend 0x5bd240, the one-shot 0x5bd290, the loaders 0x41f000 / 0x420d00 | | `mech3/mech_anim` | the same |
| The mech type's load 0x431a70 (gaits 0x431350, one-shots 0x4315d0), copies 0x431fb0 / 0x432030, the gait table 0x4329c0, the engine speed 0x433030, MASC 0x41df50, set_max_speed's calls | | `mech3/mech_data`, `mech_move` | `test_mechdata.py`: the original's load run whole on each mech's reader tree; 275 animations, copies, gait tables, 114 MASC switches; 10 of 11 breaks |
| set_pose (base, place, hip, foot, torso), the leg IK, the gait, jump and action poses | 0x460280, 0x460210, 0x4624b0, 0x4629b0, 0x464600, 0x463e40, 0x463c90; 0x417bbc.., 0x4177e7.., 0x416fb8.. | `mech3/mech_pose` | `test_pose.py`: 20000 calls on random leg geometries and scripted animations, the original run from inside Move, JumpJets and ActionUpdate; 15 of 17 breaks |
| The player's controls (keys, the stick and mouse axes, the hat's camera commands, the reticule zoom, the throttle's three modes, the eased pitch, twist and free look), the axis readers; the AI's controls; the crouch, the hip's crouch target and duck; the landing and fall rays (dust or splash), the landing pose | 0x427d60, 0x427870, 0x42fe10, 0x42fe70, 0x42fea0, 0x42fff0; 0x4e96f0; 0x41bda0, 0x4621f0, 0x4622a0; 0x4164f3.., 0x416d3a.., 0x41660b.. | `mech3/mech_control`, `mech_move`, `mech_part_update` | `test_player.py`: 20000 updates, every command read in order; 16 of 16 breaks. `test_parts.py` (crouch, duck, target, both rays): 12 of 12 breaks |
| The part updates and setups: the torso (AI aim, rate and absolute modes, free look, centring the legs, servo sound, recoil twist, jolt, shake spring, node), the hip's crouch, the feet's ground update, tilt, counter-spin, touch and plant (force feedback, splash, footprint, step sound, stepping on things), the arms' aim, recoil and kick, the base (under water) update; the hip, leg (IK constants), foot, arm and hand setups | 0x464e80, 0x464ce0, 0x465bc0, 0x465d10, 0x4659c0, 0x465940; 0x4623a0; 0x462a10, 0x462de0, 0x463110, 0x462fa0, 0x463ee0; 0x466510, 0x466840, 0x4666f0, 0x466610; 0x461730; 0x462130, 0x4637b0, 0x462860, 0x4663e0, 0x466e80, 0x45fdd0 | `mech3/mech_part_update` | `test_parts.py`: 20000 calls on random mechs (feet near their ground), every call out logged; 20 of 20 breaks |

The phase cache and the blends: `motion.md`, Playback; the pose:
`movement-pose.md`; the part updates: `movement-ground.md`, The part
updates. Porting them showed two of zmth's sums in a different order from
the exe's (0x522640 rotate_points_transposed, 0x522910
inverse_transform_points_to): fixed, bit for bit now.

## In the program (`mw3 --mission`)

The player's mech runs its real frame: the player tick 0x004a1160 calls the
pilot's vehicle's vtable +0x34, which the program sends to `Mech::update`
(0x00419fc0) for the launched mech, as the spawn's one step (0x004782f0)
does; Core's resume(7) powers it up (mission-start.md section 11). The
controls read zInput's commands (`PlayerControls`, keyboard mode 3), and
the frame goes through the ported gait, Move, turning, the skis' ground
query, collision and the pose. What the program wires for it
(`app_mech.cpp`, `wire_mech_frame`): the node matrices for the pose and
the skis (0x004fc3f0 / 0x004fc300), the skis' position (0x005a85e0) and
soil (the table 0x00608c00, ported: `build_soil_table` 0x0047f400, from
the world object's constructor), collision's vtable +0x18 (the node's
translation and zEntity's refresh 0x005a85c0) and the boxes it probes from
(0x004ecf70, node +0xa4), and 0x005a85c0 after every move.

`mw3tests` `mission_c1m1_player_walks_and_turns_headless`: a fake
DirectInput keyboard holds the throttle-up key for 300 frames, and the Mad
Cat (powered up by then) walks more than 20 m along its heading with both
skis down; the turn-left key then changes its heading.

Still hooks or logged on the frame's path in the program: the HUD
(`heat_calls().screen` and the cockpit sounds), the weapons' fire and
update (+0xd4: 0x004b0c20, 0x004b0c30) and 0x0041aba0, the sounds and
effects of the feet and the landing (`PartCalls`, `MoveCalls`), force
feedback, what a foot steps on (0x005836b0), the other mechs and vehicles
as collision targets (`collide_calls().node_mech` / `node_entity`, so they
count as walls), the AI mechs' own frames (their entities' ticks).

## Level of detail (ported 2026-10-21, `mech3/mech_lod`)

Each frame the sensors call every vehicle's vt+0xc8 while they gather the
records, then vt+0xd0 on each when they finish. A mech's vt+0xc8
(0x0041e810) tells its brain (0x004aedd0, the AI's) and resets the frame's
budget of fully detailed mechs ([0x00605634]): 0 while the player's
throttle (controls vt+0x1c) is 0, else 1, against a limit of 1 (0x005e2034).
Its vt+0xd0 (0x0041e870) sets the detail bits +0x688 from the squared
distance of its contact on the player (+0x98 +0x0c; 800 m with none):

| Distance | Bits | With the budget |
|---|---|---|
| under 75 m | 0x1c | -1 (all) |
| under 250 m | 0x1c | 0x1d |
| under 800 m, or the player's target | 0x10 | |
| beyond | 0 | |

The bits: 0 sounds (engine, walk, steps, power, eject), 1 toes, 2 the turn
pivot, 3 landing effects, 4 the full gait and arm aim, 5 the lean. So an AI
mech is heard only while the player's throttle is at 0, and then only the
first one within 250 m in the sensors' order. The player's own mech keeps
its bits. Then the model: past the type's first range (MechData +0x00,
the definition's +0x14c squared) the low-detail model (+0x698), back inside
the second (+0x04) or when it is the player's target; 0x0041e9f0 switches
only when no weapon is firing (vt+0x0c), stops the jets' flame, has every
part follow (vt+0x5c: 0x00467290 moves the pose to the other model's node,
a destroyed part's copy is hidden; the foot's 0x004672f0 also takes that
model's toe count +0x98 / +0x9c), swaps the models under the node, finds
the jet node ("butt") and restarts the flame with no node (0x00432830(1,
0, ..): a copy of the canned node, not the jet node). The answer is the
controls' (0x004aee40). Checked: `test_deaths.py` (lod 800, part_lod 300;
BREAK=12, 13); in c1m1 the Owens at 1.4 km has no bits and the low model,
at 30 m with the player walking 0x1c and the full model.

## Left

1. The torso's setup 0x464290 is ported (movement-ground.md). The calls the
   parts make outside are wired in the game (2026-10-21,
   `wire_mech_parts` in app_mech.cpp): the foot's wake (0x0045f980..
   0x0045fb80, `mech_wake`), step sounds, footprints, what a foot steps on
   (0x005836b0), the arms' aim (the weapons' +0x0c / +0x18), the cockpit
   camera's look limits, the ground point query; the pilot's sounds
   (0x00432990 through `sound_extra`) for the parts, the movement loops,
   the heat warnings; the heat model's throttle; the collider's touch.
   The pilot's effects 0x00432830 (the jump jets' flames) run the port
   (`canned_effect`); the Thor's jump in mission_c1m1_player_commands_headless
   shows them started and put out. The HUD's zoom flags (+0x1514, the
   torso's slow turn at 0.3 while zoomed) are wired (app_hud.cpp,
   `part_calls().screen_flags`). (The collider's box test,
   `CollideCalls::hit_box` 0x004ec9c0, is wired: movement-collision.md.)
   Left: force feedback, the collision's jolt (`ff_jolt`, 0x004335d0) and
   the landing's (`ff_landing`, 0x00433800) on the device at
   [0x00608620], null in the program until the SDL layer has haptics.
2. Done (2026-10-28 check): the AI brain 0x4e9710 and the vlist triggers
   0x4d9290 are ported (ai.md sections 16 and 17) and run by the program;
   the player's controls 0x00427280, which fill the commands and the axis
   table, are ported (mech_control.cpp; input.md).
