// zTwoD's HUD primitives (0x005a3170..0x005a3bc0, 0x005a67e0..0x005a6d10;
// file name unknown, named after what it holds): built and set up only, their
// drawing not ported yet (docs/twod.md).
#include "crt/msvc_math.h"
#include "gamez/ztwod/twod.h"

#include <cmath>

#pragma STDC FP_CONTRACT OFF

namespace ztwod {

// Mech3.exe 1.2 @ 0x005a39f0
Frame::Frame() : Object2D(0, 0) {}

// Mech3.exe 1.2 @ 0x005a3a60
Frame::Frame(int32_t px, int32_t py, int32_t r, uint32_t c, const Rect* cl)
    : Object2D(px, py), radius(r), radius_sq(r * r), color(c), clipped(cl != nullptr) {
  if (cl) clip = *cl;
}

// Mech3.exe 1.2 @ 0x005a3b60
FilledBox::FilledBox(int32_t px, int32_t py, int32_t r, uint32_t c, const Rect* cl) : Frame(px, py, r, c, cl) {}

// Mech3.exe 1.2 @ 0x005a67e0
Line2D::Line2D(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) : Object2D(0, 0) {
  set(x0, y0, x1, y1);
  clipped = 0;
  color = c;
  clip = nullptr;
  dash = 0;
}

// Mech3.exe 1.2 @ 0x005a6b80
Pixel2D::Pixel2D(int32_t px, int32_t py, uint32_t c) : Object2D(px, py), color(c) {}

// Mech3.exe 1.2 @ 0x005a6bb0
void Pixel2D::draw() {
  if (!can_present()) return;
  erase();
  host().pixel(x, y, color);
}

// Mech3.exe 1.2 @ 0x005a6880
void Line2D::set(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
  set_pos(x0, y0);
  end_x = x1;
  end_y = y1;
}

// Mech3.exe 1.2 @ 0x005a6ae0
ClipLine2D::ClipLine2D(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) : Line2D(x0, y0, x1, y1, c) {}

// Mech3.exe 1.2 @ 0x005a3170
Shape::Shape() {
  Shape::set_pos(0, 0);
  set_scale(0.0f);
}

// Mech3.exe 1.2 @ 0x005a34c0
void Shape::set_pos(int32_t px, int32_t py) {
  const float t[3] = {static_cast<float>(px), static_cast<float>(py), 0.0f};
  set_translation(t);
}

// Mech3.exe 1.2 @ 0x005a34f0
void Shape::set_translation(const float* t) {
  translation[0] = t[0];
  translation[1] = t[1];
  translation[2] = t[2];
  xform |= 1;
  dirty();
}

// Mech3.exe 1.2 @ 0x005a3530
void Shape::set_scale(float s) {
  scale = s;
  xform |= 2;
  dirty();
}

namespace {
const float kTriangle[9] = {0.0f, -0.57735002f, 0.0f, -0.5f, 0.28867501f, 0.0f, 0.5f, 0.28867501f, 0.0f};  // 0x005f4a20
}  // namespace

// Mech3.exe 1.2 @ 0x005a3270: scaled (when the scale changed) and moved.
// Moved alone, the translation adds onto the points again (as written; the
// HUD always sets both).
void Shape::transform() {
  if (xform > 1)
    for (int i = 0; i < 9; ++i) points[i] = scale * kTriangle[i];
  for (int c = 0; c < 3; ++c)
    for (int i = 0; i < 3; ++i) points[3 * c + i] = points[3 * c + i] + translation[i];
  set_point(0, crt::msvc_ftol(points[0]), crt::msvc_ftol(points[1]));
  set_point(1, crt::msvc_ftol(points[3]), crt::msvc_ftol(points[4]));
  set_point(2, crt::msvc_ftol(points[6]), crt::msvc_ftol(points[7]));
  set_point(3, crt::msvc_ftol(points[0]), crt::msvc_ftol(points[1]));
  xform = 0;
}

// Mech3.exe 1.2 @ 0x005a3550
void Shape::update(float dt) {
  if (xform) transform();
  Object2D::update(dt);
}

// Mech3.exe 1.2 @ 0x005a3440: between the apex's row (point 0) and the
// base's (point 1), within 0.577 of the depth either side of the centre.
bool Shape::contains(int32_t px, int32_t py) {
  if (flags & kHidden) return false;
  if (xform) transform();
  if (py > pts[1][1]) return false;
  const float d = static_cast<float>(py - pts[0][1]);
  if (!(d > 0.0f)) return false;  // [0x005cffd8]
  return !(d * 0.57735097f < std::fabs(static_cast<float>(px) - translation[0]));  // [0x005cffdc]
}

// Mech3.exe 1.2 @ 0x00409dd0
void Frame::set_clip(const Rect* r) { clip = r ? *r : Rect{}; }

// Mech3.exe 1.2 @ 0x005a3b30
bool Frame::contains(int32_t px, int32_t py) const {
  const int32_t dx = px - x, dy = py - y;
  return dx * dx + dy * dy < radius_sq;
}

// Mech3.exe 1.2 @ 0x005a6bd0
Polygon::Polygon() : Object2D(0, 0) { dirty(); }

// Mech3.exe 1.2 @ 0x005a6c30
void Polygon::set_point(int32_t i, float px, float py) {
  if (i < 0x15 && i > -1) {
    pts[i][0] = px;
    pts[i][1] = py;
    if (count < i + 1) count = i + 1;
    if (i == 0) {
      const int32_t ty = crt::msvc_ftol(py);
      set_pos(crt::msvc_ftol(px), ty);
    }
  }
  dirty();
}

namespace {
LineClip g_line_clip;
}  // namespace
LineClip& line_clip() { return g_line_clip; }

// Mech3.exe 1.2 @ 0x0059b2f0
void set_line_clip(const float* mn, const float* mx) {
  LineClip& c = g_line_clip;
  for (int i = 0; i < 3; ++i) c.min[i] = mn[i], c.max[i] = mx[i];
  c.ints[0] = crt::msvc_ftol(c.min[0]);
  c.ints[1] = crt::msvc_ftol(c.min[1]);
  c.ints[2] = crt::msvc_ftol(c.max[0]);
  c.ints[3] = crt::msvc_ftol(c.max[1]);
}

namespace {
// 0x0059b610 / 0x0059b780: `a` moved to v on one axis along the line to `b`.
void move_x(float* a, const float* b, float v) {
  const float t = a[0];
  a[0] = v;
  a[1] = ((v - t) * (b[1] - a[1])) / (b[0] - t) + a[1];
}
void move_y(float* a, const float* b, float v) {
  const float t = a[1];
  a[1] = v;
  a[0] = ((b[0] - a[0]) * (v - t)) / (b[1] - t) + a[0];
}
// 0x0059b4d0 (axis 0) and 0x0059b640 (axis 1)
bool clip_axis(float* a, float* b, int axis) {
  const LineClip& c = g_line_clip;
  const float lo = c.min[axis], hi = c.max[axis];
  if (hi < a[axis] && hi < b[axis]) return false;
  if (a[axis] < lo && b[axis] < lo) return false;
  auto move = axis == 0 ? move_x : move_y;
  if (a[axis] < lo) move(a, b, lo);
  else if (hi < a[axis]) move(a, b, hi);
  if (b[axis] < lo) move(b, a, lo);
  else if (hi < b[axis]) move(b, a, hi);
  return true;
}
}  // namespace

// Mech3.exe 1.2 @ 0x0059b490
bool clip_line(float* a, float* b) { return clip_axis(a, b, 0) && clip_axis(a, b, 1); }

// Mech3.exe 1.2 @ 0x005a3b00
void Frame::draw() {
  if (!can_present()) return;
  erase();
  host().circle(x, y, radius, color, clipped ? &clip : nullptr);
}

// Mech3.exe 1.2 @ 0x005a3bc0
void FilledBox::draw() {
  PresentationScope presentation(presentation_visible);
  if (!can_present() && noise == 0.0f) return;
  erase();
  if (noise == 0.0f)  // 0x005d016c
    host().circle(x, y, radius, color, clipped ? &clip : nullptr);
  else
    host().circle_noisy(x, y, radius, color, noise, clipped ? &clip : nullptr);
}

// Mech3.exe 1.2 @ 0x005a68b0: with a clip, the line clipped in floats by the
// box the rect makes, then the box put back as the render's clip volume
// (0x0053ce20 saved it: the camera window and its near and far planes).
// FIX (bug): the exe saves only with a rect but always puts back, from its
// stack when there is none; here it saves either way (docs/fixes.md).
void Line2D::draw() {
  if (!can_present()) return;
  erase();
  if (!clipped) {
    if (dash > 1) host().line_dashed(x, y, end_x, end_y, color, dash);
    else host().line(x, y, end_x, end_y, color);
    return;
  }
  float a[3] = {static_cast<float>(x), static_cast<float>(y), 0.0f};
  float b[3] = {static_cast<float>(end_x), static_cast<float>(end_y), 0.0f};
  float saved_min[3], saved_max[3];
  host().render_clip(saved_min, saved_max);  // 0x0053ce20
  if (clip) {
    const float mn[3] = {static_cast<float>(clip->left), static_cast<float>(clip->top), 0.0f};
    const float mx[3] = {static_cast<float>(clip->right), static_cast<float>(clip->bottom), 0.0f};
    set_line_clip(mn, mx);
  }
  if (clip_line(a, b)) {
    const int32_t x0 = crt::msvc_ftol(a[0]), y0 = crt::msvc_ftol(a[1]);
    const int32_t x1 = crt::msvc_ftol(b[0]), y1 = crt::msvc_ftol(b[1]);
    if (dash > 1) host().line_dashed(x0, y0, x1, y1, color, dash);
    else host().line(x0, y0, x1, y1, color);
  }
  set_line_clip(saved_min, saved_max);
}

// Mech3.exe 1.2 @ 0x005a6b30
void ClipLine2D::draw() {
  PresentationScope presentation(presentation_visible);
  if (!can_present() && noise == 0.0f) return;
  if (noise == 0.0f) return Line2D::draw();  // 0x005d07dc
  erase();
  host().line_noisy(x, y, end_x, end_y, color, noise, clipped ? clip : nullptr);
}

// Mech3.exe 1.2 @ 0x005a3580: the segment p0..p1 within the circle across
// `r` (centre in its middle, radius half its width), into o0..o1; false
// when none of it is inside. An outside end moves to the first crossing in
// (0, 1) along it, truncated.
bool clip_round(const Rect& r, const int32_t* p0, const int32_t* p1, int32_t* o0, int32_t* o1) {
  const float cy = static_cast<float>(r.bottom + r.top) * 0.5f;  // 0x005cffe0
  const float cx = static_cast<float>(r.right + r.left) * 0.5f;
  const float rad = static_cast<float>(r.right - r.left) * 0.5f;
  const float px = static_cast<float>(p0[0]), py = static_cast<float>(p0[1]);
  const float X = px - cx, Y = py - cy;
  const float c = (X * X + Y * Y) - rad * rad;
  const float dx = static_cast<float>(p1[0] - p0[0]), dy = static_cast<float>(p1[1] - p0[1]);
  const float A = dx * dx + dy * dy;
  const float B = X * dx + Y * dy;
  const bool start_in = !(c > 0.0f);
  const bool end_in = !((c - B * -2.0f) + A > 0.0f);  // 0x005cffe8
  float t[2];
  int32_t roots = 0;
  if (A > 0.0f) {
    const float disc = B * B - c * A;
    if (!(disc < 0.0f)) {
      const float s = std::sqrt(disc);
      roots = 2;
      t[0] = (-B - s) / A;
      t[1] = (s - B) / A;
    }
  }
  int32_t i = 0;
  auto next = [&] {
    while (i < roots && !(t[i] > 0.0f && t[i] < 1.0f)) ++i;  // 0x005cffec
    return i < roots;
  };
  if (!start_in && !next()) return false;
  o0[0] = p0[0], o0[1] = p0[1];
  if (!start_in) {
    o0[0] = crt::msvc_ftol(static_cast<float>(o0[0]) + dx * t[i]);
    o0[1] = crt::msvc_ftol(dy * t[i] + static_cast<float>(o0[1]));
    ++i;
  }
  if (end_in) {
    o1[0] = p1[0], o1[1] = p1[1];
    return true;
  }
  if (!next()) return false;
  o1[0] = crt::msvc_ftol(static_cast<float>(p0[0]) + dx * t[i]);
  o1[1] = crt::msvc_ftol(dy * t[i] + static_cast<float>(p0[1]));
  return true;
}

// Mech3.exe 1.2 @ 0x005a6d10: drawn each frame while on; a blink shows on
// phase 1, a swap trades the images at each half period.
void FlashImage::update(float dt) {
  if (flags & kHidden) return;
  if (!on) return draw();
  timer = timer - dt;
  auto flip = [&] {
    phase = -phase;
    const float t = rate + timer;
    timer = t < 0.0f ? rate : t;
  };
  if (mode == 0) draw();  // then on into mode 1's blink: a missing break (twod.md)
  if (mode <= 1) {
    if (timer < 0.0f) {
      flip();
      dirty();
    }
    if (phase == 1) draw();
    return;
  }
  if (mode == 2 && timer < 0.0f) {
    flip();
    zimg::Texture* t = image;
    set_image(alt);
    alt = t;
    dirty();
  }
  draw();
}

// Mech3.exe 1.2 @ 0x005a6ca0: the winding from the first three points.
void Polygon::draw() {
  if (!can_present()) return;
  erase();
  if (count == 0) return;
  const float a = (pts[2][1] - pts[1][1]) * (pts[1][0] - pts[0][0]);
  const float b = (pts[1][1] - pts[0][1]) * (pts[2][0] - pts[1][0]);
  host().polygon(&pts[0][0], count, color, b > a ? 1 : 0);
}

}  // namespace ztwod
