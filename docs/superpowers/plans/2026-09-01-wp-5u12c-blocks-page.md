# WP-5U12C DEFLATE Blocks Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the provisional Blocks table with a model-backed product page that is available without X/Y Lock, shows the complete generation-level Fast Index, preserves Current and Manual Selection simultaneously, and emits only WP-5U12B typed navigation targets.

**Architecture:** `analysis-engine` projects complete immutable Block rows from A. `BlockInspectorModel` exposes them through `QAbstractTableModel`; `BlockInspector` owns selection/details/actions but performs no DEFLATE work. `TraceInspectorBinding` publishes Fast Index and B state, while the application routes typed actions.

**Tech Stack:** C++20, Qt 6.8+ Model/View and Test, pnga::analysis_engine Fast Compression Index, WP-5U12B CompressionSelectionStore.

**Spec:** `docs/development/wp-5u12-compression-inspector-completion.md` section WP-5U12C and `docs/development/wp-5u12-compression-inspector-flow-ui.md` sections 6, 10, 13–14, 16–17, 19–20.

## Global Constraints

- Start only from reviewed WP-5U12B; use A Fast Index and B target/state types verbatim.
- Complete Blocks remain visible without pixel lock or Deep Trace.
- Columns are exactly `Current | # | Type | Final | Input bits | Output bytes | Events | Scanlines`.
- Event/scanline cells show `—` when not proven; Qt code never replays or guesses geometry.
- Current uses icon plus accessible text; Selection uses native row/focus, and both can coexist.
- Row selection changes only Manual Selection/details and submits zero trace jobs.
- Actions are exactly `Show in Hex`, `Show inflated output`, `Open Decode Trace`; only the last may request bounded trace.
- Cross-IDAT Hex navigation preserves all IDAT data spans and excludes length/type/CRC.
- Use QAbstractTableModel/QTableView, never per-row widgets or QTableWidgetItem.
- Preserve normative geometry, 55:45 split, responsive behavior, copy, keyboard, English copy, and themes.
- Do not change Huffman/Trace facts, parser/decoder, budgets, packaging, Compare, Statistics, or APNG.

---

## File Structure

| Path | Responsibility |
|---|---|
| `libs/analysis-engine/include/pnga/analysis-engine/block_inspector.h` | Complete Block row metadata |
| `libs/analysis-engine/src/block_inspector.cpp` | Qt-free projection |
| `tests/unit/analysis-engine/block_inspector_test.cpp` | Complete/no-lock/partial/cross-IDAT tests |
| `ui/qt/include/pnga/ui/qt/block_inspector_model.h` | Columns/roles/data API |
| `ui/qt/src/block_inspector_model.cpp` | Lazy formatting and accessibility |
| `ui/qt/include/pnga/ui/qt/block_inspector.h` | Product page and typed signals |
| `ui/qt/src/block_inspector.cpp` | View/details/actions/responsive columns |
| `ui/qt/CMakeLists.txt` | Build model source |
| `ui/qt/src/trace_inspector_binding.cpp` | Publish Fast Index and selection state |
| `tests/gui/block_inspector_test.cpp` | Model/details/actions/current+selection |
| `tests/gui/compression_inspector_responsive_test.cpp` | 320/360/480/600 Blocks behavior |
| `tests/gui/trace_inspector_binding_test.cpp` | Fast Index before Deep Trace |
| `apps/png-analyzer-gui/src/trace_controller.cpp` | Route explicit Open Decode Trace |
| `tests/gui/trace_controller_test.cpp` | Explicit/incidental replay counts |
| `tests/gui/trace_pipeline_integration_test.cpp` | Blocks navigation integration |

## Required Interfaces

```cpp
struct FastCompressionBlockRow {
  std::uint64_t block_index = 0;
  pnga::deflate_index::BlockType type = pnga::deflate_index::BlockType::kStored;
  bool last = false;
  pnga::trace_model::ZlibBitRange input_range{};
  pnga::trace_model::InflatedByteRange output_range{};
  std::vector<pnga::trace_model::ProvenanceSpan> physical_spans;
  std::optional<std::uint64_t> stored_length;
  std::optional<std::uint64_t> event_count;
  std::optional<std::uint64_t> first_scanline;
  std::optional<std::uint64_t> last_scanline;
  bool operator==(const FastCompressionBlockRow&) const = default;
};
```

Populate only facts proven by Fast Index or existing generation-level mapping; otherwise keep optionals empty. `physical_spans` deliberately remains `ProvenanceSpan`: it is a backend-neutral trace-model value and preserves `bit_offset`/`bit_length` for cross-IDAT Block ranges. It is not a Qt/UI type.

```cpp
enum BlockInspectorRole {
  BlockIndexRole = Qt::UserRole + 1,
  InputRangeRole, OutputRangeRole, PhysicalSpansRole,
  ContainsCurrentRole, IsManualSelectionRole, AccessibleTextRole
};

class BlockInspectorModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column { Current = 0, Number, Type, Final, InputBits,
                OutputBytes, Events, Scanlines, ColumnCount };
  void setIndex(std::shared_ptr<const FastCompressionIndexView> index);
  void setSelectionState(const CompressionSelectionState& state);
  const FastCompressionBlockRow* rowAt(int row) const noexcept;
};
```

`BlockInspector` emits only:

```cpp
void navigationRequested(const CompressionNavigationTarget& target);
void decodeTraceRequested(std::uint64_t generation,
                          std::uint64_t block_index,
                          InflatedByteRange output_range);
```

Remove legacy `showInHexRequested(quint64, quint64)` after all callers/tests migrate.

### Task 1: Complete the Qt-free Block projection

**Files:**
- Modify: `libs/analysis-engine/include/pnga/analysis-engine/block_inspector.h`
- Modify: `libs/analysis-engine/src/block_inspector.cpp`
- Modify: `tests/unit/analysis-engine/block_inspector_test.cpp`

- [ ] **Step 1: Write failing tests**

Assert Stored length, unavailable optional metadata, every ordered cross-IDAT `ProvenanceSpan` including exact `offset`, `length`, `bit_offset`, and `bit_length`, Ready/Partial/Error retention, and complete rows without TraceQueryResult/current pixel.

- [ ] **Step 2: Confirm failure**

```bash
cmake --build --preset dev --target pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R block_inspector --output-on-failure
```

- [ ] **Step 3: Populate proven facts only**

Copy A typed ranges and every `ProvenanceSpan` without changing order or bit fields. For a Stored Block, set `stored_length` from the checked length of its proven InflatedByteRange; leave Dynamic-specific metadata empty until it exists as a structured index fact. Do not call trace query, decoder, or PNG geometry here.

- [ ] **Step 4: Test and commit**

```bash
ctest --preset dev -R 'block_inspector|trace_orchestrator' --output-on-failure
git add libs/analysis-engine/include/pnga/analysis-engine/block_inspector.h libs/analysis-engine/src/block_inspector.cpp tests/unit/analysis-engine/block_inspector_test.cpp
git commit -m "feat: complete block inspector projection"
```

### Task 2: Replace QTableWidget with BlockInspectorModel

**Files:**
- Create: `ui/qt/include/pnga/ui/qt/block_inspector_model.h`
- Create: `ui/qt/src/block_inspector_model.cpp`
- Modify: `ui/qt/include/pnga/ui/qt/block_inspector.h`
- Modify: `ui/qt/src/block_inspector.cpp`
- Modify: `ui/qt/CMakeLists.txt`
- Modify: `tests/gui/block_inspector_test.cpp`

- [ ] **Step 1: Make model/view tests fail**

Require QTableView `compressionBlocksTable`, eight exact headers/order, no descendant QTableWidget, no index widget per row, accessible text, Current+Selection coexistence, full row count, Partial retention, and Up/Down/Home/End behavior.

- [ ] **Step 2: Confirm failure**

```bash
cmake --build --preset dev --target pnga_gui_block_inspector_tests --parallel 4
```

- [ ] **Step 3: Implement model/view/details**

Format half-open ranges with domains (`zlib bits`, `inflated bytes`). Details show all spans, Stored metadata, wrapper/error/stop facts. Model roles drive Current/Selection. Use centralized palette/style roles and no RGB literal.

- [ ] **Step 4: Test and commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --preset dev -R gui_block_inspector --output-on-failure
git add ui/qt/include/pnga/ui/qt/block_inspector_model.h ui/qt/src/block_inspector_model.cpp ui/qt/include/pnga/ui/qt/block_inspector.h ui/qt/src/block_inspector.cpp ui/qt/CMakeLists.txt tests/gui/block_inspector_test.cpp
git commit -m "feat: productize deflate blocks model"
```

### Task 3: Bind typed actions and explicit trace opening

**Files:**
- Modify: `ui/qt/src/trace_inspector_binding.cpp`
- Modify: `tests/gui/trace_inspector_binding_test.cpp`
- Modify: `apps/png-analyzer-gui/src/trace_controller.cpp`
- Modify: `tests/gui/trace_controller_test.cpp`
- Modify: `tests/gui/trace_pipeline_integration_test.cpp`

- [ ] **Step 1: Add failing behavior tests**

Assert immediate no-lock Blocks, row selection produces Manual target and zero trace submissions, Hex contains every span, output carries InflatedByteRange, Open Trace submits once for selected Block, and page/resize/DEC↔HEX submit zero.

- [ ] **Step 2: Confirm failure**

```bash
cmake --build --preset dev --target pnga_gui_trace_inspector_binding_tests pnga_gui_trace_controller_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
```

- [ ] **Step 3: Wire without replay side channels**

Binding supplies one shared state. Row selection calls only B store. For `Show in Hex`, validate each span has `space == kPhysicalFile`, create a checked half-open `FileByteRange{offset, offset + length}` for every span in original order, and retain the row's precise ZlibBitRange as the target logical range; never replace the stored `ProvenanceSpan` values with byte envelopes. Show actions call B navigation. Open Trace calls the existing bounded request once and preserves generation, cancellation, 4096-token, 8-MiB, and single-worker policies.

- [ ] **Step 4: Test and commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'trace_inspector_binding|trace_controller|trace_pipeline|block_inspector' --output-on-failure
git add ui/qt/src/trace_inspector_binding.cpp tests/gui/trace_inspector_binding_test.cpp apps/png-analyzer-gui/src/trace_controller.cpp tests/gui/trace_controller_test.cpp tests/gui/trace_pipeline_integration_test.cpp
git commit -m "feat: connect typed block navigation"
```

### Task 4: Enforce responsive Blocks UI and close C

**Files:**
- Modify: `tests/gui/compression_inspector_responsive_test.cpp`
- Verify: all C files above

- [ ] **Step 1: Add exact width assertions**

600 shows all columns; 480 hides Scanlines; 360 hides Events/Scanlines and keeps Current/#/Type/Final/Input/Output; 320 permits internal horizontal scrolling without raising Inspector minimum width. Verify footer labels/order, 26–32 px rows, 26–31 px header, usable 55:45 split, and Current+Selection.

- [ ] **Step 2: Run focused and full gates**

```bash
cmake --build --preset dev --target pnga_analysis_engine_tests pnga_gui_block_inspector_tests pnga_gui_compression_inspector_responsive_tests pnga_gui_trace_inspector_binding_tests pnga_gui_trace_controller_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'block_inspector|compression_inspector_responsive|trace_inspector_binding|trace_controller|trace_pipeline' --output-on-failure
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
git diff --check
```

- [ ] **Step 3: Audit, commit, and recheck status**

```bash
git status --short
git diff --name-status HEAD~3..HEAD
git add tests/gui/compression_inspector_responsive_test.cpp
git commit -m "test: gate responsive blocks page"
git status --short
```

Expected: no Huffman/Trace fact, parser, decoder, package, Statistics, Compare, or APNG change; incidental replay counts remain zero; status is empty.

- [ ] **Step 4: Record the C handoff**

Report columns/roles, row counts, width matrix, Current/Selection semantics, typed action examples, replay counts, full CTest count, four commits, and clean status. Do not start D until review accepts it.
