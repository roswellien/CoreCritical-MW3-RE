# Modular content packages

`mw3content` and the native game share package parsing, dependency resolution,
composition, stable IDs, integrity checks and session identities. The launcher
and Studio invoke the executable's JSON interface. `cc_content` has no renderer
dependency; `mech3/content_registry` supplies the native stock adapters and
implemented capabilities.

See [native definitions](native-content-definitions.md),
[legacy adapters](legacy-content-adapters.md), [HUD layouts](modular-hud.md) and
[arena playtests](modular-arena-playtest.md) for consumer-specific contracts.
The authored examples in `examples/modular` contain no original game media.
See [development validation](modular-content-validation.md) for demonstrated
native execution, test coverage and the remaining release gates.

## Package and profile contracts

A schema-3 `pack.json` is the `PackageManifestV3` contract. It contains `id`,
semantic `version`, `game_id: "mw3"`, `files`, `definitions`, optional
`dependencies`, `required_capabilities`, `conflicts`, and `patches`.
Every file entry declares `bytes` and lowercase hexadecimal `sha256`.
Package identity hashes the canonical manifest, including its file table.
Definitions have `kind`, namespaced `id`, `behavior`, and a typed `data` object.

```json
{
  "schema_version": 3,
  "game_id": "mw3",
  "id": "example-equipment",
  "version": "1.0.0",
  "files": {},
  "definitions": [{
    "kind": "equipment",
    "id": "example-equipment:guardian",
    "behavior": "mw3:equipment/ecm@1",
    "data": {
      "name": "example_ecm", "label": "Example ECM", "tech": 3,
      "weight": [2.0, 2.0], "slots": [2, 2]
    }
  }]
}
```

A profile has `schema_version: 1` and a `packages` array. Each root specifies
`id` and optionally exact `version`, `sha256`, or a development-directory
`path`. Dependency constraints support exact `version`, inclusive `min_version`
and exclusive `max_version`. Saved launcher/Studio profiles pin the complete
resolved dependency closure. Multiple hashes of the same version can coexist.

The resolver returns `ResolvedContentSet`: immutable composed definitions,
selected package roots, canonical exact `lock`, `gameplay_digest` and
`session_digest`. The native `SessionContentMap` uses checked per-kind handles.
Stock handles stay fixed; new handles follow sorted stable IDs. Saves and
managed loadouts carry stable IDs rather than relying on those assignments.

Cross-package resources use `package-id::relative/path`. A resource may have one
qualifier and must appear in its owner's integrity table. Nested glTF buffers
and images remain in that glTF package. Their hashes enter the gameplay
fingerprint even when their filenames resemble cosmetic resources.

Imported static glTF/GLB scenes retain named parent/child nodes, including
transform-only nodes, beneath each asset instance or vehicle actor. Mesh vertices
remain node-local; native local matrices and mirrored-transform compensation let
rendering, collision and objective paths use the same hierarchy. These packages
require `world.hierarchy` version 1; see
[imported scene hierarchy](native-content-definitions.md#imported-scene-hierarchy)
for bounds and path rules.

## Composition and storage

Each package can add several content types. A patch names `kind`, `id` and
`changes`; object leaves become field proposals. Disjoint fields combine.
Overlapping scalar/subtree proposals require a declared overriding package or
a profile `conflicts` entry with `kind`, `id`, JSON-pointer `field` and `winner`.
Integer and floating-point proposals remain distinct for native ZRD readers.
Resource fields changed by another package require explicit ownership.

Installations occupy `store/<id>/<version>/<manifest-sha256>`. Extraction uses
an exclusive private stage, bounded archive entries, traversal/link rejection
and payload verification before directory activation. Existing installations
are verified and retained. Delivery remains `.mw3pack`; development folders use
the same validation. Export currently uses bounded ZIP32: packages exceeding
4 GiB or 65,535 entries must be split into dependencies. Import limits are
8 GiB per expanded file and 32 GiB per package, with at most 65,536 files.

JSON input is bounded to 16 MiB, depth 64, one million values and 1 MiB strings;
duplicate keys are rejected. The vendored parser is nlohmann/json 3.12.0 with
its published integrity pin and MIT license. Dependency search, package count,
metadata inventory and resource closure also have explicit limits.
Payload paths use normalized portable ASCII names. Traversal, alternate streams,
links, Windows device basenames and trailing-dot/space aliases are rejected
before extraction. Host package, profile and save roots accept UTF-8. Windows
filesystem boundaries use wide absolute extended-length paths, including UNC
paths, within Windows limits. Manifest paths, exact-lock identities and VFS names
remain logical paths subject to the portable payload-name and confinement rules
above.

Metadata resolves before selected asset decoding. Native visual caches retain
their session ownership; profile changes require quiescent consumers. Resource
reads recheck resolved integrity so editing a development file cannot silently
change a running session. Managed legacy claims verify bytes at the VFS read
boundary. Gameplay changes require complete unload/restart and re-resolution.

## Command-line interface

Every command emits a JSON result and returns zero on success. Failure returns
two with `diagnostics`, including `code`, `message`, `package`, `content`,
`field`, and `other_owner` where applicable.

```text
mw3content capabilities
mw3content list --store STORE
mw3content validate PACKAGE_DIRECTORY --store STORE
mw3content resolve PROFILE.json --store STORE
mw3content inspect PROFILE.json --store STORE
mw3content compile SOURCE_DIRECTORY FRESH_OUTPUT_DIRECTORY --store STORE
mw3content package COMPILED_DIRECTORY OUTPUT.mw3pack --store STORE
mw3content install SOURCE_DIRECTORY_OR_PACK --store STORE
mw3content migrate OLD.ccsave NEW.ccsave PROFILE.json MIGRATION.json --store STORE --preview
```

Compilation preserves the source directory and verifies its complete staged
output. Output directories/exports must be fresh. Studio retains unique builds
and an atomic latest-build receipt. `capabilities` lists the actual compiled
consumers; prepared mesh visuals depend on the renderer build option.

## Persistence and network boundaries

No enabled content preserves the native stock path. Managed profiles have a
separate writable namespace derived from the exact lock. `ModSaveV1` bundles
semantic campaign/inventory/loadout state with its corresponding native world
snapshot and integrity metadata. A first-launch semantic checkpoint explicitly
has no world snapshot. Ordinary Lab/lance saves refresh semantics while keeping
the accepted snapshot; mission results capture a fresh checked native snapshot.
The managed pilot index and complete bundles are staged and replaced atomically.
Campaign records bind their expected run identity and keep different runs in
separate filenames even when their native operation/mission numbers coincide.

Loading validates the exact lock, gameplay and handle-map identity, base native
resources, simulation options and stable IDs before publishing restored state.
Migration binds source/target lock hashes, creates a verified source backup and
writes a new file. `--preview` performs validation and reports semantic changes
without writing output or backup. ID mappings affect typed identities, not
player names or arbitrary campaign variables. Changing world/base/options
requires an explicit `reset_world` at a declared `mission_boundary`.
Retaining opaque native world bytes additionally requires identical gameplay
and handle-map digests; semantic ID mappings cannot rewrite native snapshots.
Resume an explicitly migrated copy with
`mw3 --content-profile TARGET.json --content-save NEW.ccsave`. The native
importer validates and adds the run to that profile's pilot index before launch.
It preserves the input copy and refuses an already installed run.

`ContentSessionV1` adds full gameplay/map/base hashes and simulation options to
the lobby. Managed loadout envelopes carry full stable IDs and the selected
mission; native handles are never truncated into stock packed fields. Typed
HUD/skin/appearance differences may remain cosmetic. Opaque legacy whole-file
overlays are conservatively gameplay content, since scripts can select decoder
roles independently of filenames. Unsupported native source/archive roles fail
visibly in managed sessions.

The demonstration profiles cover selection and native playtest entry points.
Build, compiler, fixture and image-preview results do not establish completed
native gameplay, multiplayer interoperability or a published release. Record
those separately when exercising the complete system.
