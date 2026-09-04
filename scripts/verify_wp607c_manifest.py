#!/usr/bin/env python3
"""WP-607C controlled corpus manifest validator.

Deep contract check for tests/corpus/manifest.yaml and the generator catalog
(index.json) produced by pnga_generate_wp607c_corpus:

  * the manifest is a top-level list of records carrying a required `kind`
    field (`generated` or `external`);
  * generated records carry the exact generated key set, a generator mapping
    pinned to pnga_generate_wp607c_corpus (schema version 1, case == id),
    a build-relative output under valid/ or malformed/, a 64-lowercase-hex
    expected_sha256, sorted non-empty features/tests and exact catalog
    equality (the catalog record must equal the manifest record restricted to
    the generated key set);
  * external records keep the full provenance set (source URL, upstream
    version/commit, sha256, reviewed license) and never use placeholders;
  * every linked test is a real CTest name from `ctest --show-only=json-v1`
    output of the audited build directory;
  * the aggregate corpus revision is pinned (WP-607C ruling R1) to exactly
    four repository files; see aggregate_revision.

CLI modes:
    --self-test                  run the built-in deterministic self-test
    --manifest PATH              validate the manifest; combine with
                                 --catalog, --comparison-catalog, --build-dir
    --print-revision             print the aggregate corpus revision
    --refresh-generated-hashes   bless expected_sha256 values from a proven
                                 double generation, then re-validate

Usage: python3 scripts/verify_wp607c_manifest.py --self-test
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent

REQUIRED_IDS = (
    "ui-gray1-none", "ui-indexed4-trns", "ui-rgb8-five-filters",
    "ui-rgba16-byte-select", "ui-adam7-empty-passes",
    "trace-stored-literals", "trace-fixed-nonoverlap",
    "trace-dynamic-overlap-repeats", "trace-multiblock-bfinal",
    "idat-split-zlib-header", "idat-split-token", "idat-split-adler",
    "error-truncated-header", "error-truncated-token",
    "error-reserved-btype", "error-invalid-distance",
    "error-crc-mismatch", "error-adler-mismatch", "perf-large-rgba8",
)
GENERATED_KEYS = {
    "id", "kind", "expected_class", "expected_features",
    "expected_facts", "linked_tests", "generator", "output",
    "expected_sha256",
}
EXTERNAL_REQUIRED = {
    "id", "kind", "path", "source_url", "upstream_version",
    "upstream_commit", "sha256", "license", "expected_class",
    "expected_features", "linked_tests",
}

# WP-607C written-package ruling R1: the corpus revision covers exactly these
# four files, hashed in this order (see aggregate_revision).
REVISION_MANIFEST = Path("tests/corpus/manifest.yaml")
REVISION_GENERATOR_SOURCES = (
    Path("tests/corpus/controlled_fixture.h"),
    Path("tests/corpus/controlled_fixture.cpp"),
    Path("tests/corpus/generate_controlled_corpus.cpp"),
)

GENERATOR_EXECUTABLE = "pnga_generate_wp607c_corpus"
GENERATOR_SCHEMA_VERSION = 1
OUTPUT_ROOTS = ("valid", "malformed")
EXPECTED_CLASSES = frozenset(
    {"valid", "malformed", "boundary", "ui", "performance"})

HEX64 = re.compile(r"[0-9a-f]{64}")
HEX40 = re.compile(r"[0-9a-f]{40}")

_PLACEHOLDER = ("<", ">", "fill-", "example.invalid", "WP-00A-FILLS", "TBD")


def _is_placeholder(text):
    if not isinstance(text, str) or not text:
        return True
    return any(marker in text for marker in _PLACEHOLDER)


def _is_hex64(value):
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def _is_sorted_unique_strings(value):
    return (isinstance(value, list) and bool(value)
            and all(isinstance(item, str) and item for item in value)
            and all(value[i] < value[i + 1] for i in range(len(value) - 1)))


def _is_safe_relative(value):
    """True for a relative POSIX path without empty/dot/dot-dot segments."""
    if not isinstance(value, str) or not value or value.startswith("/"):
        return False
    if "\\" in value or PurePosixPath(value).is_absolute():
        return False
    parts = PurePosixPath(value).parts
    if not parts:
        return False
    return all(part not in ("", ".", "..") for part in parts)


def _is_build_output(value):
    if not _is_safe_relative(value):
        return False
    parts = PurePosixPath(value).parts
    return (len(parts) >= 2 and parts[0] in OUTPUT_ROOTS
            and str(PurePosixPath(value).suffix) == ".png")


def _check_classification(prefix, record, errors):
    expected_class = record.get("expected_class")
    if expected_class not in EXPECTED_CLASSES:
        errors.append(
            f"{prefix}: expected_class must be one of {sorted(EXPECTED_CLASSES)}")
    if not _is_sorted_unique_strings(record.get("expected_features")):
        errors.append(
            f"{prefix}: expected_features must be a non-empty sorted list of "
            "non-empty strings")


def _check_key_set(prefix, record, required, errors):
    keys = set(record)
    missing = sorted(required - keys)
    extra = sorted(keys - required)
    if missing:
        errors.append(f"{prefix}: missing keys {missing}")
    if extra:
        errors.append(f"{prefix}: unknown keys {extra}")
    return not missing and not extra


def validate_generated_record(prefix, record, catalog):
    errors = []
    if not _check_key_set(prefix, record, GENERATED_KEYS, errors):
        # The structure is unknown; deeper checks would misfire.
        return errors
    case_id = record["id"]
    if not isinstance(case_id, str) or not case_id:
        errors.append(f"{prefix}: id must be a non-empty string")
        return errors

    generator = record["generator"]
    if not isinstance(generator, dict):
        errors.append(f"{prefix}: generator must be a mapping")
    else:
        if generator.get("executable") != GENERATOR_EXECUTABLE:
            errors.append(f"{prefix}: generator.executable must be "
                          f"{GENERATOR_EXECUTABLE}")
        if generator.get("case") != case_id:
            errors.append(f"{prefix}: generator.case must equal the record id")
        if generator.get("schema_version") != GENERATOR_SCHEMA_VERSION:
            errors.append(f"{prefix}: generator.schema_version must be "
                          f"{GENERATOR_SCHEMA_VERSION}")
        arguments = generator.get("arguments")
        if not isinstance(arguments, dict) or not arguments:
            errors.append(f"{prefix}: generator.arguments must be a non-empty "
                          "mapping")

    if not isinstance(record["expected_facts"], dict) or not record["expected_facts"]:
        errors.append(f"{prefix}: expected_facts must be a non-empty mapping")

    if not _is_build_output(record["output"]):
        errors.append(f"{prefix}: output must be a build-relative path under "
                      "valid/ or malformed/ ending in .png")

    if not _is_hex64(record["expected_sha256"]):
        errors.append(f"{prefix}: expected_sha256 must be 64 lowercase hex")

    _check_classification(prefix, record, errors)

    if catalog is not None:
        errors.extend(_check_catalog_record(prefix, record, catalog))
    return errors


def _catalog_case_index(catalog):
    """Maps case id -> record for the generated catalog's `cases` list.

    The catalog written by pnga_generate_wp607c_corpus is
    {"schema_version": 1, "corpus_revision": <64hex>, "cases": [record...]}
    with the case records sorted by id. A catalog without a usable `cases`
    list yields an empty index, so every generated record reports the same
    "missing from the catalog" error it would for a truncated catalog.
    """
    if not isinstance(catalog, dict):
        return {}
    cases = catalog.get("cases")
    if not isinstance(cases, list):
        return {}
    return {entry.get("id"): entry for entry in cases
            if isinstance(entry, dict) and isinstance(entry.get("id"), str)}


def _check_catalog_revision(catalog):
    """The catalog revision (when present) must equal the aggregate revision
    of the four pinned repository files (ruling R1)."""
    if not isinstance(catalog, dict):
        return []
    revision = catalog.get("corpus_revision")
    if revision is None:
        return []
    if revision != compute_repo_revision():
        return ["catalog corpus_revision does not match the four pinned "
                "repository files"]
    return []


def _check_catalog_record(prefix, record, catalog):
    index = _catalog_case_index(catalog)
    case_id = record["id"]
    entry = index.get(case_id)
    if entry is None:
        return [f"{prefix}: case {case_id!r} is missing from the catalog"]
    if not isinstance(entry, dict):
        return [f"{prefix}: catalog entry for {case_id!r} must be a mapping"]
    projection = {key: record[key] for key in sorted(GENERATED_KEYS)}
    if entry != projection:
        return [f"{prefix}: catalog entry for {case_id!r} differs from the "
                "manifest record"]
    return []


def validate_external_record(prefix, record):
    errors = []
    if not _check_key_set(prefix, record, EXTERNAL_REQUIRED, errors):
        return errors
    case_id = record["id"]
    if not isinstance(case_id, str) or not case_id:
        errors.append(f"{prefix}: id must be a non-empty string")

    if not _is_safe_relative(record["path"]):
        errors.append(f"{prefix}: path must be a corpus-relative path without "
                      "traversal")
    if _is_placeholder(record["source_url"]):
        errors.append(f"{prefix}: source_url is missing or a placeholder")
    if _is_placeholder(record["upstream_version"]):
        errors.append(f"{prefix}: upstream_version is missing or a placeholder")
    upstream_commit = record["upstream_commit"]
    if upstream_commit is not None and (
            not isinstance(upstream_commit, str)
            or HEX40.fullmatch(upstream_commit) is None):
        errors.append(f"{prefix}: upstream_commit must be null or 40-hex")
    if not _is_hex64(record["sha256"]):
        errors.append(f"{prefix}: sha256 must be 64 lowercase hex")
    if _is_placeholder(record["license"]):
        errors.append(f"{prefix}: license is missing or a placeholder")

    _check_classification(prefix, record, errors)
    return errors


def validate_linked_tests(prefix, record, ctest_names):
    errors = []
    tests = record.get("linked_tests")
    if not _is_sorted_unique_strings(tests):
        errors.append(f"{prefix}: linked_tests must be a non-empty sorted list "
                      "of test names")
        return errors
    if ctest_names is not None:
        for name in tests:
            if name not in ctest_names:
                errors.append(f"{prefix}: linked test {name!r} is not a "
                              "registered CTest entry")
    return errors


def validate_required_ids(ids):
    errors = []
    missing = [case_id for case_id in REQUIRED_IDS if case_id not in ids]
    if missing:
        errors.append(f"manifest is missing required case ids {missing}")
    for case_id in sorted({case_id for case_id in ids if ids.count(case_id) > 1}):
        errors.append(f"manifest has duplicate id {case_id!r}")
    return errors


def validate_manifest(document, catalog, ctest_names):
    errors = []
    if not isinstance(document, list):
        return ["manifest root must be a list"]
    ids = []
    for index, record in enumerate(document):
        prefix = f"record[{index}]"
        if not isinstance(record, dict):
            errors.append(f"{prefix}: must be a mapping")
            continue
        case_id = record.get("id")
        ids.append(case_id)
        kind = record.get("kind")
        if kind not in {"generated", "external"}:
            errors.append(f"{prefix}: kind must be generated or external")
        if kind == "generated":
            errors.extend(validate_generated_record(prefix, record, catalog))
        if kind == "external":
            errors.extend(validate_external_record(prefix, record))
        errors.extend(validate_linked_tests(prefix, record, ctest_names))
    if catalog is not None and any(
            isinstance(record, dict) and record.get("kind") == "generated"
            for record in document):
        errors.extend(_check_catalog_revision(catalog))
    errors.extend(validate_required_ids(ids))
    return sorted(errors)


def aggregate_revision(manifest_bytes, generator_source_bytes):
    """Return the aggregate WP-607C corpus revision as 64 lowercase hex.

    Ruling R1 pins the revision to exactly four repository files, hashed in
    this order:

      1. tests/corpus/manifest.yaml                  (``manifest_bytes``)
      2. tests/corpus/controlled_fixture.h           (generator source 0)
      3. tests/corpus/controlled_fixture.cpp         (generator source 1)
      4. tests/corpus/generate_controlled_corpus.cpp (generator source 2)

    Each component contributes the SHA-256 digest of its raw bytes; the digests
    are concatenated in the order above and hashed once more. Any change to the
    byte content of one of the four files changes the revision.
    """
    digest = hashlib.sha256()
    digest.update(hashlib.sha256(manifest_bytes).digest())
    for source in generator_source_bytes:
        digest.update(hashlib.sha256(source).digest())
    return digest.hexdigest()


def check_generator_contract(failures):
    """WP-607C Task 5: the compiled generator must reproduce the corpus
    byte-identically across two fresh generations, write hash-valid outputs,
    carry the pinned aggregate revision, refuse source-tree destinations and
    expose the standard SHA-256 digest for its test CLI."""
    import shutil
    import tempfile

    candidates = (
        ROOT / "build" / "dev" / "tests" / "corpus"
        / "pnga_generate_wp607c_corpus",
        ROOT / "build" / "dev" / "tests" / "corpus"
        / "pnga_generate_wp607c_corpus.exe",
    )
    generator = next((path for path in candidates if path.is_file()), None)
    if generator is None:
        failures.append("generator: pnga_generate_wp607c_corpus is not built "
                        "under build/dev/tests/corpus")
        return

    def invoke(*arguments):
        return subprocess.run([str(generator), *arguments], cwd=str(ROOT),
                              capture_output=True, text=True)

    expected_digest = ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410f"
                       "f61f20015ad")
    digest_check = invoke("--sha256-text", "abc")
    if (digest_check.returncode != 0
            or digest_check.stdout.strip() != expected_digest):
        failures.append("generator: --sha256-text abc must print the standard "
                        f"digest {expected_digest}")

    with tempfile.TemporaryDirectory() as raw_dir:
        runs = []
        for name in ("run-a", "run-b"):
            output = Path(raw_dir) / name
            proc = invoke("--output", str(output))
            if proc.returncode != 0:
                failures.append(f"generator: --output {name} failed: "
                                f"{proc.stderr.strip()}")
                return
            runs.append(output)

        catalogs = [
            json.loads((run / "index.json").read_text(encoding="utf-8"))
            for run in runs
        ]
        if catalogs[0] != catalogs[1]:
            failures.append("generator: two fresh generations produced "
                            "different catalogs")
        cases = catalogs[0].get("cases")
        if not isinstance(cases, list) or len(cases) != len(REQUIRED_IDS):
            failures.append(f"generator: catalog must list exactly "
                            f"{len(REQUIRED_IDS)} case records")
            return
        ids = [record.get("id") for record in cases]
        if ids != sorted(REQUIRED_IDS):
            failures.append("generator: catalog ids must be the sorted "
                            "required id list")
        for record in cases:
            case_id = record.get("id")
            relative = record.get("output")
            if not isinstance(relative, str) or not relative:
                failures.append(f"generator: {case_id!r} record has no output")
                continue
            try:
                left = (runs[0] / relative).read_bytes()
                right = (runs[1] / relative).read_bytes()
            except OSError as exc:
                failures.append(f"generator: {case_id!r} output unreadable: "
                                f"{exc}")
                continue
            if left != right:
                failures.append(f"generator: {case_id!r} bytes differ between "
                                "generations")
            if hashlib.sha256(left).hexdigest() != record.get("expected_sha256"):
                failures.append(f"generator: {case_id!r} catalog hash does "
                                "not match the file bytes")

        if catalogs[0].get("corpus_revision") != compute_repo_revision():
            failures.append("generator: catalog corpus_revision does not "
                            "match the pinned four-file aggregate")

        # The generator must refuse a destination resolving inside the source
        # tests/corpus/ directory (package §5).
        forbidden = ROOT / "tests" / "corpus" / "wp-607c-probe"
        try:
            proc = invoke("--output", str(forbidden))
            if proc.returncode == 0:
                failures.append("generator: accepted a destination resolving "
                                "inside source tests/corpus/")
        finally:
            shutil.rmtree(forbidden, ignore_errors=True)

        manifest_path = ROOT / "tests" / "corpus" / "manifest.yaml"
        if manifest_path.is_file():
            document = _load_yaml_document(manifest_path)
            errors = validate_manifest(document, catalogs[0], None)
            if errors:
                failures.append("generator: source manifest fails validation "
                                f"against the generated catalog: {errors[:4]}")


def run_self_test():
    failures = []

    def make_generated(case_id, expected_class="valid", output_root="valid",
                       linked=("wp607c_png_facts_tests",)):
        return {
            "id": case_id,
            "kind": "generated",
            "expected_class": expected_class,
            "expected_features": ["fixture", "wp607c"],
            "expected_facts": {"stable_id": case_id},
            "linked_tests": sorted(linked),
            "generator": {
                "executable": GENERATOR_EXECUTABLE,
                "case": case_id,
                "schema_version": GENERATOR_SCHEMA_VERSION,
                "arguments": {"case": case_id},
            },
            "output": f"{output_root}/{case_id}.png",
            "expected_sha256": hashlib.sha256(case_id.encode()).hexdigest(),
        }

    def generated_record(**overrides):
        record = make_generated("ui-gray1-none", expected_class="ui")
        record["expected_features"] = ["bit_depth_1", "grayscale",
                                       "non_interlaced"]
        record["expected_facts"] = {"height": 3, "width": 9}
        record["generator"]["arguments"] = {"height": 3, "width": 9}
        record.update(overrides)
        return record

    def external_record(**overrides):
        record = {
            "id": "external-sample",
            "kind": "external",
            "path": "pngsuite/basn0g01.png",
            "source_url": "http://www.schaik.com/pngsuite/basn0g01.png",
            "upstream_version": "2017",
            "upstream_commit": None,
            "sha256": "b" * 64,
            "license": "PngSuite-LICENSE",
            "expected_class": "valid",
            "expected_features": ["bit_depth_1", "grayscale"],
            "linked_tests": ["wp607c_png_facts_tests"],
        }
        record.update(overrides)
        return record

    def full_document():
        classes = {
            "ui-": ("ui", "valid", ("wp607c_png_facts_tests",)),
            "trace-": ("valid", "valid", ("wp607c_trace_facts_tests",)),
            "idat-split-": ("boundary", "valid",
                            ("wp607c_trace_facts_tests",)),
            "error-": ("malformed", "malformed",
                       ("wp607c_trace_facts_tests",)),
            "perf-": ("performance", "valid",
                      ("wp607c_trace_facts_tests",)),
        }
        records = []
        for case_id in REQUIRED_IDS:
            expected_class, output_root, linked = next(
                value for prefix, value in classes.items()
                if case_id.startswith(prefix))
            records.append(make_generated(case_id, expected_class, output_root,
                                          linked))
        records.append(external_record())
        return records

    TESTS = frozenset({"wp607c_png_facts_tests", "wp607c_trace_facts_tests"})

    def expect_reject(name, document, fragment, catalog=None,
                      ctest_names=TESTS):
        errors = validate_manifest(document, catalog, ctest_names)
        if not any(fragment in error for error in errors):
            failures.append(f"{name}: expected rejection containing "
                            f"{fragment!r}, got {errors}")

    def expect_accept(name, document, catalog=None, ctest_names=TESTS):
        errors = validate_manifest(document, catalog, ctest_names)
        if errors:
            failures.append(f"{name}: expected acceptance, got {errors}")

    good = full_document()
    expect_accept("valid generated and external records", good)

    def catalog_document(records, revision=None):
        return {
            "schema_version": 1,
            "corpus_revision": (compute_repo_revision() if revision is None
                                else revision),
            "cases": [
                {key: record[key] for key in sorted(GENERATED_KEYS)}
                for record in sorted(
                    (record for record in records
                     if record["kind"] == "generated"),
                    key=lambda record: record["id"])
            ],
        }

    good_catalog = catalog_document(good)
    expect_accept("valid records with matching catalog", good,
                  catalog=good_catalog)
    expect_reject("catalog entry missing", good, "missing from the catalog",
                  catalog={})
    drifted_cases = [dict(case) for case in good_catalog["cases"]]
    for case in drifted_cases:
        if case["id"] == "ui-gray1-none":
            case["expected_sha256"] = "c" * 64
    drifted_catalog = dict(good_catalog, cases=drifted_cases)
    expect_reject("catalog record mismatch", good, "differs from the",
                  catalog=drifted_catalog)
    expect_reject("catalog revision drift", good,
                  "corpus_revision does not match",
                  catalog=catalog_document(good, revision="0" * 64))

    expect_reject("generated record with source_url",
                  [generated_record(source_url="http://example.invalid/x.png")],
                  "unknown keys ['source_url']")

    expect_reject("external record without license",
                  [generated_record(),
                   {k: v for k, v in external_record().items()
                    if k != "license"}],
                  "missing keys ['license']")

    expect_reject("duplicate ids",
                  [generated_record(), generated_record()],
                  "duplicate id")

    expect_reject("path traversal in generated output",
                  [generated_record(output="../escape.png")],
                  "output must be a build-relative path")

    expect_reject("missing required ids",
                  [generated_record()], "missing required case ids")

    expect_reject("unknown key", [generated_record(unexpected_key=1)],
                  "unknown keys ['unexpected_key']")

    expect_reject("unsorted expected_features",
                  [generated_record(expected_features=["zeta", "alpha"])],
                  "non-empty sorted list")

    expect_reject("unsorted linked_tests",
                  [generated_record(
                      linked_tests=["wp607c_trace_facts_tests",
                                    "wp607c_png_facts_tests"])],
                  "non-empty sorted list")

    expect_reject("linked test is not a registered CTest entry",
                  [generated_record(linked_tests=["not_a_real_test"])],
                  "not a registered CTest entry")

    expect_reject("placeholder source_url",
                  [generated_record(), external_record(source_url="fill-me-in")],
                  "placeholder")

    expect_reject("placeholder license",
                  [generated_record(), external_record(license="TBD")],
                  "placeholder")

    expect_reject("non-hex sha256",
                  [generated_record(), external_record(sha256="NOPE")],
                  "sha256 must be 64 lowercase hex")

    expect_reject("non-list document", {"not": "a list"},
                  "manifest root must be a list")

    # aggregate_revision is deterministic and input-sensitive.
    left = aggregate_revision(b"manifest", [b"fixture.h", b"fixture.cpp",
                                            b"generator.cpp"])
    right = aggregate_revision(b"manifest", [b"fixture.h", b"fixture.cpp",
                                             b"generator.cpp"])
    if left != right:
        failures.append("aggregate_revision: same inputs produced different digests")
    if left == aggregate_revision(b"manifest!", [b"fixture.h", b"fixture.cpp",
                                                 b"generator.cpp"]):
        failures.append("aggregate_revision: manifest bytes do not affect the digest")
    if left == aggregate_revision(b"manifest", [b"fixture.h!", b"fixture.cpp",
                                                b"generator.cpp"]):
        failures.append("aggregate_revision: generator sources do not affect the digest")
    expected = hashlib.sha256()
    expected.update(hashlib.sha256(b"manifest").digest())
    for source in (b"fixture.h", b"fixture.cpp", b"generator.cpp"):
        expected.update(hashlib.sha256(source).digest())
    if left != expected.hexdigest():
        failures.append("aggregate_revision: digest does not match the pinned formula")
    if len(left) != 64 or HEX64.fullmatch(left) is None:
        failures.append("aggregate_revision: digest is not 64 lowercase hex")

    check_generator_contract(failures)

    if failures:
        for failure in failures:
            print(f"self-test FAIL: {failure}")
        print(f"self-test: {len(failures)} failure(s)")
        return 1
    print("self-test: all negative and positive manifest checks passed")
    print("self-test: aggregate_revision matches the pinned four-component formula")
    return 0


def load_ctest_names(build_dir):
    proc = subprocess.run(
        ["ctest", "--show-only=json-v1"], cwd=str(build_dir), check=True,
        capture_output=True, text=True)
    document = json.loads(proc.stdout)
    names = set()
    for entry in document.get("tests", []):
        if isinstance(entry, dict) and isinstance(entry.get("name"), str):
            names.add(entry["name"])
    return names


def compute_repo_revision(root=ROOT):
    manifest_bytes = (root / REVISION_MANIFEST).read_bytes()
    source_bytes = [
        (root / relative).read_bytes() for relative in REVISION_GENERATOR_SOURCES
    ]
    return aggregate_revision(manifest_bytes, source_bytes)


def _load_yaml_document(path):
    try:
        import yaml
    except ImportError:
        print("verify_wp607c_manifest: PyYAML is required", file=sys.stderr)
        raise SystemExit(2)
    try:
        return yaml.safe_load(path.read_text(encoding="utf-8"))
    except yaml.YAMLError as exc:
        print(f"verify_wp607c_manifest: {path} is not valid YAML: {exc}",
              file=sys.stderr)
        raise SystemExit(2)


def _load_json_document(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print(f"verify_wp607c_manifest: {path} is not valid JSON: {exc}",
              file=sys.stderr)
        raise SystemExit(2)


def refresh_generated_hashes(manifest_path, catalog):
    """Rewrite only the expected_sha256 scalars, in stable id order.

    The caller must already have proven that the two generated catalogs are
    byte-identical; this function trusts `catalog` (the generated index.json
    document) for the new values.
    """
    entries = _catalog_case_index(catalog)
    lines = manifest_path.read_text(encoding="utf-8").splitlines(keepends=True)
    id_re = re.compile(r"^(\s*-\s*id:\s*)(\S+)\s*$")
    hash_re = re.compile(r"^(\s*expected_sha256:\s*)(\S+)\s*(#.*)?$")
    current_id = None
    refreshed = 0
    for index, line in enumerate(lines):
        id_match = id_re.match(line)
        if id_match:
            current_id = id_match.group(2)
            continue
        hash_match = hash_re.match(line)
        if hash_match and current_id in entries:
            entry = entries[current_id]
            new_hash = entry.get("expected_sha256") if isinstance(entry, dict) else None
            if not _is_hex64(new_hash):
                print(
                    "verify_wp607c_manifest: catalog has no usable "
                    f"expected_sha256 for {current_id}",
                    file=sys.stderr)
                raise SystemExit(2)
            suffix = hash_match.group(3) or ""
            lines[index] = f"{hash_match.group(1)}{new_hash}{suffix}\n"
            refreshed += 1
    if refreshed == 0:
        print("verify_wp607c_manifest: no expected_sha256 values to refresh",
              file=sys.stderr)
        raise SystemExit(2)
    temporary = manifest_path.with_suffix(manifest_path.suffix + ".tmp")
    temporary.write_text("".join(lines), encoding="utf-8")
    temporary.replace(manifest_path)
    return refreshed


def main(argv=None):
    try:
        import yaml  # noqa: F401 — validated used throughout the module
    except ImportError:
        sys.stderr.write(
            "verify_wp607c_manifest: PyYAML is required for manifest "
            "validation (python -m pip install PyYAML)\n")
        return 2
    parser = argparse.ArgumentParser(
        description="WP-607C controlled corpus manifest validator")
    parser.add_argument("--self-test", action="store_true",
                        help="run the built-in deterministic self-test")
    parser.add_argument("--manifest", help="path to tests/corpus/manifest.yaml")
    parser.add_argument("--catalog", help="path to the generated index.json")
    parser.add_argument("--comparison-catalog",
                        help="second-generation index.json for equality proof")
    parser.add_argument("--build-dir",
                        help="build directory queried for CTest entry names")
    parser.add_argument("--print-revision", action="store_true",
                        help="print the aggregate corpus revision")
    parser.add_argument("--refresh-generated-hashes", action="store_true",
                        help="bless expected_sha256 values from the proven "
                             "double generation, then re-validate")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    if args.print_revision:
        try:
            print(compute_repo_revision())
        except FileNotFoundError as exc:
            print(f"verify_wp607c_manifest: revision component missing: {exc}",
                  file=sys.stderr)
            return 2
        return 0

    if not args.manifest:
        parser.print_usage(sys.stderr)
        print("verify_wp607c_manifest: --manifest is required", file=sys.stderr)
        return 2

    manifest_path = Path(args.manifest)
    document = _load_yaml_document(manifest_path)

    catalog = None
    if args.catalog:
        catalog = _load_json_document(Path(args.catalog))

    comparison = None
    if args.comparison_catalog:
        comparison = _load_json_document(Path(args.comparison_catalog))

    if args.refresh_generated_hashes:
        if catalog is None or comparison is None:
            print("verify_wp607c_manifest: --refresh-generated-hashes requires "
                  "--catalog and --comparison-catalog", file=sys.stderr)
            return 2
        if catalog != comparison:
            print("verify_wp607c_manifest: the two generated catalogs differ; "
                  "refusing to bless hashes", file=sys.stderr)
            return 1
        count = refresh_generated_hashes(manifest_path, catalog)
        print(f"verify_wp607c_manifest: refreshed {count} expected_sha256 "
              "value(s) from the proven double generation")
        # Re-read the blessed manifest so the validation below sees the new
        # digests (and only the digests) rather than the pre-blessing draft.
        document = _load_yaml_document(manifest_path)

    ctest_names = None
    if args.build_dir:
        ctest_names = load_ctest_names(args.build_dir)

    errors = validate_manifest(document, catalog, ctest_names)
    for error in errors:
        print(f"verify_wp607c_manifest: {error}")
    if errors:
        print(f"verify_wp607c_manifest: {len(errors)} error(s)")
        return 1
    print("verify_wp607c_manifest: manifest contract ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
