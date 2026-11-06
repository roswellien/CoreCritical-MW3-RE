# Ultrawide display support

The Core Critical native preview supports full-width 21:9 and 32:9 rendering.
Custom mode 16 retains the requested frame instead of reducing it to 16:9.
The camera expands horizontal coverage while preserving vertical FOV and the
original 4:3 projection scale. HUD art and text retain their logical sizes;
centered elements remain centered and edge elements follow the frame edges.

## Choose a resolution

Graphics/Audio -> Resolution now includes these presets:

| Mode | Resolution |
| --- | --- |
| 17 | 2560x1080 |
| 18 | 3440x1440 |
| 19 | 3840x1080 |
| 20 | 5120x1440 |

Saved mode IDs 3 through 16 retain their meaning. On a first start, custom mode
16 uses the desktop dimensions as before. Existing profiles can select a preset
in the graphics menu, or retain their saved custom ScreenWidth/ScreenHeight.
The explicit window dimensions control the presentation window; they do not
override a deliberately selected in-game resolution. For example:

```text
mw3 --cc-vulkan --window 3440x1440 --cc-geometry partial --cc-render-thread 1
```

Select the matching game resolution too. A fixed 16:9 or 4:3 mode still fits
inside a wider window with side bars. Original mode IDs, gameplay decisions,
simulation timing and renderer/HUD appearance selections are independent.

Wide menus use the existing group layout and input mapping. Their width is
chosen when screens are constructed at startup; choosing another gameplay
resolution updates the camera/HUD immediately, while menus keep their consistent
startup layout until restart. `--no-wide-menus` retains the centered original
menu canvas. Neither mode stretches the menu controls to fill the screen.

## Bounds and mapping

Custom frames fit within 7680x2160. Ordinary desktop aspects, including 21:9,
32:9 and oversized 16:9 requests, scale uniformly when above those bounds.
Impossible/extreme shapes still respect the historical 640x480 minimum; those
minimum-dimension clamps are not a promise of arbitrary portrait/extreme-aspect
preservation. Integer ratios avoid overflow for int32 settings and avoid
introducing floating-point flags into native initialization.

The 2D canvas remains 480 logical pixels high for these modes: 1147 pixels wide
at 3440x1440 and 1707 at 5120x1440. HUD anchors, menu groups, absolute pointer
mapping, relative mouse scaling and GPU texture sampling use the same frame and
canvas. The Vulkan adapter receives native camera matrices/projection before
GPU geometry processing, with no extra 16:9 crop or independent stretching.

## Verification

The scoped resolution controls verify preset lookup/labels, custom bounds,
first-start settings, unchanged original modes, horizontal camera scale,
canvas/pointer round trips and HUD center/edge anchors. Local original-data
headless menu checks cover moved buttons, labels, controls, pilot lists and
Mech Lab placement at both 3440x1440 and 5120x1440. They exercise the native
application/menu code with a host fixture; they are not a live gameplay claim.

A native mission fixture switches 5 -> 13 -> 18 -> 20 -> 16 -> 5: horizontal
FOV is 80, 96.42, 112.74, 131.84, 112.74 and 80 degrees, vertical stays at 60,
reticle centers follow the canvas, and each change refits the HUD once.

`mw3ccvulkan_checks --ultrawide` exercises geometry at constant vertical scale,
sharp textured HUD edges through a canvas viewport, full-width readback,
held-frame restore, incompatible held-frame extent rejection, resize/reset and
visible presentation. CTest runs it with DXGI and native WSI, with the render
worker both off and on. Small physical fixture windows verify the same aspect
and projection contracts without allocating a full desktop per case.

Live native Training/menu captures and remaining limits are recorded in the
[validation note](ultrawide-validation-2026-11-05.md). Original media, installed
packs, generated data, images and binaries remain local. This preview is not a
merged release or full campaign/hardware/platform qualification. The portable
core checks do not constitute a Linux/macOS/Android port of the Windows adapter.
