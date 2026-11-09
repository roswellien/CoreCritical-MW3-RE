#include "gamez/zvideo/zvid_ddd3d.h"
#include "frontend/core_critical/renderer/native_texture_identity.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "crt/msvc_math.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zmodel/gmod_clip2d.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zvideo/zvid_dd.h"
#include "gamez/zvideo/zvid_format.h"

// The original rounds every step to float (Direct3D runs the FPU at 24 bits);
// no fused multiply-adds, so the steps stay the same.
#pragma STDC FP_CONTRACT OFF

namespace zvid {

static_assert(sizeof(WorkRecord) == sizeof(zmod::VertexWork), "the work record is 0x20 bytes");
static_assert(sizeof(zmod::TLVertex) == sizeof(TLVertex), "both are D3DTLVERTEX");

namespace {
TransparentQueue g_queue;
OverlayList g_overlay;
bool g_transparent_sky[TransparentQueue::kMaxPolys];
bool g_overlay_sky[OverlayList::kMax];
platform::Render3D *g_render = nullptr;
// Host-only admission sidecars preserve original queue records and draw order.
std::shared_ptr<const platform::DeferredMaterialBinding>
    g_transparent_materials[TransparentQueue::kMaxPolys];
std::shared_ptr<const platform::DeferredMaterialBinding>
    g_overlay_materials[OverlayList::kMax];
struct DeferredMaterialScope {
  platform::Render3D *renderer;
  DeferredMaterialScope(
      std::shared_ptr<const platform::DeferredMaterialBinding> binding)
      : renderer(g_render) {
    if (renderer)
      renderer->begin_deferred_material_draw(std::move(binding));
  }
  ~DeferredMaterialScope() {
    if (renderer)
      renderer->end_deferred_material_draw();
  }
};

TLVertex g_flat[64];
DeviceState g_state;
platform::DrawState g_draw;
struct NativeSkyScope {
  explicit NativeSkyScope(bool enabled) : previous(set_native_sky(enabled)) {}
  ~NativeSkyScope() { set_native_sky(previous); }
  bool previous;
};
DrawBuffers g_buffers{reinterpret_cast<TLVertex*>(zmod::clip2d().tl),
                      reinterpret_cast<const WorkRecord*>(zmod::clip2d().work), &zmod::clip2d().shadow};
// 0x008008e0: up to 64 vertices of 6 + 2 * 8 dwords (the shadowed draws' FVF).
constexpr int kMultiStride = 6 + 2 * (1 + zmod::kShadowSets);
uint32_t g_multi[64 * kMultiStride];
int32_t g_src_blend = 5, g_dest_blend = 6;  // SRCBLEND / DESTBLEND as 0x0057a900 leaves them
PointLight g_light;
ChannelMasks g_masks;
Display g_display;
D3DTexture* g_default = nullptr;
TintQuad g_tint;

constexpr const char* kFile = "C:\\zipper\\GameZMech3\\zVideo\\zvid_ddd3d.c";
constexpr float kInv255 = 0.003921569f;  // 0x005ceb78

// The sort key: the first vertex's sz bits as a signed int (negative floats
// order backwards among themselves; sz is never negative in play).
int32_t key(int32_t index) {
  int32_t k;
  std::memcpy(&k, &g_queue.polys[index].verts[0].sz, 4);
  return k;
}

// FIX (A): IDirect3DDevice3::SetRenderState (vt+0x58), kept as the state it sets.
void set_render_state(int32_t state, int32_t value) {
  platform::DrawState& d = g_draw;
  switch (state) {
    case 8: d.wireframe = value == 2; break;      // FILLMODE: 2 wireframe, 3 solid
    case 9: d.flat = value == 1; break;           // SHADEMODE: 1 flat, 2 Gouraud
    case 14: d.depth_write = value != 0; break;   // ZWRITEENABLE
    case 19:                                      // SRCBLEND: 5 SRCALPHA, 1 ZERO
    case 20:                                      // DESTBLEND: 6 INVSRCALPHA, 3 SRCCOLOR
      (state == 19 ? g_src_blend : g_dest_blend) = value;
      d.multiply = g_src_blend == 1 && g_dest_blend == 3;
      break;
    case 23: d.depth_always = value == 8; break;  // ZFUNC: 8 ALWAYS, 7 GREATEREQUAL
    case 27: d.blend = value != 0; break;         // ALPHABLENDENABLE
    case 28: d.fog = value != 0; break;           // FOGENABLE
    case 34:                                      // FOGCOLOR
      if (g_render) g_render->set_fog_color(static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8),
                                            static_cast<uint8_t>(value));
      break;
    default: break;                               // DITHERENABLE (26): not reproduced
  }
}

// FIX (A): SetTextureStageState (vt+0xa0), stage 0.
void set_texture_stage_state(int32_t type, int32_t value) {
  platform::DrawState& d = g_draw;
  switch (type) {
    case 1: d.color_select = value == 2; break;    // COLOROP: 2 SELECTARG1, 4 MODULATE
    case 4: d.alpha_modulate = value == 4; break;  // ALPHAOP: 4 MODULATE, 2 SELECTARG1
    case 11: d.texcoord = static_cast<uint8_t>(value); break;  // TEXCOORDINDEX
    case 13: d.address_u = static_cast<uint8_t>(value); break;
    case 14: d.address_v = static_cast<uint8_t>(value); break;
    default: break;  // MAGFILTER / MINFILTER (16 / 17): LINEAR, as 0x0057a900 sets them
  }
}

// FIX (A): SetTexture(0, ...) (vt+0x98).
void set_texture(const D3DTexture* t) { g_draw.texture = t ? t->handle : -1; }

// FIX (A): DrawPrimitive (vt+0x70), FVF 0x1c4, flags 0x18. Render3D::draw has
// no HRESULT, so the original's failure reports (0x00580550) go.
void draw_primitive(int32_t primitive, const TLVertex* verts, int32_t count) {
  if (g_render) g_render->draw(static_cast<platform::Primitive>(primitive), verts, count, g_draw);
}

// The textured draws' cached stage-0 states, then the texture.
void set_address(const D3DTexture* t) {
  DeviceState& s = g_state;
  if (s.address_u != t->address_u) set_texture_stage_state(13, t->address_u), s.address_u = t->address_u;
  if (s.address_v != t->address_v) set_texture_stage_state(14, t->address_v), s.address_v = t->address_v;
}

void set_shade(int32_t mode) {
  if (g_state.shade_mode != mode) set_render_state(9, mode), g_state.shade_mode = mode;
}

int32_t ftol(float x) { return crt::msvc_ftol(x); }

// `x - -0.5` then _ftol: rounds the 0..255 colours.
uint32_t round_byte(float x) { return static_cast<uint32_t>(ftol(x - -0.5f)); }

// x - -1.5 * 2^52 stored as a double, its low dword taken: x rounded to the
// nearest integer on a 53- or 64-bit FPU (zfix fpu53, as in play), but 0 for
// every colour at 24 bits (render-drawtable.md, the point-light quirk).
int32_t magic_round(float x) {
  if (zfix::options().fpu53) return static_cast<int32_t>(std::nearbyint(double{x}));
  const float t = x - -6755399441055744.0f;
  const double d = t;
  uint64_t bits;
  std::memcpy(&bits, &d, 8);
  return static_cast<int32_t>(static_cast<uint32_t>(bits));
}

int32_t clamp_byte(int32_t v) { return v > 0xff ? 0xff : v < 0 ? 0 : v; }

uint32_t rgb_bits(uint32_t r, uint32_t g, uint32_t b) { return ((r << 8 | g) << 8) | b; }

// A display-packed colour to ARGB, the way 0x0057bf80 unpacks it.
uint32_t unpack(uint32_t c, int32_t alpha) {
  const ChannelMasks& m = g_masks;
  const PixelFormat& f = pixel_format();
  uint32_t v = ((c & m.r) >> (f.r_shift & 31)) | static_cast<uint32_t>(alpha) << 8;
  v = v << 8 | (c & m.g) >> (f.g_shift & 31);
  return v << 8 | (c & m.b) << (f.b_shift_right & 31);
}

uint32_t opacity_alpha(float opacity) {
  return !(opacity >= 1.0f) ? static_cast<uint32_t>(ftol(opacity * 255.0f)) << 24 : 0xff000000u;
}

// Specular alpha = fog (255 none), rhw = sz: the first loop of every draw.
void fog_and_rhw(bool fog, int32_t count) {
  TLVertex* tl = g_buffers.tl;
  const WorkRecord* w = g_buffers.work;
  for (int32_t i = count - 1; i >= 0; --i) {
    tl[i].specular = fog ? static_cast<uint32_t>(ftol(255.5f - w[i].fog * 255.0f)) << 24 : 0xff000000u;
    tl[i].rhw = tl[i].sz;
  }
}

// The textured lit colour of one vertex (0x0057d160 and its kin): the colour
// times `dark`, plus the coloured point light (flag 4) rounded the magic way.
uint32_t lit_rgb(const WorkRecord& w, float dark, uint32_t flags) {
  const float* c = w.color;
  if ((flags & 4) && w.point > kInv255) {
    const PointLight& l = g_light;
    const float s = w.point * kInv255;
    const int32_t r = clamp_byte(magic_round((s * l.color[0] + dark) * c[0] - -0.5f));
    const int32_t g = clamp_byte(magic_round((s * l.color[1] + dark) * c[1] - -0.5f));
    const int32_t b = clamp_byte(magic_round((s * l.color[2] + dark) * c[2] - -0.5f));
    return rgb_bits(r, g, b);
  }
  return rgb_bits(round_byte(dark * c[0]), round_byte(c[1] * dark), round_byte(c[2] * dark));
}

// The projected-light draws' colour: vertex colour times its light, or the
// colour alone without one.
uint32_t light_rgb(const WorkRecord& w, const float* light) {
  const float* c = w.color;
  if (!light) return rgb_bits(round_byte(c[0]), round_byte(c[1]), round_byte(c[2]));
  int32_t v[3];
  for (int k = 0; k < 3; ++k) v[k] = clamp_byte(magic_round(c[k] * light[k] - -0.5f));
  return rgb_bits(v[0], v[1], v[2]);
}

uint32_t fog_alpha(uint32_t flags, const WorkRecord& w) {
  return (flags & 1) ? static_cast<uint32_t>(ftol(255.5f - w.fog * 255.0f)) << 24 : 0xff000000u;
}

// Vertex i of the shadowed draws' buffer: position and rhw = sz, colours,
// the texture's UVs and one pair per shadow set.
void multi_vertex(int32_t i, uint32_t diffuse, uint32_t specular) {
  const TLVertex& t = g_buffers.tl[i];
  const zmod::ShadowSets& s = *g_buffers.shadow;
  uint32_t* v = g_multi + (8 + 2 * s.count) * i;
  std::memcpy(v, &t.sx, 12);
  std::memcpy(v + 3, &t.sz, 4);
  v[4] = diffuse, v[5] = specular;
  std::memcpy(v + 6, &t.tu, 8);
  for (int32_t k = 0; k < s.count; ++k) std::memcpy(v + 8 + 2 * k, &s.uv[k][i], 8);
}

// The shadowed draws' device calls: the polygon with its texture, then each
// shadow's texture multiplied over it (SRCBLEND ZERO, DESTBLEND SRCCOLOR,
// colour = texture alone, clamped, fog off), then the defaults back and
// FOGENABLE on. ALPHABLENDENABLE goes around the state cache.
void shadow_passes(int32_t count, const D3DTexture* texture) {
  DeviceState& st = g_state;
  const zmod::ShadowSets& s = *g_buffers.shadow;
  const int32_t sets = s.count + 1;
  set_shade(2);
  set_texture(texture);
  // FIX (bug): a null texture would be read through here; it is skipped.
  if (texture) set_address(texture);
  if (st.alpha_op != 2) st.alpha_op = 2, set_texture_stage_state(4, 2);
  set_texture_stage_state(16, 2);  // MAGFILTER LINEAR
  set_texture_stage_state(17, 2);  // MINFILTER LINEAR
  auto draw = [&] {
    if (g_render) g_render->draw_multi(static_cast<platform::Primitive>(st.primitive), g_multi, sets, count, g_draw);
  };
  draw();
  set_render_state(19, 1);
  set_render_state(20, 3);
  set_render_state(27, 1);
  if (st.address_u != 3) set_texture_stage_state(13, 3), st.address_u = 3;
  if (st.address_v != 3) set_texture_stage_state(14, 3), st.address_v = 3;
  set_texture_stage_state(1, 2);  // COLOROP SELECTARG1
  set_render_state(28, 0);
  for (int32_t k = 1; k <= s.count; ++k) {
    set_texture(static_cast<const D3DTexture*>(s.texture[k - 1]));
    set_texture_stage_state(11, k);
    draw();
  }
  set_render_state(19, 5);
  set_render_state(20, 6);
  set_render_state(27, 0);
  set_texture_stage_state(11, 0);
  set_texture_stage_state(1, 4);   // COLOROP MODULATE
  set_render_state(28, 1);         // FOGENABLE: the only place the game turns fog on
}

// A slot in the overlay list, or the report (each site has its own line;
// the value printed is the list's count, not count + 1).
OverlayPoly* overlay_slot(int32_t type, int32_t count, const D3DTexture* texture, int line,
                          const char* need = "need") {
  // A replacement is already queued by the backend. Deferred source polygons
  // must not escape that model scope and draw again after end_model_draw().
  if (g_render && g_render->replaces_model_geometry()) return nullptr;
  OverlayList& o = g_overlay;
  if (o.count >= OverlayList::kMax) {
    char msg[64];
    std::snprintf(msg, sizeof msg, "Not enough ZVID_MAX_OVERWRITE_POLYS: %s %d", need, static_cast<int>(o.count));
    zerr::report(0x400, kFile, line, msg);
    return nullptr;
  }
  g_overlay_sky[o.count] = g_draw.native_sky;
  g_overlay_materials[o.count] =
      g_render && texture
          ? g_render->capture_deferred_material_binding(texture->handle)
          : nullptr;
  OverlayPoly& p = o.polys[o.count++];
  p.type = type;
  p.count = count;
  p.texture = texture;
  return &p;
}

// The textured draws' tail: queue, overlay or draw. Lines: the overlay
// reports for a transparent and an opaque polygon.
void textured_tail(int32_t count, const D3DTexture* texture, float opacity, int32_t overlay, int32_t type,
                   int line_transparent, int line_opaque, bool texture_first) {
  TLVertex* tl = g_buffers.tl;
  DeviceState& s = g_state;
  if (texture->type == 4 || !(opacity >= 1.0f)) {
    TLVertex* dst = nullptr;
    if (overlay == 0) {
      dst = queue_transparent(s.primitive, count, texture);
    } else if (OverlayPoly* p = overlay_slot(0, count, texture, line_transparent)) {
      dst = p->verts;
    }
    if (dst) std::memcpy(dst, tl, sizeof(TLVertex) * count);
    return;
  }
  if (overlay != 0) {
    if (OverlayPoly* p = overlay_slot(type, count, texture, line_opaque)) std::memcpy(p->verts, tl, sizeof(TLVertex) * count);
    return;
  }
  set_shade(2);
  if (texture_first) set_texture(texture);
  if (s.alpha_op != 2) s.alpha_op = 2, set_texture_stage_state(4, 2);  // SELECTARG1
  if (!texture_first) set_texture(texture);
  set_address(texture);
  draw_primitive(s.primitive, tl, count);
}

// Bit replication of an n-bit channel to 8 bits.
uint8_t expand(uint32_t p, uint32_t mask) {
  if (mask == 0) return 0;
  int shift = 0, bits = 0;
  while (!(mask >> shift & 1)) ++shift;
  while (mask >> (shift + bits) & 1) ++bits;
  const uint32_t v = (p & mask) >> shift;
  uint32_t r = 0;
  int filled = 0;
  while (filled < 8) r = r << bits | v, filled += bits;
  return static_cast<uint8_t>(r >> (filled - 8));
}

bool is_pow2(int32_t n) {  // 0x0057e9e0: starts at 2, so 1 is not a power of two
  int32_t p = 1;
  do p *= 2;
  while (p < n);
  return p - n == 0;
}
}  // namespace

TransparentQueue& transparent_queue() { return g_queue; }
OverlayList& overlay_list() { return g_overlay; }
TintQuad& tint_quad() { return g_tint; }

// Mech3.exe 1.2 @ 0x0057bb70
void set_fog(int32_t on) {
  DeviceState& st = g_state;
  if (st.fog != on) set_render_state(28, on), st.fog = on;
  if (st.fog_mode != 3) st.fog_mode = 3;  // SetLightState(FOGMODE, LINEAR)
}

// Mech3.exe 1.2 @ 0x00579d66..0x00579d84, in the screen mode's setup
// (0x00579c60): no quad pending, its vertices unfogged (specular alpha 255).
void reset_tint_quad() {
  g_tint.pending = 0;
  for (TLVertex& v : g_tint.verts) v.specular = 0xff000000u;
}

// Mech3.exe 1.2 @ 0x0057dd40: the quad's depth and 1/w (0.99 from the
// device's setup, 0x0057b05f).
void set_tint_depth(float z) {
  for (TLVertex& v : g_tint.verts) v.sz = z, v.rhw = z;
}
TLVertex* flat_buffer() { return g_flat; }
DeviceState& device_state() { return g_state; }
void set_render3d(platform::Render3D* render) { g_render = render; }
platform::Render3D* render3d() { return g_render; }

bool set_native_sky(bool enabled) noexcept {
  const bool previous = g_draw.native_sky;
  g_draw.native_sky = enabled;
  return previous;
}
platform::DrawState& draw_state() { return g_draw; }
DrawBuffers& draw_buffers() { return g_buffers; }
PointLight& point_light() { return g_light; }
ChannelMasks& channel_masks() { return g_masks; }
Display& display() { return g_display; }

// Mech3.exe 1.2 @ 0x00579240
int set_dither(int on) { return g_display.dither = on, 0; }
D3DTexture*& default_texture() { return g_default; }

// Inlined at each transparent draw (lines 0x591 and 0x599 of zvid_ddd3d.c).
TLVertex* queue_transparent(int32_t primitive, int32_t count, const D3DTexture* texture) {
  if (g_render && g_render->replaces_model_geometry()) return nullptr;
  TransparentQueue& q = g_queue;
  char msg[64];
  if (q.count >= TransparentQueue::kMaxPolys) {
    std::snprintf(msg, sizeof msg, "Not enough MAX_TRANSPARENT_POLYS: need %d", static_cast<int>(q.count + 1));
    zerr::report(0x400, kFile, 0x591, msg);
    return nullptr;
  }
  const uint32_t need = q.vertex_count + static_cast<uint32_t>(count);
  if (need > TransparentQueue::kMaxVerts) {
    std::snprintf(msg, sizeof msg, "Not enough MAX_TRANSPARENT_VERTICES: need %d", static_cast<int>(need));
    zerr::report(0x400, kFile, 0x599, msg);
    return nullptr;
  }
  g_transparent_sky[q.count] = g_draw.native_sky;
  g_transparent_materials[q.count] =
      g_render && texture
          ? g_render->capture_deferred_material_binding(texture->handle)
          : nullptr;
  TransparentPoly& p = q.polys[q.count++];
  p.primitive = primitive;
  p.count = count;
  p.texture = texture;
  p.verts = &q.verts[q.vertex_count];
  q.vertex_count = need;
  return p.verts;
}

// Mech3.exe 1.2 @ 0x0057bf80. Untextured, unlit: the material's packed colour
// on every vertex, flat. Opaque polygons are built in their own buffer.
// FIX (A): the device calls go to Render3D.
void draw_flat(uint32_t color, int32_t alpha, int32_t count, int32_t, int32_t overlay) {
  const uint32_t c = unpack(color, alpha);
  const TLVertex* tl = g_buffers.tl;
  TLVertex* dst = g_flat;
  if (alpha < 0xff) {
    if (overlay == 0) {
      dst = queue_transparent(g_state.primitive, count, nullptr);
    } else {
      OverlayPoly* p = overlay_slot(0, count, nullptr, 0x5f9);
      dst = p ? p->verts : nullptr;
    }
    if (!dst) return;
  }
  for (int32_t i = count - 1; i >= 0; --i) {  // tu, tv are left as they were
    dst[i].sx = tl[i].sx;
    dst[i].sy = tl[i].sy;
    dst[i].sz = tl[i].sz;
    dst[i].rhw = tl[i].sz;
    dst[i].diffuse = c;
    dst[i].specular = 0xff000000u;
  }
  if (alpha < 0xff) return;
  if (overlay == 0) {
    set_texture(nullptr);
    set_shade(1);
    draw_primitive(g_state.primitive, g_flat, count);
  } else if (OverlayPoly* p = overlay_slot(1, count, nullptr, 0x5d0, "needs")) {
    std::memcpy(p->verts, g_flat, sizeof(TLVertex) * count);
  }
}

// Mech3.exe 1.2 @ 0x0057dba0 ([0x0072f3e0]): one point, screen x, y and z
// (rhw = z), flat and untextured; under full alpha it waits in the
// transparent queue (D3DPT_POINTLIST, 1).
void draw_point(const float screen[3], uint32_t color, int32_t alpha) {
  TLVertex v{};
  v.sx = screen[0], v.sy = screen[1], v.sz = screen[2], v.rhw = screen[2];
  v.specular = 0xff000000u;
  v.diffuse = unpack(color, alpha);
  if (alpha < 0xff) {
    if (TLVertex* dst = queue_transparent(1, 1, nullptr)) *dst = v;
    return;
  }
  set_texture(nullptr);
  set_shade(1);
  draw_primitive(1, &v, 1);
}

// Mech3.exe 1.2 @ 0x0057c700. Untextured, lit or fogged: the material rgb
// times the FIRST vertex's darkness for every vertex, plus the point light
// per vertex (flag 4); fog (flag 1) in the specular alpha. A polygon with
// alpha below 255 is coloured and then dropped: nothing queues it.
// FIX (A): the device calls go to Render3D.
void draw_lit(uint32_t, const float rgb[3], uint32_t flags, int32_t alpha, int32_t count, int32_t,
              int32_t overlay) {
  TLVertex* tl = g_buffers.tl;
  const WorkRecord* w = g_buffers.work;
  const float dark = 1.0f - w[0].darkness;
  const uint32_t a = alpha < 0xff ? static_cast<uint32_t>(alpha) << 24 : 0xff000000u;
  fog_and_rhw(flags & 1, count);
  if (flags & 4) {
    const PointLight& l = g_light;
    for (int32_t i = count - 1; i >= 0; --i) {
      uint32_t v;
      if (w[i].point > kInv255) {
        const float p = w[i].point;
        float c[3] = {l.color[0] * p + dark * rgb[0], l.color[1] * p + rgb[1] * dark,
                      l.color[2] * p + rgb[2] * dark};
        float r = c[0];
        // Scaled down by the light's brightest channel only, truncated, not rounded.
        if (c[l.brightest] > 255.0f) {
          const float s = 255.0f / c[l.brightest];
          r = r * s;
          c[1] = s * c[1];
          c[2] = s * c[2];
        }
        v = rgb_bits(static_cast<uint32_t>(ftol(r)), static_cast<uint32_t>(ftol(c[1])),
                     static_cast<uint32_t>(ftol(c[2])));
      } else {
        v = rgb_bits(round_byte(dark * rgb[0]), round_byte(rgb[1] * dark), round_byte(rgb[2] * dark));
      }
      tl[i].diffuse = v | a;
    }
  } else {
    const uint32_t v = rgb_bits(round_byte(dark * rgb[0]), round_byte(rgb[1] * dark), round_byte(rgb[2] * dark)) | a;
    for (int32_t i = count - 1; i >= 0; --i) tl[i].diffuse = v;
  }
  if (alpha < 0xff) return;
  if (overlay != 0) {
    if (OverlayPoly* p = overlay_slot(3, count, nullptr, 0x760)) std::memcpy(p->verts, tl, sizeof(TLVertex) * count);
    return;
  }
  set_texture(nullptr);
  set_shade(2);
  draw_primitive(g_state.primitive, tl, count);
}

// Mech3.exe 1.2 @ 0x0057ce20. Textured, unlit: the vertex colours, rounded,
// with the node opacity as alpha. Transparent when the texture is type 4 or
// the opacity is below 1. FIX (A): the device calls go to Render3D.
void draw_textured(int32_t count, const D3DTexture* texture, float opacity, int32_t overlay) {
  TLVertex* tl = g_buffers.tl;
  const WorkRecord* w = g_buffers.work;
  const uint32_t a = opacity_alpha(opacity);
  for (int32_t i = count - 1; i >= 0; --i) {
    tl[i].specular = 0xff000000u;
    tl[i].diffuse = rgb_bits(round_byte(w[i].color[0]), round_byte(w[i].color[1]), round_byte(w[i].color[2])) | a;
    tl[i].rhw = tl[i].sz;
  }
  textured_tail(count, texture, opacity, overlay, 4, 0x88c, 0x856, false);
}

// Mech3.exe 1.2 @ 0x0057caa0. Textured, white vertices, the opacity as alpha
// ([0x72f3bc]: puffers without colours, weather). Unlike 0x0057ce20 the TL
// vertices are only written once a queue or overlay slot is free, and opaque
// polygons draw flat. FIX (A): the device calls go to Render3D.
void draw_textured_white(int32_t count, const D3DTexture* texture, int32_t, float opacity, int32_t overlay) {
  TLVertex* tl = g_buffers.tl;
  DeviceState& s = g_state;
  const auto fill = [&](uint32_t diffuse) {
    for (int32_t i = count - 1; i >= 0; --i) tl[i].rhw = tl[i].sz, tl[i].specular = 0xff000000u, tl[i].diffuse = diffuse;
  };
  if (texture->type == 4 || !(opacity >= 1.0f)) {
    TLVertex* dst = nullptr;
    if (overlay == 0) {
      dst = queue_transparent(s.primitive, count, texture);
    } else if (OverlayPoly* p = overlay_slot(0, count, texture, 0x7e7)) {
      dst = p->verts;
    }
    if (!dst) return;
    fill(static_cast<uint32_t>(ftol(opacity * 255.0f)) << 24 | 0xffffffu);
    std::memcpy(dst, tl, sizeof(TLVertex) * count);
    return;
  }
  fill(0xffffffffu);
  if (overlay != 0) {
    if (OverlayPoly* p = overlay_slot(4, count, texture, 0x7b7)) std::memcpy(p->verts, tl, sizeof(TLVertex) * count);
    return;
  }
  set_shade(1);
  if (s.alpha_op != 2) s.alpha_op = 2, set_texture_stage_state(4, 2);  // SELECTARG1
  set_texture(texture);
  set_address(texture);
  draw_primitive(s.primitive, tl, count);
}

// Mech3.exe 1.2 @ 0x0057d160. Textured, lit or fogged, the first vertex's
// darkness for all: vertex colour * (1 - darkness), plus the point light per
// vertex (flag 4). FIX (A): the device calls go to Render3D.
void draw_textured_lit(uint32_t flags, int32_t count, const D3DTexture* texture, int32_t, float opacity,
                       int32_t overlay) {
  TLVertex* tl = g_buffers.tl;
  const WorkRecord* w = g_buffers.work;
  const float dark = 1.0f - w[0].darkness;
  fog_and_rhw(flags & 1, count);
  const uint32_t a = opacity_alpha(opacity);
  for (int32_t i = count - 1; i >= 0; --i) tl[i].diffuse = lit_rgb(w[i], dark, flags) | a;
  textured_tail(count, texture, opacity, overlay, 5, 0x953, 0x925, true);
}

// Mech3.exe 1.2 @ 0x0057d690. As 0x0057d160 with each vertex's own darkness.
// FIX (A): the device calls go to Render3D.
void draw_textured_vertex_lit(uint32_t flags, int32_t count, const D3DTexture* texture, float opacity,
                              int32_t overlay) {
  TLVertex* tl = g_buffers.tl;
  const WorkRecord* w = g_buffers.work;
  fog_and_rhw(flags & 1, count);
  const uint32_t a = opacity_alpha(opacity);
  for (int32_t i = count - 1; i >= 0; --i) tl[i].diffuse = lit_rgb(w[i], 1.0f - w[i].darkness, flags) | a;
  textured_tail(count, texture, opacity, overlay, 6, 0xa23, 0x9f2, true);
}

// Mech3.exe 1.2 @ 0x0057eeb0 (ECX count, EDX texture). FIX (A): the
// multi-texture DrawPrimitives go to Render3D::draw_multi.
void draw_shadowed(int32_t count, const D3DTexture* texture) {
  const WorkRecord* w = g_buffers.work;
  for (int32_t i = 0; i < count; ++i)
    multi_vertex(i, rgb_bits(round_byte(w[i].color[0]), round_byte(w[i].color[1]), round_byte(w[i].color[2])) |
                        0xff000000u,
                 0xff000000u);
  shadow_passes(count, texture);
}

// Mech3.exe 1.2 @ 0x0057f2a0 (ECX flags, EDX count, stack texture): the
// first vertex's darkness for all, as 0x0057d160.
void draw_shadowed_lit(uint32_t flags, int32_t count, const D3DTexture* texture) {
  const WorkRecord* w = g_buffers.work;
  const float dark = 1.0f - w[0].darkness;
  for (int32_t i = 0; i < count; ++i) multi_vertex(i, lit_rgb(w[i], dark, flags) | 0xff000000u, fog_alpha(flags, w[i]));
  shadow_passes(count, texture);
}

// Mech3.exe 1.2 @ 0x0057f860: each vertex's own darkness, as 0x0057d690.
void draw_shadowed_vertex_lit(uint32_t flags, int32_t count, const D3DTexture* texture) {
  const WorkRecord* w = g_buffers.work;
  for (int32_t i = 0; i < count; ++i)
    multi_vertex(i, lit_rgb(w[i], 1.0f - w[i].darkness, flags) | 0xff000000u, fog_alpha(flags, w[i]));
  shadow_passes(count, texture);
}

// Mech3.exe 1.2 @ 0x0057ea20 (ECX light, EDX flags; stack count, texture,
// priority (unused), opacity, overlay). Lit vertices round with the
// 1.5 * 2^52 trick, so at the game's 24-bit precision they come out black
// (docs/render-shadows.md). Transparent and overlay as 0x0057d690.
// FIX (A): the device calls go to Render3D.
void draw_projected(const float* light, uint32_t flags, int32_t count, const D3DTexture* texture, int32_t,
                    float opacity, int32_t overlay) {
  TLVertex* tl = g_buffers.tl;
  const WorkRecord* w = g_buffers.work;
  fog_and_rhw(flags & 1, count);
  // Rounded here (0.5 - opacity * -255), truncated in the other draws.
  uint32_t a = 0xff000000u;
  if (!(opacity >= 1.0f)) {
    const float s = opacity * -255.0f;
    a = static_cast<uint32_t>(ftol(0.5f - s)) << 24;
  }
  for (int32_t i = count - 1; i >= 0; --i) tl[i].diffuse = light_rgb(w[i], light ? light + 3 * i : nullptr) | a;
  textured_tail(count, texture, opacity, overlay, 6, 0xce1, 0xcb3, true);
}

// Mech3.exe 1.2 @ 0x0057fe20 (ECX light, EDX flags; stack count, texture).
// FIX (A): the multi-texture DrawPrimitives go to Render3D::draw_multi.
void draw_projected_shadowed(const float* light, uint32_t flags, int32_t count, const D3DTexture* texture) {
  const WorkRecord* w = g_buffers.work;
  for (int32_t i = 0; i < count; ++i)
    multi_vertex(i, light_rgb(w[i], light ? light + 3 * i : nullptr) | 0xff000000u, fog_alpha(flags, w[i]));
  shadow_passes(count, texture);
}

const uint32_t* multi_buffer() { return g_multi; }

// Mech3.exe 1.2 @ 0x0057dec0. Quicksort down to runs of 9, median of three
// (lo, mid, hi), then one insertion sort over the whole list; not stable.
void sort_transparent() {
  int32_t* order = g_queue.order;
  int32_t stack[300];  // 0x4b4 bytes of locals; the depth stays under 2 log2(6000)
  int32_t* sp = stack;
  int32_t lo = 0;
  int32_t hi = static_cast<int32_t>(g_queue.count) - 1;
  for (;;) {
    while (hi > lo + 8) {
      const int32_t mid = (hi + lo) >> 1;
      if (key(order[lo]) > key(order[mid])) std::swap(order[lo], order[mid]);
      if (key(order[lo]) > key(order[hi])) std::swap(order[lo], order[hi]);
      if (key(order[mid]) > key(order[hi])) std::swap(order[mid], order[hi]);
      const int32_t pivot = key(order[mid]);
      int32_t i = lo + 1;
      int32_t j = hi - 1;
      do {
        while (key(order[i]) < pivot) ++i;
        while (key(order[j]) > pivot) --j;
        if (i > j) break;
        std::swap(order[i], order[j]);
        ++i;
        --j;
      } while (i <= j);
      // Keep the smaller side, push the larger.
      if (j + i > hi + lo) {
        *sp++ = lo;
        *sp++ = j;
        lo = i;
      } else {
        *sp++ = i;
        *sp++ = hi;
        hi = j;
      }
    }
    if (sp <= stack) break;
    hi = *--sp;
    lo = *--sp;
  }
  for (int32_t k = 1; k < static_cast<int32_t>(g_queue.count); ++k) {
    const int32_t x = order[k];
    const int32_t kx = key(x);
    int32_t c = k;
    while (c > 0 && key(order[c - 1]) > kx) {
      order[c] = order[c - 1];
      --c;
    }
    order[c] = x;
  }
}

// Mech3.exe 1.2 @ 0x0057e110. FIX (A): the device calls go to Render3D.
void clear_fan(const float* xyz, int32_t count) {
  if (!g_render || count < 3 || count > 64) return;
  TLVertex v[64];
  for (int32_t i = 0; i < count; ++i) {
    const float* p = xyz + 3 * (count - 1 - i);
    v[i] = TLVertex{p[0], p[1], p[2], p[2], 0u, 0xff000000u, 0.0f, 0.0f};
  }
  platform::DrawState s;
  s.blend = true, s.depth_write = true, s.flat = true;
  g_render->draw(static_cast<platform::Primitive>(g_state.primitive), reinterpret_cast<const platform::TLVertex*>(v),
                 count, s);
}

void depth_fill(const int32_t rect[4]) {
  if (!g_render || !rect) return;
  const bool own = scene_depth() == 0;
  if (own) scene_begin();
  g_render->clear_depth_rect(rect[0], rect[1], rect[2], rect[3]);
  if (own) scene_end();
}

void flush_transparent() {
  TransparentQueue& q = g_queue;
  DeviceState& s = g_state;
  if (q.count == 0) return;
  set_shade(2);
  if (s.alpha_blend != 1) set_render_state(27, 1), s.alpha_blend = 1;
  if (s.z_write != 0) set_render_state(14, 0), s.z_write = 0;
  for (uint32_t i = 0; i < q.count; ++i) q.order[i] = static_cast<int32_t>(q.count - 1 - i);
  sort_transparent();
  for (uint32_t n = 0; n < q.count; ++n) {
    DeferredMaterialScope material_scope(
        std::move(g_transparent_materials[q.order[n]]));
    const NativeSkyScope sky_scope(g_transparent_sky[q.order[n]]);
    const TransparentPoly& p = q.polys[q.order[n]];
    if (p.texture) {
      // Compares a texture type with the cached ALPHAOP: anything but 2
      // (never a type) sends MODULATE, so ALPHAOP is always MODULATE here.
      const int32_t type = p.verts[0].diffuse > 0xff000000u ? 4 : p.texture->type;
      if (s.alpha_op != type) {
        s.alpha_op = type;
        set_texture_stage_state(4, type == 2 ? type : 4);
      }
      set_address(p.texture);
    }
    set_texture(p.texture);
    draw_primitive(p.primitive, p.verts, p.count);
  }
  if (s.alpha_blend != 0) set_render_state(27, 0), s.alpha_blend = 0;
  if (s.z_write != 1) set_render_state(14, 1), s.z_write = 1;
  q.count = 0;
  q.vertex_count = 0;
}

// Mech3.exe 1.2 @ 0x0057e4c0. Every entry is drawn with the CURRENT primitive
// [0x0072f418], not the one it was queued with; types 3 and 4 lose their
// Gouraud shading (flat). FIX (A): the device calls go to Render3D.
void flush_overlay() {
  OverlayList& o = g_overlay;
  DeviceState& s = g_state;
  if (o.count == 0) return;
  set_render_state(23, 8);  // ZFUNC ALWAYS
  for (int32_t n = 0; n < o.count; ++n) {
    DeferredMaterialScope material_scope(std::move(g_overlay_materials[n]));
    const NativeSkyScope sky_scope(g_overlay_sky[n]);
    const OverlayPoly& p = o.polys[n];
    const D3DTexture* t = p.texture;
    switch (p.type) {
      case 0:
        set_shade(2);
        if (s.alpha_blend != 1) set_render_state(27, 1), s.alpha_blend = 1;
        if (s.z_write != 0) set_render_state(14, 0), s.z_write = 0;
        set_texture(t);
        if (t) {
          if (t->type == 4 || (p.verts[0].diffuse & 0xff000000u) == 0xff000000u) {
            if (s.alpha_op != t->type) {
              s.alpha_op = t->type;
              set_texture_stage_state(4, t->type == 2 ? 2 : 4);
            }
          } else if (s.alpha_op != 4) {
            s.alpha_op = 4;
            set_texture_stage_state(4, 4);
          }
          set_address(t);
        }
        draw_primitive(s.primitive, p.verts, p.count);
        if (s.alpha_blend != 0) set_render_state(27, 0), s.alpha_blend = 0;
        if (s.z_write != 1) set_render_state(14, 1), s.z_write = 1;
        break;
      case 1:
      case 2:
      case 3:
        set_texture(nullptr);
        set_shade(1);
        draw_primitive(s.primitive, p.verts, p.count);
        break;
      case 4:
        set_shade(1);
        set_texture(t);
        if (s.alpha_op != t->type) {
          s.alpha_op = t->type;
          set_texture_stage_state(4, t->type == 2 ? 2 : 4);
        }
        set_address(t);
        draw_primitive(s.primitive, p.verts, p.count);
        break;
      case 5:
      case 6:
        set_shade(2);
        set_texture(t);
        if (s.alpha_op != 2) s.alpha_op = 2, set_texture_stage_state(4, 2);
        set_address(t);
        draw_primitive(s.primitive, p.verts, p.count);
        break;
      default:
        break;
    }
  }
  set_render_state(23, 7);  // ZFUNC GREATEREQUAL
  o.count = 0;
}

// Mech3.exe 1.2 @ 0x005798a0. The brightest channel is picked only when
// [0x008026e0] is set; on ties the lower index wins.
void set_point_light_color(const float rgb[3]) {
  PointLight& l = g_light;
  l.color[0] = rgb[0] * 255.0f;
  l.color[1] = rgb[1] * 255.0f;
  l.color[2] = rgb[2] * 255.0f;
  if (g_display.fullscreen == 0) return;
  auto below = [](float a, float b) { return !(a >= b); };  // x87 C0: a < b or unordered
  if (below(l.color[0], l.color[1])) {
    l.brightest = below(l.color[1], l.color[2]) ? (below(l.color[0], l.color[2]) ? 2 : 0) : 1;
  } else {
    l.brightest = below(l.color[0], l.color[2]) ? 2 : 0;
  }
}

// Mech3.exe 1.2 @ 0x0057dd80. Without a rect the quad takes the software
// surface's [0x008027cc] as its right and [0x008027c8] as its bottom: height
// and width swapped (unreached: 0x00558540 always passes one).
void set_tint_quad(uint16_t colour, const int32_t* rect, double alpha) {
  TintQuad& q = g_tint;
  q.pending = 1;
  float l, t, r, b;
  if (!rect) {
    const Surface& c = surfaces().copy;
    l = 0.0f, t = 0.0f;  // read as unsigned (FILD of a zero-extended qword)
    r = static_cast<float>(static_cast<uint32_t>(c.height)), b = static_cast<float>(static_cast<uint32_t>(c.width));
  } else {
    l = static_cast<float>(rect[0]), t = static_cast<float>(rect[1]);
    r = static_cast<float>(rect[2]), b = static_cast<float>(rect[3]);
  }
  q.verts[0].sx = l, q.verts[0].sy = t;
  q.verts[1].sx = r, q.verts[1].sy = t;
  q.verts[2].sx = r, q.verts[2].sy = b;
  q.verts[3].sx = l, q.verts[3].sy = b;
  const PixelFormat& f = pixel_format();
  const ChannelMasks& m = g_masks;
  const auto a = static_cast<uint32_t>(crt::msvc_ftol(static_cast<float>(alpha * 255.0)));
  uint32_t argb = (colour & m.r) >> (f.r_shift & 31) | a << 8;
  argb = argb << 8 | (colour & m.g) >> (f.g_shift & 31);
  argb = argb << 8 | (colour & m.b) << (f.b_shift_right & 31);
  for (TLVertex& v : q.verts) v.diffuse = argb;
}

// Mech3.exe 1.2 @ 0x0057e380 ([0x0072f3ec]): blended, untextured, Gouraud,
// no depth test or write, in the current primitive type.
void draw_tint_quad() {
  TintQuad& q = g_tint;
  DeviceState& s = g_state;
  if (!q.pending) return;
  set_shade(2);
  if (s.alpha_blend != 1) set_render_state(27, 1), s.alpha_blend = 1;
  if (s.z_write != 0) set_render_state(14, 0), s.z_write = 0;
  set_texture(nullptr);
  set_render_state(23, 8);
  draw_primitive(s.primitive, q.verts, 4);
  q.pending = 0;
  set_render_state(23, 7);
  if (s.alpha_blend != 0) set_render_state(27, 0), s.alpha_blend = 0;
  if (s.z_write != 1) set_render_state(14, 1), s.z_write = 1;
}

// Mech3.exe 1.2 @ 0x0057bcf0. FIX (A): FOGCOLOR goes to Render3D.
void send_fog_color() {
  const float* c = zrndr::device_fog().sent;
  set_render_state(34, static_cast<int32_t>(rgb_bits(round_byte(c[0]), round_byte(c[1]), round_byte(c[2]))));
}

// Mech3.exe 1.2 @ 0x00579950
void set_mix_fog_device(const float rgb[3]) {
  float* m = zrndr::device_fog().mix;
  for (int i = 0; i < 3; ++i) m[i] = rgb[i] * 255.0f;  // [0x005ceb44]
}

// Mech3.exe 1.2 @ 0x00579980: the device's fog call ([0x0072f3f0], 0x0057bcf0).
void send_mix_fog() {
  zrndr::DeviceFog& d = zrndr::device_fog();
  const auto same = [&](int i) { return !(d.sent[i] < d.mix[i] || d.sent[i] > d.mix[i]); };  // x87 C3: NaN counts
  if (same(0) && same(1) && same(2)) return;
  for (int i = 0; i < 3; ++i) d.sent[i] = d.mix[i];
  if (d.send) d.send();
}

// Mech3.exe 1.2 @ 0x0057a740. FIX (A): BeginScene and the HRESULT report go.
int32_t begin_scene() {
  if (g_render) g_render->begin_scene();
  Display& d = g_display;
  if (d.dirty & 1) {
    if (d.fill_mode == 0) set_render_state(8, 3);
    else if (d.fill_mode == 1) set_render_state(8, 2);
    d.dirty &= ~1u;
  }
  if (d.dirty & 2) {
    set_render_state(26, d.dither);
    d.dirty &= ~2u;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0057a7e0. FIX (A): no lost surfaces to restore.
int32_t end_scene() {
  if (g_render) g_render->end_scene();
  return 0;
}

// Mech3.exe 1.2 @ 0x005777f0 (ECX the rect, EDX the surface record). Colour
// fill with the packed clear colour when on, depth fill 0 (reversed depth) on
// the back buffer when there is a Z buffer. FIX (A): one Render3D::clear;
// only the back buffer exists there.
int32_t frame_clear(bool back_buffer) {
  const Display& d = g_display;
  if (!back_buffer || !g_render || !(d.clear_on || d.zbuffer)) return 0;
  const uint32_t c = unpack(clear_color(), 0);
  g_render->clear(d.clear_on != 0, static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8),
                  static_cast<uint8_t>(c));
  return 0;
}

// Mech3.exe 1.2 @ 0x0057a850. The original optionally blits [0x008027e8]
// into the back buffer first, then flips, waiting when asked. FIX (A):
// Render3D::present; the blit is not ported (docs/drafts/drawtable.md).
int32_t present(bool) {
  if (!g_display.primary) return 0x400;
  if (g_render) g_render->present();
  return 0;
}

// The surface 0x0057b130 fills (0x0057b6c0, 0x0057b7b0), decoded to RGBA8.
void texture_rgba(const zimg::Texture& image, int32_t alpha, uint8_t* rgba) {
  const ChannelMasks& m = g_masks;
  const auto* px = static_cast<const uint16_t*>(image.pixels);
  const uint8_t* plane = image.alpha;
  const int32_t n = image.width * image.height;
  for (int32_t i = 0; i < n; ++i) {
    const uint32_t p = px[i];
    uint8_t* o = rgba + 4 * i;
    if (alpha == 0) {  // the display format, copied
      o[0] = expand(p, m.r), o[1] = expand(p, m.g), o[2] = expand(p, m.b), o[3] = 0xff;
      continue;
    }
    uint32_t q;
    uint32_t ra, ga, ba, aa;
    if (!plane || g_display.alpha_4444 == 0) {
      // ARGB1555: green's low bit lands in blue's top bit (a quirk of the >> 1).
      q = (p & m.b) | ((p & (m.r | m.g)) >> 1) | (plane ? (plane[i] & 0x80u) << 8 : p != 0 ? 0x8000u : 0);
      ra = 0x7c00, ga = 0x3e0, ba = 0x1f, aa = 0x8000;
    } else {
      const bool g6 = pixel_format().g_bits == 6;
      const uint32_t rm = g6 ? 0xf000 : 0x7800, gm = g6 ? 0x780 : 0x3c0;
      q = ((p & m.b) >> 1) | ((gm & p) >> (g6 ? 3 : 2)) | ((rm & p) >> (g6 ? 4 : 3)) | (plane[i] & 0xf0u) << 8;
      ra = 0xf00, ga = 0xf0, ba = 0xf, aa = 0xf000;
    }
    q &= 0xffff;
    o[0] = expand(q, ra), o[1] = expand(q, ga), o[2] = expand(q, ba), o[3] = expand(q, aa);
  }
}

// Mech3.exe 1.2 @ 0x0057b130 (ECX name, EDX image, stack alpha, clamp u, v).
// The device checks stay, with a modern device's caps by default. The type
// is 4 whenever the image has an alpha plane, also when the surface had to be
// ARGB1555 (no 4444 support). FIX (A): one RGBA8 Render3D texture instead
// of the system and video surfaces, the Load and the pixel format globals
// (0x00579430).
D3DTexture* create_texture(const char* name, const zimg::Texture& image, int32_t alpha, int32_t clamp_u,
                           int32_t clamp_v) {
  const Display& d = g_display;
  const int32_t w = image.width, h = image.height;
  char msg[160];
  auto fail = [&](int line, const char* format) {
    std::snprintf(msg, sizeof msg, format, name, static_cast<int>(w), static_cast<int>(h));
    zerr::report(0x200, kFile, line, msg);
    return g_default;
  };
  if (static_cast<uint32_t>(w) > d.max_width || static_cast<uint32_t>(h) > d.max_height)
    return fail(0x257, "Texture [%s] dimensions [%d x %d] are too large.  Using default texture.");
  const bool non_square = w != h;
  if ((d.caps & 2) && (!is_pow2(w) || (non_square && !is_pow2(h))))
    return fail(0x269, "Texture [%s] dimensions [%d x %d] are not power of 2.Using default texture.");
  if (non_square && (w > 8 * h || h > 8 * w))
    return fail(0x278, "Texture [%s] dimensions [%d x %d] have bad aspect ratio.Using default texture.");
  // caps 0x20 (square only) would resize the image here (0x00519820); no
  // modern device asks for it, so that path is not ported.
  if (image.palette) {
    std::snprintf(msg, sizeof msg, "Texture [%s] Palettes not supported  Using default texture.", name);
    zerr::report(0x200, kFile, 0x28e, msg);
    return g_default;
  }
  const int32_t type = alpha == 0 ? 1 : image.alpha ? 4 : 5;
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
  texture_rgba(image, alpha, rgba.data());
  // The bank pixels can still be RGB565 with a separate alpha plane. Key the
  // packed format actually decoded for upload, not a hybrid of source pixels
  // and destination format. This also authenticates alpha-plane changes.
  const auto format = alpha == 0 ? 1u : image.alpha && d.alpha_4444 ? 3u : 2u;
  mw34::materials::ScopedUploadKey upload_key(
      mw34::materials::canonical_upload_key(w, h, rgba.data(), format));
  auto* t = new D3DTexture;
  t->handle = g_render ? g_render->create_named_texture(name, w, h, rgba.data(), static_cast<uint8_t>(type)) : -1;
  t->type = type;
  t->address_u = clamp_u ? 3 : 1;
  t->address_v = clamp_v ? 3 : 1;
  return t;
}

// Mech3.exe 1.2 @ 0x0057baa0. FIX (A): the copy into the system surface
// (0x0057b6c0) and the video texture's Load become one update of the handle.
void reload_texture(D3DTexture* record, const zimg::Texture* image) {
  if (!record || !image || record->handle < 0) return;
  const int32_t w = image->width, h = image->height;
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
  texture_rgba(*image, image->flags & 2, rgba.data());
  const auto format = (image->flags & 2) == 0 ? 1u : image->alpha && g_display.alpha_4444 ? 3u : 2u;
  mw34::materials::ScopedUploadKey upload_key(
      mw34::materials::canonical_upload_key(w, h, rgba.data(), format));
  if (g_render) g_render->update_texture(record->handle, w, h, rgba.data());
}

// Mech3.exe 1.2 @ 0x0057bb00. The exe releases the default records only while
// 0x005788b0 sets [0x00802710] (the device's shutdown), which has no port.
void release_texture(D3DTexture* record) {
  if (record == g_default) return;
  if (g_render && record->handle >= 0) g_render->release_texture(record->handle);
  delete record;
}

// Mech3.exe 1.2 @ 0x00579b70
int32_t has_caps(int32_t which) {
  const Display& d = g_display;
  if (!d.fullscreen) return 1;
  if (!d.device) return 0;
  if (which & 1) {
    if (!(d.tri_misc_caps & 0x40) || !(d.src_blend_caps & 1) || !(d.dest_blend_caps & 4)) return 0;
  }
  if (which & 2) {
    if (!(d.tri_misc_caps & 0x40) || !(d.address_caps & 4)) return 0;
  }
  return 1;
}

// Mech3.exe 1.2 @ 0x00579f50, the Direct3D entries the model draw calls
// ([0x72f3b0]..[0x72f3c8]) and the fog colour's send ([0x72f3f0]).
void install_model_draw_table() {
  zrndr::device_fog().send = send_fog_color;  // 0x00579fe9: [0x0072f3f0] = 0x0057bcf0
  zmod::DrawTable& t = zmod::draw_table();
  t.ctx = nullptr;
  t.flat = [](void*, uint32_t color, int32_t alpha, int32_t count, int32_t priority, int32_t overlay) {
    draw_flat(color, alpha, count, priority, overlay);
  };
  t.point = [](void*, const float screen[3], uint32_t color, int32_t alpha) { draw_point(screen, color, alpha); };
  t.lit = [](void*, uint32_t color, const float* rgb, uint32_t fogged, int32_t alpha, int32_t count, int32_t priority,
             int32_t overlay) { draw_lit(color, rgb, fogged, alpha, count, priority, overlay); };
  t.textured = [](void*, int32_t count, void* texture, float opacity, int32_t overlay) {
    draw_textured(count, static_cast<const D3DTexture*>(texture), opacity, overlay);
  };
  t.textured_white = [](void*, int32_t count, void* texture, int32_t priority, float opacity, int32_t overlay) {
    draw_textured_white(count, static_cast<const D3DTexture*>(texture), priority, opacity, overlay);
  };
  t.textured_lit = [](void*, uint32_t fogged, int32_t count, void* texture, int32_t priority, float opacity,
                      int32_t overlay) {
    draw_textured_lit(fogged, count, static_cast<const D3DTexture*>(texture), priority, opacity, overlay);
  };
  t.textured_vertex = [](void*, uint32_t fogged, int32_t count, void* texture, float opacity, int32_t overlay) {
    draw_textured_vertex_lit(fogged, count, static_cast<const D3DTexture*>(texture), opacity, overlay);
  };
  t.shadowed = [](void*, int32_t count, void* texture) {
    draw_shadowed(count, static_cast<const D3DTexture*>(texture));
  };
  t.shadowed_lit = [](void*, uint32_t fogged, int32_t count, void* texture) {
    draw_shadowed_lit(fogged, count, static_cast<const D3DTexture*>(texture));
  };
  t.shadowed_vertex = [](void*, uint32_t fogged, int32_t count, void* texture) {
    draw_shadowed_vertex_lit(fogged, count, static_cast<const D3DTexture*>(texture));
  };
  t.projected = [](void*, const float* light, uint32_t fogged, int32_t count, void* texture, int32_t priority,
                   float opacity, int32_t overlay) {
    draw_projected(light, fogged, count, static_cast<const D3DTexture*>(texture), priority, opacity, overlay);
  };
  t.projected_shadowed = [](void*, const float* light, uint32_t fogged, int32_t count, void* texture) {
    draw_projected_shadowed(light, fogged, count, static_cast<const D3DTexture*>(texture));
  };
}

}  // namespace zvid

