#include <pnga/analysis-engine/reconstruct_view_model.h>

#include <catch2/catch_test_macros.hpp>

namespace {

pnga::analysis_engine::StageSet rgb_stage() {
  pnga::analysis_engine::StageSet stages;
  stages.success = true;
  stages.header = pnga::png_reconstruction::ImageHeader{4, 1, 8, 2, false};
  stages.scanlines = {{0, 13}};
  stages.filtered.resize(13);
  stages.filtered[0] = std::byte{1};
  for (std::size_t i = 1; i < stages.filtered.size(); ++i) {
    stages.filtered[i] = std::byte{static_cast<unsigned char>(i)};
  }
  stages.unfiltered.assign(stages.filtered.begin() + 1, stages.filtered.end());
  stages.passes.push_back({0, stages.unfiltered});
  stages.native.width = 4;
  stages.native.height = 1;
  stages.native.channels = 3;
  return stages;
}

}  // namespace

TEST_CASE("reconstruct view model exposes bounded filter steps") {
  const auto stages = rgb_stage();
  const auto view = pnga::analysis_engine::build_reconstruct_view(
      stages, 1, 0, 1);
  REQUIRE(view.status == pnga::analysis_engine::ReconstructStatus::kReady);
  REQUIRE(view.pass == 0);
  REQUIRE(view.pass_x == 1);
  REQUIRE(view.pass_y == 0);
  REQUIRE(view.stream_row == 0);
  REQUIRE(view.sample_index == 3);
  REQUIRE(view.selected_byte == 3);
  REQUIRE(view.filtered_byte_offset == 4);
  REQUIRE(view.unfiltered_byte_offset == 3);
  REQUIRE(view.steps.size() == 3);
  REQUIRE(view.steps[1].index == 3);
  REQUIRE(view.steps[1].type == pnga::png_reconstruction::FilterType::kSub);
  REQUIRE(view.steps[1].recon == 5);
}

TEST_CASE("reconstruct view model reports stable coordinate and stage errors") {
  auto stages = rgb_stage();
  stages.success = false;
  auto view = pnga::analysis_engine::build_reconstruct_view(stages, 0, 0);
  REQUIRE(view.status == pnga::analysis_engine::ReconstructStatus::kNoStage);

  stages = rgb_stage();
  view = pnga::analysis_engine::build_reconstruct_view(stages, 4, 0);
  REQUIRE(view.status == pnga::analysis_engine::ReconstructStatus::kOutOfRange);
}

TEST_CASE("reconstruct view model handles packed sample byte boundaries") {
  pnga::analysis_engine::StageSet stages;
  stages.success = true;
  stages.header = pnga::png_reconstruction::ImageHeader{4, 1, 4, 0, false};
  stages.scanlines = {{0, 3}};
  stages.filtered = {std::byte{0}, std::byte{0x12}, std::byte{0x34}};
  stages.unfiltered = {std::byte{0x12}, std::byte{0x34}};
  stages.passes.push_back({0, stages.unfiltered});
  const auto view = pnga::analysis_engine::build_reconstruct_view(stages, 2, 0);
  REQUIRE(view.status == pnga::analysis_engine::ReconstructStatus::kReady);
  REQUIRE(view.selected_byte == 1);
  REQUIRE(view.filtered_byte_offset == 2);
}
