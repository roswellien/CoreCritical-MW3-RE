#include "gamez/zutil/zutl_path.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <utility>

namespace zutl {

namespace {
Mounts g_mounts;
std::shared_ptr<const BankOverrides> g_banks;
std::set<std::string> g_bank_names;
std::vector<std::string> g_bank_index_mounts;
BankOverrides g_bank_index;
std::string bank_path_key(const std::string &path) {
  auto key =
      std::filesystem::u8path(path).lexically_normal().generic_u8string();
  for (auto &c : key)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  return key;
}

std::FILE *open_host_file(const std::string &path, const char *mode) {
#if defined(_WIN32)
  std::wstring wide_mode;
  for (const char *c = mode; *c; ++c)
    wide_mode += static_cast<wchar_t>(*c);
  return _wfopen(std::filesystem::u8path(path).c_str(), wide_mode.c_str());
#else
  return std::fopen(path.c_str(), mode);
#endif
}

bool is_file(const std::string& p) {
  std::error_code ec;
  return std::filesystem::is_regular_file(std::filesystem::u8path(p), ec);
}

std::string join(const std::string& dir, const std::string& rel) {
  if (dir.empty()) return rel;
  return dir.back() == '/' ? dir + rel : dir + "/" + rel;
}
bool same_letters(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}
// A file in a mount, by any_case.
std::string in_mount(const std::string& dir, const std::string& rel) {
  std::string p = join(dir, rel);
  if (is_file(p)) return p;
  p = any_case(dir, rel);
  return !p.empty() && is_file(p) ? p : std::string{};
}
std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// FindFirstFileA's wildcards, without case.
bool match(const char* p, const char* s) {
  for (; *p; ++p, ++s) {
    if (*p == '*') {
      for (const char* t = s;; ++t) {
        if (match(p + 1, t)) return true;
        if (!*t) return false;
      }
    }
    if (!*s) return false;
    if (*p != '?' && std::toupper(static_cast<unsigned char>(*p)) != std::toupper(static_cast<unsigned char>(*s)))
      return false;
  }
  return !*s;
}

// Windows-1252's 0x80..0x9f (the rest of its upper half is Latin-1).
constexpr uint16_t kCp1252[32] = {0x20ac, 0x81,   0x201a, 0x192,  0x201e, 0x2026, 0x2020, 0x2021,
                                  0x2c6,  0x2030, 0x160,  0x2039, 0x152,  0x8d,   0x17d,  0x8f,
                                  0x90,   0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                  0x2dc,  0x2122, 0x161,  0x203a, 0x153,  0x9d,   0x17e,  0x178};

void put_utf8(std::string& out, uint32_t u) {
  if (u < 0x80) {
    out += static_cast<char>(u);
  } else if (u < 0x800) {
    out += static_cast<char>(0xc0 | u >> 6);
    out += static_cast<char>(0x80 | (u & 0x3f));
  } else {
    out += static_cast<char>(0xe0 | u >> 12);
    out += static_cast<char>(0x80 | ((u >> 6) & 0x3f));
    out += static_cast<char>(0x80 | (u & 0x3f));
  }
}

// A host name (UTF-8) in the game's code page; false for a character it lacks.
bool to_game(const std::string& s, std::string* out) {
  out->clear();
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    uint32_t u = c;
    size_t n = 1;
    if (c >= 0xe0 && c < 0xf0 && i + 2 < s.size()) {
      u = (c & 0x0fu) << 12 | (static_cast<unsigned char>(s[i + 1]) & 0x3fu) << 6 |
          (static_cast<unsigned char>(s[i + 2]) & 0x3fu);
      n = 3;
    } else if (c >= 0xc0 && c < 0xe0 && i + 1 < s.size()) {
      u = (c & 0x1fu) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3fu);
      n = 2;
    } else if (c >= 0x80) {
      return false;
    }
    i += n;
    if (u < 0x80 || (u >= 0xa0 && u < 0x100)) {
      *out += static_cast<char>(u);
      continue;
    }
    const uint16_t* hit = std::find(kCp1252, kCp1252 + 32, static_cast<uint16_t>(u));
    if (hit == kCp1252 + 32) return false;
    *out += static_cast<char>(0x80 + (hit - kCp1252));
  }
  return true;
}
// A host path that is absolute: "/..." and, on Windows, "C:/..." or "//server"
// (after host()'s '\\' -> '/'). Ours: a Windows drive path was taken for a
// game path and joined onto the write dir.
bool absolute(const std::string& h) {
  if (!h.empty() && h[0] == '/') return true;
  return h.size() >= 3 && std::isalpha(static_cast<unsigned char>(h[0])) && h[1] == ':' && h[2] == '/';
}
// Ours (Mounts::confined): a name that would reach outside the mounts.
bool outside(const std::string& h) {
  if (!g_mounts.confined) return false;
  bool out = !h.empty() && h[0] == '/';
  for (size_t i = 0; !out && i < h.size();) {
    const size_t n = std::min(h.find('/', i), h.size()) - i;
    out = h.compare(i, n, "..") == 0 || h.find(':', i) < i + n;
    i += n + 1;
  }
  static std::vector<std::string> said;
  if (out && std::find(said.begin(), said.end(), h) == said.end()) {
    said.push_back(h);
    std::fprintf(stderr, "mw3: %s is outside the game's folders, not opened\n", h.c_str());
  }
  return out;
}
}  // namespace

bool game_text(const std::string& utf8, std::string* out) { return to_game(utf8, out); }

std::string any_case(const std::string& root, const std::string& rel) {
  std::string at = root;
  std::error_code ec;
  // The root by its letters too: the game's own mounts are named ("<install>/zbd/c1").
  if (!at.empty() && !std::filesystem::exists(at, ec)) {
    const size_t slash = at.rfind('/');
    if (slash == std::string::npos || slash == 0) return {};
    at = any_case(at.substr(0, slash), at.substr(slash + 1));
    if (at.empty()) return {};
  }
  for (size_t i = 0; i < rel.size();) {
    const size_t end = std::min(rel.find('/', i), rel.size());
    const std::string part = rel.substr(i, end - i);
    i = end + 1;
    if (part.empty()) continue;
    std::string next = join(at, part);
    if (!std::filesystem::exists(next, ec)) {
      next.clear();
      for (std::filesystem::directory_iterator it(at.empty() ? "." : at, ec), last; !ec && it != last; it.increment(ec))
        if (same_letters(it->path().filename().string(), part)) {
          next = join(at, it->path().filename().string());
          break;
        }
      if (next.empty()) return {};
    }
    at = next;
  }
  return std::filesystem::exists(at, ec) ? at : std::string{};
}

Mounts& mounts() { return g_mounts; }

void mount(const std::string& dir) { g_mounts.read.push_back(dir); }
void mount_front(const std::string& dir) { g_mounts.read.insert(g_mounts.read.begin(), dir); }
void mount_mod(const std::string& dir) {
  g_mounts.read.push_back(dir);
  g_mounts.mods.push_back(dir);
}
bool is_mod_dir(const std::string& dir) {
  for (const std::string& m : g_mounts.mods)
    if (dir == m || (dir.size() > m.size() && dir.compare(0, m.size(), m) == 0 && dir[m.size()] == '/')) return true;
  return false;
}
bool mod_may_serve(const std::string& host_path) {
  std::string p;
  for (char c : host_path) p += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  while (p.rfind("./", 0) == 0) p.erase(0, 2);
  for (const char* dir : {"pilots/", "configs/", "keys/", "saves/", "patch12/", "mods/"})
    if (p.rfind(dir, 0) == 0) return false;
  for (const char* file : {"mw3.cfg", "settings.reg", "movie-shapes.txt"})
    if (p == file) return false;
  return p.size() < 4 || p.compare(p.size() - 4, 4, ".log") != 0;
}
std::string find_in_mods(const std::string& host_path) {
  if (host_path.empty() || absolute(host_path) || outside(host_path) || !mod_may_serve(host_path)) return {};
  for (const std::string& m : g_mounts.mods) {
    const std::string p = in_mount(m, host_path);
    if (!p.empty()) return p;
  }
  return {};
}
void set_write_dir(const std::string& dir) { g_mounts.write = dir; }

std::string host(const char* path) {
  std::string s = path ? path : "";
  for (char& c : s)
    if (c == '\\') c = '/';
  if (s.empty() || absolute(s)) return s;  // already the host's
  std::string out;
  for (char c : s) {
    const auto b = static_cast<unsigned char>(c);
    put_utf8(out, b >= 0x80 && b < 0xa0 ? kCp1252[b - 0x80] : b);
  }
  return out;
}

std::shared_ptr<const BankOverrides>
set_bank_overrides(std::shared_ptr<const BankOverrides> banks) {
  auto previous = std::move(g_banks);
  g_banks = std::move(banks);
  g_bank_names.clear();
  g_bank_index.clear();
  g_bank_index_mounts.clear();
  if (g_banks)
    for (const auto &bank : *g_banks)
      g_bank_names.insert(bank.first.substr(bank.first.find_last_of('/') + 1));
  return previous;
}

std::string find(const char* path) {
  const std::string h = host(path);
  if (h.empty() || outside(h)) return {};
  auto basename = h.substr(h.find_last_of('/') + 1);
  for (auto &c : basename)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  const bool selected_bank = g_banks && g_bank_names.count(basename);
  if (selected_bank) {
    if (g_bank_index_mounts != g_mounts.read) {
      BankOverrides index;
      for (const auto &root : g_mounts.read)
        for (const auto &bank : *g_banks)
          index.emplace(bank_path_key(join(root, bank.first)), bank.second);
      g_bank_index = std::move(index);
      g_bank_index_mounts = g_mounts.read;
    }
    auto alias = h;
    for (auto &c : alias)
      if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    const auto bank = g_banks->find(alias);
    if (bank != g_banks->end()) {
      if (!is_file(bank->second))
        throw std::runtime_error("Selected environment bank disappeared: " +
                                 alias);
      return bank->second;
    }
  }

  if (absolute(h) || (g_mounts.read.empty() && g_mounts.write.empty())) return is_file(h) ? h : std::string{};
  if (!g_mounts.write.empty() && is_file(join(g_mounts.write, h))) return join(g_mounts.write, h);  // what the game wrote
  for (const std::string& dir : g_mounts.read) {
    if (!g_mounts.mods.empty() && is_mod_dir(dir) && !mod_may_serve(h)) continue;
    // Native texture libraries ask for bare names through mission mounts.
    // Resolve that mount's exact logical bank path, rather than matching a
    // basename (which would confuse different missions and common banks).
    if (selected_bank) {
      const auto bank = g_bank_index.find(bank_path_key(join(dir, h)));
      if (bank != g_bank_index.end()) {
        if (!is_file(bank->second))
          throw std::runtime_error("Selected environment bank disappeared: " +
                                   h);
        return bank->second;
      }
    }
    const std::string p = in_mount(dir, h);
    if (!p.empty()) return p;
  }
  return is_file(h) ? h : std::string{};  // relative to the working directory, as before
}

std::vector<std::string> find_all(const char* path) {
  std::vector<std::string> out;
  const std::string h = host(path);
  if (h.empty() || outside(h)) return out;
  if (absolute(h)) {
    if (is_file(h)) out.push_back(h);
    return out;
  }
  if (!g_mounts.write.empty() && is_file(join(g_mounts.write, h))) out.push_back(join(g_mounts.write, h));
  for (const std::string& dir : g_mounts.read) {
    if (!g_mounts.mods.empty() && is_mod_dir(dir) && !mod_may_serve(h)) continue;
    const std::string p = in_mount(dir, h);
    if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
  }
  return out;
}

bool exists(const char* path) { return !find(path).empty(); }

std::string write_path(const char* path) {
  const std::string h = host(path);
  if (outside(h)) return {};
  if (absolute(h)) return h;
  const std::string& dir = !g_mounts.write.empty() ? g_mounts.write : g_mounts.read.empty() ? std::string{} : g_mounts.read[0];
  return join(dir, h);
}

std::FILE* open(const char* path, const char* mode) {
  const std::string m = mode ? mode : "rb";
  if (m.find('w') != std::string::npos || m.find('a') != std::string::npos) {
    const std::string p = write_path(path);
    if (p.empty()) return nullptr;
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::u8path(p).parent_path(), ec);
    return open_host_file(p, m.c_str());
  }
  const std::string p = find(path);
  return p.empty() ? nullptr : open_host_file(p, m.c_str());
}

std::vector<std::string> list(const char* pattern) {
  const std::string h = host(pattern);
  if (outside(h)) return {};
  const size_t slash = h.rfind('/');
  const std::string dir = slash == std::string::npos ? std::string{} : h.substr(0, slash);
  const std::string pat = slash == std::string::npos ? h : h.substr(slash + 1);
  std::vector<std::string> roots;
  if (!g_mounts.write.empty()) roots.push_back(g_mounts.write);
  roots.insert(roots.end(), g_mounts.read.begin(), g_mounts.read.end());
  if (roots.empty()) roots.push_back(".");
  std::vector<std::string> out;
  for (const std::string& r : roots) {
    if (!g_mounts.mods.empty() && is_mod_dir(r) && !mod_may_serve(dir + "/")) continue;
    std::error_code ec;
    const std::string at = any_case(r, dir);  // the folder's case too, as the names'
    for (std::filesystem::directory_iterator it(at.empty() ? join(r, dir) : at, ec), end; !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      std::string name;
      if (!to_game(it->path().filename().string(), &name)) continue;  // no name the game could open
      if (!match(pat.c_str(), name.c_str())) continue;
      const std::string u = upper(name);
      if (std::none_of(out.begin(), out.end(), [&](const std::string& o) { return upper(o) == u; }))
        out.push_back(name);
    }
  }
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return upper(a) < upper(b); });
  return out;
}

}  // namespace zutl
