#include "gamez/zdeclient/zdec_crater.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/cls_zbd.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zgeometry/zgeo_model.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_matl.h"

#pragma STDC FP_CONTRACT OFF

namespace zdec {
namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zDEClient\\zdec_crater.cpp";
const char kFeature[] = "ZDEC_FEATURE";  // 0x005eb9f0, the crater nodes' name
// 0x005eb910: four up normals, the crater polygons' (one a point).
const float kUp[12] = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
constexpr float kGrey = 0.8f;  // 0x3f4ccccd, the colour where no cut ground is near
CraterState g_craters;
CraterHooks g_hooks;

zcls::World* world_data() { return static_cast<zcls::World*>(state().world); }
}  // namespace

CraterState& craters() { return g_craters; }
bool CraterState::ByIndex::operator()(const zcls::Node* a, const zcls::Node* b) const {
  return zcls::node_index_of(a) < zcls::node_index_of(b);
}
CraterHooks& crater_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x005020b0
zcls::Node* world_node() { return state().world_node; }

// Mech3.exe 1.2 @ 0x00502090
zcls::Partition* cell_at(int32_t x, int32_t z) {
  zcls::World* w = world_data();
  if (!w) return nullptr;
  return w->rows[z] + x;
}

// Mech3.exe 1.2 @ 0x004ffe80. The texture entry is the first after the
// defaults whose ground is the order's surface, else the defaults.
CraterRecord* record_new(const zwep::CraterOrder* order) {
  CraterRecord* r = new CraterRecord;
  r->order = *order;
  r->ring.assign(3 * static_cast<size_t>(order->points), 0.0f);
  r->set = zgeo::clip_set_new();
  if (r->order.x00 & 0x1008) {
    State& s = state();
    r->texture = s.textures;
    for (int32_t i = 1; i < s.texture_count; ++i) {
      if (s.textures[i].ground == static_cast<zmod::Material*>(r->order.material)) {
        r->texture = s.textures + i;
        break;
      }
    }
  }
  return r;
}

// Mech3.exe 1.2 @ 0x004ff8f0
void record_free(CraterRecord* r) {
  if (!r) return;
  if (r->set) zgeo::clip_set_free(r->set);
  delete r;
}

// The placement 0x004ffad0 and 0x00502230 share: the cell under the centre
// (refused outside the grid or full), the centre pushed back inside it by
// the radius and 1 m, the ring at sin/cos steps of 2 pi / n round it, its
// box, and refused within 5 m (by box) of another crater node in the cell.
bool place_ring(int32_t points, float radius, zwep::Vec3f3& at, std::vector<float>& ring, float box[4],
                zcls::Partition*& cell) {
  zcls::Node* world = world_node();
  zcls::World* w = static_cast<zcls::World*>(world->data);
  int32_t cx, cz, x, z, inside;
  if (!w || zcls::world_cell_wrapped(world, &cx, at.x, at.z, &cz, &x, &z, &inside) != 0 || !inside) return false;
  cell = cell_at(x, z);
  if (!cell || cell->raw[0x39] >= w->raw[0x4c]) return false;
  const float dx = at.x - cell->get<float>(8);
  const float dz = at.z - cell->get<float>(0xc);
  const float size_x = w->get<float>(0x64), low_z = w->get<float>(0x68);
  const float right = dx + radius;
  if (right > size_x) at.x = at.x - ((right - size_x) - -1.0f);
  else if (!(dx - radius >= 0.0f)) at.x = ((radius - dx) + at.x) - -1.0f;
  const float bottom = dz - radius;
  if (!(bottom >= low_z)) {
    at.z = ((low_z - bottom) + at.z) - -1.0f;
  } else if (0.0f < dz + radius) {
    at.z = at.z - ((dz + radius) - -1.0f);
  }
  const float step = static_cast<float>(6.28318530718 / static_cast<double>(points));
  float angle = 0.0f;
  for (int32_t i = 0; i < points; ++i) {
    float* p = &ring[3 * static_cast<size_t>(i)];
    p[0] = static_cast<float>(std::sin(static_cast<double>(angle)));
    p[2] = static_cast<float>(std::cos(static_cast<double>(angle)));
    p[0] = p[0] * radius;
    p[2] = radius * p[2];
    p[0] = p[0] + at.x;
    p[1] = at.y;
    p[2] = at.z + p[2];
    angle = step + angle;
  }
  const float* p = ring.data();
  box[0] = box[2] = p[0];
  box[1] = box[3] = p[2];
  for (int32_t i = 1; i < points; ++i) {
    const float* q = p + 3 * i;
    if (box[0] > q[0]) box[0] = q[0];
    if (box[2] < q[0]) box[2] = q[0];
    if (box[1] > q[2]) box[1] = q[2];
    if (box[3] < q[2]) box[3] = q[2];
  }
  if (cell->raw[0x39] != 0) {
    const int16_t count = cell->get<int16_t>(0x3a);
    for (int16_t i = 0; i < count; ++i) {
      zcls::Node* node = cell->nodes[i];
      if (std::strcmp(node->name(), kFeature) != 0 || !node->owner) continue;
      const Feature& f = *static_cast<const Feature*>(node->owner);
      if (f.type != 1 && f.type != 3) continue;
      if (box[0] < f.box[2] - -5.0f && f.box[0] - 5.0f < box[2] && box[1] < f.box[3] - -5.0f &&
          f.box[1] - 5.0f < box[3])
        return false;
    }
  }
  return true;
}

// The crater node's name and record (+0x40): here its type and box.
void mark_feature(zcls::Node* node, int32_t type, const float box[4]) {
  zcls::node_set_name(node, kFeature);
  g_craters.features.push_back(Feature{type, {box[0], box[1], box[2], box[3]}});
  node->owner = &g_craters.features.back();
}

// Mech3.exe 1.2 @ 0x004ffad0
CraterRecord* crater_build(const zwep::CraterOrder* order) {
  CraterRecord* r = record_new(order);
  if (!place_ring(order->points, r->order.radius, r->order.at, r->ring, r->box, r->cell)) {
    record_free(r);
    return nullptr;
  }
  return r;
}

// Mech3.exe 1.2 @ 0x004fff20. The ring becomes the outline as the clip
// left it (snapped, with the cut points).
int32_t crater_clip(CraterRecord* r) {
  const int32_t changed = zgeo::clip_cell(r->ring.data(), r->order.points, r->cell, *r->set);
  if (changed < 1) return changed < 0 ? 0 : changed;
  if (r->set->points.empty()) return 0;
  r->ring = r->set->points;
  r->order.points = r->set->point_count;
  return changed;
}

// Mech3.exe 1.2 @ 0x004fff80. A new node and model in the cell's place:
// for each ring point, a slope quad from it and the next to the points
// half way to the floor centre (a depth lower), and a floor triangle fan
// from the centre (two depths below the hit). Textured craters map the
// crater material over the radius and take the cut ground's colours.
int32_t crater_tessellate(CraterRecord* r) {
  zcls::Node* node = nullptr;
  zmod::Model* m = zgeo::crater_node(r->set->cells[0], world_node(), &node);
  // As in the exe, a node made without a model is still named and marked
  // (the exe leaves it in the world; harmless).
  if (node) mark_feature(node, r->type, r->box);
  if (!m) return -1;
  zwep::CraterOrder& o = r->order;
  const int32_t n = o.points;
  const bool textured = o.x00 & 0x1008;
  float radius = 0.0f, inv = 0.0f;
  zmod::Material* material = nullptr;
  if (textured) {
    radius = o.radius;
    material = r->texture->crater;
    inv = 1.0f / (radius + radius);
  }
  o.at.y = o.at.y - o.depth;
  const float* ring = r->ring.data();
  std::vector<float> uv(textured ? 4 * static_cast<size_t>(n) + 2 : 0);
  std::vector<float> mid(3 * static_cast<size_t>(n));
  const float cx = o.at.x, cz = o.at.z, cy = o.at.y - o.depth;
  if (textured) uv[4 * static_cast<size_t>(n)] = inv * radius, uv[4 * static_cast<size_t>(n) + 1] = inv * radius;
  // FIX (bug): the exe writes the centre's UV through a null pointer for an
  // untextured crater (none in the shipped worlds).
  for (int32_t k = 0; k < n; ++k) {
    const float* p = ring + 3 * k;
    float* q = &mid[3 * static_cast<size_t>(k)];
    const float down = o.at.y - p[1];
    q[0] = (cx - p[0]) * 0.5f + p[0];
    q[1] = (cy - p[1]) * 0.5f + p[1];
    q[2] = (cz - p[2]) * 0.5f + p[2];
    q[1] = (down * 0.5f + p[1]) - o.depth;
    if (textured) {
      float* a = &uv[2 * static_cast<size_t>(k)];
      float* b = &uv[2 * static_cast<size_t>(n + k)];
      a[0] = ((p[0] - cx) + radius) * inv;
      a[1] = ((p[2] - cz) + radius) * inv;
      b[0] = ((q[0] - cx) + radius) * inv;
      b[1] = ((q[2] - cz) + radius) * inv;
    }
  }
  float color[12];
  auto color_of = [&](int slot, const float* at) {
    if (!zgeo::colors_find(at, color + 3 * slot)) color[3 * slot] = color[3 * slot + 1] = color[3 * slot + 2] = kGrey;
  };
  for (int32_t k = 0; k < n; ++k) {
    const int32_t k1 = (k + 1) % n;
    float quad[12];
    std::memcpy(quad, ring + 3 * k, 12);
    std::memcpy(quad + 3, ring + 3 * k1, 12);
    std::memcpy(quad + 6, &mid[3 * static_cast<size_t>(k1)], 12);
    std::memcpy(quad + 9, &mid[3 * static_cast<size_t>(k)], 12);
    if (!textured) {
      zgeo::add_polygon(m, 4, quad, nullptr, nullptr, kUp, nullptr);
      continue;
    }
    float quv[8];
    std::memcpy(quv, &uv[2 * static_cast<size_t>(k)], 8);
    std::memcpy(quv + 2, &uv[2 * static_cast<size_t>(k1)], 8);
    std::memcpy(quv + 4, &uv[2 * static_cast<size_t>(n + k1)], 8);
    std::memcpy(quv + 6, &uv[2 * static_cast<size_t>(n + k)], 8);
    for (int i = 0; i < 4; ++i) color_of(i, quad + 3 * i);
    zgeo::add_polygon(m, 4, quad, material, quv, kUp, color);
  }
  float tri[9] = {cx, cy, cz};
  color_of(0, tri);
  for (int32_t k = 0; k < n; ++k) {
    const int32_t k1 = (k + 1) % n;
    std::memcpy(tri + 3, &mid[3 * static_cast<size_t>(k)], 12);
    std::memcpy(tri + 6, &mid[3 * static_cast<size_t>(k1)], 12);
    if (!textured) {
      zgeo::add_polygon(m, 3, tri, nullptr, nullptr, kUp, nullptr);
      continue;
    }
    float tuv[6];
    std::memcpy(tuv, &uv[4 * static_cast<size_t>(n)], 8);
    std::memcpy(tuv + 2, &uv[2 * static_cast<size_t>(n + k)], 8);
    std::memcpy(tuv + 4, &uv[2 * static_cast<size_t>(n + k1)], 8);
    color_of(1, tri + 3);
    color_of(2, tri + 6);
    zgeo::add_polygon(m, 3, tri, material, tuv, kUp, color);
  }
  zgeo::hooks().node_set_model(node, m);
  return 0;
}

uint32_t material_word(const void* material) {
  if (!material) return 0xffffffffu;
  const int32_t i = zmod::materials().index_of(static_cast<const zmod::Material*>(material));
  return static_cast<uint32_t>(i);
}
zmod::Material* word_material(uint32_t word) {
  const int32_t i = static_cast<int32_t>(word);
  return i >= 0 && i < zmod::materials().count ? &zmod::materials().at(i) : nullptr;
}

void order_words(const zwep::CraterOrder& o, uint32_t out[10]) {
  auto bits = [](float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
  };
  out[0] = o.x00, out[1] = static_cast<uint32_t>(o.points);
  out[2] = material_word(o.material);
  out[3] = o.x0c, out[4] = bits(o.depth), out[5] = bits(o.radius);
  out[6] = bits(o.at.x), out[7] = bits(o.at.y), out[8] = bits(o.at.z);
  out[9] = 0;
}

// Mech3.exe 1.2 @ 0x00500850. A crater's entry leaves its 12th word as
// the exe's stack had it; here 0.
int32_t history_push(int32_t type, const uint32_t* words) {
  int count;
  if (type == 1) count = 10;
  else if (type == 3) count = 11;
  else return 0;
  HistoryEntry e;
  e.type = type;
  std::memcpy(e.words, words, 4 * static_cast<size_t>(count));
  g_craters.history.push_back(e);
  return 0;
}

// Mech3.exe 1.2 @ 0x00500600
void note_touched(const zgeo::ClipSet& set) {
  for (const zgeo::CellClip& c : set.cells)
    for (const zgeo::NodeModel& nm : c.nodes) g_craters.touched.insert(nm.node);
}

// Mech3.exe 1.2 @ 0x004ff940. The vertex merge distance is 0.005 while the
// crater is made.
//
// FIX (bug): the exe frees the record at the end, though the crater node
// keeps pointing at it (+0x40) for the next crater's overlap test, which
// then reads freed memory (whatever the heap put there). The port keeps
// each crater node's type and box (craters().features).
int32_t crater_instance(const zwep::CraterOrder* order, int dust) {
  const float precision = zmod::vertex_precision();
  zmod::set_vertex_precision(0.005f);
  CraterRecord* r = crater_build(order);
  zgeo::colors_begin(order->at.x, order->at.y, order->at.z, order->radius);
  if (!r) {
    zerr::report(0x100, kFile, 0x93, "Failed to instance crater: Build Failed");
    zmod::set_vertex_precision(precision);
    return -1;
  }
  if (!crater_clip(r)) {
    record_free(r);
    zerr::report(0x100, kFile, 0xcb, "Failed to instance crater: Clip Failed");
    zmod::set_vertex_precision(precision);
    return -1;
  }
  if (crater_tessellate(r) != 0) {
    record_free(r);
    zerr::report(0x100, kFile, 0xda, "Failed to instance crater: Tesselation Failed");
    zmod::set_vertex_precision(precision);
    return -1;
  }
  uint32_t words[10];
  order_words(*order, words);
  history_push(1, words);
  note_touched(*r->set);
  zgeo::commit(*r->set);
  zmod::set_vertex_precision(precision);
  // FIX (bug, fixes.md "Untextured crater"): an untextured crater has no
  // texture entry; the exe reads the dust through null here.
  if (dust && r->texture && r->texture->anim && g_hooks.play_anim) {
    const float at[3] = {r->order.at.x, r->order.at.y, r->order.at.z};
    g_hooks.play_anim(r->texture->anim, at);
  }
  record_free(r);
  return 0;
}

// Mech3.exe 1.2 @ 0x004ffaa0
int32_t crater_make(const zwep::CraterOrder* order) {
  if (g_craters.veto && !g_craters.veto(order)) return -1;
  return crater_instance(order, 1);
}

// Mech3.exe 1.2 @ 0x00500760. The cell is found through the node's first
// parent (the world). The touched set is left (0x00500af0 empties it); it
// only ever holds ground nodes (flag 0x10000), which are re-read, never the
// crater nodes deleted below (object3d's 0x0108001c plus 0x8000 and 0x20000:
// kept clear, never cut), so a second reset reads only live nodes.
void craters_reset() {
  for (zcls::Node* node : g_craters.touched) {
    if (!node) continue;
    if (!g_hooks.restore_node || !g_hooks.restore_node(node)) zcls::reread(node, 1);  // 0x004ff730
    const int32_t x = node->get<int32_t>(0x4c), z = node->get<int32_t>(0x50);
    if (x < 0 || z < 0) continue;
    zcls::World* w = nullptr;
    std::set<const zcls::Node*> visited;
    visited.insert(node);
    zcls::Node* curr = (node->parent_count() > 0 && node->parents) ? node->parents[0] : nullptr;
    while (curr && visited.insert(curr).second) {
      if (curr->cls() == 2) {
        w = static_cast<zcls::World*>(curr->data);
        break;
      }
      curr = (curr->parent_count() > 0 && curr->parents) ? curr->parents[0] : nullptr;
    }
    if (!w || !w->rows) continue;
    const int32_t cols = w->get<int32_t>(0x88), rows = w->get<int32_t>(0x8c);
    if (cols <= 0 || rows <= 0 || x >= cols || z >= rows) continue;
    zcls::Partition* row = w->rows[z];
    if (!row) continue;
    row[x].raw[0x39] = 0;  // 0x004f22f0
  }
  while (zcls::Node* node = zcls::list_find_by_name(6, kFeature)) {
    while (node->parent_count() > 0) zcls::node_remove_child(node->parents[0], node);
    if (zmod::Model* m = node->model) {  // 0x004eb420
      zgeo::hooks().node_set_model(node, nullptr);
      zgeo::hooks().model_release(m);
    }
    zcls::node_delete(node);
  }
  g_craters.features.clear();
}

// Mech3.exe 1.2 @ 0x00500af0
int32_t history_clear() {
  g_craters.history.clear();
  g_craters.touched.clear();
  return 0;
}

// Mech3.exe 1.2 @ 0x00502020
int32_t decals_free() {
  history_clear();
  State& s = state();
  // The exe frees the frame-name array [0x0060bff4] here; the port's never
  // owned one (zdec_init: the names lived in the reader tree, FIX (A)).
  s.frames = nullptr;
  std::free(s.textures);
  s.textures = nullptr;
  s.quicksand_on = 0;
  g_craters.features.clear();  // the port's; the crater nodes go with the world
  return 0;
}

// Mech3.exe 1.2 @ 0x00500c60
void history_each(void (*crater)(const uint32_t* order), void (*quicksand)(const uint32_t* order)) {
  for (const HistoryEntry& e : g_craters.history) {
    if (e.type == 1) {
      if (crater) crater(e.words);
    } else if (e.type == 3 && quicksand) {
      quicksand(e.words);
    }
  }
}

namespace {
// A history entry as zEntity's 0x34-byte record: type, the order's words
// and the reset flag.
struct Record {
  uint32_t words[13] = {};
};
}  // namespace

// Mech3.exe 1.2 @ 0x00500b50. The "Dummy" record's other words are the
// exe's stack; here 0.
void history_save(void* context) {
  if (!g_hooks.write_record) return;
  Record dummy;
  dummy.words[12] = 1;
  g_hooks.write_record(context, "Dummy", dummy.words, 0x34);
  int32_t craters = 0, pits = 0;
  for (const HistoryEntry& e : g_craters.history) {
    char name[0x40];
    if (e.type == 1) std::snprintf(name, sizeof name, "Crater%d", craters++);
    else if (e.type == 3) std::snprintf(name, sizeof name, "QSand%d", pits++);
    else continue;
    Record r;
    r.words[0] = static_cast<uint32_t>(e.type);
    std::memcpy(r.words + 1, e.words, sizeof e.words);
    r.words[12] = 0;
    g_hooks.write_record(context, name, r.words, 0x34);
  }
}

// Mech3.exe 1.2 @ 0x00500c20 (stdcall, 3 arguments; only the record is
// read). Materials come back from their indices (order_words' FIX).
void history_restore(const uint32_t* record) {
  if (record[12] != 0) {
    craters_reset();
    history_clear();
    return;
  }
  auto f = [](uint32_t b) {
    float v;
    std::memcpy(&v, &b, 4);
    return v;
  };
  const uint32_t* w = record + 1;
  if (record[0] == 1) {
    zwep::CraterOrder o;
    o.x00 = w[0], o.points = static_cast<int32_t>(w[1]), o.material = word_material(w[2]), o.x0c = w[3];
    o.depth = f(w[4]), o.radius = f(w[5]), o.at = {f(w[6]), f(w[7]), f(w[8])}, o.owner = nullptr;
    crater_instance(&o, 0);
  } else if (record[0] == 3) {
    zwep::QuicksandOrder q;
    q.x00 = w[0], q.points = static_cast<int32_t>(w[1]), q.cycle = word_material(w[2]), q.still = word_material(w[3]);
    q.x10 = w[4];
    q.depth = f(w[5]), q.radius = f(w[6]), q.at = {f(w[7]), f(w[8]), f(w[9])}, q.owner = nullptr;
    quicksand_make(&q);
  }
}

}  // namespace zdec
