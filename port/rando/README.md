# Native randomizer

Fresh Picori seeds use the bundled `default.logic` for item pools, location
requirements, placement, and reachability. The parser and generator run inside
`tmc_pc` without patching the ROM. Picori uses its own reproducible seed
sequence.

`assets/rando/default.logic` has SHA-256
`5139bef94b633105758bfb3e3cbca888b1a3f525df00cdba9523b62d1d2218f5`.
`build.py` stages it in full and slim distributions. GPL-3.0 provenance and
attribution are in `LICENSE` and `THIRD-PARTY-LICENSES.md`.

## Current profile

The bundled logic parses 883 locations and 517 helpers. The default native
profile shuffles 259 reward checks. Picori's Obscure Locations setting controls
upstream `RUPEEMANIA`, `SPECIALPOTS`, `DIGGING`, and `UNDERWATER` together.
Enabling it raises the count to 352: 93 more one-time pickups from rupees,
pots, digging, and underwater spots. Every active shuffled check needs a
unique in-game award key before a seed can activate. Chests use room data to
translate logic TileEntity indices to Picori's chest ordinals; ground
items and scripted rewards use the bindings in
`rando_keymap.c`. `Rando_OverrideLocationKey` awards the placed item, and
`Rando_VerifyCurrentSeed` verifies the active parser table against its seed.

The three existing Picori pool choices select these upstream pools:

| Picori choice | Logic `ITEM_POOL` |
| --- | --- |
| Normal | Balanced (`ITEM_POOL_NORMAL`) |
| Hard | Reduced (`ITEM_POOL_RIP`) |
| Chaos | Plentiful (`ITEM_POOL_PLENTIFUL`) |

`shuffle_dungeon_items` enables logic small-key, big-key, map, and
compass keysanity together. Dungeon items retain their dungeon of origin in
the award subtype. Accessibility, coupled entrance shuffle, dojos, open
world, starting sword, and other supported settings are passed as `.logic`
overrides. By default, a new file starts with the Smith Sword and skips
Zelda's intro; the normal Hyrule Town room properties load on that path.

To start a seed, press **L** on the file screen, enable **Randomizer Mode**, then
select an empty save slot. The new-file setup offers **Generate & Start**.
For an active seed, open **F8 → Randomizer → Spoiler log** to view or copy its
placements. **Save .txt** writes the full log to a `spoilers/` folder beside
the active save profile. The log is rebuilt when a randomizer save is loaded.

Progressive sword, bow, boomerang, shield, and scroll awards are disabled in
this native profile because their upgrade behavior is not implemented. A
requested unsupported award rejects generation. Glitched logic and
kinstone-off generation are also currently rejected. Other upstream option
combinations remain extension work.

## Code and saves

- `rando_logic.cpp` parses `.logic`, places items, evaluates reachability,
  and checks accessibility. `rando.cpp` activates its table and routes awards.
  Its older native location graph remains only for loading legacy saves.
- `rando_runtime.c` resolves chest keys from the active ROM's room properties;
  `rando_keymap.c` binds ground and scripted awards. New-file flags and story
  skip live in `rando_newfile.c` and `rando_runtime.c`.
- `rando_file_menu.c` and `port_imgui_menu.cpp` expose new-file and F8
  settings. `rando_save.c` stores each slot in a profile-local `.randomizer`
  sidecar without changing the vanilla save layout.

Sidecar v8 stores up to 4096 parser entries, their item subtypes, settings,
`.logic` overrides, and a source fingerprint of the logic bytes plus
overrides. Loading refuses a parser seed when that source no longer matches.
Older v6 (211- and 228-entry) and v7 (228-entry) sidecars still load through
the legacy table and migrate when rewritten. Share the seed, settings, and
logic file together for matching Picori placements. A `PICORI-RANDO` marker in
unused save padding prevents a randomizer slot with a missing or incompatible
sidecar from silently starting as a vanilla game.

Chest binding reads the active region's room data, and named starting flags
use regional constants. The ground and scripted key map was derived from USA
ROM data. The automated runtime repro below passes on USA and JP; EU and a
full playthrough still need gameplay validation.

## Build and check

From the repository root, with the required valid ROM and assets:

```bash
xmake build -y rando_logic_test
./build/pc/rando_logic_test
python3 build.py --usa
cd dist/USA
TMC_REPRO_RANDO=1 TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./tmc_pc --no-audio
```

The offline test checks deterministic placement, native reward keys, the
three pools, keysanity, entrance assignments, and legacy table activation.
The headless repro checks file-select generation, all 259 default award
bindings, all 352 bindings with Obscure Locations, save/reload, town
story-skip state, collection flags, and runtime chest translation; success
prints `[rando-repro] ALL STAGES PASS`. It starts at file select and does not
replace a full new-game playthrough.
