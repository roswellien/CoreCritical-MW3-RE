# Core Critical HUD on the native game

The opt-in HUD keeps the native game's state, text, positions, crops, colors,
inputs and drawing order authoritative. It adapts Core Critical's prepared HUD
artwork and output-density text to owned GPU resources. It observes native image
blits, lines, circles and spans and composes them through the selected renderer.
It does not use original-executable hooks or guest addresses.

## Select the HUD

The default is `--cc-hud original`. To select the enhanced drawing path:

```text
mw3 --windowed --cc-vulkan --cc-hud core-critical --cc-hud-artwork PATH_TO_INSTALLED_HUD_PACK
```

Supply the usual verified local install, fonts and isolated write profile. The
artwork path can name the installed pack root or its `ui/sprites` directory.
Without this option the GPU drawing/text path uses native artwork. The pack
is not bundled, downloaded, copied into the install or committed to this repo.

`--cc-hud-text 0` keeps the native cached text raster; `1` enables output-density
antialiased text (default in core-critical mode). `--cc-hud original` restores
native artwork/text selection. The GPU sharp-filter improvement also applies
to the original HUD/menu layers and needs no appearance opt-in.

Persistent selections can be placed in `mw3.cfg` in the write directory:

```ini
cc_hud=core-critical
cc_hud_artwork=PATH_TO_INSTALLED_HUD_PACK
cc_hud_text=1
```

Explicit command-line settings override these entries. This slice adds runtime
arguments/configuration; the external Core Critical launcher does not yet expose
these controls in its settings UI.

## Resources and rendering

The existing prepared-ui-v1 pack uses files named by their 16-digit lowercase
hexadecimal identity. Identity is FNV-1a 64 over little-endian 32-bit logical
width and height followed by row-major RGB565 words, with zero for a transparent
pixel. It is computed from native game images, never a guest pointer.

Each `.rgba` resource has a 28-byte MW3H-v1 header: `MW3H`, little-endian width
and height, then float32 `u0,v0,u1,v1`; straight RGBA8 follows. Loading validates
exact byte size, dimensions, finite ordered UVs, file count and aggregate size.
The loader premultiplies once into owned storage. Logical crops map into the
authored UV rectangle, including padding. Invalid requested artwork fails visibly.

Text recipes copy font/style, UTF-8 text, native line metrics, color, background
and clip. SDL_ttf rasterizes at output density; glyph advances fit the widths
the native game measured. The cache detects changed pixels and preserves ordered
overlapping repaint operations. It does not retain borrowed native pixels or DCs.

Images use GPU sharp bilinear sampling at the actual quad density, including
cropped high-resolution resources. Filtering/unpremultiplication stay in the
fragment shader. Lines/circles retain native raster steps and cover one logical
pixel with batched GPU quads, so upscaling does not create one-physical-pixel gaps.
Spans are ordered quads. World quality effects precede HUD composition.

The original CPU canvas remains a compatibility shadow for native reads and
screen grabs. Known framebuffer read/write effects (damage static, tint, blur,
ripple, textured spans, canvas erases and frame-grab copies) explicitly compose
that shadow for the affected layer. Logs count these layers and name the reason.
The full-output scalar sharp filter has been removed even for this composition.
This is GPU drawing with a retained native shadow, not elimination of all CPU
raster work. GPU resource failure is an error, not a successful blank/white draw.

## Validation and platform boundary

Asset-free `mw3hud_checks` covers MW3H validation, identity, premultiplication,
crop/order, copied resource ownership, logical pixel geometry, spans, cancellation,
resource release, native shadow preservation, FP environment, actual combined
pause blur, damage static/random-call preservation, canvas erases and failed GPU
texture allocation. `mw3hud_checks --fonts DIR` additionally exercises density,
antialiasing, clipping, repeated/overlapping text and stale-image rejection using
local fonts. Font-dependent tests are explicit rather than silently accepted.

The Vulkan adapter fixtures check cropped/nonuniform sharp sampling,
premultiplied alpha, quality composition and held frames through DXGI and WSI.
Live captures/timings are separate and recorded in the task's validation report.
Startup/menu/Training evidence does not establish complete campaign fidelity.

The HUD loader, display list and SDL_ttf path use portable C++17 and SDL APIs.
SDL's D3D12, Vulkan and Metal shader variants share the sampling change without
increasing shader uniform/push-constant size. The Core Critical Vulkan/DXGI
backend itself remains Windows-specific in this branch. Linux/macOS core CI
and a Windows SDL-only build are separate from live Linux/Metal acceptance;
Android still needs its native frontend, packaging, input and device validation.
None of those unperformed live platform checks is implied by Windows captures.
