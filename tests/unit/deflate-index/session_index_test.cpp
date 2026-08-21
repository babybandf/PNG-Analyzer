// WP-404 session snapshot index tests: a single huge Huffman block (where
// portable access points are sparse) still gets dense inflateCopy snapshots,
// and extraction from every snapshot reproduces the full decode byte-for-byte.

#include <pnga/deflate-index/access_points.h>
#include <pnga/deflate-index/session_index.h>

#include <pnga/io/byte_source.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using pnga::deflate_index::build_access_index;
using pnga::deflate_index::build_session_index;
using pnga::deflate_index::extract_session_output;
using pnga::deflate_index::SessionIndexResult;
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

std::vector<std::byte> zlib_compress(const std::vector<std::byte>& raw) {
  z_stream strm{};
  if (deflateInit(&strm, 6) != Z_OK) {
    return {};
  }
  const uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> out(static_cast<std::size_t>(bound));
  strm.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(raw.data()));
  strm.avail_in = static_cast<uInt>(raw.size());
  strm.next_out = reinterpret_cast<Bytef*>(out.data());
  strm.avail_out = static_cast<uInt>(out.size());
  const int rc = deflate(&strm, Z_FINISH);
  deflateEnd(&strm);
  if (rc != Z_STREAM_END) {
    return {};
  }
  out.resize(strm.total_out);
  return out;
}

}  // namespace

TEST_CASE("Dense session snapshots beat sparse access points on one huge block",
          "[deflate-index][wp404]") {
  // Level 6 on 300 KiB of compressible data is typically a single dynamic
  // block, so portable access points sit only at block boundaries (often 1).
  const auto raw = make_compressible(300 * 1024);
  const auto compressed = zlib_compress(raw);
  REQUIRE_FALSE(compressed.empty());

  MemoryByteSource source(compressed);
  const auto access = build_access_index(source, 1u << 20, /*interval=*/32768);
  REQUIRE(access.success);

  const SessionIndexResult session = build_session_index(
      compressed, 1u << 20, /*interval_bytes=*/32768,
      /*snapshot_budget_bytes=*/1u << 30);
  REQUIRE(session.success);
  REQUIRE(session.total_output_bytes == raw.size());
  REQUIRE(session.snapshots.front().output_offset == 0);
  REQUIRE(session.snapshots.size() >= 3);
  // Session snapshots are denser than portable points for a single-block stream.
  REQUIRE(session.snapshots.size() > access.points.size());
}

TEST_CASE("Extraction from every session snapshot reproduces the stream",
          "[deflate-index][wp404]") {
  const auto raw = make_compressible(200 * 1024);
  const auto compressed = zlib_compress(raw);
  const SessionIndexResult session = build_session_index(
      compressed, 1u << 20, /*interval_bytes=*/16384,
      /*snapshot_budget_bytes=*/1u << 30);
  REQUIRE(session.success);

  for (const auto& snap : session.snapshots) {
    CAPTURE(snap.output_offset);
    if (snap.output_offset >= raw.size()) {
      continue;
    }
    const std::uint64_t len =
        std::min<std::uint64_t>(8192, raw.size() - snap.output_offset);
    const auto ex = extract_session_output(session, snap.output_offset, len);
    REQUIRE(ex.success);
    REQUIRE(ex.data ==
            std::vector<std::byte>(
                raw.begin() + static_cast<std::ptrdiff_t>(snap.output_offset),
                raw.begin() +
                    static_cast<std::ptrdiff_t>(snap.output_offset + len)));
  }

  // A sweep that straddles snapshots must also match.
  for (std::uint64_t off = 0; off + 1 < raw.size(); off += 8192) {
    const std::uint64_t len =
        std::min<std::uint64_t>(32768, raw.size() - off);
    const auto ex = extract_session_output(session, off, len);
    REQUIRE(ex.success);
    REQUIRE(ex.data ==
            std::vector<std::byte>(raw.begin() + static_cast<std::ptrdiff_t>(off),
                                   raw.begin() +
                                       static_cast<std::ptrdiff_t>(off + len)));
  }
}

TEST_CASE("Snapshot memory budget limits the snapshot count",
          "[deflate-index][wp404]") {
  const auto raw = make_compressible(300 * 1024);
  const auto compressed = zlib_compress(raw);

  // A tiny budget keeps only the mandatory snapshot at offset 0.
  const SessionIndexResult tiny = build_session_index(
      compressed, 1u << 20, /*interval_bytes=*/1024,
      /*snapshot_budget_bytes=*/1);
  REQUIRE(tiny.success);
  REQUIRE(tiny.snapshots.size() == 1);
  REQUIRE(tiny.snapshots.front().output_offset == 0);

  // A large budget keeps many.
  const SessionIndexResult large = build_session_index(
      compressed, 1u << 20, /*interval_bytes=*/1024,
      /*snapshot_budget_bytes=*/1u << 30);
  REQUIRE(large.success);
  REQUIRE(large.snapshots.size() >= 2);
}

TEST_CASE("Out-of-range session extraction is rejected",
          "[deflate-index][wp404]") {
  const auto raw = make_compressible(4096);
  const auto compressed = zlib_compress(raw);
  const SessionIndexResult session = build_session_index(
      compressed, 1u << 20, /*interval_bytes=*/1024,
      /*snapshot_budget_bytes=*/1u << 20);
  REQUIRE(session.success);
  const auto ex = extract_session_output(session, raw.size(), 16);
  REQUIRE_FALSE(ex.success);
  REQUIRE_FALSE(ex.error.empty());
}
