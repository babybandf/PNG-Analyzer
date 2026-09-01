#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using pnga::analysis_engine::BlockInspectorStatus;
using pnga::analysis_engine::FastCompressionIndexStatus;
using pnga::analysis_engine::FastCompressionStreamSummary;
using pnga::analysis_engine::TraceBlockSummary;
using pnga::analysis_engine::TraceQueryResult;
using pnga::analysis_engine::TraceQueryStatus;
using pnga::deflate_index::Adler32Status;
using pnga::deflate_index::ZlibWrapperInfo;

namespace {

pnga::png_format::ChunkNode idat(std::uint64_t offset,
                                 std::uint64_t length) {
  pnga::png_format::ChunkNode node;
  node.data_offset = offset;
  node.data_length = length;
  node.type = {std::byte{'I'}, std::byte{'D'}, std::byte{'A'},
               std::byte{'T'}};
  return node;
}

}  // namespace

TEST_CASE("block inspector projects block ranges and selected position") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  trace.generation = 42;
  TraceBlockSummary first;
  first.index = 3;
  first.type = pnga::deflate_index::BlockType::kFixed;
  first.last = false;
  first.input_bit_begin = 8;
  first.input_bit_end = 24;
  first.output_begin = 100;
  first.output_end = 120;
  first.physical_spans.push_back({pnga::trace_model::ProvenanceSpace::kPhysicalFile,
                                  512, 2, 0, 0, false});
  trace.blocks.push_back(first);

  TraceBlockSummary second;
  second.index = 4;
  second.type = pnga::deflate_index::BlockType::kDynamic;
  second.last = true;
  second.input_bit_begin = 24;
  second.input_bit_end = 40;
  second.output_begin = 120;
  second.output_end = 140;
  trace.blocks.push_back(second);

  const auto view = pnga::analysis_engine::build_block_inspector(trace, 105, 7);
  REQUIRE(view.status == BlockInspectorStatus::kReady);
  REQUIRE(view.generation == 42);
  REQUIRE(view.scanline == 7);
  REQUIRE(view.selected_block_index == 3);
  REQUIRE(view.rows.size() == 2);
  REQUIRE(view.rows[0].current_output_position == 105);
  REQUIRE(!view.rows[1].current_output_position.has_value());
  REQUIRE(view.rows[0].physical_spans.size() == 1);
  REQUIRE(pnga::analysis_engine::serialize_block_inspector(view) ==
          "status=ready;generation=42;scanline=7;selected_output=105;"
          "selected_block=3;error=;rows=2|3,fixed,0,8:24,100:120,"
          "current=105,spans=1|4,dynamic,1,24:40,120:140,current=-,spans=0");
}

TEST_CASE("block inspector preserves partial and unavailable states") {
  TraceQueryResult partial;
  partial.status = TraceQueryStatus::kPartial;
  partial.generation = 9;
  const auto partial_view =
      pnga::analysis_engine::build_block_inspector(partial);
  REQUIRE(partial_view.status == BlockInspectorStatus::kPartial);

  TraceQueryResult replaying;
  replaying.status = TraceQueryStatus::kReplaying;
  replaying.error = "replay pending";
  const auto no_trace =
      pnga::analysis_engine::build_block_inspector(replaying);
  REQUIRE(no_trace.status == BlockInspectorStatus::kNoTrace);
  REQUIRE(no_trace.error == "replay pending");
}

TEST_CASE("fast compression index retains complete blocks and segmented IDAT") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(100, 2), idat(200, 2)};
  const pnga::png_format::VirtualIDATStream stream(chunks);

  pnga::deflate_index::BlockIndexResult index;
  index.success = true;
  index.zlib_header_bits = 16;
  index.total_output_bytes = 12;
  index.wrapper = ZlibWrapperInfo{0x78, 0x9C, 8, 15, false, true};
  index.adler.status = Adler32Status::kMatch;
  index.adler.expected = 0x0A9B0C1Du;
  index.adler.actual = 0x0A9B0C1Du;
  index.blocks.push_back({0, pnga::deflate_index::BlockType::kDynamic, true,
                          8, 32, 0, 12});

  const auto view = pnga::analysis_engine::build_fast_compression_index(
      88, index, stream);
  REQUIRE(view.status == FastCompressionIndexStatus::kReady);
  REQUIRE(view.generation == 88);
  REQUIRE(view.stream.stream_range.begin ==
          pnga::trace_model::ZlibByteOffset{0});
  REQUIRE(view.stream.stream_range.end ==
          pnga::trace_model::ZlibByteOffset{4});
  REQUIRE(view.stream.deflate_data_begin ==
          pnga::trace_model::ZlibByteOffset{2});
  REQUIRE(view.stream.wrapper == ZlibWrapperInfo{0x78, 0x9C, 8, 15, false, true});
  REQUIRE(view.stream.adler.status == Adler32Status::kMatch);
  REQUIRE(view.stream.adler.expected == view.stream.adler.actual);
  REQUIRE_FALSE(view.stream.stop_input.has_value());
  REQUIRE_FALSE(view.stream.stop_output.has_value());
  REQUIRE(view.stream.idat_segment_count == 2);
  REQUIRE(view.stream.idat_spans.size() == 2);
  REQUIRE(view.stream.idat_spans[0].logical_range ==
          pnga::trace_model::ZlibByteRange{
              pnga::trace_model::ZlibByteOffset{0},
              pnga::trace_model::ZlibByteOffset{2}});
  REQUIRE(view.stream.idat_spans[0].physical_range ==
          pnga::trace_model::FileByteRange{
              pnga::trace_model::FileByteOffset{100},
              pnga::trace_model::FileByteOffset{102}});
  REQUIRE(view.stream.idat_spans[1].logical_range ==
          pnga::trace_model::ZlibByteRange{
              pnga::trace_model::ZlibByteOffset{2},
              pnga::trace_model::ZlibByteOffset{4}});
  REQUIRE(view.stream.idat_spans[1].physical_range ==
          pnga::trace_model::FileByteRange{
              pnga::trace_model::FileByteOffset{200},
              pnga::trace_model::FileByteOffset{202}});
  REQUIRE(view.blocks.size() == 1);
  REQUIRE(view.blocks[0].input_range.begin ==
          pnga::trace_model::ZlibBitOffset{8});
  REQUIRE(view.blocks[0].input_range.end ==
          pnga::trace_model::ZlibBitOffset{32});
  REQUIRE(view.blocks[0].physical_spans ==
          std::vector<pnga::trace_model::ProvenanceSpan>{
              {pnga::trace_model::ProvenanceSpace::kPhysicalFile, 101, 1, 0,
                8, true},
              {pnga::trace_model::ProvenanceSpace::kPhysicalFile, 200, 2, 0,
                16, true}});
}

TEST_CASE("fast compression index projects a single IDAT segment") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(1000, 6)};
  const pnga::png_format::VirtualIDATStream stream(chunks);

  pnga::deflate_index::BlockIndexResult index;
  index.success = true;
  index.zlib_header_bits = 16;
  index.total_output_bytes = 9;
  index.adler.status = Adler32Status::kMatch;
  index.adler.expected = 7;
  index.adler.actual = 7;
  index.blocks.push_back({0, pnga::deflate_index::BlockType::kFixed, true,
                          16, 47, 0, 9});

  const auto view = pnga::analysis_engine::build_fast_compression_index(
      5, index, stream);
  REQUIRE(view.status == FastCompressionIndexStatus::kReady);
  REQUIRE(view.stream.idat_spans.size() == 1);
  REQUIRE(view.stream.idat_spans[0].logical_range ==
          pnga::trace_model::ZlibByteRange{
              pnga::trace_model::ZlibByteOffset{0},
              pnga::trace_model::ZlibByteOffset{6}});
  REQUIRE(view.stream.idat_spans[0].physical_range ==
          pnga::trace_model::FileByteRange{
              pnga::trace_model::FileByteOffset{1000},
              pnga::trace_model::FileByteOffset{1006}});
  REQUIRE(view.stream.total_output_bytes == 9);
  REQUIRE(view.stream.stop_input.has_value() == false);
  REQUIRE(view.stream.stop_output.has_value() == false);
  REQUIRE(view.blocks.size() == 1);
}

TEST_CASE("fast compression index derives byte-aligned deflate origins") {
  // Ordinary zlib: the payload begins after the 2-byte header.
  {
    pnga::png_format::ChunkIndex chunks;
    chunks.chunks = {idat(10, 4)};
    const pnga::png_format::VirtualIDATStream stream(chunks);
    pnga::deflate_index::BlockIndexResult index;
    index.success = true;
    index.zlib_header_bits = 16;
    index.wrapper = ZlibWrapperInfo{0x78, 0x9C, 8, 15, false, true};
    const auto view = pnga::analysis_engine::build_fast_compression_index(
        1, index, stream);
    REQUIRE(view.stream.deflate_data_begin ==
            pnga::trace_model::ZlibByteOffset{2});
  }
  // FDICT: the payload begins after 2 header + 4 DICTID bytes.
  {
    pnga::png_format::ChunkIndex chunks;
    chunks.chunks = {idat(10, 8)};
    const pnga::png_format::VirtualIDATStream stream(chunks);
    pnga::deflate_index::BlockIndexResult index;
    index.error = "inflate needs a preset dictionary";
    index.zlib_header_bits = 48;
    index.wrapper = ZlibWrapperInfo{0x78, 0x20, 8, 15, true, true};
    const auto view = pnga::analysis_engine::build_fast_compression_index(
        2, index, stream);
    REQUIRE(view.status == FastCompressionIndexStatus::kError);
    REQUIRE(view.stream.deflate_data_begin ==
            pnga::trace_model::ZlibByteOffset{6});
    REQUIRE(view.stream.wrapper.preset_dictionary);
  }
}

TEST_CASE("fast compression index reports a non-byte-aligned payload origin") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(10, 4)};
  const pnga::png_format::VirtualIDATStream stream(chunks);
  pnga::deflate_index::BlockIndexResult index;
  index.success = true;
  index.zlib_header_bits = 15;
  const auto view = pnga::analysis_engine::build_fast_compression_index(
      3, index, stream);
  REQUIRE(view.status == FastCompressionIndexStatus::kError);
  REQUIRE(view.error == "DEFLATE payload origin is not byte-aligned");
}

TEST_CASE("fast compression index reports IDAT mapping overflow") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(std::numeric_limits<std::uint64_t>::max() - 1, 4)};
  const pnga::png_format::VirtualIDATStream stream(chunks);
  pnga::deflate_index::BlockIndexResult index;
  index.success = true;
  index.zlib_header_bits = 16;
  const auto view = pnga::analysis_engine::build_fast_compression_index(
      4, index, stream);
  REQUIRE(view.status == FastCompressionIndexStatus::kError);
  REQUIRE(view.error == "fast IDAT span mapping overflowed");
}

TEST_CASE("fast compression index projects exact typed stop offsets") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(10, 8)};
  const pnga::png_format::VirtualIDATStream stream(chunks);
  pnga::deflate_index::BlockIndexResult index;
  index.error = "truncated zlib stream (no end marker)";
  index.zlib_header_bits = 16;
  index.blocks.push_back({0, pnga::deflate_index::BlockType::kStored, false,
                          16, 60, 0, 3});
  index.stop_input_bit = 60;
  index.stop_output_byte = 3;

  const auto view = pnga::analysis_engine::build_fast_compression_index(
      6, index, stream);
  REQUIRE(view.status == FastCompressionIndexStatus::kPartial);
  REQUIRE(view.stream.stop_input ==
          pnga::trace_model::ZlibBitOffset{60});
  REQUIRE(view.stream.stop_output ==
          pnga::trace_model::InflatedByteOffset{3});
  REQUIRE(view.blocks.size() == 1);
}

TEST_CASE("fast compression index preserves verified partial blocks") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(100, 2)};
  const pnga::png_format::VirtualIDATStream stream(chunks);
  pnga::deflate_index::BlockIndexResult index;
  index.error = "truncated zlib stream (no end marker)";
  index.zlib_header_bits = 16;
  index.blocks.push_back({0, pnga::deflate_index::BlockType::kStored, false,
                          8, 16, 0, 1});
  index.stop_input_bit = 16;
  index.stop_output_byte = 1;

  const auto view = pnga::analysis_engine::build_fast_compression_index(
      89, index, stream);
  REQUIRE(view.status == FastCompressionIndexStatus::kPartial);
  REQUIRE(view.error == "truncated zlib stream (no end marker)");
  REQUIRE(view.blocks.size() == 1);
}
