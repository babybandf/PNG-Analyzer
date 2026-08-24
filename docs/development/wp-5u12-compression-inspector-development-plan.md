# WP-5U12 — Compression Inspector Development Plan

Status: **implemented (2026-08-24)**  
Active Work Package: `wp-5u12-compression-inspector-flow-ui.md`  
Precondition: **WP-5U13** must be landed first (application wiring of the
bounded trace pipeline). This plan refines the presentation of a live bundle;
it does not wire, submit or cancel trace requests.  
Goal: refine the wired bounded DEFLATE Inspector into a natural, responsive
master/detail workflow without changing decoder behavior or on-demand trace
scope.

## 1. Execution boundary

This plan is intentionally UI-first and bounded-result-first.

The implementation reuses:

- WP-5U13 application wiring (orchestrator lifecycle, interval mapping, thread
  bridge, `TraceInspectorBinding` publication, navigation wiring);
- WP-5T0A immutable, budgeted `TraceQueryResult`;
- WP-5T0B `TraceOrchestrator` and generation/cancellation rules;
- WP-505A `BlockInspectorView`;
- WP-505B `HuffmanInspectorView`;
- WP-505C `DecodeTraceInspectorView`; and
- M5 Trace Gate bundle publication.

It does not authorize:

- a decoder change;
- default full-file token tracing;
- a larger replay budget for visual completeness;
- IDAT concatenation;
- Qt under `libs/`;
- redesign of unrelated Preview, Reconstruction or Hex source pages; or
- any trace request submission from presentation code (owned by WP-5U13).

## 2. Required preflight

Before editing code, the implementing Agent must:

1. Read `AGENTS.md`, root and complete `REPOSITORY_LAYOUT.md`.
2. Read ADR-0003, ADR-0004, ADR-0005 and ADR-0006.
3. Read WP-5T0A, WP-5T0B, WP-505A/B/C, M5 Trace Gate, WP-5U10, WP-5U11 and
   **WP-5U13**.
4. Inspect the working tree and preserve unrelated changes.
5. Record current component ownership, signal units, row limits and test names.
6. Confirm WP-5U13 is landed: opening a deterministic fixture PNG and committing
   a pixel makes all three pages render a live, same-generation result and the
   navigation signals are wired. If not landed, report `BLOCKED`.
7. State the cheapest discriminating test before implementation.

The initial discriminating test is:

> Given a deterministic fixture PNG with a committed locked pixel, the
> WP-5U13-wired application publishes one ready `TraceInspectorBundle`
> containing a Dynamic associated block, one selected Huffman entry and
> Literal/Match/EOB tokens; all three pages render master/detail content from
> the same generation; changing subpages emits no replay request.

## 3. Phase and gate overview

| Phase | Outcome | Gate |
|---|---|---|
| D0 | Audit and fixed baseline (WP-5U13 landed) | No architecture/data-unit ambiguity remains |
| D1 | Static normative UI skeleton | 360/480 px light baseline accepted |
| D2 | Shared state/context | Stable states replace `no trace` |
| D3 | Blocks master/detail | WP-505A facts and actions preserved |
| D4 | Huffman master/detail | Stored/Fixed/Dynamic facts preserved |
| D5 | Decode Trace master/detail | Literal/Match/EOB facts preserved |
| D6 | Bundle/selection/navigation presentation | No duplicate replay or stale publish |
| D7 | Responsive/accessibility/performance | Existing GUI gates plus new matrix pass |
| D8 | Full regression and evidence | WP Definition of Done has evidence |

## 4. D0 — Audit and baseline

### 4.1 Inspect

- `MainWindow` creation and ownership of the Compression tab stack, and the
  WP-5U13 wiring it consumes (orchestrator, binding, thread bridge,
  navigation).
- `BlockInspector`, `HuffmanInspector`, `DecodeTraceInspector` construction,
  clear, `setView`, external status and navigation methods.
- Actual WP-505 Qt-free projection fields.
- `TraceInspectorBinding` bundle publication and callback thread handoff.
- The `TraceInspectorLifecycle` values available for the shared status line.
- Current Selection/Lock/X/Y to inflated-output interval mapping (owned by
  WP-5U13; do not reimplement).
- WP-5U11 Hex source selection and range units.
- Existing row cap (`kMaxVisibleRows`) and truncation behavior.
- Current GUI/component/gate tests and screenshot utilities.

### 4.2 Produce

Create an implementation note or PR description table:

| UI fact | Existing projection field | Display owner | Gap/action |
|---|---|---|---|
| Trace state/generation | `TraceInspectorLifecycle` via binding | Shared context |  |
| Associated block/current position | `BlockInspectorView` rows | Blocks |  |
| Physical IDAT spans | `BlockInspectorRow.physical_spans` | Block details |  |
| Stored/Fixed/Dynamic table mode | `HuffmanInspectorTable.mode/kind` | Huffman |  |
| Build order/symbol/bits/canonical | `HuffmanInspectorEntry` | Huffman |  |
| Selected token/table entry | `selected` / `selected_token_index` | Huffman |  |
| Literal/Match/EOB | `DecodeTraceStep` | Trace |  |
| Length/distance arithmetic | `DecodeTraceStep` length/distance fields | Trace details |  |
| Overlap-safe source ranges | `DecodeTraceStep.match_source_ranges` | Trace details |  |
| Hex/DEFLATE navigation range | WP-505A/C signals (wired by WP-5U13) | Actions |  |

### 4.3 Gate

- Every P0 field maps to an existing projection or one documented minimal
  Qt-free projection gap.
- No required visual depends on a whole-file token count or occurrence scan.
- Existing navigation units are documented before labels/layout change.
- WP-5U13 is landed (live bundle + navigation wiring); no re-implementation.
- No decoder, index, runtime or third-party path is required.

If a gate fails, report `BLOCKED` with the smallest follow-up Work Package; do
not expand WP-5U12 locally.

## 5. D1 — Static normative UI skeleton

Build the UI hierarchy using deterministic existing projection fixtures before
changing live binding behavior.

### 5.1 Shared structure

```text
Compression subpages
Shared status line
Shared current-mapping line
Page stack
  master table
  details area
page navigation actions
```

Prefer a small reusable presentation helper only when it reduces duplication
without becoming a generic catch-all widget. Keep page-specific fields and
actions in their existing page classes.

### 5.2 Fixture states

- Dynamic associated Block that is both Current and manually selected.
- Dynamic Huffman Literal/Length entry associated with the selected token.
- Literal, overlapping Match and EOB token rows.
- Stored Block/Huffman state.
- Replaying, Partial and Error status.

Use existing test constructors/builders. Do not add external fixture files for
layout-only tests.

### 5.3 Visual gate

Capture:

- all three pages at 360 and 480 px, light;
- all three pages at 480 px, dark when deterministic locally;
- Stored Huffman;
- Current plus manual Selection; and
- Replaying/Partial/Error.

Before proceeding:

- no tab/header/button is clipped;
- shared context is two compact lines;
- master/details order matches the Work Package;
- Current and Selection are distinguishable;
- Inspector minimum width is unchanged; and
- page switch produces no model request.

## 6. D2 — Shared trace context and states

### 6.1 Implement

- Move repeated user-facing status/current context into one shared owner at the
  Compression stack boundary or an equivalent single-source adapter.
- Map exact stable states: not indexed, replaying, ready, partial, error,
  cancelled. The shared status line reads the `TraceInspectorLifecycle`
  published by `TraceInspectorBinding::publishState`; per-page projection
  statuses stay page-local detail.
- Replace `Block/Huffman/Decode trace: no trace` strings.
- Keep page-specific diagnostic details available without repeating the same
  generation text three times.
- Ensure partial/error keeps the last verified rows belonging to the same
  generation.

### 6.2 Do not

- Infer trace availability from empty row vectors alone.
- Treat no current pixel as error.
- Publish cancelled results from a stale generation.
- Start trace on tab activation or resize.
- Drive the shared status from a per-page projection status that cannot express
  replaying or cancelled.

### 6.3 Gate

- Component tests cover every state.
- One bundle update causes one coherent shared context update.
- Switching pages changes neither generation nor request count.

## 7. D3 — DEFLATE Blocks master/detail

### 7.1 Master table

Reduce the visible table to:

```text
Current | # | Type | Final | Input bits | Output bytes
```

Use the existing half-open ranges without widening or changing units.

### 7.2 Details

Move to details:

- physical spans;
- selected/current output position;
- scanline context (the single caller-supplied value when present; `—` with a
  short explanation otherwise; no fabricated range or `mixed` label); and
- copyable full range text.

### 7.3 Interaction

- Preserve stable selected block index where available.
- Current marker does not depend on table selection.
- Existing navigation actions keep their signal units and enablement.
- Do not silently collapse a multi-span provenance relationship into one visual
  claim of complete coverage.

### 7.4 Gate

- Existing WP-505A projection tests still pass.
- Component test verifies Current plus different manual Selection.
- Partial view preserves verified block rows.
- No physical offset arithmetic moves into Qt beyond checked formatting already
  owned by the projection/widget boundary.

## 8. D4 — Huffman master/detail

### 8.1 Local table selector

Provide:

```text
Code length | Literal / Length | Distance
```

The selector filters already-published bounded tables
(`HuffmanInspectorTable.kind`; Stored maps to LEN/NLEN). It must not request a
larger trace result.

### 8.2 Master table

At the reference width show:

```text
Build | Symbol | Bits | Canonical | Definition bits
```

Block/mode/table kind belong in the section heading and details. At wider
widths they may remain as columns if doing so does not duplicate or distort the
reference layout.

### 8.3 Mode states

- Stored: LEN/NLEN plus plain-language no-Huffman explanation.
- Fixed: predefined capacities/entries supplied by the projection.
- Dynamic: build order, table kind, symbol, bit length, canonical value and
  provenance.

### 8.4 Semantic guardrails

- Label the existing field `Canonical`, not generic `Code`.
- Do not generate stream-read bit strings in Qt.
- Do not present table-definition provenance as a token occurrence.
- Do not display whole-block use counts without a correctly scoped field.

### 8.5 Gate

- Existing WP-505B projection tests pass.
- Stored/Fixed/Dynamic component states pass.
- Changing local table kind emits no replay.
- Selected token entry mapping remains exact.

## 9. D5 — Decode Trace master/detail

### 9.1 Master table

Reduce the visible table to:

```text
Current | Token | Path | Input bits | Output bytes
```

### 9.2 Details

Move to details:

- Huffman symbol;
- Literal relation;
- length base + extra and decoded result;
- distance base + extra and decoded result;
- match source ranges/root origins;
- selected output byte; and
- EOB explanation.

### 9.3 Guardrails

- Copy Match values from the projection; do not recompute them in Qt.
- Preserve overlap-safe source semantics.
- Do not automatically recurse through Match sources.
- Retain bounded truncation/partial indication.

### 9.4 Gate

- Existing WP-505C projection tests pass.
- Literal, Match and EOB component tests pass.
- Overlapping Match detail matches the deterministic fixture.
- Manual Selection and Current token coexist.
- Navigation buttons keep existing range units.

## 10. D6 — Bundle, Selection and navigation presentation

### 10.1 Bundle consumption

- Consume the bundle published by WP-5U13 as one generation-coherent update.
- Never partially mix page generations.
- Clear/replace old-document content atomically.
- Presentation adds no request submission and no navigation wiring change.

### 10.2 Request triggers

Track request counts in tests (the submission path itself is owned and tested
by WP-5U13; this WP verifies it adds none):

| Action | Expected bounded replay request |
|---|---:|
| Explicit committed pixel/X/Y selection | 0 or 1 (WP-5U13, deduped per interval) |
| Hover | 0 |
| Compression subpage switch | 0 |
| Manual row selection | 0 |
| Local Huffman table switch | 0 |
| DEC/HEX switch | 0 |
| Inspector resize | 0 |

### 10.3 Navigation

- Verify Block `Show in Hex`/`Show in DEFLATE` units (wired by WP-5U13).
- Verify Trace `Show in Hex`/`Show in DEFLATE` units (wired by WP-5U13).
- Verify WP-5U11 File/IDAT/Inflated/Defiltered source switching remains correct.
- Verify physical, logical IDAT, DEFLATE bit and Inflated byte ranges are never
  interchanged.
- Keep any multi-span limitation explicit.

### 10.4 Gate

- Navigation matrix integration tests pass.
- No re-entrant selection loop.
- No stale or cancelled callback updates any page/action.
- Workspace restoration keeps the selected Compression subpage without replay.

## 11. D7 — Responsive, accessibility and performance

### 11.1 Width matrix

Run each page at body widths:

```text
320 · 360 · 480 · 600 logical px
```

Verify:

- table scroll stays inside the page;
- details wrap and remain copyable;
- tabs use scroll buttons rather than ambiguous clipping;
- action labels remain complete;
- master/details remain usable; and
- no new minimum width propagates to the Inspector Dock.

### 11.2 DPI/theme

- Reuse WP-5U6C default/150%/200% GUI gate.
- Reuse current theme tokens; do not hardcode a second palette.
- Record local light/dark evidence where deterministic.

### 11.3 Accessibility

- Accessible names for shared status/context, each table, details and actions.
- Current/Selection/Partial/Error not color-only.
- Stable tab/table/action focus order.
- Status update does not steal focus.
- Full long values available to assistive technology.

### 11.4 Performance

- Retain bounded row count and replay budgets.
- Confirm page switch/resize/row selection performs no replay or file I/O.
- Compare allocation/object counts before and after repeated page switching.
- Do not migrate to an unbounded table model to solve visual layout.

### 11.5 Gate

- WP-5U6A failure-state gate passes.
- WP-5U6B performance gate passes.
- WP-5U6C cross-platform GUI gate passes locally.
- New responsive/truncation/accessibility assertions pass.

## 12. D8 — Full regression and evidence

### 12.1 Required verification

```bash
python3 scripts/verify_repository_layout.py
cmake --preset dev
cmake --build --preset dev -j
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
```

Also run the focused WP-505, M5 Trace Gate, WP-5U13 and GUI gate targets
identified in D0. Run sanitizer/differential levels required by the repository
for any conditional Qt-free projection change.

### 12.2 Manual matrix

- No file.
- Valid PNG with committed pixel.
- Stored, Fixed and Dynamic associated blocks.
- Literal, Match, overlap Match and EOB.
- Replaying, Ready, Partial, Error and superseded/cancelled request.
- Multiple IDAT segments.
- Reconstruction ↔ Compression and all three Compression subpages.
- File/IDAT/Inflated/Defiltered Hex sources.
- 320/360/480/600 px and available theme/DPI combinations.
- Reload, new file, rapid selection changes and application close.

### 12.3 Evidence package

- D0 field/owner/gap matrix.
- Before/after screenshots required by WP Section 14.7.
- Request-count evidence proving no replay from page switching (baseline from
  WP-5U13; this WP shows no regression).
- Existing and new test commands/results.
- Changed-path review.
- Performance/object-count comparison.
- Explicit remaining native-platform evidence, if any.

## 13. Recommended commit sequence

1. `ui: add shared bounded trace context`
2. `ui: establish compression master-detail skeleton`
3. `ui: refine associated block inspector`
4. `ui: refine bounded Huffman inspector`
5. `ui: refine bounded decode trace inspector`
6. `ui: preserve compression selection and navigation state`
7. `tests: add responsive state and accessibility gates`
8. `docs: record WP-5U12 verification evidence`

If a minimal Qt-free projection extension is required, isolate it before its UI
consumer with focused tests. Do not combine decoder/index changes or WP-5U13
wiring with this sequence.

## 14. Stop conditions

Report `BLOCKED` instead of proceeding when:

- WP-5U13 is not landed (no live bundle or navigation wiring), and the gap
  cannot be closed by landing WP-5U13;
- required data is absent from `TraceQueryResult` and adding it would broaden
  replay or decoder behavior;
- the requested UI requires an unbounded whole-file token/table list;
- navigation correctness requires changing accepted offset semantics;
- implementation would concatenate IDAT payloads;
- implementation requires Qt under `libs/`;
- implementation requires `third_party/`, new dependencies or corpus assets;
- a presentation action would submit a new trace request or change WP-5U13
  wiring;
- WP-5U9/WP-5U11 integration conflicts cannot be solved through existing
  interfaces; or
- the 320/360 px requirement can only be met by changing the application-wide
  Inspector minimum-width contract.

The blocking report must name the accepted contract, the exact missing field or
capability, and the smallest follow-up Work Package needed.

## 15. Final checklist

- [ ] D0 field/owner/gap matrix completed.
- [ ] WP-5U13 landed: live bundle and navigation wiring confirmed; not
      re-implemented.
- [ ] No decoder, index, runtime, third-party or corpus path changed.
- [ ] Shared status uses stable bounded trace states from
      `TraceInspectorLifecycle`.
- [ ] No ambiguous `no trace` string remains.
- [ ] Blocks page is master/detail and preserves WP-505A facts.
- [ ] Huffman page handles Stored/Fixed/Dynamic and labels Canonical correctly.
- [ ] Decode Trace handles Literal/Match/EOB and preserves overlap-safe sources.
- [ ] Current and manual Selection coexist.
- [ ] No page switch, row selection, DEC/HEX change or resize triggers replay.
- [ ] No presentation action submits a trace request (WP-5U13 owns submission).
- [ ] Explicit requests remain bounded, cancelable and generation-safe.
- [ ] Hex/DEFLATE navigation units remain correct.
- [ ] 320/360/480/600 px tests pass without Dock width growth.
- [ ] DPI/theme/truncation/accessibility gates pass.
- [ ] Partial/error preserves verified rows.
- [ ] Repository layout, focused tests and full regression pass.
- [ ] Evidence and any approved deviations are recorded.
