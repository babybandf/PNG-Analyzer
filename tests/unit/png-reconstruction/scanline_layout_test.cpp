// WP-300 scanline layout tests: channels, packed row bytes, filter bpp,
// Adam7 pass geometry (sum-of-areas invariant), empty passes and overflow
// safety for hostile dimensions.

#include <pnga/png-reconstruction/scanline_layout.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <optional>

using pnga::png_reconstruction::channels_for_color_type;
using pnga::png_reconstruction::compute_scanline_layout;
using pnga::png_reconstruction::filter_bpp;
using pnga::png_reconstruction::ImageHeader;
using pnga::png_reconstruction::row_bytes;
using pnga::png_reconstruction::ScanlineLayout;

TEST_CASE("Color type maps to the spec channel count", "[png-recon][wp300]") {
  REQUIRE(channels_for_color_type(0) == 1);  // gray
  REQUIRE(channels_for_color_type(2) == 3);  // RGB
  REQUIRE(channels_for_color_type(3) == 1);  // palette
  REQUIRE(channels_for_color_type(4) == 2);  // gray+alpha
  REQUIRE(channels_for_color_type(6) == 4);  // RGBA
  REQUIRE(channels_for_color_type(1) == 0);
  REQUIRE(channels_for_color_type(5) == 0);
  REQUIRE(channels_for_color_type(7) == 0);
}

TEST_CASE("Packed row bytes match the spec formula", "[png-recon][wp300]") {
  REQUIRE(row_bytes(3, 8, 6) == 12);   // RGBA8, 3 px
  REQUIRE(row_bytes(8, 1, 0) == 1);    // gray 1-bit, 8 px
  REQUIRE(row_bytes(8, 2, 0) == 2);    // gray 2-bit, 8 px
  REQUIRE(row_bytes(8, 4, 3) == 4);    // palette 4-bit, 8 px
  REQUIRE(row_bytes(3, 16, 2) == 18);  // RGB 16-bit, 3 px
  REQUIRE(row_bytes(5, 8, 0) == 5);    // gray 8-bit, 5 px
  // Invalid combinations.
  REQUIRE_FALSE(row_bytes(3, 3, 6).has_value());   // bad bit depth
  REQUIRE_FALSE(row_bytes(3, 8, 5).has_value());   // bad color type
  REQUIRE(row_bytes(0, 8, 6) == 0);                // zero width -> zero bytes
}

TEST_CASE("Filter bpp is max(1, ceil(bits_per_pixel / 8))", "[png-recon][wp300]") {
  REQUIRE(filter_bpp(1, 0) == 1);
  REQUIRE(filter_bpp(2, 0) == 1);
  REQUIRE(filter_bpp(4, 0) == 1);
  REQUIRE(filter_bpp(8, 0) == 1);
  REQUIRE(filter_bpp(16, 0) == 2);  // gray 16-bit
  REQUIRE(filter_bpp(8, 2) == 3);   // RGB
  REQUIRE(filter_bpp(8, 6) == 4);   // RGBA
  REQUIRE(filter_bpp(4, 3) == 1);   // palette
  REQUIRE_FALSE(filter_bpp(3, 0).has_value());
}

TEST_CASE("Non-interlaced layout uses a single full-size pass", "[png-recon][wp300]") {
  const auto layout =
      compute_scanline_layout(ImageHeader{4, 4, 8, 6, false});
  REQUIRE(layout.has_value());
  REQUIRE_FALSE(layout->interlace);
  REQUIRE(layout->pass_count == 1);
  REQUIRE(layout->passes[0].width == 4);
  REQUIRE(layout->passes[0].height == 4);
  REQUIRE(layout->passes[0].row_bytes == 16);
  REQUIRE(layout->passes[0].filter_row_bytes == 17);
  REQUIRE(layout->passes[0].total_bytes == 4 * 17);
  REQUIRE(layout->total_bytes == 68);
  REQUIRE(layout->total_pixels() == 16);
}

TEST_CASE("Adam7 pass areas sum to the full image area", "[png-recon][wp300]") {
  for (std::uint32_t w : {1u, 2u, 3u, 7u, 8u, 13u, 16u, 100u}) {
    for (std::uint32_t h : {1u, 2u, 5u, 8u, 16u, 64u}) {
      const auto layout =
          compute_scanline_layout(ImageHeader{w, h, 8, 2, true});
      REQUIRE(layout.has_value());
      REQUIRE(layout->interlace);
      REQUIRE(layout->pass_count == 7);
      const auto total_pixels = layout->total_pixels();
      REQUIRE(total_pixels.has_value());
      REQUIRE(*total_pixels == static_cast<std::uint64_t>(w) * h);
    }
  }
}

TEST_CASE("Small images produce empty Adam7 passes", "[png-recon][wp300]") {
  // 1x1 interlaced: only passes whose starting coordinates fall inside.
  const auto layout = compute_scanline_layout(ImageHeader{1, 1, 8, 0, true});
  REQUIRE(layout.has_value());
  REQUIRE(layout->passes[0].width == 1);  // pass 1 (x0,y0)
  REQUIRE(layout->passes[0].height == 1);
  std::uint64_t non_empty = 0;
  for (std::size_t p = 0; p < 7; ++p) {
    if (layout->passes[p].width != 0 && layout->passes[p].height != 0) {
      ++non_empty;
    }
  }
  REQUIRE(non_empty == 1);
  REQUIRE(layout->total_bytes == 1 * (1 + 1));  // 1 filter + 1 data byte
}

TEST_CASE("Huge dimensions overflow to nullopt instead of wrapping",
          "[png-recon][wp300]") {
  // 2^31 width, RGBA 16-bit: row bytes alone exceed 2^35; total inflate size
  // overflows uint64 for a tall image.
  const auto layout = compute_scanline_layout(
      ImageHeader{0x80000000u, 0x80000000u, 16, 6, false});
  REQUIRE_FALSE(layout.has_value());
}

TEST_CASE("Zero width or height is rejected", "[png-recon][wp300]") {
  REQUIRE_FALSE(compute_scanline_layout(ImageHeader{0, 4, 8, 6, false}).has_value());
  REQUIRE_FALSE(compute_scanline_layout(ImageHeader{4, 0, 8, 6, false}).has_value());
}

TEST_CASE("All legal bit depth / color type combinations compute",
          "[png-recon][wp300]") {
  const std::uint8_t depths[] = {1, 2, 4, 8, 16};
  const std::uint8_t types[] = {0, 2, 3, 4, 6};
  for (std::uint8_t d : depths) {
    for (std::uint8_t t : types) {
      const auto layout =
          compute_scanline_layout(ImageHeader{3, 3, d, t, false});
      REQUIRE(layout.has_value());
      REQUIRE(layout->total_bytes.has_value());
      REQUIRE(*layout->total_bytes == 3 * layout->passes[0].filter_row_bytes);
    }
  }
}
