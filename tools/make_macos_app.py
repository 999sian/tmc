#!/usr/bin/env python3
"""Wrap a built tmc_pc into Picori.app (macOS).

The bundle holds only the binary, the public defaults the release tarball
already ships (voxel_shapes.json, assets/rando/default.logic) and an icon.
No ROM or ROM-derived asset ever goes in: at runtime the app keeps its state
in ~/Library/Application Support/Picori (port/port_macos_bundle.c) and
self-extracts the asset cache there from the user's ROM.

    tools/make_macos_app.py --binary build/pc/tmc_pc --out dist/USA/Picori.app --version 0.9.6
"""
import argparse
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE_NAME = "tmc_pc"
BUNDLE_ID = "io.github.999sian.picori"
ICON_SOURCE = ROOT / "docs" / "picori-logo.png"
DEFAULTS = {
    # bundle Resources/ path -> repo source
    "voxel_shapes.json": ROOT / "assets" / "voxel_shapes.json",
    "assets/rando/default.logic": ROOT / "assets" / "rando" / "default.logic",
}


def info_plist(version: str) -> dict:
    return {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": "Picori",
        "CFBundleExecutable": EXE_NAME,
        "CFBundleIconFile": "Picori",
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "Picori",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "LSApplicationCategoryType": "public.app-category.adventure-games",
        # Opt in to macOS Game Mode when fullscreen.
        "GCSupportsGameMode": True,
        "NSHighResolutionCapable": True,
        "NSSupportsAutomaticGraphicsSwitching": True,
        "NSHumanReadableCopyright": "GPL-3.0. Requires your own copy of The Minish Cap.",
    }


def make_icns(png: Path, dest: Path) -> bool:
    """docs/picori-logo.png -> Picori.icns via sips + iconutil (both stock macOS)."""
    if not png.is_file() or not shutil.which("sips") or not shutil.which("iconutil"):
        return False
    with tempfile.TemporaryDirectory() as tmp:
        iconset = Path(tmp) / "Picori.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                px = size * scale
                name = f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png"
                subprocess.run(["sips", "-z", str(px), str(px), str(png), "--out", str(iconset / name)],
                               check=True, capture_output=True)
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(dest)], check=True)
    return True


def build_app(binary: Path, out: Path, version: str, sign: bool = True) -> Path:
    if not binary.is_file():
        raise FileNotFoundError(binary)
    if out.exists():
        shutil.rmtree(out)
    contents = out / "Contents"
    macos = contents / "MacOS"
    resources = contents / "Resources"
    macos.mkdir(parents=True)
    resources.mkdir()

    shutil.copy2(binary, macos / EXE_NAME)
    (macos / EXE_NAME).chmod(0o755)
    with open(contents / "Info.plist", "wb") as f:
        plistlib.dump(info_plist(version), f)
    (contents / "PkgInfo").write_text("APPL????")

    for rel, src in DEFAULTS.items():
        if src.is_file():
            dst = resources / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
    if not make_icns(ICON_SOURCE, resources / "Picori.icns"):
        print("warning: no icon (needs docs/picori-logo.png, sips and iconutil)", file=sys.stderr)

    # Apple Silicon refuses to run unsigned code, and editing the bundle
    # invalidated the linker's ad-hoc signature: re-seal the whole bundle.
    if sign and shutil.which("codesign"):
        subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(out)], check=True)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, required=True, help="built tmc_pc")
    parser.add_argument("--out", type=Path, required=True, help="output .app path")
    parser.add_argument("--version", required=True, help="CFBundleShortVersionString")
    parser.add_argument("--no-sign", action="store_true", help="skip the ad-hoc codesign")
    args = parser.parse_args()
    if args.out.suffix != ".app":
        parser.error("--out must end in .app")
    app = build_app(args.binary, args.out, args.version, sign=not args.no_sign)
    print(app)
    return 0


if __name__ == "__main__":
    sys.exit(main())
