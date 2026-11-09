#pragma once
#include <cstdint>

#include "platform/render3d.h"

namespace zimg {
struct Texture;
}
namespace zmod {
struct ShadowSets;
}

// The Direct3D 6 draw backend (zvid_ddd3d.c): the draw table the model draw
// calls ([0x0072f3b0].., filled by 0x00579f50), the render-state cache, the
// transparent queue and the overlay list, texture records and the frame calls
// (docs/render.md sections 3 and 4, docs/drafts/drawtable.md,
// docs/render-fogsort.md). FIX (A): every IDirect3DDevice3 call becomes the
// platform::Render3D state or call it stands for (set_render3d).
namespace zvid {

using TLVertex = platform::TLVertex;  // D3DTLVERTEX, FVF 0x1c4

// The texture record 0x0057b130 makes (0x18 bytes in the original).
// FIX (A): the system and video surfaces (+0x00, +0x04) and the
// IDirect3DTexture2 (+0x08) become one Render3D handle.
struct D3DTexture {
  int32_t handle = -1;             // +0x08 what SetTexture gets
  int32_t type = 1;                // +0x0c 1 opaque 565, 5 ARGB1555, 4 ARGB4444
  int32_t address_u = 1;           // +0x10 D3DTADDRESS (1 wrap, 3 clamp)
  int32_t address_v = 1;           // +0x14
};

struct TransparentPoly {           // 0x0080cd9c, 0x10 bytes each
  int32_t primitive;               // +0x00 D3DPRIMITIVETYPE, from [0x0072f418]
  int32_t count;                   // +0x04 vertices
  const D3DTexture* texture;       // +0x08 null when untextured
  TLVertex* verts;                 // +0x0c into TransparentQueue::verts
};

struct TransparentQueue {
  static constexpr uint32_t kMaxPolys = 6000;   // MAX_TRANSPARENT_POLYS
  static constexpr uint32_t kMaxVerts = 24000;  // MAX_TRANSPARENT_VERTICES
  uint32_t count = 0;                // 0x00803550
  uint32_t vertex_count = 0;         // 0x00803554
  int32_t order[kMaxPolys] = {};     // 0x00803558  draw order, indices into polys
  TransparentPoly polys[kMaxPolys] = {};  // 0x0080cd9c
  TLVertex verts[kMaxVerts] = {};    // 0x0082449c
};
TransparentQueue& transparent_queue();

// Node flag 0x800000 polygons, drawn last with no depth test (0x0057e4c0).
struct OverlayPoly {               // 0x008dfc9c, 0x80c bytes each
  int32_t type;                    // +0x00 how 0x0057e4c0 draws it: 0 blended, 1 flat,
                                   //       3 untextured Gouraud, 4/5/6 textured
  int32_t count;                   // +0x04
  const D3DTexture* texture;       // +0x08 null when untextured
  TLVertex verts[64];              // +0x0c
};
struct OverlayList {
  static constexpr int32_t kMax = 0x180;  // ZVID_MAX_OVERWRITE_POLYS
  int32_t count = 0;               // 0x00809318
  OverlayPoly polys[kMax] = {};    // 0x008dfc9c
};
OverlayList& overlay_list();

// The vertices 0x0057bf80 builds for an opaque flat polygon (64 slots).
TLVertex* flat_buffer();           // 0x0080c59c

// The device's cached render states. They decide when a state is sent; the
// state the device holds is draw_state().
struct DeviceState {
  int32_t primitive = 6;           // 0x0072f418  current D3DPRIMITIVETYPE
  int32_t alpha_blend = 0;         // 0x00809388  ALPHABLENDENABLE (27)
  int32_t shade_mode = 0;          // 0x0080938c  SHADEMODE (9)
  int32_t alpha_op = 0;            // 0x00809390  ALPHAOP (4); holds a texture type, see 0x0057e110
  int32_t address_u = 0;           // 0x00809394  ADDRESSU (13)
  int32_t address_v = 0;           // 0x00809398  ADDRESSV (14)
  int32_t z_write = 0;             // 0x008093a8  ZWRITEENABLE (14)
  int32_t fog = 0;                 // 0x008093b0  FOGENABLE (28) as 0x0057bb70 last sent it
  int32_t fog_mode = 0;            // 0x008093b8  the light state FOGMODE (4)
};
DeviceState& device_state();

// FIX (A): the device the calls go to, and the state it holds (what the
// original's SetRenderState / SetTextureStageState / SetTexture calls left).
void set_render3d(platform::Render3D* render);
platform::Render3D* render3d();
bool set_native_sky(bool enabled) noexcept;
platform::DrawState& draw_state();

// The per-polygon buffers the model draw fills and the draw functions read
// (docs/render.md section 3). Default: zmod::clip2d()'s tl and work.
struct WorkRecord {                // 0x20 bytes (= zmod::VertexWork)
  float darkness;                  // +0x00 1 - directed light
  float point;                     // +0x04 point-light intensity
  float fog;                       // +0x08
  float color[3];                  // +0x0c vertex colour, 0..255
  float unused[2];
};
struct DrawBuffers {
  TLVertex* tl;                    // 0x0072f41c
  const WorkRecord* work;          // 0x007481a8
  const zmod::ShadowSets* shadow;  // 0x007489a8  the projected-shadow UV sets and textures
};
DrawBuffers& draw_buffers();

struct PointLight {                // set by 0x005798a0
  float color[3] = {};             // 0x0072f40c  0..255
  int32_t brightest = 0;           // 0x00802704  index of the largest channel
};
PointLight& point_light();

// The display's 16-bit channel masks, args 2-4 of 0x005792c0 (zvid_format
// keeps the rest of that call).
struct ChannelMasks {
  uint32_t r = 0xf800, g = 0x07e0, b = 0x001f;  // 0x0080272c, 0x00802730, 0x00802734
};
ChannelMasks& channel_masks();

// Display and device flags the frame calls and texture records read.
struct Display {
  int32_t fullscreen = 1;          // 0x008026e0
  int32_t clear_on = 1;            // 0x008026f0  the frame clear fills the colour too
  uint32_t dirty = 0;              // 0x008026f8  1 fill mode, 2 dither changed
  int32_t fill_mode = 0;           // 0x008026fc  0 solid, 1 wireframe
  int32_t dither = 0;              // 0x00802700
  int32_t bits_per_pixel = 16;     // 0x00802718
  bool zbuffer = true;             // [0x00809378] != 0
  bool primary = true;             // [0x0080280c] != 0
  // Device caps (FIX (A): a modern GPU has none of these limits).
  uint32_t max_width = 4096;       // [0x0080c278] +0x148
  uint32_t max_height = 4096;      // [0x0080c278] +0x14c
  uint32_t caps = 0;               // 0x00809448  2 power of two only, 0x20 square only
  int32_t alpha_4444 = 1;          // [0x00809644] +0xb04  ARGB4444 textures
  // What 0x00579b70 tests, read as the D3D6 triangle caps (dpcTriCaps at
  // 0x008093c4) [unverified]: 0x40 of its misc caps, src blend ZERO, dest
  // blend SRCCOLOR (the shadow multiply), texture address CLAMP.
  bool device = true;              // [0x00809644] != 0
  uint32_t tri_misc_caps = 0x40;   // 0x008093c8
  uint32_t src_blend_caps = 1;     // 0x00809438
  uint32_t dest_blend_caps = 4;    // 0x0080943c
  uint32_t address_caps = 4;       // 0x00809454
};
Display& display();
int set_dither(int on);  // 0x00579240 ECX on (VideoSetDither): [0x00802700] only
// ECX which: bit 1 the multiply blend, bit 2 clamped addressing. 1 when the
// display is not fullscreen.
int32_t has_caps(int32_t which);  // 0x00579b70

// The draw table. ECX, EDX and the stack arguments as the model draw passes
// them (docs/render.md section 3); counts are 1..64, the TL vertices in
// draw_buffers().tl, already projected.
void draw_flat(uint32_t color, int32_t alpha, int32_t count, int32_t priority, int32_t overlay);  // 0x0057bf80
void draw_point(const float screen[3], uint32_t color, int32_t alpha);  // 0x0057dba0
void draw_lit(uint32_t color, const float rgb[3], uint32_t flags, int32_t alpha, int32_t count,
              int32_t priority, int32_t overlay);                                       // 0x0057c700
void draw_textured(int32_t count, const D3DTexture* texture, float opacity, int32_t overlay);  // 0x0057ce20
// Textured with white vertices (puffers without colours, weather); priority unused.
void draw_textured_white(int32_t count, const D3DTexture* texture, int32_t priority, float opacity,
                         int32_t overlay);                                              // 0x0057caa0
void draw_textured_lit(uint32_t flags, int32_t count, const D3DTexture* texture, int32_t priority,
                       float opacity, int32_t overlay);                                 // 0x0057d160
void draw_textured_vertex_lit(uint32_t flags, int32_t count, const D3DTexture* texture, float opacity,
                              int32_t overlay);                                         // 0x0057d690

// The projected-shadow draws (docs/render-shadows.md): the polygon once with
// its texture, then once per shadow multiplied by the shadow's texture
// (draw_buffers().shadow), each UV set its own texture coordinate; fog off
// for those passes and ON afterwards (the only FOGENABLE in the game). Never
// transparent, never in the overlay list; opaque white alpha.
void draw_shadowed(int32_t count, const D3DTexture* texture);                       // 0x0057eeb0
void draw_shadowed_lit(uint32_t flags, int32_t count, const D3DTexture* texture);   // 0x0057f2a0
void draw_shadowed_vertex_lit(uint32_t flags, int32_t count, const D3DTexture* texture);  // 0x0057f860
// One triangle of a polygon under projected lights (0x00547740): the vertex
// colour times light (3 per vertex, rgb multipliers; null: the colour alone).
void draw_projected(const float* light, uint32_t flags, int32_t count, const D3DTexture* texture,
                    int32_t priority, float opacity, int32_t overlay);              // 0x0057ea20
void draw_projected_shadowed(const float* light, uint32_t flags, int32_t count,
                             const D3DTexture* texture);                           // 0x0057fe20
// The interleaved vertices the shadowed draws hand DrawPrimitive (0x008008e0).
const uint32_t* multi_buffer();

// 0x00579f50's table fill (0x579fe9..0x57a165), the part the model draw and
// the puffers use: points zmod::draw_table() at the draw functions above.
void install_model_draw_table();

// The block every transparent draw inlines (0x0057bf80, 0x0057c2f0, 0x0057caa0,
// 0x0057ce20, 0x0057d160, 0x0057d690, 0x0057dba0): reserves a polygon and its
// vertices, or reports the overflow and returns null. The caller fills them.
TLVertex* queue_transparent(int32_t primitive, int32_t count, const D3DTexture* texture);

void sort_transparent();   // 0x0057dec0
void flush_transparent();  // 0x0057e110
// Mech3.exe 1.2 @ 0x00576ce0 ([0x00809348] = 0x00577770): the depth surface
// filled with 0 inside `rect` (left, top, right, bottom), a DDBLT_DEPTHFILL
// Blt. FIX (A): Render3D's clear_depth_rect, in a scene of its own when none
// is open (a draw outside one would be dropped by the next begin).
void depth_fill(const int32_t rect[4]);
// Mech3.exe 1.2 @ 0x0057bd50 ([0x0072f3ac], ECX points, EDX count): a fan of
// screen points (x, y, depth), last to first, blended with the clear texture
// "zVidDefaultClear" ([0x00809320]), flat, depth written: the zoom circle's
// mask. FIX (A): the texture is not made; the fan is drawn untextured at
// alpha 0, the same nothing with the same depth.
void clear_fan(const float* xyz, int32_t count);
void flush_overlay();      // 0x0057e4c0

void set_point_light_color(const float rgb[3]);  // 0x005798a0, rgb 0..1
void send_fog_color();     // 0x0057bcf0: zrndr::device_fog().sent to FOGCOLOR
void set_mix_fog_device(const float rgb[3]);  // 0x00579950: zrndr::device_fog().mix = rgb x 255
void send_mix_fog();       // 0x00579980: the mix as the device's fog colour when it differs from the one sent

// The full-screen colour quad (flashes and fades): 0x0057dd80 fills it,
// [0x0072f3ec] = 0x0057e380 draws it once.
struct TintQuad {
  int32_t pending = 0;               // 0x008095bc
  TLVertex verts[4] = {};            // 0x008095c0  left-top, right-top, right-bottom, left-bottom
};
TintQuad& tint_quad();
// 0x0057bb70 (ECX on), from the device's setup with 1: FOGENABLE, and the
// light state FOGMODE linear (3). FIX (A): the light state is Render3D's
// own vertex fog; only the enable is sent.
void set_fog(int32_t on);
void reset_tint_quad();             // 0x00579c60's part: none pending, specular 0xff000000
void set_tint_depth(float z);       // 0x0057dd40: sz and rhw of the four vertices
// Mech3.exe 1.2 @ 0x0057dd80 (ECX colour, EDX rect or null; the alpha a double).
void set_tint_quad(uint16_t colour, const int32_t* rect, double alpha);
void draw_tint_quad();     // 0x0057e380

int32_t begin_scene();     // 0x0057a740
int32_t end_scene();       // 0x0057a7e0
int32_t frame_clear(bool back_buffer);         // 0x005777f0
int32_t present(bool wait);                    // 0x0057a850

// 0x0057b130: a texture record for an image (16-bit pixels in the display
// format, optional alpha plane), or default_texture() when the device cannot
// take it. alpha: make an alpha format; clamp_u/v: address modes.
D3DTexture* create_texture(const char* name, const zimg::Texture& image, int32_t alpha,
                           int32_t clamp_u, int32_t clamp_v);
D3DTexture*& default_texture();                // 0x0080931c
// [0x0072f374] = 0x0057baa0 (ECX record, EDX unused, stack image): the image
// copied into the record's texture again (the image's alpha flag chooses the
// conversion; the record keeps its type and address modes).
void reload_texture(D3DTexture* record, const zimg::Texture* image);
// [0x0072f378] = 0x0057bb00: frees the record and its texture; the default
// record is kept.
void release_texture(D3DTexture* record);

// What 0x0057b130 uploads, as RGBA8 (the surface format's precision kept).
void texture_rgba(const zimg::Texture& image, int32_t alpha, uint8_t* rgba);

}  // namespace zvid
