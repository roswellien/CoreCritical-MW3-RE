# Direct native arena playtests

A mission with `data.multiplayer: true` can use the existing native network
playtest route through `--content-mission`, `--host` or `--join`, and
`--net-wait`. This constructs the pilot's `MultiplayerGame` and uses native
player/loadout replication, spawn selection and score traffic. The production
multiplayer lobby remains available separately.

The project-authored `examples/modular/arena-profile.json` selects an additional
chassis, its named loadout, and the authored checker skin:

```text
mw3 --content-profile examples/modular/arena-profile.json --content-mission demo-worlds:arena --host --net-port 27888 --net-wait 2 --net-name Host --write-dir PATH_TO_HOST_WRITES
mw3 --content-profile examples/modular/arena-profile.json --content-mission demo-worlds:arena --join 127.0.0.1:27888 --net-wait 2 --net-name Joiner --write-dir PATH_TO_JOINER_WRITES
```

Both processes require the usual verified local installation and separate write
directories. Optional existing `--net-drive SEED`, `--net-seconds N`, `--fire N`,
`--verbose` and capture options exercise the native route. A transport-only
test is not evidence of live movement, firing, damage or respawn; retain the
native peer logs and captures for those acceptance claims.

`selections.multiplayer` is optional. When present it must contain exactly
`chassis`, `loadout` and `skin`, all nonempty strings. Chassis and skin use stable
content identities. The loadout is the exact native configuration name scoped to
that chassis. The shared native validator rejects unknown fields or missing
identities. At launch the installed native definition must contain that loadout
and skin; failure occurs before constructing the arena world. Without this
selection, the pilot's existing multiplayer slot is used.

Before construction, each process publishes the production lobby content
envelope and selected arena handle. At least two peers (or the larger
`--net-wait` count) must be present. Each peer must match the resolved gameplay
digest, session mapping, admitted base gameplay resources and native simulation
options. Missing records wait for up to 120 seconds; different content, malformed
records or different arena selections fail visibly. Closing the application
cancels the wait. If an accepted peer reaches construction first, its native
kind-2 loadout record is fully validated against those same digests and its
chassis before it can satisfy the remaining peer's check.

This shortcut uses the existing debug lobby callback adapter for direct native
playtests. It does not replace production UI lobby lock/ready or team workflows.
