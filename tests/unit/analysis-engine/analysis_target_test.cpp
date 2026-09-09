// WP-APNG-INSPECT T01: frame-local coordinate mapping and frame-scoped
// coordinate queries (contract C1). Global canvas points convert to
// frame-local points with checked arithmetic; frame coordinate queries
// verify identity and rectangle membership, delegate to the pass-local
// coordinate kernel and restore canvas-global coordinates in the output.

#include <pnga/analysis-engine/analysis_target.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_frame_stream.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/trace-model/selection.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

#include "test_png_helpers.h"
#include "apng_inspection_fixture.h"

using namespace pnga::analysis_engine;
using pnga::io::MemoryByteSource;
using pnga::png_format::FrameControl;
using pnga::png_format::index_chunks;
using pnga::png_format::VirtualIDATStream;
using pnga::trace_model::AnimationFrame;
using pnga::trace_model::ImageCoordinate;
using pnga::trace_model::Selection;
using pnga::trace_model::Stage;

namespace {

FrameControl inspection_rect() {
  return FrameControl{0, 2, 3, 10, 20, 1, 100, 0, 0};
}

FrameStageSet animation_frame_8x5() {
  const pnga_test::EncodedPng encoded =
      pnga_test::encode_png(8, 5, 8, 6, /*interlace=*/false, /*all_none=*/false);
  MemoryByteSource source(encoded.png_bytes);
  const auto index = index_chunks(source);
  const VirtualIDATStream stream(index);
  FrameStageSet frame;
  frame.identity = AnimationFrame{0};
  frame.control = FrameControl{0, 8, 5, 10, 20, 1, 100, 0, 0};
  frame.stages = analyze_stages(stream, source, encoded.header);
  return frame;
}

Selection global_pixel(std::uint64_t x, std::uint64_t y) {
  Selection selection;
  ImageCoordinate coordinate;
  coordinate.identity = AnimationFrame{0};
  coordinate.pass = 0;
  coordinate.row = 0;
  coordinate.x = x;
  coordinate.y = y;
  selection.image = coordinate;
  selection.stage = Stage::kFiltered;
  return selection;
}

}  // namespace

TEST_CASE("frame_local_point maps canvas points into the frame rectangle",
          "[apng-inspect]") {
  const auto rect = inspection_rect();

  const auto inside = frame_local_point(rect, 11, 22);
  REQUIRE(inside.has_value());
  REQUIRE(*inside == LocalPoint{1, 2});

  REQUIRE_FALSE(frame_local_point(rect, 9, 22).has_value());
  REQUIRE_FALSE(frame_local_point(rect, 12, 22).has_value());
  REQUIRE_FALSE(frame_local_point(rect, 11, 19).has_value());
  REQUIRE_FALSE(frame_local_point(rect, 11, 23).has_value());

  constexpr auto max_u64 = std::numeric_limits<std::uint64_t>::max();
  REQUIRE_FALSE(frame_local_point(rect, max_u64, max_u64).has_value());

  const auto degenerate = FrameControl{0, 0, 3, 10, 20, 1, 100, 0, 0};
  REQUIRE_FALSE(frame_local_point(degenerate, 10, 20).has_value());
}

TEST_CASE("query_frame_coordinate resolves and restores global coordinates",
          "[apng-inspect]") {
  const FrameStageSet frame = animation_frame_8x5();
  REQUIRE(frame.stages.success);

  const auto summary = query_frame_coordinate(frame, global_pixel(11, 22));
  REQUIRE(summary.status == CoordinateQueryStatus::kReady);
  REQUIRE(summary.image.has_value());
  REQUIRE(summary.image->x == 11);
  REQUIRE(summary.image->y == 22);
  REQUIRE(std::holds_alternative<AnimationFrame>(summary.image->identity));
  REQUIRE(summary.selection.image.has_value());
  REQUIRE(summary.selection.image->x == 11);
  REQUIRE(summary.selection.image->y == 22);
  REQUIRE(summary.selection.image->row == 2);
  REQUIRE(summary.local_x == 1);
  REQUIRE(summary.row_in_pass == 2);
  REQUIRE(summary.stream_row == 2);

  Selection channel = global_pixel(11, 22);
  channel.image->channel = 1;
  const auto channel_summary = query_frame_coordinate(frame, channel);
  REQUIRE(channel_summary.status == CoordinateQueryStatus::kReady);
  REQUIRE(channel_summary.native_sample_index.has_value());
  REQUIRE(*channel_summary.native_sample_index == (2u * 8u + 1u) * 4u + 1u);
}

TEST_CASE("query_frame_coordinate rejects foreign identity and outside points",
          "[apng-inspect]") {
  const FrameStageSet frame = animation_frame_8x5();
  REQUIRE(frame.stages.success);

  Selection foreign = global_pixel(11, 22);
  foreign.image->identity = pnga::trace_model::StaticImage{};
  REQUIRE(query_frame_coordinate(frame, foreign).status ==
          CoordinateQueryStatus::kNotApplicable);

  Selection left_of_origin = global_pixel(9, 22);
  REQUIRE(query_frame_coordinate(frame, left_of_origin).status ==
          CoordinateQueryStatus::kOutOfRange);

  Selection beyond_width = global_pixel(19, 22);
  REQUIRE(query_frame_coordinate(frame, beyond_width).status ==
          CoordinateQueryStatus::kOutOfRange);

  Selection beyond_height = global_pixel(11, 26);
  REQUIRE(query_frame_coordinate(frame, beyond_height).status ==
          CoordinateQueryStatus::kOutOfRange);

  REQUIRE(query_frame_coordinate(frame, Selection{}).status ==
          CoordinateQueryStatus::kNoSelection);
}

TEST_CASE("make_frame_target builds the frame analysis context",
          "[apng-inspect]") {
  auto request = pnga_test::inspection_request();
  const auto target = make_frame_target(request);
  REQUIRE(target.target);
  REQUIRE(target.error.empty());
  REQUIRE(target.target->header.width == 2);
  REQUIRE(target.target->header.height == 3);
  REQUIRE(target.target->stream->size() > 0);
  REQUIRE(target.target->key.generation == 7);
  REQUIRE(std::holds_alternative<AnimationFrame>(target.target->key.identity));
  REQUIRE(target.target->control.has_value());
  REQUIRE(target.target->control->x == 10);
  REQUIRE(target.target->control->y == 20);
  REQUIRE(target.target->source == request.source);

  request.ordinal = 2;
  const auto missing = make_frame_target(request);
  REQUIRE_FALSE(missing.target);
  REQUIRE_FALSE(missing.error.empty());
}

TEST_CASE("Dual wrapped payload exposes the identical logical stream",
          "[apng-inspect]") {
  const auto dual = pnga_test::make_dual_wrapped_payload();
  REQUIRE_FALSE(dual.static_png.empty());
  REQUIRE_FALSE(dual.apng.empty());
  REQUIRE(dual.static_png.size() + dual.apng.size() <= 64u * 1024u);

  const auto target = make_frame_target(pnga_test::inspection_request());
  REQUIRE(target.target);
  const std::size_t frame_size = target.target->stream->size();
  REQUIRE(frame_size == dual.frame_payload.size());
  std::vector<std::byte> frame_bytes(frame_size);
  REQUIRE(
      target.target->stream->read(0, frame_bytes.data(), frame_bytes.size()));

  pnga::io::MemoryByteSource static_source(dual.static_png);
  const auto static_index = pnga::png_format::index_chunks(static_source);
  const auto idat_stream = pnga::png_format::make_idat_stream(
      std::make_shared<const pnga::io::MemoryByteSource>(dual.static_png),
      static_index);
  REQUIRE(idat_stream);
  REQUIRE(idat_stream->size() == dual.frame_payload.size());
  std::vector<std::byte> static_bytes(idat_stream->size());
  REQUIRE(idat_stream->read(0, static_bytes.data(), static_bytes.size()));
  REQUIRE(frame_bytes == static_bytes);
}

TEST_CASE("Dual wrapped payload copies palette and transparency values",
          "[apng-inspect]") {
  std::vector<std::byte> palette;
  for (std::uint32_t i = 0; i < 256; ++i) {
    palette.push_back(std::byte{static_cast<unsigned char>((i * 37 + 12) % 256)});
    palette.push_back(std::byte{static_cast<unsigned char>((i * 71 + 90) % 256)});
    palette.push_back(std::byte{static_cast<unsigned char>((i * 13 + 200) % 256)});
  }
  std::vector<std::byte> transparency;
  for (std::uint32_t i = 0; i < 256; ++i) {
    transparency.push_back(std::byte{static_cast<unsigned char>(i)});
  }

  const auto dual = pnga_test::make_dual_wrapped_payload(
      pnga_test::ApngFormat{3, 8}, palette, transparency);
  REQUIRE_FALSE(dual.static_png.empty());
  REQUIRE(dual.static_png.size() + dual.apng.size() <= 64u * 1024u);
  REQUIRE(dual.delivery.palette.size() == 256);
  REQUIRE(dual.delivery.palette[1][0] ==
          static_cast<std::uint8_t>((1 * 37 + 12) % 256));
  REQUIRE(dual.delivery.palette_alpha.size() == 256);
  REQUIRE(dual.delivery.palette_alpha[7] == 7);
}
