# Alek's Ultimate NX Edition v1.0.0

The first release this project considers stable.

Everything from v0.1.2 is preserved. No save or configuration migration is
required — existing `tmc.sav`, autosave data and `config.json` are used as-is.

## Added

### In-app updater

The game can now check for, download and install its own updates from the
second screen, under **CONFIG → SYSTEM**. No PC required after this release.

It is deliberately conservative about touching your installed game:

- Update metadata comes from a project-controlled manifest with a **closed
  schema**. An unknown key, a duplicate key, a non-HTTPS URL, the wrong channel
  or a malformed field rejects the whole manifest instead of being ignored.
  It is not a GitHub release scrape.
- The expected **size and SHA-256 are pinned before a single byte** of the new
  build is downloaded, and the download is verified against them.
- HTTPS only, HTTPS-only redirects, certificates verified.
- Installing **creates and verifies a backup** of your current build at
  `/switch/tmc/tmc_aleks_ultimate_nx.bak` before the installed game is touched.
  If any step fails, that backup is restored automatically and your installed
  game is left exactly as it was.
- Progress is journalled, so an interrupted install is recoverable rather than
  leaving a half-written game.

After installing, **close the game and open it again** to run the new version.

The updater replaces the game program and nothing else. Your ROM, saves,
autosave data, extracted assets and settings are never read or written by it.

### Link's position marker on the MAP tab

The second-screen map now shows where you actually are, live.

## Changed

- **Rebuilt autosave storage** — a checksummed three-slot ring with an explicit
  discovery rule. It never renames a file into place, and it migrates older
  autosave data forward on first run, so a crash or power loss mid-write cannot
  leave you without a readable autosave.
- **Corrected lineage and attribution.** Earlier releases credited
  [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds) only
  as a *reference*. That was wrong, and this release fixes it. This edition's
  core **descends from that project**: the Switch tree was branched from its
  native port at commit `afdde1b7` (2026-05-10), where 742 of 768 core source
  blobs match. It is now credited as a direct code ancestor. See
  [CREDITS.md](CREDITS.md#on-the-estebanpdn-fork-point).

## Fixed

- **Pause map regressions** — stale HDMA state is reset when the pause map is
  opened, map layer offsets use the native layout, and missing map assets are
  repaired on the fly instead of rendering wrong.
- **Autosave recovery** — data written by older versions is quarantined rather
  than misread, and recovery no longer depends on write ordering the SD card
  does not guarantee.
- Menu fixes in the figurine viewer and the pause menu.
- Further native-port gameplay fidelity work, including collision handling.

## Known limitations

- True widescreen is not part of this release.
- Built and tested as the **USA** edition (`BZME`). Use a USA ROM.

## Installation

Copy the NRO to `/switch/tmc/`, provide your own legally obtained USA ROM, and
launch in application mode. Full guide in
[docs/INSTALLATION.md](docs/INSTALLATION.md).

If you are already on v0.1.2 or earlier, copy this NRO over
`/switch/tmc/tmc_aleks_ultimate_nx.nro` by hand once. From this version on, the
in-app updater handles it.

## Verification

    SHA-256  f3308db78e042a326509b5d7972fbb8cb71b9b7b1ed795b06546e8b7939771fc
             tmc_aleks_ultimate_nx_v1.0.0.nro
    Size     13870771 bytes
