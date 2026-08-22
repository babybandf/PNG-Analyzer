// WP-600A integrity tests: bounded Chunk CRCs, IDAT trailer truncation and
// deterministic Adler-32 mismatch reporting.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/validation/integrity.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

std::byte B(unsigned char value) { return static_cast<std::byte>(value); }

std::uint32_t crc_for(const char* type, std::span<const std::byte> data) {
  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
  if (!data.empty()) {
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                static_cast<uInt>(data.size()));
  }
  return static_cast<std::uint32_t>(crc);
}

std::vector<std::byte> chunk(const char* type,
                             const std::vector<std::byte>& data,
                             std::optional<std::uint32_t> crc_override =
                                 std::nullopt) {
  const std::uint32_t length = static_cast<std::uint32_t>(data.size());
  const std::uint32_t crc = crc_override.value_or(crc_for(type, data));
  std::vector<std::byte> out;
  out.reserve(12 + data.size());
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

std::vector<std::byte> png(
    const std::vector<std::pair<const char*, std::vector<std::byte>>>& chunks) {
  std::vector<std::byte> out(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  for (const auto& [type, data] : chunks) {
    const auto bytes = chunk(type, data);
    out.insert(out.end(), bytes.begin(), bytes.end());
  }
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

pnga::validation::ValidationReport check(
    const std::vector<std::byte>& bytes,
    const std::vector<std::byte>* inflated = nullptr) {
  pnga::io::MemoryByteSource source(bytes);
  const auto index = pnga::png_format::index_chunks(source);
  if (inflated == nullptr) {
    return pnga::validation::validate_integrity(source, index);
  }
  return pnga::validation::validate_integrity(
      source, index, std::span<const std::byte>(*inflated));
}

}  // namespace

TEST_CASE("Valid chunk CRCs pass the bounded integrity rule",
          "[validation][wp600a]") {
  const auto bytes = png({{"IHDR", std::vector<std::byte>(13, B(0x11))},
                          {"IDAT", {B(0x78), B(0x9c), B(0x00), B(0x00),
                                    B(0x00), B(0x00)}},
                          {"IEND", std::vector<std::byte>{}}});
  const auto report = check(bytes);
  REQUIRE(report.issues.empty());
}

TEST_CASE("CRC mismatch has a stable rule id and CRC offset",
          "[validation][wp600a]") {
  const std::vector<std::byte> data = {B(0x11), B(0x22), B(0x33)};
  std::vector<std::byte> out(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  const auto bad = chunk("tEXt", data, 0U);
  out.insert(out.end(), bad.begin(), bad.end());
  const auto report = check(out);
  REQUIRE(has_rule(report, "chunk_crc_mismatch"));
  REQUIRE(report.issues.front().offset == 8 + 8 + data.size());
}

TEST_CASE("IDAT shorter than header plus Adler trailer is reported",
          "[validation][wp600a]") {
  const auto bytes = png({{"IHDR", std::vector<std::byte>(13, B(0x11))},
                          {"IDAT", std::vector<std::byte>(5, B(0x00))},
                          {"IEND", std::vector<std::byte>{}}});
  const auto report = check(bytes);
  REQUIRE(has_rule(report, "idat_adler_truncated"));
}

TEST_CASE("Supplied inflated bytes are checked against the IDAT Adler trailer",
          "[validation][wp600a]") {
  const std::vector<std::byte> inflated = {B(0x41), B(0x42)};
  uLong adler = adler32(0L, Z_NULL, 0);
  adler = adler32(adler, reinterpret_cast<const Bytef*>(inflated.data()),
                  static_cast<uInt>(inflated.size()));
  const std::vector<std::byte> idat = {
      B(0x78), B(0x9c), B(0x00), B(static_cast<unsigned char>(adler >> 24)),
      B(static_cast<unsigned char>(adler >> 16)),
      B(static_cast<unsigned char>(adler >> 8)),
      B(static_cast<unsigned char>(adler))};
  const auto bytes = png({{"IHDR", std::vector<std::byte>(13, B(0x11))},
                          {"IDAT", idat}, {"IEND", std::vector<std::byte>{}}});
  const auto good_report = check(bytes, &inflated);
  REQUIRE(good_report.issues.empty());

  auto bad_inflated = inflated;
  bad_inflated[0] = B(0x43);
  const auto report = check(bytes, &bad_inflated);
  REQUIRE(has_rule(report, "idat_adler_mismatch"));
}

TEST_CASE("Large chunk CRCs use bounded windows", "[validation][wp600a]") {
  const std::vector<std::byte> data(70 * 1024, B(0x5a));
  const auto bytes = png({{"IHDR", std::vector<std::byte>(13, B(0x11))},
                          {"tEXt", data}, {"IEND", std::vector<std::byte>{}}});
  const auto report = check(bytes);
  REQUIRE(report.issues.empty());
}
