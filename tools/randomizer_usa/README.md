# randomizer_usa — USA-region support for the Minish Cap Randomizer

**Status: Phase-1 scaffolding only. Real USA support remains weeks of
detailed offset work.** This directory is the staging ground for that
work; nothing here applies USA patches yet.

## Why USA support is hard

The randomizer's patch system (`libs/randomizer/RandomizerCore/Resources/Patches/`)
contains **245 patch files** with hundreds of hardcoded EU ROM offsets
of the form `ORG $hex`. Examples from `ROM Buildfile.event` alone:

```
ORG  $EDD8+2; BYTE 0x5C 0x70      // lady kinstone
ORG  $DA58+2; BYTE 0x5C 0x6C      // ghost kinstone
ORG $F2528+3; BYTE 0x01 0x00      // new item in Smith's room
ORG   $5D602; BYTE 0x63           // bell heart piece
... 100+ more in just this one file
```

These addresses are all positions in the **EU** Minish Cap ROM
(`BZMP`). They point at the EU byte where the randomizer needs to
write its patch — e.g. the byte that controls which item a kinstone
fusion gives. The USA ROM (`BZME`) has those same conceptual items
but at completely different file offsets (text shifts, language
tables, build differences).

Naively applying EU patches to a USA ROM **silently corrupts** it
(scribbles over unrelated code/data), which is exactly what the
randomizer does today if you feed it a USA ROM: `ROM.cs:59` sets
`Version = RegionVersion.Us`, then nothing in the patch pipeline
honours that distinction.

## What's done in this session

- **`translate_offsets.py`** — pure-Python parser/translator that
  reads a single `.event` file, extracts every `ORG $hex` directive,
  and emits a translated copy with each offset mapped EU→USA.
  Resolution strategy (priority order):
    1. Known-pair lookup (`KNOWN_PAIRS` dict)
    2. Decomp-map matching (requires EU and USA `tmc.map` from the
       zeldaret decomp)
    3. Byte-pattern probe across the two ROMs
    4. Otherwise: emit `// UNRESOLVED for USA` so an audit pass
       catches it.

- **`KNOWN_PAIRS`** — 14 EU/USA pairs harvested from
  `RandomizerCore/Core/Constants.cs::HeaderTable`. The randomizer
  already has these populated for both regions (header bases, palette
  set tables, chunk tables, etc.); they're the seed of the mapping.

- **Smoke test on `randomLanguage.event`** — runs cleanly and marks
  the single hardcoded ORG (`$5E946`) as `UNRESOLVED` because we have
  no EU map or EU ROM in this environment.

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
  - Byte-pattern probe catches the rest, with a one-time disambiguation
    pass for non-unique patterns.

Realistically the path to "all 245 patches USA-ready" is:

  | Step | Effort |
  |------|--------|
  | Build EU decomp + obtain EU ROM | hours |
  | Run translator over all 245 patches | < an hour once data is in place |
  | Manual audit of every `// UNRESOLVED` line | days |
  | Test-roll seeds, fix regressions per patch | weeks |
  | Upstream PR to minishmaker/randomizer | days of review + iteration |

**Total: 2-4 weeks of focused work, ideally as an upstream contribution
so it doesn't perpetually diverge from the randomizer's main branch.**

## How to run what's here today

```sh
python3 tools/randomizer_usa/translate_offsets.py \
    libs/randomizer/RandomizerCore/Resources/Patches/randomLanguage.event \
    out/translated.event \
    --usa-map ~/tmc-reference/zeldaret-tmc/build/USA/tmc.map \
    --eu-map  /path/to/eu/tmc.map         # OPTIONAL — enables sym matching
    --usa-rom /home/sian/tmc/baserom.gba  # OPTIONAL — enables byte probe
    --eu-rom  /path/to/baserom_eu.gba     # OPTIONAL — same
```

Sample output (no maps/ROMs in this environment):

```
randomLanguage.event → /tmp/translated.event
  hit:  0
  miss: 1
    unresolved: 1
```

Adding either an EU map or both ROMs flips the unresolved count down.

## Suggested next steps in priority order

1. **User supplies an EU ROM** somewhere reachable to this environment.
2. Build the EU decomp:
   ```sh
   cd ~/tmc-reference/zeldaret-tmc && make tmc_eu
   ```
   producing `build/EU/tmc.map`.
3. Re-run the translator over `ROM Buildfile.event` (the master
   include) — that's the highest-impact single patch.
4. Iterate: each newly-resolved address becomes a `KNOWN_PAIRS` entry,
   making subsequent runs faster.
5. Once `ROM Buildfile.event` is fully USA-clean, attack the 244 other
   patch files in dependency order (`extDefinitions.event`,
   `containers.event`, `hearts.event`, …).

## License note

Any changes to `libs/randomizer/RandomizerCore/Resources/Patches/` are
modifications of GPL-3.0 source. If the work eventually lives in our
fork rather than upstreamed, our fork must also be GPL-3.0 (and we
must keep tmc_pc as a separate, shell-out-only program — see
`libs/randomizer.md`'s license notes).
