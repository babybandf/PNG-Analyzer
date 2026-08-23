#!/usr/bin/env python3
"""Build and smoke-test the macOS/Windows native WP-605E installer.

The current native boundary packages the CLI. GUI framework deployment is a
separate gate and is not inferred from a CLI-only installer result.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PREFIX = "png-analyzer-"


def run(command, environment, capture=False):
    print("$ " + " ".join(command), flush=True)
    return subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
        check=True,
        text=True,
        capture_output=capture,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("release",), default="release")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--package-dir",
        type=Path,
        default=Path("build/native-packages"),
        help="generated package directory relative to the repository root",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if sys.platform == "darwin":
        generator = "DragNDrop"
        suffix = ".dmg"
        required_tools = ("cmake", "cpack", "hdiutil")
    elif os.name == "nt":
        generator = "NSIS"
        suffix = ".exe"
        required_tools = ("cmake", "cpack")
    else:
        print("native package smoke: NOT_CONFIGURED (macOS or Windows host required)")
        return
    for tool in required_tools:
        if shutil.which(tool) is None:
            raise SystemExit(f"required tool not found: {tool}")

    environment = os.environ.copy()
    build_dir = ROOT / "build" / args.preset
    if not (build_dir / "CMakeCache.txt").is_file():
        run(["cmake", "--preset", args.preset], environment)
    run(
        [
            "cmake",
            "--build",
            "--preset",
            args.preset,
            "--target",
            "pnga_cli",
            "--parallel",
            str(args.jobs),
        ],
        environment,
    )

    package_dir = args.package_dir if args.package_dir.is_absolute() else ROOT / args.package_dir
    package_dir.mkdir(parents=True, exist_ok=True)
    for old in package_dir.glob(f"{PREFIX}*"):
        if old.is_file():
            old.unlink()
    run(
        [
            "cpack",
            "--config",
            str(build_dir / "CPackConfig.cmake"),
            "-G",
            generator,
            "-B",
            str(package_dir),
        ],
        environment,
    )
    packages = sorted(path for path in package_dir.glob(f"{PREFIX}*{suffix}"))
    if len(packages) != 1:
        raise SystemExit(f"expected one {generator} package, found: {packages}")
    package = packages[0]

    if sys.platform == "darwin":
        with tempfile.TemporaryDirectory(prefix="pnga-dmg-") as temp:
            mount = Path(temp) / "mount"
            mount.mkdir()
            run(
                [
                    "hdiutil",
                    "attach",
                    "-nobrowse",
                    "-readonly",
                    "-mountpoint",
                    str(mount),
                    str(package),
                ],
                environment,
            )
            try:
                binaries = list(mount.rglob("pnga"))
                if len(binaries) != 1:
                    raise SystemExit(f"expected one mounted pnga, found: {binaries}")
                version = run([str(binaries[0]), "--version"], environment, capture=True).stdout.strip()
            finally:
                run(["hdiutil", "detach", str(mount)], environment)
    else:
        with tempfile.TemporaryDirectory(prefix="pnga-nsis-") as temp:
            install_root = Path(temp) / "installed"
            installer_args = [str(package), "/S", f"/D={install_root}"]
            run(installer_args, environment)
            binaries = list(install_root.rglob("pnga.exe"))
            if len(binaries) != 1:
                raise SystemExit(f"expected one installed pnga.exe, found: {binaries}")
            version = run([str(binaries[0]), "--version"], environment, capture=True).stdout.strip()
            uninstaller = install_root / "Uninstall.exe"
            if not uninstaller.is_file():
                raise SystemExit(f"NSIS uninstaller missing: {uninstaller}")
            run([str(uninstaller), "/S"], environment)
            if binaries[0].exists():
                raise SystemExit(f"uninstall left CLI behind: {binaries[0]}")

    if version != "pnga 0.1.0":
        raise SystemExit(f"unexpected native package version: {version!r}")
    print(f"native package smoke: PASS generator={generator} package={package} version={version}")


if __name__ == "__main__":
    main()
