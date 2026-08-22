// WP-600A: bounded PNG CRC and zlib Adler-32 integrity rules.

#include "pnga/validation/integrity.h"

#include <pnga/png-format/checksum.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <zlib.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace pnga::validation {

namespace {

void add_issue(ValidationReport& report, const char* rule_id,
               const char* message, std::uint64_t offset,
               const char* spec_ref) {
  report.issues.push_back(ValidationIssue{rule_id, Severity::kError, message,
                                          offset, spec_ref});
}

std::uint32_t read_u32_be(const std::array<std::byte, 4>& bytes) noexcept {
  return (static_cast<std::uint32_t>(bytes[0]) << 24) |
         (static_cast<std::uint32_t>(bytes[1]) << 16) |
         (static_cast<std::uint32_t>(bytes[2]) << 8) |
         static_cast<std::uint32_t>(bytes[3]);
}

std::uint64_t first_idat_offset(
    const pnga::png_format::ChunkIndex& index) noexcept {
  for (const auto& node : index.chunks) {
    if (node.text() == "IDAT") {
      return node.data_offset;
    }
  }
  return index.file_size;
}

}  // namespace

ValidationReport validate_integrity(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& index,
    std::optional<std::span<const std::byte>> inflated_idat) {
  ValidationReport report;

  for (const auto& node : index.chunks) {
    const auto calculated =
        pnga::png_format::calculate_chunk_crc(source, node);
    const auto stored = pnga::png_format::read_chunk_crc(source, node);
    if (!calculated.has_value() || !stored.has_value()) {
      add_issue(report, "chunk_integrity_unreadable",
                "indexed chunk bytes are no longer readable", node.header_offset,
                "PNG:5.2");
    } else if (*calculated != *stored) {
      add_issue(report, "chunk_crc_mismatch", "chunk CRC does not match its type and data",
                node.crc_offset, "PNG:5.2");
    }
  }

  const pnga::png_format::VirtualIDATStream idat(index);
  if (idat.size() == 0) {
    return report;
  }
  // A zlib-wrapped IDAT stream needs a two-byte header and a four-byte Adler
  // trailer. This check deliberately does not concatenate IDAT payloads.
  if (idat.size() < 6) {
    add_issue(report, "idat_adler_truncated",
              "IDAT stream is too short for a zlib header and Adler-32",
              first_idat_offset(index), "PNG:13.2");
    return report;
  }
  if (!inflated_idat.has_value()) {
    return report;
  }
  if (inflated_idat->size() >
      static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
    add_issue(report, "idat_adler_input_too_large",
              "inflated IDAT span exceeds the checksum API limit",
              first_idat_offset(index), "PNG:13.2");
    return report;
  }

  std::array<std::byte, 4> stored_bytes{};
  std::vector<pnga::png_format::PhysicalRange> trailer_spans;
  const bool trailer_mapped = idat.logical_to_physical(
      idat.size() - stored_bytes.size(), stored_bytes.size(), trailer_spans);
  const std::uint64_t trailer_offset =
      trailer_mapped && !trailer_spans.empty() ? trailer_spans.front().offset
                                               : first_idat_offset(index);
  if (!idat.read(source, idat.size() - stored_bytes.size(), stored_bytes.data(),
                 stored_bytes.size())) {
    add_issue(report, "idat_adler_unreadable",
              "IDAT Adler-32 trailer is not readable", trailer_offset,
              "PNG:13.2");
    return report;
  }
  const uLong actual = adler32(
      adler32(0L, Z_NULL, 0),
      reinterpret_cast<const Bytef*>(inflated_idat->data()),
      static_cast<uInt>(inflated_idat->size()));
  if (static_cast<std::uint32_t>(actual) != read_u32_be(stored_bytes)) {
    add_issue(report, "idat_adler_mismatch",
              "IDAT Adler-32 does not match the inflated bytes", trailer_offset,
              "PNG:13.2");
  }
  return report;
}

}  // namespace pnga::validation
