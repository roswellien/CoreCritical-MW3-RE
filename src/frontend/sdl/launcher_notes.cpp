#include "frontend/sdl/launcher_notes.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/data_version.h"

#include <cstdio>
#include <filesystem>

namespace sdlfe {

std::vector<std::string> launcher_notes(const std::string& version, bool music, bool movies, const std::string& music_dir) {
  std::vector<std::string> out;
  // 1.0's data and a mix need no note: they load as 1.2 (docs/patch12.md).
  if (version == "unknown")
    out.push_back(mech3::mod_note());
  const std::string rip = "or put your own rip of the music in " + music_dir + " as track02.ogg, track03.ogg and so on.";
  if (!movies && !music)
    out.push_back("Your install has no movies (they stayed on the CD) and no music (the original played it from the "
                  "CD's audio tracks). ADD FROM MY DISC takes them from your disc in a drive, a disc image or the "
                  "disc's folder (the music only from a .cue image and its .bin), " + rip);
  else if (!movies)
    out.push_back("Your install has no movies: they stayed on the CD. ADD FROM MY DISC takes them from your disc in a "
                  "drive, a disc image (.cue or .iso) or the disc's folder.");
  else if (!music)
    out.push_back("No music yet: the original played it from the CD's audio tracks, which an install does not have. "
                  "ADD FROM MY DISC takes it from your disc image (a .cue and its .bin), " + rip);
  return out;
}

bool have_movies(const std::string& install, const std::string& write_dir) {
  std::error_code ec;
  for (const std::string& d : {install, write_dir}) {
    if (d.empty()) continue;
    for (const auto& e : std::filesystem::directory_iterator(zutl::any_case(d, "video"), ec)) {
      std::string x = e.path().extension().string();
      for (char& c : x) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
      if (x == ".avi" || x == ".fmv") return true;
    }
  }
  return false;
}

bool have_music(const std::vector<std::string>& dirs) {
  std::error_code ec;
  for (const std::string& d : dirs)
    for (int t = 2; t <= 20; ++t)
      for (const char* ext : {"ogg", "flac", "wav", "mp3"}) {
        char name[32];
        std::snprintf(name, sizeof name, "track%02d.%s", t, ext);
        if (!d.empty() && std::filesystem::is_regular_file(std::filesystem::path(d) / name, ec)) return true;
      }
  return false;
}

}  // namespace sdlfe
