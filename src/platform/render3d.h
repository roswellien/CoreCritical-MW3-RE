#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>
#include "frontend/core_critical/renderer/geometry/resource.hpp"

// What the rebuilt Direct3D layer (zvid_ddd3d.c, docs/render.md section 4)
// needs from the machine: stands in for IDirect3DDevice3 and the DirectDraw
// surfaces behind it. The game keeps its own transform, cull, clip and
// projection, so vertices arrive already in screen pixels, as the original's
// D3DTLVERTEX (FVF 0x1c4) did. FIX (A): Direct3D 6 does not exist on macOS and
// is legacy on Windows; the SDL3 GPU front end implements this, a recorder
// does for tests.
namespace platform {

struct TLVertex {                  // D3DTLVERTEX, 0x20 bytes
  float sx, sy;                    // pixels in the viewport
  float sz;                        // depth: near / z, so larger is nearer (reversed)
  float rhw;                       // 1 / w, for perspective-correct UVs
  uint32_t diffuse;                // ARGB 8888
  uint32_t specular;               // ARGB; alpha = fog factor (255 no fog)
  float tu, tv;
};
static_assert(sizeof(TLVertex) == 0x20, "TLVertex must match D3DTLVERTEX");

enum class Primitive : uint8_t { kPoints = 1, kStrip = 5, kFan = 6 };  // D3DPRIMITIVETYPE values used

// The render and texture-stage states the draw functions set (section 4).
struct DrawState {
  bool native_sky{};
  int32_t texture = -1;            // SetTexture(0, ...): a handle from create_texture, -1 none
  uint8_t address_u = 1;           // ADDRESSU: 1 WRAP, 3 CLAMP
  uint8_t address_v = 1;           // ADDRESSV
  bool blend = false;              // ALPHABLENDENABLE with SRCALPHA / INVSRCALPHA
  bool multiply = false;           // the shadow passes: SRCBLEND ZERO, DESTBLEND SRCCOLOR
  bool depth_write = true;         // ZWRITEENABLE
  bool depth_always = false;       // ZFUNC ALWAYS (overlays), else GREATEREQUAL
  bool alpha_modulate = false;     // ALPHAOP MODULATE (transparent), else SELECTARG1
  bool fog = false;                // FOGENABLE
  bool flat = false;               // SHADEMODE FLAT
  bool wireframe = false;          // FILLMODE WIREFRAME (a debug mode)
  bool color_select = false;       // COLOROP SELECTARG1: the texture's colour alone (shadow passes)
  uint8_t texcoord = 0;            // TEXCOORDINDEX: which UV set of a draw_multi vertex
  bool sharp = false;              // ours: the 2D layer, a texel a game pixel, premultiplied, scaled sharp
};

// A synchronous model-producer view, valid until end_model_draw(). Matrices use
// GameZ row vectors. Original polygon/light/cache work still runs within the scope.
struct ModelDrawContext {
  const void* node = nullptr;
  const float* model_to_world = nullptr;
  const float* model_to_camera = nullptr;
  float near_z = 0, clip_near = 0, far_z = 0, fx_near = 0, fy_near = 0, cx = 0, cy = 0;
  float opacity = 1;
  bool ordinary = false, morphing = false, overlay = false;
  uint32_t source_vertices = 0, source_polygons = 0;
};

// Borrowed native material scope. Valid only inside the synchronous model
// scope; never store these pointers in a render-worker packet or GPU resource.
struct MaterialDrawContext {
  const void *model = nullptr;
  const void *material = nullptr;
  const void *texture_entry = nullptr;
  uint32_t polygon = 0;
};

// Opaque owned admission receipt. Implementations must keep it pointer-free
// with respect to native Node/Model/Material/TextureEntry storage.
struct DeferredMaterialBinding {
  virtual ~DeferredMaterialBinding() = default;
};

// Typed mission-change notification: the 0-based campaign world (launch
// index) and the mission number within it. {-1, 0} is the empty identity
// (failed load, non-campaign game type or scene reset).
struct MissionIdentity {
  int32_t world_index = -1;
  int32_t number = 0;
};

inline MissionIdentity make_mission_identity(int32_t game_type,
                                             int32_t world_index,
                                             int32_t number,
                                             bool load_success) {
  // IA and multiplayer numbers name iaN/mpN blocks, not campaign mN blocks.
  if (!load_success || (game_type != 1 && game_type != 4)) {
    return MissionIdentity{-1, 0};
  }
  return MissionIdentity{world_index, number};
}

class Render3D {
 public:
  virtual ~Render3D() = default;
  // What the load screen names as the video driver (ours; 0x005799f0 named the DirectDraw driver).
  virtual const char* driver_name() const { return "Default"; }
  virtual cc::geometry::Mode geometry_mode() const { return cc::geometry::Mode::legacy; }
  virtual cc::geometry::Submission submit_prepared_geometry(const ModelDrawContext&,
                                                            const cc::geometry::Fog&) {
    return cc::geometry::Submission::unsupported;
  }
  virtual cc::geometry::Submission submit_model_geometry(
      std::shared_ptr<const cc::geometry::Resource>, const ModelDrawContext&,
      const std::vector<cc::geometry::Material>&, const cc::geometry::Fog&) {
    return cc::geometry::Submission::unsupported;
  }
  virtual void note_cpu_model_geometry() {}
  virtual bool wants_model_context() const { return false; }
  // Typed mission-change callback from the native missions object. The
  // default is a no-op so renderers that do not care are unaffected.
  virtual void mission_changed(const MissionIdentity &) {}
  virtual bool wants_material_context() const { return false; }
  virtual std::shared_ptr<const DeferredMaterialBinding>
  capture_deferred_material_binding(int32_t) {
    return {};
  }
  virtual void
  begin_deferred_material_draw(std::shared_ptr<const DeferredMaterialBinding>) {
  }
  virtual void end_deferred_material_draw() noexcept {}
  // Source geometry is read-only within model draw scopes. A producer that
  // changes it must invalidate before submitting another polygon.
  virtual void invalidate_material_geometry() noexcept {}

  virtual void begin_material_draw(const MaterialDrawContext &) {}
  virtual void end_material_draw() noexcept {}
  virtual void begin_model_draw(const ModelDrawContext&) {}
  virtual void end_model_draw() noexcept {}
  virtual bool replaces_model_geometry() const { return false; }
  // 0x0057b130's surfaces: RGBA8, width * height * 4 bytes. `type` is the
  // record's 1 opaque, 4 alpha plane, 5 one-bit alpha; returns a handle.
  virtual int32_t create_texture(int width, int height, const uint8_t* rgba, uint8_t type) = 0;
  virtual int32_t create_named_texture(const char*, int width, int height, const uint8_t* rgba,
                                       uint8_t type) { return create_texture(width, height, rgba, type); }
  virtual void release_texture(int32_t handle) = 0;
  // 0x0057baa0's reload: new pixels for a texture create_texture made, same size.
  virtual void update_texture(int32_t handle, int width, int height, const uint8_t* rgba) = 0;
  virtual void set_viewport(int x, int y, int width, int height) = 0;  // Viewport3::SetViewport2
  // The frame clear (0x005777f0): colour when asked, depth always to 0 (reversed).
  virtual void clear(bool color, uint8_t r, uint8_t g, uint8_t b) = 0;
  // The depth inside a rect (left, top, right, bottom: pixels, exclusive)
  // back to 0, the colour kept: the depth surface's DDBLT_DEPTHFILL Blt
  // (0x00577770 through 0x00576ce0), which a HUD 3D view starts with. The
  // default draws an unseen quad there (alpha 0, blended) at depth 0 with
  // ZFUNC ALWAYS and depth writes on.
  virtual void clear_depth_rect(int left, int top, int right, int bottom) {
    DrawState s;
    s.blend = true, s.depth_write = true, s.depth_always = true;
    const float l = static_cast<float>(left), t = static_cast<float>(top), r = static_cast<float>(right),
                b = static_cast<float>(bottom);
    const TLVertex v[4] = {{l, t, 0, 1, 0, 0xff000000u, 0, 0}, {r, t, 0, 1, 0, 0xff000000u, 0, 0},
                           {r, b, 0, 1, 0, 0xff000000u, 0, 0}, {l, b, 0, 1, 0, 0xff000000u, 0, 0}};
    draw(Primitive::kFan, v, 4, s);
  }
  virtual void begin_scene() = 0;  // BeginScene (vt+0x24)
  virtual void end_scene() = 0;    // EndScene (vt+0x28)
  // DrawPrimitive (vt+0x70) with flags 0x18.
  virtual void draw(Primitive type, const TLVertex* vertices, int count, const DrawState& state) = 0;
  // DrawPrimitive with FVF 0x0c4 | uv_sets << 8 (the projected-shadow draws,
  // docs/render-shadows.md): per vertex sx, sy, sz, rhw, diffuse, specular,
  // then uv_sets (u, v) pairs, 6 + 2 * uv_sets dwords. The default draws the
  // set state.texcoord as a TLVertex, white in place of diffuse when
  // color_select, so a backend needs nothing more.
  virtual void draw_multi(Primitive type, const uint32_t* vertices, int uv_sets, int count, const DrawState& state) {
    std::vector<TLVertex> v(static_cast<size_t>(count > 0 ? count : 0));
    const int stride = 6 + 2 * uv_sets;
    const int set = state.texcoord < uv_sets ? state.texcoord : 0;
    for (int i = 0; i < count; ++i) {
      const uint32_t* s = vertices + stride * i;
      std::memcpy(&v[i], s, 24);
      std::memcpy(&v[i].tu, s + 6 + 2 * set, 8);
      if (state.color_select) v[i].diffuse |= 0x00ffffffu;
    }
    draw(type, v.data(), count, state);
  }
  virtual void set_fog_color(uint8_t r, uint8_t g, uint8_t b) = 0;  // FOGCOLOR (0x0057bcf0)
  virtual void reset_scene_resources() {}
  // Complete queued host rendering and surface asynchronous failures before a
  // window mutation, readback, world teardown or final timing report.
  virtual void drain() {}
  virtual void present() = 0;      // Flip, or the windowed Blt (0x0057a850 / 0x00577130)
  // FIX (A): the back buffer read back (DirectDraw's Lock read it in the exe;
  // a GPU frame has to be downloaded): 0xAARRGGBB, top row first, at the
  // back buffer's own size. False when the backend cannot.
  virtual bool read_back(std::vector<uint32_t>* argb, int* width, int* height) {
    (void)argb, (void)width, (void)height;
    return false;
  }
  // Ours (playtest W25): the frame just shown kept at its own size, put back
  // as the next frame's start (a message box's 2D over it), and let go.
  virtual bool hold_frame() { return false; }
  virtual bool restore_frame() { return false; }
  virtual void drop_frame() {}
};

}  // namespace platform
