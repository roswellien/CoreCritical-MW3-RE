#pragma once
// The usual folder and, through the pointer (mech3/data_folder.h), the player's
// data folder, as every program starts: mw3, mw3launcher, mw3import.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "mech3/data_folder.h"

namespace sdlfe {

// The usual place; MW3_USUAL_FOLDER names another (checks).
inline std::string usual_folder(const char* game = "MechWarrior 3") {
  if (const char* env = std::getenv("MW3_USUAL_FOLDER")) return env;
  std::string dir;
  if (char* p = SDL_GetPrefPath("MicroProse", game)) dir = p, SDL_free(p);
  while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
  return dir;
}

// `dir` set to the folder to use. A pointer's folder that cannot be used is said
// and asked about again (a drive to connect), and never replaced by a fresh
// folder: false when the player quits, or at once when `ask` is false.
inline bool data_folder_at_start(std::string* dir, bool ask, bool* pointer = nullptr, const char* game = "MechWarrior 3") {
  const std::string usual = usual_folder(game);
  for (;;) {
    const mech3::data_folder::Resolved r = mech3::data_folder::resolve(usual);
    if (pointer) *pointer = r.pointer;
    if (r.error.empty()) return *dir = r.dir, true;
    std::fprintf(stderr, "mw3: %s\n", r.error.c_str());
    if (!ask) return false;
    const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Try again"},
                                                {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}};
    SDL_MessageBoxData box{};
    box.flags = SDL_MESSAGEBOX_ERROR, box.title = "MechWarrior 3", box.message = r.error.c_str();
    box.numbuttons = 2, box.buttons = buttons;
    int chose = 0;
    if (!SDL_ShowMessageBox(&box, &chose) || chose != 1) return false;
  }
}

}  // namespace sdlfe
