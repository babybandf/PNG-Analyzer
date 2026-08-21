// WP-102 structural validation. Pure analysis over the physical Chunk index;
// no exceptions and no access to chunk data.

#include "pnga/validation/structural.h"

namespace pnga::validation {

namespace {

using pnga::png_format::ChunkIndex;
using pnga::png_format::ChunkIssueKind;

void add_issue(ValidationReport& report, const char* rule_id, Severity sev,
               const char* message, std::uint64_t offset,
               const char* spec_ref) {
  report.issues.push_back(
      ValidationIssue{rule_id, sev, message, offset, spec_ref});
}

}  // namespace

ValidationReport validate_structure(const ChunkIndex& index) {
  ValidationReport report;

  // Translate index-level scan problems into stable rules.
  for (const auto& issue : index.issues) {
    switch (issue.kind) {
      case ChunkIssueKind::kBadSignature:
        add_issue(report, "signature_invalid", Severity::kError,
                  "file does not start with the PNG signature", issue.offset,
                  "PNG:5.1");
        break;
      case ChunkIssueKind::kTruncatedSignature:
        add_issue(report, "signature_truncated", Severity::kError,
                  "file is shorter than the 8-byte PNG signature",
                  issue.offset, "PNG:5.1");
        break;
      case ChunkIssueKind::kTruncatedHeader:
      case ChunkIssueKind::kTruncatedData:
      case ChunkIssueKind::kTruncatedCrc:
        add_issue(report, "chunk_truncated", Severity::kError,
                  "chunk envelope runs past the end of the file", issue.offset,
                  "PNG:5.2");
        break;
      case ChunkIssueKind::kTrailingBytesAfterIend:
        add_issue(report, "data_after_iend", Severity::kError,
                  "bytes appear after the IEND chunk", issue.offset, "PNG:5.2");
        break;
    }
  }

  if (!index.valid_signature) {
    // The signature rules above already describe the failure; nothing else is
    // meaningful when the file is not a PNG.
    return report;
  }

  const auto& chunks = index.chunks;

  // IHDR must be the first chunk and appear exactly once.
  bool saw_ihdr = false;
  std::uint64_t ihdr_count = 0;
  if (chunks.empty()) {
    add_issue(report, "ihdr_required", Severity::kError,
              "file contains no chunks and no IHDR", index.file_size,
              "PNG:5.2");
  } else if (chunks.front().text() != "IHDR") {
    add_issue(report, "ihdr_required", Severity::kError,
              "first chunk is not IHDR", chunks.front().header_offset,
              "PNG:5.2");
  }
  for (const auto& node : chunks) {
    if (node.text() == "IHDR") {
      ++ihdr_count;
      saw_ihdr = true;
    }
  }
  if (saw_ihdr && ihdr_count > 1) {
    add_issue(report, "ihdr_duplicate", Severity::kError,
              "more than one IHDR chunk", index.file_size, "PNG:5.2");
  }

  // IEND must terminate the chunk stream.
  bool saw_iend = false;
  for (const auto& node : chunks) {
    if (node.text() == "IEND") {
      saw_iend = true;
      break;
    }
  }
  if (!saw_iend && !chunks.empty()) {
    add_issue(report, "iend_required", Severity::kError,
              "chunk stream does not end with IEND", index.file_size,
              "PNG:5.2");
  }

  // IDAT chunks must be consecutive once the first IDAT begins.
  bool seen_idat = false;
  bool idat_ended = false;
  for (const auto& node : chunks) {
    if (node.text() == "IDAT") {
      if (idat_ended) {
        add_issue(report, "idat_not_contiguous", Severity::kError,
                  "IDAT chunks are not consecutive", node.header_offset,
                  "PNG:5.2");
      }
      seen_idat = true;
    } else if (seen_idat) {
      idat_ended = true;
    }
  }

  return report;
}

}  // namespace pnga::validation
