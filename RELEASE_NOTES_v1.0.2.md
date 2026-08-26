# Alek's Ultimate NX Edition v1.0.2

Makes released builds self-diagnosing. No gameplay changes — if v1.0.1 works
for you, nothing about how the game plays is different here.

The point of this release: when you hit a bug, your build should be able to
show what happened instead of leaving you to describe it.

## Added

### Boot log in release builds

Released builds now write two files to your SD card:

- `/switch/tmc/tmc.log` — build identity, asset bootstrap, ROM symbol
  resolution, network bring-up, and any failure along the way.
- `/switch/tmc/startup.log` — per-phase boot timings.

Both were previously compiled out of release builds, which is why players
asked for `tmc.log` and found nothing: on v1.0.1 and earlier it genuinely did
not exist on their card.

This costs essentially nothing. Every per-frame gameplay trace is still
discarded before the game loop starts, exactly as before, so nothing is
written to the SD card while you play. `tmc.log` self-trims at 2 MiB.

### Crash reports, now documented

Release builds have always written crash reports — that was simply never
documented. If the game crashes, look in `/switch/tmc/crashlogs/`, and in
`/atmosphere/crash_reports/` for the system's own report of the same moment.

### Reporting a bug

The README now has a [Reporting a bug](README.md#reporting-a-bug) section
listing exactly which files to attach.

## Should you update?

Yes, but there is no rush. Nothing is broken in v1.0.1. The reason to update
is that any problem you run into afterwards will come with evidence attached,
which is the difference between a fix in hours and a fix in weeks.

## Verification

    SHA-256  e8c71d28170589cde46a32e83e1ded38643b3389ccd802e3d474ed2b2ba5cdd8
             tmc_aleks_ultimate_nx_v1.0.2.nro
    Size     13870771 bytes
