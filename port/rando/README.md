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
  ACCESS_LOCATIONS`), every location is verified reachable, not just the goal;
- the file-select overlay enumerates the logic's declared `!flag`/`!dropdown`/
  `!numberbox` settings and lets the player change them; each change is applied
  as a define override + reparse, so the menu genuinely drives real-logic
  generation (e.g. toggling `FIGURINE_HUNT` adds figurines to the pool).

Full-parity features (added in the 1:1 pass):
- **dungeon-id tag binding**: location name fields carry `:Tag` capabilities
  and item lines bind to one tag via their third field — the entire
  keysanity/dungeon-item-mode matrix (`Own Dungeon`/`Own Region`/`Vanilla`
  pins/`Anywhere`) is honoured as data, exactly as the file expresses it;
- **`!prizeplacement`** executed: a prize assigned to a redirected prize
  location is placed within the redirect tag pool instead (or anywhere with no
  dungeon id), the prize slot joins the Dungeon pool, and each rule fires at
  most once per generation. Prize items otherwise place ONLY on DungeonPrize
  locations per spec (a previous direct-to-Dungeon fallback was a bug);
- **`!eventdefine`** parsed and evaluated (`RandoLogic_EvalEventDefine`):
  values are stored raw, every `RAND_INT` occurrence substitutes a
  seed-derived value (per-occurrence, deterministic), and a C-like integer
  expression evaluator handles the file's `(x >> 5) & 0x1F` forms. Runtime
  consumers (`rando_runtime.c`): start inventory (full `startInventory*`
  mapping incl. bottles/kinstones/quivers/wallets), wind crests, dungeon
  warps, instant text, `dmgMulti`/`heroMode` damage scaling, low-health-beep
  and `no_music` mutes;
- **`!color`** parsed (defaults + `NAME_X` define overrides); tunic and heart
  colors apply at runtime via content-addressed palette overrides
  (`rando_cosmetic.cpp`), including rainbow hearts;
- **entrance shuffle**: generation records `Items.Entrance.*` assignments and
  `rando_entrance.cpp` performs coupled swaps at the engine's transition choke
  points;
- **sidecar v2/v3** persists the seed's `.logic` define overrides, entrance
  assignments, and per-area music assignments per slot, so a reloaded save
  restores its full context (guarded by a parse fingerprint);
- **dropdown option-value flags**: choosing a dropdown option defines the
  option's VALUE token as a flag (in addition to the setting define), which is
  what activates `!ifdef - SMALL_KEYS_STANDARD` / `MUSIC_RANDO` style branches
  throughout the file — without this, the whole keysanity/music sections were
  silently inert;
- **music shuffle** (`MUSIC_RANDO`): generation assigns `Items.Music.0xNN`
  song ids to the 141 `Area%xMusic` slots; `rando_music.c` remaps
  `gAreaMetadata[].queueBgm` at the engine's only reader (`LoadRoomBgm`) —
  same id space, no translation. Surplus pool songs are cosmetic, never a
  generation failure;
- **ground-item location keys**: `rando_keymap.c` binds 49 curated
  name→(area-room-flag) keys for default.logic's dungeon rupee/pot/underwater
  locations (triple-verified against USA ROM room data and the EU→USA address
  deltas), feeding the existing `itemOnGround` per-location hook — 45 bind
  under default settings, the other 4 live in non-default `!ifdef` branches
  and bind when those settings activate;
- **spoiler log** honors `:NoSpoiler` tags; the F8 tab exposes `!color`
  settings as live color pickers (override string = comma-separated RGB555
  hex, the same format `ParseColorDirective` consumes).

NOT yet at full parity (honest gaps):
- `!import` logic functions are approximated (logic-only item symbols are
  assumed owned, standing in for `LogicImport.cs` — not translated,
  clean-room). Note: the real `default.logic` contains no `!import` lines;
- per-location keyed hooks for NPC-script, shop, dojo, and fusion rewards
  (no stable identity exists at their grant callsites yet); such rewards
  still randomize via the global `Rando_OverrideItem` bijection;
- placement is feature-identical but not byte-identical to the C# shuffler
  (different PRNG by clean-room design — same seed text gives a different,
  equally-valid arrangement);
- custom heart-outline colors tint other HUD glyphs drawn with the shared
  palette-15 white (upstream replaces heart tile graphics; out of scope for a
  runtime palette override).

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
  items, fixed grants, and unmodelled events) are treated as owned in both
  placement and verification. This stands in for the complete event graph
  (not translated — clean-room).
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