#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="$(tr -d '\r\n' < "${ROOT}/platform/3ds/version.txt")"
REGION="${1:-${TMC_3DS_REGION:-USA}}"
REGION="$(printf '%s' "${REGION}" | tr '[:lower:]' '[:upper:]')"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
case "${REGION}" in
  USA)
    BUILD="${ROOT}/build-3ds/game"
    SUFFIX=""
    REGION_LABEL="USA"
    ;;
  EU)
    BUILD="${ROOT}/build-3ds/eu"
    SUFFIX="-eu"
    REGION_LABEL="Europe"
    ;;
  *)
    printf 'Unsupported 3DS region: %s (use USA or EU)\n' "${REGION}" >&2
    exit 2
    ;;
esac
TOOLS_ROOT="${TMC3DS_TOOLS_ROOT:-${ROOT}/../Tools/bin}"
MAKEROM="${MAKEROM:-${TOOLS_ROOT}/makerom}"
BANNERTOOL="${BANNERTOOL:-${TOOLS_ROOT}/bannertool}"

if [[ ! -x "${MAKEROM}" ]] && command -v makerom >/dev/null 2>&1; then
  MAKEROM="$(command -v makerom)"
fi
if [[ ! -x "${BANNERTOOL}" ]] && command -v bannertool >/dev/null 2>&1; then
  BANNERTOOL="$(command -v bannertool)"
fi
if [[ ! -x "${MAKEROM}" && -x "${DEVKITPRO}/tools/bin/makerom" ]]; then
  MAKEROM="${DEVKITPRO}/tools/bin/makerom"
fi
if [[ ! -x "${BANNERTOOL}" && -x "${DEVKITPRO}/tools/bin/bannertool" ]]; then
  BANNERTOOL="${DEVKITPRO}/tools/bin/bannertool"
fi

export DEVKITPRO
cmake -S "${ROOT}/platform/3ds" -B "${BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
  -DTMC_3DS_REGION="${REGION}" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD}" --parallel "${TMC3DS_JOBS:-4}"

if [[ ! -x "${MAKEROM}" || ! -x "${BANNERTOOL}" ]]; then
  printf '3DSX ready; makerom/bannertool are unavailable for CIA packaging.\n'
  exit 0
fi

"${BANNERTOOL}" makesmdh \
  -s "The Minish Cap 3DS v${VERSION} ${REGION_LABEL}" \
  -l "The Minish Cap 3DS v${VERSION} ${REGION_LABEL}" \
  -p "Esteban PDN / Project Picori / samyost1" \
  -i "${ROOT}/platform/3ds/assets/icon-48.png" \
  -f visible,nosavebackups \
  -o "${BUILD}/tmc-3ds.icn"

"${BANNERTOOL}" makebanner \
  -i "${ROOT}/platform/3ds/assets/banner.png" \
  -a "${ROOT}/platform/3ds/assets/banner.wav" \
  -o "${BUILD}/tmc-3ds.bnr"

(
cd "${ROOT}"
"${MAKEROM}" -f cia -o "${BUILD}/tmc-3ds-v${VERSION}${SUFFIX}.cia" \
  -DAPP_ROMFS="${BUILD#"${ROOT}/"}/romfs" \
  -rsf "${ROOT}/platform/3ds/cia/tmc3ds.rsf" -target t -exefslogo \
  -elf "${BUILD}/tmc-3ds.elf" -icon "${BUILD}/tmc-3ds.icn" \
  -banner "${BUILD}/tmc-3ds.bnr"
)

printf 'Ready:\n  %s\n  %s\n' \
  "${BUILD}/tmc-3ds-v${VERSION}${SUFFIX}.3dsx" "${BUILD}/tmc-3ds-v${VERSION}${SUFFIX}.cia"
