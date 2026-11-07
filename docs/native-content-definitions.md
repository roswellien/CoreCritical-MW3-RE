# Native content definitions

Managed content is activated before native game state is constructed. Stock IDs
remain fixed; additions use namespaced IDs and session-local handles. Package
installation, dependency resolution and profiles use the shared content library.
The native registry supplies the same definitions to the Mech Lab, Instant
Action, runtime construction, inventory, salvage and persistence adapters.

## Supported native variants

- `equipment`, behavior `mw3:equipment/<family>@1`: `name`, `label`, tech mask
  `tech`, and two-element `weight`/`slots` arrays, Clan first. Families are `ams`,
  `armor`, `armorff`, `fcs`, `bap`, `case`, `c3`, `c3slave`, `ecm`, `sink`, `dsink`,
  `jjets`, `masc`, `tag`, `tc`. Identity remains separate from the native family;
  installed instances contribute to the existing family effects. Armor retains
  native half-ton accounting and distributed critical slots.
- `weapon`, behavior `mw3:weapon@1`: `name`, `label`, stable `base` weapon ID,
  tech mask `tech`, booleans `uses_ammo`, `missile`, `lrm`, and optional `reader`.
  Native projectile/beam, heat, ammunition and effect code remains authoritative.
- `chassis`, behavior `mw3:biped@1`: `name`, `label`, stable `base` chassis ID,
  `tech`, `mass` in five-ton steps from 10 through 100, `weight_class` from 0
  through 3, donor `model_name`, optional `reader` and `loadouts`. The native
  stock biped anatomy, movement, firepoint and damage contracts remain in use.
  Existing Elemental behavior remains available for the stock Elemental only.
- `skin`, behavior `mw3:skin@1`: `name`, `label`, stable target `chassis` ID and
  either `entries`, each a texture name or `[name, alternate_name]`, or one
  `texture` name applied across all donor material slots. Explicit slot count must
  match the target's base skin. Additional named texture definitions can provide
  external pixels. Damage-mask replacement is a separate contract.
- `vehicle`, behavior `mw3:ai-vehicle@1` or `mw3:field-base@1`: `name`, `label`,
  stable `base`, donor `model_name`, optional `reader`. Initial stock donors are
  `stock:ttruck`, `stock:apc`, `stock:bulldog`, `stock:striker`, and
  `stock:mobilehq`. Vehicle names cannot contain underscores because the native
  instance naming convention uses the first underscore as its separator.
  Mission actors require an existing native AI controller; unavailable parked
  or player controls fail explicitly.

Native resource names use at most 40 letters, digits or underscores. Stable IDs
are distinct from those resource names: `stock:ac5` identifies the stock weapon
whose native name is `AC5`. Labels provide individual display names without
renaming a global message-table entry. Stock native names cannot be patched.

## Equipment example

```json
{
  "kind": "equipment",
  "id": "example:guardian",
  "behavior": "mw3:equipment/ecm@1",
  "data": {
    "name": "example_guardian",
    "label": "Guardian ECM",
    "tech": 2,
    "weight": [1.0, 1.5],
    "slots": [1, 2]
  }
}
```

This selects the existing MW3 ECM effect. It does not introduce an allied
jamming area, a new range model or a different missile-guidance algorithm.

## Native readers and loadouts

`reader` overlays existing native fields after the compatibility patch layer.
JSON integers remain native integers, fractional numbers remain native floats,
strings remain strings, arrays remain lists and keyed objects patch keyed lists.
An overlay must preserve the base field's types and arity; unknown fields fail
when the selected native source is loaded. For example, a weapon may override
`"reader": {"DAMAGE": [42.0]}` when its native damage field has that form.
Archive, loose-file and missing-reader paths share the composition stage.

A chassis may add `loadouts`, each with `name`, donor `base_index`, optional
`weapons` and optional `equipment`. A weapon entry uses stable `id`, `tech`,
`firepoint`, `group` (0 through 4), optional `ammo` rounds and `ammo_part`.
Equipment entries use stable `id`, `tech`, `part` and `count`. Weapons replace
the donor's mounted weapons/ammunition; equipment replaces its accessories
while retaining donor armor and heat sinks. The native mass, body-part,
critical-slot and sixteen-weapon limits are checked before registration.
Campaign setup reads native composed `weapons.zrd` mass, slots and ammunition
metadata before creating starting loadouts. World weapon effects, sounds and
projectile definitions still initialize at their native mission stage; early
loadout validation does not construct substitute runtime weapon types.

Additional gameplay chassis can retain a donor rig/collision model and select
independent prepared visuals through appearance definitions. Changing a donor
name alone does not import a new rig or validate new animations.

## Starting and inspecting a profile

```text
mw3 --content-profile profile.json --content-store store --inspect-content
mw3 --content-profile profile.json --content-store store --content-mission example:mission
mw3 --content-profile profile.json --content-store store --content-campaign example:campaign
mw3 --content-profile profile.json --content-store store --content-save migrated.ccsave
```

Inspection exits before SDL initialization. Runtime configuration also accepts
`content_profile=` and `content_store=`. The default store is `store` beside the
profile file. Legacy `mod.txt` directories and schema-1 overlay packs can be
selected as managed packages through the [legacy adapters](legacy-content-adapters.md).
Simultaneous unmanaged `--mod` overlays are rejected; select those directories
in the profile so their resources enter its integrity and compatibility lock.
Profile saves/settings live under `profiles/<resolved-lock-hash>` within the
player's writable data directory; original installations and stock profiles
remain separate.

The authored native contract tests exercise catalogue equivalence, independent
equipment identity, weight/slots, actual family aggregation and part loss,
weapon-reader composition, skin registration, vehicle-reader composition,
controller rejection and salvage identity. These tests do not by themselves
establish full native campaign or external-machine compatibility.


## Authored worlds and reusable geometry

A `world` with behavior `authored` creates new native geometry. Its `data` accepts
`bounds: [origin_x, origin_z, width, depth]`, `spawn: [x,y,z]`, `source` for a static
`.gltf`/`.glb` scene, inline `meshes`, a `heightfield`, and reusable `instances`.
Coordinates are meters, Y up; heightfield rows advance toward negative Z. The
native loader constructs zClass world/object nodes and native polygon models.
The same surfaces participate in rendering, ground queries and projectile
intersection; source triangles remain available for crater restoration.

```json
{
  "kind": "world", "id": "example:training_ground", "behavior": "authored",
  "data": {
    "name": "Training Ground", "bounds": [-128.0,128.0,256.0,256.0],
    "spawn": [0.0,0.0,0.0],
    "heightfield": {
      "origin": [-128.0,0.0,128.0], "spacing": 256.0,
      "heights": [[0.0,0.0],[0.0,0.0]], "color": [0.35,0.45,0.24], "soil": 3
    },
    "instances": [
      {"id":"beacon","asset":"example:beacon_mesh","translation":[0.0,0.0,-24.0]}
    ]
  }
}
```

A `world_asset` with behavior `static_mesh` contains `source` or `meshes`.
An inline mesh supplies `vertices`, `triangles`, optional per-vertex `uvs`,
`color` (three values in 0..1), `alpha` (0..255), `soil`, `texture` (an existing
native or package texture alias), or `albedo` (PNG). Meshes and instances accept
`collision`, `ground`, and `visible` booleans independently. An instance also
accepts translation, quaternion rotation and scale. Its bounded `id` names the
native parent node used by objectives. Instances retain distinct placements
while referring to one stable asset definition.

A variant uses `data.base` to name one nonvariant authored world. Scalar and
collection fields replace the base fields; `instances` merge by instance `id`.
An edit can set `remove: true`. Inherited resource ownership is retained across
packages. A resource reference can explicitly use `package-id::relative/path`;
only one qualifier is accepted. glTF buffer/image URIs are resolved relative to
that scene's source package and folder.

The importer accepts glTF 2 static triangle meshes, FLOAT positions/UVs,
unsigned indices, scene transforms and base color materials with PNG albedo.
GLB images may use buffer views. Mirrored transforms preserve surface winding.
Required extensions, animation/skins, sparse or normalized accessors, data URIs
and unsupported image formats fail explicitly. Native material images use
power-of-two RGB/RGBA/gray PNG dimensions up to 4096; a 16-bit grayscale PNG is
accepted as a heightfield source. Heightfields support 2..1024 samples per axis.
The world budget is one million triangles, split into native chunks of at most
256 triangles. Imported normals and PBR material effects are not part of this
native world contract.

`mw3content compile <source-directory> <fresh-output-directory>` compiles source
worlds into integrity-listed `CoreCriticalWorld` schema-1 JSON resources and
changes their definitions to behavior `compiled` with `data.resource`. Selected
world resources are decoded and checked before native construction. Activation
keeps unselected worlds lazy. The legacy archive/interpreter world loader remains
available for stock worlds and managed legacy layers.

## Missions, placement and native readers

A `mission` with behavior `native` names a stable `world`, `number` (1..999),
`name`, optional `description`, and native `readers`. Standalone missions appear
in the native campaign game list and Instant Action scenario list unless
`selectable: false`. Graph campaigns appear alongside stock campaigns. Selecting
an authored Instant Action entry runs its objective-based mission with the
selected commander; its own actor placements determine the opposition.

`nav_points` contain unique `id`, `position`, `links` and optional `label`.
They compile to the existing version-107 native waypoint network. `actors`
contain `id`, stable `vehicle`, `position`, optional `yaw`/`team`, and `net`.
Each actor requires an explicitly declared version-106 `net_XX.zrd` from 1..99.
These use the existing native AI controller and path compiler. A zero/parked AI
controller is unavailable and rejected. Route authoring is explicit; this is not
an automatic navigation-mesh generator.

A reader value is the *inner* native key/value list. The adapter supplies the
required outer list. Integer and float types remain distinct: use decimal
positions/yaw for AI records because those readers consume native float words.
Raw booleans and null are rejected. Supported nonempty schemas are `world.zrd`,
`objectives.zrd`, `valves.zrd`, basic `aiv.zrd`, version-106 route/version-107
waypoint documents, and `mpinfo.zrd`. Other optional mission facilities may be
empty; an unknown required nonempty schema fails. Native objective construction
and declared exported paths are checked again during mission setup.

For example, the mission's reader map can contain:

```json
{
  "objectives.zrd": ["m1",[
    "primary",["reach_area",[
      "point",[0.0,0.0,0.0],"radius",12.0,"desc","Secure the landing zone"
    ]],
    "secondary",["node_change",[
      "node",["beacon"],"state","active","desc","Recover beacon telemetry"
    ]]
  ]],
  "valves.zrd": ["signal",["value",[0]]]
}
```

`objective_ids` maps authored names to native paths such as
`{"landing":["primary",0],"intel":["secondary",0]}`. `export_valves` lists
actual named native valves. Those exports feed campaign rules after the real
mission result. A multiplayer mission sets `multiplayer: true` and supplies
sixteen explicit `spawn_points`. Its stable mission selection is carried by the
managed session protocol rather than packed into the legacy eight-bit folder
and scenario fields.

## Branching campaigns, items and checkpoints

A `campaign` with behavior `graph` supplies `name`, `start`, scalar `variables`
and `nodes`. Each node has unique `id`, stable `mission`, optional `on_success`,
`on_failure`, `objective_rewards` keyed by exported objective identity, and an
ordered `routes` array. A route has `when` and `next` (another node or null).
Conditions support booleans, `all`/`any`/`not`, `outcome`, and an exported
`objective`, `valve`, declared `variable`, `item`, or `stat` (`score`/`time`) with
`equals`, `gte` or `lte`. Unknown references/operators fail validation.

Actions support `set`/`add` for scalar variables and `give`/`take` with a stable
`id`, `kind` and positive `count`. Native reward kinds are `weapon`, `ammo`,
`equipment` and `chassis`. `mission_item` behavior `token-v1` supplies `name` and
optional `label`; its counts are durable graph inventory. Existing native
objectives/valves provide live interaction triggers; a token does not add an
unimplemented physical pickup class. Native inventory actions retain actual
item identity, tech and counts and reject unavailable consumption/overflow.

`starting_inventory` uses the same actions. `starting_chassis`, optional
`starting_loadout` and `starting_skin` select actual registered native loadouts.
Rewards are applied once per accepted attempt. A successful node with no
matching route ends the campaign; failure with no matching route retries it.
Mission result capture occurs before cleanup, including named native objective
completion, exported valves, score and elapsed mission time.

Managed checkpoints contain graph progress, all campaign stores, player/lance
loadouts and skins, score records, exact package identities, native base-resource
digests, simulation options and the real native
world snapshot. The native writer targets a fresh private staging file; a failed
writer or incomplete archive cannot promote an older world file with new
inventory. Only the verified pair replaces the previous atomic bundle. Normal
pilot writes do not recapture historical games from whichever world is current.

By default a new mission starts a fresh world. `persist_world: true` permits a
compatible snapshot of that same mission. Cross-mission carryover additionally
requires both missions to declare the same explicit namespaced
`persistence_group`; the loaded world digest must match exactly. Other world
boundaries retain durable graph/inventory state and construct the new world.
Both semantic restoration and the fresh bundle read immediately before the native
world callback verify base resources and simulation options. Package/profile
changes require the explicit migration workflow; original saves
and previous bundles remain available on preflight failure.

`--content-save FILE` explicitly imports a migrated ModSaveV1 bundle into the
selected pilot's campaign index and resumes that run. It requires the exact
active package lock, native base identity and simulation options. All semantic
items, configurations, skins and progression are validated before obtaining or
creating a pilot. Retained world bytes must match their declared source mission;
a checkpoint captured just before advancing can therefore name a different
snapshot mission from its next playable mission. Native world restoration is
deferred until that world's normal construction and persistence checks.

Import preserves the source and saved run ID, creates a new canonical bundle
and atomically writes the native pilot index. Existing indexed runs and existing
canonical files are refused. An index write failure restores the previous
selection and removes only the import's newly created bundle. Imported runs are
then available through the ordinary native pilot/campaign menus. Completed runs
and multiplayer missions cannot be resumed through this campaign import path.

The project-authored showcase is `examples/modular/showcase-profile.json`: a
base heightfield, reusable props, a moved/recolored variant, native vehicle AI,
standalone missions, a multiplayer arena, and a two-node campaign that starts
with the demonstration chassis/loadout/skin. Compiler, native geometry queries,
reader parsing and reducer tests cover these contracts; a release still needs
its recorded native runtime and multiplayer playtest results.

Reusable `world_asset` geometry may declare `data.markers`, an array of exact
`{ "name": "support00", "position": [x,y,z] }` objects. Positions receive the
instance transform and become real native child nodes under that instance.
Packages using markers require `world.markers` version 1. The authored truck
asset declares four support and twelve collision points matching its own hull;
required semantic vehicle actors reject missing body points before construction.
An AI vehicle definition can bind `data.asset` to a reusable static `world_asset`
and require `vehicle-assets` version 1. The native factory builds that asset's
models and markers at local origin when the actor node does not already exist;
the native AIV controller then positions it once. This works independently of
map-specific donor templates. The asset requires all four support and twelve
collision markers. Live model pools never resize: insufficient capacity fails
before any actor node is constructed. Field-base arm rigs continue using their
existing donor world models and do not advertise static asset replacement.


## Imported scene hierarchy

Packages using imported scene nodes require `world.hierarchy` version 1.
CompiledWorldV1 retains a `nodes` forest. Each entry contains exactly `id`,
`instance`, `parent` (null for a scene root), `name` and `local_matrix`.
IDs are deterministic `<instance>/gltf/<source-node-index>` bindings; names
remain the authored glTF names, with `node_<index>` for unnamed nodes. Names
must fit 35 native bytes without control characters; ambiguous sibling names
are rejected. Names may repeat beneath separate parents.

Matrices retain glTF column-major affine layout. An imported scene root includes
its asset instance placement once; descendants retain their source-local matrix.
Imported mesh chunks name their owning `node` and keep node-local vertices.
Native Object3d nodes carry those matrices and actual parent/child links, so
rendering, ground/projectile queries and objective node paths use the same tree.
The whole transform chain determines mirrored triangle winding. A mirrored bound
chunk requires `reflection: true`: its local vertices include an X reflection,
cancelled by the anonymous native mesh child's X scale. Authored node matrices
and world positions stay intact while native ground and nearest-ray queries
agree on the front face; the reverse ray remains culled. Unbound chunks cannot
declare reflection compensation. Matrix and TRS fields are mutually exclusive.
Cycles, shared-parent source nodes, missing or
cross-instance bindings, singular/nonfinite transforms, out-of-bounds geometry,
unknown compiled fields, more than 65,536 nodes or depth above 24 fail before
native construction. Transform-only nodes count toward both budgets.

Native objective paths begin with the unique asset instance or vehicle actor
root and then traverse immediate imported child names, for example
`["gate_01", "Building", "Door", "Latch"]`. An imported basename used as the
first component fails with a path diagnostic. Managed path tags belong to live
nodes and are copied with native tree copies; recycled node slots reset them.
The explicit world prefix is also supported, for example
`["world1", "gate_01", "Building", "Door", "Latch"]`; it cannot skip an instance
or intermediate parent. Vehicle support/collision point lookups use the explicit
actor-root markers, even when an imported descendant has the same name.
Inline meshes, heightfields, old compiled files without `nodes`, existing
per-instance markers, and stock native lookup retain their existing contracts.
