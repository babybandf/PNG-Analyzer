#!/usr/bin/env python3
"""WP-000/001 layout contract verifier (REPOSITORY_LAYOUT.md §14).

Returns nonzero (and reports each finding) when the tracked tree violates the
repository layout contract:

  * An unknown top-level directory.
  * An underscore or uppercase letter in a source directory name (kebab-case).
  * A libs/<module> missing its required CMakeLists.txt, README.md, include/ or
    src/ layout.
  * A Qt include or Qt6:: link under libs/.
  * libpng (PNG::PNG) linked outside libs/backend-libpng.
  * Catch2 referenced from a production target (libs/ or apps/ CMake).
  * An include of another module's src/ private header.
  * A tracked file under a generated path.
  * Vendored source without a third_party/sources.lock.yaml entry.
  * Corpus data without a manifest entry.
  * An old path name listed in REPOSITORY_LAYOUT.md §10.

Items 8-10 are shared with scripts/verify_dependencies.py; both are expected to
run in CI before compilation. Usage: python3 scripts/verify_repository_layout.py
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Canonical top-level directories from REPOSITORY_LAYOUT.md §3.
CANONICAL_TOP_LEVEL = {
    "apps", "libs", "ui", "plugins", "sdk", "tools", "scripts", "tests",
    "samples", "benchmarks", "docs", "third_party", "packaging", "cmake",
    ".github",
}

# Source trees whose directory names must be lowercase kebab-case.
SOURCE_TREES = ("apps", "libs", "ui", "tests", "tools", "benchmarks",
                "packaging", "cmake", "scripts")

# Old/conflicting path names mapped by REPOSITORY_LAYOUT.md §10.
OLD_PATH_SEGMENTS = (
    "deflate_trace", "inflate_index", "deflate_index", "plugin-sdk",
    "golden-traces",
)
OLD_PATH_PREFIXES = ("tests/conformance",)

GENERATED_PATHS = (".deps", "build", "vcpkg_installed", "Testing")

BAD_NAME = re.compile(r"[A-Z_]")


def tracked_files():
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"],
                         capture_output=True, text=True).stdout
    return [Path(p) for p in out.split("\0") if p]


def tracked_dirs():
    dirs = set()
    for f in tracked_files():
        for i in range(1, len(f.parts)):
            dirs.add(Path(*f.parts[:i]))
    return dirs


def check_top_level(rpt, files):
    present = {f.parts[0] for f in files if len(f.parts) > 1}
    unknown = sorted(present - CANONICAL_TOP_LEVEL)
    for d in unknown:
        rpt.error(f"unknown top-level directory: {d}/")


def check_names(rpt, files):
    for f in files:
        if len(f.parts) < 2 or f.parts[0] not in SOURCE_TREES:
            continue
        for seg in f.parts[1:-1]:
            if BAD_NAME.search(seg):
                rpt.error(f"directory name not kebab-case: {Path(*f.parts[:f.parts.index(seg) + 1])}")
                break


def check_libs_structure(rpt, files):
    libs = sorted({f.parts[1] for f in files if f.parts[0] == "libs" and len(f.parts) > 1})
    for m in libs:
        base = ROOT / "libs" / m
        for required in ("CMakeLists.txt", "README.md"):
            if not (base / required).exists():
                rpt.error(f"libs/{m}/ is missing required {required}")
        if not (base / "include").is_dir():
            rpt.error(f"libs/{m}/ is missing required include/ directory")
        if not (base / "src").is_dir():
            rpt.error(f"libs/{m}/ is missing required src/ directory")


def check_qt_in_libs(rpt, files):
    for f in files:
        if f.parts[0] != "libs":
            continue
        if f.suffix not in (".cpp", ".h", ".hpp", ".txt", ".cmake"):
            continue
        text = f.read_text(errors="replace")
        if "Qt6::" in text or re.search(r"#include\s*<Qt", text):
            rpt.error(f"Qt reference under libs/: {f}")


def check_libpng_scope(rpt, files):
    for f in files:
        if f.suffix != ".txt" or f.parts[0] not in ("libs", "apps"):
            continue
        if f.parts[:2] == ("libs", "backend-libpng"):
            continue
        if "PNG::PNG" in f.read_text(errors="replace"):
            rpt.error(f"PNG::PNG linked outside libs/backend-libpng: {f}")


def check_catch2_scope(rpt, files):
    for f in files:
        if f.suffix != ".txt" or f.parts[0] not in ("libs", "apps"):
            continue
        if "Catch2" in f.read_text(errors="replace"):
            rpt.error(f"Catch2 referenced from production target: {f}")


def check_cross_src_include(rpt, files):
    for f in files:
        if f.parts[0] != "libs" or f.suffix not in (".cpp", ".h", ".hpp"):
            continue
        module = f.parts[1]
        text = f.read_text(errors="replace")
        for inc in re.finditer(r'#include\s+"([^"]+)"', text):
            target = inc.group(1)
            # A quoted include into another module's private src/ tree.
            if "src/" in target and not target.startswith(f"pnga/{module}"):
                rpt.error(f"{f}: includes private header of another module: {target}")


def check_old_paths(rpt, files):
    for f in files:
        path = f.as_posix()
        if any(seg in path for seg in OLD_PATH_SEGMENTS) or \
           any(path.startswith(p) for p in OLD_PATH_PREFIXES):
            rpt.error(f"old path name (layout §10): {path}")


def check_no_tracked_generated(rpt, files):
    for f in files:
        if f.parts[0] in GENERATED_PATHS:
            rpt.error(f"tracked file under generated path: {f}")


def check_third_party(rpt):
    manifest = ROOT / "third_party" / "sources.lock.yaml"
    tp = ROOT / "third_party"
    if not tp.is_dir():
        rpt.error("third_party/ is missing")
        return
    if not manifest.exists():
        rpt.error("third_party/sources.lock.yaml is missing")
        return
    entries = set()
    try:
        import yaml
        data = yaml.safe_load(manifest.read_text())
        entries = set(data) if isinstance(data, dict) else set()
    except Exception:
        rpt.warn("PyYAML unavailable: third_party records checked by directory name only")
    for child in sorted(tp.iterdir()):
        if child.name in ("README.md", "sources.lock.yaml"):
            continue
        if child.name not in entries:
            rpt.error(f"third_party/{child.name} has no entry in sources.lock.yaml")


def check_corpus(rpt, files):
    manifest = ROOT / "tests" / "corpus" / "manifest.yaml"
    corpus_root = ROOT / "tests" / "corpus"
    binary = [f for f in files
              if f.parts[:3] == ("tests", "corpus", ) and len(f.parts) > 3
              and f.parts[3] != "manifest.yaml" and f.suffix != ".yaml"]
    if not binary:
        rpt.ok("tests/corpus: no binary fixtures tracked yet")
        return
    if not manifest.exists():
        rpt.error(f"tests/corpus has {len(binary)} fixture(s) but no manifest.yaml")
        return
    try:
        import yaml
        data = yaml.safe_load(manifest.read_text()) or []
    except Exception:
        rpt.error("tests/corpus/manifest.yaml is not valid YAML")
        return
    names = {e.get("path") or e.get("file") for e in data if isinstance(e, dict)} if isinstance(data, list) else set()
    for f in binary:
        rel = f.relative_to(corpus_root).as_posix()
        if rel not in names:
            rpt.error(f"tests/corpus fixture without manifest entry: {rel}")


def main():
    rpt = _Reporter()
    files = tracked_files()
    print("verify_repository_layout: layout contract audit\n")
    check_top_level(rpt, files)
    check_names(rpt, files)
    check_libs_structure(rpt, files)
    check_qt_in_libs(rpt, files)
    check_libpng_scope(rpt, files)
    check_catch2_scope(rpt, files)
    check_cross_src_include(rpt, files)
    check_old_paths(rpt, files)
    check_no_tracked_generated(rpt, files)
    check_third_party(rpt)
    check_corpus(rpt, files)

    print(f"\nverify_repository_layout: {rpt.failures} failure(s), {rpt.warnings} warning(s)")
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
