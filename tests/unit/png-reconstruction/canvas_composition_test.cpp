#include <pnga/png-reconstruction/canvas_composition.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <vector>

using pnga::png_reconstruction::Blend;
using pnga::png_reconstruction::CompositionResult;
using pnga::png_reconstruction::Dispose;
using pnga::png_reconstruction::FrameRect;
using pnga::png_reconstruction::RgbaImage;
using pnga::png_reconstruction::blend_into;
using pnga::png_reconstruction::dispose_into;

TEST_CASE("SOURCE and OVER use independent RGBA oracle values",
          "[png-reconstruction][apng][wp703]") {
  RgbaImage canvas{1, 1, {0, 0, 255, 255}};
  RgbaImage frame{1, 1, {255, 0, 0, 128}};
  const auto result = blend_into(canvas, frame, FrameRect{0, 0, 1, 1},
                                 Blend::kOver, [] { return false; });
  REQUIRE(result.success);
  REQUIRE(canvas.pixels == std::vector<std::uint8_t>{128, 0, 127, 255});

  RgbaImage source_canvas{1, 1, {1, 2, 3, 4}};
  const auto source_result = blend_into(
      source_canvas, frame, FrameRect{0, 0, 1, 1}, Blend::kSource,
      [] { return false; });
  REQUIRE(source_result.success);
  REQUIRE(source_canvas.pixels == frame.pixels);
}

TEST_CASE("Canvas composition preserves rectangle outside and handles dispose",
          "[png-reconstruction][apng][wp703]") {
  RgbaImage canvas{2, 1, {10, 20, 30, 255, 40, 50, 60, 255}};
  RgbaImage frame{1, 1, {100, 110, 120, 255}};
  const auto blended = blend_into(canvas, frame, FrameRect{1, 0, 1, 1},
                                  Blend::kSource, [] { return false; });
  REQUIRE(blended.success);
  REQUIRE(canvas.pixels ==
          std::vector<std::uint8_t>{10, 20, 30, 255, 100, 110, 120, 255});

  const std::vector<std::uint8_t> saved = {10, 20, 30, 255};
  const auto restored = dispose_into(
      canvas, FrameRect{0, 0, 1, 1}, Dispose::kPrevious, saved, false,
      [] { return false; });
  REQUIRE(restored.success);
  REQUIRE(canvas.pixels ==
          std::vector<std::uint8_t>{10, 20, 30, 255, 100, 110, 120, 255});

  const auto cleared = dispose_into(
      canvas, FrameRect{1, 0, 1, 1}, Dispose::kBackground, {}, false,
      [] { return false; });
  REQUIRE(cleared.success);
  REQUIRE(canvas.pixels ==
          std::vector<std::uint8_t>{10, 20, 30, 255, 0, 0, 0, 0});
}

TEST_CASE("First PREVIOUS dispose clears and invalid rectangles fail",
          "[png-reconstruction][apng][wp703]") {
  RgbaImage canvas{1, 1, {9, 8, 7, 6}};
  const auto first = dispose_into(canvas, FrameRect{0, 0, 1, 1},
                                  Dispose::kPrevious, {}, true,
                                  [] { return false; });
  REQUIRE(first.success);
  REQUIRE(canvas.pixels == std::vector<std::uint8_t>{0, 0, 0, 0});

  RgbaImage frame{2, 1, {0, 0, 0, 255, 0, 0, 0, 255}};
  const auto invalid = blend_into(canvas, frame, FrameRect{0, 0, 2, 1},
                                   Blend::kSource, [] { return false; });
  REQUIRE_FALSE(invalid.success);
  REQUIRE_FALSE(invalid.error.empty());
}

TEST_CASE("Canvas composition observes cancellation between rows",
          "[png-reconstruction][apng][wp703]") {
  RgbaImage canvas{1, 3, std::vector<std::uint8_t>(12, 1)};
  RgbaImage frame{1, 3, std::vector<std::uint8_t>(12, 2)};
  int checks = 0;
  const auto result = blend_into(canvas, frame, FrameRect{0, 0, 1, 3},
                                 Blend::kSource,
                                 [&checks] { return ++checks > 2; });
  REQUIRE_FALSE(result.success);
  REQUIRE(result.cancelled);
}
