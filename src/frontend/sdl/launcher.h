#pragma once
// The launcher and the first-start setup (ours, not original code;
// docs/launcher.md): the first screen of a player's start, before any
// movie. It finds the MechWarrior 3 install quietly and offers the games; a
// game without its files opens the find screen (find it, install from the
// disc, an image or a download), and the notes offer what an install lacks
// (the movies, the music) from the disc, over mech3::setup. Its art
// is ours (assets/launcher); the ripple under the mouse is the main menu's
// (zrndr::ripple, 0x00558dd0).
#include <string>
#include <vector>

struct SDL_Window;

namespace sdlfe {

struct LauncherOptions {
  std::string write_dir;
  std::string cfg_install;                // mw3.cfg's install=
  std::string cfg_mw1_game;               // mw3.cfg's mw1_game= (MechWarrior 1989's folder)
  std::vector<std::string> more_installs;  // a development build's copy
  bool fullscreen = true;
  bool sound = true;  // the main menu's rollover and select sounds from the install (off with --nosound)
  int win_w = 1280, win_h = 960;
  // For checking (--launcher-shot FILE [--launcher-screen main|setup|pick|work|notes|disc]):
  // the screen drawn once into FILE (a BMP of the 640x480 canvas), no window shown.
  std::string shot, screen = "main";
  // A plain mw3 start that found no install (docs/games-layout.md): the find
  // screen alone, straight on into the game once there is one.
  bool setup_only = false;
  bool write_dir_fixed = false;  // --write-dir given: no moving the data folder
  // mw3launcher (docs/launcher.md, The two programs): MECHWARRIOR 3 starts
  // this program as a child, `game_bin <install> game_args... --ready-file F`,
  // the launcher hidden from the game's first frame until it exits.
  std::string game_bin;
  std::vector<std::string> game_args;
  bool hidden = false;  // --hidden: the window never shown (checks), passed on to the game
  bool no_import = false;  // --no-import: no movie conversion here (passed on to the game)
  bool play = false;  // --launcher-play: MECHWARRIOR 3 pressed once the window is up (checks)
  bool quit_after_game = false;  // with play: quit when the game exits (checks)
  bool play_mw1 = false;  // --launcher-mw1: MECHWARRIOR (1989) pressed once, then quit when it returns (checks)
};

struct LauncherChoice {
  enum Kind { kQuit, kPlay } kind = kQuit;
  std::string install;  // kPlay: MechWarrior 3's folder (saved to mw3.cfg)
  // kPlay: the launcher's window, its renderer gone, for the game to take
  // (SdlWindow::init): one window from the launcher into the game.
  SDL_Window* window = nullptr;
};

LauncherChoice run_launcher(const LauncherOptions& o);

}  // namespace sdlfe
