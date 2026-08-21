#ifndef PNGA_DEFLATE_RUNTIME_INFLATE_SNAPSHOT_H
#define PNGA_DEFLATE_RUNTIME_INFLATE_SNAPSHOT_H

// WP-404: session inflateCopy snapshots (REPOSITORY_LAYOUT.md §5.5, ADR-0006).
// A portable checkpoint (WP-402) can only sit at Deflate block boundaries, so a
// single huge block leaves large replay gaps. An inflateCopy snapshot captures
// the exact z_stream state at any point, allowing near-random restart within a
// block. Snapshots are process-local only: never written to disk and never
// reused across zlib versions (the copied state is opaque to callers).

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace pnga::deflate_runtime {

// An opaque, move-only, process-local copy of an inflate z_stream state.
// The captured stream may be destroyed after capture; the snapshot owns its
// own copy of the state (including the 32 KiB window).
class InflateSnapshot {
 public:
  InflateSnapshot() = default;
  ~InflateSnapshot();

  InflateSnapshot(const InflateSnapshot&) = delete;
  InflateSnapshot& operator=(const InflateSnapshot&) = delete;
  InflateSnapshot(InflateSnapshot&&) noexcept;
  InflateSnapshot& operator=(InflateSnapshot&&) noexcept;

  // Captures the current state of `stream` (inflate-initialized) via
  // inflateCopy. `output_offset` is the inflated byte position at capture time
  // (caller tracks total_out). Returns nullopt when inflateCopy fails.
  static std::optional<InflateSnapshot> capture(z_stream& stream,
                                                std::uint64_t output_offset);

  // Copies this snapshot's state into `dst` (which must be inflate-initialized)
  // via inflateCopy; `dst` can then continue inflating as if it had been the
  // captured stream. Returns false on failure.
  bool restore(z_stream& dst) const;

  std::uint64_t output_offset() const noexcept { return output_offset_; }

  // Approximate retained memory (window + stream state), for budget accounting.
  std::size_t approx_bytes() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::uint64_t output_offset_ = 0;
};

}  // namespace pnga::deflate_runtime

#endif  // PNGA_DEFLATE_RUNTIME_INFLATE_SNAPSHOT_H
