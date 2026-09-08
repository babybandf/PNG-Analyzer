#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>
#include <pnga/png-format/virtual_frame_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "apng_fixture.h"

using pnga::io::MemoryByteSource;
using pnga::png_format::AnimationLimits;
using pnga::png_format::ChunkIndex;
using pnga::png_format::FrameControl;
using pnga::png_format::FrameDataSpan;
using pnga::png_format::FrameRecord;
using pnga::png_format::PhysicalRange;
using pnga::png_format::index_animation;
using pnga::png_format::index_chunks;
using pnga::png_format::make_frame_stream;
using pnga::png_format::make_idat_stream;

namespace {

std::array<FrameControl, 2> controls() {
  return std::array<FrameControl, 2>{
      FrameControl{0, 1, 1, 0, 0, 1, 100, 0, 0},
      FrameControl{0, 1, 1, 0, 0, 2, 100, 0, 1},
  };
}

std::shared_ptr<const pnga::png_format::AnimationIndex> make_index(
    const std::shared_ptr<const MemoryByteSource>& source) {
  return std::make_shared<const pnga::png_format::AnimationIndex>(
      index_animation(*source, AnimationLimits{}, [] { return false; }));
}

}  // namespace

TEST_CASE("Frame stream reads fdAT payload without exposing its sequence",
          "[png-format][apng][wp701]") {
  auto bytes = pnga_test::make_apng(false, controls());
  auto source = std::make_shared<const MemoryByteSource>(bytes);
  auto index = make_index(source);
  const auto stream = make_frame_stream(source, index, 0);

  REQUIRE(stream != nullptr);
  REQUIRE(index->frames.at(0).data.size() == 1);
  const auto payload = index->frames.at(0).data.front();
  REQUIRE(stream->size() == payload.length);

  std::vector<std::byte> actual(static_cast<std::size_t>(stream->size()));
  REQUIRE(stream->read(0, actual.data(), actual.size()));
  std::vector<std::byte> expected(actual.size());
  REQUIRE(source->read(payload.offset, expected.data(), expected.size()));
  REQUIRE(actual == expected);

  std::vector<PhysicalRange> ranges;
  REQUIRE(stream->logical_to_physical(0, stream->size(), ranges));
  REQUIRE(ranges == std::vector<PhysicalRange>{{payload.offset, payload.length}});
  REQUIRE(stream->physical_to_logical(payload.offset) == 0);
  REQUIRE_FALSE(stream->physical_to_logical(payload.offset - 1).has_value());
  REQUIRE_FALSE(stream->view(0, 1).has_value());
}

TEST_CASE("Frame stream keeps its source and animation index alive",
          "[png-format][apng][wp701]") {
  auto source = std::make_shared<const MemoryByteSource>(
      pnga_test::make_apng(true, controls()));
  auto index = make_index(source);
  const auto stream = make_frame_stream(source, index, 1);
  REQUIRE(stream != nullptr);

  const auto span = index->frames.at(1).data.front();
  source.reset();
  index.reset();

  std::vector<std::byte> bytes(static_cast<std::size_t>(stream->size()));
  REQUIRE(stream->read(0, bytes.data(), bytes.size()));
  REQUIRE(stream->physical_to_logical(span.offset) == 0);
}

TEST_CASE("IDAT factory exposes a lifetime-safe static compressed stream",
          "[png-format][apng][wp701]") {
  auto source = std::make_shared<const MemoryByteSource>(
      pnga_test::make_apng(false, controls()));
  const auto chunk_index = index_chunks(*source);
  const auto stream = make_idat_stream(source, chunk_index);
  REQUIRE(stream != nullptr);
  REQUIRE(stream->size() != 0);

  std::vector<PhysicalRange> ranges;
  REQUIRE(stream->logical_to_physical(0, stream->size(), ranges));
  REQUIRE_FALSE(ranges.empty());
  std::vector<std::byte> bytes(static_cast<std::size_t>(stream->size()));
  REQUIRE(stream->read(0, bytes.data(), bytes.size()));
  REQUIRE(stream->physical_to_logical(ranges.front().offset) == 0);
}

TEST_CASE("Frame and IDAT factories reject invalid ownership and ranges",
          "[png-format][apng][wp701]") {
  auto source = std::make_shared<const MemoryByteSource>(
      pnga_test::make_apng(false, controls()));
  auto index = make_index(source);
  REQUIRE(make_frame_stream(nullptr, index, 0) == nullptr);
  REQUIRE(make_frame_stream(source, nullptr, 0) == nullptr);
  REQUIRE(make_frame_stream(source, index, 2) == nullptr);

  const ChunkIndex chunk_index;
  REQUIRE(make_idat_stream(nullptr, chunk_index) == nullptr);

  auto bad_index = std::make_shared<pnga::png_format::AnimationIndex>(*index);
  bad_index->frames[0].data = {FrameDataSpan{source->size(), 1}};
  REQUIRE(make_frame_stream(source, bad_index, 0) == nullptr);

  ChunkIndex bad_chunks;
  pnga::png_format::ChunkNode bad_chunk;
  bad_chunk.data_offset = source->size();
  bad_chunk.data_length = 1;
  bad_chunk.type = {std::byte{'I'}, std::byte{'D'}, std::byte{'A'},
                    std::byte{'T'}};
  bad_chunks.chunks.push_back(bad_chunk);
  REQUIRE(make_idat_stream(source, bad_chunks) == nullptr);
}

TEST_CASE("Zero-width mappings use stable non-empty anchors",
          "[png-format][apng][wp701]") {
  auto source = std::make_shared<const MemoryByteSource>(std::vector<std::byte>(
      {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}}));
  auto index = std::make_shared<pnga::png_format::AnimationIndex>();
  index->frames.push_back(FrameRecord{
      0, FrameControl{}, false,
      std::vector<FrameDataSpan>{{0, 0}, {1, 2}}});
  const auto stream = make_frame_stream(source, index, 0);
  REQUIRE(stream != nullptr);
  REQUIRE(stream->size() == 2);

  std::vector<PhysicalRange> ranges;
  REQUIRE(stream->logical_to_physical(0, 0, ranges));
  REQUIRE(ranges == std::vector<PhysicalRange>{{1, 0}});
  ranges.clear();
  REQUIRE(stream->logical_to_physical(2, 0, ranges));
  REQUIRE(ranges == std::vector<PhysicalRange>{{3, 0}});
  REQUIRE(stream->physical_to_logical(3) == 2);
}
