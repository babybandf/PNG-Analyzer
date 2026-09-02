// WP-5U12E Decode Trace projection tests: exact Literal/Match/EOB event
// semantics, typed compressed-input and inflated-output ranges, per-token
// physical file spans, bounded scope facts, Current-byte provenance and
// Partial/Error retention with stable stop reasons.

#include <pnga/analysis-engine/decode_trace_inspector.h>

#include <pnga/trace-model/offset_range.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using pnga::analysis_engine::DecodeTracePath;
using pnga::analysis_engine::TraceQueryResult;
using pnga::analysis_engine::TraceQueryStatus;
using pnga::analysis_engine::TraceTokenSummary;
using pnga::analysis_engine::build_decode_trace_inspector;
using pnga::deflate_trace::TokenKind;
using pnga::deflate_trace::TokenOutputRange;
using pnga::trace_model::DeflateBitOffset;
using pnga::trace_model::DeflateBitRange;
using pnga::trace_model::FileByteOffset;
using pnga::trace_model::FileByteRange;
using pnga::trace_model::InflatedByteOffset;
using pnga::trace_model::InflatedByteRange;

TraceTokenSummary huffman_literal(std::uint64_t index, std::uint8_t value,
                                  std::uint64_t bit_begin,
                                  std::uint64_t output) {
  TraceTokenSummary token;
  token.index = index;
  token.kind = TokenKind::kLiteral;
  token.literal = value;
  token.huffman_symbol = value;
  token.input_bit_begin = bit_begin;
  token.input_bit_end = bit_begin + 8;
  token.output_begin = output;
  token.output_end = output + 1;
  token.block_index = 0;
  token.physical_input_spans.push_back(
      FileByteRange{FileByteOffset{40}, FileByteOffset{41}});
  return token;
}

TraceTokenSummary match_token(std::uint64_t index, std::uint16_t length,
                              std::uint16_t distance, std::uint64_t bit_begin,
                              std::uint64_t output_begin,
                              std::vector<TokenOutputRange> sources) {
  TraceTokenSummary token;
  token.index = index;
  token.kind = TokenKind::kLengthDistance;
  token.length = length;
  token.distance = distance;
  token.huffman_symbol = static_cast<std::uint16_t>(257 + (length >= 11 ? 11 : 0));
  token.input_bit_begin = bit_begin;
  token.input_bit_end = bit_begin + 14;
  token.output_begin = output_begin;
  token.output_end = output_begin + length;
  token.block_index = 0;
  token.match_source_ranges = std::move(sources);
  token.physical_input_spans.push_back(
      FileByteRange{FileByteOffset{40}, FileByteOffset{42}});
  return token;
}

}  // namespace

TEST_CASE("decode trace explains literal, match and end-of-block events") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  trace.generation = 6;
  trace.inflated_begin = 0;
  trace.inflated_end = 14;
  trace.tokens.push_back(huffman_literal(1, 0x41, 10, 0));
  trace.tokens.push_back(match_token(
      2, 18, 7, 17, 1, {TokenOutputRange{0, 7, 1}}));
  pnga::analysis_engine::TraceTokenSummary eob;
  eob.index = 3;
  eob.kind = TokenKind::kEndOfBlock;
  eob.huffman_symbol = 256;
  eob.input_bit_begin = 31;
  eob.input_bit_end = 38;
  eob.block_index = 0;
  trace.tokens.push_back(eob);

  const auto view = build_decode_trace_inspector(trace, std::nullopt, 5);
  REQUIRE(view.scope.generation == 6);
  REQUIRE(view.scope.status == TraceQueryStatus::kReady);
  REQUIRE(view.scope.requested_output ==
          InflatedByteRange{InflatedByteOffset{0}, InflatedByteOffset{14}});
  REQUIRE(view.scope.returned_token_count == 3);
  REQUIRE_FALSE(view.scope.truncated);
  REQUIRE(view.steps.size() == 3);

  const auto& literal = view.steps[0];
  REQUIRE(literal.path == DecodeTracePath::kLiteral);
  REQUIRE(literal.event_text == "Literal 0x41");
  REQUIRE(literal.literal == 0x41);
  REQUIRE(literal.huffman_symbol == std::optional<std::uint16_t>{65});
  REQUIRE(literal.input_range ==
          DeflateBitRange{DeflateBitOffset{10}, DeflateBitOffset{18}});
  REQUIRE(literal.output_range ==
          InflatedByteRange{InflatedByteOffset{0}, InflatedByteOffset{1}});

  const auto& match = view.steps[1];
  REQUIRE(match.path == DecodeTracePath::kMatch);
  REQUIRE(match.event_text == "Match len 18 / dist 7");
  REQUIRE(match.length == 18);
  REQUIRE(match.length_base == 17);
  REQUIRE(match.length_extra_bits == 1);
  REQUIRE(match.length_extra_value == 1);
  REQUIRE(match.distance == 7);
  REQUIRE(match.distance_base == 7);
  REQUIRE(match.distance_extra_bits == 1);
  REQUIRE(match.distance_extra_value == 0);
  REQUIRE(match.huffman_symbol == std::optional<std::uint16_t>{268});
  // The match target is the inflated copy destination and the root source
  // ranges are carried verbatim.
  REQUIRE(match.match_target == match.output_range);
  REQUIRE(match.output_range ==
          InflatedByteRange{InflatedByteOffset{1}, InflatedByteOffset{19}});
  REQUIRE(match.match_source_ranges ==
          std::vector<TokenOutputRange>{TokenOutputRange{0, 7, 1}});

  const auto& eob_step = view.steps[2];
  REQUIRE(eob_step.path == DecodeTracePath::kEndOfBlock);
  REQUIRE(eob_step.event_text == "End of block");
  REQUIRE(eob_step.huffman_symbol == std::optional<std::uint16_t>{256});
  REQUIRE(eob_step.output_range.empty());
  REQUIRE(eob_step.match_target.empty());
}

TEST_CASE("decode trace distinguishes overlapping and disjoint copies") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  // length 18 / distance 7: the immediate copy source [93, 111) overlaps the
  // target [100, 118).
  trace.tokens.push_back(
      match_token(0, 18, 7, 0, 100, {TokenOutputRange{82, 100, 0}}));
  // length 4 / distance 10: the source [90, 94) precedes the target [100,
  // 104) without touching it.
  trace.tokens.push_back(
      match_token(1, 4, 10, 20, 100, {TokenOutputRange{90, 94, 0}}));
  const auto view = build_decode_trace_inspector(trace);
  REQUIRE(view.steps.size() == 2);
  REQUIRE(view.steps[0].match_overlaps);
  REQUIRE_FALSE(view.steps[1].match_overlaps);
  REQUIRE(view.steps[0].match_target ==
          InflatedByteRange{InflatedByteOffset{100}, InflatedByteOffset{118}});
  REQUIRE(view.steps[1].match_target ==
          InflatedByteRange{InflatedByteOffset{100}, InflatedByteOffset{104}});
}

TEST_CASE("decode trace marks the current byte at first, middle and last") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  trace.tokens.push_back(
      match_token(0, 18, 7, 0, 100, {TokenOutputRange{90, 108, 0}}));
  const auto first = build_decode_trace_inspector(trace, std::nullopt, 100);
  REQUIRE(first.steps.front().contains_current);
  REQUIRE(first.steps.front().selected_byte_offset_in_event ==
          std::optional<std::uint64_t>{0});
  const auto middle = build_decode_trace_inspector(trace, std::nullopt, 109);
  REQUIRE(middle.steps.front().contains_current);
  REQUIRE(middle.steps.front().selected_byte_offset_in_event ==
          std::optional<std::uint64_t>{9});
  const auto last = build_decode_trace_inspector(trace, std::nullopt, 117);
  REQUIRE(last.steps.front().contains_current);
  REQUIRE(last.steps.front().selected_byte_offset_in_event ==
          std::optional<std::uint64_t>{17});
  // A byte outside the event leaves the step unmarked and the relative
  // offset unset.
  const auto outside = build_decode_trace_inspector(trace, std::nullopt, 118);
  REQUIRE_FALSE(outside.steps.front().contains_current);
  REQUIRE_FALSE(outside.steps.front().selected_byte_offset_in_event.has_value());
  // An explicit manual token selection marks only the matching step.
  const auto manual = build_decode_trace_inspector(trace, 0, std::nullopt);
  REQUIRE(manual.steps.front().selected);
  REQUIRE_FALSE(manual.steps.front().contains_current);
}

TEST_CASE("decode trace copies typed input ranges and physical spans") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  TraceTokenSummary one_span = huffman_literal(0, 0x41, 0, 0);
  TraceTokenSummary two_spans = huffman_literal(1, 0x42, 8, 1);
  two_spans.input_bit_begin = 8;
  two_spans.input_bit_end = 24;
  two_spans.physical_input_spans.push_back(
      FileByteRange{FileByteOffset{57}, FileByteOffset{60}});
  trace.tokens.push_back(one_span);
  trace.tokens.push_back(two_spans);

  const auto view = build_decode_trace_inspector(trace);
  REQUIRE(view.steps[0].physical_input_spans.size() == 1);
  REQUIRE(view.steps[0].physical_input_spans.front() ==
          FileByteRange{FileByteOffset{40}, FileByteOffset{41}});
  REQUIRE(view.steps[1].physical_input_spans.size() == 2);
  REQUIRE(view.steps[1].physical_input_spans.front() ==
          FileByteRange{FileByteOffset{40}, FileByteOffset{41}});
  REQUIRE(view.steps[1].physical_input_spans.back() ==
          FileByteRange{FileByteOffset{57}, FileByteOffset{60}});
  REQUIRE(view.steps[1].input_range ==
          DeflateBitRange{DeflateBitOffset{8}, DeflateBitOffset{24}});
}

TEST_CASE("decode trace preserves partial results and stop reasons") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kPartial;
  trace.generation = 12;
  trace.truncated = true;
  trace.error = "trace token budget exceeded";
  trace.tokens.push_back(huffman_literal(0, 0x41, 0, 0));

  const auto view = build_decode_trace_inspector(trace);
  REQUIRE(view.scope.status == TraceQueryStatus::kPartial);
  REQUIRE(view.scope.truncated);
  REQUIRE(view.scope.stop_reason == "trace token budget exceeded");
  REQUIRE(view.scope.returned_token_count == 1);
  REQUIRE(view.steps.size() == 1);
  REQUIRE(view.steps.front().event_text == "Literal 0x41");
}

TEST_CASE("decode trace keeps an invalid-distance stop reason") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kPartial;
  trace.error = "invalid distance code";
  trace.tokens.push_back(huffman_literal(0, 0x41, 0, 0));

  const auto view = build_decode_trace_inspector(trace);
  REQUIRE(view.scope.status == TraceQueryStatus::kPartial);
  REQUIRE(view.scope.stop_reason == "invalid distance code");
  // The verified prefix stays browsable beside the stop reason.
  REQUIRE(view.steps.size() == 1);
}

TEST_CASE("decode trace rejects lengths outside the RFC 1951 tables") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  trace.tokens.push_back(huffman_literal(0, 0x41, 0, 0));
  // length 2 has no RFC 1951 length code; the step cannot be projected.
  trace.tokens.push_back(match_token(1, 2, 7, 8, 1, {}));

  const auto view = build_decode_trace_inspector(trace);
  REQUIRE(view.scope.status == TraceQueryStatus::kError);
  REQUIRE(view.scope.stop_reason ==
          "token length/distance is outside RFC 1951 ranges");
  // The verified literal row is retained; only the broken step is dropped.
  REQUIRE(view.steps.size() == 1);
  REQUIRE(view.steps.front().token_index == 0);
}
