#!/usr/bin/env python3
"""
Generate a parallel USA patches tree from the EU originals.

This is the build-time step that produces a `Patches_USA/` directory
sibling to `libs/randomizer/RandomizerCore/Resources/Patches/`. It runs
translate_offsets on each .event file, then post-processes the output
so that the ColorzCore assembler will tolerate the unresolved ORGs:

  - Resolved ORG / POIN lines are written with the USA address.
  - Unresolved ORG lines have the *entire line* commented out (//)
    so ColorzCore skips them rather than writing at the wrong offset.
    Lines with multiple statements where any one is unresolved get the
    whole line commented — we sacrifice the resolved writes on that
    line for safety. (After scanning the 33 EU patches, only a small
    minority of lines have multiple ORGs, and the loss is documented
    in the spoiler so the user knows what wasn't shuffled.)

Non-.event files (binary tilesets, palettes, C sources, includes, etc.)
get copied verbatim into the USA tree — they're region-agnostic.

This script is intentionally idempotent and standalone; it can be run
manually for debugging or via the xmake randomizer_cli post-build step.
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

THIS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(THIS_DIR))
from translate_offsets import translate_offset, parse_map  # noqa: E402


# Match an entire statement on a line. Statements are separated by ;
# inside ColorzCore's EventAssembler dialect. We split first by ;,
# then process each substatement individually, then re-join with ;.
ORG_LINE  = re.compile(r"\bORG\s+\$([0-9A-Fa-f]+)")
POIN_LINE = re.compile(r"\bPOIN\s+\$([0-9A-Fa-f]{4,})")


def translate_substatement(sub, lookup):
    """Translate ORG/POIN within a single statement; return (new_sub, all_resolved)."""
    resolved = True

    def repl_org(m):
        nonlocal resolved
        eu_off = int(m.group(1), 16)
        usa_off, src = translate_offset(eu_off, **lookup)
        if usa_off is None:
            resolved = False
            return f"ORG ${eu_off:X}"
        return f"ORG ${usa_off:X}"

    def repl_poin(m):
        nonlocal resolved
        eu_off = int(m.group(1), 16)
        usa_off, src = translate_offset(eu_off, **lookup)
        if usa_off is None:
            resolved = False
            return f"POIN ${eu_off:X}"
        return f"POIN ${usa_off:X}"

    sub = ORG_LINE.sub(repl_org, sub)
    sub = POIN_LINE.sub(repl_poin, sub)
    return sub, resolved


def translate_event_file(in_path, out_path, lookup):
    text = in_path.read_text(encoding="utf-8", errors="replace")
    out_lines = []
    stats = {"resolved_lines": 0, "unresolved_lines": 0,
             "neutral_lines": 0, "total_lines": 0}

    for raw_line in text.splitlines():
        stats["total_lines"] += 1
        # Preserve trailing comments
        line_body, sep, comment = raw_line.partition("//")
        if not any(rx.search(line_body) for rx in (ORG_LINE, POIN_LINE)):
            # No ORG/POIN on this line — keep verbatim
            out_lines.append(raw_line)
            stats["neutral_lines"] += 1
            continue

        # Split into ; - separated substatements, translate each, watch
        # for any unresolved. EventAssembler treats ; as a statement
        # separator inside a line.
        subs = line_body.split(";")
        all_ok = True
        new_subs = []
        for s in subs:
            ns, ok = translate_substatement(s, lookup)
            if not ok:
                all_ok = False
            new_subs.append(ns)
        new_body = ";".join(new_subs)

        if all_ok:
            out_lines.append(new_body + (sep + comment if sep else ""))
            stats["resolved_lines"] += 1
        else:
            # Comment out the whole line so ColorzCore skips it. Tag with
            # // [USA-UNRESOLVED] so audits can grep for these later.
            out_lines.append("// [USA-UNRESOLVED] " + raw_line)
            stats["unresolved_lines"] += 1

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    return stats


def main():
    ap = argparse.ArgumentParser()
    default_src = (THIS_DIR.parent.parent / "libs" / "randomizer"
                   / "RandomizerCore" / "Resources" / "Patches")
    default_dst = default_src.parent / "Patches_USA"
    ap.add_argument("--src", default=str(default_src),
                    help="EU patches root")
    ap.add_argument("--dst", default=str(default_dst),
                    help="USA patches output root")
    ap.add_argument("--eu-rom",  help="EU baserom (optional — byte-pattern probe)")
    ap.add_argument("--usa-rom", help="USA baserom")
    ap.add_argument("--eu-map",  help="EU tmc.map (optional — sym matching)")
    ap.add_argument("--usa-map", help="USA tmc.map")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    lookup = {}
    if args.eu_map and args.usa_map:
        eu_s2a, eu_a2s = parse_map(args.eu_map)
        usa_s2a, _ = parse_map(args.usa_map)
        if eu_s2a and usa_s2a:
            lookup.update(eu_map_sym_to_addr=eu_s2a,
                          eu_map_addr_to_sym=eu_a2s,
                          usa_map_sym_to_addr=usa_s2a)
    if (args.eu_rom and args.usa_rom
            and Path(args.eu_rom).is_file() and Path(args.usa_rom).is_file()):
        lookup["eu_rom"]  = Path(args.eu_rom).read_bytes()
        lookup["usa_rom"] = Path(args.usa_rom).read_bytes()

    src = Path(args.src)
    dst = Path(args.dst)
    if not src.is_dir():
        sys.exit(f"source not found: {src}")

    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)

    totals = {"resolved_lines": 0, "unresolved_lines": 0,
              "neutral_lines": 0, "total_lines": 0,
              "event_files": 0, "copied_files": 0}

    for path in src.rglob("*"):
        rel = path.relative_to(src)
        out_path = dst / rel
        if path.is_dir():
            out_path.mkdir(parents=True, exist_ok=True)
            continue
        if path.suffix == ".event":
            stats = translate_event_file(path, out_path, lookup)
            totals["event_files"] += 1
            for k in ("resolved_lines", "unresolved_lines",
                      "neutral_lines", "total_lines"):
                totals[k] += stats[k]
            if not args.quiet:
                print(f"  {rel}: {stats['resolved_lines']} ok, "
                      f"{stats['unresolved_lines']} skipped, "
                      f"{stats['neutral_lines']} neutral")
        else:
            # Binary asset, C source, .bat, .png — copy verbatim
            out_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, out_path)
            totals["copied_files"] += 1

    print("\n=== USA patches generation summary ===")
    print(f"  event files translated: {totals['event_files']}")
    print(f"  non-event files copied: {totals['copied_files']}")
    print(f"  resolved lines:         {totals['resolved_lines']}")
    print(f"  unresolved (skipped):   {totals['unresolved_lines']}")
    print(f"  neutral (no ORG):       {totals['neutral_lines']}")
    print(f"  output:                 {dst}")


if __name__ == "__main__":
    main()
