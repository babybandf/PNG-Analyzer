// WP-5U1 coordinate summary tests: whole-pixel/channel distinctions,
// pass-local Adam7 coordinates, packed samples and stable error states.

#include <pnga/analysis-engine/coordinate_query.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "test_png_helpers.h"

using namespace pnga_test;
using pnga::analysis_engine::analyze_stages;
using pnga::analysis_engine::CoordinateQueryStatus;
using pnga::analysis_engine::query_coordinate;
using pnga::analysis_engine::StageSet;
using pnga::io::MemoryByteSource;
using pnga::png_format::index_chunks;
using pnga::png_format::VirtualIDATStream;
using pnga::trace_model::ImageCoordinate;
using pnga::trace_model::Selection;
using pnga::trace_model::Stage;

namespace {

StageSet stages_of(const EncodedPng& encoded) {
  MemoryByteSource source(encoded.png_bytes);
  const auto index = index_chunks(source);
  const VirtualIDATStream stream(index);
  return analyze_stages(stream, source, encoded.header);
}

}  // namespace

TEST_CASE("Coordinate query resolves whole pixel and channel selection",
          "[analysis-engine][wp5u1]") {
  const EncodedPng encoded =
      encode_png(8, 5, 8, 6, /*interlace=*/false, /*all_none=*/false);
  const StageSet stages = stages_of(encoded);
  REQUIRE(stages.success);

  Selection pixel;
  pixel.image = ImageCoordinate{0, 0, 2, 3, 2};
  pixel.stage = Stage::kFiltered;
  const auto whole = query_coordinate(stages, pixel);
  REQUIRE(whole.status == CoordinateQueryStatus::kReady);
  REQUIRE(whole.pass_number == 0);
  REQUIRE(whole.stream_row == 2);
  REQUIRE(whole.row_in_pass == 2);
  REQUIRE(whole.local_x == 3);
  REQUIRE(whole.channel_count == 4);
  REQUIRE_FALSE(whole.native_sample_index.has_value());
  REQUIRE(whole.selection.image->row == 2);

  pixel.image->channel = 1;
  const auto channel = query_coordinate(stages, pixel);
  REQUIRE(channel.status == CoordinateQueryStatus::kReady);
  REQUIRE(channel.native_sample_index.has_value());
  REQUIRE(*channel.native_sample_index == (2u * 8u + 3u) * 4u + 1u);
  REQUIRE(channel.filtered_data_offset < stages.filtered.size());
  REQUIRE(channel.unfiltered_data_offset < stages.unfiltered.size());
}

TEST_CASE("Coordinate query resolves Adam7 pass-local packed samples",
          "[analysis-engine][wp5u1]") {
  const EncodedPng encoded =
      encode_png(16, 12, 2, 0, /*interlace=*/true, /*all_none=*/false);
  const StageSet stages = stages_of(encoded);
  REQUIRE(stages.success);

  // Adam7 pass 1 starts at (0,0), steps by (8,8); (8,8) is local (1,1).
  Selection selection;
  selection.image = ImageCoordinate{0, 1, 1, 8, 8, 0};
  selection.image->packed_sample =
      pnga::trace_model::PackedSampleCoordinate{2, 2};
  selection.stage = Stage::kNative;
  const auto summary = query_coordinate(stages, selection);
  REQUIRE(summary.status == CoordinateQueryStatus::kReady);
  REQUIRE(summary.pass_index == 0);
  REQUIRE(summary.pass_number == 1);
  REQUIRE(summary.row_in_pass == 1);
  REQUIRE(summary.local_x == 1);
  REQUIRE(summary.sample_bit_offset == 2);
  REQUIRE(summary.sample_bit_length == 2);
  REQUIRE(summary.sample_byte_count == 1);
  REQUIRE(summary.selection.image->pass == 1);
  REQUIRE(summary.selection.image->row == 1);
}

TEST_CASE("Coordinate query exposes stable no-selection and applicability states",
          "[analysis-engine][wp5u1]") {
  const EncodedPng encoded =
      encode_png(4, 4, 8, 2, /*interlace=*/false, /*all_none=*/true);
  const StageSet stages = stages_of(encoded);

  const auto none = query_coordinate(stages, Selection{});
  REQUIRE(none.status == CoordinateQueryStatus::kNoSelection);
  REQUIRE(std::string(pnga::analysis_engine::coordinate_query_status_text(
              none.status)) == "no selection");

  Selection chunk;
  chunk.image = ImageCoordinate{0, 0, 0, 1, 1};
  chunk.stage = Stage::kChunk;
  const auto not_applicable = query_coordinate(stages, chunk);
  REQUIRE(not_applicable.status == CoordinateQueryStatus::kNotApplicable);
  REQUIRE(not_applicable.error ==
          "stage does not have image-coordinate semantics");

  Selection outside;
  outside.image = ImageCoordinate{0, 0, 0, 4, 0};
  outside.stage = Stage::kDelivered;
  const auto out_of_range = query_coordinate(stages, outside);
  REQUIRE(out_of_range.status == CoordinateQueryStatus::kOutOfRange);
  REQUIRE(out_of_range.error == "image coordinate is outside the image");
}

TEST_CASE("Coordinate query rejects inconsistent pass-local coordinates",
          "[analysis-engine][wp5u1]") {
  const EncodedPng encoded =
      encode_png(8, 8, 8, 2, /*interlace=*/true, /*all_none=*/true);
  const StageSet stages = stages_of(encoded);
  REQUIRE(stages.success);

  Selection selection;
  selection.image = ImageCoordinate{0, 3, 1, 4, 4};
  const auto summary = query_coordinate(stages, selection);
  REQUIRE(summary.status == CoordinateQueryStatus::kOutOfRange);
  REQUIRE(summary.error == "pass-local row does not match coordinate");
}

TEST_CASE("Coordinate query rejects a second byte on an 8-bit sample",
          "[analysis-engine][wp5u1]") {
  const EncodedPng encoded =
      encode_png(4, 4, 8, 0, /*interlace=*/false, /*all_none=*/true);
  const StageSet stages = stages_of(encoded);
  REQUIRE(stages.success);

  Selection selection;
  selection.image = ImageCoordinate{0, 0, 0, 1, 1, 0};
  selection.image->sample_byte = 1;
  const auto summary = query_coordinate(stages, selection);
  REQUIRE(summary.status == CoordinateQueryStatus::kOutOfRange);
  REQUIRE(summary.error == "sample byte is outside the sample");
}

TEST_CASE("Coordinate query resolves the selected byte of a 16-bit channel",
          "[analysis-engine][wp5u1]") {
  const EncodedPng encoded =
      encode_png(3, 2, 16, 6, /*interlace=*/false, /*all_none=*/true);
  const StageSet stages = stages_of(encoded);
  REQUIRE(stages.success);

  Selection selection;
  selection.image = ImageCoordinate{0, 0, 1, 1, 1, 2};
  selection.image->sample_byte = 1;
  selection.stage = Stage::kNative;
  const auto summary = query_coordinate(stages, selection);
  REQUIRE(summary.status == CoordinateQueryStatus::kReady);
  REQUIRE(summary.channel_count == 4);
  REQUIRE(summary.sample_byte_count == 1);
  REQUIRE(summary.sample_bit_length == 8);
  // RGBA16: pixel (1,1), channel 2, low byte is byte 13 of the row.
  REQUIRE(summary.filtered_data_offset == stages.scanlines[1].offset + 1 + 13);
  REQUIRE(summary.unfiltered_data_offset == 1 * 24 + 13);
  REQUIRE(summary.sample_bit_offset == 0);
  REQUIRE(summary.unfiltered_sample_bit_offset == 0);
  REQUIRE(summary.native_sample_index == std::optional<std::uint64_t>{18});
}
