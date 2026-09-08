// WP-603A: deterministic parser/stream fuzz smoke. Inputs are generated in
// memory with a fixed xorshift sequence and are bounded so this target is
// suitable for every developer and sanitizer preset.

#include <pnga/analysis-engine/validation.h>
#include <pnga/deflate-trace/zlib_wrapper.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-format/animation_index.h>
#include <pnga/png-format/virtual_frame_stream.h>

#include "../common/apng_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

TEST_CASE("APNG metadata and frame streams survive deterministic mutations",
          "[fuzz][wp603c][apng]") {
  const std::array<pnga::png_format::FrameControl, 3> controls = {
      pnga::png_format::FrameControl{0, 1, 1, 0, 0, 0, 0, 0, 0},
      pnga::png_format::FrameControl{0, 1, 1, 0, 0, 1, 100, 1, 1},
      pnga::png_format::FrameControl{0, 1, 1, 0, 0, 2, 100, 0, 0}};
  const auto valid = pnga_test::make_apng(false, controls);
  for (std::uint32_t iteration = 0; iteration < 64; ++iteration) {
    auto bytes = valid;
    if ((iteration & 1U) != 0) {
      pnga_test::set_sequence(bytes, 3, iteration);
    }
    if ((iteration & 2U) != 0 && bytes.size() > 20) {
      bytes.resize(bytes.size() - (iteration % 7U));
    }
    pnga::io::MemoryByteSource source(bytes);
    pnga::png_format::AnimationLimits limits;
    limits.max_frames = 8;
    limits.max_animation_chunks = 32;
    limits.max_metadata_bytes = 1U << 20;
    const auto index = pnga::png_format::index_animation(
        source, limits, [iteration] { return iteration == 63; });
    if (!index.frames.empty()) {
      auto owner = std::make_shared<const pnga::png_format::AnimationIndex>(
          index);
      const auto stream =
          pnga::png_format::make_frame_stream(
              std::make_shared<const pnga::io::MemoryByteSource>(bytes), owner,
              0);
      if (stream != nullptr && stream->size() != 0) {
        std::vector<std::byte> window(
            static_cast<std::size_t>(std::min<std::uint64_t>(stream->size(), 64)));
        REQUIRE(stream->read(0, window.data(), window.size()));
      }
    }
  }
}
