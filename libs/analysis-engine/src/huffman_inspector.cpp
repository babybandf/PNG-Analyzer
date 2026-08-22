// WP-505B Huffman Inspector projection.

#include "pnga/analysis-engine/huffman_inspector.h"

#include <pnga/deflate-index/block_index.h>

#include <sstream>

namespace pnga::analysis_engine {

namespace {

const char* status_text(HuffmanInspectorStatus status) noexcept {
  switch (status) {
    case HuffmanInspectorStatus::kNoTrace:
      return "no_trace";
    case HuffmanInspectorStatus::kReady:
      return "ready";
    case HuffmanInspectorStatus::kPartial:
      return "partial";
    case HuffmanInspectorStatus::kError:
      return "error";
  }
  return "unknown";
}

const char* mode_text(HuffmanTableMode mode) noexcept {
  switch (mode) {
    case HuffmanTableMode::kStored:
      return "stored";
    case HuffmanTableMode::kFixed:
      return "fixed";
    case HuffmanTableMode::kDynamic:
      return "dynamic";
  }
  return "unknown";
}

const char* kind_text(
    std::optional<pnga::deflate_trace::HuffmanTableKind> kind) noexcept {
  if (!kind.has_value()) {
    return "none";
  }
  switch (*kind) {
    case pnga::deflate_trace::HuffmanTableKind::kCodeLength:
      return "code_length";
    case pnga::deflate_trace::HuffmanTableKind::kLiteralLength:
      return "literal_length";
    case pnga::deflate_trace::HuffmanTableKind::kDistance:
      return "distance";
  }
  return "unknown";
}

bool overlaps(std::uint64_t begin, std::uint64_t end,
              std::uint64_t wanted_begin,
              std::uint64_t wanted_end) noexcept {
  return begin < wanted_end && wanted_begin < end;
}

}  // namespace

const char* huffman_inspector_status_text(
    HuffmanInspectorStatus status) noexcept {
  return status_text(status);
}

const char* huffman_table_mode_text(HuffmanTableMode mode) noexcept {
  return mode_text(mode);
}

HuffmanInspectorView build_huffman_inspector(
    const TraceQueryResult& trace,
    std::optional<std::uint64_t> selected_token_index) {
  HuffmanInspectorView view;
  view.generation = trace.generation;
  view.selected_token_index = selected_token_index;
  switch (trace.status) {
    case TraceQueryStatus::kReady:
      view.status = HuffmanInspectorStatus::kReady;
      break;
    case TraceQueryStatus::kPartial:
    case TraceQueryStatus::kCancelled:
      view.status = HuffmanInspectorStatus::kPartial;
      break;
    case TraceQueryStatus::kError:
      view.status = HuffmanInspectorStatus::kError;
      view.error = trace.error;
      break;
    case TraceQueryStatus::kNotIndexed:
    case TraceQueryStatus::kReplaying:
      view.status = HuffmanInspectorStatus::kNoTrace;
      view.error = trace.error;
      break;
  }

  const TraceTokenSummary* selected = nullptr;
  if (selected_token_index.has_value()) {
    for (const auto& token : trace.tokens) {
      if (token.index == *selected_token_index) {
        selected = &token;
        view.selected_input_bit_begin = token.input_bit_begin;
        view.selected_input_bit_end = token.input_bit_end;
        break;
      }
    }
  }

  std::uint32_t build_order = 0;
  std::size_t dynamic_cursor = 0;
  for (const auto& block : trace.blocks) {
    if (block.type == pnga::deflate_index::BlockType::kStored) {
      HuffmanInspectorTable table;
      table.block_index = block.index;
      table.mode = HuffmanTableMode::kStored;
      table.build_order = build_order++;
      table.declared_entry_count = 2;  // LEN, NLEN
      view.tables.push_back(std::move(table));
      continue;
    }
    if (block.type == pnga::deflate_index::BlockType::kFixed) {
      HuffmanInspectorTable literal;
      literal.block_index = block.index;
      literal.mode = HuffmanTableMode::kFixed;
      literal.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
      literal.build_order = build_order++;
      literal.declared_entry_count = 288;
      view.tables.push_back(std::move(literal));
      HuffmanInspectorTable distance;
      distance.block_index = block.index;
      distance.mode = HuffmanTableMode::kFixed;
      distance.kind = pnga::deflate_trace::HuffmanTableKind::kDistance;
      distance.build_order = build_order++;
      distance.declared_entry_count = 32;
      view.tables.push_back(std::move(distance));
      continue;
    }

    // The decoder emits dynamic tables in RFC build order. A query normally
    // covers one dynamic block; if more are present, provenance bits identify
    // the owning block and a deterministic fallback keeps all tables visible.
    for (std::size_t i = dynamic_cursor; i < trace.huffman_tables.size(); ++i) {
      const auto& source = trace.huffman_tables[i];
      bool belongs = false;
      for (const auto& entry : source.entries) {
        if (overlaps(entry.provenance_bit_begin, entry.provenance_bit_end,
                     block.input_bit_begin, block.input_bit_end)) {
          belongs = true;
          break;
        }
      }
      if (i != dynamic_cursor && !belongs) {
        break;
      }
      HuffmanInspectorTable table;
      table.block_index = block.index;
      table.mode = HuffmanTableMode::kDynamic;
      table.kind = source.kind;
      table.build_order = build_order++;
      table.declared_entry_count = source.entries.size();
      table.entries.reserve(source.entries.size());
      for (const auto& entry : source.entries) {
        HuffmanInspectorEntry projected;
        projected.symbol = entry.symbol;
        projected.bit_length = entry.bit_length;
        projected.canonical_code = entry.canonical_code;
        projected.provenance_bit_begin = entry.provenance_bit_begin;
        projected.provenance_bit_end = entry.provenance_bit_end;
        if (selected != nullptr && source.kind ==
                                        pnga::deflate_trace::HuffmanTableKind::
                                            kLiteralLength &&
            selected->kind == pnga::deflate_trace::TokenKind::kLiteral &&
            entry.symbol == selected->literal) {
          projected.selected = true;
        }
        table.entries.push_back(projected);
      }
      view.tables.push_back(std::move(table));
      ++dynamic_cursor;
      if (dynamic_cursor == trace.huffman_tables.size()) {
        break;
      }
    }
  }

  // A trace may have no block range when a bounded query starts after the
  // available index. Preserve dynamic tables rather than silently dropping
  // verified artifacts.
  while (dynamic_cursor < trace.huffman_tables.size()) {
    const auto& source = trace.huffman_tables[dynamic_cursor];
    HuffmanInspectorTable table;
    table.mode = HuffmanTableMode::kDynamic;
    table.kind = source.kind;
    table.build_order = build_order++;
    table.declared_entry_count = source.entries.size();
    table.entries.reserve(source.entries.size());
    for (const auto& entry : source.entries) {
      table.entries.push_back(HuffmanInspectorEntry{
          entry.symbol, entry.bit_length, entry.canonical_code,
          entry.provenance_bit_begin, entry.provenance_bit_end, false});
    }
    view.tables.push_back(std::move(table));
    ++dynamic_cursor;
  }
  return view;
}

std::string serialize_huffman_inspector(const HuffmanInspectorView& view) {
  std::ostringstream out;
  out << "status=" << status_text(view.status) << ";generation="
      << view.generation << ";selected_token=";
  if (view.selected_token_index.has_value()) {
    out << *view.selected_token_index;
  } else {
    out << '-';
  }
  out << ";selected_bits=";
  if (view.selected_input_bit_begin.has_value() &&
      view.selected_input_bit_end.has_value()) {
    out << *view.selected_input_bit_begin << ':'
        << *view.selected_input_bit_end;
  } else {
    out << '-';
  }
  out << ";error=" << view.error << ";tables=" << view.tables.size();
  for (const auto& table : view.tables) {
    out << "|" << table.block_index << ',' << mode_text(table.mode) << ','
        << kind_text(table.kind) << ',' << table.build_order << ','
        << table.declared_entry_count << ",entries=" << table.entries.size();
    for (const auto& entry : table.entries) {
      out << ',' << entry.symbol << ':' << static_cast<unsigned>(entry.bit_length)
          << ':' << entry.canonical_code << ':' << entry.provenance_bit_begin
          << ':' << entry.provenance_bit_end << ':' << (entry.selected ? 1 : 0);
    }
  }
  return out.str();
}

}  // namespace pnga::analysis_engine
