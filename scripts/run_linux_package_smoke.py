#!/usr/bin/env python3
"""Build and verify the Linux-native WP-605D Debian package.

Installation is performed into an isolated dpkg root and is always paired
with an uninstall before the temporary directory is removed.  The host
package database and filesystem are never touched.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PACKAGE_PREFIX = "png-analyzer-"


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
        default=Path("build/linux-packages"),
        help="generated package directory relative to the repository root",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if not sys.platform.startswith("linux"):
        print("linux package smoke: NOT_CONFIGURED (Linux host required)")
        return
    for tool in ("cmake", "cpack", "dpkg", "dpkg-deb"):
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
    target_list = run(
        ["cmake", "--build", "--preset", args.preset, "--target", "help"],
        environment,
        capture=True,
    ).stdout
    have_gui = "pnga_analyzer_gui" in target_list
    if have_gui:
        run(
            [
                "cmake",
                "--build",
                "--preset",
                args.preset,
                "--target",
                "pnga_analyzer_gui",
                "--parallel",
                str(args.jobs),
            ],
            environment,
        )

    package_dir = args.package_dir if args.package_dir.is_absolute() else ROOT / args.package_dir
    package_dir.mkdir(parents=True, exist_ok=True)
    for old in package_dir.glob(f"{PACKAGE_PREFIX}*"):
        if old.is_file():
            old.unlink()
    run(
        [
            "cpack",
            "--config",
            str(build_dir / "CPackConfig.cmake"),
            "-G",
            "DEB",
            "-B",
            str(package_dir),
        ],
        environment,
    )

    packages = sorted(package_dir.glob(f"{PACKAGE_PREFIX}*.deb"))
    if len(packages) != 1:
        raise SystemExit(f"expected one Debian package, found: {packages}")
    package = packages[0]
    metadata = run(["dpkg-deb", "--info", str(package)], environment, capture=True).stdout
    metadata_lines = {line.strip() for line in metadata.splitlines()}
    if "Package: png-analyzer" not in metadata_lines:
        raise SystemExit(f"Debian metadata missing package name: {metadata}")
    if not any(line.startswith("Version: 0.1.0") for line in metadata_lines):
        raise SystemExit(f"Debian metadata missing version: {metadata}")
    if not any(line.startswith("Architecture:") for line in metadata_lines):
        raise SystemExit(f"Debian metadata missing architecture: {metadata}")
    contents = run(["dpkg-deb", "--contents", str(package)], environment, capture=True).stdout
    for required in ("./usr/bin/pnga", "./usr/share/png-analyzer/LICENSE"):
        if required not in contents:
            raise SystemExit(f"Debian package missing {required!r}: {contents}")
    if have_gui:
        for size in (16, 24, 32, 48, 64, 128, 256, 512):
            required = (
                f"./usr/share/icons/hicolor/{size}x{size}/apps/"
                "png-analyzer.png"
            )
            if required not in contents:
                raise SystemExit(f"Debian package missing {required!r}: {contents}")
        desktop_entry = "./usr/share/applications/png-analyzer.desktop"
        if desktop_entry not in contents:
            raise SystemExit(
                f"Debian package missing {desktop_entry!r}: {contents}"
            )

    with tempfile.TemporaryDirectory(prefix="pnga-deb-root-") as temp:
        root = Path(temp)
        # dpkg's --root plus --force-not-root keeps both install and uninstall
        # state inside the temporary root while remaining usable by CI users.
        status_dir = root / "var" / "lib" / "dpkg"
        status_dir.mkdir(parents=True)
        (status_dir / "status").write_text("", encoding="utf-8")
        run(
            [
                "dpkg",
                "--root",
                str(root),
                "--force-not-root",
                "--force-bad-path",
                "-i",
                str(package),
            ],
            environment,
        )
        binary = root / "usr" / "bin" / "pnga"
        if not binary.is_file():
            raise SystemExit(f"installed CLI missing: {binary}")
        version = run([str(binary), "--version"], environment, capture=True).stdout.strip()
        if version != "pnga 0.1.0":
            raise SystemExit(f"unexpected installed version: {version!r}")
        run(
            [
                "dpkg",
                "--root",
                str(root),
                "--force-not-root",
                "--force-bad-path",
                "-r",
                "png-analyzer",
            ],
            environment,
        )
        if binary.exists():
            raise SystemExit(f"uninstall left CLI behind: {binary}")

    print(f"linux package smoke: PASS package={package} version={version}")


if __name__ == "__main__":
    main()
