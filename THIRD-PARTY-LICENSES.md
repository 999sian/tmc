# Third-Party Licenses & Notices

Project Picori's own code is licensed under the **Anti-Capitalist Software
License v1.4** (see [`LICENSE`](LICENSE)). That license applies **only** to the
original work authored by this project's contributors.

This project also **uses, links, bundles, or invokes** the third-party
components listed below. **Each retains its own license**, and those terms — not
the ACSL — govern those components. They are reproduced/redistributed here under
their respective licenses.

> ⚠️ **Copyleft note.** This project links **LGPL-3.0** code (agbplay). agbplay
> is linked into `tmc_pc` in relinkable form, as LGPL permits, while the larger
> work carries its own license. The project does **not** bundle, link, or invoke
> any GPL-3.0 component: the randomizer feature is the project's own independent
> reimplementation under `port/rando/` — a native location graph with data
> derived from the decompilation/ROM, which can optionally import the public
> `.logic` text format but does not bundle, link, or translate the GPL-3.0
> minishmaker/randomizer. It is format-compatible with that project by design,
> not a strict isolated clean-room.

## Copyleft components (GPL / LGPL family)

| Component | Path | Upstream | License | Linkage |
|-----------|------|----------|---------|---------|
| **agbplay** (agbplay_core) | `libs/agbplay_core` | https://github.com/ipatix/agbplay | **LGPL-3.0** | Linked into `tmc_pc` in relinkable form. See `libs/agbplay_core/LICENSE`. |

## Permissively-licensed components

| Component | Upstream | License |
|-----------|----------|---------|
| Dear ImGui | https://github.com/ocornut/imgui | MIT |
| {fmt} | https://github.com/fmtlib/fmt | MIT |
| nlohmann/json | https://github.com/nlohmann/json | MIT |
| GuiLite | https://github.com/idea4good/GuiLite | Apache-2.0 |
| SDL3 | https://github.com/libsdl-org/SDL | Zlib |
| zlib | https://zlib.net | Zlib |
| libpng | http://www.libpng.org/pub/png/libpng.html | libpng (PNG Reference Library) |

## Components without a published license

These submodules currently ship **without a license file** and are therefore
"all rights reserved" by default. Use is by arrangement with their author
(MatheoVignaud). Track upstream for license clarification before redistributing.

| Component | Path | Upstream | License |
|-----------|------|----------|---------|
| ViruaPPU / VirtuaPPU | `libs/ViruaPPU` | https://github.com/MatheoVignaud/VirtuaPPU | none published |
| VirtuaAPU | `libs/VirtuaAPU` | https://github.com/MatheoVignaud/VirtuaAPU | none published |
| tmc-Modern-Launcher | `libs/tmc-Modern-Launcher` | https://github.com/MatheoVignaud/tmc-Modern-Launcher | none published |
| tmc-Android-Experimental | `libs/tmc-Android-Experimental` | https://github.com/MatheoVignaud/tmc-Android-Experimental | none published |

## Upstream decompilation & game IP

This project builds on the **zeldaret/tmc** decompilation of *The Legend of
Zelda: The Minish Cap*. The decompilation reproduces the structure of a
copyrighted, commercially released game. All Nintendo intellectual property —
code, assets, characters, and trademarks — remains owned by **Nintendo**.
Nothing in this repository grants any rights to that intellectual property, and
a legitimately-owned ROM is required to extract assets and run the game.

---

*Generated as part of license review. If you add or remove a dependency, update
this file and `LICENSE` accordingly.*
