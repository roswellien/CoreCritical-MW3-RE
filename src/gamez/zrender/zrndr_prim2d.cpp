#include "gamez/zrender/zrndr_prim2d.h"

#include <cstring>

#include "crt/msvc_math.h"
#include "crt/msvc_rand.h"
#include "gamez/zrender/zrndr_blit.h"
#include "gamez/ztwod/presentation.h"

#pragma STDC FP_CONTRACT OFF

namespace zrndr {

namespace {
// FIX (bug): off the target, skipped (the original wrote there).
void put(int32_t x, int32_t y, uint16_t pixel) {
  if (!ztwod::presentation_output_enabled()) return;
  const Target2D& t = target2d();
  if (!t.base || x < 0 || y < 0 || x >= static_cast<int32_t>(t.width) || y >= static_cast<int32_t>(t.height)) return;
  uint16_t* d = reinterpret_cast<uint16_t*>(t.base) + y * static_cast<int32_t>(t.pitch >> 1) + x;
  *d = pixel, cover_opaque(d);
}
bool inside(const int32_t* c, int32_t x, int32_t y) { return x >= c[0] && x <= c[2] && y >= c[1] && y <= c[3]; }
void put_in(const int32_t* clip, int32_t x, int32_t y, uint16_t pixel) {
  if (!clip || inside(clip, x, y)) put(x, y, pixel);
}
constexpr float kRandScale = 3.0518509e-05f;  // 0x005ce60c: 1 / 32767
constexpr float kTenth = 0.1f;                 // 0x005ce608
// The exe's jitter: p + (rand / 32767 * 2 - 1) * amp, each step at float.
int32_t jitter(int32_t p, float amp) {
  float t = static_cast<float>(crt::msvc_rand()) * kRandScale;
  t = t + t;
  t = t - 1.0f;  // 0x005ce5e0
  t = t * amp;
  return crt::msvc_ftol(t + static_cast<float>(p));
}
}  // namespace

// Mech3.exe 1.2 @ 0x00563c50 (behind 0x00562c80)
void line16_dashed(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t pixel, int32_t dash) {
  int32_t dy = y1 - y0, sy = 1;
  if (dy < 0) dy = -dy, sy = -1;
  int32_t dx = x1 - x0, sx = 1;
  if (dx < 0) dx = -dx, sx = -1;
  bool on = true;
  int32_t x = x0, y = y0, k = 0;
  if (dx > dy) {
    const int32_t run = (dx + 1) / dash;
    int32_t err = dx >> 1;
    for (int32_t n = dx + 1; n; --n) {
      if (on) put(x, y, pixel);
      err += dy;
      x += sx;
      if (err > dx) err -= dx, y += sy;
      if (k++ >= run) on = !on, k = 0;
    }
  } else {
    const int32_t run = (dy + 1) / dash;
    int32_t err = dy >> 1;
    for (int32_t n = dy + 1; n; --n) {
      if (on) put(x, y, pixel);
      err += dx;
      y += sy;
      if (err > dy) err -= dy, x += sx;
      if (k++ >= run) on = !on, k = 0;
    }
  }
}

// Mech3.exe 1.2 @ 0x00563db0: outcodes 1 left, 2 right, 4 above, 8 below;
// x first by dy/dx, then y by dx/dy, each move truncated.
void line16_clipped(const int32_t c[4], int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t pixel) {
  auto code = [&](int32_t x, int32_t y) {
    int32_t o = x < c[0] ? 1 : x > c[2] ? 2 : 0;
    return o | (y < c[1] ? 4 : y > c[3] ? 8 : 0);
  };
  const int32_t c0 = code(x0, y0), c1 = code(x1, y1);
  if (c0 & c1) return;
  int32_t dx = x1 - x0, dy = y1 - y0;
  if (c0 | c1) {
    const float m = dx == 0 ? 0.0f : static_cast<float>(dy) / static_cast<float>(dx);  // 0x005ce5d4
    const float n = dy == 0 ? 0.0f : static_cast<float>(dx) / static_cast<float>(dy);
    if (x0 < c[0]) y0 += crt::msvc_ftol(static_cast<float>(c[0] - x0) * m), x0 = c[0];
    else if (x0 > c[2]) y0 += crt::msvc_ftol(static_cast<float>(c[2] - x0) * m), x0 = c[2];
    if (x1 < c[0]) y1 += crt::msvc_ftol(static_cast<float>(c[0] - x1) * m), x1 = c[0];
    else if (x1 > c[2]) y1 += crt::msvc_ftol(static_cast<float>(c[2] - x1) * m), x1 = c[2];
    if (y0 < c[1]) {
      if (y1 < c[1]) return;
      x0 += crt::msvc_ftol(static_cast<float>(c[1] - y0) * n), y0 = c[1];
    } else if (y0 > c[3]) {
      if (y1 > c[3]) return;
      x0 += crt::msvc_ftol(static_cast<float>(c[3] - y0) * n), y0 = c[3];
    }
    if (y1 < c[1]) x1 += crt::msvc_ftol(static_cast<float>(c[1] - y1) * n), y1 = c[1];
    else if (y1 > c[3]) x1 += crt::msvc_ftol(static_cast<float>(c[3] - y1) * n), y1 = c[3];
    dx = x1 - x0, dy = y1 - y0;
  }
  int32_t sy = 1, sx = 1;
  if (dy < 0) dy = -dy, sy = -1;
  if (dx < 0) dx = -dx, sx = -1;
  int32_t x = x0, y = y0;
  if (dx > dy) {
    int32_t err = dx >> 1;
    for (int32_t k = dx + 1; k; --k) {
      put(x, y, pixel);
      err += dy;
      x += sx;
      if (err > dx) err -= dx, y += sy;
    }
  } else {
    int32_t err = dy >> 1;
    for (int32_t k = dy + 1; k; --k) {
      put(x, y, pixel);
      err += dx;
      y += sy;
      if (err > dy) err -= dy, x += sx;
    }
  }
}

// Mech3.exe 1.2 @ 0x00562cb0
void line16_list(const int32_t* p, int32_t count, const int32_t clip[4], uint16_t pixel) {
  for (int32_t i = 0; i < count; ++i, p += 2) line16_clipped(clip, p[0], p[1], p[2], p[3], pixel);
}

// Mech3.exe 1.2 @ 0x005630a0
void pixel16(int32_t x, int32_t y, uint16_t pixel) { put(x, y, pixel); }

// Mech3.exe 1.2 @ 0x005630c0
void circle16(int32_t cx, int32_t cy, int32_t r, uint16_t pixel, const int32_t* clip) {
  if (r <= 0) return;
  int32_t d = 1 - r, x = r;
  for (int32_t y = 0; y <= x; ++y) {
    put_in(clip, cx + x, cy + y, pixel);
    put_in(clip, cx - x, cy + y, pixel);
    put_in(clip, cx + x, cy - y, pixel);
    put_in(clip, cx - x, cy - y, pixel);
    put_in(clip, cx + y, cy + x, pixel);
    put_in(clip, cx - y, cy + x, pixel);
    put_in(clip, cx - y, cy - x, pixel);
    put_in(clip, cx + y, cy - x, pixel);
    if (d >= 0) --x, d -= 2 * x;
    d += 3 + 2 * y;
  }
}

// Mech3.exe 1.2 @ 0x00563440 (its points by 0x005641f0, 0x00564240, 0x005642f0)
void circle16_noisy(int32_t cx, int32_t cy, int32_t r, uint16_t pixel, float noise, const int32_t* clip) {
  if (r <= 0) return;
  const float amp = static_cast<float>(r) * noise * kTenth;
  auto point = [&](int32_t px, int32_t py) {
    const int32_t jx = jitter(px, amp);
    const int32_t jy = jitter(py, amp);
    put_in(clip, jx, jy, pixel);
  };
  int32_t d = 1 - r, x = r;
  for (int32_t y = 0; y <= x; ++y) {
    point(cx + x, cy + y);
    point(cx - x, cy + y);
    point(cx + x, cy - y);
    point(cx - x, cy - y);
    point(cx + y, cy + x);
    point(cx - y, cy + x);
    point(cx - y, cy - x);
    point(cx + y, cy - x);
    if (d >= 0) --x, d -= 2 * x;
    d += 3 + 2 * y;
  }
}

// Mech3.exe 1.2 @ 0x00562970
void line16_noisy(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t pixel, float noise, const int32_t* clip) {
  int32_t dy = y1 - y0, sy = 1;
  if (dy < 0) dy = -dy, sy = -1;
  int32_t dx = x1 - x0, sx = 1;
  if (dx < 0) dx = -dx, sx = -1;
  const float fx = static_cast<float>(dx), fy = static_cast<float>(dy);
  const float len2 = fx * fx + fy * fy;
  uint32_t b;
  std::memcpy(&b, &len2, 4);
  b = static_cast<uint32_t>(static_cast<int32_t>(b) >> 1) + 0x1fc00000u;  // the square root's guess
  float len;
  std::memcpy(&len, &b, 4);
  const float amp = len * noise * kTenth;
  int32_t x = x0, y = y0;
  if (dx > dy) {
    int32_t err = dx >> 1;
    for (int32_t k = dx + 1; k; --k) {
      const int32_t px = jitter(x, amp), py = jitter(y, amp);
      put_in(clip, px, py, pixel);
      x += sx;
      err += dy;
      if (err > dx) err -= dx, y += sy;
    }
  } else {
    int32_t err = dy >> 1;
    for (int32_t k = dy + 1; k; --k) {
      const int32_t px = jitter(x, amp), py = jitter(y, amp);
      put_in(clip, px, py, pixel);
      y += sy;
      err += dx;
      if (err > dy) err -= dy, x += sx;
    }
  }
}

}  // namespace zrndr
