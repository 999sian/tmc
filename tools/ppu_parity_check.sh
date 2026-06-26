#!/usr/bin/env bash
#
# tools/ppu_parity_check.sh — PPU byte-exact parity gate (Phase 0 oracle).
#
# For every scene in tools/ppu_corpus.txt it:
#   1. captures a deterministic PPU snapshot from a headless game run
#      (TMC_PERFCAP warp or AT_FRAME), with colour-correction OFF so the
#      live framebuffer is renderer-pure;
#   2. computes Tier A = tools/ppu_parity static render hash of the snapshot
#      (mode-aware: mode1 + mode2), and
#      Tier B = the end-to-end live-frame hash perfcap prints (covers HDMA /
#      mode2 affine / full composite);
#   3. compares both against tools/ppu_golden_hashes.txt.
#
# Usage:
#   tools/ppu_parity_check.sh            # check against golden; nonzero on mismatch
#   tools/ppu_parity_check.sh --update   # (re)generate the golden file
#
# Env:
#   TMC_PC        path to the built game   (default build/pc/tmc_pc)
#   TMC_BASEROM   path to the ROM          (default baserom.gba)
#   TMC_REGION    label written to golden  (default usa)
#   MODE1_GBA_WIDTH  render width define   (default 240)
#
# The PPU source location is auto-detected so this survives the Phase 1
# vendoring move (libs/ViruaPPU -> port/ppu) with no edit.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODE1_GBA_WIDTH="${MODE1_GBA_WIDTH:-240}"
TMC_PC="${TMC_PC:-build/pc/tmc_pc}"
TMC_BASEROM="${TMC_BASEROM:-$ROOT/baserom.gba}"
TMC_REGION="${TMC_REGION:-usa}"
# Overridable so a richer, save-dependent local corpus can be used for
# same-machine before/after phase verification (PPU_CORPUS / PPU_GOLDEN),
# while the committed defaults stay portable (hermetic atframe scenes) for CI.
CORPUS="${PPU_CORPUS:-$ROOT/tools/ppu_corpus.txt}"
GOLDEN="${PPU_GOLDEN:-$ROOT/tools/ppu_golden_hashes.txt}"

# Auto-detect vendored vs submodule PPU source.
if [ -d "$ROOT/port/ppu/src" ]; then
    PPU_SRC="$ROOT/port/ppu/src"; PPU_INC="$ROOT/port/ppu/include"
else
    PPU_SRC="$ROOT/libs/ViruaPPU/src"; PPU_INC="$ROOT/libs/ViruaPPU/include"
fi

UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
PARITY="$WORK/ppu_parity"

# SIMD flags only on x86_64 (the render is integer-only, so SIMD does not
# affect the hash — it just matches the game's release flags on x86).
SIMD=""
case "$(uname -m)" in x86_64|amd64) SIMD="-mavx2 -mfma";; esac

echo "[parity] PPU source: $PPU_SRC"
gcc -O3 $SIMD -fopenmp -I "$PPU_INC" -DMODE1_GBA_WIDTH="$MODE1_GBA_WIDTH" \
    "$ROOT/tools/ppu_parity.c" "$PPU_SRC"/virtuappu.c "$PPU_SRC"/mode*.c \
    -o "$PARITY" -lm

[ -x "$TMC_PC" ] || { echo "[parity] FATAL: $TMC_PC not built"; exit 3; }
[ -f "$TMC_BASEROM" ] || { echo "[parity] FATAL: ROM not found: $TMC_BASEROM"; exit 3; }

# capture <spec> <out.bin>  -> echoes the Tier B frame_hash on stdout
capture() {
    local spec="$1" out="$2" warp="" atframe="" log="$WORK/cap.log"
    case "$spec" in
        warp=*)    warp="${spec#warp=}";;
        atframe=*) atframe="${spec#atframe=}";;
        *) echo "[parity] bad spec: $spec" >&2; return 1;;
    esac
    timeout -s KILL 90 env \
        TMC_AUTOPLAY=1 TMC_PERFCAP=1 \
        ${warp:+TMC_PERFCAP_WARP="$warp"} \
        ${atframe:+TMC_PERFCAP_AT_FRAME="$atframe"} \
        TMC_PERFCAP_DUMP="$out" \
        TMC_COLOR_CORRECTION=0 \
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        TMC_BASEROM="$TMC_BASEROM" \
        "$TMC_PC" --no-audio >"$log" 2>&1 || true
    grep -oE 'frame_hash=0x[0-9a-f]+' "$log" | tail -1 | sed 's/frame_hash=//'
}

declare -A GOLD_A GOLD_B
if [ "$UPDATE" = 0 ]; then
    [ -f "$GOLDEN" ] || { echo "[parity] FATAL: no golden file; run --update first"; exit 3; }
    while IFS=$'\t' read -r reg name a b; do
        [ "${reg:0:1}" = "#" ] && continue
        [ -z "${name:-}" ] && continue
        GOLD_A["$reg/$name"]="$a"; GOLD_B["$reg/$name"]="$b"
    done < "$GOLDEN"
fi

TMP_GOLDEN="$WORK/golden.txt"   # data rows only; header prepended at write time
fail=0; n=0
: > "$TMP_GOLDEN"
while IFS=$'\t' read -r name regions spec _desc; do
    [ "${name:0:1}" = "#" ] && continue
    [ -z "${name:-}" ] && continue
    # Region scoping: "all" or a comma list that includes TMC_REGION.
    if [ "$regions" != "all" ] && [[ ",$regions," != *",$TMC_REGION,"* ]]; then
        continue
    fi
    n=$((n+1))
    bin="$WORK/$name.bin"
    tierB="$(capture "$spec" "$bin")"
    if [ ! -s "$bin" ]; then echo "[parity] FAIL $name: no snapshot captured"; fail=1; continue; fi
    tierA="$("$PARITY" "$bin" 2>/dev/null)"
    : "${tierB:=MISSING}"
    key="$TMC_REGION/$name"
    if [ "$UPDATE" = 1 ]; then
        printf '%s\t%s\t%s\t%s\n' "$TMC_REGION" "$name" "$tierA" "$tierB" >> "$TMP_GOLDEN"
        echo "[parity] record $key  A=$tierA  B=$tierB"
    else
        ea="${GOLD_A[$key]:-?}"; eb="${GOLD_B[$key]:-?}"
        if [ "$tierA" = "$ea" ] && [ "$tierB" = "$eb" ]; then
            echo "[parity] PASS $key  A=$tierA  B=$tierB"
        else
            echo "[parity] FAIL $key"
            echo "          Tier A got $tierA  want $ea"
            echo "          Tier B got $tierB  want $eb"
            fail=1
        fi
    fi
done < "$CORPUS"

if [ "$UPDATE" = 1 ]; then
    # Merge: keep golden rows for other regions, replace this region's rows.
    if [ -f "$GOLDEN" ]; then
        grep -vE "^${TMC_REGION}	" "$GOLDEN" 2>/dev/null | grep -v '^#' >> "$TMP_GOLDEN" || true
    fi
    { printf '# region\tname\ttierA(static)\ttierB(e2e)\n'
      LC_ALL=C sort "$TMP_GOLDEN"; } > "$GOLDEN"
    echo "[parity] wrote $GOLDEN ($n scenes, region=$TMC_REGION)"
    exit 0
fi

echo "[parity] $n scenes checked, region=$TMC_REGION"
[ "$fail" = 0 ] && echo "[parity] ALL PASS" || echo "[parity] MISMATCH"
exit $fail
