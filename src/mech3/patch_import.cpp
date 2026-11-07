#include "mech3/patch_import.h"
#include "platform/sha256.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <system_error>

#include "gamez/zutil/zutl_zar.h"
#include "mech3/data_version.h"
#include "miniz/miniz_tinfl.h"

namespace mech3 {
namespace fs = std::filesystem;
namespace {

uint32_t le16(const uint8_t* p) { return p[0] | p[1] << 8; }
uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
  return s;
}

bool read_file(const fs::path& p, std::vector<uint8_t>* out) {
  std::FILE* f = std::fopen(p.string().c_str(), "rb");
  if (!f) return false;
  out->clear();
  uint8_t buf[1 << 16];
  size_t got;
  while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + got);
  std::fclose(f);
  return true;
}

// Raw deflate into `out` (its size the expected length, or grown when 0).
// A chunk that ends on a flush, not a final block, is whole once every byte
// of it is taken.
bool inflate_raw(const uint8_t* in, size_t n, std::vector<uint8_t>* out, size_t expect) {
  tinfl_decompressor d;
  tinfl_init(&d);
  out->assign(expect ? expect : n * 4 + 1024, 0);
  size_t in_at = 0, out_at = 0;
  for (;;) {
    size_t in_n = n - in_at, out_n = out->size() - out_at;
    const tinfl_status s = tinfl_decompress(&d, in + in_at, &in_n, out->data(), out->data() + out_at, &out_n,
                                            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    in_at += in_n, out_at += out_n;
    if (s == TINFL_STATUS_DONE || (s == TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS && in_at == n)) break;
    if (s == TINFL_STATUS_HAS_MORE_OUTPUT && !expect) {
      out->resize(out->size() * 2);
      continue;
    }
    return false;
  }
  if (expect && out_at != expect) return false;
  out->resize(out_at);
  return true;
}

// ZIP's CRC-32 (reflected, 0xedb88320), not zutl_zar's.
uint32_t zip_crc(const uint8_t* p, size_t n) {
  static uint32_t t[256];
  if (!t[1])
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

// A ZIP read from its central directory, entry by entry (no Zip64: the
// patch's archives are a few MB). A self-extracting exe with a ZIP appended
// reads the same, its offsets shifted by the stub.
struct Zip {
  struct Entry {
    std::string name;
    uint32_t method, crc, csize, usize, local;
  };
  std::FILE* f = nullptr;
  std::vector<Entry> entries;
  ~Zip() {
    if (f) std::fclose(f);
  }
  bool open(const fs::path& p) {
    f = std::fopen(p.string().c_str(), "rb");
    if (!f || std::fseek(f, 0, SEEK_END) != 0) return false;
    const long size = std::ftell(f);
    const long tail = std::min<long>(size, 0x10000 + 22);
    std::vector<uint8_t> t(static_cast<size_t>(tail));
    if (std::fseek(f, size - tail, SEEK_SET) != 0 || std::fread(t.data(), 1, t.size(), f) != t.size()) return false;
    long eocd = -1;
    for (long i = tail - 22; i >= 0; --i)
      if (le32(&t[i]) == 0x06054b50) {
        eocd = i;
        break;
      }
    if (eocd < 0) return false;
    const uint8_t* e = &t[eocd];
    const uint32_t count = le16(e + 10), cd_size = le32(e + 12), cd_off = le32(e + 16);
    const long cd_at = size - tail + eocd - static_cast<long>(cd_size);
    if (cd_at < 0 || cd_off == 0xffffffffu) return false;
    const long shift = cd_at - static_cast<long>(cd_off);  // the stub before an appended ZIP
    std::vector<uint8_t> cd(cd_size);
    if (std::fseek(f, cd_at, SEEK_SET) != 0 || std::fread(cd.data(), 1, cd.size(), f) != cd.size()) return false;
    for (size_t at = 0, k = 0; k < count && at + 46 <= cd.size(); ++k) {
      const uint8_t* c = &cd[at];
      if (le32(c) != 0x02014b50) return false;
      const uint32_t nlen = le16(c + 28), xlen = le16(c + 30), clen = le16(c + 32);
      if (at + 46 + nlen > cd.size()) return false;
      Entry en{std::string(reinterpret_cast<const char*>(c + 46), nlen), le16(c + 10), le32(c + 16), le32(c + 20),
               le32(c + 24), static_cast<uint32_t>(le32(c + 42) + shift)};
      std::replace(en.name.begin(), en.name.end(), '\\', '/');
      if (!en.name.empty() && en.name.back() != '/') entries.push_back(en);
      at += 46 + nlen + xlen + clen;
    }
    return true;
  }
  bool read(const Entry& en, std::vector<uint8_t>* out, std::string* error) {
    uint8_t h[30];
    if (std::fseek(f, static_cast<long>(en.local), SEEK_SET) != 0 || std::fread(h, 1, 30, f) != 30 || le32(h) != 0x04034b50) {
      *error = en.name + ": no local header";
      return false;
    }
    std::vector<uint8_t> raw(en.csize);
    if (std::fseek(f, static_cast<long>(en.local + 30 + le16(h + 26) + le16(h + 28)), SEEK_SET) != 0 ||
        std::fread(raw.data(), 1, raw.size(), f) != raw.size()) {
      *error = en.name + ": cut short";
      return false;
    }
    if (en.method == 0) {
      *out = std::move(raw);
    } else if (en.method != 8 || !inflate_raw(raw.data(), raw.size(), out, en.usize)) {
      *error = en.name + (en.method == 8 ? ": does not inflate" : ": compressed by a method other than deflate");
      return false;
    }
    if (out->size() != en.usize || zip_crc(out->data(), out->size()) != en.crc) {
      *error = en.name + ": CRC does not match (a damaged download?)";
      return false;
    }
    return true;
  }
};

// What has been found of the 28, by their paths under the install.
struct Gather {
  const DataFile* files;
  int n;
  std::map<int, std::vector<uint8_t>> got;
  std::vector<std::string> log;
  std::string first_error;
  Gather() { files = data_files(&n); }

  // The file `path` names, if it is one of the 28 (its path's end, any case).
  int which(const std::string& path) const {
    const std::string p = lower(path);
    for (int i = 0; i < n; ++i) {
      const std::string t = lower(files[i].path);
      if (p == t || (p.size() > t.size() && p.compare(p.size() - t.size(), t.size(), t) == 0 && p[p.size() - t.size() - 1] == '/'))
        return i;
    }
    return -1;
  }
  bool wants(const std::string& path) const {
    const int i = which(path);
    return i >= 0 && !got.count(i);
  }
  // Kept when it is 1.2's; another version's file of the same name is not.
  void offer(const std::string& path, std::vector<uint8_t>&& data) {
    const int i = which(path);
    if (i < 0 || got.count(i)) return;
    const DataFile& k = files[i];
    if (data.size() != k.size_12 || zutl_zar::crc32(0, data.data(), data.size()) != k.crc_12 ||
        sha256_hex(data.data(), data.size()) != k.sha256_12)
      return;
    got[i] = std::move(data);
  }
  void cabinet(const std::string& where, const std::vector<uint8_t>& hdr, const std::vector<uint8_t>& cab) {
    std::vector<PatchEntry> entries;
    std::string error;
    if (!read_installshield(hdr, cab, &entries, &error)) {
      log.push_back(where + ": " + error);
      if (first_error.empty()) first_error = where + ": " + error;
      return;
    }
    const size_t before = got.size();
    for (PatchEntry& e : entries) offer(e.path, std::move(e.data));
    log.push_back(where + ": InstallShield cabinet, " + std::to_string(entries.size()) + " files, " +
                  std::to_string(got.size() - before) + " taken");
  }
  void zip(const fs::path& p) {
    Zip z;
    if (!z.open(p)) {
      log.push_back(p.string() + ": not a ZIP");
      return;
    }
    const size_t before = got.size();
    std::string error;
    for (const Zip::Entry& e : z.entries) {
      const std::string name = lower(e.name);
      if (name == "data1.hdr" || (name.size() > 10 && name.compare(name.size() - 10, 10, "/data1.hdr") == 0)) {
        const std::string cab_name = name.substr(0, name.size() - 3) + "cab";
        auto c = std::find_if(z.entries.begin(), z.entries.end(), [&](const Zip::Entry& x) { return lower(x.name) == cab_name; });
        std::vector<uint8_t> hdr, cab;
        if (c == z.entries.end()) {
          log.push_back(p.string() + ": " + e.name + " without its data1.cab");
        } else if (!z.read(e, &hdr, &error) || !z.read(*c, &cab, &error)) {
          log.push_back(p.string() + ": " + error);
          if (first_error.empty()) first_error = error;
        } else {
          cabinet(p.string() + ": " + e.name, hdr, cab);
        }
      } else if (wants(e.name)) {
        std::vector<uint8_t> data;
        if (z.read(e, &data, &error)) offer(e.name, std::move(data));
        else if (first_error.empty()) first_error = error;
      }
    }
    log.push_back(p.string() + ": ZIP, " + std::to_string(z.entries.size()) + " entries, " + std::to_string(got.size() - before) +
                  " taken in all");
  }
  void header(const fs::path& hdr_path) {
    std::vector<uint8_t> hdr, cab;
    fs::path cab_path;
    std::error_code ec;
    for (const auto& d : fs::directory_iterator(hdr_path.parent_path(), ec))
      if (lower(d.path().filename().string()) == "data1.cab") cab_path = d.path();
    if (cab_path.empty() || !read_file(hdr_path, &hdr) || !read_file(cab_path, &cab)) {
      log.push_back(hdr_path.string() + ": no data1.cab beside it");
      return;
    }
    cabinet(hdr_path.string(), hdr, cab);
  }
  void source(const fs::path& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
      int seen = 0;
      for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
           it != fs::recursive_directory_iterator() && seen < 20000; it.increment(ec), ++seen) {
        if (ec) break;
        if (it.depth() > 6) it.disable_recursion_pending();
        if (!it->is_regular_file(ec)) continue;
        const std::string name = lower(it->path().filename().string());
        std::string rel = fs::relative(it->path(), p, ec).generic_string();
        if (name == "data1.hdr") header(it->path());
        else if (name.size() > 4 && name.compare(name.size() - 4, 4, ".zip") == 0) zip(it->path());
        else if (wants(rel)) {
          std::vector<uint8_t> data;
          if (read_file(it->path(), &data)) offer(rel, std::move(data));
        }
      }
      log.push_back(p.string() + ": folder, " + std::to_string(got.size()) + " of the " + std::to_string(n) + " found");
      return;
    }
    std::FILE* f = std::fopen(p.string().c_str(), "rb");
    uint8_t magic[4] = {};
    const bool read = f && std::fread(magic, 1, 4, f) == 4;
    if (f) std::fclose(f);
    if (!read) log.push_back(p.string() + ": cannot be read");
    else if (le32(magic) == 0x28635349) header(p);  // "ISc("
    else if (le32(magic) == 0x04034b50 || (magic[0] == 'M' && magic[1] == 'Z')) zip(p);
    else log.push_back(p.string() + ": not a ZIP, a folder or a data1.hdr");
  }
};

}  // namespace

// The layout as unshield (github.com/twogood/unshield, lib/) reads version 5:
// the common header, the cab descriptor's file table (directories, then file
// descriptors of 0x3a bytes), each compressed file as chunks of a 16-bit
// length and raw deflate, its expanded bytes' MD5 at the descriptor's +0x2a.
bool each_installshield(const std::vector<uint8_t>& hdr, uint64_t cab_size, const CabRead& read,
                        const std::function<bool(PatchEntry&&)>& each, std::string* error) {
  auto fail = [&](const std::string& why) {
    *error = why;
    return false;
  };
  if (hdr.size() < 20 || le32(hdr.data()) != 0x28635349) return fail("not an InstallShield header");
  const uint32_t version = le32(&hdr[4]);
  const uint32_t major = (version >> 24) == 1 ? (version >> 12) & 0xf : 0;
  if (major != 5) return fail("InstallShield version " + std::to_string(major) + ", not 5 (version word " + std::to_string(version) + ")");
  const size_t desc = le32(&hdr[12]);
  if (desc + 0x30 > hdr.size()) return fail("cab descriptor out of the header");
  const size_t table = desc + le32(&hdr[desc + 0x0c]);
  const uint32_t dirs = le32(&hdr[desc + 0x1c]), files = le32(&hdr[desc + 0x28]);
  if (table + 4ull * (dirs + files) > hdr.size()) return fail("file table out of the header");
  auto str = [&](size_t at) {
    std::string s;
    while (at < hdr.size() && hdr[at]) s += static_cast<char>(hdr[at++]);
    return s;
  };
  for (uint32_t i = 0; i < files; ++i) {
    const size_t at = table + le32(&hdr[table + 4 * (dirs + i)]);
    if (at + 0x3a > hdr.size()) return fail("file descriptor out of the header");
    const uint8_t* d = &hdr[at];
    const uint32_t name = le32(d), dir = le32(d + 4), flags = le16(d + 8);
    const uint32_t expanded = le32(d + 10), compressed = le32(d + 14), offset = le32(d + 0x26);
    if ((flags & 8) || !name || !offset || dir >= dirs) continue;  // invalid: no data
    std::string path = str(table + le32(&hdr[table + 4 * dir]));
    path = (path.empty() ? "" : path + "/") + str(table + name);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (flags & 1) return fail(path + " is split across volumes");
    if (flags & 2) return fail(path + " is obfuscated (not in MW3's cabinets)");
    const size_t stored = flags & 4 ? compressed : expanded;
    if (offset + static_cast<uint64_t>(stored) > cab_size) return fail(path + ": data past the cabinet's end");
    std::vector<uint8_t> raw(stored);
    if (stored && !read(offset, stored, raw.data())) return fail(path + ": cannot be read from the cabinet");
    PatchEntry e{path, {}};
    if (flags & 4) {
      e.data.reserve(expanded);
      for (size_t k = 0; k < raw.size();) {
        const size_t len = k + 2 <= raw.size() ? le16(&raw[k]) : 0;
        if (!len || k + 2 + len > raw.size()) return fail(path + ": bad chunk length");
        std::vector<uint8_t> chunk;
        if (!inflate_raw(&raw[k + 2], len, &chunk, 0)) return fail(path + ": does not inflate");
        e.data.insert(e.data.end(), chunk.begin(), chunk.end());
        k += 2 + len;
      }
    } else {
      e.data = std::move(raw);
    }
    if (e.data.size() != expanded) return fail(path + ": " + std::to_string(e.data.size()) + " bytes, not " + std::to_string(expanded));
    uint8_t sum[16];
    md5(e.data.data(), e.data.size(), sum);
    if (std::memcmp(sum, d + 0x2a, 16) != 0) return fail(path + ": MD5 does not match (a damaged cabinet?)");
    if (!each(std::move(e))) return fail(path + ": not taken");
  }
  return true;
}

bool list_installshield(const std::vector<uint8_t>& hdr, std::vector<std::pair<std::string, uint64_t>>* out,
                        std::string* error) {
  if (hdr.size() < 20 || le32(hdr.data()) != 0x28635349) return *error = "not an InstallShield header", false;
  const uint32_t version = le32(&hdr[4]);
  if ((version >> 24) != 1 || ((version >> 12) & 0xf) != 5) return *error = "not InstallShield 5", false;
  const size_t desc = le32(&hdr[12]);
  if (desc + 0x30 > hdr.size()) return *error = "cab descriptor out of the header", false;
  const size_t table = desc + le32(&hdr[desc + 0x0c]);
  const uint32_t dirs = le32(&hdr[desc + 0x1c]), files = le32(&hdr[desc + 0x28]);
  if (table + 4ull * (dirs + files) > hdr.size()) return *error = "file table out of the header", false;
  auto str = [&](size_t at) {
    std::string s;
    while (at < hdr.size() && hdr[at]) s += static_cast<char>(hdr[at++]);
    return s;
  };
  for (uint32_t i = 0; i < files; ++i) {
    const size_t at = table + le32(&hdr[table + 4 * (dirs + i)]);
    if (at + 0x3a > hdr.size()) return *error = "file descriptor out of the header", false;
    const uint8_t* d = &hdr[at];
    const uint32_t name = le32(d), dir = le32(d + 4), flags = le16(d + 8);
    if ((flags & 8) || !name || !le32(d + 0x26) || dir >= dirs) continue;
    std::string path = str(table + le32(&hdr[table + 4 * dir]));
    path = (path.empty() ? "" : path + "/") + str(table + name);
    std::replace(path.begin(), path.end(), '\\', '/');
    out->emplace_back(path, le32(d + 10));
  }
  return true;
}

bool read_installshield(const std::vector<uint8_t>& hdr, const std::vector<uint8_t>& cab, std::vector<PatchEntry>* out,
                        std::string* error) {
  return each_installshield(
      hdr, cab.size(),
      [&](uint64_t at, size_t n, uint8_t* to) {
        std::memcpy(to, cab.data() + at, n);
        return true;
      },
      [&](PatchEntry&& e) {
        out->push_back(std::move(e));
        return true;
      },
      error);
}

PatchImport import_patch_12(const std::string& source, const std::string& out_dir) {
  PatchImport r;
  Gather g;
  g.source(fs::path(source));
  r.log = g.log;
  r.found = static_cast<int>(g.got.size());
  for (int i = 0; i < g.n; ++i)
    if (!g.got.count(i)) r.missing.push_back(g.files[i].path);
  if (!r.missing.empty()) {
    r.error = "the patch's 1.2 files not found in " + source + " (" + std::to_string(r.found) + " of " + std::to_string(g.n) + ")";
    if (!g.first_error.empty()) r.error += "; " + g.first_error;
    return r;
  }
  const fs::path dst(out_dir), tmp(out_dir + ".new");
  std::error_code ec;
  fs::remove_all(tmp, ec);
  for (const auto& [i, data] : g.got) {
    const fs::path p = tmp / g.files[i].path;
    fs::create_directories(p.parent_path(), ec);
    std::FILE* f = std::fopen(p.string().c_str(), "wb");
    const bool written = f && std::fwrite(data.data(), 1, data.size(), f) == data.size();
    if (f && std::fclose(f) != 0) f = nullptr;
    if (!written || !f) {
      r.error = "cannot write " + p.string();
      fs::remove_all(tmp, ec);
      return r;
    }
  }
  fs::remove_all(dst, ec);
  fs::rename(tmp, dst, ec);
  if (ec) {
    r.error = "cannot rename " + tmp.string() + " to " + dst.string() + ": " + ec.message();
    return r;
  }
  r.ok = true;
  return r;
}

// SHA-256 (FIPS 180-4).
std::string sha256_hex(const uint8_t* data, size_t n) {
  platform::integrity::Sha256 hash;
  hash.update(data, n);
  return hash.hex();
}

// MD5 (RFC 1321): the cabinets' file descriptors carry their files'.
void md5(const uint8_t* data, size_t n, uint8_t out[16]) {
  static const uint32_t K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
      0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
      0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
      0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
      0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
      0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                            14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                            4,  11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  auto block = [&](const uint8_t* p) {
    uint32_t w[16];
    for (int i = 0; i < 16; ++i) w[i] = le32(p + 4 * i);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; ++i) {
      uint32_t f;
      int g;
      if (i < 16) f = (b & c) | (~b & d), g = i;
      else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
      else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) % 16;
      else f = c ^ (b | ~d), g = (7 * i) % 16;
      const uint32_t t = a + f + K[i] + w[g];
      a = d, d = c, c = b, b = b + ((t << R[i]) | (t >> (32 - R[i])));
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d;
  };
  size_t i = 0;
  for (; i + 64 <= n; i += 64) block(data + i);
  uint8_t last[128] = {};
  const size_t rest = n - i;
  if (rest) std::memcpy(last, data + i, rest);
  last[rest] = 0x80;
  const size_t len = rest < 56 ? 64 : 128;
  const uint64_t bits = static_cast<uint64_t>(n) * 8;
  for (int k = 0; k < 8; ++k) last[len - 8 + k] = static_cast<uint8_t>(bits >> (8 * k));
  block(last);
  if (len == 128) block(last + 64);
  for (int k = 0; k < 16; ++k) out[k] = static_cast<uint8_t>(h[k / 4] >> (8 * (k % 4)));
}

}  // namespace mech3
