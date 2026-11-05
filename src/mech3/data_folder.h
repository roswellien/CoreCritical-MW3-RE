#pragma once
// Ours, not original code (docs/program.md, The data folder): the player's
// folder (settings, pilots, saves, converted movies) elsewhere than the usual
// place, on another drive say. A pointer file in the usual place names it, and a
// marker in the folder says it is ours. Moving copies, verifies, switches the
// pointer, and only then may remove the old copy, file by file: nothing here
// removes a folder's contents it did not itself copy or make.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mech3::data_folder {

constexpr const char* kPointer = "data-folder.txt";    // in the usual place
constexpr const char* kMarker = "mw3-data-folder.txt";  // in a folder the pointer may name
constexpr const char* kCopying = "mw3-data-copying.txt";  // in a copy not finished yet

// The folder to use. No pointer: `usual`, as if none of this existed. A pointer
// whose folder cannot be used (drive not connected, read-only, full): `error`
// says so and `dir` is that folder, never a fresh one in its place.
struct Resolved {
  std::string dir;
  bool pointer = false;
  std::string error;
};
Resolved resolve(const std::string& usual);

// "" if the folder can be used: it is there, it has the marker, a small write works.
std::string unusable(const std::string& dir);

// Where a picked folder means to move to: itself if it is empty or holds no
// data of ours, else a "MechWarrior 3 Data" folder inside it (a drive's root).
std::string destination(const std::string& picked, const std::string& usual);

// A move's check before starting: `error` if it cannot be done.
struct Plan {
  uint64_t bytes = 0, free = 0;
  int files = 0;
  std::string error;
};
Plan plan(const std::string& from, const std::string& to, const std::string& usual);

struct Moved {
  bool ok = false, cancelled = false;
  std::string error;
  std::vector<std::string> copied;  // relative: what remove_old may remove in `from`
  uint64_t bytes = 0;
};
// The copy, each file read back and compared; mw3.cfg's paths under `from` now
// under `to`; the pointer switched last. On failure or cancel what this made
// is taken away again and the pointer is as it was. The log files stay behind:
// they are in use, and each start makes new ones.
Moved move_data(const std::string& from, const std::string& to, const std::string& usual,
                const std::function<bool(double, const std::string&)>& progress);

struct Removed {
  int files = 0;
  std::vector<std::string> failed;
  std::string error;  // nothing was removed
};
// The player's yes after a move: the files move_data copied, only when the
// pointer names `to` and `to` still has every one. Folders only when empty.
Removed remove_old(const std::string& from, const std::string& to, const std::vector<std::string>& copied,
                   const std::string& usual);

// Only plain ASCII in the path: what plan asks of a destination on Windows.
bool plain_name(const std::string& path);

// Tests: a drive's free bytes, when set; a call right after the pointer switches.
const uint64_t*& free_for_test();
std::function<void()>& after_switch_for_test();

// `path` under `from` as the same place under `to`; else as it was.
std::string moved_path(const std::string& path, const std::string& from, const std::string& to);

}  // namespace mech3::data_folder
