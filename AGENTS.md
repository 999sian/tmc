# Repository Guidelines

## Project Overview
Project Picori is a native PC port of **The Legend of Zelda: The Minish Cap**. It builds the zeldaret/tmc decompilation as a C/C++ SDL3 application, replacing GBA hardware with PC shims, VirtuaPPU rendering, and agbplay/VirtuaAPU audio. A SHA1-valid ROM is required for full asset extraction and runtime data.

## Architecture & Data Flow
- Entry flow: `port/port_main.c` initializes SDL3, assets, audio, input, and PPU, then enters `AgbMain()` in `src/main.c`.
- Game loop: input read -> task dispatcher (`TITLE`, `FILE_SELECT`, `GAME`, etc.) -> entity/script/UI/audio updates -> PPU render (`port/ppu`) -> SDL3 present.
- Core state is GBA-style global data: `gMain`, `gScreen`, `gUI`, `gSave`, `gRoomControls`, `gRoomVars`, `gEntityLists`.
- Entity system: fixed pool, max 72 entities, kinds include player, enemies, projectiles, objects, NPCs, player items, and managers. See `include/entity.h`, `src/entity.c`.
- Rendering: GBA VRAM/OAM/palettes -> `port/ppu/src/mode*.c` -> `port/port_ppu.cpp` -> SDL_Renderer or optional SDL_GPU (`port/port_gpu_renderer.cpp`).
- Assets: ROM -> `asset_extractor`/asset pipeline -> `assets/` or `dist/<version>/assets/`; runtime fallback can self-extract in slim builds.
- PPU changes: the software PPU is vendored in-tree at `port/ppu/` — edit it directly and guard with `tools/ppu_parity_check.sh` (byte-exact render parity gate). No submodule, no patches.

## Key Directories
- `src/` — decompiled game logic: tasks, entities, enemies, objects, NPCs, UI, rooms, scripts, sound.
- `include/` — shared GBA engine structs, globals, entity/save/screen/map definitions.
- `port/` — PC port layer: SDL3 entry, renderer, audio, ROM loading, assets, config, ImGui/debug UI, save, crash reports.
- `port/ppu/` — vendored software GBA PPU renderer (GPL-3.0; was the `libs/ViruaPPU` submodule).
- `libs/VirtuaAPU/`, `libs/agbplay_core/` — audio emulation/playback components.
- `tools/` — build-time tools and asset utilities; `tools/CMakeLists.txt` builds many helper binaries.
- `scripts/` — developer/debug helpers such as `scripts/mazaal_gdb.gdb`.
- `docs/` — audits, parity notes, widescreen/audio/perf design documents.
- `mods/` — runtime asset override mods; see `mods/README.md`.
- `.github/workflows/` — CI/release matrix definitions.

## Development Commands
```bash
# Recommended full build; prompts when needed and stages dist/USA or dist/EU
python3 build.py --usa
python3 build.py --eur

# Slim build: binary + embedded extractor fallback, skips full asset staging
python3 build.py --usa --slim

# Direct incremental build
xmake f -y --game_version=USA
xmake build -y tmc_pc

# Useful feature builds
xmake f -y --game_version=USA --gpu_renderer=y
xmake f -y --pc_sanitize=y
xmake f -y --widescreen_width=384

# Asset/tool targets
xmake build -y asset_extractor
xmake extract_assets
xmake convert_assets
xmake build_assets

# Run staged binary
cd dist/USA && ./tmc_pc

# Headless/smoke run pattern
TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./dist/USA/tmc_pc --no-audio
```

## Code Conventions & Common Patterns
- Core engine code is C; port/UI/renderer integration is C++17. Keep the C/C++ boundary explicit with `extern "C"` where needed.
- Preserve GBA behavior. PC-only fixes should be narrowly scoped and usually guarded with `#ifdef PC_PORT` or port-layer indirection.
- Prefer fixed-size pools, stack/static buffers, and existing globals over heap allocation in frame/render/update paths.
- Use existing GBA-style types and packed/layout-sensitive structs. Be careful with bitfields, pointer sizes, alignment, and save/ROM data layouts.
- Positions and velocities commonly use fixed-point/Q formats; avoid replacing them with floats unless the existing subsystem already does.
- Entity lifecycle and lists are centralized in `src/entity.c`; do not bypass pool/list helpers.
- Runtime settings go through `port/port_runtime_config.*` and persist to `config.json`.
- Logging is mostly `fprintf(stderr, "[tag] ...")`; keep frame-spam conditional or throttled.
- Rendering changes must respect pitch, visible width, widescreen toggles, SDL state churn, and zero-allocation frame loops.
- For VirtuaPPU changes, update both the submodule working tree and the corresponding `port/patches/*.patch` entry.

## Important Files
- `src/main.c` — `AgbMain()` and core task loop.
- `port/port_main.c` — native process entry and SDL/bootstrap flow.
- `port/port_ppu.cpp` — PPU presentation, scale/filter/aspect handling.
- `port/port_gpu_renderer.cpp` / `.h` — optional SDL_GPU presentation path.
- `port/ppu/src/mode1.c`, `mode2.c` — primary GBA renderer paths.
- `port/port_runtime_config.cpp` / `.h` — config defaults, input bindings, renderer/upscale/runtime options.
- `port/port_rom.c` — ROM loading, SHA1/region handling, ROM-derived data setup.
- `port/port_asset_loader.cpp` — runtime asset lookup and fallback behavior.
- `port/port_audio.c`, `src/sound.c` — PC audio backend and original sound engine integration.
- `port/port_imgui_menu.cpp`, `port/port_debug_menu.cpp` — F8 menus and debug tooling.
- `xmake.lua` — primary build graph, options, packages, submodule patch rules.
- `build.py` — user-facing build orchestrator.
- `README.md`, `INSTALL.md`, `CHANGELOG.md` — user setup, build notes, release context.

## Runtime/Tooling Preferences
- Primary build system: **xmake 2.7.0+**. Use `build.py` for full user-style builds and direct `xmake build -y tmc_pc` for focused incremental checks.
- Required native deps vary by platform; common deps include `git`, SDL3, libpng, fmt, and nlohmann-json. xmake vendors several packages.
- Python 3 drives `build.py` and helper generators such as `tools/generate_sounds_embed.py`.
- CMake is used for tools (`tools/CMakeLists.txt`), not as the main project build entry.
- Supported staged runtime expects `dist/USA/` or `dist/EU/` with `tmc_pc`, `baserom.gba`, `assets/`, and `sounds.json` unless using slim/self-extract mode.
- Config and saves are runtime-local: `config.json`, `tmc.sav`, optional save profiles.

## Testing & QA
- There is currently **no formal unit-test target**; `xmake test` is not valid for this project.
- Minimum verification for code changes: `xmake build -y tmc_pc` plus a smoke run that reaches `Port layer initialized. Entering AgbMain...`.
- Full packaging verification: `python3 build.py --usa` or `python3 build.py --eur` when ROM/assets/package behavior is affected.
- Useful smoke env: `TMC_AUTOPLAY=1`, `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, and `--no-audio` for headless launch checks.
- Renderer/PPU changes should test the relevant backend and scale path: software SDL_Renderer, SDL_GPU when enabled, nearest/internal-scale/xBRZ as applicable.
- Manual QA tools: F8 debug menu, F9 bug report capture, F5/F6 quicksave/load, F7 TTS toggle, F12 upscaler cycle.
- CI builds a multi-platform matrix via `.github/workflows/_build.yaml`; Linux release checks SDL3 audio backends (`alsa`, `pulseaudio`, `pipewire`).
- `tools/ppu_bench.c` is a manual VirtuaPPU benchmark/parity helper, not a CI test.
