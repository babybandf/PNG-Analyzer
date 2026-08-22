// WP-5U5A reconstruction view model implementation.

#include "pnga/analysis-engine/reconstruct_view_model.h"

#include <algorithm>
#include <limits>

namespace pnga::analysis_engine {

namespace {

bool mul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (b != 0 && a > std::numeric_limits<std::uint64_t>::max() / b) {
    return false;
  }
  out = a * b;
  return true;
}

bool add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (a > std::numeric_limits<std::uint64_t>::max() - b) {
    return false;
  }
  out = a + b;
  return true;
}

}  // namespace

const char* reconstruct_status_text(ReconstructStatus status) noexcept {
  switch (status) {
    case ReconstructStatus::kReady:
      return "ready";
    case ReconstructStatus::kNoStage:
      return "no stage";
    case ReconstructStatus::kOutOfRange:
      return "out of range";
    case ReconstructStatus::kError:
      return "error";
  }
  return "error";
}

ReconstructViewModel build_reconstruct_view(const StageSet& stages,
                                            std::uint64_t x,
                                            std::uint64_t y,
                                            std::uint64_t neighbor_radius) {
  ReconstructViewModel out;
  out.x = x;
  out.y = y;
  if (!stages.success) {
    out.status = ReconstructStatus::kNoStage;
    out.error = "no stage data";
    return out;
  }
  if (x >= stages.header.width || y >= stages.header.height) {
    out.status = ReconstructStatus::kOutOfRange;
    out.error = "coordinate out of range";
    return out;
  }

  const auto layout =
      pnga::png_reconstruction::compute_scanline_layout(stages.header);
  const auto channels = pnga::png_reconstruction::channels_for_color_type(
      stages.header.color_type);
  const auto bpp = pnga::png_reconstruction::filter_bpp(
      stages.header.bit_depth, stages.header.color_type);
  const auto row_bytes = pnga::png_reconstruction::row_bytes(
      stages.header.width, stages.header.bit_depth, stages.header.color_type);
  if (!layout.has_value() || channels == 0 || !bpp.has_value() ||
      !row_bytes.has_value()) {
    out.status = ReconstructStatus::kError;
    out.error = "invalid image layout";
    return out;
  }

  std::uint64_t cursor = 0;
  bool found = false;
  for (std::size_t pass = 0; pass < layout->pass_count; ++pass) {
    const auto& geometry = layout->passes[pass];
    if (geometry.width == 0 || geometry.height == 0) {
      continue;
    }
    if (x >= geometry.x_start && y >= geometry.y_start &&
        (x - geometry.x_start) % geometry.x_step == 0 &&
        (y - geometry.y_start) % geometry.y_step == 0) {
      out.pass = pass;
      out.pass_x = (x - geometry.x_start) / geometry.x_step;
      out.pass_y = (y - geometry.y_start) / geometry.y_step;
      if (!add(cursor, out.pass_y, out.stream_row)) {
        out.status = ReconstructStatus::kError;
        out.error = "stream row overflow";
        return out;
      }
      found = true;
      break;
    }
    if (!add(cursor, geometry.height, cursor)) {
      out.status = ReconstructStatus::kError;
      out.error = "stream row overflow";
      return out;
    }
  }
  if (!found || out.stream_row >= stages.scanlines.size()) {
    out.status = ReconstructStatus::kError;
    out.error = "coordinate not mapped to a pass";
    return out;
  }

  std::uint64_t pixel_sample = 0;
  if (!mul(y, stages.header.width, pixel_sample) ||
      !add(pixel_sample, x, pixel_sample) ||
      !mul(pixel_sample, channels, out.sample_index)) {
    out.status = ReconstructStatus::kError;
    out.error = "sample index overflow";
    return out;
  }

  std::uint64_t pass_sample = 0;
  if (!mul(out.pass_x, channels, pass_sample) ||
      !mul(pass_sample, stages.header.bit_depth >= 8
                               ? stages.header.bit_depth / 8
                               : 1,
           pass_sample)) {
    out.status = ReconstructStatus::kError;
    out.error = "byte offset overflow";
    return out;
  }
  if (stages.header.bit_depth < 8) {
    std::uint64_t packed_bits = 0;
    if (!mul(pass_sample, stages.header.bit_depth, packed_bits)) {
      out.status = ReconstructStatus::kError;
      out.error = "packed byte offset overflow";
      return out;
    }
    out.selected_byte = packed_bits / 8;
  } else {
    out.selected_byte = pass_sample;
  }
  const auto& span = stages.scanlines[out.stream_row];
  if (span.length == 0 || span.length - 1 <= out.selected_byte ||
      span.offset > stages.filtered.size() ||
      span.length > stages.filtered.size() - span.offset) {
    out.status = ReconstructStatus::kError;
    out.error = "filtered span out of range";
    return out;
  }
  if (!add(span.offset, 1, out.filtered_byte_offset) ||
      !add(out.filtered_byte_offset, out.selected_byte,
           out.filtered_byte_offset)) {
    out.status = ReconstructStatus::kError;
    out.error = "filtered offset overflow";
    return out;
  }

  std::uint64_t image_bits = 0;
  if (!mul(x, channels, image_bits) ||
      !mul(image_bits, stages.header.bit_depth, image_bits)) {
    out.status = ReconstructStatus::kError;
    out.error = "unfiltered offset overflow";
    return out;
  }
  const std::uint64_t image_byte = image_bits / 8;
  if (!mul(y, *row_bytes, out.unfiltered_byte_offset) ||
      !add(out.unfiltered_byte_offset, image_byte,
           out.unfiltered_byte_offset) ||
      out.unfiltered_byte_offset >= stages.unfiltered.size()) {
    out.status = ReconstructStatus::kError;
    out.error = "unfiltered span out of range";
    return out;
  }

  const auto formula = filter_formula(stages, out.stream_row);
  if (!formula.success || out.selected_byte >= formula.events.size()) {
    out.status = ReconstructStatus::kError;
    out.error = formula.error.empty() ? "filter formula unavailable"
                                      : formula.error;
    return out;
  }
  const std::uint64_t begin =
      out.selected_byte > neighbor_radius ? out.selected_byte - neighbor_radius
                                           : 0;
  std::uint64_t end = 0;
  if (!add(out.selected_byte, neighbor_radius, end) || !add(end, 1, end)) {
    out.status = ReconstructStatus::kError;
    out.error = "neighbor window overflow";
    return out;
  }
  end = std::min<std::uint64_t>(end, formula.events.size());
  out.steps.assign(formula.events.begin() + static_cast<std::size_t>(begin),
                   formula.events.begin() + static_cast<std::size_t>(end));
  out.status = ReconstructStatus::kReady;
  return out;
}

}  // namespace pnga::analysis_engine
