#include <vector>
#include <string>
#include <algorithm>
#include "frontend/sdl/sdl_twod_host.h"
#include "frontend/sdl/sdl_platform.h"

#include "gamez/zutil/zutl_msgdll.h"

#include <SDL3_ttf/SDL_ttf.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "frontend/sdl/gdi_font_size.h"
#include "frontend/sdl/sdl_input.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zrender/zrndr_ripple.h"
#include "gamez/zinput/zinput.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zrender/zrndr_blit.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zutil/zutl_msg.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zvideo/zvid_format.h"
#include "mech3/app_engine.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sdlfe {

namespace {

std::string find_text_font(const std::string& filename) {
  auto mounted = zutl::find(filename.c_str());
  if (!mounted.empty()) return mounted;
#ifdef _WIN32
  wchar_t windows[MAX_PATH];
  const auto length = GetWindowsDirectoryW(windows, MAX_PATH);
  if (length > 0 && length < MAX_PATH) {
    const auto fonts = std::filesystem::path(windows) / L"Fonts";
    const auto installed = [&](const std::string& name) -> std::string {
      const auto path = fonts / name;
      std::error_code error;
      return std::filesystem::is_regular_file(path, error) ? path.u8string() : std::string{};
    };
    auto path = installed(filename);
    if (!path.empty()) return path;
    if (filename == "lucon.ttf") {
      path = installed("consola.ttf");
      return path.empty() ? installed("cour.ttf") : path;
    }
    if (filename == "impact.ttf") {
      path = installed("arialbd.ttf");
      return path.empty() ? installed("arial.ttf") : path;
    }
    if (filename == "arial.ttf") return installed("segoeui.ttf");
  }
#endif
  return {};
}

constexpr uint32_t kCalcRect = 0x400, kWordBreak = 0x10, kCenter = 0x1, kRight = 0x2, kNoPrefix = 0x800,
                   kSingleLine = 0x20, kVCenter = 0x4, kBottom = 0x8;

// The game's strings are Windows-1252; SDL_ttf reads UTF-8.
std::string utf8(const char* s, int32_t length) {
  std::string out;
  const size_t n = length < 0 ? std::strlen(s) : static_cast<size_t>(length);
  for (size_t i = 0; i < n && s[i]; ++i) {
    const auto c = static_cast<uint8_t>(s[i]);
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else {
      out += static_cast<char>(0xc0 | c >> 6);
      out += static_cast<char>(0x80 | (c & 0x3f));
    }
  }
  return out;
}

// DrawText's prefix: "&x" underlines x (not drawn here), "&&" is one "&".
std::string strip_prefix(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '&' && i + 1 < s.size()) ++i;
    out += s[i];
  }
  return out;
}

zimg::Texture* new_image(int w, int h) {  // 0x00517c10..0x00517c90, as FontText makes them
  auto* t = static_cast<zimg::Texture*>(std::calloc(1, sizeof(zimg::Texture)));
  t->flags = 1 | zimg::kPixelsOwned;
  t->width = static_cast<int16_t>(w), t->height = static_cast<int16_t>(h);
  t->width_again = t->width;
  t->pixel_count = w * h;
  t->pixels = std::calloc(static_cast<size_t>(w > 0 ? w : 1) * (h > 0 ? h : 1), 2);
  return t;
}

}  // namespace

SdlTwodHost::SdlTwodHost(mech3::GameEngine* engine, SdlInput* input, platform::Window* window)
    : engine_(engine), input_(input), window_(window) {
  ttf_ok_ = TTF_Init();
  if (!ttf_ok_) std::fprintf(stderr, "mw3: TTF_Init: %s\n", SDL_GetError());
}

SdlTwodHost::~SdlTwodHost() {
  for (const auto& [key, font] : hud_fonts_) TTF_CloseFont(font);
  hud_fonts_.clear();
  hud_text_sources_.clear();
  for (auto& [h, f] : fonts_)
    if (f.ttf) TTF_CloseFont(f.ttf);
  if (ttf_ok_) TTF_Quit();
}

std::string SdlTwodHost::font_file(const std::string& face) {
  std::string f;
  for (char c : face) f += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
  if (f.find("impact") != std::string::npos) return "impact.ttf";
  if (f.find("lucida console") != std::string::npos || f.find("courier") != std::string::npos ||
      f.find("fixed") != std::string::npos)
    return "lucon.ttf";
  return "arial.ttf";  // Arial, and what Windows would have substituted for others
}

// ---------------------------------------------------------------------------
// zImage

zimg::Texture* SdlTwodHost::image_get(const char* name) {
  if (!name || !*name) return nullptr;
  auto it = images_.find(name);
  if (it != images_.end()) {
    ++it->second.refs;
    return it->second.image;
  }
  zimg::Texture* t = zimg::fetch_image(name);
  if (!t) {
    // The original misses silently (0x005179d0 returns 0). Reported once per
    // name: the HUD asks for 12 names no shipped library has, every mission
    // (dmg_*_blk, hud_othererrow_*; hud.md, Images the data lacks).
    if (missing_.insert(name).second) std::fprintf(stderr, "mw3: image %s not found\n", name);
    return nullptr;
  }
  images_[name] = {t, 1};
  return made(t);
}

// 0x00517b10: a named image's count down, the image freed at 0 (0x00517760);
// one not in the list (a screen grab) freed at once (0x005185f0). The
// library reads a new copy for every first get, so nothing else holds it.
// FIX (bug): an image released again after it was freed is reported and
// left alone (the original would free it twice).
zimg::Texture* SdlTwodHost::image_release(zimg::Texture* image) {
  if (!image) return nullptr;
  for (auto it = images_.begin(); it != images_.end(); ++it) {
    if (it->second.image != image) continue;
    if (--it->second.refs <= 0) {
      images_.erase(it);
      free_image(image);
    }
    return nullptr;
  }
  if (freed_.count(image)) {
    std::fprintf(stderr, "mw3: image %p released after it was freed\n", static_cast<void*>(image));
    return nullptr;
  }
  free_image(image);
  return nullptr;
}

void SdlTwodHost::free_image(zimg::Texture* image) {
  texture_free(image);
  freed_.insert(image);
}
zimg::Texture* SdlTwodHost::made(zimg::Texture* image) {
  hud_text_sources_.erase(image);
  freed_.erase(image);  // the address may come back for a new image
  return image;
}

// 0x00516f50 (zImage's texture directory, "zbd" for the shell): FIX (A), each
// ';'-separated directory under every read root joins the mounts, once.
void SdlTwodHost::image_path(const char* path) {
  if (!path) return;
  // The roots are the install and asset folders: not the folders added here,
  // nor a mission's zbd folders (or each call would nest zbd/zbd/...).
  static std::vector<std::string> added;
  std::vector<std::string>& read = zutl::mounts().read;
  std::vector<std::string> roots;
  for (const std::string& r : read) {
    if (std::find(added.begin(), added.end(), r) != added.end()) continue;
    if (r.find("/zbd") != std::string::npos) continue;
    roots.push_back(r);
  }
  std::string dirs = path;
  for (size_t at = 0; at <= dirs.size();) {
    size_t end = dirs.find(';', at);
    if (end == std::string::npos) end = dirs.size();
    std::string d = dirs.substr(at, end - at);
    for (char& c : d) if (c == '\\') c = '/';
    at = end + 1;
    if (d.empty()) continue;
    for (const std::string& r : roots) {
      const std::string m = r + "/" + d;
      if (std::find(read.begin(), read.end(), m) == read.end()) zutl::mount(m), added.push_back(m);
    }
  }
}

// The fonts.zrd WINDOWS_FONTS entries: face, height, color (r g b) or two of
// them, shadow "true", align "right" / "center", weight.
bool SdlTwodHost::load_named_fonts() {
  if (named_loaded_) return true;
  named_loaded_ = true;
  zrdr::Node* top = zrdr::load("fonts.zrd");
  if (!top) return false;
  zrdr::Node* list = zrdr::find(top, "WINDOWS_FONTS");
  if (list && list->type == zrdr::List) {
    for (int32_t i = 1; i + 1 < zrdr::list_count(list); i += 2) {
      zrdr::Node& name = list->list[i];
      zrdr::Node& props = list->list[i + 1];
      if (name.type != zrdr::String || props.type != zrdr::List) continue;
      const char* face = zrdr::get_string(&props, "face");
      int32_t height = -10, weight = 400;
      zrdr::get_int(&props, "height", &height);
      zrdr::get_int(&props, "weight", &weight);
      ztwod::NamedFont nf;
      auto colorref = [](zrdr::Node* c) -> uint32_t {
        if (!c || c->type != zrdr::List || zrdr::list_count(c) < 4) return 0xffffff;
        return static_cast<uint32_t>(c->list[1].i & 255) | static_cast<uint32_t>(c->list[2].i & 255) << 8 |
               static_cast<uint32_t>(c->list[3].i & 255) << 16;
      };
      if (zrdr::Node* c = zrdr::find(&props, "color")) {
        if (zrdr::list_count(c) >= 3 && c->list[1].type == zrdr::List) {
          nf.color_top = colorref(&c->list[1]);
          nf.color_bottom = colorref(&c->list[2]);
        } else {
          nf.color_top = nf.color_bottom = colorref(c);
        }
      }
      if (const char* s = zrdr::get_string(&props, "shadow")) nf.shadow = std::strcmp(s, "true") == 0;
      if (const char* a = zrdr::get_string(&props, "align"))
        nf.align = std::strcmp(a, "right") == 0 ? 2 : std::strcmp(a, "center") == 0 ? 1 : 0;
      nf.hfont = CreateFontA(height, 0, 0, 0, weight, 0, 0, 0, 0, 0, 0, 1, 0, face ? face : "Arial");
      const Handle h = next_++;
      named_[h] = {nf, 0};
      named_by_name_[name.s] = h;
    }
  }
  zrdr::free_node(top);
  return true;
}

Handle SdlTwodHost::font_find(const char* name) {
  load_named_fonts();
  auto it = named_by_name_.find(name ? name : "");
  if (it == named_by_name_.end()) return 0;
  ++named_[it->second].refs;
  return it->second;
}
ztwod::NamedFont SdlTwodHost::font_fields(Handle f) {
  auto it = named_.find(f);
  return it == named_.end() ? ztwod::NamedFont{} : it->second.fields;
}
void SdlTwodHost::font_addref(Handle f) {
  auto it = named_.find(f);
  if (it != named_.end()) ++it->second.refs;
}
void SdlTwodHost::font_release(Handle f) {
  auto it = named_.find(f);
  if (it != named_.end() && it->second.refs > 0) --it->second.refs;  // the table keeps them
}

// ---------------------------------------------------------------------------
// zVideo, on the engine's 16-bit buffer

void SdlTwodHost::video_lock() {
  if (engine_) engine_->video_lock();
}
void SdlTwodHost::video_unlock() {}

// FIX (A): after a 3D frame the buffer holds only the 2D layer over its key
// colour, so the frame shown is read back (the pause screen's backdrop).
zimg::Texture* SdlTwodHost::screen_grab(int32_t buffer) {
  // FIX (B), playtest W25: the message box's backdrop (buffer 2, 0x005a6e60)
  // over a GPU frame: the frame kept at its own size behind the box
  // (GameEngine::hold_shown_frame), the backdrop the layer's key colour.
  if (buffer == 2 && engine_ && engine_->hold_shown_frame()) {
    const int w = engine_->buffer_width(), h = engine_->buffer_height();
    zimg::Texture* img = new_image(w, h);
    std::fill(static_cast<uint16_t*>(img->pixels), static_cast<uint16_t*>(img->pixels) + w * h, mech3::GameEngine::kLayerKey);
    held_grab_ = img;
    return made(img);
  }
  std::vector<uint16_t> shown;
  if (engine_ && engine_->shown_frame(&shown)) {
    const zrndr::Target2D& t = zrndr::target2d();
    const int w = t.base ? static_cast<int>(t.width) : 0, h = t.base ? static_cast<int>(t.height) : 0;
    if (w > 0 && h > 0 && shown.size() == static_cast<size_t>(w) * h) {
      zimg::Texture* img = new_image(w, h);
      std::memcpy(img->pixels, shown.data(), shown.size() * 2);
      return made(img);
    }
  }
  const zrndr::Target2D& t = zrndr::target2d();
  if (!t.base) return nullptr;
  zimg::Texture* img = new_image(static_cast<int>(t.width), static_cast<int>(t.height));
  for (uint32_t y = 0; y < t.height; ++y)
    std::memcpy(static_cast<uint16_t*>(img->pixels) + y * t.width, t.base + y * t.pitch, t.width * 2);
  return made(img);
}

void SdlTwodHost::texture_free(zimg::Texture* t) {
  hud_text_sources_.erase(t);
  if (t && t == held_grab_) {  // the box gone: the kept frame let go
    held_grab_ = nullptr;
    if (engine_) engine_->drop_held();
  }
  ztwod::Host::texture_free(t);
}
zimg::Texture* SdlTwodHost::image_new(int32_t w, int32_t h) { return made(new_image(w, h)); }

zimg::Texture* SdlTwodHost::screen_grab_rect(int32_t, ztwod::Rect* r, zimg::Texture* into) {
  const zrndr::Target2D& t = zrndr::target2d();
  if (!t.base) return nullptr;
  ztwod::Rect c = *r;
  if (c.left < 0) c.left = 0;
  if (c.top < 0) c.top = 0;
  if (c.right > static_cast<int32_t>(t.width)) c.right = static_cast<int32_t>(t.width);
  if (c.bottom > static_cast<int32_t>(t.height)) c.bottom = static_cast<int32_t>(t.height);
  if (c.right <= c.left || c.bottom <= c.top) return nullptr;  // off screen
  const int w = c.right - c.left, h = c.bottom - c.top;
  if (into && (into->width < w || into->height < h)) into = nullptr;
  zimg::Texture* img = into ? into : new_image(w, h);
  for (int y = 0; y < h; ++y)
    std::memcpy(static_cast<uint16_t*>(img->pixels) + y * img->width, t.base + (c.top + y) * t.pitch + c.left * 2,
                static_cast<size_t>(w) * 2);
  *r = c;
  return made(img);
}

Handle SdlTwodHost::image_dc(zimg::Texture* image) {
  const Handle h = CreateCompatibleDC(0);
  dcs_[h].image = image;
  return h;
}
void SdlTwodHost::image_dc_release(zimg::Texture*, Handle dc) { DeleteDC(dc); }

int32_t SdlTwodHost::video_mode() { return engine_ ? static_cast<int32_t>(engine_->peek32(0x72f348)) : 0; }
uint32_t SdlTwodHost::show_cursor(uint32_t on) { return engine_ ? engine_->video_show_cursor(on) : 0; }
ztwod::Rect SdlTwodHost::screen_rect() {
  ztwod::Rect r;
  r.right = engine_ ? engine_->buffer_width() : 640;
  r.bottom = engine_ ? engine_->buffer_height() : 480;
  return r;
}
Handle SdlTwodHost::render_target(int32_t* w, int32_t* h, int32_t* depth, int32_t* pitch) {
  *w = engine_ ? engine_->buffer_width() : 640;
  *h = engine_ ? engine_->buffer_height() : 480;
  *depth = 16;
  *pitch = *w * 2;
  return mech3::GameEngine::kBackBuffer;
}
void SdlTwodHost::timer_update() { ztimer::update(); }
void SdlTwodHost::input_poll(uint8_t a) {
  if (!engine_) return;
  engine_->pump_events();
  engine_->input_poll(a);
}
float SdlTwodHost::frame_dt() { return ztimer::state().dt; }
// 0x00558dd0, ported (zrndr::ripple): on the finish target, which video_lock
// points at the program's 16-bit buffer, so the ring moves what the menu drew
// this frame before it, as the exe's moved the locked back buffer.
void SdlTwodHost::ripple(int32_t x, int32_t y, int32_t inner, int32_t outer, int32_t amplitude, float wavelength,
                         float phase, const ztwod::Rect* clip) {
  const int32_t c[4] = {clip ? clip->left : 0, clip ? clip->top : 0, clip ? clip->right : 0, clip ? clip->bottom : 0};
  zrndr::ripple(x, y, inner, outer, amplitude, wavelength, phase, clip ? c : nullptr,
                zfix::options().fpu53 ? zrndr::X87::k53 : zrndr::X87::k24);
}
void SdlTwodHost::video_flip(const ztwod::Rect*, const ztwod::Rect*, int32_t, int32_t) {
  if (engine_) engine_->video_flip(0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// zSound, the messages, zInput's mouse

int32_t SdlTwodHost::sound_find(const char* name) {
  return engine_ ? static_cast<int32_t>(engine_->sound_find(name)) : 0;
}
int32_t SdlTwodHost::sound_play(int32_t sound, float volume) {
  if (!engine_) return 0;
  auto* o = static_cast<zsnd::Object*>(engine_->object_of(static_cast<Handle>(sound)));
  return o ? static_cast<int32_t>(engine_->handle_of(zsnd::play(o, volume))) : 0;
}
void SdlTwodHost::sound_stop(int32_t handle) {
  if (engine_) engine_->voice_stop(static_cast<Handle>(handle));
}
int32_t SdlTwodHost::cd_find(const char* name) { return zsnd_cd::cue_track(name ? name : ""); }
int32_t SdlTwodHost::cd_play(int32_t track, int32_t mode) { return zsnd_cd::play_track(track, mode) ? 1 : 0; }
void SdlTwodHost::cd_stop() { zsnd_cd::stop(); }
const char* SdlTwodHost::message(const char* key) { return zutl::localize(key); }
uint32_t SdlTwodHost::format_message(char* buf, uint32_t size, uint32_t id, va_list args) {
  if (!size) return 0;
  const std::string t = zutl::format_message_v(static_cast<int32_t>(id), args);
  std::snprintf(buf, size, "%s", t.c_str());
  return static_cast<uint32_t>(t.size() < size ? t.size() : size - 1);
}
const char* SdlTwodHost::message_text(uint32_t id) {  // 0x00576040
  auto* f = zutl::messages().get_text;
  return f ? f(static_cast<int32_t>(id)) : nullptr;
}

// zInput's mouse (0x0051b7b0): its 15 dwords as zInput integrated them.
ztwod::MouseState SdlTwodHost::mouse() {
  ztwod::MouseState m{};
  static_assert(sizeof m == sizeof(zin::MouseInfo), "the same 15 dwords");
  std::memcpy(&m, zin::mouse_info(), sizeof m);
  // MW3_TRACE_MOUSE=1 (ours): the menus' mouse, once every 30 reads.
  static const bool trace = std::getenv("MW3_TRACE_MOUSE") != nullptr;
  static int reads = 0;
  if (trace && (++reads % 30 == 0 || zin::mouse_info()->left || zin::mouse_info()->right)) {
    const zin::MouseInfo& i = *zin::mouse_info();
    std::fprintf(stderr, "mw3: mouse %d,%d d %d,%d buttons %u %u\n", i.x, i.y, i.dx, i.dy, i.left, i.right);
  }
  return m;
}
void SdlTwodHost::input_reset() { zin::reset(); }  // 0x0051b1c0

// ---------------------------------------------------------------------------
// GDI

SdlTwodHost::Font* SdlTwodHost::font(Handle h) {
  auto it = fonts_.find(h);
  return it == fonts_.end() ? nullptr : &it->second;
}
SdlTwodHost::Dc* SdlTwodHost::dc(Handle h) {
  auto it = dcs_.find(h);
  return it == dcs_.end() ? nullptr : &it->second;
}

// lfHeight < 0 is the character height (the em), > 0 the cell height
// (ascent + descent), 0 a default of 12 pixels.
TTF_Font* SdlTwodHost::open_ttf(Font& f) {
  if (f.ttf || !ttf_ok_) return f.ttf;
  const std::string path = find_text_font(font_file(f.face));
  if (path.empty()) {
    std::fprintf(stderr, "mw3: font %s (%s) not found\n", f.face.c_str(),
                 font_file(f.face).c_str());
    return nullptr;
  }
  // FIX (A): GDI's size, ascent and descent for the height (VDMX, else the
  // win metrics scaled), and its widths: hinted advances (MONO matches the
  // fonts' hdmx), no kerning (twod.md section 6, Text widths).
  static std::map<std::string, GdiFaceMetrics> faces;
  auto it = faces.find(path);
  if (it == faces.end()) {
    GdiFaceMetrics m;
    if (std::FILE* fp = std::fopen(path.c_str(), "rb")) {
      std::vector<uint8_t> bytes;
      uint8_t buf[65536];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, fp)) > 0;) bytes.insert(bytes.end(), buf, buf + n);
      std::fclose(fp);
      gdi_face_metrics(bytes, &m);
    }
    it = faces.emplace(path, m).first;
  }
  const GdiSize size = gdi_font_size(it->second, f.height);
  f.ttf = TTF_OpenFont(path.c_str(), static_cast<float>(size.ppem));
  if (!f.ttf) return nullptr;
  f.em = size.ppem;
  f.ascent = size.ascent;
  f.descent = size.descent;
  TTF_SetFontHinting(f.ttf, TTF_HINTING_MONO);
  TTF_SetFontKerning(f.ttf, false);
  f.shift = f.ascent - TTF_GetFontAscent(f.ttf);
  TTF_SetFontStyle(f.ttf, (f.weight >= 600 ? TTF_STYLE_BOLD : 0) | (f.italic ? TTF_STYLE_ITALIC : 0));
  return f.ttf;
}

Handle SdlTwodHost::GetStockObject(int32_t which) {
  static Handle stock = 0;
  if (which != 10 && which != 13 && which != 17) return 0;  // OEM_FIXED, SYSTEM, DEFAULT_GUI fonts
  if (!stock) stock = CreateFontA(12, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 1, 0, "Lucida Console");
  return stock;
}

Handle SdlTwodHost::CreateFontA(int32_t height, int32_t, int32_t, int32_t, int32_t weight, uint32_t italic, uint32_t,
                                uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const char* face) {
  Font f;
  f.face = face ? face : "Arial";
  f.height = height;
  f.weight = weight ? weight : 400;
  f.italic = italic;
  if (!open_ttf(f)) return 0;
  const Handle h = next_++;
  fonts_[h] = f;
  return h;
}

int32_t SdlTwodHost::GetObjectA(Handle font_h) { return font(font_h) ? 0x3c : 0; }  // sizeof(LOGFONTA)
Handle SdlTwodHost::CreateFontIndirectA(Handle from) {
  Font* f = font(from);
  if (!f) return 0;
  return CreateFontA(f->height, 0, 0, 0, f->weight, f->italic, 0, 0, 0, 0, 0, 1, 0, f->face.c_str());
}
void SdlTwodHost::DeleteObject(Handle object) {
  auto it = fonts_.find(object);
  if (it == fonts_.end()) return;
  if (it->second.ttf) TTF_CloseFont(it->second.ttf);
  fonts_.erase(it);
}

Handle SdlTwodHost::CreateCompatibleDC(Handle) {
  const Handle h = next_++;
  dcs_[h] = Dc{};
  return h;
}
Handle SdlTwodHost::SelectObject(Handle d, Handle object) {
  Dc* x = dc(d);
  if (!x) return 0;
  const Handle old = x->font;
  x->font = object;
  return old;
}
void SdlTwodHost::DeleteDC(Handle d) { dcs_.erase(d); }

int32_t SdlTwodHost::GetTextExtentPoint32A(Handle d, const char* text, int32_t length, int32_t* cx, int32_t* cy) {
  *cx = *cy = 0;
  Dc* x = dc(d);
  Font* f = x ? font(x->font) : nullptr;
  if (!f || !f->ttf || !text) return 0;
  const std::string s = utf8(text, length);
  int w = 0, h = 0;
  if (!s.empty()) TTF_GetStringSize(f->ttf, s.c_str(), s.size(), &w, &h);
  *cx = w;
  *cy = f->ascent + f->descent;
  return 1;
}

// The text's lines: "\n" (and "\r\n") break, and with DT_WORDBREAK words wrap
// at the box's width.
std::vector<std::string> SdlTwodHost::lines(const Font& f, const std::string& s, int wrap_width, bool wrap) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t end = s.find('\n', start);
    if (end == std::string::npos) end = s.size();
    std::string line = s.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!wrap || wrap_width <= 0) {
      out.push_back(line);
    } else {
      while (true) {
        int mw = 0;
        size_t fit = 0;
        TTF_MeasureString(f.ttf, line.c_str(), line.size(), wrap_width, &mw, &fit);
        if (fit >= line.size()) {
          out.push_back(line);
          break;
        }
        size_t cut = line.rfind(' ', fit);
        if (cut == std::string::npos || cut == 0) cut = fit > 0 ? fit : 1;  // a word wider than the box
        out.push_back(line.substr(0, cut));
        size_t next = cut;
        while (next < line.size() && line[next] == ' ') ++next;
        line = line.substr(next);
      }
    }
    if (end == s.size()) break;
    start = end + 1;
  }
  return out;
}

int32_t SdlTwodHost::DrawTextA(Handle d, const char* text, int32_t length, ztwod::Rect* rect, uint32_t format) {
  Dc* x = dc(d);
  Font* f = x ? font(x->font) : nullptr;
  if (!f || !f->ttf || !text) return 0;
  std::string s = utf8(text, length);
  if (!(format & kNoPrefix)) s = strip_prefix(s);
  const int box_w = rect->right - rect->left;
  std::vector<std::string> ls =
      (format & kSingleLine) ? std::vector<std::string>{s} : lines(*f, s, box_w, (format & kWordBreak) != 0);
  const int lh = f->ascent + f->descent;
  const int total = lh * static_cast<int>(ls.size());
  if (format & kCalcRect) {
    int widest = 0;
    for (const std::string& l : ls) {
      int w = 0, h = 0;
      if (!l.empty()) TTF_GetStringSize(f->ttf, l.c_str(), l.size(), &w, &h);
      if (w > widest) widest = w;
    }
    if (!(format & kWordBreak) || widest > box_w) rect->right = rect->left + widest;
    rect->bottom = rect->top + total;
    return total;
  }
  zimg::Texture* img = x->image;
  if (!img || !img->pixels) return total;
  uint16_t* px = static_cast<uint16_t*>(img->pixels);
  const auto pack = [](uint32_t c) {
    return zvid::pack_bytes(static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16));
  };
  const uint16_t fg = pack(x->text), bg = pack(x->bk);
  HudTextSource* hud_text = nullptr;
  if (hud_text_enabled_ && !img->palette && !img->alpha && img->width > 0 && img->height > 0) {
    auto& source = hud_text_sources_[img];
    const auto count = size_t(img->width) * img->height;
    const auto hash = platform::hud_identity(img->width, img->height, px, count);
    if (source.pixels_hash != hash) {
      source.lines.clear();
      source.base.assign(px, px + count);
    }
    hud_text = &source;
  }
  int y0 = rect->top;
  if (format & kSingleLine) {
    if (format & kVCenter)
      y0 = rect->top + (rect->bottom - rect->top - lh) / 2;
    else if (format & kBottom)
      y0 = rect->bottom - lh;
  }
  for (size_t i = 0; i < ls.size(); ++i) {
    const std::string& l = ls[i];
    int w = 0, h = 0;
    if (!l.empty()) TTF_GetStringSize(f->ttf, l.c_str(), l.size(), &w, &h);
    int x0 = rect->left;
    if (format & kCenter)
      x0 = rect->left + (box_w - w) / 2;
    else if (format & kRight)
      x0 = rect->right - w;
    const int ly = y0 + static_cast<int>(i) * lh;
    auto put = [&](int px_, int py_, uint16_t c) {
      if (px_ < rect->left || px_ >= rect->right || py_ < rect->top || py_ >= rect->bottom)
        return;  // clipped
      if (px_ < 0 || py_ < 0 || px_ >= img->width || py_ >= img->height) return;
      px[py_ * img->width + px_] = c;
    };
    if (x->bk_mode == 2)  // OPAQUE: the text's cells filled first
      for (int yy = 0; yy < lh; ++yy)
        for (int xx = 0; xx < w; ++xx) put(x0 + xx, ly + yy, bg);
    if (l.empty()) continue;
    SDL_Surface* surf =
        TTF_RenderText_Solid(f->ttf, l.c_str(), l.size(), SDL_Color{255, 255, 255, 255});
    if (!surf) continue;
    if (hud_text) {
      HudTextLine line{l, f->face, f->em,      f->weight, int(f->italic), x0,   ly, ly + f->ascent,
                       w, lh,      x->bk_mode, *rect,     x->text,        x->bk};
      // Repainting an unchanged native glyph is idempotent. Keeping another
      // antialiased recipe would darken its edge and grow the cache forever.
      const auto same_recipe = [&](const auto& old) {
        return old.text == line.text && old.face == line.face && old.em == line.em &&
               old.weight == line.weight && old.italic == line.italic && old.x == line.x &&
               old.y == line.y && old.baseline == line.baseline && old.width == line.width &&
               old.height == line.height && old.background_mode == line.background_mode &&
               old.colour == line.colour && old.background == line.background &&
               old.clip.left == line.clip.left && old.clip.top == line.clip.top &&
               old.clip.right == line.clip.right && old.clip.bottom == line.clip.bottom;
      };
      if (hud_text->lines.empty() || !same_recipe(hud_text->lines.back())) {
        if (hud_text->lines.size() >= 4096)
          throw std::runtime_error("HUD text recipe budget exceeded");
        hud_text->lines.push_back(std::move(line));
      }
    }
    const auto* p = static_cast<const uint8_t*>(surf->pixels);
    for (int yy = 0; yy < surf->h; ++yy)
      for (int xx = 0; xx < surf->w; ++xx)
        if (p[yy * surf->pitch + xx]) put(x0 + xx, ly + yy + f->shift, fg);
    SDL_DestroySurface(surf);
  }
  if (hud_text)
    hud_text->pixels_hash =
        platform::hud_identity(img->width, img->height, px, size_t(img->width) * img->height);
  return total;
}

std::shared_ptr<const platform::HudImage> SdlTwodHost::hud_text_image(const zimg::Texture* source,
                                                                      float density, uint16_t key) {
  auto found = hud_text_sources_.find(source);
  if (!hud_text_enabled_ || found == hud_text_sources_.end() || found->second.lines.empty() ||
      !std::isfinite(density) || density <= 1 || source->palette || source->alpha)
    return {};
  auto& record = found->second;
  const size_t count = size_t(source->width) * source->height;
  if (record.base.size() != count ||
      record.pixels_hash != platform::hud_identity(source->width, source->height,
                                                   static_cast<const uint16_t*>(source->pixels),
                                                   count))
    return {};
  const int scale = std::clamp(int(std::lround(density * 256)), 256, 2048);
  uint64_t signature = platform::hud_identity(source->width, source->height, record.base);
  auto bytes = [&](const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
      signature ^= p[i];
      signature *= 0x100000001b3ULL;
    }
  };
  bytes(&key, sizeof key);
  bytes(&source->flags, sizeof source->flags);
  for (const auto& line : record.lines) {
    bytes(line.text.data(), line.text.size());
    bytes(line.face.data(), line.face.size());
    for (int v :
         {line.em, line.weight, line.italic, line.x, line.y, line.baseline, line.width, line.height,
          line.background_mode, line.clip.left, line.clip.top, line.clip.right, line.clip.bottom})
      bytes(&v, sizeof v);
    bytes(&line.colour, sizeof line.colour);
    bytes(&line.background, sizeof line.background);
  }
  if (record.cached && record.cached_scale == scale && record.cached_signature == signature)
    return record.cached;
  auto result = std::make_shared<platform::HudImage>();
  const float k = scale / 256.f;
  result->width = unsigned(std::ceil(source->width * k));
  result->height = unsigned(std::ceil(source->height * k));
  if (result->width > 8192 || result->height > 8192 ||
      uint64_t(result->width) * result->height > 16 * 1024 * 1024)
    throw std::runtime_error("HUD text exceeds the output image budget");
  result->pixels.resize(size_t(result->width) * result->height);
  for (unsigned y = 0; y < result->height; ++y)
    for (unsigned x = 0; x < result->width; ++x) {
      const uint16_t pixel = record.base[std::min(source->height - 1, int(y / k)) * source->width +
                                         std::min(source->width - 1, int(x / k))];
      result->pixels[size_t(y) * result->width + x] =
          (source->flags & zimg::kAlpha) && pixel == key ? 0 : SdlScreen2D::rgba(pixel, true);
    }
  auto composite = [&](int x, int y, uint32_t colour, unsigned alpha, const ztwod::Rect& clip) {
    if (x < 0 || y < 0 || x >= int(result->width) || y >= int(result->height) ||
        x < int(std::floor(clip.left * k)) || y < int(std::floor(clip.top * k)) ||
        x >= int(std::ceil(clip.right * k)) || y >= int(std::ceil(clip.bottom * k)))
      return;
    auto& destination = result->pixels[size_t(y) * result->width + x];
    uint32_t mixed = (alpha + (destination >> 24) * (255 - alpha) / 255) << 24;
    for (unsigned shift : {0U, 8U, 16U}) {
      const unsigned component = (((colour >> shift) & 255) * alpha +
                                  ((destination >> shift) & 255) * (255 - alpha) + 127) /
                                 255;
      mixed |= component << shift;
    }
    destination = mixed;
  };
  for (const auto& line : record.lines) {
    const std::string font_key = line.face + '\n' + std::to_string(line.em) + '/' +
                                 std::to_string(line.weight) + '/' + std::to_string(line.italic) +
                                 '/' + std::to_string(scale);
    auto font = hud_fonts_.find(font_key);
    if (font == hud_fonts_.end()) {
      if (hud_fonts_.size() >= 64) {
        for (const auto& old : hud_fonts_) TTF_CloseFont(old.second);
        hud_fonts_.clear();
      }
      const auto path = find_text_font(font_file(line.face));
      TTF_Font* opened = path.empty() ? nullptr : TTF_OpenFont(path.c_str(), line.em * k);
      if (!opened) throw std::runtime_error("Cannot open the enhanced HUD font");
      TTF_SetFontHinting(opened, TTF_HINTING_LIGHT);
      TTF_SetFontKerning(opened, false);
      TTF_SetFontStyle(
          opened, (line.weight >= 600 ? TTF_STYLE_BOLD : 0) | (line.italic ? TTF_STYLE_ITALIC : 0));
      font = hud_fonts_.emplace(font_key, opened).first;
    }
    const int left = int(std::lround(line.x * k)), top = int(std::lround(line.y * k));
    const int width = std::max(1, int(std::lround(line.width * k)));
    if (line.background_mode == 2)
      for (int y = top; y < int(std::lround((line.y + line.height) * k)); ++y)
        for (int x = left; x < left + width; ++x) composite(x, y, line.background, 255, line.clip);
    SDL_Surface* rendered = TTF_RenderText_Blended(font->second, line.text.c_str(),
                                                   line.text.size(), SDL_Color{255, 255, 255, 255});
    if (!rendered) throw std::runtime_error("Cannot rasterize enhanced HUD text");
    SDL_Surface* converted = SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(rendered);
    if (!converted) throw std::runtime_error("Cannot convert enhanced HUD text");
    const int glyph_top = int(std::lround(line.baseline * k)) - TTF_GetFontAscent(font->second);
    const auto* pixels = static_cast<const uint8_t*>(converted->pixels);
    for (int y = 0; y < converted->h; ++y)
      for (int x = 0; x < width; ++x) {
        const int sx = std::min(converted->w - 1, int(int64_t(x) * converted->w / width));
        composite(left + x, glyph_top + y, line.colour, pixels[y * converted->pitch + sx * 4 + 3],
                  line.clip);
      }
    SDL_DestroySurface(converted);
  }
  record.cached = result;
  record.cached_scale = scale;
  record.cached_signature = signature;
  return result;
}

int32_t SdlTwodHost::GetTextMetricsA(Handle d, ztwod::TextMetrics* out) {
  *out = ztwod::TextMetrics{};
  Dc* x = dc(d);
  Font* f = x ? font(x->font) : nullptr;
  if (!f || !f->ttf) return 0;
  out->height = f->ascent + f->descent;
  out->ascent = f->ascent;
  out->internal_leading = out->height - f->em > 0 ? out->height - f->em : 0;
  const int skip = TTF_GetFontLineSkip(f->ttf);
  out->external_leading = skip > out->height ? skip - out->height : 0;
  return 1;
}

void SdlTwodHost::SetTextColor(Handle d, uint32_t color) {
  if (Dc* x = dc(d)) x->text = color;
}
void SdlTwodHost::SetBkColor(Handle d, uint32_t color) {
  if (Dc* x = dc(d)) x->bk = color;
}
void SdlTwodHost::SetBkMode(Handle d, int32_t mode) {
  if (Dc* x = dc(d)) x->bk_mode = mode;
}

}  // namespace sdlfe
