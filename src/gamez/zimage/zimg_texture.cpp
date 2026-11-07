#include "gamez/zimage/zimg_texture.h"
#include "frontend/core_critical/renderer/material_normals.hpp"

namespace mw34::materials {
thread_local SurfaceKey g_active_surface_key{};
thread_local bool g_has_active_surface_key{false};

void set_active_surface_key(const SurfaceKey &key) {
  g_active_surface_key = key;
  g_has_active_surface_key = true;
}

void clear_active_surface_key() {
  g_has_active_surface_key = false;
  g_active_surface_key = {};
}

bool get_active_surface_key(SurfaceKey *out) {
  if (g_has_active_surface_key && out) {
    *out = g_active_surface_key;
    return true;
  }
  return false;
}
} // namespace mw34::materials

#include <cstdio>
#include <cstring>
#include <list>
#include <string>

#include "gamez/zerror/zerr.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "platform/render3d.h"

namespace zimg {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zImage\\zimg_texture.cpp";
TextureTable g_table;
}  // namespace

TextureTable& textures() { return g_table; }

// Mech3.exe 1.2 @ 0x00518080. No bounds check.
TextureEntry* texture_at(int index) { return index == -1 ? nullptr : &g_table.entries[index]; }

// Mech3.exe 1.2 @ 0x00518160. Quirk kept: a count of 0 returns without clearing
// the table count. On a short read the original keeps the bytes it got in the
// table; the port leaves the table as it was.
int read_texture_directory(int count, std::FILE* f, const char* where) {
  if (count == 0) return 0;
  if (count > kMaxTextures) {
    zerr::report(0x100, kFile, 0x10d, "Too many textures for texture array size.");
    return -1;
  }
  static uint8_t buf[kMaxTextures * 0x28];
  if (std::fread(buf, static_cast<size_t>(count) * 0x28, 1, f) != 1) {
    zerr::report(0x200, kFile, 0x11b, "Error reading GameZ Texture directory data.");
    return -1;
  }
  // Ours (docs/modding.md, M3): each mip index -1 or a row of this table;
  // the exe made a pointer of any index (texture_at) and followed it later.
  for (int i = 0; i < count; ++i) {
    int32_t mip;
    std::memcpy(&mip, buf + i * 0x28 + 0x24, 4);
    if (mip < -1 || mip >= count) {
      std::printf("mw3: %s: its texture directory is broken (texture %d's mip %d of %d), not read\n",
                  where ? where : "gamez.zbd", i, mip, count);
      return -1;
    }
  }
  for (int i = 0; i < count; ++i) {
    const uint8_t* raw = buf + i * 0x28;
    TextureEntry& e = g_table.entries[i];
    e.image = e.surface = nullptr;
    std::memcpy(e.name, raw + 8, 20);
    std::memcpy(&e.state, raw + 0x1c, 4);
    int32_t mip;
    std::memcpy(&mip, raw + 0x24, 4);
    e.group = g_table.group;
    e.mip = texture_at(mip);
  }
  g_table.count = count;
  return count;
}

}  // namespace zimg

// ---------------------------------------------------------------- loading

namespace zimg {

namespace {
TextureHooks g_hooks;
TextureLoad g_load;

// 0x005ecb40: the default image's texels, columns of two alternating per row.
uint16_t g_default_pixels[64] = {
    0xf800, 0xf800, 0x03e0, 0x03e0, 0xf800, 0xf800, 0x03e0, 0x03e0,
    0x03e0, 0x03e0, 0xf800, 0xf800, 0x03e0, 0x03e0, 0xf800, 0xf800,
    0xf800, 0xf800, 0x03e0, 0x03e0, 0xf800, 0xf800, 0x03e0, 0x03e0,
    0x03e0, 0x03e0, 0xf800, 0xf800, 0x03e0, 0x03e0, 0xf800, 0xf800,
    0xf800, 0xf800, 0x03e0, 0x03e0, 0xf800, 0xf800, 0x03e0, 0x03e0,
    0x03e0, 0x03e0, 0xf800, 0xf800, 0x03e0, 0x03e0, 0xf800, 0xf800,
    0xf800, 0xf800, 0x03e0, 0x03e0, 0xf800, 0xf800, 0x03e0, 0x03e0,
    0x03e0, 0x03e0, 0xf800, 0xf800, 0x03e0, 0x03e0, 0xf800, 0xf800,
};
Texture g_default = [] {
  Texture t{};
  t.pixel_count = 64, t.width = 8, t.height = 8;
  t.flags = kTwoBytes | kNoAlpha;  // not owned: never freed
  t.pixels = g_default_pixels;
  return t;
}();

// 0x00519150 with no new extension: cut the name at its first '.' after the
// first '\' or '/'.
void cut_extension(char* s) {
  char* p = std::strchr(s, '\\');
  if (!p) p = std::strchr(s, '/');
  if (!p) p = s;
  if (char* dot = std::strchr(p, '.')) *dot = '\0';
}
}  // namespace

TextureHooks& texture_hooks() { return g_hooks; }
TextureLoad& texture_load() { return g_load; }
Texture& default_image() { return g_default; }

// Mech3.exe 1.2 @ 0x00518210. The entry at 0x005ecbf8: the default image; its
// record (+0x04) is made on Direct3D by 0x005184e0 (not ported: set by the
// front end, e.g. to zvid::default_texture()).
TextureEntry* default_entry() {
  static TextureEntry e{&g_default, nullptr, "DEFAULT_TEXTURE", 1, 0, nullptr};
  return &e;
}

// Mech3.exe 1.2 @ 0x00518220. Plain strcmp, unlike the libraries' _stricmp.
TextureEntry* find_texture(const char* name) {
  for (int32_t i = 0; i < g_table.count; ++i) {
    TextureEntry& e = g_table.entries[i];
    if (e.state != 0 && std::strcmp(e.name, name) == 0) return &e;
  }
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00518cc0
TextureEntry* free_texture_entry() {
  for (int32_t i = 0; i < g_table.count; ++i)
    if (g_table.entries[i].state == 0) return &g_table.entries[i];
  if (g_table.count > 0xfff) return nullptr;
  return &g_table.entries[g_table.count++];
}

// Mech3.exe 1.2 @ 0x00518c60. The name is looked up without its last
// extension (the exe cuts it in place and puts the dot back).
TextureEntry* add_texture(const char* name) {
  char cut[256];
  std::snprintf(cut, sizeof cut, "%s", name);
  if (char* dot = std::strrchr(cut, '.')) *dot = 0;
  if (TextureEntry* e = find_texture(cut)) return e;
  TextureEntry* e = free_texture_entry();
  if (!e) return default_entry();
  set_entry_name(name, e->name);
  e->state = 2;
  e->group = g_table.group;
  return e;
}

// Mech3.exe 1.2 @ 0x00519210. No bound on dst (the entry's 20 bytes).
void set_entry_name(const char* path, char* dst) {
  const char* base = std::strrchr(path, '\\');
  if (!base) base = std::strrchr(path, '/');
  if (!base) base = path;
  std::strcpy(dst, base);
  cut_extension(dst);
}

// Mech3.exe 1.2 @ 0x00518e50. Palettised textures are left alone.
void clear_transparent(Texture& t) {
  if (t.palette_count != 0) return;
  const int bpp = bytes_per_pixel(t);
  for (int32_t i = 0; i < t.pixel_count; ++i) {
    if (t.alpha[i] != 0) continue;
    if (bpp == 1) static_cast<uint8_t*>(t.pixels)[i] = 0;
    else if (bpp == 2) static_cast<uint16_t*>(t.pixels)[i] = 0;
    else if (bpp == 4) static_cast<uint32_t*>(t.pixels)[i] = 0;
  }
}

// Mech3.exe 1.2 @ 0x00518ee0. The list switch sticks even when neither has it.
Texture* fetch_image(const char* name) {
  Texture* t = find_in_list(g_load.list, name);
  if (!t) {
    g_load.list = (g_load.list + 1) % 2;
    t = find_in_list(g_load.list, name);
  }
  if (t && (t->flags & kAlpha) && t->alpha) clear_transparent(*t);
  return t;
}

// Mech3.exe 1.2 @ 0x00557ed0
void software_prepare(Texture& t) {
  t.log2_width = 0, t.log2_height = 0;
  for (int32_t w = t.width; w > 1; w >>= 1) ++t.log2_width;
  for (int32_t h = t.height; h > 1; h >>= 1) ++t.log2_height;
  t.mip_scale = 1.0f;
  t.span_shift = 20 - t.log2_width;
  t.span_mask_u = (1 << t.log2_width) - 1;
  t.span_mask_v = ((1 << t.log2_height) << 20) - 0x100000;
}

// Mech3.exe 1.2 @ 0x00519270. Only a name ending in "_1" (the first "_1" in
// it) has mips: its last digit counts up while the libraries have the name.
// A new name takes the next table slot (no bound check), keeping the slot's
// old group and record. Each mip's scale is the base image's width over its own.
void link_mips(TextureEntry& entry) {
  char name[0x40];
  std::strcpy(name, entry.name);
  char* m = std::strstr(name, "_1");
  if (!m || m[2] != '\0') return;
  TextureEntry* prev = &entry;
  Texture* img = nullptr;  // [esp+0x14]
  for (;;) {
    ++m[1];
    TextureEntry* e = find_texture(name);
    if (!e || e->state == 2) {
      img = fetch_image(name);
      if (!img) return;
      if (!e) {
        e = &g_table.entries[g_table.count++];
        set_entry_name(name, e->name);
      }
      e->state = 1;
      e->image = img;
      software_prepare(*img);
    } else if (!img) {
      // The original reads an unset stack slot here (a mip already loaded
      // before its base); the port takes that mip's own image. Not reached
      // with the shipped files (docs/drafts/bake-textures.md).
      img = static_cast<Texture*>(e->image);
    }
    prev->mip = e;
    prev = e;
    img->mip_scale = static_cast<float>(static_cast<Texture*>(entry.image)->width / img->width);
    e->mip = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x00518d10
TextureEntry* load_named(const char* name, int32_t group, int32_t free_pixels) {
  TextureEntry* e = free_texture_entry();
  if (!e) return default_entry();
  set_entry_name(name, e->name);  // 0x00519210
  e->state = 2;
  e->group = group;
  load_texture(e, free_pixels);
  return e;
}

// Mech3.exe 1.2 @ 0x00518f70. A name no library has gets the default image
// (and, on Direct3D, a record of its own made from it).
void load_texture(TextureEntry* entry, int32_t free_pixels) {
  if (!entry || (entry->state != 2 && entry->state != 3)) return;
  entry->image = fetch_image(entry->name);
  entry->mip = nullptr;
  if (!entry->image) {
    if (g_hooks.fallback) entry->image = g_hooks.fallback(entry->name);
    if (!entry->image) entry->image = &g_default;
  }
  link_mips(*entry);
  Texture& img = *static_cast<Texture*>(entry->image);
  const int32_t device = zrndr::device_fog().active;  // 0x0072f348
  if (!device || (g_hooks.projected && g_hooks.projected(entry))) {
    software_prepare(img);
    entry->state = 1;
    return;
  }
  // Creation and reload can throw; never leak one upload's identity into the
  // next.
  struct ActiveSurfaceScope {
    ~ActiveSurfaceScope() { mw34::materials::clear_active_surface_key(); }
  } active_surface_scope;
  mw34::materials::clear_active_surface_key();
  if (img.pixels && img.palette_count == 0 && (img.flags & kTwoBytes) &&
      img.width > 0 && img.height > 0 && img.pixel_count > 0 &&
      std::uint64_t(img.pixel_count) >=
          std::uint64_t(img.width) * std::uint64_t(img.height)) {
    const uint32_t format = (img.flags & kAlpha) == 0 ? 1 : img.alpha ? 3 : 2;
    const uint64_t hash = mw34::materials::normal_hash(
        img.pixels, static_cast<size_t>(img.width) * img.height * 2);
    const mw34::materials::SurfaceKey key{static_cast<uint32_t>(img.width),
                                          static_cast<uint32_t>(img.height),
                                          format, hash};
    mw34::materials::set_active_surface_key(key);
  }
  if (entry->state == 3) {
    if (g_hooks.reload)
      g_hooks.reload(entry->surface, &img);
    entry->state = 1;
    return;
  }
  if (!entry->surface) {
    // [0x0072f364] = 0x0057b130 (zvid_ddd3d).
    entry->surface = zvid::create_texture(entry->name, img, img.flags & kAlpha, img.stretch & 1,
                                          (img.stretch >> 1) & 1);
    if (free_pixels && device != 2) release_buffers(img);
    g_load.memory += 2 * img.pixel_count;
  }
  entry->state = 1;
}

// Mech3.exe 1.2 @ 0x00519080. Opens list 0's files around the walk; the
// count is read again each step, so mips appended on the way are visited.
void load_group(int32_t group) {
  open_all(0);
  for (int32_t i = 0; i < g_table.count; ++i) {
    TextureEntry& e = g_table.entries[i];
    if (group == -1 || group == e.group) load_texture(&e, 1);
  }
  close_all(0);
}

// Mech3.exe 1.2 @ 0x005190d0
int32_t load_group_0() {
  load_group(0);
  return 0;
}

namespace {
struct CacheEntry {  // 0x0c bytes (0x00517730): the name (strdup'd), the image, the uses
  std::string name;
  Texture* image = nullptr;
  int32_t refs = 1;
};
bool g_cache_made = false;           // [0x006384ec] made
std::list<CacheEntry> g_cache;
}  // namespace

// Mech3.exe 1.2 @ 0x005179d0 (ECX the name)
Texture* cache_fetch(const char* name) {
  if (!name) return nullptr;
  g_cache_made = true;
  for (CacheEntry& c : g_cache)  // 0x005177a0: by name
    if (c.name == name) return ++c.refs, c.image;
  Texture* t = fetch_image(name);  // 0x005176b0
  if (!t) return nullptr;
  g_cache.push_back({name, t, 1});
  return t;
}

// Mech3.exe 1.2 @ 0x00517b10 (ECX the image)
void cache_release(Texture* image) {
  if (!g_cache_made) {
    zerr::report(0x400, "C:\\zipper\\GameZMech3\\zImage\\zimg_library.cpp", 0xd0, "lazy programmer alert!");
    return;
  }
  for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {  // 0x00517840: by image
    if (it->image != image) continue;
    if (--it->refs == 0) {  // 0x00517790
      free_texture(image);  // 0x00517760 -> 0x005185f0
      g_cache.erase(it);
    }
    return;
  }
  free_texture(image);  // 0x005185f0
}

// Mech3.exe 1.2 @ 0x00518b30 (ECX the entry): a loaded one's image and
// record freed, a waiting one forgotten; others (reload) left as they are.
void entry_free(TextureEntry& e) {
  if (e.state == 1) {
    if (e.image) {
      if (e.image != &default_image()) free_texture(static_cast<Texture*>(e.image));  // 0x005185f0
      e.image = nullptr;
    }
    if (e.surface) {
      if (g_hooks.release) g_hooks.release(e.surface);
      e.surface = nullptr;
    }
  } else if (e.state != 2) {
    return;
  }
  e.state = 0;
}

// Mech3.exe 1.2 @ 0x00518b70. The default image's own record ([0x005ecbfc],
// on Direct3D) and the block at [0x00638518] are not kept by the port.
int32_t textures_free() {
  TextureTable& t = textures();
  for (int32_t i = 0; i < t.count; ++i) {
    entry_free(t.entries[i]);
    t.entries[i].group = 0;
  }
  t.count = 0;
  t.group = 0;
  if (g_hooks.release_all) g_hooks.release_all();
  if (auto* renderer = zvid::render3d()) renderer->reset_scene_resources();
  for (uint16_t*& p : state().palettes) {
    std::free(p);
    p = nullptr;
  }
  state().palettes.clear();
  free_all(0);
  close_all(1);
  return 0;
}

}  // namespace zimg
