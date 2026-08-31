# Alek's Ultimate NX Edition v1.3.0

The game in Brazilian Portuguese, a dungeon that could not be finished, and
achievements that survive having no internet.

## Added

### Português (Brasil) — the whole game

Not just the menus. Every line of dialogue, every sign, every item description:
**2910 messages across 80 banks**, translated to Brazilian Portuguese, not
European. *Salvar*, never *guardar*. *Você*, never *tu*. *Tela*, never *ecrã*.

It runs on the **USA ROM you already have**. No EU ROM, no patched Brazilian
ROM, no second download — language and ROM region are independent here.

Accented characters (ã õ ç ê á í) are composed from the existing font at
runtime, so they render at the same size and weight as everything else rather
than as imported artwork that does not match.

Select it under `SETTINGS → GENERAL → LANGUAGE`.

### Offline achievements

RetroAchievements now works with no network. The game caches your session so
the achievement set still loads offline, and unlocks earned without a
connection are written to your SD card and sent when you reconnect — **with
their original unlock time**, not the time you came back online.

This follows the pattern RetroAchievements approved for RAOfflineProxy, and
carries the same condition: **softcore only**. A hardcore unlock is never
queued and never faked.

> **This is the newest code in the release and it has had the least testing.**
> Earning an achievement offline and syncing it later was not verified on
> hardware before publishing. If achievements misbehave, that is the first
> thing to suspect — please report it with `/switch/tmc/ra.log`.

## Fixed

### Cave of Flames B1 — the missing lava platform

The moving platform in the Rollobite lava room never appeared, leaving a
crossing that could not be made. This was not a gameplay bug at all.

The asset index is generated from the decompilation's `.incbin` directives. A
data symbol that begins with a pointer word is recorded four bytes past where
it actually starts, so the extractor sized that room's platform data as **4
bytes instead of 32**. The spawner then walked straight off the end of the
buffer, created hundreds of platforms out of unrelated memory, and never
created the real one.

Two symbols in the ROM have that shape. Both now carry their true extent, and
— more importantly for anyone already playing — **the game now falls back to
the ROM whenever a room property is shorter than the structure it holds**. You
do not need to delete or regenerate your extracted assets; the fix reaches you
in the binary. It is noted in `/switch/tmc/assetfix.log` when it happens.

The same idea already protected the pause world map. The extracted assets are
an optimisation; the ROM is the source of truth.

### Second screen — the map no longer spoils Hyrule

Regions you have not discovered are covered on the touch map, and tapping one
does nothing, matching what the in-game map knows. Previously the whole
overworld was legible from a fresh save.

### Smaller fixes

- Minish Path leaves and parallax no longer glitch at the top of vertical rooms
- Sword charge graphics facing east
- Arrow collision in all four directions
- Gleerok's fire, and its neck segment bounds
- Pullable mushroom child lifetime
- Delayed entity loader spawn-bit ordering
- A late-game Vaati progression flag that could leave a save unable to advance
- Graphics table bounds hardened against out-of-range indices

## Should you update?

Yes, especially if you play in Portuguese, use RetroAchievements, or ever got
stuck in Cave of Flames.

Your saves, autosaves and settings are untouched by this update. If you switch
to Portuguese and later go back to an older build, restore your `config.json`
first — an older build does not know the new language id.

## Honest limits

The Cave of Flames fix and the Portuguese translation were verified on
hardware. The offline achievement queue was not. Nothing in this release
changes how saves are written.

## Verification

    SHA-256  12b3aa9cf2f78007265d5741a2566b0840ab012ffd366f471e3e661ac3883c2f
             tmc_aleks_ultimate_nx_v1.3.0.nro
    Size     14202547 bytes
