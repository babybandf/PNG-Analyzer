// WP-500 zlib wrapper trace tests: fields and bit spans, structural validity
// (method/check/window), FDICT+DICTID, and Adler-32 / trailing-bytes behavior
// cross-checked against standard zlib through the deflate runtime.

#include <pnga/deflate-trace/zlib_wrapper.h>
#include <pnga/deflate-runtime/inflate.h>
#include <pnga/io/byte_source.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <cstdint>
#include <vector>

using pnga::deflate_runtime::inflate_stream;
using pnga::deflate_trace::trace_zlib_wrapper;
using pnga::deflate_trace::ZlibWrapperTrace;
using pnga::io::MemoryByteSource;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> make_raw(std::size_t n) {
  std::vector<std::byte> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = B(static_cast<unsigned char>((i * 3 + (i >> 7)) % 251) + 1);
  }
  return out;
}

// zlib-wraps `raw` (deflateInit -> zlib header). Optionally sets a preset
// dictionary first to exercise FDICT.
std::vector<std::byte> zlib_wrap(const std::vector<std::byte>& raw,
                                 const std::vector<std::byte>* dict = nullptr) {
  z_stream strm{};
  if (deflateInit(&strm, 6) != Z_OK) {
    return {};
  }
  if (dict != nullptr) {
    deflateSetDictionary(&strm, reinterpret_cast<const Bytef*>(dict->data()),
                         static_cast<uInt>(dict->size()));
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

TEST_CASE("A valid zlib wrapper traces its fields and bit spans",
          "[deflate-trace][wp500]") {
  const auto raw = make_raw(64 * 1024);
  const auto stream = zlib_wrap(raw);
  REQUIRE_FALSE(stream.empty());
  REQUIRE(stream.size() >= 8);

  MemoryByteSource source(stream);
  const ZlibWrapperTrace t = trace_zlib_wrapper(source);
  REQUIRE(t.success);
  REQUIRE(t.cm == 8);              // deflate
  REQUIRE(t.cinfo == 7);           // default 32 KiB window
  REQUIRE(t.fcheck_ok);
  REQUIRE_FALSE(t.fdict);
  REQUIRE(t.deflate_data_begin == 2);
  REQUIRE(t.adler_offset.has_value());
  REQUIRE(*t.adler_offset == stream.size() - 4);
  REQUIRE(t.adler_value.has_value());

  // Field spans tile the header and adler.
  REQUIRE(t.fields.size() >= 6);  // CM/CINFO/FCHECK/FDICT/FLEVEL + ADLER32
  // Cross-check with zlib: the same stream inflates and its Adler matches.
  const auto inf = inflate_stream(source, 1u << 20);
  REQUIRE(inf.success);
  REQUIRE(inf.stream_ended);
  REQUIRE(inf.adler_ok);
  REQUIRE(inf.output == raw);
  // The stored Adler equals the one zlib validated (computed by inflate).
  uLong raw_adler = adler32(0L, Z_NULL, 0);
  raw_adler = adler32(raw_adler, reinterpret_cast<const Bytef*>(raw.data()),
                      static_cast<uInt>(raw.size()));
  REQUIRE(*t.adler_value == raw_adler);
}

TEST_CASE("Bad FCHECK is reported and zlib rejects it",
          "[deflate-trace][wp500]") {
  auto stream = zlib_wrap(make_raw(1024));
  REQUIRE_FALSE(stream.empty());
  stream[1] ^= std::byte{0x01};  // corrupt FCHECK bits

  MemoryByteSource source(stream);
  const ZlibWrapperTrace t = trace_zlib_wrapper(source);
  REQUIRE_FALSE(t.success);
  REQUIRE_FALSE(t.fcheck_ok);
  REQUIRE(t.error.find("FCHECK") != std::string::npos);

  // zlib agrees: header check fails.
  const auto inf = inflate_stream(source, 1u << 20);
  REQUIRE_FALSE(inf.success);
}

TEST_CASE("A non-deflate method is rejected", "[deflate-trace][wp500]") {
  auto stream = zlib_wrap(make_raw(1024));
  stream[0] = static_cast<std::byte>(static_cast<unsigned char>(
      (static_cast<unsigned>(stream[0]) & 0xF0u) | 0x07u));  // CM = 7
  MemoryByteSource source(stream);
  const ZlibWrapperTrace t = trace_zlib_wrapper(source);
  REQUIRE_FALSE(t.success);
  REQUIRE(t.error.find("deflate") != std::string::npos);
}

TEST_CASE("FDICT and DICTID are traced", "[deflate-trace][wp500]") {
  const auto dict = make_raw(2048);
  const auto raw = make_raw(4096);
  const auto stream = zlib_wrap(raw, &dict);
  REQUIRE_FALSE(stream.empty());

  MemoryByteSource source(stream);
  const ZlibWrapperTrace t = trace_zlib_wrapper(source);
  REQUIRE(t.success);
  REQUIRE(t.fdict);
  REQUIRE(t.dictid.has_value());
  // zlib records the dictionary as its Adler-32, chained from the standard
  // initial value.
  uLong dict_adler = adler32(0L, Z_NULL, 0);
  dict_adler = adler32(dict_adler, reinterpret_cast<const Bytef*>(dict.data()),
                       static_cast<uInt>(dict.size()));
  REQUIRE(*t.dictid == dict_adler);
  REQUIRE(t.deflate_data_begin == 6);  // 2 header + 4 DICTID bytes
}

TEST_CASE("Corrupt Adler is reported by zlib while the trace shows the field",
          "[deflate-trace][wp500]") {
  const auto raw = make_raw(4096);
  auto stream = zlib_wrap(raw);
  REQUIRE_FALSE(stream.empty());
  uLong good = adler32(0L, Z_NULL, 0);
  good = adler32(good, reinterpret_cast<const Bytef*>(raw.data()),
                 static_cast<uInt>(raw.size()));
  stream[stream.size() - 1] ^= std::byte{0xFF};  // corrupt the last Adler byte

  MemoryByteSource source(stream);
  const ZlibWrapperTrace t = trace_zlib_wrapper(source);
  REQUIRE(t.success);            // structurally parses
  REQUIRE(t.adler_value.has_value());
  REQUIRE(*t.adler_value != good);  // the stored value is the corrupted one

  // zlib cross-check rejects the bad checksum.
  const auto inf = inflate_stream(source, 1u << 20);
  REQUIRE_FALSE(inf.success);
  REQUIRE_FALSE(inf.adler_ok);
}

TEST_CASE("Trailing bytes after the Adler are ignored by zlib",
          "[deflate-trace][wp500]") {
  const auto raw = make_raw(2048);
  auto stream = zlib_wrap(raw);
  stream.insert(stream.end(), {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}});

  MemoryByteSource source(stream);
  const ZlibWrapperTrace t = trace_zlib_wrapper(source);
  REQUIRE(t.success);
  REQUIRE(*t.adler_offset == stream.size() - 4);  // last 4 bytes traced as Adler

  // zlib stops at the real stream end and ignores the trailing bytes.
  const auto inf = inflate_stream(source, 1u << 20);
  REQUIRE(inf.success);
  REQUIRE(inf.stream_ended);
  REQUIRE(inf.adler_ok);
  REQUIRE(inf.output == raw);
}
