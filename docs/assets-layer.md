# Where the game's files come from

The rebuild never ships game files. Every file it reads goes through one
layer, `zutl::` in `src/gamez/zutil/zutl_path.*`, so the files can come from
the original install, from a folder of replacements in front of it, or from a
complete replacement set with no install at all.

## The original

Mech3.exe finds its files along search paths relative to the install
(0x5762b0 finds a name on a path list, 0x5763b0 opens it; the interpreter's
path is ".;zbd", 0x5e16c8), and some readers let a loose file override an
archive entry: a `.zrd` next to the exe beats the one in `reader.zbd`
(zreader.md), a `.zan` beats a motion in `motion.zbd` (motion.md). The
developers used both to try changes without rebuilding archives.

## The rebuild (FIX (A))

- **Game paths.** Code asks for the file by the game's own relative path,
  Windows style: `zbd\c1\gamez.zbd`, `zbd\mechlib.zbd`,
  `configs\madcat.mw3`.
- **Mounts.** An ordered list of folders laid out like the install
  (`zutl::mount`, `mount_front`). The first that holds the file wins.
- **Write dir.** Files the game writes (saved loadouts, later pilots and
  settings) go to one folder (`zutl::set_write_dir`, default the first
  mount), which is also searched first when reading, so what the game saved
  wins over a shipped copy.
- Absolute host paths are used as given (tools, tests); with no mounts,
  relative paths are relative to the working directory, as before.
- **Confined** (ours, `Mounts::confined`, set by the program once its
  mounts are up): names come from data, mods, scripts and other players, so
  a name that is absolute (a leading `/` or `\`), holds a `:` (a drive, a
  Windows stream) or has a `..` part finds, opens, writes, lists and deletes
  nothing (`find`, `find_all`, `exists`, `open`, `write_path`, `list`,
  `find_in_mods`), logged once a name. Every game file goes through these,
  so the one check covers the readers, textures, sounds, movies, fonts, the
  interpreter's `source`, delete and mkdir, the pilots and saves, and mod
  worlds. The network carries no names that reach a file: the lobby's
  world is a number (folder index and `readermp` number, from the joiner's
  own list; past the table `mission_dir` gives `none`); mechs and skins
  are numbers too, and names (pilots, sessions, chat, mods) are only shown.
  Tools and tests leave it off. Tests: `mounts_confined_names_stay_inside`,
  `sdl_program_names_stay_inside_the_game_folders`. Outside it: the player's
  own command line and the disc import's `.cue` (the player's files).

Routed through it: the zArchive reader and writer (every `.zbd` archive,
`reader.zbd`, `mechlib.zbd`, `motion.zbd`, `interp.zbd`, the saved
loadouts), the texture libraries, `gamez.zbd` and `anim.zbd`.

`mw3view --assets <dir>` mounts folders (repeatable, first searched first;
default `game/mw3-1.2`). For example, a folder with only
`zbd/t1/gamez.zbd` in it, mounted in front of the install, replaces t1's
world and nothing else.

## Replacing things

Today the unit of replacement is a **whole file**: a modified `rmechtex.zbd`,
a different `gamez.zbd`, a complete set of all of them (then no install is
needed). Planned, finer steps, each in the reader it belongs to:

1. **Single entries inside archives** (the original's own loose-file
   override, generalised): a loose `madcat_walk.zan` or `dfn_madcat.zrd` in
   any mount beats the archive entry. Needs the loose-text `.zrd` tokenizer
   for settings (not ported yet; zreader.md).
2. **Single textures** by name, as PNG (`textures/mcat01.png`), converted on
   load to what the texture code expects.
3. **Models** by node or part name, for the high-res layer planned for after
   the port (CLAUDE.md): a replacement keeps the original part tree, pivots
   and firepoints; gameplay keeps the original geometry.

## Letter case

The game's names were written for Windows, so nothing makes them match a file's
case ("mech3Msg.dll" against the disc's `Mech3Msg.dll`). `zutl::any_case` takes
each part of a path exact if it is there, else one differing only by case, and
`find`, `find_all`, `find_in_mods` and `list` use it per mount, so a mount earlier
in the order still wins though its copy is in another case. The write dir stays
exact: the game made those names itself. A mount's own path goes the same way, as
the game adds named ones ("<install>/zbd/c1"). What checks an install without the
mounts (`install_valid`, `check_install`, the launcher's sounds and notes, the
movie import) uses it too. It scans a folder only after an exact miss, so a right-cased
install costs nothing and there is no list of names to go stale.
