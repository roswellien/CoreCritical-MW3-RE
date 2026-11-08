# Managed legacy content

The package loader adapts supported older data packages into the same schema-3
definitions used by new content. Source folders and original installations are
read only. Installing or packaging an old source writes its normalized manifest
into the managed output; it does not rewrite the source manifest.

Supported inputs:

- A directory with `mod.txt`: `id`, `name`, `version`, `author`, `description`,
  `for=mw3`, `world` and `ia_world` directives. `name` is required. Without `id`,
  import derives `legacy-<folder>`; authors should supply a stable lowercase ID.
  Numeric versions become semantic versions; other version labels are preserved
  as provenance and mapped to a deterministic legacy prerelease version.
- Schema-1 `pack.json` whole-file data overlays, including dependencies and
  conflicts. A missing file table is generated on import. Existing tables are
  verified rather than regenerated to accept changed files.
- Schema-2 `prepared-native-v1`, `pack-root` prepared model registries for
  runtime-ready stock rigs. Close and distance mesh, texture, normal and optional
  surface-material bindings become typed `appearance` definitions. The adapter
  supports BWMESH1 through BWMESH6, BWTEX1, BWNRM2 and BWSFM1, subject to the
  selected runtime having the prepared visual backend. Known old environment
  names are translated as data; no environment commands or pack code execute.

Alternate prepared skin bindings, runtime candidates, additional-chassis old
registries, prepared UI/environment/music/cinematic profiles and unknown native
bindings fail with a named diagnostic. They require explicit migration to a
supported typed definition. `reference_host_sha256` is not an admission rule;
old launcher `min_engine_version` is retained as provenance. Actual format and
capability versions govern runtime admission.

All payloads receive size and SHA-256 checks. Imports reject traversal, links,
case-duplicate paths, executable/script payloads and protected player files,
including pilots, saves, configs, keys, settings, logs and profile directories.
Metadata such as `mod.txt` and documentation is pinned but not mounted as game
data. Unknown data formats conservatively affect gameplay compatibility.

Select an old source through a normal profile package entry:

```json
{
  "schema_version": 1,
  "packages": [
    {"id": "example-overlay", "version": "1.0.0", "path": "./old-pack"}
  ],
  "resource_overrides": [
    {"path": "zbd/reader.zbd", "winner": "example-overlay", "allow_composition": true}
  ]
}
```

`resource_overrides` is necessary for differing overlapping files. Byte-identical
copies coalesce deterministically. Paths use lowercase forward slashes and the
winner is a selected package ID that provides that file. Reader archives and
loose weapon, vehicle or chassis readers also require `allow_composition: true`
when typed definitions compose those readers. This chooses the overlay as the
source tree, then applies the validated typed definitions. Invalid or stale
choices fail resolution.

Managed mounts expose the chosen files ahead of patch/install data, while
retaining player-file protection. Different packages can win different files;
losing or unlisted files cannot leak through a whole-folder mount. Locks record
owners, package-relative sources, hashes and gameplay classification. They
contain no source-machine absolute paths and survive installation into a store.
All opaque legacy whole-file claims enter multiplayer and save compatibility
identity, including texture, audio and translation files. A suffix cannot prove
which role an existing native script assigns to a file. Typed skin, HUD and
prepared appearance definitions retain their explicit cosmetic contracts.

Declared `world` additions register real native world folders backed by their
own `zbd/<directory>/gamez.zbd`. They retain existing native file and mission
contracts; this adapter does not convert legacy world data into authored scene
geometry. `ia_world` entries use registered native worlds. Prepared models
select their typed appearances by default; an explicit profile
`selections.appearances` list, including an empty list, replaces that default.

Use `--no-mods` when an old `mw3.cfg` still enables unmanaged mods, then select
those directories through the managed profile. Unmanaged `--mod` and a managed
profile cannot be combined because their resource ownership would be incomplete.
Untracked `--assets` directories are rejected for the same reason.

At runtime, managed save/network identity also binds the effective original and
patch gameplay inputs: common reader/interpreter/motion/mechlib banks, native
world gamez/reader/animation banks and loose native reader/world/script sources.
Fallback copies are included in search order. Every `.zbd` bank is included,
even a texture- or audio-named bank, because native scripts choose bank roles.
Movies, logs, saves and unrelated directories are excluded. These inputs are streamed
once per mounted session and cached until registry or mount changes. This native
base digest does not require original game data for package authoring or CLI
resolution.

Actual managed legacy reads recheck the pinned size/hash; changing a development
payload after resolution fails before decoding. Native mission/common basename
lookups use the same selected per-file ownership as full logical paths.
Managed loose interpreter sources must use `.gs`, `.gw` or `.zan` and reside in
fingerprinted native directories or carry an explicit gameplay package claim.
Cosmetic files cannot be executed through `source` under another suffix.
Selected managed script claims override archived scripts regardless of copy
timestamps. Original loose script files retain native timestamp precedence;
their timestamps enter the base identity because that choice affects execution.
Managed native world reads additionally require a relative `.zbd` path in the
fingerprinted directories or an explicit gameplay package claim; renamed image
or raw arbitrary paths cannot become untracked world geometry.
