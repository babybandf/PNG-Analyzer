#!/usr/bin/env python3
"""Run the WP-604A corpus runner and record one machine-shaped result."""

import argparse
import json
import os
import platform
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
TARGET = "pnga_performance_runner"
GUI_TEST = "gui_trace_inspector_performance_tests"
GUI_TARGET = "pnga_gui_trace_inspector_performance_tests"


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


def find_binary(preset):
    base = ROOT / "build" / preset / "tests" / "performance" / TARGET
    for candidate in (base, base.with_suffix(".exe")):
        if candidate.is_file():
            return candidate
    raise SystemExit(f"performance runner not found: {base}")


def has_ctest_entry(preset, name, environment):
    result = run(
        ["ctest", "--preset", preset, "-N", "-R", f"^{name}$"],
        environment,
        capture=True,
    )
    return f"{name}" in result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("dev",), default="dev")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/performance/wp-604a-latest.json"),
        help="generated record path relative to the repository root",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    if not args.skip_build:
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

    binary = find_binary(args.preset)
    runner = run([str(binary)], environment, capture=True)
    try:
        measurement = json.loads(runner.stdout)
    except json.JSONDecodeError as error:
        raise SystemExit(f"performance runner emitted invalid JSON: {error}") from error

    ui_status = "not-configured"
    if has_ctest_entry(args.preset, GUI_TEST, environment):
        run(
            [
                "cmake",
                "--build",
                "--preset",
                args.preset,
                "--target",
                GUI_TARGET,
                "--parallel",
                str(args.jobs),
            ],
            environment,
        )
        run(
            ["ctest", "--preset", args.preset, "-R", f"^{GUI_TEST}$", "--output-on-failure"],
            environment,
        )
        ui_status = "passed"

    record = {
        "schema": "pnga-performance-record-v1",
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "runner": measurement,
        "ui": {"scenario": GUI_TEST, "status": ui_status},
    }
    output = args.output if args.output.is_absolute() else ROOT / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(record, ensure_ascii=True, separators=(",", ":")) + "\n")
    print(f"performance corpus: PASS record={output}")


if __name__ == "__main__":
    main()
