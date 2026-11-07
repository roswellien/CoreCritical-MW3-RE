#pragma once
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Where game files come from (docs/assets-layer.md). The original looks files
// up along a search path (0x005762b0 find, 0x005763b0 fopen; ".;zbd" for the
// interpreter) relative to the install. FIX (A): the rebuild generalises that
// into an ordered list of mounted folders, so a set of replacement files can
// sit in front of the original install, or stand in for it entirely.
//
// Paths are the game's own, Windows style and relative ("zbd\\c1\\gamez.zbd");
// the write dir (what the game saved) is searched first, then the mounts in
// order; the first holding the file wins. An absolute or ./-relative host path
// that exists is used as it is (tools and tests pass those).
namespace zutl {
// App-owned immutable aliases for validated optional texture banks only.
using BankOverrides = std::map<std::string, std::string>;
std::shared_ptr<const BankOverrides>
set_bank_overrides(std::shared_ptr<const BankOverrides> banks);

struct Mounts {
  std::vector<std::string> read;   // searched in order
  std::string write;               // where the game writes (configs\, saves); "" = the first read mount
  // Ours (docs/modding.md): the read mounts that are mods (and the world
  // folders under them). A mod serves game data only, never what belongs
  // to the player (mod_may_serve).
  std::vector<std::string> mods;
  // Ours: the game program's names come from data, mods and other players,
  // so once it is set up a name that is absolute, holds a ':' or has a ".."
  // part finds and writes nothing (docs/assets-layer.md). Tools and tests
  // leave it off and pass host paths.
  bool confined = false;
};
Mounts& mounts();

void mount(const std::string& dir);          // appended: searched after the ones before
void mount_front(const std::string& dir);    // searched first (an override folder)
void mount_mod(const std::string& dir);      // appended, as a mod (Mounts::mods)
bool is_mod_dir(const std::string& dir);     // a mod's root or a folder under one
// Whether a mod may serve the game path (host form): not the player's own
// files (pilots, saved loadouts, keys, mw3.cfg, the settings, the logs).
bool mod_may_serve(const std::string& host_path);
// The first mod's copy of a game path (host form, from a mod's root), "" if none.
std::string find_in_mods(const std::string& host_path);
void set_write_dir(const std::string& dir);
// FIX (A): the original's names meet Windows, where case does not matter; an
// install on a case-sensitive disk may be in any case (a tool's lower case, a
// disc's upper). `rel` ('/' between its parts) under `root`, a file or folder,
// each part's own name if there is one, else one differing only by case; the
// host path, "" if none. docs/assets-layer.md.
std::string any_case(const std::string& root, const std::string& rel);

// '\\' -> '/'; FIX (A): a game path's characters are the game's code page
// (Windows-1252: what the edit bars type, a pilot named Müller), the host's
// UTF-8, so a relative path is converted (an absolute one is the host's).
std::string host(const char* path);
// UTF-8 text in the game's code page (Windows-1252); false for a character it lacks.
bool game_text(const std::string& utf8, std::string* out);
std::string find(const char* path);          // the host path in the first mount that has it, "" if none
// Ours: every mount's copy of a game path, in the order find() looks (the
// write dir, then the mounts; a mod's only where it may serve).
std::vector<std::string> find_all(const char* path);
bool exists(const char* path);
std::FILE* open(const char* path, const char* mode);  // reads search the mounts; writes go to the write dir
std::string write_path(const char* path);    // the host path a write of `path` goes to
// FIX (A) for FindFirstFileA / FindNextFileA: the files (not folders) a game
// pattern ("keys\\*.zrd"; * and ? in the last part, any case) matches in the
// write dir and every mount, each name once (the first found), sorted as NTFS
// lists a folder (by the upper-cased name). The names are in the game's code
// page; a file whose name has a character it lacks is not listed.
std::vector<std::string> list(const char* pattern);

}  // namespace zutl
