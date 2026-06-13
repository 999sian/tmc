# The Minish Cap — Speedrun/Randomizer mechanics & how they map to the PC port

*Compiled 2026-06-13. Part A (game + speedrun + rando) is web research verified against the
speedrun wiki, speedrun.com, ZeldaSpeedRuns and TCRF. Part B is a code audit of this repo
(`/home/sian/tmc`) — every claim carries a `file:line` citation. Part C is the actionable list.*

> Provenance note: while researching, an automated page fetch of the TCRF "Regional Differences"
> article returned a prompt-injection payload impersonating "Anthropic's official CLI" and asking
> for destructive shell commands. It was ignored. Flagging it because that page is compromised.

---

## Part A — How the game and the speedrun work (reference)

### The game
GBA, 2004–05, Capcom/Flagship + Nintendo EAD; dir. Hidemaro Fujibayashi; music Mitsuhiko Takano +
Asuka Ota. **Chronologically the earliest game in the Zelda timeline** — invents the Four Sword and
Vaati. Six dungeons. Signature systems: **shrinking** at Minish portals; **Kinstone fusion**
(100 total, ~9 story-required; 100% needs all 100 → Golden Tingle Statue); **Element/sword forging**
(Smith's → White/Green → **Red → Blue → Four**); **clones** (only Red Sword+ can spawn them).

| # | Dungeon | Boss | Element / key item |
|---|---------|------|--------------------|
| 1 | Deepwood Shrine | Big Green ChuChu | **Earth** + Gust Jar |
| 2 | Cave of Flames | Gleerok | **Fire** + Cane of Pacci |
| 3 | Fortress of Winds | Mazaal | *no element* — Mole Mitts + Ocarina of Wind |
| 4 | Temple of Droplets | Big Octorok | **Water** + Flame Lantern (Flippers) |
| 5 | Palace of Winds | Gyorg Pair | **Wind** + Roc's Cape |
| 6 | Dark Hyrule Castle | Vaati (Reborn→Transfigured→Wrath) | *final* |

(Common error to avoid: Fortress of Winds does **not** give the Earth Element; Deepwood Shrine does.
Fortress of Winds and Dark Hyrule Castle are the only dungeons awarding no element.)

### Why it breaks: three engine facts
1. **Deterministic RNG.** One PRNG: `gRand = ror(gRand*3, 13)`, returns `gRand>>1`; seeded
   `0x1234567` at boot → identical after every reset. Advanced by roll/slash (+1), boot dust (+4),
   fairy (+4), room loads, enemy AI/drops. Every manip counts exact rolls.
2. **The Ocarina Glitch (OG).** Up + Ocarina 1 frame later at a staircase freezes the entity/object
   update loop and locks Link onto a higher layer → he walks over walls / "above rooms"; enables
   Portal Jump Storage. Survives normal screen transitions; **cleared by a "white load" (Save&Quit)**.
3. **Coordinate ≠ collision.** Position, facing angle, and a hidden "Push Value" update independently
   of collision resolution → clips. Many are subpixel/frame-perfect (OG = 1 frame; Octo Clip ~19–20
   frames; Stair Clip ~½ pixel).

### Speedrun categories (≈ June 2026)
- **Any%** (last hit on Vaati) — **~1:26**, Octoclip route (TGH ~1:26:13 / ToadsWoot ~1:26:49; check
  speedrun.com for live #1).
- **Glitchless** — 2:13:15 (TGH). Bans OG family, clips, Angle Retention, Wirbelhop, Portal Items, etc.
- **100%** (last item) — ~3:57 (atxmartin/ToadsWoot). Not glitchless; shares the Octo-Clip route; adds
  Kinstone Farming + deterministic Figurine RNG manip.
- CE: Firerod%, NG+ All Dungeons, Any% No S&Q, etc.

### Any% route — organized by *when you get the Red Sword*
Shared spine: Intro → Deepwood → Crenel → Cave of Flames → (Green Sword) → Castor Wilds → Fortress of
Winds → Magical Boomerang (4 green Kinstones) → Temple of Droplets → Palace of Winds → Dark Hyrule Castle.

| Variant | Red Sword | Clones? | Required glitches |
|---|---|---|---|
| Beginner | after Green Sword | yes | none (clips optional) |
| Intermediate | after Boots | yes | Crenel Clip |
| Advanced | after Ocarina | no | Crenel + **Armos Clip ×2** |
| Octoclip (WR) | with Blue Sword, post-Temple | no until then | Crenel + Armos + LLR Death abuse + **Octo Clip** |

Clones vs clips are substitutes: with the Red Sword in hand, Fortress-of-Winds rooms use clones and the
clips become optional; without it you must clip.

### Glitch toolbox
Clips (Crenel/Armos/Lever/Stair/Boss-Door/Octo/Lily) · OG family (OG, Portal Jump Storage, Portal Items
— **JP/EU only**) · Diagonal Angle Retention · Guard Skip · Peahat Clip (~3 min) · RNG manips (green
Kinstone Double/Triple/Quad, Blue Rupee, Big Octo, Triple Darknut, Vaati).

### Version differences — why JP
Speed rank **JP > EU > USA**. JP is the speedrun standard because its RNG manips were authored for JP,
**Portal Items** exists on JP/EU but was **patched out of USA** (~3 min via Fortress BK Skip), Zeffa is
callable at the Palace-of-Winds entrance on JP/USA but not EU, and JP text is ~2 min faster. EEPROM
saves ~5 s on cart; only mGBA/NanoBoyAdvance respect the delay; flashcarts banned.

### Randomizer (community + this port)
Community tool `minishmaker/randomizer` patches the **EU ROM only**; its logic guarantees a glitchless-
completable seed, with `Disable glitches` OFF promoting speedrun glitches into logic ("glitched
beatability") and `Obscure spots` adding knowledge-gated checks. The Google Doc you linked
("TMCR Obscure Locations infodump") documents the latter. *This repo ships its own native rando — see
Part B §4, which differs substantially from that model today.*

---

## Part B — How it maps to **this PC port**

Bottom line: **at default settings the port is a faithful speedrun/glitch platform** — RNG, input
debounce, camera/loading-zones, and the Save&Quit "white load" are bit-exact GBA. The caveats are a
few opt-in features and one save-state omission, plus the native rando being much thinner than its
README implies.

### 1. RNG & determinism — **faithful** (one save-state gap)
- PRNG is bit-exact: `gRand=gRand*3; (gRand>>13)|(gRand<<19); return gRand>>1`
  (`port/port_linked_stubs.c:832-836`), seeded `0x1234567` (`src/main.c:87`). All 579 `Random()` call
  sites are unmodified decomp; no host RNG (`rand`/`mt19937`/SDL) feeds gameplay. Input is sampled once
  per engine vblank (`src/main.c:93-133`, `port/port_bios.c:519-654`) → 1-frame inputs expressible,
  frame pacing/fast-forward never add/drop ticks or RNG advances.
- **DIVERGENCE (medium):** QuickSave/save-state does **not** capture/restore `gRand`. On GBA it lives in
  IWRAM (`0x03001150`) so a console savestate restores it; in the port it is a standalone host global
  absent from the snapshot region list (`port/port_quicksave.c:73-85`). → Re-loading an F1–F6 state to
  retry an RNG manip yields a **different** sequence than console. Practicing drop/enemy manips via
  save-states is unreliable; only from-boot runs reproduce console RNG. **Fix is ~4 bytes:** add `gRand`
  to `sRegions[]`.

### 2. Region targeting — **USA-default; JP not buildable**
- Runtime supports **USA (BZME) and EU (BZMP)** only, auto-detected from ROM byte `0xAC`
  (`port/port_rom.c:441-459`). Build defaults to USA (`xmake.lua:18`).
- **DIVERGENCE (high):** **JP is not buildable as a PC binary** — `pc_versions` has only USA/EU and
  silently falls back to USA (`xmake.lua:685-693`); no `ROM_REGION_JP`, no `kRomOffsets_JP`, no
  `port_offset_JP.h`. `--game_version=JP` produces a *USA* binary with no error. → **No JP speedrun
  parity** in the port.
- **DIVERGENCE (high):** region-specific glitch availability (Portal Items USA-patch, EU Zeffa, EU
  2-arrow enemies) is **not** modeled per loaded ROM — behavior follows the **build-time region macro**
  (`xmake.lua:693`) and decomp fidelity, not the ROM you feed it. A USA binary fed an EU ROM only warns
  (`port/port_main.c:740-757`) and is a USA-code/EU-data hybrid matching neither console. Always match
  build region to ROM region. EU correctness also leans on having run the EU asset pipeline
  (`port/port_rom.c:600-650`; `port_rom_tables.c` is USA-baked).

### 3. Determinism / repro harness — **good for rando placement, blind to engine RNG**
- 12 env-gated self-driving regression harnesses (`port/port_repro_*.c`) dispatched per-frame from
  `port/port_bios.c`; each pins a specific crash-fix/behavior (mazaal, vaati, clonebutton, jailbars,
  etc.) and `_Exit()`s pass/fail. CI runs only `rando_logic_test` (placement determinism: seed
  `0xfeedface` generated twice + full-table `memcmp`, `port/rando/rando_test.c:23-47`,
  `.github/workflows/_build.yaml:486-496`) + a headless boot smoke test.
- **DIVERGENCE (high):** **nothing tests the engine `Random()`/`gRand` sequence** — the most speedrun-
  load-bearing invariant has zero regression coverage; a one-char edit to `port_linked_stubs.c:832-836`
  would silently break every manip. **Recommend a golden-vector test** (assert N outputs from a known
  seed).
- **DIVERGENCE (medium):** none of the `TMC_REPRO_*` engine harnesses run in CI (manual only); two
  `port_repro_rando.c` stages are stubbed to always-pass (`:220-226`).

### 4. Native rando logic — **much thinner than README; no real glitch tier**
- Canonical logic is a hand-written **211-location native graph** (`port/rando/rando.cpp:57-269`):
  region "helper" BFS (`EvaluateHelpers` `:390-448`), OoTR-style assumed-fill (`:677-722`), beatability
  via `VerifyTable` simulating to `RH_GOAL` (`:553-592`), deterministic SplitMix64 keyed on the seed
  (independent of engine `gRand`).
- **DIVERGENCE (high):** the **`.logic` parser/VM/importer described in `port/rando/README.md` is
  absent.** `rando_logic.cpp` is a **128-line stub** (`RandoLogic_GetSettingCount()` returns `0`,
  `EvaluateReachability` blanks all locations); `rando_keymap.c` is a 4-line no-op. Commit `87100f4b`
  ("native graph canonical, .logic optional import") deleted ~2433/481 lines. The F8 "logic settings
  browser" renders empty; the in-game tracker marks everything unreached.
- **DIVERGENCE (high):** **there is no glitched-vs-glitchless reachability tier.** `glitchless_logic` is
  consumed only at `rando.cpp:793` (`if (sSettings.glitchless_logic) return;`) to skip a Hard/Chaos
  **item-color bijection** — the *same* glitchless-strict graph verifies every seed. **No documented
  speedrun glitch (OG/clips/PJS/DAR) is encoded as a logic edge or setting anywhere** (settings struct
  `rando.h:36-48`). So "glitched" seeds are not logically more open than glitchless ones. The
  `ac3516f3` "glitchless beatability + persistent settings" commit shipped persistence + a strict
  verifier, not a second tier.
- **DIVERGENCE (medium):** entrance shuffle is wired to the wrong setting — `shuffle_kinstones` drives
  coupled dungeon-entrance shuffle with the literal comment *"Reuse shuffle_kinstones as a proxy for
  now"* (`rando.cpp:644`). Toggling "shuffle kinstones" silently turns on logic-affecting entrance rando.
- Persistence is solid: per-slot `.randomizer` sidecar (v4) + `config.json` defaults; `glitchless_logic`
  is saved per-seed (`rando_save.c:232,271`).

### 5. Renderer / widescreen — **default bit-exact; opt-in widescreen can desync RNG**
- Default build is bit-exact GBA: widescreen is double-gated — needs `--widescreen_width>240`
  (default 240, so no widescreen instruction compiles, `xmake.lua:63-66`) **and** an F8 toggle
  (default off). Height pinned at 160. **Loading zones are coordinate-driven, not view-driven**
  (`src/scroll.c:614-633`) → Boss Door Clip ("loading zones always loaded") and Stair Clip alignment are
  safe; OG/PJS/clips read player coord/angle/push/`scroll_x`, none of which the default renderer touches.
- **DIVERGENCE (medium, opt-in):** when widescreen is ON in a wide build, the extra width is fed into
  `CheckOnScreen`/`CheckRectOnScreen` (`port/port_draw.c:593`, `port/port_linked_stubs.c:1950`), which
  ~31 logic sites use to gate AI spawn/despawn → right-edge enemy AI activates ~144px earlier → advances
  the shared PRNG → **can desync RNG manips**. Keep widescreen OFF for manip-accurate play. Reveal-margin
  tile garbage is cosmetic only.

### 6. Input & frame-perfect timing — **faithful; one leniency, one illegal-angle feature**
- One input poll per engine frame, single 10-bit `REG_KEYINPUT` mask, stock `ReadKeyInput`/debounce
  unmodified (`src/common.c:342-363`), no interpolation/smoothing → OG, clips, DAR all reproducible at
  default config; TAS-style exact input is feasible.
- **DIVERGENCE (medium):** an edge cache (`sEdgePressed`, `port/port_runtime_config.cpp:1000-1035`)
  latches any sub-frame key tap as a 1-frame press → makes 1-frame inputs (OG) **easier than hardware**.
  Leniency only — no new tech — but input-capture timing differs from console (matters for leaderboard
  parity).
- **DIVERGENCE (low, off by default):** 360° analog movement (`port/port_analog_movement.c:39-88`)
  overwrites `gPlayerState.direction` with a 32-slot snap → can set facing values a D-pad can't reach
  (DAR-adjacent). Non-60 Hz FPS presets change feel, not frame counts (fixed-timestep).

### 7. Save / "white loads" — **faithful; save-states are a non-vanilla vector**
- Only the EEPROM *leaf* is swapped: `src/eeprom.c` is excluded; `port/port_save.c` backs the four BIOS
  funcs with an 8 KB image flushed to `tmc.sav` (mGBA-compatible). Everything above — `src/save.c`,
  the `HandleSave` animation (`field_0xa=8` frames, `src/save.c:117-148`), the pause-menu Save&Quit, and
  the GAMEOVER→title / `SoftReset` path — runs unmodified. → **Save&Quit still clears OG** via the same
  engine reset (`port/port_bios.c:860-880`), and frame-counted S&Q segments time identically to console.
- The GBA EEPROM ~5 s / flashcart hardware rule is moot on PC (instant atomic write) — but it never
  affected in-game frames anyway.
- **DIVERGENCE (medium):** save-states + auto-save ring (`port/port_quicksave.c:73-85,418-463`) snapshot
  curated EWRAM/IWRAM/entity state and restore by `memcpy` — can restore transient/illegal state the
  game can never re-enter (mid-frame, subpixels, possibly an active OG/frozen-loop). Must be off for
  legitimate runs.

---

## Part C — Actionable findings (ranked)

| # | Sev | Finding | Where | Suggested action |
|---|-----|---------|-------|------------------|
| 1 | High | `.logic` engine & glitched-vs-glitchless reachability tier described in the rando README don't exist; logic is the native graph + a strict glitchless verifier | `port/rando/rando_logic.cpp` (stub), `rando.cpp:793` | Decide: implement glitch edges/tiers, or correct `port/rando/README.md` to match the native-graph reality |
| 2 | High | No regression test on the engine `Random()`/`gRand` stream | `port/port_linked_stubs.c:832-836` | Add a golden-vector unit test (known seed → fixed output vector) to CI |
| 3 | High | JP not buildable; `--game_version=JP` silently yields a USA binary | `xmake.lua:685-693` | Either add JP region support (offsets table + `ROM_REGION_JP`) or hard-error on unsupported regions |
| 4 | Med | `shuffle_kinstones` is a proxy for entrance shuffle (mislabeled, logic-affecting) | `rando.cpp:644` | Add a dedicated `shuffle_entrances` setting; decouple |
| 5 | Med | QuickSave doesn't restore `gRand` → save-state RNG practice diverges from console | `port/port_quicksave.c:73-85` | Add `gRand` (4 bytes) to `sRegions[]` |
| 6 | Med | Widescreen ON widens on-screen culling → off-screen AI advances PRNG early | `port/port_draw.c:593`, `port_linked_stubs.c:1950` | Document "widescreen OFF for manip parity"; or gate culling width behind a determinism flag |
| 7 | Med | Build/ROM region mismatch only warns; hybrid runs silently | `port/port_main.c:740-757` | Consider erroring (or auto-selecting offsets) on mismatch |
| 8 | Low | Edge cache makes 1-frame inputs easier than hardware | `port/port_runtime_config.cpp:1000-1035` | Optional "console-parity input" toggle for verification |

---

## Part D — Resolution status (updated 2026-06-13, post-audit work)

A "make speedrun possible on the PC port / support all versions" pass addressed the
speedrun-critical Part C items. Implemented in this repo:

| Part C # | Status | What landed |
|---|---|---|
| 2 (RNG golden test in CI) | **DONE** | `port_rng_golden_test.c` now runs on every Linux CI build via a dependency-free `cc` step (`.github/workflows/_build.yaml`); a one-char PRNG-constant edit fails CI. Verified locally: `RNG GOLDEN VECTOR OK`. |
| 3 (JP buildable / hard-error) | **DONE (boots + core-correct)** | JP fully wired AND a JP build boots against the retail JP ROM with all data tables resolving (verified: `JP (BZMJ)` detected, JP offsets, 2-level area pointers resolved, `AgbMain` entered, no crash). `kRomOffsets_JP` derived from the retail USA+JP ROMs by content-anchoring (this tree's decomp build is non-matching, so its map is unusable). Unknown `--game_version` now **warns** instead of silently building USA. Remaining for JP speedrun parity: Japanese text rendering + JP script-address tables (`port_scripts.h`, `port_script_funcs.c`). See `docs/JP_PORT_ENABLEMENT.md`. |
| 6 (widescreen RNG desync) | **DONE** | Console-Parity forces `Port_Config_WidescreenEnabled()` to return false, neutralizing the off-screen-AI cull-width advance. |
| 7 (region mismatch only warns) | **DONE** | Mismatch is now **fatal under `--console-parity`** (a parity run can't execute a version-mismatched hybrid); still a loud warning otherwise. |
| 8 (console-parity input toggle) | **DONE** | New **Console-Parity mode** (`--console-parity`, `config.json` `console_parity`, imgui toggle) disables the sub-frame input edge cache. |
| 5 (gRand in save-states) | **ALREADY FIXED** | `gRand` is in `sRegions[]` since save-state format v3 (`port/port_quicksave.c:97`) — the Part B/C note predates that. Moot under parity regardless (save-states are inert). |
| 1, 4 (rando `.logic` / `shuffle_kinstones`) | **OUT OF SCOPE** | Randomizer logic, not speedrun viability. Tracked separately. |

**Console-Parity mode** (the umbrella integrity switch) simultaneously: disables the
input edge cache, makes F1–F6 save-states inert (hotkeys swallowed + `LoadSlot`/practice
load refused), forces widescreen off, locks frame pacing to the authentic **59.7275 Hz**
(`16742706 ns/frame`, vs the default round 60 Hz), and makes a region mismatch fatal.
Default OFF. Touch points: `port/port_runtime_config.{h,cpp}`, `port/port_bios.c`,
`port/port_quicksave.c`, `port/port_main.c`, `port/port_imgui_menu.cpp`.

---

## Sources
- TMC Speedruns Wiki — https://tmcspeedruns.wiki.gg/ · ZeldaSpeedRuns — https://www.zeldaspeedruns.com/tmc/
- speedrun.com — https://www.speedrun.com/tmc (+ `/tmcce`) · Community hub — https://sites.google.com/view/tmcspeedruns
- Community randomizer — https://github.com/minishmaker/randomizer · Decomp — https://github.com/zeldaret/tmc
- Code citations are to this repo (`/home/sian/tmc`) at the commit checked out 2026-06-13.
