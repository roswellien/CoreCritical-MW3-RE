# Experimental GPU geometry

This preview moves eligible stock and immutable prepared model rendering to GPU
geometry before the original CPU vertex preparation. It is an initial slice of
issue #13, not complete geometry offload or a qualified performance improvement.
The default remains `legacy`; `--original` retains its existing meaning.

Use a Core Critical Vulkan backend and an explicit window size:

```text
mw3 --cc-vulkan --window 2560x1440 --cc-geometry partial
```

The geometry modes are:

| Mode | Behavior |
| --- | --- |
| `legacy` | Existing rendering path and default selection. |
| `partial` | Submit supported stock and rigid prepared models early; report remaining CPU model routes and non-sharp draw attempts. |
| `gpu` | Reject unsupported models and CPU 3D draw calls visibly. Expected to fail in ordinary missions until remaining families migrate. |
| `cpu-reference` | Use the same stock/prepared geometry and modern materials with CPU transforms, clipping and projection for development comparisons. |

`--cc-geometry-debug normals` displays normals for accepted geometry and requires
an explicit geometry mode. It is a correctness view, not a performance profile.

Current admission covers opaque, ordinary type-0 meshes with valid supported
materials. Position/normal transforms, projection, clipping and fog execute on
the GPU for accepted models. Unsupported alpha/cutout, material priority,
scrolling, cycles, overlays, model lights, morphs and mixed-bone prepared deformation
retain explicit partial-mode CPU routes. Billboards, points, particles and weather
are not migrated. Strict mode exempts explicitly marked 2D compositor draws;
depth-always state alone does not identify a permitted HUD draw.

Rigid prepared batches keep their selected albedo/normal maps and native pose
decisions. Their immutable VBO and small per-draw matrices replace the former
per-frame CPU transformed-and-lit stream, including near/far crossings. Accepted
early replacements bypass stock registration and rendering vertex/polygon loops.
Partial mode can also GPU-submit an immutable replacement at the later model
callback when the stock producer has required side effects. Such a late draw
does not count as an early producer bypass. CPU deformation remains explicit.

Geometry modes select modern fog/material behavior: horizontal Euclidean distance
and world altitude, with per-pixel lighting when selected. The legacy mode keeps
the existing native fog approximation and prepared diffuse path. GPU/reference
comparisons establish the modern path's consistency, not pixel identity with
legacy materials or complete original lighting/shadow coverage.

`--cc-render-thread 1` independently enables a separate CPU render worker. See
[render-worker ownership and validation](render-worker.md). It defaults to `0`.

The shared `renderer/geometry` module contains owned resources, portable reference
algorithms and its vertex shader. Registration consumes borrowed arrays once per
revision, normalizes GameZ corner order and retains immutable resource identities.
Runtime adapters live outside it. Model mutations invalidate registration;
submitted resources remain owned until their GPU work completes. No per-draw
source-vertex scan or GPU vertex readback establishes resource lifetime.

With `MW3_CORE_CRITICAL_VULKAN=ON`, Release validation includes
`mw3geometry_checks`, `mw3ccvulkan_geometry` and `mw3ccvulkan_geometry_wsi` in CTest.
These cover early stock/prepared bypass, revisions, FP state, strict rejection,
indexed GPU draws, prepared GPU/reference images, clipping, fog, normal maps,
normal orientation and resource retirement. Portable worker ownership/failure
checks and threaded DXGI/WSI/material/geometry fixtures run separately.
Live scenario coverage, GPU light lists/shadows, deformation, temporal history,
the current-runtime adapter and both-card performance qualification remain open.

The optional `MW3_BENCHMARK_TIMESTAMPS` environment variable names a CSV output
path and requires `--frame-times`. It buffers native post-present counters and
writes once after the loop. These are host wall-clock intervals, not GPU pass
timestamps or scanout measurements. Keep correctness captures separate from
capture-free matched performance runs.
