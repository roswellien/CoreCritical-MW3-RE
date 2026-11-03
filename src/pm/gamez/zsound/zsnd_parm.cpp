// C:\zipper\branchep1\gamez\zSound\zsnd_parm.cpp's sets, Pirate's Moon's:
// kept in std::lists, found in any case, made by name when first named, and
// loaded and unloaded by count (docs/sound.md, Pirate's Moon).
#include <string>

#include "crt/msvc_string.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "pm/gamez/zsound/zsnd.h"

namespace pm::zsnd {

namespace {
using ::zsnd::State;
State& st() { return ::zsnd::state(); }
}  // namespace

// Pirate's Moon 1.0 @ 0x00590a20 (ECX the set list): _stricmp, where MW3's
// 0x0056f680 compares exactly.
Set* find_set(const char* name) {
  if (!name) return nullptr;
  for (Set* s : st().sets)
    if (crt::msvc_stricmp(s->name.c_str(), name) == 0) return s;
  return nullptr;
}

// Pirate's Moon 1.0 @ 0x00590ae0: a SETS entry or a SUB_SET name, so a set
// named twice is one set, and a subset read before its own entry is the set
// that entry fills (MW3 leaves such a subset null).
Set* make_set(const char* name) {
  if (Set* s = find_set(name)) return s;
  auto* s = new Set;  // 0x00590b90
  s->name = name;
  st().sets.push_back(s);
  return s;
}

// Pirate's Moon 1.0 @ 0x00590d70 (ECX set): as MW3's 0x0056fbf0 but counted:
// a set already loaded only counts one more (and returns 1). No null check.
int32_t load_set(Set* set) {
  static const char* const kArchives[3] = {"soundsH.zbd", "soundsM.zbd", "soundsL.zbd"};
  State& s = st();
  zutl_zar::Archive a;
  zutl_zar::init(a);  // 0x00597300
  if (++set->loaded != 1) {
    zutl_zar::destroy(a);
    return 1;
  }
  uint32_t idx = static_cast<uint32_t>(*s.lod);
  if (s.archives_on) {
    for (int tries = 0; tries < 3; ++tries) {
      const char* name = idx < 3 ? kArchives[idx] : nullptr;
      bool opened = false;
      if (name) {
        if (zutl::exists(name)) {  // 0x00596ea0
          opened = zutl_zar::open(a, name);
        } else {
          const std::string p = ::zsnd::path_find(name);
          if (!p.empty()) opened = zutl_zar::open(a, p.c_str());
        }
      }
      if (opened) {
        ::zsnd::load_from_archive(set, a);  // 0x00591220
        zutl_zar::close(a);
        break;
      }
      if (++idx >= 3) idx = 0;
    }
  }
  const int32_t q = ::zsnd::quality(static_cast<int32_t>(idx));
  ::zsnd::set_primary_format(&q);
  for (Sound& snd : set->sounds) {
    if (snd.flags & ::zsnd::kLoaded) continue;
    const std::string p = ::zsnd::path_find(snd.file);
    if (p.empty()) continue;
    ::zsnd::Wave w;
    w.path = p;
    ::zsnd::wave_read_file(w);
    if (w.valid) snd.flags = (snd.flags & ~::zsnd::kLoaded) | ((::zsnd::create_buffer(&snd, w) & 1) << 3);
    else snd.flags &= ~::zsnd::kLoaded;
  }
  for (Set* sub : set->subsets) ::zsnd::load_set(sub);  // 0x00590f60
  zutl_zar::destroy(a);  // 0x00597320
  return 1;
}

// Pirate's Moon 1.0 @ 0x00590f80 (ECX set): the last of its loads frees its
// sounds and unloads its subsets once each; an earlier one only counts down
// (and returns 0).
int32_t unload_set(Set* set) {
  if (set->loaded == 0) return 0;
  if (set->loaded != 1) {
    --set->loaded;
    return 0;
  }
  set->loaded = 0;
  for (Sound& snd : set->sounds) {  // 0x00591120
    ::zsnd::drop_queued(&snd);
    ::zsnd::release_sound(&snd);
  }
  for (Set* sub : set->subsets) ::zsnd::unload_set(sub);  // 0x00590f70
  return 1;
}

// Pirate's Moon 1.0 @ 0x00590950: one unload of each set (a set loaded twice
// stays loaded), while the device is open.
int32_t unload_all() {
  State& s = st();
  if (!s.open) return 0;
  for (Set* set : s.sets) ::zsnd::unload_set(set);
  return 1;
}

// Pirate's Moon 1.0 @ 0x00590990 (the user-data free function): with the
// device open, unload_all, then each set's user data freed (0x00591250) and
// the set destroyed (0x00590c10: one more unload, then its sounds freed,
// buffers or not).
void free_sets(FreeFn fn) {
  State& s = st();
  if (!::zsnd::unload_all()) return;
  for (Set* set : s.sets) {
    if (!set) continue;
    if (fn)
      for (Sound& snd : set->sounds)
        if (snd.user) fn(&snd, snd.user), snd.user = nullptr;
    ::zsnd::unload_set(set);
    delete set;
  }
  s.sets.clear();
}

}  // namespace pm::zsnd
