// Internal shared section encoding for the statistics serializers. The
// static v1 serializer (pnga.statistics) and the frame serializer
// (pnga.frame-statistics) emit identical section payloads; only the
// envelope differs. Everything here was extracted verbatim from
// serialization.cpp (WP-APNG-INSPECT T05: shared section-code extraction;
// the v1 goldens must stay byte-identical).

#ifndef PNGA_STATISTICS_SRC_SERIALIZATION_SECTIONS_H
#define PNGA_STATISTICS_SRC_SERIALIZATION_SECTIONS_H

#include "pnga/statistics/serialization.h"
#include "pnga/statistics/statistics.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace pnga::statistics::serialization_sections_internal {

constexpr std::string_view kFingerprintPrefix = "fnv1a64-v1:";
constexpr std::size_t kFingerprintHexLength = 16;

inline bool valid_fingerprint(std::string_view fingerprint) noexcept {
  if (fingerprint.size() != kFingerprintPrefix.size() + kFingerprintHexLength) {
    return false;
  }
  if (fingerprint.substr(0, kFingerprintPrefix.size()) != kFingerprintPrefix) {
    return false;
  }
  for (const char c :
       fingerprint.substr(kFingerprintPrefix.size(), kFingerprintHexLength)) {
    const bool lower_hex =
        (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!lower_hex) {
      return false;
    }
  }
  return true;
}

inline std::string_view status_text(SectionStatus status) noexcept {
  switch (status) {
    case SectionStatus::kUnavailable:
      return "unavailable";
    case SectionStatus::kReady:
      return "ready";
    case SectionStatus::kPartial:
      return "partial";
    case SectionStatus::kCancelled:
      return "cancelled";
    case SectionStatus::kBudgetExceeded:
      return "budget_exceeded";
    case SectionStatus::kInvalidInput:
      return "invalid_input";
    case SectionStatus::kOverflow:
      return "overflow";
    case SectionStatus::kError:
      return "error";
  }
  return "error";
}

inline std::string_view scope_text(SectionScope scope) noexcept {
  switch (scope) {
    case SectionScope::kNone:
      return "none";
    case SectionScope::kWholeDocument:
      return "whole_document";
    case SectionScope::kVerifiedPrefix:
      return "verified_prefix";
  }
  return "none";
}

// Only ready + complete + whole_document names a complete section; anything
// else claiming completeness is internally contradictory and rejected.
inline bool section_state_consistent(const SectionState& state) noexcept {
  if (state.complete && state.status != SectionStatus::kReady) {
    return false;
  }
  if (state.status == SectionStatus::kReady && state.complete &&
      state.scope != SectionScope::kWholeDocument) {
    return false;
  }
  return true;
}

inline void append_u64(std::string& out, std::uint64_t value) {
  std::array<char, 20> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                    value, 10);
  out.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

inline void append_json_string(std::string& out, std::string_view value) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  out.push_back('"');
  for (const char c : value) {
    const auto uc = static_cast<unsigned char>(c);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (uc < 0x20) {
          out += "\\u00";
          out.push_back(kHexDigits[(uc >> 4) & 0xFu]);
          out.push_back(kHexDigits[uc & 0xFu]);
        } else {
          out.push_back(c);  // UTF-8 bytes pass through unchanged
        }
    }
  }
  out.push_back('"');
}

inline bool needs_csv_quoting(std::string_view value) noexcept {
  return value.find_first_of(",\"\n\r") != std::string_view::npos;
}

inline void append_csv_field(std::string& out, std::string_view value) {
  if (needs_csv_quoting(value)) {
    out.push_back('"');
    for (const char c : value) {
      if (c == '"') {
        out.push_back('"');
      }
      out.push_back(c);
    }
    out.push_back('"');
  } else {
    out.append(value);
  }
}

// Writes one CSV record: `leading_cells` (already formatted, comma
// separated) followed by the shared section/metric/key/value/unit cells.
inline void append_csv_row_cells(std::string& out,
                                 std::string_view leading_cells,
                                 std::string_view section,
                                 std::string_view metric,
                                 std::string_view key,
                                 std::string_view value,
                                 std::string_view unit) {
  out.append(leading_cells);
  if (!leading_cells.empty() && leading_cells.back() != ',') {
    out.push_back(',');
  }
  append_csv_field(out, section);
  out.push_back(',');
  append_csv_field(out, metric);
  out.push_back(',');
  append_csv_field(out, key);
  out.push_back(',');
  append_csv_field(out, value);
  out.push_back(',');
  append_csv_field(out, unit);
  out.push_back('\n');
}

inline void append_csv_u64_value(std::string& scratch, std::string& out,
                                 std::string_view leading_cells,
                                 std::string_view section,
                                 std::string_view metric,
                                 std::string_view key, std::uint64_t value,
                                 std::string_view unit) {
  scratch.clear();
  append_u64(scratch, value);
  append_csv_row_cells(out, leading_cells, section, metric, key, scratch, unit);
}

inline SerializationResult fail(const char* message) {
  SerializationResult result;
  result.error = message;
  return result;
}

constexpr const char* contradictory_section = "contradictory state for section ";

// The CSV byte contract is "LF only": no carriage-return byte may appear in
// the output, including inside quoted fields. RFC 4180 defines no escape for
// CR (quoting only neutralizes quote characters and may embed line breaks),
// so a CR-bearing field is rejected deterministically instead of being
// mangled into a non-LF-only stream. An embedded LF inside a quoted field is
// deliberately kept: it is legal per RFC 4180, it is field content rather
// than a record separator, and the schema v1 golden case is required to
// carry LF in a synthetic error. Bucket keys and error strings are untrusted
// input, so they are scanned up front and the finished output is checked
// again (mirroring the JSON side) before the bytes are published.
inline bool csv_field_contains_carriage_return(
    const StatisticsSnapshot& snapshot) {
  const auto has_cr = [](std::string_view value) {
    return value.find('\r') != std::string_view::npos;
  };
  if (has_cr(snapshot.overview.state.error) ||
      has_cr(snapshot.chunks.state.error) ||
      has_cr(snapshot.filters.state.error) ||
      has_cr(snapshot.blocks.state.error) ||
      has_cr(snapshot.tokens.state.error) ||
      has_cr(snapshot.lengths.state.error) ||
      has_cr(snapshot.distances.state.error)) {
    return true;
  }
  for (const ChunkBucket& bucket : snapshot.chunks.data.buckets) {
    if (has_cr(bucket.type)) {
      return true;
    }
  }
  return false;
}

// Validates every section state up front so a rejected snapshot never
// produces partial bytes.
inline const char* first_contradictory_section(
    const StatisticsSnapshot& snapshot) {
  if (!section_state_consistent(snapshot.overview.state)) return "overview";
  if (!section_state_consistent(snapshot.chunks.state)) return "chunks";
  if (!section_state_consistent(snapshot.filters.state)) return "filters";
  if (!section_state_consistent(snapshot.blocks.state)) return "blocks";
  if (!section_state_consistent(snapshot.tokens.state)) return "tokens";
  if (!section_state_consistent(snapshot.lengths.state)) return "lengths";
  if (!section_state_consistent(snapshot.distances.state)) return "distances";
  return nullptr;
}

// Section headers: status, complete, scope and a non-empty error, in that
// order, followed by the section data. Returns whether data values exist
// (the section is not unavailable).
inline bool begin_json_section(std::string& out, std::string_view indent,
                               std::string_view name,
                               const SectionState& state) {
  out += indent;
  out.push_back('"');
  out += name;
  out += "\": {\n";
  out += indent;
  out += "  \"status\": ";
  out += '"';
  out += status_text(state.status);
  out += "\",\n";
  out += indent;
  out += "  \"complete\": ";
  out += state.complete ? "true" : "false";
  out += ",\n";
  out += indent;
  out += "  \"scope\": ";
  out += '"';
  out += scope_text(state.scope);
  out += '"';
  if (!state.error.empty()) {
    out += ",\n";
    out += indent;
    out += "  \"error\": ";
    append_json_string(out, state.error);
  }
  out += ",\n";
  return state.status != SectionStatus::kUnavailable;
}

inline void append_json_bucket_array_open(std::string& out,
                                          std::string_view indent) {
  out += indent;
  out += "  \"buckets\": [";
}

// Closes a bucket array and its section. The section separator comma is
// emitted by the next section, never here.
inline void append_json_bucket_array_close(std::string& out,
                                           std::string_view indent,
                                           std::size_t count) {
  if (count != 0) {
    out += '\n';
    out += indent;
    out += "  ";
  }
  out += ']';
  out += '\n';
  out += indent;
  out += '}';
}

// Emits the seven section objects (without the enclosing "sections" braces
// and the document-level closure, which belong to the envelope).
void append_json_sections(std::string& out, const StatisticsSnapshot& snapshot);

// Emits the seven section row groups. `leading_cells` are the cells printed
// before the section column ("1" for schema v1,
// "pnga.frame-statistics,1,<index>" for the frame schema).
void append_csv_sections(std::string& out, std::string& scratch,
                         std::string_view leading_cells,
                         const StatisticsSnapshot& snapshot);

}  // namespace pnga::statistics::serialization_sections_internal

#endif  // PNGA_STATISTICS_SRC_SERIALIZATION_SECTIONS_H
