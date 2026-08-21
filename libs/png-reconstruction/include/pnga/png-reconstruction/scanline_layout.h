#ifndef PNGA_PNG_RECONSTRUCTION_SCANLINE_LAYOUT_H
#define PNGA_PNG_RECONSTRUCTION_SCANLINE_LAYOUT_H

// WP-300: safe scanline layout computation (REPOSITORY_LAYOUT.md §5.8).
// Maps IHDR fields to per-pass geometry, packed row bytes and expected
// inflated size. Every product uses checked arithmetic: invalid color
// type / bit depth combinations and overflowing sizes are reported as
// nullopt, never wrapped.

#include <array>
#include <cstdint>
#include <optional>

namespace pnga::png_reconstruction {

// Minimal IHDR-derived header used by the reconstruction pipeline.
struct ImageHeader {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint8_t bit_depth = 0;   // 1, 2, 4, 8 or 16
  std::uint8_t color_type = 0;  // 0, 2, 3, 4 or 6
  bool interlace = false;       // false = none, true = Adam7
};

// Number of channels for a PNG color type (spec §4.1.2):
//   0 gray=1, 2 RGB=3, 3 palette=1, 4 gray+alpha=2, 6 RGBA=4.
// Returns 0 for any other value.
std::uint8_t channels_for_color_type(std::uint8_t color_type) noexcept;

// Packed bytes per row: ceil(width * channels * bit_depth / 8). Returns
// nullopt for an invalid color type / bit depth or when the product overflows.
std::optional<std::uint64_t> row_bytes(std::uint32_t width,
                                       std::uint8_t bit_depth,
                                       std::uint8_t color_type) noexcept;

// Reverse-filter bytes-per-pixel: max(1, ceil(bits_per_pixel / 8)).
std::optional<std::uint64_t> filter_bpp(std::uint8_t bit_depth,
                                        std::uint8_t color_type) noexcept;

// One Adam7 pass's geometry and layout (passes with zero size are valid).
struct PassLayout {
  std::uint64_t width = 0;
  std::uint64_t height = 0;
  std::uint64_t x_start = 0;
  std::uint64_t y_start = 0;
  std::uint64_t x_step = 0;
  std::uint64_t y_step = 0;
  std::uint64_t row_bytes = 0;         // packed data bytes per row
  std::uint64_t filter_row_bytes = 0;  // 1 filter byte + row_bytes
  std::uint64_t total_bytes = 0;       // height * filter_row_bytes (checked)
};

constexpr std::size_t kAdam7PassCount = 7;

// Complete scanline layout for an ImageHeader. For non-interlaced images only
// passes[0] is meaningful and pass_count == 1; for Adam7 pass_count == 7.
struct ScanlineLayout {
  bool interlace = false;
  std::uint8_t pass_count = 0;
  std::array<PassLayout, kAdam7PassCount> passes{};
  std::optional<std::uint64_t> total_bytes;  // sum over passes, checked

  // Total pixels across all passes (== width*height for Adam7).
  std::optional<std::uint64_t> total_pixels() const noexcept;
};

// Computes the layout, or std::nullopt when the header is invalid or any
// product overflows.
std::optional<ScanlineLayout> compute_scanline_layout(
    const ImageHeader& header) noexcept;

}  // namespace pnga::png_reconstruction

#endif  // PNGA_PNG_RECONSTRUCTION_SCANLINE_LAYOUT_H
