#!/usr/bin/env python3
"""Writes a copy of Mech3.exe 1.2 whose mech lab knows more weapons.

    patch_weapons.py <Mech3.exe> <rows.txt> <output.exe> [--game-dir DIR] [--msg-dll DLL]

The input is checked by SHA-256 and never changed; the output must not exist and
keeps the input's exact size. This file holds offsets and generated code only: the
tables are the player's own, in place. See README.md, including what it cannot do.

The rows file has one weapon per line, appended after the game's 37:

    # name  family  tech    ammo  label
    PPC     3       sphere  no    0x2fd
    MyGun   7       both    yes   0x2ee

name    its key under BALLISTICS in weapons.zrd (exact case)
family  0..14, as an existing weapon's (WRITEUP.md)
tech    clan, sphere, both or none
ammo    yes or no
label   the lab's name: a message id in your Mech3Msg.dll
"""
import argparse
import hashlib
import re
import shlex
import struct
import sys
from pathlib import Path

# The exes it takes, by SHA-256. Adding one is adding a line here (WRITEUP.md, Which exes).
ACCEPTED = {
    '95bc2c114c9b2e5c5ada8e40ca78cc79470f862e45313ffad0d1e8e6d4d916bf': 'the official Mech3.exe 1.2',
}
# ZipperFixup's DLL hooks only an exe of exactly this size, so the patch keeps it.
EXE_SIZE = 2384384
IMAGE_BASE = 0x400000

# The weapon table grows in place over the equipment table, which moves to .rdata's slack.
TABLE = 0x5e64f0
ROW = 0x18
OLD_ROWS = 37
SPACE_END = 0x5e6998  # the self-check's second range starts here
MAX_ROWS = (SPACE_END - TABLE) // ROW  # 49
MAX_ROWS_PROXY = 127  # the run-time route (proxy/): its table is in its own memory; the compares are signed imm8
MAX_FAMILY = 14
MAX_NAME = 31
TECH = {'none': 0, 'clan': 1, 'sphere': 2, 'both': 3}

EQUIPMENT = 0x5e6868
EQUIPMENT_SIZE = 15 * 0x14
EQUIPMENT_NEW = 0x5df8b4  # .rdata's slack, 0x5df8b2..0x5dfa00, all zero on every accepted exe
TEXT_SLACK = 0x5baf98     # .text's slack, to 0x5bb000, all zero too; for names only, when the rest is full
TEXT_END, RDATA_END = 0x5bb000, 0x5dfa00

# Each is the instruction's address; the imm8 is its third byte.
COUNT_SITES = [0x44b37b, 0x45de3b, 0x45deef, 0x470ffe, 0x484904, 0x484924, 0x484944, 0x484964, 0x4b4bfe]
TABLE_END_REF = 0x4848cf  # the weapon walk's end
# The address of each disp32 or imm32 into the equipment table, with its offset into a row.
EQUIPMENT_REFS = {0x4849f7: 0x00, 0x4849a7: 0x04, 0x485366: 0x08, 0x48498f: 0x0c, 0x484a0f: 0x10}
EQUIPMENT_END_REF = 0x4849e2  # that walk's end, past the last row's +4

# (va, raw, size, virtual size) of the two sections whose virtual size grows.
TEXT = (0x401000, 0x400, 0x1ba000, 0x1b9f98)
RDATA = (0x5bb000, 0x1ba400, 0x24a00, 0x248b2)
DATA = (0x5e0000, 0x1dee00, 0x14e00, None)


class Refused(Exception):
    pass


def va_to_raw(va):
    for start, raw, size, _ in (TEXT, RDATA, DATA):
        if start <= va < start + size:
            return va - start + raw
    raise Refused(f'{va:#x} is in no section')


def old_names(data):
    """The game's own weapon names, read through the rows' pointers (lower case)."""
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    table = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, table + i * 40 + 8) for i in range(count)]

    def raw(va):
        rva = va - IMAGE_BASE
        for _, sva, rsize, rawp in sections:
            if sva <= rva < sva + rsize:
                return rva - sva + rawp
        raise Refused(f'{va:#x} is in no section')
    out = set()
    for i in range(OLD_ROWS):
        p = raw(struct.unpack_from('<I', data, raw(TABLE + i * ROW))[0])
        out.add(data[p:data.index(b'\0', p)].decode('latin-1').lower())
    return out


def parse_rows(text):
    """Rows from the list's text: dicts of name, family, tech, ammo, label (a message
    id). Checks what needs nothing but the text."""
    rows = []
    for number, line in enumerate(text.splitlines(), 1):
        try:
            parts = shlex.split(line, comments=True)
        except ValueError as e:
            raise Refused(f'line {number}: {e}')
        if not parts:
            continue
        if len(parts) != 5:
            raise Refused(f'line {number}: wants name family tech ammo label, got {len(parts)} fields')
        name, family, tech, ammo, label = parts
        if not re.fullmatch(r'[A-Za-z0-9_]{1,%d}' % MAX_NAME, name):
            raise Refused(f'line {number}: bad name {name!r}')
        if not re.fullmatch(r'\d+', family) or int(family) > MAX_FAMILY:
            raise Refused(f'line {number}: family {family} is not 0..{MAX_FAMILY}')
        if tech not in TECH:
            raise Refused(f'line {number}: tech {tech} is not one of {", ".join(TECH)}')
        if ammo not in ('yes', 'no'):
            raise Refused(f'line {number}: ammo {ammo} is not yes or no')
        try:
            value = int(label, 0)
        except ValueError:
            raise Refused(f'line {number}: label {label} is not a message id')
        if not 0 < value < 0x10000:
            raise Refused(f'line {number}: message id {label} is out of range')
        rows.append({'name': name, 'family': int(family), 'tech': TECH[tech], 'ammo': ammo == 'yes',
                     'label': value})
    return rows


def check_rows(data, rows):
    """What needs the exe: the total and clashes with the game's names."""
    if OLD_ROWS + len(rows) > MAX_ROWS:
        raise Refused(f'{len(rows)} new rows: there is room for {MAX_ROWS - OLD_ROWS}, {MAX_ROWS} weapons in all')
    seen = old_names(data)
    for row in rows:
        if row['name'].lower() in seen:  # the lab's lookup ignores case
            raise Refused(f'{row["name"]}: the table already has that name')
        seen.add(row['name'].lower())


# ---- the game's data, for the checks that need it ----

def zrd_node(data, pos):
    kind = struct.unpack_from('<i', data, pos)[0]
    pos += 4
    if kind == 1:
        return struct.unpack_from('<i', data, pos)[0], pos + 4
    if kind == 2:
        return struct.unpack_from('<f', data, pos)[0], pos + 4
    if kind == 3:
        n = struct.unpack_from('<I', data, pos)[0]
        return data[pos + 4:pos + 4 + n].decode('latin-1'), pos + 4 + n
    if kind == 4:
        n = struct.unpack_from('<I', data, pos)[0]
        pos += 4
        items = []
        for _ in range(n - 1):
            item, pos = zrd_node(data, pos)
            items.append(item)
        return items, pos
    raise Refused(f'weapons.zrd: bad node type {kind}')


def weapon_defs(reader):
    """weapons.zrd's BALLISTICS from a reader.zbd: {key: its list}."""
    version, count = struct.unpack_from('<II', reader, len(reader) - 8)
    if version != 1:
        raise Refused(f'reader.zbd: archive version {version}, not MW3 1.2\'s')
    table = len(reader) - 8 - count * 0x94
    for i in range(count):
        start, length = struct.unpack_from('<II', reader, table + i * 0x94)
        name = reader[table + i * 0x94 + 8:table + i * 0x94 + 72].split(b'\0')[0]
        if name.lower() == b'weapons.zrd':
            tree, _ = zrd_node(reader[start:start + length], 0)
            while 'BALLISTICS' not in tree:  # the file's list holds its one list
                if len(tree) != 1 or not isinstance(tree[0], list):
                    raise Refused('weapons.zrd has no BALLISTICS')
                tree = tree[0]
            ballistics = tree[tree.index('BALLISTICS') + 1]
            return dict(zip(ballistics[::2], ballistics[1::2]))
    raise Refused('reader.zbd has no weapons.zrd')


def message_ids(dll):
    """The ids in Mech3Msg.dll's message table."""
    pe = struct.unpack_from('<I', dll, 0x3c)[0]
    count = struct.unpack_from('<H', dll, pe + 6)[0]
    table = pe + 24 + struct.unpack_from('<H', dll, pe + 20)[0]
    for i in range(count):
        if dll[table + i * 40:table + i * 40 + 8].rstrip(b'\0') == b'.rsrc':
            va, _, raw = struct.unpack_from('<III', dll, table + i * 40 + 12)
            break
    else:
        raise Refused('Mech3Msg.dll has no resources')

    def entries(o):
        named, ids = struct.unpack_from('<HH', dll, o + 12)
        return [struct.unpack_from('<II', dll, o + 16 + 8 * j) for j in range(named + ids)]
    ids = set()
    for kind, sub in entries(raw):
        if kind != 11:  # RT_MESSAGETABLE
            continue
        for _, langs in entries(raw + (sub & 0x7fffffff)):
            for _, leaf in entries(raw + (langs & 0x7fffffff)):
                p = struct.unpack_from('<I', dll, raw + leaf)[0] - va + raw
                blocks = struct.unpack_from('<I', dll, p)[0]
                for b in range(blocks):
                    lo, hi, _ = struct.unpack_from('<III', dll, p + 4 + 12 * b)
                    ids.update(range(lo, hi + 1))
    return ids


def check_mass(row, fields):
    """The lab's weight and slots come from MASS, by tech; every game row with ammo has MASS `ammo`, and none other."""
    mass = fields[fields.index('MASS') + 1] if 'MASS' in fields else None
    if not isinstance(mass, list):
        raise Refused(f'{row["name"]}: its definition has no MASS')
    for bit, key in ((1, 'clan'), (2, 'sphere')):
        if row['tech'] & bit and key not in mass:
            raise Refused(f'{row["name"]}: its tech needs MASS `{key}`, which its definition lacks')
    if row['ammo'] != ('ammo' in mass):
        raise Refused(f'{row["name"]}: ammo says {"yes" if row["ammo"] else "no"}, '
                      f'but its MASS {"lacks" if row["ammo"] else "has"} `ammo`')


def check_data(rows, reader, dll):
    """Each row has a definition. With the DLL (None: not seen), each DESC and MILITARY_NAME is one it knows (else
    strdup(NULL) at load, 0x583ae8) and each label is in its table (else a null name: README.md)."""
    defs = weapon_defs(reader)
    ids = message_ids(dll) if dll is not None else None
    for row in rows:
        fields = defs.get(row['name'])
        if fields is None:
            raise Refused(f'{row["name"]}: weapons.zrd has no such definition (exact case)')
        check_mass(row, fields)
        if ids is None:
            continue
        for key in ('DESC', 'MILITARY_NAME'):
            if key in fields:
                value = fields[fields.index(key) + 1][0]
                if b'\0' + value.encode('latin-1') + b'\0' not in dll:
                    raise Refused(f'{row["name"]}: {key} {value} is not a name Mech3Msg.dll knows')
        if row['label'] not in ids:
            raise Refused(f'{row["name"]}: Mech3Msg.dll has no message {row["label"]:#x}')


# ---- the patch ----

def check_hash(data):
    if hashlib.sha256(data).hexdigest() not in ACCEPTED:
        raise Refused('this exe is not one the patcher takes: ' + ', '.join(ACCEPTED.values()) +
                      ' (an already patched copy is refused too)')


def build(data, rows):
    """The patched exe's bytes from the original's and checked rows: same size, same sections."""
    check_hash(data)
    check_rows(data, rows)
    total = OLD_ROWS + len(rows)
    table_end = TABLE + total * ROW
    out = bytearray(data)

    def put(va, chunk):
        out[va_to_raw(va):va_to_raw(va) + len(chunk)] = chunk

    # The equipment table first: the new rows go over it.
    start = va_to_raw(EQUIPMENT)
    put(EQUIPMENT_NEW, data[start:start + EQUIPMENT_SIZE])
    put(table_end, bytes(SPACE_END - table_end))

    # The new rows' names, in the first place with room.
    pools = [[table_end, SPACE_END], [EQUIPMENT_NEW + EQUIPMENT_SIZE, RDATA_END], [TEXT_SLACK, TEXT_END]]

    def string(text):
        chunk = text.encode('latin-1') + b'\0'
        for pool in pools:
            if pool[0] + len(chunk) <= pool[1]:
                va = pool[0]
                put(va, chunk)
                pool[0] += len(chunk)
                return va
        raise Refused(f'no room left for the name {text!r}: use shorter names in weapons.zrd, or fewer rows')

    for i, row in enumerate(rows):
        put(TABLE + (OLD_ROWS + i) * ROW, struct.pack('<6I', string(row['name']), row['label'], row['family'],
                                                      OLD_ROWS + i, row['tech'], int(row['ammo'])))

    for site in COUNT_SITES:
        out[va_to_raw(site) + 2] = total
    if TABLE_END_REF is not None:
        struct.pack_into('<I', out, va_to_raw(TABLE_END_REF), table_end)
    for field, offset in EQUIPMENT_REFS.items():
        struct.pack_into('<I', out, va_to_raw(field), EQUIPMENT_NEW + offset)
    if EQUIPMENT_END_REF is not None:
        struct.pack_into('<I', out, va_to_raw(EQUIPMENT_END_REF), EQUIPMENT_NEW + 4 + EQUIPMENT_SIZE)

    # The two sections' virtual sizes now cover what they hold, inside the pages they already have.
    pe = struct.unpack_from('<I', out, 0x3c)[0]
    table = pe + 24 + struct.unpack_from('<H', out, pe + 20)[0]
    if pools[2][0] != TEXT_SLACK:
        struct.pack_into('<I', out, table + 8, pools[2][0] - TEXT[0])
    struct.pack_into('<I', out, table + 40 + 8, pools[1][0] - RDATA[0])

    if len(out) != EXE_SIZE:
        raise Refused(f'the result would be {len(out)} bytes, not {EXE_SIZE}')
    return bytes(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description='Writes a copy of Mech3.exe 1.2 whose lab knows more weapons.')
    parser.add_argument('exe', type=Path, help='your Mech3.exe (read only)')
    parser.add_argument('rows', type=Path, help='the rows list')
    parser.add_argument('output', type=Path, help='the patched copy to write (must not exist)')
    parser.add_argument('--game-dir', type=Path,
                        help='the install with zbd/reader.zbd (default: the exe\'s folder)')
    parser.add_argument('--msg-dll', type=Path,
                        help='your Mech3Msg.dll, to check the rows against (default: the game folder\'s)')
    args = parser.parse_args(argv)
    try:
        data = args.exe.read_bytes()
        rows = parse_rows(args.rows.read_text())
        check_hash(data)
        check_rows(data, rows)
        if rows:
            game = args.game_dir or args.exe.parent
            reader, dll = game / 'zbd' / 'reader.zbd', args.msg_dll or game / 'Mech3Msg.dll'
            if not reader.is_file():
                raise Refused(f'{reader} is missing, needed to check the rows')
            if args.msg_dll and not dll.is_file():
                raise Refused(f'{dll} is missing')
            seen = dll.read_bytes() if dll.is_file() else None
            check_data(rows, reader.read_bytes(), seen)
            if seen is None:
                print(f'warning: no {dll}, so the message ids and DESCs are NOT checked. A missing id gives a '
                      'null name in the lab, which may crash it (README.md).', file=sys.stderr)
        patched = build(data, rows)
        try:
            with open(args.output, 'xb') as f:
                f.write(patched)
        except FileExistsError:
            raise Refused(f'{args.output} already exists')
    except (Refused, OSError, ValueError, IndexError, struct.error) as e:
        print(f'refused: {e}', file=sys.stderr)
        return 1
    print(f'{args.output}: {OLD_ROWS + len(rows)} weapons, {len(patched)} bytes; sha256 {hashlib.sha256(patched).hexdigest()}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
