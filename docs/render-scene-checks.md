# Rendering scene checks

`tools/render_scene_check.py` adds named, save-independent game scenes alongside
`tools/ppu_parity_check.sh`. Keep using the latter for byte-exact PPU parity;
presentation improvements have their own explicitly approved pixel baselines.
Screenshots and PPU memory containing game assets stay local, outside version control.

Build with the repro harness (`xmake f -y --repro_harness=y`, then
`xmake build -y tmc_pc`). Python 3 and Pillow are required. A valid local ROM is
required; `--binary` and `--rom` override the repository defaults.

```sh
# First establish and visually review a baseline for each profile.
python3 tools/render_scene_check.py --profile original --update
python3 tools/render_scene_check.py --profile widescreen --update
python3 tools/render_scene_check.py --profile hd --update

# Subsequent runs fail on pixel differences or a missing baseline.
python3 tools/render_scene_check.py --profile hd
python3 tools/render_scene_check.py --profile widescreen --scene inventory

# Script safety checks run without a game or ROM.
python3 tools/tests/render_scene_check.py
```

`original` captures the unenhanced base framebuffer at original width.
`widescreen` captures the composed presentation with menu backgrounds enabled.
`hd` adds optional HD dialogue to the widescreen presentation. Each profile has
separate hash keys in `tools/render_scene_hashes.json`; keep deliberate changes
out of the PPU golden file. Presentation hashes are intended for the same build,
backend and display environment, since SDL output and font rasterization may
vary across platforms. Use `--baseline <path>` for a separate platform/backend.

The default is the headless SDL software renderer. `--video-driver x11` or
`wayland` permits a real display. `--backend gpu` selects SDL_GPU with separate baseline keys and artifact folders.
A build with SDL_GPU enabled also needs a working GPU and display: the dummy video driver does not provide one. Missing presentation
capture support fails the run instead of silently falling back to base pixels.

The committed presentation hashes were captured with the USA ROM, software
renderer, a build using the original 240-pixel PPU width, and a 960×540 window
for enhanced profiles. GPU, wider PPU builds, other regions and other platforms
need their own reviewed baselines. On a tiling desktop, use an isolated X11
display for GPU comparisons: the compositor can resize Wayland capture windows,
changing both presentation dimensions and the visible game width.

The tab-separated `tools/render_scenes.tsv` corpus includes file select (two
frames), Hyrule Field, the inventory menu, and a sword item message at both the
top and bottom of the screen. Gameplay scenes bootstrap a fresh transient save
and warp using the existing room capture harness. Dialogue indices currently
cover USA and EU; Japanese dialogue requires separately reviewed indices.
`--region` selects manifest rows and baseline keys and must match the supplied
ROM header. The game also performs its normal ROM validation.

Each scene runs with a clean temporary working directory, generated config and
save location; inherited `TMC_*` and `SDL_*` switches are removed. Settings pin
window scale, filtering, colour correction, persistence and transient UI.
`--timeout` bounds each process (90 seconds by default). Nonzero game exits,
timeouts, invalid/missing/all-black PNGs and absent baseline entries all fail. `--update`
changes the hash file only if every selected capture succeeds and the game
binary stays unchanged during the run. Finish building before capturing.

Artifacts live under `build/render-scenes/<region>/<profile>/`: PNGs, game logs,
and `actual_hashes.json`. An explicit update also retains local `baseline_*.png`
files; a later mismatch writes `diff_*.png` when those reference images are
available. Inspect the actual and baseline images together: the diff image alone
does not explain whether a change is desired. Never approve an update simply
because the script ran successfully.

New harness switches:

- `TMC_ROOMCAP_PRESENTATION=1` requests composed output, polling the capture
  queue until the frame is saved; the default retains base framebuffer capture.
- `TMC_ROOMCAP_MENU=1` opens the existing inventory screen halfway through the
  warp settling period. Use independently of `TMC_ROOMCAP_MSG`.
