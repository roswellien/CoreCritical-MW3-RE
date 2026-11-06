# Patching Mech3.exe 1.2 for new weapons: the write-up

How the two routes work and how they were checked. `README.md` is the short version, with
what was tested. This is separate from the rebuild: no game build uses anything in this folder.

**Measured** means computed from the bytes or run in the emulator; **read** means from the
disassembly or the decompiled code. README.md says what has run on a real game.

## What it changes

The lab's weapon table, 0x5e64f0, is 37 rows of six dwords: name, message id, family, index (its
position; 0x4848e0 returns it), tech mask, uses ammo. `weapons.zrd` is not the limit: 0x583880 reads every
`BALLISTICS` entry. Nothing was free after the table, as the equipment table starts at 0x5e6868.

So the equipment table moves to .rdata's slack, and the weapon table grows in place over where it was.

| What | Where | Bytes |
|---|---|---|
| Count, `cmp r32, imm8` | 0x44b37b, 0x45de3b, 0x45deef, 0x470ffe, 0x484904, 0x484924, 0x484944, 0x484964, 0x4b4bfe | 9 |
| The weapon walk's end | 0x4848cf | 4 |
| Equipment references, by row offset | 0x4849f7 +0, 0x4849a7 +4, 0x485366 +8, 0x48498f +0xc, 0x484a0f +0x10 | 20 |
| The equipment walk's end | 0x4849e2 | 4 |
| Virtual sizes | .rdata's always; .text's only if names reach its slack | 4 to 8 |
| The equipment table, 300 bytes | 0x5df8b4 in .rdata's slack | new |
| New rows and names | 0x5e6868 on, then .rdata's and .text's slack | new |

The file stays 2,384,384 bytes with the same four sections and SizeOfImage. The virtual sizes stay inside the pages
they had, so the loader maps the same pages. The patcher refuses to write anything else. Measured for the plain PPC:
265 bytes differ from the official exe.

Measured, on the official, NOCD and fixup exes: .rdata's 334 bytes (0x5df8b2..0x5dfa00) and .text's 104 (0x5baf98..0x5bb000)
are all zero, and no dword in any section points into them.

**Finding every use.** A scan of every section for dwords in the two tables' ranges finds those above and two others that
don't belong to them: 0x48450b (the roster loop's end) and 0x4854dd (the self-check's start, 0x5e6998). The weapon table's
seven read references keep their addresses. The other 0x25s are not weapons (read): 0x41160e and 0x426d8a are ids in runs
of registration calls; 0x44bcb9, 0x44c728, 0x496488 unwind states; 0x576958, 0x576a54 a `rep movsd`; 0x593b2a,
0x595705.. `'%'` for strchr. The 0x24 and 0x23 compares are string lengths and a switch.

**Nothing writes the equipment table,** which matters since .rdata is read-only. All five references and the walk read (read);
none hands out the table's address, only a field's value. In the emulator, a watch on the moved table and the slack saw no
write through every check here, and a break shows the watch fires.

**The cap.** 49 rows, because all rows have to fit in the free space while the file stays the same size: 1,192 bytes from
0x5e64f0 to the self-check's range at 0x5e6998, at 24 bytes a row. Names share what is left of it, plus 32 bytes in .rdata and
104 in .text. A larger table needs the file to grow, with a new section (the compares allow 127 rows, being signed imm8, and
PE headers have the room), or a row table moved to some other unused space of that size, which wasn't found. A new
section turns off ZipperFixup's hooks unless the DLL's size check goes away. An earlier build of this patcher worked that way.

## Which exes

The patcher takes the official exe only, in `ACCEPTED`, a one-line list. Many players run a NOCD exe, and whether to accept
others is an open decision. Measured, for that decision, with the list extended by hand and every check here run on each:

| Exe | sha256 | Differs from official | Checks |
|---|---|---|---|
| 1.2 NOCD | `46cc4b02...` | 112 bytes, none at a patch site, the tables, the slack or the self-check's ranges | all pass |
| ZipperFixup's install of that | `343af077...` | those, and 8 bytes renaming the import KERNEL32 to ZIPFIXUP | all pass |

**ZipperFixup** (read in its public source, GPL v3, and confirmed in the 0.1.1 DLL; no code taken from it). Its exe step renames one
import and nothing else. At run time the DLL replaces the line-drawing pointers 0x8008c0 and 0x8008c4, calls 0x563b80 and 0x563c50,
and reads the screen size at 0x8007cc and 0x8007d0: none touched by this patch (measured), and neither uses padding. It installs those
hooks only if the exe's file is exactly 2,384,384 bytes, which is why this patch keeps that size. Its export list covers the exe's kernel
imports, and this patch adds none. Its installer wants exactly one `KERNEL32.dll` string, which stays (measured). The NOP-hopping code at
0x5a7575 is the NOCD crack's line clamp, not ZipperFixup's. A later DLL may differ.

## Names

Lab name: a row's +4, through FormatMessageA on Mech3Msg.dll (0x576040; its one reader is 0x48492c). An id the DLL lacks comes back
null (measured). The weapon item's 0x473900 prints that through `%s`, "(null)" on the real C runtime (read). The ammo item's 0x4739c0
passes the null to the lab's list, which may crash (read, not traced). So the patcher refuses an id missing from a DLL it can see
(`--msg-dll`, else the game folder's) and warns when it sees none.

HUD name: `DESC` in weapons.zrd, through the DLL's exported lookup ZLocGetID (0x575f90, 0x575fb0). Without DESC the HUD shows the key.
A DESC it doesn't know is strdup(NULL) at 0x583ae8 (read), so the patcher refuses one when it can see the DLL.

Data: weapons.zrd is in `zbd/reader.zbd` only (measured). It has 49 defs; the 12 without a row are PPC, the four lb*burst, LRM_BG,
mine_turr, LANDMINE, NUKE, GASTANK, CAUSECRATER and TAG_MISSILE.

A launcher that verifies the exe's hash will refuse a patched exe, and that's for its author to allow.

## The traps

- **Exe self-check 0x4854c0.** Covers 0x5e6078..0x5e64ec and 0x5e6998..0x5e6b14; the patch stays between them. Measured: true on every
  patched image, on all three exes, and false with a byte changed in its range or at the space's end.
- **Loadout checksum 0x4255b0.** Measured: an old loadout's checksum and saved bytes are the same on either exe; a new index gives the same
  checksum and bytes on either exe, a different checksum from the old loadout, and reloads with the checksum matching on both. The packed
  form holds the index in 16 bits (0x425650, read). Validate's weight and slot checks were stubbed. Changing a weapon's mass after saving
  can fail them, as it can today (read).
- **Rules check 0x46fbd7.** A CRC of the 16-byte lobby block; unaffected, and it notices nothing about weapons (read). So nothing detects
  mixed players. An unpatched peer makes no weapon for a new index, so later slots shift by one on its screen; a slot past the end is refused
  (0x4b08c0) (read, not run).
- **Disc check 0x482f90.** Unaffected, so still there on the official exe: it wants `video\c1.avi` on a CD-ROM drive. A mounted ISO should count.
- **The speed test 0x5988c0.** Not changed. It divides by zero where the performance counter runs at the CPU's clock (read). On the one Windows 11
  PC measured the counter runs at 10 MHz, which gives 100 µs by the formula; not yet run.
- **`RunDir`** (`HKLM\Software\Zipper\MechWarrior3\1.0`, 0x4828d0): where a real install sets it, the game changes to that folder, so a patched
  copy elsewhere uses that install's pilots and loadouts.

## The patch as data

Enough to make the same changes at run time from a DLL instead of on disk. These are this project's offsets and values for
Mech3.exe 1.2; all addresses are virtual, with the image at 0x400000 (no relocations). `n` is the total number of weapons.

**Rows.** New rows follow the game's 37 at index 37, 38.. in the table, six dwords each, 0x18 bytes:

| Offset | Field | Value for a new row |
|---|---|---|
| +0x00 | name | pointer to the key under BALLISTICS in weapons.zrd; must stay valid |
| +0x04 | message id | id in Mech3Msg.dll, below 0x10000 |
| +0x08 | family | 0..14 |
| +0x0c | index | its own position |
| +0x10 | tech mask | 1 clan, 2 Inner Sphere |
| +0x14 | uses ammo | 0 or 1, read as a byte |

**The count,** the imm8 at each address + 2, from 0x25 to `n`:

| `cmp` at | Why |
|---|---|
| 0x484904, 0x484924, 0x484944, 0x484964 | the name, label, tech and ammo accessors' bound |
| 0x45de3b, 0x45deef | objectives.zrd's `weapon_fire` and `weapon_select` conditions' bound |
| 0x470ffe | the store fill's loop |
| 0x44b37b | the lab's loadout summary loop |
| 0x4b4bfe | the AI ready times' reset loop |

**Where the table lives.** The dword at each address, a pointer into the table:

| Field at | Instruction | Points at | Why |
|---|---|---|---|
| 0x4848b8 | `mov esi, imm32` | table +0x00 | the lookup by name starts |
| 0x4848cf | `cmp esi, imm32` | table + `n` x 0x18 | the lookup by name ends |
| 0x4848e3 | `mov eax, [8*ecx + disp32]` | table +0x0c | the index it returns |
| 0x48490f | `mov eax, [8*eax + disp32]` | table +0x00 | the name accessor |
| 0x48492f | `mov ecx, [8*eax + disp32]` | table +0x04 | the label accessor |
| 0x4853c6 | `mov eax, [8*eax + disp32]` | table +0x08 | the family accessor |
| 0x48494f | `mov eax, [8*eax + disp32]` | table +0x10 | the tech accessor |
| 0x48496f | `mov al, [8*eax + disp32]` | table +0x14 | the ammo accessor |

On disk, the table stays at 0x5e64f0, so only 0x4848cf changes, and it needs the room below. The patch then also moves the
equipment table, 15 rows of 0x14 bytes from 0x5e6868 to 0x5df8b4, which a run-time version doesn't need:

| Field at | Points at | Why |
|---|---|---|
| 0x4849f7 | table +0x00 | the kind it returns |
| 0x4849a7 | table +0x04 | its lookup by name starts |
| 0x4849e2 | table +0x04 + 15 x 0x14 | that lookup ends |
| 0x485366 | table +0x08 | |
| 0x48498f | table +0x0c | the label |
| 0x484a0f | table +0x10 | the tech |

**At run time.** A DLL copies the game's 37 rows into its own memory, adds the new ones, and writes all eight table pointers
above and the nine counts, before the game first uses the table. The exe's file doesn't change, so its hash still matches, and a
launcher that checks the hash still accepts it (not yet run). Its own memory means no 49-row limit, and no need to move the equipment table. The
compares still stop it at 127 unless they are rewritten, being signed imm8. The self-check doesn't cover any of these addresses (measured). All eight
references only read the table, so its memory is read-only (measured below). `proxy/` is that DLL.

## The stand-in DLL

`proxy/iforce2_proxy.c`, a freestanding C DLL with kernel32 its only import, and `iforce2_proxy.def`, which forwards all 10 of `IFORCE2.dll`'s exports by name and
ordinal to the renamed original (checked against the real DLL's export table). Build:

    i686-w64-mingw32-gcc -shared -nostdlib -nostartfiles -ffreestanding -fno-stack-protector -fno-builtin \
      -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -O2 -Wall -Wextra -s \
      -Wl,-e,_DllMain@12,--image-base=0x62000000,--dynamicbase,--no-insert-timestamp -o IFORCE2.dll iforce2_proxy.c iforce2_proxy.def -lkernel32

No timestamp is linked in, so the same source and compiler give the same file.

**Why IFORCE2.dll.** The exe imports it by name from its own folder, so the DLL loads before the exe's entry point (read), and no fix or wrapper we know of
uses it. It isn't a system DLL. `ddraw.dll` is DDrawCompat's or a wrapper's, and `Mech3Msg.dll` is what modders edit.

**In order, in DllMain:** every site holds Mech3.exe 1.2's value, each count's opcode too, with the image large enough to read them, or nothing changes. The row file: at most
64 KB of plain text, the patcher's grammar, at most 127 weapons, no name the game has or twice; one bad line refuses the file. Each message id is in the `Mech3Msg.dll` beside it, read from the file
with every offset checked. The table is built and made read-only. Each site's page goes writable, is written and goes back. If a protection call fails, it undoes what it did. It frees all else, and writes one log
line. It always returns TRUE. `weapons.zrd`'s definitions are not read at load: a row without one makes no weapon; `proxy_install.py check` tests that beforehand.

`proxy_install.py`'s `install` and `restore` rename `IFORCE2.dll` to `IFORCE2o.dll` and back, never overwrite, and remove only a file holding the stand-in's marker.

## 0x4b4bd0

No PM function matches it, so it was traced (measured). It is slot +8 of the vtable 0x5c90f8 of a static object at 0x60b220 (read). It walks the
indices from -1 to the count: -1 reads "noweapon", which finds no definition; each row's WeaponType +0xc4, the time the AI may fire that type again,
goes to 0. A new row is reset like the others. A definition with no row is left alone, as it is today.

## Tests

`test_proxy.py` maps the built DLL in the emulator with kernel32 stubbed, Windows' page protection applied for real, and runs its DllMain. On each of the three exes:

- The same row, equipment, count, store fill, timer, loadout, self-check and unpatched-save checks, with 1, 2 and 90 new rows. The code pages are read-and-execute and the table read-only throughout, so
  any write faults (measured: this is the proof nothing writes the table), and a test store into each does fault. No byte off the sites changes, and no other memory stays allocated.
- 26 row-file cases, each leaving the exe byte-identical, nothing allocated and one log line: missing, empty, each bad field, a repeat, too many, too big, a NUL, a missing message id, a broken DLL.
  Each of the 17 sites, and two opcodes, changed in turn; a small image; a different base; Pirate's Moon's exe; an exe patched on disk.
- The exports against the real DLL, and the exe's four imports among them. The installer, including foreign files at every name it uses.
- 37 breaks, each a change to the source, rebuilt: all sites, the protection restores, the read-only table, every parser check, and the log.
- With 127 weapons the store fill 0x470e30 holds 394 items. The lab's lists past that aren't reached here, so that stays open.

`test_new_weapon.py` adds a definition in memory, made at run time from the player's own PPC entry, and runs the exe's zReader 0x557180 and zWeapon 0x583880 on it, with the row added by each route. Our node writer gives the file back byte for byte first.
Checked: 50 definitions, WeaponType +0 is the row's index, the key, the HUD text with no DESC, damage, the lab's tons and slots, the ammo flag and message id. The other 49 definitions' numbers don't change. Rows must match `MASS`: over all 37 game rows, a row's ammo flag is 1 exactly when `MASS` has `ammo` (measured); `AMMO_LIMIT` doesn't track it.
mech3ax's own `test.py` round-trips reader archives and compares bytes (read); `rezbd` itself hasn't been run here.

`test_weapon_patch.py`, against the unpatched exe. All pass, in about ten seconds, on each of the three exes:

- With no new rows, the plain PPC and two rows: every original row, in either case by name, reads as on the unpatched exe; new rows read as listed;
  past the end reads as the unpatched exe's past the end. Every equipment row through all its references, and its walk with an unknown name.
- The file: size, sections, pages, the `KERNEL32` string, and no byte changed but those listed.
- The nine count sites run: the last row in range, the one after it out.
- The store fill 0x470e30, real: the unpatched exe's items in order, then the new ones (125 items with the PPC, the rebuild's number).
- 49 weapons on the table, the file, the equipment and the count checks.
- The refusals, including a wrong hash, an already patched exe, an existing output, 50 weapons, names that don't fit, an unknown DESC and a missing id.
- 21 breaks caught: 9 count sites, 5 equipment references, both walks' ends, a byte in the self-check's range, a wrong row, the write watch, a longer file,
  and a byte at the space's end.

## Known limits

1. **Arrays inside objects.** 75 weapon-handling functions index no global array but this table, but that search sees only absolutely addressed arrays. PM
   went from 37 to 56 on the same code base, and its 8 widened count sites match 8 of ours, but its new functions are not compared. Nothing rules out a
   per-weapon array inside an object; a real run would show it.
2. **Large lists.** The store held 394 items in the emulator, and on a real game the lab's weapon and ammo lists opened and scrolled to the end with them. The list code keeps its rows in growing vectors (0x48bac0, read). Then one crash, not yet
   repeated: a read fault at 0x489d19, in the Action screen's enemy list refill 0x489c10, indexing the wave table 0x5e33ac by `filling + 6 * wave` (+0x16fa4, +0x16fa0). It faults only with a wave beyond about 88,000 or below about -41,000, or as wild a `filling`, so that field
   or the object holding it was bad. Neither the weapon table nor the store is touched there, and no count-limited buffer was found by reading. Heap damage or a stale pointer isn't excluded, so 127 stays unproven.
3. **Windows.** Both routes have run with the plain PPC, the stand-in through a launcher that checks the exe's hash too (README.md). No antivirus reaction seen on the PC it ran on; others may differ.
