// WP-600B: zlib wrapper and IDAT decode preflight rules.

#include "pnga/validation/decode.h"

#include <pnga/png-format/virtual_idat_stream.h>

#include <array>
#include <limits>

namespace pnga::validation {

namespace {

void add_issue(ValidationReport& report, const char* rule_id,
               const char* message, std::uint64_t offset,
               const char* spec_ref) {
  report.issues.push_back({rule_id, Severity::kError, message, offset, spec_ref});
}

std::uint64_t idat_offset(const pnga::png_format::ChunkIndex& index) noexcept {
  for (const auto& node : index.chunks) {
    if (node.text() == "IDAT") {
      return node.data_offset;
    }
  }
  return index.file_size;
}

std::uint64_t add_offset(std::uint64_t base, std::uint64_t delta) noexcept {
  return base <= std::numeric_limits<std::uint64_t>::max() - delta
             ? base + delta
             : base;
}

}  // namespace

ValidationReport validate_decode_preflight(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& index) {
  ValidationReport report;
  const pnga::png_format::VirtualIDATStream stream(index);
  const std::uint64_t offset = idat_offset(index);
  if (stream.size() == 0) {
    add_issue(report, "idat_required", "PNG must contain an IDAT data stream",
              offset, "PNG:11.2.4");
    return report;
  }
  if (stream.size() < 2) {
    add_issue(report, "idat_zlib_header_truncated",
              "IDAT stream is shorter than the zlib header", offset, "PNG:13.2");
    return report;
  }
  std::array<std::byte, 2> header{};
  if (!stream.read(source, 0, header.data(), header.size())) {
    add_issue(report, "idat_zlib_header_unreadable",
              "IDAT zlib header is not readable", offset, "PNG:13.2");
    return report;
  }
  const std::uint8_t cmf = static_cast<std::uint8_t>(header[0]);
  const std::uint8_t flg = static_cast<std::uint8_t>(header[1]);
  if ((cmf & 0x0f) != 8) {
    add_issue(report, "idat_zlib_method", "IDAT zlib method must be Deflate", offset,
              "PNG:13.2");
  }
  if ((cmf >> 4) > 7) {
    add_issue(report, "idat_zlib_window", "IDAT zlib window size is invalid", offset,
              "PNG:13.2");
  }
  if ((static_cast<unsigned>(cmf) * 256U + flg) % 31U != 0U) {
    add_issue(report, "idat_zlib_fcheck", "IDAT zlib FCHECK is invalid",
              add_offset(offset, 1),
              "PNG:13.2");
  }
  if ((flg & 0x20U) != 0U) {
    add_issue(report, "idat_zlib_dictionary", "PNG IDAT must not set FDICT",
              add_offset(offset, 1),
              "PNG:13.2");
  }
  return report;
}

}  // namespace pnga::validation
