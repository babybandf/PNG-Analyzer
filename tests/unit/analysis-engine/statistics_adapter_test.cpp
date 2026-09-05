#include <pnga/analysis-engine/statistics_adapter.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

auto cancel_on_call(int trigger_call) {
  return [trigger_call, calls = 0]() mutable { return ++calls >= trigger_call; };
}

bool ready_complete_whole_document(
    const pnga::statistics::SectionState& state) {
  return state.status == pnga::statistics::SectionStatus::kReady &&
         state.complete &&
         state.scope == pnga::statistics::SectionScope::kWholeDocument;
}

pnga::png_format::ChunkNode idat_node(std::uint64_t data_length) {
  pnga::png_format::ChunkNode node;
  node.type = {std::byte{'I'}, std::byte{'D'}, std::byte{'A'}, std::byte{'T'}};
  node.data_length = data_length;
  return node;
}

}  // namespace

TEST_CASE("Statistics adapter projects immutable analysis results",
          "[analysis-engine][wp602b]") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks.push_back(idat_node(12));

  pnga::analysis_engine::StageSet stages;
  stages.success = true;
  stages.filtered = {std::byte{0}, std::byte{1}, std::byte{2}};
  stages.scanlines.push_back({0, 3});

  pnga::deflate_index::BlockIndexResult blocks;
  blocks.success = true;
  blocks.total_output_bytes = 2;
  blocks.blocks.push_back({0, pnga::deflate_index::BlockType::kFixed, false,
                           10, 18, 0, 2});

  pnga::deflate_trace::TokenDecodeResult tokens;
  tokens.success = true;
  tokens.output_bytes = 2;
  tokens.tokens.push_back({pnga::deflate_trace::TokenKind::kLiteral, 3, 7, 0,
                           1, 0, 0, 0, 0});
  tokens.tokens.push_back({pnga::deflate_trace::TokenKind::kLengthDistance, 8,
                           12, 1, 2, 0, 4, 1, 0, 0});

  const auto snapshot = pnga::analysis_engine::collect_statistics(
      {&chunks, &stages, &blocks, &tokens});
  REQUIRE(snapshot.complete());
  REQUIRE(ready_complete_whole_document(snapshot.overview.state));
  REQUIRE(ready_complete_whole_document(snapshot.chunks.state));
  REQUIRE(snapshot.chunks.data.count == 1);
  REQUIRE(snapshot.chunks.data.data_bytes == 12);
  REQUIRE(ready_complete_whole_document(snapshot.filters.state));
  REQUIRE(snapshot.filters.data.rows == 1);
  REQUIRE(snapshot.filters.data.buckets[0].rows == 1);
  REQUIRE(ready_complete_whole_document(snapshot.blocks.state));
  REQUIRE(snapshot.blocks.data.buckets[1].blocks == 1);
  REQUIRE(ready_complete_whole_document(snapshot.tokens.state));
  REQUIRE(snapshot.tokens.data.buckets[0].count == 1);
  REQUIRE(snapshot.tokens.data.buckets[1].count == 1);
  REQUIRE(ready_complete_whole_document(snapshot.lengths.state));
  REQUIRE(ready_complete_whole_document(snapshot.distances.state));
  REQUIRE(snapshot.overview.data.compressed_bytes == 12);
  REQUIRE(snapshot.overview.data.inflated_bytes == 2);
  REQUIRE(snapshot.overview.data.has_compression_totals);
}

TEST_CASE("Missing sources stay unavailable and sources own their sections",
          "[analysis-engine][wp602b]") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks.push_back(idat_node(9));
  const auto snapshot =
      pnga::analysis_engine::collect_statistics({&chunks});

  REQUIRE(snapshot.chunks.state.status ==
          pnga::statistics::SectionStatus::kReady);
  REQUIRE(snapshot.chunks.state.complete);
  REQUIRE(snapshot.chunks.state.scope ==
          pnga::statistics::SectionScope::kWholeDocument);
  REQUIRE(snapshot.chunks.data.count == 1);
  REQUIRE(snapshot.overview.state.status ==
          pnga::statistics::SectionStatus::kReady);
  REQUIRE(snapshot.overview.data.compressed_bytes == 9);
  REQUIRE(snapshot.overview.data.has_compression_totals);
  REQUIRE(snapshot.filters.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
  REQUIRE(snapshot.blocks.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
  REQUIRE(snapshot.tokens.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
  REQUIRE(snapshot.lengths.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
  REQUIRE(snapshot.distances.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
  REQUIRE_FALSE(snapshot.complete());
}

TEST_CASE("Empty but present sources are ready with zero totals",
          "[analysis-engine][wp602b]") {
  pnga::analysis_engine::StageSet stages;
  stages.success = true;
  pnga::deflate_index::BlockIndexResult blocks;
  blocks.success = true;
  pnga::deflate_trace::TokenDecodeResult tokens;
  tokens.success = true;
  const auto snapshot = pnga::analysis_engine::collect_statistics(
      {nullptr, &stages, &blocks, &tokens});
  REQUIRE(snapshot.chunks.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
  REQUIRE(snapshot.filters.state.status ==
          pnga::statistics::SectionStatus::kReady);
  REQUIRE(snapshot.filters.data.rows == 0);
  REQUIRE(snapshot.blocks.state.status ==
          pnga::statistics::SectionStatus::kReady);
  REQUIRE(snapshot.tokens.state.status ==
          pnga::statistics::SectionStatus::kReady);
  REQUIRE(snapshot.lengths.state.status ==
          pnga::statistics::SectionStatus::kReady);
  REQUIRE(snapshot.distances.state.status ==
          pnga::statistics::SectionStatus::kReady);
  REQUIRE(snapshot.overview.state.status ==
          pnga::statistics::SectionStatus::kReady);
  REQUIRE(snapshot.overview.data.has_compression_totals);
  REQUIRE(snapshot.overview.data.inflated_bytes == 0);
}

TEST_CASE("Section failures stay confined to their owned sections",
          "[analysis-engine][wp602b]") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks.push_back(idat_node(9));

  pnga::analysis_engine::StageSet stages;
  stages.success = false;
  stages.error = "stage analysis failed";

  const auto snapshot =
      pnga::analysis_engine::collect_statistics({&chunks, &stages});
  REQUIRE(snapshot.chunks.state.status ==
          pnga::statistics::SectionStatus::kReady);
  REQUIRE(snapshot.filters.state.status ==
          pnga::statistics::SectionStatus::kInvalidInput);
  REQUIRE(snapshot.filters.state.error ==
          "cannot collect statistics from failed stage analysis");
  REQUIRE(snapshot.filters.state.scope ==
          pnga::statistics::SectionScope::kNone);
  REQUIRE(snapshot.blocks.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
  REQUIRE_FALSE(snapshot.complete());
}

TEST_CASE("Statistics adapter rejects inverted or out-of-bounds ranges",
          "[analysis-engine][wp602b]") {
  SECTION("scanline range outside the filtered buffer") {
    pnga::analysis_engine::StageSet stages;
    stages.success = true;
    stages.filtered = {std::byte{0}};
    stages.scanlines.push_back({1, 2});
    const auto snapshot =
        pnga::analysis_engine::collect_statistics({nullptr, &stages});
    REQUIRE(snapshot.filters.state.status ==
            pnga::statistics::SectionStatus::kInvalidInput);
    REQUIRE(snapshot.filters.state.error ==
            "filtered scanline range is outside its backing buffer");
  }
  SECTION("inverted block range") {
    pnga::deflate_index::BlockIndexResult blocks;
    blocks.success = true;
    blocks.blocks.push_back({0, pnga::deflate_index::BlockType::kStored, false,
                             10, 9, 0, 0});
    const auto snapshot = pnga::analysis_engine::collect_statistics(
        {nullptr, nullptr, &blocks});
    REQUIRE(snapshot.blocks.state.status ==
            pnga::statistics::SectionStatus::kInvalidInput);
    REQUIRE(snapshot.blocks.state.error == "Deflate block range is inverted");
  }
  SECTION("inverted token range") {
    pnga::deflate_trace::TokenDecodeResult tokens;
    tokens.success = true;
    tokens.tokens.push_back({pnga::deflate_trace::TokenKind::kLiteral, 7, 3, 0,
                             1, 0, 0, 0, 0});
    const auto snapshot = pnga::analysis_engine::collect_statistics(
        {nullptr, nullptr, nullptr, &tokens});
    REQUIRE(snapshot.tokens.state.status ==
            pnga::statistics::SectionStatus::kInvalidInput);
    REQUIRE(snapshot.lengths.state.status ==
            pnga::statistics::SectionStatus::kInvalidInput);
    REQUIRE(snapshot.distances.state.status ==
            pnga::statistics::SectionStatus::kInvalidInput);
    REQUIRE(snapshot.tokens.state.error == "Deflate token range is inverted");
  }
}

TEST_CASE("Statistics adapter rejects sample budget overruns",
          "[analysis-engine][wp602b]") {
  pnga::png_format::ChunkIndex chunks;
  for (int i = 0; i < 3; ++i) {
    chunks.chunks.push_back(idat_node(1));
  }
  pnga::statistics::StatisticsLimits limits;
  limits.max_samples = 2;
  const auto snapshot = pnga::analysis_engine::collect_statistics(
      {&chunks}, limits);
  REQUIRE(snapshot.chunks.state.status ==
          pnga::statistics::SectionStatus::kBudgetExceeded);
  REQUIRE(snapshot.chunks.state.error ==
          "statistics sample budget exceeded while adapting Chunks");
  REQUIRE(snapshot.chunks.data.count == 0);
}

TEST_CASE("Statistics adapter propagates cancellation before materializing",
          "[analysis-engine][wp602b]") {
  pnga::png_format::ChunkIndex chunks;
  for (int i = 0; i < 512; ++i) {
    chunks.chunks.push_back(idat_node(1));
  }
  const auto snapshot = pnga::analysis_engine::collect_statistics(
      {&chunks}, {}, cancel_on_call(1));
  REQUIRE(snapshot.chunks.state.status ==
          pnga::statistics::SectionStatus::kCancelled);
  REQUIRE(snapshot.chunks.data.count == 0);
  REQUIRE(snapshot.filters.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
  REQUIRE(snapshot.overview.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
}

TEST_CASE("Cancellation preserves adapted Chunk totals as verified_prefix",
          "[analysis-engine][wp602b]") {
  pnga::png_format::ChunkIndex chunks;
  for (int i = 0; i < 512; ++i) {
    chunks.chunks.push_back(idat_node(1));
  }
  const auto snapshot = pnga::analysis_engine::collect_statistics(
      {&chunks}, {}, cancel_on_call(2));
  REQUIRE(snapshot.chunks.state.status ==
          pnga::statistics::SectionStatus::kCancelled);
  REQUIRE(snapshot.chunks.state.scope ==
          pnga::statistics::SectionScope::kVerifiedPrefix);
  REQUIRE(snapshot.chunks.data.count == 256);
  REQUIRE(snapshot.overview.state.status ==
          pnga::statistics::SectionStatus::kUnavailable);
  REQUIRE_FALSE(snapshot.complete());
}
