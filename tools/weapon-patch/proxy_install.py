#!/usr/bin/env python3
"""Builds, installs and removes the run-time route: a stand-in IFORCE2.dll (proxy/, README.md).

    proxy_install.py build [--out IFORCE2.dll]
    proxy_install.py install <game folder> <IFORCE2.dll built> <rows.txt>
    proxy_install.py restore <game folder>
    proxy_install.py check <game folder> <rows.txt>

install renames the game's IFORCE2.dll to IFORCE2o.dll, and copies in the stand-in and the rows as
weapon-rows.txt. It refuses if either new name exists. restore removes only a file that carries the stand-in's
marker, and puts the original back. check tries a row file against that folder's reader.zbd, Mech3Msg.dll and exe. Mech3.exe is never touched. Standard library only, plus mingw to build.
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import patch_weapons as pw  # noqa: E402

COMPILER = 'i686-w64-mingw32-gcc'
FLAGS = ['-shared', '-nostdlib', '-nostartfiles', '-ffreestanding', '-fno-stack-protector', '-fno-builtin',
         '-fno-tree-loop-distribute-patterns', '-fno-asynchronous-unwind-tables', '-O2', '-Wall', '-Wextra', '-s',
         '-Wl,-e,_DllMain@12,--image-base=0x62000000,--dynamicbase,--no-insert-timestamp']
MARKER = b'weapon-patch IFORCE2 proxy'
ORIGINAL, RENAMED, ROWS = 'IFORCE2.dll', 'IFORCE2o.dll', 'weapon-rows.txt'
OLD_ROWS, MAX_ROWS = 37, 127  # the run-time route's table is in its own memory


def build_command(source, definitions, out):
    return [COMPILER, *FLAGS, '-o', str(out), str(source), str(definitions), '-lkernel32']


def build(out, source=HERE / 'proxy' / 'iforce2_proxy.c'):
    subprocess.run(build_command(source, HERE / 'proxy' / 'iforce2_proxy.def', out), check=True)


def exports(data):
    """{ordinal: (name, forward or None)} from a PE file's export table."""
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    table = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, table + i * 40 + 8) for i in range(count)]

    def raw(rva):
        for size, va, rsize, rawp in sections:
            if va <= rva < va + max(size, rsize):
                return rva - va + rawp
        raise pw.Refused(f'{rva:#x} is in no section')

    def text(rva):
        p = raw(rva)
        return data[p:data.index(b'\0', p)].decode('latin-1')
    start, size = struct.unpack_from('<II', data, pe + 24 + 96)
    if not start:
        return {}
    base, functions, names, faddr, naddr, oaddr = struct.unpack_from('<6I', data, raw(start) + 16)
    out = {}
    for i in range(names):
        name = text(struct.unpack_from('<I', data, raw(naddr) + i * 4)[0])
        index = struct.unpack_from('<H', data, raw(oaddr) + i * 2)[0]
        rva = struct.unpack_from('<I', data, raw(faddr) + index * 4)[0]
        out[index + base] = (name, text(rva) if start <= rva < start + size else None)
    return out


def check_rows(game, rows_text):
    """What can be checked before the game runs: the rows' grammar, their definitions, and their ids in the DLL.
    The stand-in checks the rest when the game starts, and writes why in weapon-patch.log."""
    rows = pw.parse_rows(rows_text)
    if OLD_ROWS + len(rows) > MAX_ROWS:
        raise pw.Refused(f'{OLD_ROWS} + {len(rows)} rows is over {MAX_ROWS}')
    reader, dll = game / 'zbd' / 'reader.zbd', game / 'Mech3Msg.dll'
    if rows:
        if not reader.is_file() or not dll.is_file():
            raise pw.Refused(f'{game} lacks zbd/reader.zbd or Mech3Msg.dll, needed to check the rows')
        pw.check_data(rows, reader.read_bytes(), dll.read_bytes())
        exe = game / 'Mech3.exe'
        if exe.is_file() and exe.stat().st_size == pw.EXE_SIZE:
            seen = pw.old_names(exe.read_bytes())
            for row in rows:
                if row['name'].lower() in seen:
                    raise pw.Refused(f'{row["name"]}: the table already has that name')
                seen.add(row['name'].lower())
    return rows


def install(game, dll, rows):
    original, renamed, stand_in, row_file = game / ORIGINAL, game / RENAMED, dll, game / ROWS
    if not original.is_file():
        raise pw.Refused(f'{original} is missing')
    if renamed.exists():
        raise pw.Refused(f'{renamed} already exists: is it installed already? (restore puts it back)')
    if row_file.exists():
        raise pw.Refused(f'{row_file} already exists')
    current = original.read_bytes()
    if MARKER in current:
        raise pw.Refused(f'{original} is already the stand-in')
    built = stand_in.read_bytes()
    if MARKER not in built:
        raise pw.Refused(f'{stand_in} is not the stand-in')
    want, have = {o: n for o, (n, _) in exports(current).items()}, {o: n for o, (n, _) in exports(built).items()}
    if want != have:
        raise pw.Refused(f'{original} exports other names than the stand-in forwards: {sorted(set(want.items()) ^ set(have.items()))}')
    rows_text = rows.read_text()
    check_rows(game, rows_text)
    original.rename(renamed)
    try:
        with open(original, 'xb') as f:
            f.write(built)
        with open(row_file, 'x') as f:
            f.write(rows_text)
    except OSError:
        if original.exists() and MARKER in original.read_bytes():
            original.unlink()
        if row_file.exists() and row_file.read_text() == rows_text:
            row_file.unlink()
        renamed.rename(original)
        raise


def restore(game):
    original, renamed = game / ORIGINAL, game / RENAMED
    if not renamed.is_file():
        raise pw.Refused(f'{renamed} is missing: nothing to put back')
    if not original.is_file() or MARKER not in original.read_bytes():
        raise pw.Refused(f'{original} is not the stand-in, so it stays')
    original.unlink()
    renamed.rename(original)


def main(argv=None):
    parser = argparse.ArgumentParser(description='The run-time route: a stand-in IFORCE2.dll.')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('build')
    p.add_argument('--out', type=Path, default=Path('IFORCE2.dll'))
    p = sub.add_parser('install')
    p.add_argument('game', type=Path)
    p.add_argument('dll', type=Path)
    p.add_argument('rows', type=Path)
    p = sub.add_parser('restore')
    p.add_argument('game', type=Path)
    p = sub.add_parser('check')
    p.add_argument('game', type=Path)
    p.add_argument('rows', type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == 'build':
            build(args.out)
            print(f'built {args.out}')
        elif args.command == 'install':
            install(args.game, args.dll, args.rows)
            print(f'installed in {args.game}; {RENAMED} is the original')
        elif args.command == 'check':
            rows = check_rows(args.game, args.rows.read_text())
            print(f'{args.rows}: good, {OLD_ROWS + len(rows)} weapons')
        else:
            restore(args.game)
            print(f'restored {args.game / ORIGINAL}; {ROWS} and weapon-patch.log are left')
    except (pw.Refused, OSError, ValueError, IndexError, struct.error, subprocess.CalledProcessError) as e:
        print(f'refused: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
