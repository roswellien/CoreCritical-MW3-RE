#include "gamez/zmodel/gmod_const.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "crt/msvc_math.h"
#include "gamez/zclass/cls_node.h"
#include "crt/msvc_math.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zvideo/zvid_format.h"

#pragma STDC FP_CONTRACT OFF

namespace zmod {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zModel\\gmod_const.c";
ModelPool g_pool;

template <typename T>
T* read_array(std::FILE* f, uint32_t count, uint32_t size, bool* ok) {
  auto* p = static_cast<T*>(std::malloc(static_cast<size_t>(count) * size));
  *ok = std::fread(p, static_cast<size_t>(count) * size, 1, f) == 1;
  return p;
}
}  // namespace

ModelPool& models() { return g_pool; }

uint32_t Model::count(int off) const {
  uint32_t v;
  std::memcpy(&v, header + off, 4);
  return v;
}

// Mech3.exe 1.2 @ 0x005380c0. The exe has no upper bound; past the pool is null here.
Model* model_at(int32_t index) {
  if (index < 0 || index >= static_cast<int32_t>(g_pool.slots.size())) return nullptr;
  return &g_pool.slots[index];
}

// Mech3.exe 1.2 @ 0x00538640 (ECX the file at the models' table, EDX the
// index): one model read again into a new slot, its arrays from the data
// offset its slot records (+0x5c). A failed read gives the slot back when
// the file's use count is 0 (else it stays taken, half read).
Model* reread_model(std::FILE* f, int32_t index) {
  int32_t size, count, free_head;
  int line = 0;
  if (std::fread(&size, 4, 1, f) != 1) line = 0x323;
  else if (std::fread(&count, 4, 1, f) != 1) line = 0x330;
  else if (std::fread(&free_head, 4, 1, f) != 1) line = 0x33d;
  if (line) {
    zerr::report(0x200, kFile, line, "Error reading GameZ Model3D buffer data.");
    zerr::report(0x200, kFile, 0x4b9, "Error reading GameZ Model3D buffer data.");
    return nullptr;
  }
  if (size == 0 || size <= index) return nullptr;
  std::fseek(f, static_cast<long>(index) * 0x60, SEEK_CUR);
  uint8_t raw[0x60];
  if (std::fread(raw, sizeof raw, 1, f) != 1) {
    zerr::report(0x200, kFile, 0x4d2, "Error reading GameZ Model3D buffer data.");
    return nullptr;
  }
  Model* m = model_alloc();
  std::memcpy(m->header, raw, 0x5c);
  uint32_t offset;
  std::memcpy(&offset, raw + 0x5c, 4);
  std::fseek(f, static_cast<long>(offset), SEEK_SET);
  if (read_model_data(f, *m) == 0) return m;  // 0x00553450's edge pass: not ported (one video mode)
  model_release(m);  // 1 (kept) when the file's use count is not 0
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00526800 (the pool's part; the draw table and lighting
// settings are the caller's).
void reset_pool() {
  ModelPool& p = g_pool;
  if (p.size == 0) p.size = 0x6d6;
  p.slots.assign(p.size, Model{});
  for (int32_t i = 0; i < p.size; ++i) p.slots[i].link = static_cast<uint32_t>(i + 1 < p.size ? i + 1 : -1);
  p.free_head = 0;
  p.count = 0;
}

// Mech3.exe 1.2 @ 0x00538fe0
Model* model_alloc() {
  ModelPool& p = g_pool;
  if (p.free_head < 0) zcls::gamez_abort("GameZ model resource failure: restarting your game usually solves this.");
  Model* m = &p.slots[p.free_head];
  ++p.count;
  p.free_head = static_cast<int32_t>(m->link);
  const uint32_t link = m->link;
  *m = Model{};
  m->link = link;
  const uint32_t flags = 3;  // (+8 & ~0x10) | 3 on the cleared header
  std::memcpy(m->header + 8, &flags, 4);
  return m;
}

// Mech3.exe 1.2 @ 0x00538a70. Every read error returns -1, which the caller
// ignores.
int read_model_data(std::FILE* f, Model& m) {
  m.invalidate_render_geometry();
  bool ok = true;
  const uint32_t polys = m.count(0x10), verts = m.count(0x14), norms = m.count(0x18);
  const uint32_t morphs = m.count(0x1c), lights = m.count(0x20);
  if (static_cast<int32_t>(verts) > 0 && (m.vertices = read_array<float>(f, verts, 12, &ok), !ok)) return -1;
  if (static_cast<int32_t>(norms) > 0 && (m.normals = read_array<float>(f, norms, 12, &ok), !ok)) return -1;
  if (static_cast<int32_t>(morphs) > 0 && (m.morphs = read_array<float>(f, morphs, 12, &ok), !ok)) return -1;
  if (static_cast<int32_t>(lights) > 0) {
    m.lights = static_cast<Light*>(std::calloc(lights, sizeof(Light)));
    for (uint32_t i = 0; i < lights; ++i)
      if (std::fread(m.lights[i].raw, 0x4c, 1, f) != 1) return -1;
    for (uint32_t i = 0; i < lights; ++i) {
      Light& l = m.lights[i];
      float rgb[3];
      std::memcpy(rgb, l.raw + 0x1c, 12);
      const uint8_t b = static_cast<uint8_t>(crt::msvc_ftol(rgb[2] + 0.5f));  // fsub of -0.5 @ 0x005ce178: rounds
      const uint8_t g = static_cast<uint8_t>(crt::msvc_ftol(rgb[1] + 0.5f));
      const uint8_t r = static_cast<uint8_t>(crt::msvc_ftol(rgb[0] + 0.5f));
      l.color = zvid::pack_bytes(r, g, b);
      int32_t n;
      std::memcpy(&n, l.raw + 0x0c, 4);
      if (n > 0 && (l.vertices = read_array<float>(f, n, 12, &ok), !ok)) {
        zerr::report(0x200, kFile, 0, "Error reading GameZ Model3D point light data.");
        return -1;
      }
    }
  }
  if (static_cast<int32_t>(polys) <= 0) return 0;
  std::vector<uint8_t> raw(static_cast<size_t>(polys) * 0x24);
  if (std::fread(raw.data(), raw.size(), 1, f) != 1) return -1;
  m.polygons = new Polygon[polys]{};
  for (uint32_t i = 0; i < polys; ++i) {
    const uint8_t* p = raw.data() + i * 0x24;
    Polygon& q = m.polygons[i];
    int32_t index;
    std::memcpy(&q.flags, p, 4), std::memcpy(&q.priority, p + 4, 4);
    std::memcpy(&index, p + 0x1c, 4), std::memcpy(&q.zone_set, p + 0x20, 4);
    q.material = take_material(index);
  }
  for (uint32_t i = 0; i < polys; ++i) {
    Polygon& q = m.polygons[i];
    const uint32_t k = q.flags & 0xff;
    if (k) {
      if ((q.vertex_indices = read_array<uint32_t>(f, k, 4, &ok), !ok)) return -1;
      if ((q.flags & 0x200) && (q.normal_indices = read_array<uint32_t>(f, k, 4, &ok), !ok)) return -1;
    }
    if (q.material && (q.material->flags & 1) && (q.uvs = read_array<float>(f, k, 8, &ok), !ok)) return -1;
    if ((q.colors = read_array<float>(f, k, 12, &ok), !ok)) return -1;
    q.scratch = static_cast<uint32_t*>(std::malloc(k * 4));
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x00538e30. FIX (A): a vector of slots; the data arrays are
// read in slot order, free slots having no counts.
int read_models(std::FILE* f) {
  ModelPool& p = g_pool;
  int32_t size, count, free_head;
  if (std::fread(&size, 4, 1, f) != 1 || std::fread(&count, 4, 1, f) != 1 ||
      std::fread(&free_head, 4, 1, f) != 1) {
    zerr::report(0x200, kFile, 0, "Error reading GameZ Model3D buffer data.");
    return -1;
  }
  p.size = size, p.count = count, p.free_head = free_head;
  if (size == 0) return 0;
  std::vector<uint8_t> raw(static_cast<size_t>(size) * 0x60);
  if (std::fread(raw.data(), raw.size(), 1, f) != 1) {
    zerr::report(0x200, kFile, 0, "Error reading GameZ Model3D buffer data.");
    return -1;
  }
  p.slots.assign(size, Model{});
  for (int i = 0; i < size; ++i) {
    std::memcpy(p.slots[i].header, raw.data() + i * 0x60, 0x5c);
    std::memcpy(&p.slots[i].link, raw.data() + i * 0x60 + 0x5c, 4);
  }
  for (int i = 0; i < size; ++i) read_model_data(f, p.slots[i]);
  return size;
}

// Mech3.exe 1.2 @ 0x00539150. 1 while a node still uses it. The polygons'
// arrays, the vertices, normals, morphs and lights are freed, the header
// zeroed and the slot put back on the pool's free list.
int model_release(Model* m) {
  if (!m) return 5;
  if (m->count(0xc) != 0) return 1;
  m->invalidate_render_geometry();
  for (uint32_t i = 0; m->polygons && i < m->count(0x10); ++i) {
    Polygon& q = m->polygons[i];
    std::free(q.vertex_indices), std::free(q.normal_indices), std::free(q.uvs);
    std::free(q.colors), std::free(q.scratch);
    q.vertex_indices = q.normal_indices = q.scratch = nullptr;
    q.uvs = q.colors = nullptr;
  }
  std::memset(m->header + 0x10, 0, 4);
  delete[] m->polygons;
  std::free(m->vertices), std::free(m->normals), std::free(m->morphs);
  for (uint32_t i = 0; m->lights && i < m->count(0x20); ++i) std::free(m->lights[i].vertices);
  std::free(m->lights);
  m->polygons = nullptr, m->vertices = m->normals = m->morphs = nullptr, m->lights = nullptr;
  std::memset(m->header, 0, sizeof m->header);
  const int32_t index = static_cast<int32_t>(m - g_pool.slots.data());
  m->link = static_cast<uint32_t>(g_pool.free_head);
  --g_pool.count;
  g_pool.free_head = index;
  return 0;
}

// Mech3.exe 1.2 @ 0x005394c0 (ECX models, EDX materials): the mission's end
// that keeps the world: every model nothing holds (+0x0c 0, not an empty
// slot) released as 0x00539150 does, then the materials no model uses
// (0x00537e50).
void models_purge(bool models, bool materials) {
  if (models) models_free_unused();
  if (materials) materials_purge();
}

// Mech3.exe 1.2 @ 0x005392c0: every model nothing holds (+0x0c 0, not an
// empty slot) released as 0x00539150 does; their count. Instant Action's
// next wave (0x0048f740) frees the last wave's mechs' models with it.
int32_t models_free_unused() {
  static const uint8_t kEmpty[sizeof(Model::header)] = {};
  int32_t n = 0;
  for (int32_t i = 0; i < g_pool.size && i < static_cast<int32_t>(g_pool.slots.size()); ++i) {
    Model& m = g_pool.slots[static_cast<size_t>(i)];
    if (m.count(0xc) == 0 && std::memcmp(m.header, kEmpty, sizeof kEmpty) != 0) model_release(&m), ++n;
  }
  return n;
}

namespace {
template <typename T>
T* dup(const T* src, size_t count) {
  auto* p = static_cast<T*>(std::malloc(count * sizeof(T)));
  if (count) std::memcpy(p, src, count * sizeof(T));
  return p;
}
void put32(uint8_t* h, int off, uint32_t v) { std::memcpy(h + off, &v, 4); }
}  // namespace

// Mech3.exe 1.2 @ 0x005396c0. Flag bits 0..4 come from m (the rest stay the
// pool's 3, which has none), then the type, mode, +0x24..+0x30, the lights
// with their points, morphs, vertices, normals and polygons; UVs only where
// the (copied) material is textured.
Model* model_copy(const Model& m, int32_t own_materials, int32_t shared) {
  Model* c = model_alloc();
  uint8_t* h = c->header;
  std::memcpy(h, m.header, 8);  // +0 type, +4 mode
  put32(h, 0xc, 0);
  put32(h, 8, (m.count(8) & 0x1f) | (c->count(8) & ~0x1fu));
  std::memcpy(h + 0x24, m.header + 0x24, 0x10);
  const uint32_t lights = m.count(0x20);
  put32(h, 0x20, lights);
  if (lights) {
    c->lights = static_cast<Light*>(std::malloc(lights * sizeof(Light)));
    for (uint32_t i = 0; i < lights; ++i) {
      c->lights[i] = m.lights[i];
      int32_t n;
      std::memcpy(&n, c->lights[i].raw + 0xc, 4);
      c->lights[i].vertices = dup(m.lights[i].vertices, n > 0 ? 3 * static_cast<size_t>(n) : 0);
    }
  }
  const int32_t morphs = static_cast<int32_t>(m.count(0x1c)), verts = static_cast<int32_t>(m.count(0x14));
  const int32_t norms = static_cast<int32_t>(m.count(0x18)), polys = static_cast<int32_t>(m.count(0x10));
  if (morphs > 0) c->morphs = dup(m.morphs, 3 * static_cast<size_t>(morphs));
  put32(h, 0x1c, m.count(0x1c));
  if (verts > 0) c->vertices = dup(m.vertices, 3 * static_cast<size_t>(verts));
  put32(h, 0x14, m.count(0x14));
  if (norms > 0) c->normals = dup(m.normals, 3 * static_cast<size_t>(norms));
  put32(h, 0x18, m.count(0x18));
  if (polys > 0) c->polygons = new Polygon[polys]{};
  put32(h, 0x10, m.count(0x10));
  std::vector<std::pair<Material*, Material*>> made;  // this copy's material copies
  for (int32_t i = 0; i < polys; ++i) {
    const Polygon& s = m.polygons[i];
    Polygon& d = c->polygons[i];
    d.priority = s.priority;
    d.flags = s.flags & 0x700;
    d.zone_set = s.zone_set;
    d.material_index = s.material_index;
    d.material = s.material;
    if (own_materials && (!shared || material_changes(*s.material))) {
      Material* copy = nullptr;
      for (const auto& p : made)
        if (p.first == s.material) copy = p.second;
      if (!copy) {
        copy = material_copy(*s.material);
        made.emplace_back(s.material, copy);
      }
      d.material = copy;
    }
    const uint32_t k = s.flags & 0xff;
    if (k) {
      d.vertex_indices = dup(s.vertex_indices, k);
      if ((s.flags & 0x200) && s.normal_indices) d.normal_indices = dup(s.normal_indices, k);
    }
    d.flags = (d.flags & ~0xffu) | k;
    if (d.material->flags & 1) d.uvs = dup(s.uvs, 2 * static_cast<size_t>(k));
    d.colors = dup(s.colors, 3 * static_cast<size_t>(k));
    d.scratch = static_cast<uint32_t*>(std::malloc(k * 4));
  }
  return c;
}

// Mech3.exe 1.2 @ 0x0053b3c0
int model_changes(const Model& m) {
  if (m.count(8) & 0x1c) return 1;
  for (uint32_t i = 0; i < m.count(0x10); ++i)
    if (material_changes(*m.polygons[i].material)) return 1;
  return 0;
}

// Mech3.exe 1.2 @ 0x0053bc10
void cycle_reset(Model* m) {
  Material* mat = m->polygons[0].material;
  Cycle* c = mat->cycle;
  if (!c) return;
  c->index = 0.0f;
  mat->texture = c->textures[0];
}

// Mech3.exe 1.2 @ 0x0053bc90. A frame past the end wraps by the count.
void cycle_set_frame(Model* m, int32_t n) {
  Material* mat = m->polygons[0].material;
  Cycle* c = mat->cycle;
  if (!c) return;
  const int32_t count = static_cast<int32_t>(c->texture_count);
  if (n >= count) n = n % count;
  else if (n < 0) n = 0;
  mat->texture = c->textures[n];
  c->index = static_cast<float>(n);
}

// Mech3.exe 1.2 @ 0x0053bc30. A null model reports and gives -1.
int cycle_set_on(Model* m, int32_t count) {
  if (!m) {
    std::fprintf(stderr, "%s(%d): ERROR setting model cycle texture. Model 3D pointer is NULL.\n",
                 "C:\\zipper\\GameZMech3\\zModel\\gmod_const.c", 0xedb);
    return -1;
  }
  Material* mat = m->polygons[0].material;
  // FIX (bug): without a material the exe clears flag 0x400 at address 0.
  if (mat) cycle_create(*mat, count);
  return 0;
}

// Mech3.exe 1.2 @ 0x0053bcd0
int cycle_set_looping(Model* m, uint32_t on) { return m ? cycle_set_loop(m->polygons[0].material, on) : 0; }

// Mech3.exe 1.2 @ 0x0053bcf0
int cycle_set_speed(Model* m, float speed) { return m ? zmod::cycle_set_speed(m->polygons[0].material, speed) : 0; }

// Mech3.exe 1.2 @ 0x0053bd10
int cycle_set_map(Model* m, zimg::TextureEntry* t) { return m ? cycle_add(*m->polygons[0].material, t) : 0; }

namespace {
// One point into the box. The x87 compares differ per side, so a NaN
// coordinate lands only on some (0x0053b733..0x0053b7d2 for the lights).
void grow_light(float* b, const float* p) {
  if (!(p[0] >= b[0])) b[0] = p[0];
  if (!(b[3] >= p[0])) b[3] = p[0];
  if (b[1] > p[1]) b[1] = p[1];
  if (!(b[4] >= p[1])) b[4] = p[1];
  if (b[2] > p[2]) b[2] = p[2];
  if (!(b[5] >= p[2])) b[5] = p[2];
}
// The vertices and the morphed ones (0x0053b80d.., 0x0053b8c8..).
void grow_vertex(float* b, const float* p) {
  if (!(p[0] >= b[0])) b[0] = p[0];
  if (p[0] > b[3]) b[3] = p[0];
  if (b[1] > p[1]) b[1] = p[1];
  if (!(b[4] >= p[1])) b[4] = p[1];
  if (b[2] > p[2]) b[2] = p[2];
  if (!(b[5] >= p[2])) b[5] = p[2];
}
int32_t hdr_i(const uint8_t* h, int off) {
  int32_t v;
  std::memcpy(&v, h + off, 4);
  return v;
}
void hdr_f(uint8_t* h, int off, float v) { std::memcpy(h + off, &v, 4); }

// Mech3.exe 1.2 @ 0x0053b680. Starts from the first vertex, else the first
// light's first point.
void model_extent(const uint8_t* h, const float* vertices, const Light* lights, const float* morphs, float* b) {
  const int32_t nv = hdr_i(h, 0x14), nl = lights ? hdr_i(h, 0x20) : 0, nm = hdr_i(h, 0x1c);
  const float* first = nv >= 1 ? vertices : nl >= 1 ? lights[0].vertices : nullptr;
  if (first) {
    b[0] = b[3] = first[0];
    b[1] = b[4] = first[1];
    b[2] = b[5] = first[2];
  }
  for (int32_t i = 0; i < nl; ++i) {
    const Light& l = lights[i];
    const int32_t n = hdr_i(l.raw, 0xc);
    for (int32_t j = 0; j < n; ++j) grow_light(b, l.vertices + 3 * j);
  }
  for (int32_t i = 1; i < nv; ++i) grow_vertex(b, vertices + 3 * i);
  if (nm != 0 && morphs) {
    std::vector<zmth::Vec3> out(static_cast<uint32_t>(nm));  // the scratch at [0x0074a288]
    zmth::morph(out.data(), reinterpret_cast<const zmth::Vec3*>(vertices),
                reinterpret_cast<const zmth::Vec3*>(morphs), static_cast<uint32_t>(nm), 1.0f);
    for (int32_t i = 0; i < nm; ++i) grow_vertex(b, &out[i].x);
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x0053b4e0. Type 0 keeps the extent; type 1 (a facade)
// takes the larger of each axis's ends: mode 0 a box as wide in z as in x,
// otherwise a cube. Other types keep the box as given.
void model_box(uint8_t* h, const float* vertices, const Light* lights, const float* morphs, float box[6]) {
  if (!h || !box) return;
  const int32_t type = hdr_i(h, 0);
  if (type == 0) {
    model_extent(h, vertices, lights, morphs, box);
  } else if (type == 1) {
    model_extent(h, vertices, lights, morphs, box);
    float x = box[3], y = box[4], z = box[5];
    if (std::fabs(box[0]) > x) x = std::fabs(box[0]);
    if (std::fabs(box[1]) > y) y = std::fabs(box[1]);
    if (std::fabs(box[2]) > z) z = std::fabs(box[2]);
    if (hdr_i(h, 4) != 0) {
      float m = x;
      if (y > m) m = y;
      if (z > m) m = z;
      x = y = z = m;
    } else {
      if (x > z) z = x;
      else x = z;
    }
    box[0] = -x, box[1] = -y, box[2] = -z;
    box[3] = x, box[4] = y, box[5] = z;
  }
  const float hx = (box[3] - box[0]) * 0.5f, hy = (box[4] - box[1]) * 0.5f, hz = (box[5] - box[2]) * 0.5f;
  hdr_f(h, 0x48, hx + box[0]);
  hdr_f(h, 0x4c, hy + box[1]);
  hdr_f(h, 0x50, hz + box[2]);
  const float r2 = (hx * hx + hy * hy) + hz * hz;
  uint32_t bits;
  std::memcpy(&bits, &r2, 4);
  bits = static_cast<uint32_t>((static_cast<int32_t>(bits) >> 1) + 0x1fc00000);
  std::memcpy(h + 0x54, &bits, 4);
}

// --- Adding polygons (craters, docs/craters.md section 5) -----------------

namespace {
float g_precision = 0.001f;                        // 0x005ee3e0
double g_planar = 0.005;                           // 0x005ee3d0  SetCoplanarTolerance (0.002 in play)
double g_colinear = 0.001;                         // 0x005ee3d8  SetColinearTolerance (0.00001 in play)
constexpr int32_t kMaxVerts = 48;                  // 0x005ee3e4
constexpr double kMaxModelVerts = 921.6;           // 0x005ce180, 0.9 x 1024

// The exe's "equal to 0" tests (fcom, test ah, 0x40) take NaN as 0.
bool zeroish(float v) { return !(v < 0.0f || v > 0.0f); }

void put_count(Model& m, int off, int32_t v) { std::memcpy(m.header + off, &v, 4); }
int32_t count_at(const Model& m, int off) { return static_cast<int32_t>(m.count(off)); }

// 0x0050da70: zGeometry's report of the shared text buffer (0x0073f780).
void report_buffer(int level, const std::string& text) { zerr::report(level, "C:\\zipper\\GameZMech3\\zGeometry\\zgeo_model.cpp", 0x23, text.c_str()); }
std::string format(const char* f, ...) {
  char text[0x400];
  va_list ap;
  va_start(ap, f);
  std::vsnprintf(text, sizeof text, f, ap);
  va_end(ap);
  return text;
}
}  // namespace

float vertex_precision() { return g_precision; }      // 0x00538050
void set_vertex_precision(float v) { g_precision = v; }  // 0x00538060
// Mech3.exe 1.2 @ 0x00538070, 0x00538080: the support scripts' (tolerances.gw).
void set_coplanar_tolerance(float v) { g_planar = v; }
void set_colinear_tolerance(float v) { g_colinear = v; }
void tolerances_reset() { g_planar = 0.005, g_colinear = 0.001; }

// Mech3.exe 1.2 @ 0x00539d80. The vertex within the precision on every axis,
// else a new one (-1 when the model is near full).
int32_t find_vertex(Model& m, const float p[3]) {
  const int32_t n = count_at(m, 0x14);
  for (int32_t i = 0; i < n; ++i) {
    const float* v = m.vertices + 3 * i;
    if (std::fabs(v[0] - p[0]) <= g_precision && std::fabs(v[1] - p[1]) <= g_precision &&
        std::fabs(v[2] - p[2]) <= g_precision)
      return i;
  }
  if (kMaxModelVerts < n) {
    report_buffer(1, format("%s: Line %d: WARNING: Model vertex count = %d\n         Approaching max allowable: %d\n",
                            kFile, 0x816, n, 0x400));
    return -1;
  }
  m.vertices = static_cast<float*>(std::realloc(m.vertices, 12 * static_cast<size_t>(n + 1)));
  std::memcpy(m.vertices + 3 * n, p, 12);
  put_count(m, 0x14, n + 1);
  return n;
}

// Mech3.exe 1.2 @ 0x00539ec0. With morphs: the vertex equal to p whose
// morph moves it to `to` exactly, else a new one (its morph `to` - p).
int32_t find_morph_vertex(Model& m, const float p[3], const float to[3]) {
  const float d[3] = {to[0] - p[0], to[1] - p[1], to[2] - p[2]};
  const int32_t n = count_at(m, 0x14);
  for (int32_t i = 0; i < n; ++i) {
    const float* v = m.vertices + 3 * i;
    const float* t = m.morphs + 3 * i;
    if (v[0] == p[0] && v[1] == p[1] && v[2] == p[2] && t[0] == d[0] && t[1] == d[1] && t[2] == d[2]) return i;
  }
  m.vertices = static_cast<float*>(std::realloc(m.vertices, 12 * static_cast<size_t>(n + 1)));
  std::memcpy(m.vertices + 3 * n, p, 12);
  m.morphs = static_cast<float*>(std::realloc(m.morphs, 12 * static_cast<size_t>(n + 1)));
  std::memcpy(m.morphs + 3 * n, d, 12);
  put_count(m, 0x14, n + 1), put_count(m, 0x1c, n + 1);
  if (kMaxModelVerts < n + 1) {
    report_buffer(1, format("%s: Line %d: WARNING: Model vertex count = %d\n         Approaching max allowable: %d\n",
                            kFile, 0x887, n + 1, 0x400));
    return -1;
  }
  return n;
}

// Mech3.exe 1.2 @ 0x0053a070. The normal within 0.0001, else a new one.
int32_t find_normal(Model& m, const float p[3]) {
  const int32_t n = count_at(m, 0x18);
  for (int32_t i = 0; i < n; ++i) {
    const float* v = m.normals + 3 * i;
    if (std::fabs(v[0] - p[0]) < 0.0001 && std::fabs(v[1] - p[1]) < 0.0001 && std::fabs(v[2] - p[2]) < 0.0001)
      return i;
  }
  m.normals = static_cast<float*>(std::realloc(m.normals, 12 * static_cast<size_t>(n + 1)));
  std::memcpy(m.normals + 3 * n, p, 12);
  put_count(m, 0x18, n + 1);
  if (kMaxModelVerts < n + 1) {
    report_buffer(1, format("%s: Line %d: WARNING: Model normal count = %d\n         Approaching max allowable: %d\n",
                            kFile, 0x8d3, n + 1, 0x400));
    return -1;
  }
  return n;
}

// Mech3.exe 1.2 @ 0x0053a530 ECX n, EDX points. Each point but the first
// lying on the line of its neighbours (unit cross product under 0.001 on
// every axis) is dropped from the caller's points, from the start again
// each time; 1 when any was.
int drop_colinear(int32_t* n, float* p, std::vector<int32_t>* kept) {
  int dropped = 0;
  for (bool again = true; again;) {
    again = false;
    const int32_t count = *n;
    if (count < 2) break;
    for (int32_t i = 1, c = 2; i <= count - 1; ++i, c = (c + 1) % count) {
      const float* a = p + 3 * (i - 1);
      const float* b = p + 3 * i;
      const float* q = p + 3 * c;
      const float u0 = a[0] - b[0], u1 = a[1] - b[1], u2 = a[2] - b[2];
      const float v0 = q[0] - b[0], v1 = q[1] - b[1], v2 = q[2] - b[2];
      float x = u2 * v1 - u1 * v2, y = u0 * v2 - u2 * v0, z = u1 * v0 - u0 * v1;
      float len = 0.0f;
      if (std::fabs(x) > g_colinear || std::fabs(y) > g_colinear || std::fabs(z) > g_colinear)
        len = std::sqrt(x * x + y * y + z * z);
      const float inv = !(std::fabs(len) > g_colinear) ? 0.0f : 1.0f / len;
      x = inv * x, y = inv * y, z = inv * z;
      if (!(std::fabs(x) >= g_colinear) && !(std::fabs(y) >= g_colinear) && !(std::fabs(z) >= g_colinear)) {
        std::memmove(p + 3 * i, p + 3 * (i + 1), 12 * static_cast<size_t>(count - 1 - i));
        if (kept) kept->erase(kept->begin() + i);
        --*n;
        dropped = again = true;
        break;
      }
    }
  }
  return dropped;
}

// Mech3.exe 1.2 @ 0x0053c370. The next polygon record (count + 1, zeroed)
// with its vertex and normal indices; 1 when the model is full (the
// record stays, uncounted).
int add_record(Model& m, int32_t n, const PolygonAdd& a) {
  m.invalidate_render_geometry();
  const int32_t count = count_at(m, 0x10);
  // new[] for the exe's realloc: its "Error adding poly to model3d" (line
  // 0x10f7) for a failed realloc has no counterpart.
  Polygon* grown = new Polygon[static_cast<size_t>(count) + 1]{};
  for (int32_t i = 0; i < count; ++i) grown[i] = m.polygons[i];
  delete[] m.polygons;
  m.polygons = grown;
  Polygon& q = m.polygons[count];
  q.priority = a.priority;
  q.flags = static_cast<uint32_t>(n & 0xff) | (a.flag & 1) << 8 | (a.normals ? 0x200u : 0u);
  q.vertex_indices = static_cast<uint32_t*>(std::malloc(4 * static_cast<size_t>(n)));
  if (a.normals) q.normal_indices = static_cast<uint32_t*>(std::malloc(4 * static_cast<size_t>(n)));
  for (int32_t i = 0; i < n; ++i) {
    const int32_t v = a.morph ? find_morph_vertex(m, a.points + 3 * i, a.morph_points + 3 * i)
                              : find_vertex(m, a.points + 3 * i);
    q.vertex_indices[i] = static_cast<uint32_t>(v);
    if (v < 0) return 1;
    if (a.normals) {
      const int32_t k = find_normal(m, a.normals + 3 * i);
      q.normal_indices[i] = static_cast<uint32_t>(k);
      if (k < 0) return 1;
    }
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0053c270. The gradient (along a, along b) of s over the
// triangle (a0, b0) (a1, b1) (a2, b2); 0, 0 when it has no area.
void uv_gradient(float a0, float b0, float a1, float b1, float a2, float b2, float s0, float s1, float s2,
                 float out[2]) {
  float d = (b2 - b1) * (a0 - a1) - (a2 - a1) * (b0 - b1);
  if (zeroish(d)) {
    out[0] = out[1] = 0.0f;
    return;
  }
  d = 1.0f / d;
  out[0] = -(((s2 - s1) * (b0 - b1) - (b2 - b1) * (s0 - s1)) * d);
  out[1] = -(((a2 - a1) * (s0 - s1) - (s2 - s1) * (a0 - a1)) * d);
}

// Mech3.exe 1.2 @ 0x0053bdc0 ECX m, EDX polygon. A textured polygon of more
// than 3 points gets its later UVs from the first three's gradient in the
// plane its normal is most along (so the texture lies flat).
void flatten_uvs(Model& m, int32_t index) {
  m.invalidate_render_geometry();
  Polygon& q = m.polygons[index];
  const int32_t n = static_cast<int32_t>(q.flags & 0xff);
  if (!((q.material->flags & 1)) || n <= 3) return;
  float uv[2 * 0x40];
  std::memcpy(uv, q.uvs, 8 * static_cast<size_t>(n));
  auto P = [&](int32_t k) { return m.vertices + 3 * q.vertex_indices[k]; };
  float normal[3];
  zmth::poly_normal(P(0), P(1), P(2), normal);
  normalize(normal);
  const float ax = std::fabs(normal[0]), ay = std::fabs(normal[1]), az = std::fabs(normal[2]);
  int a, b;  // the two axes the gradients run along
  if (ax < ay || ax < az) {
    if (ay < ax || ay < az) a = 0, b = 1;  // mostly z: x, y
    else a = 2, b = 0;                     // mostly y: z, x
  } else {
    a = 1, b = 2;                          // mostly x: y, z
  }
  float gu[2], gv[2];
  uv_gradient(P(0)[a], P(0)[b], P(1)[a], P(1)[b], P(2)[a], P(2)[b], uv[0], uv[2], uv[4], gu);
  uv_gradient(P(0)[a], P(0)[b], P(1)[a], P(1)[b], P(2)[a], P(2)[b], uv[1], uv[3], uv[5], gv);
  for (int32_t k = 3; k < n; ++k) {
    const float da = P(k)[a] - P(0)[a], db = P(k)[b] - P(0)[b];
    q.uvs[2 * k] = db * gu[1] + da * gu[0] + uv[0];
    q.uvs[2 * k + 1] = db * gv[1] + da * gv[0] + uv[1];
  }
}

// Mech3.exe 1.2 @ 0x0053af50 ECX n, EDX uvs. Each UV to a 256th (rounded
// down from half a step up), then shifted so the smallest u and v lie in
// [0, 1).
void snap_uvs(int32_t n, float* uv) {
  for (int32_t i = 0; i < 2 * n; ++i) uv[i] = static_cast<float>(crt::msvc_ftol((uv[i] - -0.001953125f) * 256.0f)) * 0.00390625f;
  float mu = uv[0], mv = uv[1];
  for (int32_t i = 1; i < n; ++i) {
    if (uv[2 * i] < mu) mu = uv[2 * i];
    if (uv[2 * i + 1] < mv) mv = uv[2 * i + 1];
  }
  const float fu = static_cast<float>(std::floor(static_cast<double>(mu)));
  const float fv = static_cast<float>(std::floor(static_cast<double>(mv)));
  for (int32_t i = 0; i < n; ++i) uv[2 * i] = uv[2 * i] - fu, uv[2 * i + 1] = uv[2 * i + 1] - fv;
}

// Mech3.exe 1.2 @ 0x0053c500. The record's UVs (shifted near 0, then
// flattened and snapped), colours (white without), scratch, material and
// zone; the polygon counted.
int fill_record(Model& m, int32_t n, const PolygonAdd& a) {
  m.invalidate_render_geometry();
  const int32_t index = count_at(m, 0x10);
  Polygon& q = m.polygons[index];
  if ((a.material->flags & 1)) {
    q.uvs = static_cast<float*>(std::malloc(8 * static_cast<size_t>(n)));
    std::memcpy(q.uvs, a.uvs, 8 * static_cast<size_t>(n));
    float mu = q.uvs[0], mv = q.uvs[1];
    for (int32_t i = 1; i < n; ++i) {
      if (q.uvs[2 * i] < mu) mu = q.uvs[2 * i];
      if (q.uvs[2 * i + 1] < mv) mv = q.uvs[2 * i + 1];
    }
    const double fu = std::floor(static_cast<double>(mu)), fv = std::floor(static_cast<double>(mv));
    for (int32_t i = 0; i < n; ++i) {
      q.uvs[2 * i] = q.uvs[2 * i] - static_cast<float>(fu);
      q.uvs[2 * i + 1] = q.uvs[2 * i + 1] - static_cast<float>(fv);
    }
  }
  q.colors = static_cast<float*>(std::malloc(12 * static_cast<size_t>(n)));
  for (int32_t i = 0; i < 3 * n; ++i) q.colors[i] = a.colors ? a.colors[i] : 255.0f;
  q.scratch = static_cast<uint32_t*>(std::malloc(4 * static_cast<size_t>(q.flags & 0xff)));
  q.material = a.material;
  flatten_uvs(m, index);
  if ((a.material->flags & 1)) snap_uvs(n, q.uvs);
  std::memcpy(&q.zone_set, a.zone, 4);
  put_count(m, 0x10, index + 1);
  return 0;
}

namespace {
// Mech3.exe 1.2 @ 0x0053a930. A non-planar polygon as the fan of triangles
// from its first point, each added on its own.
void add_fan(Model& m, int32_t n, const PolygonAdd& a) {
  const bool textured = (a.material->flags & 1);
  for (int32_t k = 1; k + 1 < n; ++k) {
    float pts[9], nrm[9], uv[6], mp[9], muv[6], col[9];
    const int32_t at[3] = {0, k, k + 1};
    for (int i = 0; i < 3; ++i) {
      std::memcpy(pts + 3 * i, a.points + 3 * at[i], 12);
      if (a.normals) std::memcpy(nrm + 3 * i, a.normals + 3 * at[i], 12);
      if (textured) std::memcpy(uv + 2 * i, a.uvs + 2 * at[i], 8);
      if (a.colors) std::memcpy(col + 3 * i, a.colors + 3 * at[i], 12);
      if (a.morph) {
        std::memcpy(mp + 3 * i, a.morph_points + 3 * at[i], 12);
        if (textured) std::memcpy(muv + 2 * i, a.morph_uvs + 2 * at[i], 8);
      }
    }
    PolygonAdd t = a;
    t.points = pts, t.normals = a.normals ? nrm : nullptr, t.uvs = uv, t.morph_points = mp, t.morph_uvs = muv;
    t.colors = a.colors ? col : nullptr;
    model_add_polygon(&m, 3, t);
  }
}

// Mech3.exe 1.2 @ 0x0053ac10. A polygon past the vertex limit as fans of at
// most `piece` (capped at 4) points from its first point.
void add_pieces(Model& m, int32_t n, const PolygonAdd& a, int32_t piece) {
  const bool textured = (a.material->flags & 1);
  if (piece > 4) piece = 4;
  for (int32_t i = 1; i < n - 1; i += piece - 2) {
    if (n + 1 < i + piece) piece = n - i + 1;
    float pts[12], nrm[12], uv[8], mp[12], muv[8], col[12];
    for (int32_t k = 0; k < piece; ++k) {
      const int32_t at = k == 0 ? 0 : i + k - 1;
      std::memcpy(pts + 3 * k, a.points + 3 * at, 12);
      if (a.normals) std::memcpy(nrm + 3 * k, a.normals + 3 * at, 12);
      if (textured) std::memcpy(uv + 2 * k, a.uvs + 2 * at, 8);
      if (a.colors) std::memcpy(col + 3 * k, a.colors + 3 * at, 12);
      if (a.morph) {
        std::memcpy(mp + 3 * k, a.morph_points + 3 * at, 12);
        if (textured) std::memcpy(muv + 2 * k, a.morph_uvs + 2 * at, 8);
      }
    }
    if (piece < 3) zerr::report(0x400, kFile, 0xb42, format("Attempting to add polygon with only %d verts", piece).c_str());
    PolygonAdd t = a;
    t.points = pts, t.normals = a.normals ? nrm : nullptr, t.uvs = uv, t.morph_points = mp, t.morph_uvs = muv;
    t.colors = a.colors ? col : nullptr;
    model_add_polygon(&m, piece, t);
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x0053bb20
int model_set_priority(Model* m, int32_t priority) {
  if (!m) return 0;
  for (int32_t i = 0; i < static_cast<int32_t>(m->count(0x10)); ++i) m->polygons[i].priority = priority;
  return 0;
}

// Mech3.exe 1.2 @ 0x0053c6c0. 0 added, 1 refused, 2 added in pieces. The
// caller's points lose their colinear ones in place (the other arrays do
// not follow, as in the exe).
int model_add_polygon(Model* m, int32_t n, const PolygonAdd& a) {
  if (n < 3) {
    zerr::report(0x200, kFile, 0x11e7, format("ERROR: You're trying to add a Polygon with only (%d) verts", n).c_str());
    return 1;
  }
  if (57.6 < n) {
    zerr::report(0x200, kFile, 0x11f1, format("Poly vertex count approaching limit (%d / %d)", n, 0x40).c_str());
    return 1;
  }
  int32_t k = n;
  std::vector<int32_t> kept(static_cast<size_t>(n));
  for (int32_t i = 0; i < n; ++i) kept[static_cast<size_t>(i)] = i;
  if (drop_colinear(&k, a.points, &kept) && k < 3) {
    zerr::report(0x100, kFile, 0x120f,
                 format("Discarding Polygon: (%d of %d) verts after 'check_colinearity()'", k, n).c_str());
    return 1;
  }
  // FIX (bug): the exe drops colinear points from the points only, so every
  // UV, colour and normal after one belongs to the next point (a crater's
  // cut ground pieces have such points). zfix bugs: they go too.
  std::vector<float> nrm, uv, col, mp, muv;
  PolygonAdd fixed = a;
  if (k < n && zfix::options().bugs) {
    auto take = [&](const float* src, int width, std::vector<float>& to) -> const float* {
      if (!src) return nullptr;
      for (int32_t i : kept) to.insert(to.end(), src + width * i, src + width * (i + 1));
      return to.data();
    };
    fixed.normals = take(a.normals, 3, nrm);
    if (a.material->flags & 1) fixed.uvs = take(a.uvs, 2, uv);
    fixed.colors = take(a.colors, 3, col);
    if (a.morph) {
      fixed.morph_points = take(a.morph_points, 3, mp);
      if (a.material->flags & 1) fixed.morph_uvs = take(a.morph_uvs, 2, muv);
    }
  }
  const PolygonAdd& use = fixed;
  if (k > 3) {
    const float* p = a.points;
    float nx = 0.0f, ny = 0.0f, nz = 0.0f, sx = 0.0f, sy = 0.0f, sz = 0.0f;
    for (int32_t i = 0; i < k; ++i) {
      const float* c = p + 3 * i;
      const float* q = p + 3 * ((i + 1) % k);
      nx = (c[1] - q[1]) * (q[2] + c[2]) + nx;
      ny = (c[2] - q[2]) * (q[0] + c[0]) + ny;
      nz = (c[0] - q[0]) * (q[1] + c[1]) + nz;
      sx = sx + c[0], sy = sy + c[1], sz = sz + c[2];
    }
    float len = 0.0f;
    if (!zeroish(nx) || !zeroish(ny) || !zeroish(nz)) len = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float inv = zeroish(len) ? 0.0f : 1.0f / len;
    const float ux = inv * nx, uy = inv * ny, uz = inv * nz;
    const float nlen = static_cast<float>(k) * len;
    const float dot = sx * nx + sy * ny + sz * nz;
    const float d = -(dot / nlen);
    for (int32_t i = 0; i < k; ++i) {
      const float* c = p + 3 * i;
      if (std::fabs(uz * c[2] + c[0] * ux + c[1] * uy + d) > g_planar) {
        zerr::report(0x100, kFile, 0x121c,
                     format("Attempting to add non-planar polygon (%d verts), triangulating...", k).c_str());
        add_fan(*m, k, use);
        return 2;
      }
    }
  }
  if (k > kMaxVerts) {
    add_pieces(*m, k, use, kMaxVerts);
    return 2;
  }
  add_record(*m, k, use);
  int full = 0;
  std::string text;
  if (kMaxModelVerts < count_at(*m, 0x14)) {
    text = format("%s: Line %d: WARNING: Model vertex count = %d\n         Approaching max allowable: %d\n", kFile,
                  0x1193, count_at(*m, 0x14), 0x400);
    report_buffer(1, text);
    full = 1;
  }
  if (kMaxModelVerts < count_at(*m, 0x18)) {
    text += format("%s: Line %d: WARNING: Model normal count = %d\n         Approaching max allowable: %d\n", kFile,
                   0x11a0, count_at(*m, 0x18), 0x400);
    report_buffer(1, text);
    full = 1;
  }
  if (full) return 1;
  return fill_record(*m, k, use);
}

// Mech3.exe 1.2 @ 0x00553160 (source file unknown, after gmod_cookie.cpp).
// A model's strip polygons (flag 0x400; no shipped world has any) each
// become triangles: the strip's first three points stay in it, and from its
// end back each further point k (with the points (n - 2) | 1 and
// (n - 1) & ~1 as n counts down) makes a new triangle appended to the
// model. Only models with +0 = 0.
void expand_strips(Model& m) {
  m.invalidate_render_geometry();
  if (count_at(m, 0) != 0) return;
  const int32_t total = count_at(m, 0x10);
  for (int32_t i = 0; i < total; ++i) {
    if (!(m.polygons[i].flags & 0x400)) continue;
    m.polygons[i].flags &= ~0x400u;
    uint32_t n = m.polygons[i].flags & 0xff;
    if (n != 3) {
      for (int32_t k = static_cast<int32_t>(n) - 3; k > 0; --k, --n) {
        const int32_t count = count_at(m, 0x10);
        Polygon* grown = new Polygon[static_cast<size_t>(count) + 1]{};
        for (int32_t j = 0; j < count; ++j) grown[j] = m.polygons[j];
        delete[] m.polygons;
        m.polygons = grown;
        put_count(m, 0x10, count + 1);
        const Polygon& s = m.polygons[i];
        Polygon& t = m.polygons[count];
        const uint32_t a = static_cast<uint32_t>(k), b = (n - 2) | 1, c = (n - 1) & ~1u;
        // FIX (bug): the exe keeps the new record's other flag bits from the
        // uninitialised realloc'd memory; here they are 0.
        t.flags = 3 | (s.flags & 0x200);
        t.priority = s.priority;
        t.vertex_indices = static_cast<uint32_t*>(std::malloc(12));
        t.vertex_indices[0] = s.vertex_indices[a], t.vertex_indices[1] = s.vertex_indices[b];
        t.vertex_indices[2] = s.vertex_indices[c];
        if (t.flags & 0x200) {
          t.normal_indices = static_cast<uint32_t*>(std::malloc(12));
          t.normal_indices[0] = s.normal_indices[a], t.normal_indices[1] = s.normal_indices[b];
          t.normal_indices[2] = s.normal_indices[c];
        }
        if (s.material && (s.material->flags & 1)) {
          t.uvs = static_cast<float*>(std::malloc(24));
          std::memcpy(t.uvs, s.uvs + 2 * a, 8), std::memcpy(t.uvs + 2, s.uvs + 2 * b, 8);
          std::memcpy(t.uvs + 4, s.uvs + 2 * c, 8);
        }
        t.colors = static_cast<float*>(std::malloc(36));
        std::memcpy(t.colors, s.colors + 3 * a, 12), std::memcpy(t.colors + 3, s.colors + 3 * b, 12);
        std::memcpy(t.colors + 6, s.colors + 3 * c, 12);
        t.scratch = static_cast<uint32_t*>(std::malloc(12));
        t.material = s.material;
        t.zone_set = s.zone_set;
      }
    }
    Polygon& s = m.polygons[i];
    s.flags = (s.flags & 0xffffff03u) | 3;
    s.vertex_indices = static_cast<uint32_t*>(std::realloc(s.vertex_indices, 12));
    if (s.flags >> 9 & 1) s.normal_indices = static_cast<uint32_t*>(std::realloc(s.normal_indices, 12));
    if (s.material && (s.material->flags & 1)) s.uvs = static_cast<float*>(std::realloc(s.uvs, 24));
    s.colors = static_cast<float*>(std::realloc(s.colors, 36));
    s.scratch = static_cast<uint32_t*>(std::realloc(s.scratch, 12));
  }
}

namespace {
template <typename T> T hget(const Model& m, int off) {
  T v;
  std::memcpy(&v, m.header + off, sizeof v);
  return v;
}
template <typename T> void hput(Model& m, int off, T v) { std::memcpy(m.header + off, &v, sizeof v); }
}  // namespace

// Mech3.exe 1.2 @ 0x00541ad0: the texture scroll's step (docs/render.md
// section 3, Texture scroll), once a frame ([0x0072f338] kept at +0x30):
// every textured polygon's UVs move by dt ([0x0072c52c]) times the speeds
// (+0x28 u, +0x2c v), less the whole part of the moved middle of its range,
// so they stay near 0..1. A speed of 0 (or NaN, as the x87 tests it) moves
// nothing on its axis.
int model_scroll_tick(Model* m) {
  if (!m) return -1;
  const uint32_t frame = zcls::frames().frame;
  if (hget<uint32_t>(*m, 0x30) == frame) return 0;
  hput<uint32_t>(*m, 0x30, frame);
  const float su = hget<float>(*m, 0x28), sv = hget<float>(*m, 0x2c);
  const bool no_u = !(su < 0.0f || su > 0.0f), no_v = !(sv < 0.0f || sv > 0.0f);
  if (no_u && no_v) return 0;
  m->invalidate_render_geometry();
  const float dt = ztimer::state().dt;
  const float du = dt * su, dv = dt * sv;
  const int32_t n = static_cast<int32_t>(m->count(0x10));
  for (int32_t k = 0; k < n; ++k) {
    Polygon& p = m->polygons[k];
    if (!(p.material->flags & 1)) continue;  // textured
    float* uv = p.uvs;
    const int32_t count = static_cast<int32_t>(p.flags & 0xff);
    if (!no_u && !no_v) {
      float umin = uv[0], umax = uv[0], vmin = uv[1], vmax = uv[1];
      for (int32_t j = 1; j < count; ++j) {
        const float u = uv[2 * j], v = uv[2 * j + 1];
        if (!(u >= umin)) umin = u;  // C0: below, or unordered
        else if (u > umax) umax = u;
        if (!(v >= vmin)) vmin = v;
        else if (v > vmax) vmax = v;
      }
      const float ou = du - static_cast<float>(crt::msvc_ftol(du - (umax + umin) * -0.5f));
      const float ov = dv - static_cast<float>(crt::msvc_ftol(dv - (vmax + vmin) * -0.5f));
      for (int32_t j = 0; j < count; ++j) uv[2 * j] = ou + uv[2 * j], uv[2 * j + 1] = ov + uv[2 * j + 1];
      continue;
    }
    // One axis: its own copy of the range walk (the maximum taken when not
    // above it, NaN included).
    const int a = no_u ? 1 : 0;
    const float d = no_u ? dv : du;
    float lo = uv[a], hi = uv[a];
    for (int32_t j = 1; j < count; ++j) {
      const float x = uv[2 * j + a];
      if (!(x >= lo)) lo = x;
      else if (!(hi >= x)) hi = x;
    }
    const float o = d - static_cast<float>(crt::msvc_ftol(d - (hi + lo) * -0.5f));
    for (int32_t j = 0; j < count; ++j) uv[2 * j + a] = o + uv[2 * j + a];
  }
  return 0;
}

}  // namespace zmod
