// WP-5U8 Chunk Detail tests: bounded field decoding and safe degradation.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_detail.h>
#include <pnga/png-format/chunk_index.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkDetail;
using pnga::png_format::ChunkIndex;
using pnga::png_format::ChunkNode;
using pnga::png_format::describe_chunk;
using pnga::png_format::index_chunks;

namespace {

std::byte B(unsigned int value) { return static_cast<std::byte>(value); }

std::vector<std::byte> chunk(const char* type,
                             const std::vector<std::byte>& payload) {
  const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
  std::vector<std::byte> out = {B(length >> 24), B(length >> 16),
                                B(length >> 8), B(length)};
  for (int i = 0; i < 4; ++i) {
    out.push_back(B(static_cast<unsigned char>(type[i])));
  }
  out.insert(out.end(), payload.begin(), payload.end());
  out.insert(out.end(), {B(0), B(0), B(0), B(0)});
  return out;
}

std::vector<std::byte> png(std::vector<std::vector<std::byte>> chunks) {
  std::vector<std::byte> out(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  for (auto& item : chunks) {
    out.insert(out.end(), item.begin(), item.end());
  }
  return out;
}

const ChunkNode& node_of(const ChunkIndex& index, const char* type) {
  for (const auto& node : index.chunks) {
    if (node.text() == type) {
      return node;
    }
  }
  FAIL("chunk not found");
}

bool has_field(const ChunkDetail& detail, const char* name,
               const char* value_fragment) {
  for (const auto& field : detail.fields) {
    if (field.name == name && field.value.find(value_fragment) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("Chunk detail decodes IHDR fields", "[png-format][chunk-detail]") {
  const std::vector<std::byte> ihdr = {
      B(0), B(0), B(0), B(32), B(0), B(0), B(0), B(16), B(8), B(6), B(0),
      B(0), B(0)};
  MemoryByteSource source(png({chunk("IHDR", ihdr), chunk("IEND", {})}));
  const ChunkIndex index = index_chunks(source);
  const ChunkDetail detail = describe_chunk(source, node_of(index, "IHDR"));

  REQUIRE_FALSE(detail.basic_only);
  REQUIRE(has_field(detail, "Width", "32"));
  REQUIRE(has_field(detail, "Height", "16"));
  REQUIRE(has_field(detail, "Bit depth", "8"));
  REQUIRE(has_field(detail, "Color type", "6"));
}

TEST_CASE("Chunk detail expands palette entries", "[png-format][chunk-detail]") {
  MemoryByteSource source(png({chunk("PLTE", {B(1), B(2), B(3), B(4), B(5), B(6)}),
                               chunk("IEND", {})}));
  const ChunkIndex index = index_chunks(source);
  const ChunkDetail detail = describe_chunk(source, node_of(index, "PLTE"));

  REQUIRE_FALSE(detail.basic_only);
  REQUIRE(has_field(detail, "Palette[0] Red", "1"));
  REQUIRE(has_field(detail, "Palette[1] Blue", "6"));
}

TEST_CASE("IDAT detail stays basic-only", "[png-format][chunk-detail]") {
  MemoryByteSource source(png({chunk("IDAT", {B(0x78), B(0x9C), B(0), B(0)}),
                               chunk("IEND", {})}));
  const ChunkIndex index = index_chunks(source);
  const ChunkDetail detail = describe_chunk(source, node_of(index, "IDAT"));

  REQUIRE(detail.basic_only);
  REQUIRE(has_field(detail, "Payload", "not decoded"));
}

TEST_CASE("Text chunks expose keyword and escaped text",
          "[png-format][chunk-detail]") {
  MemoryByteSource source(png({chunk("tEXt", {B('A'), B('p'), B('p'), B(0),
                                                B('h'), B('i'), B('!')}),
                               chunk("IEND", {})}));
  const ChunkIndex index = index_chunks(source);
  const ChunkDetail detail = describe_chunk(source, node_of(index, "tEXt"));

  REQUIRE_FALSE(detail.basic_only);
  REQUIRE(has_field(detail, "Keyword", "App"));
  REQUIRE(has_field(detail, "Text", "hi!"));
}

TEST_CASE("Malformed fixed chunks and unknown chunks degrade safely",
          "[png-format][chunk-detail]") {
  MemoryByteSource source(png({chunk("gAMA", {B(1), B(2)}),
                               chunk("zzZZ", {B(0xAA), B(0xBB)}),
                               chunk("IEND", {})}));
  const ChunkIndex index = index_chunks(source);

  const ChunkDetail gamma = describe_chunk(source, node_of(index, "gAMA"));
  REQUIRE(gamma.basic_only);
  REQUIRE(has_field(gamma, "Payload", "expected 4 bytes"));

  const ChunkDetail unknown = describe_chunk(source, node_of(index, "zzZZ"));
  REQUIRE(unknown.basic_only);
  REQUIRE(has_field(unknown, "Payload", "not implemented"));
}

TEST_CASE("Oversized text payload is not materialized",
          "[png-format][chunk-detail]") {
  std::vector<std::byte> payload(64 * 1024 + 1, B('x'));
  MemoryByteSource source(png({chunk("tEXt", payload), chunk("IEND", {})}));
  const ChunkIndex index = index_chunks(source);
  const ChunkDetail detail = describe_chunk(source, node_of(index, "tEXt"));

  REQUIRE(detail.basic_only);
  REQUIRE(has_field(detail, "Payload", "oversized"));
}
