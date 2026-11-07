#pragma once
#include <cstdarg>
#include <cstdint>
#include <memory>

#include "gamez/zgame/unported.h"

namespace zimg { struct Texture; }
namespace platform { struct HudImage; }

// What zTwoD (docs/twod.md) calls outside itself: zImage's image cache and the
// named fonts of fonts.zrd, zVideo's 2D blit and screen grabs, zSound, the
// message table, zInput's mouse, and the GDI text calls. One virtual per
// original function or import, its address or name the key; each reports to
// unported() and returns 0 until the module behind it is ported.
//
// FIX (A): the GDI calls (CreateFontA, DrawTextA, GetTextExtentPoint32A, ...)
// stand for a platform text rasteriser that draws with the shipped .ttf fonts
// into the same 16-bit image; DirectDraw's Blt and the locked-surface blits
// behind 0x0055b1f0 become a platform 2D blit.
namespace ztwod {

using zgame::Arg;
using zgame::Handle;

struct Rect {                      // a Win32 RECT
  int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// zInput's mouse (0x0063d1d8, returned by 0x0051b7b0), 15 dwords. Button
// states: 1 pressed this frame, 2 held, 4 released this frame (0x0051b6e0).
struct MouseState {
  int32_t x, y, z;                 // +0x00
  float dx, dy, dz;                // +0x0c
  int32_t raw[3];                  // +0x18
  float fx, fy, fz;                // +0x24
  uint32_t left, right, middle;    // +0x30 (0x0063d208..)
};
static_assert(sizeof(MouseState) == 0x3c, "the window copies 15 dwords");

// The fields of a fonts.zrd font (zImage, 0x00517680) the text objects read.
struct NamedFont {
  Handle hfont = 0;                // +0x44
  uint32_t color_top = 0;          // +0x48  COLORREF
  uint32_t color_bottom = 0;       // +0x4c
  int32_t align = 0;               // +0x50  0 left, 1 centre, 2 right
  uint8_t shadow = 0;              // +0x54
};

// GetTextMetricsA's fields the gradient uses.
struct TextMetrics {
  int32_t height = 0, ascent = 0, internal_leading = 0, external_leading = 0;
};

class Host : public zgame::Unported {
 public:
  // Optional output-density text artwork. An empty result uses the native image.
  virtual std::shared_ptr<const platform::HudImage> hud_text_image(const zimg::Texture*, float, uint16_t) { return {}; }
  // zImage.
  virtual zimg::Texture* image_get(const char* name);                 // 0x005179d0: cached, by name
  virtual zimg::Texture* image_release(zimg::Texture* image);         // 0x00517b10: returns 0
  virtual void image_path(const char* path);                          // 0x00516f50
  virtual Handle font_find(const char* name);                         // 0x00517680: + a reference
  virtual NamedFont font_fields(Handle font);                         // its fields
  virtual void font_addref(Handle font);                              // 0x005172e0
  virtual void font_release(Handle font);                             // 0x00517300
  virtual void texture_free(zimg::Texture* t);                        // 0x00517cc0
  // zFont, the bitmap fonts of fonts.zrd (TextBase): ECX text, EDX font.
  virtual void font_measure(const char* text, Handle font, int32_t* w, int32_t* h);  // 0x0051a0d0
  virtual void font_draw(const char* text, int32_t x, int32_t y, Handle font);       // 0x005ae170
  // zVideo.
  virtual void video_lock();                                          // 0x00576de0
  virtual void video_unlock();                                        // 0x00576e70
  virtual zimg::Texture* screen_grab(int32_t buffer);                 // 0x00579500: ECX buffer
  virtual zimg::Texture* image_new(int32_t w, int32_t h) { (void)w, (void)h; return nullptr; }  // ours: a blank 16-bit image
  // 0x00579660: the screen rectangle into `into` (a new image when null); 0 when off screen.
  virtual zimg::Texture* screen_grab_rect(int32_t buffer, Rect* r, zimg::Texture* into);
  // 0x0055b1f0: ECX image, EDX x; y, the colour key, the source rectangle
  // (null: all). Ported: zrndr::blit16 into zrndr::target2d().
  virtual void blit(zimg::Texture* image, int32_t x, int32_t y, int32_t flags, const Rect* src);
  // [0x0072f394] / [0x0072f398]: a DC on the text image's pixels.
  virtual Handle image_dc(zimg::Texture* image);
  virtual void image_dc_release(zimg::Texture* image, Handle dc);
  // zVideo and zRender for the message box's own loop (0x005a7280).
  // 0x00575ff0: Mech3Msg.dll's message `id` formatted into buf (FormatMessageA,
  // the trailing CR LF cut); its length, 0 when none.
  virtual uint32_t format_message(char* buf, uint32_t size, uint32_t id, va_list args);
  virtual int32_t video_mode();                                       // [0x0072f348]: 0 software, 1 Direct3D
  virtual void video_double();                                        // [0x0072f358](0, 0)
  virtual uint32_t show_cursor(uint32_t on);                          // 0x00579840: ECX; returns the old state
  virtual Rect screen_rect();                                         // 0x00579880
  virtual Handle render_target(int32_t* w, int32_t* h, int32_t* depth, int32_t* pitch);  // 0x00558080
  virtual void render_set_target(Handle base, const Rect* r, Handle depth, Handle pitch);  // 0x00557f90
  virtual Handle video_get(uint32_t addr);                            // 0x00576db0 / c70 / d80 / cd0 / ca0
  virtual void input_poll(uint8_t a);                                 // 0x0051b360
  virtual void timer_update();                                        // 0x00575a00
  virtual float frame_dt();                                           // [0x0072c52c]
  virtual void video_flip(const Rect* a, const Rect* b, int32_t c, int32_t d);  // 0x00576ea0
  // zRender's ripple (0x00558dd0): ECX x, EDX y, then the radii, amplitude,
  // wavelength, phase and an optional clip rectangle (docs/mainmenu.md).
  virtual void ripple(int32_t x, int32_t y, int32_t inner, int32_t outer, int32_t amplitude, float wavelength,
                      float phase, const Rect* clip);
  virtual void crt_free(void* p);                                     // MSVCRT free
  // zSound.
  virtual int32_t sound_find(const char* name);                       // 0x0056f6f0: ECX name
  virtual int32_t sound_play(int32_t sound, float volume);            // 0x0056e3d0: ECX sound
  virtual void sound_stop(int32_t handle);                            // 0x0056e9f0: ECX handle
  virtual int32_t cd_find(const char* name);                          // 0x00572a30: ECX name
  virtual int32_t cd_play(int32_t track, int32_t mode);               // 0x00572af0: ECX, EDX
  virtual void cd_stop();                                             // 0x00572c30
  // zUtil's messages, zInput.
  virtual const char* message(const char* key);                       // 0x00575fb0: ECX key
  virtual const char* message_text(uint32_t id);                      // 0x00576040: ECX id
  virtual MouseState mouse();                                         // 0x0051b7b0
  virtual void input_reset();                                         // 0x0051b1c0
  // 0x0051a9a0 (ECX, EDX): zInput's text callback, null to clear. Ported:
  // zin::set_char_callback.
  virtual void set_char_callback(void (*cb)(int32_t ch, int32_t key, uintptr_t ctx), uintptr_t ctx);
  // zRender's 2D line (0x00562940: ECX x0, EDX y0; x1, y1, the pixel) into the
  // locked buffer. Ported: zrndr::line16 into zrndr::target2d().
  virtual void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t pixel);
  // zRender's other 2D primitives (zrndr_prim2d, the same target): a dashed
  // line (0x00562c80), lines joining count + 1 points each clipped
  // (0x00562cb0), a circle (0x005630c0), one with static (0x00563440), a line
  // with static (0x00562970), a filled polygon with its winding ([0x0074a3c8],
  // 0x0055d010; vertices x, y every 12 bytes).
  virtual void pixel(int32_t x, int32_t y, uint32_t pixel);  // 0x005630a0
  virtual void line_dashed(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t pixel, int32_t dash);
  virtual void line_list(const int32_t* points, int32_t count, const Rect* clip, uint32_t pixel);
  virtual void circle(int32_t cx, int32_t cy, int32_t r, uint32_t pixel, const Rect* clip);
  virtual void circle_noisy(int32_t cx, int32_t cy, int32_t r, uint32_t pixel, float noise, const Rect* clip);
  virtual void line_noisy(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t pixel, float noise,
                          const Rect* clip);
  virtual void polygon(const float* vertices, int32_t count, uint32_t color, int32_t winding);
  // 0x0053ce20: the render's clip volume, min and max x, y, z (the camera
  // window's sides, its near and far planes; [0x007402fc..0x00740310]).
  virtual void render_clip(float min[3], float max[3]);

  // GDI32 / USER32 (FIX (A), above), argument for argument.
  virtual Handle GetStockObject(int32_t which);
  virtual Handle CreateFontA(int32_t height, int32_t width, int32_t escapement, int32_t orientation,
                             int32_t weight, uint32_t italic, uint32_t underline, uint32_t strike,
                             uint32_t charset, uint32_t out_precision, uint32_t clip_precision,
                             uint32_t quality, uint32_t pitch_family, const char* face);
  // GetObjectA(font, sizeof LOGFONT, &lf) then CreateFontIndirectA(&lf): a copy.
  virtual int32_t GetObjectA(Handle font);
  virtual Handle CreateFontIndirectA(Handle copied_from);
  virtual void DeleteObject(Handle object);
  virtual Handle CreateCompatibleDC(Handle dc);
  virtual Handle SelectObject(Handle dc, Handle object);
  virtual void DeleteDC(Handle dc);
  virtual int32_t GetTextExtentPoint32A(Handle dc, const char* text, int32_t length, int32_t* cx,
                                        int32_t* cy);
  virtual int32_t DrawTextA(Handle dc, const char* text, int32_t length, Rect* rect, uint32_t format);
  virtual int32_t GetTextMetricsA(Handle dc, TextMetrics* out);
  virtual void SetTextColor(Handle dc, uint32_t color);
  virtual void SetBkColor(Handle dc, uint32_t color);
  virtual void SetBkMode(Handle dc, int32_t mode);
  virtual uint32_t GetLastError();
};

Host& host();
void set_host(Host* h);  // null restores the default (everything unported)

}  // namespace ztwod
