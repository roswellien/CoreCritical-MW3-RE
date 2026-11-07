#pragma once
#include <cstdint>
#include <list>
#include <vector>

#include "mech3/menu_widgets.h"
#include "mech3/pilot.h"

// The briefing screen (docs/campaign.md section 2): C:\zipper\Mech3\
// briefing.cpp (0x00404ae0..0x00406440) and its helpers after it
// (0x00408b40..0x00409440). The screen the Load state builds while the
// mission loads: the buttons and their enabling, the progress bar, the
// frame, ABORT and LAUNCH. The briefing's map and its script of voices and
// map steps (0x00406650, the SOUND key), the reallocation list
// (REALLOC_LIST, 0x00404ae0) and the loadouts (0x00408b40..0x00408c30) are
// later items (campaign.md section 7).
namespace mech3 {

// An entry of the briefing's script (vtable 0x005bbc60): played one after
// another, each until its step says done.
class BriefingStep {
 public:
  virtual ~BriefingStep() = default;
  virtual bool step(float dt) = 0;   // +0x00: true when done
  virtual void x04() {}              // +0x04
  virtual void reset() {}            // +0x08: REPLAY
};

// The briefing's voice (vtable 0x005bbc50), 0x60 bytes: played with its cue
// markers moving the map on (briefing_cue); without the sound, every map
// step shows at once (999).
class BriefingVoice : public BriefingStep {
 public:
  BriefingVoice(Engine& e, const char* sound);
  bool step(float dt) override;      // +0x00  0x00405e10: started; done at once

  Engine& e;                         // (the original's calls are global)

  char name[0x50] = {};              // +0x04  strncpy'd (not ended when 0x50 long)
  float volume = 1.0f;               // +0x54
  uint8_t cues = 1;                  // +0x58
  int32_t x5c = 0;                   // +0x5c
};

// A map step (vtable 0x005bbc40), 8 bytes: done once the voice's cue reached
// it; the last one (-1) once the voice has ended.
class BriefingMarker : public BriefingStep {
 public:
  BriefingMarker(Engine& en, int32_t m) : e(en), marker(m) {}
  bool step(float dt) override;      // +0x00  0x00405db0

  Engine& e;
  int32_t marker;                    // +0x04
};

// A map object shown at its step (vtable 0x005bbc30), 8 bytes.
class BriefingShow : public BriefingStep {
 public:
  explicit BriefingShow(ztwod::Object2D* o) : obj(o) {}
  bool step(float dt) override;      // +0x00  0x00405d50
  void reset() override;             // +0x08  0x00405d90: hidden again (REPLAY)

  ztwod::Object2D* obj;              // +0x04
};

// ---- the briefing map's objects (0x00406650 reads them; campaign.md 2) ----

// A text of the map (vtable 0x005bc030), 0x4b0 bytes, made hidden.
class MapText : public ztwod::FontText {
 public:
  MapText();                         // 0x00407c90
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x00407cf0: its old place erased first
};

// A circle (vtable 0x005bbfb0), 0x50c bytes: shown with its sound, it
// shrinks from its first radius to its last, a label at its lower right.
class MapCircle : public ztwod::Frame {
 public:
  MapCircle(Engine& e, int32_t px, int32_t py, int32_t r, uint32_t color);  // inline in 0x00406650
  void erase() override {}           // +0x08  0x00405d40
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x004080a0: the label follows
  void update(float dt) override;    // +0x24  0x00407ea0
  void show_for(float seconds) override;  // +0x60  0x00407e40
  void set_radius(int32_t r);        // 0x00408050: and the label
  void shrink(float dt);             // the body of 0x00407ea0 (and of 0x004081a0)

  MapText label;                     // +0x54
  Handle sound = 0;                  // +0x504
  int32_t end_radius = 3;            // +0x508

 protected:
  Engine& e_;
  float owed_ = 0.0f;                // FIX (B): pixels owed (not in the original)
};

// A waypoint (vtable 0x005bbeb0), 0x514 bytes: a circle at a nav point of
// the mission, placed on the mission's map and labelled.
class MapWaypoint : public MapCircle {
 public:
  using MapCircle::MapCircle;
  void update(float dt) override;    // +0x24  0x004081a0: kept on its nav point once a world is up

  Handle map = 0;                    // +0x50c  the mission's map (0x00491c80)
  Handle nav = 0;                    // +0x510
  int32_t shift_x = 0;               // ours (16:9 menus): the map moved this far right on a wide canvas
};

// A triangle (vtable 0x005bbe30), 300 bytes.
class MapTriangle : public ztwod::Shape {
 public:
  MapTriangle(Engine& e, int32_t px, int32_t py, int32_t size, uint32_t color);  // 0x005a31e0 inline
  void erase() override;             // +0x08  0x00408470
  void update(float dt) override;    // +0x24  0x004084f0
  void show_for(float seconds) override;  // +0x60  0x004084b0

  Handle sound = 0;                  // +0x124
  float end = 5.0f;                  // +0x128  height's second value (read by nothing)

 private:
  Engine& e_;
};

// A line through points (vtable 0x005bbdb0), 0xf0 bytes: drawn every frame.
class MapLine : public ztwod::PolyLine {
 public:
  void update(float dt) override;    // +0x24  0x00408600
  float xec = 1.0f;                  // +0xec
};

// The script (+0x11220): a list, the entry playing and whether it plays.
struct BriefingScript {
  explicit BriefingScript(Engine& e);  // inline in 0x00405230: the voice stopped
  std::list<BriefingStep*> steps;    // +0x00 (+0x04 its head, +0x08 its size)
  std::list<BriefingStep*>::iterator at = steps.end();  // +0x0c
  uint8_t playing = 0;               // +0x10

  void stop(Engine& e);              // 0x004063e0 (and inline): the voice stopped, at the end
  void start() {                     // the Load state's (0x00409440)
    playing = 1;
    at = steps.begin();
  }
  void step(float dt);               // the head of 0x00406440
};

// [0x005e043c] the map step the script reached (-1 none) and [0x005f4c84]
// the voice playing.
int32_t& briefing_reached();
Handle& briefing_voice();
// 0x00405ed0: a cue marker of the voice reached (ECX the marker).
void briefing_cue(int32_t marker);

// The screen (vtable 0x005bbc70), 0x12e6c bytes.
class BriefingScreen : public MenuScreen {
 public:
  enum Button : int32_t { kBtnAbort = 0, kBtnLaunch = 1, kBtnMechLab = 2, kBtnReplay = 3, kBtnSalvage = 4,
                          kBtnLoading = 5, kBtnReallocOk = 7 };

  BriefingScreen(Engine& e, const char* name, bool draw_now = true);  // 0x00405230: the briefing's section of briefing.zrd
  void draw_first();  // the constructor's end: drawn once (apart for the widening, ours)
  int32_t first_map_w = -1;  // ours, for the tests: the map's width when that first frame drew
  ~BriefingScreen() override;        // 0x00406170 (deleting 0x00405fd0)
  void update(float dt) override;    // +0x00  0x00406440
  void on_button(int32_t id, int32_t a, int32_t b) override;  // +0x14  0x00404c30

  void set_loaded(bool loaded);      // 0x00408ef0: the buttons for the game, once the load ends
  void abort();                      // 0x00405050
  void launch();                     // 0x00404f70
  void progress(float f) { target = f; }  // 0x004050d0 on [0x005f4c80]
  void log(const char* s);           // 0x00405120 on [0x005f4c80]: a line in the debug lines
  void read_script(zrdr::Node* section);  // inline in 0x00405230: SOUND, its MARKERS' objects
  ztwod::Object2D* read_object(zrdr::Node* entry);  // 0x00406650: a map object, kept (+0x12e60)
  Store* build_lab_store();          // 0x00408b40: the store the mech lab opens on
  void refresh();                    // 0x00408bd0: the lab's store reallocated
  void show_realloc(bool on);        // 0x00404ae0: REALLOC_LIST and REALLOC_OK, the others off
  void lance_loadouts();             // 0x00408c30: every slot's mech and loadout out of the lab's store

  Store realloc_store;               // +0x111ec  what the mech lab left over (0x00408bd0)
  Store lab_store;                   // +0x11200  the mech lab's (0x00408b40)
  uint8_t salvage_seen = 0;          // +0x11214  SALVAGE pressed since the load ended
  float target = 0.0f;               // +0x11218  the load's fraction (0x004050d0)
  uint8_t loaded = 1;                // +0x1121c  the buttons' state (set_loaded)
  BriefingScript script{engine()};   // +0x11220
  ShellSlider bar;                   // +0x11234  TRANSPORT_PROGRESS (a click moves it)
  ztwod::TextBox lines{0x19};        // +0x113bc  the load's lines, shown with the app's +0x1ba
  HelpButton abort_btn, launch_btn, lab_btn, replay_btn, salvage_btn, loading;  // +0x11884, 0x17c each
  ztwod::Text version;               // +0x1216c  "<message 7> <version>", at the bottom right
  ShellButton realloc_ok;            // +0x12638  REALLOC_OK (7)
  StoreList realloc_list;            // +0x12790  REALLOC_LIST (what did not fit)
  std::list<ztwod::Object2D*> objects;  // +0x12e60  the map's, deleted with the screen
};

// The stores' sums: tons (0x00409fd0, each weight(-1) added as a float),
// mechs (0x00472970), the count of the items like `item` (0x00473510).
float store_tons(const Store& s);
int32_t store_mechs(const Store& s);
int32_t store_count_like(const Store& s, const ConfigItem* item);
// 0x00408690 (ECX from, EDX owner): of each of owner's items as many as
// `from` has like it moved from `from` into `out`, with a limit (tons or
// mechs not 0) only what fits beside what `out` holds.
void store_take(Store& from, const Store& owner, Store& out, float tons, int32_t mechs);
// 0x00408850: the mech lab's store split back into the game's (what fits in
// `units` x 300 t and x 2 mechs, its own first) and the salvage (the rest,
// also into `realloc` when given).
void store_reallocate(Store& lab, Store& game, Store& salvage, Store* realloc, int32_t units);
// 0x00408b40's work: a campaign's (`salvage` given) lab store is the game's
// and the salvage together; otherwise the game's own store is the lab's.
Store* lab_store_build(Store& lab, Store* game, Store* salvage);
// 0x00408c30's work on one slot: its mech the first from it on (round the
// session catalogue) the store holds, its loadout (the definition's first when none) cut
// to the store and taken out of it, the mech too; `keep` (a lancemate at Op
// 4 Mission 6) leaves the slot as it is, the store untouched.
void lance_slot(GameSlot& s, Store& store, bool keep);
// 0x00408c30's loop: every slot fitted; with `keep` the lancemates (not the
// player, slot 0) are left as they are. `keep` is Op 4 Mission 6's.
void lance_fit(Store& store, const std::vector<GameSlot*>& slots, bool keep);
bool lance_keeps(bool campaign, int32_t op, int32_t mission);

// [0x005f4c80] the briefing screen, while the Load state has one.
BriefingScreen*& briefing_screen();

}  // namespace mech3
