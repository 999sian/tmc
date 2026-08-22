# Known issues — v0.1.2

Current, honestly-stated limitations. Fixed issues are not listed.

## Performance

- **DUAL and FLIP can dip below 60 FPS in heavier scenes** (busy areas such as
  Minish Woods are the worst case). NORMAL has the most headroom and holds 60
  in normal play. See [PERFORMANCE.md](PERFORMANCE.md) for the optional
  1224 MHz recommendation.

## Display / rendering

- **Title screen after the Deepwood Shrine barrel (low severity).** After
  visiting the Deepwood Shrine rotating barrel, returning to the title screen
  may cause temporary affine/visual artifacts. A clean application restart
  restores the title screen normally. The title screen is unaffected on a
  normal launch, and the barrel room itself renders correctly — this is only
  about returning to title within the same session.

## Display / map

- In some interiors the second-screen map intentionally hides the player
  marker when no valid overworld position exists for the room — this is by
  design, not a lost marker.

## Features not in v0.1.2

- **True widescreen** is not available in this version (researched, planned
  for a later release).
- **Manual "save anywhere"** is not exposed; use the game's normal saving or
  the port's AUTOSAVE / LOAD AUTOSAVE.
- Offline RetroAchievements (queueing unlocks without a connection) is not
  supported; RA needs a network connection at play time.
- There is no in-app updater; updates are manual NRO replacement.

## Environment

- Applet mode (launching without holding a full title) is untested and may
  have memory pressure; **application mode is recommended**.
- Docked mode renders at the same internal resolution (upscaled by the
  console); the layouts are designed around the handheld 1280×720 screen.

If you hit something not listed here, please open an issue with your hardware
context and reproduction steps (see [../CONTRIBUTING.md](../CONTRIBUTING.md)).
