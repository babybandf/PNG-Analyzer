# WP-5U12 Completion — Product Compression Inspector

Status: **PASS — WP-5U12 complete** (closed 2026-09-04 via the WP-5U12F
product gate; closing record below)

Normative UI contract: `wp-5u12-compression-inspector-flow-ui.md` section 20.
This document converts that audited contract into ordered coding increments.

## Goal and invariants

Complete a product-level, bounded Compression Inspector that answers Block
structure, Huffman construction and token decode questions without generating
or retaining a default whole-file token trace.

- One generation-level Fast Compression Index owns wrapper/IDAT/Adler summary
  and the complete Block list.
- Selection-level Deep Trace owns bounded codebooks/tokens and remains
  cancelable, budgeted and generation-safe.
- Offset domains are explicit: File bytes, zlib-stream bytes, DEFLATE-payload
  bits and Inflated bytes are never interchangeable.
- Logical input spanning multiple IDAT chunks retains every physical span.
- GUI formats immutable facts; it does not parse, Inflate or reverse bit order.

## Dependencies

- WP-5U15: PASS.
- WP-607C controlled corpus may proceed in parallel, but its fixtures must be
  available before WP-5U12F.
- Existing WP-5T0A/B, WP-505A/B/C and WP-5U13 remain authoritative.

## Allowed paths

- `libs/analysis-engine/**`
- focused changes in `libs/deflate-index/**` and `libs/deflate-trace/**` only
  when required to expose an already-computed structured fact
- `ui/qt/**`, `apps/png-analyzer-gui/**`
- focused unit/GUI/integration/performance tests
- this document and the normative WP-5U12 document

## Forbidden paths

- `third_party/**`, PNG parser/filter/reconstruction behavior, packaging
- private zlib/libpng structures or libpng links outside the approved backend
- whole-file token/event retention, IDAT concatenation or unbounded GUI tables
- debug-string parsing, hard-coded demo rows or first-span-only navigation

## WP-5U12A — Offset and Fast Index contract

Define typed ranges for the four offset domains. Extend
`FastCompressionIndexView` with stable wrapper fields, all IDAT physical spans,
DEFLATE payload origin, expected/actual Adler status, completion status and the
complete Block list. Block ranges use one documented origin; token ranges carry
their own domain and may not be compared until normalized by analysis-engine.

Tests: header boundaries, one/many IDAT, Stored/Fixed/Dynamic, cross-IDAT Block,
Adler match/mismatch/not-computed, truncated stream, reserved BTYPE and exact
half-open ranges.

## WP-5U12B — Selection and navigation contract

Introduce a typed navigation value containing `generation`, source domain,
logical range, optional bit range and all physical spans. Separate immutable
Current mapping from Manual Selection. Page selection never changes current
pixel context; current context never silently clears manual selection.

Tests: complete multi-span round-trip, stale generation rejection, Current +
Selection coexistence, navigation loop suppression, File/Frame-independent
source units and correct first/next history.

## WP-5U12C — Blocks page

Use the complete Fast Index even without X/Y Lock. Show stream summary, Block
type/BFINAL, compressed and output ranges, size, IDAT spans and available
Stored/Dynamic metadata. Current and Manual Selection use text/shape in addition
to color. Actions are exactly `Show in Hex`, `Show inflated output` and
`Open Decode Trace` with typed targets.

## WP-5U12D — Huffman page

For Dynamic blocks show code-length, literal/length and distance construction
in order. For Fixed show predefined tables; Stored shows an explicit no-Huffman
state. Main columns are Symbol, Meaning, Bits, Canonical, Read order and Uses in
current trace. Hide zero-bit entries by default. Canonical and read-order values
are fixed-width binary strings produced by Qt-free projection code.

## WP-5U12E — Decode Trace page

Show bounded scope in the title. Rows distinguish Literal, Match and EOB and
include input bits, output range and semantic event. Match details include
length/distance base and extra bits, source/target, overlap and selected-byte
offset. `Show in Hex` means compressed input; `Show inflated output` means
output. The two operations must never share an untyped integer payload.

## WP-5U12F — Product gate

Verify 320/360/480/600-pixel inspector widths, Light/Dark, keyboard navigation,
copy, accessibility names, virtualized scrolling, loading/empty/partial/error,
large trace response and memory. Table rows use models, not one QWidget per
event. Page switches, row selection, resize and DEC/HEX enqueue zero replays.

## Follow-up — Interactive content column widths (2026-09-05)

Approved by the product owner in the implementation request: update the UI
constraint and implement content-derived initial widths, draggable content
columns, double-click content fitting, and same-document width preservation.
This follow-up supersedes persistent Stretch/fixed content-column requirements
in section 20.5; the historical F closing record below describes the old UI.

Dependencies: the existing C/D/E model-backed pages and F regression harness
are present at f44acc8. Scope: `ui/qt/src/*inspector.cpp`, focused
`tests/gui/*inspector*test.cpp`, this document and the normative flow UI document.
No model/public API, decoder, dependency, architecture or corpus changes.
Current remains fixed at 28 px; all content columns use content-derived initial
widths and allow mouse dragging. Overflow scrolls inside the table. Same-document
publication and selection preserve widths; opening another document refits;
Huffman table-kind changes retain the existing explicit refit policy.
Double-clicking a content-column boundary fits that column using Qt's bounded
content measurement. No interaction submits analysis work.

Cheapest discriminating test: drag the formerly Stretch columns in each real
page at 360 px, verify width growth and internal scrolling, republish the same
document and verify preservation, then double-click to refit. Also verify the
last content column and fixed Current marker.

Verification: layout/dependency verifiers, dev configure/build, focused GUI
inspector/responsive/product-gate/no-replay/performance tests, full dev CTest,
and focused ASan/UBSan GUI tests where the approved local toolchain is available.
Capture the existing 22 visual cases for review; prior screenshots are historical
references for the superseded widths, not authorization to hide changed geometry
by increasing tolerances. Do not overwrite them automatically.

### Follow-up verification record (2026-09-06)

Status: **PASS** for this UI follow-up. Worktree/branch:
`.worktrees/compression-column-interaction` / `compression-column-interaction`.
The product owner explicitly allowed the existing Homebrew Qt 6.11.1 for this
local validation; no dependency manifests or presets were changed.

Both `dev` and `asan` were configured with the following local overrides:

```text
cmake --preset <dev|asan> -DCMAKE_TOOLCHAIN_FILE=/Users/lijiangbo/project/PNG-Analyzer/.deps/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_INSTALLED_DIR=/Users/lijiangbo/project/PNG-Analyzer/build/vcpkg_installed -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
cmake --build --preset asan --target pnga_gui_block_inspector_tests pnga_gui_huffman_inspector_tests pnga_gui_decode_trace_inspector_tests pnga_gui_compression_inspector_responsive_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset asan -R 'gui_(block_inspector|huffman_inspector|decode_trace_inspector|compression_inspector_responsive)_tests' --output-on-failure
python3 scripts/run_wp_5u12_gui_gate.py --preset dev --jobs 4 --output build/gui-gate/column-interaction/evidence.json --capture-dir build/gui-gate/column-interaction/captures
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
git diff --check
```

Results: full dev CTest 57/57; focused ASan/UBSan 4/4; product gate 25 Qt
test cases, 22 captures; both static verifiers zero failures/warnings;
whitespace check clean. Existing inspector/product-gate baseline tests passed
before implementation. The new real mouse test reproduced locked Stretch
columns and the last-column refit failure before the change, then passed all
seven scenarios after it. Scroll-range assertions wait for Qt's deferred layout.

Reviewed the three 360 px light captures for content/overflow layout. Existing
tracked screenshots were not overwritten, and no old-baseline pixel comparison
is claimed for this intentionally changed column geometry. This is automated
offscreen interaction validation, not a new native screen-reader certification.
Existing offscreen font/size-hint and duplicate-library linker warnings remain.
Review: only three UI implementations, five GUI tests and two owning documents
changed; no input arithmetic, buffer ownership/copies, cancellation, model facts
or replay submission paths changed. The root worktree's unrelated audit file
was preserved.

## Verification

```text
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'block_inspector|huffman_inspector|decode_trace|compression_inspector|trace_pipeline' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_sanitizer_fuzz_gate.py
python3 scripts/run_performance_corpus.py --preset dev
git diff --check
```

## Completion definition

All requirements in normative WP-5U12 section 19 and this document must pass.
Any missing offset origin, truncated physical range, falsely complete bounded
count, unbounded retention or unresolved UI state is `FAIL`, not a known
limitation. Architecture/dependency conflicts are `BLOCKED`.

## Closing record — WP-5U12 PASS (2026-09-04)

Per the completion definition above: every automated command exited 0, every
manual-only cell is explicitly recorded PASS, the side-effect audit found no
production change, and no required evidence is missing. Status: **PASS**.
Full requirement-to-evidence mapping: `docs/evidence/wp-5u12-product-gate.md`.

### Final command matrix (all exit 0)

Executed 2026-09-04 on `wp-5u12-compression-inspector` at `630b8cb` (macOS
26.6.2 arm64, Qt 6.11.1, `QT_QPA_PLATFORM=offscreen` where applicable;
packaging smoke intentionally excluded):

| Command | Result |
|---|---|
| `python3 scripts/verify_repository_layout.py` | 0 failures, 0 warnings |
| `python3 scripts/verify_dependencies.py` | 0 failures, 0 warnings |
| `cmake --preset dev` | configured |
| `cmake --build --preset dev --parallel 4` | up to date |
| `ctest --preset dev -R 'block_inspector\|huffman_inspector\|decode_trace\|compression_inspector\|trace_pipeline\|selection_navigation'` | 8/8 passed |
| `ctest --preset dev` | 53/53 passed |
| `python3 scripts/run_gui_gate.py --preset dev --jobs 4` | PASS, 3/3 suites |
| `python3 scripts/run_wp_5u12_gui_gate.py … --compare-baselines` | PASS, 22/22 |
| `cmake --preset asan && cmake --build --preset asan --parallel 4 && ctest --preset asan` | 53/53 passed |
| `python3 scripts/run_sanitizer_fuzz_gate.py --preset asan --jobs 4` | PASS (2 deterministic replays + fuzz smoke) |
| `python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds` | PASS, thresholds enforced |
| `git diff --check` | clean |

### Test, sanitizer and performance counts

- CTest: focused inspector set 8/8; full dev suite 53/53; ASan+UBSan suite
  53/53; product-gate test binary 25/25 test functions
  (`CompressionInspectorProductGateTest`).
- Sanitizer: ASan+UBSan clean across 53/53; fuzz gate PASS.
- Enforced performance scenario `compression-inspector` (corpus
  `wp607c-static-v1`, revision
  `5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f`,
  `perf-large-rgba8` 1024×768 RGBA8, 49 Stored blocks) — threshold (µs) →
  measured (µs): `fast_index` 250,000 → 9,444; `trace_query_4096` 1,000,000 →
  198,191; `huffman_model` 50,000 → 684; `decode_trace_model` 50,000 → 4,142;
  `first_visible_rows` 25,000 → 23; `visible_row_reads` 25,000 → 9. Checksum
  13,258,482. All reviewed maxima in `tests/performance/thresholds-v1.json`
  enforced and passing.

### 22 visual baselines

22/22 baseline comparisons PASS (2 px border + antialias envelope only),
exact plan-pinned matrix, no missing/unexpected baseline. Baselines reviewed
and APPROVED by the product owner (2026-09-03) and locked in `630b8cb`;
per-baseline SHA-256 table in the evidence doc §2.3. The 320 px row is
asserted but never captured (below the lowest capture width, per contract).

### Accessibility and manual matrix

- QAccessible metadata: exact accessible names/roles asserted per row by the
  product gate and `gui_compression_inspector_responsive_tests` — PASS.
- Manual native-OS cells M-1…M-11 (screen-reader observations, native
  rendering, open/close/reload/rapid-switch, Chunk/Reconstruction/Pixels/
  Filtered/Defiltered panels, image/X/Y/Lock/DEC-HEX, all Hex sources,
  Inspector workspace restore, keyboard-only workflow, clipboard, native
  theme, high-DPI): **11/11 PASS**, executed by the product owner on native
  macOS (Apple Silicon), 2026-09-03 (VoiceOver observations for M-1 recorded
  by the product owner).

### No-replay statement

The gate's no-replay assertions (via `PNGA_TRACE_CONTROLLER_TESTING`
counters) verify that page switch, row selection, resize, DEC/HEX toggle,
history navigation, theme switch and copy submit enqueue **zero trace
replays** (`acceptedRequestCountForTest` unchanged across the full action
matrix); stale generations are rejected. Gate: `no_replay: pass`.

### Terminal commits (A–F)

```text
A (offset + fast index):  1c29246 1df02c2 2f2d8f2 3eed97b
B (selection/navigation): 2b2e4e7 9dcc1a1 d12197f
C (blocks page):          59b48c4 e0d99a7 ba0196d 9a17c70 992d0a3
D (huffman page):         e30b1f1 657fb75 dea6458 4fb43e3 e2daf18 f672720
E (decode trace page):    a99ce15 e3e1469 d2b6edc edd0c07 157c5d3 81f322f
F (product gate):         bd2f40d bb14819 41609ab 958c6d3 37593da 630b8cb d22c089 + this closing commit
```

### Evidence hashes

- Corpus revision:
  `5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f`
  (covers exactly `tests/corpus/manifest.yaml`, `controlled_fixture.h`,
  `controlled_fixture.cpp`, `generate_controlled_corpus.cpp`).
- Final-gate GUI evidence record (schema `pnga-wp5u12-gui-evidence-v1`,
  generated, untracked):
  `06649ef6984b2fe2b3298446fa8e97d9930ae37f693dd0518d75a2e2ab84ef71`;
  the Mandatory Task Exit Gate replay after this commit re-runs the 22/22
  comparison at the closing commit and rewrites the record with fresh
  commit/timestamp fields (tracked record of the recorded run: hash above).
- Baseline set: exactly the 22 plan-pinned names under
  `tests/gui/baselines/wp-5u12/` (SHA-256 per file in evidence doc §2.3).
- Performance record (generated, untracked):
  `529d5b1bba428aa38183816cbffbb65b76cc465467bd5dacfdf56b42cbe1d8e6`.

### Known native-platform coverage boundaries

- Manual regression cells were executed on **macOS native (Apple Silicon)
  only**. Windows and Linux native behavior (window chrome, native
  screen-reader certification such as NVDA/ORCA, system theme switching,
  high-DPI variants) was **not manually exercised by this gate** — recorded
  as a coverage boundary, not a failure; automated offscreen suites and the
  cross-platform GUI gate (3/3 suites) remain the covering evidence there.
- Native screen-reader coverage is limited to VoiceOver observations recorded
  by the product owner on macOS; other screen readers are out of scope of
  this gate's manual record.
- Packaging smoke is intentionally excluded from WP-5U12 per plan.
