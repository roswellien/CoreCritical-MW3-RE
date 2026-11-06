"""The weapon patcher (patch_weapons.py, WRITEUP.md) against the
unpatched exe in the emulator: every row through each of the table's references,
the self-check, the nine count sites, the store fill, the timers' reset, a loadout's
save, checksum and load, the moved equipment table; then the refusals and break tests.

    python tools/weapon-patch/test_weapon_patch.py [--base OTHER.exe]

--base runs it all on another exe of the same layout, with the patcher's hash list
extended by hand for the run; the patcher itself is unchanged.

Needs Unicorn (pip install unicorn), this repository's tests/differential/original.py,
and the official 1.2 install at game/mw3-1.2 (README.md). Patched copies are made in
a temporary folder under build/ and deleted.
"""
import contextlib
import hashlib
import importlib.util
import io
import struct
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests' / 'differential'))
from original import FTOL, IAT_FTOL, IMAGE_BASE, Original, ROOT  # noqa: E402
from unicorn import UC_HOOK_MEM_WRITE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ECX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESI)

spec = importlib.util.spec_from_file_location('patch_weapons', ROOT / 'tools' / 'weapon-patch' / 'patch_weapons.py')
pw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pw)

EXE = ROOT / 'game' / 'mw3-1.2' / 'Mech3.exe'
GAME = ROOT / 'game' / 'mw3-1.2'
ORIGINAL = OFFICIAL = EXE.read_bytes()

NAME, LABEL, HAS_TECH, USES_AMMO, FAMILY, INDEX_OF = 0x484900, 0x484920, 0x484940, 0x484960, 0x4853c0, 0x4848a0
SELF_CHECK, FIND_DEF, RESET_TIMERS, ADD_WEAPON_DEF = 0x4854c0, 0x581ed0, 0x4b4bd0, 0x4b2230
NOT_A_WEAPON = 0x5e6ef8
DEFS_COUNT, DEFS = 0x9a0ea8, 0x9a0eac
NEW, DELETE, STORE_CTOR, FILL = 0x5abd26, 0x5abd1a, 0x470ce0, 0x470e30
EQUIP_LABEL, EQUIP_KIND, EQUIP_TECH, EQUIP_FIELD = 0x484980, 0x4849a0, 0x484a00, 0x485360
CONFIG_CTOR, CRC, SAVE, LOAD, TONS = 0x423d00, 0x4255b0, 0x424480, 0x424600, 0x484b70
IAT = {'format': 0x5bb0e8, 'write': 0x5bb17c, 'read': 0x5bb0c4, 'free': 0x5bb65c, 'malloc': 0x5bb664,
       'sprintf': 0x5bb658, 'stricmp': 0x5bb580, 'strdup': 0x5bb670}
WEAPON_ITEM = 0x5c0fc0
HANDLE = 0x1234

PPC = {'name': 'PPC', 'family': 3, 'tech': 2, 'ammo': False, 'label': 0x2fd}
BG = {'name': 'LRM_BG', 'family': 7, 'tech': 3, 'ammo': True, 'label': 0x2fe}
TWO = [PPC, BG]

# For each count site: its register, and where it goes while the index is in range
# (None: the next instruction, a guard's `jge` not taken).
SITES = {0x44b37b: (UC_X86_REG_EBP, 0x44b29f), 0x45de3b: (UC_X86_REG_EDI, None), 0x45deef: (UC_X86_REG_EDI, None),
         0x470ffe: (UC_X86_REG_ESI, 0x470ece), 0x4b4bfe: (UC_X86_REG_ESI, 0x4b4bd4)}
GUARDS = (0x484904, 0x484924, 0x484944, 0x484964)  # `cmp ecx, n; jge` in each accessor


class Machine:
    """An exe in the emulator, loaded from its own section table."""

    def __init__(self, data, exe='mw3'):
        self.cpu = cpu = Original(exe=exe)
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        count = struct.unpack_from('<H', data, pe + 6)[0]
        table = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
        cpu.write(IMAGE_BASE, data[:0x400])
        for i in range(count):
            _, va, rsize, raw = struct.unpack_from('<IIII', data, table + i * 40 + 8)
            cpu.write(IMAGE_BASE + va, data[raw:raw + rsize])
        self.writes, self.no_messages = [], False
        for start, end in ((pw.TEXT_SLACK, pw.TEXT_END - 1), (pw.EQUIPMENT_NEW, pw.RDATA_END - 1)):
            cpu.uc.hook_add(UC_HOOK_MEM_WRITE, self.write_hit, begin=start, end=end)
        self.heap = 0x31000000
        cpu.uc.mem_map(self.heap, 0x400000)
        cpu.stub_function(NEW, lambda c: self.new(c.arg(0)))
        cpu.stub_function(DELETE, lambda c: 0)
        cpu.stub_import(IAT['free'], lambda c: 0)
        cpu.stub_import(IAT['malloc'], lambda c: self.new(c.arg(0)))
        cpu.stub_import(IAT['format'], self.format_message, pop_bytes=28)
        cpu.stub_import(IAT['sprintf'], lambda c: 0)
        cpu.stub_import(IAT['stricmp'], self.stricmp)
        cpu.stub_import(IAT['strdup'], lambda c: self.string(self.cstr(c.arg(0))))
        cpu.import_code(IAT_FTOL, FTOL)
        self.written, self.reading, self.read_at = bytearray(), b'', 0
        cpu.stub_import(IAT['write'], self.write_file, pop_bytes=20)
        cpu.stub_import(IAT['read'], self.read_file, pop_bytes=20)

    def write_hit(self, uc, access, address, size, value, user):
        self.writes.append(address)

    def new(self, size):
        p = self.heap
        self.heap += (size + 15) & ~15
        self.cpu.write(p, bytes(size))
        return p

    def format_message(self, c):
        if self.no_messages:
            return 0
        text = f'msg {c.arg(2):#x}'.encode() + b'\0'
        c.write(c.arg(4), text)
        return len(text) - 1

    def stricmp(self, c):
        x, y = self.cstr(c.arg(0)).lower(), self.cstr(c.arg(1)).lower()
        return (x > y) - (x < y)

    def write_file(self, c):
        self.written += c.read(c.arg(1), c.arg(2))
        c.u32(c.arg(3), c.arg(2))
        return 1

    def read_file(self, c):
        chunk = self.reading[self.read_at:self.read_at + c.arg(2)]
        self.read_at += len(chunk)
        c.write(c.arg(1), chunk)
        c.u32(c.arg(3), len(chunk))
        return 1

    def eax(self):
        return self.cpu.uc.reg_read(UC_X86_REG_EAX)

    def r32(self, addr):
        return struct.unpack('<I', self.cpu.read(addr, 4))[0]

    def cstr(self, addr):
        out = bytearray()
        while (b := self.cpu.read(addr + len(out), 1)[0]):
            out.append(b)
        return out.decode('latin-1')

    def string(self, text):
        p = self.new(len(text) + 1)
        self.cpu.write(p, text.encode('latin-1') + b'\0')
        return p

    def name(self, i):
        self.cpu.call(NAME, ecx=i)
        return self.cstr(self.eax())

    def label(self, i):
        self.cpu.call(LABEL, ecx=i)
        return self.cstr(self.eax())

    def has_tech(self, i, mask):
        self.cpu.call(HAS_TECH, ecx=i, edx=mask)
        return self.eax()

    def uses_ammo(self, i):
        self.cpu.call(USES_AMMO, ecx=i)
        return self.eax() & 0xff

    def family(self, i):
        self.cpu.call(FAMILY, ecx=i)
        return self.eax()

    def index_of(self, text):
        self.cpu.call(INDEX_OF, ecx=self.string(text))
        return struct.unpack('<i', struct.pack('<I', self.eax()))[0]

    def row(self, i):
        return (self.name(i), self.label(i), tuple(self.has_tech(i, m) for m in (1, 2, 4)), self.uses_ammo(i))

    def set_defs(self, names):
        """A definition for each name with a game half, all timers set."""
        table = self.new(0x1f4 * len(names))
        self.types = {}
        for i, text in enumerate(names):
            d, t = table + i * 0x1f4, self.new(0xc8)
            self.cpu.u32(d, self.string(text))
            self.cpu.u32(d + 0x1f0, t)
            self.cpu.u32(t + 0xc4, 0x3f800000)
            self.cpu.write(t + 0x1c, struct.pack('<f', 7.0))
            self.cpu.write(t + 0x28, struct.pack('<f', 7.0))
            self.types[text] = t
        self.cpu.u32(DEFS, table)
        self.cpu.u32(DEFS_COUNT, len(names))

    def config(self, weapons):
        cfg, cpu = self.new(0xd8), self.cpu
        cpu.call(CONFIG_CTOR, ecx=cfg, stack_args=(3, self.string('test')))
        vt = self.r32(cfg)
        self.cpu.u32(cfg + 0x18, self.string('test'))
        self.cpu.write(cfg + 0x1c, b'\x01')  # player made: only these are saved, and the CRC covers it
        for w in weapons:
            cpu.call(self.r32(vt + 0x10), ecx=cfg, stack_args=(w, 2))
        return cfg

    def items(self, cfg):
        head = self.r32(cfg + 8)
        node, out = self.r32(head), []
        while node != head:
            it = self.r32(node + 8)
            if self.r32(it) == WEAPON_ITEM:
                out.append(self.r32(it + 0x14))
            node = self.r32(node)
        return out

    def crc(self, cfg):
        self.cpu.call(CRC, ecx=cfg)
        return self.eax()

    def save(self, cfg):
        self.written = bytearray()
        self.cpu.call(SAVE, ecx=cfg, stack_args=(HANDLE,))
        return bytes(self.written)

    def tons(self, i):
        """0x484b70, the lab's weight of a weapon (Inner Sphere's), which the loadout's total adds up."""
        slot = self.new(4)
        code = (b'\xba\x02\0\0\0\xb9' + struct.pack('<I', i) + b'\xb8' + struct.pack('<I', TONS) + b'\xff\xd0'
                b'\xd9\x1d' + struct.pack('<I', slot) + b'\xc3')
        self.cpu.call(self.cpu.code(code))
        return struct.unpack('<f', self.cpu.read(slot, 4))[0]

    def step(self, site, reg, value):
        """Runs a count site's compare and branch; returns where it went."""
        self.cpu.uc.reg_write(reg, value)
        self.cpu.uc.emu_start(site, 0xffffffff, count=2)
        return self.cpu.uc.reg_read(UC_X86_REG_EIP)


def fails(where, got, want):
    return [] if got == want else [f'{where}: {got!r}, want {want!r}']


def check_rows(base, patched, rows):
    """Each original row reads as the unpatched exe's through the references; new ones as listed; past the end as
    nothing."""
    out = []
    total = pw.OLD_ROWS + len(rows)
    names = []
    for i in list(range(-2, 0)) + list(range(pw.OLD_ROWS)):
        out += fails(f'row {i}', patched.row(i), base.row(i))
        if i >= 0:
            out += fails(f'family {i}', patched.family(i), base.family(i))
            names.append(base.name(i))
            for text in (names[-1], names[-1].upper()):
                out += fails(f'index of {text}', patched.index_of(text), base.index_of(text))
    for i, row in enumerate(rows, pw.OLD_ROWS):
        want = (row['name'], f'msg {row["label"]:#x}', tuple(int(bool(row['tech'] & m)) for m in (1, 2, 4)), int(row['ammo']))
        out += fails(f'row {i}', patched.row(i), want)
        out += fails(f'family {i}', patched.family(i), row['family'])
        out += fails(f'index of {row["name"]}', patched.index_of(row['name']), i)
    for i in (total, total + 1, 200):
        out += fails(f'row {i} past the end', patched.row(i), base.row(1000))
    out += fails('an unknown name', patched.index_of('NoSuchGun'), -1)
    return out


def check_count_sites(patched, total):
    """Each of the nine compares: the last row in range, the one after it out."""
    out = []
    for site, (reg, inside) in SITES.items():
        length = 6 if site in (0x45de3b, 0x45deef, 0x470ffe, 0x44b37b) else 2
        fall = site + 3 + length
        if inside is None:  # a guard: jge away when out of range
            out += fails(f'{site:#x} in range', patched.step(site, reg, total - 1), fall)
            if patched.step(site, reg, total) == fall:
                out.append(f'{site:#x}: {total} not refused')
        else:  # a loop: jl back while in range
            out += fails(f'{site:#x} in range', patched.step(site, reg, total - 1), inside)
            out += fails(f'{site:#x} at the end', patched.step(site, reg, total), fall)
    for site in GUARDS:
        for value, inside in ((total - 1, True), (total, False)):
            if (patched.step(site, UC_X86_REG_ECX, value) == site + 5) != inside:
                out.append(f'{site:#x}: index {value} {"refused" if inside else "accepted"}')
    return out


def check_self(patched):
    patched.cpu.call(SELF_CHECK)
    return fails('the self-check', patched.eax() & 0xff, 1)


def fill_counts(m):
    st = m.new(0x14)
    m.cpu.call(STORE_CTOR, ecx=st, stack_args=(0,))
    m.cpu.call(FILL, ecx=st)
    return m.r32(st + 0xc), m.items(st)


def check_fill(base, patched, rows):
    """The lab's "everything" store (0x470e30): the unpatched exe's items, then the new ones."""
    out = []
    n0, w0 = fill_counts(base)
    n1, w1 = fill_counts(patched)
    extra = sum((row['tech'] & 1 != 0) + (row['tech'] & 2 != 0) + int(row['ammo']) for row in rows)
    out += fails('store items', n1, n0 + extra)
    new = [pw.OLD_ROWS + i for i, row in enumerate(rows) for _ in range((row['tech'] & 1 != 0) + (row['tech'] & 2 != 0))]
    out += fails('store weapons', w1[:len(w0)], w0)
    out += fails('new weapons in the store', w1[len(w0):], new)
    return out


def check_timers(patched, rows):
    """0x4b4bd0 from index -1: the default name finds no definition, then every row's type has its AI ready time
    reset, so the new ones must be reached."""
    out = []
    names = [patched.name(i) for i in range(pw.OLD_ROWS + len(rows))]
    patched.set_defs(names + ['NotInTheTable'])
    patched.cpu.call(RESET_TIMERS)
    out += fails('the reset returns', patched.eax() & 0xff, 1)
    for text in names:
        out += fails(f'{text} ready time', patched.r32(patched.types[text] + 0xc4), 0)
    out += fails('a def with no row', patched.r32(patched.types['NotInTheTable'] + 0xc4), 0x3f800000)
    return out


def check_loadouts(base, patched, rows):
    """An old loadout saves and checks the same; a new index packs, saves and reloads with its checksum good."""
    out = []
    names = [base.name(i) for i in range(pw.OLD_ROWS)] + [row['name'] for row in rows]
    for m in (base, patched):
        m.set_defs(names)
    old = [0, 5, 17]
    a, b = base.config(old), patched.config(old)
    out += fails('old loadout CRC', patched.crc(b), base.crc(a))
    out += fails('old loadout file', patched.save(b), base.save(a))
    if not rows:
        return out
    new = old + [pw.OLD_ROWS]
    a, b = base.config(new), patched.config(new)
    out += fails('new loadout CRC, either exe', patched.crc(b), base.crc(a))
    if patched.crc(b) == patched.crc(patched.config(old)):
        out.append('the new index does not change the CRC')
    blob = patched.save(b)
    out += fails('new loadout file, either exe', blob, base.save(a))
    for m, what in ((patched, 'patched'), (base, 'unpatched')):
        m.reading, m.read_at = blob, 0
        back = m.new(0xd8)
        m.cpu.call(CONFIG_CTOR, ecx=back, stack_args=(3, m.string('test')))
        m.cpu.stub_function(0x4254d0, lambda c: 1, pop_bytes=4)  # Validate: its CRC is compared just below
        m.cpu.call(LOAD, ecx=back, stack_args=(HANDLE,))
        out += fails(f'{what}: reloaded weapons', m.items(back), new)
        out += fails(f'{what}: the CRC gate', m.crc(back), m.r32(back + 0x88))
    out += fails('patched: the new weapon weighs', patched.tons(pw.OLD_ROWS), 7.0)
    out += fails('unpatched: the new weapon weighs', base.tons(pw.OLD_ROWS), 0.0)
    return out


def check_missing_name(patched, rows):
    """An id the DLL lacks: FormatMessageA fails, and 0x484920 returns null (README.md says what the lab does)."""
    if not rows:
        return []
    patched.no_messages = True
    patched.cpu.call(LABEL, ecx=pw.OLD_ROWS)
    patched.no_messages = False
    return fails('the name of a missing id', patched.eax(), 0)


def check_unpatched_save(base, rows):
    """The doc's warning: an unpatched exe finds no definition for the new index and makes no weapon."""
    if not rows:
        return []
    out = []
    base.set_defs([base.name(i) for i in range(pw.OLD_ROWS)] + [row['name'] for row in rows])
    item = base.new(0x1c)
    base.cpu.u32(item + 0x14, pw.OLD_ROWS)
    base.cpu.call(NAME, ecx=pw.OLD_ROWS)
    out += fails('unpatched: the name of a new index', base.eax(), NOT_A_WEAPON)
    base.cpu.call(ADD_WEAPON_DEF, ecx=item, edx=base.new(0x400), stack_args=(0,))
    out += fails('unpatched: the weapon made', base.eax(), 0)
    base.cpu.call(HAS_TECH, ecx=pw.OLD_ROWS, edx=3)
    out += fails('unpatched: has a tech', base.eax(), 0)
    return out


def equipment_names(m, table):
    return [m.cstr(m.r32(table + i * 0x14 + 4)) for i in range(15)]


def check_equipment(base, patched, table=None):
    """The moved table through all its references: 0x484980 the label, 0x484a00 the tech, 0x485360 +8, 0x4849a0
    (walk and kind)."""
    out = []
    names = equipment_names(base, pw.EQUIPMENT)
    out += fails('equipment names', equipment_names(patched, table or pw.EQUIPMENT_NEW), names)
    for i in list(range(-2, 18)):
        for m in (base, patched):
            m.cpu.call(EQUIP_LABEL, ecx=i)
            m.label_text = m.cstr(m.eax())
            m.techs = []
            for mask in (1, 2, 4):
                m.cpu.call(EQUIP_TECH, ecx=i, edx=mask)
                m.techs.append(m.eax())
        out += fails(f'equipment {i} label', patched.label_text, base.label_text)
        out += fails(f'equipment {i} tech', patched.techs, base.techs)
        if 0 <= i < 15:
            base.cpu.call(EQUIP_FIELD, ecx=i)
            patched.cpu.call(EQUIP_FIELD, ecx=i)
            out += fails(f'equipment {i} +8', patched.eax(), base.eax())
    for text in names + ['NoSuchThing']:
        base.cpu.call(EQUIP_KIND, ecx=base.string(text))
        patched.cpu.call(EQUIP_KIND, ecx=patched.string(text))
        out += fails(f'equipment kind of {text}', patched.eax(), base.eax())
    return out


def check_headers(out_bytes, rows):
    """The file as Windows would map it: same size and sections, only the listed bytes changed, ZipperFixup's checks
    still answered."""
    out = fails('file size', len(out_bytes), pw.EXE_SIZE)
    out += fails('file size against the input', len(out_bytes), len(ORIGINAL))
    if len(out_bytes) != len(ORIGINAL):
        return out
    pe = struct.unpack_from('<I', out_bytes, 0x3c)[0]
    table = pe + 24 + struct.unpack_from('<H', out_bytes, pe + 20)[0]
    out += fails('sections', struct.unpack_from('<H', out_bytes, pe + 6)[0], struct.unpack_from('<H', ORIGINAL, pe + 6)[0])
    for name in (b'KERNEL32.dll', b'ZIPFIXUP.dll'):  # what its installer looks for
        out += fails(f'{name} strings', out_bytes.count(name), ORIGINAL.count(name))
    for i, section in ((0, pw.TEXT), (1, pw.RDATA)):
        new = struct.unpack_from('<I', out_bytes, table + i * 40 + 8)[0]
        if not section[3] <= new <= section[2] or (new + 0xfff) >> 12 != (section[3] + 0xfff) >> 12:
            out.append(f'section {i}: virtual size {new:#x} leaves its pages')
    allowed = {*range(table + 8, table + 12), *range(table + 48, table + 52)}
    allowed.update(pw.va_to_raw(site) + 2 for site in pw.COUNT_SITES)
    fields = list(pw.EQUIPMENT_REFS) + [f for f in (pw.TABLE_END_REF, pw.EQUIPMENT_END_REF) if f]
    for field in fields:
        allowed.update(range(pw.va_to_raw(field), pw.va_to_raw(field) + 4))
    for start, end in ((pw.TEXT_SLACK, pw.TEXT_END), (pw.EQUIPMENT_NEW, pw.RDATA_END), (pw.EQUIPMENT, pw.SPACE_END)):
        allowed.update(range(pw.va_to_raw(start), pw.va_to_raw(start) + end - start))
    for i, (x, y) in enumerate(zip(ORIGINAL, out_bytes)):
        if x != y and i not in allowed:
            out.append(f'file offset {i:#x} changed')
            break
    return out


def check_all(out_bytes, rows):
    """Every check on one patched image; returns the failures."""
    base, patched = Machine(ORIGINAL), Machine(out_bytes)
    total = pw.OLD_ROWS + len(rows)
    out = check_headers(out_bytes, rows)
    out += check_rows(base, patched, rows)
    out += check_equipment(base, patched)
    out += check_self(patched)
    out += check_count_sites(patched, total)
    out += check_missing_name(patched, rows)
    fill, timers, loadouts = Machine(out_bytes), Machine(out_bytes), Machine(out_bytes)
    machines = [('rows', patched), ('fill', fill), ('timers', timers), ('loadouts', loadouts)]
    out += check_fill(Machine(ORIGINAL), fill, rows)
    out += check_timers(timers, rows)
    out += check_loadouts(Machine(ORIGINAL), loadouts, rows)
    out += check_unpatched_save(Machine(ORIGINAL), rows)
    for what, m in machines:
        if m.writes:
            out.append(f'{what}: writes to the moved table or the slack, first at {m.writes[0]:#x}')
    return out


def check_table_only(out_bytes, rows):
    """check_all without the store and timer runs, whose defs these made-up rows lack."""
    base, patched = Machine(ORIGINAL), Machine(out_bytes)
    return (check_headers(out_bytes, rows) + check_rows(base, patched, rows) + check_equipment(base, patched) +
            check_self(patched) + check_count_sites(patched, pw.OLD_ROWS + len(rows)))


# ---- the refusals ----

def refused(fn):
    try:
        fn()
    except pw.Refused:
        return True
    return False


def refusals(folder):
    out = []
    def run(exe, rows_text, output, **extra):
        rows_file = folder / 'case.txt'
        rows_file.write_text(rows_text)
        args = [str(exe), str(rows_file), str(output), '--game-dir', str(extra.get('game_dir', GAME))]
        if 'msg_dll' in extra:
            args += ['--msg-dll', str(extra['msg_dll'])]
        return pw.main(args)

    made = folder / 'made.exe'
    before = EXE.stat().st_mtime_ns
    out += fails('a good run', run(EXE, 'PPC 3 sphere no 0x2fd\n', made), 0)
    out += fails('the input untouched', (EXE.read_bytes() == ORIGINAL, EXE.stat().st_mtime_ns == before), (True, True))
    kept = made.read_bytes()
    out += fails('over an existing file', run(EXE, 'PPC 3 sphere no 0x2fd\n', made), 1)
    out += fails('the existing file kept', made.read_bytes() == kept, True)
    out += fails('an already patched exe', run(made, '', folder / 'twice.exe'), 1)
    flipped = folder / 'flipped.exe'
    flipped.write_bytes(ORIGINAL[:100] + bytes([ORIGINAL[100] ^ 1]) + ORIGINAL[101:])
    out += fails('a wrong hash', run(flipped, '', folder / 'o1.exe'), 1)
    pm = ROOT / 'game' / 'pm' / 'Mech3.exe'
    if pm.is_file():
        out += fails("Pirate's Moon's exe", run(pm, '', folder / 'o2.exe'), 1)
    bad = {
        'family 15': 'PPC 15 sphere no 0x2fd\n',
        'family -1': 'PPC -1 sphere no 0x2fd\n',
        'a bad tech': 'PPC 3 martian no 0x2fd\n',
        'a bad ammo': 'PPC 3 sphere maybe 0x2fd\n',
        'few fields': 'PPC 3 sphere no\n',
        'a clash with the game': 'erppc 3 sphere no 0x2fd\n',
        'a repeat': 'PPC 3 sphere no 0x2fd\nppc 3 sphere no 0x2fd\n',
        'no definition': 'NoSuchGun 3 sphere no 0x2fd\n',
        'the wrong case of a definition': 'ppc 3 sphere no 0x2fd\n',
        'an unknown message': 'PPC 3 sphere no 0x2ee\n',
        'a message past 16 bits': 'PPC 3 sphere no 0x10000\n',
        'a string label': 'PPC 3 sphere no "Plain PPC"\n',
    }
    for what, text in bad.items():
        out += fails(what, run(EXE, text, folder / 'bad.exe'), 1)
        if (folder / 'bad.exe').exists():
            out.append(f'{what}: wrote a file')
            (folder / 'bad.exe').unlink()
    over = [dict(PPC, name=f'G{i}') for i in range(pw.MAX_ROWS - pw.OLD_ROWS + 1)]
    out += fails('50 weapons', refused(lambda: pw.build(ORIGINAL, over)), True)
    long = [dict(PPC, name=f'{"G" * 30}{i}', label=0x2fd) for i in range(pw.MAX_ROWS - pw.OLD_ROWS)]
    out += fails('names that do not fit', refused(lambda: pw.build(ORIGINAL, long)), True)
    out += fails('a stated limit', pw.MAX_ROWS, 49)

    # A DESC the game does not know: the data's, in a copy of the install's two files.
    game = folder / 'game'
    (game / 'zbd').mkdir(parents=True)
    (game / 'Mech3Msg.dll').write_bytes((GAME / 'Mech3Msg.dll').read_bytes())
    reader = (GAME / 'zbd' / 'reader.zbd').read_bytes()
    (game / 'zbd' / 'reader.zbd').write_bytes(reader.replace(b'MSG_WPN_PPC', b'MSG_WPN_XXX'))
    out += fails('an unknown DESC', run(EXE, 'PPC 3 sphere no 0x2fd\n', folder / 'bad.exe', game_dir=game), 1)
    (game / 'zbd' / 'reader.zbd').write_bytes(reader)
    out += fails('the same, with the DESC right', run(EXE, 'PPC 3 sphere no 0x2fd\n', folder / 'ok.exe', game_dir=game), 0)
    out += fails('a named DLL without the id', run(EXE, 'PPC 3 sphere no 0x2ee\n', folder / 'bad.exe',
                                                   msg_dll=game / 'Mech3Msg.dll'), 1)
    out += fails('a named DLL that is missing', run(EXE, 'PPC 3 sphere no 0x2fd\n', folder / 'bad.exe',
                                                    msg_dll=game / 'NoSuch.dll'), 1)
    (game / 'Mech3Msg.dll').unlink()
    out += fails('no DLL: warned, not refused', run(EXE, 'PPC 3 sphere no 0x2ee\n', folder / 'warned.exe', game_dir=game), 0)
    out += fails('no reader.zbd', run(EXE, 'PPC 3 sphere no 0x2fd\n', folder / 'bad.exe', game_dir=folder), 1)
    out += fails('no rows, no data files', run(EXE, '', folder / 'empty.exe', game_dir=folder), 0)
    return out


def source_holds_no_game_bytes():
    """A crude guard: no long hex or byte-string blob in the patcher."""
    text = (ROOT / 'tools' / 'weapon-patch' / 'patch_weapons.py').read_text()
    words = [w.strip("':,") for w in text.split()]
    blobs = [w for w in words if len(w) > 40 and all(c in '0123456789abcdefABCDEF' for c in w)]
    return fails('hex blobs in the patcher (the accepted hashes alone)', len(blobs), 1)


# ---- the breaks ----

def breaks():
    """A patcher that misses one thing must fail the checks."""
    missed = []

    def attempt(what):
        try:
            data = pw.build(ORIGINAL, [PPC])
            found = check_all(data, [PPC])
        except UcError as e:  # a crash in the emulator is the check catching it too
            found = [repr(e)]
        print(f'  {what}: {found[0] if found else "NOT CAUGHT"}'[:120])
        if not found:
            missed.append(what)
        return bool(found)

    n = 0
    sites, refs, end, equip_end = (list(pw.COUNT_SITES), dict(pw.EQUIPMENT_REFS), pw.TABLE_END_REF,
                                   pw.EQUIPMENT_END_REF)
    for site in sites:
        pw.COUNT_SITES = [s for s in sites if s != site]
        n += attempt(f'count site {site:#x} missed')
    pw.COUNT_SITES = sites
    for field in refs:
        pw.EQUIPMENT_REFS = {k: v for k, v in refs.items() if k != field}
        n += attempt(f'equipment reference {field:#x} missed')
    pw.EQUIPMENT_REFS = refs
    pw.TABLE_END_REF = None
    n += attempt('the weapon walk\'s end missed')
    pw.TABLE_END_REF = end
    pw.EQUIPMENT_END_REF = None
    n += attempt('the equipment walk\'s end missed')
    pw.EQUIPMENT_END_REF = equip_end
    # The bytes themselves: a stray change inside the self-check's range, a wrong row.
    data = bytearray(pw.build(ORIGINAL, [PPC]))
    data[pw.va_to_raw(0x5e6100)] ^= 1
    n += bool(check_self(Machine(bytes(data)))) or missed.append('a change in the self-check range') or 0
    wrong = dict(PPC, family=4)
    data = pw.build(ORIGINAL, [wrong])
    n += bool(check_all(data, [PPC])) or missed.append('a wrong row') or 0
    # The write watch itself: a store into the moved table must show.
    m = Machine(data)
    m.cpu.call(m.cpu.code(b'\xa3' + struct.pack('<I', pw.EQUIPMENT_NEW) + b'\xc3'))
    n += bool(m.writes) or missed.append('a write to the moved table') or 0
    # A file a byte longer, and a table that runs into the self-check's range.
    data = pw.build(ORIGINAL, [PPC])
    n += bool(check_headers(data + b'\0', [PPC])) or missed.append('a longer file') or 0
    bad = bytearray(data)
    bad[pw.va_to_raw(pw.SPACE_END)] ^= 1
    n += bool(check_self(Machine(bytes(bad)))) or missed.append('a write at the space\'s end') or 0
    return n, missed


def main():
    global ORIGINAL, EXE
    failures = []
    if '--base' in sys.argv:
        EXE = Path(sys.argv[sys.argv.index('--base') + 1])
        ORIGINAL = EXE.read_bytes()
        pw.ACCEPTED[hashlib.sha256(ORIGINAL).hexdigest()] = f'the test base {EXE.name}'
        print(f'base {EXE.name}, sha256 {hashlib.sha256(ORIGINAL).hexdigest()}')
    for label, rows in (('no new rows', []), ('the plain PPC', [PPC]), ('two rows', TWO)):
        found = check_all(pw.build(ORIGINAL, rows), rows)
        print(f'{label}: {"ok" if not found else f"{len(found)} failures"}')
        failures += [f'{label}: {f}' for f in found]

    # The most rows, short names: every check, at the table's limit.
    many = [dict(PPC, name=f'G{i}') for i in range(pw.MAX_ROWS - pw.OLD_ROWS)]
    found = [f for f in check_table_only(pw.build(ORIGINAL, many), many)]
    print(f'{pw.MAX_ROWS} weapons: {"ok" if not found else found}')
    failures += found

    (ROOT / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='weaponpatch') as folder, \
            contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        found = refusals(Path(folder)) + source_holds_no_game_bytes()
    print(f'refusals: {"ok" if not found else found}')
    failures += found

    if failures:  # a break means nothing while the plain run fails
        for f in failures[:40]:
            print(' ', f)
        print('FAILED')
        return 1
    n, missed = breaks()
    print(f'breaks: {n} caught' + (f', missed {missed}' if missed else ''))
    failures += missed

    for f in failures[:40]:
        print(' ', f)
    print('FAILED' if failures else 'all good')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
