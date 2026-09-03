// WP-607C trace facts: the four valid DEFLATE cases freeze Block kinds,
// BFINAL flags, DEFLATE-domain input bit ranges, token sequences, cross-check
// the reconstructed output against independently declared raw bytes and prove
// the Dynamic header's code-length repeat instructions with a test-side bit
// reader (no production API is added for this).

#include "controlled_fixture.h"

#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/checksum.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using pnga_test::wp607c::BlockKind;
using pnga_test::wp607c::ControlledCaseId;
using pnga_test::wp607c::ControlledFixture;
using pnga_test::wp607c::TokenKind;
using pnga_test::wp607c::make_controlled_fixture;

constexpr std::uint64_t kZlibHeaderBits = 16;
constexpr std::uint64_t kMaxOutputBytes = 1u << 20;

std::byte B(unsigned int value) { return static_cast<std::byte>(value); }

// Adapts a VirtualIDATStream to IByteSource (the production modules consume a
// generic byte stream and never assume IDAT data is contiguous).
class VirtualIdatSource final : public pnga::io::IByteSource {
 public:
  VirtualIdatSource(const pnga::png_format::VirtualIDATStream& stream,
                    const pnga::io::IByteSource& file)
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
  const pnga::io::IByteSource& file_;
};

struct Production {
  pnga::io::MemoryByteSource source;
  pnga::png_format::ChunkIndex chunks;
  std::optional<pnga::png_format::VirtualIDATStream> stream;
  pnga::deflate_index::BlockIndexResult blocks;
  pnga::deflate_trace::TokenDecodeResult trace;
};

Production decode_fixture(const ControlledFixture& fixture) {
  Production production{pnga::io::MemoryByteSource(fixture.png_bytes), {},
                        std::nullopt, {}, {}};
  production.chunks = pnga::png_format::index_chunks(production.source);
  REQUIRE(production.chunks.valid_signature);
  REQUIRE(production.chunks.issues.empty());
  production.stream.emplace(production.chunks);
  VirtualIdatSource logical(*production.stream, production.source);
  production.blocks = pnga::deflate_index::index_blocks(logical,
                                                        kMaxOutputBytes);
  production.trace =
      pnga::deflate_trace::decode_stored_and_fixed(logical, kMaxOutputBytes);
  return production;
}

pnga::deflate_index::BlockType production_block_type(BlockKind kind) {
  switch (kind) {
    case BlockKind::kStored:
      return pnga::deflate_index::BlockType::kStored;
    case BlockKind::kFixed:
      return pnga::deflate_index::BlockType::kFixed;
    case BlockKind::kDynamic:
      return pnga::deflate_index::BlockType::kDynamic;
    case BlockKind::kReserved:
      break;
  }
  FAIL("production blocks cannot be reserved");
  return pnga::deflate_index::BlockType::kStored;
}

void require_blocks_match(const ControlledFixture& fixture,
                          const Production& production) {
  const auto& index = production.blocks;
  REQUIRE(index.success);
  REQUIRE(index.error.empty());
  REQUIRE(index.zlib_header_bits == kZlibHeaderBits);
  REQUIRE(index.adler.status == pnga::deflate_index::Adler32Status::kMatch);
  REQUIRE_FALSE(index.stop_input_bit.has_value());
  REQUIRE_FALSE(index.stop_output_byte.has_value());
  REQUIRE(index.blocks.size() == fixture.expected.blocks.size());
  for (std::size_t i = 0; i < index.blocks.size(); ++i) {
    const auto& produced = index.blocks[i];
    const auto& expected = fixture.expected.blocks[i];
    INFO("block " << i);
    REQUIRE(produced.index == i);
    REQUIRE(produced.type == production_block_type(expected.kind));
    REQUIRE(produced.last == expected.bfinal);
    REQUIRE(expected.input_bits.begin ==
            produced.input_bit_begin - index.zlib_header_bits);
    REQUIRE(expected.input_bits.end ==
            produced.input_bit_end - index.zlib_header_bits);
    REQUIRE(expected.output_bytes.begin == produced.output_begin);
    REQUIRE(expected.output_bytes.end == produced.output_end);
  }
}

void require_tokens_match(const ControlledFixture& fixture,
                          const Production& production) {
  const auto& tokens = production.trace.tokens;
  REQUIRE(production.trace.success);
  REQUIRE(production.trace.stream_ended);
  REQUIRE(tokens.size() == fixture.expected.tokens.size());
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto& produced = tokens[i];
    const auto& expected = fixture.expected.tokens[i];
    INFO("token " << i);
    switch (expected.kind) {
      case TokenKind::kLiteral:
        REQUIRE(produced.kind == pnga::deflate_trace::TokenKind::kLiteral);
        REQUIRE(expected.literal.has_value());
        REQUIRE(produced.literal == *expected.literal);
        break;
      case TokenKind::kMatch:
        REQUIRE(produced.kind ==
                pnga::deflate_trace::TokenKind::kLengthDistance);
        REQUIRE(expected.length.has_value());
        REQUIRE(expected.distance.has_value());
        REQUIRE(produced.length == *expected.length);
        REQUIRE(produced.distance == *expected.distance);
        REQUIRE(expected.match_source.has_value());
        REQUIRE(produced.match_source_begin == expected.match_source->begin);
        REQUIRE(produced.match_source_end == expected.match_source->end);
        break;
      case TokenKind::kEndOfBlock:
        REQUIRE(produced.kind == pnga::deflate_trace::TokenKind::kEndOfBlock);
        break;
    }
    REQUIRE(expected.input_bits.begin == produced.input_bit_begin);
    REQUIRE(expected.input_bits.end == produced.input_bit_end);
    REQUIRE(expected.output_bytes.begin == produced.output_begin);
    REQUIRE(expected.output_bytes.end == produced.output_end);
  }
}

void require_reconstructed_output(const Production& production,
                                  const std::vector<std::byte>& expected_raw) {
  REQUIRE(production.trace.output == expected_raw);
  REQUIRE(production.trace.output_bytes == expected_raw.size());
  REQUIRE(production.blocks.total_output_bytes == expected_raw.size());
}

// --- independent Dynamic-header reader (test-side, RFC 1951 §3.2.7) ---------

class TestBitReader {
 public:
  TestBitReader(const std::vector<std::byte>& bytes, std::uint64_t bit_limit)
      : bytes_(bytes), bit_limit_(bit_limit) {}

  std::uint64_t pos() const noexcept { return bit_offset_; }

  bool read_lsb(unsigned count, std::uint32_t& out) {
    out = 0;
    for (unsigned i = 0; i < count; ++i) {
      if (bit_offset_ >= bit_limit_) {
        return false;
      }
      const std::size_t byte_index =
          static_cast<std::size_t>(bit_offset_ / 8);
      const unsigned shift = static_cast<unsigned>(bit_offset_ % 8);
      const unsigned bit =
          (std::to_integer<unsigned>(bytes_[byte_index]) >> shift) & 1u;
      out |= static_cast<std::uint32_t>(bit) << i;
      ++bit_offset_;
    }
    return true;
  }

 private:
  const std::vector<std::byte>& bytes_;
  std::uint64_t bit_limit_;
  std::uint64_t bit_offset_ = 0;
};

// Reads the first Dynamic block header of the fixture's logical IDAT stream
// and returns the code-length repeat instructions (16/17/18) in stream order.
std::vector<std::uint8_t> read_code_length_repeats(
    const ControlledFixture& fixture) {
  const pnga::io::MemoryByteSource source(fixture.png_bytes);
  const auto chunks = pnga::png_format::index_chunks(source);
  std::vector<std::byte> idat;
  for (const auto& chunk : chunks.chunks) {
    if (chunk.text() == "IDAT") {
      REQUIRE(idat.empty());  // these cases carry a single IDAT payload
      idat.resize(chunk.data_length);
      REQUIRE(source.read(chunk.data_offset, idat.data(), idat.size()));
    }
  }
  REQUIRE(idat.size() > 4);
  constexpr unsigned kCodeLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                             11, 4, 12, 3, 13, 2, 14, 1, 15};
  TestBitReader reader(idat, static_cast<std::uint64_t>(idat.size()) * 8);
  std::uint32_t value = 0;
  REQUIRE(reader.read_lsb(16, value));  // zlib CMF/FLG
  REQUIRE(reader.read_lsb(1, value));   // BFINAL
  REQUIRE(reader.read_lsb(2, value));   // BTYPE
  REQUIRE(value == 2);                  // dynamic
  REQUIRE(reader.read_lsb(5, value));
  const std::uint32_t literal_count = value + 257;
  REQUIRE(reader.read_lsb(5, value));
  const std::uint32_t distance_count = value + 1;
  REQUIRE(reader.read_lsb(4, value));
  const std::uint32_t code_length_count = value + 4;
  std::vector<std::uint8_t> code_length_lengths(19, 0);
  for (std::uint32_t i = 0; i < code_length_count; ++i) {
    REQUIRE(reader.read_lsb(3, value));
    code_length_lengths[kCodeLengthOrder[i]] =
        static_cast<std::uint8_t>(value);
  }

  // Canonical decode of the code-length alphabet (built independently).
  std::vector<std::size_t> count_by_length(8, 0);
  for (const std::uint8_t length : code_length_lengths) {
    if (length != 0) {
      ++count_by_length[length];
    }
  }
  std::vector<std::uint32_t> first_code(8, 0);
  std::uint32_t next_code = 0;
  for (unsigned length = 1; length < 8; ++length) {
    first_code[length] = next_code;
    next_code = (next_code + count_by_length[length]) << 1;
  }
  std::vector<std::vector<std::uint16_t>> symbols_by_length(8);
  for (std::uint16_t symbol = 0; symbol < 19; ++symbol) {
    if (code_length_lengths[symbol] != 0) {
      symbols_by_length[code_length_lengths[symbol]].push_back(symbol);
    }
  }
  auto decode_symbol = [&]() -> std::optional<std::uint16_t> {
    std::uint32_t symbol_code = 0;
    for (unsigned length = 1; length < 8; ++length) {
      std::uint32_t bit = 0;
      if (!reader.read_lsb(1, bit)) {
        return std::nullopt;
      }
      symbol_code = (symbol_code << 1) | bit;
      if (symbol_code >= first_code[length] &&
          symbol_code - first_code[length] < symbols_by_length[length].size()) {
        return symbols_by_length[length][symbol_code - first_code[length]];
      }
    }
    return std::nullopt;
  };

  std::vector<std::uint8_t> repeats;
  std::size_t total_lengths = 0;
  const std::size_t expected_total = literal_count + distance_count;
  while (total_lengths < expected_total) {
    const auto symbol = decode_symbol();
    REQUIRE(symbol.has_value());
    if (*symbol <= 15) {
      ++total_lengths;
      continue;
    }
    unsigned extra_bits = 0;
    std::size_t minimum = 0;
    if (*symbol == 16) {
      extra_bits = 2;
      minimum = 3;
    } else if (*symbol == 17) {
      extra_bits = 3;
      minimum = 3;
    } else if (*symbol == 18) {
      extra_bits = 7;
      minimum = 11;
    } else {
      FAIL("unexpected code-length symbol");
    }
    REQUIRE(reader.read_lsb(extra_bits, value));
    total_lengths += minimum + value;
    repeats.push_back(static_cast<std::uint8_t>(*symbol));
  }
  return repeats;
}

}  // namespace

TEST_CASE("WP-607C stored case freezes one final Stored block",
          "[wp607c][corpus]") {
  const auto stored =
      make_controlled_fixture(ControlledCaseId::kTraceStoredLiterals);
  REQUIRE(stored.expected.blocks.size() == 1);
  REQUIRE(stored.expected.blocks[0].kind == BlockKind::kStored);
  REQUIRE(stored.expected.blocks[0].bfinal);

  const Production production = decode_fixture(stored);
  REQUIRE(production.blocks.blocks.size() == 1);
  require_blocks_match(stored, production);
  require_tokens_match(stored, production);
  // Independently declared raw bytes: filter byte + two gray pixels per row.
  require_reconstructed_output(
      production, {B(0x00), B(0x41), B(0x42), B(0x00), B(0x43), B(0x44)});
}

TEST_CASE("WP-607C fixed case freezes a non-overlapping match",
          "[wp607c][corpus]") {
  const auto fixed =
      make_controlled_fixture(ControlledCaseId::kTraceFixedNonoverlap);
  const Production production = decode_fixture(fixed);
  require_blocks_match(fixed, production);
  require_tokens_match(fixed, production);
  require_reconstructed_output(
      production, {B(0x00), B(0x41), B(0x42), B(0x00), B(0x41), B(0x42)});

  const auto& nonoverlap = fixed.expected.tokens.at(3);
  REQUIRE(nonoverlap.kind == TokenKind::kMatch);
  REQUIRE(*nonoverlap.distance >= *nonoverlap.length);
}

TEST_CASE("WP-607C dynamic case freezes overlap and code-length repeats",
          "[wp607c][corpus]") {
  const auto dynamic =
      make_controlled_fixture(ControlledCaseId::kTraceDynamicOverlapRepeats);
  const Production production = decode_fixture(dynamic);
  require_blocks_match(dynamic, production);
  require_tokens_match(dynamic, production);
  require_reconstructed_output(production,
                               std::vector<std::byte>(8, B(0x41)));

  const auto& overlap = dynamic.expected.tokens.at(2);
  REQUIRE(overlap.kind == TokenKind::kMatch);
  REQUIRE(*overlap.distance == 1);
  REQUIRE(dynamic.expected.expected_code_length_repeats ==
          std::vector<std::uint8_t>{16, 17, 18});
  REQUIRE(read_code_length_repeats(dynamic) ==
          dynamic.expected.expected_code_length_repeats);
}

TEST_CASE("WP-607C multiblock case freezes three blocks and one BFINAL",
          "[wp607c][corpus]") {
  const auto multiblock =
      make_controlled_fixture(ControlledCaseId::kTraceMultiblockBfinal);
  REQUIRE(multiblock.expected.blocks.size() == 3);
  REQUIRE_FALSE(multiblock.expected.blocks[0].bfinal);
  REQUIRE_FALSE(multiblock.expected.blocks[1].bfinal);
  REQUIRE(multiblock.expected.blocks[2].bfinal);

  const Production production = decode_fixture(multiblock);
  REQUIRE(production.blocks.blocks.size() == 3);
  require_blocks_match(multiblock, production);
  require_tokens_match(multiblock, production);
  require_reconstructed_output(
      production, {B(0x00), B(0x41), B(0x42), B(0x00), B(0x41), B(0x41)});
}

// --- cross-IDAT and malformed cases (Task 4) --------------------------------

namespace {

struct TestChunkFrame {
  std::string type;
  std::uint64_t data_offset = 0;
  std::uint64_t data_length = 0;
};

std::vector<TestChunkFrame> frame_chunks(const std::vector<std::byte>& png) {
  REQUIRE(png.size() >= 8);
  std::vector<TestChunkFrame> frames;
  std::uint64_t pos = 8;
  while (pos + 8 <= png.size()) {
    TestChunkFrame frame;
    std::uint64_t length = 0;
    for (unsigned i = 0; i < 4; ++i) {
      length = (length << 8) | std::to_integer<std::uint64_t>(
                                     png[static_cast<std::size_t>(pos + i)]);
    }
    frame.data_offset = pos + 8;
    frame.data_length = length;
    for (unsigned i = 0; i < 4; ++i) {
      frame.type.push_back(static_cast<char>(std::to_integer<unsigned char>(
          png[static_cast<std::size_t>(pos + 4 + i)])));
    }
    frames.push_back(std::move(frame));
    pos = frame.data_offset + frame.data_length + 4;
  }
  REQUIRE(pos == png.size());
  return frames;
}

void require_fixture_crcs_valid_except(
    const ControlledFixture& fixture,
    const std::optional<std::string>& mismatched_type) {
  const pnga::io::MemoryByteSource source(fixture.png_bytes);
  const auto chunks = pnga::png_format::index_chunks(source);
  REQUIRE(chunks.valid_signature);
  int mismatches = 0;
  for (const auto& node : chunks.chunks) {
    const auto calculated =
        pnga::png_format::calculate_chunk_crc(source, node);
    const auto stored = pnga::png_format::read_chunk_crc(source, node);
    REQUIRE(calculated.has_value());
    REQUIRE(stored.has_value());
    if (*calculated != *stored) {
      ++mismatches;
      REQUIRE(node.text() == mismatched_type);
    }
  }
  REQUIRE(mismatches == (mismatched_type.has_value() ? 1 : 0));
}

std::vector<std::uint64_t> idat_payload_lengths(
    const ControlledFixture& fixture) {
  std::vector<std::uint64_t> lengths;
  for (const auto& frame : frame_chunks(fixture.png_bytes)) {
    if (frame.type == "IDAT") {
      lengths.push_back(frame.data_length);
    }
  }
  return lengths;
}

// Maps a logical range through the production VirtualIDATStream and requires
// every returned range to equal the fixture's physical span facts.
void require_physical_spans_match(const ControlledFixture& fixture,
                                  std::uint64_t logical_offset,
                                  std::uint64_t length) {
  const pnga::io::MemoryByteSource source(fixture.png_bytes);
  const auto chunks = pnga::png_format::index_chunks(source);
  const pnga::png_format::VirtualIDATStream stream(chunks);
  std::vector<pnga::png_format::PhysicalRange> spans;
  REQUIRE(stream.logical_to_physical(logical_offset, length, spans));
  REQUIRE(spans.size() == fixture.expected.physical_spans.size());
  for (std::size_t i = 0; i < spans.size(); ++i) {
    INFO("span " << i);
    REQUIRE(spans[i].offset == fixture.expected.physical_spans[i].begin);
    REQUIRE(spans[i].length ==
            fixture.expected.physical_spans[i].end -
                fixture.expected.physical_spans[i].begin);
  }
}

}  // namespace

TEST_CASE("WP-607C header split maps the zlib header onto two IDATs",
          "[wp607c][corpus]") {
  const auto header_split =
      make_controlled_fixture(ControlledCaseId::kIdatSplitZlibHeader);
  // Split between CMF and FLG: payload lengths {1, remaining}.
  REQUIRE(idat_payload_lengths(header_split).size() == 2);
  REQUIRE(idat_payload_lengths(header_split)[0] == 1);
  // Complete PNG with valid per-chunk CRCs.
  require_fixture_crcs_valid_except(header_split, std::nullopt);
  // Two ordered file spans cover the logical zlib header.
  require_physical_spans_match(header_split, 0, 2);

  // The split stream still verifies end-to-end with the base block facts.
  const Production production = decode_fixture(header_split);
  const auto base = make_controlled_fixture(ControlledCaseId::kTraceStoredLiterals);
  REQUIRE(production.blocks.blocks.size() == 1);
  require_blocks_match(header_split, production);
  REQUIRE(header_split.expected.blocks == base.expected.blocks);
  require_tokens_match(header_split, production);
}

TEST_CASE("WP-607C token split keeps every ordered file span of the token",
          "[wp607c][corpus]") {
  const auto token_split =
      make_controlled_fixture(ControlledCaseId::kIdatSplitToken);
  REQUIRE(idat_payload_lengths(token_split).size() == 2);
  require_fixture_crcs_valid_except(token_split, std::nullopt);

  // Token 0 ('A') covers logical bits [19,27) -> bytes [2,4), so the split
  // at byte 3 falls inside the token's compressed byte coverage.
  const auto& token = token_split.expected.tokens.at(0);
  const std::uint64_t coverage_begin = (token.input_bits.begin + kZlibHeaderBits) / 8;
  const std::uint64_t coverage_end =
      (token.input_bits.end + kZlibHeaderBits + 7) / 8;
  REQUIRE(coverage_begin == 2);
  REQUIRE(coverage_end == 4);
  require_physical_spans_match(token_split, 2, 2);

  const Production production = decode_fixture(token_split);
  const auto base = make_controlled_fixture(ControlledCaseId::kTraceFixedNonoverlap);
  require_blocks_match(token_split, production);
  require_tokens_match(token_split, production);
  REQUIRE(token_split.expected.tokens == base.expected.tokens);
}

TEST_CASE("WP-607C adler split maps the trailer 2+2 and still verifies",
          "[wp607c][corpus]") {
  const auto adler_split =
      make_controlled_fixture(ControlledCaseId::kIdatSplitAdler);
  const auto lengths = idat_payload_lengths(adler_split);
  REQUIRE(lengths.size() == 2);
  REQUIRE(lengths[1] == 2);  // split after the second Adler byte
  require_fixture_crcs_valid_except(adler_split, std::nullopt);
  require_physical_spans_match(adler_split, 13, 4);

  const Production production = decode_fixture(adler_split);
  REQUIRE(production.blocks.adler.status ==
          pnga::deflate_index::Adler32Status::kMatch);
  const auto base = make_controlled_fixture(ControlledCaseId::kTraceStoredLiterals);
  require_blocks_match(adler_split, production);
  REQUIRE(adler_split.expected.blocks == base.expected.blocks);
}

TEST_CASE("WP-607C malformed cases freeze the stable diagnostic registry",
          "[wp607c][corpus]") {
  const auto truncated_header =
      make_controlled_fixture(ControlledCaseId::kErrorTruncatedHeader);
  REQUIRE(truncated_header.expected.error->decoder_message ==
          "truncated block header");
  const auto truncated_token =
      make_controlled_fixture(ControlledCaseId::kErrorTruncatedToken);
  REQUIRE(truncated_token.expected.error->decoder_message ==
          "truncated huffman code");
  const auto reserved =
      make_controlled_fixture(ControlledCaseId::kErrorReservedBtype);
  REQUIRE(reserved.expected.error->decoder_message ==
          "reserved deflate block type");
  REQUIRE(reserved.expected.error->stop_input_bit == 19);
  REQUIRE(reserved.expected.error->stop_output_byte == 0);
  const auto invalid_distance =
      make_controlled_fixture(ControlledCaseId::kErrorInvalidDistance);
  REQUIRE(invalid_distance.expected.error->decoder_message ==
          "distance beyond available output");
  const auto crc =
      make_controlled_fixture(ControlledCaseId::kErrorCrcMismatch);
  REQUIRE(crc.expected.error->validation_rule_id == "chunk_crc_mismatch");
  const auto adler =
      make_controlled_fixture(ControlledCaseId::kErrorAdlerMismatch);
  REQUIRE(adler.expected.error->validation_rule_id == "idat_adler_mismatch");
}

namespace {

void require_decoder_error(const ControlledFixture& fixture) {
  const auto& error = *fixture.expected.error;
  REQUIRE(error.decoder_message.has_value());
  const Production production = decode_fixture(fixture);
  REQUIRE_FALSE(production.trace.success);
  REQUIRE(production.trace.error == *error.decoder_message);
  // The block index stops at the exact logical cursors frozen in the fixture.
  REQUIRE(production.blocks.stop_input_bit == error.stop_input_bit);
  REQUIRE(production.blocks.stop_output_byte == error.stop_output_byte);
  require_fixture_crcs_valid_except(fixture, std::nullopt);
}

}  // namespace

TEST_CASE("WP-607C truncated header stops before the block header completes",
          "[wp607c][corpus]") {
  const auto truncated_header =
      make_controlled_fixture(ControlledCaseId::kErrorTruncatedHeader);
  require_decoder_error(truncated_header);
}

TEST_CASE("WP-607C truncated token retains the verified prefix only",
          "[wp607c][corpus]") {
  const auto truncated_token =
      make_controlled_fixture(ControlledCaseId::kErrorTruncatedToken);
  require_decoder_error(truncated_token);
}

TEST_CASE("WP-607C reserved BTYPE stops after three header bits",
          "[wp607c][corpus]") {
  const auto reserved =
      make_controlled_fixture(ControlledCaseId::kErrorReservedBtype);
  require_decoder_error(reserved);
  // Both production front ends agree on the reserved-type diagnostic.
  const Production production = decode_fixture(reserved);
  REQUIRE(production.blocks.error == "reserved deflate block type");
}

TEST_CASE("WP-607C invalid distance fails with one output byte produced",
          "[wp607c][corpus]") {
  const auto invalid_distance =
      make_controlled_fixture(ControlledCaseId::kErrorInvalidDistance);
  require_decoder_error(invalid_distance);
  const Production production = decode_fixture(invalid_distance);
  REQUIRE(production.trace.output.size() == 1);
  REQUIRE(production.trace.output[0] == B(0x41));
}

TEST_CASE("WP-607C CRC mismatch isolates the chunk CRC fault",
          "[wp607c][corpus]") {
  const auto crc = make_controlled_fixture(ControlledCaseId::kErrorCrcMismatch);
  // Only the IDAT CRC mismatches; the payload and every other chunk stay
  // intact, so parsing and decoding remain fully valid.
  require_fixture_crcs_valid_except(crc, "IDAT");
  const Production production = decode_fixture(crc);
  REQUIRE(production.blocks.success);
  REQUIRE(production.blocks.adler.status ==
          pnga::deflate_index::Adler32Status::kMatch);
  const std::vector<std::byte> base_output = {B(0x00), B(0x41), B(0x42),
                                              B(0x00), B(0x43), B(0x44)};
  REQUIRE(production.trace.output == base_output);
}

TEST_CASE("WP-607C adler mismatch keeps verified blocks and a valid chunk CRC",
          "[wp607c][corpus]") {
  const auto adler =
      make_controlled_fixture(ControlledCaseId::kErrorAdlerMismatch);
  require_fixture_crcs_valid_except(adler, std::nullopt);
  const Production production = decode_fixture(adler);
  // The token decoder ignores the trailer and still reconstructs everything.
  REQUIRE(production.trace.success);
  REQUIRE(production.blocks.adler.status ==
          pnga::deflate_index::Adler32Status::kMismatch);
  REQUIRE(production.blocks.adler.expected.has_value());
  REQUIRE(production.blocks.adler.actual.has_value());
  REQUIRE(*production.blocks.adler.expected != *production.blocks.adler.actual);
  const std::vector<std::byte> base_output = {B(0x00), B(0x41), B(0x42),
                                              B(0x00), B(0x43), B(0x44)};
  REQUIRE(production.trace.output == base_output);
}
