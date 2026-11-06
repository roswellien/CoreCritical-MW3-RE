# Ultrawide display support

With `any_resolution` enabled (the default), custom mode 16 keeps the full
requested aspect instead of reducing it to 16:9. The camera shows more of the
world at the sides while preserving vertical FOV and the original 4:3 projection
scale. The native HUD uses the same wider canvas: centered elements stay centered,
edge elements follow the edges, and art and text keep their logical sizes.

## Choose a resolution

Options > Graphics & Audio > Resolution includes:

| Mode | Resolution |
| --- | --- |
| 17 | 2560x1080 |
| 18 | 3440x1440 |
| 19 | 3840x1080 |
| 20 | 5120x1440 |

Saved IDs 3 through 16 keep their meaning. On a first start, custom mode 16 uses
the desktop dimensions. Existing profiles can choose a preset or keep their
saved custom `ScreenWidth`/`ScreenHeight`. Preset IDs 17 through 20 require a
runtime with this support.

An explicit `--window 3440x1440` chooses the presentation window, not the saved
game resolution. Choose the matching game resolution too. Fixed 4:3 and 16:9
selections still fit inside a wider window with side bars.

Wide menus use the existing group layout and pointer mapping. Their width is
chosen at startup. Changing resolution updates the gameplay camera and HUD
immediately; restart to rebuild menus at the new width. `--no-wide-menus` keeps
the centered original menu canvas. Neither choice stretches the menu controls.

## Bounds and mapping

Custom frames fit within 7680x2160. Ordinary desktop aspects, including 21:9,
32:9 and oversized 16:9 requests, scale uniformly above those bounds. Extreme
shapes still respect the historical 640x480 minimum, so arbitrary portrait or
extreme-aspect preservation is not promised. Integer ratios avoid overflow for
int32 settings and avoid introducing floating-point flags during initialization.

The 2D canvas stays 480 logical pixels high: 1147 wide at 3440x1440 and 1707 at
5120x1440. HUD anchors, menu groups and pointer mapping share that canvas.
Window fitting and relative mouse scaling continue to use the actual frame.
Camera projection needs no additional aspect adjustment in a renderer.

## Regression coverage

`resolution_` tests cover original modes, preset lookup/labels, custom bounds,
horizontal camera scale, HUD anchors and frame/canvas pointer round trips.
`settings_first_start_plays_at_the_desktop_size` checks desktop sizing and saved
choices. `menus_wide_follow_the_selected_aspect` checks both wide and narrow menu
selection.

With a local installation configured through `MW3_GAME_DIR`,
`menus_ultrawide_keep_native_controls_and_lab_aligned_headless` exercises native
menu groups, controls, pilot lists and Mech Lab placement at 3440 and 5120.
`mission_c1m1_mode_change_keeps_the_camera_shape_headless` switches
5 -> 13 -> 18 -> 20 -> 16 -> 5: horizontal FOV is 80, 96.42, 112.74, 131.84,
112.74 and 80 degrees; vertical FOV stays at 60. Reticle centers follow the
canvas, with one HUD refit per change. These are native host fixtures, distinct
from live graphics testing. Game files remain local and are not test assets.
