#include <pnga/statistics/statistics.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using pnga::statistics::BlockKind;
using pnga::statistics::BlockSample;
using pnga::statistics::ChunkSample;
using pnga::statistics::FilterSample;
using pnga::statistics::SectionScope;
using pnga::statistics::SectionState;
using pnga::statistics::SectionStatus;
using pnga::statistics::StatisticsAccumulator;
using pnga::statistics::StatisticsInput;
using pnga::statistics::StatisticsLimits;
using pnga::statistics::StatisticsSectionId;
using pnga::statistics::TokenKind;
using pnga::statistics::TokenSample;

namespace {

// Returns true from the k-th invocation on, so a collection that consults the
// predicate once per 256 samples stops with a deterministic collected prefix.
auto cancel_on_call(int trigger_call) {
  return [trigger_call, calls = 0]() mutable { return ++calls >= trigger_call; };
}

bool ready_complete_whole_document(const SectionState& state) {
  return state.status == SectionStatus::kReady && state.complete &&
         state.scope == SectionScope::kWholeDocument;
}

}  // namespace

TEST_CASE("Statistics aggregate into per-section ready snapshot",
          "[statistics][wp602b]") {
  const std::array chunks = {ChunkSample{"IDAT", 100},
                             ChunkSample{"IHDR", 13},
                             ChunkSample{"IDAT", 20}};
  const std::array filters = {FilterSample{0, 10}, FilterSample{4, 10},
                              FilterSample{0, 10}};
  const std::array blocks = {
      BlockSample{BlockKind::kDynamic, 20, 100},
      BlockSample{BlockKind::kFixed, 10, 20},
  };
  const std::array tokens = {
      TokenSample{TokenKind::kLiteral, 8, 1, 0, 0},
      TokenSample{TokenKind::kLengthDistance, 12, 5, 5, 3},
      TokenSample{TokenKind::kEndOfBlock, 7, 0, 0, 0},
      TokenSample{TokenKind::kLengthDistance, 13, 4, 5, 3},
  };
  const auto snapshot = pnga::statistics::collect(
      StatisticsInput{chunks, filters, blocks, tokens, 65, 120, true});

  REQUIRE(snapshot.complete());
  REQUIRE(ready_complete_whole_document(snapshot.overview.state));
  REQUIRE(snapshot.overview.data.compressed_bytes == 65);
  REQUIRE(snapshot.overview.data.inflated_bytes == 120);
  REQUIRE(snapshot.overview.data.has_compression_totals);
  REQUIRE(ready_complete_whole_document(snapshot.chunks.state));
  REQUIRE(snapshot.chunks.data.count == 3);
  REQUIRE(snapshot.chunks.data.data_bytes == 133);
  REQUIRE(snapshot.chunks.data.buckets ==
          std::vector<pnga::statistics::ChunkBucket>{{"IDAT", 2, 120},
                                                     {"IHDR", 1, 13}});
  REQUIRE(ready_complete_whole_document(snapshot.filters.state));
  REQUIRE(snapshot.filters.data.rows == 3);
  REQUIRE(snapshot.filters.data.data_bytes == 30);
  REQUIRE(snapshot.filters.data.invalid_rows == 0);
  REQUIRE(snapshot.filters.data.buckets[0].rows == 2);
  REQUIRE(snapshot.filters.data.buckets[4].rows == 1);
  REQUIRE(ready_complete_whole_document(snapshot.blocks.state));
  REQUIRE(snapshot.blocks.data.count == 2);
  REQUIRE(snapshot.blocks.data.buckets[1].blocks == 1);
  REQUIRE(snapshot.blocks.data.buckets[2].output_bytes == 100);
  REQUIRE(ready_complete_whole_document(snapshot.tokens.state));
  REQUIRE(snapshot.tokens.data.count == 4);
  REQUIRE(snapshot.tokens.data.buckets[0].count == 1);
  REQUIRE(snapshot.tokens.data.buckets[1].count == 2);
  REQUIRE(snapshot.tokens.data.buckets[2].count == 1);
  REQUIRE(ready_complete_whole_document(snapshot.lengths.state));
  REQUIRE(snapshot.lengths.data.buckets ==
          std::vector<pnga::statistics::ValueBucket>{{5, 2}});
  REQUIRE(ready_complete_whole_document(snapshot.distances.state));
  REQUIRE(snapshot.distances.data.buckets ==
          std::vector<pnga::statistics::ValueBucket>{{3, 2}});
}

TEST_CASE("Empty but present sources are ready with zero totals",
          "[statistics][wp602b]") {
  const auto snapshot = pnga::statistics::collect(StatisticsInput{});

  REQUIRE(snapshot.chunks.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.chunks.state.complete);
  REQUIRE(snapshot.chunks.state.scope == SectionScope::kWholeDocument);
  REQUIRE(snapshot.chunks.data.count == 0);
  REQUIRE(snapshot.chunks.data.buckets.empty());
  REQUIRE(snapshot.filters.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.filters.data.rows == 0);
  REQUIRE(snapshot.filters.data.buckets.size() == 5);
  REQUIRE(snapshot.blocks.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.tokens.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.lengths.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.lengths.data.buckets.empty());
  REQUIRE(snapshot.distances.state.status == SectionStatus::kReady);
  // Without compression totals the overview stays unavailable and the
  // snapshot as a whole is not complete.
  REQUIRE(snapshot.overview.state.status == SectionStatus::kUnavailable);
  REQUIRE(snapshot.overview.state.scope == SectionScope::kNone);
  REQUIRE_FALSE(snapshot.overview.data.has_compression_totals);
  REQUIRE_FALSE(snapshot.complete());
}

TEST_CASE("Cancellation preserves collected Chunk totals as verified_prefix",
          "[statistics][wp602b]") {
  std::array<ChunkSample, 512> samples{};
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i] = ChunkSample{"IDAT", 1};
  }
  const auto snapshot = pnga::statistics::collect(
      StatisticsInput{samples, {}, {}, {}, 0, 0, true},
      {}, cancel_on_call(2));

  REQUIRE(snapshot.chunks.state.status == SectionStatus::kCancelled);
  REQUIRE_FALSE(snapshot.chunks.state.complete);
  REQUIRE(snapshot.chunks.state.scope == SectionScope::kVerifiedPrefix);
  REQUIRE(snapshot.chunks.data.count == 256);
  REQUIRE(snapshot.chunks.data.data_bytes == 256);
  REQUIRE(snapshot.chunks.data.buckets.size() == 1);
  REQUIRE(snapshot.chunks.data.buckets[0] ==
          pnga::statistics::ChunkBucket{"IDAT", 256, 256});
  // Sections never attempted stay unavailable instead of reporting zeros.
  REQUIRE(snapshot.filters.state.status == SectionStatus::kUnavailable);
  REQUIRE(snapshot.blocks.state.status == SectionStatus::kUnavailable);
  REQUIRE(snapshot.tokens.state.status == SectionStatus::kUnavailable);
  REQUIRE(snapshot.lengths.state.status == SectionStatus::kUnavailable);
  REQUIRE(snapshot.distances.state.status == SectionStatus::kUnavailable);
  REQUIRE(snapshot.overview.state.status == SectionStatus::kReady);
  REQUIRE_FALSE(snapshot.complete());
}

TEST_CASE("Cancellation before the first sample keeps no totals",
          "[statistics][wp602b]") {
  std::array<ChunkSample, 512> samples{};
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i] = ChunkSample{"IDAT", 1};
  }
  const auto snapshot = pnga::statistics::collect(
      StatisticsInput{samples, {}, {}, {}}, {}, cancel_on_call(1));
  REQUIRE(snapshot.chunks.state.status == SectionStatus::kCancelled);
  REQUIRE(snapshot.chunks.state.scope == SectionScope::kVerifiedPrefix);
  REQUIRE(snapshot.chunks.data.count == 0);
  REQUIRE(snapshot.chunks.data.buckets.empty());
}

TEST_CASE("Length and distance sections fail together on an invalid match",
          "[statistics][wp602b]") {
  SECTION("zero length") {
    const std::array tokens = {
        TokenSample{TokenKind::kLengthDistance, 1, 1, 0, 2},
    };
    const auto snapshot =
        pnga::statistics::collect(StatisticsInput{{}, {}, {}, tokens});
    REQUIRE(snapshot.tokens.state.status == SectionStatus::kInvalidInput);
    REQUIRE(snapshot.lengths.state.status == SectionStatus::kInvalidInput);
    REQUIRE(snapshot.distances.state.status == SectionStatus::kInvalidInput);
    REQUIRE(snapshot.lengths.state.error ==
            snapshot.distances.state.error);
    REQUIRE_FALSE(snapshot.lengths.state.error.empty());
    // The rejected token mutates nothing (atomic token/histogram update).
    REQUIRE(snapshot.tokens.data.count == 0);
    REQUIRE(snapshot.lengths.data.buckets.empty());
    REQUIRE(snapshot.distances.data.buckets.empty());
    REQUIRE_FALSE(snapshot.complete());
  }
  SECTION("zero distance") {
    const std::array tokens = {
        TokenSample{TokenKind::kLengthDistance, 1, 1, 3, 0},
    };
    const auto snapshot =
        pnga::statistics::collect(StatisticsInput{{}, {}, {}, tokens});
    REQUIRE(snapshot.lengths.state.status == SectionStatus::kInvalidInput);
    REQUIRE(snapshot.distances.state.status == SectionStatus::kInvalidInput);
  }
  SECTION("invalid token kind") {
    const std::array tokens = {TokenSample{
        static_cast<TokenKind>(99), 1, 1, 0, 0}};
    const auto snapshot =
        pnga::statistics::collect(StatisticsInput{{}, {}, {}, tokens});
    REQUIRE(snapshot.tokens.state.status == SectionStatus::kInvalidInput);
    REQUIRE(snapshot.lengths.state.status == SectionStatus::kInvalidInput);
    REQUIRE(snapshot.distances.state.status == SectionStatus::kInvalidInput);
  }
  SECTION("valid matches before the failure are preserved") {
    const std::array tokens = {
        TokenSample{TokenKind::kLengthDistance, 12, 5, 5, 3},
        TokenSample{TokenKind::kLengthDistance, 12, 5, 0, 3},
    };
    const auto snapshot =
        pnga::statistics::collect(StatisticsInput{{}, {}, {}, tokens});
    REQUIRE(snapshot.lengths.state.status == SectionStatus::kInvalidInput);
    REQUIRE(snapshot.lengths.state.scope == SectionScope::kVerifiedPrefix);
    REQUIRE(snapshot.lengths.data.buckets ==
            std::vector<pnga::statistics::ValueBucket>{{5, 1}});
    REQUIRE(snapshot.distances.data.buckets ==
            std::vector<pnga::statistics::ValueBucket>{{3, 1}});
  }
}

TEST_CASE("Statistics reject unsafe bucket and arithmetic inputs",
          "[statistics][wp602b]") {
  SECTION("chunk type length") {
    const std::array chunks = {ChunkSample{"TOO-LONG", 1}};
    const auto snapshot =
        pnga::statistics::collect(StatisticsInput{chunks, {}, {}, {}});
    REQUIRE(snapshot.chunks.state.status == SectionStatus::kInvalidInput);
    REQUIRE(snapshot.chunks.data.count == 0);
    REQUIRE(snapshot.filters.state.status == SectionStatus::kUnavailable);
  }
  SECTION("chunk type bucket budget") {
    const std::array distinct = {ChunkSample{"A001", 1}, ChunkSample{"A002", 1}};
    StatisticsLimits limits;
    limits.max_chunk_types = 1;
    const auto snapshot =
        pnga::statistics::collect(StatisticsInput{distinct, {}, {}, {}}, limits);
    REQUIRE(snapshot.chunks.state.status == SectionStatus::kBudgetExceeded);
    REQUIRE(snapshot.chunks.data.buckets.size() == 1);
  }
  SECTION("invalid block kind") {
    const std::array blocks = {BlockSample{static_cast<BlockKind>(7), 1, 1}};
    const auto snapshot =
        pnga::statistics::collect(StatisticsInput{{}, {}, blocks, {}});
    REQUIRE(snapshot.blocks.state.status == SectionStatus::kInvalidInput);
    REQUIRE(snapshot.blocks.data.count == 0);
  }
}

TEST_CASE("Statistics reject oversized sample spans before allocation",
          "[statistics][wp602b]") {
  const std::array samples = {ChunkSample{"IDAT", 1}, ChunkSample{"IEND", 0}};
  StatisticsLimits limits;
  limits.max_samples = 1;
  const auto snapshot =
      pnga::statistics::collect(StatisticsInput{samples, {}, {}, {}}, limits);
  REQUIRE(snapshot.chunks.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.chunks.data.count == 0);
  REQUIRE(snapshot.chunks.data.buckets.empty());
  REQUIRE(snapshot.filters.state.status == SectionStatus::kUnavailable);
}

TEST_CASE("Statistics reject addition overflow without wrapping",
          "[statistics][wp602b]") {
  const std::array chunks = {ChunkSample{"IDAT", UINT64_MAX},
                             ChunkSample{"IDAT", 1}};
  const auto snapshot =
      pnga::statistics::collect(StatisticsInput{chunks, {}, {}, {}});
  REQUIRE(snapshot.chunks.state.status == SectionStatus::kOverflow);
  REQUIRE(snapshot.chunks.state.scope == SectionScope::kVerifiedPrefix);
  REQUIRE(snapshot.chunks.data.data_bytes == UINT64_MAX);
}

TEST_CASE("Non-positive limits fail every section without collection",
          "[statistics][wp602b]") {
  StatisticsLimits limits;
  limits.max_samples = 0;
  const auto snapshot =
      pnga::statistics::collect(StatisticsInput{{}, {}, {}, {}}, limits);
  REQUIRE(snapshot.overview.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.chunks.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.filters.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.blocks.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.tokens.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.lengths.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.distances.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.chunks.state.error ==
          "statistics bucket budget must be positive");
}

TEST_CASE("Length and distance histogram budgets fail the token group",
          "[statistics][wp602b]") {
  const std::array tokens = {
      TokenSample{TokenKind::kLengthDistance, 12, 5, 5, 3},
      TokenSample{TokenKind::kLengthDistance, 12, 5, 6, 3},
  };
  StatisticsLimits limits;
  limits.max_length_values = 1;
  const auto snapshot =
      pnga::statistics::collect(StatisticsInput{{}, {}, {}, tokens}, limits);
  REQUIRE(snapshot.tokens.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.lengths.state.status == SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.distances.state.status == SectionStatus::kBudgetExceeded);
  // The rejected token mutates nothing: one length bucket, one count.
  REQUIRE(snapshot.lengths.data.buckets ==
          std::vector<pnga::statistics::ValueBucket>{{5, 1}});
  REQUIRE(snapshot.tokens.data.count == 1);
}

TEST_CASE("Accumulator enforces deterministic section state transitions",
          "[statistics][wp602b]") {
  StatisticsAccumulator accumulator;

  SECTION("sealed sections refuse further samples") {
    REQUIRE(accumulator.add(ChunkSample{"IDAT", 5}));
    accumulator.finish(StatisticsSectionId::kChunks, SectionStatus::kReady,
                       true, SectionScope::kWholeDocument);
    CHECK(accumulator.snapshot().chunks.state.status == SectionStatus::kReady);
    REQUIRE_FALSE(accumulator.add(ChunkSample{"IDAT", 1}));
    REQUIRE(accumulator.snapshot().chunks.data.count == 1);
    REQUIRE(accumulator.snapshot().chunks.data.data_bytes == 5);
  }
  SECTION("finish refuses ready and complete without whole_document scope") {
    accumulator.finish(StatisticsSectionId::kBlocks, SectionStatus::kReady,
                       true, SectionScope::kVerifiedPrefix);
    REQUIRE(accumulator.snapshot().blocks.state.status ==
            SectionStatus::kPartial);
    REQUIRE_FALSE(accumulator.snapshot().blocks.state.complete);
    REQUIRE(accumulator.snapshot().blocks.state.scope ==
            SectionScope::kVerifiedPrefix);
    REQUIRE_FALSE(accumulator.snapshot().complete());
  }
  SECTION("the first non-ready error is preserved") {
    accumulator.finish(StatisticsSectionId::kTokens, SectionStatus::kCancelled,
                       false, SectionScope::kVerifiedPrefix, "first error");
    accumulator.finish(StatisticsSectionId::kTokens, SectionStatus::kError,
                       false, SectionScope::kNone, "second error");
    REQUIRE(accumulator.snapshot().tokens.state.status ==
            SectionStatus::kCancelled);
    REQUIRE(accumulator.snapshot().tokens.state.error == "first error");
    REQUIRE(accumulator.snapshot().tokens.state.scope ==
            SectionScope::kVerifiedPrefix);
  }
  SECTION("compression totals feed the overview section") {
    REQUIRE(accumulator.set_compression_totals(65, 120));
    accumulator.finish(StatisticsSectionId::kOverview, SectionStatus::kReady,
                       true, SectionScope::kWholeDocument);
    REQUIRE_FALSE(accumulator.set_compression_totals(1, 1));
    REQUIRE(accumulator.snapshot().overview.data.compressed_bytes == 65);
    REQUIRE(accumulator.snapshot().overview.data.has_compression_totals);
  }
}
