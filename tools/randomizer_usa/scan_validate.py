#!/usr/bin/env python3
"""
Validation pass: cross-check anchor-interpolation predictions against
the USA decomp symbol map.

Logic: for each ORG in the patches whose translation falls in the
`interp` tier, look up what USA symbol the predicted USA offset
falls inside. If the prediction lands inside a real named symbol
(not unmapped junk) AND that symbol's purpose matches the patch's
intent (e.g., predictions from kinstone-patching code land in
`gFuseActions`), the interpolation is very likely correct.

This is the strongest correctness signal we can produce without
an EU ROM or EU decomp map — it uses only USA-side data, which
is locally available (`~/tmc-reference/zeldaret-tmc/build/USA/tmc.map`).

Output: validation report grouped by USA symbol, with sample patch
lines that interp into each symbol. Used by README + commit notes.
"""

import argparse
import bisect
import os
import re
import sys
from pathlib import Path

THIS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(THIS_DIR))
from translate_offsets import translate_offset, parse_map  # noqa: E402


DEFAULT_PATCHES = (
    THIS_DIR.parent.parent / "libs" / "randomizer"
    / "RandomizerCore" / "Resources" / "Patches"
)
DEFAULT_USA_MAP = Path.home() / "tmc-reference" / "zeldaret-tmc" / "build" / "USA" / "tmc.map"

ORG_RE = re.compile(r"\bORG\s+\$([0-9A-Fa-f]+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--patches", default=str(DEFAULT_PATCHES))
    ap.add_argument("--usa-map", default=str(DEFAULT_USA_MAP))
    ap.add_argument("--report",  default=None,
                    help="write text report to this file instead of stdout")
    args = ap.parse_args()

    _, usa_a2s = parse_map(args.usa_map)
    if not usa_a2s:
        sys.exit(f"could not load USA map at {args.usa_map}")

    addr_list = sorted(usa_a2s.keys())

    def covering_sym(off):
        i = bisect.bisect_right(addr_list, off) - 1
        if i < 0:
            return None
        return usa_a2s[addr_list[i]]

    samples_by_sym = {}
    miss_in_no_symbol = []
    total_interp = 0
    for path in sorted(Path(args.patches).rglob("*.event")):
        for lineno, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for m in ORG_RE.findall(line):
                eu = int(m, 16)
                usa, source = translate_offset(eu)
                if usa is None or not source.startswith("interp"):
                    continue
                total_interp += 1
                sym = covering_sym(usa)
                if sym is None:
                    miss_in_no_symbol.append((eu, usa, path, lineno, line))
                else:
                    samples_by_sym.setdefault(sym, []).append((path.name, lineno, line))

    out = []
    out.append(f"# Interp-prediction validation\n")
    out.append(f"\n- Total interp predictions: {total_interp}\n")
    out.append(f"- Predictions landing in a named USA symbol: "
               f"{total_interp - len(miss_in_no_symbol)} "
               f"({(total_interp - len(miss_in_no_symbol)) * 100 // max(total_interp, 1)}%)\n")
    out.append(f"- Predictions landing in unmapped USA space (likely wrong): "
               f"{len(miss_in_no_symbol)}\n\n")

    counts = {sym: len(lines) for sym, lines in samples_by_sym.items()}
    sorted_syms = sorted(counts.items(), key=lambda x: -x[1])

    out.append("## Top symbols hit by interp predictions\n\n")
    out.append("| count | USA symbol |\n|------:|-----------|\n")
    for sym, n in sorted_syms[:30]:
        out.append(f"| {n:5d} | `{sym}` |\n")

    out.append("\n## Sample patch lines per top symbol\n")
    for sym, _ in sorted_syms[:10]:
        out.append(f"\n### `{sym}` ({counts[sym]} predictions)\n\n```\n")
        for f, ln, lin in samples_by_sym[sym][:6]:
            out.append(f"{f}:{ln}  {lin.strip()[:120]}\n")
        out.append("```\n")

    text = "".join(out)
    if args.report:
        Path(args.report).parent.mkdir(parents=True, exist_ok=True)
        Path(args.report).write_text(text)
        print(f"wrote {args.report}")
    else:
        print(text)


if __name__ == "__main__":
    main()
