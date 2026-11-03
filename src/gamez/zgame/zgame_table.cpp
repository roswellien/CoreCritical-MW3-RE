#include "gamez/zgame/zgame_table.h"

#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_zbd.h"
#include "gamez/zclass/light.h"
#include "gamez/zeffect/zeff_anim_init.h"

#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/zutl_zar.h"

namespace zgame {
namespace {
const Table kMw3 = {"MechWarrior 3", "Mech3.exe 1.2", zutl_zar::read_table, zutl_zar::write_table, zutl_zar::length_span,
                    zrdr::parse_text, zcls::read_zbd, zcls::mw3_node_alloc,
                    zcls::mw3_node_set_active, zcls::mw3_light_create, zeff::mw3_anim_read_zbd,
                    // zsound (pm-map step 7a)
                    zsnd::mw3_load, zsnd::mw3_shutdown, zsnd::mw3_load_set, zsnd::mw3_unload_set,
                    zsnd::mw3_find_set, zsnd::mw3_unload_all, zsnd::mw3_free_sets, zsnd::mw3_queue_push,
                    zsnd_cd::mw3_notify, zsnd::mw3_resume_snapshot};
const Table* g_game = &kMw3;
}  // namespace

const Table& mw3_table() { return kMw3; }
const Table& game() { return *g_game; }
void set_game(const Table& t) { g_game = &t; }

}  // namespace zgame
