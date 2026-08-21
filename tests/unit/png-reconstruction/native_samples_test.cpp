// WP-304 native sample extraction tests: 1/2/4-bit unpack (padding ignored),
// 8/16-bit big-endian samples, every legal (bit_depth, color_type) combo and
// an end-to-end round trip through the WP-303 reconstruct pipeline.

#include <pnga/png-reconstruction/native_samples.h>

#include <pnga/png-reconstruction/pass_reconstruction.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

#include "test_png_helpers.h"

using namespace pnga_test;  // NOLINT: test helpers are the local vocabulary

using pnga::png_reconstruction::compute_scanline_layout;
using pnga::png_reconstruction::extract_native_samples;
using pnga::png_reconstruction::ImageHeader;
using pnga::png_reconstruction::NativeSamplesOutcome;
using pnga::png_reconstruction::reconstruct_image;

namespace {

// Independent native extraction of a packed image (mirrors production bit
// order via the test's own bit reader). Used to cross-check the combinatorial
// sweep; the hand-crafted padding/endianness cases below break the symmetry.
std::vector<std::uint16_t> expected_native(const std::vector<std::byte>& raw,
                                           std::uint32_t w, std::uint32_t h,
                                           std::uint8_t bd, std::uint8_t ct) {
  const unsigned ch = channels_of(ct);
  const std::uint64_t rb = test_row_bytes(w, bd, ct);
  std::vector<std::uint16_t> out;
  out.reserve(static_cast<std::size_t>(w) * h * ch);
  for (std::uint32_t y = 0; y < h; ++y) {
    const std::byte* row = raw.data() + static_cast<std::size_t>(y) * rb;
    for (std::uint32_t x = 0; x < w; ++x) {
      for (unsigned c = 0; c < ch; ++c) {
        if (bd < 8) {
          out.push_back(test_read_bits(row, (x * ch + c) * bd, bd));
        } else {
          std::uint16_t v = 0;
          for (unsigned k = 0; k < bd / 8; ++k) {
            v = static_cast<std::uint16_t>(
                (v << 8) |
                static_cast<unsigned char>(
                    row[(x * ch + c) * (bd / 8) + k]));
          }
          out.push_back(v);
        }
      }
    }
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// A: every legal (bit_depth, color_type) at several sizes, including packed
// sub-byte rows and 16-bit big-endian samples.
// ---------------------------------------------------------------------------

TEST_CASE("Native extraction covers every legal header",
          "[png-reconstruction][wp304]") {
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes = {
      {1, 1}, {2, 3}, {7, 5}, {8, 8}, {13, 7}, {16, 16}};
  for (const auto& [bd, ct] : kCombos) {
    for (const auto& [w, h] : sizes) {
      CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
      const std::vector<std::byte> raw = make_raw_image(w, h, bd, ct, /*seed=*/3);
      const NativeSamplesOutcome out = extract_native_samples(
          ImageHeader{w, h, bd, ct, false}, raw);
      REQUIRE(out.success);
      REQUIRE(out.image.width == w);
      REQUIRE(out.image.height == h);
      REQUIRE(out.image.bit_depth == bd);
      REQUIRE(out.image.color_type == ct);
      REQUIRE(out.image.channels == channels_of(ct));
      REQUIRE(out.image.samples == expected_native(raw, w, h, bd, ct));
    }
  }
}

// ---------------------------------------------------------------------------
// B: row padding bits are ignored for 1/2/4-bit images.
// ---------------------------------------------------------------------------

TEST_CASE("Packed row padding bits are ignored", "[png-reconstruction][wp304]") {
  {
    // Gray 1-bit, 7 samples/row -> 1 byte, 1 padding bit set to 1.
    const std::uint32_t w = 7;
    const std::uint32_t h = 2;
    std::vector<std::byte> raw(2, std::byte{0});
    for (std::uint32_t x = 0; x < w; ++x) {
      test_write_bits(raw.data(), x * 1, 1,
                      static_cast<std::uint8_t>(1 - (x % 2)));  // 1,0,1,0,1,0,1
    }
    test_write_bits(raw.data() + 1, 0, 7, 0b0101010);
    // Row 0: 1010101 + padding=1 -> 0xAB; row 1: 0101010 + padding=1 -> 0x55.
    test_write_bits(raw.data(), 7, 1, 1);
    test_write_bits(raw.data() + 1, 7, 1, 1);
    REQUIRE(raw[0] == std::byte{0xAB});
    REQUIRE(raw[1] == std::byte{0x55});
    const NativeSamplesOutcome out =
        extract_native_samples(ImageHeader{w, h, 1, 0, false}, raw);
    REQUIRE(out.success);
    REQUIRE(out.image.samples ==
            std::vector<std::uint16_t>({1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}));
  }
  {
    // Gray 2-bit, 3 samples/row -> 6 bits, 2 padding bits set to 11.
    const std::uint32_t w = 3;
    const std::uint32_t h = 1;
    std::vector<std::byte> raw(1, std::byte{0});
    for (std::uint32_t x = 0; x < w; ++x) {
      test_write_bits(raw.data(), x * 2, 2, static_cast<std::uint8_t>(x + 1));
    }
    test_write_bits(raw.data(), 6, 2, 0b11);  // padding
    REQUIRE(raw[0] == std::byte{0x6F});
    const NativeSamplesOutcome out =
        extract_native_samples(ImageHeader{w, h, 2, 0, false}, raw);
    REQUIRE(out.success);
    REQUIRE(out.image.samples == std::vector<std::uint16_t>({1, 2, 3}));
  }
  {
    // Gray 4-bit, 5 samples/row -> 20 bits in 3 bytes; padding nibble = 0xF.
    const std::uint32_t w = 5;
    const std::uint32_t h = 1;
    std::vector<std::byte> raw(3, std::byte{0});
    for (std::uint32_t x = 0; x < w; ++x) {
      test_write_bits(raw.data(), x * 4, 4, static_cast<std::uint8_t>(x + 1));
    }
    test_write_bits(raw.data(), 20, 4, 0b1111);  // padding
    REQUIRE(raw[2] == std::byte{0x5F});
    const NativeSamplesOutcome out =
        extract_native_samples(ImageHeader{w, h, 4, 0, false}, raw);
    REQUIRE(out.success);
    REQUIRE(out.image.samples ==
            std::vector<std::uint16_t>({1, 2, 3, 4, 5}));
  }
}

// ---------------------------------------------------------------------------
// C: 16-bit samples are big-endian (high byte first).
// ---------------------------------------------------------------------------

TEST_CASE("16-bit samples are read big-endian", "[png-reconstruction][wp304]") {
  // RGBA16, 1x2. Row = 8 bytes: two RGBA pixels, each 8 bytes big-endian.
  std::vector<std::byte> raw = {
      std::byte{0x12}, std::byte{0x34}, std::byte{0xAB}, std::byte{0xCD},
      std::byte{0x00}, std::byte{0xFF}, std::byte{0x80}, std::byte{0x00},
      std::byte{0xFF}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x01},
      std::byte{0x7F}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}};
  const NativeSamplesOutcome out =
      extract_native_samples(ImageHeader{1, 2, 16, 6, false}, raw);
  REQUIRE(out.success);
  REQUIRE(out.image.samples == std::vector<std::uint16_t>(
                                   {0x1234, 0xABCD, 0x00FF, 0x8000,  //
                                    0xFFFF, 0x0001, 0x7FFF, 0x0000}));
}

// ---------------------------------------------------------------------------
// D: end-to-end — WP-303 reconstruct of an interlaced image, then extract.
// The extracted native samples must equal those of the original packed image.
// ---------------------------------------------------------------------------

TEST_CASE("Native extraction closes the reconstruct loop for interlaced images",
          "[png-reconstruction][wp304]") {
  const std::vector<
      std::tuple<std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t>>
      cases = {
          {16, 16, 8, 6}, {13, 7, 4, 0}, {5, 5, 16, 2}, {8, 8, 1, 0},
          {9, 9, 2, 3}, {10, 12, 8, 4}};
  for (const auto& [w, h, bd, ct] : cases) {
    CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
    const EncodedPng e = encode_png(w, h, bd, ct, /*interlace=*/true,
                                    /*all_none=*/false);
    const auto layout = compute_scanline_layout(e.header);
    REQUIRE(layout.has_value());
    const auto recon = reconstruct_image(e.header, *layout, e.filtered);
    REQUIRE(recon.success);
    REQUIRE(recon.target == e.raw);  // WP-303 already proven vs libpng

    const NativeSamplesOutcome out =
        extract_native_samples(e.header, recon.target);
    REQUIRE(out.success);
    REQUIRE(out.image.samples == expected_native(e.raw, w, h, bd, ct));
  }
}

// ---------------------------------------------------------------------------
// E: hostile / inconsistent input fails cleanly with no partial image.
// ---------------------------------------------------------------------------

TEST_CASE("Native extraction rejects invalid input", "[png-reconstruction][wp304]") {
  const std::vector<std::byte> raw = make_raw_image(8, 8, 8, 6, /*seed=*/1);

  // Invalid bit depth.
  const auto bad_depth = extract_native_samples(ImageHeader{8, 8, 3, 6, false}, raw);
  REQUIRE_FALSE(bad_depth.success);
  REQUIRE_FALSE(bad_depth.error.empty());
  REQUIRE(bad_depth.image.samples.empty());

  // Invalid color type.
  const auto bad_ct = extract_native_samples(ImageHeader{8, 8, 8, 7, false}, raw);
  REQUIRE_FALSE(bad_ct.success);

  // Zero dimensions.
  const auto zero = extract_native_samples(ImageHeader{0, 8, 8, 6, false}, raw);
  REQUIRE_FALSE(zero.success);

  // Packed buffer size mismatch.
  auto short_raw = raw;
  short_raw.pop_back();
  const auto mismatch = extract_native_samples(ImageHeader{8, 8, 8, 6, false}, short_raw);
  REQUIRE_FALSE(mismatch.success);
  REQUIRE_FALSE(mismatch.error.empty());
}
