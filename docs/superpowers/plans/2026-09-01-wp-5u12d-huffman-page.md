# WP-5U12D Huffman Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a model-backed Huffman construction page for Stored, Fixed, and Dynamic Blocks with correct canonical/read-order separation, bounded-use counts, Current/Selection coexistence, and typed occurrence navigation.

**Architecture:** `deflate-trace` exposes the Huffman symbol already consumed by each bounded token. `analysis-engine` produces display-ready meaning and fixed-width bit strings plus occurrence token ids. `HuffmanInspectorModel` filters zero-bit rows by default and the Qt page only renders/selects immutable facts.

**Tech Stack:** C++20, bounded DEFLATE trace, Qt 6.8+ QAbstractTableModel/QTableView, Catch2, Qt Test, WP-5U12B navigation.

**Spec:** `docs/development/wp-5u12-compression-inspector-completion.md` section WP-5U12D and `docs/development/wp-5u12-compression-inspector-flow-ui.md` sections 7, 10, 13–14, 16–17, 19–20.

## Global Constraints

- Start only from reviewed WP-5U12C and retain its selected Block identity.
- Stored explicitly states no Huffman; Fixed identifies predefined tables; Dynamic orders Code Length, Literal / Length, Distance construction.
- Main columns are exactly `Symbol | Meaning | Bits | Canonical | Read order | Uses in result`.
- Canonical/read-order are separate, fixed-width binary strings from Qt-free code; Qt never reverses bits.
- `Uses in result` counts only tokens in the bounded result and labels that scope; never scan the whole stream.
- Entries with zero bit length are hidden by default, remain available to tests/details, and are not destroyed.
- Partial/Error retains verified tables/entries/occurrences and states the stop reason.
- Selecting a symbol changes Manual Selection only. Opening an occurrence navigates to the existing bounded token and triggers no replay.
- Use model/view with no QTableWidget, per-row widget, debug-string parsing, alternate integer navigation, or unbounded occurrence index.
- Preserve normative geometry, columns, responsive behavior, copy, keyboard, accessibility, themes, and locked English labels.
- Do not modify Blocks semantics, decoder budgets, parser, packaging, Compare, Statistics, or APNG.

---

## File Structure

| Path | Responsibility |
|---|---|
| `libs/deflate-trace/include/pnga/deflate-trace/token_decoder.h` | Per-token consumed Huffman symbol |
| `libs/deflate-trace/src/token_decoder.cpp` | Capture symbol during existing bounded decode |
| `tests/unit/deflate-trace/token_decoder_test.cpp` | Literal/length/EOB symbol golden checks |
| `libs/analysis-engine/include/pnga/analysis-engine/trace_query.h` | Preserve optional symbol in bounded token summary |
| `libs/analysis-engine/src/trace_query.cpp` | Project/serialize symbol |
| `tests/unit/analysis-engine/trace_query_test.cpp` | Exact bounded symbol preservation |
| `libs/analysis-engine/include/pnga/analysis-engine/huffman_inspector.h` | Product entry/table projection types |
| `libs/analysis-engine/src/huffman_inspector.cpp` | Meaning, bit strings, bounded occurrences |
| `tests/unit/analysis-engine/huffman_inspector_test.cpp` | Stored/Fixed/Dynamic/partial golden tests |
| `ui/qt/include/pnga/ui/qt/huffman_inspector_model.h` | Columns/roles/filter API |
| `ui/qt/src/huffman_inspector_model.cpp` | Lazy model formatting |
| `ui/qt/include/pnga/ui/qt/huffman_inspector.h` | Page selectors/details/typed signal |
| `ui/qt/src/huffman_inspector.cpp` | Model/view and occurrence selection |
| `ui/qt/CMakeLists.txt` | Build model source |
| `tests/gui/huffman_inspector_test.cpp` | UI/model/state/occurrence tests |
| `tests/gui/compression_inspector_responsive_test.cpp` | Huffman width behavior |
| `ui/qt/src/trace_inspector_binding.cpp` | Keep selected Block/state across pages |
| `tests/gui/trace_pipeline_integration_test.cpp` | Blocks→Huffman→occurrence integration |

## Required Interfaces

Add to `TokenEvent` and `TraceTokenSummary`:

```cpp
std::optional<std::uint16_t> huffman_symbol;
```

Stored literals have no symbol. Huffman Literal has its byte symbol, Match has its literal/length symbol 257–285, and EndOfBlock has 256. Capture it at decode time; never reconstruct it from display text.

```cpp
struct HuffmanInspectorEntry {
  std::uint16_t symbol = 0;
  std::string meaning;
  std::uint8_t bit_length = 0;
  std::uint16_t canonical_code = 0;
  std::uint16_t read_order_code = 0;
  std::string canonical_bits;
  std::string read_order_bits;
  DeflateBitRange provenance_range{};
  std::vector<std::uint64_t> occurrence_token_indices;
  bool selected = false;
  bool operator==(const HuffmanInspectorEntry&) const = default;
};

struct HuffmanInspectorTable {
  std::uint64_t block_index = 0;
  HuffmanTableMode mode = HuffmanTableMode::kDynamic;
  std::optional<HuffmanTableKind> kind;
  std::string selector_label;
  std::uint32_t build_order = 0;
  std::uint64_t declared_entry_count = 0;
  std::uint64_t bounded_token_count = 0;
  bool truncated = false;
  std::vector<HuffmanInspectorEntry> entries;
};
```

Binary strings contain exactly `bit_length` characters. Canonical prints most-significant canonical bit first; Read order prints the transmitted least-significant-first sequence. `Uses in result == occurrence_token_indices.size()`.

```cpp
class HuffmanInspectorModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column { Symbol = 0, Meaning, Bits, Canonical,
                ReadOrder, UsesInResult, ColumnCount };
  void setTable(std::shared_ptr<const HuffmanInspectorTable> table);
  void setHideZeroBitEntries(bool hide);
  void setSelectionState(const CompressionSelectionState& state);
  const HuffmanInspectorEntry* entryAt(int visible_row) const noexcept;
};
```

### Task 1: Preserve the consumed symbol in bounded trace facts

**Files:**
- Modify: `libs/deflate-trace/include/pnga/deflate-trace/token_decoder.h`
- Modify: `libs/deflate-trace/src/token_decoder.cpp`
- Modify: `tests/unit/deflate-trace/token_decoder_test.cpp`
- Modify: `libs/analysis-engine/include/pnga/analysis-engine/trace_query.h`
- Modify: `libs/analysis-engine/src/trace_query.cpp`
- Modify: `tests/unit/analysis-engine/trace_query_test.cpp`

- [ ] **Step 1: Add failing golden assertions**

For fixed/dynamic fixtures assert literal symbol, length symbol, EOB 256, Stored null, exact serialization, and preservation under Partial truncation.

- [ ] **Step 2: Confirm failure**

```bash
cmake --build --preset dev --target pnga_deflate_trace_tests pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R 'deflate_trace|trace_query' --output-on-failure
```

- [ ] **Step 3: Capture during existing decode and project**

Assign the symbol at the same read that resolves the token. Do not add a pass, decoder, output copy, or token retention beyond the existing bound. Update stable serialization field order and golden expectation.

- [ ] **Step 4: Test and commit**

```bash
ctest --preset dev -R 'deflate_trace|trace_query' --output-on-failure
git add libs/deflate-trace/include/pnga/deflate-trace/token_decoder.h libs/deflate-trace/src/token_decoder.cpp tests/unit/deflate-trace/token_decoder_test.cpp libs/analysis-engine/include/pnga/analysis-engine/trace_query.h libs/analysis-engine/src/trace_query.cpp tests/unit/analysis-engine/trace_query_test.cpp
git commit -m "feat: preserve bounded huffman symbols"
```

### Task 2: Produce complete Huffman projection facts

**Files:**
- Modify: `libs/analysis-engine/include/pnga/analysis-engine/huffman_inspector.h`
- Modify: `libs/analysis-engine/src/huffman_inspector.cpp`
- Modify: `tests/unit/analysis-engine/huffman_inspector_test.cpp`

- [ ] **Step 1: Add failing projection tests**

Cover Stored LEN/NLEN/no-Huffman state; Fixed predefined Literal/Length and Distance; Dynamic Code Length→Literal/Length→Distance order; exact canonical/read-order strings including leading zeroes; zero-bit entries; bounded uses/occurrence ids; selected token; and Partial/Error retention.

- [ ] **Step 2: Confirm failure**

```bash
cmake --build --preset dev --target pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R huffman_inspector --output-on-failure
```

- [ ] **Step 3: Implement Qt-free formatting/counting**

Use structured table entries and bounded token symbols. Produce stable English meanings for literal byte, EOB, length range, distance range, and code-length symbol. Count only tokens belonging to the selected Block and current TraceQueryResult.

- [ ] **Step 4: Test and commit**

```bash
ctest --preset dev -R 'huffman_inspector|trace_query' --output-on-failure
git add libs/analysis-engine/include/pnga/analysis-engine/huffman_inspector.h libs/analysis-engine/src/huffman_inspector.cpp tests/unit/analysis-engine/huffman_inspector_test.cpp
git commit -m "feat: complete huffman inspector projection"
```

### Task 3: Replace QTableWidget with HuffmanInspectorModel

**Files:**
- Create: `ui/qt/include/pnga/ui/qt/huffman_inspector_model.h`
- Create: `ui/qt/src/huffman_inspector_model.cpp`
- Modify: `ui/qt/include/pnga/ui/qt/huffman_inspector.h`
- Modify: `ui/qt/src/huffman_inspector.cpp`
- Modify: `ui/qt/CMakeLists.txt`
- Modify: `tests/gui/huffman_inspector_test.cpp`

- [ ] **Step 1: Rewrite tests to fail against old widget**

Require QTableView `compressionHuffmanTable`, six exact headers, no QTableWidget/index widgets, correct selector order/labels, zero-bit hidden default, scope tooltip/accessibility, details containing both bit orders, Stored empty state, Partial retention, Current+Selection, copy, and keyboard navigation.

- [ ] **Step 2: Build and confirm failure**

```bash
cmake --build --preset dev --target pnga_gui_huffman_inspector_tests --parallel 4
```

- [ ] **Step 3: Implement product page and typed occurrence target**

Model renders projection strings without reversal. Selecting a row sets B Manual Selection with Block/symbol. Opening first/next occurrence uses `occurrence_token_indices` and emits a B target containing selected token, DeflateBitRange, and mapped physical spans already present in the bounded result. No occurrence means exact text `This symbol is defined but not used by Block #n.`

- [ ] **Step 4: Test and commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --preset dev -R gui_huffman_inspector --output-on-failure
git add ui/qt/include/pnga/ui/qt/huffman_inspector_model.h ui/qt/src/huffman_inspector_model.cpp ui/qt/include/pnga/ui/qt/huffman_inspector.h ui/qt/src/huffman_inspector.cpp ui/qt/CMakeLists.txt tests/gui/huffman_inspector_test.cpp
git commit -m "feat: productize huffman inspector model"
```

### Task 4: Bind Block/table/occurrence state and close D

**Files:**
- Modify: `ui/qt/src/trace_inspector_binding.cpp`
- Modify: `tests/gui/compression_inspector_responsive_test.cpp`
- Modify: `tests/gui/trace_pipeline_integration_test.cpp`
- Verify: all D files above

- [ ] **Step 1: Add failing integration/width tests**

Blocks→Huffman keeps Block; table changes keep Block; first/next occurrence targets correct tokens and Back returns symbol; returning to Huffman preserves Block/table/symbol/occurrence. At 600/480 retain page semantics; at 360 keep Symbol/Meaning/Bits/Read order visible and allow Canonical/Uses horizontal scroll; 320 does not raise minimum width.

- [ ] **Step 2: Run focused and full gates**

```bash
cmake --build --preset dev --target pnga_deflate_trace_tests pnga_analysis_engine_tests pnga_gui_huffman_inspector_tests pnga_gui_compression_inspector_responsive_tests pnga_gui_trace_inspector_binding_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'deflate_trace|trace_query|huffman_inspector|compression_inspector_responsive|trace_inspector_binding|trace_pipeline' --output-on-failure
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
git add ui/qt/src/trace_inspector_binding.cpp tests/gui/compression_inspector_responsive_test.cpp tests/gui/trace_pipeline_integration_test.cpp
git commit -m "test: gate huffman navigation and layout"
git status --short
```

Expected: Blocks and Decode Trace tests remain green; no whole-stream occurrence vector, parser/package/Statistics/Compare/APNG change, or incidental replay exists. Report projection fields, bit-string goldens, bounded counts, width matrix, replay counts, four commit hashes, full CTest count, and clean status. Do not start E until accepted.
