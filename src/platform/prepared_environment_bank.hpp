#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace platform::prepared::detail {
// Metadata-only validator for a local optional texture bank ("environment
// bank"). Mirrors the bounds semantics of native zimg::load/read_texture
// (header/table bounds, entry offset + palette reference, 16-byte texture
// header, full payload extent) without decoding pixels or allocating any
// pixel payload. Explicit little-endian reads, checked 64-bit arithmetic,
// visible std::runtime_error failures. No FP arithmetic, no whole-file reads.
inline void validate_environment_bank(const std::filesystem::path &bank_path,
                                      std::uint64_t expected_bytes,
                                      bool require_native_upload = true) {
  const std::string where = bank_path.u8string();

  // ---- explicit little-endian readers (host endianness independent) ----
  auto u16le = [](const std::uint8_t *p) -> std::uint16_t {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
  };
  auto u32le = [](const std::uint8_t *p) -> std::uint32_t {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
  };
  auto s16le = [&u16le](const std::uint8_t *p) -> std::int16_t {
    return static_cast<std::int16_t>(u16le(p));
  };
  auto s32le = [&u32le](const std::uint8_t *p) -> std::int32_t {
    return static_cast<std::int32_t>(u32le(p));
  };

  auto fail = [&where](const std::string &msg) -> void {
    throw std::runtime_error("validate_environment_bank: " + where + ": " +
                             msg);
  };

  // ---- checked 64-bit arithmetic: any overflow fails visibly ----
  auto checked_add = [&fail](std::uint64_t a, std::uint64_t b,
                             const char *what) -> std::uint64_t {
    if (a > std::numeric_limits<std::uint64_t>::max() - b)
      fail(std::string("64-bit add overflow: ") + what);
    return a + b;
  };
  auto checked_mul = [&fail](std::uint64_t a, std::uint64_t b,
                             const char *what) -> std::uint64_t {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a)
      fail(std::string("64-bit mul overflow: ") + what);
    return a * b;
  };

  std::ifstream file(bank_path, std::ios::binary);
  if (!file.is_open())
    fail("cannot open bank file");

  auto file_size_now = [&]() -> std::uint64_t {
    file.clear();
    file.seekg(0, std::ios::end);
    if (!file)
      fail("seek(end) failed");
    const std::streamoff off = file.tellg();
    if (off < 0)
      fail("tellg(end) failed");
    return static_cast<std::uint64_t>(off);
  };

  // Every access seeks and checks success; reads are chunked/bounded, never
  // whole-file.
  auto read_exact = [&](std::uint64_t at, std::uint8_t *dst, std::uint64_t n,
                        const std::string &what) -> void {
    if (n == 0)
      return;
    file.clear();
    file.seekg(static_cast<std::streamoff>(at), std::ios::beg);
    if (!file)
      fail(what + ": seek to " + std::to_string(at) + " failed");
    file.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(n));
    if (!file || static_cast<std::uint64_t>(file.gcount()) != n)
      fail(what + ": truncated/short read at " + std::to_string(at));
  };

  // Expected file size must hold before and after validation; byte domain is
  // INT32_MAX.
  const std::uint64_t file_size = file_size_now();
  if (file_size != expected_bytes)
    fail("file size " + std::to_string(file_size) + " != expected_bytes " +
         std::to_string(expected_bytes));
  if (file_size >
      static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
    fail("file size " + std::to_string(file_size) +
         " exceeds INT32_MAX byte domain");

  // ---- 24-byte file header ----
  if (file_size < 24)
    fail("truncated file header (< 24 bytes)");
  std::array<std::uint8_t, 24> hdr{};
  read_exact(0, hdr.data(), hdr.size(), "file header");
  // u32 zero00 at 0: not required to be zero (native does not check it).
  const std::uint32_t has_entries =
      u32le(hdr.data() + 4); // u32 at 4, must be 1
  const std::int32_t palette_count = s32le(hdr.data() + 8);  // s32 at 8
  const std::int32_t texture_count = s32le(hdr.data() + 12); // s32 at 12
  // reserved u32 at 16 and 20: not required to be zero.
  if (has_entries != 1)
    fail("has_entries " + std::to_string(has_entries) + " != 1");
  if (palette_count < 0)
    fail("negative palette_count " + std::to_string(palette_count));
  if (texture_count < 0)
    fail("negative texture_count " + std::to_string(texture_count));

  const std::uint64_t entry_table_bytes =
      checked_mul(static_cast<std::uint64_t>(texture_count), 40, "entry table");
  constexpr std::uint64_t kMaxOffsetMetadataBytes =
      64ull * 1024ull * 1024ull; // 64 MiB budget
  const auto offset_bytes =
      checked_mul(static_cast<std::uint64_t>(texture_count),
                  sizeof(std::uint32_t), "offset metadata");
  if (offset_bytes > kMaxOffsetMetadataBytes)
    fail("too many entries: offset metadata " + std::to_string(offset_bytes) +
         " bytes exceeds 64 MiB budget");
  const std::uint64_t table_size =
      checked_add(checked_add(24, entry_table_bytes, "table"),
                  checked_mul(static_cast<std::uint64_t>(palette_count), 512,
                              "palette table"),
                  "table");
  if (table_size > file_size)
    fail("table size " + std::to_string(table_size) +
         " extends past file size " + std::to_string(file_size));

  const std::uint64_t count = static_cast<std::uint64_t>(texture_count);

  // ---- pass 1: stream the entry table, validate fields, collect offsets ----
  std::vector<std::uint32_t> offsets;
  offsets.reserve(static_cast<std::size_t>(count));
  {
    constexpr std::uint64_t kChunkEntries = 1024; // 40 KiB per streamed read
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(kChunkEntries) * 40);
    std::uint64_t done = 0;
    while (done < count) {
      const std::uint64_t n = std::min(kChunkEntries, count - done);
      read_exact(checked_add(24, checked_mul(done, 40, "table offset"),
                             "table offset"),
                 buf.data(), checked_mul(n, 40, "table chunk"), "entry table");
      for (std::uint64_t k = 0; k < n; ++k) {
        const std::uint8_t *e = buf.data() + static_cast<std::size_t>(k) * 40;
        const std::uint64_t idx = done + k;
        // 40-byte entry: name[32] (not validated), offset u32 at 32, palette
        // s32 at 36.
        const std::uint32_t offset = u32le(e + 32);
        const std::int32_t palette = s32le(e + 36);
        if (palette < -1 || palette >= palette_count)
          fail("entry " + std::to_string(idx) + ": invalid palette reference " +
               std::to_string(palette) + " (palette_count " +
               std::to_string(palette_count) + ")");
        if (static_cast<std::uint64_t>(offset) < table_size ||
            static_cast<std::uint64_t>(offset) >= file_size)
          fail("entry " + std::to_string(idx) + ": offset " +
               std::to_string(offset) + " not in [table " +
               std::to_string(table_size) + ", file_size " +
               std::to_string(file_size) + ")");
        offsets.push_back(offset);
      }
      done += n;
    }
  }
  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
  // One bit per unique offset (at most 2 MiB under the offset metadata cap).
  // Aliases still pass every per-entry check below, but scan pixels only once.
  std::vector<bool> indices_valid(offsets.size(), false);

  // ---- pass 2: stream the table again; per-entry header + payload bounds ----
  {
    constexpr std::uint64_t kChunkEntries = 1024;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(kChunkEntries) * 40);
    std::uint64_t done = 0;
    while (done < count) {
      const std::uint64_t n = std::min(kChunkEntries, count - done);
      read_exact(checked_add(24, checked_mul(done, 40, "table offset"),
                             "table offset"),
                 buf.data(), checked_mul(n, 40, "table chunk"),
                 "entry table (pass 2)");
      for (std::uint64_t k = 0; k < n; ++k) {
        const std::uint8_t *e = buf.data() + static_cast<std::size_t>(k) * 40;
        const std::uint64_t idx = done + k;
        const std::uint32_t offset = u32le(e + 32);
        const std::int32_t entry_palette = s32le(e + 36);

        // Payload ends at the minimum strictly greater entry offset, else EOF.
        // Offsets need not be sorted in the table; aliases/equal offsets are
        // valid.
        const auto next =
            std::upper_bound(offsets.begin(), offsets.end(), offset);
        const std::uint64_t end = (next != offsets.end())
                                      ? static_cast<std::uint64_t>(*next)
                                      : file_size;
        if (next == offsets.begin() || *(next - 1) != offset)
          fail("Entry offset changed during validation");
        const auto offset_index =
            static_cast<std::size_t>(next - offsets.begin() - 1);

        // 16-byte texture header at the entry offset.
        std::array<std::uint8_t, 16> th{};
        read_exact(offset, th.data(), th.size(),
                   "entry " + std::to_string(idx) + " texture header");
        const std::uint8_t flags = th[0];                 // byte 0
        const std::int16_t width = s16le(th.data() + 4);  // s16 at 4
        const std::int16_t height = s16le(th.data() + 6); // s16 at 6
        const std::int16_t local_palette_count =
            s16le(th.data() + 12); // s16 at 12
        if (width <= 0 || height <= 0 || local_palette_count < 0)
          fail("entry " + std::to_string(idx) +
               ": negative dimension/count (w " + std::to_string(width) +
               ", h " + std::to_string(height) + ", lpc " +
               std::to_string(local_palette_count) + ")");

        // Selected packs must not silently fall back in zvid::create_texture.
        // False is for structural validation; selected packs use the default.
        if (require_native_upload) {
          if (local_palette_count != 0)
            fail("Indexed environment textures are not supported by the native "
                 "uploader");
          if (width > 4096 || height > 4096 || width > 8 * height ||
              height > 8 * width)
            fail("Environment texture exceeds native upload dimension or "
                 "aspect limits");
        }
        const std::uint64_t w = static_cast<std::uint64_t>(width);
        const std::uint64_t h = static_cast<std::uint64_t>(height);
        const std::uint64_t lpc =
            static_cast<std::uint64_t>(local_palette_count);
        const std::uint64_t bpp =
            (flags & 0x01u) ? 2u : 1u; // flags&1: two bytes per pixel, else 1
        // Mip linking divides by width; direct uploads read uint16_t texels.
        // Indexed paths need an actual palette with correctly sized colours.
        if (lpc == 0) {
          if (bpp != 2 || entry_palette != -1)
            fail("Direct texture requires 16-bit texels and no palette "
                 "reference");
        } else {
          if (flags & 0x10U) {
            if (entry_palette < 0)
              fail("Global indexed texture has no global palette");
          } else if (entry_palette != -1 || bpp != 2) {
            fail("Local indexed texture requires 16-bit palette colours and no "
                 "global reference");
          }
        }
        const std::uint64_t px = checked_mul(w, h, "pixel count");
        // Required payload bytes, matching native read_texture:
        // (lpc != 0 ? px : bpp*px) + (flags&8 ? px : 0) + (!(flags&16) && lpc
        // != 0 ? bpp*lpc : 0)
        std::uint64_t need =
            (lpc != 0) ? px : checked_mul(bpp, px, "pixel bytes");
        if (flags & 0x08u)
          need = checked_add(need, px, "full-alpha bytes");
        if (!(flags & 0x10u) && lpc != 0)
          need = checked_add(need, checked_mul(bpp, lpc, "local palette bytes"),
                             "local palette bytes");

        const std::uint64_t payload_end = checked_add(
            checked_add(offset, 16, "texture header"), need, "texture payload");
        if (payload_end > end)
          fail("entry " + std::to_string(idx) + ": texture spans [" +
               std::to_string(offset) + ", " + std::to_string(payload_end) +
               ") but its payload ends at " + std::to_string(end));
        if (lpc > 0 && lpc < 256 && !indices_valid[offset_index]) {
          std::array<std::uint8_t, 64 * 1024> indices{};
          for (std::uint64_t at = 0; at < px;) {
            const auto count = std::min<std::uint64_t>(indices.size(), px - at);
            read_exact(offset + 16 + at, indices.data(), count,
                       "palette indices");
            for (std::size_t i = 0; i < count; ++i)
              if (indices[i] >= lpc)
                fail("Texture palette index exceeds declared colours");
            at += count;
          }
          indices_valid[offset_index] = true;
        }
      }
      done += n;
    }
  }

  // Expected file size must still hold after validation.
  const std::uint64_t file_size_after = file_size_now();
  if (file_size_after != expected_bytes || file_size_after != file_size)
    fail("file size changed during validation (" +
         std::to_string(file_size_after) +
         " != " + std::to_string(expected_bytes) + ")");
}
} // namespace platform::prepared::detail
