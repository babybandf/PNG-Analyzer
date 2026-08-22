#!/usr/bin/env python3
"""Run the reproducible WP-605C release-candidate audit."""

import argparse
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
REQUIRED_FILES = (
    "README.md",
    "CONTRIBUTING.md",
    "SECURITY.md",
    "LICENSE",
    "vcpkg.json",
    "cmake/dependencies.lock.json",
    "packaging/CPackOptions.cmake",
    "docs/user-guide.md",
    "docs/development/trace-semantics.md",
    "docs/architecture/png-analyzer-current-development-plan-2026-08-22.md",
)
LOCAL_LINK = re.compile(r"\]\((?!https?://|mailto:)([^)#]+)")


def command_text(command):
    return " ".join(command)


def run_check(checks, check_id, command, environment):
    print("$ " + command_text(command), flush=True)
    result = subprocess.run(command, cwd=ROOT, env=environment)
    checks.append({"id": check_id, "status": "PASS" if result.returncode == 0 else "FAIL"})
    return result.returncode == 0


def check_files(checks):
    missing = [path for path in REQUIRED_FILES if not (ROOT / path).is_file()]
    checks.append(
        {
            "id": "required-docs-and-metadata",
            "status": "PASS" if not missing else "FAIL",
            "missing": missing,
        }
    )
    return not missing


def check_links(checks):
    broken = []
    for relative in REQUIRED_FILES:
        path = ROOT / relative
        if path.suffix != ".md" or not path.exists():
            continue
        for target in LOCAL_LINK.findall(path.read_text()):
            resolved = (path.parent / target).resolve()
            if not resolved.is_file():
                broken.append(f"{relative}: {target}")
    checks.append(
        {
            "id": "local-document-links",
            "status": "PASS" if not broken else "FAIL",
            "broken": broken,
        }
    )
    return not broken


def read_version():
    document = json.loads((ROOT / "vcpkg.json").read_text())
    version = document.get("version-string")
    if not isinstance(version, str) or not version:
        raise SystemExit("vcpkg.json version-string is missing")
    return version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--report",
        type=Path,
        default=Path("build/release/rc-audit-v1.json"),
        help="generated report path relative to the repository root",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    checks = []
    python = sys.executable
    run_check(checks, "layout-audit", [python, "scripts/verify_repository_layout.py"], environment)
    run_check(checks, "dependency-audit", [python, "scripts/verify_dependencies.py"], environment)
    check_files(checks)
    check_links(checks)

    for preset in ("dev", "asan"):
        run_check(
            checks,
            f"{preset}-build",
            ["cmake", "--build", "--preset", preset, "--parallel", str(args.jobs)],
            environment,
        )
        run_check(
            checks,
            f"{preset}-ctest",
            ["ctest", "--preset", preset, "--output-on-failure"],
            environment,
        )

    run_check(
        checks,
        "performance-threshold-gate",
        [python, "scripts/run_performance_corpus.py", "--enforce-thresholds"],
        environment,
    )
    run_check(
        checks,
        "portable-package-smoke",
        [python, "scripts/run_package_smoke.py"],
        environment,
    )

    version = read_version()
    cli = ROOT / "build" / "release" / "apps" / "pnga-cli" / ("pnga.exe" if os.name == "nt" else "pnga")
    version_ok = False
    if cli.is_file():
        result = subprocess.run([str(cli), "--version"], cwd=ROOT, env=environment,
                                capture_output=True, text=True)
        version_ok = result.returncode == 0 and result.stdout.strip() == f"pnga {version}"
    checks.append({"id": "release-version", "status": "PASS" if version_ok else "FAIL"})

    status = "PASS" if all(check["status"] == "PASS" for check in checks) else "FAIL"
    report = {
        "schema": "pnga-release-candidate-audit-v1",
        "status": status,
        "recommended_tag": f"v{version}-rc1",
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
        },
        "checks": checks,
    }
    output = args.report if args.report.is_absolute() else ROOT / args.report
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=True, separators=(",", ":")) + "\n")
    print(f"release candidate audit: {status} report={output} tag={report['recommended_tag']}")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
