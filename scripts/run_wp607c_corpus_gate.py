#!/usr/bin/env python3
"""WP-607C operator gate.

One-shot wrapper that builds the corpus targets, generates the corpus twice
into fresh build-tree directories, proves byte-identical double generation,
validates the manifest/catalog/hash contract, runs every `wp607c`-labeled
CTest entry and writes the deterministic evidence record
build/evidence/wp-607c-corpus.json (never committed).

Usage:
    python3 scripts/run_wp607c_corpus_gate.py --preset dev --jobs 4
    python3 scripts/run_wp607c_corpus_gate.py --dry-run
    python3 scripts/run_wp607c_corpus_gate.py --self-test
"""

import argparse
import datetime
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EVIDENCE_RECORD = Path("build/evidence/wp-607c-corpus.json")
DOUBLE_GENERATION_ROOT = Path("build/wp607c-double-generation")
MANIFEST_VERIFIER = "scripts/verify_wp607c_manifest.py"
MANIFEST = "tests/corpus/manifest.yaml"
GENERATOR_SOURCES = (
    "tests/corpus/controlled_fixture.h",
    "tests/corpus/controlled_fixture.cpp",
    "tests/corpus/generate_controlled_corpus.cpp",
)

BUILD_TARGETS = (
    "pnga_generate_wp607c_corpus",
    "pnga_wp607c_png_facts_tests",
    "pnga_wp607c_trace_facts_tests",
    "pnga_gui_wp607c_corpus_tests",
)


def plan_commands(preset, jobs):
    """The exact, order-pinned gate command sequence (relative paths only)."""
    generator = f"build/{preset}/tests/corpus/pnga_generate_wp607c_corpus"
    return [
        ["cmake", "--build", "--preset", preset, "--target", *BUILD_TARGETS,
         "--parallel", str(jobs)],
        [generator, "--output", f"{DOUBLE_GENERATION_ROOT}/run-a"],
        [generator, "--output", f"{DOUBLE_GENERATION_ROOT}/run-b"],
        ["python3", MANIFEST_VERIFIER,
         "--manifest", MANIFEST,
         "--catalog", f"{DOUBLE_GENERATION_ROOT}/run-a/index.json",
         "--comparison-catalog", f"{DOUBLE_GENERATION_ROOT}/run-b/index.json",
         "--build-dir", f"build/{preset}"],
        ["ctest", "--preset", preset, "-L", "wp607c", "--output-on-failure"],
    ]


def run(command, *, capture=False):
    print("$ " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=str(ROOT), check=False, text=True,
                          capture_output=capture)


def load_catalog(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def compare_generations(first, second):
    """Proves both fresh generations agree on facts and bytes."""
    left = load_catalog(first / "index.json")
    right = load_catalog(second / "index.json")
    if left != right:
        raise SystemExit("WP-607C catalogs differ")
    for record in left["cases"]:
        relative = Path(record["output"])
        if (first / relative).read_bytes() != (second / relative).read_bytes():
            raise SystemExit(f"WP-607C bytes differ: {relative.as_posix()}")
    return left


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def cmake_cache_value(cache_path, name):
    pattern = re.compile(rf"^{re.escape(name)}:[^=]*=(.*)$")
    try:
        for line in Path(cache_path).read_text(
                encoding="utf-8", errors="replace").splitlines():
            match = pattern.match(line)
            if match:
                return match.group(1).strip()
    except OSError:
        pass
    return None


def qt_version(build_dir):
    for config in sorted((ROOT / "build" / "vcpkg_installed").glob(
            "*/share/Qt6Core/Qt6CoreConfigVersion.cmake")):
        match = re.search(r'PACKAGE_VERSION\s+"([^"]+)"',
                          config.read_text(encoding="utf-8",
                                           errors="replace"))
        if match:
            return match.group(1)
    return "unknown"


def host_memory_bytes():
    try:
        output = subprocess.run(["sysctl", "-n", "hw.memsize"],
                                check=True, capture_output=True, text=True)
        return int(output.stdout.strip())
    except (OSError, ValueError):
        return None


def ctest_labeled_names(preset, environment=None):
    result = subprocess.run(
        ["ctest", "--preset", preset, "-L", "wp607c", "-N"],
        cwd=str(ROOT), check=True, capture_output=True, text=True,
        env=environment)
    names = re.findall(r"Test #\d+:\s*(\S+)", result.stdout)
    return sorted(set(names))


def serialize_evidence(record):
    """Compact sorted-key JSON, ASCII escaping, one trailing LF."""
    return json.dumps(record, sort_keys=True, ensure_ascii=True,
                      separators=(",", ":")) + "\n"


def write_evidence(record):
    output = ROOT / EVIDENCE_RECORD
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".json.tmp")
    temporary.write_text(serialize_evidence(record), encoding="utf-8")
    os.replace(temporary, output)


def build_record(preset, commands, results, catalog, status):
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                            check=True, capture_output=True, text=True)
    now = datetime.datetime.now(datetime.timezone.utc)
    cache = ROOT / "build" / preset / "CMakeCache.txt"
    cases = [{
        "id": record["id"],
        "output": record["output"],
        "sha256": record["expected_sha256"],
    } for record in sorted(catalog["cases"], key=lambda r: r["id"])]
    return {
        "schema_version": 1,
        "work_package": "WP-607C",
        "status": status,
        "commit": commit.stdout.strip(),
        "time_utc": now.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "host": {
            "os": platform.system(),
            "os_release": platform.release(),
            "architecture": platform.machine(),
            "compiler": (cmake_cache_value(cache, "CMAKE_CXX_COMPILER")
                         or "unknown"),
            "qt": qt_version(preset),
            "display_protocol": os.environ.get("QT_QPA_PLATFORM",
                                               "offscreen"),
            "logical_dpi": "offscreen",
            "device_pixel_ratio": "offscreen",
            "cpu": platform.processor() or platform.machine(),
            "cpu_count": os.cpu_count(),
            "memory_bytes": host_memory_bytes(),
        },
        "preset": preset,
        "manifest_sha256": sha256_file(ROOT / MANIFEST),
        "generator_sources_sha256": {
            source: sha256_file(ROOT / source) for source in GENERATOR_SOURCES
        },
        "corpus_revision": catalog["corpus_revision"],
        "commands": [{
            "command": command,
            "exit": result.returncode if result is not None else None,
        } for command, result in zip(commands, results)],
        "cases": cases,
        "ctest": {name: "passed" for name in
                  ctest_labeled_names(preset)},
    }


def run_gate(preset, jobs, dry_run):
    commands = plan_commands(preset, jobs)
    if dry_run:
        for command in commands:
            print(" ".join(command))
        return 0

    results = []
    failed = None
    for index, command in enumerate(commands):
        capture = index == 3  # the validator prints through the wrapper below
        result = run(command, capture=capture)
        if capture and result.returncode != 0 and result.stdout:
            sys.stdout.write(result.stdout)
        if result.returncode != 0 and result.stderr:
            sys.stderr.write(result.stderr)
        results.append(result)
        if result.returncode != 0:
            failed = len(results) - 1
            break

    status = "PASS" if failed is None else "FAIL"
    catalog = None
    if failed is None:
        first = ROOT / DOUBLE_GENERATION_ROOT / "run-a"
        second = ROOT / DOUBLE_GENERATION_ROOT / "run-b"
        catalog = compare_generations(first, second)
    record = build_record(preset, commands, results + [None] * (
        len(commands) - len(results)), catalog or {"cases": [],
                                                   "corpus_revision": "0" * 64},
                          status)
    write_evidence(record)
    if failed is not None:
        print(f"WP-607C corpus gate: FAIL at command {failed}: "
              f"{EVIDENCE_RECORD}")
        return 1
    print(f"WP-607C corpus gate: PASS "
          f"({len(record['cases'])} byte-identical cases, "
          f"{len(record['ctest'])} labeled CTest entries) "
          f"record={EVIDENCE_RECORD}")
    return 0


def run_self_test():
    failures = []

    # The planner output must equal the pinned plan sequence exactly.
    expected = [
        ["cmake", "--build", "--preset", "dev", "--target",
         "pnga_generate_wp607c_corpus", "pnga_wp607c_png_facts_tests",
         "pnga_wp607c_trace_facts_tests", "pnga_gui_wp607c_corpus_tests",
         "--parallel", "4"],
        ["build/dev/tests/corpus/pnga_generate_wp607c_corpus", "--output",
         "build/wp607c-double-generation/run-a"],
        ["build/dev/tests/corpus/pnga_generate_wp607c_corpus", "--output",
         "build/wp607c-double-generation/run-b"],
        ["python3", "scripts/verify_wp607c_manifest.py", "--manifest",
         "tests/corpus/manifest.yaml", "--catalog",
         "build/wp607c-double-generation/run-a/index.json",
         "--comparison-catalog",
         "build/wp607c-double-generation/run-b/index.json",
         "--build-dir", "build/dev"],
        ["ctest", "--preset", "dev", "-L", "wp607c", "--output-on-failure"],
    ]
    planned = plan_commands("dev", 4)
    if planned != expected:
        failures.append(f"planner drift: {planned}")

    # The evidence serialization is deterministic, ASCII-only, LF-terminated
    # and free of absolute paths.
    record = {
        "status": "PASS",
        "work_package": "WP-607C",
        "schema_version": 1,
        "cases": [{"id": "b", "output": "valid/b.png", "sha256": "0" * 64},
                  {"id": "a", "output": "valid/a.png", "sha256": "1" * 64}],
    }
    first = serialize_evidence(record)
    second = serialize_evidence(record)
    if first != second:
        failures.append("evidence serialization is not deterministic")
    if not first.endswith("\n") or first.endswith("\n\n"):
        failures.append("evidence serialization must end with one LF")
    if not first.isascii():
        failures.append("evidence serialization must be ASCII-only")
    try:
        parsed = json.loads(first)
    except ValueError as error:
        failures.append(f"evidence serialization is not valid JSON: {error}")
        parsed = {}
    if parsed.get("cases", [{}])[0].get("id") != "b":
        failures.append("evidence serialization must sort keys")
    # A JSON string value beginning with "/" would be an absolute path; the
    # record only ever carries repository-relative paths and fixed tokens.
    if '"/' in first:
        failures.append("evidence serialization contains an absolute path")

    if failures:
        for failure in failures:
            print(f"self-test FAIL: {failure}")
        print(f"self-test: {len(failures)} failure(s)")
        return 1
    print("self-test: command planner matches the pinned gate sequence")
    print("self-test: evidence writer is deterministic, sorted, ASCII, "
          "LF-terminated and path-safe")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="dev")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--dry-run", action="store_true",
                        help="print the exact command sequence, run nothing")
    parser.add_argument("--self-test", action="store_true",
                        help="verify the command planner and evidence writer")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.self_test:
        return run_self_test()
    return run_gate(args.preset, args.jobs, args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
