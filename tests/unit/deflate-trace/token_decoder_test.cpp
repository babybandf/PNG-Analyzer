// WP-501 token decoder tests: stored and fixed-huffman blocks decode into
// literal/length-distance/EOB tokens whose reconstructed output matches the
// source byte-for-byte and is cross-checked with zlib. Dynamic blocks and
// truncated input are rejected.

#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/deflate-runtime/inflate.h>
#include <pnga/io/byte_source.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using pnga::deflate_runtime::inflate_stream;
using pnga::deflate_trace::decode_stored_and_fixed;
using pnga::deflate_trace::TokenDecodeResult;
using pnga::deflate_trace::TokenKind;
using pnga::io::MemoryByteSource;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> make_raw(std::size_t n, unsigned char pattern) {
  std::vector<std::byte> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = B(static_cast<unsigned char>(pattern + (i % 7)));
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
  deflateEnd(&strm);
  if (rc != Z_STREAM_END) {
    return {};
  }
  out.resize(strm.total_out);
  return out;
}

void require_output_matches(const TokenDecodeResult& result,
                            const std::vector<std::byte>& raw) {
  REQUIRE(result.success);
  REQUIRE(result.stream_ended);
  REQUIRE(result.output_bytes == raw.size());
  REQUIRE(result.output == raw);
  // The final token must be an EOB that closed the last block.
  REQUIRE_FALSE(result.tokens.empty());
  REQUIRE(result.tokens.back().kind == TokenKind::kEndOfBlock);
}

}  // namespace

TEST_CASE("Empty block decodes to nothing plus an EOB",
          "[deflate-trace][wp501]") {
  const std::vector<std::byte> raw;
  const auto stream = zlib_compress(raw, 0, Z_DEFAULT_STRATEGY);  // stored
  REQUIRE_FALSE(stream.empty());
  MemoryByteSource source(stream);
  const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
  REQUIRE(result.success);
  REQUIRE(result.output.empty());
  REQUIRE(result.tokens.size() == 1);
  REQUIRE(result.tokens.front().kind == TokenKind::kEndOfBlock);
}

TEST_CASE("Stored blocks decode as byte literals matching zlib",
          "[deflate-trace][wp501]") {
  const auto raw = make_raw(4096, 0x40);
  const auto stream = zlib_compress(raw, 0, Z_DEFAULT_STRATEGY);  // stored
  MemoryByteSource source(stream);
  const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
  require_output_matches(result, raw);
  // Every token is a literal byte; none is a length-distance match.
  REQUIRE(std::all_of(result.tokens.begin(), result.tokens.end(),
                      [](const auto& t) { return t.kind == TokenKind::kLiteral ||
                                                 t.kind == TokenKind::kEndOfBlock; }));

  // zlib agrees on the output.
  const auto inf = inflate_stream(source, 1u << 20);
  REQUIRE(inf.success);
  REQUIRE(inf.output == raw);
}

TEST_CASE("Fixed-huffman blocks produce literals and matches matching zlib",
          "[deflate-trace][wp501]") {
  const auto raw = make_raw(64 * 1024, 0x50);
  const auto stream = zlib_compress(raw, 6, Z_FIXED);  // fixed huffman
  MemoryByteSource source(stream);
  const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
  require_output_matches(result, raw);
  REQUIRE(result.output == raw);

  std::uint64_t output_cursor = 0;
  for (const auto& token : result.tokens) {
    REQUIRE(token.input_bit_begin <= token.input_bit_end);
    REQUIRE(token.output_begin == output_cursor);
    REQUIRE(token.output_begin <= token.output_end);
    if (token.kind == TokenKind::kLiteral) {
      REQUIRE(token.input_bit_begin < token.input_bit_end);
      REQUIRE(token.output_end == token.output_begin + 1);
      output_cursor = token.output_end;
    } else if (token.kind == TokenKind::kLengthDistance) {
      REQUIRE(token.input_bit_begin < token.input_bit_end);
      REQUIRE(token.length >= 3);
      REQUIRE(token.distance >= 1);
      REQUIRE(token.output_end == token.output_begin + token.length);
      REQUIRE(token.match_source_begin < token.output_begin);
      REQUIRE(token.match_source_end > token.match_source_begin);
      output_cursor = token.output_end;
    } else {
      REQUIRE(token.output_end == token.output_begin);
    }
  }
  REQUIRE(output_cursor == raw.size());

  const auto inf = inflate_stream(source, 1u << 20);
  REQUIRE(inf.success);
  REQUIRE(inf.output == raw);
}

TEST_CASE("Repeated data yields maximum-length distance matches",
          "[deflate-trace][wp501]") {
  // A long run of one byte compresses to length-258 distance-1 matches.
  const auto raw = std::vector<std::byte>(200 * 1024, B(0x41));
  const auto stream = zlib_compress(raw, 6, Z_FIXED);
  MemoryByteSource source(stream);
  const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
  require_output_matches(result, raw);

  bool saw_max_length = false;
  bool saw_overlap = false;
  for (const auto& t : result.tokens) {
    if (t.kind == TokenKind::kLengthDistance) {
      saw_max_length = saw_max_length || t.length == 258;
      // distance 1 means each output byte copies its immediate predecessor.
      saw_overlap = saw_overlap || t.distance == 1;
    }
  }
  REQUIRE(saw_max_length);  // deflate emits the maximum 258-byte matches
  REQUIRE(saw_overlap);
}

TEST_CASE("Dynamic huffman blocks are rejected for now",
          "[deflate-trace][wp501]") {
  const auto raw = make_raw(64 * 1024, 0x60);
  const auto stream = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);  // dynamic
  MemoryByteSource source(stream);
  const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
  REQUIRE_FALSE(result.success);
  REQUIRE(result.error.find("dynamic") != std::string::npos);
}

TEST_CASE("Truncated input is rejected with a stable error",
          "[deflate-trace][wp501]") {
  const auto raw = make_raw(4096, 0x70);
  auto stream = zlib_compress(raw, 6, Z_FIXED);
  REQUIRE_FALSE(stream.empty());
  stream.resize(stream.size() - 5);  // cut into the huffman data
  MemoryByteSource source(stream);
  const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
  REQUIRE_FALSE(result.success);
  REQUIRE_FALSE(result.error.empty());
}
