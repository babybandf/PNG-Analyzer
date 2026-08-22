#!/usr/bin/env python3
"""Run the bounded fuzz smoke target through the ASan/UBSan gate.

The gate deliberately reuses the repository's CMake and CTest presets. The
two Catch2 tags are also replayed separately so a failure identifies the
regression family without requiring a generated corpus file.
"""

import argparse
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
FUZZ_TARGET = "pnga_fuzz_smoke_tests"
FUZZ_TEST = "fuzz_parser_stream_smoke_tests"
REPLAY_TAGS = ("[wp603a]", "[wp603b]")


def run(command, environment):
    print("$ " + " ".join(command), flush=True)
    completed = subprocess.run(command, cwd=ROOT, env=environment)
    if completed.returncode:
        raise SystemExit(completed.returncode)


def find_binary(preset):
    base = ROOT / "build" / preset / "tests" / "fuzz" / FUZZ_TARGET
    candidates = (base, base.with_suffix(".exe"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit(
        "sanitizer fuzz binary not found; expected one of: "
        + ", ".join(str(path) for path in candidates)
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--preset",
        choices=("asan",),
        default="asan",
        help="CMake/CTest preset (the sanitizer gate is intentionally ASan/UBSan only)",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=4,
        help="parallel build jobs (default: 4)",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="reuse an already-built sanitizer fuzz binary",
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
                FUZZ_TARGET,
                "--parallel",
                str(args.jobs),
            ],
            environment,
        )

    binary = find_binary(args.preset)
    for tag in REPLAY_TAGS:
        run([str(binary), tag, "--reporter", "console"], environment)

    run(
        [
            "ctest",
            "--preset",
            args.preset,
            "-R",
            f"^{FUZZ_TEST}$",
            "--output-on-failure",
        ],
        environment,
    )
    print(
        "sanitizer fuzz gate: PASS "
        f"({len(REPLAY_TAGS)} deterministic replays + CTest {FUZZ_TEST})"
    )


if __name__ == "__main__":
    main()
