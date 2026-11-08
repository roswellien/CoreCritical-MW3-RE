#include "gamez/zsave/zsave.h"

#include <cstdlib>
#include <cstring>
#include <vector>

// zSave, docs/save.md.
namespace zsave {

namespace {
Manager* g_manager = nullptr;          // [0x00736580]
std::vector<std::FILE*> g_temps;       // what _rmtmp closes: every tmpfile() still open
const char kHeader[] = "zSaveHeader";  // 0x005f4b94

uint32_t file_size(std::FILE* f) {
  const long here = std::ftell(f);
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, here, SEEK_SET);
  return size < 0 ? 0 : static_cast<uint32_t>(size);
}

// strtok's state, as the CRT keeps it between the two calls in the load.
struct Tokens {
  char* next = nullptr;
  char* take(char* s, const char* delims) {
    char* p = s ? s : next;
    if (!p) return nullptr;
    while (*p && std::strchr(delims, *p)) ++p;
    if (!*p) {
      next = p;
      return nullptr;
    }
    char* start = p;
    while (*p && !std::strchr(delims, *p)) ++p;
    if (*p) *p++ = 0;
    next = p;
    return start;
  }
};
}  // namespace

Manager* manager() { return g_manager; }

// Mech3.exe 1.2 @ 0x005a8b30
int32_t init() {
  auto* m = new Manager;
  zutl_zar::init(m->archive);
  g_manager = m;
  return 0;
}

// Mech3.exe 1.2 @ 0x005a8bb0, with the destructor 0x005a8be0
void shutdown() {
  Manager* m = g_manager;
  if (!m) return;
  delete[] m->buffer;
  m->buffer = nullptr;
  zutl_zar::destroy(m->archive);
  delete m;
  g_manager = nullptr;
}

// Mech3.exe 1.2 @ 0x005a8a20, the list's insert 0x005a8cb0
void add(const char* name, SaveFn save, LoadFn load, int32_t priority, void* user) {
  Manager* m = g_manager;
  if (!m) return;
  for (const Registrant& r : m->list)
    if (std::strcmp(name, r.name) == 0) return;
  m->list.push_back({name, save, load, priority, user});
}

// Mech3.exe 1.2 @ 0x005a8a50, 0x005a9140: "%s/%s" into 80 bytes.
void write(Context& ctx, const char* sub, const void* data, uint32_t size) {
  char name[0x50] = {};
  std::snprintf(name, sizeof name, "%s/%s", ctx.registrant->name, sub);
  zutl_zar::add(ctx.manager->archive, name, data, size);
}

// Mech3.exe 1.2 @ 0x005a8a70, 0x005a8da0
bool save(const char* path, int32_t kind) {
  Manager* m = g_manager;
  if (!m || !zutl_zar::create(m->archive, path)) return false;
  const int32_t header[2] = {8, kind};
  zutl_zar::add(m->archive, kHeader, header, 8);
  // 0x005a92e0: std::list::sort, a stable merge by priority (0x005a8c90).
  m->list.sort([](const Registrant& a, const Registrant& b) { return a.priority < b.priority; });
  struct ScopeArchive {
    Manager* mgr;
    bool committed = false;
    ~ScopeArchive() {
      if (mgr && !committed) {
        mgr->archive.dirty = false;
        zutl_zar::close(mgr->archive);
      }
    }
  } scope{m};
  for (Registrant& r : m->list) {
    Context ctx{m, &r};
    if (r.save) r.save(ctx, r.user);  // 0x005a91b0
  }
  scope.committed = true;
  zutl_zar::close(m->archive);
  return true;
}

// Mech3.exe 1.2 @ 0x005a90d0: the header entry read on the manager's archive.
int32_t kind_of(const char* path) {
  Manager* m = g_manager;
  if (!m || !zutl_zar::open(m->archive, path)) return -1;
  int32_t header[2] = {};
  uint32_t size = 8;
  int32_t kind = -1;
  if (zutl_zar::read(m->archive, zutl_zar::find(m->archive, kHeader), header, &size) == 0 && size == 8 &&
      header[0] == 8)
    kind = header[1];
  zutl_zar::close(m->archive);
  return kind;
}

// Mech3.exe 1.2 @ 0x005a8a90, 0x005a8e40
bool load(const char* path, int32_t kind) {
  Manager* m = g_manager;
  if (!m) return false;
  if (kind != -1 && kind_of(path) != kind) return false;
  zutl_zar::Archive& a = m->archive;
  if (!zutl_zar::open(a, path)) return false;
  m->stop = 0;
  for (int32_t i = 0; i < a.count; ++i) {
    // The name copied 80 bytes from the entry (its name is 64: the flags and
    // comment after it are read when it fills them).
    char name[0x51] = {};
    std::strncpy(name, reinterpret_cast<const char*>(a.entries[i].name), 0x50);
    if (std::strcmp(name, kHeader) == 0) continue;
    Tokens t;
    const char* owner = t.take(name, "/");
    const char* sub = owner ? t.take(nullptr, " ") : nullptr;
    // FIX (bug): a name without "/<sub>" (or empty) gave strncpy a null and
    // crashed the exe; such an entry is skipped (docs/save.md, fixes.md).
    if (!owner || !sub) continue;
    char who[0x51] = {}, what[0x51] = {};
    std::strncpy(who, owner, 0x50);
    std::strncpy(what, sub, 0x50);
    Registrant* r = nullptr;
    for (Registrant& x : m->list)
      if (std::strcmp(who, x.name) == 0) {
        r = &x;
        break;
      }
    if (!r) continue;
    uint32_t size = 0;
    zutl_zar::read(a, i, nullptr, &size);
    // FIX (bug): an entry longer than the whole file (a damaged save) had the
    // exe allocate that length and crash when it could not; it is skipped.
    if (size > file_size(a.file)) continue;
    if (m->capacity < size) {
      delete[] m->buffer;
      m->buffer = new uint8_t[size]();  // a short read's rest: zeros (the exe's heap leftovers)
      m->capacity = size;
    }
    uint32_t got = size;
    if (zutl_zar::read(a, i, m->buffer, &got) == 0 && r->load) {
      Context ctx{m, r};
      r->load(ctx, what, m->buffer, got, r->user);  // 0x005a91d0
    }
    if (m->stop) break;
  }
  zutl_zar::close(a);
  return true;
}

// Mech3.exe 1.2 @ 0x005a8ab0
std::FILE* temp() {
  if (!g_manager) return nullptr;
  std::FILE* f = std::tmpfile();
  if (f) g_temps.push_back(f);
  return f;
}

// Mech3.exe 1.2 @ 0x005a8b10, and the _rmtmp of 0x005a9210
void temp_done() {
  if (!g_manager) return;
  for (std::FILE* f : g_temps) std::fclose(f);
  g_temps.clear();
}

// Mech3.exe 1.2 @ 0x005a8ad0, 0x005a9210
void write_temp(std::FILE* f, Context& ctx, const char* sub) {
  if (!g_manager) return;
  std::fflush(f);
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::rewind(f);
  if (void* data = std::malloc(size > 0 ? static_cast<size_t>(size) : 1)) {
    std::fread(data, static_cast<size_t>(size), 1, f);
    write(ctx, sub, data, static_cast<uint32_t>(size));
    std::free(data);
  }
  for (std::FILE* t : g_temps) std::fclose(t);
  g_temps.clear();
}

// Mech3.exe 1.2 @ 0x005a8af0, 0x005a9290
std::FILE* temp_from(const void* data, uint32_t size) {
  if (!g_manager) return nullptr;
  std::FILE* f = std::tmpfile();
  if (!f) return nullptr;
  g_temps.push_back(f);
  std::fwrite(data, size, 1, f);
  std::fflush(f);
  std::rewind(f);
  return f;
}

}  // namespace zsave
