// WP-600B: checked PNG resource validation.

#include "pnga/validation/resource.h"

#include "pnga/validation/semantic.h"

#include <limits>

namespace pnga::validation {

namespace {

void add_issue(ValidationReport& report, const char* rule_id,
               const char* message, std::uint64_t offset,
               const char* spec_ref) {
  report.issues.push_back({rule_id, Severity::kError, message, offset, spec_ref});
}

bool checked_mul(std::uint64_t left, std::uint64_t right,
                 std::uint64_t& out) noexcept {
  if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right) {
    return false;
  }
  out = left * right;
  return true;
}

std::uint64_t channels(std::uint8_t color_type) noexcept {
  switch (color_type) {
    case 0:
      return 1;
    case 2:
      return 3;
    case 3:
      return 1;
    case 4:
      return 2;
    case 6:
      return 4;
    default:
      return 0;
  }
}

}  // namespace

ValidationReport validate_resources(const pnga::io::IByteSource& source,
                                    const pnga::png_format::ChunkIndex& index) {
  ValidationReport report;
  const auto header = read_ihdr(source, index);
  if (!header.has_value()) {
    return report;
  }
  if (header->width > kMaxImageDimension || header->height > kMaxImageDimension) {
    add_issue(report, "image_dimension_limit",
              "image width or height exceeds the configured safety limit",
              header->header_offset, "PNG:11.2.2");
    return report;
  }
  const std::uint64_t channel_count = channels(header->color_type);
  if (channel_count == 0) {
    return report;  // semantic validation owns the invalid color type rule.
  }
  std::uint64_t bits_per_row = 0;
  if (!checked_mul(static_cast<std::uint64_t>(header->width), channel_count,
                   bits_per_row) ||
      !checked_mul(bits_per_row, static_cast<std::uint64_t>(header->bit_depth),
                   bits_per_row)) {
    add_issue(report, "decoded_scanline_overflow",
              "decoded scanline bit count overflows the safety arithmetic",
              header->header_offset, "PNG:11.2.2");
    return report;
  }
  if (bits_per_row > std::numeric_limits<std::uint64_t>::max() - 7) {
    add_issue(report, "decoded_scanline_overflow",
              "decoded scanline bit count overflows the safety arithmetic",
              header->header_offset, "PNG:11.2.2");
    return report;
  }
  const std::uint64_t row_bytes = (bits_per_row + 7) / 8;
  std::uint64_t total = 0;
  if (!checked_mul(row_bytes, header->height, total)) {
    add_issue(report, "decoded_image_overflow",
              "decoded image byte count overflows the safety arithmetic",
              header->header_offset, "PNG:11.2.2");
    return report;
  }
  if (total > kMaxDecodedScanlineBytes) {
    add_issue(report, "decoded_image_budget",
              "decoded scanline storage exceeds the configured safety budget",
              header->header_offset, "PNG:11.2.2");
  }
  return report;
}

}  // namespace pnga::validation
