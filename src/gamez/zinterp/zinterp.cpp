// The interpreter object, its reader, macros, variables, the interp.zbd
// archive and the ScrollAlways list: Mech3.exe 0x594dc0..0x596e04, the
// functions after zinterp_parse.cpp's dispatcher. The exe names no file here;
// this one is named for the module (docs/interp.md).
#include "gamez/zinterp/zinterp.h"
#include "gamez/zmodel/gmod_init.h"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "crt/msvc_string.h"
#include "gamez/zclass/camera.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/cls_zbd.h"
#include "gamez/zclass/display.h"
#include "gamez/zclass/light.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_hit.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zrender/zrndr_flare.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/content_base.h"
#include "gamez/zvideo/zvid_format.h"

namespace zint {

std::map<std::string, uint32_t>& null_calls() {
  static std::map<std::string, uint32_t> m;
  return m;
}

namespace {
Globals g_globals;
ScrollList g_scroll;
std::list<std::string> g_clutter;
Observer g_observer{};

// 0x00730c20, cleared as 0x100 dwords per line. FIX (bug): the exe writes a
// text line into it with no bound (a line over 1 KB runs into the expansion
// buffer after it); here it grows.
std::vector<char> g_line(0x400);
// 0x00731020: every expanded argument is built here, so a command's second
// expanded argument overwrites its first (docs/interp.md, quirks). FIX (bug):
// the exe has no bound; this one stops at its end.
char g_expand[0x1000];

// FIX (A): Windows' DeleteFileA and _mkdir on the write dir.
int delete_file(const char* path) {
  std::error_code ec;
  return std::filesystem::remove(zutl::write_path(path), ec) ? 1 : 0;
}
int make_dir(const char* path) {
  std::error_code ec;
  const std::filesystem::path p = zutl::write_path(path);
  if (std::filesystem::exists(p, ec)) return EEXIST;
  return std::filesystem::create_directory(p, ec) ? 0 : ENOENT;
}
void print(const char* text) { std::fputs(text, stdout); }

Engine make_engine() {
  Engine e{};
  e.find_node = zcls::list_find_by_name;
  e.find_in_tree = zcls::node_find_in_tree;
  e.list_count = zcls::list_count;
  e.node_alloc = zcls::node_alloc;
  e.set_name = zcls::node_set_name;
  e.set_active = zcls::node_set_active;
  e.add_child = zcls::node_add_child;
  e.set_action = zcls::node_set_action;
  e.set_action_priority = zcls::node_set_priority;
  e.write_prepare_a = zcls::frame_update;
  e.write_prepare_b = [] { zcls::purge_lists(); };
  e.read_zbd = zcls::read_zbd;
  e.camera_set_active = zcls::node_set_active;  // 0x004f3420 is a jmp to it
  e.camera_set_clip = zcls::camera_set_clip;
  e.lod_add_child = zcls::node_link;            // 0x004fe0a0 is a jmp to it
  e.display_set_clear_color = zcls::display_set_clear_color;
  e.light_create = zcls::light_create;
  e.object3d_create = zcls::object3d_create;
  e.model_set_priority = zmod::model_set_priority;  // Object3DSetPriority's
  // The support scripts' render settings (item 7's census: every mission).
  e.bfe_tolerance = zmod::set_bfe_tolerance;
  e.get_bfe_tolerance = zmod::bfe_tolerance;
  e.coplanar_tolerance = zmod::set_coplanar_tolerance;
  e.colinear_tolerance = zmod::set_colinear_tolerance;
  e.inverse_z_tolerance = zmod::set_inverse_z_tolerance;
  e.vertex_shading = zmod::set_vertex_shading;
  e.video_dither = zvid::set_dither;
  e.material_array_size = [](int) { return 0; };  // 0x00536a60 is empty
  // No effect in the port, so done here as nothing (docs/interp.md, The live calls):
  e.model3d_array_size = [](int) { return 0; };          // 0x005269a0: gamez.zbd sets the pool's size
  e.perspective_delta_x = [](int) { return 0; };         // 0x005580d0: the software span's (not ported)
  e.perspective_far_z = [](float) { return 0; };         // 0x00558140: the same
  e.perspective_inverse_z = [](float) { return 0; };     // 0x00558170: the same
  e.texture_directory = [](const char*) { return 0; };   // 0x00516f50: loose textures (the archives are read)
  e.rdr_set_path = [](const char*) { return 0; };        // 0x005561a0: loose .zrd text (the same)
  e.rdr_add_path = [](const char*) { return 0; };        // 0x005561e0
  e.object3d_add_child = zcls::object3d_add_child;
  e.world_set_fog_type = zcls::world_set_fog_type;
  e.world_set_fog_color = zcls::world_set_fog_color;
  e.world_get_fog_color = zcls::world_get_fog_color;
  e.world_set_fog_altitude = zcls::world_set_fog_altitude;
  e.world_set_fog_density = zcls::world_set_fog_density;
  e.world_set_fog_range = zcls::world_set_fog_range;
  e.world_get_fog_range = zcls::world_get_fog_range;
  e.find_texture = zimg::find_texture;
  // 0x004eb420: the node's model (+0x3c); a null node reports and gives 5.
  // FIX (bug): a null out pointer (scroll_off's) is skipped, not written.
  e.get_model = [](Node* n, Model** out) {
    if (!n) return 5;
    if (out) *out = n->model;
    return 0;
  };
  e.texture_add = zimg::add_texture;
  e.lens_flare_texture = [](int i, zimg::TextureEntry* t) { return zrndr::flare_texture(i, t); };  // 0x005676d0
  e.cycle_set_on = zmod::cycle_set_on;
  e.cycle_set_map = zmod::cycle_set_map;
  e.cycle_set_looping = [](Model* m, int on) { return zmod::cycle_set_looping(m, static_cast<uint32_t>(on)); };
  e.cycle_set_speed = zmod::cycle_set_speed;
  e.model_set_scroll = zmod::model_set_scroll;                              // 0x00526ae0
  e.model_scroll_tick = [](Model* m) { zmod::model_scroll_tick(m); };       // 0x00541ad0
  e.pack_rgb = zvid::pack_rgb;
  e.clouds_on = zmod::clouds_set_on;
  e.write_texture_type = [](int t) { return zmod::mark_set_type(t), 0; };         // 0x005426a0
  e.write_texture_map = [](zimg::TextureEntry* m) { return zmod::mark_set_map(m), 0; };  // 0x005426b0
  e.clouds_period = zmod::clouds_set_period;
  e.clouds_wavelength = zmod::clouds_set_wavelength;
  e.clouds_level = zmod::clouds_set_level;
  e.clouds_heading = zmod::clouds_set_heading;
  e.anim_set_zbd_path = zeff::anim_set_zbd_path;
  e.load_soils = zmod::load_soils;
  e.delete_file = delete_file;
  e.make_dir = make_dir;
  e.print = print;
  return e;
}
Engine g_engine = make_engine();

// A null engine entry is a part not ported yet: nothing happens and it gives 0.
template <typename R, typename... P, typename... A>
R call(R (*fn)(P...), A... a) {
  if (fn) return fn(a...);
  return R();
}

void printf_engine(const char* fmt, ...) {
  char buf[0x800];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  call(g_engine.print, static_cast<const char*>(buf));
}

// The base class's pre/post hooks (0x005912f0 in both slots) only count a
// miss; its error hook (0x0044f4e0) does nothing.
bool base_pre(Interp& in, const char*) {
  miss(in);
  return true;
}
bool base_on_error(Interp&, const char*) { return false; }
const Vtable kBaseVtable = {base_pre, base_pre, base_on_error};

void vlog(Interp& in, const char* fmt, va_list ap) {
  if (in.log_fn) in.log_fn(fmt, ap);
}

// ScriptFile I/O. FIX (A): see ScriptFile.
int file_getc(ScriptFile& sf) {
  if (sf.eof) return EOF;  // after Ctrl-Z MSVCRT's reads stay at the end
  int c = std::fgetc(sf.f);
  if (c == EOF) {
    (std::ferror(sf.f) ? sf.err : sf.eof) = true;
    return EOF;
  }
  if (!sf.text) return c;
  if (c == 0x1a) {
    sf.eof = true;
    return EOF;
  }
  if (c == '\r') {
    const int d = std::fgetc(sf.f);
    if (d == '\n') return '\n';
    if (d != EOF) std::ungetc(d, sf.f);
  }
  return c;
}

ScriptFile* open_loose(const char* name) {
  mech3::validate_content_script_source(name);
  std::FILE* f = zutl::open(name, "rb");
  if (!f) return nullptr;
  auto* sf = new ScriptFile;
  sf->f = f;
  sf->text = true;
  return sf;
}

// FIX (A): _stat(name) relative to the working directory becomes the file's
// mtime in the mounts (zutl_path.h); 0 when it exists.
int stat_mtime(const char* name, int32_t* mtime) {
  const std::string p = zutl::find(name);
  struct stat st;
  if (p.empty() || ::stat(p.c_str(), &st) != 0) return -1;
  *mtime = static_cast<int32_t>(st.st_mtime);
  return 0;
}

// 0x005763b0 with the search path from 0x00576100: the name's file part in
// each directory of the path, else the name as given. FIX (A): through the
// mounts; the engine-wide default path (0x0072eebc) is not consulted.
std::FILE* open_on_path(const std::string& dirs, const char* name, const char* mode, std::string* found = nullptr) {
  char fname[256], ext[256];
  crt::msvc_splitpath_name(name, fname, ext);
  size_t start = 0;
  while (start <= dirs.size()) {
    size_t end = dirs.find(';', start);
    if (end == std::string::npos) end = dirs.size();
    const std::string dir = dirs.substr(start, end - start);
    if (!dir.empty()) {
      std::string path = dir;
      if (path.back() != '\\') path += '\\';
      path += fname;
      path += ext;
      if (zutl::exists(path.c_str())) {
        if (found) *found = zutl::find(path.c_str());
        return zutl::open(path.c_str(), mode);
      }
    }
    start = end + 1;
  }
  if (found) *found = zutl::find(name);
  return zutl::open(name, mode);
}

void includes_push(Interp& in, ScriptFile* f, long pos, uint8_t binary) {  // 0x00595da0
  in.includes.push_back({f, pos, binary, f->end, f->what});
}

const Include* includes_pop(Interp& in, Include* out) {  // 0x00595e20
  if (in.includes.empty()) return nullptr;
  *out = in.includes.back();
  in.includes.pop_back();
  return out;
}

void log_matrix(Interp& in, const char* name, const float* m) {  // 0x00594dc0
  log(in, "%s = [ %.1f %.1f %.1f ][ %.1f %.1f %.1f ][ %.1f %.1f %.1f ][ %.1f %.1f %.1f ]", name, m[0], m[1], m[2],
      m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
}
void log_vector(Interp& in, const char* name, const float* v) {  // 0x00594e50
  log(in, "%s = %.1f %.1f %.1f", name, v[0], v[1], v[2]);
}
void log_bool(Interp& in, const char* name, bool b) {  // 0x00594e90
  log(in, "%s = %s", name, b ? "true" : "false");
}
// 0x00594ec0. The finished line is passed to the log as its format.
void log_zone(Interp& in, const char* name, const uint8_t* zone) {
  char line[0x400];
  std::snprintf(line, sizeof line, "%s = [ ", name);
  for (int i = 0; i < (zone[0] < 3 ? zone[0] : 3); ++i) {
    char n[16];
    std::snprintf(n, sizeof n, "%d ", zone[1 + i]);
    std::strncat(line, n, sizeof line - std::strlen(line) - 1);
  }
  std::strncat(line, "]", sizeof line - std::strlen(line) - 1);
  log(in, line);
}

const char* name_or_none(Node* n) { return n ? n->name() : "none"; }

void camera_dump(Interp& in, zcls::Camera* c) {  // 0x00594f90
  log(in, "world = %s", name_or_none(c->nodes[0]));
  log(in, "window = %s", name_or_none(c->nodes[1]));
  log(in, "horizon = %s", name_or_none(c->nodes[2]));
  log(in, "horizonXZ = %s", name_or_none(c->nodes[3]));
  log_vector(in, "translate", c->floats(0x14));
  log_vector(in, "rotate", c->floats(0x20));
  log_vector(in, "world_translate", c->floats(0x2c));
  log_vector(in, "world_rotate", c->floats(0x38));
  log_matrix(in, "mtw_matrix", c->floats(0x44));
  log_matrix(in, "matrix", c->floats(0x80));
  log_vector(in, "view_vector", c->floats(0x74));
  log(in, "near_far = %.1f %.1f", c->get<float>(0xb0), c->get<float>(0xb4));
  log_vector(in, "near_clip_view_vector", c->floats(0xb8));
  log_vector(in, "far_clip_view_vector", c->floats(0xc4));
  log(in, "lod_multiplier = %.2f", c->get<float>(0xd0));
  log(in, "inverse_lod_multiplier_squared = %.2f", c->get<float>(0xd4));
  log(in, "zoom_factor: h=%.2f, v=%.2f", c->get<float>(0xd8), c->get<float>(0xdc));
  log(in, "base_fov: h=%.2f, v=%.2f", c->get<float>(0xe0), c->get<float>(0xe4));
  log(in, "h_fov: h=%.2f, v=%.2f", c->get<float>(0xe8), c->get<float>(0xec));
  log(in, "half_fov: h=%.2f v=%.2f", c->get<float>(0xf0), c->get<float>(0xf4));
  log(in, "stride = %d", c->get<int32_t>(0x1dc));
  log_bool(in, "zone_set", c->get<int32_t>(0x1e0) != 0);
  log_zone(in, "zone", c->raw + 0x1e4);
}
}  // namespace

Globals& globals() { return g_globals; }
Engine& engine() { return g_engine; }
Observer& observer() { return g_observer; }
const Vtable& base_vtable() { return kBaseVtable; }

// Mech3.exe 1.2 @ 0x005951e0
void node_dump(Interp& in, Node* node) {
  if (!node) {
    log(in, "no node to dump");
    return;
  }
  log(in, "node %s", node->name());
  if (node->cls() != 1) {
    log(in, "node not currently dumpable");
    return;
  }
  camera_dump(in, static_cast<zcls::Camera*>(node->data));
}

// Mech3.exe 1.2 @ 0x00595240
bool argv0_starts(Interp& in, const char* name, size_t n) {
  return std::strncmp(in.argc ? in.argv[0] : "", name, n) == 0;
}

// Mech3.exe 1.2 @ 0x00595270
bool argv0_is(Interp& in, const char* name) { return std::strcmp(in.argc ? in.argv[0] : "", name) == 0; }

// Mech3.exe 1.2 @ 0x004108f0. The exe keeps this copy in MW3's code; nothing
// sets the callback unless the debug console is open, so the shipped game
// logs nothing.
void log(Interp& in, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(in, fmt, ap);
  va_end(ap);
}

// Mech3.exe 1.2 @ 0x005952d0
void error(Interp& in, const char* fmt, ...) {
  in.error = 1;
  va_list ap;
  va_start(ap, fmt);
  vlog(in, fmt, ap);
  va_end(ap);
}

// Mech3.exe 1.2 @ 0x005952f0
void init(Interp& in, const char* archive, const char* search_path) {
  in.vt = &kBaseVtable;
  in.loose_opens = 0;
  in.build = new Build{};
  in.current = nullptr;
  in.skip_depth = 0;
  in.line = nullptr;
  in.binary = 0;
  in.argc = 0;
  in.log_fn = nullptr;
  in.console = 0;
  in.search_path = search_path;
  in.archive_name = archive;
  in.file = nullptr;
  in.includes.clear();
  in.archive = ScriptFile{};
  in.magic = in.version = 0;
  in.entry_count = 0;
  in.dir.clear();
  in.macros.clear();
  in.vars.clear();
}

// Mech3.exe 1.2 @ 0x00595440. The archive stays open, as in the exe.
void reset(Interp& in) {
  in.macros.clear();    // 0x00595b10
  in.vars.clear();      // 0x00595b60
  in.includes.clear();  // 0x00595e40
  in.loose_opens = 0;
}

// Mech3.exe 1.2 @ 0x005953d0
void destroy(Interp& in) {
  reset(in);
  std::free(in.line);
  in.line = nullptr;
  for (char* p : in.pending_free) std::free(p);
  in.pending_free.clear();
  delete in.build;
  in.build = nullptr;
  in.dir.clear();
}

// Mech3.exe 1.2 @ 0x005954a0. The last line runs before end-of-file stops
// the loop, so a final line without a newline still runs.
bool run(Interp& in, ScriptFile* f, bool binary) {
  if (!f) return false;
  in.binary = binary;
  in.file = f;
  bool ok, go;
  do {
    std::fill(g_line.begin(), g_line.end(), 0);
    ok = read_line(in, in.file, g_line.data());
    go = exec_line(in, g_line.data());
  } while (ok && go);
  in.binary = 0;
  return true;
}

// Mech3.exe 1.2 @ 0x00595510. Misses: the pre hook, the dispatcher and the
// post hook each add one for a word they do not know (the base post hook
// always does), so "BadCommand" is logged only when all three count one.
bool exec_line(Interp& in, char* line) {
  if (!line) return false;
  in.quit = 0;
  in.error = 0;
  in.misses = 0;
  if (!in.binary) tokenize(in, line);
  ++in.exec_depth;
  if (g_observer.line) g_observer.line(in);
  if (in.argc != 0) {
    const char* argv0 = in.argv[0];
    if (preprocess(in, argv0)) {
      in.cursor = 1;
      if (in.vt->pre(in, argv0)) {
        in.cursor = 1;
        if (g_observer.dispatched) g_observer.dispatched(in);
        dispatch(in, argv0);
        in.cursor = 1;
        in.vt->post(in, argv0);
        if (in.misses == 3) log(in, "BadCommand (%s): %s", argv0, line);
      }
    }
    if (in.error) in.vt->on_error(in, argv0);
  }
  // FIX (bug): after a `source` whose file ends in an unterminated line that
  // failed, the error hook reads argv[0] from that line, which the exe has
  // freed. Lines are freed here only when the outermost line ends.
  in.pending_free.push_back(in.line);
  in.line = nullptr;
  if (--in.exec_depth == 0) {
    for (char* p : in.pending_free) std::free(p);
    in.pending_free.clear();
  }
  const bool go_on = !in.quit;
  if (g_observer.line_done) g_observer.line_done(in, go_on);
  return go_on;
}

// Mech3.exe 1.2 @ 0x005955e0. Binary (archive) lines are {u32 size, u32
// argc, size bytes of NUL-terminated tokens}; size 0 ends the script. Text
// lines are read up to and including '\n', a NUL or the end of the file.
bool read_line(Interp& in, ScriptFile* f, char* line) {
  if (in.binary) {
    in.cursor = 1;
    uint32_t size = 0;
    // FIX (trap): a short read leaves the exe's size as the FILE pointer, and
    // a damaged size is malloc'd as it is; both end the script here.
    if (std::fread(&size, 4, 1, f->f) != 1 || size > 0x100000) size = 0;
    // Ours (docs/modding.md, M3): a line past the script's end ends it, said.
    if (size != 0 && f->end >= 0) {
      const long at = std::ftell(f->f);
      if (at < 0 || at + 4 > f->end || static_cast<long>(size) > f->end - at - 4) {
        std::printf("mw3: %s is broken (a line past its end), read up to it\n", f->what.c_str());
        size = 0;
        f->cut = true;
      }
    }
    if (size == 0) {
      in.argc = 0;
      in.line = nullptr;
      return false;
    }
    if (std::fread(&in.argc, 4, 1, f->f) != 1) in.argc = 0;
    in.line = static_cast<char*>(std::calloc(size + 1, 1));
    (void)std::fread(in.line, size, 1, f->f);
    // FIX (bug): the exe trusts argc; past 16 tokens it writes over the macro
    // table, and past the text it reads beyond the block.
    if (in.argc > 16) in.argc = 16;
    char* p = in.line;
    for (uint32_t i = 0; i < in.argc; ++i) {
      in.argv[i] = p;
      if (p < in.line + size) p += std::strlen(p) + 1;
    }
    return true;
  }
  int c = file_getc(*f);
  size_t n = 0;
  if (c != 0) {
    while (!f->eof) {
      if (n + 1 >= g_line.size()) {  // FIX (bug): grows, see g_line
        const size_t at = static_cast<size_t>(line - g_line.data());
        g_line.resize(g_line.size() * 2);
        line = g_line.data() + at;
      }
      line[n] = static_cast<char>(c);
      if (c == '\n') break;
      c = file_getc(*f);
      ++n;
      if (c == 0) break;
    }
  }
  return !(f->eof || f->err);
}

// Mech3.exe 1.2 @ 0x005956e0. Each %NAME% becomes the macro's value; an
// unknown name becomes nothing. A lone '%' stays.
char* expand(Interp& in, char* arg) {
  if (!arg) return nullptr;
  char* open = std::strchr(arg, '%');
  if (!open) return arg;
  char* close = std::strchr(open + 1, '%');
  if (!close) return arg;
  std::string out;
  do {
    out.append(arg, static_cast<size_t>(open - arg));
    // FIX (bug): the name went into a 64-byte stack buffer with no bound.
    const std::string name(open + 1, static_cast<size_t>(close - open - 1));
    if (const char* v = macro_find(in, name.c_str(), nullptr)) out += v;
    arg = close + 1;
    open = std::strchr(arg, '%');
  } while (open && (close = std::strchr(open + 1, '%')));
  if (*arg) out += arg;
  const size_t n = std::min(out.size(), sizeof g_expand - 1);
  std::memcpy(g_expand, out.data(), n);
  g_expand[n] = 0;
  return g_expand;
}

// Mech3.exe 1.2 @ 0x00595850. '#' starts a comment; tokens are split on
// ", \t\n", so a comma separates arguments as a space does, and a space
// before a comma leaves an empty token.
void tokenize(Interp& in, char* line) {
  if (in.binary) return;
  in.argc = 0;
  in.cursor = 1;
  if (const char* hash = std::strchr(line, '#')) {
    const size_t n = static_cast<size_t>(hash - line);
    in.line = static_cast<char*>(std::malloc(n + 1));
    std::memcpy(in.line, line, n);
    in.line[n] = 0;
  } else {
    in.line = static_cast<char*>(std::malloc(std::strlen(line) + 1));
    std::strcpy(in.line, line);
  }
  char* p = in.line;
  while (crt::msvc_iswspace_char(*p)) ++p;
  // FIX (bug): the exe has 16 argv slots and no bound; the 17th token would
  // overwrite the macro table pointer. Further tokens are dropped.
  for (char* q = std::strpbrk(p, ", \t\n"); q; q = std::strpbrk(p, ", \t\n")) {
    if (in.argc < 16) in.argv[in.argc++] = p;
    p = q + 1;
    const char c = *q;
    *q = 0;
    if (c == '\n') break;
    while (crt::msvc_iswspace_char(*p)) ++p;
  }
  if (*p && in.argc < 16) in.argv[in.argc++] = p;
}

// Mech3.exe 1.2 @ 0x00595990. Runs a script from the archive if it is there
// (and its loose copy, if any, is not newer), else the loose file. True if
// the file opened; errors inside it do not change that.
bool source(Interp& in, const char* name) {
  ScriptFile* f = nullptr;
  bool binary = false;
  if (in.file) includes_push(in, in.file, std::ftell(in.file->f), in.binary);
  if (g_globals.use_archive && open_archive(in, in.archive_name.c_str())) {
    if ((f = archive_find(in, name))) binary = true;
  }
  if (!f) {
    f = open_loose(name);
    ++in.loose_opens;
  }
  bool r = false;
  if (f) {
    r = run(in, f, binary);
    if (!binary) {
      std::fclose(f->f);
      delete f;
    }
  }
  Include top;
  if (includes_pop(in, &top)) {
    in.file = top.file;
    std::fseek(top.file->f, top.pos, SEEK_SET);
    top.file->end = top.end, top.file->what = top.what;  // ours (M3)
    in.binary = top.binary;
    return r;
  }
  in.file = nullptr;
  return r;
}

// Mech3.exe 1.2 @ 0x00595a90. Case-sensitive.
const char* macro_find(Interp& in, const char* name, Macro** out) {
  for (Macro& m : in.macros)
    if (m.name == name) {
      if (out) *out = &m;
      return m.value.c_str();
    }
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00595c60
bool macro_set(Interp& in, const char* name, const char* value) {
  if (!name || !value) return false;
  Macro* m = nullptr;
  if (!macro_find(in, name, &m)) {
    in.macros.push_back({name, value});
    return true;
  }
  m->value = value;
  return true;
}

// Mech3.exe 1.2 @ 0x00595bb0
void macro_set_true(Interp& in, const char* name) { macro_set(in, name, "true"); }
// Mech3.exe 1.2 @ 0x00595bd0
void macro_set_false(Interp& in, const char* name) { macro_set(in, name, "false"); }

// Mech3.exe 1.2 @ 0x00595bf0. Defined means the value is exactly "true".
bool macro_defined(Interp& in, const char* name) {
  const char* v = macro_find(in, name ? name : "", nullptr);
  return v && std::strcmp(v, "true") == 0;
}

// Mech3.exe 1.2 @ 0x00595d50. The tokens as read, not expanded.
void echo(Interp& in) {
  for (uint32_t i = 0; i < in.argc; ++i) printf_engine("%s ", in.argv[i]);
  printf_engine("\n");
}

// Mech3.exe 1.2 @ 0x00595e70. argv[0], the command, is never expanded.
char* next_arg(Interp& in) {
  const uint32_t i = in.cursor++;
  char* a = i < in.argc ? in.argv[i] : nullptr;
  if (!a) return nullptr;
  return expand(in, a);
}

// Mech3.exe 1.2 @ 0x00595ea0
bool arg_bool(Interp& in, bool def) {
  const char* s = next_arg(in);
  if (!s) return def;
  return crt::msvc_stricmp(s, "on") == 0 || crt::msvc_stricmp(s, "true") == 0;
}

// Mech3.exe 1.2 @ 0x00595ef0. atof's double, rounded by each caller.
double arg_float(Interp& in, float def) {
  const char* s = next_arg(in);
  return s ? crt::msvc_atof(s) : def;
}

// Mech3.exe 1.2 @ 0x00595f10
int32_t arg_int(Interp& in, int32_t def) {
  const char* s = next_arg(in);
  return s ? crt::msvc_atoi(s) : def;
}

// Mech3.exe 1.2 @ 0x00595f30
Variable* var_find(Interp& in, const char* name) {
  for (Variable& v : in.vars)
    if (v.name == name) return &v;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00595fa0. Registering a name again only moves its
// pointer; the type stays.
bool var_register(Interp& in, const char* name, int32_t type, void* value) {
  if (!name || !value) return false;
  if (Variable* v = var_find(in, name)) {
    v->value = value;
    return true;
  }
  in.vars.push_back({name, type, value});
  return true;
}

// Mech3.exe 1.2 @ 0x00596020. FIX (bug): for a string the exe passes the
// first character where the format wants the string; none is registered.
void var_print(Interp& in, const Variable* v) {
  if (!v) return;
  if (v->type == 0) log(in, "%s = (int) %d", v->name.c_str(), *static_cast<int32_t*>(v->value));
  else if (v->type == 1) log(in, "%s = (float) %f", v->name.c_str(), static_cast<double>(*static_cast<float*>(v->value)));
  else if (v->type == 2) log(in, "%s = (char*)\"%s\"", v->name.c_str(), static_cast<const char*>(v->value));
}

// Mech3.exe 1.2 @ 0x00596090
void miss(Interp& in) { ++in.misses; }

// Mech3.exe 1.2 @ 0x005962d0. A class of 0 skips the node check.
bool check_args(Interp& in, uint32_t count, int32_t cls, Node* node) {
  const char* argv0 = in.argc ? in.argv[0] : nullptr;
  if (cls != 0) {
    if (!node) {
      error(in, "Interp: keyword [%s] has NULL node to work with", argv0);
      return false;
    }
    if (node->cls() != cls) {
      error(in, "Interp: keyword [%s] has node [%s] of class=%d, expected=%d", argv0, node->name(), node->cls(), cls);
      return false;
    }
  }
  if (count + 1 > in.argc) {
    error(in, "Interp: keyword [%s] requires (%d) args, found (%d) args", argv0, count, in.argc - 1);
    return false;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00596620. Accepted once, then kept open. A loose copy of
// any script whose mtime differs from the archive's rejects the whole archive.
bool open_archive(Interp& in, const char* name) {
  if (in.archive.f) return true;
  std::string where;
  std::FILE* f = open_on_path(in.search_path, name, "rb", &where);
  in.archive = ScriptFile{};
  in.archive.f = f;
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);  // ours (M3): the scripts' bound
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  uint32_t head[2];
  int32_t count = 0;
  bool ok = std::fread(head, 8, 1, f) == 1 && head[0] == 0x08971119 && head[1] == 7 &&
            std::fread(&count, 4, 1, f) == 1;
  std::vector<ArchiveEntry> dir;
  if (ok) {
    // FIX (trap): a huge count makes the exe's realloc fail; the same here
    // without trying.
    ok = count >= 0 && count < 0x100000;
    if (ok && count > 0) {
      dir.resize(static_cast<size_t>(count));
      ok = std::fread(dir.data(), 0x80, static_cast<size_t>(count), f) == static_cast<size_t>(count);
    }
  }
  if (ok) {
    for (int32_t i = 0; i < count && ok; ++i) {
      int32_t mtime;
      if (stat_mtime(dir[i].name, &mtime) == 0 && dir[i].timestamp != mtime) ok = false;
    }
    if (ok) {
      for (ArchiveEntry& e : dir) e.name[sizeof e.name - 1] = 0;  // ours (M3): _stricmp stays in the name
      in.magic = head[0];
      in.version = head[1];
      in.entry_count = count;
      in.dir = std::move(dir);
      in.archive_size = size;
      in.archive_where = where.empty() ? name : where;
      return true;
    }
  }
  std::fclose(f);
  in.archive.f = nullptr;
  return false;
}

// Mech3.exe 1.2 @ 0x00596810. The name ignores case. A loose copy newer than
// the archived one wins (difftime > 0).
ScriptFile* archive_find(Interp& in, const char* name) {
  if (!in.archive.f) return nullptr;
  int32_t idx = -1;
  for (int32_t i = 0; i < in.entry_count; ++i)
    if (crt::msvc_stricmp(in.dir[i].name, name) == 0) {
      idx = i;
      break;
    }
  if (idx == -1) return nullptr;
  // An explicitly selected managed gameplay file is an override regardless of
  // installation/copy timestamps. Unclaimed stock files keep recovered rules.
  if (mech3::content_managed_script_source(name)) return nullptr;
  int32_t mtime;
  if (stat_mtime(name, &mtime) == 0 && static_cast<double>(mtime) - in.dir[idx].timestamp > 0) return nullptr;
  // Ours (docs/modding.md, M3): a script starts past the directory and runs
  // to the next one's start or the file's end; one outside the file is not
  // the archive's (the exe seeks there and reads what it finds).
  const long start = in.dir[idx].offset, table = 12 + static_cast<long>(in.entry_count) * 0x80;
  if (in.archive_size >= 0 && (start < table || start >= in.archive_size)) {
    std::printf("mw3: %s: script %s is broken (its offset past the file), not read\n", in.archive_where.c_str(), in.dir[idx].name);
    return nullptr;
  }
  long end = in.archive_size;
  for (int32_t i = 0; i < in.entry_count; ++i)
    if (in.dir[i].offset > start && in.dir[i].offset < end) end = in.dir[i].offset;
  if (std::fseek(in.archive.f, in.dir[idx].offset, SEEK_SET) != 0) return nullptr;
  in.archive.eof = in.archive.err = false;
  in.archive.end = end;
  in.archive.cut = false;
  in.archive.what = in.archive_where + ": script " + in.dir[idx].name;
  return &in.archive;
}

ScrollList& scroll_list() { return g_scroll; }

// Mech3.exe 1.2 @ 0x005962a0
void scroll_list_init() {
  g_scroll.nodes.clear();
  g_scroll.holder = nullptr;
}

// Mech3.exe 1.2 @ 0x00596370. The exe asks for the node's model with a null
// out pointer, which crashes it for any real node (no shipped script turns a
// scroll off). Kept: the default node_get_model skips the write.
bool scroll_off(Node* node) {
  if (node) call(g_engine.get_model, node, static_cast<Model**>(nullptr));
  return false;
}

// Mech3.exe 1.2 @ 0x00596380, a node's action while its texture scrolls.
void scroll_tick(Node* node) {
  Model* m = nullptr;
  call(g_engine.get_model, node, &m);
  call(g_engine.model_scroll_tick, m);
}

// Mech3.exe 1.2 @ 0x005963a0 (0x005963b0 on the static list): the holder's
// action, ticking every listed node.
void scroll_always_tick(Node*) {
  for (Node* n : g_scroll.nodes) scroll_tick(n);
}

// Mech3.exe 1.2 @ 0x005963f0. The model is read straight from the node.
bool scroll_on(Node* node, float u, float v, bool always) {
  if (!node || !node->model) return false;
  call(g_engine.model_set_scroll, node->model, 1, u, v);
  if (always) {
    if (!g_scroll.holder && (g_scroll.holder = call(g_engine.object3d_create))) {
      call(g_engine.set_name, g_scroll.holder, "ScrollAlways");
      call(g_engine.set_action, g_scroll.holder, static_cast<Action>(scroll_always_tick));
    }
    g_scroll.nodes.push_back(node);
    return true;
  }
  call(g_engine.set_action, node, static_cast<Action>(scroll_tick));
  return true;
}

// Mech3.exe 1.2 @ 0x005964c0. Full: forget the nodes and free the holder.
// Otherwise each listed node's model scroll offset (+0x30) goes back to 0.
void scroll_list_clear(bool full) {
  if (full) {
    g_scroll.nodes.clear();
    if (g_scroll.holder && call(g_engine.pool_alive)) {
      call(g_engine.set_action, g_scroll.holder, static_cast<Action>(nullptr));
      call(g_engine.free_node, g_scroll.holder);
    }
    g_scroll.holder = nullptr;
    return;
  }
  for (Node* n : g_scroll.nodes)
    if (n && n->model) std::memset(n->model->header + 0x30, 0, 4);
}

// Mech3.exe 1.2 @ 0x00596570
void scroll_list_destroy() {
  scroll_list_clear(true);
  g_scroll.nodes.clear();
}

std::list<std::string>& clutter_templates() { return g_clutter; }

// Mech3.exe 1.2 @ 0x00596db0
void add_clutter_template(const char* name) { g_clutter.push_back(name); }

}  // namespace zint
