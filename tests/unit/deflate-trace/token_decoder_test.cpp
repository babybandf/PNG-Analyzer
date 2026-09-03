// WP-501/502/503 token decoder tests: stored, fixed- and dynamic-huffman
// blocks decode into literal/length-distance/EOB tokens whose reconstructed
// output matches the source byte-for-byte and is cross-checked with zlib.
// Dynamic table failures, truncated input and LZ source/output boundaries are
// rejected or traced deterministically. WP-607C: the valid controlled corpus
// cases add exact token-sequence assertions over the shared fixture registry
// (local builders stay: they cover sizes and strategies the corpus does not).

#include "controlled_fixture.h"

#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/deflate-runtime/inflate.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

using pnga::deflate_runtime::inflate_stream;
using pnga::deflate_trace::decode_stored_and_fixed;
using pnga::deflate_trace::TokenDecodeResult;
using pnga::deflate_trace::TokenKind;
using pnga::io::IByteSource;
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

class BitWriter {
 public:
  BitWriter() : bytes_{B(0x78), B(0x01)} {}

  void write(std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
      if (bit_pos_ % 8 == 0) {
        bytes_.push_back(B(0));
      }
      if (((value >> i) & 1u) != 0) {
        const unsigned current = static_cast<unsigned>(bytes_.back());
        bytes_.back() = B(static_cast<unsigned char>(
            current | (1u << (bit_pos_ % 8))));
      }
      ++bit_pos_;
    }
  }

  void write_huffman(std::uint64_t canonical_code, unsigned count) {
    std::uint64_t reversed = 0;
    for (unsigned i = 0; i < count; ++i) {
      reversed |= ((canonical_code >> i) & 1u) << (count - 1 - i);
    }
    write(reversed, count);
  }

  std::vector<std::byte> finish() {
    if (bit_pos_ % 8 != 0) {
      write(0, 8 - static_cast<unsigned>(bit_pos_ % 8));
    }
    bytes_.insert(bytes_.end(), 4, B(0));  // Adler-32 is irrelevant here.
    return bytes_;
  }

 private:
  std::vector<std::byte> bytes_;
  std::size_t bit_pos_ = 0;
};

// Models IDAT payloads split at arbitrary byte boundaries. The decoder only
// sees the generic IByteSource contract, while read() exercises a logical
// range that crosses several backing segments.
class SegmentedByteSource final : public IByteSource {
 public:
  explicit SegmentedByteSource(std::vector<std::byte> data,
                               std::size_t segment_size) {
    for (std::size_t begin = 0; begin < data.size(); begin += segment_size) {
      const std::size_t end = std::min(data.size(), begin + segment_size);
      segments_.emplace_back(data.begin() + begin, data.begin() + end);
      total_size_ += end - begin;
    }
  }

  std::uint64_t size() const noexcept override { return total_size_; }

  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    if (out == nullptr && length != 0) {
      return false;
    }
    if (offset > total_size_ ||
        static_cast<std::uint64_t>(length) > total_size_ - offset) {
      return false;
    }
    std::uint64_t cursor = offset;
    std::size_t written = 0;
    for (const auto& segment : segments_) {
      if (cursor >= segment.size()) {
        cursor -= segment.size();
        continue;
      }
      const std::size_t available = segment.size() -
                                    static_cast<std::size_t>(cursor);
      const std::size_t count = std::min(available, length - written);
      std::copy_n(segment.data() + cursor, count, out + written);
      written += count;
      cursor = 0;
      if (written == length) {
        return true;
      }
    }
    return written == length;
  }

  std::optional<pnga::io::ByteView> view(
      std::uint64_t, std::size_t) const noexcept override {
    // A logical IDAT range may span segments, so this source intentionally
    // exposes only the checked read() path.
    return std::nullopt;
  }

 private:
  std::vector<std::vector<std::byte>> segments_;
  std::uint64_t total_size_ = 0;
};

BitWriter dynamic_header(const std::array<unsigned, 4>& code_lengths) {
  BitWriter writer;
  writer.write(1, 1);  // BFINAL
  writer.write(2, 2);  // BTYPE=dynamic
  writer.write(0, 5);  // HLIT=257
  writer.write(0, 5);  // HDIST=1
  writer.write(0, 4);  // HCLEN=4 code-length entries
  for (const unsigned length : code_lengths) {
    writer.write(length, 3);  // symbols 16, 17, 18, 0
  }
  return writer;
}

std::vector<std::byte> literal_only_dynamic_stream() {
  BitWriter writer;
  writer.write(1, 1);   // BFINAL
  writer.write(2, 2);   // BTYPE=dynamic
  writer.write(0, 5);   // HLIT=257
  writer.write(0, 5);   // HDIST=1
  writer.write(15, 4);  // HCLEN=19 code-length entries

  // Code-length symbols 0,1,2,3,4,16,17,18 each have a 3-bit code.
  const std::array<unsigned, 19> lengths = {
      3, 3, 3, 3, 0, 0, 0, 0, 0, 0,
      0, 3, 0, 3, 0, 3, 0, 3, 0};
  for (const unsigned length : lengths) {
    writer.write(length, 3);
  }

  // Literal/length code lengths: 65 zeros, literal 65, 190 zeros, EOB,
  // followed by one zero distance code. Code 18 is canonical 111 (3 bits).
  writer.write_huffman(7, 3);
  writer.write(54, 7);   // repeat 65 zeros
  writer.write_huffman(1, 3);  // code-length value 1
  writer.write_huffman(7, 3);
  writer.write(127, 7);  // repeat 138 zeros
  writer.write_huffman(7, 3);
  writer.write(41, 7);   // repeat 52 zeros
  writer.write_huffman(1, 3);  // code-length value 1 (EOB)
  writer.write_huffman(0, 3);  // one zero distance code length

  writer.write_huffman(0, 1);  // literal 'A' (symbol 65)
  writer.write_huffman(1, 1);  // EOB (symbol 256)
  return writer.finish();
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

TEST_CASE("Output intervals and overlap sources remain traceable",
          "[deflate-trace][wp503]") {
  const auto raw = std::vector<std::byte>(200 * 1024, B(0x41));
  const auto stream = zlib_compress(raw, 6, Z_FIXED);
  MemoryByteSource source(stream);
  const auto result = decode_stored_and_fixed(source, 1u << 20);
  require_output_matches(result, raw);

  const auto output_token_count = static_cast<std::size_t>(std::count_if(
      result.tokens.begin(), result.tokens.end(), [](const auto& token) {
        return token.output_begin < token.output_end;
      }));
  REQUIRE(result.output_index.ranges().size() == output_token_count);
  for (const auto& range : result.output_index.ranges()) {
    REQUIRE(range.begin < range.end);
    REQUIRE(result.output_index.containing(range.begin).has_value());
    const auto containing = result.output_index.containing(range.begin);
    REQUIRE(containing->token_index == range.token_index);
    const auto overlaps = result.output_index.overlapping(range.begin,
                                                           range.end);
    REQUIRE(std::find(overlaps.begin(), overlaps.end(), range) !=
            overlaps.end());
  }

  bool saw_wrapped_match = false;
  for (std::size_t i = 0; i < result.tokens.size(); ++i) {
    const auto& token = result.tokens[i];
    if (token.kind != TokenKind::kLengthDistance) {
      continue;
    }
    if (token.output_begin >= 32768) {
      saw_wrapped_match = true;
    }
    REQUIRE_FALSE(token.match_source_ranges.empty());
    for (const auto& source_range : token.match_source_ranges) {
      REQUIRE(source_range.begin < source_range.end);
      REQUIRE(source_range.end <= token.output_begin);
      REQUIRE(source_range.token_index < i);
      REQUIRE(result.output_index.containing(source_range.begin).has_value());
    }
  }
  REQUIRE(saw_wrapped_match);
}

TEST_CASE("LZ provenance survives arbitrary IDAT-like source splits",
          "[deflate-trace][wp503]") {
  const auto raw = make_raw(96 * 1024, 0x27);
  const auto stream = zlib_compress(raw, 6, Z_FIXED);
  SegmentedByteSource source(stream, 5);
  const auto result = decode_stored_and_fixed(source, 1u << 20);
  require_output_matches(result, raw);

  bool saw_match_after_a_segment_boundary = false;
  for (const auto& token : result.tokens) {
    if (token.kind == TokenKind::kLengthDistance &&
        token.output_begin > 32768 && !token.match_source_ranges.empty()) {
      saw_match_after_a_segment_boundary = true;
      break;
    }
  }
  REQUIRE(saw_match_after_a_segment_boundary);
}

TEST_CASE("Dynamic huffman blocks decode with table provenance",
          "[deflate-trace][wp502]") {
  const auto raw = make_raw(64 * 1024, 0x60);
  const auto stream = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);  // dynamic
  MemoryByteSource source(stream);
  const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
  require_output_matches(result, raw);
  REQUIRE(result.huffman_tables.size() == 3);
  REQUIRE(result.huffman_tables[0].kind ==
          pnga::deflate_trace::HuffmanTableKind::kCodeLength);
  REQUIRE(result.huffman_tables[1].kind ==
          pnga::deflate_trace::HuffmanTableKind::kLiteralLength);
  REQUIRE(result.huffman_tables[2].kind ==
          pnga::deflate_trace::HuffmanTableKind::kDistance);
  for (const auto& table : result.huffman_tables) {
    REQUIRE_FALSE(table.entries.empty());
    for (const auto& entry : table.entries) {
      if (entry.bit_length != 0) {
        REQUIRE(entry.provenance_bit_begin < entry.provenance_bit_end);
        REQUIRE(entry.canonical_code < (1u << entry.bit_length));
      }
    }
  }
}

TEST_CASE("Dynamic table rejects oversubscribed and incomplete code trees",
          "[deflate-trace][wp502]") {
  {
    auto writer = dynamic_header({1, 1, 1, 1});
    MemoryByteSource source(writer.finish());
    const auto result = decode_stored_and_fixed(source, 1u << 20);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.find("oversubscribed") != std::string::npos);
  }
  {
    auto writer = dynamic_header({1, 0, 0, 0});
    MemoryByteSource source(writer.finish());
    const auto result = decode_stored_and_fixed(source, 1u << 20);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.find("incomplete") != std::string::npos);
  }
}

TEST_CASE("Dynamic repeat codes require valid history and bounds",
          "[deflate-trace][wp502]") {
  {
    auto writer = dynamic_header({1, 1, 0, 0});
    writer.write(0, 1);  // code 16, before any previous length
    writer.write(0, 2);
    MemoryByteSource source(writer.finish());
    const auto result = decode_stored_and_fixed(source, 1u << 20);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.find("repeat code 16") != std::string::npos);
  }
  {
    auto writer = dynamic_header({0, 0, 1, 1});
    writer.write(1, 1);  // code 18
    writer.write(127, 7);  // repeat 138 zeros
    writer.write(1, 1);  // another code 18, which exceeds 258 entries
    writer.write(127, 7);
    MemoryByteSource source(writer.finish());
    const auto result = decode_stored_and_fixed(source, 1u << 20);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.find("repeat exceeds") != std::string::npos);
  }
}

TEST_CASE("Dynamic literal-only stream may have an empty distance table",
          "[deflate-trace][wp502]") {
  const std::vector<std::byte> raw = {B(0x41)};
  const auto stream = literal_only_dynamic_stream();
  MemoryByteSource source(stream);
  const auto result = decode_stored_and_fixed(source, 1u << 20);
  require_output_matches(result, raw);
  REQUIRE(result.huffman_tables.size() == 3);
  const auto& distance = result.huffman_tables[2];
  REQUIRE(std::all_of(distance.entries.begin(), distance.entries.end(),
                      [](const auto& entry) {
                        return entry.bit_length == 0;
                      }));
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

TEST_CASE("Fixed-huffman tokens record the consumed huffman symbol",
          "[deflate-trace][wp5u12d]") {
  const auto raw = make_raw(64 * 1024, 0x50);
  const auto stream = zlib_compress(raw, 6, Z_FIXED);
  MemoryByteSource source(stream);
  const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
  require_output_matches(result, raw);
  REQUIRE_FALSE(result.tokens.empty());

  const auto first_literal = std::find_if(
      result.tokens.begin(), result.tokens.end(),
      [](const auto& token) { return token.kind == TokenKind::kLiteral; });
  REQUIRE(first_literal != result.tokens.end());
  // A Huffman literal carries the byte symbol that produced it.
  REQUIRE(first_literal->huffman_symbol ==
          std::optional<std::uint16_t>{first_literal->literal});

  const auto first_match = std::find_if(
      result.tokens.begin(), result.tokens.end(), [](const auto& token) {
        return token.kind == TokenKind::kLengthDistance;
      });
  REQUIRE(first_match != result.tokens.end());
  // A match carries its literal/length symbol 257..285.
  REQUIRE(first_match->huffman_symbol.has_value());
  REQUIRE(*first_match->huffman_symbol >= 257);
  REQUIRE(*first_match->huffman_symbol <= 285);

  REQUIRE(result.tokens.back().kind == TokenKind::kEndOfBlock);
  REQUIRE(result.tokens.back().huffman_symbol ==
          std::optional<std::uint16_t>{256});
}

TEST_CASE("Stored tokens carry no huffman symbol",
          "[deflate-trace][wp5u12d]") {
  const auto raw = make_raw(64, 0x40);
  const auto stream = zlib_compress(raw, 0, Z_DEFAULT_STRATEGY);
  REQUIRE_FALSE(stream.empty());
  MemoryByteSource source(stream);
  const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
  require_output_matches(result, raw);
  REQUIRE(result.tokens.size() > 1);
  for (const auto& token : result.tokens) {
    REQUIRE_FALSE(token.huffman_symbol.has_value());
  }
}

TEST_CASE("Dynamic huffman tokens record the consumed huffman symbol",
          "[deflate-trace][wp5u12d]") {
  // Hand-built literal-only dynamic stream: literal 'A' then EOB.
  {
    const std::vector<std::byte> raw = {B(0x41)};
    MemoryByteSource source(literal_only_dynamic_stream());
    const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
    require_output_matches(result, raw);
    REQUIRE(result.tokens.size() == 2);
    REQUIRE(result.tokens[0].kind == TokenKind::kLiteral);
    REQUIRE(result.tokens[0].literal == 0x41);
    REQUIRE(result.tokens[0].huffman_symbol ==
            std::optional<std::uint16_t>{65});
    REQUIRE(result.tokens[1].kind == TokenKind::kEndOfBlock);
    REQUIRE(result.tokens[1].huffman_symbol ==
            std::optional<std::uint16_t>{256});
  }
  // A real dynamic stream with literals, matches and EOB.
  {
    const auto raw = make_raw(64 * 1024, 0x60);
    const auto stream = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);
    MemoryByteSource source(stream);
    const TokenDecodeResult result = decode_stored_and_fixed(source, 1u << 20);
    require_output_matches(result, raw);
    for (const auto& token : result.tokens) {
      switch (token.kind) {
        case TokenKind::kLiteral:
          REQUIRE(token.huffman_symbol ==
                  std::optional<std::uint16_t>{token.literal});
          break;
        case TokenKind::kLengthDistance:
          REQUIRE(token.huffman_symbol.has_value());
          REQUIRE(*token.huffman_symbol >= 257);
          REQUIRE(*token.huffman_symbol <= 285);
          break;
        case TokenKind::kEndOfBlock:
          REQUIRE(token.huffman_symbol ==
                  std::optional<std::uint16_t>{256});
          break;
      }
    }
  }
}

namespace {

// Adapts a VirtualIDATStream to IByteSource (the corpus cases wrap their
// DEFLATE streams in real PNG files, possibly across several IDAT chunks).
class CorpusIdatSource final : public IByteSource {
 public:
  CorpusIdatSource(const pnga::png_format::VirtualIDATStream& stream,
                   const IByteSource& file)
      : stream_(stream), file_(file) {}

  std::uint64_t size() const noexcept override { return stream_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return stream_.read(file_, offset, out, length);
  }
  std::optional<pnga::io::ByteView> view(std::uint64_t,
                                         std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  const pnga::png_format::VirtualIDATStream& stream_;
  const IByteSource& file_;
};

}  // namespace

TEST_CASE("WP-607C controlled cases decode into the frozen token sequences",
          "[deflate-trace][wp607c]") {
  using pnga_test::wp607c::ControlledCaseId;
  using pnga_test::wp607c::make_controlled_fixture;

  const ControlledCaseId valid_cases[] = {
      ControlledCaseId::kTraceStoredLiterals,
      ControlledCaseId::kTraceFixedNonoverlap,
      ControlledCaseId::kTraceDynamicOverlapRepeats,
      ControlledCaseId::kTraceMultiblockBfinal,
      ControlledCaseId::kIdatSplitZlibHeader,
      ControlledCaseId::kIdatSplitToken,
      ControlledCaseId::kIdatSplitAdler,
  };
  for (const auto id : valid_cases) {
    const auto fixture = make_controlled_fixture(id);
    CAPTURE(fixture.stable_id);

    MemoryByteSource file(fixture.png_bytes);
    const auto chunks = pnga::png_format::index_chunks(file);
    const pnga::png_format::VirtualIDATStream stream(chunks);
    CorpusIdatSource logical(stream, file);
    const TokenDecodeResult result =
        decode_stored_and_fixed(logical, 1u << 20);

    REQUIRE(result.success);
    REQUIRE(result.stream_ended);
    REQUIRE(result.tokens.size() == fixture.expected.tokens.size());
    for (std::size_t i = 0; i < result.tokens.size(); ++i) {
      const auto& produced = result.tokens[i];
      const auto& expected = fixture.expected.tokens[i];
      INFO(fixture.stable_id << " token " << i);
      switch (expected.kind) {
        case pnga_test::wp607c::TokenKind::kLiteral:
          REQUIRE(produced.kind == TokenKind::kLiteral);
          REQUIRE(expected.literal.has_value());
          REQUIRE(produced.literal == *expected.literal);
          break;
        case pnga_test::wp607c::TokenKind::kMatch:
          REQUIRE(produced.kind == TokenKind::kLengthDistance);
          REQUIRE(expected.length.has_value());
          REQUIRE(expected.distance.has_value());
          REQUIRE(produced.length == *expected.length);
          REQUIRE(produced.distance == *expected.distance);
          REQUIRE(expected.match_source.has_value());
          REQUIRE(produced.match_source_begin == expected.match_source->begin);
          REQUIRE(produced.match_source_end == expected.match_source->end);
          break;
        case pnga_test::wp607c::TokenKind::kEndOfBlock:
          REQUIRE(produced.kind == TokenKind::kEndOfBlock);
          break;
      }
      REQUIRE(produced.input_bit_begin == expected.input_bits.begin);
      REQUIRE(produced.input_bit_end == expected.input_bits.end);
      REQUIRE(produced.output_begin == expected.output_bytes.begin);
      REQUIRE(produced.output_end == expected.output_bytes.end);
    }
  }
}
