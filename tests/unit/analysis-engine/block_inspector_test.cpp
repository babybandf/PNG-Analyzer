#include <pnga/analysis-engine/block_inspector.h>

#include <catch2/catch_test_macros.hpp>

using pnga::analysis_engine::BlockInspectorStatus;
using pnga::analysis_engine::TraceBlockSummary;
using pnga::analysis_engine::TraceQueryResult;
using pnga::analysis_engine::TraceQueryStatus;

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

