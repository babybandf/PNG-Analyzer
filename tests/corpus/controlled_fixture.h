#ifndef PNGA_TEST_WP607C_CONTROLLED_FIXTURE_H
#define PNGA_TEST_WP607C_CONTROLLED_FIXTURE_H

// WP-607C: independent test-side controlled-corpus registry (package §5, §11).
// Every type here is owned by the corpus tests and deliberately independent of
// production enums, so expected facts cannot mirror production objects by
// construction. The fixture factory writes explicit PNG chunks and DEFLATE
// bitstreams; it never calls production parsers or decoders.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pnga_test::wp607c {

// The 19 frozen case ids (package §7). Ids arrive in implementation order;
// `all_controlled_cases()` always reports every id so missing cases stay
// visible.
enum class ControlledCaseId {
  kUiGray1None,
  kUiIndexed4Trns,
  kUiRgb8FiveFilters,
  kUiRgba16ByteSelect,
  kUiAdam7EmptyPasses,
  kTraceStoredLiterals,
  kTraceFixedNonoverlap,
  kTraceDynamicOverlapRepeats,
  kTraceMultiblockBfinal,
  kIdatSplitZlibHeader,
  kIdatSplitToken,
  kIdatSplitAdler,
  kErrorTruncatedHeader,
  kErrorTruncatedToken,
  kErrorReservedBtype,
  kErrorInvalidDistance,
  kErrorCrcMismatch,
  kErrorAdlerMismatch,
  kPerfLargeRgba8,
};

enum class BlockKind : std::uint8_t { kStored, kFixed, kDynamic, kReserved };
enum class TokenKind : std::uint8_t { kLiteral, kMatch, kEndOfBlock };

// Half-open byte range [begin, end).
struct ByteRangeFact {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
  friend bool operator==(const ByteRangeFact&, const ByteRangeFact&) = default;
};

struct BlockFact {
  BlockKind kind = BlockKind::kStored;
  bool bfinal = false;
  ByteRangeFact input_bits;
  ByteRangeFact output_bytes;
  friend bool operator==(const BlockFact&, const BlockFact&) = default;
};

struct TokenFact {
  TokenKind kind = TokenKind::kLiteral;
  ByteRangeFact input_bits;
  ByteRangeFact output_bytes;
  std::optional<std::uint8_t> literal;
  std::optional<std::uint16_t> length;
  std::optional<std::uint16_t> distance;
  std::optional<ByteRangeFact> match_source;
  friend bool operator==(const TokenFact&, const TokenFact&) = default;
};

struct ImageFacts {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint8_t bit_depth = 0;
  std::uint8_t color_type = 0;
  std::uint8_t interlace = 0;
  std::vector<std::uint8_t> row_filters;
  std::vector<std::array<std::uint8_t, 3>> palette_entries;
  std::vector<std::uint8_t> alpha_entries;
  std::vector<std::uint8_t> selected_sample_bytes;
  std::vector<std::uint8_t> empty_passes;
  friend bool operator==(const ImageFacts&, const ImageFacts&) = default;
};

struct ExpectedFacts {
  std::optional<ImageFacts> image;
  std::vector<BlockFact> blocks;
  std::vector<TokenFact> tokens;
  std::vector<ByteRangeFact> physical_spans;
  std::optional<std::string> error;
  std::optional<std::uint64_t> stop_input_bit;
  std::optional<std::uint64_t> stop_output_byte;
  friend bool operator==(const ExpectedFacts&, const ExpectedFacts&) = default;
};

struct ControlledFixture {
  ControlledCaseId id{};
  std::string_view stable_id;
  std::vector<std::byte> png_bytes;
  ExpectedFacts expected;
  friend bool operator==(const ControlledFixture&,
                         const ControlledFixture&) = default;
};

// All 19 case ids in stable registry order.
std::span<const ControlledCaseId> all_controlled_cases() noexcept;

// Builds the complete in-memory fixture for `id`.
// Throws std::invalid_argument("WP-607C case is not implemented") for ids
// whose owning task has not implemented them yet.
ControlledFixture make_controlled_fixture(ControlledCaseId id);

// Resolves a stable id (e.g. "ui-gray1-none") to its registry entry.
std::optional<ControlledCaseId> controlled_case_id(std::string_view stable_id);

}  // namespace pnga_test::wp607c

#endif  // PNGA_TEST_WP607C_CONTROLLED_FIXTURE_H
