"""Pirate's Moon's zSound (src/pm/gamez/zsound, docs/pm-map.md step 7)
against PM's exe, on PM's sounds.zrd, soundsH.zbd and soundsL.zbd.

    cmake --build build && .venv/bin/python tests/differential/test_pm_sound.py [--quick] [--seed N]

MW3's test_sound.py on PM's side: PM's own zSound runs in Unicorn at its own
addresses (cdecl where MW3's took ECX and EDX), its DirectSound replaced by
test_sound's model, over sounds.zrd's tree written into the emulator. The
port runs MW3's probe (tests/probe_sound.cpp, built into pmprobe) with PM's
zgame::Table, so where PM changed zSound its functions run, MW3's elsewhere.
Compared as in test_sound.py: every DirectSound call, error report and
callback, every result, and the whole state; plus PM's range scale, default
queue priority and COMPILE_FORMAT.

Scenarios: start-up and loading every set (SoundLOD 0 and 2, archives on and
off, no engine variables, the callbacks or not, the range scale 0.5), every
group's picks, random play as test_sound.py's on PM's sets; sets loaded
again and unloaded by count and by names in other cases; then sounds.zrd
changed: INCLUDE_SETS, a SUB_SET naming a set read later, a set named
twice, a keyword before a set's sounds, DEFAULT_QUEUE_PRIORITY and
QPRIORITY (the queue's order), COMPILE_FORMAT as names, PLAYBACK_FORMAT in
stereo; and the CD's notify in each mode.

What the port does not reproduce, as in MW3: the error reports name MW3's
source files and lines (PM's are other lines of other paths), so they are
compared by level and text.
"""
import ctypes
import random
import shutil
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import test_sound as ts  # noqa: E402
import zrd  # noqa: E402
from original import FTOL, IAT_FTOL, ROOT, Original  # noqa: E402
from test_pm_zar import crc32  # noqa: E402
from test_sound import BIG, BIG_SIZE, WINDOW, Mismatch, bf, cstr, fb, s32  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

PM = ROOT / 'game' / 'pm'
READER = PM / 'zbd' / 'reader.zbd'

# The port: MW3's probe in pmprobe, with test_sound's argument types.
probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))
for name, f in list(vars(ts.probe).items()):
    if name.startswith('probe_snd_'):
        g = getattr(probe, name)
        g.argtypes, g.restype = f.argtypes, f.restype
probe.probe_pm_snd_extra.restype = ctypes.c_char_p
probe.probe_pm_snd_range_scale.argtypes = [ctypes.c_float]
ts.probe = probe  # test_sound's Both and comparisons call the port through this

# PM's globals (MW3's in test_sound.STATE).
STATE = dict(initialized=0x613240, open=0x613244, api=0x61327c, primary=0x613284, primary_fmt=0x613288,
             playback=0x61328c, current=0x613298, caps=0x6132a0, sos=0x6135cc, inv_sos=0x6135d0,
             listener_set=0x6133f4, muted=0x613444, volume=0x613450, sets=0x8c1ae4, groups=0x8c1b28,
             playing=0x8c1b2c, spare=0x8c1b30, finished=0x8c1b34, finished_count=0x8c1b38, ready=0x8c1b24,
             qclock=0x61346c, qnext=0x613478, queued=0x613474, queue=0x613470, cue_sound=0x61345c,
             cue_voice=0x613460, cue_index=0x613464, cue_limit=0x613468, fades=0x8c1afc, free_fades=0x8c1b10,
             search=0x6133ec, archives_on=0x613214, dt=0x8c1b4c, total=0x8c1b58, range_scale=0x613488,
             qpriority=0x8c1af0, compile=0x61324c)
ts.STATE = STATE  # Both.time writes the clock through it
# PM's functions and their arguments from test_sound.Both's MW3 ones (ECX, EDX, stack).
FN = {
    'init': (0x5912a0, lambda c, d, a: [c]),
    'load': (0x591840, lambda c, d, a: [c, d, a[0]]),  # no demo flag
    'load_set': (0x5908f0, lambda c, d, a: [c]),
    'unload_set': (0x590920, lambda c, d, a: [c]),
    'find': (0x590a70, lambda c, d, a: [c]),
    'play': (0x58f7d0, lambda c, d, a: [c, a[0]]),
    'play_at': (0x58fc70, lambda c, d, a: [c, a[0], d, a[1]]),
    'play_fading': (0x5929e0, lambda c, d, a: [c, a[0], d, a[1], a[2], a[3]]),
    'status': (0x58fd60, lambda c, d, a: [c]),
    'stop': (0x58fe70, lambda c, d, a: [c]),
    'freq': (0x5906e0, lambda c, d, a: [c, a[0]]),
    'vvol': (0x5907b0, lambda c, d, a: [c, a[0]]),
    'lock': (0x590840, lambda c, d, a: [c]),
    'unlock': (0x590860, lambda c, d, a: [c]),
    'fade': (0x592a50, lambda c, d, a: [c, a[0], d]),
    'update3d': (0x592c70, lambda c, d, a: [c, d, a[0], a[1]]),
    'frame': (0x58f390, lambda c, d, a: [c, d]),
    'listener': (0x592b80, lambda c, d, a: [c, d]),
    'volume': (0x5906c0, lambda c, d, a: [a[0]]),
    'cue': (0x590820, lambda c, d, a: [c, d]),
    'snapshot': (0x58ff90, lambda c, d, a: []),
    'snap_stop': (0x5904b0, lambda c, d, a: [c]),
    'snap_resume': (0x590530, lambda c, d, a: [c]),
    'snap_free': (0x590580, lambda c, d, a: [c]),
    'take': (0x594010, lambda c, d, a: []),
    'restore': (0x594020, lambda c, d, a: [c]),
    'free_playing': (0x594080, lambda c, d, a: [c]),
    'reset': (0x592010, lambda c, d, a: []),
    'stop_queue': (0x5915e0, lambda c, d, a: [c]),
    'close': (0x591f40, lambda c, d, a: [c]),
    'shutdown': (0x591700, lambda c, d, a: []),
    'range_scale': (0x591830, lambda c, d, a: [a[0]]),
}
OP_NEW, OP_DELETE, REALLOC_COPY = 0x5ce8c6, 0x5ce8c0, 0x596520
LIST_POOL, POOL_GLOBALS = 0x577690, (0x8c1538, 0x8c153c, 0x8c1540, 0x8c1544)
STATIC_LISTS = ((0x5908b0, 0x8c1ae4), (0x592490, 0x8c1afc), (0x592540, 0x8c1b0c))  # ctor, sentinel slot


class PmCpu(Original):
    """PM's exe at its own addresses: no translation but for the import slots."""

    def __init__(self):
        super().__init__(exe='pm')
        self.mw3 = self.tr
        self.tr = lambda a, size=1: a
        self.uc = self._uc


def build(cpu_alloc, write, value):
    """sounds.zrd's tree (zrd.node's values) as the reader leaves it: a node is
    type and value; a list's items follow its count (zreader.md)."""
    def node(at, v):
        if isinstance(v, list):
            items = cpu_alloc(8 * (len(v) + 1))
            write(items, struct.pack('<iI', 1, len(v) + 1))
            for k, x in enumerate(v):
                node(items + 8 * (k + 1), x)
            write(at, struct.pack('<iI', 4, items))
        elif isinstance(v, str):
            b = v.encode('latin-1') + b'\0'
            p = cpu_alloc(len(b))
            write(p, b)
            write(at, struct.pack('<iI', 3, p))
        elif isinstance(v, float):
            write(at, struct.pack('<if', 2, v))
        else:
            write(at, struct.pack('<ii', 1, v))
    root = cpu_alloc(8)
    node(root, value)
    return root


def sounds_tree(path=READER):
    data = path.read_bytes()
    s, n = next((s, n) for name, s, n in zrd.entries(data) if name.lower() == 'sounds.zrd')
    return zrd.node(data[s:s + n], 0)[0]


class PmEmu(ts.Emu):
    """PM's zSound with test_sound's DirectSound model."""

    def __init__(self, game=PM):
        cpu = self.cpu = PmCpu()
        self.game = game
        self.tree = sounds_tree()
        cpu.import_code(cpu.mw3(IAT_FTOL), FTOL)
        cpu.uc.mem_map(BIG, BIG_SIZE)
        self.next = BIG
        self.sizes = {}
        self.log = []

        def imp(slot, fn, pop=0):
            cpu.stub_import(cpu.mw3(slot), fn, pop)
        imp(0x5bb664, lambda c: self.alloc(c.arg(0)))                       # malloc
        imp(0x5bb60c, lambda c: self.alloc(c.arg(0) * c.arg(1), zero=True))  # calloc
        imp(0x5bb65c, lambda c: self.free(c.arg(0)))                         # free
        imp(0x5bb4bc, lambda c: self.alloc(max(c.arg(0), 1)))               # new
        imp(0x5bb4c8, lambda c: self.free(c.arg(0)))                         # delete
        imp(0x5bb670, self.strdup)
        imp(0x5bb668, self.rand)
        imp(0x5bb580, self.stricmp)
        imp(0x5bb658, self.sprintf)
        imp(0x5bb628, self.fopen)
        imp(0x5bb594, self.fread)
        imp(0x5bb630, lambda c: 0)                                           # fclose
        imp(0x5bb598, self.ftell)
        imp(0x5bb59c, self.fseek)
        imp(0x5bb05c, self.ds_create, 12)                                    # DirectSoundCreate
        sf = cpu.stub_function
        sf(OP_NEW, lambda c: self.alloc(max(c.arg(0), 1)))
        sf(OP_DELETE, lambda c: self.free(c.arg(0)))
        sf(REALLOC_COPY, self.realloc)
        sf(0x406cb0, self.zerr)
        sf(0x578630, lambda c: self.root if cstr(c, c.arg(0)).lower() == b'sounds.zrd' else 0)
        sf(0x593190, lambda c: self.log.append((40,)) or 1)
        sf(0x5935f0, lambda c: self.log.append((41,)) or 1)
        sf(0x596f10, self.path_new)
        sf(0x596f50, self.path_add)
        sf(0x596f30, lambda c: 0)
        sf(0x5970d0, self.path_find)
        sf(0x596ea0, lambda c: 1 if (self.game / cstr(c, c.arg(0)).decode().replace('\\', '/')).is_file() else 0)
        sf(0x5b9070, self.find_var)
        sf(0x597300, self.zar_init)
        sf(0x597320, lambda c: 0)
        sf(0x597340, self.zar_open, 4)
        sf(0x5974a0, lambda c: 1)
        sf(0x5978f0, self.zar_find, 4)
        sf(0x597990, self.zar_read, 12)
        self.callbacks()
        self.make_ds()
        cpu.fpu_control(0x7f)
        self.base = self.next
        self.strings = {}

    # --- what differs from MW3's stubs: PM passes on the stack
    def path_new(self, c):
        h = self.alloc(4)
        self.paths = getattr(self, 'paths', {})
        self.paths[h] = cstr(c, c.arg(0)).decode()
        return h

    def path_add(self, c):
        self.paths[c.arg(0)] += ';' + cstr(c, c.arg(1)).decode()
        return 0

    def path_find(self, c):
        lst, name = c.arg(0), c.arg(1)
        if not lst or lst not in getattr(self, 'paths', {}):
            return 0
        base = cstr(c, name).decode().replace('/', '\\').split('\\')[-1].split(':')[-1]
        for d in reversed([d for d in self.paths[lst].split(';') if d]):
            d = d.rstrip('\\')
            if (self.game / (d + '\\' + base).replace('\\', '/')).is_file():
                return self.string(d + '\\' + base)
        return 0

    def find_var(self, c):
        if not self.vars:
            return 0
        return {b'SoundLOD': self.var_lod, b'MuteSound': self.var_mute,
                b'SoundVolume': self.var_volume}.get(cstr(c, c.arg(0)), 0)

    def fopen(self, c):
        p = self.game / cstr(c, c.arg(0)).decode().replace('\\', '/')
        if not p.is_file():
            return 0
        self.files = getattr(self, 'files', {})
        h = 0x6000 + len(self.files) * 0x10
        self.files[h] = [p.read_bytes(), 0]
        return h

    def zar_open(self, c):
        p = self.game / cstr(c, c.arg(0)).decode().replace('\\', '/')
        if not p.is_file():
            return 0
        if not hasattr(self, 'zcache'):
            self.zcache = {}
        if p not in self.zcache:
            data = p.read_bytes()
            self.zcache[p] = (data, list(zrd.entries(data)))
        self.zars[c.ecx] = self.zcache[p]
        return 1

    def callbacks(self):
        cpu = self.cpu

        def name_len(obj):
            kind = struct.unpack('<i', cpu.read(obj, 4))[0]
            p = struct.unpack('<I', cpu.read(obj + (8 if kind == 0 else 4), 4))[0]
            return kind, len(cstr(cpu, p)) if p else 0

        def load_cb(c):  # (object, its node)
            kind, n = name_len(c.arg(0))
            self.log.append((50, kind, n))
            self.cb_count += 1
            return 0x7000 + self.cb_count - 1

        def free_cb(c):  # (object, user)
            kind, _ = name_len(c.arg(0))
            self.log.append((51, kind, c.arg(1)))
            return 0

        def cue_cb(c):  # (index), ECX the sound
            p = struct.unpack('<I', cpu.read(c.ecx + 8, 4))[0]
            self.log.append((52, c.arg(0), len(cstr(cpu, p)) if p else 0))
            return 0
        self.load_cb, self.free_cb, self.cue_cb = cpu.code(b'\xc3'), cpu.code(b'\xc3'), cpu.code(b'\xc3')
        cpu.stub_function(self.load_cb, load_cb)
        cpu.stub_function(self.free_cb, free_cb)
        cpu.stub_function(self.cue_cb, cue_cb)

    # --- a scenario
    def begin(self, lod, mute, volume, vars_, archives_on, caps, seed, tree=None):
        cpu = self.cpu
        self.next = self.base
        self.sizes = {}
        self.strings = {}
        self.paths = {}
        for a in POOL_GLOBALS:  # the list pool's nodes were on the heap just rolled back
            cpu.u32(a, 0)
        cpu.call(LIST_POOL, stack_args=(0x200,))
        # zSound's globals, then the static std::lists made again, empty.
        cpu.write(0x613240, bytes(0x61348c - 0x613240))
        cpu.write(0x8c1ae0, bytes(0x8c1b60 - 0x8c1ae0))
        for ctor, _ in STATIC_LISTS:
            cpu.call(ctor, ecx={0x5908b0: 0x8c1ae0, 0x592490: 0x8c1af8, 0x592540: 0x8c1b08}[ctor])
        # The image's values (test_sound's begin sets MW3's).
        cpu.u32(STATE['archives_on'], archives_on)
        cpu.u32(STATE['sos'], fb(345.0))
        cpu.u32(STATE['inv_sos'], 0x3b3df59d)
        for k, (r, b, ch) in enumerate(((44100, 16, 2), (22050, 16, 1), (11025, 8, 1))):
            cpu.write(STATE['compile'] + 16 * k, struct.pack('<4I', 0, r, b, ch))
        self.vars = vars_
        self.var_lod, self.var_mute, self.var_volume = self.alloc(4), self.alloc(4), self.alloc(4)
        cpu.u32(self.var_lod, lod)
        cpu.u32(self.var_mute, mute)
        cpu.u32(self.var_volume, fb(volume))
        self.caps = caps
        self.extra = 0
        self.seed = seed
        self.cb_count = 0
        self.bufs = [None]
        self.scratch_size = 0
        self.gids, self.next_gid = {}, 1
        self.root = build(self.alloc, cpu.write, self.tree if tree is None else tree)
        self.log = []

    def call(self, name, ecx=0, edx=0, args=()):
        addr, conv = FN[name]
        self.cpu.call(addr, stack_args=conv(ecx, edx, list(args)))
        return self.cpu.uc.reg_read(UC_X86_REG_EAX)

    # --- PM's layout: sets and their sounds in std::lists
    def sets(self):
        return self.std_list(self.u32(STATE['sets']))

    def sounds(self, s):
        return self.std_list(self.u32(s + 4))

    def set_name(self, s):
        return cstr(self.cpu, self.u32(s + 0xc)).decode('latin-1')

    def voice_desc(self, v):
        if not v:
            return '-'
        if self.i32(v + 4) == 1:
            if v not in self.gids:
                self.gids[v] = self.next_gid
                self.next_gid += 1
            return f'g{self.gids[v]}'
        for i, s in enumerate(self.sets()):
            for j, snd in enumerate(self.sounds(s)):
                if v == snd + 0x4c:
                    return f's{i}.{j}.0'
                dn, arr = self.i32(snd + 0x94), self.u32(snd + 0x98)
                for k in range(dn):
                    if self.u32(arr + 4 * k) == v:
                        return f's{i}.{j}.{k + 1}'
        return '?'

    def dump(self):
        u, i32, cs = self.u32, self.i32, lambda p: cstr(self.cpu, p).decode('latin-1') if p else '-'
        pb = [u(STATE['playback'] + 4 * k) for k in range(3)]
        cur = u(STATE['current'])
        out = f'state {i32(STATE["initialized"])} {i32(STATE["open"])} caps{u(STATE["caps"]):08x} '
        out += f'pf{pb.index(cur) if cur in pb and cur else -1}'
        for p in pb:
            out += f' [{i32(p + 4)} {i32(p + 8)} {i32(p + 12)}]' if p else ' [-]'
        pf = u(STATE['primary_fmt'])
        f = struct.unpack('<HHIIHH', self.cpu.read(pf, 16)) if pf else (0, 0, 0, 0, 0, 0)
        out += f' prim{self.bidx(u(STATE["primary"]))} {f[1]} {f[2]} {f[3]} {f[4]} {f[5]}'
        sp = u(STATE['search'])
        search = self.paths.get(sp, '-') if sp else '-'
        out += (f' sos{u(STATE["sos"]):08x} {u(STATE["inv_sos"]):08x} lis{i32(STATE["listener_set"])} '
                f'search {search} muted {i32(STATE["muted"])} vol{u(u(STATE["volume"])):08x}\n')
        for i, s in enumerate(self.sets()):
            snds = self.sounds(s)
            subs = self.std_list(u(s + 0x18))
            out += f'set {i} {self.set_name(s)} {i32(s + 0x10)} {len(snds)} subs'
            for sb in subs:
                out += f' {self.set_name(sb) if sb else "-"}'
            out += '\n'
            for a in snds:
                out += (f' {cs(u(a + 8))} {cs(u(a + 4))} f{u(a + 0xc):x} q{u(a + 0x14):08x} v{u(a + 0x18):08x} '
                        f'r{u(a + 0x1c):08x} {u(a + 0x20):08x} a{u(a + 0x30):08x} c{u(a + 0x3c)} u{i32(a + 0xcc)}')
                if u(a + 0x40):
                    for k in range(u(a + 0x3c)):
                        out += f' {u(u(a + 0x40) + 4 * k):08x}/{u(u(a + 0x48) + 8 * k)}/{u(u(a + 0x44) + 4 * k):08x}'
                for k in range(3):
                    fm = a + 0x9c + 0x10 * k
                    out += f' {{{cs(u(fm))}}}' if u(fm) else f' {{{i32(fm + 4)} {i32(fm + 8)} {i32(fm + 12)}}}'
                out += self.voice_txt(a + 0x4c)
                for k in range(i32(a + 0x94)):
                    out += self.voice_txt(u(u(a + 0x98) + 4 * k))
                out += '\n'
        for g in self.list_items(u(STATE['groups'])):
            out += (f'group {cs(u(g + 4))} d{i32(g + 8)} s{i32(g + 0xc)} f{u(g + 0x10):08x} r{self.i16(g + 0x14)} '
                    f'{u(g + 0x18):08x} {u(g + 0x1c):08x} x{i32(g + 0x28)}:')
            for k in range(i32(g + 0x20)):
                out += self.entry_txt(u(g + 0x24) + 0x1c * k)
            out += '\n'
        pl = u(STATE['playing'])
        items = self.list_items(pl)
        out += f'playing {len(items) if pl else -1}\n'
        for gv in items:
            out += self.gv_txt(gv)
        sp_ = u(STATE['spare'])
        spare = self.list_items(sp_)
        out += f'spare {len(spare) if sp_ else -1}:' + ''.join(f' {self.voice_desc(g)}' for g in spare)
        fin = u(STATE['finished'])
        out += f' fin {self.voice_desc(fin) if fin else "-"} {i32(STATE["finished_count"])} ready {i32(STATE["ready"])}\n'
        out += f'queue {u(STATE["qclock"]):08x} {u(STATE["qnext"]):08x} {self.voice_desc(u(STATE["queued"]))}:'
        for q in self.list_items(u(STATE['queue'])):
            out += (f' ({cs(u(u(q) + 8))} {u(q + 4):08x} {i32(q + 8)} {i32(q + 0xc)} {u(q + 0x10):08x} '
                    f'{u(q + 0x1c):08x} {u(q + 0x28):08x} {u(q + 0x2c):08x})')
        cso = u(STATE['cue_sound'])
        out += (f'\ncue {cs(u(cso + 8)) if cso else "-"} {self.voice_desc(u(STATE["cue_voice"]))} '
                f'{u(STATE["cue_index"])} {u(STATE["cue_limit"])}\n')
        out += f'fades {u(STATE["free_fades"])}:'
        for fd in self.std_list(u(STATE['fades'])):
            out += f' ({self.voice_desc(u(fd + 8))} {u(fd):08x} {u(fd + 4):08x} {i32(fd + 0xc)})'
        out += '\n'
        out += f'pm rs{u(STATE["range_scale"]):08x} qp{i32(STATE["qpriority"])}'
        for k in range(3):
            fm = STATE['compile'] + 0x10 * k
            out += f' {{{cs(u(fm))}}}' if u(fm) else f' {{{i32(fm + 4)} {i32(fm + 8)} {i32(fm + 12)}}}'
        return out + '\n'


def compare_logs(what, emu):
    """test_sound's, with the error reports' lines left out (PM's are its own)."""
    def strip(log):
        return [(e[0], e[1]) + tuple(e[3:]) if e[0] == 60 else e for e in log]
    got = strip(ts.port_log())
    want = strip(emu.log)
    emu.log = []
    for e in want:
        key = ts.OPS.get(e[0], e[0])
        ts.COVERAGE[key] = ts.COVERAGE.get(key, 0) + 1
    if got != want:
        n = next((k for k in range(min(len(got), len(want))) if got[k] != want[k]), min(len(got), len(want)))
        lines = [f'{what}: calls differ']
        for k in range(max(0, n - 12), min(max(len(got), len(want)), n + 6)):
            a = ts.fmt_entry(want[k]) if k < len(want) else '-'
            b = ts.fmt_entry(got[k]) if k < len(got) else '-'
            lines.append(f'  {k}: original {a}   port {b}' + ('   <--' if k == n else ''))
        raise Mismatch('\n'.join(lines))
    return len(got)


ts.compare_logs = compare_logs


def compare_dumps(what, emu):
    a = emu.dump()
    b = probe.probe_snd_dump().decode('latin-1') + probe.probe_pm_snd_extra().decode('latin-1')
    if a != b:
        al, bl = a.splitlines(), b.splitlines()
        k = next((i for i in range(min(len(al), len(bl))) if al[i] != bl[i]), min(len(al), len(bl)))
        raise Mismatch(f'{what}: state differs at line {k}\n  original {al[k] if k < len(al) else "-"}\n'
                       f'  port     {bl[k] if k < len(bl) else "-"}')


ts.compare_dumps = compare_dumps


class Both(ts.Both):
    """test_sound's Both on PM: the statics after each begin, PM's layout."""

    def __init__(self, emu, lod=0, mute=0, volume=1.0, vars_=1, archives_on=1, caps=0x8, seed=1, tree=None):
        self.emu = emu
        emu.begin(lod, mute, volume, vars_, archives_on, caps, seed, tree)
        probe.probe_snd_begin(lod, mute, volume, vars_, archives_on, caps, seed)
        probe.probe_pm_snd_statics()
        self.objs, self.voices, self.snaps, self.lists = [0], [0], [0], [0]
        self.calls = 0
        self.dt, self.total = 0.0, 0.0
        self.dead = set()
        self.time(0.0)

    def load(self, cb=1, demo=0):
        e = self.emu
        a = e.call('load', ecx=WINDOW, edx=e.string('sounds.zrd'), args=(e.load_cb if cb else 0, demo))
        self.check('load', a, probe.probe_snd_load(WINDOW, b'sounds.zrd', cb, demo))

    def range_scale(self, s):
        self.emu.call('range_scale', args=(fb(s),))
        probe.probe_pm_snd_range_scale(s)
        self.check(f'range scale {s}', 0, 0)

    def load_set(self, name, unload=False):
        e = self.emu
        if unload:  # the last unload frees its sounds' (and subsets') duplicate voices
            todo = [x for x in e.sets() if e.set_name(x).lower() == name.lower()][:1]
            freed = set()
            while todo:
                x = todo.pop()
                if x in freed or e.i32(x + 0x10) != 1:
                    continue
                freed.add(x)
                todo += [t for t in e.std_list(e.u32(x + 0x18)) if t]
            owners = {snd for x in freed for snd in e.sounds(x)}
            for k, a in enumerate(self.voices):
                if a and e.i32(a + 4) == 0 and not e.voice_desc(a).endswith('.0') and e.u32(a + 0x2c) in owners:
                    self.dead.add(k)
        e.call('unload_set' if unload else 'load_set', ecx=e.string(name))
        (probe.probe_snd_unload_set if unload else probe.probe_snd_load_set)(name.encode('latin-1'))
        self.check(('unload ' if unload else 'load ') + name, 0, 0)


ts.Both = Both
probe.probe_pm_snd_statics.argtypes = []


def names_of(tree):
    top = tree[0] if isinstance(tree[0], list) else tree
    kv = {top[i]: top[i + 1] for i in range(0, len(top) - 1, 2) if isinstance(top[i], str)}
    sets = [kv['SETS'][i] for i in range(0, len(kv['SETS']), 2)]
    sounds = [x[0] for i in range(1, len(kv['SETS']), 2) for x in kv['SETS'][i] if isinstance(x, list)]
    groups = [g[0] for g in kv['SOUND_GROUPS'] if isinstance(g, list) and g and isinstance(g[0], str)]
    return sets, sounds, groups


def scenario_load(emu, sets, lod, archives_on, vars_, cb, caps=0x8, scale=None):
    b = Both(emu, lod=lod, vars_=vars_, archives_on=archives_on, caps=caps)
    b.init()
    if scale is not None:
        b.range_scale(scale)
    b.load(cb)
    b.compare('after load')
    for s in sets:
        b.load_set(s)
    b.compare('after loading sets')
    for s in sets[::3]:
        b.load_set(s, unload=True)
    b.compare('after unloading')
    b.close(cb)
    b.shutdown()
    b.compare('after shutdown')
    return b.calls


def scenario_counts(emu):
    """Loads counted: twice, unloaded once, a name in other cases, a subset
    loaded on its own and through its set, unload_all through the close."""
    b = Both(emu, lod=0)
    b.init()
    b.load(1)
    for name, unload in (('MENU', 0), ('MENU', 0), ('menu', 1), ('Menu', 0), ('c1m1', 0), ('c1', 0),
                         ('C1', 1), ('c1m1', 1), ('c1', 0), ('mech', 0), ('MECH', 0), ('mech', 1),
                         ('no_such_set', 0), ('no_such_set', 1)):
        b.load_set(name, unload=bool(unload))
        b.compare(f'counts {name} {unload}')
    b.close(1)
    b.compare('counts closed')
    b.shutdown()
    return b.calls


def scenario_play(emu, names, seed, steps, sets, mute=0, lod=2, extra=0):
    """test_sound.scenario_play on PM's sets."""
    r = random.Random(seed)
    b = Both(emu, lod=lod, mute=mute, volume=r.choice([1.0, 0.5, 0.8]), seed=seed)
    emu.extra = extra
    probe.probe_snd_extra_controls(extra)
    b.init()
    b.load(1, 0)
    for s in sets:
        b.load_set(s)
    loaded = [(cstr(emu.cpu, emu.u32(a + 8)).decode('latin-1'), bool(emu.u32(a + 0xc) & 4))
              for x in emu.sets() for a in emu.sounds(x) if emu.u32(a + 0xc) & 8]
    three_d = [n for n, d in loaded if d]
    objs = []
    for n in (r.sample([n for n, _ in loaded], min(70, len(loaded))) + three_d[:30] +
              r.sample(names[2], 40) + ['no_such_sound']):
        o = b.find(n)
        if o:
            objs.append(o)
    for n in ('briefing_m1', 'briefing_ia'):
        o = b.find(n)
        if o:
            b.cue(o)
            objs.append(o)
    b.compare('ready')
    snaps, lists = [], []
    for step in range(steps):
        k = r.random()
        live = [v for v in range(1, len(b.voices)) if v not in b.dead]
        if k < 0.18:
            b.play(r.choice(objs), r.choice([1.0, 0.7, 0.3, 0.0005, 1.2]))
        elif k < 0.33:
            b.play_at(r.choice(objs), ts.rpos(r), r.choice([1.0, 0.6]), r.choice([None, ts.rpos(r)]))
        elif k < 0.36:
            b.play_fading(r.choice(objs), r.choice([None, ts.rpos(r)]), r.choice([1.0, 0.2]), None,
                          r.choice([-1.0, 0.0, 0.5, 1.0]), r.randint(0, 1))
        elif k < 0.50:
            b.time(r.choice([1 / 30, 1 / 60, 0.1, 0.5, 2.0]))
            b.frame(0, r.choice([1, 1, 0]))
        elif k < 0.55 and live:
            v = r.choice(live)
            b.voice_op('status', v, (), probe.probe_snd_status(v))
        elif k < 0.60 and live:
            v = r.choice(live)
            b.voice_op('stop', v, (), probe.probe_snd_stop(v))
        elif k < 0.63 and live:
            v, t = r.choice(live), r.uniform(-0.2, 1.2)
            b.voice_op('freq', v, (fb(t),), probe.probe_snd_freq(v, t))
        elif k < 0.66 and live:
            v, x = r.choice(live), r.choice([1.0, 0.5, 0.1])
            b.voice_op('vvol', v, (fb(x),), probe.probe_snd_vvol(v, x))
        elif k < 0.68 and live:
            v, on = r.choice(live), r.randint(0, 1)
            b.voice_op('lock' if on else 'unlock', v, (), probe.probe_snd_lock(v, on))
        elif k < 0.72:
            m = list(r.choice(ts.MATS))
            m[9:12] = ts.rpos(r)
            b.listener(r.choice([m, None, m]), r.choice([None, ts.rpos(r), (0, 0, 0)]))
        elif k < 0.78 and live:
            v = r.choice(live)
            if emu.i32(b.voices[v] + 4) == 0:
                b.update3d(v, r.choice([None, ts.rpos(r)]), r.choice([None, ts.rpos(r)]), r.randint(0, 1))
        elif k < 0.81 and live:
            v = r.choice(live)
            if emu.i32(b.voices[v] + 4) == 0:
                b.fade(v, r.randint(0, 1), r.choice([0.0, 0.25, 1.0, 0.6]))
        elif k < 0.83:
            for _ in range(r.randint(1, 8)):
                n = len(emu.bufs) - 1
                if n:
                    b.finish(r.randint(1, n))
        elif k < 0.85:
            snaps.append(b.snapshot())
        elif k < 0.87 and snaps:
            s = r.choice(snaps)
            op = r.choice([0, 1, 1, 2])
            b.snap_op(s, op)
            if op == 2:
                snaps.remove(s)
        elif k < 0.885:
            lists.append(b.take())
        elif k < 0.90 and lists:
            b.groups_op(lists.pop(r.randrange(len(lists))), 0)
        elif k < 0.91:
            b.volume(r.choice([1.0, 0.5, 0.9]))
        elif k < 0.92:
            b.simple('reset', 0, probe.probe_snd_reset)
        elif k < 0.93:
            f = r.randint(0, 1)
            b.simple('stop_queue', f, lambda f=f: probe.probe_snd_stop_queue(f))
        elif k < 0.935 and not snaps and not emu.std_list(emu.u32(STATE['fades'])):
            s = r.choice(sets)
            b.load_set(s, unload=True)
            b.load_set(s)
        elif k < 0.96:
            o = r.choice(objs)
            for _ in range(r.randint(4, 14)):
                b.play(o, 1.0)
        if step % ts.DUMP_EVERY == 0:
            b.compare(f'step {step}')
    b.compare('end')
    for l in lists:
        b.groups_op(l, 1)
    b.close(1)
    b.shutdown()
    return b.calls


# --- sounds.zrd changed ---------------------------------------------------------
def top_of(tree):
    return tree[0] if isinstance(tree[0], list) else tree


def put(top, key, value):
    for i in range(0, len(top) - 1, 2):
        if top[i] == key:
            top[i + 1] = value
            return
    top += [key, value]


def get(top, key):
    for i in range(0, len(top) - 1, 2):
        if top[i] == key:
            return top[i + 1]
    return None


def variants(tree):
    """(what, tree, sets to load, sounds to queue): PM's reader paths the
    shipped file does not take."""
    import copy
    out = []

    def var(what, fn, load=(), queue=()):
        t = copy.deepcopy(tree)
        fn(top_of(t))
        out.append((what, t, list(load), list(queue)))
    var('INCLUDE_SETS', lambda t: put(t, 'INCLUDE_SETS', ['MENU', 'c1', 'nope', 3, 'mech']), ['MENU', 'mech'])
    var('INCLUDE_SETS empty', lambda t: put(t, 'INCLUDE_SETS', []), ['MENU'])

    def forward(t):  # a subset named before its own entry, and a set named twice
        s = get(t, 'SETS')
        s[:0] = ['early', [['early_one', 'sfx_button1.wav'], 'SUB_SET', ['late', 'MENU']]]
        s += ['late', [['late_one', 'sfx_button2.wav', 'VOLUME', 0.5]], 'early', [['early_two', 'sfx_button6.wav']]]
    var('SUB_SET forward, a set twice', forward, ['early', 'late'])

    def keyword(t):  # a keyword before the sounds steps over the first one
        s = get(t, 'SETS')
        s += ['kw', ['SUB_SET', ['MENU'], ['kw_one', 'sfx_button1.wav'], ['kw_two', 'sfx_button2.wav'],
                     'SUB_SET', ['mech'], ['kw_three', 'sfx_button6.wav']]]
    var('keyword first', keyword, ['kw'])

    def priorities(t):
        put(t, 'DEFAULT_QUEUE_PRIORITY', [3])
        s = get(t, 'SETS')
        lines = [['q_a', 'sfx_button1.wav', 'QUEUE', [2.0], 'QPRIORITY', [5]],
                 ['q_b', 'sfx_button2.wav', 'QUEUE', [], 'QPRIORITY', [1]],
                 ['q_c', 'sfx_button6.wav', 'QUEUE', [1.5], 'QPRIORITY', []],
                 ['q_d', 'mechlab_select.wav', 'QUEUE', [3], 'QPRIORITY', [9]],
                 ['q_e', 'mechlab_move.wav', 'QUEUE', 'QPRIORITY', [0]],
                 ['q_f', 'mechlab_error.wav', 'QUEUE', [4.0]],
                 ['q_g', 'cheesit.wav', 'QUEUE', [2.0], 'QPRIORITY', [2]],
                 ['q_h', 'mechlab_rollover.wav', 'QUEUE', [2.0], 'QPRIORITY', [4]]]
        s += ['queued', lines]
    # The default (3) first: lower numbers beat the head and go last, a 4
    # goes before the 5, the rest in order.
    var('QPRIORITY', priorities, ['queued'], ['q_c', 'q_b', 'q_a', 'q_g', 'q_h', 'q_e', 'q_d', 'q_f', 'q_a'])

    def formats(t):
        put(t, 'COMPILE_FORMAT', ['HIGH', 'hi.wav', 'MED', [8000, 8, 1], 'LOW', [1, 2], 'MEDIUM', [11025, 16, 2]])
        put(t, 'PLAYBACK_FORMAT', ['HIGH', [44100, 16, 2], 'MED', 'odd', 'MEDIUM', [22050, 8, 2], 'LOW', [11025]])
        get(t, 'SETS')[3] = [['arrow_rollover', 'cheesit.wav', 'HIGH', 'h.wav', 'LOW', [22050, 16, 2],
                              'RANGE', [10, 20], 'A3DDIST', 2, 'VOLUME', -1.0]]
    var('formats', formats, ['MENU'])
    def no_sets(t):
        k = t.index('SETS')
        del t[k:k + 2]
    var('no SETS', no_sets)
    return out


def scenario_variant(emu, what, tree, load, queue, bugs):
    b = Both(emu, lod=0, tree=tree)
    b.init()
    b.load(1)
    b.compare(f'{what}: after load')
    for s in load:
        b.load_set(s)
    b.compare(f'{what}: after loading')
    for n in queue:  # QUEUE lines: the queue's order, then played one by one
        o = b.find(n)
        if o:
            b.play(o, 1.0)
    b.compare(f'{what}: queued')
    for _ in range(len(queue) * 2):
        b.time(0.75)
        b.frame(0, 1)
        for k in range(1, len(emu.bufs)):
            b.finish(k)
    b.compare(f'{what}: played')
    b.close(1)
    b.shutdown()
    return b.calls


def ser(v):
    if isinstance(v, list):
        return struct.pack('<iI', 4, len(v) + 1) + b''.join(ser(x) for x in v)
    if isinstance(v, str):
        e = v.encode('latin-1')
        return struct.pack('<iI', 3, len(e)) + e
    if isinstance(v, float):
        return struct.pack('<if', 2, v)
    return struct.pack('<ii', 1, v)


def write_reader(dirpath, tree):
    """dir/zbd/reader.zbd, PM's version 2, holding sounds.zrd only."""
    data = ser(tree)
    entry = struct.pack('<II', 0, len(data)) + b'sounds.zrd'.ljust(64, b'\0') + bytes(0x94 - 8 - 64)
    (dirpath / 'zbd').mkdir(parents=True, exist_ok=True)
    (dirpath / 'zbd' / 'reader.zbd').write_bytes(data + entry + struct.pack('<III', 2, 1, crc32(data)))


# --- the CD's notify ------------------------------------------------------------
def cd_notify(emu):
    """PM's 0x005938c0 with play_track (0x005937a0) and the position's reset
    (0x005935b0) recorded, against the port's on a CD of files that all exist."""
    cpu = emu.cpu
    calls = []
    cpu.stub_function(0x5937a0, lambda c: calls.append(('play', c.arg(0), c.arg(1))) or 1)
    cpu.stub_function(0x5935b0, lambda c: calls.append(('clear',)) or 0)
    n = 0
    for opened in (0, 1):
        for mode in range(8):
            for status in (1, 2, 4, 8):
                for last in (1, 3):
                    calls.clear()
                    cpu.u32(0x613308, 2 if opened else 0)
                    cpu.u32(0x613300, mode)
                    cpu.u32(0x613334, 3)
                    cpu.u32(0x61330c, 0x77)
                    cpu.call(0x5938c0, stack_args=(status, 0x77))
                    want = list(calls)
                    got = probe.probe_pm_snd_cd_notify(opened, mode, 3, last, status)
                    # The original's play_track (shared, MW3's): a mode other than 6
                    # asking for the last track asked for returns at once.
                    cleared = ('clear',) in want
                    exp = last | (0 if cleared else 3) << 8
                    if want and want[-1][0] == 'play':
                        t, m = want[-1][1], want[-1][2]
                        if m == 6 or cleared or t != last:
                            exp = t | t << 8
                    if got != exp:
                        raise Mismatch(f'cd notify open {opened} mode {mode} status {status} last {last}: '
                                       f'original {want}, port asked for track {got}')
                    n += 1
    return n


def main():
    quick = '--quick' in sys.argv
    seed0 = int(sys.argv[sys.argv.index('--seed') + 1]) if '--seed' in sys.argv else 1
    if not READER.exists():
        raise SystemExit(f'needs {READER}')
    probe.probe_pm_snd_use()
    if not probe.probe_snd_setup(str(PM).encode()):
        raise SystemExit('probe could not open reader.zbd')
    emu = PmEmu()
    names = names_of(emu.tree)
    sets = names[0]
    calls = runs = 0
    try:
        for lod, arch, vars_, cb, scale in ([(0, 1, 1, 1, None), (2, 1, 1, 0, 0.5)] if quick else
                                            [(0, 1, 1, 1, None), (1, 1, 1, 0, None), (2, 1, 1, 1, None),
                                             (0, 0, 1, 1, None), (2, 1, 0, 1, None), (0, 1, 1, 1, 0.5)]):
            calls += scenario_load(emu, sets if not quick or lod == 2 else sets[:20], lod, arch, vars_, cb,
                                   scale=scale)
            runs += 1
        calls += scenario_load(emu, sets[:10], 2, 1, 1, 1, caps=0)
        calls += scenario_counts(emu)
        calls += ts.scenario_picks(emu, names, 40 if quick else 200)
        runs += 3
        play_sets = ['MENU', 'mech', 'common', 'dom', 'c1', 'c1m1', 'mcbandai', 'brief_c1m1', 'brief_ia', 'choppers']
        for k in range(3 if quick else 12):
            calls += scenario_play(emu, names, seed0 + k, 300 if quick else 1500, play_sets,
                                   mute=1 if k % 5 == 4 else 0, lod=(2, 0)[k % 2],
                                   extra=ts.CTRL_FREQ | ts.CTRL_PAN if k % 3 == 2 else 0)
            runs += 1
        tmp = Path(tempfile.mkdtemp(prefix='pm_sound_'))
        try:
            for what, tree, load, queue in variants(emu.tree):
                write_reader(tmp, tree)
                if not probe.probe_snd_setup(str(tmp).encode()):
                    raise SystemExit(f'{what}: the probe could not open the changed reader.zbd')
                probe.probe_pm_snd_mount(str(PM).encode())
                calls += scenario_variant(emu, what, tree, load, queue, 0)
                runs += 1
        finally:
            shutil.rmtree(tmp)
            probe.probe_snd_setup(str(PM).encode())
        notified = cd_notify(emu)
    except Mismatch as m:
        raise SystemExit(f'FAIL ({runs} runs passed)\n{m}')
    print('coverage:', ', '.join(f'{k} {v}' for k, v in sorted(ts.COVERAGE.items(), key=lambda kv: str(kv[0]))))
    print(f'ok: {runs} runs, {calls} DirectSound calls and callbacks identical, states identical '
          f'({len(sets)} sets of PM\'s sounds.zrd, its sound archives, {len(variants(emu.tree))} changed files); '
          f'the CD notify identical in {notified} cases')


if __name__ == '__main__':
    main()
