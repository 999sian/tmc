# port/rando/ — In-process native randomizer

Native randomizer logic that runs inside `tmc_pc`. It does not patch a GBA
ROM, shell out to the GPL C# randomizer, or allocate heap during generation.
The canonical randomizer is a **fixed-array native location graph** generated
in-process (`rando.cpp`): 211 locations, an OoTR-style assumed fill against a
region-reachability solver, and a playthrough verifier that runs before any
seed becomes active — so **rolled seeds are always beatable**.

> **Provenance.** `port/rando/` is **derived from** the GPL-3.0 Minish Cap
> randomizer (`minishmaker/randomizer`): its randomization behaviour and item
> pool follow that project. Its numeric location data (area/room/flag keys,
> chest identities) is taken from the USA baserom and the decompilation. It is
> distributed under the **GPL-3.0** with attribution — see the repository
> `LICENSE` and `THIRD-PARTY-LICENSES.md`.

When a seed is active, every give-item source (chests, NPC gifts, drops) is
routed through the randomizer. Keyed locations (chests, curated ground items,
and a set of scripted NPC/object rewards) resolve to the seed's placed item via
`Rando_OverrideLocationKey`; every other give-item source resolves through a
**seeded pool-preserving item bijection** (`Rando_OverrideItem`) that is
difficulty-scaled:

- `Normal` — shuffles collectibles only (rupees, hearts, kinstones, ammo,
  shells, heart pieces). Progression is untouched, so the seed stays beatable.
- `Hard` — also shuffles non-gating majors (bottles, upgrades, skills, etc.).
- `Chaos` — also shuffles dungeon-gating progression.

The Hard/Chaos extra pools also hit story-script gives the location graph
cannot verify, so they only apply with **glitchless logic OFF**. Glitchless ON
keeps majors/progression vanilla in the bijection — guaranteed beatable.

Primary API:

```c
RandomizerSettings settings = Rando_DefaultSettings();
bool ok = GenerateSeed(seed64, settings);
uint16_t item = Rando_ResolveLocationItem(location_id, vanilla_item);
/* Address-keyed hook: 0xAARRLL = area, room, local flag. */
Rando_OverrideLocationKey(0x000001, &item_type, &item_subtype);
```

## Architecture

- `rando.h` — C ABI: location id enum (211), `RandomizerSettings`, the
  `RandoAccessibility` verifier-strength enum, `RANDO_TRICK_*` glitch bits,
  and the public API including `Rando_SettingsFingerprint`.
- `rando.cpp` — **the engine.** SplitMix64 PRNG, the `kLocations[211]` graph
  with 31 region helpers (`EvaluateHelpers`, a fixed-point reachability
  solver), assumed-fill placement (`BuildSeedAttempt`, heap-free), a
  playthrough verifier (`VerifyTable`), the difficulty-scaled bijection, the
  spoiler builder, and the double-randomization award latch. Also the
  settings fingerprint.
- `rando_runtime.c` — new-file grants + per-frame runtime hooks: start
  inventory, wind crests, instant text, story skip, world-open flag table,
  homewarp.
- `rando_entrance.cpp` — coupled dungeon-entrance shuffle (8 doors), remapped
  at the engine's transition choke points; logic-aware door gates.
- `rando_cosmetic.cpp` — tunic/heart palette overrides (content-addressed
  against vanilla RGB555), rainbow hearts.
- `rando_music.c` — area→BGM remap at `LoadRoomBgm`. **Runtime half only**:
  no generation pass assigns songs yet, so it is populated only by sidecar
  restore (see "Not implemented").
- `rando_newfile.c` — named engine-flag tables mirroring the upstream GBA
  randomizer's `startingFlags` (baseline) and `worldOpen` (open-world) blobs,
  applied per region so EU flag renumbering stays correct; byte-parity
  asserted by the offline test.
- `rando_file_menu.c` — SDL3 file-select setup overlay backend. Owns
  randomizer persistence: the "Enable Randomizer Mode" toggle and all
  built-in-graph settings live in `config.json` (`rando_*` keys, via
  `port_runtime_config`), restored at startup and reset to vanilla defaults
  only when the toggle is switched off.
- `rando_save.c` — profile-local `*.randomizer` sidecar (magic `TMCRNDO1`,
  v6) storing seed, settings, per-slot item + subtype tables, entrance and
  music assignments for all three save slots without touching EEPROM.
- `rando_test.c` — deterministic generation/verification offline test target.

The location graph is deliberately native and data-oriented: every location
has a category, vanilla item, a region helper, and bitmask requirements;
progression/major/junk pools are separate fixed arrays; generation uses stack
scratch only; and a playthrough is simulated before a seed activates.

## Beatability & accessibility

Generation places progression via assumed fill and then calls `VerifyTable`,
which simulates a playthrough from the starting inventory and confirms the
goal (Dark Hyrule Castle / Vaati) is reachable. `Rando_GenerateSeed` retries
up to 32 salted attempts; if none verify it returns `RANDO_UNBEATABLE` and the
UI surfaces "Seed failed logic verification."

`RandomizerSettings.accessibility` (`RandoAccessibility`) strengthens the
verifier. It is a pure strengthening: a stronger mode can reject a seed the
weaker mode accepts, but never accepts a seed the weaker mode rejects.

- `RANDO_ACCESS_GOAL` (default) — only the goal must be reachable. A seed may
  bury optional checks behind items you never need.
- `RANDO_ACCESS_ALL_NONKEYS` — every enabled check must be reachable **except**
  unshuffled small-key chests (they can self-lock behind their own dungeon
  doors, which the region logic does not model).
- `RANDO_ACCESS_ALL_LOCATIONS` — every enabled check must be reachable,
  including small-key chests. Sound because small keys stay vanilla, so a
  reachable dungeon region guarantees the chest is obtainable in normal key
  order.

Small keys are pinned vanilla in both placement and verification (vanilla key
flow needs no key counting). Reachable small-key locations are still marked
collected during verification so the accessibility scan sees an accurate map,
but they grant no shuffled item.

## Logic tricks (glitch tier)

`RandomizerSettings.tricks` is a `RANDO_TRICK_*` bitmask that lets the logic
place progression behind documented speedrun glitches. Each trick also
requires the real in-game prerequisite. Tricks are **ignored when
`glitchless_logic` is true**, keeping every seed completable without glitches.
Shipped tricks: Ocarina Glitch (ToD entry without Flippers), Crenel Clip
(Mt. Crenel → Castor Wilds), Portal Jump Storage (early Cloud Tops). They are
exposed in both the file-select modal and the F8 → Randomizer tab when
glitchless logic is off.

## Seed sharing

`Rando_SettingsFingerprint` is a stable FNV-1a hash over every
placement-affecting setting (glitchless, obscure, kinstones, entrances, dojos,
open_world, item_difficulty, effective tricks, accessibility, start_sword).
Two players with the same **seed** and the same **fingerprint** are generating
the identical placement. Pure cosmetics and runtime-only QoL (tunic/heart,
homewarp, instant_text, early_crests) are excluded because they never change
placement. The F8 tab shows the active fingerprint with a copy button. Seed
text itself is decimal-as-is or FNV-hashed (`Rando_SeedFromString`), so phrases
work and are shareable.

## Settings & UI

`RandomizerSettings` fields: `glitchless_logic`, `obscure_locations`,
`shuffle_kinstones`, `shuffle_entrances`, `shuffle_dojos`, `open_world`,
`item_difficulty` (Normal/Hard/Chaos), `tricks`, `accessibility`, `homewarp`,
`start_sword`, `early_crests`, `instant_text`, `tunic_color`, `heart_color`.

Two surfaces drive the same settings, both persisted to `config.json`:

- **File-select setup modal** — auto-opens on new file when "Enable Randomizer
  Mode" is on. Drawn by `DrawRandoFileMenuModal` in `port_imgui_menu.cpp`
  inside the per-frame ImGui pass, so it presents on both the SDL_Renderer and
  SDL_GPU backends. Keyboard/mouse/gamepad all work. The surface-fallback
  backend never inits ImGui, so `Port_RandoFileMenu_ShouldOpenForNewFile()`
  gates auto-open on `Port_ImGui_CanPresent()` and falls back to the vanilla
  new-file flow rather than opening an invisible modal (= softlock).
  `TMC_RANDO_FILE_MENU=0` is the kill-switch.
- **F8 → Randomizer tab** — roll/copy seed, roll-race-seed (spoiler hidden
  behind an explicit reveal), active-seed line + fingerprint, item pool,
  every toggle, accessibility combo, trick checkboxes, cosmetics, and the
  spoiler viewer with a line filter.

## Spoiler / verification

- Spoiler: an 8 KiB static buffer built at activation — seed line, a settings
  header (pool/glitchless/tricks/kinstones/entrances/dojos/open-world/access/
  homewarp), the settings fingerprint, and the `location : item` list.
- `Rando_VerifyCurrentSeed` re-runs the verifier on the active table under the
  active settings (including the accessibility mode).

## What is randomized

- All 6 dungeons + Royal Crypt + DHC king reward: chests, item drops, heart
  pieces, pots, underwater items, iceblocks; boss heart containers (5);
  dungeon prizes (6).
- Shops (Stockwell + extras), Goron merchant sets, Business Scrubs, Biggoron
  shield, Cucco minigame rounds, Blade-Brother dojos + skill scrolls (when
  `shuffle_dojos`), Carlov medal, Tingle trophy, library books, great fairies,
  Melari, shoe shop, witch hut, Dampé, Gregal, café lady, Simon, bell HP,
  Jabber Nut, dog-food bottle, Smith floor items.
- Overworld heart pieces, dig spots, rupee caves, rock/vine items, Cloud Tops
  kills/digs (behind `obscure_locations`).
- Coupled shuffle of 8 dungeon entrances (`shuffle_entrances`).
- Cosmetic tunic/heart colours.

## Not implemented (honest gaps)

- **Keysanity / dungeon-item shuffle.** Small keys stay vanilla/unshuffled;
  big keys, maps, and compasses are not in the location table. Sound keysanity
  needs per-dungeon key identity in the item model (the logic tracks items by
  a single shared `ITEM_SMALL_KEY` id, and the engine credits a collected key
  to the *current* dungeon) plus give-item routing changes — see the roadmap
  notes in the port docs.
- **Music-shuffle generation.** The runtime remap (`rando_music.c`) is live and
  hooked, but no generation pass assigns songs, so new seeds don't shuffle
  music; only a restored sidecar can carry assignments.
- **Open-world-aware logic.** `open_world` is applied at new-file commit (the
  403-entry flag table) but is deliberately **not** fed into `EvaluateHelpers`:
  generation stays conservative (assuming obstacles exist), which keeps seeds
  beatable when the runtime later removes them. Feeding it into the solver for
  less-conservative fills is a future optimization, not a beatability fix.
- **`.logic` text-format import.** `rando_logic.cpp` is a **stub**: every
  `RandoLogic_*` entry point returns a trivial value, `RandoLogic_LoadText`
  does not parse, and `RandoLogic_Generate` defers to the native graph. The
  `TMC_RANDO_LOGIC` / `TMC_RANDO_DEBUG` env vars are inert. A prior generation
  of a full `.logic` parser/VM/keymap exists in git history (the `ea03b1e52`
  lineage) and was removed in `b3724069e`; the header (`rando_logic.h`)
  preserves the intended ABI as a spec. Restoring it is a revert/rebase job,
  not a rewrite, and is only worth doing if upstream `.logic` interop becomes
  a goal — the native graph is self-sufficient.

## Build / test

```bash
xmake build -y rando_logic_test
./build/pc/rando_logic_test          # offline: generation determinism, override
                                     # hook, glitch tier, accessibility, fingerprint
```

The main build links the module directly:

```bash
xmake build -y tmc_pc
```

End-to-end runtime check (drives the file-select overlay, generation, the
give-item hook, and the address hook, then asserts items actually change;
a second stage drives the live ImGui modal with synthetic SDL Return-key
events):

```bash
TMC_REPRO_RANDO=1 TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/pc/tmc_pc --no-audio       # prints "[rando-repro] PASS", exits 0
```
