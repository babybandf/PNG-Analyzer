// WP-5U9 bounded stage-pixel projection tests.

#include <pnga/analysis-engine/stage_pixel_process_view.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include "test_png_helpers.h"

using pnga::analysis_engine::StagePixelProcessStage;
using pnga::analysis_engine::StagePixelProcessStatus;
using pnga::analysis_engine::build_stage_pixel_process_view;
using pnga::analysis_engine::StageSet;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::VirtualIDATStream;
using namespace pnga_test;  // NOLINT: local fixture vocabulary

namespace {

StageSet stages_of(const EncodedPng& encoded) {
  MemoryByteSource source(encoded.png_bytes);
  const ChunkIndex index = pnga::png_format::index_chunks(source);
  return pnga::analysis_engine::analyze_stages(
      VirtualIDATStream(index), source, encoded.header);
}

}  // namespace

TEST_CASE("Stage pixel views expose distinct RGB8 stage facts",
          "[analysis-engine][wp5u9]") {
  const EncodedPng encoded = encode_png(5, 4, 8, 2, false, true);
  const StageSet stages = stages_of(encoded);
  REQUIRE(stages.success);

  const auto pixels = build_stage_pixel_process_view(
      stages, StagePixelProcessStage::kNative, 2, 2);
  REQUIRE(pixels.status == StagePixelProcessStatus::kReady);
  REQUIRE(pixels.channels.size() == 3);
  REQUIRE(pixels.channels[0].name == "R");
  REQUIRE(pixels.channels[1].name == "G");
  REQUIRE(pixels.channels[2].name == "B");
  for (const auto& channel : pixels.channels) {
    REQUIRE(channel.cells.size() == 15);
    REQUIRE(channel.cells[7].current);
    REQUIRE(channel.cells[7].in_bounds);
  }

  const auto filtered = build_stage_pixel_process_view(
      stages, StagePixelProcessStage::kFiltered, 2, 2);
  REQUIRE(filtered.status == StagePixelProcessStatus::kReady);
  REQUIRE(filtered.filter_byte == 0);
  REQUIRE(filtered.channels[0].cells[7].bytes.size() == 1);
  REQUIRE(filtered.channels[0].cells[7].bytes[0] ==
          static_cast<std::uint8_t>(encoded.filtered[2 * 16 + 1 + 2 * 3]));

  const auto defiltered = build_stage_pixel_process_view(
      stages, StagePixelProcessStage::kDefiltered, 2, 2);
  REQUIRE(defiltered.status == StagePixelProcessStatus::kReady);
  REQUIRE(defiltered.channels[0].cells[7].bytes[0] ==
          static_cast<std::uint8_t>(encoded.raw[2 * 15 + 2 * 3]));
  REQUIRE(defiltered.calculations.size() == 3);
  for (const auto& calculation : defiltered.calculations) {
    REQUIRE(calculation.has_raw);
    REQUIRE(calculation.has_recon);
    REQUIRE(calculation.recon == calculation.raw);
  }
}

TEST_CASE("Paeth projection carries actual dependency events",
          "[analysis-engine][wp5u9]") {
  const EncodedPng encoded = encode_png(8, 5, 8, 6, false, false);
  const StageSet stages = stages_of(encoded);
  REQUIRE(stages.success);
  const auto view = build_stage_pixel_process_view(
      stages, StagePixelProcessStage::kDefiltered, 3, 4);
  REQUIRE(view.status == StagePixelProcessStatus::kReady);
  REQUIRE(view.filter == pnga::png_reconstruction::FilterType::kPaeth);
  REQUIRE_FALSE(view.calculations.empty());
  for (const auto& calculation : view.calculations) {
    REQUIRE(calculation.has_a);
    REQUIRE(calculation.has_b);
    REQUIRE(calculation.has_c);
    REQUIRE(calculation.has_predictor);
    REQUIRE(calculation.has_recon);
  }
}

TEST_CASE("Stage pixel projection rejects missing and out-of-range data",
          "[analysis-engine][wp5u9]") {
  StageSet missing;
  const auto no_stage = build_stage_pixel_process_view(
      missing, StagePixelProcessStage::kNative, 0, 0);
  REQUIRE(no_stage.status == StagePixelProcessStatus::kNoStage);

  const EncodedPng encoded = encode_png(2, 2, 8, 0, false, true);
  const StageSet stages = stages_of(encoded);
  REQUIRE(stages.success);
  const auto outside = build_stage_pixel_process_view(
      stages, StagePixelProcessStage::kNative, 2, 0);
  REQUIRE(outside.status == StagePixelProcessStatus::kOutOfRange);
}
