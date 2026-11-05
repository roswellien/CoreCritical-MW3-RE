#include "gamez/zclass/cls_mechlib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zutil/zutl_zar.h"

namespace zcls {

namespace {
// Ours (docs/modding.md, M3): an entry read with its own end known (a mod's
// file may be broken). A count the bytes left cannot hold, a node of another
// class, a tree deeper than 64, or an index past its array mark the read
// broken; the caller refuses it. Without an end (-1) the reads are the
// original's.
long g_end = -1;
bool g_broken = false;
// Whether `bytes` more fit before the end; false (and broken) when not.
bool fits(std::FILE* f, uint64_t bytes) {
  if (g_end < 0) return true;
  if (g_broken || static_cast<uint64_t>(g_end - std::ftell(f)) < bytes || std::ftell(f) > g_end) g_broken = true;
  return !g_broken;
}

template <typename T>
T* read_array(std::FILE* f, uint32_t count, uint32_t size) {
  if (!fits(f, static_cast<uint64_t>(count) * size)) return static_cast<T*>(std::calloc(1, size));
  auto* p = static_cast<T*>(std::malloc(static_cast<size_t>(count) * size));
  std::fread(p, size, count, f);
  return p;
}

uint32_t u32_at(const uint8_t* p, int off) {
  uint32_t v;
  std::memcpy(&v, p + off, 4);
  return v;
}
}  // namespace

bool materials_fit(const MechLib& lib, const Node* node);

bool& mechlib_post_process() {
  static bool on = false;
  return on;
}

// Mech3.exe 1.2 @ 0x004fc6d0
bool mechlib_open(MechLib& lib, const char* path) {
  const bool ok = zutl_zar::open(lib.archive, path);
  uint32_t version = 0, format = 0, size = 4, size2 = 4;
  zutl_zar::read(lib.archive, zutl_zar::find(lib.archive, "version"), &version, &size);
  zutl_zar::read(lib.archive, zutl_zar::find(lib.archive, "format"), &format, &size2);
  if (version != 27 || format != 1) {  // [0x005eb1f0], [0x005eb1f4]
    zutl_zar::close(lib.archive);
    return false;
  }
  if (ok) mechlib_read_materials(lib);
  return ok;
}

// Mech3.exe 1.2 @ 0x004fc9e0. A missing entry reads none. Ours: bounded by
// the entry; a broken table leaves none (said in the log).
bool mechlib_read_materials(MechLib& lib) {
  uint32_t length = 0;
  std::FILE* f = zutl_zar::seek(lib.archive, zutl_zar::find(lib.archive, "materials"), &length);
  if (!f) return false;
  g_end = std::ftell(f) + static_cast<long>(length), g_broken = false;
  uint32_t count = 0;
  if (fits(f, 4)) std::fread(&count, 4, 1, f);
  if (!fits(f, static_cast<uint64_t>(count) * 0x28)) count = 0;
  lib.materials.reserve(count);
  for (uint32_t i = 0; i < count && !g_broken; ++i) {
    auto* m = new Material{};  // 0x004fc470
    read_material(f, *m);
    lib.materials.push_back(m);
  }
  g_end = -1;
  if (g_broken) {
    std::printf("mw3: %s: its materials are broken (a count or a name past the entry's end), not read\n",
                lib.archive.path ? lib.archive.path : "mechlib");
    for (Material* m : lib.materials) material_free(*m), delete m;
    lib.materials.clear();
    return false;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x004fc650
Node* mechlib_find(MechLib& lib, const char* name) {
  for (Node* root : lib.trees)
    if (std::strcmp(root->name(), name) == 0) return root;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x004fc8f0
Node* mechlib_load(MechLib& lib, const char* name) {
  if (Node* hit = mechlib_find(lib, name)) return hit;
  uint32_t length = 0;
  std::FILE* f = zutl_zar::seek(lib.archive, zutl_zar::find(lib.archive, name), &length);
  if (f) g_end = std::ftell(f) + static_cast<long>(length), g_broken = false;
  Node* root = read_tree(lib.reader, f, nullptr);
  g_end = -1;
  // Ours: a broken tree, or one whose polygons name a material the library
  // lacks, is refused (the original read on or indexed past the list).
  if (root && !g_broken && !materials_fit(lib, root)) g_broken = true;
  if (g_broken) {
    g_broken = false;
    std::printf("mw3: %s: its model %s is broken (a count, class, depth or index it cannot hold), not read\n",
                lib.archive.path ? lib.archive.path : "mechlib", name);
    return nullptr;
  }
  mechlib_resolve(lib, root);
  if (root) {
    auto identity = std::make_shared<SourceAsset>();
    identity->name = name;
    std::vector<Node*> pending{root};
    for (size_t i = 0; i < pending.size(); ++i) {
      Node* node = pending[i];
      node->source_asset = identity;
      ++identity->nodes[node->name()];
      node->source_asset_root = node == root;
      node->source_model_vertices = node->model ? node->model->count(0x14) : 0;
      for (int32_t child = 0; child < node->child_count(); ++child)
        pending.push_back(node->children[child]);
    }
  }
  // 0x004fc8b0 (an edge pass per model) is not ported: only one video mode sets the flag.
  if (root) lib.trees.push_back(root);
  return root;
}

// Ours: every polygon's material index within the library's list.
bool materials_fit(const MechLib& lib, const Node* node) {
  if (!node) return true;
  if (const zmod::Model* m = node->model)
    for (uint32_t i = 0; i < m->count(0x10); ++i)
      if (m->polygons[i].material_index >= lib.materials.size()) return false;
  for (int32_t i = 0; i < node->child_count(); ++i)
    if (node->children && !materials_fit(lib, node->children[i])) return false;
  return true;
}

// Mech3.exe 1.2 @ 0x004fcd40. Each polygon's index becomes the world material.
void mechlib_resolve(MechLib& lib, Node* node) {
  if (!node) return;
  if (zmod::Model* m = node->model)
    for (uint32_t i = 0; i < m->count(0x10); ++i) {
      zmod::Polygon& p = m->polygons[i];
      p.material = material_resolve(*lib.materials[p.material_index]);
    }
  for (int32_t i = 0; i < node->child_count(); ++i) mechlib_resolve(lib, node->children[i]);
}

// Mech3.exe 1.2 @ 0x004fc520. The raw bytes as a world material: the texture
// at +0x10 once named, the soil at +0x20, the cycle (+0x24) 0 in the file.
zmod::Material* material_resolve(Material& m) {
  if (m.resolved) return m.world;
  if (m.texture) m.entry = zimg::add_texture(m.texture);
  zmod::Material w{};
  const uint8_t* r = m.raw;
  w.alpha = r[0], w.flags = r[1];
  std::memcpy(&w.rgb, r + 2, 2);
  std::memcpy(&w.r, r + 4, 4), std::memcpy(&w.g, r + 8, 4), std::memcpy(&w.b, r + 0xc, 4);
  w.texture = m.entry;
  if (!m.entry) w.texture_index = u32_at(r, 0x10);
  std::memcpy(w.unk_14, r + 0x14, 12);
  w.soil = u32_at(r, 0x20);
  w.file_cycle = u32_at(r, 0x24);
  if (zmod::Material* added = zmod::material_add(w)) {
    m.resolved = true;
    std::free(m.raw);
    m.raw = nullptr;
    m.world = added;
  }
  return m.resolved ? m.world : nullptr;
}

// Mech3.exe 1.2 @ 0x004fc490
void material_free(Material& m) {
  if (!m.resolved && m.raw) std::free(m.raw);
  std::free(m.texture);
  m.texture = nullptr;
  m.raw = nullptr, m.world = nullptr;
}

// Mech3.exe 1.2 @ 0x004fc790
void mechlib_close(MechLib& lib) {
  for (Material* m : lib.materials) {
    if (!m) continue;
    material_free(*m);
    delete m;
  }
  lib.materials.clear();
  lib.trees.clear();
  zutl_zar::close(lib.archive);
}

// Mech3.exe 1.2 @ 0x004fc570. A non-zero texture field means a name follows.
bool read_material(std::FILE* f, Material& m) {
  m.raw = static_cast<uint8_t*>(std::calloc(1, 0x28));
  if (!fits(f, 0x28)) return false;
  std::fread(m.raw, 0x28, 1, f);
  m.resolved = false;
  m.texture = nullptr;
  if (u32_at(m.raw, 0x10) != 0) {
    std::memset(m.raw + 0x10, 0, 4);
    zutl_zar::read_string(f, &m.texture);
  }
  return true;
}

// Mech3.exe 1.2 @ 0x004fcf20. The file's 0x5c bytes over a pool slot (its
// link stays). The file's pointer fields only say whether an array follows.
zmod::Model* read_model(std::FILE* f) {
  uint8_t header[0x5c] = {};
  if (fits(f, sizeof header)) std::fread(header, sizeof header, 1, f);
  zmod::Model* m = zmod::model_alloc();
  std::memcpy(m->header, header, sizeof header);
  uint32_t polys = u32_at(header, 0x10), verts = u32_at(header, 0x14);
  uint32_t norms = u32_at(header, 0x18), morphs = u32_at(header, 0x1c);
  if (g_end >= 0 && (g_broken || !fits(f, static_cast<uint64_t>(polys) * 0x24))) {  // ours: none of them read
    polys = verts = norms = morphs = 0;
    std::memset(m->header + 0x10, 0, 16);
  }
  if (static_cast<int32_t>(verts) > 0) m->vertices = read_array<float>(f, verts, 12);
  if (static_cast<int32_t>(norms) > 0) m->normals = read_array<float>(f, norms, 12);
  if (static_cast<int32_t>(morphs) > 0) m->morphs = read_array<float>(f, morphs, 12);
  if (static_cast<int32_t>(polys) <= 0) return m;
  std::vector<uint8_t> raw(static_cast<size_t>(polys) * 0x24);
  std::fread(raw.data(), raw.size(), 1, f);
  m->polygons = new zmod::Polygon[polys]{};
  for (uint32_t i = 0; i < polys; ++i) {
    const uint8_t* r = raw.data() + i * 0x24;
    zmod::Polygon& p = m->polygons[i];
    p.flags = u32_at(r, 0);
    p.priority = static_cast<int32_t>(u32_at(r, 4));
    p.material_index = u32_at(r, 0x1c);
    p.zone_set = u32_at(r, 0x20);
    const uint32_t k = p.flags & 0xff;
    if (k) p.vertex_indices = read_array<uint32_t>(f, k, 4);
    // Ours: the indices within the model's arrays (a broken file's would be
    // read past them by the renderer).
    if (g_end >= 0 && k && !g_broken)
      for (uint32_t j = 0; j < k; ++j)
        if (p.vertex_indices[j] >= verts) g_broken = true;
    if ((p.flags & 0x200) && u32_at(r, 0xc)) p.normal_indices = read_array<uint32_t>(f, k, 4);
    if (g_end >= 0 && p.normal_indices && !g_broken)
      for (uint32_t j = 0; j < k; ++j)
        if (p.normal_indices[j] >= norms) g_broken = true;
    if (u32_at(r, 0x10)) p.uvs = read_array<float>(f, k, 8);
    if (u32_at(r, 0x14)) p.colors = read_array<float>(f, k, 12);
    p.scratch = static_cast<uint32_t*>(std::malloc(k * 4));
  }
  return m;
}

// Mech3.exe 1.2 @ 0x004fce50. The file's 0xd0 bytes replace the new node
// except its data, flag bits 0-1, list-7 item and parents. Only object3d
// nodes are made: the exe dereferences null for any other class.
Node* read_node(std::FILE* f, Node* parent) {
  uint8_t file_node[0xd0] = {};
  if (!fits(f, sizeof file_node + 0x90)) return nullptr;
  std::fread(file_node, sizeof file_node, 1, f);
  int32_t cls;
  std::memcpy(&cls, file_node + 0x34, 4);
  if (cls != 5) {
    if (g_end >= 0) g_broken = true;  // ours: the exe would dereference null
    return nullptr;
  }
  Node* node = object3d_create();
  std::fread(static_cast<Object3d*>(node->data)->raw, 0x90, 1, f);
  const uint32_t flags = node->flags();
  std::memcpy(node->raw, file_node, sizeof file_node);
  node->set_flags((node->flags() & ~3u) | (flags & 3));
  node->put<int32_t>(0x54, 0), node->put<int32_t>(0x58, 0);
  node->parents = nullptr;
  if (parent) node_add_child(parent, node);
  return node;
}

// Mech3.exe 1.2 @ 0x004fcdc0. Each node, its model, then its children, which
// add themselves back as they are read.
Node* read_tree(TreeReader& lib, std::FILE* f, Node* parent) {
  if (!f) return nullptr;
  ++lib.depth;
  Node* node = read_node(f, parent);
  if (!node || (g_end >= 0 && lib.depth > 64)) {  // ours: a broken file's
    g_broken = g_broken || g_end >= 0;
    --lib.depth;
    return node;
  }
  if (node->get<int32_t>(0x3c) != 0) node->model = read_model(f);
  int32_t children = node->child_count();
  if (g_end >= 0 && (children < 0 || !fits(f, static_cast<uint64_t>(children) * (0xd0 + 0x90)))) children = 0;
  node->put<int32_t>(0x5c, 0), node->put<int32_t>(0x60, 0);
  node->children = nullptr;
  for (int32_t i = 0; i < children && !(g_end >= 0 && g_broken); ++i) read_tree(lib, f, node);
  --lib.depth;
  return node;
}

}  // namespace zcls
