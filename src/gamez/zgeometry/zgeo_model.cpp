#include <algorithm>
#include "gamez/zgeometry/zgeo_model.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "crt/msvc_rand.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zgeometry/zgeo_convexify.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zgeometry/zgeo_weiler.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_matl.h"

#pragma STDC FP_CONTRACT OFF

namespace zgeo {
namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zGeometry\\zgeo_model.cpp";
constexpr float kByteScale = 0.007782219909131527f;  // 0x005cdd18, 255 / 32767
constexpr float kNear = 0.01f;                       // 0x005cdd28 (a double of it)

zmod::Model* node_model(zcls::Node* node) { return node ? node->model : nullptr; }

void report_count(int level, int line, const char* format, int32_t n) {
  char text[96];
  std::snprintf(text, sizeof text, format, n);
  zerr::report(level, kFile, line, text);
}

// The 16-bit colour 0x0050dd20 and 0x0050de00 pack: each value's low bits
// (the 0..255 values not scaled down to 5, 6 and 5 bits).
uint16_t pack(uint16_t word, float r, float g, float b) {
  word = static_cast<uint16_t>((static_cast<int32_t>(r) << 11) | (word & 0x7ff));
  word = static_cast<uint16_t>(((static_cast<int32_t>(g) & 0x3f) << 5) | (word & 0xf81f));
  return static_cast<uint16_t>((word & ~0x1f) | (static_cast<int32_t>(b) & 0x1f));
}

const float* vertex(const zmod::Model& m, const zmod::Polygon& p, int i) {
  return m.vertices + p.vertex_indices[i] * 3;
}
}  // namespace

ColorCache& colors() {
  static ColorCache c;
  return c;
}
Hooks::Hooks()
    : model_add([](zmod::Model* m, int32_t n, const float* points, const float* normals, const float* uvs, uint32_t a,
                   uint32_t b, uint32_t c, zmod::Material* material, int32_t priority, uint32_t flag,
                   const uint8_t zone[4], const float* colors) {
        zmod::PolygonAdd add;
        // 0x0053c6c0 drops colinear points from the caller's buffer in place.
        add.points = const_cast<float*>(points), add.normals = normals, add.uvs = uvs;
        add.morph = static_cast<int32_t>(a), add.morph_points = reinterpret_cast<const float*>(static_cast<uintptr_t>(b));
        add.morph_uvs = reinterpret_cast<const float*>(static_cast<uintptr_t>(c));
        add.material = material, add.priority = priority, add.flag = flag, add.zone = zone, add.colors = colors;
        return zmod::model_add_polygon(m, n, add);
      }),
      material_add(zmod::material_add),
      rand(crt::msvc_rand),
      node_model(zgeo::node_model),
      node_set_model(zcls::node_set_model),
      model_release(zmod::model_release),
      weiler_free([](void* w) { zgeo::weiler_free(static_cast<Weiler*>(w)); }),
      model_alloc(zmod::model_alloc),
      world_node(nullptr) {}
Hooks& hooks() {
  static Hooks h;
  return h;
}
State& state() {
  static State s;
  return s;
}

// Mech3.exe 1.2 @ 0x0050da90
void colors_begin(float x, float y, float z, float radius) {
  ColorCache& c = colors();
  c.count = 0;
  c.center[0] = x, c.center[1] = y, c.center[2] = z;
  c.reach = radius - -4.0f;
}

// Mech3.exe 1.2 @ 0x0050dad0
void colors_add(const float pos[3], const float color[3]) {
  ColorCache& c = colors();
  if (!(pos[0] >= c.center[0] - c.reach) || !(pos[0] <= c.reach + c.center[0])) return;
  if (!(pos[2] >= c.center[2] - c.reach) || !(pos[2] <= c.reach + c.center[2])) return;
  if (c.count == kColorEntries) return;
  for (int32_t i = 0; i < c.count; ++i)
    if (pos[0] == c.entries[i].pos[0] && pos[2] == c.entries[i].pos[2]) return;
  ColorEntry& e = c.entries[c.count++];
  std::memcpy(e.pos, pos, sizeof e.pos);
  std::memcpy(e.color, color, sizeof e.color);
}

// Mech3.exe 1.2 @ 0x0050dbc0
int colors_find(const float pos[3], float out[3]) {
  const ColorCache& c = colors();
  if (c.count <= 0) return 0;
  int32_t found = 0;
  for (int32_t i = 0; i < c.count; ++i)
    if (std::fabs(pos[0] - c.entries[i].pos[0]) < 0.5f && std::fabs(pos[2] - c.entries[i].pos[2]) < 0.5f) {
      found = i;
      break;
    }
  std::memcpy(out, c.entries[found].color, 3 * sizeof(float));
  return 1;
}

// Mech3.exe 1.2 @ 0x0050dc90
void to_plane(float* points, int32_t n) {
  for (float* p = points; n != 0; --n, p += 3) {
    const float y = p[1];
    p[1] = -p[2];
    p[2] = y;
  }
}

// Mech3.exe 1.2 @ 0x0050dc70
void from_plane(float* points, int32_t n) {
  for (float* p = points; n != 0; --n, p += 3) {
    const float z = p[2];
    p[2] = -p[1];
    p[1] = z;
  }
}

// Mech3.exe 1.2 @ 0x0050dcb0
int boxes_touch(const float a[4], const float b[4]) {
  if (b[2] - -1.0f < a[0]) return 0;
  if (a[2] < b[0] - 1.0f) return 0;
  if (b[3] - -1.0f < a[1]) return 0;
  if (a[3] < b[1] - 1.0f) return 0;
  return 1;
}

// Mech3.exe 1.2 @ 0x0050e3d0
void box_of(float box[4], const float* points, int32_t n) {
  box[0] = box[2] = points[0];
  box[1] = box[3] = points[1];
  for (int32_t i = 1; i < n; ++i) {
    const float* p = points + 3 * i;
    if (p[0] < box[0]) box[0] = p[0];
    if (box[2] < p[0]) box[2] = p[0];
    if (p[1] < box[1]) box[1] = p[1];
    if (box[3] < p[1]) box[3] = p[1];
  }
}

// Mech3.exe 1.2 @ 0x0050dd20
zmod::Material* random_material() {
  zmod::Material m{};
  zmod::material_init(m);
  Hooks& h = hooks();
  const float g = static_cast<float>(h.rand()) * kByteScale;
  const float r = static_cast<float>(h.rand()) * kByteScale;
  const float b = static_cast<float>(h.rand()) * kByteScale;
  m.r = r, m.g = g, m.b = b;
  m.rgb = pack(m.rgb, r, g, b);
  return state().random = h.material_add(m);
}

// Mech3.exe 1.2 @ 0x0050de00
zmod::Material* color_material(float r, float g, float b) {
  zmod::Material m{};
  zmod::material_init(m);
  m.r = r, m.g = g, m.b = b;
  m.rgb = pack(m.rgb, r, g, b);
  return state().colored = hooks().material_add(m);
}

// Mech3.exe 1.2 @ 0x0050e020
void gradient(const float p0[3], const float p1[3], const float p2[3], float s0, float s1, float s2,
              float out[2]) {
  const float ax = p2[0] - p1[0], az = p2[2] - p1[2];
  const float bx = p0[0] - p1[0], bz = p0[2] - p1[2];
  const float det = az * bx - ax * bz;
  const float d0 = s0 - s1, d2 = s2 - s1;
  if (det == 0.0f) {
    out[0] = out[1] = 0.0f;
    return;
  }
  const float inv = 1.0f / det;
  out[0] = -((d2 * bz - az * d0) * inv);
  out[1] = -((ax * d0 - d2 * bx) * inv);
}

// Mech3.exe 1.2 @ 0x0050df30. The UVs are (u, v) pairs, carried from vertex 1.
std::vector<float> uvs_at(const float* points, int32_t n, const zmod::Model& m, const zmod::Polygon& p) {
  const float *v0 = vertex(m, p, 0), *v1 = vertex(m, p, 1), *v2 = vertex(m, p, 2);
  const float* uv = p.uvs;
  float gu[2], gv[2];
  gradient(v0, v1, v2, uv[0], uv[2], uv[4], gu);
  gradient(v0, v1, v2, uv[1], uv[3], uv[5], gv);
  std::vector<float> out(2 * static_cast<size_t>(n > 0 ? n : 0));
  for (int32_t i = 0; i < n; ++i) {
    const float dx = points[3 * i] - v1[0], dz = points[3 * i + 2] - v1[2];
    out[2 * i] = dz * gu[1] + dx * gu[0] + uv[2];
    out[2 * i + 1] = dz * gv[1] + dx * gv[0] + uv[3];
  }
  return out;
}

// Mech3.exe 1.2 @ 0x0050e100. Colours are (r, g, b) per vertex.
std::vector<float> colors_at(const float* points, int32_t n, const zmod::Model& m, const zmod::Polygon& p) {
  const float *v0 = vertex(m, p, 0), *v1 = vertex(m, p, 1), *v2 = vertex(m, p, 2);
  const float* c = p.colors;
  float g[3][2];
  for (int k = 0; k < 3; ++k) gradient(v0, v1, v2, c[k], c[3 + k], c[6 + k], g[k]);
  std::vector<float> out(3 * static_cast<size_t>(n > 0 ? n : 0));
  for (int32_t i = 0; i < n; ++i) {
    const float dx = points[3 * i] - v1[0], dz = points[3 * i + 2] - v1[2];
    for (int k = 0; k < 3; ++k) out[3 * i + k] = dz * g[k][1] + dx * g[k][0] + c[3 + k];
  }
  return out;
}

// Mech3.exe 1.2 @ 0x0050e240
std::vector<float> normals_at(const float* points, int32_t n, const zmod::Model& m, const zmod::Polygon& p) {
  const float *v0 = vertex(m, p, 0), *v1 = vertex(m, p, 1), *v2 = vertex(m, p, 2);
  const float* n0 = m.normals + p.normal_indices[0] * 3;
  const float* n1 = m.normals + p.normal_indices[1] * 3;
  const float* n2 = m.normals + p.normal_indices[2] * 3;
  float g[3][2];
  for (int k = 0; k < 3; ++k) gradient(v0, v1, v2, n0[k], n1[k], n2[k], g[k]);
  std::vector<float> out(3 * static_cast<size_t>(n > 0 ? n : 0));
  for (int32_t i = 0; i < n; ++i) {
    const float dx = points[3 * i] - v1[0], dz = points[3 * i + 2] - v1[2];
    for (int k = 0; k < 3; ++k) out[3 * i + k] = dz * g[k][1] + dx * g[k][0] + n1[k];
    zmth::normalize(*reinterpret_cast<zmth::Vec3*>(&out[3 * i]));
  }
  return out;
}

// Mech3.exe 1.2 @ 0x0050f0a0
void polygon_points(const zmod::Model& m, const zmod::Polygon& p, std::vector<float>& out) {
  const uint32_t n = p.flags & 0xff;
  out.resize(3 * n);
  for (uint32_t i = 0; i < n; ++i) std::memcpy(&out[3 * i], vertex(m, p, static_cast<int>(i)), 12);
}

// Mech3.exe 1.2 @ 0x0050e450
ClipPolygon* clip_polygon_new(const float* points, int32_t n) {
  ClipPolygon* c = new ClipPolygon;
  c->points.assign(points, points + 3 * n);
  to_plane(c->points.data(), n);
  c->count = n;
  box_of(c->box, c->points.data(), n);
  return c;
}

// Mech3.exe 1.2 @ 0x0050e4c0
int clip_polygon_out(const ClipPolygon& c, int32_t* count, std::vector<float>& out) {
  *count = c.count;
  out.assign(c.points.begin(), c.points.begin() + 3 * c.count);
  from_plane(out.data(), *count);
  return 0;
}

// Mech3.exe 1.2 @ 0x0050e520
void clip_polygon_free(ClipPolygon* c) {
  if (hooks().weiler_free) hooks().weiler_free(c->weiler);
  delete c;
}

// Mech3.exe 1.2 @ 0x0050e550
int find_point(const ClipPolygon& c, const float point[3]) {
  for (int32_t i = 0; i < c.count; ++i)
    if (points_near(&c.points[3 * i], point, kNear)) return i;
  return -1;
}

// Mech3.exe 1.2 @ 0x0050e690. An edge along x or y takes the point within
// 0.01 of its line; others need both ratios to agree within 0.01.
int find_edge(const ClipPolygon& c, const float point[3]) {
  const int32_t n = c.count;
  for (int32_t i = 0; i < n; ++i) {
    const float* a = &c.points[3 * i];
    const float* b = &c.points[3 * ((i + 1) % n)];
    const float dx = b[0] - a[0], dy = b[1] - a[1];
    float t;
    if (std::fabs(dx) < kNear) {
      if (std::fabs(a[0] - point[0]) > kNear) continue;
      t = (point[1] - a[1]) / dy;
    } else if (std::fabs(dy) < kNear) {
      if (std::fabs(point[1] - a[1]) > kNear) continue;
      t = (point[0] - a[0]) / dx;
    } else {
      const float tx = (point[0] - a[0]) / dx, ty = (point[1] - a[1]) / dy;
      if (!(std::fabs(tx - ty) < kNear)) continue;
      if (!(ty > 0.0f) || !(ty < 1.0f) || !(tx > 0.0f) || !(tx < 1.0f)) continue;
      if (std::fabs(tx * dx + a[0] - point[0]) > kNear) continue;
      if (std::fabs(ty * dy + a[1] - point[1]) > kNear) continue;
      return i;
    }
    if (t > 0.0f && t < 1.0f) return i;
  }
  return -1;
}

// Mech3.exe 1.2 @ 0x0050e5a0
int insert_points(ClipPolygon& c, const float* points, int32_t n) {
  int changed = 0;
  for (const float* p = points; n > 0; --n, p += 3) {
    int32_t i = find_point(c, p);
    if (i != -1) {
      std::memcpy(&c.points[3 * i], p, 12);
    } else {
      i = find_edge(c, p);
      if (i == -1) continue;
      c.points.insert(c.points.begin() + 3 * (i + 1), p, p + 3);
      ++c.count;
    }
    changed = 1;
  }
  return changed;
}

// Mech3.exe 1.2 @ 0x0050e910
ClipSet* clip_set_new() { return new ClipSet; }

// Mech3.exe 1.2 @ 0x0050e930
void clip_set_free(ClipSet* s) { delete s; }

// Mech3.exe 1.2 @ 0x0050e850
int commit(ClipSet& s) {
  Hooks& h = hooks();
  for (CellClip& cell : s.cells) {
    for (NodeModel& nm : cell.nodes) {
      zmod::Model* old = h.node_model(nm.node);
      h.node_set_model(nm.node, nm.model);
      if (old) h.model_release(old);
    }
    ++cell.cell->raw[0x39];
    cell.nodes.clear();
    cell.nodes.shrink_to_fit();
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0050dea0
int add_polygon(zmod::Model* m, int32_t n, const float* points, zmod::Material* material, const float* uvs,
                const float* normals, const float* colors) {
  uint8_t zone[4];
  zmod::zone_reset(zone);
  if (n < 3) {
    report_count(0x800, 0xfc, "Attempting to generate polygon with (%d) vertices", n);
    return -1;
  }
  if (!uvs && !material) material = random_material();
  return hooks().model_add(m, n, points, normals, uvs, 0, 0, 0, material, 0, 0, zone, colors);
}

// Mech3.exe 1.2 @ 0x0050f4e0
int add_child(zmod::Model* m, int32_t n, const float* points, const zmod::Model& src, const zmod::Polygon& p) {
  if (n < 3) {
    report_count(0x800, 0x1c8, "Attempting to add child polygon with (%d) vertices", n);
    return -1;
  }
  std::vector<float> uvs, colors, normals;
  zmod::Material* material;
  if (p.uvs) {
    uvs = uvs_at(points, n, src, p);
    colors = colors_at(points, n, src, p);
    if (p.flags >> 9 & 1) normals = normals_at(points, n, src, p);
    for (int32_t i = 0; i < n; ++i) colors_add(points + 3 * i, &colors[3 * i]);
    material = p.material;
  } else {
    material = random_material();
  }
  auto ptr = [](const std::vector<float>& v) { return v.empty() ? nullptr : v.data(); };
  return hooks().model_add(m, n, points, ptr(normals), ptr(uvs), 0, 0, 0, material, p.priority, p.flags >> 8 & 1,
                           reinterpret_cast<const uint8_t*>(&p.zone_set), ptr(colors));
}

// Mech3.exe 1.2 @ 0x0050f640
int copy_polygon(zmod::Model* m, const zmod::Model& src, const zmod::Polygon& p) {
  const int32_t n = static_cast<int32_t>(p.flags & 0xff);
  std::vector<float> points, normals;
  polygon_points(src, p, points);
  if (p.flags >> 9 & 1) {
    normals.resize(3 * static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) std::memcpy(&normals[3 * i], src.normals + p.normal_indices[i] * 3, 12);
  }
  std::vector<float> colors(p.colors, p.colors + 3 * n);
  return hooks().model_add(m, n, points.empty() ? nullptr : points.data(), normals.empty() ? nullptr : normals.data(),
                           p.uvs, 0, 0, 0, p.material, p.priority, p.flags >> 8 & 1,
                           reinterpret_cast<const uint8_t*>(&p.zone_set), colors.empty() ? nullptr : colors.data());
}

namespace {
void report_text(int level, int line, const char* format, ...) {
  char text[256];
  va_list ap;
  va_start(ap, format);
  std::vsnprintf(text, sizeof text, format, ap);
  va_end(ap);
  zerr::report(level, kFile, line, text);
}
float nodef(const zcls::Node* node, int off) { return node->get<float>(off); }
// A node with a box (flag 0x200) away from the outline's box by more than
// 1 (the node's z is the plane's -y).
bool box_away(const ClipPolygon& c, const zcls::Node* node) {
  return c.box[2] - -1.0f < nodef(node, 0x8c) || nodef(node, 0x98) < c.box[0] - 1.0f ||
         -nodef(node, 0x94) < c.box[1] - 1.0f || c.box[3] - -1.0f < -nodef(node, 0xa0);
}
Weiler* weiler_of(ClipPolygon& c) { return static_cast<Weiler*>(c.weiler); }
}  // namespace

// Mech3.exe 1.2 @ 0x0050e950
zmod::Model* crater_node(const CellClip& cell, zcls::Node* world, zcls::Node** node) {
  zcls::Node* n = zcls::object3d_create();
  if (!n) {
    if (node) *node = nullptr;
    return nullptr;
  }
  n->set_flags(n->flags() | 0x8000);
  if (node) *node = n;
  zcls::node_set_zone(n, 0xff);
  for (const NodeModel& nm : cell.nodes) {
    if (!(nm.node->flags() & 0x10000)) continue;
    const uint8_t zone = nm.node->raw[0x30];  // 0x004eb6a0
    if (zone != 0xff) {
      zcls::node_set_zone(n, zone);
      break;
    }
  }
  zcls::node_set_clip_to(n, 1);
  zmod::Model* m = hooks().model_alloc();
  if (!m) {
    if (node) *node = nullptr;
    zcls::node_release(n);  // 0x004f3390
    return nullptr;
  }
  zcls::node_add_child(world, n);
  hooks().node_set_model(n, m);
  return m;
}

// Mech3.exe 1.2 @ 0x0050ea50. A skipped polygon's report gives the last
// polygon's count, not its own.
int snap_outline(ClipPolygon& c, zcls::Node* node) {
  if (!node) return 0;
  zmod::Model* m = node->model;
  if (!m) return 0;
  if ((node->flags() & 0x200) && box_away(c, node)) return 0;
  int snapped = 0;
  uint32_t last = 0;
  std::vector<float> pts;
  for (uint32_t i = 0; i < m->count(0x10); ++i) {
    const zmod::Polygon& p = m->polygons[i];
    const int32_t n = static_cast<int32_t>(p.flags & 0xff);
    if (n < 3) {
      report_text(0x400, 0x447, "Skipping clip of polygon with (%d) verts", last);
      continue;
    }
    polygon_points(*m, p, pts);
    to_plane(pts.data(), n);
    float box[4];
    box_of(box, pts.data(), n);
    if (boxes_touch(box, c.box) && snap(pts.data(), n, c.points.data(), c.count, 0.1f, 0.1f)) snapped = 1;
    last = static_cast<uint32_t>(n);
  }
  return snapped;
}

// Mech3.exe 1.2 @ 0x0050f7b0
int clear_of(ClipPolygon& c, zmod::Model* m) {
  if (!m) return 0;
  int ok = 1;
  std::vector<float> pts;
  for (uint32_t i = 0; i < m->count(0x10) && ok; ++i) {
    const zmod::Polygon& p = m->polygons[i];
    const int32_t n = static_cast<int32_t>(p.flags & 0xff);
    if (n < 3) {
      report_text(0x400, 0x6b0, "Skipping clip of polygon with (%d) verts", n);
      continue;
    }
    polygon_points(*m, p, pts);
    to_plane(pts.data(), n);
    float box[4];
    box_of(box, pts.data(), n);
    Result r;
    const int code = boxes_touch(box, c.box) ? clip(*weiler_of(c), 4, pts.data(), n, &r) : 1;
    if (code == 0) {
      report_text(0x200, 0x6cf, "Weiler algorithm clip error occurred.");
      ok = 0;
    } else if (code == 2) {
      if (r.count(2) == 0) report_text(0x200, 0x6d7, "Intersection found, no polygons...");
      ok = 0;
    } else if (code == 3 || code == 4) {
      ok = 0;
    }
    free_result(r);
  }
  return ok;
}

// Mech3.exe 1.2 @ 0x0050f120. The Weiler is made again from the outline
// before the next polygon once cut points joined it (within this model
// only: the flag starts at 0 for each).
int clip_model(ClipPolygon& c, zmod::Model* src, zmod::Model** out) {
  if (!src) return 1;
  zmod::Model* m = hooks().model_alloc();
  if (!m) return 0;
  std::memset(m->header, 0, 4);   // 0x00539cc0(m, 0)
  zmod::expand_strips(*src);      // 0x00553160
  int changed = 0, renew = 0;
  std::vector<float> pts;
  for (uint32_t i = 0; i < src->count(0x10); ++i) {
    const zmod::Polygon& p = src->polygons[i];
    const int32_t n = static_cast<int32_t>(p.flags & 0xff);
    if (n < 3) {
      report_text(0x400, 0x5b8, "Skipping clip of polygon with (%d) verts", n);
      continue;
    }
    polygon_points(*src, p, pts);
    to_plane(pts.data(), n);
    float box[4];
    box_of(box, pts.data(), n);
    Result r;
    int code = 1;
    if (boxes_touch(box, c.box)) {
      if (renew) {
        Weiler* w = weiler_of(c);
        weiler_renew(&w, c.points.data(), c.count);
        c.weiler = w;
        renew = 0;
      }
      code = clip(*weiler_of(c), 3, pts.data(), n, &r);
    }
    switch (code) {
      case 0:  // the exe frees the polygon buffer but not the Result (0x0050f470)
        hooks().model_release(m);
        return 0;
      case 1:
        copy_polygon(m, *src, p);
        break;
      case 2: {
        changed = 1;
        // The exe reads the first inside contour unchecked; the clip always
        // leaves one with code 2.
        if (r.count(0) && insert_points(c, &r.points[static_cast<size_t>(r.lists[0][0].start)], r.lists[0][0].count))
          renew = 1;
        Convex cv;
        if (convexify(r.lists[1].data(), r.count(1), r.point_count(), r.points.data(), cv)) {
          from_plane(cv.points.data(), static_cast<int32_t>(cv.points.size() / 3));
          size_t k = 0;  // the exe steps on only past pieces of 3 or more
          for (size_t j = 0; j < cv.polys.size(); ++j) {
            const ResultContour& piece = cv.polys[k];
            if (piece.count < 3) continue;
            add_child(m, piece.count, &cv.points[static_cast<size_t>(piece.start)], *src, p);
            ++k;
          }
        }
        break;
      }
      case 3:
        changed = 1;
        break;
      case 4: {
        if (r.count(1) == 0) {
          zerr::report(0x100, kFile, 0x629, "\nWEILER_CLIP_IN_SUBJ\n\tclip.outside.num_polys = 0\n");
          hooks().model_release(m);
          return 0;
        }
        changed = 1;
        Weiler* w = weiler_of(c);
        const int32_t nr = w->region_count;
        RingTriangles ring = ring_triangles(n, pts.data(), nr, w->region.data());
        if (insert_points(c, w->region.data(), nr)) renew = 1;
        if (ring.count < n + nr) {
          hooks().model_release(m);
          return 0;
        }
        for (int32_t t = 0; t < ring.count; ++t) {
          float* tri = &ring.points[9 * static_cast<size_t>(t)];
          face_up(tri, 3, 1);
          from_plane(tri, 3);
          add_child(m, 3, tri, *src, p);
        }
        break;
      }
      default:
        break;
    }
    free_result(r);
  }
  if (!changed) {
    hooks().model_release(m);
    m = nullptr;
  }
  *out = m;
  return 1;
}

// Mech3.exe 1.2 @ 0x0050ef70
int clip_node(ClipPolygon& c, zcls::Node* node, zmod::Model** out) {
  if (!node || !node->model) return 1;
  const uint32_t flags = node->flags();
  if ((flags & 0x200) && box_away(c, node)) return 1;
  if (flags & 0x20000) {
    *out = nullptr;
    return clear_of(c, node->model);
  }
  if (flags & 0x10000) {
    const int r = clip_model(c, node->model, out);
    zcls::Frames& f = zcls::frames();
    if (r && node->get<uint32_t>(0xc4) <= f.frame) {
      f.expiry = f.frame + 0xa0;
      zcls::set_expiry(node);
    }
    return r;
  }
  return 1;
}

// Mech3.exe 1.2 @ 0x0050ec10. The world's own children come before the
// cell's nodes.
int clip_cell(const float* points, int32_t n, zcls::Partition* cell, ClipSet& set) {
  if (!cell || !points) {
    report_text(0x100, 0x48b, "Null Area Partition (0x%08x) or null Outline (0x%08x) passed to ClipPatch", 0u, 0u);
    return -1;
  }
  ClipPolygon* c = clip_polygon_new(points, n);
  set.cells.emplace_back();
  CellClip& cc = set.cells.back();
  cc.cell = cell;
  const int16_t cell_nodes = cell->get<int16_t>(0x3a);
  // FIX (bug): the exe sizes the changed-node list by the cell's node
  // count only, though the world's own ground nodes may add to it.
  cc.nodes.reserve(static_cast<size_t>(cell_nodes));
  std::vector<zcls::Node*> keep, cut;  // flag 0x20000, 0x10000 (both need 4)
  auto sort = [&](zcls::Node* node) {
    if (!node) return;
    const uint32_t f = node->flags();
    if (!(f & 4)) return;
    if (f & 0x20000) keep.push_back(node);
    if (f & 0x10000) cut.push_back(node);
  };
  zcls::Node* world = hooks().world_node ? hooks().world_node() : nullptr;
  for (int32_t i = 0; world && i < world->child_count(); ++i) sort(world->children[i]);
  for (int16_t i = 0; i < cell_nodes; ++i) sort(cell->nodes[i]);
  if (world && hooks().collect_clip_nodes) {
    std::vector<zcls::Node*> extra;
    hooks().collect_clip_nodes(world, cell, extra);
    for (zcls::Node* node : extra) {
      if (!node) continue;
      if (std::find(keep.begin(), keep.end(), node) != keep.end()) continue;
      if (std::find(cut.begin(), cut.end(), node) != cut.end()) continue;
      sort(node);
    }
  }
  for (zcls::Node* node : cut)
    if (snap_outline(*c, node)) box_of(c->box, c->points.data(), c->count);
  c->weiler = weiler_new(c->points.data(), c->count, 0);
  int ok = 1;
  zmod::Model* unused = nullptr;
  for (size_t i = 0; i < keep.size() && ok; ++i) ok = clip_node(*c, keep[i], &unused);
  for (size_t i = 0; i < cut.size() && ok; ++i) {
    NodeModel nm{cut[i], nullptr};
    ok = clip_node(*c, cut[i], &nm.model);
    if (nm.model) cc.nodes.push_back(nm);
  }
  int32_t count = static_cast<int32_t>(cc.nodes.size());
  if (count == 0 || !ok) {
    set.cells.pop_back();
    count = 0;
  } else {
    clip_polygon_out(*c, &set.point_count, set.points);
  }
  clip_polygon_free(c);
  return count;
}

}  // namespace zgeo
