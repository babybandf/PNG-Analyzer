// WP-504 pixel provenance tests: native samples, reconstructed/filtered
// dependencies, Deflate output intervals and physical IDAT bit spans.

#include <pnga/analysis-engine/pixel_provenance.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "test_png_helpers.h"

using namespace pnga_test;
using pnga::analysis_engine::analyze_source;
using pnga::analysis_engine::query_pixel_provenance;
using pnga::io::MemoryByteSource;
using pnga::png_format::index_chunks;
using pnga::png_format::VirtualIDATStream;
using pnga::trace_model::ProvenanceSpace;

TEST_CASE("Pixel provenance reaches token bits and physical IDAT bytes",
          "[analysis-engine][wp504]") {
  const EncodedPng encoded =
      encode_png(8, 5, 8, 6, /*interlace=*/false, /*all_none=*/false);
  MemoryByteSource source(encoded.png_bytes);
  const auto stages = analyze_source(source);
  REQUIRE(stages.success);
  const auto index = index_chunks(source);
  const VirtualIDATStream stream(index);

  const auto result = query_pixel_provenance(
      stages, stream, source, /*x=*/3, /*y=*/2, /*channel=*/1, 1u << 20);
  REQUIRE(result.success);
  REQUIRE(result.native_samples.size() == 1);
  REQUIRE(result.native_samples.front().space == ProvenanceSpace::kNativeSample);
  REQUIRE(result.native_samples.front().length == 2);
  REQUIRE_FALSE(result.reconstructed.empty());
  REQUIRE_FALSE(result.filtered.empty());
  REQUIRE_FALSE(result.inflated.empty());
  REQUIRE_FALSE(result.token_output_ranges.empty());
  REQUIRE_FALSE(result.logical_input.empty());
  REQUIRE_FALSE(result.physical_input.empty());

  for (const auto& span : result.physical_input) {
    REQUIRE(span.space == ProvenanceSpace::kPhysicalFile);
    REQUIRE(span.bit_aligned);
    REQUIRE(span.bit_offset < 8);
    REQUIRE(span.bit_length > 0);
    REQUIRE(span.offset < source.size());
  }
  for (const auto& range : result.token_output_ranges) {
    REQUIRE(range.begin < range.end);
  }
}

TEST_CASE("Pixel provenance supports packed Adam7 samples and fan-in",
          "[analysis-engine][wp504]") {
  const EncodedPng encoded =
      encode_png(16, 12, 2, 0, /*interlace=*/true, /*all_none=*/false);
  MemoryByteSource source(encoded.png_bytes);
  const auto stages = analyze_source(source);
  REQUIRE(stages.success);
  const auto index = index_chunks(source);
  const VirtualIDATStream stream(index);

  const auto result = query_pixel_provenance(
      stages, stream, source, /*x=*/8, /*y=*/8, /*channel=*/0, 1u << 20);
  REQUIRE(result.success);
  REQUIRE(result.native_samples.size() == 1);
  REQUIRE_FALSE(result.native_samples.front().bit_aligned);
  REQUIRE(result.native_samples.front().length == 2);
  REQUIRE_FALSE(result.filtered.empty());
  REQUIRE_FALSE(result.inflated.empty());
  REQUIRE_FALSE(result.token_output_ranges.empty());
  REQUIRE_FALSE(result.physical_input.empty());
}
