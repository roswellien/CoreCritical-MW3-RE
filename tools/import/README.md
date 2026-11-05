# mw3import

The first-run importer's movies (docs/fmv.md, The imported movies). Our
addition, not original code: it converts the install's `video\*.AVI` (Indeo 5,
which nothing on macOS decodes) into files the rebuild plays.

```
build/mw3import [--install DIR] [--write-dir DIR] [--only NAME]
```

- `--install`: the MW3 1.2 folder (default: `game/mw3-1.2`).
- `--write-dir`: where the game's files go (default: the game's own, SDL's
  pref path for "MicroProse" / "MechWarrior 3", or the data folder it points to, as `mw3` uses). The movies go
  in its `video` folder, which the game searches first. They never go in the
  repo.
- `--only`: one movie by name (`c1m2`).

For each movie it writes `<name>.m1v` (the picture, MPEG-1 at 30 fps, every
frame twice), `<name>.wav` (the AVI's own sound, byte for byte) and
`<name>.fmv` (the AVI's stream headers and key frames). The 19 movies take
about a minute.

Needs:

- FFmpeg's libraries (libavformat, libavcodec, libswscale, libavutil) with
  pkg-config: `brew install ffmpeg pkg-config`. CMake builds `mw3import` only
  when it finds them; the game itself does not need FFmpeg (it decodes the
  pictures with the bundled pl_mpeg).
- SDL3, for the default write dir.
