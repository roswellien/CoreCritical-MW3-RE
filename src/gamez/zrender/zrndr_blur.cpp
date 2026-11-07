#include "gamez/zrender/zrndr_blur.h"
#include "gamez/zrender/zrndr_blit.h"

#include <cstddef>

#include "gamez/zrender/zrndr_ripple.h"
#include "gamez/zrender/zrndr_tint.h"
#include "gamez/zvideo/zvid_ddd3d.h"

namespace zrndr {
namespace {

// The display's masks as 0x00579280 gives them: green alone, red and blue together.
struct Masks {
  uint32_t g, rb;
};
Masks masks() {
  const zvid::ChannelMasks& m = zvid::channel_masks();
  return {m.g, m.r | m.b};
}

// (a + b + 2c) / 4 per channel; the sums cannot carry into the next channel.
inline uint16_t mix(const Masks& m, uint32_t a, uint32_t b, uint32_t c) {
  const uint32_t g = (((m.g & a) + (m.g & b) + (m.g & c) * 2) >> 2) & m.g;
  const uint32_t rb = ((m.rb & a) + (m.rb & b) + (m.rb & c) * 2) >> 2;
  return static_cast<uint16_t>(g | (m.rb & rb));
}

// FIX (B): the scratch as large as the target, as for the ripple (the exe's
// 1.5 MB ends at 1024x768).
uint16_t* scratch(const FinishTarget& t) {
  std::vector<uint16_t>& s = ripple_state().scratch;
  const size_t need = static_cast<size_t>(t.width) * static_cast<size_t>(t.height > 0 ? t.height : 0);
  if (s.size() < need) s.resize(need);
  return s.data();
}

}  // namespace

// Mech3.exe 1.2 @ 0x0055a3b0. Each row is blurred into the scratch and copied
// back before the next, and the rows step by the scratch's width less the
// target's pitch, not to the next row: on a target as wide as its pitch the
// pass runs on as one line, two pixels short per row (docs/fmv.md).
void blur_h(const int32_t* rect) {
  observe_framebuffer_write(finish_target().base, "horizontal blur");
  const FinishTarget& t = finish_target();
  int32_t x0, y0, x1, y1;
  if (rect) {
    x0 = rect[0] < 1 ? 1 : rect[0];
    y0 = rect[1] < 0 ? 0 : rect[1];
    x1 = rect[2] > t.width - 1 ? t.width - 1 : rect[2];
    y1 = rect[3] > t.height - 1 ? t.height - 1 : rect[3];
  } else {
    x0 = 1, y0 = 0, x1 = t.width - 1, y1 = t.height - 1;
  }
  const int32_t count = x1 - x0;
  const Masks m = masks();
  const ptrdiff_t skip = t.width - t.pitch_px;
  uint16_t* src = t.base + (static_cast<ptrdiff_t>(y0) * t.pitch_px + x0);
  uint16_t* dst = scratch(t) + (static_cast<ptrdiff_t>(y0) * t.width + x0);
  for (int32_t y = y0; y < y1 + 1; ++y) {
    if (count > 0) {
      uint16_t* const s0 = src;
      const uint16_t* d0 = dst;
      for (int32_t n = 0; n < count; ++n, ++src) *dst++ = mix(m, src[-1], src[1], src[0]);
      for (int32_t n = 0; n < count; ++n) s0[n] = d0[n];
    }
    src += skip;
    dst += skip;
  }
}

// Mech3.exe 1.2 @ 0x0055a1b0. The rows above and below are a scratch width
// away, not a pitch; the first and last rows stay.
void blur_v(const int32_t* rect) {
  observe_framebuffer_write(finish_target().base, "vertical blur");
  const FinishTarget& t = finish_target();
  int32_t x0, y0, x1, y1;
  if (rect) {
    x0 = rect[0] < 0 ? 0 : rect[0];
    y0 = rect[1] < 1 ? 1 : rect[1];
    x1 = rect[2] > t.width - 1 ? t.width - 1 : rect[2];
    y1 = rect[3] > t.height - 1 ? t.height - 1 : rect[3];
  } else {
    x0 = 0, y0 = 1, x1 = t.width - 1, y1 = t.height - 1;
  }
  const int32_t count = x1 - x0 + 1;
  const ptrdiff_t w = t.width, skip = t.width - t.pitch_px;
  const Masks m = masks();
  uint16_t* const scr = scratch(t);
  const ptrdiff_t at_t = static_cast<ptrdiff_t>(y0) * t.pitch_px + x0, at_s = static_cast<ptrdiff_t>(y0) * w + x0;
  const uint16_t* src = t.base + at_t;
  uint16_t* dst = scr + at_s;
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t n = 0; n < count; ++n, ++src) *dst++ = mix(m, src[-w], src[w], src[0]);
    src += skip;
    dst += skip;
  }
  uint16_t* back = t.base + at_t;
  const uint16_t* from = scr + at_s;
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t n = 0; n < count; ++n) *back++ = *from++;
    back += skip;
    from += skip;
  }
}

// Mech3.exe 1.2 @ 0x00559ec0. Down the columns into the scratch (the row above
// copied first, the last row after), then along the rows back onto the
// target, the ends of each row copied.
void blur_both(const int32_t* rect) {
  observe_framebuffer_write(finish_target().base, "combined blur");
  const FinishTarget& t = finish_target();
  int32_t x0, y0, x1, y1;
  if (rect) {
    x0 = rect[0] < 0 ? 0 : rect[0];
    y0 = rect[1] < 1 ? 1 : rect[1];
    x1 = rect[2] > t.width - 1 ? t.width - 1 : rect[2];
    y1 = rect[3] > t.height - 1 ? t.height - 1 : rect[3];
  } else {
    x0 = 0, y0 = 1, x1 = t.width - 1, y1 = t.height - 1;
  }
  const int32_t count = x1 - x0 + 1;
  const ptrdiff_t w = t.width, skip = t.width - t.pitch_px;
  const Masks m = masks();
  uint16_t* const scr = scratch(t);
  // The scratch's row above y0 and the target's y0 less a width (not a pitch).
  uint16_t* dst = scr + (static_cast<ptrdiff_t>(y0) * w + x0) - w;
  const uint16_t* src = t.base + (static_cast<ptrdiff_t>(y0) * t.pitch_px + x0) - w;
  for (int32_t n = 0; n < count; ++n) *dst++ = *src++;
  dst += skip;
  src += skip;
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t n = 0; n < count; ++n, ++src) *dst++ = mix(m, src[-w], src[w], src[0]);
    src += skip;
    dst += skip;
  }
  for (int32_t n = 0; n < count; ++n) *dst++ = *src++;

  const int32_t ya = y0 - 1, yb = y1 + 1, xa = x0 + 1, inner = count - 2;
  const ptrdiff_t skip2 = skip + 2;
  uint16_t* out = t.base + (static_cast<ptrdiff_t>(ya) * t.pitch_px + xa);
  const uint16_t* in = scr + (static_cast<ptrdiff_t>(ya) * w + xa);
  for (int32_t y = ya; y < yb; ++y) {
    out[-1] = in[-1];
    for (int32_t n = 0; n < inner; ++n, ++in) *out++ = mix(m, in[-1], in[1], in[0]);
    *out = *in;
    out += skip2;
    in += skip2;
  }
}

// Mech3.exe 1.2 @ 0x0055a540
void blur(const int32_t* rect, int32_t kind) {
  if (kind == 1) return blur_h(rect);
  if (kind == 2) return blur_v(rect);
  blur_both(rect);
}

}  // namespace zrndr
