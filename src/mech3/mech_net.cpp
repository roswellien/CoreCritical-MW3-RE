#include "mech3/mech_net.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "gamez/zclass/cls_node.h"
#include "gamez/zdeclient/zdec_crater.h"
#include "gamez/zweapon/zweapon.h"
#include "mech3/mech_fieldbase.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zdeclient/zdec_init.h"
#include "mech3/mech_parts.h"
#include "gamez/zmath/zmth_vector.h"
#include "crt/msvc_math.h"
#include "gamez/zeffect/zeff_anim_save.h"
#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/ztimer.h"
#include "mech3/mech_config.h"
#include "mech3/mech_dead.h"
#include "mech3/mech_data.h"
#include "mech3/mech_build.h"
#include "mech3/mech_control.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_skin.h"
#include "mech3/weapon.h"
#include "mech3/content_network.h"
#include "mech3/content_state.h"
#include "mech3/content_registry.h"

namespace mech3 {

namespace {
NetGame g_net;
NetCalls g_calls;

// The messages 0x0046d880 takes, in the exe's order of registration.
constexpr uint16_t kRouted[] = {0xf, 0x10, 0x11, 0x16, 0x15, 0x18, 0x12, 0x13, 0x17, 0x1a, 0x1b, 0x19, 0x21, 0x14, 0x1f};

// Mech3.exe 1.2 @ 0x004d72a0, message 1's: returns 0.
void ignore(uint32_t, const uint8_t*, size_t) {}
// Mech3.exe 1.2 @ 0x004709d0, message 0xe's (piece 6).
void send_done(uint32_t from, const uint8_t* msg, size_t size) {
  if (g_calls.send_done) g_calls.send_done(nullptr, from, msg, size);
  else net_send_done(msg, size);
}

void call(NetRoute fn, NetMech* m, uint32_t from, const uint8_t* msg, size_t size) {
  if (fn) fn(m, from, msg, size);
}
}  // namespace

NetGame& net_game() { return g_net; }
NetCalls& net_calls() { return g_calls; }
void net_set_lobby_left(void (*fn)(uint32_t)) { g_calls.lobby_left = fn; }

NetMech* net_mech(uint32_t owner) {
  for (NetMech* m : g_net.mechs)
    if (m->owner == owner) return m;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0046ceb0
void net_start() {
  if (g_net.running) return;
  g_net.missing.clear();
  for (uint16_t t : kRouted) znet::on(t, net_route, 2);
  znet::on(6, net_player_left, 2);
  znet::on(1, ignore, 2);
  znet::on(0xe, send_done, 2);
  zdec::craters().veto = g_calls.crater_veto ? g_calls.crater_veto : net_crater_veto;  // 0x0046fd30
  g_net.running = true;
  weapon_world().lock_targets = true;  // the same flag
  zeff::log_set_net(g_calls.anim_sent ? g_calls.anim_sent : net_anim_sent, 0xc);  // 0x0046feb0
  if (g_calls.game_clear) g_calls.game_clear();
  // The dead list's respawn of the pilot (the exe's 0x0041e220 calls
  // 0x0046dde0 in a network game): set while the network game runs (ours).
  dead_calls().respawn = net_pilot_respawn;
  g_net.reckon_gain = -6.5f;
  g_net.reckon_damp = -0.3f;
}

// Mech3.exe 1.2 @ 0x0046d100
void net_end() {
  if (!g_net.running) return;
  for (uint16_t t : kRouted) znet::off(t, net_route);
  znet::off(6, net_player_left);
  znet::off(1, ignore);
  znet::off(0xe, send_done);
  zdec::craters().veto = nullptr;
  dead_calls().respawn = nullptr;
  g_net.running = false;
  weapon_world().lock_targets = false;
  if (g_calls.player_mech_end) g_calls.player_mech_end();
  while (!g_net.mechs.empty()) {
    NetMech* m = g_net.mechs.front();
    if (g_calls.forget) g_calls.forget(*m);
    g_net.mechs.pop_front();
    if (m) g_calls.free ? g_calls.free(m) : net_mech_delete(m);
  }
  znet::set_player_data(znet::local_id(), nullptr, 0);
  zeff::log_set_net(nullptr, 0xc);
  if (g_calls.ff_stop) g_calls.ff_stop();
  for (Config*& c : g_net.configs) {
    if (c) g_calls.config_free ? g_calls.config_free(c) : delete c;
    c = nullptr;
  }
  g_net.configs.clear();
  g_net.missing.clear();
}

// Mech3.exe 1.2 @ 0x0046d880. The handlers return to zNetwork's dispatch,
// which drops the answer.
void net_route(uint32_t from, const uint8_t* msg, size_t size) {
  if (size < 2) return;
  uint16_t type;
  std::memcpy(&type, msg, 2);
  const NetCalls& c = g_calls;
  if (type == 0x19) return c.crater ? c.crater(nullptr, from, msg, size) : net_crater_in(msg, size);
  NetMech* m = net_mech(from);
  if (!m) {
    if (type != 0xf) return;
    if (c.create) c.create(nullptr, from, msg, size);
    else net_mech_create(from);
    if (!(m = net_mech(from))) return;
    // Where it is first, from the state's position, then the state applied.
    float pos[3] = {};
    std::memcpy(pos, msg + 4, size >= 16 ? 12 : size > 4 ? size - 4 : 0);
    if (c.set_position) c.set_position(*m, pos);
    m->heard = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
    net_route(from, msg, size);
    if (c.reckon) c.reckon(*m, ztimer::state().dt);
    return;
  }
  switch (type) {
    case 0xf: return c.state ? c.state(m, from, msg, size) : net_state_apply(*m, msg, size);
    case 0x10: return c.ping ? c.ping(m, from, msg, size) : net_ping_in(*m, msg, size);
    case 0x11: return c.clock ? c.clock(m, from, msg, size) : net_clock_in(msg, size);
    case 0x12: {
      if (c.respawn) return c.respawn(m, from, msg, size);
      float pos[3] = {0, 0, 0};
      if (size >= 16) std::memcpy(pos, msg + 4, 12);
      return net_respawn_in(*m, pos);
    }
    case 0x13: return c.kill ? c.kill(m, from, msg, size) : net_kill_in(msg, size);
    case 0x14: return c.scores ? c.scores(m, from, msg, size) : net_scores_in(msg, size);
    case 0x16: return call(c.rules, m, from, msg, size);
    case 0x17: return c.posture ? c.posture(m, from, msg, size) : net_posture_in(*m, msg, size);
    case 0x18: return c.knockback ? c.knockback(m, from, msg, size) : net_shove_in(*m, msg, size);
    case 0x1a: return c.effect ? c.effect(m, from, msg, size) : net_effect_in(msg, size);
    case 0x1b: return c.drop ? c.drop(m, from, msg, size) : net_drop_in(*m, msg, size);
    case 0x1f: return call(c.chat, m, from, msg, size);
    case 0x21: return c.part_off ? c.part_off(m, from, msg, size) : net_part_off_in(msg, size);
    default: return;  // 0x15 is registered but not handled in a mission
  }
}

// Mech3.exe 1.2 @ 0x0046d3b0. 0x40800000 is 4 s.
void net_player_left(uint32_t from, const uint8_t* msg, size_t size) {
  (void)msg, (void)size;
  NetMech* m = net_mech(from);
  if (!m) return;
  if (g_calls.hud_message) g_calls.hud_message(4.0f, 0x26a, znet::player_name(from));
  if (g_calls.forget) g_calls.forget(*m);
  g_calls.free ? g_calls.free(m) : net_mech_delete(m);
  if (g_calls.lobby_left) g_calls.lobby_left(from);
}

namespace {
// ftol of x to a byte, x clamped to -127..127 first (the controls' axes).
uint8_t axis_byte(float x) {
  float v = x * 127.0f;  // [0x005c33e8]
  if (v > 127.0f) v = 127.0f;
  else if (v < -127.0f) v = -127.0f;  // [0x005c33ec]
  return static_cast<uint8_t>(crt::msvc_ftol(v));
}
// A part's fraction left as a byte: 1 is 255, 0 is 0, else ceil(f x 255).
uint32_t fraction_byte(float f) {
  if (f == 1.0f) return 0xff;
  if (f == 0.0f) return 0;
  return static_cast<uint32_t>(crt::msvc_ftol(std::ceil(static_cast<double>(f * 255.0f)))) & 0xff;
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
void put_f(std::vector<uint8_t>& b, size_t at, float v) { std::memcpy(b.data() + at, &v, 4); }
void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) { std::memcpy(b.data() + at, &v, 2); }
float game_time() { return g_calls.game_time ? g_calls.game_time() : 0.0f; }
}  // namespace

// The 0xf message of 0x0046f160. Its bytes that nothing writes are 0 (the
// exe's static buffer has 0 there; a malloc'd one, whatever the heap held).
std::vector<uint8_t> net_state_message(Mech& m) {
  const NetCalls& c = g_calls;
  const auto shots = static_cast<uint32_t>(m.net_shots.size());
  uint32_t parts = 0;  // bits 0..9 counted, though only 1..8 are written
  for (int i = 9; i >= 0; --i) parts += (m.net_damaged >> i & 1) != 0;
  const size_t head = 0x3c + shots;
  std::vector<uint8_t> b(head + 4 * parts, 0);
  put16(b, 0, 0xf);
  put16(b, 2, static_cast<uint16_t>(b.size()));
  float pos[3] = {0, 0, 0};
  if (c.position) c.position(m, pos);
  std::memcpy(b.data() + 4, pos, 12);
  put16(b, 0x10, static_cast<uint16_t>(crt::msvc_ftol(m.velocity.x * 127.0f)));
  put16(b, 0x12, static_cast<uint16_t>(crt::msvc_ftol(m.velocity.y * 127.0f)));
  put16(b, 0x14, static_cast<uint16_t>(crt::msvc_ftol(m.velocity.z * 127.0f)));
  put_f(b, 0x18, static_cast<float>(std::atan2(static_cast<double>(m.orientation[2]), m.orientation[0])));
  float angles[3] = {0, 0, 0};
  if (m.torso) zmth::quat_angles(m.torso->turn_q, angles);  // 0x00525f80
  put_f(b, 0x1c, angles[0]);
  put_f(b, 0x20, angles[1]);
  const MechControls* ctl = m.controls;
  b[0x24] = axis_byte(ctl ? ctl->throttle_target : 0.0f);  // IControls vt+0x2c
  b[0x25] = axis_byte(ctl ? ctl->turn : 0.0f);             // vt+0x24
  // The aim: from the torso's node to the weapons' aim point, as a turn off
  // straight ahead (0, 0, -1) and a distance.
  float at[3] = {0, 0, 0};
  if (c.torso_position) c.torso_position(m, at);
  const auto* w = static_cast<const WeaponManager*>(m.ammo);
  zmth::Vec3 d{(w ? w->aim.x : 0.0f) - at[0], (w ? w->aim.y : 0.0f) - at[1], (w ? w->aim.z : 0.0f) - at[2]};
  const float range = zmth::normalize(d);  // 0x004020d0
  const float ahead[3] = {0.0f, 0.0f, -1.0f};
  float q[4], turn[3];
  zmth::quat_between(ahead, &d.x, q);  // 0x00525a50
  zmth::quat_log(q, turn);             // 0x005259c0
  put_f(b, 0x28, turn[0]);
  put_f(b, 0x2c, turn[1]);
  put_f(b, 0x30, range);
  // The flags: 0 nothing struck last, 1 inactive, 2-6 the motion state, 7-10
  // the first action, 11 limping, 12-18 the shots, 19-22 the parts, 23
  // passive, 24 powered down, 25 MASC, 26 the AMS on, 27 NARCed, 30 jumping,
  // 31 crouched (docs/network-messages.md).
  uint32_t f = m.collider.clear == 0 ? 1u : 0u;
  f |= (m.active ? 0u : 1u) << 1;
  f |= (static_cast<uint32_t>(m.anim_state) & 0x1f) << 2;
  f |= (m.actions.empty() ? 0u : static_cast<uint32_t>(m.actions.front()) & 0xf) << 7;
  f |= (m.limp != 0 ? 1u : 0u) << 11;  // -1 (none) too
  f |= (shots & 0x7f) << 12;
  f |= (parts & 0xf) << 19;
  f |= ((m.xbc >> 2 & 1) != 0 ? 1u : 0u) << 23;  // passive radar
  f |= (m.is_powered_down() ? 1u : 0u) << 24;  // vt+0xa0
  f |= (m.masc_time != 0.0f ? 1u : 0u) << 25;
  f |= (m.ams && m.ams->on ? 1u : 0u) << 26;
  f |= (m.narc_time > 0.0f ? 1u : 0u) << 27;
  f |= (ctl && ctl->jump != 0.0f ? 1u : 0u) << 30;  // IControls vt+0x38
  f |= (m.crouched ? 1u : 0u) << 31;
  put32(b, 0x38, f);
  put32(b, 0x34, c.target_player ? c.target_player(m) : 0);
  size_t at_shot = 0x3c;
  for (const NetShot& s : m.net_shots)
    b[at_shot++] = static_cast<uint8_t>((s.slot & 0xf) | (s.trigger & 3) << 4 | (s.target != 0) << 6);
  size_t at_part = head;
  uint32_t done = 0;
  for (int32_t id = 1; done < parts && id <= 8; ++id) {
    if (!(m.net_damaged & 1u << id)) continue;
    const int32_t sub = id == 7 ? 1 : id == 8 ? 2 : 0;  // the side torsos are the torso's sections
    const MechPart* p = part_by_id(m, id);
    uint32_t v = static_cast<uint32_t>(id) & 0xff;
    if (p) {
      v |= fraction_byte(p->get_internal(sub)) << 8;  // vt+0x48
      v |= fraction_byte(p->get_armor(sub)) << 16;    // vt+0x44
      v |= fraction_byte(p->get_armor(sub + 3)) << 24;
    }
    put32(b, at_part, v);
    at_part += 4, ++done;
  }
  return b;
}

NetMech* net_mech_of(Handle vehicle) {
  for (NetMech* m : g_net.mechs)
    if (m == vehicle) return m;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00470ab0
uint32_t net_next_player() {
  if (g_net.mechs.empty()) return 0;
  bool next = false;
  for (const NetMech* m : g_net.mechs) {
    if (next) return g_net.ping_turn = m->owner;
    next = m->owner == g_net.ping_turn;
  }
  return g_net.ping_turn = g_net.mechs.front()->owner;
}

// Mech3.exe 1.2 @ 0x00470b40
float net_send_period(int32_t players) {
  if (g_net.rate_setting == 1) return 0.1f;
  if (g_net.rate_setting != 0 && g_net.rate_setting != 2) return 0.25f;
  switch (players) {
    case 2: case 3: case 4: return 1.0f / 7.0f;
    case 5: return 0.2f;
    case 6: return 0.25f;
    case 7: return 2.0f / 7.0f;
    case 8: return 1.0f / 3.0f;
    default: return 0.5f;
  }
}

// Mech3.exe 1.2 @ 0x0046f160
int32_t net_send_event(Mech& m, int32_t kind, intptr_t arg) {
  if (!g_net.running || m.remote) return 0;  // a network mech's vt+0x168 is 0x00410490: 0
  const NetCalls& c = g_calls;
  switch (kind) {
    case 0xf: {
      std::vector<uint8_t> b = net_state_message(m);
      const uint32_t id = znet::send(b.data(), static_cast<uint32_t>(b.size()), false, 0);
      m.net_shots.clear();  // the records deleted (0x00470bc0)
      m.net_damaged = 0;
      return static_cast<int32_t>(id);
    }
    case 0x10: {  // the ping, to the next player round the list
      const uint32_t id = g_net.ping_counter++;
      // FIX (trap): the host's every tenth ping checked the locked rules
      // block's CRC (0x00576c10 on the game's +0x60, its +0x88) and ended
      // the game (0x004707c0) when they differed, after folding the live
      // flags +0x8c / +0x8d back into it for the next check: a guard against
      // a memory editor, left out with its folds (protection.md section 4).
      const uint32_t to = net_next_player();
      NetMech* them = to ? net_mech(to) : nullptr;
      if (!them) return 0;
      const uint32_t now = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
      uint8_t b[0x14] = {0x10, 0, 0x14, 0};
      const uint32_t words[4] = {id, them->ping_in[1], g_net.acked_time, now - them->ping_heard};
      std::memcpy(b + 4, words, 16);
      znet::send(b, sizeof b, false, to);
      them->ping_sent = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
      them->ping_id = id;
      return 0;
    }
    case 0x15:  // a part's damage goes with the next state
      m.net_damaged |= 1u << (static_cast<uint32_t>(arg) & 31);
      return 0;
    case 0x16: {  // a shot, with the next state
      const auto* w = reinterpret_cast<const Weapon*>(arg);
      NetShot s;
      s.slot = w->slot;
      s.trigger = m.ammo ? static_cast<const WeaponManager*>(m.ammo)->state : 0;
      const NetMech* t = w->locked && w->projectile_target ? net_mech_of(w->projectile_target) : nullptr;
      s.target = t ? t->owner : 0;
      m.net_shots.push_back(s);
      return 0;
    }
    case 0x12: {  // made again (the respawn), at its place
      uint8_t b[0x10] = {0x12, 0, 0x10, 0};
      float pos[3] = {0, 0, 0};
      if (c.position) c.position(m, pos);
      std::memcpy(b + 4, pos, 12);
      znet::send(b, sizeof b, true, 0);
      return 0;
    }
    case 0x13: {  // killed (arg its killer): to all, then as if heard
      const uint32_t victim = znet::local_id();  // vt+0x160, 0x0041ec50
      uint32_t killer = victim;
      if (arg) {
        const NetMech* k = net_mech_of(reinterpret_cast<Handle>(arg));  // vt+0x58
        killer = k ? k->owner : znet::local_id();  // else its vt+0x160
      }
      uint8_t b[0xc] = {0x13, 0, 0xc, 0};
      std::memcpy(b + 4, &victim, 4);
      std::memcpy(b + 8, &killer, 4);
      znet::send(b, sizeof b, true, 0);
      c.kill ? c.kill(nullptr, 0, b, sizeof b) : net_kill_in(b, sizeof b);  // 0x0046eba0
      return 0;
    }
    case 0x17: {  // an action queued (arg its kind)
      uint8_t b[8] = {0x17, 0, 8, 0};
      const auto a = static_cast<int32_t>(arg);
      std::memcpy(b + 4, &a, 4);
      znet::send(b, sizeof b, false, 0);
      return 0;
    }
    case 0x21: {  // a network mech's part shot off here (arg the part)
      const auto* p = reinterpret_cast<const MechPart*>(arg);
      const NetMech* t = p ? net_mech_of(p->mech) : nullptr;  // 0x00460000, vt+0x58
      const uint32_t words[3] = {znet::local_id(), t ? t->owner : 0, static_cast<uint32_t>(p && p->node ? p->id : -1)};
      uint8_t b[0x10] = {0x21, 0, 0x10, 0};
      std::memcpy(b + 4, words, 12);
      znet::send(b, sizeof b, false, 0);
      return 0;
    }
    case 0x11: {  // the clock (the host's) and the end's reason, arg the game
      if (c.send_other) return c.send_other(m, kind, arg);
      const auto* gf = reinterpret_cast<const NetGameFields*>(arg);
      uint8_t b[0xc] = {0x11, 0, 0xc, 0};
      const uint32_t reason = gf && gf->end_state ? static_cast<uint32_t>(*gf->end_state) & 3 : 0;  // [0x005e5674]'s low bits
      const float left = c.clock_left ? c.clock_left() : 0.0f;  // [0x00609920]
      std::memcpy(b + 4, &reason, 4);
      std::memcpy(b + 8, &left, 4);
      if (znet::is_host()) znet::send(b, sizeof b, false, 0);
      c.clock ? c.clock(nullptr, 0, b, sizeof b) : net_clock_in(b, sizeof b);
      return 0;
    }
    default:
      return 0;
  }
}

// Mech3.exe 1.2 @ 0x0046ef70. The game time is [0x0060991c], the clock left
// [0x00609920] while [0x00609924].
void net_mech_tick(Mech& m) {
  const NetCalls& c = g_calls;
  NetGameFields* g = c.game ? c.game() : nullptr;
  if (!g) return;
  const float now = game_time();
  if (g_net.next_state <= now) {
    if (g_net.resends++ >= 0x19) {
      g_net.resends = 0;
      m.net_damaged = 0x1fe;  // every part, every 26th state
    }
    m.net_sent = static_cast<uint32_t>(net_send_event(m, 0xf, 0));
    g_net.next_state = net_send_period(g->players) + now;
    m.net_acked = 0;
  }
  if (g_net.next_ping <= game_time()) {
    net_send_event(m, 0x10, 0);
    g_net.next_ping = game_time() - -5.1f;  // [0x005c33d8]
  }
  const bool timed = c.clock_on && c.clock_on();
  const float left = c.clock_left ? c.clock_left() : 0.0f;
  if (timed) {
    if (left <= 60.0f && !g_net.warned) {
      g_net.warned = 1;
      g_net.last_second = 0;
      if (c.hud_message) c.hud_message(4.0f, 0xf0b, nullptr);
    }
    if (left <= 1.0f && !g_net.last_second) g_net.last_second = 1;
    if (left < 3.0f && g->end_state && *g->end_state == 0) *g->end_state = 1;
  }
  if (znet::is_host() && g->clock_next && *g->clock_next <= game_time()) {
    net_send_event(m, 0x11, reinterpret_cast<intptr_t>(g));
    *g->clock_next = game_time() - -5.0f;  // [0x005c33e4]
  }
  const int32_t state = g->end_state ? *g->end_state : 0;
  if (state != 0 && state != 3) net_send_event(m, 0x11, reinterpret_cast<intptr_t>(g));
  if (state == 3 && g->end_time && game_time() > *g->end_time) c.game_over ? c.game_over() : net_game_over();
}

namespace {
// The apply's calls on the mech, through the test hook when set.
void mech_do(NetMech& m, uint32_t fn, int32_t arg) {
  if (g_calls.mech_call) return g_calls.mech_call(m, fn, arg);
  switch (fn) {
    case 0x41cc30: m.power(arg); break;
    case 0x41df50: m.masc(arg != 0); break;
    case 0x41ac10: m.mark_narc(arg != 0); break;
    case 0x41bda0: m.crouch(arg != 0); break;
    case 0x415a90: m.end_limp(); break;
    case 0x41d5f0: m.start_limp(arg, false); break;
    case 0x416780: m.queue_action(arg, true); break;
    default: break;
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x0046e4d0. The controls are set through IControls
// (vt+0x20 the throttle, +0x28 the turn, +0x34 the jump). The aim's turn
// is carried on from the last by the latency over the gap since that
// state, and its z dropped then (it is 0 anyway).
void net_state_apply(NetMech& m, const uint8_t* msg, size_t size) {
  const NetCalls& c = g_calls;
  if (size < sizeof(NetStateIn)) return;  // FIX (bug): the exe reads 0x3c bytes whatever came
  std::memcpy(&m.state, msg, sizeof(NetStateIn));
  const NetStateIn& st = m.state;
  const bool inactive = (st.flags >> 1 & 1) != 0;
  if (!inactive) {
    constexpr double kInv127 = 0.00787402;  // [0x005c33c8]
    for (int i = 0; i < 3; ++i) m.velocity_in[i] = static_cast<float>(st.vel[i] * kInv127);
    if (m.controls) {
      m.controls->set_throttle(static_cast<float>(st.throttle * kInv127));
      m.controls->set_turn(static_cast<float>(st.turn * kInv127));
    }
    float tq[4];
    zmth::quat_two_angles(st.torso[1], st.torso[0], tq);  // 0x00525280
    if (m.torso) std::memcpy(m.torso->turn_q, tq, sizeof tq);  // 0x004646f0
    zmth::Vec3 v{st.aim[0], st.aim[1], 0.0f};
    zmth::Quat q;
    zmth::quat_exp(v, q);  // 0x00525900
    const float qn[4] = {q.w, q.x, q.y, q.z};
    float use[4];
    if (m.aim_q[1] == 0.0f || m.aim_q[2] == 0.0f) {
      std::memcpy(use, qn, sizeof use);
    } else {
      const uint32_t now = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
      const double gap = static_cast<double>(now - m.heard);
      // FIX (bug): the latency (seconds) in milliseconds, as the gap is; the
      // exe adds the seconds, a correction of about 0.1%.
      const double lag = zfix::options().bugs ? m.latency * 1000.0 : m.latency;
      const double f = gap > 1.0 ? (lag + gap) / gap : 1.0;
      for (int i = 0; i < 3; ++i) use[i] = static_cast<float>((qn[i] - m.aim_q[i]) * f + m.aim_q[i]);
      use[3] = 0.0f;
    }
    std::memcpy(m.aim_q, qn, sizeof qn);
    m.heard = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
    const zmth::Quat uq{use[0], use[1], use[2], use[3]};
    const zmth::Vec3 ahead{0.0f, 0.0f, -st.range};
    zmth::Vec3 dir;
    zmth::quat_rotate(uq, ahead, dir);  // 0x00525840
    float at[3] = {0, 0, 0};
    if (c.torso_position) c.torso_position(m, at);
    const Vec3f aim{at[0] + dir.x, at[1] + dir.y, at[2] + dir.z};
    if (c.aim) c.aim(m, &aim.x);
    else if (auto* w = static_cast<WeaponManager*>(m.ammo)) w->set_aim(&aim);  // 0x004b0bc0
    m.heading_q[0] = static_cast<float>(std::cos(static_cast<double>(st.heading)));
    m.heading_q[1] = 0.0f;
    m.heading_q[2] = static_cast<float>(std::sin(static_cast<double>(st.heading)));
    m.heading_q[3] = 0.0f;
    const uint8_t passive = static_cast<uint8_t>(st.flags >> 23 & 1);
    if (passive != (m.xbc >> 2 & 1)) {
      m.xbc = static_cast<uint8_t>((m.xbc & ~4) | passive << 2);
      if (c.radar_toggled) c.radar_toggled(m);  // vt+0xb8
    }
    Handle target = nullptr;
    if (st.target == znet::local_id()) target = c.pilot_mech ? c.pilot_mech() : nullptr;
    else target = net_mech(st.target);  // 0x00470c20
    if (c.pick_target) c.pick_target(m, target);  // 0x004a7b40
  }
  // The shots and the damaged parts after the fixed part: the parts follow
  // the shots, and are taken only when the message holds more than them.
  if (size >= 4) {
    uint16_t full;
    std::memcpy(&full, msg + 2, 2);
    if (full > 0x3c && full <= size) {
      const uint32_t shots = st.flags >> 12 & 0x7f, parts = st.flags >> 19 & 0xf;
      const uint8_t* shot = shots ? msg + 0x3c : nullptr;
      const uint8_t* part = shots == 0 ? msg + 0x3c : shots + 0x3c < full ? msg + 0x3c + shots : nullptr;
      if (!inactive && shot) {
        auto* w = static_cast<WeaponManager*>(m.ammo);
        // FIX (A): a peer's packet is untrusted over the internet (the exe
        // trusted DirectPlay's peers): the shots it holds, not the count's (fixes.md).
        for (uint32_t k = 0; k < shots && 0x3c + k < full; ++k) {
          const uint8_t b = shot[k];
          const uint32_t at = b & 0x40 ? st.target : 0;  // a player id, as the exe passes it
          if (c.fire) c.fire(m, b & 0xf, at, b >> 4 & 3);
          else if (w) w->net_fire(b & 0xf, reinterpret_cast<Handle>(static_cast<uintptr_t>(at)), b >> 4 & 3);  // 0x004b08c0
        }
      }
      for (uint32_t k = 0; part && k < parts && part + 4 * k + 4 <= msg + full; ++k) {
        uint32_t word;
        std::memcpy(&word, part + 4 * k, 4);
        const uint32_t id = word & 0xff;
        if (id == 0 || id >= 9) continue;
        const int32_t sub = id == 7 ? 1 : id == 8 ? 2 : 0;
        MechPart* p = part_by_id(m, static_cast<int32_t>(id));
        if (!p) continue;
        constexpr float k255 = 1.0f / 255.0f;  // [0x005e56ec]
        p->set_internal(static_cast<float>(word >> 8 & 0xff) * k255, sub);  // vt+0x3c
        p->set_armor(static_cast<float>(word >> 16 & 0xff) * k255, sub);    // vt+0x40
        p->set_armor(static_cast<float>(word >> 24) * k255, sub + 3);
      }
    }
  }
  const uint32_t f = st.flags;
  const bool down = (f >> 24 & 1) != 0;
  if (down && !m.is_powered_down()) mech_do(m, 0x41cc30, 1);  // vt+0xa0
  else if (!down && m.is_powered_down()) mech_do(m, 0x41cc30, 0);
  if (m.ams) m.ams->on = (f >> 26 & 1) != 0;
  if ((f >> 25 & 1) != (m.masc_time != 0.0f ? 1u : 0u)) mech_do(m, 0x41df50, static_cast<int32_t>(f >> 25 & 1));
  if ((f >> 27 & 1) != (m.narc_time > 0.0f ? 1u : 0u)) mech_do(m, 0x41ac10, static_cast<int32_t>(f >> 27 & 1));
  if ((m.crouched ? 1u : 0u) != (f >> 31)) mech_do(m, 0x41bda0, static_cast<int32_t>(f >> 31));
  if (m.controls) m.controls->set_jump((f >> 30 & 1) ? 1.0f : 0.0f);
  const int32_t state = static_cast<int32_t>(f >> 2 & 0x1f);
  bool respawn = false;
  if (m.anim_state != state) {
    switch (m.anim_state) {
      case 0:
        if (state != 1 && static_cast<uint32_t>(state) < 9) mech_do(m, 0x415a90, 0);
        break;
      case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
        if (state == 0) mech_do(m, 0x41d5f0, static_cast<int32_t>(f >> 11 & 1));
        break;
      case 10: respawn = true; break;
      case 12: if (state <= 9) mech_do(m, 0x416780, 6); break;
      case 13: if (state <= 9) mech_do(m, 0x416780, 5); break;
      default: break;
    }
  }
  if (respawn) c.respawn_at ? c.respawn_at(m, st.pos) : net_respawn_in(m, st.pos);  // 0x0046ed30 with the static 0x12 [0x005e55f8]
}

// Mech3.exe 1.2 @ 0x0046ed70
void net_reckon(NetMech& m, float dt) {
  const NetCalls& c = g_calls;
  if (m.state.flags >> 1 & 1) return;
  float p[3] = {0, 0, 0};
  if (c.position) c.position(m, p);  // 0x005a85e0
  float q[4];
  if (m.state.flags & 1) {  // to the state's place and heading at once
    std::memcpy(p, m.state.pos, sizeof p);
    std::memcpy(q, m.heading_q, sizeof q);
  } else {
    float k = fast_exp(dt * g_net.reckon_gain);
    if (k > 1.0f) k = 1.0f;
    else if (k < 0.0f) k = 0.0f;
    const float j = 1.0f - k;  // 0x0051e380: p toward the state's place
    p[0] = j * m.state.pos[0] + k * p[0];
    p[1] = m.state.pos[1] * j + k * p[1];
    p[2] = m.state.pos[2] * j + k * p[2];
    for (int i = 0; i < 3; ++i) m.state.pos[i] += m.velocity_in[i] * dt;
    const float damp = fast_exp(dt * g_net.reckon_damp);
    for (float& v : m.velocity_in) v *= damp;
    zmth::quat_slerp(m.orientation, m.heading_q, k, q);  // 0x00525ce0
  }
  if (c.set_position) c.set_position(m, p);  // vtable +0x18
  if (m.transform) zmth::quat_matrix(q, m.transform);  // 0x00525740
  std::memcpy(m.orientation, q, sizeof q);
  m.entity_moved();  // 0x005a85c0
}

// Mech3.exe 1.2 @ 0x00470a00. 0.0005 halves the round trip into seconds.
void net_ping_in(NetMech& m, const uint8_t* msg, size_t size) {
  const NetCalls& c = g_calls;
  uint32_t w[5] = {};
  std::memcpy(w, msg, std::min<size_t>(size, sizeof w));
  if (m.ping_id == w[2]) {
    const uint32_t now = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
    // FIX (bug): the round trip less their delay read as signed, as the clamp
    // below means it; the exe loads it unsigned (fild of a zero-topped qword),
    // so a peer's delay past our round trip (a stall, their clock's rate, a
    // bad packet) made 2^32 ms, and their mech's arms flailed (the aim's
    // extrapolation by it) until the next ping (fixes.md).
    const uint32_t trip = now - m.ping_sent - w[4];
    const double t = zfix::options().bugs ? static_cast<double>(static_cast<int32_t>(trip)) : static_cast<double>(trip);
    const double l = (t - static_cast<double>(g_net.acked_time - w[3])) * 0.0005;  // [0x005c3408]
    m.latency = l <= 0.0 ? 0.0f : static_cast<float>(l);
  } else {
    m.latency = 0;
  }
  std::memcpy(m.ping_in, w, sizeof w);
  m.ping_heard = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
}

// Mech3.exe 1.2 @ 0x004709d0. The exe's message held a pointer to
// DirectPlay's DPMSG_SENDCOMPLETE (+0x1c the id, +0x24 the send time); ours
// has them as its words (multiplayer-port.md piece 2).
void net_send_done(const uint8_t* msg, size_t size) {
  if (size < 12) return;
  uint32_t id, sent;
  std::memcpy(&id, msg + 4, 4);
  std::memcpy(&sent, msg + 8, 4);
  Mech* p = g_calls.pilot_mech ? g_calls.pilot_mech() : nullptr;
  if (p && id == p->net_sent) {
    p->net_acked = 1;
    g_net.acked_time = sent;
  }
}

// Mech3.exe 1.2 @ 0x0046ed10
void net_posture_in(NetMech& m, const uint8_t* msg, size_t size) {
  int32_t kind = 0;
  if (size >= 8) std::memcpy(&kind, msg + 4, 4);
  mech_do(m, 0x416780, kind);
}

// Mech3.exe 1.2 @ 0x0046ed30 (ECX the mech, its message's place)
void net_respawn_in(NetMech& m, const float pos[3]) {
  if (m.active) return;
  g_calls.rebuild ? g_calls.rebuild(m) : net_mech_rebuild(m);  // 0x0046e0f0
  g_calls.reset_at ? g_calls.reset_at(m, pos) : m.reset_at(pos);  // 0x00418e90
}

// Mech3.exe 1.2 @ 0x0046e0f0. The mech is made again in its own memory:
// its destructor run (vt+0 with 0: off the lists, its parts and models
// gone; the controls kept), then the constructor 0x0046e280 with the same
// player, controls and loadout, the type's data and name again; then its
// sensors, its skin by the old one's name, its team, the state's snap bit
// off. Without controls or a loadout it is not made again.
void net_mech_rebuild(NetMech& m) {
  const NetCalls& c = g_calls;
  const int32_t roster = m.pilot ? m.pilot->roster : 0;
  MechData* data = c.data ? c.data(roster) : mech_build_hooks().mech_data(roster, 1);  // 0x00430d10
  if (!data) return;
  NetControls* ctl = static_cast<NetControls*>(m.controls);
  m.controls = nullptr;
  char name[64] = {}, skin[64] = {};
  if (const char* n = c.mech_name ? c.mech_name(m) : znet::player_name(m.owner)) std::snprintf(name, sizeof name, "%s", n);
  const auto* old_skin = static_cast<const Skin*>(m.decals);
  if (old_skin && old_skin->name) std::snprintf(skin, sizeof skin, "%s", old_skin->name);
  const uint32_t owner = m.owner;
  Config* cfg = m.config;
  NetMech* self = &m;
  if (ctl && cfg) {
    net_mech_unlist(self);
    self->release();
    self->~NetMech();
    new (self) NetMech();
    net_mech_construct(self, owner, data, ctl, cfg, name);
  }
  if (c.sensor_range) c.sensor_range(*self);  // 0x004d6d20
  const MechDfn* dfn = c.dfn ? c.dfn(self->pilot ? self->pilot->roster : roster) : mech_dfn(roster);
  Handle s = dfn && dfn->skins ? dfn->skins->named(skin) : nullptr;  // 0x00475b00
  c.skin ? c.skin(*self, s) : (void)mech_set_skin(*self, s);
  self->set_team(c.team_of ? c.team_of(owner) : 0);
  self->state.flags &= ~1u;
}

namespace {
// A mech by player for the kill's line: the network mech's, the pilot's,
// else none.
Mech* player_mech(uint32_t id) {
  if (NetMech* m = net_mech(id)) return m;
  if (id == znet::local_id() && g_calls.pilot_mech) return g_calls.pilot_mech();
  return nullptr;
}
const char* name_of(Mech* m) {
  if (!m) return "Unknown";  // [0x005e56f0]
  if (m->remote) return znet::player_name(static_cast<NetMech*>(m)->owner);  // 0x004709c0
  return g_calls.mech_name ? g_calls.mech_name(*m) : nullptr;  // 0x0041df30
}
}  // namespace

// Mech3.exe 1.2 @ 0x0046eba0 (ECX the message). 0x40a00000 is 5 s.
void net_kill_in(const uint8_t* msg, size_t size) {
  const NetCalls& c = g_calls;
  if (size < 12) return;
  uint32_t victim, killer;
  std::memcpy(&victim, msg + 4, 4);
  std::memcpy(&killer, msg + 8, 4);
  const char* v = name_of(player_mech(victim));
  const char* k = name_of(player_mech(killer));
  if (c.hud_message) {
    if (victim == killer) c.hud_message(5.0f, 0xf0a, v);  // "%1 has managed to kill himself"
    else if (c.hud_message2) c.hud_message2(5.0f, 0xf09, v, k);  // "%1 has been killed by %2"
  }
  if (znet::is_host()) net_score_kill(victim, killer);
}

// Mech3.exe 1.2 @ 0x00470170. A kill counts only between teams; reaching
// the score limit ends the game (state 2) at once.
void net_score_kill(uint32_t victim, uint32_t killer) {
  const NetCalls& c = g_calls;
  if (!znet::is_host()) return;
  NetGameFields* g = c.game ? c.game() : nullptr;
  if (!g) return;
  if (victim == killer) {
    if (c.lobby_suicide) c.lobby_suicide(victim);
  } else if ((c.team_of ? c.team_of(killer) : 0) != (c.team_of ? c.team_of(victim) : 0)) {
    if (c.lobby_kill) c.lobby_kill(killer);
    const auto score = c.lobby_score ? c.lobby_score : c.lobby_kills;  // 0x00435a50
    if ((score ? score(killer) : 0) >= g->score_limit) {
      if (g->end_state && *g->end_state == 0) *g->end_state = 2;
      if (g->clock_next) *g->clock_next = game_time();
    }
  }
  if (c.scores_reset) c.scores_reset();  // 0x004361e0
  std::vector<uint32_t> list(3 * static_cast<size_t>(std::max(g->players, 0)), 0);
  const int32_t n = c.lobby_scores ? c.lobby_scores(list.data(), g->players) : 0;
  std::vector<uint8_t> b(static_cast<size_t>(g->players) * 0xc + 6, 0);
  put16(b, 0, 0x14);
  put16(b, 2, static_cast<uint16_t>(b.size()));
  put16(b, 4, static_cast<uint16_t>(g->players));
  std::memcpy(b.data() + 6, list.data(), 4 * 3 * static_cast<size_t>(std::min(n, g->players)));
  znet::send(b.data(), static_cast<uint32_t>(b.size()), true, 0);
  if (c.hud_scores) c.hud_scores();  // 0x00446ad0
}

// Mech3.exe 1.2 @ 0x004708e0
void net_scores_in(const uint8_t* msg, size_t size) {
  const NetCalls& c = g_calls;
  if (!(c.game && c.game()) || size < 6) return;
  int16_t n;
  std::memcpy(&n, msg + 4, 2);
  for (int32_t k = 0; k < n && 6 + 12 * static_cast<size_t>(k + 1) <= size; ++k) {
    uint32_t w[3];
    std::memcpy(w, msg + 6 + 12 * k, 12);
    if (c.lobby_set_kills) c.lobby_set_kills(w[0], w[1]);
    if (c.lobby_set_deaths) c.lobby_set_deaths(w[0], w[2]);
  }
  if (c.scores_reset) c.scores_reset();
  if (c.hud_scores) c.hud_scores();
}

// Mech3.exe 1.2 @ 0x00470320 (the collision 0x00423c04)
void net_send_shove(Mech& other, float damage, float vx, float vz) {
  const NetMech* t = net_mech_of(&other);  // vt+0x58
  uint8_t b[0x10] = {0x18, 0, 0x10, 0};
  const float f[3] = {damage, vx, vz};
  std::memcpy(b + 4, f, 12);
  znet::send(b, sizeof b, false, t ? t->owner : 0);
}

// Mech3.exe 1.2 @ 0x004705b0 (the sender's mech, the message): its push on
// the pilot, whose torso takes the damage. FIX (bug): the pusher is the
// pilot's attacker (the exe sets the sender's own +0x664 to itself, so a
// death by the push credits no one).
void net_shove_in(NetMech& from, const uint8_t* msg, size_t size) {
  Mech* p = g_calls.pilot_mech ? g_calls.pilot_mech() : nullptr;
  if (!p || size < 16) return;
  float f[3];
  std::memcpy(f, msg + 4, 12);
  if (zfix::options().bugs) p->attacker = &from;
  else from.attacker = &from;
  if (p->torso) p->torso->apply_damage(f[0], 0);  // vt+0x38
  p->velocity.x += f[1];
  p->velocity.z += f[2];
}

// Mech3.exe 1.2 @ 0x0046fff0: for the pilot only, its part's structure to 0.
void net_part_off_in(const uint8_t* msg, size_t size) {
  if (size < 16) return;
  uint32_t w[3];
  std::memcpy(w, msg + 4, 12);
  if (w[1] != znet::local_id()) return;
  Mech* p = g_calls.pilot_mech ? g_calls.pilot_mech() : nullptr;
  MechPart* part = p ? part_by_id(*p, static_cast<int32_t>(w[2])) : nullptr;
  if (!part) return;
  p->attacker = net_mech(w[0]);
  part->set_internal(0.0f, 0);  // vt+0x3c
}

namespace {
// The 0x19 message (0x1c bytes): +4 bit 7 from the host, +8 the ground's
// material by index, +0xc the place, +0x18 the radius.
struct CraterMsg {
  uint16_t type = 0x19, size = 0x1c;
  uint32_t flags = 0;
  uint32_t material = 0;
  float at[3] = {};
  float radius = 0;
};
static_assert(sizeof(CraterMsg) == 0x1c, "the crater message");
int32_t make_crater(const zwep::CraterOrder* o) {
  return g_calls.crater_make ? g_calls.crater_make(o) : zdec::crater_make(o);  // 0x004ffaa0
}
}  // namespace

// Mech3.exe 1.2 @ 0x0046fd30
int net_crater_veto(const zwep::CraterOrder* order) {
  const NetCalls& c = g_calls;
  if (order->radius <= 0.0f) {  // made from a 0x19: its own, passed
    const_cast<zwep::CraterOrder*>(order)->radius = -order->radius;  // the exe writes the caller's order
    return 1;
  }
  Mech* owner = c.node_mech ? c.node_mech(order->owner) : nullptr;  // 0x0040ef10
  Mech* pilot = c.pilot_mech ? c.pilot_mech() : nullptr;
  if (!owner || owner != pilot) return 0;
  CraterMsg m;  // [0x005e5698]
  m.material = c.material_index ? c.material_index(order->material) : zdec::material_word(order->material);  // 0x00535ff0
  m.at[0] = order->at.x, m.at[1] = order->at.y, m.at[2] = order->at.z;
  m.radius = order->radius;
  if (znet::is_host()) net_crater_in(reinterpret_cast<const uint8_t*>(&m), sizeof m);
  else znet::send(&m, sizeof m, true, 0);
  return 0;
}

// Mech3.exe 1.2 @ 0x0046fde0 (ECX the message). The order is the defaults
// (0x004ff920) with the message's ground, place and its radius negated
// (for the veto). The host makes any and, when it is made, sends it to all
// with bit 7; the others make only those.
void net_crater_in(const uint8_t* msg, size_t size) {
  if (size < sizeof(CraterMsg)) return;
  CraterMsg m;
  std::memcpy(&m, msg, sizeof m);
  zwep::CraterOrder o;
  zdec::crater_defaults(&o);
  const bool host = znet::is_host();
  if (!host && !(m.flags & 0x80)) return;
  const auto index = static_cast<int32_t>(m.material);
  // FIX (A): a peer's packet is untrusted over the internet (the exe trusted
  // DirectPlay's peers): a ground past the table, a place beyond 100 km or
  // no number, a radius under 0.25 m (its points all repeat: the clipper
  // reads an empty region) or over 64 m is no crater (fixes.md).
  if (!g_calls.take_material && index >= zmod::materials().capacity) return;
  for (float v : m.at)
    if (!(std::fabs(v) <= 1.0e5f)) return;
  if (!(std::fabs(m.radius) >= 0.25f && std::fabs(m.radius) <= 64.0f)) return;
  o.material = g_calls.take_material ? g_calls.take_material(index) : zmod::take_material(index);  // 0x00536070
  o.radius = -m.radius;
  o.at = {m.at[0], m.at[1], m.at[2]};
  if (make_crater(&o) == 0 && host) {
    m.flags |= 0x80;
    znet::send(&m, sizeof m, true, 0);
  }
}

// Mech3.exe 1.2 @ 0x004700e0: one crater of the host's history, to all.
// The history keeps the ground as its material index (FIX (A), zdec_crater.h).
namespace {
void replay_crater(const uint32_t* w) {
  if (!znet::is_host()) return;
  CraterMsg m;
  m.flags = 0x80;
  m.material = w[2];
  std::memcpy(m.at, w + 6, 12);
  std::memcpy(&m.radius, w + 5, 4);
  znet::send(&m, sizeof m, true, 0);
}
}  // namespace

// Mech3.exe 1.2 @ 0x004700a0, then 0x00500c60 (ECX 0x004700e0, EDX 0)
void net_host_replay() {
  if (znet::is_host()) {
    const int32_t n = zeff::log_count();
    for (int32_t i = 0; i < n; ++i) {
      zeff::AnimLogRecord* r = zeff::log_record(i);
      if (zeff::log_flags(r) & 2) net_anim_sent(r);
    }
  }
  zdec::history_each(replay_crater, nullptr);
}

// Mech3.exe 1.2 @ 0x0046feb0 (ECX the record). The mobile field base
// stations' animations are not sent; none is while one heard is replayed.
void net_anim_sent(zeff::AnimLogRecord* rec) {
  if (std::strstr(rec->name, "mfb_station_01")) return;  // [0x005e4b68], on 0x0050d260's name
  if (g_net.echo) return;  // [0x00605fb8]
  const auto n = static_cast<size_t>(zeff::log_record_size(rec));  // 0x0050ca20
  std::vector<uint8_t> b(n + 4, 0);
  put16(b, 0, 0x1a);
  put16(b, 2, static_cast<uint16_t>(b.size()));
  std::memcpy(b.data() + 4, rec, std::min(n, sizeof *rec));
  znet::send(b.data(), static_cast<uint32_t>(b.size()), true, 0);
}

// Mech3.exe 1.2 @ 0x0046ff50
void net_effect_in(const uint8_t* msg, size_t size) {
  zeff::AnimLogRecord r;
  std::memset(&r, 0, sizeof r);
  if (size > 4) std::memcpy(&r, msg + 4, std::min(size - 4, sizeof r));
  // FIX (A): a peer's packet is untrusted over the internet (the exe trusted
  // DirectPlay's peers): its def's name ends in the record, and the nodes it
  // names (its own, a connector's two) are none (-1) or live ones (fixes.md).
  r.name[sizeof r.name - 1] = 0;
  if (!g_calls.log_replay) {
    auto live = [](int32_t i) {
      const zcls::NodePool& p = zcls::nodes();
      if (i == -1) return true;
      if (i < 0 || i >= static_cast<int32_t>(p.blocks.size()) * p.block_size) return false;
      return (zcls::node_at(i)->link & zcls::kNodeLive) != 0;
    };
    if (!live(r.node)) return;
    if ((r.type == 3 || r.type == 4) && !live(static_cast<int32_t>(r.args[0]))) return;
    if (r.type == 4 && !live(static_cast<int32_t>(r.args[4]))) return;
  }
  if (g_calls.log_has ? g_calls.log_has(&r) : zeff::log_has(&r)) return;  // 0x0050b680
  g_net.echo = 1;
  g_calls.log_replay ? g_calls.log_replay(&r) : (void)zeff::log_replay(&r);  // 0x0050ca90
  g_net.echo = 0;
}

// Mech3.exe 1.2 @ 0x0046ff90 (ECX the station's id)
void net_send_drop(uint32_t station) {
  uint8_t b[8] = {0x1b, 0, 8, 0};
  std::memcpy(b + 4, &station, 4);
  znet::send(b, sizeof b, true, 0);
}

// Mech3.exe 1.2 @ 0x0046ffd0 (the mech, the message)
void net_drop_in(NetMech& m, const uint8_t* msg, size_t size) {
  if (size < 8) return;
  uint32_t id;
  std::memcpy(&id, msg + 4, 4);
  g_calls.station ? g_calls.station(m, id) : field_bases_station_message(&m, id);  // 0x0045a140
}

// Mech3.exe 1.2 @ 0x004707c0: the game's end. The local player's kills
// and deaths in the lobby (0x00435a30 / 0x00435b40, -1 without its slot)
// added to the game's totals (+0x98, +0x9c), the pilot written (opened,
// its header, then the record), a fade to the Multi-Exit State (0x00605fa8,
// which unloads the mission and goes to the main menu), the network end.
void net_game_over() {
  const NetCalls& c = g_calls;
  NetGameFields* g = c.game ? c.game() : nullptr;  // (the exe's unchecked)
  const uint32_t local = znet::local_id();
  if (g) {
    if (g->kills) *g->kills += c.lobby_kills ? c.lobby_kills(local) : -1;
    if (g->deaths) *g->deaths += c.lobby_deaths ? c.lobby_deaths(local) : -1;
  }
  if (c.pilot_save) c.pilot_save();
  if (c.exit_fade) c.exit_fade();  // 0x0048ef00(next 0x00605fa8, 0, 1, 2, [0x005e7274], 0, 0, 0)
  net_end();                       // 0x0046d100
}

// Mech3.exe 1.2 @ 0x00470740 (ECX the message). The reasons: 1 the time,
// 2 the score limit (0x9aa, 0x9ab for 5 s); the game ends 3 s on.
void net_clock_in(const uint8_t* msg, size_t size) {
  const NetCalls& c = g_calls;
  if (size < 12) return;
  uint32_t flags;
  float left;
  std::memcpy(&flags, msg + 4, 4);
  std::memcpy(&left, msg + 8, 4);
  if (c.set_clock_left) c.set_clock_left(left);
  NetGameFields* g = c.game ? c.game() : nullptr;  // FIX (bug): the exe reads it unchecked
  const uint32_t reason = flags & 3;
  if (reason == 0 || !g || !g->end_state) return;
  if (*g->end_state == 0) *g->end_state = static_cast<int32_t>(reason);
  if (reason == 1 && c.hud_message) c.hud_message(5.0f, 0x9aa, nullptr);
  else if (reason == 2 && c.hud_message) c.hud_message(5.0f, 0x9ab, nullptr);
  *g->end_state = 3;
  if (g->end_time) *g->end_time = game_time() - -3.0f;
}

// Mech3.exe 1.2 @ 0x0046e0a0
void net_spawn_offset(float pos[3], int32_t slot) {
  if (slot >= 2 && slot <= 4) pos[0] += 10.0f;
  else if (slot != 1 && slot != 5) pos[0] -= 10.0f;
  if (slot <= 2) pos[2] += 10.0f;
  else if (slot != 3 && slot != 7) pos[2] -= 10.0f;
}

// Mech3.exe 1.2 @ 0x0046d820
bool net_read_record(uint32_t player, std::vector<uint8_t>* out) {
  out->clear();
  uint32_t size = znet::player_data_size(player);
  bool ok = false;
  if (size > 7) {
    out->resize(size);
    ok = znet::get_player_data(player, out->data(), &size);
  }
  if (!ok) out->clear();
  return ok;
}

// Mech3.exe 1.2 @ 0x0046da80. The mech's definition is loaded first
// (0x004846a0) and not used. The record is made by 0x0046dd90 (kind 2,
// zero-filled to its size).
bool net_launch_player(Mech& m, const std::vector<uint32_t>& p, int32_t skin, bool unlimited_ammo, bool outside_view) {
  const NetCalls& c = g_calls;
  const int32_t roster = m.pilot ? m.pilot->roster : 0;
  c.dfn ? (void)c.dfn(roster) : (void)mech_dfn(roster);
  g_net.next_state = 0, g_net.warned = 0, g_net.last_second = 0;
  m.net_sent = 0;
  m.net_acked = 1;
  const uint32_t local = znet::local_id();
  if (p.empty()) return false;
  std::vector<uint32_t> managed;
  const std::vector<uint32_t>* payload=&p;
  if(managed_gameplay_session()) {
    try {
      std::unique_ptr<Config> config(unpack_managed_config(p.data(),p.size()));
      if(config->mech!=roster)return false;
      const auto id=content_skin_id(roster,skin);
      if(id.empty())return false;
      managed=pack_managed_config(*config,id);payload=&managed;
    } catch(const std::exception& error) {std::fprintf(stderr,"mw3 content network: %s\n",error.what());return false;}
  }
  const uint32_t n = (*payload)[0] & 0xffff;
  if(n>payload->size()*4)return false;
  std::vector<uint8_t> rec(n + 0xc, 0);
  const uint32_t kind = 2;
  std::memcpy(rec.data(), &kind, 4);
  std::memcpy(rec.data() + 0xc, payload->data(), n);
  std::memcpy(rec.data() + 8, &roster, 4);
  std::memcpy(rec.data() + 4, &skin, 4);
  m.set_team(c.team_of ? c.team_of(local) : 0);
  if (c.unlimited) c.unlimited(m, unlimited_ammo);
  else if (auto* w = static_cast<WeaponManager*>(m.ammo)) w->set_unlimited(unlimited_ammo);
  znet::set_player_data(local, rec.data(), static_cast<uint32_t>(rec.size()));
  if (c.scores_reset) c.scores_reset();
  if (outside_view && c.view_outside) c.view_outside();
  const int32_t slot = c.start_of ? c.start_of(local) : 0;
  char name[0x20];
  std::snprintf(name, sizeof name, "remote_%d", slot);
  float pos[3] = {0, 0, 0}, yaw = 0;  // 0x0063d870
  if (!c.spawn_point || !c.spawn_point(name, pos, &yaw)) return false;
  net_spawn_offset(pos, slot);
  c.place ? c.place(m, pos, yaw) : m.set_place(pos, yaw);  // vtable +0xdc
  if (c.ff_spawn) c.ff_spawn();
  return true;
}

// Mech3.exe 1.2 @ 0x0046dde0 (ECX the pilot, its mech `m`), from the dead
// list once the pilot has been dead 7.1 s: with respawn off (+0x8c) the
// player watches (+0x8e cleared, the view's camera 4) and stays dead.
// Otherwise its mech is made again where it lay (0x004783c0), its team,
// unlimited ammo (+0x8d) and options set again, the cockpit's explosion
// reset, and it starts at the "remote_N" (N 0..15) farthest from every
// remote mech (the nearest's flat distance, squared), moved by its lobby
// slot; then 0x12 to all, its sensor range, the objectives again, the
// force feedback. Whether it was made again (off the dead list).
bool net_pilot_respawn(Mech& m) {
  const NetCalls& c = g_calls;
  NetGameFields* g = c.game ? c.game() : nullptr;  // the exe reads it unchecked: the dead list asks only in one
  if (!g) return false;
  if (!g->flags || !g->flags[0]) {
    if (g->flags) g->flags[2] = 0;
    if (c.view_camera) c.view_camera(4);
    return false;
  }
  if (c.pilot_set_mech) c.pilot_set_mech(m, m.pilot ? m.pilot->roster : -1);  // 0x004783c0 with +0x684 +0xc
  const uint32_t local = znet::local_id();
  m.set_team(c.team_of ? c.team_of(local) : 0);  // 0x00435910, the masks, [0x0060b56c] = 0
  if (c.unlimited) c.unlimited(m, g->flags[1] != 0);  // 0x004b0220 on +0xd4
  else if (auto* w = static_cast<WeaponManager*>(m.ammo)) w->set_unlimited(g->flags[1] != 0);
  if (c.options_apply) c.options_apply();  // 0x0040cb30
  if (m.pilot && m.pilot->cockpit_explosion && m.pilot->cockpit_model)  // the exe reads +0x228 unchecked
    mech_data_hooks().anim_restart(m.pilot->cockpit_explosion, m.pilot->cockpit_model->node);  // 0x00508460
  const int32_t slot = c.start_of ? c.start_of(local) : 0;  // the lobby's record +0x20 (0x00495b10)
  float pos[3] = {0, 0, 0}, yaw = 0, best = 0;
  if (c.position) c.position(m, pos);  // 0x005a85e0: where it lay, without a start
  for (int32_t i = 0; i < 0x10; ++i) {
    char name[0x20];
    std::snprintf(name, sizeof name, "remote_%d", i);  // [0x005e56e0]
    float at[3], at_yaw = 0;
    if (!c.spawn_point || !c.spawn_point(name, at, &at_yaw)) continue;  // 0x004e0680
    float d = 1.0e8f;  // 0x0046e030: the nearest remote mech, squared flat
    for (NetMech* o : g_net.mechs) {
      float q[3] = {0, 0, 0};
      if (c.position) c.position(*o, q);
      const float dx = at[0] - q[0], dz = at[2] - q[2];
      const float dd = dz * dz + dx * dx;
      if (dd < d) d = dd;
    }
    if (d > best) best = d, std::memcpy(pos, at, sizeof pos), yaw = at_yaw;
  }
  net_spawn_offset(pos, slot);  // 0x0046e0a0
  c.place ? c.place(m, pos, yaw) : m.set_place(pos, yaw);  // vtable +0xdc
  net_send_event(m, 0x12, 0);  // vtable +0x168
  if (c.sensor_range) c.sensor_range(m);  // 0x004d6d20 with its own +0x9c
  if (c.objectives_again) c.objectives_again();  // 0x00491050
  if (c.ff_respawn) c.ff_respawn();
  return true;
}

// Mech3.exe 1.2 @ 0x0046e280
NetMech* net_mech_new(uint32_t owner, MechData* data, NetControls* ctl, Config* cfg, const char* name) {
  auto* m = new NetMech();
  net_mech_construct(m, owner, data, ctl, cfg, name);
  return m;
}
void net_mech_construct(NetMech* m, uint32_t owner, MechData* data, NetControls* ctl, Config* cfg, const char* name) {
  const NetCalls& c = g_calls;
  mech_construct(*m, data, ctl, cfg, name);  // 0x004149f0
  if (c.label_new) m->label = c.label_new(*m);
  m->remote = true;  // vtable 0x005c3508
  m->owner = owner;
  m->collides = false;
  g_net.mechs.push_back(m);
  if (c.world_add) c.world_add(*m);
  if (m->torso) m->torso->set_mode(2);
  m->power(0);
}

// Mech3.exe 1.2 @ 0x0046d450. The record's word 0 is 2 for a mech's.
// FIX (bug): a record too short for a loadout's head gives none, and one
// under 12 bytes reads zeros past its end (the exe reads past both), and the controls are deleted when the type's data does
// not load (the exe leaves them).
void net_mech_create(uint32_t from) {
  const NetCalls& c = g_calls;
  const char* name = znet::player_name(from);
  std::vector<uint8_t> rec;
  uint32_t kind = 0;
  if (net_read_record(from, &rec)) std::memcpy(&kind, rec.data(), 4);
  const size_t size = rec.size();
  if (rec.size() < 0xc) rec.resize(0xc, 0);
  int32_t roster = -1;
  const MechDfn* dfn = nullptr;
  Config* cfg = nullptr;
  uint32_t team = 0;
  int32_t skin = 0;
  if (kind == 2) {
    std::memcpy(&roster, rec.data() + 8, 4);
    dfn = c.dfn ? c.dfn(roster) : mech_dfn(roster);
    team = c.team_of ? c.team_of(from) : 0;
    std::memcpy(&skin, rec.data() + 4, 4);
    char cfg_name[0x80];
    std::snprintf(cfg_name, sizeof cfg_name, "%s's Config", name ? name : "(null)");
    if (size >= 0xc + 0x34) {
      std::vector<uint32_t> packed((size - 0xc) / 4);
      std::memcpy(packed.data(), rec.data() + 0xc, 4 * packed.size());
      if(managed_gameplay_session()) {
        try {
          std::string skin_id;
          cfg=unpack_managed_config(packed.data(),packed.size(),&skin_id);
          if(cfg->mech!=roster||skin_id.empty()) {delete cfg;cfg=nullptr;}
          else {
            // A peer may omit another player's cosmetic package. The stable
            // identity cannot select an unrelated local skin by numeric index.
            skin=content_skin_index(roster,skin_id);
            if(skin<0)skin=0;
          }
        } catch(const std::exception& error) {std::fprintf(stderr,"mw3 content network: %s\n",error.what());delete cfg;cfg=nullptr;}
      } else cfg = c.unpack ? c.unpack(packed.data(), packed.size()) : Config::unpacked(packed.data(), packed.size());
    }
    if (cfg) {
      if (c.valid ? c.valid(*cfg) : cfg->validate(false)) {
        std::free(cfg->name);
        cfg->name = strdup(cfg_name);
        g_net.configs.push_back(cfg);
      } else {
        g_net.missing.push_back(from);
        c.config_free ? c.config_free(cfg) : delete cfg;
        cfg = nullptr;
      }
    }
  }
  if (!dfn || !cfg || roster == -1) return;
  auto* ctl = new NetControls();
  ctl->init();  // 0x004265a0(0, 0)
  MechData* data = c.data ? c.data(roster) : mech_build_hooks().mech_data(roster, 1);
  if (!data) {
    delete ctl;
    return;
  }
  NetMech* m = c.make ? c.make(from, data, ctl, cfg, name) : net_mech_new(from, data, ctl, cfg, name);
  if (!m) return;
  m->set_team(team);
  Handle s = nullptr;
  if (dfn->skins && skin >= 0 && skin < static_cast<int32_t>(dfn->skins->skins.size())) s = dfn->skins->skins[skin];
  c.skin ? c.skin(*m, s) : (void)mech_set_skin(*m, s);
  if (c.sensor_range) c.sensor_range(*m);  // 0x004d6d20 with its own +0x9c
  m->latency = 0;
  if (znet::is_host()) c.host_replay ? c.host_replay() : net_host_replay();
}

// Mech3.exe 1.2 @ 0x0046e3a0, with the destructor 0x0046e3c0 (its
// 0x005a05a0, zEntity3d's, is the Mech's release here).
void net_mech_unlist(NetMech* m) {
  g_net.mechs.remove(m);
  dead_mechs().remove(m);
  if (m->label && g_calls.label_free) g_calls.label_free(m->label);
  m->label = nullptr;
}
void net_mech_delete(NetMech* m) {
  if (!m) return;
  net_mech_unlist(m);
  m->release();
  delete m;
}

}  // namespace mech3
