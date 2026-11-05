// The collision's per-frame amounts at any frame rate (zfix per_second,
// docs/fixes.md): a contact of 1/30 s does the same as four of 1/120 s and
// the same as the exe's one frame. test_collide.py checks the exe's path.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "gamez/zutil/zfix.h"
#include "mech3/mech.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_move.h"
#include "mech3/mech_skis.h"
#include "check.h"

using namespace mech3;

namespace {
struct CountTorso : Torso {
  float* total = nullptr;
  void apply_damage(float d, int) override { *total += d; }
};
struct CountLeg : Leg {
  float* total = nullptr;
  void apply_damage(float d, int) override { *total += d; }
};
struct CountHead : Head {
  float* total = nullptr;
  void apply_damage(float d, int) override { *total += d; }
};
float g_entity_damage = 0;
float g_body[3], g_spin[3];
float g_box[6] = {0, 0, 0, 10, 10, 10};
float g_ducked = 0;
Mech* g_other = nullptr;

struct Pair {
  Mech a, b;
  MechControls ctl;
  CountTorso ta, tb;
  CountLeg la, lb;
  CountHead ha;
  MechData pa, pb;
  float damage_a = 0, damage_b = 0, head_a = 0;
  Pair() {
    pa.roster = 13, pb.roster = 4;
    a.pilot = &pa, b.pilot = &pb;
    a.controls = &ctl, ctl.owner = &a, ctl.throttle = 1.0f;
    ta.total = &damage_a, tb.total = &damage_b, la.total = &damage_a, lb.total = &damage_b, ha.total = &head_a;
    a.torso = &ta, b.torso = &tb, a.legs[0] = a.legs[1] = &la, b.legs[0] = b.legs[1] = &lb, a.head = &ha;
    a.max_speed = b.max_speed = 80.0f;
    a.turn_rate = 1.0f;
    a.velocity = {0, 0, 40.0f};
    a.vec_3c = {0, 0, 40.0f};
    a.collider.yaw_kick = 0.01f, a.collider.restitution = 0.5f;
    a.allies = b.allies = 0;
    a.team = 1, b.team = 2;
    CollideCalls& c = collide_calls();
    c = CollideCalls{};
    c.node_mech = [](Handle) { return g_other; };
    c.node_entity = [](Handle) -> Handle { return &g_box; };
    c.entity_target = [](Handle) -> Handle { return &g_box; };
    c.entity_body = [](Handle) -> Handle { return &g_box; };
    c.entity_damage = [](Handle, float d) -> int32_t { g_entity_damage += d; return 0; };
    c.body_velocity = [](Handle) { return g_body; };
    c.body_spin = [](Handle) { return g_spin; };
    c.body_mass = [](Handle) { return 1.0f; };
    c.hit_box = [](Handle) -> const float* { return g_box; };
    c.crouch = [](Hip&, float v) { g_ducked += v; };
    g_other = &b;
    g_entity_damage = 0, g_ducked = 0;
    std::memset(g_body, 0, sizeof g_body);
    std::memset(g_spin, 0, sizeof g_spin);
  }
  // `times` frames of `dt` in contact; the mech's speed comes back each frame.
  void run(int times, float dt, void (*f)(Pair&)) {
    a.frame_dt = dt;
    for (int i = 0; i < times; ++i) {
      a.velocity = a.vec_3c = {0, 0, 40.0f};
      b.velocity = {0, 0, 0};  // its update sets it anew (0x00419fc0)
      f(*this);
    }
  }
};
CollisionHit wall_hit() {
  CollisionHit e;
  std::memset(&e.hit, 0, sizeof e.hit);
  e.hit.normal[0] = 1.0f, e.hit.point[0] = 1.0f;
  e.from = {0, 0, 0}, e.to = {0, 0, 0}, e.probe = 0;
  return e;
}
void wall(Pair& p) { p.a.wall_response(wall_hit()); }
void mech(Pair& p) {
  p.a.collider.mechs.clear();
  p.a.collider.mechs.push_back(wall_hit());
  p.a.mech_response();
}
void leg(Pair& p) {
  p.a.collider.mechs.clear();
  CollisionHit e = wall_hit();
  e.probe = 6;
  p.a.collider.mechs.push_back(e);
  p.a.mech_response();
}
void vehicle(Pair& p) {
  p.a.collider.vehicles.clear();
  p.a.collider.vehicles.push_back(wall_hit());
  p.a.vehicle_response();
}
void crush(Pair& p) { p.a.ceiling_response(wall_hit()); }
void knock(Pair& p) {
  p.a.no_power_down = true;  // airborne, falling
  p.a.vec_3c.y = -1.0f;
  p.a.ceiling_response(wall_hit());
}
// The bump in 0x004225c0: another mech among the contacts within its radius.
void bump(Pair& p) {
  static const float m[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  static const Vec3 dir{1.0f, 0.0f, 0.0f};
  p.a.transform = const_cast<float*>(m);
  p.a.collider.count = 0;
  CollideCalls& c = collide_calls();
  c.contact_count = [](Mech&) -> int32_t { return 1; };
  c.contact = [](Mech&, int32_t) -> Handle { return &g_box; };
  c.contact_mech = [](Handle) { return g_other; };
  c.contact_dist2 = [](Handle) { return 1.0f; };
  c.contact_dist = [](Handle) { return 1.0f; };
  c.contact_dir = [](Handle) { return &dir; };
  p.a.collide();
}

bool near(float x, float y) { return std::fabs(x - y) <= 1e-4f * (std::fabs(x) + std::fabs(y) + 1e-3f); }
struct Result {
  float throttle, kick, damage_a, damage_b, shove_b, head, entity, body, ducked, push;
};
Result result(void (*f)(Pair&), int times, float dt, bool airborne = false) {
  auto* p = new Pair;
  p->a.no_power_down = airborne;
  p->run(times, dt, f);
  const Result r{p->ctl.throttle, p->a.vec_58.y, p->damage_a, p->damage_b, p->b.velocity.z, p->head_a, g_entity_damage, g_body[2], g_ducked, p->a.push.x};
  delete p;
  return r;
}
void same_at_any_rate(void (*f)(Pair&)) {
  zfix::options().per_second = true;
  const Result once = result(f, 1, 1 / 30.0f), many = result(f, 4, 1 / 120.0f), more = result(f, 8, 1 / 240.0f);
  std::printf("  1/30 s at 30, 120 fps: throttle %.4f %.4f, kick %.3f %.3f, damage %.4f %.4f, theirs %.4f %.4f, shove %.3f %.3f, "
              "head %.4f %.4f, vehicle %.3f %.3f, its shove %.3f %.3f, crouch %.3f %.3f\n",
              once.throttle, many.throttle, once.kick, many.kick, once.damage_a, many.damage_a, once.damage_b, many.damage_b,
              once.shove_b, many.shove_b, once.head, many.head, once.entity, many.entity, once.body, many.body, once.ducked, many.ducked);
  for (const Result& r : {many, more}) {
    EXPECT(near(once.throttle, r.throttle) && near(once.kick, r.kick));
    EXPECT(near(once.damage_a, r.damage_a) && near(once.damage_b, r.damage_b) && near(once.shove_b, r.shove_b));
    EXPECT(near(once.head, r.head) && near(once.entity, r.entity) && near(once.body, r.body) && near(once.ducked, r.ducked));
  }
}
}  // namespace

// 0x004231c0: x0.1 of the throttle (0x005bd378), the yaw kick.
TEST(collide_rate_wall) {
  same_at_any_rate(wall);
  const Result r = result(wall, 1, 1 / 30.0f);
  EXPECT(near(r.throttle, 0.1f) && r.kick != 0.0f);
}
// 0x00423a20: the crash's damage to each by time; the shove is one frame's at any rate.
TEST(collide_rate_mech) {
  same_at_any_rate(mech);
  same_at_any_rate(leg);
  const Result r = result(mech, 1, 1 / 30.0f), l = result(leg, 1, 1 / 30.0f);
  EXPECT(r.damage_a > 0.0f && r.damage_b > 0.0f && r.shove_b != 0.0f && l.damage_a > 0.0f);
}
// 0x004237c0: the rammed vehicle's damage (0x005bd3b0) and shove (0x005bd3b4).
TEST(collide_rate_vehicle) {
  same_at_any_rate(vehicle);
  const Result r = result(vehicle, 1, 1 / 30.0f);
  EXPECT(r.entity > 0.0f && r.body != 0.0f);
}
// 0x00423600 on foot: the crush's damage (0x005bd3a0, 0x005bd3a8) and crouch.
TEST(collide_rate_crush) {
  same_at_any_rate(crush);
  const Result r = result(crush, 1, 1 / 30.0f);
  EXPECT(r.head > 0.0f && r.damage_a > 0.0f && r.ducked > 0.0f);
}
// 0x00423600 in the air: the knock on the head.
TEST(collide_rate_knock) {
  same_at_any_rate(knock);
  EXPECT(near(result(knock, 1, 1 / 30.0f).head, 0.002f));
}
// The bump in 0x004225c0: x0.1 of the throttle (0x005bd378), the shove (0x005bd368).
TEST(collide_rate_bump) {
  zfix::options().per_second = true;
  const Result one = result(bump, 1, 1 / 30.0f), many = result(bump, 4, 1 / 120.0f);
  std::printf("  bump at 30, 120 fps: throttle %.4f %.4f, shove %.3f %.3f\n", one.throttle, many.throttle, one.push, many.push);
  EXPECT(near(one.throttle, 0.1f) && near(one.throttle, many.throttle));
  EXPECT(one.push != 0.0f && near(one.push, many.push));
}
// With the fix off every amount is the exe's, once a frame.
TEST(collide_rate_original_path_is_per_frame) {
  zfix::options().per_second = false;
  const Result one = result(mech, 1, 1 / 30.0f), four = result(mech, 4, 1 / 120.0f);
  EXPECT(near(4.0f * one.damage_a, four.damage_a) && near(4.0f * one.damage_b, four.damage_b));
  const Result v1 = result(vehicle, 1, 1 / 30.0f), v4 = result(vehicle, 4, 1 / 120.0f);
  EXPECT(near(4.0f * v1.entity, v4.entity) && near(4.0f * v1.body, v4.body));
  const Result b1 = result(bump, 1, 1 / 30.0f), b4 = result(bump, 4, 1 / 120.0f);
  EXPECT(near(4.0f * b1.push, b4.push));
  EXPECT(near(four.throttle, 0.0001f));
  const Result c = result(crush, 4, 1 / 120.0f), c1 = result(crush, 1, 1 / 30.0f);
  EXPECT(near(4.0f * c1.head, c.head));
  zfix::options().per_second = true;
}
// 1/30 s with the fix on is the exe's frame, bit for bit.
TEST(collide_rate_30fps_is_the_original) {
  for (auto* f : {wall, mech, leg, vehicle, crush, knock, bump}) {
    zfix::options().per_second = false;
    const Result off = result(f, 1, 1 / 30.0f);
    zfix::options().per_second = true;
    const Result on = result(f, 1, 1 / 30.0f);
    EXPECT(std::memcmp(&off, &on, sizeof off) == 0);
  }
}
