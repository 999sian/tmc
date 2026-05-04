#!/usr/bin/env python3
"""
TMC PC Port — interactive build script
Run from repository root: python3 build.py
"""

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

# ── Constants ─────────────────────────────────────────────────────────────────

REPO_ROOT = Path(__file__).resolve().parent
PLATFORM  = platform.system()   # Linux | Windows | Darwin
EXE_NAME  = "tmc_pc.exe" if PLATFORM == "Windows" else "tmc_pc"

def _read_sha1_file(filename: str) -> Optional[str]:
    p = REPO_ROOT / filename
    if p.exists():
        return p.read_text().split()[0]
    return None

VERSIONS = {
    "USA": {
        "rom_filename": "baserom.gba",
        "sha1":         _read_sha1_file("tmc.sha1"),
        "game_version": "USA",
    },
    "EU": {
        "rom_filename": "baserom_eu.gba",
        "sha1":         _read_sha1_file("tmc_eu.sha1"),
        "game_version": "EU",
    },
}

SHA1_TO_VERSION = {
    v["sha1"]: k for k, v in VERSIONS.items() if v["sha1"]
}

# ── UI helpers ────────────────────────────────────────────────────────────────

W = 64
USE_UNICODE_UI = PLATFORM != "Windows"

def _ui_char(unicode_ch: str, ascii_ch: str) -> str:
    return unicode_ch if USE_UNICODE_UI else ascii_ch

def hr(ch=None):
    ch = ch or _ui_char("─", "-")
    print(ch * W)
def blank():       print()
def header(t):     hr(_ui_char("═", "=")); print(f"  {t}"); hr(_ui_char("═", "="))
def section(t):    blank(); hr(); print(f"  {t}"); hr()
def ok(m):         print(f"  \033[32m{_ui_char('✓', 'OK')}\033[0m  {m}")
def warn(m):       print(f"  \033[33m{_ui_char('!', 'WARN')}\033[0m  {m}")
def err(m):        print(f"  \033[31m{_ui_char('✗', 'ERR')}\033[0m  {m}")
def info(m):       print(f"     {m}")

def prompt(msg: str, choices=None) -> str:
    suffix = f" [{'/'.join(choices)}]" if choices else ""
    while True:
        try:
            ans = input(f"  → {msg}{suffix}: ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            blank(); sys.exit(0)
        if not choices or ans in choices:
            return ans
        err(f"Enter one of: {', '.join(choices)}")

# ── Subprocess ────────────────────────────────────────────────────────────────

def run_cmd(cmd, env=None, cwd=None, check=True) -> subprocess.CompletedProcess:
    display = " ".join(str(c) for c in cmd)
    print(f"\n  \033[90m$ {display}\033[0m")
    result = subprocess.run(
        [str(c) for c in cmd],
        env=env,
        cwd=str(cwd) if cwd else None,
    )
    if check and result.returncode != 0:
        raise RuntimeError(f"Command exited {result.returncode}: {display}")
    return result

# ── File utils ────────────────────────────────────────────────────────────────

def sha1_file(path: Path) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def dir_populated(d: Path) -> bool:
    try:
        return d.is_dir() and any(True for _ in d.iterdir())
    except OSError:
        return False

def replace_file_atomically(src: Path, dst: Path) -> None:
    """Copy src to a temp file beside dst, then atomically replace dst.

    This avoids ETXTBSY on Linux when dst is a currently running executable.
    """
    tmp = dst.with_name(dst.name + ".tmp")
    shutil.copy2(src, tmp)
    os.replace(tmp, dst)

# ── Dependency detection ──────────────────────────────────────────────────────

def detect_distro() -> str:
    try:
        with open("/etc/os-release") as f:
            for line in f:
                if line.startswith("ID="):
                    return line.split("=", 1)[1].strip().strip('"').lower()
    except FileNotFoundError:
        pass
    return "unknown"

def pkg_config_ok(name: str) -> bool:
    return subprocess.run(["pkg-config", "--exists", name], capture_output=True).returncode == 0

# (label, check_fn, arch_pkg, apt_pkg)
LINUX_DEPS = [
    ("git",           lambda: bool(shutil.which("git")),        "git",           "git"),
    ("libpng",        lambda: pkg_config_ok("libpng"),          "libpng",        "libpng-dev"),
    ("fmt",           lambda: pkg_config_ok("fmt"),             "fmt",           "libfmt-dev"),
    ("nlohmann-json", lambda: pkg_config_ok("nlohmann_json"),   "nlohmann-json", "nlohmann-json3-dev"),
]

WIN_DEPS = [
    ("xmake", lambda: bool(shutil.which("xmake"))),
    ("git",   lambda: bool(shutil.which("git"))),
]

def check_deps(non_interactive: bool = False) -> bool:
    all_ok = True

    if PLATFORM == "Linux":
        distro   = detect_distro()
        is_arch  = distro in ("arch", "cachyos", "manjaro", "endeavouros", "garuda")
        miss_arch, miss_apt = [], []

        for label, check_fn, arch_pkg, apt_pkg in LINUX_DEPS:
            if check_fn():
                ok(label)
            else:
                err(label)
                miss_arch.append(arch_pkg)
                miss_apt.append(apt_pkg)

        if miss_arch:
            blank()
            warn("Missing dependencies. Install with:")
            if is_arch:
                info(f"  sudo pacman -S {' '.join(miss_arch)}")
            else:
                info(f"  sudo apt install {' '.join(miss_apt)}")
            blank()
            if non_interactive:
                all_ok = False
            elif prompt("Attempt automatic install?", ["y", "n"]) == "y":
                cmd = (["sudo", "pacman", "-S", "--noconfirm"] + miss_arch if is_arch
                       else ["sudo", "apt", "install", "-y"] + miss_apt)
                if run_cmd(cmd, check=False).returncode != 0:
                    err("Automatic install failed — install manually and re-run.")
                    return False
                # Re-check after install
                still_missing = [l for l, fn, *_ in LINUX_DEPS if not fn()]
                if still_missing:
                    err(f"Still missing after install: {', '.join(still_missing)}")
                    return False
            else:
                all_ok = False

    elif PLATFORM == "Windows":
        missing = []
        for label, check_fn in WIN_DEPS:
            if check_fn():
                ok(label)
            else:
                err(label); missing.append(label)
        if missing:
            blank()
            warn("Missing: " + ", ".join(missing))
            info("xmake : https://xmake.io")
            info("git   : https://git-scm.com")
            all_ok = False

    else:
        err(f"Unsupported platform: {PLATFORM}")
        return False

    # Git submodules
    virua  = REPO_ROOT / "libs" / "ViruaPPU"
    virtua = REPO_ROOT / "libs" / "VirtuaAPU"
    if not dir_populated(virua) or not dir_populated(virtua):
        warn("Git submodules not initialized — fetching...")
        try:
            run_cmd(["git", "submodule", "update", "--init", "--recursive"], cwd=REPO_ROOT)
            ok("Git submodules")
        except RuntimeError:
            err("Failed to initialize submodules")
            all_ok = False
    else:
        ok("Git submodules")

    return all_ok

# ── ROM handling ──────────────────────────────────────────────────────────────

def scan_roms() -> dict:
    """Return {version: Path} for all recognized ROMs found nearby."""
    search_dirs = [REPO_ROOT, REPO_ROOT.parent, Path.home() / "Downloads"]
    found: dict = {}
    for d in search_dirs:
        if not d.is_dir():
            continue
        candidates = sorted(d.glob("*.gba")) + sorted(d.glob("*.GBA"))
        for gba in candidates:
            try:
                digest = sha1_file(gba)
            except OSError:
                continue
            version = SHA1_TO_VERSION.get(digest)
            if version and version not in found:
                found[version] = gba
    return found

def ensure_roms(selected: list, found: dict, non_interactive: bool = False) -> dict:
    """Ensure each selected ROM is at REPO_ROOT/<rom_filename>. Returns {version: bool}."""
    result = {}
    for v in selected:
        meta   = VERSIONS[v]
        target = REPO_ROOT / meta["rom_filename"]
        sha1   = meta["sha1"]

        if target.exists() and sha1 and sha1_file(target) == sha1:
            ok(f"{v}: {target.name}")
            result[v] = True
            continue

        if target.exists() and not sha1:
            warn(f"{v}: SHA1 unknown (missing {v.lower()}.sha1), using existing file")
            result[v] = True
            continue

        if v in found:
            src = found[v]
            if src.resolve() == target.resolve():
                ok(f"{v}: {target.name}")
                result[v] = True
                continue
            info(f"Copy  {src}")
            info(f"  →   {target}")
            if non_interactive or prompt("Proceed?", ["y", "n"]) == "y":
                shutil.copy2(src, target)
                ok(f"Copied {target.name}")
                result[v] = True
            else:
                result[v] = False
        else:
            err(f"{v} ROM not found: {meta['rom_filename']}")
            if sha1:
                info(f"Expected SHA1: {sha1}")
            info(f"Place ROM at:  {target}")
            result[v] = False

    return result

# ── Build pipeline ────────────────────────────────────────────────────────────

def make_env() -> dict:
    env = os.environ.copy()
    env["XMAKE_ROOT"] = "y"

    if PLATFORM == "Windows":
        env.setdefault("TMC_XMAKE_PLATFORM", "mingw")
        env.setdefault("TMC_XMAKE_TOOLCHAIN", "mingw")

    if PLATFORM == "Linux" and pkg_config_ok("sdl3"):
        env["XMAKE_USE_SYSTEM_SDL3"] = "1"

    return env

def build_version(version: str, env: dict, non_interactive: bool = False) -> Optional[Path]:
    dist_dir = REPO_ROOT / "dist" / version

    # Skip prompt if dist binary already exists
    dst_bin = dist_dir / EXE_NAME
    if dst_bin.exists() and not non_interactive:
        ans = prompt(f"{version} already built at dist/{version}/{EXE_NAME}. Rebuild?", ["y", "n"])
        if ans == "n":
            return dst_bin

    dist_dir.mkdir(parents=True, exist_ok=True)

    configure_cmd = ["xmake", "f", "-y", f"--game_version={version}"]

    xmake_platform = env.get("TMC_XMAKE_PLATFORM", "").strip()
    if xmake_platform:
        configure_cmd.append(f"--plat={xmake_platform}")

    toolchain = env.get("TMC_XMAKE_TOOLCHAIN", "").strip()
    if toolchain:
        configure_cmd.append(f"--toolchain={toolchain}")

    assets_dir = REPO_ROOT / "build" / "pc" / "assets"
    assets_src_dir = REPO_ROOT / "build" / "pc" / "assets_src"
    assets_ready = assets_dir.exists() and assets_src_dir.exists()

    steps = [
        (f"Configure ({version})", configure_cmd),
    ]

    if assets_ready:
        info("Assets already exist in build/pc/assets and build/pc/assets_src — skipping extract/convert/build_assets.")
    else:
        steps.extend([
            ("Extract assets",              ["xmake", "extract_assets"]),
            ("Convert assets",              ["xmake", "convert_assets"]),
            ("Build assets",                ["xmake", "build_assets"]),
        ])

    steps.append((f"Compile tmc_pc ({version})", ["xmake", "build", "-y", "tmc_pc"]))

    for label, cmd in steps:
        info(f"{label}...")
        try:
            run_cmd(cmd, env=env, cwd=REPO_ROOT)
        except RuntimeError as exc:
            err(str(exc))
            return None

    # Copy ROM so the runtime asset extractor can find it
    import shutil
    shutil.copy2("baserom.gba", "build/pc/baserom.gba")
    print("✓  Copied baserom.gba → build/pc/")
    
    # asset_extractor: generates build/pc/assets/ + build/pc/assets_src/
    extractor = REPO_ROOT / "build" / "pc" / (
        "asset_extractor.exe" if PLATFORM == "Windows" else "asset_extractor"
    )
    if extractor.exists():
        info("Extracting runtime assets...")
        try:
            run_cmd([extractor], cwd=REPO_ROOT)
        except RuntimeError:
            warn("asset_extractor failed — runtime assets may be incomplete")
    else:
        warn("asset_extractor not built — run: xmake build asset_extractor")

    # ── Copy artefacts to dist/<version>/ ────────────────────────────────────

    src_bin = REPO_ROOT / "build" / "pc" / EXE_NAME
    if not src_bin.exists():
        err(f"Binary not found: {src_bin}")
        return None

    replace_file_atomically(src_bin, dst_bin)
    if PLATFORM != "Windows":
        dst_bin.chmod(dst_bin.stat().st_mode | 0o111)
    ok(f"Binary    →  dist/{version}/{EXE_NAME}")

    # Runtime assets consumed by the PC port live under build/pc/.
    # build/<version>/assets only contains the intermediate extraction tree.
    for src_name in ("assets", "assets_src"):
        src = REPO_ROOT / "build" / "pc" / src_name
        dst = dist_dir / src_name
        if src.exists():
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
            ok(f"{src_name}/  →  dist/{version}/{src_name}/")
        else:
            warn(f"build/{version}/{src_name}/ not found — skipping")

    sounds_src = REPO_ROOT / "assets" / "sounds.json"
    if sounds_src.exists():
        shutil.copy2(sounds_src, dist_dir / "sounds.json")
        ok(f"sounds.json →  dist/{version}/sounds.json")
    else:
        warn("assets/sounds.json not found — songs will be silent")

    return dst_bin

# ── Main ──────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="TMC PC Port build helper")
    parser.add_argument("--usa", action="store_true", help="Build USA version without prompts")
    parser.add_argument("--eur", action="store_true", help="Build EU version without prompts")
    return parser.parse_args()

def main():
    args = parse_args()
    non_interactive = args.usa or args.eur

    header("TMC PC Port Builder")
    info(f"Platform : {PLATFORM}")
    info(f"Repo root: {REPO_ROOT}")

    section("Dependencies")
    if not check_deps(non_interactive=non_interactive):
        err("Fix missing dependencies and re-run.")
        sys.exit(1)

    section("ROM Detection")
    found = scan_roms()
    for v, path in found.items():
        ok(f"{v}: {path}")
    for v in VERSIONS:
        if v not in found:
            expected = REPO_ROOT / VERSIONS[v]["rom_filename"]
            if expected.exists():
                ok(f"{v}: {expected.name} (already in repo root)")
            else:
                warn(f"{v}: not found")

    keys = list(VERSIONS.keys())
    if non_interactive:
        selected = []
        if args.usa:
            selected.append("USA")
        if args.eur:
            selected.append("EU")
        info("Non-interactive mode enabled via --usa/--eur")
        info(f"Selected: {', '.join(selected)}")
    else:
        section("Select Version")
        for i, v in enumerate(keys, 1):
            rom_ready = (
                v in found
                or (REPO_ROOT / VERSIONS[v]["rom_filename"]).exists()
            )
            tag = "\033[32mROM ready\033[0m" if rom_ready else "\033[31mROM missing\033[0m"
            print(f"  {i}) {v:<6} [{tag}]")
        print(f"  {len(keys) + 1}) Both")
        print(f"  q) Quit")

        valid = [str(i) for i in range(1, len(keys) + 2)] + ["q"]
        choice = prompt("Choice", valid)
        if choice == "q":
            sys.exit(0)

        idx = int(choice)
        selected = keys if idx == len(keys) + 1 else [keys[idx - 1]]

    section("Preparing ROMs")
    rom_ok = ensure_roms(selected, found, non_interactive=non_interactive)
    buildable = [v for v in selected if rom_ok.get(v)]
    skipped   = [v for v in selected if not rom_ok.get(v)]

    if skipped:
        warn(f"Skipping (no ROM): {', '.join(skipped)}")
    if not buildable:
        err("Nothing to build.")
        sys.exit(1)

    blank()
    info(f"Will build: {', '.join(buildable)}")
    if not non_interactive and prompt("Start?", ["y", "n"]) == "n":
        sys.exit(0)

    env     = make_env()
    results = {}
    for v in buildable:
        section(f"Building {v}")
        results[v] = build_version(v, env, non_interactive=non_interactive)

    section("Done")
    any_ok = False
    for v, exe in results.items():
        if exe:
            any_ok = True
            rel = exe.parent.relative_to(REPO_ROOT)
            ok(f"{v} binary: {exe}")
            blank()
            info(f"  Run {v}:")
            info(f"    cd {rel}")
            run_cmd = f".\\{EXE_NAME}" if PLATFORM == "Windows" else f"./{EXE_NAME}"
            info(f"    {run_cmd}")
            blank()
        else:
            err(f"{v} — build failed")

    sys.exit(0 if any_ok else 1)

if __name__ == "__main__":
    main()