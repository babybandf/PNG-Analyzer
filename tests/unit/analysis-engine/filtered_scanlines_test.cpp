// WP-301 filtered-scanline tests: inflate a virtual IDAT stream and split it
// per layout, including cross-IDAT boundaries, Adler corruption, truncation
// and extra-data rejection.

#include <pnga/analysis-engine/filtered_scanlines.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <array>
#include <cstdint>
#include <vector>

using pnga::analysis_engine::FilteredOutcome;
using pnga::analysis_engine::FilteredScanlineSpan;
using pnga::analysis_engine::inflate_filtered;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::ChunkNode;
using pnga::png_format::kPngSignature;
using pnga::png_format::VirtualIDATStream;
using pnga::png_reconstruction::compute_scanline_layout;
using pnga::png_reconstruction::ImageHeader;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

// Raw filtered scanlines: each row is one 0x00 filter byte + `row_bytes` data.
std::vector<std::byte> filtered_rows(std::uint64_t rows, std::uint64_t row_bytes,
                                     unsigned char fill) {
  std::vector<std::byte> out;
  for (std::uint64_t r = 0; r < rows; ++r) {
    out.push_back(std::byte{0});
    for (std::uint64_t i = 0; i < row_bytes; ++i) {
      out.push_back(B(static_cast<unsigned char>(fill + r)));
    }
  }
  return out;
}

std::vector<std::byte> zlib_compress(const std::vector<std::byte>& raw) {
  uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> out(bound);
  if (compress2(reinterpret_cast<Bytef*>(out.data()), &bound,
                reinterpret_cast<const Bytef*>(raw.data()),
                static_cast<uLong>(raw.size()), Z_DEFAULT_COMPRESSION) != Z_OK) {
    return {};
  }
  out.resize(bound);
  return out;
}

// Builds a ChunkIndex with one IDAT whose data is `compressed`. Optional extra
// trailing bytes simulate a second IDAT / overflow.
struct BuiltPng {
  MemoryByteSource source;
  ChunkIndex index;
  VirtualIDATStream stream;
  std::vector<std::byte> compressed;
};

BuiltPng build_png(std::vector<std::byte> compressed,
                   std::uint64_t declared_idat_length) {
  std::vector<std::byte> bytes;
  bytes.assign(kPngSignature.begin(), kPngSignature.end());
  auto push_chunk = [&](const char* type, std::uint64_t len,
                        std::vector<std::byte> data) {
    bytes.push_back(B(static_cast<unsigned char>(len >> 24)));
    bytes.push_back(B(static_cast<unsigned char>(len >> 16)));
    bytes.push_back(B(static_cast<unsigned char>(len >> 8)));
    bytes.push_back(B(static_cast<unsigned char>(len)));
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(B(static_cast<unsigned char>(type[i])));
    }
    bytes.insert(bytes.end(), data.begin(), data.end());
    bytes.insert(bytes.end(), 4, std::byte{0});
  };
  push_chunk("IHDR", 13, std::vector<std::byte>(13, std::byte{0}));
  push_chunk("IDAT", declared_idat_length, compressed);
  push_chunk("IEND", 0, {});

  auto source = std::make_unique<MemoryByteSource>(std::move(bytes));
  auto index = pnga::png_format::index_chunks(*source);
  VirtualIDATStream stream(index);
  return BuiltPng{std::move(*source), std::move(index), std::move(stream),
                  std::move(compressed)};
}

}  // namespace

TEST_CASE("Filtered scanlines split an RGBA8 non-interlaced image exactly",
          "[analysis-engine][wp301]") {
  // 4x4 RGBA8: 4 rows of (1 + 16) bytes.
  const auto raw = filtered_rows(4, 16, 0x10);
  auto compressed = zlib_compress(raw);
  REQUIRE_FALSE(compressed.empty());

  BuiltPng png = build_png(compressed, compressed.size());
  const auto layout = compute_scanline_layout(ImageHeader{4, 4, 8, 6, false});
  REQUIRE(layout.has_value());

  const FilteredOutcome out = inflate_filtered(png.stream, png.source, *layout);
  REQUIRE(out.success);
  REQUIRE(out.exact_size);
  REQUIRE(out.adler_ok);
  REQUIRE(out.filtered == raw);
  REQUIRE(out.scanlines.size() == 4);
  REQUIRE(out.scanlines[0] == FilteredScanlineSpan{0, 17});
  REQUIRE(out.scanlines[3] == FilteredScanlineSpan{3 * 17, 17});
}

TEST_CASE("A zlib stream split across multiple IDAT segments inflates correctly",
          "[analysis-engine][wp301]") {
  const auto raw = filtered_rows(8, 8, 0x20);  // 8 rows of 9 bytes
  auto compressed = zlib_compress(raw);
  REQUIRE_FALSE(compressed.empty());

  // Split the compressed bytes across two IDATs at a mid-stream byte.
  const std::size_t mid = compressed.size() / 2;
  std::vector<std::byte> c1(compressed.begin(), compressed.begin() + mid);
  std::vector<std::byte> c2(compressed.begin() + mid, compressed.end());

  std::vector<std::byte> bytes;
  bytes.assign(kPngSignature.begin(), kPngSignature.end());
  auto push_chunk = [&](const char* type, std::uint64_t len,
                        const std::vector<std::byte>& data) {
    bytes.push_back(B(static_cast<unsigned char>(len >> 24)));
    bytes.push_back(B(static_cast<unsigned char>(len >> 16)));
    bytes.push_back(B(static_cast<unsigned char>(len >> 8)));
    bytes.push_back(B(static_cast<unsigned char>(len)));
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(B(static_cast<unsigned char>(type[i])));
    }
    bytes.insert(bytes.end(), data.begin(), data.end());
    bytes.insert(bytes.end(), 4, std::byte{0});
  };
  push_chunk("IHDR", 13, std::vector<std::byte>(13, std::byte{0}));
  push_chunk("IDAT", c1.size(), c1);
  push_chunk("IDAT", c2.size(), c2);
  push_chunk("IEND", 0, {});

  MemoryByteSource source(std::move(bytes));
  const ChunkIndex index = pnga::png_format::index_chunks(source);
  VirtualIDATStream stream(index);
  REQUIRE(stream.segment_count() == 2);

  const auto layout = compute_scanline_layout(ImageHeader{8, 8, 8, 0, false});
  REQUIRE(layout.has_value());
  const FilteredOutcome out = inflate_filtered(stream, source, *layout);
  REQUIRE(out.success);
  REQUIRE(out.filtered == raw);
  REQUIRE(out.scanlines.size() == 8);
}

TEST_CASE("Adler corruption is reported as failure", "[analysis-engine][wp301]") {
  const auto raw = filtered_rows(4, 16, 0x10);
  auto compressed = zlib_compress(raw);
  // Flip a byte in the compressed payload (not the last 4 Adler bytes).
  compressed[compressed.size() / 2] ^= std::byte{0xFF};

  BuiltPng png = build_png(compressed, compressed.size());
  const auto layout = compute_scanline_layout(ImageHeader{4, 4, 8, 6, false});
  const FilteredOutcome out = inflate_filtered(png.stream, png.source, *layout);
  REQUIRE_FALSE(out.success);
  REQUIRE_FALSE(out.adler_ok);
}

TEST_CASE("Truncated stream is reported as failure", "[analysis-engine][wp301]") {
  const auto raw = filtered_rows(4, 16, 0x10);
  auto compressed = zlib_compress(raw);
  compressed.resize(compressed.size() - 4);  // drop the tail

  BuiltPng png = build_png(compressed, compressed.size());
  const auto layout = compute_scanline_layout(ImageHeader{4, 4, 8, 6, false});
  const FilteredOutcome out = inflate_filtered(png.stream, png.source, *layout);
  REQUIRE_FALSE(out.success);
  REQUIRE_FALSE(out.exact_size);
}

TEST_CASE("Extra inflated data beyond the layout is rejected",
          "[analysis-engine][wp301]") {
  // 2 rows expected, but the stream holds 4 rows of data.
  const auto raw = filtered_rows(4, 16, 0x10);
  auto compressed = zlib_compress(raw);

  BuiltPng png = build_png(compressed, compressed.size());
  const auto layout = compute_scanline_layout(ImageHeader{2, 2, 8, 6, false});
  REQUIRE(layout.has_value());
  const FilteredOutcome out = inflate_filtered(png.stream, png.source, *layout);
  REQUIRE_FALSE(out.success);
  REQUIRE_FALSE(out.exact_size);
}

TEST_CASE("Interlaced layout splits scanlines pass-major", "[analysis-engine][wp301]") {
  // 4x4 interlaced RGBA8: build raw filtered bytes for the 7 passes.
  const ImageHeader header{4, 4, 8, 6, true};
  const auto layout = compute_scanline_layout(header);
  REQUIRE(layout.has_value());

  // Assemble the stream-order filtered bytes from the layout itself.
  std::vector<std::byte> raw;
  unsigned char fill = 0;
  for (std::size_t p = 0; p < 7; ++p) {
    const auto& pass = layout->passes[p];
    for (std::uint64_t row = 0; row < pass.height; ++row) {
      raw.push_back(std::byte{0});
      for (std::uint64_t i = 0; i < pass.row_bytes; ++i) {
        raw.push_back(B(fill++));
      }
    }
  }
  auto compressed = zlib_compress(raw);
  BuiltPng png = build_png(compressed, compressed.size());

  const FilteredOutcome out = inflate_filtered(png.stream, png.source, *layout);
  INFO("interlaced error: " << out.error << " inflated=" << out.filtered.size()
       << " expected=" << layout->total_bytes.value_or(0));
  REQUIRE(out.success);
  REQUIRE(out.exact_size);
  REQUIRE(out.filtered == raw);
  // Pass 1 of a 4x4 Adam7 image is 1x1 -> its filter-row byte length first.
  REQUIRE(out.scanlines.front().length == layout->passes[0].filter_row_bytes);
  std::uint64_t total = 0;
  for (const auto& s : out.scanlines) {
    total += s.length;
  }
  REQUIRE(total == layout->total_bytes);
}
