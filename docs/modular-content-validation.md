# Modular content development validation

This is source-branch development evidence from 2026-11-07, not a published
release or acceptance of every release criterion. Original game data, binaries,
native captures and debugger logs remain local. The launcher/Studio companion
is [MW3Recomp-Private PR 264](https://github.com/roswellien/MW3Recomp-Private/pull/264).

## Automated checks

- The Windows clang-cl build produces `mw3`, `mw3content` and the content/native
  tests with SDL and the Core Critical Vulkan backend.
- 78 content tests pass. They exercise composition and dependency failures,
  stable handles, real native readers and geometry queries, item/store/lance
  consumers, campaign reduction, transactional persistence/import, migration,
  multiplayer envelopes, HUD presentation and lazy resource integrity.
- 133 focused content and existing stock regression tests pass, including
  pilot/configuration, Lab, lobby, resolution, weapons, mods and patch behavior.
- All 31 presentation/CLI CTests pass: the Vulkan fixture matrix, renderer
  worker, prepared resources/staging, geometry, GPU HUD, SDL and the actual
  Unicode-path CLI. The separately run native test executable is excluded from
  that count. This does not cover the complete original-data differential suite.
- Windows long-Unicode-root fixtures exercise package export/install, complete
  save-bundle reads and failed replacement preserving the accepted save,
  managed campaign refresh/cold load, VFS and selected loose-reader reads,
  strings and settings. A correctly hashed malformed selected reader fails
  visibly instead of loading an archive fallback. The actual native executable
  also completes a packaged campaign mission from a Unicode profile/store root.
- Retained-hierarchy fixtures check transform-only nodes, deterministic bindings,
  invalid graphs/matrices, nested native objective and ground/ray queries,
  mirrored surfaces, restoration, vehicle marker binding and hierarchy-tag
  lifetime. These are native adapter checks, separate from live game runs.
- A deliberate removal of the Windows trailing-dot/space path guard makes its
  regression fail. Restoring the exact source makes it pass. A package containing
  `data.txt` and `data.txt.` was accepted before the fix and is now rejected
  before activation, preventing two claimed files aliasing one Windows file.
- Asset-free CI passes content checks and the shared CLI on Linux, macOS and
  Windows for runtime source revision `9950cbcf`.

## Demonstrated native execution

The project-authored showcase loads its new heightfield, bridge and reusable
objects through the native scene/collision adapter, constructs an added chassis,
weapon/equipment loadout, skin and asset-backed AI truck, and evaluates native
reach-area/node-change objectives. Added weapon firing/heat and replicated peer
movement were observed. Geometry, ground/projectile and restoration tests cover
paths that a capture alone cannot establish.

Fourteen actual CLI calls install the nine showcase source packages, compile
both worlds, export and install the compiled `.mw3pack`, and resolve/inspect its
selected profile. Native runs using that installed compiled world complete
landing, including the imported `gantry/Gantry/Control/Beacon` objective, grant
one telemetry token, cold-load the extraction branch, consume the token and end
with two accepted results.
An active checkpoint imports into a separate profile write directory using
`--content-save`; its source remains unchanged. Native snapshot payloads preserve
the existing game's save facilities, principally animation state. A map with no
saved animations can legitimately have a header-only native archive; this does
not establish arbitrary world/object serialization.

Two native ENet peers with the demonstration chassis, weapon and equipment pass
matching-content preflight, enter play, move/fire and exit with zero unknown
original calls. A damage-definition mismatch is rejected before Core. Different
owned skin pixels retain the same gameplay identity and were admitted. The
cosmetic rerun exits cleanly under the debugger after fixing a static texture
ownership teardown defect. An earlier intermittent access violation was not
reproduced or conclusively attributed; extended multiplayer stability remains a
release gate. The final compiled hierarchy showcase also passes matching and
cosmetic native peer runs; both changed-damage peers exit 2 before Core. These
peers use complete version/hash locks and the installed package store. The
direct arena route does not establish production lobby UI,
scoreboard or respawn acceptance.

The no-profile stock chain traverses all 20 missions, reaches credits and exits
zero, with zero unknown original functions and zero skipped script-engine calls.
All 15 fitted-weapon checks pass. A same-seed comparison with a verified baseline
binary built at `98ed1168`, whose tracked source tree is identical to integration
base `17a527b8`, matches all 20 progression/score lines, 15 fitted-weapon checks,
20 salvage diagnostics and 19 transfer diagnostics. This is a verified identical
source-tree baseline, not an exact-commit build. The seven salvage `differs`
diagnostics also occur in that baseline; this run introduces no new difference
in those diagnostics. It does not establish equivalence to original-game output.

Native captures cover 800x600, 1280x720 and 1680x720 and an authored HUD scale of
1.25. Inspection caught and corrected overlap between the example radar and
weapon list. A separate live native run maximizes and restores its 1280x720
window, keeps the selected HUD aligned, resumes through the displayed pause
button at both sizes and toggles objectives through F12, then exits zero.
This tests presentation resize and native input alignment; it does not change
the logical mission resolution. The complete Studio/launcher interaction remains
to be exercised; launcher UI follow-up is
[issue 262](https://github.com/roswellien/MW3Recomp-Private/issues/262).

## Release boundary

Keep the implementation PRs in draft until remaining native stability, UI and
full demonstration checks are accepted. The launcher full
suite also encounters its existing Steam-must-be-closed guard on the developer
machine; focused native-CLI integration and its other checks pass. Do not count
that environment-blocked aggregate test as passing. No original-game fallback,
general scripting, native plugins, arbitrary anatomy, player vehicle controls,
custom widget types or live gameplay editing are advertised.
Runtime acceptance is tracked in [issue 35](https://github.com/roswellien/CoreCritical-MW3-RE/issues/35).
