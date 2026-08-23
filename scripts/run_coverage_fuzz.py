#!/usr/bin/env python3
"""Run the optional LLVM libFuzzer harness or record missing runtime support."""

import argparse
import json
import os
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PRESET = "coverage-fuzz"
TARGET = "pnga_coverage_fuzz"


def write_report(path, status, reason=None, seconds=None):
    report = {
        "schema": "pnga-coverage-fuzz-evidence-v1",
        "status": status,
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
        },
        "target": TARGET,
        "duration_seconds": seconds,
        "reason": reason,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, separators=(",", ":")) + "\n")
    print(f"coverage fuzz: {status} report={path}")


def probe(compiler):
    with tempfile.TemporaryDirectory(prefix="pnga-fuzzer-probe-") as directory:
        source = Path(directory) / "probe.cpp"
        binary = Path(directory) / "probe"
        source.write_text(
            "#include <cstddef>\n"
            "#include <cstdint>\n"
            'extern "C" int LLVMFuzzerTestOneInput('
            "const std::uint8_t*, std::size_t) { return 0; }\n"
        )
        result = subprocess.run(
            [compiler, "-fsanitize=fuzzer", str(source), "-o", str(binary)],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode != 0:
            lines = result.stderr.strip().splitlines()
            if not lines:
                return "probe failed"
            # Keep enough linker context to make a NOT_CONFIGURED report
            # actionable while avoiding compiler-specific absolute paths.
            return " | ".join(lines[-4:])
    return None


def run(command, environment):
    print("$ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=environment, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument(
        "--output", type=Path,
        default=Path("build/coverage-fuzz/evidence.json"),
    )
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error("--seconds must be positive")
    output = args.output if args.output.is_absolute() else ROOT / args.output
    compiler = os.environ.get("CXX") or shutil.which("c++")
    if not compiler:
        write_report(output, "NOT_CONFIGURED", "no C++ compiler found")
        return 0
    reason = probe(compiler)
    if reason is not None:
        write_report(output, "NOT_CONFIGURED", reason)
        return 0

    environment = os.environ.copy()
    run(["cmake", "--preset", PRESET], environment)
    run(["cmake", "--build", "--preset", PRESET, "--target", TARGET], environment)
    binary = ROOT / "build" / PRESET / "tests" / "fuzz" / TARGET
    corpus = ROOT / "build" / PRESET / "corpus"
    corpus.mkdir(parents=True, exist_ok=True)
    (corpus / "empty").write_bytes(b"")
    (corpus / "png-signature").write_bytes(b"\x89PNG\r\n\x1a\n")
    run(
        [
            str(binary),
            f"-max_total_time={args.seconds}",
            "-print_final_stats=1",
            str(corpus),
        ],
        environment,
    )
    write_report(output, "PASS", seconds=args.seconds)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
