// WP-602A/602B: deterministic, bounded, per-section statistics aggregation.

#include "pnga/statistics/statistics.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

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

bool cancelled(const CancelPredicate& predicate, std::uint64_t index) {
  return static_cast<bool>(predicate) &&
         (index % kCheckInterval == 0) && predicate();
}

SectionState& state_of(StatisticsSnapshot& snapshot,
                       StatisticsSectionId id) noexcept {
  switch (id) {
    case StatisticsSectionId::kOverview:
      return snapshot.overview.state;
    case StatisticsSectionId::kChunks:
      return snapshot.chunks.state;
    case StatisticsSectionId::kFilters:
      return snapshot.filters.state;
    case StatisticsSectionId::kBlocks:
      return snapshot.blocks.state;
    case StatisticsSectionId::kTokens:
      return snapshot.tokens.state;
    case StatisticsSectionId::kLengths:
      return snapshot.lengths.state;
    case StatisticsSectionId::kDistances:
      return snapshot.distances.state;
  }
  return snapshot.overview.state;
}

}  // namespace

bool StatisticsSnapshot::complete() const noexcept {
  const auto ready = [](const SectionState& state) noexcept {
    return state.status == SectionStatus::kReady && state.complete &&
           state.scope == SectionScope::kWholeDocument;
  };
  return ready(overview.state) && ready(chunks.state) &&
         ready(filters.state) && ready(blocks.state) &&
         ready(tokens.state) && ready(lengths.state) &&
         ready(distances.state);
}

StatisticsAccumulator::StatisticsAccumulator(StatisticsLimits limits)
    : limits_(limits) {
  snapshot_.filters.data.buckets.resize(5);
  snapshot_.blocks.data.buckets.resize(3);
  snapshot_.tokens.data.buckets.resize(3);
}

void StatisticsAccumulator::finish(StatisticsSectionId id, SectionStatus status,
                                   bool complete, SectionScope scope,
                                   std::string error) {
  SectionState& state = state_of(snapshot_, id);
  if (state.status != SectionStatus::kUnavailable) {
    // Sealed sections keep their first terminal state and error.
    return;
  }
  if (status == SectionStatus::kReady && complete &&
      scope != SectionScope::kWholeDocument) {
    // Refuse a falsely complete section: only ready + complete +
    // whole_document may be published as complete.
    state.status = SectionStatus::kPartial;
    state.complete = false;
    state.scope = scope;
    state.error = std::move(error);
    return;
  }
  state.status = status;
  state.complete = complete;
  state.scope = scope;
  state.error = std::move(error);
}

bool StatisticsAccumulator::add(ChunkSample sample) {
  auto& section = snapshot_.chunks;
  if (section.state.status != SectionStatus::kUnavailable) {
    return false;
  }
  if (limits_.max_samples == 0 || limits_.max_chunk_types == 0) {
    finish(StatisticsSectionId::kChunks, SectionStatus::kBudgetExceeded,
           false, SectionScope::kVerifiedPrefix,
           "statistics bucket budget must be positive");
    return false;
  }
  if (chunk_samples_ >= limits_.max_samples) {
    finish(StatisticsSectionId::kChunks, SectionStatus::kBudgetExceeded,
           false, SectionScope::kVerifiedPrefix,
           "statistics sample budget exceeded");
    return false;
  }
  if (sample.type.size() != 4) {
    finish(StatisticsSectionId::kChunks, SectionStatus::kInvalidInput, false,
           SectionScope::kVerifiedPrefix,
           "Chunk type must contain exactly four bytes");
    return false;
  }
  auto it = std::lower_bound(
      section.data.buckets.begin(), section.data.buckets.end(), sample.type,
      [](const ChunkBucket& bucket, std::string_view wanted) {
        return bucket.type < wanted;
      });
  if (it == section.data.buckets.end() || it->type != sample.type) {
    if (section.data.buckets.size() >= limits_.max_chunk_types) {
      finish(StatisticsSectionId::kChunks, SectionStatus::kBudgetExceeded,
             false, SectionScope::kVerifiedPrefix,
             "chunk type bucket budget exceeded");
      return false;
    }
    it = section.data.buckets.insert(
        it, ChunkBucket{std::string(sample.type), 0, 0});
  }
  if (!checked_add(&section.data.count, 1) ||
      !checked_add(&section.data.data_bytes, sample.data_bytes) ||
      !checked_add(&it->count, 1) ||
      !checked_add(&it->data_bytes, sample.data_bytes)) {
    finish(StatisticsSectionId::kChunks, SectionStatus::kOverflow, false,
           SectionScope::kVerifiedPrefix, "chunk statistics overflow");
    return false;
  }
  ++chunk_samples_;
  return true;
}

bool StatisticsAccumulator::add(FilterSample sample) {
  auto& section = snapshot_.filters;
  if (section.state.status != SectionStatus::kUnavailable) {
    return false;
  }
  if (limits_.max_samples == 0) {
    finish(StatisticsSectionId::kFilters, SectionStatus::kBudgetExceeded,
           false, SectionScope::kVerifiedPrefix,
           "statistics bucket budget must be positive");
    return false;
  }
  if (filter_samples_ >= limits_.max_samples) {
    finish(StatisticsSectionId::kFilters, SectionStatus::kBudgetExceeded,
           false, SectionScope::kVerifiedPrefix,
           "statistics sample budget exceeded");
    return false;
  }
  if (sample.type >= section.data.buckets.size()) {
    if (!checked_add(&section.data.invalid_rows, 1)) {
      finish(StatisticsSectionId::kFilters, SectionStatus::kOverflow, false,
             SectionScope::kVerifiedPrefix, "filter statistics overflow");
      return false;
    }
    ++filter_samples_;
    return true;
  }
  FilterBucket& bucket = section.data.buckets[sample.type];
  if (!checked_add(&section.data.rows, 1) ||
      !checked_add(&section.data.data_bytes, sample.data_bytes) ||
      !checked_add(&bucket.rows, 1) ||
      !checked_add(&bucket.data_bytes, sample.data_bytes)) {
    finish(StatisticsSectionId::kFilters, SectionStatus::kOverflow, false,
           SectionScope::kVerifiedPrefix, "filter statistics overflow");
    return false;
  }
  ++filter_samples_;
  return true;
}

bool StatisticsAccumulator::add(BlockSample sample) {
  auto& section = snapshot_.blocks;
  if (section.state.status != SectionStatus::kUnavailable) {
    return false;
  }
  if (limits_.max_samples == 0) {
    finish(StatisticsSectionId::kBlocks, SectionStatus::kBudgetExceeded,
           false, SectionScope::kVerifiedPrefix,
           "statistics bucket budget must be positive");
    return false;
  }
  if (block_samples_ >= limits_.max_samples) {
    finish(StatisticsSectionId::kBlocks, SectionStatus::kBudgetExceeded,
           false, SectionScope::kVerifiedPrefix,
           "statistics sample budget exceeded");
    return false;
  }
  const auto kind = static_cast<std::size_t>(sample.kind);
  if (kind >= section.data.buckets.size()) {
    finish(StatisticsSectionId::kBlocks, SectionStatus::kInvalidInput, false,
           SectionScope::kVerifiedPrefix, "invalid Deflate block kind");
    return false;
  }
  BlockBucket& bucket = section.data.buckets[kind];
  if (!checked_add(&section.data.count, 1) ||
      !checked_add(&section.data.compressed_bits, sample.compressed_bits) ||
      !checked_add(&section.data.output_bytes, sample.output_bytes) ||
      !checked_add(&bucket.blocks, 1) ||
      !checked_add(&bucket.compressed_bits, sample.compressed_bits) ||
      !checked_add(&bucket.output_bytes, sample.output_bytes)) {
    finish(StatisticsSectionId::kBlocks, SectionStatus::kOverflow, false,
           SectionScope::kVerifiedPrefix, "block statistics overflow");
    return false;
  }
  ++block_samples_;
  return true;
}

bool StatisticsAccumulator::add(TokenSample sample) {
  auto& section = snapshot_.tokens;
  if (section.state.status != SectionStatus::kUnavailable) {
    return false;
  }
  const auto fail_group = [&](SectionStatus status, const char* message) {
    // Token-driven failures mark tokens and the dependent length/distance
    // histograms together; the first failure per section wins.
    finish(StatisticsSectionId::kTokens, status, false,
           SectionScope::kVerifiedPrefix, message);
    finish(StatisticsSectionId::kLengths, status, false,
           SectionScope::kVerifiedPrefix, message);
    finish(StatisticsSectionId::kDistances, status, false,
           SectionScope::kVerifiedPrefix, message);
  };
  if (limits_.max_samples == 0 || limits_.max_length_values == 0 ||
      limits_.max_distance_values == 0) {
    fail_group(SectionStatus::kBudgetExceeded,
               "statistics bucket budget must be positive");
    return false;
  }
  if (token_samples_ >= limits_.max_samples) {
    fail_group(SectionStatus::kBudgetExceeded,
               "statistics sample budget exceeded");
    return false;
  }
  const auto kind = static_cast<std::size_t>(sample.kind);
  if (kind >= section.data.buckets.size()) {
    fail_group(SectionStatus::kInvalidInput, "invalid Deflate token kind");
    return false;
  }
  auto& lengths = snapshot_.lengths.data.buckets;
  auto& distances = snapshot_.distances.data.buckets;
  if (sample.kind == TokenKind::kLengthDistance) {
    // Validate the whole match group before mutating either histogram so a
    // rejected token leaves tokens, lengths and distances untouched.
    if (sample.length == 0 || sample.distance == 0) {
      fail_group(SectionStatus::kInvalidInput,
                 "length-distance token requires non-zero length and distance");
      return false;
    }
    const auto length_present = std::lower_bound(
        lengths.begin(), lengths.end(), sample.length,
        [](const ValueBucket& bucket, std::uint64_t wanted) {
          return bucket.value < wanted;
        });
    const bool length_new =
        length_present == lengths.end() || length_present->value != sample.length;
    if (length_new && lengths.size() >= limits_.max_length_values) {
      fail_group(SectionStatus::kBudgetExceeded,
                 "length histogram budget exceeded");
      return false;
    }
    const auto distance_present = std::lower_bound(
        distances.begin(), distances.end(), sample.distance,
        [](const ValueBucket& bucket, std::uint64_t wanted) {
          return bucket.value < wanted;
        });
    const bool distance_new =
        distance_present == distances.end() ||
        distance_present->value != sample.distance;
    if (distance_new && distances.size() >= limits_.max_distance_values) {
      fail_group(SectionStatus::kBudgetExceeded,
                 "distance histogram budget exceeded");
      return false;
    }
  }
  TokenBucket& bucket = section.data.buckets[kind];
  if (!checked_add(&section.data.count, 1) ||
      !checked_add(&section.data.input_bits, sample.input_bits) ||
      !checked_add(&section.data.output_bytes, sample.output_bytes) ||
      !checked_add(&bucket.count, 1) ||
      !checked_add(&bucket.input_bits, sample.input_bits) ||
      !checked_add(&bucket.output_bytes, sample.output_bytes)) {
    fail_group(SectionStatus::kOverflow, "token statistics overflow");
    return false;
  }
  if (sample.kind == TokenKind::kLengthDistance) {
    const auto add_to = [](std::vector<ValueBucket>* buckets,
                           std::uint64_t value) {
      auto it = std::lower_bound(
          buckets->begin(), buckets->end(), value,
          [](const ValueBucket& bucket, std::uint64_t wanted) {
            return bucket.value < wanted;
          });
      if (it != buckets->end() && it->value == value) {
        return checked_add(&it->count, 1);
      }
      buckets->insert(it, ValueBucket{value, 1});
      return true;
    };
    if (!add_to(&lengths, sample.length) || !add_to(&distances, sample.distance)) {
      fail_group(SectionStatus::kOverflow, "statistics count overflow");
      return false;
    }
  }
  ++token_samples_;
  return true;
}

bool StatisticsAccumulator::set_compression_totals(std::uint64_t compressed,
                                                   std::uint64_t inflated) {
  auto& section = snapshot_.overview;
  if (section.state.status != SectionStatus::kUnavailable) {
    return false;
  }
  section.data.compressed_bytes = compressed;
  section.data.inflated_bytes = inflated;
  section.data.has_compression_totals = true;
  return true;
}

const StatisticsSnapshot& StatisticsAccumulator::snapshot() const noexcept {
  return snapshot_;
}

StatisticsSnapshot collect(const StatisticsInput& input, StatisticsLimits limits,
                           CancelPredicate should_cancel) {
  StatisticsAccumulator accumulator(limits);

  if (limits.max_samples == 0 || limits.max_chunk_types == 0 ||
      limits.max_length_values == 0 || limits.max_distance_values == 0) {
    for (const StatisticsSectionId id :
         {StatisticsSectionId::kOverview, StatisticsSectionId::kChunks,
          StatisticsSectionId::kFilters, StatisticsSectionId::kBlocks,
          StatisticsSectionId::kTokens, StatisticsSectionId::kLengths,
          StatisticsSectionId::kDistances}) {
      accumulator.finish(id, SectionStatus::kBudgetExceeded, false,
                         SectionScope::kNone,
                         "statistics bucket budget must be positive");
    }
    return accumulator.snapshot();
  }

  if (input.has_compression_totals) {
    accumulator.set_compression_totals(input.compressed_bytes,
                                       input.inflated_bytes);
    accumulator.finish(StatisticsSectionId::kOverview, SectionStatus::kReady,
                       true, SectionScope::kWholeDocument);
  }

  const auto cancel_message = "statistics collection cancelled";
  const auto budget_message = "statistics sample budget exceeded";

  bool running = true;
  if (input.chunks.size() > limits.max_samples) {
    accumulator.finish(StatisticsSectionId::kChunks,
                       SectionStatus::kBudgetExceeded, false,
                       SectionScope::kNone, budget_message);
    running = false;
  } else {
    std::uint64_t index = 0;
    for (const ChunkSample& sample : input.chunks) {
      if (cancelled(should_cancel, index++)) {
        accumulator.finish(StatisticsSectionId::kChunks,
                           SectionStatus::kCancelled, false,
                           SectionScope::kVerifiedPrefix, cancel_message);
        running = false;
        break;
      }
      if (!accumulator.add(sample)) {
        running = false;
        break;
      }
    }
    if (running) {
      accumulator.finish(StatisticsSectionId::kChunks, SectionStatus::kReady,
                         true, SectionScope::kWholeDocument);
    }
  }

  if (running) {
    if (input.filters.size() > limits.max_samples) {
      accumulator.finish(StatisticsSectionId::kFilters,
                         SectionStatus::kBudgetExceeded, false,
                         SectionScope::kNone, budget_message);
      running = false;
    } else {
      std::uint64_t index = 0;
      for (const FilterSample& sample : input.filters) {
        if (cancelled(should_cancel, index++)) {
          accumulator.finish(StatisticsSectionId::kFilters,
                             SectionStatus::kCancelled, false,
                             SectionScope::kVerifiedPrefix, cancel_message);
          running = false;
          break;
        }
        if (!accumulator.add(sample)) {
          running = false;
          break;
        }
      }
      if (running) {
        accumulator.finish(StatisticsSectionId::kFilters,
                           SectionStatus::kReady, true,
                           SectionScope::kWholeDocument);
      }
    }
  }

  if (running) {
    if (input.blocks.size() > limits.max_samples) {
      accumulator.finish(StatisticsSectionId::kBlocks,
                         SectionStatus::kBudgetExceeded, false,
                         SectionScope::kNone, budget_message);
      running = false;
    } else {
      std::uint64_t index = 0;
      for (const BlockSample& sample : input.blocks) {
        if (cancelled(should_cancel, index++)) {
          accumulator.finish(StatisticsSectionId::kBlocks,
                             SectionStatus::kCancelled, false,
                             SectionScope::kVerifiedPrefix, cancel_message);
          running = false;
          break;
        }
        if (!accumulator.add(sample)) {
          running = false;
          break;
        }
      }
      if (running) {
        accumulator.finish(StatisticsSectionId::kBlocks, SectionStatus::kReady,
                           true, SectionScope::kWholeDocument);
      }
    }
  }

  if (running) {
    const auto finish_group = [&](SectionStatus status, bool complete,
                                  SectionScope scope, const char* message) {
      for (const StatisticsSectionId id :
           {StatisticsSectionId::kTokens, StatisticsSectionId::kLengths,
            StatisticsSectionId::kDistances}) {
        accumulator.finish(id, status, complete, scope, message);
      }
    };
    if (input.tokens.size() > limits.max_samples) {
      finish_group(SectionStatus::kBudgetExceeded, false, SectionScope::kNone,
                   budget_message);
    } else {
      std::uint64_t index = 0;
      bool stopped = false;
      for (const TokenSample& sample : input.tokens) {
        if (cancelled(should_cancel, index++)) {
          finish_group(SectionStatus::kCancelled, false,
                       SectionScope::kVerifiedPrefix, cancel_message);
          stopped = true;
          break;
        }
        if (!accumulator.add(sample)) {
          // The accumulator already sealed tokens, lengths and distances.
          stopped = true;
          break;
        }
      }
      if (!stopped) {
        finish_group(SectionStatus::kReady, true,
                     SectionScope::kWholeDocument, "");
      }
    }
  }

  return accumulator.snapshot();
}

}  // namespace pnga::statistics
