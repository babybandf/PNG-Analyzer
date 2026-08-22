// WP-602A: deterministic, bounded statistics aggregation.

#include "pnga/statistics/statistics.h"

#include <algorithm>
#include <array>
#include <limits>

namespace pnga::statistics {
namespace {

constexpr std::uint64_t kCheckInterval = 256;

bool checked_add(std::uint64_t* target, std::uint64_t value) noexcept {
  if (value > std::numeric_limits<std::uint64_t>::max() - *target) {
    return false;
  }
  *target += value;
  return true;
}

bool checked_mul(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* result) noexcept {
  if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right) {
    return false;
  }
  *result = left * right;
  return true;
}

void fail(StatisticsSnapshot* output, BuildStatus status, const char* message) {
  if (output->status == BuildStatus::kReady) {
    output->status = status;
    output->error = message;
  }
}

bool check_budget(std::uint64_t current, std::uint64_t limit,
                  StatisticsSnapshot* output, const char* message) {
  if (current >= limit) {
    fail(output, BuildStatus::kBudgetExceeded, message);
    return false;
  }
  return true;
}

bool add_value(std::vector<ValueBucket>* buckets, std::uint64_t value,
               std::uint64_t limit, StatisticsSnapshot* output,
               const char* message) {
  const auto it = std::lower_bound(
      buckets->begin(), buckets->end(), value,
      [](const ValueBucket& bucket, std::uint64_t wanted) {
        return bucket.value < wanted;
      });
  if (it != buckets->end() && it->value == value) {
    if (!checked_add(&it->count, 1)) {
      fail(output, BuildStatus::kOverflow, "statistics count overflow");
      return false;
    }
    return true;
  }
  if (!check_budget(buckets->size(), limit, output, message)) {
    return false;
  }
  buckets->insert(it, ValueBucket{value, 1});
  return true;
}

bool cancelled(const CancelPredicate& predicate, std::uint64_t index) {
  return static_cast<bool>(predicate) &&
         (index % kCheckInterval == 0) && predicate();
}

}  // namespace

const char* build_status_text(BuildStatus status) noexcept {
  switch (status) {
    case BuildStatus::kReady:
      return "ready";
    case BuildStatus::kCancelled:
      return "cancelled";
    case BuildStatus::kOverflow:
      return "overflow";
    case BuildStatus::kBudgetExceeded:
      return "budget_exceeded";
    case BuildStatus::kInvalidInput:
      return "invalid_input";
  }
  return "unknown";
}

std::uint64_t StatisticsSnapshot::compression_rate_per_mille() const noexcept {
  if (!has_compression_totals || inflated_bytes == 0) {
    return 0;
  }
  std::uint64_t scaled = 0;
  if (!checked_mul(compressed_bytes, 1000, &scaled)) {
    return 0;
  }
  return scaled / inflated_bytes;
}

StatisticsSnapshot collect(const StatisticsInput& input, StatisticsLimits limits,
                           CancelPredicate should_cancel) {
  StatisticsSnapshot output;
  output.filters.resize(5);
  output.blocks.resize(3);
  output.tokens.resize(3);

  if (limits.max_chunk_types == 0 || limits.max_length_values == 0 ||
      limits.max_distance_values == 0) {
    fail(&output, BuildStatus::kBudgetExceeded,
         "statistics bucket budget must be positive");
    return output;
  }

  if (input.has_compression_totals) {
    output.compressed_bytes = input.compressed_bytes;
    output.inflated_bytes = input.inflated_bytes;
    output.has_compression_totals = true;
  }

  std::uint64_t index = 0;
  for (const ChunkSample& sample : input.chunks) {
    if (cancelled(should_cancel, index++)) {
      fail(&output, BuildStatus::kCancelled, "statistics collection cancelled");
      return output;
    }
    if (sample.type.size() != 4) {
      fail(&output, BuildStatus::kInvalidInput,
           "Chunk type must contain exactly four bytes");
      return output;
    }
    auto it = std::lower_bound(
        output.chunks.begin(), output.chunks.end(), sample.type,
        [](const ChunkBucket& bucket, std::string_view wanted) {
          return bucket.type < wanted;
        });
    if (it == output.chunks.end() || it->type != sample.type) {
      if (!check_budget(output.chunks.size(), limits.max_chunk_types, &output,
                        "chunk type bucket budget exceeded")) {
        return output;
      }
      it = output.chunks.insert(it, ChunkBucket{std::string(sample.type), 0, 0});
    }
    if (!checked_add(&output.chunk_count, 1) ||
        !checked_add(&output.chunk_data_bytes, sample.data_bytes) ||
        !checked_add(&it->count, 1) ||
        !checked_add(&it->data_bytes, sample.data_bytes)) {
      fail(&output, BuildStatus::kOverflow, "chunk statistics overflow");
      return output;
    }
  }

  index = 0;
  for (const FilterSample& sample : input.filters) {
    if (cancelled(should_cancel, index++)) {
      fail(&output, BuildStatus::kCancelled, "statistics collection cancelled");
      return output;
    }
    if (sample.type >= output.filters.size()) {
      if (!checked_add(&output.invalid_filter_rows, 1)) {
        fail(&output, BuildStatus::kOverflow, "filter statistics overflow");
        return output;
      }
      continue;
    }
    FilterBucket& bucket = output.filters[sample.type];
    if (!checked_add(&output.filter_rows, 1) ||
        !checked_add(&output.filter_data_bytes, sample.data_bytes) ||
        !checked_add(&bucket.rows, 1) ||
        !checked_add(&bucket.data_bytes, sample.data_bytes)) {
      fail(&output, BuildStatus::kOverflow, "filter statistics overflow");
      return output;
    }
  }

  index = 0;
  for (const BlockSample& sample : input.blocks) {
    if (cancelled(should_cancel, index++)) {
      fail(&output, BuildStatus::kCancelled, "statistics collection cancelled");
      return output;
    }
    const auto kind = static_cast<std::size_t>(sample.kind);
    if (kind >= output.blocks.size()) {
      fail(&output, BuildStatus::kInvalidInput, "invalid Deflate block kind");
      return output;
    }
    BlockBucket& bucket = output.blocks[kind];
    if (!checked_add(&output.block_count, 1) ||
        !checked_add(&output.block_compressed_bits, sample.compressed_bits) ||
        !checked_add(&output.block_output_bytes, sample.output_bytes) ||
        !checked_add(&bucket.blocks, 1) ||
        !checked_add(&bucket.compressed_bits, sample.compressed_bits) ||
        !checked_add(&bucket.output_bytes, sample.output_bytes)) {
      fail(&output, BuildStatus::kOverflow, "block statistics overflow");
      return output;
    }
  }

  index = 0;
  for (const TokenSample& sample : input.tokens) {
    if (cancelled(should_cancel, index++)) {
      fail(&output, BuildStatus::kCancelled, "statistics collection cancelled");
      return output;
    }
    const auto kind = static_cast<std::size_t>(sample.kind);
    if (kind >= output.tokens.size()) {
      fail(&output, BuildStatus::kInvalidInput, "invalid Deflate token kind");
      return output;
    }
    TokenBucket& bucket = output.tokens[kind];
    if (!checked_add(&output.token_count, 1) ||
        !checked_add(&output.token_input_bits, sample.input_bits) ||
        !checked_add(&output.token_output_bytes, sample.output_bytes) ||
        !checked_add(&bucket.count, 1) ||
        !checked_add(&bucket.input_bits, sample.input_bits) ||
        !checked_add(&bucket.output_bytes, sample.output_bytes)) {
      fail(&output, BuildStatus::kOverflow, "token statistics overflow");
      return output;
    }
    if (sample.kind == TokenKind::kLengthDistance) {
      if (sample.length == 0 || sample.distance == 0 ||
          !add_value(&output.lengths, sample.length, limits.max_length_values,
                     &output, "length histogram budget exceeded") ||
          !add_value(&output.distances, sample.distance,
                     limits.max_distance_values, &output,
                     "distance histogram budget exceeded")) {
        if (output.status == BuildStatus::kReady) {
          fail(&output, BuildStatus::kInvalidInput,
               "length-distance token requires non-zero length and distance");
        }
        return output;
      }
    }
  }

  return output;
}

}  // namespace pnga::statistics
