#!/usr/bin/env python3
"""WP-5U12F GUI gate runner.

Builds the focused product-gate target and the WP-607C corpus, validates the
corpus manifest contract, runs the real three-page Compression inspector gate
under the offscreen platform, captures the plan-pinned 22-baseline matrix
under `build/gui-gate/wp-5u12/captures`, hashes every artifact and writes the
machine-readable evidence record (schema `pnga-wp5u12-gui-evidence-v1`).

The runner refuses NOT_CONFIGURED builds, missing corpus cases, unexpected
extra tracked baselines, missing tracked baselines in --compare-baselines
mode, and a dirty capture directory. It never writes into the tracked
baseline directory; approved baselines are copied there by the controller
only after image review.

Usage:
    python3 scripts/run_wp_5u12_gui_gate.py --preset dev --jobs 4 \
        --output build/gui-gate/wp-5u12/evidence.json \
        --capture-dir build/gui-gate/wp-5u12/captures
    python3 scripts/run_wp_5u12_gui_gate.py ... --compare-baselines
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

TARGET = "pnga_gui_compression_inspector_product_gate_tests"
GENERATOR_TARGET = "pnga_generate_wp607c_corpus"
MANIFEST = "tests/corpus/manifest.yaml"
MANIFEST_VERIFIER = "scripts/verify_wp607c_manifest.py"
BASELINE_DIR = Path("tests/gui/baselines/wp-5u12")
SCHEMA = "pnga-wp5u12-gui-evidence-v1"

# The exact plan-pinned baseline matrix. Do not extend without product
# review (flow-ui §20.8 and the WP-5U12F plan).
EXPECTED_BASELINES = (
    "blocks-360-light.png",
    "blocks-480-light.png",
    "blocks-600-light.png",
    "huffman-360-light.png",
    "huffman-480-light.png",
    "huffman-600-light.png",
    "decode-trace-360-light.png",
    "decode-trace-480-light.png",
    "decode-trace-600-light.png",
    "blocks-360-dark.png",
    "huffman-360-dark.png",
    "decode-trace-360-dark.png",
    "blocks-480-dark.png",
    "huffman-480-dark.png",
    "decode-trace-480-dark.png",
    "huffman-stored-360-light.png",
    "loading-360-light.png",
    "partial-error-360-light.png",
    "partial-error-480-light.png",
    "blocks-current-selection-480-light.png",
    "decode-trace-current-selection-480-light.png",
    "cross-idat-details-480-light.png",
)


def run(command, *, env=None, capture=False):
    print("$ " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=str(ROOT), check=False, text=True,
                          env=env, capture_output=capture)


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def serialize_evidence(record):
    """Compact sorted-key JSON, ASCII escaping, one trailing LF."""
    return json.dumps(record, sort_keys=True, ensure_ascii=True,
                      separators=(",", ":")) + "\n"


def absolute_path_strings(value):
    """Every string anywhere in `value` shaped like an absolute path."""
    offenders = []
    if isinstance(value, str):
        if value.startswith("/") or re.match(r"^[A-Za-z]:[\\/]", value):
            offenders.append(value)
    elif isinstance(value, dict):
        for entry in value.values():
            offenders.extend(absolute_path_strings(entry))
    elif isinstance(value, (list, tuple)):
        for entry in value:
            offenders.extend(absolute_path_strings(entry))
    return offenders


def write_evidence(path, record):
    offenders = absolute_path_strings(record)
    if offenders:
        raise SystemExit(
            f"{SCHEMA}: evidence record would contain absolute path(s) "
            f"{offenders[:3]}; refusing to write")
    output = path if path.is_absolute() else ROOT / path
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".json.tmp")
    temporary.write_text(serialize_evidence(record), encoding="utf-8")
    os.replace(temporary, output)
    return output


def target_configured(preset):
    result = run(["cmake", "--build", "--preset", preset, "--target", "help"],
                 capture=True)
    return result.returncode == 0 and TARGET in result.stdout


def commit_sha():
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                            check=True, capture_output=True, text=True)
    return result.stdout.strip()


def corpus_revision(build_dir):
    catalog = json.loads((ROOT / build_dir / "tests/corpus/wp-607c" /
                          "index.json").read_text(encoding="utf-8"))
    return catalog["corpus_revision"], catalog


def missing_cases(catalog):
    """Corpus cases the gate depends on (plan Global Constraints)."""
    required = {
        "trace-dynamic-overlap-repeats",
        "trace-multiblock-bfinal",
        "trace-stored-literals",
        "idat-split-token",
        "error-truncated-token",
        "error-reserved-btype",
    }
    present = {record["id"] for record in catalog.get("cases", [])}
    return sorted(required - present)


def check_capture_dir(capture_dir):
    target = capture_dir if capture_dir.is_absolute() else ROOT / capture_dir
    if target.exists() and any(target.iterdir()):
        print(f"refusing dirty capture directory {target}: remove it or "
              "pass a fresh --capture-dir", file=sys.stderr)
        raise SystemExit(2)
    return target


def baseline_state():
    directory = ROOT / BASELINE_DIR
    if not directory.is_dir():
        return sorted(EXPECTED_BASELINES), []
    present = sorted(path.name for path in directory.glob("*.png"))
    missing = sorted(set(EXPECTED_BASELINES) - set(present))
    extra = sorted(set(present) - set(EXPECTED_BASELINES))
    return missing, extra


def plan_commands(preset, jobs, build_dir):
    generator = f"{build_dir}/tests/corpus/pnga_generate_wp607c_corpus"
    binary = f"{build_dir}/tests/gui/{TARGET}"
    commands = [
        ["cmake", "--build", "--preset", preset, "--target",
         GENERATOR_TARGET, TARGET, "--parallel", str(jobs)],
        [generator, "--output", f"{build_dir}/tests/corpus/wp-607c"],
        ["python3", MANIFEST_VERIFIER,
         "--manifest", MANIFEST,
         "--catalog", f"{build_dir}/tests/corpus/wp-607c/index.json",
         "--build-dir", build_dir],
        [binary],
    ]
    return commands, binary


def binary_environment(capture_dir, results_path, env_path, compare):
    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    environment["PNGA_WP5U12_CAPTURE_DIR"] = str(capture_dir)
    environment["PNGA_WP5U12_RESULTS_JSON"] = str(results_path)
    environment["PNGA_WP5U12_ENV_JSON"] = str(env_path)
    environment["PNGA_WP5U12_BASELINE_DIR"] = str(ROOT / BASELINE_DIR)
    if compare:
        environment["PNGA_WP5U12_COMPARE_BASELINES"] = "1"
    return environment


def host_facts():
    return {
        "os": platform.system(),
        "os_release": platform.release(),
        "architecture": platform.machine(),
    }


def build_gate_record(preset, commands, results, env_facts, catalog,
                      case_records, gates, status, compare):
    cases = []
    for record in case_records:
        case = {
            "id": record.get("id"),
            "page": record.get("page"),
            "width": record.get("width"),
            "theme": record.get("theme"),
            "fixture": record.get("fixture"),
            "capture": record.get("capture"),
            "capture_sha256": record.get("capture_sha256"),
            "result": record.get("result", "captured"),
        }
        if compare:
            case["baseline"] = f"{BASELINE_DIR.as_posix()}/{record['id']}.png"
        cases.append(case)
    return {
        "schema": SCHEMA,
        "work_package": "WP-5U12F",
        "status": status,
        "commit": commit_sha(),
        "time_utc": datetime.datetime.now(datetime.timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%SZ"),
        "host": host_facts(),
        "preset": preset,
        "qt": env_facts.get("qt"),
        "platform_plugin": env_facts.get("platform_plugin"),
        "device_pixel_ratio": env_facts.get("device_pixel_ratio"),
        "logical_dpi": env_facts.get("logical_dpi"),
        "themes": sorted({case["theme"] for case in cases
                          if case["theme"]}),
        "fixture_manifest_sha256": sha256_file(ROOT / MANIFEST),
        "corpus_revision": catalog["corpus_revision"],
        "commands": [{"command": command,
                      "exit": result.returncode if result is not None else None}
                     for command, result in zip(commands, results)],
        "cases": cases,
        "gates": gates,
    }


def run_gate(preset, jobs, output, capture_dir, compare):
    build_dir = f"build/{preset}"
    results_path = (ROOT / build_dir / "gui-gate/wp-5u12/test-results.json")
    env_path = (ROOT / build_dir / "gui-gate/wp-5u12/qt-env.json")

    commands, binary = plan_commands(preset, jobs, build_dir)
    run_dir = ROOT / build_dir / "gui-gate/wp-5u12"
    run_dir.mkdir(parents=True, exist_ok=True)

    if not target_configured(preset):
        record = {
            "schema": SCHEMA,
            "work_package": "WP-5U12F",
            "status": "NOT_CONFIGURED",
            "commit": commit_sha(),
            "host": host_facts(),
            "preset": preset,
            "tests": [TARGET],
        }
        written = write_evidence(output, record)
        print(f"WP-5U12 GUI gate: NOT_CONFIGURED record={written}")
        return 1

    physical_capture_dir = check_capture_dir(capture_dir)

    missing, extra = baseline_state()
    if compare:
        if missing or extra:
            record = {
                "schema": SCHEMA,
                "work_package": "WP-5U12F",
                "status": "FAIL",
                "commit": commit_sha(),
                "host": host_facts(),
                "preset": preset,
                "baseline_directory": BASELINE_DIR.as_posix(),
                "missing_baselines": missing,
                "unexpected_baselines": extra,
                "cases": [{"id": name[:-4], "result": "missing-baseline"}
                          for name in missing],
                "gates": {},
            }
            written = write_evidence(output, record)
            print(f"WP-5U12 GUI gate: FAIL "
                  f"({len(missing)} missing, {len(extra)} unexpected "
                  f"baselines) record={written}")
            return 1
    elif extra:
        print(f"refusing unexpected tracked baselines {extra}; the pinned "
              "matrix is exactly 22 names (WP-5U12F plan)", file=sys.stderr)
        return 2

    results = []
    failed = None
    for index, command in enumerate(commands):
        environment = binary_environment(physical_capture_dir, results_path,
                                         env_path, compare) \
            if index == len(commands) - 1 else None
        result = run(command, env=environment, capture=(environment is None))
        if environment is not None and result.stdout:
            sys.stdout.write(result.stdout)
        if result.returncode != 0 and result.stderr:
            sys.stderr.write(result.stderr)
        results.append(result)
        if result.returncode != 0:
            failed = len(results) - 1
            break

    env_facts = {}
    if env_path.is_file():
        env_facts = json.loads(env_path.read_text(encoding="utf-8"))
    catalog = {"corpus_revision": "0" * 64}
    try:
        _, catalog = corpus_revision(build_dir)
    except (OSError, ValueError):
        pass

    case_records = []
    gates = {}
    if results_path.is_file():
        payload = json.loads(results_path.read_text(encoding="utf-8"))
        case_records = payload.get("cases", [])
        gates = payload.get("gates", {})
        for record in case_records:
            capture_name = f"{record.get('id')}.png"
            capture_file = physical_capture_dir / capture_name
            if capture_file.is_file():
                record["capture"] = os.path.relpath(capture_file, ROOT)
                record["capture_sha256"] = sha256_of(capture_file)
                record.setdefault("result", "captured")
            else:
                record["result"] = "missing-capture"

    problems = []
    if failed is not None:
        problems.append(f"command {failed} failed")
    captured_names = sorted(str(record.get("id")) for record in case_records)
    expected_names = sorted(name[:-4] for name in EXPECTED_BASELINES)
    if captured_names != expected_names:
        missing_captures = sorted(set(expected_names) - set(captured_names))
        extra_captures = sorted(set(captured_names) - set(expected_names))
        problems.append(
            f"case matrix mismatch (missing={missing_captures}, "
            f"extra={extra_captures})")
    for record in case_records:
        if record.get("result") == "missing-capture":
            problems.append(f"missing capture for {record.get('id')}")
        if compare and record.get("compare") == "missing-baseline":
            record["result"] = "missing-baseline"
            problems.append(f"missing baseline for {record.get('id')}")
        if compare and record.get("compare") == "fail":
            record["result"] = "fail"
            problems.append(f"baseline divergence for {record.get('id')}")
    failing_gates = sorted(name for name, value in gates.items()
                           if value not in ("pass", "pending"))
    if failing_gates:
        problems.append(f"failing gates: {failing_gates}")
    if compare and any(record.get("result") in (None, "captured")
                       for record in case_records):
        problems.append("compare mode left cases without a comparison result")

    status = "FAIL" if problems else "PASS"
    record = build_gate_record(preset, commands, results, env_facts, catalog,
                               case_records, gates, status, compare)
    written = write_evidence(output, record)
    if problems:
        for problem in problems:
            print(f"WP-5U12 GUI gate: {problem}", file=sys.stderr)
        print(f"WP-5U12 GUI gate: FAIL record={written}")
        return 1
    print(f"WP-5U12 GUI gate: PASS ({len(record['cases'])} captures, "
          f"corpus revision {record['corpus_revision'][:12]}…, "
          f"record={written})")
    return 0


def sha256_of(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="dev")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--output",
                        default="build/gui-gate/wp-5u12/evidence.json")
    parser.add_argument("--capture-dir",
                        default="build/gui-gate/wp-5u12/captures")
    parser.add_argument("--compare-baselines", action="store_true",
                        help="capture and compare against the tracked "
                             "baselines in tests/gui/baselines/wp-5u12")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    return run_gate(args.preset, args.jobs, Path(args.output),
                    Path(args.capture_dir), args.compare_baselines)


if __name__ == "__main__":
    sys.exit(main())
