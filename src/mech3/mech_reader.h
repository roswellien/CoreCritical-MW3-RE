#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace zrdr { struct Node; }

// mech3\mech_reader.cpp: a mech's definition from its dfn_<mech>.zrd
// (docs/mechdef-skins.md, docs/mechdef-parts.md). The reader 0x0046ba40 is
// ported a block at a time.
namespace mech3 {

struct SkinSet;
struct ConfigList;

struct Firepoint {                 // 0x20 bytes
  const char* name;                // +0x00  borrowed from the reader tree
  float radius;                    // +0x04  scatter of salvo shots
  float share;                     // +0x08  offset of a second weapon on it
  int32_t part;                    // +0x0c  from the file's "part"
  int32_t model_part = -1;         // +0x10  set later, from the model (0x0046cb80)
  float position[3] = {};          // +0x14
};

struct PartSlot {                  // 0x10 bytes
  const char* name = nullptr;
  float f4 = 1.0f;                 // meaning unknown
  int32_t i8 = -1;                 // meaning unknown
  int32_t id = -1;
  bool set(int32_t id, zrdr::Node* unused);  // 0x0046ba20
};

struct MechDfn {                   // 0x1a8 bytes
  uint8_t raw[0x1a8] = {};         // the plain fields; the members below replace the pointers
  int32_t index = 0;               // +0x00
  uint8_t loaded = 0;              // +0x04
  zrdr::Node* reader = nullptr;    // +0x08
  SkinSet* skins = nullptr;        // +0x1c
  ConfigList* configs = nullptr;   // +0x20
  std::vector<Firepoint*>* firepoints = nullptr;  // +0x24
  PartSlot parts[18];              // +0x28  by part id
  MechDfn(int32_t index, zrdr::Node* reader);    // 0x0046b890
  ~MechDfn();                                    // 0x0046b9b0, saves the player's loadouts
  MechDfn(const MechDfn&) = delete;
  MechDfn& operator=(const MechDfn&) = delete;
  template <typename T> T get(int off) const {
    T v;
    std::memcpy(&v, raw + off, sizeof v);
    return v;
  }
  template <typename T> void put(int off, T v) { std::memcpy(raw + off, &v, sizeof v); }
};

// What the head leaves on the stack for the loadout block.
struct HeadValues {
  float half_turn_rate = 0;        // +0x44  max_turn_rate / 2
  int32_t tech = 0;                // +0x54  the mech's
  float accel_rate = 0;            // +0x58
  float decel_rate = 0;            // +0x5c
  float turn_accel_factor = 0;     // +0x60
};

// Blocks of 0x0046ba40, in its order.
bool read_head(MechDfn& dfn, HeadValues* out);   // 0x0046ba95..0x0046be07, false on a bad version
void read_firepoints(MechDfn& dfn);   // 0x0046bfd8..0x0046c13a
void read_part_slots(MechDfn& dfn);   // 0x0046c13a..0x0046c1f0
bool read_configurations(MechDfn& dfn, const HeadValues& head);  // 0x0046c1f0..0x0046cb3c, false without any

bool read_dfn(MechDfn& dfn);                 // 0x0046ba40, the blocks in order
MechDfn* load_dfn(int32_t mech);             // 0x0046b7a0, 0 when missing or bad
MechDfn* mech_dfn(int32_t mech);             // 0x004846a0, loaded once per mech
MechDfn*& mech_dfn_slot(int32_t mech);       // an owned cache entry (0 <= mech < chassis_count), for tests
void free_dfns();                            // 0x004844b0's loop: each deleted (0x0046b9b0)

// 0x00424b70 and 0x00424f60, the player's saved loadouts (docs/mechdef-head.md):
// loaded after the file's own loadouts, written by the dtor. A test can
// replace them (nullptr skips the call).
using LoadSaved = void (*)(ConfigList* configs);
void set_load_saved(LoadSaved fn);
using SaveAll = void (*)(ConfigList* configs);
void set_save_all(SaveAll fn);

}  // namespace mech3
