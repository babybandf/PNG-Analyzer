// WP-102 structural validator tests: one positive and one negative fixture per
// rule, with stable rule ids and deterministic reports.

#include <pnga/validation/structural.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::index_chunks;
using pnga::png_format::kPngSignature;
using pnga::validation::Severity;
using pnga::validation::ValidationReport;
using pnga::validation::validate_structure;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> chunk_bytes(const char* type, std::uint32_t length,
                                   std::uint32_t crc = 0) {
  std::vector<std::byte> out;
  out.push_back(B(static_cast<unsigned char>(length >> 24)));
  out.push_back(B(static_cast<unsigned char>(length >> 16)));
  out.push_back(B(static_cast<unsigned char>(length >> 8)));
  out.push_back(B(static_cast<unsigned char>(length)));
  for (int i = 0; i < 4; ++i) {
    out.push_back(B(static_cast<unsigned char>(type[i])));
  }
  for (std::uint32_t i = 0; i < length; ++i) {
    out.push_back(B(0x11));
  }
  out.push_back(B(static_cast<unsigned char>(crc >> 24)));
  out.push_back(B(static_cast<unsigned char>(crc >> 16)));
  out.push_back(B(static_cast<unsigned char>(crc >> 8)));
  out.push_back(B(static_cast<unsigned char>(crc)));
  return out;
}

std::vector<std::byte> png_bytes(std::vector<std::vector<std::byte>> chunks) {
  std::vector<std::byte> out;
  out.assign(kPngSignature.begin(), kPngSignature.end());
  for (auto& c : chunks) {
    out.insert(out.end(), c.begin(), c.end());
  }
  return out;
}

ValidationReport report_for(std::vector<std::byte> bytes) {
  MemoryByteSource src(std::move(bytes));
  const ChunkIndex index = index_chunks(src);
  return validate_structure(index);
}

bool has_rule(const ValidationReport& report, const std::string& rule_id) {
  for (const auto& issue : report.issues) {
    if (issue.rule_id == rule_id) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("A valid minimal PNG passes all structural rules",
          "[validation][wp102]") {
  const ValidationReport report =
      report_for(png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8),
                            chunk_bytes("IEND", 0)}));
  REQUIRE(report.valid());
  REQUIRE(report.issues.empty());
}

TEST_CASE("Bad signature yields signature_invalid", "[validation][wp102]") {
  auto bytes = png_bytes({chunk_bytes("IHDR", 13)});
  bytes[0] = std::byte{0x00};
  const ValidationReport report = report_for(bytes);
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "signature_invalid"));
}

TEST_CASE("Truncated signature yields signature_truncated",
          "[validation][wp102]") {
  const ValidationReport report =
      report_for(std::vector<std::byte>(4, std::byte{0}));
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "signature_truncated"));
}

TEST_CASE("A first chunk that is not IHDR fails ihdr_required",
          "[validation][wp102]") {
  const ValidationReport report =
      report_for(png_bytes({chunk_bytes("IDAT", 8), chunk_bytes("IEND", 0)}));
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "ihdr_required"));
}

TEST_CASE("A file without chunks fails ihdr_required", "[validation][wp102]") {
  const ValidationReport report = report_for(png_bytes({}));
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "ihdr_required"));
}

TEST_CASE("A duplicate IHDR fails ihdr_duplicate", "[validation][wp102]") {
  const ValidationReport report =
      report_for(png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IHDR", 13),
                            chunk_bytes("IEND", 0)}));
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "ihdr_duplicate"));
}

TEST_CASE("A missing IEND fails iend_required", "[validation][wp102]") {
  const ValidationReport report =
      report_for(png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8)}));
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "iend_required"));
}

TEST_CASE("Non-consecutive IDAT chunks fail idat_not_contiguous",
          "[validation][wp102]") {
  const ValidationReport report = report_for(png_bytes(
      {chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8),
       chunk_bytes("tEXt", 4), chunk_bytes("IDAT", 8),
       chunk_bytes("IEND", 0)}));
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "idat_not_contiguous"));
}

TEST_CASE("Consecutive IDAT chunks pass idat continuity",
          "[validation][wp102]") {
  const ValidationReport report = report_for(png_bytes(
      {chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8),
       chunk_bytes("IDAT", 4), chunk_bytes("IEND", 0)}));
  REQUIRE(report.valid());
}

TEST_CASE("Trailing bytes after IEND fail data_after_iend",
          "[validation][wp102]") {
  auto bytes = png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IEND", 0)});
  bytes.insert(bytes.end(), {std::byte{0xAA}, std::byte{0xBB}});
  const ValidationReport report = report_for(bytes);
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "data_after_iend"));
}

TEST_CASE("A truncated chunk envelope fails chunk_truncated",
          "[validation][wp102]") {
  auto ihdr = chunk_bytes("IHDR", 13);
  auto bad = chunk_bytes("IDAT", 100);
  bad.resize(bad.size() - 20);
  const ValidationReport report =
      report_for(png_bytes({std::move(ihdr), std::move(bad)}));
  REQUIRE_FALSE(report.valid());
  REQUIRE(has_rule(report, "chunk_truncated"));
}

TEST_CASE("Reports are deterministic for identical input",
          "[validation][wp102]") {
  const auto bytes = png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8),
                                chunk_bytes("IEND", 0)});
  const ValidationReport a = report_for(bytes);
  const ValidationReport b = report_for(bytes);
  REQUIRE(a.issues.size() == b.issues.size());
  for (std::size_t i = 0; i < a.issues.size(); ++i) {
    REQUIRE(a.issues[i].rule_id == b.issues[i].rule_id);
    REQUIRE(a.issues[i].message == b.issues[i].message);
    REQUIRE(a.issues[i].offset == b.issues[i].offset);
    REQUIRE(a.issues[i].spec_ref == b.issues[i].spec_ref);
  }
}

TEST_CASE("Error severity is used for structural violations",
          "[validation][wp102]") {
  const ValidationReport report = report_for(
      png_bytes({chunk_bytes("IDAT", 8), chunk_bytes("IEND", 0)}));
  REQUIRE_FALSE(report.issues.empty());
  for (const auto& issue : report.issues) {
    REQUIRE(issue.severity == Severity::kError);
  }
}
