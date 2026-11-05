#include "gamez/zclass/cls_util.h"

#include <cstdint>
#include <cstring>

#include <cstdio>

#include "gamez/zclass/camera.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/lod.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmodel/gmod_const.h"

#pragma STDC FP_CONTRACT OFF

namespace zcls {

float approx_sqrt(float x) {
  int32_t b;
  std::memcpy(&b, &x, 4);
  b = (b >> 1) + 0x1fc00000;  // sar: a negative input stays negative
  float r;
  std::memcpy(&r, &b, 4);
  return r;
}

// Mech3.exe 1.2 @ 0x004f9c40
void corners_sphere(const float corners[8][3], float centre[3], float* radius) {
  float lo[3] = {corners[0][0], corners[0][1], corners[0][2]};
  float hi[3] = {lo[0], lo[1], lo[2]};
  for (int i = 1; i < 8; ++i) {
    for (int k = 0; k < 3; ++k) {
      const float v = corners[i][k];
      if (v < lo[k]) lo[k] = v;
      else if (hi[k] < v) hi[k] = v;
    }
  }
  const float hx = (hi[0] - lo[0]) * 0.5f, hy = (hi[1] - lo[1]) * 0.5f, hz = (hi[2] - lo[2]) * 0.5f;
  centre[0] = hx + lo[0], centre[1] = hy + lo[1], centre[2] = hz + lo[2];
  const float xy = hx * hx + hy * hy;
  const float sum = xy + hz * hz;
  *radius = approx_sqrt(sum);
}

// Mech3.exe 1.2 @ 0x004f9bc0
void box_sphere(const float box[6], float centre[3], float* radius) {
  const float hx = (box[3] - box[0]) * 0.5f, hy = (box[4] - box[1]) * 0.5f, hz = (box[5] - box[2]) * 0.5f;
  centre[0] = box[0] + hx, centre[1] = hy + box[1], centre[2] = hz + box[2];
  *radius = approx_sqrt((hx * hx + hy * hy) + hz * hz);
}

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zClass\\cls_util.c";
CopyState g_copy;

// The base copy's error: its message names the source node.
int copy_error(int level, int line, const char* what, int r) {
  char msg[160];
  std::snprintf(msg, sizeof msg, "ERROR copying node while setting %s", what);
  zerr::report(level, kFile, line, msg);
  return r;
}
}  // namespace

CopyState& copy_state() { return g_copy; }

// Mech3.exe 1.2 @ 0x004f90e0. A copied model replaces the shared one unless
// `shared` is set and the model does not change (0x0053b3c0).
int copy_node_model(Node* src, Node* dst) {
  zmod::Model* m = src->model;
  if (!m) return 0;
  if (g_copy.copy_models && (!g_copy.shared || zmod::model_changes(*m))) {
    zmod::Model* c = zmod::model_copy(*m, g_copy.own_materials, g_copy.shared);
    if (!c) return 1;
    return node_set_model(dst, c);
  }
  return node_set_model(dst, m);
}

// Mech3.exe 1.2 @ 0x004f9190. Name, flags, the bits of +0x2c, the model,
// priority, action and zone; the user field (+0x40) is not copied.
int copy_node_base(Node* src, Node* dst) {
  dst->source_asset = src->source_asset;
  dst->source_asset_root = src->source_asset_root;
  dst->source_model_vertices = src->source_model_vertices;
  const uint32_t f = src->flags();
  auto bit = [f](int n) { return static_cast<int>((f >> n) & 1); };
  if (int r = node_set_name(dst, src->name())) return copy_error(0x400, 0x3fb, "description field.", r);
  if (int r = node_set_active(dst, bit(2))) return copy_error(0x400, 0x406, "active field.", r);
  if (int r = node_set_flag8(dst, bit(3))) return copy_error(0x400, 0x411, "altitude surface field.", r);
  if (int r = node_set_intersect(dst, bit(4))) return copy_error(0x400, 0x41c, "intersection field.", r);
  if (int r = node_set_intersect_bbox(dst, bit(5))) return copy_error(0x400, 0x427, "intersect bbox field.", r);
  if (int r = node_set_proximity(dst, bit(6))) return copy_error(0x400, 0x432, "proximity flag", r);
  if (int r = node_set_landmark(dst, bit(7))) return copy_error(0x400, 0x43d, "landmark flag", r);
  if (int r = node_set_can_modify(dst, bit(16))) return copy_error(0x400, 0x448, "can_modify flag", r);
  if (int r = node_set_clip_to(dst, bit(17))) return copy_error(0x400, 0x453, "clip_to flag", r);
  if (int r = node_set_zone_check(dst, bit(24))) return copy_error(0x400, 0x45e, "DI zone check flag", r);
  if (int r = node_set_overwrite(dst, bit(23))) return copy_error(0x400, 0x469, "overwrite flag", r);
  dst->set_flags(dst->flags() | (f & 0x70000000));
  dst->put<uint32_t>(0x28, src->get<uint32_t>(0x28));
  node_set_draw_bits(dst, node_draw_bits(src));
  uint32_t b = dst->get<uint32_t>(0x2c);
  b = (b & ~0x60u) | (src->get<uint32_t>(0x2c) & 0x60);
  dst->put<uint32_t>(0x2c, b);
  if (int r = copy_node_model(src, dst)) {
    zerr::report(0x400, kFile, 0x480, "ERROR copying node graphics data.");
    return r;
  }
  if (src->owner) {
    char msg[96];
    std::snprintf(msg, sizeof msg, "Source node (%s) has environment data.  Ignoring.", src->name());
    zerr::report(0x100, kFile, 0x48a, msg);
  }
  dst->owner = nullptr;
  if (int r = node_set_priority(dst, src->get<int32_t>(0x44)))
    return copy_error(0x100, 0x494, "action callback priority field.", r);
  if (int r = node_set_action(dst, reinterpret_cast<NodeAction>(src->action)))
    return copy_error(0x100, 0x49f, "action callback field", r);
  if (int r = node_set_zone(dst, src->raw[0x30])) return copy_error(0x100, 0x4aa, "zone ID.", r);
  return 0;
}

namespace {
void base_failed(int line) { zerr::report(0x100, kFile, line, "ERROR copying node base data."); }

// Mech3.exe 1.2 @ 0x004f96f0. Its transform unless it is the identity
// (flag 8): a given matrix as such, else translation, rotation, scale.
// A child that fails to copy is left out.
Node* copy_object3d(Node* src) {
  Node* dst = object3d_create();
  if (!dst) return nullptr;
  if (copy_node_base(src, dst)) return base_failed(0x56b), nullptr;
  const auto* d = static_cast<const Object3d*>(src->data);
  const uint32_t flags = d->get<uint32_t>(0);
  if (object3d_set_opacity(dst, d->get<float>(4))) return nullptr;
  if (object3d_use_opacity(dst, (flags >> 1) & 1)) return nullptr;
  if (!(flags & 8)) {
    if (flags & 0x10) {
      if (object3d_set_matrix(dst, reinterpret_cast<const float*>(d->raw + 0x30))) return nullptr;
    } else {
      if (object3d_set_translation(dst, d->get<float>(0x54), d->get<float>(0x58), d->get<float>(0x5c))) return nullptr;
      if (object3d_set_rotation(dst, d->get<float>(0x18), d->get<float>(0x1c), d->get<float>(0x20))) return nullptr;
      if (object3d_set_scale(dst, d->get<float>(0x24), d->get<float>(0x28), d->get<float>(0x2c))) return nullptr;
    }
  }
  for (int32_t i = 0; i < src->child_count(); ++i)
    if (Node* c = node_copy(src->children[i]))
      if (object3d_add_child(dst, c)) return nullptr;
  return dst;
}

// Mech3.exe 1.2 @ 0x004f9560. A child that fails to copy fails the copy.
Node* copy_camera(Node* src) {
  Node* dst = camera_create();
  if (!dst) return nullptr;
  if (copy_node_base(src, dst)) return base_failed(0x4d4), nullptr;
  auto* d = static_cast<Camera*>(src->data);
  if (camera_set_world(dst, d->nodes[0])) return nullptr;
  if (camera_set_window(dst, d->nodes[1])) return nullptr;
  if (camera_set_position(dst, d->get<float>(0x14), d->get<float>(0x18), d->get<float>(0x1c))) return nullptr;
  if (camera_set_rotation(dst, d->get<float>(0x20), d->get<float>(0x24), d->get<float>(0x28))) return nullptr;
  if (camera_set_clip(dst, d->get<float>(0xb0), d->get<float>(0xb4))) return nullptr;
  if (camera_set_lod_scale(dst, d->get<float>(0xd0))) return nullptr;
  if (camera_set_fov_angles(dst, d->get<float>(0xe8), d->get<float>(0xec))) return nullptr;
  for (int32_t i = 0; i < src->child_count(); ++i) {
    Node* c = node_copy(src->children[i]);
    if (!c || camera_add_child(dst, c)) return nullptr;
  }
  return dst;
}

// Mech3.exe 1.2 @ 0x004f9840. The data's +4..+0x44 as they are; the second
// range goes through the engine's square root guess (it is squared again).
Node* copy_lod(Node* src) {
  Node* dst = lod_create();
  if (!dst) return nullptr;
  if (copy_node_base(src, dst)) return base_failed(0x5f0), nullptr;
  const auto* s = static_cast<const Lod*>(src->data);
  auto* d = static_cast<Lod*>(dst->data);
  if (lod_set_level(dst, s->get<uint32_t>(0))) return nullptr;
  std::memcpy(d->raw + 4, s->raw + 4, 0x44);
  if (lod_set_range2(dst, s->get<uint32_t>(0x48), approx_sqrt(s->get<float>(0x4c)))) return nullptr;
  for (int32_t i = 0; i < src->child_count(); ++i) {
    Node* c = node_copy(src->children[i]);
    if (!c || node_link(dst, c)) return nullptr;
  }
  return dst;
}

Node* refuse(int level, int line, const char* msg) {
  zerr::report(level, kFile, line, msg);
  return nullptr;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004f99f0
Node* node_copy(Node* node) {
  if (!node) return refuse(0x400, 0x66a, "Null node pointer.");
  if (node->flags() & 0x4000000) return node;
  switch (node->cls()) {
    case 1: return copy_camera(node);
    case 2: return refuse(0x100, 0x693, "ERROR cannot copy world class node.");
    case 5: return copy_object3d(node);
    case 6: return copy_lod(node);
    case 7: return refuse(0x100, 0x637, "ERROR copying sequence node; Function not implemented yet.");  // 0x004f99b0
    case 8: return refuse(0x100, 0x5ca, "ERROR copying animate node; Function not implemented yet.");   // 0x004f9820
    case 9: return refuse(0x800, 0x52f, "Can't copy light node; Function not yet implemented");        // 0x004f96b0
    case 10: return refuse(0x800, 0x545, "Can't copy sound node; Function not yet implemented");       // 0x004f96d0
    case 11: return refuse(0x100, 0x64e, "ERROR copying switch node; Function not implemented yet.");  // 0x004f99d0
    default: {
      char msg[96];
      std::snprintf(msg, sizeof msg, "ERROR Unrecognized node in copying process: %s", node->name());
      return refuse(0x100, 0x69a, msg);
    }
  }
}

// Mech3.exe 1.2 @ 0x004f9af0
Node* tree_copy(Node* node, int32_t copy_models, int32_t own_materials) {
  if (!node) return refuse(0x400, 0x6c1, "Null node pointer.");
  const CopyState saved = g_copy;
  g_copy.copy_models = copy_models, g_copy.own_materials = own_materials;
  Node* c = node_copy(node);
  g_copy.copy_models = saved.copy_models, g_copy.own_materials = saved.own_materials;
  return c;
}

// Mech3.exe 1.2 @ 0x004f9b50
Node* tree_copy_materials(Node* node, int32_t own_materials, int32_t shared) {
  if (!node) return refuse(0x400, 0x6fa, "Null node pointer.");
  const CopyState saved = g_copy;
  g_copy.own_materials = own_materials, g_copy.shared = shared;
  Node* c = node_copy(node);
  g_copy.own_materials = saved.own_materials, g_copy.shared = saved.shared;
  return c;
}

// Mech3.exe 1.2 @ 0x004f9050. A child left without parents goes with it; a
// failing child stops the walk, not the node's own delete.
int tree_destroy(Node* node) {
  if (!node || node->parent_count() > 0) return 1;
  while (node->child_count() > 0) {
    Node* child = node->children[0];
    if (node_remove_child(node, child) != 0) break;
    if (child->parent_count() == 0 && tree_destroy(child) != 0) break;
  }
  if (int r = node_drop_model(node)) return r;
  return node_delete(node);
}

}  // namespace zcls
