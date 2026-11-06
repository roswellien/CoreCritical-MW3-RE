"""A brand-new weapon, end to end in the emulator: a definition added to weapons.zrd (made at run time from the player's own
PPC entry, so no game data lives here), read by the exe's own zReader 0x557180 and zWeapon 0x583880, with its row added by
each route. Checks the name, HUD text, weight, slots, ammo and store line up, and that the other definitions don't change.

    python tools/weapon-patch/test_new_weapon.py

Needs what test_proxy.py needs. The edit here is in memory; a real one is made with mech3ax (README.md).
"""
import struct
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import proxy_install as pi  # noqa: E402
import test_proxy as tp  # noqa: E402
import test_weapon_patch as t  # noqa: E402

pw, ROOT = t.pw, t.ROOT
READER = ROOT / 'game' / 'mw3-1.2' / 'zbd' / 'reader.zbd'
READ_NODE, STARTUP, INIT, CALLBACK, LOAD = 0x557180, 0x583750, 0x583880, 0x4b0020, 0x556740
HANDLE, WORLD, ARG, TICK = 0x1234, 0x6600, 0x1234, 0x7700
IAT_CALLOC = 0x5bb60c
NAME = 'LightPPC'
ROW = {'name': NAME, 'family': 3, 'tech': 2, 'ammo': False, 'label': 0x306}
DAMAGE, TONS, SLOTS = 7.0, 4, 2


def write_node(value, out):
    if isinstance(value, bool):
        raise ValueError(value)
    if isinstance(value, int):
        out += struct.pack('<ii', 1, value)
    elif isinstance(value, float):
        out += struct.pack('<if', 2, value)
    elif isinstance(value, str):
        b = value.encode('latin-1')
        out += struct.pack('<iI', 3, len(b)) + b
    else:
        out += struct.pack('<iI', 4, len(value) + 1)
        for item in value:
            write_node(item, out)
    return out


def weapons_zrd():
    data = READER.read_bytes()
    version, count = struct.unpack_from('<II', data, len(data) - 8)
    table = len(data) - 8 - count * 0x94
    for i in range(count):
        start, length = struct.unpack_from('<II', data, table + i * 0x94)
        if data[table + i * 0x94 + 8:table + i * 0x94 + 72].split(b'\0')[0].lower() == b'weapons.zrd':
            return data[start:start + length]
    raise SystemExit('no weapons.zrd')


def ballistics(tree):
    while 'BALLISTICS' not in tree:
        tree = tree[0]
    return tree[tree.index('BALLISTICS') + 1]


def add_definition(raw, name=NAME, edit=None):
    """The file's bytes with a new definition last: the PPC's, lighter and weaker, with no DESC (the HUD then shows the key)."""
    tree, _ = pw.zrd_node(raw, 0)
    entries = ballistics(tree)
    new = list(entries[entries.index('PPC') + 1])
    i = new.index('DESC')
    del new[i:i + 2]
    new[new.index('DAMAGE') + 1] = [DAMAGE]
    new[new.index('MASS') + 1] = ['clan', [TONS - 1, SLOTS - 1], 'sphere', [TONS, SLOTS]]
    if edit:
        edit(new)
    entries += [name, new]
    return bytes(write_node(tree, bytearray())), tree


def load_weapons(m, raw):
    """The exe's own reader on these bytes, unported systems answered with a name's handle."""
    cpu = m.cpu

    def alloc_copy(c, text):
        return m.string(text)

    def calloc(c):
        return m.new(c.arg(0) * c.arg(1))

    def handle(c):
        return 0x70000000 | (sum(m.cstr(c.ecx).encode('latin-1')) & 0xffff)
    cpu.stub_import(IAT_CALLOC, calloc)
    root = m.new(8)
    stubs = {0x586970: (lambda c: 0, 0), LOAD: (lambda c: root, 4), 0x56f6f0: (handle, 0), 0x575fb0: (lambda c: alloc_copy(c, 'M:' + m.cstr(c.ecx)), 0),
             0x50b450: (handle, 0), 0x50d9e0: (handle, 0), 0x4f7fe0: (lambda c: 0x71000000, 0), 0x4ff920: (lambda c: 0, 0), 0x4eb6d0: (lambda c: 0, 0),
             0x4eb170: (lambda c: 0, 0), 0x4f9e00: (lambda c: 0, 0), 0x5847c0: (lambda c: 0, 0), 0x4fb740: (lambda c: TICK, 0), 0x4eb5b0: (lambda c: 0, 0),
             0x4eb450: (lambda c: 0, 0), 0x49bfd0: (lambda c: 0, 0), 0x5a8a20: (lambda c: 0, 0xc)}
    for addr, (fn, pop) in stubs.items():
        cpu.stub_function(addr, fn, pop)
    m.reading, m.read_at = raw, 0
    cpu.call(READ_NODE, ecx=HANDLE, edx=root)
    cpu.call(STARTUP)
    for a in (0x9a177c, 0x9a1794, 0x9a1798, 0x9a179c):
        cpu.u32(a, 0)
    cpu.u32(0x72f348, 1)
    cpu.call(INIT, ecx=WORLD, edx=m.string('weapons.zrd'), stack_args=(ARG, CALLBACK))
    return m.eax()


def find_def(m, name):
    m.cpu.call(t.FIND_DEF, ecx=m.string(name))
    return m.eax()


def numbers(m, d):
    """A definition's numbers and flags, and its game half's: everything but pointers."""
    ty = m.r32(d + 0x1f0)
    return m.cpu.read(d + 0x10, 0x6c), m.cpu.read(ty, 4), m.cpu.read(ty + 0x1c, 0x20)


def check_route(what, base, patched, raw_edited, raw_plain, names):
    out = t.fails(f'{what}: the reader', struct.unpack('<i', struct.pack('<I', load_weapons(patched, raw_edited)))[0] >= -1, True)
    out += t.fails(f'{what}: definitions', patched.r32(t.DEFS_COUNT), 50)
    load_weapons(base, raw_plain)
    out += t.fails(f'{what}: the original file\'s definitions', base.r32(t.DEFS_COUNT), 49)
    d = find_def(patched, NAME)
    if not d:
        return out + [f'{what}: no definition found']
    ty = patched.r32(d + 0x1f0)
    index = pw.OLD_ROWS
    out += t.fails(f'{what}: its key', patched.cstr(patched.r32(d)), NAME)
    out += t.fails(f'{what}: the HUD text, DESC left out', patched.cstr(patched.r32(d + 8)), NAME)
    out += t.fails(f'{what}: its damage', struct.unpack('<f', patched.cpu.read(d + 0x48, 4))[0], DAMAGE)
    out += t.fails(f'{what}: its index, the row', patched.r32(ty), index)
    out += t.fails(f'{what}: the name lookup', patched.index_of(NAME), index)
    out += t.fails(f'{what}: the name', patched.name(index), NAME)
    out += t.fails(f'{what}: the lab text', patched.label(index), f'msg {ROW["label"]:#x}')
    out += t.fails(f'{what}: tech', [patched.has_tech(index, m) for m in (1, 2)], [0, 1])
    out += t.fails(f'{what}: the ammo flag', patched.uses_ammo(index), 0)
    out += t.fails(f'{what}: tons in the lab', patched.tons(index), float(TONS))
    out += t.fails(f'{what}: Inner Sphere slots', patched.r32(ty + 0x2c), SLOTS)
    out += t.fails(f'{what}: tons per round, none', struct.unpack('<f', patched.cpu.read(ty + 0x34, 4))[0], 0.0)
    for name in names:
        a, b = find_def(base, name), find_def(patched, name)
        if not a or not b:
            out.append(f'{what}: {name} lost')
        elif numbers(base, a) != numbers(patched, b):
            out.append(f'{what}: {name} changed')
    return out


def check_unpatched(raw_edited):
    """The edited data on an exe with no row: read as one more definition, the lab's list as it was."""
    m = t.Machine(t.OFFICIAL)
    load_weapons(m, raw_edited)
    d = find_def(m, NAME)
    out = t.fails('no row: definitions', m.r32(t.DEFS_COUNT), 50)
    out += t.fails('no row: its index', m.r32(m.r32(d + 0x1f0)), 0xffffffff)
    out += t.fails('no row: the name lookup', m.index_of(NAME), -1)
    return out


def check_validation(folder, tree_bytes):
    """The row file's check against this data: what lines up and what doesn't."""
    out = []
    game = folder / 'game'
    (game / 'zbd').mkdir(parents=True)
    (game / 'Mech3Msg.dll').write_bytes((ROOT / 'game' / 'mw3-1.2' / 'Mech3Msg.dll').read_bytes())
    data = READER.read_bytes()
    plain = weapons_zrd()
    assert data.count(plain) == 1
    version, count = struct.unpack_from('<II', data, len(data) - 8)
    table = len(data) - 8 - count * 0x94
    rebuilt = bytearray(data[:table]) + tree_bytes
    tail = bytearray(data[table:])
    for i in range(count):
        start, length = struct.unpack_from('<II', tail, i * 0x94)
        if tail[i * 0x94 + 8:i * 0x94 + 72].split(b'\0')[0].lower() == b'weapons.zrd':
            struct.pack_into('<II', tail, i * 0x94, table, len(tree_bytes))
    (game / 'zbd' / 'reader.zbd').write_bytes(bytes(rebuilt + tail))

    def check(text):
        try:
            pi.check_rows(game, text)
            return True
        except pi.pw.Refused:
            return False
    out += t.fails('a good row', check(f'{NAME} 3 sphere no 0x306\n'), True)
    out += t.fails('both techs, both in MASS', check(f'{NAME} 3 both no 0x306\n'), True)
    out += t.fails('ammo yes with no MASS ammo', check(f'{NAME} 3 sphere yes 0x306\n'), False)
    out += t.fails('an id the DLL lacks', check(f'{NAME} 3 sphere no 0x307\n'), False)
    out += t.fails('the wrong case', check(f'lightppc 3 sphere no 0x306\n'), False)
    out += t.fails('the PPC as it was', check('PPC 3 sphere no 0x2fd\n'), True)
    return out


def main():
    plain = weapons_zrd()
    tree, _ = pw.zrd_node(plain, 0)
    failures = t.fails('the file writes back byte for byte', bytes(write_node(tree, bytearray())) == plain, True)
    edited, new_tree = add_definition(plain)
    names = [n for n in ballistics(tree)[::2]]
    out_exe = pw.build(t.OFFICIAL, [ROW])
    failures += check_route('patched copy', t.Machine(t.OFFICIAL), t.Machine(out_exe), edited, plain, names)
    (ROOT / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='weaponnew') as name:
        folder = Path(name)
        dll_path = folder / 'IFORCE2.dll'
        pi.build(dll_path)
        dll = dll_path.read_bytes()
        failures += check_route('stand-in', t.Machine(t.OFFICIAL), tp.Proxied(t.OFFICIAL, dll, [ROW]), edited, plain, names)
        failures += check_unpatched(edited)
        failures += check_validation(folder, edited)
        # Breaks: the checks must see a wrong definition or a wrong row.
        caught = 0
        for what, row, raw in (
                ('a wrong row index', dict(ROW, label=0x305), edited),
                ('a wrong name', dict(ROW, name='LightPPX'), edited),
                ('no definition', ROW, plain),
                ('a wrong damage', ROW, add_definition(plain, edit=lambda d: d.__setitem__(d.index('DAMAGE') + 1, [8.0]))[0]),
                ('a wrong weight', ROW, add_definition(plain, edit=lambda d: d.__setitem__(d.index('MASS') + 1, ['sphere', [5, 2]]))[0])):
            found = check_route('break', t.Machine(t.OFFICIAL), t.Machine(pw.build(t.OFFICIAL, [row])), raw, plain, names)
            print(f'  {what}: {found[0] if found else "NOT CAUGHT"}'[:110])
            caught += bool(found)
            if not found:
                failures.append(f'break not caught: {what}')
    print(f'breaks: {caught} caught')
    for f in failures[:30]:
        print(' ', f)
    print('FAILED' if failures else 'all good')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
