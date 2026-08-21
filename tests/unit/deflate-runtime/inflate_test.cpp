// WP-301 inflate wrapper tests: round-trip, bounded output (bomb), Adler
// corruption and truncation, reported by the generic deflate-runtime layer.

#include <pnga/deflate-runtime/inflate.h>

#include <pnga/io/byte_source.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <cstdint>
#include <vector>

using pnga::deflate_runtime::inflate_stream;
using pnga::deflate_runtime::InflateOutcome;
using pnga::io::MemoryByteSource;

namespace {

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

std::vector<std::byte> bytes_of(std::size_t n, unsigned char fill = 0x5A) {
  return std::vector<std::byte>(n, static_cast<std::byte>(fill));
}

}  // namespace

TEST_CASE("A valid zlib stream round-trips", "[deflate-runtime][wp301]") {
  const auto raw = bytes_of(100000);
  const auto compressed = zlib_compress(raw);
  REQUIRE_FALSE(compressed.empty());

  MemoryByteSource src(compressed);
  const InflateOutcome out = inflate_stream(src, 200000);
  REQUIRE(out.success);
  REQUIRE(out.stream_ended);
  REQUIRE(out.adler_ok);
  REQUIRE_FALSE(out.truncated);
  REQUIRE_FALSE(out.output_capped);
  REQUIRE(out.output == raw);
}

TEST_CASE("Output above the cap is rejected as a bomb", "[deflate-runtime][wp301]") {
  const auto raw = bytes_of(10000);
  const auto compressed = zlib_compress(raw);
  MemoryByteSource src(compressed);
  const InflateOutcome out = inflate_stream(src, 1000);  // cap far below
  REQUIRE_FALSE(out.success);
  REQUIRE(out.output_capped);
}

TEST_CASE("Corrupt stream fails with Adler/checksum error", "[deflate-runtime][wp301]") {
  auto compressed = zlib_compress(bytes_of(1000));
  compressed[compressed.size() / 2] ^= std::byte{0x01};
  MemoryByteSource src(compressed);
  const InflateOutcome out = inflate_stream(src, 5000);
  REQUIRE_FALSE(out.success);
  REQUIRE_FALSE(out.adler_ok);
}

TEST_CASE("Truncated stream is reported", "[deflate-runtime][wp301]") {
  auto compressed = zlib_compress(bytes_of(1000));
  compressed.resize(compressed.size() - 5);
  MemoryByteSource src(compressed);
  const InflateOutcome out = inflate_stream(src, 5000);
  REQUIRE_FALSE(out.success);
  REQUIRE(out.truncated);
  REQUIRE_FALSE(out.stream_ended);
}

TEST_CASE("Empty zlib stream of zero bytes inflates cleanly",
          "[deflate-runtime][wp301]") {
  // A zlib stream containing only a stored empty block.
  const std::vector<std::byte> empty_ok = zlib_compress({});
  MemoryByteSource src(empty_ok);
  const InflateOutcome out = inflate_stream(src, 10);
  REQUIRE(out.success);
  REQUIRE(out.stream_ended);
  REQUIRE(out.output.empty());
}
