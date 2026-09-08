#include <pnga/io/byte_source.h>
#include <pnga/validation/animation.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "apng_fixture.h"

using pnga::io::MemoryByteSource;
using pnga::png_format::AnimationLimits;
using pnga::png_format::FrameControl;
using pnga::png_format::index_animation;
using pnga::validation::ValidationReport;
using pnga::validation::validate_animation;

namespace {

std::array<FrameControl, 2> controls() {
  return std::array<FrameControl, 2>{
      FrameControl{0, 1, 1, 0, 0, 1, 100, 0, 0},
      FrameControl{0, 1, 1, 0, 0, 2, 100, 0, 1},
  };
}

bool has_rule(const ValidationReport& report, const std::string& rule) {
  for (const auto& issue : report.issues) {
    if (issue.rule_id == rule) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("A valid APNG index produces a stable empty validation report",
          "[validation][apng][wp700]") {
  MemoryByteSource source(pnga_test::make_apng(false, controls()));
  const auto index = index_animation(source, AnimationLimits{}, [] {
    return false;
  });
  const auto report = validate_animation(index);
  REQUIRE(report.valid());
}

TEST_CASE("A sequence gap preserves the verified frame prefix",
          "[validation][apng][wp700]") {
  auto bytes = pnga_test::make_apng(false, controls());
  // IHDR, acTL, fallback IDAT, first fcTL, first fdAT, second fcTL.
  pnga_test::set_sequence(bytes, 5, 9);
  MemoryByteSource source(std::move(bytes));
  const auto index = index_animation(source, AnimationLimits{}, [] {
    return false;
  });
  REQUIRE(index.frames.size() == 1);
  REQUIRE(index.status == pnga::png_format::AnimationStatus::kPartial);
  REQUIRE(index.stop == pnga::png_format::AnimationStop::kFormat);

  const auto report = validate_animation(index);
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "apng.sequence"));
  REQUIRE(report.issues.front().offset > 0);
}

TEST_CASE("Invalid blend and dispose values have stable frame control rules",
          "[validation][apng][wp700]") {
  auto invalid = controls();
  invalid[0].dispose = 3;
  invalid[0].blend = 2;
  MemoryByteSource source(pnga_test::make_apng(true, invalid));
  const auto index = index_animation(source, AnimationLimits{}, [] {
    return false;
  });
  const auto report = validate_animation(index);
  REQUIRE(has_rule(report, "apng.frame.control"));
}
