#include "gamez/zclass/class.h"
#include "gamez/zmodel/gmod_light.h"

#include <cmath>
#include <cstring>

#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zvideo/zvid_ddd3d.h"

#pragma STDC FP_CONTRACT OFF

namespace zmod {

namespace {
LightList g_list;
Clouds g_clouds;
Lighting g_lighting;
LightHooks g_hooks;

const char kLightFile[] = "C:\\zipper\\GameZMech3\\zModel\\gmod_light.c";
const char kClassFile[] = "C:\\zipper\\GameZMech3\\zClass\\Light.c";

constexpr double kVisible = 0.003921569;  // 0x005ce4a0, 1/255 as a double
constexpr float kTwoPi = 6.2831855f;      // 0x005ce460 / 0x005ce478

// Light data (docs/drafts/lighting.md has the layout).
enum : int {
  kAngles = 0x18, kWorldPos = 0x24, kPosCopy = 0x60, kCamPos = 0x78, kWorldDir = 0x84,
  kCamDir = 0x90, kDiffuse = 0x9c, kAmbient = 0xa0, kColor = 0xa4, kFlags = 0xb0,
  kNear = 0xb4, kFar = 0xb8, kNearSq = 0xbc, kFarSq = 0xc0, kInvRange = 0xc4,
};
enum : uint32_t {
  kRecalc = 0x1, kRanged = 0x2, kSpot = 0x4, kDirected = 0x8, kHasPos = 0x10,
  kSaturated = 0x20, kNoShadowed = 0x40, kBaked = 0x100, kOn = 0x200,
};

float lf(const zcls::Light* l, int off) { return l->get<float>(off); }
uint32_t lflags(const zcls::Light* l) { return l->get<uint32_t>(kFlags); }
const float* lvec(zcls::Light* l, int off) { return l->floats(off); }

// The exe's square root by halving the float's exponent bits (no Newton step).
float approx_sqrt(float x) {
  int32_t bits;
  std::memcpy(&bits, &x, 4);
  bits = (bits >> 1) + 0x1fc00000;
  float r;
  std::memcpy(&r, &bits, 4);
  return r;
}

// The x87 dot product order: (x*x' + y*y') + z*z'.
float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// x87 "!=" with NaN counted as equal (C3 set when unordered).
bool differs(float a, float b) { return a < b || a > b; }

// Mech3.exe 1.2 @ 0x0051e110. dst = src / len, a copy when len is 0.
void scale_inverse(const float src[3], float dst[3], float len) {
  if (len == 0.0f) {
    if (dst != src) std::memcpy(dst, src, 12);
    return;
  }
  const float inv = 1.0f / len;
  dst[0] = inv * src[0], dst[1] = src[1] * inv, dst[2] = src[2] * inv;
}

// The cloud table at 0x005ce260: 128 steps of a sine, built from its first
// quarter (the table is exactly symmetric).
float cloud_table(uint32_t i) {
  static const uint32_t kQuarter[33] = {
      0x00000000, 0x3d48fb30, 0x3dc8bd36, 0x3e164083, 0x3e47c5c2, 0x3e78cfcc, 0x3e94a031,
      0x3eac7cd4, 0x3ec3ef15, 0x3edae880, 0x3ef15aea, 0x3f039c3d, 0x3f0e39da, 0x3f187fc0,
      0x3f226799, 0x3f2beb4a, 0x3f3504f3, 0x3f3daef9, 0x3f45e403, 0x3f4d9f02, 0x3f54db31,
      0x3f5b941a, 0x3f61c598, 0x3f676bd8, 0x3f6c835e, 0x3f710908, 0x3f74fa0b, 0x3f7853f8,
      0x3f7b14be, 0x3f7d3aac, 0x3f7ec46d, 0x3f7fb10f, 0x3f800000};
  i &= 0x7f;
  const uint32_t q = i & 0x3f;
  uint32_t bits = kQuarter[q <= 32 ? q : 64 - q];
  if (i >= 64) bits ^= 0x80000000u * (bits != 0);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

// The table index the exe takes with the 1.5 * 2^52 rounding trick: the low
// word of (a * 128/2pi + 1.5 * 2^52) as a double, a * 128/2pi rounded on a
// 53-bit FPU (zfix fpu53, as in play). At 24 bits the sum keeps no low bits:
// the index is always 0 and the pattern is flat (render-lighting.md, Clouds).
uint32_t cloud_index(float a) {
  const float scaled = a * 20.371832f;          // 0x005ce480, 128 / 2pi
  if (zfix::options().fpu53) return static_cast<uint32_t>(static_cast<int64_t>(std::nearbyint(double{scaled})));
  const float sum = scaled - -6755399441055744.0f;  // 0x005ce488
  const double d = sum;
  uint64_t bits;
  std::memcpy(&bits, &d, 8);
  return static_cast<uint32_t>(bits);
}

float cloud_value(float u, float v) {
  return cloud_table(cloud_index(v)) * cloud_table(cloud_index(u)) * g_clouds.amplitude + g_clouds.base;
}

// The pattern at a camera-space point: its world x and z through the
// camera-to-world matrix, less the camera offset.
float cloud_at_camera(const float p[3]) {
  const float* m = zmth::view().camera.m;
  const float* off = g_lighting.camera_offset;
  const float wx = m[0] * p[0] + m[6] * p[2] + m[3] * p[1] + m[9];
  const float wz = m[2] * p[0] + m[8] * p[2] + m[5] * p[1] + m[11];
  const float u = (wx - off[0]) * g_clouds.scale + g_clouds.u;
  const float v = (wz - off[2]) * g_clouds.scale + g_clouds.v;
  return cloud_value(u, v);
}

// Falloff over the light's range; NaN counts as inside both bounds.
float falloff(const zcls::Light* l, float dist) {
  if (!(dist < lf(l, kFar))) return 0.0f;
  if (!(dist > lf(l, kNear))) return 1.0f;
  return (lf(l, kFar) - dist) * lf(l, kInvRange);
}

// The default for LightHooks::node_to_world: the classes a light's chain
// holds in the worlds (light 9 itself, world 2 above it). 0x004ed280 in full
// also handles cameras, object3d (cached matrices) and animation nodes.
int light_chain_to_world(zcls::Node* node, int) {
  zcls::Node* chain[16];
  int n = 0;
  for (zcls::Node* p = node; p && n < 16; p = p->parent_count() == 1 ? p->parents[0] : nullptr) {
    if (p->parent_count() >= 2) return 1;
    chain[n++] = p;
  }
  static const float kOne[3] = {1, 1, 1};  // 0x005e9e20
  for (int i = n - 1; i >= 0; --i) {
    zcls::Node* p = chain[i];
    p->set_flags(p->flags() & ~0x2000000u);
    const int cls = p->cls();
    if (cls == 9) {
      auto* d = static_cast<zcls::Light*>(p->data);
      zmth::euler(d->floats(0), d->floats(0xc), kOne);
    } else if (cls == 10) {
      auto* d = static_cast<zcls::Sound*>(p->data);
      static const float kZero[3] = {};  // 0x0060b940
      zmth::euler(kZero, d->floats(0x30), kOne);
    }
  }
  return 0;
}
}  // namespace

LightList& light_list() { return g_list; }
Clouds& clouds() { return g_clouds; }
Lighting& lighting() { return g_lighting; }
LightHooks& light_hooks() { return g_hooks; }
void use_device_light_output() {
  // FIX (bug): the whole chain (0x004ed280), or a light under an object3d (a mech's headlight, a world's lamp) sits
  // at its parent's origin: the simple chain above stands in only for the tests' worlds.
  g_hooks.node_to_world = zcls::node_to_world;
  g_hooks.point_color = zvid::set_point_light_color;
  g_hooks.mix_fog = zrndr::set_mix_fog_color;
  g_hooks.send_mix_fog = zvid::send_mix_fog;
}

// Mech3.exe 1.2 @ 0x00543b60
void build_light_list(zcls::Node** nodes, zcls::Light** data, int32_t count) {
  LightList& L = g_list;
  L.nodes = nodes, L.data = data, L.in_count = count;
  L.count = 0, L.directed = -1;
  for (int32_t i = 0; i < L.in_count; ++i) {
    if (!(L.nodes[i]->flags() & 4)) continue;
    if (L.count == LightList::kMax) {
      zerr::report(0x200, kLightFile, 0x11b, "Not enough MAX_LIGHTS: 128; requesting more.");
      break;
    }
    L.list[L.count].data = L.data[i];
    L.list[L.count].node = L.nodes[i];
    if (lflags(L.data[i]) & kDirected) L.directed = L.count;
    ++L.count;
  }
  L.any = L.count > 0;
  if (zrndr::device_fog().active) return;
  // The software renderer's colours.
  const Fog& f = fog();
  std::memcpy(L.sw_fog, f.color, 12);
  L.sw_one = 1.0f;
  if (L.directed < 0) {
    L.sw_dark = 0.0f;
    std::memcpy(L.sw_color, f.color, 12);
  } else {
    zcls::Light* d = L.list[L.directed].data;
    std::memcpy(L.sw_color, lvec(d, kColor), 12);
    L.sw_dark = 1.0f - lf(d, kAmbient);
  }
  L.sw_one2 = 1.0f;
  L.sw_zero = 0.0f;
  if (L.directed >= 0) {
    zcls::Light* d = L.list[L.directed].data;
    std::memcpy(L.sw_color3, lvec(d, kColor), 12);
    std::memcpy(L.sw_color2, lvec(d, kColor), 12);
  } else {
    for (int k = 0; k < 3; ++k) L.sw_color2[k] = L.sw_color3[k] = 0.0f;
  }
}

// Mech3.exe 1.2 @ 0x004f2f30
int32_t world_build_lights(zcls::Node* world, int32_t none) {
  auto* w = static_cast<zcls::World*>(world->data);
  build_light_list(w->lights, reinterpret_cast<zcls::Light**>(w->light_data),
                   none ? 0 : w->get<int32_t>(0xa0));
  return 0;
}

// Mech3.exe 1.2 @ 0x004f2f60
int32_t world_update_lights(zcls::Node* world) {
  auto* w = static_cast<zcls::World*>(world->data);
  for (int32_t i = 0; i < w->get<int32_t>(0xa0); ++i) light_update(w->lights[i]);
  return 0;
}

// Mech3.exe 1.2 @ 0x004fb0c0. The node's world matrix applied to (0,0,0)
// and (0,0,-1): the position (+0x24), the angles toward the second point
// (+0x18, directed and spot lights) and the matrix's -z axis (+0x84).
void light_world_pose(zcls::Node* node, zcls::Light* d) {
  zmth::Mat34 m;
  const zmth::Vec3 pts[2] = {{0, 0, 0}, {0, 0, -1}};
  zmth::Vec3 out[2];
  zmth::push(&m);
  zmth::load_identity();
  (g_hooks.node_to_world ? g_hooks.node_to_world : light_chain_to_world)(node, 1);
  if (!(lflags(d) & (kSpot | kDirected))) {
    zmth::transform_points_to(pts, out, 1);
  } else {
    zmth::transform_points_to(pts, out, 2);
    // 0x00523ac0: pitch and yaw from out[0] toward out[1], roll 0.
    const float dx = out[0].x - out[1].x;
    const float dy = out[1].y - out[0].y;
    const float dz = out[0].z - out[1].z;
    const float yaw = static_cast<float>(std::atan2(double{dx}, double{dz}));
    const float flat = std::sqrt(dx * dx + dz * dz);
    const float pitch = static_cast<float>(std::atan2(double{dy}, double{flat}));
    d->put(kAngles, pitch), d->put(kAngles + 4, yaw), d->put(kAngles + 8, 0.0f);
  }
  d->put(kWorldPos, out[0].x), d->put(kWorldPos + 4, out[0].y), d->put(kWorldPos + 8, out[0].z);
  d->put(kWorldDir, -m.m[6]), d->put(kWorldDir + 4, -m.m[7]), d->put(kWorldDir + 8, -m.m[8]);
  zmth::pop();
}

// Mech3.exe 1.2 @ 0x004fb310
int32_t light_update(zcls::Node* node) {
  if (!node) return zerr::report(0x400, kClassFile, 0x40d, "Null node pointer."), 5;
  if (!(node->flags() & 4)) return 0;
  auto* d = static_cast<zcls::Light*>(node->data);
  if (!d) return zerr::report(0x400, kClassFile, 0x413, "Null class data pointer"), 5;
  light_world_pose(node, d);
  zmth::Mat34 m;
  zmth::push_copy(&m);
  zmth::load_view();
  if (lflags(d) & (kSpot | kDirected)) {
    zmth::rotate_points_to(reinterpret_cast<const zmth::Vec3*>(d->floats(kWorldDir)),
                           reinterpret_cast<zmth::Vec3*>(d->floats(kCamDir)), 1);
    float* c = d->floats(kCamDir);
    c[0] = -c[0], c[1] = -c[1], c[2] = -c[2];
  }
  if (lflags(d) & kHasPos) {
    std::memcpy(d->floats(kPosCopy), d->floats(kWorldPos), 12);
    zmth::transform_points_to(reinterpret_cast<const zmth::Vec3*>(d->floats(kPosCopy)),
                              reinterpret_cast<zmth::Vec3*>(d->floats(kCamPos)), 1);
  }
  zmth::pop();
  d->put<uint32_t>(kFlags, lflags(d) & ~kRecalc);
  return 0;
}

// Mech3.exe 1.2 @ 0x00543d90
int32_t lights_reaching(const float centre[3], float radius) {
  LightList& L = g_list;
  float dist[LightList::kMax];
  int32_t reached = 0, partial_directed = 0;
  const bool d3d = zrndr::device_fog().active != 0;
  for (int32_t i = 0; i < L.count; ++i) {
    LightEntry& e = L.list[i];
    const uint32_t fl = lflags(e.data);
    if ((d3d && (fl & kDirected)) || !(fl & kRanged)) {
      e.full = 1, e.reach = 1;
      ++reached;
      continue;
    }
    e.full = 0, e.reach = 0;
    if (!(fl & kOn)) continue;
    if (fl & kDirected) {
      L.directed = i;
      dist[i] = centre[2];
    } else {
      const float* p = lvec(e.data, kCamPos);
      const float v[3] = {p[0] - centre[0], p[1] - centre[1], p[2] - centre[2]};
      const float d2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
      dist[i] = d2 == 0.0f ? d2 : approx_sqrt(d2);
    }
    const float d = dist[i];
    const float nearest = d - radius, farthest = radius + d;
    dist[i] = nearest;
    if (!(nearest < lf(e.data, kFar)) && !(fl & kDirected)) continue;
    e.reach = 1;
    if (farthest < lf(e.data, kNear)) {
      e.full = 1;
    } else if (fl & kDirected) {
      partial_directed = 1;
    }
    ++reached;
  }
  if (reached == 0) return 0;
  for (int32_t i = 0; i < L.count; ++i) {
    LightEntry& e = L.list[i];
    if (!e.reach) continue;
    const uint32_t fl = lflags(e.data);
    if (partial_directed && g_lighting.single && !(fl & kDirected)) {
      e.reach = 0;
      --reached;
      continue;
    }
    float v = e.full ? 1.0f : falloff(e.data, dist[i]);
    const float cap = lf(e.data, kDiffuse) + lf(e.data, kAmbient);
    if (cap < v) v = cap;
    if (v > 1.0f) v = 1.0f;
    else if (v < 0.0f) v = 0.0f;
    g_lighting.rec[i][(fl & kDirected) ? 0 : 1] = v;
  }
  return reached;
}

// Mech3.exe 1.2 @ 0x0053daa0
uint32_t model_needs(uint32_t model_flags, const float centre[3], float radius) {
  float c[3];
  zmth::transform_points_to(reinterpret_cast<const zmth::Vec3*>(centre), reinterpret_cast<zmth::Vec3*>(c), 1);
  uint32_t needs = 0;
  int32_t lights = 0;
  if (fog().on && (model_flags & 2) && double{fog_sphere(c, radius)} > 0.003921569) needs = 1;
  if ((*g_lighting.mask & 1) && g_list.any && (model_flags & 1)) {
    lights = lights_reaching(c, radius);
    if (lights > 0) needs |= 2;
  }
  if (g_lighting.tint_on) ++lights, needs |= 4;
  if (lights <= 1) return needs;
  // Two or more colour sources: their weighted mean becomes the mixed fog colour.
  float r = 0, g = 0, b = 0, w = 0;
  if (needs & 2) {
    for (int32_t i = 0; i < g_list.count; ++i) {
      const LightEntry& e = g_list.list[i];
      if (!(e.reach && !g_lighting.single) && (lflags(e.data) & kDirected)) continue;
      const float k = g_lighting.rec[i][1];  // +4 for every light, the directed slot is +0
      r = lf(e.data, kColor) * k + r;
      g = lf(e.data, kColor + 4) * k + g;
      b = lf(e.data, kColor + 8) * k + b;
      w = w + k;
    }
  }
  if (needs & 4) {
    r = r + g_lighting.tint[0], g = g + g_lighting.tint[1], b = b + g_lighting.tint[2];
    w = w + g_lighting.tint_weight;
  }
  const float inv = 1.0f / w;
  float mix[3] = {inv * r, inv * g, inv * b};
  if (g_hooks.mix_fog) g_hooks.mix_fog(mix);
  return needs;
}

// Mech3.exe 1.2 @ 0x00545720. Vertices are walked last to first, as the
// original does. See docs/drafts/lighting.md for the flag bits.
int32_t shade_polygon(const float* baked, float normal[3], int32_t count, uint32_t* draw_flags,
                      uint32_t* vary_flags, int32_t shadowed, uint32_t needs) {
  Lighting& G = g_lighting;
  float (*rec)[8] = G.rec;
  const float (*cam)[3] = G.cam;
  float point[64];  // [ebp-0x1a0], per vertex
  const uint32_t no_sun = *vary_flags & 4;  // model flag 0x20
  uint32_t st = 0;  // [ebp-4]: 1 lit, 2 saturated, 4 normal normalised, 8 vertex 0 in range, 0x10 vertex normals
  for (int32_t i = count - 1; i >= 0; --i) rec[i][0] = 0, rec[i][1] = 0, point[i] = 0;
  if (!(*draw_flags & 1))
    for (int32_t i = count - 1; i >= 0; --i) rec[i][2] = 0;
  int32_t directed_lit = 0, point_lit = 0, last_point = -1;
  if (needs & 2) {
    if (baked) {
      // Baked vertex light, in file order against the reversed vertices.
      float sum = 0.0f;
      for (int32_t j = 0, i = count - 1; j < count; ++j, --i) {
        float t;
        if (no_sun) {
          t = 1.0f - baked[j];
        } else {
          const float c = g_clouds.on ? cloud_at_camera(cam[i]) : 1.0f;
          t = 1.0f - c * baked[j];
        }
        rec[i][0] = t + rec[i][0];
        sum = sum + t;
      }
      if (double{sum} > kVisible) directed_lit = 1, st = 1;
    }
    for (int32_t li = 0; li < g_list.count; ++li) {
      LightEntry& e = g_list.list[li];
      if (!e.reach) continue;
      zcls::Light* d = e.data;
      const uint32_t fl = lflags(d);
      if ((fl & kBaked) && baked) continue;
      if ((fl & kNoShadowed) && shadowed) continue;
      const float* L = lvec(d, kCamDir);
      const float diffuse = lf(d, kDiffuse), ambient = lf(d, kAmbient);
      float lit = 0.0f;  // [ebp-8]; unset on the vertex-normal path, which sets it per vertex
      if (fl & kDirected) {
        st &= ~0x10u;
        float sum = 0.0f;
        if (no_sun) {
          lit = ambient;
          if (lit > 1.0f) lit = 1.0f;
        } else if (G.vertex_normals) {
          st |= 0x10;
        } else {
          if (!(st & 4)) normalize(normal), st |= 4;
          const float nl = dot(normal, L);
          if (!(nl > 0.0f)) {
            lit = ambient;
          } else {
            lit = diffuse * nl + ambient;
            if (lit > 1.0f) lit = 1.0f;
          }
        }
        if (e.full) st |= 1;
        for (int32_t i = count - 1; i >= 0; --i) {
          st |= 1;
          if (st & 0x10) {
            const float nl = dot(G.normals[i], L);
            if (!(nl > 0.0f)) {
              lit = ambient;
            } else {
              lit = diffuse * nl + ambient;
              if (lit > 1.0f) lit = 1.0f;
            }
          }
          float I;
          if (no_sun) {
            I = lit;
          } else {
            const float c = g_clouds.on ? cloud_at_camera(cam[i]) : 1.0f;
            I = c * lit;
          }
          const float t = 1.0f - I;
          rec[i][0] = t + rec[i][0];
          sum = sum + t;
        }
        if (double{sum} > kVisible) ++directed_lit;
        continue;
      }
      // Point lights.
      float total = 0.0f;  // [ebp-0x3c]
      if ((fl & kSaturated) && (*draw_flags & 1)) continue;
      float factor, spot;  // st0 and [ebp-0x34]
      if (fl & kSpot) {
        if (!(st & 4)) normalize(normal), st |= 4;
        const float nl = dot(normal, L);
        spot = nl;
        if (double{nl} > kVisible) {
          // The vector toward the light is not set yet here: the slot holds
          // the previous light's last vertex, or stack leftovers (FIX: none, kept).
          factor = dot(G.stale, L);
          if (double{factor} < kVisible) factor = 0.0f;
        } else {
          factor = 0.0f, spot = 0.0f;
        }
      } else {
        factor = 1.0f, spot = 1.0f;
      }
      if (!(spot > 0.0f) && !(ambient > 0.0f)) continue;
      if (!(factor > 0.0f)) {
        lit = ambient;
      } else {
        lit = diffuse * factor + ambient;
        if (lit > 1.0f) lit = 1.0f;
      }
      const float* P = lvec(d, kCamPos);
      float dist = 0.0f;  // [ebp-0x10]
      // Distance from the light to vertex i; false beyond the far range. Within
      // near^2 the squared distance is kept and compared as a distance.
      auto range = [&](int32_t i) {
        float* v = G.stale;
        v[0] = P[0] - cam[i][0], v[1] = P[1] - cam[i][1], v[2] = P[2] - cam[i][2];
        const float d2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        dist = d2;
        if (!(d2 < lf(d, kFarSq))) return false;
        if (fl & kSpot) {
          dist = approx_sqrt(d2);
          scale_inverse(v, v, dist);
        } else if (!(d2 < lf(d, kNearSq))) {
          dist = approx_sqrt(d2);
        }
        return true;
      };
      if (!e.full) {
        st &= ~8u;
        if (range(0)) st |= 9;
      } else {
        st |= 9;
      }
      if (st & 8) {
        float val = lit;
        if (!e.full) val = falloff(d, dist) * lit;
        if (fl & kSaturated) {
          rec[0][2] = val + rec[0][2];
          st |= 2;
        } else {
          point[0] = point[0] + val;
        }
        total = val;
      }
      for (int32_t i = count - 1; i >= 1; --i) {
        if (!e.full) {
          if (!range(i)) continue;
          st |= 1;
        }
        float f;
        if (!(spot > 0.0f)) {
          f = 0.0f;
        } else {
          f = spot;
          if (fl & kSpot) {
            float* v = G.stale;
            if (e.full) v[0] = P[0] - cam[i][0], v[1] = P[1] - cam[i][1], v[2] = P[2] - cam[i][2];
            f = dot(v, L);
            if (double{f} < kVisible) f = 0.0f;
          }
        }
        if (!(double{f} > kVisible) && !(ambient > 0.0f)) continue;
        if (fl & kSpot) {
          if (!(f > 0.0f)) {
            lit = ambient;
          } else {
            lit = diffuse * f + ambient;
            if (lit > 1.0f) lit = 1.0f;
          }
        }
        float val = lit;
        if (!e.full) val = falloff(d, dist) * lit;
        if (fl & kSaturated) {
          rec[i][2] = val + rec[i][2];
          st |= 2;
        } else {
          point[i] = val + point[i];
        }
        total = total + val;
      }
      if (double{total} > kVisible) ++point_lit, last_point = li;
    }
  }
  if (needs & 4) {
    st |= 1;
    for (int32_t i = count - 1; i >= 0; --i) rec[i][2] = G.tint_weight + rec[i][2];
    st |= 2;
  }
  const int32_t points = point_lit + ((needs & 4) ? 1 : 0);
  if (!(st & 1)) return 0;
  if (directed_lit > 0) {
    if (rec[0][0] > 1.0f) rec[0][0] = 1.0f;
    else if (rec[0][0] < 0.0f) rec[0][0] = 0.0f;
  } else if (points == 0) {
    return 0;
  }
  // A point light that differs across the polygon makes the light per vertex.
  if (points != 0) {
    for (int32_t i = count - 1;; --i) {
      if (double{std::fabs(point[i] - point[0])} > kVisible) {
        *vary_flags |= 1;
        break;
      }
      if (i <= 1) break;
    }
  }
  int32_t result = 0;
  for (int32_t i = count - 1; i >= 0; --i) {
    float& dk = rec[i][0];
    if (dk > 1.0f) dk = 1.0f;
    else if (dk < 0.0f) dk = 0.0f;
    if (double{dk} > kVisible) result = 1;
  }
  if (result) *vary_flags |= 1;
  for (int32_t i = count - 1; i >= 0; --i) {
    float& p = point[i];
    if (p > 1.0f) p = 1.0f;
    else if (p < 0.0f) p = 0.0f;
    if (double{p} > kVisible) {
      rec[i][1] = p + rec[i][1];
      result = 1;
    }
  }
  if (st & 2) {
    st &= ~2u;
    for (int32_t i = count - 1; i >= 0; --i) {
      float& s = rec[i][2];
      if (s > 1.0f) s = 1.0f;
      else if (s < 0.0f) s = 0.0f;
      if (double{s} > kVisible) st |= 2;
    }
    if (!(*vary_flags & 3)) {
      for (int32_t i = count - 1;; --i) {
        if (double{std::fabs(rec[i][2] - rec[0][2])} > kVisible) {
          *vary_flags |= 2;
          break;
        }
        if (i <= 1) break;
      }
    }
    if (st & 2) {
      result = 1;
      const uint32_t was = *draw_flags;
      *draw_flags = was | 9;
      const float* color = last_point >= 0 ? lvec(g_list.list[last_point].data, kColor) : G.tint;
      if (was & 1) {
        // Fogged: the saturated colour goes to the point-light slot and the
        // device fog keeps the mix model_needs made.
        if (g_hooks.point_color) g_hooks.point_color(color);
        if (g_hooks.send_mix_fog) g_hooks.send_mix_fog();
      } else {
        zrndr::DeviceFog& df = zrndr::device_fog();
        for (int k = 0; k < 3; ++k) df.color[k] = color[k] * 255.0f;
        if (differs(df.sent[0], df.color[0]) || differs(df.sent[1], df.color[1]) ||
            differs(df.sent[2], df.color[2])) {
          for (int k = 0; k < 3; ++k) df.sent[k] = df.color[k];
          if (df.send) df.send();
        }
      }
    }
  }
  if (last_point >= 0 && result) {
    *draw_flags |= 4;
    if (g_hooks.point_color) g_hooks.point_color(lvec(g_list.list[last_point].data, kColor));
  }
  return result;
}

// Mech3.exe 1.2 @ 0x00543910
void clouds_set_on(int32_t on) { g_clouds.on = on; }

// Mech3.exe 1.2 @ 0x00543920
void clouds_set_period(float period) { g_clouds.speed = kTwoPi / period; }

// Mech3.exe 1.2 @ 0x00543940
void clouds_set_wavelength(float wavelength) { g_clouds.scale = kTwoPi / wavelength; }

// Mech3.exe 1.2 @ 0x00543960. The amplitude is kept within base and 1 - base.
void clouds_set_level(float base, float amplitude) {
  if (base > 1.0f) base = 1.0f;
  else if (base < 0.0f) base = 0.0f;
  float a = amplitude;
  if (a < 0.0f) a = -a;
  if (a > base) a = base;
  if (a + base > 1.0f) a = 1.0f - base;
  g_clouds.amplitude = a;
  g_clouds.base = base;
}

// Mech3.exe 1.2 @ 0x005439f0
void clouds_set_heading(float radians) {
  g_clouds.heading_sin = static_cast<float>(std::sin(double{radians}));
  g_clouds.heading_cos = static_cast<float>(std::cos(double{radians}));
}

// Mech3.exe 1.2 @ 0x00543a50. The phases move by dir * dt * speed, wrapped to 2 pi.
void clouds_advance(const float dir[2], float dt) {
  g_clouds.u = dir[0] * dt * g_clouds.speed + g_clouds.u;
  g_clouds.v = dir[1] * dt * g_clouds.speed + g_clouds.v;
  g_clouds.u = static_cast<float>(std::fmod(double{g_clouds.u}, double{kTwoPi}));
  g_clouds.v = static_cast<float>(std::fmod(double{g_clouds.v}, double{kTwoPi}));
}

// Mech3.exe 1.2 @ 0x00543ac0. 1 when the pattern is off.
float clouds_at(float x, float z) {
  if (!g_clouds.on) return 1.0f;
  return cloud_value(x * g_clouds.scale + g_clouds.u, z * g_clouds.scale + g_clouds.v);
}

// --- Projected lights (docs/render-shadows.md) ---

namespace {
ProjectedLights g_proj;

// Mech3.exe 1.2 @ 0x0051dfa0 (ECX a, EDX b): |a - b|^2, the x87's order.
float dist2(const float a[3], const float b[3]) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  const float xx = dx * dx, yy = dy * dy, zz = dz * dz;
  const float s = xx + yy;
  return s + zz;
}

// x87 "below" (fcomp; test ah, 1): also taken on NaN.
bool below(float a, float b) { return !(a >= b); }

// The lights with flags 0x10 and 0x40 among those reaching this model.
int32_t list_projected() {
  ProjectedLights& p = g_proj;
  p.light_count = 0;
  for (int32_t i = 0; i < g_list.count; ++i) {
    const LightEntry& e = g_list.list[i];
    if (!e.reach) continue;
    const uint32_t f = lflags(e.data);
    if ((f & kHasPos) && (f & kNoShadowed)) p.lights[p.light_count++] = e.data;
  }
  return p.light_count;
}

// Every vertex unprojected from the TL buffer (0x00523590), the centre at
// [count] and the radius around it (the approximate square root).
float polygon_sphere(int32_t count) {
  ProjectedLights& p = g_proj;
  p.vertex_count = count;
  const TLVertex* tl = clip2d().tl;
  for (int32_t i = 0; i < count; ++i) {
    const zmth::Vec3 s{tl[i].sx, tl[i].sy, tl[i].sz};
    zmth::unproject_d3d(&s, reinterpret_cast<zmth::Vec3*>(p.vertices[i].pos), 1);
  }
  float* c = p.vertices[count].pos;
  for (int k = 0; k < 3; ++k) c[k] = p.vertices[0].pos[k] + p.vertices[1].pos[k];
  for (int32_t i = 2; i < count; ++i)
    for (int k = 0; k < 3; ++k) c[k] = c[k] + p.vertices[i].pos[k];
  const float r = 1.0f / static_cast<float>(count);
  for (int k = 0; k < 3; ++k) c[k] = r * c[k];
  float most = 0.0f;
  for (int32_t i = 0; i < count; ++i) {
    const float d = dist2(p.vertices[i].pos, c);
    if (d > most) most = d;
  }
  return approx_sqrt(most);
}

// x87 "d2 < r * r" with the product rounded to float; unordered counts as below.
bool within(float d2, float r) {
  const float rr = r * r;
  return below(d2, rr);
}
}  // namespace

ProjectedLights& projected_lights() { return g_proj; }

// Mech3.exe 1.2 @ 0x00547540
int32_t projected_reach(int32_t count) {
  if (list_projected() == 0) return 0;
  const float radius = polygon_sphere(count);
  const float* c = g_proj.vertices[count].pos;
  for (int32_t i = 0; i < g_proj.light_count; ++i) {
    zcls::Light* l = g_proj.lights[i];
    if (within(dist2(lvec(l, kCamPos), c), lf(l, kFar) + radius)) return 1;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x00547060. lit[] = light[] plus each projected light's
// colour by (diffuse + ambient), with a squared falloff from near to far.
void projected_light_vertex(int32_t k) {
  ProjVertex& v = g_proj.vertices[k];
  std::memcpy(v.lit, v.light, 12);
  for (int32_t i = 0; i < g_proj.light_count; ++i) {
    zcls::Light* l = g_proj.lights[i];
    const float d2 = dist2(lvec(l, kCamPos), v.pos);
    if (!below(d2, lf(l, kFarSq))) continue;
    float w = lf(l, kDiffuse) + lf(l, kAmbient);
    if (d2 > lf(l, kNearSq)) {
      const float d = lf(l, kFar) - approx_sqrt(d2);
      const float f = d * lf(l, kInvRange);
      const float ff = f * f;
      w = ff * w;
    }
    const float* col = lvec(l, kColor);
    for (int c = 0; c < 3; ++c) {
      const float a = col[c] * w;
      v.lit[c] = a + v.lit[c];
    }
  }
}

// Mech3.exe 1.2 @ 0x00547160 (ECX t, EDX edge). A new vertex at the middle of
// edge v[edge]..v[edge + 1]: t keeps the half before it, a new triangle the
// rest, and the triangle across the edge is split the same way.
void projected_split(int32_t t, int32_t edge) {
  ProjectedLights& p = g_proj;
  ProjTriangle* tri = p.triangles;
  const int32_t next = edge == 2 ? 0 : edge + 1;
  const int32_t prev = edge == 0 ? 2 : edge - 1;
  ProjTriangle& T = tri[t];
  const int32_t n = p.vertex_count;
  ProjVertex& m = p.vertices[n];
  const ProjVertex& a = p.vertices[T.v[next]];
  const ProjVertex& b = p.vertices[T.v[edge]];
  auto mid = [](float x, float y) {
    const float s = x + y;
    return s * 0.5f;  // 0x005ce4b0
  };
  for (int k = 0; k < 3; ++k) m.pos[k] = mid(b.pos[k], a.pos[k]);
  for (int32_t s = 0; s <= clip2d().shadow.count; ++s)
    for (int k = 0; k < 2; ++k) m.uv[s][k] = mid(a.uv[s][k], b.uv[s][k]);
  m.fog = mid(a.fog, b.fog);
  for (int k = 0; k < 3; ++k) m.light[k] = mid(a.light[k], b.light[k]);
  for (int k = 0; k < 3; ++k) m.color[k] = mid(a.color[k], b.color[k]);
  m.outline = 0;
  projected_light_vertex(n);
  // A neighbour's first link to `from` becomes `to`.
  auto relink = [&](int32_t nb, int32_t from, int32_t to) {
    if (nb < 0) return;
    for (int32_t j = 0; j < 3; ++j)
      if (tri[nb].across[j] == from) {
        tri[nb].across[j] = to;
        return;
      }
  };
  const int32_t k = p.triangle_count++;
  ProjTriangle& N = tri[k];
  N.v[0] = n, N.v[1] = T.v[next], N.v[2] = T.v[prev];
  T.v[next] = n;
  N.across[1] = T.across[next];
  relink(N.across[1], t, k);
  N.across[2] = t;
  T.across[next] = k;
  N.active = 1;
  const int32_t o = T.across[edge];
  if (o < 0) {
    N.across[0] = -1;
    m.outline = 1;
    ++p.vertex_count;
    return;
  }
  ProjTriangle& O = tri[o];
  int32_t j = 0;
  while (j < 3 && O.v[j] != N.v[1]) ++j;
  // The edge is always shared, so j < 3; the original would index past v[]
  // otherwise. FIX (bug): stop there.
  if (j == 3) {
    N.across[0] = -1;
    ++p.vertex_count;
    return;
  }
  const int32_t jn = j == 2 ? 0 : j + 1;
  const int32_t jp = j == 0 ? 2 : j - 1;
  const int32_t k2 = p.triangle_count++;
  ProjTriangle& P = tri[k2];
  P.v[0] = n, P.v[1] = O.v[jn], P.v[2] = O.v[jp];
  O.v[jn] = n;
  P.across[1] = O.across[jn];
  relink(P.across[1], o, k2);
  P.across[2] = o;
  O.across[jn] = k2;
  P.active = O.active;
  T.across[edge] = k2;
  N.across[0] = o;
  P.across[0] = t;
  O.across[j] = k;
  ++p.vertex_count;
}

// Mech3.exe 1.2 @ 0x00547740 (ECX count, EDX per_vertex; stack flags,
// material, priority). Returns 1 when it drew the polygon.
int32_t draw_projected(int32_t count, uint32_t per_vertex, uint32_t flags, const Material* material,
                       int32_t priority) {
  ProjectedLights& p = g_proj;
  if (list_projected() == 0) return 0;
  const float radius = polygon_sphere(count);
  ProjVertex& centre = p.vertices[count];
  // Lights out of reach leave the list, the last one taking their place.
  for (int32_t i = 0; i < p.light_count; ++i) {
    zcls::Light* l = p.lights[i];
    if (within(dist2(lvec(l, kCamPos), centre.pos), lf(l, kFar) + radius)) continue;
    --p.light_count;
    if (i == p.light_count) continue;
    p.lights[i] = p.lights[p.light_count];
    --i;
  }
  if (p.light_count == 0) return 0;
  Clip2D& d = clip2d();
  const int32_t sets = d.shadow.count;
  const float* point = zvid::point_light().color;  // 0x0072f40c
  for (int32_t i = 0; i < count; ++i) {
    ProjVertex& v = p.vertices[i];
    v.screen[0] = d.tl[i].sx, v.screen[1] = d.tl[i].sy, v.screen[2] = d.tl[i].sz;
    v.uv[0][0] = d.tl[i].tu, v.uv[0][1] = d.tl[i].tv;
    for (int32_t s = 0; s < sets; ++s) v.uv[1 + s][0] = d.shadow.uv[s][i].u, v.uv[1 + s][1] = d.shadow.uv[s][i].v;
    v.fog = d.work[i].light[2];
    const float lit = 1.0f - (per_vertex ? d.work[i].light[0] : d.work[0].light[0]);
    v.light[0] = v.light[1] = v.light[2] = lit;
    if (flags & 4) {
      const float s = d.work[i].light[1] * 0.003921569f;  // 0x005ce4b4
      for (int c = 0; c < 3; ++c) {
        const float a = s * point[c];
        v.light[c] = a + v.light[c];
      }
    }
    std::memcpy(v.color, d.work[i].color, 12);
    for (int c = 0; c < 3; ++c)
      if (below(v.light[c], 0.0f)) v.light[c] = 0.0f;
    projected_light_vertex(i);
  }
  // The centre takes the mean of the corners' attributes.
  for (int32_t s = 0; s <= sets; ++s) centre.uv[s][0] = p.vertices[0].uv[s][0], centre.uv[s][1] = p.vertices[0].uv[s][1];
  centre.fog = p.vertices[0].fog;
  std::memcpy(centre.light, p.vertices[0].light, 12);
  std::memcpy(centre.color, p.vertices[0].color, 12);
  for (int32_t i = 1; i < count; ++i) {
    const ProjVertex& v = p.vertices[i];
    for (int32_t s = 0; s <= sets; ++s)
      for (int k = 0; k < 2; ++k) centre.uv[s][k] = v.uv[s][k] + centre.uv[s][k];
    centre.fog = v.fog + centre.fog;
    for (int k = 0; k < 3; ++k) centre.light[k] = v.light[k] + centre.light[k];
    for (int k = 0; k < 3; ++k) centre.color[k] = v.color[k] + centre.color[k];
  }
  const float r = 1.0f / static_cast<float>(count);
  for (int32_t s = 0; s <= sets; ++s)
    for (int k = 0; k < 2; ++k) centre.uv[s][k] = r * centre.uv[s][k];
  centre.outline = 0;
  centre.fog = r * centre.fog;
  for (int k = 0; k < 3; ++k) centre.light[k] = r * centre.light[k];
  for (int k = 0; k < 3; ++k) centre.color[k] = r * centre.color[k];
  projected_light_vertex(count);
  // A fan around the centre: triangle i is (i, i + 1, centre), its outer edge open.
  p.triangle_count = count;
  for (int32_t i = 0; i < count; ++i) {
    const int32_t nx = i + 1 == count ? 0 : i + 1;
    ProjTriangle& t = p.triangles[i];
    t.v[0] = i, t.v[1] = nx, t.v[2] = count;
    t.across[0] = -1, t.across[1] = nx, t.across[2] = i == 0 ? count - 1 : i - 1;
    t.active = 1;
  }
  p.vertex_count = count + 1;
  // Six passes: a triangle wider than sqrt(10) that some light's falloff
  // (between its near and far) crosses is split along its longest edge.
  for (int32_t pass = 0; pass < 6; ++pass) {
    const int32_t n = p.triangle_count;
    for (int32_t t = 0; t < n; ++t) {
      ProjTriangle& T = p.triangles[t];
      if (!T.active) continue;
      const float* a = p.vertices[T.v[0]].pos;
      const float* b = p.vertices[T.v[1]].pos;
      const float* c = p.vertices[T.v[2]].pos;
      float mid[3];
      for (int k = 0; k < 3; ++k) mid[k] = a[k] + b[k];
      for (int k = 0; k < 3; ++k) mid[k] = mid[k] + c[k];
      for (int k = 0; k < 3; ++k) mid[k] = mid[k] * 0.3333333f;  // 0x005ce4b8
      float most = dist2(mid, a);
      float e = dist2(mid, b);
      if (e > most) most = e;
      e = dist2(mid, c);
      if (e > most) most = e;
      if (below(most, 10.0f)) {  // 0x005ce4bc
        T.active = 0;
        continue;
      }
      const float tr = approx_sqrt(most);
      int32_t li = 0;
      for (; li < p.light_count; ++li) {
        zcls::Light* l = p.lights[li];
        const float d2 = dist2(lvec(l, kCamPos), mid);
        if (!within(d2, lf(l, kFar) + tr)) continue;
        const float inner = lf(l, kNear) - tr;
        if (below(inner, 0.0f)) break;
        const float ii = inner * inner;
        if (d2 > ii) break;
      }
      if (li >= p.light_count) {
        T.active = 0;
        continue;
      }
      const float e01 = dist2(a, b);
      const float e12 = dist2(b, c);
      float longest = e01;
      int32_t edge = 0;
      if (below(e01, e12)) longest = e12, edge = 1;
      if (dist2(c, a) > longest) edge = 2;
      // FIX (bug): the original has room for 2048 triangles and 6144 vertices
      // and no check (a large polygon overruns them into the light count and
      // the vertices); a split that would not fit is not made.
      const int32_t adds = T.across[edge] < 0 ? 1 : 2;
      if (p.triangle_count + adds > ProjectedLights::kMaxTriangles ||
          p.vertex_count + 1 > ProjectedLights::kMaxVertices) {
        T.active = 0;
        continue;
      }
      projected_split(t, edge);
    }
  }
  // The new vertices are projected; the corners keep their screen positions.
  for (int32_t k = count; k < p.vertex_count; ++k)
    zmth::project_d3d(reinterpret_cast<const zmth::Vec3*>(p.vertices[k].pos),
                      reinterpret_cast<zmth::Vec3*>(p.vertices[k].screen), 1);
  const float bias = static_cast<float>(priority) * draw_state().priority_scale;
  const float scale = bias - -1.0f;
  for (int32_t k = 0; k < p.vertex_count; ++k) p.vertices[k].screen[2] = scale * p.vertices[k].screen[2];
  void* texture = material->texture ? material->texture->surface : nullptr;
  // Midpoints on the outline move a pixel away from the centre (on screen).
  for (int32_t k = count + 1; k < p.vertex_count; ++k) {
    ProjVertex& v = p.vertices[k];
    if (!v.outline) continue;
    const float dx = v.screen[0] - centre.screen[0];
    const float dy = v.screen[1] - centre.screen[1];
    const float xx = dx * dx, yy = dy * dy;
    const float len2 = xx + yy;
    if (!differs(len2, 0.0f)) continue;
    const float inv = 1.0f / approx_sqrt(len2);
    const float mx = inv * dx;
    v.screen[0] = mx + v.screen[0];
    const float my = inv * dy;
    v.screen[1] = my + v.screen[1];
  }
  const DrawTable& table = draw_table();
  const DrawState& ds = draw_state();
  for (int32_t t = 0; t < p.triangle_count; ++t) {
    float light[3][3];
    for (int32_t j = 0; j < 3; ++j) {
      const ProjVertex& v = p.vertices[p.triangles[t].v[j]];
      d.tl[j].sx = v.screen[0], d.tl[j].sy = v.screen[1], d.tl[j].sz = v.screen[2];
      d.tl[j].tu = v.uv[0][0], d.tl[j].tv = v.uv[0][1];
      for (int32_t s = 0; s < sets; ++s) d.shadow.uv[s][j].u = v.uv[1 + s][0], d.shadow.uv[s][j].v = v.uv[1 + s][1];
      d.work[j].light[2] = v.fog;
      std::memcpy(d.work[j].color, v.color, 12);
      std::memcpy(light[j], v.lit, 12);
    }
    if (sets) {
      if (table.projected_shadowed) table.projected_shadowed(table.ctx, light[0], flags, 3, texture);
    } else if (table.projected) {
      table.projected(table.ctx, light[0], flags, 3, texture, priority, ds.opacity, ds.overlay);
    }
  }
  return 1;
}

}  // namespace zmod
