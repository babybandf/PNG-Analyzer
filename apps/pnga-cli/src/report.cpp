// WP-103 deterministic JSON reports. Strings are built with explicit field
// order; integer formatting via std::to_string is locale-independent.

#include "report.h"

#include <cstdio>
#include <cstdint>
#include <sstream>
#include <string>

namespace pnga::cli {

namespace {

// Minimal JSON string escaping for the file path field.
std::string json_escape(const std::string& s) {
  std::ostringstream out;
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out << buf;
        } else {
          out << static_cast<char>(c);
        }
    }
  }
  return out.str();
}

void append_chunks(std::ostringstream& out,
                   const pnga::png_format::ChunkIndex& index) {
  out << '"' << "chunks" << "\":[";
  for (std::size_t i = 0; i < index.chunks.size(); ++i) {
    const auto& n = index.chunks[i];
    if (i != 0) {
      out << ',';
    }
    out << '{'
        << "\"type\":\"" << json_escape(n.text()) << "\","
        << "\"header_offset\":" << n.header_offset << ","
        << "\"data_offset\":" << n.data_offset << ","
        << "\"data_length\":" << n.data_length << ","
        << "\"crc_offset\":" << n.crc_offset
        << '}';
  }
  out << ']';
}

void append_index_issues(std::ostringstream& out,
                         const pnga::png_format::ChunkIndex& index) {
  out << '"' << "issues" << "\":[";
  for (std::size_t i = 0; i < index.issues.size(); ++i) {
    const auto& issue = index.issues[i];
    if (i != 0) {
      out << ',';
    }
    out << '{'
        << "\"kind\":\"" << issue_kind_text(issue.kind) << "\","
        << "\"offset\":" << issue.offset
        << '}';
  }
  out << ']';
}

const char* severity_text(pnga::validation::Severity severity) noexcept {
  switch (severity) {
    case pnga::validation::Severity::kInfo:
      return "info";
    case pnga::validation::Severity::kWarning:
      return "warning";
    case pnga::validation::Severity::kError:
      return "error";
  }
  return "error";
}

void append_validation_issues(
    std::ostringstream& out,
    const pnga::analysis_engine::DocumentValidationReport& report) {
  out << '"' << "issues" << "\":[";
  for (std::size_t i = 0; i < report.issues.size(); ++i) {
    const auto& issue = report.issues[i];
    if (i != 0) {
      out << ',';
    }
    out << '{'
        << "\"rule_id\":\"" << json_escape(issue.rule_id) << "\","
        << "\"severity\":\"" << severity_text(issue.severity) << "\","
        << "\"message\":\"" << json_escape(issue.message) << "\","
        << "\"offset\":" << issue.offset << ','
        << "\"spec_ref\":\"" << json_escape(issue.spec_ref) << "\""
        << '}';
  }
  out << ']';
}

}  // namespace

const char* issue_kind_text(pnga::png_format::ChunkIssueKind kind) {
  using pnga::png_format::ChunkIssueKind;
  switch (kind) {
    case ChunkIssueKind::kBadSignature:
      return "bad_signature";
    case ChunkIssueKind::kTruncatedSignature:
      return "truncated_signature";
    case ChunkIssueKind::kTruncatedHeader:
      return "truncated_header";
    case ChunkIssueKind::kTruncatedData:
      return "truncated_data";
    case ChunkIssueKind::kTruncatedCrc:
      return "truncated_crc";
    case ChunkIssueKind::kTrailingBytesAfterIend:
      return "trailing_bytes_after_iend";
  }
  return "unknown";
}

std::string inspect_json(const std::string& file,
                         const pnga::png_format::ChunkIndex& index) {
  std::ostringstream out;
  out << '{'
      << "\"file\":\"" << json_escape(file) << "\","
      << "\"size\":" << index.file_size << ","
      << "\"signature_valid\":" << (index.valid_signature ? "true" : "false")
      << ',';
  append_chunks(out, index);
  out << ',';
  append_index_issues(out, index);
  out << '}';
  return out.str();
}

std::string validate_json(const std::string& file,
                          const pnga::png_format::ChunkIndex& index,
                          const pnga::analysis_engine::DocumentValidationReport& report) {
  std::ostringstream out;
  out << '{'
      << "\"file\":\"" << json_escape(file) << "\","
      << "\"size\":" << index.file_size << ","
      << "\"valid\":" << (report.issues.empty() ? "true" : "false")
      << ',';
  append_validation_issues(out, report);
  out << '}';
  return out.str();
}

std::string error_json(const std::string& file, int error_code) {
  std::ostringstream out;
  out << '{'
      << "\"file\":\"" << json_escape(file) << "\","
      << "\"error\":true,"
      << "\"error_code\":" << error_code
      << '}';
  return out.str();
}

}  // namespace pnga::cli
