#include "mech3/source_map.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <memory>
#include <sstream>

#include "gamez/zutil/zutl_zar.h"
#include "mech3/data_version.h"
#include "mech3/disc_install.h"
#include "mech3/patch_import.h"
#include "mech3/source_archive.h"
#include "gamez/zutil/zutl_path.h"

namespace fs = std::filesystem;

namespace mech3::source {
namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool ends(const std::string& s, const char* e) {
  const size_t n = std::strlen(e);
  return s.size() >= n && s.compare(s.size() - n, n, e) == 0;
}
std::string base_of(const std::string& p) {
  const size_t k = p.find_last_of('/');
  return k == std::string::npos ? p : p.substr(k + 1);
}
std::string dir_of(const std::string& p) {
  const size_t k = p.find_last_of('/');
  return k == std::string::npos ? "" : p.substr(0, k + 1);
}

// What a zbd/reader.zbd's bytes say: the game by the archive's version (MW3
// 1, Pirate's Moon 2, docs/pirates-moon.md), the data version by size and CRC.
void reader_info(const std::vector<uint8_t>& b, Found::Game* game, std::string* version) {
  *game = Found::kUnknown;
  version->clear();
  if (b.size() < 12) return;
  uint32_t v8;
  std::memcpy(&v8, b.data() + b.size() - 8, 4);
  uint32_t v12;
  std::memcpy(&v12, b.data() + b.size() - 12, 4);
  if (v8 == 1) *game = Found::kMw3;
  else if (v12 == 2) *game = Found::kPiratesMoon;
  if (*game != Found::kMw3) return;
  int n = 0;
  const DataFile* files = data_files(&n);
  for (int i = 0; i < n; ++i) {
    if (std::strcmp(files[i].path, "zbd/reader.zbd") != 0) continue;
    const uint32_t crc = zutl_zar::crc32(0, b.data(), b.size());
    if (b.size() == files[i].size_10 && crc == files[i].crc_10) *version = "1.0";
    if (b.size() == files[i].size_12 && crc == files[i].crc_12) *version = "1.2";
  }
}

// What a disc's DATA1.HDR lists: Pirate's Moon by its standalone missions
// (readersa1.zbd), MechWarrior 3 by its zbd/reader.zbd, the version by that
// file's size.
void hdr_info(const std::vector<uint8_t>& hdr, Found::Game* game, std::string* version, std::string* why) {
  *game = Found::kUnknown;
  version->clear();
  std::vector<std::pair<std::string, uint64_t>> files;
  std::string err;
  if (!list_installshield(hdr, &files, &err)) {
    *why = "its installer header could not be read (" + err + ")";
    return;
  }
  uint64_t reader = 0;
  bool expansion = false;
  for (const auto& [p, size] : files) {
    const std::string l = lower(p);
    if (l == "zbd/reader.zbd") reader = size;
    if (l.find("readersa1.zbd") != std::string::npos) expansion = true;
  }
  if (expansion) {
    *game = Found::kPiratesMoon;
    return;
  }
  if (!reader) {
    *why = "its installer holds no game data";
    return;
  }
  *game = Found::kMw3;
  int n = 0;
  const DataFile* df = data_files(&n);
  for (int i = 0; i < n; ++i)
    if (std::strcmp(df[i].path, "zbd/reader.zbd") == 0) {
      if (reader == df[i].size_10) *version = "1.0";
      if (reader == df[i].size_12) *version = "1.2";
    }
}

// A forward reader of a disc image's 2048-byte sectors from a stream.
class SectorStream {
 public:
  explicit SectorStream(ArchiveReader::ReadFn read) : read_(std::move(read)) {}
  // Sector `n` (not before the last one read), into `out`.
  bool get(uint64_t n, uint8_t* out) {
    if (n < next_) return false;
    while (next_ <= n) {
      size_t got = 0;
      while (got < 2048) {
        const int64_t r = read_(out + got, 2048 - got);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
      }
      ++next_;
    }
    return true;
  }

 private:
  ArchiveReader::ReadFn read_;
  uint64_t next_ = 0;
};

// A ReadFn that gives `prefix` first, then the rest of `rest`.
ArchiveReader::ReadFn prefixed(std::vector<uint8_t> prefix, ArchiveReader::ReadFn rest) {
  auto st = std::make_shared<std::pair<std::vector<uint8_t>, size_t>>(std::move(prefix), 0);
  return [st, rest](uint8_t* buf, size_t n) -> int64_t {
    if (st->second < st->first.size()) {
      const size_t k = std::min(n, st->first.size() - st->second);
      std::memcpy(buf, st->first.data() + st->second, k);
      st->second += k;
      return static_cast<int64_t>(k);
    }
    return rest(buf, n);
  };
}

// A disc image read from a stream: its kind (raw or 2048-byte sectors), its
// volume label, and its DATA1.HDR (read when the stream reaches it).
struct ImageInfo {
  bool image = false, raw = false;
  int offset = 0;
  std::string label;
  std::vector<uint8_t> hdr;
};
ImageInfo read_image(ArchiveReader::ReadFn read) {
  ImageInfo info;
  std::vector<uint8_t> head;
  head.resize(17 * 2352);
  size_t got = 0;
  while (got < head.size()) {
    const int64_t r = read(head.data() + got, head.size() - got);
    if (r <= 0) break;
    got += static_cast<size_t>(r);
  }
  head.resize(got);
  static const uint8_t sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
  ArchiveReader::ReadFn cooked;
  if (head.size() >= 2352 && std::memcmp(head.data(), sync, 12) == 0) {
    info.raw = true;
    info.offset = head[15] == 2 ? 24 : 16;
    cooked = cooked_sectors(prefixed(head, read), info.offset);
  } else if (head.size() > 0x8006 && std::memcmp(head.data() + 0x8001, "CD001", 5) == 0) {
    cooked = prefixed(head, read);
  } else {
    return info;  // not a data track (an audio track, or not an image)
  }
  SectorStream s(cooked);
  uint8_t pvd[2048];
  if (!s.get(16, pvd) || std::memcmp(pvd + 1, "CD001", 5) != 0) return info;
  info.image = true;
  info.label.assign(reinterpret_cast<char*>(pvd + 40), 32);
  while (!info.label.empty() && info.label.back() == ' ') info.label.pop_back();
  uint32_t root, root_size;
  std::memcpy(&root, pvd + 156 + 2, 4);
  std::memcpy(&root_size, pvd + 156 + 10, 4);
  // The root directory: DATA1.HDR's extent and size.
  uint32_t at = 0, size = 0;
  uint8_t sec[2048];
  for (uint32_t i = 0; i < (root_size + 2047) / 2048 && !at; ++i) {
    if (!s.get(root + i, sec)) return info;
    for (uint32_t o = 0; o < 2048 && sec[o];) {
      const uint8_t len = sec[o];
      const uint8_t nl = sec[o + 32];
      std::string name(reinterpret_cast<char*>(sec + o + 33), nl);
      name = lower(name.substr(0, name.find(';')));
      if (name == "data1.hdr") {
        std::memcpy(&at, sec + o + 2, 4);
        std::memcpy(&size, sec + o + 10, 4);
        break;
      }
      o += len;
    }
  }
  if (!at || size > (8u << 20)) return info;
  info.hdr.resize(size);
  for (uint32_t i = 0; i * 2048 < size; ++i) {
    if (!s.get(at + i, sec)) {
      info.hdr.clear();
      return info;
    }
    std::memcpy(info.hdr.data() + i * 2048, sec, std::min<size_t>(2048, size - i * 2048));
  }
  return info;
}

// A cue sheet's tracks: each FILE's name and whether it is AUDIO.
struct CueTrack {
  std::string file;
  int number = 0;
  int64_t start = 0;  // INDEX 01, in bytes into its file
  bool audio = false;
  bool raw = true;
  int offset = 16;
};
std::vector<CueTrack> parse_cue(const std::string& text) {
  std::vector<CueTrack> out;
  std::istringstream in(text);
  std::string line, file;
  while (std::getline(in, line)) {
    std::istringstream w(line);
    std::string kw;
    w >> kw;
    kw = lower(kw);
    if (kw == "file") {
      const size_t a = line.find('"'), b = line.rfind('"');
      file = a != std::string::npos && b > a ? line.substr(a + 1, b - a - 1) : "";
    } else if (kw == "track") {
      std::string num, mode;
      w >> num >> mode;
      mode = lower(mode);
      CueTrack t;
      t.file = file;
      t.number = std::atoi(num.c_str());
      t.audio = mode == "audio";
      t.raw = mode.find("2352") != std::string::npos;
      t.offset = mode.rfind("mode2", 0) == 0 ? 24 : 16;
      if (!t.raw) t.offset = 0;
      out.push_back(t);
    } else if (kw == "index" && !out.empty()) {
      std::string num, msf;
      w >> num >> msf;
      int m = 0, sec = 0, fr = 0;
      if (std::atoi(num.c_str()) == 1 && std::sscanf(msf.c_str(), "%d:%d:%d", &m, &sec, &fr) == 3)
        out.back().start = (static_cast<int64_t>(m) * 60 * 75 + sec * 75 + fr) * 2352;
    }
  }
  return out;
}

struct Walk {
  Found best;
  std::string detail;
  MapProgress progress;
  ArchiveReader* top = nullptr;  // the picked file's reader: its place for the fraction
  uint64_t top_size = 1;
  bool stopped = false;
  bool go(const std::string& entry) {
    if (stopped) return false;
    if (progress && !progress(top ? std::min(1.0, static_cast<double>(top->position()) / top_size) : 0.0, entry))
      stopped = true;
    return !stopped;
  }
  // `read` asking go() every 4 MB: a stop ends the stream.
  ArchiveReader::ReadFn watched(ArchiveReader::ReadFn read, const std::string& entry) {
    auto since = std::make_shared<uint64_t>(0);
    return [this, read, entry, since](uint8_t* buf, size_t n) -> int64_t {
      if ((*since += n) >= (4u << 20)) {
        *since = 0;
        if (!go(entry)) return -1;
      }
      return read(buf, n);
    };
  }
  void consider(const Found& f) {
    // MechWarrior 3 over Pirate's Moon over nothing; an installed copy over a disc.
    auto rank = [](const Found& x) {
      return x.kind == Found::kNothing ? 0 : (x.game == Found::kMw3 ? 10 : 2) + (x.kind == Found::kInstall ? 1 : 0);
    };
    if (rank(f) > rank(best)) best = f;
  }
};

std::vector<uint8_t> read_whole(ArchiveReader& r, size_t limit) {
  std::vector<uint8_t> out;
  uint8_t buf[1 << 16];
  for (int64_t n; (n = r.read(buf, sizeof buf)) > 0;) {
    out.insert(out.end(), buf, buf + n);
    if (out.size() > limit) break;
  }
  return out;
}

void walk_archive(ArchiveReader& r, const std::string& path, const std::vector<Step>& steps, int depth, Walk& w) {
  std::vector<std::pair<std::string, std::vector<CueTrack>>> cues;  // their folder, their tracks
  std::vector<std::pair<std::string, Found>> images;                // the data tracks found, by entry
  ArchiveReader::Entry e;
  while (r.next(&e) && w.go(e.path)) {
    if (e.dir) continue;
    const std::string l = lower(e.path), base = base_of(l);
    if (base == "reader.zbd" && ends(dir_of(l), "zbd/")) {
      const std::vector<uint8_t> b = read_whole(r, 64u << 20);
      Found f;
      reader_info(b, &f.game, &f.version);
      if (f.game == Found::kUnknown) continue;
      f.kind = Found::kInstall, f.path = path, f.steps = steps;
      const std::string d = dir_of(e.path);
      f.root = d.substr(0, d.size() - 4);  // without "zbd/"
      w.detail += "  an installed copy at " + (f.root.empty() ? "the top" : f.root) + "\n";
      w.consider(f);
    } else if (base == "data1.hdr") {
      const std::vector<uint8_t> b = read_whole(r, 8u << 20);
      Found f;
      std::string why;
      hdr_info(b, &f.game, &f.version, &why);
      if (f.game == Found::kUnknown) {
        w.detail += "  " + e.path + ": " + why + "\n";
        continue;
      }
      f.kind = Found::kDisc, f.path = path, f.steps = steps, f.root = dir_of(e.path);
      w.detail += "  a disc's files at " + (f.root.empty() ? "the top" : f.root) + "\n";
      w.consider(f);
    } else if (ends(base, ".cue")) {
      const std::vector<uint8_t> b = read_whole(r, 1u << 20);
      cues.emplace_back(dir_of(e.path), parse_cue(std::string(b.begin(), b.end())));
    } else if (ends(base, ".iso") || ends(base, ".bin") || ends(base, ".img")) {
      const ImageInfo info = read_image(w.watched(r.entry_stream(), e.path));
      if (!info.image) continue;
      Found f;
      std::string why = "it has no installer data";
      if (!info.hdr.empty()) hdr_info(info.hdr, &f.game, &f.version, &why);
      w.detail += "  " + e.path + ": a disc image labelled \"" + info.label + "\"" +
                  (f.game == Found::kUnknown ? ", but " + why : "") + "\n";
      if (f.game == Found::kUnknown) continue;
      Step s;
      s.kind = info.raw ? Step::kRawImage : Step::kImage, s.entry = e.path, s.raw_offset = info.offset;
      f.kind = Found::kDisc, f.path = path, f.steps = steps, f.label = info.label;
      f.steps.push_back(s);
      f.data_track = e.path, f.raw = info.raw, f.raw_offset = info.offset;
      images.emplace_back(e.path, f);
    } else if (archive_name(base) && depth < 4) {
      ArchiveReader inner;
      if (!inner.open_stream(w.watched(r.entry_stream(), e.path))) continue;
      std::vector<Step> next = steps;
      Step s;
      s.entry = e.path;
      next.push_back(s);
      w.detail += "  into " + e.path + "\n";
      walk_archive(inner, path, next, depth + 1, w);
    }
  }
  // The images with their cue's audio tracks (a cue may come after its .bins).
  for (auto& [entry, f] : images) {
    for (const auto& [dir, tracks] : cues)
      for (const CueTrack& t : tracks)
        if (lower(dir + t.file) == lower(entry))
          for (const CueTrack& a : tracks)
            if (a.audio) f.audio.push_back(dir + a.file), f.audio_track.push_back(a.number), f.audio_start.push_back(a.start);
    w.consider(f);
  }
}

}  // namespace

std::string game_words(const Found& f) {
  if (f.game == Found::kPiratesMoon)
    return "This is MechWarrior 3: Pirate's Moon. Its support comes later: point to MechWarrior 3 itself.";
  if (f.game != Found::kMw3) return "";
  std::string v = f.version.empty() ? "" : " version " + f.version;
  if (f.kind == Found::kInstall) return "An installed copy of MechWarrior 3" + v + ".";
  std::string s = "The MechWarrior 3 disc" + v;
  if (!f.audio.empty())
    s += ", with its music (" + std::to_string(f.audio.size()) + (f.audio.size() == 1 ? " CD track)" : " CD tracks)");
  return s + ".";
}

Found map_source(const std::string& path, const MapProgress& progress) {
  Walk w;
  w.progress = progress;
  std::error_code ec;
  fs::path p(path);
  if (fs::is_regular_file(p, ec) && lower(p.filename().string()) == "mech3.exe") p = p.parent_path();
  const std::string ps = p.string();
  if (fs::is_directory(p, ec) && !zutl::any_case(ps, "zbd/reader.zbd").empty()) {
    // An installed copy, on disk.
    Found f;
    std::FILE* fh = std::fopen(zutl::any_case(ps, "zbd/reader.zbd").c_str(), "rb");
    std::vector<uint8_t> b;
    if (fh) {
      uint8_t buf[1 << 16];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, fh)) > 0;) b.insert(b.end(), buf, buf + n);
      std::fclose(fh);
    }
    reader_info(b, &f.game, &f.version);
    if (f.game != Found::kUnknown) {
      f.kind = Found::kInstall, f.path = ps, f.on_disk = true;
      w.consider(f);
    }
  } else {
    // A disc, a drive, an .iso or a .cue on disk.
    Disc disc;
    std::string err;
    const bool image_or_folder = fs::is_directory(p, ec) || ends(lower(ps), ".iso") || ends(lower(ps), ".cue") ||
                                 ends(lower(ps), ".bin") || ends(lower(ps), ".img");
    if (image_or_folder && disc.open(ps, &err)) {
      if (const Disc::File* hdr = disc.find("DATA1.HDR")) {
        std::vector<uint8_t> h(static_cast<size_t>(hdr->size));
        Found f;
        std::string why;
        if (disc.read(*hdr, 0, h.size(), h.data())) hdr_info(h, &f.game, &f.version, &why);
        if (f.game != Found::kUnknown) {
          f.kind = Found::kDisc, f.path = ps, f.on_disk = true;
          if (!disc.cue.empty()) {
            std::ifstream in(disc.cue, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            for (const CueTrack& t : parse_cue(text))
              if (t.audio) f.audio.push_back(t.file), f.audio_track.push_back(t.number), f.audio_start.push_back(t.start);
          }
          w.consider(f);
        } else {
          w.detail += "  " + why + "\n";
        }
      }
    } else if (fs::is_regular_file(p, ec)) {
      // Anything else: an archive, perhaps.
      ArchiveReader r;
      if (r.open_file(ps)) {
        w.top = &r, w.top_size = std::max<uint64_t>(1, fs::file_size(p, ec));
        walk_archive(r, ps, {}, 0, w);
        if (!r.error().empty()) w.detail += "  " + r.error() + "\n";
      } else {
        w.detail += "  not an archive this reads: " + r.error() + "\n";
      }
    }
  }
  if (w.stopped) {
    Found f;
    f.path = ps, f.cancelled = true, f.detail = w.detail;
    f.summary = "Stopped. Nothing was changed.";
    return f;
  }
  Found f = w.best;
  f.path = ps;
  f.detail = w.detail;
  if (f.kind == Found::kNothing) {
    f.summary = archives_supported()
                    ? "No MechWarrior 3 was found in that. Point to your installed game, your disc, a disc image, or the "
                      "file you downloaded."
                    : "No MechWarrior 3 was found in that. Point to your installed game, your disc or a disc image (.iso "
                      "or .cue).";
  } else {
    f.summary = game_words(f);
  }
  return f;
}

}  // namespace mech3::source
