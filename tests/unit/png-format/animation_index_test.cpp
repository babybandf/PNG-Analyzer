#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "apng_fixture.h"

using pnga::io::MemoryByteSource;
using pnga::png_format::AnimationLimits;
using pnga::png_format::AnimationStatus;
using pnga::png_format::AnimationStop;
using pnga::png_format::FrameControl;
using pnga::png_format::index_animation;

namespace {

std::array<FrameControl, 2> two_frames() {
  return std::array<FrameControl, 2>{
      FrameControl{0, 1, 1, 0, 0, 1, 100, 0, 0},
      FrameControl{0, 1, 1, 0, 0, 2, 100, 0, 1},
  };
}

}  // namespace

TEST_CASE("Animation index parses default image as first frame",
          "[png-format][apng][wp700]") {
  const auto controls = two_frames();
  MemoryByteSource source(pnga_test::make_apng(true, controls));

  const auto result = index_animation(source, AnimationLimits{}, [] {
    return false;
  });

  REQUIRE(result.status == AnimationStatus::kComplete);
  REQUIRE(result.stop == AnimationStop::kNone);
  REQUIRE(result.control.has_value());
  REQUIRE(result.control->num_frames == 2);
  REQUIRE(result.default_is_frame);
  REQUIRE(result.frames.size() == 2);
  REQUIRE(result.frames[0].uses_idat);
  REQUIRE_FALSE(result.frames[1].uses_idat);
  REQUIRE(result.frames[0].data.size() == 1);
  REQUIRE(result.frames[1].data.size() == 1);
  REQUIRE(result.frames[1].data[0].length > 0);
}

TEST_CASE("Animation index preserves a separate static fallback",
          "[png-format][apng][wp700]") {
  const auto controls = two_frames();
  MemoryByteSource source(pnga_test::make_apng(false, controls));

  const auto result = index_animation(source, AnimationLimits{}, [] {
    return false;
  });

  REQUIRE(result.status == AnimationStatus::kComplete);
  REQUIRE_FALSE(result.default_is_frame);
  REQUIRE(result.frames.size() == 2);
  REQUIRE_FALSE(result.frames[0].uses_idat);
  REQUIRE(result.frames[0].data.size() == 1);
}

TEST_CASE("Animation index stops at a frame budget before unbounded growth",
          "[png-format][apng][wp700]") {
  const auto controls = two_frames();
  MemoryByteSource source(pnga_test::make_apng(true, controls));
  AnimationLimits limits;
  limits.max_frames = 1;

  const auto result = index_animation(source, limits, [] {
    return false;
  });

  REQUIRE(result.status == AnimationStatus::kPartial);
  REQUIRE(result.stop == AnimationStop::kBudget);
  REQUIRE(result.frames.size() == 1);
}

TEST_CASE("Animation index stops scanning when cancellation is requested",
          "[png-format][apng][wp700]") {
  const auto controls = two_frames();
  MemoryByteSource source(pnga_test::make_apng(true, controls));
  int checks = 0;

  const auto result = index_animation(source, AnimationLimits{}, [&checks] {
    return ++checks >= 2;
  });

  REQUIRE(result.status == AnimationStatus::kCancelled);
  REQUIRE(result.stop == AnimationStop::kCancelled);
  REQUIRE(checks == 2);
}
