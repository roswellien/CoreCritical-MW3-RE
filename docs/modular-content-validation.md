# Modular content development validation

This is source-branch development evidence from 2026-11-07, not a published
release or acceptance of every release criterion. Original game data, binaries,
native captures and debugger logs remain local. The launcher/Studio companion
is [MW3Recomp-Private PR 264](https://github.com/roswellien/MW3Recomp-Private/pull/264).

## Automated checks

- The Windows clang-cl build produces `mw3`, `mw3content` and the content/native
  tests with SDL and the Core Critical Vulkan backend.
- 70 content tests pass. They exercise composition and dependency failures,
  stable handles, real native readers and geometry queries, item/store/lance
  consumers, campaign reduction, transactional persistence/import, migration,
  multiplayer envelopes, HUD presentation and lazy resource integrity.
- 125 focused content and existing stock regression tests pass, including
  pilot/configuration, Lab, lobby, resolution, weapons, mods and patch behavior.
- Six selected CTest consumers pass: renderer worker, prepared resources,
  geometry, GPU HUD, SDL and prepared staging. This is not the entire Vulkan
  fixture matrix or the complete original-data differential suite.
- A deliberate removal of the Windows trailing-dot/space path guard makes its
  regression fail. Restoring the exact source makes it pass. A package containing
  `data.txt` and `data.txt.` was accepted before the fix and is now rejected
  before activation, preventing two claimed files aliasing one Windows file.
- CI includes asset-free content checks and the shared CLI on Linux, macOS and
  Windows. Local Windows results do not establish cross-platform CI success.

## Demonstrated native execution

The project-authored showcase loads its new heightfield, bridge and reusable
objects through the native scene/collision adapter, constructs an added chassis,
weapon/equipment loadout, skin and asset-backed AI truck, and evaluates native
reach-area/node-change objectives. Added weapon firing/heat and replicated peer
movement were observed. Geometry, ground/projectile and restoration tests cover
paths that a capture alone cannot establish.

The authored campaign completes landing, grants one telemetry token, cold-loads
the extraction branch, consumes the token and ends with two accepted results.
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
release gate. The direct arena route does not establish production lobby UI,
scoreboard or respawn acceptance.

The no-profile stock chain traverses all 20 missions, reaches credits and exits
zero, with zero unknown original functions and zero skipped script-engine calls.
All 15 fitted-weapon checks pass. Seven salvage comparison diagnostics report
differences; a baseline comparison is still required before calling every
salvage lifecycle check equivalent.

Native captures cover 800x600, 1280x720 and 1680x720 and an authored HUD scale of
1.25. Inspection caught and corrected overlap between the example radar and
weapon list. Dynamic resize/input acceptance and the complete Studio/launcher
interaction remain to be exercised; launcher UI follow-up is
[issue 262](https://github.com/roswellien/MW3Recomp-Private/issues/262).

## Release boundary

Keep the implementation PRs in draft until remaining native stability, UI,
baseline salvage and full demonstration checks are accepted. The launcher full
suite also encounters its existing Steam-must-be-closed guard on the developer
machine; focused native-CLI integration and its other checks pass. Do not count
that environment-blocked aggregate test as passing. No original-game fallback,
general scripting, native plugins, arbitrary anatomy, player vehicle controls,
custom widget types or live gameplay editing are advertised.
Runtime acceptance is tracked in [issue 35](https://github.com/roswellien/CoreCritical-MW3-RE/issues/35).
