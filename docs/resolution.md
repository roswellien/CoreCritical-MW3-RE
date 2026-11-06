# Screen size and aspect

Core Critical's current ultrawide behavior supersedes the historical 16:9 cap
below. These notes preserve earlier design and validation receipts. See
[ultrawide support](ultrawide.md) for current custom bounds, resolution presets,
menu defaults, camera/HUD/input mapping and validation.

Mech3.exe 1.2, mapped 2026-10-21 (mac 2) for Owen's decision (fixes.md,
Candidates and Decided): any size up to 4K, 16:9 included, 640x480 4:3
identical to the original. A first start plays at the desktop's size
(Owen, 2026-10-30; section 10). 16:9 widens the view instead of
stretching it. The menus and HUD art are scaled, and the menus pillarboxed.
If larger sizes are trouble, the cap is 1920x1080. A FIX (B), behind
`zfix::options().any_resolution`.

## 1. Where the size comes from

The video mode (VMode [0x005f4d0c], and InGameVMode [0x005f4d10] for a
mission) is the only source. `set_vmode` 0x0040d810 (`mech3/options`)
turns it into three rectangles:

- the render rect (0x0040d6a0): what the 3D view draws;
- the window rect (0x0040d7b0): the zClass window node;
- the display rect (0x0040d750): the screen and the 2D layers.

The modes are:
- 3: 320x240 drawn, doubled to 640x480; software only.
- 5..10: 640x480, 800x600, 1024x768, 1152x864, 1280x1024, 1600x1200.
- 4 and any other number keep the sizes they had.

The input gets the display size (0x0051b590, `zin::mouse_set_screen`).
Nothing else stores a width or height of its own: everything below asks
these rectangles, or assumes 640x480.

## 2. Every place the size reaches

The 3D view:

| Exe | What | Port |
|---|---|---|
| 0x0040d590 | the camera's horizontal field of view = the vertical one x render w / h, linear in the angle | `mech3::set_camera`; the getter 0x004f3ee0 and setter 0x004f3d90 were unwired hooks |
| 0x004f9fa0 / 0x004fa0b0 (`WindowResolution`) | the window node's size and origin, from the render rect | `zcls` window (ported) |
| 0x00521c60 | the projection: centre = window origin + half size; `fx = cot(hfov / 2) * w / 2`, `fy = cot(vfov / 2) * h / 2` (render.md section 0) | `zmth` (ported) |
| 0x007402f8 | the clip rect, from the window node | `zmod::clip2d` (ported) |
| 0x004f3b90 | the zoom setter: angle = base / factor, **capped at 1.396 rad (80 degrees)**; only gamez.zbd's camera load (factors 1) and the unshipped anim events 20/21 call it | `zcls::camera_set_fov` |
| 0x00480df0 / 0x00481290 / 0x00481790 / 0x00481a10 / 0x00443320 | the frames: render target and back buffer by the three rects | `app_states` (Direct3D path); the software paths are not ported |
| the HUD's radar 0x0043f0c0 | the view cone from the camera's +0xe8 | follows the camera |

The 2D side:

| Exe | What | Port |
|---|---|---|
| zTwoD's dialogs, every .zrd position | 640x480 pixel positions | `ztwod`, the menus (menus.md section 7, mainmenu.md section 3) |
| 0x0040a110 / 0x0040a210 / 0x0042fb30 | the menu backgrounds' views and wraps, MECH3TEXT's slide: the display size | `menu_widgets` |
| 0x004289a0, 0x0042a590, 0x0042da80, 0x0042e890, 0x00486260, 0x00488bc0, 0x00493260, 0x00495f90, 0x00438cc0 | each screen's background view at the display size when it is made | credits, controls, main menu, pilot screens (ported); 0x00438cc0 the graphics screen's |
| the version line 0x005c4b04 / 0x005c4b00 | the constants 640 and 480 | `mainmenu` |
| zVideo buffer 2 grabs (controls' axis popup, load/save) | the display size | ported |
| the still background 0x0055b3c0 | a 1280x960 image: the source rect past it above ~1000 lines was the community's crash; the port clips (fix done) | `zrndr::blit16` |

The HUD:

| Exe | What | Port |
|---|---|---|
| 0x0043bb80 | hud.zrd's block by VMode: 3/5 V640x480, 6 V800x600, 7 V1024x768, 8..10 the three hidden; any other mode keeps the 640x480 defaults (hud.md section 3) | `hud`, `app_view` |
| constructors (hud.md section 5) | 640x480 default positions, radar area 320x240, message lines from y 0x5f, fixed fonts | `hud` |
| 0x0043d6d0, 0x0043fa40, 0x00440510, 0x00440b00, 0x004438f0, 0x004445f0 | the zoom window, on-screen clamps, target lines, the arrow, the centre at the display's half size | `hud`, `app_hud` |
| 0x0040f330 | the HUD's text area re-placed after a mode change (display size) | not ported |
| 0x00479dd0 / 0x00479ef0 | the mission timer's 2D box | partly (`hud.h`) |
| 0x00468900, 0x004690d0 | the mech's screen-space aim (window rect; unproject) and a display-width text | not ported |

The modes and settings:

| Exe | What | Port |
|---|---|---|
| 0x0040c420 | detail.zrd's VMode (not saved, 5 on any modern PC), InGameVMode read and saved as any number (settings.md) | `options` |
| 0x0047fc10 / 0x0047fe10 / 0x00480970 | the start-up: an earlier run's mode re-applied, VMode below 5 raised to 5, the screen opened at it | `app` |
| 0x004ab860 | the device's range: software 3..5; a card 5..7, cut to 6 / 5 by a video-memory estimate | `Engine::video_mode_range` |
| 0x0043a470 | RESOLUTION: the modes in the range, labels 0x41..0x44 and 0xfda..0xfdc (8..10 named but never in range) | `ResolutionCycle::rebuild` |
| 0x004ab6b0 | the switch: zVideo's surfaces re-made at the new size, the render target, the HUD (0x0040f330), every mech's cockpit (0x0041cdc0) | not ported (`set_screen_mode` hook) |
| 0x004ab910 | Shift+V (`VMODE_TOGGLE`): the next mode in the range, saved as InGameVMode | not ported |
| 0x00481f70 | Core state: the drawing path re-picked when VMode changes | `app_states` |
| 0x00482070, 0x004834b0 | the pixel doubling (mode 3) and the stride for the software finish | `app_states` |

The platform (FIX (A) layer):

- `GameEngine::video_open` makes the 16-bit 2D buffer at the display size
  and sets `Render3D`'s viewport to it.
- `SdlRender3D` renders into a back buffer the window's pixel size and
  stretches the game's viewport over it: a 16:9 mode in a 4:3 window is
  squashed, so the window must take the mode's shape, or be pillarboxed
  or letterboxed.
- `mw3_main.cpp` scales the mouse by `640 / window width` (`mickey_scale`),
  a 640x480 assumption.
- `--window WxH` (default 1280x960) is independent of the mode.

## 3. The original's view is not square

At 640x480 the camera is 80 degrees across and 60 high (the world's camera
data, and 0x0040d590's rule 60 x 640 / 480). Projected onto 640x480,
tan 40 / tan 30 = 1.453 against 640 / 480 = 1.333: the horizontal scale is
`cot 40 x 320 = 381.4` against the vertical `cot 30 x 240 = 415.7`, so
everything is drawn about 8% narrower than tall (discoveries.md).

0x0040d590's rule is linear in the angle. At other shapes it gives:
- 1920x1080: 106.7 degrees across, which squeezes the picture to 77%.
- 1280x1024 (5:4): 75 degrees.

The 16:9 fix keeps the original's 4:3 look: the same vertical angle and
the same squeeze. The rule is `tan(h / 2) = tan(v / 2) x (w / h) x k`, with
`k = tan(2v / 3) / (tan(v / 2) x 4 / 3)`; k is 1.090 for 60 degrees, so the
squeeze is 1 / k = 0.9175. At 16:9 that gives 96.4 degrees across, where
square pixels would give 91.5 and the linear rule 106.7. A 4:3 size takes
the original's formula unchanged, so 640x480 stays bit for bit.

## 4. Porting order

1. **Settings and the mode list (done 2026-10-21, section 5)**: the sizes,
   the graphics screen's list and the range check, the camera's angle for
   any shape. Headless.
2. The window's shape: `--window` and the SDL window follow the mode's
   aspect (or pillarbox the back buffer); the mouse scale from the display
   size, not 640. Platform only; Owen checks it on screen. Done
   (2026-10-28), section 6.
3. The mode switch 0x004ab6b0 and Shift+V 0x004ab910 on the port's zVideo:
   the 2D buffer, `Render3D`'s viewport, the HUD's text area 0x0040f330, the
   cockpits 0x0041cdc0. Done (2026-10-28), section 7.
4. The HUD by anchors: the 640x480 block's pieces placed from the left,
   right and bottom edges of any size (hud.md section 5), with a scale for
   the art. The hud.zrd blocks stay for their own modes when the fix is
   off. Done (2026-10-28), section 8.
5. The menus: one 640x480 frame scaled to the height and centred
   (pillarboxed), the cursor and clicks mapped through it; the backgrounds
   tiled past 2560x1920 (menus.md section 7). Done (2026-10-28), section 9.
6. The rest of section 2's unported readers (0x00468900's aim, 0x00479dd0's
   timer) as their modules land.

## 5. The port (step 1)

`zfix::options().any_resolution` (FIX (B), on; the probes turn it off).
With it:

- **Modes.** 3 and 5..10 are the original's. 11..15 are 1280x720, 1600x900,
  1920x1080, 2560x1440 and 3840x2160. 16 is the player's own size,
  `ScreenWidth` x `ScreenHeight` in the settings (saved, 1920x1080 by
  default), clamped to 640x480..3840x2160. Without the fix, 11..16 keep
  their sizes, as any unknown mode does in the exe. `mech3::vmode_size`
  answers a mode's size, and `kMaxScreen` (3840x2160) is the cap: one
  constant to lower to 1920x1080 if larger sizes are trouble.
- **The range.** A 3D card offers 5..16, since a modern card has the memory.
  Software stays 3..5.
- **The list.** RESOLUTION shows 8..10 under their own names, then the new
  sizes, labelled "1280x720" and so on, the custom one by its size.
- **The camera.** `set_camera` keeps 0x0040d590's linear rule when
  w x 3 == h x 4. For any other shape it uses section 3's rule. The
  options' `camera_get_fov` / `camera_set_fov` are wired to zClass
  (0x004f3ee0, ported now; 0x004f3d90).

Checked by `tests/test_resolution.cpp`:
- every original mode's sizes;
- the new modes and the custom size with its clamps;
- the range and the list, with the fix and without;
- the camera at 640x480 (bit for bit the original's rule);
- at 16:9, 5:4 and 21:9 (the vertical kept, the squeeze kept);
- through the wired hooks onto a real camera node.

The original's paths are those of test_app_options.py and test_menus
(fix off). Breaks: with square pixels (k = 1) the 16:9 test fails, and so
does the modes test with 11..16 not gated by the fix. Removing the 4:3 guard
changes nothing: on 20,000 vertical angles the new rule at 4:3 rounds to
the original's float every time, since tan and atan cancel. The guard only
keeps the original's own expression.

## 6. The window (step 2)

Platform only (the FIX (A) layer), behind `any_resolution`; the game's
logic is untouched. `sdlfe::FrameShape` (`src/frontend/sdl/sdl_fit.h`) is
the frame the game draws (the display rect, which `GameEngine::video_open`
hands the window through `platform::Window::frame_size`), and `fit_rect` its
place in the window: the largest centred rect of its shape.

- **The back buffer** (`SdlRender3D::ensure_frame`) is that rect's size;
  `present` blits it into the rect and clears the bars black. A shot is
  the window as shown, bars included; the fade's still (`read_back`) is the
  frame alone.
- **The window**: without `--window` it takes the frame's shape when the
  screen opens (`default_window`: the frame, doubled while under 1280
  wide, so 640x480 opens at 1280x960 as before). A given `--window` stays,
  with the frame pillarboxed or letterboxed in it.
- **The mouse**: SdlZinHost's client area is the frame, and the cursor maps
  through the rect (`window_to_frame`, `frame_to_window`; the bars map
  outside the frame). An uncaptured relative mouse is a frame pixel a
  mickey through the rect's width (`mickey_scale`), where `mw3_main` had
  640 / the window's width. The game's own mouse is captured (exclusive):
  there a mickey is a count of the device's, as DirectInput's were at any
  resolution (2026-10-30), so a bigger window or screen no longer slows the
  look and the cursor.
- **High DPI**: the mouse's mapping uses the window's coordinates
  (`SDL_GetWindowSize`, the events') and the frame's draw the window's pixels
  (`SDL_GetWindowSizeInPixels`); the fit rect is the same fraction of both.
  On Windows SDL3 is per-monitor DPI aware: coordinates are pixels (density
  1) and 150% is only the display's scale. On a Retina Mac the density is 2
  (the window has no SDL_WINDOW_HIGH_PIXEL_DENSITY, so the frame draws at
  the points' size). `sdl_frame_maps_alike_in_points_and_pixels`.
- **Off**: the frame is stretched over the whole window and the mouse's
  scale is 640 / width, as before.

`sdl_window_shape_maps_the_mouse_through_the_frame` (mw3sdltests) checks the
rect and the mapping at 4:3, 16:9, pillarboxed and letterboxed, and with the
fix off. It also checks SdlZinHost's mapping and SdlInput's mickeys in a
hidden 1280x720 window (96 points of motion are 64 mickeys). Break: a fit
rect that always stretches fails 9 checks.

A 16:9 frame itself waited on step 3. VMode is detail.zrd's, not the
settings', and 5 on a modern PC; the mission's InGameVMode comes in through
the mode switch (section 7).

## 7. The mode switch (step 3)

The Core state's enter and resume switch to InGameVMode. Its leave and the
options screen's enter switch back to 5, the menus' 640x480.

- **0x004ab6b0** (`Engine::set_screen_mode`, app_hooks.cpp), as the exe:
  - The same mode only restores the surfaces (0x00577cb0).
  - Else, between the lock and the unlock, the software path keeps a still
    of the screen (0x00579500). zVideo then takes the mode (0x00576c80); if
    it refuses, the old mode stays.
  - Then the options' rects (0x0040d810) and the two render targets
    (0x00557f90, 0x00558290), as the Intro state sets them.
  - The software path draws the still back ([0x0072f360], the flip between)
    and remakes the spans, aborting with "Error opening HSE" if they fail.
    The hardware path clears.
  - Then the cursor, the message panel (0x0040f330: ECX the display rect,
    EDX the render rect) and every mech's cockpit (0x0041cdc0) over zEntity's
    list. The still is freed last.
- **0x00576c80** (`GameEngine::video_set_mode`, FIX (A)): the 16-bit buffer,
  Render3D's viewport and the window's shape at the mode's size, then the
  defaults the device's setup leaves (as `video_open`). The sizes are
  0x0057a1a0's for 5..10, and FIX (B) `vmode_size`'s for 11..16. A mode it
  doesn't know keeps the size.
- **0x0040f330** (`mech3::message_panel_place`): the panel centred across the
  display, shown its height above the bottom, hidden just under it, and put
  at its hidden place. The two rects go to 0x005fc34c..0x005fc374
  (`panel_area`), which nothing reads. At the start the panel's constructor
  stands in for 0x0040f870, reached only through the developer lines' font
  set-up 0x0040f7f0 (deferred).
- **0x0041cdc0** (`Engine::cockpit_refit`): the view's own mech's HUD
  (0x00430aa0 on +0x684) set up again (0x0043bb80) for the new mode's
  hud.zrd block. The program walks its own entity list
  (`GameEngine::screen_mode_cockpits`).
- **0x004ab910** (`Engine::vmode_toggle`, Shift+V): 3 to 5, 5 to 6, 6 to 7,
  the rest to the range's lowest, and past its highest to the lowest; the
  mode reached is saved as InGameVMode. FIX (B): with any_resolution, 7..15
  step on to the next (the range is 5..16).

Checked by test_app_states.py:
- `screen mode`: ECX 3..7 and 11, the same mode, zVideo refusing,
  software and hardware, the spans failing, zEntity's list with mechs.
- `vmode toggle`: with 0x004ab860 run on both sides.
- `cockpit refit`: the view's target, another mech, no HUD.

test_hud.py's op 43 runs 0x0040f330 with each rect none or one of the
options' three, and compares the panel and the six globals.

Breaks:
- the panel's two rects swapped fails `screen mode`;
- 6 going to 6 fails `vmode toggle`;
- the view check dropped fails `cockpit refit`;
- the shown y one lower, or the centre without the area's x, fails
  test_hud.

In the program, c1m1 with InGameVMode 13 opens its window at 1920x1080
(the frame's shape, step 2) and draws the 16:9 view, with 0 calls outside
the known table. As expected, the HUD keeps its 640x480 block at the top
left until step 4: hud.zrd has no block for 11..16, and the pieces' places
are 640x480 ones. The message panel is centred across 1920, but its block
is the only one placed for the size. Not fixed here.

## 8. The HUD by anchors and the art's scale (step 4)

Behind `any_resolution`, for the modes past 10 only. Modes 3..10 keep
hud.zrd's blocks and the exe's sizes, with the fix on as off.

**The canvas.** The 2D (the display rect: the HUD, the message panel, a
pause menu's dialogs) is drawn on a canvas 480 high and of the frame's
shape (`vmode_canvas`): 853x480 at every 16:9 size, 1138x480 at 2560x1080.
The render and window rects keep the whole frame (the 3D and its zClass
window), as mode 3 keeps a render rect apart from its display. The 16-bit
buffer is the canvas, and its layer is stretched over the frame by the art
scale, the frame's height over 480 (`art_scale`: 1.5 at 720p, 2.25 at
1080p, 3 at 1440p, 4.5 at 4K), so the HUD covers the same share of the
screen as the original's at 640x480 (Owen, 2026-10-30: "as big as it was").
Until then the scale was whole (the height over 480, rounded down): at
1280x720 the art drew at 1x, smaller than at 640x480, and at 1080p at 2x.

**Sharp scaling.** The layer is drawn sharp bilinear (`DrawState::sharp`,
`sample_sharp` in both shader sets): each texel flat but for one frame
pixel's blend at its edges, so a whole scale (2x, 3x) is exactly nearest
and a fractional one stays crisp without uneven pixel widths. The layer's
texels are premultiplied, so a piece's edge blends toward the key's clear
texels without a dark fringe. It is the layer's draw everywhere: the menus'
640x480 frame in a 1280x960 window, until then plain bilinear (soft), is now
nearest at 2x. `sdl_layer_scales_sharp` draws a layer onto a 1280x960 and a
960x720 frame and reads it back: at 2x every pixel is its texel's colour, a
lone texel beside the key keeps its own, and at 1.5 a texel's first pixel
is its own. Plain bilinear fails it 3 times.

**Between the two.** The exe already maps the HUD's screen points through
0x0040d9e0 / 0x0040da30 (mode 3's halving and doubling). With a canvas
they map by the render-to-display ratio: the reticle's ray, TARGET's
tracking, the pocket panel. The pieces drawn in the 3D pass from canvas
pixels go onto the frame the same way: the HUD views' zClass windows
(`HudViewWindow::set_area`), the panels' quads and their depth clears, and
the MFD's and TARGET's view clears, and the colour tint's quad (0x00558540
-> 0x0057dd80: the mission's opening fade, the hit flashes; its rect is the
canvas's, the finish target's; `tint_quad_on_frame`, playtest S14: until
2026-10-30 the opening fade darkened only the canvas-sized top-left part of
the frame). On a canvas, the HUD's own window rect is
the display (`HudHooks::window_rect`). The 3D's viewport is the frame and
the 2D layer's the canvas (`GameEngine::screen_size`). The window takes the
frame (step 2), and the cursor is in canvas pixels (`FrameShape::canvas_w`).

**Projected points (the audit, after the lead's review).** Every 2D piece
placed from a 3D projection (0x0053cf10), and how it reaches the canvas:

| Piece | Projection | On the canvas by |
|---|---|---|
| TARGET's bracket, its size and lead mark (0x00440b00) | the player's view | 0x0040da30, as the exe |
| TARGET's radar line to the target (0x00440b00..) | the player's view, or the zoom window's | 0x0040da30, as the exe |
| TARGET's bracket while the zoom window is up | the zoom camera, in the zoom window's frame pixels | FIX (B) `render_point_to_canvas`; the exe maps nothing |
| The waypoint arrows (0x00479ef0, `hud_navarrow_*`) | the player's view | FIX (B) `render_point_to_canvas`; the exe maps nothing, as at mode 3 |
| The pocket panel's markers and lines (0x00469d00..) | the player's view | 0x0040da30, as the exe |
| The reticle | not projected: the display's centre and the look offset; its ray goes out through 0x0040d9e0 | as the exe |
| The radar's contacts, the compass, the torso marker | not projected: world positions and angles on the radar and the tape | |

The waypoint arrow had been placed in frame pixels on the canvas: at 0.91
of the height at 1080p, and in the bottom corner at 4K, against 0.47 at
640x480. `mission_c1m1_hud_anchored_at_any_size_headless` checks its share
of the height at modes 13 and 15 against mode 5's (0.444 against 0.446).
Without the mapping it reads 0.891 and 1.000.

**The layout.** hud.md section 5's anchors on V640x480, at the canvas's
size. At 853x480 the right-anchored pieces move 213 and the centred ones
106: the MFD at 743, 370, the weapons at 743, 30, the compass at 351. (On
the earlier 960x540 canvas the bottom-anchored ones moved 60 as well.)

Found on the way: the program's HUD hooks answered the video mode with 0
(`vmode()` read an unset `[[0x005f4d0c]]`), so the program's HUD never read
a hud.zrd block. At 640x480 the constructor's defaults are V640x480's, so
it never showed. Wired to the options' mode, so modes 6..10 now take their
blocks, as the exe does.

Tests:
- `resolution_hud_anchors_make_the_original_blocks`: 70 of 70.
- `resolution_canvas_past_mode_10`: the canvas sizes, the rects at mode 13,
  the conversions, mode 3's doubling, the fix off.
- `mission_c1m1_hud_anchored_at_any_size_headless`: c1m1 at InGameVMode 5
  (with the fix on and off: V640x480's places) and at 11, 13 and 15 (the
  anchored places on 853x480).

Breaks:
- the compass anchored left fails both anchor tests;
- an art scale of 1 fails the mission test 4 times;
- the whole scale (960x540 at 1080p) fails the canvas and mission tests 14
  times;
- the anchors never applied fails it twice.

Shots: c1m1 at modes 13 and 15; after the fractional scale, c1m1 with
TARGET, the MFD, the radar and the weapon list at 640x480, 1280x720,
1920x1080, 2560x1440 and 3840x2160, and the objectives box at 1080p.

## 9. The menus (step 5)

**What step 2 already covered.** The front end, the mech lab and the
briefing (its map and movies) always run at VMode 5: VMode is detail.zrd's
and never saved, and InGameVMode reaches only a mission. They are one
640x480 frame, which step 2's fit scales to the window's height and
pillarboxes, with the cursor mapped through it. A 1920x1080 window draws it
1440x1080 at x 240, and 3840x2160 2880x2160 at 480.
`sdl_menu_clicks_land_on_the_same_buttons_at_any_window` puts each of the
main menu's seven buttons through the mapping at 640x480, 1920x1080,
3840x2160 and 2560x1080 (28 of 28).

**What was left: the 2D over a mission at a canvas mode.** These are the
pause screen (pushed over the Core state, which keeps the mission's mode)
and the quit box (made at the Core state's enter, after the switch). They
were made at their 640x480 places in the canvas's top-left corner. FIX (B),
`ztwod::dialog_origin`: GameEngine sets it with the screen (`screen_size`),
centring the 640x480 frame on a canvas ((106, 0) on 853x480) and (0, 0)
otherwise. A Dialog (0x0059ec40) and a MenuScreen (0x0040a110) made then
take it as their origin, so every piece the .zrd places, and every hit
rect, moves together. The cursor is in canvas pixels (step 4), so clicks
hit them without further mapping. The art is drawn at the canvas's scale:
2.25 at 1080p, 4.5 at 4K. The pause screen's own backdrop (the mission's
frame) and MECH3TEXT's slide stay display-sized, as in the exe. Options from
the pause switches back to mode 5 first (0x00438c90), so it is the 640x480
front end.

**The backgrounds past 2560x1920.** They are no longer needed: the 2D is
never larger than a canvas (at most 960 by about 540 past mode 10), and
the menus' scrolling backgrounds only run at mode 5. (The canvas is 480
high, wider only for shapes wider than 4:3.)

`--click-at X,Y@N` takes a 640x480 frame pixel and adds the dialogs' origin
over a mission (the Core and Pause states); the menus' frame is at 0. With
wide menus a piece in a middle group is half the extra width further and one
in a right group all of it: the program's own clicks say which (`--briefing`'s
ACCEPT right, `--lab-mech`'s MECHLAB middle; mw3_main).

Tests:
- `mission_c1m1_pause_click_at_any_size_headless`: c1m1 at InGameVMode 5,
  13 and 15, Escape, then a real mouse click (zInput's position and the
  left button) on RESUME resumes. At 13 and 15 the button sits 106
  further right. A click a pixel past it misses.
- Break: the origin left at (0, 0) on a canvas fails 4 checks.

A regression found while taking the lab's shot. Item 21's port of
0x00558010 (`render_flush`) read its rect argument as memory, but its
callers pass handles: the lab passes the screen's (0x00579880) and the Core
state's software paths an options rect. That gave the 2D target 0 x 0, and
the mech lab drew black from merge 4c2d7cf on. Bisected: f28c712 and
e737613 draw it, 4c2d7cf onwards don't. Now the handle is resolved
(the 16-bit buffer's size, or the options' rect).
`render_flush_takes_the_rect_by_its_handle` checks it; the memory read put
back fails it twice.

Shots:
- the main menu in a 1920x1080 window;
- the lab in a 1920x1080 window;
- the pause at mode 13.

## 10. The first start (2026-10-30)

Owen: 16:9 by default. FIX (B), with any_resolution. With no InGameVMode
saved, `load_options` (0x0040c420) makes the mission's mode 16 at the
desktop's size (`OptionsHooks::first_start_screen`, the program's
`platform::Window::first_start_size`: SDL's desktop mode, in points as the
frame is on a Retina Mac, pixels on Windows), which `vmode_size` keeps
inside 640x480 .. 7680x2160, preserving ordinary desktop aspects when reduced.
The size goes to ScreenWidth / ScreenHeight,
so RESOLUTION lists it ("2560x1440") beside the fixed presets and the next start reads it back. A
saved InGameVMode (or a saved size) wins: the store reads the saved values
over the defaults. With `--no-wide-menus`, menus stay 640x480 and pillarboxed.
Wide menus default to the selected startup canvas; changing the gameplay mode
keeps that menu layout until restart. See [current behavior](ultrawide.md).

Only a play run asks: checks, shots, `--frames` and the chains keep the
original's 5 (`set_first_start_desktop`), so the tests stay at their sizes.
In a window (`--windowed`) a desktop-sized frame's window is shrunk into
the display's usable area, the frame fitted in it.

`settings_first_start_plays_at_the_desktop_size`: an empty settings file
gives 16 at the desktop's 2560x1440; a 5K desktop plays at 3840x2160; a
saved 13, or a saved 16 at 1280x720, wins; no desktop (a check) or the fix
off gives 5. Break: the mode not set fails it twice.

**Historical 16:9 cap, superseded by Core Critical's ultrawide support.**
The previous policy (Owen, 2026-10-30, instead of extra cockpit geometry for
wide screens) made mode 16's size (`vmode_size`) at most 16:9: a 21:9 or 32:9
screen gets a 16:9 frame at its height, which the window's fit (section 6)
pillarboxes, centred. 3440x1440 plays at 2560x1440 at x 440, 2560x1080 and
3840x1080 at 1920x1080. The first start's desktop size and RESOLUTION's own
size ("2560x1440") both come from it; ScreenWidth keeps the desktop's.
Narrower than 4:3 (5:4, 1280x1024) is unchanged. The fixed modes 11..15 are
16:9 already. Tests: `resolution_new_modes_only_with_the_fix` (the sizes at
21:9, 32:9, 5:4 and 16:9, and the canvas), the first-start test (a 3440x1440
desktop), `sdl_window_shape_maps_the_mouse_through_the_frame` (the fit at
21:9 and 32:9). Break: without the clamp, 6 checks fail.

## 11. Playtest W13: a mode changed mid-mission (2026-10-30)

Owen's shots from his PC (d02a386, D3D12): after choosing RESOLUTION
1920x1080 from a mission played at 640x480, two things were wrong.

**The 3D picture stretched.** The exe sets the camera's angle across only
at the mission's load (0x0040d590, called from 0x0048ffc6 and 0x00490503).
Its modes were all 4:3, so a mode change never needed it. Ours are not:
the mission kept 640x480's 80 degrees across, now drawn over a 16:9 frame,
which is the 4:3 picture stretched 1.33 wide (Owen measured about 1.4 on
his own mech). The mode switch 0x004ab6b0 (`Engine::set_screen_mode`) now
sets the angle again for the new shape, with any_resolution: 96.4 degrees at
16:9, 80 back at 640x480, the vertical 60 kept. My earlier checks all
started the mission at its mode, so they never saw this.

**The menus' widgets 106 pixels right of their art.** Core's start makes
the menu screens the mission can reach (the options, the controls, the
debriefing and others) while the screen is in the mission's mode. A
dialog takes its frame's origin when it is made (section 9). On the 853x480
canvas that origin is 106, but the options show at mode 5 (0x00438c90 sets
it), on the 640x480 canvas. The art (a grab) was in place, while every label,
value, slider and button, and their hit rects, sat 106 to the right. Now
`GameEngine::construct` makes every menu screen with the origin at 0, except
the pause screen, which shows over the mission on its canvas.

Tests:
- `mission_c1m1_mode_change_keeps_the_camera_shape_headless`: c1m1 at mode
  5, then 13, then 5. The camera reads 80 by 60, then 96.42 by 60, then 80
  by 60. Without the fix it stays at 80 at 1920x1080 (1 check fails).
  Each change also refits the player's HUD (0x004ab6b0's loop, 0x0041cdc0
  on the view's mech: Hud::setup once a change, `Hud::setups`; break: the
  loop empty, no setup). The exe's loop walks zEntity's list at
  [0x0073655c]..[0x00736560]; the base Engine's (the emulator probes')
  reads that mirror, which the program never writes, but the program's
  own (`GameEngine::screen_mode_cockpits`, and `update_shadows` through
  `update_mech_shadows`) walk `ai::entity_list()`, so both run in play
  (2026-11-01, the lead's check of dev 2's note).
- `mission_c1m1_options_from_the_pause_keep_the_menus_frame_headless`: c1m1
  at modes 5, 13 and 15. Escape, the pause screen's OPTIONS, then GRAPHICS &
  AUDIO. Both screens have origin 0, and ACCEPT's and RESOLUTION's rects are
  mode 5's. Without the fix, at 13 and 15 the origins are 106 and the rects
  106 to the right (4 checks fail).


## 12. 16:9 menus (2026-10-30; on by default 2026-10-31)

Ours, `zfix::options().wide_menus`, a display switch beside any_resolution
(it changes no play, so `--original` keeps it). On by default (Owen,
2026-10-31); mw3.cfg's `widemenus=0/1` and `--no-wide-menus` /
`--wide-menus` set it, the command line first. `Engine::menu_vmode` gives
the menus the in-mission mode when its canvas is wider than 4:3 (853x480
at 16:9), else 5. The shape, not the size: the original's modes up to 10
(1600x1200) draw the 2D at full size and are 4:3, and a size test alone
would have put the menus on a 1600x1200 canvas
(`menus_wide_by_default_at_16_9_only`). Tests that pin the 4:3 menus'
places set it off (`mission_c1m1_options_from_the_pause_keep_the_menus_frame_headless`). It is chosen once, at the start-up: the screens
are laid out when they are made (an OptionsState makes its screen in its
constructor), so a mode picked in the graphics options reaches the menus at
the next start, not screens already made for another width.

`GameEngine::widen_screen` (app_menus.cpp) runs on each menu screen as it is
made, except the pause screen (it keeps the dialog origin, section 9):
- a background 600 or more across (its rect, or an opaque plain image's
  width up to 640: the briefing's map; an overlay the table stretches is
  never one) goes to the middle, its sides filled from its own edges,
  mirrored about its edges and darkened, into a new image (the player's
  art on disk is not touched; until the lab the left side was a copy of the
  first columns, not a mirror, a seam at the art's edge); a screen can start the mirror further in (the briefing's 40,
  past the grid's labels and the north arrow) and set the sides' shading
  (150/256 at the art down to 60 at the canvas's edge; the debriefing's
  dark circuit board 256 to 160, else its sides read black);
- per-screen group tables (frame rects, anchored left, centre or right)
  move each object whose middle is in a group, so a sphere, its label, its
  ripple and its trap rect move together; the rest moves by half the extra
  width, as one block with the background. A screen with no table is one
  centred block;
- a main menu button's underline (AUXBITMAP, the button's own image, not on
  the screen's list) moves with its button;
- controls move first, each object to its start plus its offset; some move
  pieces of their own that are on the list too (a cycle its value,
  0x0048b9b0; an edit bar its label, 0x0048b220), and an object already
  moved that way is left alone. (Moved relative to where it stood, a
  cycle's value went twice the distance: the graphics screen at a centre
  offset showed every value 106 right of its box.) A check button or list
  box's rect is BOUNDS as read, not its place, so it moves too (else the
  controls' table drew moved and took clicks where it was); a file list's
  UP and DOWN (its own, not on the list) move with it, and its rect when
  its image set it (0x0048d2f0); an edit bar moves as an image: its
  after_bind (0x0048b1d0) keeps its label's place, not its offset, so its
  set_pos (0x0048b220) would put the label down by the bar's own place (the
  pilot screen centred showed PILOT ENTRY's caret under its bar); a plain
  polyline's points are where it draws (0x005a38f0), so they move with it
  (the briefing's lines); a briefing waypoint is put back on its nav point
  every frame (0x004081a0), so it keeps the map's shift (ours, `shift_x`);
  a ripple button's ripple (SlideButton) is carried by the offset, not put
  again at its image's centre, which a baked or not yet shown button has not
  (playtest W41: HOST's ripple at its corner);
  a Line2D draws from its place to its end, so its end moves too (the
  debriefing's totals); a control whose rect runs from 0 to past its own
  place (its bounds pulled to a label at 0: the lab's data and lists) is
  placed in its group by its own place;
- an overlay drawn as one image across two groups is stretched at a column
  between them, from a row down (that column repeated, alpha and flags
  kept, the new columns above that row left clear), by the right group's
  offset less its own: the game options' LINES (483x346) at column 367, so
  the link from the settings' panel lengthens and the screens' box lands
  with GRAPHICS & AUDIO and CONTROLS; the controls' LINES (596 wide,
  centred) at column 400 from row 300, so ACCEPT's and CANCEL's lines go
  out to them.

Done: the main menu (0x486260), the game options (0x4379f0) and the
graphics and audio options (0x438f60: the panel left, ACCEPT, BACK and their
plate right) and the controls (0x42a590, which GameEngine::construct makes
apart from the other screens: the table, its lines and the description one
block in the middle, the DEFAULT buttons left, ACCEPT and CANCEL right) and
the pilot screen (0x486f40, adopted by its state like the controls' two
popups, which GameEngine::adopt_screen now widens by their sizes: PILOT
ENTRY and the pilots left, OPERATION, ACCEPT and CANCEL right, LINES2 (619
wide) stretched at column 320, which only its top line crosses).
The briefing (0x405230) is one centred block on its widened map. Its
script's objects are made with it (read_script), so the widening sees them.
The debriefing (0x47b430, made apart too): MECH STATUS and ABORT left,
SALVAGE FOUND and CONTINUE right, MISSION SUCCESS and REPLAY in the middle.
The mech lab (0x44b920, made apart too) is one centred block: its panels
change looks as it goes (LabPanel::show_look, a double one across two
columns), so its columns cannot be split once. Its edge panels' three
looks are made wider outward (the left one stays at 0, its art half the
extra width in), shaded as the debriefing's board. The 3D view's window
(lab_place_window, the 640 rect 210,52..430,288 at [0x005e4688]) moves
with the block and is scaled to the render target: at mode 13 the 3D
target is the render size, not the canvas's (unscaled, the 'mech drew
small at the top left).
Instant Action (0x488bc0; its pilot screen first, in its IA mode, laid out
as the pilot screen): ALLIES and ENEMIES left, the mission's box, ACCEPT and
CANCEL right, ALLY_TITLE's image (576 wide: the lines of both boxes)
stretched at column 385, at the link between them. Its cycles' rects are
pulled to 0, so they go by their own places (the worlds at 420: right).
The controls' two popups (assign axis, load/save) are one centred block
each; their backdrop, a grab of the canvas put in as each opens (prepare),
is already the canvas's width and stays where it is. The message boxes
(dlgMessage.zrd) are centred by the dialogs' origin (section 9); their
TITLE and MESSAGE are primitives, whose POSITION (Object2D::read,
0x0059feb0) had not taken the origin: they sat at their 640 places, left of
the box. Now it does (fixes.md, the dialogs' row).
The credits (0x4289a0) are one block, their lines (431 wide at 0,70) kept at
the left edge and stretched at column 100 by half the extra width (the
table's `to_middle`), so the line from the edge still reaches the box. CREDITS_SCREEN's RECT (read with the dialog, 0x429200) moves with the control, since each frame places the names in it (0x429120; playtest W60: they had stayed 106 left of the box). The
salvage screen (0x47d3b0) is one block on the debriefing's board, shaded as
it. Training and multiplayer open on the pilot screen in their modes.
The multiplayer screens (piece 14, 2026-11-01): the connection screen
(0x493260: NET NAME, SESSIONS, CANCEL and JOIN left; CONNECT, HOST SETUP
and HOST right; LINES1, the boxes' 610x345 lines, stretched at column 300,
which only the two links cross; the address lines with SESSIONS), the
lobby (0x495f90: GAME SETUP left, SCORES and CHAT in the middle, PLAYER
SETUP right; along the bottom CANCEL and LOCK OPTIONS left, MECHLAB in the
middle, I AM READY and START right) and the new team's dialog (0x49bd80,
one centred block; its backdrop, a grab of the canvas as it enters, stays
at 0 as the controls' popups'). The lobby's lines (one 640x480 overlay)
take a stretch at two columns (`at2`: the part between to the middle
group, the part past it to the right; the right column stretched first),
202 and 412, just inside the middle column's two dividers: that column's
shade and lines widen, the outer two keep the screen's edges. The
briefing in its multiplayer mode is the briefing's one block; there is no
multiplayer debriefing (the lobby's scores after a game). The lobby's two
address lines (ours, piece 9c: under CANCEL and LOCK OPTIONS at 640, where
nothing else fits) move on a wide canvas to the middle column's band under
the chat bar (y 379 and 389), clear of the bottom row. The frame's bottom
line stops under the middle column as it does at 640 (the art's: just left
of MECHLAB). A briefing draws its first frame (the constructor's,
0x00405230) only once widened (`BriefingScreen::draw_first`, called by
GameEngine::construct after widen_screen); before, that one frame showed
at 640 over the last screen.
`--click-at` in the menus is a canvas pixel now (the menus' origin is 0).

Test: `menus_wide_spread_by_their_groups_headless`, with a saved
InGameVMode of 13. With wide menus off, then on: main menu, OPTIONS, BACK.
CAMPAIGN and DIFFICULTY stay, while CREDITS (and its underline), ACCEPT and
GRAPHICS & AUDIO move 213. LINES is 213 wider. In GRAPHICS & AUDIO,
RESOLUTION stays, ACCEPT moves 213, and SHADOWS' value keeps its place
from its cycle. In CONTROLS, ACCEPT moves 213, MOUSE/KEYBD DEFAULT stays,
KEY A's rect moves 106 with the table, and LINES is 107 wider. A 640x480 picked from the
options leaves the menus at 853. Break: without the underline move and the
stretch, 2 checks fail. Without the graphics table and with the old
relative move, SHADOWS' value is 351 from its cycle instead of 245 (2 checks
fail). Without moving BOUNDS, KEY A's rect stays at 206 (1 check fails).
Last, CAMPAIGN's pilot screen: ACCEPT moves 213, the new name's bar label
and PILOTS' UP stay, the games' UP moves 213 and LINES2 is 213 wider.
Without the arrows' move the games' UP stays at 382 (1 check fails). In
CONTROLS, the load/save popup (no table, one centred block) has its name
bar moved 106 and its label 10,6 from it; moved by the bar's own set_pos,
the label is 260,122 from it (1 check fails). Then a pilot stepped on to
c1m2 is accepted: the briefing's map is 853 wide, LAUNCH moves 106, a map
line's points 106, and a waypoint sits 106 right of its nav point's
projection. Without the points' move or the waypoint's shift, both checks
fail. The mission launched and won, the debriefing's circuit board is 853
wide, CONTINUE moves 213, ABORT and the cards stay, and the totals' line
moves 106 at both ends; without the end's move it is 331..414 (1 check
fails). On the main menu's second visit the Instant Action screen (made at
the start) is checked: ACCEPT and the worlds' cycle 213 right, an ally
list stays, ALLY_TITLE 213 wider; without the pulled rects' rule the
worlds stay at 420 (1 check fails). A message box made there has its OK
and its TITLE 106 right; with the primitive's place read without the
origin, the TITLE stays at 230 (1 check fails). First of all the credits
are opened: their lines 106 wider at 0, BACK 106 right (without the table,
the lines move instead: 1 check fails). Before the lab the briefing's
salvage screen is opened: its board 853 wide, ACCEPT 106 right. Before LAUNCH the mech lab is visited: its left panel 106 wider at
0, the right 107 wider, ACCEPT 106 right, the 3D window at (210 + 106) x
render / canvas; without the panels' and the window's changes, 3 checks
fail.
