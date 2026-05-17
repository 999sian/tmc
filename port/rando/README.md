# port/rando/ — In-process clean-room randomizer

A C++ rewrite of the randomizer logic that runs inside tmc_pc. No
shell-out to MinishCapRandomizerCLI, no .NET dependency, no separate
binaries. Just a function call: `Rando_RollSeed(seed) → result`.

## Why this exists

The vendored randomizer at `libs/randomizer` is GPL-3.0 C# that runs as
a child process. Two real costs:

1. **Two binaries to ship** — tmc_pc + MinishCapRandomizerCLI (~95 MB
   between them with the self-contained .NET runtime).
2. **License contamination** — linking the GPL-3.0 code in-process
   would force tmc_pc to also be GPL-3.0.

This port is **clean-room**: we observe the input/output formats (`.logic`
files, ROM offsets, spoiler text) but don't translate the C# source.
That keeps tmc_pc's license unrestricted.

## Milestone plan (incremental, each delivers value)

  M1 — Kinstone shuffle  (this commit, days)
      Random permutation of the 136 kinstone-fusion entries in
      gFuseActions. In-process. No logic engine. Drop-in alternative
      to the C# CLI for users who only want kinstone shuffle.

  M2 — Item-table shuffle  (weeks)
      Shuffle the major-item drops (chests, NPCs, kinstone rewards)
      using a simple "place + check reachability" loop. Still no
      logic-file parser — locations and dependencies hardcoded.

  M3 — Logic-file parser  (weeks)
      Parse `.logic` files into an in-memory location/item graph.
      Replaces hardcoded tables from M2.

  M4 — Patch application  (weeks)
      Native replacement for ColorzCore's EventAssembler. Reads
      `.event` files, applies ORG/POIN/BYTE/etc. writes to the ROM
      buffer. Drops the C# CLI from the dist entirely.

  M5 — Full parity  (months)
      Settings YAML, dungeon entrance shuffle, music shuffle,
      cosmetics. At this point libs/randomizer can be unvendored.

## Files

  rando.h               — public API: Rando_RollSeed, Rando_GetSpoiler
  rando_kinstone.cpp    — M1 implementation: gFuseActions shuffle
  rando_rom_io.cpp      — read/write helpers over the loaded gRomData

(more files added per milestone)

## Clean-room note

Because tmc_pc previously linked to the C# randomizer as a submodule,
some patterns may overlap by coincidence. The legal cleanliness of
"clean-room" depends on the implementer never having read the GPL-3
source. Anyone unsure should treat this directory as license-
unspecified until reviewed.

## Testing without an EU ROM

M1 onwards only writes to memory regions documented in the USA decomp
symbol map (`~/tmc-reference/zeldaret-tmc/build/USA/tmc.map`). All
shuffled addresses are validated against named USA symbols at runtime
so we never scribble over unmapped data — the same safety invariant
documented in `tools/randomizer_usa/reports/interp_validation.md`.
