# WP-602B–H Statistics UI, CLI and Deterministic Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Deliver lazy, cancelable whole-document Statistics in the GUI and CLI with one versioned Qt-free snapshot and byte-identical deterministic JSON/CSV output.

**Architecture:** Extend the existing pnga_statistics library into a per-section accumulator and serializer, then add a scalar observer mode to the owned DEFLATE decoder so whole-document token facts are aggregated without retaining an event list. A Qt-free analysis-engine collector builds immutable snapshots and navigation rows; the CLI calls it synchronously, while a low-priority dedicated GUI worker and StatisticsController run it lazily and generation-gate publication.

**Tech Stack:** C++20, CMake/CTest, Catch2, Qt 6.8+ Widgets/Test, existing zlib-backed DEFLATE modules, WP-607C generated fixtures.

**Spec:** docs/development/wp-602b-statistics-ui-export-reentry.md and binding rulings R1–R13 in docs/development/wp-602b-h-written-package-review.md.

## Global Constraints

- Keep every path under libs Qt-free; Qt code stays under ui/qt, apps/png-analyzer-gui and GUI tests.
- Add no dependency, package manager, top-level directory, Compare field or APNG field.
- Preserve VirtualIDATStream; never concatenate the complete IDAT payload.
- Whole-document token collection retains only the 32 KiB DEFLATE window, fixed decoder workspace and bounded histogram buckets, never a full TokenEvent/output/table list.
- Fixed section order is overview, chunks, filters, blocks, tokens, lengths, distances.
- Fixed status vocabulary is unavailable, ready, partial, cancelled, budget_exceeded, invalid_input, overflow, error.
- Fixed scope vocabulary is none, whole_document, verified_prefix.
- Fingerprint is fnv1a64-v1:<16 lowercase hex>, computed in 64 KiB windows, used only for result correlation.
- Default sample/bucket limits remain WP-602A values; declared background working memory is capped at 64 MiB.
- Occurrence queries are bounded to 4,096 tokens and 8 MiB input.
- JSON/CSV are UTF-8 without BOM, LF only, locale/clock/path independent and end in exactly one LF.
- CLI syntax and exits are fixed: pnga statistics <file> --format json|csv; 0 ready, 1 I/O, 2 arguments/format, 3 validation issues with usable statistics, 4 partial/cancel/budget.
- Collection begins only on first Statistics-tab activation, Refresh or Export; file-open and hover paths stay unchanged.
- Execute tasks serially. Each task begins RED, passes its focused gate and ends in an independently reviewable commit.

## File Structure

| Path | Responsibility |
|---|---|
| libs/statistics/include/pnga/statistics/statistics.h | Per-section snapshot, bounded samples and accumulator API |
| libs/statistics/src/statistics.cpp | Checked aggregation and stable section-state transitions |
| libs/statistics/include/pnga/statistics/serialization.h | DocumentIdentity and JSON/CSV result API |
| libs/statistics/src/serialization.cpp | Sole deterministic JSON/CSV implementation |
| libs/deflate-trace/include/pnga/deflate-trace/token_decoder.h | Backward-compatible scalar Token scan contract |
| libs/deflate-trace/src/token_decoder.cpp | Shared streaming decoder engine and rich/scalar sinks |
| libs/analysis-engine/include/pnga/analysis-engine/document_fingerprint.h | Cancelable source fingerprint API |
| libs/analysis-engine/src/document_fingerprint.cpp | Windowed FNV-1a 64 computation |
| libs/analysis-engine/include/pnga/analysis-engine/statistics_collector.h | Whole-document collection request/progress/result |
| libs/analysis-engine/src/statistics_collector.cpp | Chunk/filter/block/token section orchestration |
| libs/analysis-engine/include/pnga/analysis-engine/statistics_view.h | Immutable page rows and navigation requests |
| libs/analysis-engine/src/statistics_view.cpp | Snapshot-to-row projection |
| libs/analysis-engine/include/pnga/analysis-engine/statistics_occurrence_query.h | Bounded first/previous/next query |
| libs/analysis-engine/src/statistics_occurrence_query.cpp | Direct-index and scalar Token occurrence resolution |
| apps/pnga-cli/src/statistics_command.h/.cpp | CLI-only argument and collector composition |
| ui/qt/include/pnga/ui/qt/statistics_table_model.h and src/statistics_table_model.cpp | Lazy Qt formatting of immutable Statistics rows |
| ui/qt/include/pnga/ui/qt/statistics_inspector.h and src/statistics_inspector.cpp | Four-page Statistics UI and action signals |
| apps/png-analyzer-gui/src/statistics_worker.h/.cpp | Background collection with cooperative cancellation |
| apps/png-analyzer-gui/src/statistics_controller.h/.cpp | Lazy start, publication, navigation and atomic export |
| tests/unit/statistics/** | Snapshot, accumulator and serializer goldens |
| tests/unit/deflate-trace/token_decoder_test.cpp | Scalar scan equivalence and O(1) retention |
| tests/unit/analysis-engine/** | Fingerprint, collector, view and occurrence contracts |
| tests/integration/cli/cli_test.cpp | Real statistics command/output/exit tests |
| tests/gui/statistics_inspector_test.cpp | Widget/model/accessibility tests |
| tests/gui/statistics_controller_test.cpp | Lazy lifecycle/export/navigation integration |
| tests/performance/performance_runner.cpp | Bounded statistics performance and peak retained-token evidence |

---

### Task 1: WP-602B Per-section Snapshot and Accumulator

**Files:**
- Modify: libs/statistics/include/pnga/statistics/statistics.h
- Modify: libs/statistics/src/statistics.cpp
- Modify: libs/analysis-engine/src/statistics_adapter.cpp
- Modify: tests/unit/statistics/statistics_test.cpp
- Modify: tests/unit/analysis-engine/statistics_adapter_test.cpp

**Interfaces:**
- Produces: StatisticsSectionId, SectionStatus, SectionScope, SectionState, typed section data, StatisticsSnapshot and StatisticsAccumulator.
- Preserves: collect(const StatisticsInput&, StatisticsLimits, CancelPredicate) as a compatibility entry implemented through StatisticsAccumulator.
- Consumed by: Tasks 2–9.

Freeze these public shapes:

~~~cpp
enum class StatisticsSectionId {
  kOverview, kChunks, kFilters, kBlocks, kTokens, kLengths, kDistances
};
enum class SectionStatus {
  kUnavailable, kReady, kPartial, kCancelled, kBudgetExceeded,
  kInvalidInput, kOverflow, kError
};
enum class SectionScope { kNone, kWholeDocument, kVerifiedPrefix };

struct SectionState {
  SectionStatus status = SectionStatus::kUnavailable;
  bool complete = false;
  SectionScope scope = SectionScope::kNone;
  std::string error;
};

struct OverviewStatistics {
  std::uint64_t compressed_bytes = 0;
  std::uint64_t inflated_bytes = 0;
  bool has_compression_totals = false;
};
struct ChunkStatistics {
  std::uint64_t count = 0;
  std::uint64_t data_bytes = 0;
  std::vector<ChunkBucket> buckets;
};
struct FilterStatistics {
  std::uint64_t rows = 0;
  std::uint64_t data_bytes = 0;
  std::uint64_t invalid_rows = 0;
  std::vector<FilterBucket> buckets;
};
struct BlockStatistics {
  std::uint64_t count = 0;
  std::uint64_t compressed_bits = 0;
  std::uint64_t output_bytes = 0;
  std::vector<BlockBucket> buckets;
};
struct TokenStatistics {
  std::uint64_t count = 0;
  std::uint64_t input_bits = 0;
  std::uint64_t output_bytes = 0;
  std::vector<TokenBucket> buckets;
};
struct ValueStatistics { std::vector<ValueBucket> buckets; };

template <class Data>
struct StatisticsSection { SectionState state; Data data; };

struct StatisticsSnapshot {
  StatisticsSection<OverviewStatistics> overview;
  StatisticsSection<ChunkStatistics> chunks;
  StatisticsSection<FilterStatistics> filters;
  StatisticsSection<BlockStatistics> blocks;
  StatisticsSection<TokenStatistics> tokens;
  StatisticsSection<ValueStatistics> lengths;
  StatisticsSection<ValueStatistics> distances;
  bool complete() const noexcept;
};

class StatisticsAccumulator {
 public:
  explicit StatisticsAccumulator(StatisticsLimits limits = {});
  bool add(ChunkSample sample);
  bool add(FilterSample sample);
  bool add(BlockSample sample);
  bool add(TokenSample sample);
  bool set_compression_totals(std::uint64_t compressed,
                              std::uint64_t inflated);
  void finish(StatisticsSectionId id, SectionStatus status, bool complete,
              SectionScope scope, std::string error = {});
  const StatisticsSnapshot& snapshot() const noexcept;
};
~~~

- [ ] **Step 1: Write failing section-state tests**

Add Catch2 cases that require missing sources to remain unavailable, an empty-but-present source to become ready with zero totals, cancellation to preserve collected Chunk totals as verified_prefix, length and distance sections to fail together on an invalid match, and complete() to require all seven sections ready/complete/whole_document.

~~~cpp
REQUIRE(snapshot.chunks.state.status == SectionStatus::kReady);
REQUIRE(snapshot.chunks.state.complete);
REQUIRE(snapshot.chunks.state.scope == SectionScope::kWholeDocument);
REQUIRE(snapshot.tokens.state.status == SectionStatus::kUnavailable);
REQUIRE_FALSE(snapshot.complete());
~~~

- [ ] **Step 2: Run focused tests and confirm RED**

~~~bash
cmake --build --preset dev --target pnga_statistics_tests pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R 'statistics_engine|analysis_engine_artifact_store' --output-on-failure
~~~

Expected: compile failures for the new section types and accumulator.

- [ ] **Step 3: Implement checked accumulator transitions**

Move the existing checked-add, bucket-ordering and budget logic behind StatisticsAccumulator. Initialize fixed filter/block/token vectors to 5/3/3 entries. add(TokenSample) updates tokens and, for matches, lengths/distances atomically: validate non-zero length/distance and capacity before mutating either histogram. finish refuses ready+complete with a non-whole_document scope and preserves the first non-ready error.

- [ ] **Step 4: Reimplement collect and adapt missing sources**

Make collect feed all four supplied spans through the accumulator and mark its supplied sections complete. Update collect_statistics so each non-null StatisticsSources pointer marks only its owned section(s); a null pointer stays unavailable. Preserve validated prefixes on cancellation or malformed ranges.

- [ ] **Step 5: Run focused and module regression**

~~~bash
cmake --build --preset dev --target pnga_statistics_tests pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R 'statistics_engine|analysis_engine_artifact_store' --output-on-failure
~~~

Expected: PASS.

- [ ] **Step 6: Commit**

~~~bash
git add libs/statistics/include/pnga/statistics/statistics.h \
  libs/statistics/src/statistics.cpp \
  libs/analysis-engine/src/statistics_adapter.cpp \
  tests/unit/statistics/statistics_test.cpp \
  tests/unit/analysis-engine/statistics_adapter_test.cpp
git commit -m "feat: add sectioned statistics snapshot"
~~~

### Task 2: WP-602B/D Document Identity and Shared Serializers

**Files:**
- Create: libs/analysis-engine/include/pnga/analysis-engine/document_fingerprint.h
- Create: libs/analysis-engine/src/document_fingerprint.cpp
- Modify: libs/analysis-engine/CMakeLists.txt
- Create: libs/statistics/include/pnga/statistics/serialization.h
- Create: libs/statistics/src/serialization.cpp
- Modify: libs/statistics/CMakeLists.txt
- Create: tests/unit/analysis-engine/document_fingerprint_test.cpp
- Modify: tests/unit/analysis-engine/CMakeLists.txt
- Create: tests/unit/statistics/serialization_test.cpp
- Create: tests/unit/statistics/golden/ready-v1.json
- Create: tests/unit/statistics/golden/ready-v1.csv
- Create: tests/unit/statistics/golden/partial-v1.json
- Create: tests/unit/statistics/golden/partial-v1.csv
- Modify: tests/unit/statistics/CMakeLists.txt

**Interfaces:**

~~~cpp
namespace pnga::statistics {
struct DocumentIdentity {
  std::uint64_t file_size = 0;
  std::string fingerprint;
};
struct SerializationResult {
  bool success = false;
  std::string bytes;
  std::string error;
};
SerializationResult serialize_statistics_json(
    const DocumentIdentity&, const StatisticsSnapshot&);
SerializationResult serialize_statistics_csv(
    const DocumentIdentity&, const StatisticsSnapshot&);
}

namespace pnga::analysis_engine {
std::optional<pnga::statistics::DocumentIdentity> compute_document_identity(
    const pnga::io::IByteSource&, const CancellationToken* cancellation,
    std::string* error);
}
~~~

- [ ] **Step 1: Add failing fingerprint vectors**

Test the FNV-1a 64 offset basis for an empty source, the canonical value af63dc4c8601ec8c for one byte a, equality across MemoryByteSource and a read-only windowed test source, cancellation between 64 KiB reads, and read failure without a partial identity.

- [ ] **Step 2: Add failing JSON/CSV golden tests**

Build one ready and one mixed partial snapshot. Switch the global C locale through C, C.UTF-8 when present and one non-English installed locale; require byte equality to committed goldens. Assert no absolute path, timestamp, CR, BOM or extra final blank line appears. Include comma, quote and LF in a synthetic error/key to prove RFC 4180 and JSON escaping.

- [ ] **Step 3: Run and confirm RED**

~~~bash
cmake --build --preset dev --target pnga_statistics_tests pnga_analysis_engine_tests --parallel 4
~~~

Expected: missing headers/functions.

- [ ] **Step 4: Implement windowed fingerprint**

Read exactly min(65,536, remaining) bytes per call, update FNV-1a with unsigned byte values, check offset advance, and format with lowercase fixed-width hexadecimal. Return fnv1a64-v1:cbf29ce484222325 for an empty source.

- [ ] **Step 5: Implement the sole serializers**

Emit JSON in schema/document/sections order and fixed per-section metric order. CSV begins with exactly:

~~~text
schema_version,section,metric,key,value,unit
~~~

For each section emit status, complete and scope before data. Emit unavailable numeric values as an empty value field, not 0. Implement local JSON and RFC 4180 escaping without locale streams; format integers through std::to_chars. Validate the fingerprint prefix/length and reject an internally contradictory ready/complete/scope state.

- [ ] **Step 6: Run focused tests**

~~~bash
cmake --build --preset dev --target pnga_statistics_tests pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R 'statistics_engine|analysis_engine_artifact_store' --output-on-failure
~~~

Expected: PASS and byte-identical goldens.

- [ ] **Step 7: Commit**

~~~bash
git add libs/statistics/include/pnga/statistics/serialization.h \
  libs/statistics/src/serialization.cpp libs/statistics/CMakeLists.txt \
  libs/analysis-engine/CMakeLists.txt \
  libs/analysis-engine/include/pnga/analysis-engine/document_fingerprint.h \
  libs/analysis-engine/src/document_fingerprint.cpp \
  tests/unit/statistics/serialization_test.cpp \
  tests/unit/statistics/golden/ready-v1.json \
  tests/unit/statistics/golden/ready-v1.csv \
  tests/unit/statistics/golden/partial-v1.json \
  tests/unit/statistics/golden/partial-v1.csv \
  tests/unit/statistics/CMakeLists.txt tests/unit/analysis-engine/CMakeLists.txt \
  tests/unit/analysis-engine/document_fingerprint_test.cpp
git commit -m "feat: add deterministic statistics serialization"
~~~

### Task 3: WP-602C Streaming DEFLATE Scalar Observer

**Files:**
- Modify: libs/deflate-trace/include/pnga/deflate-trace/token_decoder.h
- Modify: libs/deflate-trace/src/token_decoder.cpp
- Modify: tests/unit/deflate-trace/token_decoder_test.cpp

**Interfaces:**

~~~cpp
enum class TokenScanStatus {
  kReady, kPartial, kCancelled, kBudgetExceeded, kInvalidInput, kError
};
struct TokenFact {
  TokenKind kind = TokenKind::kLiteral;
  std::uint64_t input_bits = 0;
  std::uint64_t output_bytes = 0;
  std::uint16_t length = 0;
  std::uint16_t distance = 0;
  std::uint64_t output_begin = 0;
};
struct TokenScanOptions {
  std::uint64_t max_input_bytes = 0;
  std::uint64_t max_output_bytes = 0;
  std::uint64_t max_tokens = 0;
  std::function<bool()> should_cancel;
  std::function<bool(const TokenFact&)> observer;
};
struct TokenScanResult {
  TokenScanStatus status = TokenScanStatus::kError;
  std::string error;
  std::uint64_t token_count = 0;
  std::uint64_t input_bits = 0;
  std::uint64_t output_bytes = 0;
  bool stream_ended = false;
};
TokenScanResult scan_tokens(const pnga::io::IByteSource&,
                            const TokenScanOptions&);
~~~

- [ ] **Step 1: Add scalar/rich equivalence tests**

For Stored, Fixed, Dynamic, length/distance overlap, multi-block, truncated and invalid streams, compare every scalar TokenFact with the corresponding fields from decode_stored_and_fixed. Assert the observer sees EOB, cancellation keeps the counted prefix, and false from observer returns Partial without reading later input.

- [ ] **Step 2: Add bounded-retention test**

Use a generated stream with more than one million literal/match tokens and a counting observer that stores only totals. Expose the auditable metric peak_retained_token_records in TokenScanResult and require it <= 1; require no output vector or Huffman table vector in the scalar result. Also use an IByteSource that refuses view() to prove bounded read() operation.

- [ ] **Step 3: Run and confirm RED**

~~~bash
cmake --build --preset dev --target pnga_deflate_trace_tests --parallel 4
ctest --preset dev -R deflate_trace --output-on-failure
~~~

Expected: missing scalar scan API.

- [ ] **Step 4: Refactor one decoder engine with two sinks**

Replace the whole-input BitReader with a bounded windowed reader over IByteSource. Keep one 32 KiB LZ value window. Route decoded scalar facts to an internal sink interface: RichTraceSink builds the existing TokenDecodeResult; ScalarSink invokes TokenScanOptions::observer and retains no events/tables/output. Do not duplicate Stored/Fixed/Dynamic symbol interpretation.

- [ ] **Step 5: Enforce input/output/token/cancel boundaries**

Check cancellation at least every 256 tokens and every input-window refill. Before decoding beyond a limit, return BudgetExceeded with exact consumed input bits/output bytes. A malformed stream returns InvalidInput/Error while keeping counters for the verified prefix. The legacy decode function preserves its current messages, token ranges and tests.

- [ ] **Step 6: Run full DEFLATE regression**

~~~bash
cmake --build --preset dev --parallel 4
ctest --preset dev -R 'deflate_trace|analysis_engine_artifact_store|compression_inspector' --output-on-failure
~~~

Expected: PASS.

- [ ] **Step 7: Commit**

~~~bash
git add libs/deflate-trace/include/pnga/deflate-trace/token_decoder.h \
  libs/deflate-trace/src/token_decoder.cpp \
  tests/unit/deflate-trace/token_decoder_test.cpp
git commit -m "feat: add streaming token statistics observer"
~~~

### Task 4: WP-602C Whole-document Collector

**Files:**
- Create: libs/analysis-engine/include/pnga/analysis-engine/statistics_collector.h
- Create: libs/analysis-engine/src/statistics_collector.cpp
- Modify: libs/analysis-engine/CMakeLists.txt
- Create: tests/unit/analysis-engine/statistics_collector_test.cpp
- Modify: tests/unit/analysis-engine/CMakeLists.txt

**Interfaces:**

~~~cpp
struct StatisticsProgress {
  pnga::statistics::StatisticsSectionId section;
  std::uint64_t processed = 0;
  std::optional<std::uint64_t> total;
};
struct StatisticsCollectionRequest {
  std::uint64_t generation = 0;
  std::shared_ptr<const pnga::io::IByteSource> source;
  pnga::png_format::ChunkIndex chunks;
  std::shared_ptr<const StageSet> stages;
  pnga::statistics::StatisticsLimits limits;
  std::uint64_t max_working_bytes = 64ull << 20;
};
struct StatisticsCollectionResult {
  std::uint64_t generation = 0;
  pnga::statistics::DocumentIdentity document;
  pnga::statistics::StatisticsSnapshot snapshot;
};
using StatisticsProgressCallback =
    std::function<void(const StatisticsCollectionResult&,
                       const StatisticsProgress&)>;
StatisticsCollectionResult collect_document_statistics(
    const StatisticsCollectionRequest&, const CancellationToken*,
    StatisticsProgressCallback = {});
~~~

- [ ] **Step 1: Write failing staged-publication tests**

Generate WP-607C Stored/Fixed/Dynamic/cross-IDAT/malformed fixtures. Require progress order overview/chunks/filters/blocks before tokens, at most 10 progress callbacks in one logical second through an injected monotonic-clock seam, and each later snapshot to preserve earlier verified rows. Missing/failed StageSet makes only filters unavailable/error.

- [ ] **Step 2: Write cancellation and budget tests**

Cancel during fingerprint, blocks and tokens and assert the matching section plus dependent sections are cancelled/partial with verified_prefix while completed sections remain ready. Reject max_working_bytes > 64 MiB and zero limits. Require a large token stream to finish without TokenEvent retention.

- [ ] **Step 3: Run and confirm RED**

~~~bash
cmake --build --preset dev --target pnga_analysis_engine_tests --parallel 4
~~~

Expected: missing collector API.

- [ ] **Step 4: Implement section pipeline**

Compute DocumentIdentity, feed ChunkIndex and StageSet scalars to StatisticsAccumulator, build VirtualIDATStream without concatenation, adapt it to IByteSource, run index_blocks for complete Block facts, then scan_tokens with the accumulator observer. Map each lower-level result to the frozen SectionStatus/Scope without upgrading Partial to Ready.

- [ ] **Step 5: Implement throttled immutable progress**

Publish value copies/shared immutable snapshots after fast sections and during token scanning no more than every 100 ms. The callback receives the request generation and never receives mutable accumulator storage. Do not invoke callbacks while holding decoder or scheduler locks.

- [ ] **Step 6: Verify**

~~~bash
cmake --build --preset dev --target pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R 'analysis_engine_artifact_store|statistics_engine|wp607c_trace_facts' --output-on-failure
~~~

Expected: PASS.

- [ ] **Step 7: Commit**

~~~bash
git add libs/analysis-engine/include/pnga/analysis-engine/statistics_collector.h \
  libs/analysis-engine/src/statistics_collector.cpp \
  libs/analysis-engine/CMakeLists.txt \
  tests/unit/analysis-engine/statistics_collector_test.cpp \
  tests/unit/analysis-engine/CMakeLists.txt
git commit -m "feat: collect whole-document statistics"
~~~

### Task 5: WP-602E CLI Statistics Command

**Files:**
- Create: apps/pnga-cli/src/statistics_command.h
- Create: apps/pnga-cli/src/statistics_command.cpp
- Modify: apps/pnga-cli/src/main.cpp
- Modify: apps/pnga-cli/CMakeLists.txt
- Modify: tests/integration/cli/cli_test.cpp
- Modify: README.md
- Modify: docs/user-guide.md

**Interfaces:**

~~~cpp
namespace pnga::cli {
enum class StatisticsOutputFormat { kJson, kCsv };
int run_statistics_command(const std::filesystem::path& file,
                           StatisticsOutputFormat format,
                           FILE* out, FILE* err);
}
~~~

- [ ] **Step 1: Split CLI stdout/stderr capture in tests**

Change CliResult to hold stdout_text and stderr_text using two temporary files. Preserve existing inspect/validate expectations, including platform exit-code normalization.

- [ ] **Step 2: Add failing command-contract tests**

Cover JSON and CSV ready output, byte equality with direct serializer output, missing file exit 1, missing/duplicate/unknown --format exit 2, malformed-with-usable-prefix exit 3, budget/partial exit 4, stdout free of diagnostics, stderr free of report bytes, and --help containing pnga.statistics schema_version=1 plus all five exit meanings.

- [ ] **Step 3: Run and confirm RED**

~~~bash
cmake --build --preset dev --target pnga_cli pnga_cli_tests --parallel 4
ctest --preset dev -R cli --output-on-failure
~~~

Expected: unknown statistics command.

- [ ] **Step 4: Implement strict parsing and synchronous composition**

Accept exactly one positional file and one --format value. Map the file once, index chunks, run analyze_source off the CLI main thread restriction because CLI has no UI thread, invoke collect_document_statistics, validate_document and the shared serializer, then write bytes with fwrite. Do not use puts because the serializer already owns the final LF.

- [ ] **Step 5: Implement exact exit mapping**

Return 0 only when every requested section is ready/complete. Return 3 when validation has issues and at least one section is usable. Return 4 for verified partial/cancel/budget without a stronger I/O/argument failure. Serialization failure writes only stderr and returns 4.

- [ ] **Step 6: Update user-facing CLI documentation**

Add the two command examples, schema version, stdout/stderr contract and exit code 4. Keep the README statement that Statistics UI is deferred until Task 8 makes the GUI available; Task 5 documents only the completed CLI surface.

- [ ] **Step 7: Verify and commit**

~~~bash
cmake --build --preset dev --target pnga_cli pnga_cli_tests --parallel 4
ctest --preset dev -R cli --output-on-failure
git add apps/pnga-cli/src/statistics_command.h \
  apps/pnga-cli/src/statistics_command.cpp apps/pnga-cli/src/main.cpp \
  apps/pnga-cli/CMakeLists.txt tests/integration/cli/cli_test.cpp \
  README.md docs/user-guide.md
git commit -m "feat: add deterministic statistics cli"
~~~

### Task 6: WP-602F Immutable Rows and Bounded Occurrence Navigation

**Files:**
- Create: libs/analysis-engine/include/pnga/analysis-engine/statistics_view.h
- Create: libs/analysis-engine/src/statistics_view.cpp
- Create: libs/analysis-engine/include/pnga/analysis-engine/statistics_occurrence_query.h
- Create: libs/analysis-engine/src/statistics_occurrence_query.cpp
- Modify: libs/analysis-engine/CMakeLists.txt
- Create: tests/unit/analysis-engine/statistics_view_test.cpp
- Create: tests/unit/analysis-engine/statistics_occurrence_query_test.cpp
- Modify: tests/unit/analysis-engine/CMakeLists.txt

**Interfaces:**

~~~cpp
enum class StatisticsPage { kOverview, kChunks, kFilters, kDeflate };
enum class StatisticsBucketDomain {
  kChunkType, kFilterType, kBlockType, kTokenKind, kLength, kDistance
};
enum class OccurrenceDirection { kFirst, kPrevious, kNext };
struct StatisticsNavigationRequest {
  std::uint64_t generation = 0;
  StatisticsBucketDomain domain;
  std::string key;
  OccurrenceDirection direction = OccurrenceDirection::kFirst;
  std::optional<std::uint64_t> after_output_offset;
  std::uint64_t max_tokens = 4096;
  std::uint64_t max_input_bytes = 8ull << 20;
};
struct StatisticsRow {
  std::string id;
  std::string group;
  std::string label;
  std::optional<std::uint64_t> value;
  std::string unit;
  pnga::statistics::SectionState state;
  std::optional<StatisticsNavigationRequest> navigation;
};
struct StatisticsView {
  std::uint64_t generation = 0;
  std::vector<StatisticsRow> overview;
  std::vector<StatisticsRow> chunks;
  std::vector<StatisticsRow> filters;
  std::vector<StatisticsRow> deflate;
};
StatisticsView build_statistics_view(
    std::uint64_t generation,
    const pnga::statistics::StatisticsSnapshot&);

enum class OccurrenceStatus { kReady, kPartial, kNotFound, kCancelled, kError };
struct StatisticsOccurrenceResult {
  OccurrenceStatus status;
  std::string error;
  std::uint64_t generation = 0;
  pnga::trace_model::Selection selection;
  std::uint64_t searched_input_bytes = 0;
  std::uint64_t searched_tokens = 0;
};
StatisticsOccurrenceResult query_statistics_occurrence(
    const pnga::io::IByteSource&, const pnga::png_format::ChunkIndex&,
    const StageSet*, const pnga::deflate_index::BlockIndexResult*,
    const StatisticsNavigationRequest&, const CancellationToken*);
~~~

- [ ] **Step 1: Add deterministic row tests**

Require stable IDs such as chunks.IDAT, filters.4, blocks.dynamic, tokens.match, lengths.258 and distances.32768; fixed page/group ordering; raw integers without locale formatting; unavailable rows retaining state; and navigation requests carrying the view generation and frozen budgets.

- [ ] **Step 2: Add direct and Token occurrence tests**

Prove Chunk/Filter/Block first/previous/next use index facts and return typed Selection. For Token/Length/Distance, cover Stored/Fixed/Dynamic, first/previous/next around an output cursor, overlap matches, no match, 4,096-token truncation, 8 MiB input truncation, cancellation and stale generation rejection by the caller.

- [ ] **Step 3: Run and confirm RED**

~~~bash
cmake --build --preset dev --target pnga_analysis_engine_tests --parallel 4
~~~

Expected: missing view/query APIs.

- [ ] **Step 4: Implement row projection**

Emit Overview then sorted Chunk rows, Filters 0–4, Blocks Stored/Fixed/Dynamic, Tokens Literal/Match/EOB, Length ascending and Distance ascending. Put Blocks/Tokens/Lengths/Distances in the single deflate vector with group labels. Do not format percentages or thousands separators in analysis-engine.

- [ ] **Step 5: Implement bounded navigation**

Resolve direct domains from existing ranges. Resolve Token domains through scan_tokens; first stops on first match, next skips output offsets <= cursor, previous retains only the last matching scalar before cursor. Convert matched facts to existing Selection/typed compression ranges and map every cross-IDAT physical span.

- [ ] **Step 6: Verify and commit**

~~~bash
cmake --build --preset dev --target pnga_analysis_engine_tests --parallel 4
ctest --preset dev -R 'analysis_engine_artifact_store|selection|trace' --output-on-failure
git add libs/analysis-engine/include/pnga/analysis-engine/statistics_view.h \
  libs/analysis-engine/src/statistics_view.cpp \
  libs/analysis-engine/include/pnga/analysis-engine/statistics_occurrence_query.h \
  libs/analysis-engine/src/statistics_occurrence_query.cpp \
  libs/analysis-engine/CMakeLists.txt \
  tests/unit/analysis-engine/statistics_view_test.cpp \
  tests/unit/analysis-engine/statistics_occurrence_query_test.cpp \
  tests/unit/analysis-engine/CMakeLists.txt
git commit -m "feat: add statistics views and occurrence queries"
~~~

### Task 7: WP-602G Statistics Qt Model and Inspector

**Files:**
- Create: ui/qt/include/pnga/ui/qt/statistics_table_model.h
- Create: ui/qt/src/statistics_table_model.cpp
- Create: ui/qt/include/pnga/ui/qt/statistics_inspector.h
- Create: ui/qt/src/statistics_inspector.cpp
- Modify: ui/qt/CMakeLists.txt
- Create: tests/gui/statistics_inspector_test.cpp
- Modify: tests/gui/CMakeLists.txt

**Interfaces:**

~~~cpp
class StatisticsTableModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  explicit StatisticsTableModel(QObject* parent = nullptr);
  void setRows(std::shared_ptr<const std::vector<StatisticsRow>> rows);
  const StatisticsRow* rowAt(int row) const noexcept;
};

class StatisticsInspector final : public QWidget {
  Q_OBJECT
 public:
  explicit StatisticsInspector(QWidget* parent = nullptr);
  void setView(std::shared_ptr<const StatisticsView> view);
  void setProgress(QString text);
  void clear();
 signals:
  void refreshRequested();
  void cancelRequested();
  void exportRequested(int format);
  void occurrenceRequested(StatisticsNavigationRequest request);
};
~~~

Freeze object names: statisticsInspector, statisticsPages, statisticsOverviewTable, statisticsChunksTable, statisticsFiltersTable, statisticsDeflateTable, statisticsRefresh, statisticsCancel, statisticsExportJson, statisticsExportCsv, statisticsShowOccurrence and statisticsProgress.

- [ ] **Step 1: Add failing construction/accessibility tests**

Require page labels Overview/Chunks/Filters/DEFLATE in that order, all object names, non-empty accessible names/roles, no per-row QWidget delegates, 320 px width without horizontal growth beyond table scroll behavior, and initial unavailable copy without zero values.

- [ ] **Step 2: Add model/action tests**

Publish ready then partial views and require verified rows remain, numeric display follows the current QLocale only in DisplayRole while raw UserRole is quint64, selection enables Show occurrence only when navigation exists, Enter triggers it, Escape cancels, Refresh/Cancel/export signals fire exactly once, and keyboard tab order covers every action and page table.

- [ ] **Step 3: Run and confirm RED**

~~~bash
cmake --build --preset dev --target pnga_gui_statistics_inspector_tests --parallel 4
~~~

Expected: target/source missing.

- [ ] **Step 4: Implement model and four-page widget**

Use QTableView plus QAbstractTableModel for every page. Use fixed columns Group, Metric, Value, Unit, Status; hide Group where not needed. Preserve selection by stable row id across progress snapshots. Use accessible descriptions to include partial/error scope without encoding semantics only by color.

- [ ] **Step 5: Verify Qt component**

~~~bash
cmake --build --preset dev --target pnga_gui_statistics_inspector_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R gui_statistics_inspector --output-on-failure
~~~

Expected: PASS.

- [ ] **Step 6: Commit**

~~~bash
git add ui/qt/include/pnga/ui/qt/statistics_table_model.h \
  ui/qt/src/statistics_table_model.cpp \
  ui/qt/include/pnga/ui/qt/statistics_inspector.h \
  ui/qt/src/statistics_inspector.cpp ui/qt/CMakeLists.txt \
  tests/gui/statistics_inspector_test.cpp tests/gui/CMakeLists.txt
git commit -m "feat: add statistics inspector widget"
~~~

### Task 8: WP-602G Lazy GUI Worker, Controller and Atomic Export

**Files:**
- Create: apps/png-analyzer-gui/src/statistics_worker.h
- Create: apps/png-analyzer-gui/src/statistics_worker.cpp
- Create: apps/png-analyzer-gui/src/statistics_controller.h
- Create: apps/png-analyzer-gui/src/statistics_controller.cpp
- Modify: apps/png-analyzer-gui/src/document_session.h
- Modify: apps/png-analyzer-gui/src/document_session.cpp
- Modify: apps/png-analyzer-gui/src/main_window_ui.h
- Modify: apps/png-analyzer-gui/src/main_window_ui.cpp
- Modify: apps/png-analyzer-gui/src/main_window.h
- Modify: apps/png-analyzer-gui/src/main_window.cpp
- Modify: apps/png-analyzer-gui/CMakeLists.txt
- Modify: tests/gui/CMakeLists.txt
- Modify: tests/gui/document_session_test.cpp
- Modify: tests/gui/main_window_ui_test.cpp
- Create: tests/gui/statistics_controller_test.cpp
- Modify: README.md
- Modify: docs/user-guide.md

**Interfaces:**

~~~cpp
class StatisticsWorker final : public QThread {
  Q_OBJECT
 public:
  StatisticsWorker(StatisticsCollectionRequest request, QObject* parent);
  void cancel() noexcept;
 signals:
  void progress(std::uint64_t generation,
                std::shared_ptr<const StatisticsCollectionResult> result);
  void finishedResult(std::uint64_t generation,
                      std::shared_ptr<const StatisticsCollectionResult> result);
};

class StatisticsController final : public QObject {
  Q_OBJECT
 public:
  StatisticsController(MainWindowWidgets, DocumentSession&, QObject* parent);
  void onDocumentReplaced(std::uint64_t generation);
  void onDocumentClosed(std::uint64_t generation);
  void onStagesPublished(std::uint64_t generation);
};
~~~

- [ ] **Step 1: Add failing DocumentSession lifecycle tests**

Open a fixture and assert no Statistics worker starts during replace/startPrimaryWorkers. Request Statistics before StageSet publication and assert it waits, then starts once. Replace/close during collection, require cancellation and no old-generation progress/final signal. Repeated first-tab activation must not create duplicate workers.

- [ ] **Step 2: Add failing UI composition tests**

Require top-level Inspector order Reconstruction, Compression, Statistics; the Statistics widget pointer in MainWindowWidgets; unchanged Reconstruction default; no collection before selecting Statistics; one lazy request on first selection; Refresh starts a new generation-scoped request; Cancel retains verified rows.

- [ ] **Step 3: Add failing export/navigation tests**

Inject a deterministic save-path callback in StatisticsController tests. Require JSON/CSV file bytes equal the shared serializer, Partial export remains enabled and visibly labeled, QSaveFile commit failure leaves the old target bytes untouched, and occurrence results publish once through SelectionBus while stale results publish nothing.

- [ ] **Step 4: Run and confirm RED**

~~~bash
cmake --build --preset dev --target pnga_gui_document_session_tests \
  pnga_gui_main_window_ui_tests pnga_gui_statistics_controller_tests --parallel 4
~~~

Expected: missing worker/controller/widget fields.

- [ ] **Step 5: Implement StatisticsWorker and session ownership**

Register immutable shared result metatypes. StatisticsWorker calls collect_document_statistics and checks requestInterruption through a CancellationToken bridge. DocumentSession owns at most one worker, records statistics_requested_, starts it with QThread::LowPriority only when source/index/StageSet are available, generation-checks both progress and final publication, and cancels on replace/close/destruction.

- [ ] **Step 6: Compose the tab and controller**

Append Statistics after Compression in buildMainWindowUi. StatisticsController listens to inspectorTabs::currentChanged, widget actions and session signals. It transforms snapshots through build_statistics_view off the UI hot path, applies immutable views on the GUI thread, routes occurrence queries through background work and publishes accepted selections through the existing bus.

- [ ] **Step 7: Implement QSaveFile export**

Choose a path only after a verified section exists. Serialize before opening the destination; write all bytes, check write length and commit. On any failure show a stable error and leave the prior file untouched. Never add a custom Statistics serialization path to Qt code.

- [ ] **Step 8: Finish documentation and verify focused GUI**

Update README/current capabilities and docs/user-guide.md with the Statistics tab, lazy behavior, Partial labeling, CLI commands and deterministic export contract.

~~~bash
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'statistics|document_session|main_window|selection|gui' --output-on-failure
~~~

Expected: PASS.

- [ ] **Step 9: Commit**

~~~bash
git add apps/png-analyzer-gui/src/statistics_worker.h \
  apps/png-analyzer-gui/src/statistics_worker.cpp \
  apps/png-analyzer-gui/src/statistics_controller.h \
  apps/png-analyzer-gui/src/statistics_controller.cpp \
  apps/png-analyzer-gui/src/document_session.h \
  apps/png-analyzer-gui/src/document_session.cpp \
  apps/png-analyzer-gui/src/main_window_ui.h \
  apps/png-analyzer-gui/src/main_window_ui.cpp \
  apps/png-analyzer-gui/src/main_window.h \
  apps/png-analyzer-gui/src/main_window.cpp \
  apps/png-analyzer-gui/CMakeLists.txt tests/gui/CMakeLists.txt \
  tests/gui/document_session_test.cpp tests/gui/main_window_ui_test.cpp \
  tests/gui/statistics_controller_test.cpp README.md docs/user-guide.md
git commit -m "feat: integrate lazy statistics gui"
~~~

### Task 9: WP-602H Product, Determinism and Performance Gate

**Files:**
- Create: tests/gui/statistics_product_gate_test.cpp
- Modify: tests/gui/CMakeLists.txt
- Modify: tests/performance/performance_runner.cpp
- Modify: tests/performance/thresholds-v1.json
- Modify: tests/performance/README.md
- Modify: docs/development/wp-602b-statistics-ui-export-reentry.md
- Modify: docs/architecture/png-analyzer-current-development-plan-2026-08-22.md

**Interfaces:**
- Produces: CTest entry gui_statistics_product_gate_tests and performance scenario statistics.
- Closes: WP-602B–H only when every command below passes and the completion record contains exact evidence.

- [ ] **Step 1: Add the failing GUI product matrix**

Drive the real MainWindow with WP-607C ready, malformed, large and rapid-switch fixtures. Cover lazy start, progress preservation, Refresh, Cancel, stale generation, four pages, 320 px width, keyboard, accessible names/states, three locale display passes, Partial export labeling, occurrence navigation and exact GUI-export/direct-serializer byte equality.

- [ ] **Step 2: Add the failing performance scenario**

Measure collector fast_sections_us, whole_document_us, token_count, peak_retained_token_records, view_projection_us and serializer_us on perf-large-rgba8. Assert file-open and 200 hover events execute without invoking the collector. Add reviewed generous maxima to thresholds-v1.json and require peak_retained_token_records <= 1 and max_working_bytes <= 64 MiB as non-time invariants.

- [ ] **Step 3: Run and confirm RED**

~~~bash
cmake --build --preset dev --target pnga_gui_statistics_product_gate_tests \
  pnga_performance_runner --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R statistics_product_gate --output-on-failure
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds
~~~

Expected: new product/performance assertions fail until final wiring and record shape are complete.

- [ ] **Step 4: Make the smallest gate fixes**

Fix only Statistics paths exposed by the new matrix. Do not weaken thresholds, skip malformed/locale cases, increase token/input/memory budgets or change existing open/hover behavior. Any needed parser/reconstruction or dependency change is BLOCKED.

- [ ] **Step 5: Run the complete verification matrix**

~~~bash
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'statistics|cli|main_window|selection|gui' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds
python3 scripts/run_sanitizer_fuzz_gate.py
git diff --check
~~~

Expected: all commands exit 0; GUI gate passes three configured scales; statistics scenario stays under reviewed thresholds; sanitizer/fuzz regression is clean.

- [ ] **Step 6: Review invariants**

Confirm no Qt include under libs, no complete IDAT copy, no whole-file TokenEvent/output vector in scalar mode, no output path/locale/clock in reports, no stale GUI publication, no non-atomic export, no Statistics work on file-open/hover, and no APNG/Compare field or visible control.

- [ ] **Step 7: Write the completion record**

Update the package status to PASS only with test counts, exact commands/exit codes, schema golden hashes, performance metrics, maximum retained-token records, peak reservation, locale list and changed-path audit. Otherwise record exactly BLOCKED or FAIL with the named unmet cell.

- [ ] **Step 8: Commit**

~~~bash
git add tests/gui/statistics_product_gate_test.cpp tests/gui/CMakeLists.txt \
  tests/performance docs/development/wp-602b-statistics-ui-export-reentry.md \
  docs/architecture/png-analyzer-current-development-plan-2026-08-22.md
git commit -m "test: close statistics product gate"
~~~

## Plan Self-review Result

- Spec coverage: WP-602B through WP-602H each maps to at least one task; status/schema, streaming, serializers, CLI, rows/navigation, GUI/export and final gates are all covered.
- Scope: the work is one dependency chain with independently reviewable increments; APNG and Compare remain excluded.
- Type consistency: StatisticsSnapshot, DocumentIdentity, StatisticsCollectionResult, StatisticsView and StatisticsNavigationRequest are introduced before their consumers and retain the same names thereafter.
- Placeholder scan: no implementation placeholder, unspecified edge-case instruction or deferred in-scope requirement remains.

## Execution Handoff

Execute only from an isolated feature worktree created at execution time. Preferred branch name: wp-602b-h-statistics-ui-export. Use subagent-driven-development for one fresh implementer/reviewer cycle per task, or executing-plans for serial batches with review checkpoints.
