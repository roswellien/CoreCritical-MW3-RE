#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <map>
#include <string>
#include <vector>

// The scene node and its pool (docs/zclass-world.md, docs/gamez.md). The
// original file is unknown: these functions sit between class.c, cls_util.c
// and cls_zbd.c, so the file is named for the struct.
namespace zmod { struct Model; }

namespace platform { class Alert; }

namespace zcls {

struct ListItem;
struct Node;
using NodeAction = void (*)(Node* node);  // +0x48, run by the action lists with ECX = node

struct SourceAsset {
  std::string name;
  std::map<std::string, unsigned> nodes;
};

struct Node {                      // 0xd4-byte pool slot
  uint8_t raw[0xd0];               // as stored; the fields below replace its pointers
  uint32_t link;                   // +0xd0  bits 0-23 free link, bit 24 live
  void* data = nullptr;            // +0x38  class data
  zmod::Model* model = nullptr;    // +0x3c  an index in the file, a pool slot
  void* action = nullptr;          // +0x48  callback (a NodeAction)
  void* owner = nullptr;           // +0x40  set by its user (an animation's def for its
                                   // own object, zeff_anim_run)
  void* damage = nullptr;          // +0xbc  the damage and touch handlers (zwep::NodeHandlers)
  Node** parents = nullptr;        // +0x58
  Node** children = nullptr;       // +0x60
  ListItem* changed = nullptr;     // +0xcc  its item on list 7
  // Host-only immutable resource identity. Copies keep the originating library
  // entry so optional visuals can bind without changing game-facing raw fields.
  std::shared_ptr<const SourceAsset> source_asset;
  bool source_asset_root = false;
  uint32_t source_model_vertices = 0;

  template <typename T> T get(int off) const {
    T v;
    std::memcpy(&v, raw + off, sizeof v);
    return v;
  }
  template <typename T> void put(int off, T v) { std::memcpy(raw + off, &v, sizeof v); }
  const char* name() const { return reinterpret_cast<const char*>(raw); }
  uint32_t flags() const { return get<uint32_t>(0x24); }
  void set_flags(uint32_t f) { put<uint32_t>(0x24, f); }
  int32_t cls() const { return get<int32_t>(0x34); }
  int32_t parent_count() const { return get<int32_t>(0x54); }
  int32_t child_count() const { return get<int32_t>(0x5c); }
};

constexpr uint32_t kNodeLive = 0x1000000;
constexpr uint32_t kNodeLinkMask = 0xffffff;

// FIX (A): blocks are owned arrays; the exe reallocs a pointer array.
struct NodePool {
  std::vector<std::unique_ptr<Node[]>> blocks;  // 0x0060bf38, count 0x0060bf30
  int32_t block_size = 2500;       // 0x005ea830
  int32_t capacity = 0;            // 0x0060bf34
  int32_t live = 0;                // 0x0060bf3c
  int32_t free_head = -1;          // 0x005ea834
};
NodePool& nodes();

// Frame counters used when a node joins list 7.
struct Frames {
  uint32_t frame = 0;              // 0x0072f338
  uint32_t expiry = 0;             // 0x0073f3a8
  uint32_t update_frame = 1;       // 0x005e9e2c
  int32_t frozen = 0;              // 0x0060b94c
};
Frames& frames();

Node* node_alloc();                // 0x004eacd0
int node_pool_grow();              // 0x004f8c80, 0 ok, 1 out of memory
void node_slot_free(Node* node);   // 0x004f8de0
int32_t node_total();              // 0x004eacb0
int32_t node_live();               // 0x004eacc0
[[noreturn]] void gamez_abort(const char* message);  // 0x004eada0
void set_alert(platform::Alert* alert);
Node* node_at(int32_t index);      // 0x004fe1d0
void free_node(int32_t index);     // 0x004f8ec0
void set_expiry(Node* node);       // 0x004ed990
int node_set_tree_valid(Node* node, int on);  // 0x004ed8f0, 1 for a null node
Node* node_root(Node* node);       // 0x004ed8b0, 0 at a node with several parents
int node_link(Node* parent, Node* child);       // 0x004eba90
int node_set_active(Node* node, int on);        // 0x004eb170
// Mech3.exe 1.2 @ 0x0040ea30: a game object's kind bits (+0x28, replaced),
// its owner (+0x40) and flags (+0x24, or'd) on the node and every child;
// 0x0040ec20 takes them off where the owner is `owner`, the children first.
void node_tree_bind(Node* node, void* owner, uint32_t kind, uint32_t flags);
void node_tree_unbind(Node* node, void* owner, uint32_t kind, uint32_t flags);
int node_set_name(Node* node, const char* name);  // 0x004eb300
void node_touch_frame();                        // 0x004ed960
Node* node_find_in_tree(Node* node, const char* name);  // 0x004f9d60
Node* node_find(Node* node, const char* name);          // 0x004eb0d0: itself, then the children first to last
int node_add_child(Node* parent, Node* child);  // 0x004eb9b0
int node_unlink(Node* parent, Node* child);     // 0x004ebc30, 1 when not its child

// The action callback (+0x48) and its list (+0x44, lists 0..5). A new
// callback puts the node on its list (front or back), a null one takes it off.
int node_set_action(Node* node, NodeAction action);       // 0x004eb450, push front
int node_set_action_back(Node* node, NodeAction action);  // 0x004eb500, push back
int node_set_priority(Node* node, int32_t priority);      // 0x004eb5b0
int node_set_flag8(Node* node, int on);                   // 0x004eb620
int node_get_flag8(Node* node, int32_t* out);             // 0x004eb660
int node_set_proximity(Node* node, int on);               // 0x004eb790, flag 0x40
// The other flag setters the tree copy uses (names from its messages).
int node_set_intersect(Node* node, int on);               // 0x004eb6d0, flag 0x10
int node_set_intersect_bbox(Node* node, int on);          // 0x004eb750, flag 0x20
int node_set_landmark(Node* node, int on);                // 0x004eb7d0, flag 0x80
int node_set_can_modify(Node* node, int on);              // 0x004eb260, flag 0x10000
int node_set_clip_to(Node* node, int on);                 // 0x004eb2b0, flag 0x20000
int node_set_overwrite(Node* node, int on);               // 0x004eb960, flag 0x800000
int node_set_zone_check(Node* node, int on);              // 0x004eb920, flag 0x1000000 (clears only)
uint32_t node_draw_bits(const Node* node);                // 0x004eb880, +0x2c bits 3-4
int node_set_draw_bits(Node* node, uint32_t bits);        // 0x004eb840
int node_set_zone(Node* node, uint8_t zone);              // 0x004eb810, the byte at +0x30
// Depth first, the node then its children last to first: 1 at the first
// node `test` returns 1 for. Original file unknown (zClass, next to 0x004f9d60).
int node_tree_any(Node* node, int (*test)(Node* node));   // 0x004f9e00
int node_is_billboard(Node* node);                        // 0x004f9de0, a CylindricalY facade model

// The node's model's 0x5c bytes (the use count at +0xc), null for none.
uint8_t* node_model_header(const Node* node);
// 0x004eb370: the old model's use count down, the new one's up and its box
// into +0x8c (flag 0x200), the node marked changed (+0x2c bit 2). Null
// drops the model (the use count is the caller's to act on).
int node_set_model(Node* node, zmod::Model* model);       // 0x004eb370
// The node lets its model go; one no node uses any more is freed (0x00539150).
// Inline in 0x004f84b0 and 0x004f9050.
int node_drop_model(Node* node);

// Taking nodes down (docs/zclass-world.md, Destroy). Removing a child is by
// the parent's class; freeing returns the slot when nothing holds the node.
int node_remove_child(Node* parent, Node* child);  // 0x004ebb40, 5 null, 1 not its child
int node_free(Node* node);         // 0x004eaf90, 1 while it has links or a model
int node_release(Node* node);      // 0x004eb050: off the lists, then freed (or deferred)
int node_delete(Node* node);       // 0x004eaea0: by class, only without parents
// [0x005e9e18]: a released node leaves its lists and is freed at once (1),
// or stays listed until the lists are reset (0, the shutdown's setting).
int32_t& immediate_delete();       // 0x004eb040 reads it, 0x004eb030 sets it

// The null and class checks every class setter starts with: 0, or the
// setter's return code (5 null node or data, 3 wrong class).
int check_class(const Node* node, int cls, const char* file, int line);

}  // namespace zcls
