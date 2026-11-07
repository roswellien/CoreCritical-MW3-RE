#include "gamez/zrender/zrndr_ripple.h"
#include "gamez/zrender/zrndr_blit.h"

#include <cmath>
#include <cstring>

#include "crt/msvc_math.h"
#include "gamez/zrender/zrndr_tint.h"

#pragma STDC FP_CONTRACT OFF

namespace zrndr {
namespace {

RippleState g_ripple;

// The x87's precision (CLAUDE.md, FPU precision): at 24 bits every product and
// sum rounds to a float, at 53 to a double. fsin itself is not affected.
template <X87 P>
struct Fpu {
  using T = std::conditional_t<P == X87::k24, float, double>;
};

// 0x00558f15..0x00558f7d: amplitude x sin(r / wavelength + phase) and 1 / r
// for each radius the ring can reach, as floats.
template <X87 P>
void fill_tables(float* wave, float* inv, int32_t inner, int32_t outer, int32_t amplitude, float wavelength,
                 float phase) {
  using T = typename Fpu<P>::T;
  const float amp = static_cast<float>(amplitude);
  wave[0] = static_cast<float>(static_cast<T>(std::sin(double{phase}) * double{amp}));
  inv[0] = 1.0f;
  for (int32_t r = inner - 1 < 1 ? 1 : inner - 1; r <= outer; ++r) {
    const T a = static_cast<T>(static_cast<T>(r) / static_cast<T>(wavelength)) + static_cast<T>(phase);
    wave[r] = static_cast<float>(static_cast<T>(std::sin(double{a}) * double{amp}));
    inv[r] = static_cast<float>(T{1} / static_cast<T>(r));  // 0x005ce5b0
  }
}

// The radius of a squared distance by its float's bits: exponent halved,
// mantissa linear (never below the true root), truncated (0x005ac340).
int32_t approx_root(float d2) {
  uint32_t bits;
  std::memcpy(&bits, &d2, 4);
  const uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(bits) >> 1) + 0x1fc00000u;
  float f;
  std::memcpy(&f, &r, 4);
  return crt::msvc_ftol(f);
}

// One pixel's shift: ftol(i x wave[r] / r), ftol(j x ...).
template <X87 P>
void shift(const float* wave, const float* inv, int32_t r, int32_t i, int32_t j, int32_t* dx, int32_t* dy) {
  using T = typename Fpu<P>::T;
  const T k = static_cast<T>(static_cast<T>(inv[r]) * static_cast<T>(wave[r]));
  *dx = crt::msvc_ftol(static_cast<double>(static_cast<T>(static_cast<T>(i) * k)));
  *dy = crt::msvc_ftol(static_cast<double>(static_cast<T>(static_cast<T>(j) * k)));
}

template <X87 P>
void ripple_at(int32_t x, int32_t y, int32_t inner, int32_t outer, int32_t amplitude, float wavelength, float phase,
               const int32_t* clip_rect) {
  RippleState& st = g_ripple;
  const FinishTarget& t = finish_target();
  if (outer < 1) outer = 0;
  if (inner > outer) inner = outer;
  else if (inner < 0) inner = 0;
  if (inner == outer) return;
  const int32_t inner2 = inner * inner, outer2 = outer * outer;
  int32_t* c = st.clip;
  if (clip_rect) {
    std::memcpy(c, clip_rect, sizeof st.clip);
  } else {
    c[0] = 0, c[1] = 0, c[2] = t.width - 1, c[3] = t.height - 1;
  }
  const int32_t left = x - outer, top = y - outer;
  if (left - amplitude > c[2] || x + outer + amplitude < c[0]) return;
  if (top - amplitude > c[3] || y + outer + amplitude < c[1]) return;

  // FIX (B): the scratch as large as the target (rows of the target's width).
  const size_t need = static_cast<size_t>(t.width) * static_cast<size_t>(t.height > 0 ? t.height : 0);
  if (st.scratch.size() < need) st.scratch.resize(need);
  uint16_t* const dst = st.scratch.data();
  uint16_t* const src = t.base;
  const int32_t pd = t.width, ps = t.pitch_px;

  std::vector<float> wave(static_cast<size_t>(outer) + 1), inv(static_cast<size_t>(outer) + 1);
  fill_tables<P>(wave.data(), inv.data(), inner, outer, amplitude, wavelength, phase);
  const float far2 = static_cast<float>(outer2);
  // In the ring when inner^2 < d2 < outer^2 (d2 as a float); the radius is
  // capped at outer.
  auto ring = [&](int32_t i, int32_t j, float* d2) {
    *d2 = static_cast<float>(i * i + j * j);
    return *d2 < far2 && static_cast<float>(inner2) < *d2;
  };
  auto radius = [&](float d2) {
    const int32_t r = approx_root(d2);
    return r < outer ? r : outer;
  };
  // The eight points an (i, j) stands for, in the exe's order, each with its
  // shift turned the same way: (col, row, shifted col, shifted row).
  auto points = [](int32_t i, int32_t j, int32_t dx, int32_t dy, int32_t p[8][4]) {
    const int32_t a = i + dx, b = j + dy;
    const int32_t v[8][4] = {{i, j, a, b},   {j, i, b, a},   {-i, j, -a, b},  {j, -i, b, -a},
                             {i, -j, a, -b}, {-j, i, -b, a}, {-i, -j, -a, -b}, {-j, -i, -b, -a}};
    std::memcpy(p, v, sizeof v);
  };

  const bool inside = !(left - amplitude < c[0] || x + outer + amplitude >= c[2] || top - amplitude < c[1] ||
                        y + outer + amplitude >= c[3]);
  if (inside) {
    // 0x00558fbc: no checks, the 8 points of each (i, j) with -outer <= j <= i <= inner.
    for (int32_t j = -outer; j <= inner; ++j) {
      for (int32_t i = j; i <= inner; ++i) {
        float d2;
        int32_t dx = 0, dy = 0;
        if (ring(i, j, &d2)) shift<P>(wave.data(), inv.data(), radius(d2), i, j, &dx, &dy);
        int32_t p[8][4];
        points(i, j, dx, dy, p);
        for (const auto& q : p)
          dst[(y + q[1]) * pd + x + q[0]] = src[(y + q[3]) * ps + x + q[2]];
      }
    }
    // 0x00559306: the square back onto the target. The rows and columns are
    // tested against +-inner as screen coordinates, not from the centre, and
    // the pointers only move on a copy (both kept: they change nothing for a
    // button away from the screen's top-left corner).
    for (int32_t row = top; row < y + outer; ++row) {
      if (!(row > inner || row < -inner)) continue;
      uint16_t* s = src + row * ps + left;
      const uint16_t* d = dst + row * pd + left;
      for (int32_t col = left; col < x + outer; ++col)
        if (col > inner || col < -inner) *s++ = *d++;
    }
    return;
  }

  // 0x0055939a: clipped, every point and its source inside left <= x < right,
  // top <= y < bottom. A point whose source is outside keeps whatever the
  // scratch held, and that goes back onto the target.
  st.cx = x, st.cy = y;
  auto in = [c](int32_t px, int32_t py) { return c[0] <= px && px < c[2] && c[1] <= py && py < c[3]; };
  for (int32_t j = -outer; j <= inner; ++j) {
    for (int32_t i = j; i <= inner; ++i) {
      float d2;
      int32_t dx = 0, dy = 0;
      if (ring(i, j, &d2)) shift<P>(wave.data(), inv.data(), radius(d2), i, j, &dx, &dy);
      int32_t p[8][4];
      points(i, j, dx, dy, p);
      for (const auto& q : p) {
        const int32_t px = st.cx + q[0], py = st.cy + q[1], sx = st.cx + q[2], sy = st.cy + q[3];
        if (in(px, py) && in(sx, sy)) dst[py * pd + px] = src[sy * ps + sx];
      }
    }
  }
  const int32_t row_end = y + outer < c[3] ? y + outer : c[3];
  const int32_t col0 = left <= c[0] ? c[0] : left;
  for (int32_t row = top > c[1] ? top : c[1]; row < row_end; ++row) {
    if (!(row > inner || row < -inner)) continue;
    uint16_t* s = src + row * ps + col0;
    const uint16_t* d = dst + row * pd + col0;
    const int32_t col_end = c[2] <= x + outer ? c[2] : x + outer;
    for (int32_t col = col0; col < col_end; ++col)
      if (col > inner || col < -inner) *s++ = *d++;
  }
}

}  // namespace

RippleState& ripple_state() { return g_ripple; }

// Mech3.exe 1.2 @ 0x00558dd0
void ripple(int32_t x, int32_t y, int32_t inner, int32_t outer, int32_t amplitude, float wavelength, float phase,
            const int32_t* clip, X87 precision) {
  if (!finish_target().base) return;
  observe_framebuffer_write(finish_target().base, "ripple");
  if (precision == X87::k24) ripple_at<X87::k24>(x, y, inner, outer, amplitude, wavelength, phase, clip);
  else ripple_at<X87::k53>(x, y, inner, outer, amplitude, wavelength, phase, clip);
}

}  // namespace zrndr
