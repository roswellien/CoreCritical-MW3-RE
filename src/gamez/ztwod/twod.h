#pragma once
#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>

#include "gamez/ztwod/twod_host.h"
#include "gamez/ztwod/presentation.h"

namespace zimg { struct Texture; }
namespace zrdr { struct Node; }

// zTwoD, the engine's 2D objects (C:\zipper\GameZMech3\zTwoD\twod_*.cpp,
// 0x0059b2f0..0x005a7e10; docs/twod.md): menus, dialogs, buttons, text, the
// software cursor. Objects draw by blitting zImage images into the locked back
// buffer; each remembers the static background behind it to erase itself, so a
// screen redraws only what changed. Fields keep the original offsets in their
// comments; virtual methods are in vtable order, slot in the comment.
namespace ztwod {

class Layer;
class Window;
class Dialog;
class Text;

// Object flags (+0x0c).
enum : uint32_t {
  kTimed = 0x01,    // hides itself when the timer runs out
  kManaged = 0x02,  // drawn only when dirty; without it, every frame
  kDirty = 0x04,    // draw (or erase) once
  kDirty2 = 0x08,   // the same for the second buffer when page flipping
  kHidden = 0x10,
};
// The bits dirty() sets: 4, or 0xc when page flipping (0x005f49b8, 0x0059fb80).
uint32_t& dirty_bits();
void set_page_flip(bool flipping);  // 0x0059fb80

// Mouse bits of an object (+0x30, 16 bits).
enum : uint16_t { kHover = 1, kPressed = 2 };

// The base object (vtable 0x005bc0e0), 0x34 bytes.
class Object2D {
 public:
  Object2D* next = nullptr;          // +0x04  in the owner's list
  Layer* owner = nullptr;            // +0x08
  uint32_t flags = 0;                // +0x0c
  float timer = 0.0f;                // +0x10  seconds left when kTimed
  int32_t x = 0, y = 0;              // +0x14, +0x18
  zimg::Texture* background = nullptr;  // +0x1c  what erase() blits back
  Rect bg_rect;                      // +0x20  its part to blit
  uint16_t mouse = 0;                // +0x30  kHover, kPressed
  // Host-only presentation policy, independent of native visibility/timers.
  bool presentation_visible = true;
  bool can_present() const { return presentation_visible && presentation_output_enabled(); }
  void set_presentation_visible(bool visible) {
    if (presentation_visible != visible) { presentation_visible = visible; dirty(); }
  }

  Object2D(int32_t x, int32_t y);    // 0x0059fda0
  Object2D(const Object2D& o);       // 0x0059fdf0 (list links cleared)
  Object2D& operator=(const Object2D& o);  // 0x0059fe50
  virtual ~Object2D();               // +0x00  0x00409af0
  virtual void draw();               // +0x04  0x00409a10: erase()
  virtual void erase();              // +0x08  0x00409a20
  virtual void set_pos(int32_t px, int32_t py);  // +0x0c  0x00409a40
  virtual void set_x(int32_t v);     // +0x10  0x00409a60
  virtual void set_y(int32_t v);     // +0x14  0x00409a70
  virtual void set_background(zimg::Texture* image, const Rect* r);  // +0x18  0x0059ff60
  virtual void set_bg_rect(const Rect* r);  // +0x1c  0x0059ff80
  virtual void dirty();              // +0x20  0x0059ff00
  virtual void update(float dt);     // +0x24  0x0059ffb0
  virtual void draw3d(float) {}      // +0x28  0x004a5bc0: the Direct3D pass's draw (the HUD's, hud.md)
  virtual Rect* get_rect() { return nullptr; }  // +0x2c  0x0040bae0
  // Mouse events from Window::update (0x005a2bb0); the release build's are empty.
  virtual void on_left_up() {}       // +0x30
  virtual void on_right_up() {}      // +0x34
  virtual void on_hover() {}         // +0x38
  virtual void on_enter() {}         // +0x3c
  virtual void on_leave() {}         // +0x40
  virtual void on_press() {}         // +0x44
  virtual void on_release() {}       // +0x48
  virtual void on_drag(int32_t, int32_t) {}  // +0x4c  0x0040baf0
  virtual void on_click() {}         // +0x50
  virtual bool accepts(Window*, bool) { return true; }  // +0x54  0x00409a80
  virtual void after_input(Window*, bool) {}            // +0x58  0x0040baf0
  virtual int32_t hit(int32_t px, int32_t py);  // +0x5c  0x0059fd50
  virtual void show_for(float seconds);  // +0x60  0x0059ff10: < 0 shows for good
  virtual void hide();               // +0x64  0x0059ff50
  virtual int32_t get_x() { return x; }  // +0x68  0x0040a8a0
  virtual int32_t get_y() { return y; }  // +0x6c  0x0040a890
  virtual void set_fixed_rect(const Rect*) {}  // +0x70  0x004a5bc0
  virtual void rect_at(Rect* out);   // +0x74  0x005a0090: a point at (get_x, get_y)
  virtual bool read(zrdr::Node* node, Dialog* dialog);  // +0x78  0x0059feb0: POSITION
  // Not a virtual of the original: BindPrimitive's MOUSETRAP writes its
  // rectangle over the four dwords at +0x34 of any object; only classes that
  // keep a rectangle there (none in the shipped dialogs) take it.
  virtual void set_trap_rect(const Rect&) {}
};

// A list of objects updated in order (vtable 0x005cfd10), 0x10 bytes.
class Layer {
 public:
  int32_t active = 0;                // +0x04
  Object2D* head = nullptr;          // +0x08
  Object2D* tail = nullptr;          // +0x0c

  Layer();                           // 0x0059fba0
  virtual ~Layer();                  // 0x0059fbd0
  virtual void update(float dt);     // +0x00  0x0059fd20
  virtual void set_active(int32_t on);  // +0x04  0x0040e820

  bool add(Object2D* o);             // 0x0059fbe0
  bool find(Object2D* o, Object2D** prev);  // 0x0059fc30
  bool remove(Object2D* o);          // 0x0059fc80
  void set_flags_all(uint32_t f);    // 0x0059fcf0: keeps each one's kHidden
  void dirty_all();                  // 0x0059fa60
};

// A layer that routes the mouse to its objects and draws a cursor
// (vtable 0x005cfed0), 0x58 bytes.
class Window : public Layer {
 public:
  Object2D* cursor = nullptr;        // +0x10
  MouseState m{};                    // +0x14  copied each update
  uint32_t press_mask;               // +0x50  left-button bits that press (1: pressed this frame)
  uint32_t release_mask;             // +0x54  left-button bits that release outside

  Window(uint32_t press, uint32_t release);  // 0x005a2b50
  ~Window() override;                // 0x005a2b80
  void update(float dt) override;    // +0x00  0x005a2bb0
  void set_active(int32_t on) override;  // +0x04  0x005a2ba0
  void set_cursor(Object2D* c) { cursor = c; }  // 0x005a2b90
};

// A queued draw of an image at another place (Image +0x4c).
struct QueuedDraw {
  int32_t count = 0;                 // +0x00  frames left
  int32_t x = 0, y = 0;              // +0x04
  Rect src;                          // +0x0c
};

// An image (vtable 0x005cf8c8), 0xbc bytes.
class Image : public Object2D {
 public:
  int32_t owned = 0;                 // +0x34  released with the object
  int32_t queued = 0;                // +0x38  QueuedDraw slots in use
  zimg::Texture* image = nullptr;    // +0x3c
  uint16_t u40 = 0;                  // +0x40  1 for controls
  const Rect* src = nullptr;         // +0x44  the part to draw, null: all
  int32_t center = 0;                // +0x48  x, y are the centre
  QueuedDraw queue[4];               // +0x4c

  explicit Image(int32_t center);    // 0x0059b970
  Image(const char* name, int32_t px, int32_t py, int32_t center);   // 0x0059b830
  Image(zimg::Texture* t, int32_t px, int32_t py, int32_t center);   // 0x0059b8e0
  ~Image() override;                 // 0x0059b9c0
  void draw() override;              // +0x04  0x0059bb70
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x0059bab0
  int32_t get_x() override;          // +0x68  0x00409cb0: the centre when center
  int32_t get_y() override;          // +0x6c  0x00409cf0
  bool read(zrdr::Node* node, Dialog* dialog) override;  // +0x78  0x0059ba40
  virtual void update_bg_rect();     // +0x7c  0x00409d30

  void release();                    // 0x0059ba10
  zimg::Texture* set_image(const char* name);   // 0x0059bb10
  zimg::Texture* set_image(zimg::Texture* t);   // 0x0059bb50 (not owned)
};

// A push button (vtable 0x005cf948), 0x14c bytes: images, sounds and labels
// for its normal, rollover, activate (pressed) and disabled looks.
class Control : public Image {
 public:
  int32_t base_x = 0, base_y = 0;    // +0xbc  the dialog origin plus POSITION
  int32_t enabled = 1;               // +0xc4
  Dialog* dialog = nullptr;          // +0xc8
  Rect bounds;                       // +0xcc  get_rect's
  zimg::Texture* img_normal = nullptr;     // +0xdc
  zimg::Texture* img_disabled = nullptr;   // +0xe0
  zimg::Texture* img_rollover = nullptr;   // +0xe4
  int32_t snd_rollover = 0;          // +0xe8
  int32_t rollover_playing = 0;      // +0xec
  float vol_rollover = 1.0f;         // +0xf0
  zimg::Texture* img_activate = nullptr;   // +0xf4
  int32_t snd_activate = 0;          // +0xf8
  int32_t activate_playing = 0;      // +0xfc
  float vol_activate = 1.0f;         // +0x100
  int32_t snd_disable = 0;           // +0x104  never initialised by the constructor
  float vol_disable = 0.0f;          // +0x108  (likewise)
  std::vector<Text*> labels;         // +0x10c  shown normally
  std::vector<Text*> rollover_labels;  // +0x11c
  std::vector<Text*> activate_labels;  // +0x12c
  std::vector<Text*> disable_labels;   // +0x13c

  Control();                         // 0x0059bbe0
  ~Control() override;               // 0x0059bdd0
  void dirty() override;             // +0x20  0x0059c020
  Rect* get_rect() override;         // +0x2c  0x0059c310
  void on_left_up() override;        // +0x30  0x0059c960: activate
  void on_enter() override;          // +0x3c  0x0059c650
  void on_leave() override;          // +0x40  0x0059c8b0
  int32_t hit(int32_t px, int32_t py) override;  // +0x5c  0x0059c050
  void show_for(float seconds) override;  // +0x60  0x0059c260
  void hide() override;              // +0x64  0x0059c280
  virtual void refresh();            // +0x80  0x0059c790: the enabled look
  virtual bool read_control(zrdr::Node* node, Dialog* dialog);  // +0x84  0x0059cdc0
  virtual void after_bind() {}       // +0x88  0x0049bfd0
  // 0x00409e60 (inline, with the briefing): returns the old value.
  int32_t enable(int32_t on) {
    const int32_t old = enabled;
    enabled = on;
    refresh();
    return old;
  }
};

// A two-state button (vtable 0x005cfaa8), 0x164 bytes.
class CheckButton : public Control {
 public:
  Text* text = nullptr;              // +0x14c  CHECKED's or DISABLE_SEL's TEXT, owned
  int32_t checked = 0;               // +0x150
  zimg::Texture* img_disabled_checked = nullptr;    // +0x154
  zimg::Texture* img_disabled_unchecked = nullptr;  // +0x158
  zimg::Texture* img_unchecked = nullptr;           // +0x15c
  zimg::Texture* img_checked = nullptr;             // +0x160

  CheckButton();                     // 0x0059d870
  ~CheckButton() override;           // 0x0059d8d0
  Rect* get_rect() override { return &bounds; }     // +0x2c  0x0059d9b0
  void on_left_up() override;        // +0x30  0x0059db90: toggles
  void on_enter() override;          // +0x3c  0x0059db00
  void on_leave() override;          // +0x40  0x0059db50
  void show_for(float seconds) override;  // +0x60  0x0059d960
  void hide() override;              // +0x64  0x0059d990
  void refresh() override;           // +0x80  0x0059d9c0
  bool read_control(zrdr::Node* node, Dialog* dialog) override;  // +0x84  0x0059dc40
  virtual int32_t set_checked(int32_t on);  // +0x8c  0x0059dbc0: returns the old state
};

// A bar filled to a value (vtable 0x005cfb38), 0x188 bytes: FILLBITMAP over
// the button's image, cut at the value. The game's subclasses take the value
// from a click (set_from_mouse) and set it with set_value.
class Slider : public Control {
 public:
  float value = 0.0f;                // +0x14c  0..1 (0x0059fad0 stores it raw)
  zimg::Texture* fill = nullptr;     // +0x150
  Rect fill_src;                     // +0x154
  int32_t fill_dx = 0, fill_dy = 0;  // +0x164
  zimg::Texture* empty = nullptr;    // +0x16c  the button's own image
  Rect empty_src;                    // +0x170
  int32_t empty_dx = 0, empty_dy = 0;  // +0x180

  Slider();                          // 0x0059e0f0
  ~Slider() override;                // 0x0059e170
  void draw() override;              // +0x04  0x0059e1c0
  bool read_control(zrdr::Node* node, Dialog* dialog) override;  // +0x84  0x0059e260
  virtual void set_raw(float v);     // +0x8c  0x0059fad0
  void set_from_mouse();             // 0x0059e2f0 (a subclass's +0x30)
  void set_value(float v);           // 0x0059e350 (a subclass's +0x8c)
};

// Text drawn with the HUD's bitmap fonts (vtable 0x005cfed8), 0x248 bytes.
class TextBase : public Object2D {
 public:
  char text[0x200] = {};             // +0x34
  Handle font = 0;                   // +0x234  a zFont (fonts.zrd bitmap font)
  int32_t centered = 0;              // +0x238  centre between centre_left/right
  int32_t centre_left = 0, centre_right = 0;  // +0x23c, +0x240
  int32_t align = 0;                 // +0x244  0 left, 1 centre, 2 right

  TextBase(const char* format, int32_t px, int32_t py, Handle font);  // 0x005a2da0
  TextBase(const TextBase& o);       // 0x005a2e30
  void assign(const TextBase& o);    // 0x005a2ed0 (the list links cleared)
  ~TextBase() override;              // 0x005a01f0
  void draw() override;              // +0x04  0x005a3080: zFont's text draw
  void set_background(zimg::Texture* image, const Rect* r) override;  // +0x18  0x005a2f90
  int32_t hit(int32_t px, int32_t py) override;  // +0x5c  0x005a30f0
  // +0x7c (0x005a2f40): the format and its arguments, sprintf'd (cdecl).
  virtual void set_text(const char* format, ...);
  int32_t font_width();              // 0x005a3010
  void update_bg_rect();             // 0x005a2fd0: bg_rect's size from the text
  void centre_x();                   // 0x005a3040
};

// Text drawn with a Windows font into a 16-bit image (vtable 0x005cfd28),
// 0x4b0 bytes. twod_wintext.cpp.
class FontText : public TextBase {
 public:
  zimg::Texture* image = nullptr;    // +0x248  the rendered text
  uint32_t color_top = 0xffffff;     // +0x24c  COLORREF; a gradient to color_bottom
  uint32_t color_bottom = 0xffffff;  // +0x250
  uint32_t shadow_color = 0x141414;  // +0x254
  Handle hfont = 0;                  // +0x258
  uint32_t u25c = 0;                 // +0x25c  never set here
  char rendered[0x200] = {};         // +0x260  the text the image holds
  int32_t width = 0, height = 0;     // +0x460  of the image's text
  int32_t shadow = 0;                // +0x468
  int32_t bk_mode = 1;               // +0x46c  1 TRANSPARENT, 2 OPAQUE
  uint32_t bk_color = 0;             // +0x470
  int32_t needs_render = 1;          // +0x474
  int32_t leading = 0;               // +0x478  external leading
  int32_t fixed = 0;                 // +0x47c  draw into fixed_rect, wrapping
  Rect fixed_rect;                   // +0x480
  Rect text_rect;                    // +0x490
  int32_t shadow_dx = -1, shadow_dy = -1;  // +0x4a0
  int32_t max_width = -1;            // +0x4a8
  Handle named = 0;                  // +0x4ac  a fonts.zrd font, referenced

  FontText(const char* format, int32_t px, int32_t py);  // 0x005a00c0
  FontText(const FontText& o);       // 0x005a0200: the font copied (GetObjectA + CreateFontIndirectA)
  void assign(const FontText& o);    // 0x005a03f0: likewise; the old image and font are dropped, not freed
  ~FontText() override;              // 0x005a05a0
  void draw() override;              // +0x04  0x005a10d0
  void set_pos(int32_t px, int32_t py) override { x = px; y = py; }  // +0x0c  0x00409b10
  void dirty() override;             // +0x20  0x00409b30
  int32_t hit(int32_t px, int32_t py) override;  // +0x5c  0x005a1040
  void set_fixed_rect(const Rect* r) override;   // +0x70  0x00409c80
  void rect_at(Rect* out) override;  // +0x74  0x005a13b0
  bool read(zrdr::Node* node, Dialog* dialog) override;  // +0x78  0x005a1400
  void set_text(const char* format, ...) override;     // +0x7c  0x005a11b0
  virtual void measure();            // +0x80  0x005a0f00: bg_rect from the text's extent
  virtual Handle get_hfont() { return hfont; }  // +0x84  0x00409b40
  virtual void create_font(const char* face, int32_t height, int32_t weight, int32_t width,
                           uint32_t italic, uint32_t charset, uint32_t pitch);  // +0x88  0x005a0710
  virtual bool set_named_font(Handle f, bool force);  // +0x8c  0x005a0680
  virtual void set_font(Handle f) { set_named_font(f, true); }  // +0x90  0x005a0660
  // +0x94 0x005961d0 (a LOGFONT): not ported (docs/twod.md).
  virtual void set_text_id(uint32_t id, ...);  // +0x98  0x00409bd0 (cdecl)
  virtual void set_text_id_v(uint32_t id, va_list args);  // +0x9c  0x00409bf0: a message as the format
  virtual void set_text_v(const char* format, va_list args);  // +0xa0  0x005a1250
  virtual void set_text_plain(const char* s);   // +0xa4  0x005a12f0
  virtual void set_message(uint32_t id);  // +0xa8  0x00409c40: a message's text, plain
  virtual void render();             // +0xac  0x005a0790

  void delete_font();                // 0x005a0610
  // 0x005a0e20: DrawTextA's box (DT_CALCRECT) of the first n characters into
  // r (its left and top in); n < 1 measures "W" and keeps the width 0. False
  // when no DC or DrawTextA fails, or n is past the text.
  bool measure_prefix(int32_t n, Rect* r);
  int32_t text_height();             // 0x005a1380: height - leading
  const char* rendered_text();       // 0x005a10b0: the text the image holds, rendered first
 protected:
  void changed_text();               // the common tail of the set_text family
};

// Text with a flash (vtable 0x005cf9d8), 0x4cc bytes.
class Text : public FontText {
 public:
  float flash_timer = 0.0f;          // +0x4b0  to the next half period
  float flash_rate = 0.35f;          // +0x4b4  a half period, seconds
  uint32_t alt_top = 0;              // +0x4b8  the colours swapped in (mode 2)
  uint32_t alt_bottom = 0;           // +0x4bc  (not set by the constructor)
  uint8_t flash_on = 0;              // +0x4c0
  int32_t flash_mode = 0;            // +0x4c4  1 blink, 2 colour swap
  int32_t phase = 1;                 // +0x4c8  1 or -1

  Text();                            // 0x005a15a0
  Text(const Text& o);               // 0x005a2ae0
  void assign(const Text& o);        // 0x005a2a70
  ~Text() override;                  // 0x00409ef0
  void update(float dt) override;    // +0x24  0x005a1790
  void set_flash_rate(float period); // 0x005a1690: half of it, when > 0
  void set_flash(uint32_t color, float period);  // 0x005a16e0
  void stop_flash();                 // 0x005a1720
};

// The exe's std::vector<Text> (HP STL, by value): its growth and its copy
// order kept, since every Text copy copies a GDI font (0x005a2700).
class TextVector {
 public:
  TextVector() = default;
  TextVector(const TextVector&) = delete;
  TextVector& operator=(const TextVector&) = delete;
  ~TextVector();
  size_t size() const { return static_cast<size_t>(last_ - first_); }
  Text& operator[](size_t i) { return first_[i]; }
  Text* begin() { return first_; }
  Text* end() { return last_; }
  void insert(Text* pos, size_t n, const Text& x);  // 0x005a2700
  void erase_to_end(Text* from);     // the inline erase(from, end)
 private:
  Text* first_ = nullptr;            // +0x04
  Text* last_ = nullptr;             // +0x08
  Text* end_ = nullptr;              // +0x0c
};

// Lines of text, each a Text, that scroll as lines are added (vtable
// 0x005cfe00), 0x4c8 bytes: the briefing's and the mech lab's text panels, the
// interp console. Its own text holds the font, colour and line height.
class TextBox : public FontText {
 public:
  int32_t current = 0;               // +0x4b0  the line written next
  uint8_t bottom_up = 0;             // +0x4b4  lines placed upward from the last
  uint8_t reposition = 0;            // +0x4b5  re-place the lines on the next update
  TextVector lines;                  // +0x4b8

  explicit TextBox(uint32_t count);  // 0x005a18f0
  ~TextBox() override;               // 0x005a1ac0
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x005a1ba0
  void update(float dt) override;    // +0x24  0x005a1ae0
  void create_font(const char* face, int32_t height, int32_t weight, int32_t width, uint32_t italic,
                   uint32_t charset, uint32_t pitch) override;  // +0x88  0x005a20a0
  void set_font(Handle f) override;  // +0x90  0x005a2160
  void set_text(const char* format, ...) override;           // +0x7c  0x005a1d70
  void set_text_v(const char* format, va_list args) override;  // +0xa0  0x005a1d90: a new line
  // +0x98 / +0x9c (0x005a1cd0, 0x005a1cf0: by a message id): not ported;
  // FontText's run instead.
  virtual void set_text_top(const char* format, ...);        // +0xb0  0x005a1e10
  virtual void set_text_top_v(const char* format, va_list args);  // +0xb4  0x005a1e30
  virtual void advance();            // +0xb8  0x005a1e80
  virtual void advance_down();       // +0xbc  0x005a1f90
  virtual void after_write(int32_t) {}  // +0xc0  0x004a5bc0

  void set_line_count(uint32_t n);   // 0x005a2320
  void clear();                      // 0x005a24e0
  void clear_lines(int32_t from, int32_t to);  // 0x005a2520
  void set_color(uint32_t c);        // 0x005a21e0
  Text& line();                      // 0x005a2280
  void new_line();                   // 0x005a22a0
  void append(const char* s);        // 0x005a22f0
  int32_t total_height();            // 0x005a25f0
  int32_t max_width();               // 0x005a2640
};

// An entry of a ListBox (vtable 0x005cfbc8), 0x4b8 bytes; also the box's
// shown choice.
class ListItem : public FontText {
 public:
  int32_t index = 0;                 // +0x4b0  (not set by the constructor)
  class ListBox* box = nullptr;      // +0x4b4  (likewise)

  ListItem();                        // 0x0059e9a0
  void draw() override;              // +0x04  0x0059faf0: and its box from the text
  Rect* get_rect() override { return &fixed_rect; }  // +0x2c  0x0048e540
  void on_left_up() override;        // +0x30  0x0059ec20: its box selects it
};

// A list of choices (vtable 0x005be308), 0x650 bytes: a check button with
// the chosen entry shown (+0x164) and `count` entries around it, `before` of
// them above it. The game fills `strings` (CString pointers).
class ListBox : public CheckButton {
 public:
  ListItem shown;                    // +0x164
  int32_t count = 0;                 // +0x61c
  int32_t before = 0;                // +0x620
  ListItem* items = nullptr;         // +0x624  new[count]
  std::vector<std::string> strings;  // +0x628  the CStrings, owned (deleted by the destructor)
  int32_t spacing = 15;              // +0x638
  int32_t first = -1;                // +0x63c  the selected index
  float before_x = 0.0f, before_y = 0.0f;  // +0x640  LIST_OFFSET's first pair
  float after_x = 0.0f, after_y = 0.0f;    // +0x648  and its second

  ListBox();                         // 0x0059e430
  ~ListBox() override;
  bool read_control(zrdr::Node* node, Dialog* dialog) override;  // +0x84  0x0059e4e0
  virtual void select(int32_t index);  // +0x90  0x0059ea20 -> 0x0059ea30
  void set_size(int32_t n, int32_t above);  // 0x0059e7d0
};

// The software cursor (vtable 0x005d0da0), 0xd0 bytes: saves what is under it.
class Cursor : public Image {
 public:
  zimg::Texture* under = nullptr;    // +0xbc  the screen under it
  int32_t capture = 0;               // +0xc0
  int32_t buffer = 1;                // +0xc4  the screen buffer it reads
  int32_t uc8 = 0, ucc = 0;          // +0xc8

  Cursor(const char* name, int32_t capture);  // 0x005a7750
  ~Cursor() override;                // 0x005a77f0
  void draw() override;              // +0x04  0x005a7a20
  void erase() override;             // +0x08  0x005a7a50
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x005a7940
  virtual void set_cursor_image(zimg::Texture* t);  // +0x80  0x005a7840
  virtual void set_cursor_image(const char* name);  // +0x84  0x005a7820
  void set_capture(int32_t on);      // 0x005a7860
  void grab();                       // 0x005a78b0
  void grab_at(int32_t px, int32_t py);  // 0x005a7970
};

// A movie in a dialog (vtable 0x005d0e28), 0x144 bytes. Its zFMV stream is not
// ported: open, colour key and start are hooks (docs/twod.md).
class Movie2D : public Object2D {
 public:
  Handle stream = 0;                 // +0x34
  uint32_t u38 = 0;                  // +0x38
  uint8_t u3e = 0;                   // +0x3e

  Movie2D();                         // 0x005a7a90
  ~Movie2D() override;               // 0x005a7ae0
  void open(const char* name);       // 0x005a7b50
  void set_color_key(uint16_t c);    // 0x005a7c30
  virtual void start();              // +0x7c  0x005a7d10
};

// A ring of the screen pushed out along a sine wave (vtable 0x005d04a8), 0x4c
// bytes: the ripple around a main menu button (docs/mainmenu.md). It draws
// through zRender (0x00558dd0, Host::ripple), from `inner` to `outer` pixels
// around (x, y), shifted by up to `amplitude` pixels.
class Ripple : public Object2D {
 public:
  const Rect* clip = nullptr;        // +0x34  null: the whole target
  int32_t inner = 0;                 // +0x38  (not set by the constructor)
  int32_t outer = 0;                 // +0x3c
  int32_t amplitude = 0;             // +0x40
  float wavelength = 0.0f;           // +0x44  pixels a radian
  float phase = 0.0f;                // +0x48

  Ripple();                          // 0x005a53e0
  ~Ripple() override = default;      // 0x005a6860
  // +0x04 (0x005a5360) draws it once for each view of the render overlay layer
  // it belongs to (0x005a4590, not ported); the menus call +0x7c directly.
  virtual void draw_ripple();        // +0x7c  0x005a5440
};

// ---- the edit bar (0x005a3830..0x005a3a40, 0x005a46a0..0x005a5340; docs/twod.md, The edit bar) ----

// A line of text being typed (vtable 0x005d0330), 0x114 bytes: the buffer, the
// cursor, characters refused outright, and what each character does
// (`keys`: 1 insert, 2 nothing, 3 Escape, 4 Enter, 5 Backspace, 6 Delete,
// 7 left, 8 right, 9 home, 10 end; zInput's keypad codes 1, 2, 4, 6, 0x7f).
// The interp console's line (0x005a4660..0x005a4690) is one too.
class LineEditor {
 public:
  explicit LineEditor(int32_t size);  // 0x005a46a0
  virtual ~LineEditor();             // 0x005a4720 (not a slot: its owner destroys it in place)
  virtual void on_reject(int32_t) {}  // +0x00  0x004bf620
  virtual void insert(int32_t ch);   // +0x04  0x005a49a0: at the cursor; on_full when full
  virtual void on_control(int32_t) {}  // +0x08  0x004bf620
  virtual void on_enter() {}         // +0x0c  0x005ac5c0
  virtual void on_escape() {}        // +0x10  0x005ac5c0
  virtual void backspace();          // +0x14  0x005a49f0
  virtual void del();                // +0x18  0x005a4a10
  virtual void left();               // +0x1c  0x005a4a20
  virtual void right();              // +0x20  0x005a4a30
  virtual void home() { cursor = 0; }  // +0x24  0x005a4a50
  virtual void end();                // +0x28  0x005a4a60
  virtual void on_full() {}          // +0x2c  0x005ac5c0

  int32_t key(int32_t ch);           // 0x005a4880: its action (0 refused)
  void set_reject(const char* chars);  // 0x005a4840: _strdup'd, null: none
  void resize(int32_t n);            // 0x005a4740
  void set_text(const char* s);      // 0x005a47b0: cut to the buffer, the cursor kept inside
  const char* text() const { return buf; }  // 0x005a47f0
  void set_cursor(int32_t pos);      // 0x005a4800: past the end goes to the end
  bool open_gap(int32_t n, int32_t at);   // 0x005a4a80: false when it would not fit
  void close_gap(int32_t n, int32_t at);  // 0x005a4ad0

  char* reject = nullptr;            // +0x04
  char* buf = nullptr;               // +0x08
  int32_t size = 0;                  // +0x0c  bytes, the terminator included
  int32_t cursor = 0;                // +0x10
  int8_t keys[256] = {};             // +0x14
};

// Lines through points (vtable 0x005cfff0), 0xec bytes: the HUD's polylines
// and the edit bar's caret. Each draw erases, then joins the
// points in order with zRender's 2D line (0x00562940, Host::line).
class PolyLine : public Object2D {
 public:
  PolyLine();                        // 0x005a3830
  void draw() override;              // +0x04  0x005a38f0
  void set_point(int32_t i, int32_t px, int32_t py);  // 0x005a38a0: point 0 moves the object

  int32_t pts[21][2] = {};           // +0x34
  int32_t count = 0;                 // +0xdc
  uint32_t color = 0;                // +0xe0  a pixel (not set by the constructor)
  // The HUD's clipping: within `clip` (0x00562cb0), or with `round`, within
  // the circle across it (0x005a3580). Unset, the lines join unclipped.
  const Rect* clip = nullptr;        // +0xe4
  uint8_t round = 0;                 // +0xe8
};

// A circle outline around the object's place (vtable 0x005d0070), 0x54 bytes.
class Frame : public Object2D {
 public:
  Frame();                           // 0x005a39f0
  Frame(int32_t px, int32_t py, int32_t r, uint32_t c, const Rect* clip);  // 0x005a3a60
  void draw() override;              // +0x04  0x005a3b00
  bool contains(int32_t px, int32_t py) const;  // 0x005a3b30: inside the circle
  int32_t hit(int32_t px, int32_t py) override { return contains(px, py) ? 1 : 0; }  // +0x5c  0x00409db0
  virtual void set_clip(const Rect* r);  // +0x70  0x00409dd0: the rect copied, none: [0x0063d880] (zero)
  int32_t radius = 0;                // +0x34
  int32_t radius_sq = 0;             // +0x38
  uint32_t color = 0;                // +0x3c  a pixel
  uint8_t clipped = 0;               // +0x40  clip given
  Rect clip{};                       // +0x44  [0x0063d880] (always zero) when none
};

// A circle with static (vtable 0x005d00f0), 0x58 bytes: its points thrown
// about by `noise` (the radar's; named a box before its draw was read).
class FilledBox : public Frame {
 public:
  FilledBox(int32_t px, int32_t py, int32_t r, uint32_t c, const Rect* clip);  // 0x005a3b60
  void draw() override;              // +0x04  0x005a3bc0: plain at 0
  void set_noise(float f) { noise = f; }  // 0x005a3bb0
  float noise = 0.0f;                // +0x54
};

// A HUD line from the object's place to an end (vtable 0x005d06e0), 0x4c
// bytes: clipped to a rect when `clipped`, dashed when `dash` is over 1.
class Line2D : public Object2D {
 public:
  Line2D(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c);  // 0x005a67e0
  void set(int32_t x0, int32_t y0, int32_t x1, int32_t y1);  // 0x005a6880
  void draw() override;              // +0x04  0x005a68b0
  int32_t end_x = 0, end_y = 0;      // +0x34
  uint32_t color = 0;                // +0x3c  a pixel
  int32_t clipped = 0;               // +0x40
  const Rect* clip = nullptr;        // +0x44
  int32_t dash = 0;                  // +0x48
};

// One pixel (vtable 0x005d07e0), 0x38 bytes: the radar's shots.
class Pixel2D : public Object2D {
 public:
  Pixel2D(int32_t px, int32_t py, uint32_t c);  // 0x005a6b80
  void draw() override;              // +0x04  0x005a6bb0
  uint32_t color = 0;                // +0x34  a pixel
};

// A HUD line with static (vtable 0x005d0760), 0x50 bytes: drawn thrown about
// by `noise`, plain at 0.
class ClipLine2D : public Line2D {
 public:
  ClipLine2D(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c);  // 0x005a6ae0
  void draw() override;              // +0x04  0x005a6b30
  void set_noise(float f) { noise = f; }  // 0x005a6b20
  float noise = 0.0f;                // +0x4c
};

// The line clipping box zTwoD's HUD lines set ([0x005f4800], [0x005f4810]:
// min and max x, y, z; their whole-number copies at [0x007319a0]).
struct LineClip {
  float min[3] = {}, max[3] = {};
  int32_t ints[4] = {};              // 0x007319a0 left, top, right, bottom
};
LineClip& line_clip();
void set_line_clip(const float* min, const float* max);  // 0x0059b2f0
// 0x0059b490 (0x0059b4d0 by x, then 0x0059b640 by y): the segment within the
// box, false when it misses (the flags set for a clipped end, never read).
bool clip_line(float* a, float* b);
bool clip_round(const Rect& r, const int32_t* p0, const int32_t* p1, int32_t* o0, int32_t* o1);  // 0x005a3580

// An image that blinks or swaps with another (0x005a6d10 its update; the
// HUD's damage parts, whose constructor 0x00447460 sets these inline).
class FlashImage : public Image {
 public:
  explicit FlashImage(int32_t center) : Image(center) {}
  void update(float dt) override;    // +0x24  0x005a6d10
  float rate = 0.35f;                // +0xbc  a half period, seconds
  float timer = 0.0f;                // +0xc0
  int32_t on = 0;                    // +0xc4
  int32_t mode = 0;                  // +0xc8  1 blink, 2 swap with `alt`
  int32_t phase = 1;                 // +0xcc  1 or -1
  uint32_t xd0 = 0;                  // +0xd0
  zimg::Texture* alt = nullptr;      // +0xd4
};

// A filled polygon of up to 21 points (vtable 0x005d0860), 0x138 bytes: the
// HUD's bars.
class Polygon : public Object2D {
 public:
  Polygon();                         // 0x005a6bd0
  void set_point(int32_t i, float px, float py);  // 0x005a6c30: point 0 moves the object
  void draw() override;              // +0x04  0x005a6ca0

  float pts[21][3] = {};             // +0x34  x, y (the third never set here)
  int32_t count = 0;                 // +0x130
  uint32_t color = 0;                // +0x134 a pixel
};

// A triangle polyline (vtable 0x005cff58), 0x124 bytes: a base triangle
// (0x005f4a20) scaled and moved, its points worked out at the next update
// (TARGET's shape, hud.md section 14).
class Shape : public PolyLine {
 public:
  Shape();                           // 0x005a3170
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x005a34c0: the translation
  void set_translation(const float* t);  // 0x005a34f0
  void set_scale(float s);           // 0x005a3530: the base triangle (0x005f4a20) scaled by it
  void update(float dt) override;    // +0x24  0x005a3550: the points first when pending
  void transform();                  // 0x005a3270
  bool contains(int32_t px, int32_t py);  // 0x005a3440: below the apex, above the base, within its slope
  int32_t hit(int32_t px, int32_t py) override { return contains(px, py) ? 1 : 0; }  // +0x5c  0x00409e40
  int32_t get_x() override { return static_cast<int32_t>(translation[0]); }  // +0x68  0x00409e20
  int32_t get_y() override { return static_cast<int32_t>(translation[1]); }  // +0x6c  0x00409e30
  uint32_t xform = 0;                // +0xec  1 moved, 2 scaled (pending)
  float translation[3] = {};         // +0xf0
  float points[9] = {};              // +0xfc  the three corners worked out
  float scale = 0.0f;                // +0x120
};

// The edit bar's caret (vtable 0x005d0360), 0x10c bytes: an I-beam of 13
// points, blinking when `blink` is set (never, here).
class Caret : public PolyLine {
 public:
  Caret();                           // 0x005a4b10
  void update(float dt) override;    // +0x24  0x005a4c90: drawn every frame it shows
  void place(int32_t px, int32_t py, int32_t w, int32_t h);  // 0x005a4cf0

  int32_t cx = 0, cy = 0;            // +0xec
  int32_t cw = 1, ch = 10;           // +0xf4
  int32_t blink = 0;                 // +0xfc
  float period = 0.35f;              // +0x100
  float blink_timer = 0.0f;          // +0x104
  int32_t phase = 1;                 // +0x108  1 shown, -1 not
};

class EditBar;

// The bar's line (vtable 0x005d0478): Enter and Escape go to the bar.
class BarEditor : public LineEditor {
 public:
  explicit BarEditor(int32_t n) : LineEditor(n) {}
  void on_enter() override;          // +0x0c  0x005a5320: the bar's +0x90
  void on_escape() override;         // +0x10  0x005a5340: the bar's +0x94
  EditBar* bar = nullptr;            // +0x114
};

// A text field (vtable 0x005d03e0), 0x380 bytes: a button whose first LABEL
// shows the line being typed. While it has the focus zInput's text callback
// (0x0051a9a0) sends it every character typed, and the caret stands after
// the cursor's character.
class EditBar : public Control {
 public:
  EditBar();                         // 0x005a4ec0
  ~EditBar() override;               // 0x005a4fb0 (deleting 0x005a4f80): the focus given up
  void update(float dt) override;    // +0x24  0x005a50e0
  void on_left_up() override;        // +0x30  0x005a50d0
  // +0x8c 0x005a5040: a typed character (and its key code); numeric bars take
  // only "0123456789.-" and the editing keys.
  virtual void on_char(int32_t ch, int32_t key);
  virtual void on_return() {}        // +0x90  0x0049bfd0: Enter
  virtual void on_cancel() {}        // +0x94  0x0049bfd0: Escape

  // 0x005a5020: zInput's text callback (ECX the character, EDX its key code).
  static void char_callback(int32_t ch, int32_t key, uintptr_t bar);
  void set_focus(uint8_t on);        // 0x005a5090: the callback set or cleared on a change
  void set_size(int32_t n) { editor.resize(n); }  // 0x005a5280
  void set_text(const char* s);      // 0x005a52a0: the cursor at its end
  const char* get_text() const { return editor.text(); }  // 0x005a5310

  BarEditor editor{0x200};           // +0x14c
  Caret caret;                       // +0x264
  int32_t caret_w = 0;               // +0x370
  uint8_t focus = 0;                 // +0x374
  int32_t clicked = 1;               // +0x378  set by a click, read by nothing
  uint8_t numeric = 0;               // +0x37c
};

// A font of a dialog's FONTS list (Dialog +0x1d38), 0x24 bytes.
struct FontEntry {
  int32_t used = 0;                  // +0x00
  const char* face = nullptr;        // +0x04  the tree's string
  int32_t height = 0;                // +0x08
  uint32_t color = 0;                // +0x0c  COLORREF
  uint32_t color2 = 0;               // +0x10  the background colour when opaque
  int32_t mode = 0;                  // +0x14  1 transparent, 2 opaque (two colours)
  int32_t shadow = 0;                // +0x18
  int32_t weight = 500;              // +0x1c
  int32_t align = 0;                 // +0x20  LEFT 0, CENTER 1, RIGHT 2
};

// A BACKGROUND_SOUNDS entry (Dialog +0x1c98), 0x10 bytes.
struct DialogSound {
  int32_t cd = 0;                    // +0x00  a CD track, else a sound
  int32_t id = 0;                    // +0x04
  int32_t playing = 0;               // +0x08
  float volume = 1.0f;               // +0x0c
};

// FIX (B), any_resolution (resolution.md step 5): where a dialog's 640x480
// frame sits on the 2D canvas, set by the program with the screen: centred
// on a canvas past mode 10, else (0, 0) as the exe. A dialog made then takes
// it as its origin (the constructors' 0).
struct DialogOrigin {
  int32_t x = 0, y = 0;
};
DialogOrigin& dialog_origin();


// A screen read from a .zrd (vtable 0x005cfc78), 0x10fec bytes.
class Dialog : public Window {
 public:
  Cursor cursor_obj{nullptr, 1};     // +0x58  (vtable 0x005cfc88 there)
  zrdr::Node* tree = nullptr;        // +0x128  owned when load() read it
  zrdr::Node* node = nullptr;        // +0x12c  this dialog's entry
  zimg::Texture* behind = nullptr;   // +0x130  the screen before it was drawn
  zimg::Texture* static_bg = nullptr;  // +0x134  the screen after the static parts
  Image images[20]{Image(0), Image(0), Image(0), Image(0), Image(0), Image(0), Image(0),
                   Image(0), Image(0), Image(0), Image(0), Image(0), Image(0), Image(0),
                   Image(0), Image(0), Image(0), Image(0), Image(0), Image(0)};  // +0x138
  Movie2D movies[10];                // +0xfe8
  int32_t sound_count = 0;           // +0x1c90
  int32_t sound_next = 0;            // +0x1c94
  DialogSound sounds[10];            // +0x1c98
  FontEntry fonts[20];               // +0x1d38
  Text texts[50];                    // +0x2008
  int32_t origin_x = 0, origin_y = 0;  // +0x10fe0
  int32_t mute = 1;                  // +0x10fe8  no background sounds

  Dialog();                          // 0x0059ec40
  ~Dialog() override;                // 0x0059edc0
  void set_active(int32_t on) override;  // +0x04  0x0059eed0

  void set_static_bg(zimg::Texture* t);  // 0x0059eeb0
  void stop_sounds(int32_t stop_cd);     // 0x0059ef20
  void play_sounds(int32_t from);        // 0x0059ef60
  bool load(const char* file, const char* name, bool no_draw);  // 0x0059f010
  bool load_from(zrdr::Node* tree, const char* name, bool no_draw);  // 0x0059f050
  bool load_node(zrdr::Node* tree, zrdr::Node* entry, bool no_draw);  // 0x0059f080
  bool bind_button(Control* c, const char* name);    // 0x0059f8b0 (0x0059f810)
  bool bind_primitive(Object2D* o, const char* name);  // 0x0059f8d0
  void unload();                     // 0x0059fa10
  void restore_behind();             // 0x0059fa40
};

// A button of the message box (vtables 0x005d0d10..0x005d0920): its left-up
// answers through the box's slot (+0x10..+0x2c), then acts as a Control.
class MessageBox;
class MbButton : public Control {
 public:
  int32_t slot = 0;                  // which answer: 0 OK .. 7 USERBTN (the vtable in the original)
  void on_left_up() override;        // +0x30  0x005a7650..0x005a7730
};

// The modal question box (vtable 0x005d08f0), 0x12494 bytes: dlgMessage.zrd's
// MESSAGEBOX with a TITLE and MESSAGE text and the MB_* buttons; run() keeps
// drawing frames until a button answers (docs/twod.md, The message box).
class MessageBox : public Dialog {
 public:
  Rect screen;                       // +0x10fec  zVideo's screen (0x00579880)
  int32_t result = 0;                // +0x10ffc  IDOK 1, IDCANCEL 2, IDABORT 3, IDRETRY 4, IDIGNORE 5, IDYES 6, IDNO 7
  int32_t frames = 0;                // +0x11000  frames left
  zimg::Texture* grab = nullptr;     // +0x1100c  the screen behind, as the backdrop
  uint32_t u11010 = 0, u11014 = 0;   // +0x11010
  FontText message{nullptr, 0, 0};   // +0x11018
  FontText title{nullptr, 0, 0};     // +0x114c8
  MbButton buttons[8];               // +0x11978  OK, CANCEL, YES, NO, ABORT, RETRY, IGNORE, USERBTN
  Image backdrop{0};                 // +0x123d8

  MessageBox(const char* file, const char* name, bool no_draw);  // 0x005a6e60
  ~MessageBox() override;            // 0x005a7150
  // 0x005a7280: the text is the message's printf format (as written); style's
  // low 4 bits are MB_OK .. MB_RETRYCANCEL. Returns the answer, 0 for none.
  int32_t run(const char* text, const char* caption, uint32_t style);
  // +0x10..+0x2c (0x005a7560..0x005a7640)
  virtual void answer_ok() { result = 1; frames = 0; }
  virtual void answer_cancel() { result = 2; frames = 0; }
  virtual void answer_yes() { result = 6; frames = 0; }
  virtual void answer_no() { result = 7; frames = 0; }
  virtual void answer_abort() { result = 3; frames = 0; }
  virtual void answer_retry() { result = 4; frames = 0; }
  virtual void answer_ignore() { result = 5; frames = 0; }
  virtual void answer_user() { frames = 1; }
};

// 0x0059f810: ECX dialog, EDX its node.
bool bind_button(Dialog* d, zrdr::Node* entry, Control* c, const char* name);
// 0x0059cb30: ECX dialog, EDX a LABEL value; the labels go to a shared array
// (0x007319b0) whose filled count is *count.
Text** read_labels(Dialog* d, zrdr::Node* value, int32_t* count, const int32_t* base_xy);
// 0x0059ca90: ECX a FLASH value, EDX the labels' vector.
void read_flash(zrdr::Node* value, std::vector<Text*>* labels);
// 0x0059cd90: ECX a node whose value starts with a string; EDX an "offset" out (or null).
const char* name_and_offset(zrdr::Node* node, int32_t* offset);

}  // namespace ztwod
