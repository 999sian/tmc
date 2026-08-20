# Changelog

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
