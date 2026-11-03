// C:\zipper\branchep1\gamez\zSound\zsnd_init.cpp: Pirate's Moon's sounds.zrd
// reader, rewritten smaller (docs/sound.md, Pirate's Moon): no SYNTAX, no
// demo sets, sets made by name, the formats from COMPILE_FORMAT, queue
// priorities. The reader's functions follow 0x00591900 in the exe.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gamez/zerror/zerr.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/zfix.h"
#include "pm/gamez/zsound/zsnd.h"

namespace pm::zsnd {

namespace {
using ::zsnd::Format;
using ::zsnd::State;
const char kFile[] = "C:\\zipper\\branchep1\\gamez\\zSound\\zsnd_init.cpp";

State& st() { return ::zsnd::state(); }
uint32_t raw(const zrdr::Node& n) { return static_cast<uint32_t>(n.i); }
bool has(zrdr::Node* n, const char* key) { return zrdr::find(n, key) != nullptr; }

// FIX (bug) as MW3's reader: the exe asks for "MED", which never matches the data's "MEDIUM".
const char* med() { return zfix::options().bugs ? "MEDIUM" : "MED"; }

// 0x00592060: a format entry, a file name or a list of rate, bits and
// channels (at least three items); false leaves `f` as it was.
bool read_spec(Format* f, zrdr::Node* n) {
  if (!n) return false;
  if (n->type == zrdr::String) {
    *f = Format{};
    f->file = n->s, f->has_file = true;
    return true;
  }
  if (n->type == zrdr::List && n->list[0].i > 3) {
    f->file.clear(), f->has_file = false;
    f->rate = static_cast<int32_t>(raw(n->list[1]));
    f->bits = static_cast<int32_t>(raw(n->list[2]));
    f->channels = static_cast<int32_t>(raw(n->list[3]));
    return true;
  }
  return false;
}

void read_specs(Format* out, zrdr::Node* parent) {
  read_spec(&out[0], zrdr::find(parent, "HIGH"));
  read_spec(&out[1], zrdr::find(parent, med()));
  read_spec(&out[2], zrdr::find(parent, "LOW"));
}

// 0x005917c0: a set INCLUDE_SETS names (strcmp), or any set when it names none.
bool included(const std::vector<const char*>& include, const char* name) {
  if (!name) return false;
  if (include.empty()) return true;
  for (const char* s : include)
    if (s && std::strcmp(s, name) == 0) return true;
  return false;
}
}  // namespace

void statics() {
  ::zsnd::set_speed_of_sound(345.0f);
  st().max_duplicates = 4;
}

// Pirate's Moon 1.0 @ 0x00591830
void set_range_scale(float scale) { st().range_scale = scale; }

// Pirate's Moon 1.0 @ 0x00591840 (the window, the file, the load callback):
// as MW3's 0x005705e0, then the reader whatever SYNTAX says.
int32_t load(uint32_t window, const char* name, LoadFn fn, int32_t /*demo*/) {
  State& s = st();
  if (s.open || !s.initialized) return 0;
  if (window) s.window = window;
  if (s.api == 0 && !::zsnd::open_device()) return 0;
  s.open = 1;
  s.root = zrdr::load(name);
  if (!s.root) {
    char msg[300];
    std::snprintf(msg, sizeof msg, "Failed to find menu sounds (%s)", name);
    zerr::report(0x200, kFile, 0x31c, msg);
    return 0;
  }
  read_sounds(s.root, fn);  // the callback through [0x00613484] meanwhile
  return 1;
}

// Pirate's Moon 1.0 @ 0x00591900: CD_TRACKS, SOUND_PATH, COMPILE_FORMAT,
// DEFAULT_QUEUE_PRIORITY, SPEED_OF_SOUND, PLAYBACK_FORMAT, the SETS that
// INCLUDE_SETS names (all when it names none; the data's EXCLUDE_SETS is not
// read), then SOUND_GROUPS (DEMO_GROUPS is not read either).
int32_t read_sounds(zrdr::Node* root, LoadFn fn) {
  State& s = st();
  if (::zsnd::hooks().cd_open) ::zsnd::hooks().cd_open(zrdr::find(root, "CD_TRACKS"));  // 0x00593190
  if (const char* sp = zrdr::get_string(root, "SOUND_PATH")) {
    if (!s.has_search) s.search = sp, s.has_search = true;  // 0x00596f10
    else s.search = s.search + ";" + sp;                     // 0x00596f50
  }
  if (zrdr::Node* cf = zrdr::find(root, "COMPILE_FORMAT")) read_specs(s.compile, cf);
  // The value's first item, raw (a value not a list crashes the exe).
  if (zrdr::Node* q = zrdr::find(root, "DEFAULT_QUEUE_PRIORITY"); q && q->type == zrdr::List)
    s.default_qpriority = q->list[0].i > 1 ? q->list[1].i : 0;
  float sos = 0.0f;
  if (zrdr::get_float(root, "SPEED_OF_SOUND", &sos)) ::zsnd::set_speed_of_sound(sos);
  if (zrdr::Node* pf = zrdr::find(root, "PLAYBACK_FORMAT")) {
    // Each kept as the data has it, channels too (MW3's forced mono).
    const char* const keys[3] = {"HIGH", med(), "LOW"};
    for (int i = 0; i < 3; ++i) {
      Format f;
      if (!read_spec(&f, zrdr::find(pf, keys[i]))) continue;
      s.has_playback[i] = true;  // calloc'd at 0x0061328c + 4i
      s.playback[i] = f;
    }
  }
  std::vector<const char*> include;
  if (zrdr::Node* inc = zrdr::find(root, "INCLUDE_SETS"); inc && inc->type == zrdr::List)
    for (int32_t i = 1; i < inc->list[0].i; ++i)
      if (inc->list[i].type == zrdr::String) include.push_back(inc->list[i].s);
  if (zrdr::Node* sets = zrdr::find(root, "SETS"); sets && sets->type == zrdr::List) {
    zrdr::Node* items = sets->list;
    for (int32_t i = 1; i < items[0].i; i += 2)
      if (items[i].type == zrdr::String && included(include, items[i].s)) read_set(&items[i], fn);
  }
  if (zrdr::Node* g = zrdr::find(root, "SOUND_GROUPS")) ::zsnd::read_groups(g, nullptr, fn);  // 0x00594150
  return 1;
}

// Pirate's Moon 1.0 @ 0x005920e0 (the set's name node, its list after it):
// the set of that name, new or not, its SUB_SET names (made if not read yet)
// and its sounds. A keyword item steps over three: itself, its value and the
// item after (MW3 steps two); the shipped sets put SUB_SET last.
Set* read_set(zrdr::Node* name_node, LoadFn fn) {
  Set* set = make_set(name_node->s);
  zrdr::Node* body = name_node + 1;
  if (zrdr::Node* sub = zrdr::find(body, "SUB_SET"); sub && sub->type == zrdr::List)
    for (int32_t j = 1; j < sub->list[0].i; ++j)
      if (sub->list[j].type == zrdr::String) set->subsets.push_back(make_set(sub->list[j].s));  // 0x00590d20
  zrdr::Node* items = body->list;
  const int32_t n = items[0].i;
  for (int32_t i = 1; i < n; ++i) {
    if (items[i].type == zrdr::List) read_sound(set, &items[i], fn);
    else i += 2;
  }
  return set;
}

// Pirate's Moon 1.0 @ 0x00592170 (ECX the set, the sound's list): as MW3's
// (0x00570af0's loop), with RANGE through the vec2 getter and times the range
// scale, the formats from COMPILE_FORMAT (zero without it), QPRIORITY, and
// the volume clamped after the callbacks. The sound goes to the set's end.
Sound* read_sound(Set* set, zrdr::Node* node, LoadFn fn) {
  State& st_ = st();
  set->sounds.emplace_back();  // calloc(0xd0) (0x00592040), appended at the end
  Sound& s = set->sounds.back();
  s.kind = 0;
  s.queue_life = 5.0f;
  s.volume = 1.0f;
  s.a3d_dist = 1.0f;
  s.range_min = st_.range_scale * 50.0f;
  s.range_max = st_.range_scale * 400.0f;
  for (int f = 0; f < 3; ++f) s.formats[f] = st_.compile[f];
  s.freq_max = 90000.0f;
  s.freq_min = 20000.0f;
  s.subtitle = nullptr;
  s.user = nullptr;
  const zrdr::Node* e = node->list;
  s.name = e[1].s;
  s.file = e[2].s;
  using namespace ::zsnd;
  s.flags = has(node, "3D") ? (s.flags | k3D) : (s.flags & ~k3D);
  s.flags = has(node, "LOOPED") ? (s.flags | kLooped) : (s.flags & ~kLooped);
  s.flags = has(node, "FREQUENCY") ? (s.flags | kFrequency) : (s.flags & ~kFrequency);
  s.flags = has(node, "HARDWARE") ? (s.flags | kHardware) : (s.flags & ~kHardware);
  s.flags = has(node, "PURGEABLE") ? (s.flags | kPurgeable) : (s.flags & ~kPurgeable);
  s.flags = has(node, "VOICE") ? (s.flags | kVoice) : (s.flags & ~kVoice);
  if (zrdr::Node* q = zrdr::find(node, "QUEUE")) {
    s.flags |= kQueue;
    zrdr::get_float_value(q, &s.queue_life);  // 0x00578fc0
  } else {
    s.flags &= ~kQueue;
  }
  // The default, or the value's first item, raw; 0..7 only (a value not a
  // list crashes the exe).
  if (zrdr::Node* q = zrdr::find(node, "QPRIORITY"); q && q->type == zrdr::List) {
    const int32_t v = q->list[0].i > 1 ? q->list[1].i : st_.default_qpriority;
    if (v >= 0 && v < 8) s.flags = (s.flags & ~kQPriority) | (static_cast<uint32_t>(v) << 10);
  }
  zrdr::get_float(node, "VOLUME", &s.volume);
  zrdr::get_float(node, "A3DDIST", &s.a3d_dist);
  float r[2];
  if (zrdr::get_vec2(node, "RANGE", r)) {  // 0x005790f0
    s.flags |= k3D;
    s.range_min = st_.range_scale * r[0];
    s.range_max = st_.range_scale * r[1];
  }
  read_specs(s.formats, node);
  if (st_.subtitle_hook) s.subtitle = st_.subtitle_hook(node);
  if (fn) s.user = fn(&s, node);
  if (s.volume > 1.0f) s.volume = 1.0f;
  else if (!(s.volume >= 0.0f)) s.volume = 0.0f;
  return &s;
}

// Pirate's Moon 1.0 @ 0x00591700: as MW3's 0x00570510 without its second
// round of frees: the CD, the groups and the sets go only with the device
// (0x00591f30), so not at all when it was never opened.
int32_t shutdown() {
  State& s = st();
  ::zsnd::stop_queue(0);
  delete s.queue;
  s.queue = nullptr;
  ::zsnd::close_device(nullptr);
  ::zsnd::fades_shutdown();
  if (s.root) {
    zrdr::free_node(s.root);
    s.root = nullptr;
  }
  s.has_search = false, s.search.clear();
  s.has_primary_format = false;
  for (bool& b : s.has_playback) b = false;
  s.playback_current = -1;
  return 1;
}

}  // namespace pm::zsnd
