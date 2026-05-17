#!/usr/bin/env python3
"""
Batch scanner: walks the randomizer's Resources/Patches tree, classifies
every `ORG $hex` directive by translation status, and emits a coverage
report so we can prioritize the audit.

Usage:
    python3 tools/randomizer_usa/scan_patches.py
    python3 tools/randomizer_usa/scan_patches.py --csv out/coverage.csv

Produces a per-patch summary like:

    libs/.../ROM Buildfile.event           224 ORGs    13 known    87 interp-tight    124 unresolved
    libs/.../improvements/installer.event  149 ORGs     0 known    32 interp-tight    117 unresolved
    ...

…and a global summary at the end. Counts let us spot patches that are
"close to resolvable" (mostly known + interp-tight) versus those that
need an EU ROM or EU decomp map to make any progress.
"""

import argparse
import csv
import os
import re
import sys
from pathlib import Path

# Import translate_offset from the sibling translator.
THIS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(THIS_DIR))
from translate_offsets import (                                # noqa: E402
    KNOWN_PAIRS,
    translate_offset,
    parse_map,
)


DEFAULT_PATCH_ROOT = (
    Path(__file__).resolve().parent.parent.parent
    / "libs" / "randomizer" / "RandomizerCore" / "Resources" / "Patches"
)


ORG_RE  = re.compile(r"\bORG\s+\$([0-9A-Fa-f]+)")
POIN_RE = re.compile(r"\bPOIN\s+\$([0-9A-Fa-f]{4,})")


def scan_file(path, **lookup_kwargs):
    """Return dict per file: total ORGs+POINs, unique addresses, per-class counts."""
    text = path.read_text(encoding="utf-8", errors="replace")
    orgs = [int(m, 16) for m in ORG_RE.findall(text)]
    poins = [int(m, 16) for m in POIN_RE.findall(text)]
    unique = sorted(set(orgs) | set(poins))
    classes = {}
    for off in unique:
        _, tag = translate_offset(off, **lookup_kwargs)
        bucket = tag.split(":", 1)[0]
        classes[bucket] = classes.get(bucket, 0) + 1
    return {
        "path": str(path),
        "total_orgs": len(orgs),
        "total_poins": len(poins),
        "unique_orgs": len(unique),
        "classes": classes,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--patches", default=str(DEFAULT_PATCH_ROOT),
                    help="root of Resources/Patches")
    ap.add_argument("--csv",  help="write per-file CSV report here")
    ap.add_argument("--eu-rom",  help="EU baserom (enables byte-pattern probe)")
    ap.add_argument("--usa-rom", help="USA baserom")
    ap.add_argument("--eu-map",  help="EU tmc.map (enables symbol matching)")
    ap.add_argument("--usa-map", help="USA tmc.map")
    args = ap.parse_args()

    lookup = {}
    if args.eu_map and args.usa_map:
        eu_s2a, eu_a2s = parse_map(args.eu_map)
        usa_s2a, _ = parse_map(args.usa_map)
        if eu_s2a and usa_s2a:
            lookup.update(
                eu_map_sym_to_addr=eu_s2a,
                eu_map_addr_to_sym=eu_a2s,
                usa_map_sym_to_addr=usa_s2a,
            )
            print(f"loaded EU map ({len(eu_s2a)} syms), USA map ({len(usa_s2a)} syms)",
                  file=sys.stderr)
    if args.eu_rom and args.usa_rom \
            and Path(args.eu_rom).is_file() and Path(args.usa_rom).is_file():
        lookup["eu_rom"]  = Path(args.eu_rom).read_bytes()
        lookup["usa_rom"] = Path(args.usa_rom).read_bytes()
        print(f"loaded EU ROM ({len(lookup['eu_rom'])} B), USA ROM ({len(lookup['usa_rom'])} B)",
              file=sys.stderr)

    patch_root = Path(args.patches)
    if not patch_root.is_dir():
        sys.exit(f"patches root not found: {patch_root}")

    rows = []
    for path in sorted(patch_root.rglob("*.event")):
        rows.append(scan_file(path, **lookup))

    # Per-file table — display path relative to patches root so it's readable
    print(f"\n=== Coverage report ({len(rows)} .event files) ===\n")
    header_buckets = ["known", "symmap", "pattern", "interp", "unresolved"]
    print(f"{'patch':55s} {'orgs':>5s} {'uniq':>5s}   " +
          "  ".join(f"{b:>10s}" for b in header_buckets))
    totals = {b: 0 for b in header_buckets}
    total_orgs = 0
    total_unique = 0
    for r in rows:
        rel = Path(r['path']).relative_to(patch_root) if Path(r['path']).is_relative_to(patch_root) else Path(r['path']).name
        line = f"{str(rel):55s} {r['total_orgs']:5d} {r['unique_orgs']:5d}   "
        for b in header_buckets:
            n = r['classes'].get(b, 0)
            totals[b] += n
            line += f"  {n:10d}"
        total_orgs += r['total_orgs']
        total_unique += r['unique_orgs']
        print(line)

    print("\n--- totals ---")
    print(f"  files:        {len(rows)}")
    print(f"  total ORGs:   {total_orgs}")
    print(f"  unique ORGs:  {total_unique}")
    for b in header_buckets:
        print(f"  {b:>11s}:  {totals[b]}")

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["path", "total_orgs", "unique_orgs"] + header_buckets)
            for r in rows:
                writer.writerow(
                    [r["path"], r["total_orgs"], r["unique_orgs"]] +
                    [r["classes"].get(b, 0) for b in header_buckets]
                )
        print(f"\nwrote {args.csv}")


if __name__ == "__main__":
    main()
