// WP-602F: immutable statistics view row tests. The projection turns a
// StatisticsSnapshot into fixed, deterministic page rows with stable ids,
// raw integers, units, the owning section state and optional typed
// navigation requests carrying the view generation and the frozen budgets.

#include <pnga/analysis-engine/statistics_view.h>

#include <catch2/catch_test_macros.hpp>

#include <pnga/statistics/statistics.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using pnga::analysis_engine::OccurrenceDirection;
using pnga::analysis_engine::StatisticsBucketDomain;
using pnga::analysis_engine::StatisticsNavigationRequest;
using pnga::analysis_engine::StatisticsRow;
using pnga::analysis_engine::StatisticsView;
using pnga::analysis_engine::build_statistics_view;
using pnga::statistics::BlockKind;
using pnga::statistics::BlockSample;
using pnga::statistics::ChunkSample;
using pnga::statistics::FilterSample;
using pnga::statistics::SectionStatus;
using pnga::statistics::StatisticsAccumulator;
using pnga::statistics::StatisticsSnapshot;
using pnga::statistics::TokenKind;
using pnga::statistics::TokenSample;

// Builds a fully ready snapshot from hand-built scalar samples: two chunk
// types, filter types 0 and 4, one block of each kind, one literal, one
// match (length 258, distance 32768) and one end-of-block.
StatisticsSnapshot make_ready_snapshot() {
  StatisticsAccumulator accumulator;
  REQUIRE(accumulator.add(ChunkSample{"IDAT", 120}));
  REQUIRE(accumulator.add(ChunkSample{"IHDR", 13}));
  REQUIRE(accumulator.add(FilterSample{0, 20}));
  REQUIRE(accumulator.add(FilterSample{4, 10}));
  REQUIRE(accumulator.add(BlockSample{BlockKind::kStored, 10, 5}));
  REQUIRE(accumulator.add(BlockSample{BlockKind::kFixed, 20, 7}));
  REQUIRE(accumulator.add(BlockSample{BlockKind::kDynamic, 30, 9}));
  REQUIRE(accumulator.add(TokenSample{TokenKind::kLiteral, 8, 1, 0, 0}));
  REQUIRE(accumulator.add(
      TokenSample{TokenKind::kLengthDistance, 15, 258, 258, 32768}));
  REQUIRE(accumulator.add(TokenSample{TokenKind::kEndOfBlock, 7, 0, 0, 0}));
  REQUIRE(accumulator.set_compression_totals(133, 270));
  for (const auto id : {pnga::statistics::StatisticsSectionId::kOverview,
                        pnga::statistics::StatisticsSectionId::kChunks,
                        pnga::statistics::StatisticsSectionId::kFilters,
                        pnga::statistics::StatisticsSectionId::kBlocks,
                        pnga::statistics::StatisticsSectionId::kTokens,
                        pnga::statistics::StatisticsSectionId::kLengths,
                        pnga::statistics::StatisticsSectionId::kDistances}) {
    accumulator.finish(id, SectionStatus::kReady, true,
                       pnga::statistics::SectionScope::kWholeDocument, "");
  }
  return accumulator.snapshot();
}

const StatisticsRow* find_row(const std::vector<StatisticsRow>& rows,
                              const std::string& id) {
  for (const StatisticsRow& row : rows) {
    if (row.id == id) {
      return &row;
    }
  }
  return nullptr;
}

// Requires one navigation request with the frozen contracts.
void require_navigation(const StatisticsRow& row,
                        std::uint64_t generation,
                        StatisticsBucketDomain domain,
                        const std::string& key) {
  REQUIRE(row.navigation.has_value());
  const StatisticsNavigationRequest& request = *row.navigation;
  REQUIRE(request.generation == generation);
  REQUIRE(request.domain == domain);
  REQUIRE(request.key == key);
  REQUIRE(request.direction == OccurrenceDirection::kFirst);
  REQUIRE_FALSE(request.after_output_offset.has_value());
  REQUIRE(request.max_tokens == 4096);
  REQUIRE(request.max_input_bytes == 8ull << 20);
}

}  // namespace

TEST_CASE("Statistics view projects stable ready rows in fixed order",
          "[analysis-engine][wp602f]") {
  const StatisticsSnapshot snapshot = make_ready_snapshot();
  const StatisticsView view = build_statistics_view(41, snapshot);
  REQUIRE(view.generation == 41);

  // Overview page: fixed totals rows with raw integer values.
  REQUIRE(view.overview.size() == 2);
  REQUIRE(view.overview[0].id == "overview.compressed_bytes");
  REQUIRE(view.overview[0].group == "overview");
  REQUIRE(view.overview[0].value == 133);
  REQUIRE(view.overview[0].unit == "bytes");
  REQUIRE(view.overview[0].state.status == SectionStatus::kReady);
  REQUIRE_FALSE(view.overview[0].navigation.has_value());
  REQUIRE(view.overview[1].id == "overview.inflated_bytes");
  REQUIRE(view.overview[1].value == 270);
  REQUIRE(view.overview[1].unit == "bytes");

  // Chunks page: totals first, then the buckets in snapshot order.
  REQUIRE(view.chunks.size() == 4);
  REQUIRE(view.chunks[0].id == "chunks.count");
  REQUIRE(view.chunks[0].group == "chunks");
  REQUIRE(view.chunks[0].value == 2);
  REQUIRE(view.chunks[0].unit == "chunks");
  REQUIRE(view.chunks[1].id == "chunks.data_bytes");
  REQUIRE(view.chunks[1].value == 133);
  REQUIRE(view.chunks[1].unit == "bytes");
  REQUIRE(view.chunks[2].id == "chunks.IDAT");
  REQUIRE(view.chunks[2].label == "IDAT");
  REQUIRE(view.chunks[2].value == 1);
  REQUIRE(view.chunks[2].unit == "chunks");
  REQUIRE(view.chunks[2].state.status == SectionStatus::kReady);
  require_navigation(view.chunks[2], 41, StatisticsBucketDomain::kChunkType,
                     "IDAT");
  REQUIRE(view.chunks[3].id == "chunks.IHDR");
  REQUIRE(view.chunks[3].value == 1);
  require_navigation(view.chunks[3], 41, StatisticsBucketDomain::kChunkType,
                     "IHDR");

  // Filters page: three totals and the five fixed filter buckets.
  REQUIRE(view.filters.size() == 8);
  REQUIRE(view.filters[0].id == "filters.rows");
  REQUIRE(view.filters[0].value == 2);
  REQUIRE(view.filters[0].unit == "rows");
  REQUIRE(view.filters[1].id == "filters.data_bytes");
  REQUIRE(view.filters[1].value == 30);
  REQUIRE(view.filters[1].unit == "bytes");
  REQUIRE(view.filters[2].id == "filters.invalid_rows");
  REQUIRE(view.filters[2].value == 0);
  REQUIRE(view.filters[2].unit == "rows");
  for (std::size_t i = 0; i < 5; ++i) {
    const std::string id = "filters." + std::to_string(i);
    const StatisticsRow* row = find_row(view.filters, id);
    REQUIRE(row != nullptr);
    REQUIRE(row->state.status == SectionStatus::kReady);
    if (i == 0) {
      REQUIRE(row->value == 1);
    } else if (i == 4) {
      REQUIRE(row->value == 1);
    } else {
      REQUIRE(row->value == 0);
    }
    REQUIRE(row->unit == "rows");
    require_navigation(*row, 41, StatisticsBucketDomain::kFilterType,
                       std::to_string(i));
  }

  // DEFLATE page: blocks, tokens, lengths and distances in one fixed vector.
  REQUIRE(view.deflate.size() == 14);
  REQUIRE(view.deflate[0].id == "blocks.count");
  REQUIRE(view.deflate[0].group == "blocks");
  REQUIRE(view.deflate[0].value == 3);
  REQUIRE(view.deflate[0].unit == "blocks");
  REQUIRE(view.deflate[1].id == "blocks.compressed_bits");
  REQUIRE(view.deflate[1].value == 60);
  REQUIRE(view.deflate[1].unit == "bits");
  REQUIRE(view.deflate[2].id == "blocks.output_bytes");
  REQUIRE(view.deflate[2].value == 21);
  REQUIRE(view.deflate[2].unit == "bytes");
  REQUIRE(view.deflate[3].id == "blocks.stored");
  REQUIRE(view.deflate[3].label == "Stored blocks");
  REQUIRE(view.deflate[3].value == 1);
  require_navigation(view.deflate[3], 41, StatisticsBucketDomain::kBlockType,
                     "stored");
  REQUIRE(view.deflate[4].id == "blocks.fixed");
  REQUIRE(view.deflate[4].value == 1);
  require_navigation(view.deflate[4], 41, StatisticsBucketDomain::kBlockType,
                     "fixed");
  REQUIRE(view.deflate[5].id == "blocks.dynamic");
  REQUIRE(view.deflate[5].value == 1);
  require_navigation(view.deflate[5], 41, StatisticsBucketDomain::kBlockType,
                     "dynamic");
  REQUIRE(view.deflate[6].id == "tokens.count");
  REQUIRE(view.deflate[6].group == "tokens");
  REQUIRE(view.deflate[6].value == 3);
  REQUIRE(view.deflate[6].unit == "tokens");
  REQUIRE(view.deflate[7].id == "tokens.input_bits");
  REQUIRE(view.deflate[7].value == 30);
  REQUIRE(view.deflate[7].unit == "bits");
  REQUIRE(view.deflate[8].id == "tokens.output_bytes");
  REQUIRE(view.deflate[8].value == 259);
  REQUIRE(view.deflate[8].unit == "bytes");
  REQUIRE(view.deflate[9].id == "tokens.literal");
  REQUIRE(view.deflate[9].label == "Literal tokens");
  REQUIRE(view.deflate[9].value == 1);
  require_navigation(view.deflate[9], 41, StatisticsBucketDomain::kTokenKind,
                     "literal");
  REQUIRE(view.deflate[10].id == "tokens.match");
  REQUIRE(view.deflate[10].label == "Match tokens");
  REQUIRE(view.deflate[10].value == 1);
  require_navigation(view.deflate[10], 41, StatisticsBucketDomain::kTokenKind,
                     "match");
  REQUIRE(view.deflate[11].id == "tokens.eob");
  REQUIRE(view.deflate[11].value == 1);
  require_navigation(view.deflate[11], 41, StatisticsBucketDomain::kTokenKind,
                     "eob");
  REQUIRE(view.deflate[12].id == "lengths.258");
  REQUIRE(view.deflate[12].group == "lengths");
  REQUIRE(view.deflate[12].label == "Length 258");
  REQUIRE(view.deflate[12].value == 1);
  REQUIRE(view.deflate[12].unit == "matches");
  require_navigation(view.deflate[12], 41, StatisticsBucketDomain::kLength,
                     "258");
  REQUIRE(view.deflate[13].id == "distances.32768");
  REQUIRE(view.deflate[13].group == "distances");
  REQUIRE(view.deflate[13].label == "Distance 32768");
  REQUIRE(view.deflate[13].value == 1);
  REQUIRE(view.deflate[13].unit == "matches");
  require_navigation(view.deflate[13], 41, StatisticsBucketDomain::kDistance,
                     "32768");
  // The last deflate row is the final distances row.
  REQUIRE(view.deflate[13].id == "distances.32768");
}

TEST_CASE("Statistics view keeps raw integers without locale formatting",
          "[analysis-engine][wp602f]") {
  StatisticsAccumulator accumulator;
  const std::uint64_t large = 12'345'678'901'234'567ull;
  REQUIRE(accumulator.set_compression_totals(large, large));
  accumulator.finish(pnga::statistics::StatisticsSectionId::kOverview,
                     SectionStatus::kReady, true,
                     pnga::statistics::SectionScope::kWholeDocument, "");
  const StatisticsView view = build_statistics_view(
      0, accumulator.snapshot());
  REQUIRE(view.overview[0].value == large);
  REQUIRE(view.overview[1].value == large);
}

TEST_CASE("Unavailable sections keep their state and lose no rows",
          "[analysis-engine][wp602f]") {
  const StatisticsSnapshot snapshot;  // every section unavailable
  const StatisticsView view = build_statistics_view(7, snapshot);

  REQUIRE(view.overview.size() == 2);
  for (const StatisticsRow& row : view.overview) {
    REQUIRE(row.value == std::nullopt);
    REQUIRE(row.state.status == SectionStatus::kUnavailable);
    REQUIRE_FALSE(row.navigation.has_value());
  }
  // Fixed totals rows still exist; no bucket rows exist without evidence.
  REQUIRE(view.chunks.size() == 2);
  REQUIRE(view.chunks[0].id == "chunks.count");
  REQUIRE(view.chunks[0].state.status == SectionStatus::kUnavailable);
  REQUIRE(view.chunks[0].value == std::nullopt);
  // The five fixed filter rows remain with unavailable state and no
  // navigation.
  REQUIRE(view.filters.size() == 8);
  REQUIRE(find_row(view.filters, "filters.4") != nullptr);
  for (const StatisticsRow& row : view.filters) {
    REQUIRE(row.state.status == SectionStatus::kUnavailable);
    REQUIRE(row.value == std::nullopt);
    REQUIRE_FALSE(row.navigation.has_value());
  }
  // Blocks and tokens keep their three fixed bucket rows unavailable.
  REQUIRE(view.deflate.size() == 12);
  REQUIRE(find_row(view.deflate, "blocks.dynamic") != nullptr);
  REQUIRE(find_row(view.deflate, "tokens.match") != nullptr);
  for (const StatisticsRow& row : view.deflate) {
    REQUIRE(row.state.status == SectionStatus::kUnavailable);
    REQUIRE(row.value == std::nullopt);
    REQUIRE_FALSE(row.navigation.has_value());
  }
}

TEST_CASE("Partial sections keep verified values and navigation",
          "[analysis-engine][wp602f]") {
  using pnga::statistics::SectionScope;
  StatisticsAccumulator accumulator;
  REQUIRE(accumulator.add(ChunkSample{"IDAT", 64}));
  accumulator.finish(pnga::statistics::StatisticsSectionId::kChunks,
                     SectionStatus::kCancelled, false,
                     SectionScope::kVerifiedPrefix, "cancelled");
  const StatisticsView view = build_statistics_view(
      9, accumulator.snapshot());
  REQUIRE(view.chunks[0].id == "chunks.count");
  REQUIRE(view.chunks[0].value == 1);
  REQUIRE(view.chunks[0].state.status == SectionStatus::kCancelled);
  REQUIRE(view.chunks[2].id == "chunks.IDAT");
  REQUIRE(view.chunks[2].value == 1);
  REQUIRE(view.chunks[2].state.status == SectionStatus::kCancelled);
  require_navigation(view.chunks[2], 9, StatisticsBucketDomain::kChunkType,
                     "IDAT");
}
