#ifndef PNGA_STATISTICS_STATISTICS_H
#define PNGA_STATISTICS_STATISTICS_H

// WP-602A/602B: bounded, Qt-free statistics aggregation with per-section
// state. The engine consumes backend-neutral samples so callers adapt
// Chunk/PNG filter/Deflate models at the composition boundary without adding
// reverse dependencies. Every section carries an independent status,
// completion flag, scope and error; missing data is unavailable and is never
// represented as a ready zero value.

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pnga::statistics {

enum class StatisticsSectionId {
  kOverview,
  kChunks,
  kFilters,
  kBlocks,
  kTokens,
  kLengths,
  kDistances,
};

enum class SectionStatus {
  kUnavailable,
  kReady,
  kPartial,
  kCancelled,
  kBudgetExceeded,
  kInvalidInput,
  kOverflow,
  kError,
};

enum class SectionScope { kNone, kWholeDocument, kVerifiedPrefix };

enum class BlockKind : std::uint8_t { kStored = 0, kFixed = 1, kDynamic = 2 };
enum class TokenKind : std::uint8_t {
  kLiteral = 0,
  kLengthDistance = 1,
  kEndOfBlock = 2,
};

struct StatisticsLimits {
  std::uint64_t max_samples = 1ULL << 20;
  std::uint64_t max_chunk_types = 1024;
  std::uint64_t max_length_values = 65536;
  std::uint64_t max_distance_values = 65536;
};

// These samples intentionally contain only stable scalar values. They are
// views into caller-owned data and are consumed synchronously by collect()
// and StatisticsAccumulator::add().
struct ChunkSample {
  std::string_view type;  // PNG Chunk type, exactly four bytes
  std::uint64_t data_bytes = 0;
};

struct FilterSample {
  std::uint8_t type = 0;  // PNG filter type 0..4
  std::uint64_t data_bytes = 0;
};

struct BlockSample {
  BlockKind kind = BlockKind::kStored;
  std::uint64_t compressed_bits = 0;
  std::uint64_t output_bytes = 0;
};

struct TokenSample {
  TokenKind kind = TokenKind::kLiteral;
  std::uint64_t input_bits = 0;
  std::uint64_t output_bytes = 0;
  std::uint64_t length = 0;
  std::uint64_t distance = 0;
};

struct ChunkBucket {
  std::string type;
  std::uint64_t count = 0;
  std::uint64_t data_bytes = 0;

  bool operator==(const ChunkBucket&) const = default;
};

struct FilterBucket {
  std::uint64_t rows = 0;
  std::uint64_t data_bytes = 0;

  bool operator==(const FilterBucket&) const = default;
};

struct BlockBucket {
  std::uint64_t blocks = 0;
  std::uint64_t compressed_bits = 0;
  std::uint64_t output_bytes = 0;

  bool operator==(const BlockBucket&) const = default;
};

struct TokenBucket {
  std::uint64_t count = 0;
  std::uint64_t input_bits = 0;
  std::uint64_t output_bytes = 0;

  bool operator==(const TokenBucket&) const = default;
};

struct ValueBucket {
  std::uint64_t value = 0;
  std::uint64_t count = 0;

  bool operator==(const ValueBucket&) const = default;
};

struct SectionState {
  SectionStatus status = SectionStatus::kUnavailable;
  bool complete = false;
  SectionScope scope = SectionScope::kNone;
  std::string error;

  bool operator==(const SectionState&) const = default;
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
  std::vector<FilterBucket> buckets;  // always five entries, types 0..4
};

struct BlockStatistics {
  std::uint64_t count = 0;
  std::uint64_t compressed_bits = 0;
  std::uint64_t output_bytes = 0;
  std::vector<BlockBucket> buckets;  // always three entries, stored/fixed/dynamic
};

struct TokenStatistics {
  std::uint64_t count = 0;
  std::uint64_t input_bits = 0;
  std::uint64_t output_bytes = 0;
  std::vector<TokenBucket> buckets;  // always three entries, literal/match/EOB
};

struct ValueStatistics {
  std::vector<ValueBucket> buckets;  // sorted by value
};

template <class Data>
struct StatisticsSection {
  SectionState state;
  Data data;
};

struct StatisticsSnapshot {
  StatisticsSection<OverviewStatistics> overview;
  StatisticsSection<ChunkStatistics> chunks;
  StatisticsSection<FilterStatistics> filters;
  StatisticsSection<BlockStatistics> blocks;
  StatisticsSection<TokenStatistics> tokens;
  StatisticsSection<ValueStatistics> lengths;
  StatisticsSection<ValueStatistics> distances;

  // True only when every section is ready, complete and whole_document.
  bool complete() const noexcept;
};

struct StatisticsInput {
  std::span<const ChunkSample> chunks;
  std::span<const FilterSample> filters;
  std::span<const BlockSample> blocks;
  std::span<const TokenSample> tokens;
  std::uint64_t compressed_bytes = 0;
  std::uint64_t inflated_bytes = 0;
  bool has_compression_totals = false;
};

using CancelPredicate = std::function<bool()>;

// Checked, bounded aggregation of scalar samples into per-section state.
// Every section starts unavailable; add() feeds one sample through checked
// arithmetic and deterministic bucket ordering; finish() seals a section.
// A TokenSample match updates the token bucket and the length/distance
// histograms atomically after validating length, distance and histogram
// capacity, and every token failure marks the lengths and distances sections
// together with tokens. Totals collected before cancellation, overflow or a
// budget limit are preserved as a verified prefix; the accumulator never
// allocates according to an untrusted sample count.
class StatisticsAccumulator {
 public:
  explicit StatisticsAccumulator(StatisticsLimits limits = {});
  bool add(ChunkSample sample);
  bool add(FilterSample sample);
  bool add(BlockSample sample);
  bool add(TokenSample sample);
  bool set_compression_totals(std::uint64_t compressed,
                              std::uint64_t inflated);
  // Seals one section. finish refuses ready+complete with a non
  // whole_document scope by downgrading it to partial+incomplete and
  // preserves the first non-ready status and error of an already sealed
  // section.
  void finish(StatisticsSectionId id, SectionStatus status, bool complete,
              SectionScope scope, std::string error = {});
  const StatisticsSnapshot& snapshot() const noexcept;

 private:
  StatisticsSnapshot snapshot_;
  StatisticsLimits limits_;
  std::uint64_t chunk_samples_ = 0;
  std::uint64_t filter_samples_ = 0;
  std::uint64_t block_samples_ = 0;
  std::uint64_t token_samples_ = 0;
};

// Compatibility entry implemented through StatisticsAccumulator: feeds all
// four supplied spans through the accumulator and marks each fed section
// complete. Empty spans are empty-but-present sources and become ready with
// zero totals; overview is ready only when compression totals are supplied.
StatisticsSnapshot collect(const StatisticsInput& input,
                           StatisticsLimits limits = {},
                           CancelPredicate should_cancel = {});

}  // namespace pnga::statistics

#endif  // PNGA_STATISTICS_STATISTICS_H
