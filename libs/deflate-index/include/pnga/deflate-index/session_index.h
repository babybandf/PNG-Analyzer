#ifndef PNGA_DEFLATE_INDEX_SESSION_INDEX_H
#define PNGA_DEFLATE_INDEX_SESSION_INDEX_H

// WP-404: session inflateCopy snapshot index (REPOSITORY_LAYOUT.md §5.6,
// ADR-0006). Supplements the portable access points (WP-402) for streams with
// very large single blocks, where block-boundary points are sparse. Snapshots
// are process-local (deflate_runtime::InflateSnapshot), never written to disk
// and never reused across zlib versions; the compressed input is retained in
// memory for the session. On memory-budget exhaustion fewer snapshots are kept
// (larger replay gaps); the portable access points remain the durable fallback.

#include <pnga/deflate-runtime/inflate_snapshot.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::deflate_index {

struct SessionSnapshot {
  std::uint64_t output_offset = 0;
  pnga::deflate_runtime::InflateSnapshot state;  // captured inflate state
};

struct SessionIndexResult {
  bool success = false;
  std::string error;
  std::vector<std::byte> compressed;       // retained input (snapshots reference it)
  std::vector<SessionSnapshot> snapshots;  // sorted by output_offset; first at 0
  std::uint64_t total_output_bytes = 0;
  std::uint64_t retained_bytes = 0;        // snapshot memory (budget accounting)
  bool adler_ok = true;
};

struct SessionExtractResult {
  bool success = false;
  std::string error;
  std::vector<std::byte> data;
};

// Builds the session index over the whole `compressed` zlib stream (retained
// in memory). Captures an inflateCopy snapshot at output intervals of at least
// `interval_bytes` until `snapshot_budget_bytes` of snapshot memory is spent;
// the snapshot at output offset 0 is always kept. `max_output_bytes` caps
// total inflated output.
SessionIndexResult build_session_index(const std::vector<std::byte>& compressed,
                                       std::uint64_t max_output_bytes,
                                       std::uint64_t interval_bytes,
                                       std::uint64_t snapshot_budget_bytes);

// Extracts `length` inflated bytes at `output_offset` from the nearest snapshot.
SessionExtractResult extract_session_output(const SessionIndexResult& index,
                                            std::uint64_t output_offset,
                                            std::uint64_t length);

}  // namespace pnga::deflate_index

#endif  // PNGA_DEFLATE_INDEX_SESSION_INDEX_H
