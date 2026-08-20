# Third-party notices

This project builds on, links against, or descends from the following works.
Copies of license texts that ship in the development tree are in
[`LICENSES/`](LICENSES/). This file records what release preparation could
verify from the actual tree — where provenance is uncertain it says so rather
than guessing (see also `PUBLICATION_REVIEW.md`).

## Upstream project lineage (code this edition descends from)

| Project | License status found |
|---|---|
| [zeldaret/tmc](https://github.com/zeldaret/tmc) | Decompilation project; the game's code and assets remain Nintendo's copyright — which is why **no game content ships with this project** and the user must supply their own ROM. |
| [Project Picori — 999sian/tmc](https://github.com/999sian/tmc) | GPL-3.0 (per upstream repository). |
| [samyost1/tmc-android](https://github.com/samyost1/tmc-android) | GPL-3.0 (LICENSE present in upstream tree), with bundled components keeping their own licenses. |
| [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds) | GPL-3.0 (per upstream repository). |
| [HayatoG/tmc](https://github.com/hayatog/tmc) | **No overall repository license file found in the development tree at release-preparation time.** Its README documents licenses for some subcomponents (e.g. agbplay, LGPL-3.0) but does not declare a whole-repo license. This is flagged in `PUBLICATION_REVIEW.md`; source redistribution is withheld pending clarification. |

## Libraries

| Component | License | Notes |
|---|---|---|
| agbplay / agbplay_core ([ipatix/agbplay](https://github.com/ipatix/agbplay)) | **LGPL-3.0** | License text: [`LICENSES/agbplay-LICENSE.txt`](LICENSES/agbplay-LICENSE.txt). Linking it does not relicense the rest of the project. |
| rcheevos ([RetroAchievements/rcheevos](https://github.com/RetroAchievements/rcheevos)) | **MIT** | License text: [`LICENSES/rcheevos-LICENSE.txt`](LICENSES/rcheevos-LICENSE.txt). |
| libnx ([switchbrew/libnx](https://github.com/switchbrew/libnx)) | ISC | Linked via devkitPro toolchain. |
| SDL ([libsdl-org/SDL](https://github.com/libsdl-org/SDL), Switch port) | zlib | Linked via devkitPro portlibs. |
| libpng | libpng/zlib license | Linked via devkitPro portlibs. |
| zlib | zlib | Linked via devkitPro portlibs. |
| mbedTLS / libcurl | Apache-2.0 / curl license | Linked via devkitPro portlibs (HTTPS for RetroAchievements). |
| nlohmann/json | MIT | Header-only, vendored in base tree. |
| [ViruaPPU](https://github.com/MatheoVignaud/VirtuaPPU) / [VirtuaAPU](https://github.com/MatheoVignaud/VirtuaAPU) | **No license stated.** Vendored as submodules from MatheoVignaud's repositories; neither the vendored copies nor the upstream repositories carry a LICENSE file or a GitHub-displayed license. (Project Picori is itself a fork of `MatheoVignaud/tmc`.) Flagged in `PUBLICATION_REVIEW.md`. |
| xBRZ upscaler | see base-tree notices | Present in the base tree's upscaling path. |

## A note to upstream authors

This project's aim is to credit every upstream author accurately and to
respect their terms. Some components in the lineage do not state a license
publicly, which is recorded plainly above rather than assumed one way or the
other. **If you are an upstream author and would like your attribution or
redistribution terms adjusted, please open an issue — the project will be
corrected accordingly.**

## Trademarks / game content

*The Legend of Zelda: The Minish Cap* © Nintendo / Capcom / Flagship. All
game code (as decompiled), art, audio and text remain the property of their
owners. This project distributes **no ROM and no ROM-derived assets**; all
game data is read at runtime from the user's own copy.

## RetroAchievements

Achievement definitions, badge artwork and Rich Presence scripts are provided
by [retroachievements.org](https://retroachievements.org) and its community,
fetched at runtime for the logged-in user, and are not distributed with this
project.
