#ifndef PNGA_ANALYSIS_ENGINE_HUFFMAN_INSPECTOR_H
#define PNGA_ANALYSIS_ENGINE_HUFFMAN_INSPECTOR_H

// WP-505B: a Qt-free projection of bounded Huffman trace artifacts. It makes
// the Stored/Fixed/Dynamic presentation explicit without rebuilding tables in
// the UI or retaining a second decoder-owned trace.

#include "pnga/analysis-engine/trace_query.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

enum class HuffmanInspectorStatus {
  kNoTrace = 0,
  kReady = 1,
  kPartial = 2,
  kError = 3,
};

const char* huffman_inspector_status_text(
    HuffmanInspectorStatus status) noexcept;

enum class HuffmanTableMode { kStored = 0, kFixed = 1, kDynamic = 2 };

const char* huffman_table_mode_text(HuffmanTableMode mode) noexcept;

struct HuffmanInspectorEntry {
  std::uint16_t symbol = 0;
  std::uint8_t bit_length = 0;
  std::uint16_t canonical_code = 0;
  std::uint64_t provenance_bit_begin = 0;
  std::uint64_t provenance_bit_end = 0;
  bool selected = false;
  // DEFLATE transmits the canonical code least-significant bit first. This is
  // the wire/read-order value, kept separate from the canonical code domain.
  std::uint16_t read_order_code = 0;

  bool operator==(const HuffmanInspectorEntry&) const = default;
};

struct HuffmanInspectorTable {
  std::uint64_t block_index = 0;
  HuffmanTableMode mode = HuffmanTableMode::kDynamic;
  std::optional<pnga::deflate_trace::HuffmanTableKind> kind;
  std::uint32_t build_order = 0;
  // Stored has LEN/NLEN (2 fields); Fixed has the RFC predefined table
  // cardinality; Dynamic is the exact traced entry count.
  std::uint64_t declared_entry_count = 0;
  std::vector<HuffmanInspectorEntry> entries;

  bool operator==(const HuffmanInspectorTable&) const = default;
};

struct HuffmanInspectorView {
  HuffmanInspectorStatus status = HuffmanInspectorStatus::kNoTrace;
  std::string error;
  std::uint64_t generation = 0;
  std::optional<std::uint64_t> selected_token_index;
  std::optional<std::uint64_t> selected_input_bit_begin;
  std::optional<std::uint64_t> selected_input_bit_end;
  std::vector<HuffmanInspectorTable> tables;

  bool operator==(const HuffmanInspectorView&) const = default;
};

HuffmanInspectorView build_huffman_inspector(
    const TraceQueryResult& trace,
    std::optional<std::uint64_t> selected_token_index = std::nullopt);

std::string serialize_huffman_inspector(const HuffmanInspectorView& view);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_HUFFMAN_INSPECTOR_H
