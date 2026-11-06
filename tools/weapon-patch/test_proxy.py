"""The stand-in IFORCE2.dll (proxy/, README.md) against the unpatched exe in the emulator. The built DLL is mapped with
kernel32 stubbed and its DllMain run; then the same checks as the on-disk patch, with the exe's code pages and the table
really protected, so a stray write faults. Then its refusals, the installer, and breaks of the source, rebuilt each.

    python tools/weapon-patch/test_proxy.py [--base OTHER.exe]

Needs what test_weapon_patch.py needs, and mingw (i686-w64-mingw32-gcc). Everything built goes in a temporary folder
under build/ and is deleted.
"""
import contextlib
import hashlib
import io
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import proxy_install as pi  # noqa: E402
import test_weapon_patch as t  # noqa: E402
from unicorn import UC_PROT_EXEC, UC_PROT_READ, UC_PROT_WRITE, UcError  # noqa: E402

pw, ROOT = t.pw, t.ROOT
GAME_DIR = 'C:\\game\\'
EXE_LOG = 'weapon-patch.log'
TEXT = (0x401000, 0x1ba000)
RX, R, RW, RWX = 0x20, 0x02, 0x04, 0x40
PERM = {RX: UC_PROT_READ | UC_PROT_EXEC, R: UC_PROT_READ, RW: UC_PROT_READ | UC_PROT_WRITE,
        RWX: UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC}
SITES = {**{s + 2: 1 for s in pw.COUNT_SITES},
         **{a: 4 for a in (0x4848b8, 0x4848cf, 0x4848e3, 0x48490f, 0x48492f, 0x48494f, 0x48496f, 0x4853c6)}}
PPC, BG, TWO = t.PPC, t.BG, t.TWO
TECH_WORD = {0: 'none', 1: 'clan', 2: 'sphere', 3: 'both'}
MSG_DLL = (ROOT / 'game' / 'mw3-1.2' / 'Mech3Msg.dll').read_bytes()


def row_text(rows):
    return ''.join(f'{r["name"]} {r["family"]} {TECH_WORD[r["tech"]]} {"yes" if r["ammo"] else "no"} {r["label"]:#x}\n'
                   for r in rows)


class Proxied(t.Machine):
    """An exe with the stand-in loaded, as Windows would: mapped, imports bound, DllMain run."""

    def __init__(self, exe_bytes, dll, rows=None, files=None, exe='mw3', base=None, edit=None):
        super().__init__(exe_bytes, exe=exe)
        self.files = {'weapon-rows.txt': row_text(rows).encode()} if rows is not None else {}
        self.files['Mech3Msg.dll'] = MSG_DLL
        self.files.update(files or {})
        self.files = {k: v for k, v in self.files.items() if v is not None}
        self.module_base = t.IMAGE_BASE if base is None else base
        self.prot, self.allocs, self.next_alloc, self.handles, self.log, self.read_at_h = {}, {}, 0x33000000, {}, b'', {}
        cpu = self.cpu
        if edit:
            edit(cpu)
        cpu.uc.mem_protect(TEXT[0], TEXT[1], PERM[RX])
        for page in range(TEXT[0], TEXT[0] + TEXT[1], 0x1000):
            self.prot[page] = RX
        self.before = cpu.read(TEXT[0], TEXT[1])
        self.load(dll)

    def load(self, dll):
        cpu = self.cpu
        pe = struct.unpack_from('<I', dll, 0x3c)[0]
        count = struct.unpack_from('<H', dll, pe + 6)[0]
        table = pe + 24 + struct.unpack_from('<H', dll, pe + 20)[0]
        entry, image = struct.unpack_from('<I', dll, pe + 40)[0], struct.unpack_from('<I', dll, pe + 80)[0]
        base = struct.unpack_from('<I', dll, pe + 52)[0]
        cpu.uc.mem_map(base, (image + 0xfff) & ~0xfff)
        cpu.write(base, dll[:0x400])
        for i in range(count):
            _, va, rsize, raw = struct.unpack_from('<IIII', dll, table + i * 40 + 8)
            cpu.write(base + va, dll[raw:raw + rsize])
        imports = struct.unpack_from('<I', dll, pe + 24 + 104)[0]
        stubs = {'CloseHandle': (lambda c: 1, 4), 'CreateFileA': (self.create_file, 28),
                 'FlushInstructionCache': (lambda c: 1, 12), 'GetCurrentProcess': (lambda c: 0xffffffff, 0),
                 'GetFileSize': (self.file_size, 8), 'GetModuleFileNameA': (self.module_name, 12),
                 'GetModuleHandleA': (lambda c: self.module_base, 4), 'ReadFile': (self.read_h, 20),
                 'WriteFile': (self.write_h, 20), 'VirtualAlloc': (self.valloc, 16), 'VirtualFree': (self.vfree, 12),
                 'VirtualProtect': (self.vprotect, 16)}
        for d in range(0, 0x100, 20):
            look, _, _, name, bound = struct.unpack_from('<5I', cpu.read(base + imports + d, 20))
            if not name:
                break
            i = 0
            while (thunk := self.r32(base + look + i * 4)):
                fn = self.cstr(base + thunk + 2)
                cpu.stub_import(base + bound + i * 4, stubs[fn][0], pop_bytes=stubs[fn][1])
                i += 1
        cpu.call(base + entry, ecx=0, edx=0, stack_args=(base, 1, 0))
        self.main_result = self.eax()

    def create_file(self, c):
        path, access = self.cstr(c.arg(0)), c.arg(1)
        name = path[len(GAME_DIR):] if path.startswith(GAME_DIR) else path
        if access & 0x40000000:
            self.handles[0x700] = EXE_LOG
            self.log = b''
            return 0x700
        if name not in self.files:
            return 0xffffffff
        h = 0x800 + len(self.handles)
        self.handles[h] = name
        self.read_at_h[h] = 0
        return h

    def file_size(self, c):
        if c.arg(1):
            c.u32(c.arg(1), 0)
        return len(self.files[self.handles[c.arg(0)]])

    def module_name(self, c):
        text = (GAME_DIR + 'IFORCE2.dll').encode() + b'\0'
        c.write(c.arg(1), text)
        return len(text) - 1

    def read_h(self, c):
        h = c.arg(0)
        chunk = self.files[self.handles[h]][self.read_at_h[h]:self.read_at_h[h] + c.arg(2)]
        self.read_at_h[h] += len(chunk)
        c.write(c.arg(1), chunk)
        c.u32(c.arg(3), len(chunk))
        return 1

    def write_h(self, c):
        self.log += c.read(c.arg(1), c.arg(2))
        c.u32(c.arg(3), c.arg(2))
        return 1

    def valloc(self, c):
        size = (c.arg(1) + 0xfff) & ~0xfff
        addr = self.next_alloc
        self.next_alloc += size + 0x1000
        c.uc.mem_map(addr, size, PERM[c.arg(3)])
        self.allocs[addr] = size
        for page in range(addr, addr + size, 0x1000):
            self.prot[page] = c.arg(3)
        return addr

    def vfree(self, c):
        c.uc.mem_unmap(c.arg(0), self.allocs.pop(c.arg(0)))
        return 1

    def vprotect(self, c):
        addr, size, new = c.arg(0), c.arg(1), c.arg(2)
        start, end = addr & ~0xfff, (addr + size + 0xfff) & ~0xfff
        c.u32(c.arg(3), self.prot.get(start, RW))
        c.uc.mem_protect(start, end - start, PERM[new])
        for page in range(start, end, 0x1000):
            self.prot[page] = new
        return 1

    def after(self):
        return self.cpu.read(TEXT[0], TEXT[1])

    def changed(self):
        return [i + TEXT[0] for i, (x, y) in enumerate(zip(self.before, self.after())) if x != y]

    def text_perms(self):
        return {(s, e, p) for s, e, p in self.cpu.uc.mem_regions() if s < TEXT[0] + TEXT[1] and e >= TEXT[0]
                and not (s == t.IMAGE_BASE and e < TEXT[0])}


def unchanged(m, what, want_log):
    out = t.fails(f'{what}: DllMain', m.main_result, 1)
    if m.changed():
        out.append(f'{what}: the exe changed at {m.changed()[0]:#x}')
    if m.allocs:
        out.append(f'{what}: memory left allocated')
    out += protections(m, what)
    if want_log.encode() not in m.log or m.log.count(b'\n') != 1:
        out.append(f'{what}: the log says {m.log!r}, wanted {want_log!r} on one line')
    return out


def protections(m, what):
    out = []
    bad = [hex(p) for p in range(TEXT[0], TEXT[0] + TEXT[1], 0x1000) if m.prot[p] != RX]
    if bad:
        out.append(f'{what}: code pages left unprotected, first {bad[0]}')
    for s, e, p in m.cpu.uc.mem_regions():
        if s < TEXT[0] + TEXT[1] and e >= TEXT[0] and s >= TEXT[0] and p != PERM[RX]:
            out.append(f'{what}: the emulator sees code at {s:#x} with protection {p}')
    return out


def check_patched(base_bytes, dll, rows):
    """Every check on the stand-in's result; base_bytes is the exe it was loaded on."""
    total = pw.OLD_ROWS + len(rows)

    def proxied():
        return Proxied(base_bytes, dll, rows)

    base, patched = t.Machine(base_bytes), proxied()
    out = t.fails('DllMain', patched.main_result, 1)
    out += t.fails('the log', patched.log.split(b': ', 1)[-1], f'weapons in the lab: {total}\r\n'.encode())
    table = patched.r32(0x48490f)
    out += t.fails('the table is alone in memory', list(patched.allocs), [table & ~0xfff])
    out += t.fails('its protection', patched.prot.get(table), R)
    allowed = {a + i for a, n in SITES.items() for i in range(n)}
    stray = [a for a in patched.changed() if a not in allowed]
    if stray:
        out.append(f'a change off the sites, at {stray[0]:#x}')
    out += protections(patched, 'patched')
    try:
        patched.cpu.call(patched.cpu.code(b'\xa3' + struct.pack('<I', table) + b'\xc3'))
        out.append('a store into the table did not fault')
    except UcError:
        pass
    try:
        patched.cpu.call(patched.cpu.code(b'\xa3' + struct.pack('<I', 0x484904) + b'\xc3'))
        out.append('a store into the code did not fault')
    except UcError:
        pass
    # A fault in any of these would be a write to the table or the code.
    patched = proxied()
    out += t.check_rows(base, patched, rows)
    out += t.check_equipment(base, patched, table=pw.EQUIPMENT)
    out += t.check_self(patched)
    out += t.check_count_sites(patched, total)
    out += t.check_missing_name(patched, rows)
    out += t.check_fill(t.Machine(base_bytes), proxied(), rows)
    out += t.check_timers(proxied(), rows)
    out += t.check_loadouts(t.Machine(base_bytes), proxied(), rows)
    out += t.check_unpatched_save(t.Machine(base_bytes), rows)
    return out


def check_refusals(base_bytes, dll):
    out = []
    ok = row_text([PPC])
    cases = {
        'no row file': ({'weapon-rows.txt': None}, 'no weapon-rows.txt'),
        'an empty row file': ({'weapon-rows.txt': b''}, 'has no rows'),
        'only comments': ({'weapon-rows.txt': b'# nothing\n  \n'}, 'has no rows'),
        'family 15': ('PPC 15 sphere no 0x2fd\n', 'family'),
        'family -1': ('PPC -1 sphere no 0x2fd\n', 'family'),
        'a long family': ('PPC 123456789012 sphere no 0x2fd\n', 'family'),
        'a bad tech': ('PPC 3 martian no 0x2fd\n', 'tech'),
        'a bad ammo': ('PPC 3 sphere maybe 0x2fd\n', 'ammo'),
        'few fields': ('PPC 3 sphere no\n', 'wants name'),
        'many fields': ('PPC 3 sphere no 0x2fd extra\n', 'wants name'),
        'a string label': ('PPC 3 sphere no "Plain"\n', 'message id'),
        'id 0': ('PPC 3 sphere no 0\n', 'message id'),
        'id 0x10000': ('PPC 3 sphere no 0x10000\n', 'message id'),
        'a leading zero': ('PPC 3 sphere no 0765\n', 'message id'),
        'a clash with the game': ('erppc 3 sphere no 0x2fd\n', 'already has'),
        'a repeat': ('PPC 3 sphere no 0x2fd\nppc 3 sphere no 0x2fd\n', 'twice'),
        'a long name': (f'{"G" * 32} 3 sphere no 0x2fd\n', 'bad name'),
        'a bad name': ('P-C 3 sphere no 0x2fd\n', 'bad name'),
        'a bad line after a good one': (ok + 'LRM_BG 99 both yes 0x2fe\n', 'line 2'),
        'a NUL': ({'weapon-rows.txt': ok.encode() + b'\0'}, 'not plain text'),
        'a high byte': ({'weapon-rows.txt': ok.encode() + b'\x80\n'}, 'not plain text'),
        '91 new rows': (''.join(f'G{i} 3 sphere no 0x2fd\n' for i in range(91)), 'too many'),
        'too big a file': ({'weapon-rows.txt': b'#' * 0x10001}, 'no weapon-rows.txt'),
        'an id the DLL lacks': ('PPC 3 sphere no 0x2ee\n', 'no message for row'),
        'no Mech3Msg.dll': ({'weapon-rows.txt': ok.encode(), 'Mech3Msg.dll': None}, "can't read Mech3Msg"),
        'a broken Mech3Msg.dll': ({'weapon-rows.txt': ok.encode(), 'Mech3Msg.dll': b'MZ' + bytes(50)}, 'message table'),
    }
    for what, (case, want) in cases.items():
        files = case if isinstance(case, dict) else {'weapon-rows.txt': case.encode()}
        out += unchanged(Proxied(base_bytes, dll, files=files), what, want)
    # The exe, by every site: any one different and nothing changes.
    for site, size in SITES.items():
        def edit(cpu, site=site):
            cpu.write(site, bytes([cpu.read(site, 1)[0] ^ 1]))
        out += unchanged(Proxied(base_bytes, dll, [PPC], edit=edit), f'site {site:#x} different', 'not an exe')
    for op in (0x44b37b, 0x484904):
        def edit(cpu, op=op):
            cpu.write(op, b'\x90')
        out += unchanged(Proxied(base_bytes, dll, [PPC], edit=edit), f'an opcode at {op:#x} different', 'not an exe')

    def small(cpu):
        pe = struct.unpack('<I', cpu.read(t.IMAGE_BASE + 0x3c, 4))[0]
        cpu.u32(t.IMAGE_BASE + pe + 80, 0x1000)
    out += unchanged(Proxied(base_bytes, dll, [PPC], edit=small), 'a small image', 'not an exe')
    out += unchanged(Proxied(base_bytes, dll, [PPC], base=0x500000), 'loaded elsewhere', 'not an exe')
    return out


def check_exes(dll):
    """Exes it must leave alone, with good rows: Pirate's Moon's, and an exe patched on disk."""
    out = unchanged(Proxied(pw.build(t.OFFICIAL, [PPC]), dll, [PPC]), 'an on-disk patched exe', 'not an exe')
    pm = ROOT / 'game' / 'pm' / 'Mech3.exe'
    if pm.is_file():
        out += unchanged(Proxied(pm.read_bytes(), dll, [PPC], exe='pm'), "Pirate's Moon's exe", 'not an exe')
    return out


def check_exports(dll):
    real = (ROOT / 'game' / 'mw3-1.2' / 'IFORCE2.dll').read_bytes()
    want, have = pi.exports(real), pi.exports(dll)
    out = t.fails('export names and ordinals', {o: n for o, (n, _) in have.items()}, {o: n for o, (n, _) in want.items()})
    out += t.fails('forwards', {n: f for n, f in have.values()}, {n: 'IFORCE2o.' + n for n, _ in want.values()})
    out += t.fails('export count', len(have), 10)
    imports = subprocess.run(['objdump', '-p', str(ROOT / 'game' / 'mw3-1.2' / 'Mech3.exe')], capture_output=True, text=True).stdout
    names = imports[imports.index('DLL Name: IFORCE2'):].split('DLL Name:')[1]
    used = [line.split()[-1] for line in names.splitlines() if line.strip().endswith(('@4', '@8', '@12'))]
    if len(used) != 4 or set(used) - {n for n, _ in have.values()}:
        out.append(f"the exe's imports {used} are not all forwarded")
    out += t.fails('the marker', pi.MARKER in dll, True)
    return out


def check_installer(folder, dll_path):
    out = []
    game = folder / 'game'
    (game / 'zbd').mkdir(parents=True)
    real = (ROOT / 'game' / 'mw3-1.2' / 'IFORCE2.dll').read_bytes()
    (game / 'IFORCE2.dll').write_bytes(real)
    (game / 'Mech3Msg.dll').write_bytes(MSG_DLL)
    (game / 'zbd' / 'reader.zbd').write_bytes((ROOT / 'game' / 'mw3-1.2' / 'zbd' / 'reader.zbd').read_bytes())
    rows = folder / 'rows.txt'
    rows.write_text(row_text([PPC]))

    def run(*args):
        return pi.main([str(a) for a in args])

    def state():
        return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in game.iterdir() if p.is_file()}

    start = state()
    rows.write_text('PPC 99 sphere no 0x2fd\n')
    out += t.fails('bad rows', run('install', game, dll_path, rows), 1)
    out += t.fails('bad rows changed nothing', state(), start)
    rows.write_text('NoSuchGun 3 sphere no 0x2fd\n')
    out += t.fails('no definition', run('install', game, dll_path, rows), 1)
    rows.write_text('PPC 3 sphere no 0x2ee\n')
    out += t.fails('an id the DLL lacks', run('install', game, dll_path, rows), 1)
    out += t.fails('still nothing changed', state(), start)
    out += t.fails('not the stand-in', run('install', game, game / 'Mech3Msg.dll', rows), 1)
    out += t.fails('restore with nothing installed', run('restore', game), 1)
    out += t.fails('and nothing changed', state(), start)
    rows.write_text(row_text([PPC]))
    out += t.fails('install', run('install', game, dll_path, rows), 0)
    out += t.fails('the original is kept', (game / 'IFORCE2o.dll').read_bytes() == real, True)
    out += t.fails('the stand-in is in', (game / 'IFORCE2.dll').read_bytes() == dll_path.read_bytes(), True)
    out += t.fails('the rows are in', (game / 'weapon-rows.txt').read_text(), rows.read_text())
    installed = state()
    out += t.fails('install twice', run('install', game, dll_path, rows), 1)
    out += t.fails('install twice changed nothing', state(), installed)
    keep = (game / 'IFORCE2.dll').read_bytes()
    (game / 'IFORCE2.dll').write_bytes(b'someone else\'s file')
    out += t.fails('restore over a foreign file', run('restore', game), 1)
    out += t.fails('the foreign file stays', (game / 'IFORCE2.dll').read_bytes(), b'someone else\'s file')
    (game / 'IFORCE2.dll').write_bytes(keep)
    out += t.fails('restore', run('restore', game), 0)
    out += t.fails('the original is back', (game / 'IFORCE2.dll').read_bytes() == real, True)
    out += t.fails('no renamed file', (game / 'IFORCE2o.dll').exists(), False)
    (game / 'weapon-rows.txt').unlink()
    (game / 'IFORCE2o.dll').write_bytes(b'foreign')
    out += t.fails('install over a foreign rename target', run('install', game, dll_path, rows), 1)
    out += t.fails('that file stays', (game / 'IFORCE2o.dll').read_bytes(), b'foreign')
    (game / 'IFORCE2o.dll').unlink()
    return out


def suite(base_bytes, dll):
    out = check_exports(dll)
    for rows in ([PPC], TWO):
        out += check_patched(base_bytes, dll, rows)
    many = [dict(PPC, name=f'G{i}', tech=3, ammo=True) for i in range(pw.MAX_ROWS_PROXY - pw.OLD_ROWS)]
    out += check_patched(base_bytes, dll, many)
    out += check_refusals(base_bytes, dll)
    out += check_exes(dll)
    return out


# ---- breaks: each a change to the source, rebuilt; the suite must then fail ----

COUNT_BREAKS = [(f'count site {a:#x} missed', f'{{{a:#x}, 0x{m:02x}}},', '')
                for a, m in ((0x44b37b, 0xfd), (0x45de3b, 0xff), (0x45deef, 0xff), (0x470ffe, 0xfe), (0x484904, 0xf9),
                             (0x484924, 0xf9), (0x484944, 0xf9), (0x484964, 0xf9))]
BREAKS = COUNT_BREAKS + [
    ('count site 0x4b4bfe missed', ', {0x4b4bfe, 0xfe},', ','),
    ('ref 0x4848b8 missed', '{0x4848b8, 0x00}, ', ''),
    ('ref 0x48490f missed', '{0x48490f, 0x00}, ', ''),
    ('ref 0x48492f missed', '{0x48492f, 0x04}, ', ''),
    ('ref 0x4853c6 missed', ', {0x4853c6, 0x08},', ','),
    ('ref 0x4848e3 missed', '{0x4848e3, 0x0c}, ', ''),
    ('ref 0x48494f missed', '{0x48494f, 0x10}, ', ''),
    ('ref 0x48496f missed', ', {0x48496f, 0x14},', ','),
    ("the walk's end not written", 'at[n] = END_REF, value[n] = (DWORD)(ULONG_PTR)table + total * ROW, size[n++] = 4;', ''),
    ('code left writable', 'if (!VirtualProtect((void*)(ULONG_PTR)at[i], size[i], prot, &unused)) {', 'if (0) {'),
    ('no protection change', 'if (!VirtualProtect((void*)(ULONG_PTR)at[i], size[i], PAGE_EXECUTE_READWRITE, &prot)) break;', 'prot = 0;'),
    ('the table left writable', 'if (!VirtualProtect(table, size, PAGE_READONLY, &prot)) {', 'if (0) {'),
    ('any exe taken', 'if (!known_exe()) {', 'if (0) {'),
    ('one site unchecked', 'if (at32(REFS[i].at) != TABLE + REFS[i].offset) return 0;', 'if (i && at32(REFS[i].at) != TABLE + REFS[i].offset) return 0;'),
    ('the image size unchecked', '|| nt->OptionalHeader.SizeOfImage < TABLE + OLD_ROWS * ROW - IMAGE', ''),
    ('family unchecked', '|| row->family > MAX_FAMILY', ''),
    ('tech unchecked', 'else return "tech is not none, clan, sphere or both";', ''),
    ('ammo unchecked', 'else return "ammo is not yes or no";', ''),
    ('id range unchecked', '|| !row->label || row->label >= 0x10000', ''),
    ('field count unchecked', 'if (n != 5) return "wants name family tech ammo label";', ''),
    ('row count unchecked', 'if (*count == MAX_ROWS - OLD_ROWS) return "too many rows, 127 weapons in all";', 'if (*count == MAX_ROWS - OLD_ROWS + 1) return "x";'),
    ('game names unchecked', 'if (game_has_name(rows[*count].name)) return "the table already has that name";', ''),
    ('repeats unchecked', 'if (same_name(rows[i].name, rows[*count].name)) return "that name is listed twice";', ';'),
    ('text unchecked', 'return "a character that is not plain text";', 'continue;'),
    ('messages unchecked', 'if (!found) {', 'if (0) {'),
    ('file size unchecked', '&& n <= limit', ''),
    ('memory not freed', 'if (text) VirtualFree(text, 0, MEM_RELEASE);', ''),
    ('a bad row kept the good ones', 'if (bad) {', 'if (0) {'),
    ('no log', 'WriteFile(f, line, (DWORD)(p - line), &done, NULL);', ''),
]


def breaks(folder, base_bytes):
    source = (HERE / 'proxy' / 'iforce2_proxy.c').read_text()
    missed = []
    for what, old, new in BREAKS:
        if old not in source:
            missed.append(f'{what}: its text is gone')
            continue
        src = folder / 'broken.c'
        src.write_text(source.replace(old, new))
        out = folder / 'broken.dll'
        run = subprocess.run(pi.build_command(src, HERE / 'proxy' / 'iforce2_proxy.def', out), capture_output=True)
        if run.returncode:
            missed.append(f'{what}: did not build: {run.stderr[:200]!r}')
            continue
        dll = out.read_bytes()
        try:
            found = suite(base_bytes, dll)
        except (UcError, KeyError, struct.error, ValueError) as e:
            found = [repr(e)]
        print(f'  {what}: {found[0] if found else "NOT CAUGHT"}'[:125])
        if not found:
            missed.append(what)
    return len(BREAKS) - len(missed), missed


def main():
    base_bytes = t.OFFICIAL
    if '--base' in sys.argv:
        path = Path(sys.argv[sys.argv.index('--base') + 1])
        base_bytes = path.read_bytes()
        print(f'base {path.name}, sha256 {hashlib.sha256(base_bytes).hexdigest()}')
    t.ORIGINAL = base_bytes
    failures = []
    (ROOT / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='weaponproxy') as name:
        folder = Path(name)
        dll_path = folder / 'IFORCE2.dll'
        pi.build(dll_path)
        dll = dll_path.read_bytes()
        print(f'built {len(dll)} bytes, sha256 {hashlib.sha256(dll).hexdigest()}')
        print('command:', ' '.join(pi.build_command('proxy/iforce2_proxy.c', 'proxy/iforce2_proxy.def', 'IFORCE2.dll')))
        found = suite(base_bytes, dll)
        print(f'the suite: {"ok" if not found else f"{len(found)} failures"}')
        failures += found
        m = Proxied(base_bytes, dll, [dict(PPC, name=f'G{i}', tech=3, ammo=True) for i in range(pw.MAX_ROWS_PROXY - pw.OLD_ROWS)])
        items, _ = t.fill_counts(m)
        print(f'127 weapons: the store fill holds {items} items')
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            found = check_installer(folder, dll_path)
        print(f'the installer: {"ok" if not found else found}')
        failures += found
        if failures:
            for f in failures[:40]:
                print(' ', f)
            print('FAILED')
            return 1
        n, missed = breaks(folder, base_bytes)
        print(f'breaks: {n} caught' + (f', missed {missed}' if missed else ''))
        failures += missed
    print('FAILED' if failures else 'all good')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
