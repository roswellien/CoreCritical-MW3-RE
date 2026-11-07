# Authored modular gameplay examples

These schema-3 source packages contain project-authored
definitions and one generated checker texture. They contain no original models,
textures, motions, game archives or generated game code. Native donor content
comes from the player's installation at runtime.

| Package | Demonstration |
| --- | --- |
| `demo-equipment` | New ECM identity with a distinctive 2.25-ton mass and three critical slots, using the existing MW3 effect |
| `demo-weapons` | New Medium Laser identity with native damage set to 12.0 |
| `demo-chassis` | Independently selectable Bushwacker-based chassis with a loadout referencing both preceding packages |
| `demo-paint` | New skin and external named RGB565 pixels, applied across the donor's material slots |
| `demo-vehicles` | Independently named truck definition using an existing native AI controller family |
| `demo-assets` (`assets/`) | Separate reusable authored marker, bridge deck/support collision geometry and truck body meshes |
| `demo-worlds` (`worlds/`) | Heightfield, variant, standalone missions and multiplayer arena referencing the reusable asset package |
| `demo-campaigns` (`campaigns/`) | Branching campaign using the new chassis/loadout/skin and authored missions |

`gameplay-profile.json` selects five gameplay packages and their reusable asset
dependency by relative source path. Its
dependency declarations remain the authority for composition. Resolve it with:

```text
mw3content resolve examples/modular/gameplay-profile.json --store examples/modular/store
```

The resulting registry includes `demo-chassis:scout`, its `Modular Demonstration`
loadout, `demo-weapons:laser`, `demo-equipment:guardian`,
`demo-paint:checker_skin` and `demo-vehicles:truck`. Open the native Mech Lab or
Instant Action with the same profile to select the additional chassis. The
stock Bushwacker retains its existing identity and loadouts. The authored world
and campaign examples can select these definitions through stable IDs.

```text
mw3 --content-profile examples/modular/gameplay-profile.json --inspect-content
mw3 --content-profile examples/modular/gameplay-profile.json
```

The truck requires a mission placement with an implemented positive AI-net
reference and real model geometry. The vehicle binds `asset: demo-assets:truck_body`;
the native factory constructs its models and helper nodes at local origin,
then the AIV placement positions `demotruck_01` once. No map-specific truck
template is required. The native donor reader supplies the
existing controller and health parameters. The reusable truck asset supplies
four authored wheel support points and twelve hull collision points through
real native marker nodes. A vehicle
definition does not import a missing stock world model. Required semantic actor
construction failures stop mission loading visibly. Parked/player controls are unavailable. Each package can also be
validated, installed and packaged independently with the shared content CLI.

`showcase-profile.json` adds the world and campaign packages to that set.
Its bridge contains an elevated deck
and two supports with native ground/collision geometry. The truck's hull,
cabin, bed and wheels are project-authored box meshes; no original vehicle
art is redistributed.

The checker payload is exactly reproducible with the adjacent
`demo-paint/make_checker.py` script using Python's standard library. The package
integrity table records the payload's size and SHA-256. Python is an authoring
convenience; the native game reads the prepared payload directly.

Resolver success establishes package composition and supported metadata. Live
selection, movement, firing, damage, AI, save/load and multiplayer require the
corresponding native validation. See `docs/native-content-definitions.md` for
the native field and behavior contracts.
