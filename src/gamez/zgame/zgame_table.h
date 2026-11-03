#pragma once

#include <cstdint>
#include <cstdio>
#include <list>

namespace zutl_zar {
struct Archive;
}
namespace zrdr {
struct Node;
}
namespace zcls {
struct Node;
}
namespace zsnd {
struct Object;
struct Record;
struct Set;
struct Sound;
}

// Ours, not the exe's: which game the shared engine runs (docs/games-layout.md).
// An engine function Pirate's Moon changed is picked through the running
// game's table at run time, never by #ifdef in shared code. MW3's table is the
// default; mw3pm sets PM's (src/pm/pm_game.cpp) before anything loads. Each
// entry arrives with its port (docs/pm-map.md, A porting order); a game's
// table names only functions, no data.
namespace zgame {

struct Table {
  const char* name;  // for the start-up print: "MechWarrior 3", "Pirate's Moon"
  const char* exe;   // the exe its address comments name: "Mech3.exe 1.2", "Pirate's Moon 1.0"
  // zArchive (zutl_zar.h): MW3's version 1, PM's version 2 with its checksum.
  bool (*zar_read_table)(zutl_zar::Archive& a);   // 0x005767b0; PM 0x00597610
  bool (*zar_write_table)(zutl_zar::Archive& a);  // 0x00576720; PM 0x00597570
  // Ours: how many bytes entry `index`'s data may take, for the bounded
  // readers (MW3: its length; PM: up to the next entry, its motion.zbd's
  // lengths being 1).
  uint32_t (*zar_span)(const zutl_zar::Archive& a, int index);
  // zReader's loose text files (zreader.h): PM's adds #include.
  zrdr::Node* (*zrdr_parse_text)(std::FILE* file);  // 0x005563d0; PM 0x005782f0
  // A world file, gamez.zbd (zcls/cls_zbd.h): PM's stacks onto what is loaded.
  int (*zcls_read_zbd)(const char* path);  // 0x004ff520; PM 0x00516bc0
  // A new node, light and the active switch (zcls/cls_node.h, light.h):
  // PM's node points at its box, its light data is 0x100 bytes.
  zcls::Node* (*zcls_node_alloc)();                      // 0x004eacd0; PM 0x004fc550
  int (*zcls_node_set_active)(zcls::Node* node, int on);  // 0x004eb170; PM 0x004fca50
  zcls::Node* (*zcls_light_create)();                    // 0x004fa720; PM 0x0050e470
  // A mission's anim.zbd (zeff/zeff_anim_init.h): PM's is version 50.
  int (*zeff_anim_read_zbd)();  // 0x0050a0e0; PM 0x00523140
  // zsound (pm-map step 7a; zsnd.h, zsnd_cd.h): PM's reads sounds.zrd
  // anew (sets made by name, INCLUDE_SETS, COMPILE_FORMAT, QPRIORITY), counts
  // a set's loads, finds sets in any case, queues lines oldest first and
  // loops the whole disc in CD mode 3.
  int32_t (*zsnd_load)(uint32_t window, const char* name, void* (*fn)(zsnd::Object*, zrdr::Node*),
                       int32_t demo);                              // 0x005705e0; PM 0x00591840
  int32_t (*zsnd_shutdown)();                                      // 0x00570510; PM 0x00591700
  int32_t (*zsnd_load_set)(zsnd::Set* set);                        // 0x0056fbf0; PM 0x00590d70
  int32_t (*zsnd_unload_set)(zsnd::Set* set);                      // 0x0056fe00; PM 0x00590f80
  zsnd::Set* (*zsnd_find_set)(const char* name);                   // 0x0056f680; PM 0x00590a20
  int32_t (*zsnd_unload_all)();                                    // 0x0056f540; PM 0x00590950
  void (*zsnd_free_sets)(void (*fn)(zsnd::Object*, void*));        // 0x0056f5c0; PM 0x00590990
  void (*zsnd_queue_push)(zsnd::Sound* s, const float* pos, float volume, const float* vel,
                          uint32_t start);                         // 0x0056e750; PM 0x0058fb60
  void (*zsnd_cd_notify)(int status);                              // 0x00572bf0; PM 0x005938c0
  int32_t (*zsnd_resume_snapshot)(std::list<zsnd::Record>* s);     // 0x0056f130; PM 0x00590530
};

const Table& mw3_table();
const Table& game();          // the running game's
void set_game(const Table& t);  // once, at start

}  // namespace zgame
