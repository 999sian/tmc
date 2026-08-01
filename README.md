# The Minish Cap 3DS

![The Minish Cap 3DS splash screen](platform/3ds/assets/splash.png)

A native dual-screen Nintendo 3DS port of *The Legend of Zelda: The Minish
Cap*, based on the open-source Project Picori engine and samyost1's dual-screen
Android work.

This repository contains no ROM and no extracted game assets. You must provide
your own legally obtained compatible Game Boy Advance ROM.

## Current Features

- Native CIA and Homebrew Launcher 3DSX builds.
- 360x240 nearest-neighbor gameplay on the top screen.
- Live map, dungeon information, quest status, and touch item UI on the bottom
  screen.
- D-pad and Circle Pad movement.
- A, B, L, R, Start, and Select controls matching the original GBA layout.
- PICA200/Citro2D presentation for both screens.
- Parallel software PPU rendering across the available application cores.
- New Nintendo 3DS speedup and third-core rendering support.
- Native NDSP stereo audio.
- Local save data stored beside the ROM on the SD card.

The v0.1.0 series is an early pre-release. New Nintendo 3DS is the primary
60 FPS performance target; measured Old Nintendo 3DS limitations will be
documented in the release notes.

## Installation

1. Install `tmc-3ds-v0.1.0.cia` with FBI, or copy the 3DSX build to the
   Homebrew Launcher.
2. Create this directory on the SD card:

```text
sdmc:/3ds/The Minish Cap 3DS/
```

3. Place your clean USA ROM there as:

```text
sdmc:/3ds/The Minish Cap 3DS/baserom.gba
```

The expected ROM SHA-1 is:

```text
b4bd50e4131b027c334547b4524e2dbbd4227130
```

The ROM stays on your SD card and is never included in the CIA.

Audio requires a working 3DS DSP firmware setup. On Luma3DS, use Rosalina's
`Dump DSP firmware` option if homebrew audio is unavailable.

## Controls

| Nintendo 3DS | Game Boy Advance |
| --- | --- |
| D-pad / Circle Pad | D-pad |
| A | A |
| B | B |
| L | L |
| R | R |
| Start | Start |
| Select | Select |
| Touch screen | Bottom-screen UI |

## Building

Requirements:

- devkitPro with devkitARM, libctru, Citro2D, and Citro3D
- CMake and the devkitPro Nintendo 3DS toolchain
- `makerom` and `bannertool` for CIA packaging

Build:

```sh
chmod +x platform/3ds/build.sh
./platform/3ds/build.sh
```

Outputs are written to:

```text
build-3ds/game/tmc-3ds-v0.1.0.cia
build-3ds/game/tmc-3ds-v0.1.0.3dsx
```

The build does not embed `baserom.gba`.

## Credits

- [samyost1/tmc-android](https://github.com/samyost1/tmc-android) - dual-screen
  Android project used as the direct porting base.
- [Project Picori](https://github.com/999sian/tmc) - native Minish Cap engine,
  software PPU, and port infrastructure.
- [Raekwon1603/tmc-android](https://github.com/Raekwon1603/tmc-android) - Android
  packaging and platform work behind the dual-screen fork.
- [zeldaret/tmc](https://github.com/zeldaret/tmc) - original decompilation.
- Esteban PDN - Nintendo 3DS port and release maintenance.

## License And Legal Notice

Source code is distributed under GPL-3.0; see [LICENSE](LICENSE). Third-party
components retain their respective compatible licenses as listed in
[THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

Nintendo owns *The Legend of Zelda*, *The Minish Cap*, and all associated game
content. This is an unofficial fan-made port. No Nintendo ROM, extracted game
asset package, save data, or firmware is distributed by this project.
