#ifndef PNGA_ANALYSIS_ENGINE_HUFFMAN_INSPECTOR_H
#define PNGA_ANALYSIS_ENGINE_HUFFMAN_INSPECTOR_H

// WP-505B / WP-5U12D: a Qt-free projection of bounded Huffman trace artifacts.
// It makes the Stored/Fixed/Dynamic presentation explicit with display-ready
// meanings and fixed-width canonical/read-order bit strings, counts bounded
// occurrences from the captured per-token symbols, and retains verified
// tables for Partial/Error results — without rebuilding tables in the UI,
// reversing bits in Qt, or retaining a second decoder-owned trace.

#include "pnga/analysis-engine/trace_query.h"

#include <pnga/trace-model/offset_range.h>
#include <pnga/trace-model/provenance.h>

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
  std::string meaning;
  std::uint8_t bit_length = 0;
  std::uint16_t canonical_code = 0;
  std::uint16_t read_order_code = 0;
  // Fixed-width binary strings with exactly bit_length characters. Canonical
  // prints the most-significant canonical bit first; read order prints the
  // transmitted least-significant-first sequence.
  std::string canonical_bits;
  std::string read_order_bits;
  // The Deflate-payload bits that supplied this entry's code length. Fixed
  // entries are predefined and carry an empty range.
  pnga::trace_model::DeflateBitRange provenance_range{};
  // Bounded token indices in the current result that consumed this symbol.
  std::vector<std::uint64_t> occurrence_token_indices;
  bool selected = false;

  bool operator==(const HuffmanInspectorEntry&) const = default;
};

struct HuffmanInspectorTable {
  std::uint64_t block_index = 0;
  HuffmanTableMode mode = HuffmanTableMode::kDynamic;
  std::optional<pnga::deflate_trace::HuffmanTableKind> kind;
  // Locked selector label (flow-ui section 20.7); stored blocks use the
  // LEN/NLEN field state instead of a selectable table.
  std::string selector_label;
  std::uint32_t build_order = 0;
  // Stored has LEN/NLEN (2 fields); Fixed has the RFC predefined table
  // cardinality; Dynamic is the exact traced entry count.
  std::uint64_t declared_entry_count = 0;
  // Tokens of the current bounded result that belong to this block.
  std::uint64_t bounded_token_count = 0;
  bool truncated = false;
  std::vector<HuffmanInspectorEntry> entries;

  bool operator==(const HuffmanInspectorTable&) const = default;
};

// One bounded token that consumed a projected symbol. input_range is the
// token's own DEFLATE bit range; an end-of-block boundary is empty.
struct HuffmanOccurrenceFact {
  std::uint64_t token_index = 0;
  pnga::trace_model::DeflateBitRange input_range{};

  bool operator==(const HuffmanOccurrenceFact&) const = default;
};

// The owning block's DEFLATE bit window and its already-mapped physical file
// spans, copied verbatim from the bounded result. Occurrence navigation uses
// these typed facts instead of re-mapping anything in the GUI.
struct HuffmanBlockScope {
  std::uint64_t block_index = 0;
  pnga::trace_model::DeflateBitRange deflate_range{};
  std::vector<pnga::trace_model::ProvenanceSpan> physical_spans;

  bool operator==(const HuffmanBlockScope&) const = default;
};

struct HuffmanInspectorView {
  HuffmanInspectorStatus status = HuffmanInspectorStatus::kNoTrace;
  std::string error;
  std::uint64_t generation = 0;
  std::optional<std::uint64_t> selected_token_index;
  std::optional<std::uint64_t> selected_input_bit_begin;
  std::optional<std::uint64_t> selected_input_bit_end;
  std::vector<HuffmanInspectorTable> tables;
  std::vector<HuffmanOccurrenceFact> occurrences;
  std::vector<HuffmanBlockScope> block_scopes;

  bool operator==(const HuffmanInspectorView&) const = default;
};

HuffmanInspectorView build_huffman_inspector(
    const TraceQueryResult& trace,
    std::optional<std::uint64_t> selected_token_index = std::nullopt);

std::string serialize_huffman_inspector(const HuffmanInspectorView& view);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_HUFFMAN_INSPECTOR_H
