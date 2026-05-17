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

### Recommended: let xmake do it

The `tmc_pc` target now depends on `randomizer_cli`, which calls
`dotnet publish` automatically and drops the result at
`build/pc/randomizer/MinishCapRandomizerCLI[.exe]` — exactly where
`Port_Randomizer_FindCLI()` looks (search slot #2).

A normal `xmake build -y tmc_pc` produces both binaries. If .NET 8
SDK is missing, the `randomizer_cli` step prints a yellow warning and
the rest of the build continues — only F8 → Randomizer is degraded
until you install .NET and rerun.

### One-time .NET 8 SDK install

```sh
# Linux:
curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0

# macOS:
brew install --cask dotnet-sdk

# Windows:
winget install Microsoft.DotNet.SDK.8

# Verify:
dotnet --list-sdks   # should show 8.0.xxx
```

### Manual rebuild of just the CLI

```sh
xmake build randomizer_cli
```

Or invoke `dotnet publish` directly with the same args xmake uses:

```sh
dotnet publish libs/randomizer/MinishCapRandomizerCLI/MinishCapRandomizerCLI.csproj \
    -c Release -r linux-x64 --self-contained \
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true \
    -o build/pc/randomizer
```

`-r linux-x64` → swap for `win-x64`, `osx-x64`, or `osx-arm64` to
cross-build for other platforms. The csproj's
`<SelfContained>true</SelfContained>` means the resulting binary
embeds the .NET 8 runtime — **end users do not need .NET installed**.

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

### Why USA `baserom.gba` is rejected today

Feeding a USA ROM to the CLI fails at:

```
Randomization failed! Error: ROM does not match the expected CRC for the logic file!
```

The default logic file
(`libs/randomizer/RandomizerCore/Resources/default.logic:6`) declares
`!crc - 0xE8637292`, which is the **EU** ROM CRC32. USA ROM CRC32 is
`0xABCEBBB1` — the check correctly refuses to apply EU-targeted
patches to a USA ROM that they'd corrupt.

This is the *content* blocker the `tools/randomizer_usa/` Phase 1 work
is solving — once the patch set is USA-aware, a parallel USA logic
file with `!crc - 0xABCEBBB1` becomes safe to write. Until then, F8 →
Randomizer requires an EU ROM as input.

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
