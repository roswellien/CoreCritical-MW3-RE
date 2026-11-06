# Ultrawide preview validation, 2026-11-05

Issue [#22](https://github.com/roswellien/CoreCritical-MW3-RE/issues/22) removes
the native custom-mode 16:9 crop and adds selectable ultrawide presets. The
execution boundary advanced is full-width native menu -> Training rendering,
including camera projection, the logical HUD canvas and pointer mapping. This
build extends the GPU/render-worker preview in
[PR #21](https://github.com/roswellien/CoreCritical-MW3-RE/pull/21); it is not a
merged or published release. See [usage and bounds](ultrawide.md).

## Source and environment

The task branch is `roswellien/feature/22-ultrawide`, based on current `develop`
at `87378429ff6736aae9f98b29dce4a12bdc3b7bd0` and the unmerged renderer preview
through `69e6c276a00bc64c9316956855432647a661df83`.

Local evidence is under
`D:\Projects\local\corecritical-mw3-re-ultrawide-20261105`. Original game files,
prepared packs, capture images and executables stay local. The build uses
Windows, clang-cl 21.1.8, CMake/Ninja Release, SDL 3.4.12, Vulkan headers 1.4.321
and the AMD Radeon RX 9070. The physical desktop is 3440x1440. A 5120x1440
render/readback on this host does not qualify physical 32:9 monitor scanout.

`MW3_GAME_DIR` was configured to the verified local retail installation, so
original-data tests below actually execute instead of skipping for absent
media. This differs from the asset-free CI fixtures. No original executable or
interpreter fallback is used to run the native application.

## Automated checks

- Final Release build passed for the game, native/SDL tests, prepared geometry,
  staging, render worker, HUD and Vulkan fixture targets (`build-final.log`).
- 30/30 selected CTests passed (`final-ctest.log`): native core with the scoped
  filter below, prepared geometry/staging, render-worker ownership, HUD, and
  Vulkan DXGI/WSI geometry, quality, material, worker and ultrawide fixtures.
  Vulkan validation was enabled for the authored graphics fixtures.
  The scoped native core run contains 17 tests with zero failed checks.
- SDL `sdl_window_shape_maps_the_mouse_through_the_frame`: 1 test, 0 failed
  checks (`final-sdl-input.log`).
- HUD checks with Windows fonts passed: payload/identity/order/ownership,
  clipping, native shadow composition and floating-point state
  (`final-hud-fonts.log`).
- Changed-line clang-format verification passed for seven changed/new C++
  sources; `git diff --check` passed. This public native fork does not contain
  the private project's `tools/check.py` or `tools/style/check_style.py`; those
  commands were not claimed to run. Full unrelated core/SDL suites were not
  run as part of this task.

The native core selection is:

```text
MW3_TEST=msvc_rand,msvc_ftol,ztimer_step_follows_the_clock,resolution_,settings_first_start_plays_at_the_desktop_size,menus_wide_follow_the_selected_aspect,menus_ultrawide_keep_native_controls_and_lab_aligned_headless,mission_c1m1_mode_change_keeps_the_camera_shape_headless
```

Coverage includes preserved original mode IDs, new preset labels/ranges,
int32/custom-size bounds, first-start desktop sizing, horizontal camera scale,
70 original HUD anchor positions, frame/canvas pointer round trips, native menu
groups/controls/pilot lists/Mech Lab at 3440 and 5120, and live mission-object
mode changes 5 -> 13 -> 18 -> 20 -> 16 -> 5. The headless menu/mission tests
exercise the native application through a host fixture; they are distinct from
the native graphics captures below. The four authored Vulkan ultrawide cases
check full-width geometry and textured HUD/canvas mapping, held-frame identity,
incompatible extent rejection, resize/reset and visible presentation, with
DXGI/WSI and render worker off/on. Their physical windows are one tenth of the
native dimensions to keep the asset-free contract checks inexpensive.

### Tests observed failing

Two reversible local fault trials rebuilt the actual production code and
restored exact original source bytes before the final build:

- Reinstating the old custom 16:9 cap produced 28 failed checks across the
  custom-size, bounds, HUD/pointer, first-start, native menu and mission-mode
  controls (`fault-checks.log`, `fault-check.json`).
- Removing the new presets, limiting the horizontal camera to 16:9 and
  suppressing custom wide menus produced 15 failed checks in the four scoped
  preset/camera/canvas/menu controls (`fault-mapping-core.log`). Cropping the
  Vulkan scene target to 16:9 made all four ultrawide CTests fail on the output
  extent (`fault-mapping-gpu.log`). Source hashes before/after restoration are
  in `fault-mapping-check.json`. No fault mutation is part of the branch.

## Live native menu and Training

Each successful capture used an isolated settings/pilot/controls directory,
custom native mode 16, matching output dimensions, fixed seed 2718 and 17 ms
simulation steps. The game navigated its native pilot/menu/briefing flow into
Training, then powered the mech with the actual configured command binding.
The Vulkan configuration was FXAA, AF16, normal lighting, Core Critical HUD,
prepared model/texture/normal bindings, `--cc-geometry partial` and
`--cc-render-thread 1`. `run.json` preserves arguments, executable/input hashes,
component bindings and process checks; `mw3.log` preserves runtime counters.

| Local run under `runs/` | Frame | Result |
| --- | --- | --- |
| `core-gpu-thread-2560-hud-r10` | 1800 | 2560x1440 external-camera control, powered HUD |
| `core-gpu-thread-3440-hud-r8` | 1800 | 3440x1440 full-width world and powered HUD |
| `core-gpu-thread-5120-hud-r9` | 1800 | 5120x1440 full-width world and powered HUD |
| `core-gpu-thread-5120-firstperson-r13` | 1800 | 5120x1440 cockpit and powered HUD |
| `core-gpu-thread-3440-firstperson-visible-r14` | 1..1800 | Final rebuilt binary, 3440x1440 native menu/Training with resident presentation |

All five exited 0 with no other native/build process detected during their
sampled process checks. Images show a centered reticle/compass, fixed-size HUD
elements and edge-anchored heat/coolant, speed and MFD groups. The cockpit trial
uses the existing profile's `GameOptions` camera bits; the legacy `CameraMode`
setting only initializes that field on a new profile. Earlier `firstperson`
trial names alone are not evidence of a cockpit view. Deferred/aborted runs are
excluded.

The 3440 HUD run completed 8,410/8,410 render-worker jobs and processed
233,798,076 prepared GPU vertices. The 5120 cockpit run completed 12,497/12,497
jobs and processed 141,459,108 prepared GPU vertices; both recorded zero prepared
CPU fallback batches. This remains the inherited partial geometry path:
unsupported models and other native CPU rendering work still exist. These
correctness captures are not a performance benchmark. Screenshot mode performs
readback and records zero resident presents. The separate final 3440 native
visible run recorded 1,798 resident presents and completed 14,286/14,286 worker
jobs, with 141,322,707 prepared GPU vertices and zero prepared CPU fallback
batches. This establishes native windowed presentation through the menu and
Training flow in addition to the authored Vulkan fixtures; its startup/menu
timings are not the standard benchmark.

### Same-height world comparison

The external-camera runs use identical state and input. Compare the 16:9 world
crop x=450..1999, y=150..1049 with the same-height center crop shifted horizontally
by `(width - 2560) / 2` in the wider images. This omits the side HUD groups and
top compass. `capture-comparison.json` records:

| Output | Exact RGB pixels in crop | Mean absolute channel error (0..255) |
| --- | --- | --- |
| 3440x1440 | 99.6831% | 0.01230 |
| 5120x1440 | 99.3938% | 0.01219 |

The nearly identical central world area, plus additional side coverage, supports
horizontal expansion with stable object scale. It does not claim pixel identity
for the whole frame or the cockpit trial.

SHA-256 of each local `verify.bmp`:

```text
2560-hud-r10: b0ffa3b308467055726509700ca6c99d63f5d043b64e0a5e7124a51f56ff9883
3440-hud-r8:  74106adc98d9751a0b2831888315df7dac31aa2995f044f2624c602df522e21e
5120-hud-r9:  0aa6d41b8ffb4ad06372983b63cbf692860e2faca9efd2877a60164eca443b9a
5120-cpit-r13: fd5e12de980083543ce202aa28aa2b1ab57e796f9fa716af213b509885a6c6ce
```

## Remaining limits and release impact

The local native logs retain existing missing optional dialog bindings and
model support/collision/destroy-animation diagnostics; exit 0 is not a claim of
an error-free whole game. Menus cache their layout at startup, so restart after
changing resolution to rebuild their width; gameplay camera/HUD changes apply
immediately. Fixed 4:3/16:9 selections still fit inside wider windows. Extremely
wide/portrait invalid settings can hit the historical 640x480 floor.

Fullscreen startup, physical 32:9/HDR/high-DPI hardware, full campaign and
Linux/macOS/Android graphics execution are not qualified here. Portable CI
checks cover the upstream core, not a port of the Windows Vulkan adapter.
Existing native menu/HUD narrow-mode controls remain available.

Saved IDs 3..16 remain stable; new preset IDs 17..20 require this or a newer
runtime. No launcher, installer, update package, asset distribution, release
version or default renderer/thread selection changes. Merge/release remain
separate owner decisions.
