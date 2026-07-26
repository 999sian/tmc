# Minish Cap — Dual Screen

A dual-screen mod of *The Legend of Zelda: The Minish Cap* for the AYN Thor.
The handheld's bottom panel becomes a live map, quest status and touch
inventory, so the top screen can be nothing but the game.

The dual-screen mod was made with the help of Claude Code and opencode.

![](https://github.com/samyost1/tmc-android/releases/download/v1.0/showcase.png)

Everything on the panel is decoded from your own ROM as the game runs — the
map artwork, the menu chrome, the item icons, the font. No game data is stored
in this repository.

## What's on the panel

- **Map** — Hyrule with a follow cam. ZOOM steps out to the whole kingdom;
  tap a region to open the game's own enlarged map of it. Windcrest warps
  show as pins.
- **Dungeon** — the real automap: floor plaques, explored rooms, Link's
  position, and the small-key count beside your rupees.
- **Quest** — the pause menu's quest screen, reflowed for a square panel. Tap
  the kinstone bag or the technique scroll to open those lists.
- **Items** — tap a ring to arm it, then tap an item to equip that slot.
- **Settings** — hide the top HUD (the panel takes over your vitals), true
  widescreen, and the port-wide toggles the desktop build puts behind F8.

## Install

1. Download the APK from the [Releases page](https://github.com/samyost1/tmc-android/releases)
   and install it. It is debug-signed, so Android will warn about an unknown
   developer.
2. Put your own `baserom.gba` (USA, SHA-1 `b4bd50e4131b027c334547b4524e2dbbd4227130`)
   in `Android/data/dev.picori.tmc/files/` on internal storage.
3. Launch. The panel appears on the second display automatically.

Built for the Thor, but it is a plain Android `Presentation` — any device with
a second display will do.

## Build from source

Needs the Android SDK and NDK r26. Build the native library for both ABIs,
then package:

```sh
for abi in arm64-v8a x86_64; do
  xmake f -y -p android -a $abi --ndk=$ANDROID_NDK_HOME \
      --game_version=USA --gpu_renderer=y --widescreen_width=384
  xmake -y
done
cd android && ./gradlew assembleRelease
```

`--widescreen_width=384` is what compiles the wide render paths in; without it
the WIDESCREEN row is hidden because the setting would have nothing to switch.
The APK lands in `android/app/build/outputs/apk/release/`.

For the desktop port, see [`INSTALL.md`](INSTALL.md).

## Built on

- [Project Picori](https://github.com/999sian/tmc) — the native Minish Cap
  port this mod extends (SDL3, software PPU, agbplay audio).
- [Raekwon1603/tmc-android](https://github.com/Raekwon1603/tmc-android) — the
  Android packaging and second-screen scaffold this forked from.
- [zeldaret/tmc](https://github.com/zeldaret/tmc) — the decompilation
  underneath all of it.

## License

GPL-3.0 — see [`LICENSE`](LICENSE). Bundled and linked third-party components
keep their own GPL-compatible licenses, listed in
[`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md); notably agbplay is
LGPL-3.0 and is not relicensed by being linked here.

This builds on a decompilation of a copyrighted game. All Nintendo
intellectual property remains Nintendo's, and a legitimately-owned ROM is
required to play.
