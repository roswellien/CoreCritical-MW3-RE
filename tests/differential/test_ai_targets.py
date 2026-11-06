"""Targets and targeting against the original x86 (docs/ai.md section 14).

Part 1, AiTarget (the vehicle's targetable part): the pick 0x4d6b90, the
side tests 0x4d6190, set_target 0x4d5e40, the six target keys 0x4d5ee0,
0x4d6260, 0x4d64b0, 0x4d6710, 0x4d6970, 0x4d6a80, the ranges 0x4d6d00 and
0x4d6d20, 0x4d7220 and 0x4d7240, on random contact lists (in sight or not,
of every side and of several teams at once, duplicates, the current target
in the list, out of it or none) through every side value.

Part 2, CZAiTarget (ZAiTarget.cpp): every mission's targets.zrd records as
the port's reader reads them (0x4d2e20; the reader itself is
test_ai_readers.py's), and made-up ones, on made-up nodes (one or several
of a record's name, with and without a box) and destroy animations (found,
not found, free, on another node, with the node's damage handlers missing,
the animation's own, inherited, foreign or another animation's): the HUD
markers, the descriptions (0x4d2480, 0x4d25c0, 0x4d24f0 with the sort
0x4d21f0), the constructor 0x4d2960 with its attach 0x4d3390 (0x4d27d0,
0x4d26f0, translate_to_anim_root), then hits 0x4d26b0, collisions 0x4d26d0
and destruction 0x4d2b30 / 0x4d2b60 firing the health_below triggers, and
the end: every target deleted 0x4d8360 (0x4d2aa0, 0x4d81e0, 0x4d58a0) and
the markers 0x4d3030. Traps 0x4d31d0 by name prefix (none ship) likewise.

Both sides: every call out stubbed and answered from one script; the call
traces and the objects' fields after every step must be equal. In play:
the x87 at 24 bits.

    cmake --build build && .venv/bin/python tests/differential/test_ai_targets.py [cases]
"""
import ctypes
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from calltrace import Emu, Trace, compare, load_probe  # noqa: E402
from ai_world import ZBD  # noqa: E402
import zrd  # noqa: E402

P = ctypes.c_void_p
U32 = ctypes.c_uint32
PILOT = 0x60861c
SORTED = 0x60b56c
LIST, MARKERS = 0x60b580, 0x60b4f0
KEYS = [0x4d5ee0, 0x4d6260, 0x4d64b0, 0x4d6710, 0x4d6970, 0x4d6a80]


def die():
    sys.stdout.flush()
    os._exit(1)


def f2u(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


def u2f(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]


def setup_probe(probe):
    probe.probe_aitarget_set.argtypes = [P, ctypes.c_int32, P]
    probe.probe_aitarget_call.argtypes = [ctypes.c_int32, ctypes.c_int32, U32, U32]
    probe.probe_aitarget_call.restype = U32
    probe.probe_aitarget_state.argtypes = [P]
    probe.probe_aitarget_sets.argtypes = [P, ctypes.c_int32]
    probe.probe_aitarget_sets.restype = ctypes.c_int32
    probe.probe_targets_poke.argtypes = [U32, U32]
    probe.probe_targets_add_record.argtypes = [ctypes.c_int32, P, P, P, P, ctypes.c_int32]
    probe.probe_targets_handles.argtypes = [P, ctypes.c_int32]
    probe.probe_targets_create.argtypes = [ctypes.c_int32]
    probe.probe_targets_op.argtypes = [ctypes.c_int32, U32, U32, U32, U32]
    probe.probe_targets_op.restype = U32
    probe.probe_targets_state.argtypes = [P, ctypes.c_int32]
    probe.probe_targets_state.restype = ctypes.c_int32


# --- part 1: AiTarget --------------------------------------------------------------------
def team_mask(r):
    m = 1 << r.randrange(4)
    if r.random() < 0.2:
        m |= 1 << r.randrange(4)
    return m


def make_pick_case(seed):
    r = random.Random(seed)
    n = r.choice([0, 1, 2, 3, 5, 8, 12])
    my = team_mask(r)
    allies = my & ~1
    case = {'n': n, 'team': my, 'allies': allies, 'enemies': ~allies & 0xfffffffe,
            'teams': [team_mask(r) if r.random() < 0.9 else r.choice([0, 0xffffffff]) for _ in range(n + 1)],
            'flags': [r.choice([0, 1, 1, 1, 3]) for _ in range(n + 1)],
            'entities': [r.randrange(3) if r.random() < 0.3 else 100 + i for i in range(n + 1)],
            'owners': [r.randrange(2) for _ in range(n + 1)]}
    if r.random() < 0.15:
        case['allies'], case['enemies'] = r.randrange(1 << 5), r.randrange(1 << 5)
    picks = list(range(-1, n)) + [n]  # -1 none, n: a contact not in the list
    case['fields'] = [r.choice(picks) for _ in range(4)]
    if r.random() < 0.5:  # the last ally and enemy picks are the current one or none
        cur = case['fields'][0]
        case['fields'][1] = r.choice([cur, -1])
        case['fields'][2] = r.choice([cur, -1])
    fn = r.randrange(13)
    case['fn'] = fn
    case['side'] = r.choice([0, 0, 1, 1, 2, 3, -1, 4])
    if fn == 7:
        case['arg'] = r.choice(list(range(-1, n)))
        case['side'] = r.randrange(2)  # keep_sides
    elif fn in (8, 9):
        case['arg'] = f2u(r.choice([0.0, 50.0, 500.0, 999.9, 1000.0, 1000.5, 2000.0, 60000.0, -5.0, float('nan')]))
    elif fn == 12:
        case['arg'] = r.randrange(n + 1)
    else:
        case['arg'] = 0
    case['pilot'] = r.random() < 0.8
    case['self_is_player'] = r.random() < 0.5
    case['x58'] = r.random() < 0.3
    case['game'] = r.choice([0, 1, 2, 3, 4])
    return case


# The target keys' set_target calls (vt+0xc0) the cases must reach: the
# last pick of a kind, the one found before or after the current, the wrap.
COVER1 = {0x4d5f06: 'prev ally: the last enemy', 0x4d5f2b: 'prev ally: the last ally',
          0x4d601e: 'prev ally: before', 0x4d6109: 'prev ally: wrapped',
          0x4d6285: 'prev enemy: the last enemy', 0x4d62ab: 'prev enemy: the last ally',
          0x4d639e: 'prev enemy: before', 0x4d6489: 'prev enemy: wrapped',
          0x4d64d8: 'next ally: the last enemy', 0x4d64fe: 'next ally: the last ally', 0x4d66e5: 'next ally: after',
          0x4d6736: 'next enemy: the last enemy', 0x4d675e: 'next enemy: the last ally', 0x4d6944: 'next enemy: after',
          0x4d6a6d: 'nearest ally', 0x4d6b7c: 'nearest enemy'}
REACHED1 = set()


def pick_original(case):
    t = Trace()
    e = Emu(t)
    e.cpu.fpu_control(0x007f)
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP

    def hit(uc, address, size, user):
        REACHED1.add(address)
    for addr in COVER1:
        e.uc.hook_add(UC_HOOK_CODE, hit, begin=addr, end=addr)
    n = case['n']
    mem = {}

    def put(a, v):
        e.w32(a, v & 0xffffffff)
        mem[a] = v & 0xffffffff
    vehicles, blocks, seens, contacts = [], [], [], []
    for i in range(n + 1):
        blk = e.world_alloc(0xd0)
        put(blk + 0xc0, case['teams'][i])
        veh = e.object('veh', slots={0x28: ('c', 0)})
        seen = e.object('seen', slots={0x2c: ('c', 0)})
        c = e.world_alloc(0x30)
        put(c, case['owners'][i]), put(c + 4, seen), put(c + 8, case['entities'][i])
        e.w8(c + 0x2c, case['flags'][i])
        mem[c + 0x2c] = case['flags'][i]
        vehicles.append(veh), blocks.append(blk), seens.append(seen), contacts.append(c)
    v2b = dict(zip(vehicles, blocks))
    s2v = dict(zip(seens, vehicles))
    # The AiTarget: AiTarget's vtable with vt+0x50 and vt+0x58 stubbed.
    obj = e.world_alloc(0xd4)
    vt = e.world_alloc(0x100)
    e.cpu.write(vt, e.cpu.read(0x5cced8, 0xe0))
    e.w32(vt + 0x50, e.new_stub('self.vt+0x50', 'c', 0))
    e.w32(vt + 0x58, e.new_stub('self.vt+0x58', 'c', 0))
    e.w32(obj, vt)
    arr = e.world_alloc(4 * max(n, 1))
    for i in range(n):
        e.w32(arr + 4 * i, contacts[i])
    e.w32(obj + 0xac, arr if n else 0), e.w32(obj + 0xb0, arr + 4 * n if n else 0)
    e.w32(obj + 0xb4, arr + 4 * n if n else 0)
    at = lambda k: contacts[k] if k >= 0 else 0  # noqa: E731
    for i, k in enumerate(case['fields']):
        e.w32(obj + 0x6c + 4 * i, at(k))
    e.w32(obj + 0xc0, case['team']), e.w32(obj + 0xc4, case['allies']), e.w32(obj + 0xc8, case['enemies'])
    for off, v in ((0x9c, 1000.0), (0xa0, 1000.0), (0xa4, 1e6)):
        e.wf(obj + off, v)
    player = vehicles[0]
    game = e.object('game', slots={0x2c: ('', 0)})
    pilot = e.world_alloc(0x100)
    put(pilot + 0x14, player), put(pilot + 0x6c, game)
    put(PILOT, pilot if case['pilot'] else 0)
    t.script = {
        'seen.vt+0x2c': lambda k, a: s2v[a[0]], 'veh.vt+0x28': lambda k, a: v2b[a[0]],
        'self.vt+0x50': player if case['self_is_player'] else 0x7777, 'self.vt+0x58': int(case['x58']),
        'game.vt+0x2c': case['game'],
    }
    fn, side, arg = case['fn'], case['side'] & 0xffffffff, case['arg']
    sets = []
    if fn == 0:
        ret = e.call(0x4d6b90, ecx=obj, stack=(side,))
    elif 1 <= fn <= 6:
        # Every set goes through vt+0xc0 (a mech's tells the HUD, 0x0041d890).
        def vt_c0(uc, address, size, user):
            sets.append(e.r32(uc.reg_read(UC_X86_REG_ESP) + 4))
        e.uc.hook_add(UC_HOOK_CODE, vt_c0, begin=0x4d5e40, end=0x4d5e40)
        e.call(KEYS[fn - 1], ecx=obj, stack=(side,))
        ret = 0
    elif fn == 7:
        ret = e.call(0x4d5e40, ecx=obj, stack=(at(arg), side)) & 0xff
    elif fn == 8:
        e.call(0x4d6d20, ecx=obj, stack=(arg,))
        ret = 0
    elif fn == 9:
        e.call(0x4d6d00, ecx=obj, stack=(arg,))
        ret = 0
    elif fn == 10:
        e.call(0x4d7220, ecx=obj)
        ret = 0
    elif fn == 11:
        ret = e.call(0x4d7240, ecx=obj)
    else:
        ret = e.call(0x4d6190, ecx=obj, stack=(side, vehicles[arg])) & 0xff
    state = [e.r32(obj + 0x6c + 4 * i) for i in range(4)] + [e.r32(obj + o) for o in (0x9c, 0xa0, 0xa4)]
    return t, ret, state, dict(mem=mem, contacts=contacts, vehicles=vehicles, obj=obj, s2v=s2v, v2b=v2b,
                                player=player, game=game, sets=sets)


def pick_rebuild(probe, case, w):
    s2v, v2b, player = w['s2v'], w['v2b'], w['player']
    t = Trace({
        'seen.vt+0x2c': lambda k, a: s2v[a[0]], 'veh.vt+0x28': lambda k, a: v2b[a[0]],
        'self.vt+0x50': player if case['self_is_player'] else 0x7777, 'self.vt+0x58': int(case['x58']),
        'game.vt+0x2c': case['game'],
    })
    probe.probe_app_set_callback(t.callback())
    probe.probe_targets_reset()
    for a, v in w['mem'].items():
        probe.probe_targets_poke(a, v)
    n, contacts = case['n'], w['contacts']
    arr = (U32 * (5 * max(n, 1)))()
    for i in range(n):
        c = contacts[i]
        arr[5 * i:5 * i + 5] = [c, w['mem'][c], w['mem'][c + 4], w['mem'][c + 8], w['mem'][c + 0x2c]]
    at = lambda k: contacts[k] if k >= 0 else 0  # noqa: E731
    fields = (U32 * 7)(*[at(k) for k in case['fields']], case['team'], case['allies'], case['enemies'])
    probe.probe_aitarget_set(arr, n, fields)
    fn, side, arg = case['fn'], case['side'], case['arg']
    if fn == 7:
        arg = at(arg)
    elif fn == 12:
        arg = w['vehicles'][arg]
    ret = probe.probe_aitarget_call(fn, side, arg, w['obj'])
    out = (U32 * 7)()
    probe.probe_aitarget_state(out)
    sets = (U32 * 16)()
    n_sets = probe.probe_aitarget_sets(sets, 16)
    return t, ret, list(out), list(sets[:n_sets])


def run_picks(probe, count):
    calls = 0
    fns = {}
    for seed in range(count):
        case = make_pick_case(seed)
        a, ra, sa, w = pick_original(case)
        b, rb, sb, sets = pick_rebuild(probe, case, w)
        name = f'pick seed {seed} (fn {case["fn"]}, side {case["side"]}, {case["n"]} contacts)'
        if not compare(name, a.log, b.log):
            die()
        if ra != rb or sa != sb:
            print(f'{name}: returned {ra:#x} / {rb:#x}; state {[hex(x) for x in sa]} / {[hex(x) for x in sb]}')
            die()
        if 1 <= case['fn'] <= 6 and w['sets'] != sets:
            print(f'{name}: vt+0xc0 calls {[hex(x) for x in w["sets"]]} / {[hex(x) for x in sets]}')
            die()
        calls += len(a.log)
        fns[case['fn']] = fns.get(case['fn'], 0) + 1
    missing = [v for k, v in COVER1.items() if k not in REACHED1]
    if missing:
        print(f'  part 1 never reached: {", ".join(missing)}')
    print(f'  part 1: {count} picks ({", ".join(f"fn {k}: {v}" for k, v in sorted(fns.items()))}), '
          f'{calls} calls identical')


# --- part 2: CZAiTarget --------------------------------------------------------------------
def shipped_records():
    """Every mission's targets.zrd as (archive, [record dicts])."""
    out = []
    for p in sorted(ZBD.glob('*/reader*.zbd')):
        d = p.read_bytes()
        e = {n: d[s:s + k] for n, s, k in zrd.entries(d)}
        if 'targets.zrd' not in e:
            continue
        v, _ = zrd.node(e['targets.zrd'], 0)
        recs = v[0] if isinstance(v, list) and len(v) == 1 else v
        lst = []
        for i in range(0, len(recs) - 1, 2):
            lst.append(record_of(recs[i], recs[i + 1]))
        out.append((f'{p.parent.name}/{p.name}', lst))
    return out


def s(v):
    return v.decode('latin1') if isinstance(v, bytes) else v


def record_of(key, body):
    """The record as mech3/zrdraiv's TargetRec would hold it (the reader's
    own rules are test_ai_readers.py's): the fields this test needs."""
    rec = {'key': s(key), 'kind': body[0] if isinstance(body[0], int) else 2, 'health': 100.0, 'flags': 0x30,
           'height': 0.0, 'toff': [0.0] * 3, 'coff': [0.0] * 3, 'name': None, 'anim': None, 'target': None,
           'centroid': None, 'hb': None, 'parsed': True}
    for j in range(1, len(body) - 1, 2):
        k, v = s(body[j]), body[j + 1]
        if not isinstance(v, list):
            continue
        if k == 'health' and v:
            rec['health'] = float(v[0])
            rec['flags'] |= 1
        elif k == 'destroy_anim' and v:
            rec['anim'] = s(v[0])
        elif k == 'name' and v:
            rec['name'] = 'loc:' + s(v[0])
        elif k == 'target' and v:
            rec['target'] = s(v[0])
            if len(v) > 1 and isinstance(v[1], list) and len(v[1]) >= 3:
                rec['toff'] = [float(x) for x in v[1][:3]]
        elif k == 'centroid' and v:
            rec['centroid'] = s(v[0])
            if len(v) > 1 and isinstance(v[1], list) and len(v[1]) >= 3:
                rec['coff'] = [float(x) for x in v[1][:3]]
        elif k == 'height' and v:
            rec['height'] = float(v[0])
        elif k == 'radar':
            rec['flags'] |= 4
        elif k == 'health_below':
            hb = []
            for q in range(0, len(v) - 2, 3):
                hb.append((s(v[q]), v[q + 1] if isinstance(v[q + 1], int) else 0, float(v[q + 2])))
            rec['hb'] = hb
    return rec


def crafted_records(r, n):
    out = []
    for i in range(n):
        key = r.choice(['tgt', 'tgt', 'bunker', 'tower_a', 'tower_ab', 'x'])
        hb = None
        if r.random() < 0.7:
            k = r.choice([0, 1, 2, 3, 5, 16, 17, 20, 33])
            hb = [(f'v{r.randrange(6)}', r.randrange(3), r.choice([0.0, 10.0, 25.0, 50.0, 50.0, 75.0, 100.0,
                                                                   r.uniform(-10, 120)])) for _ in range(k)]
        out.append({'key': key, 'kind': r.choice([0, 1, 2, 2, 3, 33, 65]), 'health': r.choice([1.0, 35.0, 100.0, 250.0]),
                    'flags': r.randrange(0x40), 'height': r.choice([0.0, 0.0, 12.5, -1.0]),
                    'toff': [r.uniform(-5, 5) for _ in range(3)], 'coff': [r.uniform(-5, 5) for _ in range(3)],
                    'name': r.choice([None, 'loc:MSG_X']), 'anim': r.choice([None, 'destroy_a', 'destroy_b', 'nope']),
                    'target': r.choice([None, 'tnode']), 'centroid': r.choice([None, 'cnode']), 'hb': hb,
                    'parsed': r.random() < 0.95})
    return out


class World:
    """The nodes, animations and handler tables of one case, at the same
    addresses on both sides."""

    def __init__(self, e, recs, r, traps):
        self.e, self.mem = e, {}
        self.all_nodes = []
        self.steals = []
        self.nodes = {}       # walk key -> [node]
        self.anims = {}       # (anim name, node) -> anim or 0
        self.valves = {}
        names = sorted({rec['key'] for rec in recs})
        for nm in names:
            k = r.choice([0, 1, 1, 1, 2, 3]) if traps or r.random() < 0.3 else 1
            self.nodes[nm] = [self.node(r) for _ in range(k)]
        for rec in recs:
            for node in self.nodes[rec['key']]:
                if rec['anim'] and (rec['anim'], node) not in self.anims:
                    self.anims[(rec['anim'], node)] = self.anim(r, node) if r.random() < 0.9 else 0

    def put(self, a, v, size=4):
        (self.e.w8 if size == 1 else self.e.w32)(a, v & 0xffffffff)
        self.mem[a] = v & 0xffffffff

    def node(self, r):
        n = self.e.world_alloc(0xd0)
        self.all_nodes.append(n)
        self.put(n + 0x24, 0x100 if r.random() < 0.8 else 0)
        self.put(n + 0x38, 0x5000)
        for i in range(6):
            self.put(n + 0x74 + 4 * i, f2u(r.uniform(-20, 20)))
        self.put(n + 0xc0, self.e.world_alloc(0x10))  # the table a registration makes (the test's)
        kids = [self.e.world_alloc(0x10) if r.random() < 0.7 else r.choice([0x7300, 0x7400, 0x7500])
                for _ in range(r.randrange(4))]
        arr = self.e.world_alloc(4 * max(1, len(kids)))
        for i, k in enumerate(kids):
            self.put(arr + 4 * i, k)
        self.put(n + 0x5c, len(kids)), self.put(n + 0x60, arr)
        return n

    def anim(self, r, node):
        a = self.e.world_alloc(0x140)
        self.put(a + 0x40, node)
        self.put(a + 0x98, 1, 1)  # in use: the lookup 0x50b4e0 never gives a free one (5)
        self.put(a + 0x64, self.node(r) if r.random() < 0.15 else 0)
        self.put(a + 0xac, f2u(r.choice([50.0, 100.0, 300.0])))
        self.put(a + 0xb0, f2u(r.choice([50.0, 100.0, 300.0])))
        owner = self.mem[a + 0x64] or node
        kind = r.choice(['none', 'none', 'anim', 'anim', 'anim_nohit', 'foreign', 'other', 'anim_collide_only'])
        if kind != 'none':
            t = self.e.world_alloc(0x10)
            w = {'anim': (a, 0x502e60, a, 0x502df0), 'anim_nohit': (0, 0, a, 0x502df0),
                 'foreign': (a, 0x401000, a, 0x502df0), 'other': (0x7000, 0x502e60, a, 0x502df0),
                 'anim_collide_only': (a, 0x502e60, 0, 0)}[kind]
            for i, v in enumerate(w):
                self.put(t + 4 * i, v)
            self.put(owner + 0xbc, t)
        return a

    def valve(self, name):
        if name not in self.valves:
            self.valves[name] = 0x7100 + 0x10 * len(self.valves)
        return self.valves[name]


def script_for(world, recs, traps, r_seed, answers):
    """The calls' answers (each side gets its own walk state)."""
    state = {'walk': []}
    key_walk = '0x004f8b80' if traps else '0x004f8c30'

    def walk(n, a):
        if isinstance(a[0], str):
            if traps:
                state['walk'] = [x for k, v in sorted(world.nodes.items()) if k.startswith(a[0]) for x in v]
            else:
                state['walk'] = list(world.nodes.get(a[0], []))
            return 0
        return state['walk'].pop(0) if state['walk'] else 0

    def anim_of(n, a):
        name, ln, node = a
        for (an, nd), v in world.anims.items():
            if nd == node and an[:ln] == name[:ln] and len(name) == ln:
                return v
        return 0
    rng = random.Random(r_seed)
    ents = [rng.choice([0, 0x7300, 0x7400]) for _ in range(64)]
    return {
        key_walk: walk, '0x0050b4e0': anim_of,
        ('0x004decf0/traps' if traps else '0x004decf0'): len(recs),
        '0x005a8390': lambda n, a: ents[n % len(ents)] if a[1] else 0,
        '0x004d4f20': 1, '0x00583250': lambda n, a: answers[n % len(answers)],
        '0x005833b0': lambda n, a: answers[(n + 3) % len(answers)], '0x004d28e0': lambda n, a: answers[(n + 5) % len(answers)],
        '0x004fc3f0': 0x7500,
    }


# Places in the original the cases must reach.
COVER = {0x4d2770: 'a foreign callback reported', 0x4d27b0: "another animation's callback reported",
         0x4d2827: 'handlers inherited', 0x4d34dd: "the animation's handlers taken over",
         0x4d34af: 'new handlers', 0x4d34cb: 'the collide handler after the weapon one',
         0x4d352a: 'translate_to_anim_root', 0x4d3585: 'a child moved back', 0x4d33e4: 'handlers removed',
         0x4d2a37: 'the height from the record', 0x4d2a4f: 'the height from the box',
         0x4d2c4a: 'a trigger on a hit', 0x4d2c83: 'destroyed by a hit', 0x4d2ca4: 'a killing hit',
         0x4d2cbf: 'the health left', 0x4d2bbe: 'a trigger on the kill', 0x4d2562: 'the triggers sorted',
         0x4d2237: 'more than 16 triggers', 0x4d253c: "the animation's health", 0x4d2644: 'no destroy_anim',
         0x4d2640: 'the animation not found', 0x4d3263: 'a trap made', 0x4d2f89: 'a target made',
         0x4d8360: 'every target deleted', 0x4d3030: 'the markers deleted', 0x4d2b4a: 'the destroy animation',
         0x4d344c: 'the centroid offset', 0x4d346a: 'the target offset'}
REACHED = set()

CALLS2 = {0x59b830: 'zs0,s1,s2,s3', 0x5a7e10: 'c,s0', 0x5a7f70: 'c', 0x5a7f90: 'c',
          0x40ed00: '*c,s0', 0x40eaa0: '*c,s0', 0x4fc1b0: 'c,s0,s1,s2', 0x4d5100: 'c', 0x4d4f20: 'c,d',
          0x583250: 'c', 0x5833b0: 'c,d,s0,s1', 0x5833f0: 'c,d,s0', 0x583400: 'c', 0x583710: 'c,d', 0x583730: 's0',
          0x4d3c00: 'c,s0', 0x508c10: 'c,d,s0,s1,s2', 0x49bfd0: 's0,s2,zs3,s4', 0x50b4e0: 'zc,d,s0',
          0x4d28e0: 'd', 0x4fc300: 'c', 0x523c50: 's0', 0x51e9d0: '', 0x51e930: '', 0x4fc3f0: 'c',
          0x51fee0: 'c,d', 0x51ea00: '', 0x542e40: 'c'}


def targets_original(recs, traps, ops, seed, answers, r_world):
    t = Trace()
    t.silent |= {'new', 'delete'}
    e = Emu(t)
    e.cpu.fpu_control(0x007f)
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP

    def hit(uc, address, size, user):
        REACHED.add(address)
    for addr in COVER:
        e.uc.hook_add(UC_HOOK_CODE, hit, begin=addr, end=addr)
    world = World(e, recs, r_world, traps)
    targets, markers = [0x2c300000 + 0x200 * i for i in range(64)], [0x2c3f0000 + 0x100 * i for i in range(3)]
    for a in CALLS2:
        e.stub(a, spec=CALLS2[a])
    e.stub(0x41ebb0, 'marker.vt+0x0', 'c,s0')  # the markers' vtable 0x005bce78 slot 0

    def register(slot):
        def fn(em, key, a, pop):
            v = t.handle(key, a)
            if v:  # as zWeapon's: the node's table (made when it has none) gets the slot
                node = a[1]
                if not em.r32(node + 0xbc):
                    em.w32(node + 0xbc, em.r32(node + 0xc0))
                tb = em.r32(node + 0xbc)
                em.w32(tb + 8 * slot, a[0]), em.w32(tb + 8 * slot + 4, a[-1])
            em._ret(v, pop)
        return fn
    e.stub(0x5833b0, spec=CALLS2[0x5833b0], fn=register(0))
    e.stub(0x5833f0, spec=CALLS2[0x5833f0], fn=register(1))
    alloc = {'t': 0, 'm': 0}

    def new(em, key, a, pop):
        if a[0] == 0xf4:
            v = targets[alloc['t']]
            alloc['t'] += 1
        elif a[0] == 0xbc:
            v = markers[alloc['m']]
            alloc['m'] += 1
        else:
            v = em.alloc(max(a[0], 4))
        em.cpu.write(v, bytes(a[0]))
        em._ret(v, 0)
    e.stub(0x5abd26, 'new', 's0', 0, new)
    e.stub(0x5abd1a, 'delete', 's0', 0, lambda em, k, a, p: em._ret(0, 0))

    def strdup(em, key, a, pop):
        sv = em.cstr(em.args('s0')[0])
        p = em.alloc(len(sv) + 1)
        em.cpu.write(p, sv.encode('latin1') + b'\0')
        em._ret(p, 0)
    e.import_stub(0x5bb670, 'strdup', '', 0, strdup)
    e.import_stub(0x5bb65c, 'free', '', 0, lambda em, k, a, p: em._ret(0, 0))
    e.stub(0x4f8b80 if traps else 0x4f8c30, spec='zc,d')

    def entity_init(em, key, a, pop):
        t.handle(key, a)
        em.w32(a[0] + 4, 0), em.w8(a[0] + 8, 1)
        for off in (0xc, 0x10, 0x14):
            em.w32(a[0] + off, 0)
        em._ret(a[0], 4)
    e.stub(0x5a7e10, spec='c,s0', fn=entity_init)

    def set_node(em, key, a, pop):
        this, node, aa, cen, tg = em.args('c,s0,s1,zs2,zs3')
        r = t.handle('0x005a8390', [this, node, aa, cen, tg])
        em.w32(this + 4, node), em.w32(this + 0xc, 0)
        em.w32(this + 0x10, r if node else 0), em.w32(this + 0x14, (r + 0x100 if r else 0) if node else 0)
        em._ret(0, 0x10)
    e.stub(0x5a8390, spec='', fn=set_node)
    # The reader: the records as the port's would be, kept in the emulator's memory.
    strings = {}

    def cstr(v):
        if v is None:
            return 0
        if v not in strings:
            strings[v] = e.world_string(v)
        return strings[v]
    rec_addr = []
    for rec in recs:
        a = e.world_alloc(0x40)
        e.w32(a, rec['kind'] & 0xffffffff), e.w32(a + 4, cstr(rec['name'])), e.wf(a + 8, rec['health'])
        e.w32(a + 0xc, cstr(rec['anim'])), e.w32(a + 0x10, cstr(rec['target'])), e.w32(a + 0x14, cstr(rec['centroid']))
        for i in range(3):
            e.wf(a + 0x18 + 4 * i, rec['toff'][i]), e.wf(a + 0x24 + 4 * i, rec['coff'][i])
        if rec['hb'] is not None:
            vec = e.world_alloc(0x14)
            arr = e.world_alloc(12 * max(1, len(rec['hb'])))
            for i, (vn, aa, h) in enumerate(rec['hb']):
                e.w32(arr + 12 * i, world.valve(vn)), e.w32(arr + 12 * i + 4, aa & 0xffffffff)
                e.wf(arr + 12 * i + 8, h)
            e.w32(vec + 4, arr), e.w32(vec + 8, arr + 12 * len(rec['hb'])), e.w32(vec + 0xc, arr + 12 * len(rec['hb']))
            e.w32(a + 0x30, vec)
        e.wf(a + 0x34, rec['height'])
        e.w8(a + 0x38, rec['flags'])
        rec_addr.append(a if rec['parsed'] else 0)
    keys = {rec['key']: cstr(rec['key']) for rec in recs}
    sfx = '/traps' if traps else ''
    file_obj = 0x60b630 if traps else 0x60b608
    for a, k in ((0x4deb50, '0x004deb50'), (0x4deba0, '0x004deba0')):
        e.stub(a, spec='', fn=lambda em, key, aa, pop, k=k: (t.handle(k + sfx, []), em._ret(0, 0)))
    e.stub(0x4decf0, spec='', fn=lambda em, key, aa, pop: em._ret(t.handle('0x004decf0' + sfx, []), 0))

    def record_value(em, key, a, pop):
        i, slot = em.args('s0,s1')
        t.handle('0x004ded20', [i])
        ii = i - (1 << 32) if i & 0x80000000 else i
        em.w32(slot, keys[recs[ii]['key']] if 0 <= ii < len(recs) else 0)
        em._ret(0x7600 + i, 8)
    e.stub(0x4ded20, spec='', fn=record_value)

    def parse(em, key, a, pop):
        value, name = em.args('s0,s1')
        t.handle('0x004e07e0' if traps else '0x004e08c0', [em.cstr(name) if name else 0])
        em._ret(rec_addr[value - 0x7600] if 0x7600 <= value < 0x7600 + len(recs) else 0, 8)
    e.stub(0x4e07e0 if traps else 0x4e08c0, spec='', fn=parse)
    for a in (0x4e0870, 0x4e07d0, 0x4e0890):
        e.stub(a, spec='', fn=lambda em, key, aa, pop: em._ret(0, 0))
    for a in (LIST, LIST + 4, LIST + 8, LIST + 0xc, LIST + 0x10, MARKERS, MARKERS + 4, MARKERS + 8, SORTED):
        e.w32(a, 0)
    t.script = script_for(world, recs, traps, seed, answers)
    t.script[('0x004decf0/traps' if traps else '0x004decf0')] = len(recs)
    e.call(0x4d31d0 if traps else 0x4d2e20)
    t.log.append(('=state', tuple(state_original(e, world))))
    for op in ops:
        made = state_list(e)
        k = op[0]
        if k in ('hit', 'collide', 'destroy', 'steal') and not made:
            continue
        tgt = made[op[1] % len(made)] if made and len(op) > 1 else 0
        if k == 'hit':
            e.call(0x4d26b0, ecx=tgt, edx=op[2], stack=(op[3], op[4]))
        elif k == 'collide':
            e.call(0x4d26d0, ecx=tgt, stack=(op[2],))
        elif k == 'destroy':
            e.call(0x4d2b30, ecx=tgt)
        elif k == 'delete':
            e.call(0x4d8360, ecx=LIST)
        elif k == 'markers':
            e.call(0x4d3030)
        elif k == 'steal':
            pokes = []
            anim = e.r32(tgt + 0xdc) if tgt else 0
            if anim:
                owner = e.r32(anim + 0x64) or e.r32(anim + 0x40)
                tb = e.r32(owner + 0xbc)
                if tb:
                    pokes = [(tb, 0x7000), (tb + 4, 0x502e60)]
                    for a, v in pokes:
                        e.w32(a, v)
            world.steals.append(pokes)
        t.log.append(('=state', tuple(state_original(e, world))))
    return t, world, targets


def state_list(e):
    b, l = e.r32(LIST + 4), e.r32(LIST + 8)
    return [e.r32(p) for p in range(b, l, 4)] if b else []


def state_original(e, world):
    out = []
    lst = state_list(e)
    out.append(len(lst))
    for a in lst:
        vt = e.r32(a)
        out += [a, int(vt == 0x5ccc80), e.r32(a + 4), e.r8(a + 8), e.r32(a + 0x10), e.r32(a + 0x14)]
        out += [e.r32(a + 0x68), e.r32(a + 0xc0), e.r32(a + 0xc4), e.r32(a + 0xc8)]
        out += [e.r32(a + 0x9c), e.r32(a + 0xa0), e.r32(a + 0xa4)]
        out += [e.r32(a + 0xd4), e.r32(a + 0xd8), e.r32(a + 0xdc), e.r32(a + 0xe0), e.r8(a + 0xe8)]
        nm = e.r32(a + 0xec)
        if nm == 0:
            out.append(0xffffffff)
        else:
            sv = e.cstr(nm)
            out.append(len(sv))
            out += [ord(c) for c in sv]
        hb = e.r32(a + 0xe4)
        if hb == 0:
            out.append(0xffffffff)
        else:
            b, l, cur = e.r32(hb + 4), e.r32(hb + 8), e.r32(hb + 0x10)
            out += [(l - b) // 12 if b else 0, (cur - b) // 12 if b else 0]
            for p in range(b, l, 12):
                out += [e.r32(p), e.r32(p + 4), e.r32(p + 8)]
        out.append(e.r8(a + 0xf0))
    out += [e.r32(MARKERS), e.r32(MARKERS + 4), e.r32(MARKERS + 8)]
    for n in world.all_nodes:
        tb = e.r32(n + 0xbc)
        out += [tb] + ([e.r32(tb + 4 * i) for i in range(4)] if tb else [])
    return out


def targets_rebuild(probe, recs, traps, ops, seed, answers, world, targets):
    steals = list(world.steals)
    t = Trace(script_for(world, recs, traps, seed, answers))
    t.script[('0x004decf0/traps' if traps else '0x004decf0')] = len(recs)
    t.silent |= {'new', 'delete'}
    markers = [0x2c3f0000 + 0x100 * i for i in range(3)]
    mk = {'n': 0}

    def marker(n, a):
        v = markers[mk['n']]
        mk['n'] += 1
        return v
    t.script['0x0059b830'] = marker
    probe.probe_app_set_callback(t.callback())
    probe.probe_targets_reset()
    for a, v in world.mem.items():
        probe.probe_targets_poke(a, v)
    keep = []
    for rec in recs:
        w = (U32 * 11)(rec['kind'] & 0xffffffff, f2u(rec['health']), rec['flags'], f2u(rec['height']),
                       *[f2u(x) for x in rec['toff']], *[f2u(x) for x in rec['coff']], int(rec['parsed']))
        strs = [rec['key'], rec['name'], rec['anim'], rec['target'], rec['centroid']]
        sv = (ctypes.c_char_p * 5)(*[(x or '').encode('latin1') for x in strs])
        has = (ctypes.c_int32 * 5)(*[int(x is not None) for x in strs])
        hb = rec['hb']
        arr = (U32 * (3 * max(1, len(hb or []))))()
        for i, (vn, aa, h) in enumerate(hb or []):
            arr[3 * i:3 * i + 3] = [world.valve(vn), aa & 0xffffffff, f2u(h)]
        keep.append((w, sv, has, arr))
        probe.probe_targets_add_record(int(traps), w, sv, has, arr, -1 if hb is None else len(hb))
    hs = (U32 * len(targets))(*targets)
    probe.probe_targets_handles(hs, len(targets))
    probe.probe_targets_create(int(traps))
    t.log.append(('=state', tuple(state_rebuild(probe, world))))
    for op in ops:
        made = rebuild_list(probe)
        k = op[0]
        if k in ('hit', 'collide', 'destroy', 'steal') and not made:
            continue
        tgt = made[op[1] % len(made)] if made and len(op) > 1 else 0
        if k == 'hit':
            probe.probe_targets_op(0, tgt, op[2], op[3], op[4])
        elif k == 'collide':
            probe.probe_targets_op(1, tgt, op[2], 0, 0)
        elif k == 'destroy':
            probe.probe_targets_op(2, tgt, 0, 0, 0)
        elif k == 'delete':
            probe.probe_targets_op(3, 0, 0, 0, 0)
        elif k == 'markers':
            probe.probe_targets_op(4, 0, 0, 0, 0)
        elif k == 'steal':
            for a, v in steals.pop(0):
                probe.probe_targets_poke(a, v)
        t.log.append(('=state', tuple(state_rebuild(probe, world))))
    return t


def state_rebuild(probe, world=None):
    buf = (U32 * 200000)()
    n = probe.probe_targets_state(buf, 200000)
    out = list(buf[:n])
    for nd in (world.all_nodes if world else []):
        tb = probe.probe_targets_peek(nd + 0xbc)
        out += [tb] + ([probe.probe_targets_peek(tb + 4 * i) for i in range(4)] if tb else [])
    return out


def rebuild_list(probe):
    st = state_rebuild(probe)
    out, i = [], 1
    for _ in range(st[0]):
        out.append(st[i])
        i += 18
        nlen = st[i]
        i += 1 + (0 if nlen == 0xffffffff else nlen)
        hn = st[i]
        i += 1 if hn == 0xffffffff else 2 + 3 * hn
        i += 1
    return out


def make_ops(r):
    ops = []
    for _ in range(r.randrange(0, 12)):
        k = r.random()
        if k < 0.5:
            ops.append(('hit', r.randrange(64), r.choice([0, 0x7700]), 0x7800,
                        f2u(r.choice([1.0, 5.0, 10.0, 25.0, 40.0, 100.0, 1000.0, r.uniform(0, 60)]))))
        elif k < 0.75:
            ops.append(('collide', r.randrange(64), f2u(r.choice([1.0, 20.0, 50.0, r.uniform(0, 80)]))))
        elif k < 0.9:
            ops.append(('destroy', r.randrange(64)))
        else:
            ops.append(('steal', r.randrange(64)))
    if r.random() < 0.6:
        ops.append(('delete',))
    if r.random() < 0.7:
        ops.append(('markers',))
    return ops


def run_targets(probe, count):
    calls, made, n_cases = 0, 0, 0
    shipped = shipped_records()
    cases = [(f'{name}', lst, False) for name, lst in shipped]
    r = random.Random(1)
    for i in range(count):
        traps = r.random() < 0.2
        cases.append((f'crafted {i}', crafted_records(r, r.randrange(0, 7)), traps))
    for idx, (name, recs, traps) in enumerate(cases):
        rr = random.Random(idx)
        ops = make_ops(rr)
        answers = [rr.choice([0, 1, 1]) for _ in range(16)]
        a, world, targets = targets_original(recs, traps, ops, idx, answers, random.Random(idx * 7 + 1))
        world_mem = world
        b = targets_rebuild(probe, recs, traps, ops, idx, answers, world_mem, targets)
        if not compare(f'targets {name}', a.log, b.log):
            die()
        calls += len(a.log)
        made += next(x[1][0] for x in a.log if x[0] == '=state')
        n_cases += 1
    missing = [v for k, v in COVER.items() if k not in REACHED]
    if missing:
        print(f'  part 2 never reached: {", ".join(missing)}')
    print(f'  part 2: {n_cases} cases ({len(shipped)} shipped files), {made} targets made, {calls} calls and states '
          f'identical')


def main():
    probe = load_probe()
    setup_probe(probe)
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    run_picks(probe, count)
    run_targets(probe, max(60, count // 10))
    print('the rebuilt targeting and targets match the original')
    sys.stdout.flush()
    os._exit(0)


if __name__ == '__main__':
    main()
