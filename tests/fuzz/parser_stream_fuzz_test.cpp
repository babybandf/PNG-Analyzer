// WP-603A: deterministic parser/stream fuzz smoke. Inputs are generated in
// memory with a fixed xorshift sequence and are bounded so this target is
// suitable for every developer and sanitizer preset.

#include <pnga/analysis-engine/validation.h>
#include <pnga/deflate-trace/zlib_wrapper.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

std::uint32_t next_word(std::uint32_t& state) noexcept {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

std::vector<std::byte> generated_input(std::uint32_t& state) {
  constexpr std::size_t kMaxInputBytes = 4096;
  const std::size_t length = next_word(state) % (kMaxInputBytes + 1);
  std::vector<std::byte> bytes(length);
  for (auto& byte : bytes) {
    byte = static_cast<std::byte>(next_word(state) & 0xffU);
  }
  return bytes;
}

}  // namespace

TEST_CASE("Parser, Virtual IDAT and zlib wrapper survive bounded fuzz input",
          "[fuzz][wp603a]") {
  constexpr std::uint32_t kSeed = 0x603a2026U;
  constexpr int kCases = 512;
  std::uint32_t state = kSeed;
  for (int iteration = 0; iteration < kCases; ++iteration) {
    const auto bytes = generated_input(state);
    pnga::io::MemoryByteSource source(bytes);
    const auto index = pnga::png_format::index_chunks(source);
    const auto report = pnga::analysis_engine::validate_document(source, index);
    (void)report;

    pnga::png_format::VirtualIDATStream stream(index);
    if (stream.size() != 0) {
      const std::size_t take = static_cast<std::size_t>(
          std::min<std::uint64_t>(stream.size(), 32));
      std::vector<std::byte> window(take);
      REQUIRE(stream.read(source, 0, window.data(), window.size()));
    }
    const auto wrapper = pnga::deflate_trace::trace_zlib_wrapper(source);
    REQUIRE(wrapper.total_bytes == source.size());
  }
}

