# Modular native HUD layouts

A schema-3 `hud` definition with behavior `native-widgets-v1` configures the
existing native widgets. A profile selects one definition through
`selections.hud`. See the project-authored
[`demo-hud` package](../examples/modular/demo-hud/pack.json) and
[profile](../examples/modular/hud-profile.json). They require no artwork payload.

```text
mw3content resolve examples/modular/hud-profile.json --store examples/modular/store
mw3 --content-profile examples/modular/hud-profile.json
```

The sample moves and resizes the native view panels, anchors the radar to the
center, places objectives explicitly, and hides the timer. The remaining
widgets retain native layout values. This is a native layout contract; it does
not introduce new widget types or script execution.

| Widget | Supported fields besides `visible` |
| --- | --- |
| `MFD`, `TARGET` | `position`, `size: {width, height}` |
| `TIMER` | `position` |
| `WEAPONS`, `EQUIPMENT` | `position`, `width` |
| `RADAR` | `position`, `zoom_position`, `radius`, `zoom_radius` |
| `OBJECTIVES` | `position` (top-left of the objective text) |
| `COMPASS` | `tape`, `frame` (positions) |
| `GAUGES` | `speed_frame`, `fuel_frame`, `heat_bar`, `heat_meter`, `heat_text`, `heat_arrow`, `coolant_frame` (positions) |
| `RETICLE` | Visibility only; its position follows native aiming and zoom geometry |

Each position contains `x` and `y`, with optional `anchor_x` (`left`, `center`,
`right`) and `anchor_y` (`top`, `center`, `bottom`). Coordinates use logical
display pixels. `reference_width` and `reference_height` default to 640 and 480.
An edge/center anchor shifts the authored coordinate by the corresponding
difference between the reference canvas and current viewport. Optional `scale`
(0.25–4) scales authored coordinates and supported dimensions; native font and
artwork sizes retain their own rendering contract. Radar coordinates/radii
describe its normal and expanded circular modes. Mission-map mode continues to
use the mission map's world-to-pixel mapping.

`visible` is a boolean, defaulting to true. All ten groups suppress their 2D and
3D presentation, damage-static pixels, and widget-specific interaction when
false. Native updates, countdowns, queued draws, animations and noisy drawing's
random calls continue. Native gameplay targeting, firing, objective evaluation,
and other gameplay commands retain their behavior. The presentation flag is
separate from native `kHidden` flags; it does not force a normally hidden widget
to appear. Changing profiles resets presentation to the newly selected layout
when the HUD is set up. With no selected HUD, native presentation is enabled.

Unknown fields, unsupported widget names, invalid coordinate anchors and unsafe
dimensions fail validation. A fixed reticle offset is deliberately unsupported
because the native reticle position also participates in target acquisition.

## Artwork authoring routes

A selected HUD can bind package-owned MW3H-v1 sprites through optional
`data.artwork`. Add the actual locally authored sprite files beside the source
manifest and compile normally; the compiler pins them in `files`. Declare
`"hud-artwork-mw3h-v1": 1` in `required_capabilities`:

```json
"artwork": {
  "format": "mw3h-v1",
  "sprites": {
    "0123456789abcdef": "artwork/reticle.rgba"
  }
}
```

Replace the example key with the native sprite's actual pixel identity. Keys
are exactly 16 lowercase hexadecimal digits; values are explicit package
resource references. A dependency's resource may use `package-id::relative/path`.
There is no directory scan or implicit file discovery. The selected HUD alone
binds artwork. Empty mappings, unknown fields, invalid headers/UVs/dimensions,
unlisted resources and changed integrity hashes fail visibly. At most 256
sprites and 512 MiB of decoded pixels may be declared. Profile resolution reads
metadata without decoding pixels; the first matching native image resolves its
integrity table again, decodes the sprite, and retains owned pixels. Unmatched
sprite payloads remain undecoded.

This uses the existing native GPU HUD compositor on both SDL and Core Critical
renderers, preserving native widget state, inputs, position, crop and draw order.
Builds without that consumer reject the artwork capability and definitions.
The selected package automatically enables the compositor; no manual artwork
path is needed. A simultaneous manual artwork directory is rejected.

MW3H-v1 consists of `MW3H`, little-endian uint32 width and height, float32
`u0,v0,u1,v1`, then exactly width × height × 4 straight RGBA8 bytes. Dimensions
are 1–8192 and UV bounds are finite, ordered, and within [0,1]. The consumer
premultiplies once into owned storage. Sprite identity is FNV-1a 64 over
little-endian logical width/height and native RGB565 words (zero for transparent
pixels). This is the same prepared UI sprite format used by the existing
authoring/export tools. See [`core-critical-hud.md`](core-critical-hud.md).

The unmanaged manual `--cc-hud core-critical --cc-hud-artwork PATH` route remains
available for legacy prepared directories. The sample layout needs no artwork.

Managed legacy packs can explicitly claim native image archives such as
`zbd/rimage.zbd`; the managed archive mount and integrity checks apply. These
archive replacements affect every native consumer of those images and must be
declared as file claims, with conflicts resolved in the profile. Schema-3 named
`texture/rgb565-v1` definitions provide new names for skins and world materials;
they do not replace stock HUD sprite names. Native-name collisions are rejected.

## Verification boundary

`test_content_hud.cpp` exercises anchored coordinates, validation, all ten
visibility selections, radar dimensions, nested output masking, input hits,
native timers and queued draws, noisy primitive/static RNG preservation, owned
artwork metadata and integrity checks. `mw3hud_checks` additionally exercises
package-selected lazy sprites through the actual native GPU HUD consumer.
Live layout composition with the installed game and prepared artwork remains a
separate playtest check.
