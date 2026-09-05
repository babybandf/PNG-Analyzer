#ifndef PNGA_STATISTICS_SERIALIZATION_H
#define PNGA_STATISTICS_SERIALIZATION_H

// WP-602B/602D: the sole deterministic JSON/CSV statistics serializer. GUI
// and CLI must both serialize immutable snapshots through this Qt-free
// implementation and must never assemble their own fields.

#include <cstdint>
#include <string>

#include <pnga/statistics/statistics.h>

namespace pnga::statistics {

// Content-derived correlation identity of the analyzed source. The
// fingerprint format is "fnv1a64-v1:<16 lowercase hex>"; file_size is the
// 64-bit source size. Neither field encodes a path, mtime, locale or clock.
struct DocumentIdentity {
  std::uint64_t file_size = 0;
  std::string fingerprint;
};

struct SerializationResult {
  bool success = false;
  std::string bytes;
  std::string error;
};

// Schema v1 JSON: envelope {schema, schema_version, document{file_size,
// fingerprint}, sections{...}} in fixed field order, UTF-8 without BOM, LF
// line endings, decimal ASCII integers and exactly one trailing LF. Returns
// either the complete bytes or a stable error; never writes files.
SerializationResult serialize_statistics_json(
    const DocumentIdentity& document, const StatisticsSnapshot& snapshot);

// Schema v1 CSV with exactly the columns
// schema_version,section,metric,key,value,unit. Status, complete and scope
// precede the totals and buckets of every section; unavailable numeric values
// use an empty value field so empty stays distinct from zero. Same byte
// contract as the JSON output.
SerializationResult serialize_statistics_csv(
    const DocumentIdentity& document, const StatisticsSnapshot& snapshot);

}  // namespace pnga::statistics

#endif  // PNGA_STATISTICS_SERIALIZATION_H
