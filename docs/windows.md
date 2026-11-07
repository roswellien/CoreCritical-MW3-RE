# Building and running on Windows

The same code as on the Mac, built with clang-cl (the Mac's compiler family,
so the floats round alike: `-ffp-contract=off` under both) and Ninja against
Visual Studio's headers and libraries.

## What it needs

- Visual Studio (2022 or later) with the C++ tools; its Windows SDK brings
  `dxc`, which compiles the Direct3D 12 shaders.
- LLVM's clang-cl on the PATH (`scoop install llvm`, or Visual Studio's
  "C++ Clang tools" component).
- SDL3 and SDL3_ttf: their `-VC.zip` development releases unpacked into
  `deps\` (git-ignored), e.g. `deps\SDL3-3.4.12`, `deps\SDL3_ttf-3.2.2`.
- Optional, for .ogg, .flac and .mp3 music (without it the music plays
  `.wav` tracks only): SDL3_sound's `SDL3_sound-devel-3.2.0-VC.zip`
  (github.com/icculus/SDL_sound, releases) unpacked into `deps\` as
  `deps\SDL3_sound-3.2.0`; build.cmd hands it to CMake as
  `SDL3_sound_ROOT`, which an existing `build-win\` also picks up on its
  next configure. Its decoders are built in (stb_vorbis, dr_flac, dr_mp3).
- For the movies' importer only: our minimal LGPL 2.1 FFmpeg 8.1.2, the
  same configure line as the Mac's and Linux's (docs/deps.md), cross-built
  on a Mac (or Linux) with mingw-w64: `tools/deps/build_deps.sh win`, then
  `build-deps/win/prefix` copied to the PC as
  `deps\ffmpeg-8.1.2-win64-lgpl-shared` (its `include\`, `lib\*.lib`,
  `bin\*.dll` and licence files; CMake takes the first
  `deps\ffmpeg-*shared*`, or `-DFFMPEG_DIR`). Four DLLs that load only
  Windows' own. Without it `mw3import` is not built; the game is. (Until
  2026-11-02 this was BtbN's "latest" LGPL build: not pinned, LGPL v3,
  some fifty libraries built in, zvbi's GPL-2 files among them; not used
  any more.)

## Building

`tools\windows\build.cmd` finds Visual Studio (vswhere), configures
`build-win\` once and builds `mw3` (or the targets given). The build copies
the DLLs mw3.exe links (`SDL3.dll`, `SDL3_ttf.dll`, `SDL3_sound.dll`) next
to it. `mw3soundcheck --music FILE` plays a music file through the game's
music player for 3 s and says whether this build decodes it.
`tools\windows\build.cmd mw3 mw3import` also builds the importer, and its
build copies FFmpeg's DLLs next to `mw3import.exe`; give a player the two
exes and all the DLLs in one folder (FFmpeg's are four). On Owen's PC
(2026-10-30): the 19 movies in 29 s; their `.wav` and `.fmv` files are
the Mac's byte for byte, the `.m1v` differ by FFmpeg build but play either
way; `--cue` on his MW3 disc image gives track02 (191.9 s) and track03
(186.1 s).

## Core Critical Vulkan development backend

Configure the Windows x64 build with `-DMW3_CORE_CRITICAL_VULKAN=ON`,
Vulkan headers, and `glslangValidator` in addition to the usual SDL and
Direct3D 12 build tools. CMake accepts `MW3_VULKAN_HEADERS` as the include
directory containing `vulkan/vulkan_core.h` and `GLSLANG` as the path to the
shader compiler. It compiles the renderer's GLSL at build time.

Build `mw3` and `mw3ccvulkan_checks`. Run the asset-free fixture with
`ctest --test-dir build-win -R '^mw3ccvulkan(_wsi)?_checks$' --output-on-failure`.
Set `MW34_VK_VALIDATION=1` and point `VK_LAYER_PATH` at the installed
Khronos validation layer when checking a development build.

```powershell
& .\build-win\mw3.exe --cc-vulkan --window 960x720 --install $game --assets $fonts --write-dir $trial --ready-file "$trial/ready" --original --no-mods --no-import --no-vfw-helper -nofmv
```

Use a verified retail 1.2 install for `$game`, the Windows Fonts directory
for `$fonts`, and a fresh local directory for `$trial`. The windowed trial
renders with Vulkan and presents through the same GPU's DXGI swapchain.
It requires Vulkan 1.1 and a device that supports Win32 external memory and
keyed mutex sharing with DXGI.

Replace `--cc-vulkan` with `--cc-vulkan-wsi` to use an SDL-created Vulkan
surface and a native Vulkan swapchain. This route selects a graphics queue
that can present to that surface before creating scene resources. It requires
Vulkan 1.1 and surface/swapchain support, without DXGI interop. A failure reports
the surface or device error. The two Core Critical routes are explicit choices;
omitting both switches selects the upstream renderer. Both trials require a
window and bypass the launcher. Fullscreen toggling is disabled during the trial.

The native WSI fixture checks scene operations, hidden/minimized readiness,
restore, and repeated swapchain resizing on a desktop with a Vulkan device.

Add `--cc-aa fxaa --cc-af 16` to enable world FXAA and 16x anisotropic
filtering. `--cc-aa` accepts `off` (default), `fxaa`, `ssaa2` (a 2x1 sample
grid) and `ssaa4` (2x2). `--cc-af` accepts `1` (default/off), `2`, `4`, `8`
and `16`. These are process-start options for either Core Critical presenter;
they require a restart to change. The upstream backend rejects enhanced requests.
Unsupported values or GPU capabilities fail visibly.

FXAA runs on world color before sharp HUD composition. AF uses generated mip
levels and anisotropic world samplers; HUD sampling stays at level zero.
SSAA resolves to the current SDL output dimensions, including readbacks and
held frames. Resize recreates the target at its new extent. Run the quality
fixtures with `ctest --test-dir build-win -R '^mw3ccvulkan_quality_' --output-on-failure`.
Run the upstream comparison with `SDL_GPU_DRIVER=direct3d12`, a separate
profile, and `--no-launcher` in place of the Core Critical switch.

Prepared visuals can be selected explicitly, using local Core Critical mesh and
albedo components:

```powershell
& .\build-win\mw3.exe --cc-vulkan-wsi --window 960x720 --cc-model madcat 0 $meshLod0 $texturesLod0 --cc-model madcat 1 $meshLod1 $texturesLod1 --cc-aa fxaa --cc-af 16 --install $game --write-dir $trial --no-launcher
```

`--cc-model CHASSIS LOD MESH BWTEX1` is repeatable. It binds the named native
library entry (`mech_<chassis>.flt` or `mech_<chassis>_1.flt`) to a validated
BWMESH1–6/BWTEX1 pair. Rigid batches use immutable Vulkan geometry and current
native controls; donor rigs retain Core Critical's visual retargeting solver.
Mixed-bone seams and near/far-plane crossings use the adapter's CPU deformation
and clipping path. Original model production, animation and gameplay continue;
only the selected visual geometry is replaced. Optional `--cc-textures BWTEX1`
replaces exact named native albedos, retaining the native alpha mask on reload.
Invalid files, duplicate selections and incompatible control bindings fail with
an error. No asset payload is bundled with the executable or this repository.

This is component admission, not admission of a complete installed pack. BWSFM1
surfaces, skin selection and pack capability negotiation are still pending.
Morphing, non-unit node opacity, overlay, detached and changed source meshes
retain their native geometry; those damage/cockpit paths need further parity
validation. With lighting off, prepared geometry retains the existing directional
visual shade and native fog samples. Per-pixel lighting and normal maps are opt-in below.

Run asset-free reader and GPU fixtures with
`ctest --test-dir build-win -R '^(mw3prepared_checks|mw3ccvulkan_prepared_)' --output-on-failure`.
The reader fixture also builds without SDL on Linux and macOS. The Core Critical
GPU adapter remains Windows-only; these portable readers do not establish a
Linux, macOS or Android renderer port.

Append `--cc-lighting normal --cc-model-normals madcat 0 $normalsLod0
--cc-model-normals madcat 1 $normalsLod1` to the prepared-model command to use
the existing BWNRM2 sidecars. Normal selections are repeatable and must match a
selected chassis/LOD. Their batch counts, content hashes and albedo material names
must match; `UINT32_MAX` is an explicit absent-map binding. The reader accepts
BWNRM2 linear RGBA8 tangent normals, preserves authored alpha and rejects other
normal formats. Its FNV content key checks payload consistency, not publisher identity.

`--cc-lighting` accepts `off` (default, existing visual shade), `flat` (per-pixel
directional lighting without normal perturbation), and `normal` (the same lighting
with selected normal maps). Normal mode requires a normal selection; explicitly
absent bindings retain geometric lighting. Selected files are validated even when
lighting is off. Configuration belongs to each renderer and survives scene reload.

`--cc-environment-pack DIR` selects an already extracted schema-v2
`prepared-environment-v1` pack. Repeat the option for different regions; duplicate
region ownership fails rather than depending on argument order. The runtime checks
member paths, sizes and SHA-256 pins before routing native mission/common texture
banks and authored/world normal catalogs. Explicit normal catalogs take precedence,
then operation1..4, training and shared-mechs. World pages remain lazy with a 512 MiB
decoded resident budget. Missing or corrupt selected files fail visibly.
Selected banks require direct 16-bit texels; global and local indexed textures
are rejected because the native uploader does not support them. Texture edges
must not exceed 4096 pixels and the aspect ratio must not exceed 8:1.

Pack selection does not enable prepared model replacement or change the lighting
mode. Use `--cc-lighting normal` to apply admitted normal maps. Lighting, effects,
detail and surface-material metadata are retained for later integration; this option
does not activate their rendering stages. Omit all pack options to restore stock
bank routing. Pack admission rechecks file content; there is no live file watcher
or automatic archive installation.

`--cc-sun AZIMUTH ELEVATION` sets angles in degrees: azimuth 0 points toward world
+X, 90 toward +Z, and positive elevation toward +Y. Supported ranges are -360..360
and 1..89. The default is 45/35. This affects selected prepared models; native world,
cockpit and HUD lighting remain on their existing paths. It does not enable sky,
weather, local lights or shadow maps.

The lit path keeps true world positions separate from inverse-transpose normals.
CPU-clipped and donor-deformed batches retain smooth normals in a transient Vulkan
stream. Mirrored UVs retain handedness; singular UV charts and zero tangent vectors
retain geometric lighting. Native fog and sharp HUD composition are preserved.

Run the material GPU checks with
`ctest --test-dir build-win -R '^mw3ccvulkan_material_' --output-on-failure`.
The checks cover both presenters, FXAA/AF and SSAA, neutral/absent/invalid tangent
vectors, singular/mirrored UVs, nonuniform transforms, depth clipping, native fog,
HUD composition, instance isolation and resource reload. Per-pixel materials need
further model/damage/LOD calibration; this is not full pack or release validation.

The adapter and renderer sources derive from the Core Critical project.
Their MIT notice is in [src/frontend/core_critical/LICENSE](../src/frontend/core_critical/LICENSE).

## The Indeo helper

`tools\windows\build_indeo.cmd` builds `build-win\mw3indeo.exe`, the
32-bit helper that plays the movies through the install's own Indeo 5
codec (fmv.md, The Indeo helper): `clang-cl -m32` under `vcvarsall x86`,
one file, Windows' own libraries, the CRT linked in, a GUI program (no
console when mw3 starts it). With it beside `mw3.exe` the movies need no
FFmpeg and no import. Its check, from the repo, with the install at
`C:\Games\MW3` (any 1.0 or 1.2 install; the reference frame from any
full `ffmpeg` with the Indeo 5 decoder, as the package's DLLs come
without `ffmpeg.exe`):

    ffmpeg -i C:\Games\MW3\video\C1M1.AVI -vf "fps=15,select=eq(n\,100)" -frames:v 1 -f rawvideo -pix_fmt rgb565le ref100.565
    start /wait "" build-win\mw3indeo.exe --check C:\Games\MW3\Ir50_32.dll C:\Games\MW3\video\C1M1.AVI 100 c1m1-100 ref100.565 > check.txt

`check.txt` says whether the codec loaded and ICOpen gave its driver (its
name and description), the movie's size and frames, every frame decoded in
order (key frames and the frames between, as the player decodes them) with
the time a frame, frame 100 written as `c1m1-100.bmp` and `c1m1-100.565`,
its PSNR against FFmpeg's decode of the same frame (the optional last
argument; `fps=15` keeps FFmpeg's frame n the AVI's sample n, repeating the
picture for the packets Indeo gives none for, 105 of C1M1's 690;
FFmpeg's Indeo 5 is its own decoder, so close, not equal), and
which `ir50*`, AVIFile and VfW DLLs the process loaded.

## Packaging

`tools\windows\package.cmd` makes the package for other players:
`build-win\dist\mw3-<git describe>-win64\` and its `.zip`. It configures
its own tree, `build-win-pkg\`, from scratch with `-DMW3_PACKAGE=ON` (mw3 and
mw3import then carry no development copy of the game: only the player's
install, through the finder or mw3.cfg's `install=`, which `mw3import`
reads too), builds `mw3` and `mw3import`, then the 32-bit Indeo helper
(`build_indeo.cmd build-win-pkg`, in a child shell for its x86 vcvars), and
collects:

- `mw3.exe`, `mw3indeo.exe` (the movies through the install's own codec;
  mw3 starts it when it is beside it), `mw3import.exe` (the fall-back:
  the movies converted when the helper cannot play them, and a disc
  image's music) and the DLLs their builds copied beside them
  (SDL3, SDL3_ttf, SDL3_sound, FFmpeg's nine), plus the Visual C++
  runtime from Visual Studio's redist folder (app-local);
- `README.txt` and `THIRD-PARTY.txt` (`tools\windows\dist\`), each
  `deps\*` folder's own `LICENSE*`/`COPYING*` files under `licenses\`,
  pl_mpeg's, miniz's, and `licenses\ffmpeg-configuration.txt` (`mw3import
  --version`: FFmpeg's version and configure line, naming the libraries
  built into its DLLs).

**The installer**: then `MW3-Rebuild-Setup-<git describe>.exe` beside the
zip, made by Inno Setup's compiler from `tools\windows\installer.iss`
(`iscc /DAppVersion=... /DDist=<the folder> /O<out>`). It installs the
package folder to `Program Files\MechWarrior 3 Rebuild` (or, for a player
without administrator rights, "only for me" in the user's Programs folder),
with a Start-menu shortcut to the launcher, mw3launcher.exe ("MechWarrior
3"), one straight to the game, mw3.exe ("MechWarrior 3 (no launcher)"), one
to the read-me, a desktop shortcut to the launcher, "MechWarrior 3" (ticked
by default), the rebuild's `LICENSE.txt` as its licence page, and
an uninstaller. The game writes nothing in that folder, so the uninstaller
leaves `%APPDATA%\MicroProse\MechWarrior 3` (settings, pilots, saves, the
imported movies and music, a disc install) and says so. The same AppId
every version: a new installer upgrades in place. Without Inno Setup
`package.cmd` still makes the folder and zip and says no installer was
made. Inno Setup without administrator rights, through scoop (its extras
bucket unpacks it into the user's folder, `iscc` on the PATH):

    scoop bucket add extras
    scoop install extras/inno-setup
    iscc /?

**The package exe is a GUI program**: `-DMW3_PACKAGE=ON` on Windows links
`mw3.exe` for the Windows subsystem (`/SUBSYSTEM:WINDOWS
/ENTRY:mainCRTStartup`, so `main` stays the entry), and no console window
opens beside the game. With no console, its output goes to `mw3.log` in
the write dir, the start before's kept as `mw3.old.log`
(`sdlfe::log_to_file`), and the helpers it starts (mw3import, the Video for
Windows helper; console programs) start without a window of their own
(`sdlfe::start_child`: SDL's background process, `CREATE_NO_WINDOW`). The
development build stays a console program. `mw3import.exe` stays one too,
for `mw3import --cue` from a prompt.

The package's FFmpeg is FFmpeg's own code only (LGPL v2.1+, no other
library inside), so its notice is FFmpeg's licence and configure line
(`licenses\ffmpeg-configuration.txt`, from `mw3import --version`).

`check_dist.ps1` then checks with `dumpbin /dependents` that every DLL the
exes and DLLs import is in the folder or Windows' own (System32 or an
`api-ms-win-*` set; the VC++ runtime must be in the folder), and that no game
file is in it. `mw3.exe` has a version resource (ProductName "MechWarrior 3
rebuild", the version from `git describe`, the commit count as the build
number; `tools\windows\version.rc.in`) and our placeholder icon
(`tools\windows\mw3.ico`, a green reticle drawn by `make_icon.py`;
nothing of the game's or MicroProse's). `mw3 --version` and `mw3import
--version` print the version and need no window, so they show over ssh
that the programs and every DLL load.


- The install: `mw3.exe C:\path\to\MechWarrior3`, or an `install=` line in
  `mw3.cfg` in the write dir (`%APPDATA%\MicroProse\MechWarrior 3\`).
- Any version's data runs (assets.md, Data versions); `mw3` prints which at
  start ("data version 1.2", "1.0", "1.0 with 1.2 files" with the counts, or
  the files that are neither). An install with the 1.2 `Mech3.exe` copied
  over older data (seen on one PC) is older data: 1.0's `reader.zbd` lacks
  hud.zrd's 1152x864, 1280x1024 and 1600x1200 blocks (it has 800x600 and
  1024x768), and its `interp.zbd` and campaign 3's missions differ.
- The movies: through `mw3indeo.exe` beside `mw3.exe` and the install's
  Ir50_32.dll (The Indeo helper), with nothing imported. Without the helper
  (or when it fails), on a player's first plain start, when the write dir's
  `video\` lacks the install's movies and `mw3import.exe` is beside
  `mw3.exe`, mw3 runs it for the missing ones with a small progress window
  (about a minute for the 19; fmv.md section 6). Without the importer the
  game plays without movies, as before; `--import` runs it again after a
  failed try, `--no-import` skips it. A set made on another machine can be
  copied into `video\` instead.
- The music: `mw3import --cue "D:\path\disc.cue" --music-only` writes a
  disc image's audio tracks as `music\trackNN.wav` in the write dir
  (fmv.md section 6, Music).
- Fullscreen by default (borderless, the desktop's size, the frame fitted);
  `--windowed`, `--window WxH` or `windowed=1` in `mw3.cfg` keep a window.
- It must run in a desktop session: over ssh there is no swapchain
  (`SDL_ClaimWindowForGPUDevice` fails with 0x887A0022).

## A crash

Windows' event log (Application, event 1000) gives the exception code and
the fault offset in mw3.exe. The build keeps symbols (mw3.pdb beside the
exe), so `llvm-symbolizer --obj=build-win\mw3.exe --inlining
--relative-address 0x<offset>` names the function and line.

## The renderer

`sdl_render3d.hlsl` is the Direct3D 12 twin of `sdl_render3d.cpp`'s Metal
shaders; CMake runs `dxc` on it for each entry point and the renderer takes
DXIL when the device offers it. The viewer (`tools/viewer`) is Metal only.

`sdl_render3d.glsl` is the Vulkan twin (Linux, and anywhere
`glslangValidator` is found): the same maths in GLSL 450, compiled per
stage (`-DVS`, `-DFS_TEX`, `-DFS_FLAT`) to SPIR-V headers in
`build/spirv/`, with SDL GPU's Vulkan sets (vertex uniforms 1, fragment
combined image-samplers 2, fragment uniforms 3). GLSL rather than the HLSL
through glslang because SDL's Vulkan backend wants combined image-samplers,
which HLSL's separate texture and sampler do not give. The renderer takes
DXIL, then SPIR-V, then MSL, whichever the device accepts. Checked on the
Mac through MoltenVK (`brew install molten-vk`;
`SDL_GPU_DRIVER=vulkan SDL_VULKAN_LIBRARY=/opt/homebrew/lib/libMoltenVK.dylib`):
c1m1's frame 60 identical to Metal's, every pixel.

## Not yet built on Windows

The tests (`mw3tests`, `mw3sdltests`; several use POSIX process calls,
guarded), `mw3import` (FFmpeg), the viewer's shaders.
