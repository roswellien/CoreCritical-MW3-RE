# Ground following and the part updates

Mech3.exe 1.2, 2026-10-20, from the asm (part of `movement.md`). The mech never
casts a ray for its height: a probe object at mech +0x41c ("skis", source
`C:\zipper\Mech3\mech_skis.cpp`, the "No altitude" assert; code 0x475c80..
0x4779d1) holds seven ground probes and moves the body.

## The skis (0x300 bytes, ctor 0x476320 from mech setup 0x415e09)

Probes j: 0..2 the left ski (local z = -L, 0, +L), 3..5 the right ski, 6 the
centre. Forward is -z, so 0 and 3 are the front probes.

| Off | Meaning |
|---|---|
| +0x00 | the mech |
| +0x05 | slid or pushed this frame (Update reads it: no push decay, throttle stop) |
| +0x08 | the mech's world 3x4 matrix, copied each frame (translation +0x2c) |
| +0x38, +0x3c, +0x40 | ski root and the two ski nodes (object3d, hidden) |
| +0x44 / +0x4c + 4s | sin / cos of each ski's pitch |
| +0x54, +0x5c, +0x64 + 4s | each ski's height parabola a, b, c |
| +0x6c, +0x6d | left / right ski on the ground (ctor 1) |
| +0x70, +0x74 | left / right slope (sine of the pitch), smoothed |
| +0x78 | half track: (foot 1 x - foot 0 x) / 2 |
| +0x7c + 0xc j | world probe positions |
| +0xd0 + 0xc j | local probes in ski space; +0xe4, +0x108 (the mid probes) follow the feet's z |
| +0x124 + 4j | ground height under each probe |
| +0x140 + 4j | probe vertical velocity |
| +0x15c + 4j, +0x178 + 4j | water surface (-300 none), in water |
| +0x194 + 0x28 j | the ground contact (an engine hit, x and z set to the probe's) |
| +0x2ac + 4j | clearance (probe y - ground) |
| +0x2c8 + 4j | probe y after integration |
| +0x2e4 + j | landed hard (vy > 20 at the snap) |
| +0x2ec, +0x2f0 | ski length (12, Elemental 4), L = half of it |
| +0x2f4 | **never written** (read as the mid-probe width; heap garbage) |
| +0x2f8, +0x2fc | 1/L, 1/L^2 |

### The skis' debug quads (0x476090, 0x4776d0, 0x477730)

Each ski's node gets a model of two quads along the ski, a 1 m wide flat
one and an upright one (0x476090), never shown: the nodes are made
inactive. The model's priority 1 is set before the quads are added, so it
reaches no polygon (0x53bb20 writes each polygon's +0x04 and the model has
none yet). Each quad (0x4776d0: refused under 3 points; zone list reset,
0x53c6c0 with flag 1, priority 0) takes a material of its own (0x477730):
`material_init`, then three `rand()`s times 255/32767 until they sum to
150 (r is the second, g the first, b the third), packed into the 16-bit
colour unscaled: `(r << 11) | (g & 0x3f) << 5 | (b & 0x1f)` of the 0..255
values, so the colour is their low bits, not a 565 of the colour. Each
mech build draws at least 12 `rand()`s here (two skis, two quads, three a
try), which moves every later random number; the port draws them the same
(`crt::msvc_rand`). `test_ski_polygon.py` (1,325 runs).

## Update (0x4765b0, from 0x4162c0, returns airborne)

```
simple = !(detail bit 4) || !mech active                 one probe (6) instead of seven
frozen = collider +0x374 || +0x375 || +0x377: ignore the local x/z velocity
skis matrix = mech matrix, translation += (v_local x R) * dt in x and z    (predicted)
mid probes' z = the feet's local z (0 when the foot part is gone)
probes to world through the ski nodes; ground query 0x4ee2f0 (below), top = y + 50
g = gravity [0x5e5d34] (30); air = jet impulse > 0 || (clearance 6 >= 4 && !both feet down)
if !air: g *= 4                                          hugs the ground
step = (ceiling hit && air) ? 0 : 0.7 * height
for j = 6 down to the first probe:
  if landed hard && !air && vy > 0: vy = 0
  vy += jet impulse (per frame)
  h = PickGround 0x477820(hits, y, step): highest hit within step above y, water never
      (the mech walks on the river bed; water sets the surface); none valid: the lowest
  integrate 0x477940: vy -= g dt; y' = y + vy dt; above h: y = y'; else
      vy = (h - y) / max(dt, 0.0333), y = h, landed hard = vy > 20
  c = y - h; !air && c < 3: snap y = h, vy -= c / dt
  !air: slope slide 0x477620: steeper than n.y 0.85 and not noslip: push += 30 dt n.xz
  vy < 0: vy = max(vy, vy 6)
simple: probes 0..5 copy probe 6; the push is tripled when sliding (per frame)
per ski: pick the regular segment; slope = -(dh/dz); s += (slope - s) * 2 dt, snap 0.01,
  clamp to sin 35 deg; offset = clamp(slope z1 + y1 - y6, -0.8, 1)
  blocked step (> 3 m up at the front or back, clearance <= 0): push +-1 along forward
  clamped slope: push along the ski by slope * 0.8 / 0.5736;   foot down = |clearance mid| < 0.2
one ski down and the other not, clearance differs by > 1.5: push 1 sideways toward it
V = mean vy (simple: vy 6), capped at (jet ceiling [0x5e5d44] - y) / 4; probes y += V dt
ski pitch asin(s) and offset to the ski nodes (0x4764a0), mech local vy (+0x40) = V
both down and !air: every vy = 0
zone 0x476550 (contact 6's zone set -> mech +0x64; The zone record below)
airborne = jet impulse > 0 || (|clearance 6| >= 0.2 && neither ski down)
```

The body never tilts (the orientation is yaw only); the legs follow slopes
because the animation's feet are evaluated in the pitched ski frames (anim
vt +0x1c 0x41fbe0) and solved by the leg IK (0x463e40 / 0x463c90, `movement-pose.md`).

## The terrain query (0x4ee2f0, `zclass\cls_di.c`)

Ported (`zclass/cls_di`, the polygon test in `zmodel/gmod_hit`) and checked
on the six worlds in `test_di.py`; the skis use it by default.

Not a ray: per probe a flat x/z point-in-polygon test over the probe's grid
cell (`floor((x - W+0x34) * W+0x74)`, same for z; off-grid probes wrap through
0x4f1f10). Nodes with ACTIVE and ALTITUDE_SURFACE, then the cell's 0x44-byte
entries (empty in the shipped worlds), then the world's children; object3d
matrices are pushed. A polygon counts when n.y > 0 and its height at the
point (0x54c330) is at most `top`. Inside test per edge, backwards:
`(p.x - vj.x)(vi.z - vj.z) + (p.z - vj.z)(vj.x - vi.x) > -0.0001`; flag 0x400
is a strip. Out per probe 0x504 bytes: count, then up to 32 hits of 0x28
(normal, +0x10 height, +0x18 zone set, +0x20 material, +0x24 node), unsorted.
Each model caches its last polygon per probe (header +0x58). Always returns 0.
Water is a separate flat polygon (y 0, soil 1).

## The part updates (ported: `mech3/mech_part_update.cpp`, `test_parts.py`)

Mech::Update runs, when no action plays, the torso's and the hip's update,
then (after Collide) both legs' (which run their foot's) and both arms'
(which run their hand's). The poses (`movement-pose.md`) only store targets;
these updates put them on the nodes.

### Feet: the ground update (0x462a10, Foot::GroundUpdate)

From the hip's set_pose, and from Move for far mechs (no animation):

```
side = node id != 13 (13 the left foot)
W = world position (0x4ed530); ground = the ski parabola at the foot's local z,
    contact = mid probe within +0x2f4 / 2 of it, else front or back (0x475de0)
surface (+0xe0) = 0; in water (+0xac) = the same probe's (0x475ea0)
tilt 0x462de0 (below)
clearance (+0x8c) = n.y (W.y - ground) - sole (+0x74); the last one kept (+0x90)
state +0x94: 0 -> 1 when not rising (last >= now) and below 0.25;
             3 -> 0 above 0.2 (1 -> 2 -> 3 in the foot's update)
in water: depth (+0xdc) = surface - W.y, out of the water when negative
else: the splash effect (+0xe4) stops if playing; depth 0
state 1 (touching): soil (+0xe8) = table 0x608c00 by the contact's material,
    and the node underfoot is handed 1000 (0x5836b0, its damage record +0xbc)
returns the clearance
```

The shadow node (+0x78, a matrix under the foot shown out of the water)
is cleared by the constructor: dead code, ported as written. Two switches,
[0x605cb4] (the world's own ground under the player's feet within 1 m) and
[0x605cb8] (a second height, 0x475d10), are never set.

Tilt 0x462de0 (detail bits 2 and 4 only): the contact's normal kept (+0x7c);
a foot on the ground takes the rotation turning up onto that normal (seen in
the mech's frame, 0x4ed280 mode 2, normalised; 0x525a50), less the hip's turn
(hip +0x84 x that), into +0xc4; in the air the pose's (+0x58). [0x605cbc]
would ease it by half the frame; never set.

### Feet: the update (0x463110, through the leg's 0x463ee0)

```
no node: nothing.  detail bit 4 clear: stop the splash, return
counter-spin (+0xd8): += rate (+0xd4) dt on a turn-in-place frame (+0xc0, cleared),
    else times exp(-3 dt) (the fast exp) outside +-0.01; 0 airborne or without bit 2
node rotation = rotation (+0xc4) x yaw(spin) outside +-0.01, else +0xc4
state 1 -> 2: the player's force feedback 0x4337c0(|forward speed| / max speed)
state 2 -> 3, with footsteps (bit 3), at W less the sole:
    in water: soil 1, the splash moved to the probe's x, z and the water's surface
              and started with the throttle (0x45faf0)
    else, not pushed this frame (skis +0x05): a footprint (pilot +0x230 +0x2c by soil,
              0x508aa0) 5 cm up, laid by 0x462fa0; a playing splash stops
    sounds (bit 0): the soil's step sound (pilot +0x230 by soil) at 0.55 |throttle| + 0.45
other states: a playing splash follows the probe on the water
```

Footprint 0x462fa0: flat ground (n.y exactly 1) faces atan2(-fx, -fz);
vertical faces (n.y 0) none; else the mech's flat forward laid on the plane
(y solved from n), normalised, its cross with n, and the Euler angles
(0x523bc0) of the rows (f x n, n, -f).

### Hip (0x4623a0) and the setups

Hip::Update: the crouch offset (+0x9c, y of +0x98) approaches its target
(+0xa8) at 2/s (`y - (t - y) dt (-2)`), snaps within 0.05, kept in
[-crouch depth, 0]; the node is set. Hip::Setup 0x462130: hip height
(mech +0x508) = the rest y, crouch depth (+0x50c) = def `crouch_height`
(+0x148), 0.7 hip height when that is 0; +0x94 the thighs' x spacing.

Leg::Setup 0x4637b0 (with the calf's 0x45fdd0 and foot's 0x462860, which
makes the splash effect 0x45f980): knee direction from mech +0x585, the
mech's node tree refreshed (0x4f7db0), then on the matrix stack: the thigh's
origin, the foot's rest place (a child of the hip) taken into the thigh's
and the calf's frames (0x522910 twice). Thigh length = |calf offset|_yz
(+0xac), calf length = |ankle in the calf|_yz (+0xb8), the IK constants
`-(a^2 + b^2)` (+0xcc) and `1 / 2ab` (+0xd0), the foot's rest x (+0xb4),
the thigh's (+0xa8), atan2(-(fy - ty), fx - tx) (+0xbc), the ankle in the
calf (+0xc0). (The calf's origin is also taken through the thigh, unused.)

Arm::Setup 0x4663e0: def +0x180.. `recoil_*` (+0x80 pitch, +0x84 push, +0x88
twist), aim rate (+0xb4), pitch and yaw limits (+0xb8..+0xc4), the hand's
share (+0xc8), hand takes all (+0xcc); the rest place (+0x8c); the hand's
setup 0x466e80 keeps its own rest (+0x84), and the arm its length (+0x78);
a hand that fails is deleted. Every setup starts with 0x45fdd0: a part with
a mech and a node registers (0x41b8d0) and inits its armor (vt +0x08).

Torso::Setup 0x464290 (ported 2026-10-21; before, the port's torso kept
its constructor's limits of 100, -12 and 20 degrees and no box):
- The definition's shake: `scale` (+0x14c..+0x154), `freq` (+0x158),
  `damp_freq` (+0x15c).
- Its turn limits: max yaw +0x148 (def +0x174), max pitch +0x144 (+0x178),
  min pitch +0x140 (+0x17c).
- The mech's node refreshed (0x4f7db0).
- The box (0x4ec9c0: node +0x74 min, +0x80 max, when node flag 0x100) of
  the torso node's `torso_polys` child (0x4f9d60), or of the torso node
  itself, gives the half width (+0x7c, x) and half depth (+0x80, z).
- Then the cockpit off the torso (0x464580(0)), even when the base setup
  failed.

The half width is what the part aim's side torsos use (look.md); the
limits bound the torso's twist and pitch.

### Torso (0x464e80, mode +0x84)

- Mode 0, the AI (when an aim is set, +0x9c): the aim direction (+0x8c, in
  the mech's frame when +0x88; 0x465d10) gives pitch asin(y) and yaw
  atan2(x, -z), clamped to the limits (0x465bc0); clamped, the direction
  becomes (sin yaw, sin pitch, -cos yaw). Unless the pitch is locked, pitch
  and yaw approach it by k = exp(-2 dt) (0 below 0.03), the yaw with its sign
  flipped. Into the view quaternion (+0xa4) when +0xa2, else the torso's
  (+0x118); +0x98 is the aim's cosine to the forward.
- Mode 1, the player: the pitch lock (+0xa1) levels pitch at 1.75/s and
  drops the pitch input. Any input (twist, pitch, free look) cancels
  CMD_CENTER_LEGS; while centring and not turning, the controls' turn is set
  to `1.75 yaw / ((1 - 0.8 |throttle|) turn_rate)`, the mech's Turn runs
  (vt +0x170), the turn is zeroed: the legs come round under the torso at
  1.75 x the twist per second. Rate mode (+0xa0 clear, 0x464ce0): yaw and
  pitch += dt x input (x 0.3 zoomed in: the HUD's +0x1514 bit 4 clear), clamped to
  def +0x174 max yaw, +0x178 max pitch, +0x17c min pitch; the free look the
  same with the cockpit's limits (mech vt +0x10c(1), vt +0x1c: +0x68,
  +0x74, +0x70), which stay in the torso's fields. Absolute mode (or
  centring): each approaches `limit x input` at 1.75/s (1.4 centring, 0.3
  centring with the HUD bit clear). The servo sound (mech +0x5c8, 0x432990
  id 4 at 0.6) plays while there is input that is not clamped, and stops
  without input, when clamped, or with +0xa2.
- Every mode: the arms' recoil twist (+0xd0 x (1 - 3 dt), spring +0xd4 by
  (1 - 5 dt)), the jolt's spin (+0xd8 x (1 - 0.9 dt)) and its integral (+0xe4,
  x (1 - 2.5 dt)) as a quaternion (+0xf0); the shake spring (position +0x10c,
  velocity +0x100, def `freq` +0x158, `damp_freq` +0x15c) in 1/15 s substeps
  (already frame-rate independent); the forward (+0x160) and its flat unit
  (+0x16c); the node: pose rotation x (turn x yaw(+0xd4) x jolt x exp(spring))
  (0x4659c0, 0x465940); the base update.

### Arms (0x466510) and the base update (0x461730)

Full detail (bit 4) only. Aim 0x466840 (not a destroyed arm): with a target
(weapons +0x18, the point +0x0c) its direction from the arm, in the torso's
frame (the torso node's world Euler angles 0x4ed5d0, 0x525340), as pitch and
yaw (0x523ac0), approached by exp(-rate dt) (0x523f70), clamped (the right
arm's yaw limits mirrored); without a target back toward 0 by exp(-rate dt
/ 2). The arm takes the yaw and `|yaw| x share` as its pitch angle, the hand
the pitch less that share; with +0xcc the hand takes it all and the arm
keeps its last rotation. Recoil (+0x34, raised by a weapon through vt +0x24,
0x466610, to at most 1; it also twists the torso by `recoil_twist`, the left
arm one way and the right the other): decays toward a hold (+0x7c, itself
x exp(-4 dt)) by exp(-6 dt), 0 below 0.001; its pose 0x4666f0 pitches the
arm up by asin(recoil_pitch x recoil) and slides the hand back by
recoil_push x recoil (the arm itself without a hand). Then the hand's update
and the arm's base update.

Base update 0x461730 (hands, the arms', the torso's): a part is under water
when both feet are in water and its world y is below the left foot's
surface (+0x49); the player's HUD redraws its weapon list when that changes.

## Soils (table 0x608c00 by material +0x20)

1 water (surface, not ground; splashes), 10 noslip (no slope slide), 8 lava
(heat only). Footprint decals and step sounds per soil (pilot +0x230). No
soil changes speed.

## Detail bits (+0x688, 0x41e870 by camera distance; the player all)

< 75 m all (one mech per frame), < 250 m 0x1c / 0x1d, < 800 m or the player's
target 0x10, else 0. Bit 0 sounds, 1 toes, 2 foot spin and tilt, 3 footsteps
and landing effects, 4 seven probes, the full gait and the part animation,
5 the lean.

## Quirks

- +0x2f4 is never initialised; with 0 the feet always use the front or back
  probe. FIX (bug) (Owen, 2026-11-05): the port uses L / 2, the value Pirate's
  Moon's own code writes (its 0x477d1a), so the middle probe counts within
  L / 4 of the foot; `zfix bugs` off keeps the exe's (0).
- The in-water test passes up to 0.7 height above the surface.
- The wading movement sound compares the absolute water y with 0.35 height
  (0x41a88e), so it never plays where the water is at y = 0.
- Distant mechs triple their push every frame while sliding (no dt); most
  pushes (step kick, ledge, slope clamp, jet impulse) are per frame.

## The zone record (0x476550; ported 2026-10-29)

After the skis' frame, the centre probe's polygon gives the mech its zones:
- The record: its count, then up to three ids, at the skis' +0x29c.
- The id it is drawn under: the first of the record's that the player
  view's camera can see. The camera's list is `[0x5f4c8c]` +4 (the camera
  node), data +0x1e4, picked by 0x53cd20 (`zmod::zone_common`); failing
  that, the record's first id.
- zEntity's 0x5a80d0 then copies the record into the mech's +0x64 and puts
  the id on its node's zone byte (0x5a80c0 -> 0x4eb810).
- Port: `Skis::zone_update`, with `SkisCalls::camera_zones` and `set_zone`.
  test_skis.py runs 0x476550 for real, with 0x5a80d0 recorded and a random
  camera list each frame; a break (the fallback's id) is caught.
- Until now only the viewed mech got a record, through the camera's 0x40b080
  (view.md section 1); the others kept count 0, which every zone test
  passes. In play the record changed none of the shots' hits: the same
  impact counts on c1m1-c1m4 with and without it.

