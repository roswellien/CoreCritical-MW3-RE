# Cutting a release

A checklist for the three platforms' packages. Owen approves every tag and
every GitHub release; nothing here publishes anything by itself. The
packages are built from one commit, so every platform's version is the same.

## MW3 v1.0: "done"

Owen, 2026-10-31: MechWarrior 3 is declared done with the tag `v1.0`
before any Pirate's Moon work reaches `main`. Pirate's Moon lives on its own
branch, `pm`, which never merges to `main` (Owen, 2026-11-04). `v1.0` is tagged when
every box here is ticked:

- [ ] Multiplayer ported (`docs/multiplayer-port.md`, every piece), with
      the two-process and soak tests passing.
- [x] The 1.2 patch rebuilt from 1.0 data (`docs/patch12.md`), and SETUP's
      patch button gone (shown only for data of an unknown version).
- [ ] The stealth object and BASIC ported as the original has them, its
      fly camera on Shift+F12 under --debug-camera (docs/editor.md S1-S4;
      F11 stays the lancemate command screen). The
      Battlesport editor's menus (the AI net editor, the vehicle editor) come
      after v1.0, with Pirate's Moon, whose net editor changed (Owen,
      2026-11-01).
- [x] SETUP takes any source foolproof: an install, a disc, .iso, .cue/.bin,
      .zip (nested too), .7z; maps it and installs without the player
      unpacking anything (`docs/setup-sources.md`; the packages carry
      libarchive, zlib and liblzma with their licences).
- [x] The software renderer no longer offered anywhere in the menus.
- [ ] Fully 16:9 at the desktop's resolution (Owen, 2026-11-01): every
      screen widened (the multiplayer screens and the logo screens checked);
      the movies with black bars baked into their 4:3 frame (INTRO 640x320,
      C4WIN 640x240) shown without them, scaled to the screen's width, the
      whole picture kept; the 17 true-4:3 briefing movies centred with
      plain black sides.
      The movies and the logo screens done 2026-11-01 (fmv.md section 8:
      the shapes measured from the pictures; C4WIN is letterboxed but for
      its 4:3 ending, which is centred like the briefings).
- [ ] Moddable, simply (Owen, 2026-11-01): players and hobbyists can add
      their own game data without any change to the exe later. A `mods/`
      folder in the settings folder (one folder per mod, a small manifest,
      mounted in front of the install in mw3.cfg's order, nothing written
      into the install); new worlds, Instant Action maps, multiplayer maps
      and a campaign declared by a mod (the 1.2 patch's add-on scan,
      extended); a plain-text string file over Mech3Msg.dll's; readers that
      refuse a broken file with a message, never crash; docs/modding.md and
      `mw3 --check-mod DIR`. Nothing changes with no mods installed.
- [x] Every row in `docs/playtest.md` closed: fixed, by design or the
      original's, none open.
- [x] The community-reported problems (`docs/community-issues.md`) each
      have a verdict (Owen, 2026-10-31: items 9 and 12 kept as the original).
- The whole campaign on the release build: the automated 20-mission run
  (`sdl_program_campaign_by_objectives`) counts; Owen's own playthrough is a
  later playtest, not a gate (Owen, 2026-10-31).
- [ ] Every check in sections 1-4 below passes on all three platforms.

## After Pirate's Moon: Rebuild Upgrades (planned for 1.1.0)

Owen, 2026-11-05. Work starts when Pirate's Moon is finished. Today one switch,
bug fixes, covers every play change. After this:

- **The bug fixes switch goes away.**
- **Game-breaking bug fixes are always on,** with no switch.
- **Every other fix is an upgrade,** on a new settings page, each with its own
  switch, all on by default, so a player can turn them off for the original
  experience. These include:
  - The mobile field bases regrouping as soon as a repair is done.
  - The improved AI (lancemates finding a way round walls, and so on).
  - The water movement.
  - Artemis FCS tracking.
- **In multiplayer the host's settings apply to everyone in the game.**

When the work starts: go through `fixes.md` and sort each fix into game-breaking
or upgrade, for Owen to approve.

## 1. Before

- [ ] `main` is the commit to release, with nothing uncommitted.
- [ ] The tests of what changed since the last release pass on that commit, not
      the whole suites (Owen, 2026-11-05): each change's own tests and those of
      the code that uses it. A change to movement or collision runs the mission
      tests (`MW3_TEST=mission_`), a change to loading or play the campaign runs,
      a change to platform code its tests on that platform. The notes say which
      ran. Full runs when wanted: `tests/differential/run_all.sh` under
      `run_capped.py --secs 2400`; Linux one test at a time (docs/linux.md,
      Checking).
- [ ] `CHANGES.md` has a line for every change since the last release.
- [ ] The README's **What's new** has this release's section, its date, and an
      emptied "Coming in the next release". Fixes add a line there as they reach
      `main` (Owen, 2026-11-04).
- [ ] The release notes are drafted (`docs/release-notes-<version>.md`) and
      Owen has read them.
- [ ] The players' read-mes match the build: `tools/windows/dist/README.txt`,
      `tools/macos/README.txt`, the top-level `README.md`.
- [x] Open since 2026-11-01: the Mac `--deps` release form
      (`tools/macos/make_app.sh --deps`) not built since libarchive, zlib
      and liblzma arrived: rebuild build-deps/mac (`build_deps.sh mac`),
      build the app, and check it is universal, links them in, and carries
      their licences in Contents/Resources/licenses.

## 2. The version

The version is git's: `git describe --tags --match "v[0-9]*" --always --dirty`, so only game tags count (CMake reads it
into `MW3_VERSION`, which `mw3 --version`, the Windows version resource, the
Mac's Info.plist and every package's file name carry), and the commit count
into `MW3_BUILD_NUMBER`.

**Numbering (Owen, 2026-11-05):** each part is one digit. 1.0.9 is followed by
1.1.0 and 1.1.9 by 1.2.0, whatever is in the release; there is never a 1.1.10.

- [ ] Owen tags the commit (`git tag -a v0.1 -m ...`), or approves the tag.
      Until then the version is the commit's short hash.
- [ ] The version does not end in `-dirty`: every package script builds from
      scratch, and a dirty tree names every file.
- [ ] Each platform builds from the same tagged commit.

## 3. The packages

All three are configured with `-DMW3_PACKAGE=ON`, so no development copy of
the game's path is built into the programs.

### Windows (on the PC)

`tools\windows\package.cmd` (docs/windows.md, Packaging) makes
`build-win\dist\mw3-<version>-win64\`, its `.zip`, and
`MW3-Rebuild-Setup-<version>.exe` (Inno Setup).

- [ ] `mw3.exe --version` names the version.
- [x] The folder has `mw3.exe`, `mw3indeo.exe`, `mw3import.exe`, the SDL and
      FFmpeg DLLs, the Visual C++ runtime, `README.txt`, `THIRD-PARTY.txt`,
      `licenses\`, `assets\launcher\`.
- [ ] On a clean user: the installer installs, the launcher finds the
      install (or SETUP does), a mission plays, the intro movie plays through
      `mw3indeo.exe`, and with the helper renamed `mw3import.exe` converts
      the movies instead.
- [ ] The uninstaller removes the program and keeps
      `%APPDATA%\MicroProse\MechWarrior 3\`.

### Mac

`tools/deps/build_deps.sh mac`, then `tools/macos/make_app.sh --deps`
(docs/macos.md) makes `build-mac/dist/MW3-Rebuild-<version>-mac.dmg`.

- [x] Every binary in the bundle is x86_64 and arm64 (`lipo -archs`), with
      minimum macOS 12.0, and loads only system libraries and the bundle's
      own (`otool -L`: no `/opt/homebrew`).
- [x] `codesign -dv` on the app: `Signature=adhoc`, `TeamIdentifier=not set`.
- [ ] Opened from the DMG on a Mac that has not seen it: Gatekeeper's
      first-open steps in the README work.
- [ ] It starts and plays a mission natively and under Rosetta
      (`arch -x86_64`).

### Linux

`tools/deps/build_deps.sh linux` (JOBS=6 under WSL), then
`tools/linux/make-appimage.sh build-deps/linux/prefix <tools dir>`
(docs/linux.md) makes `build-linux-pkg/dist/MW3-Rebuild-<version>-x86_64.AppImage`.

- [x] `AppDir/usr/lib` has SDL3, SDL3_ttf, SDL3_sound and the four FFmpeg
      libraries; `ldd` on `mw3` and `mw3import` finds them there, and nothing
      else from the build prefix.
- [ ] The launcher shows its text; with an install, a mission plays.
- [ ] `--import --movie INTRO` converts the movies and plays the intro.
- [x] `AppDir/usr/share/doc/mw3/licenses` has every bundled library's
      licence files.

## 4. Privacy

Every file that leaves the machine (the packages, the read-mes, the release
notes, the README) is checked before it goes up.

- [ ] **No legal name.** The credits are "Owen Morgan" everywhere: LICENSE,
      the README, `THIRD-PARTY.txt`, the installer, the Info.plist, the
      release notes. Search each package's text and binaries for Owen's other
      name and for his email addresses (`strings -a`, `grep -ri`); there must
      be no match.
- [x] **No team ID or signing identity.** The Mac app is signed ad hoc
      (above); the Windows files carry no certificate.
- [x] **No local paths.** No machine's folders in the binaries or the text:
      `strings -a <each binary> | grep -E '/Users/|/Volumes/|/home/|C:\\Users\\|E:\\'`
      must print nothing. (`MW3_PACKAGE=ON` keeps the game folder out; the
      compilers' own `__FILE__` strings must be relative or absent.
      The package scripts check this themselves (`check_paths.py`,
      `check_dist.ps1`).)
- [x] **No game files.** No `.zbd`, `.avi`, `.wav`, `.zrd` or other MicroProse
      data in any package; only our own launcher art (`assets/launcher`).
- [x] **No logs or settings.** No `mw3.log`, `mw3.cfg`, `settings.reg`,
      pilots or saves in any package.
- [ ] The release notes and the README name no machine, folder or private
      service.

### Status of the checks (2026-11-02, packages from 19a68f71)

Ticked above: what passed on the Mac DMG, the Windows folder and zip, and
the Linux AppDir. Left, and why:

- **Local paths: fixed (2026-11-02).** FFmpeg keeps its configure line in
  each library (`avutil_configuration()`), and build_deps.sh's real
  `--prefix` put the build folder and user name into the Mac and Linux
  FFmpeg. It is configured with `/ffbuild` now. make_app.sh and
  make-appimage.sh run `tools/deps/check_paths.py` on what they ship, and
  check_dist.ps1 does the same on Windows: any `/Volumes/`, `/Users/`,
  `/home/`, `C:\Users\` or the building user's name fails the package.
  Rebuilt and checked clean on the Mac and Linux.
- Windows `--version` says `unknown` and the package `dev`: the PC copy it
  was built in is not a git checkout. The release build comes from a clone
  at the tag. The installer was built (Inno Setup 7 is on the PC); its
  payload is the checked folder.
- The tests at 19a68f71 on Windows: the file-leak crash and the drive-path
  fix are on lead/b26, not main yet; v1.0 needs them.
- Owen's: the tag and the version (section 2), the release notes, a clean
  Windows user (install, uninstall), Gatekeeper on a fresh Mac, play under
  Rosetta, and the Linux launcher, mission and movie runs (WSLg windows
  appear on his desktop).

## 5. Publishing (Owen)

- [ ] The tag pushed, the GitHub release made with the notes, and the three
      platforms' files uploaded, with a `SHA256SUMS` of them.
- [ ] Each upload downloaded again and its checksum checked.
