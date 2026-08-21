// WP-305 differential harness tests: the Trace reconstruction pipeline vs the
// libpng raw oracle over a generated corpus batch, plus fault injection that
// proves the first-difference reporter locates a planted divergence at the
// exact (row, x, channel), and malformed-input handling.

#include "differential_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>

using namespace pnga_test;  // NOLINT: test helpers are the local vocabulary

using pnga::differential::compare_png;
using pnga::differential::compare_pngs;
using pnga::differential::DifferentialResult;

// ---------------------------------------------------------------------------
// Corpus batch: every legal header x a few sizes x interlace x filter modes
// must agree byte-for-byte between Trace and libpng.
// ---------------------------------------------------------------------------

TEST_CASE("Differential corpus: Trace matches libpng across the legal matrix",
          "[wp305][oracle]") {
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes = {
      {1, 1}, {3, 5}, {8, 8}, {16, 12}, {31, 17}};
  std::size_t count = 0;
  for (const auto& [bd, ct] : kCombos) {
    for (const auto& [w, h] : sizes) {
      for (const bool interlace : {false, true}) {
        for (const bool filters : {false, true}) {
          CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct),
                  interlace, filters);
          const EncodedPng e = encode_png(w, h, bd, ct, interlace, filters);
          const DifferentialResult r = compare_png(e.png_bytes);
          INFO("error: " << r.error);
          REQUIRE(r.ok);
          REQUIRE(r.dimensions_match);
          REQUIRE(r.target_matches);
          REQUIRE(r.native_matches);
          REQUIRE_FALSE(r.first.found);
          ++count;
        }
      }
    }
  }
  REQUIRE(count > 100);
}

// ---------------------------------------------------------------------------
// Fault injection 1: a single packed byte changed in one side's file. The
// harness must report the first native-sample difference at exactly that
// sample with both values.
// ---------------------------------------------------------------------------

TEST_CASE("Differential locates a planted single-sample change",
          "[wp305][oracle]") {
  const std::uint32_t w = 8;
  const std::uint32_t h = 8;
  const std::vector<std::byte> r1 = make_raw_image(w, h, 8, 6, /*seed=*/5);
  std::vector<std::byte> r2 = r1;
  const std::size_t rb = static_cast<std::size_t>(test_row_bytes(w, 8, 6));  // 32
  // Sample (row 2, x 3, channel 1) -> packed byte 2*32 + 3*4 + 1 = 77.
  const std::size_t offset = 2 * rb + (3 * 4 + 1);
  r2[offset] = static_cast<std::byte>(
      static_cast<std::uint8_t>(static_cast<unsigned>(r2[offset]) ^ 0xFFu));

  const EncodedPng a = encode_png_raw(r1, w, h, 8, 6, false, true);
  const EncodedPng b = encode_png_raw(r2, w, h, 8, 6, false, true);
  const DifferentialResult r = compare_pngs(a.png_bytes, b.png_bytes);

  REQUIRE_FALSE(r.ok);
  REQUIRE(r.dimensions_match);
  // The packed targets differ exactly where the byte changed (Trace decoded A,
  // libpng decoded B), so target_matches is legitimately false here.
  REQUIRE_FALSE(r.target_matches);
  REQUIRE_FALSE(r.native_matches);
  REQUIRE(r.first.found);
  REQUIRE(r.first.row == 2);
  REQUIRE(r.first.x == 3);
  REQUIRE(r.first.channel == 1);
  REQUIRE(r.first.trace_value ==
          static_cast<std::uint8_t>(r1[offset]));
  REQUIRE(r.first.libpng_value ==
          static_cast<std::uint8_t>(r2[offset]));
}

// ---------------------------------------------------------------------------
// Fault injection 2: a filter byte changed (None -> Sub) on one row. The first
// affected byte is the first byte after bpp (=4 for RGBA8) of that row, i.e.
// sample (row 5, x 1, channel 0), with the Sub reconstruction as expected.
// ---------------------------------------------------------------------------

TEST_CASE("Differential locates a planted filter-byte change",
          "[wp305][oracle]") {
  const std::uint32_t w = 8;
  const std::uint32_t h = 8;
  const std::size_t rb = static_cast<std::size_t>(test_row_bytes(w, 8, 6));  // 32
  const EncodedPng a = encode_png(w, h, 8, 6, false, true);

  std::vector<std::byte> filtered = a.filtered;
  const std::size_t row_offset = 5 * (1 + rb);  // 5 * 33
  filtered[row_offset] = std::byte{1};          // None -> Sub

  const std::vector<std::byte> b = build_png_file(w, h, 8, 6, false, filtered);
  const DifferentialResult r = compare_pngs(a.png_bytes, b);

  REQUIRE_FALSE(r.ok);
  REQUIRE(r.first.found);
  REQUIRE(r.first.row == 5);
  REQUIRE(r.first.x == 1);  // first byte after bpp=4 of the row
  REQUIRE(r.first.channel == 0);
  // Trace decoded the original (None) row; libpng decoded the Sub row.
  REQUIRE(r.first.trace_value == static_cast<std::uint8_t>(a.raw[5 * rb + 4]));
  REQUIRE(r.first.libpng_value ==
          static_cast<std::uint8_t>(
              static_cast<unsigned>(a.raw[5 * rb + 4]) +
              static_cast<unsigned>(a.raw[5 * rb + 0])));
}

// ---------------------------------------------------------------------------
// Malformed input: neither side may crash; the harness reports a clean error.
// ---------------------------------------------------------------------------

TEST_CASE("Differential handles malformed input cleanly", "[wp305][oracle]") {
  // Not a PNG at all.
  const std::vector<std::byte> garbage(64, std::byte{0xAB});
  const DifferentialResult r1 = compare_png(garbage);
  REQUIRE_FALSE(r1.ok);
  REQUIRE_FALSE(r1.error.empty());

  // Valid file truncated inside the compressed IDAT payload.
  const EncodedPng e = encode_png(8, 8, 8, 6, true, false);
  std::vector<std::byte> truncated(e.png_bytes.begin(),
                                   e.png_bytes.begin() +
                                       static_cast<std::ptrdiff_t>(e.png_bytes.size() / 2));
  const DifferentialResult r2 = compare_png(truncated);
  REQUIRE_FALSE(r2.ok);
  REQUIRE_FALSE(r2.error.empty());
}
