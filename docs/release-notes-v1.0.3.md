**The game no longer closes after the intro on Linux, your data can live on
another drive, and a crash now says why.** Install it over any earlier 1.0.x:
your pilots, saves and settings are kept.

## New

- **Your data folder can live on another drive.** In the launcher, GAME FILES,
  then MY DATA FOLDER. It copies your settings, pilots, saved games and converted
  movies, checks each file against the original, and only then switches over. It
  then offers to remove the old copy; if you keep it, removing it later is up to
  you. Log files stay in the old folder. If the drive isn't connected when you
  start, the game says so and waits, instead of starting afresh with no pilots.
  On Windows, the new folder's path has to use plain English letters for now.
  We tested this with ordinary folders and simulated failures, not with a
  removable drive or a disk that really filled up.

## Fixed

- **Linux: the game closed at the end of the intro movies.** The launcher closed
  as the game appeared, which took the AppImage away from the game still running
  from it. The launcher now waits, out of sight, until the game ends.
- **Linux: an install in any letter case now works.** All lower case, all upper
  case or mixed. Windows ignores case, so the game's own names never had to match
  the files; on Linux they did.
- **Crashes say why.** A crash, or a start that can't open its window or
  graphics, now leaves a line in `mw3.log` and shows a message box that names the
  log. It used to just close.
- **Windows: the installer asks for a folder every time,** so you can install
  somewhere other than C, or to a new place when upgrading. A new folder leaves
  the old folder's files behind. We set this in the installer but haven't
  yet watched the page on a real install; tell us if it doesn't appear.
- **A softer message for mods.** When files match neither 1.0 nor 1.2, it now
  says data mods are read, though they may not play right, and that a mod which
  patches the old `Mech3.exe` won't apply. This message never stopped the game
  and still doesn't.
- **Vehicles on fast machines.** Small bumps throw them much less: the suspension's
  reaction to the ground is capped at what it is at 30 fps. On rough ground they
  still tip more than at 30 fps, and at full speed they still jump off steep banks
  now and then, at ordinary frame rates too. This is part of bug fixes.
- **A band under the sky.** Seen from high up, where nothing is drawn between the
  far ground and the sky, the game showed a dark band. That gap now takes the
  fog's colour. This is part of bug fixes.
- **Smaller fixes.** A memory leak in the zoom views. A light carried by something
  that moves now shines from where it is, not from the world's origin. The pitch
  of moving sounds matches the original more closely.

## Still known

- **macOS: window tools can delay the mouse button by about a second**
  (Magnet and similar). Quit the tool while you play, or exclude
  MechWarrior 3 in it.
- **Jump jets lighting up water (Op 3, Mission 5):** one report, not
  reproduced. A screenshot and `mw3.log` help.
- Mech speed in shallow water and on hills is the original's: speed has no
  slope term, and water only slows a mech deeper than a tenth of its height.
- Pirate's Moon is not supported yet.

## Downloads

| | Download | |
|---|---|---|
| Windows | `MW3-Rebuild-Setup-v1.0.3.exe` | An installer (a `.zip` of the same files is there too). |
| Mac | `MW3-Rebuild-v1.0.3-mac.dmg` | Drag the app to Applications. |
| Linux | `MW3-Rebuild-v1.0.3-x86_64.AppImage` | Make it executable and run it. Built and checked under WSL, not playtested on a Linux machine. |

You need your own MechWarrior 3 (an installed copy, the disc, or a disc
image); see the 1.0 release for getting started.

## Reporting bugs

Open an issue on the project's GitHub page with `mw3.log` from the
settings folder (the README says where it is).
