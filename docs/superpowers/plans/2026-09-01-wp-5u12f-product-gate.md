# WP-5U12F Compression Inspector Product Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close WP-5U12 with auditable corpus, model/response, visual, theme, keyboard, copy, accessibility, no-replay, sanitizer, performance, and full-regression evidence while making no new Compression feature design.

**Architecture:** A dedicated Qt product-gate test drives the real three-page widgets with WP-607C controlled projections and captures deterministic baselines. A wrapper records hashes/environment/results under `build/**`. Existing performance and sanitizer gates enforce bounded model behavior; a text evidence record maps every normative acceptance item to an automated or explicitly manual result.

**Tech Stack:** C++20, Qt 6.8+ Test/Model/View/accessibility, CMake/CTest, Python 3 gate wrappers, ASan/UBSan, existing performance corpus/threshold schema.

**Spec:** `docs/development/wp-5u12-compression-inspector-completion.md` WP-5U12F/Completion definition and `docs/development/wp-5u12-compression-inspector-flow-ui.md` sections 13–14, 16–20.

## Global Constraints

- Start only from reviewed WP-5U12E. This task is a product gate, not a redesign.
- Before any F code, verify WP-607C supplies Stored, Fixed, Dynamic, overlap, multi-Block, cross-IDAT, truncated, reserved, invalid-distance, Adler mismatch, narrow, and large cases. A case may be a manifest-backed tracked fixture or a deterministic generator-backed fixture with fixed arguments, exact expected facts, and an owning test. If any required case is absent, report `BLOCKED` and finish WP-607C first.
- Exercise real model/view pages and real typed projection values; no hard-coded demo rows in production.
- Verify 320/360/480/600 px, Light/Dark, all normative states, Current+Selection, keyboard, copy, accessible names/roles, and action order.
- Tables must remain QAbstractItemModel/QTableView with zero row widgets. Large data remains bounded or model-virtualized and does not block the UI threshold.
- Page switch, row selection, resize, DEC/HEX, history, theme switch, and copy submit zero replays.
- Do not loosen existing assertions, screenshot tolerance, trace budgets, output cap, worker count, sanitizer flags, or performance thresholds to obtain PASS.
- Generated execution records go under ignored `build/gui-gate/wp-5u12/**` and `build/performance/**`. Only reviewed baselines and the text evidence index are tracked.
- Do not modify production code unless a separately reproduced defect first fails a focused test; if needed, stop F and create a distinct fix commit listing the defect and affected prior package.
- No parser/reconstruction, packaging, Compare, Statistics, APNG, third-party, or release artifact changes.
- Final status is PASS only if every automated command succeeds and manual-only cells are explicitly recorded; missing required evidence is FAIL/BLOCKED, never a known limitation.

---

## File Structure

| Path | Responsibility |
|---|---|
| `tests/gui/compression_inspector_product_gate_test.cpp` | Full state/width/theme/input/accessibility/no-replay matrix and capture |
| `tests/gui/CMakeLists.txt` | Add `pnga_gui_compression_inspector_product_gate_tests` |
| `tests/gui/baselines/wp-5u12/*.png` | Reviewed normative baseline images explicitly listed below |
| `scripts/run_wp_5u12_gui_gate.py` | Build/run/capture/hash machine-readable evidence |
| `tests/gui/trace_inspector_performance_test.cpp` | Model virtualization, object-count, response, scrolling gate |
| `tests/performance/performance_runner.cpp` | Add bounded Compression query/model timing scenario |
| `tests/performance/thresholds-v1.json` | Reviewed fixed maximums for the new scenario |
| `tests/performance/README.md` | Document deterministic inputs/metrics |
| `docs/evidence/wp-5u12-product-gate.md` | Requirement-to-evidence matrix and final record |
| `docs/development/wp-5u12-compression-inspector-completion.md` | Mark PASS only after every gate |

The exact tracked baseline names are:

```text
blocks-360-light.png
blocks-480-light.png
blocks-600-light.png
huffman-360-light.png
huffman-480-light.png
huffman-600-light.png
decode-trace-360-light.png
decode-trace-480-light.png
decode-trace-600-light.png
blocks-360-dark.png
huffman-360-dark.png
decode-trace-360-dark.png
blocks-480-dark.png
huffman-480-dark.png
decode-trace-480-dark.png
huffman-stored-360-light.png
loading-360-light.png
partial-error-360-light.png
partial-error-480-light.png
blocks-current-selection-480-light.png
decode-trace-current-selection-480-light.png
cross-idat-details-480-light.png
```

Do not add a baseline outside this list without product review.

## Required Gate Contract

`scripts/run_wp_5u12_gui_gate.py` accepts:

```text
--preset dev
--jobs 4
--output build/gui-gate/wp-5u12/evidence.json
--capture-dir build/gui-gate/wp-5u12/captures
--compare-baselines
```

Its JSON schema is `pnga-wp5u12-gui-evidence-v1` and contains commit, UTC time, OS, architecture, Qt version, platform plugin, DPR, logical DPI, theme, fixture manifest SHA-256, every case id/width/baseline/capture SHA-256/result, action/accessibility/no-replay test results, and command exit statuses. Missing Qt/corpus/baseline is not PASS.

Screenshot comparison permits antialias/scrollbar noise and a 2-pixel border envelope only. It must fail on clipped text, overlap, changed component order, missing/reordered required columns, indistinguishable Current/Selection, footer duplication/order, increased minimum width, or a table cleared by Partial/Error.

### Task 1: Freeze controlled-corpus prerequisites and the acceptance matrix

**Files:**
- Create: `docs/evidence/wp-5u12-product-gate.md`

- [ ] **Step 1: Verify WP-607C tracked and generated coverage**

```bash
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
grep -R -n -E 'stored|fixed|dynamic|overlap|cross[-_ ]?idat|truncated|reserved|invalid[-_ ]?distance|adler[-_ ]?mismatch|narrow|large' tests/corpus tests/common tests/unit tests/gui tests/fuzz
```

Expected: both verifiers pass and every category has one of the following auditable records:

- a tracked fixture with `tests/corpus/manifest.yaml` path, source/generator, license where applicable, SHA-256, expected facts, and owning test; or
- an in-memory/generated fixture with a stable case id, generator source file/function, fixed arguments, exact expected-fact assertions, and owning CTest target.

An empty manifest does not fail by itself when all required cases are generator-backed. A tracked fixture missing its manifest record, or any category missing both forms of evidence, is `BLOCKED`. Do not synthesize F-only demo data.

- [ ] **Step 2: Write the requirement matrix before code**

Create rows for every completion item in sections 19 and 20 plus all section 16 fixtures. Each row names requirement id, fixture id, fixture form (`tracked` or `generated`), manifest entry or generator source/function and fixed arguments, automated test/case, expected assertion, evidence path, and status. Native screen-reader certification is labelled manual and cannot be inferred from QAccessible metadata tests.

- [ ] **Step 3: Commit the initial matrix**

```bash
git add docs/evidence/wp-5u12-product-gate.md
git commit -m "docs: define compression inspector evidence matrix"
```

### Task 2: Implement the normative GUI/state/accessibility gate

**Files:**
- Create: `tests/gui/compression_inspector_product_gate_test.cpp`
- Modify: `tests/gui/CMakeLists.txt`

- [ ] **Step 1: Add the exact test target and failing matrix cases**

Add `pnga_gui_compression_inspector_product_gate_tests`, linked to ui_qt, analysis_engine, trace_model, Qt6::Test and test corpus helpers. Data rows cover the 22 baseline names and 320 px non-capture degradation. Assert component order, geometry ranges, exact headers/copy, column visibility/scroll, state copy, Current+Selection, typed actions/enabled states, selection persistence, zero index widgets, minimum width, Light/Dark palette roles, focus, keyboard, clipboard, accessible names/roles/descriptions, and no-replay counter.

- [ ] **Step 2: Build and confirm failure before capture**

```bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_compression_inspector_product_gate_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R gui_compression_inspector_product_gate --output-on-failure
```

Expected: missing baselines/capture contract or unmet assertion fails; do not generate baselines until semantic assertions pass.

- [ ] **Step 3: Make only test/harness corrections**

Bind manifest-backed projection facts to the real widgets. For clipboard use Qt clipboard under offscreen platform. For accessibility query QAccessibleInterface and exact names/roles/state. Expose trace submission counter only through existing `PNGA_TRACE_CONTROLLER_TESTING` instrumentation.

- [ ] **Step 4: Run semantic cases and commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'compression_inspector_product_gate|block_inspector|huffman_inspector|decode_trace_inspector|trace_pipeline' --output-on-failure
git add tests/gui/compression_inspector_product_gate_test.cpp tests/gui/CMakeLists.txt
git commit -m "test: add compression inspector product gate"
```

### Task 3: Capture and lock the approved visual baselines

**Files:**
- Create: `scripts/run_wp_5u12_gui_gate.py`
- Create: exactly the 22 PNG files under `tests/gui/baselines/wp-5u12/`
- Modify: `docs/evidence/wp-5u12-product-gate.md`

- [ ] **Step 1: Implement the runner and machine record**

Runner builds the focused target, validates corpus SHA, captures all cases under build, compares named baselines, hashes artifacts, and writes the required JSON. It refuses `NOT_CONFIGURED`, missing cases, unexpected extra baselines, or a dirty capture directory.

- [ ] **Step 2: Capture candidates and review every image**

```bash
python3 scripts/run_wp_5u12_gui_gate.py --preset dev --jobs 4 --output build/gui-gate/wp-5u12/evidence.json --capture-dir build/gui-gate/wp-5u12/captures
```

Review all images at native size for section 20.8 failure conditions. Copy candidates to tracked baseline paths only after review approval; the script itself must not overwrite approved baselines in compare mode.

- [ ] **Step 3: Run strict baseline comparison**

```bash
python3 scripts/run_wp_5u12_gui_gate.py --preset dev --jobs 4 --output build/gui-gate/wp-5u12/evidence.json --capture-dir build/gui-gate/wp-5u12/captures --compare-baselines
```

Expected: 22/22 PASS, exact matrix coverage, no unexpected baseline, and hashes recorded.

- [ ] **Step 4: Commit baselines/runner/evidence index**

```bash
git add scripts/run_wp_5u12_gui_gate.py tests/gui/baselines/wp-5u12 docs/evidence/wp-5u12-product-gate.md
git commit -m "test: lock compression inspector visual baselines"
```

### Task 4: Enforce model response, object count, and bounded performance

**Files:**
- Modify: `tests/gui/trace_inspector_performance_test.cpp`
- Modify: `tests/performance/performance_runner.cpp`
- Modify: `tests/performance/thresholds-v1.json`
- Modify: `tests/performance/README.md`

- [ ] **Step 1: Replace old capped-QTableWidget assertions**

Use QTableView/model assertions for 10,000 Blocks, maximum bounded Huffman table size, and 4,096 Decode Trace tokens. Assert rowCount equals source facts, zero row widgets, visible-row formatting only, set-model cold/hot thresholds, deterministic 200-scroll response, and no retained duplicate token/output buffers.

- [ ] **Step 2: Add `compression-inspector` performance scenario**

Measure Fast Index projection, bounded 4,096-token query, three model publications, first visible rows, 200 deterministic row reads/scroll targets, and checksum. Add fixed reviewed maxima to `thresholds-v1.json`; include the scenario in README. Do not measure screenshot capture in performance thresholds.

- [ ] **Step 3: Run enforced performance gate**

```bash
cmake --build --preset dev --target pnga_gui_trace_inspector_performance_tests pnga_performance_runner --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R gui_trace_inspector_performance --output-on-failure
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds --output build/performance/wp-5u12-latest.json
```

Expected: every fixed threshold passes and record contains `compression-inspector`.

- [ ] **Step 4: Commit performance gate**

```bash
git add tests/gui/trace_inspector_performance_test.cpp tests/performance/performance_runner.cpp tests/performance/thresholds-v1.json tests/performance/README.md
git commit -m "test: gate compression inspector performance"
```

### Task 5: Run sanitizer/full regression and close WP-5U12

**Files:**
- Modify: `docs/evidence/wp-5u12-product-gate.md`
- Modify: `docs/development/wp-5u12-compression-inspector-completion.md`
- Verify: all F files above

- [ ] **Step 1: Run all final automated gates**

```bash
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'block_inspector|huffman_inspector|decode_trace|compression_inspector|trace_pipeline|selection_navigation' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py --preset dev --jobs 4 --output build/gui-gate/wp-5u12/cross-platform-evidence.json
python3 scripts/run_wp_5u12_gui_gate.py --preset dev --jobs 4 --output build/gui-gate/wp-5u12/evidence.json --capture-dir build/gui-gate/wp-5u12/captures --compare-baselines
cmake --preset asan
cmake --build --preset asan --parallel 4
ctest --preset asan --output-on-failure
python3 scripts/run_sanitizer_fuzz_gate.py --preset asan --jobs 4
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds --output build/performance/wp-5u12-latest.json
git diff --check
```

Expected: every command exits 0. Packaging smoke is intentionally not part of WP-5U12 and must not be invoked or changed.

- [ ] **Step 2: Execute the manual regression record**

On the current native platform record open/close/reload/rapid-switch; Chunk/Reconstruction/Pixels/Filtered/Defiltered; image/X/Y/Lock/DEC-HEX; all Hex sources; Inspector workspace restore; keyboard-only workflow; clipboard; native theme; high-DPI; and screen-reader observations if available. Mark unexecuted native OS cells `BLOCKED`, not PASS.

- [ ] **Step 3: Audit side effects and evidence hashes**

```bash
git status --short
git diff --name-status HEAD~4..HEAD
git diff HEAD~4..HEAD -- libs apps ui/qt
git ls-files tests/gui/baselines/wp-5u12 | sort
```

Expected: production diff is empty; tracked baselines exactly match the 22-name list; generated JSON/captures remain ignored; no package, parser, Statistics, Compare, APNG, third-party, or unrelated file changed.

- [ ] **Step 4: Mark completion only after PASS and commit**

Fill the evidence matrix with command results, test counts, commit hashes, JSON/baseline SHA-256 values, manual cells, limitations by platform, and final PASS/BLOCKED/FAIL. Change the package status to PASS only if all WP-5U12 requirements pass.

```bash
git add docs/evidence/wp-5u12-product-gate.md docs/development/wp-5u12-compression-inspector-completion.md
git commit -m "docs: close compression inspector product gate"
git show --check --stat --oneline HEAD
git status --short
```

- [ ] **Step 5: Produce the final F handoff**

Report final status, exact CTest/sanitizer/performance counts, threshold values/results, 22 screenshot results, accessibility/manual matrix, no-replay counters, six A–F terminal commits, evidence hashes, known native-platform coverage boundaries, and empty status. Do not claim WP-5U12 complete if any required cell is BLOCKED/FAIL.
