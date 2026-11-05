#pragma once
#include <memory>
#include <string>
#include <vector>

#include "gamez/ztwod/twod.h"
#include "mech3/app_states.h"

namespace mech3 { class GameEngine; }

// mw3's debug starts (docs/program.md): states of our own, not the original's,
// that take the place of the state after the intro (the app's +0x1dc, where a
// lobby launch puts the Load state) while the menus and the mission load are
// not ported. They run inside the original main loop like any state.
namespace sdlfe {

class SdlInput;
class FrameRenderer;

struct DebugMissionOptions {
  std::string mission = "c1";    // zbd/<mission>
  std::string variant;           // a world.zrd block, "" the first ("m1")
  int number = 1;                // the mission number the music pick uses
  float eye[3] = {0, 60, 0};
  float yaw = 0, pitch = -0.05f; // radians
  std::string shot;              // a .bmp to save after `shot_frame` frames, then quit
  int shot_frame = 30;
  int frames = 0;                // quit after this many frames (0: run until Escape or close)
};

// A mission world through the ported render path, a free camera on the
// keyboard (W/S/A/D, Q/E down and up, arrows turn, Shift fast, right mouse
// button looks), zSound's frame and the mission's music as the Core state
// starts them. Escape quits.
class DebugMissionState : public mech3::State {
 public:
  DebugMissionState(mech3::GameEngine* engine, SdlInput* input, FrameRenderer* render, DebugMissionOptions o)
      : State("Debug Mission State", 0), engine_(engine), input_(input), render_(render), o_(std::move(o)) {}
  bool enter() override;
  bool update() override;
  void leave() override;

 private:
  mech3::GameEngine* engine_;
  SdlInput* input_;
  FrameRenderer* render_;
  DebugMissionOptions o_;
  int frame_ = 0;
  bool loaded_ = false;
};

// One .zrd dialog drawn by the ported zTwoD on the SDL host: every button and
// primitive bound by name, the mouse moving over them. Escape quits.
class DebugDialogState : public mech3::State {
 public:
  DebugDialogState(mech3::GameEngine* engine, SdlInput* input, FrameRenderer* render, std::string file,
                   std::string name, std::string shot, int shot_frame)
      : State("Debug Dialog State", 0), engine_(engine), input_(input), render_(render), file_(std::move(file)),
        name_(std::move(name)),
        shot_(std::move(shot)), shot_frame_(shot_frame) {}
  ~DebugDialogState() override;
  bool enter() override;
  bool update() override;
  void leave() override;

 private:
  mech3::GameEngine* engine_;
  SdlInput* input_;
  FrameRenderer* render_;
  std::string file_, name_, shot_;
  int shot_frame_;
  int frame_ = 0;
  std::unique_ptr<ztwod::Dialog> dialog_;
  std::vector<ztwod::Object2D*> bound_;
};

// An fmv.zrd block played as the states play theirs (docs/fmv.md): made,
// given the window, loaded, played and polled until it ends; then the frames
// shown are reported. Escape skips it, as in the game.
class DebugMovieState : public mech3::State {
 public:
  // With `shot`: the movie's `shot_frame`-th frame shown is saved there, then it quits.
  DebugMovieState(mech3::GameEngine* engine, FrameRenderer* render, std::string name, std::string shot,
                  int shot_frame)
      : State("Debug Movie State", 0), engine_(engine), render_(render), name_(std::move(name)),
        shot_(std::move(shot)), shot_frame_(shot_frame) {}
  bool enter() override;
  bool update() override;
  void leave() override;

 private:
  mech3::GameEngine* engine_;
  FrameRenderer* render_;
  std::string name_, shot_;
  int shot_frame_;
  zgame::Handle movie_ = 0;
  int shown_at_start_ = 0;
};

}  // namespace sdlfe
