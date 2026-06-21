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
> not a strict isolated clean-room. The optional *Minish Cap Reborn*-parity
> quality-of-life features are likewise first-party: each is an independent
> implementation written against the decompilation and the port's own
> subsystems, with no Admentus64/The-Minish-Cap-Reborn (GPL-3.0) source
> incorporated — see [`docs/reborn-parity.md`](docs/reborn-parity.md).

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

## Build-time decompilation toolchain (`tools/src/`)

These are **build-time only** tools (used to build the GBA ROM and process
assets); they are **not** part of the shipped `tmc_pc` binary. They originate
from the pret/zeldaret decomp toolchain and each keeps its own license, with the
license file present in-tree alongside the tool.

| Tool | Path | License | License file |
|------|------|---------|--------------|
| agb2mid | `tools/src/agb2mid` | MIT (© YamaArashi) | `tools/src/agb2mid/LICENSE` |
| aif2pcm | `tools/src/aif2pcm` | MIT (© huderlem; © Marco Trillo) | `tools/src/aif2pcm/LICENSE` |
| bin2c | `tools/src/bin2c` | MIT (© YamaArashi) | `tools/src/bin2c/LICENSE` |
| gbagfx | `tools/src/gbagfx` | MIT (© YamaArashi) | `tools/src/gbagfx/LICENSE` |
| mid2agb | `tools/src/mid2agb` | MIT (© YamaArashi) | `tools/src/mid2agb/LICENSE` |
| preproc | `tools/src/preproc` | MIT (© YamaArashi) | `tools/src/preproc/LICENSE` |
| scaninc | `tools/src/scaninc` | MIT (© YamaArashi) | `tools/src/scaninc/LICENSE` |
| **gbafix** | `tools/src/gbafix` | **GPL-3.0** | `tools/src/gbafix/COPYING` |

> `gbafix` is **GPL-3.0**, but it is a standalone build-time executable (it
> stamps the GBA ROM header); it is not linked into or distributed with the PC
> port, so it does not affect the port's licensing.

The project's own tools — `asset_processor`, `assets_extractor`, `tmc_strings`,
`util` — are first-party and covered by the project `LICENSE`.

**Build-time fetched tool dependencies** (downloaded by `tools/CMakeLists.txt`
via CMake FetchContent; not committed to this repo): nlohmann/json (MIT) and
{fmt} (MIT) as above, **CLI11** (BSD-3-Clause,
https://github.com/CLIUtils/CLI11), and cpp-best-practices/project_options
(MIT). These are pulled only when building the CMake tool set.

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
