#include "gamez/zclass/camera.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma STDC FP_CONTRACT OFF

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zrender/zrndr_flare.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zvideo/zvid_ddd3d.h"

namespace zcls {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zClass\\Camera.c";
SceneWalk g_walk;
WorldRenderHooks g_hooks;

constexpr uint32_t kActive = 4, kTreeValid = 0x80000, kOverride = 0x800000;
constexpr uint32_t kDrawn = 0x80000000u, kUnk25 = 0x2000000;
const float kOnes[3] = {1.0f, 1.0f, 1.0f};
const float kZeros[3] = {};
const float kNullOffset[3] = {};   // 0x0063d870, never written

Camera* cam_of(Node* n) { return static_cast<Camera*>(n->data); }
zmth::Mat34* mat(float* p) { return reinterpret_cast<zmth::Mat34*>(p); }
float* nf(Node* n, int off) { return reinterpret_cast<float*>(n->raw + off); }
uint32_t nu(const Node* n, int off) { return n->get<uint32_t>(off); }
void set_nu(Node* n, int off, uint32_t v) { n->put<uint32_t>(off, v); }

// The bounding sphere refresh every class renderer inlines: the box
// (0x004ebdb0) through the top of the stack, its sphere (0x004f9c40).
void refresh_sphere(Node* node) {
  float corners[8][3];
  node_box_corners(node, corners);
  corners_sphere(corners, nf(node, 0x64), nf(node, 0x70));
}

bool is_caster(const Node* node) {
  const zmod::ShadowCasters& c = zmod::shadow_casters();
  for (int32_t i = 0; i < c.count; ++i)
    if (c.list[i]->node == node) return true;
  return false;
}

void push_mask(uint32_t mask) {
  zmod::WalkState& w = zmod::walk_state();
  w.mask[++w.top] = mask;
}
void pop_mask() { --zmod::walk_state().top; }

void draw_model_of(Node* node, uint32_t mask) {
  if (!node->model) return;
  if (g_walk.morph) {
    uint8_t* h = node->model->header;
    uint32_t f;
    std::memcpy(&f, h + 8, 4);
    f |= 8;
    std::memcpy(h + 8, &f, 4);
    std::memcpy(h + 0x24, &g_walk.morph_factor, 4);
  }
  if (zmod::walk_state().draw) zmod::walk_state().draw(node, mask);
}

void draw_children(Node* node, uint32_t mask) {
  if (node->child_count() <= 0) return;
  push_mask(mask);
  for (int32_t i = 0; i < node->child_count(); ++i) render_node(node->children[i], node->child_count());
  pop_mask();
}

void pop_opacity() {
  --g_walk.opacity_top;
  zmod::set_opacity(g_walk.opacity_top < 0 ? 1.0f : g_walk.opacity[g_walk.opacity_top]);
}

// 0x004f4580 (camera), 0x004f4730 (sound), 0x004f4900 (light): the same
// body around a different matrix. No box check here: a node without one
// gets its sphere from whatever the corner buffer held (0x004ebdb0 returns
// early); the worlds' cameras and lights are not drawn as nodes.
int32_t render_posed(Node* node, int32_t mode, float* slot, const float* rot, const float* pos) {
  uint32_t flags = node->flags();
  if (!(flags & kActive)) return 0;
  flags &= ~kUnk25;
  node->set_flags(flags);
  uint32_t mask = zmod::mask_top();
  int32_t result = 0, recomputed = 0;
  if ((mask != 0 && mode > 1) || !(flags & kTreeValid)) {
    if ((nu(node, 0x2c) & 1) || g_walk.recompute || !(flags & kTreeValid)) {
      refresh_sphere(node);
      if (node->flags() & kTreeValid) set_nu(node, 0x2c, nu(node, 0x2c) & ~1u);
      if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
    }
    result = static_cast<int32_t>(zmod::cull_sphere(nf(node, 0x64), &mask, *nf(node, 0x70)));
    if (node->raw[0x24] & 0x80) {
      if (result == 0x20) result = 0;
      mask &= ~0x20u;
    }
  }
  if (result == 0) {
    node->set_flags(node->flags() | kDrawn);
    zmth::push_copy(mat(slot));
    zmth::euler(rot, pos, kOnes);
    if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
    draw_model_of(node, mask);
    draw_children(node, mask);
    zmth::pop();
  }
  if (recomputed) g_walk.recompute = 0;
  return result;
}

// Mech3.exe 1.2 @ 0x004f4580
int32_t render_camera(Node* node, int32_t mode) {
  Camera* d = cam_of(node);
  return render_posed(node, mode, d->floats(0x44), d->floats(0x20), d->floats(0x14));
}

// Mech3.exe 1.2 @ 0x004f4730. Rotation zero, position +0x30.
int32_t render_sound(Node* node, int32_t mode) {
  auto* d = static_cast<Sound*>(node->data);
  return render_posed(node, mode, d->floats(0x48), kZeros, d->floats(0x30));
}

// Mech3.exe 1.2 @ 0x004f4900
int32_t render_light(Node* node, int32_t mode) {
  auto* d = static_cast<Light*>(node->data);
  return render_posed(node, mode, d->floats(0x30), d->floats(0), d->floats(0xc));
}

// Mech3.exe 1.2 @ 0x004f4ab0. The node's world matrix is cached in data
// +0x60 (it is the stack slot): rebuilt from +0x30 when the cache is dirty
// (0x20) or the node is not tree-valid, used as is otherwise.
int32_t render_object3d(Node* node, int32_t mode) {
  uint32_t flags = node->flags();
  if (!(flags & kActive)) return 0;
  flags &= ~kUnk25;
  node->set_flags(flags);
  zmod::DrawState& ds = zmod::draw_state();
  int32_t second = 0;
  if (ds.second_pass && node == g_walk.second_pass_node) second = 1, ds.second_pass = 0;
  auto* d = static_cast<Object3d*>(node->data);
  uint32_t mask = zmod::mask_top();
  int32_t result = 0, recomputed = 0;
  const bool cull = g_walk.hidden_surface ? ((mask != 0 || g_walk.ring > 0) && mode > 1)
                                          : (mask != 0 && mode > 1);
  if (cull || !(flags & kTreeValid)) {
    if ((nu(node, 0x2c) & 1) || g_walk.recompute || !(node->flags() & kTreeValid)) {
      if (!(node->flags() & 0x100)) {
        if (second) ds.second_pass = 1;
        return 0;
      }
      refresh_sphere(node);
      if (node->flags() & kTreeValid) set_nu(node, 0x2c, nu(node, 0x2c) & ~1u);
      if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
    }
    result = static_cast<int32_t>(zmod::cull_sphere(nf(node, 0x64), &mask, *nf(node, 0x70)));
    flags = node->flags();
    if (flags & 0x80) {
      if (result == 0x20) result = 0;
      mask &= ~0x20u;
    }
  }
  if (result == 0) {
    int32_t pushed = 0;
    const uint32_t df = d->get<uint32_t>(0);
    if (!(df & 8)) {
      pushed = 1;
      if (flags & kTreeValid) {
        if (df & 0x20) {
          zmth::push_copy(mat(d->floats(0x60)));
          zmth::multiply(*mat(d->floats(0x30)), 3);
          d->put<uint32_t>(0, d->get<uint32_t>(0) & ~0x20u);
          if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
        } else {
          zmth::push(mat(d->floats(0x60)));
        }
      } else {
        zmth::push_copy(mat(d->floats(0x60)));
        zmth::multiply(*mat(d->floats(0x30)), 3);
        if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
      }
    }
    int32_t caster = 0, saved_shadows = 0, overlay = 0, opacity = 0, color = 0;
    if (is_caster(node)) {
      caster = 1;
      saved_shadows = zmod::shadows_on();
      zmod::set_shadows(0);
    }
    if ((node->flags() & kOverride) && !g_walk.overlay) {
      overlay = 1;
      g_walk.overlay = 1;
      zmod::set_overlay(1);
    }
    if (d->get<uint32_t>(0) & 2) {
      opacity = 1;
      g_walk.opacity[++g_walk.opacity_top] = d->get<float>(4);
      zmod::set_opacity(d->get<float>(4));
    }
    if (d->get<uint32_t>(0) & 4) {
      color = 1;
      WalkColor& c = g_walk.color[++g_walk.color_top];
      std::memcpy(c.rgb, d->floats(8), 12);
      c.weight = d->get<float>(0x14);
      zmod::set_tint(c.rgb, c.weight);
    }
    int32_t visible = 1;
    if (g_walk.hidden_surface && g_walk.ring > 0 && mode != 1 && !g_walk.overlay &&
        (!node->model || static_cast<int32_t>(node->model->count(0x20)) <= 0))
      visible = zmod::hidden_surface(nf(node, 0x64), *nf(node, 0x70));
    if (visible) {
      node->set_flags(node->flags() | kDrawn);
      draw_model_of(node, mask);
      draw_children(node, mask);
    }
    if (caster) zmod::set_shadows(saved_shadows);
    if (overlay) g_walk.overlay = 0, zmod::set_overlay(0);
    if (opacity) pop_opacity();
    if (color) {
      --g_walk.color_top;
      if (g_walk.color_top < 0)
        zmod::set_tint(nullptr, 0.0f);
      else
        zmod::set_tint(g_walk.color[g_walk.color_top].rgb, g_walk.color[g_walk.color_top].weight);
    }
    if (pushed) zmth::pop();
  }
  if (recomputed) g_walk.recompute = 0;
  if (second) ds.second_pass = 1;
  return result;
}

// Mech3.exe 1.2 @ 0x004f4ee0. Data +4 bit 2: its matrix (+8) applies,
// cached at +0x38.
int32_t render_animate(Node* node, int32_t mode) {
  uint32_t flags = node->flags();
  if (!(flags & kActive)) return 0;
  flags &= ~kUnk25;
  node->set_flags(flags);
  auto* d = static_cast<uint8_t*>(node->data);
  uint32_t mask = zmod::mask_top();
  int32_t result = 0, recomputed = 0;
  if ((mask != 0 && mode > 1) || !(flags & kTreeValid)) {
    if ((nu(node, 0x2c) & 1) || g_walk.recompute || !(flags & kTreeValid)) {
      refresh_sphere(node);
      if (node->flags() & kTreeValid) set_nu(node, 0x2c, nu(node, 0x2c) & ~1u);
      if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
    }
    result = static_cast<int32_t>(zmod::cull_sphere(nf(node, 0x64), &mask, *nf(node, 0x70)));
    if (node->raw[0x24] & 0x80) {
      if (result == 0x20) result = 0;
      mask &= ~0x20u;
    }
  }
  if (result == 0) {
    node->set_flags(node->flags() | kDrawn);
    int32_t pushed = 0;
    if (d[4] & 4) {
      pushed = 1;
      zmth::push_copy(reinterpret_cast<zmth::Mat34*>(d + 0x38));
      zmth::multiply(*reinterpret_cast<zmth::Mat34*>(d + 8), 3);
      if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
    }
    draw_model_of(node, mask);
    draw_children(node, mask);
    if (pushed) zmth::pop();
  }
  if (recomputed) g_walk.recompute = 0;
  return result;
}

// Mech3.exe 1.2 @ 0x004f5090. Data: +0 level (1: measure the distance here,
// 0: use the enclosing LoD's), +4 near^2, +8 far, +0xc far^2; grow-in
// +0x10..+0x30 (and +0x44 uniform), fade-out +0x3c/+0x40, fade-in
// +0x34/+0x38, morph +0x48/+0x4c (render.md, LoD). The distances use the
// approximate square root.
int32_t render_lod(Node* node, int32_t mode) {
  float sx = 1.0f, sy = 1.0f, sz = 1.0f;
  int32_t scaled = 0, faded = 0, recomputed = 0;
  uint32_t flags = node->flags();
  if (!(flags & kActive)) return 0;
  auto* d = static_cast<Lod*>(node->data);
  flags &= ~kUnk25;
  node->set_flags(flags);
  auto ret0 = [&]() {
    if (recomputed) g_walk.recompute = 0;
    return 0;
  };
  if (d->get<int32_t>(0) == 0) {
    const float d2 = g_walk.lod[g_walk.lod_level].d2;
    if (d2 < d->get<float>(4)) return 0;
    if (!(d2 < d->get<float>(0xc))) return 0;
  }
  if ((nu(node, 0x2c) & 1) || g_walk.recompute || !(flags & kTreeValid)) {
    refresh_sphere(node);
    if (node->flags() & kTreeValid) set_nu(node, 0x2c, nu(node, 0x2c) & ~1u);
    if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
  }
  WalkLod& e = g_walk.lod[g_walk.lod_level];
  if (d->get<int32_t>(0) != 0) {
    std::memcpy(e.centre, nf(node, 0x64), 12);
    const float* pos = g_walk.camera->floats(0x2c);
    const float dx = pos[0] - e.centre[0], dy = pos[1] - e.centre[1], dz = pos[2] - e.centre[2];
    const float d2 = (dx * dx + dy * dy) + dz * dz;
    e.d2 = g_walk.camera->get<float>(0xd4) * d2;
    if (e.d2 < d->get<float>(4)) return ret0();
    if (!(e.d2 < d->get<float>(0xc))) return ret0();
  }
  const float dist = approx_sqrt(e.d2);
  float far_v = d->get<float>(8);
  float dc = dist > far_v ? far_v : dist;
  auto grow = [&](int on, int len, int k, float* s) {
    if (!(d->get<float>(on) > 0.01f)) return;  // 0x005cdb54
    scaled = 1;
    const float l = d->get<float>(len);
    const float t = far_v - l;
    if (dc > t)
      *s = 1.0f - ((t - dc) * (d->get<float>(k) - 1.0f)) / l;
    else
      *s = 1.0f;
  };
  grow(0x1c, 0x10, 0x28, &sx);
  if (d->get<int32_t>(0x44)) {
    if (d->get<float>(0x1c) > 0.01f) sy = sx, sz = sx;
  } else {
    grow(0x20, 0x14, 0x2c, &sy);
    grow(0x24, 0x18, 0x30, &sz);
  }
  float fade = 1.0f;
  if (d->get<float>(0x40) > 0.01f) {
    const float l = d->get<float>(0x3c);
    if (far_v - l < dc) faded = 1, fade = (far_v - dc) / l;
  }
  if (d->get<float>(0x38) > 0.01f) {
    const float l = d->get<float>(0x34);
    const float nr = approx_sqrt(d->get<float>(4));
    if (dc < nr) dc = nr;
    if (nr + l > dc) {
      const float o = (dc - nr) / l;
      if (o < fade) fade = o, faded = 1;
    }
  }
  uint32_t mask = zmod::mask_top();
  int32_t result = 0;
  if (mask != 0 && mode > 1) {
    result = static_cast<int32_t>(zmod::cull_sphere(nf(node, 0x64), &mask, *nf(node, 0x70)));
    if (node->raw[0x24] & 0x80) {
      if (result == 0x20) result = 0;
      mask &= ~0x20u;
    }
  }
  if (result == 0) {
    node->set_flags(node->flags() | kDrawn);
    push_mask(mask);
    if (d->get<int32_t>(0x48)) {
      const float t = d->get<float>(0xc) - d->get<float>(0x4c);
      const float d2 = g_walk.lod[g_walk.lod_level].d2;
      g_walk.morph = 1;
      g_walk.morph_factor = t < d2 ? (d2 - t) / (d->get<float>(0xc) - t) : 0.0f;
    }
    if (node->child_count() > 0) {
      ++g_walk.lod_level;
      g_walk.lod[g_walk.lod_level] = g_walk.lod[g_walk.lod_level - 1];
      zmth::Mat34 local;
      if (scaled) {
        zmth::push_copy(&local);
        zmth::scale(sx, sy, sz);
      }
      if (faded) {
        g_walk.opacity[++g_walk.opacity_top] = fade;
        zmod::set_opacity(fade);
      }
      int32_t caster = 0, saved = 0, overlay = 0;
      if (is_caster(node)) {
        caster = 1;
        saved = zmod::shadows_on();
        zmod::set_shadows(0);
      }
      if ((node->flags() & kOverride) && !g_walk.overlay) {
        overlay = 1;
        g_walk.overlay = 1;
        zmod::set_overlay(1);
      }
      for (int32_t i = 0; i < node->child_count(); ++i) render_node(node->children[i], node->child_count());
      if (scaled) zmth::pop();
      if (faded) pop_opacity();
      if (caster) zmod::set_shadows(saved);
      if (overlay) g_walk.overlay = 0, zmod::set_overlay(0);
      --g_walk.lod_level;
    }
    g_walk.morph = 0;
    pop_mask();
  }
  if (recomputed) g_walk.recompute = 0;
  return result;
}

// Mech3.exe 1.2 @ 0x004f56c0. Draws child data[0x20 + 8 * data[0x14]], the
// current step; no tree-valid test.
int32_t render_sequence(Node* node, int32_t mode) {
  uint32_t flags = node->flags();
  if (!(flags & kActive)) return 0;
  auto* d = static_cast<uint8_t*>(node->data);
  node->set_flags(flags & ~kUnk25);
  int32_t first;
  std::memcpy(&first, d, 4);
  if (first == 0) return 0;
  uint32_t mask = zmod::mask_top();
  int32_t result = 0, recomputed = 0;
  if (mask != 0 && mode > 1) {
    if ((nu(node, 0x2c) & 1) || g_walk.recompute) {
      refresh_sphere(node);
      set_nu(node, 0x2c, nu(node, 0x2c) & ~1u);
      if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
    }
    result = static_cast<int32_t>(zmod::cull_sphere(nf(node, 0x64), &mask, *nf(node, 0x70)));
    if (node->raw[0x24] & 0x80) {
      if (result == 0x20) result = 0;
      mask &= ~0x20u;
    }
  }
  if (result == 0) {
    node->set_flags(node->flags() | kDrawn);
    push_mask(mask);
    int32_t step;
    std::memcpy(&step, d + 0x14, 4);
    Node* child;
    std::memcpy(&child, d + 0x20 + 8 * step, sizeof child);  // FIX (A): a 32-bit pointer in the exe
    render_node(child, node->child_count());
    pop_mask();
  }
  if (recomputed) g_walk.recompute = 0;
  return result;
}

// Mech3.exe 1.2 @ 0x004f57e0. Child i is drawn when bit i of the dword at
// data + 8 + 4 * data[0] is set (x86 shifts by i mod 32).
int32_t render_switch(Node* node, int32_t mode) {
  uint32_t flags = node->flags();
  if (!(flags & kActive)) return 0;
  auto* d = static_cast<uint8_t*>(node->data);
  node->set_flags(flags & ~kUnk25);
  uint32_t mask = zmod::mask_top();
  int32_t result = 0, recomputed = 0;
  if (mask != 0 && mode > 1) {
    if ((nu(node, 0x2c) & 1) || g_walk.recompute) {
      refresh_sphere(node);
      set_nu(node, 0x2c, nu(node, 0x2c) & ~1u);
      if (!g_walk.recompute) recomputed = 1, g_walk.recompute = 1;
    }
    result = static_cast<int32_t>(zmod::cull_sphere(nf(node, 0x64), &mask, *nf(node, 0x70)));
    if (node->raw[0x24] & 0x80) {
      if (result == 0x20) result = 0;
      mask &= ~0x20u;
    }
  }
  if (result == 0) {
    node->set_flags(node->flags() | kDrawn);
    push_mask(mask);
    int32_t which;
    uint32_t bits;
    std::memcpy(&which, d, 4);
    std::memcpy(&bits, d + 8 + 4 * which, 4);
    for (int32_t i = 0; i < node->child_count(); ++i)
      if ((bits >> (i & 31)) & 1) render_node(node->children[i], node->child_count());
    pop_mask();
  }
  if (recomputed) g_walk.recompute = 0;
  return result;
}

// 0x004f5a00: the angle of b seen from a in the XZ plane as a diamond
// angle in [0, 4) (no trigonometry), and |dx| + |dz| as the distance.
void diamond_angle(const float* a, const float* b, float* angle, float* dist) {
  const float dx = b[0] - a[0];
  const float dz = a[2] - b[2];
  const float s = std::fabs(dx) + std::fabs(dz);
  *dist = s;
  float t = s;
  if (s != 0.0f) t = dz / s;
  if (dx < 0.0f) {
    *angle = 2.0f - t;
  } else {
    if (dz < 0.0f) t = t - -4.0f;
    *angle = t;
  }
}

void fill_rect_error(int line, const char* msg) { zerr::report(0x200, kFile, line, msg); }

// The part 0x004f5c80 and 0x004f61b0 share: the camera's footprint (its
// position and two or four far corners, through its position and angles)
// as an XZ hull, made the view polygon, and its XZ bounds.
void footprint(Camera* cam, int line, float* min_x, float* max_x, float* min_z, float* max_z) {
  zmth::translate(cam->get<float>(0x2c), cam->get<float>(0x30), cam->get<float>(0x34));
  zmth::rotate_y(cam->get<float>(0x3c));
  int32_t n;
  if (std::fabs(static_cast<double>(cam->get<float>(0x38))) > 0.174533 ||  // 0x005cdb68, 10 degrees
      std::fabs(static_cast<double>(cam->get<float>(0x40))) > 0.174533) {
    n = 5;
    zmth::rotate_x(cam->get<float>(0x38));
    zmth::rotate_z(cam->get<float>(0x40));
  } else {
    n = 3;
  }
  g_walk.view_count = n;
  const float* src = cam->floats(0xfc);
  if (zmth::matrix_stack().is_identity()) {
    std::memcpy(g_walk.view_points, src, 12 * n);
  } else {
    const float* m = zmth::matrix_stack().matrix().m;
    for (int32_t i = 0; i < n; ++i) {
      const float* p = src + 3 * i;
      for (int c = 0; c < 3; ++c) {
        const float a = p[0] * m[c] + p[1] * m[3 + c];
        const float b = a + p[2] * m[6 + c];
        g_walk.view_points[i][c] = b + m[9 + c];
      }
    }
  }
  if (n > 3) g_walk.view_count = n = view_hull(g_walk.view_points, n);
  if (!zmod::set_view_polygon(g_walk.view_points, n)) {
    char msg[200];
    std::snprintf(msg, sizeof msg,
                  "%s: Line %d: ERROR from gModDIPointInPolygonInit() for camera frustrum footprint.\n", kFile,
                  line);
    gamez_abort(msg);  // 0x0050da70(1), zerr's fatal exit
  }
  const float(*p)[3] = g_walk.view_points;
  *min_x = p[0][0] < p[1][0] ? p[0][0] : p[1][0];
  *max_x = p[0][0] > p[1][0] ? p[0][0] : p[1][0];
  *min_z = p[0][2] < p[1][2] ? p[0][2] : p[1][2];
  float mz = p[0][2] > p[1][2] ? p[0][2] : p[1][2];
  for (int32_t i = 2; i < n; ++i) {
    if (p[i][0] < *min_x) *min_x = p[i][0];
    if (p[i][0] > *max_x) *max_x = p[i][0];
    if (p[i][2] < *min_z) *min_z = p[i][2];
    if (mz < p[i][2]) mz = p[i][2];
  }
  *max_z = mz;
}

void add_cell(int32_t ring, int32_t x, int32_t z, int32_t wrapped, float dx, float dz, uint32_t mask,
              int line_rings, int line_cells) {
  if (ring >= 50) return fill_rect_error(line_rings, "Error: Need more diamond tiler rings.");
  ScreenRing& r = g_walk.rings[ring];
  if (r.count >= 30) return fill_rect_error(line_cells, "Error: Need more diamond tiler cells per ring.");
  ScreenRing::Cell& c = r.cell[r.count++];
  c.x = x, c.z = z, c.mask = mask, c.wrapped = wrapped;
  if (wrapped) c.dx = dx, c.dz = dz;
}

Partition& cell_at(World* w, int32_t x, int32_t z) { return w->rows[z][x]; }
}  // namespace

SceneWalk& scene_walk() { return g_walk; }
WorldRenderHooks& world_render_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x004f6c80 (ECX the camera, EDX the zone list). The ids
// are read up to the count byte, as many as it says.
int camera_set_zone(Node* node, const uint8_t zone[4]) {
  if (int r = check_class(node, 1, kFile, 0x1838)) return r;
  const uint8_t n = zone[0];
  if (n == 0) return 0;
  bool set = true;
  for (int i = 0; i < n; ++i)
    if (zone[i + 1] == 0xff) set = false;
  if (set) {
    auto* c = static_cast<Camera*>(node->data);
    c->put<int32_t>(0x1e0, 1);
    std::memcpy(c->raw + 0x1e4, zone, 4);
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3a70
int camera_set_clip(Node* node, float near_clip, float far_clip) {
  if (int r = check_class(node, 1, kFile, 0x5ca)) return r;
  auto* c = static_cast<Camera*>(node->data);
  c->put(0xb0, near_clip), c->put(0xb4, far_clip);
  c->put<int32_t>(0xf8, 1);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3b90. The angle is clamped to 1.396 rad (80 degrees).
int camera_set_fov(Node* node, float h, float v) {
  if (int r = check_class(node, 1, kFile, 0x614)) return r;
  auto* c = static_cast<Camera*>(node->data);
  const float ah = c->get<float>(0xe0) / h, av = c->get<float>(0xe4) / v;
  c->put(0xd8, h), c->put(0xdc, v);
  c->put(0xe8, ah > 1.396f ? 1.396f : ah);  // 0x005cdb4c
  c->put(0xec, av > 1.396f ? 1.396f : av);
  const double hh = c->get<float>(0xe8) * 0.5, hv = c->get<float>(0xec) * 0.5;
  c->put<int32_t>(0x138, 1), c->put<int32_t>(0xf8, 1);
  c->put(0xf0, static_cast<float>(hh)), c->put(0xf4, static_cast<float>(hv));
  c->put(0x1d4, static_cast<float>(1.0 / std::tan(hh)));
  c->put(0x1d8, static_cast<float>(1.0 / std::tan(hv)));
  return 0;
}

// Mech3.exe 1.2 @ 0x004f32e0: unit fov factors, the matrices and planes to
// rebuild, an empty zone list (+0x1e4), on list 8.
Node* camera_create() {
  Node* node = node_alloc();
  if (!node) return zerr::report(0x400, kFile, 0x292, "Null node pointer."), nullptr;
  node->put<int32_t>(0x34, 1);
  auto* c = new Camera{};
  std::memset(c->raw, 0, sizeof c->raw);
  node->data = c;
  c->put(0xd8, 1.0f), c->put(0xdc, 1.0f);
  c->put<int32_t>(0xf8, 1), c->put<int32_t>(0x184, 1), c->put<int32_t>(0x138, 1);
  zmod::zone_reset(c->raw + 0x1e4);
  push_front(kCameras, node);
  return node;
}

// Mech3.exe 1.2 @ 0x004f34c0
int camera_set_flag8(Node* node, bool on) {
  if (int r = check_class(node, 1, kFile, 0x336)) return r;
  Camera* c = cam_of(node);
  c->put<uint32_t>(0x10, on ? c->get<uint32_t>(0x10) | 8u : c->get<uint32_t>(0x10) & ~8u);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3560
int camera_set_world(Node* node, Node* world) {
  if (!node) return zerr::report(0x400, kFile, 0x37f, "Null node pointer."), 5;
  if (!world) return zerr::report(0x400, kFile, 0x380, "Null node pointer."), 5;
  if (!node->data) return zerr::report(0x400, kFile, 0x382, "Null class data pointer"), 5;
  if (!world->data) return zerr::report(0x400, kFile, 0x383, "Null class data pointer"), 5;
  char msg[64];
  if (node->cls() != 1) {
    std::snprintf(msg, sizeof msg, "Bad Class Found.\n Wanted (%d)\n Found (%d)", node->cls(), 1);
    return zerr::report(0x400, kFile, 0x385, msg), 3;
  }
  if (world->cls() != 2) {
    std::snprintf(msg, sizeof msg, "Bad Class Found.\n Wanted (%d)\n Found (%d)", world->cls(), 2);
    return zerr::report(0x400, kFile, 0x386, msg), 3;
  }
  cam_of(node)->nodes[0] = world;
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3620. No checks.
int camera_set_window(Node* node, Node* window) {
  cam_of(node)->nodes[1] = window;
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3d90: the field of view as angles (+0xe8/+0xec are
// what 0x004f3b90 derives), the base the factors divide.
int camera_set_fov_angles(Node* node, float h, float v) {
  if (int r = check_class(node, 1, kFile, 0x673)) return r;
  Camera* c = cam_of(node);
  const double ah = static_cast<double>(h) / c->get<float>(0xd8), av = static_cast<double>(v) / c->get<float>(0xdc);
  c->put<int32_t>(0x138, 1), c->put<int32_t>(0xf8, 1);
  const double hh = ah * 0.5, hv = av * 0.5;
  c->put(0xe0, h), c->put(0xe4, v);
  c->put(0xe8, static_cast<float>(ah)), c->put(0xec, static_cast<float>(av));
  c->put(0xf0, static_cast<float>(hh)), c->put(0xf4, static_cast<float>(hv));
  c->put(0x1d4, static_cast<float>(1.0 / std::tan(hh)));
  c->put(0x1d8, static_cast<float>(1.0 / std::tan(hv)));
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3ee0: the angles 0x004f3d90 set (+0xe0, +0xe4).
int camera_get_fov_angles(Node* node, float* h, float* v) {
  if (int r = check_class(node, 1, kFile, 0x6a8)) return r;
  const Camera* c = cam_of(node);
  *h = c->get<float>(0xe0);
  *v = c->get<float>(0xe4);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f33a0
int camera_add_child(Node* node, Node* child) {
  if (!node) return zerr::report(0x400, kFile, 0x2e3, "Null node pointer."), 5;
  if (!child) return zerr::report(0x400, kFile, 0x2e4, "Null node pointer."), 5;
  return node_link(node, child);
}

// Mech3.exe 1.2 @ 0x004f3610. No checks.
Node* camera_world(Node* node) { return cam_of(node)->nodes[0]; }

namespace {
// Mech3.exe 1.2 @ 0x004f3760 (ECX node, EDX data): matrix dirty (+0x10 bit
// 2), the node on the changed list, its children marked.
void camera_mark_changed(Node* node, Camera* d) {
  d->put<uint32_t>(0x10, d->get<uint32_t>(0x10) | 4);
  if (!(node->flags() & 1)) {
    push_front(kChanged, node);
    node->set_flags(node->flags() | 1);
  }
  node->set_flags(node->flags() | 2);
  if (node->changed) node->changed->removable = 0;
  for (int32_t i = 0; i < node->child_count(); ++i) node_mark_unk25(node->children[i]);
}
}  // namespace

// Mech3.exe 1.2 @ 0x004f36b0. Drops the given-matrix flag (+0x10 bit 1).
int camera_set_rotation(Node* node, float x, float y, float z) {
  if (int r = check_class(node, 1, kFile, 0x468)) return r;
  Camera* d = cam_of(node);
  d->put<int32_t>(0x184, 1);
  d->put(0x20, x), d->put(0x24, y), d->put(0x28, z);
  d->put<uint32_t>(0x10, d->get<uint32_t>(0x10) & ~2u);
  if (node->parent_count() > 0) camera_mark_changed(node, d);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3870. With a given matrix the position goes to +0xa4.
int camera_set_position(Node* node, float x, float y, float z) {
  if (int r = check_class(node, 1, kFile, 0x4fd)) return r;
  Camera* d = cam_of(node);
  const int off = (d->get<uint32_t>(0x10) & 2) ? 0xa4 : 0x14;
  d->put(off, x), d->put(off + 4, y), d->put(off + 8, z);
  if (node->parent_count() > 0) camera_mark_changed(node, d);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f37c0. Unlike the setter, the given-matrix flag stays.
int camera_add_rotation(Node* node, float x, float y, float z) {
  if (int r = check_class(node, 1, kFile, 0x4a0)) return r;
  Camera* d = cam_of(node);
  x = x + d->get<float>(0x20), y = y + d->get<float>(0x24), z = z + d->get<float>(0x28);
  d->put(0x20, x), d->put(0x24, y);
  d->put<int32_t>(0x184, 1);
  d->put(0x28, z);
  if (node->parent_count() > 0) camera_mark_changed(node, d);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3920
int camera_add_position(Node* node, float x, float y, float z) {
  if (int r = check_class(node, 1, kFile, 0x530)) return r;
  Camera* d = cam_of(node);
  const int off = (d->get<uint32_t>(0x10) & 2) ? 0xa4 : 0x14;
  x = x + d->get<float>(off), y = y + d->get<float>(off + 4), z = z + d->get<float>(off + 8);
  d->put(off, x), d->put(off + 4, y), d->put(off + 8, z);
  if (node->parent_count() > 0) camera_mark_changed(node, d);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3ff0
int camera_set_lod_scale(Node* node, float f) {
  if (int r = check_class(node, 1, kFile, 0x6eb)) return r;
  Camera* d = cam_of(node);
  d->put(0xd0, f);
  d->put(0xd4, 1.0f / (f * f));
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3430
int camera_set_listener(Node* node, int32_t on) {
  if (int r = check_class(node, 1, kFile, 0x31e)) return r;
  Camera* d = cam_of(node);
  if (on) {
    d->put<uint32_t>(0x10, d->get<uint32_t>(0x10) | 1u);
    g_walk.listener_frame = 0;
  } else {
    d->put<uint32_t>(0x10, d->get<uint32_t>(0x10) & ~1u);
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004f4090
int camera_set_horizon(Node* node, Node* horizon) {
  if (int r = check_class(node, 1, kFile, 0x70e)) return r;
  cam_of(node)->nodes[2] = horizon;
  return 0;
}

// Mech3.exe 1.2 @ 0x004f4100
int camera_set_horizon_xz(Node* node, Node* horizon) {
  if (int r = check_class(node, 1, kFile, 0x72f)) return r;
  cam_of(node)->nodes[3] = horizon;
  return 0;
}

// Mech3.exe 1.2 @ 0x004f4170
int camera_update(Node* node) {
  if (!node) return zerr::report(0x400, kFile, 0x821, "Null node pointer."), 5;
  if (!node->data) return zerr::report(0x400, kFile, 0x822, "Null class data pointer"), 5;
  return camera_update_offset(node, nullptr);
}

// Mech3.exe 1.2 @ 0x004f41b0. The local planes are rebuilt when the fov
// changed (+0x138), the far corners when clip or fov changed (+0xf8):
// (a, b, -far), (-a, b, -far), (a, -b, -far), (-a, -b, -far) with
// a = tan(h/2) far, b = -tan(v/2) far, after the origin at +0xfc.
int camera_update_offset(Node* node, const float* offset) {
  Camera* d = cam_of(node);
  camera_matrices(node, d, offset);
  d->put<int32_t>(0x184, 1);
  if (d->get<int32_t>(0x138)) {
    d->put<int32_t>(0x138, 0);
    zmod::camera_local_planes(d);
    d->put<int32_t>(0x184, 1);
  }
  d->put<int32_t>(0x184, 0);
  zmod::camera_world_planes(d);
  if (d->get<int32_t>(0xf8)) {
    d->put<int32_t>(0xf8, 0);
    d->put<uint32_t>(0xfc, 0), d->put<uint32_t>(0x100, 0), d->put<uint32_t>(0x104, 0);
    const float far_z = d->get<float>(0xb4);
    const float a = static_cast<float>(std::tan(static_cast<double>(d->get<float>(0xe8) * 0.5f)) * far_z);
    const float b = -static_cast<float>(std::tan(static_cast<double>(d->get<float>(0xec) * 0.5f)) * far_z);
    const float c = -far_z;
    const float corners[12] = {a, b, c, -a, b, c, a, -b, c, -a, -b, c};
    std::memcpy(d->floats(0x108), corners, sizeof corners);
  }
  const float* dir = d->floats(0x74);
  const float* pos = d->floats(0x2c);
  for (int off : {0xb8, 0xc4}) {
    const float k = d->get<float>(off == 0xb8 ? 0xb0 : 0xb4);
    float* p = d->floats(off);
    p[0] = dir[0] * k, p[1] = dir[1] * k, p[2] = dir[2] * k;
    p[0] = p[0] + pos[0], p[1] = p[1] + pos[1], p[2] = p[2] + pos[2];
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004f4370 (ECX node, EDX data, stack offset). Leaves the
// top of the stack the identity. With +0x10 bit 0 the camera is the sound
// listener, its velocity from the frame step (zeroed above the limit).
int camera_matrices(Node* node, Camera* d, const float* offset) {
  zmth::load_identity();
  node_to_world(node, 1);
  float* m = zmth::matrix_stack().matrix().m;
  float* off = zmod::lighting().camera_offset;  // 0x0073fbb0
  if (offset) {
    m[9] = offset[0] + m[9], m[10] = offset[1] + m[10], m[11] = offset[2] + m[11];
    off[0] = offset[0], off[1] = offset[1], off[2] = offset[2];
  } else {
    off[0] = kNullOffset[0], off[1] = kNullOffset[1], off[2] = kNullOffset[2];
  }
  d->put(0x2c, m[9]), d->put(0x30, m[10]), d->put(0x34, m[11]);
  d->put(0x74, -m[6]), d->put(0x78, -m[7]), d->put(0x7c, -m[8]);
  std::memcpy(d->floats(0x44), m, 48);
  zmth::matrix_to_euler(*mat(m), d->floats(0x38));
  zmth::load_identity();
  zmth::set_view(*mat(d->floats(0x44)));
  if (!(d->get<uint32_t>(0x10) & 1)) return 0;
  if (frames().frame < g_walk.listener_frame) return 0;
  float v[3] = {0, 0, 0};
  const float dt = ztimer::state().dt;
  const float* p = d->floats(0x68);
  if (dt != 0.0f) {
    const float inv = 1.0f / dt;
    for (int i = 0; i < 3; ++i) v[i] = p[i] - g_walk.listener_last[i];
    for (int i = 0; i < 3; ++i) v[i] = inv * v[i];
    const float speed = std::sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
    const float limit = g_hooks.listener_speed_limit ? g_hooks.listener_speed_limit(g_hooks.ctx) : 0.0f;
    if (!(limit > speed)) v[0] = v[1] = v[2] = 0.0f;
  }
  std::memcpy(g_walk.listener_last, p, 12);
  if (g_hooks.listener) g_hooks.listener(g_hooks.ctx, d->floats(0x44), v);
  g_walk.listener_frame = frames().frame + 1;
  return 0;
}

// Mech3.exe 1.2 @ 0x004f5910. EDX is not a mode the callers choose: every
// caller leaves its child (or cell node) count there, so a node is culled
// only when it has a sibling; the world's own children get 2 (0x004f6c29).
void render_node(Node* node, int32_t siblings) {
  if (!zmod::zone_visible(node->raw[0x30])) return;
  if ((nu(node, 0x2c) & 0x20) && g_walk.pre) g_walk.pre(node);
  const bool horizon = g_walk.camera && node == g_walk.camera->nodes[2];
  const bool previous_sky = horizon && zvid::set_native_sky(true);
  switch (node->cls()) {
    case 1: render_camera(node, siblings); break;
    case 5: render_object3d(node, siblings); break;
    case 6: render_lod(node, siblings); break;
    case 7: render_sequence(node, siblings); break;
    case 8: render_animate(node, siblings); break;
    case 9: render_light(node, siblings); break;
    case 10: render_sound(node, siblings); break;
    case 11: render_switch(node, siblings); break;
    default: std::fprintf(stderr, "Unrecognized node rendering type: %s\n", node->name()); break;
  }
  if (horizon) zvid::set_native_sky(previous_sky);
  if ((nu(node, 0x2c) & 0x40) && g_walk.post) g_walk.post(node);
}

// Mech3.exe 1.2 @ 0x004f5a90 (find_convex_hull_xz): gift wrapping from the
// point with the largest z (then smallest x), by diamond angle, farther
// first on a tie. Writes points[count] as the closing copy; returns the
// hull's size (0 and a report if it does not close).
int32_t view_hull(float (*points)[3], int32_t count) {
  int32_t start = 0;
  for (int32_t i = 1; i < count; ++i) {
    if (points[i][2] > points[start][2] ||
        (points[i][2] == points[start][2] && points[i][0] < points[start][0]))
      start = i;
  }
  std::memcpy(points[count], points[start], 12);
  float best = 0.0f;
  int32_t next = start;
  for (int32_t i = 0; i < count; ++i) {
    float tmp[3];
    std::memcpy(tmp, points[i], 12);
    std::memcpy(points[i], points[next], 12);
    std::memcpy(points[next], tmp, 12);
    const float prev = best;
    best = 4.0f;
    float best_dist = 0.0f;
    next = count;
    for (int32_t j = i + 1; j <= count; ++j) {
      float angle, dist;
      diamond_angle(points[i], points[j], &angle, &dist);
      if (angle < prev || !(dist > 0.0f)) continue;
      if (angle < best || (angle == best && dist > best_dist)) next = j, best = angle, best_dist = dist;
    }
    if (next == count) return i + 1;
  }
  zerr::report(0x200, kFile, 0x115e, "Returning from find_convex_hull_xz in unexpected line.");
  return 0;
}

// Mech3.exe 1.2 @ 0x004f5c80 (ECX world node, EDX its data, stack camera
// data): world +0x50 == 0. Cells clamped to the grid, no wrapping.
int pick_cells(Node* world, Camera* cam) {
  World* w = static_cast<World*>(world->data);
  for (ScreenRing& r : g_walk.rings) r.count = 0;
  int32_t cx, cz;
  world_cell_clamped(world, &cx, cam->get<float>(0x2c), cam->get<float>(0x34), &cz);
  zmth::Mat34 local;
  zmth::push(&local);
  zmth::load_identity();
  float min_x, max_x, min_z, max_z;
  footprint(cam, 0x11ff, &min_x, &max_x, &min_z, &max_z);
  int32_t x0, z0, x1, z1;
  world_cell_clamped(world, &x0, min_x, min_z, &z0);
  world_cell_clamped(world, &x1, max_x, max_z, &z1);
  if (z0 > z1) std::swap(z0, z1);
  const int32_t cols = w->get<int32_t>(0x88) - 1, rows = w->get<int32_t>(0x8c) - 1;
  auto clamp = [](int32_t v, int32_t hi) { return v > hi ? hi : (v < 0 ? 0 : v); };
  x0 = clamp(x0, cols), x1 = clamp(x1, cols), z0 = clamp(z0, rows), z1 = clamp(z1, rows);
  const uint32_t cam_mask = cell_at(w, cx, cz).get<uint32_t>(4);
  for (int32_t x = x0; x <= x1; ++x) {
    for (int32_t z = z0; z <= z1; ++z) {
      Partition& p = cell_at(w, x, z);
      if (!(p.get<uint32_t>(4) & cam_mask)) continue;
      const float pt[3] = {w->get<float>(0x6c) + p.get<float>(8), 0.0f, w->get<float>(0x70) + p.get<float>(0xc)};
      if (!zmod::inside_view_polygon(pt, w->get<float>(0x7c))) continue;
      uint32_t mask = 0x3f;
      const uint32_t r = (p.get<uint32_t>(0) & 0x100) ? zmod::cull_sphere(p.floats(0x28), &mask, p.get<float>(0x34))
                                                      : zmod::cull_sphere(pt, &mask, -w->get<float>(0x7c));
      if (r) continue;
      add_cell(std::abs(z - cz) + std::abs(x - cx), x, z, 0, 0, 0, mask, 0x12bf, 0x12b9);
    }
  }
  zmth::pop();
  return 0;
}

// Mech3.exe 1.2 @ 0x004f61b0 (ECX world node, EDX its data, stack camera
// data): virtual partitions (world +0x50, every shipped world). A cell past
// the grid stands for one in the virtual block, recorded with the move that
// puts it there and not sphere-culled.
int pick_cells_virtual(Node* world, Camera* cam) {
  World* w = static_cast<World*>(world->data);
  for (ScreenRing& r : g_walk.rings) r.count = 0;
  int32_t cx, cz, wx, wz, inside;
  world_cell_wrapped(world, &cx, cam->get<float>(0x2c), cam->get<float>(0x34), &cz, &wx, &wz, &inside);
  zmth::Mat34 local;
  zmth::push(&local);
  zmth::load_identity();
  float min_x, max_x, min_z, max_z;
  footprint(cam, 0x138f, &min_x, &max_x, &min_z, &max_z);
  int32_t x0, z0, x1, z1, a, b, c, e;
  world_cell_wrapped(world, &x0, min_x, min_z, &z0, &a, &b, &inside);
  world_cell_wrapped(world, &x1, max_x, max_z, &z1, &c, &e, &inside);
  if (z0 > z1) std::swap(z0, z1);  // (the exe also swaps the unused wrapped rows)
  const uint32_t cam_mask = cell_at(w, wx, wz).get<uint32_t>(4);
  for (int32_t x = x0; x <= x1; ++x) {
    for (int32_t z = z0; z <= z1; ++z) {
      int32_t px, pz;
      world_wrap_cell(world, x, z, &px, &pz, &inside);
      const int32_t wrapped = inside == 0;
      float dx = 0.0f, dz = 0.0f;
      if (wrapped) {
        dx = static_cast<float>(x - px) * w->get<float>(0x64);
        dz = static_cast<float>(z - pz) * w->get<float>(0x68);
      }
      Partition& p = cell_at(w, px, pz);
      if (!(p.get<uint32_t>(4) & cam_mask)) continue;
      const float pt[3] = {(p.get<float>(8) + w->get<float>(0x6c)) + dx, 0.0f,
                           (p.get<float>(0xc) + w->get<float>(0x70)) + dz};
      if (!zmod::inside_view_polygon(pt, w->get<float>(0x7c))) continue;
      uint32_t mask = 0x3f;
      uint32_t r = 0;
      if (!wrapped)
        r = (p.get<uint32_t>(0) & 0x100) ? zmod::cull_sphere(p.floats(0x28), &mask, p.get<float>(0x34))
                                         : zmod::cull_sphere(pt, &mask, -w->get<float>(0x7c));
      if (r) continue;
      add_cell(std::abs(z - cz) + std::abs(x - cx), px, pz, wrapped, dx, dz, mask, 0x1459, 0x1453);
    }
  }
  zmth::pop();
  return 0;
}

// Mech3.exe 1.2 @ 0x004f6720 (ECX world node, EDX camera node, stack camera
// data): the picked cells ring by ring, nearest first.
int render_cells(Node* world, Node* camera_node, Camera* cam) {
  World* w = static_cast<World*>(world->data);
  int32_t restored = 1;
  const int r = w->get<int32_t>(0x50) ? pick_cells_virtual(world, cam) : pick_cells(world, cam);
  if (r) return r;
  const int32_t fog = zmod::fog_on();
  const float fog_near = fog ? zmod::fog_near() : 0.0f;
  for (int32_t ring = 0; ring < 50; ++ring) {
    g_walk.ring = ring;
    for (int32_t k = 0; k < g_walk.rings[ring].count; ++k) {
      const ScreenRing::Cell& e = g_walk.rings[ring].cell[k];
      Partition& p = cell_at(w, e.x, e.z);
      float centre[3];
      std::memcpy(centre, p.floats(0x28), 12);
      if (e.wrapped) {
        const float off[3] = {-e.dx, 0.0f, -e.dz};
        camera_update_offset(camera_node, off);
        restored = 0;
      } else if (!restored) {
        camera_update(camera_node);
        restored = 1;
      }
      const int32_t visible =
          (g_walk.hidden_surface && g_walk.ring > 0) ? zmod::hidden_surface(centre, p.get<float>(0x34)) : 1;
      if (!visible) continue;
      // A positioned, ranged light (flags 0x10, 2) keeps 0x200, on, only while it
      // reaches this cell (+0x60 position, +0xb8 range); any other light always has it.
      for (int32_t i = 0; i < w->get<int32_t>(0xa0); ++i) {
        if (!(w->lights[i]->flags() & kActive)) continue;
        auto* l = static_cast<Light*>(w->light_data[i]);
        uint32_t lf = l->get<uint32_t>(0xb0);
        bool on = true;
        if ((lf & 0x10) && (lf & 2)) {
          const float dx = centre[0] - l->get<float>(0x60), dy = centre[1] - l->get<float>(0x64),
                      dz = centre[2] - l->get<float>(0x68);
          const float reach = p.get<float>(0x34) + l->get<float>(0xb8);
          const float d2 = (dx * dx + dy * dy) + dz * dz;
          on = reach * reach > d2;
        }
        l->put<uint32_t>(0xb0, on ? (lf | 0x200) : (lf & ~0x200u));
      }
      if (fog) {
        const float* pos = cam->floats(0x2c);
        const float dx = centre[0] - pos[0], dy = centre[1] - pos[1], dz = centre[2] - pos[2];
        const float dist = std::sqrt((dx * dx + dy * dy) + dz * dz);
        zmod::fog_set_on((p.get<float>(0x34) * 1.1f + dist < fog_near) ? 0 : 1);  // 0x005cdb70
      }
      if (zmod::shadows_on()) {
        zmod::CullHooks& h = zmod::cull_hooks();
        if (h.cell_casters) h.cell_casters(h.ctx, p.floats(0x10));
        zmod::use_cell_casters();
      }
      zmod::mask_top() = e.mask;
      const int32_t count = p.get<int16_t>(0x3a);
      if (e.wrapped) {
        for (int32_t i = 0; i < count; ++i)
          if (p.nodes[i]->flags() & 0x8000) render_node(p.nodes[i], count);
        continue;
      }
      for (int32_t i = 0; i < count; ++i) render_node(p.nodes[i], count);
      // Fade entries (0x44 each; none in the files): node, matrix, near and
      // far on the squared distance (0x0051dfa0 returns it squared), factor.
      const int32_t fades = p.get<int32_t>(0x40);
      for (int32_t i = 0; i < fades; ++i) {
        uint8_t* f = p.fades + 0x44 * i;
        float fl[16];
        std::memcpy(fl, f + 4, sizeof fl);  // +0x04..+0x40
        const float* cpos = zmth::view().camera.m + 9;  // 0x0073fc24
        const float dx = cpos[0] - fl[9], dy = cpos[1] - fl[10], dz = cpos[2] - fl[11];
        const float d2 = (dx * dx + dy * dy) + dz * dz;
        float o;
        if (!(d2 > fl[12])) o = 1.0f;
        else if (!(d2 < fl[13])) o = 0.0f;
        else o = (fl[13] - d2) * fl[14];
        if (!(o > 0.0f)) continue;
        if (o < 1.0f) {
          g_walk.opacity[++g_walk.opacity_top] = o;
          zmod::set_opacity(o);
        }
        Node* n;
        std::memcpy(&n, f, sizeof n);
        std::memcpy(nf(n, 0x64), fl + 9, 12);
        if (n->cls() == 5) {
          auto* od = static_cast<Object3d*>(n->data);
          od->put<uint32_t>(0, od->get<uint32_t>(0) & ~8u);
          std::memcpy(od->floats(0x30), fl, 48);
          std::memcpy(od->floats(0x60), fl, 48);
        }
        n->raw[0x30] = f[0x40];
        render_node(n, fades);
        if (o < 1.0f) pop_opacity();
      }
    }
  }
  if (!restored) camera_update(camera_node);
  for (int32_t i = 0; i < w->get<int32_t>(0xa0); ++i) {
    auto* l = static_cast<Light*>(w->light_data[i]);
    l->put<uint32_t>(0xb0, l->get<uint32_t>(0xb0) | 0x200);
  }
  if (fog) zmod::fog_set_on(fog);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f6c00 (ECX world node): the world's direct children
// with all six planes, EDX 2.
int render_world_children(Node* world) {
  if (zmod::shadows_on()) zmod::use_all_casters();
  zmod::mask_top() = 0x3f;
  for (int32_t i = 0; i < world->child_count(); ++i) render_node(world->children[i], 2);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f6c50 (ECX world node, EDX camera node, stack camera data)
int render_scene(Node* world, Node* camera_node, Camera* cam) {
  render_cells(world, camera_node, cam);
  return render_world_children(world);
}

// Mech3.exe 1.2 @ 0x004f6d40. The horizon node (camera +8) is moved to the
// camera's position; the XZ one (+0xc, unused by the game) keeps its own y.
// Either runs the frame update of changed nodes (0x004f7f70).
void horizon_follow() {
  Camera* cam = g_walk.camera;
  bool moved = false;
  if (cam->nodes[2]) {
    object3d_set_translation(cam->nodes[2], cam->get<float>(0x2c), cam->get<float>(0x30), cam->get<float>(0x34));
    moved = true;
  }
  if (cam->nodes[3]) {
    float x, y, z;
    object3d_get_translation(cam->nodes[3], &x, &y, &z);
    object3d_set_translation(cam->nodes[3], cam->get<float>(0x2c), y, cam->get<float>(0x34));
    moved = true;
  }
  if (moved && update_hooks().frame_update) update_hooks().frame_update(update_hooks().ctx);
}

// Mech3.exe 1.2 @ 0x004f73f0. render_world's set-up without the zones, then
// only `node` walked (0x004f5910 with 2: never culled alone), from its
// parent's world matrix when it has one parent.
int render_subtree(Node* camera_node, Node* node, int32_t hw) {
  if (!node || !camera_node) return 1;
  zmth::Mat34 local;
  zmth::push(&local);
  Camera* cam = cam_of(camera_node);
  g_walk.camera = cam;
  Node* world = camera_world(camera_node);
  if (g_walk.lod_dynamic) {
    if (ztimer::state().dt > g_walk.lod_target)
      g_walk.lod_factor = g_walk.lod_factor - g_walk.lod_step;
    else
      g_walk.lod_factor = g_walk.lod_factor + g_walk.lod_step;
    if (g_walk.lod_factor > 1.0f) g_walk.lod_factor = 1.0f;
    else if (g_walk.lod_factor < g_walk.lod_min) g_walk.lod_factor = g_walk.lod_min;
    camera_set_lod_scale(camera_node, g_walk.lod_factor);
  }
  zmod::world_build_lights(world, (cam->get<uint32_t>(0x10) >> 3) & 1);
  camera_update(camera_node);
  horizon_follow();
  zmod::set_camera(g_walk.camera);
  zmod::world_update_lights(world);
  if (g_hooks.world_sounds) g_hooks.world_sounds(g_hooks.ctx, world);
  if (zmod::shadows_on()) {
    zmod::CullHooks& h = zmod::cull_hooks();
    if (h.collect_casters) h.collect_casters(h.ctx);
    if (h.caster_directions) h.caster_directions(h.ctx);
    zmod::use_all_casters();
  }
  g_walk.lod_level = 0;
  if (!hw) {  // the software renderer's pass (0x00564500 ..), not ported
    zmth::pop();
    return 0;
  }
  std::memcpy(zmod::zones().list, cam->raw + 0x1e4, 4);
  if (g_hooks.begin_scene) g_hooks.begin_scene(g_hooks.ctx);
  zmod::mask_top() = 0x3f;
  zmth::Mat34 m;
  zmth::push(&m);
  zmth::load_identity();
  if (node->parent_count() == 1) node_to_world(node->parents[0], 1);
  render_node(node, 2);
  zmth::pop();
  if (g_hooks.flush_transparent) g_hooks.flush_transparent(g_hooks.ctx);
  if (g_hooks.flush_overlay) g_hooks.flush_overlay(g_hooks.ctx);
  if (g_hooks.draw_quad) g_hooks.draw_quad(g_hooks.ctx);
  if (g_hooks.end_scene) g_hooks.end_scene(g_hooks.ctx);
  zmth::pop();
  return 0;
}

// Mech3.exe 1.2 @ 0x004f7350
int camera_draw_flares(Camera* cam, int32_t from) {
  if (g_hooks.begin_scene) g_hooks.begin_scene(g_hooks.ctx);
  const int32_t n = zrndr::flares_pick(from);
  for (int32_t i = 0; i < n; ++i) {
    const zrndr::Flare* f = zrndr::flare_picked(i);
    SegmentOptions& o = segment_options();
    o.exclude = 0x40000;  // 0x004eda00, cleared by the query
    o.first_hit = 1;      // 0x004ed9c0
    static LineHits hits;
    const float from_pt[3] = {cam->get<float>(0x2c), cam->get<float>(0x30), cam->get<float>(0x34)};
    const int32_t res = segment_list(cam->nodes[0], from_pt, f->world, &hits);
    o.first_hit = 0;
    if (res != 0 || hits.count == 0) zrndr::flare_draw(i);
  }
  if (g_hooks.flush_transparent) g_hooks.flush_transparent(g_hooks.ctx);
  if (g_hooks.end_scene) g_hooks.end_scene(g_hooks.ctx);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f7030 (ECX camera node, EDX: after the flush, 0x005a67c0).
// The Direct3D frame's 3D part: LoD scale, lights, camera, horizon,
// viewport, zones, then the scene walk between BeginScene and EndScene.
int render_world(Node* camera_node, int32_t after) {
  zmth::check_stack_at_frame();  // FIX (bug), ours (W59): one imbalance cannot carry
  zmth::Mat34 local;
  zmth::push(&local);
  Camera* cam = cam_of(camera_node);
  g_walk.camera = cam;
  Node* world = camera_world(camera_node);
  auto* window = static_cast<Window*>(cam->nodes[1]->data);
  if (g_walk.lod_dynamic) {
    if (ztimer::state().dt > g_walk.lod_target)
      g_walk.lod_factor = g_walk.lod_factor - g_walk.lod_step;
    else
      g_walk.lod_factor = g_walk.lod_factor + g_walk.lod_step;
    if (g_walk.lod_factor > 1.0f) g_walk.lod_factor = 1.0f;
    else if (g_walk.lod_factor < g_walk.lod_min) g_walk.lod_factor = g_walk.lod_min;
    camera_set_lod_scale(camera_node, g_walk.lod_factor);
  }
  zmod::world_build_lights(world, (cam->get<uint32_t>(0x10) >> 3) & 1);
  // 0x004d72a0(world): returns 0, nothing else (a shared empty body).
  camera_update(camera_node);
  horizon_follow();
  zmod::set_camera(g_walk.camera);
  zmod::world_update_lights(world);
  if (g_hooks.world_sounds) g_hooks.world_sounds(g_hooks.ctx, world);
  if (zmod::shadows_on()) {
    zmod::CullHooks& h = zmod::cull_hooks();
    if (h.collect_casters) h.collect_casters(h.ctx);
    if (h.caster_directions) h.caster_directions(h.ctx);
  }
  const int32_t mode = zmod::zones().mode;
  g_walk.lod_level = 0;
  if (mode) {
    uint8_t* zl = cam->raw + 0x1e4;
    if (cam->get<int32_t>(0x1e0) != 0 && mode == 1) {
      std::memcpy(zmod::zones().list, zl, 4);
    } else {
      uint32_t zone = 0;
      zmod::zones().mode = 0;
      const int32_t hit = g_hooks.zone_probe ? g_hooks.zone_probe(g_hooks.ctx, world, cam->floats(0x2c), &zone) : 0;
      zmod::zones().mode = mode;
      if (!hit) {
        zmod::zone_reset(zl);
        std::memcpy(zmod::zones().list, zl, 4);
      } else if (static_cast<uint8_t>(zone) != 0) {
        std::memcpy(zl, &zone, 4);
        std::memcpy(zmod::zones().list, &zone, 4);
      }
    }
    g_walk.zone = cam->get<uint32_t>(0x1e4);
  }
  if (g_hooks.begin_scene) g_hooks.begin_scene(g_hooks.ctx);
  render_scene(world, camera_node, g_walk.camera);
  if (g_hooks.puffers) g_hooks.puffers(g_hooks.ctx);
  zmth::pop();
  if (g_hooks.flush_transparent) g_hooks.flush_transparent(g_hooks.ctx);
  if (after && g_hooks.after_scene) g_hooks.after_scene(g_hooks.ctx, ztimer::state().dt);
  if (g_hooks.flush_transparent) g_hooks.flush_transparent(g_hooks.ctx);
  if (g_hooks.flush_overlay) g_hooks.flush_overlay(g_hooks.ctx);
  if (g_hooks.draw_quad) g_hooks.draw_quad(g_hooks.ctx);
  if (g_hooks.end_scene) g_hooks.end_scene(g_hooks.ctx);
  // With more than one camera, the window's polygon list (+0x40, 0x34
  // each, +0xe0 count with bit 31 on) gives the rects to repaint.
  if (list_count(kCameras) <= 1) return 0;
  const uint32_t n = window->get<uint32_t>(0xe0);
  if (!(n & 0x80000000u)) return 0;
  for (uint32_t i = 0; i < (window->get<uint32_t>(0xe0) & 0x7fffffffu); ++i) {
    const int base = 0x10 + 0x34 * static_cast<int>(i);
    const uint32_t c = window->get<uint32_t>(base + 0x30);
    if (!(c & 0x80000000u)) continue;
    const float* pt = window->floats(base);
    int32_t rect[4];
    rect[0] = rect[2] = static_cast<int32_t>(pt[0]);
    rect[1] = rect[3] = static_cast<int32_t>(pt[1]);
    for (uint32_t k = 1; k < (c & 0x7fffffffu); ++k) {
      const float x = pt[3 * k], y = pt[3 * k + 1];
      if (static_cast<float>(rect[0]) > x) rect[0] = static_cast<int32_t>(x);
      if (static_cast<float>(rect[2]) < x) rect[2] = static_cast<int32_t>(x);
      if (static_cast<float>(rect[1]) > y) rect[1] = static_cast<int32_t>(y);
      if (static_cast<float>(rect[3]) < y) rect[3] = static_cast<int32_t>(y);
    }
    if (g_hooks.dirty_rect) g_hooks.dirty_rect(g_hooks.ctx, rect);
  }
  return 0;
}

namespace {
// The node's model takes the morph of a morphing LoD, then casts.
void shadow_model_morphed(Node* node) {
  uint8_t* h = node->model ? node->model->header : nullptr;
  if (!h) return;
  if (g_walk.morph) {
    uint32_t f;
    std::memcpy(&f, h + 8, 4);
    f |= 8;
    std::memcpy(h + 8, &f, 4);
    std::memcpy(h + 0x24, &g_walk.morph_factor, 4);
  }
  zmod::shadow_model(node);
}

void shadow_children(Node* node) {
  for (int32_t i = 0; i < node->child_count(); ++i) zmod::shadow_node(node->children[i]);
}
}  // namespace

// Mech3.exe 1.2 @ 0x004f7670. The matrix as the walk's object3d renderer
// pushes it (0x004f5910), but a clean cached one is pushed without the
// local multiply and a dirty one under a tree-valid node is cleaned.
void shadow_object3d(Node* node) {
  if (!(node->flags() & kActive)) return;
  auto* d = static_cast<Object3d*>(node->data);
  const uint32_t flags = node->flags() & ~kUnk25;
  node->set_flags(flags);
  int32_t pushed = 0;
  const uint32_t df = d->get<uint32_t>(0);
  if (!(df & 8)) {
    pushed = 1;
    if (flags & kTreeValid) {
      if (df & 0x20) {
        zmth::push_copy(mat(d->floats(0x60)));
        zmth::multiply(*mat(d->floats(0x30)), 3);
        d->put<uint32_t>(0, d->get<uint32_t>(0) & ~0x20u);
      } else {
        zmth::push(mat(d->floats(0x60)));
      }
    } else {
      zmth::push_copy(mat(d->floats(0x60)));
      zmth::multiply(*mat(d->floats(0x30)), 3);
    }
  }
  node->set_flags(node->flags() | kDrawn);
  shadow_model_morphed(node);
  shadow_children(node);
  if (pushed) zmth::pop();
}

// Mech3.exe 1.2 @ 0x004f7750. Only a LoD whose +4 is 0 or less (or NaN):
// no matrix, no range test.
void shadow_lod(Node* node) {
  if (!(node->flags() & kActive)) return;
  const float level = static_cast<Lod*>(node->data)->get<float>(4);
  if (level > 0.0f) return;
  node->set_flags(node->flags() | kDrawn);
  shadow_model_morphed(node);
  shadow_children(node);
}

}  // namespace zcls
