// WP-603B: bounded Deflate/reconstruction fuzz smoke over independent
// generated PNGs and deterministic byte mutations.

#include "../common/test_png_helpers.h"

#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

struct Case {
  std::uint32_t width;
  std::uint32_t height;
  std::uint8_t bit_depth;
  std::uint8_t color_type;
  bool interlace;
};

}  // namespace

TEST_CASE("Deflate trace and reconstruction survive bounded generated mutations",
          "[fuzz][wp603b]") {
  constexpr std::array<Case, 4> kCases = {{
      {8, 5, 8, 6, false},
      {16, 12, 8, 2, true},
      {9, 7, 2, 3, false},
      {5, 3, 16, 0, false},
  }};
  std::uint32_t mutation = 0x603b2026U;
  for (const auto& test_case : kCases) {
    const auto encoded = pnga_test::encode_png(
        test_case.width, test_case.height, test_case.bit_depth,
        test_case.color_type, test_case.interlace, /*all_none=*/false,
        mutation++);
    pnga::io::MemoryByteSource source(encoded.png_bytes);
    const auto stages = pnga::analysis_engine::analyze_source(source);
    REQUIRE(stages.success);
    REQUIRE(stages.header.width == test_case.width);
    REQUIRE(stages.header.height == test_case.height);
    for (std::uint64_t row = 0; row < stages.scanlines.size(); ++row) {
      const auto formula = pnga::analysis_engine::filter_formula(stages, row);
      REQUIRE(formula.success);
    }

    const auto index = pnga::png_format::index_chunks(source);
    const pnga::png_format::VirtualIDATStream stream(index);
    std::vector<std::byte> zlib_bytes(static_cast<std::size_t>(stream.size()));
    REQUIRE(stream.read(source, 0, zlib_bytes.data(), zlib_bytes.size()));
    pnga::io::MemoryByteSource zlib_source(std::move(zlib_bytes));
    const auto trace = pnga::deflate_trace::decode_stored_and_fixed(
        zlib_source, /*max_output_bytes=*/1u << 20);
    REQUIRE(trace.success);
    REQUIRE(trace.stream_ended);

    // Mutations are deliberately bounded and may produce either a structured
    // error or a partial result; the invariant is that no input crashes or
    // escapes the parser's budgets.
    for (int i = 0; i < 8; ++i) {
      auto mutated = encoded.png_bytes;
      mutation = mutation * 1664525U + 1013904223U;
      const std::size_t offset = mutation % mutated.size();
      mutated[offset] ^= static_cast<std::byte>(mutation >> 24);
      pnga::io::MemoryByteSource mutated_source(std::move(mutated));
      const auto result = pnga::analysis_engine::analyze_source(mutated_source);
      (void)result;
    }
  }
}

