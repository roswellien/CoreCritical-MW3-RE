#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gamez/ztwod/twod_host.h"
#include "platform/prepared_hud.h"

typedef struct TTF_Font TTF_Font;

namespace mech3 { class GameEngine; }
namespace platform { class Window; }

// ztwod::Host on SDL3 (docs/twod.md section 6, docs/program.md): the GDI text
// calls drawn with SDL3_ttf over the game's own arial.ttf, impact.ttf and
// lucon.ttf (found through the mounts), into the same 16-bit images; the
// zVideo, zSound, message and mouse calls on the program's engine. FIX (A):
// GDI does not exist on macOS. Text is drawn without antialiasing, as GDI
// drew it on a 16-bit DIB, so the key-colour blit keeps clean edges.
namespace sdlfe {

class SdlInput;
using zgame::Handle;

class SdlTwodHost : public ztwod::Host {
 public:
  // `input` and `window` give the mouse; both may be null (tests).
  SdlTwodHost(mech3::GameEngine* engine, SdlInput* input, platform::Window* window);
  ~SdlTwodHost() override;
  void enable_hud_text(bool enabled) { hud_text_enabled_ = enabled; }
  std::shared_ptr<const platform::HudImage> hud_text_image(const zimg::Texture*, float, uint16_t) override;


  // zImage: a refcounted cache over zimg::fetch_image (0x005179d0 / 0x00517b10).
  zimg::Texture* image_get(const char* name) override;
  zimg::Texture* image_release(zimg::Texture* image) override;
  void image_path(const char* path) override;
  // fonts.zrd's WINDOWS_FONTS by name (zimg_winfonts.cpp 0x00517280..0x00517680).
  Handle font_find(const char* name) override;
  ztwod::NamedFont font_fields(Handle font) override;
  void font_addref(Handle font) override;
  void font_release(Handle font) override;

  void video_lock() override;
  void video_unlock() override;
  zimg::Texture* screen_grab(int32_t buffer) override;
  void texture_free(zimg::Texture* t) override;
  zimg::Texture* held_grab_ = nullptr;  // the message box's backdrop over a kept frame (W25)
  zimg::Texture* image_new(int32_t w, int32_t h) override;
  zimg::Texture* screen_grab_rect(int32_t buffer, ztwod::Rect* r, zimg::Texture* into) override;
  Handle image_dc(zimg::Texture* image) override;
  void image_dc_release(zimg::Texture* image, Handle dc) override;
  int32_t video_mode() override;
  void video_double() override {}
  uint32_t show_cursor(uint32_t on) override;
  ztwod::Rect screen_rect() override;
  Handle render_target(int32_t* w, int32_t* h, int32_t* depth, int32_t* pitch) override;
  void render_set_target(Handle, const ztwod::Rect*, Handle, Handle) override {}
  Handle video_get(uint32_t addr) override { return addr; }
  void input_poll(uint8_t a) override;  // FIX (A): the window's events pumped first (the message box's loop)
  void timer_update() override;
  float frame_dt() override;
  void video_flip(const ztwod::Rect* a, const ztwod::Rect* b, int32_t c, int32_t d) override;
  void ripple(int32_t x, int32_t y, int32_t inner, int32_t outer, int32_t amplitude, float wavelength, float phase,
              const ztwod::Rect* clip) override;

  int32_t sound_find(const char* name) override;
  int32_t sound_play(int32_t sound, float volume) override;
  void sound_stop(int32_t handle) override;
  int32_t cd_find(const char* name) override;
  int32_t cd_play(int32_t track, int32_t mode) override;
  void cd_stop() override;
  const char* message(const char* key) override;
  const char* message_text(uint32_t id) override;
  // 0x00575ff0: FormatMessageA on Mech3Msg.dll's text, the arguments C strings.
  uint32_t format_message(char* buf, uint32_t size, uint32_t id, va_list args) override;
  ztwod::MouseState mouse() override;
  void input_reset() override;

  Handle GetStockObject(int32_t which) override;
  Handle CreateFontA(int32_t height, int32_t width, int32_t escapement, int32_t orientation, int32_t weight,
                     uint32_t italic, uint32_t underline, uint32_t strike, uint32_t charset,
                     uint32_t out_precision, uint32_t clip_precision, uint32_t quality, uint32_t pitch_family,
                     const char* face) override;
  int32_t GetObjectA(Handle font) override;
  Handle CreateFontIndirectA(Handle copied_from) override;
  void DeleteObject(Handle object) override;
  Handle CreateCompatibleDC(Handle dc) override;
  Handle SelectObject(Handle dc, Handle object) override;
  void DeleteDC(Handle dc) override;
  int32_t GetTextExtentPoint32A(Handle dc, const char* text, int32_t length, int32_t* cx, int32_t* cy) override;
  int32_t DrawTextA(Handle dc, const char* text, int32_t length, ztwod::Rect* rect, uint32_t format) override;
  int32_t GetTextMetricsA(Handle dc, ztwod::TextMetrics* out) override;
  void SetTextColor(Handle dc, uint32_t color) override;
  void SetBkColor(Handle dc, uint32_t color) override;
  void SetBkMode(Handle dc, int32_t mode) override;
  uint32_t GetLastError() override { return 0; }

  // Which .ttf a GDI face name uses ("Lucida Console" is lucon.ttf).
  static std::string font_file(const std::string& face);

 private:
  struct Font {
    std::string face;
    int32_t height = 0, weight = 400;
    uint32_t italic = 0;
    TTF_Font* ttf = nullptr;
    int em = 0;  // pixels
    int ascent = 0, descent = 0;  // GDI's tmAscent and tmDescent (gdi_font_size.h)
    int shift = 0;                // GDI's ascent less SDL_ttf's: the baseline's move down
  };
  struct Dc {
    Handle font = 0;
    uint32_t text = 0, bk = 0xffffff;
    int32_t bk_mode = 2;  // OPAQUE, GDI's default
    zimg::Texture* image = nullptr;
  };
  struct HudTextLine {
    std::string text, face;
    int em{}, weight{}, italic{}, x{}, y{}, baseline{}, width{}, height{}, background_mode{};
    ztwod::Rect clip{};
    uint32_t colour{}, background{};
  };
  struct HudTextSource {
    uint64_t pixels_hash{}, cached_signature{};
    int cached_scale{};
    std::vector<uint16_t> base;
    std::vector<HudTextLine> lines;
    std::shared_ptr<const platform::HudImage> cached;
  };
  bool hud_text_enabled_{};
  std::map<const zimg::Texture*, HudTextSource> hud_text_sources_;
  std::map<std::string, TTF_Font*> hud_fonts_;
  struct Named {
    ztwod::NamedFont fields;
    int refs = 0;
  };
  Font* font(Handle h);
  Dc* dc(Handle h);
  TTF_Font* open_ttf(Font& f);
  std::vector<std::string> lines(const Font& f, const std::string& utf8, int wrap_width, bool wrap);
  bool load_named_fonts();

  mech3::GameEngine* engine_;
  SdlInput* input_;
  platform::Window* window_;
  Handle next_ = 0x00300000;
  std::map<Handle, Font> fonts_;
  std::map<Handle, Dc> dcs_;
  std::map<std::string, Handle> named_by_name_;
  std::map<Handle, Named> named_;
  bool named_loaded_ = false;
  struct Cached {
    zimg::Texture* image;
    int refs;
  };
  std::map<std::string, Cached> images_;
  std::set<std::string> missing_;  // the names already reported
  std::set<zimg::Texture*> freed_;   // freed through image_release, until the address is reused
  void free_image(zimg::Texture* image);
  zimg::Texture* made(zimg::Texture* image);
  uint32_t buttons_ = 0;
  bool ttf_ok_ = false;
};

}  // namespace sdlfe
