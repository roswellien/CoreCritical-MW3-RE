# Changes from the original MechWarrior 3

This rebuild runs the original game's own logic, ported function by function
from `Mech3.exe` 1.2. Wherever it does something different from the 1999
game, the change is listed here in plain words. The technical record (the
original address, the evidence, how each fix is tested) is
[`docs/fixes.md`](docs/fixes.md). In the code each change is marked `FIX`
next to the function it touches.

**To play with every bug fix off:** start `mw3 --original`, or put
`bugfixes=0` in `mw3.cfg` in your settings folder. That puts back every
change under "Bug fixes that change play" below except the frame-rate ones
(the game still plays at the same pace on any computer), and keeps the
fixes that let the game run at all and the crash fixes. `bugfixes=1`, or no
line, keeps them on.

The rule: keep everything as the original did it, quirks that players know
and plan around included. Change only what stops the game running on today's
machines, what makes it behave differently on a fast computer, and bugs where
it is clear what the designers meant.

## Bug fixes that change play

- **A joystick's throttle lever reaches reverse.** The original's lever ran from stop to full ahead; reverse took the Toggle throttle direction key (Backspace). Now the lever covers the whole range, full reverse at the bottom to full ahead at the top, with stop a little over a third of the way up (reverse is slower, so each step of the lever is the same change of speed both ways) and a small band around it that holds stop. A lever left at the bottom does not send the mech backwards at the start of a mission. Toggle throttle direction (Backspace, stick button 10) flips the keys' throttle but not the lever, so nothing can leave the lever backwards for the rest of a mission; a lever that runs backwards on your stick is cured by unticking Invert in Assign Axis. `throttle_reverse=0` in `mw3.cfg` gives the original's lever, which Backspace flips. Gamepad triggers are unchanged. The controls screen's Assign Axis also offers the stick's sliders, where many throttle levers are.
- **A laser's beam leaves from the arm after a quick click.** A short laser keeps firing for a moment after you let go of the button, and the original left it at the point where the arm was when you let go, so on a walking mech the beam hung back a metre or more behind the arm (and fired from there). It now follows the arm until it ends, as it does while the button is held.
- **Every nav point you pass on your objectives' list is ticked off.** The game marks a nav point an objective watches as reached (and says its line) when you come within 25 m, but once its check passed the currently selected nav point it skipped the rest of the list, so those were only ticked off once selected. Now all of them are checked.
- **Turrets do not slow down mission after mission.** Each turret looks for a target, aims and fires on only one frame in so many, and the original counted that over every turret the game had made since it started: the more missions you had played in one sitting, the less often each mission's turrets acted. The count now starts again with each mission's turrets.
- **Falling over is felt on a force feedback stick.** Patch 1.2 added a force feedback effect for a fall, but the game asked for it by another name than the effect file gives it, so it never played. The rebuild finds it.
- **The mobile field bases always come when called.** Called back to the point where they last repaired you, the bases were already standing in place and never "arrived", so the dock waited for the rest of the mission and refused every new call; a base that got stuck on its way did the same. They now count as arrived, and a base that cannot move on for 30 seconds is left behind so the repair goes ahead.
- **Artemis IV FCS makes missiles track twice as hard.** Locking with an Artemis fire control system was meant to double your missiles' turn rate, but the game reset the setting before any missile read it. Each missile now keeps it, so FCS-guided missiles turn (and climb over obstacles) twice as fast.
- **A turret's charging effect goes out when it dies.** The original's test for putting it out was the wrong way round (no shipped turret shows one, so nothing visible changes).
- **"Sort by name" in the mech lab sorts by name.** The original looked the names up and then sorted by the game's own mech numbers.
- **Lancemates keep their career kill counts.** After a won mission the original copied each lance card's "'Mechs Defeated" number into the campaign's records by card position, writing 0 for a card with no matching pilot, which wiped a lancemate's whole tally when they were not on that card. Now such a card leaves the record as it was.
- **Answering No to "Replace existing game?" keeps your old save.** The original wrote the new mission's world into the saved game's file before asking, so No kept the old entry in the list but its file was already overwritten. The world is now saved only once you answer Yes (or when there was nothing to replace).
- **A vehicle driving over burning ground no longer crashes the game.** When a tank's collision check touched a floor marked as fire, the original wrote into memory it should not have and could crash.
- **A vehicle gun's pitch limit is worked out with the right length.** The original's function for putting an aim on a vehicle gun's top or bottom angle used the wrong length (a slip of one letter), so the result missed the limit. Nothing in the game uses that result (the AI aims with the unclipped direction), so this changes nothing you can see; it matters only if a mod or later code relies on it.
- **A vehicle with chassis pitch settings and no roll settings keeps its pitch.** The original zeroed the pitch values when the roll values were missing (a copy slip). No vehicle in the game has either setting, so this only matters for modded vehicles.
- **Lava heats the same at any frame rate.** The original added heat on
  every frame a foot touched lava, so a faster computer cooked you faster
  (1.5 heat a second at 30 fps, over 7 at 144). It is now 1.5 a second
  always, what lava did at 30 fps.
- **The HUD's damage static builds at the same speed at any frame rate.**
  With the torso or head badly hurt the panels break up with static; the
  original added to it every frame, so a faster computer showed more.
- **Rain and snow fall the same at any frame rate.** The original moved
  them by a fixed step a frame for the fall and the wind but by the real
  frame's move for your mech, so above 30 fps the rain looked steeper while
  you walked and the snow fell and blew faster; now both look as they did
  at 30 fps.
- **Smoke drifts the same at any frame rate.** Above 60 fps the original's
  smoke stopped slowing down and ignored the wind, and the wind's gusts
  changed faster the faster the game ran. Both now go by time, at the 30 fps
  pace.
- **Turrets and mech arms turn at any frame rate.** The original eased
  them toward where they aim, and eased the arms' recoil, by a table that
  rounds a frame's step down to 1/51: above about 150 fps the turrets did
  not turn at all, and the missile platforms that start facing straight up
  (Op 1 Mission 2) were drawn flat, invisible but alive, and the objective
  looked impossible. These now ease at the original's 30 fps pace at any
  frame rate, and exactly as before at 30 fps. A turret aimed straight up
  or down also keeps its shape. Vehicles keep the original's damping.
- **Mech movement plays the same at any frame rate.** Falls, the shoves from
  slopes and ledges, the blend into a walk and jump-jet thrust all went by
  frames; they now play as they did at 30 fps.
- **The throttle keys open the throttle at the same speed at any frame
  rate.** Each frame a key was held added a fixed step, so on a fast computer
  the throttle ran up several times faster. It now moves as it did at
  30 fps: from stop to full in about 1.3 seconds (with the lance command
  panel open too, which has its own copy of the keys).
  The mech's speed then follows at the same pace too: above about 83 fps an
  earlier version of this fix let it skip its slow build-up and pull away
  nearly 2 seconds sooner.
- **Lasers and TAG recharge at the same speed at any frame rate.** The
  original dropped the fraction of each frame's recharge, so the faster the
  game ran the slower beams came back: TAG took 67 seconds at 30 fps, 83 at
  60, and above about 166 fps (or at 144 fps once the mech ran a little hot)
  it never recharged at all after its first shot. Beams now recharge at
  their 30 fps pace whatever the frame rate.
- **Turrets act at the same pace at any frame rate.** A turret looks for a
  target, turns and fires only on its turn, one frame in as many as there
  are turrets, so on a faster computer turrets reacted and shot more often.
  Turns now come by time, at the 30 fps pace: with 14 turrets each acts
  about twice a second.
- **Alan talks in every mission.** His name is misspelt in two missions'
  data (the first Instant Action map of the first world and operation 3's
  mission 6), so the game did not give him his radio voice there. Both
  spellings now get it.
- **Two enemies follow their orders.** On the fourth world's Instant Action
  maps, two enemies had their scripted orders written without the label that
  marks them, so they were ignored. They are read now.
- **Feet use the ground under their middle.** Each foot reads the ground
  at its front, middle and back, but the setting that picks the middle was
  never filled in, so the game used leftover memory: usually the front or
  back reading, sometimes any. The middle is now used when the foot is close
  to it, by the value the original team's own Pirate's Moon sets.
  This touches how feet sit on slopes and riverbanks and when a foot counts
  as in water.
- **Both feet ease into a walk.** For the first two thirds of a second of
  walking, the right foot snapped straight to its walking angle and the left
  one swung toward the right foot's old angle. Each foot now eases in from
  its own position.
- **Deep water no longer stops a mech dead.** Wading in water nearly as deep
  as the mech is tall slowed it almost to a standstill, then it sped back up
  to 70% once the water was deeper still. It now slows smoothly down to 70%.
- **Wingmen call out low and empty ammo.** Dom, Alan and Epona have lines
  for it that never played because of a mistake in the sound data. They now
  play, each line at most twice a mission.
- **The ERPPC's HUD static lasts as long as its data says** (2 seconds); the
  original read the first number twice and held it half a second.
- **The HUD no longer reads a stray layout** when the screen mode has none of
  its own in hud.zrd: the original could use whatever memory held; the
  rebuild keeps the 640x480 layout, as later set-ups did.
- **Pilot names ignore capitals, as their files do.** Making "BOB" when
  "bob" exists picks bob instead of a second pilot sharing (and
  overwriting) bob's file, and renaming onto another pilot's name is refused.
- **The plain PPC is in the mech lab.** The game defines and names it, but it
  was left out of the mech lab's weapon list. It can now be mounted (Inner
  Sphere, 7 tons, 3 slots, as the data says).
- **The Mad Cat and Supernova show their battle damage.** Their damage
  texture lists were shifted out of line, so the wrong parts got bullet holes
  or none. They now line up.
- **Unpausing resumes the sounds that were playing,** from where they
  stopped, instead of restarting every copy of them, including ones that had
  already finished.
- **Volume fades finish.** A sound fading to a partial volume used to wobble
  around it forever; it now settles.
- **3D sounds pan left and right.** The game asked the sound card for mono
  even though its own settings say stereo, so on 1990s Windows the panning
  was lost. The rebuild reads the setting. Medium sound quality also gets its
  own settings now (the code misspelled their name).
- **Heat sink critical hits are reported.** Losing a heat sink to a critical
  hit now gives the "heat sink" callout and sound, which the original's check
  never allowed.
- **A broken animation condition no longer cancels the others.** When one
  of a mission animation's conditions names an object missing from the
  level, the original dropped all its conditions and fired it at once; now
  only the broken one is ignored. No shipped mission has one; it matters
  for modded missions.
- **The last animation in a mission can be waited for.** A search stopped one
  short of the end of the list. No shipped mission was affected.
- **A smoke fade-in typo is fixed:** smoke set to fade in near the camera
  vanished instead.
- The debriefing's mission time keeps its seconds under 60 past the first hour, as the HUD's timer.
- The HUD's mission timer keeps its seconds under 60 past the first hour (the original showed an hour and 40 seconds as 01:01:3640).
- The credits' "Other Voices" heading sits where the other headings do (a typo in the credits file gave it one number of its two, so it hung indented under the line above).
- TARGET's distance past 1 km reads " (1.23km)" like the " (500m)" below it; the original ran the kilometres straight on after the loadout's name.
- **The message panel slides up at any frame rate.** The original moved it a whole number of pixels a frame, which above 60 fps is none: on a fast computer the in-mission messages never came into view.
- The circles on the briefing map shrink at the same speed at any frame rate (the original shrank them a pixel a frame above 20 fps).
- The briefing's loading bar creeps at the same speed at any frame rate (the original moved it 0.005 a frame).
- The coolant gauge glides at the same speed at any frame rate (the original eased it a tenth of the way each frame).
- **Accepting the game options keeps your mouse sensitivity.** The original reset the vertical mouse sensitivity to 1.3 (or -1.3 with Invert Mouse Y) every time; now the invert box only flips its direction.
- **TAG is the same at every frame rate, and its airstrike follows the target.** The one-second hold is a true second, and the beam lasts what it did at 30 fps, so you always have about 67 ms to slip (the original gave two frames at 30 fps and one at 60 or 144). The four missiles now home on the mech you marked instead of the spot it stood on seven seconds earlier, so a moving mech is hit too. TAG still fires only from a full charge.
- **A mech put back on the ground looks 10 m above where it stands**, as everything else in the game does. The original looked y + 10 above a mech at height y, so below sea level it found nothing and dropped to the lowest surface (under a bridge), and high up it could land on a roof overhead. No mission's starting place changes: all 195 placements in the 24 campaign missions and all 768 records in every campaign, Instant Action and multiplayer file land exactly where they did. What changes is a mech re-placed where it stands (swapping mechs mid-mission or in multiplayer), which now stays put.
- **Cut ground keeps its texture lined up.** When a crater cut a ground polygon and a corner of a cut piece lay on a straight edge, the original dropped that corner from the piece's points but not from its texture coordinates and colours, so every corner after it took the next one's: the texture and shading on that piece slid by one corner (about 1 in 200 of the polygons a crater adds). The extra corner's texture coordinates and colours now go with it.
- **Craters next to an earlier crater are judged on the earlier one.** The original refuses a crater that would come within 5 m of an earlier crater in the same patch of ground, but it checked against the earlier crater's record after freeing it, so the answer depended on what the memory held by then. The rebuild keeps the earlier craters' outlines for the test.
- From high up, no dark band between the far ground and the sky: what nothing draws takes the fog's colour (bug fixes on).
- A light carried by something that moves now shines from where it is, not from the world's origin.

## Crashes and broken data that no longer break the game

- **Finishing a mission while commanding your lance from the pocket view no
  longer crashes.** The original threw the commander's view away at the
  mission's end while your mech was still using it, then had the mech let
  go of it again; the rebuild gives the mech its own controls back first.

None of these change normal play; they stop a crash or a hang when something
unusual happens.

- A movie script line with too few values reads the missing ones as 0 instead of reading past the line (no shipped script has one).
- c2 mission 3's two Firefly objectives are real: the data named the wrong Fireflies, so the original ticked them off before the mission began.
- The HUD no longer crashes drawing static on the TARGET display in a mode with nothing to show.
- The rain's and snow's very first frame no longer draws its streaks from unset sizes (it used the screen's size from the second frame on anyway).
- A picture the menus or the HUD let go of twice is no longer freed twice (which could crash); the rebuild reports it instead.
- Keypad 0 (the zoom lock) no longer crashes when the mech you are looking at has no HUD; it does nothing then.
- A script that cycles the textures of a model with no material, or turns a scroll off, no longer crashes (neither happens with the shipped scripts).
- An edited debriefing dialog missing its buttons' help, its title's, its lance cards' or its rank's lines no longer crashes.
- An edited mech lab dialog missing the supply list's slot titles (ALIST_TITLES) no longer crashes.
- An edited mech lab dialog missing the armour panel's texts (ARMOR_TITLES, ARMOR_REMOVE) no longer crashes.
- A briefing whose voice block has no map markers plays the voice instead of crashing.
- Salvaging a mech with a weapon that has no definition skips that weapon instead of crashing.
- Losing a field base while carrying 64 or more spare mechs no longer overruns the game's list of them (which could crash).
- Cycling the field bases' dock points (MFB, then N or Shift+N) onto the list's empty first entry no longer reads through nothing on the radar and the edge arrow (which could crash); no marker is shown there.
- A nav point file linking to a node that is not there no longer reads or writes past the game's lists (no shipped file does).
- The waypoint arrows' images are all released at the end of a mission (two of the four sides were kept, a small leak each mission).
- The waypoint arrow no longer keeps an image the last mission let go: the next mission's first arrow at the screen's right or bottom edge read that freed image's size, which on today's systems can crash the game.
- In Instant Action, a wreck taken away at 180 seconds no longer stays the "last attacker" of the mechs it hit: one of them dying later (a fall, its own ammo) credited the kill to the removed wreck, which could crash.
- A hand-edited turret without parts, or without a gun, no longer crashes in play; it shuts down, or does not aim.
- A mission animation that sends the unused "move the player through viaNode" message, in a world without that node, no longer throws the player's mech to a random place (no shipped animation or world does this).
- A hand-made mission animation whose object is missing no longer crashes the mission load when it flips a switch on a part above it (no shipped animation does this).
- A hand-edited turret file whose turret names a weapon that does not exist, without a detection range, no longer crashes the mission load; the turret sees 200 m. A turret file entry shorter than the game reads is taken as missing (no shipped file has either).
- A hand-edited objectives file with a number or a list where a name belongs, or an objective list without its `list`, no longer crashes the mission load; the odd entry is skipped.
- An Instant Action game in an unknown mode no longer crashes when its "destroy the enemies" objective is built; the objective is left out.
- An objective description with a `%` in its second or later lines is shown as written instead of being read as a format.
- A line of the briefing's debug log with a `%` in it is printed as it is (the original read it as a printf format).
- A soils file missing one of the game's ground types no longer corrupts the
  word in memory before the game's soil table; the type is just left out.
- A shot passing through more than 32 objects no longer writes past the end
  of the list of what it hit; the original had room for 32.
- Asking for the ground under a point where more than 32 objects' surfaces
  overlap no longer writes past the end of the answer; the original had
  room for 32 and stopped only some of the time. No shipped mission comes
  close.

- A cockpit camera given a distance in a mech file no longer reads leftover
  memory for its offset (no shipped mech does it).
- On a computer without MMX, the software renderer's screen flashes no longer
  write past the end of the screen's memory on its last line.
- A mech file with a bad version no longer crashes the game when it unloads.
- A mech type whose file fails to load no longer crashes the next mission's
  load (the original kept the broken type and read through it there).
- The option screens no longer read past their background picture on a
  screen taller than 960 lines or wider than 1280, the likely cause of the
  crash when opening Options from the pause menu at 1280x1024 or 1600x1200.
- A video card whose name has a `%` in it, or is very long, no longer
  garbles or crashes the graphics options; it is shown as written.
- Three checks the original menus skipped no longer crash when missed (a
  credits line over 255 characters, a dialog without a mouse pointer, the
  credits opened without the main menu); nothing shipped reaches them.
- A scripted sequence with no frames in it no longer reads past its data (no
  shipped mission makes one).
- The game starts without a mouse (the original crashed), and pressing a
  newer key such as Power, Sleep or a web key while holding Ctrl, Alt and
  Shift no longer corrupts the game's input memory.
- A damaged or hand-edited saved loadout (`configs\<mech>.mw3`) can no longer
  hang the load or crash it.
- A controls profile with a `%` in its file name shows its name in the load
  and save list instead of garbage (or a crash), and an empty list's
  clickable area is empty instead of whatever was left in memory.
- A damaged archive or script file is stopped at its end instead of making
  the game read or allocate gigabytes.
- A damaged saved game (`pilots\games\*.mw3`) no longer crashes the
  mission it is loaded into; the damaged parts are passed over.
- Rare random rolls in equipment critical hits no longer crash.
- Clearing every shot in flight no longer writes past a firing laser's
  memory (the original treated beams as missiles there).
- Very large polygons, long script lines and oversized weapon sound or
  damage lists no longer write past their buffers.
- A null texture, a missing weapon group and a few malformed script commands
  are checked instead of crashing.
- A mech whose right foot had fewer toes than its left would have animated
  one toe too many on that foot, writing past its data; it now stops at the
  last toe.
- A mech animation sampled at the very start of its cycle, before any other
  moment of it, no longer reads leftover data past its frames (it could show
  one wrong pose; for the longest motions, garbage).
- Undefined values the original left in a few places (a matrix row, a new
  sound copy's position, an air burst's surface direction) are zeroed.
- A damaged saved controls-file name (no end marker) no longer makes the game
  read past it.
- A hand-edited controls profile (`keys\<name>.zrd`) with an axis of an
  unknown name, or a number or an empty entry where a command belongs, no
  longer crashes the game; the bad line is skipped.
- The controls screen no longer overruns its file name with a very long
  current-profile setting, and an empty command group (none ships) shows an
  empty list instead of crashing.
- A text data file with a word of 256 letters or more, a quote that is never
  closed, or an `#ifdef` without its `#endif` no longer crashes or hangs the
  game.
- A vehicle with a name of 20 letters or more (or with a `%` in it) in a
  mission's vehicle file keeps its name; the original cut it and, from 28
  letters, overwrote the vehicle's settings. A made-up vehicle whose name
  ends in `_mech...` without an AI path no longer gets the file's own record
  as its driver. No shipped mission has either.
- A mech model missing a node its definition names (a firepoint or a foot),
  or a definition without camos, no longer crashes the game as the mech is
  built at launch, nor does a launch with the view on another vehicle (the
  game never does that). No shipped mech has either.
- When the only delayed trigger waiting fires and starts more delayed
  triggers, they are kept; the original dropped them, which can silence
  three radio lines in operation 3 mission 2.
- A hand-edited mission trigger file (`valves.zrd`) with a number where a
  list or a name belongs, or a trigger that teleports or switches the scan
  of a pilot that is not there, no longer crashes the game.
- Leaving the mission area now fails operation 4b's missions 5 and 6, as
  their objectives say: their boundary triggers named a region and a
  switch that did not exist (behind the bug-fix switch).
- Ending a mission while the lance command mode is open no longer frees
  the mode twice (a possible crash in the original).
- The lance command mode no longer reads past a missing HUD when the view
  is not on your own mech (a possible crash in the original).
- Starting a mission after another no longer runs the previous mission's
  player triggers against switches that were already thrown away.
- A mission trigger that updates itself, directly or round a loop of
  triggers, no longer crashes the game: each trigger's actions run once
  for each change. No shipped mission has such a loop.
- An AI vehicle that fails to be built no longer crashes the mission start
  as its driver is thrown away, and a made-up radio voice name of 15
  letters or more in a mission's vehicle file no longer overruns memory.
  The shipped missions have neither.
- A big C3 network with mission targets in range no longer writes past
  the memory set aside for its shared contacts, and a lancemate without a
  pilot name in a hand-made mission file no longer crashes the start.
- A hand-made AI region file without a name, or an AI path linking past its
  last point or to points without a position, no longer crashes the
  mission load or the AI's route finding. One shipped path (c2 mission 3,
  path 13) links to a point that does not exist; the original read
  leftover memory there, the rebuild takes no link.

- A damaged pilot file no longer hangs the game while it is read (a count
  read past the end of the file made up to 4 billion empty campaigns or
  lancemates).
- A file in `pilots\` with a name of 64 characters or more no longer
  crashes the pilot screen; it is not listed.
- DELETE on the pilot screen before any pilot exists no longer crashes the
  game; it does nothing.
- Copying a saved loadout that holds an engine item (only a hand-made file
  can) no longer crashes the game after a mission; the item is left out. A
  damaged loadout with an item in a part that does not exist no longer
  corrupts the loadout when its free slots are counted.
- Deleting the pilot you had chosen before no longer leaves the game holding
  the deleted pilot (it could be taken for the player's in a lobby game).
- A weapon group with no weapons in it (or none selected) reads as having no ammo; the original's answer depended on where its weapons list sat in memory.
- An AI pilot with exactly 13 (or 39, ...) orders queued no longer reads past its order list (a possible crash) when the game checks whether it is on a path it is not on.
- A target behind the zoom window's camera no longer picks TARGET's arrow from stray memory bits (the original read an unset value there).
- A mech with a targeting computer and no weapon group selected no longer crashes the game when it has a target on screen (the lead circle needs a group).
- An AI vehicle record with a broken attack range no longer crashes the game when its pilot has no target.
- An AI path with a step that leads nowhere no longer crashes the game when a pilot weighs the ways on from a waypoint; that way counts as closed. No shipped path has one.
- An AI "backup" waypoint (none in the shipped game) turns the pilot toward the way it came, before backing in and again at the spot, instead of toward whatever direction happened to be left in memory.
- A vehicle definition without its `platform` or `collision` points no longer crashes the mission start.
- A vehicle whose weapon list (`vehicle.zrd`) names a weapon the game does not know no longer crashes the mission start; that weapon slot stays empty.
- A modded mission with more than three mobile field bases no longer overwrites the first base's valve; the extra bases are put out of play as the original does, without the stray write. No shipped mission has more than three.
- A mobile field base hit while there is no player's mech no longer crashes the game (it cannot happen in the shipped game).
- A modded mission with fewer than three mobile field bases no longer crashes when a repair starts.
- An AI "deploy", "mcbwait" or "valve_assign" waypoint without its switches named no longer crashes the game; the missing switches read as off and are not set.
- An AI jump waypoint that does not lead to a path node no longer crashes the game; the pilot jumps without turning.
- An AI vehicle without weapons no longer crashes the game when it is hit (its pilot's firing stands down); neither does an AI weapon without a definition when it fires.
- An AI pilot whose target disappears while it is lining up a shot no longer crashes the game; it gives the shot up.
- An AI mech with no weapon group selected no longer crashes the game when it turns its torso toward a target.
- An AI mech whose chosen weapon belongs to no weapon group no longer crashes the game when it picks what to fire.
- An AI pilot given an order before it has a vehicle no longer crashes the game when the order first runs.
- An AI pilot that finds no step of its old route to walk back to after a fight stops there instead of crashing the game.
- An AI pilot that starts keeping station during a fight no longer keeps a stale home point after the fight ends (the original could measure its leash from freed memory).
- A lancemate AI pilot that would keep station on a vehicle with no driver no longer crashes the game.
- A campaign lancemate slot left empty no longer risks crashing the launch when its mission entry cannot be read or there is no player mech to take over its duties (no shipped mission does either), and a lancemate's name with a `%` in it is no longer read as a format.
- Instant Action's line-up no longer risks a crash on a mission entry it cannot read, a wave number below 1 or a very long entry name (none happens with the shipped files).
- Two training objectives (looking at a target, choosing a weapon group) no longer read through a missing mech or weapon list; they simply wait (never the case in the shipped lessons).
- The Instant Action screen, opened after all 20 progression waves are beaten, no longer reads past its list of waves for a moment (it read text as mechs before going back to Wave 1; nothing showed).
- A vehicle hit by a laser and still alive when the mission ends no longer leaves the laser's light behind (the game could not free it).
- A line-of-sight or shot query toward something at an undefined place no longer freezes the game.
- An effect started on an object, after the same effect was used in mid-air, no longer takes that object's model down with it when the mission ends.
- An AI pilot with a negative personal-space setting no longer hangs the game when another mech comes close (no shipped mission has one).
- A world's `declient.zrd` with a one-frame quicksand texture, or a `QUICK_SAND` block without `DEFAULT_TEXTURE`, no longer crashes the mission load; the one frame is shown, or the quicksand is left untextured. No shipped file has either.
- Cutting a crater into large stretches of ground no longer risks reading freed memory or writing past the clipper's tables.
- A crater whose outline passes exactly through a corner of the ground's polygons no longer freezes the game; that piece of ground is left uncut.
- A crater whose outline runs exactly along an edge of the ground's polygons and touches one at a corner no longer freezes or crashes the game; that piece of ground is left uncut.
- A crater dug inside one large ground polygon can no longer overrun the memory used to cut the ground around it into triangles.
- A crater without its texture flags no longer crashes the game (the original wrote the crater floor's texture position through a null pointer; every shipped crater has the flags).
- Clearing a mission's ejection seats can no longer hang if one of them will not come off what holds it (none does in the shipped game).
- A modded mission script that sets a surface-mark type above 2 no longer overwrites other settings (no shipped script uses surface marks).
- A mission without an AI boundary (a multiplayer game, or a map without one) played after a campaign mission no longer leaves the AI using the previous mission's freed boundary.
- A modded map whose boundary names a mission past its operation's last no longer writes past the game's list of boundaries; that one is ignored.
- An AI pilot at a broken (NaN) position no longer reads leftover memory when the boundary looks for the way out.
- An objective's text can no longer garble the objectives box: the original used each objective's text as a formatting pattern, so a '%' in it would have printed garbage or crashed. No text in the game has one, so nothing you see changes.
- A file name the game reads from its data, a mod or a script (a texture, a sound, a movie, a script's `source` or file delete, a world folder) can no longer reach outside the game's folders: a name with `..`, a drive or a full path opens, writes and deletes nothing, and the log says so. The original opened whatever it was given. Nothing shipped uses such a name. Multiplayer sends worlds as numbers, which only ever name your own worlds.

## Copy protection removed

- No CD check at startup; the importer copies the movies from the disc.
- No self-check of the game's files, which made a modified game quit after
  the intro.
- Hand-edited saved loadouts load if they follow the mech lab's rules
  (weight, slots, jump jets, armor). They still get the original's
  checksum, so the original game can read them. The same goes for a
  loadout imported in the mech lab (Ctrl+I).

Details: [`docs/protection.md`](docs/protection.md).

## Running on modern machines

These change how the game talks to the computer, not how it plays.

- The graphics options no longer offer the software renderer, which the rebuild does not have: the video device list shows your graphics card only, and a setting saved by the original or an older rebuild that chose software starts on the card.
- **Gamepads work as they did on Windows.** A modern pad's sticks, triggers
  and buttons arrive in the order the game knew from Windows: the left stick
  steers and walks, the triggers are the throttle, the right stick looks.
  Read the raw way, a pad's resting triggers held the torso twisted.
- **Flight sticks' axes land where the game expects them.** A stick with a
  twist and a throttle lever (a Thrustmaster T.16000M, say) gave its twist
  as the throttle and turned no torso, so the reticle could not aim. Each
  axis is put where Windows put it, read from the stick itself; a player
  can set the map by hand with `joystick_axes=` in `mw3.cfg` (the log
  shows the line to start from).
- **The joystick is chosen sensibly.** The original took the first device
  that called itself a joystick; headsets and RGB keyboards often do now.
  The game takes a gamepad or a real stick first, or the one named by
  `joystick=` in `mw3.cfg`. Unplugged during a mission, the stick lets go
  of its turn and fire instead of holding them, and works again when
  plugged back in.
- **Force feedback becomes rumble.** The original drove force-feedback
  joysticks through Immersion's driver. The rebuild reads the same effect
  file and plays the effects on your pad's or stick's rumble motors: weapon
  recoil, laser buzz, hits, footsteps, landings, the jump jets, wading and
  your death. The stick-centring springs have no rumble version.

- The game finds your MechWarrior 3 installation by itself: where its installer put it (the registry, then Program Files), or it asks you once to point at Mech3.exe, then remembers the folder in `mw3.cfg`. The original simply ran from its own folder.
- The game installs straight from its disc, with no Windows installer: `mw3 --install-from` takes the CD drive, a disc image (.iso) or a .cue/.bin, copies the game into a folder of its own and remembers it, so Mac and Linux players need no Windows to install it. Given the 1.2 patch too (`--patch`), it is installed as 1.2 in the same step, and a .cue's CD audio becomes the music.
- Alt+Enter switches between fullscreen and a window while you play (the original had no window). Your saved choice (`windowed=1` in `mw3.cfg`) is what the next start uses.
- The mouse turns the view and moves the menu cursor at the same speed in a window and fullscreen, at any screen size, as the original's did at 640x480. Before, a bigger window or screen made the mouse slower.
- In a window, the menus leave the mouse free: the menu cursor follows the system's, and the mouse can leave the window (to resize it, or to use another program).
- In a window during a mission, pressing Ctrl+Alt together (and letting go) frees the mouse; the game ignores it until you click inside the window again, and that click does not fire. The pause menu frees it too, and switching to another program frees it while you are away. Coming back to a mission (Cmd+Tab, Alt+Tab or a click) takes the mouse again at once, cursor hidden, and the click that brought the window back does not fire; a mouse you freed with Ctrl+Alt before leaving stays free until you click. Fullscreen holds the mouse as before.
- While the game holds the mouse (a mission, or fullscreen), the system's cursor stays hidden even if Windows or SDL let go of the mouse for a moment (it showed in the middle of the screen).
- The click on LAUNCH (or RESUME) no longer fires your weapons as the mission starts.
- A yes/no question over the mech lab or a mission (saving over a config, quitting) keeps the picture behind it sharp instead of dropping to low resolution until you answer.
- Switching away from the game (Alt+Tab) mutes its sounds and pauses it, as the original did, and nothing you were holding (a key, a mouse button) stays held when you come back.
- The game opens fullscreen at your desktop's resolution, the picture fitted with bars at the sides if its shape differs, instead of switching the monitor to 640x480. `--windowed` (or `windowed=1` in `mw3.cfg`) plays in a window.
- The movies play from files the importer makes from the game's own (the picture as MPEG-1, the sound as it was), since Indeo 5 video does not play outside old Windows; a movie that is not in the game's folder is not looked for on a CD drive. Timing, skipping and the end are the original's: the picture still follows the sound.

- The pause menu's background is the battlefield as you left it. The original copied the screen; the rebuild draws the 3D world on the graphics card, so it reads the last frame back from it.
- At 16:9, coming back from the pause menu no longer shows the top-left corner of the picture blown up over the whole screen (the menu's picture size stayed on the 3D view).
- At 16:9 the pause menu fills the screen with its buttons at the left edge, and stays that way after visiting the options.
- Changing the resolution in the options during a mission no longer leaves part of the HUD undrawn (the weapon list, the speed gauge's marks).

- The game's question boxes ("Ok to delete pilot?", "Replace existing game?" and the rest) keep reading the mouse and keyboard while they wait: the original's box read DirectInput directly, the rebuild's also handles the window's events while it waits.

- Changing a mech's skin in the lab no longer blanks the lab's screen for a frame. The original's skin change briefly unlocked the screen while it was being drawn, which DirectDraw tolerated; the rebuild keeps the screen locked until it is finished.

- The mech lab's hidden loadout export and import keys (Ctrl+X, Ctrl+I) opened Windows file dialogs. Ctrl+X now writes the loadout to the `export` folder (named as the dialog suggested, `<mech>_<loadout>.m3c`), and Ctrl+I reads the next `.m3c` file from the `import` folder, then marks it done.

- The main menu's water ripple around a button builds its ring in a buffer
  as large as the screen; the original's held 1024x768, enough for its
  640x480 menus but not for menus at today's sizes.

- More screen sizes: besides the original 640x480 to 1600x1200 (all six
  now offered on a 3D card, not only the first three), 1280x720, 1600x900,
  1920x1080, 2560x1440, 3840x2160 and a size of your own (ScreenWidth and
  ScreenHeight in the settings). On a wide screen the view widens to the
  sides instead of stretching, keeping the original's look; 640x480 is
  exactly as before. The window, the HUD and the menus at those sizes come
  in later steps (docs/resolution.md).
- The picture keeps its shape in a window of any size: black bars at the
  sides (or top and bottom) when the window's shape differs, and the mouse
  moves the same distance whatever the window's size. A window opened
  without `--window` takes the picture's shape.
- The in-mission screen size (InGameVMode) is used: a mission opens at the
  size chosen in the graphics options, 16:9 sizes included, and the menus
  go back to 640x480. Shift+V steps through every size the screen offers.
- At the new sizes the HUD is laid out for the screen: the MFD, the weapons
  list, the timer and the gauges keep to the right and bottom edges, as the
  original's larger layouts put them. It grows with the screen's height,
  covering the same share of the screen as at 640x480 (1.5 times at 720p,
  2.25 at 1080p, 4.5 at 4K), and stays sharp.
- The first time the game starts, missions play at the desktop's own size
  (16:9 on most screens) instead of 640x480; a size chosen in the graphics
  options is kept from then on. The menus stay 640x480.
- On screens wider than 16:9 (21:9, 32:9) the game plays 16:9 at the
  screen's full height, with black bars at the sides: the cockpit was never
  made wider than that.
- Changing the resolution in the middle of a mission gives the view the new shape (the original never needed to: all its sizes were 4:3), and the options reached from a mission stay in their places at any resolution.
- The pause screen and the quit box over a mission at the new sizes are
  centred and drawn at the same scale as the HUD.
- On screens refreshing at 120 Hz or more, the main menu's buttons' ripple spreads as it should (it stayed a dot: each frame's growth rounded down to nothing).
- 16:9 menus: at a widescreen in-mission size the menus open at that size
  too, their buttons spread to the sides of the screen with their labels and
  lines, and the backgrounds filled out to the edges from the player's own
  art as the game runs (nothing on disk changes). On by default; `widemenus=0`
  in mw3.cfg or `--no-wide-menus` keeps the original's 4:3 menus. A size
  picked in the menus takes effect for the menus at the next start. The
  multiplayer connection screen and lobby spread the same way, the lobby's
  middle column widening between the other two.
- With the 16:9 menus, the credits (from the main menu and at the campaign's end) scroll inside their box, not to the left of it.

- The craters a host sends to a player joining a multiplayer game name their ground and texture by material number instead of by the host's memory addresses, which meant nothing on the joining machine (and do not fit a 64-bit program).

- A developer's leftover debug message ("haha", whenever an object named
  blackhawk_01 has its bounds rebuilt) is not printed. The 1999 game had no
  console to show it in; the rebuild would print it to the terminal.
- The controls' load and save list shows the profiles in your save folder
  (and any in the game folders) instead of asking Windows for the game
  folder's `keys` files, in alphabetical order as Windows lists an NTFS
  folder.

- The main menu's crawling background, sliding MECH3 logo and the ripples
  around its buttons move at the same speed at any frame rate (the 30 fps
  pace). The original moved them a step every frame, so on a fast computer
  the menu raced and the ripples vanished almost at once.

- Keyboard, mouse and joystick through SDL3 instead of DirectInput, read the
  same way: the mouse is captured while the window has the focus (as
  DirectInput's exclusive mouse was) and moves at the same speed whatever the
  window's size; a joystick gets the game's own ranges and dead zones.
- Graphics through SDL3 (Metal, Direct3D 12 or Vulkan) instead of DirectX 6;
  the game's own drawing code is unchanged.
- The fades (a mission's start, leaving it) take their still picture from
  the program's own copy of the screen instead of from DirectDraw; the colour
  over it is drawn as the original drew it. They look and last the same.
- The HUD and the other 2D drawing in a mission are laid over the 3D picture
  instead of being drawn into a locked DirectDraw back buffer; they look the
  same.
- The fade at a mission's start reads the picture back from the graphics card
  to fade in from it, as the original's DirectDraw grab did.
- Sound through SDL3 instead of DirectSound, with the same volumes and
  panning.
- Settings are saved in `settings.reg` in the user's folder instead of the
  Windows registry, with the same names and values (regedit's format: a
  Windows export of the game's keys can be dropped in). They are read even
  without an installer's key, which the original needed.
- The detail settings are picked for a modern machine described by the
  program (a fast processor with MMX, a 3D card, the real memory) instead of
  the original's processor timing and DirectDraw queries.
- A mission loads before its briefing appears, instead of in the
  background while the briefing plays (the graphics card wants the world's
  textures loaded from the game's own thread).
- Music from `music/trackNN.ogg` (or .flac, .wav, .mp3) instead of the CD;
  a ripped disc maps track for track.
- The frame clock counts from game start, not from when the computer booted,
  so the game no longer stutters on a machine that has been on for days.
- The software renderer's stack-pointer trick (drawing pixels with PUSH) is
  replaced by ordinary writes, pixel for pixel the same.
- Files are found through search folders, so replacement files can sit in
  front of the original install, or stand in for it.
- Controls profiles (`keys\<name>.zrd`) are saved in the user's folder
  instead of the game folder, in the same text format (a profile from the
  original game can be copied in).
- The game is its own program, `mw3`, in an ordinary window (SDL3) instead
  of a Windows program; it starts the way the original did, step for step.
  Saves and settings go to the user's application-data folder, not into the
  game folder.
- The keyboard, mouse and joystick are read through SDL3 instead of
  DirectInput, with every key reported under the same code the original's
  key tables use.
- The game's texts come from its own `Mech3Msg.dll`, read as data, and the
  menus' writing is drawn with the game's own fonts (Arial, Impact, Lucida
  Console, shipped with it) instead of Windows' text drawing.
- Message boxes are the computer's own instead of Windows'.
- A message box over the 3D picture (the mech lab's "Overwrite?", the
  mission's quit) shows that picture behind it at full size, as the
  original did; the rebuild's v1.0 showed the lab's box over a purple
  screen.
- The game's random numbers are generated exactly as on the original's
  Windows, so everything random behaves the same.
- Some rendering depends on the processor's math precision. In the emulator,
  at the 24-bit precision DirectX 6 sets, mech shadows, coloured lights on
  textures and the moving cloud shadows come out blank or black. On Windows
  11 the original shows mech shadows, so the rebuild draws what that runtime
  shows: mech shadows, coloured explosion and muzzle-flash light on textured
  surfaces, the ground lit by weapon fire, and moving cloud shadows. (On a
  1999 Windows 98 machine these may never have shown; that is unconfirmed.)

- Pilot files (and every saved file) are written in the rebuild's own
  folder; pilots from an imported install are copied there the first time
  they are opened, so their progress continues. Accented pilot names
  (Windows' own character set, as the original typed them) are stored as
  ordinary file names on macOS.
- In Direct3D the HUD's see-through images (compass, reticle) blend with the 3D picture behind them, as on the original; the rebuild's overlay layer had shown them with magenta fringes.
- Mechs use the full-resolution textures and show bullet holes, as a full
  install on a good card did. The rebuild answers the installer's question
  "what was installed" with everything (every texture set and sound
  fidelity); before, it said only the smallest texture set, so the mechs
  wore the reduced textures, took no holes and the audio screen offered no
  sound quality.

- The rebuild runs on any MechWarrior 3 data, 1.0 as well as 1.2, and says
  at start which it found. On 1.0's files it makes the 1.2 patch's changes
  itself as the data loads, in its own words and numbers, without the patch's
  files: so far the messages 1.2 added (target names, the new resolutions,
  the jump-jet limit), the HUD layouts for 1152x864, 1280x1024 and
  1600x1200, 1.2's weapon balance, the mechs' torso twist, heat sinks and
  loadouts, its force feedback effects (a fall and a death shake), the
  mission fixes (objectives, boundaries, a jump-jet ceiling, patrol points),
  the named targets, the footstep and engine sounds (the engine loop
  without its click, made from 1.0's own sound), and the AI paths and
  regions 1.0 left out of Op 4b Mission 5 (taken from your own Mission 6
  and 4 files, as 1.2 did), the other new AI paths, and 1.2's fixes to Op
  3's world (the dome's gaps, the helipads' stray wall, the terrain holes).
  Op 3's two new multiplayer maps, which 1.2 added as files of their own,
  are copied into your settings folder on the first start, so a 1.0 player
  has every 1.2 map too. Your files are not changed, and data that is neither 1.0's nor
  1.2's (a mod) is left as it is. Details in `docs/patch12.md`.
- A 1.0 install can be brought to 1.2 with the official patch file you
  already have (`--patch`, its ZIP or folder): the rebuild reads the patch
  without running its installer, checks every file against 1.2's, and
  keeps the result in its own folder, leaving the install as it was.
- On the first start the rebuild converts the game's movies itself (about a
  minute, with a progress window), when its importer is beside it; the
  original played them from the disc.
- On Windows the movies play through the game's own Indeo 5 codec from your
  install, in a small 32-bit helper beside the rebuild, so they look exactly
  as they did and need no conversion; the converted copies are only the
  fall-back. It can also turn a disc image's CD
  music into files the game plays.
- Each start writes everything the rebuild reports to `mw3.log` in the saves
  folder (the start before kept as `mw3.old.log`), for bug reports. It also
  says which version of the game's data it found, and on the first start
  shows a note when no music was found or the game files are of an unknown
  version, with what to do (1.0's need nothing: they play as 1.2).

- Setting the game up takes whatever you have (ours): one POINT TO YOUR GAME button takes your installed game, the disc, a disc image (.iso, .cue/.bin with its music) or the file you downloaded (.zip, .7z, .rar, even a zip inside a zip) and installs straight from it, with nothing to unpack by hand; it tells MechWarrior 3 from Pirate's Moon by the files themselves. Started directly, the game goes straight in when it finds your install, and shows only this setup screen when it does not.
- A launcher (ours; its own program, the main icon, which starts the game and steps aside while it runs; the game also starts directly) finds your MechWarrior 3 by itself and, when it cannot, sets it up from buttons instead of command lines: point to your game (with a progress bar and Cancel), apply the official 1.2 patch (offered only for game files of an unknown version), add the music from a disc image.

## Kept as the original, on purpose

Known quirks the rebuild deliberately keeps:

- Wading in any depth of water gives the full x8 cooling, and only the left
  foot counts.
- Elementals have one point of armor everywhere and die to a single hit.
- Missiles that miss vanish at the end of their range without exploding.
- Radio chatter plays the newest line first.
- A lancemate told to follow you or attack walks round a wall in the way instead of standing behind it (bug fixes on).
- A lancemate following you takes the way you walked, round corners and buildings and along canyons, instead of stopping against a wall or short of you (bug fixes on).
- The mobile field bases stay where you called them after a repair: the next mech (a lancemate told to repair, or you powering down on the spot again) is repaired without calling them again; the third MFB press still sends them away (bug fixes on).
- Every mech's engine tops out at 400.
- Jump jets lift you when you fire them walking uphill at more than 30 fps (the original could skim the ground with its jets burning).
- The reticle turns yellow over a target out to your selected weapons' range, past 800 m (the ER large laser, ER PPC, Gauss rifle and others reach further); the original stopped looking at 800 m (bug fixes on).
- A targeted building such as the Clan HQ in mission c3m1 shows on the radar and the map where it stands; the original pointed it at the map's zero point instead (bug fixes on).
- The movies play smoothly with their sound: each frame shows once, when the sound reaches it; the original repeated one frame and skipped one every three or so, because it looked at the sound only when its own clock ticked (bug fixes on).
- The movies no longer flash a frame from two seconds earlier every two seconds when they play through Windows' own Indeo codec: at each turn of the sound buffer the original read the sound's position as a whole buffer back (bug fixes on).
- Multiplayer runs over UDP with ENet instead of DirectPlay, which modern systems no longer have: games are found on the local network or joined by address (`IP:port`, the host's UDP port forwarded for internet play). IPX, modem and serial connections are gone.
- The multiplayer connection screen searches the local network as soon as it opens: games there show up without ticking Connect or pressing Find Game. Typing a host's address and pressing Find Game (or Enter) while it searches asks that host instead of stopping the search. The modem's Answer, Dial and Call buttons are gone, and the connection list has its one entry.
- Hosting a multiplayer game opens its UDP port (27930) on your router by UPnP when the router allows it, and closes it again when you stop. The connection screen shows the address to give your friends, with a click to copy it, and your LAN address; when the internet cannot reach you it says why (no router answered, the router refused, another router in front of yours, or your provider's carrier-grade NAT) and what to do.
- Joining a game with your player name already in use there is refused right after joining (the original checked before; our connection learns the names only on joining), with the same message.
- Another player's loadout that arrives cut short is read only as far as it goes, instead of past its end.
- Another player's record that arrives cut short makes no mech instead of being read past its end.
- A multiplayer update or shot aimed at something that is not another player's mech names no player, instead of whatever memory followed that object.
- A multiplayer update that arrives cut short is dropped, and its shots and damage are read only as far as it goes.
- With bug fixes on, other players' aim is carried forward by the measured lag as intended (the original mixed seconds and milliseconds, so it barely moved).
- With bug fixes on, a player whose mech is shoved to death by another player's collision credits that player with the kill.
- A multiplayer clock message that would end the game outside a network game is ignored instead of crashing.
- Messages from other players are checked before they are used, as anyone on the internet can send anything: a crater on ground that does not exist, an effect on objects that do not exist, more shots than a message holds, or a move the mech does not have are dropped instead of reading or writing past the game's tables.
- The multiplayer host no longer re-checks its locked game rules every 51 seconds (a guard against memory editing that ended the game).
- The multiplayer lobby no longer sends the scores to an online game service (the MSN Gaming Zone's score reporting, long gone).
- The multiplayer lobby shows the host its internet and LAN addresses, with a click to copy, as the connection screen does.
- The multiplayer lobby takes game settings only from the host, and checks them before use.
- The multiplayer lobby takes the start of a game only from the host, and reads the scores between games only as far as the message goes.
- The multiplayer lobby's chat reads each message only as far as it goes.
- In-game multiplayer chat lines are read only as far as the message goes, and control characters in them show as '?'.
- With bug fixes on, another player's 'Mech no longer throws its arms about on your screen after a hiccup in the connection (a delay longer than the ping's round trip was taken as a lag of 24 days).
- In a multiplayer match Escape shows the game's "Are you sure you want to quit?" box, which the original never drew; with bug fixes on it does not stop play (move and fire as normal), Y leaves the match for the main menu and N or Escape closes it. Before, an Escape in a match left the mouse unable to fire.
- The controls are saved only when you set them in the controls menus (ACCEPT, or loading, saving or resetting a set), not from whatever they are at the moment the game closes or the options are accepted. Before, closing the game while a multiplayer match's quit box was up saved the mouse's fire button unbound, and the mouse never fired again until the controls were reset. Controls already saved that way by an older build are mended when the game starts: if the fire command has no mouse button and the left button is bound to nothing, the left button goes back on fire (a fire button moved on purpose is left alone).
- A player who leaves a multiplayer game disappears for the others at once instead of after about half a minute.
- A multiplayer game keeps running while its window is minimised or in the background, so your 'Mech is not a frozen target for the others (the original paused it).
- On a Mac the numeric keypad types digits in text fields (an IP address on the connection screen); a Mac has no Num Lock, which the game needed on.
- A locked multiplayer game turns away players who try to join it, instead of only greying out its Join button.
- With bug fixes on, locking a multiplayer game with "Use stock 'Mechs" keeps the stock loadout a player chose instead of resetting it to the mech's first.
- For developers only, with `--debug-camera` (or `debugcamera=1` in mw3.cfg): Shift+F12 opens the game editor the original carries but never lets you reach (its menu over the mission, the view on the invisible "stealth" camera flown with the movement keys); Shift+F12 again or EXIT GAME EDIT closes it and puts the view back on your mech (the original left it on the stealth). The AI net and vehicle editors behind its menu are not there yet.
- The launcher closes once MechWarrior 3 is on screen, instead of waiting hidden for the game to end; if the game fails to start it stays and says so.
- The launcher is installed as "MechWarrior 3" (the Windows Start menu and desktop, the Mac's "MechWarrior 3.app", the Linux desktop entry); the game itself (mw3.exe) is installed too and starts directly, without a shortcut of its own.
- The launcher shows the games, GAME FILES and Quit (Escape too). GAME FILES opens the "find your game" screen at any time, to pick another copy or refresh it, and to add what it lacks from the disc; a new copy replaces the old only once it is complete. The launcher works with the arrow keys and a gamepad's d-pad, A and B as well as the mouse.
- Choosing a game whose files are not found opens a short "find your game" screen: FIND THE GAME searches the computer and the disc drives and uses what it finds; if nothing is there, one picker takes a disc image or download, or Mech3.exe (any file) of an installed copy. Once the game is found or installed it starts at once. The separate Setup, "a folder instead", "search again" and "apply the 1.2 patch" buttons are gone (1.0 plays as 1.2 by itself).
- On the Mac and Linux the movies are converted by the launcher right after the game is set up, with a progress bar and Cancel, instead of the game's first start waiting on a status line. The file picker no longer takes long to open on the Mac.
- An install whose movies stayed on the CD, or with no music, is told so before the first game, with ADD FROM MY DISC: it takes the movies from your disc in a drive, a disc image or the disc's folder, and the music from a .cue disc image.
- On a wide screen the movies keep their shape: the intro and the ending movie, whose black bars are part of the picture, fill the screen's width; the 4:3 movies and the logo screens are centred with black sides (they were stretched a third too wide). The shipped movies' shapes come with the game (measured by us from the pictures), so nothing is measured when they play; a movie the game does not know (a mod's) is measured after it has played, in the background, and kept in movie-shapes.txt in the settings folder. Every movie's first play on Windows no longer freezes for 10 to 30 seconds while this was measured.
- Mods: folders of game data in the settings folder's mods\ folder, enabled in mw3.cfg (mod=NAME, in order), sit in front of the game's files; a mod's loose .zrd replaces one entry of a reader archive. A mod can never replace a player's pilots, loadouts, controls or settings, and with no mods enabled nothing changes (docs/modding.md).
- A multiplayer game needs every player's gameplay mods to match: a player joining with other mods is told which mods the host uses and which they use, and LOCK OPTIONS names the player whose mods differ. Text, image, sound and music mods never count.
- Mods can add new worlds (world= in mod.txt) and Instant Action maps on any world (ia_world=), and change the game's text (strings.txt, over Mech3Msg.dll's messages); a pilot whose Instant Action world came from a mod since removed plays on the first world.
- A mod's new world can have multiplayer maps (readermp3.zbd and on), listed in the lobby with the others.
- A broken entry in a reader archive (a mod's, say) is refused and named in the log instead of being read into whatever follows it; `mw3 --check-mod DIR` reads a mod as the game would and reports what it would refuse.
- A broken texture or image library, world texture directory, sound or script file (a mod's, say) is refused and named in the log instead of being read past its end; a refused texture or sound is a missing one, a refused script line ends its script. `mw3 --check-mod` looks inside these files too.
- The mouse settings, like the controls, are saved only as you set them in the menus.
- The controls screen's KEY A and KEY B columns take a key again: click the box, press the key (v1.0 never saw it; the mouse and joystick columns were not affected).
- A MOUSE SENSITIVITY slider on the Graphics & Audio screen, under the volume sliders (0.5 to 3.0): it sets how far the mouse turns the mech in a mission. It starts at the original's 1.3, and Invert Mouse keeps it.
- The music pauses while a movie plays and comes back after (the campaign's opening movie no longer has the menu music under it), as when the game read its movies off the CD.
- On a wide screen the arm weapons reach the reticle across the whole width: an arm weapon still fires straight ahead when aimed far off to the side, but the limit now widens with the screen, so it starts at the same place on screen as on a 4:3 one (bug fixes on).
- Cheat codes in single player (ours): Shift+Enter, Keypad * or Keypad − in a mission opens a box for six codes, from a radar that shows everything to an instant win (README, Cheat codes).
- Shooting off an arm with a weapon in its hand no longer reads memory the game has just freed (harmless on Windows 98, a possible crash today).
- A crash late in a fight (seen in Operation 4's last fight): the graphics' matrix stack no longer writes through a bad entry when it is pushed or popped out of step; the slip is refused and logged instead, and the stack is cleared each frame. Its cause was the rebuild's own (a shortcut that took a moving mech's position on the shared stack instead of on its own), and that is fixed too.
- A lancemate no longer gets thrown back by his own feet when you are in another part of a mission (bug fixes on): he used to lose sight of the floor under him and read the land above as a wall. In the mission where you meet Epona Rhi he now follows you through the tunnel instead of standing at its mouth for ever.
- Lancemates find their way when their straight walk fails (bug fixes on, single player): round walls and parked vehicles, up ramps and along narrow walkways, and across a river at its bridge or ford, for follow, attack and repair orders. A lancemate will now come to you inside the areas the original told the AI to keep out of, if there is a way. They notice doors opening and buildings falling. On open ground they walk exactly as before.
- With no joystick attached, a saved controls file with axes on the stick no longer makes your mech walk or its torso turn by itself (bug fixes on). The original moved the axes off the stick only for a file exactly like the defaults; a slider, or just a changed deadzone, kept them. The binding is kept for when the stick is back.
- On fast machines, small bumps throw vehicles much less: the suspension's reaction to the ground is capped at what it is at 30 fps. On rough ground they still tip more than at 30 fps, and they still jump off steep banks at full speed now and then, as in the original.
- Linux: the game no longer closes at the end of the intro movies. The launcher closed when the game appeared, which unmounted the AppImage the game was still running from; it now waits, out of sight, until the game ends.
- A crash now says so, in the log and in a message box that names the log, as does a start that cannot open its window or graphics. It used to just close.
- Linux: an install whose files are in another letter case (all lower case, all upper case, mixed) now works. Windows ignores case, so the game's names never had to match; on Linux it could not find them.
- A softer message when files match neither 1.0 nor 1.2: they look modded or from another version, data mods are read but may not play right, and a mod that patches the old Mech3.exe won't apply. It never stopped the game, and still doesn't.
- Your data folder (settings, pilots, saves, converted movies) can live on another drive: GAME FILES, MY DATA FOLDER in the launcher copies it, checks the copy, then switches over, and removes the old copy only if you say so. If the drive is missing at start the game says so instead of starting afresh.
