// C:\zipper\branchep1\gamez\zSound\zsnd_cd.cpp: the CD's notify, Pirate's
// Moon's. The rest of the file is MW3's (src/gamez/zsound/zsnd_cd).
#include "gamez/zsound/zsnd_cd.h"
#include "pm/gamez/zsound/zsnd.h"

namespace pm::zsnd {

// Pirate's Moon 1.0 @ 0x005938c0 (status, device): MW3's (0x00572bf0) for
// modes 5 and 6, and mode 3 (play to the disc's end) starts the disc again
// from track 1, whatever the status; after a successful end the position is
// forgotten first, so the same request plays again.
void cd_notify(int status) {
  zsnd_cd::State& cd = zsnd_cd::state();
  if (!(cd.flags & 2)) return;
  if (cd.mode == zsnd_cd::Loop || cd.mode == zsnd_cd::Restart) {
    if (status == zsnd_cd::kNotifySuccessful) zsnd_cd::play_track(cd.track, zsnd_cd::Restart);
  } else if (cd.mode == 3) {
    if (status == zsnd_cd::kNotifySuccessful) zsnd_cd::clear_position();  // 0x005935b0
    zsnd_cd::play_track(1, 3);
  }
}

}  // namespace pm::zsnd
