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

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* out) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  *out = left + right;
  return true;
}

bool checked_mul(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* out) noexcept {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  *out = left * right;
  return true;
}

bool append_physical_bit_spans(
    const pnga::png_format::VirtualIDATStream& stream,
    pnga::trace_model::ZlibBitRange bits,
    std::vector<pnga::trace_model::ProvenanceSpan>* spans) {
  if (!bits.valid()) {
    return false;
  }
  if (bits.empty()) {
    return true;
  }
  std::uint64_t rounded_end = 0;
  if (!checked_add(bits.end.value, 7, &rounded_end)) {
    return false;
  }
  const std::uint64_t logical_begin = bits.begin.value / 8;
  const std::uint64_t logical_end = rounded_end / 8;
  if (logical_end < logical_begin) {
    return false;
  }
  const std::uint64_t logical_length = logical_end - logical_begin;
  std::vector<pnga::png_format::PhysicalRange> ranges;
  if (!stream.logical_to_physical(logical_begin, logical_length, ranges)) {
    return false;
  }

  std::uint64_t remaining = bits.end.value - bits.begin.value;
  std::uint8_t bit_offset = static_cast<std::uint8_t>(bits.begin.value % 8);
  for (const auto& range : ranges) {
    std::uint64_t capacity = 0;
    if (!checked_mul(range.length, 8, &capacity) || capacity < bit_offset) {
      return false;
    }
    capacity -= bit_offset;
    const std::uint64_t take = std::min(remaining, capacity);
    spans->push_back(pnga::trace_model::ProvenanceSpan{
        pnga::trace_model::ProvenanceSpace::kPhysicalFile,
        range.offset,
        range.length,
        bit_offset,
        take,
        true});
    remaining -= take;
    bit_offset = 0;
    if (remaining == 0) {
      break;
    }
  }
  return remaining == 0;
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

const char* fast_compression_index_status_text(
    FastCompressionIndexStatus status) noexcept {
  switch (status) {
    case FastCompressionIndexStatus::kUnavailable:
      return "unavailable";
    case FastCompressionIndexStatus::kReady:
      return "ready";
    case FastCompressionIndexStatus::kPartial:
      return "partial";
    case FastCompressionIndexStatus::kError:
      return "error";
  }
  return "unknown";
}

FastCompressionIndexView build_fast_compression_index(
    std::uint64_t generation,
    const pnga::deflate_index::BlockIndexResult& block_index,
    const pnga::png_format::VirtualIDATStream& stream) {
  FastCompressionIndexView view;
  view.generation = generation;

  // Stream summary: wrapper, byte-aligned payload origin, IDAT spans, checksum
  // and stop facts, projected once per generation with checked arithmetic.
  FastCompressionStreamSummary& summary = view.stream;
  summary.wrapper = block_index.wrapper;
  summary.adler = block_index.adler;
  summary.total_output_bytes = block_index.total_output_bytes;
  summary.adler_ok = block_index.adler.status ==
                     pnga::deflate_index::Adler32Status::kMatch;
  if (block_index.stop_input_bit.has_value()) {
    summary.stop_input =
        pnga::trace_model::ZlibBitOffset{*block_index.stop_input_bit};
  }
  if (block_index.stop_output_byte.has_value()) {
    summary.stop_output =
        pnga::trace_model::InflatedByteOffset{*block_index.stop_output_byte};
  }

  const auto stream_range =
      make_range(pnga::trace_model::ZlibByteOffset{0}, stream.size());
  if (stream_range.has_value()) {
    summary.stream_range = *stream_range;
  } else {
    view.status = FastCompressionIndexStatus::kError;
    view.error = "fast stream range overflowed";
    return view;
  }

  if (block_index.zlib_header_bits % 8 != 0) {
    view.status = FastCompressionIndexStatus::kError;
    view.error = "DEFLATE payload origin is not byte-aligned";
    return view;
  }
  summary.deflate_data_begin =
      pnga::trace_model::ZlibByteOffset{block_index.zlib_header_bits / 8};

  summary.idat_spans.reserve(stream.segment_count());
  for (std::size_t i = 0; i < stream.segment_count(); ++i) {
    const pnga::png_format::IdatSegment& segment = stream.segment(i);
    const auto logical =
        make_range(pnga::trace_model::ZlibByteOffset{segment.logical_start},
                   segment.length);
    const auto physical =
        make_range(pnga::trace_model::FileByteOffset{segment.physical_offset},
                   segment.length);
    if (!logical.has_value() || !physical.has_value()) {
      view.status = FastCompressionIndexStatus::kError;
      view.error = "fast IDAT span mapping overflowed";
      return view;
    }
    summary.idat_spans.push_back(
        FastCompressionIdatSpan{*logical, *physical});
  }
  summary.idat_segment_count = summary.idat_spans.size();

  // Complete Block list with every physical bit span.
  view.blocks.reserve(block_index.blocks.size());
  for (const auto& block : block_index.blocks) {
    FastCompressionBlockRow row;
    row.block_index = block.index;
    row.type = block.type;
    row.last = block.last;
    row.input_range = {
        pnga::trace_model::ZlibBitOffset{block.input_bit_begin},
        pnga::trace_model::ZlibBitOffset{block.input_bit_end}};
    row.output_range = {
        pnga::trace_model::InflatedByteOffset{block.output_begin},
        pnga::trace_model::InflatedByteOffset{block.output_end}};
    if (!append_physical_bit_spans(stream, row.input_range,
                                   &row.physical_spans)) {
      view.status = FastCompressionIndexStatus::kError;
      view.error = "fast block input provenance is unavailable";
      return view;
    }
    view.blocks.push_back(std::move(row));
  }

  if (block_index.success) {
    view.status = FastCompressionIndexStatus::kReady;
  } else if (!block_index.blocks.empty()) {
    view.status = FastCompressionIndexStatus::kPartial;
    view.error = block_index.error;
  } else if (!block_index.error.empty()) {
    view.status = FastCompressionIndexStatus::kError;
    view.error = block_index.error;
  } else {
    view.status = FastCompressionIndexStatus::kUnavailable;
  }
  return view;
}

}  // namespace pnga::analysis_engine
