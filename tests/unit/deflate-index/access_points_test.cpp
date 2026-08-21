// WP-402 access point tests: restarting from every point reproduces the full
// inflate byte-for-byte (including distance references that cross a
// checkpoint, exercised by the dictionary), bit-aligned and mid-byte
// restarts, interval spacing, source-modification rejection and bounds.

#include <pnga/deflate-index/access_points.h>

#include <pnga/io/byte_source.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using pnga::deflate_index::AccessIndexResult;
using pnga::deflate_index::build_access_index;
using pnga::deflate_index::extract_output;
using pnga::io::MemoryByteSource;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> make_compressible(std::size_t n) {
  std::vector<std::byte> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = B(static_cast<unsigned char>((i * 7 + (i >> 9)) % 251) + 1);
  }
  return out;
}

std::vector<std::byte> zlib_compress(const std::vector<std::byte>& raw,
                                     int level, int strategy) {
  z_stream strm{};
  if (deflateInit2(&strm, level, Z_DEFLATED, 15, 8, strategy) != Z_OK) {
    return {};
  }
  const uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> out(static_cast<std::size_t>(bound));
  strm.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(raw.data()));
  strm.avail_in = static_cast<uInt>(raw.size());
  strm.next_out = reinterpret_cast<Bytef*>(out.data());
  strm.avail_out = static_cast<uInt>(out.size());
  const int rc = deflate(&strm, Z_FINISH);
  if (rc != Z_STREAM_END) {
    deflateEnd(&strm);
    return {};
  }
  out.resize(strm.total_out);
  deflateEnd(&strm);
  return out;
}

}  // namespace

TEST_CASE("Every access point restarts inflate byte-for-byte",
          "[deflate-index][wp402]") {
  // Stored (level 0) input yields many small blocks, hence many access points,
  // so every restart path is exercised.
  const auto raw = make_compressible(300 * 1024);
  const auto compressed = zlib_compress(raw, 0, Z_DEFAULT_STRATEGY);
  REQUIRE_FALSE(compressed.empty());

  MemoryByteSource source(compressed);
  const AccessIndexResult index =
      build_access_index(source, 1u << 20, /*min_interval=*/32768);
  REQUIRE(index.success);
  REQUIRE(index.total_output_bytes == raw.size());
  REQUIRE(index.points.size() >= 3);
  REQUIRE(index.points.front().output_offset == 0);

  // Extracting from every point's offset (plus a little past it) must match
  // the source bytes exactly.
  for (const auto& point : index.points) {
    CAPTURE(point.output_offset, static_cast<unsigned>(point.prime_bits));
    const std::uint64_t off = point.output_offset + 7;
    if (off >= raw.size()) {
      continue;
    }
    const std::uint64_t len =
        std::min<std::uint64_t>(4096, raw.size() - off);
    const auto ex = extract_output(index, source, off, len);
    REQUIRE(ex.success);
    REQUIRE(ex.data ==
            std::vector<std::byte>(raw.begin() + static_cast<std::ptrdiff_t>(off),
                                   raw.begin() + static_cast<std::ptrdiff_t>(off + len)));
  }
}

TEST_CASE("Extraction across checkpoint boundaries uses the dictionary",
          "[deflate-index][wp402]") {
  const auto raw = make_compressible(400 * 1024);
  const auto compressed = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);
  MemoryByteSource source(compressed);
  const AccessIndexResult index =
      build_access_index(source, 1u << 20, /*min_interval=*/32768);
  REQUIRE(index.success);

  // Sweep chunks that start between points and extend past the next point, so
  // distance codes after each checkpoint must be resolved via the dictionary.
  for (std::uint64_t off = 0; off + 1 < raw.size(); off += 8192) {
    CAPTURE(off);
    const std::uint64_t len =
        std::min<std::uint64_t>(32768, raw.size() - off);
    const auto ex = extract_output(index, source, off, len);
    REQUIRE(ex.success);
    REQUIRE(ex.data ==
            std::vector<std::byte>(raw.begin() + static_cast<std::ptrdiff_t>(off),
                                   raw.begin() + static_cast<std::ptrdiff_t>(off + len)));
  }
}

TEST_CASE("Bit-aligned and mid-byte restarts both reproduce the stream",
          "[deflate-index][wp402]") {
  const auto raw = make_compressible(200 * 1024);
  const auto compressed = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);
  MemoryByteSource source(compressed);
  const AccessIndexResult index =
      build_access_index(source, 1u << 20, /*min_interval=*/16384);
  REQUIRE(index.success);

  // The first point is the bit-aligned zlib-header boundary.
  REQUIRE(index.points.front().prime_bits == 0);
  const auto from_zero = extract_output(index, source, 0, 256);
  REQUIRE(from_zero.success);
  REQUIRE(from_zero.data ==
          std::vector<std::byte>(raw.begin(), raw.begin() + 256));

  // Dynamic huffman blocks end mid-byte, so some later point must be primed.
  const bool saw_primed =
      std::any_of(index.points.begin(), index.points.end(),
                  [](const auto& p) { return p.prime_bits != 0; });
  REQUIRE(saw_primed);
  for (const auto& point : index.points) {
    if (point.prime_bits == 0 || point.output_offset >= raw.size()) {
      continue;
    }
    const std::uint64_t len =
        std::min<std::uint64_t>(2048, raw.size() - point.output_offset);
    const auto ex = extract_output(index, source, point.output_offset, len);
    REQUIRE(ex.success);
    REQUIRE(ex.data ==
            std::vector<std::byte>(
                raw.begin() + static_cast<std::ptrdiff_t>(point.output_offset),
                raw.begin() +
                    static_cast<std::ptrdiff_t>(point.output_offset + len)));
  }
}

TEST_CASE("Points are spaced by the output interval", "[deflate-index][wp402]") {
  const auto raw = make_compressible(300 * 1024);
  const auto compressed = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);
  MemoryByteSource source(compressed);
  const AccessIndexResult index =
      build_access_index(source, 1u << 20, /*min_interval=*/65536);
  REQUIRE(index.success);
  REQUIRE(index.points.front().output_offset == 0);
  for (std::size_t i = 1; i < index.points.size(); ++i) {
    REQUIRE(index.points[i].output_offset - index.points[i - 1].output_offset >=
            65536);
  }
  // Densest setting still keeps a bounded count for a 300 KiB stream.
  const AccessIndexResult dense =
      build_access_index(source, 1u << 20, /*min_interval=*/1);
  REQUIRE(dense.success);
  REQUIRE(dense.points.size() <= 64);
}

TEST_CASE("Modified source rejects a stale access index",
          "[deflate-index][wp402]") {
  const auto raw = make_compressible(64 * 1024);
  const auto compressed = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);
  MemoryByteSource source(compressed);
  const AccessIndexResult index =
      build_access_index(source, 1u << 20, /*min_interval=*/16384);
  REQUIRE(index.success);

  // Corrupt the zlib header: the fingerprint no longer matches.
  auto modified = compressed;
  modified[0] ^= std::byte{0xFF};
  MemoryByteSource mod_source(modified);
  const auto ex = extract_output(index, mod_source, 1000, 100);
  REQUIRE_FALSE(ex.success);
  REQUIRE(ex.error.find("changed") != std::string::npos);
}

TEST_CASE("Out-of-range extraction is rejected", "[deflate-index][wp402]") {
  const auto raw = make_compressible(4096);
  const auto compressed = zlib_compress(raw, 0, Z_DEFAULT_STRATEGY);
  MemoryByteSource source(compressed);
  const AccessIndexResult index =
      build_access_index(source, 1u << 20, /*min_interval=*/1024);
  REQUIRE(index.success);
  const auto ex = extract_output(index, source, raw.size(), 16);
  REQUIRE_FALSE(ex.success);
  REQUIRE_FALSE(ex.error.empty());
}
