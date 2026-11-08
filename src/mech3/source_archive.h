#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Archives as sources for SETUP's "Point to your game" (ours, not original
// code; docs/setup-sources.md): zip (zip64, deflate64), 7z (LZMA, LZMA2,
// BCJ, BCJ2, PPMd), RAR4 and RAR5, tar and its compressions, and ISO 9660,
// through libarchive (its own readers, no unRAR code). Forward only: each
// entry's data is read once, in order, as libarchive streams it, so an
// archive inside an archive opens straight from its parent's entry with
// nothing unpacked to disk.
namespace mech3::source {

// Whether this build reads archives (libarchive found at build time).
bool archives_supported();

// A file name an archive is likely to hide under (by its extension):
// .zip .7z .rar .tar .tgz .tar.gz .tar.xz .gz .xz .iso. Discs and their
// images are judged by content elsewhere; this only says what to look into.
bool archive_name(const std::string& name);

class ArchiveReader {
 public:
  struct Entry {
    std::string path;   // '/'-separated, as stored
    int64_t size = -1;  // -1 when the archive does not say (a stream)
    bool dir = false;
    bool special = false; // links/devices are never valid managed package payloads
  };
  // Reads up to `n` bytes into `buf`: the count, 0 at the end, -1 on an error.
  using ReadFn = std::function<int64_t(uint8_t* buf, size_t n)>;

  ArchiveReader();
  ~ArchiveReader();
  ArchiveReader(const ArchiveReader&) = delete;
  ArchiveReader& operator=(const ArchiveReader&) = delete;

  // A file on disk (seekable: zip and 7z list their contents from their
  // tables without reading the data).
  bool open_file(const std::string& path);
  // A stream: another archive's entry (read through `read` once, in order).
  bool open_stream(ReadFn read);
  // `iso` only: an ISO 9660 image's 2048-byte sectors (a raw .bin through
  // CookedSectors, or an .iso inside an archive).
  bool open_iso_stream(ReadFn read);

  // The next entry; false at the end or on an error (error() then says).
  bool next(Entry* out);
  // The current entry's data, in order: as ReadFn.
  int64_t read(uint8_t* buf, size_t n);
  // A ReadFn over the current entry, for an archive inside it.
  ReadFn entry_stream();

  const std::string& error() const { return error_; }
  std::string format() const;  // "ZIP 2.0 (deflation)", "7-Zip", "RAR5", "ISO9660", ...
  // The bytes read so far of what was opened (a file's: its place in the
  // file), for progress.
  int64_t position() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string error_;
};

// A raw CD data track (2352-byte MODE1 or MODE2/form 1 sectors, as a .bin
// holds them) read as its 2048-byte user data, for ArchiveReader's ISO
// reader. `read` is the raw track; `offset` the first byte of the user data
// in a sector (16 for MODE1/2352, 24 for MODE2/2352).
ArchiveReader::ReadFn cooked_sectors(ArchiveReader::ReadFn raw, int offset = 16);

}  // namespace mech3::source
