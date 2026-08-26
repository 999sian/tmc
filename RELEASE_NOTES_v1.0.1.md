# Alek's Ultimate NX Edition v1.0.1

A hotfix for a single rendering bug at the top of Mt. Crenel, reported within
hours of v1.0.0. Everything from v1.0.0 is preserved — no save or
configuration migration is required.

**If you are on v1.0.0 you do not need to download anything from this page:**
open the game, go to **CONFIG → SYSTEM** on the second screen, and the in-app
updater will offer v1.0.1. This is the updater's first real release.

## Fixed

- **Black background at the top of Mt. Crenel.** The mountaintop's
  weather-change effect cross-fades the background palettes from a 26-palette
  block that the port allocated but never filled from the ROM — it stayed all
  zeros, and a palette of zeros is pure black. The background faded to black
  while Link, enemies and the HUD (sprite palettes, untouched by the fade)
  stayed correct. The block is now populated from the ROM's palette data at
  load.

Thanks to the player who reported it with a screenshot — that made the
diagnosis fast.

## Verification

    SHA-256  58649bb6f2d677f95399e501cb09d5cca3503fee7f51f62b5d78ea6523a8ea09
             tmc_aleks_ultimate_nx_v1.0.1.nro
    Size     13870771 bytes
