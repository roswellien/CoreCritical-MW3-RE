#pragma once
#include <cstdint>
#include <deque>
#include <list>
#include <string>
#include <vector>

#include "platform/sound_output.h"

namespace zrdr { struct Node; }
namespace zutl_zar { struct Archive; }

// C:\zipper\GameZMech3\zSound\: the sound effects, voices and their 3D
// placement (docs/sound.md). The original's source files are zsnd_play.cpp,
// zsnd_parm.cpp, zsnd_init.cpp, zsnd_3d.cpp, zsnd_grp.cpp, zsnd_create.cpp and
// zsnd_cd.cpp (the CD music, its own header). Everything plays through
// DirectSound, here platform::SoundOutput (FIX (A)).
//
// The data comes from sounds.zrd: SETS of sounds, each a WAV in soundsH.zbd /
// soundsL.zbd (or a loose file on SOUND_PATH), loaded and unloaded by set, and
// SOUND_GROUPS that play sounds in sequences and by weighted chance.
//
// Handles are the original's: a sound or group (find) and a voice (play). The
// first field tells sounds (0) from groups (1); a voice's second field tells a
// sound's voice (0) from a playing group (1). The original's A3D path
// ([0x0072c2cc] == 1, Aureal's 3D hardware) is never selected and not ported.
namespace zsnd {

using platform::SoundBuffer;

struct Sound;
struct Group;

// A sound or a group, what find (0x0056f6f0) returns.
struct Object {
  int32_t kind = 0;            // +0x00  0 Sound, 1 Group
};

// A playing sound or group, what play returns.
struct VoiceBase {
  uint8_t locked = 0;          // +0x00  0x0056f3f0: kept by its owner, not reused
  int32_t kind = 0;            // +0x04  0 Voice, 1 GroupVoice
};

// One buffer of a sound (0x48 bytes): the sound's own (+0x4c) or a duplicate.
struct Voice : VoiceBase {
  SoundBuffer buffer = 0;      // +0x08
  float pos[3] = {};           // +0x0c  last 3D position
  float vel[3] = {};           // +0x18
  int32_t volume = 0;          // +0x24  hundredths of a dB, before 3D falloff
  int32_t has_pos = 0;         // +0x28
  Sound* sound = nullptr;      // +0x2c
};

// One of a sound's PLAYBACK formats (HIGH, MED, LOW): its own file and format,
// read but never used for playing.
struct Format {
  std::string file;            // +0x00  _strdup, empty = none
  int32_t rate = 0, bits = 0, channels = 0;  // +0x04
  bool has_file = false;
};

// Sound flags (+0x0c).
enum : uint32_t {
  kLooped = 0x1, kPurgeable = 0x2, k3D = 0x4, kLoaded = 0x8, kVoice = 0x10, kFrequency = 0x20,
  kHardware = 0x40, kCreated = 0x80, kStream = 0x100, kQueue = 0x200,
  kQPriority = 0x1c00,  // PM only: QPRIORITY 0..7 (bits 10-12), the queue's order
};

using CueFn = void (*)(uint32_t index, Sound* sound);  // ECX index, EDX sound

// One sound of a set (0xd0 bytes).
struct Sound : Object {
  const char* file = nullptr;  // +0x04  WAV name, the reader's string
  const char* name = nullptr;  // +0x08
  uint32_t flags = 0;          // +0x0c
  void* subtitle = nullptr;    // +0x10  from the 0x0072c4cc hook (never set in MW3)
  float queue_life = 5.0f;     // +0x14  QUEUE: seconds a queued line may wait
  float volume = 1.0f;         // +0x18  VOLUME, 0..1
  float range_min = 50.0f;     // +0x1c  RANGE: full volume up to here
  float range_max = 400.0f;    // +0x20  silent beyond
  float freq_max = 90000.0f;   // +0x24  set_frequency's range
  float freq_min = 20000.0f;   // +0x28
  float a3d_rate = 0.0f;       // +0x2c  A3D only
  float a3d_dist = 1.0f;       // +0x30  A3DDIST, A3D only
  CueFn cue_fn = nullptr;      // +0x34  0x0056f3e0
  float cue_offset = 0.0f;     // +0x38  always 0
  uint32_t cue_count = 0;      // +0x3c  the WAV's cue points + 1 (its end)
  std::vector<float> cue_times;     // +0x40  seconds into the sound
  std::vector<float> cue_at;        // +0x44  game time of each, set by play
  std::vector<uint32_t> cue_bytes;  // +0x48  byte offset of each (8-byte slots)
  Voice voice;                 // +0x4c
  std::vector<Voice*> dups;    // +0x94 count, +0x98  duplicates, at most State::max_duplicates
  Format formats[3];           // +0x9c  HIGH, MED, LOW
  void* user = nullptr;        // +0xcc  the load callback's result
};

// A named set of sounds (0x18 bytes; PM's 0x20, its sounds and subsets in
// std::lists), loaded and unloaded together. A deque: PM's reader adds sounds
// to a set already made, and their addresses must stay (docs/sound.md, PM).
struct Set {
  std::string name;            // +0x00  (PM +0x0c)
  std::deque<Sound> sounds;    // +0x04 count, +0x08  (PM +0x04 list, +0x08 count)
  int32_t loaded = 0;          // +0x0c  MW3 0 or 1; PM a count of loads (+0x10)
  std::vector<Set*> subsets;   // +0x10 count, +0x14  SUB_SET, null when not found (PM +0x18 list, +0x1c)
};

// One step of a group (0x1c bytes): a sound, possibly followed by more (+0x18).
struct GroupEntry {
  int16_t initial = 0;         // +0x00  PLAY_COUNT, -1 unlimited
  int16_t count = 0;           // +0x02  plays left; 0 = used up
  float delay = 0.0f;          // +0x04  DELAY_PLAY
  float weight = 0.0f;         // +0x08  WEIGHT
  int32_t solo = 0;            // +0x0c  PLAY_SOLO: wait for it to end
  const char* name = nullptr;  // +0x10  the sound, "NULL" for a pause
  Object* sound = nullptr;     // +0x14  found on first play
  GroupEntry* next = nullptr;  // +0x18  calloc'd, owned
};

// A SOUND_GROUPS entry (0xd0 bytes).
struct Group : Object {
  const char* name = nullptr;  // +0x04
  int32_t dynamic = 0;         // +0x08  DYNAMIC_WEIGHTS
  int32_t solo = 0;            // +0x0c  PLAY_SOLO: one at a time
  float factor = 0.0f;         // +0x10  the weight kept by a pick
  int16_t repeat = 0;          // +0x14  REPEAT, -1 forever
  float delay_repeat = 0.0f;   // +0x18
  float delay_term = 0.0f;     // +0x1c
  std::vector<GroupEntry> entries;  // +0x20 count, +0x24
  void* extra = nullptr;       // +0x28  EXTRA, the load callback's result
};

// A playing group (0x48 bytes).
struct GroupVoice : VoiceBase {
  int32_t has_pos = 0;         // +0x08
  float pos[3] = {};           // +0x0c
  float vel[3] = {};           // +0x18
  float volume = 1.0f;         // +0x24
  float timer = 0.0f;          // +0x28
  int32_t repeats = 0;         // +0x2c
  GroupEntry* entry = nullptr; // +0x30
  int32_t wait = 0;            // +0x34  the entry was PLAY_SOLO
  std::vector<VoiceBase*> voices;  // +0x38 count, +0x3c
  int32_t state = 0;           // +0x40  0 start, 1 playing, 2 repeat delay, 3 end delay, 4 done
  Group* group = nullptr;      // +0x44
};

// A line waiting in the voice queue (0x30 bytes).
struct Queued {
  Sound* sound = nullptr;      // +0x00
  float volume = 0.0f;         // +0x04
  int32_t has_pos = 0, has_vel = 0;  // +0x08, +0x0c
  float pos[3] = {}, vel[3] = {};    // +0x10, +0x1c
  uint32_t start = 0;          // +0x28  start position, always 0
  float expires = 0.0f;        // +0x2c  queue clock past which it is dropped
};

// A volume fade (0x10 bytes).
struct Fade {
  float target = 0.0f;         // +0x00  hundredths of a dB
  float current = 0.0f;        // +0x04
  Voice* voice = nullptr;      // +0x08
  int32_t stop = 0;            // +0x0c  stop the voice when reached
};

// What pause keeps (0x0056eb10): a voice that was playing, 0x28 bytes. The
// first record holds only the master volume.
struct Record {
  Voice* voice = nullptr;      // +0x00
  Sound* sound = nullptr;      // +0x04
  int32_t volume = 0;          // +0x08  the sound's own voice's volume (the master volume in the first)
  uint32_t flags = 0;          // +0x0c  1: has a position
  float pos[3] = {}, vel[3] = {};  // +0x10, +0x1c
  float master = 0.0f;         // the first record's +0x08 as a float
};
using Snapshot = std::list<Record>;

using LoadFn = void* (*)(Object* obj, zrdr::Node* node);  // ECX object, EDX its data
using FreeFn = void (*)(Object* obj, void* user);         // ECX object, EDX what LoadFn made


struct State {
  int32_t initialized = 0;     // 0x0072c2c0
  int32_t open = 0;            // 0x0072c2c4  device open, sounds.zrd read
  uint32_t window = 0;         // 0x0072c2c8
  int32_t api = 0;             // 0x0072c2cc  0 DirectSound; 1 A3D, never selected
  SoundBuffer primary = 0;     // 0x0072c2d4
  bool has_primary_format = false;  // 0x0072c2d8, malloc'd
  platform::WaveFormat primary_format;
  bool has_playback[3] = {};   // 0x0072c2dc HIGH, 0x0072c2e0 MED, 0x0072c2e4 LOW
  Format playback[3];
  int32_t playback_current = -1;  // 0x0072c2e8, an index into playback
  uint32_t caps = 0;           // 0x0072c2f0  DSCAPS dwFlags (DSCAPS at 0x0072c2ec)
  zrdr::Node* root = nullptr;  // 0x0072c440  sounds.zrd
  std::string search;          // 0x0072c43c  SOUND_PATH
  bool has_search = false;
  int32_t listener_set = 0;    // 0x0072c444
  float listener[12] = {};     // 0x0072c448  rotation rows, then the position at +0x24
  float listener_vel[3] = {};  // 0x0072c478
  int32_t lod_default = 0;     // 0x0072c484
  int32_t* lod = &lod_default; // 0x0072c488  -> SoundLOD: 0 soundsH, 1 soundsM, 2 soundsL
  int32_t mute_default = 0;    // 0x0072c48c
  int32_t* mute = &mute_default;  // 0x0072c490  -> MuteSound
  int32_t muted = 0;           // 0x0072c494  *mute when initialised
  float volume_default = 1.0f; // 0x0072c49c
  // 0x0072c4a0 -> SoundVolume, the master volume; null: volume_default (the
  // exe's static start value). Not a pointer into this State: a copy of one
  // (state() = State{}) would keep pointing at the copied-from object.
  float* volume = nullptr;
  float& master() { return volume ? *volume : volume_default; }
  float master() const { return volume ? *volume : volume_default; }
  int32_t voices_on = 1;       // 0x0072c4a4  voice-flagged sounds play
  Sound* cue_sound = nullptr;  // 0x0072c4ac  the sound whose cues are followed
  Voice* cue_voice = nullptr;  // 0x0072c4b0
  uint32_t cue_index = 0;      // 0x0072c4b4
  uint32_t cue_limit = 999;    // 0x0072c4b8
  float queue_clock = 0.0f;    // 0x0072c4bc
  std::list<Queued*>* queue = nullptr;  // 0x0072c4c0  MW3 newest first; PM oldest first, by QPRIORITY
  Voice* queued = nullptr;     // 0x0072c4c4  the queued line playing
  float queue_next = 0.0f;     // 0x0072c4c8  no queued line before this
  void* (*subtitle_hook)(zrdr::Node* node) = nullptr;  // 0x0072c4cc, never set
  int32_t (*can_play_hook)(void* subtitle) = nullptr;  // 0x0072c4d0, never set
  std::list<Fade*> fades;      // 0x0072c4dc
  std::list<Fade*> free_fades; // 0x0072c4ec
  int32_t groups_ready = 0;    // 0x0072c504
  std::list<Group*>* groups = nullptr;        // 0x0072c508  newest first
  std::list<GroupVoice*>* playing = nullptr;  // 0x0072c50c
  std::list<GroupVoice*>* spare = nullptr;    // 0x0072c510
  GroupVoice* finished = nullptr;  // 0x0072c514
  int32_t finished_count = 0;  // 0x0072c518
  std::vector<Set*> sets;      // 0x0072c2ac
  int32_t archives_on = 1;     // 0x005efc2c  0x0056f4b0
  int32_t max_duplicates = 7;  // [0x005efba8]: 8 voices per sound (PM's [0x00613188]: 4, 5 voices)
  float speed_of_sound = 343.0f;      // 0x005efd98
  float inv_speed_of_sound = 1.0f / 343.0f;  // 0x005efd9c
  // PM only (docs/sound.md, Pirate's Moon):
  float range_scale = 1.0f;    // 0x00613488  RANGE times this; 0.5 at the lowest object detail
  int32_t default_qpriority = 0;  // 0x008c1af0  DEFAULT_QUEUE_PRIORITY
  // 0x0061324c  COMPILE_FORMAT: each sound's HIGH, MED, LOW to start with;
  // the image's values are MW3's built-in ones (0x00570af0).
  Format compile[3] = {{"", 44100, 16, 2}, {"", 22050, 16, 1}, {"", 11025, 8, 1}};
};

State& state();

// What the original found in Windows and in modules not ported; the front end
// (or a test) supplies these.
struct Hooks {
  platform::SoundOutput* output = nullptr;
  // 0x00597d10: the engine's named variables (SoundLOD, MuteSound, SoundVolume);
  // null when there is none.
  void* (*find_var)(const char* name) = nullptr;
  // 0x00572450's CD part with the CD_TRACKS node (zsnd_cd, ported); null skips.
  void (*cd_open)(zrdr::Node* cd_tracks) = nullptr;
  void (*cd_close)() = nullptr;  // 0x005728f0
};
Hooks& hooks();

// zsnd_init.cpp
int32_t init(uint32_t window);                       // 0x005700f0
int32_t quality(int32_t lod);                        // 0x00570280
int32_t set_primary_format(const int32_t* quality);  // 0x005702a0
void stop_queue(int32_t stop_playing);               // 0x00570410
void drop_queued(Sound* sound);                      // 0x00570490
// shutdown, load: the running game's (zgame::Table); mw3_ is MW3's.
int32_t shutdown();
int32_t mw3_shutdown();                              // 0x00570510
int32_t load(uint32_t window, const char* name, LoadFn fn, int32_t demo);  // PM: no demo
int32_t mw3_load(uint32_t window, const char* name, LoadFn fn, int32_t demo);  // 0x005705e0
int32_t read_syntax2(zrdr::Node* root, LoadFn fn, int32_t demo);  // 0x00570af0
int32_t open_device();                               // 0x00571460

// zsnd_3d.cpp (with the device shutdown and the fades before it)
int32_t close_device(FreeFn fn);                     // 0x005715d0
void reset();                                        // 0x005716a0
void fades_update(float dt);                         // 0x00571a90
void fades_shutdown();                               // 0x00571b90
VoiceBase* play_fading(Object* obj, const float* pos, float volume, const float* vel, float fade_to,
                       int32_t stop);                // 0x00571cc0
VoiceBase* fade(VoiceBase* voice, int32_t stop, float level);  // 0x00571d20
int32_t set_listener(const float* matrix, const float* vel);  // 0x00571e50
int32_t update_3d(Voice* voice, const float* pos, const float* vel, int32_t doppler);  // 0x00571f30
float speed_of_sound();                              // 0x00572360
void set_speed_of_sound(float v);                    // 0x00572370

// zsnd_play.cpp
void frame(int32_t a3d_flush, int32_t advance);      // 0x0056dfb0
// A QUEUE line waits (ECX sound, EDX position, volume, velocity, start): the
// running game's; MW3's puts it first.
void queue_push(Sound* s, const float* pos, float volume, const float* vel, uint32_t start);
void mw3_queue_push(Sound* s, const float* pos, float volume, const float* vel, uint32_t start);  // 0x0056e750
VoiceBase* play(Object* obj, float volume);          // 0x0056e3d0
int32_t volume_db(float linear);                     // 0x0056e410
VoiceBase* play_at(Object* obj, const float* pos, float volume, const float* vel);  // 0x0056e7e0
int32_t status(VoiceBase* voice);                    // 0x0056e8e0  1 playing, 0 not, -1 no voice
int32_t stop(VoiceBase* voice);                      // 0x0056e9f0
Snapshot* snapshot();                                // 0x0056eb10
int32_t stop_snapshot(Snapshot* s);                  // 0x0056f0a0
int32_t resume_snapshot(Snapshot* s);                // the running game's (zgame::Table)
int32_t mw3_resume_snapshot(Snapshot* s);            // 0x0056f130
// 0x0056f010 (ECX sound + 4, EDX voice, restart, the volume's change in dB/100).
void resume_voice(Sound* s, Voice* v, int32_t restart, int32_t delta);
int32_t free_snapshot(Snapshot* s);                  // 0x0056f190
bool muted();                                        // 0x0056f210
float set_volume(float v);                           // 0x0056f260  the master volume

// zsnd_parm.cpp
int32_t set_frequency(VoiceBase* voice, float t);    // 0x0056f280
int32_t set_voice_volume(VoiceBase* voice, float v); // 0x0056f370
void set_cue_fn(Object* obj, CueFn fn);              // 0x0056f3e0
int32_t lock(VoiceBase* voice);                      // 0x0056f3f0
int32_t unlock(VoiceBase* voice);                    // 0x0056f410
void* user_data(Object* obj);                        // 0x0056f430
void set_archives(int32_t on);                       // 0x0056f4b0
// The sets: unload_all, free_sets, find_set, load_set and unload_set (of a
// Set) are the running game's (zgame::Table); mw3_ is MW3's.
void load_set(const char* name);                     // 0x0056f520
void unload_set(const char* name);                   // 0x0056f530
int32_t unload_all();
int32_t mw3_unload_all();                            // 0x0056f540
void free_sets(FreeFn fn);
void mw3_free_sets(FreeFn fn);                       // 0x0056f5c0
Set* set_at(int32_t i);                              // 0x0056f630
int32_t set_count();                                 // 0x0056f660
Set* find_set(const char* name);
Set* mw3_find_set(const char* name);                 // 0x0056f680
Object* find(const char* name);                      // 0x0056f6f0
const char* name_of(Object* obj);                    // 0x0056f740
int32_t load_set(Set* set);
int32_t mw3_load_set(Set* set);                      // 0x0056fbf0
int32_t unload_set(Set* set);
int32_t mw3_unload_set(Set* set);                    // 0x0056fe00
Sound* set_sound(Set* set, int32_t i);               // 0x0056fe70
Sound* find_in_set(Set* set, const char* name);      // 0x0056fea0
// 0x005762b0 on SOUND_PATH: the file in the first directory that has it, "" for none.
std::string path_find(const char* name);
int32_t load_from_archive(Set* set, zutl_zar::Archive& a);  // 0x0056ffc0

// zsnd_grp.cpp
void groups_reset();                                 // 0x00573270
std::list<GroupVoice*>* take_playing();              // 0x005732f0, for pause
int32_t restore_playing(std::list<GroupVoice*>* l);  // 0x00573300
int32_t free_playing(std::list<GroupVoice*>* l);     // 0x00573350
Group* find_group(const char* name);                 // 0x005733b0
int32_t read_groups(zrdr::Node* node, zrdr::Node* demo, LoadFn fn);  // 0x00573420
Group* read_group(zrdr::Node* node, LoadFn fn);      // 0x00573560
void groups_update();                                // 0x005742c0 (0x00574270)
int32_t free_groups(FreeFn fn);                      // 0x005742d0
int32_t group_status(GroupVoice* g);                 // 0x00574610
VoiceBase* play_group(Group* g, int32_t has_pos, float volume, const float* pos, const float* vel);  // 0x00574470

// zsnd_create.cpp
struct Wave;
// 0x00574af0: names a failed HRESULT in an error report; 1 for DS_OK, else 0.
int32_t ds_error(int32_t hr, const char* file, int line);
int32_t create_buffer(Sound* s, const Wave& w);      // 0x00574fd0 -> 0x005752e0
int32_t make_cues(Sound* s, const Wave& w);          // 0x005751e0
int32_t release_sound(Sound* s);                     // 0x00575700
Sound* stream_create(const platform::WaveFormat* format, const void* data, uint32_t bytes);  // 0x005758c0
int32_t stream_lock(Sound* s, uint32_t offset, uint32_t bytes, void** p1, uint32_t* n1, void** p2,
                    uint32_t* n2);               // 0x00575550
int32_t stream_unlock(Sound* s, void* p1, uint32_t n1, void* p2, uint32_t n2);  // 0x00575600
uint32_t stream_position(Sound* s);                  // 0x00575690
void stream_free(Sound* s);                          // 0x00575980

// A WAV in memory (0x24 bytes, 0x00574d40): the file's bytes and where its
// chunks are.
struct Wave {
  int32_t valid = 0;           // +0x00
  std::string path;            // +0x04
  std::vector<uint8_t> bytes;  // +0x08 size, +0x0c
  uint32_t data_size = 0;      // +0x10
  bool has_format = false;     // +0x14  the "fmt " chunk (copied here)
  platform::WaveFormat format;
  uint32_t cue_count = 0;      // +0x18
  size_t cues = 0;             // +0x1c  offset of the "cue " chunk's points (0x18 bytes each), 0 none
  size_t data = 0;             // +0x20  offset of the samples, 0 none
};
int32_t wave_parse(Wave& w);                         // 0x00574db0
int32_t wave_read_file(Wave& w);                     // 0x00574e90
int32_t wave_read_archive(Wave& w, zutl_zar::Archive& a);  // 0x00574f50
// Ours (docs/modding.md, M3): both readers refuse a WAV whose samples or cue
// points run past its bytes, or whose format no output can play (logged).
const char* wave_broken(const Wave& w);

// Ours, not the original's: a sound the archive and the loose files lack,
// made from another sound of the same archive with its end cut (the 1.2
// data layer, mech3/patch12.h: 1.2's renamed sounds from 1.0's own).
struct WaveAlias {
  std::string source;
  float trim_end_seconds = 0;
};
using WaveAliasHook = bool (*)(const char* name, WaveAlias* out);
void set_wave_alias_hook(WaveAliasHook hook);

}  // namespace zsnd
