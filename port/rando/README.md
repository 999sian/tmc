# Native randomizer

Fresh Picori seeds use the Picori-authored `assets/rando/picori.logic` rules
for item pools, location requirements, placement, and reachability. The
parser and generator run inside `tmc_pc` without patching the ROM. Picori
uses its own reproducible seed sequence. `build.py` stages the rules in full
and slim distributions. The engine's GPL-3.0 provenance and attribution are
in `LICENSE` and `THIRD-PARTY-LICENSES.md`.

## Current profile

The default profile shuffles at least 259 reward checks. Every active
shuffled check needs a unique in-game award key before a seed can activate.
Chest rules already use native chest ordinals, which the runtime validates
against the active ROM. Ground items and scripted rewards use the bindings in
`rando_keymap.c`. `Rando_OverrideLocationKey` awards the placed item, and
`Rando_VerifyCurrentSeed` checks the active parser table against the authored
item/check reachability graph. This does not prove every check is accessible
in a full gameplay run.

The three item pool choices are:

| Menu choice | Logic `ITEM_POOL` |
| --- | --- |
| Balanced | `ITEM_POOL_NORMAL` |
| Reduced | `ITEM_POOL_RIP` |
| Plentiful | `ITEM_POOL_PLENTIFUL` |

Goal-only reachability is the supported target. The all non-key and all-check
targets are unavailable until shop, event, and fusion access is modeled in the
rules. Dojos, starting sword, and item pool choices are passed as `.logic`
overrides. Open world applies starting flags and runtime shortcuts. Fresh seed
generation currently rejects entrance shuffle and dungeon-item shuffle. By
default, a new file starts with the Smith Sword and skips
Zelda's intro; the normal Hyrule Town room properties load on that path.

To start a seed, press **L** on the file screen, enable **Randomizer Mode**, then
select an empty save slot. The new-file setup offers **Generate & Start**.
For an active seed, open **F8 → Randomizer → Spoiler log** to view or copy its
placements. **Save .txt** writes the full log to a `spoilers/` folder beside
the active save profile. Chest and ground entries include the area, room, and
room-local tile or pixel position; scripted rewards name the NPC, shop, or
lesson. The log is rebuilt when a randomizer save is loaded.

Progressive sword, bow, boomerang, shield, and scroll awards are disabled in
this native profile because their upgrade behavior is not implemented. A
requested unsupported award rejects generation. Glitched logic and
kinstone-off generation are also currently rejected.

## Code and saves

- `rando_logic.cpp` parses `.logic`, places items, evaluates reachability,
  and checks accessibility. `rando.cpp` activates its table and routes awards.
  Its older native location graph remains only for loading legacy saves.
- `rando_runtime.c` validates native chest ordinals and ground flags against
  the active ROM; `rando_keymap.c` binds scripted awards. New-file flags and story
  skip live in `rando_newfile.c` and `rando_runtime.c`.
- `rando_file_menu.c` and `port_imgui_menu.cpp` expose new-file and F8
  settings. `rando_save.c` stores each slot in a profile-local `.randomizer`
  sidecar without changing the vanilla save layout.

Sidecar v8 stores up to 4096 parser entries, their item subtypes, settings,
`.logic` overrides, and a source fingerprint of the logic bytes plus
overrides. Loading refuses a parser seed when that source no longer matches.
Parser seeds created with the removed rules file are not automatically
migrated to `picori.logic`. Older v6 (211- and 228-entry) and v7 (228-entry)
sidecars still load through the legacy table and migrate when rewritten.
Share the seed, settings, and logic file together for matching Picori
placements. A `PICORI-RANDO` marker in
unused save padding prevents a randomizer slot with a missing or incompatible
sidecar from silently starting as a vanilla game.

Chest validation reads the active region's room data, and named starting flags
use regional constants. The ground and scripted key map was derived from USA
ROM data. Other regions and a full playthrough still need gameplay validation.

## Build and check

From the repository root, with the required valid ROM and assets:

```bash
python3 tools/verify_picori_rules.py build/pc/baserom.gba
xmake build -y rando_logic_test
./build/pc/rando_logic_test
python3 build.py --usa
cd dist/USA
TMC_REPRO_RANDO=1 TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./tmc_pc --no-audio
```

The ROM-backed rules check requires a USA baseline ROM. It verifies active
Picori pickup keys against ROM chest ordinals, ground items, and scripted
bindings. The offline test checks deterministic placement, native reward keys, the
three pools, Obscure checks, unsupported-setting rejection, spoiler logs,
and legacy table activation.
The headless repro checks file-select generation, default award bindings,
save/reload, town story-skip state, collection flags, and runtime key
validation; success prints `[rando-repro] ALL STAGES PASS`. It starts at file
select and does not replace a full new-game playthrough or verify shop and
kinstone fusion access in gameplay.
