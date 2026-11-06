#include "mech3/source_archive.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <vector>

#if MW3_HAVE_LIBARCHIVE
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX  // archive.h brings <windows.h>, whose min/max macros break std::min
#endif
#include <archive.h>
#include <archive_entry.h>
#endif

namespace mech3::source {

#if MW3_HAVE_LIBARCHIVE
bool archives_supported() { return true; }
#else
bool archives_supported() { return false; }
#endif

bool archive_name(const std::string& name) {
  std::string n;
  for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  auto ends = [&](const char* e) {
    const size_t k = std::strlen(e);
    return n.size() >= k && n.compare(n.size() - k, k, e) == 0;
  };
  for (const char* e : {".zip", ".7z", ".rar", ".tar", ".tgz", ".tar.gz", ".tar.xz", ".txz", ".gz", ".xz", ".iso"})
    if (ends(e)) return true;
  return false;
}

#if MW3_HAVE_LIBARCHIVE

struct ArchiveReader::Impl {
  archive* a = nullptr;
  ReadFn source;                // a stream's
  std::vector<uint8_t> buffer;  // what the read callback hands libarchive
  bool in_entry = false;

  static la_ssize_t read_cb(archive*, void* client, const void** out) {
    auto* self = static_cast<Impl*>(client);
    if (self->buffer.empty()) self->buffer.resize(1 << 16);
    const int64_t n = self->source(self->buffer.data(), self->buffer.size());
    *out = self->buffer.data();
    return n < 0 ? -1 : static_cast<la_ssize_t>(n);
  }
  ~Impl() {
    if (a) archive_read_free(a);
  }
};

ArchiveReader::ArchiveReader() : impl_(new Impl) {}
ArchiveReader::~ArchiveReader() = default;

namespace {
archive* reader(bool iso_only) {
  archive* a = archive_read_new();
  if (iso_only) {
    archive_read_support_format_iso9660(a);
  } else {
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
  }
  return a;
}
}  // namespace

bool ArchiveReader::open_file(const std::string& path) {
  impl_.reset(new Impl);
  impl_->a = reader(false);
  if (archive_read_open_filename(impl_->a, path.c_str(), 1 << 16) != ARCHIVE_OK) {
    error_ = archive_error_string(impl_->a) ? archive_error_string(impl_->a) : "cannot open";
    return false;
  }
  return true;
}

bool ArchiveReader::open_stream(ReadFn read) {
  impl_.reset(new Impl);
  impl_->a = reader(false);
  impl_->source = std::move(read);
  if (archive_read_open(impl_->a, impl_.get(), nullptr, &Impl::read_cb, nullptr) != ARCHIVE_OK) {
    error_ = archive_error_string(impl_->a) ? archive_error_string(impl_->a) : "not an archive";
    return false;
  }
  return true;
}

// An image's first 32 sectors (the system area and the volume descriptors)
// read ahead, each primary volume descriptor's reserved fields cleared, then
// the rest. Real pressings carry their mastering tool's marks there (the MW3
// disc: a Joliet escape "%/@" at byte 88 and bytes at 1395), which libarchive
// takes for a damaged image (docs/setup-sources.md).
static ArchiveReader::ReadFn reserved_fields_cleared(ArchiveReader::ReadFn read) {
  struct State {
    ArchiveReader::ReadFn read;
    std::vector<uint8_t> head;
    size_t at = 0;
  };
  auto st = std::make_shared<State>();
  st->read = std::move(read);
  st->head.resize(32 * 2048);
  size_t got = 0;
  while (got < st->head.size()) {
    const int64_t r = st->read(st->head.data() + got, st->head.size() - got);
    if (r <= 0) break;
    got += static_cast<size_t>(r);
  }
  st->head.resize(got);
  for (size_t sec = 16; (sec + 1) * 2048 <= got; ++sec) {
    uint8_t* d = st->head.data() + sec * 2048;
    if (std::memcmp(d + 1, "CD001", 5) != 0 || d[0] == 255) break;
    if (d[0] != 1) continue;
    d[7] = 0, d[882] = 0;
    std::memset(d + 72, 0, 8), std::memset(d + 88, 0, 32), std::memset(d + 1395, 0, 2048 - 1395);
  }
  return [st](uint8_t* buf, size_t n) -> int64_t {
    if (st->at < st->head.size()) {
      const size_t k = std::min(n, st->head.size() - st->at);
      std::memcpy(buf, st->head.data() + st->at, k);
      st->at += k;
      return static_cast<int64_t>(k);
    }
    return st->read(buf, n);
  };
}

bool ArchiveReader::open_iso_stream(ReadFn read) {
  impl_.reset(new Impl);
  impl_->a = reader(true);
  impl_->source = reserved_fields_cleared(std::move(read));
  if (archive_read_open(impl_->a, impl_.get(), nullptr, &Impl::read_cb, nullptr) != ARCHIVE_OK) {
    error_ = archive_error_string(impl_->a) ? archive_error_string(impl_->a) : "not an ISO 9660 image";
    return false;
  }
  return true;
}

bool ArchiveReader::next(Entry* out) {
  if (!impl_->a) return false;
  archive_entry* e = nullptr;
  const int r = archive_read_next_header(impl_->a, &e);
  if (r == ARCHIVE_EOF) return false;
  if (r < ARCHIVE_WARN) {
    error_ = archive_error_string(impl_->a) ? archive_error_string(impl_->a) : "damaged archive";
    return false;
  }
  const char* p = archive_entry_pathname(e);
  out->path = p ? p : "";
  std::replace(out->path.begin(), out->path.end(), '\\', '/');
  out->size = archive_entry_size_is_set(e) ? archive_entry_size(e) : -1;
  out->dir = archive_entry_filetype(e) == AE_IFDIR;
  impl_->in_entry = true;
  return true;
}

int64_t ArchiveReader::read(uint8_t* buf, size_t n) {
  if (!impl_->a || !impl_->in_entry) return -1;
  const la_ssize_t r = archive_read_data(impl_->a, buf, n);
  if (r < 0) {
    error_ = archive_error_string(impl_->a) ? archive_error_string(impl_->a) : "damaged entry";
    return -1;
  }
  return r;
}

ArchiveReader::ReadFn ArchiveReader::entry_stream() {
  return [this](uint8_t* buf, size_t n) { return read(buf, n); };
}

std::string ArchiveReader::format() const {
  const char* f = impl_->a ? archive_format_name(impl_->a) : nullptr;
  return f ? f : "";
}

int64_t ArchiveReader::position() const { return impl_->a ? archive_filter_bytes(impl_->a, -1) : 0; }

#else  // no libarchive in this build

struct ArchiveReader::Impl {};
ArchiveReader::ArchiveReader() : impl_(new Impl) {}
ArchiveReader::~ArchiveReader() = default;
bool ArchiveReader::open_file(const std::string&) { return error_ = "this build reads no archives", false; }
bool ArchiveReader::open_stream(ReadFn) { return error_ = "this build reads no archives", false; }
bool ArchiveReader::open_iso_stream(ReadFn) { return error_ = "this build reads no archives", false; }
bool ArchiveReader::next(Entry*) { return false; }
int64_t ArchiveReader::read(uint8_t*, size_t) { return -1; }
ArchiveReader::ReadFn ArchiveReader::entry_stream() {
  return [](uint8_t*, size_t) { return int64_t{-1}; };
}
std::string ArchiveReader::format() const { return ""; }
int64_t ArchiveReader::position() const { return 0; }

#endif

ArchiveReader::ReadFn cooked_sectors(ArchiveReader::ReadFn raw, int offset) {
  struct State {
    ArchiveReader::ReadFn raw;
    int offset;
    uint8_t sector[2352];
    size_t have = 0, pos = 2048;  // the current sector's user bytes left: pos..2048
    bool end = false;
  };
  auto st = std::make_shared<State>();
  st->raw = std::move(raw), st->offset = offset;
  return [st](uint8_t* buf, size_t n) -> int64_t {
    size_t out = 0;
    while (out < n) {
      if (st->pos >= 2048) {
        if (st->end) break;
        size_t got = 0;
        while (got < 2352) {  // one whole raw sector
          const int64_t r = st->raw(st->sector + got, 2352 - got);
          if (r < 0) return -1;
          if (r == 0) break;
          got += static_cast<size_t>(r);
        }
        if (got < 2352) {  // a short tail is not a sector
          st->end = true;
          break;
        }
        st->pos = 0;
      }
      const size_t k = std::min(n - out, 2048 - st->pos);
      std::memcpy(buf + out, st->sector + st->offset + st->pos, k);
      st->pos += k, out += k;
    }
    return static_cast<int64_t>(out);
  };
}

}  // namespace mech3::source
