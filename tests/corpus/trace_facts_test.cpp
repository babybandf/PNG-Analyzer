// WP-607C trace facts: the four valid DEFLATE cases freeze Block kinds,
// BFINAL flags, DEFLATE-domain input bit ranges, token sequences, cross-check
// the reconstructed output against independently declared raw bytes and prove
// the Dynamic header's code-length repeat instructions with a test-side bit
// reader (no production API is added for this).

#include "controlled_fixture.h"

#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

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
