// Per-frame statistics collection (WP-APNG-INSPECT contract C3). The
// collector runs in a worker: it reuses the shared StatisticsAccumulator,
// scopes chunk statistics to the frame's own data chunks plus the owning
// fcTL, and never modifies collect_document_statistics semantics.

#ifndef PNGA_ANALYSIS_ENGINE_FRAME_STATISTICS_H
#define PNGA_ANALYSIS_ENGINE_FRAME_STATISTICS_H

#include "pnga/analysis-engine/analysis_target.h"
#include "pnga/analysis-engine/frame_analysis.h"
#include "pnga/analysis-engine/job_scheduler.h"

#include <pnga/statistics/frame_statistics.h>
#include <pnga/statistics/statistics.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pnga::analysis_engine {

struct FrameStatisticsRequest {
  std::shared_ptr<const AnalysisTarget> target;
  std::shared_ptr<const FrameStageSet> frame;
  pnga::statistics::DocumentIdentity document;
  pnga::statistics::StatisticsLimits limits;
  // Declared background working memory; must not exceed the 64 MiB cap.
  std::uint64_t max_working_bytes = 64ull << 20;
  // Deterministic monotonic-clock seam in milliseconds for progress
  // throttling. Empty uses std::chrono::steady_clock.
  std::function<std::uint64_t()> monotonic_millis;
};

struct FrameStatisticsResult {
  pnga::trace_model::AnalysisKey key;
  pnga::statistics::FrameStatistics value;
  std::string error;
};

using FrameStatisticsProgress =
    std::function<void(const FrameStatisticsResult&)>;

// Collects the frame statistics. Progress is published at most once per
// 100 ms (first publish unconditional) and the result carries the verified
// prefix when a budget or cancellation stops the scan. On failure the
// result carries the target key and a stable error, never a half-valid
// value presented as complete.
FrameStatisticsResult collect_frame_statistics(
    const FrameStatisticsRequest& request, const CancellationToken* cancellation,
    FrameStatisticsProgress on_progress = {});

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_FRAME_STATISTICS_H
