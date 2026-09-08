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

// WP-706: deterministic rect-arithmetic fuzz. The seeded generator walks
// canvas sizes and frame rectangles (including ones hanging off the canvas
// edges) through blend/dispose and asserts the stable-contract outcomes:
// either a clean success with only rect bytes touched, or a rejection —
// never a crash, an uncontrolled write, or an aliasing artifact.

#include <algorithm>
#include <random>

namespace {

RgbaImage make_canvas(std::uint32_t width, std::uint32_t height,
                      std::uint8_t fill) {
  RgbaImage canvas;
  canvas.width = width;
  canvas.height = height;
  canvas.pixels.assign(static_cast<std::size_t>(width) * height * 4, fill);
  return canvas;
}

void require_outside_untouched(
    const RgbaImage& canvas, const std::vector<std::uint8_t>& before,
    std::uint32_t canvas_width, std::uint32_t canvas_height,
    const FrameRect& rect) {
  for (std::uint32_t y = 0; y < canvas_height; ++y) {
    for (std::uint32_t x = 0; x < canvas_width; ++x) {
      const std::size_t offset =
          (static_cast<std::size_t>(y) * canvas_width + x) * 4;
      const bool inside = x >= rect.x && x < rect.x + rect.width &&
                          y >= rect.y && y < rect.y + rect.height;
      if (!inside) {
        REQUIRE(canvas.pixels[offset] == before[offset]);
        REQUIRE(canvas.pixels[offset + 1] == before[offset + 1]);
        REQUIRE(canvas.pixels[offset + 2] == before[offset + 2]);
        REQUIRE(canvas.pixels[offset + 3] == before[offset + 3]);
      }
    }
  }
}

}  // namespace

TEST_CASE("Composition rect arithmetic survives deterministic fuzz",
          "[png-reconstruction][apng][wp703][fuzz]") {
  std::mt19937_64 rng{20260908};
  auto pick = [&rng](std::uint32_t low, std::uint32_t high) {
    return std::uniform_int_distribution<std::uint32_t>(low, high)(rng);
  };

  std::uint64_t accepted = 0;
  std::uint64_t rejected = 0;
  for (int iteration = 0; iteration < 4000; ++iteration) {
    const std::uint32_t canvas_width = pick(1, 37);
    const std::uint32_t canvas_height = pick(1, 37);
    // Rect coordinates deliberately range past the canvas so the checked
    // arithmetic rejections are exercised as often as the accepted paths.
    const std::uint32_t rect_x = pick(0, canvas_width + 4);
    const std::uint32_t rect_y = pick(0, canvas_height + 4);
    const std::uint32_t rect_width = pick(1, canvas_width + 4);
    const std::uint32_t rect_height = pick(1, canvas_height + 4);
    const bool rect_inside_canvas =
        rect_x <= canvas_width && rect_y <= canvas_height &&
        rect_width <= canvas_width - rect_x &&
        rect_height <= canvas_height - rect_y;
    const FrameRect rect{rect_x, rect_y, rect_width, rect_height};

    auto canvas = make_canvas(canvas_width, canvas_height,
                              static_cast<std::uint8_t>(iteration % 251 + 1));
    const std::vector<std::uint8_t> before = canvas.pixels;
    auto frame = make_canvas(rect.width, rect.height,
                             static_cast<std::uint8_t>(iteration % 249 + 2));

    // PREVIOUS dispose restores the pre-blend rectangle, so the production
    // caller snapshots it before blending; the fuzz does the same.
    std::vector<std::uint8_t> saved;
    if (rect_inside_canvas) {
      saved.resize(static_cast<std::size_t>(rect.width) * rect.height * 4);
      for (std::uint32_t y = 0; y < rect.height; ++y) {
        const std::size_t src =
            (static_cast<std::size_t>(rect.y + y) * canvas_width + rect.x) * 4;
        std::copy_n(before.begin() + static_cast<long>(src),
                    static_cast<std::size_t>(rect.width) * 4,
                    saved.begin() +
                        static_cast<std::size_t>(y) * rect.width * 4);
      }
    }

    const auto blend = iteration % 2 == 0 ? Blend::kSource : Blend::kOver;
    const auto blend_result =
        blend_into(canvas, frame, rect, blend, [] { return false; });
    if (!rect_inside_canvas) {
      REQUIRE_FALSE(blend_result.success);
      ++rejected;
      REQUIRE(canvas.pixels == before);
      continue;
    }
    REQUIRE(blend_result.success);
    ++accepted;
    require_outside_untouched(canvas, before, canvas_width, canvas_height,
                              rect);

    const auto dispose = static_cast<Dispose>(iteration % 3);
    const auto dispose_result = dispose_into(canvas, rect, dispose, saved,
                                             false, [] { return false; });
    REQUIRE(dispose_result.success);
    require_outside_untouched(canvas, before, canvas_width, canvas_height,
                              rect);
    if (dispose == Dispose::kBackground) {
      for (std::uint32_t y = 0; y < rect.height; ++y) {
        for (std::uint32_t x = 0; x < rect.width; ++x) {
          const std::size_t offset =
              (static_cast<std::size_t>(rect.y + y) * canvas_width + rect.x +
               x) * 4;
          REQUIRE(canvas.pixels[offset + 3] == 0);
        }
      }
    }
    if (dispose == Dispose::kPrevious) {
      for (std::uint32_t y = 0; y < rect.height; ++y) {
        for (std::uint32_t x = 0; x < rect.width; ++x) {
          const std::size_t offset =
              (static_cast<std::size_t>(rect.y + y) * canvas_width + rect.x +
               x) * 4;
          const std::size_t saved_offset =
              (static_cast<std::size_t>(y) * rect.width + x) * 4;
          REQUIRE(canvas.pixels[offset] == saved[saved_offset]);
          REQUIRE(canvas.pixels[offset + 1] == saved[saved_offset + 1]);
          REQUIRE(canvas.pixels[offset + 2] == saved[saved_offset + 2]);
          REQUIRE(canvas.pixels[offset + 3] == saved[saved_offset + 3]);
        }
      }
    }
  }
  REQUIRE(accepted > 0);
  REQUIRE(rejected > 0);
}
