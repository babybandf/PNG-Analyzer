// WP-505B / WP-5U12D Huffman Inspector projection tests: the Stored/Fixed/
// Dynamic table states, exact canonical/read-order bit strings, bounded
// occurrence ids scoped to the selected block, selected-symbol marking and
// Partial/Error retention — all produced from structured trace facts only.

#include <pnga/analysis-engine/huffman_inspector.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using pnga::analysis_engine::HuffmanInspectorEntry;
using pnga::analysis_engine::HuffmanInspectorTable;
using pnga::analysis_engine::HuffmanInspectorStatus;
using pnga::analysis_engine::HuffmanTableMode;
using pnga::analysis_engine::TraceBlockSummary;
using pnga::analysis_engine::TraceQueryResult;
using pnga::analysis_engine::TraceQueryStatus;
using pnga::analysis_engine::TraceTokenSummary;
using pnga::deflate_index::BlockType;
using pnga::deflate_trace::HuffmanTableKind;
using pnga::deflate_trace::TokenKind;
using pnga::trace_model::DeflateBitOffset;
using pnga::trace_model::DeflateBitRange;
using pnga::trace_model::ProvenanceSpace;
using pnga::trace_model::ProvenanceSpan;

TraceTokenSummary make_token(std::uint64_t index, TokenKind kind,
                             std::int64_t block_index,
                             std::optional<std::uint16_t> symbol,
                             std::uint64_t input_begin = 0,
                             std::uint64_t input_end = 0) {
  TraceTokenSummary token;
  token.index = index;
  token.kind = kind;
  token.block_index = block_index;
  token.huffman_symbol = symbol;
  token.input_bit_begin = input_begin;
  token.input_bit_end = input_end;
  return token;
}

const HuffmanInspectorEntry* find_entry(
    const HuffmanInspectorTable& table, std::uint16_t symbol) {
  for (const auto& entry : table.entries) {
    if (entry.symbol == symbol) {
      return &entry;
    }
  }
  return nullptr;
}

// A dynamic block at DEFLATE bits [0, 64) with code-length, literal/length
// and distance tables plus four bounded tokens (two literals, one match, one
// end-of-block).
TraceQueryResult dynamic_trace() {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  trace.generation = 3;
  trace.deflate_data_begin = 2;
  TraceBlockSummary block;
  block.index = 7;
  block.type = BlockType::kDynamic;
  block.input_bit_begin = 16;
  block.input_bit_end = 80;
  block.physical_spans.push_back(
      ProvenanceSpan{ProvenanceSpace::kPhysicalFile, 100, 10, 0, 80, true});
  trace.blocks.push_back(block);

  pnga::analysis_engine::TraceHuffmanTableSummary code_length;
  code_length.kind = HuffmanTableKind::kCodeLength;
  code_length.entries.push_back({16, 2, 0, 0, 6});
  code_length.entries.push_back({17, 2, 1, 6, 12});
  trace.huffman_tables.push_back(code_length);

  pnga::analysis_engine::TraceHuffmanTableSummary literal_length;
  literal_length.kind = HuffmanTableKind::kLiteralLength;
  literal_length.entries.push_back({256, 1, 0, 26, 27});
  literal_length.entries.push_back({65, 3, 4, 20, 23});
  literal_length.entries.push_back({268, 3, 5, 23, 26});
  literal_length.entries.push_back({66, 0, 0, 26, 27});
  trace.huffman_tables.push_back(literal_length);

  pnga::analysis_engine::TraceHuffmanTableSummary distance;
  distance.kind = HuffmanTableKind::kDistance;
  distance.entries.push_back({0, 1, 0, 30, 31});
  trace.huffman_tables.push_back(distance);

  trace.tokens.push_back(make_token(3, TokenKind::kLiteral, 7, 65, 4, 12));
  trace.tokens.push_back(make_token(4, TokenKind::kLengthDistance, 7, 268, 12,
                                    20));
  trace.tokens.push_back(make_token(5, TokenKind::kLiteral, 7, 65, 20, 28));
  trace.tokens.push_back(make_token(6, TokenKind::kEndOfBlock, 7, 256, 28,
                                    28));
  return trace;
}

}  // namespace

TEST_CASE("stored block projects the explicit no-Huffman state") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  trace.generation = 5;
  trace.deflate_data_begin = 2;
  TraceBlockSummary block;
  block.index = 1;
  block.type = BlockType::kStored;
  block.input_bit_begin = 16;
  block.input_bit_end = 144;
  block.physical_spans.push_back(
      ProvenanceSpan{ProvenanceSpace::kPhysicalFile, 100, 16, 0, 128, true});
  trace.blocks.push_back(block);
  trace.tokens.push_back(make_token(0, TokenKind::kLiteral, 1, std::nullopt));
  trace.tokens.push_back(make_token(1, TokenKind::kLiteral, 1, std::nullopt));
  trace.tokens.push_back(make_token(2, TokenKind::kEndOfBlock, 1, std::nullopt));

  const auto view = pnga::analysis_engine::build_huffman_inspector(trace);
  REQUIRE(view.status == HuffmanInspectorStatus::kReady);
  REQUIRE(view.tables.size() == 1);
  const auto& table = view.tables[0];
  REQUIRE(table.block_index == 1);
  REQUIRE(table.mode == HuffmanTableMode::kStored);
  REQUIRE_FALSE(table.kind.has_value());
  REQUIRE(table.selector_label == "LEN/NLEN");
  REQUIRE(table.build_order == 0);
  REQUIRE(table.declared_entry_count == 2);
  REQUIRE(table.bounded_token_count == 3);
  REQUIRE(table.entries.empty());
  REQUIRE_FALSE(table.truncated);

  REQUIRE(view.block_scopes.size() == 1);
  REQUIRE(view.block_scopes[0].block_index == 1);
  REQUIRE(view.block_scopes[0].deflate_range ==
          DeflateBitRange{DeflateBitOffset{0}, DeflateBitOffset{128}});
  REQUIRE(view.block_scopes[0].physical_spans.size() == 1);
  REQUIRE(view.occurrences.empty());

  REQUIRE(pnga::analysis_engine::serialize_huffman_inspector(view) ==
          "status=ready;generation=5;selected_token=-;selected_bits=-;"
          "error=;tables=1|1,stored,none,LEN/NLEN,0,2,3,0,entries=0");
}

TEST_CASE("fixed block projects predefined tables with exact bit strings") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  trace.deflate_data_begin = 2;
  TraceBlockSummary block;
  block.index = 2;
  block.type = BlockType::kFixed;
  block.input_bit_begin = 16;
  block.input_bit_end = 800;
  block.physical_spans.push_back(
      ProvenanceSpan{ProvenanceSpace::kPhysicalFile, 100, 100, 0, 784, true});
  trace.blocks.push_back(block);
  trace.tokens.push_back(make_token(0, TokenKind::kLiteral, 2, 65, 16, 24));
  trace.tokens.push_back(
      make_token(1, TokenKind::kLengthDistance, 2, 268, 24, 32));
  trace.tokens.push_back(make_token(2, TokenKind::kEndOfBlock, 2, 256, 32,
                                    32));

  const auto view = pnga::analysis_engine::build_huffman_inspector(trace);
  REQUIRE(view.status == HuffmanInspectorStatus::kReady);
  REQUIRE(view.tables.size() == 2);

  const auto& literal = view.tables[0];
  REQUIRE(literal.block_index == 2);
  REQUIRE(literal.mode == HuffmanTableMode::kFixed);
  REQUIRE(literal.kind == HuffmanTableKind::kLiteralLength);
  REQUIRE(literal.selector_label == "Literal / Length");
  REQUIRE(literal.build_order == 0);
  REQUIRE(literal.declared_entry_count == 288);
  REQUIRE(literal.entries.size() == 288);
  REQUIRE(literal.bounded_token_count == 3);

  // RFC 1951 section 3.2.6 predefined literal/length goldens, including
  // leading zeroes and the transmitted read order.
  const auto* zero = find_entry(literal, 0);
  REQUIRE(zero != nullptr);
  REQUIRE(zero->bit_length == 8);
  REQUIRE(zero->canonical_code == 48);
  REQUIRE(zero->canonical_bits == "00110000");
  REQUIRE(zero->read_order_code == 12);
  REQUIRE(zero->read_order_bits == "00001100");
  REQUIRE(zero->meaning == "literal 0");
  REQUIRE(zero->occurrence_token_indices.empty());

  const auto* sixty_five = find_entry(literal, 65);
  REQUIRE(sixty_five != nullptr);
  REQUIRE(sixty_five->bit_length == 8);
  REQUIRE(sixty_five->canonical_code == 113);
  REQUIRE(sixty_five->canonical_bits == "01110001");
  REQUIRE(sixty_five->read_order_code == 142);
  REQUIRE(sixty_five->read_order_bits == "10001110");
  REQUIRE(sixty_five->meaning == "literal 65");
  REQUIRE(sixty_five->occurrence_token_indices ==
          std::vector<std::uint64_t>{0});

  const auto* eob = find_entry(literal, 256);
  REQUIRE(eob != nullptr);
  REQUIRE(eob->bit_length == 7);
  REQUIRE(eob->canonical_code == 0);
  REQUIRE(eob->canonical_bits == "0000000");
  REQUIRE(eob->read_order_code == 0);
  REQUIRE(eob->read_order_bits == "0000000");
  REQUIRE(eob->meaning == "end-of-block");
  REQUIRE(eob->occurrence_token_indices == std::vector<std::uint64_t>{2});

  const auto* length_3 = find_entry(literal, 257);
  REQUIRE(length_3 != nullptr);
  REQUIRE(length_3->bit_length == 7);
  REQUIRE(length_3->canonical_code == 1);
  REQUIRE(length_3->canonical_bits == "0000001");
  REQUIRE(length_3->read_order_code == 64);
  REQUIRE(length_3->read_order_bits == "1000000");
  REQUIRE(length_3->meaning == "length 3");
  REQUIRE(length_3->occurrence_token_indices.empty());

  const auto* length_17 = find_entry(literal, 268);
  REQUIRE(length_17 != nullptr);
  REQUIRE(length_17->bit_length == 7);
  REQUIRE(length_17->canonical_code == 12);
  REQUIRE(length_17->canonical_bits == "0001100");
  REQUIRE(length_17->read_order_code == 24);
  REQUIRE(length_17->read_order_bits == "0011000");
  REQUIRE(length_17->meaning == "length 17-18");
  REQUIRE(length_17->occurrence_token_indices ==
          std::vector<std::uint64_t>{1});

  const auto* length_258 = find_entry(literal, 285);
  REQUIRE(length_258 != nullptr);
  REQUIRE(length_258->bit_length == 8);
  REQUIRE(length_258->canonical_code == 197);
  REQUIRE(length_258->canonical_bits == "11000101");
  REQUIRE(length_258->read_order_code == 163);
  REQUIRE(length_258->read_order_bits == "10100011");
  REQUIRE(length_258->meaning == "length 258");

  const auto* reserved_literal = find_entry(literal, 286);
  REQUIRE(reserved_literal != nullptr);
  REQUIRE(reserved_literal->meaning == "reserved");

  const auto& dist = view.tables[1];
  REQUIRE(dist.mode == HuffmanTableMode::kFixed);
  REQUIRE(dist.kind == HuffmanTableKind::kDistance);
  REQUIRE(dist.selector_label == "Distance");
  REQUIRE(dist.build_order == 1);
  REQUIRE(dist.declared_entry_count == 32);
  REQUIRE(dist.entries.size() == 32);
  REQUIRE(dist.bounded_token_count == 3);
  const auto* distance_0 = find_entry(dist, 0);
  REQUIRE(distance_0 != nullptr);
  REQUIRE(distance_0->bit_length == 5);
  REQUIRE(distance_0->canonical_code == 0);
  REQUIRE(distance_0->canonical_bits == "00000");
  REQUIRE(distance_0->read_order_bits == "00000");
  REQUIRE(distance_0->meaning == "distance 1");
  const auto* distance_5 = find_entry(dist, 4);
  REQUIRE(distance_5 != nullptr);
  REQUIRE(distance_5->meaning == "distance 5-6");
  const auto* reserved_distance = find_entry(dist, 30);
  REQUIRE(reserved_distance != nullptr);
  REQUIRE(reserved_distance->meaning == "reserved");
  // Distance symbols are not captured per token in this stage; no distance
  // entry may fabricate occurrence ids.
  for (const auto& entry : dist.entries) {
    REQUIRE(entry.occurrence_token_indices.empty());
  }

  REQUIRE(view.block_scopes.size() == 1);
  REQUIRE(view.block_scopes[0].block_index == 2);
  REQUIRE(view.block_scopes[0].deflate_range ==
          DeflateBitRange{DeflateBitOffset{0}, DeflateBitOffset{784}});
  REQUIRE(view.occurrences.size() == 3);
  REQUIRE(view.occurrences[0].token_index == 0);
  REQUIRE(view.occurrences[0].input_range ==
          DeflateBitRange{DeflateBitOffset{16}, DeflateBitOffset{24}});
  REQUIRE(view.occurrences[2].input_range ==
          DeflateBitRange{DeflateBitOffset{32}, DeflateBitOffset{32}});
}

TEST_CASE("dynamic tables project build order, exact strings and bounded "
          "occurrences") {
  const TraceQueryResult trace = dynamic_trace();
  const auto view = pnga::analysis_engine::build_huffman_inspector(trace);
  REQUIRE(view.status == HuffmanInspectorStatus::kReady);
  REQUIRE(view.tables.size() == 3);

  const auto& code_length = view.tables[0];
  REQUIRE(code_length.mode == HuffmanTableMode::kDynamic);
  REQUIRE(code_length.kind == HuffmanTableKind::kCodeLength);
  REQUIRE(code_length.selector_label == "Code Length");
  REQUIRE(code_length.build_order == 0);
  REQUIRE(code_length.declared_entry_count == 2);
  REQUIRE(code_length.entries.size() == 2);
  const auto* repeat_16 = find_entry(code_length, 16);
  REQUIRE(repeat_16 != nullptr);
  REQUIRE(repeat_16->canonical_bits == "00");
  REQUIRE(repeat_16->read_order_bits == "00");
  REQUIRE(repeat_16->meaning == "repeat previous length 3-6");
  const auto* repeat_17 = find_entry(code_length, 17);
  REQUIRE(repeat_17 != nullptr);
  REQUIRE(repeat_17->canonical_bits == "01");
  REQUIRE(repeat_17->read_order_bits == "10");
  REQUIRE(repeat_17->meaning == "repeat zero length 3-10");

  const auto& literal = view.tables[1];
  REQUIRE(literal.kind == HuffmanTableKind::kLiteralLength);
  REQUIRE(literal.selector_label == "Literal / Length");
  REQUIRE(literal.build_order == 1);
  REQUIRE(literal.declared_entry_count == 4);
  REQUIRE(literal.entries.size() == 4);
  const auto* eob = find_entry(literal, 256);
  REQUIRE(eob != nullptr);
  REQUIRE(eob->bit_length == 1);
  REQUIRE(eob->canonical_code == 0);
  REQUIRE(eob->canonical_bits == "0");
  REQUIRE(eob->read_order_bits == "0");
  REQUIRE(eob->meaning == "end-of-block");
  REQUIRE(eob->occurrence_token_indices == std::vector<std::uint64_t>{6});
  const auto* sixty_five = find_entry(literal, 65);
  REQUIRE(sixty_five != nullptr);
  REQUIRE(sixty_five->bit_length == 3);
  REQUIRE(sixty_five->canonical_code == 4);
  REQUIRE(sixty_five->canonical_bits == "100");
  REQUIRE(sixty_five->read_order_code == 1);
  REQUIRE(sixty_five->read_order_bits == "001");
  REQUIRE(sixty_five->meaning == "literal 65");
  REQUIRE(sixty_five->provenance_range ==
          DeflateBitRange{DeflateBitOffset{20}, DeflateBitOffset{23}});
  REQUIRE(sixty_five->occurrence_token_indices ==
          std::vector<std::uint64_t>{3, 5});
  const auto* length_17 = find_entry(literal, 268);
  REQUIRE(length_17 != nullptr);
  REQUIRE(length_17->canonical_bits == "101");
  REQUIRE(length_17->read_order_bits == "101");
  REQUIRE(length_17->meaning == "length 17-18");
  REQUIRE(length_17->occurrence_token_indices ==
          std::vector<std::uint64_t>{4});
  // Zero-bit entries are retained with empty bit strings, never destroyed.
  const auto* unused = find_entry(literal, 66);
  REQUIRE(unused != nullptr);
  REQUIRE(unused->bit_length == 0);
  REQUIRE(unused->canonical_bits.empty());
  REQUIRE(unused->read_order_bits.empty());
  REQUIRE(unused->meaning == "literal 66");
  REQUIRE(unused->occurrence_token_indices.empty());

  const auto& dist = view.tables[2];
  REQUIRE(dist.kind == HuffmanTableKind::kDistance);
  REQUIRE(dist.build_order == 2);
  const auto* distance_0 = find_entry(dist, 0);
  REQUIRE(distance_0 != nullptr);
  REQUIRE(distance_0->canonical_bits == "0");
  REQUIRE(distance_0->read_order_bits == "0");
  REQUIRE(distance_0->meaning == "distance 1");

  for (const auto& table : view.tables) {
    REQUIRE(table.bounded_token_count == 4);
  }

  REQUIRE(view.block_scopes.size() == 1);
  REQUIRE(view.block_scopes[0].block_index == 7);
  REQUIRE(view.block_scopes[0].deflate_range ==
          DeflateBitRange{DeflateBitOffset{0}, DeflateBitOffset{64}});
  REQUIRE(view.block_scopes[0].physical_spans ==
          trace.blocks[0].physical_spans);
  REQUIRE(view.occurrences.size() == 4);
  REQUIRE(view.occurrences[1].token_index == 4);
  REQUIRE(view.occurrences[1].input_range ==
          DeflateBitRange{DeflateBitOffset{12}, DeflateBitOffset{20}});

  // The selected token marks its consumed literal/length symbol only.
  const auto selected_view =
      pnga::analysis_engine::build_huffman_inspector(trace, 4);
  REQUIRE(selected_view.selected_token_index == 4);
  REQUIRE(selected_view.selected_input_bit_begin == 12);
  REQUIRE(selected_view.selected_input_bit_end == 20);
  const auto* selected_entry =
      find_entry(selected_view.tables[1], 268);
  REQUIRE(selected_entry != nullptr);
  REQUIRE(selected_entry->selected);
  const auto* unselected = find_entry(selected_view.tables[1], 65);
  REQUIRE(unselected != nullptr);
  REQUIRE_FALSE(unselected->selected);
  REQUIRE_FALSE(find_entry(selected_view.tables[0], 16)->selected);
}

TEST_CASE("partial and error traces retain verified tables and stop reasons") {
  TraceQueryResult trace = dynamic_trace();
  trace.status = TraceQueryStatus::kPartial;
  trace.truncated = true;
  trace.error = "trace token budget exceeded";
  const auto partial =
      pnga::analysis_engine::build_huffman_inspector(trace, 4);
  REQUIRE(partial.status == HuffmanInspectorStatus::kPartial);
  REQUIRE(partial.error == "trace token budget exceeded");
  REQUIRE(partial.tables.size() == 3);
  for (const auto& table : partial.tables) {
    REQUIRE(table.truncated);
  }
  const auto* sixty_five = find_entry(partial.tables[1], 65);
  REQUIRE(sixty_five != nullptr);
  REQUIRE(sixty_five->occurrence_token_indices ==
          std::vector<std::uint64_t>{3, 5});
  REQUIRE(find_entry(partial.tables[1], 268)->selected);

  TraceQueryResult failed = dynamic_trace();
  failed.status = TraceQueryStatus::kError;
  failed.truncated = false;
  failed.error = "reserved deflate block type";
  const auto error = pnga::analysis_engine::build_huffman_inspector(failed);
  REQUIRE(error.status == HuffmanInspectorStatus::kError);
  REQUIRE(error.error == "reserved deflate block type");
  REQUIRE(error.tables.size() == 3);
  REQUIRE(error.tables[1].entries.size() == 4);
  REQUIRE_FALSE(error.tables[0].truncated);
}

TEST_CASE("huffman inspector serialization pins the projection fields") {
  TraceQueryResult trace;
  trace.status = TraceQueryStatus::kReady;
  trace.generation = 1;
  trace.deflate_data_begin = 2;
  TraceBlockSummary block;
  block.index = 7;
  block.type = BlockType::kDynamic;
  block.input_bit_begin = 16;
  block.input_bit_end = 80;
  trace.blocks.push_back(block);
  pnga::analysis_engine::TraceHuffmanTableSummary code_length;
  code_length.kind = HuffmanTableKind::kCodeLength;
  code_length.entries.push_back({16, 2, 0, 0, 6});
  trace.huffman_tables.push_back(code_length);
  trace.tokens.push_back(make_token(0, TokenKind::kLiteral, 7, 65, 4, 12));

  const auto view = pnga::analysis_engine::build_huffman_inspector(trace);
  REQUIRE(pnga::analysis_engine::serialize_huffman_inspector(view) ==
          "status=ready;generation=1;selected_token=-;selected_bits=-;"
          "error=;tables=1|7,dynamic,code_length,Code Length,0,1,1,0,"
          "entries=1,16:2:0:0:0:6:0:00:00:-:repeat previous length 3-6");
}
