# Mods

For additive weapons, equipment, chassis, worlds, missions, campaigns and native
HUD layouts, use the shared [modular content system](modular-content-system.md).
The `mw3content` CLI validates, compiles, packages and resolves profiles for the
game, launcher and Studio. The legacy folder overlays described below remain
available separately; managed profiles import only the explicit adapters in
[legacy content adapters](legacy-content-adapters.md).

Ours (Owen, 2026-11-01: "players and hobbyists can add their own game data
without any change to the exe later"; small, "nothing big and dramatic").
With no mods enabled nothing is mounted and nothing in play changes; nothing
is ever written into the install or into a mod.

## A mod (M1, 2026-11-01)

A folder in the settings folder's `mods\` (the write dir: on Windows
`%APPDATA%\MicroProse\MechWarrior 3\mods`, on the Mac `~/Library/Application
Support/MicroProse/MechWarrior 3/mods`), laid out like the install
(`zbd\...`, `video\...`), with a `mod.txt`:

```
name=Hard Rain
version=1.0
author=Someone
for=mw3 1.2
description=Rain on every world.
```

`name` is required; `for`, when given, must start with `mw3` (a Pirate's
Moon mod is left out of MW3). Other keys are ignored, `#` starts a comment.

**Enabling.** `mw3.cfg` in the settings folder, one line a mod, the folder's
name, in order (the first wins where two hold the same file):

```
mod=hardrain
mod=newmaps
```

`mw3 --mod NAME` (repeatable) uses that list for one run instead,
`--no-mods` none, `mw3 --mods` lists the mods found and which are enabled.
At start the log names each enabled mod and every file it replaces or adds;
a name that is no usable mod (no folder, no `mod.txt`, no `name`, another
game) is said and left out.

**Where it sits.** The settings folder's own files first (what the game
saved: a player's files always win), then the enabled mods in order, then
the imported 1.2 patch (`patch12\`), then the install (`zutl::mount_mod`,
assets-layer.md). The world folders a mission adds (`zbd\<world>\`) come
from every mount, the mods' too.

**Game data only.** A mod never serves the player's own files: `pilots\`,
`configs\` (saved loadouts), `keys\` (controls), `saves\`, `mw3.cfg`,
`settings.reg`, the logs and `movie-shapes.txt` (`zutl::mod_may_serve`).
Such files in a mod are listed as ignored.

**Replacing.**

- A whole file: the mod's copy of any game file (an archive such as
  `zbd\c1\gamez.zbd`, `zbd\rmechtex.zbd`, `zbd\reader.zbd`) is read instead.
- One entry of a reader archive, as text (ours, mods only): a loose `.zrd` in
  the mod, in the archive's own folder, is read instead of that archive's
  entry (`zbd\fmv.zrd` for `zbd\reader.zbd`'s fmv.zrd; `zbd\c1\world.zrd` for
  the c1 world's reader). The game's own rule (zreader.md) reads a loose file
  only when no archive has the name; inside a mod the loose copy comes first.
  Each such file is named in the log; one that does not parse leaves the
  archive's entry in use.
- mech3ax (TerranMechworks, `unzbd` / `rezbd`) unpacks the archives to JSON
  and PNG and packs them again byte for byte: reader, image and texture,
  sound, interp, mechlib, motion, gamez and anim archives. It reads
  Mech3Msg.dll but cannot rebuild it (M2's `strings.txt` is the way to change
  text).

**Multiplayer.** Every player's gameplay data must match. A number over the
enabled mods' gameplay files (`mods_gameplay_hash`) travels in the lobby's
player data, with the names of the mods that count (`mods_identity`, set at
start-up). A joiner whose number differs from the host's is sent back with
"Your mods don't match the host's. The host uses: X. You use: Y.", and LOCK
refuses while any player's differs, naming theirs and ours. Strings,
images, sounds, music and movies do not count, so a translation or a
texture pack never blocks a game; with none on either side nothing changes.
The block's layout: multiplayer-port.md, the mods check.

Tests: `test_mods.cpp` (mod.txt; the folders found; the mounts in order,
the first winning, unusable names left out, nothing served for pilots or
mw3.cfg, writes to the settings folder; a loose fmv.zrd over reader.zbd's,
the archive's own without the mod or with a broken copy; the gameplay
number ignoring textures, sounds, strings and music).

## New worlds and words (M2, 2026-11-01)

**A new world.** `world=<folder>` in mod.txt (as many as the mod has):

- the world's files in the mod's `zbd\<folder>\`, as an install's world
  folder holds them (`gamez.zbd`, `anim.zbd`, `reader.zbd`, `readerm<n>.zbd`
  for missions, `readeria<n>.zbd` for Instant Action, `readermp<n>.zbd` for
  multiplayer);
- its build script `<folder>.gs` at the mod's root, a text script read
  where `interp.zbd` has none (the game's own fallback, interp.md). It can be
  a whole script or `source` another world's (`source c1.gs`).

The game's worlds are the nine folders of 0x00484c50 (c1 c2 c3 c4 c4b t1 lab
dev1 dev2, by number); a mod's are numbered on from 9 in mod order
(`mission_dir`, `mod_worlds()`). A folder name that is already a world (any
case), has a slash, dot or space, or has no `zbd\<folder>\gamez.zbd` in the
mod is said and left out.

**Instant Action.** `ia_world=<folder> <name>` puts a world on Instant
Action's list after the original's four, named `<name>` (UTF-8, shown in the
game's code page; the folder when none). The world can be the mod's own or
any other's. Its maps are its `readeria<n>.zbd`: the screen's scenarios
pick `n` and step down to the nearest the world has, as for the game's own
worlds. A pilot keeps the chosen world by number; with that mod gone the
Action screen shows the first world and a launch asks for c1, said in the
log (ours: the original would load a world named "none").

**Multiplayer.** The lobby's add-on scan (every `readermp<n>.zbd`, `n` over
2, named by its `mpinfo.zrd`'s `db_name`) looks in the first six folders
and, ours, in every mod world (9 on); t1, lab and dev1/dev2 stay left out.
A mod world's map is an add-on world like c3's: listed in the player's
lobby data, `**` beside a player who lacks it, and LOCK refused for it.
Test: `net_lobby_lists_a_mod_worlds_maps_headless`.

**Words.** `strings.txt` at the mod's root: `id=text` a line, over
Mech3Msg.dll's messages (the first enabled mod's line wins). The id is a
number (decimal or `0x` hex, as mech3ax lists them) or a message's name;
the text is UTF-8, shown in the game's code page (Windows-1252: a character
it lacks leaves the line out, said in the log), `\n` a line break, `\\` a
backslash, `#` a comment line. Cut to the original's 511 characters.

Tests: `test_mods.cpp` (world= and ia_world= read, a good world numbered 9,
a name taken, without files, with `..` and an unknown Instant Action world
left out; strings.txt: the first mod's line, hex ids and names, UTF-8 to
Windows-1252, `\n` and `\\`, a character the game lacks and lines without
an id left out, nothing without mods) and `mod_world_plays_in_instant_action_headless`
(a mod world of c1's files linked, `source c1.gs`, Instant Action on world
9: its readers read from `zbd\mymap\`, the player's mech in play).

## Broken files and checking a mod (M3, begun 2026-11-01)

**Names stay inside the game's folders** (2026-11-02): a name a mod's files
give (a texture, a sound, a movie, a script's `source`, a world folder) is
looked up only inside the mounts; one with `..`, a drive or a full path
reads, writes and deletes nothing, and the log says so
(docs/assets-layer.md, Confined).

**The reader archives' entries, bounded.** Every `reader*.zbd` entry (a
mission's objectives, a mech's definition, the worlds' settings) is read
through `zrdr::load` with the entry's own length known: a list whose count
the bytes left cannot hold (every node is at least 8 bytes), a node of no
type, or lists nested past 64 deep mark the entry broken. It is then read as
no entry at all, and the log names the archive and the entry. The original
read on into the next entries or allocated what the count said; the shipped
entries read exactly as before (`test_zreader.py`, 178 files identical;
`test_zreader_text.py`, 540; `test_ai_readers.py`). Already bounded before
M3: the archives' tables (`zutl_zar`: a count or a length the file cannot
hold), strings (to the file's end), the text `.zrd` parser (an unclosed
list), `gamez.zbd` (a bad signature, version or a cut file), the AI files,
the mech definitions and the saved loadouts.

**`mechlib.zbd`, bounded** (2026-11-01): the materials table and each
model tree are read with the entry's end known (`cls_mechlib.cpp`): a count
the bytes left cannot hold (materials, vertices, normals, morphs, polygons,
a polygon's arrays, a node's children), a node of another class than
object3d (the exe dereferenced null), a tree deeper than 64, or a polygon's
vertex, normal or material index past its array refuse the tree (or the
materials), named in the log; a refused tree is a mech without its model.
`test_mechlib.py` still reads all 770 models, 442 materials and 57 trees as
the original; `bounded_mechlib_reads_the_shipped_trees_whole` (57 of 57)
and `bounded_mechlib_fuzz_never_crashes` (210 trees cut short, bytes flipped
or words made huge, and 30 broken material tables: none crash, every one
cut short refused; without the bound it crashes at once).

**`motion.zbd`, bounded** (2026-11-01): a motion is read with its entry's
end known (`zsis::Script::read`, `load_motion`): a frame count of 0 (the
loop time divides by it), a frame or part count the bytes left cannot hold,
a loop time not over 0, or a part's keys past the end refuse the motion
(its parts freed, named in the log); a refused motion is a missing one.
`test_motion.py` still reads all 258 as the original, each to its exact
end; `bounded_motion_reads_the_shipped_motions_whole` (258 of 258) and
`bounded_motion_fuzz_never_crashes` (300 copies: none crash, every one cut
short refused; without the bound none is refused).
A refused or missing motion is then taken from the next mount's copy of
`motion.zbd` (normally the install's: `load_motion` over `zutl::find_all`),
said in the log with the mod's archive and the motion's name; the rest of
the mod's motions stay its own. A gait a mod broke, with no whole copy
anywhere, refuses the mech (`load_animations`, `motion_broken`; the log
names the mech and the motion), and so does a step or walk missing from
every copy (`load_animations` reads both right away, a crash in the exe
too): the mech's set-up fails without the data
box 0xf2d, so an AI mech is only a log line, and the player's mech gets the
launch's error box (0xf30) and the briefing's Launch does not go on. Any other gait
missing from every copy is not refused: the exe ran with it null, as the
shipped Elemental does (there is no `elemental_limp`; 2026-11-02, the first
rule refused it and the campaign's Elementals with it). A mech without its
gaits can no longer crash (`pose_jump`, `load_animations`).
Tests: `bounded_motion_falls_back_to_the_next_copy` (madcat_walk cut short
in a mod: the install's frames, refused with no other copy, the mod's other
motions its own) and `mod_broken_motion_falls_back_or_refuses_the_mech_headless`
(Instant Action in a Mad Cat: it plays on the install's walk; with an install
of links lacking motion.zbd it is refused, no player mech, no crash; without
the check the refusal case segfaults).

**The worlds' `anim.zbd`, bounded** (2026-11-01; a whole file, not an
archive): every count and size it holds (its source list, each def's
lists, blobs and sequences, the messages) is checked against the bytes left
in the file before anything is allocated (`zeff_anim_init.cpp`); a broken
copy, or one cut short, is refused whole, named in the log, and the world
has no animations (the original left what it had read in place, half
bound). A def that cannot be bound to the scene (a lookup) still fails the
original's way. `test_anim.py` (6 worlds and the 10 mismatched and failing
runs), `test_anim_reset.py` and `test_anim_binder.py` as the original;
`bounded_anim_fuzz_never_crashes` (150 copies of c1's: none crash, every one
cut short refused; a source count of 0xfffffff0 refused before any
allocation, where unbounded the run passes 4 GB and is stopped). What the
anim run later makes of a garbled event stream inside a well-sized blob is
not checked here.

**The texture and image libraries, bounded** (2026-11-02; `rimage.zbd`,
`rtexture*.zbd`, `rmechtex*.zbd`, one format, not zar archives:
`zimg_library.cpp`). The table's counts must fit the file (the exe
malloc'd and read them as they were); an entry's name ends inside its 32
bytes; an entry's offset lies past the table and palettes and inside the
file, its global palette one the library has (the exe indexed the shared
palette table with it unchecked); a texture's bytes run to the next
texture's offset or the file's end, and a width, height or palette count
below 0, or pixels, an alpha plane or a palette past them, refuse the
texture (null, named in the log: the exe returned a half-read texture). A
refused library is a missing one; a refused texture is one the library
lacks. `test_texload.py` and `test_shadow_image.py` as the original;
`bounded_textures_read_the_shipped_libraries_whole` (22 libraries, 17,644
textures, each the same pixels as the exe's read) and
`bounded_textures_survive_broken_copies` (300 copies of a 12-texture
library: no crash, every cut copy keeps exactly the textures whose bytes it
still has; table counts past the file or below 0 and an entry's offset or
palette out of range refused).

**`gamez.zbd`'s texture directory, bounded** (2026-11-02,
`zimg_texture.cpp`): each entry's mip index must be -1 or a row of the
table; one outside refuses the directory (the table as it was, named in the
log), and the world fails to load as for a short read ("Error reading GameZ
texture data."). The exe made a pointer of any index and followed it when
the texture loaded. `test_gamez.py` and `test_texload.py` as the original;
`bounded_gamez_texture_directory_mips` (300 garbled copies of c1's 553
entries: every one with an index outside refused, every one read with each
mip inside the table; without the bound none is refused).

**The sounds, bounded** (2026-11-02; `soundsL.zbd`, `soundsH.zbd` and loose
WAVs: `zsnd_create.cpp`). After the chunk walk, a WAV whose samples or cue
points run past its bytes, or whose format no output can play (0 or more
than 8 channels, 0 or more than 32 bits, a rate of 0 or over 384 kHz, a
block or byte rate of 0), is refused, named in the log, and is a sound not
loaded (the exe copied the samples as the data chunk said, from past the
buffer). `bounded_sounds_read_the_shipped_archives_whole` (1,998 sounds and
10 loose WAVs, each buffer its own samples) and
`bounded_sounds_survive_broken_copies` (300 cut, flipped or garbled sounds:
no crash, all 99 cut inside their samples refused; without the bound all
300 are made).

**`interp.zbd`'s scripts, bounded** (2026-11-02, `zinterp.cpp`). An entry's
name ends inside its 0x78 bytes; a script whose offset lies outside the
file is not the archive's (the loose file's turn, as for a script the
archive lacks; the exe seeked there); a script runs to the next one's start
or the file's end, and a line past that ends the script there, said in the
log (the exe read on into the next script). Each include keeps its own end.
`test_interp.py` as the original; `bounded_interp_reads_the_shipped_scripts_whole`
(44 scripts, 2,504 lines, as unbounded) and `bounded_interp_survives_broken_copies`
(300 copies of a 10-script archive: no crash, every cut copy reads its whole
scripts as before; a first line reaching into the next script ends the
script there, the others unchanged; an offset past the file refused).

**Every format a mod can replace is now read with its end known** (M3 complete, 2026-11-02): a broken file or entry is refused and named in the log, never read past its end.

**`mw3 --check-mod DIR`** reads a mod as the game would without mounting it
into play, and exits 1 when anything would be refused, else 0. It reports:
mod.txt; each `world=` (its `zbd\<folder>\gamez.zbd` and `<folder>.gs`)
and `ia_world=`; every file and what it replaces in the install (mw3.cfg's
`install=`) or adds; the player's own kind of file, ignored; every archive's
table; every reader archive's entries through the bounded reader; a
`mechlib.zbd`'s materials and every tree, a `motion.zbd`'s every motion, an
`anim.zbd`'s read, every texture and image library's textures, every sounds
archive's sounds and `interp.zbd`'s scripts through theirs; every loose
`.zrd` through the text parser; `strings.txt`'s lines the game would
leave out (warnings). Test: `mods_check_mod_reports_what_the_game_would_refuse`
(a good mod with a copied reader, a loose fmv.zrd and strings: no errors; a
bad one: no name, a world without files, an unknown Instant Action world, a
non-archive `.zbd`, a reader entry claiming a billion nodes and an unclosed
`.zrd`: six errors) and `mods_broken_reader_entries_are_refused` (a list of
a billion, a type 9, 100 lists deep, a list running into the next entry:
each refused, the entries around them read), and
`bounded_check_mod_reads_textures_sounds_and_scripts` (shipped copies of a
library, interp.zbd and six sounds pass; a library cut by 5,000 bytes names
its last four textures, a script offset past the file and a sound cut in
half are named: six errors).

## Planned
- M3 (v1.0), the rest: bounded readers for mechlib, motion and anim
  (above), each with `--check-mod` looking inside.
- After v1.0: a custom campaign, new mechs and weapons (the exe's name
  tables made data), loose `.zan` motions, the launcher's MODS list.
