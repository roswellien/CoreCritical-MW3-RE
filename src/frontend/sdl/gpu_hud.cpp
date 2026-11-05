#include "frontend/sdl/gpu_hud.h"

#include <algorithm>
#include <cfenv>
#include <cstdio>
#include <stdexcept>

#include "frontend/sdl/sdl_render3d.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/ztwod/twod_host.h"

namespace sdlfe {
namespace {
class FloatScope {
 public:
  FloatScope() {
    std::feholdexcept(&saved_);
    std::fesetround(FE_TONEAREST);
  }
  ~FloatScope() { std::fesetenv(&saved_); }

 private:
  std::fenv_t saved_{};
};
uint32_t rgba(uint16_t colour, unsigned alpha = 255) {
  return (((colour >> 11) * 255 / 31) * alpha + 127) / 255 |
         (((((colour >> 5) & 63) * 255 / 63) * alpha + 127) / 255) << 8 |
         ((((colour & 31) * 255 / 31) * alpha + 127) / 255) << 16 | alpha << 24;
}
void fold(uint64_t& hash, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    hash ^= uint8_t(value >> (8 * i));
    hash *= 0x100000001b3ULL;
  }
}
}  // namespace
GpuHud::GpuHud(FrameRenderer& renderer, const HudOptions& options)
    : renderer_(renderer), options_(options) {
  if (options.mode != "core-critical")
    throw std::runtime_error("GPU HUD requires core-critical mode");
  if (!options.artwork.empty()) artwork_ = platform::load_hud_artwork(options.artwork);
}
GpuHud::~GpuHud() {
  cancel();
  for (const auto& item : resources_) renderer_.release_texture(item.second.texture);
  std::fprintf(stderr,
               "GPU HUD: %llu layers, %llu images, %llu artwork hits, %llu text hits, %llu points, "
               "%llu spans; native CPU shadow retained\n",
               static_cast<unsigned long long>(layers_), static_cast<unsigned long long>(images_),
               static_cast<unsigned long long>(hits_), static_cast<unsigned long long>(text_hits_),
               static_cast<unsigned long long>(points_), static_cast<unsigned long long>(spans_));
  std::fprintf(stderr, "GPU HUD compatibility composition: %llu layers\n",
               static_cast<unsigned long long>(shadow_layers_));
  for (const auto& [reason, count] : shadow_reasons_)
    std::fprintf(stderr, "GPU HUD compatibility: %s %llu\n", reason.c_str(),
                 static_cast<unsigned long long>(count));
}
void GpuHud::begin(const uint16_t* target, int width, int height, int pitch, int output_height) {
  FloatScope scope;
  if (zrndr::draw_observer()) throw std::runtime_error("Nested GPU HUD target");
  if (!target || width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
      pitch < int64_t(width) * 2)
    throw std::runtime_error("Invalid GPU HUD target");
  target_ = target;
  width_ = width;
  height_ = height;
  pitch_ = pitch;
  shadow_layer_ = false;
  density_ = std::max(1.f, float(output_height) / height);
  vertices_.clear();
  commands_.clear();
  ++frame_;
  while (resources_.size() > 512 || resource_bytes_ > 256 * 1024 * 1024) {
    auto oldest = std::min_element(
        resources_.begin(), resources_.end(),
        [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
    renderer_.release_texture(oldest->second.texture);
    resource_bytes_ -= oldest->second.image->pixels.size() * sizeof(uint32_t);
    resources_.erase(oldest);
  }
  uint64_t source_bytes = 0;
  for (const auto& source : sources_)
    source_bytes += source.second.image->pixels.size() * sizeof(uint32_t);
  if (sources_.size() > 512 || source_bytes > 128 * 1024 * 1024) sources_.clear();
  zrndr::set_draw_observer(this);
}
void GpuHud::cancel() {
  if (zrndr::draw_observer() == this) zrndr::set_draw_observer(nullptr);
  target_ = nullptr;
  vertices_.clear();
  commands_.clear();
}
bool GpuHud::finish() {
  if (!target_) return false;
  FloatScope scope;
  zrndr::set_draw_observer(nullptr);
  target_ = nullptr;
  if (shadow_layer_) {
    ++shadow_layers_;
    vertices_.clear();
    commands_.clear();
    return false;
  }
  if (commands_.empty()) return false;
  int x, y, width, height;
  renderer_.flush();
  renderer_.viewport(&x, &y, &width, &height);
  renderer_.set_viewport(0, 0, width_, height_);
  renderer_.begin_scene();
  for (const auto& command : commands_) {
    platform::DrawState state;
    state.texture = command.texture;
    state.blend = true;
    state.depth_always = true;
    state.depth_write = false;
    state.sharp = command.sharp;
    state.address_u = state.address_v = 3;
    renderer_.draw(command.primitive, vertices_.data() + command.first,
                   static_cast<int>(command.count), state);
  }
  renderer_.end_scene();
  renderer_.set_viewport(x, y, width, height);
  ++layers_;
  vertices_.clear();
  commands_.clear();
  return true;
}
void GpuHud::framebuffer_write(const void* target, const char* operation) {
  int x, y;
  if (shadow_layer_ || !position(static_cast<const uint16_t*>(target), x, y)) return;
  shadow_layer_ = true;
  const auto count = ++shadow_reasons_[operation];
  if (count == 1)
    std::fprintf(stderr, "GPU HUD: using native shadow composition for %s\n", operation);
  vertices_.clear();
  commands_.clear();
}
int GpuHud::texture(const std::shared_ptr<const platform::HudImage>& image) {
  auto found = resources_.find(image.get());
  if (found != resources_.end()) {
    found->second.used = frame_;
    return found->second.texture;
  }
  const int handle =
      renderer_.create_texture(static_cast<int>(image->width), static_cast<int>(image->height),
                               reinterpret_cast<const uint8_t*>(image->pixels.data()), 1);
  if (handle < 0) throw std::runtime_error("GPU HUD texture allocation failed");
  resources_.emplace(image.get(), Resource{image, handle, frame_});
  resource_bytes_ += image->pixels.size() * sizeof(uint32_t);
  return handle;
}
void GpuHud::append(platform::TLVertex vertex, int image, platform::Primitive primitive,
                    bool sharp) {
  commands_.push_back({image, static_cast<unsigned>(vertices_.size()), 0, primitive, sharp});
  vertices_.push_back(vertex);
  ++commands_.back().count;
}
void GpuHud::rectangle(float x0, float y0, float x1, float y1, uint32_t colour, int image, float u0,
                       float v0, float u1, float v1) {
  append({x0, y0, 0, 1, colour, 0, u0, v0}, image, platform::Primitive::kFan, image >= 0);
  // One fan must retain its four vertices; subsequent rectangles are separate.
  vertices_.push_back({x1, y0, 0, 1, colour, 0, u1, v0});
  vertices_.push_back({x1, y1, 0, 1, colour, 0, u1, v1});
  vertices_.push_back({x0, y1, 0, 1, colour, 0, u0, v1});
  commands_.back().count = 4;
}
bool GpuHud::position(const uint16_t* pointer, int& x, int& y) const {
  if (!target_) return false;
  const auto address = reinterpret_cast<uintptr_t>(pointer),
             base = reinterpret_cast<uintptr_t>(target_);
  if (address < base || address - base >= uint64_t(pitch_) * height_ || ((address - base) & 1))
    return false;
  const auto offset = address - base;
  y = int(offset / pitch_);
  x = int((offset % pitch_) / 2);
  return x < width_;
}
void GpuHud::pixel(const uint16_t* destination, uint16_t colour) {
  int x, y;
  if (shadow_layer_ || !position(destination, x, y)) return;
  if (zrndr::target2d().format != 6) {
    framebuffer_write(target_, "RGB555 target");
    return;
  }
  if (colour == 0xf81f) {
    framebuffer_write(target_, "layer erase");
    return;
  }
  FloatScope scope;
  const uint32_t packed = rgba(colour);
  const uint32_t argb = (packed & 0xff00ff00) | (packed & 255) << 16 | (packed >> 16 & 255);
  // Each native pixel covers one logical pixel, including after noninteger
  // scaling. Hardware points cover one physical pixel and leave dotted lines.
  const platform::TLVertex upper_left{float(x), float(y), 0, 1, argb, 0, 0, 0};
  if (!commands_.empty() && commands_.back().primitive == platform::Primitive::kStrip) {
    const auto last = vertices_.back();
    vertices_.push_back(last);
    vertices_.push_back(upper_left);
    vertices_.push_back(upper_left);
    commands_.back().count += 3;
  } else {
    append(upper_left, -1, platform::Primitive::kStrip, false);
  }
  vertices_.push_back({float(x + 1), float(y), 0, 1, argb, 0, 0, 0});
  vertices_.push_back({float(x), float(y + 1), 0, 1, argb, 0, 0, 0});
  vertices_.push_back({float(x + 1), float(y + 1), 0, 1, argb, 0, 0, 0});
  commands_.back().count += 3;
  ++points_;
}
void GpuHud::span(const uint16_t* destination, uint32_t count, uint16_t colour) {
  int x, y;
  if (shadow_layer_ || !position(destination, x, y) || !count) return;
  if (zrndr::target2d().format != 6) {
    framebuffer_write(target_, "RGB555 target");
    return;
  }
  if (count > unsigned(width_ - x)) throw std::runtime_error("GPU HUD span crosses target bounds");
  if (colour == 0xf81f) {
    framebuffer_write(target_, "layer erase");
    return;
  }
  FloatScope scope;
  const uint32_t packed = rgba(colour);
  const uint32_t argb = (packed & 0xff00ff00) | (packed & 255) << 16 | (packed >> 16 & 255);
  rectangle(float(x), float(y), float(x) + count, float(y + 1), argb);
  ++spans_;
}
void GpuHud::image(const zimg::Texture* source, int32_t x, int32_t y, uint16_t key,
                   const zrndr::BlitRect* crop) {
  if (shadow_layer_ || !target_ ||
      zrndr::target2d().base != reinterpret_cast<const uint8_t*>(target_))
    return;
  if (zrndr::target2d().format != 6) {
    framebuffer_write(target_, "RGB555 target");
    return;
  }
  if (!source || !source->pixels || source->width <= 0 || source->height <= 0 ||
      source->width > 2048 || source->height > 2048 || source->width_again < source->width ||
      (source->palette && source->palette_count <= 0))
    throw std::runtime_error("Unsupported native GPU HUD image");
  int64_t left = crop ? crop->left : 0, top = crop ? crop->top : 0;
  int64_t right = crop ? crop->right : source->width, bottom = crop ? crop->bottom : source->height;
  int64_t destination_x = x, destination_y = y;
  destination_x += std::max<int64_t>(0, -left);
  destination_y += std::max<int64_t>(0, -top);
  left = std::max<int64_t>(0, left);
  top = std::max<int64_t>(0, top);
  right = std::min<int64_t>(source->width, right);
  bottom = std::min<int64_t>(source->height, bottom);
  if (destination_x < 0) left -= destination_x, destination_x = 0;
  if (destination_y < 0) top -= destination_y, destination_y = 0;
  right = std::min(right, left + width_ - destination_x);
  bottom = std::min(bottom, top + height_ - destination_y);
  if (right <= left || bottom <= top) return;
  FloatScope scope;
  std::vector<uint16_t> normalized(size_t(source->width) * source->height);
  uint64_t hash = 0xcbf29ce484222325ULL;
  fold(hash, source->width);
  fold(hash, source->height);
  auto image = std::make_shared<platform::HudImage>();
  image->width = source->width;
  image->height = source->height;
  image->pixels.resize(normalized.size());
  for (int row = 0; row < source->height; ++row)
    for (int column = 0; column < source->width; ++column) {
      const size_t at = size_t(row) * source->width_again + column,
                   into = size_t(row) * source->width + column;
      unsigned index = 0;
      if (source->palette) index = static_cast<const uint8_t*>(source->pixels)[at];
      if (source->palette && index >= unsigned(source->palette_count))
        throw std::runtime_error("GPU HUD palette index is invalid");
      const uint16_t pixel = source->palette ? source->palette[index]
                                             : static_cast<const uint16_t*>(source->pixels)[at];
      unsigned alpha = source->alpha ? source->alpha[at] : 255;
      if (!source->alpha && (source->flags & zimg::kAlpha) &&
          (source->palette ? index : pixel) == key)
        alpha = 0;
      normalized[into] = alpha ? pixel : 0;
      fold(hash, uint32_t(pixel) | alpha << 16);
      if (alpha <= 3)
        alpha = 0;
      else if (alpha >= 252)
        alpha = 255;
      if (pixel == 0xf81f && alpha == 255) {
        framebuffer_write(target_, "unkeyed layer erase");
        return;
      }
      image->pixels[into] = rgba(pixel, alpha);
    }
  std::shared_ptr<const platform::HudImage> selected;
  const auto enhanced =
      options_.text ? ztwod::host().hud_text_image(source, density_, key) : nullptr;
  if (enhanced) {
    selected = std::move(enhanced);
    ++text_hits_;
  } else if (auto found =
                 artwork_.find(platform::hud_identity(source->width, source->height, normalized));
             found != artwork_.end()) {
    selected = found->second;
    ++hits_;
  } else {
    auto cached = sources_.find(source);
    if (cached != sources_.end() && cached->second.hash == hash)
      selected = cached->second.image;
    else {
      selected = std::move(image);
      sources_[source] = {hash, selected};
    }
  }
  const float du = (selected->u1 - selected->u0) / source->width,
              dv = (selected->v1 - selected->v0) / source->height;
  rectangle(float(destination_x), float(destination_y), float(destination_x + right - left),
            float(destination_y + bottom - top), 0xffffffffu, texture(selected),
            selected->u0 + left * du, selected->v0 + top * dv, selected->u0 + right * du,
            selected->v0 + bottom * dv);
  ++images_;
}
}  // namespace sdlfe
