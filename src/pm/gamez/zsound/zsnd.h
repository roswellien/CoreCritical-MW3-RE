#pragma once
#include <cstdint>

#include "gamez/zsound/zsnd.h"

// C:\zipper\branchep1\gamez\zSound\: what Pirate's Moon changed in zSound
// (docs/pm-map.md step 7, docs/sound.md, Pirate's Moon), through its
// zgame::Table. The rest of zSound is MW3's (src/gamez/zsound), on the same
// State and the same sets, sounds and groups.
namespace pm::zsnd {

using ::zsnd::FreeFn;
using ::zsnd::LoadFn;
using ::zsnd::Set;
using ::zsnd::Sound;

// PM's image's zSound data where it is not MW3's (State's defaults): the
// speed of sound, 345 m/s (0x006135cc; MW3 343), which no sounds.zrd sets,
// and 4 duplicate voices a sound (0x00613188; MW3 7). Once at start, after
// zgame::set_game, as the exe has them from its image.
void statics();

// zsnd_init.cpp
// No SYNTAX and no demo sets: the demo flag is not PM's and is ignored.
int32_t load(uint32_t window, const char* name, LoadFn fn, int32_t demo);  // Pirate's Moon 1.0 @ 0x00591840
int32_t read_sounds(zrdr::Node* root, LoadFn fn);   // Pirate's Moon 1.0 @ 0x00591900
Set* read_set(zrdr::Node* name_node, LoadFn fn);    // Pirate's Moon 1.0 @ 0x005920e0
Sound* read_sound(Set* set, zrdr::Node* node, LoadFn fn);  // Pirate's Moon 1.0 @ 0x00592170
int32_t shutdown();                                 // Pirate's Moon 1.0 @ 0x00591700
void set_range_scale(float scale);                  // Pirate's Moon 1.0 @ 0x00591830

// zsnd_parm.cpp: the sets.
Set* find_set(const char* name);    // Pirate's Moon 1.0 @ 0x00590a20, any case
Set* make_set(const char* name);    // Pirate's Moon 1.0 @ 0x00590ae0, the set of that name, made if new
int32_t load_set(Set* set);         // Pirate's Moon 1.0 @ 0x00590d70
int32_t unload_set(Set* set);       // Pirate's Moon 1.0 @ 0x00590f80
int32_t unload_all();               // Pirate's Moon 1.0 @ 0x00590950
void free_sets(FreeFn fn);          // Pirate's Moon 1.0 @ 0x00590990

// zsnd_play.cpp
// Pause's records: only the voices that were playing, each from where it
// stopped (MW3 replays every voice of each sound recorded: its FIX (bug)).
int32_t resume_snapshot(::zsnd::Snapshot* s);  // Pirate's Moon 1.0 @ 0x00590530
void queue_push(Sound* s, const float* pos, float volume, const float* vel, uint32_t start);  // Pirate's Moon 1.0 @ 0x0058fb60

// zsnd_cd.cpp
void cd_notify(int status);         // Pirate's Moon 1.0 @ 0x005938c0

}  // namespace pm::zsnd
