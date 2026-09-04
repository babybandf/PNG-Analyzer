# WP-5U12F — Compression Inspector product gate evidence matrix

Status: matrix frozen before gate code (plan Task 1); Task 3 baselines locked
(§2), Task 5 automated gates recorded (§5, §6). Final status awaits the
native-OS manual regression record (§4, product owner decision pending) —
see §8. This file is the living requirement-to-evidence record for WP-5U12F.
Normative sources:
`docs/development/wp-5u12-compression-inspector-completion.md` (WP-5U12F and
completion definition) and `docs/development/wp-5u12-compression-inspector-flow-ui.md`
§13–14, §16–20. Row statuses: **PASS** (evidence exists today), **PENDING**
(automated check owned by a later WP-5U12F task), **PENDING-HUMAN**
(manual-only cell awaiting the product owner's native-OS record; never
inferred from QAccessible metadata). Missing evidence
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
| large data | `perf-large-rgba8` (3,146,496 inflated bytes, 49 Stored blocks) | `wp607c_trace_facts_tests`, performance gate (Task 4) |

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
| `loading-360-light` | real `DecodeTraceInspector` page, typed `kReplaying` view (real pages cannot freeze mid-replay deterministically; precedent `gui_compression_inspector_responsive_tests`; capture-unit rationale in wave-1 report `.superpowers/sdd/2026-09-03-wp-607c-controlled-static-ui-trace-corpus/task-F-wave1-report.md`) | typed view |

### 2.2 Baseline approval record (product owner)

- **Reviewer:** product owner. **Date:** 2026-09-03.
- **Scope:** all 22 candidate captures reviewed at native size against the
  flow-ui §20.8 failure conditions (clipped text, overlap, changed component
  order, missing/reordered required columns, indistinguishable
  Current/Selection, footer duplication/order, increased minimum width,
  error-cleared tables). **Result: APPROVED.**
- **Provenance:** the two capture runs that produced the candidates were
  byte-identical (22/22 same SHA-256). The approved bytes were copied to
  `tests/gui/baselines/wp-5u12/` and committed in `630b8cb`
  (`test: lock compression inspector visual baselines`). The runner itself
  never writes into the tracked baseline directory.

### 2.3 Baseline SHA-256 (tracked, committed in `630b8cb`)

```text
3b84af3b45aaf956c4a9c71de22bd6abf17d8dd8dc55f6fd0030a977b75293a5  blocks-360-dark.png
76f9e6bf47f00659316a1cb989f4832d1c929c77a3ed52d9b772d93bcdb96abe  blocks-360-light.png
59fdaeb5034fe0a13b65553d0a27d28dedb0ff1ee51a18a9563a4d5883e3e278  blocks-480-dark.png
f004a5212c47e153e0195f2c1eda8ece357507dbe7df6daf707db0c6174f72c3  blocks-480-light.png
5c545a7ff05cf0bddcc79addaea27d5a67cd7738202feabe7e71c79619b2b382  blocks-600-light.png
576ae96e350dcb0a75c94a3a83741663a9c7f97e58a42bbc4fee6b577856366c  blocks-current-selection-480-light.png
547677f06e4f11977243eb59cd0ee424906723d5435bc439f556b3149f22bc8d  cross-idat-details-480-light.png
0d0443ac6fc1db6d4113dcbf29af14859c7916984cfa81c5b23c66cc7275d399  decode-trace-360-dark.png
183803063883a96c5992e96d4040ed40825d9825a8f50e74b938a0530556dfbc  decode-trace-360-light.png
3c7f16abdce60033e301eb1d7a38350c85afff7c4a84ceaa180c71c1b69262da  decode-trace-480-dark.png
95c78b722af07731685767b7faf4f6a5f4f81451ee32938985da1ab585addb1b  decode-trace-480-light.png
80551bacd274a1483ffccedcc764122bf51fdd8020491652827bbe719cd5cd99  decode-trace-600-light.png
b8848c00a4e0afa4e036787178569328eb762a2a9dc5a10b0f5e771f54630b5d  decode-trace-current-selection-480-light.png
f8defb55b04e22f4ee9c6fcdaf5046cb6c3ee935cd9fa30fc0d5fb74c98b2f29  huffman-360-dark.png
1a6d231c5c6f79660e266e21024922fc3d225e7825ab868766c267cae4572316  huffman-360-light.png
7d174554683b9114f164b58eabb33eaf6a772fed2d0821e39c6d67b168c24d97  huffman-480-dark.png
35683f98ab1f273df195b0046cfd3b1e3e9a4f07255fc6737561b6dd4d6cbb7f  huffman-480-light.png
3739eb873301a39b2b6fe6a83bf11fa960dc0aa6baaf81dee7f99f9c82bc7477  huffman-600-light.png
43b0f091e82cd53a202dcd2883848bddf477d463f2d4863d082aa1d9d2f52364  huffman-stored-360-light.png
0ff41a612b57f6b7f45ab377d7a662535cbb5be6c01ef998cd74ac0ef3d5489e  loading-360-light.png
30dc0a6fdb3ac77a2b8c246d85a7652b307ce259c9e10453a75de33e97ded0ee  partial-error-360-light.png
ca987cb17bfe6f69ddd6890da41e6225b47fc48a3af56b8d30d898cf14d1168b  partial-error-480-light.png
```

### 2.4 Strict comparison record (plan Task 3 Step 3 + Task 5 Step 1 rerun)

`python3 scripts/run_wp_5u12_gui_gate.py --preset dev --jobs 4 --output
build/gui-gate/wp-5u12/evidence.json --capture-dir
build/gui-gate/wp-5u12/captures --compare-baselines` → exit 0, status PASS:

- 22/22 cases `result = "pass"`; exact matrix coverage; 0 missing and 0
  unexpected tracked baselines (runner refuses both).
- Per-case provenance verified three ways: tracked baseline ==
  fresh compare-mode capture == approved candidate capture, and equal to the
  `capture_sha256` recorded in the evidence JSON (§2.3 hashes).
- Semantic gates all pass: component_order, geometry_bands, degradation_320,
  keyboard, clipboard, accessibility, no_replay.
- Adjudication applied (§1, WP-607C package §7.5): `partial-error-*` baselines
  assert the stable non-ready context copy with zero verified blocks — the
  malformed fixtures never reach Partial/Error and production refuses to
  index; no Partial/Error table state and no replay is asserted for them.
- Harness correction recorded: the first compare-mode run exposed a runner
  defect — a `compare: "pass"` verdict was never mapped to a per-case result,
  so compare mode could only report FAIL. Fixed in `630b8cb` by mapping the
  verdict; the fail/missing-baseline paths and all comparisons are unchanged
  (no assertion loosened).

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
| F-1 | 320/360/480/600 px inspector widths, no content-driven growth | dyn | gate `narrow320Degradation…` + every capture row; `gui_compression_inspector_responsive_tests`, `gui_wp607c_corpus_tests` | `page.minimumWidth() <= width`, width honored, viewport-internal scroll | ctest logs; captures §2 | PASS (§2.4) |
| F-2 | Light/Dark themes | dyn | gate dark/light rows | Current background resolves through `ApplicationTheme::ColorToken::kCurrentPixel` per mode; no hard-coded RGB | captures §2 | PASS (§2.4) |
| F-3 | Keyboard navigation | dyn | gate 320 row: focus + arrow keys on all three tables | selection moves via key events, focus ring distinct from Current highlight | ctest log | PASS (§5.1 gates) |
| F-4 | Copy | dyn | gate 320 row + per-row copy contract | detail value labels selectable (`Qt::TextSelectableByMouse`), clipboard round-trip works | ctest log | PASS (§5.1 gates; native clipboard = §4 M-8) |
| F-5 | Accessibility names | all | gate per-row QAccessible metadata; `gui_compression_inspector_responsive_tests` | tables/buttons/context expose exact accessible names and Table/Button roles | ctest log | PASS (§5.1 gates; native screen-reader = §4 M-1) |
| F-6 | Virtualized scrolling, models not row widgets | dyn, `perf-large-rgba8` | gate rows (zero `QTableWidget`, model-backed `QTableView`); Task 4 `gui_trace_inspector_performance_tests` | `qobject_cast<QTableWidget*> == nullptr`, `QAbstractTableModel` under every table; 10k/4096-row budgets | ctest log | PASS (§5.1 gates + §5.4) |
| F-7 | loading/empty/partial/error states | trunc, reserved, typed kReplaying view | gate `loading-360-light`, `partial-error-*` rows; `gui_compression_inspector_responsive_tests` | stable copy, verified facts retained, no invented rows, no `no trace` | captures §2 | PASS (§2.4, §1 adjudication) |
| F-8 | Large trace response and memory | `perf-large-rgba8` | Task 4 performance gate + thresholds | fixed maxima in `tests/performance/thresholds-v1.json` | `build/performance/**` | PASS (§5.4) |
| F-9 | Page switch / row selection / resize / DEC-HEX / history / theme / copy submit zero replays | dyn | gate 320 row with `PNGA_TRACE_CONTROLLER_TESTING` counters | `acceptedRequestCountForTest` unchanged across the full action matrix | ctest log | PASS (§5.1 gates) |
| F-10 | Table rows use models, never one QWidget per event | dyn, multi | gate per-row; Task 4 | viewport holds no per-row QWidget | ctest log | PASS (§5.1 gates + §5.4) |

### 3.2 Flow-ui §19 completion definition

| ID | Requirement (summary) | Fixture | Automated check | Expected assertion | Evidence | Status |
|---|---|---|---|---|---|---|
| R-1 | Three pages answer structure / code rules / decode process without duplication | dyn | `gui_wp607c_corpus_tests`, gate rows | Blocks table, Huffman tables, Trace rows all published from one generation | ctest logs | PASS (§2.4) |
| R-2 | No unexplained `Block trace: no trace` after opening a valid PNG | dyn, stored, multi | `gui_wp607c_corpus_tests`, gate rows | status reaches `ready`; heading carries bounded scope facts | ctest logs | PASS |
| R-3 | Stored/Fixed/Dynamic/error blocks all show correct content or explicit state | stored, multi, trunc, reserved | `gui_wp607c_corpus_tests`, gate stored/error rows | Stored heading + no-Huffman copy; malformed keeps stable non-ready copy, 0 rows | ctest logs | PASS (§2.4) |
| R-4 | Canonical code vs read-order bits clearly separated | dyn | `gui_huffman_inspector_tests`, gate Huffman rows | `Canonical` and `Read order` are distinct columns and details | captures | PASS (§2.4) |
| R-5 | Literal/Match/EOB input+output ranges accurate | dyn, `idat-split-token` | `wp607c_trace_facts_tests`, `gui_decode_trace_inspector_tests` | exact half-open ranges from manifest facts | ctest logs | PASS |
| R-6 | Match length/distance/source/target/overlap/provenance accurate | dyn | `wp607c_trace_facts_tests`, `gui_decode_trace_inspector_tests` | typed step payload equality | ctest logs | PASS |
| R-7 | All IDAT analyzed as one stream, cross-chunk mapping accurate | `idat-split-token` | `wp607c_trace_facts_tests`, `gui_trace_pipeline_integration_tests`, gate cross-idat row | every physical span highlighted; details list all spans | ctest logs | PASS (§2.4) |
| R-8 | Pixel → output range → event → block reverse mapping; explicit degradation | dyn | `gui_trace_pipeline_integration_tests` | Current rows marked; `mapping unavailable` when imprecise | ctest logs | PASS |
| R-9 | Bidirectional navigation accurate, no update loops | dyn | `gui_trace_pipeline_integration_tests`, gate navigation row | one request → one view update; stale generation rejected | ctest logs | PASS |
| R-10 | Current context and manual selection coexist, distinguishable | multi, dyn | gate current-selection rows; `gui_compression_inspector_responsive_tests` | `ContainsCurrentRole` + `IsManualSelectionRole` + native selection simultaneously | captures | PASS (§2.4) |
| R-11 | Page switch / coordinate change: no reparse, no re-Inflate, no full-trace scan | dyn | `gui_trace_pipeline_integration_tests`, gate no-replay row | counters unchanged, bundle generation stable | ctest logs | PASS (§5.1 gates) |
| R-12 | Virtualized/lazy models; bounded tables proven safe by object-count and response gates | dyn, `perf-large-rgba8` | Task 4 `gui_trace_inspector_performance_tests` | model/view assertions + thresholds | ctest + perf JSON | PASS (§5.4) |
| R-13 | Narrow inspector, light/dark, keyboard, copy usable | dyn | gate rows F-1..F-4 | §3.1 matrix | captures + logs | PASS (§2.4, §5.1) |
| R-14 | partial/error keeps prior facts; no fabrication, no clearing, no raw debug strings | trunc, reserved | gate partial-error rows; `gui_compression_inspector_responsive_tests` | stable copy, table intact for indexed streams, human-readable stop reasons | captures | PASS (§2.4, §1 adjudication) |
| R-15 | Core/model/GUI/regression tests all pass | all | full `ctest --preset dev` | 100% pass | ctest logs | PASS (§5.1: 53/53) |

### 3.3 Flow-ui §20 normative UI contract

| ID | Requirement (summary) | Fixture | Automated check | Expected assertion | Evidence | Status |
|---|---|---|---|---|---|---|
| U-20.2 | Fixed component tree and order (tab bar → context → page stack → footer actions) | dyn | gate per-row | y-order: context above pages; table above details; footer actions bottom-most; exactly one footer action row | ctest logs + captures | PASS (§5.1 gates) |
| U-20.3 | Geometry bands (header 26–31, row 26–32, buttons 26–32, split ≈55:45 with 4 rows + 120 px details minima) | dyn | gate + `gui_compression_inspector_responsive_tests` | band assertions per §20.3 | ctest logs | PASS (§5.1 gates) |
| U-20.4 | 600/420–599/360–419/320–359 responsive behavior; no width-driven Inspector growth | dyn | gate width matrix + 320 row | Blocks Events/Scanlines hidden ≤480/≤360; Huffman/Trace keep all columns; 320 stacks footer vertically in order | ctest logs | PASS (§2.4) |
| U-20.5 | Default column order and resize priorities | dyn | gate per-row | exact header sequences `Current|#|Type|Final|Input bits|Output bytes[|Events|Scanlines]`, `Symbol|Meaning|Bits|Canonical|Read order|Uses in result`, `Current|Step|Input bits|Event|Output`; Event stretch on Trace | ctest logs | PASS (§2.4) |
| U-20.6 | Current / Selection / Current+Selection visual contract; Error/Partial keep tables; Loading/Empty copy | multi, dyn, trunc, reserved | gate current-selection + state rows; `gui_compression_inspector_responsive_tests` | `●` Current column + accessible text + token background ≠ native selection; both survive selection | captures | PASS (§2.4) |
| U-20.7 | Locked copy (`Show in Hex`, `Show inflated output`, headers, tab labels; no `Show in DEFLATE`, no `no trace`) | dyn | gate per-row | exact label equality set | ctest logs | PASS (§2.4) |
| U-20.8 | Visual baselines and deformation gate (clipping, overlap, order, columns, Current/Selection distinction, footer duplication/order, min width, error clears table) | all §2 fixtures | gate captures + runner compare (2 px border + antialias envelope only) | 22/22 baseline comparisons PASS at final gate | `build/gui-gate/wp-5u12/**`, tracked baselines (`630b8cb`) | PASS (§2.2, §2.4) |

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
| X-16 | §16.4 regression matrix (open/close/reload, themes, DPI, debug+release) | n/a | full ctest + `run_gui_gate.py` + sanitizer/performance gates | PASS (§5.1/§5.2/§5.3/§5.4; native-OS manual portion = §4) |

## 4. Manual regression record (all cells PENDING-HUMAN)

Native-OS session owned by the **product owner**; decision pending. Every cell
below is **PENDING-HUMAN** — not PASS, not BLOCKED yet. The human executes the
procedure, records the observation and date in the cell, and marks PASS or
BLOCKED (with reason) per flow-ui §13–14 expectations. Offscreen automation
(§5.1) already covers the semantic layer; these cells verify native windowing,
input, accessibility and rendering behavior that cannot be inferred offscreen.

| ID | Item | Procedure (what the human must do) | Expected observation | Status |
|---|---|---|---|---|
| M-1 | Native screen-reader observations (if available; VoiceOver on this macOS host) | Open a valid fixture (`valid/trace-dynamic-overlap-repeats.png`), enable VoiceOver, traverse the three inspector tables, footer actions and context panel with VO cursor and Tab | Tables announced as tables with exact accessible names; rows expose Current/Selection state; buttons read their locked labels | PENDING-HUMAN |
| M-2 | Native window rendering beyond offscreen (glyph rasterization, scrollbar chrome) | Open the same fixture in a native window; compare against `blocks-480-light` baseline subjectively; toggle window sizes 360/480/600 | No clipped/overlapping text; scrollbars appear only when needed; component order unchanged vs baseline | PENDING-HUMAN |
| M-3 | Open / close / reload / rapid page-switch lifecycle | Open 3 fixtures in sequence, close each, reload the last one; rapidly switch Blocks→Huffman→Trace ≥20 times | No stale rows, no duplicate footers, no replay storm (trace submission counter stable — verify via log if instrumented), window closes cleanly | PENDING-HUMAN |
| M-4 | Chunk / Reconstruction / Pixels / Filtered / Defiltered panels alongside inspector | Open `valid/idat-split-token.png`; walk Chunk → Reconstruction → Pixels → Filtered → Defiltered tabs with the Compression inspector open | Inspector context stays consistent; `Show in Hex` targets still resolve while each panel is active; no cross-panel state clobbering | PENDING-HUMAN |
| M-5 | Image / X / Y / Lock / DEC-HEX coordinate controls | With the trace open, move the probe over the image, set X/Y numerically, toggle Lock, flip DEC/HEX | X/Y display matches probe position; Lock freezes coordinate readouts; DEC-HEX flip reformats without losing selection or Current row | PENDING-HUMAN |
| M-6 | All Hex sources (`File`, `IDAT Stream`, `Inflated`, `Defiltered`) | For a trace row use `Show in Hex` (File and IDAT Stream variants per flow-ui §13 table) and `Show inflated output` (Inflated source); select a filtered/defiltered byte and inspect | Each source activates with the exact promised range selected; source labels match flow-ui §13/§20.7 locked copy | PENDING-HUMAN |
| M-7 | Inspector workspace restore | Arrange pages, selection, theme; quit the app; relaunch | Inspector workspace (active page, selection, theme) restored per flow-ui §14; no crash, no fabricated state for a fresh document | PENDING-HUMAN |
| M-8 | Keyboard-only workflow end-to-end | Without mouse: Tab through tab bar → tables → footer actions; arrow-key navigation in all three tables; activate `Show in Hex` / `Show inflated output` / copy from keyboard | Every action reachable and activable; focus ring visible and distinct from Current highlight; order matches §20.2 | PENDING-HUMAN |
| M-9 | Clipboard on native window system | Select a detail value and a table cell; copy via keyboard; paste into TextEdit | Copied text matches the on-screen value (offscreen round-trip already PASS in §5.1; this verifies native clipboard ownership/UTF-8) | PENDING-HUMAN |
| M-10 | Native theme (system Light/Dark switching) | Toggle macOS appearance Light↔Dark while the inspector is open on each page | All three pages re-resolve palette tokens (including Current pixel token); no hard-coded RGB artifacts; legible in both modes | PENDING-HUMAN |
| M-11 | High-DPI native display | Run on the native Retina display and, if available, an external lower-DPI display; resize across 360–600 px | Text and table chrome render sharply at DPR 2; no blurry artifacts; geometry bands hold; no width-driven growth | PENDING-HUMAN |

Instruction to the product owner: execute each procedure on the current
native platform (macOS), append the observation and date to the cell, and set
PASS or BLOCKED (with reason). After all cells are resolved, update
`FINAL STATUS` in §8 and close the package per plan Task 5 Step 4.

## 5. Task 5 Step 1 — final automated gate record

Executed 2026-09-04 on branch `wp-5u12-compression-inspector` at `630b8cb`
(macOS 26.6.2 arm64, Qt 6.11.1, `QT_QPA_PLATFORM=offscreen` for ctest/GUI
runs). Packaging smoke intentionally excluded (plan Task 5).

### 5.1 Command matrix (all exit 0)

| Command | Exit | Result |
|---|---|---|
| `python3 scripts/verify_repository_layout.py` | 0 | 0 failures, 0 warnings |
| `python3 scripts/verify_dependencies.py` | 0 | 0 failures, 0 warnings |
| `cmake --preset dev` | 0 | configured |
| `cmake --build --preset dev --parallel 4` | 0 | up to date |
| `ctest --preset dev -R 'block_inspector\|huffman_inspector\|decode_trace\|compression_inspector\|trace_pipeline\|selection_navigation'` | 0 | **8/8 passed** |
| `ctest --preset dev` | 0 | **53/53 passed** |
| `python3 scripts/run_gui_gate.py --preset dev --jobs 4 --output build/gui-gate/wp-5u12/cross-platform-evidence.json` | 0 | PASS (3/3 suites) |
| `python3 scripts/run_wp_5u12_gui_gate.py --preset dev --jobs 4 --output build/gui-gate/wp-5u12/evidence.json --capture-dir build/gui-gate/wp-5u12/captures --compare-baselines` | 0 | **PASS 22/22** (§2.4) |
| `cmake --preset asan && cmake --build --preset asan --parallel 4 && ctest --preset asan` | 0 | **53/53 passed** |
| `python3 scripts/run_sanitizer_fuzz_gate.py --preset asan --jobs 4` | 0 | PASS (2 deterministic replays + fuzz smoke) |
| `python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds --output build/performance/wp-5u12-latest.json` | 0 | PASS, thresholds enforced |
| `git diff --check` | 0 | clean |

### 5.2 Machine record hashes (SHA-256, generated — never committed)

```text
06649ef6984b2fe2b3298446fa8e97d9930ae37f693dd0518d75a2e2ab84ef71  build/gui-gate/wp-5u12/evidence.json
cc5941021054546229febb6346ead8bb256669294b1505f38d1f867a7c5c1de7  build/gui-gate/wp-5u12/cross-platform-evidence.json
529d5b1bba428aa38183816cbffbb65b76cc465467bd5dacfdf56b42cbe1d8e6  build/performance/wp-5u12-latest.json
```

Tracked baseline hashes: §2.3.

### 5.3 F terminal commits

```text
bd2f40d docs: define compression inspector evidence matrix
bb14819 test: add compression inspector product gate
41609ab test: add wp5u12 gui gate runner
958c6d3 test: render wp5u12 gate captures via QWidget::render
37593da test: gate compression inspector performance
630b8cb test: lock compression inspector visual baselines
<this commit> docs: record compression inspector gate evidence
```

### 5.4 Enforced performance scenario `compression-inspector`

`pnga-performance-record-v1`, corpus `wp607c-static-v1`, revision
`5df99ad82f14…`, `perf-large-rgba8` (1024×768 RGBA8, 3,146,804 PNG bytes,
**49 Stored blocks** — confirming the §1.1 correction): `fast_index_us`
9,444; `trace_query_4096_us` 198,191; `huffman_model_us` 684;
`decode_trace_model_us` 4,142; `first_visible_rows_us` 23;
`visible_row_reads_us` 9; checksum 13,258,482. All reviewed maxima in
`tests/performance/thresholds-v1.json` enforced and passing; UI scenario
`gui_trace_inspector_performance_tests` passed.

## 6. Task 5 Step 3 — side-effect audit

Range `8d2b152..HEAD` (all F commits, from before
`docs: define compression inspector evidence matrix`):

- `git status --short` → empty (clean tree).
- `git diff --name-status 8d2b152..HEAD` → only:
  `docs/evidence/wp-5u12-product-gate.md` (A),
  `scripts/run_wp_5u12_gui_gate.py` (A), `tests/gui/CMakeLists.txt` (M),
  22 × `tests/gui/baselines/wp-5u12/*.png` (A),
  `tests/gui/compression_inspector_product_gate_test.cpp` (A),
  `tests/gui/trace_inspector_performance_test.cpp` (M),
  `tests/performance/README.md` (M),
  `tests/performance/performance_runner.cpp` (M),
  `tests/performance/thresholds-v1.json` (M). No package, parser,
  Statistics, Compare, APNG, third-party or unrelated file touched.
- `git diff 8d2b152..HEAD -- libs apps ui/qt` → **EMPTY** (production
  untouched by every F commit).
- `git ls-files tests/gui/baselines/wp-5u12 | sort` → exactly the 22 pinned
  names of §2, nothing else.
- Generated JSON/captures under `build/**` remain ignored (`.gitignore`).

## 7. Generated evidence locations (never committed)

- Captures: `build/gui-gate/wp-5u12/captures/*.png`
- Machine record: `build/gui-gate/wp-5u12/evidence.json` (schema
  `pnga-wp5u12-gui-evidence-v1`, written by `scripts/run_wp_5u12_gui_gate.py`)
- Performance records: `build/performance/**`

Tracked artifacts for WP-5U12F: this matrix, the product gate test, the runner
and exactly the 22 baselines of §2 (locked in `630b8cb`).

## 8. FINAL STATUS: pending manual record

All automated gates pass (§5.1) and the side-effect audit is clean (§6).
Every §4 manual cell is **PENDING-HUMAN**: the product owner executes the
native-OS regression record (§4) and resolves each cell PASS/BLOCKED. Only
then is the package status set per plan Task 5 Step 4
(`docs/development/wp-5u12-compression-inspector-completion.md` and the final
close-out commit). This placeholder must not read PASS until that record is
complete.
