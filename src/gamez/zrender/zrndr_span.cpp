#include "gamez/zrender/zrndr_span.h"
#include "gamez/zrender/zrndr_blit.h"

#include <cstring>

#pragma STDC FP_CONTRACT OFF

namespace zrndr {

namespace {
SpanState g_span;
SoftTarget g_target;

// One PUSH: step the write pointer back and store the value little-endian, as x86
// does. The original's write pointer is ESP; here it is an ordinary pointer.
inline void push16(uint8_t*& p, uint16_t v) {
  p -= 2;
  std::memcpy(p, &v, 2);
}
inline void push32(uint8_t*& p, uint32_t v) {
  p -= 4;
  std::memcpy(p, &v, 4);
}
}  // namespace

SpanState& span_state() { return g_span; }

// FIX (A): the original fills spans by pointing ESP at them and pushing
// (docs/blit-trick.md); same writes in the same order, through a pointer.
void span_fill16(uint32_t color, uint32_t count) {
  if (auto* observer = draw_observer())
    observer->span(reinterpret_cast<const uint16_t*>(g_span.dest), count, static_cast<uint16_t>(color));
  uint8_t* p = g_span.dest + count * 2;    // ADD ESP, ECX twice
  const uint32_t pair = (color << 16) | color;
  uint32_t n = count;
  if (n & 1) push16(p, static_cast<uint16_t>(pair));  // PUSH AX
  n >>= 1;
  if (n & 1) push32(p, pair);              // PUSH EAX
  n >>= 1;
  while (n != 0) {                         // PUSH EAX; PUSH EAX; DEC ECX
    push32(p, pair);
    push32(p, pair);
    n -= 1;
  }
}

void span_textured16(uint32_t u, uint32_t v, int32_t count) {
  observe_framebuffer_write(g_span.dest, "textured span");
  uint8_t* p = g_span.dest + static_cast<uint32_t>(count) * 2;
  uint32_t bytes = static_cast<uint32_t>(count) * 2;
  uint32_t mask = g_span.v_mask;
  // As in the original, count must be at least 1: SUB EDI, 2 / JNZ never stops at 0.
  do {
    const uint32_t col = (static_cast<uint32_t>(static_cast<int32_t>(u) >> 20)) & 0x3ff;  // SAR
    const uint32_t row = (mask & v) >> 10;
    u += g_span.du;
    v += g_span.dv;
    mask = g_span.v_mask;
    bytes -= 2;
    push16(p, g_span.texture[col + row]);  // PUSH WORD [EBP + EAX*2]
  } while (bytes != 0);
}

SoftTarget& soft_target() { return g_target; }

// Mech3.exe 1.2 @ 0x00557f90
void set_target(uint8_t* base, const int32_t* rect, int32_t depth, int32_t pitch) {
  SoftTarget& t = g_target;
  t.base = base;
  if (rect) {
    t.width = rect[2] - rect[0];
    t.height = rect[3] - rect[1];
    std::memcpy(t.rect, rect, sizeof t.rect);
  }
  if (depth) t.bytes = static_cast<int32_t>(static_cast<uint32_t>(depth) >> 3);
  t.pitch = pitch;
  t.screen_row = t.screen_w * t.bytes;
}

namespace {
// One entry of 0x0055d010's edge lists (16 bytes there): from its first row
// on, x steps by slope each row, both 16.16.
struct Edge {
  int32_t slope;                   // +0x00
  int32_t row;                     // +0x04  first row; 0x7fffffff ends the list
  int32_t x;                       // +0x08
};
// The exe keeps 65 a side on its stack, unchecked; a side holds at most one
// entry per vertex and the end (the count is a byte).
constexpr int kEdges = 257;
constexpr float kScale = -65536.0f;  // 0x005ce5c4

// The 1.5 * 2^52 trick: `scaled` (a value times -65536) taken from the
// constant at the x87's precision R; the double's low dword is the value in
// 16.16, rounded, when R has the bits. At 24 bits only bits 29..31 survive:
// 0 for anything under 2^13.
template <typename R>
int32_t magic(R scaled) {
  const R r = static_cast<R>(6755399441055744.0) - scaled;  // 0x005ce5c8
  const double d = static_cast<double>(r);
  uint64_t b;
  std::memcpy(&b, &d, 8);
  return static_cast<int32_t>(static_cast<uint32_t>(b));
}
template <typename R>
int32_t fixed(float v) {
  return magic<R>(static_cast<R>(v) * static_cast<R>(kScale));
}
int32_t add(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

template <typename R>
struct EdgeWalk {
  const float* v;
  int32_t row = 0;                 // the next edge's first row: (16.16 y + 0x7fff) >> 16
  R centre = 0;                    // its centre, row + 0.5
  void start_at(int32_t i) {
    row = add(fixed<R>(v[3 * i + 1]), 0x7fff) >> 16;
    centre = static_cast<R>(row) - static_cast<R>(-0.5f);  // 0x005ce5d0
  }
  // From vertex s to e: an entry unless the centre is below e (also for NaN).
  void edge(int32_t s, int32_t e, Edge*& out) {
    const float* a = v + 3 * s;
    const float* b = v + 3 * e;
    if (centre > static_cast<R>(b[1])) return;
    Edge& d = *out++;
    d.row = row;
    const R dy = static_cast<R>(b[1]) - static_cast<R>(a[1]);
    if (dy < 0 || dy > 0) {
      const R dx = static_cast<R>(b[0]) - static_cast<R>(a[0]);
      const R slope = dx / dy;
      const R x = (centre - static_cast<R>(a[1])) * slope + static_cast<R>(a[0]);
      d.slope = magic<R>(slope * static_cast<R>(kScale));
      d.x = magic<R>(x * static_cast<R>(kScale));
    } else {
      // FIX (bug): a level edge just above a row centre (within 2^-17) gets
      // its entry but no x or slope: the original's stack garbage, used for
      // that row. Here its end and no slope (fixes.md).
      d.slope = 0;
      d.x = fixed<R>(b[0]);
    }
    start_at(e);
  }
};

template <typename R>
void fill(const float* v, int32_t n, uint32_t color) {
  // Top and bottom by the y's bits as signed integers (the y's are >= 0 here).
  auto ybits = [v](int32_t i) {
    int32_t b;
    std::memcpy(&b, v + 3 * i + 1, 4);
    return b;
  };
  int32_t top = 0, bottom = 0, lo = ybits(0), hi = lo;
  for (int32_t i = n - 1; i > 0; --i) {
    const int32_t y = ybits(i);
    if (y < lo) lo = y, top = i;
    else if (y > hi) hi = y, bottom = i;
  }
  // Zeroed: the original's lists start as stack garbage (read only by the
  // level edges above and a side with no entry).
  Edge left[kEdges] = {}, right[kEdges] = {};
  Edge* up = g_target.winding ? left : right;  // walking up the vertex list
  Edge* down = g_target.winding ? right : left;
  EdgeWalk<R> w{v};
  w.start_at(top);
  for (int32_t s = top, e = -1; e != bottom; s = e) {
    e = s + 1 < n ? s + 1 : 0;
    w.edge(s, e, up);
  }
  w.start_at(top);
  for (int32_t s = top, e = -1; e != bottom; s = e) {
    e = s - 1 >= 0 ? s - 1 : n - 1;
    w.edge(s, e, down);
  }
  up->row = 0x7fffffff;
  down->row = 0x7fffffff;

  int32_t y = add(fixed<R>(v[3 * top + 1]), 0x7fff) >> 16;
  const int32_t last = add(fixed<R>(v[3 * bottom + 1]), -0x8041) >> 16;
  uint8_t* row = g_target.base + static_cast<intptr_t>(y) * g_target.pitch;
  const Edge* l = left + 1;
  const Edge* r = right + 1;
  int32_t lx = left[0].x, ls = left[0].slope, rx = right[0].x, rs = right[0].slope;
  for (; y <= last; ++y, row += g_target.pitch) {
    if (y >= l->row) lx = l->x, ls = l->slope, ++l;
    if (y >= r->row) rx = r->x, rs = r->slope, ++r;
    const int32_t x0 = add(lx, 0x7fff) >> 16, x1 = add(rx, -0x8001) >> 16;
    lx = add(lx, ls);
    rx = add(rx, rs);
    if (x0 <= x1) {
      g_span.dest = row + static_cast<intptr_t>(x0) * g_target.bytes;
      g_target.fill(color, static_cast<uint32_t>(x1 - x0 + 1));
    }
  }
}
}  // namespace

void fill_polygon(const float* vertices, int32_t count, uint32_t color, X87 precision) {
  // FIX (bug): with no vertices the exe's second walk never meets the bottom
  // and loops forever (a polygon of count 0 whose stale first three face up).
  if (count <= 0) return;
  if (precision == X87::k24)
    fill<float>(vertices, count, color);
  else
    fill<double>(vertices, count, color);
}

}  // namespace zrndr
