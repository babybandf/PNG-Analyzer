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
DEFAULT_THRESHOLDS = Path("tests/performance/thresholds-v1.json")


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


def check_thresholds(measurement, threshold_path):
    thresholds = json.loads(threshold_path.read_text())
    if thresholds.get("schema") != "pnga-performance-thresholds-v1":
        raise SystemExit(f"unsupported threshold schema: {threshold_path}")
    if thresholds.get("record_schema") != "pnga-performance-record-v1":
        raise SystemExit(f"threshold record schema mismatch: {threshold_path}")
    expected = {entry["id"]: entry for entry in thresholds.get("scenarios", [])}
    failures = []
    seen = set()
    for scenario in measurement.get("scenarios", []):
        scenario_id = scenario.get("id")
        config = expected.get(scenario_id)
        if config is None:
            failures.append(f"{scenario_id}: missing threshold configuration")
            continue
        seen.add(scenario_id)
        for metric, maximum in config.get("max_us", {}).items():
            if not isinstance(maximum, int) or maximum < 0:
                failures.append(f"{scenario_id}.{metric}: invalid threshold")
                continue
            value = scenario.get(metric)
            if not isinstance(value, int) or value < 0:
                failures.append(f"{scenario_id}.{metric}: missing integer measurement")
            elif value > maximum:
                failures.append(
                    f"{scenario_id}.{metric}: {value}us > {maximum}us maximum"
                )
    for scenario_id in sorted(set(expected) - seen):
        failures.append(f"{scenario_id}: threshold has no measured scenario")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("dev",), default="dev")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument(
        "--enforce-thresholds",
        action="store_true",
        help="fail after recording when a fixed WP-604B maximum is exceeded",
    )
    parser.add_argument(
        "--thresholds",
        type=Path,
        default=DEFAULT_THRESHOLDS,
        help="threshold JSON relative to the repository root",
    )
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
    if args.enforce_thresholds:
        threshold_path = (
            args.thresholds
            if args.thresholds.is_absolute()
            else ROOT / args.thresholds
        )
        failures = check_thresholds(measurement, threshold_path)
        if failures:
            print(f"performance threshold gate: FAIL record={output}")
            for failure in failures:
                print(f"  - {failure}")
            raise SystemExit(1)
        print(
            "performance threshold gate: PASS "
            f"thresholds={threshold_path} record={output}"
        )
    else:
        print(f"performance corpus: PASS record={output}")


if __name__ == "__main__":
    main()
