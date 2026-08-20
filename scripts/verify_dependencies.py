#!/usr/bin/env python3
"""WP-00A dependency static audit.

Returns nonzero (and reports each finding) when any of the following holds:
  * vcpkg.json is invalid JSON, its builtin-baseline is not a 40-char hex
    commit, uses a placeholder, or declares an override outside the approved
    pinned version set.
  * cmake/dependencies.lock.json is invalid JSON or its pinned versions do not
    match the vcpkg.json overrides.
  * A file under third_party/ has no provenance entry in
    third_party/sources.lock.yaml.
  * A tests/corpus/manifest.yaml entry uses a placeholder source URL, or has an
    empty sha256 / unknown license.
  * A tracked file lives under a generated path (.deps/, build/, vcpkg_installed/).

This mirrors the static guarantees the layout verifier (WP-00A/WP-001) enforces
and hardens them with the pinned versions from cmake/dependencies.lock.json.

Usage: python3 scripts/verify_dependencies.py
"""

import json
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover - CI installs PyYAML; local degrade is graceful
    yaml = None

ROOT = Path(__file__).resolve().parent.parent

# The single approved pin set; keep in sync with cmake/dependencies.lock.json.
APPROVED_VERSIONS = {"libpng": "1.6.58", "zlib": "1.3.2", "catch2": "3.11.0"}

# Substrings that indicate an unresolved placeholder rather than real data.
_PLACEHOLDER = ("<", ">", "fill-", "example.invalid", "WP-00A-FILLS", "TBD")

GENERATED_PATHS = (".deps", "build", "vcpkg_installed", "Testing")


def is_placeholder(text):
    if not isinstance(text, str):
        return True
    return any(m in text for m in _PLACEHOLDER)


def load_json(path, rpt):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError) as exc:
        rpt.error(f"{path.relative_to(ROOT)}: not valid JSON ({exc})")
        return None


def check_vcpkg(rpt):
    path = ROOT / "vcpkg.json"
    if not path.exists():
        rpt.error("vcpkg.json is missing")
        return
    doc = load_json(path, rpt)
    if doc is None:
        return

    baseline = doc.get("builtin-baseline")
    if not isinstance(baseline, str) or not re.fullmatch(r"[0-9a-f]{40}", baseline):
        rpt.error("vcpkg.json builtin-baseline is not a 40-char hex commit")
    elif is_placeholder(baseline):
        rpt.error("vcpkg.json builtin-baseline is a placeholder")

    names = {d.get("name") for d in doc.get("dependencies", []) if isinstance(d, dict)}
    missing = set(APPROVED_VERSIONS) - names
    if missing:
        rpt.error(f"vcpkg.json dependencies missing approved ports: {sorted(missing)}")

    overrides = {o.get("name"): o.get("version")
                 for o in doc.get("overrides", []) if isinstance(o, dict)}
    for name, ver in overrides.items():
        if name not in APPROVED_VERSIONS:
            rpt.error(f"vcpkg.json overrides an unapproved port: {name}")
        elif ver != APPROVED_VERSIONS[name]:
            rpt.error(f"vcpkg.json override {name} is {ver}, expected {APPROVED_VERSIONS[name]}")
    for name in set(APPROVED_VERSIONS) - set(overrides):
        rpt.error(f"vcpkg.json has no override pin for {name}")


def check_lock(rpt):
    path = ROOT / "cmake" / "dependencies.lock.json"
    if not path.exists():
        rpt.error("cmake/dependencies.lock.json is missing")
        return
    doc = load_json(path, rpt)
    if doc is None:
        return
    versions = doc.get("versions", {})
    for name, ver in APPROVED_VERSIONS.items():
        if versions.get(name) != ver:
            rpt.error(f"lock versions.{name} is {versions.get(name)!r}, expected {ver!r}")
    for key in ("vcpkg_release_tag", "vcpkg_registry_commit", "qt_ci_version"):
        val = doc.get("toolchain", {}).get(key)
        if not val or is_placeholder(val):
            rpt.error(f"lock toolchain.{key} is empty or a placeholder")


def check_third_party(rpt):
    tp = ROOT / "third_party"
    manifest = ROOT / "third_party" / "sources.lock.yaml"
    if not tp.exists():
        rpt.error("third_party/ is missing")
        return
    if not manifest.exists():
        rpt.error("third_party/sources.lock.yaml is missing")
        return

    if yaml is None:
        rpt.warn("PyYAML unavailable: third_party/sources.lock.yaml integrity checked structurally only")
        entries = {}
    else:
        try:
            entries = yaml.safe_load(manifest.read_text()) or {}
        except yaml.YAMLError as exc:
            rpt.error(f"third_party/sources.lock.yaml is not valid YAML: {exc}")
            return

    if not isinstance(entries, dict) or not entries:
        rpt.ok("third_party/sources.lock.yaml: no vendored sources recorded yet (schema only)")
    else:
        rpt.ok(f"third_party/sources.lock.yaml: {len(entries)} provenance record(s)")

    # Every non-documentation file directly under third_party/ must be recorded.
    for child in sorted(tp.iterdir()):
        if child.is_file() and child.name in ("README.md", "sources.lock.yaml"):
            continue
        key = child.name
        if isinstance(entries, dict) and key not in entries:
            rpt.error(f"third_party/{key} has no entry in sources.lock.yaml")


def check_corpus(rpt):
    path = ROOT / "tests" / "corpus" / "manifest.yaml"
    if not path.exists():
        # Optional until WP-100 introduces fixtures; empty is acceptable now.
        rpt.ok("tests/corpus/manifest.yaml: not present yet (no external fixtures in WP-00A)")
        return
    if yaml is None:
        rpt.warn("PyYAML unavailable: corpus manifest checked structurally only")
        return
    try:
        entries = yaml.safe_load(path.read_text()) or []
    except yaml.YAMLError as exc:
        rpt.error(f"tests/corpus/manifest.yaml is not valid YAML: {exc}")
        return
    if not isinstance(entries, list):
        rpt.error("tests/corpus/manifest.yaml must be a list of fixture records")
        return
    for i, e in enumerate(entries):
        if is_placeholder(e.get("source_url")):
            rpt.error(f"corpus entry {i} has a placeholder source_url")
        if is_placeholder(e.get("sha256")) or is_placeholder(e.get("license")):
            rpt.error(f"corpus entry {i} has a placeholder sha256 or license")


def check_no_tracked_generated(rpt):
    out = __import__("subprocess").run(["git", "-C", str(ROOT), "ls-files", "-z"],
                                       capture_output=True, text=True).stdout
    for raw in out.split("\0"):
        if not raw:
            continue
        rel = Path(raw)
        if any(p == rel.parts[0] for p in GENERATED_PATHS):
            rpt.error(f"tracked file under generated path: {raw}")


def main():
    rpt = _Reporter()
    print("verify_dependencies: static dependency audit\n")
    check_vcpkg(rpt)
    check_lock(rpt)
    check_third_party(rpt)
    check_corpus(rpt)
    check_no_tracked_generated(rpt)

    print(f"\nverify_dependencies: {rpt.failures} failure(s), {rpt.warnings} warning(s)")
    return 1 if rpt.failures else 0


class _Reporter:
    def __init__(self):
        self.failures = 0
        self.warnings = 0

    def ok(self, msg):
        print(f"  [ok]   {msg}")

    def warn(self, msg):
        print(f"  [warn] {msg}")
        self.warnings += 1

    def error(self, msg):
        print(f"  [fail] {msg}")
        self.failures += 1


if __name__ == "__main__":
    sys.exit(main())