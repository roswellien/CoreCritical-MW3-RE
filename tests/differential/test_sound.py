"""zSound against the original x86 code (docs/sound.md).

    cmake --build build && .venv/bin/python tests/differential/test_sound.py [--quick] [--seed N]

The original's zSound runs in Unicorn with the FPU at 24 bits (the probe's fpu53 off) on
the real sounds.zrd (read by the original zReader) and the real soundsH.zbd /
soundsL.zbd and loose WAVs, its DirectSound replaced by Python COM objects
that model DirectSound and record every call; the port runs on the same
model in C++ (tests/probe_sound.cpp). Compared after every operation: the
DirectSound calls with their arguments (WAV bytes hashed as they are copied
into buffers), the error reports, the load/free/cue callbacks, what each call
returns (voices by which sound and slot they are, groups by identity); and
regularly a dump of the whole state (every set and sound, voices, groups and
their entries, the playing groups, the voice queue, cue tracking, fades).

Scenarios: start-up with each SoundLOD, archives on and off, the engine
variables present or not, demo sets, callbacks; loading and unloading sets;
then thousands of random operations: play, play at a position, status, stop,
frequency, voice volume, locks, frames (fades, queue, cues, groups), the
listener and 3D updates with doppler, fades, pause snapshots, group stash,
reset, the queue's stop, master volume, mute; and buffers finishing.
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import FTOL, IAT_FTOL, ROOT, Original  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

LIB = next((ROOT / 'build').glob('libmw3probe.*'))
probe = ctypes.CDLL(str(LIB))
GAME = ROOT / 'game' / 'mw3-1.2'
READER = GAME / 'zbd' / 'reader.zbd'

c_float_p = ctypes.POINTER(ctypes.c_float)
probe.probe_snd_setup.argtypes = [ctypes.c_char_p]
probe.probe_snd_begin.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_float, ctypes.c_int, ctypes.c_int,
                                  ctypes.c_uint32, ctypes.c_uint32]
probe.probe_snd_log.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_int]
probe.probe_snd_time.argtypes = [ctypes.c_float, ctypes.c_float]
probe.probe_snd_load.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
probe.probe_snd_load_set.argtypes = [ctypes.c_char_p]
probe.probe_snd_unload_set.argtypes = [ctypes.c_char_p]
probe.probe_snd_find.argtypes = [ctypes.c_char_p]
probe.probe_snd_obj.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
probe.probe_snd_voice.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
probe.probe_snd_play.argtypes = [ctypes.c_int, ctypes.c_float]
probe.probe_snd_play_at.argtypes = [ctypes.c_int, c_float_p, ctypes.c_float, c_float_p]
probe.probe_snd_play_fading.argtypes = [ctypes.c_int, c_float_p, ctypes.c_float, c_float_p, ctypes.c_float,
                                        ctypes.c_int]
probe.probe_snd_freq.argtypes = [ctypes.c_int, ctypes.c_float]
probe.probe_snd_vvol.argtypes = [ctypes.c_int, ctypes.c_float]
probe.probe_snd_fade.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_float]
probe.probe_snd_update3d.argtypes = [ctypes.c_int, c_float_p, c_float_p, ctypes.c_int]
probe.probe_snd_listener.argtypes = [c_float_p, c_float_p]
probe.probe_snd_volume.argtypes = [ctypes.c_float]
probe.probe_snd_dump.restype = ctypes.c_char_p

WINDOW = 0x1234
BIG = 0x40000000          # our heap: malloc/calloc/new, bump with roll-back
BIG_SIZE = 0x20000000
STATE = dict(initialized=0x72c2c0, open=0x72c2c4, primary=0x72c2d4, primary_fmt=0x72c2d8,
             playback=0x72c2dc, current=0x72c2e8, caps=0x72c2f0, sos=0x5efd98, inv_sos=0x5efd9c,
             listener_set=0x72c444, muted=0x72c494, volume=0x72c4a0, sets_begin=0x72c2ac, sets_end=0x72c2b0,
             groups=0x72c508, playing=0x72c50c, spare=0x72c510, finished=0x72c514, finished_count=0x72c518,
             ready=0x72c504, qclock=0x72c4bc, qnext=0x72c4c8, queued=0x72c4c4, queue=0x72c4c0,
             cue_sound=0x72c4ac, cue_voice=0x72c4b0, cue_index=0x72c4b4, cue_limit=0x72c4b8,
             fades=0x72c4dc, free_fades=0x72c4f0, search=0x72c43c, archives_on=0x5efc2c, dt=0x72c52c,
             total=0x72c538)
FN = dict(init=0x5700f0, load=0x5705e0, find=0x56f6f0, play=0x56e3d0, play_at=0x56e7e0, status=0x56e8e0,
          stop=0x56e9f0, freq=0x56f280, vvol=0x56f370, lock=0x56f3f0, unlock=0x56f410, frame=0x56dfb0,
          listener=0x571e50, update3d=0x571f30, fade=0x571d20, play_fading=0x571cc0, snapshot=0x56eb10,
          snap_stop=0x56f0a0, snap_resume=0x56f130, snap_free=0x56f190, volume=0x56f260, load_set=0x56f520,
          unload_set=0x56f530, take=0x5732f0, restore=0x573300, free_playing=0x573350, reset=0x5716a0,
          stop_queue=0x570410, cue=0x56f3e0, close=0x5715d0, shutdown=0x570510, archives=0x56f4b0)
OPS = {1: 'create', 2: 'release_device', 3: 'coop', 4: 'caps', 5: 'create_buffer', 6: 'dup', 7: 'release',
       8: 'get_format', 9: 'set_format', 10: 'status', 11: 'restore', 12: 'lock', 13: 'unlock', 14: 'play',
       15: 'stop', 16: 'setpos', 17: 'getpos', 18: 'getvol', 19: 'setvol', 20: 'pan', 21: 'getfreq',
       22: 'setfreq', 40: 'cd_open', 41: 'cd_close', 50: 'load_cb', 51: 'free_cb', 52: 'cue_cb', 60: 'zerr'}
CTRL_FREQ, CTRL_PAN, CTRL_VOL, PRIMARY = 0x20, 0x40, 0x80, 0x1
PRIMARY_FMT = (1, 2, 22050, 88200, 4, 16)
CONTROLUNAVAIL, INVALIDPARAM = 0x8878001e, 0x80070057


def fb(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def bf(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]


def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x >= 1 << 31 else x


def fnv(data, h=2166136261):
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def archive_entries(path):
    data = Path(path).read_bytes()
    version, count = struct.unpack_from('<II', data, len(data) - 8)
    table = len(data) - 8 - count * 0x94
    out = []
    for i in range(count):
        start, length = struct.unpack_from('<II', data, table + i * 0x94)
        name = data[table + i * 0x94 + 8:table + i * 0x94 + 72].split(b'\0')[0].decode('latin-1')
        out.append((name, start, length))
    return data, out


def c_format(cpu, fmt, args):
    out, i, n = '', 0, 0
    while i < len(fmt):
        if fmt[i] == '%' and i + 1 < len(fmt):
            a = args[n]
            n += 1
            if fmt[i + 1] == 's':
                out += cstr(cpu, a).decode('latin-1') if a else '(null)'
            else:
                out += str(s32(a))
            i += 2
        else:
            out += fmt[i]
            i += 1
    return out


def cstr(cpu, addr):
    out = bytearray()
    while True:
        b = cpu.read(addr + len(out), 64)
        i = b.find(0)
        if i >= 0:
            return bytes(out + b[:i])
        out += b


class Buf:
    def __init__(self, flags, size, fmt):
        self.flags, self.size, self.fmt = flags, size, fmt
        self.playing = self.looping = False
        self.pos = self.volume = self.pan = 0
        self.freq = fmt[2]


class Emu:
    """The original zSound with a DirectSound model."""

    def __init__(self, reader_data):
        cpu = self.cpu = Original()
        cpu.import_code(IAT_FTOL, FTOL)
        cpu.uc.mem_map(BIG, BIG_SIZE)
        self.next = BIG
        self.sizes = {}
        self.log = []
        self.reader_data = reader_data
        imp = cpu.stub_import
        imp(0x5bb664, lambda c: self.alloc(c.arg(0)))                       # malloc
        imp(0x5bb60c, lambda c: self.alloc(c.arg(0) * c.arg(1), zero=True))  # calloc
        imp(0x5bb610, self.realloc)
        imp(0x5bb65c, lambda c: self.free(c.arg(0)))                         # free
        imp(0x5bb4bc, lambda c: self.alloc(max(c.arg(0), 1)))               # new
        imp(0x5bb4c8, lambda c: self.free(c.arg(0)))                         # delete
        imp(0x5bb670, self.strdup)
        imp(0x5bb668, self.rand)
        imp(0x5bb580, self.stricmp)
        imp(0x5bb658, self.sprintf)
        imp(0x5bb0c4, self.read_file, pop_bytes=20)
        imp(0x5bb628, self.fopen)
        imp(0x5bb594, self.fread)
        imp(0x5bb630, lambda c: 0)                                           # fclose
        imp(0x5bb598, self.ftell)
        imp(0x5bb59c, self.fseek)
        imp(0x5bb05c, self.ds_create, pop_bytes=12)                          # DirectSoundCreate
        sf = cpu.stub_function
        sf(0x49bfd0, self.zerr)
        sf(0x556740, lambda c: self.root if cstr(c, c.ecx).lower() == b'sounds.zrd' else 0, 4)
        sf(0x572450, lambda c: self.log.append((40,)) or 1)
        sf(0x5728f0, lambda c: self.log.append((41,)) or 1)
        sf(0x576100, self.path_new)
        sf(0x576140, self.path_add)
        sf(0x576120, lambda c: 0)
        sf(0x5762b0, self.path_find)
        sf(0x576080, lambda c: 1 if (GAME / cstr(c, c.ecx).decode().replace('\\', '/')).is_file() else 0)
        sf(0x597d10, self.find_var)
        sf(0x5764e0, self.zar_init)
        sf(0x576500, lambda c: 0)
        sf(0x576520, self.zar_open, 4)
        sf(0x576650, lambda c: 1)
        sf(0x576a80, self.zar_find, 4)
        sf(0x576b30, self.zar_read, 12)
        self.callbacks()
        self.make_ds()
        # Static constructors and the list pool the exe sets up at start.
        cpu.call(0x5716d0)
        cpu.call(0x555cd0, ecx=0x200)
        cpu.fpu_control(0x7f)
        self.base = self.next
        self.strings = {}

    # --- memory
    def alloc(self, n, zero=True):
        a = self.next
        n = max(n, 1)
        self.next += (n + 15) & ~15
        if self.next > BIG + BIG_SIZE:
            raise MemoryError('emulator heap')
        if zero:  # padding too: reads past an array's end see 0 (zsnd_play.cpp, cue points)
            self.cpu.write(a, bytes((n + 15) & ~15))
        self.sizes[a] = (n + 15) & ~15
        return a

    def free(self, a):
        if a and a in self.sizes and a + self.sizes[a] == self.next:
            self.next = a
            del self.sizes[a]
        return 0

    def realloc(self, c):
        old, n = c.arg(0), c.arg(1)
        new = self.alloc(n)
        if old:
            keep = min(self.sizes.get(old, n), n)
            self.cpu.write(new, self.cpu.read(old, keep))
        return new

    def string(self, s):
        if s not in self.strings:
            b = s.encode('latin-1') + b'\0'
            p = self.alloc(len(b))
            self.cpu.write(p, b)
            self.strings[s] = p
        return self.strings[s]

    def strdup(self, c):
        s = cstr(c, c.arg(0))
        p = self.alloc(len(s) + 1)
        c.write(p, s + b'\0')
        return p

    def rand(self, c):
        self.seed = (self.seed * 214013 + 2531011) & 0xffffffff
        return (self.seed >> 16) & 0x7fff

    def stricmp(self, c):
        a, b = cstr(c, c.arg(0)).lower(), cstr(c, c.arg(1)).lower()
        return (a > b) - (a < b)

    def sprintf(self, c):
        fmt = cstr(c, c.arg(1)).decode('latin-1')
        s = c_format(c, fmt, [c.arg(2), c.arg(3), c.arg(4)])
        c.write(c.arg(0), s.encode('latin-1') + b'\0')
        return len(s)

    def zerr(self, c):
        fmt = cstr(c, c.arg(3)).decode('latin-1')
        msg = c_format(c, fmt, [c.arg(4), c.arg(5), c.arg(6)])
        self.log.append((60, s32(c.arg(0)), s32(c.arg(2))) + tuple(msg.encode('latin-1')))
        return 0

    # --- the reader (as test_zreader) and sounds.zrd
    def read_file(self, c):
        buf, n, outp = c.arg(1), c.arg(2), c.arg(3)
        chunk = self.reader_data[self.rpos:self.rpos + n]
        self.rpos += len(chunk)
        c.write(buf, chunk)
        c.u32(outp, len(chunk))
        return 1

    def read_root(self):
        # The original reader's tree of sounds.zrd (read here: a call can not
        # run inside a stub).
        for n, start, length in self.reader_entries:
            if n.lower() == 'sounds.zrd':
                self.rpos = start
                self.root = self.alloc(8)
                self.cpu.call(0x557180, ecx=0x9999, edx=self.root)
                return
        raise SystemExit('no sounds.zrd')

    # --- files: loose WAVs through fopen (the paths path_find made)
    def fopen(self, c):
        p = GAME / cstr(c, c.arg(0)).decode().replace('\\', '/')
        if not p.is_file():
            return 0
        self.files = getattr(self, 'files', {})
        h = 0x6000 + len(self.files) * 0x10
        self.files[h] = [p.read_bytes(), 0]
        return h

    def fread(self, c):
        buf, size, n, h = c.arg(0), c.arg(1), c.arg(2), c.arg(3)
        data, pos = self.files[h]
        chunk = data[pos:pos + size * n]
        self.files[h][1] = pos + len(chunk)
        c.write(buf, chunk)
        return len(chunk) // size if size else 0

    def ftell(self, c):
        return self.files[c.arg(0)][1]

    def fseek(self, c):
        h, off, whence = c.arg(0), s32(c.arg(1)), c.arg(2)
        data, pos = self.files[h]
        self.files[h][1] = off if whence == 0 else pos + off if whence == 1 else len(data) + off
        return 0

    # --- SOUND_PATH, 0x00576100 / 0x00576140 / 0x005762b0
    def path_new(self, c):
        h = self.alloc(4)
        self.paths = getattr(self, 'paths', {})
        self.paths[h] = cstr(c, c.ecx).decode()
        return h

    def path_add(self, c):
        self.paths[c.ecx] += ';' + cstr(c, c.edx).decode()
        return 0

    def path_find(self, c):
        if not c.ecx or c.ecx not in getattr(self, 'paths', {}):
            return 0
        name = cstr(c, c.edx).decode()
        base = name.replace('/', '\\').split('\\')[-1].split(':')[-1]
        for d in reversed([d for d in self.paths[c.ecx].split(';') if d]):
            d = d.rstrip('\\')
            if (GAME / (d + '\\' + base).replace('\\', '/')).is_file():
                return self.string(d + '\\' + base)
        return 0

    def find_var(self, c):
        if not self.vars:
            return 0
        return {b'SoundLOD': self.var_lod, b'MuteSound': self.var_mute,
                b'SoundVolume': self.var_volume}.get(cstr(c, c.ecx), 0)

    # --- the archives (zutl_zar, ported and tested on its own)
    def zar_init(self, c):
        self.zars = getattr(self, 'zars', {})
        self.zars[c.ecx] = None
        return c.ecx

    def zar_open(self, c):
        p = GAME / cstr(c, c.arg(0)).decode().replace('\\', '/')
        if not p.is_file():
            return 0
        if not hasattr(self, 'zcache'):
            self.zcache = {}
        if p not in self.zcache:
            self.zcache[p] = archive_entries(p)
        self.zars[c.ecx] = self.zcache[p]
        return 1

    def zar_find(self, c):
        name = cstr(c, c.arg(0)).decode('latin-1').lower()
        for i, (n, _, _) in enumerate(self.zars[c.ecx][1]):
            if n.lower() == name:
                return i
        return 0xffffffff

    def zar_read(self, c):
        data, ents = self.zars[c.ecx]
        idx, buf, sizep = s32(c.arg(0)), c.arg(1), c.arg(2)
        if idx < 0 or idx >= len(ents):
            return 0x10001
        _, start, length = ents[idx]
        have = struct.unpack('<I', c.read(sizep, 4))[0]
        c.u32(sizep, length)
        if have < length:
            return 0x10002
        if buf:
            c.write(buf, data[start:start + length])
        return 0

    # --- callbacks the test passes in
    def callbacks(self):
        cpu = self.cpu

        def name_len(obj):
            kind = struct.unpack('<i', cpu.read(obj, 4))[0]
            p = struct.unpack('<I', cpu.read(obj + (8 if kind == 0 else 4), 4))[0]
            return kind, len(cstr(cpu, p)) if p else 0

        def load_cb(c):
            kind, n = name_len(c.ecx)
            self.log.append((50, kind, n))
            self.cb_count += 1
            return 0x7000 + self.cb_count - 1

        def free_cb(c):
            kind, _ = name_len(c.ecx)
            self.log.append((51, kind, c.edx))
            return 0

        def cue_cb(c):
            p = struct.unpack('<I', cpu.read(c.edx + 8, 4))[0]
            self.log.append((52, c.ecx, len(cstr(cpu, p)) if p else 0))
            return 0
        self.load_cb, self.free_cb, self.cue_cb = cpu.code(b'\xc3'), cpu.code(b'\xc3'), cpu.code(b'\xc3')
        cpu.stub_function(self.load_cb, load_cb)
        cpu.stub_function(self.free_cb, free_cb)
        cpu.stub_function(self.cue_cb, cue_cb)

    # --- DirectSound
    def make_ds(self):
        cpu = self.cpu
        self.ds_vt, self.buf_vt = cpu.alloc(0x100), cpu.alloc(0x100)
        self.ds_obj = cpu.alloc(16)
        cpu.u32(self.ds_obj, self.ds_vt)

        def method(table, off, fn, args):
            addr = cpu.code(b'\xc3')
            cpu.u32(table + off, addr)
            cpu.stub_function(addr, fn, 4 * (args + 1))
        d = self.ds_vt
        method(d, 0x08, lambda c: self.log.append((2,)) or 0, 0)
        method(d, 0x0c, self.create_buffer, 3)
        method(d, 0x10, self.get_caps, 1)
        method(d, 0x14, self.duplicate, 2)
        method(d, 0x18, lambda c: self.log.append((3, s32(c.arg(1)), s32(c.arg(2)))) or 0, 2)
        b = self.buf_vt
        method(b, 0x08, lambda c: self.log.append((7, self.bidx(c.arg(0)))) or 0, 0)
        method(b, 0x10, self.get_pos, 2)
        method(b, 0x14, self.get_format, 3)
        method(b, 0x18, self.get_volume, 1)
        method(b, 0x20, self.get_freq, 1)
        method(b, 0x24, self.get_status, 1)
        method(b, 0x2c, self.lock, 7)
        method(b, 0x30, self.play, 3)
        method(b, 0x34, self.set_pos, 1)
        method(b, 0x38, self.set_format, 1)
        method(b, 0x3c, self.set_volume, 1)
        method(b, 0x40, self.set_pan, 1)
        method(b, 0x44, self.set_freq, 1)
        method(b, 0x48, self.stop, 0)
        method(b, 0x4c, self.unlock, 4)
        method(b, 0x50, lambda c: self.log.append((11, self.bidx(c.arg(0)))) or 0, 0)

    def ds_create(self, c):
        self.log.append((1,))
        c.u32(c.arg(1), self.ds_obj)
        return 0

    def bidx(self, obj):
        return struct.unpack('<I', self.cpu.read(obj + 4, 4))[0] if obj else 0

    def new_buf(self, buf):
        self.bufs.append(buf)
        obj = self.alloc(8)
        self.cpu.u32(obj, self.buf_vt)
        self.cpu.u32(obj + 4, len(self.bufs) - 1)
        return obj

    def get_caps(self, c):
        self.log.append((4,))
        c.u32(c.arg(1) + 4, self.caps)
        return 0

    def create_buffer(self, c):
        desc, outp = c.arg(1), c.arg(2)
        _, flags, size, _, pfmt = struct.unpack('<IIIII', c.read(desc, 20))
        f = struct.unpack('<HHIIHH', c.read(pfmt, 16)) if pfmt else (0, 0, 0, 0, 0, 0)
        obj = self.new_buf(Buf(flags | self.extra, size, PRIMARY_FMT if flags & PRIMARY else f))
        c.u32(outp, obj)
        self.log.append((5, len(self.bufs) - 1, flags, size, 1 if pfmt else 0) + f)
        return 0

    def duplicate(self, c):
        src = self.bidx(c.arg(1))
        o = self.bufs[src]
        b = Buf(o.flags, o.size, o.fmt)
        b.volume, b.pan, b.freq = o.volume, o.pan, o.freq
        obj = self.new_buf(b)
        c.u32(c.arg(2), obj)
        self.log.append((6, src, len(self.bufs) - 1))
        return 0

    def get_format(self, c):
        b, fmt, size, written = self.bidx(c.arg(0)), c.arg(1), c.arg(2), c.arg(3)
        if not fmt:
            c.u32(written, 0x12)
            return 0
        self.log.append((8, b))
        c.write(fmt, struct.pack('<HHIIHHH', *self.bufs[b].fmt, 0))
        return 0

    def set_format(self, c):
        b = self.bidx(c.arg(0))
        self.log.append((9, b) + struct.unpack('<HHIIHH', c.read(c.arg(1), 16)))
        return 0

    def get_status(self, c):
        b = self.bidx(c.arg(0))
        x = self.bufs[b]
        s = (1 if x.playing else 0) | (4 if x.playing and x.looping else 0)
        c.u32(c.arg(1), s)
        self.log.append((10, b, s))
        return 0

    def lock(self, c):
        b, off, n = self.bidx(c.arg(0)), c.arg(1), c.arg(2)
        self.log.append((12, b, s32(off), s32(n)))
        x = self.bufs[b]
        if off > x.size or n > x.size:
            return INVALIDPARAM
        first = min(n, x.size - off)
        if first + 16 > getattr(self, 'scratch_size', 0):
            self.scratch_size = first + 0x10000
            self.scratch = self.alloc(self.scratch_size, zero=False)
        c.u32(c.arg(3), self.scratch)
        c.u32(c.arg(4), first)
        c.u32(c.arg(5), self.scratch if n > first else 0)
        c.u32(c.arg(6), n - first)
        return 0

    def unlock(self, c):
        b, p1, n1, p2, n2 = self.bidx(c.arg(0)), c.arg(1), c.arg(2), c.arg(3), c.arg(4)
        h = fnv(c.read(p1, n1)) if n1 else 2166136261
        if p2:
            h = fnv(c.read(p2, n2), h)
        self.log.append((13, b, s32(h), s32(n1), s32(n2)))
        return 0

    def play(self, c):
        b, flags = self.bidx(c.arg(0)), c.arg(3)
        self.log.append((14, b, s32(flags)))
        self.bufs[b].playing, self.bufs[b].looping = True, bool(flags & 1)
        return 0

    def stop(self, c):
        b = self.bidx(c.arg(0))
        self.log.append((15, b))
        self.bufs[b].playing = False
        return 0

    def set_pos(self, c):
        b = self.bidx(c.arg(0))
        self.log.append((16, b, s32(c.arg(1))))
        self.bufs[b].pos = c.arg(1)
        return 0

    def get_pos(self, c):
        b = self.bidx(c.arg(0))
        self.log.append((17, b))
        if c.arg(1):
            c.u32(c.arg(1), self.bufs[b].pos)
        if c.arg(2):
            c.u32(c.arg(2), self.bufs[b].pos)
        return 0

    def get_volume(self, c):
        b = self.bidx(c.arg(0))
        self.log.append((18, b))
        c.u32(c.arg(1), self.bufs[b].volume)
        return 0

    def set_volume(self, c):
        b = self.bidx(c.arg(0))
        self.log.append((19, b, s32(c.arg(1))))
        if not self.bufs[b].flags & CTRL_VOL:
            return CONTROLUNAVAIL
        self.bufs[b].volume = s32(c.arg(1))
        return 0

    def set_pan(self, c):
        b = self.bidx(c.arg(0))
        can = bool(self.bufs[b].flags & CTRL_PAN)
        self.log.append((20, b, s32(c.arg(1)), 1 if can else 0))
        if not can:
            return CONTROLUNAVAIL
        self.bufs[b].pan = s32(c.arg(1))
        return 0

    def get_freq(self, c):
        b = self.bidx(c.arg(0))
        self.log.append((21, b))
        if not self.bufs[b].flags & CTRL_FREQ:
            return CONTROLUNAVAIL
        c.u32(c.arg(1), self.bufs[b].freq)
        return 0

    def set_freq(self, c):
        b = self.bidx(c.arg(0))
        can = bool(self.bufs[b].flags & CTRL_FREQ)
        self.log.append((22, b, s32(c.arg(1)) if can else -1))
        if not can:
            return CONTROLUNAVAIL
        self.bufs[b].freq = c.arg(1)
        return 0

    # --- a scenario
    def begin(self, lod, mute, volume, vars_, archives_on, caps, seed):
        cpu = self.cpu
        self.reader_entries = list(archive_entries_cached(READER))
        self.next = self.base
        self.sizes = {}
        self.strings = {}
        # The list pool's nodes came from the heap just rolled back: a new pool.
        for a in (0x72be80, 0x72be84, 0x72be88, 0x72be8c):
            cpu.u32(a, 0)
        cpu.call(0x555cd0, ecx=0x200)
        for a, n in ((0x72c2a8, 0x2c0 - 0x2a8 + 0x1000),):
            pass
        # zSound's globals 0x0072c290..0x0072c540, but not the static lists.
        keep = {a: cpu.read(a, 4) for a in (0x72c4dc, 0x72c4ec)}
        cpu.write(0x72c290, bytes(0x72c540 - 0x72c290))
        for a, v in keep.items():
            cpu.write(a, v)
        for lst in (0x72c4dc, 0x72c4ec):  # empty them (sentinel to itself)
            s = struct.unpack('<I', cpu.read(lst, 4))[0]
            cpu.u32(s, s)
            cpu.u32(s + 4, s)
        cpu.u32(0x72c4f0, 0)
        cpu.u32(0x72c4e0, 0)
        cpu.u32(0x72c2cc, 0)
        cpu.u32(STATE['archives_on'], archives_on)
        cpu.u32(STATE['sos'], fb(343.0))
        cpu.u32(STATE['inv_sos'], 0x3b3f0f6c if False else struct.unpack('<I', struct.pack('<f', 1.0 / 343.0))[0])
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
        self.log = []
        self.gids, self.next_gid = {}, 1
        self.read_root()
        self.log = []

    def call(self, name, ecx=0, edx=0, args=()):
        self.cpu.call(FN[name], ecx=ecx, edx=edx, stack_args=args)
        return self.cpu.uc.reg_read(UC_X86_REG_EAX)

    def u32(self, a):
        return struct.unpack('<I', self.cpu.read(a, 4))[0]

    def i32(self, a):
        return s32(self.u32(a))

    def i16(self, a):
        return struct.unpack('<h', self.cpu.read(a, 2))[0]

    def vec(self, v):
        if v is None:
            return 0
        p = self.alloc(12)
        self.cpu.write(p, struct.pack('<3f', *v))
        return p

    # --- descriptions, as the probe prints them
    def sets(self):
        b, e = self.u32(STATE['sets_begin']), self.u32(STATE['sets_end'])
        return [self.u32(a) for a in range(b, e, 4)] if b else []

    def voice_desc(self, v):
        if not v:
            return '-'
        if self.i32(v + 4) == 1:
            if v not in self.gids:
                self.gids[v] = self.next_gid
                self.next_gid += 1
            return f'g{self.gids[v]}'
        for i, s in enumerate(self.sets()):
            n, base = self.i32(s + 4), self.u32(s + 8)
            for j in range(n):
                snd = base + 0xd0 * j
                if v == snd + 0x4c:
                    return f's{i}.{j}.0'
                dn, arr = self.i32(snd + 0x94), self.u32(snd + 0x98)
                for k in range(dn):
                    if self.u32(arr + 4 * k) == v:
                        return f's{i}.{j}.{k + 1}'
        return '?'

    def name_of(self, obj):
        if not obj:
            return None
        kind = self.i32(obj)
        p = self.u32(obj + (8 if kind == 0 else 4))
        return cstr(self.cpu, p).decode('latin-1') if p else ''

    def obj_desc(self, obj):
        if not obj:
            return '-'
        return f'{self.i32(obj)}:{self.name_of(obj)}'

    def list_items(self, lst):
        if not lst or self.u32(lst) == 0:
            return []
        head = self.u32(lst + 0x10)
        out, n = [], head
        while True:
            out.append(self.u32(n))
            n = self.u32(n + 4)
            if n == head:
                return out

    def std_list(self, sentinel):
        out, n = [], self.u32(sentinel)
        while n != sentinel:
            out.append(self.u32(n + 8))
            n = self.u32(n)
        return out

    def voice_txt(self, a):
        u = self.u32
        return (f' [b{self.bidx(u(a + 8))} k{self.i32(a + 4)} l{self.cpu.read(a, 1)[0]} v{self.i32(a + 0x24)} '
                f'p{self.i32(a + 0x28)} ' + ' '.join(f'{u(a + o):08x}' for o in range(0xc, 0x24, 4)) + ']')

    def entry_txt(self, e):
        out = ''
        while e:
            nm = self.u32(e + 0x10)
            out += (f' ({self.i16(e)} {self.i16(e + 2)} {self.u32(e + 4):08x} {self.u32(e + 8):08x} '
                    f'{self.i32(e + 0xc)} {cstr(self.cpu, nm).decode("latin-1") if nm else "-"} '
                    f'{self.obj_desc(self.u32(e + 0x14))})')
            e = self.u32(e + 0x18)
        return out

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
        search = self.paths.get(sp, '-') if sp and hasattr(self, 'paths') else '-'
        out += (f' sos{u(STATE["sos"]):08x} {u(STATE["inv_sos"]):08x} lis{i32(STATE["listener_set"])} '
                f'search {search} muted {i32(STATE["muted"])} vol{u(u(STATE["volume"])):08x}\n')
        for i, s in enumerate(self.sets()):
            n, base = i32(s + 4), u(s + 8)
            subs = [u(u(s + 0x14) + 4 * k) for k in range(i32(s + 0x10))]
            out += f'set {i} {cs(u(s))} {i32(s + 0xc)} {max(n, 0)} subs'
            for sb in subs:
                out += f' {cs(u(sb)) if sb else "-"}'
            out += '\n'
            for j in range(max(n, 0)):
                a = base + 0xd0 * j
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
        return out + '\n'

    def gv_txt(self, g):
        u, i32 = self.u32, self.i32
        grp = u(g + 0x44)
        e, ed = u(g + 0x30), '-'
        if e:
            ed = '?'
            for k in range(i32(grp + 0x20)):
                x, d = u(grp + 0x24) + 0x1c * k, 0
                while x:
                    if x == e:
                        ed = f'{k}.{d}'
                    x, d = u(x + 0x18), d + 1
        out = (f'  {self.voice_desc(g)} {cstr(self.cpu, u(grp + 4)).decode("latin-1")} st{i32(g + 0x40)} '
               f't{u(g + 0x28):08x} r{i32(g + 0x2c)} e{ed} w{i32(g + 0x34)} v{u(g + 0x24):08x} p{i32(g + 8)} '
               f'{u(g + 0xc):08x} {u(g + 0x10):08x} {u(g + 0x14):08x} l{self.cpu.read(g, 1)[0]}:')
        for k in range(i32(g + 0x38)):
            out += f' {self.voice_desc(u(u(g + 0x3c) + 4 * k))}'
        return out + '\n'


_entries_cache = {}


def archive_entries_cached(path):
    if path not in _entries_cache:
        _entries_cache[path] = archive_entries(path)[1]
    return _entries_cache[path]


def port_log():
    buf = (ctypes.c_int32 * (1 << 20))()
    n = probe.probe_snd_log(buf, len(buf))
    if n < 0:
        raise SystemExit('port log overflow')
    out, i = [], 0
    while i < n:
        op, cnt = buf[i], buf[i + 1]
        out.append((op,) + tuple(buf[i + 2:i + 2 + cnt]))
        i += 2 + cnt
    return out


def fmt_entry(e):
    if e[0] == 60:
        return f'zerr({e[1]}, {e[2]:#x}, {bytes(e[3:]).decode("latin-1")!r})'
    return f'{OPS.get(e[0], e[0])}{e[1:]}'


class Mismatch(Exception):
    pass


COVERAGE = {}
TRACE = bool(__import__("os").environ.get("SND_TRACE"))
DUMP_EVERY = int(__import__("os").environ.get("SND_DUMP_EVERY", "50"))


def compare_logs(what, emu):
    got, want = port_log(), emu.log
    emu.log = []
    for e in want:
        key = OPS.get(e[0], e[0])
        if e[0] == 22 and e[2] == -1:
            key = 'setfreq (no control)'
        if e[0] == 20 and e[3] == 0:
            key = 'pan (no control)'
        COVERAGE[key] = COVERAGE.get(key, 0) + 1
    if got != want:
        lines = [f'{what}: calls differ']
        n = next((k for k in range(min(len(got), len(want))) if got[k] != want[k]), min(len(got), len(want)))
        for k in range(max(0, n - 12), min(max(len(got), len(want)), n + 6)):
            a = fmt_entry(want[k]) if k < len(want) else '-'
            b = fmt_entry(got[k]) if k < len(got) else '-'
            lines.append(f'  {k}: original {a}   port {b}' + ('   <--' if k == n else ''))
        raise Mismatch('\n'.join(lines))
    return len(got)


def compare_dumps(what, emu):
    a, b = emu.dump(), probe.probe_snd_dump().decode('latin-1')
    if a != b:
        al, bl = a.splitlines(), b.splitlines()
        k = next((i for i in range(min(len(al), len(bl))) if al[i] != bl[i]), min(len(al), len(bl)))
        raise Mismatch(f'{what}: state differs at line {k}\n  original {al[k] if k < len(al) else "-"}\n'
                       f'  port     {bl[k] if k < len(bl) else "-"}')


class Both:
    """One scenario on both sides, op by op."""

    def __init__(self, emu, lod=0, mute=0, volume=1.0, vars_=1, archives_on=1, caps=0x8, seed=1):
        self.emu = emu
        emu.begin(lod, mute, volume, vars_, archives_on, caps, seed)
        probe.probe_snd_begin(lod, mute, volume, vars_, archives_on, caps, seed)
        self.objs = [0]      # emulator addresses
        self.voices = [0]
        self.snaps = [0]
        self.lists = [0]
        self.calls = 0
        self.dt, self.total = 0.0, 0.0
        self.dead = set()  # voices whose memory was freed: neither side may touch them
        self.time(0.0)

    def check(self, what, a, b):
        self.calls += compare_logs(what, self.emu)
        if a != b:
            raise Mismatch(f'{what}: original returned {a}, port {b}')

    def time(self, dt):
        self.total = bf(fb(self.total + dt))
        self.dt = dt
        self.emu.cpu.u32(STATE['dt'], fb(dt))
        self.emu.cpu.u32(STATE['total'], fb(self.total))
        probe.probe_snd_time(dt, self.total)

    def init(self):
        a = self.emu.call('init', ecx=WINDOW)
        self.check('init', a, probe.probe_snd_init(WINDOW))

    def load(self, cb=1, demo=0):
        e = self.emu
        a = e.call('load', ecx=WINDOW, edx=e.string('sounds.zrd'), args=(e.load_cb if cb else 0, demo))
        self.check('load', a, probe.probe_snd_load(WINDOW, b'sounds.zrd', cb, demo))

    def load_set(self, name, unload=False):
        e = self.emu
        if unload:  # its sounds' (and subsets') duplicate voices are freed
            todo = [x for x in e.sets() if cstr(e.cpu, e.u32(x)).decode('latin-1') == name][:1]
            freed = set()
            while todo:
                x = todo.pop()
                if x in freed or not e.i32(x + 0xc):
                    continue
                freed.add(x)
                todo += [e.u32(e.u32(x + 0x14) + 4 * k) for k in range(e.i32(x + 0x10))]
                todo = [t for t in todo if t]
            for k, a in enumerate(self.voices):
                if a and e.i32(a + 4) == 0 and not e.voice_desc(a).endswith('.0'):
                    owner = e.u32(a + 0x2c)
                    if any(e.u32(x + 8) <= owner < e.u32(x + 8) + 0xd0 * e.i32(x + 4) for x in freed):
                        self.dead.add(k)
        e.call('unload_set' if unload else 'load_set', ecx=e.string(name))
        (probe.probe_snd_unload_set if unload else probe.probe_snd_load_set)(name.encode('latin-1'))
        self.check(('unload ' if unload else 'load ') + name, 0, 0)

    def find(self, name):
        e = self.emu
        a = e.call('find', ecx=e.string(name))
        b = probe.probe_snd_find(name.encode('latin-1'))
        da = e.obj_desc(a)
        buf = ctypes.create_string_buffer(256)
        if b:
            probe.probe_snd_obj(b, buf, 256)
        db = buf.value.decode('latin-1') if b else '-'
        self.check(f'find {name}', da, db)
        if not a:
            return 0
        self.objs.append(a)
        assert len(self.objs) - 1 == b
        return b

    def voice_result(self, what, a, b):
        e = self.emu
        buf = ctypes.create_string_buffer(256)
        if b:
            probe.probe_snd_voice(b, buf, 256)
        self.check(what, e.voice_desc(a), buf.value.decode() if b else '-')
        if not a:
            return 0
        self.voices.append(a)
        assert len(self.voices) - 1 == b, (len(self.voices), b)
        return b

    def play(self, o, vol):
        a = self.emu.call('play', ecx=self.objs[o], args=(fb(vol),))
        return self.voice_result(f'play {o} {vol}', a, probe.probe_snd_play(o, vol))

    def play_at(self, o, pos, vol, vel):
        e = self.emu
        a = e.call('play_at', ecx=self.objs[o], edx=e.vec(pos), args=(fb(vol), e.vec(vel)))
        b = probe.probe_snd_play_at(o, fvec(pos), vol, fvec(vel))
        return self.voice_result(f'play_at {o} {pos} {vol} {vel}', a, b)

    def play_fading(self, o, pos, vol, vel, to, stop):
        e = self.emu
        a = e.call('play_fading', ecx=self.objs[o], edx=e.vec(pos), args=(fb(vol), e.vec(vel), fb(to), stop))
        b = probe.probe_snd_play_fading(o, fvec(pos), vol, fvec(vel), to, stop)
        return self.voice_result(f'play_fading {o} {vol} {to} {stop}', a, b)

    def voice_op(self, name, v, args, port):
        a = s32(self.emu.call(name, ecx=self.voices[v], args=args))
        self.check(f'{name} v{v} ({self.emu.voice_desc(self.voices[v])})', a, port)

    def frame(self, a3d, adv):
        self.emu.call('frame', ecx=a3d, edx=adv)
        probe.probe_snd_frame(a3d, adv)
        self.check('frame', 0, 0)

    def listener(self, m, vel):
        e = self.emu
        pm = 0
        if m is not None:
            pm = e.alloc(48)
            e.cpu.write(pm, struct.pack('<12f', *m))
        a = e.call('listener', ecx=pm, edx=e.vec(vel))
        b = probe.probe_snd_listener((ctypes.c_float * 12)(*m) if m is not None else None, fvec(vel))
        self.check('listener', a, b)

    def update3d(self, v, pos, vel, doppler):
        e = self.emu
        a = s32(e.call('update3d', ecx=self.voices[v], edx=e.vec(pos), args=(e.vec(vel), doppler)))
        self.check(f'update3d v{v} {pos} {vel} {doppler}', a, probe.probe_snd_update3d(v, fvec(pos), fvec(vel), doppler))

    def fade(self, v, stop, level):
        a = self.emu.call('fade', ecx=self.voices[v], edx=stop, args=(fb(level),))
        return self.voice_result(f'fade v{v} {stop} {level}', a, probe.probe_snd_fade(v, stop, level))

    def volume(self, v):
        self.emu.call('volume', args=(fb(v),))
        probe.probe_snd_volume(v)
        self.check(f'volume {v}', 0, 0)

    def snapshot(self):
        a = self.emu.call('snapshot')
        b = probe.probe_snd_snapshot()
        self.check('snapshot', 0, 0)
        self.snaps.append(a)
        return b

    def snap_op(self, s, op):
        a = self.emu.call(('snap_stop', 'snap_resume', 'snap_free')[op], ecx=self.snaps[s])
        b = probe.probe_snd_snap_op(s, op)
        self.check(f'snapshot op {op}', a, b)
        if op == 2:
            self.snaps[s] = 0

    def take(self):
        a = self.emu.call('take')
        b = probe.probe_snd_take_groups()
        self.check('take groups', 0, 0)
        self.lists.append(a)
        return b

    def groups_op(self, l, op):
        if op == 1:
            for g in self.emu.list_items(self.lists[l]):
                self.emu.gids.pop(g, None)
                self.dead.update(k for k, a in enumerate(self.voices) if a == g)
        a = self.emu.call(('restore', 'free_playing')[op], ecx=self.lists[l])
        self.lists[l] = 0
        self.check(f'groups op {op}', a, probe.probe_snd_groups_op(l, op))

    def simple(self, name, ecx, port):
        self.emu.call(name, ecx=ecx)
        port()
        self.check(name, 0, 0)

    def cue(self, o):
        self.emu.call('cue', ecx=self.objs[o], edx=self.emu.cue_cb)
        probe.probe_snd_cue(o)
        self.check('cue', 0, 0)

    def finish(self, b):
        x = self.emu.bufs[b]
        if x.playing and not x.looping:
            x.playing, x.pos = False, 0
        probe.probe_snd_finish(b)

    def close(self, cb):
        a = self.emu.call('close', ecx=self.emu.free_cb if cb else 0)
        self.check('close', a, probe.probe_snd_close(cb))

    def shutdown(self):
        a = self.emu.call('shutdown')
        self.check('shutdown', a, probe.probe_snd_shutdown())

    def compare(self, what):
        compare_dumps(what, self.emu)


def fvec(v):
    return (ctypes.c_float * 3)(*v) if v is not None else None


def set_names():
    """The SETS of sounds.zrd, in order, from the reader data."""
    text = subprocess_zrd()
    return text


def subprocess_zrd():
    import subprocess
    out = subprocess.run([sys.executable, str(ROOT / 'tools' / 'analysis' / 'zrd.py'), str(READER), 'sounds.zrd'],
                         capture_output=True, text=True).stdout
    return out


def sounds_zrd_names():
    """Set names, sound names and group names from a text dump of sounds.zrd."""
    lines = subprocess_zrd().splitlines()
    sets, sounds, groups = [], [], []
    mode = None
    for k, l in enumerate(lines):
        if l == '    "SETS"':
            mode = 'sets'
        elif l == '    "SOUND_GROUPS"':
            mode = 'groups'
        elif l.startswith('    "') and l.strip('" ') not in ('SETS', 'SOUND_GROUPS'):
            mode = None
        if mode == 'sets' and l.startswith('      "') and l.endswith('"'):
            sets.append(l.strip().strip('"'))
        if mode == 'sets' and l.startswith('        ("'):
            sounds.append(l.strip()[2:].split('"')[0])
        if mode == 'sets' and l == '        (' and k + 1 < len(lines):
            sounds.append(lines[k + 1].strip().strip('"'))
        if mode == 'groups' and l == '      (' and k + 1 < len(lines):
            groups.append(lines[k + 1].strip().strip('"'))
    return sets, sounds, groups


def scenario_load(emu, names, lod, archives_on, vars_, cb, demo, sets, caps=0x8):
    b = Both(emu, lod=lod, vars_=vars_, archives_on=archives_on, caps=caps)
    b.init()
    b.load(cb, demo)
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


MATS = [(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0), (0, 0, -1, 0, 1, 0, 1, 0, 0, 120.0, 3.5, -40.0)]


def rpos(r):
    return (r.uniform(-500, 500), r.uniform(-50, 50), r.uniform(-500, 500))


def scenario_play(emu, names, seed, steps, sets, mute=0, lod=2, extra=0):
    r = random.Random(seed)
    b = Both(emu, lod=lod, mute=mute, volume=r.choice([1.0, 0.5, 0.8]), seed=seed)
    # extra: controls every buffer gets beyond what the game asked for, so the
    # frequency paths (doppler, set_frequency) run to the end as on a sound
    # with FREQUENCY, which no shipped sound has.
    emu.extra = extra
    probe.probe_snd_extra_controls(extra)
    b.init()
    b.load(1, 0)
    for s in sets:
        b.load_set(s)
    _, _, groups = names
    loaded = []
    for x in emu.sets():
        for j in range(emu.i32(x + 4)):
            a = emu.u32(x + 8) + 0xd0 * j
            if emu.u32(a + 0xc) & 8:
                loaded.append((cstr(emu.cpu, emu.u32(a + 8)).decode('latin-1'), bool(emu.u32(a + 0xc) & 4)))
    three_d = [n for n, d in loaded if d]
    objs = []
    for n in (r.sample([n for n, _ in loaded], min(70, len(loaded))) + three_d[:30] + r.sample(groups, 40) +
              ['no_such_sound']):
        o = b.find(n)
        if o:
            objs.append(o)
    cue_objs = [b.find(n) for n in ('briefing_c1m1', 'briefing_train1')]
    for o in cue_objs:
        if o:
            b.cue(o)
            objs.append(o)
    b.compare('ready')
    snaps, lists = [], []
    for step in range(steps):
        k = r.random()
        if TRACE:
            print("step", step, round(k, 4), len(b.voices))
        live = [v for v in range(1, len(b.voices)) if v not in b.dead]
        if k < 0.18:
            b.play(r.choice(objs), r.choice([1.0, 0.7, 0.3, 0.0005, 1.2]))
        elif k < 0.33:
            b.play_at(r.choice(objs), rpos(r), r.choice([1.0, 0.6]), r.choice([None, rpos(r)]))
        elif k < 0.36:
            b.play_fading(r.choice(objs), r.choice([None, rpos(r)]), r.choice([1.0, 0.2]), None,
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
            m = list(r.choice(MATS))
            m[9:12] = rpos(r)
            b.listener(r.choice([m, None, m]), r.choice([None, rpos(r), (0, 0, 0)]))
        elif k < 0.78 and live:
            v = r.choice(live)
            if emu.i32(b.voices[v] + 4) == 0:
                b.update3d(v, r.choice([None, rpos(r)]), r.choice([None, rpos(r)]), r.randint(0, 1))
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
            l = lists.pop(r.randrange(len(lists)))
            b.groups_op(l, 0)  # freeing (quitting) leaves voices in other groups dangling
        elif k < 0.91:
            b.volume(r.choice([1.0, 0.5, 0.9]))
        elif k < 0.92:
            b.simple('reset', 0, probe.probe_snd_reset)
        elif k < 0.93:
            f = r.randint(0, 1)
            b.simple('stop_queue', f, lambda f=f: probe.probe_snd_stop_queue(f))
        elif k < 0.935 and not snaps and not emu.std_list(emu.u32(STATE["fades"])):
            # Unloading frees duplicate voices: a fade or a pause record still on one would
            # be a use after free in both (docs/sound.md).
            s = r.choice(sets)
            b.load_set(s, unload=True)
            b.load_set(s)
        elif k < 0.96:
            # A burst of one sound (all eight voices, then the own voice taken back) or of
            # one group (its weighted picks).
            o = r.choice(objs)
            for _ in range(r.randint(4, 14)):
                b.play(o, 1.0)
        if step % DUMP_EVERY == 0:
            b.compare(f'step {step}')
    b.compare('end')
    for l in lists:
        b.groups_op(l, 1)
    b.close(1)
    b.shutdown()
    return b.calls


def scenario_picks(emu, names, plays):
    """Every group started `plays` times: its weighted pick (MSVCRT rand) and,
    for DYNAMIC_WEIGHTS, the weights it leaves."""
    b = Both(emu, lod=2, seed=7)
    b.init()
    b.load(0, 0)
    for g in names[2]:
        o = b.find(g)
        if not o:
            continue
        for k in range(plays):
            b.play(o, 1.0)
            if k % 10 == 9:
                b.time(0.05)
                b.frame(0, 1)
        b.compare(f'group {g}')
    b.close(0)
    b.shutdown()
    return b.calls


def main():
    quick = '--quick' in sys.argv
    seed0 = int(sys.argv[sys.argv.index('--seed') + 1]) if '--seed' in sys.argv else 1
    if not READER.exists():
        raise SystemExit(f'needs {READER}')
    if not probe.probe_snd_setup(str(GAME).encode()):
        raise SystemExit('probe could not open reader.zbd')
    emu = Emu(READER.read_bytes())
    names = sounds_zrd_names()
    sets = names[0]
    calls = runs = 0
    try:
        # Start-up and loading, every set.
        for lod, arch, vars_, cb, demo in ([(0, 1, 1, 1, 0), (2, 1, 1, 0, 0)] if quick else
                                           [(0, 1, 1, 1, 0), (1, 1, 1, 0, 0), (2, 1, 1, 1, 0), (0, 0, 1, 1, 0),
                                            (2, 1, 0, 1, 0), (0, 1, 1, 1, 1)]):
            calls += scenario_load(emu, names, lod, arch, vars_, cb, demo, sets if not quick or lod == 2 else
                                   sets[:20])
            runs += 1
        calls += scenario_load(emu, names, 2, 1, 1, 1, 0, sets[:10], caps=0)
        runs += 1
        calls += scenario_picks(emu, names, 40 if quick else 200)
        runs += 1
        play_sets = ['MENU', 'mech', 'common', 'dom', 'c1', 'c1m4', 't1', 'brief_c11', 'brief_t11', 'choppers']
        for k in range(3 if quick else 12):
            calls += scenario_play(emu, names, seed0 + k, 300 if quick else 1500, play_sets,
                                   mute=1 if k % 5 == 4 else 0, lod=(2, 0)[k % 2], extra=CTRL_FREQ | CTRL_PAN if k % 3 == 2 else 0)
            runs += 1
    except Mismatch as m:
        raise SystemExit(f'FAIL ({runs} runs passed)\n{m}')
    print('coverage:', ', '.join(f'{k} {v}' for k, v in sorted(COVERAGE.items(), key=lambda kv: str(kv[0]))))
    print(f'ok: {runs} runs, {calls} DirectSound calls and callbacks identical, states identical '
          f'({len(sets)} sets, sounds.zrd and the sound archives)')


if __name__ == '__main__':
    main()
