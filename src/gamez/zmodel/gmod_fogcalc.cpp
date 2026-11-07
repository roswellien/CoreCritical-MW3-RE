#include "gamez/zmodel/gmod_fogcalc.h"

#include <cstring>

#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zrender/zrndr_fog.h"

namespace zmod {

namespace {
FogCalc g_calc;

// The exe's square root by halving the float's exponent bits (no Newton step).
float approx_sqrt(float x) {
  int32_t bits;
  std::memcpy(&bits, &x, 4);
  bits = (bits >> 1) + 0x1fc00000;
  float r;
  std::memcpy(&r, &bits, 4);
  return r;
}

// The x87 keeps sums of products unrounded until they are stored; double
// does the same for float inputs, so boundary compares go the same way.
double world_y(const float v[3]) {
  const float(&m)[4][3] = g_calc.camera_to_world;
  return double{m[2][1]} * v[2] + double{v[1]} * m[1][1] + double{v[0]} * m[0][1] + m[3][1];
}

float horizontal_sq(const float v[3]) {
  return static_cast<float>(double{v[0]} * v[0] + double{v[2]} * v[2]);
}

// The x87 compares treat NaN as "below", so these keep that sense.
float clamp01(double f) {
  if (f > 1.0) return 1.0f;
  if (!(f >= 0.0)) return 0.0f;
  return static_cast<float>(f);
}

constexpr double kVisible = 0.003921569;  // 0x005ce548, 1/255
}  // namespace

FogCalc& fog_calc() { return g_calc; }

// Mech3.exe 1.2 @ 0x00555640
float fog_sphere(const float centre[3], float radius) {
  const Fog& f = fog();
  const float s = approx_sqrt(horizontal_sq(centre));
  const float farthest = s + radius;
  if (!(farthest > f.near_range)) return 0.0f;
  const double y_exact = world_y(centre);
  const float y = static_cast<float>(y_exact);
  const float bottom = static_cast<float>(y_exact - radius);
  if (bottom >= f.alt_high) return 0.0f;
  const double low = bottom >= f.alt_low ? bottom : f.alt_low;  // the sphere's lowest point in the band
  double fog_amount;
  if (!(double{s} - radius >= f.far_range)) {
    fog_amount = (double{farthest > f.far_range ? f.far_range : farthest} - f.near_range) * f.inv_range;
    if (double{y} + radius > f.alt_low) fog_amount *= (f.alt_high - low) * f.inv_alt;
  } else {
    if (!(double{y} + radius > f.alt_low)) return 1.0f;
    fog_amount = (f.alt_high - low) * f.inv_alt;
  }
  return clamp01(fog_amount);
}

float fog_at(const float v[3]) {
  const Fog& f = fog();
  const float d2 = horizontal_sq(v);
  if (!(d2 > f.near_sq)) return 0.0f;
  const double y_exact = world_y(v);
  const float y = static_cast<float>(y_exact);
  if (y_exact >= f.alt_high) return 0.0f;
  if (!(d2 >= f.far_sq)) {
    const double g = (double{approx_sqrt(d2)} - f.near_range) * f.inv_range;
    float out = static_cast<float>(g);
    if (y > f.alt_low) out = static_cast<float>((double{f.alt_high} - y) * f.inv_alt * g);
    return clamp01(out);
  }
  if (!(y > f.alt_low)) return 1.0f;
  return clamp01((double{f.alt_high} - y) * f.inv_alt);
}

// Mech3.exe 1.2 @ 0x00555a70. Walks the vertices last to first.
int32_t fog_vertices(int32_t count) {
  const Fog& f = fog();
  int32_t any = 0;
  for (int32_t i = count - 1; i >= 0; --i) {
    g_calc.factor[i] = fog_at(g_calc.verts[i]);
    if (g_calc.factor[i] > kVisible) any = 1;
  }
  if (!any) return 0;
  // The device colour push, without the device-up test of zrndr::device_fog_color.
  zrndr::DeviceFog& d = zrndr::device_fog();
  for (int i = 0; i < 3; ++i) d.color[i] = f.color[i] * 255.0f;
  if (d.sent[0] != d.color[0] || d.sent[1] != d.color[1] || d.sent[2] != d.color[2]) {
    for (int i = 0; i < 3; ++i) d.sent[i] = d.color[i];
    if (d.send) d.send();
  }
  return 1;
}

}  // namespace zmod
