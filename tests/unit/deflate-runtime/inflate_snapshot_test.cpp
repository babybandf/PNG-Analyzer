// WP-404 InflateSnapshot tests: capturing mid-stream and restoring into a fresh
// stream continues inflating byte-for-byte; move semantics and output offsets.

#include <pnga/deflate-runtime/inflate_snapshot.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <cstdint>
#include <vector>

using pnga::deflate_runtime::InflateSnapshot;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> make_raw(std::size_t n) {
  std::vector<std::byte> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = B(static_cast<unsigned char>((i * 3 + (i >> 8)) % 251) + 1);
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

// Inflates `compressed` from a fresh stream until `limit` output bytes are
// produced or the stream ends; returns the produced bytes.
std::vector<std::byte> inflate_some(const std::vector<std::byte>& compressed,
                                    std::size_t limit) {
  z_stream strm{};
  inflateInit(&strm);
  strm.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(compressed.data()));
  strm.avail_in = static_cast<uInt>(compressed.size());
  std::vector<std::byte> out(limit);
  strm.next_out = reinterpret_cast<Bytef*>(out.data());
  strm.avail_out = static_cast<uInt>(limit);
  inflate(&strm, Z_NO_FLUSH);
  const std::size_t produced = limit - strm.avail_out;
  out.resize(produced);
  inflateEnd(&strm);
  return out;
}

}  // namespace

TEST_CASE("Restoring a captured snapshot continues inflate byte-for-byte",
          "[deflate-runtime][wp404]") {
  const auto raw = make_raw(200 * 1024);
  const auto compressed = zlib_compress(raw);
  REQUIRE_FALSE(compressed.empty());

  // Inflate partway and capture a snapshot at a known output offset.
  const std::size_t cut = 70000;
  z_stream live{};
  REQUIRE(inflateInit(&live) == Z_OK);
  live.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(compressed.data()));
  live.avail_in = static_cast<uInt>(compressed.size());
  std::vector<std::byte> head(cut);
  live.next_out = reinterpret_cast<Bytef*>(head.data());
  live.avail_out = static_cast<uInt>(cut);
  REQUIRE(inflate(&live, Z_NO_FLUSH) == Z_OK);
  REQUIRE(cut - live.avail_out == cut);
  const auto snapshot = InflateSnapshot::capture(live, cut);
  REQUIRE(snapshot.has_value());
  REQUIRE(snapshot->output_offset() == cut);
  inflateEnd(&live);  // destroy the captured stream; snapshot must survive

  // Restore into a fresh stream and finish inflating.
  z_stream resume{};
  REQUIRE(inflateInit(&resume) == Z_OK);
  REQUIRE(snapshot->restore(resume));
  std::vector<std::byte> tail(raw.size() - cut);
  resume.next_out = reinterpret_cast<Bytef*>(tail.data());
  resume.avail_out = static_cast<uInt>(tail.size());
  const int rc = inflate(&resume, Z_NO_FLUSH);
  REQUIRE(rc == Z_STREAM_END);
  const std::size_t tail_len = tail.size() - resume.avail_out;
  inflateEnd(&resume);

  // The concatenation must equal the full decode.
  tail.resize(tail_len);
  std::vector<std::byte> combined = head;
  combined.insert(combined.end(), tail.begin(), tail.end());
  REQUIRE(combined == raw);
}

TEST_CASE("InflateSnapshot is move-only and reports size",
          "[deflate-runtime][wp404]") {
  const auto raw = make_raw(64 * 1024);
  const auto compressed = zlib_compress(raw);
  z_stream live{};
  inflateInit(&live);
  live.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(compressed.data()));
  live.avail_in = static_cast<uInt>(compressed.size());
  std::byte scratch[4096];
  live.next_out = reinterpret_cast<Bytef*>(scratch);
  live.avail_out = sizeof(scratch);
  inflate(&live, Z_NO_FLUSH);
  auto snap = InflateSnapshot::capture(live, 4096);
  inflateEnd(&live);
  REQUIRE(snap.has_value());
  REQUIRE(snap->approx_bytes() > 0);

  // Move construction transfers ownership; the moved-from is empty.
  InflateSnapshot moved = std::move(*snap);
  REQUIRE(moved.output_offset() == 4096);
  REQUIRE(snap->approx_bytes() == 0);  // moved-from no longer owns state

  // Move assignment also releases any prior destination state without
  // leaking the copied z_stream.
  InflateSnapshot assigned;
  assigned = std::move(moved);
  REQUIRE(assigned.output_offset() == 4096);
  REQUIRE(moved.approx_bytes() == 0);

  // Restoring from the moved-to snapshot works; from the moved-from fails.
  z_stream resume{};
  REQUIRE(inflateInit(&resume) == Z_OK);
  REQUIRE(assigned.restore(resume));
  inflateEnd(&resume);
  z_stream dead{};
  REQUIRE(inflateInit(&dead) == Z_OK);
  REQUIRE_FALSE(snap->restore(dead));
  inflateEnd(&dead);
}
