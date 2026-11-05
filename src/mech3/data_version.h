#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Which MechWarrior 3 data the mounts hold (ours, not original code;
// docs/assets.md, Data versions). The official 1.2 patch replaces or adds 26
// data files, the message table Mech3Msg.dll and force_eff.ifr; each of the
// 28 is matched by size and CRC (zutl_zar::crc32) against 1.0's and 1.2's.
// The exe plays no part: the port reads only zbd\, video\, Mech3Msg.dll and
// force_eff.ifr.
namespace mech3 {

struct DataFile {
  const char* path;          // under the install, '/'-separated
  uint32_t size_10, crc_10;  // 0, 0: not in 1.0 (the patch adds it)
  uint32_t size_12, crc_12;
  const char* sha256_12;     // 1.2's, the patch import's check (patch_import.h)
  const char* sha256_10 = nullptr;  // 1.0's (a disc install's check, disc_install.h); null: not in 1.0
};
const DataFile* data_files(int* count);  // the 28

struct DataVersion {
  // "1.2", "1.0", "1.0 with 1.2 files" (some files patched, some not),
  // "unknown" (a file that is neither: another version, perhaps 1.1, or a
  // mod) or "incomplete" (a file 1.0 already had is missing).
  std::string name;
  int of_10 = 0, of_12 = 0, missing = 0;  // of the 28
  std::vector<std::string> from_12;       // in a mix, the files that are 1.2's
  std::vector<std::string> unknown;       // files that match neither (incomplete: the missing ones)
  std::string describe() const;           // one line for the start-up print
};

DataVersion detect_data_version();  // through zutl's mounts

// One of the 28 by its path: 10 (1.0's), 12 (1.2's), 0 (neither, or not one
// of the 28), -1 (missing).
int data_file_version(const char* path);

// What a player is told when files are neither 1.0's nor 1.2's (Owen, 2026-11-04:
// a mod for the original may well work, so say so; none of this stops the game).
// `files`, when given: the names, in brackets after "another version".
std::string mod_note(const std::string& files = "");

}  // namespace mech3
