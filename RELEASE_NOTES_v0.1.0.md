# Alek's Ultimate NX Edition v0.1.0

The first public release of a personal Nintendo Switch edition of the native
*The Minish Cap* port. I built it because I wanted to play the game this
specific way on my own Switch — dual-screen in the spirit of the DS Zeldas, a
vertical Flip-Grip-style mode, and achievements. It's shared in case others
want to play it, study it, or fork it.

This project did not create the Minish Cap decompilation, the native PC port,
or the original dual-screen work. It builds on them — see
[CREDITS.md](CREDITS.md).

## Highlights

- **Three display modes** — NORMAL, **DUAL** (game + companion panel) and
  **FLIP 270** (vertical, for physically rotating the console).
- **Second-screen panel** with **QUEST** (Story Guide), **MAP** (overworld and
  dungeon, live player marker), **ITEMS** (A/B rings + X/Y/ZL/ZR shortcuts)
  and **CONFIG**, all decoded from the game's own art.
- **Touchscreen** interaction on the panel.
- **Port UI localized** in English, Español, Français, Deutsch and Italiano.
- **RetroAchievements** — login/logout, game recognition, Rich Presence, real
  unlock notifications with real badge artwork.
- **Autosave / Load Autosave** — port-managed, separate from and never
  overwriting the game's own save.
- **Return to Title** and a read-only **Version** row.
- **Action Hint** — OFF / CONTEXTUAL / ON for the panel's R-button hint.
- **UI polish** — heart display laid out for the full 20-heart maximum at a
  constant size, and a larger, more readable rupee counter.
- **Fast Boot** — startup was observed at roughly 13–14 seconds on tested
  hardware (may vary with SD card and system conditions).
- **Deepwood Shrine barrel fix** — the rotating-barrel room rendered flat and
  untextured; its per-scanline affine background is now correct. Verified on
  real hardware.
- **X/Y shortcut correction** — the X and Y item shortcuts were swapped
  relative to the physical buttons. An old config is corrected automatically
  on first launch.

## Performance

NORMAL has the most headroom. DUAL and FLIP are more demanding than NORMAL. On
tested hardware, a 1224 MHz CPU clock is recommended for performance closer to
a consistent 60 FPS. This is optional and does not guarantee a locked 60 FPS in
every scene. See [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Installation

Copy the NRO to `/switch/tmc/` on your SD card, put your own `.gba` ROM in the
same folder, and launch from the Homebrew Menu (application mode recommended).
Full guide: [docs/INSTALLATION.md](docs/INSTALLATION.md) ·
[español](docs/INSTALLATION_ES.md).

## ROM requirement

**No ROM is included and no download links are provided.** You must supply
your own legally obtained copy. This release targets the **USA** version
(header `BZME`); `baserom.gba` in `/switch/tmc/` is the recommended name,
though the loader accepts any `.gba` filename and identifies the game by its
header. Reference USA dump SHA-1
`b4bd50e4131b027c334547b4524e2dbbd4227130` (not enforced at runtime — it is
simply what this build was tested against).

Verify your download against
[`SHA256SUMS.txt`](release/SHA256SUMS.txt):

```
fddd091156a7619dff8e74484c25476ce1adf7345d95f2430b27b3cac9ec2079  tmc_aleks_ultimate_nx_v0.1.0.nro
```

## Known issues

- Heavier scenes may dip below 60 FPS in DUAL/FLIP; NORMAL has more headroom.
- After visiting the Deepwood Shrine rotating barrel, returning to the title
  screen may cause temporary affine/visual artifacts. A clean application
  restart restores the title screen normally. (The barrel room itself renders
  correctly.)
- True widescreen is not part of this version.
- Manual "save anywhere" is not exposed; use the game's own saving or the
  port's Autosave / Load Autosave.
- Offline RetroAchievements and an in-app updater are not supported.

Full list: [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) ·
[español](docs/KNOWN_ISSUES_ES.md).

## Development transparency

This project was developed with significant AI assistance (Claude / Claude
Code, ChatGPT, OpenAI Codex) for investigation, debugging, implementation,
review and documentation. Project direction, feature decisions, visual
judgement, real-hardware testing and all release decisions were handled
manually by the maintainer.
[docs/DEVELOPMENT_TRANSPARENCY.md](docs/DEVELOPMENT_TRANSPARENCY.md)

## Maintenance

This is a personal project. The goal is to make it fully playable and stable
for its intended use; after that, updates may become infrequent or stop. No
long-term support is promised. Forks are welcome under the applicable upstream
licenses.

## Thanks

To [zeldaret/tmc](https://github.com/zeldaret/tmc) for the decompilation,
[Project Picori](https://github.com/999sian/tmc) for the native port
foundation, [samyost1/tmc-android](https://github.com/samyost1/tmc-android)
for the dual-screen work this extends,
[EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds) for
the console adaptation reference, and
[HayatoG/tmc](https://github.com/HayatoG/tmc) for the Switch base — plus the
RetroAchievements community and everyone maintaining devkitPro and the Switch
homebrew stack.

---

Unofficial fan project, not affiliated with or endorsed by Nintendo.
