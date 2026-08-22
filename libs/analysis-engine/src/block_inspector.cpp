// WP-505A Block Inspector projection.

#include "pnga/analysis-engine/block_inspector.h"

#include <pnga/deflate-index/block_index.h>

#include <charconv>
#include <limits>
#include <sstream>

namespace pnga::analysis_engine {

namespace {

const char* status_text(BlockInspectorStatus status) noexcept {
  switch (status) {
    case BlockInspectorStatus::kNoTrace:
      return "no_trace";
    case BlockInspectorStatus::kReady:
      return "ready";
    case BlockInspectorStatus::kPartial:
      return "partial";
    case BlockInspectorStatus::kError:
      return "error";
  }
  return "unknown";
}

bool contains(std::uint64_t begin, std::uint64_t end,
              std::uint64_t offset) noexcept {
  return begin <= offset && offset < end;
}

void append_number(std::ostringstream& out, std::uint64_t value) {
  out << value;
}

void append_optional_number(std::ostringstream& out,
                            std::optional<std::uint64_t> value) {
  if (value.has_value()) {
    append_number(out, *value);
  } else {
    out << '-';
  }
}

}  // namespace

const char* block_inspector_status_text(BlockInspectorStatus status) noexcept {
  return status_text(status);
}

BlockInspectorView build_block_inspector(
    const TraceQueryResult& trace,
    std::optional<std::uint64_t> selected_output_offset,
    std::optional<std::uint64_t> scanline) {
  BlockInspectorView view;
  view.generation = trace.generation;
  view.scanline = scanline;
  view.selected_output_offset = selected_output_offset;

  switch (trace.status) {
    case TraceQueryStatus::kReady:
      view.status = BlockInspectorStatus::kReady;
      break;
    case TraceQueryStatus::kPartial:
    case TraceQueryStatus::kCancelled:
      view.status = BlockInspectorStatus::kPartial;
      break;
    case TraceQueryStatus::kError:
      view.status = BlockInspectorStatus::kError;
      view.error = trace.error;
      break;
    case TraceQueryStatus::kNotIndexed:
    case TraceQueryStatus::kReplaying:
      view.status = BlockInspectorStatus::kNoTrace;
      view.error = trace.error;
      break;
  }

  view.rows.reserve(trace.blocks.size());
  for (const auto& block : trace.blocks) {
    BlockInspectorRow row;
    row.block_index = block.index;
    row.type = block.type;
    row.last = block.last;
    row.input_bit_begin = block.input_bit_begin;
    row.input_bit_end = block.input_bit_end;
    row.output_begin = block.output_begin;
    row.output_end = block.output_end;
    row.physical_spans = block.physical_spans;
    if (selected_output_offset.has_value() &&
        contains(row.output_begin, row.output_end,
                 *selected_output_offset)) {
      row.current_output_position = *selected_output_offset;
      view.selected_block_index = row.block_index;
    }
    view.rows.push_back(std::move(row));
  }
  return view;
}

std::string serialize_block_inspector(const BlockInspectorView& view) {
  std::ostringstream out;
  out << "status=" << status_text(view.status) << ";generation=";
  append_number(out, view.generation);
  out << ";scanline=";
  append_optional_number(out, view.scanline);
  out << ";selected_output=";
  append_optional_number(out, view.selected_output_offset);
  out << ";selected_block=";
  append_optional_number(out, view.selected_block_index);
  out << ";error=" << view.error << ";rows=" << view.rows.size();
  for (const auto& row : view.rows) {
    out << "|";
    append_number(out, row.block_index);
    out << "," << pnga::deflate_index::block_type_text(row.type) << ","
        << (row.last ? 1 : 0) << ",";
    append_number(out, row.input_bit_begin);
    out << ":";
    append_number(out, row.input_bit_end);
    out << ",";
    append_number(out, row.output_begin);
    out << ":";
    append_number(out, row.output_end);
    out << ",current=";
    append_optional_number(out, row.current_output_position);
    out << ",spans=" << row.physical_spans.size();
  }
  return out.str();
}

}  // namespace pnga::analysis_engine
