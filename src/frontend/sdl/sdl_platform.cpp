#include "frontend/sdl/sdl_platform.h"

#ifdef CC_MW3_RE_ADAPTER
#include "frontend/core_critical/frame_renderer.hpp"
#include <exception>
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <algorithm>

#include "frontend/sdl/sdl_input.h"
#include "gamez/zgame/zgameapp.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/zfix.h"

namespace sdlfe {

bool SdlWindow::init(bool hidden_for_good, SDL_Window* adopt, RendererBackend backend,
                     const CoreCriticalQuality& quality, const CoreCriticalAssets& assets) {
  if (backend == RendererBackend::sdl &&
      (quality.antialiasing != "off" || quality.anisotropy != "1" || !assets.empty()))
    return std::fprintf(stderr, "mw3: Core Critical quality requires a Core Critical Vulkan backend\n"), false;
  hidden_ = hidden_for_good;
  if (backend != RendererBackend::sdl) {
#ifdef CC_MW3_RE_ADAPTER
    if (adopt) return std::fprintf(stderr, "mw3: Core Critical Vulkan requires its own window\n"), false;
    auto flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
    if (backend == RendererBackend::core_critical_wsi) flags |= SDL_WINDOW_VULKAN;
    window_ = SDL_CreateWindow("MechWarrior 3", 1280, 960, flags);
    if (!window_) return std::fprintf(stderr, "mw3: SDL_CreateWindow: %s\n", SDL_GetError()), false;
    try {
      render_ = cc_mw3_re::make_renderer(window_, backend, quality, assets);
      render_->set_shape(&shape_);
      return render_->ok();
    } catch (const std::exception& error) {
      return std::fprintf(stderr, "mw3: Core Critical Vulkan setup: %s\n", error.what()), false;
    }
#else
    return std::fprintf(stderr, "mw3: this build has no Core Critical Vulkan adapter\n"), false;
#endif
  }
  // Metal, Direct3D 12 or Vulkan (SPIR-V only where the build has it).
  SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL;
#if defined(MW3_HAVE_SPIRV)
  formats |= SDL_GPU_SHADERFORMAT_SPIRV;
#endif
  device_ = SDL_CreateGPUDevice(formats, false, nullptr);
  if (!device_) return std::fprintf(stderr, "mw3: SDL_CreateGPUDevice: %s\n", SDL_GetError()), false;
  window_ = adopt ? adopt : SDL_CreateWindow("MechWarrior 3", 1280, 960, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
  if (adopt) std::printf("mw3: the game took the launcher's window\n");
  if (!window_) return std::fprintf(stderr, "mw3: SDL_CreateWindow: %s\n", SDL_GetError()), false;
  if (!SDL_ClaimWindowForGPUDevice(device_, window_))
    return std::fprintf(stderr, "mw3: SDL_ClaimWindowForGPUDevice: %s\n", SDL_GetError()), false;
  // Diagnostic only: compare backend throughput without the default vblank cap.
  const char* immediate = SDL_getenv("MW3_BENCHMARK_IMMEDIATE");
  if (immediate && std::strcmp(immediate, "1") == 0) {
    if (!SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE) ||
        !SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      SDL_GPU_PRESENTMODE_IMMEDIATE)) {
      std::fprintf(stderr, "mw3 benchmark: immediate presentation unavailable: %s\n", SDL_GetError());
      return false;
    }
    std::printf("mw3 benchmark: backend=%s adapter=%s present=immediate\n",
                SDL_GetGPUDeviceDriver(device_),
                SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device_), SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"));
  }
  render_ = std::make_unique<SdlRender3D>(device_, window_);
  render_->set_shape(&shape_);
  return render_->ok();
}

SdlWindow::~SdlWindow() {
  render_.reset();
  if (device_ && window_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
  if (window_) SDL_DestroyWindow(window_);
  if (device_) SDL_DestroyGPUDevice(device_);
}

uint32_t SdlWindow::create(const std::string& title, int width, int height) {
  if (!window_) return 0;
  if (render_) render_->drain();
  windowed_w_ = width, windowed_h_ = height;
  if (fullscreen_) return SDL_SetWindowTitle(window_, title.empty() ? "MechWarrior 3" : title.c_str()), kHwnd;
  SDL_SetWindowSize(window_, width, height);
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_SyncWindow(window_);  // X11 and Wayland apply a size later: the next frame's is this one (docs/linux.md)
  if (!title.empty()) SDL_SetWindowTitle(window_, title.c_str());
  return kHwnd;
}
void SdlWindow::set_title(const std::string& title) {
  if (window_) SDL_SetWindowTitle(window_, title.c_str());
}
void SdlWindow::show() {
  if (!window_ || hidden_) return;
  if (render_) render_->drain();
  if (fullscreen_) SDL_SetWindowFullscreen(window_, true), SDL_SyncWindow(window_);
  // Brought forward and given the keyboard (the Mac's app activated, as SDL
  // does by default; Windows' foreground taken even from another process),
  // so the game is not left waiting for a click (the launcher's window too).
  SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
  SDL_ShowWindow(window_), SDL_RaiseWindow(window_);
}
// The display rect the game draws: with any_resolution its shape is kept
// (pillarboxed in a window of another shape), and a window opened without
// --window takes it (sdl_fit.h default_window).
void SdlWindow::frame_size(int width, int height, int canvas_w, int canvas_h) {
  if (render_) render_->drain();
  shape_.frame_w = width, shape_.frame_h = height;
  shape_.canvas_w = canvas_w, shape_.canvas_h = canvas_h;
  shape_.fit = zfix::options().any_resolution;
  if (!window_ || !shape_.fit || !auto_size_ || fullscreen_) return;
  int w = 0, h = 0;
  default_window(shape_, &w, &h);
  if (!hidden_) inside_usable(&w, &h);  // a desktop-sized frame (a first start) in a window
  SDL_SetWindowSize(window_, w, h);
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_SyncWindow(window_);
}
void SdlWindow::inside_usable(int* w, int* h) const {
  SDL_Rect usable{};
  if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_), &usable) && usable.w > 0 && usable.h > 0) {
    const float k = std::fmin(1.0f, std::fmin(usable.w * 0.9f / *w, usable.h * 0.9f / *h));  // room for the title bar
    *w = static_cast<int>(*w * k), *h = static_cast<int>(*h * k);
  }
}
// FIX (B): a first start (no InGameVMode saved) plays at the desktop's size,
// in points as the frame is (resolution.md, the first start).
bool SdlWindow::first_start_size(int* w, int* h) {
  if (!window_ || !first_start_desktop_) return false;
  SDL_DisplayID d = SDL_GetDisplayForWindow(window_);
  const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(d ? d : SDL_GetPrimaryDisplay());
  if (!m || m->w <= 0 || m->h <= 0) return false;
  *w = m->w, *h = m->h;
  return true;
}
void SdlWindow::switch_fullscreen(bool on) {
  if (!window_ || hidden_ || on == fullscreen_) return;
  if (render_) render_->drain();
  fullscreen_ = on;
  if (on) {
    SDL_SetWindowFullscreen(window_, true);
  } else {
    SDL_SetWindowFullscreen(window_, false);
    int w = windowed_w_, h = windowed_h_;
    if (shape_.fit && auto_size_) default_window(shape_, &w, &h);
    inside_usable(&w, &h);
    SDL_SetWindowSize(window_, w, h);
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
  SDL_SyncWindow(window_);
}
void SdlWindow::client_size(int* width, int* height) {
  *width = *height = 0;
  if (window_) SDL_GetWindowSize(window_, width, height);
}

namespace {
uint32_t rgba_of(uint16_t p, bool rgb565) {
  const int r = rgb565 ? p >> 11 : (p >> 10) & 31, g = rgb565 ? (p >> 5) & 63 : ((p >> 5) & 31) << 1, b = p & 31;
  return static_cast<uint32_t>(r << 3 | r >> 2) | static_cast<uint32_t>(g << 2 | g >> 4) << 8 |
         static_cast<uint32_t>(b << 3 | b >> 2) << 16 | 0xff000000u;
}
// rgba_of for every 16-bit value, made once a format (performance.md).
const uint32_t* rgba_table(bool rgb565) {
  static const std::vector<uint32_t> t565 = [] {
    std::vector<uint32_t> t(65536);
    for (uint32_t p = 0; p < 65536; ++p) t[p] = rgba_of(static_cast<uint16_t>(p), true);
    return t;
  }();
  static const std::vector<uint32_t> t555 = [] {
    std::vector<uint32_t> t(65536);
    for (uint32_t p = 0; p < 65536; ++p) t[p] = rgba_of(static_cast<uint16_t>(p), false);
    return t;
  }();
  return rgb565 ? t565.data() : t555.data();
}
// The layer is premultiplied (its sharp scaling blends with the key's clear texels).
uint32_t premultiplied(uint32_t rgba, uint32_t a) {
  uint32_t out = a << 24;
  for (int c = 0; c < 24; c += 8) out |= ((rgba >> c & 0xffu) * a + 127) / 255 << c;
  return out;
}
}  // namespace

uint32_t SdlScreen2D::rgba(uint16_t pixel, bool rgb565) { return rgba_table(rgb565)[pixel]; }

// The 16-bit pixels as RGBA8 into the layer's texture; `key` pixels (when
// keyed) transparent, blended ones (`alpha`, zrndr::LayerCover) see-through.
void SdlScreen2D::upload(const uint16_t* pixels, int width, int height, int pitch, bool rgb565, bool keyed,
                         uint16_t key, const uint8_t* alpha, const uint16_t* colour) {
  rgba_.resize(static_cast<size_t>(width) * height * 4);
  const uint32_t* table = rgba_table(rgb565);
  for (int y = 0; y < height; ++y) {
    const uint16_t* row = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(pixels) + y * pitch);
    auto* o = reinterpret_cast<uint32_t*>(&rgba_[static_cast<size_t>(y) * width * 4]);
    if (keyed)
      for (int x = 0; x < width; ++x) o[x] = row[x] == key ? 0u : table[row[x]];
    else
      for (int x = 0; x < width; ++x) o[x] = table[row[x]];
    if (alpha) {
      const size_t at = static_cast<size_t>(y) * (pitch / 2);
      for (int x = 0; x < width; ++x)
        if (alpha[at + x] && colour[at + x] == row[x]) o[x] = premultiplied(o[x], alpha[at + x]);
    }
  }
  if (texture_ < 0 || tw_ != width || th_ != height) {
    if (texture_ >= 0) render_->release_texture(texture_);
    texture_ = render_->create_texture(width, height, rgba_.data(), 1);
    tw_ = width, th_ = height;
  } else {
    render_->update_texture(texture_, width, height, rgba_.data());
  }
}

bool SdlScreen2D::layer_empty(const uint16_t* pixels, int width, int height, int pitch, uint16_t key) {
  uint64_t k4 = key;
  k4 |= k4 << 16, k4 |= k4 << 32;
  for (int y = 0; y < height; ++y) {
    const uint16_t* row = pixels + static_cast<size_t>(y) * (pitch / 2);
    int x = 0;
    for (; x + 4 <= width; x += 4) {  // four pixels a step
      uint64_t v;
      std::memcpy(&v, row + x, 8);
      if (v != k4) return false;
    }
    for (; x < width; ++x)
      if (row[x] != key) return false;
  }
  return true;
}

void SdlScreen2D::draw_layer(int width, int height, bool blend) {
  platform::DrawState s;
  s.texture = texture_, s.address_u = s.address_v = 3;
  s.depth_write = false, s.depth_always = true, s.blend = blend, s.sharp = true;
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  const platform::TLVertex quad[4] = {{0, 0, 1, 1, 0xffffffffu, 0xff000000u, 0, 0},
                                      {w, 0, 1, 1, 0xffffffffu, 0xff000000u, 1, 0},
                                      {w, h, 1, 1, 0xffffffffu, 0xff000000u, 1, 1},
                                      {0, h, 1, 1, 0xffffffffu, 0xff000000u, 0, 1}};
  render_->begin_scene();
  render_->draw(platform::Primitive::kFan, quad, 4, s);
  render_->end_scene();
}

void SdlScreen2D::present_16(const uint16_t* pixels, int width, int height, int pitch, bool rgb565) {
  if (!render_ || width <= 0 || height <= 0) return;
  ++frames;
  upload(pixels, width, height, pitch, rgb565, false, 0);
  // FIX (B), playtest W22: the 3D's viewport back after the layer. On a canvas
  // (16:9 past mode 10) the menus' 853x480 left on it made the mission after
  // the pause draw the frame's top-left corner over the whole window.
  int vx, vy, vw, vh;
  render_->viewport(&vx, &vy, &vw, &vh);
  render_->set_viewport(0, 0, width, height);
  render_->clear(true, 0, 0, 0);
  draw_layer(width, height, false);
  render_->present();
  render_->set_viewport(vx, vy, vw, vh);
}

SdlScreen2D::SdlScreen2D(FrameRenderer* render, const HudOptions& options, SDL_Window* window, const FrameShape* shape)
    : render_(render), hud_window_(window), hud_shape_(shape) {
  if (options.mode != "original" && options.mode != "core-critical")
    throw std::runtime_error("--cc-hud supports original or core-critical");
  if (options.mode == "core-critical") {
    if (!render_) throw std::runtime_error("Core Critical HUD requires a renderer");
    hud_ = std::make_unique<GpuHud>(*render_, options);
  }
}
SdlScreen2D::~SdlScreen2D() = default;
void SdlScreen2D::begin_overlay_16(const uint16_t* target, int width, int height, int pitch) {
  if (!hud_) return;
  int output_height = height;
  if (hud_window_) {
    int output_width = 0;
    SDL_GetWindowSizeInPixels(hud_window_, &output_width, &output_height);
    if (hud_shape_ && hud_shape_->fit && hud_shape_->frame_w > 0)
      output_height = std::min(output_height, int(int64_t(output_width) * hud_shape_->frame_h / hud_shape_->frame_w));
  }
  hud_->begin(target, width, height, pitch, output_height);
}
void SdlScreen2D::cancel_overlay_16() { if (hud_) hud_->cancel(); }
void SdlScreen2D::overlay_16(const uint16_t* pixels, int width, int height, int pitch, bool rgb565, uint16_t key,
                             const uint8_t* alpha, const uint16_t* colour) {
  if (!render_ || width <= 0 || height <= 0) return;
  if (hud_ && hud_->finish()) return;
  render_->flush();
  // Nothing drawn (every pixel still the key): nothing to lay over the frame.
  // A frame's locks often draw nothing (performance.md); the conversion,
  // upload and draw of the whole layer are skipped.
  if (layer_empty(pixels, width, height, pitch, key)) return;
  upload(pixels, width, height, pitch, rgb565, true, key, alpha, colour);
  int vx, vy, vw, vh;
  render_->viewport(&vx, &vy, &vw, &vh);
  render_->set_viewport(0, 0, width, height);
  draw_layer(width, height, true);
  render_->set_viewport(vx, vy, vw, vh);
}

void SdlAlert::abort_message(const std::string& message) {
  std::fprintf(stderr, "mw3: %s\n", message.c_str());
  if (!headless_) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MechWarrior 3", message.c_str(), parent_);
}

int32_t SdlAlert::message_box(const std::string& text, const std::string& caption, uint32_t style) {
  std::fprintf(stderr, "mw3: [%s] %s\n", caption.c_str(), text.c_str());
  if (headless_) return 1;
  const bool ok_cancel = (style & 0xf) == 1;
  SDL_MessageBoxButtonData buttons[2] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK"},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Cancel"}};
  SDL_MessageBoxData box{};
  box.flags = (style & 0xf0) == 0x10 ? SDL_MESSAGEBOX_ERROR : (style & 0xf0) == 0x30 ? SDL_MESSAGEBOX_WARNING
                                                                                     : SDL_MESSAGEBOX_INFORMATION;
  box.window = parent_;
  box.title = caption.c_str();
  box.message = text.c_str();
  box.numbuttons = ok_cancel ? 2 : 1;
  box.buttons = buttons;
  int hit = 1;
  if (!SDL_ShowMessageBox(&box, &hit)) return 1;
  return hit == 2 ? 2 : 1;  // IDOK / IDCANCEL
}

Uint32 SdlPump::music_event() {
  static const Uint32 e = SDL_RegisterEvents(1);
  return e;
}

bool SdlPump::peek() {
  // Only a pass that runs the game (no message waiting: the loop's idle
  // branch) is one of --frame-ms's steps. A pass that only takes a window
  // or SDL event counted too once, and as those come with the host's timing,
  // the game's clock (and a seeded run) differed with the machine's load.
  SDL_PumpEvents();
  const bool waiting = !quit_ && SDL_PeepEvents(nullptr, 0, SDL_PEEKEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) > 0;
  if (each_pass && !waiting) each_pass();
  if (!quit_ && should_quit && should_quit()) post_quit();
  if (quit_) return true;
  return waiting;
}

bool SdlPump::pump() {
  SDL_Event e;
  if (!SDL_PollEvent(&e)) return !quit_;  // WM_QUIT is the last message
  handle(e);
  return !(quit_ && e.type == SDL_EVENT_QUIT);
}

void SdlPump::handle(const SDL_Event& e) {
  // Ours: Alt+Enter toggles fullscreen and a window. Neither key is the
  // game's then: the Enter would press a menu's default button.
  if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) &&
      (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER) &&
      (enter_swallowed_ || (e.type == SDL_EVENT_KEY_DOWN && (e.key.mod & SDL_KMOD_ALT)))) {
    enter_swallowed_ = e.type == SDL_EVENT_KEY_DOWN;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && toggle_fullscreen) toggle_fullscreen();
    return;
  }
  if (input_) input_->on_event(e);
  switch (e.type) {
    case SDL_EVENT_QUIT:
      quit_ = true;
      break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:  // WM_CLOSE: the frame is destroyed, PostQuitMessage
      post_quit();
      break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      if (focus_changed) focus_changed(true);
      if (app_ && !app_->active()) app_->on_window_activate(1);  // WA_ACTIVE
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      if (focus_changed) focus_changed(false);
      if (app_ && app_->active() && !stays_active) app_->on_window_activate(0);
      break;
    default:
      if (e.type == music_event() && app_) {
        const zsnd_cd::State& s = zsnd_cd::state();
        if (s.track + 1 < s.end_track && !zsnd_cd::track_path(s.track + 1).empty())
          zsnd_cd::on_output_finished();  // a CD plays on into the next track
        else
          app_->message(1, 0);  // MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL (0x00590740)
      }
      break;
  }
}

void SdlPump::wait(uint32_t ms) { SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(ms)); }

void SdlPump::post_quit() {
  if (quit_) return;
  quit_ = true;
  SDL_Event q{};
  q.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&q);
}

void SdlPump::raise_priority() { SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH); }

}  // namespace sdlfe
