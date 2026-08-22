// WP-600B tests: IHDR semantics, checked resource budgets and zlib preflight.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/validation/decode.h>
#include <pnga/validation/resource.h>
#include <pnga/validation/semantic.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

std::byte B(unsigned char value) { return static_cast<std::byte>(value); }

std::uint32_t crc_for(const char* type, const std::vector<std::byte>& data) {
  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
  if (!data.empty()) {
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                static_cast<uInt>(data.size()));
  }
  return static_cast<std::uint32_t>(crc);
}

void put_u32(std::vector<std::byte>& body, std::size_t offset,
             std::uint32_t value) {
  body[offset] = B(static_cast<unsigned char>(value >> 24));
  body[offset + 1] = B(static_cast<unsigned char>(value >> 16));
  body[offset + 2] = B(static_cast<unsigned char>(value >> 8));
  body[offset + 3] = B(static_cast<unsigned char>(value));
}

std::vector<std::byte> chunk(const char* type,
                             const std::vector<std::byte>& data) {
  const auto crc = crc_for(type, data);
  const auto length = static_cast<std::uint32_t>(data.size());
  std::vector<std::byte> out;
  out.reserve(data.size() + 12);
  out.insert(out.end(), {B(static_cast<unsigned char>(length >> 24)),
                         B(static_cast<unsigned char>(length >> 16)),
                         B(static_cast<unsigned char>(length >> 8)),
                         B(static_cast<unsigned char>(length))});
  for (int i = 0; i < 4; ++i) {
    out.push_back(B(static_cast<unsigned char>(type[i])));
  }
  out.insert(out.end(), data.begin(), data.end());
  out.insert(out.end(), {B(static_cast<unsigned char>(crc >> 24)),
                         B(static_cast<unsigned char>(crc >> 16)),
                         B(static_cast<unsigned char>(crc >> 8)),
                         B(static_cast<unsigned char>(crc))});
  return out;
}

std::vector<std::byte> png(std::vector<std::byte> ihdr,
                           std::vector<std::byte> idat = {B(0x78), B(0x9c)},
                           bool include_idat = true) {
  std::vector<std::byte> out(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  const auto ihdr_chunk = chunk("IHDR", ihdr);
  out.insert(out.end(), ihdr_chunk.begin(), ihdr_chunk.end());
  if (include_idat) {
    const auto idat_chunk = chunk("IDAT", idat);
    out.insert(out.end(), idat_chunk.begin(), idat_chunk.end());
  }
  const auto iend = chunk("IEND", {});
  out.insert(out.end(), iend.begin(), iend.end());
  return out;
}

bool has_rule(const pnga::validation::ValidationReport& report,
              const std::string& rule_id) {
  for (const auto& issue : report.issues) {
    if (issue.rule_id == rule_id) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("Valid IHDR and zlib preflight pass", "[validation][wp600b]") {
  std::vector<std::byte> ihdr(13, B(0));
  put_u32(ihdr, 0, 1);
  put_u32(ihdr, 4, 1);
  ihdr[8] = B(8);
  ihdr[9] = B(6);
  const auto bytes = png(ihdr);
  pnga::io::MemoryByteSource source(bytes);
  const auto index = pnga::png_format::index_chunks(source);
  REQUIRE(pnga::validation::validate_semantics(source, index).issues.empty());
  REQUIRE(pnga::validation::validate_resources(source, index).issues.empty());
  REQUIRE(pnga::validation::validate_decode_preflight(source, index)
              .issues.empty());
}

TEST_CASE("IHDR rejects invalid bit-depth/color and interlace values",
          "[validation][wp600b]") {
  std::vector<std::byte> ihdr(13, B(0));
  put_u32(ihdr, 0, 4);
  put_u32(ihdr, 4, 4);
  ihdr[8] = B(4);
  ihdr[9] = B(2);
  ihdr[12] = B(2);
  const auto bytes = png(ihdr);
  pnga::io::MemoryByteSource source(bytes);
  const auto index = pnga::png_format::index_chunks(source);
  const auto report = pnga::validation::validate_semantics(source, index);
  REQUIRE(has_rule(report, "ihdr_bit_depth_color_type"));
  REQUIRE(has_rule(report, "ihdr_interlace_method"));
}

TEST_CASE("Oversized dimensions produce a resource issue without allocation",
          "[validation][wp600b]") {
  std::vector<std::byte> ihdr(13, B(0));
  put_u32(ihdr, 0, static_cast<std::uint32_t>(
                         pnga::validation::kMaxImageDimension + 1));
  put_u32(ihdr, 4, 1);
  ihdr[8] = B(8);
  ihdr[9] = B(6);
  const auto bytes = png(ihdr);
  pnga::io::MemoryByteSource source(bytes);
  const auto index = pnga::png_format::index_chunks(source);
  REQUIRE(has_rule(pnga::validation::validate_resources(source, index),
                   "image_dimension_limit"));
}

TEST_CASE("Decode preflight reports missing IDAT and invalid zlib method",
          "[validation][wp600b]") {
  std::vector<std::byte> ihdr(13, B(0));
  put_u32(ihdr, 0, 1);
  put_u32(ihdr, 4, 1);
  ihdr[8] = B(8);
  ihdr[9] = B(6);
  const auto no_idat = png(ihdr, {}, false);
  pnga::io::MemoryByteSource no_idat_source(no_idat);
  const auto no_idat_index = pnga::png_format::index_chunks(no_idat_source);
  REQUIRE(has_rule(pnga::validation::validate_decode_preflight(
                       no_idat_source, no_idat_index),
                   "idat_required"));

  const auto bad_method = png(ihdr, {B(0x77), B(0x01)});
  pnga::io::MemoryByteSource bad_source(bad_method);
  const auto bad_index = pnga::png_format::index_chunks(bad_source);
  REQUIRE(has_rule(pnga::validation::validate_decode_preflight(
                       bad_source, bad_index),
                   "idat_zlib_method"));
}
