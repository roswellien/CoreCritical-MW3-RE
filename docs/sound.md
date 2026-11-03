# zSound: sound effects, voices and 3D sound

Mech3.exe 1.2, 2026-10-20. Port: `src/gamez/zsound/zsnd_*.cpp` (header
`zsnd.h`), the DirectSound stand-in `src/platform/sound_output.h`, its SDL3
implementation `src/frontend/sdl/sdl_sound.*`, the sound nodes in
`src/gamez/zclass/sound.cpp`. Check: `tests/differential/test_sound.py`
(probe `tests/probe_sound.cpp`). The CD music is `zsnd_cd` (its own header,
ported before; the game asks for track N, the music folder plays it).

## 0. Map

| Range | File (func2file / strings) | What |
|---|---|---|
| 0x56dfb0..0x56f260 | zsnd_play.cpp (string at 0x5efbb0) | frame, play, voices, status, stop, pause snapshot, master volume |
| 0x56f280 | zsnd_parm.cpp | set_frequency |
| 0x56f370..0x5700a0 | (no string; kept in zsnd_parm.cpp) | voice volume, locks, user data, sets: list, load, unload, find |
| 0x5700f0..0x571460 | zsnd_init.cpp | init, primary format, the voice queue's stop, shutdown, sounds.zrd, DirectSound open |
| 0x5715c0..0x5723d0 | zsnd_3d.cpp (string at 0x5efda0, used by 0x572050) | device close, mission reset, fades, listener, 3D placement, speed of sound |
| 0x572450..0x5731f0 | zsnd_cd.cpp | CD music (ported earlier), aux / mixer CD volume |
| 0x573270..0x574610 | zsnd_grp.cpp (readers 0x573560, 0x5739f0) | sound groups: lists, reader, player |
| 0x574660..0x575980 | zsnd_create.cpp (0x5752e0) | error names, WAV reader, buffer creation, release, the movie stream |

Callers outside zSound (the API): play 0x56e3d0 (30 callers), play_at
0x56e7e0 (weapons, effects, mechs, sound nodes), status 0x56e8e0, stop
0x56e9f0 (40), find 0x56f6f0 (34), set_frequency 0x56f280 (weapons
0x581230 / 0x5814b0), set_voice_volume 0x56f370, update_3d 0x571f30 (mech
0x419fc0, 0x4adc30/60, sound nodes), fade 0x571d20 and play_fading
0x571cc0, listener 0x571e50 and speed 0x572360 (the camera 0x4f4370), the
frame 0x56dfb0 (Core state, briefing 0x405230, 0x48ed30), pause 0x56eb10 /
0x56f0a0 / 0x56f130 / 0x56f190 with the group stash 0x5732f0 / 0x573300 /
0x573350 (0x492750 pause, 0x492d70 resume, 0x481fc0 mission leave), sets by
name 0x56f520 / 0x56f530, init 0x5700f0 / load 0x5705e0 (0x480570, and
0x409440 for "briefing sounds"), shutdown 0x5715d0 then 0x570510, reset
0x5716a0, cue callback 0x56f3e0 (briefings 0x405e10), the master volume
0x56f260 (the SoundVolume option), archives on/off 0x56f4b0 (0x480570),
user data 0x56f430 and name 0x56f740.

Two APIs are compiled in: [0x72c2cc] 0 DirectSound, 1 Aureal A3D
(CoCreateInstance, its own error names 0x5746b0). Only 0x570625 writes the
selector, and only to 0 when A3D fails to open, so A3D is never used; its
branches are not ported. The syntax 1 reader of sounds.zrd (0x5706f0) is
not ported either: the shipped file is SYNTAX 2.

## 1. Start-up and sounds.zrd

`init` 0x5700f0 (ECX window) binds three engine variables through 0x597d10
(the named-variable list at 0x731910): SoundLOD (0 soundsH, 1 soundsM,
2 soundsL), MuteSound (copied once, to [0x72c494]: muted while > 0) and
SoundVolume (the master volume, linear). It also sets the CD's play mode to 2.

`load` 0x5705e0 (ECX window, EDX "sounds.zrd", the load callback, the demo
flag) opens DirectSound (0x571460: DirectSoundCreate, cooperation NORMAL,
GetCaps, a primary buffer with flags 0xc1, its format), reads the file
(0x556740) and, for SYNTAX 2, 0x570af0:

- CD_TRACKS to the CD (0x572450), SOUND_PATH to a search list (0x576100,
  LIFO: the last directory is searched first; the shipped
  `..\data\common\sounds;zbd` finds only `zbd`).
- HIGH_DEFAULT / MEDIUM_DEFAULT / LOW_DEFAULT: each sound's three formats
  (read, never used). SPEED_OF_SOUND (default 343).
- PLAYBACK_FORMAT: HIGH, "MED" (never matches the data's MEDIUM) and LOW, as
  {rate, bits, 1}: the channel count in the data is not read; 1 is stored.
- SETS: name / list pairs (DEMO_SETS only, in a demo). A set is 0x18 bytes
  (name, count, 0xd0-byte sounds, loaded, subsets). SUB_SET names earlier
  sets (a later one would not be found; none is). Each sound list
  `("name" "file.wav" KEYWORDS...)`: 3D, LOOPED, FREQUENCY, HARDWARE,
  PURGEABLE, VOICE, QUEUE [(seconds)] (default 5), VOLUME (clamped 0..1),
  A3DDIST, RANGE (min max, raw floats; makes it 3D; default 50 400), and
  per-sound HIGH/MED/LOW. Keywords are found anywhere in the list
  (zrdr::find), the name and file are the reader's own strings.
- SOUND_GROUPS (DEMO_GROUPS in a demo), section 6.

The load callback (MW3: 0x4e6b00) gets each sound (ECX) with its list (EDX);
its result is the sound's user data (+0xcc). The 0x72c4cc / 0x72c4d0 hooks
(subtitles, a play veto) are never set.

## 2. Sets: loading and finding

`load_set` 0x56fbf0: opens the SoundLOD archive, or the next one that opens
(H, M, L, wrapping); soundsM.zbd is not shipped, so medium loads soundsL.
Each sound not loaded yet: the WAV from the archive (0x574f50), a buffer
(0x5752e0). The primary buffer then takes that archive's PLAYBACK_FORMAT
(0x5702a0: SetCooperativeLevel EXCLUSIVE, SetFormat, NORMAL). Sounds still
missing are looked for as loose files on the search list. Then the subsets.

Buffers (0x5752e0): CTRLVOLUME always, CTRLPAN for 3D sounds, CTRLFREQUENCY
for FREQUENCY (none in the data), LOCSOFTWARE for PURGEABLE, else STATIC for
HARDWARE, GETCURRENTPOSITION2 for streams; the WAV copied in through
Lock/Unlock; SetCurrentPosition 0. The WAV's "cue " chunk becomes cue times
(position / rate) and byte offsets, plus the end as a last cue (0x5751e0).

`unload_set` 0x56fe00 drops the set's queued lines and releases every buffer
(duplicates freed), its subsets too, even when another loaded set shares
them. `find` 0x56f6f0: the first set (in load order) with a sound of that
name whose buffer exists, then the groups by name.

Of the 1058 sound entries, 998 load: 60 name WAVs that are in neither
archive nor loose (discoveries).

## 3. Playing

Linear volume to DirectSound's hundredths of a dB (0x56e410):
`ftol(1000 log2(v) - 0.5)`, 0 at 1 and up, -10000 at 1/1024 and below: 10 dB
per halving (DirectSound's own scale would be 6).

`play_at` 0x56e7e0 (ECX sound or group, EDX position or 0, volume,
velocity or 0): groups go to 0x574470. A sound must be loaded. Volume =
sound VOLUME x master x the caller's. QUEUE sounds go to the voice queue
instead and return 0. Others: 0x56e610:

- A voice: the sound's own (+0x4c) if not locked and not playing, else a
  duplicate not locked and not playing, else a new duplicate
  (DuplicateSoundBuffer) while fewer than 7; with all eight busy the sound's
  own voice is taken again, cutting it off.
- VOICE-flagged sounds play only while [0x72c4a4] (always 1).
- With a position: 3D placement (section 4); a non-looping sound out of
  range does not start. Without: SetVolume (or -10000 when muted).
- SetCurrentPosition(0), cue times made absolute (game time) if the sound has
  cues and a cue function, Play (looping for LOOPED).

`status` 0x56e8e0: 1 playing, 0 not, -1 no voice. `stop` 0x56e9f0 also ends
cue tracking for the sound. `set_frequency` (t 0..1, 20000..90000 Hz) and
`set_voice_volume` (then the 3D falloff again). `lock`/`unlock` keep a voice
from being reused (sound nodes keep theirs).

The voice queue (QUEUE: radio chatter): 0x56e750 pushes the line with an
expiry `QUEUE seconds + queue clock`; the frame plays one at a time, half a
second after the last ends, **newest first** (the zUtil list is a stack:
push and pop at the head), dropping lines that waited past their expiry.

Cue points (briefings): while the sound plays, the frame calls the cue
function (ECX index, EDX sound) when game time passes each cue. After the
last (the end), the index steps one past the array and the next frame
compares with whatever follows it on the heap: FIX (bug), the port reads 0
there (what the exe's heap most likely holds: the next array's start), so
one extra call with index = count comes, then tracking stops.

Pause (0x56eb10): records every playing voice (the first record holds the
master volume). Unpause (0x56f130) does not resume those voices: for each
record it plays the sound's own voice and every duplicate whose volume is
not 0 dB again, each with the master volume's change (x 10000) added,
whether they were playing or not. A sound with two voices playing is
replayed twice. Voices that had ended (DirectSound rewinds them) start over.

## 4. 3D placement (0x572050)

No hardware 3D: the game computes volume and pan itself. The listener is the
camera's world matrix (0x571e50: rows; position at +0x24) and velocity. For
a 3D sound with a position:

- d = sound - listener, distance by a fast square root (the float's
  exponent halved: up to 6% off), 0 distance = RANGE min.
- Beyond RANGE max: volume -10000, returns 0. From RANGE min on:
  `volume += ftol((dist / min - 1) x -600)`: 6 dB per min distance.
- Pan `ftol(dot(d, listener right) / dist x 1600)`: at most 16 dB on the far
  side (DirectSound's range is 100).
- With the doppler flag (only 0x4adc60 passes one through from its own
  caller, not traced):
  GetFrequency, then SetFrequency(f x (1 - closing speed / 343)), the factor
  applied to the current frequency each time, never put back. Without
  CTRLFREQUENCY (every shipped sound) both calls fail.

## 5. Fades (0x571830..0x571d20)

A fade moves a voice's volume 2500 hundredths of a dB a second toward a
target (the sound's volume x level x master, in dB). It ends only when the
volume lands exactly on the target, which the steps do only at 0 dB and
-10000 (where the clamp lands them): fades to anything else never end and
keep the volume wobbling one step around the target (in the tests' dumps
they pile up). `stop` set: the voice is stopped at the end. Finished fades
go to a free list.

## 6. Groups (zsnd_grp.cpp)

`("name" KEYWORDS... (entry) (entry) ...)`: DELAY_REPEAT, DELAY_TERMINATION,
DYNAMIC_WEIGHTS factor (0..1), PLAY_SOLO (one instance at a time), REPEAT n
(-1 forever), EXTRA (the load callback's), and any other string becomes the
**name** (so six groups whose data has `"PLAY_COUNT" 2` after the name are
renamed "PLAY_COUNT": discoveries). Entries: a sound name ("NULL" for a
pause) with DELAY_PLAY, PLAY_COUNT, PLAY_SOLO, WEIGHT; a nested list in an
entry chains another sound after it. Without weights on every entry all
weigh 100/n.

Playing (0x574470, the voice kind 1): a weighted pick (MSVCRT `rand`,
0x573e00: the first entry whose running weight + 0.1% of the total reaches
`rand / 32767 x total`; DYNAMIC_WEIGHTS then multiplies the pick's weight by
the factor, lifts the others to at least 0.001 and scales them to 100), then
per frame: the entry's chain plays as its delays pass (a PLAY_SOLO entry
holds the group until it ends), then REPEAT (after DELAY_REPEAT) or
DELAY_TERMINATION, then done; done groups leave the playing list one per
frame for a spare list. `status` is 1 while the group is in the playing list.
The floor of 0.001 cannot be reached with the shipped factor (0.5).

## 7. The port

- `platform::SoundOutput` mirrors the DirectSound calls one to one
  (HRESULTs, units, controls; Lock hands out the buffer's memory). FIX (A).
  The SDL3 implementation (`sdlfe::SdlSoundOutput`) mixes in software:
  nearest-frame resampling at each buffer's frequency, DirectSound's volume
  and pan law, controls a buffer lacks fail as in DirectSound, and the
  output is stereo at the device rate whatever the primary format says (as
  on Windows since Vista). `build/mw3soundcheck [sound] [set] [lod]` plays a
  game sound through the whole chain (checked: `menu_select`, and
  `briefing_c1m1` for 5 s, stopped at shutdown).
- Globals are `zsnd::State` fields with their addresses; the engine
  variables, the CD and the device are `zsnd::Hooks`.
- FIX (bug): a new duplicate voice is zeroed (the exe's malloc leaves its
  position and has-position flag as the heap had them).
- The sound nodes (zclass/sound.cpp): 0x4fd680 (each frame: start the
  node's sound once, locked; 3D when the node has a parent or a position,
  at its world position from 0x4fd780), 0x4f3170 (the world's sound nodes,
  the scene walk's `world_sounds` hook), 0x4fd5c0 / 0x4fd620 (position),
  and 0x4fd520 now stops a playing sound. Not emulator-checked (the node
  update needs zClass nodes in the emulator); `zcls::world_sounds` is not
  wired to the hook yet.

## 8. Test

`test_sound.py` runs the original zSound in Unicorn (FPU 24 bits) with the
original zReader's tree of sounds.zrd, the real archives (read by Python
stand-ins of the ported zutl_zar), loose WAVs through fopen, and Python COM
objects modelling DirectSound (the same model as the probe's C++ recorder).
After every operation the DirectSound calls (with the WAV bytes hashed at
Unlock), error reports and callbacks must match, and the returned voices
(as set.sound.slot or group identity); every 50 operations (1 with
`SND_DUMP_EVERY=1`) the whole state. Runs: loading every set at each
SoundLOD, archives off, without the engine variables, demo sets, a card
without 16-bit; every group started 200 times (picks and weights); 12 runs
of 1500 random operations (plays, 3D, listener, doppler, fades, frames,
queue, cues, pause and resume, group stash, mute, buffers ending), some
with every control available so the frequency paths run to the end.

Use-after-free in the original is kept out of the random runs (it is the
same in the port, so not comparable): freeing a stashed group list while
other groups hold its voices, and unloading a set while a fade or a pause
record still points at its duplicates (both are real hazards: quitting
from pause, unloading a mission's sets with fades running).

Breaks caught (each made in the port, the quick test fails): DirectSound's
volume curve, an exact square root, 5 dB falloff, pan 2000, a FIFO queue,
8 duplicates, 30 dB/s fades, the pick without its slack, unpause skipping
idle duplicates, MED read as MEDIUM, the data's channel count, a queued line
kept 1 s longer, a group's status not asking its voices. Not caught: the
0.001 weight floor (unreachable with the shipped data, above).

## 9. Left

- The A3D path and the syntax 1 reader (dead in MW3).
- CD volume through the aux / mixer API (0x572ee0, 0x573060..0x5731f0): the
  music output's own volume stands in (zsnd_cd).
- Wiring: `mech3::Engine`'s sound hooks (sound_find, sound_play, ...) and
  the scene walk's `world_sounds` / `listener` hooks to the port, and the
  options' SoundLOD / MuteSound / SoundVolume to `zsnd::Hooks::find_var`.
- The movie soundtrack stream (0x5758c0, 0x575550, 0x575600, 0x575690) is
  ported but untested until the movie player is.
- The sound node destructor 0x4fd370 and 0x4fd440 (need the node free).
- An emulator test of the sound nodes.

## Pirate's Moon (2026-11-03, pm-map step 7a)

PM's zSound (`C:\zipper\branchep1\gamez\zSound\`) is MW3's with the
sounds.zrd reader rewritten and the sets moved into std::lists. The port
keeps one zSound (`zsnd::State`, `Set`, `Sound`, the groups) and forks
through `zgame::Table` only what PM changed (`src/pm/gamez/zsound`):

| | MW3 | PM |
|---|---|---|
| reader | 0x005705e0 + 0x00570af0: SYNTAX 2 only, DEMO_SETS / DEMO_GROUPS with the demo flag, HIGH/MEDIUM/LOW_DEFAULT | 0x00591840 + 0x00591900: no SYNTAX, no demo; COMPILE_FORMAT, DEFAULT_QUEUE_PRIORITY, INCLUDE_SETS |
| a set | made per SETS entry; a SUB_SET naming a set read later stays null | made by name (0x00590ae0): a name twice is one set, a SUB_SET may name a set read later |
| a set's items | a keyword steps over two items | over three (0x005920e0): a keyword before the sounds loses the first sound; every shipped SUB_SET is last |
| a sound | the formats from the *_DEFAULT keys, RANGE's raw words | the formats from COMPILE_FORMAT (the image holds MW3's built-in three), RANGE through the vec2 getter, times the range scale, QPRIORITY 0..7 in flags bits 10-12, the volume clamped after the callbacks |
| PLAYBACK_FORMAT | channels forced to 1 | the data's channels (PM's data says 1); "MED" still asked for, as MW3 |
| find a set | exact (0x0056f680) | any case (0x00590a20, _stricmp) |
| load / unload a set | a flag: a second load does nothing, one unload unloads | counted (0x00590d70, 0x00590f80); unload_all (0x00590950) counts each down once, with the device open; destroying a set (0x00590c10) counts down once more and frees its sounds |
| the voice queue | newest first (0x0056e750) | oldest first, before the first line of a higher QPRIORITY number (0x0058fb60); one that beats the very first goes last (0x00577950 leaves the head); no shipped sound has QPRIORITY |
| unpause | every voice of each sound recorded, again (MW3's FIX (bug) in the port) | each recorded voice once, from where it stopped (0x00590530) |
| duplicates | 7 a sound ([0x005efba8]) | 4 ([0x00613188]) |
| speed of sound | 343 m/s | 345 ([0x006135cc]); no sounds.zrd sets it, no sound has frequency control |
| range scale | none | 0x00591830: 0.5 at the lowest object detail (0x00482ca0, 0x00408120), else 1 |
| shutdown | frees the groups and sets, closes the device and the CD, then the CD and sets again | only through the device's close (0x00591700) |
| CD notify | modes 5 and 6 replay the track | also mode 3: the disc again from track 1 (0x005938c0) |

The rest pairs with MW3's (compiler only, other offsets, or calls PM's own
code compiles another way): the play path, status, stop, pause records,
fades, 3D, the groups, the WAV reader, the buffers. The error reports name
PM's paths and lines; the port's keep MW3's. Gone in PM (16 of the port's
MW3 functions): the set index (0x0056f630 set_at, 0x0056f660 set_count,
0x0056fe70 set_sound; PM's sets are lists), the movie soundtrack stream
(0x005758c0, 0x00575550, 0x00575600, 0x00575690, 0x00575980; PM has no
movies), and ones PM has under new pairs: 0x0056f130 (PM 0x00590530,
forked), 0x0056f3e0 (0x00590820), 0x0056f520 / 0x0056f530 (0x005908f0 /
0x00590920, which check the device is open and pass no null on),
0x0056f680 (0x00590a20, forked), 0x0056fe00 (0x00590f80, forked),
0x00573420 (0x00594150, MW3's without DEMO_GROUPS) and 0x005742c0
(0x00594f10). PM's static data the port sets with `pm::zsnd::statics()`
after `zgame::set_game` (the speed of sound, the duplicates).

`test_pm_sound.py` is `test_sound.py` on PM's exe at its own addresses
(cdecl where MW3 passed ECX and EDX) and PM's sounds.zrd, soundsH.zbd and
soundsL.zbd: loading every set, every group's picks, random play, loads
counted and names in other cases, the range scale, seven changed
sounds.zrd files (INCLUDE_SETS, SUB_SET forward and a set twice, a keyword
first, QPRIORITY and DEFAULT_QUEUE_PRIORITY, COMPILE_FORMAT and
PLAYBACK_FORMAT as names and in stereo, no SETS) and the CD notify in each
mode. Breaks caught (each made in the port, the quick run fails): two
items stepped over a keyword, sets not merged by name, set names matched
exactly, loads or unloads not counted, free_sets without its second
unload, the queue newest first, without priorities, or a line beating the
head put first, unpause as MW3's, 7 duplicates, 343 m/s, INCLUDE_SETS,
COMPILE_FORMAT, the range scale, QPRIORITY or DEFAULT_QUEUE_PRIORITY
ignored, the playback in mono, a format list of two items taken, shutdown
as MW3's, CD mode 3 not looped or without its reset. Not caught, not
observable: unload_all's open check (it runs only from the device's close)
and the volume clamped before the callbacks (no callback reads it).

## The sounds' EXTRA records (sound_extra.cpp)

sounds.zrd's per-sound EXTRA list goes to zSound's load callback 0x004e6b00:
`MINDELAY` (the first value: seconds before the sound may play again) and
`SUBTITLE` (a message name, its text through 0x00575fb0) make a 0x0c-byte
record (subtitle, delay, next time) kept as the object's user data (0x004e6a60,
0x004e6aa0, the list 0x0060b814 / 0x0060b818, freed by 0x004e6be0 when the
groups shut down). 20 sounds have MINDELAY and 40 a SUBTITLE, all radio
lines (the lance mates' calls, the commands' answers). Sounds without one
use the zeroed record 0x0060b800. 0x004e6cd0 plays a sound once its delay
has passed (the game time [0x0060991c]) through zsnd's 0x00571cc0, and
shows the subtitle in the message panel (0x0040fe50) when [0x0060b878] is
set; 0x004e6d80 finds the record; 0x00432990 plays the pilot's slot (the
fifth argument unread, no fade). Nothing in the exe writes [0x0060b878]:
the subtitles never show (a candidate in fixes.md).


## In a mission (c1m1, 2026-10-21)

The mission's music: the Core start (0x00480df0) plays its track through
0x004ac290 (mode 6), Core's suspend and resume pause and resume the CD
(0x00482330, 0x00482430), its leave stops it (0x00481fc0), and the main
loop's notify keeps a looping track going (0x00572bf0); all ported and wired,
playing `music/trackNN.*` (zsnd_cd). Nothing else in the exe changes the
music during a mission. The mission's cleanup (0x00491780) stops zSound's
queue (0x00570410(1)) and, as Core's leave, snapshots every playing voice,
stops them and frees the snapshot (0x0056eb10, 0x0056f0a0, 0x0056f190);
wired 2026-10-21. The three-mission test plays a stray voice before each
cleanup and finds none playing after.
