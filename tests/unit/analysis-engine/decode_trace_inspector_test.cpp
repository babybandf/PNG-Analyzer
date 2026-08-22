#include <pnga/analysis-engine/decode_trace_inspector.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("decode trace explains literal and match arithmetic") {
  pnga::analysis_engine::TraceQueryResult trace;
  trace.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  trace.generation = 6;
  pnga::analysis_engine::TraceTokenSummary literal;
  literal.index = 1;
  literal.kind = pnga::deflate_trace::TokenKind::kLiteral;
  literal.literal = 65;
  literal.input_bit_begin = 10;
  literal.input_bit_end = 17;
  literal.output_begin = 0;
  literal.output_end = 1;
  trace.tokens.push_back(literal);
  pnga::analysis_engine::TraceTokenSummary match;
  match.index = 2;
  match.kind = pnga::deflate_trace::TokenKind::kLengthDistance;
  match.length = 12;
  match.distance = 6;
  match.input_bit_begin = 17;
  match.input_bit_end = 31;
  match.output_begin = 1;
  match.output_end = 13;
  match.match_source_ranges.push_back({0, 7, 0});
  trace.tokens.push_back(match);

  const auto view = pnga::analysis_engine::build_decode_trace_inspector(
      trace, 2, 5);
  REQUIRE(view.status ==
          pnga::analysis_engine::DecodeTraceInspectorStatus::kReady);
  REQUIRE(view.steps.size() == 2);
  REQUIRE(view.steps[0].path ==
          pnga::analysis_engine::DecodeTracePath::kLiteral);
  REQUIRE(view.steps[0].huffman_symbol == 65);
  REQUIRE(view.steps[1].path ==
          pnga::analysis_engine::DecodeTracePath::kMatch);
  REQUIRE(view.steps[1].huffman_symbol == 265);
  REQUIRE(view.steps[1].length_base == 11);
  REQUIRE(view.steps[1].length_extra_bits == 1);
  REQUIRE(view.steps[1].length_extra_value == 1);
  REQUIRE(view.steps[1].distance_base == 5);
  REQUIRE(view.steps[1].distance_extra_bits == 1);
  REQUIRE(view.steps[1].distance_extra_value == 1);
  REQUIRE(view.steps[1].selected);
  REQUIRE(view.steps[1].selected_output_byte == 5);
  REQUIRE(view.steps[1].match_source_ranges.size() == 1);
}

TEST_CASE("decode trace preserves partial state and EOB") {
  pnga::analysis_engine::TraceQueryResult trace;
  trace.status = pnga::analysis_engine::TraceQueryStatus::kPartial;
  pnga::analysis_engine::TraceTokenSummary eob;
  eob.index = 4;
  eob.kind = pnga::deflate_trace::TokenKind::kEndOfBlock;
  eob.input_bit_begin = 40;
  eob.input_bit_end = 40;
  trace.tokens.push_back(eob);
  const auto view =
      pnga::analysis_engine::build_decode_trace_inspector(trace);
  REQUIRE(view.status ==
          pnga::analysis_engine::DecodeTraceInspectorStatus::kPartial);
  REQUIRE(view.steps.size() == 1);
  REQUIRE(view.steps[0].path ==
          pnga::analysis_engine::DecodeTracePath::kEndOfBlock);
  REQUIRE(view.steps[0].huffman_symbol == 256);
}
