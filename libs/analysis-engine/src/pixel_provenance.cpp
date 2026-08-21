// WP-504 pixel/channel provenance query. The query is deliberately a
// composition layer: reconstruction owns pixel geometry, Deflate Trace owns
// token bits, and VirtualIDATStream owns logical-to-physical mapping.

#include "pnga/analysis-engine/pixel_provenance.h"

#include <pnga/png-reconstruction/reverse_filter.h>

#include "virtual_idat_source.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace pnga::analysis_engine {

namespace {

using pnga::trace_model::ProvenanceSpace;
using pnga::trace_model::ProvenanceSpan;

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* out) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  *out = left + right;
  return true;
}

bool checked_mul(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* out) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  *out = left * right;
  return true;
}

void append_unique(std::vector<ProvenanceSpan>* spans, ProvenanceSpan span) {
  if (span.length == 0 || span.bit_aligned && span.bit_length == 0) {
    return;
  }
  if (std::find(spans->begin(), spans->end(), span) == spans->end()) {
    spans->push_back(span);
  }
}

void append_unique(std::vector<pnga::deflate_trace::TokenOutputRange>* ranges,
                   pnga::deflate_trace::TokenOutputRange range) {
  if (range.begin >= range.end) {
    return;
  }
  if (std::find(ranges->begin(), ranges->end(), range) == ranges->end()) {
    ranges->push_back(range);
  }
}

bool byte_span(ProvenanceSpace space, std::uint64_t offset,
               std::uint64_t length, std::vector<ProvenanceSpan>* out) {
  if (length == 0) {
    return true;
  }
  append_unique(out, ProvenanceSpan{space, offset, length, 0, 0, false});
  return true;
}

bool bit_span(ProvenanceSpace space, std::uint64_t offset,
              std::uint8_t bit_offset, std::uint64_t bit_length,
              std::vector<ProvenanceSpan>* out) {
  if (bit_length == 0 || bit_offset >= 8) {
    return bit_length == 0;
  }
  std::uint64_t covered_bits = 0;
  if (!checked_add(static_cast<std::uint64_t>(bit_offset), bit_length,
                   &covered_bits)) {
    return false;
  }
  std::uint64_t rounded_bits = 0;
  if (!checked_add(covered_bits, 7, &rounded_bits)) {
    return false;
  }
  const std::uint64_t length = rounded_bits / 8;
  append_unique(out, ProvenanceSpan{space, offset, length, bit_offset,
                                    bit_length, true});
  return true;
}

struct PixelLocation {
  std::size_t pass = 0;
  std::uint64_t stream_row = 0;
  std::uint64_t row_in_pass = 0;
  std::uint64_t local_x = 0;
};

std::optional<PixelLocation> locate_pixel(
    const pnga::png_reconstruction::ScanlineLayout& layout,
    std::uint64_t x, std::uint64_t y) {
  std::uint64_t stream_row = 0;
  for (std::size_t p = 0; p < layout.pass_count; ++p) {
    const auto& pass = layout.passes[p];
    if (pass.width == 0 || pass.height == 0) {
      continue;
    }
    if (x < pass.x_start || y < pass.y_start ||
        (x - pass.x_start) % pass.x_step != 0 ||
        (y - pass.y_start) % pass.y_step != 0) {
      stream_row += pass.height;
      continue;
    }
    const std::uint64_t local_x = (x - pass.x_start) / pass.x_step;
    const std::uint64_t row_in_pass = (y - pass.y_start) / pass.y_step;
    if (local_x >= pass.width || row_in_pass >= pass.height) {
      stream_row += pass.height;
      continue;
    }
    return PixelLocation{p, stream_row + row_in_pass, row_in_pass, local_x};
  }
  return std::nullopt;
}

bool map_token_bits(
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::deflate_trace::TokenDecodeResult& trace,
    const pnga::deflate_trace::TokenEvent& token,
    PixelProvenanceResult* out) {
  if (token.input_bit_end < token.input_bit_begin) {
    return false;
  }
  std::uint64_t wrapper_bits = 0;
  if (!checked_mul(trace.deflate_data_begin, 8, &wrapper_bits)) {
    return false;
  }
  std::uint64_t logical_begin_bits = 0;
  std::uint64_t logical_end_bits = 0;
  if (!checked_add(wrapper_bits, token.input_bit_begin,
                   &logical_begin_bits) ||
      !checked_add(wrapper_bits, token.input_bit_end, &logical_end_bits) ||
      logical_end_bits <= logical_begin_bits) {
    return true;  // zero-width synthetic events have no file bits
  }
  const std::uint64_t logical_byte_begin = logical_begin_bits / 8;
  std::uint64_t rounded_end_bits = 0;
  if (!checked_add(logical_end_bits, 7, &rounded_end_bits)) {
    return false;
  }
  const std::uint64_t logical_byte_end = rounded_end_bits / 8;
  if (logical_byte_end < logical_byte_begin) {
    return false;
  }
  const std::uint64_t logical_bytes = logical_byte_end - logical_byte_begin;
  if (!bit_span(ProvenanceSpace::kLogicalDeflate, logical_byte_begin,
                static_cast<std::uint8_t>(logical_begin_bits % 8),
                logical_end_bits - logical_begin_bits,
                &out->logical_input)) {
    return false;
  }

  std::vector<pnga::png_format::PhysicalRange> physical;
  if (!stream.logical_to_physical(logical_byte_begin, logical_bytes,
                                  physical)) {
    return false;
  }
  std::uint64_t remaining_bits = logical_end_bits - logical_begin_bits;
  std::uint8_t segment_bit_offset =
      static_cast<std::uint8_t>(logical_begin_bits % 8);
  for (const auto& range : physical) {
    std::uint64_t capacity_bits = 0;
    if (!checked_mul(range.length, 8, &capacity_bits) ||
        capacity_bits < segment_bit_offset) {
      return false;
    }
    capacity_bits -= segment_bit_offset;
    const std::uint64_t take = std::min(remaining_bits, capacity_bits);
    if (!bit_span(ProvenanceSpace::kPhysicalFile, range.offset,
                  segment_bit_offset, take, &out->physical_input)) {
      return false;
    }
    remaining_bits -= take;
    segment_bit_offset = 0;
    if (remaining_bits == 0) {
      break;
    }
  }
  return remaining_bits == 0;
}

bool append_filter_dependency(
    const StageSet& stages, const PixelLocation& location,
    std::uint64_t data_index, std::uint64_t bpp,
    pnga::png_reconstruction::FilterType filter,
    std::vector<ProvenanceSpan>* filtered) {
  if (location.stream_row >= stages.scanlines.size()) {
    return false;
  }
  const auto& row = stages.scanlines[location.stream_row];
  if (row.offset > stages.filtered.size() || row.length == 0 ||
      row.length - 1 > stages.filtered.size() - row.offset) {
    return false;
  }
  const std::uint64_t data_length = row.length - 1;
  if (data_index >= data_length) {
    return false;
  }
  std::uint64_t data_offset = 0;
  if (!checked_add(row.offset, 1, &data_offset) ||
      !checked_add(data_offset, data_index, &data_offset)) {
    return false;
  }

  const auto add_same_row = [&](std::uint64_t index) {
    std::uint64_t offset = 0;
    if (index >= data_length || !checked_add(data_offset, index - data_index,
                                             &offset)) {
      return false;
    }
    return byte_span(ProvenanceSpace::kFiltered, offset, 1, filtered);
  };
  const auto add_previous_row = [&](std::uint64_t row_index) {
    if (location.row_in_pass == 0 || row_index >= data_length) {
      return false;
    }
    const std::uint64_t previous =
        location.stream_row - location.row_in_pass;
    if (previous >= stages.scanlines.size()) {
      return false;
    }
    const auto& previous_row = stages.scanlines[previous];
    if (previous_row.length == 0 || row_index >= previous_row.length - 1) {
      return false;
    }
    std::uint64_t offset = 0;
    if (!checked_add(previous_row.offset, 1, &offset) ||
        !checked_add(offset, row_index, &offset)) {
      return false;
    }
    return byte_span(ProvenanceSpace::kFiltered, offset, 1, filtered);
  };

  if ((filter == pnga::png_reconstruction::FilterType::kSub ||
       filter == pnga::png_reconstruction::FilterType::kAverage ||
       filter == pnga::png_reconstruction::FilterType::kPaeth) &&
      data_index >= bpp && !add_same_row(data_index - bpp)) {
    return false;
  }
  if ((filter == pnga::png_reconstruction::FilterType::kUp ||
       filter == pnga::png_reconstruction::FilterType::kAverage ||
       filter == pnga::png_reconstruction::FilterType::kPaeth) &&
      location.row_in_pass > 0 && !add_previous_row(data_index)) {
    return false;
  }
  if (filter == pnga::png_reconstruction::FilterType::kPaeth &&
      location.row_in_pass > 0 && data_index >= bpp &&
      !add_previous_row(data_index - bpp)) {
    return false;
  }
  return true;
}

}  // namespace

PixelProvenanceResult query_pixel_provenance(
    const StageSet& stages,
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source, std::uint64_t x, std::uint64_t y,
    std::uint64_t channel, std::uint64_t max_trace_output) {
  PixelProvenanceResult out;
  out.x = x;
  out.y = y;
  out.channel = channel;
  if (!stages.success) {
    out.error = "no stage data";
    return out;
  }
  if (x >= stages.header.width || y >= stages.header.height ||
      channel >= stages.native.channels || max_trace_output == 0) {
    out.error = "pixel or trace budget out of range";
    return out;
  }
  const auto layout =
      pnga::png_reconstruction::compute_scanline_layout(stages.header);
  if (!layout.has_value()) {
    out.error = "invalid image layout";
    return out;
  }
  const auto location = locate_pixel(*layout, x, y);
  if (!location.has_value() || location->stream_row >= stages.scanlines.size()) {
    out.error = "pixel is not mapped to a scanline";
    return out;
  }
  const auto bpp = pnga::png_reconstruction::filter_bpp(
      stages.header.bit_depth, stages.header.color_type);
  if (!bpp.has_value()) {
    out.error = "invalid filter bytes-per-pixel";
    return out;
  }

  pnga::analysis_engine::VirtualIdatSource adapter(stream, source);
  const auto trace = pnga::deflate_trace::decode_stored_and_fixed(
      adapter, max_trace_output);
  if (!trace.success) {
    out.error = "deflate trace: " + trace.error;
    return out;
  }
  if (trace.output != stages.filtered) {
    out.error = "deflate trace output differs from filtered stage";
    return out;
  }

  const std::uint64_t channels = stages.native.channels;
  std::uint64_t sample_number = 0;
  if (!checked_mul(y, stages.header.width, &sample_number) ||
      !checked_add(sample_number, x, &sample_number) ||
      !checked_mul(sample_number, channels, &sample_number) ||
      !checked_add(sample_number, channel, &sample_number)) {
    out.error = "native sample offset overflow";
    return out;
  }
  std::uint64_t native_offset = 0;
  if (!checked_mul(sample_number, 2, &native_offset) ||
      !byte_span(ProvenanceSpace::kNativeSample, native_offset, 2,
                 &out.native_samples)) {
    out.error = "native sample range overflow";
    return out;
  }

  std::uint64_t pass_sample_number = 0;
  std::uint64_t sample_bits = 0;
  if (!checked_mul(location->local_x, channels, &pass_sample_number) ||
      !checked_add(pass_sample_number, channel, &pass_sample_number) ||
      !checked_mul(pass_sample_number, stages.header.bit_depth,
                   &sample_bits)) {
    out.error = "pass sample bit offset overflow";
    return out;
  }
  const std::uint64_t pass_byte = sample_bits / 8;
  std::uint64_t pass_row_data = 0;
  const auto& scanline = stages.scanlines[location->stream_row];
  if (scanline.length == 0 ||
      !checked_add(scanline.offset, 1, &pass_row_data) ||
      !checked_add(pass_row_data, pass_byte, &pass_row_data) ||
      pass_byte >= scanline.length - 1) {
    out.error = "pass sample range out of bounds";
    return out;
  }
  std::uint64_t sample_covered_bits = 0;
  std::uint64_t sample_rounded_bits = 0;
  if (!checked_add(sample_bits % 8, stages.header.bit_depth,
                   &sample_covered_bits) ||
      !checked_add(sample_covered_bits, 7, &sample_rounded_bits)) {
    out.error = "pass sample bit range overflow";
    return out;
  }
  const std::uint64_t sample_byte_count = sample_rounded_bits / 8;
  if (sample_byte_count > scanline.length - 1 - pass_byte) {
    out.error = "pass sample range out of bounds";
    return out;
  }
  if (!bit_span(ProvenanceSpace::kFiltered, pass_row_data,
                static_cast<std::uint8_t>(sample_bits % 8),
                stages.header.bit_depth, &out.filtered)) {
    out.error = "filtered sample range overflow";
    return out;
  }
  std::uint64_t full_row_bytes = 0;
  if (!pnga::png_reconstruction::row_bytes(
          stages.header.width, stages.header.bit_depth,
          stages.header.color_type)
           .has_value()) {
    out.error = "invalid full row layout";
    return out;
  } else {
    full_row_bytes = *pnga::png_reconstruction::row_bytes(
        stages.header.width, stages.header.bit_depth,
        stages.header.color_type);
  }
  std::uint64_t full_sample_number = 0;
  std::uint64_t full_pixel_bits = 0;
  if (!checked_mul(x, channels, &full_sample_number) ||
      !checked_add(full_sample_number, channel, &full_sample_number) ||
      !checked_mul(full_sample_number, stages.header.bit_depth,
                   &full_pixel_bits)) {
    out.error = "reconstructed sample offset overflow";
    return out;
  }
  std::uint64_t reconstructed_offset = 0;
  if (!checked_mul(y, full_row_bytes, &reconstructed_offset) ||
      !checked_add(reconstructed_offset, full_pixel_bits / 8,
                   &reconstructed_offset) ||
      !bit_span(ProvenanceSpace::kReconstructed, reconstructed_offset,
                static_cast<std::uint8_t>(full_pixel_bits % 8),
                stages.header.bit_depth, &out.reconstructed)) {
    out.error = "reconstructed sample range overflow";
    return out;
  }

  const auto formula = filter_formula(stages, location->stream_row);
  if (!formula.success) {
    out.error = "filter provenance unavailable";
    return out;
  }
  for (std::uint64_t i = 0; i < sample_byte_count; ++i) {
    const std::uint64_t data_index = pass_byte + i;
    if (data_index >= formula.events.size() ||
        !append_filter_dependency(stages, *location, data_index, *bpp,
                                   formula.filter, &out.filtered)) {
      out.error = "filter dependency range out of bounds";
      return out;
    }
  }

  // Filter dependencies are also inflated bytes. Walk the deduplicated
  // filtered ranges so Up/Average/Paeth fan-in reaches its own token(s) and
  // input bits instead of only tracing the selected sample's direct byte.
  for (const auto& filtered_span : out.filtered) {
    std::uint64_t end = 0;
    if (!checked_add(filtered_span.offset, filtered_span.length, &end)) {
      out.error = "filtered provenance range overflow";
      return out;
    }
    for (std::uint64_t inflated_offset = filtered_span.offset;
         inflated_offset < end; ++inflated_offset) {
      byte_span(ProvenanceSpace::kInflatedOutput, inflated_offset, 1,
                &out.inflated);
      std::uint64_t next = 0;
      if (!checked_add(inflated_offset, 1, &next)) {
        out.error = "inflated range overflow";
        return out;
      }
      const auto token_ranges =
          trace.output_index.overlapping(inflated_offset, next);
      for (const auto& range : token_ranges) {
        append_unique(&out.token_output_ranges, range);
        if (range.token_index >= trace.tokens.size() ||
            !map_token_bits(stream, trace, trace.tokens[range.token_index],
                            &out)) {
          out.error = "token input provenance unavailable";
          return out;
        }
        for (const auto& source_range :
             trace.tokens[range.token_index].match_source_ranges) {
          append_unique(&out.match_source_ranges, source_range);
        }
      }
    }
  }

  out.success = true;
  return out;
}

}  // namespace pnga::analysis_engine
