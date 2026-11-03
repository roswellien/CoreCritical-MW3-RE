// C:\zipper\GameZMech3\zSound\zsnd_parm.cpp: a voice's parameters, and the
// sound sets (docs/sound.md section 2). Only 0x0056f280 names the file; the
// functions after it up to zsnd_init.cpp (0x0056f370..0x005700a0: the set
// list, load and unload, find) have no string of their own and are kept here.
#include <cstring>

#include "gamez/zgame/zgame_table.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"

namespace zsnd {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zSound\\zsnd_parm.cpp";
platform::SoundOutput* out() { return hooks().output; }
WaveAliasHook g_alias = nullptr;
}  // namespace

// 0x005762b0 on the SOUND_PATH list (0x00576100): the file part of `name` in
// the first directory that has it. The list is LIFO, so the last directory of
// SOUND_PATH is searched first. FIX (A): through the mounts; the engine-wide
// default list ([0x0072eebc]) is not consulted (as in zinterp).
std::string path_find(const char* name) {
  State& st = state();
  if (!st.has_search || !name) return "";
  const char* base = name;
  for (const char* p = name; *p; ++p)
    if (*p == '\\' || *p == '/' || *p == ':') base = p + 1;
  std::vector<std::string> dirs;
  const std::string& s = st.search;
  size_t start = 0;
  while (start <= s.size()) {
    size_t end = s.find(';', start);
    if (end == std::string::npos) end = s.size();
    if (end > start) dirs.push_back(s.substr(start, end - start));
    start = end + 1;
  }
  for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
    std::string dir = *it;
    if (!dir.empty() && dir.back() == '\\') dir.pop_back();
    const std::string path = dir + "\\" + base;
    if (zutl::exists(path.c_str())) return path;
  }
  return "";
}

namespace {
// The alias hook's sound for `name`, when the archive lacks it and no loose
// file has it (ours; set_wave_alias_hook).
void read_alias(Wave& w, zutl_zar::Archive& a, const char* name) {
  WaveAlias alias;
  if (!g_alias || !name || !path_find(name).empty() || !g_alias(name, &alias)) return;
  Wave src;
  src.path = alias.source;
  if (!wave_read_archive(src, a) || !src.has_format || src.format.block_align == 0) return;
  const uint32_t cut = static_cast<uint32_t>(alias.trim_end_seconds * src.format.samples_per_sec + 0.5f) *
                       src.format.block_align;
  if (cut >= src.data_size) return;
  src.data_size -= cut;
  src.path = name;
  w = std::move(src);
}
}  // namespace

// 0x0056ffc0: every sound of the set not loaded yet, from the open archive.
int32_t load_from_archive(Set* set, zutl_zar::Archive& a) {
  for (Sound& s : set->sounds) {
    if (s.flags & kLoaded) continue;
    Wave w;
    w.path = s.file ? s.file : "";
    wave_read_archive(w, a);
    if (!w.valid) read_alias(w, a, s.file);
    if (w.valid) s.flags = (s.flags & ~kLoaded) | ((create_buffer(&s, w) & 1) << 3);
    else s.flags &= ~kLoaded;
  }
  return 1;
}

// Mech3.exe 1.2 @ 0x0056f280 (ECX voice): pitch between the sound's
// frequency range, t from 0 to 1.
int32_t set_frequency(VoiceBase* voice, float t) {
  if (voice->kind != 0) return -1;
  auto* v = static_cast<Voice*>(voice);
  if (v->sound->kind != 0) return -1;
  if (t > 1.0f) t = 1.0f;
  else if (t < 0.0f) t = 0.0f;
  if (!v->buffer) return -1;
  Sound* s = v->sound;
  const int32_t hz = static_cast<int32_t>((s->freq_max - s->freq_min) * t + s->freq_min);
  const int32_t hr = out()->set_frequency(v->buffer, static_cast<uint32_t>(hz));
  if (hr != 0) return ds_error(hr, kFile, 0xda);
  return 1;
}

// Mech3.exe 1.2 @ 0x0056f370 (ECX voice): the voice's own volume (times the
// master volume), then its 3D falloff again.
int32_t set_voice_volume(VoiceBase* voice, float v) {
  if (voice->kind != 0) return 0;
  auto* vc = static_cast<Voice*>(voice);
  vc->volume = volume_db(state().master() * v);
  return update_3d(vc, nullptr, nullptr, 0);
}

// Mech3.exe 1.2 @ 0x0056f3e0 (ECX sound, EDX function): called at each of
// the sound's cue points while it plays (the briefings).
void set_cue_fn(Object* obj, CueFn fn) {
  if (obj->kind == 0) static_cast<Sound*>(obj)->cue_fn = fn;
}

// Mech3.exe 1.2 @ 0x0056f3f0
int32_t lock(VoiceBase* voice) {
  if (voice && voice->locked == 0) {
    voice->locked = 1;
    return 1;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0056f410
int32_t unlock(VoiceBase* voice) {
  if (voice && voice->locked != 0) {
    voice->locked = 0;
    return 1;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0056f430
void* user_data(Object* obj) {
  if (obj->kind == 0) return static_cast<Sound*>(obj)->user;
  if (obj->kind == 1) return static_cast<Group*>(obj)->extra;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0056f4b0: 0 skips the archives (loose files only).
void set_archives(int32_t on) { state().archives_on = on; }

// Mech3.exe 1.2 @ 0x0056f520 (PM's 0x005908f0). A name no set has does
// nothing: MW3's load_set takes the null, PM's (no null check) is not called.
void load_set(const char* name) {
  if (Set* s = find_set(name)) load_set(s);
}

// Mech3.exe 1.2 @ 0x0056f530 (PM's 0x00590920)
void unload_set(const char* name) {
  if (Set* s = find_set(name)) unload_set(s);
}

int32_t unload_all() { return zgame::game().zsnd_unload_all(); }
void free_sets(FreeFn fn) { zgame::game().zsnd_free_sets(fn); }
Set* find_set(const char* name) { return zgame::game().zsnd_find_set(name); }
int32_t load_set(Set* set) { return zgame::game().zsnd_load_set(set); }
int32_t unload_set(Set* set) { return zgame::game().zsnd_unload_set(set); }

// Mech3.exe 1.2 @ 0x0056f540
int32_t mw3_unload_all() {
  for (Set* s : state().sets) unload_set(s);
  return 1;
}

// Mech3.exe 1.2 @ 0x0056f5c0 (ECX function for each sound's user data),
// with 0x005700a0 and 0x0056f990.
void mw3_free_sets(FreeFn fn) {
  State& st = state();
  unload_all();
  for (Set* set : st.sets) {
    if (!set) continue;
    if (fn) {
      for (Sound& s : set->sounds) {
        if (s.user) fn(&s, s.user), s.user = nullptr;
      }
    }
    unload_set(set);
    delete set;
  }
  st.sets.clear();
}

// Mech3.exe 1.2 @ 0x0056f630
Set* set_at(int32_t i) {
  const std::vector<Set*>& v = state().sets;
  if (i < 0 || static_cast<size_t>(i) >= v.size()) return nullptr;
  return v[i];
}

// Mech3.exe 1.2 @ 0x0056f660
int32_t set_count() { return static_cast<int32_t>(state().sets.size()); }

// Mech3.exe 1.2 @ 0x0056f680
Set* mw3_find_set(const char* name) {
  for (Set* s : state().sets)
    if (s->name == name) return s;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0056f6f0: a loaded sound of any set, else a group.
Object* find(const char* name) {
  State& st = state();
  if (!st.open || !name) return nullptr;
  for (Set* s : st.sets)
    if (Sound* found = find_in_set(s, name)) return found;
  return find_group(name);
}

// Mech3.exe 1.2 @ 0x0056f740
const char* name_of(Object* obj) {
  if (!obj) return nullptr;
  if (obj->kind == 0) return static_cast<Sound*>(obj)->name;
  if (obj->kind == 1) return static_cast<Group*>(obj)->name;
  return nullptr;
}

void set_wave_alias_hook(WaveAliasHook hook) { g_alias = hook; }

// Mech3.exe 1.2 @ 0x0056fbf0. The archive of the detail setting (SoundLOD),
// or the next one that opens: soundsM.zbd is not shipped, so medium loads
// soundsL.zbd. The primary buffer takes that archive's playback format. Sounds
// still missing are looked for as loose files, then the subsets load.
int32_t mw3_load_set(Set* set) {
  static const char* const kArchives[3] = {"soundsH.zbd", "soundsM.zbd", "soundsL.zbd"};
  State& st = state();
  zutl_zar::Archive a;
  zutl_zar::init(a);
  if (!set || set->loaded) {
    zutl_zar::destroy(a);
    return 0;
  }
  uint32_t idx = static_cast<uint32_t>(*st.lod);
  bool opened = false;
  if (st.archives_on) {
    for (int tries = 0; tries < 3; ++tries) {
      const char* name = idx < 3 ? kArchives[idx] : nullptr;
      if (name) {
        if (zutl::exists(name)) {  // 0x00576080, _access in the working directory
          opened = zutl_zar::open(a, name);
        } else {
          const std::string p = path_find(name);
          if (!p.empty()) opened = zutl_zar::open(a, p.c_str());
        }
      }
      if (opened) break;
      if (++idx >= 3) idx = 0;
    }
    if (opened) {
      load_from_archive(set, a);
      zutl_zar::close(a);
    }
  }
  const int32_t q = quality(static_cast<int32_t>(idx));
  set_primary_format(&q);
  for (Sound& s : set->sounds) {
    if (s.flags & kLoaded) continue;
    const std::string p = path_find(s.file);
    if (p.empty()) continue;
    Wave w;
    w.path = p;
    wave_read_file(w);
    if (w.valid) s.flags = (s.flags & ~kLoaded) | ((create_buffer(&s, w) & 1) << 3);
    else s.flags &= ~kLoaded;
  }
  for (Set* sub : set->subsets) load_set(sub);
  set->loaded = 1;
  zutl_zar::destroy(a);
  return 1;
}

// Mech3.exe 1.2 @ 0x0056fe00. Unloads the subsets too, even when another
// loaded set shares them.
int32_t mw3_unload_set(Set* set) {
  if (!set || !set->loaded) return 0;
  for (Sound& s : set->sounds) {
    drop_queued(&s);
    release_sound(&s);
  }
  for (Set* sub : set->subsets) unload_set(sub);
  set->loaded = 0;
  return 1;
}

// Mech3.exe 1.2 @ 0x0056fe70
Sound* set_sound(Set* set, int32_t i) {
  if (set && i < static_cast<int32_t>(set->sounds.size())) return &set->sounds[i];
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0056fea0: by name among the set's loaded sounds, then
// its subsets.
Sound* find_in_set(Set* set, const char* name) {
  if (!set) return nullptr;
  for (Sound& s : set->sounds)
    if (s.name && std::strcmp(name, s.name) == 0 && s.voice.buffer) return &s;
  for (Set* sub : set->subsets)
    if (Sound* found = find_in_set(sub, name)) return found;
  return nullptr;
}

}  // namespace zsnd
