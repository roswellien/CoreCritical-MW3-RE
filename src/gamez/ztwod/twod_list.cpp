// zTwoD's slider and list box (docs/twod.md, Buttons). Original file unknown:
// with the buttons before twod_dialog.cpp (0x0059e0f0..0x0059ec20); the list
// box's destructor sits in mech3's code (0x0042f480), where its vtable was
// emitted.
#include "gamez/zimage/zimg_library.h"
#include "gamez/zreader/zreader.h"
#include "gamez/ztwod/twod.h"

namespace ztwod {

// ---- Slider ----

// Mech3.exe 1.2 @ 0x0059e0f0
Slider::Slider() = default;

// Mech3.exe 1.2 @ 0x0059e170
Slider::~Slider() {
  if (empty && empty != image) empty = host().image_release(empty);
  if (fill && fill != image) fill = host().image_release(fill);
}

// Mech3.exe 1.2 @ 0x0059e1c0
void Slider::draw() {
  if (!can_present()) return;
  if (!empty || !fill) return;
  erase();
  if (fill_src.left != fill_src.right) host().blit(fill, fill_dx + x, fill_dy + y, 0, &fill_src);
  if (empty_src.left != empty_src.right) host().blit(empty, empty_dx + x, empty_dy + y, 0, &empty_src);
}

// Mech3.exe 1.2 @ 0x0059e260
// A Control's entry plus FILLBITMAP ("name" [x y]): the fill, and the button
// moved to the fill's place.
bool Slider::read_control(zrdr::Node* n, Dialog* d) {
  Control::read_control(n, d);
  if (zrdr::Node* f = zrdr::find(n, "FILLBITMAP")) {
    int32_t px = base_x, py = base_y;
    fill = host().image_get(f->list[1].s);
    dirty();
    if (f->list[0].i >= 4) {
      px += f->list[2].i;
      py += f->list[3].i;
    }
    set_pos(px, py);
    empty = image;
    dirty();
  }
  set_raw(0.0f);
  return true;
}

// Mech3.exe 1.2 @ 0x0059fad0
void Slider::set_raw(float v) {
  value = v;
  dirty();
}

// Mech3.exe 1.2 @ 0x0059e2f0
// The click's distance from the left edge over the image's width.
void Slider::set_from_mouse() {
  const int32_t dx = dialog->m.x - get_x();
  const int32_t w = image ? image->width : 0;
  set_raw(static_cast<float>(dx) / static_cast<float>(w));
  Control::on_left_up();
}

// Mech3.exe 1.2 @ 0x0059e350
// The fill shows its left `v` of the width; the empty image the rest (as tall
// as the fill image, as written).
void Slider::set_value(float v) {
  if (!(v >= 0.0f)) v = 0.0f;  // below 0 or not a number
  else if (v > 1.0f) v = 1.0f;
  value = v;
  dirty();
  if (!fill) return;
  fill_src.top = 0;
  fill_src.bottom = fill->height;
  fill_src.left = 0;
  const int32_t cut = static_cast<int32_t>(static_cast<float>(fill->width) * v);
  fill_src.right = cut;
  fill_dx = 0;
  fill_dy = 0;
  if (!empty) return;
  empty_src.top = 0;
  empty_src.bottom = fill->height;
  empty_src.left = cut;
  empty_src.right = empty->width;
  empty_dx = cut;
  empty_dy = 0;
}

// ---- ListBox ----

// Mech3.exe 1.2 @ 0x0059e9a0
ListItem::ListItem() : FontText(nullptr, 0, 0) {}

// Mech3.exe 1.2 @ 0x0059faf0
void ListItem::draw() {
  FontText::draw();
  bg_rect.left = get_x();
  if (needs_render) render();
  bg_rect.right = get_x() + width;
  bg_rect.top = get_y();
  bg_rect.bottom = text_height() + get_y();
}

// Mech3.exe 1.2 @ 0x0059ec20
void ListItem::on_left_up() {
  if (box) box->select(index);
}

// Mech3.exe 1.2 @ 0x0059e430
ListBox::ListBox() = default;

// Mech3.exe 1.2 @ 0x0042f480 (the destructor its vtable names)
ListBox::~ListBox() {
  strings.clear();
  first = -1;
  delete[] items;
  items = nullptr;
}

// Mech3.exe 1.2 @ 0x0059e4e0
// A CheckButton's entry plus SELECTED_FONT (a font number or a fonts.zrd
// name), ACTIVE_RECT, LIST_FONT, SPACING, LIST_OFFSET ((x y) (x y)),
// LISTSIZE (n [above]) and LISTWIDTH.
bool ListBox::read_control(zrdr::Node* n, Dialog* d) {
  Rect empty_rect;
  CheckButton::read_control(n, d);
  zimg::Texture* bg = d->static_bg;
  if (zrdr::Node* sf = zrdr::find(n, "SELECTED_FONT")) {
    if (sf->type == zrdr::Int) {
      FontEntry& f = dialog->fonts[sf->i];
      if (f.used) {
        shown.create_font(f.face, f.height, f.weight, 0, 0, 0, 2);
        if (f.color != shown.color_top || f.color != shown.color_bottom) {
          shown.color_top = f.color;
          shown.color_bottom = f.color;
          shown.needs_render = 1;
        }
        shown.shadow_dx = 1;
        shown.shadow = f.shadow;
        shown.shadow_dy = 1;
      }
    } else {
      shown.set_font(host().font_find(sf->list[1].s));
    }
  }
  if (zrdr::Node* ar = zrdr::find(n, "ACTIVE_RECT")) {
    bounds.left = ar->list[1].i;
    bounds.top = ar->list[2].i;
    bounds.right = ar->list[3].i;
    bounds.bottom = ar->list[4].i;
  }
  const Handle list_font = host().font_find(zrdr::get_string(n, "LIST_FONT"));
  if (zrdr::Node* sp = zrdr::find(n, "SPACING")) spacing = sp->i;
  if (zrdr::Node* lo = zrdr::find(n, "LIST_OFFSET")) {
    before_x = static_cast<float>(lo->list[1].list[1].i);
    before_y = static_cast<float>(lo->list[1].list[2].i);
    after_x = static_cast<float>(lo->list[2].list[1].i);
    after_y = static_cast<float>(lo->list[2].list[2].i);
  }
  if (zrdr::Node* ls = zrdr::find(n, "LISTSIZE")) {
    zrdr::Node* l = ls->list;
    set_size(l[1].i, l[0].i > 2 ? l[2].i : 0);
    for (int32_t i = 0; i < count; ++i) {
      d->add(&items[i]);
      items[i].show_for(-1.0f);
      items[i].box = this;
      if (bg) items[i].set_background(bg, &empty_rect);
      if (list_font) items[i].set_font(list_font);
    }
    d->add(&shown);
    shown.show_for(-1.0f);
    shown.box = this;
  }
  if (zrdr::Node* lw = zrdr::find(n, "LISTWIDTH")) {
    for (int32_t i = 0; i < count; ++i) items[i].max_width = lw->i;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x0059e7d0
// `n` entries, `above` of them stacked upward from the shown one (LIST_OFFSET's
// first pair), the rest downward (its second), `spacing` apart.
void ListBox::set_size(int32_t n, int32_t above) {
  if (items) {
    delete[] items;  // vt+0(3)
    items = nullptr;
  }
  items = new ListItem[static_cast<size_t>(n)];
  count = n;
  before = above;
  for (int32_t i = 0; i < before; ++i) {
    const float fy = static_cast<float>((i - before) * spacing) + static_cast<float>(base_y) + before_y;
    const int32_t py = static_cast<int32_t>(fy);
    const int32_t px = static_cast<int32_t>(static_cast<float>(base_x) + before_x);
    items[i].set_pos(px, py);
  }
  shown.set_pos(base_x, base_y);
  for (int32_t i = before; i < count; ++i) {
    const float fy = static_cast<float>((i - before + 1) * spacing) + static_cast<float>(base_y) + after_y;
    const int32_t py = static_cast<int32_t>(fy);
    const int32_t px = static_cast<int32_t>(static_cast<float>(base_x) + after_x);
    items[i].set_pos(px, py);
  }
}

// Mech3.exe 1.2 @ 0x0059ea30
// The entries show the strings around `index`: those above count up to it,
// those below on from it; entries past either end are hidden.
void ListBox::select(int32_t index) {
  const uint32_t size = static_cast<uint32_t>(strings.size());
  auto fill = [&](ListItem& it, int32_t k) {
    if (k < 0 || static_cast<uint32_t>(k) >= size) {
      it.hide();
      it.erase();
    } else {
      it.index = k;
      it.set_text("%s", strings[static_cast<size_t>(k)].c_str());
      it.show_for(-1.0f);
    }
    it.dirty();
  };
  for (int32_t i = 0; i < before; ++i) fill(items[i], i + index - before);
  if (index >= 0 && static_cast<uint32_t>(index) < size) {
    shown.index = index;
    shown.set_text("%s", strings[static_cast<size_t>(index)].c_str());
  }
  for (int32_t i = before; i < count; ++i) fill(items[i], i + index - before + 1);
  first = index;
}

}  // namespace ztwod
