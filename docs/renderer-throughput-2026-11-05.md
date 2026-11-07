# Native renderer throughput investigation, November 5, 2026

The approximately 1200 FPS SDL/D3D12 result was real. The main identified
Vulkan bottleneck was the adapter's scalar, output-sized `sharp_layer()` HUD
filter. Original assets alone reproduce the gap. This evidence supports moving
that filter into the GPU fragment shader before attributing the loss to upgraded
models, normal maps, or Vulkan hardware execution.

## Matched original-asset controls

Release clang-cl build at `ed74ad42c1e856f677999274d27ffce967323dce`, Ryzen 7
9700X, RX 9070, driver 32.0.31035.1003. Both backends use the same executable,
original retail 1.2 data, direct native `t1m1`, seed 1, a fixed 17 ms simulation
step, default controls and scripted key intervals. Window and raster are both
2560x1440. No replacement assets, FXAA, anisotropy, validation, capture or CPU
profiling are enabled. Presentation is immediate; SDL reports D3D12 on RX 9070,
and the Vulkan/DXGI bridge verifies the same adapter LUID.

Each run renders 2200 frames and measures 1399 intervals beginning at frame 801.
FPS is 1000 divided by the reported mean interval. The ABBA order is deliberate.

| Run | Backend | Mean ms | p95 ms | p99 ms | FPS |
| --- | --- | ---: | ---: | ---: | ---: |
| d3d12-a1 | SDL/D3D12 | 0.8709 | 1.0141 | 1.1616 | 1148.24 |
| vk-original-b1 | Core Critical Vulkan/DXGI | 40.8927 | 43.7647 | 46.8871 | 24.45 |
| vk-original-b2 | Core Critical Vulkan/DXGI | 40.6989 | 43.2566 | 46.2491 | 24.57 |
| d3d12-a2 | SDL/D3D12 | 0.8896 | 1.0396 | 1.2489 | 1124.10 |

All four use executable SHA-256
`dc0397015d9e7db19f60cd4307512a288ed8398cbded26dad4af8ab4a231f9fb`.
Both Vulkan runs report 364637 logical/GPU draws and 2201 resident presents.
Monitoring found no overlapping game or build job. These are native game frames;
they do not establish complete campaign fidelity or release readiness.

## CPU diagnostic and resolution control

Separate diagnostic runs enable `MW3_CC_CPU_PROFILE=1`. Their whole-run buckets
include initialization and warmup, whereas their steady frame statistics cover
299 intervals from frame 801 through 1100. The buckets measure CPU-thread wall
duration, including waits inside the wrapped calls. They are not exclusive CPU
cycles or GPU timestamps.

| Diagnostic | Whole-run wall | HUD filter | Renderer flushes | Transport | Steady FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| vk-profile-1440-r2, 2560x1440 | 60.205 s | 56.062 s | 1.659 s | 0.352 s | 24.38 |
| vk-profile-720, 1280x720 | 17.380 s | 14.338 s | 0.616 s | 0.260 s | 89.98 |

Both report 1103 sharp-layer calls, 1102 presents and 4169 renderer flush calls.
The output-pixel counters are 4,066,099,200 and 1,016,524,800 respectively.
Quartering the output area approximately quarters the filter duration. The
1440p filter alone occupies about 93% of the process lifetime. Renderer and
transport buckets include preparation, uploads, fences and other work; they
must not be interpreted as isolated Vulkan device execution time.

The SDL path already applies sharp filtering to native-size textures in a GPU
shader. The imported Vulkan adapter instead expanded changing HUD/menu bitmaps
on the CPU, interpolated and unpremultiplied each output pixel, then uploaded
the expanded resource. The difference persists with the original assets and
quality effects disabled. Issue #16 ports this operation to the GPU and adapts
the existing Core Critical HUD appearance/settings.

## What the earlier numbers mean

The November 4 SDL/D3D12 receipts used upstream
`af29bdfc3ed8720f770d0081c23b60e9c06ba420` plus a local instrumentation patch,
the same direct mission/seed/step, 2560x1440 internal rendering inside a
3440x1440 window, and original assets. Short runs produced 1136-1220 FPS;
a longer 11399-interval run produced approximately 1163 FPS. The matched
controls above substantiate that result without requiring that old binary.

The approximately 21 FPS enhanced Vulkan run followed the live menu into
Training 1 with a different pilot, unarmed Bushwacker, controls, mission
initialization, upgraded banks/models, FXAA and 16x anisotropy. Its accepted
ABBA runs averaged 20.94 FPS with normal lighting off and 20.57 FPS with it on.
That pair isolates only the lighting toggle in that configuration. It does
not isolate model, texture, quality or backend costs against the direct-mission
D3D12 run. The private remaster's 90-108 FPS result is another implementation
and is not the native D3D12 baseline.

## Reproduction and remaining measurements

Use isolated write profiles and verified local retail files. For lean controls,
use `--mission t1m1 --seed 1 --frame-ms 17 --window 2560x1440 --frames 2200
--frame-times 801 --tap-ingame 0x19@800:900 --tap-ingame 0x11@1400:1900
--tap-ingame 0x20@2000:2500 --no-mods --no-import`. Add `--cc-vulkan` for the
Vulkan variant and leave optional asset/quality arguments unset.
`MW3_BENCHMARK_IMMEDIATE=1` selects immediate SDL presentation only if
the backend supports it and logs its actual backend/adapter. `MW34_VSYNC=0`
selects the equivalent Vulkan bridge setting. Profiling remains opt-in and
must be disabled for accepted lean comparisons.

Next compare the GPU-filter replacement against these controls, then stage
quality, textures, prepared models and lighting on one unchanged scene. Any
remaining throughput difference needs CPU/GPU phase measurements; low device
utilization alone cannot assign a GPU bottleneck. Platform and campaign
coverage remain separate from this Windows diagnostic.

Raw run receipts, logs, local harness, pilot/settings copies and captures remain
outside Git in `local/corecritical-mw3-re-gap-20261105` beside the checkouts.
Original media, generated game data and prepared proprietary packs are not
part of this report or PR. The first `vk-profile-1440` attempt was deferred
because another build was active; only the clean `-r2` run is used above.
