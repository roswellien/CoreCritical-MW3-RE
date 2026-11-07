#include "frontend/sdl/launcher.h"
#include "frontend/sdl/data_folder_start.h"
#include "frontend/sdl/launcher_notes.h"
#include "frontend/sdl/movie_import.h"
#include "frontend/sdl/text_fit.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#include "frontend/sdl/sdl_install.h"
#include "gamez/zutil/zutl_zar.h"
#include "gamez/zrender/zrndr_ripple.h"
#include "mech3/menu_ripple.h"
#include "gamez/zrender/zrndr_tint.h"
#include "mech3/game_setup.h"
#include "mech3/source_map.h"
#include "gamez/zutil/zutl_path.h"

namespace sdlfe {

namespace {
// How long the launcher waits for the game's first frame before it says so
// on its status line: 20 s (MW3_LAUNCHER_READY_MS for the checks).
double ready_wait_ms() {
  const char* e = SDL_getenv("MW3_LAUNCHER_READY_MS");
  return e ? std::atof(e) : 20000.0;
}
namespace fs = std::filesystem;
constexpr int W = 640, H = 480;
constexpr int kBadge = 76;  // a game's badge across, on the 640x480 canvas

// ---- art ----

std::string assets_dir() {
  std::error_code ec;
  if (const char* base = SDL_GetBasePath()) {
    const fs::path p = fs::path(base) / "assets" / "launcher";
    if (fs::is_directory(p, ec)) return p.string();
  }
#ifdef MW3_ASSETS_DIR
  const fs::path p = fs::path(MW3_ASSETS_DIR) / "launcher";
  if (fs::is_directory(p, ec)) return p.string();
#endif
  return {};
}
// The font: assets/launcher/font.ttf when shipped, else a condensed system
// face (a placeholder until Owen picks one; launcher.md, Open).
std::string font_path(const std::string& dir) {
  std::error_code ec;
  std::vector<std::string> tries;
  if (!dir.empty()) tries.push_back((fs::path(dir) / "font.ttf").string());
  tries.insert(tries.end(), {"C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/arialbd.ttf",
                             "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf",
                             "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                             // Linux (docs/linux.md): a narrow face first, at Debian/Ubuntu's,
                             // Fedora's and Arch's places, then a plain bold one.
                             "/usr/share/fonts/truetype/liberation/LiberationSansNarrow-Bold.ttf",
                             "/usr/share/fonts/liberation-narrow/LiberationSansNarrow-Bold.ttf",
                             "/usr/share/fonts/liberation/LiberationSansNarrow-Bold.ttf",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf",
                             "/usr/share/fonts/dejavu-sans-fonts/DejaVuSansCondensed-Bold.ttf",
                             "/usr/share/fonts/TTF/DejaVuSansCondensed-Bold.ttf",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                             "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
                             "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                             "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"});
  for (const std::string& t : tries)
    if (fs::exists(t, ec)) return t;
  return {};
}

SDL_Surface* load_png(const std::string& dir, const char* name) {
  if (dir.empty()) return nullptr;
  SDL_Surface* s = SDL_LoadPNG((fs::path(dir) / name).string().c_str());
  if (!s) return nullptr;
  SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
  SDL_DestroySurface(s);
  return c;
}
// The picture's lit part (brighter than `thresh`), cropped; with `alpha`, its
// near-black made see-through (the generated art is on black).
SDL_Surface* cut_out(SDL_Surface* s, int thresh, bool alpha) {
  if (!s) return nullptr;
  int x0 = s->w, y0 = s->h, x1 = -1, y1 = -1;
  for (int y = 0; y < s->h; ++y) {
    const uint32_t* row = reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(s->pixels) + y * s->pitch);
    for (int x = 0; x < s->w; ++x) {
      const uint32_t p = row[x];
      const int lum = std::max({int(p >> 16 & 0xff), int(p >> 8 & 0xff), int(p & 0xff)});
      if (lum > thresh) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
  }
  if (x1 < x0) return s;
  SDL_Surface* c = SDL_CreateSurface(x1 - x0 + 1, y1 - y0 + 1, SDL_PIXELFORMAT_ARGB8888);
  const SDL_Rect r{x0, y0, c->w, c->h};
  SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
  SDL_BlitSurface(s, &r, c, nullptr);
  SDL_DestroySurface(s);
  if (alpha) {
    for (int y = 0; y < c->h; ++y) {
      uint32_t* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(c->pixels) + y * c->pitch);
      for (int x = 0; x < c->w; ++x) {
        const uint32_t p = row[x];
        const int lum = std::max({int(p >> 16 & 0xff), int(p >> 8 & 0xff), int(p & 0xff)});
        const int a = std::clamp((lum - 18) * 255 / 42, 0, 255);
        row[x] = (p & 0xffffffu) | (static_cast<uint32_t>(a) << 24);
      }
    }
    SDL_SetSurfaceBlendMode(c, SDL_BLENDMODE_BLEND);
  }
  return c;
}
SDL_Surface* scaled(SDL_Surface* s, int w, int h) {
  if (!s) return nullptr;
  SDL_Surface* r = SDL_ScaleSurface(s, w, h, SDL_SCALEMODE_LINEAR);
  if (r) SDL_SetSurfaceBlendMode(r, SDL_BLENDMODE_BLEND);
  return r;
}

struct Art {
  SDL_Surface *stars = nullptr, *metal = nullptr, *plate = nullptr, *title = nullptr, *emblem = nullptr;
  SDL_Surface* badge[3] = {};  // the games' round badges: MechWarrior 3, Pirate's Moon, MechWarrior (1989)
  ~Art() {
    for (SDL_Surface* s : {stars, metal, plate, title, emblem, badge[0], badge[1], badge[2]})
      if (s) SDL_DestroySurface(s);
  }
  void load(const std::string& dir) {
    if (SDL_Surface* s = load_png(dir, "bg_stars.png")) stars = scaled(s, s->w * H / s->h, H), SDL_DestroySurface(s);
    if (SDL_Surface* s = load_png(dir, "bg_metal.png")) metal = scaled(s, s->w * 240 / s->h, 240), SDL_DestroySurface(s);
    if (stars) SDL_SetSurfaceBlendMode(stars, SDL_BLENDMODE_NONE);
    if (metal) SDL_SetSurfaceBlendMode(metal, SDL_BLENDMODE_NONE), SDL_SetSurfaceColorMod(metal, 190, 190, 190);
    plate = cut_out(load_png(dir, "plate.png"), 24, false);
    if (SDL_Surface* t = cut_out(load_png(dir, "title.png"), 24, true)) title = scaled(t, 420, t->h * 420 / t->w), SDL_DestroySurface(t);
    if (SDL_Surface* e = cut_out(load_png(dir, "emblem.png"), 24, true)) emblem = scaled(e, 72, 72), SDL_DestroySurface(e);
    const char* const badges[3] = {"badge_mw3.png", "badge_pm.png", "badge_mw1.png"};
    for (int i = 0; i < 3; ++i)
      if (SDL_Surface* b = cut_out(load_png(dir, badges[i]), 24, true)) badge[i] = scaled(b, kBadge, kBadge), SDL_DestroySurface(b);
  }
};

// ---- text ----

struct Text {
  std::string path;
  std::map<int, TTF_Font*> fonts;
  bool ok = false;
  ~Text() {
    for (auto& [k, f] : fonts) TTF_CloseFont(f);
    if (ok) TTF_Quit();
  }
  void open(const std::string& p) {
    path = p;
    ok = !p.empty() && TTF_Init();
  }
  TTF_Font* font(int size) {
    if (!ok) return nullptr;
    auto it = fonts.find(size);
    if (it != fonts.end()) return it->second;
    return fonts[size] = TTF_OpenFont(path.c_str(), static_cast<float>(size));
  }
  // `s` as drawn and measured: a narrow space doubled (widen_spaces).
  std::string shown(TTF_Font* f, int size, const std::string& s) {
    int adv = 0;
    if (!TTF_GetGlyphMetrics(f, ' ', nullptr, nullptr, nullptr, nullptr, &adv)) return s;
    return widen_spaces(s, adv, size);
  }
  // `s` at (x, y): align -1 left, 0 centred, 1 right of x; y its middle.
  void draw(SDL_Surface* dst, const std::string& s, int size, SDL_Color c, int x, int y, int align = 0) {
    TTF_Font* f = font(size);
    if (!f || s.empty()) return;
    const std::string text = shown(f, size, s);
    SDL_Surface* t = TTF_RenderText_Blended(f, text.c_str(), 0, c);
    if (!t) return;
    SDL_Rect r{x, y - t->h / 2, t->w, t->h};
    if (align == 0) r.x -= t->w / 2;
    if (align > 0) r.x -= t->w;
    SDL_Surface* shadow = TTF_RenderText_Blended(f, text.c_str(), 0, SDL_Color{0, 0, 0, 255});
    if (shadow) {
      SDL_Rect sr{r.x + 1, r.y + 2, r.w, r.h};
      SDL_BlitSurface(shadow, nullptr, dst, &sr), SDL_DestroySurface(shadow);
    }
    SDL_BlitSurface(t, nullptr, dst, &r);
    SDL_DestroySurface(t);
  }
  // `s` shortened from the middle with "..." until it is `width` across; its
  // first `keep` characters stay whole (a folder's start and end both show).
  std::string fit(const std::string& s, int size, int width, size_t keep = 0) {
    TTF_Font* f = font(size);
    if (!f) return s;
    auto w_of = [&](const std::string& t) {
      int w = 0, h = 0;
      TTF_GetStringSize(f, shown(f, size, t).c_str(), 0, &w, &h);
      return w;
    };
    return fit_middle(s, keep, width, w_of);
  }
  // Wrapped to `width`, lines centred on x from y; the height drawn.
  int draw_wrapped(SDL_Surface* dst, const std::string& s, int size, SDL_Color c, int x, int y, int width) {
    TTF_Font* f = font(size);
    if (!f) return 0;
    std::vector<std::string> lines;
    std::string line, word;
    auto fits = [&](const std::string& t) {
      int w = 0, h = 0;
      TTF_GetStringSize(f, shown(f, size, t).c_str(), 0, &w, &h);
      return w <= width;
    };
    auto flush_word = [&] {
      if (word.empty()) return;
      const std::string tryl = line.empty() ? word : line + " " + word;
      if (!fits(tryl) && !line.empty()) lines.push_back(line), line = word;
      else line = tryl;
      word.clear();
    };
    for (char ch : s) {
      if (ch == '\n') flush_word(), lines.push_back(line), line.clear();
      else if (ch == ' ') flush_word();
      else word += ch;
    }
    flush_word();
    if (!line.empty()) lines.push_back(line);
    const int lh = TTF_GetFontLineSkip(f);
    for (size_t i = 0; i < lines.size(); ++i) draw(dst, lines[i], size, c, x, y + static_cast<int>(i) * lh);
    return static_cast<int>(lines.size()) * lh;
  }
};

const SDL_Color kWhite{228, 230, 232, 255}, kAmber{255, 176, 64, 255}, kGrey{120, 124, 130, 255}, kDim{190, 190, 180, 255};

// The one picker's filters (Owen: one option): the sources first, any file
// as well (an installed copy or the disc's folder by one of its files).
const SDL_DialogFileFilter kGameFiles[] = {
    {"Your game: a disc image, a download, or Mech3.exe", "iso;cue;bin;img;zip;7z;rar;tar;gz;tgz;xz;exe"},
    {"All files", "*"}};
const SDL_DialogFileFilter kDiscFiles[] = {{"Your disc: its image (.cue, .iso) or any file of it", "cue;iso;bin;img"},
                                           {"All files", "*"}};

// The find screen's words (Owen, 2026-11-02): what the game needs, and that
// nothing of the player's is changed.
const char* const kFindWords =
    "MechWarrior 3 needs your own copy of the game. Show us your disc, a disc image or download (.iso, .cue, .zip), "
    "or a folder where it's installed. We copy what the game needs into your user folder; your disc and any "
    "installed copy are left untouched.";

// The main menu's own sounds (sounds.zrd's MAINMENU set: menu_rollover,
// sfx_button2.wav at 0.7; menu_select, sfx_button6.wav), read from the
// player's sound archive as zSnd reads it; none without an install (we ship
// no sounds of our own).
struct Sounds {
  enum { kRollover, kSelect };
  SDL_AudioStream* stream[2] = {};
  Uint8* audio[2] = {};
  Uint32 len[2] = {};
  ~Sounds() {
    for (int i = 0; i < 2; ++i) {
      if (stream[i]) SDL_DestroyAudioStream(stream[i]);
      SDL_free(audio[i]);
    }
  }
  bool load(const std::string& install) {
    static const char* const kFiles[2] = {"sfx_button2.wav", "sfx_button6.wav"};
    static const float kGain[2] = {0.7f, 1.0f};
    for (const char* arc : {"soundsL.zbd", "soundsH.zbd"}) {
      zutl_zar::Archive a;
      zutl_zar::init(a);
      const std::string path = zutl::any_case(install, std::string("zbd/") + arc);
      if (path.empty() || !fs::is_regular_file(path) || !zutl_zar::open(a, path.c_str())) {
        zutl_zar::destroy(a);
        continue;
      }
      int got = 0;
      for (int i = 0; i < 2; ++i) {
        const int e = zutl_zar::find(a, kFiles[i]);
        uint32_t size = 0;
        if (e < 0) continue;
        zutl_zar::read(a, e, nullptr, &size);
        std::vector<Uint8> bytes(size);
        if (!size || zutl_zar::read(a, e, bytes.data(), &size) != 0) continue;
        SDL_AudioSpec spec{};
        if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true, &spec, &audio[i], &len[i])) continue;
        stream[i] = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        // MW3_LAUNCHER_MUTE (checks): loaded and played at no volume.
        if (stream[i]) SDL_SetAudioStreamGain(stream[i], SDL_getenv("MW3_LAUNCHER_MUTE") ? 0.0f : kGain[i]), ++got;
      }
      zutl_zar::close(a);
      zutl_zar::destroy(a);
      if (got) {
        std::printf("mw3: launcher: the menu's sounds from %s (%d of 2)\n", path.c_str(), got);
        return true;
      }
    }
    return false;
  }
  void play(int i) {
    if (!stream[i]) return;
    SDL_ClearAudioStream(stream[i]);
    SDL_PutAudioStreamData(stream[i], audio[i], static_cast<int>(len[i]));
    SDL_ResumeAudioStreamDevice(stream[i]);
  }
};

// ---- the screens ----

struct Button {
  int id = 0;
  SDL_Rect r{};
  std::string label, sub;
  bool enabled = true;
  size_t sub_keep = 0;  // the sub's start kept whole when it is shortened to fit
  int badge = -1;       // a game's entry: its round badge (Art::badge), the name beside it
};

enum Id {
  kMw3 = 1, kPm, kQuit, kMw1, kFiles,
  kFind = 10, kPoint, kBack,
  kPatch,  // the 1.2 patch's zip: unreachable since the patch is built in (patch12.h), its code kept
  kAddDisc = 20,
  kGo = 30, kCancel, kNotesOk,
  kData = 40, kDataPick, kDataUsual, kMoveGo, kRemoveOld, kKeepOld,
};

struct Install {
  bool ok = false;
  std::string dir, version, summary;
};

// An operation on a worker thread (mech3::setup's calls run on the caller's).
struct Work {
  std::thread th;
  std::mutex m;
  mech3::setup::Progress progress;
  mech3::setup::Result result;
  std::atomic<bool> cancel{false}, done{false};
  std::string title;
  enum Kind { kOther, kLook, kInstallIt, kAddDisc, kConvert, kMoveData, kRemoveData } kind = kOther;  // a look, an install, ADD FROM MY DISC, the movies
  ~Work() {
    cancel = true;
    if (th.joinable()) th.join();
  }
  void start(std::string t, std::function<mech3::setup::Result(const mech3::setup::ProgressFn&)> op) {
    title = std::move(t);
    std::printf("mw3: launcher: work: %s\n", title.c_str());  // the checks follow it
    th = std::thread([this, op] {
      mech3::setup::Result r = op([this](const mech3::setup::Progress& p) {
        std::lock_guard<std::mutex> g(m);
        progress = p;
        return !cancel.load();
      });
      std::lock_guard<std::mutex> g(m);
      result = std::move(r);
      done = true;
    });
  }
};

// A picked file or folder from SDL's dialogs (their answers come through a
// callback, on another thread on some systems).
struct Pick {
  std::mutex m;
  int what = 0;  // the button that asked
  std::string path;
  bool answered = false, cancelled = false;
  static void SDLCALL done(void* user, const char* const* list, int) {
    auto* p = static_cast<Pick*>(user);
    std::lock_guard<std::mutex> g(p->m);
    p->answered = true;
    if (list && list[0]) p->path = list[0];
    else p->cancelled = true;
  }
};

class Launcher {
 public:
  explicit Launcher(const LauncherOptions& o) : o_(o) {}
  ~Launcher() {
    if (canvas_) SDL_DestroySurface(canvas_);
    if (tex_) SDL_DestroyTexture(tex_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
  }
  LauncherChoice run();

 private:
  void find_install();
  void find_mw1();
  void run_mw1();
  void start_game();
  void watch_game();
  void layout();
  void draw(float dt);
  void background(float dt);
  void plate(const Button& b, bool hot);
  void activate(int id);
  void ask_file(int what, const SDL_DialogFileFilter* filters, int n);
  void ask_folder(int what);
  void picked(int what, const std::string& path);
  void finish_work();
  void look_at(const std::string& path);
  void search();
  void use_install(const std::string& dir, const std::string& version, const std::string& summary);
  bool data_elsewhere() const;
  void ask_move(const std::string& to);
  void start_move();
  void moved(const mech3::setup::Result& r);
  void play();
  int movies_to_convert() const;
  void convert_movies(std::function<void()> then);
  void notes_refresh();
  std::string in_use() const;
  void add_disc(const std::string& source);
  void present();
  void nav(int action);  // 1 next, -1 previous, 2 press, 3 back (keys and a pad)

  LauncherOptions o_;
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  SDL_Texture* tex_ = nullptr;
  SDL_Surface* canvas_ = nullptr;
  Art art_;
  Text text_;
  enum Screen { kMain, kSetupScreen, kConfirm, kWorking, kNotes, kDataScreen } screen_ = kMain;
  std::vector<std::string> notes_;  // the first start's (launcher_notes)
  std::vector<Button> buttons_;
  int hot_ = -1, focus_ = -1;
  float hot_t_ = 0, phase_ = 0, scroll_ = 0;
  // The ripple (mech3::menu_ripple_step's state) and the button it is for.
  int frames_ = 0;                    // frames shown, and their draw and present times (the log)
  bool vsync_ = false;
  int quit_after_ = SDL_getenv("MW3_LAUNCHER_FRAMES") ? std::atoi(SDL_getenv("MW3_LAUNCHER_FRAMES")) : 0;
  double draw_ms_ = 0, present_ms_ = 0;
  int ring_for_ = -1, ring_radius_ = 0;
  bool drew_ripple_ = false, drew_edge_ = false;  // the last frame's hover (the shot's log)
  float ring_carry_ = 0, ring_amp_ = 0, ring_phase_ = 0;
  // The ripple's centre: a game's badge, else the plate's middle.
  SDL_Point ring_centre(const Button& b) const {
    if (b.badge >= 0) return {b.r.x + kBadge / 2 + 4, b.r.y + b.r.h / 2};
    return {b.r.x + b.r.w / 2, b.r.y + b.r.h / 2};
  }
  Install install_;
  SDL_Process* game_ = nullptr;  // mw3, started by MECHWARRIOR 3 (game_bin)
  Uint64 game_click_ = 0;
  bool game_slow_ = false;
  std::string game_status_;     // the game's own status line before its first frame
  double status_read_ms_ = -1e9;  // no first frame within ready_wait_ms(): the status line says so
  std::string ready_file_;
  std::string mw1_exe_, mw1_game_;  // MechWarrior (1989): Elthial's port beside ours, the player's folder
  std::string status_;
  bool status_good_ = true;
  std::unique_ptr<Work> work_;
  Pick pick_;
  bool asking_ = false;
  Uint64 ask_at_ = 0;      // when the picker was asked for (its timing in the log)
  bool ask_shown_ = false;  // the panel took the window's focus
  std::string confirm_text_, pending_source_;
  std::string move_to_, move_from_;  // the data folder's move (ask_move)
  std::vector<std::string> move_copied_;
  std::shared_ptr<std::vector<std::string>> move_copied_out_;
  int confirm_go_ = 0;
  std::shared_ptr<mech3::source::Found> found_;  // the last look's (look_at)
  bool auto_go_ = false;  // MW3_LAUNCHER_PICK (checks): its look's INSTALL pressed too
  bool auto_ok_ = false;  // and the notes' OK after it
  bool play_after_notes_ = false;  // the notes after an install: their OK starts the game
  bool from_files_ = false;        // the find screen opened by GAME FILES: a new pick goes back to the games
  std::function<void()> after_convert_;  // where the movies' conversion goes on to
  bool converted_ = false;               // tried once this start (a failure is not retried before the game)
  int disc_back_ = 0;              // the screen ADD FROM MY DISC returns to (Screen)
  bool lacks_music_ = false, lacks_movies_ = false;
  LauncherChoice choice_;
  bool done_ = false;
  Sounds sounds_;
  bool sounds_on_ = false, sounds_tried_ = false;  // loaded once there is an install (at the start or once found)
};

void Launcher::find_install() {
  install_ = {};
  std::vector<std::string> tries;
  if (!o_.cfg_install.empty()) tries.push_back(o_.cfg_install);
  if (!o_.write_dir.empty()) tries.push_back((fs::path(o_.write_dir) / "install").string());
  for (const std::string& c : install_candidates()) tries.push_back(c);
  for (const std::string& c : o_.more_installs) tries.push_back(c);
  for (const std::string& d : tries) {
    if (!install_valid(d)) continue;
    const mech3::setup::Result r = mech3::setup::check_install(d, o_.write_dir);
    if (!r.ok) continue;
    install_ = {true, r.install_dir.empty() ? d : r.install_dir, r.version, r.summary};
    std::printf("mw3: launcher: %s\n", r.summary.c_str());
    return;
  }
  std::printf("mw3: launcher: no MechWarrior 3 install found\n");
}

namespace {
// A MechWarrior (1989) folder: BTECH.EXE in it (any case).
bool mw1_game_valid(const std::string& dir) {
  std::error_code ec;
  if (dir.empty() || !fs::is_directory(dir, ec)) return false;
  for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
    std::string n = e.path().filename().string();
    for (char& c : n) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    if (n == "BTECH.EXE") return true;
  }
  return false;
}
}  // namespace

// Elthial's MechWarrior (1989) SDL port (docs/launcher.md): its program
// beside ours or in a MechWarrior1989 folder beside it (MW3_MW1_PORT for
// checks); the entry shows only when it is there.
void Launcher::find_mw1() {
#if defined(_WIN32)
  const char* name = "mechwarrior.exe";
#else
  const char* name = "mechwarrior";
#endif
  std::vector<fs::path> tries;
  if (const char* env = SDL_getenv("MW3_MW1_PORT")) tries.emplace_back(env);
  if (const char* base = SDL_GetBasePath()) tries.push_back(fs::path(base) / name), tries.push_back(fs::path(base) / "MechWarrior1989" / name);
  std::error_code ec;
  for (const fs::path& t : tries)
    if (fs::is_regular_file(t, ec)) {
      mw1_exe_ = t.string();
      std::printf("mw3: launcher: MechWarrior (1989) port at %s\n", mw1_exe_.c_str());
      break;
    }
  if (mw1_game_valid(o_.cfg_mw1_game)) mw1_game_ = o_.cfg_mw1_game;
}

// The port started as its own script starts it (`mechwarrior <game dir>
// <capture dir>`, the saves in MECHWARRIOR_SAVE_DIRECTORY), both under the
// write dir's mw1; this window hidden until it exits.
void Launcher::run_mw1() {
  const fs::path base = o_.write_dir.empty() ? fs::path("mw1") : fs::path(o_.write_dir) / "mw1";
  std::error_code ec;
  fs::create_directories(base / "captures", ec);
  fs::create_directories(base / "saves", ec);
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "MECHWARRIOR_SAVE_DIRECTORY", (base / "saves").string().c_str(), true);
  const std::string game = mw1_game_, captures = (base / "captures").string();
  const char* args[] = {mw1_exe_.c_str(), game.c_str(), captures.c_str(), nullptr};
  SDL_Process* p = SDL_CreateProcess(args, false);
  if (!p) {
    status_ = std::string("MechWarrior (1989) did not start: ") + SDL_GetError(), status_good_ = false;
    std::printf("mw3: launcher: %s\n", status_.c_str());
    return;
  }
  std::printf("mw3: launcher: MechWarrior (1989) started\n");
  if (win_) SDL_HideWindow(win_);
  int code = 0;
  while (!SDL_WaitProcess(p, false, &code)) {
    SDL_PumpEvents();
    SDL_Delay(50);
  }
  SDL_DestroyProcess(p);
  std::printf("mw3: launcher: MechWarrior (1989) exited (%d)\n", code);
  status_ = code == 0 ? "MechWarrior (1989) closed." : "MechWarrior (1989) closed with an error (" + std::to_string(code) + "): see its log.";
  status_good_ = code == 0;
  if (win_ && !o_.hidden) SDL_ShowWindow(win_), SDL_RaiseWindow(win_);
}

// MechWarrior 3 as its own program (game_bin): the install and the options
// passed on, the launcher kept on screen until the game's first frame
// (mw3 --ready-file) so the switch shows no empty moment, then closed: the
// game goes on alone (Owen, 2026-11-01).
void Launcher::start_game() {
  std::error_code ec;
  ready_file_ = (fs::temp_directory_path(ec) / ("mw3-ready-" + std::to_string(SDL_GetTicksNS()))).string();
  fs::remove(ready_file_, ec);
  std::vector<std::string> args = {o_.game_bin, install_.dir};
  args.insert(args.end(), o_.game_args.begin(), o_.game_args.end());
  args.push_back("--ready-file"), args.push_back(ready_file_);
  std::vector<const char*> argv;
  std::string line;
  for (const std::string& a : args) argv.push_back(a.c_str()), line += (line.empty() ? "" : " ") + a;
  argv.push_back(nullptr);
  save_install(o_.write_dir, install_.dir);
  game_click_ = SDL_GetTicksNS(), game_slow_ = false, game_status_.clear(), status_read_ms_ = -1e9;
  // Its own stdio, none of ours: the game writes mw3.log itself, and our
  // log's pipe in its stdout would hold this program's exit until the game's.
  const SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
  // The game's box after a crash would page from a mount going away: this
  // program's instead (watch_game), which stays for an AppImage.
  if (std::getenv("APPIMAGE")) SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "MW3_LAUNCHER_WATCHES", "1", true);
  game_ = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if (!game_) {
    status_ = std::string("MechWarrior 3 did not start: ") + SDL_GetError(), status_good_ = false;
    std::printf("mw3launcher: %s (%s)\n", status_.c_str(), line.c_str());
    layout();
    return;
  }
  std::printf("mw3launcher: started %s\n", line.c_str());
  status_ = "Starting MechWarrior 3...", status_good_ = true;
}

// Each pass while the game starts: at its first frame the launcher closes,
// the game left running; a game that exits first (an error) or shows no
// frame in 20 s leaves the launcher up with a status line, still watching.
void Launcher::watch_game() {
  if (!game_) return;
  std::error_code ec;
  const double ms = static_cast<double>(SDL_GetTicksNS() - game_click_) / 1e6;
  int code = 0;
  if (SDL_WaitProcess(game_, false, &code)) {
    SDL_DestroyProcess(game_);
    game_ = nullptr;
    fs::remove(ready_file_, ec);
    fs::remove(ready_file_ + ".status", ec);
    std::printf("mw3launcher: MechWarrior 3 exited (%d) after %.1f s\n", code, ms / 1000.0);
    status_ = code == 0 ? "" : "MechWarrior 3 closed with an error (" + std::to_string(code) + "): see mw3.log.";
    status_good_ = code == 0;
    if (win_ && !o_.hidden) SDL_ShowWindow(win_), SDL_RaiseWindow(win_);
    if (o_.quit_after_game) done_ = true;
    layout();
    return;
  }
  if (fs::exists(ready_file_, ec)) {
    std::printf("mw3launcher: the game's first frame %.0f ms after MECHWARRIOR 3 was pressed: the launcher closes\n", ms);
    fs::remove(ready_file_, ec);
    fs::remove(ready_file_ + ".status", ec);
    if (win_) SDL_HideWindow(win_);
    // FIX (A): an AppImage unmounts when its first process (this) ends, with
    // the game still paging its code and libraries from there: it would die
    // when it reaches a page not yet read, after the intro. So here this
    // waits, hidden, and says if the game crashes (docs/linux.md, Packaging).
    if (std::getenv("APPIMAGE")) {
      int code = 0;
      std::fflush(stdout);  // a long wait: the log has this far
      SDL_WaitProcess(game_, true, &code);
      std::printf("mw3launcher: MechWarrior 3 exited (%d) after %.1f s\n", code,
                  static_cast<double>(SDL_GetTicksNS() - game_click_) / 1e9);
      std::fflush(stdout);
      if (code < 0) {
        const std::string text = "MechWarrior 3 crashed (signal " + std::to_string(-code) + ").\n\nThe log is " +
                                 o_.write_dir + "/mw3.log\nPlease include it if you report this.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MechWarrior 3", text.c_str(), nullptr);
      }
    }
    SDL_DestroyProcess(game_);  // let go: the game runs on, where it has not ended above
    game_ = nullptr;
    done_ = true;
    return;
  }
  // What the game says it is doing before its first frame (its first start's
  // movie import, sdl_child.h), read four times a second: on the status line
  // instead of the wait's.
  if (ms - status_read_ms_ >= 250.0) {
    status_read_ms_ = ms;
    std::ifstream in(ready_file_ + ".status");
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line != game_status_) {
      game_status_ = line;
      std::printf("mw3launcher: the game says: %s\n", line.c_str());
      status_ = line, status_good_ = true;
      layout();
    }
  }
  if (!game_slow_ && game_status_.empty() && ms > ready_wait_ms()) {
    game_slow_ = true;
    std::printf("mw3launcher: no first frame from the game after %.0f s: the launcher stays\n", ready_wait_ms() / 1000.0);
    status_ = "MechWarrior 3 has not shown a picture yet: still waiting (see mw3.log).", status_good_ = false;
    layout();
  }
}

void Launcher::layout() {
  buttons_.clear();
  auto add = [&](int id, SDL_Rect r, std::string label, std::string sub = {}, bool on = true) {
    buttons_.push_back({id, r, std::move(label), std::move(sub), on});
  };
  if (sounds_on_ && install_.ok && !sounds_tried_) {  // any screen, once there is an install
    sounds_tried_ = true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) sounds_.load(install_.dir);
  }
  switch (screen_) {
    case kMain: {
      // A long folder is shortened to the plate when drawn (Text::fit), the
      // words before it kept.
      const std::string ready = "Ready: version " + install_.version + ", ";
      std::string sub = install_.ok ? ready + install_.dir : "Not found yet: click to find your game";
      // The games as round badges (Owen: the ripple belongs to a circle),
      // each name and line beside its badge, the ripple from its centre.
      auto game = [&](int id, int row, int badge, const char* name, const std::string& line, bool on) {
        add(id, {130, 118 + row * 84, 380, 80}, name, line, on);
        buttons_.back().badge = badge;
      };
      game(kMw3, 0, 0, "MECHWARRIOR 3", sub, true);
      if (sub.rfind(ready, 0) == 0) buttons_.back().sub_keep = ready.size();
      game(kPm, 1, 1, "PIRATE'S MOON", "Coming later", false);
      if (!mw1_exe_.empty()) {  // Elthial's port, credited on its line (MIT; THIRD-PARTY.txt)
        const std::string by = "Elthial's SDL port: ";
        game(kMw1, 2, 2, "MECHWARRIOR (1989)", by + (mw1_game_.empty() ? "click to pick your MechWarrior folder" : mw1_game_), true);
        buttons_.back().sub_keep = by.size();
      }
      // GAME FILES (Owen, 2026-11-02): the find screen at any time, to pick
      // or refresh the game's files; QUIT for fullscreen (Escape too).
      add(kFiles, {196, 384, 120, 34}, "GAME FILES");
      add(kQuit, {324, 384, 120, 34}, "QUIT");
      break;
    }
    case kSetupScreen: {
      // Find the game (Owen, 2026-11-02: one option): the search first (an
      // installed copy or a disc in a drive used at once), else one picker:
      // an image or a download, or any file of an installed copy (its
      // folder; pick_source). The 1.2 patch is built in (patch12.h).
      add(kFind, {130, 222, 380, 52}, "FIND THE GAME", "A disc image, a download (.zip .iso), or Mech3.exe in an installed copy");
      if (from_files_ && !o_.write_dir_fixed) add(kData, {130, 278, 380, 36}, "MY DATA FOLDER");
      if (install_.ok && (lacks_music_ || lacks_movies_))  // what the install in use lacks, from the disc
        add(kAddDisc, {130, 318, 380, 46}, "ADD FROM MY DISC", "A disc in a drive first, else its image or any file of the disc");
      add(kBack, {250, 384, 140, 34}, o_.setup_only ? "QUIT" : "BACK");
      break;
    }
    case kDataScreen:
      add(kDataPick, {130, 250, 380, 46}, "MOVE IT TO ANOTHER FOLDER", "Copied, checked, then switched. The old copy goes only if you say so");
      if (data_elsewhere()) add(kDataUsual, {130, 304, 380, 40}, "MOVE IT BACK TO THE USUAL FOLDER");
      add(kBack, {250, 384, 140, 34}, "BACK");
      break;
    case kConfirm:
      if (confirm_go_ == 3) {
        add(kRemoveOld, {115, 300, 260, 40}, "REMOVE THE OLD COPY");
        add(kKeepOld, {385, 300, 140, 40}, "KEEP IT");
        break;
      }
      if (confirm_go_ == 2) add(kMoveGo, {170, 300, 140, 40}, "MOVE");
      else if (confirm_go_) add(kGo, {170, 300, 140, 40}, "INSTALL");
      add(kBack, {330, 300, 140, 40}, "BACK");
      break;
    case kWorking:
      add(kCancel, {250, 372, 140, 34}, "CANCEL");
      break;
    case kNotes:
      if (lacks_music_ || lacks_movies_)  // what the install lacks, from the disc (lead, 2026-11-02)
        add(kAddDisc, {130, 318, 380, 46}, "ADD FROM MY DISC", "A disc in a drive first, else its image or any file of the disc");
      add(kNotesOk, {250, 384, 140, 34}, "OK");
      break;
  }
  {  // the screen and its plates (the checks follow them)
    static const char* const kNames[] = {"main", "find", "confirm", "work", "notes", "data"};
    std::string labels;
    for (const Button& b : buttons_) labels += (labels.empty() ? "" : ", ") + b.label;
    std::printf("mw3: launcher: screen %s: %s\n", kNames[screen_], labels.c_str());
  }
  hot_ = focus_ = -1;
}

void Launcher::background(float dt) {
  scroll_ += dt * 6.0f;  // a few pixels a second
  SDL_FillSurfaceRect(canvas_, nullptr, SDL_MapSurfaceRGB(canvas_, 8, 8, 12));
  if (art_.stars) {
    const int sw = art_.stars->w, x = static_cast<int>(scroll_) % sw;
    SDL_Rect src{x, 0, std::min(W, sw - x), H}, dst{0, 0, src.w, H};
    SDL_BlitSurface(art_.stars, &src, canvas_, &dst);
    if (src.w < W) {
      SDL_Rect s2{0, 0, W - src.w, H}, d2{src.w, 0, W - src.w, H};
      SDL_BlitSurface(art_.stars, &s2, canvas_, &d2);
    }
  }
  if (art_.metal) {
    const int span = art_.metal->w - W - 200, o = span > 0 ? static_cast<int>(scroll_ / 3) % span : 0;
    SDL_Rect top{o, 40, W, 96}, bot{o + 100, 150, W, 48};
    SDL_Rect dt0{0, 0, W, 96}, db{0, H - 48, W, 48};
    SDL_BlitSurface(art_.metal, &top, canvas_, &dt0);
    SDL_BlitSurface(art_.metal, &bot, canvas_, &db);
  }
  const uint32_t amber = SDL_MapSurfaceRGB(canvas_, 255, 176, 64);
  SDL_Rect l1{0, 96, W, 1}, l2{0, H - 49, W, 1};
  SDL_FillSurfaceRect(canvas_, &l1, amber), SDL_FillSurfaceRect(canvas_, &l2, amber);
  if (art_.title) {
    const int x = (W - art_.title->w - 84) / 2;
    if (art_.emblem) {
      SDL_Rect e{x, 12, art_.emblem->w, art_.emblem->h};
      SDL_BlitSurface(art_.emblem, nullptr, canvas_, &e);
    }
    SDL_Rect t{x + 84, 48 - art_.title->h / 2, art_.title->w, art_.title->h};
    SDL_BlitSurface(art_.title, nullptr, canvas_, &t);
  } else {
    text_.draw(canvas_, "MW3 REBUILD", 40, kWhite, W / 2, 48);
  }
}

void Launcher::plate(const Button& b, bool hot) {
  SDL_Rect r = b.r;
  if (b.badge >= 0) {  // a game: its badge, the name and its line beside it
    const SDL_Point c = ring_centre(b);
    SDL_Surface* art = art_.badge[b.badge];
    const uint8_t m = !b.enabled ? 80 : hot ? 255 : 205;
    if (art) {
      SDL_SetSurfaceColorMod(art, m, m, m);
      SDL_Rect d{c.x - kBadge / 2, c.y - kBadge / 2, kBadge, kBadge};
      SDL_BlitSurface(art, nullptr, canvas_, &d);
    } else {  // no art: a plain disc
      for (int y = -kBadge / 2; y <= kBadge / 2; ++y) {
        const int half = static_cast<int>(std::sqrt(static_cast<float>(kBadge * kBadge / 4 - y * y)));
        SDL_Rect row{c.x - half, c.y + y, half * 2, 1};
        SDL_FillSurfaceRect(canvas_, &row, SDL_MapSurfaceRGB(canvas_, m / 3, m / 3, m / 3 + 8));
      }
    }
    const SDL_Color col = !b.enabled ? kGrey : hot ? kAmber : kWhite;
    const int tx = c.x + kBadge / 2 + 18, tw = r.x + r.w - tx;
    text_.draw(canvas_, b.label, 22, col, tx, c.y - 9, -1);
    text_.draw(canvas_, text_.fit(b.sub, 12, tw, b.sub_keep), 12, b.enabled ? kDim : kGrey, tx, c.y + 14, -1);
    return;
  }
  if (art_.plate) {
    const uint8_t m = !b.enabled ? 90 : hot ? 255 : 215;
    SDL_SetSurfaceColorMod(art_.plate, m, m, m);
    SDL_BlitSurfaceScaled(art_.plate, nullptr, canvas_, &r, SDL_SCALEMODE_LINEAR);
  } else {
    SDL_FillSurfaceRect(canvas_, &r, SDL_MapSurfaceRGB(canvas_, b.enabled ? 70 : 40, b.enabled ? 72 : 40, b.enabled ? 78 : 44));
  }
  if (hot) {  // a square plate lights its edge in the frame's amber (the ripple is the round badges'; Owen)
    const uint32_t amber = SDL_MapSurfaceRGB(canvas_, 255, 176, 64), soft = SDL_MapSurfaceRGB(canvas_, 150, 104, 40);
    const SDL_Rect outer[4] = {{r.x, r.y, r.w, 1}, {r.x, r.y + r.h - 1, r.w, 1}, {r.x, r.y, 1, r.h}, {r.x + r.w - 1, r.y, 1, r.h}};
    const SDL_Rect inner[4] = {{r.x + 1, r.y + 1, r.w - 2, 1}, {r.x + 1, r.y + r.h - 2, r.w - 2, 1},
                               {r.x + 1, r.y + 1, 1, r.h - 2}, {r.x + r.w - 2, r.y + 1, 1, r.h - 2}};
    SDL_FillSurfaceRects(canvas_, outer, 4, amber);
    SDL_FillSurfaceRects(canvas_, inner, 4, soft);
    drew_edge_ = true;
  }
  const SDL_Color c = !b.enabled ? kGrey : hot ? kAmber : kWhite;
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  const int big = r.h >= 50 ? 22 : r.h >= 40 ? 20 : 18;
  if (b.sub.empty()) {
    text_.draw(canvas_, b.label, big, c, cx, cy + 1);
  } else {
    text_.draw(canvas_, b.label, big, c, cx, cy - 8);
    text_.draw(canvas_, text_.fit(b.sub, 12, r.w - 20, b.sub_keep), 12, b.enabled ? kDim : kGrey, cx, cy + 13);
  }
}

void Launcher::draw(float dt) {
  background(dt);
  switch (screen_) {
    case kMain:
      if (!status_.empty()) {
        SDL_Rect box{110, 104, 420, 24};
        SDL_FillSurfaceRect(canvas_, &box, SDL_MapSurfaceRGB(canvas_, status_good_ ? 16 : 30, status_good_ ? 26 : 14, 16));
        text_.draw(canvas_, status_, 12, status_good_ ? SDL_Color{200, 230, 200, 255} : SDL_Color{240, 190, 180, 255}, W / 2, 116);
      }
      text_.draw(canvas_, "Unofficial fan rebuild. MechWarrior is a trademark of Microsoft.", 12, kDim, 16, H - 24, -1);
      break;
    case kSetupScreen: {
      SDL_Rect box{70, 104, 500, 64};
      SDL_FillSurfaceRect(canvas_, &box, SDL_MapSurfaceRGB(canvas_, 16, 18, 22));
      text_.draw_wrapped(canvas_, kFindWords, 13, kWhite, W / 2, 114, 480);
      if (!status_.empty())
        text_.draw_wrapped(canvas_, status_, 13, status_good_ ? kAmber : SDL_Color{240, 190, 180, 255}, W / 2, 184, 480);
      else if (install_.ok)  // GAME FILES: the one in use, and what it lacks
        text_.draw(canvas_, text_.fit(in_use(), 13, 480), 13, kDim, W / 2, 190);
      break;
    }
    case kNotes: {
      text_.draw(canvas_, "BEFORE YOU PLAY", 24, kAmber, W / 2, 128);
      int y = 162;
      for (const std::string& n : notes_) {
        y += text_.draw_wrapped(canvas_, n, 14, kWhite, W / 2, y, 470) + 14;
      }
      if (!status_.empty())  // what ADD FROM MY DISC did, or where to look
        text_.draw_wrapped(canvas_, status_, 13, status_good_ ? kAmber : SDL_Color{240, 190, 180, 255}, W / 2, 296, 480);
      break;
    }
    case kConfirm:
      text_.draw_wrapped(canvas_, confirm_text_, 16, kWhite, W / 2, 150, 460);
      break;
    case kDataScreen: {
      text_.draw_wrapped(canvas_, "Your settings, pilots, saved games and converted movies are in:", 16, kWhite, W / 2, 130, 480);
      text_.draw_wrapped(canvas_, text_.fit(o_.write_dir, 14, 480), 14, kAmber, W / 2, 176, 480);
      if (!status_.empty())
        text_.draw_wrapped(canvas_, status_, 13, status_good_ ? kAmber : SDL_Color{240, 190, 180, 255}, W / 2, 350, 480);
      break;
    }
    case kWorking: {
      mech3::setup::Progress p;
      std::string title;
      if (work_) {
        std::lock_guard<std::mutex> g(work_->m);
        p = work_->progress, title = work_->title;
      }
      text_.draw(canvas_, title, 26, kAmber, W / 2, 140);
      SDL_Rect bar{120, 200, 400, 26}, in{123, 203, static_cast<int>(394 * std::clamp(p.fraction, 0.0, 1.0)), 20};
      SDL_FillSurfaceRect(canvas_, &bar, SDL_MapSurfaceRGB(canvas_, 140, 100, 50));
      SDL_Rect inner{122, 202, 396, 22};
      SDL_FillSurfaceRect(canvas_, &inner, SDL_MapSurfaceRGB(canvas_, 10, 12, 14));
      SDL_FillSurfaceRect(canvas_, &in, SDL_MapSurfaceRGB(canvas_, 214, 140, 40));
      char pct[16];
      std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(std::lround(p.fraction * 100)));
      text_.draw(canvas_, pct, 14, kWhite, W / 2, 214);
      text_.draw(canvas_, p.stage, 13, kWhite, W / 2, 246);
      text_.draw(canvas_, p.message, 10, kGrey, W / 2, 266);
      text_.draw(canvas_, "Cancel stops within a file and changes nothing.", 12, kDim, 16, H - 24, -1);
      break;
    }
  }
  const int hot = focus_ >= 0 ? focus_ : hot_;
  drew_ripple_ = drew_edge_ = false;
  for (size_t i = 0; i < buttons_.size(); ++i) plate(buttons_[i], static_cast<int>(i) == hot && buttons_[i].enabled);
  // The main menu's own ripple (its step, mech3::menu_ripple_step, and its
  // draw 0x00558dd0) round the round button (a game's badge) under the
  // mouse, started afresh on each new one as SlideButton's on_enter does;
  // a square plate lights its edge instead (plate).
  hot_t_ += dt;
  if (hot != ring_for_) {
    ring_for_ = hot, ring_radius_ = mech3::MenuRipple::kStartRadius, ring_carry_ = 0.0f;
    ring_amp_ = mech3::MenuRipple::kStartAmplitude, ring_phase_ = 0.0f;
  }
  if (hot >= 0 && hot < static_cast<int>(buttons_.size()) && buttons_[hot].enabled && buttons_[hot].badge >= 0 &&
      mech3::menu_ripple_step(ring_radius_, ring_carry_, ring_amp_, ring_phase_, mech3::MenuRipple::kSpeed,
                              mech3::MenuRipple::kWavelength, mech3::MenuRipple::kMaxRadius, dt)) {
    const SDL_Point c = ring_centre(buttons_[hot]);
    const float wl = mech3::MenuRipple::kWavelength;
    zrndr::set_finish_target(static_cast<uint16_t*>(canvas_->pixels), W, H, canvas_->pitch);
    zrndr::ripple(c.x, c.y, static_cast<int32_t>(static_cast<float>(ring_radius_) - wl * 3.0f), ring_radius_,
                  static_cast<int32_t>(ring_amp_), wl, ring_phase_, nullptr, zrndr::X87::k53);
    drew_ripple_ = true;
  }
}

void Launcher::present() {
  SDL_UpdateTexture(tex_, nullptr, canvas_->pixels, canvas_->pitch);
  SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
  SDL_RenderClear(ren_);
  SDL_RenderTexture(ren_, tex_, nullptr, nullptr);
  SDL_RenderPresent(ren_);
}

void Launcher::ask_file(int what, const SDL_DialogFileFilter* filters, int n) {
  {
    std::lock_guard<std::mutex> g(pick_.m);
    pick_.what = what, pick_.path.clear(), pick_.answered = pick_.cancelled = false;
  }
  asking_ = true, ask_shown_ = false, ask_at_ = SDL_GetTicksNS();
  std::printf("mw3: launcher: the file picker asked for\n");
  SDL_ShowOpenFileDialog(Pick::done, &pick_, win_, filters, n, nullptr, false);
}
void Launcher::ask_folder(int what) {
  {
    std::lock_guard<std::mutex> g(pick_.m);
    pick_.what = what, pick_.path.clear(), pick_.answered = pick_.cancelled = false;
  }
  asking_ = true, ask_shown_ = false, ask_at_ = SDL_GetTicksNS();
  std::printf("mw3: launcher: the folder picker asked for\n");
  SDL_ShowOpenFolderDialog(Pick::done, &pick_, win_, nullptr, false);
}

// A picked path: what the button that asked does with it.
void Launcher::picked(int what, const std::string& path) {
  using namespace mech3::setup;
  if (what == kMw1) {
    const std::string dir = fs::is_regular_file(path) ? fs::path(path).parent_path().string() : path;
    if (!mw1_game_valid(dir)) {
      status_ = "That folder has no BTECH.EXE: it is not MechWarrior (1989).", status_good_ = false;
      return;
    }
    mw1_game_ = dir;
    save_config_value(o_.write_dir, "mw1_game", dir);
    layout();
    run_mw1();
    return;
  }
  if (what == kDataPick) return ask_move(mech3::data_folder::destination(path, usual_folder()));
  if (what == kPoint) {  // an image or archive as it is, any other file for its folder
    look_at(pick_source(path, {"iso", "cue", "img", "zip", "7z", "rar", "tar", "gz", "tgz", "xz"}));
    return;
  } else if (what == kAddDisc) {
    add_disc(pick_source(path, {"iso", "cue", "img"}));
    return;
  } else if (what == kPatch) {  // unreachable (kPatch has no plate since the patch is built in); kept
    work_ = std::make_unique<Work>();
    const std::string wd = o_.write_dir;
    work_->start("APPLYING THE 1.2 PATCH", [path, wd](const ProgressFn& f) { return apply_patch(path, wd, f); });
    screen_ = kWorking;
  }
  layout();
}

// What the install lacks (the movies left on the CD, the music), taken from
// the disc, a disc image or the disc's folder (setup::add_from_disc).
void Launcher::add_disc(const std::string& source) {
  disc_back_ = screen_;
  work_ = std::make_unique<Work>();
  work_->kind = Work::kAddDisc;
  const std::string video = lacks_movies_ ? (fs::path(o_.write_dir) / "video").string() : std::string();
  const std::string music = lacks_music_ ? (fs::path(o_.write_dir) / "music").string() : std::string();
  work_->start("ADDING FROM YOUR DISC", [source, video, music](const mech3::setup::ProgressFn& f) {
    return mech3::setup::add_from_disc(source, video, music, f);
  });
  screen_ = kWorking;
  layout();
}

// The notes for the install now there (launcher_notes): what it lacks.
void Launcher::notes_refresh() {
  notes_.clear();
  lacks_music_ = lacks_movies_ = false;
  if (!install_.ok) return;
  const fs::path music = o_.write_dir.empty() ? fs::path("music") : fs::path(o_.write_dir) / "music";
  lacks_music_ = !have_music({music.string(), (zutl::any_case(install_.dir, "music").empty() ? fs::path(install_.dir) / "music" : fs::path(zutl::any_case(install_.dir, "music"))).string()});
  lacks_movies_ = !have_movies(install_.dir, o_.write_dir);
  const std::string dir = fs::path(music).make_preferred().string();
  for (const std::string& n : launcher_notes(install_.version, !lacks_music_, !lacks_movies_, dir))
    std::printf("mw3: launcher: note: %s\n", n.c_str());
  notes_ = launcher_notes(install_.version, !lacks_music_, !lacks_movies_, text_.fit(dir, 14, 440));  // a long folder shortened
}

// GAME FILES' line: the install in use and what it lacks.
std::string Launcher::in_use() const {
  std::string s = "Using MechWarrior 3 " + install_.version + " at " + install_.dir;
  if (lacks_movies_ && lacks_music_) s += ". No movies or music.";
  else if (lacks_movies_) s += ". No movies.";
  else if (lacks_music_) s += ". No music.";
  return s;
}

// MechWarrior 3 started: by mw3launcher as the game's own program, else
// (a plain mw3 start, or no game_bin) the window handed on.
void Launcher::play() {
  // The movies converted here first, with the bar (Owen: a first start sat
  // 44 s on "Starting MechWarrior 3..." while mw3 did it); mw3's own import
  // stays the fallback for a start without the launcher.
  if (!converted_ && movies_to_convert() > 0) {
    converted_ = true;
    convert_movies([this] { play(); });
    return;
  }
  if (!o_.setup_only && !o_.game_bin.empty()) {
    screen_ = kMain, layout();
    if (!game_) start_game();
    return;
  }
  choice_ = {LauncherChoice::kPlay, install_.dir};
  done_ = true;
}

// The movies the install and the write dir (ADD FROM MY DISC's) have not
// converted for the player (mw3import); none on Windows, whose helper plays
// the AVIs as they are, or without the importer or with MW3_NO_IMPORT.
int Launcher::movies_to_convert() const {
#if defined(_WIN32)
  return 0;
#else
  if (!install_.ok || o_.no_import || SDL_getenv("MW3_NO_IMPORT") || importer_path().empty()) return 0;
  std::error_code ec;
  const bool same = fs::equivalent(install_.dir, o_.write_dir, ec);
  return movies_missing(install_.dir, o_.write_dir) + (same ? 0 : movies_missing(o_.write_dir, o_.write_dir));
#endif
}

// mw3import on the worker, the bar following its "progress" lines; CANCEL
// stops it (the movies done stay; the rest wait for the next start).
void Launcher::convert_movies(std::function<void()> then) {
  after_convert_ = std::move(then);
  work_ = std::make_unique<Work>();
  work_->kind = Work::kConvert;
  const std::string install = install_.dir, wd = o_.write_dir;
  std::error_code ec;
  const bool same = fs::equivalent(install, wd, ec);
  work_->start("CONVERTING THE MOVIES", [install, wd, same](const mech3::setup::ProgressFn& f) {
    mech3::setup::Result r;
    const int a = movies_missing(install, wd), b = same ? 0 : movies_missing(wd, wd), total = a + b;
    int base = 0, code = 0;
    for (const auto& [src, n] : {std::pair<std::string, int>{install, a}, std::pair<std::string, int>{wd, b}}) {
      if (n == 0) continue;
      code = run_importer(src, wd, [&](int done, int, const std::string& name) {
        return f(mech3::setup::Progress{"Converting the movies (" + std::to_string(base + done) + " of " + std::to_string(total) + ")",
                                        total ? static_cast<double>(base + done) / total : 0, name});
      });
      if (code != 0) break;
      base += n;
    }
    r.ok = code == 0, r.cancelled = code == 1, r.movies = total;
    r.summary = r.ok          ? std::to_string(total) + " movies converted for this computer."
                : r.cancelled ? "Converting the movies was stopped; the rest are done when the game starts."
                              : "Some movies could not be converted; the game plays without them.";
    return r;
  });
  screen_ = kWorking, layout();
}

bool Launcher::data_elsewhere() const {
  std::error_code ec;
  const std::string usual = usual_folder();
  return !usual.empty() && !fs::equivalent(o_.write_dir, usual, ec) && o_.write_dir != usual;
}

// The player's folder to `to` (docs/program.md, The data folder): asked first,
// with the sizes; a move that cannot be done is said and nothing starts.
void Launcher::ask_move(const std::string& to) {
  const mech3::data_folder::Plan p = mech3::data_folder::plan(o_.write_dir, to, usual_folder());
  if (!p.error.empty()) {
    status_ = p.error, status_good_ = false;
    screen_ = kDataScreen, layout();
    return;
  }
  char size[64];
  std::snprintf(size, sizeof size, "%.2f GB, %.1f GB free there", static_cast<double>(p.bytes) / 1e9, static_cast<double>(p.free) / 1e9);
  move_to_ = to;
  confirm_text_ = "Move your data to " + to + "?\n\n" + std::to_string(p.files) + " files, " + size +
                  ". The old copy stays until you choose.";
  confirm_go_ = 2;
  screen_ = kConfirm, layout();
}

void Launcher::start_move() {
  work_ = std::make_unique<Work>();
  work_->kind = Work::kMoveData;
  const std::string from = o_.write_dir, to = move_to_, usual = usual_folder();
  move_from_ = from;
  auto copied = std::make_shared<std::vector<std::string>>();
  move_copied_out_ = copied;
  work_->start("MOVING YOUR DATA", [from, to, usual, copied](const mech3::setup::ProgressFn& f) {
    mech3::setup::Result r;
    const mech3::data_folder::Moved m = mech3::data_folder::move_data(from, to, usual, [&](double at, const std::string& name) {
      return f(mech3::setup::Progress{"Copying and checking", at, name});
    });
    *copied = m.copied;
    r.ok = m.ok, r.cancelled = m.cancelled;
    r.summary = m.ok ? "Your data is now in " + to + "." : m.cancelled ? "Stopped. Nothing was changed." : m.error;
    return r;
  });
  screen_ = kWorking, layout();
}

// The move done: this run goes on with the new folder (its own log stays in
// the old one until the next start), and the old copy is the player's to remove.
void Launcher::moved(const mech3::setup::Result& r) {
  if (!r.ok) {
    status_ = r.summary, status_good_ = r.cancelled;
    screen_ = kDataScreen, layout();
    return;
  }
  move_copied_ = *move_copied_out_;
  using mech3::data_folder::moved_path;
  const std::string from = move_from_, to = move_to_;
  if (install_.ok) install_.dir = moved_path(install_.dir, from, to);
  o_.cfg_install = moved_path(o_.cfg_install, from, to);
  mw1_game_ = moved_path(mw1_game_, from, to);
  o_.write_dir = to;
  bool named = false;
  for (size_t i = 0; i + 1 < o_.game_args.size(); ++i)
    if (o_.game_args[i] == "--write-dir") o_.game_args[i + 1] = to, named = true;
  if (!named) o_.game_args.push_back("--write-dir"), o_.game_args.push_back(to);
  notes_refresh();
  confirm_text_ = r.summary + "\n\nThe old copy is still in " + from + ".\nRemove it? Log files stay there.";
  confirm_go_ = 3;
  screen_ = kConfirm, layout();
  if (const char* a = SDL_getenv("MW3_LAUNCHER_MOVE_OLD"))  // checks: the answer
    activate(std::strcmp(a, "remove") == 0 ? kRemoveOld : kKeepOld);
}

// A found install used: saved to mw3.cfg, then straight on into the game
// (Owen, 2026-11-02); when it lacks the movies or the music, or its data is
// odd, the notes first, their OK the start (lead).
void Launcher::use_install(const std::string& dir, const std::string& version, const std::string& summary) {
  const std::string before = install_.ok ? install_.dir : std::string();
  install_ = {true, dir, version, summary};
  save_install(o_.write_dir, dir);
  status_ = summary, status_good_ = true;
  // GAME FILES moved to a copy used where it is: the launcher's own earlier
  // copy (<write dir>/install) is no longer pointed at, so it goes (a disc
  // or archive install replaces it in place, setup::swap_in_folder).
  const fs::path own = fs::path(o_.write_dir) / "install";
  std::error_code ec;
  if (!o_.write_dir.empty() && !before.empty() && fs::equivalent(before, own, ec) && !fs::equivalent(dir, own, ec) &&
      fs::exists(own / "mw3-install.txt", ec)) {
    std::printf("mw3: launcher: the earlier copy in %s is no longer used: removed\n", own.string().c_str());
    fs::remove_all(own, ec);
  }
  notes_refresh();
  if (from_files_) {  // GAME FILES: back to the games, the new files in use
    from_files_ = false;
    screen_ = kMain, layout();
    return;
  }
  if (!notes_.empty()) {
    play_after_notes_ = true, status_.clear();
    screen_ = kNotes, layout();
    if (auto_ok_) {  // checks (MW3_LAUNCHER_PICK): the notes' OK pressed
      auto_ok_ = false;
      activate(kNotesOk);
    }
    return;
  }
  play();
}

// What the player pointed at, looked into on the worker (an archive's look
// can take seconds: the MW3 disc's installer header is 209 MB into its image).
void Launcher::look_at(const std::string& path) {
  pending_source_ = path;
  found_ = std::make_shared<mech3::source::Found>();
  work_ = std::make_unique<Work>();
  work_->kind = Work::kLook;
  auto found = found_;
  const std::string name = fs::path(path).filename().string();
  work_->start("LOOKING AT YOUR GAME", [path, found, name](const mech3::setup::ProgressFn& f) {
    *found = mech3::source::map_source(path, [&](double at, const std::string& entry) {
      return f(mech3::setup::Progress{"Looking into " + (name.empty() ? path : name), at, entry});
    });
    mech3::setup::Result r;
    r.ok = found->game == mech3::source::Found::kMw3, r.cancelled = found->cancelled;
    r.summary = found->summary, r.detail = found->detail;
    return r;
  });
  screen_ = kWorking;
  layout();
}

// The automatic search: an installed copy (mw3.cfg's, the installer's
// places), else a disc in a drive (looked into as if picked).
void Launcher::search() {
  find_install();
  if (install_.ok) {
    use_install(install_.dir, install_.version, install_.summary);
    return;
  }
  const std::vector<mech3::setup::DiscSource> discs = mech3::setup::find_discs();
  if (!discs.empty()) {
    look_at(discs.front().path);
    return;
  }
  status_ = "Not found on this computer or in a disc drive. Pick it: a disc image, a download, or Mech3.exe.";
  status_good_ = true;
  screen_ = kSetupScreen;
  layout();
  ask_file(kPoint, kGameFiles, 2);  // the one picker (an installed copy by any of its files)
}

void Launcher::finish_work() {
  mech3::setup::Result r;
  {
    std::lock_guard<std::mutex> g(work_->m);
    r = work_->result;
  }
  if (work_->th.joinable()) work_->th.join();
  const Work::Kind kind = work_->kind;
  work_.reset();
  std::printf("mw3: launcher: %s%s%s\n", r.summary.c_str(), r.detail.empty() ? "" : " | ", r.detail.c_str());
  if (kind == Work::kRemoveData) {
    status_ = r.summary, status_good_ = r.ok;
    from_files_ = false, screen_ = kMain, layout();
    return;
  }
  if (kind == Work::kMoveData) return moved(r);
  if (kind == Work::kConvert) {  // on where it was going
    std::function<void()> then = std::move(after_convert_);
    after_convert_ = nullptr;
    if (then) then();
    return;
  }
  if (kind == Work::kAddDisc) {  // back to the notes, saying what was added
    status_ = r.summary, status_good_ = r.ok || r.cancelled;
    for (const std::string& w : r.warnings) status_ += " " + w;
    notes_refresh();
    const Screen back = disc_back_ == kSetupScreen ? kSetupScreen : kNotes;
    if (r.ok && r.movies > 0 && movies_to_convert() > 0) {  // the movies just added, converted at once
      const std::string said = status_;
      convert_movies([this, back, said] { status_ = said, screen_ = back, layout(); });
      return;
    }
    screen_ = back, layout();
    return;
  }
  if (kind == Work::kLook) {
    const mech3::source::Found& f = *found_;
    if (f.cancelled || !r.ok) {
      status_ = r.summary, status_good_ = f.cancelled;
      screen_ = kSetupScreen;
    } else if (f.kind == mech3::source::Found::kInstall && f.on_disk) {  // used where it is
      const mech3::setup::Result c = mech3::setup::check_install(f.path, o_.write_dir);
      if (c.ok) return use_install(c.install_dir.empty() ? f.path : c.install_dir, c.version, c.summary);
      status_ = c.summary, status_good_ = false, screen_ = kSetupScreen;
    } else {
      const std::string out = (fs::path(o_.write_dir) / "install").make_preferred().string();
      confirm_text_ = f.summary + "\n\nIt will be installed into " + out + ". Your own files are only read.";
      confirm_go_ = 1;
      screen_ = kConfirm;
      if (auto_go_) {
        auto_go_ = false;
        activate(kGo);
        return;
      }
    }
    layout();
    return;
  }
  status_ = r.summary, status_good_ = r.ok;
  for (const std::string& w : r.warnings) status_ += " " + w;
  if (r.ok && !r.install_dir.empty()) save_install(o_.write_dir, r.install_dir);
  find_install();
  if (kind == Work::kInstallIt && r.ok && install_.ok) return use_install(install_.dir, install_.version, status_);
  screen_ = install_.ok && r.ok ? kMain : kSetupScreen;
  layout();
}

void Launcher::activate(int id) {
  sounds_.play(Sounds::kSelect);
  static const SDL_DialogFileFilter kZip[] = {{"The 1.2 patch (.zip, .exe)", "zip;exe"}, {"All files", "*"}};
  switch (id) {
    case kMw3:
      if (install_.ok) play();
      else from_files_ = false, screen_ = kSetupScreen, status_.clear(), layout();  // its files to find first
      break;
    case kQuit: done_ = true; break;
    case kData:
      status_.clear();
      screen_ = kDataScreen, layout();
      break;
    case kDataPick: ask_folder(kDataPick); break;
    case kDataUsual: ask_move(usual_folder()); break;
    case kMoveGo: start_move(); break;
    case kRemoveOld: {
      work_ = std::make_unique<Work>();
      work_->kind = Work::kRemoveData;
      const std::string old = move_from_, now = o_.write_dir, usual = usual_folder();
      const std::vector<std::string> copied = move_copied_;
      work_->start("REMOVING THE OLD COPY", [old, now, usual, copied](const mech3::setup::ProgressFn&) {
        mech3::setup::Result r;
        const mech3::data_folder::Removed d = mech3::data_folder::remove_old(old, now, copied, usual);
        r.ok = d.error.empty() && d.failed.empty();
        r.summary = !d.error.empty()     ? d.error
                    : d.failed.empty()   ? "The old copy is removed."
                                         : std::to_string(d.failed.size()) + " files of the old copy could not be removed, in " + old + ".";
        return r;
      });
      screen_ = kWorking, layout();
      break;
    }
    case kKeepOld:
      status_ = "The old copy stays in " + move_from_ + ".", status_good_ = true;
      from_files_ = false, screen_ = kMain, layout();
      break;
    case kFiles:  // the find screen at any time (Owen): repick or refresh the game's files
      from_files_ = true, status_.clear();
      screen_ = kSetupScreen, layout();
      break;
    case kMw1:
      if (mw1_game_.empty()) {
        status_ = "Pick your MechWarrior (1989) folder: the one with BTECH.EXE.", status_good_ = true;
        ask_folder(kMw1);
      } else {
        run_mw1();
      }
      break;
    case kBack:
      if (screen_ == kNotes) return activate(kNotesOk);  // Escape on the notes: as OK
      if (o_.setup_only && screen_ == kSetupScreen) done_ = true;  // a plain start's QUIT
      else if (screen_ == kConfirm && confirm_go_ == 3) return activate(kKeepOld);  // Escape: nothing removed
      else if (screen_ == kConfirm && confirm_go_ == 2) screen_ = kDataScreen, layout();
      else if (screen_ == kConfirm) screen_ = kSetupScreen, layout();
      else if (screen_ == kDataScreen) screen_ = kSetupScreen, status_.clear(), layout();
      else screen_ = kMain, from_files_ = false, status_.clear(), layout();
      break;
    case kNotesOk:
      status_.clear();
      if (play_after_notes_) {
        play_after_notes_ = false;
        play();
      } else {
        screen_ = kMain, layout();
      }
      break;
    case kFind: search(); break;  // the usual places and the drives first, then the one picker
    case kAddDisc: {  // a disc in a drive first, then one picker: its image or any file of the disc
      const std::vector<mech3::setup::DiscSource> discs = mech3::setup::find_discs();
      if (!discs.empty()) {
        add_disc(discs.front().path);
      } else {
        status_ = "No disc in a drive. Pick its image (.cue .iso) or any file of the disc.", status_good_ = true;
        layout();
        ask_file(kAddDisc, kDiscFiles, 2);
      }
      break;
    }
    case kPatch: ask_file(kPatch, kZip, 2); break;  // unreachable: no plate (the patch is built in)
    case kGo: {
      work_ = std::make_unique<Work>();
      work_->kind = Work::kInstallIt;
      const auto found = found_;
      const std::string wd = o_.write_dir, out = (fs::path(wd) / "install").string(), music = (fs::path(wd) / "music").string();
      work_->start("INSTALLING MECHWARRIOR 3", [found, out, music, wd](const mech3::setup::ProgressFn& f) {
        return mech3::setup::install_from_source(*found, out, "", music, wd, f);
      });
      screen_ = kWorking, layout();
      break;
    }
    case kCancel:
      if (work_) work_->cancel = true;
      break;
    default: break;
  }
}

void Launcher::nav(int action) {
  const int n = static_cast<int>(buttons_.size());
  const int at = focus_ >= 0 ? focus_ : hot_;
  if (action == 1 || action == -1) {
    int f = at;
    for (int i = 0; i < n; ++i) {
      f = ((f < 0 ? (action > 0 ? -1 : 0) : f) + action + n) % n;
      if (buttons_[f].enabled) break;
    }
    focus_ = f, hot_t_ = 0;
    if (f >= 0) sounds_.play(Sounds::kRollover);
  } else if (action == 2 && at >= 0 && at < n && buttons_[at].enabled) {
    activate(buttons_[at].id);
  } else if (action == 3) {
    activate(screen_ == kMain ? kQuit : kBack);
  }
}

LauncherChoice Launcher::run() {
  const bool shot = !o_.shot.empty();
  // Where the start's time goes (Owen: "pretty slow"), in the log.
  const Uint64 t0 = SDL_GetTicksNS();
  auto ms = [](Uint64 a, Uint64 b) { return static_cast<double>(b - a) / 1e6; };
  const std::string dir = assets_dir();
  art_.load(dir);
  const Uint64 t_art = SDL_GetTicksNS();
  text_.open(font_path(dir));
  canvas_ = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGB565);
  const Uint64 t_font = SDL_GetTicksNS();
  find_install();
  const Uint64 t_install = SDL_GetTicksNS();
  find_mw1();
  std::printf("mw3: launcher: art %.0f ms, font %.0f ms, the install search %.0f ms\n", ms(t0, t_art), ms(t_art, t_font),
              ms(t_font, t_install));
  sounds_on_ = o_.sound && !shot;
  // The first start's notes (no install= in mw3.cfg yet: PLAY saves one),
  // on their own screen before the main one; logged on every start.
  notes_refresh();
  const bool first_start = o_.cfg_install.empty() && !notes_.empty();
  if (shot) {
    if (o_.screen == "setup" || o_.screen == "pick" || o_.screen == "files")
      screen_ = kSetupScreen, from_files_ = o_.screen == "files";
    if (o_.screen == "pick") status_ = "Not found on this computer or in a disc drive. Pick it: a disc image, a download, or Mech3.exe.";
    if (o_.screen == "notes" || o_.screen == "disc") screen_ = kNotes;
    if (o_.screen == "data") screen_ = kDataScreen;
    if (o_.screen == "disc") status_ = "No disc in a drive. Pick its image (.cue .iso) or any file of the disc.";
    if (o_.screen == "work") {
      screen_ = kWorking;
      work_ = std::make_unique<Work>();
      work_->title = "INSTALLING MECHWARRIOR 3";
      work_->progress = {"Copying the movies", 0.62, "VIDEO\\C1M3.AVI"};
    }
    layout();
    if (!buttons_.empty()) hot_ = 0, hot_t_ = 0.4f;
    scroll_ = 120.0f;
    const Uint64 td = SDL_GetTicksNS();
    draw(0.0f);
    for (int i = 0; i < 6; ++i) draw(1.0f / 30.0f);  // 0.2 s on: the hovered entry's ripple spreading
    if (hot_ >= 0)
      std::printf("mw3: launcher: hover %s:%s%s\n", buttons_[hot_].label.c_str(), drew_ripple_ ? " ripple" : "", drew_edge_ ? " edge" : "");
    std::printf("mw3: launcher: first frame drawn %.0f ms after the start (its draw %.1f ms)\n", ms(t0, SDL_GetTicksNS()),
                ms(td, SDL_GetTicksNS()));
    SDL_SaveBMP(canvas_, o_.shot.c_str());
    work_.reset();
    return {};
  }
  // A game without its files opens the find screen only when it is chosen;
  // a plain mw3 start that found none shows that screen alone.
  if (o_.setup_only) screen_ = kSetupScreen;
  else if (install_.ok && first_start) screen_ = kNotes;
  layout();
  // Not high pixel density: the game takes this window (one window into the
  // game), and its sizes are the window's points (SdlWindow, SdlInput).
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
  if (o_.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
  if (o_.hidden) flags |= SDL_WINDOW_HIDDEN;
  win_ = SDL_CreateWindow("MechWarrior 3", o_.win_w, o_.win_h, flags);
  if (!win_ || !(ren_ = SDL_CreateRenderer(win_, nullptr))) {
    std::fprintf(stderr, "mw3: launcher: no window (%s): straight to the game\n", SDL_GetError());
    return install_.ok ? LauncherChoice{LauncherChoice::kPlay, install_.dir} : LauncherChoice{};
  }
  SDL_SetRenderLogicalPresentation(ren_, W, H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  // Paced by the display (Owen: "pretty slow"): a fixed SDL_Delay(15) was
  // 16 to 31 ms on Windows' 15.6 ms timer, and no vsync let frames tear.
  // MW3_LAUNCHER_NOVSYNC (checks): the timed sleep instead, to compare.
  vsync_ = !SDL_getenv("MW3_LAUNCHER_NOVSYNC") && SDL_SetRenderVSync(ren_, 1);
  tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);
  SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_LINEAR);
  Uint64 last = SDL_GetTicks();
  SDL_InitSubSystem(SDL_INIT_GAMEPAD);  // a pad steps through the plates too (nav)
  if (o_.play) activate(kMw3);
  // Checks: keys or a pad as a list ("next,next,press"), each step logged.
  if (const char* steps = SDL_getenv("MW3_LAUNCHER_NAV")) {
    std::string all = steps;
    for (size_t at = 0; at <= all.size();) {
      const size_t comma = std::min(all.find(',', at), all.size());
      const std::string s = all.substr(at, comma - at);
      nav(s == "next" ? 1 : s == "prev" ? -1 : s == "press" ? 2 : s == "back" ? 3 : 0);
      const int f = focus_;
      if (f >= 0 && f < static_cast<int>(buttons_.size()) && s != "press" && s != "back")
        std::printf("mw3: launcher: focus %s\n", buttons_[f].label.c_str());
      at = comma + 1;
    }
  }
  // Checks: a pick as if made in the find screen's dialog (after any NAV
  // steps: GAME FILES), INSTALL and the notes' OK pressed.
  if (const char* to = SDL_getenv("MW3_LAUNCHER_MOVE_TO")) {  // checks: the data folder's move, its MOVE pressed
    from_files_ = true;
    picked(kDataPick, to);
    if (screen_ == kConfirm && confirm_go_ == 2) activate(kMoveGo);
  }
  if (const char* pick = SDL_getenv("MW3_LAUNCHER_PICK"); pick && (screen_ == kSetupScreen || !install_.ok))
    auto_go_ = auto_ok_ = true, look_at(pick);
  if (o_.play_mw1) activate(kMw1), done_ = !asking_;
  while (!done_) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      SDL_ConvertEventToRenderCoordinates(ren_, &e);
      if (e.type == SDL_EVENT_QUIT) done_ = true;
      if (asking_ && !ask_shown_ && e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {  // the panel up (a sheet takes the focus)
        ask_shown_ = true;
        std::printf("mw3: launcher: the picker took the focus %.0f ms after it was asked for\n",
                    static_cast<double>(SDL_GetTicksNS() - ask_at_) / 1e6);
      }
      if (game_) continue;  // the game is starting or running
      if (asking_ || screen_ == kWorking) {
        if (screen_ == kWorking && e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
          for (const Button& b : buttons_)
            if (e.button.x >= b.r.x && e.button.x < b.r.x + b.r.w && e.button.y >= b.r.y && e.button.y < b.r.y + b.r.h) activate(b.id);
        continue;
      }
      if (e.type == SDL_EVENT_MOUSE_MOTION) {
        int h = -1;
        for (size_t i = 0; i < buttons_.size(); ++i) {
          const SDL_Rect& r = buttons_[i].r;
          if (e.motion.x >= r.x && e.motion.x < r.x + r.w && e.motion.y >= r.y && e.motion.y < r.y + r.h) h = static_cast<int>(i);
        }
        if (h != hot_) {
          hot_ = h, hot_t_ = 0, focus_ = -1;
          if (h >= 0 && buttons_[h].enabled) sounds_.play(Sounds::kRollover);
        }
      } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        if (hot_ >= 0 && buttons_[hot_].enabled) activate(buttons_[hot_].id);
      } else if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
        SDL_OpenGamepad(e.gdevice.which);  // closed with the subsystem
      } else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        // Keys and a pad alike: the arrows (and Tab) or the d-pad step through
        // the plates, Enter or A presses, Escape or B goes back.
        enum { kNone, kNext, kPrev, kPress, kEscape } a = kNone;
        if (e.type == SDL_EVENT_KEY_DOWN) {
          const SDL_Keycode k = e.key.key;
          if (k == SDLK_DOWN || k == SDLK_RIGHT || k == SDLK_TAB) a = kNext;
          else if (k == SDLK_UP || k == SDLK_LEFT) a = kPrev;
          else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) a = kPress;
          else if (k == SDLK_ESCAPE) a = kEscape;
        } else {
          const int b = e.gbutton.button;
          if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN || b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) a = kNext;
          else if (b == SDL_GAMEPAD_BUTTON_DPAD_UP || b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) a = kPrev;
          else if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START) a = kPress;
          else if (b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_BACK) a = kEscape;
        }
        nav(a == kNext ? 1 : a == kPrev ? -1 : a == kPress ? 2 : a == kEscape ? 3 : 0);
      }
    }
    if (asking_) {
      int what = 0;
      std::string path;
      bool answered = false, cancelled = false;
      {
        std::lock_guard<std::mutex> g(pick_.m);
        answered = pick_.answered, cancelled = pick_.cancelled, what = pick_.what, path = pick_.path;
      }
      if (answered) {
        asking_ = false;
        std::printf("mw3: launcher: the picker answered (%s) %.0f ms after it was asked for\n", cancelled ? "cancelled" : "a pick",
                    static_cast<double>(SDL_GetTicksNS() - ask_at_) / 1e6);
        if (!cancelled) picked(what, path);
      } else {
        // While the panel is up the launcher stops drawing and waits in the
        // run loop: on the Mac the panel is another process answering through
        // the main run loop, which a vsync frame loop served once a frame
        // (Owen: the picker took a long time to open).
        SDL_WaitEventTimeout(nullptr, 50);
        continue;
      }
    }
    if (work_ && work_->done) finish_work();
    watch_game();
    const Uint64 now = SDL_GetTicks();
    const float dt = std::min(0.1f, static_cast<float>(now - last) / 1000.0f);
    last = now;
    const Uint64 ta = SDL_GetTicksNS();
    draw(dt);
    const Uint64 tb = SDL_GetTicksNS();
    present();
    const Uint64 tc = SDL_GetTicksNS();
    if (!frames_) std::printf("mw3: launcher: first frame shown %.0f ms after the start\n", ms(t0, tc));
    ++frames_, draw_ms_ += ms(ta, tb), present_ms_ += ms(tb, tc);
    if (quit_after_ > 0 && frames_ >= quit_after_) done_ = true;  // MW3_LAUNCHER_FRAMES (checks)
    if (!vsync_) {  // no vsync: the rest of a 60th of a second, to the nanosecond
      const Uint64 spent = SDL_GetTicksNS() - ta, frame = 1000000000ull / 60;
      if (spent < frame) SDL_DelayPrecise(frame - spent);
    }
  }
  if (frames_) {
    const double secs = ms(t0, SDL_GetTicksNS()) / 1000.0;
    std::printf("mw3: launcher: %d frames, %.1f a second; the draw %.2f ms, the present %.2f ms a frame (%s)\n", frames_,
                frames_ / std::max(secs, 0.001), draw_ms_ / frames_, present_ms_ / frames_, ren_ ? SDL_GetRendererName(ren_) : "-");
    std::printf("mw3: launcher: paced by %s\n", vsync_ ? "vsync" : "a timed sleep to 60 a second");
  }
  if (work_) work_->cancel = true;
  if (game_) {  // quit while the game runs: it goes on, we stop watching
    SDL_DestroyProcess(game_);
    game_ = nullptr;
  }
  if (choice_.kind == LauncherChoice::kPlay) {
    save_install(o_.write_dir, choice_.install);
    // The window goes on to the game, its renderer freed first (a window
    // drawn by an SDL renderer cannot be the GPU device's too).
    if (tex_) SDL_DestroyTexture(tex_), tex_ = nullptr;
    if (ren_) SDL_DestroyRenderer(ren_), ren_ = nullptr;
    // Its window's events so far are the launcher's: a focus lost to another
    // app would reach the game as a deactivation and leave it waiting.
    SDL_FlushEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
    choice_.window = win_;
    win_ = nullptr;
    if (choice_.window) std::printf("mw3: launcher: its window goes on to the game\n");
  }
  return choice_;
}

}  // namespace

LauncherChoice run_launcher(const LauncherOptions& o) {
  Launcher l(o);
  return l.run();
}

}  // namespace sdlfe
