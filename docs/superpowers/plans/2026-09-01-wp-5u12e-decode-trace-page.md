# WP-5U12E Decode Trace Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a bounded, model-backed Decode Trace page whose Literal, Match, and EOB rows expose exact typed compressed input, inflated output, Match provenance, Current/Selection state, and distinct Hex/output navigation.

**Architecture:** `analysis-engine` converts bounded token facts into complete semantic steps and typed multi-IDAT mappings. `DecodeTraceModel` lazily exposes those immutable steps; the page owns selection/details/actions only. Existing generation/cancellation/budget controls remain the sole producer of trace results.

**Tech Stack:** C++20, bounded TraceQueryResult, Qt 6.8+ QAbstractTableModel/QTableView, Catch2, Qt Test, WP-5U12B navigation.

**Spec:** `docs/development/wp-5u12-compression-inspector-completion.md` section WP-5U12E and `docs/development/wp-5u12-compression-inspector-flow-ui.md` sections 8, 10, 13–14, 16–17, 19–20.

## Global Constraints

- Start only from reviewed WP-5U12D; consume B target/state and D occurrence token identity verbatim.
- Title/context identifies bounded output range, returned token count, status, and truncation; it never implies a whole-stream trace.
- Columns are exactly `Current | Step | Input bits | Event | Output`.
- Literal, Match, and EOB remain distinct structured paths; GUI never parses or constructs semantic event text from debug strings.
- Match details include length/distance base, extra-bit count/value, source/target, overlap, selected-byte offset, and root source token ranges.
- `Show in Hex` carries compressed DeflateBitRange plus all File spans. `Show inflated output` carries InflatedByteRange and no compressed scalar.
- Current and Manual Selection coexist. Row selection emits zero replay; only an explicit scope/drill-down request may submit bounded trace.
- Partial/Error retains verified rows and stop reason. Loading keeps already published facts. Empty uses normative human text, never `no trace`.
- Use model/view; no QTableWidget, per-row widget, untyped integer signal, IDAT concatenation, second decode, or unbounded retention.
- Preserve 320/360/480/600 behavior, geometry, keyboard, copy, accessibility, theme tokens, and locked English labels.
- Do not modify Blocks/Huffman semantics, budgets, parser, packaging, Compare, Statistics, or APNG.

---

## File Structure

| Path | Responsibility |
|---|---|
| `libs/analysis-engine/include/pnga/analysis-engine/trace_query.h` | Per-token typed physical input spans |
| `libs/analysis-engine/src/trace_query.cpp` | Checked Deflate→zlib→File mapping during existing composition |
| `tests/unit/analysis-engine/trace_query_test.cpp` | One/two-IDAT exact token mapping tests |
| `libs/analysis-engine/include/pnga/analysis-engine/decode_trace_inspector.h` | Typed semantic step/scope projection |
| `libs/analysis-engine/src/decode_trace_inspector.cpp` | Literal/Match/EOB facts and mapping |
| `tests/unit/analysis-engine/decode_trace_inspector_test.cpp` | Exact event/match/partial golden tests |
| `ui/qt/include/pnga/ui/qt/decode_trace_model.h` | Columns/roles/lazy model API |
| `ui/qt/src/decode_trace_model.cpp` | Model formatting and accessible values |
| `ui/qt/include/pnga/ui/qt/decode_trace_inspector.h` | Product page and typed signal |
| `ui/qt/src/decode_trace_inspector.cpp` | View/details/actions/current+selection |
| `ui/qt/CMakeLists.txt` | Build model source |
| `tests/gui/decode_trace_inspector_test.cpp` | UI/model/navigation tests |
| `tests/gui/compression_inspector_responsive_test.cpp` | Trace width behavior |
| `ui/qt/src/trace_inspector_binding.cpp` | Publish bounded result and B state |
| `apps/png-analyzer-gui/src/trace_controller.cpp` | Preserve explicit bounded submission policy |
| `tests/gui/trace_controller_test.cpp` | Replay count/generation/cancel tests |
| `tests/gui/trace_pipeline_integration_test.cpp` | Pixel/Block/Huffman↔Trace↔Hex/output flows |

## Required Interfaces

Extend the existing bounded token summary:

```cpp
struct TraceTokenSummary {
  // Existing fields remain.
  std::vector<FileByteRange> physical_input_spans;
};
```

`TraceQueryResult::deflate_data_begin` is the existing scalar byte origin supplied by deflate-trace; normalize it immediately as `ZlibByteOffset{trace.deflate_data_begin}`. During `compose_trace_query`, checked-multiply `deflate_origin.raw_value()` by 8, checked-add each token's DeflateBitRange to that zlib-bit origin, map its byte envelope through `VirtualIDATStream::logical_to_physical`, and retain every ordered FileByteRange. This is the same byte unit as A's `FastCompressionStreamSummary::deflate_data_begin`; do not multiply a `ZlibBitOffset` by 8. The DeflateBitRange remains the precise bit range; File spans are the exact containing data bytes used by Hex.

```cpp
struct DecodeTraceScope {
  std::uint64_t generation = 0;
  InflatedByteRange requested_output{};
  std::uint64_t returned_token_count = 0;
  TraceQueryStatus status = TraceQueryStatus::kNotIndexed;
  bool truncated = false;
  std::string stop_reason;
  bool operator==(const DecodeTraceScope&) const = default;
};

struct DecodeTraceStep {
  std::uint64_t token_index = 0;
  std::int64_t block_index = -1;
  DecodeTracePath path = DecodeTracePath::kLiteral;
  DeflateBitRange input_range{};
  std::vector<FileByteRange> physical_input_spans;
  InflatedByteRange output_range{};
  std::string event_text;
  std::optional<std::uint16_t> huffman_symbol;
  std::uint8_t literal = 0;
  std::uint16_t length = 0;
  std::uint16_t distance = 0;
  std::uint16_t length_base = 0;
  std::uint8_t length_extra_bits = 0;
  std::uint16_t length_extra_value = 0;
  std::uint16_t distance_base = 0;
  std::uint8_t distance_extra_bits = 0;
  std::uint16_t distance_extra_value = 0;
  std::vector<TokenOutputRange> match_source_ranges;
  InflatedByteRange match_target{};
  bool match_overlaps = false;
  bool contains_current = false;
  bool selected = false;
  std::optional<std::uint64_t> selected_byte_offset_in_event;
  bool operator==(const DecodeTraceStep&) const = default;
};

struct DecodeTraceInspectorView {
  DecodeTraceScope scope;
  std::vector<DecodeTraceStep> steps;
};
```

`selected_byte_offset_in_event` is zero-based within `output_range`; it is set only when Current output intersects that step. `match_overlaps` is true exactly when immediate source and target copy regions overlap. Physical spans preserve source order.

```cpp
class DecodeTraceModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column { Current = 0, Step, InputBits, Event, Output, ColumnCount };
  void setView(std::shared_ptr<const DecodeTraceInspectorView> view);
  void setSelectionState(const CompressionSelectionState& state);
  const DecodeTraceStep* stepAt(int row) const noexcept;
};
```

`DecodeTraceInspector` emits only `navigationRequested(const CompressionNavigationTarget&)`; remove legacy `showInHexRequested(quint64, quint64)` after callers/tests migrate.

### Task 1: Complete the Qt-free semantic step projection

**Files:**
- Modify: `libs/analysis-engine/include/pnga/analysis-engine/trace_query.h`
- Modify: `libs/analysis-engine/src/trace_query.cpp`
- Modify: `tests/unit/analysis-engine/trace_query_test.cpp`
- Modify: `libs/analysis-engine/include/pnga/analysis-engine/decode_trace_inspector.h`
- Modify: `libs/analysis-engine/src/decode_trace_inspector.cpp`
- Modify: `tests/unit/analysis-engine/decode_trace_inspector_test.cpp`

- [ ] **Step 1: Add failing exact golden tests**

Cover Literal byte/text, Match length/distance base+extra/value, non-overlap and overlap, target/source root ranges, EOB, selected-byte offset at first/middle/last byte, exact DeflateBitRange, one/two File spans, bounded scope title facts, Partial/Error retention, and invalid-distance stop text.

- [ ] **Step 2: Confirm the missing per-token mapping failure**

```bash
cmake --build --preset dev --target pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R decode_trace_inspector --output-on-failure
```

Expected: compilation fails because `TraceTokenSummary::physical_input_spans`, `DecodeTraceScope`, typed ranges, or new semantic fields are absent, or the new exact mapping assertions fail. `DecodeTraceStep::huffman_symbol` already exists and is not the expected red-test cause.

- [ ] **Step 3: Implement from structured facts only**

Use TraceTokenSummary, D symbol, and typed checked conversions. Add per-token physical spans during the existing `compose_trace_query` pass using VirtualIDATStream; the inspector then copies them without reading source bytes. Format stable event text in analysis-engine. Calculate overlap with checked half-open range intersection and preserve verified steps after error/truncation. Do not replay or decode again.

- [ ] **Step 4: Test and commit**

```bash
ctest --preset dev -R 'decode_trace_inspector|trace_query' --output-on-failure
git add libs/analysis-engine/include/pnga/analysis-engine/trace_query.h libs/analysis-engine/src/trace_query.cpp tests/unit/analysis-engine/trace_query_test.cpp libs/analysis-engine/include/pnga/analysis-engine/decode_trace_inspector.h libs/analysis-engine/src/decode_trace_inspector.cpp tests/unit/analysis-engine/decode_trace_inspector_test.cpp
git commit -m "feat: complete decode trace semantics"
```

### Task 2: Replace QTableWidget with DecodeTraceModel

**Files:**
- Create: `ui/qt/include/pnga/ui/qt/decode_trace_model.h`
- Create: `ui/qt/src/decode_trace_model.cpp`
- Modify: `ui/qt/include/pnga/ui/qt/decode_trace_inspector.h`
- Modify: `ui/qt/src/decode_trace_inspector.cpp`
- Modify: `ui/qt/CMakeLists.txt`
- Modify: `tests/gui/decode_trace_inspector_test.cpp`

- [ ] **Step 1: Rewrite tests to fail against old widget**

Require QTableView `compressionDecodeTraceTable`, five exact headers, no QTableWidget/index widgets, scope text, Literal/Match/EOB rows, details with every Match field, Current+Selection, Partial rows, copy, accessible text, and Up/Down/Home/End/Page keys.

- [ ] **Step 2: Confirm failure**

```bash
cmake --build --preset dev --target pnga_gui_decode_trace_inspector_tests --parallel 4
```

- [ ] **Step 3: Implement model/view/details**

Model returns domain-labelled input/output displays and accessible text. Details render structured projection only. Use theme palette roles; Current icon cannot obscure native selection/focus. Loading/Empty/Partial/Error copy follows sections 13 and 20.

- [ ] **Step 4: Test and commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --preset dev -R gui_decode_trace_inspector --output-on-failure
git add ui/qt/include/pnga/ui/qt/decode_trace_model.h ui/qt/src/decode_trace_model.cpp ui/qt/include/pnga/ui/qt/decode_trace_inspector.h ui/qt/src/decode_trace_inspector.cpp ui/qt/CMakeLists.txt tests/gui/decode_trace_inspector_test.cpp
git commit -m "feat: productize decode trace model"
```

### Task 3: Wire typed compressed/output navigation and cross-page state

**Files:**
- Modify: `ui/qt/src/trace_inspector_binding.cpp`
- Modify: `apps/png-analyzer-gui/src/trace_controller.cpp`
- Modify: `tests/gui/trace_controller_test.cpp`
- Modify: `tests/gui/trace_pipeline_integration_test.cpp`

- [ ] **Step 1: Add failing interaction tests**

Assert pixel Current highlights intersecting Block/events; row Manual Selection preserves Current; Huffman occurrence selects exact token; Show in Hex sends DeflateBitRange and all File spans; Show inflated output sends only InflatedByteRange; Back restores prior symbol; new generation drops old result; row/page/resize/DEC↔HEX submit zero.

- [ ] **Step 2: Confirm failure**

```bash
cmake --build --preset dev --target pnga_gui_trace_controller_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
```

- [ ] **Step 3: Bind one state and two distinct target variants**

Publish E view only when generation matches. Map Current to all overlapping steps without changing Manual. Both action buttons call B navigation with different variant alternatives. Remove every in-repository connection to the legacy integer signal. Do not alter submission budgets or worker count.

- [ ] **Step 4: Test and commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'decode_trace_inspector|trace_controller|trace_pipeline|selection_navigation' --output-on-failure
git add ui/qt/src/trace_inspector_binding.cpp apps/png-analyzer-gui/src/trace_controller.cpp tests/gui/trace_controller_test.cpp tests/gui/trace_pipeline_integration_test.cpp
git commit -m "feat: connect typed decode trace navigation"
```

### Task 4: Enforce responsive Trace behavior and close E

**Files:**
- Modify: `tests/gui/compression_inspector_responsive_test.cpp`
- Verify: all E files above

- [ ] **Step 1: Add exact width/state assertions**

At 600/480/360 retain Current/Step/Input/Event/Output; Event stretches and long facts stay in details. At 320 internal horizontal scroll/footer stacking does not increase minimum width. Verify Loading, Empty, Partial, Error, Current+Selection, Light/Dark palette, button labels/order/enabled states, and 55:45 split.

- [ ] **Step 2: Run focused and full gates**

```bash
cmake --build --preset dev --target pnga_analysis_engine_tests pnga_gui_decode_trace_inspector_tests pnga_gui_compression_inspector_responsive_tests pnga_gui_trace_inspector_binding_tests pnga_gui_trace_controller_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'decode_trace_inspector|compression_inspector_responsive|trace_inspector_binding|trace_controller|trace_pipeline|selection_navigation' --output-on-failure
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
git diff --check
```

- [ ] **Step 3: Audit, commit, and record handoff**

```bash
git status --short
git diff --name-status HEAD~3..HEAD
git add tests/gui/compression_inspector_responsive_test.cpp
git commit -m "test: gate decode trace states and layout"
git status --short
```

Expected: no untyped navigation, extra replay/decoder, package/parser/Statistics/Compare/APNG change, or generated tracked artifact. Report semantic goldens, mapping examples, action variants, replay counts, width/state matrix, four commit hashes, full CTest count, and clean status. Do not start F until accepted.
