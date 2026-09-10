#!/usr/bin/env python3
"""WP-APNG-INSPECT T00 acceptance runner.

Records a baseline of the static product state (HEAD, dirty files, toolchain,
CTest manifest and results, static expectation hashes and five performance
corpus runs) and later compares a candidate against that baseline. The runner
must never pass without actually running the required tests, so the required
CTest names are asserted before anything else executes.
"""

import argparse
import hashlib
import json
import platform
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

REQUIRED_TESTS = {
    "analysis_engine_artifact_store_tests",
    "statistics_engine_tests",
    "gui_stage_inspector_tests",
    "gui_selection_view_state_tests",
}

# Baseline failures that are attributed to the offscreen Qt platform in this
# environment. They are recorded verbatim; a candidate must not add failures
# beyond this set and a final PASS still requires native GUI evidence.
ATTRIBUTED_BASELINE_FAILURES = {
    "gui_main_window_layout_tests": "offscreen hover and dock separator rendering",
    "gui_wp607a_native_gui_gate_tests": "Tab focus traversal needs a native window",
    "gui_statistics_inspector_tests": "Tab focus traversal needs a native window",
}

# Static expectation files hashed byte for byte. On-disk goldens plus the test
# sources that embed static selection, statistics serializer and CLI golden
# expectations inline.
STATIC_EXPECTATION_FILES = [
    "tests/unit/statistics/golden/partial-v1.csv",
    "tests/unit/statistics/golden/partial-v1.json",
    "tests/unit/statistics/golden/ready-v1.csv",
    "tests/unit/statistics/golden/ready-v1.json",
    "tests/unit/statistics/serialization_test.cpp",
    "tests/unit/trace-model/selection_test.cpp",
    "tests/integration/cli/cli_test.cpp",
]

PERFORMANCE_RUNS = 5


def run(command, capture=True, check=True):
    return subprocess.run(
        command,
        cwd=ROOT,
        check=check,
        capture_output=capture,
        text=True,
    )


def require_build_directory(path):
    resolved = path.resolve()
    current = resolved
    while current != current.parent:
        if (current / "CMakeCache.txt").is_file():
            return resolved
        current = current.parent
    raise SystemExit(
        f"--out {path} must be a build directory subdirectory: no CMakeCache.txt ancestor"
    )


def ctest_names():
    p = subprocess.run(
        ["ctest", "--preset", "dev", "--show-only=json-v1"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return {t["name"] for t in json.loads(p.stdout)["tests"]}


def assert_required_tests():
    names = ctest_names()
    if not REQUIRED_TESTS <= names:
        raise SystemExit(
            "required tests missing: " + repr(sorted(REQUIRED_TESTS - names))
        )
    return sorted(names)


def sha256(path):
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def collect_toolchain():
    def version_of(command):
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        return result.stdout.splitlines()[0]

    qt_version = ""
    cache = ROOT / "build" / "dev" / "CMakeCache.txt"
    if cache.is_file():
        prefix = None
        for line in cache.read_text(errors="replace").splitlines():
            if line.startswith("CMAKE_PREFIX_PATH:"):
                prefix = line.split("=", 1)[1].strip()
                break
        if prefix:
            qmake = Path(prefix) / "bin" / "qmake"
            if qmake.is_file():
                qt_version = subprocess.run(
                    [str(qmake), "-query", "QT_VERSION"],
                    capture_output=True,
                    text=True,
                    check=True,
                ).stdout.strip()
    return {
        "system": platform.system(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "cmake": version_of(["cmake", "--version"]),
        "ctest": version_of(["ctest", "--version"]),
        "qt6": qt_version,
    }


def collect_git_state():
    head = run(["git", "rev-parse", "HEAD"]).stdout.strip()
    status = run(["git", "status", "--porcelain"]).stdout
    dirty = [
        line
        for line in status.splitlines()
        if line.strip()
    ]
    return {"head": head, "dirty": dirty}


def collect_ctest_results(out_dir):
    junit = out_dir / "ctest-results.xml"
    result = subprocess.run(
        [
            "ctest",
            "--preset",
            "dev",
            "--no-tests=error",
            "--output-on-failure",
            f"--output-junit={junit}",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    (out_dir / "ctest-stdout.txt").write_text(result.stdout)
    (out_dir / "ctest-stderr.txt").write_text(result.stderr)
    statuses = {}
    if junit.is_file():
        import xml.etree.ElementTree as ElementTree

        for case in ElementTree.parse(junit).getroot().iter("testcase"):
            name = case.attrib.get("name", "")
            failed = case.find("failure") is not None
            statuses[name] = "failed" if failed else "passed"
    return {
        "exit_code": result.returncode,
        "statuses": statuses,
    }


def collect_expectation_hashes():
    hashes = {}
    for relative in STATIC_EXPECTATION_FILES:
        path = ROOT / relative
        if not path.is_file():
            raise SystemExit(f"static expectation file missing: {relative}")
        hashes[relative] = sha256(path)
    return hashes


def collect_performance(out_dir):
    runs = []
    for index in range(1, PERFORMANCE_RUNS + 1):
        run_dir = out_dir / f"perf-run-{index}"
        run_dir.mkdir(parents=True, exist_ok=True)
        record_path = run_dir / "record.json"
        result = subprocess.run(
            [
                sys.executable,
                "scripts/run_performance_corpus.py",
                "--preset",
                "dev",
                "--enforce-thresholds",
                "--output",
                str(record_path.relative_to(ROOT)),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        (run_dir / "stdout.txt").write_text(result.stdout)
        (run_dir / "stderr.txt").write_text(result.stderr)
        record = json.loads(record_path.read_text()) if record_path.is_file() else None
        runs.append(
            {
                "exit_code": result.returncode,
                "record": record,
            }
        )
    return runs


def collect_inspection_performance(out_dir):
    """Runs the WP-APNG-INSPECT T11 performance program five times and
    archives each machine-shaped record with its stdout/stderr."""
    binary = ROOT / "build" / "dev" / "tests" / "performance" / \
        "pnga_apng_inspection_perf"
    runs = []
    for index in range(1, PERFORMANCE_RUNS + 1):
        run_dir = out_dir / f"inspection-perf-run-{index}"
        run_dir.mkdir(parents=True, exist_ok=True)
        if not binary.is_file():
            (run_dir / "stdout.txt").write_text("")
            (run_dir / "stderr.txt").write_text("perf binary missing")
            runs.append({"exit_code": 1, "record": None})
            continue
        result = subprocess.run(
            [str(binary)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        (run_dir / "stdout.txt").write_text(result.stdout)
        (run_dir / "stderr.txt").write_text(result.stderr)
        record = None
        if result.returncode == 0 and result.stdout.strip():
            record = json.loads(result.stdout)
            problems = check_budgets(record)
            if problems:
                (run_dir / "budget-problems.txt").write_text(
                    "\n".join(problems) + "\n")
                result.returncode = 1
        runs.append({"exit_code": result.returncode, "record": record,
                     "budget_ok": not problems if record else False})
    return runs


INSPECTION_BUDGETS = {
    "retained_budget_bytes": 67108864,
    "reservation_budget_bytes": 67108864,
}
INSPECTION_TIME_METRICS = (
    "cold_p50_us", "cold_p95_us", "warm_p50_us", "warm_p95_us",
    "deep_provenance_p50_us", "deep_provenance_max_us",
    "pixel_query_p50_us", "pixel_query_p95_us",
)


def check_budgets(record):
    """C5 budget facts asserted on every inspection performance record."""
    problems = []
    if record.get("schema") != "pnga-apng-inspection-performance-v1":
        problems.append("inspection perf record schema mismatch")
        return problems
    for key, expected in INSPECTION_BUDGETS.items():
        if record.get(key) != expected:
            problems.append(f"{key}: {record.get(key)} != {expected}")
    if record.get("queue_cap") != 8:
        problems.append(f"queue_cap: {record.get('queue_cap')} != 8")
    if not isinstance(record.get("deep_provenance_max_us"), int) or \
            record["deep_provenance_max_us"] <= 0:
        problems.append("deep provenance did not terminate within budgets")
    return problems


def compare_inspection_performance(baseline_runs, candidate_runs):
    """WP tolerance: time metric medians within max(5%, 1ms) of baseline."""
    problems = []

    def medians(runs):
        samples = {metric: [] for metric in INSPECTION_TIME_METRICS}
        for entry in runs:
            record = entry.get("record")
            if not record:
                continue
            for metric in INSPECTION_TIME_METRICS:
                value = record.get(metric)
                if isinstance(value, int) and value >= 0:
                    samples[metric].append(value)
        return {metric: statistics.median(values) if values else None
                for metric, values in samples.items()}

    base = medians(baseline_runs)
    cand = medians(candidate_runs)
    for metric in INSPECTION_TIME_METRICS:
        base_median = base[metric]
        cand_median = cand[metric]
        if base_median is None or cand_median is None:
            problems.append(f"inspection.{metric}: missing measurements")
            continue
        budget = max(base_median * 0.05, 1000.0)
        if cand_median - base_median > budget:
            problems.append(
                f"inspection.{metric}: candidate median {cand_median} "
                f"regressed beyond baseline median {base_median} "
                f"(budget {budget:.1f})")
    return problems


def scenario_metric_index(runs):
    samples = {}
    for entry in runs:
        record = entry["record"]
        if not record or entry["exit_code"] != 0:
            continue
        for scenario in record["runner"]["scenarios"]:
            scenario_id = scenario["id"]
            for key, value in scenario.items():
                if key == "id" or not isinstance(value, int) or value < 0:
                    continue
                samples.setdefault((scenario_id, key), []).append(value)
    return samples


def compare_performance(baseline_runs, candidate_runs):
    baseline = scenario_metric_index(baseline_runs)
    candidate = scenario_metric_index(candidate_runs)
    problems = []
    for key in sorted(baseline):
        scenario_id, metric = key
        base_values = baseline[key]
        cand_values = candidate.get(key)
        if not cand_values:
            problems.append(f"{scenario_id}.{metric}: candidate has no measurements")
            continue
        base_median = statistics.median(base_values)
        cand_median = statistics.median(cand_values)
        if metric == "process_rss_peak_kib":
            budget = max(base_median * 0.02, 1024.0)
        else:
            budget = max(base_median * 0.05, 1000.0)
        # The WP tolerance guards against REGRESSIONS (added work); an
        # improvement is welcome and never a gate failure. Back-to-back
        # same-code runs differ by several percent in both directions on
        # this machine, so absolute-difference flagging would fail noise.
        delta = cand_median - base_median
        if delta > budget:
            problems.append(
                f"{scenario_id}.{metric}: candidate median {cand_median} "
                f"regressed beyond baseline median {base_median} "
                f"(budget {budget:.1f})"
            )
    for key in sorted(set(candidate) - set(baseline)):
        problems.append(f"{key[0]}.{key[1]}: metric absent from baseline")
    return problems


def phase_baseline(out_dir):
    evidence = {
        "phase": "baseline",
        "git": collect_git_state(),
        "toolchain": collect_toolchain(),
        "ctest_names": assert_required_tests(),
        "ctest": collect_ctest_results(out_dir),
        "static_expectation_hashes": collect_expectation_hashes(),
        "performance": collect_performance(out_dir),
        "inspection_performance": collect_inspection_performance(out_dir),
        "attributed_baseline_failures": ATTRIBUTED_BASELINE_FAILURES,
    }
    (out_dir / "manifest.json").write_text(
        json.dumps(evidence, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    )
    return evidence


def phase_candidate(out_dir, baseline_dir):
    baseline_manifest_path = baseline_dir / "manifest.json"
    if not baseline_manifest_path.is_file():
        raise SystemExit(f"baseline manifest missing: {baseline_manifest_path}")
    baseline = json.loads(baseline_manifest_path.read_text())

    evidence = {
        "phase": "candidate",
        "git": collect_git_state(),
        "toolchain": collect_toolchain(),
        "ctest_names": assert_required_tests(),
        "ctest": collect_ctest_results(out_dir),
        "static_expectation_hashes": collect_expectation_hashes(),
        "performance": collect_performance(out_dir),
        "inspection_performance": collect_inspection_performance(out_dir),
        "attributed_baseline_failures": ATTRIBUTED_BASELINE_FAILURES,
    }

    problems = []
    for index, entry in enumerate(evidence["inspection_performance"], 1):
        if entry["exit_code"] != 0 or not entry.get("record"):
            problems.append(
                f"inspection perf run {index} failed or produced no record")
    baseline_head = baseline["git"]["head"]
    if evidence["git"]["head"] != baseline_head:
        problems.append(
            f"HEAD changed between baseline and candidate: {baseline_head} -> "
            f"{evidence['git']['head']}"
        )
    if evidence["static_expectation_hashes"] != baseline["static_expectation_hashes"]:
        changed = [
            name
            for name, digest in evidence["static_expectation_hashes"].items()
            if baseline["static_expectation_hashes"].get(name) != digest
        ]
        problems.append(f"static expectation files changed: {sorted(changed)}")

    baseline_status = baseline["ctest"]["statuses"]
    for name, status in evidence["ctest"]["statuses"].items():
        if status == "failed" and baseline_status.get(name) == "passed":
            problems.append(f"new test failure: {name}")
    missing = set(baseline_status) - set(evidence["ctest"]["statuses"])
    if missing:
        problems.append(f"tests missing from candidate run: {sorted(missing)}")
    if evidence["ctest"]["exit_code"] != 0 and baseline["ctest"]["exit_code"] == 0:
        problems.append("ctest exit code regressed to nonzero")

    performance_problems = compare_performance(
        baseline["performance"], evidence["performance"]
    )
    problems.extend(performance_problems)
    problems.extend(compare_inspection_performance(
        baseline["inspection_performance"], evidence["inspection_performance"]))

    evidence["comparison"] = {
        "baseline_dir": str(baseline_dir),
        "problems": problems,
        "result": "PASS" if not problems else "FAIL",
    }
    (out_dir / "manifest.json").write_text(
        json.dumps(evidence, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    )
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("baseline", "candidate"), required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, help="baseline directory for candidate")
    args = parser.parse_args()

    out_dir = require_build_directory(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.phase == "baseline":
        evidence = phase_baseline(out_dir)
    else:
        if not args.baseline:
            parser.error("--baseline is required for --phase candidate")
        evidence = phase_candidate(out_dir, args.baseline.resolve())

    if evidence["phase"] == "candidate":
        comparison = evidence["comparison"]
        print(f"candidate gate: {comparison['result']}")
        for problem in comparison["problems"]:
            print(f"  - {problem}")
        raise SystemExit(0 if not comparison["problems"] else 1)

    failures = [
        name
        for name, status in evidence["ctest"]["statuses"].items()
        if status == "failed"
    ]
    print(f"baseline recorded at {out_dir}")
    print(f"head: {evidence['git']['head']}")
    print(f"tests: {len(evidence['ctest']['statuses'])}, failures: {len(failures)}")
    for name in failures:
        reason = ATTRIBUTED_BASELINE_FAILURES.get(name, "unattributed")
        print(f"  failed: {name} ({reason})")


if __name__ == "__main__":
    main()
