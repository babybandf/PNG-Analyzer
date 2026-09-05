// WP-5T0A trace query contract tests: bounded composition, stable statuses,
// logical/physical bit provenance, deterministic serialization and partial
// results when either the fast index or the replay is unavailable.

#include <pnga/analysis-engine/trace_query.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "test_png_helpers.h"

using pnga::analysis_engine::TraceQueryStatus;
using pnga::analysis_engine::compose_trace_query;
using pnga::analysis_engine::serialize_trace_query;
using pnga::deflate_index::BlockIndexResult;
using pnga::deflate_index::index_blocks;
using pnga::deflate_trace::TokenDecodeResult;
using pnga::deflate_trace::decode_stored_and_fixed;
using pnga::io::IByteSource;
using pnga::io::MemoryByteSource;
using pnga::png_format::VirtualIDATStream;
using pnga::png_format::index_chunks;
using pnga::trace_model::Selection;

namespace {

class VirtualSource final : public IByteSource {
 public:
  VirtualSource(const VirtualIDATStream& stream, const IByteSource& file)
      : stream_(stream), file_(file) {}

  std::uint64_t size() const noexcept override { return stream_.size(); }

  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return stream_.read(file_, offset, out, length);
  }

  std::optional<pnga::io::ByteView> view(
      std::uint64_t, std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  const VirtualIDATStream& stream_;
  const IByteSource& file_;
};

struct TraceInputs {
  MemoryByteSource file;
  pnga::png_format::ChunkIndex chunks;
  VirtualIDATStream stream;
  VirtualSource logical;
  BlockIndexResult blocks;
  TokenDecodeResult trace;

  explicit TraceInputs(std::vector<std::byte> bytes)
      : file(std::move(bytes)),
        chunks(index_chunks(file)),
        stream(chunks),
        logical(stream, file),
        blocks(index_blocks(logical, 1u << 20)),
        trace(decode_stored_and_fixed(logical, 1u << 20)) {}

  explicit TraceInputs(const pnga_test::EncodedPng& encoded)
      : TraceInputs(encoded.png_bytes) {}
};

// A one-IDAT PNG whose zlib stream is a single final stored block over the
// given raw bytes: BFINAL=1/BTYPE=00, LEN/NLEN, payload, Adler-32.
std::vector<std::byte> stored_block_png(const std::vector<std::byte>& raw) {
  std::vector<std::byte> payload;
  payload.push_back(pnga_test::B(0x78));
  payload.push_back(pnga_test::B(0x01));
  payload.push_back(pnga_test::B(0x01));  // BFINAL=1, BTYPE=00, aligned
  REQUIRE(raw.size() <= 0xFFFFu);
  const std::uint16_t len = static_cast<std::uint16_t>(raw.size());
  const std::uint16_t nlen = static_cast<std::uint16_t>(~len & 0xFFFFu);
  payload.push_back(pnga_test::B(static_cast<unsigned char>(len & 0xFF)));
  payload.push_back(pnga_test::B(static_cast<unsigned char>(len >> 8)));
  payload.push_back(pnga_test::B(static_cast<unsigned char>(nlen & 0xFF)));
  payload.push_back(pnga_test::B(static_cast<unsigned char>(nlen >> 8)));
  payload.insert(payload.end(), raw.begin(), raw.end());
  const uLong adler =
      adler32(adler32(0, Z_NULL, 0),
              reinterpret_cast<const Bytef*>(raw.data()),
              static_cast<uInt>(raw.size()));
  for (int shift = 24; shift >= 0; shift -= 8) {
    payload.push_back(pnga_test::B(
        static_cast<unsigned char>((adler >> shift) & 0xFFu)));
  }

  std::vector<std::byte> png(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  const auto append_chunk = [&png](const char* type,
                                   const std::vector<std::byte>& data) {
    const auto length = static_cast<std::uint32_t>(data.size());
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length)));
    const auto type_bytes = reinterpret_cast<const Bytef*>(type);
    uLong crc = crc32(0, type_bytes, 4);
    if (!data.empty()) {
      crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uInt>(data.size()));
    }
    for (int i = 0; i < 4; ++i) {
      png.push_back(pnga_test::B(static_cast<unsigned char>(type[i])));
    }
    png.insert(png.end(), data.begin(), data.end());
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc)));
  };
  std::vector<std::byte> ihdr(13, std::byte{0});
  ihdr[0] = pnga_test::B(0);  // 1x1 grayscale placeholder IHDR
  ihdr[3] = pnga_test::B(1);
  ihdr[8] = pnga_test::B(8);
  append_chunk("IHDR", ihdr);
  append_chunk("IDAT", payload);
  append_chunk("IEND", {});
  return png;
}

// A one-IDAT fixture with two stored blocks ("A" not final, "B" final). The
// zlib stream is 78 01 | 00 01 00 FE FF 41 | 01 01 00 FE FF 42 | Adler-32;
// the block boundary between them carries an empty DEFLATE bit range.
std::vector<std::byte> two_stored_blocks_png() {
  const std::vector<std::byte> output = {pnga_test::B(0x41), pnga_test::B(0x42)};
  std::vector<std::byte> zlib = {pnga_test::B(0x78), pnga_test::B(0x01)};
  const std::vector<std::byte> stored_header = {
      pnga_test::B(0x00), pnga_test::B(0x01), pnga_test::B(0x00),
      pnga_test::B(0xFE), pnga_test::B(0xFF)};
  const auto push_block = [&zlib, &stored_header](std::byte flag,
                                                  std::byte value) {
    zlib.push_back(flag);
    zlib.insert(zlib.end(), stored_header.begin() + 1, stored_header.end());
    zlib.push_back(value);
  };
  push_block(pnga_test::B(0x00), pnga_test::B(0x41));  // not final, "A"
  push_block(pnga_test::B(0x01), pnga_test::B(0x42));  // final, "B"
  const uLong adler =
      adler32(adler32(0, Z_NULL, 0),
              reinterpret_cast<const Bytef*>(output.data()),
              static_cast<uInt>(output.size()));
  for (int shift = 24; shift >= 0; shift -= 8) {
    zlib.push_back(pnga_test::B(
        static_cast<unsigned char>((adler >> shift) & 0xFFu)));
  }

  std::vector<std::byte> png(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  const auto append_chunk = [&png](const char* type,
                                   const std::vector<std::byte>& data) {
    const auto length = static_cast<std::uint32_t>(data.size());
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length)));
    const auto type_bytes = reinterpret_cast<const Bytef*>(type);
    uLong crc = crc32(0, type_bytes, 4);
    if (!data.empty()) {
      crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uInt>(data.size()));
    }
    for (int i = 0; i < 4; ++i) {
      png.push_back(pnga_test::B(static_cast<unsigned char>(type[i])));
    }
    png.insert(png.end(), data.begin(), data.end());
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc)));
  };
  std::vector<std::byte> ihdr(13, std::byte{0});
  ihdr[0] = pnga_test::B(0);  // 1x1 grayscale placeholder IHDR
  ihdr[3] = pnga_test::B(1);
  ihdr[8] = pnga_test::B(8);
  append_chunk("IHDR", ihdr);
  append_chunk("IDAT", zlib);
  append_chunk("IEND", {});
  return png;
}

// A one-PNG fixture whose zlib stream is a final fixed-Huffman block over
// "ABC", split across two IDAT chunks after zlib byte 2. Payload bit layout
// (BFINAL=1, BTYPE=01, then MSB-first codes 0x71, 0x72, 0x73 and the 7-bit
// end-of-block code 0000000) packs into bytes 73 74 72 06 00.
std::vector<std::byte> fixed_abc_two_idat_png() {
  const std::vector<std::byte> output = {pnga_test::B(0x41), pnga_test::B(0x42),
                                         pnga_test::B(0x43)};
  std::vector<std::byte> zlib = {pnga_test::B(0x78), pnga_test::B(0x01),
                                 pnga_test::B(0x73), pnga_test::B(0x74),
                                 pnga_test::B(0x72), pnga_test::B(0x06),
                                 pnga_test::B(0x00)};
  const uLong adler =
      adler32(adler32(0, Z_NULL, 0),
              reinterpret_cast<const Bytef*>(output.data()),
              static_cast<uInt>(output.size()));
  for (int shift = 24; shift >= 0; shift -= 8) {
    zlib.push_back(pnga_test::B(
        static_cast<unsigned char>((adler >> shift) & 0xFFu)));
  }

  std::vector<std::byte> png(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  const auto append_chunk = [&png](const char* type,
                                   const std::vector<std::byte>& data) {
    const auto length = static_cast<std::uint32_t>(data.size());
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length)));
    const auto type_bytes = reinterpret_cast<const Bytef*>(type);
    uLong crc = crc32(0, type_bytes, 4);
    if (!data.empty()) {
      crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uInt>(data.size()));
    }
    for (int i = 0; i < 4; ++i) {
      png.push_back(pnga_test::B(static_cast<unsigned char>(type[i])));
    }
    png.insert(png.end(), data.begin(), data.end());
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc)));
  };
  std::vector<std::byte> ihdr(13, std::byte{0});
  ihdr[0] = pnga_test::B(0);  // 1x1 grayscale placeholder IHDR
  ihdr[3] = pnga_test::B(1);
  ihdr[8] = pnga_test::B(8);
  append_chunk("IHDR", ihdr);
  // Split after zlib byte 2: the first literal's byte envelope [2, 4)
  // crosses the chunk boundary.
  append_chunk("IDAT", std::vector<std::byte>(zlib.begin(), zlib.begin() + 3));
  append_chunk("IDAT", std::vector<std::byte>(zlib.begin() + 3, zlib.end()));
  append_chunk("IEND", {});
  return png;
}

std::vector<std::byte> split_idat_payload(const pnga_test::EncodedPng& encoded) {
  MemoryByteSource original(encoded.png_bytes);
  const auto index = index_chunks(original);
  std::vector<std::byte> ihdr;
  std::vector<std::byte> compressed;
  for (const auto& chunk : index.chunks) {
    if (chunk.text() == "IHDR") {
      ihdr.resize(static_cast<std::size_t>(chunk.data_length));
      REQUIRE(original.read(chunk.data_offset, ihdr.data(), ihdr.size()));
    } else if (chunk.text() == "IDAT") {
      const auto old_size = compressed.size();
      compressed.resize(old_size + static_cast<std::size_t>(chunk.data_length));
      REQUIRE(original.read(chunk.data_offset, compressed.data() + old_size,
                            static_cast<std::size_t>(chunk.data_length)));
    }
  }
  REQUIRE_FALSE(ihdr.empty());
  REQUIRE(compressed.size() > 2);

  std::vector<std::byte> png(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  const auto append_chunk = [&png](const char* type,
                                   const std::vector<std::byte>& data) {
    const auto length = static_cast<std::uint32_t>(data.size());
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length)));
    const auto type_bytes = reinterpret_cast<const Bytef*>(type);
    uLong crc = crc32(0, type_bytes, 4);
    if (!data.empty()) {
      crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uInt>(data.size()));
    }
    for (int i = 0; i < 4; ++i) {
      png.push_back(pnga_test::B(static_cast<unsigned char>(type[i])));
    }
    png.insert(png.end(), data.begin(), data.end());
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc)));
  };
  append_chunk("IHDR", ihdr);
  const auto split = compressed.size() / 2;
  append_chunk("IDAT", std::vector<std::byte>(compressed.begin(),
                                                compressed.begin() + split));
  append_chunk("IDAT", std::vector<std::byte>(compressed.begin() + split,
                                                compressed.end()));
  append_chunk("IEND", {});
  return png;
}

}  // namespace

TEST_CASE("Trace query status text is stable", "[analysis-engine][wp5t0a]") {
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kNotIndexed)) == "not indexed");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kReplaying)) == "replaying");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kReady)) == "ready");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kPartial)) == "partial");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kError)) == "error");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kCancelled)) == "cancelled");
}

TEST_CASE("Trace query composes a ready bounded result",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(64, 32, 8, 2, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(encoded);
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);

  Selection selection;
  selection.stage = pnga::trace_model::Stage::kTrace;
  const auto result = compose_trace_query(
      9, selection, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  REQUIRE_FALSE(result.blocks.empty());
  REQUIRE_FALSE(result.tokens.empty());
  REQUIRE_FALSE(result.logical_input.empty());
  REQUIRE_FALSE(result.physical_input.empty());
  REQUIRE(result.output_bytes == inputs.trace.output_bytes);
  REQUIRE(std::all_of(result.tokens.begin(), result.tokens.end(),
                       [](const auto& token) {
                         return token.input_bit_begin <= token.input_bit_end &&
                                token.output_begin <= token.output_end;
                       }));
}

TEST_CASE("Trace query preserves physical spans across multiple IDAT chunks",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(64, 32, 8, 2, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(split_idat_payload(encoded));
  REQUIRE(inputs.stream.segment_count() == 2);
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);
  const auto result = compose_trace_query(
      3, Selection{}, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  REQUIRE(result.physical_input.size() >= 2);
}

TEST_CASE("Trace query preserves verified data as a partial result",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(32, 32, 8, 0, /*interlace=*/false,
                            /*all_none=*/true);
  TraceInputs inputs(encoded);
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);

  auto unavailable_trace = inputs.trace;
  unavailable_trace.success = false;
  unavailable_trace.error = "replay stopped at block boundary";
  const auto partial = compose_trace_query(
      4, Selection{}, inputs.blocks, unavailable_trace, inputs.stream,
      inputs.file, 0, inputs.blocks.total_output_bytes, 64);
  REQUIRE(partial.status == TraceQueryStatus::kPartial);
  REQUIRE(partial.error == "replay stopped at block boundary");
  REQUIRE_FALSE(partial.blocks.empty());
  // A failed replay can still carry verified prefix artifacts; those are
  // retained so the UI can render a useful partial trace.
  REQUIRE_FALSE(partial.tokens.empty());

  auto unavailable_index = inputs.blocks;
  unavailable_index.success = false;
  unavailable_index.error = "trace index not available";
  const auto index_partial = compose_trace_query(
      4, Selection{}, unavailable_index, inputs.trace, inputs.stream,
      inputs.file, 0, inputs.trace.output_bytes, 64);
  REQUIRE(index_partial.status == TraceQueryStatus::kPartial);
  REQUIRE(index_partial.error == "trace index not available");
  // A failed index may still carry verified block ranges; the contract keeps
  // those ranges visible while marking the result partial.
  REQUIRE_FALSE(index_partial.blocks.empty());
  REQUIRE_FALSE(index_partial.tokens.empty());
}

TEST_CASE("Trace token budget and invalid ranges have stable errors",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(64, 32, 8, 2, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(encoded);
  const auto partial = compose_trace_query(
      1, Selection{}, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 1);
  REQUIRE(partial.status == TraceQueryStatus::kPartial);
  REQUIRE(partial.truncated);
  REQUIRE(partial.tokens.size() == 1);
  REQUIRE(partial.error == "trace token budget exceeded");

  const auto invalid = compose_trace_query(
      1, Selection{}, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      inputs.trace.output_bytes, 0, 1);
  REQUIRE(invalid.status == TraceQueryStatus::kError);
  REQUIRE(invalid.error == "inflated query range is out of bounds");

  const auto zero_budget = compose_trace_query(
      1, Selection{}, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 0);
  REQUIRE(zero_budget.status == TraceQueryStatus::kError);
  REQUIRE(zero_budget.error == "trace token budget must be non-zero");
}

TEST_CASE("Trace query serialization is deterministic and complete",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(32, 16, 8, 6, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(encoded);
  Selection selection;
  selection.node = 17;
  selection.stage = pnga::trace_model::Stage::kTrace;
  const auto result = compose_trace_query(
      11, selection, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 8);
  const auto first = serialize_trace_query(result);
  const auto second = serialize_trace_query(result);
  REQUIRE(first == second);
  REQUIRE(first.starts_with("trace-query-v1\nstatus:partial\n"));
  REQUIRE(first.find("generation:11\n") != std::string::npos);
  REQUIRE(first.find("selection:node:17;stage:trace\n") !=
          std::string::npos);
  REQUIRE(first.find("blocks:") != std::string::npos);
  REQUIRE(first.find("tokens:") != std::string::npos);
  REQUIRE(first.find("tables:") != std::string::npos);
  REQUIRE(first.find("logical:") != std::string::npos);
  REQUIRE(first.find("physical:") != std::string::npos);
  REQUIRE(first.find("matches:") != std::string::npos);
}

TEST_CASE("Trace query serialization names every Deflate block type",
          "[analysis-engine][wp5t0a]") {
  pnga::analysis_engine::TraceQueryResult result;
  result.status = TraceQueryStatus::kReady;
  result.blocks = {
      {0, pnga::deflate_index::BlockType::kStored, false, 0, 8, 0, 1, {}},
      {1, pnga::deflate_index::BlockType::kFixed, false, 8, 16, 1, 2, {}},
      {2, pnga::deflate_index::BlockType::kDynamic, true, 16, 24, 2, 3, {}},
  };
  const auto serialized = serialize_trace_query(result);
  REQUIRE(serialized.find("block:0,stored,") != std::string::npos);
  REQUIRE(serialized.find("block:1,fixed,") != std::string::npos);
  REQUIRE(serialized.find("block:2,dynamic,") != std::string::npos);
}

TEST_CASE("Trace query preserves the bounded huffman symbol",
          "[analysis-engine][wp5u12d]") {
  const auto encoded =
      pnga_test::encode_png(64, 32, 8, 2, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(encoded);
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);
  Selection selection;
  selection.stage = pnga::trace_model::Stage::kTrace;
  const auto result = compose_trace_query(
      9, selection, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  REQUIRE_FALSE(result.tokens.empty());
  bool saw_literal = false;
  for (const auto& token : result.tokens) {
    switch (token.kind) {
      case pnga::deflate_trace::TokenKind::kLiteral:
        saw_literal = true;
        REQUIRE(token.huffman_symbol ==
                std::optional<std::uint16_t>{token.literal});
        break;
      case pnga::deflate_trace::TokenKind::kLengthDistance:
        REQUIRE(token.huffman_symbol.has_value());
        REQUIRE(*token.huffman_symbol >= 257);
        REQUIRE(*token.huffman_symbol <= 285);
        break;
      case pnga::deflate_trace::TokenKind::kEndOfBlock:
        REQUIRE(token.huffman_symbol ==
                std::optional<std::uint16_t>{256});
        break;
    }
  }
  REQUIRE(saw_literal);

  // Partial truncation keeps the symbol of every returned token.
  const auto partial = compose_trace_query(
      9, selection, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 1);
  REQUIRE(partial.status == TraceQueryStatus::kPartial);
  REQUIRE(partial.tokens.size() == 1);
  REQUIRE(partial.tokens.front().huffman_symbol.has_value());
  REQUIRE(partial.tokens.front().huffman_symbol ==
          result.tokens.front().huffman_symbol);
}

TEST_CASE("Trace query maps every token to exact physical file spans",
          "[analysis-engine][wp5u12e]") {
  // One IDAT: two final/non-final stored blocks over "A" and "B". The IDAT
  // data begins at file offset 41 (signature 8 + IHDR chunk 25). Stored
  // literal i reads DEFLATE bits [40+8i, 48+8i) relative to its own block
  // header, so its one-byte input envelope is the zlib byte holding it.
  TraceInputs inputs(two_stored_blocks_png());
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);
  REQUIRE(inputs.trace.tokens.size() == 4);
  Selection selection;
  selection.stage = pnga::trace_model::Stage::kTrace;
  const auto result = compose_trace_query(
      7, selection, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  // Literal 'A', the block boundary (empty input range) and literal 'B' all
  // intersect the requested output range; the final boundary sits at the
  // exclusive query end and is retained by the closed-window EOB rule.
  REQUIRE(result.tokens.size() == 4);
  REQUIRE(result.tokens.back().kind ==
          pnga::deflate_trace::TokenKind::kEndOfBlock);
  REQUIRE(result.tokens.back().output_begin == 2);
  REQUIRE(result.tokens.back().output_end == 2);
  const auto& literal_a = result.tokens[0];
  REQUIRE(literal_a.kind == pnga::deflate_trace::TokenKind::kLiteral);
  REQUIRE(literal_a.literal == 65);
  REQUIRE(literal_a.input_range() ==
          (pnga::trace_model::DeflateBitRange{
              pnga::trace_model::DeflateBitOffset{40},
              pnga::trace_model::DeflateBitOffset{48}}));
  REQUIRE(literal_a.physical_input_spans.size() == 1);
  REQUIRE(literal_a.physical_input_spans.front() ==
          (pnga::trace_model::FileByteRange{
              pnga::trace_model::FileByteOffset{48},
              pnga::trace_model::FileByteOffset{49}}));
  // The stored block boundary consumes no bits and owns no data bytes.
  const auto& boundary = result.tokens[1];
  REQUIRE(boundary.kind == pnga::deflate_trace::TokenKind::kEndOfBlock);
  REQUIRE(boundary.input_range().empty());
  REQUIRE(boundary.physical_input_spans.empty());
  const auto& literal_b = result.tokens[2];
  REQUIRE(literal_b.literal == 66);
  REQUIRE(literal_b.physical_input_spans.size() == 1);
  REQUIRE(literal_b.physical_input_spans.front() ==
          (pnga::trace_model::FileByteRange{
              pnga::trace_model::FileByteOffset{54},
              pnga::trace_model::FileByteOffset{55}}));
}

TEST_CASE("Trace query keeps every span of a token crossing IDAT chunks",
          "[analysis-engine][wp5u12e]") {
  // Hand-built final fixed-Huffman block over "ABC" (BFINAL=1, BTYPE=01,
  // literal codes 0x71/0x72/0x73, EOB 0000000), split after zlib byte 2 so
  // the first literal's byte envelope [2, 4) crosses the chunk boundary.
  // Bit packing: byte 0 = 0x73, byte 1 = 0x74, byte 2 = 0x72, byte 3 = 0x06,
  // byte 4 = 0x00. IDAT data begins at file offset 41; the second IDAT data
  // begins at file offset 56.
  TraceInputs inputs(fixed_abc_two_idat_png());
  REQUIRE(inputs.stream.segment_count() == 2);
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);
  REQUIRE(inputs.trace.tokens.size() == 4);
  REQUIRE(inputs.trace.tokens[0].literal == 65);
  REQUIRE(inputs.trace.tokens[1].literal == 66);
  REQUIRE(inputs.trace.tokens[2].literal == 67);
  Selection selection;
  selection.stage = pnga::trace_model::Stage::kTrace;
  const auto result = compose_trace_query(
      9, selection, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  REQUIRE(result.deflate_data_begin == 2);
  // The final end-of-block event sits at the exclusive query end; the
  // closed-window boundary rule keeps the EOB row visible after the last
  // literal (flow-ui section 9.2), so all three literals are returned plus
  // the final EOB.
  REQUIRE(result.tokens.size() == 4);
  REQUIRE(result.tokens.back().kind ==
          pnga::deflate_trace::TokenKind::kEndOfBlock);
  REQUIRE(result.tokens.back().output_begin == 3);
  REQUIRE(result.tokens.back().output_end == 3);
  using pnga::trace_model::FileByteOffset;
  using pnga::trace_model::FileByteRange;
  // 'A' reads DEFLATE bits [3, 11) -> zlib bits [19, 27) -> bytes [2, 4):
  // one byte in each IDAT segment, both retained in source order.
  REQUIRE(result.tokens[0].input_range() ==
          (pnga::trace_model::DeflateBitRange{
              pnga::trace_model::DeflateBitOffset{3},
              pnga::trace_model::DeflateBitOffset{11}}));
  REQUIRE(result.tokens[0].physical_input_spans.size() == 2);
  REQUIRE(result.tokens[0].physical_input_spans[0] ==
          FileByteRange{FileByteOffset{43}, FileByteOffset{44}});
  REQUIRE(result.tokens[0].physical_input_spans[1] ==
          FileByteRange{FileByteOffset{56}, FileByteOffset{57}});
  // 'B' spans zlib bytes [3, 5), entirely inside the second segment.
  REQUIRE(result.tokens[1].physical_input_spans ==
          (std::vector<FileByteRange>{FileByteRange{FileByteOffset{56},
                                                    FileByteOffset{58}}}));
  // 'C' spans zlib bytes [4, 6).
  REQUIRE(result.tokens[2].physical_input_spans ==
          (std::vector<FileByteRange>{FileByteRange{FileByteOffset{57},
                                                    FileByteOffset{59}}}));
}

TEST_CASE("Trace query keeps a covered interval ready when the decode budget overshoots",
          "[analysis-engine][wp5u12]") {
  // Real-image defect: the trace decode budget extends past the requested
  // interval end (+64 KiB lookahead), so a sequential decode can hit its
  // output cap in the lookahead zone after [begin, end) was already fully
  // tiled. The covered interval must stay ready instead of downgrading to a
  // partial result.
  const auto encoded =
      pnga_test::encode_png(1672, 4, 8, 2, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(encoded);
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);
  // One 1672-wide RGB8 filter row is 1 + 1672*3 = 5017 output bytes.
  REQUIRE(inputs.trace.output_bytes >= 5017);

  auto lookahead_capped = inputs.trace;
  lookahead_capped.success = false;
  lookahead_capped.error = "output cap exceeded";
  // The cap fired beyond the interval end: the decoder produced 57153 bytes
  // while the requested interval ends at 5017.
  lookahead_capped.output_bytes = 57153;

  const auto result = compose_trace_query(
      9, Selection{}, inputs.blocks, lookahead_capped, inputs.stream,
      inputs.file, 0, 5017, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  REQUIRE(result.error.empty());
  REQUIRE_FALSE(result.truncated);
  // The returned tokens still tile the requested interval contiguously.
  REQUIRE_FALSE(result.tokens.empty());
  REQUIRE(result.tokens.front().output_begin == 0);
  for (std::size_t i = 1; i < result.tokens.size(); ++i) {
    REQUIRE(result.tokens[i].output_begin == result.tokens[i - 1].output_end);
  }
  REQUIRE(result.tokens.back().output_end >= 5017);
}

TEST_CASE("Stored trace tokens keep an absent huffman symbol",
          "[analysis-engine][wp5u12d]") {
  std::vector<std::byte> raw(8);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    raw[i] = pnga_test::B(static_cast<unsigned char>(0x10 + i));
  }
  TraceInputs inputs(stored_block_png(raw));
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);
  REQUIRE(inputs.blocks.blocks.front().type ==
          pnga::deflate_index::BlockType::kStored);
  const auto result = compose_trace_query(
      2, Selection{}, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  REQUIRE_FALSE(result.tokens.empty());
  for (const auto& token : result.tokens) {
    REQUIRE_FALSE(token.huffman_symbol.has_value());
  }
}

TEST_CASE("Trace query serialization pins the huffman symbol field",
          "[analysis-engine][wp5u12d]") {
  pnga::analysis_engine::TraceQueryResult result;
  result.status = TraceQueryStatus::kReady;
  pnga::analysis_engine::TraceTokenSummary literal;
  literal.index = 0;
  literal.kind = pnga::deflate_trace::TokenKind::kLiteral;
  literal.input_bit_begin = 16;
  literal.input_bit_end = 24;
  literal.output_begin = 0;
  literal.output_end = 1;
  literal.literal = 65;
  literal.block_index = 0;
  literal.huffman_symbol = 65;
  result.tokens.push_back(literal);
  pnga::analysis_engine::TraceTokenSummary stored_literal;
  stored_literal.index = 1;
  stored_literal.kind = pnga::deflate_trace::TokenKind::kLiteral;
  stored_literal.input_bit_begin = 24;
  stored_literal.input_bit_end = 32;
  stored_literal.output_begin = 1;
  stored_literal.output_end = 2;
  stored_literal.literal = 66;
  stored_literal.block_index = 0;
  result.tokens.push_back(stored_literal);

  const auto serialized = serialize_trace_query(result);
  // The symbol field sits between block_index and the per-token physical
  // span count, followed by sources=; a stored literal that consumed no
  // Huffman code serializes as '-'.
  REQUIRE(serialized.find(
              "token:0,literal,16,24,0,1,65,0,0,0,0,0,65,spans=0,sources=0\n") !=
          std::string::npos);
  REQUIRE(serialized.find(
              "token:1,literal,24,32,1,2,66,0,0,0,0,0,-,spans=0,sources=0\n") !=
          std::string::npos);
}
