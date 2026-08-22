#!/usr/bin/env python3
"""Run the repeatable WP-5U6C GUI gate and record platform evidence."""

import argparse
import json
import os
import platform
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
TARGET = "pnga_gui_cross_platform_gate_tests"
TESTS = (
    "gui_cross_platform_gate_tests",
    "gui_cross_platform_gate_dpi_150_tests",
    "gui_cross_platform_gate_dpi_200_tests",
)


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
    parser.add_argument("--preset", choices=("dev",), default="dev")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/gui-gate/wp-5u6c-evidence.json"),
        help="generated evidence path relative to the repository root",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    target_list = run(
        ["cmake", "--build", "--preset", args.preset, "--target", "help"],
        environment,
        capture=True,
    ).stdout
    if TARGET not in target_list:
        report = {
            "schema": "pnga-gui-gate-evidence-v1",
            "status": "NOT_CONFIGURED",
            "host": {
                "system": platform.system(),
                "release": platform.release(),
                "machine": platform.machine(),
            },
            "tests": list(TESTS),
            "coverage": {"covered": ["Qt-free build boundary"], "not_covered": list(TESTS)},
        }
        output = args.output if args.output.is_absolute() else ROOT / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, separators=(",", ":")) + "\n")
        print(f"GUI gate: NOT_CONFIGURED report={output}")
        return 0

    run(
        [
            "cmake",
            "--build",
            "--preset",
            args.preset,
            "--target",
            TARGET,
            "--parallel",
            str(args.jobs),
        ],
        environment,
    )
    regex = "^(" + "|".join(TESTS) + ")$"
    result = run(
        ["ctest", "--preset", args.preset, "-R", regex, "--output-on-failure"],
        environment,
        capture=True,
    )
    print(result.stdout, end="")
    binary = ROOT / "build" / args.preset / "tests" / "gui" / TARGET
    runtime = []
    for label, scale in (("default", None), ("dpi_150", "1.5"),
                         ("dpi_200", "2")):
        scaled_environment = environment.copy()
        if scale is not None:
            scaled_environment["QT_SCALE_FACTOR"] = scale
            scaled_environment["QT_SCALE_FACTOR_ROUNDING_POLICY"] = "PassThrough"
        direct = subprocess.run(
            [str(binary)], cwd=ROOT, env=scaled_environment,
            check=True, text=True, capture_output=True,
        )
        match = re.search(
            r"GUI gate platform=.*?qt= ([^ ]+) screen_dpr= ([^ ]+) logical_dpi= ([^\n]+)",
            direct.stdout + direct.stderr,
        )
        if match is None:
            raise SystemExit(f"missing GUI platform evidence for {label}")
        runtime.append({
            "case": label,
            "qt": match.group(1),
            "screen_dpr": match.group(2),
            "logical_dpi": match.group(3).strip(),
        })
    output = args.output if args.output.is_absolute() else ROOT / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": "pnga-gui-gate-evidence-v1",
        "status": "PASS",
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
        },
        "tests": list(TESTS),
        "runtime": runtime,
        "coverage": {
            "covered": [
                "900x600 and 1600x1000 reference layouts",
                "current, 150% and 200% Qt scale factors",
                "light/dark palette switch",
                "Open shortcut and coordinate-to-inspector focus chain",
                "all Inspector page accessible names",
                "Block/Huffman/Decode Trace truncation sentinels",
            ],
            "not_covered": [
                "native Windows window-system rendering",
                "native Linux window-system rendering",
                "screen-reader certification",
                "native DMG/MSIX/AppImage/Flatpak deployment",
            ],
        },
    }
    output.write_text(json.dumps(report, ensure_ascii=True, separators=(",", ":")) + "\n")
    print(f"GUI gate: PASS report={output}")


if __name__ == "__main__":
    raise SystemExit(main())
