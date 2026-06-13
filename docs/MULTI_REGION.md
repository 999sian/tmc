# Multi-region PC binary (one binary, any ROM, runtime US/EU/JP)

Goal: a single `tmc_pc` that loads a USA, EU, or JP ROM and plays it *faithfully* —
correct data, assets, text, and version-specific gameplay behavior — selected at
runtime from the loaded ROM. Replaces the previous one-binary-per-region model.

## Status

| Milestone | State |
|---|---|
| **M1 region foundation** (`include/region.h` dual-mode macros, `gActiveRegion`) | **DONE, verified** |
| **M2 `--multi_region` build mode** (compile once, runtime offset/region select) | **DONE** — one binary boots USA+EU+JP, each with correct offsets + area tables |
| **Matching maps (keystone)** | **DONE** — upstream `zeldaret/tmc` builds all 3 regions byte-matching; retail per-region maps in `tools/retail_maps/` (gitignored) |
| **M3 per-region script-func table** | **DONE** — `port_script_funcs.c` has runtime-selected USA/EU/JP tables (exact symbol-lookup derivation; USA/JP regression-clean) |
| **M3 GBA_script_\* + inline_ptrs** | TODO — `port_scripts.h` (102, used in static arrays — needs resolve-time translation) + `port_inline_ptrs.c` (8) |
| **M3 EU init crash** | **DONE** — was an integer-overflow in the asset extractor's `read_pointer` bounds check (`offset+4` wrapped) on a misparsed EU text root; added overflow guard + count sanity caps in `assets_extractor.hpp`. **All 3 regions now boot to AgbMain in one binary.** |
| **M4 convert behavior `#ifdef`s to runtime** | IN PROGRESS — **simple-tail function-level slice DONE** (`ff61caaa`, `042be0a4`): ~30 more sites converted across enemy/object/npc/item/playerItem/projectile files via parallel agent waves (one agent/file, strict control-flow-only rules + flag-don't-touch for data/case/exclusive-fn sites; build + all-3-boot verified). Files with EU/JP sites: **75 → 57**. Earlier function-level waves via `m4-convert-behavior-wave1`. **TRAP (found + fixed in `areaMetadata.c`/`gameUtils.c`, `b41c0333`):** flattening a per-region *data table* while its *consumers* stay region-split silently breaks the non-baseline region — the table's flag bits and the consumers' equality constants must agree. Pick one: per-region table, OR single-baseline table + region-agnostic consumers. Never half-and-half. REMAINING (the 57, all harder categories): (a) **data-table pattern** (`objectDefinitions.c` 51, `sound.c` gSongTable, `projectile.c`, `itemMetaData.c`, `*Definitions.c`, `data/*.c`, sprite-index `#define`s) — needs per-region data, not control flow; (b) **exclusive-function dispatch** (whole-function `#ifdef` defs — un-guard + runtime-select, addresses from the maps); (c) **special cases** (`case`-label guards, brace-splits, `&&`-chain operands like `collision.c` knockback, 3-way `#elif`); (d) **core/save files** deferred (`save.c`, `fileselect.c`, `main.c`, `interrupts.c`, `common.c`, `game.c`, `script.c`, `player.c`). DEMO_* sites left alone. |
| **M5 region-tagged saves** | TODO — cross-region load currently wipes (signature differs) |
| **M6 multi-language text verify + selector** | TODO — text mostly already works (shared fonts, JP uses same blitter) |

## The keystone: matching retail maps (DONE)

Per-region **code** addresses (script-func keys, version-exclusive functions) can't be
content-anchored (code differs across versions) and this fork is intentionally
non-matching. Solved by building the **upstream `zeldaret/tmc`** decomp, which byte-matches:

```
git clone --depth 1 https://github.com/zeldaret/tmc /tmp/tmc_upstream
cd /tmp/tmc_upstream && ln -sfn /home/sian/tmc/tools/agbcc tools/agbcc
cp -L /home/sian/tmc/baserom.gba baserom.gba; cp /home/sian/tmc/baserom_eu.gba .; cp /home/sian/tmc/baserom_jp.gba .
CMAKE_POLICY_VERSION_MINIMUM=3.5 make usa eu jp -j$(nproc)   # all three print "tmc*.gba: OK" (sha1 match)
```

Maps copied to `tools/retail_maps/{usa,eu,jp}.map`. Derivation is then an **exact symbol
lookup**: e.g. a script-func entry names `sub_08046078`; that symbol is at `0x08046078`
(USA) / `0x08045e84` (EU) / `0x08045f74` (JP) in the three maps. Validated: the 470 USA
keys equal `usa_map_addr|1` with **0 mismatches**. `/tmp/derive_script_funcs.py` +
`/tmp/gen_script_funcs_mr.py` did the script-func generation; same method applies to the
remaining tables (`script_*` and rail-data symbols are all in the maps).

## Build & run

```
xmake f --game_version=USA --multi_region=y -y    # USA = asset/offset baseline
xmake build -y tmc_pc
```
Run against any ROM (`baserom.gba` / `baserom_eu.gba` / `baserom_jp.gba`). The binary
prints `Active region set to {USA,EU,JP}` and selects `kRomOffsets_{USA,EU,JP}`.
`--multi_region=n` (default) keeps the classic single-region build. The matching GBA
ROM build is unaffected (`region.h` is pure compile-time constants there).

## Architecture

- **`include/region.h`** — the keystone. `REGION_IS_USA/EU/JP` are **compile-time
  constants** chosen by `-DUSA/-DEU/-DJP` in the default/matching build (so the decomp
  still byte-matches and dead branches fold away), but read the runtime **`gActiveRegion`**
  under `PC_PORT + MULTI_REGION`. Force-included by the build (`add_forceincludes`), so
  available everywhere; inert (macros only) in the non-multi-region path.
- **`gActiveRegion`** (`port_rom.c`) — set from `gRomRegion` in `Port_DetectRomRegion`,
  which already selects the right `kRomOffsets_*` table. Assets self-extract from the
  loaded ROM, so they follow the ROM with no bundling.

## M4 conversion pattern (for the 232 behavior sites)

Convert each region `#ifdef` from preprocessor to C control flow:

```c
#ifdef EU                         if (REGION_IS_EU) {
    eu_behavior();                    eu_behavior();
#else                       ->    } else {
    usa_behavior();                   usa_behavior();
#endif                            }

#if defined(JP) || defined(EU)  ->  if (REGION_IS_JP || REGION_IS_EU)
#ifndef EU                      ->  if (!REGION_IS_EU)
```

Rules:
1. In `MULTI_REGION` **both branches compile**, so any symbol a non-active branch
   references must exist for all regions. The ~3–5 version-exclusive functions
   (`AreaAllowsWarp` USA-only, `sub_08052878`/`sub_0805289C`, region-specific data
   arrays) must get un-`#ifdef`'d definitions (rename collisions, dispatch at runtime).
2. **The matching ROM build must keep working** — before converting `src/` sites, add
   `-include region.h` to the `xmake rom` task's preprocess (like the `-I port` fix), so
   converted files compile compile-time-constant there too.
3. No struct-layout differs by region (verified), so runtime branching is safe.
4. Verify each region at runtime with all three ROMs (USA/EU/JP all present locally).

Inventory: ~232 code-behavior sites across 47 files (mostly EU; JP ≈ USA with ~60 sites).
See the analysis in `docs/speedrun-and-rando-port-notes-2026-06-13.md` lineage.
