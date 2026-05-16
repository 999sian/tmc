# Randomizer integration

`libs/randomizer` is a submodule pin of [minishmaker/randomizer](https://github.com/minishmaker/randomizer),
the C# tool that produces randomized The Minish Cap ROMs from a clean
EU input + a seed + a settings file.

## License — important

The randomizer is **GPL-3.0**. tmc_pc must therefore treat it as a
**separate program** (own process, own binary) and only call it via
the existing shell-out hook (`port_randomizer.cpp` → `system()` →
`MinishCapRandomizerCLI`). **Do not** statically link, dynamically
link, or in-process-embed the randomizer into tmc_pc — doing so would
propagate GPL-3.0 to every part of the tmc_pc build.

This rules out the "Phase C — AOT embed" idea from the original plan
unless tmc_pc itself is relicensed under a GPL-3.0-compatible licence.

## Why a submodule and not a runtime download

Pinning upstream by commit gives:

- A reproducible randomizer version per tmc_pc commit (no surprise
  upstream changes mid-rolling-release).
- No HTTP downloads at first run; tmc_pc works offline.
- The source is in our tree for patching (e.g. USA-region support, if
  we want to add it).

The flip side: **building** the CLI still needs the .NET 8 SDK at
release time. tmc_pc itself doesn't need .NET — only the build step
that produces `MinishCapRandomizerCLI` does. End users just need the
prebuilt binary placed at `<exe>/randomizer/MinishCapRandomizerCLI[.exe]`.

## Building the CLI from the submodule

```sh
# One-time .NET SDK install:
#   Linux:   curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0
#   macOS:   brew install --cask dotnet-sdk
#   Windows: winget install Microsoft.DotNet.SDK.8
#
# Then from this repo's root:
cd libs/randomizer/MinishCapRandomizerCLI
dotnet publish -c Release -r linux-x64 --self-contained \
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true \
    -o ../../../build/pc/randomizer
```

`-r linux-x64` → use `win-x64`, `osx-x64`, or `osx-arm64` for the other
platforms. The output ends up at
`build/pc/randomizer/MinishCapRandomizerCLI[.exe]`, which is where
`Port_Randomizer_FindCLI()` already looks (search order #2). No
additional configuration needed.

## Using it in tmc_pc

After the CLI is built and dropped at `<exe>/randomizer/`:

1. Launch tmc_pc, press **F8** → **Randomizer** → **Detect CLI**. A
   toast confirms the resolved path.
2. Place a clean EU Minish Cap ROM at `./baserom_eu.gba`, or set
   `TMC_RANDOMIZER_INPUT_ROM=/path/to/eu.gba`.
3. **F8 → Randomizer → Roll new seed**. A randomized ROM lands at
   `./baserom_rando.gba` (override via `TMC_RANDOMIZER_OUTPUT_ROM`),
   spoiler log at `./baserom_rando_spoiler.txt`.
4. Manually copy `baserom_rando.gba` over `baserom.gba` and restart
   tmc_pc. (Manual to avoid clobbering a USA `baserom.gba` if you
   keep one for non-randomized play.)
5. **tmc_pc must be built with `xmake f --game_version=EU`** for the
   rolled output to load correctly — the rando produces EU-region
   ROMs.

## Pin & update procedure

```sh
# Bump the pinned version after upstream cuts a new release
cd libs/randomizer
git fetch
git checkout v1.0.0-rc2.X     # or new tag
cd ../..
git add libs/randomizer
git commit -m "deps: bump randomizer to v1.0.0-rc2.X"
```

Currently pinned: **v1.0.0-rc2.1** (commit `ac14b4e`).
