// Per-frame statistics export schema (WP-APNG-INSPECT contract C3): an
// independent `pnga.frame-statistics` schema, version 1. The section
// payloads reuse the shared statistics section encoding verbatim; the
// envelope adds frame identity, geometry and byte accounting. Static
// serialization output (pnga.statistics v1) is not touched.

#ifndef PNGA_STATISTICS_FRAME_STATISTICS_H
#define PNGA_STATISTICS_FRAME_STATISTICS_H

#include "pnga/statistics/serialization.h"
#include "pnga/statistics/statistics.h"

#include <pnga/trace-model/selection.h>

#include <cstdint>

namespace pnga::statistics {

// One analyzed APNG frame's statistics. `payload_bytes` is the frame
// stream size including the zlib wrapper; `chunk_overhead_bytes` counts the
// owning fcTL (38 bytes) plus 12 bytes per IDAT or 16 bytes per fdAT of the
// frame's own data chunks; `inflated_bytes` is the checked sum of
// height * (1 + row_bytes) over the non-empty Adam7 passes.
struct FrameStatistics {
  DocumentIdentity document;
  pnga::trace_model::ImageIdentity identity =
      pnga::trace_model::AnimationFrame{0};
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t payload_bytes = 0;
  std::uint64_t chunk_overhead_bytes = 0;
  std::uint64_t inflated_bytes = 0;
  StatisticsSnapshot snapshot;
};

// JSON field order: schema, schema_version, document, identity, geometry,
// bytes, sections. The ratio is present only when the snapshot is complete
// and the inflated denominator is positive (serialized as "payload/inflated").
SerializationResult serialize_frame_statistics_json(
    const FrameStatistics& statistics);

// Fixed columns: schema,schema_version,frame_index,section,metric,key,
// value,unit. Missing values stay empty; UTF-8, LF, decimal, single
// trailing LF.
SerializationResult serialize_frame_statistics_csv(
    const FrameStatistics& statistics);

}  // namespace pnga::statistics

#endif  // PNGA_STATISTICS_FRAME_STATISTICS_H
