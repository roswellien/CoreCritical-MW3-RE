MechWarrior 3 Rebuild for macOS
===============================

A fan rebuild of MechWarrior 3's program: the original game's logic, rewritten
to run natively on today's computers. A personal learning project: free, not
sold, and not affiliated with or endorsed by MicroProse, Microsoft or anyone
who holds the MechWarrior name.

It contains no part of the game: you need your own MechWarrior 3, on its disc,
as a disc image (.iso, or .cue with its .bin files) or a download of one
(.zip, .7z, .rar), or an installed copy from a Windows PC.

Installing
----------
It runs on macOS 12 (Monterey) or later, on Apple silicon and Intel Macs.
Drag "MechWarrior 3" into Applications and start it.

The first time, macOS says it cannot check the app for malicious software
(it is a free fan project, not registered with Apple). Open it once this way:
right-click (or Control-click) the app, choose Open, then Open again. Or, after
macOS has blocked it once: System Settings > Privacy & Security, and click
"Open Anyway" beside its name. After that it starts normally.

On the first start it looks for your installed copy and goes straight into
the game when it finds one. If not, its setup screen's POINT TO YOUR GAME
takes whatever you have: the disc, a disc image (.iso, or .cue with its .bin
files, whose CD audio becomes the music), the file you downloaded (.zip, .7z
or .rar, even a zip inside a zip; nothing to unpack by hand), or a copy from
a Windows PC. It says what it found, installs it with a progress bar and
Cancel, remembers it, and goes on into the game.
Then it converts the game's movies once (about 3 seconds each, with a
progress bar; closing it stops, and the next start does the rest) into the
folder below, so the intro and the mission movies play.

Your settings and saves
-----------------------
In ~/Library/Application Support/MicroProse/MechWarrior 3/ (in Finder: Go >
Go to Folder). It holds mw3.cfg, your pilots and saves, the settings, the
installed game, its music, the converted movies and mw3.log (what the game reported while it ran;
send it with a bug report). Deleting the app keeps this folder. To keep it on
another drive: in the launcher, GAME FILES, MY DATA FOLDER.

Licences
--------
The libraries this app uses and their licences: THIRD-PARTY.txt inside the
app (Contents/Resources), with their licence files in Contents/Resources/licenses.
