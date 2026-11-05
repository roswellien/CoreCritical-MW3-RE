#pragma once
#include <cstdint>

namespace zimg { struct Texture; }

// The software 2D blits zTwoD draws with (docs/twod.md, Drawing). Original
// file unknown: between zreader.cpp and zrndr_draw.c (0x0055b1f0..0x0055bbe0),
// next to the 2D target they draw into.
namespace zrndr {

// The locked 16-bit buffer (0x008007c8..0x008007d4), set by the video lock.
struct Target2D {
  uint8_t* base = nullptr;         // 0x008007c8
  uint32_t width = 0;              // 0x008007cc  pixels
  uint32_t height = 0;             // 0x008007d0
  uint32_t pitch = 0;              // 0x008007d4  bytes
  int32_t format = 6;              // 0x0074a738  6: RGB565, else RGB555 (the alpha blend)
};
Target2D& target2d();

struct BlitRect;
// The SDL HUD adapter observes owned draw recipes while the native buffer is
// still maintained for reads/grabs. It never receives guest addresses or state.
class DrawObserver {
 public:
  virtual ~DrawObserver() = default;
  virtual void image(const zimg::Texture*, int32_t x, int32_t y, uint16_t key, const BlitRect*) = 0;
  virtual void pixel(const uint16_t* destination, uint16_t colour) = 0;
  virtual void span(const uint16_t* destination, uint32_t count, uint16_t colour) = 0;
  virtual void framebuffer_write(const void* target, const char* operation) = 0;
};
DrawObserver* draw_observer();
void set_draw_observer(DrawObserver*);
void observe_framebuffer_write(const void* target, const char* operation);

// FIX (A): the program's 16-bit layer over the Direct3D frame (app_engine)
// starts as `key`, not the frame the original's locked back buffer held, so
// an alpha blend there would mix with the key colour. While `alpha` is set
// (the layer's lock), a blend records each pixel's coverage and the colour it
// wrote, and the overlay draws that pixel with that alpha (docs/render-frame.md).
struct LayerCover {
  uint8_t* alpha = nullptr;        // per pixel, as the target; 0: none recorded
  uint16_t* colour = nullptr;      // the colour the blend wrote
  uint16_t key = 0;                // the layer's untouched colour
};
LayerCover& layer_cover();
void cover_opaque(uint16_t* pixel);  // an opaque write into the 2D target: its recorded blend ended

// A source rectangle, as a Win32 RECT.
struct BlitRect {
  int32_t left, top, right, bottom;
};

// Mech3.exe 1.2 @ 0x0055b3c0, the 16-bit target's blit ([0x00800824]):
// `image` at (x, y), all of it or `src`'s part, clipped to the target. Pixels
// are 16-bit or palette indices; with an alpha plane each pixel blends by its
// alpha (none at 0..3, or 0..7 in 555; all from 0xfc), else with flag 0x02 the
// colour (or index) `key` is transparent.
void blit16(const zimg::Texture* image, int32_t x, int32_t y, uint16_t key, const BlitRect* src);

// Mech3.exe 1.2 @ 0x00563b80, the 16-bit target's line ([0x008008c0], behind
// 0x00562940): from (x0, y0) to (x1, y1) inclusive, stepping the longer axis.
void line16(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t pixel);

}  // namespace zrndr
