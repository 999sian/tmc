# Installation (v0.1.0)

*[Versión en español → INSTALLATION_ES.md](INSTALLATION_ES.md)*

## Requirements

- A Nintendo Switch capable of running homebrew (tested with Atmosphère).
- Your **own, legally obtained** *The Minish Cap* **USA** ROM (`.gba`).
- An SD card.

**No ROM is included, no download link is provided, and no copyrighted game
content is distributed with this project.** Dump your own cartridge or obtain
the game legally.

## SD card layout

```
/switch/tmc/
    tmc_aleks_ultimate_nx_v0.1.0.nro    ← the port
    baserom.gba                         ← your USA ROM (any filename works)
```

1. Create `/switch/tmc/` on the SD card if it does not exist.
2. Copy the NRO there.
3. Copy your USA ROM into the same folder.

### ROM naming

The filename does **not** matter: the loader scans `/switch/tmc/` for any
`.gba` file and identifies *The Minish Cap* by its internal header
(`BZME` = USA). `baserom.gba` is the conventional name and is found first. If
several Minish Cap ROMs are in the folder, the first match wins — keep just
one to avoid ambiguity.

v0.1.0 targets the **USA** version. The known-good USA dump is SHA-1
`b4bd50e4131b027c334547b4524e2dbbd4227130` (not enforced at runtime, but it is
what this release was built and tested against).

## First boot

- Launch from the Homebrew Menu. **Application mode** (launching over a full
  title) is recommended; applet mode has less memory and is not the tested
  configuration.
- The first boot prepares a runtime asset cache from your ROM into
  `/switch/tmc/assets/` with an on-screen progress bar. This happens once;
  later boots skip it (roughly 13–14 seconds to title on tested hardware).
- The game then boots to the title screen.

## Files the port creates

| Path | Purpose |
|---|---|
| `/switch/tmc/assets/` | runtime asset cache generated from **your** ROM (safe to delete; it will be rebuilt) |
| `/switch/tmc/config.json` | settings and control bindings |
| `/switch/tmc/tmc.sav` | the game's normal save file |
| `/switch/tmc/autosave.bin` | the port-managed autosave (separate from the game's save) |
| `/switch/tmc/tmc.softslots` | X/Y/ZL/ZR shortcut assignments |
| `/switch/tmc/ra_badges/` | cached RetroAchievements badge images |
| `/switch/tmc/ra_token` | your RetroAchievements login token (created only if you log in; removed by LOG OUT) |

Deleting `assets/`, `config.json`, `ra_badges/` is always safe — they are
regenerated or reset. `tmc.sav` and `autosave.bin` are your game progress:
back them up before experimenting.

## Updating from an older build

Replace the NRO. Saves, config and the asset cache are kept. If a future
version changes the asset format it will rebuild the cache by itself.

## Uninstalling

Delete `/switch/tmc/`. (Copy `tmc.sav` / `autosave.bin` elsewhere first if you
want to keep your progress.)
