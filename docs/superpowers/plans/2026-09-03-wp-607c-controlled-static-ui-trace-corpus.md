# WP-607C Controlled Static UI/Trace Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic 19-case static PNG/DEFLATE corpus with exact facts, hashes, owning CTest targets and a reusable gate that unblocks WP-5U12F.

**Architecture:** A Qt-free C++20 test-only library owns one typed registry and writes explicit PNG chunks and DEFLATE bitstreams. A generator materializes the registry under the active build tree; Python validates the YAML/catalog/hash contract and writes gate evidence, while Catch2, Qt and performance consumers all use the same cases and corpus revision.

**Tech Stack:** C++20, CMake 3.28+, CTest, Catch2 3.11.0, Qt 6.8+, pinned zlib 1.3.2/libpng 1.6.58, Python 3.11+, PyYAML and Python standard-library `hashlib`/`json`/`unittest`.

**Spec:** `docs/development/wp-607c-controlled-static-ui-trace-corpus.md`

## Global Constraints

- Start from branch `wp-5u12-compression-inspector`, commit `7d30e3c` or its direct documentation-only successor; record and explain any different baseline before editing.
- WP-5U15 and WP-5U12A-E plus audited behavioral fixes must remain intact; the pre-change gate is 47/47 CTest entries passing.
- Production uses C++20, CMake and Qt 6; all `libs/` code remains Qt-free.
- Add no dependency, package manager, top-level directory, third-party fixture or committed generated PNG.
- Generated output lives only under `${CMAKE_BINARY_DIR}/tests/corpus/wp-607c/` and must never be written into source `tests/corpus/`.
- Use explicit test-side DEFLATE bit writers; do not let zlib compression heuristics select Block layout or tokens.
- Use pinned zlib only for deterministic CRC-32 and Adler-32 calculations and libpng only from explicit oracle tests.
- Preserve multiple IDAT payloads as ordered physical spans; never concatenate a complete production IDAT stream into a new large buffer.
- Use checked arithmetic for every generated length, offset, span end, dimension product and allocation.
- Keep JSON/YAML order, filenames, expected facts and hashes deterministic and locale-independent.
- Do not modify `libs/`, `ui/qt/`, `apps/`, `third_party/`, `.github/` or `packaging/`; a revealed production defect returns `BLOCKED` and moves to a separately approved defect package.
- Run tasks serially. Each task ends with a focused passing gate and a commit before the next task begins.

## File Structure

New files:

- `tests/corpus/controlled_fixture.h` — independent test-side ids, fact types and construction API.
- `tests/corpus/controlled_fixture.cpp` — checked PNG/chunk builders, explicit DEFLATE writers and the 19-case registry.
- `tests/corpus/generate_controlled_corpus.cpp` — safe atomic build-tree writer, private SHA-256 implementation and deterministic `index.json` writer.
- `tests/corpus/png_facts_test.cpp` — pixel, palette, filter, Adam7 and libpng-oracle assertions.
- `tests/corpus/trace_facts_test.cpp` — Block, Huffman, token, cross-IDAT and malformed assertions.
- `tests/corpus/CMakeLists.txt` — test-only library, generator, CTest setup fixture and labels.
- `tests/gui/controlled_corpus_gate_test.cpp` — real-file Qt state and narrow-width gate.
- `scripts/verify_wp607c_manifest.py` — deep YAML/catalog/hash/CTest contract validator.
- `scripts/run_wp607c_corpus_gate.py` — one-shot build, double-generation, test and evidence wrapper.

Modified files:

- `tests/corpus/manifest.yaml`, `tests/corpus/README.md` — versioned generated/external records and operator guidance.
- `tests/CMakeLists.txt`, `tests/gui/CMakeLists.txt` — corpus and GUI target registration.
- `scripts/verify_repository_layout.py`, `scripts/verify_dependencies.py` — kind-aware source-tree policy.
- `tests/differential/differential_harness_test.cpp`, `tests/differential/CMakeLists.txt` — five controlled pixel cases.
- `tests/performance/performance_runner.cpp`, `tests/performance/CMakeLists.txt`, `scripts/run_performance_corpus.py` — shared large case and revision propagation.
- `tests/unit/deflate-index/block_index_test.cpp`, `tests/unit/deflate-index/CMakeLists.txt` — migrate only exact duplicate fixture construction.
- `tests/unit/deflate-trace/token_decoder_test.cpp`, `tests/unit/deflate-trace/CMakeLists.txt` — migrate only exact duplicate fixture construction.
- `docs/development/wp-607c-controlled-static-ui-trace-corpus.md`, `docs/development/wp-607-cross-platform-quality-evidence.md` — final status and handoff evidence.

---

### Task 1: Freeze the generated/external manifest schema

**Files:**
- Create: `scripts/verify_wp607c_manifest.py`
- Modify: `scripts/verify_dependencies.py:134-162`
- Modify: `scripts/verify_repository_layout.py:177-201`
- Modify: `tests/corpus/README.md`

**Interfaces:**
- Consumes: existing top-level-list `tests/corpus/manifest.yaml` and `ctest --show-only=json-v1` output.
- Produces: `validate_manifest(document, catalog, ctest_names) -> list[str]`, `aggregate_revision(manifest_bytes, generator_source_bytes) -> str`, and CLI modes `--self-test`, `--manifest`, `--catalog`, `--comparison-catalog`, `--build-dir`, `--print-revision`, `--refresh-generated-hashes`.

- [ ] **Step 1: Create failing validator self-tests before validation logic**

Create `scripts/verify_wp607c_manifest.py` with argument parsing, these fixed constants and a self-test entry that calls not-yet-implemented validation functions:

```python
REQUIRED_IDS = (
    "ui-gray1-none", "ui-indexed4-trns", "ui-rgb8-five-filters",
    "ui-rgba16-byte-select", "ui-adam7-empty-passes",
    "trace-stored-literals", "trace-fixed-nonoverlap",
    "trace-dynamic-overlap-repeats", "trace-multiblock-bfinal",
    "idat-split-zlib-header", "idat-split-token", "idat-split-adler",
    "error-truncated-header", "error-truncated-token",
    "error-reserved-btype", "error-invalid-distance",
    "error-crc-mismatch", "error-adler-mismatch", "perf-large-rgba8",
)
GENERATED_KEYS = {
    "id", "kind", "expected_class", "expected_features",
    "expected_facts", "linked_tests", "generator", "output",
    "expected_sha256",
}
EXTERNAL_REQUIRED = {
    "id", "kind", "path", "source_url", "upstream_version",
    "upstream_commit", "sha256", "license", "expected_class",
    "expected_features", "linked_tests",
}
```

The self-test must assert that a generated record containing `source_url` is
rejected, an external record without `license` is rejected, duplicate ids are
rejected, `../escape.png` is rejected, a missing required id is rejected, an
unknown key is rejected and unsorted features/tests are rejected.

- [ ] **Step 2: Run the self-test and verify RED**

Run:

```bash
python3 scripts/verify_wp607c_manifest.py --self-test
```

Expected: non-zero with `NameError: name 'validate_manifest' is not defined`
or an equivalent missing-function failure.

- [ ] **Step 3: Implement strict schema validation and deterministic revision calculation**

Implement validation around these signatures:

```python
def validate_manifest(document, catalog, ctest_names):
    errors = []
    if not isinstance(document, list):
        return ["manifest root must be a list"]
    ids = []
    for index, record in enumerate(document):
        prefix = f"record[{index}]"
        if not isinstance(record, dict):
            errors.append(f"{prefix}: must be a mapping")
            continue
        case_id = record.get("id")
        ids.append(case_id)
        kind = record.get("kind")
        if kind not in {"generated", "external"}:
            errors.append(f"{prefix}: kind must be generated or external")
        if kind == "generated":
            errors.extend(validate_generated_record(prefix, record, catalog))
        if kind == "external":
            errors.extend(validate_external_record(prefix, record))
        errors.extend(validate_linked_tests(prefix, record, ctest_names))
    errors.extend(validate_required_ids(ids))
    return sorted(errors)


def aggregate_revision(manifest_bytes, generator_source_bytes):
    digest = hashlib.sha256()
    digest.update(hashlib.sha256(manifest_bytes).digest())
    for source in generator_source_bytes:
        digest.update(hashlib.sha256(source).digest())
    return digest.hexdigest()
```

Generated validation requires exact keys, stable id equality with
`generator.case`, executable `pnga_generate_wp607c_corpus`, schema version 1,
non-empty argument/fact mappings, build-relative output rooted in `valid/` or
`malformed/`, 64 lowercase hex hash, sorted non-empty features/tests and exact
catalog equality. External validation preserves every current provenance field
and rejects placeholder URLs, hashes and licenses.

- [ ] **Step 4: Make the existing shallow verifiers kind-aware**

In both repository scripts, branch on `record.get("kind", "external")`.
Generated records validate id/output/hash shape and forbid upstream fields;
external records retain the current provenance checks. Keep `[]` valid until
Task 5 installs the deep required-id CTest gate.

```python
kind = record.get("kind", "external")
if kind == "generated":
    check_generated_record(rpt, index, record)
elif kind == "external":
    check_external_record(rpt, index, record)
else:
    rpt.error(f"corpus entry {index} has unsupported kind {kind!r}")
```

- [ ] **Step 5: Run schema tests and repository audits**

Run:

```bash
python3 scripts/verify_wp607c_manifest.py --self-test
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
```

Expected: self-test reports all negative/positive cases passed; both repository
audits report 0 failures and 0 warnings.

- [ ] **Step 6: Commit the schema slice**

```bash
git add scripts/verify_wp607c_manifest.py scripts/verify_dependencies.py scripts/verify_repository_layout.py tests/corpus/README.md
git commit -m "test: define wp607c corpus manifest contract"
```

---

### Task 2: Build the independent fixture API and five pixel cases

**Files:**
- Create: `tests/corpus/controlled_fixture.h`
- Create: `tests/corpus/controlled_fixture.cpp`
- Create: `tests/corpus/png_facts_test.cpp`
- Create: `tests/corpus/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: zlib checksum functions and test-only libpng oracle boundaries.
- Produces: namespace `pnga_test::wp607c`, all 19 ids, typed expected facts, `all_controlled_cases()`, `make_controlled_fixture()` and `controlled_case_id()`.

- [ ] **Step 1: Write failing tests for the five valid image cases**

Create `png_facts_test.cpp` using the future fixture API. Freeze these
assertions independently of production output:

```cpp
TEST_CASE("WP-607C pixel cases expose fixed image facts", "[wp607c][corpus]") {
  using namespace pnga_test::wp607c;
  const auto gray = make_controlled_fixture(ControlledCaseId::kUiGray1None);
  REQUIRE(gray.stable_id == "ui-gray1-none");
  REQUIRE(gray.expected.image->width == 9);
  REQUIRE(gray.expected.image->height == 3);
  REQUIRE(gray.expected.image->bit_depth == 1);
  REQUIRE(gray.expected.image->color_type == 0);
  REQUIRE(gray.expected.image->row_filters == std::vector<std::uint8_t>{0,0,0});

  const auto indexed = make_controlled_fixture(ControlledCaseId::kUiIndexed4Trns);
  REQUIRE(indexed.expected.image->bit_depth == 4);
  REQUIRE(indexed.expected.image->color_type == 3);
  REQUIRE(indexed.expected.image->palette_entries.size() == 16);
  REQUIRE(indexed.expected.image->alpha_entries.size() == 16);

  const auto filters = make_controlled_fixture(ControlledCaseId::kUiRgb8FiveFilters);
  REQUIRE(filters.expected.image->row_filters == std::vector<std::uint8_t>{0,1,2,3,4});

  const auto rgba16 = make_controlled_fixture(ControlledCaseId::kUiRgba16ByteSelect);
  REQUIRE(rgba16.expected.image->selected_sample_bytes ==
          std::vector<std::uint8_t>{0x12, 0x34});

  const auto adam7 = make_controlled_fixture(ControlledCaseId::kUiAdam7EmptyPasses);
  REQUIRE(adam7.expected.image->interlace == 1);
  REQUIRE(adam7.expected.image->empty_passes ==
          std::vector<std::uint8_t>{1,2,3,4,6});
}
```

Also assert PNG signature, IHDR values, PLTE/tRNS ordering, valid Chunk CRCs,
exact filtered rows and independently declared delivered pixels.
`empty_passes` uses zero-based Adam7 pass indexes, so `{1,2,3,4,6}` means
specification passes 2, 3, 4, 5 and 7 are empty for the 2x1 case.

- [ ] **Step 2: Register the test target and verify RED**

Add `add_subdirectory(corpus)` immediately before `add_subdirectory(differential)`
in `tests/CMakeLists.txt`. Define the reusable library and first test target:

```cmake
add_library(pnga_test_wp607c_corpus STATIC controlled_fixture.cpp)
target_include_directories(pnga_test_wp607c_corpus PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnga_test_wp607c_corpus PUBLIC ZLIB::ZLIB)
target_compile_features(pnga_test_wp607c_corpus PUBLIC cxx_std_20)

add_executable(pnga_wp607c_png_facts_tests png_facts_test.cpp)
target_link_libraries(pnga_wp607c_png_facts_tests PRIVATE
  pnga_test_wp607c_corpus
  pnga::png_format pnga::validation pnga::png_reconstruction pnga::io
  PNG::PNG ZLIB::ZLIB Catch2::Catch2WithMain)
add_test(NAME wp607c_png_facts_tests COMMAND pnga_wp607c_png_facts_tests)
set_tests_properties(wp607c_png_facts_tests PROPERTIES
  LABELS "corpus;wp607c")
```

Run:

```bash
cmake --preset dev
cmake --build --preset dev --target pnga_wp607c_png_facts_tests --parallel 4
```

Expected: compilation fails because `controlled_fixture.h` and its symbols are
not implemented.

- [ ] **Step 3: Define the complete typed test-side contract**

Define all ids and fact types in `controlled_fixture.h`:

```cpp
enum class ControlledCaseId {
  kUiGray1None, kUiIndexed4Trns, kUiRgb8FiveFilters,
  kUiRgba16ByteSelect, kUiAdam7EmptyPasses,
  kTraceStoredLiterals, kTraceFixedNonoverlap,
  kTraceDynamicOverlapRepeats, kTraceMultiblockBfinal,
  kIdatSplitZlibHeader, kIdatSplitToken, kIdatSplitAdler,
  kErrorTruncatedHeader, kErrorTruncatedToken, kErrorReservedBtype,
  kErrorInvalidDistance, kErrorCrcMismatch, kErrorAdlerMismatch,
  kPerfLargeRgba8,
};

enum class BlockKind : std::uint8_t { kStored, kFixed, kDynamic, kReserved };
enum class TokenKind : std::uint8_t { kLiteral, kMatch, kEndOfBlock };

struct ByteRangeFact { std::uint64_t begin; std::uint64_t end; };
struct BlockFact { BlockKind kind; bool bfinal; ByteRangeFact input_bits; ByteRangeFact output_bytes; };
struct TokenFact {
  TokenKind kind;
  ByteRangeFact input_bits;
  ByteRangeFact output_bytes;
  std::optional<std::uint8_t> literal;
  std::optional<std::uint16_t> length;
  std::optional<std::uint16_t> distance;
  std::optional<ByteRangeFact> match_source;
};
struct ImageFacts {
  std::uint32_t width;
  std::uint32_t height;
  std::uint8_t bit_depth;
  std::uint8_t color_type;
  std::uint8_t interlace;
  std::vector<std::uint8_t> row_filters;
  std::vector<std::array<std::uint8_t, 3>> palette_entries;
  std::vector<std::uint8_t> alpha_entries;
  std::vector<std::uint8_t> selected_sample_bytes;
  std::vector<std::uint8_t> empty_passes;
};
struct ExpectedFacts {
  std::optional<ImageFacts> image;
  std::vector<BlockFact> blocks;
  std::vector<TokenFact> tokens;
  std::vector<ByteRangeFact> physical_spans;
  std::optional<std::string> error;
  std::optional<std::uint64_t> stop_input_bit;
  std::optional<std::uint64_t> stop_output_byte;
};
struct ControlledFixture {
  ControlledCaseId id;
  std::string_view stable_id;
  std::vector<std::byte> png_bytes;
  ExpectedFacts expected;
};

std::span<const ControlledCaseId> all_controlled_cases() noexcept;
ControlledFixture make_controlled_fixture(ControlledCaseId id);
std::optional<ControlledCaseId> controlled_case_id(std::string_view stable_id);
```

Add equality operators needed by tests. Keep every type independent of
production enums so expected facts cannot mirror production objects by
construction.

- [ ] **Step 4: Implement checked PNG and deterministic Stored-DEFLATE builders**

In `controlled_fixture.cpp`, add private checked append helpers, big-endian
integer writers, `push_chunk(type, data)`, a forward-filter encoder, Adam7
geometry and a Stored-Block zlib wrapper. Use fixed source pixels declared by
each case. Reject size conversions before vector growth.

```cpp
std::optional<std::size_t> checked_size(std::uint64_t value);
bool checked_append_size(std::size_t current, std::size_t addition,
                         std::size_t& result);
void append_u32_be(std::vector<std::byte>& out, std::uint32_t value);
void push_chunk(std::vector<std::byte>& png, std::string_view type,
                std::span<const std::byte> data);
std::vector<std::byte> make_stored_zlib(
    std::span<const std::byte> filtered_bytes);
```

The `switch` in `make_controlled_fixture()` must implement the five pixel ids
now and throw `std::invalid_argument("WP-607C case is not implemented")` for
the remaining ids until their owning task adds them. `all_controlled_cases()`
still returns all 19 ids so missing cases remain visible.

- [ ] **Step 5: Run the pixel gate**

```bash
cmake --build --preset dev --target pnga_wp607c_png_facts_tests --parallel 4
ctest --preset dev -R '^wp607c_png_facts_tests$' --output-on-failure
```

Expected: PASS with the five fixed image cases and no filesystem writes.

- [ ] **Step 6: Commit the pixel slice**

```bash
git add tests/CMakeLists.txt tests/corpus/CMakeLists.txt tests/corpus/controlled_fixture.h tests/corpus/controlled_fixture.cpp tests/corpus/png_facts_test.cpp
git commit -m "test: add deterministic wp607c pixel fixtures"
```

---

### Task 3: Add four exact valid DEFLATE/Trace cases

**Files:**
- Modify: `tests/corpus/controlled_fixture.cpp`
- Create: `tests/corpus/trace_facts_test.cpp`
- Modify: `tests/corpus/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 `ControlledFixture` and expected Block/token types.
- Produces: implemented ids `trace-stored-literals`, `trace-fixed-nonoverlap`, `trace-dynamic-overlap-repeats`, `trace-multiblock-bfinal` and CTest `wp607c_trace_facts_tests`.

- [ ] **Step 1: Write exact failing Block and token tests**

Use `index_blocks()` and `decode_stored_and_fixed()` over each fixture's
virtual IDAT stream. Compare production results field-by-field to the
independent expected vectors. Add these discriminating assertions:

```cpp
REQUIRE(stored.expected.blocks.size() == 1);
REQUIRE(stored.expected.blocks[0].kind == BlockKind::kStored);
REQUIRE(stored.expected.blocks[0].bfinal);

const auto& nonoverlap = fixed.expected.tokens.at(3);
REQUIRE(nonoverlap.kind == TokenKind::kMatch);
REQUIRE(*nonoverlap.distance >= *nonoverlap.length);

const auto& overlap = dynamic.expected.tokens.at(2);
REQUIRE(overlap.kind == TokenKind::kMatch);
REQUIRE(*overlap.distance == 1);
REQUIRE(dynamic.expected_code_length_repeats ==
        std::vector<std::uint8_t>{16,17,18});

REQUIRE(multiblock.expected.blocks.size() == 3);
REQUIRE_FALSE(multiblock.expected.blocks[0].bfinal);
REQUIRE_FALSE(multiblock.expected.blocks[1].bfinal);
REQUIRE(multiblock.expected.blocks[2].bfinal);
```

Add `expected_code_length_repeats` to `ExpectedFacts` as a vector of the
encoded repeat symbols; parse those bits with a test-side reader in this test
instead of asking production code to expose a new API.

- [ ] **Step 2: Register and run the Trace test to verify RED**

Define `pnga_wp607c_trace_facts_tests`, linked to the fixture library,
`pnga::deflate_index`, `pnga::deflate_trace`, `pnga::png_format`, `pnga::io`,
`ZLIB::ZLIB` and `Catch2::Catch2WithMain`.

Run:

```bash
cmake --build --preset dev --target pnga_wp607c_trace_facts_tests --parallel 4
```

Expected: FAIL because the four case ids still throw the Task 2 not-implemented
exception.

- [ ] **Step 3: Implement the LSB-first bit writer and fixed-code encoder**

Add private `BitWriter::write_lsb(value, count)`, byte alignment and canonical
code reversal. Implement RFC 1951 fixed literal/length and distance code lookup
with explicit range checks. Construct `ABCABC` using literals A/B/C plus a
length-3, distance-3 Match so its source `[0,3)` does not overlap target
`[3,6)`.

```cpp
class BitWriter {
 public:
  std::uint64_t bit_position() const noexcept;
  void write_lsb(std::uint32_t value, unsigned count);
  void write_canonical(std::uint32_t code, unsigned count);
  void align_to_byte();
  std::span<const std::byte> bytes() const noexcept;
 private:
  std::vector<std::byte> bytes_;
  std::uint64_t bit_position_ = 0;
};

struct HuffmanCode { std::uint16_t canonical; std::uint8_t length; };
HuffmanCode fixed_literal_length_code(std::uint16_t symbol);
HuffmanCode fixed_distance_code(std::uint8_t symbol);
```

- [ ] **Step 4: Implement the Dynamic and three-Block streams**

Hand-write a complete Dynamic header whose code-length instruction sequence
contains 16, 17 and 18, then emit literals `A`, `A`, a distance-1 overlapping
Match and EOB so the Match is expected token index 2. Hand-write the
multi-Block stream in exact order Stored, Fixed,
Dynamic, with BFINAL bits false, false, true. Store bit positions immediately
before and after each emitted field so expected ranges are derived from the
writer cursor, not production output.

```cpp
ControlledFixture make_trace_dynamic_overlap_repeats() {
  BitWriter writer;
  write_zlib_header(writer);
  write_dynamic_header_with_repeats(writer, {16, 17, 18});
  write_dynamic_literal(writer, 0x41);
  write_dynamic_literal(writer, 0x41);
  write_dynamic_match(writer, 6, 1);
  write_dynamic_end_of_block(writer);
  return finish_png_case(ControlledCaseId::kTraceDynamicOverlapRepeats,
                         writer, std::vector<std::byte>(8, std::byte{0x41}));
}
```

- [ ] **Step 5: Run focused and existing Deflate gates**

```bash
cmake --build --preset dev --target pnga_wp607c_trace_facts_tests pnga_deflate_index_tests pnga_deflate_trace_tests --parallel 4
ctest --preset dev -R '^(wp607c_trace_facts_tests|deflate_index_block_index_tests|deflate_trace_zlib_wrapper_tests)$' --output-on-failure
```

Expected: all three CTest entries PASS; exact blocks tile output and token
output equals independently declared raw bytes.

- [ ] **Step 6: Commit the valid Trace slice**

```bash
git add tests/corpus/controlled_fixture.h tests/corpus/controlled_fixture.cpp tests/corpus/trace_facts_test.cpp tests/corpus/CMakeLists.txt
git commit -m "test: add exact wp607c deflate fixtures"
```

---

### Task 4: Add cross-IDAT and malformed corpus cases

**Files:**
- Modify: `tests/corpus/controlled_fixture.h`
- Modify: `tests/corpus/controlled_fixture.cpp`
- Modify: `tests/corpus/trace_facts_test.cpp`

**Interfaces:**
- Consumes: Task 3 explicit streams and token/block expected facts.
- Produces: three split ids, six malformed ids, typed `ErrorFacts`, and exact physical-span/stop assertions.

- [ ] **Step 1: Write failing cross-IDAT mapping tests**

For header split, require IDAT payload lengths `{1, remaining}` and two file
spans covering CMF/FLG. For token split, identify one expected token whose
compressed byte coverage crosses the split and require two ordered file spans.
For Adler split, require a 2+2 trailer mapping and a matching checksum.

Use `VirtualIDATStream::logical_to_physical()` and assert every returned range
equals `fixture.expected.physical_spans`; do not accept only a span count.

- [ ] **Step 2: Write failing malformed tests**

Add exact assertions against current stable diagnostics:

```cpp
REQUIRE(truncated_header.expected.error->decoder_message ==
        "truncated block header");
REQUIRE(truncated_token.expected.error->decoder_message ==
        "truncated huffman code");
REQUIRE(reserved.expected.error->decoder_message ==
        "reserved deflate block type");
REQUIRE(reserved.expected.error->stop_input_bit == 19);
REQUIRE(reserved.expected.error->stop_output_byte == 0);
REQUIRE(invalid_distance.expected.error->decoder_message ==
        "distance beyond available output");
REQUIRE(crc.expected.error->validation_rule_id == "chunk_crc_mismatch");
REQUIRE(adler.expected.error->validation_rule_id == "idat_adler_mismatch");
```

Replace the three Task 2 error/stop members with this single interface:

```cpp
struct ErrorFacts {
  std::optional<std::string> decoder_message;
  std::optional<std::string> validation_rule_id;
  std::optional<std::uint64_t> stop_input_bit;
  std::optional<std::uint64_t> stop_output_byte;
};

struct ExpectedFacts {
  std::optional<ImageFacts> image;
  std::vector<BlockFact> blocks;
  std::vector<TokenFact> tokens;
  std::vector<ByteRangeFact> physical_spans;
  std::vector<std::uint8_t> expected_code_length_repeats;
  std::optional<ErrorFacts> error;
};
```

- [ ] **Step 3: Run the Trace test and verify RED**

```bash
cmake --build --preset dev --target pnga_wp607c_trace_facts_tests --parallel 4
ctest --preset dev -R '^wp607c_trace_facts_tests$' --output-on-failure
```

Expected: FAIL because all nine ids still use the not-implemented branch.

- [ ] **Step 4: Implement precise split and mutation builders**

Wrap existing valid streams into multiple IDAT chunks at the three specified
logical offsets and recompute every affected Chunk CRC. Implement malformed
cases by mutating exactly one property:

- cut the Block header after two of its three bits;
- cut a Fixed Huffman code before its final bit;
- encode BTYPE bits `11` after a valid zlib header;
- encode a length followed by distance 2 when only one output byte exists;
- flip one bit in the stored IDAT CRC without changing payload;
- flip one Adler trailer bit, then recompute only the enclosing IDAT CRC.

Record the bit cursor and output cursor at each failure while building the
stream. The builder must not call production parsing to discover expected stop
positions.

```cpp
ControlledFixture split_idat(ControlledFixture base,
                             std::span<const std::uint64_t> logical_splits);
ControlledFixture truncate_deflate_header(ControlledFixture base,
                                          std::uint64_t keep_bits);
ControlledFixture truncate_huffman_token(ControlledFixture base,
                                         std::uint64_t keep_bits);
ControlledFixture make_reserved_btype();
ControlledFixture make_invalid_distance();
ControlledFixture corrupt_idat_crc(ControlledFixture base);
ControlledFixture corrupt_adler_and_repair_crc(ControlledFixture base);
```

- [ ] **Step 5: Verify all boundary/error facts**

```bash
cmake --build --preset dev --target pnga_wp607c_trace_facts_tests --parallel 4
ctest --preset dev -R '^(wp607c_trace_facts_tests|validation_structural_tests|deflate_index_block_index_tests|deflate_trace_zlib_wrapper_tests)$' --output-on-failure
```

Expected: PASS. CRC case has only `chunk_crc_mismatch`; Adler case has only
`idat_adler_mismatch`; verified prefixes and exact stops match the registry.

- [ ] **Step 6: Commit the boundary/error slice**

```bash
git add tests/corpus/controlled_fixture.h tests/corpus/controlled_fixture.cpp tests/corpus/trace_facts_test.cpp
git commit -m "test: add wp607c split and malformed fixtures"
```

---

### Task 5: Materialize the corpus, hashes and manifest

**Files:**
- Create: `tests/corpus/generate_controlled_corpus.cpp`
- Modify: `tests/corpus/CMakeLists.txt`
- Modify: `tests/corpus/manifest.yaml`
- Modify: `scripts/verify_wp607c_manifest.py`

**Interfaces:**
- Consumes: all 19 Task 2-4 registry cases.
- Produces: `pnga_generate_wp607c_corpus --output OUTPUT_DIR`, CTest setup `wp607c_generate_corpus`, `${CMAKE_BINARY_DIR}/tests/corpus/wp-607c/index.json`, completed manifest hashes/facts and CTest `wp607c_manifest_tests`.

- [ ] **Step 1: Add a failing generator/catalog contract to the Python self-test**

Add a temporary-directory test that invokes the future generator twice, loads
both `index.json` files, requires 19 sorted records, compares every PNG byte and
checks every catalog hash with `hashlib.sha256(path.read_bytes()).hexdigest()`.
Also require the generator to reject a destination resolving inside source
`tests/corpus/`.

- [ ] **Step 2: Register the missing generator and verify RED**

Add the generator target and setup CTest command:

```cmake
add_executable(pnga_generate_wp607c_corpus generate_controlled_corpus.cpp)
target_link_libraries(pnga_generate_wp607c_corpus PRIVATE pnga_test_wp607c_corpus)
add_test(NAME wp607c_generate_corpus
  COMMAND pnga_generate_wp607c_corpus
          --output ${CMAKE_BINARY_DIR}/tests/corpus/wp-607c)
set_tests_properties(wp607c_generate_corpus PROPERTIES
  FIXTURES_SETUP wp607c-generated-corpus
  LABELS "corpus;wp607c")
```

Run the self-test. Expected: FAIL because the target source and CLI do not yet
exist.

- [ ] **Step 3: Implement safe generation and SHA-256**

In `generate_controlled_corpus.cpp`, implement:

```cpp
std::string sha256_hex(std::span<const std::byte> bytes);
bool is_inside(const std::filesystem::path& child,
               const std::filesystem::path& parent);
void write_atomic_catalog(const std::filesystem::path& output);
```

Implement SHA-256 directly from FIPS 180-4 in this test-only file using
32-bit rotate/add operations, the 64 fixed round constants and checked message
padding. Validate it indirectly for every output against Python `hashlib`, and
directly expose `--sha256-text abc` only in the executable's test CLI so the
self-test requires the standard digest
`ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.

Resolve output/source paths canonically, reject source-tree destinations and
path traversal, write to `OUTPUT_DIR.new`, close every file, rename the old
complete directory to `OUTPUT_DIR.old`, rename `.new` into place, then remove
`.old`. If the second rename fails, restore `.old`; on any earlier failure,
remove only `.new` and retain the last complete output.

- [ ] **Step 4: Define and propagate one corpus revision**

In `tests/corpus/CMakeLists.txt`, add `CMAKE_CONFIGURE_DEPENDS` for the manifest
and both generator sources. Compute manifest/source hashes with `file(SHA256)`
and the aggregate with `string(SHA256)` over
`manifest_sha:controlled_fixture_sha:generator_sha`. Pass it as
`PNGA_WP607C_CORPUS_REVISION` to the fixture and generator targets. Emit the
same 64-hex value in `index.json`.

- [ ] **Step 5: Populate all 19 manifest records and bless generated hashes**

For every required id, record `kind: generated`, the fixed arguments from the
approved package, exact expected facts, real linked CTest names and output
under `valid/` or `malformed/`. Initially generate into two fresh build
directories, verify equality, then run:

```bash
python3 scripts/verify_wp607c_manifest.py \
  --manifest tests/corpus/manifest.yaml \
  --catalog build/dev/tests/corpus/wp-607c/index.json \
  --comparison-catalog build/wp607c-double-generation/run-b/index.json \
  --build-dir build/dev \
  --refresh-generated-hashes
```

This mode updates only `expected_sha256` scalar values in stable id order and
then immediately performs normal read-only validation. It refuses to write if
the two-generation comparison was not supplied and successful.

- [ ] **Step 6: Add and run the manifest CTest gate**

Register `wp607c_manifest_tests` to invoke the validator against the source
manifest, generated index and build directory. Set
`FIXTURES_REQUIRED wp607c-generated-corpus` and labels `corpus;wp607c`.

Run:

```bash
cmake --preset dev
cmake --build --preset dev --target pnga_generate_wp607c_corpus pnga_wp607c_png_facts_tests pnga_wp607c_trace_facts_tests --parallel 4
ctest --preset dev -R '^wp607c_(generate_corpus|manifest_tests|png_facts_tests|trace_facts_tests)$' --output-on-failure
```

Expected: all four entries PASS, 19 files exist only in the build tree and
every manifest/catalog/file hash agrees.

- [ ] **Step 7: Commit the materialized-corpus contract**

```bash
git add tests/corpus/generate_controlled_corpus.cpp tests/corpus/CMakeLists.txt tests/corpus/manifest.yaml scripts/verify_wp607c_manifest.py
git commit -m "test: materialize audited wp607c corpus"
```

---

### Task 6: Drive the real GUI with controlled files

**Files:**
- Create: `tests/gui/controlled_corpus_gate_test.cpp`
- Modify: `tests/gui/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 5 generated output directory and case ids.
- Produces: target `pnga_gui_wp607c_corpus_tests`, CTest `gui_wp607c_corpus_tests`, real-file state/width/error assertions and labels `corpus;gui;wp607c`.

- [ ] **Step 1: Write the failing real-file Qt test**

Compile `PNGA_WP607C_CORPUS_DIR` as the generated output path. Open
`ui-rgb8-five-filters`, `trace-stored-literals`,
`trace-dynamic-overlap-repeats`, `error-truncated-token` and
`error-reserved-btype` through `MainWindow::openFile()`.

The test must assert:

- valid pixel image appears and Compression context reaches `ready`;
- Stored selects a Stored Block and Huffman shows the no-Huffman explanation;
- Dynamic shows Blocks, Huffman and Decode Trace with a Match and EOB;
- malformed cases keep verified rows and show stable Partial/Error copy;
- widths 320/360/480/600 do not increase inspector minimum width or overlap
  footer/details;
- dark widths 360/480 preserve required columns and visible selection;
- row selection and navigation do not add Deep Trace submissions beyond the
  explicit open action.

- [ ] **Step 2: Register the target and verify RED**

Build it with `${PNGA_GUI_APP_TEST_SOURCES}`, `AUTOMOC`, the application-source
include path and the same libraries as `pnga_gui_trace_pipeline_integration_tests`.
Set `FIXTURES_REQUIRED wp607c-generated-corpus` and
`ENVIRONMENT QT_QPA_PLATFORM=offscreen`.

Run:

```bash
cmake --build --preset dev --target pnga_gui_wp607c_corpus_tests --parallel 4
ctest --preset dev -R '^gui_wp607c_corpus_tests$' --output-on-failure
```

Expected: FAIL on the first missing/incomplete real-file assertion.

- [ ] **Step 3: Complete only test wiring and fixture expectations**

Use existing object names from `compression_inspector_responsive_test.cpp` and
`trace_pipeline_integration_test.cpp`; do not add application hooks. Wait only
for documented asynchronous state transitions with bounded QTRY timeouts.
Where a controlled case exposes a production defect, stop with the required
`BLOCKED` status instead of changing production files.

```cpp
QString fixturePath(QStringView relative) {
  return QDir(QString::fromUtf8(PNGA_WP607C_CORPUS_DIR))
      .filePath(relative.toString());
}

void openAndWaitReady(MainWindow& window, QStringView relative) {
  QVERIFY(window.openFile(fixturePath(relative)));
  auto* status = window.findChild<QLabel*>("compressionContextStatus");
  QVERIFY(status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("ready"), 5000);
}
```

- [ ] **Step 4: Run focused GUI regression gates**

```bash
cmake --build --preset dev --target pnga_gui_wp607c_corpus_tests pnga_gui_trace_pipeline_integration_tests pnga_gui_compression_inspector_responsive_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R '^(gui_wp607c_corpus_tests|gui_trace_pipeline_integration_tests|gui_compression_inspector_responsive_tests)$' --output-on-failure
```

Expected: all three CTest entries PASS with no new production diff.

- [ ] **Step 5: Commit the GUI consumer**

```bash
git add tests/gui/controlled_corpus_gate_test.cpp tests/gui/CMakeLists.txt
git commit -m "test: gate gui with wp607c corpus"
```

---

### Task 7: Reuse controlled cases in differential, Deflate and performance gates

**Files:**
- Modify: `tests/differential/differential_harness_test.cpp`
- Modify: `tests/differential/CMakeLists.txt`
- Modify: `tests/unit/deflate-index/block_index_test.cpp`
- Modify: `tests/unit/deflate-index/CMakeLists.txt`
- Modify: `tests/unit/deflate-trace/token_decoder_test.cpp`
- Modify: `tests/unit/deflate-trace/CMakeLists.txt`
- Modify: `tests/performance/performance_runner.cpp`
- Modify: `tests/performance/CMakeLists.txt`
- Modify: `scripts/run_performance_corpus.py`

**Interfaces:**
- Consumes: Task 5 registry and compile-time `PNGA_WP607C_CORPUS_REVISION`.
- Produces: five controlled libpng comparisons, duplicate-builder migration, shared `perf-large-rgba8`, and performance JSON field `corpus_revision`.

- [ ] **Step 1: Add failing differential and performance assertions**

Add a differential section that loops exactly the five `ui-*` ids and requires
`dimensions_match`, `target_matches`, `native_matches` and no first difference.
In performance tests require:

```cpp
REQUIRE(measurement.at("corpus") == "wp607c-static-v1");
REQUIRE(measurement.at("corpus_revision").size() == 64);
REQUIRE(measurement.at("large_case") == "perf-large-rgba8");
```

In the Python wrapper require the runner revision to equal
`verify_wp607c_manifest.py --print-revision` before writing its record.

- [ ] **Step 2: Link the shared fixture target and verify RED**

Link `pnga_test_wp607c_corpus` into differential, Deflate-index, Deflate-trace
and performance test targets. Add fixture setup requirements where a test reads
files. Run the four focused targets; expected failure is missing controlled
case consumption/revision fields, not a linker-order error.

- [ ] **Step 3: Replace only exact duplicate builders**

Replace local Stored/Fixed/Dynamic/two-IDAT builders only where the same exact
case now exists. Retain local builders for tests with different sizes,
strategies or fault shapes. Preserve every old assertion; add the controlled
facts as stronger assertions rather than replacing broad fuzz/matrix coverage.

```cpp
const auto fixture = pnga_test::wp607c::make_controlled_fixture(
    pnga_test::wp607c::ControlledCaseId::kTraceStoredLiterals);
pnga::io::MemoryByteSource source(fixture.png_bytes);
const auto chunks = pnga::png_format::index_chunks(source);
const pnga::png_format::VirtualIDATStream stream(chunks);
```

- [ ] **Step 4: Move the performance runner to the shared large case**

Construct `perf-large-rgba8` through `make_controlled_fixture()`, derive the
production `ImageHeader` from independent `ImageFacts`, and keep the existing
64 random-row formula, 16 pixel queries and threshold metric names unchanged.
Emit the revision using the compile-time definition rather than a literal
sample value:

```cpp
record["corpus"] = "wp607c-static-v1";
record["corpus_revision"] = std::string(PNGA_WP607C_CORPUS_REVISION);
record["large_case"] = "perf-large-rgba8";
```

- [ ] **Step 5: Run differential, Deflate and performance gates**

```bash
cmake --preset dev
cmake --build --preset dev --target pnga_differential_tests pnga_deflate_index_tests pnga_deflate_trace_tests pnga_performance_runner --parallel 4
ctest --preset dev -R '^(differential_harness_tests|deflate_index_block_index_tests|deflate_trace_zlib_wrapper_tests|performance_corpus_runner)$' --output-on-failure
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds
```

Expected: all CTest entries and the fixed threshold gate PASS; the written
performance record carries the same corpus revision as the manifest verifier.

- [ ] **Step 6: Commit shared consumer integration**

```bash
git add tests/differential tests/unit/deflate-index tests/unit/deflate-trace tests/performance scripts/run_performance_corpus.py
git commit -m "test: share wp607c corpus across quality gates"
```

---

### Task 8: Add the operator gate, run clean verification and record handoff

**Files:**
- Create: `scripts/run_wp607c_corpus_gate.py`
- Modify: `tests/corpus/README.md`
- Modify: `docs/development/wp-607c-controlled-static-ui-trace-corpus.md`
- Modify: `docs/development/wp-607-cross-platform-quality-evidence.md`

**Interfaces:**
- Consumes: Tasks 1-7, labels `wp607c`, generator CLI, manifest verifier and CMake presets.
- Produces: `python3 scripts/run_wp607c_corpus_gate.py --preset dev --jobs 4`, `build/evidence/wp-607c-corpus.json`, final aggregate revision and WP-5U12F preflight handoff.

- [ ] **Step 1: Write a failing wrapper dry-run test**

Implement `--dry-run` first so it prints the exact command sequence without
executing it. Add a self-check requiring this order:

```text
cmake --build --preset dev --target pnga_generate_wp607c_corpus pnga_wp607c_png_facts_tests pnga_wp607c_trace_facts_tests pnga_gui_wp607c_corpus_tests --parallel 4
pnga_generate_wp607c_corpus --output build/wp607c-double-generation/run-a
pnga_generate_wp607c_corpus --output build/wp607c-double-generation/run-b
verify_wp607c_manifest.py --manifest tests/corpus/manifest.yaml --catalog build/wp607c-double-generation/run-a/index.json --comparison-catalog build/wp607c-double-generation/run-b/index.json --build-dir build/dev
ctest --preset dev -L wp607c --output-on-failure
```

Run `python3 scripts/run_wp607c_corpus_gate.py --self-test`. Expected: FAIL
until the command planner and evidence writer are implemented.

- [ ] **Step 2: Implement the gate and deterministic evidence record**

Use `argparse`, `subprocess.run(check=True)`, `tempfile.TemporaryDirectory`
under `build/wp607c-double-generation/`, `hashlib`, `json`, `platform` and UTC
timestamps. Compare sorted relative paths, bytes, catalog facts and hashes
between both generations before running CTest.

```python
def run(command, *, capture=False):
    return subprocess.run(
        command, cwd=ROOT, check=True, text=True,
        capture_output=capture,
    )


def compare_generations(first, second):
    left = load_catalog(first / "index.json")
    right = load_catalog(second / "index.json")
    if left != right:
        raise SystemExit("WP-607C catalogs differ")
    for record in left["cases"]:
        relative = Path(record["output"])
        if (first / relative).read_bytes() != (second / relative).read_bytes():
            raise SystemExit(f"WP-607C bytes differ: {relative.as_posix()}")
    return left
```

Write `build/evidence/wp-607c-corpus.json` atomically with:

- `schema_version = 1`, `work_package = "WP-607C"`, status;
- commit, UTC time, OS/build, architecture, compiler, Qt, display protocol,
  logical DPI, device-pixel ratio, CPU, memory and preset;
- manifest SHA-256, generator-source SHA-256 and corpus revision;
- exact command list and exit codes;
- 19 sorted case ids, relative outputs and SHA-256 values;
- owning CTest names and results.

Use compact sorted-key JSON with ASCII escaping and one trailing LF. Absolute
paths and volatile temporary directory names are forbidden in the record.

- [ ] **Step 3: Verify the wrapper from the current build**

```bash
python3 scripts/run_wp607c_corpus_gate.py --self-test
python3 scripts/run_wp607c_corpus_gate.py --preset dev --jobs 4
```

Expected: PASS, 19 equal cases, all `wp607c` CTests passed, and a hash-valid
evidence record under `build/evidence/`.

- [ ] **Step 4: Run the full approved verification matrix**

```bash
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
python3 scripts/run_wp607c_corpus_gate.py --preset dev --jobs 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure -j 4
python3 scripts/run_sanitizer_fuzz_gate.py
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds
git diff --check
```

Expected: every command exits 0. The CTest total increases from 47 by the new
corpus setup, manifest, PNG facts, Trace facts and GUI entries; report the
actual discovered total rather than pre-asserting a number.

- [ ] **Step 5: Repeat the corpus gate from a fresh build directory**

Configure a fresh preset-compatible build directory without reusing generated
corpus output, then run the generator, manifest and labeled CTest gates. Verify
the aggregate revision and all 19 file hashes equal the primary `dev` run.

```bash
cmake --preset dev -B build/wp607c-fresh
cmake --build build/wp607c-fresh --parallel 4
QT_QPA_PLATFORM=offscreen ctest --test-dir build/wp607c-fresh -L wp607c --output-on-failure
python3 scripts/verify_wp607c_manifest.py --manifest tests/corpus/manifest.yaml --catalog build/wp607c-fresh/tests/corpus/wp-607c/index.json --build-dir build/wp607c-fresh --print-revision
```

- [ ] **Step 6: Review changed paths and source-tree cleanliness**

```bash
git diff --name-only 7d30e3c..HEAD
git status --short
git ls-files tests/corpus | sort
```

Expected: every changed path appears in this plan's file map; no PNG/index/
evidence build artifact is tracked; only source manifest/docs/scripts/tests are
present under `tests/corpus`.

- [ ] **Step 7: Record PASS and the WP-5U12F handoff**

Only after Steps 3-6 pass, change the package status to `PASS`, record the
implementation commit range, aggregate revision, evidence-record SHA-256 and
exact command results. In the parent WP-607 document mark only WP-607C PASS;
leave WP-607A/B/D and overall WP-607 incomplete. State that WP-5U12F must rerun
the corpus gate as its first preflight.

- [ ] **Step 8: Commit final gate and evidence references**

```bash
git add scripts/run_wp607c_corpus_gate.py tests/corpus/README.md docs/development/wp-607c-controlled-static-ui-trace-corpus.md docs/development/wp-607-cross-platform-quality-evidence.md
git commit -m "docs: record wp607c corpus gate handoff"
```

Do not add `build/evidence/wp-607c-corpus.json`; record only its SHA-256 and
reproduction command in tracked documentation.

---

## Final Review Checklist

- Every one of the 19 ids has fixed arguments, exact facts, a 64-hex hash and at least one real owning CTest.
- Pixel cases cover grayscale1, indexed4+PLTE/tRNS, RGB8 five filters, RGBA16 byte selection and Adam7 empty passes.
- Trace cases cover Stored, Fixed, Dynamic, literal, non-overlap/overlap Match, repeats 16/17/18, multi-Block and BFINAL.
- Cross-IDAT cases split the zlib header, a token and Adler independently and retain every physical span.
- Malformed cases isolate truncated header/token, reserved BTYPE, invalid distance, CRC mismatch and Adler mismatch with exact stops.
- GUI uses real generated files at the required light/dark widths without production changes.
- Performance uses `perf-large-rgba8` and reports the same aggregate revision.
- Two fresh generations are byte-identical; no generated output is tracked.
- Static audits, full CTest, sanitizer/fuzz, performance threshold and diff checks all pass before PASS is recorded.
