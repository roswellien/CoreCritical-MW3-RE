# Native renderer performance controls

The post-HUD investigation keeps two workloads separate: direct `t1m1` with
original assets, and the enhanced live-menu Training path with its pilot,
controls, updated banks/models, FXAA, anisotropy and normal lighting. A backend
ratio is meaningful only when the scene, executable, settings, GPU, assets and
simulation inputs match. Frame timing uses wall-clock intervals; `--frame-ms`
pins simulation time and is not an FPS cap.

## CPU/GPU offload status

GPU drawing is not the same as a GPU geometry producer. The current backend
has GPU HUD sampling/composition, immutable replacement-model buffers and
per-pixel material lighting. Stock native geometry still performs substantial
CPU transform/lighting/clipping work. Replacement models still prepare CPU
vertex streams for native fog, color and clipping contracts before their GPU
draws. This change reduces duplicate preparation; it does not finish that
offload. The separate stock-geometry GPU prototype is not included here.

## Opt-in diagnostics

`MW3_CC_CPU_PROFILE=1` records integer performance-counter durations and prints
them at shutdown. The disabled path does not read a clock. Buckets cover:

- Renderer flush and transport wall time, including their internal waits.
- Prepared model selection, retargeting/deformation, draw emission and TL append.
- Prepared vertex staging and submission assembly, with processed-vertex count.
- Waiting for a previous reusable scene submission.

CPU buckets overlap: selection contains retarget/deformation, emission contains
vertex staging/assembly, and TL append can flush. Do not sum them as disjoint
parts of a frame. Totals include warmup and initialization; they do not directly
describe the separate steady frame-interval window.

`MW3_CC_GPU_PROFILE=1` enables two timestamp queries around each render command
buffer. The selected graphics queue must support timestamps. Results are read
after the existing completion fence (or device idle at shutdown), then the query
pool is reused. Timestamp valid bits and period determine the duration.

The GPU interval includes uploads, barriers, ordered scene draws and inline FXAA
in that command buffer. It excludes separate quality/presentation submissions.
It is approximate GPU command execution time, not CPU wall time or total device
utilization. Timestamp commands can affect scheduling, so disable both profiles
for accepted lean timings. The method follows Khronos's
[timestamp query sample](https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html)
and [query specification](https://docs.vulkan.org/spec/latest/chapters/queries.html#queries-timestamps).

## Measured next bottleneck

The pre-optimization diagnostic at `5a6c9378` used Ryzen 7 9700X/RX 9070,
2560x1440, enhanced live-menu Training, a fixed 17 ms step and seed 2718.
Across the run, including warmup, it recorded:

| Operation | Duration or count |
| --- | ---: |
| Prepared draw emission, CPU | 6869.25 ms |
| Vertex staging within emission | 3044.72 ms |
| Assembly within emission | 1474.71 ms |
| Prepared vertices processed on CPU | 226,124,748 |
| Renderer flush wall time | 1750.05 ms |
| Previous scene waits | 458.18 ms |
| GPU render command buffers, including inline quality | 1022.44 ms / 8244 submissions |

This identifies duplicate CPU model preparation as a significant enhanced cost.
Models can be drawn through GPU geometry while still carrying a CPU-generated
per-vertex stream for original color/fog/clipping contracts. The existing path
recreated that stream even for unchanged inputs. It does not follow that all
remaining costs, or the original-assets D3D12/Vulkan gap, have the same cause.

## Bounded prepared staging reuse

The adapter owns immutable prepared mesh batches. A candidate cache entry is
selected by batch/instance identity and reused only when all mutable inputs
match byte-for-byte: camera/world matrices, clip/projection values, viewport,
target size, lighting selection, native fog state and camera-to-world matrix,
rounding mode, x86 SSE control state and immutable geometry identity. Architectures
whose control state is not handled recompute instead. Pose changes, camera movement,
fog changes, clipping changes and resize invalidate that entry. Deformed
vertices always bypass the immutable cache.

The cached result owns its vertices and instance data. Emission copies the draw
record before assigning current texture bindings or offsets. Shared results
survive eviction; cache destruction precedes the immutable source owner.
The default limits are 512 entries and 128 MiB of retained payload/entry storage,
including clipped smooth-normal streams. Allocator bookkeeping and geometry
already owned by the adapter are separate. Oversized results still render and
are not retained. Logs report hits, misses, bypasses and retained bytes.

`MW3_CC_PREPARED_CACHE=0` forces recomputation for a matched diagnostic control;
`1` or an unset value enables reuse. Invalid values fail visibly. This is a
performance control, not a graphics-quality selection. No simulation work,
native drawing order, polygon detail or material effect is removed.

Known GPU-path output size is reserved before assembling TL data. Cleared scene
vectors retain capacity across submissions, while all scene metadata and
resource references are reset. The cache remains separate from the experimental
stock-geometry port.

## Verification and limits

`mw3prepared_staging_checks` compares cached results with fresh computation and
exercises transform/projection/fog/rounding/geometry changes, clipped normals,
deformation bypass, disabled/oversized cases, budget enforcement, owned eviction
and invalid clip planes. It needs no retail data or live GPU. Existing Vulkan
fixtures cover held/readback frames, scene reset, alpha, quality and material
composition, with optional profiling and validation enabled locally.

Use separate correctness captures and lean ABBA runs. Keep profiles and media
outside Git, record executable/settings/component hashes, and defer runs while
another graphics/build job is active. The issue/PR records the final timing and
capture comparisons. These are development previews; native campaign fidelity,
live Linux/macOS and Android acceptance remain separate work.
