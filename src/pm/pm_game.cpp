#include "pm/pm_game.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "pm/gamez/zclass/cls_node.h"
#include "pm/gamez/zclass/cls_zbd.h"
#include "pm/gamez/zclass/light.h"
#include "pm/gamez/zeffect/zeff_anim_init.h"
#include "pm/gamez/zreader/zrdr_main.h"
#include "pm/gamez/zutil/zutl_zar.h"
#include "pm/gamez/zsound/zsnd.h"

namespace pm {
namespace {
const zgame::Table kPm = {"Pirate's Moon", "Pirate's Moon 1.0", zar::read_table, zar::write_table, zar::span,
                         zrdr::parse_text, zcls::read_zbd, zcls::node_alloc, zcls::node_set_active,
                         zcls::light_create, zeff::anim_read_zbd,
                         // zsound (pm-map step 7a)
                         zsnd::load, zsnd::shutdown, zsnd::load_set, zsnd::unload_set, zsnd::find_set,
                         zsnd::unload_all, zsnd::free_sets, zsnd::queue_push, zsnd::cd_notify,
                         zsnd::resume_snapshot};
}  // namespace

const zgame::Table& table() { return kPm; }

// MW3's archive ends "version (1), count"; PM's "count, checksum" after a
// version word of 2 (docs/pm-map.md, zArchive). Only the last 12 bytes are read.
Install install_kind(const std::string& dir) {
  const std::filesystem::path p = std::filesystem::path(dir) / "zbd" / "reader.zbd";
  std::FILE* f = std::fopen(p.string().c_str(), "rb");
  if (!f) return Install::kNone;
  uint8_t tail[12] = {};
  const bool ok = std::fseek(f, -12, SEEK_END) == 0 && std::fread(tail, 1, 12, f) == 12;
  std::fclose(f);
  if (!ok) return Install::kUnknown;
  uint32_t v8, v12;
  std::memcpy(&v8, tail + 4, 4);
  std::memcpy(&v12, tail, 4);
  if (v8 == 1) return Install::kMw3;
  if (v12 == 2) return Install::kPm;
  return Install::kUnknown;
}

const char* install_words(Install k) {
  switch (k) {
    case Install::kNone: return "no zbd/reader.zbd: not a game install";
    case Install::kMw3: return "MechWarrior 3, not Pirate's Moon";
    case Install::kPm: return "Pirate's Moon";
    case Install::kUnknown: return "a zbd/reader.zbd neither game's";
  }
  return "";
}

}  // namespace pm
