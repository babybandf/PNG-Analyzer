// WP-505C Decode Trace projection.

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

bool contains(std::uint64_t begin, std::uint64_t end,
              std::uint64_t offset) noexcept {
  return begin <= offset && offset < end;
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
  view.generation = trace.generation;
  view.selected_token_index = selected_token_index;
  view.selected_output_offset = selected_output_offset;
  switch (trace.status) {
    case TraceQueryStatus::kReady:
      view.status = DecodeTraceInspectorStatus::kReady;
      break;
    case TraceQueryStatus::kPartial:
    case TraceQueryStatus::kCancelled:
      view.status = DecodeTraceInspectorStatus::kPartial;
      break;
    case TraceQueryStatus::kError:
      view.status = DecodeTraceInspectorStatus::kError;
      view.error = trace.error;
      break;
    case TraceQueryStatus::kNotIndexed:
    case TraceQueryStatus::kReplaying:
      view.status = DecodeTraceInspectorStatus::kNoTrace;
      view.error = trace.error;
      break;
  }

  view.steps.reserve(trace.tokens.size());
  for (const auto& token : trace.tokens) {
    DecodeTraceStep step;
    step.token_index = token.index;
    step.block_index = token.block_index;
    step.input_bit_begin = token.input_bit_begin;
    step.input_bit_end = token.input_bit_end;
    step.output_begin = token.output_begin;
    step.output_end = token.output_end;
    step.literal = token.literal;
    step.length = token.length;
    step.distance = token.distance;
    step.match_source_ranges = token.match_source_ranges;
    switch (token.kind) {
      case pnga::deflate_trace::TokenKind::kLiteral:
        step.path = DecodeTracePath::kLiteral;
        step.huffman_symbol = token.literal;
        break;
      case pnga::deflate_trace::TokenKind::kLengthDistance: {
        step.path = DecodeTracePath::kMatch;
        const auto* length = find_base_extra(kLengths, token.length);
        const auto* distance = find_base_extra(kDistances, token.distance);
        if (length == nullptr || distance == nullptr) {
          view.status = DecodeTraceInspectorStatus::kError;
          view.error = "token length/distance is outside RFC 1951 ranges";
          continue;
        }
        step.huffman_symbol = static_cast<std::uint16_t>(257 +
                                                          (length - kLengths));
        step.length_base = length->base;
        step.length_extra_bits = length->extra;
        step.length_extra_value = static_cast<std::uint16_t>(
            token.length - length->base);
        step.distance_base = distance->base;
        step.distance_extra_bits = distance->extra;
        step.distance_extra_value = static_cast<std::uint16_t>(
            token.distance - distance->base);
        break;
      }
      case pnga::deflate_trace::TokenKind::kEndOfBlock:
        step.path = DecodeTracePath::kEndOfBlock;
        step.huffman_symbol = 256;
        break;
    }
    if (selected_token_index.has_value() &&
        *selected_token_index == token.index) {
      step.selected = true;
    }
    if (selected_output_offset.has_value() &&
        contains(step.output_begin, step.output_end,
                 *selected_output_offset)) {
      step.selected = true;
      step.selected_output_byte = *selected_output_offset;
    }
    view.steps.push_back(std::move(step));
  }
  return view;
}

std::string serialize_decode_trace_inspector(
    const DecodeTraceInspectorView& view) {
  std::ostringstream out;
  out << "status=" << status_text(view.status) << ";generation="
      << view.generation << ";selected_token=";
  if (view.selected_token_index.has_value()) {
    out << *view.selected_token_index;
  } else {
    out << '-';
  }
  out << ";selected_output=";
  if (view.selected_output_offset.has_value()) {
    out << *view.selected_output_offset;
  } else {
    out << '-';
  }
  out << ";error=" << view.error << ";steps=" << view.steps.size();
  for (const auto& step : view.steps) {
    out << "|" << step.token_index << ',' << path_text(step.path) << ','
        << step.input_bit_begin << ':' << step.input_bit_end << ','
        << step.output_begin << ':' << step.output_end << ",symbol=";
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
        << step.distance_extra_value << ",selected=" << (step.selected ? 1 : 0)
        << ",sources=" << step.match_source_ranges.size();
  }
  return out.str();
}

}  // namespace pnga::analysis_engine
