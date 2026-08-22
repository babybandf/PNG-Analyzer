// WP-600B: PNG IHDR semantic validation.

#include "pnga/validation/semantic.h"

#include <array>
#include <limits>

namespace pnga::validation {

namespace {

constexpr std::array<std::byte, 4> kIhdr = {
    std::byte{'I'}, std::byte{'H'}, std::byte{'D'}, std::byte{'R'}};

void add_issue(ValidationReport& report, const char* rule_id,
               const char* message, std::uint64_t offset,
               const char* spec_ref) {
  report.issues.push_back({rule_id, Severity::kError, message, offset, spec_ref});
}

const pnga::png_format::ChunkNode* find_ihdr(
    const pnga::png_format::ChunkIndex& index) noexcept {
  for (const auto& node : index.chunks) {
    if (node.type == kIhdr) {
      return &node;
    }
  }
  return nullptr;
}

std::uint32_t u32(const std::array<std::byte, 13>& body,
                  std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(body[offset]) << 24) |
         (static_cast<std::uint32_t>(body[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(body[offset + 2]) << 8) |
         static_cast<std::uint32_t>(body[offset + 3]);
}

std::uint64_t field_offset(std::uint64_t base, std::uint64_t delta) noexcept {
  return base <= std::numeric_limits<std::uint64_t>::max() - delta
             ? base + delta
             : base;
}

}  // namespace

std::optional<PngHeaderFields> read_ihdr(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& index) noexcept {
  const auto* node = find_ihdr(index);
  if (node == nullptr || node->data_length != 13) {
    return std::nullopt;
  }
  std::array<std::byte, 13> body{};
  if (!source.read(node->data_offset, body.data(), body.size())) {
    return std::nullopt;
  }
  return PngHeaderFields{u32(body, 0), u32(body, 4),
                         static_cast<std::uint8_t>(body[8]),
                         static_cast<std::uint8_t>(body[9]),
                         static_cast<std::uint8_t>(body[10]),
                         static_cast<std::uint8_t>(body[11]),
                         static_cast<std::uint8_t>(body[12]),
                         node->header_offset};
}

ValidationReport validate_semantics(const pnga::io::IByteSource& source,
                                    const pnga::png_format::ChunkIndex& index) {
  ValidationReport report;
  const auto* node = find_ihdr(index);
  if (node == nullptr) {
    return report;  // structural validation owns the missing-IHDR rule.
  }
  if (node->data_length != 13) {
    add_issue(report, "ihdr_invalid_length", "IHDR data length must be 13 bytes",
              node->header_offset, "PNG:11.2.2");
    return report;
  }
  const auto header = read_ihdr(source, index);
  if (!header.has_value()) {
    add_issue(report, "ihdr_unreadable", "IHDR body is not readable",
              node->data_offset, "PNG:11.2.2");
    return report;
  }
  if (header->width == 0 || header->height == 0) {
    add_issue(report, "ihdr_zero_dimension", "IHDR width and height must be non-zero",
              node->data_offset, "PNG:11.2.2");
  }
  const bool valid_depth =
      (header->color_type == 0 && (header->bit_depth == 1 ||
                                   header->bit_depth == 2 ||
                                   header->bit_depth == 4 ||
                                   header->bit_depth == 8 ||
                                   header->bit_depth == 16)) ||
      (header->color_type == 2 &&
       (header->bit_depth == 8 || header->bit_depth == 16)) ||
      (header->color_type == 3 && (header->bit_depth == 1 ||
                                   header->bit_depth == 2 ||
                                   header->bit_depth == 4 ||
                                   header->bit_depth == 8)) ||
      (header->color_type == 4 &&
       (header->bit_depth == 8 || header->bit_depth == 16)) ||
      (header->color_type == 6 &&
       (header->bit_depth == 8 || header->bit_depth == 16));
  if (!valid_depth) {
    add_issue(report, "ihdr_bit_depth_color_type",
              "IHDR bit depth is invalid for its color type",
              field_offset(node->data_offset, 8),
              "PNG:11.2.2");
  }
  if (header->compression_method != 0) {
    add_issue(report, "ihdr_compression_method",
              "IHDR compression method must be zero",
              field_offset(node->data_offset, 10),
              "PNG:11.2.2");
  }
  if (header->filter_method != 0) {
    add_issue(report, "ihdr_filter_method", "IHDR filter method must be zero",
              field_offset(node->data_offset, 11), "PNG:11.2.2");
  }
  if (header->interlace_method > 1) {
    add_issue(report, "ihdr_interlace_method",
              "IHDR interlace method must be zero or one",
              field_offset(node->data_offset, 12),
              "PNG:11.2.2");
  }
  return report;
}

}  // namespace pnga::validation
