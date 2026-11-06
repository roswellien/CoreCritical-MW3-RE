# Add weapons to MechWarrior 3 1.2's mech lab

`patch_weapons.py` makes a copy of `Mech3.exe` 1.2 with extra rows in the mech lab's weapon
table, so weapons defined in `weapons.zrd` can be fitted to a mech. The example,
`rows-plain-ppc.txt`, adds the plain PPC, which the game defines and names but never offers.

This folder holds no game files, and no patched exe. It stands apart from the rebuild in
this repository; no game build uses it.

Made with AI assistance, by Claude, on Owen's project.

## What you need

- Python 3. Nothing else for the patcher.
- Your own `Mech3.exe`, one of the exes listed in `ACCEPTED` at the top of
  `patch_weapons.py`. Today that is the official 1.2, sha256
  `95bc2c114c9b2e5c5ada8e40ca78cc79470f862e45313ffad0d1e8e6d4d916bf`. Anything else is
  refused, including an exe already patched.
- With the official exe, the disc in the drive or a mounted image: it checks at start.

## Use

    python patch_weapons.py Mech3.exe rows-plain-ppc.txt Mech3-ppc.exe

Run it in your game folder, or give the folder with `--game-dir`, so it can read
`zbd/reader.zbd`. It writes a new file and never changes your exe, and refuses to write over a
file that exists. Keep the patched exe beside the original and start it from there.

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

A row needs a definition already in `weapons.zrd`. This version does not write new definitions
into `reader.zbd`, so for now that means the 12 the game defines without a row. Most of those
are not meant to be mounted (`LANDMINE`, `NUKE`, `lb2xburst`), and only the PPC has been tried.

### Picking a message id

The name is yours to add: put a message in your own edited `Mech3Msg.dll`, at an id its message
table doesn't use. 0x2ee is free in the shipped DLL. Ids 0x2d7 to 0x2fd are the game's weapon
names, and 0x2fd is the plain PPC's. Ids must be below 0x10000.

Pass your DLL so the patcher can check it:

    python patch_weapons.py Mech3.exe rows.txt Mech3-new.exe --msg-dll MyMech3Msg.dll

Without `--msg-dll` it checks the `Mech3Msg.dll` in the game folder. A row whose id isn't in the
DLL it checks is refused. If it finds no DLL at all, it prints a warning and goes on, and
checks neither the ids nor `DESC`.

**A missing message id may crash the lab, so edit the DLL first.** The name comes back null (seen in
the emulator). From reading the code, a weapon then shows as "(null)", but its ammo item passes the null
on to the lab's list. Neither has been run.

## What it can't do

- Go past 49 weapons in all, which is 12 new ones. Every row has to fit in free space inside the
  exe while the file stays the same size. Short names matter too, as they share that space: with
  12 new rows the names have 152 bytes, about 11 characters each. The patcher says so if they don't
  fit.
- Insert or reorder rows.
- Add a new family.
- Add new models or sounds: a weapon uses what the game already has.
- Use a `DESC` or `MILITARY_NAME` that `Mech3Msg.dll` doesn't know: the patcher refuses it, because
  the game would read it as a null name. Leave `DESC` out and the HUD shows the key.

**ZipperFixup:** the patch keeps the exe's size because
[ZipperFixup](https://github.com/TerranMechworks/ZipperFixup)'s DLL checks it: version 0.1.1 installs
its line clamps only for an exe of exactly 2,384,384 bytes. Nothing is taken from ZipperFixup, and
the patch uses no NOP padding or anything else its fixes use. A later version of the DLL may differ;
development has moved off GitHub, and only the public source and 0.1.1 were checked.

A launcher that verifies the exe's hash will refuse a patched exe, and that's for its author to allow.

## Warnings

**Multiplayer:** every player needs the same patched exe, the same `weapons.zrd` and the same names.
Nothing checks this and nothing warns you. Someone on an unpatched exe makes no weapon for a new
row, so that mech's later weapons shift by one, and a shot can fire a different weapon on their
screen. This comes from reading the code; it has not been run.

**A save with a new weapon, opened in an unpatched exe:** in the emulator the loadout loads, the
weapon is named "noweapon" and weighs nothing, and no weapon is made, so the mech just lacks it, with
no message. Both exes use the same `pilots` and `configs` folders, so back those up before trying
this. The game may run from its registered install folder, even if you start the copy from somewhere
else.

## What was tested

As of 2026-11-05:

- **In an emulator (Unicorn), one function at a time, against the unpatched exe:** every original
  weapon and equipment row reads the same, new rows read as listed, the exe's self-check still passes,
  all nine count checks, the lab's full weapon store, a loadout's save, checksum and reload, and nothing
  writes to the moved table. The refusals work, and 21 deliberate breaks of the patcher were all caught.
  This passed on the official exe and, with the hash list extended by hand, on a NOCD 1.2 exe and on the
  same exe with ZipperFixup's import change.
- **Not tested: anything on a real game.** Nothing has been run in Windows yet: not Windows loading the
  patched exe, the lab's screens, saving in play, firing, or ZipperFixup's DLL with a patched exe.
- **Not known:** whether a per-weapon array hides inside some object (the search only sees plain global
  arrays), and how the lab's lists behave past 125 items.

## Running the checks

    pip install unicorn
    python tools/weapon-patch/test_weapon_patch.py

Run from the top of this repository, with the official 1.2 install at `game/mw3-1.2/` (`Mech3.exe`,
`Mech3Msg.dll`, `zbd/reader.zbd`). It uses `tests/differential/original.py`, the repository's emulator
harness. Patched copies go in a temporary folder under `build/` and are deleted. `--base OTHER.exe`
runs it on another exe of the same layout.

WRITEUP.md has how it works, the addresses and what was measured.
