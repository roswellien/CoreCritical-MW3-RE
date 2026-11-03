// C:\zipper\GameZMech3\zSound\zsnd_play.cpp: playing, stopping, the voice
// queue, cue points and pause (docs/sound.md section 3). The file starts at
// 0x0056dfb0, right after zRender's span fillers.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

#include "gamez/zerror/zerr.h"
#include "gamez/zgame/zgame_table.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/ztimer.h"

namespace zsnd {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zSound\\zsnd_play.cpp";
using SO = platform::SoundOutput;
platform::SoundOutput* out() { return hooks().output; }

// 0x0056e2d0 (0x0056e160's DirectSound branch): a voice of the sound that is
// not playing and not locked; a new duplicate while fewer than 7; else 0.
// FIX (bug): a new duplicate's fields are zeroed; the exe's malloc leaves
// them as the heap had them (has_pos then decides whether a later volume
// change places the sound in 3D).
Voice* free_voice(Sound* s) {
  if (!s->voice.buffer) return nullptr;
  uint32_t st = 0;
  if (!s->voice.locked) {
    out()->get_status(s->voice.buffer, &st);
    if (!(st & SO::kStatusPlaying)) return &s->voice;
  }
  for (Voice* v : s->dups) {
    if (v->locked) continue;
    st = 0;
    out()->get_status(v->buffer, &st);
    if (!(st & SO::kStatusPlaying)) return v;
  }
  if (static_cast<int>(s->dups.size()) >= state().max_duplicates) return nullptr;
  auto* v = new Voice;
  if (out()->duplicate_buffer(s->voice.buffer, &v->buffer) != 0) {
    delete v;
    return nullptr;
  }
  s->dups.push_back(v);
  return v;
}

// 0x0056e610 (ECX sound, EDX volume in dB/100, position, velocity, start).
// With every voice busy, the sound's own voice is restarted.
Voice* play_ds(Sound* s, int32_t db, const float* pos, const float* vel, uint32_t start) {
  State& st = state();
  if (s->kind != 0) return nullptr;
  if ((s->flags & kVoice) && !st.voices_on) return nullptr;
  Voice* v = free_voice(s);
  if (!v || !v->buffer) v = &s->voice;
  v->kind = 0;
  v->sound = s;
  uint32_t status = 0;
  out()->get_status(v->buffer, &status);
  if (status & SO::kStatusLost) out()->restore(v->buffer);
  v->volume = db;
  if (pos) {
    if (update_3d(v, pos, vel, 0) == 0 && !(s->flags & kLooped)) return nullptr;
  } else {
    out()->set_volume(v->buffer, muted() ? -10000 : v->volume);
  }
  out()->set_current_position(v->buffer, start);
  if (s->cue_count && s->cue_fn) {
    const float now = ztimer::state().total_raw;
    for (uint32_t i = 0; i < s->cue_count; ++i) s->cue_at[i] = s->cue_times[i] + now - s->cue_offset;
    st.cue_sound = s;
    st.cue_voice = v;
  }
  const int32_t hr = out()->play(v->buffer, s->flags & kLooped);
  if (hr != 0) ds_error(hr, kFile, 0x2d0);
  return v;
}

// 0x0056e480 (ECX sound, EDX position, volume, velocity, start).
Voice* play_raw(Sound* s, const float* pos, float volume, const float* vel, uint32_t start) {
  return play_ds(s, volume_db(volume), pos, vel, start);
}

}  // namespace

void queue_push(Sound* s, const float* pos, float volume, const float* vel, uint32_t start) {
  zgame::game().zsnd_queue_push(s, pos, volume, vel, start);
}

// Mech3.exe 1.2 @ 0x0056e750 (ECX sound, EDX position, volume, velocity,
// start): a QUEUE line waits, newest first, until its QUEUE seconds run out.
void mw3_queue_push(Sound* s, const float* pos, float volume, const float* vel, uint32_t start) {
  State& st = state();
  auto* q = new Queued;
  q->volume = volume;
  q->sound = s;
  q->has_pos = pos != nullptr;
  q->has_vel = vel != nullptr;
  if (pos) std::memcpy(q->pos, pos, 12);
  if (vel) std::memcpy(q->vel, vel, 12);
  q->start = start;
  q->expires = s->queue_life + st.queue_clock;
  st.queue->push_front(q);  // 0x00555ec0
}

namespace {

// 0x0056ee20 (ECX the record, EDX voice): fills the record only for a sound's
// voice; the record keeps what it held otherwise.
void record_voice(Record& r, Voice* v) {
  if (v->kind != 0) return;
  r = Record{};
  r.voice = v;
  r.sound = v->sound;
  r.volume = v->sound->voice.volume;  // the sound's own voice's, also for a duplicate
  if (v->has_pos) {
    r.flags |= 1;
    std::memcpy(r.pos, v->pos, 12);
    std::memcpy(r.vel, v->vel, 12);
  }
}

}  // namespace

// 0x0056f010 (ECX sound + 4, EDX voice, restart, volume change).
void resume_voice(Sound* s, Voice* v, int32_t restart, int32_t delta) {
  int32_t hr;
  if (delta) {
    v->volume = v->volume + delta;
    hr = out()->set_volume(v->buffer, v->volume);
    if (hr != 0) ds_error(hr, kFile, 0x58e);
  }
  if (restart) {
    hr = out()->set_current_position(v->buffer, 0);
    if (hr != 0) ds_error(hr, kFile, 0x596);
  }
  hr = out()->play(v->buffer, s->flags & kLooped);
  if (hr != 0) ds_error(hr, kFile, 0x59d);
}

namespace {

// 0x0056eea0 (ECX sound, EDX restart, the master volume's change): plays the
// sound's own voice again and every duplicate not at 0 dB, whether they were
// playing or not.
void resume_sound(Sound* s, int32_t restart, float delta) {
  const int32_t ddb = static_cast<int32_t>(delta * 10000.0f);
  if (!s->voice.buffer) return;
  resume_voice(s, &s->voice, restart, ddb);
  for (Voice* d : s->dups)
    if (d && d->volume != 0) resume_voice(s, d, restart, ddb);
}
}  // namespace

// Mech3.exe 1.2 @ 0x0056dfb0 (ECX the A3D flush, EDX advance the queue). Once
// a frame: fades, the cue points of the sound followed, the voice queue (one
// line at a time, half a second apart), the groups.
void frame(int32_t /*a3d_flush*/, int32_t advance) {
  State& st = state();
  const ztimer::State& t = ztimer::state();
  fades_update(t.dt);
  if (Sound* s = st.cue_sound) {
    if (s->cue_at.empty()) {
      st.cue_index = 0, st.cue_sound = nullptr, st.cue_limit = 999;
      return;
    }
    // FIX (A): after the last cue (the sound's end) the index moves one
    // past the array and the exe compares with whatever follows it on the
    // heap; that reads as 0 here, so the extra call (index == count) comes on
    // the next frame, then the walk stops (docs/sound.md, cue points).
    const float at = st.cue_index < s->cue_at.size() ? s->cue_at[st.cue_index] : 0.0f;
    if (!(t.total_raw < at)) {
      if (s->cue_fn) s->cue_fn(st.cue_index, s);
      Sound* now = st.cue_sound;
      if (!(st.cue_index < now->cue_count)) {
        st.cue_index = 0, st.cue_sound = nullptr, st.cue_limit = 999;
      } else if (!(st.cue_index < st.cue_limit)) {
        stop(st.cue_voice);
        st.cue_limit = 999;
      } else {
        st.cue_index += 1;
      }
    }
  }
  if (advance) {
    st.queue_clock = st.queue_clock + t.dt;
    if (st.queued) {
      if (status(st.queued) == 0) {
        st.queued = nullptr;
        st.queue_next = st.queue_clock - -0.5f;
        groups_update();
        return;
      }
    } else if (!(st.queue_clock < st.queue_next)) {
      while (st.queue && !st.queue->empty()) {
        Queued* q = st.queue->front();
        st.queue->pop_front();
        if (st.queue_clock < q->expires)
          st.queued = play_raw(q->sound, q->has_pos ? q->pos : nullptr, q->volume, q->has_vel ? q->vel : nullptr,
                               q->start);
        delete q;
        if (st.queued) break;
      }
    }
  }
  groups_update();
}

// Mech3.exe 1.2 @ 0x0056e3d0 (ECX sound or group, the volume 0..1).
VoiceBase* play(Object* obj, float volume) {
  State& st = state();
  if (!st.open || !st.initialized || !obj) return nullptr;
  if (obj->kind == 1) return play_group(static_cast<Group*>(obj), 0, volume, nullptr, nullptr);  // 0x00574450
  return play_at(obj, nullptr, volume, nullptr);
}

// Mech3.exe 1.2 @ 0x0056e410: linear volume to hundredths of a dB, as
// 1000 log2(v): 10 dB for each halving, not DirectSound's 6.
int32_t volume_db(float v) {
  if (!(v < 1.0f)) return 0;
  if (!(v > 0.0009765625f)) return -10000;
  const float t = static_cast<float>(1000.0 * std::log2(static_cast<double>(v)));
  return static_cast<int32_t>(t - 0.5f);
}

// Mech3.exe 1.2 @ 0x0056e7e0 (ECX sound or group, EDX position or null, the
// volume, the velocity or null).
VoiceBase* play_at(Object* obj, const float* pos, float volume, const float* vel) {
  State& st = state();
  if (!st.open || !st.initialized || !obj) return nullptr;
  if (obj->kind == 1) return play_group(static_cast<Group*>(obj), 1, volume, pos, vel);  // 0x005745f0
  auto* s = static_cast<Sound*>(obj);
  if (!(s->flags & kLoaded)) return nullptr;
  s->cue_offset = 0.0f;
  if (st.can_play_hook && !st.can_play_hook(s->subtitle)) return nullptr;
  const float v = s->volume * st.master() * volume;
  if (s->flags & kQueue) {
    if (s->flags & kLooped) {
      char msg[128];
      std::snprintf(msg, sizeof msg, "%s: queued sound is also looped.", s->name ? s->name : "(null)");
      zerr::report(0x200, kFile, 0x365, msg);
    }
    queue_push(s, pos, v, vel, 0);
    return nullptr;
  }
  return play_raw(s, pos, v, vel, 0);
}

// Mech3.exe 1.2 @ 0x0056e8e0: 1 playing, 0 not, -1 for no voice (or the
// sound system closed).
int32_t status(VoiceBase* voice) {
  State& st = state();
  if (!st.open || !st.initialized || !voice) return -1;
  if (voice->kind == 1) return group_status(static_cast<GroupVoice*>(voice));
  auto* v = static_cast<Voice*>(voice);
  if (!v->buffer) return 0;
  uint32_t s = 0;
  int32_t hr = out()->get_status(v->buffer, &s);
  if (hr != 0) return ds_error(hr, kFile, 0x3bb);
  if (s == SO::kStatusLost) {
    hr = out()->restore(v->buffer);
    if (hr != 0) return ds_error(hr, kFile, 0x3bf);
  }
  hr = out()->get_status(v->buffer, &s);
  if (hr != 0) return ds_error(hr, kFile, 0x3c4);
  return static_cast<int32_t>(s & 1);
}

// Mech3.exe 1.2 @ 0x0056e9f0: 0 when stopped.
int32_t stop(VoiceBase* voice) {
  State& st = state();
  if (!st.open || !st.initialized || !voice) return -1;
  if (voice->kind == 1) {  // 0x005743f0
    auto* g = static_cast<GroupVoice*>(voice);
    if (!st.playing) return 0;
    for (GroupVoice* p : *st.playing)
      if (p == g) {
        for (VoiceBase* v : g->voices)  // 0x00574230
          if (status(v) != 0) stop(v);
        g->state = 4;
        return 1;
      }
    return 0;
  }
  auto* v = static_cast<Voice*>(voice);
  if (v->sound == st.cue_sound) st.cue_index = 0, st.cue_sound = nullptr;
  if (!v->buffer) return -1;
  uint32_t s = 0;
  int32_t hr = out()->get_status(v->buffer, &s);
  if (hr != 0) return ds_error(hr, kFile, 0x3f8);
  if (s == SO::kStatusLost) {
    hr = out()->restore(v->buffer);
    if (hr != 0) return ds_error(hr, kFile, 0x3fc);
  }
  hr = out()->stop(v->buffer);
  if (hr != 0) return ds_error(hr, kFile, 0x400);
  return 0;
}

// Mech3.exe 1.2 @ 0x0056eb10: pause keeps the voices playing now.
Snapshot* snapshot() {
  State& st = state();
  auto* l = new Snapshot;
  Record r;
  r.master = st.master();
  l->push_back(r);
  for (int32_t i = 0; i < set_count(); ++i) {
    Set* set = set_at(i);
    for (Sound& s : set->sounds) {
      uint32_t status_ = 0;
      if (s.voice.buffer) {
        out()->get_status(s.voice.buffer, &status_);
        if (status_ & SO::kStatusPlaying) record_voice(r, &s.voice), l->push_back(r);
      }
      for (Voice* d : s.dups) {
        if (!d) continue;
        status_ = 0;
        out()->get_status(d->buffer, &status_);
        if (status_ & SO::kStatusPlaying) record_voice(r, d), l->push_back(r);
      }
    }
  }
  return l;
}

// Mech3.exe 1.2 @ 0x0056f0a0: stops the voices kept, if still playing.
int32_t stop_snapshot(Snapshot* l) {
  for (auto it = std::next(l->begin()); it != l->end(); ++it) {
    uint32_t s = 0;
    out()->get_status(it->voice->buffer, &s);
    if (s & SO::kStatusPlaying) stop(it->voice);
  }
  return 1;
}

// Mech3.exe 1.2 @ 0x0056f130: back from pause. Each kept voice's sound plays
// all its voices again (docs/sound.md, pause), with the master volume's
// change since added to each.
// FIX (bug): only the voices that were playing resume, each once and from where
// it stopped (the exe replays every voice of the sound, finished ones from the start).
int32_t resume_snapshot(Snapshot* l) { return zgame::game().zsnd_resume_snapshot(l); }

int32_t mw3_resume_snapshot(Snapshot* l) {
  const float delta = state().master() - l->front().master;
  if (zfix::options().bugs) {
    std::vector<Voice*> done;
    for (auto it = std::next(l->begin()); it != l->end(); ++it) {
      if (!it->voice || std::find(done.begin(), done.end(), it->voice) != done.end()) continue;
      done.push_back(it->voice);
      resume_voice(it->sound, it->voice, 0, static_cast<int32_t>(delta * 10000.0f));
    }
    return 1;
  }
  for (auto it = std::next(l->begin()); it != l->end(); ++it) resume_sound(it->sound, 0, delta);
  return 1;
}

// Mech3.exe 1.2 @ 0x0056f190
int32_t free_snapshot(Snapshot* l) {
  delete l;
  return 1;
}

// Mech3.exe 1.2 @ 0x0056f210
bool muted() {
  const State& st = state();
  return st.initialized && st.muted > 0;
}

// Mech3.exe 1.2 @ 0x0056f260
float set_volume(float v) {
  state().master() = v;
  return v;
}

}  // namespace zsnd
