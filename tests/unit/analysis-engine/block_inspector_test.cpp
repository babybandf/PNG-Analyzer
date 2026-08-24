#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

using pnga::analysis_engine::BlockInspectorStatus;
using pnga::analysis_engine::TraceBlockSummary;
using pnga::analysis_engine::TraceQueryResult;
using pnga::analysis_engine::TraceQueryStatus;

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
  index.adler_ok = true;
  index.blocks.push_back({0, pnga::deflate_index::BlockType::kDynamic, true,
                          8, 32, 0, 12});

  const auto view = pnga::analysis_engine::build_fast_compression_index(
      88, index, stream);
  REQUIRE(view.status ==
          pnga::analysis_engine::FastCompressionIndexStatus::kReady);
  REQUIRE(view.generation == 88);
  REQUIRE(view.stream.stream_range.begin ==
          pnga::trace_model::ZlibByteOffset{0});
  REQUIRE(view.stream.stream_range.end ==
          pnga::trace_model::ZlibByteOffset{4});
  REQUIRE(view.stream.deflate_data_begin ==
          pnga::trace_model::ZlibBitOffset{16});
  REQUIRE(view.stream.idat_segment_count == 2);
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

TEST_CASE("fast compression index preserves verified partial blocks") {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(100, 2)};
  const pnga::png_format::VirtualIDATStream stream(chunks);
  pnga::deflate_index::BlockIndexResult index;
  index.error = "truncated zlib stream (no end marker)";
  index.blocks.push_back({0, pnga::deflate_index::BlockType::kStored, false,
                          8, 16, 0, 1});

  const auto view = pnga::analysis_engine::build_fast_compression_index(
      89, index, stream);
  REQUIRE(view.status ==
          pnga::analysis_engine::FastCompressionIndexStatus::kPartial);
  REQUIRE(view.error == "truncated zlib stream (no end marker)");
  REQUIRE(view.blocks.size() == 1);
}
