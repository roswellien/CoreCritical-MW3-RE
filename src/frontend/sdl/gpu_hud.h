#pragma once
#include <map>
#include <string>
#include <vector>

#include "gamez/zrender/zrndr_blit.h"
#include "platform/prepared_hud.h"
#include "platform/render3d.h"

namespace sdlfe {
class FrameRenderer;
struct HudOptions {
  std::string mode = "original";
  std::string artwork;
  bool text = true;
};
// An instance-owned HUD draw list. The original canvas remains a CPU shadow
// for native reads; display uses owned GPU images and ordered primitives.
class GpuHud final : public zrndr::DrawObserver {
 public:
  GpuHud(FrameRenderer& renderer, const HudOptions& options);
  ~GpuHud() override;
  void begin(const uint16_t* target, int width, int height, int pitch, int output_height);
  bool finish();
  void cancel();
  void image(const zimg::Texture*, int32_t x, int32_t y, uint16_t key,
             const zrndr::BlitRect*) override;
  void pixel(const uint16_t* destination, uint16_t colour) override;
  void span(const uint16_t* destination, uint32_t count, uint16_t colour) override;
  void framebuffer_write(const void* target, const char* operation) override;

 private:
  struct Resource {
    std::shared_ptr<const platform::HudImage> image;
    int texture;
    uint64_t used;
  };
  struct Source {
    uint64_t hash;
    std::shared_ptr<const platform::HudImage> image;
  };
  struct Command {
    int texture;
    unsigned first, count;
    platform::Primitive primitive;
    bool sharp;
  };
  bool position(const uint16_t* pointer, int& x, int& y) const;
  int texture(const std::shared_ptr<const platform::HudImage>& image);
  void rectangle(float x0, float y0, float x1, float y1, uint32_t colour, int texture = -1,
                 float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);
  void append(platform::TLVertex vertex, int texture, platform::Primitive primitive, bool sharp);
  FrameRenderer& renderer_;
  HudOptions options_;
  platform::HudArtwork artwork_;
  std::map<const platform::HudImage*, Resource> resources_;
  std::map<const zimg::Texture*, Source> sources_;
  std::vector<platform::TLVertex> vertices_;
  std::vector<Command> commands_;
  const uint16_t* target_{};
  int width_{}, height_{}, pitch_{};
  float density_{1};
  uint64_t frame_{}, layers_{}, images_{}, hits_{}, text_hits_{}, points_{}, spans_{};
  uint64_t resource_bytes_{};
  bool shadow_layer_{};
  uint64_t shadow_layers_{};
  std::map<std::string, uint64_t> shadow_reasons_;
};
}  // namespace sdlfe
