#pragma once
#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

#include "frontend/sdl/sdl_fit.h"
#include "platform/render3d.h"

// platform::Render3D on SDL3's GPU API (docs/render.md section 4): the
// original's D3DTLVERTEX draws collected over a scene, fans and strips turned
// into triangle lists, then drawn in order in one render pass with reversed
// depth (cleared to 0, GREATER_OR_EQUAL). The game's viewport rect is stretched
// over the whole back buffer, which is the window, or with any_resolution the
// frame's shape inside it (sdl_fit.h, black bars round it). Shaders are MSL for now (Metal);
// SPIR-V and DXIL come with the Windows build.
namespace platform::prepared {
struct EnvironmentSelection;
}

namespace sdlfe {

enum class RendererBackend { sdl, core_critical_dxgi, core_critical_wsi };

struct CoreCriticalQuality {
  std::string antialiasing = "off";
  std::string anisotropy = "1";
};

struct PreparedModelBinding {
  std::string chassis, lod, mesh, textures;
};
struct PreparedNormalBinding {
  std::string chassis, lod, path;
};
struct PreparedMaterialBinding {
  std::string chassis, lod, path;
};
struct CoreCriticalAssets {
  std::vector<std::string> environment_packs;
  std::shared_ptr<const platform::prepared::EnvironmentSelection> environment;
  bool render_thread = false;
  std::string geometry = "legacy";
  bool geometry_debug_normals = false;
  std::string textures;
  std::vector<PreparedModelBinding> models;
  std::vector<PreparedNormalBinding> normals;
  std::vector<PreparedMaterialBinding> materials;
  bool sun_explicit = false;
  std::string lighting = "off", sun_azimuth = "45", sun_elevation = "35";
  bool empty() const {
    return environment_packs.empty() && !environment && !sun_explicit &&
           !render_thread && geometry == "legacy" && !geometry_debug_normals &&
           textures.empty() && models.empty() && normals.empty() &&
           materials.empty() && lighting == "off" && sun_azimuth == "45" &&
           sun_elevation == "35";
  }
};

class FrameRenderer : public platform::Render3D {
 public:
  virtual bool ok() const = 0;
  virtual void capture_next(const char* bmp_path) = 0;
  virtual void flush() = 0;
  virtual void viewport(int* x, int* y, int* w, int* h) const = 0;
  virtual void set_shape(const FrameShape* shape) = 0;
};

class SdlRender3D : public FrameRenderer {
 public:
  SdlRender3D(SDL_GPUDevice* device, SDL_Window* window);
  ~SdlRender3D() override;
  bool ok() const override { return ok_; }
  const char* driver_name() const override;

  int32_t create_texture(int width, int height, const uint8_t* rgba, uint8_t type) override;
  void release_texture(int32_t handle) override;
  void update_texture(int32_t handle, int width, int height, const uint8_t* rgba) override;
  void set_viewport(int x, int y, int width, int height) override;
  void clear(bool color, uint8_t r, uint8_t g, uint8_t b) override;
  void begin_scene() override;
  void end_scene() override;
  void draw(platform::Primitive type, const platform::TLVertex* vertices, int count,
            const platform::DrawState& state) override;
  void set_fog_color(uint8_t r, uint8_t g, uint8_t b) override;
  void present() override;
  bool read_back(std::vector<uint32_t>* argb, int* width, int* height) override;
  bool hold_frame() override;
  bool restore_frame() override;
  void drop_frame() override;

  // Saves the next presented frame as a BMP instead of showing it.
  void capture_next(const char* bmp_path) override { capture_path_ = bmp_path; }
  // Draws what is collected so far (outside a scene), so a 2D layer goes on top.
  void flush() override {
    if (!runs_.empty()) end_scene();
  }
  void viewport(int* x, int* y, int* w, int* h) const override { *x = vx_, *y = vy_, *w = vw_, *h = vh_; }
  void set_shape(const FrameShape* s) override { shape_ = s; }

 private:
  void upload(SDL_GPUTexture* t, int width, int height, const uint8_t* rgba);
  struct Run {
    platform::DrawState state;
    uint32_t first, count;
    // FIX (A): FOGCOLOR as it was at these draws, as Direct3D applies it per
    // draw (a saturated light's colour on its polygons only; W20).
    float fog[3];
  };
  SDL_GPUGraphicsPipeline* pipeline(const platform::DrawState& s, bool textured);
  SDL_GPUSampler* sampler(uint8_t u, uint8_t v);
  void ensure_depth(uint32_t w, uint32_t h);
  void ensure_buffer(uint32_t bytes);
  bool ensure_frame();

  SDL_GPUDevice* dev_;
  SDL_Window* window_;
  bool ok_ = false;
  SDL_GPUTextureFormat color_format_ = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  SDL_GPUShader* vs_ = nullptr;
  SDL_GPUShader* fs_tex_ = nullptr;
  SDL_GPUShader* fs_flat_ = nullptr;
  std::map<uint32_t, SDL_GPUGraphicsPipeline*> pipelines_;
  std::map<uint32_t, SDL_GPUSampler*> samplers_;
  std::vector<SDL_GPUTexture*> textures_;
  SDL_GPUTexture* depth_ = nullptr;
  uint32_t depth_w_ = 0, depth_h_ = 0;
  SDL_GPUBuffer* vbuf_ = nullptr;
  uint32_t vbuf_size_ = 0;
  // The upload staging buffers, kept and cycled (performance.md): textures', vertices'.
  struct Staging {
    SDL_GPUTransferBuffer* buf = nullptr;
    uint32_t size = 0;
  };
  Staging tex_staging_, vert_staging_;
  void* map_staging(Staging& s, uint32_t bytes);  // mapped, cycled when the GPU may still read it
  std::vector<platform::TLVertex> vertices_;
  std::vector<Run> runs_;
  int vx_ = 0, vy_ = 0, vw_ = 640, vh_ = 480;
  bool clear_color_ = true;
  bool clear_pending_ = true;
  bool frame_new_ = true;  // frame_ was just made: nothing has defined its pixels
  SDL_GPUTexture* frame_ = nullptr;  // the back buffer
  SDL_GPUTexture* held_ = nullptr;   // a shown frame kept (hold_frame), the back buffer's size
  // A hidden window's last frame (no swapchain to wait on): waited for before
  // the next is submitted, so a slow GPU (software Vulkan) cannot fall behind
  // by many frames, each holding cycled staging buffers (docs/linux.md).
  SDL_GPUFence* last_frame_ = nullptr;
  Uint32 held_w_ = 0, held_h_ = 0;
  Uint32 frame_w_ = 0, frame_h_ = 0;
  float clear_rgb_[3] = {0, 0, 0};
  float fog_rgb_[3] = {0, 0, 0};
  const char* capture_path_ = nullptr;
  const FrameShape* shape_ = nullptr;  // the frame's shape in the window (null: the whole window)
  FitRect fit_;                        // where the back buffer goes in the window's pixels
};

}  // namespace sdlfe
