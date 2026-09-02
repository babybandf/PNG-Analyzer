#ifndef PNGA_ANALYSIS_ENGINE_DECODE_TRACE_INSPECTOR_H
#define PNGA_ANALYSIS_ENGINE_DECODE_TRACE_INSPECTOR_H

// WP-505C / WP-5U12E: a bounded, Qt-free Decode Trace projection. It explains
// token arithmetic from immutable TraceQueryResult fields as typed semantic
// steps (Literal / Match / EndOfBlock) with exact compressed input, inflated
// output, Match provenance and per-token physical file spans; the GUI never
// decodes and never constructs event text from debug strings.

#include "pnga/analysis-engine/trace_query.h"

#include <pnga/trace-model/offset_range.h>

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

// WP-5U12E: the bounded scope of one published trace result. The title facts
// (requested output range, returned token count, status, truncation and stop
// reason) identify the bounded window and never imply a whole-stream trace.
struct DecodeTraceScope {
  std::uint64_t generation = 0;
  pnga::trace_model::InflatedByteRange requested_output{};
  std::uint64_t returned_token_count = 0;
  TraceQueryStatus status = TraceQueryStatus::kNotIndexed;
  bool truncated = false;
  std::string stop_reason;

  bool operator==(const DecodeTraceScope&) const = default;
};

// One semantic decode event. Literal, Match and EndOfBlock stay distinct
// structured paths; every fact is a typed projection field, never parsed
// back from event_text.
struct DecodeTraceStep {
  std::uint64_t token_index = 0;
  std::int64_t block_index = -1;
  DecodeTracePath path = DecodeTracePath::kLiteral;
  pnga::trace_model::DeflateBitRange input_range{};
  // Every ordered physical file range containing this token's compressed
  // input bytes, mapped once during composition and copied here without
  // re-reading source bytes.
  std::vector<pnga::trace_model::FileByteRange> physical_input_spans;
  pnga::trace_model::InflatedByteRange output_range{};
  std::string event_text;
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
  pnga::trace_model::InflatedByteRange match_target{};
  bool match_overlaps = false;
  bool contains_current = false;
  bool selected = false;
  // Zero-based position of the Current output byte inside this event; set
  // only when the Current output intersects the step.
  std::optional<std::uint64_t> selected_byte_offset_in_event;

  bool operator==(const DecodeTraceStep&) const = default;
};

struct DecodeTraceInspectorView {
  DecodeTraceScope scope;
  std::vector<DecodeTraceStep> steps;

  // WP-505C legacy projection fields retained for consumers outside this
  // work package (bundle, orchestrator and binding gates). The builder keeps
  // them in sync with the scope and step facts; new code reads the scope and
  // the typed step fields instead.
  DecodeTraceInspectorStatus status = DecodeTraceInspectorStatus::kNoTrace;
  std::uint64_t generation = 0;
  std::optional<std::uint64_t> selected_token_index;
  std::optional<std::uint64_t> selected_output_offset;

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
