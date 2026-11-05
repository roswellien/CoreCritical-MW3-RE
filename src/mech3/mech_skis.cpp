#include "mech3/mech_skis.h"

#include <cmath>
#include <cstddef>
#include <cstring>

#include "crt/msvc_math.h"
#include "crt/msvc_rand.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "mech3/mech.h"
#include "mech3/mech_build.h"
#include "mech3/mission_world.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

// The skis keep the engine's records as they are (0x006061d0).
static_assert(sizeof(GroundHits) == sizeof(zcls::GroundHits) && sizeof(GroundHit) == sizeof(zcls::GroundHit) &&
                  offsetof(GroundHit, height) == offsetof(zcls::GroundHit, height) &&
                  offsetof(GroundHit, zone) == offsetof(zcls::GroundHit, zone) &&
                  offsetof(GroundHit, material) == offsetof(zcls::GroundHit, material) &&
                  offsetof(GroundHit, node) == offsetof(zcls::GroundHit, node) &&
                  offsetof(GroundHits, hit) == offsetof(zcls::GroundHits, hit),
              "the skis read the ground query's records");

namespace {
SkisCalls g_calls;

constexpr float kHalf = 0.5f;            // 0x005c3738
constexpr float kQueryTop = -50.0f;      // 0x005c3750, subtracted
constexpr float kAirGap = 4.0f;          // 0x005c3754
constexpr float kGroundPull = 4.0f;      // 0x005e58bc, gravity x4 on the ground
constexpr float kStep = -0.7f;           // 0x005c3758, of the height, subtracted
constexpr float kSnap = 3.0f;            // 0x005c375c, also the push x3 of a far mech
constexpr float kIrregular = 0.2f;       // 0x005c3760
constexpr float kStepUp = 3.0f;          // 0x005e5914
constexpr float kOnGround = 0.2f;        // 0x005e58ac
constexpr float kSlopeRate = 2.0f;       // 0x005e5908
constexpr float kMaxSlope = 0.573576f;   // 0x005e5904, sin 35 degrees
constexpr float kLiftMax = 1.0f;         // 0x005e58f8
constexpr float kLiftMin = -0.8f;        // 0x005e58f4
constexpr float kSlopePush = 0.8f;       // 0x005e58f0
constexpr double kLedge = 1.5;           // 0x005c3778
constexpr float kSeventh = 0.142857149f; // 0x005c3780
constexpr float kCeilingTime = 4.0f;     // 0x005e58c4
constexpr bool kStopOnGround = true;     // byte 0x005e58c8
constexpr float kSteep = 0.85f;          // 0x005e58b0, normal y
constexpr float kSlide = 30.0f;          // 0x005e58b4
constexpr int kWater = 1, kNoSlip = 10;

int soil(Handle material) { return material && g_calls.soil ? g_calls.soil(material) : 0; }

// A point through the top of the matrix stack, as the skis inline it.
Vec3 through_top(const Vec3& p) {
  zmth::MatrixStack& s = zmth::matrix_stack();
  if (s.is_identity()) return p;
  const float* m = s.matrix().m;
  return {((p.x * m[0] + p.y * m[3]) + p.z * m[6]) + m[9], ((p.x * m[1] + p.y * m[4]) + p.z * m[7]) + m[10],
          ((p.x * m[2] + p.y * m[5]) + p.z * m[8]) + m[11]};
}

float* node_matrix(Handle node) { return g_calls.node_matrix ? g_calls.node_matrix(node) : nullptr; }
}  // namespace

SkisCalls& skis_calls() { return g_calls; }

// Mech3.exe 1.2 @ 0x00475c80: the parabola through the ski's three heights.
void Skis::fit(int side) {
  const int i = side ? 3 : 0;
  a[side] = (ground[i] + ground[i + 2]) * inv_half2 * kHalf - ground[i + 1] * inv_half2;
  b[side] = (ground[i] - ground[i + 2]) * inv_half * kHalf;
  c[side] = ground[i + 1];
}

// FIX (bug): half x 0.5, the value Pirate's Moon writes (its 0x477d1a); MW3 never writes it.
float Skis::mid() const { return zfix::options().bugs ? half * kHalf : mid_width; }

// Mech3.exe 1.2 @ 0x00475de0. The contact is the mid probe's within
// mid() / 2 of it, else the front or back one.
float Skis::height_at(int side, float z, GroundHit** out) {
  const float h = z * z * a[side] - b[side] * z + c[side];
  if (out) {
    const int i = side ? 3 : 0;
    const float zm = local[i + 1].z;
    if (mid() * kHalf > std::fabs(z - zm)) *out = &contact[i + 1];
    else *out = &contact[z < zm ? i : i + 2];
  }
  return h;
}

// Mech3.exe 1.2 @ 0x00475ea0
bool Skis::water_at(int side, float z, float* surface_y) const {
  const int i = side ? 3 : 0;
  const float zm = local[i + 1].z;
  const int k = mid() * kHalf > std::fabs(z - zm) ? i + 1 : z < zm ? i : i + 2;
  *surface_y = surface[k];
  return in_water[k] == 1;
}

// Mech3.exe 1.2 @ 0x004764a0: the ski's pitch about x and its lift.
void Skis::set_ski(int side, float angle, float lift) {
  sin_pitch[side] = static_cast<float>(std::sin(static_cast<double>(angle)));
  cos_pitch[side] = static_cast<float>(std::cos(static_cast<double>(angle)));
  if (float* m = node_matrix(ski[side])) {
    m[4] = cos_pitch[side];
    m[8] = cos_pitch[side];
    m[5] = sin_pitch[side];
    m[7] = -sin_pitch[side];
    m[10] = lift;
    m[9] = 0.0f;
    m[11] = 0.0f;
    if (g_calls.set_node_matrix) g_calls.set_node_matrix(ski[side], m);
  }
  fit(side);
}

// Mech3.exe 1.2 @ 0x00475fb0
void Skis::reset() {
  for (int side = 0; side < 2; ++side) {
    const float x = side ? half_track : -half_track;
    local[3 * side] = {x, 0.0f, -half};
    local[3 * side + 1] = {x, 0.0f, 0.0f};
    local[3 * side + 2] = {x, 0.0f, half};
  }
  set_ski(0, 0.0f, 0.0f);
  set_ski(1, 0.0f, 0.0f);
}

// Mech3.exe 1.2 @ 0x00476590
void Skis::set_vy(float v) {
  for (float& x : vy) x = v;
}

// Mech3.exe 1.2 @ 0x00477820. The highest polygon no more than `step` above
// the probe; water is never ground but gives the surface. With none in
// reach, the lowest.
float Skis::pick_ground(const GroundHits& hits, int* best, float probe_y, float step, bool water_not_ground,
                        int32_t* in_water, float* surface) {
  *best = 0;
  *in_water = 0;
  *surface = -300.0f;
  float best_h = -250.0f;
  if (hits.count == 0) return probe_y;
  float min_h = -250.0f;  // 0x005c378c
  bool water = false;
  for (int i = 0; i < hits.count; ++i) {
    const float h = hits.hit[i].height;
    if (i == 0 || h < min_h) min_h = h;
    if (soil(hits.hit[i].material) == kWater) {
      water = true;
      *surface = h;
      if (water_not_ground) continue;
    }
    if (h > best_h && h - step <= probe_y) {
      best_h = h;
      *best = i;
    }
  }
  if (!(*surface + step < probe_y)) *in_water = water;
  return best_h > min_h ? best_h : min_h;
}

// Mech3.exe 1.2 @ 0x00477940. Lands on the ground with the speed that takes
// it there in one step (no shorter than 1/30 s); true for a hard landing.
bool Skis::integrate(float* y, float* vy, float dt, float ground, float gravity) {
  *vy = *vy - gravity * dt;
  const float y1 = dt * *vy + *y;
  if (y1 > ground) {
    *y = y1;
    return false;
  }
  const double span = dt < 0.0333 ? 0.0333 : dt;  // 0x005c3790
  const float v = static_cast<float>(-(*y - ground) / span);
  // FIX (B): there in this step at any rate, as at 30 fps; the 1/30 s floor
  // left the probes under rising ground, eating the jets' climb (fixes.md).
  // The hard landing keeps the floor's speed.
  *vy = zfix::options().per_second ? -(*y - ground) / dt : v;
  *y = ground;
  return v > 20.0f;  // 0x005c3798
}

// Mech3.exe 1.2 @ 0x00477620: down a polygon steeper than about 32 degrees.
bool Skis::slope_slide(const GroundHit& c, Vec3* push, float dt) {
  if (!(c.normal.y < kSteep)) return false;
  if (soil(c.material) == kNoSlip) return false;
  const float k = kSlide * dt;
  const Vec3 d{k * c.normal.x, 0.0f, k * c.normal.z};
  *push = {d.x + push->x, d.y + push->y, d.z + push->z};
  return true;
}

// Mech3.exe 1.2 @ 0x004765b0. Walkthrough: docs/movement-ground.md.
bool Skis::update(float dt) {
  Mech& m = *mech;
  const bool simple = !(m.detail & 0x10) || !m.active;
  const bool frozen = m.blocked[0] || m.blocked[1] || m.blocked[3];
  slid = false;
  Vec3 v = m.vec_3c;
  std::memcpy(matrix, m.transform, sizeof matrix);
  if (frozen) v.z = v.x = 0.0f;
  const float* r = matrix;
  const Vec3 w{(v.x * r[0] + v.y * r[3]) + v.z * r[6], (v.x * r[1] + v.y * r[4]) + v.z * r[7],
               (v.x * r[2] + v.y * r[5]) + v.z * r[8]};
  const Vec3 step_xz{w.x * dt, 0.0f, w.z * dt};
  matrix[9] = step_xz.x + matrix[9];
  matrix[10] = step_xz.y + matrix[10];
  matrix[11] = step_xz.z + matrix[11];
  const int start = simple ? 6 : 0, count = simple ? 1 : 7;
  for (int side = 0; side < 2; ++side) {
    Foot* f = m.foot(side);
    local[side ? 4 : 1].z = f->destroyed ? 0.0f : f->translation->z;
  }

  zmth::Mat34 body;
  std::memcpy(body.m, matrix, sizeof body.m);
  zmth::push(&body);
  world[6] = through_top(local[6]);
  if (!simple) {
    zmth::Mat34 ski_frame;
    zmth::push_copy(&ski_frame);
    zmth::multiply(*reinterpret_cast<zmth::Mat34*>(node_matrix(ski[0])), 1);
    for (int j = 0; j < 3; ++j) world[j] = through_top(local[j]);
    zmth::pop();
    zmth::multiply(*reinterpret_cast<zmth::Mat34*>(node_matrix(ski[1])), 1);
    for (int j = 3; j < 6; ++j) world[j] = through_top(local[j]);
  }
  zmth::pop();

  static GroundHits hits[7];  // 0x006061d0
  for (GroundHits& h : hits) h = GroundHits{};
  // FIX (bug), W58: the query honours the zones in the camera's view, so an AI
  // mech in another zone (a lancemate in c2m4's tunnel) lost its own floor and
  // read the land over it as a wall ahead (fixes.md). Its own feet read them all.
  const int32_t zone_mode = zmod::zones().mode;
  if (zfix::options().bugs && &m != mech3::world().mission_player) zmod::zones().mode = 0;
  if (g_calls.query) {
    g_calls.query(&world[start], count, matrix[10] - kQueryTop, &hits[start]);
  } else if (Node* w = world_hooks().world) {
    zcls::ground_query(w, &world[start].x, count, matrix[10] - kQueryTop,
                       reinterpret_cast<zcls::GroundHits*>(&hits[start]));
  }
  zmod::zones().mode = zone_mode;

  float g = environment().gravity;
  const bool both = down[0] && down[1];
  bool air;
  if (m.impulse.y > 0.0f || (!(clearance[6] < kAirGap) && !both)) {
    air = true;
  } else {
    g = kGroundPull * g;
    air = false;
  }
  const float step = m.blocked[2] && air ? 0.0f : 0.0f - m.height * kStep;
  for (int j = 6; j >= start; --j) {
    surface[j] = 0.0f;
    if (landed[j] && !air && vy[j] > 0.0f) vy[j] = 0.0f;
    vy[j] = m.impulse.y + vy[j];
    in_water[j] = 0;
    int best = 0;
    ground[j] = pick_ground(hits[j], &best, world[j].y, step, true, &in_water[j], &surface[j]);
    contact[j] = hits[j].hit[best];
    contact[j].x = world[j].x;
    contact[j].z = world[j].z;
    y[j] = world[j].y;
    landed[j] = integrate(&y[j], &vy[j], dt, ground[j], g);
    clearance[j] = y[j] - ground[j];
    if (!air && clearance[j] < kSnap) {
      world[j].y = ground[j];
      y[j] = ground[j];
      vy[j] = vy[j] - clearance[j] / dt;
      clearance[j] = 0.0f;
    }
    if (!air) slid = slope_slide(contact[j], &m.push, dt) || slid;
    if (vy[j] < 0.0f && vy[j] < vy[6]) vy[j] = vy[6];
  }
  if (simple) {
    for (int k = 0; k < start; ++k) {
      surface[k] = surface[6], vy[k] = vy[6], in_water[k] = in_water[6], ground[k] = ground[6];
      contact[k] = contact[6];
      y[k] = y[6], clearance[k] = clearance[6], landed[k] = landed[6];
    }
    // x3 a frame. FIX (B): x3 per 1/30 s (fixes.md, Frame rate).
    const float x3 = zfix::options().per_second ? std::pow(kSnap, zfix::frame_steps(dt)) : kSnap;
    if (slid) m.push = {m.push.x * x3, m.push.y * x3, m.push.z * x3};
  }

  // Each ski's slope from its regular segment; a wall-like step ahead or
  // behind and a slope past 35 degrees push the mech (per frame).
  Vec3* push = down[0] || down[1] ? &m.push : nullptr;
  const float steps = zfix::frame_steps(dt);  // FIX (B): these pushes per 1/30 s
  float lift[2];
  for (int side = 0; side < 2; ++side) {
    const int i = side ? 3 : 0;
    float kick = 0.0f, t;
    const float back = ground[i + 2] - ground[i + 1], front = ground[i] - ground[i + 1];
    if (std::fabs(front) - std::fabs(back) < kIrregular) {
      t = (ground[i + 1] - ground[i]) / (local[i + 1].z - local[i].z);
      vy[i + 2] = vy[i + 1];
      if (!(clearance[i + 2] > 0.0f) && ground[i + 2] - ground[i + 1] > kStepUp) kick = 1.0f;
    } else {
      t = back / (local[i + 2].z - local[i + 1].z);
      vy[i] = vy[i + 1];
      if (!(clearance[i] > 0.0f) && ground[i] - ground[i + 1] > kStepUp) kick = -1.0f;
    }
    const float mid = clearance[i + 1];
    const bool on = mid < kOnGround && mid > -kOnGround;
    t = t * -1.0f;
    slope[side] = (t - slope[side]) * kSlopeRate * dt + slope[side];
    if (std::fabs(t - slope[side]) < 0.01f) slope[side] = t;
    bool clamped = true;
    if (slope[side] > kMaxSlope) slope[side] = kMaxSlope;
    else if (-kMaxSlope > slope[side]) slope[side] = -kMaxSlope;
    else clamped = false;
    float l = (y[6] - (t * 1.0f * local[i + 1].z + y[i + 1])) * -1.0f;
    l = l > kLiftMax ? kLiftMax : l < kLiftMin ? kLiftMin : l;
    lift[side] = l;
    if (push) {
      if (kick != 0.0f) {
        slid = true;
        kick = kick * steps;
        const Vec3 d{m.forward.x * kick, m.forward.y * kick, m.forward.z * kick};
        *push = {d.x + push->x, d.y + push->y, d.z + push->z};
      }
      if (clamped) {
        slid = true;
        const float k = t * kSlopePush / kMaxSlope * steps;
        zmth::Vec3 d;
        zmth::direction({world[i].x, world[i].y, world[i].z}, {world[i + 1].x, world[i + 1].y, world[i + 1].z}, d);
        d.y = 0.0f;
        zmth::normalize(d);
        const Vec3 s{d.x * k, d.y * k, d.z * k};
        *push = {s.x + push->x, s.y + push->y, s.z + push->z};
      }
    }
    down[side] = on;
  }

  // One ski down and the other off a ledge: a shove toward the low side.
  if (down[0] != down[1]) {
    const float d = clearance[1] - clearance[4];
    if (std::fabs(d) > kLedge) {
      const float sgn = d < 0.0f ? -1.0f : 1.0f;
      zmth::Vec3 r;
      zmth::quat_rotate({m.orientation[0], m.orientation[1], m.orientation[2], m.orientation[3]},
                        {sgn * -1.0f, 0.0f, 0.0f}, r);
      const float s = zfix::frame_steps(dt);  // FIX (B): per 1/30 s
      m.push = {m.push.x + r.x * s, m.push.y + r.y * s, m.push.z + r.z * s};
      slid = true;
    }
  }

  float body_vy;
  if (!simple) {
    float sum = 0.0f;
    for (float x : vy) sum = sum + x;
    body_vy = sum * kSeventh;
  } else {
    body_vy = vy[6];
  }
  const Vec3* pos = g_calls.position ? g_calls.position(m) : nullptr;
  const float cap = (environment().jet_ceiling - (pos ? pos->y : 0.0f)) / kCeilingTime;
  if (!(body_vy < cap)) body_vy = cap;
  const float rise = body_vy * dt;
  for (Vec3& p : world) p.y = rise + p.y;
  const float pitch0 = !simple && slope[0] != 0.0f ? static_cast<float>(std::asin(slope[0])) : 0.0f;  // 0x005ac380
  const float pitch1 = !simple && slope[1] != 0.0f ? static_cast<float>(std::asin(slope[1])) : 0.0f;
  set_ski(0, pitch0, lift[0]);
  set_ski(1, pitch1, lift[1]);
  m.vec_3c.y = body_vy;
  if (down[0] && down[1] && !air)
    for (float& x : vy) x = kStopOnGround ? 0.0f : body_vy;
  zone_update(contact[6].zone);
  if (m.impulse.y > 0.0f) return true;
  const float c6 = clearance[6];
  if (c6 < kOnGround && c6 > -kOnGround) return false;
  return !down[0] && !down[1];
}

// Mech3.exe 1.2 @ 0x00476550: the mech's zone record from the centre probe's
// polygon. The id it is drawn under is the first of the record's the camera
// can see, else the record's first (docs/movement-ground.md, The zone record).
void Skis::zone_update(const uint8_t record[4]) {
  if (!mech || !g_calls.set_zone) return;
  static const uint8_t kNone[4] = {0, 0xff, 0xff, 0xff};
  const uint8_t* cam = g_calls.camera_zones ? g_calls.camera_zones() : nullptr;
  int32_t id = zmod::zone_common(cam ? cam : kNone, record);  // 0x0053cd20 (ECX the camera's, EDX the record)
  if (id == -1) id = record[1];
  g_calls.set_zone(*mech, record, static_cast<uint8_t>(id));
}

// Mech3.exe 1.2 @ 0x00476320: the skis a half track apart, `length` long,
// under a "ski base" node; each ski's probes, model and pose (0x00476090).
Skis* make_skis(Mech& m, float half_track, float length) {
  MechBuildHooks& h = mech_build_hooks();
  auto* s = new Skis();
  h.on_new(0x300, s);
  s->mech = &m;
  s->slid = false;
  s->half_track = half_track;
  s->length = length;
  h.on_new(8, &s->base);  // the base's wrapper (vtable 0x005bd2f4): the port keeps its node
  s->base = h.object3d_create();
  h.node_set_name(s->base, "ski base");
  h.node_add_child(m.node, s->base);
  s->half = s->length * 0.5f;
  s->inv_half = 1.0f / s->half;
  s->inv_half2 = s->inv_half * s->inv_half;
  for (int i = 0; i < 7; ++i) {
    s->ground[i] = s->vy[i] = s->clearance[i] = 0;
    s->world[i] = s->local[i] = Vec3{};
    s->contact[i] = GroundHit{};
    s->landed[i] = false;
  }
  s->down[0] = s->down[1] = true;
  s->slope[0] = s->slope[1] = 0;
  skis_make_ski(*s, 0);
  skis_make_ski(*s, 1);
  s->slid = false;
  h.node_set_active(s->ski[0], 0);
  h.node_set_active(s->ski[1], 0);
  for (GroundHit& c : s->contact) c = GroundHit{};
  return s;
}

// Mech3.exe 1.2 @ 0x00476090: a ski's three probes (front, middle, back) at
// its side of the track, and its node with a debug model (a 1 m wide flat
// quad and an upright one along the ski), the flag cleared, the ski level.
void skis_make_ski(Skis& s, int32_t side) {
  MechBuildHooks& h = mech_build_hooks();
  h.on_new(8, &s.ski[side]);  // the ski's wrapper
  const Handle node = h.object3d_create();
  h.node_set_name(node, side ? "right ski" : "left ski");
  s.ski[side] = node;
  h.node_add_child(s.base, node);  // the base wrapper's vt+0 (0x0040bab0)
  const float x = side ? s.half_track : -s.half_track;
  Vec3* p = &s.local[3 * side];
  p[0] = {x, 0, -s.half}, p[1] = {x, 0, 0}, p[2] = {x, 0, s.half};
  const Handle model = h.model_new();
  h.node_set_model(node, model);
  h.model_set_priority(model, 1);
  const float X = p[0].x, H = s.half;
  const float flat[12] = {X + 0.5f, 0, H, X + 0.5f, 0, -H, X - 0.5f, 0, -H, X - 0.5f, 0, H};
  h.model_add_polygon(model, 4, flat);
  const float upright[12] = {X, 0.5f, H, X, 0.5f, -H, X, -0.5f, -H, X, -0.5f, H};
  h.model_add_polygon(model, 4, upright);
  h.node_set_flag8(node, 0);
  h.ski_set(s, side);
}

// Mech3.exe 1.2 @ 0x004776d0
bool ski_polygon(zmod::Model* model, int32_t n, float* points) {
  uint8_t zone[4];
  zmod::zone_reset(zone);
  if (n < 3) return false;
  zmod::PolygonAdd a;
  a.points = points;
  a.material = ski_material();
  a.flag = 1;
  a.zone = zone;
  return zmod::model_add_polygon(model, n, a) == 0;
}

// Mech3.exe 1.2 @ 0x00477730: three rand()s scaled to 0..255 (0x005c3784),
// drawn again until they sum to 150 (0x005c3788). The packing shifts the
// 0..255 values into the 16-bit colour unscaled, so the shown colour is
// their low bits (docs/movement-ground.md, The skis' debug quads).
zmod::Material* ski_material() {
  zmod::Material m;
  zmod::material_init(m);
  const float k = 0x1.fe03fcp-8f;  // 0x005c3784, 255 / 32767
  float first, second, third;
  do {
    first = static_cast<float>(crt::msvc_rand()) * k;
    second = static_cast<float>(crt::msvc_rand()) * k;
    third = static_cast<float>(crt::msvc_rand()) * k;
  } while ((second + first) + third < 150.0f);
  m.r = second, m.g = first, m.b = third;
  uint32_t w = m.rgb;
  w = (static_cast<uint32_t>(crt::msvc_ftol(m.r)) << 11) | (w & 0x7ff);
  w = (w & 0xf81f) | ((static_cast<uint32_t>(crt::msvc_ftol(m.g)) & 0x3f) << 5);
  w = (w & ~0x1fu) | (static_cast<uint32_t>(crt::msvc_ftol(m.b)) & 0x1f);
  m.rgb = static_cast<uint16_t>(w);
  return zmod::material_add(m);
}

}  // namespace mech3
