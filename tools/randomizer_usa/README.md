# randomizer_usa — USA-region support for the Minish Cap Randomizer

**Status: Phase-1 scaffolding + scope-measurement.** Real USA support
remains weeks of detailed offset work, but Phase 1 has measured the
exact scope and ships tooling that already covers ~25% of the unique
EU addresses via anchor interpolation alone. This directory is the
staging ground for the rest.

## The actual scope (after measurement)

The randomizer's patch system
(`libs/randomizer/RandomizerCore/Resources/Patches/`) is a tree of
**33 EventAssembler `.event` files** wrapped around supporting C source,
images, and binaries. Running `tools/randomizer_usa/scan_patches.py`:

```
files:           33
patches with at least one ORG: 28
total ORGs:     804
unique ORGs:    650
```

Each `ORG $hex` directive tells the assembler to seek to that EU ROM
offset and overwrite from there. Examples from `ROM Buildfile.event`:

```
ORG  $EDD8+2; BYTE 0x5C 0x70      // lady kinstone
ORG  $DA58+2; BYTE 0x5C 0x6C      // ghost kinstone
ORG $F2528+3; BYTE 0x01 0x00      // new item in Smith's room
ORG   $5D602; BYTE 0x63           // bell heart piece
```

These addresses are all positions in the **EU** Minish Cap ROM
(`BZMP`). They point at the EU byte where the randomizer needs to
write its patch — e.g. the byte that controls which item a kinstone
fusion gives. The USA ROM (`BZME`) has those same conceptual items
but at different file offsets (text shifts, language tables, build
differences).

Naively applying EU patches to a USA ROM **silently corrupts** it
(scribbles over unrelated code/data), which is exactly what the
randomizer does today if you feed it a USA ROM: `ROM.cs:59` sets
`Version = RegionVersion.Us`, then nothing in the patch pipeline
honours the distinction.

## What's done in this session

- **`translate_offsets.py`** — per-file translator. Reads a `.event`,
  finds every `ORG $hex` directive, emits a translated copy. Five-tier
  fallback in priority order:

    1. **`KNOWN_PAIRS`** — 15 verified EU/USA pairs harvested from
       `RandomizerCore/Core/Constants.cs::HeaderTable` (which the
       randomizer team filled out but never wired into the patch
       pipeline).
    2. **Decomp-map matching** — requires EU and USA `tmc.map` from
       `~/tmc-reference/zeldaret-tmc/`.
    3. **Byte-pattern probe** — read 32 bytes at the EU offset, search
       for the same sequence in the USA ROM. Requires both ROMs.
    4. **Anchor-delta interpolation** — if the EU offset is within
       0x2000 (tight) or 0x20000 (loose) bytes of a KNOWN_PAIR anchor,
       apply the same delta. Tagged `interp:tight` / `interp:loose`
       so the human audit can prioritize.
    5. **Unresolved** — emit `// UNRESOLVED for USA` so the patch is
       auditable.

- **`scan_patches.py`** — walks every `.event` in the patches tree,
  classifies every unique `ORG` by which tier resolves it, prints a
  per-file table, and (with `--csv`) writes a coverage report.

- **Coverage report at zero data inputs**
  (`reports/coverage_no_eu_data.csv`) — established the baseline. With
  no EU ROM and no EU map, the interpolation tier alone covers
  **162/650 = 24.9%** of unique EU offsets. The other 488 need either
  the EU ROM (enables byte-pattern probe) or the EU decomp map
  (enables symbol matching).

## Key findings

1. **The HeaderTable delta is *not* constant.** Observed EU→USA deltas
   across the 15 anchors:

   | EU range            | delta |
   |---------------------|-------|
   | 0x0D4828 (single)   | 0x8D4 |
   | 0xFED88 (single)    | 0xAC8 |
   | 0x101BC8–0x1077AC   | 0x8A4 |
   | 0x107800–0x107AEC   | 0x8A4–0x8AC |
   | 0x107B02–0x107B5C   | 0x8AC |
   | 0x11D95C (single)   | 0x8B8 |
   | 0x323FEC (single)   | 0xAF8 |
   | 0x5A23D0 (single)   | 0xAB0 |

   So interpolation is reliable *within* anchor clusters but breaks
   down across them. Inside the dense 0x107000–0x108000 cluster you
   can usually use the nearest-anchor delta; elsewhere you really
   need to probe.

2. **One `FreeSpace` macro covers most expansion writes.** The
   `ROM Buildfile.event` `#define FreeSpace $EF3340` controls
   *every* expansion-region ORG in the entire patch tree (most of
   the high-address writes flow through it). Once we know the USA
   freespace start, all those ORGs auto-retarget — no per-line work.

3. **Patches don't ORG *at* the known anchors.** Zero patches write
   directly at any of the 15 HeaderTable addresses (`known: 0` in the
   scan). They all write at offsets *within* tables — which is
   exactly the use case anchor-interpolation was added for.

4. **Tractable subset.** Several smaller patches sit at >50%
   interp-tight coverage (`improvements/barlov.event`, `containers.event`,
   `hash.event`). Those are the highest-leverage candidates to
   manually finish and validate first.

## What's blocking real progress

1. **An EU ROM** (SHA1 `cff199b36ff173fb6faf152653d1bccf87c26fb7`).
   Without it the byte-pattern probe can't run, and pattern matching
   is the highest-yield way to translate the patches we can't sym-map.

2. **An EU build of the zeldaret-tmc decomp.** The repo at
   `~/tmc-reference/zeldaret-tmc/` only has a USA build under
   `build/USA/tmc.map` (16129 symbols). Producing `build/EU/tmc.map`
   needs an EU ROM + a `make game_version=EU` run of the decomp.

With both in place, the translator becomes high-throughput:

  - Decomp-map matching resolves any offset that lands inside a
    labelled symbol — should be the vast majority of patch ORGs.
  - Byte-pattern probe catches the rest, with a one-time
    disambiguation pass for non-unique patterns.

Realistic path to "all 33 patches USA-ready":

  | Step | Effort |
  |------|--------|
  | Build EU decomp + obtain EU ROM | hours |
  | Re-run `scan_patches.py` with --eu-map / --eu-rom | minutes |
  | Run translator over all 33 patches | < 1 minute |
  | Manual audit of remaining `// UNRESOLVED` lines | 1–2 days |
  | Test-roll seeds, fix regressions per patch | 1–2 weeks |
  | Upstream PR to minishmaker/randomizer | days of review |

**Total: 2–3 weeks of focused work, ideally as an upstream contribution
so it doesn't perpetually diverge from the randomizer's main branch.**

## How to run

Single-file translation:

```sh
python3 tools/randomizer_usa/translate_offsets.py \
    libs/randomizer/RandomizerCore/Resources/Patches/randomLanguage.event \
    out/translated.event \
    --usa-map ~/tmc-reference/zeldaret-tmc/build/USA/tmc.map \
    --eu-map  /path/to/eu/tmc.map         # OPTIONAL — enables sym matching
    --usa-rom /home/sian/tmc/baserom.gba  # OPTIONAL — enables byte probe
    --eu-rom  /path/to/baserom_eu.gba     # OPTIONAL — same
```

Bulk coverage scan:

```sh
python3 tools/randomizer_usa/scan_patches.py \
    --csv tools/randomizer_usa/reports/coverage_with_eu_data.csv \
    --eu-map  /path/to/eu/tmc.map \
    --usa-map ~/tmc-reference/zeldaret-tmc/build/USA/tmc.map \
    --eu-rom  /path/to/baserom_eu.gba \
    --usa-rom /home/sian/tmc/baserom.gba
```

## Suggested next steps in priority order

1. **User supplies an EU ROM** somewhere reachable. SHA1
   `cff199b36ff173fb6faf152653d1bccf87c26fb7`.
2. **Build the EU decomp** to produce `build/EU/tmc.map`:
   ```sh
   cd ~/tmc-reference/zeldaret-tmc && make game_version=EU
   ```
3. **Re-run `scan_patches.py`** with `--eu-map` and `--eu-rom`. The
   `unresolved` column should drop dramatically.
4. **Translate** `ROM Buildfile.event` (the master include) end-to-end,
   audit each `UNRESOLVED` by hand, promote any verified pair into
   `KNOWN_PAIRS`.
5. Iterate through the other 27 patches in size order; each newly-
   resolved address becomes a `KNOWN_PAIRS` entry, making subsequent
   runs faster.
6. Smoke-test by rolling a real USA seed once `ROM Buildfile.event` is
   USA-clean — even a half-translated build will fail at apply time
   if any write lands inside another patch's territory.

## License note

Any changes to `libs/randomizer/RandomizerCore/Resources/Patches/` are
modifications of GPL-3.0 source. If the work eventually lives in our
fork rather than upstreamed, our fork must also be GPL-3.0 (and we
must keep tmc_pc as a separate, shell-out-only program — see
`libs/randomizer.md`).

## File map

```
tools/randomizer_usa/
├── README.md              — this file
├── translate_offsets.py   — per-file EU→USA translator
├── scan_patches.py        — bulk scanner / coverage report
└── reports/
    └── coverage_no_eu_data.csv  — baseline (interp-only) coverage
```
