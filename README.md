# MechWarrior 3 Rebuild

An unofficial fan rebuild of **MechWarrior 3** (Zipper Interactive and
MicroProse, 1999) for today's computers: Windows 10 and 11, macOS and Linux,
with no Wine, DirectX wrappers, DDrawCompat or dgVoodoo.

It is not a remake. The original `Mech3.exe` (patch 1.2) was taken apart and
its code rewritten function by function in C++, most of it checked against
the original running in an emulator. The game plays the way it always did,
minus the bugs and the problems it has on modern machines. It is a personal
learning project: free, not sold, and not affiliated with or endorsed by
Microsoft or MicroProse.

**No game files are included.** You need your own copy of MechWarrior 3.

> Found a bug? Please [report it](#reporting-a-bug).

## What's new

Every release's full notes are on the [Releases](../../releases) page, and
[CHANGES.md](CHANGES.md) lists everything the rebuild changes from the
original.

### Coming in the next release

- **The same acceleration above 83 fps.** Holding a throttle key above about
  83 fps skipped the mech's slow build-up of speed, so it pulled away nearly
  2 seconds sooner than at 30 or 60 fps. It now builds up the same at any
  frame rate.
- **Walking into a building does its full damage at any frame rate.** An
  earlier fix scaled the 20 damage of each touch by the frame time, so the
  higher your frame rate the less a building took (about a fifth at 144 fps).
  Each touch does 20 again, as in the original.
- **Where a foot reads the ground.** The original never filled in the setting
  that decides when a foot uses the ground under its middle. Our fix guessed
  at it; it now uses the value the original team's own Pirate's Moon sets. This
  changes slightly how feet sit on slopes and when a foot counts as in water.
  This is part of bug fixes.
- **The target keys tell the HUD again.** Choosing a target with E, W or their
  Ctrl and Shift forms now does what Z and Q already did: the target triangle
  pulses, the target's mech sound plays, and a missile lock starts over on the new
  target instead of carrying across.
- **A stick's throttle lever stays the right way round.** Tapping throttle-down
  with the lever below stop, or pressing Toggle throttle direction (Backspace or
  stick button 10) with it above stop, could leave the lever backwards until the
  mission ended. That toggle no longer affects a lever. If yours runs backwards
  from the start, use the Invert box (Options, Controls, Assign Axis).
- **Op 2 Mission 2's gas kills.** After the Air Refinery goes down, standing in its
  gas now destroys you, or Dominic, as the original does; before, nothing happened.
- **The debriefing's scores.** A campaign debriefing now lists the mission's
  objectives, each worth 2500 when done, and counts them in the mission and
  campaign totals and the rank; before, it listed none and scored none. Instant
  Action's Battle Waves and Progression now score 1000 a wave plus a bonus for
  how far you got, as the original does, in place of a flat 1000, so a round that
  clears no wave scores nothing but its kills. Scores you already have stay as
  saved; new missions add to them.

**1.0.3 (2026-11-05)**

- **Your data folder can live on another drive:** GAME FILES, MY DATA FOLDER in the
  launcher copies it, checks the copy, then switches over, and removes the old
  copy only if you say so. If the drive is missing at start the game says so
  instead of starting afresh.
- **Linux:** the game no longer closes at the end of the intro movies.
- **Linux:** an install in any letter case now works (all lower case, all upper
  case, or mixed).
- **A softer message for mods:** when files match neither 1.0 nor 1.2, it now says
  data mods are read, though they may not play right, and that a mod which
  patches the old Mech3.exe won't apply. It never stopped the game.
- **Crashes say why:** a crash or a failed start now leaves a line in the log
  and shows a message box, on every platform.
- **Windows:** the installer asks for a folder every time, so you can install
  somewhere other than C.
- **Vehicles on fast machines:** small bumps throw them much less. On rough
  ground they still tip more than at 30 fps, and they still jump off steep
  banks at full speed, as in the original.
- **A band under the sky:** where nothing is drawn between the far ground and
  the sky, the game now shows the fog's colour.
- Smaller fixes: a memory leak in the zoom views, a light's position, and the
  pitch of moving sounds.

**1.0.2 (2026-11-04)**

- **Lancemates walk round things.** When a straight walk fails, they find a
  way round walls, up ramps and across rivers. Part of bug fixes; not for
  enemies or in multiplayer.
- **Lights made during play work.** Jump jets and gunfire light your mech.
- **A mech no longer walks by itself** when saved stick controls are used with
  no stick attached.

**1.0.1 (2026-11-03)**

- **Op 1, Mission 2's missile platforms** no longer vanish at high frame rates,
  which could make the mission impossible to win.
- **Custom key bindings** work on the Controls screen.
- **Flight sticks:** twist and throttle reach the right axes, and the throttle
  lever reaches reverse.
- **A crash late in a fight** is fixed, along with the end credits on wide
  screens and the mech lab's "Overwrite?" box.

## What you need

- **Your own MechWarrior 3**, any of these:
  - an installed copy, version 1.2 or 1.0 (the rebuild makes the 1.2
    patch's changes itself as 1.0's data loads, so it plays as 1.2; your
    files are not changed);
  - the original disc;
  - a disc image: an `.iso`, or a `.cue` with its `.bin` files (a `.cue`
    also gives you the music).
- **A computer** with a graphics card that does Direct3D 12 (Windows), Metal
  (Mac) or Vulkan (Linux):
  - Windows 10 or 11, 64-bit;
  - macOS 12 or later, Apple silicon or Intel;
  - Linux, x86-64, with your distribution's Vulkan driver.

Pirate's Moon is not supported yet.

## Download

From the [Releases](../../releases) page:

| | File | |
|---|---|---|
| Windows | `MW3-Rebuild-Setup-<version>.exe` | An installer. A `.zip` of the same files is there too. |
| Mac | `MW3-Rebuild-<version>-mac.dmg` | Drag the app to Applications. |
| Linux | `MW3-Rebuild-<version>-x86_64.AppImage` | Make it executable (`chmod +x`) and run it. |

**On a Mac, the first time:** the app is not signed by Apple, so macOS stops
it the first time you open it ("MechWarrior 3 Not Opened"; click **Done**,
not Move to Trash).

- **macOS 15 Sequoia and later:** open **System Settings > Privacy &
  Security**, scroll down to the line about MechWarrior 3, click **Open
  Anyway**, then **Open** (and your password if asked). Right-click > Open
  no longer gets past it on these versions.
- **macOS 12 to 14:** right-click (or Control-click) the app, choose
  **Open**, then **Open** again. Open Anyway in Privacy & Security works
  there too.

macOS remembers your choice after that.

**On Windows**, SmartScreen may warn about an unknown publisher: click
**More info**, then **Run anyway**.

## The first start

Start **MechWarrior 3** and it goes straight into the game when it finds
your install: where the original installer put it, `Program Files`,
`C:\Games`, a Wine, Proton or Lutris copy on Linux and the Mac, or a copy it
installed itself earlier. It says which version it found in its log.

**If it finds none**, choosing MechWarrior 3 opens a screen that finds
your game (a plain `mw3` start shows only that screen), and once the game
is there it goes straight on into it:

- **Find the game** searches this computer and your disc drives first and
  uses what it finds, with nothing to pick. If there is nothing, one
  picker opens: choose a disc image or download (an `.iso`, a `.cue` with
  its `.bin` files, whose CD audio becomes the music, or a `.zip`, `.7z` or
  `.rar` holding any of those, even a zip inside a zip; nothing needs
  unpacking by hand), or `Mech3.exe` (or any file) in an installed copy,
  which means that whole folder.
  It looks inside first and says what it found ("The MechWarrior 3 disc
  version 1.0, with its music (2 CD tracks)"), then copies what the game
  needs into a folder of the rebuild's own, with a progress bar and
  **Cancel**. An installed copy is used where it is. No Windows and no
  original installer needed. Versions 1.0 and 1.2 both work as they are:
  1.0 plays as 1.2. Pirate's Moon is recognised, but its support comes later.

If something is wrong (not enough space, a damaged download, a file that
is not MechWarrior 3) it says so in plain words and changes nothing.

- **The launcher**, installed as "MechWarrior 3" (the Start-menu and
  desktop shortcut on Windows, the Mac's "MechWarrior 3.app", the Linux
  desktop entry), is the rebuild's own front screen: the games. It starts
  the game and closes as soon as the game's first frame is on screen (if
  the game fails to start, it stays and says so). **Game files** there
  opens the same find-your-game screen at any time: to use another copy,
  install again from your disc, or add movies and music it lacks. `mw3`
  (`mw3.exe`) also starts the game directly. The arrow keys and a gamepad
  work in the launcher as well as the mouse.
- Before the first game it tells you anything worth knowing about your
  copy: no movies (a CD-type install leaves them on the disc), no music, or
  game files of an unknown version. **Add from my disc** there takes what
  is missing from your disc in a drive, a disc image or the disc's folder.
- **The movies** play from your own copy. Where they cannot be played
  directly, the rebuild converts them once, the first time (about a minute).

Your MechWarrior 3 files are only ever read, never changed.

## Music

The original played its music from the CD's audio tracks, which an
installed copy does not have. Two ways to get it back:

- With a disc image (a `.cue` and its `.bin`): **Add from my disc** on the
  notes before your first game (installing from the `.cue` takes it at once).
- Your own rip: put the tracks in the rebuild's `music` folder (below) as
  `track02.ogg`, `track03.ogg` and so on (or `.flac`, `.wav`, `.mp3`),
  numbered as on the CD.

## Playing

- The game runs fullscreen at your desktop's resolution, widescreen
  included. **Alt+Enter** switches between fullscreen and a window.
- In a window, press and release **Ctrl+Alt** to free the mouse; click in
  the window to take it back. Switching away frees it too, and coming back
  to a mission (Cmd+Tab, Alt+Tab or a click) takes it again at once; that
  click does not fire.
- **Options > Graphics & Audio > Mouse sensitivity** (ours) sets how fast the mouse aims in a mission; it starts at the original's setting.
- Gamepads work as they did on Windows 98, and force feedback becomes rumble.
- Flight sticks' axes (twist, throttle lever) go where the game expects
  them. With several devices, `joystick=Part of its name` in `mw3.cfg`
  picks one; an odd stick's axes can be set with `joystick_axes=X,Y,Rz,S0`
  (one word a stick axis: X, Y, Z, Rx, Ry, Rz, S0, S1, or - for none).
  `mw3.log` lists every device found and the line in use.
- A stick's throttle lever covers reverse too: full reverse at the bottom,
  stop a little over a third of the way up. If yours runs backwards, untick
  Invert in Options, Controls, Assign Axis on Forward Throttle, and save
  when Accept asks. Toggle throttle direction (Backspace) is for the keys
  and does not flip a lever. `throttle_reverse=0` in `mw3.cfg` gives the original's
  stop at the bottom (reverse is then Backspace).
- On a widescreen every screen uses the whole width: the missions, the
  menus, the mech lab, the briefings and the multiplayer screens. For the
  original's 4:3 menus, put `widemenus=0` in `mw3.cfg` (or start with
  `--no-wide-menus`).
- The movies keep their shape on a wide screen: the intro and the ending,
  whose black bars were part of the picture, fill the screen's width; the
  4:3 movies are centred with black sides.

### Multiplayer

**Multiplayer** on the main menu plays the original's network games over
the internet or a local network, up to 8 players. DirectPlay, which the
original used, is gone from modern systems, so the rebuild has its own
connection (UDP); IPX, modem and serial play are gone with it. Everyone
plays the rebuild, on any mix of Windows, Mac and Linux.

- **On a local network**, games show up on the connection screen by
  themselves as soon as it opens.
- **Joining by address**: type the host's address (`203.0.113.7`, or
  `203.0.113.7:27930` for another port) and press **Find Game** or
  Enter.
- **Hosting**: **Host Setup**, then **Host**. The game opens its UDP port,
  27930, on your router by UPnP when the router allows it, and closes it
  again when you stop. The connection screen and the lobby show the
  address to give your friends (click it to copy) and your LAN address.
  When the internet cannot reach you, the screen says why and what to do:
  - "forward UDP 27930 on your router to host": no router answered, so set
    up the port forward by hand in your router's settings;
  - "Could not open the port": the router refused UPnP; forward the port
    by hand;
  - "Another router stands in front of yours": a second router (often the
    provider's modem) sits between yours and the internet; forward UDP
    27930 on that one too, or use a VPN such as Tailscale;
  - "Your internet provider blocks hosting": your provider shares one
    internet address between many customers (carrier-grade NAT), so no
    one can reach you from outside. Play on a LAN, or over a VPN such as
    Tailscale; joining other people's games still works.
- **Leaving a match**: Escape shows "Are you sure you want to quit?". With
  bug fixes on (below) the match goes on while it is up, so you can still
  move and fire; **Y** leaves
  for the main menu, **N** or Escape closes the box. The others see you
  leave at once. A match also keeps running when its window is in the
  background, so your 'Mech is never a frozen target.
- **Mods** must match: a player whose gameplay mods differ from the host's
  is told which mods each side uses.

### Bug fixes, and playing exactly like the original

The rebuild fixes the original's bugs where the intent is clear: things that
ran faster on a fast computer, the mobile field bases that would not come
back, lancemates walking into walls, and more. Quirks that players know and
plan around are kept. Every change is listed in plain words in
[CHANGES.md](CHANGES.md).

To play exactly as the original did, turn the fixes off: start with
`--original`, or put the line `bugfixes=0` in `mw3.cfg` (in the folder
below). The changes the game needs to run on a modern computer stay on.

### Cheat codes

The rebuild adds cheat codes (ours, not in the original) for the campaign,
Instant Action and training; a network game refuses them. In a mission,
press **Shift+Enter** (or **Keypad \*** or **Keypad −**) to open a one-line
box, type a code and press **Enter**; **Esc** closes it. The mission keeps
running while you type. Capitals and extra spaces don't matter. A code
lasts until the mission ends; the ones that switch something on switch it
off when typed again. The message panel says what was turned on or off.

| Code | What it does |
|---|---|
| `black sheep wall` | Every mech and vehicle on the radar, at any range |
| `power overwhelming` | Your mech takes no damage |
| `operation cwal` | Your weapons reload and your mech cools ten times as fast |
| `food for thought` | Maximum salvage: every chance in the salvage goes your way |
| `there is no cow level` | Wins the mission: every enemy mech and vehicle destroyed by you and salvaged whole, every objective done |
| `game over man` | Loses the mission |

A mission won with a code counts as won. In a network game the keypad keys
stay the chat's. More in [docs/cheats.md](docs/cheats.md).

## Mods

The rebuild loads mods: folders of game data in a `mods` folder inside the
settings folder (below), laid out like the game's own files, each with a
short `mod.txt`. A mod can change textures, sounds, text, missions and the
game's data files, or add new worlds and maps; it never touches your
install or your pilots. List them in `mw3.cfg` (`mod=NAME`, one a line) to
turn them on; `mw3 --mods` lists what it found, and `mw3 --check-mod
FOLDER` reads a mod as the game would and says what it would refuse. With
no mods turned on, nothing changes. How to make one:
[docs/modding.md](docs/modding.md).

The original game has a tool of its own here: [tools/weapon-patch](tools/weapon-patch)
adds weapons to the original `Mech3.exe`'s mech lab. It isn't part of the rebuild.

## Where your saves and settings go

| | Folder |
|---|---|
| Windows | `%APPDATA%\MicroProse\MechWarrior 3\` |
| Mac | `~/Library/Application Support/MicroProse/MechWarrior 3/` |
| Linux | `~/.local/share/MicroProse/MechWarrior 3/` |

It holds `mw3.cfg`, your pilots and saves, the settings, the converted movies
and the music, and `mw3.log`. Uninstalling the rebuild keeps it; deleting it
starts you over.

## Known issues

- **macOS: window tools can delay the mouse button by about a second.**
  Tools that filter mouse clicks to snap or move windows (Magnet and
  similar) can hold each mouse-button press back, so in a mission the mouse
  fires only when you let go. The keyboard is not affected. Quit the tool
  while you play, or add MechWarrior 3 to its excluded apps if it has that
  setting.

## Reporting a bug

Open an [issue](../../issues) and include:

- what you did and what happened (a screenshot helps);
- the mission, and whether it happens with `--original` too;
- your system (Windows, Mac or Linux, and the version);
- **`mw3.log`** from the folder above. It is written new at every start and
  the one before is kept as `mw3.old.log`, so if you started the game again
  after a crash, send `mw3.old.log`.

## Hidden mechanics

Taking the game apart turned up hundreds of hidden mechanics, quirks and
bugs, collected for players in [docs/discoveries.md](docs/discoveries.md).
A few:

- Lasers never cause critical hits, and criticals only happen once a part's
  armor is down to 15%.
- Ammo criticals don't explode: they just empty a bin. CASE in a side torso
  does nothing at all, and the stock Mad Cat carries both of its CASE there.
- Ultra autocannons never jam; they simply fire twice as fast.
- TAG works, but the missiles home on the spot you marked, not the target,
  and take 7 seconds to arrive.
- Pilot files track hidden Instant Action and multiplayer stats that the
  game never shows.

**For the curious:** start with `--debug-camera` (or put `debugcamera=1`
in `mw3.cfg`) and press **Shift+F12** in a mission to open the game editor
the original carries but never lets you reach: its menu over the mission,
and the view on the invisible "stealth" camera, flown with the movement
keys. Shift+F12 again puts the view back on your 'Mech. It is a developer's
tool, as it was in 1999: expect rough edges.

## Building from source

See [docs/building.md](docs/building.md). How the code is organised (the
shared engine, MechWarrior 3, Pirate's Moon and the launcher):
[docs/games-layout.md](docs/games-layout.md).

## Credits and licence

- **MechWarrior 3** was made by Zipper Interactive and published by
  MicroProse in 1999. All the game's content is theirs and comes from your
  own copy.
- The rebuild is by Owen Morgan, under the [MIT licence](LICENSE). The
  original's code was studied with Ghidra and
  [REA](https://github.com/morluto/rea), the data formats cross-checked with
  [mech3ax](https://github.com/TerranMechworks/mech3ax), and the port checked
  against the original in the [Unicorn](https://www.unicorn-engine.org/)
  emulator.
- It runs on [SDL3](https://www.libsdl.org/) (with SDL3_ttf, FreeType and
  SDL3_sound), and converts the movies with a minimal LGPL build of
  [FFmpeg](https://ffmpeg.org/). Each download lists them in
  `THIRD-PARTY.txt`, with their own licence files.
- The launcher can also start **MechWarrior (1989)** through
  [Elthial's SDL port](https://github.com/Elthial/Mechwarrior-sdl-port), if
  you have that installed beside it.

MechWarrior is a trademark of Microsoft. This project is not affiliated with
or endorsed by Microsoft, MicroProse or Zipper Interactive.
