#include <pnga/analysis-engine/animation_replay.h>

#include <pnga/analysis-engine/job_scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

#include "apng_fixture.h"

using pnga::analysis_engine::AnimationReplay;
using pnga::analysis_engine::FrameRequest;
using pnga::analysis_engine::ReplayRequest;
using pnga::analysis_engine::ReplayResult;
using pnga::io::MemoryByteSource;
using pnga::png_format::AnimationLimits;
using pnga::png_format::FrameControl;
using pnga::png_format::index_animation;
using pnga::trace_model::Stage;

namespace {

std::array<FrameControl, 2> controls() {
  return std::array<FrameControl, 2>{
      FrameControl{0, 1, 1, 0, 0, 1, 100, 0, 0},
      FrameControl{0, 1, 1, 0, 0, 2, 100, 0, 1},
  };
}

FrameRequest make_request() {
  auto source = std::make_shared<const MemoryByteSource>(
      pnga_test::make_apng(false, controls()));
  auto index = std::make_shared<const pnga::png_format::AnimationIndex>(
      index_animation(*source, AnimationLimits{}, [] { return false; }));
  FrameRequest frame;
  frame.generation = 3;
  frame.request_serial = 8;
  frame.ordinal = 1;
  frame.source = std::move(source);
  frame.index = std::move(index);
  frame.canvas_header = {1, 1, 8, 6, false};
  return frame;
}

}  // namespace

TEST_CASE("Animation replay materializes frame and canvas stages",
          "[analysis-engine][apng][wp703]") {
  AnimationReplay replay(64);
  ReplayRequest replay_request{make_request(), Stage::kFrameOutput};
  const auto frame = replay.materialize(replay_request, nullptr);
  REQUIRE(frame.stop == ReplayResult::Stop::kReady);
  REQUIRE(frame.stage == Stage::kFrameOutput);
  REQUIRE(frame.image != nullptr);
  REQUIRE(frame.image->pixels.size() == 4);

  replay_request.requested_stage = Stage::kPostDispose;
  const auto canvas = replay.materialize(replay_request, nullptr);
  REQUIRE(canvas.stop == ReplayResult::Stop::kReady);
  REQUIRE(canvas.image != nullptr);
  REQUIRE(canvas.image->width == 1);
  REQUIRE(replay.retained_bytes() <= 64);
}

TEST_CASE("Animation replay rejects an artifact larger than its cache budget",
          "[analysis-engine][apng][wp703]") {
  AnimationReplay replay(3);
  const ReplayRequest replay_request{make_request(), Stage::kFrameOutput};
  const auto result = replay.materialize(replay_request, nullptr);
  REQUIRE(result.stop == ReplayResult::Stop::kPartial);
  REQUIRE(result.image == nullptr);
}

TEST_CASE("Animation replay cancellation does not publish a canvas",
          "[analysis-engine][apng][wp703]") {
  AnimationReplay replay(64);
  const ReplayRequest replay_request{make_request(), Stage::kPostBlend};
  pnga::analysis_engine::CancellationToken token;
  token.request_cancel();
  const auto result = replay.materialize(replay_request, &token);
  REQUIRE(result.stop == ReplayResult::Stop::kCancelled);
  REQUIRE(result.image == nullptr);
}
