// C:\zipper\GameZMech3\zSound\zsnd_init.cpp: start-up, sounds.zrd and the
// DirectSound device (docs/sound.md section 1).
#include <cstdio>
#include <cstring>

#include "gamez/zerror/zerr.h"
#include "gamez/zgame/zgame_table.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/zfix.h"

namespace zsnd {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zSound\\zsnd_init.cpp";
State g_state;
Hooks g_hooks;
platform::SoundOutput* out() { return g_hooks.output; }

uint32_t raw(const zrdr::Node& n) { return static_cast<uint32_t>(n.i); }
float raw_float(const zrdr::Node& n) {
  float f;
  std::memcpy(&f, &n.i, 4);
  return f;
}

// A list value's items 1..3 as raw dwords, as the exe copies them.
void read_triple(zrdr::Node* n, int32_t* a, int32_t* b, int32_t* c) {
  const zrdr::Node* l = n->list;
  *a = static_cast<int32_t>(raw(l[1])), *b = static_cast<int32_t>(raw(l[2])), *c = static_cast<int32_t>(raw(l[3]));
}

// One of a sound's HIGH / MED / LOW entries: a file name, or a format list.
void read_format(zrdr::Node* n, Format& f) {
  if (!n) return;
  if (n->type == zrdr::String) {
    f = Format{};
    f.file = n->s, f.has_file = true;
  } else if (n->type == zrdr::List) {
    f.file.clear(), f.has_file = false;
    read_triple(n, &f.rate, &f.bits, &f.channels);
  }
}

// PLAYBACK_FORMAT's entries keep rate and bits; the channels are set to 1,
// whatever the data says (docs/sound.md, discoveries).
void read_playback(zrdr::Node* pf, const char* key, int i) {
  zrdr::Node* n = zrdr::find(pf, key);
  if (!n) return;
  State& st = g_state;
  st.has_playback[i] = true;
  st.playback[i] = Format{};
  st.playback[i].rate = static_cast<int32_t>(raw(n->list[1]));
  st.playback[i].bits = static_cast<int32_t>(raw(n->list[2]));
  st.playback[i].channels = 1;
  // FIX (bug): the data's channel count is read, so the high format is stereo
  // and the 3D sounds' pan is heard (the exe forced mono).
  if (zfix::options().bugs) st.playback[i].channels = static_cast<int32_t>(raw(n->list[3]));
}

bool has(zrdr::Node* n, const char* key) { return zrdr::find(n, key) != nullptr; }
}  // namespace

State& state() { return g_state; }
Hooks& hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x005700f0 (ECX window). Also the CD state's start
// (0x0072c350 = 2, zsnd_cd) and the bindings to the engine variables.
int32_t init(uint32_t window) {
  State& st = g_state;
  if (st.initialized) return 0;
  st.initialized = 1;
  st.open = 0;
  if (st.api == 0) {
    st.primary = 0;
    st.has_primary_format = false;
    for (bool& b : st.has_playback) b = false;
    st.playback_current = -1;
  }
  st.queue_clock = 0.0f;
  zsnd_cd::state().flags &= ~3u;
  st.queue_next = 0.0f;
  st.has_search = false, st.search.clear();
  zsnd_cd::state().mode = 2;
  st.window = window;
  st.lod_default = 0;
  st.lod = g_hooks.find_var ? static_cast<int32_t*>(g_hooks.find_var("SoundLOD")) : nullptr;
  if (!st.lod) st.lod = &st.lod_default;
  st.mute_default = 0;
  st.mute = g_hooks.find_var ? static_cast<int32_t*>(g_hooks.find_var("MuteSound")) : nullptr;
  if (!st.mute) st.mute = &st.mute_default;
  st.muted = *st.mute;
  st.cue_sound = nullptr, st.cue_voice = nullptr, st.cue_index = 0, st.cue_limit = 999;
  st.volume_default = 1.0f;
  st.volume = g_hooks.find_var ? static_cast<float*>(g_hooks.find_var("SoundVolume")) : nullptr;
  // null: the State's own volume_default (master())
  st.voices_on = 1;
  st.queue = new std::list<Queued*>;
  st.queued = nullptr;
  st.subtitle_hook = nullptr;
  st.can_play_hook = nullptr;
  st.range_scale = 1.0f;  // PM's 0x005912a0 (MW3 has no scale)
  return 1;
}

// Mech3.exe 1.2 @ 0x00570280
int32_t quality(int32_t lod) { return lod == 1 ? 1 : lod == 2 ? 2 : 0; }

// Mech3.exe 1.2 @ 0x005702a0 (ECX the quality, null for SoundLOD's). The
// primary buffer's format: the PLAYBACK_FORMAT entry, else mono 22050 Hz at
// 16 bits if the card has them. Returns the last SetCooperativeLevel's result.
int32_t set_primary_format(const int32_t* q) {
  State& st = g_state;
  const int32_t quality_ = q ? *q : quality(*st.lod);
  if (st.api != 0 || !st.has_primary_format) return 0;
  if (quality_ >= 0 && quality_ <= 2) st.playback_current = st.has_playback[quality_] ? quality_ : -1;
  platform::WaveFormat& f = st.primary_format;
  if (st.playback_current >= 0) {
    const Format& p = st.playback[st.playback_current];
    f.channels = static_cast<uint16_t>(p.channels);
    f.bits_per_sample = static_cast<uint16_t>(p.bits);
    f.samples_per_sec = static_cast<uint32_t>(p.rate);
  } else {
    f.channels = 1;
    f.bits_per_sample = (st.caps & platform::SoundOutput::kCapsPrimary16Bit) ? 16 : 8;
    f.samples_per_sec = 22050;
  }
  f.block_align = static_cast<uint16_t>((f.bits_per_sample >> 3) * f.channels);
  f.avg_bytes_per_sec = static_cast<uint32_t>(f.block_align) * f.samples_per_sec;
  int32_t hr = out()->set_cooperative_level(st.window, platform::SoundOutput::kExclusive);
  if (hr != 0) return ds_error(hr, kFile, 0xf0);
  hr = out()->set_format(st.primary, f);
  if (hr != 0) return ds_error(hr, kFile, 0xf3);
  return out()->set_cooperative_level(st.window, platform::SoundOutput::kNormal);
}

// Mech3.exe 1.2 @ 0x00570410 (ECX: stop the line playing). Empties the voice
// queue.
void stop_queue(int32_t stop_playing) {
  State& st = g_state;
  if (stop_playing && st.queued && status(st.queued) != 0) {
    stop(st.queued);
    st.queue_next = st.queue_clock - -0.5f;
  }
  st.queued = nullptr;
  if (st.queue) {
    for (Queued* q : *st.queue) delete q;
    st.queue->clear();
  }
}

// Mech3.exe 1.2 @ 0x00570490: forgets the queued lines of one sound.
void drop_queued(Sound* sound) {
  State& st = g_state;
  if (st.queued && (st.queued->sound == nullptr || st.queued->sound == sound)) st.queued = nullptr;
  if (!st.queue) return;
  for (auto it = st.queue->begin(); it != st.queue->end();) {
    if ((*it)->sound == sound) {
      delete *it;
      it = st.queue->erase(it);
    } else {
      ++it;
    }
  }
}

int32_t shutdown() { return zgame::game().zsnd_shutdown(); }
int32_t load(uint32_t window, const char* name, LoadFn fn, int32_t demo) {
  return zgame::game().zsnd_load(window, name, fn, demo);
}

// Mech3.exe 1.2 @ 0x00570510. After close_device the device calls do nothing
// (they check the open flag).
int32_t mw3_shutdown() {
  State& st = g_state;
  stop_queue(0);
  delete st.queue;
  st.queue = nullptr;
  free_groups(nullptr);
  close_device(nullptr);  // 0x005715c0; ECX there is whatever 0x005742d0 left
  if (g_hooks.cd_close) g_hooks.cd_close();
  free_sets(nullptr);
  fades_shutdown();
  if (st.root) {
    zrdr::free_node(st.root);
    st.root = nullptr;
  }
  st.has_search = false, st.search.clear();
  st.has_primary_format = false;
  for (bool& b : st.has_playback) b = false;
  st.playback_current = -1;
  return 1;
}

// Mech3.exe 1.2 @ 0x005705e0 (ECX window, EDX file, the load callback, the
// demo flag). Opens DirectSound, reads the file, then its SYNTAX: 2 is the
// shipped one. The older syntax 1 reader (0x005706f0) is not ported; no data
// uses it (docs/sound.md).
int32_t mw3_load(uint32_t window, const char* name, LoadFn fn, int32_t demo) {
  State& st = g_state;
  if (st.open || !st.initialized) return 0;
  if (window) st.window = window;
  if (st.api == 0 && !open_device()) return 0;
  st.open = 1;
  st.root = zrdr::load(name);
  if (!st.root) {
    char msg[300];
    std::snprintf(msg, sizeof msg, "Failed to find menu sounds (%s)", name);
    zerr::report(0x200, kFile, 0x45a, msg);
    return 0;
  }
  int32_t syntax = 0;
  if (!zrdr::get_int(st.root, "SYNTAX", &syntax)) syntax = 1;
  if (syntax == 2) read_syntax2(st.root, fn, demo);  // syntax 1 reads nothing here
  return 1;
}

// Mech3.exe 1.2 @ 0x00570af0 (ECX the root, EDX the load callback, the demo
// flag): CD_TRACKS, SOUND_PATH, the default formats, SPEED_OF_SOUND,
// PLAYBACK_FORMAT, every SET (or only DEMO_SETS), then SOUND_GROUPS.
int32_t read_syntax2(zrdr::Node* root, LoadFn fn, int32_t demo) {
  State& st = g_state;
  Format defaults[3];
  defaults[0].rate = 44100, defaults[0].bits = 16, defaults[0].channels = 2;
  defaults[1].rate = 22050, defaults[1].bits = 16, defaults[1].channels = 1;
  defaults[2].rate = 11025, defaults[2].bits = 8, defaults[2].channels = 1;
  if (g_hooks.cd_open) g_hooks.cd_open(zrdr::find(root, "CD_TRACKS"));  // 0x00572450
  if (const char* sp = zrdr::get_string(root, "SOUND_PATH")) {
    if (!st.has_search) st.search = sp, st.has_search = true;  // 0x00576100
    else st.search = st.search + ";" + sp;                      // 0x00576140
  }
  static const char* const kDefaults[3] = {"HIGH_DEFAULT", "MEDIUM_DEFAULT", "LOW_DEFAULT"};
  for (int i = 0; i < 3; ++i)
    if (zrdr::Node* n = zrdr::find(root, kDefaults[i]))
      read_triple(n, &defaults[i].rate, &defaults[i].bits, &defaults[i].channels);
  float sos = 0.0f;
  if (zrdr::get_float(root, "SPEED_OF_SOUND", &sos)) set_speed_of_sound(sos);
  if (zrdr::Node* pf = zrdr::find(root, "PLAYBACK_FORMAT")) {
    read_playback(pf, "HIGH", 0);
    // FIX (bug): the exe asks for "MED", which never matches the data's "MEDIUM".
    read_playback(pf, zfix::options().bugs ? "MEDIUM" : "MED", 1);
    read_playback(pf, "LOW", 2);
  }
  zrdr::Node* demo_sets = nullptr;
  int32_t demo_count = 0;
  if (demo) {
    demo_sets = zrdr::find(root, "DEMO_SETS");
    if (demo_sets) demo_count = demo_sets->list[0].i - 1;
    else demo = 0;
  }
  zrdr::Node* sets = zrdr::find(root, "SETS");
  if (!sets) return 1;  // the exe dereferences it regardless
  zrdr::Node* items = sets->list;
  const int32_t pairs = (items[0].i - 1) / 2;
  for (int32_t k = 0; k < pairs; ++k) {
    zrdr::Node& name_node = items[1 + 2 * k];
    zrdr::Node& set_node = items[2 + 2 * k];
    if (demo) {
      bool listed = false;
      for (int32_t j = 1; j <= demo_count; ++j)
        if (std::strcmp(demo_sets->list[j].s, name_node.s) == 0) listed = true;
      if (!listed) continue;
    }
    int32_t n = set_node.list[0].i - 1;
    zrdr::Node* sub = zrdr::find(&set_node, "SUB_SET");
    auto* set = new Set;  // 0x0056f760
    if (sub) n -= 2;
    set->name = name_node.s;
    if (n > 0) set->sounds.resize(n);
    st.sets.push_back(set);
    if (sub) {  // 0x0056f9d0, 0x0056fbd0: sets defined later are not found
      const int32_t m = sub->list[0].i - 1;
      set->subsets.assign(m > 0 ? m : 0, nullptr);
      for (int32_t i = 1; i <= m; ++i) set->subsets[i - 1] = find_set(sub->list[i].s);
    }
    zrdr::Node* node = &set_node.list[1];
    zrdr::Node* const end = &set_node.list[set_node.list[0].i];
    int32_t i = 0;
    while (i < n && node < end) {  // the exe reads on past the list when it holds fewer sounds
      if (node->type != zrdr::List) {
        node += 2;  // a keyword and its value
        continue;
      }
      Sound& s = set->sounds[i];
      const zrdr::Node* e = node->list;
      s.kind = 0;
      s.name = e[1].s;
      s.file = e[2].s;
      s.flags = has(node, "3D") ? (s.flags | k3D) : (s.flags & ~k3D);
      s.flags = has(node, "LOOPED") ? (s.flags | kLooped) : (s.flags & ~kLooped);
      s.flags = has(node, "FREQUENCY") ? (s.flags | kFrequency) : (s.flags & ~kFrequency);
      s.flags = has(node, "HARDWARE") ? (s.flags | kHardware) : (s.flags & ~kHardware);
      s.flags = has(node, "PURGEABLE") ? (s.flags | kPurgeable) : (s.flags & ~kPurgeable);
      s.flags = has(node, "VOICE") ? (s.flags | kVoice) : (s.flags & ~kVoice);
      s.queue_life = 5.0f;
      if (zrdr::Node* q = zrdr::find(node, "QUEUE")) {
        s.flags |= kQueue;
        if (q->list[0].i > 1) s.queue_life = raw_float(q->list[1]);
      }
      s.volume = 1.0f;
      zrdr::get_float(node, "VOLUME", &s.volume);
      if (s.volume > 1.0f) s.volume = 1.0f;
      else if (!(s.volume >= 0.0f)) s.volume = 0.0f;
      s.a3d_dist = 1.0f;
      zrdr::get_float(node, "A3DDIST", &s.a3d_dist);
      if (zrdr::Node* r = zrdr::find(node, "RANGE")) {
        s.flags |= k3D;
        s.range_min = raw_float(r->list[1]);
        s.range_max = raw_float(r->list[2]);
      } else {
        s.range_min = 50.0f, s.range_max = 400.0f;
      }
      for (int f = 0; f < 3; ++f) s.formats[f] = defaults[f];
      read_format(zrdr::find(node, "HIGH"), s.formats[0]);
      read_format(zrdr::find(node, zfix::options().bugs ? "MEDIUM" : "MED"), s.formats[1]);  // FIX (bug) as above
      read_format(zrdr::find(node, "LOW"), s.formats[2]);
      s.freq_min = 20000.0f;
      s.freq_max = 90000.0f;
      s.subtitle = st.subtitle_hook ? st.subtitle_hook(node) : nullptr;
      s.user = fn ? fn(&s, node) : nullptr;
      ++i;
      node += 1;
    }
  }
  zrdr::Node* demo_groups = demo ? zrdr::find(root, "DEMO_GROUPS") : nullptr;
  if (zrdr::Node* g = zrdr::find(root, "SOUND_GROUPS")) read_groups(g, demo_groups, fn);
  return 1;
}

// Mech3.exe 1.2 @ 0x00571460: DirectSoundCreate, normal cooperation, the
// card's caps, the primary buffer and its format.
int32_t open_device() {
  State& st = g_state;
  int32_t hr = out()->create();
  if (hr != 0) return ds_error(hr, kFile, 0x3e0);
  hr = out()->set_cooperative_level(st.window, platform::SoundOutput::kNormal);
  if (hr != 0) return ds_error(hr, kFile, 0x3e3);
  hr = out()->get_caps(&st.caps);
  if (hr != 0) return ds_error(hr, kFile, 0x3e7);
  platform::BufferDesc desc;
  desc.flags = 0xc1;
  hr = out()->create_buffer(desc, &st.primary);
  if (hr != 0) return ds_error(hr, kFile, 0x403);
  st.has_primary_format = true;
  hr = out()->get_format(st.primary, &st.primary_format);
  if (hr != 0) return ds_error(hr, kFile, 0x417);
  return 1;
}

}  // namespace zsnd
