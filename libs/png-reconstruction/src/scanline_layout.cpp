// WP-300 scanline layout implementation. All arithmetic is checked with the
// invariant-form helpers below; nothing wraps on hostile or extreme input.

#include "pnga/png-reconstruction/scanline_layout.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace pnga::png_reconstruction {

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

// Checked helpers: return nullopt on overflow.
std::optional<std::uint64_t> mul(std::uint64_t a, std::uint64_t b) noexcept {
  if (a != 0 && b > kMax / a) {
    return std::nullopt;
  }
  return a * b;
}

std::optional<std::uint64_t> add(std::uint64_t a, std::uint64_t b) noexcept {
  if (a > kMax - b) {
    return std::nullopt;
  }
  return a + b;
}

std::optional<std::uint64_t> ceil_div(std::uint64_t value,
                                      std::uint64_t div) noexcept {
  if (value == 0) {
    return std::uint64_t{0};
  }
  if (value > kMax - (div - 1)) {
    return std::nullopt;
  }
  return (value + div - 1) / div;
}

bool valid_bit_depth(std::uint8_t bit_depth) noexcept {
  return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
         bit_depth == 8 || bit_depth == 16;
}

bool valid_color_type(std::uint8_t color_type) noexcept {
  return color_type == 0 || color_type == 2 || color_type == 3 ||
         color_type == 4 || color_type == 6;
}

// Adam7 pass starting coordinates and steps (PNG spec §8.5).
constexpr std::array<std::array<std::uint64_t, 4>, kAdam7PassCount>
    kAdam7StartStep = {{
        {0, 0, 8, 8},   // pass 1
        {4, 0, 8, 8},   // pass 2
        {0, 4, 4, 8},   // pass 3
        {2, 0, 4, 4},   // pass 4
        {0, 2, 2, 4},   // pass 5
        {1, 0, 2, 2},   // pass 6
        {0, 1, 1, 2},   // pass 7
    }};

std::optional<std::uint64_t> pass_extent(std::uint64_t full,
                                         std::uint64_t start,
                                         std::uint64_t step) noexcept {
  if (full <= start) {
    return std::uint64_t{0};
  }
  return ceil_div(full - start, step);
}

}  // namespace

std::uint8_t channels_for_color_type(std::uint8_t color_type) noexcept {
  switch (color_type) {
    case 0:  // gray
    case 3:  // palette
      return 1;
    case 2:  // RGB
      return 3;
    case 4:  // gray + alpha
      return 2;
    case 6:  // RGBA
      return 4;
    default:
      return 0;
  }
}

std::optional<std::uint64_t> row_bytes(std::uint32_t width,
                                       std::uint8_t bit_depth,
                                       std::uint8_t color_type) noexcept {
  if (!valid_bit_depth(bit_depth) || !valid_color_type(color_type)) {
    return std::nullopt;
  }
  const std::uint64_t channels = channels_for_color_type(color_type);
  const auto per_pixel =
      mul(channels, static_cast<std::uint64_t>(bit_depth));
  if (!per_pixel.has_value()) {
    return std::nullopt;
  }
  const auto bits =
      mul(static_cast<std::uint64_t>(width), *per_pixel);
  if (!bits.has_value()) {
    return std::nullopt;
  }
  return ceil_div(*bits, 8);
}

std::optional<std::uint64_t> filter_bpp(std::uint8_t bit_depth,
                                        std::uint8_t color_type) noexcept {
  if (!valid_bit_depth(bit_depth) || !valid_color_type(color_type)) {
    return std::nullopt;
  }
  const std::uint64_t channels = channels_for_color_type(color_type);
  const auto bits = mul(channels, static_cast<std::uint64_t>(bit_depth));
  if (!bits.has_value()) {
    return std::nullopt;
  }
  const auto bpp = ceil_div(*bits, 8);
  if (!bpp.has_value()) {
    return std::nullopt;
  }
  return std::max<std::uint64_t>(1, *bpp);
}

std::optional<ScanlineLayout> compute_scanline_layout(
    const ImageHeader& header) noexcept {
  if (!valid_bit_depth(header.bit_depth) ||
      !valid_color_type(header.color_type) || header.width == 0 ||
      header.height == 0) {
    return std::nullopt;
  }

  ScanlineLayout layout;
  layout.interlace = header.interlace;
  layout.pass_count = header.interlace ? kAdam7PassCount : 1;

  std::optional<std::uint64_t> total = std::uint64_t{0};
  for (std::size_t p = 0; p < layout.pass_count; ++p) {
    PassLayout& pass = layout.passes[p];
    if (header.interlace) {
      pass.x_start = kAdam7StartStep[p][0];
      pass.y_start = kAdam7StartStep[p][1];
      pass.x_step = kAdam7StartStep[p][2];
      pass.y_step = kAdam7StartStep[p][3];
      auto w = pass_extent(header.width, pass.x_start, pass.x_step);
      auto h = pass_extent(header.height, pass.y_start, pass.y_step);
      if (!w.has_value() || !h.has_value()) {
        return std::nullopt;
      }
      pass.width = *w;
      pass.height = *h;
    } else {
      pass.width = header.width;
      pass.height = header.height;
      pass.x_step = 1;
      pass.y_step = 1;
    }

    if (pass.width == 0 || pass.height == 0) {
      // Empty Adam7 pass: normalize both dimensions so consumers iterating
      // rows never see a pass with a nonzero row count but zero width.
      pass.width = 0;
      pass.height = 0;
      continue;
    }

    const auto rb = row_bytes(static_cast<std::uint32_t>(pass.width),
                              header.bit_depth, header.color_type);
    if (!rb.has_value()) {
      return std::nullopt;
    }
    pass.row_bytes = *rb;
    const auto frb = add(*rb, 1);  // filter byte prefix
    if (!frb.has_value()) {
      return std::nullopt;
    }
    pass.filter_row_bytes = *frb;
    const auto pass_total = mul(pass.height, *frb);
    if (!pass_total.has_value()) {
      return std::nullopt;
    }
    pass.total_bytes = *pass_total;
    total = add(*total, *pass_total);
    if (!total.has_value()) {
      return std::nullopt;
    }
  }
  layout.total_bytes = total;
  return layout;
}

std::optional<std::uint64_t> ScanlineLayout::total_pixels() const noexcept {
  std::optional<std::uint64_t> total = std::uint64_t{0};
  for (std::size_t p = 0; p < pass_count; ++p) {
    const auto area =
        mul(passes[p].width, passes[p].height);
    if (!area.has_value()) {
      return std::nullopt;
    }
    total = add(*total, *area);
    if (!total.has_value()) {
      return std::nullopt;
    }
  }
  return total;
}

}  // namespace pnga::png_reconstruction
