# Add weapons to MechWarrior 3 1.2's mech lab

Two ways to add rows to the mech lab's weapon table, so weapons defined in `weapons.zrd` can be fitted to a mech.
The example, `rows-plain-ppc.txt`, adds the plain PPC, which the game defines and names but never offers.

This folder holds no game files and no built files. It stands apart from the rebuild in this repository; no game
build uses it.

Made with AI assistance, by Claude, on Owen's project.

**Both routes have run on a real game with the plain PPC; large row lists are unproven.** See What was tested.

## Which route

| | Stand-in DLL (`proxy/`) | Patched copy (`patch_weapons.py`) |
|---|---|---|
| What it does | replaces `IFORCE2.dll` with a small DLL that adds the rows in memory when the game starts | writes a new exe with the rows in it |
| `Mech3.exe` | not touched | a changed copy, same size |
| Launchers | works without editing the exe, so a launcher's hash check still passes (run through a launcher that checks the exe's hash: works) | a launcher that verifies the exe's hash will refuse it, and that's for its author to allow |
| Weapons in all | 127 at most, unproven past the lab's lists (What was tested) | 49 |
| Exes | official 1.2, NOCD 1.2, and either with ZipperFixup: anything else is left alone | only the ones in `ACCEPTED` |
| Needs | a built DLL | Python only |
| Antivirus | **may be flagged** | less likely |

Use the stand-in unless you have a reason not to. Use the patched copy if your antivirus objects, or you want one file
you can start beside the original.

**Antivirus:** the stand-in replaces a DLL and changes game code in memory, which is what some malware does, so a
scanner may flag or quarantine it. You can read and build the source yourself. If `IFORCE2.dll` disappears, the game won't
start: restore it (below) or take it out of quarantine.

## The stand-in DLL

    python tools/weapon-patch/proxy_install.py build --out IFORCE2.dll
    python tools/weapon-patch/proxy_install.py install "<game folder>" IFORCE2.dll rows.txt

Building needs mingw (`i686-w64-mingw32-gcc`); the command is in `proxy_install.py`. Install renames the game's `IFORCE2.dll` to
`IFORCE2o.dll`, and copies in the stand-in and your rows as `weapon-rows.txt`. It refuses if either name is taken, and never
overwrites. To undo it, which removes the stand-in and puts the original back:

    python tools/weapon-patch/proxy_install.py restore "<game folder>"

To change your rows, edit `weapon-rows.txt` in the game folder with any text editor. Each time the game starts, the stand-in
writes one line to `weapon-patch.log` beside it, saying how many weapons it added or why it added none, such as
`weapon-rows.txt line 3: family is not 0..14`. **It never stops the game:** a bad row, a missing file or an exe it doesn't
recognise means no changes at all, and the game runs as usual. To check a row file first, against your own `reader.zbd`, DLL
and exe:

    python tools/weapon-patch/proxy_install.py check "<game folder>" rows.txt

## The patched copy

    python tools/weapon-patch/patch_weapons.py Mech3.exe rows-plain-ppc.txt Mech3-ppc.exe

Run it in your game folder, or give the folder with `--game-dir`, so it can read `zbd/reader.zbd`. It writes a new file, never
changes your exe, and refuses to write over a file that exists. Keep the patched exe beside the original and start it from there.

It takes one of the exes in `ACCEPTED` at the top of `patch_weapons.py`. Today that is the official 1.2, sha256
`95bc2c114c9b2e5c5ada8e40ca78cc79470f862e45313ffad0d1e8e6d4d916bf`, which also wants the disc in the drive or a mounted image.
Anything else is refused, including an exe already patched.

## Adding a row

One line for each weapon:

    # name  family  tech    ammo  label
    PPC     3       sphere  no    0x2fd

- `name`: the weapon's key under `BALLISTICS` in `weapons.zrd`, in exact case.
- `family`: 0 to 14. Copy a similar weapon's.
- `tech`: `clan`, `sphere`, `both` or `none`.
- `ammo`: `yes` or `no`.
- `label`: the lab's name, as a message id in your `Mech3Msg.dll`.

Always add at the end of your list and keep the order: saved loadouts hold a weapon's position.

A row needs a definition in `weapons.zrd`. The game has 12 without a row, most not meant to be mounted (`LANDMINE`, `NUKE`,
`lb2xburst`); only the PPC has been tried. `ammo` has to match the definition: in every row the game has, `yes` means its `MASS` has an `ammo` entry, so the tools refuse
a mismatch, and each tech in the row needs its own `MASS` entry.

### Adding a definition

Use [mech3ax](https://github.com/TerranMechworks/mech3ax)'s `unzbd` and `rezbd`, which read and write reader archives. Keep a copy of the
original `zbd/reader.zbd`:

    unzbd mw reader "zbd/reader.zbd" reader.zip
    (in reader.zip, edit weapons.json: add your weapon's name and list at the end of BALLISTICS)
    rezbd mw reader reader.zip "zbd/reader.zbd"

Copy a similar weapon's list and change what you want: `DAMAGE`, `MASS` (tons and slots, for `clan` and `sphere`), and so on. It reuses that weapon's model and sounds. Keep whole numbers and decimals as the
original has them, as the game tells them apart. Leave out `DESC` and the HUD shows your weapon's name as you spelled it; or give a `DESC` your DLL knows. Then add its row, and try it with `proxy_install.py check`.

### Picking a message id

The name is yours to add: put a message in your own edited `Mech3Msg.dll`, at an id its message
table doesn't use. 0x2ee is free in the shipped DLL. Ids 0x2d7 to 0x2fd are the game's weapon
names, and 0x2fd is the plain PPC's. Ids must be below 0x10000.

The stand-in reads the `Mech3Msg.dll` beside it and takes no rows if an id is missing. The patched copy's tool checks the game folder's, or
one you pass:

    python patch_weapons.py Mech3.exe rows.txt Mech3-new.exe --msg-dll MyMech3Msg.dll

A row whose id isn't in the DLL it checks is refused. If it finds no DLL at all, it prints a warning and goes on, and checks neither the ids
nor `DESC`.

**A missing message id may crash the lab, so edit the DLL first.** The name comes back null (seen in
the emulator). From reading the code, a weapon then shows as "(null)", but its ammo item passes the null
on to the lab's list. Neither has been run.

## What it can't do

- Go past 127 weapons with the stand-in (itself unproven: see What was tested), or 49 with the patched copy, which is 12 new ones. The patched copy has to fit every row
  in free space inside the exe while the file stays the same size, and short names matter too, as they share that space: with 12 new
  rows they get 152 bytes, about 11 characters each. It says so if they don't fit.
- Insert or reorder rows.
- Add a new family.
- Add new models or sounds: a weapon uses what the game already has.
- Use a `DESC` or `MILITARY_NAME` that `Mech3Msg.dll` doesn't know: the patcher, `install` and `check` refuse it, because the game would read
  it as a null name. Leave `DESC` out and the HUD shows the key.

**ZipperFixup:** the patched copy keeps the exe's size, and the stand-in leaves the exe alone, because
[ZipperFixup](https://github.com/TerranMechworks/ZipperFixup)'s DLL checks it: version 0.1.1 installs
its line clamps only for an exe of exactly 2,384,384 bytes. Nothing is taken from ZipperFixup, and
neither route uses NOP padding or anything else its fixes use. A later version of the DLL may differ;
development has moved off GitHub, and only the public source and 0.1.1 were checked.

## Warnings

These apply to both routes.

**Multiplayer:** every player needs the same rows, the same `weapons.zrd` and the same names.
Nothing checks this and nothing warns you. Someone without them makes no weapon for a new
row, so that mech's later weapons shift by one, and a shot can fire a different weapon on their
screen. This comes from reading the code; it has not been run.

**A save with a new weapon, opened without the rows** (an unpatched exe, or the stand-in removed): the game drops the weapon with no
message. In a real run the loadout stayed, with no row or name for the weapon, and the mech's weight no longer counted it. So keep a copy of
your `pilots` and `configs` folders. Both exes use the same ones, and the game may run from its registered install folder, even if you start the copy
from somewhere else.

## What was tested

As of 2026-11-05:

- **In an emulator (Unicorn), one function at a time, against the unpatched exe:** every original weapon and equipment row reads the same,
  new rows read as listed, the exe's self-check still passes, all nine count checks, the lab's full weapon store, a loadout's save, checksum and
  reload. This passed on the official exe and, with the hash list extended by hand for the patched copy, on a NOCD 1.2 exe and on the same exe
  with ZipperFixup's import change.
- **The patched copy:** nothing writes to the moved table, and the refusals work. 21 deliberate breaks of the patcher were all caught.
- **The stand-in, with the built DLL mapped in the emulator and kernel32 stubbed:** the code pages go back to read-and-execute and the table is
  read-only, and with those really enforced nothing writes to either. It changes nothing on Pirate's Moon's exe, an on-disk patched exe, or an exe that
  differs at any one site, or for an empty or bad row file. Its exports forward to the original. 37 deliberate breaks of its source were all caught. The
  store held 394 items with 127 weapons.
- **On a real game, the patched copy with the plain PPC** (Windows 11 with DDrawCompat): on the official exe with a mounted disc image, and on a NOCD exe with
  ZipperFixup 0.1.1's DLL, the PPC is in the lab, last in the list, at 7 tons and 3 slots. It saves, reloads and fires.
- **On a real game, the stand-in with the plain PPC:** on the same two exes, 38 weapons, and the PPC fits, saves, reloads and fires. A bad row leaves the game running as usual with a clear
  log line, and `restore` puts the original back.
- **127 weapons, unproven.** With 90 made-up rows, 394 store items, the lab's weapon and ammo lists opened and scrolled to the end on a real game. Then, after Accept on the lab and the next click, the game crashed
  once (0xc0000005 at `Mech3.exe+0x89d19`, on the Instant Action screen's enemy list). That code uses the screen's wave numbers, not the weapon table, and reading finds no limit on row count, so the cause is not known. The original
  also crashes easily on Windows 11. Until that is settled, treat large lists as a risk.
- **On a real game, through a launcher that checks the exe's hash:** the stand-in works, with the PPC fitted, saved, restarted, fired and damaging a target. A patched copy is refused there.
- **A new definition, in the emulator:** one added to `weapons.zrd`, copied from the PPC with its own weight and damage, is read by the game's own code on both routes. Its name, HUD text, weight, slots, ammo, damage and place in the store all line up, and no other
  definition changes. 5 deliberate breaks were caught. On an exe without its row, it's only one more definition.
- **Not tested:** `unzbd` and `rezbd` here, as we haven't run them on this. On a real game, rows beyond the plain PPC, and whether its line clamps went in (they can't be seen directly).
- **Not known:** whether a per-weapon array hides inside some object (the search only sees plain global arrays), and what caused the crash above.

## Running the checks

    pip install unicorn
    python tools/weapon-patch/test_weapon_patch.py
    python tools/weapon-patch/test_proxy.py
    python tools/weapon-patch/test_new_weapon.py

Run from the top of this repository, with the official 1.2 install at `game/mw3-1.2/` (`Mech3.exe`, `Mech3Msg.dll`, `IFORCE2.dll`,
`zbd/reader.zbd`). They use `tests/differential/original.py`, the repository's emulator harness; `test_proxy.py` also needs mingw and takes about
four minutes. Anything built goes in a temporary folder under `build/` and is deleted. `--base OTHER.exe` runs either on another exe of the same layout.

WRITEUP.md has how it works, the addresses and what was measured.
