#include "pnga/statistics/frame_statistics.h"

#include "serialization_sections.h"

#include <string>

namespace pnga::statistics {
namespace {

using serialization_sections_internal::append_csv_row_cells;
using serialization_sections_internal::append_json_sections;
using serialization_sections_internal::append_u64;
using serialization_sections_internal::append_csv_sections;
using serialization_sections_internal::append_json_string;
using serialization_sections_internal::csv_field_contains_carriage_return;
using serialization_sections_internal::fail;
using serialization_sections_internal::first_contradictory_section;
using serialization_sections_internal::valid_fingerprint;

constexpr std::string_view kFrameSchemaName = "pnga.frame-statistics";
constexpr std::uint64_t kFrameSchemaVersion = 1;

bool frame_index_of(const pnga::trace_model::ImageIdentity& identity,
                    std::uint32_t* index) {
  const auto* frame =
      std::get_if<pnga::trace_model::AnimationFrame>(&identity);
  if (frame == nullptr) {
    return false;
  }
  *index = frame->index;
  return true;
}

bool ratio_available(const FrameStatistics& statistics) {
  return statistics.snapshot.complete() && statistics.inflated_bytes > 0;
}

std::string ratio_text(const FrameStatistics& statistics) {
  std::string out;
  append_u64(out, statistics.payload_bytes);
  out += '/';
  append_u64(out, statistics.inflated_bytes);
  return out;
}

}  // namespace

SerializationResult serialize_frame_statistics_json(
    const FrameStatistics& statistics) {
  std::uint32_t frame_index = 0;
  if (!frame_index_of(statistics.identity, &frame_index)) {
    return fail("frame statistics require an animation frame identity");
  }
  if (!valid_fingerprint(statistics.document.fingerprint)) {
    return fail("invalid document fingerprint");
  }
  if (const char* section = first_contradictory_section(statistics.snapshot);
      section != nullptr) {
    SerializationResult result;
    result.error = std::string(serialization_sections_internal::contradictory_section) +
                   section;
    return result;
  }

  std::string out;
  out += "{\n";
  out += "  \"schema\": ";
  out += '"';
  out += kFrameSchemaName;
  out += "\",\n";
  out += "  \"schema_version\": ";
  append_u64(out, kFrameSchemaVersion);
  out += ",\n";
  out += "  \"document\": {\n";
  out += "    \"file_size\": ";
  append_u64(out, statistics.document.file_size);
  out += ",\n";
  out += "    \"fingerprint\": ";
  append_json_string(out, statistics.document.fingerprint);
  out += "\n";
  out += "  },\n";
  out += "  \"identity\": {\n";
  out += "    \"kind\": \"animation_frame\",\n";
  out += "    \"index\": ";
  append_u64(out, frame_index);
  out += "\n";
  out += "  },\n";
  out += "  \"geometry\": {\n";
  out += "    \"width\": ";
  append_u64(out, statistics.width);
  out += ",\n";
  out += "    \"height\": ";
  append_u64(out, statistics.height);
  out += "\n";
  out += "  },\n";
  out += "  \"bytes\": {\n";
  out += "    \"payload\": ";
  append_u64(out, statistics.payload_bytes);
  out += ",\n";
  out += "    \"chunk_overhead\": ";
  append_u64(out, statistics.chunk_overhead_bytes);
  out += ",\n";
  out += "    \"inflated\": ";
  append_u64(out, statistics.inflated_bytes);
  out += ",\n";
  out += "    \"ratio\": ";
  if (ratio_available(statistics)) {
    append_json_string(out, ratio_text(statistics));
  } else {
    out += "null";
  }
  out += "\n";
  out += "  },\n";
  out += "  \"sections\": {\n";

  append_json_sections(out, statistics.snapshot);

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

SerializationResult serialize_frame_statistics_csv(
    const FrameStatistics& statistics) {
  std::uint32_t frame_index = 0;
  if (!frame_index_of(statistics.identity, &frame_index)) {
    return fail("frame statistics require an animation frame identity");
  }
  if (!valid_fingerprint(statistics.document.fingerprint)) {
    return fail("invalid document fingerprint");
  }
  if (const char* section = first_contradictory_section(statistics.snapshot);
      section != nullptr) {
    SerializationResult result;
    result.error = std::string(serialization_sections_internal::contradictory_section) +
                   section;
    return result;
  }
  if (csv_field_contains_carriage_return(statistics.snapshot)) {
    return fail("statistics csv must not contain carriage returns");
  }

  std::string out;
  std::string scratch;
  out += "schema,schema_version,frame_index,section,metric,key,value,unit\n";
  std::string leading;
  leading += kFrameSchemaName;
  leading += ',';
  append_u64(leading, kFrameSchemaVersion);
  leading += ',';
  append_u64(leading, frame_index);
  leading += ',';

  const auto frame_row = [&](std::string_view metric, std::string_view value,
                             std::string_view unit) {
    append_csv_row_cells(out, leading, "frame", metric, "", value, unit);
  };
  const auto frame_u64_row = [&](std::string_view metric, std::uint64_t value,
                                 std::string_view unit) {
    scratch.clear();
    serialization_sections_internal::append_u64(scratch, value);
    append_csv_row_cells(out, leading, "frame", metric, "", scratch, unit);
  };

  frame_row("kind", "animation_frame", "");
  frame_u64_row("index", frame_index, "frames");
  frame_u64_row("width", statistics.width, "px");
  frame_u64_row("height", statistics.height, "px");
  frame_u64_row("payload", statistics.payload_bytes, "bytes");
  frame_u64_row("chunk_overhead", statistics.chunk_overhead_bytes, "bytes");
  frame_u64_row("inflated", statistics.inflated_bytes, "bytes");
  frame_row("ratio", ratio_available(statistics)
                         ? ratio_text(statistics)
                         : std::string{},
            "");

  append_csv_sections(out, scratch, leading, statistics.snapshot);

  if (out.find('\r') != std::string::npos) {
    return fail("statistics csv must not contain carriage returns");
  }

  SerializationResult result;
  result.success = true;
  result.bytes = std::move(out);
  return result;
}

}  // namespace pnga::statistics
