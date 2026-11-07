#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mech3/armor_display.h"
#include "mech3/menu_widgets.h"

// The debriefing screen (docs/campaign.md section 3): the Win State's
// (new(0x13bf4), 0x0047b430, its press 0x0047d030), dlgWin.zrd's WINMENU.
// Original file unknown: 0x0047af70..0x0047d2c0, between the Win State
// (0x0047ab10..) and the Salvage State (0x0047d2f0..), named after them.
// The screen, its texts by the game and the result, the buttons and their
// press, the results (the lance, the points, the score, the rank), the
// cards' armor displays (0x0047e840..0x0047f140) and the salvage list.
namespace mech3 {

// The mech lab's mech button (0x0044a770, vtable 0x005c0d70): the help
// button with a mech's number and its name under the image.
class MechButton : public HelpButton {
 public:
  explicit MechButton(Engine& e);    // 0x0044a770
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0044a840
  void show_name(bool on);           // 0x0044aa70
  // 0x0044a890 (lab_screen.cpp): a mech's icons (none for -1), then chosen
  // or not with the glow (0x0044af90).
  void set_mech(int32_t mech, bool selected, ztwod::Image* glow);

  int32_t mech = 0x13;               // +0x17c  0x13: none
  ztwod::Text name;                  // +0x180
};

// A lance member's card (0x0047af70, vtable 0x005c39b0), 0x6a0 bytes: its
// mech button with the mech's display (0x0047e840, HTAL's place) and a line.
class LanceCard : public MechButton {
 public:
  explicit LanceCard(Engine& e);     // 0x0047af70
  ~LanceCard() override;             // 0x0047b180: the display deleted
  void update(float dt) override;    // +0x24  0x0047b2b0: no blink; the line and the display
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0047b070
  void on_left_up() override {}      // +0x30  0x0049bfd0: a card is not pressed
  void on_enter() override {}        // +0x3c  0x0049bfd0
  void on_leave() override {}        // +0x40  0x0049bfd0
  void set_vehicle(Handle v);        // 0x0047b210
  void set_mech_image(int32_t mech, int32_t color);  // 0x0047b260

  Handle vehicle = 0;                // +0x64c
  std::unique_ptr<LanceDisplay> display;  // +0x650
  ztwod::Line2D line{0, 0, 0, 0, 0};  // +0x654
};

// The rank's icon (vtable 0x005c3bd0) and the title (vtable 0x005c11c0):
// no rollover (+0x3c, +0x40 0x0049bfd0).
class RankIcon : public HelpButton {
 public:
  explicit RankIcon(Engine& e) : HelpButton(e, -1) {}
  void on_enter() override {}
  void on_leave() override {}
  void set_rank(int32_t score);      // 0x0047b2f0
};
class WinTitle : public ztwod::Control {
 public:
  void on_enter() override {}
  void on_leave() override {}
};

// The screen (vtable 0x005c3b00), 0x13bf4 bytes.
class DebriefScreen : public MenuScreen {
 public:
  enum Button : int32_t { kContinue = 0, kReplay = 1, kAbort = 2 };

  // 0x0047b430: the Win State's address, the operation and mission shown,
  // and the Win State's +0x10 (the campaign is over).
  DebriefScreen(Engine& e, Handle state, int32_t campaign, int32_t mission, const uint8_t* won);
  ~DebriefScreen() override;         // 0x0047c920 (deleting 0x0047c8d0)
  void update(float dt) override;    // +0x00  0x0042efe0: the window's
  void on_button(int32_t id, int32_t a, int32_t b) override;  // +0x14  0x0047d030
  void results(int32_t kind);        // 0x0047baf0..0x0047c84b, inlined in 0x0047b430
  ztwod::Text* add_text(const char* s, int32_t x, int32_t* y, Handle font, int32_t step);    // 0x0047cab0
  ztwod::Text* add_number(int32_t v, int32_t x, int32_t* y, Handle font, int32_t step);      // 0x0047cce0
  ztwod::Text* add_points(int32_t v, int32_t x, int32_t* y, Handle font, int32_t step);      // 0x0047cd70

  ztwod::Image background{0};        // +0x111ec  WIN_BACKGROUND
  HelpButton continue_btn, replay_btn, abort_btn;  // +0x112a8, +0x11424, +0x115a0 (0x0044c310)
  LanceCard cards[4];                // +0x1171c  ICON_PLAYER0..3
  RankIcon rank;                     // +0x1319c  ICON_RANK
  ztwod::Line2D lines[2]{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}};  // +0x13318, +0x13364
  WinTitle title;                    // +0x133b0  WIN_TITLE
  StoreList salvage;                 // +0x134fc  SALVAGE_LIST (vtable 0x005c3b20: only its destructor its own)
  std::vector<ztwod::Text*> texts;   // +0x13bd0  the results' texts (+0x13bcc its allocator byte)
  const uint8_t* won;                // +0x13bdc  the Win State's +0x10
  int32_t score = 0;                 // +0x13be0  the campaign's +0xc4, with this mission's
  int32_t lance_scores[4] = {};      // +0x13be4  each pilot's mechs destroyed in the campaign (into +0xb4)
  uint8_t clean = 0;                 // no cheat ([0x00609994] clear; a local of 0x0047b430)
  bool lance_scored[4] = {};         // (ours) the record line written: lance_scores[i] is real
};

// [0x00608bf8]: the lance's count the last results found (cards used).
int32_t& debrief_lance_count();
std::string debrief_rows(const DebriefScreen& d);  // (ours) the results' texts, " | " between
// 0x0044a9b0 (the mech lab's): a mech's icon, "icon_<name>_" and red,
// green, yellow or grey by `color` (0..3; else none added).
zimg::Texture* mech_icon(Engine& e, int32_t mech, int32_t color);

}  // namespace mech3
