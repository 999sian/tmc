# port/ppu — vendored software GBA PPU

First-party, in-tree software GBA Picture Processing Unit for the Minish Cap
PC port. **GPL-3.0-or-later** (matches the project license).

## Provenance

Derived from **VirtuaPPU** by Mathéo Vignaud
(<https://github.com/MatheoVignaud/VirtuaPPU>), upstream commit
`5cf5e990d3ecb08ae00d266fea833ccc56286bd5`, plus this project's 15
accuracy/portability patches that were previously re-applied to a pinned git
submodule at build time. Those patch files
(`port/patches/viruappu-*.patch`) are preserved in git history; the canonical,
reconciled source now lives here directly.

This replaces the old model (pinned `libs/ViruaPPU` submodule + an `xmake.lua`
`before_build` step that ran `git apply` on every build). That model had two
problems this vendoring fixes:

1. **License** — the upstream submodule publishes no license, which is
   incompatible with shipping a GPL-3.0 binary.
2. **Fragility** — the patch set no longer cleanly re-applies onto pinned
   `5cf5e99` via `git apply -3` (the `mode2-affine-latch` hunk conflicts), so
   the build depended on a hand-reconciled dirty submodule worktree.

The vendored tree is verified byte-faithful to the previously-shipping render
output by `tools/ppu_parity_check.sh` (see `tools/ppu_golden_hashes.txt`).

## What's here

- `src/virtuappu.c` — dispatcher (`virtuappu_render_frame` switches on mode).
- `src/mode1.c` — the load-bearing tiled renderer (GBA mode 0): 4 text BGs,
  OBJ, windows, blending, mosaic; per-scanline IO snapshot + OpenMP render.
- `src/mode2.c` — affine BG2 path (GBA modes 1/2).
- `src/mode0.c`, `src/mode7.c` — **dead on the TMC path** (mode0 is a black
  stub; mode7 is a misnamed Game Boy DMG renderer). Slated for removal.
- `include/` — public API (`virtuappu.h`, `ppu_memory.h`) + internal headers.

The port bridges to this via `port/port_ppu.cpp`; consumers also include
`port/port_draw.c`, `port/port_hdma.c`, `port/port_linked_stubs.c`,
`src/scroll.c`, and the parity tools.
