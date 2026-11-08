// zTwoD's text field: the line editor, the polyline and its caret, the edit
// bar (docs/twod.md, The edit bar). Original file unknown: the polyline sits
// with the HUD primitives (0x005a3830..0x005a3a40), the editor and the bar
// after the interp console (0x005a46a0..0x005a5340), all between
// twod_wintext.cpp and zent_init.cpp.
#include <cstdlib>
#include <cstring>

#include "gamez/ztwod/twod.h"
#include "gamez/zvideo/zvid_format.h"

namespace ztwod {

namespace {
int32_t len_of(const char* s) { return static_cast<int32_t>(std::strlen(s)); }
}  // namespace

// ---- the line editor ----

// Mech3.exe 1.2 @ 0x005a46a0
LineEditor::LineEditor(int32_t n) {
  resize(n);
  std::memset(keys, 2, 0x20);        // control characters: nothing
  std::memset(keys + 0x20, 1, 0xe0);  // the rest: typed
  keys[0x1b] = 3;
  keys[0x0d] = 4;
  keys[0x08] = 5;
  keys[0x7f] = 6;
  keys[0x02] = 7;
  keys[0x06] = 8;
  keys[0x01] = 9;
  keys[0x04] = 10;
}

// Mech3.exe 1.2 @ 0x005a4720
LineEditor::~LineEditor() {
  delete[] buf;
  set_reject(nullptr);
}

// Mech3.exe 1.2 @ 0x005a4840
void LineEditor::set_reject(const char* chars) {
  if (reject) std::free(reject);
  reject = chars ? strdup(chars) : nullptr;
}

// Mech3.exe 1.2 @ 0x005a4740. FIX (bug): a shorter buffer took the old text's
// first bytes with no terminator when the text was longer; it is cut at the
// last byte here, as set_text cuts (docs/fixes.md).
void LineEditor::resize(int32_t n) {
  char* d = new char[static_cast<size_t>(n)];
  if (buf) {
    const int32_t keep = n < size ? n : size;
    std::strncpy(d, buf, static_cast<size_t>(keep));
    if (keep < n) std::memset(d + keep, 0, static_cast<size_t>(n - keep));  // new's bytes, zero here
    if (n > 0 && keep == n) d[n - 1] = 0;
    delete[] buf;
  } else {
    std::memset(d, 0, static_cast<size_t>(n));
  }
  buf = d;
  size = n;
}

// Mech3.exe 1.2 @ 0x005a47b0
void LineEditor::set_text(const char* s) {
  std::strncpy(buf, s, static_cast<size_t>(size));
  buf[size - 1] = 0;
  set_cursor(cursor);
}

// Mech3.exe 1.2 @ 0x005a4800
void LineEditor::set_cursor(int32_t pos) { cursor = pos < len_of(buf) ? pos : len_of(buf); }

// Mech3.exe 1.2 @ 0x005a4880: a refused character goes to on_reject; the map
// says what the others do.
int32_t LineEditor::key(int32_t ch) {
  if (reject && std::strchr(reject, ch & 0xff)) {
    on_reject(ch);
    return 0;
  }
  const int32_t action = keys[ch & 0xff];
  switch (action) {
    case 1: insert(ch); break;
    case 2: on_control(ch); break;
    case 3: on_escape(); break;
    case 4: on_enter(); break;
    case 5: backspace(); break;
    case 6: del(); break;
    case 7: left(); break;
    case 8: right(); break;
    case 9: home(); break;
    case 10: end(); break;
    default: break;
  }
  return action;
}

// Mech3.exe 1.2 @ 0x005a49a0
void LineEditor::insert(int32_t ch) {
  if (len_of(buf) < size - 1) {
    open_gap(1, cursor);
    buf[cursor] = static_cast<char>(ch);
    ++cursor;
    return;
  }
  on_full();
}

// Mech3.exe 1.2 @ 0x005a49f0
void LineEditor::backspace() {
  if (cursor > 0) {
    --cursor;
    close_gap(1, cursor);
  }
}

// Mech3.exe 1.2 @ 0x005a4a10
void LineEditor::del() { close_gap(1, cursor); }

// Mech3.exe 1.2 @ 0x005a4a20
void LineEditor::left() {
  if (cursor > 0) --cursor;
}

// Mech3.exe 1.2 @ 0x005a4a30
void LineEditor::right() {
  if (cursor < len_of(buf)) ++cursor;
}

// Mech3.exe 1.2 @ 0x005a4a60
void LineEditor::end() { cursor = len_of(buf); }

// Mech3.exe 1.2 @ 0x005a4a80: the text from `at` (and its terminator) moved n on.
bool LineEditor::open_gap(int32_t n, int32_t at) {
  int32_t i = len_of(buf) + n;
  if (i >= size) return false;
  for (; at < i; --i) buf[i] = buf[i - n];
  return true;
}

// Mech3.exe 1.2 @ 0x005a4ad0
void LineEditor::close_gap(int32_t n, int32_t at) {
  for (const int32_t len = len_of(buf); at < len; ++at) buf[at] = buf[at + n];
}

// ---- the polyline and the caret ----

// Mech3.exe 1.2 @ 0x005a3830
PolyLine::PolyLine() : Object2D(0, 0) { Object2D::dirty(); }

// Mech3.exe 1.2 @ 0x005a38a0
void PolyLine::set_point(int32_t i, int32_t px, int32_t py) {
  pts[i][0] = px;
  pts[i][1] = py;
  if (count <= i) count = i + 1;
  if (i == 0) set_pos(px, py);
  dirty();
}

// Mech3.exe 1.2 @ 0x005a38f0: within its clip rect (each line clipped), or
// with `round` within the circle across it, or the lines as they are.
void PolyLine::draw() {
  if (!can_present()) return;
  erase();
  if (count == 0) return;
  if (clip && !round) return host().line_list(&pts[0][0], count - 1, clip, color);
  for (int32_t i = 0; i < count - 1; ++i) {
    if (!round) {
      host().line(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], color);
      continue;
    }
    int32_t a[2], b[2];
    if (clip_round(*clip, pts[i], pts[i + 1], a, b)) host().line(a[0], a[1], b[0], b[1], color);
  }
}

// Mech3.exe 1.2 @ 0x005a4b10
Caret::Caret() {
  set_point(0, -1, 0);
  set_point(1, cw, 0);
  set_point(2, cw, 1);
  set_point(3, 0, 1);
  set_point(4, 0, ch - 1);
  set_point(5, cw, ch - 1);
  set_point(6, cw, ch);
  set_point(7, -cw, ch);
  set_point(8, -cw, ch - 1);
  set_point(9, 0, ch - 1);
  set_point(10, 0, 1);
  set_point(11, -cw, 1);
  set_point(12, -cw, 0);
}

// Mech3.exe 1.2 @ 0x005a4cf0: an I-beam at (px, py), w either side, h tall.
void Caret::place(int32_t px, int32_t py, int32_t w, int32_t h) {
  cx = px;
  cy = py;
  cw = w;
  ch = h;
  set_point(0, px - w, py);
  set_point(1, cw + cx, cy);
  set_point(2, cw + cx, cy + 1);
  set_point(3, cx, cy + 1);
  set_point(4, cx, ch - 1 + cy);
  set_point(5, cw + cx, ch - 1 + cy);
  set_point(6, cw + cx, ch + cy);
  set_point(7, cx - cw, ch + cy);
  set_point(8, cx - cw, ch - 1 + cy);
  set_point(9, cx, ch - 1 + cy);
  set_point(10, cx, cy + 1);
  set_point(11, cx - cw, cy + 1);
  set_point(12, cx - cw, cy);
}

// Mech3.exe 1.2 @ 0x005a4c90: blinking, the phase flips each period.
void Caret::update(float dt) {
  if (flags & kHidden) return;
  if (blink) {
    blink_timer -= dt;
    if (blink_timer < 0.0f) {
      phase = -phase;
      blink_timer = period;
    }
    if (phase != 1) return;
  }
  PolyLine::draw();
}

// ---- the edit bar ----

// Mech3.exe 1.2 @ 0x005a5320
void BarEditor::on_enter() { bar->on_return(); }
// Mech3.exe 1.2 @ 0x005a5340
void BarEditor::on_escape() { bar->on_cancel(); }

// Mech3.exe 1.2 @ 0x005a4ec0
EditBar::EditBar() {
  caret.show_for(-1.0f);
  editor.bar = this;
  Control::show_for(-1.0f);
}

// Mech3.exe 1.2 @ 0x005a4fb0
EditBar::~EditBar() { set_focus(0); }

// Mech3.exe 1.2 @ 0x005a5020
void EditBar::char_callback(int32_t ch, int32_t key, uintptr_t bar) {
  if (bar) reinterpret_cast<EditBar*>(bar)->on_char(ch, key);
}

// Mech3.exe 1.2 @ 0x005a5040
void EditBar::on_char(int32_t ch, int32_t) {
  if (numeric && !std::strchr("0123456789.-\x1b\r\b\x7f\x02\x06", ch)) return;
  editor.key(ch);
}

// Mech3.exe 1.2 @ 0x005a5090
void EditBar::set_focus(uint8_t on) {
  if (on == focus) return;
  focus = on;
  if (on) host().set_char_callback(&EditBar::char_callback, reinterpret_cast<uintptr_t>(this));
  else host().set_char_callback(nullptr, 0);
}

// Mech3.exe 1.2 @ 0x005a50d0
void EditBar::on_left_up() {
  clicked = 1;
  Control::on_left_up();
}

// Mech3.exe 1.2 @ 0x005a52a0. FIX (bug): a null text (the file list's select
// of -1, 0x0042f1c0) faulted in the original's strncpy and strlen; it is
// empty here (docs/fixes.md).
void EditBar::set_text(const char* s) {
  if (!s) s = "";
  editor.set_text(s);
  editor.set_cursor(len_of(s));
  const char* t = editor.text();
  if (!labels.empty()) labels[0]->set_text_plain(t);
  dirty();
}

// Mech3.exe 1.2 @ 0x005a50e0: with the focus the first label shows the line
// and the caret stands at the cursor's place in it, in the label's colour.
// The original reads the first label unchecked (a bar needs one).
void EditBar::update(float dt) {
  Text* label = labels.empty() ? nullptr : labels[0];
  if (flags & kHidden) {
    if (label) {
      label->hide();
      label->dirty();
    }
    caret.hide();
    caret.dirty();
    return;
  }
  if (!focus) {
    caret.hide();
  } else if (label) {
    label->show_for(-1.0f);
    label->set_text_plain(editor.text());
    const int32_t ly = label->get_y();
    const int32_t lx = label->get_x();
    Rect r{lx, ly, lx, ly};
    if (label->measure_prefix(editor.cursor, &r)) {
      const uint32_t c = label->color_top;
      caret.color = zvid::pack_bytes(static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8),
                                     static_cast<uint8_t>(c >> 16));
      caret.place(r.right, r.top, caret_w, r.bottom - r.top);
      caret.show_for(-1.0f);
    }
    dirty();
  }
  Object2D::update(dt);
  caret.update(dt);
}

}  // namespace ztwod
