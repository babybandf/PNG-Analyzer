// WP-5U1 coordinate summary query. Geometry comes from png-reconstruction;
// this file only composes immutable StageSet facts for UI consumers.

#include "pnga/analysis-engine/coordinate_query.h"

#include <pnga/png-reconstruction/scanline_layout.h>

#include <cstdint>
#include <limits>

namespace pnga::analysis_engine {

namespace {

using pnga::trace_model::ImageCoordinate;

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

struct Location {
  std::uint8_t pass_index = 0;
  std::uint64_t stream_row = 0;
  std::uint64_t row_in_pass = 0;
  std::uint64_t local_x = 0;
};

std::optional<Location> locate(const pnga::png_reconstruction::ScanlineLayout& layout,
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
      if (!checked_add(stream_row, pass.height, &stream_row)) {
        return std::nullopt;
      }
      continue;
    }
    const std::uint64_t local_x = (x - pass.x_start) / pass.x_step;
    const std::uint64_t row_in_pass = (y - pass.y_start) / pass.y_step;
    if (local_x >= pass.width || row_in_pass >= pass.height) {
      if (!checked_add(stream_row, pass.height, &stream_row)) {
        return std::nullopt;
      }
      continue;
    }
    std::uint64_t row = 0;
    if (!checked_add(stream_row, row_in_pass, &row)) {
      return std::nullopt;
    }
    return Location{static_cast<std::uint8_t>(p), row, row_in_pass, local_x};
  }
  return std::nullopt;
}

}  // namespace

const char* coordinate_query_status_text(CoordinateQueryStatus status) noexcept {
  switch (status) {
    case CoordinateQueryStatus::kReady:
      return "ready";
    case CoordinateQueryStatus::kNoSelection:
      return "no selection";
    case CoordinateQueryStatus::kNotApplicable:
      return "not applicable";
    case CoordinateQueryStatus::kOutOfRange:
      return "out of range";
    case CoordinateQueryStatus::kError:
      return "error";
  }
  return "unknown";
}

CoordinateSummary query_coordinate(
    const StageSet& stages,
    const pnga::trace_model::Selection& selection) {
  CoordinateSummary out;
  out.selection = selection;
  if (!selection.image.has_value()) {
    return out;
  }
  out.image = selection.image;
  if (!selection.image->valid()) {
    out.status = CoordinateQueryStatus::kOutOfRange;
    out.error = "image coordinate has invalid sample addressing";
    return out;
  }
  if (selection.image->frame != 0) {
    out.status = CoordinateQueryStatus::kNotApplicable;
    out.error = "frame is not available for static PNG analysis";
    return out;
  }
  if (selection.stage == pnga::trace_model::Stage::kFile ||
      selection.stage == pnga::trace_model::Stage::kChunk ||
      selection.stage == pnga::trace_model::Stage::kTrace) {
    out.status = CoordinateQueryStatus::kNotApplicable;
    out.error = "stage does not have image-coordinate semantics";
    return out;
  }
  if (!stages.success) {
    out.status = CoordinateQueryStatus::kError;
    out.error = "no stage data";
    return out;
  }
  const auto& coordinate = *selection.image;
  if (coordinate.x >= stages.header.width ||
      coordinate.y >= stages.header.height) {
    out.status = CoordinateQueryStatus::kOutOfRange;
    out.error = "image coordinate is outside the image";
    return out;
  }
  const auto layout =
      pnga::png_reconstruction::compute_scanline_layout(stages.header);
  if (!layout.has_value()) {
    out.status = CoordinateQueryStatus::kError;
    out.error = "invalid image layout";
    return out;
  }
  const auto location = locate(*layout, coordinate.x, coordinate.y);
  if (!location.has_value() || location->stream_row >= stages.scanlines.size()) {
    out.status = CoordinateQueryStatus::kOutOfRange;
    out.error = "image coordinate is not mapped to a scanline";
    return out;
  }
  const std::uint8_t expected_pass =
      stages.header.interlace ? static_cast<std::uint8_t>(location->pass_index + 1)
                              : 0;
  if (coordinate.pass != 0 && coordinate.pass != expected_pass) {
    out.status = CoordinateQueryStatus::kOutOfRange;
    out.error = "image pass does not match coordinate";
    return out;
  }
  if (coordinate.row != 0 && coordinate.row != location->row_in_pass) {
    out.status = CoordinateQueryStatus::kOutOfRange;
    out.error = "pass-local row does not match coordinate";
    return out;
  }

  const std::uint8_t channels = stages.native.channels != 0
                                    ? stages.native.channels
                                    : pnga::png_reconstruction::channels_for_color_type(
                                          stages.header.color_type);
  if (channels == 0) {
    out.status = CoordinateQueryStatus::kError;
    out.error = "image has no native channels";
    return out;
  }
  if (coordinate.channel.has_value() && *coordinate.channel >= channels) {
    out.status = CoordinateQueryStatus::kOutOfRange;
    out.error = "channel is outside the image format";
    return out;
  }
  const auto& pass = layout->passes[location->pass_index];
  const auto& scanline = stages.scanlines[location->stream_row];
  if (scanline.length == 0 || scanline.length - 1 != pass.row_bytes ||
      scanline.offset > stages.filtered.size() ||
      scanline.length > stages.filtered.size() - scanline.offset) {
    out.status = CoordinateQueryStatus::kError;
    out.error = "filtered scanline span is invalid";
    return out;
  }
  const auto full_row_bytes = pnga::png_reconstruction::row_bytes(
      stages.header.width, stages.header.bit_depth, stages.header.color_type);
  if (!full_row_bytes.has_value()) {
    out.status = CoordinateQueryStatus::kError;
    out.error = "invalid full image row layout";
    return out;
  }

  ImageCoordinate canonical = coordinate;
  canonical.pass = expected_pass;
  canonical.row = location->row_in_pass;
  out.image = canonical;
  out.selection.image = canonical;
  out.pass_index = location->pass_index;
  out.pass_number = expected_pass;
  out.stream_row = location->stream_row;
  out.row_in_pass = location->row_in_pass;
  out.local_x = location->local_x;
  out.channel_count = channels;

  const std::uint64_t selected_channel = coordinate.channel.value_or(0);
  std::uint64_t pass_sample = 0;
  if (!checked_mul(location->local_x, channels, &pass_sample) ||
      !checked_add(pass_sample, selected_channel, &pass_sample) ||
      !checked_mul(pass_sample, stages.header.bit_depth, &pass_sample)) {
    out.status = CoordinateQueryStatus::kError;
    out.error = "sample bit offset overflow";
    return out;
  }
  std::uint64_t full_sample = 0;
  if (!checked_mul(coordinate.x, channels, &full_sample) ||
      !checked_add(full_sample, selected_channel, &full_sample) ||
      !checked_mul(full_sample, stages.header.bit_depth, &full_sample)) {
    out.status = CoordinateQueryStatus::kError;
    out.error = "full sample bit offset overflow";
    return out;
  }

  if (coordinate.packed_sample.has_value()) {
    if (stages.header.bit_depth >= 8 ||
        coordinate.packed_sample->bit_length != stages.header.bit_depth ||
        static_cast<std::uint8_t>(pass_sample % 8) !=
            coordinate.packed_sample->bit_offset) {
      out.status = CoordinateQueryStatus::kOutOfRange;
      out.error = "packed sample bits do not match image format";
      return out;
    }
  }
  if (coordinate.sample_byte.has_value()) {
    const std::uint8_t sample_byte_count =
        stages.header.bit_depth >= 8
            ? static_cast<std::uint8_t>(stages.header.bit_depth / 8)
            : 0;
    if (coordinate.sample_byte.value() >= sample_byte_count) {
      out.status = CoordinateQueryStatus::kOutOfRange;
      out.error = stages.header.bit_depth < 8
                      ? "sample byte is not applicable to packed samples"
                      : "sample byte is outside the sample";
      return out;
    }
  }

  std::uint64_t filtered_byte = 0;
  if (!checked_add(scanline.offset, 1, &filtered_byte) ||
      !checked_add(filtered_byte, pass_sample / 8, &filtered_byte) ||
      filtered_byte >= stages.filtered.size()) {
    out.status = CoordinateQueryStatus::kError;
    out.error = "filtered sample offset overflow";
    return out;
  }
  std::uint64_t unfiltered_byte = 0;
  if (!checked_mul(coordinate.y, *full_row_bytes, &unfiltered_byte) ||
      !checked_add(unfiltered_byte, full_sample / 8, &unfiltered_byte) ||
      unfiltered_byte >= stages.unfiltered.size()) {
    out.status = CoordinateQueryStatus::kError;
    out.error = "unfiltered sample offset overflow";
    return out;
  }

  out.filtered_data_offset = filtered_byte;
  out.unfiltered_data_offset = unfiltered_byte;
  out.sample_bit_offset = pass_sample % 8;
  out.unfiltered_sample_bit_offset = full_sample % 8;
  out.sample_bit_length = stages.header.bit_depth;
  out.sample_byte_count = stages.header.bit_depth >= 8
                              ? static_cast<std::uint8_t>(stages.header.bit_depth / 8)
                              : 1;
  if (coordinate.sample_byte.has_value()) {
    if (!checked_add(out.filtered_data_offset, *coordinate.sample_byte,
                     &out.filtered_data_offset) ||
        !checked_add(out.unfiltered_data_offset, *coordinate.sample_byte,
                     &out.unfiltered_data_offset)) {
      out.status = CoordinateQueryStatus::kError;
      out.error = "sample byte offset overflow";
      return out;
    }
    std::uint64_t filtered_end = 0;
    std::uint64_t unfiltered_end = 0;
    if (!checked_add(out.filtered_data_offset, 1, &filtered_end) ||
        !checked_add(out.unfiltered_data_offset, 1, &unfiltered_end) ||
        filtered_end > stages.filtered.size() ||
        unfiltered_end > stages.unfiltered.size()) {
      out.status = CoordinateQueryStatus::kError;
      out.error = "sample byte range is outside stage data";
      return out;
    }
    out.sample_bit_offset = 0;
    out.unfiltered_sample_bit_offset = 0;
    out.sample_bit_length = 8;
    out.sample_byte_count = 1;
  }
  if (coordinate.channel.has_value()) {
    std::uint64_t native_index = 0;
    if (!checked_mul(coordinate.y, stages.header.width, &native_index) ||
        !checked_add(native_index, coordinate.x, &native_index) ||
        !checked_mul(native_index, channels, &native_index) ||
        !checked_add(native_index, *coordinate.channel, &native_index)) {
      out.status = CoordinateQueryStatus::kError;
      out.error = "native sample index overflow";
      return out;
    }
    out.native_sample_index = native_index;
  }
  out.status = CoordinateQueryStatus::kReady;
  return out;
}

}  // namespace pnga::analysis_engine
