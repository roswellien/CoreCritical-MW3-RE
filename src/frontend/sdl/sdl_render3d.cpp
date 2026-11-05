#include <string>
#include "frontend/sdl/sdl_render3d.h"
#include "frontend/sdl/sdl_child.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sdlfe {
namespace {

// Screen pixels in, as Direct3D's pre-transformed vertices: clip w = 1 / rhw
// keeps UVs perspective-correct, depth is sz (reversed: nearer is larger).
const char* kShaders = R"(
#include <metal_stdlib>
using namespace metal;
struct In {
  float4 xyzrhw [[attribute(0)]];
  float4 diffuse [[attribute(1)]];
  float4 specular [[attribute(2)]];
  float2 uv [[attribute(3)]];
};
struct Out { float4 pos [[position]]; float4 diffuse; float4 specular; float2 uv; };
struct V { float4 viewport; };
vertex Out vs(In in [[stage_in]], constant V& v [[buffer(0)]]) {
  Out o;
  float w = in.xyzrhw.w > 0.0 ? 1.0 / in.xyzrhw.w : 1.0;
  float x = (in.xyzrhw.x - v.viewport.x) / v.viewport.z * 2.0 - 1.0;
  float y = 1.0 - (in.xyzrhw.y - v.viewport.y) / v.viewport.w * 2.0;
  o.pos = float4(x * w, y * w, in.xyzrhw.z * w, w);
  o.diffuse = in.diffuse.zyxw;     // ARGB in memory is B, G, R, A
  o.specular = in.specular.zyxw;
  o.uv = in.uv;
  return o;
}
// opts.x fog on, opts.y alpha modulate, opts.z sharp sampling enabled
struct F { float4 fog; float4 opts; };
float4 finish(float4 c, Out in, constant F& f) {
  if (f.opts.x > 0.5) c.rgb = mix(f.fog.rgb, c.rgb, in.specular.a);
  return c;
}
// Sharp bilinear: each texel flat but for one frame pixel's blend at its
// edges, so a whole scale is exactly nearest and a fractional one stays
// crisp. The texels are premultiplied, so a key's clear texel blends no black.
float4 sample_sharp(texture2d<float> t, sampler s, float2 uv) {
  float2 size = float2(t.get_width(), t.get_height());
  float2 scale = max(float2(1.0), 1.0 / max(fwidth(uv) * size, float2(0.000001)));
  float2 texel = uv * size, base = floor(texel), d = texel - base - 0.5;
  float2 r = 0.5 - 0.5 / scale;
  float4 c = t.sample(s, (base + (d - clamp(d, -r, r)) * scale + 0.5) / size);
  return c.a > 0.0 ? float4(c.rgb / c.a, c.a) : c;
}
fragment float4 fs_tex(Out in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]],
                       constant F& f [[buffer(0)]]) {
  float4 tex = f.opts.z > 0.0 ? sample_sharp(t, s, in.uv) : t.sample(s, in.uv);
  float4 c = float4(tex.rgb * in.diffuse.rgb, f.opts.y > 0.5 ? tex.a * in.diffuse.a : tex.a);
  return finish(c, in, f);
}
fragment float4 fs_flat(Out in [[stage_in]], constant F& f [[buffer(0)]]) {
  return finish(in.diffuse, in, f);
}
)";

uint32_t key_of(const platform::DrawState& s, bool textured) {
  return textured | s.blend << 1 | s.multiply << 2 | s.depth_write << 3 | s.depth_always << 4 | s.wireframe << 5;
}

bool same(const platform::DrawState& a, const platform::DrawState& b) {
  return a.texture == b.texture && a.address_u == b.address_u && a.address_v == b.address_v && a.blend == b.blend &&
         a.multiply == b.multiply && a.depth_write == b.depth_write && a.depth_always == b.depth_always &&
         a.alpha_modulate == b.alpha_modulate && a.fog == b.fog && a.wireframe == b.wireframe && a.sharp == b.sharp;
}

#if defined(MW3_HAVE_DXIL)
#include "fs_flat.h"  // sdl_render3d.hlsl, by dxc (CMakeLists.txt)
#include "fs_tex.h"
#include "vs.h"
#endif
#if defined(MW3_HAVE_SPIRV)
#include "spv_fs_flat.h"  // sdl_render3d.glsl, by glslangValidator (CMakeLists.txt)
#include "spv_fs_tex.h"
#include "spv_vs.h"
#endif

SDL_GPUShader* make_shader(SDL_GPUDevice* dev, const char* entry, SDL_GPUShaderStage stage, int samplers) {
  SDL_GPUShaderCreateInfo info{};
#if defined(MW3_HAVE_DXIL)
  if (SDL_GetGPUShaderFormats(dev) & SDL_GPU_SHADERFORMAT_DXIL) {
    const std::string e = entry;
    const auto set = [&](const unsigned char* code, size_t n) { info.code = code, info.code_size = n; };
    if (e == "vs") set(kDxil_vs, sizeof kDxil_vs);
    else if (e == "fs_tex") set(kDxil_fs_tex, sizeof kDxil_fs_tex);
    else set(kDxil_fs_flat, sizeof kDxil_fs_flat);
    info.entrypoint = entry;
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
  } else
#endif
#if defined(MW3_HAVE_SPIRV)
  if (SDL_GetGPUShaderFormats(dev) & SDL_GPU_SHADERFORMAT_SPIRV) {
    const std::string e = entry;
    const auto set = [&](const uint32_t* code, size_t n) { info.code = reinterpret_cast<const Uint8*>(code), info.code_size = n; };
    if (e == "vs") set(kSpv_vs, sizeof kSpv_vs);
    else if (e == "fs_tex") set(kSpv_fs_tex, sizeof kSpv_fs_tex);
    else set(kSpv_fs_flat, sizeof kSpv_fs_flat);
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  } else
#endif
  {
    info.code = reinterpret_cast<const Uint8*>(kShaders);
    info.code_size = std::strlen(kShaders);
    info.entrypoint = entry;
    info.format = SDL_GPU_SHADERFORMAT_MSL;
  }
  info.stage = stage;
  info.num_samplers = samplers;
  info.num_uniform_buffers = 1;
  return SDL_CreateGPUShader(dev, &info);
}

}  // namespace

SdlRender3D::SdlRender3D(SDL_GPUDevice* device, SDL_Window* window) : dev_(device), window_(window) {
  color_format_ = SDL_GetGPUSwapchainTextureFormat(dev_, window_);
  vs_ = make_shader(dev_, "vs", SDL_GPU_SHADERSTAGE_VERTEX, 0);
  fs_tex_ = make_shader(dev_, "fs_tex", SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
  fs_flat_ = make_shader(dev_, "fs_flat", SDL_GPU_SHADERSTAGE_FRAGMENT, 0);
  ok_ = vs_ && fs_tex_ && fs_flat_;
  if (!ok_) std::fprintf(stderr, "SdlRender3D shaders: %s\n", SDL_GetError());
}

SdlRender3D::~SdlRender3D() {
  if (last_frame_) SDL_WaitForGPUFences(dev_, true, &last_frame_, 1), SDL_ReleaseGPUFence(dev_, last_frame_);
  for (auto& [k, p] : pipelines_) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
  for (auto& [k, s] : samplers_) SDL_ReleaseGPUSampler(dev_, s);
  for (SDL_GPUTexture* t : textures_)
    if (t) SDL_ReleaseGPUTexture(dev_, t);
  if (depth_) SDL_ReleaseGPUTexture(dev_, depth_);
  if (vbuf_) SDL_ReleaseGPUBuffer(dev_, vbuf_);
  for (Staging* s : {&tex_staging_, &vert_staging_})
    if (s->buf) SDL_ReleaseGPUTransferBuffer(dev_, s->buf);
  if (frame_) SDL_ReleaseGPUTexture(dev_, frame_);
  for (SDL_GPUShader* s : {vs_, fs_tex_, fs_flat_})
    if (s) SDL_ReleaseGPUShader(dev_, s);
}

SDL_GPUGraphicsPipeline* SdlRender3D::pipeline(const platform::DrawState& s, bool textured) {
  const uint32_t key = key_of(s, textured);
  auto it = pipelines_.find(key);
  if (it != pipelines_.end()) return it->second;
  SDL_GPUVertexBufferDescription vb{};
  vb.slot = 0, vb.pitch = sizeof(platform::TLVertex), vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  SDL_GPUVertexAttribute attrs[4] = {
      {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(platform::TLVertex, sx)},
      {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(platform::TLVertex, diffuse)},
      {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(platform::TLVertex, specular)},
      {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(platform::TLVertex, tu)}};
  SDL_GPUColorTargetDescription ct{};
  ct.format = color_format_;
  if (s.blend || s.multiply) {
    ct.blend_state.enable_blend = true;
    ct.blend_state.color_blend_op = ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    if (s.multiply) {  // SRCBLEND ZERO, DESTBLEND SRCCOLOR
      ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
      ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
    } else {           // SRCALPHA, INVSRCALPHA
      ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
      ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    }
    ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  }
  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vs_, info.fragment_shader = textured ? fs_tex_ : fs_flat_;
  info.vertex_input_state.vertex_buffer_descriptions = &vb;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attrs;
  info.vertex_input_state.num_vertex_attributes = 4;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;  // CULLMODE NONE: the game culls (0x0057a900)
  info.rasterizer_state.fill_mode = s.wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
  info.depth_stencil_state.enable_depth_test = true;
  info.depth_stencil_state.enable_depth_write = s.depth_write;
  info.depth_stencil_state.compare_op = s.depth_always ? SDL_GPU_COMPAREOP_ALWAYS : SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
  info.target_info.color_target_descriptions = &ct;
  info.target_info.num_color_targets = 1;
  info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  info.target_info.has_depth_stencil_target = true;
  SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(dev_, &info);
  if (!p) std::fprintf(stderr, "SdlRender3D pipeline: %s\n", SDL_GetError());
  pipelines_[key] = p;
  return p;
}

SDL_GPUSampler* SdlRender3D::sampler(uint8_t u, uint8_t v) {
  const uint32_t key = u << 8 | v;
  auto it = samplers_.find(key);
  if (it != samplers_.end()) return it->second;
  auto mode = [](uint8_t m) { return m == 3 ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE : SDL_GPU_SAMPLERADDRESSMODE_REPEAT; };
  SDL_GPUSamplerCreateInfo si{};
  si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;  // TEXTUREMAG/MIN LINEAR
  si.address_mode_u = mode(u), si.address_mode_v = mode(v), si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  SDL_GPUSampler* s = SDL_CreateGPUSampler(dev_, &si);
  samplers_[key] = s;
  return s;
}

int32_t SdlRender3D::create_texture(int width, int height, const uint8_t* rgba, uint8_t) {
  SDL_GPUTextureCreateInfo ti{};
  ti.type = SDL_GPU_TEXTURETYPE_2D, ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  ti.width = width, ti.height = height, ti.layer_count_or_depth = 1, ti.num_levels = 1;
  SDL_GPUTexture* t = SDL_CreateGPUTexture(dev_, &ti);
  if (!t) return -1;
  upload(t, width, height, rgba);
  textures_.push_back(t);
  return static_cast<int32_t>(textures_.size() - 1);
}

void SdlRender3D::update_texture(int32_t h, int width, int height, const uint8_t* rgba) {
  if (h < 0 || h >= static_cast<int32_t>(textures_.size()) || !textures_[h]) return;
  upload(textures_[h], width, height, rgba);
}

// A staging buffer of at least `bytes`, mapped. Kept between calls (made per
// call, they were a quarter of a frame; performance.md); mapping with cycle
// gives a fresh one while the GPU still reads the last.
void* SdlRender3D::map_staging(Staging& s, uint32_t bytes) {
  if (!s.buf || s.size < bytes) {
    if (s.buf) SDL_ReleaseGPUTransferBuffer(dev_, s.buf);
    s.size = bytes < 1u << 20 ? 1u << 20 : bytes;
    SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, s.size, 0};
    s.buf = SDL_CreateGPUTransferBuffer(dev_, &tbi);
  }
  return s.buf ? SDL_MapGPUTransferBuffer(dev_, s.buf, true) : nullptr;
}

void SdlRender3D::upload(SDL_GPUTexture* t, int width, int height, const uint8_t* rgba) {
  const auto size = static_cast<uint32_t>(width * height * 4);
  void* to = map_staging(tex_staging_, size);
  if (!to) return;
  std::memcpy(to, rgba, size);
  SDL_GPUTransferBuffer* tb = tex_staging_.buf;
  SDL_UnmapGPUTransferBuffer(dev_, tb);
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo src{tb, 0, 0, 0};
  SDL_GPUTextureRegion dst{};
  dst.texture = t, dst.w = width, dst.h = height, dst.d = 1;
  SDL_UploadToGPUTexture(copy, &src, &dst, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
}

void SdlRender3D::release_texture(int32_t h) {
  if (h < 0 || h >= static_cast<int32_t>(textures_.size()) || !textures_[h]) return;
  SDL_ReleaseGPUTexture(dev_, textures_[h]);
  textures_[h] = nullptr;
}

void SdlRender3D::set_viewport(int x, int y, int w, int h) { vx_ = x, vy_ = y, vw_ = w, vh_ = h; }

// FIX (A): the frame clear (0x005777f0) fills the back buffer once; here it
// is kept for the next scene's pass, colour and depth (depth to 0).
// The GPU driver SDL picked: "metal", "vulkan", "direct3d12".
const char* SdlRender3D::driver_name() const {
  static std::string name;
  const char* d = dev_ ? SDL_GetGPUDeviceDriver(dev_) : nullptr;
  name = std::string("SDL GPU (") + (d ? d : "none") + ")";
  return name.c_str();
}

void SdlRender3D::clear(bool color, uint8_t r, uint8_t g, uint8_t b) {
  clear_pending_ = true;
  clear_color_ = color;
  clear_rgb_[0] = r / 255.0f, clear_rgb_[1] = g / 255.0f, clear_rgb_[2] = b / 255.0f;
}

void SdlRender3D::set_fog_color(uint8_t r, uint8_t g, uint8_t b) {
  fog_rgb_[0] = r / 255.0f, fog_rgb_[1] = g / 255.0f, fog_rgb_[2] = b / 255.0f;
}

void SdlRender3D::begin_scene() {
  vertices_.clear();
  runs_.clear();
}

// Fans as (0, i, i+1), strips with alternating order, points as tiny quads.
void SdlRender3D::draw(platform::Primitive type, const platform::TLVertex* v, int count,
                       const platform::DrawState& state) {
  if (count < 1) return;
  const auto first = static_cast<uint32_t>(vertices_.size());
  if (type == platform::Primitive::kFan) {
    for (int i = 1; i + 1 < count; ++i) vertices_.insert(vertices_.end(), {v[0], v[i], v[i + 1]});
  } else if (type == platform::Primitive::kStrip) {
    for (int i = 0; i + 2 < count; ++i)
      if (i & 1) vertices_.insert(vertices_.end(), {v[i + 1], v[i], v[i + 2]});
      else vertices_.insert(vertices_.end(), {v[i], v[i + 1], v[i + 2]});
  } else {
    for (int i = 0; i < count; ++i) {
      platform::TLVertex a = v[i], b = v[i], c = v[i], d = v[i];
      b.sx += 1, c.sy += 1, d.sx += 1, d.sy += 1;
      vertices_.insert(vertices_.end(), {a, b, c, b, d, c});
    }
  }
  const auto n = static_cast<uint32_t>(vertices_.size()) - first;
  if (n == 0) return;
  const bool same_fog = !runs_.empty() && runs_.back().fog[0] == fog_rgb_[0] && runs_.back().fog[1] == fog_rgb_[1] &&
                        runs_.back().fog[2] == fog_rgb_[2];
  if (same_fog && same(runs_.back().state, state) && runs_.back().first + runs_.back().count == first)
    runs_.back().count += n;
  else
    runs_.push_back({state, first, n, {fog_rgb_[0], fog_rgb_[1], fog_rgb_[2]}});
}

void SdlRender3D::ensure_depth(uint32_t w, uint32_t h) {
  if (depth_ && depth_w_ == w && depth_h_ == h) return;
  if (depth_) SDL_ReleaseGPUTexture(dev_, depth_);
  SDL_GPUTextureCreateInfo ti{};
  ti.type = SDL_GPU_TEXTURETYPE_2D, ti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  ti.width = w, ti.height = h, ti.layer_count_or_depth = 1, ti.num_levels = 1;
  depth_ = SDL_CreateGPUTexture(dev_, &ti);
  depth_w_ = w, depth_h_ = h;
}

void SdlRender3D::ensure_buffer(uint32_t bytes) {
  if (vbuf_ && vbuf_size_ >= bytes) return;
  if (vbuf_) SDL_ReleaseGPUBuffer(dev_, vbuf_);
  vbuf_size_ = bytes < 1u << 20 ? 1u << 20 : bytes * 2;
  SDL_GPUBufferCreateInfo bi{SDL_GPU_BUFFERUSAGE_VERTEX, vbuf_size_, 0};
  vbuf_ = SDL_CreateGPUBuffer(dev_, &bi);
}

// The back buffer: one colour target the size of the window's pixels, kept
// across the scenes, 2D layers and quads of a frame (FIX (A): DirectDraw's
// back buffer), shown by present(). With any_resolution it is the frame's
// shape inside the window (FIX (B), sdl_fit.h), placed at fit_.
bool SdlRender3D::ensure_frame() {
  int pw = 0, ph = 0;
  SDL_GetWindowSizeInPixels(window_, &pw, &ph);
  if (pw <= 0 || ph <= 0) pw = 1280, ph = 960;
  fit_ = fit_rect(static_cast<float>(pw), static_cast<float>(ph), shape_ ? *shape_ : FrameShape{});
  const Uint32 w = fit_.w >= 1 ? static_cast<Uint32>(fit_.w) : 1, h = fit_.h >= 1 ? static_cast<Uint32>(fit_.h) : 1;
  if (frame_ && frame_w_ == w && frame_h_ == h) return true;
  if (frame_) SDL_ReleaseGPUTexture(dev_, frame_);
  SDL_GPUTextureCreateInfo ti{};
  ti.type = SDL_GPU_TEXTURETYPE_2D, ti.format = color_format_;
  ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  ti.width = w, ti.height = h, ti.layer_count_or_depth = 1, ti.num_levels = 1;
  frame_ = SDL_CreateGPUTexture(dev_, &ti);
  frame_w_ = w, frame_h_ = h;
  clear_pending_ = true;  // depth: a new back buffer's starts cleared
  frame_new_ = true;      // colour too, even with the colour clear off (end_scene)
  return frame_ != nullptr;
}

// One scene's draws into the back buffer, in order, in one render pass with
// reversed depth (GREATER_OR_EQUAL; cleared to 0 by a pending frame clear).
void SdlRender3D::end_scene() {
  if (std::getenv("MW3_R3D_STATS"))
    std::fprintf(stderr, "Render3D scene: %zu triangles in %zu runs, %zu textures, viewport %d %d %d %d\n",
                 vertices_.size() / 3, runs_.size(), textures_.size(), vx_, vy_, vw_, vh_);
  if (!ensure_frame()) return;
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
  const auto bytes = static_cast<uint32_t>(vertices_.size() * sizeof(platform::TLVertex));
  void* to = bytes ? map_staging(vert_staging_, bytes) : nullptr;
  if (to) {
    ensure_buffer(bytes);
    std::memcpy(to, vertices_.data(), bytes);
    SDL_GPUTransferBuffer* tb = vert_staging_.buf;
    SDL_UnmapGPUTransferBuffer(dev_, tb);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{tb, 0};
    SDL_GPUBufferRegion dst{vbuf_, 0, bytes};
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);  // cycled: an earlier pass may still read it
    SDL_EndGPUCopyPass(copy);
  }
  const Uint32 w = frame_w_, h = frame_h_;
  ensure_depth(w, h);
  SDL_GPUColorTargetInfo ct{};
  ct.texture = frame_;
  // A new back buffer starts black even with the colour clear off (a mission's): the pixels no
  // draw reaches would show undefined memory.
  const bool fill = clear_pending_ && clear_color_;
  if (fill) ct.clear_color = {clear_rgb_[0], clear_rgb_[1], clear_rgb_[2], 1};
  else ct.clear_color = {0, 0, 0, 1};
  ct.load_op = fill || frame_new_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  frame_new_ = false;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPUDepthStencilTargetInfo dt{};
  dt.texture = depth_, dt.clear_depth = 0;  // DEPTHFILL 0 (0x005777f0)
  dt.load_op = clear_pending_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  dt.store_op = SDL_GPU_STOREOP_STORE;
  clear_pending_ = false;
  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
  // The game's viewport (its window, e.g. 640x480) is stretched over the
  // whole target: the shader maps game pixels to NDC by the game's rect.
  SDL_GPUViewport vp{0, 0, static_cast<float>(w), static_cast<float>(h), 0, 1};
  SDL_SetGPUViewport(pass, &vp);
  const float vu[4] = {static_cast<float>(vx_), static_cast<float>(vy_), static_cast<float>(vw_), static_cast<float>(vh_)};
  for (const Run& r : runs_) {
    const bool textured = r.state.texture >= 0 && r.state.texture < static_cast<int32_t>(textures_.size()) &&
                          textures_[r.state.texture];
    SDL_GPUGraphicsPipeline* p = pipeline(r.state, textured);
    if (!p) continue;
    SDL_BindGPUGraphicsPipeline(pass, p);
    SDL_PushGPUVertexUniformData(cmd, 0, vu, sizeof vu);
    // A sharp draw's texel is a game pixel: the frame's pixels per game pixel.
    const float sx = r.state.sharp && vw_ > 0 ? static_cast<float>(w) / static_cast<float>(vw_) : 0.0f;
    const float sy = r.state.sharp && vh_ > 0 ? static_cast<float>(h) / static_cast<float>(vh_) : 0.0f;
    const float fu[8] = {r.fog[0], r.fog[1], r.fog[2], 1, r.state.fog ? 1.0f : 0.0f,
                         r.state.alpha_modulate ? 1.0f : 0.0f, std::max(sx, 1.0f) * (sx > 0), std::max(sy, 1.0f) * (sy > 0)};
    SDL_PushGPUFragmentUniformData(cmd, 0, fu, sizeof fu);
    SDL_GPUBufferBinding vb{vbuf_, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    if (textured) {
      SDL_GPUTextureSamplerBinding tsb{textures_[r.state.texture], sampler(r.state.address_u, r.state.address_v)};
      SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
    }
    SDL_DrawGPUPrimitives(pass, r.count, 1, r.first, 0);
  }
  SDL_EndGPURenderPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  vertices_.clear();
  runs_.clear();
}

// FIX (A): the back buffer downloaded (the fade's still, the capture). The
// swapchain's format is BGRA or RGBA; either comes back as 0xAARRGGBB.
bool SdlRender3D::read_back(std::vector<uint32_t>* argb, int* width, int* height) {
  if (!frame_) return false;
  const Uint32 w = frame_w_, h = frame_h_;
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
  if (!cmd) return false;
  SDL_GPUTransferBufferCreateInfo dbi{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, w * h * 4, 0};
  SDL_GPUTransferBuffer* db = SDL_CreateGPUTransferBuffer(dev_, &dbi);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureRegion src{};
  src.texture = frame_, src.w = w, src.h = h, src.d = 1;
  SDL_GPUTextureTransferInfo dst{db, 0, 0, 0};
  SDL_DownloadFromGPUTexture(copy, &src, &dst);
  SDL_EndGPUCopyPass(copy);
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  SDL_WaitForGPUFences(dev_, true, &fence, 1);
  SDL_ReleaseGPUFence(dev_, fence);
  const auto* p = static_cast<const uint32_t*>(SDL_MapGPUTransferBuffer(dev_, db, false));
  argb->assign(p, p + static_cast<size_t>(w) * h);
  SDL_UnmapGPUTransferBuffer(dev_, db);
  SDL_ReleaseGPUTransferBuffer(dev_, db);
  if (color_format_ == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM || color_format_ == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB)
    for (uint32_t& v : *argb) v = (v & 0xff00ff00u) | (v >> 16 & 0xffu) | (v & 0xffu) << 16;
  *width = static_cast<int>(w);
  *height = static_cast<int>(h);
  return true;
}

// Flip (0x0057a850): the back buffer onto the window's swapchain (vsync), or
// into the capture file. With the window hidden there is no swapchain to wait on.
// Ours (playtest W25): the back buffer copied on the GPU, at its size, and back.
namespace {
void copy_texture(SDL_GPUDevice* dev, SDL_GPUTexture* from, SDL_GPUTexture* to, Uint32 w, Uint32 h) {
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
  if (!cmd) return;
  SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureLocation src{}, dst{};
  src.texture = from, dst.texture = to;
  SDL_CopyGPUTextureToTexture(cp, &src, &dst, w, h, 1, false);
  SDL_EndGPUCopyPass(cp);
  SDL_SubmitGPUCommandBuffer(cmd);
}
}  // namespace
bool SdlRender3D::hold_frame() {
  if (!frame_) return false;
  if (!held_ || held_w_ != frame_w_ || held_h_ != frame_h_) {
    if (held_) SDL_ReleaseGPUTexture(dev_, held_);
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D, ti.format = color_format_;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = frame_w_, ti.height = frame_h_, ti.layer_count_or_depth = 1, ti.num_levels = 1;
    held_ = SDL_CreateGPUTexture(dev_, &ti);
    held_w_ = frame_w_, held_h_ = frame_h_;
  }
  if (!held_) return false;
  copy_texture(dev_, frame_, held_, frame_w_, frame_h_);
  return true;
}
bool SdlRender3D::restore_frame() {
  if (!held_ || !ensure_frame() || held_w_ != frame_w_ || held_h_ != frame_h_) return false;
  copy_texture(dev_, held_, frame_, frame_w_, frame_h_);
  clear_pending_ = false;  // the next pass draws over it
  frame_new_ = false;
  return true;
}
void SdlRender3D::drop_frame() {
  if (held_) SDL_ReleaseGPUTexture(dev_, held_);
  held_ = nullptr, held_w_ = held_h_ = 0;
}

void SdlRender3D::present() {
  if (!frame_) return;
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
  if (capture_path_) {
    SDL_SubmitGPUCommandBuffer(cmd);
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    if (read_back(&px, &w, &h)) {
      // The window as shown: the back buffer at its place, black round it.
      int pw = 0, ph = 0;
      SDL_GetWindowSizeInPixels(window_, &pw, &ph);
      if (pw > 0 && ph > 0 && (pw != w || ph != h) && fit_.w == w && fit_.h == h) {
        std::vector<uint32_t> all(static_cast<size_t>(pw) * ph, 0xff000000u);
        const int ox = static_cast<int>(fit_.x), oy = static_cast<int>(fit_.y);
        for (int y = 0; y < h && oy + y < ph; ++y)
          std::copy(px.begin() + static_cast<size_t>(y) * w, px.begin() + static_cast<size_t>(y) * w + std::min(w, pw - ox),
                    all.begin() + static_cast<size_t>(oy + y) * pw + ox);
        px.swap(all), w = pw, h = ph;
      }
      SDL_Surface* sf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4);
      SDL_SaveBMP(sf, capture_path_);
      SDL_DestroySurface(sf);
    }
    capture_path_ = nullptr;
    return;
  }
  SDL_GPUTexture* sc = nullptr;
  Uint32 w = 0, h = 0;
  const bool hidden = (SDL_GetWindowFlags(window_) & SDL_WINDOW_HIDDEN) != 0;
  if (!hidden && SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &sc, &w, &h) && sc) {
    SDL_GPUBlitInfo bi{};
    bi.source.texture = frame_, bi.source.w = frame_w_, bi.source.h = frame_h_;
    bi.destination.texture = sc, bi.destination.w = w, bi.destination.h = h;
    bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
    // With the frame's shape kept: into its rect (of the swapchain's size), the bars cleared black.
    const FitRect r = fit_rect(static_cast<float>(w), static_cast<float>(h), shape_ ? *shape_ : FrameShape{});
    if (r.x > 0 || r.y > 0 || r.w < w || r.h < h) {
      bi.destination.x = static_cast<Uint32>(r.x), bi.destination.y = static_cast<Uint32>(r.y);
      bi.destination.w = static_cast<Uint32>(r.w), bi.destination.h = static_cast<Uint32>(r.h);
      bi.load_op = SDL_GPU_LOADOP_CLEAR, bi.clear_color = {0, 0, 0, 1};
    }
    bi.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(cmd, &bi);
    SDL_SubmitGPUCommandBuffer(cmd);
    mark_ready();  // a frame on the screen: the launcher may hide (sdl_child.h)
    return;
  }
  // No swapchain image (a hidden window, or none to be had): nothing paces
  // the frames, so at most one stays in flight.
  SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (last_frame_) SDL_WaitForGPUFences(dev_, true, &last_frame_, 1), SDL_ReleaseGPUFence(dev_, last_frame_);
  last_frame_ = f;
}

}  // namespace sdlfe
