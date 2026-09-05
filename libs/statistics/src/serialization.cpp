// WP-602B/602D: the sole deterministic JSON/CSV statistics serializer.
// Byte-identical output independent of locale, clock and path: UTF-8 without
// BOM, LF only, decimal ASCII integers via std::to_chars, fixed field and row
// order, RFC 4180 quoting and exactly one trailing LF.

#include "pnga/statistics/serialization.h"

#include <array>
#include <charconv>
#include <string>
#include <string_view>

namespace pnga::statistics {
namespace {

constexpr std::string_view kSchemaName = "pnga.statistics";
constexpr std::uint64_t kSchemaVersion = 1;
constexpr std::string_view kFingerprintPrefix = "fnv1a64-v1:";
constexpr std::size_t kFingerprintHexLength = 16;

std::string_view status_text(SectionStatus status) noexcept {
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

std::string_view scope_text(SectionScope scope) noexcept {
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

bool valid_fingerprint(std::string_view fingerprint) noexcept {
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

// Only ready + complete + whole_document names a complete section; anything
// else claiming completeness is internally contradictory and rejected.
bool section_state_consistent(const SectionState& state) noexcept {
  if (state.complete && state.status != SectionStatus::kReady) {
    return false;
  }
  if (state.status == SectionStatus::kReady && state.complete &&
      state.scope != SectionScope::kWholeDocument) {
    return false;
  }
  return true;
}

void append_u64(std::string& out, std::uint64_t value) {
  std::array<char, 20> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                    value, 10);
  out.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

void append_json_string(std::string& out, std::string_view value) {
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

bool needs_csv_quoting(std::string_view value) noexcept {
  return value.find_first_of(",\"\n\r") != std::string_view::npos;
}

void append_csv_field(std::string& out, std::string_view value) {
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

void append_csv_row(std::string& out, std::string_view schema_version,
                    std::string_view section, std::string_view metric,
                    std::string_view key, std::string_view value,
                    std::string_view unit) {
  append_csv_field(out, schema_version);
  out.push_back(',');
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

void append_csv_u64_value(std::string& scratch, std::string& out,
                          std::string_view section, std::string_view metric,
                          std::string_view key, std::uint64_t value,
                          std::string_view unit) {
  scratch.clear();
  append_u64(scratch, value);
  append_csv_row(out, "1", section, metric, key, scratch, unit);
}

SerializationResult fail(const char* message) {
  SerializationResult result;
  result.error = message;
  return result;
}

const char* contradictory_section = "contradictory state for section ";

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
bool csv_field_contains_carriage_return(const StatisticsSnapshot& snapshot) {
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
const char* first_contradictory_section(const StatisticsSnapshot& snapshot) {
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
bool begin_json_section(std::string& out, std::string_view indent,
                        std::string_view name, const SectionState& state) {
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

void append_json_bucket_array_open(std::string& out,
                                   std::string_view indent) {
  out += indent;
  out += "  \"buckets\": [";
}

// Closes a bucket array and its section. The section separator comma is
// emitted by the next section, never here.
void append_json_bucket_array_close(std::string& out, std::string_view indent,
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

}  // namespace

SerializationResult serialize_statistics_json(const DocumentIdentity& document,
                                              const StatisticsSnapshot& snapshot) {
  if (!valid_fingerprint(document.fingerprint)) {
    return fail("invalid document fingerprint");
  }
  if (const char* section = first_contradictory_section(snapshot);
      section != nullptr) {
    SerializationResult result;
    result.error = std::string(contradictory_section) + section;
    return result;
  }

  std::string out;
  out += "{\n";
  out += "  \"schema\": ";
  out += '"';
  out += kSchemaName;
  out += "\",\n";
  out += "  \"schema_version\": ";
  append_u64(out, kSchemaVersion);
  out += ",\n";
  out += "  \"document\": {\n";
  out += "    \"file_size\": ";
  append_u64(out, document.file_size);
  out += ",\n";
  out += "    \"fingerprint\": ";
  append_json_string(out, document.fingerprint);
  out += "\n";
  out += "  },\n";
  out += "  \"sections\": {\n";

  const auto numeric = [](std::uint64_t value, bool available) {
    std::string text;
    if (available) {
      append_u64(text, value);
    } else {
      text = "null";
    }
    return text;
  };
  const auto bool_text = [](bool value) { return value ? "true" : "false"; };

  {
    const bool available =
        begin_json_section(out, "    ", "overview", snapshot.overview.state);
    out += "      \"compressed_bytes\": ";
    out += numeric(snapshot.overview.data.compressed_bytes, available);
    out += ",\n";
    out += "      \"inflated_bytes\": ";
    out += numeric(snapshot.overview.data.inflated_bytes, available);
    out += ",\n";
    out += "      \"has_compression_totals\": ";
    out += bool_text(snapshot.overview.data.has_compression_totals);
    out += "\n    }";
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "chunks", snapshot.chunks.state);
    out += "      \"count\": ";
    out += numeric(snapshot.chunks.data.count, available);
    out += ",\n";
    out += "      \"data_bytes\": ";
    out += numeric(snapshot.chunks.data.data_bytes, available);
    out += ",\n";
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.chunks.data.buckets.size(); ++i) {
        const ChunkBucket& bucket = snapshot.chunks.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"type\": ";
        append_json_string(out, bucket.type);
        out += ", \"count\": ";
        append_u64(out, bucket.count);
        out += ", \"data_bytes\": ";
        append_u64(out, bucket.data_bytes);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.chunks.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "filters", snapshot.filters.state);
    out += "      \"rows\": ";
    out += numeric(snapshot.filters.data.rows, available);
    out += ",\n";
    out += "      \"data_bytes\": ";
    out += numeric(snapshot.filters.data.data_bytes, available);
    out += ",\n";
    out += "      \"invalid_rows\": ";
    out += numeric(snapshot.filters.data.invalid_rows, available);
    out += ",\n";
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.filters.data.buckets.size(); ++i) {
        const FilterBucket& bucket = snapshot.filters.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"rows\": ";
        append_u64(out, bucket.rows);
        out += ", \"data_bytes\": ";
        append_u64(out, bucket.data_bytes);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.filters.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "blocks", snapshot.blocks.state);
    out += "      \"count\": ";
    out += numeric(snapshot.blocks.data.count, available);
    out += ",\n";
    out += "      \"compressed_bits\": ";
    out += numeric(snapshot.blocks.data.compressed_bits, available);
    out += ",\n";
    out += "      \"output_bytes\": ";
    out += numeric(snapshot.blocks.data.output_bytes, available);
    out += ",\n";
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.blocks.data.buckets.size(); ++i) {
        const BlockBucket& bucket = snapshot.blocks.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"blocks\": ";
        append_u64(out, bucket.blocks);
        out += ", \"compressed_bits\": ";
        append_u64(out, bucket.compressed_bits);
        out += ", \"output_bytes\": ";
        append_u64(out, bucket.output_bytes);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.blocks.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "tokens", snapshot.tokens.state);
    out += "      \"count\": ";
    out += numeric(snapshot.tokens.data.count, available);
    out += ",\n";
    out += "      \"input_bits\": ";
    out += numeric(snapshot.tokens.data.input_bits, available);
    out += ",\n";
    out += "      \"output_bytes\": ";
    out += numeric(snapshot.tokens.data.output_bytes, available);
    out += ",\n";
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.tokens.data.buckets.size(); ++i) {
        const TokenBucket& bucket = snapshot.tokens.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"count\": ";
        append_u64(out, bucket.count);
        out += ", \"input_bits\": ";
        append_u64(out, bucket.input_bits);
        out += ", \"output_bytes\": ";
        append_u64(out, bucket.output_bytes);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.tokens.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "lengths", snapshot.lengths.state);
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.lengths.data.buckets.size(); ++i) {
        const ValueBucket& bucket = snapshot.lengths.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"value\": ";
        append_u64(out, bucket.value);
        out += ", \"count\": ";
        append_u64(out, bucket.count);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.lengths.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "distances", snapshot.distances.state);
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.distances.data.buckets.size(); ++i) {
        const ValueBucket& bucket = snapshot.distances.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"value\": ";
        append_u64(out, bucket.value);
        out += ", \"count\": ";
        append_u64(out, bucket.count);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.distances.data.buckets.size()
                                             : 0);
  }

  out += "\n  }\n";
  out += "}\n";
  if (out.find('\r') != std::string::npos) {
    return fail("statistics json must not contain carriage returns");
  }

  SerializationResult result;
  result.success = true;
  result.bytes = std::move(out);
  return result;
}

SerializationResult serialize_statistics_csv(const DocumentIdentity& document,
                                             const StatisticsSnapshot& snapshot) {
  if (!valid_fingerprint(document.fingerprint)) {
    return fail("invalid document fingerprint");
  }
  if (const char* section = first_contradictory_section(snapshot);
      section != nullptr) {
    SerializationResult result;
    result.error = std::string(contradictory_section) + section;
    return result;
  }
  if (csv_field_contains_carriage_return(snapshot)) {
    return fail("statistics csv must not contain carriage returns");
  }

  std::string out;
  std::string scratch;
  out += "schema_version,section,metric,key,value,unit\n";
  const auto row = [&](std::string_view section, std::string_view metric,
                       std::string_view key, std::string_view value,
                       std::string_view unit) {
    append_csv_row(out, "1", section, metric, key, value, unit);
  };
  const auto numeric_row = [&](std::string_view section,
                               std::string_view metric, std::string_view key,
                               std::uint64_t value, bool available,
                               std::string_view unit) {
    if (!available) {
      append_csv_row(out, "1", section, metric, key, "", unit);
      return;
    }
    append_csv_u64_value(scratch, out, section, metric, key, value, unit);
  };

  const auto section_header = [&](std::string_view name,
                                  const SectionState& state) {
    row(name, "status", "", status_text(state.status), "");
    row(name, "complete", "", state.complete ? "true" : "false", "");
    row(name, "scope", "", scope_text(state.scope), "");
    if (!state.error.empty()) {
      row(name, "error", "", state.error, "");
    }
    return state.status != SectionStatus::kUnavailable;
  };

  {
    const bool available =
        section_header("overview", snapshot.overview.state);
    numeric_row("overview", "compressed_bytes", "",
                snapshot.overview.data.compressed_bytes, available, "bytes");
    numeric_row("overview", "inflated_bytes", "",
                snapshot.overview.data.inflated_bytes, available, "bytes");
    row("overview", "has_compression_totals", "",
        snapshot.overview.data.has_compression_totals ? "true" : "false", "");
  }
  {
    const bool available = section_header("chunks", snapshot.chunks.state);
    numeric_row("chunks", "count", "", snapshot.chunks.data.count, available,
                "chunks");
    numeric_row("chunks", "data_bytes", "", snapshot.chunks.data.data_bytes,
                available, "bytes");
    if (available) {
      for (const ChunkBucket& bucket : snapshot.chunks.data.buckets) {
        append_csv_u64_value(scratch, out, "chunks", "bucket.count", bucket.type,
                             bucket.count, "chunks");
        append_csv_u64_value(scratch, out, "chunks", "bucket.data_bytes",
                             bucket.type, bucket.data_bytes, "bytes");
      }
    }
  }
  {
    const bool available = section_header("filters", snapshot.filters.state);
    numeric_row("filters", "rows", "", snapshot.filters.data.rows, available,
                "rows");
    numeric_row("filters", "data_bytes", "", snapshot.filters.data.data_bytes,
                available, "bytes");
    numeric_row("filters", "invalid_rows", "",
                snapshot.filters.data.invalid_rows, available, "rows");
    if (available) {
      for (std::size_t i = 0; i < snapshot.filters.data.buckets.size(); ++i) {
        const FilterBucket& bucket = snapshot.filters.data.buckets[i];
        scratch.clear();
        append_u64(scratch, i);
        const std::string key = scratch;
        append_csv_u64_value(scratch, out, "filters", "bucket.rows", key,
                             bucket.rows, "rows");
        append_csv_u64_value(scratch, out, "filters", "bucket.data_bytes", key,
                             bucket.data_bytes, "bytes");
      }
    }
  }
  {
    const bool available = section_header("blocks", snapshot.blocks.state);
    numeric_row("blocks", "count", "", snapshot.blocks.data.count, available,
                "blocks");
    numeric_row("blocks", "compressed_bits", "",
                snapshot.blocks.data.compressed_bits, available, "bits");
    numeric_row("blocks", "output_bytes", "",
                snapshot.blocks.data.output_bytes, available, "bytes");
    if (available) {
      static constexpr std::string_view kBlockKeys[] = {"stored", "fixed",
                                                        "dynamic"};
      for (std::size_t i = 0; i < snapshot.blocks.data.buckets.size() &&
                               i < std::size(kBlockKeys);
           ++i) {
        const BlockBucket& bucket = snapshot.blocks.data.buckets[i];
        append_csv_u64_value(scratch, out, "blocks", "bucket.blocks",
                             kBlockKeys[i], bucket.blocks, "blocks");
        append_csv_u64_value(scratch, out, "blocks", "bucket.compressed_bits",
                             kBlockKeys[i], bucket.compressed_bits, "bits");
        append_csv_u64_value(scratch, out, "blocks", "bucket.output_bytes",
                             kBlockKeys[i], bucket.output_bytes, "bytes");
      }
    }
  }
  {
    const bool available = section_header("tokens", snapshot.tokens.state);
    numeric_row("tokens", "count", "", snapshot.tokens.data.count, available,
                "tokens");
    numeric_row("tokens", "input_bits", "", snapshot.tokens.data.input_bits,
                available, "bits");
    numeric_row("tokens", "output_bytes", "",
                snapshot.tokens.data.output_bytes, available, "bytes");
    if (available) {
      static constexpr std::string_view kTokenKeys[] = {"literal", "match",
                                                        "eob"};
      for (std::size_t i = 0; i < snapshot.tokens.data.buckets.size() &&
                               i < std::size(kTokenKeys);
           ++i) {
        const TokenBucket& bucket = snapshot.tokens.data.buckets[i];
        append_csv_u64_value(scratch, out, "tokens", "bucket.count",
                             kTokenKeys[i], bucket.count, "tokens");
        append_csv_u64_value(scratch, out, "tokens", "bucket.input_bits",
                             kTokenKeys[i], bucket.input_bits, "bits");
        append_csv_u64_value(scratch, out, "tokens", "bucket.output_bytes",
                             kTokenKeys[i], bucket.output_bytes, "bytes");
      }
    }
  }
  {
    const bool available = section_header("lengths", snapshot.lengths.state);
    if (available) {
      for (const ValueBucket& bucket : snapshot.lengths.data.buckets) {
        scratch.clear();
        append_u64(scratch, bucket.value);
        const std::string key = scratch;
        append_csv_u64_value(scratch, out, "lengths", "bucket.count", key,
                             bucket.count, "matches");
      }
    }
  }
  {
    const bool available = section_header("distances", snapshot.distances.state);
    if (available) {
      for (const ValueBucket& bucket : snapshot.distances.data.buckets) {
        scratch.clear();
        append_u64(scratch, bucket.value);
        const std::string key = scratch;
        append_csv_u64_value(scratch, out, "distances", "bucket.count", key,
                             bucket.count, "matches");
      }
    }
  }

  if (out.find('\r') != std::string::npos) {
    return fail("statistics csv must not contain carriage returns");
  }

  SerializationResult result;
  result.success = true;
  result.bytes = std::move(out);
  return result;
}

}  // namespace pnga::statistics
