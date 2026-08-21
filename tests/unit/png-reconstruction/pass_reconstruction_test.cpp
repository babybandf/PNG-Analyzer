// WP-303 pass reconstruction tests: Adam7 pass unfiltering + placement into the
// final target coordinates. The test-side encoder and libpng raw oracle live in
// test_png_helpers.h (independent implementations, never production code), so
// libpng's no-transform decode acts as an independent oracle for the
// reconstructed packed target.

#include <pnga/png-reconstruction/pass_reconstruction.h>

#include <pnga/png-reconstruction/scanline_layout.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

#include "test_png_helpers.h"

using namespace pnga_test;  // NOLINT: test helpers are the local vocabulary

using pnga::png_reconstruction::compute_scanline_layout;
using pnga::png_reconstruction::PassReconstructionOutcome;
using pnga::png_reconstruction::reconstruct_image;

// ---------------------------------------------------------------------------
// A: exhaustive geometry — Adam7 round-trip over small sizes, every legal
// color type / bit depth. Uses None filters so the reconstructed target must
// equal the packed source bytes exactly (a placement gap leaves a zero).
// ---------------------------------------------------------------------------

TEST_CASE("Adam7 placement round-trips every legal header at small sizes",
          "[png-reconstruction][wp303]") {
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes = {
      {1, 1}, {1, 2}, {2, 1}, {2, 2}, {3, 5}, {5, 3}, {7, 7},
      {8, 8}, {16, 16}, {17, 13}, {31, 31}, {100, 7}};
  for (const auto& [bd, ct] : kCombos) {
    for (const auto& [w, h] : sizes) {
      CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
      const EncodedPng e = encode_png(w, h, bd, ct, /*interlace=*/true,
                                      /*all_none=*/true);
      const auto layout = compute_scanline_layout(e.header);
      REQUIRE(layout.has_value());
      const PassReconstructionOutcome out =
          reconstruct_image(e.header, *layout, e.filtered);
      REQUIRE(out.success);
      REQUIRE(out.interlace);
      REQUIRE(out.passes.size() == 7);
      REQUIRE(out.target == e.raw);

      // Pass artifacts: byte counts match pass geometry; empty passes empty.
      // total_bytes == unfiltered data bytes + one filter byte per row.
      std::uint64_t placed = 0;
      std::uint64_t filter_bytes = 0;
      for (std::size_t p = 0; p < 7; ++p) {
        const auto& pass = layout->passes[p];
        const auto& art = out.passes[p];
        REQUIRE(art.pass_index == p);
        REQUIRE(art.rows.size() ==
                static_cast<std::size_t>(pass.height * pass.row_bytes));
        placed += static_cast<std::uint64_t>(art.rows.size());
        filter_bytes += pass.height;  // one filter byte per unfiltered row
      }
      REQUIRE(placed + filter_bytes == layout->total_bytes.value_or(0));
    }
  }
}

TEST_CASE("Production Adam7 geometry matches independent spec constants",
          "[png-reconstruction][wp303]") {
  for (std::uint32_t w : {1u, 2u, 3u, 8u, 16u, 17u, 31u, 100u}) {
    for (std::uint32_t h : {1u, 2u, 5u, 8u, 13u, 100u}) {
      CAPTURE(w, h);
      const auto layout =
          compute_scanline_layout(pnga::png_reconstruction::ImageHeader{
              w, h, 8, 6, true});
      REQUIRE(layout.has_value());
      const auto g = test_pass_geometry(w, h);
      for (std::size_t p = 0; p < 7; ++p) {
        CAPTURE(p);
        REQUIRE(layout->passes[p].width == g[p].w);
        REQUIRE(layout->passes[p].height == g[p].h);
        REQUIRE(layout->passes[p].x_start == g[p].xs);
        REQUIRE(layout->passes[p].y_start == g[p].ys);
        REQUIRE(layout->passes[p].x_step == g[p].xstep);
        REQUIRE(layout->passes[p].y_step == g[p].ystep);
      }
      REQUIRE(layout->total_pixels().has_value());
      REQUIRE(*layout->total_pixels() == w * h);
    }
  }
}

// ---------------------------------------------------------------------------
// B: non-interlaced reconstruction with rotating filters (unfiltering through
// the reconstruct entry point, including the degenerate placement path).
// ---------------------------------------------------------------------------

TEST_CASE("Non-interlaced reconstruction round-trips with all filters",
          "[png-reconstruction][wp303]") {
  const std::vector<
      std::tuple<std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t>>
      cases = {
          {8, 8, 8, 6}, {8, 8, 1, 0}, {8, 8, 2, 3}, {8, 8, 4, 0},
          {16, 16, 16, 2}, {7, 9, 8, 4}, {13, 5, 8, 0}, {5, 5, 16, 6}};
  for (const auto& [w, h, bd, ct] : cases) {
    CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
    const EncodedPng e = encode_png(w, h, bd, ct, /*interlace=*/false,
                                    /*all_none=*/false);
    const auto layout = compute_scanline_layout(e.header);
    REQUIRE(layout.has_value());
    const PassReconstructionOutcome out =
        reconstruct_image(e.header, *layout, e.filtered);
    REQUIRE(out.success);
    REQUIRE_FALSE(out.interlace);
    REQUIRE(out.passes.size() == 1);
    REQUIRE(out.target == e.raw);
    REQUIRE(out.passes[0].rows == e.raw);
  }
}

// ---------------------------------------------------------------------------
// C: interlaced reconstruction with all five filters, verified against the
// libpng raw oracle.
// ---------------------------------------------------------------------------

TEST_CASE("Interlaced reconstruction matches libpng with rotating filters",
          "[png-reconstruction][wp303][oracle]") {
  const std::vector<
      std::tuple<std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t>>
      cases = {
          {8, 8, 8, 6}, {16, 12, 8, 2}, {10, 10, 16, 0}, {7, 9, 16, 6},
          {20, 20, 8, 4}, {33, 17, 8, 0}};
  for (const auto& [w, h, bd, ct] : cases) {
    CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
    const EncodedPng e = encode_png(w, h, bd, ct, /*interlace=*/true,
                                    /*all_none=*/false);
    const auto layout = compute_scanline_layout(e.header);
    REQUIRE(layout.has_value());
    const PassReconstructionOutcome out =
        reconstruct_image(e.header, *layout, e.filtered);
    REQUIRE(out.success);

    const RawOracle oracle = raw_decode(e.png_bytes);
    REQUIRE(oracle.ok);
    REQUIRE(oracle.w == w);
    REQUIRE(oracle.h == h);
    REQUIRE(oracle.bd == bd);
    REQUIRE(oracle.ct == ct);
    REQUIRE(oracle.interlace);
    REQUIRE(oracle.rows == out.target);
  }
}

// ---------------------------------------------------------------------------
// D: interlaced sub-byte depths (gray + palette), verified against libpng.
// ---------------------------------------------------------------------------

TEST_CASE("Interlaced sub-byte reconstruction matches libpng",
          "[png-reconstruction][wp303][oracle]") {
  const std::vector<
      std::tuple<std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t>>
      cases = {
          {8, 8, 1, 0}, {8, 8, 2, 0}, {8, 8, 4, 0}, {13, 7, 1, 3},
          {5, 17, 2, 3}, {16, 16, 4, 3}, {9, 9, 4, 0}};
  for (const auto& [w, h, bd, ct] : cases) {
    CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
    const EncodedPng e = encode_png(w, h, bd, ct, /*interlace=*/true,
                                    /*all_none=*/false);
    const auto layout = compute_scanline_layout(e.header);
    REQUIRE(layout.has_value());
    const PassReconstructionOutcome out =
        reconstruct_image(e.header, *layout, e.filtered);
    REQUIRE(out.success);

    const RawOracle oracle = raw_decode(e.png_bytes);
    REQUIRE(oracle.ok);
    REQUIRE(oracle.rows == out.target);
  }
}

// ---------------------------------------------------------------------------
// E: empty passes produce empty artifacts while the composite stays correct.
// ---------------------------------------------------------------------------

TEST_CASE("Empty Adam7 passes yield empty artifacts and a correct target",
          "[png-reconstruction][wp303][oracle]") {
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes = {
      {1, 1}, {2, 1}, {1, 2}, {5, 3}, {8, 8}, {16, 16}, {31, 1}};
  for (const auto& [w, h] : sizes) {
    CAPTURE(w, h);
    const EncodedPng e = encode_png(w, h, 8, 6, /*interlace=*/true,
                                    /*all_none=*/false);
    const auto layout = compute_scanline_layout(e.header);
    REQUIRE(layout.has_value());
    const PassReconstructionOutcome out =
        reconstruct_image(e.header, *layout, e.filtered);
    REQUIRE(out.success);

    // The set of empty passes reported by reconstruction must agree with the
    // independent geometry, and empty passes must yield empty artifacts.
    const auto geom = test_pass_geometry(w, h);
    bool saw_empty = false;
    bool expected_empty = false;
    for (std::size_t p = 0; p < 7; ++p) {
      expected_empty = expected_empty || (geom[p].w == 0 || geom[p].h == 0);
      if (layout->passes[p].height == 0) {
        saw_empty = true;
        REQUIRE(out.passes[p].rows.empty());
      } else {
        REQUIRE_FALSE(out.passes[p].rows.empty());
      }
    }
    REQUIRE(saw_empty == expected_empty);

    const RawOracle oracle = raw_decode(e.png_bytes);
    REQUIRE(oracle.ok);
    REQUIRE(oracle.rows == out.target);
  }
}

// ---------------------------------------------------------------------------
// F: hostile / inconsistent input fails cleanly with a stable error.
// ---------------------------------------------------------------------------

TEST_CASE("Truncated or oversized filtered buffers are rejected",
          "[png-reconstruction][wp303]") {
  const EncodedPng e = encode_png(8, 8, 8, 6, /*interlace=*/true,
                                  /*all_none=*/true);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());

  auto truncated = e.filtered;
  truncated.pop_back();
  const auto out_short = reconstruct_image(e.header, *layout, truncated);
  REQUIRE_FALSE(out_short.success);
  REQUIRE_FALSE(out_short.error.empty());
  REQUIRE(out_short.target.empty());

  auto oversized = e.filtered;
  oversized.push_back(std::byte{0});
  const auto out_long = reconstruct_image(e.header, *layout, oversized);
  REQUIRE_FALSE(out_long.success);
}

TEST_CASE("Header/layout mismatches and invalid headers are rejected",
          "[png-reconstruction][wp303]") {
  const EncodedPng e = encode_png(8, 8, 8, 6, /*interlace=*/false,
                                  /*all_none=*/true);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());

  // Interlace flag disagreement.
  pnga::png_reconstruction::ImageHeader wrong_interlace = e.header;
  wrong_interlace.interlace = true;
  const auto out1 = reconstruct_image(wrong_interlace, *layout, e.filtered);
  REQUIRE_FALSE(out1.success);
  REQUIRE_FALSE(out1.error.empty());

  // Invalid bit depth (3 is not a PNG bit depth).
  pnga::png_reconstruction::ImageHeader bad_depth = e.header;
  bad_depth.bit_depth = 3;
  const auto out2 = reconstruct_image(bad_depth, *layout, e.filtered);
  REQUIRE_FALSE(out2.success);
  REQUIRE_FALSE(out2.error.empty());

  // Zero dimensions.
  pnga::png_reconstruction::ImageHeader zero = e.header;
  zero.width = 0;
  const auto out3 = reconstruct_image(zero, *layout, e.filtered);
  REQUIRE_FALSE(out3.success);
}

TEST_CASE("An invalid filter byte aborts reconstruction",
          "[png-reconstruction][wp303]") {
  const EncodedPng e = encode_png(8, 8, 8, 6, /*interlace=*/true,
                                  /*all_none=*/true);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());
  auto corrupted = e.filtered;
  corrupted[0] = std::byte{0xFF};  // first row's filter byte
  const auto out = reconstruct_image(e.header, *layout, corrupted);
  REQUIRE_FALSE(out.success);
  REQUIRE_FALSE(out.error.empty());
  // The no-partial-result contract holds even for mid-stream failures.
  REQUIRE(out.target.empty());
  REQUIRE(out.passes.empty());
}

// ---------------------------------------------------------------------------
// G: deterministic output.
// ---------------------------------------------------------------------------

TEST_CASE("Reconstruction is deterministic", "[png-reconstruction][wp303]") {
  const EncodedPng e = encode_png(17, 13, 8, 6, /*interlace=*/true,
                                  /*all_none=*/false);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());
  const PassReconstructionOutcome a =
      reconstruct_image(e.header, *layout, e.filtered);
  const PassReconstructionOutcome b =
      reconstruct_image(e.header, *layout, e.filtered);
  REQUIRE(a.success);
  REQUIRE(b.success);
  REQUIRE(a.target == b.target);
  REQUIRE(a.passes == b.passes);
}
