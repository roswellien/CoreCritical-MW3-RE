// C:\zipper\branchep1\gamez\zSound\zsnd_play.cpp: the voice queue, Pirate's
// Moon's (docs/sound.md, Pirate's Moon). The rest of the file is MW3's.
#include <cstring>
#include <iterator>
#include <list>

#include "pm/gamez/zsound/zsnd.h"

namespace pm::zsnd {

// Pirate's Moon 1.0 @ 0x0058fb60 (sound, volume, position, velocity, start):
// MW3's queue (0x0056e750) played the newest line first; PM's plays the
// oldest, a line going before the first one queued with a higher QPRIORITY
// number (0x0058fc20, 0x0058fc40). One that goes before the very first is
// put last instead: the list insert (0x00577950) leaves the head where it
// was. The shipped sounds.zrd sets no QPRIORITY, so every line is in order.
// Pirate's Moon 1.0 @ 0x00590530: each record's voice again (0x00590310),
// if it still has a buffer, with the master volume's change since.
int32_t resume_snapshot(::zsnd::Snapshot* l) {
  const float delta = ::zsnd::state().master() - l->front().master;
  for (auto it = std::next(l->begin()); it != l->end(); ++it)
    if (it->voice->buffer) ::zsnd::resume_voice(it->sound, it->voice, 0, static_cast<int32_t>(delta * 10000.0f));
  return 1;
}

void queue_push(Sound* s, const float* pos, float volume, const float* vel, uint32_t start) {
  ::zsnd::State& st = ::zsnd::state();
  auto* q = new ::zsnd::Queued;  // malloc(0x30)
  q->sound = s;
  q->volume = volume;
  q->has_pos = pos != nullptr;
  q->has_vel = vel != nullptr;
  if (pos) std::memcpy(q->pos, pos, 12);
  if (vel) std::memcpy(q->vel, vel, 12);
  q->start = start;
  q->expires = st.queue_clock + s->queue_life;
  std::list<::zsnd::Queued*>& l = *st.queue;
  const uint32_t mine = s->flags & ::zsnd::kQPriority;
  auto it = l.begin();
  while (it != l.end() && !(mine < ((*it)->sound->flags & ::zsnd::kQPriority))) ++it;
  if (it == l.begin()) l.push_back(q);  // 0x00577900, or before the head: the same place
  else l.insert(it, q);
}

}  // namespace pm::zsnd
