#pragma once
#include <cstdint>
#include <deque>
#include <set>
#include <vector>

#include "gamez/zdeclient/zdec_init.h"
#include "gamez/zweapon/zweapon.h"

namespace zcls { struct Node; struct Partition; }
namespace zgeo { struct ClipSet; }
namespace zmod { struct Material; }

// zDEClient's craters (zdec_crater.cpp; docs/craters.md, section 2): a ring
// on the ground where a shot lands, the ground under it clipped away and a
// sloped ring and floor put in.
namespace zdec {

// A crater being made (0x50 bytes, 0x004ffe80; freed by 0x004ff8f0).
struct CraterRecord {
  int32_t type = 1;                  // +0x00  1 crater, 3 quicksand
  zwep::CraterOrder order;           // +0x04..+0x28  (+0x08 the ring's point count)
  std::vector<float> ring;           // +0x2c
  float box[4] = {};                 // +0x30 min x, +0x34 min z, +0x38 max x, +0x3c max z
  zcls::Partition* cell = nullptr;   // +0x40
  zgeo::ClipSet* set = nullptr;      // +0x44
  CraterTexture* texture = nullptr;  // +0x4c  the ground's crater material and dust
};

// A quicksand pit being made (0x50 bytes, 0x005025e0; freed by 0x005020c0):
// the crater's record one word on (its order is 11 words).
struct QuicksandRecord {
  int32_t type = 3;                  // +0x00
  zwep::QuicksandOrder order;        // +0x04..+0x2c
  zmod::Material* cycle = nullptr;   // +0x0c  (order +0x08) the animated material, zDEClient's by default
  zmod::Material* still = nullptr;   // +0x10  (order +0x0c) its first frame
  std::vector<float> ring;           // +0x30
  float box[4] = {};                 // +0x34
  zcls::Partition* cell = nullptr;   // +0x44
  zgeo::ClipSet* set = nullptr;      // +0x48
};

// What a crater node (named ZDEC_FEATURE) keeps of its record for the next
// crater's overlap test (0x004ffad0): its type and box.
struct Feature {
  int32_t type = 0;
  float box[4] = {};
};

// One crater or quicksand pit as made (0x34 bytes): type and order.
struct HistoryEntry {
  int32_t type = 0;
  uint32_t words[12] = {};
};

struct CraterState {
  int (*veto)(const zwep::CraterOrder* order) = nullptr;  // 0x0060bf88 (multiplayer's 0x0046fd30)
  std::vector<HistoryEntry> history;                      // 0x0060bf94..0x0060bf9c
  // 0x0060bfa0: the nodes whose models craters replaced, in the exe a set by
  // address (the pool's order); here by pool index, whatever the blocks'
  // addresses.
  struct ByIndex {
    bool operator()(const zcls::Node* a, const zcls::Node* b) const;
  };
  std::set<zcls::Node*, ByIndex> touched;
  std::deque<Feature> features;                           // the port's: the crater nodes' records (FIX, see .cpp)
  int (*quicksand_veto)(const zwep::QuicksandOrder* order) = nullptr;  // 0x0060c0c8 (Mech3.exe never sets it)
};
CraterState& craters();

// What a crater calls outside zDEClient and zGeometry.
struct CraterHooks {
  // A host can restore immutable generated geometry that has no archive entry.
  // Return false to retain the ordinary gamez.zbd reread path. The host clears
  // its callback after crater teardown and before releasing its source state.
  bool (*restore_node)(zcls::Node *node) = nullptr;
  // zEntity's record write (0x005a8a50 ECX context, EDX name, stack record
  // and size): the save that sends the craters to a joining player.
  void (*write_record)(void* context, const char* name, const void* record, int32_t size) = nullptr;
  // 0x00508aa0: an animation at a point (the dust), ECX the def, EDX 0.
  void (*play_anim)(zeff::AnimDef* anim, const float at[3]) = nullptr;
};
CraterHooks& crater_hooks();

zcls::Node* world_node();                                   // 0x005020b0
// Shared by crater and quicksand: see zdec_crater.cpp.
bool place_ring(int32_t points, float radius, zwep::Vec3f3& at, std::vector<float>& ring, float box[4],
                zcls::Partition*& cell);
void mark_feature(zcls::Node* node, int32_t type, const float box[4]);
zcls::Partition* cell_at(int32_t x, int32_t z);             // 0x00502090 ECX x, EDX z

// The order's crater, unless the veto says no (then -1).
int32_t crater_make(const zwep::CraterOrder* order);        // 0x004ffaa0 ECX order
// 0 made, -1 not (no cell, the cell full, near another crater, a no-crater
// ground node in the way, or the clip or tessellation failed). With `dust`
// the ground's dust animation plays at the crater.
int32_t crater_instance(const zwep::CraterOrder* order, int dust);  // 0x004ff940 ECX order, EDX dust

CraterRecord* record_new(const zwep::CraterOrder* order);   // 0x004ffe80 ECX order
void record_free(CraterRecord* r);                          // 0x004ff8f0 ECX r
CraterRecord* crater_build(const zwep::CraterOrder* order); // 0x004ffad0 ECX order
int32_t crater_clip(CraterRecord* r);                       // 0x004fff20 ECX r: nodes changed, 0 none or failed
int32_t crater_tessellate(CraterRecord* r);                 // 0x004fff80 ECX r: 0, or -1
// The order as the exe's dwords. FIX (A): a material travels as its table
// index (-1 for none) and the shooter as 0, not as the exe's 32-bit pointers
// (which a 64-bit port cannot keep and which mean nothing on another machine
// a restore runs on); the indices are the same on every machine with the
// same world.
uint32_t material_word(const void* material);
zmod::Material* word_material(uint32_t word);
void order_words(const zwep::CraterOrder& o, uint32_t out[10]);
int32_t history_push(int32_t type, const uint32_t* words);   // 0x00500850 ECX type, EDX order
void quicksand_words(const zwep::QuicksandOrder& o, uint32_t out[11]);

// The quicksand pit (zdec_qsand.cpp): the crater's steps with its own
// tessellation; only while declient.zrd's quicksand is on (0x0060bfe8). No
// shipped weapon orders one.
int32_t quicksand_make(const zwep::QuicksandOrder* order);       // 0x00502110 ECX order
QuicksandRecord* quicksand_record_new(const zwep::QuicksandOrder* order);  // 0x005025e0 ECX order
void quicksand_record_free(QuicksandRecord* r);                 // 0x005020c0 ECX r
QuicksandRecord* quicksand_build(const zwep::QuicksandOrder* order);  // 0x00502230 ECX order
int32_t quicksand_clip(QuicksandRecord* r);                     // 0x00502660 ECX r
int32_t quicksand_tessellate(QuicksandRecord* r);               // 0x005026c0 ECX r: 0, or -1
void note_touched(const zgeo::ClipSet& set);                // 0x00500600 ECX set

// The world as it was before any crater: every touched node's model (and
// its children's) read again from gamez.zbd and its cell's crater count 0,
// every crater node taken out of the world and freed.
void craters_reset();                                       // 0x00500760
// The history and the touched set emptied.
int32_t history_clear();                                    // 0x00500af0
// The mission's unload (decals_free): the history, the quicksand frame names
// and the crater texture table freed, quicksand off.
int32_t decals_free();                                      // 0x00502020
// Each crater's order to `crater`, each quicksand pit's to `quicksand`
// (multiplayer sends them to a joining player, 0x0046d7e3).
void history_each(void (*crater)(const uint32_t* order), void (*quicksand)(const uint32_t* order));  // 0x00500c60 ECX, EDX
// zSave's save and restore of the craters (0x005a8a20("zDEClient", 0x00500b50,
// 0x00500c20, 1000, 0), switched off in MW3, docs/save.md): a "Dummy" record that resets, then one per crater
// ("Crater<n>") or pit ("QSand<n>"), 0x34 bytes: type, order, the reset
// flag at +0x30. Restoring remakes each without dust.
void history_save(void* context);                           // 0x00500b50 ECX context
void history_restore(const uint32_t* record);               // 0x00500c20 stack record

}  // namespace zdec
