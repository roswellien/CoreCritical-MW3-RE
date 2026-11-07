// The ported menu screens and menu objects on the program's engine (our
// addition, not original code; docs/mainmenu.md, The port; docs/menus.md).
// The shell states reach their screens through Engine handles: construct,
// vcall, the Layer calls. A ported screen's handle is a block of the module
// memory the size of the original object, so the offsets the states add (a
// button's +0x111ec) still lead to its parts; a menu object's handle is a block
// of 0x10, its address for the hooks that name it.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "gamez/ztwod/twod.h"
#include "gamez/ztwod/twod_host.h"
#include "gamez/zimage/zimg_library.h"
#include "mech3/app.h"
#include "mech3/app_engine.h"
#include "mech3/mission_map.h"
#include "mech3/app_mech.h"
#include "mech3/app_states.h"
#include "mech3/briefing.h"
#include "mech3/debriefing.h"
#include "mech3/lab_screen.h"
#include "mech3/mech_objective.h"
#include "mech3/mech_reader.h"
#include "mech3/action_menu.h"
#include "mech3/mech_skin.h"
#include "mech3/controls_menu.h"
#include "mech3/credits.h"
#include "mech3/mainmenu.h"
#include "mech3/menu_states.h"
#include "mech3/mission.h"
#include "mech3/mech_salvage.h"
#include "mech3/options_menu.h"
#include "mech3/pause_menu.h"
#include "mech3/quit_box.h"
#include "mech3/salvage_screen.h"
#include "mech3/pilot.h"
#include "mech3/pilot_menu.h"
#include "mech3/mp_connect.h"
#include "mech3/mp_lobby.h"

namespace mech3 {

MenuScreen* GameEngine::screen_of(Handle h, uint32_t* offset) const {
  if (!h || screens_.empty()) return nullptr;
  auto it = screens_.upper_bound(h);
  if (it == screens_.begin()) return nullptr;
  --it;
  const uint32_t off = h - it->first;
  if (off >= it->second.size) return nullptr;
  if (offset) *offset = off;
  else if (off) return nullptr;
  return it->second.screen;
}

MenuState* GameEngine::menu_state_of(Handle h) const {
  auto it = menu_states_.find(h);
  return it == menu_states_.end() ? nullptr : it->second;
}

MenuState* GameEngine::popup_state_of(Handle h) const {
  uint32_t off = 0;
  auto* c = dynamic_cast<ControlsScreen*>(screen_of(h, &off));
  if (!c) return nullptr;
  if (off == 0x16760) return &c->axis_popup;
  if (off == 0x1677c) return &c->load_save_popup;
  return nullptr;
}

Handle GameEngine::adopt_screen(MenuScreen* screen, uint32_t size) {
  const Handle h = alloc_block(size);
  screens_[h] = {screen, size, true};
  // Ours (16:9 menus): spread like the others, by their constructors (the
  // sizes their states new: the pilot screen, the controls' two popups).
  const uint32_t ctor = size == 0x13184 ? 0x486f40 : size == 0x12194 ? 0x42da80 : size == 0x11cd4 ? 0x42e890 : 0;
  if (ctor) widen_screen(screen, ctor);
  return h;
}

namespace {
// Ours (16:9 menus, wide_menus with any_resolution; resolution.md section
// 12): a menu screen on a canvas wider than its 640 frame. A full-width
// background (600 or more across) goes to the middle, its sides filled from
// its own edges, mirrored and darkened (made here, the player's art left as
// it is); the other objects move by their screen's groups (below).
// `src` made `left` and `right` columns wider, each side its edge mirrored
// (from `inset` in) and dimmed outward, shade0 to shade1 (/256); its alpha
// mirrored with it. Null when it cannot (an 8-bit image).
zimg::Texture* extend_texture(zimg::Texture* src, int32_t left, int32_t right, int32_t inset, int32_t shade0,
                              int32_t shade1) {
  if (!src || !src->pixels || src->palette_count) return nullptr;
  const int w = src->width, h = src->height, nw = w + left + right;
  zimg::Texture* dst = ztwod::host().image_new(nw, h);
  if (!dst) return nullptr;
  auto* in = static_cast<const uint16_t*>(src->pixels);
  auto* out = static_cast<uint16_t*>(dst->pixels);
  if (src->alpha) {
    dst->flags = (src->flags & ~(zimg::kAlphaOwned | zimg::kPaletteOwned)) | zimg::kPixelsOwned | zimg::kAlphaOwned;
    dst->alpha = static_cast<uint8_t*>(std::calloc(static_cast<size_t>(nw) * h, 1));
  }
  auto dark = [](uint16_t p, int k) {  // 565, scaled by k/256
    const int r = (p >> 11) * k >> 8, g = ((p >> 5) & 63) * k >> 8, b = (p & 31) * k >> 8;
    return static_cast<uint16_t>(r << 11 | g << 5 | b);
  };
  for (int y = 0; y < h; ++y) {
    auto put = [&](int ox, int sx, int k) {
      out[y * nw + ox] = k < 256 ? dark(in[y * w + sx], k) : in[y * w + sx];
      if (dst->alpha) dst->alpha[y * nw + ox] = src->alpha[y * w + sx];
    };
    for (int x = 0; x < w; ++x) put(left + x, x, 256);
    for (int x = 0; x < left; ++x)  // outward from the left edge
      put(left - 1 - x, std::min(w - 1, inset + x), shade0 - (shade0 - shade1) * x / std::max(1, left));
    for (int x = 0; x < right; ++x)
      put(left + w + x, std::max(0, w - 1 - inset - x), shade0 - (shade0 - shade1) * x / std::max(1, right));
  }
  return dst;
}

void widen_background(ztwod::Image& im, int32_t extra, int32_t inset, int32_t shade0, int32_t shade1) {
  zimg::Texture* dst = extend_texture(im.image, extra / 2, extra - extra / 2, inset, shade0, shade1);
  if (!dst) return;
  im.set_image(dst);
  im.set_pos(im.get_x(), im.get_y());
}

// An overlay drawn as one image across two groups (the game options' panel,
// its link and the screens' box): made `extra` wider at column `at` from row
// `from_y` down, that column repeated, so a line crossing it lengthens and
// the part right of it lands with its group; above `from_y` the new columns
// are left clear.
void stretch_image(ztwod::Image& im, int32_t at, int32_t from_y, int32_t extra) {
  zimg::Texture* src = im.image;
  if (!src || !src->pixels || src->palette_count || at <= 0 || at >= src->width) return;
  const int w = src->width, h = src->height;
  zimg::Texture* dst = ztwod::host().image_new(w + extra, h);
  if (!dst) return;
  auto* in = static_cast<const uint16_t*>(src->pixels);
  auto* out = static_cast<uint16_t*>(dst->pixels);
  dst->flags = (src->flags & ~(zimg::kAlphaOwned | zimg::kPaletteOwned)) | zimg::kPixelsOwned;
  if (src->alpha) {  // drawn see-through by its alpha (the panel's shade)
    dst->alpha = static_cast<uint8_t*>(std::calloc(static_cast<size_t>(w + extra) * h, 1));
    dst->flags |= zimg::kAlphaOwned;
  }
  auto from = [&](int x) { return x < at ? x : x < at + extra ? at : x - extra; };
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w + extra; ++x) {
      const int sx = y >= from_y ? from(x) : x < w ? x : -1;
      out[y * (w + extra) + x] = sx < 0 ? in[0] : in[y * w + sx];  // clear: the corner's (empty) colour
      if (dst->alpha) dst->alpha[y * (w + extra) + x] = sx < 0 ? 0 : src->alpha[y * w + sx];
    }
  }
  im.set_image(dst);
  im.set_pos(im.get_x(), im.get_y());
}
}  // namespace

namespace {
// A screen's groups (in its 640x480 frame) and where each goes on a wide
// canvas: an object whose middle is inside a group moves with it, the rest
// stay one block in the middle with the background. Screens not listed are
// all one block. Whole groups, so a button's sphere and its label (separate
// objects) move together.
enum Anchor { kLeft, kCentre, kRight };
struct Group {
  int32_t x0, y0, x1, y1;
  Anchor anchor;
};
struct Stretch {
  int32_t x, y, w;     // the image: its place in the frame and its width
  int32_t at;          // the column repeated, out to the right group
  int32_t from_y = 0;  // from this row down
  bool to_middle = false;  // the part right of `at` goes to the middle group, not the right
  int32_t at2 = -1;        // a second column: `at`..`at2` to the middle group, past `at2` to the right
};
struct ScreenGroups {
  uint32_t ctor;  // the screen's constructor (GameEngine::construct)
  std::vector<Group> groups;
  std::vector<Stretch> stretches = {};
  int32_t inset = 0;  // the background's sides mirrored from this far in (past a map's marks)
  int32_t shade0 = 150, shade1 = 60;  // the sides' brightness (/256), at the art and at the canvas's edge
};
const std::vector<ScreenGroups>& screen_groups() {
  static const std::vector<ScreenGroups> k = {
      // The main menu: its column of buttons at the left edge, CREDITS and
      // QUIT and the version line at the right.
      {0x486260, {{0, 0, 330, 480, kLeft}, {380, 330, 640, 480, kRight}}},
      // The game options: the settings' panel left, the screens' box,
      // ACCEPT and BACK right.
      {0x4379f0, {{0, 0, 400, 420, kLeft}, {400, 0, 640, 480, kRight}}, {{0, 0, 483, 367}}},
      // The graphics and audio options: the settings' panel left, ACCEPT and
      // BACK (and the plate behind them) right.
      {0x438f60, {{0, 0, 380, 420, kLeft}, {380, 0, 640, 480, kRight}}},
      // The controls: the table, its lines and the description stay one
      // block in the middle; the two DEFAULT buttons left, ACCEPT and CANCEL
      // right, their lines in LINES (596 wide) stretched below the table.
      {0x42a590, {{0, 400, 340, 480, kLeft}, {380, 290, 640, 480, kRight}}, {{0, 0, 596, 400, 300}}},
      // The pilot screen: PILOT ENTRY and the pilots left; OPERATION (the
      // games, or training's cycles, their rects from x 0 but their middles
      // past 200), ACCEPT and CANCEL right; LINES' top line lengthened.
      {0x486f40, {{0, 0, 200, 480, kLeft}, {200, 0, 640, 480, kRight}}, {{0, 0, 619, 320}}},
      // The briefing: one block, the map in the middle, its sides from its
      // terrain, past the grid's labels and the north arrow at its edges.
      {0x405230, {}, {}, 40},
      // The debriefing: MECH STATUS left, SALVAGE FOUND right, MISSION
      // SUCCESS in the middle, each with the button under it; its dark
      // circuit board's sides barely dimmed (dimmed as the others, black).
      {0x47b430, {{0, 0, 213, 480, kLeft}, {427, 0, 640, 480, kRight}}, {}, 0, 256, 160},
      // The mech lab: one block (its panels change looks as it goes, a
      // double one across two columns), the 3D view's hole with it
      // (lab_place_window); the sides from its edge panels, its board as
      // the debriefing's.
      {0x44b920, {}, {}, 0, 256, 160},
      // Instant Action: ALLIES and ENEMIES left; the mission's box, ACCEPT
      // and CANCEL right; the lines (a 576-wide ShellButton's image) taken
      // out to the box at the link between them.
      {0x488bc0, {{0, 0, 385, 480, kLeft}, {385, 0, 640, 480, kRight}}, {{0, 51, 576, 385}}},
      // The credits: one block, but their lines (431 wide at 0,70) keep
      // their start at the screen's left edge, lengthened to the box.
      {0x4289a0, {{0, 70, 1, 71, kLeft}}, {{0, 70, 431, 100, 0, true}}},
      // The salvage screen: one block on the debriefing's circuit board.
      {0x47d3b0, {}, {}, 0, 256, 160},
      // The connection screen: NET NAME, SESSIONS, CANCEL and JOIN left;
      // CONNECT, HOST SETUP and HOST right; the boxes' lines (one 610x345
      // overlay) lengthened at column 300, which only the two links cross.
      {0x493260, {{0, 0, 330, 480, kLeft}, {330, 0, 640, 480, kRight}}, {{0, 0, 610, 300}}},
      // The lobby: GAME SETUP left, SCORES and CHAT in the middle, PLAYER
      // SETUP right; along the bottom CANCEL and LOCK OPTIONS left, MECHLAB
      // in the middle, I AM READY and START right. The columns' lines (one
      // 640x480 overlay) widen the middle column at 202 and 412, inside its
      // two dividers.
      {0x495f90,
       {{0, 400, 280, 480, kLeft}, {280, 400, 400, 480, kCentre}, {400, 400, 640, 480, kRight},
        {0, 0, 200, 400, kLeft}, {415, 0, 640, 400, kRight}},
       {{0, 0, 640, 202, 0, false, 412}}},
  };
  return k;
}
}  // namespace

void GameEngine::widen_screen(MenuScreen* s, uint32_t ctor) {
  const int32_t extra = buffer_width() - 640;
  if (!s || extra <= 0 || !zfix::options().any_resolution || !zfix::options().wide_menus) return;
  const ScreenGroups* table = nullptr;
  for (const ScreenGroups& g : screen_groups())
    if (g.ctor == ctor) table = &g;
  // Controls first: some move their own pieces that are on the list too (a
  // cycle's value, 0x0048b9b0; an edit bar's label, 0x0048b220), so those go
  // with them and are left alone after.
  struct Move {
    ztwod::Object2D* o;
    int32_t x, y, off;
  };
  std::vector<Move> moves;
  for (ztwod::Object2D* o = s->head; o; o = o->next) {
    const ztwod::Rect* r = o->get_rect();
    const int32_t left = (r ? r->left : o->get_x()) - s->origin_x, right = (r ? r->right : o->get_x()) - s->origin_x;
    const int32_t top = (r ? r->top : o->get_y()) - s->origin_y, bottom = (r ? r->bottom : o->get_y()) - s->origin_y;
    auto* im = dynamic_cast<ztwod::Image*>(o);
    bool stretched = false;  // an overlay the table stretches (checked first: some are 600 across)
    if (im && im->image && table)
      for (const Stretch& st : table->stretches) stretched |= left == st.x && top == st.y && im->image->width == st.w;
    // A plain opaque image the frame's size (the briefing's map), or anything 600 across.
    const int32_t img_w = im && !r && im->image && !im->image->alpha ? im->image->width : 0;
    // The controls' popups' backdrops: a grab of the canvas put in as each
    // opens (prepare), already the canvas's width: left where they are.
    auto* lsp = dynamic_cast<LoadSavePopupScreen*>(s);
    auto* axp = dynamic_cast<AxisPopupScreen*>(s);
    auto* team = dynamic_cast<TeamScreen*>(s);  // the new team's dialog's: the same (0x00579500(2) as it enters)
    if ((lsp && o == &lsp->backdrop) || (axp && o == &axp->background) || (team && o == &team->backdrop)) continue;
    if (im && !stretched && (right - left >= 600 || (img_w >= 600 && img_w <= 640))) {
      widen_background(*im, extra, table ? table->inset : 0, table ? table->shade0 : 150, table ? table->shade1 : 60);
      continue;
    }
    Anchor a = kCentre;
    // A rect from 0 to past its place was pulled there by a label at 0 (the
    // lab's data and lists): such an object goes by its own place.
    const bool pulled = r && left == 0 && o->get_x() - s->origin_x > 0;
    if (auto* lp = dynamic_cast<LabPanel*>(o); lp && (lp->single || lp->image)) {
      // A lab panel at a frame's edge: each of its looks made wider on that
      // side (the left one then stays, its art half the extra width in).
      const int32_t pw = (lp->single ? lp->single : lp->image)->width;  // (its look is chosen later: show_look)
      const bool at_left = o->get_x() - s->origin_x == 0, at_right = o->get_x() - s->origin_x + pw == 640;
      if (at_left || at_right) {
        const int32_t l = at_left ? extra / 2 : 0, rr = at_right ? extra - extra / 2 : 0;
        const int32_t k0 = table ? table->shade0 : 150, k1 = table ? table->shade1 : 60;
        zimg::Texture* shown = lp->image;
        for (zimg::Texture** t : {&lp->single, &lp->twice, &lp->twice2}) {
          zimg::Texture* wide = extend_texture(*t, l, rr, 0, k0, k1);
          if (!wide) continue;
          if (shown == *t) lp->set_image(wide);
          ztwod::host().image_release(*t);
          *t = wide;
        }
        if (at_left) continue;
      }
    }
    const int32_t mx = pulled ? o->get_x() - s->origin_x : (left + right) / 2;
    const int32_t my = pulled ? o->get_y() - s->origin_y : (top + bottom) / 2;
    if (table)
      for (const Group& g : table->groups)
        if (mx >= g.x0 && mx < g.x1 && my >= g.y0 && my < g.y1) {
          a = g.anchor;
          break;
        }
    const int32_t off = a == kLeft ? 0 : a == kRight ? extra : extra / 2;
    if (im && im->image && table)
      for (const Stretch& st : table->stretches)
        if (left == st.x && top == st.y && im->image->width == st.w) {
          if (st.at2 >= 0) {  // the right column first, so `at` still stands where it was
            stretch_image(*im, st.at2, st.from_y, extra - extra / 2);
            stretch_image(*im, st.at, st.from_y, extra / 2 - off);
          } else {
            stretch_image(*im, st.at, st.from_y, (st.to_middle ? extra / 2 : extra) - off);
          }
        }
    if (off) moves.push_back({o, o->get_x(), o->get_y(), off});
  }
  for (const bool controls : {true, false})
    for (const Move& m : moves) {
      if ((dynamic_cast<ztwod::Control*>(m.o) != nullptr) != controls) continue;
      if (!controls && (m.o->get_x() != m.x || m.o->get_y() != m.y)) continue;  // moved with its control
      // An edit bar keeps its label's place, not its offset (0x0048b1d0), so
      // its set_pos (0x0048b220) would add the bar's place again: moved as
      // an image, its label (on the list) by itself.
      if (auto* eb = dynamic_cast<ShellEditBar*>(m.o)) eb->ztwod::Image::set_pos(m.x + m.off, m.y);
      else if (auto* sb = dynamic_cast<SlideButton*>(m.o)) {
        // Its set_pos (0x0042a400) puts the ripple at the image's centre, but
        // a baked button (FastMenu) or one not yet shown has no image then:
        // the ripple went to the corner (playtest W41). The ripple and its
        // reach are carried by the offset, as placed at the bind.
        const int32_t rx = sb->ripple.get_x(), ry = sb->ripple.get_y();
        const ztwod::Rect reach = sb->reach;
        sb->set_pos(m.x + m.off, m.y);
        sb->ripple.set_pos(rx + m.off, ry);
        sb->reach = {reach.left + m.off, reach.top, reach.right + m.off, reach.bottom};
      } else {
        m.o->set_pos(m.x + m.off, m.y);
      }
      auto* pl = dynamic_cast<ztwod::PolyLine*>(m.o);  // its points are where it draws (0x005a38f0)
      if (pl && !dynamic_cast<ztwod::Shape*>(pl) && !dynamic_cast<ztwod::Caret*>(pl))
        for (int32_t i = 0; i < pl->count; ++i) pl->pts[i][0] += m.off;
      if (auto* ln = dynamic_cast<ztwod::Line2D*>(m.o)) ln->end_x += m.off;  // drawn from its place to its end
      if (auto* w = dynamic_cast<MapWaypoint*>(m.o)) w->shift_x += m.off;  // put back on its nav point each frame
      if (auto* fl = dynamic_cast<FileList*>(m.o)) {  // its arrows, and its rect when its image sets it (0x0048d2f0)
        for (FileListArrow* ar : {fl->up, fl->down})
          if (ar) ar->set_pos(ar->get_x() + m.off, ar->get_y());
        if (fl->image) fl->rect.left += m.off, fl->rect.right += m.off;
      }
      if (auto* cr = dynamic_cast<CreditsScroll*>(m.o))  // RECT as read (0x00429200): the names go in it each frame (W60)
        cr->left += m.off, cr->right += m.off;
      if (auto* b = dynamic_cast<MainMenuButton*>(m.o))  // its underline, drawn by the button
        b->line.set_pos(b->line.get_x() + m.off, b->line.get_y());
      auto* cb = dynamic_cast<ztwod::CheckButton*>(m.o);  // its rect is BOUNDS as read (a list box's rows)
      if (cb && cb->bounds.right > cb->bounds.left) cb->bounds.left += m.off, cb->bounds.right += m.off;
    }
  // Ours: the lobby's two address lines (piece 9c), under CANCEL and LOCK at
  // 640, go to the middle column's band under the chat bar, which the width
  // leaves room for (at 640 they would cross MECHLAB).
  if (auto* l = dynamic_cast<LobbyScreen*>(s)) {
    l->internet_line.set_pos(s->origin_x + 207 + extra / 2, s->origin_y + 379);
    l->lan_line.set_pos(s->origin_x + 207 + extra / 2, s->origin_y + 389);
  }
}

Handle GameEngine::construct(uint32_t ctor, std::initializer_list<Arg> args) {
  const Handle a0 = args.size() ? args.begin()->v : 0;
  // FIX (B), playtest W13: every menu screen shows on the menus' 640x480
  // (mode 5: 0x00438c90, Core's leave), so its frame is at 0 even when Core
  // makes it on the mission's wide canvas. The pause screen shows over the
  // mission on its canvas: at 0 too, its buttons against the left edge (W36;
  // its title is a marquee across the display's whole width already).
  struct MenuFrame {
    ztwod::DialogOrigin keep = ztwod::dialog_origin();
    explicit MenuFrame(bool menus) {
      if (menus) ztwod::dialog_origin() = {};
    }
    ~MenuFrame() { ztwod::dialog_origin() = keep; }
  } frame(true);
  auto screen = [&](uint32_t size, MenuScreen* (*make)(GameEngine&, Handle)) {
    const Handle h = alloc_block(size);
    screens_[h] = {make(*this, a0), size};
    if (ctor != 0x492750) widen_screen(screens_[h].screen, ctor);
    return h;
  };
  auto state = [&](MenuState* (*make)(Handle)) {
    const Handle h = alloc_block(0x10);
    menu_states_[h] = make(h);
    return h;
  };
  switch (ctor) {
    case 0x486260: {  // the main menu, new(0x12c1c)
      const Handle h = screen(0x12c1c, [](GameEngine& e, Handle s) -> MenuScreen* { return new MainMenu(e, s); });
      poke32(0x609854, h);  // its constructor's [0x00609854] = this
      return h;
    }
    case 0x4289a0:
      return screen(0x11ae0, [](GameEngine& e, Handle s) -> MenuScreen* { return new CreditsScreen(e, s); });
    case 0x4379f0:
      return screen(0x1407c, [](GameEngine& e, Handle s) -> MenuScreen* { return new GameOptionsScreen(e, s); });
    case 0x438f60:
      return screen(0x13f9c, [](GameEngine& e, Handle s) -> MenuScreen* { return new GfxAudioScreen(e, s); });
    case 0x438be0:
      return state([](Handle h) -> MenuState* { return new OptionsState(OptionsState::kGame, h); });
    case 0x43a640:
      return state([](Handle h) -> MenuState* { return new OptionsState(OptionsState::kGfxAudio, h); });
    case 0x42a590: {  // the controls screen, new(0x16c94): its popups' states sit at its offsets
      const Handle h = alloc_block(0x16c94);
      screens_[h] = {nullptr, 0x16c94};
      screens_[h].screen = new ControlsScreen(*this, a0, h);
      widen_screen(screens_[h].screen, ctor);
      return h;
    }
    case 0x42d9d0:
      return state([](Handle h) -> MenuState* { return new OptionsState(OptionsState::kControls, h); });
    case 0x4295e0:
      return state([](Handle h) -> MenuState* { return new CreditsState(h); });
    case 0x488910: {  // the Pilot State: its screen made with it (adopted)
      const Handle h = alloc_block(0x14);
      menu_states_[h] = new PilotState(*this, h);
      return h;
    }
    case 0x493260:  // the Multiplayer State's first screen, new(0x13b04): the connection
      return screen(0x13b04, [](GameEngine& e, Handle s) -> MenuScreen* { return new ConnectScreen(e, s); });
    case 0x495f90:  // its second, new(0x1505c): the lobby
      return screen(0x1505c, [](GameEngine& e, Handle s) -> MenuScreen* { return new LobbyScreen(e, s); });
    case 0x49bd80:  // its third, new(0x11f3c): the new team's dialog
      return screen(0x11f3c, [](GameEngine& e, Handle s) -> MenuScreen* { return new TeamScreen(e, s); });
    case 0x488bc0:  // the Action State's screen, new(0x17730)
      return screen(0x17730, [](GameEngine& e, Handle s) -> MenuScreen* { return new ActionScreen(e, s); });
    case 0x48a6c0:
      return state([](Handle h) -> MenuState* { return new GameMenuState(GameMenuState::kAction, h); });
    case 0x48aef0:
      return state([](Handle h) -> MenuState* { return new GameMenuState(GameMenuState::kCampaign, h); });
    case 0x48b120:
      return state([](Handle h) -> MenuState* { return new GameMenuState(GameMenuState::kTraining, h); });
    case 0x49c900:
      return state([](Handle h) -> MenuState* { return new GameMenuState(GameMenuState::kMultiplayer, h); });
    case 0x47b430: {  // the debriefing, new(0x13bf4): the Win State, operation, mission, its +0x10
      const Handle st = a0;
      int32_t op = 0, mission = 0;
      if (args.size() >= 3) {
        op = static_cast<int32_t>(args.begin()[1].v);
        mission = static_cast<int32_t>(args.begin()[2].v);
      }
      States& ss = states();
      const uint8_t* won = st == ss.end_win.addr() ? &ss.end_win.campaign_won_ : &ss.win.campaign_won_;
      const Handle h = alloc_block(0x13bf4);
      screens_[h] = {new DebriefScreen(*this, st, op, mission, won), 0x13bf4};
      widen_screen(screens_[h].screen, ctor);
      return h;
    }
    case 0x47d3b0:  // the salvage screen, new(0x166a8)
      return screen(0x166a8, [](GameEngine& e, Handle s) -> MenuScreen* { return new SalvageScreen(e, s); });
    case 0x492750:  // the pause screen, new(0x11b20)
      return screen(0x11b20, [](GameEngine& e, Handle s) -> MenuScreen* { return new PauseScreen(e, s); });
    case 0x44b920: {  // the mech lab's screen, new(0x1b468): the lance's slot count
      const Handle h = alloc_block(0x1b468);
      screens_[h] = {new LabScreen(*this, static_cast<int32_t>(a0)), 0x1b468};
      widen_screen(screens_[h].screen, ctor);
      return h;
    }
    case 0x405230: {  // the briefing, new(0x12e6c): its section's name
      const char* name = args.size() && args.begin()->s ? args.begin()->s : "default";
      const Handle h = alloc_block(0x12e6c);
      auto* b = new BriefingScreen(*this, name, false);
      widen_screen(b, ctor);  // its script's objects are made with it (read_script)
      b->draw_first();        // its first frame once widened (the constructor's end)
      screens_[h] = {b, 0x12e6c};
      briefing_screen() = b;
      return h;
    }
    default:
      return Engine::construct(ctor, args);
  }
}

namespace {
// The ported shell states by address, the end screen's Win State too.
ShellState* shell_state_at(Handle state) {
  States& st = states();
  ShellState* const shells[] = {&st.main, &st.pause, &st.load, &st.win, &st.end_win, &st.salvage,
                                &st.briefing_salvage};
  for (ShellState* s : shells)
    if (s->addr() == state) return s;
  return nullptr;
}
}  // namespace

// Mech3.exe 1.2 @ 0x0048dfe0 on new(0x12688): the mission's quit box.
Handle GameEngine::message_box_new(const char* file, const char* name) {
  const Handle h = alloc_block(0x12688);
  delete quit_box_;
  quit_box_ = new QuitBox(*this, file, name);
  quit_box_at_ = h;
  return h;
}
void GameEngine::message_box_quit_prompt(Handle box) {
  if (quit_box_ && box == quit_box_at_) quit_box_->prompt();
}

bool GameEngine::menu_vcall(Handle obj, uint32_t slot, std::initializer_list<Arg> args, uint32_t* out) {
  const uint32_t a0 = args.size() ? args.begin()->v : 0;
  if (quit_box_ && obj == quit_box_at_) {  // the quit box: its frame, on or off, its deleting destructor
    *out = 0;
    switch (slot) {
      case 0x00:
        quit_box_->keys();  // ours (W46)
        if (quit_box_) quit_box_->update(zgame::from_bits(a0));
        return true;
      case 0x04: quit_box_->set_active(static_cast<int32_t>(a0)); return true;
      case 0x08:
        delete quit_box_;
        quit_box_ = nullptr;
        quit_box_at_ = 0;
        return true;
      default: break;
    }
  }
  if (ShellState* sh = shell_state_at(obj); sh && slot == 0x28) {  // the pause screen's RESUME
    *out = 0;
    sh->escape();
    return true;
  }
  MenuState* st = menu_state_of(obj);
  if (!st) st = popup_state_of(obj);
  if (st) {
    *out = 0;
    switch (slot) {
      case 0x00:  // the deleting destructor (the popups' states are their screen's)
        if (menu_states_.erase(obj)) delete st;
        return true;
      case 0x28: st->escape(); return true;
      default: break;
    }
    char key[48];
    std::snprintf(key, sizeof key, "menu state vt+0x%x", slot);
    unported(key, {});
    return true;
  }
  uint32_t off = 0;
  MenuScreen* s = screen_of(obj, &off);
  if (!s) return false;
  *out = 0;
  if (off == 0) {
    switch (slot) {
      case 0x00: s->update(zgame::from_bits(a0)); return true;
      case 0x04: s->set_active(static_cast<int32_t>(a0)); return true;
      case 0x08:  // the deleting destructor
        if (main_menu() == s) poke32(0x609854, 0);
        screens_.erase(obj);
        delete s;
        return true;
      case 0x10: s->apply(static_cast<int32_t>(a0)); return true;
      case 0x18: *out = s->set_busy(static_cast<uint8_t>(a0)); return true;
      case 0x1c: *out = s->busy(); return true;
      default: break;
    }
  } else if (auto* m = dynamic_cast<MainMenu*>(s)) {
    if (MainMenuButton* b = m->button_at(off)) {
      if (slot == 0x30) {
        b->on_left_up();
        return true;
      }
    }
  }
  char key[48];
  std::snprintf(key, sizeof key, "menu screen +0x%x vt+0x%x", off, slot);
  unported(key, {});
  return true;
}

void GameEngine::delete_screens() {
  delete quit_box_;
  quit_box_ = nullptr;
  quit_box_at_ = 0;
  // The app may be gone: the screens go below, not through the states' destructors.
  while (!menu_states_.empty()) {
    MenuState* st = menu_states_.begin()->second;
    menu_states_.erase(menu_states_.begin());
    st->screen_ = 0;
    delete st;
  }
  // A screen's own popups go with it (their states delete them).
  std::vector<MenuScreen*> top;
  for (auto& [h, s] : screens_)
    if (!s.owned && s.screen) top.push_back(s.screen);
  for (MenuScreen* s : top) {
    if (main_menu() == s) poke32(0x609854, 0);
    delete s;
  }
  screens_.clear();
}


Handle GameEngine::state_screen(Handle state) {
  if (ShellState* s = shell_state_at(state)) return s->screen_;
  if (MenuState* st = menu_state_of(state)) return st->screen_;
  if (MenuState* st = popup_state_of(state)) return st->screen_;
  return Engine::state_screen(state);
}

void GameEngine::credits_restart(Handle screen) {
  if (auto* c = dynamic_cast<CreditsScreen*>(screen_of(screen))) c->clock = 0.0f;
  else Engine::credits_restart(screen);
}

void GameEngine::screen_refresh(Handle h) {
  if (MenuScreen* s = screen_of(h)) s->dirty_all();
  else Engine::screen_refresh(h);
}
void GameEngine::screen_hide(Handle h) {
  if (MenuScreen* s = screen_of(h)) s->restore_behind();
  else Engine::screen_hide(h);
}
void GameEngine::twod_reset(Handle h) {
  if (MenuScreen* s = screen_of(h)) s->unload();
  else Engine::twod_reset(h);
}
void GameEngine::twod_set_cursor(uint32_t v) { ztwod::set_page_flip(v != 0); }

// 0x0048ee00 on the state the address names.
void GameEngine::shell_clear(uint32_t state) {
  if (ShellState* s = shell_state_at(state)) {
    s->clear_screen();
    return;
  }
  MenuState* m = menu_state_of(state);
  if (!m) m = popup_state_of(state);
  if (m) {
    m->clear_screen();
    return;
  }
  Engine::shell_clear(state);
}

// App::Push of a menu object: the ported ones are pushed; any other is logged.
void GameEngine::push_state(Handle state, uint32_t arg) {
  MenuState* st = menu_state_of(state);
  if (!st) st = popup_state_of(state);
  if (st) {
    app().push(st, arg);
    return;
  }
  char key[40];
  std::snprintf(key, sizeof key, "0x00590400 (menu state %#x)", state);
  unported(key, {state, arg});
}

// ---- the pilot (mech3/pilot) ----

void GameEngine::action_tally() { program_action_tally(); }

void GameEngine::action_open(Handle screen) {
  if (auto* a = dynamic_cast<ActionScreen*>(screen_of(screen))) a->reset();
  else Engine::action_open(screen);
}

void GameEngine::pilot_state_set_mode(Handle state, int32_t mode) {
  if (auto* p = dynamic_cast<PilotState*>(menu_state_of(state))) p->set_mode(mode);
  else Engine::pilot_state_set_mode(state, mode);
}

// [0x0060861c] as a handle of the pilot object, for the parts that read it
// through the module memory (the mission's, not yet bridged: they see a
// pilot, not its games).
void GameEngine::pilot_chosen(Pilot* p) { poke32(0x60861c, handle_of(p)); }

// 0x004785f0 at the app's stop: every pilot destroyed, so saved.
void GameEngine::pilots_delete() {
  current_pilot() = nullptr;
  delete_other_pilots();
}
void GameEngine::pilot_delete(const char* name) { delete_pilot(name); }

// 0x00478f10 on the pilot the screen chose (the Win State passes 1, the launch
// 0); with none (a debug start's stand-in), the call is logged.
void GameEngine::pilot_record_mission(Handle p, int32_t a) {
  if (!program_record_mission(a != 0)) Engine::pilot_record_mission(p, a);
}

bool program_pilot_file(int op) {
  Pilot* p = current_pilot();
  if (!p) return false;
  switch (op) {
    case 0: return p->open();
    case 1: return p->write_header();
    case 2: return p->write();
    default: p->close(); return true;
  }
}
bool program_record_mission(bool next) {
  Pilot* pl = current_pilot();
  if (pl) pl->record_mission(next);
  return pl != nullptr;
}

namespace {
// The pilot module's calls out in the program: the message box is the
// engine's; the loadouts are the ported Config's; zSave is the port's.
struct ProgramPilotHooks : PilotHooks {
  GameEngine* engine = nullptr;
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return engine ? engine->unported(key, args) : 0;
  }
  int32_t message_box(int32_t type, int32_t caption, int32_t text) override {
    return engine ? static_cast<int32_t>(engine->message_box(type, caption, text)) : 0;
  }
  Store* mission_salvage() override { return &mech3::mission_salvage(); }  // [0x00609990]
};
ProgramPilotHooks g_pilot_hooks;
}  // namespace

void wire_pilot_hooks(GameEngine* e) {
  g_pilot_hooks.engine = e;
  set_pilot_hooks(e ? &g_pilot_hooks : nullptr);
}

uint32_t GameEngine::message_box1(int32_t a, int32_t caption, int32_t text, const char* s) {
  return message_box3(a, caption, text, s, "", "");
}

// ---- the briefing (mech3/briefing, campaign.md section 7, item 5) ----

bool launch_player();  // 0x00491150 (mech3/mission_launch.h, whose mech headers clash with pilot.h)

namespace {
BriefingScreen* briefing_of(const GameEngine& e, Handle s) { return dynamic_cast<BriefingScreen*>(e.screen_of(s)); }

// 0x00435350 (a campaign's vt+0x54): the mission directory, c4b past Op 4 Mission 3.
int32_t campaign_dir(const CampaignGame& c) {
  switch (c.operation()) {
    case 1: return 0;
    case 2: return 1;
    case 3: return 2;
    case 4: return (3 < c.mission_number()) + 3;
    default: return -1;
  }
}
}  // namespace

// The Load state from the menus: its game is the chosen pilot's (the
// launch's stand-in filled from it; a debug start keeps its own). A
// campaign's, training's, Instant Action's and multiplayer's missions.
void GameEngine::load_state_enter() {
  if (launch_.active && launch_.auto_launch) return;
  Pilot* p = current_pilot();
  Game* g = p ? p->current : nullptr;
  Launch l;
  l.auto_launch = false;
  l.keep_pilot = true;
  l.type = g ? g->type() : -1;
  if (auto* c = dynamic_cast<CampaignGame*>(g)) {
    l.number = c->mission_number();
    l.op = c->operation();
    l.index = campaign_dir(*c);
  } else if (auto* t = dynamic_cast<TrainingGame*>(g)) {
    l.number = t->mission;
    l.index = 5;  // t1
  } else if (auto* a = dynamic_cast<ActionGame*>(g)) {
    // The Action screen's (menus.md 13): vt+0x54 the world (0x00437890), vt+0x4c
    // the ia mission (0x004378f0), vt+0x48 the world as 1..4 (0x004378b0).
    l.index = a->x74;
    // Ours: a mod's world past the known ones (the mod since gone): c1. No
    // world at all (-1) stays as it is: no launch, as in the original.
    if (l.index >= 9 && l.index >= mission_dir_count()) {
      std::printf("mw3: Instant Action's world %d is gone (a mod no longer enabled?): c1 instead\n", l.index);
      l.index = 0;
    }
    l.number = a->x78;
    l.op = a->x74 >= 0 && a->x74 <= 3 ? a->x74 + 1 : 0;
  } else if (auto* m = dynamic_cast<MultiplayerGame*>(g)) {
    // The lobby's world (0x0049b790): vt+0x54 the folder, vt+0x4c the mp
    // number, vt+0x48 0 (0x0040bae0); UNAVAILABLE is folder -1.
    l.index = m->x90;
    l.number = m->x94;
    l.op = 0;
    l.multi = m;
  } else {
    l.index = -1;
  }
  if (l.index < 0) {
    // No game, or no world: back to the main menu rather than a
    // mission left from before.
    unported("the Load state of this game", {l.type});
    launch_.active = false;
    replace_state(2);
    return;
  }
  l.mech = g->slot.mech;
  l.config = g->slot.config ? handle_of(g->slot.config) : 0;  // the slot's, alive while its game is
  l.skin = g->slot.skin;
  set_launch(l);
}

// The briefing's game texts (game_type, db_name, scenario_name,
// scenario_desc: the game's vt+0x04 .. +0x10). A debug start has no pilot's
// game: the one its launch names answers (ours).
const char* GameEngine::game_text(int32_t slot) {
  Pilot* p = launch_.auto_launch ? nullptr : current_pilot();
  if (Game* g = p ? p->current : nullptr) return g->game_text(slot);
  if (launch_.action) return launch_.action->game_text(slot);
  if (launch_.index == 5) return TrainingGame::text_of(slot, launch_.number);  // t1
  if (slot != 0x0c) return nullptr;
  static std::string name;  // 0x00434900's
  name = pilot_hooks().format_ints(0x21d, {launch_.op, launch_.number});
  return name.c_str();
}

bool GameEngine::launch_slot_refresh() {
  Pilot* p = current_pilot();
  Game* g = p ? p->current : nullptr;
  if (!g) return false;
  const Handle config = g->slot.config ? handle_of(g->slot.config) : 0;
  if (launch_.mech == g->slot.mech && launch_.config == config && launch_.skin == g->slot.skin) return false;
  launch_.mech = g->slot.mech, launch_.config = config, launch_.skin = g->slot.skin;
  return true;
}

// Mech3.exe 1.2 @ 0x0040a820: the app's movies-on flag (+0x1c1).
uint8_t GameEngine::briefing_has_movie() { return app().fmv(); }

// Mech3.exe 1.2 @ 0x00409320 (ECX the Load state): its movie (+0x18) given
// the main window and fmv.zrd's "c<op>m<n>" of the pilot's game (0x004099c0),
// played when it loads: the mission's briefing movie, an operation's opener
// at its first mission. Nothing shows until zFMV is ported.
void GameEngine::briefing_play_movie(uint32_t state) {
  if (state != states().load.addr()) return Engine::briefing_play_movie(state);
  const Handle m = states().load.movie_;
  const Handle game = pilot_game(pilot());
  char name[32];
  std::snprintf(name, sizeof name, "c%dm%d", static_cast<int32_t>(vcall(game, 0x48, {})),
                static_cast<int32_t>(vcall(game, 0x4c, {})));
  const Handle frame = app().frame_;
  if (const Handle hwnd = frame ? frame_hwnd(frame) : 0) movie_set_window(m, hwnd);  // 0x0040a7d0
  if (movie_load(m, "fmv.zrd", name) != -1) movie_play(m);
}

void GameEngine::briefing_script_start(Handle s) {
  if (BriefingScreen* b = briefing_of(*this, s)) b->script.start();
  else Engine::briefing_script_start(s);
}

// Mech3.exe 1.2 @ 0x00409ec0 / 0x00409eb0: no cursor while built, then the screen's own.
void GameEngine::briefing_init(Handle s) {
  if (BriefingScreen* b = briefing_of(*this, s)) b->set_cursor(nullptr);
  else Engine::briefing_init(s);
}
void GameEngine::briefing_start(Handle s) {
  if (BriefingScreen* b = briefing_of(*this, s)) b->set_cursor(&b->cursor_obj);
  else Engine::briefing_start(s);
}

// 0x00405160 stood in for: the briefing's MAP image (images\maps) as the
// dialog's first image, at the origin behind the rest, and the mission's map
// (0x004582a0 into [0x006098d0]+0xbc through 0x0040a0e0: the radar's and the
// waypoints'; mission_map.h).
void GameEngine::briefing_map_image(ztwod::Dialog* d, zrdr::Node* section) {
  ztwod::Host& h = ztwod::host();
  h.image_path("..\\data\\common\\images\\maps");
  const char* name = zrdr::get_string(section, "MAP");
  briefing_pick_map(name);
  zimg::Texture* t = name ? h.image_get(name) : nullptr;
  if (!d || !t) return;
  ztwod::Image& im = d->images[0];
  d->add(&im);
  im.show_for(-1.0f);
  im.set_pos(0, 0);
  im.set_image(t);
  im.flags = ztwod::kManaged | (im.flags & ztwod::kHidden);  // 0x00409ac0(2)
  im.dirty();
}

// 0x004582a0 on the MAP name (the maps' images from images\maps, the path
// already set), into the missions object's +0xbc (0x0040a0e0).
void GameEngine::briefing_pick_map(const char* name) {
  MissionMap* m = maps_pick(name);
  missions().xbc = m ? handle_of(m) : 0;
}

void program_briefing_progress(float f) {
  if (BriefingScreen* b = briefing_screen()) b->progress(f);
}
uint32_t program_slot_count() {
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  return c ? static_cast<uint32_t>(c->slot_count()) : 0;
}
// 0x00479440's +0xa8, the field bases left. The debug launch's campaign has
// no game behind it: a new campaign's three.
void* program_campaign() { return current_pilot() ? current_pilot()->campaign() : nullptr; }  // 0x00479440
int32_t program_campaign_bases() {
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  return c ? c->a8 : 3;
}
void program_briefing_line(const char* s) {
  if (BriefingScreen* b = briefing_screen()) b->log(s);
}

// 0x00479440 on the pilot the launch filled: its game's stand-in.
Handle GameEngine::pilot_campaign(Handle p) {
  if (p && p == pilot_ && launch_.type == kCampaign) return game_;
  return Engine::pilot_campaign(p);
}

// Mech3.exe 1.2 @ 0x0047ae10 (CL a, DL b): a failed mission's salvage merged
// back (0x00434cb0); a won one's: the player's mech into the campaign's
// +0x64 store, with b its allies' into the salvage store (zEntity's list,
// 0x00472f00 each: mech_salvage.cpp).
void GameEngine::mission_results(uint8_t a, uint8_t b) {
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  if (!c) return;
  if (a) c->merge_salvage();
  else mech_lance_salvage(c->store64, b != 0);
}

// The results' lance from the ported mechs; a lancemate's name is its
// brain's (0x004aee70), "" ([0x005f4c90]) without one.
const char* program_pilot_name() {
  Pilot* p = current_pilot();
  return p ? p->name : nullptr;
}

std::vector<Engine::LanceMech> GameEngine::debrief_lance() {
  std::vector<LanceMech> r;
  for (const DebriefMech& d : mech_debrief_lance()) {
    LanceMech m;
    m.mech = handle_of(d.mech);
    m.player = d.player;
    m.team = d.team;
    m.type = d.type;
    for (int i = 0; i < 6; ++i) m.stats[i] = d.stats[i];
    const char* n = d.player ? program_pilot_name() : mech_brain_name(d.mech);  // 0x0041df30
    m.name = n ? n : "";
    r.push_back(m);
  }
  return r;
}
// [0x0060994c], [0x00609950]: the mission's primary and secondary lists.
std::vector<Engine::Objective> GameEngine::debrief_objectives(int32_t which) {
  std::vector<Objective> r;
  const Objectives& all = mission_objectives();
  const ObjectiveGroup* g = which == 0 ? all.primary.get() : all.secondary.get();
  if (!g) return r;
  for (const auto& o : g->items) r.push_back({handle_of(o.get()), o->done != 0, o->visible != 0});
  return r;
}
// Mech3.exe 1.2 @ 0x0045e080 (with 0): the description, without the nav point's name.
const char* GameEngine::objective_text(Handle obj) {
  const auto* o = static_cast<const mech3::Objective*>(object_of(obj));
  return o && o->desc ? o->desc->c_str() : nullptr;
}
uint32_t GameEngine::player_allies() { return mech_player_allies(); }
bool GameEngine::vehicle_armor(Handle v, LanceArmor* a) {
  return mech_lance_armor(object_of(v), &a->type, a->armor, a->left);
}
const char* mech_name(int32_t mech);  // 0x00484650 (mech_names.h, whose engine table clashes with Engine)
const char* GameEngine::mech_name_of(int32_t mech) { return mech_name(mech); }

// 0x0047ac20's copy, from the ported screen into the pilot's campaign.
void GameEngine::win_keep_scores(Handle screen, int32_t n) {
  auto it = screens_.find(screen);
  auto* d = it != screens_.end() ? dynamic_cast<DebriefScreen*>(it->second.screen) : nullptr;
  CampaignGame* c = current_pilot() ? current_pilot()->campaign() : nullptr;
  if (!d || !c) return Engine::win_keep_scores(screen, n);
  // n is the campaign's slots, at most the lance's four cards (+0x13be4 has
  // four). The exe writes every card's number, 0 for a card without a
  // record line. FIX (bug), behind `bugs`: a card without one keeps the
  // record, else a lancemate not on that card loses their career kills.
  const bool fix = zfix::options().bugs;
  for (int32_t i = 0; i < n && i < 4; ++i)
    if (!fix || d->lance_scored[i]) c->b4[i] = d->lance_scores[i];
  c->c4 = d->score;
}

bool GameEngine::missions_loaded() { return missions().finished(); }
// The launch 0x00491150 ends by emptying the missions object's +0xc0 store
// (mission_launch.cpp walks it through memory); here the ported store.
bool GameEngine::mission_launch() {
  const bool ok = launch_player();
  if (ok) mission_salvage().clear();
  return ok;
}
Handle GameEngine::mission_salvage_store() { return store_handle(&mission_salvage()); }
void GameEngine::briefing_refresh(Handle) {
  if (BriefingScreen* b = briefing_screen()) b->refresh();
}
void GameEngine::briefing_show(Handle, int32_t a) {
  if (BriefingScreen* b = briefing_screen()) b->show_realloc(a != 0);
}
// Mech3.exe 1.2 @ 0x0047d2f0
void GameEngine::salvage_open() { app().push(&states().briefing_salvage, 0); }

void GameEngine::mech_lab_open(Handle store, int32_t a) {
  mech3::mech_lab_open(pilot(), store, static_cast<uint8_t>(a));
}
int32_t GameEngine::lab_slot_count(Handle) { return game_slot_count(); }
Handle GameEngine::lab_slot(Handle, int32_t i) { return handle_of(game_slot(i)); }
void GameEngine::lab_slot_release(Handle, int32_t i) {
  if (GameSlot* s = game_slot(i)) s->x0c = 0;
}
LabScreen* GameEngine::lab_screen(Handle h) { return dynamic_cast<LabScreen*>(screen_of(h)); }
void GameEngine::lab_show_slot(Handle, int32_t slot) { states().lab.show_slot(slot); }
void GameEngine::lab_page(Handle h, int32_t page) {
  if (LabScreen* s = lab_screen(h)) s->switch_page(page);
}
int32_t GameEngine::lab_slot_mech(Handle slot) {
  const auto* s = static_cast<const GameSlot*>(object_of(slot));
  return s ? s->mech : -1;
}
void GameEngine::lab_slot_drop_config(Handle slot, Handle store) {
  if (auto* s = static_cast<GameSlot*>(object_of(slot))) s->set_config(nullptr, store_of(store), true);
}
void GameEngine::lab_slot_set_mech(Handle slot, int32_t mech) {
  if (auto* s = static_cast<GameSlot*>(object_of(slot))) s->mech = mech;
}
int32_t GameEngine::lab_slot_skin(Handle slot) {
  const auto* s = static_cast<const GameSlot*>(object_of(slot));
  return s ? s->skin : 0;
}
void GameEngine::lab_slot_set_skin(Handle slot, int32_t skin) {
  if (auto* s = static_cast<GameSlot*>(object_of(slot))) s->skin = skin;
}
Handle GameEngine::lab_slot_cfg(Handle slot) {
  const auto* s = static_cast<const GameSlot*>(object_of(slot));
  return s ? handle_of(s->config) : 0;
}
int32_t GameEngine::config_mech(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->mech : -1;
}
uint8_t GameEngine::lab_config_modified(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->modified : 0;
}
std::string GameEngine::lab_config_name(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c && c->name ? c->name : "";
}
void GameEngine::lab_config_recount(Handle config) {
  if (auto* c = static_cast<Config*>(object_of(config))) c->recount();
}
// (The exe reads them unchecked: the lists ask only with a loadout shown.)
int32_t GameEngine::lab_config_slots_left(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->slots_left() : 0;
}
int32_t GameEngine::lab_config_ferro_slots(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->ferro_slots() : 0;
}
int32_t GameEngine::lab_config_endo_slots(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->endo_slots() : 0;
}
int32_t GameEngine::lab_config_sink_items(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->sink_items() : 0;
}
int32_t GameEngine::lab_config_sink_limit(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->sink_limit() : 10;
}
float GameEngine::lab_config_weight(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? static_cast<float>(c->weight(-1)) : 0.0f;
}
int32_t GameEngine::lab_config_weapons(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->weapon_count() : 0;
}
int32_t GameEngine::lab_config_equipment(Handle config, int32_t kind, int32_t tech) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->equipment_count(kind, tech) : 0;
}
int32_t GameEngine::lab_config_part_free(Handle config, int32_t part) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c && part >= -1 && part < 9 ? c->free_slots_in(part) : 0;
}
float GameEngine::lab_config_armor_tons(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->armor_tons : 0.0f;
}
int32_t GameEngine::lab_config_armor_tech(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->armor_tech : 0;
}
uint8_t GameEngine::lab_config_armor_ferro(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->ferro : 0;
}
std::vector<Engine::LabFirepoint> GameEngine::lab_dfn_firepoints(Handle dfn) {
  std::vector<LabFirepoint> out;
  const auto* d = static_cast<const MechDfn*>(object_of(dfn));
  if (d && d->firepoints)
    for (const Firepoint* f : *d->firepoints) out.push_back({f ? f->part : -1, f ? f->radius : 0.0f});
  return out;
}
Config* GameEngine::lab_config_of(Handle config) { return static_cast<Config*>(object_of(config)); }
void GameEngine::lab_message(const char* text) {
  if (LabScreen* s = lab_screen(states().lab.lab_screen_)) s->message.set_text("%s", text);  // (the exe's format is the text)
}
int32_t GameEngine::lab_assigned_part() {
  LabScreen* s = lab_screen(states().lab.lab_screen_);
  return s ? s->assigned.part() : -1;
}
void GameEngine::lab_put_on(LabUnassignedList& list, int32_t index) { mech3::lab_put_on(*this, list, index); }
void GameEngine::lab_take_off(LabAssignedList& list, int32_t index) { mech3::lab_take_off(*this, list, index); }
std::vector<Handle> GameEngine::lab_config_items(Handle config) {
  std::vector<Handle> out;
  if (const auto* c = static_cast<const Config*>(object_of(config)))
    for (const ConfigItem* it : c->items) out.push_back(item_handle(it));
  return out;
}
int32_t GameEngine::lab_item_location(Handle item, int32_t mech) {
  const ConfigItem* it = item_of(item);
  return it ? it->location(mech) : -1;
}
std::string GameEngine::lab_engine_name(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? engine_name(c->engine, c->xl, [this](int32_t id) { return message(id); }) : std::string();
}
int32_t GameEngine::lab_config_engine(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->engine : 0;
}
void GameEngine::lab_config_set_engine(Handle config, int32_t index) {
  if (auto* c = static_cast<Config*>(object_of(config))) c->engine = index;
}
void GameEngine::lab_config_set_xl(Handle config, uint8_t v) {
  if (auto* c = static_cast<Config*>(object_of(config))) c->xl = v;
}
int32_t GameEngine::lab_config_engine_slots(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->engine_slots() : 0;
}
int32_t GameEngine::lab_dfn_engine_min(Handle dfn) {
  const auto* d = static_cast<const MechDfn*>(object_of(dfn));
  return d ? d->get<int32_t>(0x10) : 0;
}
int32_t GameEngine::lab_dfn_engine_max(Handle dfn) {
  const auto* d = static_cast<const MechDfn*>(object_of(dfn));
  return d ? d->get<int32_t>(0x14) : 0;
}
float GameEngine::lab_config_weight_of(Handle config, int32_t what) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? static_cast<float>(c->weight(what)) : 0.0f;
}
void GameEngine::lab_config_set_endo(Handle config, uint8_t v) {
  if (auto* c = static_cast<Config*>(object_of(config))) c->endo_steel = v;
}
int32_t GameEngine::lab_config_section(Handle config, int32_t part) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c && part >= 0 && part < 11 ? c->armor[part] : 0;
}
void GameEngine::lab_config_set_section(Handle config, int32_t part, int32_t v) {
  if (auto* c = static_cast<Config*>(object_of(config)); c && part >= 0 && part < 11) c->armor[part] = v;
}
void GameEngine::lab_config_spread_armor(Handle config) {
  if (auto* c = static_cast<Config*>(object_of(config))) c->spread_armor(c->mech);
}
void GameEngine::lab_config_remove(Handle config, Handle item, int32_t n) {
  auto* c = static_cast<Config*>(object_of(config));
  if (c) c->remove(const_cast<ConfigItem*>(item_of(item)), n);
}
void GameEngine::lab_store_add_equipment(Handle store, int32_t kind, int32_t tech, int32_t n) {
  if (Store* s = store_of(store)) s->add_equipment(kind, tech, n);
}
int32_t GameEngine::lab_config_sinks(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->sinks : 0;
}
uint8_t GameEngine::lab_config_double_sinks(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? c->double_sinks : 0;
}
bool GameEngine::lab_config_valid(Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c && c->validate(false);
}
Handle GameEngine::mech_dfn_handle(int32_t mech) { return handle_of(mech3::mech_dfn(mech)); }
void GameEngine::lab_dfn_skins(Handle dfn, std::vector<LabSkin>* out) {
  out->clear();
  const auto* d = static_cast<const MechDfn*>(object_of(dfn));
  if (!d || !d->skins) return;
  for (Skin* s : d->skins->skins) out->push_back({handle_of(s), s && s->name, s && s->name ? s->name : ""});
}
Handle GameEngine::lab_dfn_configs(Handle dfn) {
  const auto* d = static_cast<const MechDfn*>(object_of(dfn));
  return d ? handle_of(d->configs) : 0;
}
std::vector<Handle> GameEngine::lab_list_configs(Handle list) {
  std::vector<Handle> out;
  if (const auto* l = static_cast<const ConfigList*>(object_of(list)))
    for (Config* c : l->configs) out.push_back(handle_of(c));
  return out;
}
void GameEngine::lab_list_append(Handle list, Handle config) {
  if (auto* l = static_cast<ConfigList*>(object_of(list))) l->configs.push_back(static_cast<Config*>(object_of(config)));
}
bool GameEngine::lab_list_remove(Handle list, Handle config) {
  auto* l = static_cast<ConfigList*>(object_of(list));
  return l && l->remove(static_cast<const Config*>(object_of(config)));
}
void GameEngine::lab_list_save(Handle list) {
  if (auto* l = static_cast<ConfigList*>(object_of(list))) l->save_all();
}
Handle GameEngine::lab_config_copy(const char* name, Handle config) {
  const auto* c = static_cast<const Config*>(object_of(config));
  return c ? handle_of(config_copy_store(name, *c, nullptr, false)) : 0;
}
void GameEngine::lab_config_delete(Handle config) { delete static_cast<Config*>(object_of(config)); }
void GameEngine::lab_config_set_name(Handle config, const char* name) {
  if (auto* c = static_cast<Config*>(object_of(config))) {
    std::free(c->name);
    c->name = name ? strdup(name) : nullptr;
  }
}
void GameEngine::lab_config_set_modified(Handle config, uint8_t v) {
  if (auto* c = static_cast<Config*>(object_of(config))) c->modified = v;
}
uint8_t GameEngine::lab_config_byte(Handle config, uint32_t off) {
  const auto* c = static_cast<const Config*>(object_of(config));
  if (!c) return 0;
  switch (off) {
    case 0x1c: return c->player_made;
    case 0x1e: return c->training;
    case 0x1f: return c->modified;
    case 0x20: return c->endo_steel;
    case 0x28: return c->xl;
    case 0x34: return c->ferro;
    default: return 0;
  }
}
bool GameEngine::lab_store_has_config(Handle store, int32_t mech, Handle other, Handle config) {
  Store* s = store_of(store);
  return s && s->has_config(mech, static_cast<const Config*>(object_of(other)),
                            static_cast<const Config*>(object_of(config)));
}
uint8_t GameEngine::lab_store_full(Handle store) {
  const Store* s = store_of(store);
  return s ? s->full : 0;
}
void GameEngine::lab_slot_set_config(Handle slot, Handle config, Handle store) {
  if (auto* s = static_cast<GameSlot*>(object_of(slot)))
    s->set_config(static_cast<const Config*>(object_of(config)), store_of(store), true);
}
void GameEngine::config_export(Handle config) {
  if (const auto* c = static_cast<const Config*>(object_of(config))) lab_export_config(*c);
}
Handle GameEngine::config_import() { return handle_of(lab_import_config()); }
void GameEngine::lab_page_refresh(Handle screen) {
  if (LabScreen* s = lab_screen(screen)) s->refresh();
}
Handle GameEngine::lab_slot_config(Handle, int32_t i) {
  GameSlot* s = game_slot(i);
  return s ? handle_of(s->config) : 0;
}
std::string GameEngine::message_format_sd(int32_t id, const char* s, int32_t n, bool has_n) {
  return has_n ? zutl::format_message(id, {s, std::to_string(n).c_str()}) : zutl::format_message(id, s);
}
std::string GameEngine::message_format_is(int32_t id, int32_t a, const char* s) {
  return zutl::format_message_f(id, a, s);
}
std::string GameEngine::message_format_isi(int32_t id, int32_t a, const char* s, int32_t b) {
  return zutl::format_message_f(id, a, s, b);
}
void GameEngine::mission_cleanup(int32_t unload) { missions().cleanup(unload != 0); }
void GameEngine::replace_state(int32_t n) { app().replace(app().state(n), 0); }

Handle GameEngine::voice_play(Handle s, float v) {
  auto* o = static_cast<zsnd::Object*>(object_of(s));
  return o ? handle_of(zsnd::play(o, v)) : 0;
}
// Mech3.exe 1.2 @ 0x0047abe0 (ECX the Win state): its voice (+0x1c) stopped,
// then the sound named at +0x18 ("c%dm%dwin", loaded by the state's enter)
// played at full volume into +0x1c, which the state's leave stops.
void GameEngine::debrief_voice_play(Handle state) {
  WinState& w = states().end_win;
  if (state != w.addr()) return Engine::debrief_voice_play(state);
  if (const Handle v = field32(state, 0x1c)) voice_stop(v);
  set_field32(state, 0x1c, 0);
  if (zsnd::Object* o = zsnd::find(w.voice_)) set_field32(state, 0x1c, handle_of(zsnd::play(o, 1.0f)));
}
int32_t GameEngine::voice_status(Handle v) { return zsnd::status(static_cast<zsnd::VoiceBase*>(object_of(v))); }
void GameEngine::sound_cue_briefing(Handle s) {
  if (auto* o = static_cast<zsnd::Object*>(object_of(s)))
    zsnd::set_cue_fn(o, [](uint32_t marker, zsnd::Sound*) { briefing_cue(static_cast<int32_t>(marker)); });
}
uint8_t GameEngine::app_flag_1ba() { return app().byte_at(0x1ba); }
Handle GameEngine::mission_navs() { return missions().stations; }
void* program_nav_find(const char* name);  // app_vehicle.h
Handle GameEngine::nav_find(Handle navs, const char* name) {
  if (!navs) return 0;
  void* p = program_nav_find(name);  // the waynet behind [0x00609980] (missions().stations)
  return p ? handle_of(p) : 0;
}
Handle GameEngine::mission_map() { return handle_of(mech3::mission_map()); }
void program_waypoint_position(void* p, float out[3]);  // app_vehicle.h
bool GameEngine::map_project(Handle map, Handle nav, int32_t xy[2]) {
  xy[0] = xy[1] = 0;
  auto* m = static_cast<MissionMap*>(object_of(map));
  if (!m || !nav) return false;
  float at[3];
  program_waypoint_position(object_of(nav), at);
  return m->point(at, xy);
}
zimg::Texture* GameEngine::map_image(Handle map) {
  auto* m = static_cast<MissionMap*>(object_of(map));
  return m ? m->image : nullptr;
}
}  // namespace mech3

namespace mech3 {

// Mech3.exe 1.2 @ 0x0047fc40: the app's network on or off. On: zNetwork up
// (0x00586ba0; FIX (A): on the platform's transport, not DirectPlay's
// instance), unless it is already (a --host or --join start's); the lobby
// launch's set-up (+0x19c) is not ported. Off: the network game ended
// (0x0046d100), zNetwork down (0x00586c20), the transport given up.
void GameEngine::network_set(int32_t on) {
  if (on) {
    if (network_on_) return;
    if (!znet::session().initialised && p_.network_open)
      if (znet::Transport* t = p_.network_open()) znet::initialise(t);
    network_on_ = true;
    return;
  }
  if (!network_on_) return;
  net_end();
  znet::shutdown();
  if (p_.network_close) p_.network_close();
  network_on_ = false;
}

// Mech3.exe 1.2 @ 0x0049cb20: the Multiplayer State's page shown as wanted
// (1 the connection screen, 2 the lobby, 3 the new team), each told when
// it starts over (+0x19): the connection screen forgets its kind
// (0x004949d0), the lobby enters (0x00497720), the new team's dialog
// enters over the lobby (0x0049c2c0).
void GameEngine::multiplayer_switch(Handle state) {
  auto it = menu_states_.find(state);
  auto* g = it == menu_states_.end() ? nullptr : dynamic_cast<GameMenuState*>(it->second);
  if (!g) return Engine::multiplayer_switch(state);
  if (g->want == 1) {
    g->screen_ = g->screens[0];
    if (g->x19)
      if (auto* c = dynamic_cast<ConnectScreen*>(screen_of(g->screens[0]))) c->forget_kind();
  } else if (g->want == 2) {
    g->screen_ = g->screens[1];
    if (g->x19)
      if (auto* l = dynamic_cast<LobbyScreen*>(screen_of(g->screens[1]))) {
        l->enter();
        // Ours: each entry counted, and what the scores show (the after-game checks).
        ++lobby_entries_;
        if (!quiet) {
          std::string t;
          for (const std::string& x : l->shown_scores) t += (t.empty() ? "" : " | ") + x;
          std::printf("mw3: lobby entered %d, scores: %s\n", lobby_entries_, t.empty() ? "none" : t.c_str());
        }
      }
  } else if (g->want == 3) {
    g->screen_ = g->screens[2];
    if (g->x19)
      if (auto* t = dynamic_cast<TeamScreen*>(screen_of(g->screens[2])))
        t->enter(dynamic_cast<LobbyScreen*>(screen_of(g->screens[1])));
  }
  if (!quiet && g->page != g->want) std::printf("mw3: multiplayer page %d\n", static_cast<int>(g->want));  // ours
  g->page = g->want;
}

// Mech3.exe 1.2 @ 0x0049cb00
void GameEngine::multiplayer_page(Handle state, int32_t page, uint8_t reenter) {
  auto it = menu_states_.find(state);
  auto* g = it == menu_states_.end() ? nullptr : dynamic_cast<GameMenuState*>(it->second);
  if (!g) return Engine::multiplayer_page(state, page, reenter);
  g->want = page;
  g->x19 = reenter;
}

// Mech3.exe 1.2 @ 0x0049cc30's: 0x00494920 on the connection screen with the
// pilot; the lobby's pilot and its current game (+0x15058, +0x116ec; 0x00479480).
void GameEngine::multiplayer_take_pilot(Handle first, Handle second) {
  Pilot* p = current_pilot();
  if (auto* c = dynamic_cast<ConnectScreen*>(screen_of(first))) c->take_pilot(p ? handle_of(p) : 0, p ? p->name : nullptr);
  else Engine::multiplayer_take_pilot(first, second);
  if (auto* l = dynamic_cast<LobbyScreen*>(screen_of(second))) l->take_pilot(p ? handle_of(p) : 0, p ? p->multiplayer() : nullptr);
}

// Mech3.exe 1.2 @ 0x0049b660 on the lobby: back from the mech lab.
void GameEngine::multiplayer_resume(Handle screen) {
  if (auto* l = dynamic_cast<LobbyScreen*>(screen_of(screen))) {
    l->resume();
    if (!quiet)  // ours: the mech the lab left in the game's slot (the tests)
      if (MultiplayerGame* g = current_pilot() ? current_pilot()->multiplayer() : nullptr)
        std::printf("mw3: lobby back from the lab, mech %d\n", static_cast<int>(g->slot.mech));
  } else {
    Engine::multiplayer_resume(screen);
  }
}

}  // namespace mech3
