#ifndef PNGA_STATISTICS_STATISTICS_H
#define PNGA_STATISTICS_STATISTICS_H

// WP-602A: bounded, Qt-free statistics aggregation. The engine consumes
// backend-neutral samples so callers adapt Chunk/PNG filter/Deflate models at
// the composition boundary without adding reverse dependencies.

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pnga::statistics {

enum class BuildStatus {
  kReady = 0,
  kCancelled = 1,
  kOverflow = 2,
  kBudgetExceeded = 3,
  kInvalidInput = 4,
};

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
// views into caller-owned data and are consumed synchronously by collect().
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

struct StatisticsSnapshot {
  BuildStatus status = BuildStatus::kReady;
  std::string error;

  std::uint64_t chunk_count = 0;
  std::uint64_t chunk_data_bytes = 0;
  std::vector<ChunkBucket> chunks;

  std::uint64_t filter_rows = 0;
  std::uint64_t filter_data_bytes = 0;
  std::vector<FilterBucket> filters;  // always five entries, types 0..4
  std::uint64_t invalid_filter_rows = 0;

  std::uint64_t block_count = 0;
  std::uint64_t block_compressed_bits = 0;
  std::uint64_t block_output_bytes = 0;
  std::vector<BlockBucket> blocks;  // always three entries, stored/fixed/dynamic

  std::uint64_t token_count = 0;
  std::uint64_t token_input_bits = 0;
  std::uint64_t token_output_bytes = 0;
  std::vector<TokenBucket> tokens;  // always three entries, literal/match/EOB
  std::vector<ValueBucket> lengths;   // sorted by value
  std::vector<ValueBucket> distances; // sorted by value

  std::uint64_t compressed_bytes = 0;
  std::uint64_t inflated_bytes = 0;
  bool has_compression_totals = false;

  // A compact, locale-independent ratio for callers that need a display
  // value: compressed bytes per 1000 inflated bytes, rounded down. The raw
  // totals remain authoritative when the multiplication would overflow.
  std::uint64_t compression_rate_per_mille() const noexcept;

  bool complete() const noexcept { return status == BuildStatus::kReady; }
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

// Aggregates bounded scalar samples synchronously. The returned snapshot keeps
// all validated totals collected before cancellation, overflow or a budget
// limit is observed; it never allocates according to an untrusted sample
// count. Chunk and histogram buckets are emitted in deterministic sort order.
StatisticsSnapshot collect(const StatisticsInput& input,
                           StatisticsLimits limits = {},
                           CancelPredicate should_cancel = {});

const char* build_status_text(BuildStatus status) noexcept;

}  // namespace pnga::statistics

#endif  // PNGA_STATISTICS_STATISTICS_H
