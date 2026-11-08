// zTwoD's objects, layers, windows, images and the cursor (docs/twod.md).
// Original files unknown: between twod_dialog.cpp and twod_wintext.cpp
// (0x0059fa10..0x005a01f0), the images before twod_dialog.cpp (0x0059b830..),
// the window and cursor after twod_wintext.cpp; the small virtuals the compiler
// folded into mech3's code (0x00409a10..0x0040a8a0) are theirs too.
#include <cstdlib>
#include <cstring>

#include "gamez/zimage/zimg_library.h"
#include "gamez/zreader/zreader.h"
#include "gamez/ztwod/twod.h"

namespace ztwod {

namespace {
uint32_t g_dirty_bits = 0xc;  // 0x005f49b8: both buffers until told otherwise
}  // namespace

uint32_t& dirty_bits() { return g_dirty_bits; }

// Mech3.exe 1.2 @ 0x0059fb80
void set_page_flip(bool flipping) { g_dirty_bits = flipping ? 0xc : 0x4; }

// ---- Object2D ----

// Mech3.exe 1.2 @ 0x0059fda0
Object2D::Object2D(int32_t px, int32_t py) : x(px), y(py) {
  Object2D::dirty();  // a direct call; the flags are cleared just after
  flags = 0;
  mouse = 0;
  set_background(nullptr, nullptr);
}

// Mech3.exe 1.2 @ 0x0059fdf0
Object2D::Object2D(const Object2D& o)
    : flags(o.flags), timer(o.timer), x(o.x), y(o.y), background(o.background),
      bg_rect(o.bg_rect), mouse(o.mouse), presentation_visible(o.presentation_visible) {}

// Mech3.exe 1.2 @ 0x0059fe50
Object2D& Object2D::operator=(const Object2D& o) {
  next = nullptr;
  owner = nullptr;
  flags = o.flags;
  mouse = o.mouse;
  presentation_visible = o.presentation_visible;
  timer = o.timer;
  x = o.x;
  y = o.y;
  background = o.background;
  bg_rect = o.bg_rect;
  return *this;
}

// Mech3.exe 1.2 @ 0x00409af0
Object2D::~Object2D() = default;

// Mech3.exe 1.2 @ 0x00409a10
void Object2D::draw() { erase(); }

// Mech3.exe 1.2 @ 0x00409a20
void Object2D::erase() {
  if (!can_present()) return;
  if (background) host().blit(background, x, y, 0, &bg_rect);
}

// Mech3.exe 1.2 @ 0x00409a40
void Object2D::set_pos(int32_t px, int32_t py) {
  x = px;
  y = py;
  dirty();
}

// Mech3.exe 1.2 @ 0x00409a60
void Object2D::set_x(int32_t v) {
  x = v;
  dirty();
}

// Mech3.exe 1.2 @ 0x00409a70
void Object2D::set_y(int32_t v) {
  y = v;
  dirty();
}

// Mech3.exe 1.2 @ 0x0059ff60
void Object2D::set_background(zimg::Texture* image, const Rect* r) {
  background = image;
  set_bg_rect(r);
}

// Mech3.exe 1.2 @ 0x0059ff80
void Object2D::set_bg_rect(const Rect* r) {
  if (r) bg_rect = *r;
}

// Mech3.exe 1.2 @ 0x0059ff00
void Object2D::dirty() { flags |= g_dirty_bits; }

// Mech3.exe 1.2 @ 0x0059ffb0
void Object2D::update(float dt) {
  PresentationScope presentation(presentation_visible);
  if (!(flags & kHidden)) {
    if (!(flags & kManaged)) {
      draw();
    } else if (flags & kDirty) {
      draw();
      flags &= ~kDirty;
    } else if (flags & kDirty2) {
      draw();
      flags &= ~kDirty2;
    }
    if (flags & kTimed) {
      timer -= dt;
      if (timer <= 0.0f) hide();
    }
    return;
  }
  if (!(flags & kManaged)) return;
  if (flags & kDirty) {
    erase();
    flags &= ~kDirty;
  } else if (flags & kDirty2) {
    erase();
    flags &= ~kDirty2;
  }
}

// Mech3.exe 1.2 @ 0x0059fd50
int32_t Object2D::hit(int32_t px, int32_t py) {
  if (!can_present()) return 0;
  if (flags & kHidden) return 0;
  const Rect* r = get_rect();
  if (r && r->left <= px && px <= r->right && r->top <= py && py <= r->bottom) return 1;
  return 0;
}

// Mech3.exe 1.2 @ 0x0059ff10
void Object2D::show_for(float seconds) {
  if (flags & kHidden) dirty();
  flags &= ~kHidden;
  if (seconds >= 0.0f) {
    flags |= kTimed;
    timer = seconds;
  }
}

// Mech3.exe 1.2 @ 0x0059ff50
void Object2D::hide() { flags |= kHidden; }

// Mech3.exe 1.2 @ 0x005a0090
void Object2D::rect_at(Rect* out) {
  out->left = out->right = get_x();
  out->top = out->bottom = get_y();
}

// Mech3.exe 1.2 @ 0x0059feb0
// FIX (B), with the dialogs' origin (resolution.md step 5): a primitive's
// place is in its dialog's frame too, as its controls' (read_control, 0x0059cdc0)
// and its background pieces' are; the exe's origin was always 0. (Off it, a
// message box's TITLE and MESSAGE on a wide canvas sat outside its box.)
bool Object2D::read(zrdr::Node* n, Dialog* d) {
  if (!n) return false;
  int32_t p[2];
  if (zrdr::get_ivec2(n, "POSITION", p)) set_pos(p[0] + (d ? d->origin_x : 0), p[1] + (d ? d->origin_y : 0));
  return true;
}

// ---- Layer ----

// Mech3.exe 1.2 @ 0x0059fba0
Layer::Layer() { set_active(0); }

// Mech3.exe 1.2 @ 0x0059fbd0
Layer::~Layer() = default;

// Mech3.exe 1.2 @ 0x0059fd20
void Layer::update(float dt) {
  if (!active) return;
  for (Object2D* o = head; o; o = o->next) {
    PresentationScope presentation(o->presentation_visible);
    o->update(dt);
  }
}

// Mech3.exe 1.2 @ 0x0040e820
void Layer::set_active(int32_t on) { active = on; }

// Mech3.exe 1.2 @ 0x0059fbe0
bool Layer::add(Object2D* o) {
  if (head && tail) {
    tail->next = o;
  } else {
    head = o;
  }
  tail = o;
  o->next = nullptr;
  o->owner = this;
  return true;
}

// Mech3.exe 1.2 @ 0x0059fc30
bool Layer::find(Object2D* o, Object2D** prev) {
  Object2D* p = head;
  if (!p) return false;
  if (o == p) {
    *prev = nullptr;  // unchecked, as written
    return true;
  }
  for (Object2D* n = p->next;; n = p->next) {
    if (n == o) {
      if (prev) *prev = p;
      return true;
    }
    p = n;
    if (!n) return false;
  }
}

// Mech3.exe 1.2 @ 0x0059fc80
bool Layer::remove(Object2D* o) {
  Object2D* prev = nullptr;
  if (!find(o, &prev)) return false;
  if (!prev) {
    head = o->next;
    if (o == tail) tail = o->next;
  } else {
    prev->next = o->next;
    if (o == tail) tail = prev;
  }
  o->next = nullptr;
  o->owner = nullptr;
  return true;
}

// Mech3.exe 1.2 @ 0x0059fcf0
void Layer::set_flags_all(uint32_t f) {
  for (Object2D* o = head; o; o = o->next) o->flags = (o->flags & kHidden) ? (f | kHidden) : f;
}

// Mech3.exe 1.2 @ 0x0059fa60
void Layer::dirty_all() {
  for (Object2D* o = head; o; o = o->next) o->dirty();
}

// ---- Window ----

// Mech3.exe 1.2 @ 0x005a2b50
Window::Window(uint32_t press, uint32_t release) : press_mask(press), release_mask(release) {}

// Mech3.exe 1.2 @ 0x005a2b80
Window::~Window() = default;

// Mech3.exe 1.2 @ 0x005a2ba0
void Window::set_active(int32_t on) { active = on; }

// Mech3.exe 1.2 @ 0x005a2bb0
// Each object hears hover, enter, leave, press, release and click from the left
// button; release is also sent on a right-button release (+0x48 below).
void Window::update(float dt) {
  if (!active) return;
  m = host().mouse();
  for (Object2D* o = head; o; o = o->next) {
    const bool in = o->hit(m.x, m.y) == 1;
    if (o->accepts(this, in)) {
      if ((m.right & 4) && (o->mouse & kPressed) == kPressed) {
        o->mouse &= ~kPressed;
        o->on_release();
      }
      if (in) {
        if (!(o->mouse & kHover)) {
          o->mouse |= kHover;
          o->on_enter();
        } else {
          o->on_hover();
        }
        if ((m.left & press_mask) && !(o->mouse & kPressed)) {
          o->mouse |= kPressed;
          o->on_press();
        }
        if (m.left & 4) o->on_left_up();
        if (m.right & 4) o->on_right_up();
        if (m.left & 3) o->on_drag(m.x, m.y);
        if ((m.left & 4) && (o->mouse & kPressed) == kPressed) o->on_click();
      } else {
        if ((m.left & release_mask) && (o->mouse & kPressed) == kPressed) {
          o->mouse &= ~kPressed;
          o->on_release();
        }
        if ((m.left & 3) && (o->mouse & kPressed) == kPressed) o->on_drag(m.x, m.y);
        if ((o->mouse & kHover) == kHover) {
          o->mouse &= ~kHover;
          o->on_leave();
        }
      }
    }
    o->after_input(this, in);
  }
  if (cursor) cursor->erase();
  Layer::update(dt);
  if (cursor) {
    cursor->set_pos(m.x, m.y);
    cursor->update(dt);
  }
}

// ---- Image ----

// Mech3.exe 1.2 @ 0x0059b970
Image::Image(int32_t c) : Object2D(0, 0), center(c) {}

// Mech3.exe 1.2 @ 0x0059b830
Image::Image(const char* name, int32_t px, int32_t py, int32_t c) : Object2D(0, 0) {
  set_image(name);
  center = c;
  Image::set_pos(px, py);
}

// Mech3.exe 1.2 @ 0x0059b8e0
Image::Image(zimg::Texture* t, int32_t px, int32_t py, int32_t c) : Object2D(0, 0), center(c) {
  image = t;
  set_image(t);
  Image::set_pos(px, py);
}

// Mech3.exe 1.2 @ 0x0059b9c0
Image::~Image() { release(); }

// Mech3.exe 1.2 @ 0x0059ba10
void Image::release() {
  if (image && owned) image = host().image_release(image);
  owned = 0;
}

// Mech3.exe 1.2 @ 0x0059bb10
zimg::Texture* Image::set_image(const char* name) {
  if (!name) return nullptr;
  release();
  image = host().image_get(name);
  if (image) owned = 1;
  dirty();
  return image;
}

// Mech3.exe 1.2 @ 0x0059bb50
zimg::Texture* Image::set_image(zimg::Texture* t) {
  owned = 0;
  image = t;
  dirty();
  return t;
}

// Mech3.exe 1.2 @ 0x0059bb70
void Image::draw() {
  if (!image) return;
  if (queued) {
    for (QueuedDraw& q : queue) {
      if (!q.count) continue;
      if (can_present()) host().blit(image, q.x, q.y, 0, &q.src);
      if (--q.count == 0) --queued;
    }
    return;
  }
  erase();
  if (can_present()) host().blit(image, x, y, 0, src);
}

// Mech3.exe 1.2 @ 0x0059bab0
void Image::set_pos(int32_t px, int32_t py) {
  if (center && image) {
    x = px - image->width / 2;
    y = py - image->height / 2;
  } else {
    x = px;
    y = py;
  }
  dirty();
}

// Mech3.exe 1.2 @ 0x00409cb0
int32_t Image::get_x() {
  if (center && image) return x + image->width / 2;
  return x;
}

// Mech3.exe 1.2 @ 0x00409cf0
int32_t Image::get_y() {
  if (center && image) return y + image->height / 2;
  return y;
}

// Mech3.exe 1.2 @ 0x0059ba40
bool Image::read(zrdr::Node* n, Dialog* d) {
  if (!n) return false;
  const char* name = zrdr::get_string(n, "BITMAP");
  bool c = false;
  zrdr::get_bool(n, "CENTER", &c);
  set_image(name);
  center = c ? 1 : 0;
  Object2D::read(n, d);
  return true;
}

// Mech3.exe 1.2 @ 0x00409d30
void Image::update_bg_rect() {
  Rect r;
  r.left = x;
  r.top = y;
  r.right = x + (image ? image->width : 0);
  r.bottom = y + (image ? image->height : 0);
  set_bg_rect(&r);
}

// ---- Cursor ----

namespace {
Rect g_zero_rect;  // 0x00736548
}  // namespace

// Mech3.exe 1.2 @ 0x005a7750
Cursor::Cursor(const char* name, int32_t cap) : Image(0) {
  capture = cap;
  under = nullptr;
  if (name) Cursor::set_cursor_image(name);
  uc8 = 0;
  ucc = 0;
  buffer = 1;
}

// Mech3.exe 1.2 @ 0x005a77f0
Cursor::~Cursor() {
  if (under) host().texture_free(under);
  under = nullptr;
}

// Mech3.exe 1.2 @ 0x005a7820
void Cursor::set_cursor_image(const char* name) {
  if (set_image(name)) grab();
}

// Mech3.exe 1.2 @ 0x005a7840
void Cursor::set_cursor_image(zimg::Texture* t) {
  if (set_image(t)) grab();
}

// Mech3.exe 1.2 @ 0x005a7860
void Cursor::set_capture(int32_t on) {
  capture = on;
  if (!on) {
    if (under) {
      host().texture_free(under);
      under = nullptr;
      set_background(nullptr, nullptr);
    }
    return;
  }
  if (!under) grab();
}

// Mech3.exe 1.2 @ 0x005a78b0
// A buffer the size of the cursor's image for the screen under it.
void Cursor::grab() {
  if (!capture || !image) return;
  if (under) host().texture_free(under);
  under = static_cast<zimg::Texture*>(std::calloc(1, sizeof(zimg::Texture)));  // 0x00517c10
  if (!under) return;
  under->width = image->width;  // 0x00517c90
  under->height = image->height;
  under->width_again = image->width;
  under->pixel_count = image->width * image->height;
  under->pixels = std::malloc(static_cast<size_t>(under->pixel_count) * 2);
  under->alpha = nullptr;  // 0x00517c70(0)
  under->flags |= zimg::kPixelsOwned;
  grab_at(get_x(), get_y());
}

// Mech3.exe 1.2 @ 0x005a7940
void Cursor::set_pos(int32_t px, int32_t py) {
  Image::set_pos(px, py);
  grab_at(x, y);
}

// Mech3.exe 1.2 @ 0x005a7970
void Cursor::grab_at(int32_t px, int32_t py) {
  if (!under) return;
  Rect r;
  r.left = px;
  r.top = py;
  r.right = px + image->width;
  r.bottom = py + image->height;
  if (host().screen_grab_rect(buffer, &r, under)) {
    Rect b;  // the grabbed part, relative to the cursor (clipped at the screen edge)
    b.left = r.left - px;
    b.top = r.top - py;
    b.right = r.right - px;
    b.bottom = r.bottom - py;
    set_background(under, &b);
  } else {
    set_background(nullptr, &g_zero_rect);
  }
}

// Mech3.exe 1.2 @ 0x005a7a20
void Cursor::draw() {
  if (!can_present()) return;
  host().blit(image, x < 0 ? 0 : x, y < 0 ? 0 : y, 0, &bg_rect);
}

// Mech3.exe 1.2 @ 0x005a7a50
void Cursor::erase() {
  if (!can_present()) return;
  if (background) host().blit(background, x < 0 ? 0 : x, y < 0 ? 0 : y, 0, &bg_rect);
}

// ---- Movie2D ----

// Mech3.exe 1.2 @ 0x005a7a90
Movie2D::Movie2D() : Object2D(0, 0) {}

// Mech3.exe 1.2 @ 0x005a7ae0
Movie2D::~Movie2D() {
  if (stream) {
    host().unported("0x0059a530", {stream});  // the zFMV stream's close, then delete
    stream = 0;
  }
}

// Mech3.exe 1.2 @ 0x005a7b50 (not ported: opens a zFMV stream, 0x0059a4a0)
void Movie2D::open(const char* name) { stream = host().unported("0x005a7b50", {name}); }

// Mech3.exe 1.2 @ 0x005a7c30 (not ported)
void Movie2D::set_color_key(uint16_t c) { host().unported("0x005a7c30", {c}); }

// Mech3.exe 1.2 @ 0x005a7d10 (not ported)
void Movie2D::start() { host().unported("0x005a7d10", {}); }

// ---- Ripple ----

// Mech3.exe 1.2 @ 0x005a53e0
Ripple::Ripple() : Object2D(0, 0) {}

// Mech3.exe 1.2 @ 0x005a5440
void Ripple::draw_ripple() { host().ripple(x, y, inner, outer, amplitude, wavelength, phase, clip); }

}  // namespace ztwod
