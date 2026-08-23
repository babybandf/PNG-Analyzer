#!/usr/bin/env python3
"""Build, package and smoke-test the relocatable WP-605A archive."""

import argparse
import os
import subprocess
import tarfile
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CLI_NAME = "pnga.exe" if os.name == "nt" else "pnga"


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


def extract(package, destination):
    if package.suffix == ".zip":
        with zipfile.ZipFile(package) as archive:
            archive.extractall(destination)
        return
    if package.name.endswith((".tar.gz", ".tgz")):
        with tarfile.open(package, "r:gz") as archive:
            archive.extractall(destination)
        return
    raise SystemExit(f"unsupported package format: {package}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("release",), default="release")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--package-dir",
        type=Path,
        default=Path("build/packages"),
        help="generated package directory relative to the repository root",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
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
    if "pnga_analyzer_gui" in target_list:
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
    for old in package_dir.glob("png-analyzer-*"):
        if old.is_file():
            old.unlink()
    run(
        [
            "cpack",
            "--config",
            str(ROOT / "build" / args.preset / "CPackConfig.cmake"),
            "-B",
            str(package_dir),
        ],
        environment,
    )

    packages = sorted(
        path
        for path in package_dir.glob("png-analyzer-*")
        if path.suffix == ".zip" or path.name.endswith((".tar.gz", ".tgz"))
    )
    if len(packages) != 1:
        raise SystemExit(f"expected one package, found: {packages}")

    with tempfile.TemporaryDirectory(prefix="pnga-package-") as temp:
        extracted = Path(temp)
        extract(packages[0], extracted)
        binaries = list(extracted.rglob(CLI_NAME))
        if len(binaries) != 1:
            raise SystemExit(f"expected one packaged {CLI_NAME}, found: {binaries}")
        binary = binaries[0]
        license_files = list(extracted.rglob("LICENSE"))
        if len(license_files) != 1:
            raise SystemExit(f"expected one packaged LICENSE, found: {license_files}")
        result = run([str(binary), "--version"], environment, capture=True)
        version = result.stdout.strip()
        if not version.startswith("pnga "):
            raise SystemExit(f"unexpected packaged version output: {version!r}")

    print(f"package smoke: PASS package={packages[0]} version={version}")


if __name__ == "__main__":
    main()
