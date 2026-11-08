#include "gamez/zclass/cls_node.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/light.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zclass/seq.h"
#include "gamez/zclass/sound.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmodel/gmod_const.h"
#include "platform/platform.h"

namespace zcls {

namespace {
NodePool g_pool;
Frames g_frames;
platform::Alert* g_alert = nullptr;
}  // namespace

NodePool& nodes() { return g_pool; }
Frames& frames() { return g_frames; }

void set_alert(platform::Alert* alert) { g_alert = alert; }

// Mech3.exe 1.2 @ 0x004eada0. FIX (A): the message box goes through the
// platform; the exe also leaves the DirectDraw mode first.
void gamez_abort(const char* message) {
  if (g_alert) g_alert->abort_message(message);
  for (std::FILE* f : {stdout, stderr}) {
    std::fprintf(f, "%s", message);
    std::fprintf(f, "ABORT.  Shutting down...\n");
    std::fflush(f);
  }
  std::exit(0);
}

// Mech3.exe 1.2 @ 0x004f8c80. The new block's slots link in order.
int node_pool_grow() {
  NodePool& p = g_pool;
  Node* block = new (std::nothrow) Node[p.block_size]{};
  if (!block) return 1;
  p.blocks.emplace_back(block);
  p.free_head = p.capacity;
  p.capacity += p.block_size;
  for (int32_t i = 0; i < p.block_size - 1; ++i) block[i].link = static_cast<uint32_t>(p.free_head + i + 1);
  block[p.block_size - 1].link |= kNodeLinkMask;
  return 0;
}

// Mech3.exe 1.2 @ 0x004eacd0. Every node starts on list 6 with these defaults.
Node* node_alloc() {
  NodePool& p = g_pool;
  if (p.free_head == -1) node_pool_grow();
  if (p.free_head == -1) gamez_abort("GameZ node resource failure: restarting your game usually solves this.");
  Node* n = node_at(p.free_head);
  p.free_head = static_cast<int32_t>(n->link << 8) >> 8;  // sign-extended: 0xffffff is -1
  const uint32_t link = n->link;
  *n = Node{};
  n->link = link;
  static uint64_t g_next_alloc_id = 0;
  n->allocation_id = ++g_next_alloc_id;
  ++p.live;
  push_front(kAllNodes, n);
  n->set_flags(0x0108001c);
  n->put<int32_t>(0x44, 1);
  n->put<int32_t>(0x4c, -1), n->put<int32_t>(0x50, -1);
  n->raw[0x30] = 0xff;
  std::snprintf(reinterpret_cast<char*>(n->raw), 0x24, "%s", "Default_node_name");
  n->put<int32_t>(0xbc, 0);
  return n;
}

// Mech3.exe 1.2 @ 0x004f8de0. The slot keeps its contents and lists.
void node_slot_free(Node* node) {
  NodePool& p = g_pool;
  int32_t index = -1;
  for (size_t b = 0; b < p.blocks.size(); ++b) {
    const Node* base = p.blocks[b].get();
    if (node >= base && node <= base + p.block_size - 1)
      index = static_cast<int32_t>(b) * p.block_size + static_cast<int32_t>(node - base);
  }
  node->link = (node->link & ~kNodeLinkMask) | (static_cast<uint32_t>(p.free_head) & kNodeLinkMask);
  p.free_head = index;
  --p.live;
}

// Mech3.exe 1.2 @ 0x004eacb0
int32_t node_total() { return g_pool.capacity; }

// Mech3.exe 1.2 @ 0x004eacc0
int32_t node_live() { return g_pool.live; }

// Mech3.exe 1.2 @ 0x004fe1d0. No upper bound check.
Node* node_at(int32_t index) {
  if (index < 0) return nullptr;
  return &g_pool.blocks[index / g_pool.block_size][index % g_pool.block_size];
}

// Mech3.exe 1.2 @ 0x004f8ec0. Only the link changes; the slot keeps its contents.
void free_node(int32_t index) {
  Node* n = node_at(index);
  n->link = (n->link & ~kNodeLinkMask) | (static_cast<uint32_t>(g_pool.free_head) & kNodeLinkMask);
  g_pool.free_head = index;
  --g_pool.live;
}

// Mech3.exe 1.2 @ 0x004ed990. Children last to first.
void set_expiry(Node* node) {
  node->put<uint32_t>(0xc0, 0);
  node->put<uint32_t>(0xc4, g_frames.expiry);
  for (int32_t i = node->child_count() - 1; i >= 0; --i) set_expiry(node->children[i]);
}

namespace {
const char kClassFile[] = "C:\\zipper\\GameZMech3\\zClass\\Class.c";

template <typename T>
T* append(T* array, int32_t count, T value) {
  T* grown = new T[count + 1];
  for (int32_t i = 0; i < count; ++i) grown[i] = array[i];
  grown[count] = value;
  delete[] array;
  return grown;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004ed8f0. TREE_VALID (0x80000): one path to the root.
// Setting it stops at a node with several parents; clearing goes all the way.
int node_set_tree_valid(Node* node, int on) {
  if (!node) return 1;
  if (on) {
    if (node->parent_count() > 1) return 0;
    node->set_flags(node->flags() | 0x80000);
  } else {
    node->set_flags(node->flags() & ~0x80000u);
  }
  for (int32_t i = 0; i < node->child_count(); ++i) node_set_tree_valid(node->children[i], on);
  return 0;
}

// Mech3.exe 1.2 @ 0x004ed8b0
Node* node_root(Node* node) {
  while (node) {
    if (node->parent_count() == 0) return node;
    if (node->parent_count() != 1) {
      char msg[96];
      std::snprintf(msg, sizeof msg, "Error getting root node; Multiple parents found.\n  Node: %s\n", node->name());
      zerr::report(0x200, kClassFile, 0x10b4, msg);
      return nullptr;
    }
    node = node->parents[0];
  }
  return nullptr;
}

// Mech3.exe 1.2 @ 0x004eba90. No duplicate check. FIX (A): new arrays for realloc.
int node_link(Node* parent, Node* child) {
  parent->children = append(parent->children, parent->child_count(), child);
  parent->put<int32_t>(0x5c, parent->child_count() + 1);
  child->parents = append(child->parents, child->parent_count(), parent);
  child->put<int32_t>(0x54, child->parent_count() + 1);
  if (child->parent_count() > 1) node_set_tree_valid(child, 0);
  parent->put<uint32_t>(0x2c, parent->get<uint32_t>(0x2c) | 2);
  if ((parent->flags() & 1) == 0) {
    push_front(kChanged, parent);
    parent->set_flags(parent->flags() | 1);
  }
  parent->set_flags(parent->flags() | 2);
  if (parent->changed) parent->changed->removable = 0;
  return 0;
}

// Mech3.exe 1.2 @ 0x004eb9b0. By the parent's class; the classes whose own
// add-child is not ported yet stop the game rather than guess.
int node_add_child(Node* parent, Node* child) {
  if (!child) return zerr::report(0x400, kClassFile, 0x78f, "Null node pointer."), 5;
  if (!parent) return zerr::report(0x400, kClassFile, 0x790, "Null node pointer."), 5;
  if (parent == child) return 1;
  switch (parent->cls()) {
    case 2: return world_add_child(parent, child);
    case 3: case 4: case 9: case 10: return node_link(parent, child);
    case 5: return object3d_add_child(parent, child);
    default: {
      char msg[80];
      std::snprintf(msg, sizeof msg, "node_add_child: class %d is not ported yet", parent->cls());
      gamez_abort(msg);
    }
  }
}

// Mech3.exe 1.2 @ 0x004ed960
void node_touch_frame() {
  if (g_frames.frozen == 0) g_frames.update_frame = g_frames.frame + 1;
}

// Mech3.exe 1.2 @ 0x0040ea30
void node_tree_bind(Node* node, void* owner, uint32_t kind, uint32_t flags) {
  if (!node) return;
  node->put<uint32_t>(0x28, kind);
  node->owner = owner;
  node->put<uint32_t>(0x24, node->get<uint32_t>(0x24) | flags);
  for (int32_t i = 0; i < node->child_count(); ++i) node_tree_bind(node->children[i], owner, kind, flags);
}

// Mech3.exe 1.2 @ 0x0040ec20
void node_tree_unbind(Node* node, void* owner, uint32_t kind, uint32_t flags) {
  if (!node) return;
  for (int32_t i = 0; i < node->child_count(); ++i) node_tree_unbind(node->children[i], owner, kind, flags);
  if (node->owner == owner) {
    node->put<uint32_t>(0x28, node->get<uint32_t>(0x28) & ~kind);
    node->owner = nullptr;
    node->put<uint32_t>(0x24, node->get<uint32_t>(0x24) & ~flags);
  }
}

// Mech3.exe 1.2 @ 0x004eb170. Flag 4 is "active"; on is 1 or 0, anything
// else leaves it.
int node_set_active(Node* node, int on) {
  if (!node) return zerr::report(0x400, kClassFile, 0x48e, "Null node pointer."), 5;
  switch (node->cls()) {
    case 1: case 2: case 5: case 6:
      break;
    case 9: {
      auto* light = static_cast<Light*>(node->data);
      if (light->get<uint32_t>(0xb0) & 0x100) {
        const uint32_t f = on ? ~node->flags() : node->flags();
        if ((f >> 2) & 1) node_touch_frame();
      }
      break;
    }
    case 10:
      sound_set_active(node, on);
      return 0;
    default: {
      char msg[120];
      std::snprintf(msg, sizeof msg, "gwNodeSetActive(): Unrecognized node class type:  node = %s class_type = %d\n",
                    node->name(), node->cls());
      zerr::report(0x400, kClassFile, 0x4aa, msg);
      return 3;
    }
  }
  if (on == 1) node->set_flags(node->flags() | 4);
  else if (on == 0) node->set_flags(node->flags() & ~4u);
  return 0;
}

// Mech3.exe 1.2 @ 0x004eb300. A name over 35 characters keeps its first 34;
// the byte after them is left as it was.
int node_set_name(Node* node, const char* name) {
  if (!node) return zerr::report(0x400, kClassFile, 0x4e5, "Null node pointer."), 5;
  char* dst = reinterpret_cast<char*>(node->raw);
  if (std::strlen(name) > 0x23) {
    std::strncpy(dst, name, 0x22);
    dst[0x23] = 0;
  } else {
    std::strcpy(dst, name);
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004f9d60. The node or a descendant by name, children
// last to first. Original file unknown (zClass).
Node* node_find_in_tree(Node* node, const char* name) {
  if (!node) return nullptr;
  if (std::strcmp(name, node->name()) == 0) return node;
  for (int32_t i = node->child_count(); i > 0;)
    if (Node* hit = node_find_in_tree(node->children[--i], name)) return hit;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x004eb0d0. The name matched whole, the node itself first,
// then each child's tree, first to last (node_find_in_tree, 0x004f9d60, goes
// last to first).
Node* node_find(Node* node, const char* name) {
  if (!node) return zerr::report(0x400, kClassFile, 0x43b, "Null node pointer."), nullptr;
  if (std::strcmp(node->name(), name) == 0) return node;
  for (int32_t i = 0; i < node->child_count(); ++i)
    if (Node* hit = node_find(node->children[i], name)) return hit;
  return nullptr;
}

namespace {
// 0x004eb450 / 0x004eb500. The list push never fails, so the exe's free() of
// the node on failure is dead.
int set_action(Node* node, NodeAction action, int null_line, bool back) {
  if (!node) return zerr::report(0x400, kClassFile, null_line, "Null node pointer."), 5;
  const int32_t list = node->get<int32_t>(0x44);
  if (list < 0 || list >= 6) {
    char msg[64];
    std::snprintf(msg, sizeof msg, "ERROR setting action callback; priority = %d", list);
    return zerr::report(0x400, kClassFile, null_line + 5, msg), 1;
  }
  if (!node->action) {
    if (action) back ? push_back(list, node) : push_front(list, node);
  } else if (!action) {
    list_remove(list, node);
  }
  node->action = reinterpret_cast<void*>(action);
  return 0;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004eb450
int node_set_action(Node* node, NodeAction action) { return set_action(node, action, 0x584, false); }

// Mech3.exe 1.2 @ 0x004eb500
int node_set_action_back(Node* node, NodeAction action) { return set_action(node, action, 0x5c9, true); }

// Mech3.exe 1.2 @ 0x004eb5b0. A node with an action moves to the new list's
// front; any priority is stored, a list exists only for 0..5.
int node_set_priority(Node* node, int32_t priority) {
  if (!node) return zerr::report(0x400, kClassFile, 0x602, "Null node pointer."), 5;
  if (node->action) {
    const int32_t old = node->get<int32_t>(0x44);
    if (old >= 0 && old < 6) list_remove(old, node);
    if (priority >= 0 && priority < 6) push_front(priority, node);
  }
  node->put<int32_t>(0x44, priority);
  return 0;
}

// Mech3.exe 1.2 @ 0x004eb620
int node_set_flag8(Node* node, int on) {
  if (!node) return zerr::report(0x400, kClassFile, 0x62f, "Null node pointer."), 5;
  node->set_flags(on ? node->flags() | 8 : node->flags() & ~8u);
  return 0;
}

// Mech3.exe 1.2 @ 0x004eb660
int node_get_flag8(Node* node, int32_t* out) {
  if (!node) return zerr::report(0x400, kClassFile, 0x648, "Null node pointer."), 5;
  *out = (node->flags() >> 3) & 1;
  return 0;
}

namespace {
int set_flag(Node* node, uint32_t flag, int on, int line) {
  if (!node) return zerr::report(0x400, kClassFile, line, "Null node pointer."), 5;
  node->set_flags(on ? node->flags() | flag : node->flags() & ~flag);
  return 0;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004eb6d0
int node_set_intersect(Node* node, int on) { return set_flag(node, 0x10, on, 0x672); }
// Mech3.exe 1.2 @ 0x004eb750
int node_set_intersect_bbox(Node* node, int on) { return set_flag(node, 0x20, on, 0x6a0); }
// Mech3.exe 1.2 @ 0x004eb7d0
int node_set_landmark(Node* node, int on) { return set_flag(node, 0x80, on, 0x6e7); }
// Mech3.exe 1.2 @ 0x004eb260
int node_set_can_modify(Node* node, int on) { return set_flag(node, 0x10000, on, 0x4bd); }
// Mech3.exe 1.2 @ 0x004eb2b0
int node_set_clip_to(Node* node, int on) { return set_flag(node, 0x20000, on, 0x4cc); }
// Mech3.exe 1.2 @ 0x004eb960
int node_set_overwrite(Node* node, int on) { return set_flag(node, 0x800000, on, 0x756); }

// Mech3.exe 1.2 @ 0x004eb920. Only ever clears the flag (alloc sets it).
int node_set_zone_check(Node* node, int on) {
  if (!node) return zerr::report(0x400, kClassFile, 0x738, "Null node pointer."), 5;
  if ((node->flags() & 0x1000000) && on == 0) node->set_flags(node->flags() & ~0x1000000u);
  return 0;
}

// Mech3.exe 1.2 @ 0x004eb880. 0 for a null node.
uint32_t node_draw_bits(const Node* node) { return node ? (node->get<uint32_t>(0x2c) >> 3) & 3 : 0; }

// Mech3.exe 1.2 @ 0x004eb840
int node_set_draw_bits(Node* node, uint32_t bits) {
  if (!node) return zerr::report(0x400, kClassFile, 0x707, "Null node pointer."), 5;
  node->put<uint32_t>(0x2c, ((bits & 3) << 3) | (node->get<uint32_t>(0x2c) & ~0x18u));
  return 0;
}

// Mech3.exe 1.2 @ 0x004eb810
int node_set_zone(Node* node, uint8_t zone) {
  if (!node) return zerr::report(0x400, kClassFile, 0x6ff, "Null node pointer."), 5;
  node->raw[0x30] = zone;
  return 0;
}

// Mech3.exe 1.2 @ 0x004eb790. mech3ax PROXIMITY: only set once anims are loaded.
int node_set_proximity(Node* node, int on) {
  if (!node) return zerr::report(0x400, kClassFile, 0x6cd, "Null node pointer."), 5;
  node->set_flags(on ? node->flags() | 0x40 : node->flags() & ~0x40u);
  return 0;
}

// Mech3.exe 1.2 @ 0x004ebc30. The parent's +0x2c bit 2, then marked changed;
// the child regains TREE_VALID when left with one parent that has it.
int node_unlink(Node* parent, Node* child) {
  int32_t at = -1;
  for (int32_t i = 0; i < parent->child_count(); ++i)
    if (parent->children[i] == child) {
      at = i;
      break;
    }
  if (at < 0) {
    char msg[120];
    std::snprintf(msg, sizeof msg, "ERROR deleting child node %s from parent node %s", child->name(), parent->name());
    return zerr::report(0x200, kClassFile, 0x8c8, msg), 1;
  }
  for (int32_t i = at; i < parent->child_count() - 1; ++i) parent->children[i] = parent->children[i + 1];
  parent->put<int32_t>(0x5c, parent->child_count() - 1);
  at = -1;
  for (int32_t i = 0; i < child->parent_count(); ++i)
    if (child->parents[i] == parent) {
      at = i;
      break;
    }
  if (at >= 0) {
    for (int32_t i = at; i < child->parent_count() - 1; ++i) child->parents[i] = child->parents[i + 1];
    child->put<int32_t>(0x54, child->parent_count() - 1);
    if (child->parent_count() == 1 && (parent->flags() & 0x80000)) node_set_tree_valid(child, 1);
  }
  parent->put<uint32_t>(0x2c, parent->get<uint32_t>(0x2c) | 2);
  if ((parent->flags() & 1) == 0) {
    push_front(kChanged, parent);
    parent->set_flags(parent->flags() | 1);
  }
  parent->set_flags(parent->flags() | 2);
  if (parent->changed) parent->changed->removable = 0;
  return 0;
}

// Mech3.exe 1.2 @ 0x004f9e00
int node_tree_any(Node* node, int (*test)(Node* node)) {
  if (test(node) == 1) return 1;
  for (int32_t i = node->child_count() - 1; i >= 0; --i)
    if (node_tree_any(node->children[i], test) == 1) return 1;
  return 0;
}

// Mech3.exe 1.2 @ 0x004f9de0. Model type 1 (facade), mode 0 (cylindrical y).
int node_is_billboard(Node* node) {
  const zmod::Model* m = node->model;
  if (!m) return 0;
  uint32_t type, mode;
  std::memcpy(&type, m->header, 4), std::memcpy(&mode, m->header + 4, 4);
  return type == 1 && mode == 0 ? 1 : 0;
}

uint8_t* node_model_header(const Node* node) { return node->model ? node->model->header : nullptr; }

namespace {
int32_t g_immediate = 1;  // .data initial 1

void add_uses(uint8_t* header, int32_t d) {
  int32_t n;
  std::memcpy(&n, header + 0xc, 4);
  n += d;
  std::memcpy(header + 0xc, &n, 4);
}
}  // namespace

int32_t& immediate_delete() { return g_immediate; }

// Mech3.exe 1.2 @ 0x004eb370
int node_set_model(Node* node, zmod::Model* model) {
  if (!node) return zerr::report(0x400, kClassFile, 0x52a, "Null node pointer."), 5;
  if (node->model) add_uses(node->model->header, -1);  // 0x00539d60
  node->model = model;
  if (model) {
    add_uses(model->header, 1);  // 0x00539d50
    node_model_box(node);        // 0x0053b4e0 into +0x8c, flag 0x200
  } else {
    node->set_flags(node->flags() & ~0x200u);
  }
  node->put<uint32_t>(0x2c, node->get<uint32_t>(0x2c) | 4);
  if ((node->flags() & 1) == 0) {
    push_front(kChanged, node);
    node->set_flags(node->flags() | 1);
  }
  node->set_flags(node->flags() | 2);
  if (node->changed) node->changed->removable = 0;
  return 0;
}

int node_drop_model(Node* node) {
  zmod::Model* m = node->model;
  if (!m) return 0;
  if (int r = node_set_model(node, nullptr)) return r;
  if (m->count(0xc) != 0) return 0;  // 0x00539d70
  return zmod::model_release(m);
}

namespace {
// The class data as its class made it (cls_data.h); the arrays it points to
// are freed by the class's own delete before this.
void delete_class_data(Node* node) {
  switch (node->cls()) {
    case 1: delete static_cast<Camera*>(node->data); break;
    case 2: {
      auto* w = static_cast<World*>(node->data);  // the lists a staying world kept (world_delete)
      delete[] w->lights, delete[] w->light_data, delete[] w->sounds, delete[] w->sound_data;
      delete[] w->area_pending;
      delete w;
      break;
    }
    case 3: delete static_cast<Window*>(node->data); break;
    case 4: delete static_cast<Display*>(node->data); break;
    case 5: delete static_cast<Object3d*>(node->data); break;
    case 6: delete static_cast<Lod*>(node->data); break;
    case 7: delete static_cast<Seq*>(node->data); break;
    case 8: delete static_cast<Animate*>(node->data); break;
    case 9: delete static_cast<Light*>(node->data); break;
    case 10: delete static_cast<Sound*>(node->data); break;
    default: break;  // no class makes other data
  }
}

// The child removals whose whole body is null checks and 0x004ebc30:
// Camera.c 0x004f33e0, Window.c 0x004f9f60, Display.c 0x004fa540 (0 even
// when the child is not its), Animate.c 0x004fd900, Light.c 0x004fa900,
// Sound.c 0x004fd440, Switch.c 0x004fd230 (the last three-line ones also
// check the data).
int plain_remove_child(Node* parent, Node* child) {
  struct Lines {
    const char* file;
    int line;
    bool data;
  };
  Lines l{};
  switch (parent->cls()) {
    case 1: l = {"C:\\zipper\\GameZMech3\\zClass\\Camera.c", 0x2fb, false}; break;
    case 3: l = {"C:\\zipper\\GameZMech3\\zClass\\Window.c", 0xb6, false}; break;
    case 4: l = {"C:\\zipper\\GameZMech3\\zClass\\Display.c", 0x8f, false}; break;
    case 8: l = {"C:\\zipper\\GameZMech3\\zClass\\Animate.c", 0xa4, true}; break;
    case 9: l = {"C:\\zipper\\GameZMech3\\zClass\\Light.c", 0x14c, false}; break;
    case 10: l = {"C:\\zipper\\GameZMech3\\zClass\\Sound.c", 0x100, false}; break;
    default: l = {"C:\\zipper\\GameZMech3\\zClass\\Switch.c", 0x9f, true}; break;
  }
  if (!child) return zerr::report(0x400, l.file, l.line + 1, "Null node pointer."), 5;
  if (l.data && !parent->data) return zerr::report(0x400, l.file, l.line + 2, "Null class data pointer"), 5;
  const int r = node_unlink(parent, child);
  return parent->cls() == 4 ? 0 : r;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004ebb40. By the parent's class; an unknown class is
// reported (0x0050da70) and refused.
int node_remove_child(Node* parent, Node* child) {
  if (!parent) return zerr::report(0x400, kClassFile, 0x83f, "Null node pointer."), 5;
  if (!child) return zerr::report(0x400, kClassFile, 0x840, "Null node pointer."), 5;
  switch (parent->cls()) {
    case 1: case 3: case 4: case 8: case 9: case 10: case 11: return plain_remove_child(parent, child);
    case 2: return world_remove_child(parent, child);
    case 5: return object3d_remove_child(parent, child);
    case 6: node_unlink(parent, child); return 0;  // the LoD's 0x004fe0b0
    case 7: return seq_remove_child(parent, child);
    default: {
      char msg[120];
      std::snprintf(msg, sizeof msg, "%s: Line %d: ERROR: Unrecognized node class type for node: %s\n", kClassFile,
                    0x874, parent->name());
      zerr::report(1, "C:\\zipper\\GameZMech3\\zError\\zerr_old.c", 0x23, msg);
      return 1;
    }
  }
}

// Mech3.exe 1.2 @ 0x004eaf90. Nothing is freed while the node has children,
// parents or a model; the slot keeps its bytes.
int node_free(Node* node) {
  if (!node) return zerr::report(0x400, kClassFile, 0x381, "Null node pointer."), 5;
  if (node->child_count() > 0 || node->parent_count() > 0 || node_model_header(node)) return 1;
  delete[] node->children;
  node->children = nullptr;
  delete[] node->parents;
  node->parents = nullptr;
  if (node->data) {
    delete_class_data(node);
    node->data = nullptr;
    node->put<int32_t>(0x34, 0);
  }
  node_slot_free(node);
  return 0;
}

// Mech3.exe 1.2 @ 0x004eb050. While the lists cannot purge (an action list
// is running) the node waits on the deferred-delete list.
int node_release(Node* node) {
  if (!node) return zerr::report(0x400, kClassFile, 0x3ea, "Null node pointer."), 5;
  node->set_flags(node->flags() & ~2u);
  if (node->changed) node->changed->removable = 1;
  if (g_immediate) {
    node_unlist(node);
    if (purge_lists() != 0) {
      deferred_push(node);
      return 0;
    }
  }
  node_free(node);
  return 0;
}

// Mech3.exe 1.2 @ 0x004eaea0. Only a node without parents. The exe frees a
// block at +0xc8 first; nothing in the port keeps one there.
int node_delete(Node* node) {
  if (!node) return 5;
  if (node->parent_count() >= 1) return 1;
  switch (node->cls()) {
    case 1: case 3: case 4: case 5: case 6: case 7: case 11: return node_release(node);
    case 2: return world_delete(node);
    case 8: return animate_delete(node);
    case 9: return light_delete(node);
    case 10: return sound_delete(node);
    default: {
      char msg[96];
      std::snprintf(msg, sizeof msg, "ERROR: Unrecognized node class type for node: %s\n", node->name());
      zerr::report(0x400, kClassFile, 0x365, msg);
      return 1;
    }
  }
}

// The message's arguments are swapped: "Wanted" shows the class found.
int check_class(const Node* node, int cls, const char* file, int line) {
  if (!node) return zerr::report(0x400, file, line, "Null node pointer."), 5;
  if (!node->data) return zerr::report(0x400, file, line + 1, "Null class data pointer"), 5;
  if (node->cls() != cls) {
    char msg[64];
    std::snprintf(msg, sizeof msg, "Bad Class Found.\n Wanted (%d)\n Found (%d)", node->cls(), cls);
    return zerr::report(0x400, file, line + 2, msg), 3;
  }
  return 0;
}

}  // namespace zcls
