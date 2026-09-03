# WP-5U12F — Compression Inspector product gate evidence matrix

Status: matrix frozen before gate code (plan Task 1). This file is the living
requirement-to-evidence record for WP-5U12F; Tasks 3–5 append capture hashes,
command results and final statuses. Normative sources:
`docs/development/wp-5u12-compression-inspector-completion.md` (WP-5U12F and
completion definition) and `docs/development/wp-5u12-compression-inspector-flow-ui.md`
§13–14, §16–20. Row statuses: **PASS** (evidence exists today), **PENDING**
(automated check owned by a later WP-5U12F task), **MANUAL** (explicitly
manual-only cell; never inferred from QAccessible metadata). Missing evidence
is FAIL/BLOCKED, never a known limitation.

## 1. Prerequisite verification (plan Task 1 Step 1)

Executed 2026-09-03 on branch `wp-5u12-compression-inspector` at `8d2b152`:

- `python3 scripts/verify_repository_layout.py` → 0 failures, 0 warnings.
- `python3 scripts/verify_dependencies.py` → 0 failures, 0 warnings.
- Category grep over `tests/corpus tests/common tests/unit tests/gui tests/fuzz`
  → every plan-required category resolves to auditable records (table 1.1).
- WP-607C gate (`python3 scripts/run_wp607c_corpus_gate.py`) → PASS with 19
  byte-identical double-generated cases; aggregate corpus revision
  `5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f`.
  The revision covers exactly `tests/corpus/manifest.yaml`,
  `controlled_fixture.h`, `controlled_fixture.cpp`,
  `generate_controlled_corpus.cpp` (ruling R1 in `tests/corpus/CMakeLists.txt`).

### 1.1 Category → fixture audit (all generator-backed)

Every row: form = **generated** (build-tree only, never committed), generator
= `pnga_generate_wp607c_corpus --case <id>` over
`tests/corpus/controlled_fixture.cpp make_controlled_fixture(ControlledCaseId)`
with fixed arguments, exact expected facts pinned in
`tests/corpus/manifest.yaml` (SHA-256 per case), owning CTest targets
`wp607c_trace_facts_tests` / `wp607c_png_facts_tests` plus the GUI gate
`gui_wp607c_corpus_tests` (fixture `wp607c-generated-corpus`).

| Required category | Case id | Owning test |
|---|---|---|
| Stored | `trace-stored-literals` | `wp607c_trace_facts_tests`, `gui_wp607c_corpus_tests` |
| Fixed | `trace-fixed-nonoverlap` | `wp607c_trace_facts_tests` |
| Dynamic | `trace-dynamic-overlap-repeats` | `wp607c_trace_facts_tests`, `gui_wp607c_corpus_tests` |
| overlap match | `trace-dynamic-overlap-repeats` (`match_overlap`) | `wp607c_trace_facts_tests` |
| multi-Block | `trace-multiblock-bfinal` (Stored+Fixed+Dynamic, BFINAL last) | `wp607c_trace_facts_tests` |
| cross-IDAT | `idat-split-token`, `idat-split-adler`, `idat-split-zlib-header` | `wp607c_trace_facts_tests` |
| truncated | `error-truncated-header`, `error-truncated-token` | `wp607c_trace_facts_tests` |
| reserved BTYPE | `error-reserved-btype` | `wp607c_trace_facts_tests` |
| invalid distance | `error-invalid-distance` | `wp607c_trace_facts_tests` |
| Adler mismatch | `error-adler-mismatch` | `wp607c_trace_facts_tests` |
| narrow widths | width matrices 320/360/480/600 in `gui_compression_inspector_responsive_tests`, `gui_wp607c_corpus_tests`, product gate (§3) | GUI suites |
| large data | `perf-large-rgba8` (3,146,496 inflated bytes, 51 Stored blocks) | `wp607c_trace_facts_tests`, performance gate (Task 4) |

Adjudication (WP-607C package §7.5, binding for this gate): the malformed
cases `error-truncated-token` and `error-reserved-btype` have a DEFLATE
verified prefix with **zero complete blocks**, so production refuses to index:
the context shows its stable non-ready copy, the Blocks table stays empty
(zero invented rows) and no replay is ever submitted. The product gate asserts
exactly this reality for those two cases — no Partial/Error table state, no
replay — while `error-invalid-distance`/`error-adler-mismatch`-class behavior
remains covered by the bounded-trace Partial/Error contract on indexed
streams (flow-ui §13, exercised by `gui_compression_inspector_responsive_tests`
typed-view rows and trace unit tests).

## 2. Pinned visual baselines (plan-pinned, exactly 22)

`tests/gui/baselines/wp-5u12/` — reviewed baselines are committed only after
capture review (plan Task 3 Step 4; controller-owned after this wave):

```text
blocks-360-light.png            blocks-480-light.png            blocks-600-light.png
huffman-360-light.png           huffman-480-light.png           huffman-600-light.png
decode-trace-360-light.png      decode-trace-480-light.png      decode-trace-600-light.png
blocks-360-dark.png             huffman-360-dark.png            decode-trace-360-dark.png
blocks-480-dark.png             huffman-480-dark.png            decode-trace-480-dark.png
huffman-stored-360-light.png    loading-360-light.png
partial-error-360-light.png     partial-error-480-light.png
blocks-current-selection-480-light.png
decode-trace-current-selection-480-light.png
cross-idat-details-480-light.png
```

Plus the 320 px degradation row, which is asserted but **never captured**
(below the lowest capture width by contract).

### 2.1 Baseline → fixture/state mapping

| Baseline group | Fixture/state | Backing |
|---|---|---|
| `blocks-*`, `huffman-*`, `decode-trace-*` light/dark width matrices | `valid/trace-dynamic-overlap-repeats.png`, ready bundle, committed pixel (0,0) | pipeline |
| `blocks-current-selection-480-light` | `valid/trace-multiblock-bfinal.png`; Current on block #0, manual selection on block #2 | pipeline |
| `decode-trace-current-selection-480-light` | `valid/trace-dynamic-overlap-repeats.png`; Current on step #0, manual selection on the overlap match row | pipeline |
| `huffman-stored-360-light` | `valid/trace-stored-literals.png`, Huffman no-Huffman state | pipeline |
| `cross-idat-details-480-light` | `valid/idat-split-token.png`, Blocks details carrying both physical spans | pipeline |
| `partial-error-360-light` | `malformed/error-truncated-token.png`, stable non-ready context copy, 0 block rows | pipeline |
| `partial-error-480-light` | `malformed/error-reserved-btype.png`, stable non-ready context copy, 0 block rows | pipeline |
| `loading-360-light` | real `DecodeTraceInspector` page, typed `kReplaying` view (real pages cannot freeze mid-replay deterministically; precedent `gui_compression_inspector_responsive_tests`) | typed view |

## 3. Requirement matrix

Fixture column: `dyn` = `valid/trace-dynamic-overlap-repeats.png`,
`multi` = `valid/trace-multiblock-bfinal.png`, `stored` =
`valid/trace-stored-literals.png`, `xidat` = `valid/idat-split-token.png`,
`trunc` = `malformed/error-truncated-token.png`, `reserved` =
`malformed/error-reserved-btype.png`. All form **generated** (table 1.1).
Check column names the automated CTest entry; the product gate is
`gui_compression_inspector_product_gate_tests`
(`tests/gui/compression_inspector_product_gate_test.cpp`).

### 3.1 WP-5U12F scope (completion doc §WP-5U12F)

| ID | Requirement (summary) | Fixture | Automated check | Expected assertion | Evidence | Status |
|---|---|---|---|---|---|---|
| F-1 | 320/360/480/600 px inspector widths, no content-driven growth | dyn | gate `narrow320Degradation…` + every capture row; `gui_compression_inspector_responsive_tests`, `gui_wp607c_corpus_tests` | `page.minimumWidth() <= width`, width honored, viewport-internal scroll | ctest logs; captures §2 | PENDING (gate) / PASS (existing suites) |
| F-2 | Light/Dark themes | dyn | gate dark/light rows | Current background resolves through `ApplicationTheme::ColorToken::kCurrentPixel` per mode; no hard-coded RGB | captures §2 | PENDING |
| F-3 | Keyboard navigation | dyn | gate 320 row: focus + arrow keys on all three tables | selection moves via key events, focus ring distinct from Current highlight | ctest log | PENDING |
| F-4 | Copy | dyn | gate 320 row + per-row copy contract | detail value labels selectable (`Qt::TextSelectableByMouse`), clipboard round-trip works | ctest log | PENDING |
| F-5 | Accessibility names | all | gate per-row QAccessible metadata; `gui_compression_inspector_responsive_tests` | tables/buttons/context expose exact accessible names and Table/Button roles | ctest log | PENDING (gate) / PASS (existing) |
| F-6 | Virtualized scrolling, models not row widgets | dyn, `perf-large-rgba8` | gate rows (zero `QTableWidget`, model-backed `QTableView`); Task 4 `gui_trace_inspector_performance_tests` | `qobject_cast<QTableWidget*> == nullptr`, `QAbstractTableModel` under every table; 10k/4096-row budgets | ctest log | PENDING (gate) / PENDING (Task 4) |
| F-7 | loading/empty/partial/error states | trunc, reserved, typed kReplaying view | gate `loading-360-light`, `partial-error-*` rows; `gui_compression_inspector_responsive_tests` | stable copy, verified facts retained, no invented rows, no `no trace` | captures §2 | PENDING |
| F-8 | Large trace response and memory | `perf-large-rgba8` | Task 4 performance gate + thresholds | fixed maxima in `tests/performance/thresholds-v1.json` | `build/performance/**` | PENDING (Task 4) |
| F-9 | Page switch / row selection / resize / DEC-HEX / history / theme / copy submit zero replays | dyn | gate 320 row with `PNGA_TRACE_CONTROLLER_TESTING` counters | `acceptedRequestCountForTest` unchanged across the full action matrix | ctest log | PENDING |
| F-10 | Table rows use models, never one QWidget per event | dyn, multi | gate per-row; Task 4 | viewport holds no per-row QWidget | ctest log | PENDING |

### 3.2 Flow-ui §19 completion definition

| ID | Requirement (summary) | Fixture | Automated check | Expected assertion | Evidence | Status |
|---|---|---|---|---|---|---|
| R-1 | Three pages answer structure / code rules / decode process without duplication | dyn | `gui_wp607c_corpus_tests`, gate rows | Blocks table, Huffman tables, Trace rows all published from one generation | ctest logs | PASS / PENDING (captures) |
| R-2 | No unexplained `Block trace: no trace` after opening a valid PNG | dyn, stored, multi | `gui_wp607c_corpus_tests`, gate rows | status reaches `ready`; heading carries bounded scope facts | ctest logs | PASS |
| R-3 | Stored/Fixed/Dynamic/error blocks all show correct content or explicit state | stored, multi, trunc, reserved | `gui_wp607c_corpus_tests`, gate stored/error rows | Stored heading + no-Huffman copy; malformed keeps stable non-ready copy, 0 rows | ctest logs | PASS / PENDING |
| R-4 | Canonical code vs read-order bits clearly separated | dyn | `gui_huffman_inspector_tests`, gate Huffman rows | `Canonical` and `Read order` are distinct columns and details | captures | PASS / PENDING |
| R-5 | Literal/Match/EOB input+output ranges accurate | dyn, `idat-split-token` | `wp607c_trace_facts_tests`, `gui_decode_trace_inspector_tests` | exact half-open ranges from manifest facts | ctest logs | PASS |
| R-6 | Match length/distance/source/target/overlap/provenance accurate | dyn | `wp607c_trace_facts_tests`, `gui_decode_trace_inspector_tests` | typed step payload equality | ctest logs | PASS |
| R-7 | All IDAT analyzed as one stream, cross-chunk mapping accurate | `idat-split-token` | `wp607c_trace_facts_tests`, `gui_trace_pipeline_integration_tests`, gate cross-idat row | every physical span highlighted; details list all spans | ctest logs | PASS / PENDING |
| R-8 | Pixel → output range → event → block reverse mapping; explicit degradation | dyn | `gui_trace_pipeline_integration_tests` | Current rows marked; `mapping unavailable` when imprecise | ctest logs | PASS |
| R-9 | Bidirectional navigation accurate, no update loops | dyn | `gui_trace_pipeline_integration_tests`, gate navigation row | one request → one view update; stale generation rejected | ctest logs | PASS |
| R-10 | Current context and manual selection coexist, distinguishable | multi, dyn | gate current-selection rows; `gui_compression_inspector_responsive_tests` | `ContainsCurrentRole` + `IsManualSelectionRole` + native selection simultaneously | captures | PENDING (gate) / PASS (responsive) |
| R-11 | Page switch / coordinate change: no reparse, no re-Inflate, no full-trace scan | dyn | `gui_trace_pipeline_integration_tests`, gate no-replay row | counters unchanged, bundle generation stable | ctest logs | PASS / PENDING |
| R-12 | Virtualized/lazy models; bounded tables proven safe by object-count and response gates | dyn, `perf-large-rgba8` | Task 4 `gui_trace_inspector_performance_tests` | model/view assertions + thresholds | ctest + perf JSON | PENDING (Task 4) |
| R-13 | Narrow inspector, light/dark, keyboard, copy usable | dyn | gate rows F-1..F-4 | §3.1 matrix | captures + logs | PENDING |
| R-14 | partial/error keeps prior facts; no fabrication, no clearing, no raw debug strings | trunc, reserved | gate partial-error rows; `gui_compression_inspector_responsive_tests` | stable copy, table intact for indexed streams, human-readable stop reasons | captures | PENDING (gate) / PASS (responsive) |
| R-15 | Core/model/GUI/regression tests all pass | all | full `ctest --preset dev` | 100% pass | ctest logs | PENDING (final gate run, Task 5) |

### 3.3 Flow-ui §20 normative UI contract

| ID | Requirement (summary) | Fixture | Automated check | Expected assertion | Evidence | Status |
|---|---|---|---|---|---|---|
| U-20.2 | Fixed component tree and order (tab bar → context → page stack → footer actions) | dyn | gate per-row | y-order: context above pages; table above details; footer actions bottom-most; exactly one footer action row | ctest logs + captures | PENDING |
| U-20.3 | Geometry bands (header 26–31, row 26–32, buttons 26–32, split ≈55:45 with 4 rows + 120 px details minima) | dyn | gate + `gui_compression_inspector_responsive_tests` | band assertions per §20.3 | ctest logs | PENDING (gate) / PASS (responsive) |
| U-20.4 | 600/420–599/360–419/320–359 responsive behavior; no width-driven Inspector growth | dyn | gate width matrix + 320 row | Blocks Events/Scanlines hidden ≤480/≤360; Huffman/Trace keep all columns; 320 stacks footer vertically in order | ctest logs | PENDING |
| U-20.5 | Default column order and resize priorities | dyn | gate per-row | exact header sequences `Current|#|Type|Final|Input bits|Output bytes[|Events|Scanlines]`, `Symbol|Meaning|Bits|Canonical|Read order|Uses in result`, `Current|Step|Input bits|Event|Output`; Event stretch on Trace | ctest logs | PENDING |
| U-20.6 | Current / Selection / Current+Selection visual contract; Error/Partial keep tables; Loading/Empty copy | multi, dyn, trunc, reserved | gate current-selection + state rows; `gui_compression_inspector_responsive_tests` | `●` Current column + accessible text + token background ≠ native selection; both survive selection | captures | PENDING (gate) / PASS (responsive) |
| U-20.7 | Locked copy (`Show in Hex`, `Show inflated output`, headers, tab labels; no `Show in DEFLATE`, no `no trace`) | dyn | gate per-row | exact label equality set | ctest logs | PENDING |
| U-20.8 | Visual baselines and deformation gate (clipping, overlap, order, columns, Current/Selection distinction, footer duplication/order, min width, error clears table) | all §2 fixtures | gate captures + runner compare (2 px border + antialias envelope only) | 22/22 baseline comparisons PASS at final gate | `build/gui-gate/wp-5u12/**`, tracked baselines (Task 3 Step 4) | PENDING |

### 3.4 Flow-ui §16 fixture coverage (core golden layer)

| ID | Fixture item (§16.1 unless noted) | Corpus record / form | Automated check | Status |
|---|---|---|---|---|
| X-1 | single Stored block | `trace-stored-literals`, generated | `wp607c_trace_facts_tests` | PASS |
| X-2 | single Fixed block | `trace-fixed-nonoverlap`, generated | `wp607c_trace_facts_tests` | PASS |
| X-3 | single Dynamic block | `trace-dynamic-overlap-repeats`, generated | `wp607c_trace_facts_tests` | PASS |
| X-4 | multi-block, BFINAL only last | `trace-multiblock-bfinal`, generated | `wp607c_trace_facts_tests` | PASS |
| X-5 | block across two IDAT chunks | `idat-split-token` / `idat-split-adler` / `idat-split-zlib-header`, generated | `wp607c_trace_facts_tests` | PASS |
| X-6 | literal-only | `trace-stored-literals` (stored literals only), generated | `wp607c_trace_facts_tests` | PASS |
| X-7 | non-overlapping match | `trace-fixed-nonoverlap`, generated | `wp607c_trace_facts_tests` | PASS |
| X-8 | overlapping match | `trace-dynamic-overlap-repeats`, generated | `wp607c_trace_facts_tests` | PASS |
| X-9 | code-length repeats 16/17/18 | `trace-dynamic-overlap-repeats` (`expected_code_length_repeats`), generated | `wp607c_trace_facts_tests` | PASS |
| X-10 | legal empty distance tree boundary | in-memory generated, `tests/unit/deflate-trace/token_decoder_test.cpp` "Dynamic literal-only stream may have an empty distance table" | `unit_deflate_trace` target | PASS |
| X-11 | truncated header / token | `error-truncated-header` / `error-truncated-token`, generated | `wp607c_trace_facts_tests` | PASS |
| X-12 | BTYPE=11 | `error-reserved-btype`, generated | `wp607c_trace_facts_tests` | PASS |
| X-13 | invalid distance | `error-invalid-distance`, generated | `wp607c_trace_facts_tests` | PASS |
| X-14 | Adler-32 mismatch | `error-adler-mismatch`, generated | `wp607c_trace_facts_tests` | PASS |
| X-15 | §16.2 mapping cases (gray/16-bit/indexed/Adam7/filters/byte-select) | `ui-gray1-none`, `ui-rgba16-byte-select`, `ui-indexed4-trns`, `ui-adam7-empty-passes`, `ui-rgb8-five-filters`, generated | `wp607c_png_facts_tests`, reconstruction unit tests | PASS |
| X-16 | §16.4 regression matrix (open/close/reload, themes, DPI, debug+release) | n/a | full ctest + `run_gui_gate.py` + sanitizer/performance gates | PENDING (Task 5) |

## 4. Manual cells

| ID | Item | Status |
|---|---|---|
| M-1 | Native screen-reader certification (VoiceOver/NVDA/ORCA reading table rows, buttons, context) | MANUAL — cannot be inferred from QAccessible metadata tests; requires native OS session |
| M-2 | Native window-system rendering beyond offscreen (glyph rasterization, scrollbar chrome) | MANUAL/PENDING — recorded per platform at final gate (Task 5) |

## 5. Generated evidence locations (never committed)

- Captures: `build/gui-gate/wp-5u12/captures/*.png`
- Machine record: `build/gui-gate/wp-5u12/evidence.json` (schema
  `pnga-wp5u12-gui-evidence-v1`, written by `scripts/run_wp_5u12_gui_gate.py`)
- Performance records: `build/performance/**`

Tracked artifacts for WP-5U12F: this matrix, the product gate test, the runner
and — after controller review — exactly the 22 baselines of §2.
