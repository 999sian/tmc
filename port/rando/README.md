# port/rando/ — In-process clean-room randomizer

Native randomizer logic that runs inside `tmc_pc`. It does not patch a GBA
ROM, shell out to the GPL C# randomizer, or allocate heap containers during
generation.

When a seed is active, every give-item source (chests, NPC gifts, drops) is
routed through `Rando_OverrideItem`, which applies a **seeded pool-preserving
item bijection** — so real in-game rewards change. The bijection is
difficulty-scaled:

- `Normal` — shuffles collectibles only (rupees, hearts, kinstones, ammo,
  shells, heart pieces). Progression is untouched, so the seed stays beatable.
- `Hard` — also shuffles non-gating majors (bottles, upgrades, skills, etc.).
- `Chaos` — also shuffles dungeon-gating progression (may be unbeatable without
  a logic file).

For true per-location logic, supply a MinishMaker-style `.logic` file
(`TMC_RANDO_LOGIC=/path`); chests are then remapped per area/room/flag key via
`Rando_OverrideLocationKey`.

Primary API:

```c
RandomizerSettings settings = Rando_DefaultSettings();
bool ok = GenerateSeed(seed64, settings);
uint16_t item = Rando_ResolveLocationItem(location_id, vanilla_item);
/* Optional MinishMaker-style address hook: 0xAARRLL = area, room, local flag. */
Rando_OverrideLocationKey(0x000001, &item_type, &item_subtype);
```

## Current architecture

- `rando.h` — C ABI, location/category enums, settings, and table access.
- `rando.cpp` — SplitMix64 PRNG, the seeded pool-preserving item bijection
  applied via `Rando_OverrideItem`, plus a built-in location graph used for the
  spoiler/self-verification and offline tests.
- `rando_logic.h/.cpp` — MinishMaker-style `.logic` parser and expression
  VM for public-format interoperability without translating GPL C# code.
  Startup probes `TMC_RANDO_LOGIC`, `assets/rando/default.logic`,
  `dist/USA/assets/rando/default.logic`, `dist/EU/assets/rando/default.logic`,
  `rando/default.logic`, then `default.logic`. The GPL logic file is not
  vendored.
- `rando_file_menu.h/.c` — SDL3 file-select setup overlay using fixed
  element arrays and `SDL_FRect` hitboxes.
- `rando_save.h/.c` — profile-local `*.randomizer` sidecar storing seed,
  settings, active location count, and fixed item table for all three save
  slots without touching EEPROM.
- `rando_test.c` — deterministic generation/self-verification test target.

The location graph is deliberately native and data-oriented:

- every built-in location has a category, vanilla item, and bitmask
  requirements;
- parsed `.logic` data has fixed symbol/item/location/node arrays and supports
  conditionals, default UI settings, backtick expansion, replacement/type
  directives, count/AND/OR/NOT logic expressions, and location key lookup;
- progression, major, and junk pools are separate fixed arrays;
- generation uses stack scratch arrays only;
- verification simulates a playthrough before a seed becomes active.

## MinishMaker `.logic` parity status

The `.logic` engine is a clean-room reimplementation of the documented
MinishMaker format/algorithm (written from the public spec block at the top of
`default.logic`, not from the GPL C# source).

Implemented and tested (`rando_logic_test`):
- expression grammar: base-level AND (`,`), `(| …)`, `(& …)`, weighted count
  `(+N, X:2, …)`, `~X`, arbitrary nesting, `Items.`/`Locations.`/`Helpers.`;
- directives: `!flag`/`!dropdown`/`!numberbox` (defaults), `!define`/`!undefine`,
  `!ifdef`/`!ifndef`/`!else`/`!endif`, backtick expansion, `!addition`,
  `!replace`/`!replaceamount`/`!replaceincrement`, `!settype`;
- item pool syntax `Items.type.subtype:amount.multiplier` (ceil amount/mult);
- address forms `area-room-chest` and precise `xxxxxx`; dungeon-id stripping;
- typed item/location pools with the documented fallback acceptance matrix;
- **assumed-fill** placement (advancement = items referenced in logic) that
  guarantees beatable seeds, in the documented type-priority order;
- `~Items.X` treated as a **placement guard** (not a reachability term);
- accessibility modes (`ACCESS_BEATABLE` = goal only, otherwise all-locations);
- `NO_LOGIC`; entrance/constraint pool-exhaustion failure;
- graceful unmapped items: a symbol with no native engine id leaves that
  location's vanilla reward in place (logic still counts it);
- symbol→engine-item map covering the documented item pool (swords, weapons,
  gear, bottles, quest/key items, elements, scrolls, dungeon items, upgrades,
  foods, rupees/ammo/hearts/shells/kinstones, progressive items), using the
  authoritative `Item` enum shared via `include/item_ids.h` (no hardcoded id
  copies);
- reward write-back hooks: small chests, big chests, and freestanding ground
  items are remapped per-location. Chests use the MinishMaker chest identity
  `area-room-chestIndex` (the third byte is the chest's 0-based position among
  the room's chest TileEntities, resolved by `Rando_RoomChestIndex`), so real
  `.logic` placements land on the right chest in-game. Every other give-item
  source (NPCs, shops, dojos, fusions, drops, …) is randomized via the global
  `Rando_OverrideItem` pool bijection;
- `!ensurereachability` is executed: when present (or `ACCESSIBILITY =
  ACCESS_LOCATIONS`), every location is verified reachable, not just the goal.

NOT yet at full parity (honest gaps):
- `!import` logic functions are approximated (logic-only item symbols are
  assumed owned, standing in for `LogicImport.cs` — not translated, clean-room);
- `!prizeplacement`, `!eventdefine`, `!color` are parsed but not executed;
- exact dungeon/region/keysanity item-mode semantics and entrance shuffle;
- per-location keyed hooks for non-chest/non-ground reward types (those use
  MinishMaker precise ROM addresses rather than `area-room-chestIndex`);
- byte-for-byte spoiler/placement parity (needs the real `default.logic` data
  plus reference spoilers to diff against).

### Real `default.logic` status

Validated against the actual MinishMaker `default.logic` (set
`TMC_RANDO_LOGIC=path`; `rando_logic_test` then asserts it). The full file
(882 locations / 176 items at default settings) **parses correctly and fast**
and the engine **generates a deterministic seed end-to-end with all 365 real
locations verified reachable** (`!ensurereachability`) — the assumed-fill
places every pooled item, filler covers the rest. `TMC_RANDO_DEBUG=1` adds
`[gen]` placement traces. In-game, the runtime chest hooks resolve to the
logic's chest identity: the `TMC_REPRO_RANDO=1` harness (with `TMC_RANDO_LOGIC`
set) confirms 144/161 keyed chests match real engine chests and 128 receive
their `.logic`-placed item at the title-screen probe frame (more at actual play
time, once distant areas are resolved). The harness also confirms a real-logic
seed survives a sidecar save/reset/reload round-trip (chests stay randomized
across save+restart).

Approximations still in effect (don't block beatable generation, but are not
full parity):
- Item symbols that appear in logic but aren't in the shuffled pool (start
  items, fixed grants, and unmodelled events/`!import` results) are treated as
  owned in both placement and verification. This stands in for the complete
  event graph + the `!import` functions defined in MinishMaker's
  `LogicImport.cs` (not translated — clean-room).
- `!prizeplacement` is parsed; dungeon-prize items are placed in prize/dungeon
  pools without the per-location dungeon redirect.
- 156/176 item symbols map to native engine ids (gear, elements, scrolls,
  heart pieces/containers, subtyped `BigKey`/`SmallKey`/`Compass`/`DungeonMap`,
  butterflies, progressive bases, …); the remaining ~20 are non-reward symbols
  (kinstone fusions, figurines, music, entrance/trap dummies) that leave their
  vanilla reward in place. At default settings every keyed chest resolves to a
  native item (probe: 161/161 keyed locations overridden).

## Build / test

```bash
xmake build -y rando_logic_test
./build/pc/rando_logic_test           # offline: generation determinism + .logic parser/VM
```

The main build links the module directly:

```bash
xmake build -y tmc_pc
```

End-to-end runtime check (drives the file-select overlay, generation, the
`Rando_OverrideItem` give-item hook, and the `Rando_OverrideLocationKey`
address hook, then asserts items actually change):

```bash
TMC_REPRO_RANDO=1 TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/pc/tmc_pc --no-audio        # prints "[rando-repro] PASS", exits 0
```

### Backend note

The file-select setup overlay draws via SDL_Renderer 2D primitives, so it only
engages on the **software / SDL_Renderer** backend. The default `render_backend`
is `auto`, which selects **SDL_GPU** whenever the platform supports it (most
desktops, Steam Deck) — and the GPU/surface backends do not present these SDL
primitives. To prevent a freeze (the overlay masking all input behind an
undrawn menu), `Port_RandoFileMenu_ShouldOpenForNewFile()` checks
`Port_PPU_OverlaysUseRenderer()` and only auto-opens on the SDL_Renderer
backend; otherwise the new-file flow stays vanilla.

To use the file-select overlay, set `render_backend` to `software` in
`config.json` (or the F8 renderer setting). On any backend, **F8 → Randomizer**
rolls a seed and works because the F8 menu renders through ImGui on GPU too.