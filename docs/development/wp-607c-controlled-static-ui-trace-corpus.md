# WP-607C — Controlled Static UI/Trace Corpus

Status: **PASS** (2026-09-04)

Implementation plan:
`docs/superpowers/plans/2026-09-03-wp-607c-controlled-static-ui-trace-corpus.md`.

## 1. Goal

Provide one deterministic, provenance-auditable static PNG corpus that supplies
the exact pixel, reconstruction, DEFLATE, error, UI-width and performance cases
required by WP-5U12F and later release evidence work.

WP-607C completes when every required category has a stable case id, fixed
generator arguments, exact expected facts, a reproducible output hash and at
least one owning CTest target. Generated PNG files remain build artifacts; they
are not committed to the repository.

## 1a. Gate handoff (2026-09-04)

- Implementation commit range: `b603ead..af10e39` on branch
  `wp-5u12-compression-inspector` (Tasks 1–7); a docs-only commit records this
  handoff afterwards.
- Aggregate corpus revision:
  `5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f`
  (identical from the `dev` build, the fresh non-preset build and
  `verify_wp607c_manifest.py --print-revision`).
- Evidence record: `build/evidence/wp-607c-corpus.json` (not committed),
  SHA-256 `8498e1b45f6d99916b88b1bdb85b389ac83e6510981c304e28df26e84182c1c2`,
  schema version 1, status PASS, 19 byte-identical double-generation cases,
  five `wp607c` CTest entries passed, no absolute paths in the record (the
  compiler fact is a deterministic id+version identity, enforced at write
  time and by `--self-test`).
  Regenerate with
  `python3 scripts/run_wp607c_corpus_gate.py --preset dev --jobs 4`.
- Full verification matrix (all exit 0): repository layout audit
  (0 failures / 0 warnings), dependency audit (0/0), dev configure + full
  build, corpus gate, `QT_QPA_PLATFORM=offscreen ctest --preset dev -j 4`
  with **52/52 entries passing** (47 baseline +
  `wp607c_generate_corpus` + `wp607c_manifest_tests` +
  `gui_wp607c_corpus_tests`), ASan/UBSan fuzz gate
  (`--preset asan --jobs 4`), performance threshold gate with the
  corpus-revision cross-check, and `git diff --check`.
- Fresh-build rerun: explicit non-preset configure
  (`cmake -S . -B build/wp607c-fresh -G Ninja` with the dev preset's
  toolchain/manifest/vcpkg settings), full build, `ctest -L wp607c` 5/5,
  aggregate revision and all 19 file hashes byte-identical to the `dev` run.
- WP-5U12F must re-run `run_wp607c_corpus_gate.py` as its first preflight
  step; this PASS supplies fixtures and facts only.

## 2. Baseline and prerequisites

- Implementation baseline is branch `wp-5u12-compression-inspector`, commit
  `9cec67c`, in `.worktrees/wp-5u12-compression-inspector`.
- WP-5U15 is complete on that baseline.
- WP-5U12A-E and the three audited behavioral fixes are complete; the current
  development gate is 47/47 CTest entries passing.
- WP-5U12F remains blocked until this package reaches PASS and its corpus gate
  is re-run from the WP-5U12 worktree.
- Accepted ADRs, `AGENTS.md`, `REPOSITORY_LAYOUT.md`, the approved next-work-
  packages design and `wp-607-cross-platform-quality-evidence.md` remain
  authoritative.

If implementation starts from a commit other than `9cec67c`, Task 0 must first
record the new commit and prove that the WP-5U12 public test-facing contracts
used by this package have not regressed.

## 3. Scope

WP-607C owns:

- a test-only deterministic PNG/DEFLATE fixture factory;
- a versioned generated-fixture schema in `tests/corpus/manifest.yaml`;
- generation into the CMake build tree;
- exact fact verification for pixels, filters, Adam7, Blocks, tokens,
  cross-IDAT provenance and malformed streams;
- real-file GUI smoke coverage at required narrow widths;
- a shared large case consumed by the existing performance runner;
- one corpus gate command and a machine-readable gate record.

WP-607C does not own:

- production behavior changes;
- the 22 WP-5U12F visual baselines or their tolerance policy;
- Windows/macOS native performance baselines;
- WP-607A native accessibility execution;
- WP-607D final evidence audit;
- APNG, Statistics, Compare or First Difference work;
- downloaded or committed third-party fixtures.

If a controlled case reveals a product defect, record a focused failing test
and return `BLOCKED: production defect <short description>`. Fixing production
code requires a separately approved defect package; WP-607C must not use its
test-fixture scope as implicit production-change authority.

## 4. Audit of the current baseline

The baseline already has useful generators and assertions, but they are local
to individual tests and do not form a controlled corpus contract.

| Requirement | Existing evidence | WP-607C gap |
|---|---|---|
| Legal color type/bit depth matrix | `test_png_helpers.h` and differential tests cover all 15 legal combinations | No stable case ids or manifest facts |
| Five PNG filters | Rotating test-side filters exist | No named real-file UI case with exact row filter sequence |
| Adam7 and empty passes | Reconstruction tests cover both | Not exported as a reusable corpus case |
| Indexed 4-bit | PLTE generation exists | No PLTE+tRNS controlled case |
| RGBA16 selection | Native-sample and coordinate tests exist | No reusable file with fixed high/low-byte facts |
| Stored/Fixed/Dynamic | Block and token unit tests exist | Compressor-driven cases are not an auditable exact corpus |
| Literal and Match | Token tests cover both | Non-overlap and overlap are not separate named cases with exact token facts |
| Multi-Block/BFINAL | Large Stored input produces multiple Blocks | Exact Block sequence and final-bit facts are not frozen |
| Cross-IDAT | Several local two-IDAT fixtures exist | Header, token and Adler splits are not three explicit cases |
| Truncated/Reserved/CRC/Adler | Focused unit tests exist | No shared files or manifest-linked owning targets |
| Invalid distance | Decoder behavior exists | No whole-PNG controlled case with an exact stop location |
| Narrow UI | Synthetic model tests cover 320/360/480/600 px | No real controlled PNG drives the same gate |
| Large performance | WP-604A has a generated 1024x768 case | It is not tied to the WP-607C revision/catalog |

Existing focused unit tests remain valuable. WP-607C centralizes only fixture
construction and corpus-level facts; it does not delete or weaken independent
boundary tests.

## 5. Architecture decision

Use a generated-first, test-only corpus. A C++20 fixture library constructs PNG
chunks and exact DEFLATE bitstreams without calling PNG Analyzer production
parsers or decoders. A small generator executable writes named PNG files and a
JSON index beneath the active build directory. Catch2 and Qt tests consume the
same case registry in memory or from those generated files.

The fixture factory may use the pinned zlib checksum functions for CRC-32 and
Adler-32. It must not use zlib compression heuristics to decide Block layout or
token selection. Stored, Fixed, Dynamic and malformed DEFLATE streams are
written explicitly so BFINAL, BTYPE, input bit ranges, token boundaries and
IDAT split positions remain byte-identical across supported platforms.

Generated output root:

```text
${CMAKE_BINARY_DIR}/tests/corpus/wp-607c/
  index.json
  valid/*.png
  malformed/*.png
```

No generated file may be written under the source-tree `tests/corpus/`
directory. The generator refuses a destination that resolves inside the source
tree.

## 6. Manifest contract

`tests/corpus/manifest.yaml` remains a top-level list for compatibility with
the existing repository verifiers. Every record gains a required `kind` field
whose value is `generated` or `external`.

Every record contains:

- `id`: unique lower-kebab-case stable id;
- `kind`;
- `expected_class`: `valid`, `malformed`, `boundary`, `ui` or `performance`;
- `expected_features`: non-empty sorted list;
- `expected_facts`: typed mapping of exact assertions relevant to the case;
- `linked_tests`: non-empty sorted list of real CTest names.

A generated record additionally contains:

- `generator.executable = pnga_generate_wp607c_corpus`;
- `generator.case`: case-registry key, identical to `id`;
- `generator.schema_version = 1`;
- `generator.arguments`: complete fixed argument mapping;
- `output`: build-root-relative PNG path;
- `expected_sha256`: final lowercase 64-hex digest.

Implementation obtains `expected_sha256` by generating the complete corpus in
two fresh temporary directories and first proving byte-for-byte equality. The
equal digest is then recorded in the manifest. A missing, malformed or unequal
digest is a failing gate, never an accepted draft value.

An external record retains the current required fields: `path`, exact
`source_url`, `upstream_version`, `upstream_commit`, `sha256`, reviewed
`license`, expected classification/features and linked tests. WP-607C adds no
external records. Generated records must not invent `source_url`, `license` or
upstream fields.

`scripts/verify_dependencies.py` and `scripts/verify_repository_layout.py`
must distinguish the two kinds. They validate generated schema and hashes,
continue to enforce full provenance for external files, and reject any tracked
generated output.

## 7. Required case matrix

The package freezes these 19 ids. One PNG may share base pixels with another,
but every id produces its own output file and exact fact record.

### 7.1 Pixel, filter and reconstruction cases

| Id | Fixed construction | Required exact facts |
|---|---|---|
| `ui-gray1-none` | 9x3 grayscale, bit depth 1, non-interlaced | Packed sample values, row bytes and filter 0 |
| `ui-indexed4-trns` | 5x3 indexed, bit depth 4, PLTE+tRNS | Packed indices, palette RGB entries and per-index alpha |
| `ui-rgb8-five-filters` | 8x5 RGB8, one row per filter | Filter sequence 0,1,2,3,4 and selected pixel bytes |
| `ui-rgba16-byte-select` | 3x2 RGBA16 | Big-endian channel values and selected high/low byte offsets |
| `ui-adam7-empty-passes` | 2x1 RGBA8 Adam7 | All seven pass geometries, exact empty-pass set and final pixels |

The five cases are compared against libpng through the existing differential
harness. Expectations for packed values, pass geometry and filtered bytes come
from the test fixture definition, not from production reconstruction output.

### 7.2 Valid DEFLATE and Trace cases

| Id | Fixed construction | Required exact facts |
|---|---|---|
| `trace-stored-literals` | One final Stored Block | LEN/NLEN, literal bytes, input/output ranges and EOB semantics |
| `trace-fixed-nonoverlap` | One final Fixed Block | At least one literal and one Match whose distance is at least its length |
| `trace-dynamic-overlap-repeats` | One final Dynamic Block | Literal, distance-1 overlap Match, EOB and valid code-length repeats 16/17/18 |
| `trace-multiblock-bfinal` | Stored then Fixed then Dynamic | Three exact Block ranges; BFINAL false,false,true; continuous output tiling |

Each case freezes zlib header fields, DEFLATE-domain bit ranges, output ranges,
canonical and read-order codes, literal/Match/EOB sequence, extra-bit values,
Match source/target ranges and overlap classification.

### 7.3 Cross-IDAT cases

| Id | Split rule | Required exact facts |
|---|---|---|
| `idat-split-zlib-header` | Split between CMF and FLG | Two ordered physical spans cover the logical zlib header |
| `idat-split-token` | Split inside a known Huffman token byte span | Token retains every ordered file span; no first-span-only mapping |
| `idat-split-adler` | Split after the second Adler byte | Adler range maps to two ordered physical spans and still verifies |

All three are complete PNG files with valid per-chunk CRCs. The manifest stores
IDAT payload lengths and exact logical-to-physical mappings.

### 7.4 Malformed cases

| Id | Mutation | Required exact facts |
|---|---|---|
| `error-truncated-header` | Cut a DEFLATE Block header before completion | Stable error category and exact input/output stop positions |
| `error-truncated-token` | Cut a Huffman token before completion | Verified prefix retained; exact token/input/output stop positions |
| `error-reserved-btype` | Encode BTYPE=11 | Reserved type, stop after three Block-header bits, zero output |
| `error-invalid-distance` | Emit a distance beyond produced output | Stable invalid-distance category and exact failing token/ranges |
| `error-crc-mismatch` | Flip one IDAT CRC bit only | CRC issue points to the CRC field while parsed structure is retained |
| `error-adler-mismatch` | Flip one Adler bit and repair IDAT CRC | Adler expected/actual mismatch with verified Blocks retained |

Malformed cases must change only the named fault. Tests prove the surrounding
wrapper, chunk ordering and unaffected checksums remain valid where applicable.

### 7.5 UI and performance profiles

| Id | Construction | Required use |
|---|---|---|
| `perf-large-rgba8` | 1024x768 RGBA8, deterministic Stored Blocks | Fast index, first preview, random row, pixel provenance and reopen measurements |

`ui-rgb8-five-filters`, `trace-stored-literals`,
`trace-dynamic-overlap-repeats`, `error-truncated-token` and
`error-reserved-btype` form the real-file UI profile. The GUI test exercises
them at 320, 360, 480 and 600 logical pixels in light mode and at 360 and 480
logical pixels in dark mode. It checks state and geometry; screenshot capture
remains WP-5U12F scope. Adjudication (controller ruling, 2026-09-04): for the
two plan-pinned malformed UI cases the verified DEFLATE prefix contains no
complete block, so production correctly refuses to index the stream and the
"keep verified rows and show stable Partial/Error copy" expectation is
unobservable; the GUI gate instead asserts the stable non-ready context copy
and the preserved parsed structure, which satisfies that expectation's safety
intent. Decoder-level Partial/Error facts remain frozen by the trace facts
tests wherever a verified prefix exists.

## 8. Test ownership and CMake wiring

Create these test-only targets and CTest entries:

| Target | CTest name | Ownership |
|---|---|---|
| `pnga_generate_wp607c_corpus` | `wp607c_generate_corpus` | CTest setup fixture that writes the 19 files and `index.json`; it is not by itself a corpus PASS claim |
| `scripts/verify_wp607c_manifest.py` | `wp607c_manifest_tests` | Manifest schema, coverage, hashes, linked-test existence and double-generation equality |
| `pnga_wp607c_png_facts_tests` | `wp607c_png_facts_tests` | Pixel/filter/Adam7/chunk facts and libpng differential checks |
| `pnga_wp607c_trace_facts_tests` | `wp607c_trace_facts_tests` | Blocks, tokens, cross-IDAT mapping and malformed stop facts |
| `pnga_gui_wp607c_corpus_tests` | `gui_wp607c_corpus_tests` | Real-file GUI state, navigation, narrow widths and error preservation |

CTest uses a setup fixture named `wp607c-generated-corpus`; file-consuming
tests declare it through `FIXTURES_REQUIRED`. All WP-607C entries carry labels
`corpus;wp607c`; the GUI entry also carries `gui`.

`scripts/run_wp607c_corpus_gate.py` is the single operator entry point. It:

1. builds the generator and owning targets for the selected preset;
2. generates into two fresh build-tree directories;
3. validates manifest/catalog equality and SHA-256 equality;
4. runs `ctest -L wp607c --output-on-failure`;
5. writes deterministic JSON to `build/evidence/wp-607c-corpus.json`.

The record contains schema version, Work Package, status, commit, UTC time,
OS/build/compiler/Qt facts, manifest SHA-256, generator-source SHA-256,
aggregate corpus revision, command, each output SHA-256 and each CTest result.
Volatile machine values do not participate in the corpus revision.

## 9. Existing-test integration

- `tests/differential/differential_harness_test.cpp` consumes the five pixel
  cases in addition to retaining its broad generated matrix.
- `tests/performance/performance_runner.cpp` consumes
  `perf-large-rgba8` from the shared fixture factory and emits the aggregate
  WP-607C corpus revision.
- `scripts/run_performance_corpus.py` copies that revision into its record.
- Existing Deflate unit tests keep focused synthetic boundaries. Where their
  local helper exactly duplicates a WP-607C builder, migrate construction to
  the shared fixture library without weakening assertions.
- Existing base64 GUI samples remain only when they test a distinct behavior.
  WP-607C scenarios use stable case ids rather than adding new embedded binary
  strings.

No existing test is removed merely because a corpus-level equivalent exists.

## 10. File map

Create:

- `tests/corpus/controlled_fixture.h` — case ids, expected-fact types and
  in-memory construction API;
- `tests/corpus/controlled_fixture.cpp` — PNG chunks, explicit bit writers and
  the 19-case registry;
- `tests/corpus/generate_controlled_corpus.cpp` — safe build-tree writer and
  deterministic JSON index;
- `tests/corpus/png_facts_test.cpp` — pixel, filter, Adam7 and differential
  assertions;
- `tests/corpus/trace_facts_test.cpp` — Blocks, tokens, cross-IDAT and malformed
  assertions;
- `tests/corpus/CMakeLists.txt` — test-only library, generator and CTest setup
  fixture;
- `tests/gui/controlled_corpus_gate_test.cpp` — real-file GUI corpus gate;
- `scripts/verify_wp607c_manifest.py` — deep YAML/catalog/hash and linked-CTest
  contract verifier;
- `scripts/run_wp607c_corpus_gate.py` — operator gate and evidence writer.

Modify:

- `tests/corpus/manifest.yaml` and `tests/corpus/README.md`;
- `tests/CMakeLists.txt` and `tests/gui/CMakeLists.txt`;
- `scripts/verify_repository_layout.py` and
  `scripts/verify_dependencies.py`;
- `tests/differential/differential_harness_test.cpp` and its CMake target;
- `tests/performance/performance_runner.cpp`, its CMake target and
  `scripts/run_performance_corpus.py`;
- only the Deflate test files whose duplicate builders are migrated;
- `docs/development/wp-607-cross-platform-quality-evidence.md` for package
  status and handoff linkage.

No file under `libs/`, `ui/qt/`, `apps/`, `third_party/`, `.github/` or
`packaging/` is authorized by this package.

## 11. Interfaces

The test-only fixture library exposes:

```cpp
enum class ControlledCaseId;

struct ControlledFixture {
  ControlledCaseId id;
  std::string_view stable_id;
  std::vector<std::byte> png_bytes;
  ExpectedFacts expected;
};

std::span<const ControlledCaseId> all_controlled_cases() noexcept;
ControlledFixture make_controlled_fixture(ControlledCaseId id);
std::optional<ControlledCaseId> controlled_case_id(std::string_view stable_id);
```

`ExpectedFacts` is a typed test-side variant covering image, block, token,
physical-span, checksum and expected-error facts. Consumers must not parse
human-readable diagnostic strings to recover numeric facts.

The generator command is:

```text
pnga_generate_wp607c_corpus --output <build-tree-directory>
```

It writes the full catalog atomically through a sibling temporary directory,
then renames the completed directory. Existing output is replaced only after a
complete successful generation. Failure leaves the last complete catalog
unchanged and returns non-zero.

## 12. Safety, determinism and failure rules

- All length, offset and allocation arithmetic uses checked operations before
  conversion to `size_t` or container growth.
- Generator arguments are bounded; the fixed large case is the maximum output
  authorized by this package.
- Case enumeration, manifest entries, JSON arrays and fact maps use stable
  order independent of locale, filesystem enumeration and test order.
- JSON is UTF-8, compact, LF-terminated and contains no absolute paths.
- Generation uses no clock, random device, process id or host-specific input.
- Output filenames derive only from stable case ids.
- Each generated PNG is written only after its bytes and SHA-256 are complete.
- The gate rejects unknown manifest keys, duplicate ids/outputs, missing
  categories, unregistered linked tests, path traversal and source-tree output.
- `NOT_CONFIGURED` cannot satisfy the required Qt GUI cell on the approved
  WP-5U12 baseline.

## 13. Delivery slices and exit gates

The implementation is serial:

1. **Schema and verifier gate** — manifest accepts generated/external kinds and
   rejects invalid provenance or generated records before fixture code exists.
2. **Fixture library and valid pixel cases** — five image cases reproduce
   exactly and pass differential checks.
3. **Valid Trace cases** — four explicit DEFLATE cases freeze Block/Huffman/
   token facts.
4. **Cross-IDAT and malformed cases** — nine boundary/error cases freeze spans
   and stop facts.
5. **GUI and performance consumers** — real files drive Qt states and the
   existing performance runner reports the shared revision.
6. **Corpus gate and evidence** — double generation, hashes, labeled CTest and
   the JSON gate record pass from a clean build tree.

Each slice begins with a failing focused test, ends with its own CTest target
passing, and is independently reviewable. Slices must not be implemented in
parallel because later slices consume the registry and schema frozen earlier.

## 14. Verification

Required commands from the WP-5U12 worktree:

```text
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
python3 scripts/run_wp607c_corpus_gate.py --preset dev --jobs 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure -j 4
python3 scripts/run_sanitizer_fuzz_gate.py
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds
git diff --check
```

The corpus gate must also be run once from a fresh build directory so stale
generated files cannot satisfy coverage.

## 15. Completion definition and handoff

Report exactly one status:

- `PASS`: all 19 manifest entries are complete, two fresh generations are
  byte-identical, every expected hash/fact and owning CTest passes, the GUI cell
  is configured and passing, performance records the same corpus revision, all
  verification commands pass and the worktree is clean after committed work.
- `BLOCKED`: a separately scoped production defect, missing approved baseline
  capability or unavailable required Qt environment prevents completion after
  all local corpus work is finished.
- `FAIL`: generated outputs, expected facts, hashes, tests or evidence disagree.

On PASS, record the aggregate corpus revision and gate-record SHA-256 in the
WP-607 parent document. WP-5U12F then re-runs
`run_wp607c_corpus_gate.py` as its first preflight step. WP-607C PASS supplies
fixtures and facts; it does not imply WP-5U12F, WP-607A/B/D or product release
PASS.
