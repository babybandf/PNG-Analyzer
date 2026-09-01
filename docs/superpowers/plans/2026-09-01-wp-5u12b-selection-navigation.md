# WP-5U12B Selection and Navigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce one generation-safe, typed Compression navigation contract in which immutable Current mapping and user-owned Manual Selection coexist, multi-IDAT physical spans survive round trips, and programmatic navigation cannot form signal loops.

**Architecture:** `trace-model` owns Qt-free source-unit, target, and selection-state value types. `ui/qt` owns a small state store and history. The application `SelectionNavigationController` resolves typed targets to Hex/Inflated views and enforces generation/request-serial gates without submitting Deep Trace work.

**Tech Stack:** C++20, `std::variant`, Qt 6.8+ signals, Catch2, Qt Test, existing SelectionBus and SelectionViewState.

**Spec:** `docs/development/wp-5u12-compression-inspector-completion.md` section WP-5U12B and `docs/development/wp-5u12-compression-inspector-flow-ui.md` sections 10, 11, 14, 17, 19–20.

## Global Constraints

- Start only from reviewed WP-5U12A and consume its typed ranges verbatim.
- Table selection never rewrites Current mapping; Current updates never clear same-generation Manual Selection.
- Navigation carries one typed logical range and every mapped File span; no first-span-only path is allowed.
- Reject stale generation before UI publication; suppress a serial already applied by the receiver.
- `DocumentSourceUnit` supports File and future animation-frame identity without adding APNG UI/parsing.
- `Show in Hex` accepts File/Zlib/DEFLATE input; `Show inflated output` accepts only InflatedByteRange.
- Page switch, row selection, history movement, numeric-base switch, resize, and programmatic focus enqueue zero Deep Trace jobs.
- Do not change Compression layout, parser/Inflate behavior, trace budgets, packaging, Compare, Statistics, or APNG.
- End each task with focused tests, full CTest, exact changed-path review, and a clean post-commit worktree.

---

## File Structure

| Path | Responsibility |
|---|---|
| `libs/trace-model/include/pnga/trace-model/compression_navigation.h` | Typed source unit, target, Current, Manual Selection, state |
| `libs/trace-model/src/compression_navigation.cpp` | Value validation rules |
| `libs/trace-model/CMakeLists.txt` | Build the value implementation |
| `tests/unit/trace-model/compression_navigation_test.cpp` | Domain/generation/coexistence tests |
| `tests/unit/trace-model/CMakeLists.txt` | Compile the focused test |
| `ui/qt/include/pnga/ui/qt/compression_selection_store.h` | QObject state/history owner |
| `ui/qt/src/compression_selection_store.cpp` | Current/manual transitions and loop suppression |
| `ui/qt/CMakeLists.txt` | Build the store |
| `tests/gui/compression_selection_store_test.cpp` | Signal/history tests |
| `tests/gui/CMakeLists.txt` | Add its GUI test target |
| `apps/png-analyzer-gui/src/selection_navigation_controller.h` | Typed Compression entry points |
| `apps/png-analyzer-gui/src/selection_navigation_controller.cpp` | Generation gate and Hex/Inflated routing |
| `apps/png-analyzer-gui/src/main_window.h` | Own the shared store |
| `apps/png-analyzer-gui/src/main_window.cpp` | Inject one shared store |
| `tests/gui/selection_navigation_controller_test.cpp` | Routing/zero-replay tests |
| `tests/gui/trace_pipeline_integration_test.cpp` | Multi-span/stale/coexistence integration |

## Required Interfaces

```cpp
enum class DocumentSourceUnitKind { kFile = 0, kAnimationFrame = 1 };

struct DocumentSourceUnit {
  DocumentSourceUnitKind kind = DocumentSourceUnitKind::kFile;
  std::uint32_t index = 0;
  bool valid() const noexcept; // File requires index 0.
  bool operator==(const DocumentSourceUnit&) const = default;
};

using CompressionLogicalRange = std::variant<
    FileByteRange, ZlibByteRange, ZlibBitRange,
    DeflateBitRange, InflatedByteRange>;

enum class CompressionNavigationOrigin {
  kBlocks = 0, kHuffman = 1, kDecodeTrace = 2,
  kHex = 3, kInflated = 4
};

struct CompressionNavigationTarget {
  std::uint64_t generation = 0;
  std::uint64_t request_serial = 0;
  DocumentSourceUnit source_unit{};
  CompressionNavigationOrigin origin = CompressionNavigationOrigin::kBlocks;
  CompressionLogicalRange logical_range = FileByteRange{};
  std::vector<FileByteRange> physical_spans;
  std::optional<std::uint64_t> block_index;
  std::optional<std::uint64_t> token_index;
  std::optional<std::uint16_t> symbol;
  bool valid() const noexcept;
  bool operator==(const CompressionNavigationTarget&) const = default;
};

struct CompressionCurrentMapping {
  std::uint64_t generation = 0;
  DocumentSourceUnit source_unit{};
  InflatedByteRange output_range{};
  std::optional<std::uint64_t> block_index;
  std::optional<std::uint64_t> token_index;
  bool operator==(const CompressionCurrentMapping&) const = default;
};

struct CompressionSelectionState {
  std::uint64_t generation = 0;
  std::optional<CompressionCurrentMapping> current;
  std::optional<CompressionNavigationTarget> manual;
  bool operator==(const CompressionSelectionState&) const = default;
};
```

Every range is non-empty. File source uses index 0. Emitted serials are non-zero. Non-File compressed ranges require at least one non-empty physical span; Inflated ranges require none. File spans remain ordered and non-overlapping; do not sort, merge, or truncate them.

```cpp
class CompressionSelectionStore final : public QObject {
  Q_OBJECT
 public:
  explicit CompressionSelectionStore(QObject* parent = nullptr);
  const CompressionSelectionState& state() const noexcept;
  const std::vector<CompressionNavigationTarget>& history() const noexcept;
  std::optional<std::size_t> historyIndex() const noexcept;
  void resetGeneration(std::uint64_t generation);
  bool setCurrent(const CompressionCurrentMapping& current);
  bool setManual(const CompressionNavigationTarget& target);
  bool applyNavigation(const CompressionNavigationTarget& target);
  bool goBack();
  bool goForward();
 signals:
  void stateChanged(const CompressionSelectionState& state);
  void navigationRequested(const CompressionNavigationTarget& target);
};
```

`applyNavigation` rejects invalid/stale/duplicate serials, sets only Manual Selection, appends one history item, discards only the forward branch after Back, and emits once. `setCurrent` changes only Current. `resetGeneration` clears all old-generation state.

### Task 1: Add the Qt-free navigation values

**Files:**
- Create: `libs/trace-model/include/pnga/trace-model/compression_navigation.h`
- Create: `libs/trace-model/src/compression_navigation.cpp`
- Modify: `libs/trace-model/CMakeLists.txt`
- Create: `tests/unit/trace-model/compression_navigation_test.cpp`
- Modify: `tests/unit/trace-model/CMakeLists.txt`

- [ ] **Step 1: Write failing contract tests**

Cover File/Frame units, all five logical variants, ordered two-span cross-IDAT values, empty/overlap rejection, zero serial rejection, Current+Manual equality, and generation 7→8 reset values.

- [ ] **Step 2: Confirm the expected compile failure**

```bash
cmake --build --preset dev --target pnga_trace_model_tests --parallel 4
```

Expected: `compression_navigation.h` is missing.

- [ ] **Step 3: Implement the exact values**

Use `std::visit` over typed `begin/end`; never normalize the variant to an untyped scalar pair. Validate spans in caller order. Keep trace-model Qt-free.

- [ ] **Step 4: Test and commit**

```bash
ctest --preset dev -R trace_model --output-on-failure
git add libs/trace-model/include/pnga/trace-model/compression_navigation.h libs/trace-model/src/compression_navigation.cpp libs/trace-model/CMakeLists.txt tests/unit/trace-model/compression_navigation_test.cpp tests/unit/trace-model/CMakeLists.txt
git commit -m "feat: add typed compression navigation values"
```

### Task 2: Implement Current/Manual state and history

**Files:**
- Create: `ui/qt/include/pnga/ui/qt/compression_selection_store.h`
- Create: `ui/qt/src/compression_selection_store.cpp`
- Modify: `ui/qt/CMakeLists.txt`
- Create: `tests/gui/compression_selection_store_test.cpp`
- Modify: `tests/gui/CMakeLists.txt`

- [ ] **Step 1: Write failing transition tests**

Assert Current→Manual preserves both; later Current preserves Manual; later Manual preserves Current; duplicate/stale request emits nothing; A→B→Back→C produces A,C; Back/Forward emits once without history duplication; reset clears state/history and emits once.

- [ ] **Step 2: Add `pnga_gui_compression_selection_store_tests` and confirm failure**

Link `pnga::ui_qt`, `pnga::trace_model`, and `Qt6::Test`.

```bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_compression_selection_store_tests --parallel 4
```

- [ ] **Step 3: Implement state and signal guards**

Maintain `last_applied_serial_`, history vector/index, and one state. The store never calls SelectionBus or trace submission. Register QSignalSpy value types with `Q_DECLARE_METATYPE`/`qRegisterMetaType`.

- [ ] **Step 4: Test and commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'compression_selection_store|selection_bus|selection_view_state' --output-on-failure
git add ui/qt/include/pnga/ui/qt/compression_selection_store.h ui/qt/src/compression_selection_store.cpp ui/qt/CMakeLists.txt tests/gui/compression_selection_store_test.cpp tests/gui/CMakeLists.txt
git commit -m "feat: separate compression current and selection"
```

### Task 3: Route typed targets through the application

**Files:**
- Modify: `apps/png-analyzer-gui/src/selection_navigation_controller.h`
- Modify: `apps/png-analyzer-gui/src/selection_navigation_controller.cpp`
- Modify: `apps/png-analyzer-gui/src/main_window.h`
- Modify: `apps/png-analyzer-gui/src/main_window.cpp`
- Modify: `tests/gui/selection_navigation_controller_test.cpp`
- Modify: `tests/gui/trace_pipeline_integration_test.cpp`

- [ ] **Step 1: Add failing routing tests**

With a two-IDAT fixture, assert Zlib/DEFLATE targets highlight both exact IDAT data spans, File selects its exact range, Inflated selects only the Inflated source, generation N is ignored after N+1, one request gives one view update, and the trace submission counter is unchanged.

- [ ] **Step 2: Confirm interface failure**

```bash
cmake --build --preset dev --target pnga_gui_selection_navigation_controller_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
```

- [ ] **Step 3: Add exact entry points and shared ownership**

Add `applyCompressionNavigation(const CompressionNavigationTarget&)` and `setCompressionCurrent(const CompressionCurrentMapping&)`. Check generation first, route by variant, use all physical spans, and route Inflated range through the existing Inflated source/highlight. MainWindow owns exactly one store.

- [ ] **Step 4: Test and commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'compression_selection_store|selection_navigation|trace_pipeline' --output-on-failure
git add apps/png-analyzer-gui/src/selection_navigation_controller.h apps/png-analyzer-gui/src/selection_navigation_controller.cpp apps/png-analyzer-gui/src/main_window.h apps/png-analyzer-gui/src/main_window.cpp tests/gui/selection_navigation_controller_test.cpp tests/gui/trace_pipeline_integration_test.cpp
git commit -m "feat: route typed compression navigation"
```

### Task 4: Close WP-5U12B without side effects

**Files:** Verify every B file above.

- [ ] **Step 1: Run focused and full gates**

```bash
cmake --build --preset dev --target pnga_trace_model_tests pnga_gui_compression_selection_store_tests pnga_gui_selection_navigation_controller_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'trace_model|compression_selection_store|selection_bus|selection_navigation|trace_pipeline' --output-on-failure
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
git diff --check
```

- [ ] **Step 2: Audit paths and behavior**

```bash
git status --short
git diff --name-status HEAD~3..HEAD
git diff HEAD~3..HEAD -- apps/png-analyzer-gui/src/trace_controller.cpp
```

Expected: only B files changed; TraceController diff is empty; no parser, decoder, layout, table, budget, package, Statistics, Compare, or APNG change exists.

- [ ] **Step 3: Record the B handoff**

Report exact signatures, CTest count, multi-span result, before/after trace submission counts, three commit hashes, and empty status. Do not start C until review accepts it.
