# WP-5U12 Compression Inspector Master Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete WP-5U12 through six isolated, strictly serial work packages whose individual commits preserve all unrelated PNG Analyzer behavior and pass their own regression gates.

**Architecture:** WP-5U12A freezes offset and Fast Index facts; WP-5U12B freezes typed selection/navigation; WP-5U12C, D, and E then productize Blocks, Huffman, and Decode Trace against those contracts; a WP-607C prerequisite gate must pass after E (or be revalidated there if already complete); WP-5U12F performs the product-wide model, performance, visual, accessibility, and regression gate. The six WP-5U12 packages are never implemented concurrently: each successor starts only from the verified commit produced by its predecessor.

**Tech Stack:** C++20, Qt 6.8+ Widgets/Test, CMake presets, Catch2, QAbstractItemModel, pnga::trace_model, pnga::analysis_engine, pnga::deflate_index, pnga::deflate_trace.

**Spec:** `docs/development/wp-5u12-compression-inspector-completion.md` and normative `docs/development/wp-5u12-compression-inspector-flow-ui.md` sections 19–20.

## Global Constraints

- Execute the six packages in exact serial order `WP-5U12A → WP-5U12B → WP-5U12C → WP-5U12D → WP-5U12E → WP-5U12F`; between E and F, complete or revalidate the external WP-607C corpus prerequisite.
- At execution time, create or select an isolated worktree using the `using-git-worktrees` skill before changing code.
- WP-5U15 is a required verified predecessor. Compare remains deferred and is outside this plan.
- One generation-level Fast Compression Index owns wrapper/IDAT/Adler summary and the complete Block list.
- Selection-level Deep Trace remains bounded, cancelable, budgeted, generation-safe, and never becomes a default whole-file token/event trace.
- File bytes, zlib-stream bytes, DEFLATE-payload bits, and Inflated bytes remain distinct typed domains.
- Logical input crossing IDAT boundaries retains every physical span; no task concatenates all IDAT payloads.
- GUI code formats immutable facts only; it never parses PNG/DEFLATE, inflates data, reverses Huffman bits, or derives facts by parsing debug strings.
- Do not modify `third_party/**`, packaging, PNG parser/filter/reconstruction behavior, Compare, Statistics, or APNG.
- Keep the normative component tree, English copy, 320/360/480/600 px behavior, Current/Selection semantics, and Light/Dark behavior from section 20.
- Every task begins from a clean worktree, adds only its allowed files, ends with a commit, passes its focused and full regression commands, and leaves no generated artifacts or unrelated edits in `git status --short`.
- A task reports `BLOCKED` rather than weakening types, budgets, assertions, accessibility, screenshots, performance thresholds, or existing tests.

---

## Plan Set

| Serial stage | Independent execution plan | Primary output |
|---|---|---|
| A | `docs/superpowers/plans/2026-09-01-wp-5u12a-offset-fast-index.md` | Stable typed offset and generation-level Fast Index contract |
| B | `docs/superpowers/plans/2026-09-01-wp-5u12b-selection-navigation.md` | Typed current/manual selection and navigation contract |
| C | `docs/superpowers/plans/2026-09-01-wp-5u12c-blocks-page.md` | Complete Blocks page over Fast Index |
| D | `docs/superpowers/plans/2026-09-01-wp-5u12d-huffman-page.md` | Product Huffman construction page |
| E | `docs/superpowers/plans/2026-09-01-wp-5u12e-decode-trace-page.md` | Product bounded Decode Trace page |
| F | `docs/superpowers/plans/2026-09-01-wp-5u12f-product-gate.md` | Final model/performance/visual/accessibility/regression evidence |

## Task 0: Version the Audited Plan Set Before Implementation

This documentation commit is an execution preflight, not a seventh implementation package. Run it before WP-5U12A so A's clean-worktree assertion is meaningful.

- [ ] **Step 1: Confirm the eight audited documents are the only expected untracked plan artifacts**

```bash
git status --short
```

Expected: the seven A–F/Master plan files and `docs/superpowers/plans/WP-5U12_checked_byglm5.3flash.md` are present; any other modification is recorded as pre-existing user-owned work or resolved before continuing.

- [ ] **Step 2: Commit the plan set and audit report**

```bash
git add docs/superpowers/plans/2026-09-01-wp-5u12-compression-inspector-master.md docs/superpowers/plans/2026-09-01-wp-5u12a-offset-fast-index.md docs/superpowers/plans/2026-09-01-wp-5u12b-selection-navigation.md docs/superpowers/plans/2026-09-01-wp-5u12c-blocks-page.md docs/superpowers/plans/2026-09-01-wp-5u12d-huffman-page.md docs/superpowers/plans/2026-09-01-wp-5u12e-decode-trace-page.md docs/superpowers/plans/2026-09-01-wp-5u12f-product-gate.md docs/superpowers/plans/WP-5U12_checked_byglm5.3flash.md
git commit -m "docs: add audited wp-5u12 execution plans"
git show --check --stat --oneline HEAD
git status --short
```

Expected: commit contains exactly those eight documents, has no whitespace errors, and status is empty except separately recorded user-owned files.

## Mandatory Task Exit Gate

Every stage performs this gate after its focused tests and before handoff:

```bash
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
git diff --check
git status --short
```

Expected:

- both repository scripts report `0 failure(s), 0 warning(s)`;
- configure/build exit 0;
- CTest reports 100% pass;
- `git diff --check` prints nothing;
- before commit, `git status --short` lists only the files explicitly allowed by the active child plan;
- after commit, `git status --short` is empty, except pre-existing user-owned files recorded before task start;
- build outputs remain under existing ignored `build/**` paths;
- no tracked binary fixture, screenshot, package, or generated source appears unless WP-5U12F explicitly names it.

For each commit created by a child plan, review changed paths immediately:

```bash
git show --check --stat --oneline HEAD
git show --format= --name-status HEAD
```

Expected: no whitespace errors, and every path is present in that child plan’s **Files** section.

### Task 1: Execute WP-5U12A — Offset and Fast Index

**Files:**
- Plan: `docs/superpowers/plans/2026-09-01-wp-5u12a-offset-fast-index.md`
- Verify: `tests/unit/trace-model/offset_range_test.cpp`
- Verify: `tests/unit/analysis-engine/trace_orchestrator_test.cpp`
- Verify: `tests/unit/analysis-engine/block_inspector_test.cpp`

**Interfaces:**
- Consumes: current `FileByteRange`, `ZlibByteRange`, `ZlibBitRange`, `DeflateBitRange`, `InflatedByteRange` and `VirtualIDATStream` mapping.
- Produces: the exact `FastCompressionIndexView`, `ZlibByteOffset deflate_data_begin`, wrapper/Adler status, typed range, and bit-precise physical-span interfaces consumed by stages B–F.

- [ ] **Step 1: Confirm the predecessor and worktree are clean**

```bash
git log -1 --oneline
git status --short
```

Expected: HEAD contains the verified WP-5U15 integration and status contains no unexpected files.

- [ ] **Step 2: Execute every checkbox in the A plan**

Use `superpowers:executing-plans` or `superpowers:subagent-driven-development` with the A plan. Do not start B while any A checkbox, focused test, exit gate, or review finding remains open.

- [ ] **Step 3: Run the A focused gate**

```bash
cmake --build --preset dev --target pnga_trace_model_tests pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R 'offset_range|trace_orchestrator|trace_query|block_inspector' --output-on-failure
```

Expected: every matching test passes and exact range/Adler/truncation assertions are active.

- [ ] **Step 4: Run the Mandatory Task Exit Gate and inspect A commits**

Run the commands under **Mandatory Task Exit Gate**. Confirm no GUI copy, navigation action, selection semantics, or replay budgets changed.

- [ ] **Step 5: Approve the A handoff**

The A handoff record must list the final type names, field names, status enums, test count, and commit hashes. B consumes that record verbatim.

### Task 2: Execute WP-5U12B — Selection and Navigation

**Files:**
- Plan: `docs/superpowers/plans/2026-09-01-wp-5u12b-selection-navigation.md`
- Verify: `tests/unit/trace-model/selection_test.cpp`
- Verify: `tests/gui/selection_bus_test.cpp`
- Verify: `tests/gui/trace_pipeline_integration_test.cpp`

**Interfaces:**
- Consumes: the exact A typed domains and multi-span mapping.
- Produces: `CompressionNavigationTarget`, `CompressionSelectionState`, generation gate, current/manual selection coexistence, and loop-suppressed navigation consumed by C–E.

- [ ] **Step 1: Verify A is the direct predecessor**

```bash
git log --oneline -3
git status --short
```

Expected: the approved A commits are present and the worktree has no unexpected file.

- [ ] **Step 2: Execute every checkbox in the B plan**

Keep current mapping immutable under row selection; keep manual selection intact under current-coordinate updates; reject stale generations before UI publication.

- [ ] **Step 3: Run the B focused gate**

```bash
cmake --build --preset dev --target pnga_trace_model_tests pnga_gui_compression_selection_store_tests pnga_gui_selection_bus_tests pnga_gui_selection_navigation_controller_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'compression_selection_store|selection_bus|selection_navigation|trace_pipeline' --output-on-failure
```

Expected: multi-span round-trip, stale rejection, coexistence, history, and loop suppression pass.

- [ ] **Step 4: Run the Mandatory Task Exit Gate and inspect B commits**

Confirm trace submission counts are unchanged for page switch, row selection, resize, and numeric-base changes.

- [ ] **Step 5: Approve the B handoff**

Record exact target/state type signatures and navigation signals. C, D, and E may not define alternate integer-only navigation payloads.

### Task 3: Execute WP-5U12C — Blocks Page

**Files:**
- Plan: `docs/superpowers/plans/2026-09-01-wp-5u12c-blocks-page.md`
- Verify: `tests/unit/analysis-engine/block_inspector_test.cpp`
- Verify: `tests/gui/block_inspector_test.cpp`
- Verify: `tests/gui/compression_inspector_responsive_test.cpp`

**Interfaces:**
- Consumes: A Fast Index facts and B typed selection/navigation.
- Produces: complete Blocks model/page, stream summary, bit-precise `ProvenanceSpan` rows, Current/Selection rendering, and typed Block drill-down used by D/E.

- [ ] **Step 1: Verify B is the direct predecessor**

```bash
git log --oneline -3
git status --short
```

Expected: approved A and B changes are present and no unexpected edits exist.

- [ ] **Step 2: Execute every checkbox in the C plan**

The complete block list must remain visible without X/Y Lock. Row selection changes only manual selection and details; it submits no trace until explicit `Open Decode Trace`.

- [ ] **Step 3: Run the C focused gate**

```bash
cmake --build --preset dev --target pnga_gui_block_inspector_tests pnga_gui_compression_inspector_responsive_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'block_inspector|compression_inspector_responsive|trace_pipeline' --output-on-failure
```

Expected: complete Blocks, multi-span navigation, no-lock availability, widths, and zero incidental replay pass.

- [ ] **Step 4: Run the Mandatory Task Exit Gate and inspect C commits**

Confirm Huffman and Decode Trace result fields/copy remain unchanged except the shared B navigation contract.

- [ ] **Step 5: Approve the C handoff**

Record the Block model roles, selected block API, explicit actions, screenshots/tests, and commit hashes.

### Task 4: Execute WP-5U12D — Huffman Page

**Files:**
- Plan: `docs/superpowers/plans/2026-09-01-wp-5u12d-huffman-page.md`
- Verify: `tests/unit/analysis-engine/huffman_inspector_test.cpp`
- Verify: `tests/gui/huffman_inspector_test.cpp`
- Verify: `tests/gui/trace_pipeline_integration_test.cpp`

**Interfaces:**
- Consumes: A typed bit domains, B selection/navigation, and C selected Block identity.
- Produces: Stored/Fixed/Dynamic Huffman projection, fixed-width canonical/read-order strings, bounded uses, and occurrence navigation.

- [ ] **Step 1: Verify C is the direct predecessor**

```bash
git log --oneline -3
git status --short
```

Expected: A–C handoff commits exist and no unexpected edits exist.

- [ ] **Step 2: Execute every checkbox in the D plan**

Do not reverse bits in Qt. Do not count whole-stream occurrences. Zero-bit entries remain hidden by default while stored/fixed/partial states remain explicit.

- [ ] **Step 3: Run the D focused gate**

```bash
cmake --build --preset dev --target pnga_deflate_trace_tests pnga_analysis_engine_tests pnga_gui_huffman_inspector_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'huffman_inspector|trace_pipeline' --output-on-failure
```

Expected: canonical/read-order golden values, scope-labelled uses, Stored/Fixed/Dynamic, selection coexistence, and occurrence navigation pass.

- [ ] **Step 4: Run the Mandatory Task Exit Gate and inspect D commits**

Confirm Blocks and Decode Trace continue to pass unchanged and no unbounded symbol/event index was introduced.

- [ ] **Step 5: Approve the D handoff**

Record projection fields, model roles, occurrence target semantics, test count, and commit hashes.

### Task 5: Execute WP-5U12E — Decode Trace Page

**Files:**
- Plan: `docs/superpowers/plans/2026-09-01-wp-5u12e-decode-trace-page.md`
- Verify: `tests/unit/analysis-engine/decode_trace_inspector_test.cpp`
- Verify: `tests/gui/decode_trace_inspector_test.cpp`
- Verify: `tests/gui/trace_pipeline_integration_test.cpp`

**Interfaces:**
- Consumes: A domains, B typed navigation, C Block identity, and D occurrence selection.
- Produces: bounded semantic event model/page, match provenance, and separately typed compressed-input/inflated-output actions.

- [ ] **Step 1: Verify D is the direct predecessor**

```bash
git log --oneline -3
git status --short
```

Expected: A–D handoffs are present and no unexpected edits exist.

- [ ] **Step 2: Execute every checkbox in the E plan**

Literal, Match, and EOB semantics must remain bounded. `Show in Hex` targets compressed input; `Show inflated output` targets inflated bytes; neither shares an untyped integer signal.

- [ ] **Step 3: Run the E focused gate**

```bash
cmake --build --preset dev --target pnga_analysis_engine_tests pnga_gui_decode_trace_inspector_tests pnga_gui_trace_pipeline_integration_tests pnga_gui_trace_controller_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'decode_trace|trace_pipeline|trace_controller' --output-on-failure
```

Expected: Literal/Match/EOB, overlap/source/target/current-byte detail, typed actions, bounded scope, stale rejection, and zero incidental replay pass.

- [ ] **Step 4: Run the Mandatory Task Exit Gate and inspect E commits**

Confirm no whole-file token retention, UI-side decoding, or first-span-only navigation exists.

- [ ] **Step 5: Approve the E handoff**

Record event roles, details fields, navigation targets, test count, and commit hashes.

### Task 6: Execute WP-5U12F — Product Gate

**Files:**
- Plan: `docs/superpowers/plans/2026-09-01-wp-5u12f-product-gate.md`
- Verify: all focused/model/GUI/integration/performance tests named by that plan.
- Modify: `docs/development/wp-5u12-compression-inspector-completion.md` only after every gate passes.

**Interfaces:**
- Consumes: final A–E interfaces and controlled fixtures from WP-607C.
- Produces: objective evidence for normative sections 19–20 and the only authorization to mark WP-5U12 complete.

- [ ] **Step 1: Verify E and WP-607C prerequisites**

```bash
git log --oneline -5
git status --short
grep -R -n -E 'stored|fixed|dynamic|cross[-_ ]?idat|adler[-_ ]?mismatch|truncated|reserved|invalid[-_ ]?distance|overlap|narrow|large' tests/corpus tests/common tests/unit tests/gui tests/fuzz
```

Expected: A–E handoffs are present. Every controlled category is either manifest-backed or has a stable generator case id, fixed arguments, exact assertions, and an owning CTest target. An empty corpus manifest is acceptable only for generator-backed cases; a tracked fixture without metadata or a missing category reports `BLOCKED: WP-607C fixture prerequisite`.

- [ ] **Step 2: Execute every checkbox in the F plan**

F may fix only defects exposed by its gate and must add a failing regression before each fix. It does not redesign A–E contracts silently.

- [ ] **Step 3: Run the final product gate**

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
git status --short
```

Expected: every command exits 0, all CTests pass, GUI/sanitizer/performance gates report PASS, and status contains only the intended F evidence/doc changes before commit.

- [ ] **Step 4: Perform the normative side-effect audit**

Confirm:

- PNG open/close/reload/rapid-switch behavior still passes;
- Reconstruction, Pixels, Filtered, Defiltered, File/IDAT/Inflated/Defiltered Hex still pass;
- APNG/Statistics/Compare files are unchanged;
- page switch, row select, splitter resize, theme change, and DEC/HEX submit zero replays;
- no table creates one QWidget per data row;
- memory/token/index budgets remain bounded;
- every screenshot/state required by section 20.8 has an evidence record.

- [ ] **Step 5: Mark WP-5U12 complete and commit**

Only after Steps 1–4 pass, set completion status and append exact commands, test counts, performance values, evidence paths, and commit hashes to the completion document. Run the Mandatory Task Exit Gate once more after the documentation commit.

## Handoff Rule

A single agent may receive the entire Master Plan, but it must execute Task 0 first, stop at each stage boundary, and present the exit-gate evidence before beginning the next child plan. It must stop again after E until WP-607C is completed or revalidated. When using subagent-driven development, dispatch a fresh implementation agent for each child plan and perform specification review plus code-quality review before accepting its commits. No agent is authorized to skip, merge, reorder, or parallelize the six stages.
