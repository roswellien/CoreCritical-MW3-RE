// zTwoD's buttons (docs/twod.md, Buttons). Original file unknown: before
// twod_dialog.cpp (0x0059bbe0..0x0059e0f0).
#include <cstring>

#include "gamez/zimage/zimg_library.h"
#include "gamez/zreader/zreader.h"
#include "gamez/ztwod/twod.h"

namespace ztwod {

namespace {

void show_all(std::vector<Text*>& v) {
  for (Text* t : v) t->show_for(-1.0f);
}
void hide_all(std::vector<Text*>& v) {
  for (Text* t : v) t->hide();
}
void delete_all(std::vector<Text*>& v) {
  for (Text*& t : v) {
    delete t;  // 0x0059fab0: vt+0(1)
    t = nullptr;
  }
  v.clear();
}

// The width of a label's text, rendered first when it changed (the inline
// "+0x474 ? vt+0xac" the compiler repeats before each read of +0x460).
int32_t text_width(Text* t) {
  if (t->needs_render) t->render();
  return t->width;
}

// SOUND ("name" [volume]): the sound's id and its volume (1 unless a float follows).
void read_sound(zrdr::Node* n, int32_t* id, float* volume) {
  zrdr::Node* v = zrdr::find(n, "SOUND");
  if (!v) return;
  zrdr::Node* l = v->list;
  const int32_t count = l[0].i;
  if (count <= 1) return;
  const char* name = l[1].type == zrdr::String ? l[1].s : nullptr;
  float vol = 1.0f;
  if (count >= 3 && l[2].type == zrdr::Float) vol = l[2].f;
  *id = host().sound_find(name);
  *volume = vol;
}

// LABEL's texts: hidden, added to the dialog, appended to `v` (0x0059cfcd..).
void read_hidden_labels(Dialog* d, zrdr::Node* value, Control* c, std::vector<Text*>& v) {
  int32_t count = 0;
  Text** texts = read_labels(d, value, &count, &c->base_x);
  for (int32_t i = 0; i < count; ++i) {
    Text* t = texts[i];
    t->hide();
    d->add(t);
    v.push_back(t);
  }
}

}  // namespace

// Mech3.exe 1.2 @ 0x0059bbe0
Control::Control() : Image(0) {
  u40 = 1;
  dirty();
  flags = ((flags >> 4) & 1) << 4 | kManaged;
}

// Mech3.exe 1.2 @ 0x0059bdd0
Control::~Control() {
  delete_all(labels);
  delete_all(disable_labels);
  delete_all(rollover_labels);
  delete_all(activate_labels);
  if (img_normal && img_normal != image) img_normal = host().image_release(img_normal);
  if (img_activate && img_activate != image) img_activate = host().image_release(img_activate);
  if (img_rollover && img_rollover != image) img_rollover = host().image_release(img_rollover);
  if (img_disabled && img_disabled != image) img_disabled = host().image_release(img_disabled);
  if (image && !owned) host().image_release(image);
}

// Mech3.exe 1.2 @ 0x0059c020
void Control::dirty() {
  Object2D::dirty();
  for (Text* t : labels) t->dirty();
}

// Mech3.exe 1.2 @ 0x0059c050
int32_t Control::hit(int32_t px, int32_t py) {
  if (!can_present()) return 0;
  if (flags & kHidden) return 0;
  int32_t left = x, right = x;  // what an alignment outside 0..2 would leave (uninitialised)
  if (image) {
    left = x;
    right = image->width + x;
    if (px >= x && px <= right && py >= y && py <= image->height + y) return 1;
  }
  if (labels.empty()) return 0;
  for (Text* t : labels) {
    const int32_t top = t->get_y();
    const int32_t bottom = t->text_height() + top;
    switch (t->align) {
      case 0:
        left = t->get_x();
        right = t->get_x() + text_width(t);
        break;
      case 1:
        left = t->get_x() - text_width(t) / 2;
        right = t->get_x() + text_width(t) / 2;
        break;
      case 2:
        left = t->get_x() - text_width(t);
        right = t->get_x();
        break;
      default:
        break;
    }
    if (px >= left && px <= right && py >= top && py <= bottom) return 1;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0059c260
void Control::show_for(float seconds) {
  Object2D::show_for(seconds);
  refresh();
}

// Mech3.exe 1.2 @ 0x0059c280
void Control::hide() {
  Object2D::hide();
  hide_all(labels);
  hide_all(rollover_labels);
  hide_all(activate_labels);
  hide_all(disable_labels);
}

// Mech3.exe 1.2 @ 0x0059c310
// The image's box grown to hold every normal label, each by its alignment.
Rect* Control::get_rect() {
  bounds = Rect{};
  Rect* out = nullptr;
  if (!enabled) return nullptr;
  if (image) {
    bounds.top = y;
    bounds.left = x;
    bounds.bottom = image->height + y;
    bounds.right = image->width + x;
    out = &bounds;
  }
  if (labels.empty()) return out;
  if (bounds.top == 0) bounds.top = labels[0]->get_y();
  if (bounds.bottom == 0) bounds.bottom = labels[0]->text_height() + bounds.top;
  for (Text* t : labels) {
    bounds.bottom += t->text_height();
    switch (t->align) {
      case 0: {
        const int32_t gx = t->get_x();
        bounds.left = bounds.left < gx ? bounds.left : t->get_x();
        const int32_t w = text_width(t);
        bounds.right = t->get_x() + w <= bounds.right ? bounds.right : t->get_x() + text_width(t);
        break;
      }
      case 1: {
        int32_t w = text_width(t);
        bounds.left = t->get_x() - w / 2 >= bounds.left ? bounds.left : t->get_x() - text_width(t) / 2;
        w = text_width(t);
        bounds.right = t->get_x() + w / 2 <= bounds.right ? bounds.right : t->get_x() + text_width(t) / 2;
        break;
      }
      case 2: {
        const int32_t w = text_width(t);
        bounds.left = bounds.left < t->get_x() - w ? bounds.left : t->get_x() - text_width(t);
        bounds.right = bounds.right > t->get_x() ? bounds.right : t->get_x();
        break;
      }
      default:
        break;
    }
  }
  bounds.bottom -= labels[0]->text_height();
  return &bounds;
}

// Mech3.exe 1.2 @ 0x0059c650
void Control::on_enter() {
  if (!enabled) return;
  if (img_rollover) {
    if (!img_normal) img_normal = image;
    set_image(img_rollover);
  }
  if (snd_rollover) rollover_playing = host().sound_play(snd_rollover, vol_rollover);
  if (rollover_labels.empty()) {
    show_all(labels);
    hide_all(activate_labels);
  } else {
    hide_all(labels);
    hide_all(activate_labels);
    show_all(rollover_labels);
  }
}

// Mech3.exe 1.2 @ 0x0059c8b0
void Control::on_leave() {
  if (!enabled) return;
  set_image(img_normal);
  if (rollover_playing) rollover_playing = 0;
  hide_all(rollover_labels);
  hide_all(activate_labels);
  show_all(labels);
}

// Mech3.exe 1.2 @ 0x0059c960
void Control::on_left_up() {
  if (!enabled) return;
  host().input_reset();
  if (img_activate) set_image(img_activate);
  if (rollover_playing) {
    host().sound_stop(rollover_playing);
    rollover_playing = 0;
  }
  if (snd_activate) activate_playing = host().sound_play(snd_activate, vol_activate);
  hide_all(rollover_labels);
  if (activate_labels.empty()) {
    show_all(labels);
  } else {
    show_all(activate_labels);
    hide_all(labels);
  }
}

// Mech3.exe 1.2 @ 0x0059c790
void Control::refresh() {
  if (flags & kHidden) return;
  hide_all(rollover_labels);
  hide_all(activate_labels);
  zimg::Texture* t;
  if (!enabled) {
    hide_all(labels);
    show_all(disable_labels);
    t = img_disabled;
  } else {
    show_all(labels);
    hide_all(disable_labels);
    t = img_normal;
  }
  set_image(t);
  dirty();
}

// Mech3.exe 1.2 @ 0x0059cdc0
// BUTTONS entry: POSITION, BITMAP (name [offset]), LABEL, FLASH, and ROLLOVER,
// DISABLE and ACTIVATE each with BITMAP, SOUND, LABEL and FLASH.
bool Control::read_control(zrdr::Node* n, Dialog* d) {
  dialog = d;
  show_for(-1.0f);
  d->add(this);
  base_x = d->origin_x;
  base_y = d->origin_y;
  if (zrdr::Node* p = zrdr::find(n, "POSITION")) {
    base_x += p->list[1].i;
    base_y += p->list[2].i;
  }
  int32_t px = base_x, py = base_y;
  if (zrdr::Node* b = zrdr::find(n, "BITMAP")) {
    int32_t off[2] = {0, 0};
    if (const char* name = name_and_offset(b, off)) {
      img_normal = set_image(name);
      px += off[0];
      py += off[1];
    }
  }
  set_pos(px, py);
  if (zimg::Texture* bg = d->static_bg) {
    if (Rect* r = get_rect()) {
      const Rect copy = *r;
      set_background(bg, &copy);
    }
  }

  if (zrdr::Node* ro = zrdr::find(n, "ROLLOVER")) {
    if (zrdr::Node* b = zrdr::find(ro, "BITMAP")) {
      int32_t off[2] = {0, 0};
      if (const char* name = name_and_offset(b, off)) {
        img_rollover = host().image_get(name);
        set_pos(off[0] + base_x, off[1] + base_y);
      }
    }
    read_sound(ro, &snd_rollover, &vol_rollover);
    if (zrdr::Node* l = zrdr::find(ro, "LABEL")) read_hidden_labels(d, l, this, rollover_labels);
    if (zrdr::Node* f = zrdr::find(ro, "FLASH")) read_flash(f, &rollover_labels);
  }
  if (zrdr::Node* di = zrdr::find(n, "DISABLE")) {
    if (zrdr::Node* b = zrdr::find(di, "BITMAP")) img_disabled = host().image_get(name_and_offset(b, nullptr));
    read_sound(di, &snd_disable, &vol_disable);
    if (zrdr::Node* l = zrdr::find(di, "LABEL")) read_hidden_labels(d, l, this, disable_labels);
    if (zrdr::Node* f = zrdr::find(di, "FLASH")) read_flash(f, &disable_labels);
  }
  if (zrdr::Node* ac = zrdr::find(n, "ACTIVATE")) {
    if (zrdr::Node* b = zrdr::find(ac, "BITMAP")) img_activate = host().image_get(name_and_offset(b, nullptr));
    read_sound(ac, &snd_activate, &vol_activate);
    if (zrdr::Node* l = zrdr::find(ac, "LABEL")) read_hidden_labels(d, l, this, activate_labels);
    if (zrdr::Node* f = zrdr::find(ac, "FLASH")) read_flash(f, &activate_labels);
  }
  if (zrdr::Node* l = zrdr::find(n, "LABEL")) {
    int32_t count = 0;
    Text** texts = read_labels(d, l, &count, &base_x);
    for (int32_t i = 0; i < count; ++i) {
      d->add(texts[i]);
      labels.push_back(texts[i]);
    }
  }
  if (zrdr::Node* f = zrdr::find(n, "FLASH")) read_flash(f, &labels);
  return true;
}

// ---- CheckButton ----

// Mech3.exe 1.2 @ 0x0059d870
CheckButton::CheckButton() = default;

// Mech3.exe 1.2 @ 0x0059d8d0
CheckButton::~CheckButton() {
  set_image(img_unchecked);
  if (img_checked) img_checked = host().image_release(img_checked);
  if (text) {
    delete text;
    text = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x0059d960
void CheckButton::show_for(float seconds) {
  Control::show_for(seconds);
  set_checked(checked);
}

// Mech3.exe 1.2 @ 0x0059d990
void CheckButton::hide() {
  Control::hide();
  if (text) text->hide();
}

// Mech3.exe 1.2 @ 0x0059d9c0
void CheckButton::refresh() {
  hide_all(rollover_labels);
  hide_all(activate_labels);
  zimg::Texture* t;
  if (enabled) {
    show_all(labels);
    hide_all(disable_labels);
    t = checked ? img_checked : img_unchecked;
  } else {
    hide_all(labels);
    show_all(disable_labels);
    t = checked ? img_disabled_checked : img_disabled_unchecked;
  }
  if (t) set_image(t);
  dirty();
}

// Mech3.exe 1.2 @ 0x0059db00
void CheckButton::on_enter() {
  if (!enabled || checked) return;
  if (snd_rollover) rollover_playing = host().sound_play(snd_rollover, vol_rollover);
  Control::on_enter();
}

// Mech3.exe 1.2 @ 0x0059db50
void CheckButton::on_leave() {
  if (!enabled || checked) return;
  if (rollover_playing) {
    host().sound_stop(rollover_playing);
    rollover_playing = 0;
  }
  Control::on_leave();
}

// Mech3.exe 1.2 @ 0x0059db90
void CheckButton::on_left_up() {
  if (!enabled) return;
  set_checked(checked == 0);
  Control::on_left_up();
}

// Mech3.exe 1.2 @ 0x0059dbc0
int32_t CheckButton::set_checked(int32_t on) {
  const int32_t old = checked;
  checked = on;
  if (!on) {
    if (img_unchecked) set_image(img_unchecked);
    if (!text) {
      dirty();
      return old;
    }
    text->hide();
    dirty();
    return old;
  }
  if (img_checked) set_image(img_checked);
  if (text) text->show_for(-1.0f);
  dirty();
  return old;
}

// Mech3.exe 1.2 @ 0x0059dc40
// A Control's entry plus CHECKED (BITMAP, TEXT), DISABLE_UNSEL (BITMAP, TEXT)
// and DISABLE_SEL (BITMAP, TEXT); the bounds are the unchecked image's or the
// labels'.
bool CheckButton::read_control(zrdr::Node* n, Dialog* d) {
  Control::read_control(n, d);
  img_unchecked = image;
  if (zrdr::Node* ch = zrdr::find(n, "CHECKED")) {
    if (zrdr::Node* b = zrdr::find(ch, "BITMAP")) img_checked = host().image_get(name_and_offset(b, nullptr));
    if (zrdr::Node* t = zrdr::find(ch, "TEXT")) {
      int32_t count = 0;
      Text** texts = read_labels(dialog, t, &count, &base_x);
      if (count > 0) {
        text = texts[0];
        text->hide();
        dialog->add(text);
      }
    }
  }
  if (zrdr::Node* du = zrdr::find(n, "DISABLE_UNSEL")) {
    if (zrdr::Node* b = zrdr::find(du, "BITMAP")) {
      img_disabled_unchecked = host().image_get(name_and_offset(b, nullptr));
    }
    if (zrdr::Node* t = zrdr::find(du, "TEXT")) {
      const int32_t bx = base_x, by = base_y;
      text = new Text();  // replaces CHECKED's TEXT, which stays in the dialog (as written)
      zrdr::Node* l = t->list;
      text->set_text(host().message(l[1].s));
      text->set_pos(l[2].i + bx, l[3].i + by);
      FontEntry& f = dialog->fonts[l[4].i];
      if (f.used) {
        text->create_font(f.face, f.height, f.weight, 0, 0, 0, 2);
        if (f.color != text->color_top || f.color != text->color_bottom) {
          text->color_top = f.color;
          text->color_bottom = f.color;
          text->needs_render = 1;
        }
        text->shadow = f.shadow;
        text->shadow_dx = 1;
        text->shadow_dy = 1;
      }
      text->hide();
      dialog->add(text);
    }
    if (zrdr::Node* l = zrdr::find(n, "LABEL")) read_hidden_labels(dialog, l, this, disable_labels);
  }
  if (zrdr::Node* ds = zrdr::find(n, "DISABLE_SEL")) {
    if (zrdr::Node* b = zrdr::find(ds, "BITMAP")) {
      img_disabled_checked = host().image_get(name_and_offset(b, nullptr));
    }
    if (zrdr::Node* t = zrdr::find(ds, "TEXT")) {
      int32_t count = 0;
      Text** texts = read_labels(dialog, t, &count, &base_x);
      if (count > 0) {
        text = texts[0];
        text->hide();
        dialog->add(text);
      }
    }
  }
  if (img_unchecked) {
    bounds.top = y;
    bounds.left = x;
    bounds.bottom = img_unchecked->height + y;
    bounds.right = img_unchecked->width + x;
  } else if (!labels.empty()) {
    bounds.top = labels[0]->get_y();
    bounds.left = labels[0]->get_x();
    bounds.bottom = labels[0]->text_height() + bounds.top;
    for (Text* t : labels) {
      bounds.bottom += t->text_height();
      const int32_t w = text_width(t);
      bounds.right = w + bounds.left > bounds.right ? text_width(t) + bounds.left : bounds.right;
    }
    bounds.bottom -= labels[0]->text_height();
  }
  return true;
}

}  // namespace ztwod
