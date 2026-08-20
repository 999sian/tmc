# Credits

Alek's Ultimate NX Edition is the top layer of a long chain of community work.
None of the foundations below were created by this project, and this edition
would not exist without them.

## Lineage

Verified from repository remotes, in-tree statements and file identity during
release preparation:

```
zeldaret/tmc                    the decompilation (the game as C source)
      │
Project Picori (999sian/tmc)    native PC port: SDL3, software PPU, agbplay
      │
samyost1/tmc-android            dual-screen second-screen mod of Picori
      │                         (forked from Raekwon1603/tmc-android packaging)
      ├── EstebanPdN/zelda-tmc-3ds   3DS dual-screen adaptation (reference)
      │
HayatoG/tmc                     the direct base fork of this Switch tree
      │
Alek's Ultimate NX Edition      this project (Switch-specific work)
```

## Foundational projects

| Project | Role in this edition |
|---|---|
| [zeldaret/tmc](https://github.com/zeldaret/tmc) | **Foundation.** The Minish Cap decompilation. All game logic originates here. |
| [Project Picori — 999sian/tmc](https://github.com/999sian/tmc) | **Foundation / direct code source.** The native PC port: SDL3 platform layer, the ViruaPPU software renderer approach, agbplay audio integration. GPL-3.0. |
| [samyost1/tmc-android](https://github.com/samyost1/tmc-android) | **Direct code source.** The dual-screen second-screen concept and implementation (panel, ROM-decoded UI art, touch inventory) that this edition's second screen descends from. GPL-3.0. Itself built on Project Picori and forked from [Raekwon1603/tmc-android](https://github.com/Raekwon1603/tmc-android) (Android packaging / second-screen scaffold). |
| [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds) | **Reference.** The native 3DS dual-screen adaptation, studied as the console-adaptation reference for this Switch work (including its widescreen research, which is not shipped in v0.1.0). GPL-3.0. |
| [HayatoG/tmc](https://github.com/hayatog/tmc) | **Base fork.** The repository this Switch tree is directly forked from (`origin` of the development tree). |

## Switch-specific work in this edition

Switch platform layer (libnx integration, display layouts NORMAL / DUAL /
FLIP 270, presenter, render worker pool), second-screen Switch renderer and
theme, Story Guide content system, port UI localization (EN/ES/FR/DE/IT),
RetroAchievements integration on Switch (badges, toasts, logout),
port-managed autosave UX, Return to Title, startup-time work, CPU clock
handling, and assorted rendering fixes (including the mode-2 affine/HBlank-DMA
per-scanline fix). Much of this builds directly on the layers above.

## Localization

The multilingual layer in this edition covers the **port-owned UI only** —
the second-screen panel, the settings, the Story Guide and the port's own
prompts — in English, Spanish, French, German and Italian. It was implemented
for this edition as part of the Switch-specific work.

Localization implementation and wording were informed by the original regional
releases and by related community port work
([HayatoG/tmc](https://github.com/hayatog/tmc),
[Project Picori](https://github.com/999sian/tmc),
[samyost1/tmc-android](https://github.com/samyost1/tmc-android),
[EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds)).

The game's own dialogue and text are Nintendo's original translations, read at
runtime from the user's ROM. This project does not claim authorship of them,
and the recommended tested base for v0.1.0 is the USA ROM.

## Third-party libraries

| Library | Use | License |
|---|---|---|
| [libnx](https://github.com/switchbrew/libnx) | Switch homebrew runtime | ISC |
| [SDL](https://github.com/libsdl-org/SDL) (switch port) | video/input/audio platform layer | zlib |
| ViruaPPU / VirtuaAPU (vendored in base tree) | software PPU / APU emulation cores | see THIRD_PARTY_NOTICES.md — no license file in the vendored copies; provenance from the Picori line |
| [rcheevos](https://github.com/RetroAchievements/rcheevos) | RetroAchievements client | MIT (see LICENSES/) |
| [agbplay](https://github.com/ipatix/agbplay) (agbplay_core) | GBA music engine | LGPL-3.0 (see LICENSES/) |
| [libpng](http://www.libpng.org/pub/png/libpng.html) | RA badge decoding | libpng/zlib license |
| [nlohmann/json](https://github.com/nlohmann/json) | configuration | MIT |
| xBRZ (upscaler in base tree) | optional upscaling | see base tree notices |

## Also

- The **RetroAchievements** community and site (achievement sets, artwork,
  APIs) — [retroachievements.org](https://retroachievements.org).
- Everyone in the GBA decompilation and Switch homebrew communities whose
  tooling (devkitPro, Atmosphère, hbmenu) makes projects like this possible.

## Attribution corrections welcome

If you believe your work is used here and is not credited correctly — or if
you are an upstream author who would like your attribution or redistribution
terms adjusted — please open an issue and the project will be corrected
accordingly. The intent is to over-credit, never to under-credit. Licensing
details, including components whose upstream repositories do not state a
license, are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
