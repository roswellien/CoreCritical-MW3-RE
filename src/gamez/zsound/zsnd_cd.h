#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace platform { class Files; class MusicOutput; }
namespace zrdr { struct Node; }

// C:\zipper\GameZMech3\zSound\zsnd_cd.cpp: CD audio. The original drives the
// drive through MCI "cdaudio" and addresses music by CD track number. The rebuild
// keeps the track numbers and plays music/trackNN.(ogg|flac|wav|mp3) from the
// player's folder instead, so a ripped disc drops in one file per track. Track 1
// is the data track; MW3 1.2's CD_TRACKS table plays track 2 in the menus and
// track 3 in the campaign.
namespace zsnd_cd {

// Play modes (0x0072c350). 0 stopped, 1 paused; 2 plays one track once; 5 and 6
// play one track and replay it when it ends; anything else plays from the track
// to the end of the disc.
enum Mode : int { Stopped = 0, Paused = 1, Once = 2, Loop = 5, Restart = 6 };

// One CD_TRACKS entry: a cue name with one or more tracks, chosen at random.
struct Cue {
  std::string name;
  std::vector<int> tracks;
};

// The CD_TRACKS half of 0x00572450: the node after "CD_TRACKS" in the reader
// data, a list of ("name" track) or ("name" (track track ...)) items, in order.
// Like the original it trusts the shape: every item must be such a list.
std::vector<Cue> read_cues(const zrdr::Node* cd_tracks);

struct State {
  uint32_t flags = 0;          // 0x0072c358  bit 1: open
  int mode = Stopped;          // 0x0072c350
  int num_tracks = 0;          // 0x0072c36c  highest track on the "disc"
  int last_track = 0;          // 0x0072c378  last track asked for
  int track = 0;               // 0x0072c384  track playing now
  int end_track = 0;           // 0x0072c390  play stops before this track
  bool enabled = true;         // *0x0072c3a0 the music option
  std::vector<Cue> cues;       // 0x0072c4fc
};

State& state();

// What the original found in Windows; the front end supplies these.
void set_backend(platform::Files* files, platform::MusicOutput* output, std::string music_dir);
std::string track_path(int track);  // music/trackNN.<ext> that exists, else ""

bool open(std::vector<Cue> cues);   // 0x00572450
void close();                       // 0x005728f0
bool play_track(int track, int mode);  // 0x00572af0
bool seek(int track);               // 0x00572e40
bool play(int mode);                // 0x00572b40
bool stop();                        // 0x00572c30
bool pause();                       // 0x00572c90
bool resume(int mode);              // 0x00572d70
// MM_MCINOTIFY: the running game's (zgame::Table); PM's loops mode 3 too.
void notify(int status);
void mw3_notify(int status);        // 0x00572bf0
void clear_position();              // 0x005728b0
constexpr int kNotifySuccessful = 1;  // MCI_NOTIFY_SUCCESSFUL
int cue_track(const std::string& name);  // 0x00572a30
void set_volume(uint16_t left, uint16_t right);  // 0x00572f80

// Called by the front end when the current file ends. Stands in for the CD
// playing on to the next track and for MCI's MM_MCINOTIFY at the end of a range.
void on_output_finished();

}  // namespace zsnd_cd
