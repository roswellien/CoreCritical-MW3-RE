#include "gamez/zrender/zrndr_tint.h"
#include "gamez/zrender/zrndr_blit.h"

#include <cstring>

#include "crt/msvc_math.h"
#include "crt/msvc_rand.h"
#include "gamez/zvideo/zvid_ddd3d.h"

#pragma STDC FP_CONTRACT OFF

namespace zrndr {

namespace {
FinishTarget g_target;
TintState g_tint;
FinishConstants g_k;
FinishMachine g_machine;
TintHooks g_hooks;

// FIX (bug): the scalar rows write one pair past their count, so the last row
// of a buffer the width of the screen writes past its end: there only the
// pixels inside are read and written. A count that would run the loops wild
// draws nothing.
int64_t room(const uint16_t* p) {
  const FinishTarget& t = g_target;
  if (!t.base) return 1 << 30;
  return t.base + static_cast<int64_t>(t.height) * t.pitch_px - p;
}
bool inside(const uint16_t* p, int32_t pixels) { return room(p) >= pixels; }

// A scalar pair, read and written as far as the buffer goes.
uint32_t read_pair(const uint16_t* p) {
  const int64_t n = room(p);
  return (n > 0 ? p[0] : 0u) | (n > 1 ? static_cast<uint32_t>(p[1]) << 16 : 0u);
}
void write_pair(uint16_t* p, uint32_t v) {
  const int64_t n = room(p);
  if (n > 0) p[0] = static_cast<uint16_t>(v);
  if (n > 1) p[1] = static_cast<uint16_t>(v >> 16);
}
}  // namespace

FinishTarget& finish_target() { return g_target; }
TintState& tint_state() { return g_tint; }
FinishConstants& finish_constants() { return g_k; }
FinishMachine& finish_machine() { return g_machine; }
TintHooks& tint_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x00558290
void set_finish_target(uint16_t* base, int32_t width, int32_t height, int32_t pitch) {
  FinishTarget& t = g_target;
  t.width = width;
  t.height = height;
  t.pitch = pitch;
  t.base = base;
  t.pitch_px = pitch / 2;
}

// Mech3.exe 1.2 @ 0x00558540. The Direct3D rect's right is one past.
void tint(uint16_t colour, const int32_t* rect, double alpha, bool direct3d) {
  int32_t l, t, r, b;
  if (rect) {
    l = rect[0], t = rect[1], r = rect[2], b = rect[3];
  } else {
    l = 0, t = 0, r = g_target.width - 1, b = g_target.height;
  }
  if (direct3d) {
    const int32_t q[4] = {l, t, r + 1, b};
    if (g_hooks.quad) g_hooks.quad(colour, q, alpha);
    return;
  }
  TintState& s = g_tint;
  s.top = t, s.right = r, s.bottom = b, s.left = l;
  s.pending = 1;
  s.colour = colour;
  s.alpha = alpha;
}

// Mech3.exe 1.2 @ 0x005587b0
void tint_clear() { g_tint.pending = 0; }

// Mech3.exe 1.2 @ 0x005582c0: two pixels a dword, the fields of both scaled
// at once; one pair more than count / 2.
void tint_row_555(uint16_t* row, int32_t count) {
  const FinishConstants& k = g_k;
  for (int32_t n = count >> 1;; --n) {
    if (n < 0 || !inside(row, 1)) return;
    uint32_t p = read_pair(row);
    const uint32_t hi = (((p >> 5) & 0x3e0f81fu) * k.keep + k.pair_hi) & 0x7c1f03e0u;
    const uint32_t lo = ((((p & 0x3e07c1fu) * k.keep) >> 5) + k.pair_lo) & 0x3e07c1fu;
    write_pair(row, hi | lo);
    row += 2;
    if (n == 0) return;
  }
}

// Mech3.exe 1.2 @ 0x00558320
void tint_row_565(uint16_t* row, int32_t count) {
  const FinishConstants& k = g_k;
  for (int32_t n = count >> 1;; --n) {
    if (n < 0 || !inside(row, 1)) return;
    uint32_t p = read_pair(row);
    const uint32_t hi = ((p >> 5) & 0x7c0f83fu) * k.keep + k.pair_hi;
    const uint32_t lo = (((p & 0x7e0f81fu) * k.keep) >> 5) + k.pair_lo;
    write_pair(row, ((lo ^ hi) & 0x7e0f81fu) ^ hi);
    row += 2;
    if (n == 0) return;
  }
}

namespace {
// The MMX rows: four words at a time, each field scaled in its own word
// (PMULLW keeps the low 16 bits); a count's last count % 4 pixels are left.
void mmx_row(uint16_t* row, int32_t count, uint16_t rm, uint16_t gm, uint16_t bm) {
  const FinishConstants& k = g_k;
  const uint32_t quads = static_cast<uint32_t>(count) >> 2;
  if (!inside(row, static_cast<int32_t>(quads > 0x10000000u ? 0x7fffffff : quads * 4))) return;
  const auto keep = static_cast<uint16_t>(k.keep);
  const auto red = static_cast<uint16_t>(k.red), green = static_cast<uint16_t>(k.green),
             blue = static_cast<uint16_t>(k.blue);
  for (uint32_t i = 0; i < quads * 4; ++i) {
    const uint16_t p = row[i];
    const auto r = static_cast<uint16_t>(static_cast<uint16_t>(((p & rm) >> 5) * keep) + red) & rm;
    const auto g = static_cast<uint16_t>(static_cast<uint16_t>(((p & gm) >> 5) * keep) + green) & gm;
    const auto b = static_cast<uint16_t>((static_cast<uint16_t>((p & bm) * keep) >> 5) + blue) & bm;
    row[i] = static_cast<uint16_t>(r + g + b);
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x00558380
void tint_row_555_mmx(uint16_t* row, int32_t count) { mmx_row(row, count, 0x7c00, 0x03e0, 0x001f); }

// Mech3.exe 1.2 @ 0x00558460
void tint_row_565_mmx(uint16_t* row, int32_t count) { mmx_row(row, count, 0xf800, 0x07e0, 0x001f); }

// Mech3.exe 1.2 @ 0x00558610. The tint stays pending: every later finish
// tints again until 0x00558540 sets another or 0x005587b0 drops it. The right
// edge kept for the screen is width - 1, so the MMX rows (four at a time)
// leave the last four columns of a 640-wide screen untinted.
uint32_t finish() {
  const TintState& s = g_tint;
  if (!s.pending) return 0;
  observe_framebuffer_write(g_target.base, "tint");
  FinishConstants& k = g_k;
  const FinishTarget& t = g_target;
  if (g_machine.mmx) k.row = g_machine.format == 5 ? 2 : 3;
  else k.row = g_machine.format == 5 ? 0 : 1;
  const zvid::ChannelMasks& m = zvid::channel_masks();
  const auto in = static_cast<uint32_t>(crt::msvc_ftol(static_cast<float>(s.alpha) * 32.0f));
  uint32_t red = ((m.r & s.colour) * in) >> 5;
  uint32_t green = ((m.g & s.colour) * in) >> 5;
  uint32_t blue = ((m.b & s.colour) * in) >> 5;
  red |= red << 16;
  green |= green << 16;
  blue |= blue << 16;
  const uint32_t hi = ((m.b & blue) | (m.r & red)) << 16 | (m.g & green);
  k.pair_hi = hi;
  k.blue = blue;
  k.red = red;
  k.pair_lo = hi >> 16 | hi << 16;
  k.green = green;
  const int32_t top = s.top < 0 ? 0 : s.top;
  const int32_t bottom = s.bottom > t.height ? t.height : s.bottom;
  const int32_t left = s.left < 0 ? 0 : s.left;
  const int32_t right = s.right > t.width ? t.width : s.right;
  k.keep = static_cast<uint32_t>(crt::msvc_ftol(static_cast<double>(static_cast<float>(1.0 - s.alpha)) * 32.0));
  uint32_t ret = k.keep;
  uint16_t* row = t.base + (top * t.pitch_px + left);
  const int32_t count = right - left;
  for (int32_t y = top; y < bottom; ++y) {
    switch (k.row) {
      case 0: tint_row_555(row, count); break;
      case 1: tint_row_565(row, count); break;
      case 2: tint_row_555_mmx(row, count); break;
      default: tint_row_565_mmx(row, count); break;
    }
    ret = static_cast<uint32_t>(t.pitch_px);
    row += t.pitch_px;
  }
  return ret;
}

std::vector<uint8_t>& noise_table() {
  static std::vector<uint8_t> t;
  return t;
}

// Mech3.exe 1.2 @ 0x005581c0
void finish_init(int32_t (*rand)()) {
  std::vector<uint8_t>& t = noise_table();
  t.assign(0x6400 + 1, 0);
  for (size_t i = 0; i < 0x6400; ++i) t[i] = static_cast<uint8_t>(rand ? rand() : crt::msvc_rand());
}

// Mech3.exe 1.2 @ 0x00558c10
void static_fill(const int32_t* rect, double level, uint16_t lo, uint16_t mid, uint16_t hi, int32_t (*rand)()) {
  const FinishTarget& f = g_target;
  const int32_t under = crt::msvc_ftol(level * 256.0);  // 0x005ce5a8
  int32_t left, top, right, bottom;
  if (rect) left = rect[0], top = rect[1], right = rect[2], bottom = rect[3];
  else left = 0, top = 0, right = f.width - 1, bottom = f.height - 1;
  const int32_t w = right - left;
  const std::vector<uint8_t>& t = noise_table();
  if (t.empty() || !f.base) return;  // not in the exe: no set-up, no target
  const int32_t size = 0x6400;       // [0x0072c1d8]
  for (int32_t y = top; y < bottom; ++y) {
    const int32_t r = rand ? rand() : crt::msvc_rand();
    const uint8_t* n = t.data() + r * (size - w) / 0x7fff;
    uint16_t* p = f.base + (y * f.pitch_px + left);
    for (int32_t i = 0; i < w; ++i, ++p, ++n) {
      if (n[1] >= under) continue;
      observe_framebuffer_write(f.base, "damage static");
      *p = n[0] < 0x55 ? lo : n[0] < 0xab ? mid : hi;
    }
  }
}

// Mech3.exe 1.2 @ 0x0055a740 (ECX a rect or null: the finish target's
// whole): in Direct3D the display's green (0x00579280's second mask) over it
// at 0.3 (0x0057dd80), the pocket commander's view (0x004b5280). The
// software path, which masks the finish target's pixels, is not ported (the
// software renderer is not).
void dim(const int32_t* rect) {
  int32_t r[4] = {0, 0, finish_target().width - 1, finish_target().height - 1};
  if (rect) std::memcpy(r, rect, sizeof r);
  if (tint_hooks().quad) tint_hooks().quad(static_cast<uint16_t>(zvid::channel_masks().g), r, 0.3);
}

}  // namespace zrndr
