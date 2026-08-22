#include <pnga/analysis-engine/statistics_adapter.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Statistics adapter projects immutable analysis results",
          "[analysis-engine][wp602a]") {
  pnga::png_format::ChunkIndex chunks;
  pnga::png_format::ChunkNode idat;
  idat.type = {std::byte{'I'}, std::byte{'D'}, std::byte{'A'}, std::byte{'T'}};
  idat.data_length = 12;
  chunks.chunks.push_back(idat);

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

  const auto result = pnga::analysis_engine::collect_statistics(
      {&chunks, &stages, &blocks, &tokens});
  REQUIRE(result.complete());
  REQUIRE(result.chunk_count == 1);
  REQUIRE(result.chunk_data_bytes == 12);
  REQUIRE(result.filters[0].rows == 1);
  REQUIRE(result.blocks[1].blocks == 1);
  REQUIRE(result.tokens[0].count == 1);
  REQUIRE(result.tokens[1].count == 1);
  REQUIRE(result.compressed_bytes == 12);
  REQUIRE(result.inflated_bytes == 2);
}

TEST_CASE("Statistics adapter rejects inverted or out-of-bounds ranges",
          "[analysis-engine][wp602a]") {
  pnga::analysis_engine::StageSet stages;
  stages.success = true;
  stages.filtered = {std::byte{0}};
  stages.scanlines.push_back({1, 2});
  REQUIRE(pnga::analysis_engine::collect_statistics({nullptr, &stages})
              .status == pnga::statistics::BuildStatus::kInvalidInput);

  pnga::deflate_index::BlockIndexResult blocks;
  blocks.success = true;
  blocks.blocks.push_back({0, pnga::deflate_index::BlockType::kStored, false,
                           10, 9, 0, 0});
  REQUIRE(pnga::analysis_engine::collect_statistics({nullptr, nullptr, &blocks})
              .status == pnga::statistics::BuildStatus::kInvalidInput);
}

TEST_CASE("Statistics adapter propagates cancellation before materializing",
          "[analysis-engine][wp602a]") {
  pnga::png_format::ChunkIndex chunks;
  for (int i = 0; i < 512; ++i) {
    pnga::png_format::ChunkNode node;
    node.type = {std::byte{'I'}, std::byte{'D'}, std::byte{'A'}, std::byte{'T'}};
    chunks.chunks.push_back(node);
  }
  const auto result = pnga::analysis_engine::collect_statistics(
      {&chunks}, {}, [] { return true; });
  REQUIRE(result.status == pnga::statistics::BuildStatus::kCancelled);
}
