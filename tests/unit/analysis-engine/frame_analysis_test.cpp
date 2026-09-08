#include <pnga/analysis-engine/frame_analysis.h>

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

#include "apng_fixture.h"

using pnga::analysis_engine::analyze_frame;
using pnga::analysis_engine::FrameRequest;
using pnga::analysis_engine::FrameResult;
using pnga::io::MemoryByteSource;
using pnga::png_format::AnimationLimits;
using pnga::png_format::FrameControl;
using pnga::png_format::index_animation;

namespace {

std::array<FrameControl, 2> controls() {
  return std::array<FrameControl, 2>{
      FrameControl{0, 1, 1, 0, 0, 1, 100, 0, 0},
      FrameControl{0, 1, 1, 0, 0, 2, 100, 0, 1},
  };
}

FrameRequest request_of(bool default_frame = false) {
  auto source = std::make_shared<const MemoryByteSource>(
      pnga_test::make_apng(default_frame, controls()));
  auto index = std::make_shared<const pnga::png_format::AnimationIndex>(
      index_animation(*source, AnimationLimits{}, [] { return false; }));
  FrameRequest request;
  request.generation = 7;
  request.request_serial = 11;
  request.ordinal = 1;
  request.source = std::move(source);
  request.index = std::move(index);
  request.canvas_header = pnga::png_reconstruction::ImageHeader{1, 1, 8, 6,
                                                                 false};
  request.limits.max_working_bytes = 64ull * 1024 * 1024;
  return request;
}

}  // namespace

TEST_CASE("Frame analysis returns an identified APNG result",
          "[analysis-engine][apng][wp702]") {
  const FrameRequest request = request_of();
  const FrameResult result = analyze_frame(request, nullptr);
  REQUIRE(result.stop == FrameResult::Stop::kReady);
  REQUIRE(result.generation == 7);
  REQUIRE(result.request_serial == 11);
  REQUIRE(result.identity ==
          pnga::trace_model::ImageIdentity{
              pnga::trace_model::AnimationFrame{1}});
  REQUIRE(result.frame != nullptr);
  REQUIRE(result.frame->control.width == 1);
  REQUIRE(result.frame->stages.header.width == 1);
  REQUIRE(result.frame->stages.success);
  REQUIRE(result.frame->delivered.pixels.size() == 4);
}

TEST_CASE("Frame analysis keeps generation and serial on cancellation",
          "[analysis-engine][apng][wp702]") {
  const FrameRequest request = request_of(true);
  pnga::analysis_engine::CancellationToken token;
  token.request_cancel();
  const FrameResult result = analyze_frame(request, &token);
  REQUIRE(result.stop == FrameResult::Stop::kCancelled);
  REQUIRE(result.generation == request.generation);
  REQUIRE(result.request_serial == request.request_serial);
  REQUIRE(result.frame == nullptr);
}

TEST_CASE("Frame analysis rejects an ordinal outside the verified prefix",
          "[analysis-engine][apng][wp702]") {
  FrameRequest request = request_of();
  request.ordinal = 2;
  const FrameResult result = analyze_frame(request, nullptr);
  REQUIRE(result.stop == FrameResult::Stop::kError);
  REQUIRE(result.frame == nullptr);
  REQUIRE_FALSE(result.error.empty());
}

