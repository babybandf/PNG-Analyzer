// WP-403 scanline anchor tests: restoring random scanlines reproduces the full
// decode byte-for-byte (non-interlaced and interlaced), pass-first rows are
// always anchored, replay distance stays bounded for multi-block streams and
// out-of-range rows are rejected.

#include <pnga/analysis-engine/scanline_anchor.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "test_png_helpers.h"

using namespace pnga_test;  // NOLINT: test helpers are the local vocabulary

using pnga::analysis_engine::analyze_stages;
using pnga::analysis_engine::build_scanline_anchors;
using pnga::analysis_engine::restore_scanline;
using pnga::analysis_engine::ScanlineAnchorIndexResult;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::VirtualIDATStream;
using pnga::png_reconstruction::compute_scanline_layout;
using pnga::png_reconstruction::ImageHeader;

namespace {

struct Parsed {
  MemoryByteSource file;
  ChunkIndex index;
  VirtualIDATStream stream;
  ImageHeader header;

  Parsed(std::vector<std::byte> bytes, ImageHeader h)
      : file(std::move(bytes)),
        index(pnga::png_format::index_chunks(file)),
        stream(index),
        header(h) {}
};

Parsed parse(const EncodedPng& e) { return Parsed(e.png_bytes, e.header); }

// Deterministic pseudo-random row sequence (no RNG dependency).
std::uint64_t step_hash(std::uint64_t x) { return x * 2654435761ull; }

}  // namespace

TEST_CASE("Random non-interlaced scanlines restore byte-for-byte",
          "[analysis-engine][wp403]") {
  const EncodedPng e = encode_png(24, 37, 8, 6, /*interlace=*/false,
                                  /*all_none=*/false);  // rotating filters
  Parsed p = parse(e);
  const ScanlineAnchorIndexResult index =
      build_scanline_anchors(p.stream, p.file, p.header, /*interval_bytes=*/512,
                             /*max_output_bytes=*/1u << 20);
  REQUIRE(index.success);
  REQUIRE(index.anchors.front().stream_row == 0);
  const std::uint64_t rb = test_row_bytes(24, 8, 6);  // 24 * 4 = 96

  std::uint64_t h = 12345;
  for (int i = 0; i < 100; ++i) {
    h = step_hash(h);
    const std::uint64_t row = h % 37;
    CAPTURE(row);
    const auto r = restore_scanline(index, p.stream, p.file, row);
    REQUIRE(r.success);
    REQUIRE(r.unfiltered ==
            std::vector<std::byte>(e.raw.begin() + static_cast<std::ptrdiff_t>(row * rb),
                                   e.raw.begin() +
                                       static_cast<std::ptrdiff_t>((row + 1) * rb)));
  }
}

TEST_CASE("Multi-block stored stream keeps replay distance bounded",
          "[analysis-engine][wp403]") {
  // Level-0 (stored) input yields many deflate blocks, hence dense access
  // points, so the replay distance from a point to an anchor stays small.
  const EncodedPng e = encode_png(64, 64, 8, 6, /*interlace=*/false,
                                  /*all_none=*/true);
  Parsed p = parse(e);
  const std::uint64_t interval = 4096;
  const ScanlineAnchorIndexResult index =
      build_scanline_anchors(p.stream, p.file, p.header, interval, 1u << 20);
  REQUIRE(index.success);
  REQUIRE(index.max_replay_bytes <= interval + 65536);

  const std::uint64_t rb = test_row_bytes(64, 8, 6);  // 256
  std::uint64_t h = 99;
  for (int i = 0; i < 100; ++i) {
    h = step_hash(h);
    const std::uint64_t row = h % 64;
    const auto r = restore_scanline(index, p.stream, p.file, row);
    REQUIRE(r.success);
    REQUIRE(r.replay_bytes <= interval + 65536);
    REQUIRE(r.unfiltered ==
            std::vector<std::byte>(e.raw.begin() + static_cast<std::ptrdiff_t>(row * rb),
                                   e.raw.begin() +
                                       static_cast<std::ptrdiff_t>((row + 1) * rb)));
  }
}

TEST_CASE("Interlaced scanlines restore byte-for-byte with pass-first anchors",
          "[analysis-engine][wp403]") {
  const EncodedPng e = encode_png(16, 12, 8, 6, /*interlace=*/true,
                                  /*all_none=*/false);
  Parsed p = parse(e);
  const ScanlineAnchorIndexResult index =
      build_scanline_anchors(p.stream, p.file, p.header, 256, 1u << 20);
  REQUIRE(index.success);

  // Every Adam7 pass's first row is anchored (prev = empty), so a restore
  // never crosses a pass boundary.
  const auto layout = compute_scanline_layout(p.header);
  REQUIRE(layout.has_value());
  std::uint64_t cursor = 0;
  std::size_t pass_anchors = 0;
  for (const auto& pass : layout->passes) {
    if (pass.height == 0) {
      continue;
    }
    const bool found = std::any_of(
        index.anchors.begin(), index.anchors.end(),
        [cursor](const auto& a) { return a.stream_row == cursor; });
    REQUIRE(found);
    ++pass_anchors;
    cursor += pass.height;
  }
  REQUIRE(pass_anchors >= 2);

  // Ground truth: the analysis-engine stage artifacts (validated vs libpng in
  // WP-305/306).
  const auto stages = analyze_stages(p.stream, p.file, p.header);
  REQUIRE(stages.success);

  std::uint64_t h = 7;
  for (int i = 0; i < 100; ++i) {
    h = step_hash(h);
    const std::uint64_t row = h % index.scanline_count;
    // Map stream row -> (pass, row-in-pass).
    std::uint64_t c = 0;
    std::size_t pass_index = 0;
    std::uint64_t row_in_pass = 0;
    for (std::size_t pp = 0; pp < layout->pass_count; ++pp) {
      const auto& pass = layout->passes[pp];
      if (pass.height == 0) {
        continue;
      }
      if (row < c + pass.height) {
        pass_index = pp;
        row_in_pass = row - c;
        break;
      }
      c += pass.height;
    }
    CAPTURE(row);
    const auto r = restore_scanline(index, p.stream, p.file, row);
    REQUIRE(r.success);
    const auto& pass = layout->passes[pass_index];
    const std::size_t off =
        static_cast<std::size_t>(row_in_pass * pass.row_bytes);
    REQUIRE(r.unfiltered ==
            std::vector<std::byte>(stages.passes[pass_index].rows.begin() + off,
                                   stages.passes[pass_index].rows.begin() + off +
                                       static_cast<std::ptrdiff_t>(pass.row_bytes)));
  }
}

TEST_CASE("Out-of-range scanline restore is rejected",
          "[analysis-engine][wp403]") {
  const EncodedPng e = encode_png(8, 8, 8, 6, false, true);
  Parsed p = parse(e);
  const ScanlineAnchorIndexResult index =
      build_scanline_anchors(p.stream, p.file, p.header, 64, 1u << 20);
  REQUIRE(index.success);
  const auto r = restore_scanline(index, p.stream, p.file, 1000);
  REQUIRE_FALSE(r.success);
  REQUIRE_FALSE(r.error.empty());
}
