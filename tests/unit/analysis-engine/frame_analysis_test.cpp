#include <pnga/analysis-engine/frame_analysis.h>
#include <pnga/analysis-engine/analysis_target.h>

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <memory>

#include "apng_fixture.h"
#include "apng_inspection_fixture.h"

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

// --- WP-706 T8 format matrix ------------------------------------------------

#include <string>

namespace {

using pnga::analysis_engine::delivery_context_from;
using pnga::png_reconstruction::DeliveryContext;

struct FormatCase {
  std::uint8_t color_type;
  std::uint8_t bit_depth;
};

// Independent expectation for the delivered RGBA of the fixture pattern:
// reimplements the delivery rules (scale 255/max, >>8, palette lookup)
// rather than calling the production conversion.
std::array<std::uint8_t, 4> expected_pixel(const FormatCase& format,
                                           std::uint32_t x, std::uint32_t y,
                                           const std::vector<std::uint8_t>& palette_alpha) {
  const auto sample = [&](std::uint8_t c) {
    return pnga_test::apng_pattern_sample(x, y, c, format.bit_depth);
  };
  const auto scale = [&](std::uint16_t v) -> std::uint8_t {
    if (format.bit_depth == 16) {
      return static_cast<std::uint8_t>(v >> 8);
    }
    const std::uint32_t max = (1u << format.bit_depth) - 1u;
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) * 255u / max);
  };
  std::array<std::uint8_t, 4> out{255, 255, 255, 255};
  switch (format.color_type) {
    case 0:
      out = {scale(sample(0)), scale(sample(0)), scale(sample(0)), 255};
      break;
    case 2:
      out = {scale(sample(0)), scale(sample(1)), scale(sample(2)), 255};
      break;
    case 3: {
      const auto entry = pnga_test::apng_palette_entry(sample(0));
      out = {entry[0], entry[1], entry[2], 255};
      if (sample(0) < palette_alpha.size()) {
        out[3] = palette_alpha[sample(0)];
      }
      break;
    }
    case 4:
      out = {scale(sample(0)), scale(sample(0)), scale(sample(0)),
             scale(sample(1))};
      break;
    default:
      out = {scale(sample(0)), scale(sample(1)), scale(sample(2)),
             scale(sample(3))};
      break;
  }
  return out;
}

FrameRequest request_for_format(
    const pnga_test::ApngFormat& format,
    std::span<const FrameControl> frames,
    std::span<const std::byte> palette = {},
    std::span<const std::byte> transparency = {},
    const std::function<std::uint16_t(std::uint32_t, std::uint32_t,
                                      std::uint8_t)>& sample = {},
    std::uint32_t canvas_width = 0, std::uint32_t canvas_height = 0) {
  const std::uint32_t width =
      canvas_width != 0 ? canvas_width : frames.front().width;
  const std::uint32_t height =
      canvas_height != 0 ? canvas_height : frames.front().height;
  auto source = std::make_shared<const MemoryByteSource>(
      pnga_test::make_apng_format(false, frames, format, sample, palette,
                                  transparency, width, height));
  auto index = std::make_shared<const pnga::png_format::AnimationIndex>(
      index_animation(*source, AnimationLimits{}, [] { return false; }));
  REQUIRE(index->status == pnga::png_format::AnimationStatus::kComplete);
  REQUIRE(index->palette_bytes.size() / 3 ==
          (palette.size() + 2) / 3);
  FrameRequest request;
  request.generation = 3;
  request.request_serial = 4;
  request.ordinal = 0;
  request.source = std::move(source);
  request.index = std::move(index);
  request.canvas_header = pnga::png_reconstruction::ImageHeader{
      width, height, format.bit_depth, format.color_type, format.interlace};
  request.limits.max_working_bytes = 64ull * 1024 * 1024;
  request.delivery =
      delivery_context_from(*request.index, request.canvas_header);
  return request;
}

const std::vector<std::byte>& palette_for(std::uint8_t color_type,
                                          std::uint8_t bit_depth) {
  static std::vector<std::byte> cached;
  const std::size_t entries = 1u << bit_depth;
  if (color_type != 3 || cached.size() != entries * 3) {
    cached.clear();
    if (color_type == 3) {
      for (std::size_t i = 0; i < entries; ++i) {
        for (const auto component :
             pnga_test::apng_palette_entry(static_cast<std::uint16_t>(i))) {
          cached.push_back(std::byte{component});
        }
      }
    }
  }
  return cached;
}

void expect_pattern(const pnga::png_reconstruction::RgbaImage& image,
                    const FormatCase& format,
                    const std::vector<std::uint8_t>& palette_alpha) {
  REQUIRE(image.width > 0);
  REQUIRE(image.height > 0);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const auto expected =
          expected_pixel(format, x, y, palette_alpha);
      const std::size_t offset =
          (static_cast<std::size_t>(y) * image.width + x) * 4;
      for (int c = 0; c < 4; ++c) {
        REQUIRE(image.pixels[offset + static_cast<std::size_t>(c)] ==
                expected[static_cast<std::size_t>(c)]);
      }
    }
  }
}

std::size_t find_fdAT_body(const std::vector<std::byte>& png,
                           std::size_t* ordinal_out) {
  std::uint64_t pos = pnga::png_format::kPngSignature.size();
  for (std::size_t ordinal = 0; pos + 12 <= png.size(); ++ordinal) {
    const std::uint32_t length = pnga_test::fixture_u32(png.data() + pos);
    const char* type =
        reinterpret_cast<const char*>(png.data() + pos + 4);
    if (type[0] == 'f' && type[1] == 'd' && type[2] == 'A' &&
        type[3] == 'T') {
      *ordinal_out = ordinal;
      return static_cast<std::size_t>(pos + 8);
    }
    pos += 12 + length;
  }
  return 0;
}

}  // namespace

TEST_CASE("Frame analysis covers the color type and bit depth matrix",
          "[analysis-engine][apng][wp702]") {
  const std::vector<FormatCase> cases = {
      {0, 1}, {0, 2}, {0, 4}, {0, 8}, {0, 16}, {2, 8},  {2, 16},
      {3, 1}, {3, 2}, {3, 4}, {3, 8}, {4, 8},  {4, 16}, {6, 8},
      {6, 16},
  };
  for (const auto& format : cases) {
    SECTION("color " + std::to_string(format.color_type) + " depth " +
            std::to_string(format.bit_depth)) {
      const std::uint32_t width = 9;
      const std::uint32_t height = 5;
      const std::vector<FrameControl> frames = {
          FrameControl{0, width, height, 0, 0, 1, 100, 0, 0}};
      const auto& palette = palette_for(format.color_type, format.bit_depth);
      std::vector<std::uint8_t> palette_alpha;
      if (format.color_type == 3) {
        palette_alpha.assign(palette.size() / 3, 255);
      }
      const auto request = request_for_format(
          pnga_test::ApngFormat{format.color_type, format.bit_depth, false, 1},
          frames, palette);
      const FrameResult result = analyze_frame(request, nullptr);
      REQUIRE(result.stop == FrameResult::Stop::kReady);
      REQUIRE(result.frame != nullptr);
      expect_pattern(result.frame->delivered, format, palette_alpha);
    }
  }
}

TEST_CASE("Frame analysis decodes Adam7 interlaced frames",
          "[analysis-engine][apng][wp702]") {
  const pnga_test::ApngFormat format = GENERATE(
      pnga_test::ApngFormat{6, 8, true, 1}, pnga_test::ApngFormat{0, 16, true, 1},
      pnga_test::ApngFormat{3, 4, true, 1});
  const std::uint32_t width = 9;
  const std::uint32_t height = 7;  // odd dims exercise partial passes
  const std::vector<FrameControl> frames = {
      FrameControl{0, width, height, 0, 0, 1, 100, 0, 0}};
  const auto& palette = palette_for(format.color_type, format.bit_depth);
  std::vector<std::uint8_t> palette_alpha;
  if (format.color_type == 3) {
    palette_alpha.assign(palette.size() / 3, 255);
  }
  const auto request = request_for_format(format, frames, palette);
  const FrameResult result = analyze_frame(request, nullptr);
  REQUIRE(result.stop == FrameResult::Stop::kReady);
  REQUIRE(result.frame != nullptr);
  REQUIRE(result.frame->stages.header.interlace);
  expect_pattern(result.frame->delivered,
                 FormatCase{format.color_type, format.bit_depth},
                 palette_alpha);
}

TEST_CASE("Frame analysis applies tRNS at the original depth",
          "[analysis-engine][apng][wp702]") {
  // Gray 8: only the pixel whose sample matches the tRNS value turns
  // transparent; others stay opaque.
  {
    const std::vector<FrameControl> frames = {
        FrameControl{0, 2, 1, 0, 0, 1, 100, 0, 0}};
    const std::vector<std::byte> trns = {std::byte{0}, std::byte{5}};
    const auto request = request_for_format(
        pnga_test::ApngFormat{0, 8, false, 1}, frames, {}, trns,
        [](std::uint32_t x, std::uint32_t, std::uint8_t) {
          return static_cast<std::uint16_t>(x == 0 ? 5 : 200);
        });
    const FrameResult result = analyze_frame(request, nullptr);
    REQUIRE(result.stop == FrameResult::Stop::kReady);
    REQUIRE(result.frame->delivered.pixels[0] == 5);
    REQUIRE(result.frame->delivered.pixels[3] == 0);
    REQUIRE(result.frame->delivered.pixels[4] == 200);
    REQUIRE(result.frame->delivered.pixels[7] == 255);
  }
  // Gray 1: the tRNS value is a 2-byte sample at the image depth.
  {
    const std::vector<FrameControl> frames = {
        FrameControl{0, 2, 1, 0, 0, 1, 100, 0, 0}};
    const std::vector<std::byte> trns = {std::byte{0}, std::byte{1}};
    const auto request = request_for_format(
        pnga_test::ApngFormat{0, 1, false, 1}, frames, {}, trns,
        [](std::uint32_t x, std::uint32_t, std::uint8_t) {
          return static_cast<std::uint16_t>(x);
        });
    const FrameResult result = analyze_frame(request, nullptr);
    REQUIRE(result.stop == FrameResult::Stop::kReady);
    REQUIRE(result.frame->delivered.pixels[3] == 255);  // sample 0
    REQUIRE(result.frame->delivered.pixels[7] == 0);    // sample 1
  }
  // RGB 16: the tRNS triple is compared before the >>8 truncation.
  {
    const std::vector<FrameControl> frames = {
        FrameControl{0, 2, 1, 0, 0, 1, 100, 0, 0}};
    const std::vector<std::byte> trns = {
        std::byte{0x12}, std::byte{0x34}, std::byte{0x56},
        std::byte{0x78}, std::byte{0x9a}, std::byte{0xbc}};
    const auto request = request_for_format(
        pnga_test::ApngFormat{2, 16, false, 1}, frames, {}, trns,
        [](std::uint32_t x, std::uint32_t, std::uint8_t c) {
          const std::uint16_t values[2][3] = {
              {0x1234, 0x5678, 0x9abc}, {0x1235, 0x5678, 0x9abc}};
          return values[x][c];
        });
    const FrameResult result = analyze_frame(request, nullptr);
    REQUIRE(result.stop == FrameResult::Stop::kReady);
    REQUIRE(result.frame->delivered.pixels[3] == 0);
    REQUIRE(result.frame->delivered.pixels[7] == 255);
  }
  // Palette 4: the tRNS bytes are per-index alpha entries.
  {
    const std::vector<FrameControl> frames = {
        FrameControl{0, 2, 1, 0, 0, 1, 100, 0, 0}};
    const auto& palette = palette_for(3, 4);
    const std::vector<std::byte> trns = {std::byte{255}, std::byte{0}};
    const auto request = request_for_format(
        pnga_test::ApngFormat{3, 4, false, 1}, frames, palette, trns,
        [](std::uint32_t x, std::uint32_t, std::uint8_t) {
          return static_cast<std::uint16_t>(x + 1);  // indices 1 and 2
        });
    const FrameResult result = analyze_frame(request, nullptr);
    REQUIRE(result.stop == FrameResult::Stop::kReady);
    REQUIRE(result.frame->delivered.pixels[3] == 0);
    const auto entry = pnga_test::apng_palette_entry(2);
    REQUIRE(result.frame->delivered.pixels[4] == entry[0]);
    REQUIRE(result.frame->delivered.pixels[7] == 255);
  }
}

TEST_CASE("Frame analysis concatenates multi-fdAT frame payloads",
          "[analysis-engine][apng][wp702]") {
  const std::uint32_t width = 6;
  const std::uint32_t height = 4;
  const std::vector<FrameControl> frames = {
      FrameControl{0, width, height, 0, 0, 1, 100, 0, 0}};
  const auto single = request_for_format(pnga_test::ApngFormat{6, 8}, frames);
  const auto single_result = analyze_frame(single, nullptr);
  REQUIRE(single_result.stop == FrameResult::Stop::kReady);

  const auto split = request_for_format(pnga_test::ApngFormat{6, 8, false, 4},
                                        frames);
  REQUIRE(split.index->frames[0].data.size() == 4);
  const auto split_result = analyze_frame(split, nullptr);
  REQUIRE(split_result.stop == FrameResult::Stop::kReady);
  REQUIRE(split_result.frame->delivered.pixels ==
          single_result.frame->delivered.pixels);
}

TEST_CASE("Frame analysis maps subrect frames with their offsets",
          "[analysis-engine][apng][wp702]") {
  const std::vector<FrameControl> frames = {
      FrameControl{0, 10, 6, 3, 2, 1, 100, 0, 0}};
  const auto request = request_for_format(
      pnga_test::ApngFormat{6, 8}, frames, {}, {}, {}, 16, 12);
  const FrameResult result = analyze_frame(request, nullptr);
  REQUIRE(result.stop == FrameResult::Stop::kReady);
  REQUIRE(result.frame != nullptr);
  REQUIRE(result.frame->control.x == 3);
  REQUIRE(result.frame->control.y == 2);
  REQUIRE(result.frame->delivered.width == 10);
  REQUIRE(result.frame->delivered.height == 6);
  expect_pattern(result.frame->delivered, FormatCase{6, 8}, {});
}

TEST_CASE("Frame analysis rejects corrupt frame streams",
          "[analysis-engine][apng][wp702]") {
  const std::vector<FrameControl> frames = {
      FrameControl{0, 4, 2, 0, 0, 1, 100, 0, 0}};

  const auto analyze = [](const std::vector<std::byte>& png) {
    auto source = std::make_shared<const MemoryByteSource>(png);
    auto index = std::make_shared<const pnga::png_format::AnimationIndex>(
        index_animation(*source, AnimationLimits{}, [] { return false; }));
    REQUIRE(index->status == pnga::png_format::AnimationStatus::kComplete);
    FrameRequest request;
    request.source = std::move(source);
    request.index = std::move(index);
    request.canvas_header =
        pnga::png_reconstruction::ImageHeader{4, 2, 8, 6, false};
    request.limits.max_working_bytes = 64ull * 1024 * 1024;
    return analyze_frame(request, nullptr);
  };

  // Corrupted zlib payload with a recomputed CRC: indexing still succeeds
  // (the parser scans structure, the fdAT sequence stays intact) and frame
  // analysis fails with a stable error instead of partial pixels.
  auto corrupted = pnga_test::make_apng_format(
      false, frames, pnga_test::ApngFormat{6, 8});
  {
    std::size_t fdat_ordinal = 0;
    const std::size_t body = find_fdAT_body(corrupted, &fdat_ordinal);
    REQUIRE(body != 0);
    REQUIRE(corrupted[body + 4] != std::byte{0});
    corrupted[body + 4] = corrupted[body + 4] ^ std::byte{0xff};
    pnga_test::refresh_chunk_crc(corrupted, fdat_ordinal);
    const FrameResult result = analyze(corrupted);
    REQUIRE(result.stop == FrameResult::Stop::kError);
    REQUIRE(result.frame == nullptr);
    REQUIRE_FALSE(result.error.empty());
  }

  // Invalid filter byte at the start of the frame's raw stream (after the
  // fdAT sequence number).
  auto bad_filter = pnga_test::make_apng_format(
      false, frames, pnga_test::ApngFormat{6, 8});
  {
    std::size_t fdat_ordinal = 0;
    const std::size_t body = find_fdAT_body(bad_filter, &fdat_ordinal);
    REQUIRE(body != 0);
    bad_filter[body + 4] = std::byte{9};
    pnga_test::refresh_chunk_crc(bad_filter, fdat_ordinal);
    const FrameResult result = analyze(bad_filter);
    REQUIRE(result.stop == FrameResult::Stop::kError);
    REQUIRE(result.frame == nullptr);
  }
}

TEST_CASE("Inspection fixture drives frame analysis and target factory identically",
          "[apng-inspect]") {
  const auto request = pnga_test::inspection_request();
  const FrameResult analyzed = analyze_frame(request, nullptr);
  REQUIRE(analyzed.stop == FrameResult::Stop::kReady);
  REQUIRE(analyzed.frame != nullptr);
  REQUIRE(analyzed.frame->control.width == 2);
  REQUIRE(analyzed.frame->control.height == 3);
  REQUIRE(analyzed.frame->control.x == 10);
  REQUIRE(analyzed.frame->control.y == 20);

  const auto target = pnga::analysis_engine::make_frame_target(request);
  REQUIRE(target.target);
  REQUIRE(target.target->header.width == analyzed.frame->stages.header.width);
  REQUIRE(target.target->header.height ==
          analyzed.frame->stages.header.height);
  REQUIRE(target.target->key.generation == request.generation);
}
