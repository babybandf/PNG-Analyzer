#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

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

TEST_CASE("Animation index retains bounded global palette metadata",
          "[png-format][apng][wp702]") {
  auto bytes = pnga_test::make_apng(false, two_frames());
  std::vector<std::byte> palette_chunk;
  const std::vector<std::byte> palette = {
      std::byte{0xff}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0xff}, std::byte{0x00}};
  pnga_test::append_apng_chunk(palette_chunk, "PLTE", palette);
  const std::vector<std::byte> transparency = {
      std::byte{0x00}, std::byte{0x80}, std::byte{0x00},
      std::byte{0xff}, std::byte{0x00}, std::byte{0x00}};
  pnga_test::append_apng_chunk(palette_chunk, "tRNS", transparency);
  const std::size_t after_ihdr = pnga::png_format::kPngSignature.size() + 12 + 13;
  bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(after_ihdr),
               palette_chunk.begin(), palette_chunk.end());
  const std::size_t ihdr_data = pnga::png_format::kPngSignature.size() + 8;
  bytes[ihdr_data + 9] = std::byte{2};  // RGB, so tRNS has six bytes.
  const std::size_t ihdr_crc = ihdr_data + 13;
  uLong crc = crc32(0, Z_NULL, 0);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(bytes.data() + ihdr_data - 4),
             4);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(bytes.data() + ihdr_data),
             13);
  bytes[ihdr_crc] = static_cast<std::byte>(crc >> 24);
  bytes[ihdr_crc + 1] = static_cast<std::byte>(crc >> 16);
  bytes[ihdr_crc + 2] = static_cast<std::byte>(crc >> 8);
  bytes[ihdr_crc + 3] = static_cast<std::byte>(crc);

  MemoryByteSource source(std::move(bytes));
  const auto result = index_animation(source, AnimationLimits{}, [] {
    return false;
  });
  REQUIRE(result.status == AnimationStatus::kComplete);
  REQUIRE(result.palette_bytes == palette);
  REQUIRE(result.transparency_bytes == transparency);
}
