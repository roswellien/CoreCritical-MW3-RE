# The program: mw3

`mw3` is the rebuild as a program: the original's own start-up and main loop
(startup.md, game-loop.md) running on SDL3. Everything the start-up reaches
that is ported runs; everything that is not is a hook that does nothing,
logged once with the original's address. It is the place the remaining
modules plug into as they are ported. `mw3view` stays the development viewer.

## Building and starting

Needs SDL3 and SDL3_ttf (`brew install sdl3 sdl3_ttf`); SDL3_sound
(`brew install sdl3_sound`) adds .ogg, .flac and .mp3 music (without it,
.wav only). CMake builds `build/mw3` when SDL3_ttf is found.

```
build/mw3 [install-dir] [options] [-nofmv] [-seasick] [-cdN]
```

- **The install**: the folder holding `zbd/` (MW3 1.2; `Mech3.exe` is not
  needed, the port never runs it), as a path argument or `--install DIR`
  (used as given). `--install-from PATH` (ours, `mech3/disc_install`)
  first installs one from the disc, a disc image or a .cue/.bin into
  `--install-to DIR` (default `<write dir>/install`), with the 1.2 patch
  when `--patch` is given and the cue's music, and saves it as `install=`
  (assets.md, Installing from the disc). Without one (ours;
  `sdl_install.cpp`, FIX (A)):
  1. `install=DIR` in `mw3.cfg` in the write dir, while it still holds
     `zbd/reader.zbd`;
  2. else where the original's installer put it. Its InstallShield script
     (the disc's SETUP.INS and value.shl) wrote
     `HKLM\SOFTWARE\MicroProse\MechWarrior 3\1.0` with `Program` and
     `Version`, and the App Paths entry for `mech3.exe` (the default value and
     `Path`). These are read in the 32-bit and 64-bit views and the user's
     VirtualStore copy. Then its default folder,
     `PROGRAMFILES\MicroProse\MechWarrior 3` (Program Files (x86) on 64-bit
     Windows), a few usual folders on the system drive (`\MicroProse\`,
     `\Games\` and the root, as `MechWarrior 3`, `MechWarrior3` or `MW3`),
     the program's own folder and the one above it (on the Mac, beside the
     .app). A development build then tries the repo's `game/mw3-1.2`, unless
     `MW3_FIND_INSTALL=1` is set; a package build (`MW3_PACKAGE`,
     windows.md, Packaging) has no such copy;
  3. else a message box ("Find your MechWarrior 3 installation": Browse...
     or Quit) and an open-file dialog for `Mech3.exe` (or All files). The
     picked file's folder is the install (a file in `zbd/` or `video/`: the
     folder above it). A folder without `zbd/reader.zbd` gets a message
     saying what is missing (`zbd\`, and `video\` when that is gone too),
     and the dialog opens again. Quit or Cancel ends the program.
  An install found by the search or the dialog is saved as `install=` in
  `mw3.cfg`, with its other lines kept. Check runs (`--hidden`, `--frames`,
  `--shot`, `--campaign-chain`) never ask: with none found they quit with a
  report. Tests: mw3sdltests `sdl_install_is_found_and_saved`,
  `sdl_program_check_runs_never_ask_for_the_install`.
  The install is mounted (assets-layer.md); `--assets DIR` (or `assets=DIR`
  lines) mounts folders of replacement files in front of it.
- **The write dir**: saves and settings, never the install. Default
  `SDL_GetPrefPath("MicroProse", "MechWarrior 3")` (on macOS
  `~/Library/Application Support/MicroProse/MechWarrior 3/`), or
  `--write-dir DIR`. It is searched before the mounts, so what the game wrote
  wins.
- **The log** (ours, `run_log.cpp`): from the moment the write dir is
  known, everything on stdout and stderr also goes to `<write dir>/mw3.log`
  (first line: the build's version and the log's path), made new at each
  start; the previous start's is renamed `mw3.old.log`, so a crash's log
  survives the restart a player makes before reporting it. Each stream
  goes through a pipe and a thread that writes the console's own handle and
  the file; at exit the handles are put back and the threads drained.
- **The data folder** (ours, `mech3/data_folder.cpp`, `data_folder_start.h`;
  Owen, 2026-11-04): the write dir on another drive, chosen in the launcher (GAME
  FILES, MY DATA FOLDER). `data-folder.txt` in the usual folder names it, and
  `mw3-data-folder.txt` in the folder says it is ours; with no pointer nothing
  changes. mw3, mw3launcher and mw3import all read it, and `--write-dir` wins (and
  hides the setting); the launcher passes the folder on to the game. A pointer's
  folder that is missing, unmarked, read-only or full is said, with Try again and
  Quit (hidden runs and mw3import exit 1), and no fresh folder ever stands in.
  A move: the plan refuses a target inside or around the folder, one holding
  files (a picked folder that is not empty and not ours gets a `MechWarrior 3
  Data` folder inside it), or short of room; then each file is copied and read
  back, `mw3.cfg`'s paths under the old folder follow (a disc install lives in
  `<folder>/install`), and the pointer switches last, by rename. Failure or
  cancel removes only what that run made; cut off outright, the pointer still
  names the old folder, and `mw3-data-copying.txt` lets a new move finish the half
  copy. The old copy goes only on the player's yes, and only in that session:
  the files that move copied, one by one (never `remove_all`), when the pointer
  names the new folder and it has every one; folders only when that empties
  them. Logs stay behind: they are in use, and each start makes new ones.
  Anything in the folder it cannot read stops the move before the switch.
  The top level's `.DS_Store`, `Thumbs.db` and `desktop.ini` count as not data,
  neither copied nor removed. On Windows a destination's name must be plain ASCII for
  now: the dialog gives UTF-8, and `fs::path` reads narrow strings in the ANSI code page
  (installs have the same limit).
  `MW3_USUAL_FOLDER`, `MW3_LAUNCHER_MOVE_TO`, `MW3_LAUNCHER_MOVE_OLD`: for checks.
- **A crash says why** (ours, `crash_report.cpp`; not in hidden runs): a
  crash signal, a Windows exception or an uncaught exception puts a line in
  the log (`mw3: crashed: ...`, with a backtrace where the system has one) and
  shows a box naming the log. A signal handler cannot use SDL, so it starts
  its own program with `--crash-box <text>` and ends; the launcher's box
  stands in where it stays for an AppImage (`docs/linux.md`, Packaging).
  A failed start (SDL, the window, the GPU device) also shows a box. A normal
  end logs `mw3: closed, result N`, so a log with neither line was killed.
- **The start's notes** (ours, `start_notes` in `mw3_main.cpp`): after the
  data version, a note when it is not 1.2's (1.0, a mix: `--patch`; files
  of neither version; files missing) and when no `music/trackNN` is found
  (`mw3import --cue ... --music-only`, or a rip), printed as
  `mw3: note: ...` each start and shown in one box on a player's first
  start (the install found by the search, not `mw3.cfg`'s; never on check
  runs).
- **The original's switches** (`-nofmv`, `-seasick`, `-cdN`) go on to the
  ported command-line parser as the exe's command line.
- `--window WxH` (default 1280x960: the game's 640x480 doubled),
  `--nosound` (a silent DirectSound stand-in), `--hidden`, `--verbose` (lists
  every unported function reached, with call counts, at exit; from the
  menus, also the player's mech 30 frames into the mission: its weapons,
  sinks, engine, speed and centre torso armour),
  `--frames N` (quit after N frames).

Plain `mw3` runs the original start: the constructor, InitInstance, the
engine start (the `zXxxInit: PASSED` lines), the Intro state (movies hooked,
so it moves on at once), then the Main state and its main menu (ported,
mainmenu.md: the background, the buttons and their ripples' state, the
logo, the hotkeys; the ripple's zRender drawing 0x00558dd0 is a hook, so no
ring shows). Its buttons reach the menus they open.

**A campaign from the menus** (campaign.md section 7, item 5): NEW CAMPAIGN
opens the pilot screen (type a name, Enter, ACCEPT); the Load state then
builds the ported briefing (brief_c11: the voice, its map, the buttons) and
loads the mission (at once: FIX (A), mission-load.md); LAUNCH (or L) builds
the pilot's mech with its own loadout and goes on to the Core state, c1
mission 1. The Load state's game is the chosen pilot's campaign (or training)
game, through the launch's stand-in (`GameEngine::load_state_enter`); its
briefing's other buttons (Mech Lab, Salvage, the reallocation list) and the
map's background picture's mission map object are hooks. `build/mw3 -nofmv`
from the repo root after a build; `--window 1280x960` is the default.
Once a mission is won, Exit Mission (Ctrl+Q) fades to the debriefing (the
ported screen: CONTINUE, REPLAY, ABORT; its results, lance cards and
salvage rows are hooks, campaign.md section 3), and CONTINUE goes on to the
next mission's briefing.

## Debug starts (our addition)

While the menus are not ported, the debug starts take the place of the state
after the intro (the app's +0x1dc, where a lobby launch puts the Load state).
They run inside the original main loop, after the original start-up.

- `mw3 --mission c1m1` (`c1` is mission 1; `c2`, `c3`, `c4`, `c4b`, `t1`;
  c1 and c2 have m1..m4, c3 m1..m6, c4 m1..m3, c4b m4..m6, t1 m1..m4; a
  mission without its `zbd\<op>\readermN.zbd` is refused;
  `--pos X Y Z`, `--look YAW PITCH` in degrees place the camera):
  **through the original's states** (mission-load.md): the pilot's game
  answers a campaign mission (type 1, the number, the directory's index), the
  Load state runs, its mission load (0x00490880 -> 0x00490ab0 -> 0x0048faf0,
  ported: the interp scripts read gamez.zbd, the textures, the view,
  animations, world.zrd) prints its progress lines, then the Core state
  (queued in place of the briefing's Launch button) runs the mission frame
  and draws the world through its Direct3D path from `camera1`. Mechs, AI,
  weapons, the HUD and the briefing screen are hooks logged once.
  `--shot`/`--frames` count the Core state's frames.
- `mw3 --fly c1` (also `c1m2`: world block m2, mission number 2; `c4b`,
  `t1`; `--variant BLOCK`, `--pos X Y Z`, `--look YAW PITCH` in degrees): a
  state of our own, the world loaded as `mw3view --real` loads it (`src/frontend/debug_world`: the
  ported gamez.zbd reader, texture groups, world.zrd parameters), drawn every
  frame by the ported render path on the SDL GPU. A free camera: W/S/A/D, Q/E
  down and up, arrows turn, Shift fast, right mouse button looks, P prints the
  camera as arguments, Escape quits. Sound: zSound's frame each frame as the
  Core frame calls it (0x0056dfb0(0, 1)), the `common` and `mech` sets,
  `mech_powerup` at the start (as the Core state's enter plays it), the
  world's sound nodes and the listener wired to the scene walk; the music
  track picked as the mission start picks it (0x00480df0: `-cdN`, else by
  mission number on a 2..5-track disc). No mission load, animations, mechs,
  AI, weapons, HUD or weather.
- `mw3 --dialog dialog.zrd MAINMENU` (any dialog of twod.md section 3): the
  dialog drawn by the ported zTwoD on the SDL host, every BUTTONS and
  PRIMITIVES entry bound by name (check buttons when they have CHECKED, texts
  when they have TEXT), the window's mouse given to Window::update (the
  buttons do nothing: their screens are not ported).
- `--vehicle-ahead M` (with `--mission`): the mission's first armed vehicle
  (else its first) placed M m in front of the player's mech on the first
  frame, at the player's height (to look at a vehicle up close: its shadow,
  its model, its weapons). `--vehicle-hold` also keeps the player where it
  stood, turned to face that vehicle each frame, to watch it fire. Placing
  the vehicle again each frame would reset its AI.
- `--turret-ahead M` (with `--mission`): the player taken M m from the
  nearest live turret on the first frame and turned to face it each frame
  (the reticle over a turret, playtest W6). With `--tap 12@150:175` (PITCH
  UP, `-`) the reticle rises onto c1m1's nearest turret at 30 m.
- `--mission`'s launch names the operation by its directory (c2 2, c3 3,
  c4 and c4b 4, else 1), as a campaign's would (the objectives box's
  "Operation N, Mission M"; playtest S2). t1 runs as a campaign mission
  (type 1), not training (no training game behind it).
- `--mech-ahead M` (with `--mission`): the player kept M to 1.5 M m from
  the nearest enemy mech in play (the chain's list), on the ground at its
  level (not a cave's roof: playtest S7), and turned to face it
  each frame, to watch a mech up close (its damage decals with `--fire`,
  damage-decals.md). The mission's opening camera still runs first.
- `--debug-camera` (ours; `debugcamera=1` in mw3.cfg): Shift+F12 opens
  The "BATTLESPORT GAME EDITOR" over the mission (debug command 0x9a,
  which the exe binds to no key; F11 stays the lancemate command screen):
  the view on the stealth object, flown with the movement keys, and the
  editor's menu; Shift+F12 again or EXIT GAME EDIT closes it and (ours)
  puts the view back on the player's mech. docs/editor.md S4.
- `--original` (ours): every play-changing bug fix off (zfix bugs), as
  `bugfixes=0` in mw3.cfg; it wins over the file (fixes.md, The bug fixes
  switch).
- `--import` / `--no-import` (ours): the movies' first-run import
  (fmv.md section 6) run even after a failed try, or skipped.
- `--vfw-helper EXE` / `--no-vfw-helper` (ours): the movies through the
  Indeo helper (fmv.md, The Indeo helper; on Windows `mw3indeo.exe` beside
  `mw3.exe` by default), or never; without it, the importer's files.
- `--patch ZIP|DIR` (ours): the official 1.2 patch, read without running
  it, into the write dir's `patch12\` overlay (assets.md, The 1.2 patch),
  mounted in front of the install on this start and every later one. A
  source without all of 1.2's files writes nothing and mw3 exits with the
  reason.
- `--mod NAME` (repeatable), `--no-mods`, `--mods` (ours, docs/modding.md):
  this run's mods in order instead of mw3.cfg's `mod=` lines, none, or the
  mods found and enabled listed (then it quits).
- `--check-mod DIR` (ours, docs/modding.md): the mod at DIR read as the game
  would read it, nothing mounted into play; the report printed, exit 1 when
  anything would be refused.
- `--shot FILE.bmp [--shot-frame N] [--shot-any]` saves frame N (default 30;
  with `--shot-any` every frame shown counts, the fade's too) of either
  and quits, with the window hidden; `--shot-series K` also the K frames
  before it.
- For checking (ours, from 2026-10-25): `--tap DIK@N[:M]` presses a key as
  an SDL event; `--click-at X,Y@N` (the plain start) clicks at a game pixel (`--click-players X,Y@N/P` N frames after the network session first holds P players, for two processes in one lobby; `--click-return X,Y@N` N frames after each return to the lobby from a game; `--net-seconds S` each menu-started network game's clock, `--net-kill-at S` a kill staged on the pilot S seconds of 30 frames into each, `--net-ahead M` the pilot held M m from the other mech there too, `--net-walk A,B` the throttle up held from A to B seconds into each; `--hover-at X,Y@N` the cursor there with no click (a rollover, W41), `--click-ingame X,Y@N`, `--tap-ingame DIK@N[:M]` and `--hold-ingame N:M` (the left button held) by each such game's frames; `--fire-probe` each game frame's buttons, trigger and weapons' reloads (a beam firing counts 100), with the wall time (W40); `--blur-ingame N:MS` the app deactivated at game frame N as a lost focus, the focus back MS ms later by a timer (W45); the soak lines' `wall` (epoch ms) and each seen mech's `heard` (ms since its last state); with `--net-soak` the shown objectives box's lines each second (`soak menus box:`); the lobby's line on coming back from the lab (`lobby back from the lab, mech N`); with `--net-soak` the "soak menus" lines each 15 frames, rss, each mech's gait slot and arms' aims included, and each lobby entry's scores);
  `--click N:M`, `--walk`, `--fire-group G`, `--objectives N` in a mission;
  `--briefing c1m2` (the plain start) saves a pilot "Briefing" whose newest
  campaign stands at that mission and clicks CAMPAIGN, the pilot and ACCEPT,
  so the Load state shows the mission's own briefing (frontend-plan.md item
  0). Its campaign is the pilot's new one stepped on mission by mission
  (0x00434e00), so the store, the slots and the field bases are a real
  campaign's (without the earlier missions' salvage). `--win N` wins the
  mission N frames in as a player does, by Exit Mission once won (the end
  screen, its fade, the debriefing's Win State and its save); with
  `--verbose` the program prints the pilot's game as the Win State saved it
  and as each briefing finds it once its mission has loaded
  (`tools/analysis/savecheck.py`, save.md), and each state the app moves to
  by name (`tools/analysis/opcheck.py`: an operation's end and the
  campaign's). `--lab-mech N` hands the lab's 3D view chassis N (0..18) once
  the lab is up, the slot keeping its mech (mechlab.md, the 3D view);
  given alone it takes `--briefing c1m2` and clicks MECHLAB (300,435)
  after frame 2500, so a shot from frame ~2900 shows the lab.

## What runs ported

The app's start-up and loop as written, checked against the exe by the app
tests (game-loop.md, Testing): the constructor 0x0047f7a0, InitInstance
0x00480000, the engine start 0x00480570, the video open 0x00480970, the
main window 0x00482d10 and its command line, `zGameApp::Run` 0x0058fd70 with
the state commands, every state (Intro, Main, Attract, Pause, Exit, Core,
Load, Pre Lab, Salvage, Win, the key list), the stop 0x004808a0 and
ExitInstance 0x0047fa10.

The quit: the Exit state runs the stop (0x00482060), then the loop's
WM_QUIT returns ExitInstance. A quit outside it (the window closed, a
debug run's `--frames`) returns through ExitInstance alone, as the exe
does, and the world stays until the process ends. mw3 once ran the stop
after that; zSound had freed its sounds by then, and the world's sound
nodes wrote into them (mission-plan.md item 2).

Hooks wired to ports by `mech3::GameEngine` (`src/mech3/app_engine`):

| Hook | Port |
|---|---|
| the interp script object 0x00410700 | `mech3::interp_create` (interp.zbd, DamageScale) |
| zReader init / archives 0x00556170, 0x00557320, 0x005573d0 | `zrdr` |
| zSound init, load, sets, play, frame, pause snapshot, reset, shutdown (0x005700f0, 0x005705e0, 0x0056f4b0, 0x0056f520, 0x0056f530, 0x0056f6f0, 0x0056e3d0, 0x0056e9f0, 0x0056dfb0, 0x0056eb10, 0x0056f0a0, 0x0056f190, 0x005716a0, 0x005715d0, 0x00570510) | `zsnd`; its engine variables SoundLOD and SoundVolume are the options' |
| the CD (CD_TRACKS from sounds.zrd, 0x00572450 / 0x005728f0) and the music calls | `zsnd_cd`, `mech3_music` |
| the clock 0x00575a00, its start 0x005759a0 | `ztimer::update`, `ztimer::init` (ported here) |
| global light 0x00526a70 / 0x00526a80, lights 0x00542bb0 | `zmod` |
| the options and their hooks (sound volume, music volume, global light, video mode) | `mech3/options`, `zsnd`, `mech3_music`, `zmod` |
| messages 0x00576040, 0x00575fb0, 0x00575fd0 | `zutl_msgdll` over Mech3Msg.dll |
| zTwoD's outside calls (`ztwod::Host`) | `SdlTwodHost` (below) |
| the settings store 0x00598210, 0x00597d80, the loader 0x0040c420, the save 0x0040cb20 and the store's free 0x00598270 (the app's destructor 0x0047fab0, `Mech3App::destroy`) | `zreg`, `mech3::load_options` (settings.md); the registry is the write dir's `settings.reg` (`platform::RegFile`) |
| the mission load 0x00490880, 0x00490ab0, 0x0048faf0 (and 0x004902a0, 0x00490630, 0x00484c50) | `mech3::Missions` (mission-load.md); its hooks in `app_mission.cpp`: zInterp, the options, zImage, zClass's and zModel's resets, node lookups, animations, world.zrd, the readers, sounds; the view's camera run is the scene walk |
| the briefing screen 0x00405230 (the Load state's) and its calls: the load's fraction and lines 0x004050d0 / 0x00405120, the script's start, its cursor 0x00409ec0 / 0x00409eb0, the voice's play, state and cue markers, LAUNCH's 0x00491150, the cleanup 0x00491780, Replace, the nav points [0x00609980]; 0x00405160's MAP image stood in for (the image only); the app's accessors 0x0040a7d0 / 0x0040a800 / 0x0040a830 and the two empty calls 0x0040baf0 / 0x004a5bc0 | `mech3::BriefingScreen` (campaign.md section 2) in `app_menus.cpp`; the pilot's game into the launch's stand-in (`load_state_enter`), its loadout the slot's |
| the save after a mission 0x00478f10 (the Win State's, the launch's) | `mech3::Pilot::record_mission` on the pilot the pilot screen chose (`program_record_mission`; a debug start's stand-in is logged); the pilot module's message box is the engine's (`wire_pilot_hooks`), zSave's world save 0x005a8a70 logged (campaign.md) |
| the sensors 0x004a8cd0 (each Core frame) and the entity sort 0x004d5ca0 (the mission start's resume 7) | `mech3/sensors` (ai.md section 15) over `app_sensors.cpp`: zEntity's list is the port's, the mechs put on it by the build's 0x005a7f50; the Core state's mission-start fade (0x0048ee70) runs the ported fade state, whose end gives resume(7) |
| the fade state's screen calls: the grab 0x00579500, the draw 0x0055b1f0, the free 0x00517cc0; the colour 0x00558540 | `GameEngine` (`app_engine.cpp`): the still is a copy of the 16-bit buffer, drawn back whole (FIX (A)); the colour is the port (`zrndr::tint`) |
| the Direct3D frame: the lock 0x00576d00 / 0x00576d60, the scene nesting 0x00579af0 / 0x00579b10, the quad [0x0072f3ec], the software finish 0x00558610 | `zvid_dd`, `zvid_ddd3d`, `zrndr_tint` (render-frame.md); the surface Lock / Unlock on the 16-bit layer and `Screen2D::overlay_16` (FIX (A)) |
| the player's view 0x0040aca0 (its update from the player's tick, the attach 0x0041ce30, the camera switch 0x0040ae70) and the mech types' cameras 0x00430e70 | `mech3/player_view` over `app_view.cpp` (view.md): camera1 follows the target's camera; the app code reads the view's mode and target from its block, copied after each change |
| zInput: the command map 0x0051c630, start and stop 0x0051b0a0 / 0x0051b170, the activate and deactivate, polls 0x0051b360, key states 0x0051a9b0, resets, the mouse's use, centre, position, acceleration and screen, joystick and force feedback 0x0051dc10 / 0x0051ddb0 | `zin` (input.md), on `SdlInput` and `SdlZinHost` |

Memory of modules not ported (the `peek`/`poke` the app code uses for
their fields) is a sparse byte map: what is poked reads back, the rest is 0.
Objects the ports hand out (zSound objects, voices, snapshots, the interp)
get 32-bit handles from a table.

Still hooked (logged once each, return 0), the main ones: the movies (zFMV
0x00598cc0..), the main menu's menu objects (0x0048a6c0 ..) and App::Push
of them (0x00590400), the pilots, mechs, maps and missions (0x004788b0,
0x0040f410, 0x004583b0 ...), the mission's modules (mission-load.md, Open), zEffect, zClass's and zModel's
module starts, mech3's controls (0x00411150, 0x004120a0), the network. `--verbose` prints the full list of a run.

## The platform underneath (FIX (A), fixes.md)

`src/platform/platform.h` names what the game needs; `src/frontend/sdl`
implements it:

- **Window** (`SdlWindow`): the `Mech3Class` frame. Made hidden before the
  engine starts so the GPU device and Render3D exist; the game's
  `CreateEx` sizes and titles it ("MechWarrior 3 1.2", message 7 and the
  version), `UpdateWindow` shows it. The game's 640x480 is stretched over the
  window. Ours: fullscreen at the desktop's size by default (`--windowed` or
  `windowed=1` in mw3.cfg for a window); Alt+Enter switches between the two
  for the run (`switch_fullscreen`; the window takes the frame's shape at its
  opening size, inside the display's usable area). The pump swallows that
  Enter and its release, so a menu's default button never sees it.
- **Message pump** (`SdlPump`): SDL's event queue for `zGameApp::Run`. Focus
  gained and lost are the frame's OnActivate (0x00590c00), so the loop idles
  while the window is in the background, as the original's did, and the
  game's sounds play on silent: no buffer asks DSBCAPS_GLOBALFOCUS (0x8000,
  0x005752e0), so DirectSound muted them away from the focus; the music
  (MCI's CD) plays on. On return zInput clears each device's state
  (0x0051b080); `SdlInput` also drops the buttons and keys held when the
  focus went, whose releases never arrive. The window's
  close posts the quit. A music file's end is MCI's notify (0x00590740).
  Ours (2026-10-31): a run that ends by itself (`--frames`, `--shot`, a
  chain or a modes run) does not pause for a lost focus (`stays_active`):
  started behind another app, or on the launcher's window handed over
  unfocused, it waited for an activation that never came (a 28-minute hang
  on the Mac, a scheduled `--shot` on the PC). The window is raised and
  given the keyboard when shown (`SDL_HINT_FORCE_RAISEWINDOW`; on the Mac
  the app is activated), and the launcher's window events are flushed at
  the hand-over. `MW3_FOCUS_LOST_AT=N` loses the focus at the pump's Nth
  pass, for `sdl_program_check_runs_on_without_focus`.
- **Clock** (`SdlClock`): `GetTickCount` counted from the start, `Sleep`.
- **Input** (`SdlInput`): DirectInput as zInput opens it (0x0051b0a0): the
  keyboard buffered as `GetDeviceData` gives it (0x80 events of
  DIDEVICEOBJECTDATA with DIK codes, overflow reported as
  DI_BUFFEROVERFLOW, auto-repeat not reported, nothing while unacquired), the
  mouse as DIMOUSESTATE (relative counts since the last read, the wheel in
  120s, buttons left, right, middle, X1), the first joystick as DIJOYSTATE2
  (axes 0..65535, DirectInput's default range; hats as POV hundredths of a
  degree, -1 centred; 128 buttons). SDL scancodes are USB usages; the DIK
  codes are the PC keyboard's set-1 scan codes, mapped key for key by
  position (`dik_from_scancode`). zInput (input.md) runs on it: the
  cooperative level (the mouse exclusive and foreground: SDL's relative mouse
  mode while acquired, so the cursor is hidden and held as DirectInput held
  it; taken away with the focus, reads then give DIERR_INPUTLOST), motion
  while captured a mickey a count, as DirectInput's raw counts were at any
  resolution (SDL's relative motion is unscaled by the system's pointer
  speed: SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE is 0), else scaled to the
  frame across the window, the joystick's
  capabilities, axis names, ranges and dead zones. `SdlZinHost` gives zInput
  its Win32 calls (the client area is the 640x480 frame, characters from
  SDL's keymap). The debug states read DIK codes directly; the dialog one
  polls zInput, so its dialog gets zInput's mouse.
- **Render3D** (`SdlRender3D`, render.md section 4) for the 3D path, and
  **Screen2D** (`SdlScreen2D`) for the menus: the 16-bit back buffer the
  2D blits draw into (locked by 0x00576de0, flipped by 0x00576ea0) is
  converted to a texture and drawn over the window by Render3D. Render3D
  keeps one back-buffer texture a frame (the window's pixels): every scene,
  2D layer and colour quad of the frame draws into it in order, the frame
  clear (0x005777f0) clears it once, and the flip shows it (or saves it for
  `--shot`). In Direct3D the back buffer's lock (0x00576d00, render-frame.md)
  hands the 2D code the 16-bit layer filled with a key colour (0xf81f), and
  its unlock lays the changed pixels over the 3D frame
  (`Screen2D::overlay_16`), so the HUD and the fade's still go on top of
  the world as on the locked DirectDraw surface.
- **2D host** (`SdlTwodHost`, twod.md section 6): GDI's text on SDL3_ttf
  with the game's own `arial.ttf`, `impact.ttf` and `lucon.ttf` ("Lucida
  Console"), found through the mounts. GDI's rules where the game can see
  them: a negative height is the character height, a positive one the cell
  height, sized as GDI does (VDMX, hinted advances, no kerning; twod.md
  section 6, Text widths); text is drawn without antialiasing (GDI on a 16-bit DIB), so the
  key-colour blit keeps clean edges; `DT_CALCRECT`, line breaks,
  `DT_WORDBREAK` in the box, `DT_CENTER` / `DT_RIGHT`, `&` prefixes; opaque
  and transparent background modes; `GetTextMetrics` (internal leading the
  cell height less the em). Text is Windows-1252, converted to UTF-8 for
  SDL_ttf. fonts.zrd's WINDOWS_FONTS are read by name for zImage's named
  fonts (face, height, colour or two, shadow, align, weight). Images come
  from the image library (`zimg::fetch_image`, refcounted by name); screen
  grabs copy the 16-bit buffer; the mouse is zInput's (0x0051b7b0).
- **Sound** (`SdlSoundOutput`, sound.md section 7), or
  `platform::SilentSoundOutput` with `--nosound` and in tests.
- **Music** (`SdlMusicOutput`): `zsnd_cd` asks for `music/trackNN.(ogg|flac|
  wav|mp3)`; `mech3::MountFiles` looks it up in the write dir, then every
  mount, so the tracks can sit in the write dir's `music/` or the install's.
  The file is decoded as it plays (SDL3_sound), gain from the CD volume.
- **Settings** (`platform::RegFile`, settings.md): the registry's two keys
  as regedit's text in the write dir's `settings.reg`, written when the app
  ends; zInput's bindings are a store entry (`SdlZinHost::settings_block`).
- **Alert** (`SdlAlert`): `MessageBoxA` and the game's own box 0x004ac150
  (its dlgMessage.zrd box is the 2D front end's, not wired yet) as SDL's
  message box with the same texts and buttons.
- **Mech3Msg.dll** (`zutl_msgdll`): read as data. The message table is the
  DLL's RT_MESSAGETABLE resource; `ZLocGetID`'s `{name, id}` table is found
  by following the export into its binary search (`mov ebx, table` ...
  `cmp ebx, end`; 1,577 entries in 1.2's DLL). Missing, the program says so
  and exits, as the original does.

## Ported calls wired late (mission-plan item 12)

The options' setters reach the player's mech through `WiredOptionsHooks`
(the pilot's +0x14, `program_mech_*` in app_mech.cpp), with the render
rect onto the mission's window; the briefing map's waypoints come from
`GameEngine::nav_find`, `mission_map`, `map_project` and `map_image` (the
nav's place read from the nav itself, `nav_position` handing the nav
through); `video_is_3d` answers zVideo's opened flag and `video_has_cap`
1, a modern GPU having every cap 0x00579b70 tests.

## What the start-up changes from the data (mission-plan item 13)

InitInstance (0x00480020) turns the animations' records for the save and
the network off (0x0050d1e0(0), 0x0050d1f0(0): `zeff::anim_log`), and the
Core state's start (0x00480df0) the frame clear (0x0057a3c0(0):
`zvid::display().clear_on`) and, with a card, the objects'
hidden-surface test (0x004f3550(0), CameraSetObjectHSETest:
`zcls::scene_walk().hidden_surface`); the program answers them as the exe
does. With the clear off the program's frame texture is loaded, not
cleared, as the exe's back buffer was; the world's window draws its clear
polygon each frame, and no trail shows (checked on c1m1, c3m1, c4bm4, t1).

## The census: known and not (mission-plan item 11)

`GameEngine::known_unported` names each reached key the program answers
its own way, leaves by a decision, or that has no effect, with the
program's equivalent or the reason; it is counted as before
(`unported_counts`), not logged, and `--verbose` and the census list it
apart. Everything else is a real gap and stays listed.

- **The program's own**: RdrSetPath / RdrAddPath 0x005561a0 / 0x005561e0
  and the image paths 0x00516f50 (the mounts and the archives; loose files
  unused), the fog's [0x0072f3f0] (zrndr::device_fog in the Render3D pass),
  the fillers' and the tint's targets 0x00557f90 / 0x00558290 with the back
  buffer's size and pitch 0x00576d90 / 0x00576da0 / 0x00576dc0
  (`zvid::lock_3d` gives them the Surface's own each frame), the targets'
  nodes 0x005a8390 (zent::marker_nodes), CZAiTarget's defaults and
  destructor 0x005a7e10 / 0x005a7f90 (the port's AiTarget), crc32's table
  0x00576c50 (built on first use), and at the app's end Mech3Msg.dll's
  FreeLibrary 0x00575f70 (the port reads the DLL as a file) and zutl's
  search paths 0x00576c60 (the mounts). The end's other two are wired:
  zInput's maps 0x0051d0c0 (`zin::shutdown`) and the reader 0x00556210
  (`zrdr::shutdown`: every archive closed; a changed table would be written
  there, 0x00576720, but the reader's .zbd files are only read). The
  app's stop (0x004808a0, the Exit state's and the tests'): the mechs'
  0x00414000 wired (`mechs_shutdown`; its archive close is the read-only
  motion archive's), and four frees known: zImage's 0x00516f00, zModel's
  0x00526780, zRender's 0x00557b10, the span buffers' 0x005646d0 (memory
  only, no write in their calls five deep).
- `census_every_mission_unported_headless` (MW3_CENSUS=1, about 6
  minutes): the 21 missions (c1m1..c2m4, c3m1..c3m6, c4m1..c4m3,
  c4bm4..c4bm6, t1) launched as `--mission` does, 1200 Core frames each,
  then the stop, ExitInstance and the app's end; fails on any key outside
  the table or a mission without the player's mech.
- With input (mission-plan item 21, by `build/mw3`'s `--click-at`,
  `--tap`, `--walk`, `--fire`, `--win`, `--lab-mech`): walking and
  firing, c1m1 won through the debriefing, the lab through LAUNCH, the
  Instant Action, Training, options, credits and multiplayer menus,
  c1m1's pause, Training's lesson 1 from the menus: 0 keys outside the
  table.
- `--campaign-chain new:N` / `resume:N` and `sdl_program_campaign_chain`
  (MW3_CAMPAIGN=1): the campaign played through, campaign.md section 13.
  `kills:N` wins each mission by shooting every enemy through zWeapon's
  impact and follows the salvage into the store and the lab (section 14).
  With no enemy left it does what a player would. It walks to an open
  reach_area's nav point or to a valve's region, and gives the MFB
  command from the net link that sets the valve (c3m1, c3m3). It logs
  each objective's completion frame. `retry:N` fails each mission once
  first (the pause's REPLAY) and compares the retry's nodes with the
  first try's (campaign.md section 14, The retry); `sdl_program_campaign_chain`
  runs `retry:4`.
- **`sdl_program_campaign_by_objectives`** (mw3sdltests,
  MW3_CAMPAIGN_ALL=1, about 5 minutes). The kills chain runs a new pilot
  through all 20 missions, seeded on the 33 ms clock. Each mission must
  be won by its own objectives (no --win fallback), the census must be 0,
  and the run must end at the credits. Each mission's win frame is
  printed, not pinned. **Run it after any change to the AI, the
  objectives, the mission scripts, the valves or the nets.**
- `--frame-ms N` (ours): each pass of the app's loop that runs the game is
  N ms on the game's clock (`SdlClock`, stepped by the pump when it finds
  no message waiting), a sleep only adding its time. Scripted runs keep the
  same timing and run as fast as they draw. Until 2026-10-29 a pass that
  only took a window or SDL event counted too, so the clock depended on
  the machine's load.
- `--frame-times N` (ours, for measuring; performance.md): from the N-th
  frame shown, each frame's wall time (present to present), summed up at
  the end: mean, median, p95, p99, max.
- `--mission [<world>]ia<N>[w<W>]` (ours, for measuring): Instant Action
  map N in wave mode from wave W, four Champions with the player, as the
  Action screen would set it (the AI survey's start); on c1 without a
  world, else on it (`c3ia2`: c3's readeria2; 2026-11-01, for the editor S1
  sweep, vehicles.md).
- `--seed N` (ours): every srand the game does from the time of day
  (0x00501ab0, 0x00508fc0, 0x0049aca0) takes N instead
  (`crt::rand_fixed_seed`). With `--frame-ms` a run repeats line for line:
  the kills chain (`--campaign-chain kills:4 --frame-ms 33 --seed 1999`)
  wins c1m3 at core frame 6491 every time (6636 before W10, when the
  field bases had no way to Charlie and took the long one).
- `sdl_program_screens_are_not_blank` (mw3sdltests, about 20 s; after item
  21's black lab, resolution.md section 9): the program itself, one run a
  screen, all at once, each shot through `--shot`. The screens: the main
  menu, the pilot screen, the briefing, the lab's three tabs, Instant
  Action, c1m1 at modes 5 and 13, the pause and the debriefing. Each shot's
  share of lit pixels must be over 0.3 (the lowest seen 0.525). The lab
  (0.2) and the briefing and debriefing (0.3) also need their green share.
  With render_flush's memory read put back, the three lab checks fail.
- The census's long runs (mission-plan item 22): `MW3_CENSUS_MISSION=c4m2`
  (any mission by name) and `MW3_CENSUS_STATS=1` (each 20 s cycle's wall
  time, memory footprint, handles, live nodes, shots) on
  `census_c1m1_unported_headless`.
- `--frames` and `--shot` on a launched mission (mission-plan item 22)
  count every frame shown from the first that drew the scene, the pause
  and end screens included. They counted only frames that drew the scene,
  so Escape's pause stopped the count and the run never ended (item 21's
  first pause run). `sdl_program_frames_end_a_paused_mission` runs
  `build/mw3 --mission c1m1 --tap 0x01@900 --frames 1200` and expects it to
  quit by itself. The fade into the mission is timed, so the frames it
  takes vary with the run's speed: c1m1's Core state has started at frames
  99 to 576 in hidden runs, under the test's 900.
- **Deferred by decision**: multiplayer (0x00588930, 0x00586c20, the chat
  panel 0x0041ab90, the Multiplayer State's three screens 0x00493260,
  0x00495f90, 0x0049bd80, the lobby session's flag 0x00586ce0), the stealth object (0x004ac860, 0x004a4de0), the developer-only lines
  (0x0040e4b0, the two vt+0x7c clears, 0x0040f7f0, 0x0040f890), the
  stealth object's name 0x005a84c0, the bitmap fonts 0x00519e50 (with
  zFont's measure and draw, which still log).
- **No effect**: 0x00558070 (its globals only written), 0x0040fef0 (its flag 0), 0x004b52e0 and 0x0047f2b0
  (item 6), 0x00518790, 0x004f8f20 (their defaults), 0x005269e0 (software
  only), 0x004f3540 (its global unread), 0x00535ed0 (read only by the
  software draw), 0x004f8f60 (zSave's registration off, none in the
  port), 0x00537520 (it Releases the used textures' system surfaces after
  the launch; the port's record is one Render3D handle, no system copy).

The modules' starts 0x00526510, 0x005578f0 and 0x00557b30 are the program's
own: the port's struct initialisers hold the same state, and what differs
is unread or set before use (module-starts.md). The gaps found while
checking were mission-plan.md items 12 to 15 and 17; the 30 s c1m1 census
now lists none. A key
joins the table only with its equivalent or reason verified.

## The engine's reports (our addition)

The exe's error report (`zerr_report`, 0x0049bfd0) is empty in this build,
so the original never shows its asserts and warnings. The rebuild prints
them to stderr as `mw3: report <level> <file>:<line> <text>` (not when
quiet), with the text filled the way the exe's calls pass their arguments
(`zerr::reportf`; the modules' own printers in `app_mission.cpp` the same).
Where the exe passes nothing for a `%s` (weilerIntersect's error, zWeapon's
"Failed to read %s") the format prints as written; ZAiTarget's animation
name comes from 0x0050b620, empty in this build (the exe passes what EAX
holds), so the rebuild names the animation. `reports_fill_their_formats`
checks it. A mission start prints five: the quit box's dialog lacks the
YES, NO, ABORT, RETRY and IGNORE buttons the message box binds.

`MW3_TRACE_UNPORTED=<key>` (ours, not on Windows) prints a backtrace for that unported
key's first 8 reaches, to find which hook a census entry comes through.

## Tests

- `build/mw3tests` (`tests/test_program.cpp`): Mech3Msg.dll read (message 7,
  0x14d, the skin keys through `zutl::localize`), the mounts and write dir
  order, the music tracks found through the mounts and the disc's track
  count, and the whole start-up with fake platform parts: InitInstance,
  `zGameApp::Run` through Intro to the Main state, the title, Direct3D
  chosen, the movies reaching their hooks, the main menu built from
  MAINMENU (its buttons bound, the version line), frames flipped.
- `build/mw3sdltests` (`tests/test_sdl_frontend.cpp`): the DIK table (known
  codes, no two keys sharing one), the keyboard buffer (order, auto-repeat,
  overflow, unacquired), the mouse counts, the GDI text on the game's fonts
  (extents, metrics, two lines, colour only where the glyphs are, opaque mode,
  GDI's cell height, word breaks, `&` prefixes).
- `tests/differential/test_zinput.py`: zInput against the exe (input.md,
  Testing).
- `tests/differential/test_mission_load.py`: the mission load against the
  exe (mission-load.md, Testing).
- `tests/differential/test_settings.py`: the settings loader and the store
  against the exe (settings.md, Testing); `mw3tests` reads a regedit export
  and saves through `platform::RegFile`.
- `tests/differential/test_ztimer.py` now also runs the clock start
  0x005759a0 on a random frame against the exe (break tests: the total not
  zeroed, the tick product rounded differently: both caught).
- The app tests (`test_app_loop.py`, `test_app_options.py`,
  `test_app_states.py`) are unchanged and pass: `GameEngine` only overrides
  hooks; the base `Engine` they test is as it was.

Checked by hand (2026-10-20): `mw3 --mission c1 --shot` draws the world
(c1's river and canyon under the hormain sky), `--dialog dialog.zrd MAINMENU`
draws the main menu with its TrueType labels, a visible window runs 150
frames of c1m2, a test track in the write dir's `music/` plays and loops
through the MCI notify path.

## Open

- mech3's controls setup (the default keys of the 0xa7 commands, 0x00411150,
  and the controls file, input.md Open).
- The screens after the main menu (mainmenu.md, Left) on zTwoD and the
  SDL host; the game's message box 0x004ac150 on dlgMessage.zrd; the
  ripple's drawing 0x00558dd0.
- The mission's hooked modules (mission-load.md, Open): the player's mech,
  AI, weapons, effects, HUD; the briefing screen and its Launch button.
- Movies (zFMV, Indeo 5).
- The cursor (0x00579840) is not shown or hidden; the menus draw their own.
- Shaders: MSL (Metal) and DXIL (Direct3D 12, windows.md); no SPIR-V (Vulkan) yet.
