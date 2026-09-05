#ifndef PNGA_ANALYSIS_ENGINE_STATISTICS_COLLECTOR_H
#define PNGA_ANALYSIS_ENGINE_STATISTICS_COLLECTOR_H

// WP-602C: lazy, cancelable whole-document statistics collection. The
// collector combines the cached Chunk index and StageSet facts with a
// streaming scalar token pass over the virtual IDAT stream (never a
// concatenated payload), aggregates everything through the bounded
// StatisticsAccumulator and publishes throttled immutable progress copies.
// Qt-free (ADR-0003); the GUI drives it from a background worker.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/statistics/statistics.h>
#include <pnga/statistics/serialization.h>

namespace pnga::analysis_engine {

// One throttled progress publication. `section` identifies the stage that
// advanced; `total` is absent when the total is unknown up front (tokens).
struct StatisticsProgress {
  pnga::statistics::StatisticsSectionId section;
  std::uint64_t processed = 0;
  std::optional<std::uint64_t> total;
};

struct StatisticsCollectionRequest {
  std::uint64_t generation = 0;
  std::shared_ptr<const pnga::io::IByteSource> source;
  pnga::png_format::ChunkIndex chunks;
  std::shared_ptr<const StageSet> stages;
  pnga::statistics::StatisticsLimits limits;
  // Declared background working memory; must not exceed the 64 MiB cap.
  std::uint64_t max_working_bytes = 64ull << 20;
  // Deterministic monotonic-clock seam in milliseconds for progress
  // throttling. Empty uses std::chrono::steady_clock; tests inject a fake
  // monotonic clock. Values must behave monotonically.
  std::function<std::uint64_t()> monotonic_millis;
};

struct StatisticsCollectionResult {
  std::uint64_t generation = 0;
  pnga::statistics::DocumentIdentity document;
  pnga::statistics::StatisticsSnapshot snapshot;
};

// Receives value copies of the result; never mutable accumulator storage and
// never invoked while holding a decoder or scheduler lock.
using StatisticsProgressCallback =
    std::function<void(const StatisticsCollectionResult&,
                       const StatisticsProgress&)>;

// Collects whole-document statistics synchronously. Progress is published
// after the fast sections (overview/chunks/filters/blocks) and during the
// token scan no more than once per 100 ms on the injected monotonic clock;
// the first publication is unconditional. Cancellation is cooperative: the
// token is checked at section boundaries, every 256 samples and during the
// token scan; completed sections stay ready and the remaining sections keep
// their collected verified prefix. Rejecting requests (working-memory cap,
// zero budgets) and lower-level failures map to the frozen section statuses
// without upgrading partial results to ready.
StatisticsCollectionResult collect_document_statistics(
    const StatisticsCollectionRequest& request,
    const CancellationToken* cancellation,
    StatisticsProgressCallback on_progress = {});

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_STATISTICS_COLLECTOR_H
