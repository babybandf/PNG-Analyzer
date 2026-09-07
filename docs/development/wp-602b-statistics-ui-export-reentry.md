# WP-602B–H — Statistics UI, CLI and Deterministic Export Re-entry

Status: **RE-OPENED — quality review failed; re-acceptance in progress** (2026-09-07).
The 2026-09-06 PASS record below is retained as history; the quality review
`docs/development/wp-602b-h-quality-review-2026-09-07.md` found three defects
(fake-complete Overview totals, unbounded Block pre-scan, wrong Stored-EOB
physical anchor) that block re-closure. The fix plan is
`docs/superpowers/plans/2026-09-07-wp-602-statistics-quality-fixes.md`.

## Quality-fix scope supplement (2026-09-07)

The original B–H allowed paths did not include `libs/deflate-index/**`. This
re-opening authorizes a narrow supplement to that path only:

- a new bounded, cancelable index entry point alongside the existing
  `index_blocks(source, max_output_bytes)`, which stays callable with its
  original error semantics;
- the bounded entry reports typed stop reasons (`complete`,
  `budget_exceeded`, `cancelled`, `invalid_input`), honors independent
  input-byte, output-byte, block-count and retained-byte limits, and keeps
  the verified Block prefix plus stop coordinates on an early stop;
- no decode-algorithm change, no second Inflate implementation, no new
  dependency, no ownership or dependency-direction change.

Beyond this supplement, any further `libs/deflate-index` change requires its
own ruling.

Binding review:
`docs/development/wp-602b-h-written-package-review.md`.

Implementation plan:
`docs/superpowers/plans/2026-09-05-wp-602b-h-statistics-ui-export.md`.

## Goal

Expose the existing Qt-free Statistics capability through a lazy, cancelable
whole-document analysis, a Statistics Inspector, and byte-identical JSON/CSV
serialization shared by GUI and CLI.

## Dependencies

- WP-602A Statistics Engine: PASS.
- WP-5U15: PASS.
- WP-5U12A Fast Compression Index: PASS before full Block statistics.
- APNG and Compare semantics are forbidden in schema version 1.

## Architecture invariants

- `pnga_statistics` remains Qt-free and never parses, reads files or decodes.
- Whole-document Token statistics stream decoded events into an accumulator and
  discard each event; no whole-file token vector is retained.
- Collection runs below interactive Selection work, is cancelable and checks
  document generation before publication.
- Chunk/Filter/Block/Token sections carry independent `status`, `complete` and
  `scope`; unavailable or partial data is not represented as zero.
- GUI and CLI serialize the same immutable snapshot through one implementation.

## Allowed paths

- `libs/statistics/**`, `libs/analysis-engine/**`
- focused observer support in `libs/deflate-trace/**` that does not change
  decode semantics or retain events
- `apps/pnga-cli/**`, `ui/qt/**`, `apps/png-analyzer-gui/**`
- Statistics unit, CLI integration, GUI and performance tests
- user/CLI documentation and this document

## Forbidden paths

- PNG parser/reconstruction behavior, third-party code, Compare or APNG fields
- Qt under `libs/**`, full payload copies, unbounded buckets/occurrences
- locale-sensitive formatting, timestamps in report bodies or map iteration
  order that changes output bytes

## WP-602B — Snapshot and schema v1

Replace the single global completion assumption with per-section state while
preserving WP-602A validated prefixes. Freeze this JSON envelope:

```text
schema: "pnga.statistics"
schema_version: 1
document: {file_size, fingerprint}
sections: {overview, chunks, filters, blocks, tokens, lengths, distances}
```

Every section contains `status`, `complete`, `scope`, totals and deterministically
ordered buckets. `fingerprint` is content-derived and excludes absolute paths.
Golden tests cover ready, partial, cancelled, budget, invalid and overflow.

CSV contains exactly:

```text
schema_version,section,metric,key,value,unit
```

Status, complete and scope are metric rows. Encoding is UTF-8 without BOM, line
ending LF, decimal ASCII integers and RFC 4180 quoting. Empty is distinct from
zero. Ordering is Overview, Chunks, Filters 0–4, Blocks Stored/Fixed/Dynamic,
Tokens Literal/Match/EOB, Length ascending, Distance ascending.

## WP-602C — Whole-document streaming aggregation

Add a scalar observer path to the project's owned DEFLATE decoder. It receives
Block/Token facts synchronously and cannot retain borrowed payload. The
Statistics orchestrator combines cached complete Chunk/Filter/Block facts with
the streaming Token pass under `StatisticsLimits`, a cancellation predicate,
generation and progress callback.

Default limits remain WP-602A's 2^20 samples, 1024 Chunk types and 65,536
length/distance buckets. The job reserves at most 64 MiB and publishes progress
no more frequently than 10 times per second. Cancellation and decoder error
return the validated prefix and exact stopped input/output range.

Tests compare streamed aggregation with hand-built scalar input for Stored,
Fixed, Dynamic, matches, overlap, cross-IDAT and malformed streams. Peak retained
Token events must remain O(1).

## WP-602D — Shared serializers

Add Qt-free JSON/CSV functions accepting only immutable snapshot plus document
identity. They return either complete bytes or a stable serialization error;
they do not write files. Tests switch C/CN/DE locales where available and
require byte-identical output, proper escaping and golden schema v1 files.

## WP-602E — CLI

Add:

```text
pnga statistics <file> --format json
pnga statistics <file> --format csv
```

Exactly one file and one format are required. Output goes to stdout; callers
redirect when a file is needed. Exit codes are 0 ready, 1 I/O, 2 format/argument,
3 validation issues with usable statistics, 4 statistics partial/cancel/budget.
Diagnostics go to stderr and never contaminate CSV/JSON stdout. `--help` lists
schema version and exit codes.

## WP-602F — View and occurrence query contract

Build immutable Qt-free rows for Overview, Chunks, Filters and DEFLATE. A row
contains stable ID, label, raw integer values, units, completeness and an
optional typed navigation request. GUI formatting occurs only for display.

Chunk navigation uses ChunkIndex, Filter navigation uses scanline anchors and
Block navigation uses Fast Index. Token/Length/Distance navigation submits a
bounded occurrence query for first/previous/next; it does not store all
occurrences. Requests carry generation, bucket identity, direction and a
4096-token/8 MiB replay budget. Budget exhaustion returns Partial with the
searched range.

## WP-602G — GUI and export

Add `Statistics` after `Compression`, with fixed pages
`Overview | Chunks | Filters | DEFLATE`. Static opening behavior remains Image
+ Reconstruction; no statistics job runs until the Statistics tab is first
shown or Export is invoked. Fast sections publish first; Token progress updates
without clearing verified rows.

Provide Refresh, Cancel, Export JSON, Export CSV and Show occurrence. Export is
enabled when at least one verified section exists and explicitly labels Partial
output. Save uses `QSaveFile` for atomic replacement and does not overwrite on
serialization/write failure. Selection publication uses existing loop guards.

## WP-602H — Gate

Test empty/malformed/large inputs, cancellation, rapid file switching, stale
publication, three locales, exact CLI/GUI byte equality, CSV importability,
bounded occurrence queries, 320-pixel Inspector width, keyboard/a11y and peak
memory. Statistics work must not increase file-open or hover latency.

## Verification

```text
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --preset dev -R 'statistics|cli|main_window|selection|gui' --output-on-failure
ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_performance_corpus.py --preset dev
python3 scripts/run_sanitizer_fuzz_gate.py
git diff --check
```

## Completion definition

`PASS` requires every B–H package, schema goldens, byte-identical GUI/CLI
exports, bounded memory/work and responsive UI. A schema ambiguity, falsely
complete section, retained whole-file events or locale-dependent byte is
`FAIL`. A required architecture change outside allowed paths is `BLOCKED`.

## WP-602H completion record (2026-09-06)

Branch `wp-602b-h-statistics-ui-export`; the evidence below was produced by
the Task 9 gate commit. All commands ran from the repository root with exit
code 0 unless stated otherwise.

Verification matrix (exact commands, all exit 0):

1. `python3 scripts/verify_repository_layout.py`
2. `python3 scripts/verify_dependencies.py`
3. `cmake --preset dev`
4. `cmake --build --preset dev --parallel 4`
5. `QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'statistics|cli|main_window|selection|gui' --output-on-failure` — 41/41 pass
6. `QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure` — 60/60 pass (was 59 before the product gate)
7. `python3 scripts/run_gui_gate.py` — PASS, three configured scales
8. `python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds` — PASS
9. `python3 scripts/run_sanitizer_fuzz_gate.py` — PASS (`asan` preset configured in the worktree; 2 deterministic replays + fuzz smoke)
10. `git diff --check`

Product gate: `gui_statistics_product_gate_tests` (tests/gui/
statistics_product_gate_test.cpp) — 8 slots, all pass under the offscreen
platform in ~3 s: lazy start with zero statistics activity on file open and
200 delivered hover events; ready full pass (four frozen pages, honest
sections); malformed verified prefix with explicit Partial labeling; large
bounded collection with `budget_exceeded` token prefix (never falsely
complete); rapid switch and stale generation/occurrence non-publication;
cancel with verified rows retained; GUI export == shared serializer == real
CLI stdout bytes (JSON and CSV; `ui-gray1-none` exit 0); strict RFC 4180 CSV
importability; 320 px Inspector; keyboard chains and accessible names/roles.

Performance (`statistics` scenario on WP-607C `perf-large-rgba8`,
thresholds-v1.json):

- `fast_sections_us` = 99,272 (max 500,000). Measured as the time to the
  first token-scan progress publication; the whole collection finished
  inside the 100 ms production throttle window, so the conservative whole
  duration is reported.
- `whole_document_us` = 99,272 (max 5,000,000)
- `token_count` = 1,048,576 — the frozen WP-602A default sample budget; the
  token section stays `budget_exceeded`/incomplete (2^20 < 2,359,296 stored
  literals), never a falsely complete section
- `peak_retained_token_records` = 1 (non-time invariant, enforced by the
  runner and the GUI gate over the complete 2.36M-token stream)
- `view_projection_us` = 69 (max 250,000)
- `serializer_us` = 91 (max 250,000)
- Non-time invariant: declared `max_working_bytes` = 64 MiB, the frozen
  background reservation (asserted by the runner and the GUI gate)

Schema golden SHA-256 (tests/unit/statistics/golden/):

- `ready-v1.json` — `07be9dca520763884f5a9461ea8b15de7806f3351843f1bc83e4d6c0045c3bd9`
- `ready-v1.csv` — `c4cd4ec373c20ad1f7e847729bb69d215145a8e6848263e2ff3f24e49e88ae23`
- `partial-v1.json` — `e68a584e3b39cb1e1dca38ea8134ff94037e79c4cb22ef4eb42019ad681aece5`
- `partial-v1.csv` — `1b0106cba1d949adbe7fadc9fbe9cc6e9ccfadd171c30d202fd38e8e86682dda`

Locale list: `C` baseline plus `de_DE`, `ru_RU` and `zh_CN` display passes on
the real MainWindow; the DisplayRole follows the current QLocale while every
JSON/CSV export stays byte-identical. The committed goldens additionally
cover the C, `C.UTF-8` and non-English C-locale passes of the serializer.

Honest-status notes: the malformed `error-truncated-token` fixture raises no
structural validation issue (the corruption is decoder-level), so the frozen
CLI exit mapping yields 4, not 3; invalid_input sections keep their verified
prefix values (zeros are real zeros), and the empty-versus-zero distinction
is pinned by the committed `partial-v1.csv` golden.

Changed-path audit (Task 9 only; no production, parser, reconstruction or
dependency change): `tests/gui/statistics_product_gate_test.cpp` (new),
`tests/gui/CMakeLists.txt`, `tests/performance/performance_runner.cpp`,
`tests/performance/thresholds-v1.json`, `tests/performance/README.md`, this
document, and
`docs/architecture/png-analyzer-current-development-plan-2026-08-22.md`.
`build/asan` was configured in the worktree to run the sanitizer gate; it is
not a repository path.

## WP-602 quality-fix completion record (2026-09-07)

Status: **PASS — re-accepted** on the strength of the evidence below. The
three quality-review defects are fixed, regressed and gated on the final
commits of this branch.

### Final code commits (branch `wp-602-statistics-quality-fixes`)

1. `432f4db` — fix: preserve unknown statistics totals (quality fix 1)
2. `db3b1de` — fix: bound statistics block scans and cancellation (fix 2)
3. `b3fec4a` — fix: map stored EOB anchors to physical IDAT offsets (fix 3)
4. the evidence/gate commit that contains this record

The commit hashes above contain the code they describe; this record and the
final gate run belong to the following commit and were produced from the
same tree state.

### Platform and toolchain

- macOS 26.6.2, arm64 (Darwin), Apple clang 21.0.0, CMake 4.4.2, Ninja.
- Dependencies via the pinned vcpkg manifest (baseline `9e593bb18ea69cc5`,
  overrides libpng 1.6.58, zlib 1.3.2, catch2 3.11.0); Qt 6 from the
  official installer. Python 3.9.6 for the gate scripts.

### Verification matrix (exact commands, all exit 0)

1. `python3 scripts/verify_repository_layout.py` — 0 failures
2. `python3 scripts/verify_dependencies.py` — 0 failures
3. `cmake --preset dev` / `cmake --build --preset dev --parallel 8` — clean
4. `QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'statistics|cli|main_window|selection|gui'` — 41/41 pass
5. `QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure` — 60/60 pass (actual ctest count read from the run output, not a hardcoded figure)
6. `python3 scripts/run_gui_gate.py` — PASS
7. `python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds` — PASS
8. `python3 scripts/run_sanitizer_fuzz_gate.py` — PASS (ASan/UBSan preset; 2 deterministic replays + fuzz smoke)
9. `git diff --check` — clean
10. Focused per-task runs during development: `deflate_index` 31/31 cases, `analysis_engine` 142/142 cases, statistics 20/20, CLI integration 13/13, GUI controller 14/14, GUI product gate 10/10.

### Regression evidence per defect

- **Overview fake-complete totals**: the malformed `error-truncated-token`
  fixture previously exported `overview: ready, complete=true, inflated=0`;
  it now reports `invalid_input`, `complete=false`, `scope=none` with both
  values null/empty (CLI exit 4 unchanged). A valid 65-empty-stored-block
  fixture with a 32-sample budget previously did the same; it now reports
  `budget_exceeded` with the pair absent. The ready and partial schema
  goldens are byte-identical to the closed 2026-09-06 record
  (SHA-256 re-verified: ready-v1.json `07be9dca…5c3bd9`, ready-v1.csv
  `c4cd4ec3…e88ae23`, partial-v1.json `e68a584e…681aece5`, partial-v1.csv
  `1b0106cb…e86682dda`), and the serializer locale suite (C, `C.UTF-8`,
  `de_DE.UTF-8`, `fr_FR.UTF-8`) still passes byte-identically.
- **Unbounded Block scan / cancellation**: the 1,500,000-empty-block stress
  PNG (7,500,070 bytes, generated by the fix plan's appendix A script)
  previously peaked at 182,403,072 bytes of CLI process RSS with a
  post-hoc budget check; it now peaks at 34,865,152 bytes with blocks
  `budget_exceeded` (262,144-block verified prefix, retained capacity
  12,582,912 bytes ≤ the 32 MiB share), honest absent totals, and the
  token prefix at the frozen 2^20 budget. The read-counted cancellation
  workload stops at the trigger read with zero additional reads (limit:
  two). The occurrence pre-index is bounded by the request's 8 MiB input
  window with the same retained caps and cancels mid-scan.
- **Stored-EOB physical anchor**: the corrected unit oracle pins the
  anchor to the fixture's IDAT payload file start + 13; new regressions
  cover the payload-junction, empty-middle-IDAT, consecutive
  zero-output-block, and failed-index-prefix cases with exact file offsets,
  plus the GUI SelectionBus publication check.

### Actual working memory coverage

- Covered and enforced: the retained block vector (real capacity including
  old+new reallocation peaks, planned growth, post-reserve re-check), the
  index I/O buffers (128 KiB), the zlib inflate workspace, the accumulator
  buckets/histograms (≤ 2 MiB token histograms), snapshot/progress copies
  (O(bucket counts)), and the per-token O(1) scan retention.
- Recorded as out of scope: the CLI composition pre-builds a full StageSet
  (`analyze_source`) before the collector runs; that preprocessing
  allocation is not part of the declared collector working memory and is
  not claimed as covered. Process RSS is recorded in the performance
  record as auxiliary evidence only (`process_rss_peak_kib`).

### Not covered by this re-acceptance

- No native (non-offscreen) manual GUI clicking was performed; the macOS
  on-screen Stored-EOB Hex/Source navigation check remains an open manual
  item for the native GUI release environment. Offscreen results above do
  not substitute for it.
- No tag, push, merge or release was created.
