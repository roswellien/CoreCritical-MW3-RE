# Experimental render worker and geometry offload

Issue #20 integrates the early stock geometry prototype with the prepared-model,
HUD and performance preview stack. The new execution boundary is native gameplay
and pose selection on the main thread -> owned scene packets -> ordered Vulkan
submission on a separate CPU thread -> GPU geometry/material processing.

This is an opt-in development preview. Nothing in this change merges or publishes
the existing draft renderer stack, changes installed assets, or completes the
broader issue #13 offload/performance qualification.

```text
mw3 --cc-vulkan --window 2560x1440 --cc-geometry partial --cc-render-thread 1
```

The equivalent native WSI selection is `--cc-vulkan-wsi`. Geometry and threading
are independent switches; their defaults remain `legacy` and `0`. The SDL GPU
backend rejects Core Critical options. Invalid thread values fail before launch.
Prepared selections still require the existing explicit model/texture/normal
bindings. Geometry modes and their intentional modern fog/material behavior are
described in [GPU geometry](gpu-geometry.md).

## Ownership and ordering

`RenderWorker` is an ordered bounded queue. Outstanding jobs include the active
job, with an eight-job and 128 MiB accounted-packet budget. A single larger packet
is admitted exclusively so a large initial texture upload cannot deadlock the
producer. This is an admission budget, not a cap on total process memory or GPU
residency. Packets own their TL data, textures, immutable geometry references,
matrices, fog and lighting inputs. Live shadow callbacks are rejected. Gameplay
nodes and borrowed native matrix/vertex arrays are consumed only on the producer.
Completed packet references are released, and empty scene vector capacities are
reused. Producer-side upload identities refer only to preceding ordered packets;
they never query a concurrently changing Vulkan texture cache.

Each job snapshots the producer floating-point environment, including x86 SSE
control state. Worker execution restores its own environment after each job.
Errors close the queue, cancel pending work and wake blocked producers/barriers;
the original exception propagates to the frontend. A canceled synchronous call
does not wait indefinitely on a promise whose callback will never execute.

Readback, held-frame capture, reset, extent changes and shutdown are barriers.
Window mutation drains prior submissions. Renderer initialization, first visible
window binding, readiness notification, child-window resize and destruction stay
on the SDL/window owner. Later resident presentations run on the worker. Windows
barrier/backpressure waits service synchronous sent window messages without
consuming input or reentering gameplay, so DXGI window messages cannot strand
the renderer behind a blocked window thread. The backend retains its own Vulkan
fence/resource retirement rules after host submission completes.

The frontend explicitly drains before final reports and app destruction, so a
late worker error cannot become a successful exit. Destructor cleanup also joins
the worker before reading backend statistics or destroying the window/device.

## Validation and limits

The portable `mw3render_worker_checks` target covers a distinct worker thread,
owned packet lifetime, FIFO/barriers, bounded byte admission, exclusive oversized
admission, FP state and failed-job cancellation. `mw3geometry_checks` poisons the
native rendering scratch buffers and verifies stock and prepared early handoffs
leave them unchanged. Vulkan fixtures compare prepared movement, normal maps,
fog and near/far clipping against the explicit CPU reference. Threaded DXGI/WSI
fixtures also exercise texture slot reuse, held frames, readback, presentation,
resize and reset. No original media is part of these fixtures.

Windows native Training and matched local performance receipts are recorded in
[the integration validation note](render-worker-validation-2026-11-05.md).
The queue and early-handoff/resource fixtures are portable and are included in
Linux/macOS core CI. GPU/reference image fixtures use the Windows adapter. This
does not make the Windows Vulkan/presentation adapter a working Linux,
macOS or Android renderer. Live validation on those platforms, RTX 5070
qualification, morph/mixed-bone deformation, GPU light lists/shadows, effects,
temporal history and the second runtime adapter remain future work.
