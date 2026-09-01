# WP-5U12A Offset and Fast Index Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the type-safe File/zlib/DEFLATE/Inflated range contract and publish one immutable generation-level Fast Compression Index containing wrapper, all IDAT segments, Adler, completion, and every verified Block.

**Architecture:** `trace-model` owns domain-safe offsets/ranges and forbids implicit scalar/domain conversion. `deflate-index` performs one bounded sequential zlib scan using public zlib APIs and exposes already-computed wrapper, checksum, stop, and block facts. `analysis-engine` projects those facts plus VirtualIDATStream segments into immutable UI-neutral Fast Index rows; no token trace is created or retained.

**Tech Stack:** C++20, Catch2, public zlib 1.3 APIs, pnga::io, VirtualIDATStream, CMake/CTest.

**Spec:** `docs/development/wp-5u12-compression-inspector-completion.md` section WP-5U12A and `docs/development/wp-5u12-compression-inspector-flow-ui.md` sections 5, 10, 16, 19–20.

## Global Constraints

- Preserve half-open ranges and checked arithmetic.
- FileByteOffset, ZlibByteOffset/ZlibBitOffset, DeflateBitOffset, and InflatedByteOffset are never implicitly convertible or cross-comparable.
- Do not concatenate IDAT payloads; use VirtualIDATStream segment/read/mapping APIs.
- Use only public zlib headers/APIs; do not inspect private zlib state.
- Keep the existing max-index-output bound and one sequential index pass.
- Partial/error output retains every verified Block and exact stop position.
- Do not change GUI layout/copy/navigation, Deep Trace budgets, PNG parsing, filtering, reconstruction, packaging, Compare, Statistics, or APNG.
- End every task with focused tests, full CTest, `git diff --check`, allowed-path review, and a clean post-commit worktree.

---

## File Structure

| Path | Responsibility |
|---|---|
| `libs/trace-model/include/pnga/trace-model/offset_range.h` | Non-convertible typed offsets, ranges, lengths, and checked helpers |
| `tests/unit/trace-model/offset_range_test.cpp` | Compile-time domain separation and half-open/overflow tests |
| `libs/deflate-index/include/pnga/deflate-index/block_index.h` | Structured wrapper, Adler, completion/stop and Block facts |
| `libs/deflate-index/src/block_index.cpp` | Bounded extraction of those facts during the existing scan |
| `tests/unit/deflate-index/block_index_test.cpp` | Stored/Fixed/Dynamic/reserved/truncated/header/Adler facts |
| `libs/deflate-index/include/pnga/deflate-index/index_cache.h` | Bump durable schema for structured Block Index facts |
| `libs/deflate-index/src/index_cache.cpp` | Bounded serialization/deserialization of wrapper/Adler/stop facts |
| `tests/unit/deflate-index/index_cache_test.cpp` | v2 round-trip and v1 invalidation tests |
| `libs/analysis-engine/include/pnga/analysis-engine/block_inspector.h` | Fast Index immutable stream/IDAT/Block projection types |
| `libs/analysis-engine/src/block_inspector.cpp` | Typed VirtualIDATStream projection with all physical spans |
| `tests/unit/analysis-engine/block_inspector_test.cpp` | Exact Fast Index status, spans, ranges, wrapper and Adler projection |
| `libs/analysis-engine/src/trace_orchestrator.cpp` | Publish the expanded Fast Index without changing replay policy |
| `tests/unit/analysis-engine/trace_orchestrator_test.cpp` | Generation, immutable index, partial/error and zero-token-index tests |
| `docs/development/wp-5u12-compression-inspector-flow-ui.md` | Append the implemented offset-origin contract and capability matrix |

## Required Interfaces

Implement these exact public types:

```cpp
// offset_range.h
template <OffsetDomain Domain, OffsetUnit Unit>
struct Offset {
  std::uint64_t value = 0;
  constexpr explicit Offset(std::uint64_t raw) noexcept : value(raw) {}
  constexpr std::uint64_t raw_value() const noexcept { return value; }
  constexpr bool operator==(const Offset&) const noexcept = default;
  constexpr auto operator<=>(const Offset&) const noexcept = default;
};

```

The Offset default constructor remains available. Remove the implicit `operator std::uint64_t()`; callers use `raw_value()`. Keep same-domain subtraction and checked `make_range`.

```cpp
// block_index.h
enum class Adler32Status { kNotComputed = 0, kMatch = 1, kMismatch = 2 };

struct ZlibWrapperInfo {
  std::uint8_t cmf = 0;
  std::uint8_t flg = 0;
  std::uint8_t compression_method = 0;
  std::uint8_t window_bits = 0;
  bool preset_dictionary = false;
  bool header_valid = false;
  bool operator==(const ZlibWrapperInfo&) const = default;
};

struct Adler32Info {
  Adler32Status status = Adler32Status::kNotComputed;
  std::optional<std::uint32_t> expected;
  std::optional<std::uint32_t> actual;
  bool operator==(const Adler32Info&) const = default;
};

struct BlockIndexResult {
  bool success = false;
  std::string error;
  std::vector<DeflateBlock> blocks;
  std::uint64_t zlib_header_bits = 0;
  std::uint64_t total_output_bytes = 0;
  ZlibWrapperInfo wrapper;
  Adler32Info adler;
  std::optional<std::uint64_t> stop_input_bit;
  std::optional<std::uint64_t> stop_output_byte;
};
```

```cpp
// analysis-engine/block_inspector.h
struct FastCompressionIdatSpan {
  pnga::trace_model::ZlibByteRange logical_range{};
  pnga::trace_model::FileByteRange physical_range{};
  bool operator==(const FastCompressionIdatSpan&) const = default;
};

struct FastCompressionStreamSummary {
  pnga::trace_model::ZlibByteRange stream_range{};
  pnga::deflate_index::ZlibWrapperInfo wrapper;
  pnga::trace_model::ZlibByteOffset deflate_data_begin{};
  std::vector<FastCompressionIdatSpan> idat_spans;
  std::uint64_t total_output_bytes = 0;
  pnga::deflate_index::Adler32Info adler;
  std::optional<pnga::trace_model::ZlibBitOffset> stop_input;
  std::optional<pnga::trace_model::InflatedByteOffset> stop_output;
  bool operator==(const FastCompressionStreamSummary&) const = default;
};
```

`deflate_data_begin` is the byte-aligned start of the DEFLATE payload in the logical zlib stream: ordinary zlib uses `ZlibByteOffset{2}` and FDICT uses `ZlibByteOffset{6}`. Keep `FastCompressionIndexStatus::{kUnavailable,kReady,kPartial,kError}` as the completion status. `FastCompressionBlockRow::input_range` remains ZlibBitRange; token ranges remain DeflateBitRange.

The persistent index schema becomes `kIndexCacheSchemaVersion = 2` and magic suffix `2`. Version-1 files are invalid cache inputs and are rebuilt; do not add a compatibility reader that invents absent expected/actual checksum or stop facts.

### Task 1: Make offset domains non-convertible

**Files:**
- Modify: `libs/trace-model/include/pnga/trace-model/offset_range.h`
- Modify: `tests/unit/trace-model/offset_range_test.cpp`

**Interfaces:**
- Consumes: current typed offset aliases.
- Produces: `Offset::raw_value()` and compile-time prohibition of implicit scalar/domain conversion.

- [ ] **Step 1: Add failing compile-time assertions**

```cpp
static_assert(!std::is_convertible_v<ZlibBitOffset, std::uint64_t>);
static_assert(!std::is_convertible_v<DeflateBitOffset, std::uint64_t>);
static_assert(!std::is_convertible_v<ZlibBitOffset, DeflateBitOffset>);
static_assert(!std::is_constructible_v<ZlibBitRange,
                                       DeflateBitOffset, DeflateBitOffset>);
TEST_CASE("typed offset exposes raw value explicitly") {
  REQUIRE(ZlibBitOffset{16}.raw_value() == 16);
}
```

- [ ] **Step 2: Run the test and confirm failure**

```bash
cmake --build --preset dev --target pnga_trace_model_tests --parallel 4
ctest --preset dev -R offset_range --output-on-failure
```

Expected: compile fails because offsets are implicitly convertible and `raw_value` is absent.

- [ ] **Step 3: Remove implicit conversion and verify the call surface**

Implement the Required Interface. First run `grep -R -n -E 'operator std::uint64_t|static_cast<std::uint64_t>\(' libs ui apps` and verify no alternate implicit conversion is introduced. The current audited call surface uses typed comparison or the public `value` member, so no production caller file is authorized in this task. If compilation names a production dependency, stop and amend/review this plan before editing that file; never add a cross-domain overload or generic scalar normalization helper.

- [ ] **Step 4: Run trace-model and compilation regression**

```bash
cmake --build --preset dev --parallel 4
ctest --preset dev -R 'offset_range|trace_model_selection' --output-on-failure
```

Expected: build and matching tests pass.

- [ ] **Step 5: Commit**

```bash
git add libs/trace-model/include/pnga/trace-model/offset_range.h tests/unit/trace-model/offset_range_test.cpp
git commit -m "refactor: enforce compression offset domains"
```

### Task 2: Expose wrapper, Adler and exact stop facts from the bounded index

**Files:**
- Modify: `libs/deflate-index/include/pnga/deflate-index/block_index.h`
- Modify: `libs/deflate-index/src/block_index.cpp`
- Modify: `tests/unit/deflate-index/block_index_test.cpp`
- Modify: `libs/deflate-index/include/pnga/deflate-index/index_cache.h`
- Modify: `libs/deflate-index/src/index_cache.cpp`
- Modify: `tests/unit/deflate-index/index_cache_test.cpp`

**Interfaces:**
- Consumes: public zlib stream header/trailer and existing sequential inflate output.
- Produces: `ZlibWrapperInfo`, `Adler32Info`, `stop_input_bit`, `stop_output_byte`.

- [ ] **Step 1: Add failing structured-fact tests**

Add assertions to existing controlled fixtures:

```cpp
REQUIRE(index.wrapper.cmf == 0x78);
REQUIRE(index.wrapper.compression_method == 8);
REQUIRE(index.wrapper.window_bits == 15);
REQUIRE(index.wrapper.header_valid);
REQUIRE_FALSE(index.wrapper.preset_dictionary);
REQUIRE(index.adler.status == Adler32Status::kMatch);
REQUIRE(index.adler.expected == index.adler.actual);
REQUIRE_FALSE(index.stop_input_bit.has_value());
```

For corrupted Adler assert `kMismatch` and both expected/actual values differ. For truncated input assert `kNotComputed`, expected/actual are empty, verified blocks remain, and both stop offsets equal the final verified boundary. For reserved BTYPE assert stable error `reserved deflate block type` plus exact stop bit. Add a cache round-trip containing every new field and a handcrafted v1-magic cache that returns Invalid without publication.

- [ ] **Step 2: Run and verify test failure**

```bash
cmake --build --preset dev --target pnga_deflate_index_tests --parallel 4
ctest --preset dev -R deflate_index --output-on-failure
```

Expected: compile fails because structured fields are absent.

- [ ] **Step 3: Implement extraction during the existing pass**

Read CMF/FLG with `IByteSource::read(0, ...)` before inflate; validate CM=8, CINFO<=7, FCHECK modulo 31 and FDICT. Update actual Adler using public `adler32` over each produced scratch slice. Read the four-byte big-endian expected trailer only when a complete trailer is available. Assign stop offsets from the latest verified boundary before every error return. Serialize every structured fact with bounded reads, bump cache magic/schema to 2, and reject v1. Do not add a second inflate pass.

- [ ] **Step 4: Run exact index tests**

```bash
cmake --build --preset dev --target pnga_deflate_index_tests --parallel 4
ctest --preset dev -R deflate_index --output-on-failure
```

Expected: Stored, Fixed, Dynamic, multi-block, bad-Adler, truncated, reserved, output-cap and mapping tests pass.

- [ ] **Step 5: Commit**

```bash
git add libs/deflate-index/include/pnga/deflate-index/block_index.h libs/deflate-index/src/block_index.cpp tests/unit/deflate-index/block_index_test.cpp libs/deflate-index/include/pnga/deflate-index/index_cache.h libs/deflate-index/src/index_cache.cpp tests/unit/deflate-index/index_cache_test.cpp
git commit -m "feat: expose zlib index completion facts"
```

### Task 3: Project the complete generation-level Fast Index

**Files:**
- Modify: `libs/analysis-engine/include/pnga/analysis-engine/block_inspector.h`
- Modify: `libs/analysis-engine/src/block_inspector.cpp`
- Modify: `tests/unit/analysis-engine/block_inspector_test.cpp`
- Modify: `libs/analysis-engine/src/trace_orchestrator.cpp`
- Modify: `tests/unit/analysis-engine/trace_orchestrator_test.cpp`

**Interfaces:**
- Consumes: Task 2 BlockIndexResult and VirtualIDATStream::segment.
- Produces: expanded `FastCompressionStreamSummary` and unchanged complete Block list.

- [ ] **Step 1: Add failing one/many-IDAT and status tests**

Construct one- and two-segment ChunkIndex values and assert exact typed logical/physical ranges, wrapper values, byte-aligned origins `ZlibByteOffset{2}` and `ZlibByteOffset{6}`, expected/actual Adler, Ready/Partial/Error status, stop offsets, and all complete/verified blocks.

- [ ] **Step 2: Run and verify failure**

```bash
cmake --build --preset dev --target pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R 'block_inspector|trace_orchestrator' --output-on-failure
```

Expected: compile/assertion failure for missing summary fields.

- [ ] **Step 3: Implement the immutable projection**

Iterate `stream.segment_count()` in segment order. Convert each segment to `ZlibByteRange{logical_start, logical_start+length}` and `FileByteRange{physical_offset, physical_offset+length}` using checked `make_range`. Convert `zlib_header_bits` to `ZlibByteOffset` only when it is divisible by 8; otherwise return Error `DEFLATE payload origin is not byte-aligned`. Preserve every block physical bit span. Return Error for mapping overflow, Partial when verified blocks exist after an index failure, Ready only on complete index success.

- [ ] **Step 4: Verify no Deep Trace is created**

Extend TraceOrchestrator tests to call `open` and `fast_index` without `submit`; assert complete Blocks and `queued_tasks()==0`. Run:

```bash
ctest --preset dev -R 'block_inspector|trace_orchestrator|trace_query' --output-on-failure
```

Expected: all matching tests pass with zero submitted trace jobs.

- [ ] **Step 5: Commit**

```bash
git add libs/analysis-engine/include/pnga/analysis-engine/block_inspector.h libs/analysis-engine/src/block_inspector.cpp libs/analysis-engine/src/trace_orchestrator.cpp tests/unit/analysis-engine/block_inspector_test.cpp tests/unit/analysis-engine/trace_orchestrator_test.cpp
git commit -m "feat: complete fast compression index"
```

### Task 4: Document the offset contract and close A without side effects

**Files:**
- Modify: `docs/development/wp-5u12-compression-inspector-flow-ui.md`
- Verify: all A files above

**Interfaces:**
- Consumes: final implemented signatures.
- Produces: authoritative origin/conversion table for B–F agents.

- [ ] **Step 1: Append the implemented domain table**

Document File bytes, zlib bytes/bits, DEFLATE payload bits, and Inflated bytes; name each C++ type, origin, unit, allowed conversion function, and UI label. State that `DeflateBitOffset{0}` normalizes to `ZlibBitOffset{deflate_data_begin.raw_value() * 8}` only after checked multiplication in analysis-engine; direct comparison or unchecked multiplication is forbidden.

- [ ] **Step 2: Run the focused and full gates**

```bash
cmake --build --preset dev --parallel 4
ctest --preset dev -R 'offset_range|deflate_index|block_inspector|trace_orchestrator|trace_query' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
git diff --check
git status --short
```

Expected: 100% pass; status contains only this task’s documented files before commit.

- [ ] **Step 3: Audit side effects**

```bash
git diff --name-status HEAD~3..HEAD
grep -R -n -E 'max_tokens|trace_output_budget_bytes|worker_count' apps/png-analyzer-gui/src/trace_controller.cpp libs/analysis-engine
```

Expected: no UI/selection/parser/packaging file changed; trace budgets remain 4096 tokens, 8 MiB output, one GUI trace worker.

- [ ] **Step 4: Commit documentation**

```bash
git add docs/development/wp-5u12-compression-inspector-flow-ui.md
git commit -m "docs: freeze compression offset contract"
```

- [ ] **Step 5: Produce the A handoff record**

Report exact final types/statuses, focused/full test counts, changed paths, commit hashes, and `git status --short` output. Do not start B until review accepts the record.
