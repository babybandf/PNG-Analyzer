#ifndef PNGA_ANALYSIS_ENGINE_DECODE_TRACE_INSPECTOR_H
#define PNGA_ANALYSIS_ENGINE_DECODE_TRACE_INSPECTOR_H

// WP-505C: a bounded, Qt-free Decode Trace projection. It explains token
// arithmetic from immutable TraceQueryResult fields; the GUI never decodes.

#include "pnga/analysis-engine/trace_query.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

enum class DecodeTraceInspectorStatus {
  kNoTrace = 0,
  kReady = 1,
  kPartial = 2,
  kError = 3,
};

const char* decode_trace_inspector_status_text(
    DecodeTraceInspectorStatus status) noexcept;

enum class DecodeTracePath { kLiteral = 0, kMatch = 1, kEndOfBlock = 2 };

const char* decode_trace_path_text(DecodeTracePath path) noexcept;

struct DecodeTraceStep {
  std::uint64_t token_index = 0;
  std::int64_t block_index = -1;
  DecodeTracePath path = DecodeTracePath::kLiteral;
  std::uint64_t input_bit_begin = 0;
  std::uint64_t input_bit_end = 0;
  std::uint64_t output_begin = 0;
  std::uint64_t output_end = 0;
  std::optional<std::uint16_t> huffman_symbol;
  std::uint8_t literal = 0;
  std::uint16_t length = 0;
  std::uint16_t distance = 0;
  std::uint16_t length_base = 0;
  std::uint8_t length_extra_bits = 0;
  std::uint16_t length_extra_value = 0;
  std::uint16_t distance_base = 0;
  std::uint8_t distance_extra_bits = 0;
  std::uint16_t distance_extra_value = 0;
  std::vector<pnga::deflate_trace::TokenOutputRange> match_source_ranges;
  bool selected = false;
  std::optional<std::uint64_t> selected_output_byte;

  bool operator==(const DecodeTraceStep&) const = default;
};

struct DecodeTraceInspectorView {
  DecodeTraceInspectorStatus status = DecodeTraceInspectorStatus::kNoTrace;
  std::string error;
  std::uint64_t generation = 0;
  std::optional<std::uint64_t> selected_token_index;
  std::optional<std::uint64_t> selected_output_offset;
  std::vector<DecodeTraceStep> steps;

  bool operator==(const DecodeTraceInspectorView&) const = default;
};

DecodeTraceInspectorView build_decode_trace_inspector(
    const TraceQueryResult& trace,
    std::optional<std::uint64_t> selected_token_index = std::nullopt,
    std::optional<std::uint64_t> selected_output_offset = std::nullopt);

std::string serialize_decode_trace_inspector(
    const DecodeTraceInspectorView& view);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_DECODE_TRACE_INSPECTOR_H
