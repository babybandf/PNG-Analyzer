#!/usr/bin/env python3
"""WP-00A bootstrap: provision a pinned vcpkg and validate the toolchain.

Responsibilities (project bootstrap spec §7.2, §8):
  * Check that git, cmake, ninja and a suitable Python are available.
  * Clone microsoft/vcpkg into ``.deps/vcpkg`` at a fixed commit and build the
    vcpkg tool with its own bootstrap script.
  * Never modify vcpkg.json, cmake/dependencies.lock.json, third_party/ or any
    source file. The only writable locations are ``.deps/``, ``vcpkg_installed/``
    and the build directories produced by CMake presets.

``--check-only`` prints an environment report and exits without provisioning.

Usage:
  python3 scripts/bootstrap.py [--qt-root /abs/path/to/Qt/6.11.2/<kit>] [--check-only]
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

VCPKG_REPOSITORY = "https://github.com/microsoft/vcpkg.git"
# vcpkg release 2026.07.29. Recorded alongside qt_ci_version in
# cmake/dependencies.lock.json.
VCPKG_COMMIT = "9e593bb18ea69cc5095e012465dcd675a822ed0d"
PYTHON_MIN = (3, 11)

_SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = _SCRIPT_DIR.parent
DEPS_DIR = REPO_ROOT / ".deps"
VCPKG_DIR = DEPS_DIR / "vcpkg"

# Directories this bootstrap may write to. Everything else is read-only.
_ALLOWED_WRITABLES = (DEPS_DIR, REPO_ROOT / "vcpkg_installed", REPO_ROOT / "build")


class Report:
    """Collect simple pass/fail/warn lines and a final exit status."""

    def __init__(self):
        self._errors = []
        self._warnings = []

    def ok(self, msg):
        print(f"  [ok]   {msg}")

    def warn(self, msg):
        print(f"  [warn] {msg}")
        self._warnings.append(msg)

    def error(self, msg):
        print(f"  [fail] {msg}")
        self._errors.append(msg)

    @property
    def clean(self):
        return not self._errors


def _tool(name):
    path = shutil.which(name)
    if path is None:
        return None, None
    try:
        version = subprocess.run([name, "--version"], capture_output=True,
                                 text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        version = ""
    return path, version.strip().splitlines()[0] if version.strip() else ""


def report_environment(rpt, qt_root):
    rpt.ok(f"repository root: {REPO_ROOT}")

    git, gitv = _tool("git")
    if git is None:
        rpt.error("git not found on PATH")
    else:
        rpt.ok(f"git: {gitv or git}")

    cmake, cmakev = _tool("cmake")
    if cmake is None:
        rpt.error("cmake not found on PATH (install CMake >= 3.28)")
    else:
        rpt.ok(f"cmake: {cmakev or cmake}")

    ninja, ninjav = _tool("ninja")
    if ninja is None:
        rpt.warn("ninja not found on PATH; configure will require an explicit generator")
    else:
        rpt.ok(f"ninja: {ninjav or ninja}")

    major, minor = sys.version_info[:2]
    if (major, minor) >= PYTHON_MIN:
        rpt.ok(f"python: {sys.version.split()[0]} (>= 3.11)")
    else:
        rpt.warn(
            f"python {sys.version.split()[0]} is below the project minimum 3.11; "
            "supported, but the smoke/verify scripts may be exercised under a newer Python in CI")

    if qt_root:
        p = Path(qt_root)
        rpt.ok(f"qt-root provided: {p} (used by the future WP-001 dev build, not deps-smoke)")
        if not p.is_dir():
            rpt.warn(f"qt-root '{qt_root}' is not an existing directory")

    if DEPS_DIR.exists() and VCPKG_DIR.exists():
        rpt.ok(f"vcpkg checkout present: {VCPKG_DIR}")
    else:
        rpt.warn("vcpkg checkout not provisioned yet (run without --check-only)")


def ensure_vcpkg(rpt):
    """Clone vcpkg at the pinned commit and build the tool. Write-only under .deps."""

    def run(cmd, cwd=None):
        print(f"  $ {subprocess.list2cmdline(cmd)}")
        subprocess.run(cmd, cwd=cwd, check=True)

    DEPS_DIR.mkdir(parents=True, exist_ok=True)
    if not VCPKG_DIR.exists():
        rpt.ok(f"cloning vcpkg repository into {DEPS_DIR.relative_to(REPO_ROOT)}")
        run(["git", "clone", "--no-checkout", VCPKG_REPOSITORY, str(VCPKG_DIR)])
    else:
        rpt.ok(f"vcpkg checkout already present at {VCPKG_DIR.relative_to(REPO_ROOT)}")

    # Pin the exact commit; this also covers the case of a previously-cloned copy.
    run(["git", "checkout", "--detach", VCPKG_COMMIT], cwd=VCPKG_DIR)

    bootstrap = "bootstrap-vcpkg.bat" if os.name == "nt" else "bootstrap-vcpkg.sh"
    rpt.ok(f"building vcpkg tool via {bootstrap}")
    run([str(VCPKG_DIR / bootstrap)], cwd=VCPKG_DIR)
    vcpkg_exe = VCPKG_DIR / ("vcpkg.exe" if os.name == "nt" else "vcpkg")
    if not vcpkg_exe.exists():
        rpt.error(f"vcpkg binary not produced at {vcpkg_exe}")
        return
    out = subprocess.run([str(vcpkg_exe), "version"], capture_output=True, text=True).stdout
    rpt.ok("vcpkg ready:\n" + "\n".join("    " + l for l in out.splitlines()[:3]))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--qt-root", help="absolute path to a Qt 6 kit (used by WP-001, optional here)")
    parser.add_argument("--check-only", action="store_true",
                        help="print an environment report and exit without provisioning")
    args = parser.parse_args()

    rpt = Report()
    print(f"bootstrap: environment report for {REPO_ROOT.name}")
    report_environment(rpt, args.qt_root)

    if args.check_only:
        print(f"\nbootstrap {rpt._warnings and 'WARNINGS' or ('OK' if rpt.clean else 'ERRORS')}")
        return 0 if rpt.clean else 2

    if rpt._errors:
        print("\nbootstrap ABORTED: fix missing required tools first")
        return 2

    print(f"\nbootstrap: provisioning pinned vcpkg ({VCPKG_COMMIT[:12]}…)")
    ensure_vcpkg(rpt)
    print(f"\nbootstrap {'OK' if rpt.clean else 'FAILED'}")
    return 0 if rpt.clean else 2


if __name__ == "__main__":
    sys.exit(main())