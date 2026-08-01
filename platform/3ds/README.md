# The Minish Cap 3DS Platform

This target builds the native dual-screen Nintendo 3DS frontend.

## Console Installation

Install the CIA, then create this directory on the SD card:

```text
sdmc:/3ds/The Minish Cap 3DS/
```

Place a legally obtained clean USA ROM there. Any `.gba` filename is accepted,
though short names are recommended. The expected SHA-1 is:

```text
b4bd50e4131b027c334547b4524e2dbbd4227130
```

The ROM is read locally from the SD card and is never copied into the CIA.

Audio requires a working 3DS DSP firmware setup. On Luma3DS, use Rosalina's
`Dump DSP firmware` option if homebrew audio is unavailable.

## Display

- Top screen: fullscreen 360x240 gameplay with nearest-neighbor scaling.
- Bottom screen: 320x240 map, dungeon/status information and touch item UI.
- Rendering: PICA200/Citro2D presenter fed by the software GBA PPU.
- New 3DS: requests 804 MHz, L2 cache and access to the extra application core.

The CIA metadata uses a stable title ID and requests SD card access for local
ROM and save data.

## Requirements

- devkitPro with devkitARM, libctru, Citro2D and Citro3D
- CMake with the devkitPro Nintendo 3DS toolchain
- `makerom` and `bannertool` for CIA packaging

Run:

```sh
chmod +x platform/3ds/build.sh
platform/3ds/build.sh
```

The script creates both public packages under `build-3ds/game/`. No ROM,
extracted asset package or save data is included in either package.
