// WP-306 stage analysis tests: materialize Filtered/Unfiltered/Native stages
// and replay the per-byte filter formula, for both non-interlaced and
// interlaced images.

#include <pnga/analysis-engine/stage_analysis.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-reconstruction/native_samples.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "test_png_helpers.h"

using namespace pnga_test;  // NOLINT: test helpers are the local vocabulary

using pnga::analysis_engine::analyze_stages;
using pnga::analysis_engine::filter_formula;
using pnga::analysis_engine::FilterFormula;
using pnga::analysis_engine::StageSet;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::VirtualIDATStream;
using pnga::png_reconstruction::compute_scanline_layout;
using pnga::png_reconstruction::extract_native_samples;

namespace {

std::uint8_t u8(std::byte b) { return static_cast<std::uint8_t>(b); }

StageSet stages_of(const EncodedPng& e) {
  MemoryByteSource source(e.png_bytes);
  const ChunkIndex index = pnga::png_format::index_chunks(source);
  VirtualIDATStream stream(index);
  return analyze_stages(stream, source, e.header);
}

}  // namespace

TEST_CASE("Stage analysis materializes non-interlaced stages",
          "[analysis-engine][wp306]") {
  const EncodedPng e = encode_png(8, 8, 8, 6, /*interlace=*/false,
                                  /*all_none=*/true);
  const StageSet s = stages_of(e);
  REQUIRE(s.success);
  REQUIRE_FALSE(s.interlace);
  REQUIRE(s.filtered == e.filtered);   // exact filtered bytes the encoder made
  REQUIRE(s.unfiltered == e.raw);      // packed reconstruction == source rows
  REQUIRE(s.scanlines.size() == 8);
  REQUIRE(s.passes.size() == 1);
  REQUIRE(s.passes[0].rows == e.raw);

  const auto expected = extract_native_samples(e.header, e.raw);
  REQUIRE(expected.success);
  REQUIRE(s.native.samples == expected.image.samples);
  REQUIRE(s.native.channels == 4);
}

TEST_CASE("Stage analysis materializes interlaced stages",
          "[analysis-engine][wp306]") {
  const EncodedPng e = encode_png(16, 12, 8, 2, /*interlace=*/true,
                                  /*all_none=*/false);
  const StageSet s = stages_of(e);
  REQUIRE(s.success);
  REQUIRE(s.interlace);
  REQUIRE(s.unfiltered == e.raw);
  REQUIRE(s.passes.size() == 7);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());
  std::uint64_t expected_rows = 0;
  for (const auto& pass : layout->passes) {
    expected_rows += pass.height;
  }
  REQUIRE(s.scanlines.size() == static_cast<std::size_t>(expected_rows));

  const auto expected = extract_native_samples(e.header, e.raw);
  REQUIRE(expected.success);
  REQUIRE(s.native.samples == expected.image.samples);
}

// ---------------------------------------------------------------------------
// Formula replay: for every scanline, the events must reproduce the exact
// source bytes (non-interlaced: compare against the packed raw rows directly;
// interlaced: compare against the pass artifacts, which WP-303/305 already
// validated against libpng).
// ---------------------------------------------------------------------------

TEST_CASE("Filter formula reproduces source bytes for all five filters",
          "[analysis-engine][wp306]") {
  // 8x5 RGBA8, rotating filters: rows 0..4 -> None,Sub,Up,Average,Paeth.
  const EncodedPng e = encode_png(8, 5, 8, 6, /*interlace=*/false,
                                  /*all_none=*/false);
  const StageSet s = stages_of(e);
  REQUIRE(s.success);
  const std::uint64_t rb = test_row_bytes(8, 8, 6);  // 32
  const std::uint64_t bpp = test_bpp(8, 6);          // 4
  for (std::uint64_t row = 0; row < 5; ++row) {
    CAPTURE(row);
    const FilterFormula f = filter_formula(s, row);
    REQUIRE(f.success);
    REQUIRE(f.row == row);
    REQUIRE(f.filter == filter_for(row, false));
    REQUIRE(f.events.size() == static_cast<std::size_t>(rb));
    for (std::uint64_t i = 0; i < rb; ++i) {
      const auto& ev = f.events[static_cast<std::size_t>(i)];
      const std::uint8_t a = i >= bpp ? u8(e.raw[row * rb + i - bpp]) : 0;
      const std::uint8_t b = row > 0 ? u8(e.raw[(row - 1) * rb + i]) : 0;
      const std::uint8_t c = (i >= bpp && row > 0)
                                 ? u8(e.raw[(row - 1) * rb + i - bpp])
                                 : 0;
      REQUIRE(ev.index == i);
      REQUIRE(ev.a == a);
      REQUIRE(ev.b == b);
      REQUIRE(ev.c == c);
      REQUIRE(ev.recon == u8(e.raw[row * rb + i]));  // recovers the source byte
      // Forward filtering guarantees raw + predictor == recon (mod 256).
      REQUIRE((static_cast<unsigned>(ev.raw) + ev.predictor) % 256u ==
              static_cast<unsigned>(ev.recon));
    }
  }
  // First row: no previous row, so all up-neighbors are zero.
  const FilterFormula first = filter_formula(s, 0);
  REQUIRE(first.success);
  for (const auto& ev : first.events) {
    REQUIRE(ev.b == 0);
    REQUIRE(ev.c == 0);
  }
}

TEST_CASE("Filter formula maps stream rows to interlaced passes",
          "[analysis-engine][wp306]") {
  const EncodedPng e = encode_png(16, 12, 8, 2, /*interlace=*/true,
                                  /*all_none=*/false);
  const StageSet s = stages_of(e);
  REQUIRE(s.success);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());

  std::uint64_t cursor = 0;
  for (std::size_t p = 0; p < layout->pass_count; ++p) {
    const auto& pass = layout->passes[p];
    if (pass.height == 0) {
      continue;
    }
    for (std::uint64_t rp = 0; rp < pass.height; ++rp) {
      const std::uint64_t row = cursor + rp;
      CAPTURE(p, rp, row);
      const FilterFormula f = filter_formula(s, row);
      REQUIRE(f.success);
      REQUIRE(f.events.size() == static_cast<std::size_t>(pass.row_bytes));
      // The unfiltered row must equal the pass artifact (independent ground
      // truth, validated against libpng in WP-305).
      const auto& rows = s.passes[p].rows;
      for (std::uint64_t i = 0; i < pass.row_bytes; ++i) {
        const std::size_t off = static_cast<std::size_t>(rp * pass.row_bytes + i);
        REQUIRE(f.events[static_cast<std::size_t>(i)].recon ==
                static_cast<std::uint8_t>(rows[off]));
      }
    }
    cursor += pass.height;
  }
}

// ---------------------------------------------------------------------------
// Errors.
// ---------------------------------------------------------------------------

TEST_CASE("Stage analysis and formula reject invalid input",
          "[analysis-engine][wp306]") {
  // Row out of range.
  const EncodedPng e = encode_png(8, 5, 8, 6, false, false);
  const StageSet s = stages_of(e);
  REQUIRE(s.success);
  const FilterFormula bad = filter_formula(s, 100);
  REQUIRE_FALSE(bad.success);
  REQUIRE_FALSE(bad.error.empty());

  // Truncated IDAT stream -> no stages.
  std::vector<std::byte> truncated(e.png_bytes.begin(),
                                   e.png_bytes.begin() +
                                       static_cast<std::ptrdiff_t>(e.png_bytes.size() / 2));
  MemoryByteSource source(truncated);
  const ChunkIndex index = pnga::png_format::index_chunks(source);
  VirtualIDATStream stream(index);
  const StageSet failed = analyze_stages(stream, source, e.header);
  REQUIRE_FALSE(failed.success);
  REQUIRE_FALSE(failed.error.empty());
}
