#include "pnga/analysis-engine/stage_pixel_process_view.h"

#include <pnga/png-reconstruction/scanline_layout.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>

namespace pnga::analysis_engine {

namespace {

using pnga::png_reconstruction::PassLayout;
using pnga::png_reconstruction::ScanlineLayout;

bool checked_add(std::uint64_t a, std::uint64_t b,
                 std::uint64_t& out) noexcept {
  if (b > std::numeric_limits<std::uint64_t>::max() - a) {
    return false;
  }
  out = a + b;
  return true;
}

bool checked_mul(std::uint64_t a, std::uint64_t b,
                 std::uint64_t& out) noexcept {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
    return false;
  }
  out = a * b;
  return true;
}

std::uint8_t byte_value(std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

const char* channel_name(std::uint8_t color_type,
                         std::uint8_t channel) noexcept {
  switch (color_type) {
    case 0:
      return "Gray";
    case 2:
      return channel == 0 ? "R" : channel == 1 ? "G" : "B";
    case 3:
      return "Index";
    case 4:
      return channel == 0 ? "Gray" : "Alpha";
    case 6:
      return channel == 0 ? "R" : channel == 1 ? "G" : channel == 2 ? "B" : "A";
    default:
      return "Channel";
  }
}

struct PassCoordinate {
  std::uint64_t pass = 0;
  std::uint64_t local_x = 0;
  std::uint64_t local_y = 0;
  std::uint64_t stream_row = 0;
};

std::optional<PassCoordinate> map_global(const ScanlineLayout& layout,
                                          std::uint64_t x,
                                          std::uint64_t y) {
  std::uint64_t row_cursor = 0;
  for (std::size_t pass_index = 0; pass_index < layout.pass_count;
       ++pass_index) {
    const PassLayout& pass = layout.passes[pass_index];
    if (pass.width == 0 || pass.height == 0 || x < pass.x_start ||
        y < pass.y_start || pass.x_step == 0 || pass.y_step == 0) {
      if (!checked_add(row_cursor, pass.height, row_cursor)) {
        return std::nullopt;
      }
      continue;
    }
    const std::uint64_t dx = x - pass.x_start;
    const std::uint64_t dy = y - pass.y_start;
    if (dx % pass.x_step != 0 || dy % pass.y_step != 0) {
      if (!checked_add(row_cursor, pass.height, row_cursor)) {
        return std::nullopt;
      }
      continue;
    }
    const std::uint64_t local_x = dx / pass.x_step;
    const std::uint64_t local_y = dy / pass.y_step;
    if (local_x >= pass.width || local_y >= pass.height) {
      if (!checked_add(row_cursor, pass.height, row_cursor)) {
        return std::nullopt;
      }
      continue;
    }
    std::uint64_t stream_row = 0;
    if (!checked_add(row_cursor, local_y, stream_row)) {
      return std::nullopt;
    }
    return PassCoordinate{static_cast<std::uint64_t>(pass_index), local_x,
                          local_y, stream_row};
  }
  if (layout.pass_count == 1 && layout.passes[0].width != 0 &&
      layout.passes[0].height != 0) {
    return std::nullopt;
  }
  return std::nullopt;
}

bool pass_row_offset(const PassLayout& pass, std::uint64_t local_y,
                     std::uint64_t& offset) noexcept {
  return checked_mul(local_y, pass.row_bytes, offset);
}

bool byte_position(const PassLayout& pass, std::uint64_t local_x,
                   std::uint8_t channels, std::uint8_t bit_depth,
                   std::uint8_t channel, std::uint64_t& byte_index,
                   std::uint64_t& bit_offset,
                   std::uint64_t& bit_length) noexcept {
  std::uint64_t pixel_bits = 0;
  if (!checked_mul(static_cast<std::uint64_t>(channels), bit_depth,
                   pixel_bits)) {
    return false;
  }
  std::uint64_t pixel_offset = 0;
  if (!checked_mul(local_x, pixel_bits, pixel_offset)) {
    return false;
  }
  std::uint64_t channel_offset = 0;
  if (!checked_mul(channel, bit_depth, channel_offset) ||
      !checked_add(pixel_offset, channel_offset, bit_offset)) {
    return false;
  }
  byte_index = bit_offset / 8;
  bit_offset %= 8;
  bit_length = bit_depth < 8 ? bit_depth : 0;
  return byte_index < pass.row_bytes;
}

void set_error(StagePixelProcessView& out, StagePixelProcessStatus status,
               const char* message) {
  out.status = status;
  out.error = message;
  out.channels.clear();
  out.calculations.clear();
}

}  // namespace

const char* stage_pixel_process_status_text(
    StagePixelProcessStatus status) noexcept {
  switch (status) {
    case StagePixelProcessStatus::kReady:
      return "ready";
    case StagePixelProcessStatus::kNoStage:
      return "no stage data";
    case StagePixelProcessStatus::kOutOfRange:
      return "coordinate out of range";
    case StagePixelProcessStatus::kUnavailable:
      return "stage unavailable";
    case StagePixelProcessStatus::kError:
    default:
      return "stage query error";
  }
}

StagePixelProcessView build_stage_pixel_process_view(
    const StageSet& stages, StagePixelProcessStage stage, std::uint64_t x,
    std::uint64_t y) {
  StagePixelProcessView out;
  out.stage = stage;
  out.image_x = x;
  out.image_y = y;
  if (!stages.success) {
    set_error(out, StagePixelProcessStatus::kNoStage,
              stages.error.empty() ? "no stage data" : stages.error.c_str());
    return out;
  }
  if (x >= stages.header.width || y >= stages.header.height) {
    set_error(out, StagePixelProcessStatus::kOutOfRange,
              "coordinate outside image bounds");
    return out;
  }
  const auto layout = pnga::png_reconstruction::compute_scanline_layout(
      stages.header);
  if (!layout.has_value()) {
    set_error(out, StagePixelProcessStatus::kError, "invalid image layout");
    return out;
  }
  const std::uint8_t channels =
      pnga::png_reconstruction::channels_for_color_type(
          stages.header.color_type);
  if (channels == 0) {
    set_error(out, StagePixelProcessStatus::kError, "invalid source channels");
    return out;
  }
  const auto center = map_global(*layout, x, y);
  if (!center.has_value()) {
    set_error(out, StagePixelProcessStatus::kError,
              "coordinate has no pass mapping");
    return out;
  }
  out.pass_index = center->pass;
  out.pass_local_x = center->local_x;
  out.pass_local_y = center->local_y;
  out.stream_row = center->stream_row;
  if (center->pass >= layout->pass_count ||
      center->stream_row >= stages.scanlines.size() ||
      center->pass >= stages.passes.size()) {
    set_error(out, StagePixelProcessStatus::kUnavailable,
              "scanline mapping unavailable");
    return out;
  }

  const PassLayout& pass = layout->passes[center->pass];
  const std::uint8_t bit_depth = stages.header.bit_depth;
  std::uint64_t stream_row_base = 0;
  for (std::size_t index = 0; index < center->pass; ++index) {
    if (!checked_add(stream_row_base, layout->passes[index].height,
                     stream_row_base)) {
      set_error(out, StagePixelProcessStatus::kError,
                "scanline offset overflow");
      return out;
    }
  }

  const auto make_cell = [&](std::uint64_t local_x,
                             std::uint64_t local_y,
                             std::uint8_t channel,
                             StagePixelProcessCell& cell) -> bool {
    if (stage == StagePixelProcessStage::kNative) {
      if (local_x >= stages.header.width || local_y >= stages.header.height) {
        return true;
      }
      cell.image_x = local_x;
      cell.image_y = local_y;
      cell.in_bounds = true;
      cell.exact_mapping = true;
      std::uint64_t pixel_index = 0;
      if (!checked_mul(local_y, stages.header.width, pixel_index) ||
          !checked_add(pixel_index, local_x, pixel_index) ||
          !checked_mul(pixel_index, channels, pixel_index) ||
          !checked_add(pixel_index, channel, pixel_index) ||
          pixel_index >= stages.native.samples.size()) {
        return false;
      }
      cell.native_samples.push_back(stages.native.samples[
          static_cast<std::size_t>(pixel_index)]);
      const auto full_row_bytes = pnga::png_reconstruction::row_bytes(
          stages.header.width, bit_depth, stages.header.color_type);
      if (!full_row_bytes.has_value()) {
        return false;
      }
      std::uint64_t byte_index = 0;
      std::uint64_t bits = 0;
      std::uint64_t bit_length = 0;
      PassLayout full_pass = pass;
      full_pass.row_bytes = *full_row_bytes;
      if (!byte_position(full_pass, local_x, channels, bit_depth, channel,
                         byte_index, bits, bit_length)) {
        return false;
      }
      std::uint64_t row_offset = 0;
      if (!checked_mul(local_y, *full_row_bytes, row_offset) ||
          !checked_add(row_offset, byte_index, cell.byte_offset) ||
          cell.byte_offset >= stages.unfiltered.size()) {
        return false;
      }
      cell.bit_offset = bits;
      cell.bit_length = bit_length;
      const std::size_t lane_count = bit_depth == 16 ? 2 : 1;
      if (cell.byte_offset > stages.unfiltered.size() ||
          lane_count > stages.unfiltered.size() -
                           static_cast<std::size_t>(cell.byte_offset)) {
        return false;
      }
      for (std::size_t lane = 0; lane < lane_count; ++lane) {
        std::uint64_t lane_offset = 0;
        if (!checked_add(cell.byte_offset, lane, lane_offset)) {
          return false;
        }
        cell.bytes.push_back(byte_value(
            stages.unfiltered[static_cast<std::size_t>(lane_offset)]));
      }
      return true;
    }

    if (local_x >= pass.width || local_y >= pass.height) {
      return true;
    }
    std::uint64_t image_x = 0;
    std::uint64_t image_y = 0;
    std::uint64_t delta = 0;
    if (!checked_mul(local_x, pass.x_step, delta) ||
        !checked_add(pass.x_start, delta, image_x) ||
        !checked_mul(local_y, pass.y_step, delta) ||
        !checked_add(pass.y_start, delta, image_y)) {
      return false;
    }
    cell.image_x = image_x;
    cell.image_y = image_y;
    cell.pass_local_x = local_x;
    cell.pass_local_y = local_y;
    cell.pass_index = center->pass;
    cell.in_bounds = image_x < stages.header.width &&
                     image_y < stages.header.height;
    cell.exact_mapping = cell.in_bounds;
    if (!cell.in_bounds) {
      return true;
    }
    std::uint64_t row_offset = 0;
    std::uint64_t byte_index = 0;
    std::uint64_t bits = 0;
    std::uint64_t bit_length = 0;
    if (!pass_row_offset(pass, local_y, row_offset) ||
        !byte_position(pass, local_x, channels, bit_depth, channel,
                       byte_index, bits, bit_length) ||
        !checked_add(row_offset, byte_index, row_offset)) {
      return false;
    }
    cell.byte_offset = row_offset;
    cell.bit_offset = bits;
    cell.bit_length = bit_length;
    if (!checked_add(stream_row_base, local_y, cell.stream_row)) {
      return false;
    }
    cell.packed_shared = bit_depth < 8;
    const std::vector<std::byte>* bytes = nullptr;
    std::uint64_t base = 0;
    if (stage == StagePixelProcessStage::kFiltered) {
      if (cell.stream_row >= stages.scanlines.size()) {
        return false;
      }
      const auto& span = stages.scanlines[static_cast<std::size_t>(cell.stream_row)];
      if (span.length == 0 || span.offset > stages.filtered.size() ||
          span.length > stages.filtered.size() - span.offset ||
          !checked_add(span.offset, 1, base) ||
          !checked_add(base, byte_index, base) ||
          base >= stages.filtered.size()) {
        return false;
      }
      cell.byte_offset = base;
      bytes = &stages.filtered;
    } else {
      const auto& pass_rows = stages.passes[static_cast<std::size_t>(center->pass)].rows;
      if (base > pass_rows.size() || row_offset > pass_rows.size() - base) {
        return false;
      }
      bytes = &pass_rows;
    }
    const std::size_t lane_count = bit_depth == 16 ? 2 : 1;
    if (cell.byte_offset > bytes->size() ||
        lane_count > bytes->size() - static_cast<std::size_t>(cell.byte_offset)) {
      return false;
    }
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
      std::uint64_t lane_offset = 0;
      if (!checked_add(cell.byte_offset, lane, lane_offset)) {
        return false;
      }
      cell.bytes.push_back(byte_value(
          (*bytes)[static_cast<std::size_t>(lane_offset)]));
    }
    return true;
  };

  if (stage != StagePixelProcessStage::kNative) {
    const auto& span = stages.scanlines[static_cast<std::size_t>(center->stream_row)];
    if (span.length == 0 || span.offset >= stages.filtered.size() ||
        span.length > stages.filtered.size() - span.offset) {
      set_error(out, StagePixelProcessStatus::kUnavailable,
                "filtered scanline unavailable");
      return out;
    }
    out.filter_byte_offset = span.offset;
    out.filter_byte = byte_value(stages.filtered[static_cast<std::size_t>(span.offset)]);
    if (!pnga::png_reconstruction::is_valid_filter_type(out.filter_byte)) {
      set_error(out, StagePixelProcessStatus::kError, "invalid filter byte");
      return out;
    }
    out.filter = static_cast<pnga::png_reconstruction::FilterType>(out.filter_byte);
  }

  out.channels.reserve(channels);
  for (std::uint8_t channel = 0; channel < channels; ++channel) {
    StagePixelProcessChannel result_channel;
    result_channel.source_index = channel;
    result_channel.name = channel_name(stages.header.color_type, channel);
    result_channel.cells.reserve(15);
    for (int row = -1; row <= 1; ++row) {
      for (int column = -2; column <= 2; ++column) {
        StagePixelProcessCell cell;
        cell.current = row == 0 && column == 0;
        if (stage == StagePixelProcessStage::kNative) {
          const auto global_x = static_cast<std::int64_t>(x) + column;
          const auto global_y = static_cast<std::int64_t>(y) + row;
          if (global_x >= 0 && global_y >= 0) {
            if (!make_cell(static_cast<std::uint64_t>(global_x),
                           static_cast<std::uint64_t>(global_y), channel,
                           cell)) {
              set_error(out, StagePixelProcessStatus::kError,
                        "native sample mapping overflow");
              return out;
            }
          }
        } else {
          const auto local_x = static_cast<std::int64_t>(center->local_x) + column;
          const auto local_y = static_cast<std::int64_t>(center->local_y) + row;
          if (local_x >= 0 && local_y >= 0 &&
              !make_cell(static_cast<std::uint64_t>(local_x),
                         static_cast<std::uint64_t>(local_y), channel, cell)) {
            set_error(out, StagePixelProcessStatus::kError,
                      "stage byte mapping overflow");
            return out;
          }
        }
        result_channel.cells.push_back(std::move(cell));
      }
    }
    out.channels.push_back(std::move(result_channel));
  }

  if (stage == StagePixelProcessStage::kDefiltered) {
    const FilterFormula formula = filter_formula(stages, center->stream_row);
    if (formula.success) {
      for (std::uint8_t channel = 0; channel < channels; ++channel) {
        std::uint64_t byte_index = 0;
        std::uint64_t bit_offset = 0;
        std::uint64_t bit_length = 0;
        if (!byte_position(pass, center->local_x, channels, bit_depth,
                           channel, byte_index, bit_offset, bit_length)) {
          continue;
        }
        const std::size_t lane_count = bit_depth == 16 ? 2 : 1;
        for (std::size_t lane = 0; lane < lane_count; ++lane) {
          std::uint64_t event_index = 0;
          if (!checked_add(byte_index, lane, event_index)) {
            continue;
          }
          const auto event = std::find_if(
              formula.events.begin(), formula.events.end(),
              [event_index](const auto& candidate) {
                return candidate.index == event_index;
              });
          if (event == formula.events.end()) {
            continue;
          }
          StagePixelProcessCalculation calculation;
          calculation.channel_index = channel;
          calculation.lane = static_cast<std::uint8_t>(lane);
          calculation.byte_offset = event_index;
          calculation.has_raw = calculation.has_a = calculation.has_b =
              calculation.has_c = calculation.has_predictor =
                  calculation.has_recon = true;
          calculation.raw = event->raw;
          calculation.a = event->a;
          calculation.b = event->b;
          calculation.c = event->c;
          calculation.predictor = event->predictor;
          calculation.recon = event->recon;
          calculation.boundary_zero_a = event->a == 0 && event->index <
              pnga::png_reconstruction::filter_bpp(bit_depth,
                                                   stages.header.color_type)
                  .value_or(0);
          calculation.boundary_zero_b = center->local_y == 0;
          calculation.boundary_zero_c = calculation.boundary_zero_b;
          out.calculations.push_back(calculation);
        }
      }
    }
  }

  out.status = StagePixelProcessStatus::kReady;
  return out;
}

}  // namespace pnga::analysis_engine
