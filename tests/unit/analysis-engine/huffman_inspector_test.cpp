#include <pnga/analysis-engine/huffman_inspector.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("huffman inspector keeps build order and selected literal") {
  pnga::analysis_engine::TraceQueryResult trace;
  trace.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  trace.generation = 17;
  pnga::analysis_engine::TraceBlockSummary block;
  block.index = 5;
  block.type = pnga::deflate_index::BlockType::kDynamic;
  block.input_bit_begin = 0;
  block.input_bit_end = 100;
  trace.blocks.push_back(block);
  pnga::analysis_engine::TraceTokenSummary token;
  token.index = 8;
  token.kind = pnga::deflate_trace::TokenKind::kLiteral;
  token.literal = 65;
  token.input_bit_begin = 70;
  token.input_bit_end = 74;
  trace.tokens.push_back(token);
  pnga::analysis_engine::TraceHuffmanTableSummary table;
  table.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  table.entries.push_back({65, 3, 1, 10, 13});
  trace.huffman_tables.push_back(table);

  const auto view = pnga::analysis_engine::build_huffman_inspector(trace, 8);
  REQUIRE(view.status ==
          pnga::analysis_engine::HuffmanInspectorStatus::kReady);
  REQUIRE(view.selected_input_bit_begin == 70);
  REQUIRE(view.selected_input_bit_end == 74);
  REQUIRE(view.tables.size() == 1);
  REQUIRE(view.tables[0].mode ==
          pnga::analysis_engine::HuffmanTableMode::kDynamic);
  REQUIRE(view.tables[0].build_order == 0);
  REQUIRE(view.tables[0].entries.size() == 1);
  REQUIRE(view.tables[0].entries[0].selected);
  REQUIRE(view.tables[0].entries[0].read_order_code == 4);
}

TEST_CASE("huffman inspector exposes stored and fixed capabilities") {
  pnga::analysis_engine::TraceQueryResult trace;
  trace.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  pnga::analysis_engine::TraceBlockSummary stored;
  stored.index = 1;
  stored.type = pnga::deflate_index::BlockType::kStored;
  trace.blocks.push_back(stored);
  pnga::analysis_engine::TraceBlockSummary fixed;
  fixed.index = 2;
  fixed.type = pnga::deflate_index::BlockType::kFixed;
  trace.blocks.push_back(fixed);
  const auto view = pnga::analysis_engine::build_huffman_inspector(trace);
  REQUIRE(view.tables.size() == 3);
  REQUIRE(view.tables[0].declared_entry_count == 2);
  REQUIRE(view.tables[1].declared_entry_count == 288);
  REQUIRE(view.tables[2].declared_entry_count == 32);
  REQUIRE(pnga::analysis_engine::serialize_huffman_inspector(view) ==
          "status=ready;generation=0;selected_token=-;selected_bits=-;"
          "error=;tables=3|1,stored,none,0,2,entries=0|2,fixed,literal_length,"
          "1,288,entries=0|2,fixed,distance,2,32,entries=0");
}

TEST_CASE("huffman provenance is matched in Deflate-relative bit space") {
  pnga::analysis_engine::TraceQueryResult trace;
  trace.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  trace.deflate_data_begin = 2;  // zlib wrapper occupies 16 stream bits.
  pnga::analysis_engine::TraceBlockSummary block;
  block.index = 7;
  block.type = pnga::deflate_index::BlockType::kDynamic;
  block.input_bit_begin = 16;
  block.input_bit_end = 80;
  trace.blocks.push_back(block);

  pnga::analysis_engine::TraceHuffmanTableSummary table;
  table.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  // Decoder provenance is relative to the Deflate payload, not zlib stream.
  table.entries.push_back({65, 7, 42, 8, 12});
  trace.huffman_tables.push_back(table);

  const auto view = pnga::analysis_engine::build_huffman_inspector(trace);
  REQUIRE(view.tables.size() == 1);
  REQUIRE(view.tables[0].block_index == 7);
}
