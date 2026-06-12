# port/rando/ — In-process native randomizer

The canonical randomizer is a **native location graph** generated in-process
(see `rando.cpp`). The external `.logic` text format is supported only as an
**optional import** (`rando_logic.*`), gated behind an alias table
(`rando_keymap.c`) that maps a public-format file's location names onto the
project's native runtime keys. With no `.logic` file imported, none of the
import machinery runs and the native graph alone drives generation.

> **Provenance / independence.** `port/rando/` is an independent reimplementation.
> Its numeric location data (area/room/flag keys, chest identities) is derived
> from the USA baserom and the decompilation — not copied from any external
> project. The optional importer parses a *public text format*; it does not
> translate GPL C# code and vendors no `.logic` data file. It is, however,
> format- and behaviour-aware of the upstream randomizer by design (so imported
> files work), so this is best described as an *independent reimplementation*,
> not a strict isolated clean-room.

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
- `Chaos` — also shuffles dungeon-gating progression.

The Hard/Chaos extra pools also hit story-script gives the location graph
cannot verify (e.g. the shield gating Deepwood's Business Scrub), so they
only apply with **glitchless logic OFF** (issue #155). Glitchless ON keeps
majors/progression vanilla in the bijection — guaranteed beatable.

For true per-location logic, supply a public-format `.logic` file
(`TMC_RANDO_LOGIC=/path`); chests are then remapped per area/room/flag key via
`Rando_OverrideLocationKey`.

Primary API:

```c
RandomizerSettings settings = Rando_DefaultSettings();
bool ok = GenerateSeed(seed64, settings);
uint16_t item = Rando_ResolveLocationItem(location_id, vanilla_item);
/* Optional address-keyed hook: 0xAARRLL = area, room, local flag. */
Rando_OverrideLocationKey(0x000001, &item_type, &item_subtype);
```

## Current architecture

- `rando.h` — C ABI, location/category enums, settings, and table access.
- `rando.cpp` — SplitMix64 PRNG, the seeded pool-preserving item bijection
  applied via `Rando_OverrideItem`, plus a built-in location graph used for the
  spoiler/self-verification and offline tests.
- `rando_logic.h/.cpp` — parser and expression VM for the public `.logic`
  text format, for interoperability without translating GPL C# code.
  Startup probes `TMC_RANDO_LOGIC`, `assets/rando/default.logic`,
  `dist/USA/assets/rando/default.logic`, `dist/EU/assets/rando/default.logic`,
  `rando/default.logic`, then `default.logic`. The GPL logic file is not
  vendored.
- `rando_file_menu.h/.c` — SDL3 file-select setup overlay using fixed
  element arrays and `SDL_FRect` hitboxes. Owns randomizer persistence
  (issue #155): the "Enable Randomizer Mode" toggle, built-in graph
  settings, and `.logic` define overrides live in `config.json`
  (`rando_*` keys, via `port_runtime_config`), restored at startup and
  reset to vanilla defaults only when the toggle is switched off.
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

## `.logic` import format support

The optional `.logic` importer is an independent reimplementation of the
documented public text format (written from the public spec block at the top of
a `.logic` file, not from the GPL C# source).

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
- reward write-back hooks: small chests, big chests, freestanding ground
  items, Stockwell's shop slots, Blade Brothers dojo rewards, Cucco minigame
  rounds, Carlov's medal, Business Scrub item sales, and Goron Merchant sets
  (when the corresponding `.logic` settings enable them) are remapped
  per-location. Chests use the format's chest identity
  `area-room-chestIndex` (the third byte is the chest's 0-based position among
  the room's chest TileEntities, resolved by `Rando_RoomChestIndex`), so real
  `.logic` placements land on the right chest in-game. Remaining NPC / fusion /
  script-driven give-item sources still fall back to the global
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
- **story skip** (issue #155): every rando new file starts post-intro —
  Link wakes at home with Ezlo, festival/castle/Minish-door sequence done.
  The flag set mirrors the engine's own canonical post-intro state
  (`gDemoSave` in `src/title.c`), flags only: items stay with the shuffled
  pool and the spawn stays `FinalizeSave()`'s bed spawn;
- **world-opening eventdefines** (issue #155): `m<hex>` defines are byte
  pokes into GBA save state (gSave at EWRAM `0x2000A40`; offsets are
  translated per region because the PC `SaveFile` packs `KinstoneSave`
  one byte tighter than agbcc) — this makes every fusion-opening World
  Setting (`OPEN_GOLD/RED/BLUE/GREEN_FUSIONS`, the Castor Wilds block)
  work natively. Named defines: `goldTornado` (KUMOTATSUMAKI),
  `openWindTribe` (WARP_EVENT_END), `openTingleBrothers` (TINGLE_TALK1ST
  + a PC-side bypass of the `global_progress > 3` half at the three
  roomInit spawn sites), `openLibrary` (MIZUKAKI_START — approximation:
  upstream re-times its own modified library opening), and the five
  beanstalk demos (`BEANDEMO_00..04`, LOCAL_BANK_1);
- **homewarp** (`allowHomewarp`; issue #155): SELECT on the pause menu's
  Quest Status screen warps Link back to his bed, GBA-randomizer "SLEEP"
  parity. The request is refused while minish (no portal home = softlock);
  the warp fires from a per-frame tick after the menu closes, and the
  pause overlay shows a `SELECT: SLEEP` hint when available;
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
- **ground-item location keys**: `rando_keymap.c` binds 138 curated
  name→(area-room-flag) keys — every dungeon rupee/pot/underwater item, every
  overworld heart piece, dig spot, rock item, rupee-cave item, the Cloud Tops
  dig kinstones and kill rewards, crypt gibdo/key drops, fight-completion key
  drops (FallingItemManager records — flag is the spritePtr **low** half),
  the ToD entrance ice-block keys, the five boss heart containers, the Lost
  Woods chest, and the two smith-house floor items. All triple-verified
  against USA ROM room data and the per-block EU→USA address deltas. 127 bind
  under default settings; the other 11 (pot-rando pots, `FALLSDIGKINSTONE`
  spots — `Inaccessible` at defaults) live in non-default `!ifdef` branches
  and bind when those settings activate. `TMC_RANDO_DEBUG=1` logs binder
  misses by name;
- **boss heart containers**: `src/object/heartContainer.c` keys the pickup by
  the container's room-local persistence flag, covering the five
  `*_BossItem` locations (Deepwood/CoF/Fortress/Droplets/Palace);
- **smith-house floor items**: the `.logic` file declares `Smith_Floor_Item1/2`,
  which the GBA randomizer creates by
  rewriting two furniture records in the GBA room data; natively
  `src/roomInit.c` spawns the equivalent ground items (flags `0xE0/0xE1`,
  unused across every LOCAL_BANK_2 area) when an active `.logic` seed has
  those locations;
- **scripted runtime keys**: `rando_keymap.h/.c` reserves bit 31 for stable
  native identities that are not area-room-local triples. The runtime binds
  default.logic's Stockwell shop slots, Blade Brothers dojo rewards, Carlov
  medal, the Hylia dog bottle, the Minish Village barrel-house Jabber Nut, the
  three library books, Melari's broken-sword reward, the shoe-shop Pegasus
  Boots, the Witch Hut mushroom, the Bomb Minish bomb bag, Minish/Crenel/Valley
  Great Fairy rewards, Valley Dampe's graveyard key, Biggoron's mirror shield,
  the library yellow-minish reward, the Town Cafe lady kinstone, the Crypt
  prize, Gregal's shells + light arrows, the Deepwood/CoF/Droplets/Palace
  dungeon prizes, the Fortress prize (the falling-bird ocarina,
  `src/object/bird.c`), the Hyrule Town bell heart piece
  (`src/object/graveyardKey.c` type 1), Tingle's trophy, the DHC B2 king
  reward (Minister Potho's script grant), Simon's Simulation heart piece, and
  Business Scrub item sales by default (43 locations at stock settings). With
  `GORON_5` + `VANILLA_BLUE_FUSIONS` + `VANILLA_RED_FUSIONS` +
  `BIGGORON_NORMAL` + `CUCCO_10`, 72/73 scripted locations bind in the
  real-file diagnostic (the last, `Town_Shop_Extra600Item`, is gated behind
  the `SHOPBOMBBAG` template's setting);
- **same-item subtype overrides**: external logic no longer loses placements
  whose native engine item id matches the vanilla reward but whose subtype is
  different. `RandoLogic_Generate` now emits a per-location subtype table
  (shell counts, kinstone piece ids, dungeon item ids), `Rando_OverrideLocationKey`
  compares both item and subtype, and sidecar v4 persists those subtypes across
  save/reload;
- **spoiler log** honors `:NoSpoiler` tags; the F8 tab exposes `!color`
  settings as live color pickers (override string = comma-separated RGB555
  hex, the same format `ParseColorDirective` consumes).

NOT yet at full parity (honest gaps):
- `!import` logic functions are approximated (logic-only item symbols are
  assumed owned, standing in for upstream's import mechanism — not
  translated, but unused by the real `default.logic`);
- locations whose `.logic` entries are disabled by settings (`Inaccessible`
  templates, non-default `!ifdef` branches) have no keyed identity until the
  matching setting is enabled; give-item sources for those still randomize
  via the global `Rando_OverrideItem` bijection. Every reward location that
  parses in is keyed — the only unkeyed entries at default settings are pure
  logic constructs (`Shared%x`/`DummyToD` constraint nodes, the eight
  `*_Entrance` dummies, `Dummy_*` pool pins, `All_Requirements_Met`) which
  never hold real in-world rewards;
- placement is not byte-identical to the upstream shuffler — a different,
  independent PRNG means the same seed text gives a different but
  equally-valid arrangement;
- custom heart-outline colors tint other HUD glyphs drawn with the shared
  palette-15 white (upstream replaces heart tile graphics; out of scope for a
  runtime palette override).
- a tranche of eventdefines is still unconsumed: `openWorld` (the
  "every cut tree / cracked block / bomb wall" permanent-obstacle list —
  needs a curated native flag table), `kinstoneMultiplier*` (bag-grant
  scaling for removed colors), `follower`/`followerID`, fusion-flow
  tweaks (`DEFICKLE`, `SEEDED_SHARED`, `FUSION_SKIPS`/`SHOW_MAP`,
  `noFairy`, `guaranteedBarlov`), and `historyOption` (item-name HUD
  ticker). Their settings parse and persist but have no runtime effect
  yet.

### Optional import: real `default.logic` interop

As an optional interop check, the importer can be pointed at a user-supplied
upstream `default.logic` (set `TMC_RANDO_LOGIC=path`; `rando_logic_test` then
asserts the import parses and resolves). This file is never vendored; it is the
user's own. The canonical native graph does not depend on it. The full file's
`[gen]` placement traces. In-game, the runtime hooks resolve to the logic's
location identity: the `TMC_REPRO_RANDO=1` harness (with `TMC_RANDO_LOGIC`
set) probes 331 keyed locations at default settings (161 chests + 127 ground
items from `rando_keymap.c` + 43 scripted rewards from the high-bit runtime
namespace); 160 chest keys match real engine chest TileEntities (all 160
key-resolve through the runtime `Rando_RoomChestIndex` path) and 330
locations receive their `.logic`-placed item at the title-screen probe frame
(more at actual play time, once distant areas are resolved). The harness also
confirms a real-logic seed survives a sidecar save/reset/reload round-trip
(chests stay randomized across save+restart).

Approximations still in effect (don't block beatable generation, but are not
full parity):
- Item symbols that appear in logic but aren't in the shuffled pool (start
  items, fixed grants, and unmodelled events) are treated as owned in both
  placement and verification. This stands in for the complete event graph
  (independently approximated, not translated from upstream code).
- 156/176 item symbols map to native engine ids (gear, elements, scrolls,
  heart pieces/containers, subtyped `BigKey`/`SmallKey`/`Compass`/`DungeonMap`,
  butterflies, progressive bases, …); the remaining ~20 are non-reward symbols
  (kinstone fusions, figurines, music, entrance/trap dummies) that leave their
  vanilla reward in place. At default settings 243/244 keyed locations
  override at the probe frame; the rest hold placements that keep their
  vanilla reward.

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
address hook, then asserts items actually change; a second stage drives the
real ImGui modal with synthetic SDL Return-key events through the live event
pump and asserts the typed seed goes active):

```bash
TMC_REPRO_RANDO=1 TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/pc/tmc_pc --no-audio        # prints "[rando-repro] PASS", exits 0
```

### Backend note

The file-select setup modal is drawn by `port_imgui_menu.cpp`
(`DrawRandoFileMenuModal`) inside the per-frame ImGui pass, so it presents on
both the **SDL_Renderer** and **SDL_GPU** backends. Keyboard, mouse, and
gamepad all work: Enter in the seed field (or with nothing focused) =
generate & start, Esc / gamepad B = cancel, and the window clamps to the
viewport so the action row stays reachable at small window scales.

The **surface fallback** backend never initialises ImGui, and the GPU device
probe can fail — `Port_RandoFileMenu_ShouldOpenForNewFile()` therefore gates
auto-open on `Port_ImGui_CanPresent()` and falls back to the vanilla new-file
flow rather than opening an invisible modal over masked input (= softlock).
`TMC_RANDO_FILE_MENU=0` is the explicit kill-switch. On any backend,
**F8 → Randomizer** also rolls/configures seeds in-game.

### Menu UX

Both randomizer surfaces (the file-select setup modal and **F8 →
Randomizer**) share one settings browser built from the `.logic` file's own
metadata: every `!flag`/`!dropdown`/`!numberbox` directive's window tab,
setting group, and tooltip text are parsed into `RandoLogicSetting`, so the
UI groups ~200 settings under collapsing tab sections with group separators,
a text search filter, hover tooltips (upstream's own descriptions), orange
`*` modified-from-default markers with per-section changed counts,
right-click → reset-to-default, and a confirm-gated "Reset all".

Presets follow the OoTR convention (absolute, not additive — loading one
resets everything except cosmetics first): **File defaults (Beginner)**,
**Standard shuffle**, **Keysanity**, and **Open world (fast)**.

The F8 tab also gets: Random/Copy buttons on the seed field, a **Roll race
seed** button (random seed, spoiler log hidden behind an explicit "Reveal
anyway"), a copyable active-seed line, a **settings fingerprint** (FNV-1a
over every generation-relevant setting's effective value — two players with
the same seed and fingerprint are playing identical seeds), and a line
filter on the spoiler log.