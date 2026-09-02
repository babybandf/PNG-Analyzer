// WP-505B / WP-5U12D Huffman Inspector projection.

#include "pnga/analysis-engine/huffman_inspector.h"

#include <pnga/deflate-index/block_index.h>

#include <algorithm>
#include <array>
#include <limits>
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

std::string selector_label(
    std::optional<pnga::deflate_trace::HuffmanTableKind> kind) noexcept {
  if (!kind.has_value()) {
    return "LEN/NLEN";
  }
  switch (*kind) {
    case pnga::deflate_trace::HuffmanTableKind::kCodeLength:
      return "Code Length";
    case pnga::deflate_trace::HuffmanTableKind::kLiteralLength:
      return "Literal / Length";
    case pnga::deflate_trace::HuffmanTableKind::kDistance:
      return "Distance";
  }
  return "Unknown";
}

bool overlaps(std::uint64_t begin, std::uint64_t end,
              std::uint64_t wanted_begin,
              std::uint64_t wanted_end) noexcept {
  return begin < wanted_end && wanted_begin < end;
}

struct DeflateBitWindow {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
};

std::optional<DeflateBitWindow> block_deflate_window(
    const TraceQueryResult& trace, const TraceBlockSummary& block) {
  if (trace.deflate_data_begin >
      std::numeric_limits<std::uint64_t>::max() / 8) {
    return std::nullopt;
  }
  const std::uint64_t wrapper_bits = trace.deflate_data_begin * 8;
  if (block.input_bit_begin < wrapper_bits ||
      block.input_bit_end < wrapper_bits) {
    return std::nullopt;
  }
  return DeflateBitWindow{block.input_bit_begin - wrapper_bits,
                          block.input_bit_end - wrapper_bits};
}

std::optional<pnga::trace_model::DeflateBitRange> make_deflate_range(
    std::uint64_t begin, std::uint64_t end) noexcept {
  if (end < begin || end - begin >
                         std::numeric_limits<std::uint64_t>::max() -
                             begin) {
    return std::nullopt;
  }
  return pnga::trace_model::DeflateBitRange{pnga::trace_model::DeflateBitOffset{begin},
                                            pnga::trace_model::DeflateBitOffset{end}};
}

std::uint16_t read_order_code(std::uint16_t canonical,
                              std::uint8_t bit_length) noexcept {
  std::uint16_t result = 0;
  for (std::uint8_t i = 0; i < bit_length; ++i) {
    result = static_cast<std::uint16_t>(
        (result << 1) | ((canonical >> i) & static_cast<std::uint16_t>(1)));
  }
  return result;
}

// Fixed-width MSB-first binary string with exactly bit_length characters.
std::string canonical_bits(std::uint16_t canonical_code,
                           std::uint8_t bit_length) {
  std::string bits(static_cast<std::size_t>(bit_length), '0');
  for (std::uint8_t i = 0; i < bit_length; ++i) {
    const std::uint8_t shift = static_cast<std::uint8_t>(bit_length - 1 - i);
    bits[i] = ((canonical_code >> shift) & 1u) != 0 ? '1' : '0';
  }
  return bits;
}

std::string read_order_bits(std::uint16_t canonical_code,
                            std::uint8_t bit_length) {
  std::string bits(static_cast<std::size_t>(bit_length), '0');
  for (std::uint8_t i = 0; i < bit_length; ++i) {
    bits[i] = ((canonical_code >> i) & 1u) != 0 ? '1' : '0';
  }
  return bits;
}

struct BaseExtra {
  std::uint16_t base;
  std::uint8_t extra;
};

constexpr std::array<BaseExtra, 29> kLengths = {{
    {3, 0},  {4, 0},  {5, 0},  {6, 0},  {7, 0},  {8, 0},  {9, 0},  {10, 0},
    {11, 1}, {13, 1}, {15, 1}, {17, 1}, {19, 2}, {23, 2}, {27, 2}, {31, 2},
    {35, 3}, {43, 3}, {51, 3}, {59, 3}, {67, 4}, {83, 4}, {99, 4}, {115, 4},
    {131, 5}, {163, 5}, {195, 5}, {227, 5}, {258, 0},
}};

constexpr std::array<BaseExtra, 30> kDistances = {{
    {1, 0},    {2, 0},    {3, 0},    {4, 0},    {5, 1},    {7, 1},
    {9, 2},    {13, 2},   {17, 3},   {25, 3},   {33, 4},   {49, 4},
    {65, 5},   {97, 5},   {129, 6},  {193, 6},  {257, 7},  {385, 7},
    {513, 8},  {769, 8},  {1025, 9}, {1537, 9}, {2049, 10}, {3073, 10},
    {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13}, {24577, 13},
}};

// Stable English meaning for one symbol of a codebook (flow-ui section 8.3).
std::string entry_meaning(
    pnga::deflate_trace::HuffmanTableKind kind, std::uint16_t symbol) {
  switch (kind) {
    case pnga::deflate_trace::HuffmanTableKind::kCodeLength:
      if (symbol <= 15) {
        return "code length " + std::to_string(symbol);
      }
      if (symbol == 16) {
        return "repeat previous length 3-6";
      }
      if (symbol == 17) {
        return "repeat zero length 3-10";
      }
      if (symbol == 18) {
        return "repeat zero length 11-138";
      }
      return "reserved";
    case pnga::deflate_trace::HuffmanTableKind::kLiteralLength:
      if (symbol <= 255) {
        return "literal " + std::to_string(symbol);
      }
      if (symbol == 256) {
        return "end-of-block";
      }
      if (symbol >= 257 && symbol <= 285) {
        const auto& range = kLengths[symbol - 257];
        const std::uint32_t maximum =
            static_cast<std::uint32_t>(range.base) +
            ((std::uint32_t{1} << range.extra) - 1u);
        if (maximum == range.base) {
          return "length " + std::to_string(range.base);
        }
        return "length " + std::to_string(range.base) + "-" +
               std::to_string(maximum);
      }
      return "reserved";
    case pnga::deflate_trace::HuffmanTableKind::kDistance:
      if (symbol < kDistances.size()) {
        const auto& range = kDistances[symbol];
        const std::uint32_t maximum =
            static_cast<std::uint32_t>(range.base) +
            ((std::uint32_t{1} << range.extra) - 1u);
        if (maximum == range.base) {
          return "distance " + std::to_string(range.base);
        }
        return "distance " + std::to_string(range.base) + "-" +
               std::to_string(maximum);
      }
      return "reserved";
  }
  return "reserved";
}

HuffmanInspectorEntry project_entry(
    pnga::deflate_trace::HuffmanTableKind kind,
    const pnga::deflate_trace::HuffmanTableEntry& source) {
  HuffmanInspectorEntry entry;
  entry.symbol = source.symbol;
  entry.meaning = entry_meaning(kind, source.symbol);
  entry.bit_length = source.bit_length;
  entry.canonical_code = source.canonical_code;
  entry.read_order_code = read_order_code(source.canonical_code,
                                          source.bit_length);
  entry.canonical_bits = canonical_bits(source.canonical_code,
                                        source.bit_length);
  entry.read_order_bits = read_order_bits(source.canonical_code,
                                          source.bit_length);
  if (source.provenance_bit_end >= source.provenance_bit_begin) {
    if (auto range = make_deflate_range(source.provenance_bit_begin,
                                        source.provenance_bit_end);
        range.has_value()) {
      entry.provenance_range = *range;
    }
  }
  return entry;
}

// RFC 1951 section 3.2.6 predefined code lengths. The decoder does not emit
// table traces for fixed blocks, so the projection states the predefined
// tables itself instead of leaving the Fixed state without entries.
std::vector<HuffmanInspectorEntry> fixed_entries(
    pnga::deflate_trace::HuffmanTableKind kind) {
  struct FixedEntry {
    pnga::deflate_trace::HuffmanTableEntry source;
    std::string meaning;
    std::string canonical_bits;
    std::string read_bits;
    std::uint16_t read_code;
  };
  static const auto literal_entries = [] {
    std::vector<std::uint8_t> lengths(288, 0);
    for (std::size_t symbol = 0; symbol <= 143; ++symbol) {
      lengths[symbol] = 8;
    }
    for (std::size_t symbol = 144; symbol <= 255; ++symbol) {
      lengths[symbol] = 9;
    }
    for (std::size_t symbol = 256; symbol <= 279; ++symbol) {
      lengths[symbol] = 7;
    }
    for (std::size_t symbol = 280; symbol <= 287; ++symbol) {
      lengths[symbol] = 8;
    }
    std::array<std::uint32_t, 16> bl_count{};
    std::array<std::uint32_t, 16> next_code{};
    for (const std::uint8_t length : lengths) {
      if (length != 0) {
        ++bl_count[length];
      }
    }
    std::uint32_t code = 0;
    for (std::uint32_t bits = 1; bits <= 15; ++bits) {
      code = (code + bl_count[bits - 1]) << 1;
      next_code[bits] = code;
    }
    std::vector<FixedEntry> entries(lengths.size());
    for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
      auto& entry = entries[symbol];
      entry.source.symbol = static_cast<std::uint16_t>(symbol);
      entry.source.bit_length = lengths[symbol];
      if (lengths[symbol] != 0) {
        entry.source.canonical_code =
            static_cast<std::uint16_t>(next_code[lengths[symbol]]++);
      }
      entry.meaning = entry_meaning(
          pnga::deflate_trace::HuffmanTableKind::kLiteralLength,
          entry.source.symbol);
      entry.canonical_bits = canonical_bits(entry.source.canonical_code,
                                            entry.source.bit_length);
      entry.read_bits = read_order_bits(entry.source.canonical_code,
                                        entry.source.bit_length);
      entry.read_code = read_order_code(entry.source.canonical_code,
                                        entry.source.bit_length);
    }
    return entries;
  }();
  static const auto distance_entries = [] {
    std::vector<FixedEntry> entries(32);
    for (std::size_t symbol = 0; symbol < 32; ++symbol) {
      auto& entry = entries[symbol];
      entry.source.symbol = static_cast<std::uint16_t>(symbol);
      entry.source.bit_length = 5;
      entry.source.canonical_code = static_cast<std::uint16_t>(symbol);
      entry.meaning = entry_meaning(
          pnga::deflate_trace::HuffmanTableKind::kDistance,
          entry.source.symbol);
      entry.canonical_bits = canonical_bits(entry.source.canonical_code, 5);
      entry.read_bits = read_order_bits(entry.source.canonical_code, 5);
      entry.read_code = read_order_code(entry.source.canonical_code, 5);
    }
    return entries;
  }();

  const auto& base = kind == pnga::deflate_trace::HuffmanTableKind::kDistance
                         ? distance_entries
                         : literal_entries;
  std::vector<HuffmanInspectorEntry> entries;
  entries.reserve(base.size());
  for (const auto& source : base) {
    HuffmanInspectorEntry entry;
    entry.symbol = source.source.symbol;
    entry.meaning = source.meaning;
    entry.bit_length = source.source.bit_length;
    entry.canonical_code = source.source.canonical_code;
    entry.read_order_code = source.read_code;
    entry.canonical_bits = source.canonical_bits;
    entry.read_order_bits = source.read_bits;
    entries.push_back(std::move(entry));
  }
  return entries;
}

// Counts bounded occurrences for one table: only tokens of the current
// result whose owning block is this table's block. Bounded token counts
// cover every token of the block; occurrence ids require a captured symbol
// that matches a projected entry. The whole stream is never scanned.
void count_occurrences(const TraceQueryResult& trace,
                       HuffmanInspectorTable* table) {
  for (const auto& token : trace.tokens) {
    if (token.block_index < 0 ||
        static_cast<std::uint64_t>(token.block_index) != table->block_index) {
      continue;
    }
    ++table->bounded_token_count;
    if (!token.huffman_symbol.has_value()) {
      continue;
    }
    const std::uint16_t symbol = *token.huffman_symbol;
    const auto entry = std::find_if(
        table->entries.begin(), table->entries.end(),
        [symbol](const HuffmanInspectorEntry& candidate) {
          return candidate.symbol == symbol;
        });
    if (entry != table->entries.end()) {
      entry->occurrence_token_indices.push_back(token.index);
    }
  }
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
      view.error = trace.error;
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

  // Typed block scopes: the DEFLATE window and the already-mapped physical
  // spans of every block in the bounded result.
  for (const auto& block : trace.blocks) {
    HuffmanBlockScope scope;
    scope.block_index = block.index;
    if (const auto window = block_deflate_window(trace, block);
        window.has_value()) {
      if (auto range = make_deflate_range(window->begin, window->end);
          range.has_value()) {
        scope.deflate_range = *range;
      }
    }
    scope.physical_spans = block.physical_spans;
    view.block_scopes.push_back(std::move(scope));
  }

  std::uint32_t build_order = 0;
  std::size_t dynamic_cursor = 0;
  for (const auto& block : trace.blocks) {
    if (block.type == pnga::deflate_index::BlockType::kStored) {
      HuffmanInspectorTable table;
      table.block_index = block.index;
      table.mode = HuffmanTableMode::kStored;
      table.selector_label = selector_label(std::nullopt);
      table.build_order = build_order++;
      table.declared_entry_count = 2;  // LEN, NLEN
      table.truncated = trace.truncated;
      count_occurrences(trace, &table);
      view.tables.push_back(std::move(table));
      continue;
    }
    if (block.type == pnga::deflate_index::BlockType::kFixed) {
      HuffmanInspectorTable literal;
      literal.block_index = block.index;
      literal.mode = HuffmanTableMode::kFixed;
      literal.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
      literal.selector_label = selector_label(literal.kind);
      literal.build_order = build_order++;
      literal.declared_entry_count = 288;
      literal.truncated = trace.truncated;
      literal.entries = fixed_entries(
          pnga::deflate_trace::HuffmanTableKind::kLiteralLength);
      count_occurrences(trace, &literal);
      view.tables.push_back(std::move(literal));
      HuffmanInspectorTable distance;
      distance.block_index = block.index;
      distance.mode = HuffmanTableMode::kFixed;
      distance.kind = pnga::deflate_trace::HuffmanTableKind::kDistance;
      distance.selector_label = selector_label(distance.kind);
      distance.build_order = build_order++;
      distance.declared_entry_count = 32;
      distance.truncated = trace.truncated;
      distance.entries =
          fixed_entries(pnga::deflate_trace::HuffmanTableKind::kDistance);
      count_occurrences(trace, &distance);
      view.tables.push_back(std::move(distance));
      continue;
    }

    // The decoder emits dynamic tables in RFC build order. A query normally
    // covers one dynamic block; if more are present, provenance bits identify
    // the owning block and a deterministic fallback keeps all tables visible.
    const auto block_window = block_deflate_window(trace, block);
    for (std::size_t i = dynamic_cursor; i < trace.huffman_tables.size(); ++i) {
      const auto& source = trace.huffman_tables[i];
      bool belongs = false;
      for (const auto& entry : source.entries) {
        if (block_window.has_value() &&
            overlaps(entry.provenance_bit_begin, entry.provenance_bit_end,
                     block_window->begin, block_window->end)) {
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
      table.selector_label = selector_label(source.kind);
      table.build_order = build_order++;
      table.declared_entry_count = source.entries.size();
      table.truncated = trace.truncated;
      table.entries.reserve(source.entries.size());
      for (const auto& entry : source.entries) {
        table.entries.push_back(project_entry(source.kind, entry));
      }
      count_occurrences(trace, &table);
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
    table.selector_label = selector_label(source.kind);
    table.build_order = build_order++;
    table.declared_entry_count = source.entries.size();
    table.truncated = trace.truncated;
    table.entries.reserve(source.entries.size());
    for (const auto& entry : source.entries) {
      table.entries.push_back(project_entry(source.kind, entry));
    }
    count_occurrences(trace, &table);
    view.tables.push_back(std::move(table));
    ++dynamic_cursor;
  }

  // Bounded occurrence facts for the tokens that consumed a projected
  // symbol, in bounded result order.
  for (const auto& token : trace.tokens) {
    if (token.block_index < 0 || !token.huffman_symbol.has_value()) {
      continue;
    }
    HuffmanOccurrenceFact fact;
    fact.token_index = token.index;
    if (auto range = make_deflate_range(token.input_bit_begin,
                                        token.input_bit_end);
        range.has_value()) {
      fact.input_range = *range;
    }
    view.occurrences.push_back(fact);
  }

  // The selected token marks the literal/length entry of its captured symbol
  // inside its own block.
  if (selected != nullptr && selected->block_index >= 0 &&
      selected->huffman_symbol.has_value()) {
    const std::uint64_t selected_block =
        static_cast<std::uint64_t>(selected->block_index);
    const std::uint16_t symbol = *selected->huffman_symbol;
    for (auto& table : view.tables) {
      if (table.block_index != selected_block ||
          table.kind !=
              pnga::deflate_trace::HuffmanTableKind::kLiteralLength) {
        continue;
      }
      const auto entry = std::find_if(
          table.entries.begin(), table.entries.end(),
          [symbol](const HuffmanInspectorEntry& candidate) {
            return candidate.symbol == symbol;
          });
      if (entry != table.entries.end()) {
        entry->selected = true;
      }
    }
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
        << kind_text(table.kind) << ',' << table.selector_label << ','
        << table.build_order << ',' << table.declared_entry_count << ','
        << table.bounded_token_count << ',' << (table.truncated ? 1 : 0)
        << ",entries=" << table.entries.size();
    for (const auto& entry : table.entries) {
      out << ',' << entry.symbol << ':' << static_cast<unsigned>(entry.bit_length)
          << ':' << entry.canonical_code << ':' << entry.read_order_code << ':'
          << entry.provenance_range.begin.value
          << ':' << entry.provenance_range.end.value << ':'
          << (entry.selected ? 1 : 0) << ':' << entry.canonical_bits << ':'
          << entry.read_order_bits << ':';
      if (entry.occurrence_token_indices.empty()) {
        out << '-';
      } else {
        for (std::size_t i = 0; i < entry.occurrence_token_indices.size();
             ++i) {
          if (i != 0) {
            out << ';';
          }
          out << entry.occurrence_token_indices[i];
        }
      }
      out << ':' << entry.meaning;
    }
  }
  return out.str();
}

}  // namespace pnga::analysis_engine
