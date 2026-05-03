# The Legend of Zelda: The Minish Cap

[![Decompilation Progress][progress-badge]][progress] [![Contributors][contributors-badge]][contributors] [![Discord Channel][discord-badge]][discord]

[progress]: https://zelda64.dev/games/tmc
[progress-badge]: https://img.shields.io/endpoint?url=https://zelda64.dev/assets/csv/progress-tmc-shield.json

[contributors]: https://github.com/zeldaret/tmc/graphs/contributors
[contributors-badge]: https://img.shields.io/github/contributors/zeldaret/tmc

[discord]: https://discord.zelda64.dev
[discord-badge]: https://img.shields.io/discord/688807550715560050?color=%237289DA&logo=discord&logoColor=%23FFFFFF

A decompilation of The Legend of Zelda: The Minish Cap (GBA, 2004) — and a work-in-progress native PC port built on top of it.

The decompilation reconstructs the original C source from the GBA ROM using static and dynamic analysis.
The PC port compiles that source for x86-64 Linux and Windows, replacing GBA hardware with SDL3, a software PPU renderer, and the agbplay audio engine.

## Supported ROMs

A copy of the original game is required to build either the ROM or the PC port.

| Version  | Filename         | SHA1                                       |
|----------|------------------|--------------------------------------------|
| USA      | `baserom.gba`    | `b4bd50e4131b027c334547b4524e2dbbd4227130` |
| EU       | `baserom_eu.gba` | `cff199b36ff173fb6faf152653d1bccf87c26fb7` |
| JP       | `baserom_jp.gba` | `6c5404a1effb17f481f352181d0f1c61a2765c5d` |
| USA Demo | `baserom_demo_usa.gba` | `63fcad218f9047b6a9edbb68c98bd0dec322d7a1` |
| JP Demo  | `baserom_demo_jp.gba`  | `9cdb56fa79bba13158b81925c1f3641251326412` |

The PC port currently supports **USA** and **EU**.

## PC Port — Pre-built releases (recommended)

Pre-built tarballs are published on the [Releases page](https://github.com/MatheoVignaud/tmc/releases). They contain just two binaries plus the audio metadata file:

```
asset_extractor      sounds.json      tmc_pc
```

Setup, once:

1. Download `tmc-usa-{linux,windows}-<version>.tar.gz` and unpack it anywhere.
2. Drop your own `baserom.gba` next to the binaries (this repo does **not** ship the ROM).
3. Run the extractor once. It writes `assets/` and `assets_src/` next to itself:

   ```sh
   ./asset_extractor          # Linux
   asset_extractor.exe        # Windows
   ```

4. Run the game:

   ```sh
   ./tmc_pc                   # Linux
   tmc_pc.exe                 # Windows (double-click works)
   ```

The binaries resolve `baserom.gba`, `sounds.json`, and the extracted asset
trees relative to their own location, so the install directory can be
anywhere — no `cd` dance required.

## PC Port — Build from source

Place your ROM in the repository root, then run:

```sh
python3 build.py
```

The script will:
- Check and prompt to install missing dependencies (xmake, SDL3, libpng, fmt, nlohmann-json)
- Initialize git submodules automatically
- Scan for ROM files and verify their checksums
- Let you choose USA, EU, or both
- Extract and convert assets from your ROM
- Compile the native binary for your platform
- Place everything under `dist/<VERSION>/`

Run the result:

```sh
cd dist/USA
./tmc_pc
```

Saves are written to `tmc.sav` in the working directory.

### Dependencies

**Linux (Arch / CachyOS):**
```sh
sudo pacman -S xmake sdl3 libpng fmt nlohmann-json git curl
```

**Linux (Ubuntu / Debian):**
```sh
sudo apt install xmake libsdl3-dev libpng-dev libfmt-dev nlohmann-json3-dev git curl
```

**Windows:** Install [xmake](https://xmake.io) and [git](https://git-scm.com). SDL3 and other libraries are downloaded automatically by xmake.

## PC port (work in progress)

This fork includes an experimental PC build target (`tmc_pc`) that runs the
decompiled game natively via SDL3 + a software PPU (`libs/ViruaPPU`). The port
is **WIP** — many rendering and gameplay paths are still rough.

Tested platforms:

* Linux (Wayland preferred, X11 fallback)
* Windows via MinGW static link

macOS may build (the xmake config sets up the toolchain) but is not regularly
tested.

Build with `xmake build tmc_pc`; the binary lands in `build/pc/`. As of
0.1.1 the binary resolves `baserom.gba`, `sounds.json`, and the asset
trees relative to its own path (and falls back to cwd in dev), so it
works whether you `cd build/pc && ./tmc_pc` or invoke it from elsewhere.

### What's fixed and what's still broken

See [CHANGELOG.md](CHANGELOG.md) for the per-release notes. **0.1.4-experimental** clears the Hyrule Town + South Hyrule Field playthrough — kinstone-bag interaction (5-bug crash chain), spear moblin loading-zone crash (read-only ROM hitbox + packed-pointer-table), and the peahat "corpse never despawns" gust-jar-state bug all closed. CI now builds on `ubuntu-22.04` so the Linux tarball runs on every distro from glibc 2.35 onwards. Known-still-open issues at 0.1.4: door-priority glitches, Item-Get BGM ducking, mushroom held-pose extraction, blue/red teleport icons, mosaic effect, festival house facades — all listed at the bottom of the changelog entry.

### Controls

| Action               | Keyboard            | Gamepad                |
|----------------------|---------------------|------------------------|
| Fast-forward (hold)  | Tab                 | Right trigger          |
| Toggle fullscreen    | F11 / Alt+Enter     | —                      |
| Cycle upscaler       | F12                 | —                      |

Default upscaler is nearest-neighbor (sharp pixels). F12 cycles through
xBRZ 4× and linear modes.

### Nix

A `flake.nix` is provided with all dependencies. Run the port directly with:

```sh
nix run
```

Or enter a development shell:

```sh
nix develop
```

## ROM Build (GBA)

To rebuild the original GBA ROM you also need the `arm-none-eabi` toolchain and [agbcc](https://github.com/pret/agbcc). See [INSTALL.md](INSTALL.md) for full instructions.

```sh
xmake rom
```

## Contributing

All contributions are welcome — decompilation, port improvements, tools, and documentation.

Most discussions happen on our [Discord Server](https://discord.zelda64.dev), where you are welcome to ask if you need help getting started, or if you have any questions regarding this project and other decompilation projects.



# Third-party notice: agbplay

`libs/agbplay_core` contains code derived from:

- Project: agbplay
- Repository: https://github.com/ipatix/agbplay
- Author: ipatix and contributors
- License: GNU Lesser General Public License v3.0

The original agbplay project is licensed under the LGPL-3.0. The copied and
modified files in this directory remain under that license.

The rest of this repository is not automatically relicensed as LGPL-3.0 solely
because it links to or uses this LGPL component.
