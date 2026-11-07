// mw3launcher (ours, not original code; docs/launcher.md, The two programs;
// docs/games-layout.md): the rebuild's main icon. It finds the install, runs
// SETUP and starts the game the player picks as its own program, mw3 beside
// it, hiding while the game runs. mw3 itself starts without it.
//
//   mw3launcher [--write-dir DIR] [--windowed | --window WxH] [--nosound] [--no-import]
//               [--launcher-shot FILE [--launcher-screen main|setup|work|notes]]
//               [--launcher-play] [--launcher-mw1]
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "frontend/sdl/crash_report.h"
#include "frontend/sdl/data_folder_start.h"
#include "frontend/sdl/launcher.h"
#include "frontend/sdl/run_log.h"
#include "frontend/sdl/sdl_child.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;

// This program's own folder (an app bundle's Contents/MacOS, the AppImage's
// usr/bin), where the game is too; SDL_GetBasePath is the bundle's Resources.
fs::path own_dir() {
#if defined(__APPLE__)
  char buf[4096];
  uint32_t n = sizeof buf;
  if (_NSGetExecutablePath(buf, &n) == 0) return fs::weakly_canonical(fs::path(buf)).parent_path();
#elif defined(_WIN32)
  wchar_t buf[4096];
  const DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
  if (n > 0 && n < 4096) return fs::path(std::wstring(buf, n)).parent_path();
#else
  std::error_code ec;
  const fs::path self = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) return self.parent_path();
#endif
  const char* base = SDL_GetBasePath();
  return base ? fs::path(base) : fs::path(".");
}

// The game beside us; MW3_GAME_BIN names another (checks: a stand-in).
std::string game_bin() {
  if (const char* env = std::getenv("MW3_GAME_BIN")) return env;
#if defined(_WIN32)
  const fs::path p = own_dir() / "mw3.exe";
#else
  const fs::path p = own_dir() / "mw3";
#endif
  std::error_code ec;
  return fs::is_regular_file(p, ec) ? p.string() : std::string();
}

// mw3.cfg's install= and mw1_game= (the game's own settings file, shared).
void read_config(const std::string& write_dir, sdlfe::LauncherOptions& o) {
  std::ifstream f(fs::path(write_dir) / "mw3.cfg");
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("install=", 0) == 0) o.cfg_install = line.substr(8);
    if (line.rfind("mw1_game=", 0) == 0) o.cfg_mw1_game = line.substr(9);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (sdlfe::crash_box_main(argc, argv)) return 0;
  sdlfe::LauncherOptions o;
  bool windowed = false, window_given = false, hidden_log = false;
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    if (s == "--write-dir") o.write_dir = next();
    else if (s == "--windowed") windowed = true;
    else if (s == "--window") {
      const std::string v = next();
      if (std::sscanf(v.c_str(), "%dx%d", &o.win_w, &o.win_h) == 2) window_given = true;
    } else if (s == "--nosound") o.sound = false;
    else if (s == "--hidden") o.hidden = true;
    else if (s == "--no-import") o.no_import = true;
    else if (s == "--launcher-shot") o.shot = next(), hidden_log = true;
    else if (s == "--launcher-screen") o.screen = next();
    else if (s == "--launcher-play") o.play = true;
    else if (s == "--launcher-mw1") o.play_mw1 = true;
    else {
      std::fprintf(stderr,
                   "usage: mw3launcher [--write-dir DIR] [--windowed | --window WxH] [--nosound] [--hidden] [--no-import]\n"
                   "    [--launcher-shot FILE [--launcher-screen main|setup|work|notes]] [--launcher-play] [--launcher-mw1]\n");
      return 2;
    }
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "mw3launcher: SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const bool write_dir_given = !o.write_dir.empty();
  o.write_dir_fixed = write_dir_given;
  bool pointer = false;  // --write-dir wins; else the usual folder, or the one its pointer names
  if (!write_dir_given && !sdlfe::data_folder_at_start(&o.write_dir, !hidden_log && !o.hidden, &pointer)) {
    SDL_Quit();
    return 1;
  }
  while (o.write_dir.size() > 1 && o.write_dir.back() == '/') o.write_dir.pop_back();
  std::error_code ec;
  if (!o.write_dir.empty()) fs::create_directories(o.write_dir, ec);
  // Its own log beside the game's: mw3launcher.log (the game's is mw3.log).
  sdlfe::RunLog run_log;
  if (!hidden_log && !sdlfe::log_to_file(o.write_dir, "mw3launcher") && !o.write_dir.empty())
    run_log.open(o.write_dir, "mw3launcher");
  std::printf("mw3launcher (MechWarrior 3 rebuild) %s, write dir %s\n", MW3_VERSION, o.write_dir.c_str());
  if (!hidden_log && !o.write_dir.empty()) sdlfe::crash_report_install("mw3launcher", o.write_dir + "/mw3launcher.log", true);
  read_config(o.write_dir, o);
#ifdef MW3_GAME_DIR
  if (!std::getenv("MW3_FIND_INSTALL")) o.more_installs.push_back(MW3_GAME_DIR);
#endif
  o.fullscreen = !windowed && !window_given;
  o.game_bin = game_bin();
  if (o.game_bin.empty()) std::printf("mw3launcher: no mw3 beside this program: MECHWARRIOR 3 cannot start\n");
  // The game gets this start's choices; a named write dir goes on too.
  if (write_dir_given || pointer) o.game_args.push_back("--write-dir"), o.game_args.push_back(o.write_dir);
  if (windowed) o.game_args.push_back("--windowed");
  if (window_given) o.game_args.push_back("--window"), o.game_args.push_back(std::to_string(o.win_w) + "x" + std::to_string(o.win_h));
  if (!o.sound) o.game_args.push_back("--nosound");
  if (o.hidden) o.game_args.push_back("--hidden");
  if (o.no_import) o.game_args.push_back("--no-import");
  o.quit_after_game = o.play;
  sdlfe::run_launcher(o);
  SDL_Quit();
  return 0;
}
