// WP-505C / WP-5U12E Decode Trace projection. Converts bounded token facts
// into complete semantic steps with stable event text, Match arithmetic and
// checked overlap provenance. Verified steps are preserved after errors and
// truncation; nothing here decodes again or replays.

#include "pnga/analysis-engine/decode_trace_inspector.h"

#include <limits>
#include <sstream>

namespace pnga::analysis_engine {

namespace {

struct BaseExtra {
  std::uint16_t base;
  std::uint8_t extra;
};

constexpr BaseExtra kLengths[] = {
    {3, 0},    {4, 0},    {5, 0},    {6, 0},    {7, 0},    {8, 0},
    {9, 0},    {10, 0},   {11, 1},   {13, 1},   {15, 1},   {17, 1},
    {19, 2},   {23, 2},   {27, 2},   {31, 2},   {35, 3},   {43, 3},
    {51, 3},   {59, 3},   {67, 4},   {83, 4},   {99, 4},   {115, 4},
    {131, 5},  {163, 5},  {195, 5},  {227, 5},  {258, 0},
};

constexpr BaseExtra kDistances[] = {
    {1, 0},    {2, 0},    {3, 0},    {4, 0},    {5, 1},    {7, 1},
    {9, 2},    {13, 2},   {17, 3},   {25, 3},   {33, 4},   {49, 4},
    {65, 5},   {97, 5},   {129, 6},  {193, 6},  {257, 7},  {385, 7},
    {513, 8},  {769, 8},  {1025, 9}, {1537, 9}, {2049, 10}, {3073, 10},
    {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13},
    {24577, 13},
};

const char* status_text(DecodeTraceInspectorStatus status) noexcept {
  switch (status) {
    case DecodeTraceInspectorStatus::kNoTrace:
      return "no_trace";
    case DecodeTraceInspectorStatus::kReady:
      return "ready";
    case DecodeTraceInspectorStatus::kPartial:
      return "partial";
    case DecodeTraceInspectorStatus::kError:
      return "error";
  }
  return "unknown";
}

const char* path_text(DecodeTracePath path) noexcept {
  switch (path) {
    case DecodeTracePath::kLiteral:
      return "literal";
    case DecodeTracePath::kMatch:
      return "match";
    case DecodeTracePath::kEndOfBlock:
      return "eob";
  }
  return "unknown";
}

template <std::size_t N>
const BaseExtra* find_base_extra(const BaseExtra (&table)[N],
                                 std::uint16_t value) noexcept {
  for (const auto& entry : table) {
    const std::uint32_t max = static_cast<std::uint32_t>(entry.base) +
                              ((std::uint32_t{1} << entry.extra) - 1u);
    if (value >= entry.base && value <= max) {
      return &entry;
    }
  }
  return nullptr;
}

// Stable, locale-independent event text (flow-ui section 9.2 wireframe).
std::string literal_event_text(std::uint8_t value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out = "Literal 0x";
  out.push_back(kHex[value >> 4]);
  out.push_back(kHex[value & 0x0F]);
  return out;
}

std::string match_event_text(std::uint16_t length, std::uint16_t distance) {
  return "Match len " + std::to_string(length) + " / dist " +
         std::to_string(distance);
}

const char* const kEndOfBlockEventText = "End of block";

}  // namespace

const char* decode_trace_inspector_status_text(
    DecodeTraceInspectorStatus status) noexcept {
  return status_text(status);
}

const char* decode_trace_path_text(DecodeTracePath path) noexcept {
  return path_text(path);
}

DecodeTraceInspectorView build_decode_trace_inspector(
    const TraceQueryResult& trace,
    std::optional<std::uint64_t> selected_token_index,
    std::optional<std::uint64_t> selected_output_offset) {
  DecodeTraceInspectorView view;
  view.scope.generation = trace.generation;
  if (trace.inflated_end >= trace.inflated_begin) {
    const auto requested = pnga::trace_model::make_range(
        pnga::trace_model::InflatedByteOffset{trace.inflated_begin},
        trace.inflated_end - trace.inflated_begin);
    if (requested.has_value()) {
      view.scope.requested_output = *requested;
    }
  }
  view.scope.status = trace.status;
  view.scope.truncated = trace.truncated;
  view.scope.stop_reason = trace.error;

  view.steps.reserve(trace.tokens.size());
  for (const auto& token : trace.tokens) {
    DecodeTraceStep step;
    step.token_index = token.index;
    step.block_index = token.block_index;
    step.input_range = token.input_range();
    // The per-token physical spans were mapped during composition; copy
    // them without reading source bytes.
    step.physical_input_spans = token.physical_input_spans;
    step.output_range = token.output_range();
    step.literal = token.literal;
    step.length = token.length;
    step.distance = token.distance;
    step.match_source_ranges = token.match_source_ranges;
    // The symbol captured by the decoder at the read that resolved the
    // token (WP-5U12D); stored tokens consume no Huffman code and stay
    // unset. No RFC re-derivation happens here.
    step.huffman_symbol = token.huffman_symbol;
    switch (token.kind) {
      case pnga::deflate_trace::TokenKind::kLiteral:
        step.path = DecodeTracePath::kLiteral;
        step.event_text = literal_event_text(token.literal);
        break;
      case pnga::deflate_trace::TokenKind::kLengthDistance: {
        step.path = DecodeTracePath::kMatch;
        const auto* length = find_base_extra(kLengths, token.length);
        const auto* distance = find_base_extra(kDistances, token.distance);
        if (length == nullptr || distance == nullptr) {
          // Preserve the verified steps projected so far and report the
          // stop reason; the broken token itself is not projected.
          view.scope.status = TraceQueryStatus::kError;
          view.scope.stop_reason =
              "token length/distance is outside RFC 1951 ranges";
          continue;
        }
        step.event_text = match_event_text(token.length, token.distance);
        step.length_base = length->base;
        step.length_extra_bits = length->extra;
        step.length_extra_value = static_cast<std::uint16_t>(
            token.length - length->base);
        step.distance_base = distance->base;
        step.distance_extra_bits = distance->extra;
        step.distance_extra_value = static_cast<std::uint16_t>(
            token.distance - distance->base);
        // The copy target is the step's inflated output range; the overlap
        // fact compares it with the immediate copy source region
        // [target_begin - distance, +length) using checked half-open
        // intersection.
        step.match_target = step.output_range;
        if (step.output_range.begin.value >= step.distance) {
          const auto immediate_source = pnga::trace_model::make_range(
              pnga::trace_model::InflatedByteOffset{
                  step.output_range.begin.value - step.distance},
              step.length);
          step.match_overlaps = immediate_source.has_value() &&
                                immediate_source->overlaps(step.match_target);
        }
        break;
      }
      case pnga::deflate_trace::TokenKind::kEndOfBlock:
        step.path = DecodeTracePath::kEndOfBlock;
        step.event_text = kEndOfBlockEventText;
        break;
    }
    if (selected_token_index.has_value() &&
        *selected_token_index == token.index) {
      step.selected = true;
    }
    if (selected_output_offset.has_value() &&
        step.output_range.contains(
            pnga::trace_model::InflatedByteOffset{*selected_output_offset})) {
      step.contains_current = true;
      step.selected_byte_offset_in_event =
          *selected_output_offset - step.output_range.begin.value;
    }
    view.steps.push_back(std::move(step));
  }
  view.scope.returned_token_count = view.steps.size();
  return view;
}

std::string serialize_decode_trace_inspector(
    const DecodeTraceInspectorView& view) {
  std::ostringstream out;
  out << "decode-trace-v2\n";
  out << "status:" << trace_query_status_text(view.scope.status) << '\n';
  out << "generation:" << view.scope.generation << '\n';
  out << "requested-output:" << view.scope.requested_output.begin.value << ','
      << view.scope.requested_output.end.value << '\n';
  out << "tokens:" << view.scope.returned_token_count << '\n';
  out << "truncated:" << (view.scope.truncated ? 1 : 0) << '\n';
  out << "stop:" << view.scope.stop_reason << '\n';
  out << "steps:" << view.steps.size() << '\n';
  for (const auto& step : view.steps) {
    out << "step:" << step.token_index << ',' << step.block_index << ','
        << path_text(step.path) << ',' << step.input_range.begin.value << ':'
        << step.input_range.end.value << ",spans="
        << step.physical_input_spans.size();
    for (const auto& span : step.physical_input_spans) {
      out << ",file," << span.begin.value << ',' << span.end.value;
    }
    out << ',' << step.output_range.begin.value << ':'
        << step.output_range.end.value << ",event=" << step.event_text
        << ",symbol=";
    if (step.huffman_symbol.has_value()) {
      out << *step.huffman_symbol;
    } else {
      out << '-';
    }
    out << ",length=" << step.length << '+' << step.length_base << '+'
        << static_cast<unsigned>(step.length_extra_bits) << '+'
        << step.length_extra_value << ",distance=" << step.distance << '+'
        << step.distance_base << '+'
        << static_cast<unsigned>(step.distance_extra_bits) << '+'
        << step.distance_extra_value << ",sources="
        << step.match_source_ranges.size() << ",overlap="
        << (step.match_overlaps ? 1 : 0)
        << ",current=" << (step.contains_current ? 1 : 0)
        << ",selected=" << (step.selected ? 1 : 0) << ",selbyte=";
    if (step.selected_byte_offset_in_event.has_value()) {
      out << *step.selected_byte_offset_in_event;
    } else {
      out << '-';
    }
    out << '\n';
  }
  return out.str();
}

}  // namespace pnga::analysis_engine
