# Mech collision (buildings, mechs, vehicles, mines)

Mech3.exe 1.2, 2026-10-20. Static map from the asm (Ghidra gets the bool of
0x4225c0, the push decay in 0x419fc0 and the arguments of 0x423a20, 0x4237c0
and 0x5836b0 wrong). Part of `movement.md`. Ported (`mech3/mech_collide`, the queries in
`zclass/cls_di`) and checked in `test_collide.py` and `test_di.py`; the
corrections below came out of that.

Corrections: probes 0-2 sit at -z, so ahead of the mech (forward is -z), and
moving forward (local vz < 0) sweeps them, reversing sweeps 3-5. A ceiling
alone does not count as resolved: Collide re-queries only when a vehicle,
mech, bump, touch or wall resolved (+0x374, +0x375, +0x377), and a ceiling
hit with nothing else goes on to the network record. The mech's own node is
cleared of INTERSECT for the query and set again afterwards, whatever it was.
The segment query keeps or drops every segment by the first one's box: its
bounds test reads fixed addresses (0x60b990..), segment 0's (quirk; only the
first matters when the world does not wrap). The bump also moves the probes
by the push times the frame time, and its own sound is slot 13 (+0x5ec).

The code sits between `mech_anim.cpp` and `mech_data.cpp` (likely its own
file; `mech_collide` would fit the alphabetical link order). Everything works
on a collider object embedded in the mech at +0xfc (ctor 0x4222e0 from the mech
ctor 0x414a39, probe setup 0x422360 at 0x416165 once the model exists).

## In the frame

```
Mech::Update 0x419fc0
  if +0x5b0 (1 in the mech ctor; 0 for network-remote mechs, 0x46e2e7):
    blocked = Collide 0x4225c0()
  if !blocked: integrate the yaw (the angular-velocity quaternion step)
  if +0x374 || +0x375 || +0x377: local velocity +0x3c = 0 except y (restored)
  world velocity +0x30 = rotate(+0x3c by the orientation +0x48); if !blocked: += push +0x5a0
  translate the node by +0x30 * dt (0x4fc1b0, object3d.c), moved hook 0x5a85c0
  if ground byte [+0x41c]+5: SetThrottle(0) when +0x30 . push < 0 (xz)
  else: push -= push * 4.5 * dt (0x5e2000), x and z snap to 0 below 0.01
  landmines 0x413560
```

## The collider (mech +0xfc)

| Off | Meaning |
|---|---|
| +0x0fc | 1 = the last query hit nothing |
| +0x100 | hits on vehicles and other entities (node +0x28 bits 0x40000 / 0x80000) |
| +0x104 | hits on another mech |
| +0x108 | ceilings: probe 10 with normal.y < -0.3 |
| +0x10c | walls |
| +0x114 | nodes with a touch callback (node +0xbc slot 1) |
| +0x118 | 0.1: wall restitution, also the push share in mech hits |
| +0x11c | 0.01: yaw kick per unit of speed on a wall |
| +0x120 | probe count, 11 |
| +0x124 | local probe points (vec3[15], 11 used) |
| +0x1d8 | world probe points this frame |
| +0x28c | world probe points last frame (segment starts) |
| +0x374..+0x377 | resolved: vehicle, mech/bump, ceiling, wall/touch/net. The ground object 0x4765b0 reads 374/375/377 |

A hit list entry is malloc(0x48), prepended (the head is the last hit):
normal +0, contact point +0xc, material +0x20, node +0x24, from +0x28,
to +0x34, probe index +0x40, next +0x44.

**Probes (0x422360)**, from the node's box at node +0xa4 (min xyz, max xyz):
`cx = (minx+maxx)*0.5, cy = (miny+maxy)*0.4, cz = (minz+maxz)*0.5`; front and
rear z are `+-(ground +0x2f0) * 1.1` (ground +0x2f0 = 0.5 * 12, 0.5 * 4 for the
Elemental) when [0x5e24d4] (1) and the ground object exist, else 2 min z and
2 max z. Points 0-2 the rear row (min x, cx, max x at cy), 3-5 the front row,
6-9 the feet at (+-foot0 x, 0.75, +-min z), 10 the top (cx, max y, cz).

**Segments (0x422d20)**: corners 0, 2, 3, 5 when |forward speed| < |yaw rate
* 3.3|; when moving, the rear row (reversing) or the front row, and **only for
the player** the two foot probes on that side; probe 10 always, from mid
height to the top. Each segment runs from where the probe was after last
frame's resolution to where it is now: the move is found after it happens,
then pushed back. The query is the engine's `di_segments` 0x4efcd0
(`zclass\cls_di.c`): segment AABBs, the world's partition cells and children,
per node 0x4f0480 (needs node flags 4 and 0x10 and BBOX_NODE 0x100; with
INTERSECT_BBOX 0x20 the box faces are the whole collision), else the model's
polygons (plane crossing, one-sided unless polygon flag 0x100). Up to 32 hits
per segment in walk order, no distance.

**Sorting (0x423010)**, the first rule that matches: another mech's node ->
mech list; an entity -> vehicle list; probe 10 facing down -> ceiling; a node
with a touch callback -> touch list; a near-vertical face (|n.y| < 0.78) on
probes 0-5, or below 0.78 on probes 0-9 while airborne -> walls. Floors and
ramps never block.

## Responses

**Wall (0x4231c0)**: flatten the normal N and the move D to xz; move the mech
(and every probe) out by -2(N.D)N; the new velocity is the move reflected off
the wall, its along-wall part kept at full speed and the into-wall part times
0.1; push = that minus the current velocity. Throttle: the player's drops to
10 %, an AI's flips (controls vt +0x70). Yaw kick: speed * 0.01 away from the
wall. The player gets a force-feedback jolt (0x4335d0 on [0x608620]).
Quirk: the reflect helper 0x51e280 returns -v when N.v is 0.

**Bump (in 0x4225c0)**, when nothing else resolved: the nearest AiTarget
contact that is a mech (or the Mobile HQ) within 9 m pushes the mech away by
(9 - d) * 1.1, adds |forward speed| * 0.6 to the push, sets the yaw rate to
the turn rate * 1.8 away from it, throttle to 10 %, sound 13 for the player.

**Mech (0x423a20)**, the head entry only, unless allied: r = |v_xz|^2 /
max_speed^2 * 0.02, split by tonnage: you take `other / sum` of it on the
torso front (0.5) and both side torsos (0.25 each), or a leg when a foot
probe hit; the other mech takes `yours / sum` on its torso and gets 0.1 x
your share of your velocity added (a network message 0x18 for remote mechs).
Then the wall response.

**Vehicle (0x4237c0)**: a non-allied vehicle or the Mobile HQ takes
|v_xz|^2 / max_speed^2 * 20 damage (vt +0x160); if that kills it nothing
blocks; otherwise its body is shoved by v * tonnage * body +0x9b0 * 0.1,
and the wall response runs. Dead units are ignored by the foot probes.

**In the program (2026-10-29).** Until then a mech walked through every
vehicle: the vehicle hooks were unset, so 0x4237c0 found no target.
- What a hit node's entity answers, for the basic vehicle (vtable
  0x5c88e8) and the field base (0x5c23d8):
  - vt+0xec, what takes the damage: itself (0x4d2a70).
  - vt+0x140: its controls at +0xe4 as IControls (0x4020b0). Their vt+0x5c
    gives the body, which is the controls object itself (0x4a5c40, the
    vehicle's own 0x4aead0, the AI's 0x4e7740).
  - vt+0xf4, its team owner: none for a basic vehicle (0x40bae0), so a
    basic vehicle is damaged allied or not; itself for a field base, whose
    +0xc0 team is checked.
  - vt+0x160, the damage: 0x4ad3f0, or the field base's 0x458f50.
  - +0x08, alive.
- The body: +0x64 its world velocity, +0x70 its local velocity, and +0x9b0
  the motion block's inverse mass (+0x764 + 0x24c), so the shove is
  v x tonnage x inverse mass x 0.1.
- The mech's vt+0xec answers 0, so a mech never takes this path; the turret's
  vtable ends before these slots, so its nodes must never reach it.
- Anything else answers none and reports `0x004237c0 entity` to the census.
  None of the 21 missions reaches it.
- The wall's reverse (0x4231c0): the mech's vt+0x140 (0x426bf0, its controls
  at +0x37c as IControls), then their vt+0x70 "on a path". The AI's controls
  answer yes (0x4e7790 -> 0x4a5270, and the AiPilot's), so an AI mech
  reverses its throttle off a wall. The player's answer no (0x40bae0), so
  the player only slows to 10 %. `MechControls::on_path`.
- Checked in play (`mission_c1m2_a_mech_bumps_a_vehicle_a_mech_and_a_wreck_headless`,
  the player at full throttle from 25 m):
  - Into a Bulldog held in place: stopped 6.4 m off. Its health 1.0 ->
    0.876, and it is shoved. Before the wiring the player went through
    (0.1 m) and nothing was shoved.
  - Into an enemy mech: stopped 8.9 m off, both lose armour. This already
    worked: the mech hooks were wired.
  - Into a Bulldog's wreck: walked over it. Only foot probes 6 and 7 meet a
    Bulldog, and a dead unit's foot hits are skipped. A live one is still
    met by the feet.

**Touch (0x423c90)**: buildings and animated objects with a damage handler
take 20 per frame of contact (0x5836b0 -> node +0xbc slot 1); while they
stand, the wall response blocks.

**Ceiling (0x423600)**: airborne and rising: vertical speed * -0.66, the mech
put just under the contact, 0.002 head damage. On foot: damage by the volume
of the box hit (dx dy dz / 180 / 4): head 0.1, torso 0.4, sides 0.25 each, and
the hip crouches (0x4622a0, sound 12; ported as `Hip::duck`, `test_parts.py`).
The box is 0x4ec9c0's (node +0x74 min, +0x80 max, when node flag 0x100;
`zcls::node_box`), which the program hands the collider as
`CollideCalls::hit_box` (2026-10-21: unwired before, so on foot nothing
happened in play). `mission_c1m1_ceiling_crush_uses_the_box_headless`: a
boxed node of c1's world as the ceiling, the hip's drop exactly v x the
crouch depth; a node without a box does nothing. Breaks: hit_box unwired,
the box read from +0x80, the flag ignored.

After the responses the segments are queried again; the mech stays blocked
while touch, ceiling or wall hits remain, and only then are the probe
positions kept as next frame's starts. A network-synced collision record
(+0xd8) can also fire the wall response.

**Landmines (0x413560)**: `landmines.zrd` (`weapon`, `radius`, default 20 m)
fills a list at 0x604e48; a mech within the radius fires the mine's weapon
at the ground and disarms it. Ported (`landmines.md`).

## Porting order

1. Pure helpers: reflect 0x51e280, normalize 0x4020d0, flat normalize
   0x51e210, TurnRate 0x418f70, probe setup 0x422360, sort 0x423010.
2. The responses 0x4231c0, 0x423a20, 0x4237c0, 0x423600, 0x423c90 on a fake
   mech with recorded part damage.
3. 0x422d20 with `di_segments` stubbed by canned hits; then 0x4225c0 whole.
4. `di_segments` 0x4efcd0 itself (cls_di.c), on the worlds test_gamez loads.

## In the program

`app_mech.cpp` wires the lookups: `node_mech` is 0x40e9d0 (the node's +0x28
id: 0xf000 its owner, 0xff its part's mech), `node_entity` 0x40e960 (bit 18
the owner, else 0x40e9d0, else bit 19 the owner), the contacts are the
sensors'. Another mech is a mech, not a wall (ai.md section 16).

## The shot queries (cls_di.c)

The weapons ask the world what a shot's segment crosses and what lies near
a point, through a third family of queries in cls_di.c, apart from the
ground query (0x004ee2f0, 0x004edab0) and the mechs' segment query
(0x004efcd0). Ported in `zclass/cls_di.cpp` and `zmodel/gmod_hit.cpp`;
zWeapon reaches them through `RunHooks::segment_list`, `segment` and
`HitHooks::query` (wiring: weapons.md).

**The segment list 0x004eed90** (`zcls::segment_list`): ECX the world, EDX
a record (the exe's 0x504 bytes: a count, then 32 slots of 0x28: normal,
point, +0x18, the squared distance, material, node). The segment's box goes
to 0x0060b990, the segment to 0x0060b978. When the world wraps or the box
meets the world's bounds (+0x34 / +0x44 in x, +0x48 / +0x38 in z, whose z
runs downward), the cells are walked along the
segment (0x004eefe0, a grid walk in x and z by the cells' edges); then the
world's own children. Every node needs ACTIVE (4) and INTERSECT (0x10),
none of the exclude flags (0x0060b974, cleared at the end), its zone, and
with the static-only mode (0x0060b96c) a +0xc4 stamp not past the frame
counter. Off the grid of a wrapping world the stored cell is walked with the
segment moved there and the crossings found there moved back; with
first-hit on (0x0060b968) the walk stops at the first crossing, **in a
wrapped cell before moving its point back** (the point stays in the stored
cell's coordinates). Returns 1 when nothing was crossed.

The node walk 0x004ef570 (cameras 0x004efaa0, animated nodes 0x004ef980,
lights 0x004efba0): a node among siblings, or with INTERSECT_BBOX (0x20),
must have its box met by the segment's box (0x004f1470: the box through its
matrices, 0x004ec6b0, or with INTERSECT_BBOX its corners' extent and a face
crossed, 0x0054d2f0); an INTERSECT_BBOX node is then recorded as itself.
Otherwise its model is tested when its +0xc4 stamp is past `since`
(0x0060b970: 0 at start, -1 "every node" once 0x004ed9e0 turns it off), and
its children walked. One slot per node.

**The model test 0x0054cad0** (`zmod::model_segment`) works in the model's
space: the segment through the inverse of the stack's top (0x00523c50; a
singular top finds nothing), the cached polygon first, then the rest; the
crossing's point and normal back through the top, the normal normalised.
Polygons are crossed from the front (either side with flag 0x100), tested
on the plane the normal points along most (0x0054d760: picked by the
magnitudes' bit patterns; on the -z plane a NaN edge fails, on the others
only the first edge's does), within 0.0001. A polygon whose material has
**flag 2** is tested with its texture coordinates (0x0054dd30), which are
always captured (0x005426e0), unlike the mechs' segment query's flag-1
test. A strip of two vertices at the cached index reads an unset result as
a hit in the exe (the port does the same); fewer than two would walk past
its vertex list (none ships; the port skips it).

**The nearest hit 0x004eece0** (`zcls::segment_nearest`): the list, then
with two or more crossings each one's squared distance from the start
(0x0051dfa0) and the nearest one's index in the count byte, the count in
byte 1. With one, no distance is written.

**The sphere query 0x004f0ac0** (`zcls::sphere_query`): the cells under the
sphere's square (0x004f2060 at two corners), each cell's nodes, then the
world's children. A node with flag 0x40 and a box is taken by its box's
bounding sphere through its world matrix (0x004ebd50, 0x004f0f00,
0x004ed280, 0x005227a0, 0x004f9c40); without 0x40 its children are walked
(0x004f0f90). A name prefix filters them (strncmp). With `ranged`, the
distance from the centre past the node's own radius, squared, must be
within radius squared (else 0 is recorded). With `sighted`, a node with
flag 0x400000 must be visible: a first-hit segment list from the centre to
it, with exclude 0x40000 and the node's own INTERSECT off, finds nothing
(first-hit is left off after, not restored). The record: node, its centre
as the point, material 0, the squared distance; the normal is left.

**Precision.** These run while the game is up, at the 24-bit precision
Direct3D 6 sets; the ports are in floats. The inverse 0x00523c50 had been
ported at 53 bits (`zmth::invert`, doubles); `zmth::invert_single` is the
same at 24 bits, which this test needed.

**FIX (A)**: the model test past the record's 32nd slot would write past it
(only the walk's entry checks); the port stops at 32.

**Test.** `tests/differential/test_shotq.py` (in run_all.sh), on the six
worlds with the camera and sun as world children, +0xc4 stamps and sphere
targets (flags 0x40, 0x400000) given to a share of nodes on both sides:
shots along the ground, from above, up, long, short and off the grid, with
the world wrapping or not, first-hit, exclude flags, static-only and
`since` drawn at random; the list or the nearest hit; a sphere at each with
a prefix, range and sight; half way, strips, INTERSECT_BBOX and material
flag 2. Records (32 slots), results, options left, captured coordinates,
cached polygons and node flags identical at 24-bit precision. No world has
an animated node (class 8, 0x004ef980 not reached).

Break tests (`test_shotq.py 60` had to fail): 18 of 25 caught. Caught: the
closing edge's tolerance, the normal's sign in the plane's pick, the
textured test's capture and its u gradient, the cached polygon, the
textured test by flag 1, the boxes' strict comparison, an INTERSECT_BBOX
node not recorded, `since` taken inclusively, the exclude flags ignored or
kept after, the static-only mode in the walk, a wrapped cell's crossings
not moved back, the sphere's distance inside a node, its sight line's
exclude, the whole name for the prefix, first-hit left on after the sight
line, the unmoved box's maximum. Not caught, equivalent or out of reach on
the shipped worlds: a polygon whose plane holds the segment's end exactly,
a NaN edge on the -z plane, the box's face order (its faces are one-sided,
so one at most is crossed), the normal renormalised after the world
transform (the nodes hit carry no scale), the grid walk's diagonal step
(exact ties at cell corners), the last of equally near crossings, and a
division for a multiplication in the inverse (an ulp, not reached).
