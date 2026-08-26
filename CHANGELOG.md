# Changelog

## v1.0.1 — Mt. Crenel hotfix

Fixes a single rendering bug reported within hours of v1.0.0. Everything from
v1.0.0 is preserved; no save or configuration migration is required.

**This is also the first update deliverable through the in-app updater** — if
you are on v1.0.0, the game will offer it to you under CONFIG → SYSTEM.

### Fixed

- **Black background at the top of Mt. Crenel.** At the mountaintop the
  weather-change manager cross-fades the background palettes from a 26-palette
  block (`gPalette_549`). The port allocated that block but the code that was
  supposed to fill it from the ROM's palette data was never written, so it
  stayed all zeros — and a palette of zeros is pure black. The cross-fade
  blended the background to black while Link, enemies and the HUD (sprite
  palettes, untouched by the fade) rendered normally. The block is now
  populated from the ROM at load, verified against the USA ROM's actual
  palette data.

## v1.0.0 — the first stable release

The first release this project considers stable. It adds an in-app updater, a
live position marker on the second-screen map, and a rebuilt autosave store,
and it corrects the project's attribution of its own lineage.

Everything from v0.1.2 is preserved. No save or configuration migration is
required: existing `tmc.sav`, autosave data and `config.json` are used as-is.

### Added

- **In-app updater.** The game can now check for, download and install its own
  updates from the second screen, under SYSTEM. It is deliberately conservative:

  - Update metadata comes from a project-controlled manifest with a **closed
    schema** — any unknown key, duplicate key, wrong channel, non-HTTPS URL or
    malformed field causes the whole manifest to be rejected. It is not a
    GitHub release scrape.
  - The expected **size and SHA-256 are pinned before a single byte** of the
    new build is fetched, and the download is verified against them.
  - Downloads are HTTPS-only, with HTTPS-only redirects and certificate
    verification.
  - Installing **creates and verifies a backup** of the current build
    (`tmc_aleks_ultimate_nx.bak`) before the installed game is touched. If any
    step fails, the backup is restored automatically and the installed game is
    left exactly as it was.
  - Progress is journalled, so an interrupted install is recoverable rather
    than leaving a half-written game.

  After installing, close the game and open it again to run the new version.

- **Link's position marker on the second-screen MAP tab.** The map now shows
  where you actually are, live.

### Changed

- **Rebuilt autosave storage.** Autosave now uses a checksummed three-slot ring
  with an explicit discovery rule, never renames a file into place, and
  migrates older autosave data forward on first run. The goal is that a crash
  or a power loss mid-write can never leave you without a readable autosave.
- **Corrected lineage and attribution.** Earlier releases credited
  [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds) only
  as a *reference*. That was wrong. This edition's core actually **descends
  from that project** — the Switch tree was branched from its native port at
  commit `afdde1b7` (2026-05-10), where 742 of 768 core source blobs match.
  It is now credited as a direct code ancestor. See
  [CREDITS.md](CREDITS.md#on-the-estebanpdn-fork-point).

### Fixed

- **Pause map regressions.** A stale HDMA state is now reset when the pause map
  is opened, map layer offsets use the native layout, and missing map assets
  are repaired on the fly instead of rendering wrong.
- **Autosave recovery.** Autosave data written by older versions is quarantined
  rather than misread, and recovery no longer depends on write ordering that
  the SD card does not guarantee.
- Menu fixes in the figurine viewer and the pause menu.
- Further native-port gameplay fidelity work, including collision handling.

### Known limitations

- True widescreen is not part of this release.
- The updater replaces the game NRO only. Your ROM, saves, assets and settings
  are never touched by it.

## v0.1.2 — sword hitbox hotfix

A minimal collision hotfix. No other gameplay, content or configuration
changes: everything from v0.1.1 is preserved.

### Fixed

- **Sword collision could be mirrored to the wrong side during certain
  right-facing attacks.** The horizontal mirror applied to the sword's
  damage hitbox was not taking effect, so those attacks tested collision on
  the opposite side of Link from the one being attacked.
- Improved native-port gameplay fidelity.

### Notes

- No save or configuration migration is required.

## v0.1.1 — clean-install hotfix

A first-run and stability hotfix. No gameplay content changes: everything
from v0.1.0 is preserved.

### Fixed

- **Clean-install first boot on real Nintendo Switch hardware.** A fresh
  `/switch/tmc/` containing only the NRO and your own ROM now generates its
  runtime assets and reaches the title screen.
- **Build-state path handling on `sdmc:/`.** The Switch device prefix broke
  path canonicalisation, so a successful extraction was recorded as a
  failure.
- **Repeated asset extraction.** The cache state marker was never written,
  so every launch re-extracted from scratch.
- **Extraction stability and filesystem durability**, including bounds and
  overflow hardening in the asset reader and LZ77 self-reference handling.
- Standard error is routed to an internal sink on Switch; no `/dev/null` is
  used, which previously could be fatal.
- The applet message queue is pumped during extraction, so the system no
  longer sees the app as unresponsive while it works.

### Changed

- **First-run extraction is faster**: development-only editable output is no
  longer written. Expect around a minute or more on the first launch,
  depending on SD-card speed; later launches reuse the cache.
- The contextual Ezlo/hat shortcut now defaults to **Left Stick click
  (LSTICK)** instead of **L**, so L keeps its normal GBA and menu duties.
  `CONTEXT L` is still selectable in CONFIG. An existing configuration still
  on the old default is migrated automatically; a binding you chose yourself
  is left alone.

### Not in this release

- Automatic CPU clock management. It is still being evaluated and no clock
  is touched, exactly as in v0.1.0.
- The update checker, deferred to v0.2.0. Nothing is downloaded or installed
  by this release.

## v0.1.0 — first public release

The first public build of Alek's Ultimate NX Edition. Everything below is
relative to the upstream base this Switch tree was forked from (see
[CREDITS.md](CREDITS.md)).

### Nintendo Switch port

- Native Switch target (libnx + SDL), packaged as homebrew NRO with custom
  icon.
- Threaded software PPU rendering (per-scanline worker pool) for real-hardware
  performance.
- Application-mode oriented; docked and handheld both supported.

### Display layouts

- Three runtime-switchable display modes: **NORMAL**, **DUAL** (game +
  second-screen panel) and **FLIP 270** (vertical composition for physically
  rotated play).
- Configurable geometry: DUAL game scale 2X/3X, DUAL panel size 70–90%, FLIP
  panel size 70–100%, and adjustable screen gaps; NORMAL integer screen
  scaling.
- Quick display-mode switch chord (PLUS + ZR).

### Second screen

- Four-tab companion panel rendered from the game's own ROM-decoded art:
  **QUEST** (Story Guide: quest, objective, hint, location), **MAP**
  (overworld and dungeon maps, live player marker, follow cam, windcrest
  pins, floor auto-return), **ITEMS** (A/B rings plus X/Y/ZL/ZR shortcut
  slots), **CONFIG** (all settings).
- Touchscreen interaction across tabs, settings and item assignment.
- Panel backdrop styles (parchment pattern / cream / dark).
- Contextual R-button **Action Hint** with OFF / CONTEXTUAL / ON setting.

### Controls

- Full rebinding of the six GBA controls plus a Controls Reference screen.
- Dedicated **Talk to Ezlo** shortcut (contextual L by default, rebindable).
- Four extra item shortcut slots (X/Y/ZL/ZR) with persistent assignments.

### RetroAchievements

- Login/logout, game recognition, Rich Presence, real unlock notifications
  with real badge artwork (cached), six toast styles, and a local test
  notification.

### Localization

- Port-owned UI localized into English, Español, Français, Deutsch, Italiano —
  including the Story Guide. Correct accented rendering throughout the panel
  UI. (Nintendo's original in-game dialogue localization is untouched.)

### Saves / system

- Port-managed **Autosave** with **Load Autosave** (separate `autosave.bin`;
  never overwrites the game's own save).
- **Return to Title** with confirmation.
- Read-only **Version** row (v0.1.0).

### Performance / startup

- Substantially reduced startup time by removing a legacy per-page ROM
  extraction step from boot (roughly 13–14 s to title observed on tested
  hardware).
- Second-screen repaint caching so the panel only repaints when its content
  changes.

### Rendering fixes

- Fixed the **X and Y item shortcuts being swapped**: the default bindings had
  the X slot firing on the physical Y button and vice versa. Physical X now
  drives the X shortcut and physical Y the Y shortcut. A config written by an
  earlier build is corrected automatically on first launch, unless its
  shortcut bindings were hand-edited (those are left alone).
- Fixed the Deepwood Shrine rotating-barrel room rendering flat/untextured:
  the affine background's per-scanline HBlank-DMA register updates are now
  applied and the BG2 reference point is modelled as the hardware latch it is,
  fixing both the missing texture and its vertical phase during rotation.
  Verified on real hardware.

### UI polish

- Heart display laid out for the full 20-heart maximum at a constant heart
  size (wraps rows instead of shrinking).
- Larger, more readable rupee counter, consistent across tabs.
