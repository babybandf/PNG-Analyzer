// WP-602B/602D: the sole deterministic JSON/CSV statistics serializer.
// Byte-identical output independent of locale, clock and path: UTF-8 without
// BOM, LF only, decimal ASCII integers via std::to_chars, fixed field and row
// order, RFC 4180 quoting and exactly one trailing LF. The section payloads
// are shared verbatim with the frame statistics serializer
// (serialization_sections.*); only the envelope differs.

#include "pnga/statistics/serialization.h"

#include "serialization_sections.h"

#include <string>

namespace pnga::statistics {
namespace {

using serialization_sections_internal::append_json_sections;
using serialization_sections_internal::append_u64;
using serialization_sections_internal::append_csv_sections;
using serialization_sections_internal::append_json_string;
using serialization_sections_internal::contradictory_section;
using serialization_sections_internal::csv_field_contains_carriage_return;
using serialization_sections_internal::fail;
using serialization_sections_internal::first_contradictory_section;
using serialization_sections_internal::valid_fingerprint;

constexpr std::string_view kSchemaName = "pnga.statistics";
constexpr std::uint64_t kSchemaVersion = 1;

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

  append_json_sections(out, snapshot);

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
  append_csv_sections(out, scratch, "1", snapshot);

  if (out.find('\r') != std::string::npos) {
    return fail("statistics csv must not contain carriage returns");
  }

  SerializationResult result;
  result.success = true;
  result.bytes = std::move(out);
  return result;
}

}  // namespace pnga::statistics
