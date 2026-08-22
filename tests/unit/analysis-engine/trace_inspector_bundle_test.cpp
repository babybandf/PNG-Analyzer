#include <pnga/analysis-engine/trace_inspector_bundle.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("trace inspector bundle keeps one generation across pages") {
  pnga::analysis_engine::TraceQueryResult trace;
  trace.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  trace.generation = 77;
  pnga::analysis_engine::TraceBlockSummary block;
  block.index = 0;
  block.type = pnga::deflate_index::BlockType::kFixed;
  block.output_begin = 0;
  block.output_end = 4;
  trace.blocks.push_back(block);
  pnga::analysis_engine::TraceTokenSummary token;
  token.index = 3;
  token.kind = pnga::deflate_trace::TokenKind::kLiteral;
  token.literal = 0x41;
  token.input_bit_begin = 8;
  token.input_bit_end = 15;
  token.output_begin = 1;
  token.output_end = 2;
  trace.tokens.push_back(token);

  const auto bundle = pnga::analysis_engine::build_trace_inspector_bundle(
      trace, 3, 1, 5);
  REQUIRE(bundle.generation == 77);
  REQUIRE(bundle.block.generation == 77);
  REQUIRE(bundle.huffman.generation == 77);
  REQUIRE(bundle.decode.generation == 77);
  REQUIRE(bundle.block.selected_block_index == 0);
  REQUIRE(bundle.decode.steps[0].selected);
}

TEST_CASE("trace-to-original-literal is explicitly bounded") {
  pnga::analysis_engine::TraceQueryResult trace;
  trace.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  pnga::analysis_engine::TraceTokenSummary literal;
  literal.index = 1;
  literal.kind = pnga::deflate_trace::TokenKind::kLiteral;
  trace.tokens.push_back(literal);
  pnga::analysis_engine::TraceTokenSummary match;
  match.index = 2;
  match.kind = pnga::deflate_trace::TokenKind::kLengthDistance;
  match.match_source_ranges.push_back({0, 1, 1});
  trace.tokens.push_back(match);

  const auto ready = pnga::analysis_engine::trace_to_original_literal(
      trace, 2, 4, 4);
  REQUIRE(ready.status ==
          pnga::analysis_engine::TraceLiteralWalkStatus::kReady);
  REQUIRE(ready.token_path == std::vector<std::uint64_t>{2, 1});

  const auto limited = pnga::analysis_engine::trace_to_original_literal(
      trace, 2, 1, 4);
  REQUIRE(limited.status ==
          pnga::analysis_engine::TraceLiteralWalkStatus::kBudgetExceeded);

  const auto nav = pnga::analysis_engine::trace_token_navigation(
      trace, 2,
      pnga::analysis_engine::TraceNavigationRange::Space::kLogicalDeflate);
  REQUIRE(nav.has_value());
}
