#include "gamez/zmodel/gmod_draw.h"
#include "platform/render3d.h"
#include "gamez/zmodel/gmod_geometry.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

#include "crt/msvc_math.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_bake.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zrender/zrndr_flare.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zvideo/zvid_ddd3d.h"

// Semantics, quirks and the emulator check: docs/drafts/modeldraw.md. The
// game runs the x87 at 24 bits (CLAUDE.md), so float steps in the original's
// order reproduce it; no fused multiply-adds.
#pragma STDC FP_CONTRACT OFF

namespace zmod {

namespace {
DrawBuffers g_buf;
DrawState g_state;
DrawTable g_table;
DrawHooks g_hooks;

constexpr double kVisible = 0.003921569;  // 0x005ce1f8, 1/255

template <typename T> T field(const Model& m, int off) {
  T v;
  std::memcpy(&v, m.header + off, sizeof v);
  return v;
}
template <typename T> T node_field(const zcls::Node& n, int off) { return n.get<T>(off); }
template <typename T> T light_field(const Light& l, int off) {
  T v;
  std::memcpy(&v, l.raw + off, sizeof v);
  return v;
}
template <typename T> void set_light_field(Light& l, int off, T v) { std::memcpy(l.raw + off, &v, sizeof v); }

// The x87 compares (fcomp; test ah, 1): "below" is also taken on NaN.
bool below(float a, float b) { return !(a >= b); }

// The image's flag 2 (alpha), zimg::Texture +9: [[material +0x10]] +9.
bool texture_alpha(const Material& m) {
  // The original dereferences without a test; a null texture or image would
  // crash there. FIX (bug): treated as no alpha.
  if (!m.texture || !m.texture->image) return false;
  return (static_cast<const zimg::Texture*>(m.texture->image)->flags & 2) != 0;
}
void* texture_record(const Material& m) { return m.texture ? m.texture->surface : nullptr; }

ScreenClip screen(const ClipPlanes& c) {
  ScreenClip s;
  std::memcpy(&s, &c, sizeof s);
  return s;
}

// The clippers each port their own copy of the shared buffers (0x72f41c,
// 0x7481a8, 0x7489a8..): clip2d() is the draw's, clip3d_buffers() gets them
// around a camera-space clip. The original has one set.
template <typename F> bool clip3d(F fn, int32_t& count) {
  Clip3dBuffers& c = clip3d_buffers();
  Clip2D& d = clip2d();
  for (int i = 0; i < kClipVerts; ++i) {
    std::memcpy(c.rec[i], &d.work[i], 0x20);
    c.tl[i][6] = d.tl[i].tu, c.tl[i][7] = d.tl[i].tv;
  }
  c.shadow_count = d.shadow.count;
  for (int s = 0; s < kShadowSets; ++s)
    for (int i = 0; i < kShadowVerts; ++i)
      c.shadow_uv[Clip3dBuffers::kVerts * s + i][0] = d.shadow.uv[s][i].u,
      c.shadow_uv[Clip3dBuffers::kVerts * s + i][1] = d.shadow.uv[s][i].v;
  const bool r = fn(g_state.clip, count);
  for (int i = 0; i < kClipVerts; ++i) {
    std::memcpy(&d.work[i], c.rec[i], 0x20);
    d.tl[i].tu = c.tl[i][6], d.tl[i].tv = c.tl[i][7];
  }
  for (int s = 0; s < kShadowSets; ++s)
    for (int i = 0; i < kShadowVerts; ++i)
      d.shadow.uv[s][i].u = c.shadow_uv[Clip3dBuffers::kVerts * s + i][0],
      d.shadow.uv[s][i].v = c.shadow_uv[Clip3dBuffers::kVerts * s + i][1];
  return r;
}

// Projection into the TL buffer: w = near / z, sx = fx' * x * w + cx (fx' =
// fx / near on Direct3D), sy alike, sz = w. `w_first` is the order of the
// untextured lit path (0x541497): (fx' * w) * x.
void project(int32_t count, bool w_first) {
  const zmth::Projection& p = zmth::projection();
  const float(*cam)[3] = clip3d_buffers().cam;
  TLVertex* tl = clip2d().tl;
  for (int32_t i = 0; i < count; ++i) {
    const float w = p.near_z / cam[i][2];
    tl[i].sz = w;
    const float ax = w_first ? p.fx_near * w : p.fx_near * cam[i][0];
    const float bx = w_first ? ax * cam[i][0] : ax * w;
    tl[i].sx = bx + p.cx;
    const float ay = cam[i][1] * p.fy_near;
    const float by = ay * w;
    tl[i].sy = by + p.cy;
  }
}

// Polygon +4: sz *= 1 + priority * [0x74a284] (rhw follows in the draw functions).
void priority_bias(int32_t priority, int32_t count) {
  if (priority == 0) return;
  const float scaled = static_cast<float>(priority) * g_state.priority_scale;
  const float f = scaled - -1.0f;
  for (int32_t i = count - 1; i >= 0; --i) clip2d().tl[i].sz = f * clip2d().tl[i].sz;
}

// n = (b0 - b1) x (b2 - b1) of the first three camera-space vertices, and
// n . b0 (the camera is at the origin).
float backface(float n[3]) {
  const float(*v)[3] = clip3d_buffers().cam;
  const float e2x = v[2][0] - v[1][0], e2y = v[2][1] - v[1][1], e2z = v[2][2] - v[1][2];
  const float e0x = v[0][0] - v[1][0], e0y = v[0][1] - v[1][1], e0z = v[0][2] - v[1][2];
  float a = e0y * e2z, b = e0z * e2y;
  n[0] = a - b;
  a = e0z * e2x, b = e0x * e2z;
  n[1] = a - b;
  a = e0x * e2y, b = e0y * e2x;
  n[2] = a - b;
  const float px = n[0] * v[0][0], py = n[1] * v[0][1], pz = n[2] * v[0][2];
  const float s = px + py;
  return s + pz;
}

int32_t alpha_of(const Material& m) {
  const float a = static_cast<float>(m.alpha) * g_state.opacity;
  return crt::msvc_ftol(a);
}

void copy3(float* d, const float* s) { std::memcpy(d, s, 12); }

// The vertex transform inlined at 0x53f3a9 / 0x53f8fb, as
// zmth::transform_points_to but compiled here without fused multiply-adds
// (zmth_matrix.cpp has no FP_CONTRACT pragma, so its sums round differently
// on arm64). The identity copy is dead (0x51fb30 clears the flag).
void transform_point(const float* v, const float* t, float* out) {
  const float x0 = v[0] * t[0], x1 = v[0] * t[1], x2 = v[0] * t[2];
  const float y0 = v[1] * t[3], y1 = v[1] * t[4], y2 = v[1] * t[5];
  const float z0 = v[2] * t[6], z1 = v[2] * t[7], z2 = v[2] * t[8];
  const float a0 = x0 + y0, a1 = x1 + y1, a2 = x2 + y2;
  const float b0 = a0 + z0, b1 = a1 + z1, b2 = a2 + z2;
  out[0] = b0 + t[9], out[1] = b1 + t[10], out[2] = b2 + t[11];
}

void transform_vertices(const Model& m, uint32_t count) {
  const float* t = zmth::matrix_stack().matrix().m;
  if (zmth::matrix_stack().is_identity()) {
    std::memcpy(g_buf.cam, m.vertices, 12 * count);
    return;
  }
  for (uint32_t i = 0; i < count; ++i) transform_point(m.vertices + 3 * i, t, g_buf.cam[i]);
}

// Model flag 8 with a morph factor and count: blend, then transform in place.
// The x87 test (fcomp 0.0, C3) also takes a NaN factor as zero.
bool morphing(const Model& m) {
  const float f = field<float>(m, 0x24);
  return (field<uint8_t>(m, 8) & 8) && (f < 0.0f || f > 0.0f) && field<uint32_t>(m, 0x1c) != 0;
}

// zmth::morph (0x5220e0) then zmth::transform_points (0x5227a0), written out
// here for the same reason as transform_vertices: no fused sums.
void morph_vertices(const Model& m) {
  const uint32_t k = field<uint32_t>(m, 0x1c), n = field<uint32_t>(m, 0x14);
  const float f = field<float>(m, 0x24);
  for (uint32_t i = 0; i < k; ++i)
    for (int j = 0; j < 3; ++j) {
      const float d = m.morphs[3 * i + j] * f;
      g_buf.cam[i][j] = m.vertices[3 * i + j] + d;
    }
  if (zmth::matrix_stack().is_identity()) return;
  const float* t = zmth::matrix_stack().matrix().m;
  for (uint32_t i = 0; i < n; ++i) {
    const float v[3] = {g_buf.cam[i][0], g_buf.cam[i][1], g_buf.cam[i][2]};
    transform_point(v, t, g_buf.cam[i]);
  }
}

// Mech3.exe 1.2 @ 0x00541df0 (ECX vertex, EDX colour, the light): one model
// light as a point, the hardware path (the software one plots into the flare
// list). The vertex to camera space by the stack's top (unless it is the
// identity), past the near plane, projected, inside the point rect; its depth
// biased by the light's priority (+0x18) as polygons are; faded out between
// +0x30 and +0x34 at the rate +0x38. A light with +0x3c also goes on the
// lens-flare list (0x00567420) with its world point (camera space by
// 0x0073fc00) and that screen point.
void model_light(const float* vertex, uint32_t color, Light& l) {
  DrawState& s = g_state;
  zmth::MatrixStack& st = zmth::matrix_stack();
  float c[3] = {vertex[0], vertex[1], vertex[2]};
  if (!st.is_identity()) transform_point(vertex, st.matrix().m, c);
  if (!(c[2] > s.clip.near_z)) return;
  const zmth::Vec3 cam{c[0], c[1], c[2]};
  zmth::Vec3 sc;
  zmth::project_d3d(&cam, &sc, 1);
  if (below(sc.x, s.point_rect[0]) || below(sc.y, s.point_rect[1])) return;
  if (sc.x > s.point_rect[2] || sc.y > s.point_rect[3]) return;
  const float priority = static_cast<float>(light_field<int16_t>(l, 0x18));
  const float screen_pt[3] = {sc.x, sc.y, (priority * s.priority_scale - -1.0f) * sc.z};  // 0x005ce1dc
  int32_t alpha = 0xff;
  const float near_ = light_field<float>(l, 0x30), far_ = light_field<float>(l, 0x34);
  if (far_ != 0.0f) {
    if (!(c[2] < far_)) alpha = 0;
    else if (c[2] > near_) alpha = static_cast<int32_t>((far_ - c[2]) * light_field<float>(l, 0x38));  // ftol
  }
  if (alpha != 0 && g_table.point) g_table.point(g_table.ctx, screen_pt, color & 0xffff, alpha);
  if (light_field<int32_t>(l, 0x3c) != 0) {
    float world[3];
    transform_point(c, zmth::view().camera.m, world);  // 0x00541ea8..
    if (g_hooks.flare) g_hooks.flare(g_hooks.ctx, world, screen_pt, color & 0xffff, &l);
    else zrndr::flare_add(world, screen_pt, color & 0xffff, l.raw + 0x30);
  }
}

// The model lights (+0x20, 0x4c each): type 0 blinks by swapping its two
// colours, type 1 steps along its vertices drawing the previous one in the
// second colour.
// The test hook stands in for the draw when set.
void light(const float* vertex, uint32_t color, Light& l) {
  if (g_hooks.model_light) g_hooks.model_light(g_hooks.ctx, vertex, color, &l);
  else model_light(vertex, color, l);
}

void animate_lights(Model& m) {
  const int32_t n = field<int32_t>(m, 0x20);
  for (int32_t i = 0; i < n; ++i) {
    Light& l = m.lights[i];
    const int32_t type = light_field<int32_t>(l, 0);
    const float dt = ztimer::state().dt;
    if (type == 0) {
      if (light_field<int32_t>(l, 4) == 1) {
        const float t = dt + light_field<float>(l, 0x10);
        set_light_field(l, 0x10, t);
        if (t > light_field<float>(l, 8)) {
          const uint32_t pair = light_field<uint32_t>(l, 0x14);
          set_light_field<float>(l, 0x10, 0.0f);
          set_light_field<uint32_t>(l, 0x14, (pair & 0xffff) | (static_cast<uint32_t>(l.color) << 16));
          l.color = static_cast<uint16_t>(pair >> 16);
        }
      }
      light(l.vertices, l.color, l);
    } else if (type == 1) {
      const float t = dt + light_field<float>(l, 0x10);
      set_light_field(l, 0x10, t);
      if (t > light_field<float>(l, 8)) {
        uint32_t k = light_field<uint32_t>(l, 0x14) + 1;  // the whole dword: 0xffff carries into the colour
        if (static_cast<int32_t>(k & 0xffff) >= light_field<int32_t>(l, 0xc)) k &= 0xffff0000u;
        set_light_field(l, 0x14, k);
        set_light_field<float>(l, 0x10, 0.0f);
      }
      const uint32_t k = light_field<uint32_t>(l, 0x14);
      const uint32_t at = k & 0xffff;
      light(l.vertices + 3 * at, l.color, l);
      if (at != 0) light(l.vertices + 3 * (at - 1), k >> 16, l);
    }
  }
}

// The flat and lit untextured draws drawn again into a second rect, scaled
// and offset (0x541666..0x541719); [0x74a32c] is never set in the shipped
// game as far as known.
void second_pass(const Material& mat, int32_t priority, int32_t count) {
  DrawState& s = g_state;
  if (!s.second_pass || !screen_reject(s.second, count)) return;
  if (!clip2d_flat(screen(s.second), &count)) return;
  if (count > 0) {
    const float r = -1.0f / s.clip.near_z;
    for (int32_t i = 0; i < count; ++i) {
      TLVertex& v = clip2d().tl[i];
      const float x = s.second_scale[0] * v.sx;
      v.sx = x + s.second_offset[0];
      const float y = s.second_scale[1] * v.sy;
      v.sy = y + s.second_offset[1];
      v.sz = v.sz - r;
    }
  }
  if (g_table.flat) g_table.flat(g_table.ctx, mat.rgb, alpha_of(mat), count, priority, s.overlay);
}

}  // namespace

// Mech3.exe 1.2 @ 0x00540552..0x005408d8 (and the copy at 0x00540ef6..):
// the receiver test inlined in 0x0053f2e0. The polygon (camera space,
// reversed) goes to world space; each caster in the walk's list, last first,
// whose top it does not clear, whose light it faces and whose box it meets
// once its low or near-side vertices slide down the light onto the caster's
// ground, gives one UV set and texture. Seven at most. docs/render-shadows.md.
void collect_shadows(int32_t count, const float n[3]) {
  ShadowSets& out = clip2d().shadow;
  const ShadowCasters& casters = shadow_casters();
  if (casters.count == 0) return;
  const float(*cam)[3] = clip3d_buffers().cam;
  const float* m = zmth::view().camera.m;  // 0x0073fc00
  const float* off = lighting().camera_offset;  // 0x0073fbb0
  float world[kClipVerts][3];
  float lowest = 3.4028235e+38f;
  for (int32_t i = count - 1; i >= 0; --i) {
    transform_point(cam[i], m, world[i]);
    if (lowest > world[i][1]) lowest = world[i][1];
  }
  float ground[kClipVerts][2];
  for (int32_t c = casters.count - 1; c >= 0; --c) {
    const ShadowCaster& s = *casters.list[c];
    if (lowest > s.box_max[1]) continue;  // the whole polygon above the caster's top
    const float f0 = n[0] * s.slope_cam[0], f1 = n[1] * s.slope_cam[1], f2 = n[2] * s.slope_cam[2];
    const float f01 = f0 + f1;
    const float facing = f01 + f2;
    if (!(facing > 0.0f)) continue;
    // Vertices on the light's side of a line through the caster (2 slope
    // lengths beyond it) are all slid down, not just those below its ground.
    const float px = s.position[0] + off[0], pz = s.position[2] + off[2];
    const float lx = px * s.slope[0], lz = pz * s.slope[1];
    const float l = lx + lz;
    const float r2 = s.slope_len * -2.0f;
    const float limit = l - r2;
    bool slide_all = false;
    for (int32_t i = count - 1; i >= 0; --i) {
      const float a = world[i][2] * s.slope[1], b = world[i][0] * s.slope[0];
      if (!(a + b > limit)) {
        slide_all = true;
        break;
      }
    }
    float max_x = -3.4028235e+38f, min_x = 3.4028235e+38f, min_z = 3.4028235e+38f, max_z = -3.4028235e+38f;
    for (int32_t i = count - 1; i >= 0; --i) {
      ground[i][0] = world[i][0];
      ground[i][1] = world[i][2];
      if (slide_all || below(world[i][1], s.ground_y)) {
        const float t = s.ground_y - world[i][1];
        const float dx = t * s.slope[0];
        ground[i][0] = dx + ground[i][0];
        const float dz = t * s.slope[1];
        ground[i][1] = dz + ground[i][1];
      }
      if (min_x > ground[i][0]) min_x = ground[i][0];
      if (below(max_x, ground[i][0])) max_x = ground[i][0];
      if (min_z > ground[i][1]) min_z = ground[i][1];
      if (below(max_z, ground[i][1])) max_z = ground[i][1];
    }
    const float x0 = s.box_min[0] + off[0], z0 = s.box_min[2] + off[2];
    if (!(max_x > x0) || !(max_z > z0)) continue;
    const float x1 = s.box_max[0] + off[0], z1 = s.box_max[2] + off[2];
    if (!(x1 > min_x) || !(z1 > min_z)) continue;
    ShadowUV* uv = out.uv[out.count];
    for (int32_t i = 0; i < count; ++i) {
      const float u = ground[i][0] - x0;
      uv[i].u = u * s.scale[0];
      const float v = ground[i][1] - z0;
      uv[i].v = v * s.scale[1];
    }
    out.texture[out.count] = s.texture;
    if (++out.count >= kShadowSets) return;
  }
}

namespace {

// 0x545720 (gmod_light) on this polygon's buffers.
int32_t shade(uint32_t* baked, float n[3], int32_t count, uint32_t* fogged, uint32_t* per_vertex, int32_t spot,
              uint32_t needs) {
  lighting().vertex_normals = g_buf.poly_normals;
  return shade_polygon(reinterpret_cast<const float*>(baked), n, count, fogged, per_vertex, spot, needs);
}

void draw_untextured(const Model& m, const Polygon& p, uint32_t needs, uint32_t fogged, uint32_t per_vertex,
                     bool cache, int32_t count, float n[3]) {
  DrawState& s = g_state;
  VertexWork* w = clip2d().work;
  Material& mat = *p.material;
  if (needs & 6) {
    if (field<uint8_t>(m, 8) & 0x20) per_vertex |= 4;
    uint32_t* c = cache && !(p.flags & 0x100) ? p.scratch : nullptr;
    if (shade(c, n, count, &fogged, &per_vertex, 0, needs)) fogged |= 2;
    per_vertex &= ~4u;
  } else if (fogged) {
    w[0].light[0] = 0;
    if (per_vertex && count > 1)
      for (int32_t i = 1; i < count; ++i) w[i].light[0] = 0;
  }
  if (fogged) {
    if ((s.clip.mask & 0x30) && !clip3d(clip3d_lit, count)) return;
    project(count, true);
    if ((s.clip.mask & 0xf) && !clip2d_lit(screen(s.clip), &count)) return;
    priority_bias(p.priority, count);
    if (g_table.lit)
      g_table.lit(g_table.ctx, mat.rgb, &mat.r, fogged, alpha_of(mat), count, p.priority, s.overlay);
  } else {
    if ((s.clip.mask & 0x30) && !clip3d(clip3d_flat, count)) return;
    project(count, false);
    if ((s.clip.mask & 0xf) && !clip2d_flat(screen(s.clip), &count)) return;
    priority_bias(p.priority, count);
    if (g_table.flat) g_table.flat(g_table.ctx, mat.rgb, alpha_of(mat), count, p.priority, s.overlay);
  }
  second_pass(mat, p.priority, count);
}

// One strip triangle from the saved strip: vertices k, a, b with a/b
// alternating by parity, so every triangle keeps the strip's winding.
// Returns false when it faces away (strips have no two-sided test).
bool strip_triangle(int32_t k, int32_t parity, int32_t& a, int32_t& b, float n[3]) {
  const int32_t odd = parity & 1;
  a = k - odd - 1;
  b = odd + k - 2;
  float(*cam)[3] = clip3d_buffers().cam;
  copy3(cam[0], g_buf.save_cam[k]);
  copy3(cam[1], g_buf.save_cam[a]);
  copy3(cam[2], g_buf.save_cam[b]);
  const float d = backface(n);
  return -g_state.backface > d;
}

// After the uniform-light clip: the vertices it added take vertex 0's point
// light and fog (0x540946, 0x540a29).
void fill_uniform(int32_t from, int32_t count) {
  VertexWork* w = clip2d().work;
  for (int32_t i = from; i < count; ++i) w[i].light[1] = w[0].light[1], w[i].light[2] = w[0].light[2];
}

// Textured, fogged or lit (0x53fffd..0x540b6d).
void draw_textured_lit(const Polygon& p, uint32_t fogged, uint32_t per_vertex, int32_t spot, bool receiver,
                       bool strip, int32_t count, float n[3]) {
  DrawState& s = g_state;
  DrawBuffers& b = g_buf;
  Clip2D& d = clip2d();
  Material& mat = *p.material;
  const bool split = strip && !(s.clip.mask == 0 && !below(s.opacity, 1.0f) && !texture_alpha(mat) && !spot);
  int32_t total = count, k = count - 1;
  if (split) {
    for (int32_t i = 0; i < count; ++i) {
      copy3(b.save_cam[i], clip3d_buffers().cam[i]);
      b.save_uv[i][0] = d.tl[i].tu, b.save_uv[i][1] = d.tl[i].tv;
      std::memcpy(&b.save_work[i], &d.work[i], 0x18);
    }
    if (b.poly_normals)
      for (int32_t i = 0; i < count; ++i) copy3(b.save_normal[i], b.poly_normal[i]);
    zvid::device_state().primitive = 6;
  }
  int32_t parity = total - k;
  do {
    if (split) {
      if (k < 2) break;
      int32_t ia, ib;
      if (!strip_triangle(k, parity, ia, ib, n)) {
        --k, ++parity;
        continue;
      }
      const int32_t idx[3] = {k, ia, ib};
      for (int j = 0; j < 3; ++j) {
        d.tl[j].tu = b.save_uv[idx[j]][0], d.tl[j].tv = b.save_uv[idx[j]][1];
        std::memcpy(&d.work[j], &b.save_work[idx[j]], 0x18);
        if (b.poly_normals) copy3(b.poly_normal[j], b.save_normal[idx[j]]);
      }
      --k, ++parity;
      count = 3;
    }
    d.shadow.count = 0;
    if (receiver && mat.texture && mat.texture->image && !texture_alpha(mat) && g_hooks.collect_shadows)
      g_hooks.collect_shadows(g_hooks.ctx, count, n);
    const bool shadowed = d.shadow.count != 0;
    if (s.clip.mask & 0x30) {
      const int32_t before = count;
      const bool ok = per_vertex ? clip3d(shadowed ? clip3d_textured_lit_shadow : clip3d_textured_lit, count)
                                 : clip3d(shadowed ? clip3d_textured_shadow : clip3d_textured, count);
      if (!ok) continue;
      if (!per_vertex && before < count) fill_uniform(before, count);
    }
    project(count, false);
    if (s.clip.mask & 0xf) {
      const int32_t before = count;
      const ScreenClip r = screen(s.clip);
      const int32_t ok = per_vertex ? (shadowed ? clip2d_textured_lit_shadowed(r, &count) : clip2d_textured_lit(r, &count))
                                    : (shadowed ? clip2d_textured_shadowed(r, &count) : clip2d_textured(r, &count));
      if (!ok) continue;
      if (!per_vertex && before < count) fill_uniform(before, count);
    }
    if (spot && g_hooks.projected_draw &&
        g_hooks.projected_draw(g_hooks.ctx, count, per_vertex, fogged, &mat, p.priority))
      continue;
    void* tex = texture_record(mat);
    priority_bias(p.priority, count);
    if (per_vertex) {
      if (shadowed) {
        if (g_table.shadowed_vertex) g_table.shadowed_vertex(g_table.ctx, fogged, count, tex);
      } else if (g_table.textured_vertex) {
        g_table.textured_vertex(g_table.ctx, fogged, count, tex, s.opacity, s.overlay);
      }
    } else if (shadowed) {
      if (g_table.shadowed_lit) g_table.shadowed_lit(g_table.ctx, fogged, count, tex);
    } else if (g_table.textured_lit) {
      g_table.textured_lit(g_table.ctx, fogged, count, tex, p.priority, s.opacity, s.overlay);
    }
  } while (split);
  if (split && b.poly_normals)
    for (int32_t i = 0; i < total; ++i) copy3(b.poly_normal[i], b.save_normal[i]);
}

// Textured, unlit and unfogged (0x540b72..0x5413ae).
void draw_textured(const Polygon& p, bool receiver, bool strip, int32_t count, float n[3]) {
  DrawState& s = g_state;
  DrawBuffers& b = g_buf;
  Clip2D& d = clip2d();
  Material& mat = *p.material;
  const bool split = strip && !(s.clip.mask == 0 && !below(s.opacity, 1.0f) && !texture_alpha(mat));
  int32_t total = count, k = count - 1;
  if (split) {
    for (int32_t i = 0; i < count; ++i) {
      copy3(b.save_cam[i], clip3d_buffers().cam[i]);
      b.save_uv[i][0] = d.tl[i].tu, b.save_uv[i][1] = d.tl[i].tv;
      std::memcpy(b.save_work[i].color, d.work[i].color, 12);
    }
    zvid::device_state().primitive = 6;
  }
  int32_t parity = total - k;
  do {
    if (split) {
      if (k < 2) break;
      int32_t ia, ib;
      if (!strip_triangle(k, parity, ia, ib, n)) {
        --k, ++parity;
        continue;
      }
      const int32_t idx[3] = {k, ia, ib};
      for (int j = 0; j < 3; ++j) {
        d.tl[j].tu = b.save_uv[idx[j]][0], d.tl[j].tv = b.save_uv[idx[j]][1];
        std::memcpy(d.work[j].color, b.save_work[idx[j]].color, 12);
      }
      --k, ++parity;
      count = 3;
    }
    d.shadow.count = 0;
    if (receiver && !texture_alpha(mat) && g_hooks.collect_shadows) g_hooks.collect_shadows(g_hooks.ctx, count, n);
    const bool shadowed = d.shadow.count != 0;
    if ((s.clip.mask & 0x30) && !clip3d(shadowed ? clip3d_textured_shadow : clip3d_textured, count)) continue;
    project(count, false);
    if (s.clip.mask & 0xf) {
      const ScreenClip r = screen(s.clip);
      if (!(shadowed ? clip2d_textured_shadowed(r, &count) : clip2d_textured(r, &count))) continue;
    }
    priority_bias(p.priority, count);
    void* tex = texture_record(mat);
    if (shadowed) {
      if (g_table.shadowed) g_table.shadowed(g_table.ctx, count, tex);
    } else if (g_table.textured) {
      g_table.textured(g_table.ctx, count, tex, s.opacity, s.overlay);
    }
  } while (split);
}

// n is the draw's local [ebp-0x24]: strips skip the backface test and pass the
// previous polygon's normal on to the lighting.
void draw_polygon(Model& m, Polygon& p, uint32_t needs, bool receiver, int32_t projected, bool cache,
                  float n[3]) {
  DrawState& s = g_state;
  DrawBuffers& b = g_buf;
  Clip2D& d = clip2d();
  const int32_t count = static_cast<int32_t>(p.flags & 0xff);
  // FIX (bug): the original's copies are do-whiles (0 vertices would run 2^32
  // times) into 64-vertex buffers with no bound; shipped polygons stay inside.
  if (count == 0 || count > kPolyVerts) return;
  float(*cam)[3] = clip3d_buffers().cam;
  for (int32_t j = 0; j < count; ++j) copy3(cam[j], b.cam[p.vertex_indices[count - 1 - j]]);
  const bool strip = (p.flags >> 10) & 1;
  if (!strip) {
    const float dist = backface(n);
    if (!(-s.backface > dist)) {
      if (!(p.flags & 0x100) || !(dist > s.backface)) return;
      n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
    }
  }
  zvid::device_state().primitive = 6 - (strip ? 1 : 0);
  if (s.vertex_shading && field<uint32_t>(m, 0x18) && (p.flags & 0x200)) {
    for (int32_t j = 0; j < count; ++j) copy3(b.poly_normal[j], b.normals[p.normal_indices[count - 1 - j]]);
    b.poly_normals = 1;
  } else {
    b.poly_normals = 0;
  }
  Material& mat = *p.material;
  uint32_t per_vertex = 0, fogged = 0;
  if (needs & 1) {
    for (int32_t j = 0; j < count; ++j) {
      const float f = fog_calc().factor[p.vertex_indices[count - 1 - j]];
      d.work[j].light[2] = f;
      if (f > kVisible) fogged = 1;
    }
    if (fogged)
      for (int32_t j = 1; j < count; ++j)
        if (std::fabs(d.work[j].light[2] - d.work[0].light[2]) > kVisible) {
          per_vertex = 2;
          break;
        }
  }
  if (!(mat.flags & 1)) return draw_untextured(m, p, needs, fogged, per_vertex, cache, count, n);
  if (mat.flags & 4) material_cycle(mat);
  struct MaterialScope {
    platform::Render3D *renderer = nullptr;
    ~MaterialScope() {
      if (renderer)
        renderer->end_material_draw();
    }
  } material_scope;
  if (auto *renderer = zvid::render3d();
      renderer && renderer->wants_material_context()) {
    platform::MaterialDrawContext context;
    context.model = &m;
    context.material = &mat;
    context.texture_entry = mat.texture;
    context.polygon = static_cast<uint32_t>(&p - m.polygons);
    material_scope.renderer = renderer;
    renderer->begin_material_draw(context);
  }
  int32_t spot = projected && !texture_alpha(mat) ? 1 : 0;
  if (strip && spot) spot = g_hooks.projected_strip ? g_hooks.projected_strip(g_hooks.ctx, count) : 0;
  if (needs & 6) {
    if (field<uint8_t>(m, 8) & 0x20) per_vertex |= 4;
    uint32_t* c = cache && !(p.flags & 0x100) ? p.scratch : nullptr;
    if (shade(c, n, count, &fogged, &per_vertex, spot, needs)) fogged |= 2;
    per_vertex &= ~4u;
  } else if (fogged) {
    d.work[0].light[0] = 0;
    // One past the polygon, down to vertex 1: in the original slot 64 is the
    // shadow count at 0x7489a8, reset before every draw anyway.
    if (per_vertex)
      for (int32_t i = count; i >= 1; --i)
        if (i < kClipVerts) d.work[i].light[0] = 0;
  }
  for (int32_t j = 0; j < count; ++j) {
    d.tl[count - 1 - j].tu = p.uvs[2 * j], d.tl[count - 1 - j].tv = p.uvs[2 * j + 1];
    std::memcpy(d.work[count - 1 - j].color, p.colors + 3 * j, 12);
  }
  if (fogged) return draw_textured_lit(p, fogged, per_vertex, spot, receiver, strip, count, n);
  draw_textured(p, receiver, strip, count, n);
}

// --- Facades (model type 1). The billboard tops 0x51ea90.. belong to the
// maths module (zmth, not ported there yet); kept here with the draw. ---

// x87 "equal to zero" (fcomp 0.0, C3): NaN counts as zero.
bool zero(float x) { return !(x < 0.0f || x > 0.0f); }

void rotate_point(const float* v, const float* t, float* out) {
  const float x0 = v[0] * t[0], x1 = v[0] * t[1], x2 = v[0] * t[2];
  const float y0 = v[1] * t[3], y1 = v[1] * t[4], y2 = v[1] * t[5];
  const float z0 = v[2] * t[6], z1 = v[2] * t[7], z2 = v[2] * t[8];
  const float a0 = x0 + y0, a1 = x1 + y1, a2 = x2 + y2;
  out[0] = a0 + z0, out[1] = a1 + z1, out[2] = a2 + z2;
}

// Mech3.exe 1.2 @ 0x0053dcf0. ECX model, EDX out: the mean of every polygon's
// vertices (shared ones counted each time), morphed; polygons last to first.
void facade_centre(const Model& m, float out[3]) {
  out[0] = out[1] = out[2] = 0.0f;  // the zero vector at 0x63d870
  const int32_t polys = field<int32_t>(m, 0x10);
  if (polys == 0) return;
  int32_t total = 0;
  const bool morph = field<uint8_t>(m, 8) & 8;
  const float f = field<float>(m, 0x24);
  for (int32_t i = polys - 1; i >= 0; --i) {
    const Polygon& p = m.polygons[i];
    const int32_t n = static_cast<int32_t>(p.flags & 0xff);
    total += n;
    for (int32_t j = n - 1; j >= 0; --j) {
      const uint32_t at = p.vertex_indices[j];
      const float* v = m.vertices + 3 * at;
      out[0] = out[0] + v[0], out[1] = out[1] + v[1], out[2] = out[2] + v[2];
      if (morph && field<int32_t>(m, 0x1c) > static_cast<int32_t>(at)) {
        const float* d = m.morphs + 3 * at;
        const float dx = f * d[0], dy = d[1] * f, dz = d[2] * f;
        out[0] = out[0] + dx, out[1] = out[1] + dy, out[2] = out[2] + dz;
      }
    }
  }
  const float r = 1.0f / static_cast<float>(total);
  out[0] = r * out[0], out[1] = r * out[1], out[2] = r * out[2];
}

// r = l * view, inlined in the billboards as 0x51fb30 has it: the 3x3 and
// x, y of the translation in the usual order, but translation z summed as
// (l11*v8 + l10*v5) + (l9*v2 + v11). (zmth::load_model_view sums it in order.)
void times_view(const float* l, float* r) {
  const float* v = zmth::view().view.m;
  for (int row = 0; row < 3; ++row)
    for (int j = 0; j < 3; ++j) {
      const float a = l[3 * row] * v[j], b = l[3 * row + 1] * v[3 + j], c = l[3 * row + 2] * v[6 + j];
      const float s = a + b;
      r[3 * row + j] = s + c;
    }
  for (int j = 0; j < 2; ++j) {
    const float a = l[9] * v[j], b = l[10] * v[3 + j], c = l[11] * v[6 + j];
    const float s = a + b, t = s + c;
    r[9 + j] = t + v[9 + j];
  }
  const float a = l[11] * v[8], b = l[10] * v[5], c = l[9] * v[2];
  const float s = a + b, t = c + v[11];
  r[11] = s + t;
}

// Mech3.exe 1.2 @ 0x0051ea90 (stack: the camera's yaw, camera data +0x3c).
// Facade mode 0, turning about its y axis: the top becomes the node's world
// matrix with its yaw replaced by the camera's, times the view.
void billboard_y(float camera_yaw) {
  zmth::MatrixStack& st = zmth::matrix_stack();
  const zmth::Mat34 below = st.below();
  float yaw = 0.0f;
  if (!st.below_identity() && (!zero(below.m[6]) || !zero(below.m[8])))
    yaw = static_cast<float>(std::atan2(static_cast<double>(below.m[6]), static_cast<double>(below.m[8])));
  zmth::load_identity();
  zmth::rotate_y(camera_yaw - yaw);
  zmth::multiply(below, 1);  // top = below * Ry
  zmth::Mat34& top = st.matrix();
  top.m[9] = below.m[9], top.m[10] = below.m[10], top.m[11] = below.m[11];
  const zmth::Mat34 facing = top;
  times_view(facing.m, top.m);
  st.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x0051efb0. Facade mode 1, turning to face the camera: the
// model-view's rotation turned so its y axis points along the view direction
// to the model.
void billboard_sphere() {
  zmth::MatrixStack& st = zmth::matrix_stack();
  float* m = st.matrix().m;
  times_view(st.below().m, m);
  st.is_identity() = 0;
  for (int k : {1, 2, 4, 5, 7, 8}) m[k] = -m[k];
  float d[3] = {-m[9], m[10], m[11]};
  if (zero(normalize(d))) d[0] = 1.0f;
  float u[3] = {m[3], m[4], m[5]};
  if (zero(normalize(u))) u[0] = 1.0f;
  float q[4], r[9];
  zmth::quat_between(u, d, q);  // 0x00525a50
  zmth::quat_matrix(q, r);
  for (int row = 0; row < 3; ++row) {
    float* v = m + 3 * row;
    const float a = v[0], b = v[1], c = v[2];
    const float x0 = b * r[3], x1 = c * r[6];
    const float y0 = b * r[4], y1 = c * r[7];
    const float z0 = b * r[5], z1 = c * r[8];
    const float sx = x0 + x1, sy = y0 + y1, sz = z0 + z1;
    const float ax = a * r[0], ay = a * r[1], az = a * r[2];
    v[0] = sx + ax;
    v[1] = -(sy + ay);
    v[2] = -(sz + az);
  }
}

// 0x53f5fa..0x53f846: the facade's top. Model flag 0x40 turns it about its
// polygons' centre (shifting the world matrix's translation around the turn).
// c is the draw's [ebp-0x24], the slot the polygon normal uses later.
void facade_top(const Model& m, float c[3]) {
  zmth::MatrixStack& st = zmth::matrix_stack();
  const bool about_centre = field<uint8_t>(m, 8) & 0x40;
  float v[3];
  if (about_centre) {
    facade_centre(m, c);
    float* w = st.below().m;
    rotate_point(c, w, v);
    w[9] = w[9] + v[0], w[10] = w[10] + v[1], w[11] = w[11] + v[2];
  }
  switch (field<int32_t>(m, 4)) {
    case 0: billboard_y(g_state.camera_data ? g_state.camera_data[0x3c / 4] : 0.0f); break;
    case 1: billboard_sphere(); break;
    // Modes 2 (0x51f390, about x) and 3 (0x51f750, about z) are not ported: no
    // shipped world uses them. Any other mode leaves the pushed slot
    // uninitialised in the original. Both draw as a default model here.
    default: zmth::load_model_view(); break;
  }
  if (about_centre) {
    float* w = st.below().m;
    rotate_point(c, w, v);
    w[9] = w[9] - v[0], w[10] = w[10] - v[1], w[11] = w[11] - v[2];
    float* t = st.matrix().m;
    rotate_point(c, t, v);
    t[9] = t[9] - v[0], t[10] = t[10] - v[1], t[11] = t[11] - v[2];
  }
}

void draw_points(Model& m) {
  DrawState& s = g_state;
  const uint32_t n = field<uint32_t>(m, 0x14);
  zmth::load_model_view();
  if (morphing(m)) {
    morph_vertices(m);
  } else if (n != 0) {  // FIX (bug): a do-while in the original (0 runs 2^32 times)
    transform_vertices(m, n);
  }
  // The colour of polygon 0's material, read even without polygons. FIX (bug):
  // no polygons draws nothing here (the original reads through a null pointer).
  if (!m.polygons || !m.polygons[0].material) return;
  const uint32_t color = m.polygons[0].material->rgb;
  for (uint32_t i = 0; i < n; ++i) {
    const zmth::Vec3& v = reinterpret_cast<const zmth::Vec3*>(g_buf.cam)[i];
    if (!(v.z > s.clip.near_z)) continue;
    zmth::Vec3 sc;
    zmth::project_d3d(&v, &sc, 1);  // 0x53f47f's software 0x5234f0 is dead: 0x53f2e0 needs a device
    if (below(sc.x, s.point_rect[0]) || below(sc.y, s.point_rect[1])) continue;
    if (sc.x > s.point_rect[2] || sc.y > s.point_rect[3]) continue;
    const float screen_pt[3] = {sc.x, sc.y, sc.z};
    if (g_table.point) g_table.point(g_table.ctx, screen_pt, color, 0xff);
  }
}

}  // namespace

DrawBuffers& draw_buffers() { return g_buf; }
DrawState& draw_state() { return g_state; }

// Mech3.exe 1.2 @ 0x0053cef0, 0x0053cf00, 0x00526df0, 0x00526a00
void set_bfe_tolerance(float v) { g_state.backface = v; }
float bfe_tolerance() { return g_state.backface; }
int set_inverse_z_tolerance(float v) { return g_state.priority_scale = v, 0; }
int set_vertex_shading(int on) { return g_state.vertex_shading = on, 0; }
DrawTable& draw_table() { return g_table; }
DrawHooks& draw_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x004020d0. Returns the length; leaves a zero vector alone.
float normalize(float v[3]) {
  const float xx = v[0] * v[0], yy = v[1] * v[1], zz = v[2] * v[2];
  const float s = xx + zz;
  const float len = std::sqrt(s + yy);
  if (len != 0.0f) {
    const float r = 1.0f / len;
    v[0] = r * v[0], v[1] = r * v[1], v[2] = r * v[2];
  }
  return len;
}

// Mech3.exe 1.2 @ 0x005376c0 (zModel\gmod_matl.c). Once a frame: show texture
// int(index) % count, then advance by speed * dt; a non-looping cycle stops on
// its last texture.
void material_cycle(Material& mat) {
  Cycle* c = mat.cycle;
  if (!c) {
    zerr::report(0x200, "C:\\zipper\\GameZMech3\\zModel\\gmod_matl.c", 0x6f6,
                 (mat.flags & 4) ? "Material Cycle Pointer is NULL: flag is (TRUE)"
                                 : "Material Cycle Pointer is NULL: flag is (FALSE)");
    return;
  }
  const uint32_t frame = zcls::frames().frame;
  if (c->frame == frame) return;
  const int32_t at = crt::msvc_ftol(c->index);
  mat.texture = c->textures[at % static_cast<int32_t>(c->texture_count)];
  const float step = c->speed * ztimer::state().dt;
  c->index = step + c->index;
  if (c->looping == 0 && !(static_cast<float>(static_cast<int32_t>(c->texture_count)) > c->index))
    c->index = static_cast<float>(static_cast<int32_t>(c->texture_count) - 1);
  if (below(c->index, 0.0f)) {
    // Adds whole multiples of the count only for |speed| >= 1 (a quirk).
    const int32_t back = crt::msvc_ftol(std::fabs(c->speed)) * static_cast<int32_t>(c->texture_count);
    c->index = static_cast<float>(back) + c->index;
  }
  c->frame = frame;
}

// Mech3.exe 1.2 @ 0x00535d20. 0 when every vertex is outside one side the
// rect's mask selects (+4 left, +8 top, +0x10 right, +0x14 bottom).
int32_t screen_reject(const ClipPlanes& rect, int32_t count) {
  float r[6];
  std::memcpy(r, &rect.side_04, sizeof r);
  const uint32_t mask = rect.mask;
  const TLVertex* tl = clip2d().tl;
  if (mask == 0) return 1;
  auto all_out = [&](auto inside) {
    for (int32_t i = 0; i < count; ++i)
      if (inside(tl[i])) return false;
    return true;
  };
  if ((mask & 1) && all_out([&](const TLVertex& v) { return !below(v.sx, r[0]); })) return 0;
  if ((mask & 2) && all_out([&](const TLVertex& v) { return below(v.sx, r[3]); })) return 0;
  if ((mask & 4) && all_out([&](const TLVertex& v) { return !below(v.sy, r[1]); })) return 0;
  if ((mask & 8) && all_out([&](const TLVertex& v) { return below(v.sy, r[4]); })) return 0;
  return 1;
}


// Mech3.exe 1.2 @ 0x0053f2e0. ECX = the node, EDX = the plane mask the cull
// left (0x10 near, 0x20 far, 0xf sides). Always returns 0.
int32_t draw_model(zcls::Node* node, uint32_t mask) {
  DrawState& s = g_state;
  Model& m = *node->model;
  zmth::Mat34 local;  // [ebp-0xd0]
  zmth::push(&local);
  struct MatrixScope {
    ~MatrixScope() { zmth::pop(); }
  } matrix_scope;
  fog_calc().verts = g_buf.cam;
  Lighting& l = lighting();  // the lighting reads the same buffers (0x7476a4, 0x7481a8, 0x747ca8)
  l.cam = clip3d_buffers().cam;
  l.rec = reinterpret_cast<float(*)[8]>(clip2d().work);
  l.normals = g_buf.poly_normal;
  const int32_t type = field<int32_t>(m, 0);
  if (type == 2) {
    if (auto* renderer = zvid::render3d(); renderer &&
        renderer->geometry_mode() != cc::geometry::Mode::legacy) {
      renderer->note_cpu_model_geometry();
      if (renderer->geometry_mode() == cc::geometry::Mode::gpu)
        throw std::runtime_error("Strict GPU geometry: point model is not implemented");
    }
    draw_points(m);
    s.default_model = 0;
    return 0;
  }
  float n[3] = {};  // [ebp-0x24]: the polygon normal, a facade's centre before
  if (type == 1) {
    facade_top(m, n);
  } else {
    zmth::load_model_view();
  }
  s.default_model = type == 0 ? 1 : 0;
  // The owned geometry handoff precedes every rendering-related vertex loop.
  // This initial family has no model lights, morphs or material cycles.
  if (submit_geometry(*node)) {
    s.clip.mask = mask;
    zvid::device_state().primitive = 6;
    return 0;
  }
  uint32_t needs = 0;  // uninitialised stack in the original when there are no polygons
  if (field<int32_t>(m, 0x10) > 0) {
    needs = model_needs(field<uint32_t>(m, 8), reinterpret_cast<const float*>(m.header + 0x48), field<float>(m, 0x54));
    if (morphing(m)) {
      morph_vertices(m);
    } else if (field<uint32_t>(m, 0x14) != 0) {
      transform_vertices(m, field<uint32_t>(m, 0x14));
    }
    if ((needs & 1) && !fog_vertices(field<int32_t>(m, 0x14))) needs &= ~1u;
    if (s.vertex_shading && field<uint32_t>(m, 0x18)) {
      const uint32_t k = field<uint32_t>(m, 0x18);
      zmth::rotate_points_to(reinterpret_cast<const zmth::Vec3*>(m.normals),
                             reinterpret_cast<zmth::Vec3*>(g_buf.normals), k);
      for (int32_t i = 0; i < static_cast<int32_t>(k); ++i) normalize(g_buf.normals[i]);
    }
  }
  animate_lights(m);
  s.clip.mask = mask;
  const bool receiver = s.shadows && (field<uint8_t>(m, 8) & 1) && !below(s.opacity, 1.0f);
  int32_t projected = 0;
  if (node_field<uint32_t>(*node, 0xbc) <= 1 && !below(s.opacity, 1.0f) && (field<uint8_t>(m, 8) & 1)) {
    const LightList& l = light_list();  // an enabled light with flags 0x10 and 0x40 (0x53fb41)
    for (int32_t i = 0; i < l.count && !projected; ++i) {
      const uint32_t f = l.list[i].data->get<uint32_t>(0xb0);
      if (l.list[i].reach && (f & 0x10) && (f & 0x40)) projected = 1;
    }
  }
  bool cache = false;
  if (s.static_lights && type == 0 && field<int32_t>(m, 0xc) < 2 && (needs & 2)) {
    if (node_field<uint32_t>(*node, 0xc0) >= zcls::frames().update_frame) {
      cache = true;
    } else if (node_field<uint32_t>(*node, 0xc4) <= zcls::frames().frame) {
      // 0x00544ed0 then 0x004ed980: the bake into each polygon's +0x18, the
      // node's cache stamped. Without it the cache is read unfilled: black.
      if (g_hooks.refresh_static) {
        g_hooks.refresh_static(g_hooks.ctx, &m, node);
      } else {
        bake_static_light(m, g_buf.cam, g_buf.normals);
        node->put<uint32_t>(0xc0, zcls::frames().update_frame);
      }
      cache = true;
    }
  }
  const int32_t polys = field<int32_t>(m, 0x10);
  struct ModelScope {
    platform::Render3D* renderer = nullptr;
    ~ModelScope() { if (renderer) renderer->end_model_draw(); }
  } scope;
  if (auto* renderer = zvid::render3d(); renderer && renderer->wants_model_context()) {
    const auto& projection = zmth::projection();
    auto& stack = zmth::matrix_stack();
    if (stack.top > 0) {
      platform::ModelDrawContext context;
      context.node = node;
      context.model_to_world = stack.below().m;
      context.model_to_camera = stack.matrix().m;
      context.near_z = projection.near_z, context.far_z = projection.far_z;
      context.clip_near = s.clip.near_z;
      context.fx_near = projection.fx_near, context.fy_near = projection.fy_near;
      context.cx = projection.cx, context.cy = projection.cy;
      context.opacity = s.opacity;
      context.ordinary = type == 0;
      context.morphing = morphing(m);
      context.overlay = s.overlay != 0;
      scope.renderer = renderer;
      renderer->begin_model_draw(context);
    }
  }
  for (int32_t i = 0; i < polys; ++i) draw_polygon(m, m.polygons[i], needs, receiver, projected, cache, n);
  zvid::device_state().primitive = 6;
  return 0;
}

}  // namespace zmod
