#pragma once
#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <vector>

#include "frontend/sdl/sdl_fit.h"
#include "frontend/sdl/sdl_render3d.h"
#include "frontend/sdl/gpu_hud.h"
#include "platform/platform.h"

namespace zgame { class App; }

// The SDL3 program's Windows stand-ins (docs/program.md). FIX (A) each: the
// window and its GPU device, the message pump zGameApp::Run drives, the clock,
// the message boxes, and the 16-bit menu frame shown through the GPU.
namespace sdlfe {

class SdlInput;
class SdlMusicOutput;

// The main window ("Mech3Class", CWnd::CreateEx in 0x00482d10) with the GPU
// device behind Render3D. Made hidden at start so Render3D exists before the
// game creates its window; create() sizes, titles and keeps it hidden until
// UpdateWindow shows it.
class SdlWindow : public platform::Window {
 public:
  // The window and GPU device; false with SDL's error printed. `adopt`: the
  // launcher's window, taken instead of a new one (ours, docs/launcher.md).
  bool init(bool hidden_for_good, SDL_Window* adopt = nullptr,
            RendererBackend backend = RendererBackend::sdl, const CoreCriticalQuality& quality = {},
            const CoreCriticalAssets& assets = {});
  ~SdlWindow() override;
  uint32_t create(const std::string& title, int width, int height) override;
  void set_title(const std::string& title) override;
  void show() override;
  void client_size(int* width, int* height) override;
  void frame_size(int width, int height, int canvas_w = 0, int canvas_h = 0) override;
  // --window not given: the window takes the frame's shape (sdl_fit.h).
  void set_auto_size(bool on) { auto_size_ = on; }
  void set_fullscreen(bool on) { fullscreen_ = on; }  // ours: borderless, the desktop's size
  // Ours (Alt+Enter): fullscreen or a window now, for this run only; the
  // window takes the frame's shape at the size it would open with, kept
  // inside the display's usable area.
  void switch_fullscreen(bool on);
  bool fullscreen() const { return fullscreen_; }
  const FrameShape& shape() const { return shape_; }
  // A play run (not a check or a shot): a first start takes the desktop's size.
  void set_first_start_desktop(bool on) { first_start_desktop_ = on; }
  bool first_start_size(int* width, int* height) override;

  SDL_Window* sdl() const { return window_; }
  SDL_GPUDevice* device() const { return device_; }
  FrameRenderer* render() const { return render_.get(); }
  static constexpr uint32_t kHwnd = 0x00020000;  // the HWND handle the game keeps

 private:
  SDL_Window* window_ = nullptr;
  SDL_GPUDevice* device_ = nullptr;
  std::unique_ptr<FrameRenderer> render_;
  bool hidden_ = false;
  bool auto_size_ = false;
  bool fullscreen_ = false;
  bool first_start_desktop_ = false;
  void inside_usable(int* w, int* h) const;  // shrunk into the display's usable area
  int windowed_w_ = 1280, windowed_h_ = 960;  // the size create() asked for
  FrameShape shape_;
};

// The DirectDraw flip of the 16-bit back buffer (0x00576ea0): the pixels as a
// texture drawn over the whole window by Render3D.
class SdlScreen2D : public platform::Screen2D {
 public:
  explicit SdlScreen2D(FrameRenderer* render, const HudOptions& hud = {}, SDL_Window* window = nullptr,
                       const FrameShape* shape = nullptr);
  ~SdlScreen2D() override;
  void begin_overlay_16(const uint16_t*, int width, int height, int pitch) override;
  void cancel_overlay_16() override;
  void present_16(const uint16_t* pixels, int width, int height, int pitch, bool rgb565) override;
  // A 16-bit pixel (565, else 555) as the layer's RGBA8, opaque.
  static uint32_t rgba(uint16_t pixel, bool rgb565);
  // Every pixel of the layer still `key` (pitch in bytes): nothing to overlay.
  static bool layer_empty(const uint16_t* pixels, int width, int height, int pitch, uint16_t key);
  void overlay_16(const uint16_t* pixels, int width, int height, int pitch, bool rgb565, uint16_t key,
                  const uint8_t* alpha, const uint16_t* colour) override;
  int frames = 0;  // frames shown

 private:
  void upload(const uint16_t* pixels, int width, int height, int pitch, bool rgb565, bool keyed, uint16_t key,
              const uint8_t* alpha = nullptr, const uint16_t* colour = nullptr);
  void draw_layer(int width, int height, bool blend);
  FrameRenderer* render_;
  std::unique_ptr<GpuHud> hud_;
  SDL_Window* hud_window_{};
  const FrameShape* hud_shape_{};
  int32_t texture_ = -1;
  int tw_ = 0, th_ = 0;
  std::vector<uint8_t> rgba_;
};

// GetTickCount from the game's start, and Sleep. With `frame_ms` (ours,
// --frame-ms): each pass of the app's loop is that long and a sleep only
// adds its time, so a run's timing is the same however fast it draws.
class SdlClock : public platform::Clock {
 public:
  SdlClock() : start_(SDL_GetTicks()) {}
  uint32_t ticks_ms() override {
    if (frame_ms) return static_cast<uint32_t>(frames_ * frame_ms + slept_);
    return static_cast<uint32_t>(SDL_GetTicks() - start_);
  }
  void sleep(uint32_t ms) override {
    if (frame_ms) slept_ += ms;
    else SDL_Delay(ms);
  }
  void frame() { ++frames_; }  // a pass of the app's loop

  uint32_t frame_ms = 0;

 private:
  Uint64 start_;
  uint64_t frames_ = 0, slept_ = 0;
};

// MessageBoxA: SDL's message box on the window.
class SdlAlert : public platform::Alert {
 public:
  explicit SdlAlert(SDL_Window* parent, bool headless) : parent_(parent), headless_(headless) {}
  void abort_message(const std::string& message) override;
  int32_t message_box(const std::string& text, const std::string& caption, uint32_t style) override;

 private:
  SDL_Window* parent_;
  bool headless_;  // log only (screenshots, scripted runs)
};

// The Windows message queue (zGameApp::Run, docs/game-loop.md): SDL's event
// queue. Input events go to the input layer, focus changes to the frame's
// OnActivate (0x00590c00), a finished music file becomes MM_MCINOTIFY
// (0x00590740), the window's close WM_CLOSE -> PostQuitMessage.
class SdlPump : public platform::MessagePump {
 public:
  SdlPump(SdlInput* input, zgame::App* app) : input_(input), app_(app) {}
  bool peek() override;
  bool pump() override;
  void wait(uint32_t ms) override;
  void post_quit() override;
  void raise_priority() override;

  // Asked before each look at the queue: true posts the quit (mw3 --frames).
  std::function<bool()> should_quit;
  // Ours: run at each look at the queue that finds no message, once a pass
  // of the app's loop that runs the game (--frame-ms steps its clock there).
  std::function<void()> each_pass;
  static Uint32 music_event();  // the user event SdlMusicOutput posts at a file's end
  // Ours: the window gained (true) or lost the focus (the sound mutes).
  std::function<void(bool)> focus_changed;
  // Ours: a run that ends by itself (--frames, --shot, a chain) stays active
  // when its window loses focus; the original's pause on deactivation
  // (0x00590c00) is for play. A check started behind another app, or on a
  // window the launcher handed over unfocused, idled for good otherwise.
  bool stays_active = false;
  // Ours: Alt+Enter, swallowed before the game sees the Enter.
  std::function<void()> toggle_fullscreen;

  void handle(const SDL_Event& e);  // public for the tests

 private:
  SdlInput* input_;
  zgame::App* app_;
  bool quit_ = false;
  bool enter_swallowed_ = false;  // Alt+Enter's Enter is down: its release goes too
};

}  // namespace sdlfe
