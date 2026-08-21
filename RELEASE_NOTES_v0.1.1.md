# Alek's Ultimate NX Edition v0.1.1

v0.1.1 is primarily a first-run installation and stability hotfix. Everything
from v0.1.0 is preserved — this release changes how the port installs itself,
not how it plays.

## Highlights

- Fixed clean-install first boot on real Nintendo Switch hardware.
- Runtime assets can now be generated directly from your own compatible ROM.
- Fixed Switch `sdmc:/` build-state path handling that caused a successful
  extraction to be marked as failed.
- Fixed repeated asset extraction caused by the missing cache state marker.
- Improved first-run extraction performance by no longer writing editable
  development output that the console never reads.
- Improved extraction stability and filesystem durability.
- Moved the contextual Ezlo/hat shortcut from **L** to **Left Stick click
  (LSTICK)**, so L stays free for its normal GBA and menu duties.
- Preserved all existing v0.1.0 gameplay, Dual Screen, FLIP 270, touchscreen
  and RetroAchievements features.

## Installation

Copy the release to `/switch/tmc/`, provide your own legally obtained
compatible USA ROM, and launch in application mode.

No game content is included with this project, and none is downloaded. The
`.zip` asset contains a ready-made `switch/` folder plus a plain-text install
guide if you would rather not lay the files out by hand.

### ROM filename

The filename does not matter — the port scans `/switch/tmc/` for any `.gba`
file and recognises The Minish Cap from its internal header. Naming it
`baserom.gba` is **recommended, not mandatory**: that exact name is always
picked first. If several Minish Cap ROMs are present under non-standard names,
which one wins is not guaranteed, so keep just the one you want to play.

The **USA** version is the tested and recommended base.

## First boot

Runtime assets are generated locally from your ROM, with a progress bar on
screen. This may take around a minute or longer depending on SD-card
performance. Let it finish.

## Subsequent boots

The generated assets are cached and reused, so later launches reach the title
screen much faster.

## Performance

DUAL and FLIP modes are more demanding than NORMAL. Users with supported
external clock-management setups may find 1224 MHz CPU improves consistency.
Automatic clock-management integration is still being evaluated, and this
release does not touch the CPU clock.

## Not in this release

- Automatic CPU clock management.
- The update checker — deferred to v0.2.0. Nothing is downloaded, installed or
  replaced by this release.

## Verifying your download

`SHA256SUMS.txt` lists the checksums of both the `.nro` and the `.zip`
published with this release.

---

Real Nintendo Switch hardware is the primary tested platform.

This project is not affiliated with Nintendo. The Legend of Zelda and The
Minish Cap are trademarks of Nintendo.
