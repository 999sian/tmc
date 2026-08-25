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
      │
EstebanPdN/zelda-tmc-3ds        3DS dual-screen adaptation — the native port
      │                         this tree's core actually descends from
      │
HayatoG/tmc                     Switch port of the above; direct `origin` of
      │                         this development tree
      │
Alek's Ultimate NX Edition      this project (Switch-specific work)
```

### On the EstebanPdN fork point

Earlier releases of this edition listed `EstebanPdN/zelda-tmc-3ds` only as a
*reference*. That understated it, and this release corrects the record.

The core of this tree derives from `EstebanPdN/zelda-tmc-3ds`, not directly
from the decompilation: `HayatoG/tmc` branched that project's native port at
commit `afdde1b7dcebde9b66ada9a63ff0c8d51a1a6ab9` (2026-05-10) and adapted it
to the Switch. The two repositories share no commit hashes — upstream's history
was rewritten — so the relationship was re-established by content: hashing every
blob under `src/` and `include/` and scoring overlap across upstream's history.
At that commit **742 of 768 core blobs match**, and the score falls off sharply
on either side, so the fork point is unambiguous.

Esteban's project is therefore a **direct code ancestor** of this edition, and
is credited as such below.

## Foundational projects

| Project | Role in this edition |
|---|---|
| [zeldaret/tmc](https://github.com/zeldaret/tmc) | **Foundation.** The Minish Cap decompilation. All game logic originates here. |
| [Project Picori — 999sian/tmc](https://github.com/999sian/tmc) | **Foundation / direct code source.** The native PC port: SDL3 platform layer, the ViruaPPU software renderer approach, agbplay audio integration. GPL-3.0. |
| [samyost1/tmc-android](https://github.com/samyost1/tmc-android) | **Direct code source.** The dual-screen second-screen concept and implementation (panel, ROM-decoded UI art, touch inventory) that this edition's second screen descends from. GPL-3.0. Itself built on Project Picori and forked from [Raekwon1603/tmc-android](https://github.com/Raekwon1603/tmc-android) (Android packaging / second-screen scaffold). |
| [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds) | **Direct code ancestor.** The native 3DS dual-screen adaptation. This edition's core descends from it: `HayatoG/tmc` branched its native port at `afdde1b7` (2026-05-10) and made it a Switch port, so the great majority of the core C in this tree originates here. Also the console-adaptation reference for this Switch work, including widescreen research that is not shipped. GPL-3.0. |
| [HayatoG/tmc](https://github.com/hayatog/tmc) | **Base fork.** The repository this Switch tree is directly forked from (`origin` of the development tree), and where the Switch port of Esteban's native port was made. |

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
and the recommended tested base for v0.1.1 is the USA ROM.

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
