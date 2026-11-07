// C:\zipper\GameZMech3\zTwoD\twod_wintext.cpp: text in a Windows font,
// rendered by GDI into a 16-bit image and blitted (FontText, 0x005a0200..
// 0x005a0710 by its error reports), with its neighbours: the bitmap-font text
// it extends (TextBase, 0x005a2da0..) and the flashing Text (0x005a15a0..).
// docs/twod.md, Text. FIX (A): the GDI calls go through Host (twod_host.h).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gamez/zimage/zimg_library.h"
#include "gamez/zreader/zreader.h"
#include "gamez/ztwod/twod.h"
#include "gamez/zvideo/zvid_format.h"

namespace ztwod {

namespace {

// MSVCRT _vsnprintf: at most n bytes, no terminator when the text fills them.
void msvc_vsnprintf(char* dst, size_t n, const char* format, va_list args) {
  char tmp[0x1000];
  const int len = std::vsnprintf(tmp, sizeof tmp, format, args);
  if (len < 0) return;
  const size_t l = static_cast<size_t>(len) < sizeof tmp ? static_cast<size_t>(len) : sizeof tmp - 1;
  if (l < n) {
    std::memcpy(dst, tmp, l + 1);
  } else {
    std::memcpy(dst, tmp, n);
  }
}

// MSVCRT strncpy: pads with zeros to n.
void msvc_strncpy(char* dst, const char* src, size_t n) {
  size_t i = 0;
  for (; i < n && src[i]; ++i) dst[i] = src[i];
  for (; i < n; ++i) dst[i] = 0;
}

int msvc_strncmp(const char* a, const char* b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const unsigned char ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[i]);
    if (ca != cb) return ca < cb ? -1 : 1;
    if (!ca) return 0;
  }
  return 0;
}

int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

// Mech3.exe 1.2 @ 0x005a0d20
// ECX top, EDX bottom (COLORREFs): the blend at f, each channel truncated.
uint32_t blend(uint32_t top, uint32_t bottom, float f) {
  if (f < 0.001f) return top;
  if (f > 0.999f) return bottom;
  const float g = 1.0f - f;
  const uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(static_cast<float>(top & 0xff) * g +
                                                                static_cast<float>(bottom & 0xff) * f));
  const uint32_t gr = static_cast<uint32_t>(static_cast<int32_t>(
      static_cast<float>((top >> 8) & 0xff) * g + static_cast<float>((bottom >> 8) & 0xff) * f));
  const uint32_t b = static_cast<uint32_t>(static_cast<int32_t>(
      static_cast<float>((top >> 16) & 0xff) * g + static_cast<float>((bottom >> 16) & 0xff) * f));
  return ((b & 0xff) << 16 | (gr & 0xff) << 8) | (r & 0xff);
}

}  // namespace

// ---- TextBase ----

// Mech3.exe 1.2 @ 0x005a2da0
TextBase::TextBase(const char* format, int32_t px, int32_t py, Handle f) : Object2D(0, 0) {
  centered = 0;
  if (!format) {
    std::memset(text, 0, sizeof text);
  } else {
    std::snprintf(text, sizeof text, "%s", format);  // vsprintf with no arguments
    TextBase::dirty();
  }
  x = px;
  y = py;
  dirty();
  font = f;
  dirty();
  align = 0;
}

// Mech3.exe 1.2 @ 0x005a01f0
TextBase::~TextBase() = default;

// Mech3.exe 1.2 @ 0x005a2f40
void TextBase::set_text(const char* format, ...) {
  if (!format) {
    std::memset(text, 0, sizeof text);
    return;
  }
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof text, format, args);  // vsprintf, unbounded in the original
  va_end(args);
  if (centered) centre_x();
  dirty();
}

// Mech3.exe 1.2 @ 0x005a3010
int32_t TextBase::font_width() {
  int32_t w = 0, h = 0;
  host().font_measure(text, font, &w, &h);
  return w;
}

// Mech3.exe 1.2 @ 0x005a2fd0
void TextBase::update_bg_rect() {
  int32_t w = 0, h = 0;
  host().font_measure(text, font, &w, &h);
  bg_rect.right = bg_rect.left + w;
  bg_rect.bottom = bg_rect.top + h;
}

// Mech3.exe 1.2 @ 0x005a3040
void TextBase::centre_x() {
  const int32_t w = font_width();
  x = (centre_right - w - centre_left) / 2 + centre_left;
  if (background) {
    bg_rect.left = x;
    bg_rect.top = y;
    update_bg_rect();
  }
}

// Mech3.exe 1.2 @ 0x005a3080
void TextBase::draw() {
  erase();
  if (!text[0]) return;
  if (align) {
    int32_t w = font_width();
    if (align == 1) w >>= 1;
    x -= w;
    if (can_present()) host().font_draw(text, x, y, font);
    x += w;
    return;
  }
  if (can_present()) host().font_draw(text, x, y, font);
}

// Mech3.exe 1.2 @ 0x005a2f90
void TextBase::set_background(zimg::Texture* image, const Rect* r) {
  background = image;
  if (r) bg_rect = *r;
  dirty();
}

// Mech3.exe 1.2 @ 0x005a30f0
int32_t TextBase::hit(int32_t px, int32_t py) {
  if ((flags & kHidden) || x > px || y > py) return 0;
  int32_t w = 0, h = 0;
  host().font_measure(text, font, &w, &h);
  return px <= x + w && py <= y + h ? 1 : 0;
}

// ---- FontText ----

// Mech3.exe 1.2 @ 0x005a00c0
FontText::FontText(const char* format, int32_t px, int32_t py) : TextBase(format, px, py, 0) {
  hfont = host().GetStockObject(10);  // OEM_FIXED_FONT until a font is set
}

// Mech3.exe 1.2 @ 0x005a05a0
FontText::~FontText() {
  if (image) {
    host().texture_free(image);
    image = nullptr;
  }
  delete_font();
}

// Mech3.exe 1.2 @ 0x005a0610
void FontText::delete_font() {
  if (named) {
    host().font_release(named);
    named = 0;
    hfont = 0;
    return;
  }
  if (hfont) {
    host().DeleteObject(hfont);
    hfont = 0;
  }
}

// Mech3.exe 1.2 @ 0x00409b30
void FontText::dirty() {
  needs_render = 1;
  Object2D::dirty();
}

// Mech3.exe 1.2 @ 0x00409c80
void FontText::set_fixed_rect(const Rect* r) {
  fixed = 1;
  fixed_rect = *r;
}

// Mech3.exe 1.2 @ 0x005a0e20
bool FontText::measure_prefix(int32_t n, Rect* r) {
  bool ok = false;
  const Handle dc = host().CreateCompatibleDC(0);
  if (!dc) return false;
  const Handle old = host().SelectObject(dc, hfont);
  if (n < 1) {
    if (host().DrawTextA(dc, "W", -1, r, 0x400)) {  // DT_CALCRECT: the line's height
      ok = true;
      r->right = r->left;
    }
  } else {
    std::string s(text);
    if (n <= static_cast<int32_t>(s.size())) {
      s.resize(static_cast<size_t>(n));
      if (host().DrawTextA(dc, s.c_str(), -1, r, 0x400)) ok = true;
    }
  }
  host().SelectObject(dc, old);
  host().DeleteDC(dc);
  return ok;
}

// Mech3.exe 1.2 @ 0x005a1380
int32_t FontText::text_height() {
  if (needs_render) render();
  return height - leading;
}

// Mech3.exe 1.2 @ 0x005a10b0
const char* FontText::rendered_text() {
  if (needs_render) render();
  return rendered;
}

// Mech3.exe 1.2 @ 0x005a13b0
void FontText::rect_at(Rect* out) {
  Object2D::rect_at(out);
  if (needs_render) render();
  out->right = width + out->left;
  out->bottom = text_height() + out->top;
}

// Mech3.exe 1.2 @ 0x005a1040
int32_t FontText::hit(int32_t px, int32_t py) {
  if ((flags & kHidden) || x > px || y > py) return 0;
  if (needs_render) render();
  if (px >= width + x) return 0;
  if (py >= text_height() + y) return 0;
  return 1;
}

// Mech3.exe 1.2 @ 0x005a10d0
// Erases its old box, then blits the text image, shifted left by half or all
// of its width when centred or right-aligned (the box's own width for the erase).
void FontText::draw() {
  if (needs_render) render();
  if (!image) return;
  if (!text[0]) {
    erase();
    return;
  }
  if (align) {
    if (needs_render) render();
    int32_t box = bg_rect.right - bg_rect.left;
    int32_t w = width;
    if (align == 1) {
      box >>= 1;
      w >>= 1;
    }
    x -= box;
    erase();
    x += box - w;
    if (can_present()) host().blit(image, x, y, 0, &text_rect);
    x += w;
    return;
  }
  erase();
  if (can_present()) host().blit(image, x, y, 0, &text_rect);
}

void FontText::changed_text() {
  if (msvc_strncmp(rendered, text, 0x200) != 0) {
    if (centered) centre_x();
    dirty();
    needs_render = 1;
    msvc_strncpy(rendered, text, 0x200);
  }
}

// Mech3.exe 1.2 @ 0x005a11b0
void FontText::set_text(const char* format, ...) {
  if (!format) {
    std::memset(text, 0, sizeof text);
    needs_render = 1;
    return;
  }
  va_list args;
  va_start(args, format);
  msvc_vsnprintf(text, 0x200, format, args);
  va_end(args);
  changed_text();
}

// Mech3.exe 1.2 @ 0x00409bd0
void FontText::set_text_id(uint32_t id, ...) {
  va_list args;
  va_start(args, id);
  set_text_id_v(id, args);
  va_end(args);
}

// Mech3.exe 1.2 @ 0x00409bf0: the message is passed as a format.
void FontText::set_text_id_v(uint32_t id, va_list args) {
  char buf[0x200];
  if (host().format_message(buf, sizeof buf, id, args)) set_text(buf);
}

// Mech3.exe 1.2 @ 0x005a1250
void FontText::set_text_v(const char* format, va_list args) {
  if (!format) {
    std::memset(text, 0, sizeof text);
    needs_render = 1;
    return;
  }
  msvc_vsnprintf(text, 0x200, format, args);
  changed_text();
}

// Mech3.exe 1.2 @ 0x005a12f0
void FontText::set_text_plain(const char* s) {
  if (!s) {
    std::memset(text, 0, sizeof text);
    needs_render = 1;
    return;
  }
  msvc_strncpy(text, s, 0x200);
  changed_text();
}

// Mech3.exe 1.2 @ 0x00409c40
void FontText::set_message(uint32_t id) { set_text_plain(host().message_text(id)); }

// Mech3.exe 1.2 @ 0x005a0710
void FontText::create_font(const char* face, int32_t h, int32_t weight, int32_t w, uint32_t italic,
                           uint32_t charset, uint32_t pitch) {
  delete_font();
  hfont = host().CreateFontA(-h, w, 0, 0, weight, italic, 0, 0, charset, 4, 0, 1, pitch, face);
  if (!hfont) host().GetLastError();  // reported to the (empty) error sink
  needs_render = 1;
}

// Mech3.exe 1.2 @ 0x005a0680
bool FontText::set_named_font(Handle f, bool force) {
  if (!force && f == named) return false;
  delete_font();
  if (f) {
    host().font_addref(f);
    named = f;
    const NamedFont nf = host().font_fields(f);
    hfont = nf.hfont;
    color_top = nf.color_top;
    color_bottom = nf.color_bottom;
    shadow_dx = 1;
    shadow = nf.shadow;
    shadow_dy = 1;
    align = nf.align;
  }
  needs_render = 1;
  return true;
}

// Mech3.exe 1.2 @ 0x005a0f00
// bg_rect from the text's extent: the fixed box, or GetTextExtentPoint32 placed
// by the alignment and grown by the shadow.
void FontText::measure() {
  const int32_t len = static_cast<int32_t>(std::strlen(text));
  if (fixed) {
    bg_rect.left = x;
    bg_rect.top = y;
    bg_rect.right = fixed_rect.right + x;
    bg_rect.bottom = fixed_rect.bottom + y;
    return;
  }
  const Handle dc = host().CreateCompatibleDC(0);
  if (!dc) return;
  const Handle old = host().SelectObject(dc, hfont);
  int32_t cx = 0, cy = 0;
  if (host().GetTextExtentPoint32A(dc, text, len, &cx, &cy)) {
    int32_t left;
    if (!align) {
      left = bg_rect.left;
    } else {
      if (needs_render) render();
      if (align == 1) {
        left = (bg_rect.right - bg_rect.left) / 2 - (width >> 1) + bg_rect.left;
      } else {
        left = bg_rect.right - width;
      }
    }
    bg_rect.left = left;
    bg_rect.right = cx + left;
    bg_rect.bottom = bg_rect.top + cy;
    if (shadow) {
      bg_rect.bottom += iabs(shadow_dy);
      bg_rect.right += iabs(shadow_dx);
    }
  }
  host().SelectObject(dc, old);
  host().DeleteDC(dc);
}

// Mech3.exe 1.2 @ 0x005a0790
// GDI draws the text (and its shadow, offset by shadow_dx/dy) into a cleared
// 16-bit image grown to fit; with two colours it is drawn white and each row's
// white pixels are tinted by the blend for that row's height in the line.
void FontText::render() {
  const int32_t len = static_cast<int32_t>(std::strlen(text));
  // A mutex (0x00731ab8) around the whole render, for the movie thread: not
  // needed here, the port renders on one thread.
  if (len == 0) {
    text_rect = Rect{};
    width = 0;
    height = 0;
    needs_render = 0;
    return;
  }
  Host& h = host();
  const Handle dc = h.CreateCompatibleDC(0);
  if (!dc) {
    needs_render = 0;
    return;
  }
  const Handle old = h.SelectObject(dc, hfont);
  uint32_t format;
  int32_t ok;
  if (fixed) {
    format = 0x10;  // DT_WORDBREAK
    text_rect = fixed_rect;
    ok = 1;
  } else {
    format = 0;
    ok = h.DrawTextA(dc, text, -1, &text_rect, 0x400);  // DT_CALCRECT
  }
  if (!ok) {
    h.GetLastError();
  } else {
    if (shadow) {
      text_rect.bottom += iabs(shadow_dy);
      text_rect.right += iabs(shadow_dx);
    }
    width = text_rect.right - text_rect.left;
    if (max_width > 0 && width > max_width) {
      width = max_width;
      text_rect.right = max_width + text_rect.left;
    }
    height = text_rect.bottom - text_rect.top;
    if (!image || width > image->width || height > image->height) {
      if (image) h.texture_free(image);
      image = static_cast<zimg::Texture*>(std::calloc(1, sizeof(zimg::Texture)));  // 0x00517c10
      image->flags = 3;  // 0x00517c60: two bytes, alpha
      image->width = static_cast<int16_t>(width);  // 0x00517c90
      image->height = static_cast<int16_t>(height);
      image->width_again = image->width;
      image->pixel_count = image->width * image->height;
      const int32_t bpp = zimg::bytes_per_pixel(*image);
      image->pixels = std::malloc(static_cast<size_t>(bpp * width * height));
      image->alpha = nullptr;  // 0x00517c70(0)
      image->flags |= zimg::kPixelsOwned;
    }
    if (image) {
      std::memset(image->pixels, 0, static_cast<size_t>(zimg::bytes_per_pixel(*image) * image->pixel_count));
      if (const Handle idc = h.image_dc(image)) {
        Rect main = text_rect;
        const Handle prev = h.SelectObject(idc, hfont);
        if (shadow) {
          Rect sh = text_rect;
          if (shadow_dx > 0) {
            sh.left += shadow_dx;
            sh.right += shadow_dx;
          } else {
            main.left -= shadow_dx;
            main.right -= shadow_dx;
          }
          if (shadow_dy > 0) {
            sh.top += shadow_dy;
            sh.bottom += shadow_dy;
          } else {
            main.top -= shadow_dy;
            main.bottom -= shadow_dy;
          }
          h.SetTextColor(idc, shadow_color);
          if (bk_mode == 2) h.SetBkColor(idc, 0x20);
          h.SetBkMode(idc, bk_mode);
          h.DrawTextA(idc, text, -1, &sh, format);
        }
        h.SetTextColor(idc, color_top != color_bottom ? 0xffffff : color_top);
        if (bk_mode == 2) h.SetBkColor(idc, bk_color);
        h.SetBkMode(idc, bk_mode);
        h.DrawTextA(idc, text, -1, &main, format);
        h.SelectObject(idc, prev);
        h.image_dc_release(image, idc);
      }
      TextMetrics tm;
      if (h.GetTextMetricsA(dc, &tm)) {
        if (color_top != color_bottom) {
          const uint16_t white = zvid::pack_bytes(0xff, 0xff, 0xff);
          uint16_t* p = static_cast<uint16_t*>(image->pixels);
          for (int32_t row = 0; row < image->height; ++row) {
            const int32_t r = shadow ? shadow_dy + row : row;
            const int32_t line = r % (tm.height + tm.external_leading);
            const float f = static_cast<float>(line - tm.internal_leading) /
                            static_cast<float>(tm.ascent - tm.internal_leading);
            const uint32_t c = blend(color_top, color_bottom, f);
            const uint16_t tint = zvid::pack_bytes(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
            for (int32_t col = 0; col < image->width; ++col, ++p) {
              if (*p == white) *p = tint;
            }
          }
        }
        leading = tm.external_leading;
      }
    }
  }
  h.SelectObject(dc, old);
  h.DeleteDC(dc);
  needs_render = 0;
}

// Mech3.exe 1.2 @ 0x005a1400
// TEXT (a message key), WORDWRAP (w h), the font by name ("font" or "FONT"
// string) or by the dialog's number (FONT n), POSITION.
bool FontText::read(zrdr::Node* n, Dialog* d) {
  if (!n) return false;
  if (zrdr::Node* t = zrdr::find(n, "TEXT")) set_text(host().message(t->list[1].s));
  if (zrdr::Node* w = zrdr::find(n, "WORDWRAP")) {
    Rect r;
    r.right = w->list[1].i;
    r.bottom = w->list[2].i;
    set_fixed_rect(&r);
  }
  const char* fname = zrdr::get_string(n, "font");
  if (!fname) fname = zrdr::get_string(n, "FONT");
  if (fname) {
    set_font(host().font_find(fname));
  } else {
    int32_t id = 0;
    if (zrdr::get_int(n, "FONT", &id)) {
      FontEntry& f = d->fonts[id];
      if (f.used) {
        align = f.align;
        create_font(f.face, f.height, f.weight, 0, 0, 0, 2);
        if (f.color != color_top || f.color != color_bottom) {
          color_top = f.color;
          color_bottom = f.color;
          needs_render = 1;
        }
        shadow_dx = 1;
        shadow = f.shadow;
        shadow_dy = 1;
        bk_mode = f.mode;
        bk_color = f.color2;
      }
    }
  }
  Object2D::read(n, d);
  return true;
}

// ---- Text ----

// Mech3.exe 1.2 @ 0x005a15a0
Text::Text() : FontText(nullptr, 0, 0) {}

// Mech3.exe 1.2 @ 0x00409ef0 (0x005a05a0)
Text::~Text() = default;

// Mech3.exe 1.2 @ 0x005a1690
void Text::set_flash_rate(float period) {
  flash_on = 1;
  if (period > 0.0f) flash_rate = period * 0.5f;
  phase = 1;
  flash_timer = flash_rate;
}

// Mech3.exe 1.2 @ 0x005a16e0
void Text::set_flash(uint32_t color, float period) {
  if (flash_mode == 2) return;
  set_flash_rate(period);
  flash_mode = 2;
  alt_top = color;
  alt_bottom = color;
}

// Mech3.exe 1.2 @ 0x005a1720
void Text::stop_flash() {
  if (!flash_mode) return;
  if (phase == -1 && flash_mode >= 2) {
    const uint32_t t = color_top, b = color_bottom;
    color_top = alt_top;
    color_bottom = alt_bottom;
    alt_top = t;
    alt_bottom = b;
    needs_render = 1;
  }
  flash_mode = 0;
  flash_on = 0;
}

// Mech3.exe 1.2 @ 0x005a1790
// Text draws every frame while shown (no dirty flags); a flash blinks it
// (mode 1) or swaps in the flash colours (2, 3) every half period.
void Text::update(float dt) {
  if (flags & kHidden) return;
  if (flags & kTimed) {
    timer -= dt;
    if (timer <= 0.0f) hide();
  }
  if (!flash_on || (flags & kHidden)) {
    FontText::draw();
    return;
  }
  const float t = flash_timer - dt;
  flash_timer = t;
  switch (flash_mode) {
    case 1:
      if (t < 0.0f) {
        flash_timer = flash_rate + t;
        needs_render = 1;
        phase = -phase;
      }
      if (phase == 1) FontText::draw();
      return;
    case 2:
    case 3:
      if (t < 0.0f) {
        flash_timer = flash_rate;
        needs_render = 1;
        phase = -phase;
        const uint32_t top = color_top, bottom = color_bottom;
        color_top = alt_top;
        color_bottom = alt_bottom;
        alt_top = top;
        alt_bottom = bottom;
      }
      FontText::draw();
      return;
    default:
      FontText::draw();
      return;
  }
}

}  // namespace ztwod
