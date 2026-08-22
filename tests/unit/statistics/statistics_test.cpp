#include <pnga/statistics/statistics.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using pnga::statistics::BlockKind;
using pnga::statistics::BlockSample;
using pnga::statistics::BuildStatus;
using pnga::statistics::ChunkSample;
using pnga::statistics::FilterSample;
using pnga::statistics::StatisticsInput;
using pnga::statistics::TokenKind;
using pnga::statistics::TokenSample;

TEST_CASE("Statistics aggregate deterministic buckets and compression totals",
          "[statistics][wp602a]") {
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
  const auto result = pnga::statistics::collect(
      StatisticsInput{chunks, filters, blocks, tokens, 65, 120, true});

  REQUIRE(result.status == BuildStatus::kReady);
  REQUIRE(result.chunk_count == 3);
  REQUIRE(result.chunk_data_bytes == 133);
  REQUIRE(result.chunks.size() == 2);
  REQUIRE(result.chunks[0] == pnga::statistics::ChunkBucket{"IDAT", 2, 120});
  REQUIRE(result.chunks[1] == pnga::statistics::ChunkBucket{"IHDR", 1, 13});
  REQUIRE(result.filters[0].rows == 2);
  REQUIRE(result.filters[4].rows == 1);
  REQUIRE(result.blocks[1].blocks == 1);
  REQUIRE(result.blocks[2].output_bytes == 100);
  REQUIRE(result.tokens[0].count == 1);
  REQUIRE(result.tokens[1].count == 2);
  REQUIRE(result.tokens[2].count == 1);
  REQUIRE(result.lengths == std::vector<pnga::statistics::ValueBucket>{
                            {5, 2}});
  REQUIRE(result.distances == std::vector<pnga::statistics::ValueBucket>{{3, 2}});
  REQUIRE(result.compression_rate_per_mille() == 541);
}

TEST_CASE("Statistics preserve partial totals on cancellation",
          "[statistics][wp602a]") {
  std::array<ChunkSample, 512> samples{};
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i] = ChunkSample{"IDAT", 1};
  }
  const auto result = pnga::statistics::collect(
      StatisticsInput{samples, {}, {}, {}}, {}, [] { return true; });
  REQUIRE(result.status == BuildStatus::kCancelled);
  REQUIRE(result.chunk_count == 0);
  REQUIRE(result.chunks.empty());
}

TEST_CASE("Statistics reject unsafe bucket and arithmetic inputs",
          "[statistics][wp602a]") {
  const std::array chunks = {ChunkSample{"TOO-LONG", 1}};
  REQUIRE(pnga::statistics::collect(StatisticsInput{chunks, {}, {}, {}}).status ==
          BuildStatus::kInvalidInput);

  const std::array tokens = {
      TokenSample{TokenKind::kLengthDistance, 1, 1, 0, 2},
  };
  REQUIRE(pnga::statistics::collect(StatisticsInput{{}, {}, {}, tokens}).status ==
          BuildStatus::kInvalidInput);

  const std::array distinct = {ChunkSample{"A001", 1}, ChunkSample{"A002", 1}};
  pnga::statistics::StatisticsLimits limits;
  limits.max_chunk_types = 1;
  REQUIRE(pnga::statistics::collect(StatisticsInput{distinct, {}, {}, {}}, limits)
              .status == BuildStatus::kBudgetExceeded);
}

TEST_CASE("Statistics reject oversized sample spans before allocation",
          "[statistics][wp602a]") {
  const std::array samples = {ChunkSample{"IDAT", 1}, ChunkSample{"IEND", 0}};
  pnga::statistics::StatisticsLimits limits;
  limits.max_samples = 1;
  const auto result =
      pnga::statistics::collect(StatisticsInput{samples, {}, {}, {}}, limits);
  REQUIRE(result.status == BuildStatus::kBudgetExceeded);
  REQUIRE(result.chunk_count == 0);
}

TEST_CASE("Statistics reject addition overflow without wrapping",
          "[statistics][wp602a]") {
  const std::array chunks = {ChunkSample{"IDAT", UINT64_MAX},
                             ChunkSample{"IDAT", 1}};
  const auto result =
      pnga::statistics::collect(StatisticsInput{chunks, {}, {}, {}});
  REQUIRE(result.status == BuildStatus::kOverflow);
  REQUIRE(result.chunk_data_bytes == UINT64_MAX);
}
