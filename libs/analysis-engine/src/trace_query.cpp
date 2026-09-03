// WP-5T0A trace query contract implementation. This is a bounded composition
// layer over immutable block/token artifacts; it never starts a worker.

#include "pnga/analysis-engine/trace_query.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace pnga::analysis_engine {

namespace {

using pnga::trace_model::ProvenanceSpace;
using pnga::trace_model::ProvenanceSpan;

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

template <typename T>
void append_unique(std::vector<T>* values, const T& value) {
  if (std::find(values->begin(), values->end(), value) == values->end()) {
    values->push_back(value);
  }
}

const char* token_kind_text(pnga::deflate_trace::TokenKind kind) noexcept {
  switch (kind) {
    case pnga::deflate_trace::TokenKind::kLiteral:
      return "literal";
    case pnga::deflate_trace::TokenKind::kLengthDistance:
      return "match";
    case pnga::deflate_trace::TokenKind::kEndOfBlock:
      return "eob";
  }
  return "unknown";
}

const char* table_kind_text(
    pnga::deflate_trace::HuffmanTableKind kind) noexcept {
  switch (kind) {
    case pnga::deflate_trace::HuffmanTableKind::kCodeLength:
      return "code_length";
    case pnga::deflate_trace::HuffmanTableKind::kLiteralLength:
      return "literal_length";
    case pnga::deflate_trace::HuffmanTableKind::kDistance:
      return "distance";
  }
  return "unknown";
}

const char* space_text(ProvenanceSpace space) noexcept {
  switch (space) {
    case ProvenanceSpace::kNativeSample:
      return "native";
    case ProvenanceSpace::kReconstructed:
      return "reconstructed";
    case ProvenanceSpace::kFiltered:
      return "filtered";
    case ProvenanceSpace::kInflatedOutput:
      return "inflated";
    case ProvenanceSpace::kLogicalDeflate:
      return "logical_deflate";
    case ProvenanceSpace::kPhysicalFile:
      return "physical_file";
  }
  return "unknown";
}

const char* block_type_name(pnga::deflate_index::BlockType type) noexcept {
  return pnga::deflate_index::block_type_text(type);
}

bool overlaps(std::uint64_t begin, std::uint64_t end,
              std::uint64_t wanted_begin,
              std::uint64_t wanted_end) noexcept {
  return begin < wanted_end && wanted_begin < end;
}

// WP-5U12E: maps one token's DEFLATE bit range onto the exact ordered
// physical file bytes that contain it. The bit origin is the zlib-bit
// wrapper origin (checked multiplication of the byte-unit DEFLATE origin by
// 8); the byte envelope [floor(begin/8), ceil(end/8)) is mapped through
// VirtualIDATStream and every returned physical range is retained in order.
bool token_physical_spans(
    const pnga::png_format::VirtualIDATStream& stream,
    std::uint64_t wrapper_bits, const pnga::deflate_trace::TokenEvent& token,
    std::vector<pnga::trace_model::FileByteRange>* spans) {
  std::uint64_t absolute_begin = 0;
  std::uint64_t absolute_end = 0;
  if (!checked_add(wrapper_bits, token.input_bit_begin, &absolute_begin) ||
      !checked_add(wrapper_bits, token.input_bit_end, &absolute_end)) {
    return false;
  }
  if (absolute_end < absolute_begin) {
    return false;
  }
  if (absolute_end == absolute_begin) {
    return true;  // a zero-bit boundary consumes no data bytes
  }
  std::uint64_t rounded_end = 0;
  if (!checked_add(absolute_end, 7, &rounded_end)) {
    return false;
  }
  const std::uint64_t logical_begin = absolute_begin / 8;
  const std::uint64_t logical_end = rounded_end / 8;
  if (logical_end < logical_begin) {
    return false;
  }
  const std::uint64_t logical_length = logical_end - logical_begin;
  std::vector<pnga::png_format::PhysicalRange> ranges;
  if (!stream.logical_to_physical(logical_begin, logical_length, ranges)) {
    return false;
  }
  std::uint64_t covered = 0;
  for (const auto& range : ranges) {
    if (covered > std::numeric_limits<std::uint64_t>::max() - range.length) {
      return false;
    }
    covered += range.length;
    const auto made = pnga::trace_model::make_range(
        pnga::trace_model::FileByteOffset{range.offset}, range.length);
    if (!made.has_value()) {
      return false;
    }
    spans->push_back(*made);
  }
  // The mapped ranges must tile the whole byte envelope; a gap would hide
  // input provenance from the Hex navigation.
  return covered == logical_length;
}

bool append_bit_mapping(
    const pnga::png_format::VirtualIDATStream& stream,
    std::uint64_t logical_begin_bits, std::uint64_t logical_end_bits,
    std::vector<ProvenanceSpan>* logical,
    std::vector<ProvenanceSpan>* physical) {
  if (logical_end_bits <= logical_begin_bits) {
    return true;
  }
  std::uint64_t rounded_end = 0;
  if (!checked_add(logical_end_bits, 7, &rounded_end)) {
    return false;
  }
  const std::uint64_t logical_begin = logical_begin_bits / 8;
  const std::uint64_t logical_end = rounded_end / 8;
  if (logical_end < logical_begin) {
    return false;
  }
  const std::uint64_t logical_length = logical_end - logical_begin;
  append_unique(logical, ProvenanceSpan{
                               ProvenanceSpace::kLogicalDeflate,
                               logical_begin,
                               logical_length,
                               static_cast<std::uint8_t>(logical_begin_bits % 8),
                               logical_end_bits - logical_begin_bits,
                               true});

  std::vector<pnga::png_format::PhysicalRange> ranges;
  if (!stream.logical_to_physical(logical_begin, logical_length, ranges)) {
    return false;
  }
  std::uint64_t remaining = logical_end_bits - logical_begin_bits;
  std::uint8_t bit_offset = static_cast<std::uint8_t>(logical_begin_bits % 8);
  for (const auto& range : ranges) {
    std::uint64_t capacity = 0;
    if (!checked_mul(range.length, 8, &capacity) || capacity < bit_offset) {
      return false;
    }
    capacity -= bit_offset;
    const std::uint64_t take = std::min(remaining, capacity);
    append_unique(physical, ProvenanceSpan{
                                  ProvenanceSpace::kPhysicalFile,
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

void append_serialized_span(std::ostringstream& out,
                            const ProvenanceSpan& span) {
  out << space_text(span.space) << ',' << span.offset << ',' << span.length
      << ',' << static_cast<unsigned>(span.bit_offset) << ','
      << span.bit_length << ',' << (span.bit_aligned ? 1 : 0);
}

void append_serialized_range(std::ostringstream& out,
                             const pnga::deflate_trace::TokenOutputRange& r) {
  out << r.begin << ',' << r.end << ',' << r.token_index;
}

std::string escape_text(std::string_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char c : text) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

}  // namespace

const char* trace_query_status_text(TraceQueryStatus status) noexcept {
  switch (status) {
    case TraceQueryStatus::kNotIndexed:
      return "not indexed";
    case TraceQueryStatus::kReplaying:
      return "replaying";
    case TraceQueryStatus::kReady:
      return "ready";
    case TraceQueryStatus::kPartial:
      return "partial";
    case TraceQueryStatus::kError:
      return "error";
    case TraceQueryStatus::kCancelled:
      return "cancelled";
  }
  return "unknown";
}

TraceQueryResult compose_trace_query(
    std::uint64_t generation, const pnga::trace_model::Selection& selection,
    const pnga::deflate_index::BlockIndexResult& block_index,
    const pnga::deflate_trace::TokenDecodeResult& trace,
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource&, std::uint64_t inflated_begin,
    std::uint64_t inflated_end, std::uint64_t max_tokens) {
  TraceQueryResult out;
  out.generation = generation;
  out.selection = selection;
  out.inflated_begin = inflated_begin;
  out.inflated_end = inflated_end;
  out.output_bytes = trace.output_bytes != 0 ? trace.output_bytes
                                              : block_index.total_output_bytes;
  out.deflate_data_begin = trace.deflate_data_begin;

  if (inflated_end < inflated_begin || inflated_end > out.output_bytes) {
    out.status = TraceQueryStatus::kError;
    out.error = "inflated query range is out of bounds";
    return out;
  }
  if (max_tokens == 0) {
    out.status = TraceQueryStatus::kError;
    out.error = "trace token budget must be non-zero";
    return out;
  }
  if (!block_index.success && !trace.success) {
    out.status = TraceQueryStatus::kError;
    out.error = !block_index.error.empty()
                    ? block_index.error
                    : (!trace.error.empty() ? trace.error
                                             : "trace artifacts unavailable");
    return out;
  }

  for (const auto& block : block_index.blocks) {
    if (!overlaps(block.output_begin, block.output_end, inflated_begin,
                  inflated_end)) {
      continue;
    }
    TraceBlockSummary summary;
    summary.index = block.index;
    summary.type = block.type;
    summary.last = block.last;
    summary.input_bit_begin = block.input_bit_begin;
    summary.input_bit_end = block.input_bit_end;
    summary.output_begin = block.output_begin;
    summary.output_end = block.output_end;
    if (block.input_bit_end > block.input_bit_begin &&
        !append_bit_mapping(stream, block.input_bit_begin,
                            block.input_bit_end, &out.logical_input,
                            &summary.physical_spans)) {
      out.status = TraceQueryStatus::kError;
      out.error = "block input provenance is unavailable";
      return out;
    }
    out.blocks.push_back(std::move(summary));
  }

  // WP-5U12E: normalize the byte-unit DEFLATE payload origin supplied by the
  // trace artifact (ZlibByteOffset{2} ordinary, ZlibByteOffset{6} FDICT)
  // before any bit arithmetic; the checked multiplication by 8 yields the
  // zlib-bit origin shared by every token mapping below.
  const pnga::trace_model::ZlibByteOffset deflate_origin{
      trace.deflate_data_begin};
  std::uint64_t wrapper_bits = 0;
  const bool wrapper_bits_ok =
      checked_mul(deflate_origin.raw_value(), 8, &wrapper_bits);

  // A decoder stopped at its bounded output budget still owns verified
  // tables/tokens for the prefix it decoded. Preserve those artifacts in the
  // partial result; dropping them makes large valid images appear empty even
  // though their selected output range was already covered.
  const bool has_trace_artifacts =
      trace.success || !trace.tokens.empty() || !trace.huffman_tables.empty();
  if (has_trace_artifacts) {
    out.huffman_tables.reserve(trace.huffman_tables.size());
    for (const auto& table : trace.huffman_tables) {
      out.huffman_tables.push_back(
          TraceHuffmanTableSummary{table.kind, table.entries});
    }
    for (std::size_t i = 0; i < trace.tokens.size(); ++i) {
      const auto& token = trace.tokens[i];
      // flow-ui section 9.2: the End-of-block row stays visible after the
      // final Match. A zero-width EOB never intersects a half-open query
      // window, so it is retained exactly when its boundary position falls
      // inside the closed query window; every other token keeps the
      // half-open overlap rule.
      const bool boundary_eob =
          token.kind == pnga::deflate_trace::TokenKind::kEndOfBlock &&
          token.output_begin == token.output_end &&
          inflated_begin <= token.output_begin &&
          token.output_begin <= inflated_end;
      if (!boundary_eob &&
          !overlaps(token.output_begin, token.output_end, inflated_begin,
                    inflated_end)) {
        continue;
      }
      if (out.tokens.size() >= max_tokens) {
        out.truncated = true;
        break;
      }
      TraceTokenSummary summary;
      summary.index = i;
      summary.kind = token.kind;
      summary.input_bit_begin = token.input_bit_begin;
      summary.input_bit_end = token.input_bit_end;
      summary.output_begin = token.output_begin;
      summary.output_end = token.output_end;
      summary.literal = token.literal;
      summary.length = token.length;
      summary.distance = token.distance;
      summary.match_source_begin = token.match_source_begin;
      summary.match_source_end = token.match_source_end;
      summary.match_source_ranges = token.match_source_ranges;
      summary.huffman_symbol = token.huffman_symbol;
      for (const auto& block : block_index.blocks) {
        if (overlaps(token.output_begin, token.output_end, block.output_begin,
                     block.output_end)) {
          summary.block_index = static_cast<std::int64_t>(block.index);
          break;
        }
      }
      std::uint64_t absolute_begin = 0;
      std::uint64_t absolute_end = 0;
      if (!wrapper_bits_ok ||
          !checked_add(wrapper_bits, token.input_bit_begin,
                       &absolute_begin) ||
          !checked_add(wrapper_bits, token.input_bit_end, &absolute_end) ||
          !append_bit_mapping(stream, absolute_begin, absolute_end,
                              &out.logical_input, &out.physical_input) ||
          !token_physical_spans(stream, wrapper_bits, token,
                                &summary.physical_input_spans)) {
        out.status = TraceQueryStatus::kError;
        out.error = "token input provenance is unavailable";
        return out;
      }
      for (const auto& source_range : token.match_source_ranges) {
        append_unique(&out.match_source_ranges, source_range);
      }
      out.tokens.push_back(std::move(summary));
    }
  }

  if (!block_index.success || !trace.success) {
    out.status = TraceQueryStatus::kPartial;
    if (!block_index.success) {
      out.error = !block_index.error.empty() ? block_index.error
                                             : "trace index unavailable";
    } else {
      out.error = !trace.error.empty() ? trace.error : "trace replay unavailable";
    }
  } else if (out.truncated) {
    out.status = TraceQueryStatus::kPartial;
    out.error = "trace token budget exceeded";
  } else {
    out.status = TraceQueryStatus::kReady;
  }
  return out;
}

std::string serialize_trace_query(const TraceQueryResult& result) {
  std::ostringstream out;
  out << "trace-query-v1\n";
  out << "status:" << trace_query_status_text(result.status) << '\n';
  out << "generation:" << result.generation << '\n';
  out << "selection:" << pnga::trace_model::serialize(result.selection) << '\n';
  out << "error:" << escape_text(result.error) << '\n';
  out << "range:" << result.inflated_begin << ',' << result.inflated_end << '\n';
  out << "output:" << result.output_bytes << '\n';
  out << "deflate-data-begin:" << result.deflate_data_begin << '\n';
  out << "truncated:" << (result.truncated ? 1 : 0) << '\n';
  out << "blocks:" << result.blocks.size() << '\n';
  for (const auto& block : result.blocks) {
    out << "block:" << block.index << ',' << block_type_name(block.type) << ','
        << (block.last ? 1 : 0) << ',' << block.input_bit_begin << ','
        << block.input_bit_end << ',' << block.output_begin << ','
        << block.output_end << ",spans=" << block.physical_spans.size();
    for (const auto& span : block.physical_spans) {
      out << ',';
      append_serialized_span(out, span);
    }
    out << '\n';
  }
  out << "tokens:" << result.tokens.size() << '\n';
  for (const auto& token : result.tokens) {
    out << "token:" << token.index << ',' << token_kind_text(token.kind) << ','
        << token.input_bit_begin << ',' << token.input_bit_end << ','
        << token.output_begin << ',' << token.output_end << ','
        << static_cast<unsigned>(token.literal) << ',' << token.length << ','
        << token.distance << ',' << token.match_source_begin << ','
        << token.match_source_end << ',' << token.block_index << ',';
    if (token.huffman_symbol.has_value()) {
      out << *token.huffman_symbol;
    } else {
      out << '-';
    }
    out << ",spans=" << token.physical_input_spans.size();
    for (const auto& span : token.physical_input_spans) {
      out << ",file," << span.begin.value << ',' << span.end.value;
    }
    out << ",sources=" << token.match_source_ranges.size();
    for (const auto& range : token.match_source_ranges) {
      out << ',';
      append_serialized_range(out, range);
    }
    out << '\n';
  }
  out << "tables:" << result.huffman_tables.size() << '\n';
  for (const auto& table : result.huffman_tables) {
    out << "table:" << table_kind_text(table.kind) << ",entries="
        << table.entries.size();
    for (const auto& entry : table.entries) {
      out << ',' << entry.symbol << ',' << static_cast<unsigned>(entry.bit_length)
          << ',' << entry.canonical_code << ',' << entry.provenance_bit_begin
          << ',' << entry.provenance_bit_end;
    }
    out << '\n';
  }
  out << "logical:" << result.logical_input.size() << '\n';
  for (const auto& span : result.logical_input) {
    out << "span:";
    append_serialized_span(out, span);
    out << '\n';
  }
  out << "physical:" << result.physical_input.size() << '\n';
  for (const auto& span : result.physical_input) {
    out << "span:";
    append_serialized_span(out, span);
    out << '\n';
  }
  out << "matches:" << result.match_source_ranges.size() << '\n';
  for (const auto& range : result.match_source_ranges) {
    out << "match:";
    append_serialized_range(out, range);
    out << '\n';
  }
  return out.str();
}

}  // namespace pnga::analysis_engine
