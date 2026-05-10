# The Legend of Zelda: The Minish Cap — PC Port

A native PC port of *The Legend of Zelda: The Minish Cap* (GBA, 2004) built on
SDL3, a software PPU renderer (`libs/ViruaPPU`), and the agbplay audio engine.
Targets **x86-64 Linux, Windows, and macOS** (Apple Silicon and Intel).

The port is **work in progress** — many rendering and gameplay paths are still
rough; please file issues for anything that breaks.

## Supported ROMs

A copy of the original game is required. This repository does **not** ship the
ROM.

| Version | Filename         | SHA1                                       |
|---------|------------------|--------------------------------------------|
| USA     | `baserom.gba`    | `b4bd50e4131b027c334547b4524e2dbbd4227130` |
| EU      | `baserom_eu.gba` | `cff199b36ff173fb6faf152653d1bccf87c26fb7` |

## Pre-built releases (recommended)

Pre-built tarballs are published on the [Releases page](https://github.com/999sian/tmc/releases).
Each tarball contains:

```
tmc_pc      sounds.json      assets/      assets_src/
```

Setup, once:

1. Download `tmc-usa-{linux,windows,macos}-<version>.tar.gz` and unpack it
   anywhere.
2. Drop your own `baserom.gba` next to the binary.
3. Run the game:

   ```sh
   ./tmc_pc                   # Linux / macOS
   tmc_pc.exe                 # Windows (double-click works)
   ```

The first launch self-extracts a runtime asset cache from the ROM (≈3–5 s,
shows a progress bar). Every subsequent launch is instant. The binary
resolves `baserom.gba`, `sounds.json`, and the asset trees relative to its
own location, so the install directory can live anywhere — no `cd` dance.

## Build from source

Place your ROM in the repository root, then run:

```sh
python3 build.py
```

The script will:

- Check and prompt to install missing dependencies (xmake, SDL3, libpng,
  fmt, nlohmann-json)
- Initialize git submodules automatically
- Scan for ROM files and verify their checksums
- Let you choose USA, EU, or both
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

**macOS (Apple Silicon or Intel):**
```sh
xcode-select --install                                                                            # Apple's compiler + git
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"   # Homebrew, if you don't have it
brew install xmake pkg-config fmt nlohmann-json libpng libomp
```

What each piece does, in plain English:

- **Xcode Command Line Tools** — Apple's C/C++ compiler (`clang`), linker, and
  `git`. You can't build any native code on a Mac without them, and Homebrew
  won't install them for you.
- **Homebrew** — the package manager that fetches everything else. If you've
  never used a Mac for development, this is the one prerequisite you'll
  always end up installing.
- **xmake** — the build system that drives the whole compile (think
  `make`/`cmake`, but it auto-downloads any C/C++ libraries that aren't
  already on your system).
- **pkg-config** — a tiny tool xmake uses to ask "where did Homebrew put
  libpng?". Without it, xmake can't find the brew-installed libraries and
  will rebuild them itself (slow and sometimes broken).
- **fmt** and **nlohmann-json** — small C++ helper libraries the asset tools
  and game code use for text formatting and JSON parsing.
- **libpng** — needed by the graphics tools to read and write PNG sprite
  sheets.
- **libomp** — Apple Clang doesn't ship with an OpenMP runtime, so VirtuaPPU's
  parallel scanline renderer needs Homebrew's `libomp`. If it's missing, the
  build still works but falls back to a single-threaded path.
- **SDL3** — the cross-platform window/input/audio layer. Not in the brew
  list because xmake builds its own copy automatically on macOS (the Linux
  path uses the system one when available, but on macOS the bundled build
  is the default and Just Works).

**Windows:** Install [xmake](https://xmake.io) and [git](https://git-scm.com).
SDL3 and other libraries are downloaded automatically by xmake.

See [INSTALL.md](INSTALL.md) for slim builds, the `xmake build tmc_pc`
single-target invocation, and other build options.

## Tested platforms

* Linux (Wayland preferred, X11 fallback)
* Windows via MinGW static link
* macOS (Apple Silicon and Intel) — builds cleanly with Homebrew + Xcode
  Command Line Tools; lightly tested compared to Linux/Windows.

## Controls

| Action               | Keyboard         | Gamepad        |
|----------------------|------------------|----------------|
| Fast-forward (hold)  | Tab              | Right trigger  |
| Toggle fullscreen    | F11 / Alt+Enter  | —              |
| Cycle upscaler       | F12              | —              |
| Capture bug report   | F9               | —              |
| Open debug menu      | F8               | —              |
| Quicksave            | F5               | —              |
| Quickload            | F6               | —              |

Default upscaler is nearest-neighbor (sharp pixels). F12 cycles through xBRZ
4× and linear modes.

The window title shows the running port version — please include it when
filing issues.

See [CHANGELOG.md](CHANGELOG.md) for per-release notes.

## Nix

A `flake.nix` is provided with all dependencies. Run the port directly with:

```sh
nix run
```

Or enter a development shell:

```sh
nix develop
```

## Contributing

Issues and pull requests are welcome — port improvements, tools, and
documentation. Open an issue describing the bug or proposed change before
larger work so we can coordinate.

# Third-party notice: agbplay

`libs/agbplay_core` contains code derived from:

- Project: agbplay
- Repository: https://github.com/ipatix/agbplay
- Author: ipatix and contributors
- License: GNU Lesser General Public License v3.0

The original agbplay project is licensed under the LGPL-3.0. The copied and
modified files in this directory remain under that license.

The rest of this repository is not automatically relicensed as LGPL-3.0
solely because it links to or uses this LGPL component.
