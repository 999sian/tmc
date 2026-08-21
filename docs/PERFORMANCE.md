# Performance notes — v0.1.1

All numbers below come from the maintainer's real Nintendo Switch (handheld,
application mode). Your results may vary with firmware, SD card and scene.

## By display mode

| Mode | Expectation |
|---|---|
| **NORMAL** | 60 FPS with the most headroom; the recommended mode if you want the smoothest experience. |
| **DUAL** | Generally close to 60; the second-screen panel adds real cost. Heavier rooms and the MAP tab are the most demanding combinations. |
| **FLIP 270** | Similar to DUAL (same panel, vertical composition). |

Busy areas (for example Minish Woods) are genuinely heavy and are where drops
below 60 are most likely in DUAL/FLIP.

## The optional 1224 MHz recommendation

DUAL and FLIP are more demanding than NORMAL. On tested hardware, a
**1224 MHz CPU clock** is recommended for performance closer to a consistent
60 FPS in those modes. This is optional and does not guarantee a locked
60 FPS in every scene.

Notes on that recommendation:

- 1224 MHz is within the range the Switch itself uses in some official
  situations; this document does not suggest anything beyond it.
- How you set the clock is up to you and your setup (e.g. a system-level tool
  such as sys-clk). This project does not include or require an overclocking
  tool, and the port does not present CPU control as a user-facing feature.
- The port itself does not change the CPU clock, in this or any earlier
  release. Integrating with external clock managers automatically is still
  being evaluated: the clock managers in common use do not all speak the same
  protocol, and a port that guessed wrong would be fighting the user's own
  configuration for control of their console.
- If you stay on stock clocks, expect occasional dips in heavy DUAL/FLIP
  scenes; NORMAL is unaffected in normal play.

## Startup (Fast Boot)

Startup was substantially reduced during development by removing an early-boot
step that re-extracted the ROM to thousands of small page files on every
launch. For reference, the development baseline before that work was roughly
**33 seconds** to title.

After the Fast Boot improvements, startup was observed at roughly **13–14
seconds** on the maintainer's tested hardware. Actual startup time may vary
depending on SD-card and system conditions.

The very first boot is longer than subsequent ones, because the runtime asset
cache is generated once from your ROM.

## Second-screen cost model (for the curious)

The panel renders to its own 720×800 surface and repaints only when its
content changes; the game view and panel are composited each frame. MAP is
the most expensive tab (live map rendering), ITEMS/QUEST are lighter. This is
why DUAL/FLIP cost more than NORMAL and why the MAP tab is the worst case.
