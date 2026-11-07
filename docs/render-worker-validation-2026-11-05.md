# Render worker and GPU geometry integration validation

This is local Windows development-preview evidence for issue #20, not a release
or completion of the broader issue #13 qualification. It combines the stock
prototype imported as `2912a000` with the current prepared/HUD/performance stack.
Original media, installed asset payloads, captures and binaries remain local.

## Matched capture-free Training trials

All successful rows below use the same executable SHA-256:
`29a9868b0589c38478f2a5cac93058f9b3fd0aeb904f6cdc11d1fc5716931813`.
The receipt's Git-head field names the prototype base while implementation was
uncommitted; it is not claimed as the executable's full source identity. The
executable hash, explicit arguments and source/change records identify this
development candidate. Subsequent parser rejection fixes and source-file
extractions are checked separately before the PR handoff.

Workload: native live menu -> Training, AMD Radeon RX 9070, Vulkan/DXGI with
matched adapter LUID, 2560x1440, uncapped presentation, seed 2718, 17 ms simulation
step, 2,200 total frames and 1,400 measured intervals after frame 800. The same
frozen pilot, controls/settings, 36 prepared model/LOD bindings, albedo/normal
payloads, Core Critical HUD/artwork, normal lighting, FXAA and AF16 were selected.
Captures, validation and CPU/GPU profiling were disabled for these timings.

| Run | Geometry | Worker | Mean ms | FPS | p95 ms |
| --- | --- | --- | ---: | ---: | ---: |
| `core-baseline-lean-r1` | legacy | off | 4.5836 | 218.17 | 7.2998 |
| `core-gpu-lean-r1` | partial | off | 1.6382 | 610.43 | 2.3359 |
| `core-gpu-lean-r2` | partial | off | 1.5915 | 628.34 | 2.2672 |
| `core-gpu-thread-lean-r1` | partial | on | 1.0996 | 909.42 | 1.4695 |
| `core-gpu-thread-lean-r2` | partial | on | 1.1563 | 864.83 | 1.6148 |

These are host frame intervals, not exclusive GPU pass times or scanout rates.
The worker has bounded queued work, which is fully drained before final reporting
and teardown. A second baseline attempt (`core-baseline-lean-r2`) was aborted
when another checkout began a build, and contributes no timing result. Successful
receipts record no detected overlapping build/game jobs. This small sample shows
a large local improvement; it does not establish both-card or whole-game gates.
Modern geometry changes fog/material policy intentionally, so this is a matched
workload/settings comparison, not a claim of identical legacy shading.

Final-source confirmation at `ecf231c64c6d0d1ead2638a59a4e51ce9f2c74ab` used
executable SHA-256
`36a2ecb6a0c7f9a018ccea99ea274cff67da63da51866259fc5920aee01f1e28`.
The capture-free legacy baseline repeat `core-baseline-final-r3` exited zero:
4.5393 ms mean, 220.30 FPS, 7.3937 ms p95. A separate instrumented modern/worker
run, `core-gpu-thread-profile-final`, also exited zero and reported **zero CPU
prepared vertices, zero vertex-stage time and zero assembly time**, alongside
the same 226,124,748 GPU prepared vertex instances and zero prepared CPU fallback
batches. Its timing is excluded from the lean table because profiling was on.
The worker completed all 10,444 jobs. Pose/selection, remaining stock TL work and
GPU fence waits still exist; they are not implied to have disappeared. The final
source manifest and executable identity are retained separately from the earlier
candidate snapshot.

## Demonstrated offload and ownership

Each completed partial-mode lean run reported:

- 226,124,748 prepared vertex instances submitted to the GPU; zero prepared
  reference CPU vertices and zero prepared CPU fallback batches.
- 38,674 early prepared model submissions. Together with stock submissions,
  2,042,449 stock CPU vertex preparations and 1,640,380 polygon preparations
  were bypassed.
- 6,098 unsupported stock model attempts and 232,392 CPU 3D draw calls remain
  explicit. Morphs, mixed-bone deformation, effects and other families are not
  claimed as ported by these results.
- The threaded runs completed all 10,444 worker jobs and 2,198 resident presents.
  Peak outstanding jobs were eight. The initial texture packet exceeded the
  128 MiB admission budget and was processed exclusively, as designed; the
  recorded peak accounted packet bytes were 339,465,586.

The paired live Training captures `core-gpu-thread-capture-r1` and
`core-gpu-capture-r1` exited zero and produced byte-identical 960x540 BMPs at the
same native frame, with SHA-256
`43786eda16807e1f14f09d01ad914033dc4e6fe51f167b001008eca3cba6ca4e`.
This compares worker on/off in modern geometry mode, not modern versus legacy
visual policy. The capture visibly contains Training and the selected prepared
mech. It is separate from every lean timing row.

## Checks and remaining limits

Release builds passed. Twenty-six selected CTests passed with Vulkan validation,
including portable ownership/FP/failure and early producer-bypass checks, stock
and prepared GPU/reference images, DXGI/WSI, materials, quality, HUD and staging
cache checks. The core suite was deliberately limited to native RNG, FP conversion
and fixed-clock controls. The HUD fixture also passed with explicit local fonts.
The full unrelated core/SDL suites are not claimed as passing. Three invalid
CLI values were checked to exit 2 before launch. All 84 unique selected installed
asset payloads were rehashed against their content identities. An intentional
inline-on-producer fault made the worker test fail; source was restored and the
final Release build and 26 selected checks passed again.

The project pre-PR/review skills were applied. The public native fork has neither
the private `tools/check.py` nor its style checker; actual CMake/CTest,
changed-source formatting and diff checks substitute for unavailable scripts.
An independent source review found the invalid bool-parser success returns,
which were fixed; three passes ended with no remaining major findings. Minor duplicated
MSVC exception flags and packet setup remain possible cleanup work.

Portable queue and early-handoff/resource targets are included in Linux/macOS
core CI. GPU images and native gameplay here were tested only on Windows/RX9070.
The Windows presentation adapter is not yet a Linux/macOS/Android implementation.
The second GPU, full mission/LOD/damage/effect coverage, mixed-bone/morph offload,
GPU light lists/shadows, temporal history and the other runtime adapter remain
open. No claim is made that all CPU rendering work is gone.

Local receipts: `D:/Projects/local/corecritical-mw3-re-offload-20261105/`, including
per-run `run.json`/logs, `timing-summary.json`, `capture-comparison.json`, build
logs, selected-check output and source/change records. No local media or generated
outputs are added to this repository.
